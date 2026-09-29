/*
    melonDS Multiplayer - real game session runner.

    The diagnostic ROM proves the plumbing works; this is what tells us whether
    actual games survive it. It boots a commercial ROM into one or more emulated
    consoles, optionally taps buttons on a schedule so the game can be driven to
    its multiplayer menu, and reports what happened: did it keep running, is the
    picture changing, did the game power its wireless on, and is the link
    actually carrying traffic.

    It also times the run, because "can this machine host four consoles" is a
    question best answered with a number.

    Usage:
        md_game_session <rom> [options]

    Options:
        --players N              number of consoles (default 1)
        --frames N               emulated frames to run (default 3600, ~60s)
        --shot-dir DIR           write raw frames here
        --shot-every K           capture every K frames (default 0 = off)
        --tap FRAME:NAMES[@P]    tap NAMES for 6 frames starting at FRAME
        --hold FRAME:NAMES[@P]   hold NAMES down from FRAME onwards
        --autotap NAMES:START:PERIOD[@P]
                                 tap NAMES repeatedly, starting at START
        --touch FRAME:X:Y[@P]    press the touch screen at X,Y for 6 frames
        --holdtouch FRAME:X:Y[@P]  press and keep pressing from FRAME onwards
        --touchtap X:Y:START:PERIOD[@P]
                                 press X,Y repeatedly, starting at START
        --stop-input FRAME       end every input scheduled to begin at or
                                 before FRAME at FRAME. The menu route that
                                 walks a game into multiplayer is a repeating
                                 tap, and a repeating tap never stops; this is
                                 how one run gets "the route, then nothing"
        --all-players            apply input to every console, not just 1
        --serial                 run the consoles one after another instead of
                                 one thread each (see the note below)
        --recv-timeout MS        how long the link blocks waiting for a peer
                                 (default 0: poll; see the note below)
        --reply-timeout MS       how long the host waits to collect clients'
                                 replies (default 2ms; see the note below)
        --no-jit                 run the ARM interpreter instead (the JIT is on
                                 by default; a title that misbehaves under it can
                                 be checked without a rebuild)
        --pc-every N             print both cores' program counters every N
                                 frames; a game that has stopped responding is
                                 usually a core spinning in one place, and which
                                 core it is says where to look
        --quiet                  only the summary

    With more than one console every instance gets its own thread and the
    console threads meet at a per-frame barrier. That is not a performance
    detail, it is what makes linked play work at all: a console that has to wait
    for its peer's wireless frame can only be answered while the peer is
    actually running. Running the consoles in sequence instead makes every
    blocking receive wait out its full real-time timeout, which is orders of
    magnitude slower - a session that takes twenty seconds threaded does not
    finish in ten minutes serial.

    Threading is necessary but not sufficient. melonDS's local-multiplayer bus
    defaults to a 25ms BLOCKING receive, which is right for    its own frontend: several consoles running on real-time threads, where the host
    really does put a beacon on the wire a few milliseconds later. In a lockstep
    host it deadlocks by construction - a client waiting for the host's beacon
    blocks, and the host is waiting at the frame barrier for that client to finish
    its frame. The counter in USTimer only advances when a receive succeeds, so
    the client then blocks again on the very next tick: roughly four seconds per
    frame, which looks exactly like a hang. Defaulting that timeout to 0 makes the
    receive a poll, and a frame the peer has not produced yet simply arrives next
    frame.

    The host's reply collection is the opposite case and keeps a small wait. The
    host asks for replies once per command slot and the clients' replies are
    genuinely racing it, so polling there means the host keeps giving up on
    handshakes it could have completed - which is what left a two-console session
    stranded on the character-select screen.

    Button names: A B X Y L R UP DOWN LEFT RIGHT START SELECT

    The optional @P suffix names the consoles to drive, 1-based and comma
    separated ("@2", "@1,2"). Without it, input goes to player 1, or to every
    console when --all-players is given. Touch coordinates are touch-screen
    pixels: 0..255 across, 0..191 down, origin top-left.
*/

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "melonds_bridge.h"

namespace
{

/* The app charges a frame's reply waits to that frame's own budget
   (MPInterface::SetReplyBudget): a wait is wall-clock time spent inside an
   emulated frame, and an unbounded one took a real MKDS join to 9fps. This
   harness has no pace timer to take that slack from - it runs as fast as the
   machine allows - so it publishes the app's default, two DS frames at DS
   speed (33ms), and can be overridden to anything: MD_REPLY_BUDGET_NS=-1
   restores the unbounded behaviour this replaced. */
int64_t reply_budget_ns()
{
    static const int64_t budget = []() {
        const char* v = std::getenv("MD_REPLY_BUDGET_NS");
        if (v && *v) return (int64_t)strtoll(v, nullptr, 10);
        return (int64_t)(2 * 1000000000LL * 889 / 53150);
    }();
    return budget;
}

const std::map<std::string, uint32_t> BUTTONS = {
    { "A", MD_BTN_A }, { "B", MD_BTN_B }, { "X", MD_BTN_X }, { "Y", MD_BTN_Y },
    { "L", MD_BTN_L }, { "R", MD_BTN_R },
    { "UP", MD_BTN_UP }, { "DOWN", MD_BTN_DOWN },
    { "LEFT", MD_BTN_LEFT }, { "RIGHT", MD_BTN_RIGHT },
    { "START", MD_BTN_START }, { "SELECT", MD_BTN_SELECT },
};

uint32_t parse_buttons(const std::string& names, bool& ok)
{
    uint32_t mask = 0;
    std::stringstream ss(names);
    std::string name;

    while (std::getline(ss, name, '+'))
    {
        /* Tolerate the separator being omitted, e.g. "A B". */
        std::stringstream inner(name);
        std::string part;
        while (inner >> part)
        {
            auto it = BUTTONS.find(part);
            if (it == BUTTONS.end())
            {
                std::printf("unknown button '%s'\n", part.c_str());
                ok = false;
                return 0;
            }
            mask |= it->second;
        }
    }

    if (!mask) { ok = false; return 0; }
    return mask;
}

/* Which consoles an input event is delivered to, as a bitmask over player
   indexes. Kept explicit so a session can drive a host differently from the
   consoles joining it. */
using PlayerMask = uint32_t;

struct Tap
{
    int first_frame;
    int last_frame;    /* exclusive; 0 means "runs to the end" */
    uint32_t buttons;
    PlayerMask players;
    int period = 0;    /* 0 = one-shot */
    bool hold = false;
};

struct Touch
{
    int first_frame;
    int last_frame;    /* exclusive; 0 means "runs to the end" */
    int x;
    int y;
    PlayerMask players;
    int period = 0;    /* 0 = one-shot */
    bool hold = false;
};

/* Sense-reversing barrier: every participant must arrive before any of them
   continues, and the same object is reused for every frame. */
class Barrier
{
public:
    explicit Barrier(int participants) : count(participants) {}

    void wait()
    {
        std::unique_lock<std::mutex> lock(mutex);
        const int generation = this->generation;

        if (++arrived == count)
        {
            arrived = 0;
            ++this->generation;
            cv.notify_all();
        }
        else
        {
            cv.wait(lock, [this, generation] { return this->generation != generation; });
        }
    }

private:
    std::mutex mutex;
    std::condition_variable cv;
    const int count;
    int arrived = 0;
    int generation = 0;
};

/* Splits a trailing "@1,2" selector off the end of an argument. Returns the
   part before it and leaves the selector in `out` (empty when absent). */
std::string split_players(const std::string& value, std::string& out)
{
    const size_t at = value.rfind('@');
    if (at == std::string::npos) { out.clear(); return value; }
    out = value.substr(at + 1);
    return value.substr(0, at);
}

/* Parses "1,2" (1-based) into a bitmask. An empty selector is left as 0 and
   resolved after every argument has been read, because the default depends on
   --all-players and the flag may appear anywhere on the command line. Resolving
   it here instead made the meaning of an input depend on argument ORDER: a
   "--touch F:X:Y --all-players" run quietly drove player 1 only. */
bool parse_players(const std::string& text, PlayerMask& out)
{
    if (text.empty())
    {
        out = 0;
        return true;
    }

    out = 0;
    std::stringstream ss(text);
    std::string part;
    while (std::getline(ss, part, ','))
    {
        int n = 0;
        try { n = std::stoi(part); } catch (...) { return false; }
        if (n < 1 || n > MD_MAX_INSTANCES) return false;
        out |= (1u << (n - 1));
    }
    return out != 0;
}

/* True when an event is asserted on this emulated frame. A period makes a
   press repeat for the first 6 frames of every period; without one it is a
   single window, or a hold when the window runs to the end of the session. */
bool asserted(const int frame, const int first, const int last, const int period)
{
    if (frame < first) return false;
    /* One rule for both kinds of input. A repeating input's last_frame is the
       session end, so this changes nothing for it - but it is what --stop-input
       needs to be able to end a repeating tap. (Used to be checked only on the
       non-repeating branch, which made a periodic input unstoppable.) */
    if (frame >= last) return false;
    if (period > 0) return ((frame - first) % period) < 6;
    return true;
}

struct Options
{
    std::string rom;
    int players = 1;
    int frames = 3600;
    std::string shot_dir;
    int shot_every = 0;
    bool every_player = false;
    bool quiet = false;
    bool serial = false;
    bool no_jit = false;
    int pc_every = 0;
    int recv_timeout = 0;
    int reply_timeout = 2;
    int stop_input = -1;   /* <0 = off; see --stop-input */
    std::vector<Tap> taps;
    std::vector<Touch> touches;
};

bool parse_int(const std::string& text, int& out)
{
    try { out = std::stoi(text); return true; }
    catch (...) { return false; }
}

/* Parses "FRAME:X:Y". */
bool parse_coords(const std::string& body, int& frame, int& x, int& y)
{
    const size_t c1 = body.find(':');
    const size_t c2 = (c1 == std::string::npos) ? std::string::npos : body.find(':', c1 + 1);
    if (c1 == std::string::npos || c2 == std::string::npos) return false;

    return parse_int(body.substr(0, c1), frame) &&
           parse_int(body.substr(c1 + 1, c2 - c1 - 1), x) &&
           parse_int(body.substr(c2 + 1), y);
}

void usage()
{
    std::printf("usage: md_game_session <rom> [--players N] [--frames N] "
                "[--shot-dir DIR] [--shot-every K]\n"
                "       [--tap FRAME:NAMES[@P]] [--hold FRAME:NAMES[@P]] "
                "[--autotap NAMES:START:PERIOD[@P]]\n"
                "       [--touch FRAME:X:Y[@P]] [--holdtouch FRAME:X:Y[@P]] "
                "[--touchtap X:Y:START:PERIOD[@P]]\n"
                "       [--all-players] [--quiet]\n");
}

} // namespace

int main(int argc, char** argv)
{
    Options opt;

    if (argc < 2) { usage(); return 1; }
    opt.rom = argv[1];

    for (int i = 2; i < argc; i++)
    {
        const std::string arg = argv[i];
        auto next = [&](std::string& out) -> bool
        {
            if (i + 1 >= argc) { std::printf("%s needs a value\n", arg.c_str()); return false; }
            out = argv[++i];
            return true;
        };

        std::string value;
        std::string players_text;
        bool ok = true;

        if (arg == "--players")        { if (!next(value) || !parse_int(value, opt.players)) return 1; }
        else if (arg == "--frames")    { if (!next(value) || !parse_int(value, opt.frames)) return 1; }
        else if (arg == "--shot-dir")  { if (!next(opt.shot_dir)) return 1; }
        else if (arg == "--shot-every"){ if (!next(value) || !parse_int(value, opt.shot_every)) return 1; }
        else if (arg == "--all-players") opt.every_player = true;
        else if (arg == "--serial")      opt.serial = true;
        else if (arg == "--recv-timeout") { if (!next(value) || !parse_int(value, opt.recv_timeout)) return 1; }
        else if (arg == "--reply-timeout") { if (!next(value) || !parse_int(value, opt.reply_timeout)) return 1; }
        else if (arg == "--quiet")     opt.quiet = true;
        else if (arg == "--tap" || arg == "--hold")
        {
            if (!next(value)) return 1;
            const std::string body = split_players(value, players_text);
            const size_t colon = body.find(':');
            if (colon == std::string::npos) { std::printf("malformed %s\n", arg.c_str()); return 1; }

            Tap tap;
            if (!parse_players(players_text, tap.players)) return 1;
            if (!parse_int(body.substr(0, colon), tap.first_frame)) return 1;
            tap.buttons = parse_buttons(body.substr(colon + 1), ok);
            if (!ok) return 1;
            tap.last_frame = tap.first_frame + 6;
            opt.taps.push_back(tap);
        }
        else if (arg == "--autotap")
        {
            if (!next(value)) return 1;
            const std::string body = split_players(value, players_text);
            /* NAMES:START:PERIOD */
            const size_t c1 = body.find(':');
            const size_t c2 = body.find(':', c1 + 1);
            if (c1 == std::string::npos || c2 == std::string::npos)
            {
                std::printf("malformed --autotap, expected NAMES:START:PERIOD\n");
                return 1;
            }

            Tap tap;
            if (!parse_players(players_text, tap.players)) return 1;
            tap.buttons = parse_buttons(body.substr(0, c1), ok);
            if (!ok) return 1;
            if (!parse_int(body.substr(c1 + 1, c2 - c1 - 1), tap.first_frame)) return 1;
            if (!parse_int(body.substr(c2 + 1), tap.period)) return 1;

            tap.hold = true;
            opt.taps.push_back(tap);
        }
        else if (arg == "--touch" || arg == "--holdtouch")
        {
            if (!next(value)) return 1;
            const std::string body = split_players(value, players_text);
            /* FRAME:X:Y */
            Touch touch;
            if (!parse_coords(body, touch.first_frame, touch.x, touch.y)) return 1;
            if (!parse_players(players_text, touch.players)) return 1;

            if (arg == "--holdtouch") touch.hold = true;
            else                      touch.last_frame = touch.first_frame + 6;
            opt.touches.push_back(touch);
        }
        else if (arg == "--touchtap")
        {
            if (!next(value)) return 1;
            const std::string body = split_players(value, players_text);
            /* X:Y:START:PERIOD */
            const size_t c1 = body.find(':');
            const size_t c2 = (c1 == std::string::npos) ? std::string::npos : body.find(':', c1 + 1);
            const size_t c3 = (c2 == std::string::npos) ? std::string::npos : body.find(':', c2 + 1);
            if (c1 == std::string::npos || c2 == std::string::npos || c3 == std::string::npos)
            {
                std::printf("malformed --touchtap, expected X:Y:START:PERIOD\n");
                return 1;
            }

            Touch touch;
            if (!parse_int(body.substr(0, c1), touch.x) ||
                !parse_int(body.substr(c1 + 1, c2 - c1 - 1), touch.y) ||
                !parse_int(body.substr(c2 + 1, c3 - c2 - 1), touch.first_frame) ||
                !parse_int(body.substr(c3 + 1), touch.period)) return 1;
            if (!parse_players(players_text, touch.players)) return 1;

            touch.last_frame = opt.frames + 1;
            opt.touches.push_back(touch);
        }
        else if (arg == "--stop-input") { if (!next(value) || !parse_int(value, opt.stop_input)) return 1; }
        else if (arg == "--no-jit") opt.no_jit = true;
        else if (arg == "--pc-every") { if (!next(value) || !parse_int(value, opt.pc_every)) return 1; }
        else { std::printf("unknown option %s\n", arg.c_str()); usage(); return 1; }
    }

    if (opt.players < 1 || opt.players > MD_MAX_INSTANCES)
    {
        std::printf("players must be 1..%d\n", MD_MAX_INSTANCES);
        return 1;
    }

    /* Now that every argument has been read, resolve the inputs that named no
       console: player 1, or every console under --all-players whatever order
       the flag appeared in. */
    const PlayerMask default_players = opt.every_player ? 0xFFFFFFFFu : 1u;
    for (Tap& tap : opt.taps)
    {
        if (!tap.players) tap.players = default_players;
        if (tap.hold) tap.last_frame = opt.frames + 1;
    }
    for (Touch& touch : opt.touches)
    {
        if (!touch.players) touch.players = default_players;
        if (touch.hold) touch.last_frame = opt.frames + 1;
    }

    /* --stop-input: an input that was running when the cutoff arrives stops
       there. One that begins after it is not touched, so a run can end its
       menu route and then inject the input it is actually measuring. */
    if (opt.stop_input >= 0)
    {
        for (Tap& tap : opt.taps)
            if ((tap.first_frame <= opt.stop_input) && (tap.last_frame > opt.stop_input))
                tap.last_frame = opt.stop_input;
        for (Touch& touch : opt.touches)
            if ((touch.first_frame <= opt.stop_input) && (touch.last_frame > opt.stop_input))
                touch.last_frame = opt.stop_input;
    }

    const bool threaded = (opt.players > 1) && !opt.serial;

    std::printf("melonDS Multiplayer - real game session\n");
    std::printf("ROM     : %s\n", opt.rom.c_str());
    std::printf("players : %d\n", opt.players);
    std::printf("frames  : %d\n", opt.frames);
    std::printf("threads : %s\n",
                opt.players == 1 ? "1 (single console)"
                                 : (threaded ? "one per console" : "SERIAL (link will crawl)"));
    std::printf("jit     : %s\n",
                opt.no_jit ? "disabled (interpreter)" : "enabled");
    std::printf("link    : %dms receive timeout, %dms host reply timeout\n",
                opt.recv_timeout, opt.reply_timeout);
    if (opt.stop_input >= 0)
        std::printf("input   : scheduled input ends at frame %d\n", opt.stop_input);
    std::printf("\n");

    if (!opt.shot_dir.empty())
    {
        std::error_code ec;
        std::filesystem::create_directories(opt.shot_dir, ec);
    }

    md_link_init();
    md_link_set_recv_timeout(opt.recv_timeout);
    md_link_set_reply_timeout(opt.reply_timeout);

    std::vector<MDInstance*> players(opt.players, nullptr);
    for (int i = 0; i < opt.players; i++)
    {
        const std::string name = "player" + std::to_string(i + 1);
        players[i] = md_create(0, name.c_str());
        if (!players[i])
        {
            std::printf("failed to create %s\n", name.c_str());
            return 1;
        }
        md_link_attach(players[i], i);
        if (opt.no_jit) md_set_jit(players[i], 0);
    }

    /* Every console runs the same game. Whether it needs a BIOS is a question
       this answers rather than assumes: the bridge falls back to the built-in
       FreeBIOS, and a game that refuses to boot will say so here. */
    for (int i = 0; i < opt.players; i++)
    {
        const int rc = md_load_rom(players[i], opt.rom.c_str());
        if (rc != 0)
        {
            std::printf("player %d could not load the ROM: %s\n", i + 1, md_last_error(players[i]));
            return 1;
        }
    }

    char title[32] = { 0 };
    md_rom_title(players[0], title, sizeof(title));
    std::printf("cartridge title: \"%s\"\n\n", title);

    /* A session is "alive" while the picture keeps changing: a game that has
       crashed or hung on a black screen will stop producing new frames. */
    std::vector<std::vector<uint32_t>> last_top(opt.players);
    std::vector<uint32_t> changes(opt.players, 0);
    std::vector<uint32_t> blink(opt.players, 0);
    /* The frame at which each console's picture last changed, so a game parked
       on a static screen (or wedged) is a failure and not "running frames". */
    std::vector<int> last_beat_frame(opt.players, -1);

    std::ofstream manifest;
    if (!opt.shot_dir.empty())
    {
        manifest.open(opt.shot_dir + "/manifest.tsv", std::ios::binary);
        manifest << "index\tfile\tinstance\thost_frame\tnote\n";
    }

    int shot_index = 0;
    const auto started = std::chrono::steady_clock::now();

    /* Apply the input schedule. Each console's whole input state is recomputed
       every frame and written once: an earlier version only wrote on the frames
       a one-shot tap was asserted, so afterwards the buttons stayed held. */
    auto apply_input = [&](int frame, int p)
    {
        const PlayerMask bit = 1u << p;
        uint32_t buttons = 0;
        bool touching = false;
        int touch_x = 0, touch_y = 0;

        for (const Tap& tap : opt.taps)
        {
            if (!(tap.players & bit)) continue;
            if (asserted(frame, tap.first_frame, tap.last_frame, tap.period))
                buttons |= tap.buttons;
        }

        for (const Touch& touch : opt.touches)
        {
            if (!(touch.players & bit)) continue;
            if (asserted(frame, touch.first_frame, touch.last_frame, touch.period))
            {
                touching = true;
                touch_x = touch.x;
                touch_y = touch.y;
            }
        }

        md_set_buttons(players[p], buttons);
        if (touching) md_touch(players[p], touch_x, touch_y);
        else          md_release_touch(players[p]);
    };

    /* Compare against the previous frame's picture, counting changed pixels:
       games often animate one sprite, and "did anything change" is what says the
       console is alive. Only ever called with every console stopped, so the
       framebuffers cannot move underneath it. */
    auto note_progress = [&](int frame, int p)
    {
        const uint32_t* top = nullptr;
        const uint32_t* bottom = nullptr;
        if (md_get_screens(players[p], &top, &bottom) != 0) return;

        if (last_top[p].empty())
        {
            last_top[p].assign(top, top + MD_SCREEN_PIXELS);
            return;
        }

        uint32_t diff = 0;
        for (size_t i = 0; i < MD_SCREEN_PIXELS; i++)
            if (top[i] != last_top[p][i]) diff++;

        if (diff > 0) { changes[p]++; blink[p] += diff; last_beat_frame[p] = frame; }
        std::memcpy(last_top[p].data(), top, MD_SCREEN_PIXELS * 4);
    };

    auto take_shot = [&](int frame, int p)
    {
        if (opt.shot_dir.empty() || opt.shot_every <= 0) return;
        if ((frame % opt.shot_every) != 0) return;

        const uint32_t* top = nullptr;
        const uint32_t* bottom = nullptr;
        if (md_get_screens(players[p], &top, &bottom) != 0) return;

        char name[128];
        std::snprintf(name, sizeof(name), "%04d_p%d_f%06d.bgra",
                      shot_index++, p + 1, frame);

        std::ofstream out(opt.shot_dir + "/" + name, std::ios::binary);
        out.write((const char*)top, (std::streamsize)MD_SCREEN_PIXELS * 4);
        out.write((const char*)bottom, (std::streamsize)MD_SCREEN_PIXELS * 4);

        if (opt.players == 1)
            manifest << (shot_index - 1) << '\t' << name << "\tplayer1\t"
                     << frame << "\temulated frame " << frame << '\n';
        else
            manifest << (shot_index - 1) << '\t' << name << "\tplayer"
                     << (p + 1) << '\t' << frame << "\temulated frame "
                     << frame << " of player " << (p + 1) << '\n';
    };

    Barrier start_line(opt.players);
    Barrier finish_line(opt.players);

    /* Players 1..N-1 run on their own threads; the console we created first runs
       on this one. Every console meets at both barriers each frame, so the
       emulated consoles advance in lockstep while actually running at the same
       time - which is what lets the link's blocking receives be answered. */
    std::vector<std::thread> workers;
    if (threaded)
    {
        for (int p = 1; p < opt.players; p++)
        {
            workers.emplace_back([&, p]
            {
                md_tag_current_thread((std::string("p") + std::to_string(p + 1)).c_str());
                for (int frame = 0; frame < opt.frames; frame++)
                {
                    start_line.wait();
                    apply_input(frame, p);
                    md_link_set_reply_budget(players[p], reply_budget_ns());
                    md_run_frame(players[p]);
                    finish_line.wait();
                }
            });
        }
    }

    md_tag_current_thread("p1");
    for (int frame = 0; frame < opt.frames; frame++)
    {
        if (threaded)
        {
            start_line.wait();
            apply_input(frame, 0);
            md_link_set_reply_budget(players[0], reply_budget_ns());
            md_run_frame(players[0]);
            finish_line.wait();
        }
        else
        {
            for (int p = 0; p < opt.players; p++)
            {
                apply_input(frame, p);
                md_link_set_reply_budget(players[p], reply_budget_ns());
                md_run_frame(players[p]);
            }
        }

        /* Every console is stopped here, so reading their framebuffers is safe. */
        for (int p = 0; p < opt.players; p++)
        {
            note_progress(frame, p);
            take_shot(frame, p);
        }

        /* A core that has stopped responding shows up as a PC that does not
           move. Printing both cores is what distinguishes "the game is waiting
           for input" from "the game is waiting for the ARM7". */
        if (opt.pc_every > 0 && (frame % opt.pc_every) == 0)
        {
            std::printf("pc    frame=%6d", frame);
            for (int p = 0; p < opt.players; p++)
                std::printf("  p%d arm9=%#010x arm7=%#010x", p + 1,
                            md_get_pc(players[p], 9), md_get_pc(players[p], 7));
            std::printf("\n");
        }
    }

    for (std::thread& worker : workers) worker.join();

    const auto elapsed = std::chrono::steady_clock::now() - started;
    const double seconds = std::chrono::duration<double>(elapsed).count();
    const double emulated_seconds = (double)opt.frames / 59.8261;

    if (manifest.is_open()) manifest.close();

    /* ---- findings --------------------------------------------------------- */
    std::printf("=== stability ===\n");
    int problems = 0;

    for (int p = 0; p < opt.players; p++)
    {
        const int stop = md_stop_reason(players[p]);
        const uint32_t beats = changes[p];
        /* A console that produced frames but whose picture has not moved for the
           last quarter of the run is frozen: the frame counter cannot tell that
           apart from a healthy game, which is how the early-reply regression
           (NSMB's screens byte-identical from frame 3200 to 12000 while every
           link counter read green) survived thirteen reviews. */
        const int since_beat = (opt.frames - 1) - last_beat_frame[p];
        const bool frozen = (opt.frames >= 600) && (last_beat_frame[p] >= 0) &&
                            (since_beat > opt.frames / 4);
        const bool alive = beats > 0 && stop == MD_STOP_NONE && !frozen;
        if (!alive) problems++;

        std::printf("  player %d: %s  frames drawn=%u  stop=%d  asleep=%d\n",
                    p + 1, alive ? "running" : (frozen ? "FROZEN PICTURE" : "PROBLEM"),
                    beats, stop, md_console_asleep(players[p]));
        std::printf("            picture: %u beats, %u pixels moved, last change %d frames ago%s\n",
                    beats, blink[p], since_beat,
                    frozen ? " - this game is not progressing; link counters above it are not health" : "");
        std::printf("            host frames=%u  arm9 pc=%#010x  arm7 pc=%#010x\n",
                    md_frame_counter(players[p]),
                    md_get_pc(players[p], 9), md_get_pc(players[p], 7));
    }

    std::printf("\n=== link ===\n");
    for (int p = 0; p < opt.players; p++)
    {
        MDLinkStats stats {};
        md_link_stats(players[p], &stats);

        /* A host's replies arrive through LocalMP::RecvReplies, which counts
           the packet and cannot size the payload, so its byte total is a lower
           bound (see MDLinkStats): print the note with the number rather than
           leave "received=4454 (136 bytes)" reading as a session that lost
           4452 frames. A host is the console that sends commands. */
        const char* bnote = stats.tx_cmd ? " + replies, counted as packets only" : "";
        std::printf("  player %d: wireless on/off=%u/%u  sent=%llu (%llu bytes)"
                    "  received=%llu (%llu bytes%s)\n",
                    p + 1, stats.begin_count, stats.end_count,
                    (unsigned long long)stats.packets_sent, (unsigned long long)stats.bytes_sent,
                    (unsigned long long)stats.packets_received,
                    (unsigned long long)stats.bytes_received, bnote);
    }
    std::printf("  slots on the bus: %#05x\n", md_link_connected_mask());

    /* ---- multiplayer digest ------------------------------------------------
       The same counters the app prints every five seconds and at every state
       change (md_link_mp_digest), dumped once at the end. On the NSMB script
       this is the known-good reference every live MKDS run gets compared
       against, so a counter that disagrees with the wire here is a counter that
       would have misled the next read-back. */
    std::printf("\n=== multiplayer digest ===\n");
    for (int p = 0; p < opt.players; p++)
    {
        MDMPDigest d {};
        if (md_link_mp_digest(players[p], &d) != 0)
        {
            std::printf("  player %d: digest unavailable\n", p + 1);
            continue;
        }

        const auto hexbody = [](const uint8_t* body, uint16_t len, char* out, size_t cap) {
            const unsigned n = (len > 16) ? 16u : (unsigned)len;
            if (n == 0) { std::snprintf(out, cap, "-"); return; }
            size_t off = 0;
            for (unsigned i = 0; i < n; i++)
            {
                const int wrote = std::snprintf(&out[off], cap - off, (i == 0) ? "%02X" : " %02X", body[i]);
                if (wrote <= 0) break;
                off += (size_t)wrote;
            }
            if (len > n && off + 4 < cap) std::snprintf(&out[off], cap - off, " ..");
        };

        /* When the last command and answer of the session went out, on the
           shared emulated clock. The app prints this every five seconds as the
           "at +NNN.NNs" of `mp pN last:`; the same number here is what makes a
           long run comparable across arms - the offset the link stopped at is
           the reading, not the frame it was noticed on. */
        const auto at_us = [](uint64_t us) -> std::string {
            if (!us) return std::string("-");
            char buf[32];
            std::snprintf(buf, sizeof(buf), "+%.2fs", (double)us / 1000000.0);
            return std::string(buf);
        };

        char cmdbody[64], ansbody[64];
        hexbody(d.last_cmd_body, d.last_cmd_len, cmdbody, sizeof(cmdbody));
        hexbody(d.last_answer_body, d.last_answer_len, ansbody, sizeof(ansbody));

        std::printf("  player %d [%s]: cmds rx=%llu (dup %llu)  answers=%llu (blank %llu, canned %llu)\n"
                    "            arms=%llu  aids=%llu  window reads=%llu  deauth out/in=%llu/%llu\n"
                    "            host side: cmds tx=%llu (dup %llu)  acks=%llu (clientfail %llu)\n"
                    "            replies sent=%llu  collected=%llu  empty=%llu  blank=%llu  stale=%llu"
                    "  reanchor-drop=%llu  notlooking=%llu\n"
                    "            last cmd: seq=%04X len=%u body[%s] at %s  last answer: len=%u body[%s] at %s\n"
                    "            last deauth reason=%04X  last ack clientfail=%04X\n",
                    p + 1,
                    (d.role == 1) ? "host" : ((d.role == 2) ? "guest" : "not in MP"),
                    (unsigned long long)d.cmds_rx, (unsigned long long)d.cmds_rx_dup,
                    (unsigned long long)d.answers_sent,
                    (unsigned long long)d.answers_blank, (unsigned long long)d.answers_canned,
                    (unsigned long long)d.arms, (unsigned long long)d.aids_set,
                    (unsigned long long)d.window_reads,
                    (unsigned long long)d.deauth_sent, (unsigned long long)d.deauth_recv,
                    (unsigned long long)d.cmds_tx, (unsigned long long)d.cmds_tx_dup,
                    (unsigned long long)d.acks_sent, (unsigned long long)d.acks_client_fail,
                    (unsigned long long)d.replies_written, (unsigned long long)d.replies_collected,
                    (unsigned long long)d.windows_empty, (unsigned long long)d.replies_blank,
                    (unsigned long long)d.stale_dropped, (unsigned long long)d.reanchor_dropped,
                    (unsigned long long)d.not_looking,
                    d.last_cmd_seq, (unsigned)d.last_cmd_len, cmdbody, at_us(d.last_cmd_us).c_str(),
                    (unsigned)d.last_answer_len, ansbody, at_us(d.last_answer_us).c_str(),
                    d.last_deauth_reason, d.last_ack_clientfail);
    }

    std::printf("\n=== performance ===\n");
    std::printf("  %d frames x %d console(s) in %.1fs  ->  %.1f emulated seconds, %.2fx realtime\n",
                opt.frames, opt.players, seconds, emulated_seconds,
                seconds > 0 ? emulated_seconds / seconds : 0.0);
    std::printf("  %.1f frames/s per console, %.1f frames/s across all consoles\n",
                seconds > 0 ? (double)opt.frames / seconds : 0.0,
                seconds > 0 ? (double)opt.frames * opt.players / seconds : 0.0);

    for (MDInstance* p : players) md_destroy(p);
    md_link_shutdown();

    std::printf("\n%s\n", problems == 0 ? "all consoles kept running"
                                        : "one or more consoles did not stay alive");
    return problems == 0 ? 0 : 1;
}

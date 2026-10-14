/*
    melonDS Multiplayer - headless smoke test.

    Boots tools/mkdiagrom/diag.nds through the same bridge the Tauri host uses
    and checks the parts of the splitscreen pipeline that can be verified
    without a window:

      * the ROM parses and boots (no crash, no immediate stop)
      * both screens actually get drawn
      * the picture advances with the frame counter
      * two instances fed identical input stay byte-identical
      * the host-stamped identity shows up on screen
      * input reaches exactly one instance
      * savestates round-trip
      * the IPC FIFO counters: a push and a pop are counted, a dropped write and
        an empty poll are not
      * the wifi io report: its PC-tagged register record is present and the
        whole report still fits the control channel's fixed buffer
      * the local link: slot routing, isolation, and the wireless power-on path
      * the link's reply path: which console an answer belongs to and where in
        the host's buffer it lands (none of this was checked before)

    Run it with: build-tauri/tools/coretest/md_smoke_test <path to diag.nds>
*/

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "melonds_bridge.h"
#include "LocalMP.h" // the link bus itself, for the reply-path checks below

namespace
{

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const std::string& what)
{
    g_checks++;
    if (condition)
    {
        std::printf("  ok   %s\n", what.c_str());
        return;
    }
    g_failures++;
    std::printf("  FAIL %s\n", what.c_str());
}

struct Screens
{
    std::vector<uint32_t> top;
    std::vector<uint32_t> bottom;
};

bool grab(MDInstance* inst, Screens& out)
{
    const uint32_t* top = nullptr;
    const uint32_t* bottom = nullptr;
    if (md_get_screens(inst, &top, &bottom) != 0)
        return false;

    out.top.assign(top, top + MD_SCREEN_PIXELS);
    out.bottom.assign(bottom, bottom + MD_SCREEN_PIXELS);
    return true;
}

size_t distinct_colours(const std::vector<uint32_t>& pixels)
{
    std::vector<uint32_t> seen;
    seen.reserve(16);
    for (uint32_t pixel : pixels)
    {
        bool known = false;
        for (uint32_t other : seen)
        {
            if (other == pixel)
            {
                known = true;
                break;
            }
        }
        if (!known)
        {
            seen.push_back(pixel);
            if (seen.size() > 8) break;
        }
    }
    return seen.size();
}

void run_frames(MDInstance* inst, int count)
{
    for (int i = 0; i < count; i++)
        md_run_frame(inst);
}

/* Stamps the diagnostic identity block the ROM reads at startup. */
bool stamp_identity(MDInstance* inst, uint32_t player, uint32_t colour_bgr555)
{
    uint32_t block[4] = { 0x4D44474D /* 'MDGM' */, player, colour_bgr555, 0 };
    return md_write_ram(inst, 0x02FFF000, block, sizeof(block)) == 0;
}

const uint32_t COLOUR_RED = 0x001F;   /* BGR555 */
const uint32_t COLOUR_BLUE = 0x7C00;

/* The diagnostic ROM's identity + progress block (see tools/mkdiagrom). */
struct Diag
{
    uint32_t magic = 0;
    uint32_t index = 0;
    uint32_t colour = 0;
    uint32_t flags = 0;
    uint32_t heartbeat = 0;   /* ROM main-loop iterations completed */
    uint32_t stage = 0;       /* how far through the current iteration */
    uint32_t boot = 0;        /* written by the ROM's first instruction */
    uint32_t keys = 0;        /* buttons the emulated console really sees */
    uint32_t resolved = 0;    /* colour the ROM resolved for itself, if published */
};

Diag sample_diag(MDInstance* inst)
{
    Diag d;
    uint32_t words[9] = { 0 };
    if (md_read_ram(inst, 0x02FFF000, words, sizeof(words)) != 0)
        return d;

    d.magic = words[0];
    d.index = words[1];
    d.colour = words[2];
    d.flags = words[3];
    d.heartbeat = words[4];
    d.stage = words[5];
    d.boot = words[6];
    d.keys = words[7];
    d.resolved = words[8];
    return d;
}

/* The ROM rewrites `stage` several times per main-loop iteration and only the
   host can observe it, at frame boundaries, so a single sample can land anywhere
   in the loop. `heartbeat`, by contrast, is bumped once at the very end of the
   loop, after both screens have been drawn - so the number of heartbeat ticks is
   the number of *complete* iterations. Sampling it a few times and keeping the
   highest value gives a stable count regardless of where the loop happens to be.

   `keys` is sampled the same way, but as a bitwise OR: the mask the ARM9 read is
   latched in RAM, and reading it after any frame in which a button was held will
   show that button. */
Diag watch_diag(MDInstance* inst, int frames)
{
    Diag best = sample_diag(inst);
    for (int i = 0; i < frames; i++)
    {
        md_run_frame(inst);
        const Diag d = sample_diag(inst);
        if (d.stage > best.stage) best.stage = d.stage;
        if (d.heartbeat > best.heartbeat) best.heartbeat = d.heartbeat;
        if (d.boot != 0) best.boot = d.boot;
        best.keys |= d.keys;
    }
    return best;
}

} // namespace

int main(int argc, char** argv)
{
    const std::string rom = argc > 1 ? argv[1] : "diag.nds";

    std::printf("melonDS Multiplayer smoke test\n");
    std::printf("ROM: %s\n\n", rom.c_str());

    md_link_init();

    MDInstance* a = md_create(0, "player1");
    MDInstance* b = md_create(0, "player2");
    MDInstance* c = md_create(0, "player1-again");

    check(a && b && c, "created three instances");
    if (!a || !b || !c) return 1;

    md_link_attach(a, 0);
    md_link_attach(b, 1);
    md_link_attach(c, 2);
    check(md_link_slot(a) == 0 && md_link_slot(b) == 1 && md_link_slot(c) == 2,
          "instances took link slots 0-2");

    /* ---- boot ---------------------------------------------------------- */
    const int loaded = md_load_rom(a, rom.c_str());
    check(loaded == 0, "ROM loads and parses");
    if (loaded != 0)
    {
        std::printf("       %s\n", md_last_error(a));
        return 1;
    }

    char title[32] = { 0 };
    md_rom_title(a, title, sizeof(title));
    check(std::strcmp(title, "MD DIAG") == 0, "ROM title reads back as 'MD DIAG'");

    /* The game code is the identity a per-game profile resolves from, so it has
       to survive the same load path the title does. The diag ROM's code is not
       in the registry on purpose - it exercises the no-entry default path. */
    char game_code[8] = { 0 };
    const int code_rc = md_rom_game_code(a, game_code, sizeof(game_code));
    check(code_rc == 0 && std::strcmp(game_code, "MDSP") == 0,
          "ROM game code reads back as 'MDSP'");

    check(md_load_rom(b, rom.c_str()) == 0, "second instance loads the ROM");
    check(md_load_rom(c, rom.c_str()) == 0, "third instance loads the ROM");

    /* The synthesized Download Play joiner addresses its management frames as
       its own console (their receiver/transmitter addresses decide whether the
       host's reply is accepted back), so the per-slot identity has to be
       readable through the bridge and different on each console. A console
       that has not booted a ROM has no identity yet, which is a refusal. */
    unsigned char mac_a[6] = { 0 }, mac_b[6] = { 0 }, mac_c[6] = { 0 };
    check(md_console_mac(a, mac_a) == 0 && md_console_mac(b, mac_b) == 0 &&
          md_console_mac(c, mac_c) == 0,
          "console MACs read back through the bridge");
    check(std::memcmp(mac_a, mac_b, 6) != 0 && std::memcmp(mac_b, mac_c, 6) != 0 &&
          std::memcmp(mac_a, mac_c, 6) != 0,
          "console MACs differ per slot");
    {
        MDInstance* fresh = md_create(0, "never-booted");
        unsigned char none[6] = { 0 };
        check(fresh && md_console_mac(fresh, none) != 0,
              "a console with no ROM has no MAC to hand out");
        md_destroy(fresh);
    }

    /* The synthesized Download Play joiner attaches at the bus level and must
       be able to notice that a game's own wireless power cycle detached it
       again, so the attachment state is read back rather than assumed. */
    check(md_link_attached(a) == 0,
          "a console whose radio is off is not on the bus");
    check(md_link_test_attach(a, 1) == 0 && md_link_attached(a) == 1,
          "attaching at the bus level puts a console on the bus");
    check(md_link_test_attach(a, 0) == 0 && md_link_attached(a) == 0,
          "detaching takes it off the bus again");

    check(stamp_identity(a, 0, COLOUR_RED), "stamped identity into instance A");
    check(stamp_identity(b, 1, COLOUR_BLUE), "stamped identity into instance B");
    check(stamp_identity(c, 0, COLOUR_RED), "stamped identity into instance C");

    /* ---- does it actually render? -------------------------------------- */
    run_frames(a, 30);
    run_frames(b, 30);
    run_frames(c, 30);

    if (std::getenv("MD_TRACE"))
    {
        for (int i = 0; i < 24; i++)
        {
            md_run_frame(a);
            const Diag d = sample_diag(a);
            Screens s;
            grab(a, s);
            std::printf("       trace %2d stage=%u hb=%4u keys=%03x boot=%#010x "
                        "bottomdistinct=%zu topdistinct=%zu\n",
                        i, d.stage, d.heartbeat, d.keys, d.boot,
                        distinct_colours(s.bottom), distinct_colours(s.top));
        }
    }

    check(md_stop_reason(a) == MD_STOP_NONE, "instance A never stopped (no crash)");
    check(md_stop_reason(b) == MD_STOP_NONE, "instance B never stopped (no crash)");

    /* The wifi io report is read over the control channel into a fixed 64 KiB
       buffer, and the C side reports truncation as a failure rather than as a
       short answer - so a section that outgrows the buffer takes the whole
       command with it. Two sections were added to it (the PC-tagged register
       record, which is what names a driver's bring-up gate), so both the shape
       and the fit are worth pinning here: the sections must be present and the
       whole report, IPC trace included, must still fit the cap the control
       channel hands it. */
    {
        const size_t cap = 64 * 1024;   // ctrl_wifiio's CAP (src-tauri/src/lib.rs)
        std::vector<char> rep(cap + 1, 0);
        const int n = md_wifi_io_report(a, rep.data(), (int)cap);
        check(n > 0, "the wifi io report fits the control channel's buffer");
        check(n > 0 && std::strstr(rep.data(), "register accesses in order") != nullptr,
              "the wifi io report carries the PC-tagged register record");
        check(n > 0 && std::strstr(rep.data(),
                                  "registers the game wrote, last writer per offset") != nullptr,
              "the wifi io report carries the register write table");
        std::printf("       wifi io report: %d bytes of %zu\n", n, cap);
    }

    /* The ROM keeps progress markers in RAM so a failure points at a specific
       part of the program instead of just "nothing happens". */
    const Diag watch_start = sample_diag(a);
    /* Enough frames for several complete main-loop iterations. One iteration
       takes a handful of emulated frames, so this is comfortably more than one
       round trip through the ROM. */
    const int BOOT_FRAMES = 64;
    const Diag diag = watch_diag(a, BOOT_FRAMES);

    /* Keep the other two in step, otherwise the determinism checks below would
       be comparing pictures taken at different points in the animation. */
    run_frames(b, BOOT_FRAMES);
    run_frames(c, BOOT_FRAMES);

    std::printf("       identity magic=%#010x index=%u colour=%#06x flags=%u\n",
                diag.magic, diag.index, diag.colour, diag.flags);
    std::printf("       rom heartbeat=%u (was %u) stage=%u boot=%#010x\n",
                diag.heartbeat, watch_start.heartbeat, diag.stage, diag.boot);
    std::printf("       arm9 pc=%#010x | arm7 pc=%#010x | asleep=%d\n",
                md_get_pc(a, 9), md_get_pc(a, 7), md_console_asleep(a));

    check(diag.magic == 0x4D44474D, "host-stamped identity survived into the ROM's RAM");
    check(diag.boot == 0xB0070000, "the ROM's first instruction executed");

    /* How the console resolved its own identity, as opposed to whether the
       stamp merely survived in RAM. These are different claims: the stamp can
       sit in memory untouched while the ROM quietly uses its fallback colour,
       which is exactly what a broken compare did for a while. */
    for (int i = 0; i < 3; i++)
    {
        MDInstance* inst = (i == 0) ? a : (i == 1) ? b : c;
        const uint32_t want = (i == 1 ? 0x7C00u : COLOUR_RED) | 0x8000u;
        const Diag d = sample_diag(inst);
        std::printf("       instance %c resolved colour %#06x (stamped %#06x)\n",
                    'A' + i, d.resolved, want);
        check(d.resolved == want, std::string("instance ") + char('A' + i) +
                                  " resolves the colour the host stamped");
    }
    /* heartbeat only ticks after the bottom screen has been drawn, so at least
       one tick proves the ROM got all the way around its loop. */
    check(diag.heartbeat > watch_start.heartbeat,
          "the ROM's frame loop is running (heartbeat advanced)");
    check(diag.heartbeat >= watch_start.heartbeat + 3,
          "the ROM completes repeated full main-loop iterations");
    check(diag.stage >= 2, "the ROM reaches its main loop");

    Screens fa, fb, fc;
    check(grab(a, fa) && grab(b, fb) && grab(c, fc), "framebuffers are readable");

    {
        size_t first = fa.top.size();
        size_t painted = 0;
        for (size_t i = 0; i < fa.top.size(); i++)
        {
            if (fa.top[i] != 0xFF000000u)
            {
                if (first == fa.top.size()) first = i;
                painted++;
            }
        }
        std::printf("       top distinct=%zu painted=%zu first_nonblack=%zu (y=%zu x=%zu) value=%#010x\n",
                    distinct_colours(fa.top), painted,
                    first, first / 256, first % 256,
                    first < fa.top.size() ? fa.top[first] : 0u);
    }
    std::printf("       vram main=%#010x sub=%#010x | dispcntA=%#010x bg2cntA=%#06x powcnt1=%#06x\n",
                md_read_gpu(a, 0x06000000), md_read_gpu(a, 0x06200000),
                md_read_gpu(a, 0x04000000),
                md_read_gpu(a, 0x0400000C) & 0xFFFF,
                md_read_gpu(a, 0x04000304) & 0xFFFF);
    std::printf("       engineA dispcnt=%#010x bg2cnt=%#06x | engineB dispcnt=%#010x bg2cnt=%#06x\n",
                md_read_gpu(a, 0x04000000), md_read_gpu16(a, 0x0400000C),
                md_read_gpu(a, 0x04001000), md_read_gpu16(a, 0x0400100C));

    check(distinct_colours(fa.top) >= 2, "top screen shows a multi-colour picture");
    check(distinct_colours(fa.bottom) >= 2, "bottom screen shows a multi-colour picture");

    /* ---- animation + determinism ---------------------------------------- */
    /* The ROM's animation steps once per main-loop iteration, and an iteration
       spans several emulated frames, so advance far enough to be sure at least
       one iteration happened. */
    const int ANIM_FRAMES = 32;
    run_frames(a, ANIM_FRAMES);
    run_frames(b, ANIM_FRAMES);
    run_frames(c, ANIM_FRAMES);

    Screens fa2, fb2, fc2;
    check(grab(a, fa2) && grab(b, fb2) && grab(c, fc2), "grabbed a later frame");
    check(fa2.top != fa.top, "top screen advances between frames");
    check(fa2.bottom != fa.bottom, "bottom screen advances between frames");

    /* Two instances with the same identity, driven for the same number of
       frames, must end up byte-identical. This is the property the splitscreen
       host leans on: every player sees the same world. */
    check(fa2.top == fc2.top && fa2.bottom == fc2.bottom,
          "two instances with identical identity are pixel-identical");

    /* ---- identity reaches the screen ------------------------------------ */
    check(fa2.top != fb2.top || fa2.bottom != fb2.bottom,
          "a different player identity renders differently");

    /* ---- input routing -------------------------------------------------- */
    /* Read the mask back out of the emulated console rather than inferring it
       from pixels: this is the value the ARM9 actually got from the keypad, so
       it proves the whole path - host API, NDS::SetKeyMask polarity, the ARM7's
       mailbox, and the ARM9's read of it. */
    md_set_buttons(a, MD_BTN_A);
    const Diag held_a = watch_diag(a, 24);
    const Diag idle_b = watch_diag(b, 24);
    md_set_buttons(a, 0);
    run_frames(a, 4);
    const Diag released_a = watch_diag(a, 8);

    std::printf("       while A held: A sees keys=%#05x, B sees keys=%#05x; after release A sees %#05x\n",
                held_a.keys, idle_b.keys, released_a.keys);
    /* The two cores do not share a register map: EXKEY carries X/Y and exists on
       the ARM7 only, which is why the ROM has to route X/Y through the ARM7. */
    std::printf("       ARM7 EXKEY=%#06x | ARM9 EXKEY=%#06x (melonDS decodes no EXKEY on the ARM9)\n",
                md_read_io16(a, 7, 0x04000136), md_read_io16(a, 9, 0x04000136));

    check(held_a.keys == MD_BTN_A, "instance A's console sees exactly A held");
    check(idle_b.keys == 0, "instance B's console sees nothing held (input is per-instance)");
    check(released_a.keys == 0, "releasing the button clears it again");

    /* X/Y are the interesting case: the ARM9 has no EXKEY register, so this
       only works if the ARM7's mailbox carries them across. */
    md_set_buttons(a, MD_BTN_X | MD_BTN_Y);
    const Diag held_xy = watch_diag(a, 24);
    md_set_buttons(a, 0);
    run_frames(a, 4);

    std::printf("       while X|Y held: A sees keys=%#05x\n", held_xy.keys);
    check(held_xy.keys == (MD_BTN_X | MD_BTN_Y),
          "X and Y reach the ARM9 through the ARM7's keypad mailbox");

    /* ---- touch screen ---------------------------------------------------- */
    /* The pen is the same kind of problem as X/Y: the guest cannot see "a touch
       at (x,y)". All it ever reads is two ADC conversions and a pen-down key
       bit, so read those back and check they say what we asked for. melonDS
       converts screen pixels to ADC by shifting left 4 (see
       TSC::SetTouchCoords), and forces the firmware calibration to identity on
       reset, so the round trip should be exact. */
    MDTouchProbe pen {};
    const int px = 128, py = 135;
    check(md_touch_probe(a, 1, px, py, &pen) == 0, "touch probe runs");

    std::printf("       pen at (%d,%d): pen_down=%u adc=(%u,%u) expected=(%u,%u)\n",
                px, py, pen.pen_down, pen.adc_x, pen.adc_y,
                (unsigned)(px * 16), (unsigned)(py * 16));

    check(pen.pen_down == 1, "the guest's key register reports the pen down");
    check(pen.adc_x == (uint32_t)(px * 16) && pen.adc_y == (uint32_t)(py * 16),
          "the guest reads back the touched pixel (ADC = pixel x 16)");

    MDTouchProbe lifted {};
    check(md_touch_probe(a, 0, 0, 0, &lifted) == 0, "touch release probe runs");
    check(lifted.pen_down == 0, "releasing the pen clears the pen-down bit");

    /* Touch must be per-instance for the same reason buttons are. */
    MDTouchProbe other {};
    check(md_touch_probe(b, 1, 10, 20, &other) == 0, "second instance touch probe runs");
    check(other.pen_down == 1 && other.adc_x == 10 * 16 && other.adc_y == 20 * 16,
          "a touch lands on the instance it was sent to");
    check(md_touch_probe(a, 1, px, py, &pen) == 0 && pen.adc_x == (uint32_t)(px * 16),
          "the first instance's own pen is unaffected by the second's");
    md_touch_probe(a, 0, 0, 0, &lifted);
    md_touch_probe(b, 0, 0, 0, &other);

    /* ---- savestates ----------------------------------------------------- */
    const size_t state_size = md_max_state_size();
    std::vector<uint8_t> state(state_size, 0);
    const size_t wrote = md_save_state(a, state.data(), state.size());
    check(wrote > 0, "savestate serializes");

    Screens before_save;
    check(grab(a, before_save), "grabbed the frame the state was taken from");

    run_frames(a, 10);
    Screens after;
    check(grab(a, after), "grabbed a later frame");
    check(after.top != before_save.top, "state advanced before reload");

    check(md_load_state(a, state.data(), wrote) == 0, "savestate restores");

    /* Replaying the same number of frames from the restored point has to
       reproduce the same picture. Comparing the live framebuffer straight
       after the load would not: it is only refilled as scanlines are drawn. */
    run_frames(a, 10);
    Screens restored;
    check(grab(a, restored), "grabbed the restored frame");
    check(restored.top == after.top && restored.bottom == after.bottom,
          "replaying frames from a savestate reproduces the same picture");

    /* ---- link bus ------------------------------------------------------- */
    check(md_link_slot(a) == 0 && md_link_slot(b) == 1 && md_link_slot(c) == 2,
          "each console still holds its own link slot");

    /* Being assigned a slot is not the same as being on the bus: a console joins
       the link when its game powers the wireless hardware on. Every instance is
       attached, yet nobody has started wireless, so the bus is empty. That is
       the distinction a lobby UI needs to show, and the distinction that decides
       whether packets are delivered at all. */
    check(md_link_connected_mask() == 0, "no console is on the link bus before wireless starts");
    check(md_link_begin_count(a) == 0, "instance A has not powered wireless on yet");

    const char payload[] = "melon";
    char inbox_b[64] = { 0 };
    char inbox_c[64] = { 0 };

    /* Nothing is listening yet, so a broadcast goes nowhere. */
    md_link_send_packet(a, payload, sizeof(payload) - 1, 1);
    check(md_link_recv_packet(b, inbox_b, sizeof(inbox_b), nullptr) == 0,
          "a broadcast reaches nobody while every console is powered down");

    /* ---- wireless power-on reaches the bus ------------------------------ */
    /* This is the seam between the emulated console and the host. Wireless power
       is an ARM7 register, not an ARM9 one: PowerControl7 (0x04000304) bit 1
       enables the hardware, and W_PowerUS (0x04800036) releases the modem. The
       core then calls back into the bridge, which is what puts the console on
       the bus under its own slot. */
    auto wireless = [](MDInstance* inst, bool on)
    {
        md_write_io16(inst, 7, 0x04000304, on ? 0x0002 : 0x0000);
        md_write_io16(inst, 7, 0x04800036, on ? 0x0000 : 0x0001);
        run_frames(inst, 2);
    };

    wireless(a, true);
    std::printf("       link: A wireless on -> begin=%u end=%u mask=%#05x\n",
                md_link_begin_count(a), md_link_end_count(a), md_link_connected_mask());

    check(md_link_begin_count(a) > 0,
          "powering the console's wireless on reaches the link bus");
    check((md_link_connected_mask() & 0x1u) != 0,
          "instance A appears on the bus in its own slot");
    check((md_link_connected_mask() & 0x6u) == 0, "the other consoles stay off the bus");

    /* ---- delivery over the shared bus ----------------------------------- */
    wireless(b, true);
    wireless(c, true);
    check(md_link_connected_mask() == 0x7u, "all three consoles are on the bus");

    uint64_t ts_b = 0;
    const int queued = md_link_send_packet(a, payload, sizeof(payload) - 1, 1234);
    check(queued == (int)(sizeof(payload) - 1), "a packet queued on the bus");

    const int got_b = md_link_recv_packet(b, inbox_b, sizeof(inbox_b), &ts_b);
    const int got_c = md_link_recv_packet(c, inbox_c, sizeof(inbox_c), nullptr);

    check(got_b == (int)(sizeof(payload) - 1) &&
              std::memcmp(inbox_b, payload, sizeof(payload) - 1) == 0,
          "instance B receives the packet A sent, intact");
    check(got_c == (int)(sizeof(payload) - 1) &&
              std::memcmp(inbox_c, payload, sizeof(payload) - 1) == 0,
          "instance C receives it too (the link is a broadcast bus)");
    check(ts_b == 1234, "the packet's timestamp survives the trip");

    char inbox_a[64] = { 0 };
    check(md_link_recv_packet(a, inbox_a, sizeof(inbox_a), nullptr) == 0,
          "instance A does not receive its own packet back");
    check(md_link_recv_packet(b, inbox_b, sizeof(inbox_b), nullptr) == 0,
          "a drained link returns nothing rather than blocking");

    /* ---- the wire's own rewrite of a frame ------------------------------- */
    /* A frame's trailing four bytes are the hardware's FCS slot, not the
       sender's payload: SendPacketGeneric writes the checksum *into the frame*
       (GBATEK's W_TXBUF_LOCn[0Ah], "the hardware generates the FCS for you"),
       and the receiving console is handed the frame minus those bytes. That
       rule is what the Link test's probe now arms with, and here is why it has
       to: the old test tagged its frames with a nonce in that very slot and
       then waited for the nonce back, so its comparison could never succeed -
       the last word always arrived as the sender's checksum.

       An all-zero four-byte body pins the value independently of this code:
       0x2144DF1C is the CRC-32 of four zero bytes, the same number any other
       implementation of the standard polynomial produces. */
    {
        unsigned char wire_in[20] = { 0 };
        for (int i = 0; i < 16; i++) wire_in[i] = (unsigned char)(0x11 + i);
        wire_in[12] = wire_in[13] = wire_in[14] = wire_in[15] = 0;

        unsigned char wire_out[20] = { 0 };
        const bool wrote = melonDS::LocalMP::ApplyWireFCS(wire_out, wire_in, sizeof(wire_in));
        check(wrote, "the wire writes the hardware's FCS into a frame's trailing four bytes");
        check(std::memcmp(wire_out, wire_in, 16) == 0,
              "the FCS rewrite leaves the frame's own bytes - header and body - alone");

        const unsigned long fcs = (unsigned long)wire_out[16] |
                                  ((unsigned long)wire_out[17] << 8) |
                                  ((unsigned long)wire_out[18] << 16) |
                                  ((unsigned long)wire_out[19] << 24);
        check(fcs == 0x2144DF1Cul,
              "the four bytes it writes are CRC-32 over the frame's header and body");
        check(!melonDS::LocalMP::ApplyWireFCS(wire_out, wire_in, 12),
              "a frame with no room for an FCS slot is passed through, not rewritten");
    }

    /* ---- what the Link test measures: delivered, and read by whom --------- */
    /* The receive half of a Link test cannot be "drain the peer and look for
       the frame": the peer's game polls its receive slot from inside the
       emulation every frame and wins that race whenever it is running (and its
       read proves delivery just as well). So the bus keeps its own ledger of
       frames handed to each console, and a probe armed on the receiver records
       whichever consumer read the armed bytes. What follows is that instrument
       driven through the same C calls the app button uses. */
    {
        const uint32_t probe_magic = 0x4D535430u; // 'MST0'
        const uint32_t probe_nonce = 0x5A5A1234u;
        const uint32_t probe_ts = 4321u;
        uint32_t words[5] = { probe_magic, 0u, 1u, probe_nonce, 0u };
        unsigned char wire[sizeof(words)] = { 0 };
        std::memcpy(wire, words, sizeof(wire));

        MDLinkStats before {};
        md_link_stats(b, &before);

        check(md_probe_arm(b, wire, sizeof(wire), probe_ts) == 0,
              "an expectation can be armed on the receiving console");
        check(md_link_send_packet(a, wire, sizeof(wire), probe_ts) == (int)sizeof(wire),
              "the probe frame queues on the bus");

        unsigned char seen[64] = { 0 };
        uint64_t seen_ts = 0;
        const int got = md_link_recv_packet(b, seen, sizeof(seen), &seen_ts);

        MDProbeResult pr {};
        check(md_probe_result(b, &pr) == 0, "the armed expectation reports its state");
        check(pr.observed == 1 && std::string(pr.seen_by) == "drain",
              "a frame a scripted drain reads is credited to the drain");
        check(pr.payload_ok == 1 && pr.ts_ok == 1,
              "the probe saw the armed bytes and the timestamp the sender queued them with");
        check(got == (int)sizeof(wire) && std::memcmp(seen, wire, 16) == 0 && seen_ts == probe_ts,
              "the drain reads the sender's own bytes, with the wire's FCS in the slot it owns");

        MDLinkStats after {};
        md_link_stats(b, &after);
        check(after.packets_delivered == before.packets_delivered + 1,
              "the bus's ledger counts the frame it handed the receiving console");
        check(after.packets_read_drained == before.packets_read_drained + 1 &&
                  after.packets_read_game == before.packets_read_game,
              "the read is attributed to the drain, and the game's own count is untouched by it");

        /* A frame nobody reads is still a delivery. This is the state the old
           test could not tell from a broken bus: its health lines showed the
           delivery ledger climbing while its drain came up empty, because the
           games had already eaten the frames. */
        md_probe_arm(b, wire, sizeof(wire), probe_ts);
        MDLinkStats unread_before {};
        md_link_stats(b, &unread_before);
        md_link_send_packet(a, wire, sizeof(wire), probe_ts + 1);
        MDLinkStats unread_after {};
        md_link_stats(b, &unread_after);
        MDProbeResult unread {};
        md_probe_result(b, &unread);
        check(unread_after.packets_delivered == unread_before.packets_delivered + 1 &&
                  unread.observed == 0,
              "a frame that reaches a console nothing is polling for is a delivery nobody has read");
        md_link_recv_packet(b, seen, sizeof(seen), &seen_ts); // read it out, back to empty

        /* The instrument must not accept a different frame because it happens
           to arrive while an expectation is armed. */
        uint32_t other[5] = { probe_magic, 0u, 1u, probe_nonce ^ 0xFFFFu, 0u };
        unsigned char other_wire[sizeof(other)] = { 0 };
        std::memcpy(other_wire, other, sizeof(other_wire));
        md_probe_arm(b, wire, sizeof(wire), probe_ts);
        md_link_send_packet(a, other_wire, sizeof(other_wire), probe_ts);
        md_link_recv_packet(b, seen, sizeof(seen), &seen_ts);
        MDProbeResult mismatch {};
        md_probe_result(b, &mismatch);
        check(mismatch.observed == 0,
              "a different frame arriving under an armed expectation is not credited to it");

        /* Disarming is what the test does when it gives up waiting, and it has
           to be complete: an expectation left armed would report the *next*
           frame as this test's. */
        md_probe_disarm(b);
        md_link_send_packet(a, wire, sizeof(wire), probe_ts);
        md_link_recv_packet(b, seen, sizeof(seen), &seen_ts);
        MDProbeResult disarmed {};
        md_probe_result(b, &disarmed);
        check(disarmed.armed == 0 && disarmed.observed == 0,
              "a disarmed expectation observes nothing, whoever reads the frame");
        check(md_probe_arm(b, wire, 4096, probe_ts) == -1,
              "a frame too long for the expectation buffer is refused, never silently truncated");

        /* Nothing of the probe's may be left in a console's queue: the frames
           above were read out one by one, so the next console to look finds the
           bus as empty as it left it. */
        char scratch[64];
        check(md_link_recv_packet(b, scratch, sizeof(scratch), nullptr) == 0,
              "the probe frames are gone once read, not left lying in the peer's inbox");

        /* Every broadcast above also reached the third console, which never
           looked: it holds the same four frames, whole and in order, and
           nothing else. Left there they are what the flush at the end of a
           Link test exists to clear. */
        int c_frames = 0;
        while (c_frames < 16 && md_link_recv_packet(c, scratch, sizeof(scratch), nullptr) > 0)
            c_frames++;
        check(c_frames == 4 && md_link_recv_packet(c, scratch, sizeof(scratch), nullptr) == 0,
              "a console that never looked holds exactly the broadcasts sent to it, and no more");
    }

    /* ---- the ledger is per session, and counts bytes as well ------------- */
    /* A Link test that waits for the ledger to move must not be satisfied by
       the session before it: re-powering a console's wireless starts its half
       of the count again, the same way it starts its flight recorder again. */
    {
        melonDS::LocalMP ledger;
        ledger.SetRecvTimeout(0);
        ledger.SetReplyTimeout(0);
        ledger.Begin(0);
        ledger.Begin(1);

        unsigned char frame[20] = { 0 };
        ledger.SendPacket(0, frame, sizeof(frame), 1);
        melonDS::u64 packets = 0, bytes = 0;
        ledger.GetDelivered(1, &packets, &bytes);
        check(packets == 1 && bytes == sizeof(frame),
              "the ledger credits a console with the frame it was handed, and its bytes");
        ledger.GetDelivered(0, &packets, &bytes);
        check(packets == 0 && bytes == 0, "a console is not credited with a frame it sent itself");
        ledger.GetDelivered(9, &packets, &bytes);
        check(packets == 0 && bytes == 0, "a console outside the bus has no ledger at all");

        /* The ledger's epoch is the bus, not the radio: a wireless power cycle
           does not restart it, because the read counters beside it are not
           restarted either and a line showing more reads than deliveries is a
           contradiction, not a finding. */
        ledger.Begin(1);
        ledger.GetDelivered(1, &packets, &bytes);
        check(packets == 1 && bytes == sizeof(frame),
              "re-powering a wireless does not restart the router's own count, and it cannot fall behind the reads");

        ledger.Reset();
        ledger.GetDelivered(1, &packets, &bytes);
        check(packets == 0 && bytes == 0,
              "ending the bus is what restarts the ledger, together with every other count on it");
    }

    /* ---- leaving the bus stops delivery --------------------------------- */
    /* Powering wireless down must both clear the connected bit and stop the
       traffic, or a console that has left the game keeps accumulating packets. */
    wireless(c, false);
    std::printf("       link: C wireless off -> end=%u mask=%#05x\n",
                md_link_end_count(c), md_link_connected_mask());

    check(md_link_end_count(c) > 0, "powering wireless off reaches the link bus too");
    check((md_link_connected_mask() & 0x4u) == 0, "instance C leaves the bus");

    md_link_send_packet(a, payload, sizeof(payload) - 1, 7);
    check(md_link_recv_packet(c, inbox_c, sizeof(inbox_c), nullptr) == 0,
          "a console that left the bus receives nothing further");
    check(md_link_recv_packet(b, inbox_b, sizeof(inbox_b), nullptr) > 0,
          "the consoles still on the bus keep receiving");

    /* A detached console must stop seeing traffic too, or a dropped player would
       keep receiving game state. */
    md_link_detach(b);
    check(md_link_slot(b) == -1, "detaching clears the link slot");

    md_link_send_packet(a, payload, sizeof(payload) - 1, 99);
    check(md_link_recv_packet(b, inbox_b, sizeof(inbox_b), nullptr) == 0,
          "a detached console no longer receives link traffic");

    /* ---- the reply path: who answers whom -------------------------------- */
    /* The bus's other half, and the half no check covered. A console that reads
       a command becomes a client of the console that sent it, and its answer is
       filed with *that* console - not with whichever console sent a command
       last, which is all a bus with one reply queue and one process-wide host
       record can express. These checks drive the bus class directly (its public
       interface is the one the bridge's link calls use), so any implementation
       of it that keeps the contract has to keep them green; the pass that made
       each console own its inbox, host and answers is what they were written
       for. */
    melonDS::LocalMP bus;
    bus.SetRecvTimeout(0);
    bus.SetReplyTimeout(0);
    bus.Begin(0);
    bus.Begin(1);
    bus.Begin(2);

    unsigned char cmd[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    check(bus.SendCmd(0, cmd, sizeof(cmd), 100) == (int)sizeof(cmd),
          "a command handed to the bus is queued for every other console");

    unsigned char got1[64] = { 0 }, got2[64] = { 0 };
    check(bus.RecvHostPacket(1, got1, nullptr) == (int)sizeof(cmd) &&
              std::memcmp(got1, cmd, sizeof(cmd)) == 0,
          "client 1 reads the host's command off its own inbox");
    check(bus.RecvHostPacket(2, got2, nullptr) == (int)sizeof(cmd) &&
              std::memcmp(got2, cmd, sizeof(cmd)) == 0,
          "client 2 reads the same command off its own inbox");
    check(bus.RecvPacket(0, got1, nullptr) == 0,
          "the host does not receive its own command back");

    /* Two clients answering one round: both answers arrive in the same window,
       each under its own AID, in its own slot of the host's buffer. */
    unsigned char ans1[] = { 0xA1, 0xA2 };
    unsigned char ans2[] = { 0xB1, 0xB2, 0xB3 };
    bus.SendReply(1, ans1, sizeof(ans1), 100, 1);
    bus.SendReply(2, ans2, sizeof(ans2), 100, 2);

    unsigned char collected[15 * 1024] = { 0 };
    check(bus.RecvReplies(0, collected, 100, 0x6) == 0x6,
          "the host collects both clients' answers in one window");
    check(std::memcmp(&collected[0], ans1, sizeof(ans1)) == 0,
          "client 1's answer sits in AID 1's slot");
    check(std::memcmp(&collected[1024], ans2, sizeof(ans2)) == 0,
          "client 2's answer sits in AID 2's slot, not in client 1's");

    melonDS::LocalMP::MPReplyDiag rdiag {};
    bus.GetReplyDiag(0, &rdiag);
    check(rdiag.collected == 2,
          "the host's flight recorder counts both answers collected");
    bus.GetReplyDiag(1, &rdiag);
    check(rdiag.replies_written == 1,
          "client 1's recorder counts the one answer it wrote");

    /* An answer from a console that has never read a command is following
       nobody: it has no owner and must not be filed with a host that happens to
       be collecting. */
    bus.Begin(3);
    bus.SendReply(3, ans1, sizeof(ans1), 100, 1);
    check(bus.RecvReplies(0, collected, 100, 0x8) == 0,
          "an answer from a console following nobody is not filed with a host");

    /* A client whose host leaves the bus is told, rather than left waiting for
       a frame that cannot come. */
    bus.SendCmd(0, cmd, sizeof(cmd), 100);
    check(bus.RecvHostPacket(1, got1, nullptr) == (int)sizeof(cmd),
          "client 1 is still following the host before it leaves");
    bus.End(0);
    check(bus.RecvHostPacket(1, got1, nullptr) == -1,
          "a client whose host has left the bus is told so instead of waiting");

    /* A new command re-anchors the answers it is willing to collect: with the
       default one answer kept per client, a second answer that arrived before
       the command is dropped rather than read a round late, and both are
       counted. */
    {
        melonDS::LocalMP two;
        two.SetRecvTimeout(0);
        two.SetReplyTimeout(0);
        two.Begin(0);
        two.Begin(1);
        two.SendCmd(0, cmd, sizeof(cmd), 100);
        check(two.RecvHostPacket(1, got1, nullptr) > 0, "the client reads the first command");

        unsigned char older[] = { 0x01 }, newest[] = { 0x02 };
        two.SendReply(1, older, sizeof(older), 100, 1);
        two.SendReply(1, newest, sizeof(newest), 100, 1);
        two.SendCmd(0, cmd, sizeof(cmd), 200); // re-anchors: one answer per client

        unsigned char one[15 * 1024] = { 0 };
        check(two.RecvReplies(0, one, 200, 0x2) == 0x2 && one[0] == newest[0],
              "a new command keeps the newest answer from each client, not the oldest");
        melonDS::LocalMP::MPReplyDiag d2 {};
        two.GetReplyDiag(0, &d2);
        check(d2.reanchor_dropped == 1 && d2.collected == 1,
              "the answer the re-anchor dropped is counted, the one it kept is collected");
    }

    /* A re-anchor drops surplus answers, never an answer a console has only
       one of. Here one client answered twice (its US clock is offset, so it
       answers a whole frame's worth of rounds in one clump) and the other
       answered once, queued *in front of* the doubled one. The drop is a
       prefix of the queue, so the older code took the doubled client's older
       answer *and* the single client's only one, and the host's game was told
       that client - which had just answered - failed. */
    {
        melonDS::LocalMP three;
        three.SetRecvTimeout(0);
        three.SetReplyTimeout(0);
        for (int i = 0; i < 3; i++) three.Begin(i);
        three.SendCmd(0, cmd, sizeof(cmd), 100);
        check(three.RecvHostPacket(1, got1, nullptr) > 0 &&
                  three.RecvHostPacket(2, got2, nullptr) > 0,
              "both clients read the command before answering it");

        unsigned char only[] = { 0x30 }, older[] = { 0x01 }, newest[] = { 0x02 };
        three.SendReply(2, only, sizeof(only), 100, 2);   // the single answer, first in the queue
        three.SendReply(1, older, sizeof(older), 100, 1); // then the clump
        three.SendReply(1, newest, sizeof(newest), 100, 1);
        three.SendCmd(0, cmd, sizeof(cmd), 200); // re-anchors: one answer per client

        unsigned char slots[15 * 1024] = { 0 };
        const auto got = three.RecvReplies(0, slots, 200, 0x6);
        check((got & 0x6) == 0x6 && slots[1 * 1024] == only[0] && slots[0] == older[0],
              "a client that answered once is credited even when another client's clump is queued in front of it");
        melonDS::LocalMP::MPReplyDiag d3 {};
        three.GetReplyDiag(0, &d3);
        check(d3.reanchor_dropped == 0 && d3.collected == 2,
              "nothing is dropped: the queue holds both clients' next round");

        /* ... and the answer the clump had left over is the next round's, not
           thrown away: the round it was written for still gets an answer. */
        check(three.RecvReplies(0, slots, 200, 0x2) == 0x2 && slots[0] == newest[0],
              "the clump's leftover answer is handed to the next round, not lost");
        three.GetReplyDiag(0, &d3);
        check(d3.collected == 3 && d3.reanchor_dropped == 0,
              "both of the clump's answers reach the game, one round apart");
    }

    /* Two consoles that each host a client of their own. Each client's answer
       has to reach the console it is following: a single process-wide "the last
       console to send a command" cannot tell these apart and would file both
       answers with whichever host spoke last. This is the case the per-console
       bus exists for. */
    {
        melonDS::LocalMP twohosts;
        twohosts.SetRecvTimeout(0);
        twohosts.SetReplyTimeout(0);
        for (int i = 0; i < 4; i++) twohosts.Begin(i);

        twohosts.SendCmd(0, cmd, sizeof(cmd), 100);
        twohosts.SendCmd(2, cmd, sizeof(cmd), 100);
        /* Console 3 hears both commands (that is what a shared bus is) and reads
           the one its game would act on: the second one, from its own host. */
        check(twohosts.RecvHostPacket(1, got1, nullptr) > 0 &&
                  twohosts.RecvHostPacket(3, got2, nullptr) > 0 &&
                  twohosts.RecvHostPacket(3, got2, nullptr) > 0,
              "two hosts send a command each, and each client reads the one it follows");

        twohosts.SendReply(1, ans1, sizeof(ans1), 100, 1);
        twohosts.SendReply(3, ans2, sizeof(ans2), 100, 1);

        unsigned char c0[15 * 1024] = { 0 };
        unsigned char c2[15 * 1024] = { 0 };
        check(twohosts.RecvReplies(0, c0, 100, 0x2) == 0x2 && c0[0] == ans1[0],
              "each client's answer is collected by the console it is following");
        check(twohosts.RecvReplies(2, c2, 100, 0x2) == 0x2 && c2[0] == ans2[0],
              "... and the other host is not handed a client that is not its own");
    }

    /* ---- a console that stops reading (the queues are bounded) ---------- */
    /* A console stays attached while its game stops draining its inbox: it is
       in a menu, its radio is on, and the other consoles keep sending. The
       router writes into that console's own queue whether or not it reads, so
       *this* is where the bus has to decide what a lagging console loses. What
       it may not lose is a frame that was never sent: a writer that wraps over
       unread bytes hands the reader its next header out of the middle of some
       entry, and a frame stitched from two others is worse than a missing one.
       The bound is the oldest unread frames, oldest first, said once on the
       log - and these checks are its contract. */
    {
        melonDS::LocalMP lag;
        lag.SetRecvTimeout(0);
        lag.SetReplyTimeout(0);
        lag.Begin(0);
        lag.Begin(1);

        const int kFrames = 2000;      // 2000 * (12 + 40) = 104 KB into a 64 KB inbox
        unsigned char tagged[40] = { 0 };
        for (int i = 0; i < kFrames; i++)
        {
            /* The frame carries its own index twice - at its head and, in
               complement, at its tail - so a frame made of two entries, or of
               an entry and a leftover header, fails one of the two. */
            tagged[0] = (unsigned char)(i & 0xFF);
            tagged[1] = (unsigned char)((i >> 8) & 0xFF);
            tagged[34] = (unsigned char)(~tagged[0]);
            tagged[35] = (unsigned char)(~tagged[1]);
            lag.SendPacket(0, tagged, sizeof(tagged), (melonDS::u64)i);
        }

        int first = -1, last = -1, gotn = 0, bad = 0, gaps = 0;
        unsigned char buf[64] = { 0 };
        melonDS::u64 ts = 0;
        for (;;)
        {
            const int got = lag.RecvPacket(1, buf, &ts);
            if (got == 0) break;

            /* Each frame carried its own index in its first two bytes, so a
               frame that does not match the timestamp it arrived with was put
               together by the queue rather than by the sender. */
            if (got != (int)sizeof(tagged) || buf[0] != (unsigned char)(ts & 0xFF) ||
                buf[1] != (unsigned char)((ts >> 8) & 0xFF) ||
                buf[34] != (unsigned char)~buf[0] || buf[35] != (unsigned char)~buf[1])
            {
                bad++;
                continue;
            }

            const int idx = (int)ts;
            if (first < 0) first = idx;
            if (last >= 0 && idx != last + 1) gaps++;
            last = idx;
            gotn++;
        }

        const unsigned long long dropped = lag.GetInboxDrops(1);
        std::printf("       link: slow reader - read %d frames, first index %d, last %d, bad %d, gaps %d, "
                    "dropped %llu\n", gotn, first, last, bad, gaps, dropped);
        check(bad == 0, "a lagging console is never handed a frame assembled out of two others");
        check(first > 0, "what it loses is the oldest unread frames, not the newest");
        check(last == kFrames - 1, "the newest frame sent is still there to read");
        check(gaps == 0 && gotn == kFrames - first,
              "what it keeps is the newest contiguous run, with nothing in it but sent frames");
        check(dropped == (unsigned long long)first,
              "every frame it did not get back was dropped on purpose, not overwritten under it");
    }

    /* The same bound on the host's side: a client that answers faster than the
       host opens windows must not be able to hand the host a reply stitched
       out of two, and the answer that survives has to be the whole one the
       client wrote. */
    {
        melonDS::LocalMP burst;
        burst.SetRecvTimeout(0);
        burst.SetReplyTimeout(0);
        burst.Begin(0);
        burst.Begin(1);
        burst.SendCmd(0, cmd, sizeof(cmd), 1);
        check(burst.RecvHostPacket(1, got1, nullptr) > 0,
              "the client reads a command before it starts answering");

        const int kAnswers = 1200;     // 1200 * (12 + 56) = 81 KB into a 64 KB answer queue
        unsigned char answer[56] = { 0 };
        for (int i = 0; i < kAnswers; i++)
        {
            /* Every byte of answer i is a running count from i, so a reply
               that is not one whole answer fails a byte somewhere. The last
               four bytes are left out of the pattern: that is where the link
               writes the frame's FCS (see LocalMP::SendPacketGeneric). */
            for (int j = 0; j < (int)sizeof(answer); j++)
                answer[j] = (unsigned char)((i + j) & 0xFF);
            burst.SendReply(1, answer, sizeof(answer), (melonDS::u64)i, 1);
        }

        unsigned char big[15 * 1024] = { 0 };
        auto whole = [](const unsigned char* p)
        {
            for (int j = 0; j < 52; j++)
                if (p[j] != (unsigned char)((p[0] + j) & 0xFF)) return false;
            return true;
        };
        const int newest = (kAnswers - 1) & 0xFF;

        const unsigned gotmask = burst.RecvReplies(0, big, (melonDS::u64)kAnswers, 0x2);
        melonDS::LocalMP::MPReplyDiag bdiag {};
        burst.GetReplyDiag(0, &bdiag);
        std::printf("       link: answer burst - window mask %#05x, first answer index %d, whole %d, "
                    "dropped to make room %llu\n", gotmask, big[0], (int)whole(big),
                    (unsigned long long)bdiag.reanchor_dropped);
        check(gotmask == 0x2, "the host still credits the client after a burst larger than its queue");
        check(whole(big), "the answer it reads back is one whole answer, not two entries joined");

        /* And the newest answer is the one a new command's trim keeps. */
        burst.SendCmd(0, cmd, sizeof(cmd), (melonDS::u64)kAnswers + 1);
        std::memset(big, 0, sizeof(big));
        const unsigned after = burst.RecvReplies(0, big, (melonDS::u64)kAnswers + 1, 0x2);
        check(after == 0x2 && whole(big) && big[0] == (unsigned char)newest,
              "the newest answer survives the trim whole, and it is the client's own bytes");
    }

    /* ---- IPC FIFO counters ---------------------------------------------- */
    /* The ARM9<->ARM7 IPC FIFO is where a DS game hands work between its two
       CPUs, and an empty FIFO reads the same whether its last word moved a
       frame ago or it has never carried one - the ambiguity that stalled the
       MKDS investigation. The counters exist to break that tie, so drive a word
       across the real IO ports (0x04000184 control, 0x04000188 send,
       0x04100000 receive) and check each side's count moves only where it
       should. */
    auto ipc = [](MDInstance* inst, MDIPCStats& out)
    {
        std::memset(&out, 0, sizeof(out));
        return md_get_ipc_stats(inst, &out);
    };

    MDIPCStats ipc_a {};
    check(ipc(a, ipc_a) == 0, "IPC counters are readable");
    check(ipc_a.arm9_sends == 0 && ipc_a.arm7_sends == 0 &&
              ipc_a.arm9_recvs == 0 && ipc_a.arm7_recvs == 0,
          "a console that never used the FIFO reports zero traffic");

    /* The FIFOs power up disabled, so this push is dropped by the mode register
       rather than counted: a write nobody receives is not traffic. */
    const uint32_t ipc_word = 0xC0FFEE01u;
    md_write_io32(a, 9, 0x04000188, ipc_word);
    ipc(a, ipc_a);
    check(ipc_a.arm9_sends == 0, "a push with the FIFO disabled is not counted as traffic");

    md_write_io16(a, 9, 0x04000184, 0x8001); /* ARM9: enable, both sides empty */
    md_write_io32(a, 9, 0x04000188, ipc_word);
    ipc(a, ipc_a);
    check(ipc_a.arm9_sends == 1, "a push with the FIFO enabled is counted once");
    check(ipc_a.arm9_recvs == 0 && ipc_a.arm7_sends == 0 && ipc_a.arm7_recvs == 0,
          "pushing counts only the pushing side");

    MDIPCStats ipc_b {};
    check(ipc(b, ipc_b) == 0 && ipc_b.arm9_sends == 0,
          "IPC counters belong to one console, not to the process");

    /* The receive port only pops when *that* CPU's own control register has the
       enable bit set - the two halves are independent enables, and the ARM7's
       is still off here, so this read peeks and must not count. */
    uint32_t ipc_got = 0;
    check(md_read_ram_cpu(a, 7, 0x04100000, &ipc_got, 4) == 0 && ipc_got == ipc_word,
          "a disabled receiver still sees the word its peer pushed");
    ipc(a, ipc_a);
    check(ipc_a.arm7_recvs == 0, "a peek by a disabled receiver is not a receive");

    md_write_io16(a, 7, 0x04000184, 0x8001); /* ARM7: enable too */
    ipc_got = 0;
    check(md_read_ram_cpu(a, 7, 0x04100000, &ipc_got, 4) == 0 && ipc_got == ipc_word,
          "the ARM7 reads the pushed word off its own receive port");
    ipc(a, ipc_a);
    check(ipc_a.arm7_recvs == 1, "the ARM7's receive is counted at its own port");
    check(ipc_a.arm9_sends == 1, "reading the word does not count as a second push");

    /* The FIFO is a queue, not a latch: a second read must come back empty (the
       port answers with its error flag set) and must not inflate the count. A
       game's own poll loop reads this port far more often than words arrive. */
    ipc_got = 0;
    md_read_ram_cpu(a, 7, 0x04100000, &ipc_got, 4);
    ipc(a, ipc_a);
    check(ipc_a.arm7_recvs == 1, "reading an emptied FIFO does not count as a receive");
    check((md_read_io16(a, 7, 0x04000184) & 0x0100u) != 0,
          "the FIFO reads back empty to the ARM7 after the word was taken");

    /* ---- the main-RAM store watch (memwatch) ---------------------------- */

    /* It exists to answer "which instruction stores into this variable", which
       nothing else here can: a writer that reaches its target through a computed
       pointer has no literal in the code to search for. The checks below are its
       contract - a store inside the window is named, a store outside it is not,
       a mirror address lands in the same window, and a disarmed watch says so. */
    {
        char rep[4096];
        const int cap = (int)sizeof(rep);

        check(md_mem_watch(a, 0x02FFF200, 0x02FFF20F) == 0,
              "the store watch arms on a main-RAM window");

        const uint32_t word = 0x5A5A1234;
        check(md_write_ram(a, 0x02FFF204, &word, sizeof(word)) == 0, "wrote the watched word");

        /* The report prints the folded main-RAM address, not the mirror spelling
           the caller may have used: that is the form the watch compares, and a
           report that echoed whichever spelling was armed would make two names
           for one byte look like two bytes. */
        int n = md_mem_watch_report(a, rep, cap);
        check(n > 0 && std::strstr(rep, "023FF204") != nullptr,
              "the store watch names the address that was written");
        check(n > 0 && std::strstr(rep, "5A5A1234") != nullptr,
              "the store watch reports the value that was written");

        /* Arming clears the hits: a report read twice must not accumulate. */
        md_mem_watch(a, 0x02FFF210, 0x02FFF21F);
        n = md_mem_watch_report(a, rep, cap);
        check(n > 0 && std::strstr(rep, "0 overlapping store(s)") != nullptr,
              "re-arming the watch drops the previous hits");

        /* A store just outside the window must not be claimed, and one that
           only *overlaps* its low edge must be: a 4-byte store at 0x02FFF20E
           writes into 0x02FFF210's window only by its last two bytes, and the
           2-byte store at 0x02FFF20E sits entirely below it. */
        uint32_t off = 0;
        md_write_ram(a, 0x02FFF20C, &off, sizeof(off));
        n = md_mem_watch_report(a, rep, cap);
        check(n > 0 && std::strstr(rep, "0 overlapping store(s)") != nullptr,
              "a store that ends below the window is not claimed");

        /* The ARM7 sees main RAM again at 0x02800000, and a window armed with
           that spelling must match the same bytes. Arming there used to match
           nothing and answer "0 stores", which reads like "nothing writes this"
           - the one wrong answer a watch must never give. */
        const uint32_t mirrorword = 0x11223344;
        md_mem_watch(a, 0x027FF240, 0x027FF24F); /* the 0x027FFFFF mirror of 0x023FF240 */
        md_write_ram(a, 0x02FFF240, &mirrorword, sizeof(mirrorword));
        n = md_mem_watch_report(a, rep, cap);
        check(n > 0 && std::strstr(rep, "023FF240") != nullptr,
              "a window armed at a main-RAM mirror matches the folded address");

        check(md_mem_watch_report(a, rep, 64) == -1,
              "a report too small for its header is refused, not truncated");

        /* ARM7 WRAM is watched too: its state lives at 0x03800000, invisible
           to a main-RAM-only watch. md_write_ram_cpu as cpu 7 routes through
           ARM7Write32, the hooked path. */
        const uint32_t wramword = 0xA5A5A5A5;
        check(md_mem_watch(a, 0x02003FF0, 0x02003FFF) == 0,
              "the store watch arms on the canonical ARM7-WRAM window");
        check(md_write_ram_cpu(a, 7, 0x03803FF4, &wramword, sizeof(wramword)) == 0,
              "wrote the watched ARM7-WRAM word");
        n = md_mem_watch_report(a, rep, cap);
        check(n > 0 && std::strstr(rep, "02003FF4") != nullptr,
              "the store watch names an ARM7-WRAM store at its folded address");
        check(n > 0 && std::strstr(rep, "A5A5A5A5") != nullptr,
              "the store watch reports the ARM7-WRAM value that was written");
    }

    /* ---- the per-CPU path trace (pctrace) ------------------------------- */

    /* It exists to answer "which code did this CPU run on the way to a parked
       state", which a snapshot cannot: a decision already left behind is not a
       value anyone can read back. The checks below are its contract - arming
       records the ARM9's own code, the report carries the ordered regions and
       the precise tail, disarming says so, and a tiny buffer is refused. The
       diag ROM's ARM9 runs out of main RAM at 0x02000000, so a window over it
       must catch the running program. */
    {
        const size_t cap = 192 * 1024;   // ctrl_pctrace's CAP (src-tauri/src/lib.rs)
        std::vector<char> rep(cap + 1, 0);

        check(md_pc_trace(a, 0, 0x02000000, 0x02400000) == 0,
              "the path trace arms on an ARM9 code window");
        // Arming and disarming are published by the control thread but only
        // applied by the CPU thread at the top of its next RunFrame, so the
        // smoke test has to let a frame run before the state is observable.
        run_frames(a, 4);

        const int n = md_pc_trace_report(a, rep.data(), (int)cap);
        check(n > 0, "the path trace report fits the instrument's buffer");

        unsigned long long distinct = 0;
        const char* head = std::strstr(rep.data(), "path trace");
        if (head) std::sscanf(head, "path trace %*x..%*x: %llu", &distinct);
        check(distinct > 0, "the path trace records the code the ARM9 ran");
        check(n > 0 && std::strstr(rep.data(), "code regions reached, in order") != nullptr,
              "the path trace reports the code regions in order");
        check(n > 0 && std::strstr(rep.data(), "(pc lr cpsr r0 r1 r2 r3)") != nullptr,
              "the path trace reports the precise tail with its registers");

        md_pc_trace(a, 0, 1, 0);   // lo >= hi disarms
        run_frames(a, 1);          // let the CPU thread apply the disarm
        const int m = md_pc_trace_report(a, rep.data(), (int)cap);
        check(m > 0 && std::strstr(rep.data(), "path trace: off") != nullptr,
              "the path trace disarms to an explicit off");

        check(md_pc_trace_report(a, rep.data(), 64) == -1,
              "a pc trace report too small for its header is refused");
    }

    /* ---- the IPC readouts are per boot, not per instance ---------------- */

    /* These four counters and the trace ring exist to answer what a console's
       *own* two CPUs said to each other, and the one console that needs the
       answer most is a Download Play child, which boots the image it just
       downloaded by crossing NDS::Reset(). If Reset() cleared the FIFOs but
       kept their counters, that child's numbers would still carry the firmware
       Download Play client's traffic it booted from - so the boot would look
       busy before it had executed an instruction, and a frontend could not
       separate the child's own words from its predecessor's. Drive a word in,
       cross the real boot boundary (md_reset, the same Reset() the hand-off
       runs), and require both the counters and the report to read zero. */
    {
        md_write_io16(c, 9, 0x04000184, 0x8001);       /* ARM9: FIFO enabled */
        md_write_io32(c, 9, 0x04000188, 0xC0FFEE02u);  /* ... and one word pushed */
        MDIPCStats ipc_c {};
        check(ipc(c, ipc_c) == 0 && ipc_c.arm9_sends == 1,
              "the word is counted before the boot");

        md_reset(c);

        check(ipc(c, ipc_c) == 0 && ipc_c.arm9_sends == 0 && ipc_c.arm7_sends == 0 &&
                  ipc_c.arm9_recvs == 0 && ipc_c.arm7_recvs == 0,
              "booting clears the IPC traffic counters: they are since boot, not since create");

        const size_t cap = 64 * 1024;
        std::vector<char> rep(cap + 1, 0);
        const int n = md_wifi_io_report(c, rep.data(), (int)cap);
        check(n > 0 && std::strstr(rep.data(), "arm9->arm7 0 word(s)") != nullptr &&
                  std::strstr(rep.data(), "arm7->arm9 0 word(s)") != nullptr,
              "booting clears the traced FIFO words too, both directions");
    }

    /* ---- shutdown ------------------------------------------------------- */

    md_destroy(a);
    md_destroy(b);
    md_destroy(c);
    md_link_shutdown();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

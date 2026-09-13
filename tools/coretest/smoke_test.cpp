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
      * the local link: slot routing, isolation, and the wireless power-on path

    Run it with: build-tauri/tools/coretest/md_smoke_test <path to diag.nds>
*/

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "melonds_bridge.h"

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

    check(md_load_rom(b, rom.c_str()) == 0, "second instance loads the ROM");
    check(md_load_rom(c, rom.c_str()) == 0, "third instance loads the ROM");

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

    /* ---- shutdown ------------------------------------------------------- */

    md_destroy(a);
    md_destroy(b);
    md_destroy(c);
    md_link_shutdown();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

/*
    melonDS Multiplayer - visual capture harness.

    The smoke test proves the pipeline works by asserting on state. This does the
    complementary job: it drives the same bridge and writes out the actual
    pictures, so a human (or a screenshot) can confirm that what the host would
    put on screen is right - the two engines show different content, each player
    has its own colour, holding a button lights the matching block on exactly one
    console, and the picture animates.

    Output is deliberately dumb: one raw BGRA image per capture, 256x384, with
    the top screen in the upper half and the bottom screen in the lower half,
    plus a TSV manifest describing what each capture was supposed to show.
    tools/coretest/frames_to_html.py turns that into a contact sheet.

    Run it with:
        build-tauri/md_capture <path to diag.nds> <output directory>
*/

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "melonds_bridge.h"

namespace
{

/* Player identity colours, in the DS's native BGR555. */
const uint32_t PLAYER_COLOURS[4] = {
    0x001F,   /* red   */
    0x7C00,   /* blue  */
    0x03E0,   /* green */
    0x03FF,   /* yellow */
};

struct Recorder
{
    MDInstance* inst = nullptr;
    std::string label;
    std::string outdir;
    std::ofstream manifest;
    int index = 0;
    int failures = 0;

    std::vector<uint32_t> top;
    std::vector<uint32_t> bottom;
};

/* See tools/mkdiagrom for the identity block the ROM reads at startup. */
bool stamp_identity(MDInstance* inst, uint32_t player)
{
    const uint32_t block[4] = { 0x4D44474D /* 'MDGM' */, player,
                                PLAYER_COLOURS[player % 4], 0 };
    if (md_write_ram(inst, 0x02FFF000, block, sizeof(block)) != 0) return false;

    /* Read it straight back. If stamping silently failed, every console would
       render the ROM's fallback colour and the pictures would all look alike. */
    uint32_t check[4] = { 0 };
    if (md_read_ram(inst, 0x02FFF000, check, sizeof(check)) != 0) return false;
    if (check[0] != block[0] || check[2] != block[2])
    {
        std::printf("  FAIL identity stamp did not stick (wrote %#x/%#x, read %#x/%#x)\n",
                    block[0], block[2], check[0], check[2]);
        return false;
    }
    return true;
}

/* The colour the ROM resolved for itself, which is what ends up on screen. */
uint32_t resolved_colour(MDInstance* inst)
{
    uint32_t word = 0;
    if (md_read_ram(inst, 0x02FFF020, &word, sizeof(word)) != 0) return 0;
    return word;
}

bool grab(MDInstance* inst, std::vector<uint32_t>& top, std::vector<uint32_t>& bottom)
{
    const uint32_t* t = nullptr;
    const uint32_t* b = nullptr;
    if (md_get_screens(inst, &t, &b) != 0) return false;

    top.assign(t, t + MD_SCREEN_PIXELS);
    bottom.assign(b, b + MD_SCREEN_PIXELS);
    return true;
}

std::string slot_name(int slot)
{
    return "player" + std::to_string(slot + 1);
}

/* Writes "<outdir>/NN_label.bgra" and appends a manifest row.

   The note is what a reviewer should look for in the picture, which is the
   whole point of capturing it: an image nobody can check is decoration. */
void capture(Recorder& rec, const std::string& label, const std::string& note)
{
    if (!grab(rec.inst, rec.top, rec.bottom))
    {
        std::printf("  FAIL could not read framebuffers for %s\n", label.c_str());
        rec.failures++;
        return;
    }

    char filename[256];
    std::snprintf(filename, sizeof(filename), "%02d_%s.bgra", rec.index, label.c_str());

    const std::string path = rec.outdir + "/" + filename;
    std::ofstream out(path, std::ios::binary);
    if (!out)
    {
        std::printf("  FAIL could not write %s\n", path.c_str());
        rec.failures++;
        return;
    }

    /* Top screen first, then the bottom, so the image reads like a DS. */
    out.write((const char*)rec.top.data(), (std::streamsize)rec.top.size() * 4);
    out.write((const char*)rec.bottom.data(), (std::streamsize)rec.bottom.size() * 4);
    out.close();

    std::printf("  wrote %s  (%s)\n", filename, note.c_str());

    rec.manifest << rec.index << '\t' << filename << '\t' << rec.label << '\t'
                 << md_frame_counter(rec.inst) << '\t' << note << '\n';
    rec.index++;
}

void run_frames(MDInstance* inst, int count)
{
    for (int i = 0; i < count; i++)
        md_run_frame(inst);
}

} // namespace

int main(int argc, char** argv)
{
    const std::string rom = argc > 1 ? argv[1] : "diag.nds";
    const std::string outdir = argc > 2 ? argv[2] : "captures";

    std::printf("melonDS Multiplayer visual capture\n");
    std::printf("ROM: %s\n", rom.c_str());
    std::printf("Out: %s\n\n", outdir.c_str());

    std::error_code ec;
    std::filesystem::remove_all(outdir, ec);
    std::filesystem::create_directories(outdir, ec);
    if (ec)
    {
        std::printf("could not create %s: %s\n", outdir.c_str(), ec.message().c_str());
        return 1;
    }

    md_link_init();
    md_link_shutdown();

    /* One instance per player, each with its own colour. */
    MDInstance* players[3] = {
        md_create(0, "player1"),
        md_create(0, "player2"),
        md_create(0, "player3"),
    };

    for (MDInstance* p : players)
    {
        if (!p)
        {
            std::printf("failed to create an instance\n");
            return 1;
        }
    }

    for (int i = 0; i < 3; i++)
    {
        if (md_load_rom(players[i], rom.c_str()) != 0)
        {
            std::printf("ROM load failed: %s\n", md_last_error(players[i]));
            return 1;
        }
        if (!stamp_identity(players[i], (uint32_t)i))
        {
            std::printf("failed to stamp identity for player %d\n", i + 1);
            return 1;
        }
    }

    Recorder rec;
    rec.outdir = outdir;
    rec.manifest.open(outdir + "/manifest.tsv", std::ios::binary);
    rec.manifest << "index\tfile\tinstance\thost_frame\tnote\n";

    /* ---- does each player render at all, and differently? --------------- */
    for (int i = 0; i < 3; i++)
    {
        rec.inst = players[i];
        rec.label = slot_name(i);
        run_frames(players[i], 64);

        std::printf("  %s resolved colour %#06x (stamped %#06x)\n",
                    rec.label.c_str(), resolved_colour(players[i]),
                    PLAYER_COLOURS[i] | 0x8000);

        capture(rec, "idle", "player " + std::to_string(i + 1) +
                    "'s own colour stripes on top, twelve unlit button blocks below");

        if (md_stop_reason(players[i]) != MD_STOP_NONE)
        {
            std::printf("  FAIL %s stopped: reason %d\n",
                        rec.label.c_str(), md_stop_reason(players[i]));
            rec.failures++;
        }
    }

    /* ---- does the picture move? ----------------------------------------- */
    rec.inst = players[0];
    rec.label = slot_name(0);

    run_frames(players[0], 48);
    capture(rec, "animated", "same console later: stripes scrolled, colour bar moved, "
                             "progress bar advanced");

    /* ---- does input land on exactly one console? ------------------------ */
    md_set_buttons(players[0], MD_BTN_A | MD_BTN_L);
    run_frames(players[0], 16);
    run_frames(players[1], 16);
    run_frames(players[2], 16);

    rec.inst = players[0];
    rec.label = slot_name(0);
    capture(rec, "hold_a_l", "player 1 holds A and L: only those two blocks light");

    rec.inst = players[1];
    rec.label = slot_name(1);
    capture(rec, "hold_a_l", "player 2 holds nothing: blocks stay dim, colour unchanged");

    rec.inst = players[2];
    rec.label = slot_name(2);
    capture(rec, "hold_a_l", "player 3 holds nothing either: no input leaked");

    /* A different player pressing different buttons, to prove the routing is
       per-instance rather than just per-frame. */
    md_set_buttons(players[0], 0);
    md_set_buttons(players[1], MD_BTN_UP | MD_BTN_R | MD_BTN_X);
    run_frames(players[0], 16);
    run_frames(players[1], 16);

    rec.inst = players[0];
    rec.label = slot_name(0);
    capture(rec, "release", "player 1 released: blocks go dim again");

    rec.inst = players[1];
    rec.label = slot_name(1);
    capture(rec, "hold_up_r_x", "player 2 holds Up, R and X: three different blocks light");

    md_set_buttons(players[1], 0);
    run_frames(players[1], 16);

    /* ---- savestate round trip, shown rather than asserted --------------- */
    const size_t cap = md_max_state_size();
    std::vector<uint8_t> state(cap, 0);
    const size_t wrote = md_save_state(players[0], state.data(), state.size());

    rec.inst = players[0];
    rec.label = slot_name(0);
    if (wrote > 0)
    {
        capture(rec, "state_saved", "framebuffer at the moment the state was taken");

        run_frames(players[0], 40);
        capture(rec, "state_advanced", "the same console 40 frames later, before reloading");

        if (md_load_state(players[0], state.data(), wrote) == 0)
        {
            /* The live framebuffer is only refilled as scanlines are drawn, so
               replaying the same number of frames is the honest comparison. */
            run_frames(players[0], 40);
            capture(rec, "state_replayed", "40 frames replayed after loading the state - "
                                          "should match 'state_advanced' exactly");
        }
        else
        {
            std::printf("  FAIL savestate restore failed\n");
            rec.failures++;
        }
    }
    else
    {
        std::printf("  FAIL savestate serialize failed\n");
        rec.failures++;
    }

    rec.manifest.close();

    for (MDInstance* p : players)
        md_destroy(p);
    md_link_shutdown();

    std::printf("\n%d captures, %d failures\n", rec.index, rec.failures);
    std::printf("Build the contact sheet with:\n");
    std::printf("  python3 tools/coretest/frames_to_html.py %s\n", outdir.c_str());

    return rec.failures == 0 ? 0 : 1;
}

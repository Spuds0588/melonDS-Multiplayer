/*
    melonDS Multiplayer - native bridge implementation.

    One MDInstance owns one emulated DS. The Tauri host creates 2-4 of them,
    runs each on its own thread and links them through the shared local
    multiplayer bus (melonDS's in-tree LocalMP, i.e. the "virtual link cable").
*/

#include "melonds_bridge.h"
#include "BridgeInternal.h"

#include <cstdio>
#include <cstring>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "NDS.h"
#include "NDSCart.h"
#include "Savestate.h"
#include "GPU.h"
#include "SPI.h"
#include "Wifi.h"

#include <optional>

#include "LocalMP.h"

using melonDS::u32;
using melonDS::u64;
using melonDS::u8;

/* ------------------------------------------------------------ instance state */

/* One outstanding Link-test expectation for one console: the exact bytes and
   timestamp a consumer there should read, and what the consumers saw of it.
   Only the first matching read is recorded - by then the frame is gone, and a
   second sighting would be a different frame.

   `expect` is the frame as the *wire* leaves it, not as the sender handed it
   over: the bus writes the hardware's FCS into the frame's own trailing four
   bytes (see LocalMP::ApplyWireFCS), so a probe armed with the sender's bytes
   would wait for a frame that cannot arrive. That mistake made the Link test
   compare its nonce against a checksum and report FAILED on a healthy bus in
   every state (2026-09-30, history.md v0.1.9bo). */
struct MDProbe
{
    bool   armed = false;
    u8     expect[32] {};
    size_t expect_len = 0;
    u64    expect_ts = 0;
    bool   fcs_written = false; // the wire owns this frame's trailing four bytes

    bool   observed = false;   // a consumer read a frame carrying this tag
    bool   payload_ok = false; // ... and the bytes are the ones the sender queued
    bool   ts_ok = false;      // ... and the timestamp is the one it was queued with
    char   seen_by[12] = "";   // "game" or "drain"
};

struct MDInstance
{
    std::string name;
    std::unique_ptr<melonDS::NDS> nds;
    std::string last_error;
    std::string rom_path;
    std::string rom_title;
    /* The 4-byte game code at header offset 0x0C ("AMCE", "A2DE", ...), the
       cartridge's own identity. Read once at load so the host can resolve a
       per-game profile without re-reading (or trusting) the ROM path. */
    std::string rom_game_code;
    std::string sav_path;

    /* Buttons the host currently holds down, in the MD_BTN_* active-high
       layout from melonds_bridge.h. */
    u32 buttons = 0;
    bool touch_active = false;
    int touch_x = 0;
    int touch_y = 0;
    bool lid_closed = false;

    int slot = -1;
    int stop_reason = MD_STOP_NONE;
    u32 frame_counter = 0;
    u32 link_begin_count = 0;
    u32 link_end_count = 0;
    u64 link_packets_sent = 0;
    u64 link_packets_received = 0;
    u64 link_bytes_sent = 0;
    u64 link_bytes_received = 0;

    /* The receive total, split by *who read the frame*. Both halves used to be
       one number, which made "the console accepted 9 frames" ambiguous between
       the console's own game polling them and a scripted drain collecting them
       - and on 2026-09-30 an app Link test read that growth as its own success
       while its drains had collected nothing at all (see the L/L2 fields in
       LinkStats v3). `link_packets_received` stays the sum. */
    u64 link_packets_read_game = 0;
    u64 link_bytes_read_game = 0;
    u64 link_packets_read_drained = 0;
    u64 link_bytes_read_drained = 0;

    /* Per-kind packet counters (LinkStats v2). 'broadcast' = MP frames and
       acks a console pulled off its own receive slot; 'cmd'/'reply' = the
       host's command/reply handshake; 'host_polls' = MP host-frame polls,
       counted separately because Wifi::CheckRX hammers them and the game's
       own per-packet count would drown in polls that returned nothing. */
    u64 link_rx_broadcast = 0;
    u64 link_rx_cmd = 0;
    u64 link_rx_reply = 0;
    u64 link_rx_ack = 0;
    u64 link_rx_polls = 0;
    u64 link_tx_cmd = 0;
    u64 link_tx_reply = 0;
    u64 link_tx_ack = 0;
    u64 link_rx_empty_polls = 0;

    /* Monotonic microsecond timestamp of the last accepted-from-a-peer packet
       (total-packet counter only; a clock of polls is meaningless). Drives
       LINK STALL detection in the host's health log. */
    u64 link_last_rx_us = 0;

    /* The outstanding Link-test expectation for this console, if any. */
    MDProbe probe;

    /* Identity on the virtual air. Every instance boots the same firmware, so
       the per-console offset has to be applied by the host; MELN keeps the
       firmware MAC as loaded so the offset can be re-derived on every boot
       instead of compounding (see apply_console_mac). The user name is the
       other half of that identity and is kept the same way. */
    melonDS::MacAddress console_mac = {};
    melonDS::MacAddress firmware_mac = {};
    bool firmware_mac_valid = false;
    /* The name this console advertises to the other consoles (see
       apply_console_mac): "Player 1".."Player 4", the same string the
       Download Play stand-in reports. Readable through md_console_name. */
    char16_t firmware_nickname[10] = {};
    melonDS::u16 firmware_nickname_len = 0;
    bool firmware_nickname_valid = false;

    /* scratch buffer for md_load_state() so the caller's buffer stays const */
    std::vector<u8> state_scratch;
};

namespace
{

int set_error(MDInstance* inst, const char* msg)
{
    if (inst) inst->last_error = msg ? msg : "unknown error";
    std::fprintf(stderr, "[melonDS bridge] %s\n", msg ? msg : "unknown error");
    return 1;
}

std::string sav_path_for(const std::string& rom_path)
{
    std::string path = rom_path;
    const size_t dot = path.find_last_of('.');
    const size_t slash = path.find_last_of("/\\");
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
        path.erase(dot);
    path += ".sav";
    return path;
}

bool read_whole_file(const std::string& path, std::vector<u8>& out)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;

    std::fseek(f, 0, SEEK_END);
    const long len = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (len <= 0)
    {
        std::fclose(f);
        return false;
    }

    out.resize((size_t)len);
    const size_t got = std::fread(out.data(), 1, (size_t)len, f);
    std::fclose(f);
    return got == (size_t)len;
}

bool write_whole_file(const std::string& path, const void* data, size_t len)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const size_t wrote = std::fwrite(data, 1, len, f);
    std::fclose(f);
    return wrote == len;
}

bool file_exists(const std::string& path)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

/* Shared link bus, created lazily by md_link_init(). */
std::mutex g_link_lock;
std::unique_ptr<melonDS::LocalMP> g_link_bus;

/* Monotonic wall clock for link event timestamps and session-relative log
   prefixes, taken once at md_link_init(). */
std::chrono::steady_clock::time_point g_link_epoch;
u64 now_us()
{
    return (u64)std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
/* Seconds since the bus was created; the log's shared time axis. */
double session_secs()
{
    return std::chrono::duration_cast<std::chrono::duration<double>>(
               std::chrono::steady_clock::now() - g_link_epoch)
        .count();
}

/* Slots whose console currently has its wireless hardware powered on.

   LocalMP tracks this itself but keeps it private, and it is the bridge that
   decides which slot an instance maps to, so the bridge mirrors it here. Every
   transition comes through notify_mp_begin/notify_mp_end, so the two cannot
   drift apart. */
u32 g_link_connected = 0;

} // namespace

/* melonDS takes the keypad as an active-low mask: 0xFFF means "nothing
   pressed", and holding a key clears its bit. (melonDS's own Qt frontend keeps
   inputMask = 0xFFF and does keyInputMask &= ~(1 << i).) The bridge API is
   active-high, because that is what a frontend naturally builds up from
   keyboard/gamepad events, so the conversion lives here.

   Bits 0-9 are A/B/Select/Start/Right/Left/Up/Down/R/L and bits 10-11 are X/Y,
   matching NDS::SetKeyMask's packing (low keys into KeyInput bits 0-9, X/Y into
   bits 16-17). */
static void push_buttons(MDInstance* inst);

/* --------------------------------------------------------------- md:: hooks */

namespace md
{

melonDS::NDS* nds_for(void* userdata)
{
    auto* inst = static_cast<MDInstance*>(userdata);
    return inst ? inst->nds.get() : nullptr;
}

int slot_for(void* userdata)
{
    auto* inst = static_cast<MDInstance*>(userdata);
    return inst ? inst->slot : -1;
}

void notify_stop(void* userdata, int reason)
{
    auto* inst = static_cast<MDInstance*>(userdata);
    if (inst) inst->stop_reason = reason;
}

void notify_save_write(void* userdata, const u8* savedata, u32 savelen, u32 writeoffset, u32 writelen)
{
    (void)writeoffset;
    (void)writelen;
    if (!savedata || savelen == 0) return;

    /* A DS game rewrote its save RAM: persist it next to the ROM. */
    auto* inst = static_cast<MDInstance*>(userdata);
    if (inst && !inst->sav_path.empty())
        write_whole_file(inst->sav_path, savedata, savelen);
}

void notify_gba_save_write(void* userdata, const u8* savedata, u32 savelen, u32 writeoffset, u32 writelen)
{
    (void)userdata;
    (void)savedata;
    (void)savelen;
    (void)writeoffset;
    (void)writelen;
    /* Slot-2 GBA cartridge support is out of scope for splitscreen v1. */
}

void notify_firmware_write(void* userdata, const melonDS::Firmware& firmware,
                           u32 writeoffset, u32 writelen)
{
    (void)userdata;
    (void)firmware;
    (void)writeoffset;
    (void)writelen;
    /* v1 regenerates firmware per instance; persisting edits is a TODO. */
}

melonDS::LocalMP* link_bus()
{
    std::lock_guard<std::mutex> lock(g_link_lock);
    return g_link_bus.get();
}

void notify_mp_begin(void* userdata)
{
    auto* inst = static_cast<MDInstance*>(userdata);
    if (!inst) return;

    std::lock_guard<std::mutex> lock(g_link_lock);
    inst->link_begin_count++;
    if (inst->slot >= 0) g_link_connected |= (1u << inst->slot);
    /* The seconds are *wall* clock since the bus was initialized, which is a
       different unit from the digest's "at +NNN.NNs" emulated offsets - the two
       were read as one clock on 2026-09-29, which put the guest's join 130
       emulated seconds after the frame it was visible on in the captures. The
       console's own frame counter is printed beside it so an event can be
       placed on the timeline a route is written against. */
    std::fprintf(stderr,
        "[link-event] +%.3fs wall frame %u p%d (%s): wireless ON (begin #%u, slots now 0x%x)\n",
        session_secs(), inst->frame_counter, inst->slot, inst->name.c_str(),
        inst->link_begin_count, g_link_connected);
}

void notify_mp_end(void* userdata)
{
    auto* inst = static_cast<MDInstance*>(userdata);
    if (!inst) return;

    std::lock_guard<std::mutex> lock(g_link_lock);
    inst->link_end_count++;
    if (inst->slot >= 0) g_link_connected &= ~(1u << inst->slot);
    std::fprintf(stderr,
        "[link-event] +%.3fs wall frame %u p%d (%s): wireless off (end #%u, slots now 0x%x)\n",
        session_secs(), inst->frame_counter, inst->slot, inst->name.c_str(),
        inst->link_end_count, g_link_connected);
}

void note_packet_sent(void* userdata, int len)
{
    auto* inst = static_cast<MDInstance*>(userdata);
    if (!inst) return;

    std::lock_guard<std::mutex> lock(g_link_lock);
    inst->link_packets_sent++;
    if (len > 0) inst->link_bytes_sent += (u64)len;
}

void note_packet_sent_kind(void* userdata, int kind, int len)
{
    auto* inst = static_cast<MDInstance*>(userdata);
    if (!inst) return;

    std::lock_guard<std::mutex> lock(g_link_lock);
    inst->link_packets_sent++;
    switch (kind)
    {
    case 1: inst->link_tx_cmd++; break;
    case 2: inst->link_tx_reply++; break;
    case 3: inst->link_tx_ack++; break;
    default: break; /* kind 0 = MP frame, no dedicated tx counter */
    }
    if (len > 0) inst->link_bytes_sent += (u64)len;
}

void note_packet_received_kind(void* userdata, int kind, int len)
{
    auto* inst = static_cast<MDInstance*>(userdata);
    if (!inst) return;

    std::lock_guard<std::mutex> lock(g_link_lock);
    inst->link_packets_received++;
    inst->link_packets_read_game++;
    switch (kind)
    {
    case 0: inst->link_rx_broadcast++; break;
    case 1: inst->link_rx_cmd++; break;
    case 2: inst->link_rx_reply++; break;
    case 3: inst->link_rx_ack++; break;
    default: break;
    }
    if (len > 0)
    {
        inst->link_bytes_received += (u64)len;
        inst->link_bytes_read_game += (u64)len;
    }
    inst->link_last_rx_us = now_us();
}

/* ------------------------------------------------------------------ probe

   One outstanding expectation per console, armed by the Link test on the
   receiver and observed by whichever consumer reads the frame first. Its own
   mutex, not g_link_lock: md_link_recv_packet calls it while holding the link
   lock, and the game path calls it without, so sharing one lock would either
   deadlock the drain or leave the game path unguarded. */
std::mutex g_probe_lock;

void probe_observe(void* userdata, const char* who, const u8* frame, size_t len, u64 timestamp)
{
    auto* inst = static_cast<MDInstance*>(userdata);
    if (!inst || !frame) return;

    std::lock_guard<std::mutex> lock(g_probe_lock);
    MDProbe& pr = inst->probe;
    if (!pr.armed || pr.observed) return;
    if (len != pr.expect_len) return;

    /* The tag is every byte of the frame the sender chose - which is the whole
       frame when the wire wrote no FCS into it, and the frame minus its
       trailing four bytes when it did. The slot is left out of the tag because
       the wire computes it: it is a checksum *over* those bytes, so a mangled
       body already fails the comparison, and including it would only re-test
       our own CRC. Everything else has to match byte for byte before this is
       taken as the probe's own frame. */
    const size_t tag = pr.fcs_written ? (len - 4) : len;
    if (tag > pr.expect_len) return;
    if (memcmp(frame, pr.expect, tag) != 0) return;

    pr.observed = true;
    pr.payload_ok = (memcmp(frame, pr.expect, pr.expect_len) == 0);
    pr.ts_ok = (timestamp == pr.expect_ts);
    std::snprintf(pr.seen_by, sizeof(pr.seen_by), "%s", who ? who : "?");
}

void note_poll(void* userdata, bool got_packet)
{
    auto* inst = static_cast<MDInstance*>(userdata);
    if (!inst) return;

    std::lock_guard<std::mutex> lock(g_link_lock);
    inst->link_rx_polls++;
    if (!got_packet) inst->link_rx_empty_polls++;
}

bool link_enabled()
{
    return link_bus() != nullptr;
}

} // namespace md

/* ------------------------------------------------------------------ lifecycle */

/* Gives this console its own MAC address.

   All the instances in a session load the same firmware, so out of the box they
   all present the same MAC - and a DS game identifies the consoles it talks to
   by MAC. MKDS, for one, builds the room list out of the host's MAC, so a
   joiner that shares the host's MAC cannot tell the host's group apart from its
   own console and the group never shows up. melonDS's own frontend offsets the
   MAC of every instance past the first for the same reason
   (EmuInstance::customizeFirmware); slot 0 keeps the firmware MAC and the rest
   get distinct low bytes, which is exactly the scheme used here.

   Called before the console boots: the guest reads its MAC out of the firmware
   over SPI while booting, then programs it into the wireless hardware. */
void apply_console_mac(MDInstance* inst)
{
    if (!inst || !inst->nds) return;

    melonDS::Firmware& firmware = inst->nds->GetFirmware();

    if (!inst->firmware_mac_valid)
    {
        inst->firmware_mac = firmware.GetHeader().MacAddr;
        inst->firmware_mac_valid = true;
    }

    inst->console_mac = inst->firmware_mac;

    const int id = (inst->slot > 0) ? inst->slot : 0;
    if (id > 0)
    {
        inst->console_mac[3] += (u8)id;
        inst->console_mac[4] += (u8)(id * 0x44);
        inst->console_mac[5] += (u8)(id * 0x10);
        inst->console_mac[0] &= 0xFC; /* never present a broadcast MAC */
    }

    firmware.GetHeader().MacAddr = inst->console_mac;
    firmware.UpdateChecksums();

    /* The other half of the same identity: the console's *user name*.

       Every instance boots the same firmware, so all of them carry the same
       name, and a title whose lobby lists the players it can see shows one name
       for every console it is talking to. Measured in this fork's own Tetris DS
       multi-card session: three guests associated with distinct MACs and
       distinct AIDs (1, 2, 3), each polled by the host at full rate (`mask
       000E`) with every reply credited (70,857 hand-overs across the three
       AIDs, logs/m4_tetris3.log.gz) - and each earlier guest's own game wrote
       `deauth sent ... reason 0003` a few seconds after the next one appeared,
       in join order, while the newest survived. The name is the one identity
       the four consoles still shared, so it is the one thing to make distinct
       before any conclusion about the games' own state machines is worth
       drawing. Applied only past the first console, exactly like the MAC: slot
       0 keeps the firmware's own name, so single-console behaviour is
       untouched. */
    /* The name is the *player*, for every slot including the first: the DS
       nickname field holds ten characters, so "Player 1".."Player 4" fit with
       room to spare. A supplied firmware image's own nickname is replaced too
       - four consoles booting one image all carry the same name, and a room
       whose list reads "melonDS" four times is a room nothing in it can tell
       apart (the same reason every console gets its own MAC above). */
    char16_t name[10] = {};
    melonDS::u16 namelen = 0;
    for (const char* c = "Player "; *c && namelen < 9; c++)
        name[namelen++] = (char16_t)(u8)*c;
    name[namelen++] = (char16_t)(u'1' + (id & 0x0F));
    for (melonDS::u16 i = namelen; i < 10; i++) name[i] = 0;

    for (melonDS::u16 i = 0; i < 10; i++) inst->firmware_nickname[i] = name[i];
    inst->firmware_nickname_len = namelen;
    inst->firmware_nickname_valid = true;

    for (auto& ud : firmware.GetUserData())
    {
        memcpy(ud.Nickname, name, sizeof(name));
        ud.NameLength = namelen;
        ud.UpdateChecksum(); /* the header's checksum does not cover this block */
    }

    const melonDS::MacAddress& m = inst->console_mac;
    std::fprintf(stderr,
                 "[melonDS bridge] %s: wireless MAC %02X:%02X:%02X:%02X:%02X:%02X (%s), "
                 "user name %u chars\n",
                 inst->name.c_str(), m[0], m[1], m[2], m[3], m[4], m[5],
                 (id > 0) ? "offset from the shared firmware MAC" : "firmware MAC",
                 (unsigned)namelen);
}

MDInstance* md_create(int console_type, const char* name)
{
    if (console_type != 0)
    {
        std::fprintf(stderr, "[melonDS bridge] only NDS mode is supported for now\n");
        return nullptr;
    }

    auto inst = std::make_unique<MDInstance>();
    inst->name = name ? name : "player";

    /* NDSArgs defaults give us FreeBIOS, generated firmware, the software
       renderer and the JIT - everything a headless splitscreen instance needs. */
    melonDS::NDSArgs args {};
    inst->nds = std::make_unique<melonDS::NDS>(std::move(args), (void*)inst.get());

    return inst.release();
}

void md_destroy(MDInstance* inst)
{
    if (!inst) return;
    md_link_detach(inst);
    inst->nds.reset();
    delete inst;
}

/* melonDS's other frontend runs the software 3D rasteriser on its own thread per
   console - "3D.Soft.Threaded" defaults to true in its config - and our bridge
   never told the renderer anything, so every console rasterised in sequence with
   its CPU and JIT. Threaded, the 3D rasteriser produces the frame for line y
   while the main thread emulates the lines before it, and the 2D compositor
   waits on a per-line semaphore for exactly the line it needs, which is the same
   picture rendered in the same order by the same code.

   Measured, four MKDS consoles, race scene (2026-10-20): the rasteriser was 10.7
   of the 24.4ms a console thread spent per frame. */
static void apply_render_settings(MDInstance* inst)
{
    melonDS::RendererSettings settings {};
    settings.ScaleFactor = 1;
    settings.Threaded = true;
    settings.HiresCoordinates = false;
    settings.BetterPolygons = false;
    inst->nds->GetRenderer().SetRenderSettings(settings);
}

int md_load_rom(MDInstance* inst, const char* rom_path)
{
    if (!inst || !inst->nds) return set_error(inst, "no emulator instance");
    if (!rom_path || !*rom_path) return set_error(inst, "empty ROM path");

    std::vector<u8> romdata;
    if (!read_whole_file(rom_path, romdata))
        return set_error(inst, "failed to read ROM file");

    if (romdata.size() < 0x200)
        return set_error(inst, "ROM file is too small to be a DS ROM");

    /* Header: 12 byte internal title at offset 0. */
    std::string title(romdata.data(), romdata.data() + 12);
    while (!title.empty() && (title.back() == '\0' || title.back() == ' '))
        title.pop_back();

    /* Game code: the second thing a cartridge is, after its title. Four ASCII
       bytes at offset 0x0C; kept verbatim (not trimmed) because the code is
       fixed-width and a space is not a terminator. */
    const std::string game_code(romdata.data() + 0x0C, romdata.data() + 0x10);

    const std::string sav = sav_path_for(rom_path);
    std::unique_ptr<u8[]> savedata;
    u32 savelen = 0;

    if (file_exists(sav))
    {
        std::vector<u8> savbytes;
        if (read_whole_file(sav, savbytes))
        {
            savelen = (u32)savbytes.size();
            savedata = std::make_unique<u8[]>(savelen);
            std::memcpy(savedata.get(), savbytes.data(), savelen);
            std::fprintf(stderr, "[melonDS bridge] %s: loaded %u bytes of save RAM\n",
                         inst->name.c_str(), savelen);
        }
    }

    melonDS::NDSCart::NDSCartArgs args {};
    args.SRAM = std::move(savedata);
    args.SRAMLength = savelen;

    auto romlen = (u32)romdata.size();
    auto cart = melonDS::NDSCart::ParseROM(romdata.data(), romlen, inst, std::move(args));
    if (!cart)
        return set_error(inst, "failed to parse DS ROM");

    if (inst->nds->CartInserted())
        inst->nds->EjectCart();

    inst->nds->SetNDSCart(std::move(cart));

    /* Before Reset: the console reads its MAC out of the firmware over SPI as
       it boots, so the per-console identity has to be in the image by then. */
    apply_console_mac(inst);

    inst->nds->Reset();
    apply_render_settings(inst);

    if (inst->nds->NeedsDirectBoot())
        inst->nds->SetupDirectBoot(rom_path);

    /* Reset() parks the console; Start() lets the CPUs actually run. Without
       this the LCD keeps scanning but no instruction ever executes. */
    inst->nds->Start();
    inst->buttons = 0;
    push_buttons(inst);

    inst->rom_path = rom_path;
    inst->rom_title = title;
    inst->rom_game_code = game_code;
    inst->sav_path = sav;
    inst->stop_reason = MD_STOP_NONE;
    inst->last_error.clear();

    std::fprintf(stderr, "[melonDS bridge] %s: loaded \"%s\" (%u bytes)\n",
                 inst->name.c_str(), title.c_str(), romlen);
    return 0;
}

void md_eject_rom(MDInstance* inst)
{
    if (!inst || !inst->nds) return;
    if (inst->nds->CartInserted())
        inst->nds->EjectCart();
    inst->rom_title.clear();
    inst->rom_game_code.clear();
    inst->rom_path.clear();
}

int md_set_bios(MDInstance* inst, const char* arm9_path, const char* arm7_path)
{
    if (!inst || !inst->nds) return set_error(inst, "no emulator instance");

    std::vector<u8> bios9;
    std::vector<u8> bios7;

    if (arm9_path && *arm9_path)
    {
        if (!read_whole_file(arm9_path, bios9) || bios9.size() != 0x1000)
            return set_error(inst, "invalid ARM9 BIOS image");
        std::array<u8, 0x1000> image {};
        std::memcpy(image.data(), bios9.data(), image.size());
        inst->nds->SetARM9BIOS(image);
    }

    if (arm7_path && *arm7_path)
    {
        if (!read_whole_file(arm7_path, bios7) || bios7.size() != 0x4000)
            return set_error(inst, "invalid ARM7 BIOS image");
        std::array<u8, 0x4000> image {};
        std::memcpy(image.data(), bios7.data(), image.size());
        inst->nds->SetARM7BIOS(image);
    }

    return 0;
}

int md_set_firmware(MDInstance* inst, const char* firmware_path)
{
    if (!inst || !inst->nds) return set_error(inst, "no emulator instance");
    if (!firmware_path || !*firmware_path) return set_error(inst, "empty firmware path");

    std::vector<u8> fw;
    if (!read_whole_file(firmware_path, fw))
        return set_error(inst, "failed to read firmware image");

    /* Firmware copies and pads the buffer, so a short/garbage image is caught
       by the size check above rather than here. */
    inst->nds->SetFirmware(melonDS::Firmware(fw.data(), (u32)fw.size()));

    /* A new image means a new base MAC: the per-console offset has to be
       re-derived from this one at the next boot. */
    inst->firmware_mac_valid = false;
    return 0;
}

int md_write_ram(MDInstance* inst, u32 addr, const void* data, size_t len)
{
    return md_write_ram_cpu(inst, 9, addr, data, len);
}

int md_write_ram_cpu(MDInstance* inst, int cpu, u32 addr, const void* data, size_t len)
{
    if (!inst || !inst->nds || !data) return -1;
    if ((addr & 3) || (len & 3))
        return set_error(inst, "md_write_ram_cpu requires 4-byte alignment");
    if (cpu != 7 && cpu != 9)
        return set_error(inst, "md_write_ram_cpu takes cpu 7 or 9");

    const u8* bytes = static_cast<const u8*>(data);
    for (size_t i = 0; i < len; i += 4)
    {
        u32 word;
        std::memcpy(&word, bytes + i, 4);
        /* The write half of md_read_ram_cpu: the reader went in first because
           "what value is this ARM7 waiting on" is a read, but a variable that
           holds a console can only be settled by changing it, and the ARM7's
           0x03800000/0x06000000 are not reachable from an ARM9 write. */
        if (cpu == 7)
            inst->nds->ARM7Write32(addr + (u32)i, word);
        else
            inst->nds->ARM9Write32(addr + (u32)i, word);
    }

    return 0;
}

int md_read_ram(MDInstance* inst, u32 addr, void* out, size_t len)
{
    return md_read_ram_cpu(inst, 9, addr, out, len);
}

int md_read_ram_cpu(MDInstance* inst, int cpu, u32 addr, void* out, size_t len)
{
    if (!inst || !inst->nds || !out) return -1;
    if ((addr & 3) || (len & 3))
        return set_error(inst, "md_read_ram_cpu requires 4-byte alignment");
    if (cpu != 7 && cpu != 9)
        return set_error(inst, "md_read_ram_cpu takes cpu 7 or 9");

    u8* bytes = static_cast<u8*>(out);
    for (size_t i = 0; i < len; i += 4)
    {
        /* The two CPUs have their own address spaces: an ARM9 read of an ARM7
           address (0x03800000 WRAM, 0x06000000 the game's own ARM7 image) sees
           nothing at all, which is why the routine that posts into an ARM9
           queue could not be read for eleven passes. */
        const u32 word = (cpu == 7) ? inst->nds->ARM7Read32(addr + (u32)i)
                                    : inst->nds->ARM9Read32(addr + (u32)i);
        std::memcpy(bytes + i, &word, 4);
    }

    return 0;
}

int md_get_regs(MDInstance* inst, int cpu, u32* out16)
{
    if (!inst || !inst->nds || !out16) return -1;
    if (cpu != 7 && cpu != 9) return set_error(inst, "md_get_regs takes cpu 7 or 9");

    /* ARM9 is an ARMv5 and ARM7 an ARMv4, so the two cores are unrelated types
       and the copy cannot be shared. */
    if (cpu == 7)
    {
        for (int i = 0; i < 16; i++) out16[i] = inst->nds->ARM7.R[i];
    }
    else
    {
        for (int i = 0; i < 16; i++) out16[i] = inst->nds->ARM9.R[i];
    }
    return 0;
}

int md_get_ipc_stats(MDInstance* inst, MDIPCStats* out)
{
    if (!inst || !inst->nds || !out) return -1;

    /* Unsynchronised reads of counters the emulating thread bumps: each is a
       single aligned 64-bit load, and the question they answer ("did this side
       ever push a word") is about orders of magnitude, not one word. */
    inst->nds->GetIPCTraffic(out->arm9_sends, out->arm7_sends,
                             out->arm9_recvs, out->arm7_recvs);
    return 0;
}

void md_reset(MDInstance* inst)
{
    if (!inst || !inst->nds) return;

    if (inst->nds->CartInserted())
        inst->nds->EjectCart();

    inst->nds->Reset();
    apply_render_settings(inst);

    if (!inst->rom_path.empty())
    {
        md_load_rom(inst, inst->rom_path.c_str());
        return;
    }

    inst->nds->Start();
    push_buttons(inst);
    inst->stop_reason = MD_STOP_NONE;
}

int md_set_jit(MDInstance* inst, int enabled)
{
    if (!inst || !inst->nds) return -1;
    inst->nds->SetJITArgs(enabled ? std::optional<melonDS::JITArgs>(melonDS::JITArgs())
                                  : std::nullopt);
    return 0;
}

int md_jit_enabled(MDInstance* inst)
{
    if (!inst || !inst->nds) return 0;
    return inst->nds->IsJITEnabled() ? 1 : 0;
}

/* ---------------------------------------------------------------------- input */

void md_set_buttons(MDInstance* inst, u32 mask)
{
    if (!inst) return;
    inst->buttons = mask;
}

void md_touch(MDInstance* inst, int x, int y)
{
    if (!inst) return;
    /* The frame matters as much as the coordinate: a menu route is a list of
       one-shot taps at the exact frame a given boot reaches a given screen, and
       a route that landed cannot be read back off its own log without it -
       re-deriving a working MP-Hunters join cost three runs on 2026-09-29
       because these lines named the point and not the frame. */
    std::fprintf(stderr, "[input] frame %u player %d touch -> %d,%d\n",
                 inst->frame_counter, inst->slot + 1, x, y);
    inst->touch_active = true;
    inst->touch_x = x;
    inst->touch_y = y;
}

void md_release_touch(MDInstance* inst)
{
    if (!inst) return;
    /* The host re-asserts the whole input state every frame, so this fires
       60x/second with no press in progress; log only the actual down->up
       transition or the run log drowns (102385 lines in one MKDS run). */
    if (inst->touch_active)
        std::fprintf(stderr, "[input] frame %u player %d touch released\n",
                     inst->frame_counter, inst->slot + 1);
    inst->touch_active = false;
}

void md_set_lid(MDInstance* inst, int closed)
{
    if (!inst) return;
    inst->lid_closed = closed != 0;
}

/* Reads one touchscreen channel the way the guest's ARM7 does: write the
   control byte to start a conversion, then clock out the two result bytes.

   The first dummy transfer is load-bearing. TSC::Write() refreshes Data from the
   *current* DataPos and only then acts on the control byte, so the byte sitting
   in the shift register right after starting a conversion is still the previous
   channel's - reading it there yields a mix of two conversions. The result bytes
   arrive on the two transfers after that. */
static u32 read_tsc_channel(melonDS::TSC* tsc, u8 control)
{
    tsc->Write(control);

    tsc->Write(0x00);
    const u8 high = tsc->Read(); /* (ConvResult >> 5) & 0x7F */
    tsc->Write(0x00);
    const u8 low = tsc->Read();  /* (ConvResult << 3) & 0xFF */

    return ((u32)high << 5) | ((u32)low >> 3);
}

int md_touch_probe(MDInstance* inst, int touching, int x, int y, MDTouchProbe* out)
{
    if (!inst || !inst->nds || !out) return -1;

    inst->touch_active = touching != 0;
    inst->touch_x = x;
    inst->touch_y = y;

    /* Push it through the same call RunFrame makes, so the probe reports the
       machine's real state rather than merely what we asked for. */
    if (inst->touch_active)
        inst->nds->TouchScreen((melonDS::u16)x, (melonDS::u16)y);
    else
        inst->nds->ReleaseScreen();

    melonDS::TSC* tsc = inst->nds->SPI.GetTSC();
    if (!tsc) return -1;

    out->pen_down = (inst->nds->KeyInput & (1u << 22)) ? 0 : 1;
    out->adc_y = read_tsc_channel(tsc, 0x90); /* 0x10 = Y channel */
    out->adc_x = read_tsc_channel(tsc, 0xD0); /* 0x50 = X channel */

    /* Leave the controller where the guest expects to find it, so probing a
       running console cannot corrupt the game's own next transfer. */
    tsc->Release();
    return 0;
}

/* ------------------------------------------------------------------ execution */

static void push_buttons(MDInstance* inst)
{
    /* What the player is driving the emulated game with, once per change: the
       v0.1.8r MKDS investigation ended at "the host's game polls and never
       advances", which is consistent with the player's press never arriving
       here. `buttons` is the MD_BTN_* active-high layout from
       melonds_bridge.h (bit 0 = A, 1 = B, 2 = SELECT, 3 = START, 4 = RIGHT,
       5 = LEFT, 6 = UP, 7 = DOWN, 8 = R, 9 = L, 10 = X, 11 = Y); touch is
       logged at the md_touch/md_release_touch transitions, not here. */
    static u32 last_logged[8] = {0};
    const int slot = inst->slot;
    if (slot >= 0 && slot < 8 && inst->buttons != last_logged[slot])
    {
        last_logged[slot] = inst->buttons;
        char keys[80] = "none";
        {
            const char* names[12] = {"A", "B", "SELECT", "START", "RIGHT", "LEFT",
                                     "UP", "DOWN", "R", "L", "X", "Y"};
            int n = 0;
            for (int i = 0; i < 12; i++)
            {
                if (inst->buttons & (1u << i))
                {
                    if (n) keys[n++] = '+';
                    for (const char* c = names[i]; *c && n < 70; c++) keys[n++] = *c;
                }
            }
            keys[n] = 0;
            if (!n) strcpy(keys, "none");
        }
        std::fprintf(stderr,
            "[input] frame %u player %d buttons -> %s (mask %08X)\n",
            inst->frame_counter, slot + 1, keys, inst->buttons);
    }

    inst->nds->SetKeyMask(~inst->buttons & 0xFFF);
}

u32 md_run_frame(MDInstance* inst)
{
    if (!inst || !inst->nds) return 0;
    if (inst->stop_reason != MD_STOP_NONE) return 0;

    push_buttons(inst);

    if (inst->touch_active)
        inst->nds->TouchScreen((melonDS::u16)inst->touch_x, (melonDS::u16)inst->touch_y);
    else
        inst->nds->ReleaseScreen();

    if (inst->lid_closed != inst->nds->IsLidClosed())
        inst->nds->SetLidClosed(inst->lid_closed);

    const u32 lines = inst->nds->RunFrame();
    inst->frame_counter++;
    /* The Download Play re-seed lives here rather than in the wifi event
       because a stalled child has no wifi event: its own init leaves the
       radio down, which cancels Event_Wifi, so a USTimer hook would never
       fire (measured: 2 ticks total, then silence). The frame step is what
       still runs for every console. One-shot and guarded inside. */
    inst->nds->Wifi.ReseedDownloadPlayLink();
    /* Same reasoning as above: only a stalled Download Play child has an
       un-echoed nibble, so this is silent everywhere else. */
    inst->nds->EchoDownloadPlaySync();
    return lines;
}

/* Drains this console's mixed audio into `out` as interleaved stereo s16.

   melonDS's SPU always runs and always fills its output ring - an emulator with
   no audio frontend is not an emulator with no audio, it is one whose ring
   wraps - so the samples are simply there to be read. Until 2026-10-17 nothing
   in this host read them (the app shipped silent); this is the first sink, and
   it exists so a remote player can hear the console they are playing.

   `max_frames` counts stereo frames, not s16 values: one frame is four bytes.
   Returns the number of **s16 values** written (frames * 2), 0 when the ring is
   empty, or -1 for a bad instance. `out_rate` receives the mixer's rate (the
   core's default 48 kHz), which the caller must resample for its sink. */
int md_read_audio(MDInstance* inst, int16_t* out, int max_frames, int* out_rate)
{
    if (!inst || !inst->nds || !out || max_frames <= 0) return -1;
    if (out_rate) *out_rate = 48000;

    const int available = inst->nds->SPU.GetOutputSize();
    if (available <= 0) return 0;
    const int frames = available < max_frames ? available : max_frames;
    return inst->nds->SPU.ReadOutput(out, frames) * 2;
}

int md_get_screens(MDInstance* inst, const u32** top, const u32** bottom)
{
    if (!inst || !inst->nds || !top || !bottom) return -1;

    void* topbuf = nullptr;
    void* botbuf = nullptr;
    if (!inst->nds->GPU.GetFramebuffers(&topbuf, &botbuf))
        return -1;
    if (!topbuf || !botbuf)
        return -1;

    *top = static_cast<const u32*>(topbuf);
    *bottom = static_cast<const u32*>(botbuf);
    return 0;
}

/* ---------------------------------------------------------------------- state */

size_t md_max_state_size(void)
{
    return melonDS::Savestate::DEFAULT_SIZE;
}

size_t md_save_state(MDInstance* inst, u8* out, size_t cap)
{
    if (!inst || !inst->nds || !out || cap == 0) return 0;

    melonDS::Savestate state(out, (u32)cap, true);
    inst->nds->DoSavestate(&state);
    state.Finish();

    if (state.Error) return 0;
    return state.Length();
}

int md_load_state(MDInstance* inst, const u8* data, size_t len)
{
    if (!inst || !inst->nds || !data || len == 0) return -1;

    /* Savestate wants a mutable buffer, so keep our own copy. */
    inst->state_scratch.assign(data, data + len);

    melonDS::Savestate state(inst->state_scratch.data(), (u32)inst->state_scratch.size(), false);
    if (!inst->nds->DoSavestate(&state)) return -1;
    if (state.Error) return -1;

    inst->stop_reason = MD_STOP_NONE;
    return 0;
}

const u8* md_get_save(MDInstance* inst, size_t* len)
{
    if (!inst || !inst->nds) return nullptr;

    const u32 savelen = inst->nds->GetNDSSaveLength();
    if (len) *len = savelen;
    if (savelen == 0) return nullptr;
    return inst->nds->GetNDSSave();
}

int md_flush_save(MDInstance* inst, const char* path)
{
    if (!inst || !inst->nds || !path || !*path) return -1;

    const u32 savelen = inst->nds->GetNDSSaveLength();
    const u8* savedata = inst->nds->GetNDSSave();
    if (!savedata || savelen == 0) return -1;

    if (!write_whole_file(path, savedata, savelen))
        return -1;

    inst->sav_path = path;
    return 0;
}

/* ------------------------------------------------------------------ link bus */

void md_link_init(void)
{
    std::lock_guard<std::mutex> lock(g_link_lock);
    if (!g_link_bus)
    {
        g_link_bus = std::make_unique<melonDS::LocalMP>();
        g_link_connected = 0;
        g_link_epoch = std::chrono::steady_clock::now();
        std::fprintf(stderr, "[link-event] +0.000s wall bus: init (16 slots)\n");
    }
}

void md_link_shutdown(void)
{
    std::lock_guard<std::mutex> lock(g_link_lock);
    /* The bus object is deliberately never destroyed. Platform's MP_* hooks
       fetch it through md::link_bus(), which returns the raw pointer after
       releasing g_link_lock, and the core calls those hooks from each
       console's driver thread. Freeing the bus here could therefore free it
       under a thread that is inside LocalMP right now - a use-after-free the
       host cannot defend against from the outside. Resetting it to its
       just-constructed state gives the next session the same clean slate
       without ever invalidating a pointer. */
    if (g_link_bus) g_link_bus->Reset();
    g_link_connected = 0;
    std::fprintf(stderr, "[link-event] +%.3fs wall bus: shutdown (state reset, bus kept alive)\n",
        session_secs());
}

void md_link_attach(MDInstance* inst, int slot)
{
    if (!inst) return;
    if (slot < 0 || slot >= MD_MAX_INSTANCES)
    {
        set_error(inst, "link slot out of range");
        return;
    }

    md_link_init();

    /* Moving an instance between slots has to leave the old one cleanly, or the
       bus keeps believing a console is present at both. */
    if (inst->slot >= 0 && inst->slot != slot)
        md_link_detach(inst);

    std::lock_guard<std::mutex> lock(g_link_lock);
    inst->slot = slot;
}

void md_link_detach(MDInstance* inst)
{
    if (!inst) return;

    std::lock_guard<std::mutex> lock(g_link_lock);
    if (inst->slot >= 0)
    {
        if (g_link_bus) g_link_bus->End(inst->slot);
        g_link_connected &= ~(1u << inst->slot);
    }
    inst->slot = -1;
}

void md_link_resync(MDInstance* inst)
{
    if (!inst || inst->slot < 0) return;

    std::lock_guard<std::mutex> lock(g_link_lock);
    if (!g_link_bus) return;

    /* LocalMP::Begin re-anchors this console's packet-FIFO read offset to the
       bus's current write offset and resets its receive semaphores. A console
       whose read pointer drifted into stale bytes - or whose semaphore went
       unsignaled while packets piled up - starts clean. This is the automated
       form of the discovery that clicking "Link test" unstuck a frozen lobby:
       the selftest's wireless power cycle reaches here through the core's real
       MP_Begin path, and the kick is what fixed it, not the test packets. */
    g_link_bus->Begin(inst->slot);
    std::fprintf(stderr,
        "[link-event] +%.3fs wall frame %u p%d (%s): resync (FIFO read offset re-anchored, receive semaphores cleared)\n",
        session_secs(), inst->frame_counter, inst->slot, inst->name.c_str());
}

melonDS::u32 md_link_connected_mask(void)
{
    std::lock_guard<std::mutex> lock(g_link_lock);
    return g_link_connected;
}

void md_link_set_recv_timeout(int milliseconds)
{
    std::lock_guard<std::mutex> lock(g_link_lock);
    if (g_link_bus) g_link_bus->SetRecvTimeout(milliseconds < 0 ? 0 : milliseconds);
    std::fprintf(stderr, "[link-event] +%.3fs wall bus: recv_timeout=%dms\n",
        session_secs(), g_link_bus ? g_link_bus->GetRecvTimeout() : 0);
}

int md_link_recv_timeout(void)
{
    std::lock_guard<std::mutex> lock(g_link_lock);
    return g_link_bus ? g_link_bus->GetRecvTimeout() : 0;
}

void md_link_set_reply_timeout(int milliseconds)
{
    std::lock_guard<std::mutex> lock(g_link_lock);
    if (g_link_bus) g_link_bus->SetReplyTimeout(milliseconds < 0 ? 0 : milliseconds);
    std::fprintf(stderr, "[link-event] +%.3fs wall bus: recv_timeout=%dms reply_timeout=%dms\n",
        session_secs(), g_link_bus ? g_link_bus->GetRecvTimeout() : 0,
        g_link_bus ? g_link_bus->GetReplyTimeout() : 0);
}

int md_link_reply_timeout(void)
{
    std::lock_guard<std::mutex> lock(g_link_lock);
    return g_link_bus ? g_link_bus->GetReplyTimeout() : 0;
}

void md_link_set_reply_budget(MDInstance* inst, int64_t budget_ns)
{
    if (!inst || inst->slot < 0) return;

    /* Published by the host immediately before the frame that may spend it,
       negative to leave the waits unbounded (stock melonDS). Both halves of the
       number matter: the ceiling above bounds one reply window, this bounds all
       of them put together within a single frame. See
       MPInterface::SetReplyBudget for the measurement behind it. */
    std::lock_guard<std::mutex> lock(g_link_lock);
    if (g_link_bus) g_link_bus->SetReplyBudget(inst->slot, (long long)budget_ns);
}

melonDS::u32 md_link_begin_count(MDInstance* inst)
{
    return inst ? inst->link_begin_count : 0;
}

melonDS::u32 md_link_end_count(MDInstance* inst)
{
    return inst ? inst->link_end_count : 0;
}

int md_link_send_packet(MDInstance* inst, const void* data, size_t len, uint64_t timestamp)
{
    if (!inst || inst->slot < 0 || !data || len == 0) return 0;

    std::lock_guard<std::mutex> lock(g_link_lock);
    if (!g_link_bus) return 0;

    const int queued = g_link_bus->SendPacket(inst->slot, (u8*)data, (int)len, timestamp);

    /* Counted here rather than via note_packet_sent: the lock is already held. */
    inst->link_packets_sent++;
    if (queued > 0) inst->link_bytes_sent += (u64)queued;
    return queued;
}

int md_link_send_reply(MDInstance* inst, const void* data, size_t len, uint64_t timestamp, uint16_t aid)
{
    if (!inst || inst->slot < 0 || !data || len == 0) return 0;

    std::lock_guard<std::mutex> lock(g_link_lock);
    if (!g_link_bus) return 0;

    const int queued = g_link_bus->SendReply(inst->slot, (u8*)data, (int)len, timestamp, aid);

    inst->link_packets_sent++;
    if (queued > 0) inst->link_bytes_sent += (u64)queued;
    return queued;
}

int md_link_recv_packet(MDInstance* inst, void* out, size_t cap, uint64_t* timestamp)
{
    if (!inst || inst->slot < 0 || !out || cap == 0) return 0;

    std::lock_guard<std::mutex> lock(g_link_lock);
    if (!g_link_bus) return 0;

    u64 ts = 0;
    const int got = g_link_bus->RecvPacket(inst->slot, (u8*)out, &ts);
    if (got > 0)
    {
        inst->link_packets_received++;
        inst->link_bytes_received += (u64)got;
        inst->link_packets_read_drained++;
        inst->link_bytes_read_drained += (u64)got;
        if (timestamp) *timestamp = ts;
        md::probe_observe(inst, "drain", (const u8*)out, (size_t)got, ts);
    }
    return got;
}

int md_link_recv_packet_full(MDInstance* inst, void* out, size_t cap, uint64_t* timestamp,
                             uint32_t* sender, uint32_t* type)
{
    if (!inst || inst->slot < 0 || !out || cap == 0) return 0;

    std::lock_guard<std::mutex> lock(g_link_lock);
    if (!g_link_bus) return 0;

    u64 ts = 0;
    u32 from = 0, kind = 0;
    // Same buffer contract as md_link_recv_packet: the bus writes the whole
    // entry or nothing, so the caller passes a frame-size buffer (the joiner
    // uses 2048, as it always has). No floor is enforced here for exactly the
    // same reason none is enforced there - and a floor above the caller's
    // buffer reads as an empty bus, which is what a first cut of this function
    // did (0x948 against a 2048 buffer: the joiner heard nothing at all).
    const int got = g_link_bus->RecvPacketFull(inst->slot, (u8*)out, &ts, &from, &kind);
    if (got > 0)
    {
        inst->link_packets_received++;
        inst->link_bytes_received += (u64)got;
        inst->link_packets_read_drained++;
        inst->link_bytes_read_drained += (u64)got;
        if (timestamp) *timestamp = ts;
        if (sender) *sender = from;
        if (type) *type = kind;
        md::probe_observe(inst, "drain", (const u8*)out, (size_t)got, ts);
    }
    return got;
}

int md_link_requeue_packet(MDInstance* inst, uint32_t sender, uint32_t type,
                           const void* data, size_t len, uint64_t timestamp)
{
    if (!inst || inst->slot < 0 || !data || len == 0) return 0;

    std::lock_guard<std::mutex> lock(g_link_lock);
    if (!g_link_bus) return 0;

    // A returned frame is not a new delivery: the read counters already fired
    // when it was drained, so only the bus's own requeue reports. out==len
    // whole or nothing, like every other path through this lock.
    return g_link_bus->RequeuePacket(inst->slot, sender, type, (u8*)data, (int)len, timestamp);
}

int md_probe_arm(MDInstance* inst, const void* expect, size_t len, uint64_t ts)
{
    if (!inst || !expect || len == 0 || len > sizeof(inst->probe.expect)) return -1;

    std::lock_guard<std::mutex> lock(md::g_probe_lock);
    MDProbe& pr = inst->probe;
    memset(pr.expect, 0, sizeof(pr.expect));
    pr.fcs_written = melonDS::LocalMP::ApplyWireFCS(pr.expect, (const u8*)expect, len);
    pr.expect_len = len;
    pr.expect_ts = ts;
    pr.observed = false;
    pr.payload_ok = false;
    pr.ts_ok = false;
    pr.seen_by[0] = 0;
    pr.armed = true;
    return 0;
}

void md_probe_disarm(MDInstance* inst)
{
    if (!inst) return;

    /* Clears the observation too, not just the arming: a caller that disarms
       and then asks has to be told "nothing armed", never handed the result of
       whatever the last expectation saw (the Rust side reads first and disarms
       after, and this is what keeps that order from being load-bearing). */
    std::lock_guard<std::mutex> lock(md::g_probe_lock);
    MDProbe& pr = inst->probe;
    pr.armed = false;
    pr.observed = false;
    pr.payload_ok = false;
    pr.ts_ok = false;
    pr.seen_by[0] = 0;
}

int md_probe_result(MDInstance* inst, MDProbeResult* out)
{
    if (!inst || !out) return -1;

    std::lock_guard<std::mutex> lock(md::g_probe_lock);
    const MDProbe& pr = inst->probe;
    out->armed = pr.armed ? 1u : 0u;
    out->observed = pr.observed ? 1u : 0u;
    out->payload_ok = pr.payload_ok ? 1u : 0u;
    out->ts_ok = pr.ts_ok ? 1u : 0u;
    std::snprintf(out->seen_by, sizeof(out->seen_by), "%s", pr.seen_by);
    return 0;
}

int md_link_test_attach(MDInstance* inst, int on)
{
    if (!inst || inst->slot < 0) return -1;

    std::lock_guard<std::mutex> lock(g_link_lock);
    if (!g_link_bus) return -1;

    if (on) g_link_bus->Begin(inst->slot);
    else g_link_bus->End(inst->slot);
    return 0;
}

int md_link_stats(MDInstance* inst, MDLinkStats* out)
{
    if (!inst || !out) return -1;

    std::lock_guard<std::mutex> lock(g_link_lock);
    out->connected = g_link_connected;
    out->begin_count = inst->link_begin_count;
    out->end_count = inst->link_end_count;
    out->packets_sent = inst->link_packets_sent;
    out->packets_received = inst->link_packets_received;
    out->bytes_sent = inst->link_bytes_sent;
    out->bytes_received = inst->link_bytes_received;
    out->packets_read_game = inst->link_packets_read_game;
    out->bytes_read_game = inst->link_bytes_read_game;
    out->packets_read_drained = inst->link_packets_read_drained;
    out->bytes_read_drained = inst->link_bytes_read_drained;
    if (g_link_bus && inst->slot >= 0)
        g_link_bus->GetDelivered(inst->slot, &out->packets_delivered, &out->bytes_delivered);
    out->rx_broadcast = inst->link_rx_broadcast;
    out->rx_cmd = inst->link_rx_cmd;
    out->rx_reply = inst->link_rx_reply;
    out->rx_ack = inst->link_rx_ack;
    out->rx_polls = inst->link_rx_polls;
    out->tx_cmd = inst->link_tx_cmd;
    out->tx_reply = inst->link_tx_reply;
    out->tx_ack = inst->link_tx_ack;
    out->rx_empty_polls = inst->link_rx_empty_polls;
    out->last_rx_us = inst->link_last_rx_us;
    out->epoch_us = (u64)std::chrono::duration_cast<std::chrono::microseconds>(
                        g_link_epoch.time_since_epoch())
                        .count();
    return 0;
}

uint64_t md_link_stats_canary(uint8_t* out, size_t cap)
{
    if (!out || cap < sizeof(MDLinkStats)) return 0;

    /* Every field carries a distinct small number: its position in this
       declaration order. A reader in another language asserts each of its own
       fields against the number this side put in *that name*, so a field that
       moved, was dropped, or was written in the wrong place shows up as a
       named mismatch instead of as a plausible-looking number that means
       something else. `offsetof` would not do it: two u64 fields swapped in
       place keep their offsets and change only which name reads them. */
    MDLinkStats s {};
#define MDCAN(name, idx) s.name = (decltype(s.name))idx
    MDCAN(connected, 0);
    MDCAN(begin_count, 1);
    MDCAN(end_count, 2);
    MDCAN(packets_sent, 3);
    MDCAN(packets_received, 4);
    MDCAN(bytes_sent, 5);
    MDCAN(bytes_received, 6);
    MDCAN(packets_delivered, 7);
    MDCAN(bytes_delivered, 8);
    MDCAN(packets_read_game, 9);
    MDCAN(bytes_read_game, 10);
    MDCAN(packets_read_drained, 11);
    MDCAN(bytes_read_drained, 12);
    MDCAN(rx_broadcast, 13);
    MDCAN(rx_cmd, 14);
    MDCAN(rx_reply, 15);
    MDCAN(rx_ack, 16);
    MDCAN(rx_polls, 17);
    MDCAN(tx_cmd, 18);
    MDCAN(tx_reply, 19);
    MDCAN(tx_ack, 20);
    MDCAN(rx_empty_polls, 21);
    MDCAN(last_rx_us, 22);
    MDCAN(epoch_us, 23);
#undef MDCAN

    memset(out, 0, sizeof(s));
    memcpy(out, &s, sizeof(s));
    return (uint64_t)sizeof(s);
}

int md_link_mp_digest(MDInstance* inst, MDMPDigest* out)
{
    if (!inst || !out) return -1;
    memset(out, 0, sizeof(*out));

    if (inst->nds)
    {
        /* Refresh the interrupt snapshot first: the counters below read as
           "since reset" and the register values only as "now", so they have to
           be sampled together with the export that carries them. */
        inst->nds->Wifi.SampleMPIrqSnapshot();
        const melonDS::Wifi::MPDiag& d = inst->nds->Wifi.GetMPDiag();

        out->role = (uint32_t)d.role;
        out->cmds_rx = d.cmds_rx;
        out->cmds_rx_dup = d.cmds_rx_dup;
        out->answers_sent = d.answers_sent;
        out->answers_blank = d.answers_blank;
        out->answers_canned = d.answers_canned;
        out->arms = d.arms;
        out->aids_set = d.aids_set;
        out->window_reads = d.window_reads;
        out->deauth_sent = d.deauth_sent;
        out->deauth_recv = d.deauth_recv;
        out->cmds_tx = d.cmds_tx;
        out->cmds_tx_dup = d.cmds_tx_dup;
        out->acks_sent = d.acks_sent;
        out->acks_client_fail = d.acks_client_fail;
        out->last_cmd_seq = d.last_cmd_seq;
        out->last_cmd_len = d.last_cmd_len;
        memcpy(out->last_cmd_body, d.last_cmd_body, sizeof(out->last_cmd_body));
        out->last_answer_len = d.last_answer_len;
        memcpy(out->last_answer_body, d.last_answer_body, sizeof(out->last_answer_body));
        out->last_deauth_reason = d.last_deauth_reason;
        out->last_ack_clientfail = d.last_ack_clientfail;
        out->last_cmd_us = d.last_cmd_us;
        out->last_answer_us = d.last_answer_us;

        for (int i = 0; i < 16; i++)
        {
            out->irq_raised[i] = d.irq_raised[i];
            out->irq_acked[i] = d.irq_acked[i];
        }
        out->irq_cpu_line = d.irq_cpu_line;
        out->block_beacon_irq14 = d.block_beacon_irq14_now;
        out->w_if = d.if_now;
        out->w_ie = d.ie_now;
        out->w_uscntcnt = d.uscntcnt_now;
        out->w_uscmpcnt = d.uscmpcnt_now;
        out->w_bcncount1 = d.bcncount1_now;
        out->w_bcncount2 = d.bcncount2_now;
        out->w_txslot_beacon = d.txslot_beacon_now;
        out->w_txslot_reply1 = d.txslot_reply1_now;
        out->w_txslot_reply2 = d.txslot_reply2_now;
        out->w_txbusy = d.txbusy_now;
        out->w_txreqread = d.txreqread_now;
        out->w_powerstate = d.powerstate_now;
        out->w_powertx = d.powertx_now;
        out->comstatus = d.comstatus_now;
        out->uscounter = d.uscounter_now;
        out->uscompare = d.uscompare_now;
    }

    std::lock_guard<std::mutex> lock(g_link_lock);
    if (g_link_bus && inst->slot >= 0)
    {
        melonDS::LocalMP::MPReplyDiag rd {};
        g_link_bus->GetReplyDiag(inst->slot, &rd);

        out->replies_written = rd.replies_written;
        out->replies_collected = rd.collected;
        out->replies_blank = rd.collected_blank;
        out->windows_empty = rd.windows_empty;
        out->stale_dropped = rd.stale_dropped;
        out->reanchor_dropped = rd.reanchor_dropped;
        out->not_looking = rd.not_looking;
    }

    return 0;
}

uint64_t md_link_mp_digest_size(void)
{
    return (uint64_t)sizeof(MDMPDigest);
}

int md_wifi_io_report(MDInstance* inst, char* out, int cap)
{
    if (!inst || !inst->nds || !out || cap <= 0) return -1;
    int n = inst->nds->Wifi.FormatMPIoReport(out, (size_t)cap);
    if (n < 0) return -1;
    /* The wireless picture carries the channel a game feeds its own ARM7
       wireless driver over as well: without it, "this console's game stopped
       composing commands" and "it never asked the other CPU to send anything"
       are the same observation. */
    const int m = inst->nds->FormatIPCTrace(out + n, (size_t)(cap - n));
    if (m > 0) n += m;
    return n;
}

int md_mem_watch(MDInstance* inst, uint32_t lo, uint32_t hi)
{
    if (!inst || !inst->nds) return -1;
    inst->nds->SetMemWatch(lo, hi);
    return 0;
}

int md_mem_watch_report(MDInstance* inst, char* out, int cap)
{
    if (!inst || !inst->nds || !out || cap <= 0) return -1;
    return inst->nds->FormatMemWatch(out, (size_t)cap);
}

int md_pc_trace(MDInstance* inst, int cpu, uint32_t lo, uint32_t hi)
{
    if (!inst || !inst->nds) return -1;
    inst->nds->SetPcTrace(cpu, lo, hi);
    return 0;
}

int md_pc_trace_report(MDInstance* inst, char* out, int cap)
{
    if (!inst || !inst->nds || !out || cap <= 0) return -1;
    return inst->nds->FormatPcTrace(out, (size_t)cap);
}

int md_link_mp_vary(MDInstance* inst, int kind, int value, const uint8_t* body,
                    int bodylen, int* armed, int* left, uint64_t* applied)
{
    if (!inst || !inst->nds) return -1;
    /* 1 = the body override, 0 = clear. Both spelling lists - this one and the
       Rust control channel's - are kept in step deliberately: a kind the control
       channel accepts and this function refuses is an experiment that silently
       never runs (measured 2026-09-28: a matrix stopped after its first cell on
       `md_link_mp_vary failed (1)`). */
    if (kind < 0 || kind > 1) return set_error(inst, "md_link_mp_vary kind must be 0 or 1");
    if (bodylen < 0) bodylen = 0;

    inst->nds->Wifi.SetMPProbe(kind, value, body, bodylen);

    /* What it armed *as*, which is 0 for a body probe with no body: the caller
       is told what is really armed rather than what it asked for. */
    int k = 0, l = 0;
    u64 a = 0;
    inst->nds->Wifi.GetMPProbe(&k, &l, &a);
    if (armed) *armed = k;
    if (left) *left = l;
    if (applied) *applied = a;
    return 0;
}

int md_link_slot(MDInstance* inst)
{
    return inst ? inst->slot : -1;
}

int md_link_attached(MDInstance* inst)
{
    if (!inst || inst->slot < 0) return -1;

    std::lock_guard<std::mutex> lock(g_link_lock);
    if (!g_link_bus) return -1;
    return g_link_bus->IsAttached(inst->slot) ? 1 : 0;
}

int md_console_mac(MDInstance* inst, unsigned char* out6)
{
    if (!inst || !out6) return -1;
    /* Set by apply_console_mac at ROM load, before the console boots. Until
       then there is no identity to hand out, which is a refusal and not a
       zero MAC. */
    if (!inst->firmware_mac_valid) return -1;
    std::memcpy(out6, inst->console_mac.data(), 6);
    return 0;
}

int md_console_name(MDInstance* inst, char* out, int cap)
{
    if (!inst || !out || cap <= 0) return -1;
    if (!inst->firmware_nickname_valid) return -1;

    /* Read back out of the firmware the guests actually read, not out of a copy
       the bridge kept: this is the name that appears in a game's room list, so
       a reader has to see the same bytes a game sees. UTF-16 -> UTF-8; the
       field is Latin-1-safe ASCII in practice ("Player N"). */
    const auto& ud = inst->nds->GetFirmware().GetEffectiveUserData();
    melonDS::u16 n = ud.NameLength;
    if (n > 10) n = 10;
    int written = 0;
    for (melonDS::u16 i = 0; i < n; i++)
    {
        char32_t c = ud.Nickname[i];
        char utf8[4];
        int len = 0;
        if (c < 0x80) { utf8[0] = (char)c; len = 1; }
        else if (c < 0x800) { utf8[0] = (char)(0xC0 | (c >> 6)); utf8[1] = (char)(0x80 | (c & 0x3F)); len = 2; }
        else { utf8[0] = (char)(0xE0 | (c >> 12)); utf8[1] = (char)(0x80 | ((c >> 6) & 0x3F)); utf8[2] = (char)(0x80 | (c & 0x3F)); len = 3; }
        if (written + len >= cap) break;
        std::memcpy(out + written, utf8, (size_t)len);
        written += len;
    }
    out[written] = 0;
    return written;
}

/* ---------------------------------------------------------------- diagnostics */

const char* md_last_error(MDInstance* inst)
{
    if (!inst) return "no instance";
    return inst->last_error.c_str();
}

int md_rom_title(MDInstance* inst, char* out, size_t cap)
{
    if (!inst || !out || cap == 0) return -1;
    out[0] = '\0';
    if (inst->rom_title.empty()) return -1;

    std::snprintf(out, cap, "%s", inst->rom_title.c_str());
    return 0;
}

int md_rom_game_code(MDInstance* inst, char* out, size_t cap)
{
    if (!inst || !out || cap == 0) return -1;
    out[0] = '\0';
    if (inst->rom_game_code.size() != 4) return -1;

    std::snprintf(out, cap, "%s", inst->rom_game_code.c_str());
    return 0;
}

u32 md_frame_counter(MDInstance* inst)
{
    return inst ? inst->frame_counter : 0;
}

u32 md_read_gpu(MDInstance* inst, u32 addr)
{
    if (!inst || !inst->nds) return 0;
    /* Read through the ARM9's address space so VRAM windows, palettes, OAM and
       I/O registers all behave the way the emulated CPU sees them. */
    return inst->nds->ARM9Read32(addr);
}

melonDS::u16 md_read_gpu16(MDInstance* inst, u32 addr)
{
    if (!inst || !inst->nds) return 0;
    return inst->nds->ARM9Read16(addr);
}

melonDS::u16 md_read_io16(MDInstance* inst, int cpu, u32 addr)
{
    if (!inst || !inst->nds) return 0;
    return (cpu == 7) ? inst->nds->ARM7Read16(addr) : inst->nds->ARM9Read16(addr);
}

int md_write_io32(MDInstance* inst, int cpu, u32 addr, u32 value)
{
    if (!inst || !inst->nds) return -1;
    if (cpu == 7) inst->nds->ARM7Write32(addr, value);
    else          inst->nds->ARM9Write32(addr, value);
    return 0;
}

int md_write_io16(MDInstance* inst, int cpu, u32 addr, melonDS::u16 value)
{
    if (!inst || !inst->nds) return -1;
    if (cpu == 7) inst->nds->ARM7Write16(addr, value);
    else          inst->nds->ARM9Write16(addr, value);
    return 0;
}

u32 md_get_pc(MDInstance* inst, int cpu)
{
    if (!inst || !inst->nds) return 0;
    if (cpu == 9) return inst->nds->ARM9.R[15];
    if (cpu == 7) return inst->nds->ARM7.R[15];
    return 0;
}

int md_dp_boot(MDInstance* inst, const void* image, size_t len,
               u32 header_dest, u32 header_size,
               u32 arm9_dest, u32 arm9_size, u32 arm9_entry,
               u32 arm7_dest, u32 arm7_temp, u32 arm7_size, u32 arm7_entry,
               const MDDPSession* session)
{
    if (!inst || !inst->nds || !image || !session) return -1;

    melonDS::NDS::DownloadPlayBootInfo info {};
    info.ARM9Entry = arm9_entry;
    info.ARM7Entry = arm7_entry;
    info.HeaderDest = header_dest;
    info.HeaderSize = header_size;
    info.ARM9Dest = arm9_dest;
    info.ARM9Size = arm9_size;
    info.ARM7Dest = arm7_dest;
    info.ARM7Temp = arm7_temp;
    info.ARM7Size = arm7_size;
    info.Channel = (int)session->channel;
    info.AID = (int)session->aid;
    std::memcpy(info.HostBSSID, session->host_bssid, 6);
    info.HostSerial = session->host_serial;
    info.HostToken = session->host_token;
    info.HostCapability = session->host_capability;
    info.HostInterval = session->host_interval;
    info.HostRateMask = session->host_rate_mask;
    info.HostBeaconSize = session->host_beacon_size;

    if (!inst->nds->SetupDownloadPlayBoot(static_cast<const u8*>(image), (u32)len, info))
        return set_error(inst, "download-play boot: image does not fit the RSA load map");

    std::fprintf(stderr,
                 "[melonDS bridge] %s: booted the downloaded image (%zu bytes) - "
                 "header -> %08X, ARM9 -> %08X (entry %08X), ARM7 -> %08X (entry %08X), "
                 "session carried: channel %d, BSSID %02X:%02X:%02X:%02X:%02X:%02X, AID %d\n",
                 inst->name.c_str(), len, info.HeaderDest, info.ARM9Dest, info.ARM9Entry,
                 info.ARM7Dest, info.ARM7Entry, info.Channel,
                 info.HostBSSID[0], info.HostBSSID[1], info.HostBSSID[2],
                 info.HostBSSID[3], info.HostBSSID[4], info.HostBSSID[5], info.AID);
    return 0;
}

int md_console_asleep(MDInstance* inst)
{
    if (!inst || !inst->nds) return 0;
    return (inst->nds->CPUStop & melonDS::CPUStop_Sleep) != 0;
}

int md_stop_reason(MDInstance* inst)
{
    if (!inst) return MD_STOP_UNKNOWN;

    switch (inst->stop_reason)
    {
    case MD_STOP_NONE:
    case MD_STOP_EXTERNAL:
    case MD_STOP_GBAMODE_NOT_SUPPORTED:
    case MD_STOP_BAD_EXCEPTION_REGION:
    case MD_STOP_POWER_OFF:
        return inst->stop_reason;
    default:
        return MD_STOP_UNKNOWN;
    }
}

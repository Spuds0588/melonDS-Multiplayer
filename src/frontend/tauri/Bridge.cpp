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
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "NDS.h"
#include "NDSCart.h"
#include "Savestate.h"
#include "GPU.h"

#include "LocalMP.h"

using melonDS::u32;
using melonDS::u8;

/* ------------------------------------------------------------ instance state */

struct MDInstance
{
    std::string name;
    std::unique_ptr<melonDS::NDS> nds;
    std::string last_error;
    std::string rom_path;
    std::string rom_title;
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
}

void notify_mp_end(void* userdata)
{
    auto* inst = static_cast<MDInstance*>(userdata);
    if (!inst) return;

    std::lock_guard<std::mutex> lock(g_link_lock);
    inst->link_end_count++;
    if (inst->slot >= 0) g_link_connected &= ~(1u << inst->slot);
}

bool link_enabled()
{
    return link_bus() != nullptr;
}

} // namespace md

/* ------------------------------------------------------------------ lifecycle */

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
    inst->nds->Reset();

    if (inst->nds->NeedsDirectBoot())
        inst->nds->SetupDirectBoot(rom_path);

    /* Reset() parks the console; Start() lets the CPUs actually run. Without
       this the LCD keeps scanning but no instruction ever executes. */
    inst->nds->Start();
    inst->buttons = 0;
    push_buttons(inst);

    inst->rom_path = rom_path;
    inst->rom_title = title;
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
    return 0;
}

int md_write_ram(MDInstance* inst, u32 addr, const void* data, size_t len)
{
    if (!inst || !inst->nds || !data) return -1;
    if ((addr & 3) || (len & 3))
        return set_error(inst, "md_write_ram requires 4-byte alignment");

    const u8* bytes = static_cast<const u8*>(data);
    for (size_t i = 0; i < len; i += 4)
    {
        u32 word;
        std::memcpy(&word, bytes + i, 4);
        inst->nds->ARM9Write32(addr + (u32)i, word);
    }

    return 0;
}

int md_read_ram(MDInstance* inst, u32 addr, void* out, size_t len)
{
    if (!inst || !inst->nds || !out) return -1;
    if ((addr & 3) || (len & 3))
        return set_error(inst, "md_read_ram requires 4-byte alignment");

    u8* bytes = static_cast<u8*>(out);
    for (size_t i = 0; i < len; i += 4)
    {
        const u32 word = inst->nds->ARM9Read32(addr + (u32)i);
        std::memcpy(bytes + i, &word, 4);
    }

    return 0;
}

void md_reset(MDInstance* inst)
{
    if (!inst || !inst->nds) return;

    if (inst->nds->CartInserted())
        inst->nds->EjectCart();

    inst->nds->Reset();

    if (!inst->rom_path.empty())
    {
        md_load_rom(inst, inst->rom_path.c_str());
        return;
    }

    inst->nds->Start();
    push_buttons(inst);
    inst->stop_reason = MD_STOP_NONE;
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
    inst->touch_active = true;
    inst->touch_x = x;
    inst->touch_y = y;
}

void md_release_touch(MDInstance* inst)
{
    if (!inst) return;
    inst->touch_active = false;
}

void md_set_lid(MDInstance* inst, int closed)
{
    if (!inst) return;
    inst->lid_closed = closed != 0;
}

/* ------------------------------------------------------------------ execution */

static void push_buttons(MDInstance* inst)
{
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
    return lines;
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
        std::fprintf(stderr, "[melonDS bridge] local multiplayer bus ready (16 slots)\n");
    }
}

void md_link_shutdown(void)
{
    std::lock_guard<std::mutex> lock(g_link_lock);
    g_link_bus.reset();
    g_link_connected = 0;
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

melonDS::u32 md_link_connected_mask(void)
{
    std::lock_guard<std::mutex> lock(g_link_lock);
    return g_link_connected;
}

void md_link_set_recv_timeout(int milliseconds)
{
    std::lock_guard<std::mutex> lock(g_link_lock);
    if (g_link_bus) g_link_bus->SetRecvTimeout(milliseconds < 0 ? 0 : milliseconds);
}

int md_link_recv_timeout(void)
{
    std::lock_guard<std::mutex> lock(g_link_lock);
    return g_link_bus ? g_link_bus->GetRecvTimeout() : 0;
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

    return g_link_bus->SendPacket(inst->slot, (u8*)data, (int)len, timestamp);
}

int md_link_recv_packet(MDInstance* inst, void* out, size_t cap, uint64_t* timestamp)
{
    if (!inst || inst->slot < 0 || !out || cap == 0) return 0;

    std::lock_guard<std::mutex> lock(g_link_lock);
    if (!g_link_bus) return 0;

    melonDS::u64 ts = 0;
    const int got = g_link_bus->RecvPacket(inst->slot, (u8*)out, &ts);
    if (got > 0 && timestamp) *timestamp = ts;
    return got;
}

int md_link_slot(MDInstance* inst)
{
    return inst ? inst->slot : -1;
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

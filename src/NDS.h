/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#ifndef NDS_H
#define NDS_H

#include <memory>
#include <string>
#include <optional>
#include <atomic>
#include <functional>

#include "Platform.h"
#include "Savestate.h"
#include "types.h"
#include "NDSCart.h"
#include "GBACart.h"
#include "SPU.h"
#include "Mic.h"
#include "SPI.h"
#include "RTC.h"
#include "Wifi.h"
#include "AREngine.h"
#include "GPU.h"
#include "ARMJIT.h"
#include "MemRegion.h"
#include "ARMJIT_Memory.h"
#include "ARM.h"
#include "CRC32.h"
#include "DMA.h"
#include "FreeBIOS.h"

// when touching the main loop/timing code, pls test a lot of shit
// with this enabled, to make sure it doesn't desync
//#define DEBUG_CHECK_DESYNC

namespace melonDS
{
struct NDSArgs;
class Firmware;
enum
{
    Event_LCD = 0,
    Event_SPU,
    Event_Wifi,
    Event_RTC,

    Event_DisplayFIFO,
    Event_CartROMTransfer9,
    Event_CartSPITransfer9,
    Event_CartROMTransfer7,
    Event_CartSPITransfer7,
    Event_SPITransfer,
    Event_Div,
    Event_Sqrt,

    // DSi
    Event_DSi_SDMMCTransfer,
    Event_DSi_SDIOTransfer,
    Event_DSi_NWifi,
    Event_DSi_CamIRQ,
    Event_DSi_CamTransfer,
    Event_DSi_DSP,
    Event_DSi_DSPHLE, // TODO use same event for both flavors of DSP?
    Event_DSi_Cart2ROMTransfer9,
    Event_DSi_Cart2SPITransfer9,
    Event_DSi_Cart2ROMTransfer7,
    Event_DSi_Cart2SPITransfer7,
    Event_DSi_Cart1Power,
    Event_DSi_Cart2Power,

    Event_MAX
};

static constexpr u32 MaxEventFunctions = 3;

typedef void (*EventFunc)(void* that, u32 param);
typedef std::initializer_list<EventFunc> EventFuncList;
#define MakeEventThunk(class, func) [](void* that, u32 param) { static_cast<class*>(that)->func(param); }

struct SchedEvent
{
    std::array<EventFunc, MaxEventFunctions> Funcs;
    void* That;
    u64 Timestamp;
    u32 FuncID;
    u32 Param;
};
enum
{
    IRQ_VBlank = 0,
    IRQ_HBlank,
    IRQ_VCount,
    IRQ_Timer0,
    IRQ_Timer1,
    IRQ_Timer2,
    IRQ_Timer3,
    IRQ_RTC,
    IRQ_DMA0,
    IRQ_DMA1,
    IRQ_DMA2,
    IRQ_DMA3,
    IRQ_Keypad,
    IRQ_GBASlot,
    IRQ_Unused14,
    IRQ_Unused15,
    IRQ_IPCSync,
    IRQ_IPCSendDone,
    IRQ_IPCRecv,
    IRQ_CartXferDone,
    IRQ_CartIREQMC,   // IRQ triggered by game cart (example: Pokémon Typing Adventure, BT controller)
    IRQ_GXFIFO,
    IRQ_LidOpen,
    IRQ_SPI,
    IRQ_Wifi,

    // DSi IRQs
    IRQ_DSi_DSP = 24,
    IRQ_DSi_Camera,
    IRQ_DSi_Cart2XferDone,
    IRQ_DSi_Cart2IREQMC,
    IRQ_DSi_NDMA0,
    IRQ_DSi_NDMA1,
    IRQ_DSi_NDMA2,
    IRQ_DSi_NDMA3,
};

enum
{
    // DSi ARM7-side IE2/IF2
    IRQ2_DSi_GPIO18_0 = 0,
    IRQ2_DSi_GPIO18_1,
    IRQ2_DSi_GPIO18_2,
    IRQ2_DSi_Unused3,
    IRQ2_DSi_GPIO33_0,
    IRQ2_DSi_Headphone,
    IRQ2_DSi_BPTWL,
    IRQ2_DSi_GPIO33_3, // "sound enable input"
    IRQ2_DSi_SDMMC,
    IRQ2_DSi_SD_Data1,
    IRQ2_DSi_SDIO,
    IRQ2_DSi_SDIO_Data1,
    IRQ2_DSi_AES,
    IRQ2_DSi_I2C,
    IRQ2_DSi_MicExt
};

enum
{
    CPUStop_DMA9_0 = (1<<0),
    CPUStop_DMA9_1 = (1<<1),
    CPUStop_DMA9_2 = (1<<2),
    CPUStop_DMA9_3 = (1<<3),
    CPUStop_NDMA9_0 = (1<<4),
    CPUStop_NDMA9_1 = (1<<5),
    CPUStop_NDMA9_2 = (1<<6),
    CPUStop_NDMA9_3 = (1<<7),
    CPUStop_DMA9 = 0xFFF,

    CPUStop_DMA7_0 = (1<<16),
    CPUStop_DMA7_1 = (1<<17),
    CPUStop_DMA7_2 = (1<<18),
    CPUStop_DMA7_3 = (1<<19),
    CPUStop_NDMA7_0 = (1<<20),
    CPUStop_NDMA7_1 = (1<<21),
    CPUStop_NDMA7_2 = (1<<22),
    CPUStop_NDMA7_3 = (1<<23),
    CPUStop_DMA7 = (0xFFF<<16),

    CPUStop_Wakeup = (1<<29),
    CPUStop_Sleep = (1<<30),
    CPUStop_GXStall = (1<<31),
};

struct Timer
{
    u16 Reload;
    u16 Cnt;
    u32 Counter;
    u32 CycleShift;
};

enum
{
    Mem9_ITCM       = 0x00000001,
    Mem9_DTCM       = 0x00000002,
    Mem9_BIOS       = 0x00000004,
    Mem9_MainRAM    = 0x00000008,
    Mem9_WRAM       = 0x00000010,
    Mem9_IO         = 0x00000020,
    Mem9_Pal        = 0x00000040,
    Mem9_OAM        = 0x00000080,
    Mem9_VRAM       = 0x00000100,
    Mem9_GBAROM     = 0x00020000,
    Mem9_GBARAM     = 0x00040000,

    Mem7_BIOS       = 0x00000001,
    Mem7_MainRAM    = 0x00000002,
    Mem7_WRAM       = 0x00000004,
    Mem7_IO         = 0x00000008,
    Mem7_Wifi0      = 0x00000010,
    Mem7_Wifi1      = 0x00000020,
    Mem7_VRAM       = 0x00000040,
    Mem7_GBAROM     = 0x00000100,
    Mem7_GBARAM     = 0x00000200,

    // TODO: add DSi regions!
};

// supported GBA slot addon types
enum
{
    GBAAddon_RAMExpansion = 1,
    GBAAddon_RumblePak = 2,
    // Each game in the GBA Boktai trilogy uses the same solar sensor,
    // but Lunar Knights (the only NDS game to use the solar sensor)
    // applies slightly different effects depending on the game.
    GBAAddon_SolarSensorBoktai1 = 3,
    GBAAddon_SolarSensorBoktai2 = 4,
    GBAAddon_SolarSensorBoktai3 = 5,
    GBAAddon_MotionPakHomebrew = 6,
    GBAAddon_MotionPakRetail = 7,
    GBAAddon_GuitarGrip = 8,
};

class SPU;
class SPIHost;
class RTC;
class Wifi;

class AREngine;
class GPU;
class ARMJIT;

class NDS
{
private:
#ifdef JIT_ENABLED
    bool EnableJIT;
#endif
#ifdef GDBSTUB_ENABLED
    bool EnableGDBStub = false;
#endif

public: // TODO: Encapsulate the rest of these members
    void* UserData;

    int ConsoleType;
    int CurCPU;

    SchedEvent SchedList[Event_MAX] {};
    u8 ARM9MemTimings[0x40000][8];
    u32 ARM9Regions[0x40000];
    u8 ARM7MemTimings[0x20000][4];
    u32 ARM7Regions[0x20000];

    u32 NumFrames;
    u32 NumLagFrames;
    bool LagFrameFlag;

    /* Emulated time in microseconds, on a base that every console in a
       lockstep session agrees on: a whole number of DS frames (each console
       advances exactly one frame per session step, so this part is identical
       on all of them) plus this console's own progress through the frame it is
       in.

       The wifi module's own clock (`Wifi::USTimestamp`) is derived from this,
       which is what makes the local-MP protocol's timestamps - a frame's
       timestamp is the sender's clock, and the receiver can only compare it
       with its own - meaningful across two consoles. Before that, each console
       stamped frames with a clock that advanced with its *own* emulated
       cycles, and the games are free to change how long a frame of theirs is
       (MKDS writes VCOUNT every 61 frames, its client more often during a
       join, and a VCOUNT write delays the frame), so the two clocks parted by
       254ms of emulated time over a single MKDS join and never came back. */
    [[nodiscard]] u64 GetSharedUSClock() const noexcept
    {
        const u64 cycles = ((u64)NumFrames * 560190) + (SysTimestamp - FrameStartTimestamp);
        return (cycles * 1000000) / 33513982;
    }

    // no need to worry about those overflowing, they can keep going for atleast 4350 years
    u64 ARM9Timestamp, ARM9Target;
    u64 ARM7Timestamp, ARM7Target;
    u32 ARM9ClockShift;

    u32 IME[2];
    u32 IE[2];
    u32 IF[2];
    u32 IE2;
    u32 IF2;
    Timer Timers[8];

    u32 CPUStop;

    u16 PowerControl9;

    u16 ExMemCnt[2];

protected:
    // These BIOS arrays should be declared *before* the component objects (JIT, SPI, etc.)
    // so that they're initialized before the component objects' constructors run.
    std::array<u8, ARM9BIOSSize> ARM9BIOS;
    std::array<u8, ARM7BIOSSize> ARM7BIOS;
    bool ARM9BIOSNative;
    bool ARM7BIOSNative;
public: // TODO: Encapsulate the rest of these members
    u16 ARM7BIOSProt;

    u8* MainRAM;
    u32 MainRAMMask;

    const u32 MainRAMMaxSize = 0x1000000;

    const u32 SharedWRAMSize = 0x8000;
    u8* SharedWRAM;

    MemRegion SWRAM_ARM9;
    MemRegion SWRAM_ARM7;

    u32 KeyInput;
    u16 RCnt;

    // JIT MUST be declared before all other component objects,
    // as they'll need the memory that it allocates in its constructor!
    // (Reminder: C++ fields are initialized in the order they're declared,
    // regardless of what the constructor's initializer list says.)
    melonDS::ARMJIT JIT;
    ARMv5 ARM9;
    ARMv4 ARM7;
    melonDS::SPU SPU;
    melonDS::Mic Mic;
    SPIHost SPI;
    melonDS::RTC RTC;
    melonDS::Wifi Wifi;
    NDSCart::NDSCartSlot NDSCartSlot;
    GBACart::GBACartSlot GBACartSlot;
    melonDS::GPU GPU;
    melonDS::AREngine AREngine;

    const u32 ARM7WRAMSize = 0x10000;
    u8* ARM7WRAM;

    // provision for DSi second cart slot
    NDSCart::NDSCartSlot* NDSCartSlots[2];

    virtual void Reset();
    void Start();

    /// Stop the emulator.
    virtual void Stop(Platform::StopReason reason = Platform::StopReason::External);

    bool DoSavestate(Savestate* file);

    void SetARM9RegionTimings(u32 addrstart, u32 addrend, u32 region, int buswidth, int nonseq, int seq);
    void SetARM7RegionTimings(u32 addrstart, u32 addrend, u32 region, int buswidth, int nonseq, int seq);

    void LoadBIOS();

    /// @return \c true if the loaded ARM9 BIOS image is a known dump
    /// of a native DS-compatible ARM9 BIOS.
    [[nodiscard]] bool IsLoadedARM9BIOSKnownNative() const noexcept { return ARM9BIOSNative; }
    [[nodiscard]] const std::array<u8, ARM9BIOSSize>& GetARM9BIOS() const noexcept { return ARM9BIOS; }
    void SetARM9BIOS(const std::array<u8, ARM9BIOSSize>& bios) noexcept;

    [[nodiscard]] const std::array<u8, ARM7BIOSSize>& GetARM7BIOS() const noexcept { return ARM7BIOS; }
    void SetARM7BIOS(const std::array<u8, ARM7BIOSSize>& bios) noexcept;

    /// @return \c true if the loaded ARM7 BIOS image is a known dump
    /// of a native DS-compatible ARM9 BIOS.
    [[nodiscard]] bool IsLoadedARM7BIOSKnownNative() const noexcept { return ARM7BIOSNative; }

    [[nodiscard]] NDSCart::CartCommon* GetNDSCart() { return NDSCartSlot.GetCart(); }
    [[nodiscard]] const NDSCart::CartCommon* GetNDSCart() const { return NDSCartSlot.GetCart(); }
    virtual void SetNDSCart(std::unique_ptr<NDSCart::CartCommon>&& cart);
    [[nodiscard]] bool CartInserted() const noexcept { return NDSCartSlot.GetCart() != nullptr; }
    virtual std::unique_ptr<NDSCart::CartCommon> EjectCart() { return NDSCartSlot.EjectCart(); }

    [[nodiscard]] u8* GetNDSSave() { return NDSCartSlot.GetSaveMemory(); }
    [[nodiscard]] const u8* GetNDSSave() const { return NDSCartSlot.GetSaveMemory(); }
    [[nodiscard]] u32 GetNDSSaveLength() const { return NDSCartSlot.GetSaveMemoryLength(); }
    void SetNDSSave(const u8* savedata, u32 savelen);

    const Firmware& GetFirmware() const { return SPI.GetFirmwareMem()->GetFirmware(); }
    Firmware& GetFirmware() { return SPI.GetFirmwareMem()->GetFirmware(); }
    void SetFirmware(Firmware&& firmware) { SPI.GetFirmwareMem()->SetFirmware(std::move(firmware)); }

    const Renderer& GetRenderer() const noexcept { return GPU.GetRenderer(); }
    Renderer& GetRenderer() noexcept { return GPU.GetRenderer(); }
    void SetRenderer(std::unique_ptr<Renderer>&& renderer) noexcept
    {
        if (renderer != nullptr)
            GPU.SetRenderer(std::move(renderer));
    }

    virtual bool NeedsDirectBoot() const;
    void SetupDirectBoot(const std::string& romname);
    virtual void SetupDirectBoot();

    /// The load map of a single-card Download Play child image, as the host's
    /// RSA signature frame states it (GBATEK, DS Download Play): the two
    /// entrypoints at `00h`/`04h`, and the header/ARM9/ARM7 destinations and
    /// sizes at `0Ch`/`14h`, `1Ch`/`24h` and `2Ch`/`34h`. The image buffer is
    /// the host's packet payloads concatenated in packet order, so its three
    /// segments are these lengths back to back.
    struct DownloadPlayBootInfo
    {
        u32 ARM9Entry;
        u32 ARM7Entry;
        u32 HeaderDest;
        u32 HeaderSize;
        u32 ARM9Dest;
        u32 ARM9Size;
        u32 ARM7Dest;
        /// The ARM7's temp staging address (`2Ch`); a real client writes the
        /// ARM7 image here for the ARM9's SHA1 pass before relocating it. Zero
        /// or equal to ARM7Dest means no separate staging copy.
        u32 ARM7Temp;
        u32 ARM7Size;
        /// The channel the host's beacon advertised. A downloaded child does
        /// not scan for one (it was already tuned to the host's when the image
        /// arrived), so the boot keeps this on its radio - see
        /// Wifi::SetDownloadPlaySession(). 0 leaves the radio untuned.
        int Channel;
        /// The host's own BSSID, off the same beacon: the station this client
        /// is associated *with*. A real client keeps it across the hand-over;
        /// see Wifi::SetDownloadPlaySession().
        u8 HostBSSID[6];
        /// The AID the host's association response granted this console, low
        /// nibble (0 = unregistered). The same value `Wifi` reports as
        /// `MPMYAID` while the console is an MP client, and a poll is only this
        /// console's to answer while it is set.
        int AID;
        /// The beacon's game serial (0040xxxx), little-endian: the handoff
        /// block's SSID serial field (027FFC4Eh).
        u32 HostSerial;
        /// The two stream bytes after the serial (low halfword): the handoff
        /// block's SSID stream field (027FFC52h).
        u32 HostToken;
        /// Beacon capability / interval / 1-2Mbit rate mask / computed beacon
        /// size: the handoff block's matching fields (027FFC6Eh/70h/72h/74h
        /// and 027FFC42h).
        u32 HostCapability;
        u32 HostInterval;
        u32 HostRateMask;
        u32 HostBeaconSize;
    };

    /// Boot a single-card Download Play child image the way a real client does
    /// after the transfer: write the header/ARM9/ARM7 segments to the
    /// destinations the RSA frame stated, set the boot indicator to the
    /// DS-Download-Play value, carry the session the client negotiated into its
    /// radio state, and point both CPUs at the entrypoints. This is the "jump
    /// to entrypoints" half of GBATEK's post-transfer step; the radio side
    /// (tuning to the host's channel and re-seeding the association) is set up
    /// here too, and the caller deliberately sends no deauthentication (see the
    /// handoff's own comment in src-tauri/src/dp_joiner.rs).
    ///
    /// Returns true when the image's three segments fit the buffer and were
    /// written; false (and changes nothing) otherwise.
    bool SetupDownloadPlayBoot(const u8* image, u32 imagelen,
                               const DownloadPlayBootInfo& info);

    /* Mirror a posted IPCSYNC nibble the way the child's own ARM9 handshake
       would (see the definition). One frame-step call; silent unless a
       Download Play child's ARM7 is waiting on an echo its sleeping ARM9
       never sends. */
    void EchoDownloadPlaySync();

    [[nodiscard]] GBACart::CartCommon* GetGBACart() { return (ConsoleType == 1) ? nullptr : GBACartSlot.GetCart(); }
    [[nodiscard]] const GBACart::CartCommon* GetGBACart() const {  return (ConsoleType == 1) ? nullptr : GBACartSlot.GetCart(); }

    /// Inserts a GBA cart into the emulated console's Slot-2.
    ///
    /// @param cart The GBA cart, most likely (but not necessarily) returned from GBACart::ParseROM.
    /// To insert an accessory that doesn't use a ROM image
    /// (e.g. the Expansion Pak), create it manually and pass it here.
    /// If \c nullptr, the existing cart is ejected.
    /// If this is a DSi, this method does nothing.
    ///
    /// @post \c cart is \c nullptr and this NDS takes ownership
    /// of the cart object it held, if any.
    void SetGBACart(std::unique_ptr<GBACart::CartCommon>&& cart) { if (ConsoleType == 0) GBACartSlot.SetCart(std::move(cart)); }

    u8* GetGBASave() { return GBACartSlot.GetSaveMemory(); }
    const u8* GetGBASave() const { return GBACartSlot.GetSaveMemory(); }
    u32 GetGBASaveLength() const { return GBACartSlot.GetSaveMemoryLength(); }
    void SetGBASave(const u8* savedata, u32 savelen);

    std::unique_ptr<GBACart::CartCommon> EjectGBACart() { return GBACartSlot.EjectCart(); }

    u32 RunFrame();

    bool IsRunning() const noexcept { return Running; }

    void TouchScreen(u16 x, u16 y);
    void ReleaseScreen();

    void SetKeyMask(u32 mask);

    bool IsLidClosed() const;
    void SetLidClosed(bool closed);

    void RegisterEventFuncs(u32 id, void* that, const EventFuncList& funcs);
    void UnregisterEventFuncs(u32 id);
    void ScheduleEvent(u32 id, bool periodic, s32 delay, u32 funcid, u32 param);
    void CancelEvent(u32 id);

    void debug(u32 p);

    void Halt();

    void MapSharedWRAM(u8 val);

    void UpdateIRQ(u32 cpu);
    void SetIRQ(u32 cpu, u32 irq);
    void ClearIRQ(u32 cpu, u32 irq);
    void SetIRQ2(u32 irq);
    void ClearIRQ2(u32 irq);
    bool HaltInterrupted(u32 cpu) const;
    void StopCPU(u32 cpu, u32 mask);
    void ResumeCPU(u32 cpu, u32 mask);
    void GXFIFOStall();
    void GXFIFOUnstall();

    u32 GetPC(u32 cpu) const;
    u64 GetSysClockCycles(int num);
    void NocashPrint(u32 cpu, u32 addr, bool appendNewline = true);

    void MonitorARM9Jump(u32 addr);

    virtual bool DMAsInMode(u32 cpu, u32 mode) const;
    virtual bool DMAsRunning(u32 cpu) const;
    virtual void CheckDMAs(u32 cpu, u32 mode);
    virtual void StopDMAs(u32 cpu, u32 mode);

    void RunTimers(u32 cpu);

    virtual u8 ARM9Read8(u32 addr);
    virtual u16 ARM9Read16(u32 addr);
    virtual u32 ARM9Read32(u32 addr);
    virtual void ARM9Write8(u32 addr, u8 val);
    virtual void ARM9Write16(u32 addr, u16 val);
    virtual void ARM9Write32(u32 addr, u32 val);

    virtual bool ARM9GetMemRegion(u32 addr, bool write, MemRegion* region);

    virtual u8 ARM7Read8(u32 addr);
    virtual u16 ARM7Read16(u32 addr);
    virtual u32 ARM7Read32(u32 addr);
    virtual void ARM7Write8(u32 addr, u8 val);
    virtual void ARM7Write16(u32 addr, u16 val);
    virtual void ARM7Write32(u32 addr, u32 val);

    virtual bool ARM7GetMemRegion(u32 addr, bool write, MemRegion* region);

    virtual u8 ARM9IORead8(u32 addr);
    virtual u16 ARM9IORead16(u32 addr);
    virtual u32 ARM9IORead32(u32 addr);
    virtual void ARM9IOWrite8(u32 addr, u8 val);
    virtual void ARM9IOWrite16(u32 addr, u16 val);
    virtual void ARM9IOWrite32(u32 addr, u32 val);

    virtual u8 ARM7IORead8(u32 addr);
    virtual u16 ARM7IORead16(u32 addr);
    virtual u32 ARM7IORead32(u32 addr);
    virtual void ARM7IOWrite8(u32 addr, u8 val);
    virtual void ARM7IOWrite16(u32 addr, u16 val);
    virtual void ARM7IOWrite32(u32 addr, u32 val);

#ifdef JIT_ENABLED
    [[nodiscard]] bool IsJITEnabled() const noexcept { return EnableJIT; }
    void SetJITArgs(std::optional<JITArgs> args) noexcept;
#else
    [[nodiscard]] bool IsJITEnabled() const noexcept { return false; }
    void SetJITArgs(std::optional<JITArgs> args) noexcept {}
#endif

#ifdef GDBSTUB_ENABLED
    void SetGdbArgs(std::optional<GDBArgs> args) noexcept;
#else
    void SetGdbArgs(std::optional<GDBArgs> args) noexcept {}
#endif

    /* IPC FIFO traffic since boot (see IPCSent9). A frontend reads this from a
       diagnostic thread; it is a method rather than four public members only
       because the FIFOs themselves are protected.

       "Since boot" is exact: NDS::Reset() is the boot boundary and clears
       these four counters and the trace ring below, so a console that booted
       by running a downloaded Download Play image reports only what it did
       after that boot, not the firmware Download Play client's traffic it
       replaced. */
    void GetIPCTraffic(u64& sent9, u64& sent7, u64& recv9, u64& recv7) const
    {
        sent9 = IPCSent9;
        sent7 = IPCSent7;
        recv9 = IPCRecv9;
        recv7 = IPCRecv7;
    }

    /* One line per traced IPCFIFOSEND word: the direction, the word, and the
       PC/LR of the instruction that pushed it (`see IPCTrace9`). Returns the
       characters written (excluding the terminator), or -1 if the buffer is
       too small. */
    int FormatIPCTrace(char* out, size_t cap) const;

    /* Debug: watch stores into main RAM and ARM7 WRAM and name the instruction that made one.
       A live state variable whose writer is only reachable through a computed
       pointer cannot be found by reading code - there is no literal to search
       for - so this turns the question into "which store touched it" and answers
       it from the store itself. `lo`/`hi` are the inclusive window as the CPU
       presents it - main RAM as 0x02000000..0x023FFFFF (either mirror folds
       onto it), ARM7 WRAM canonically as 0x02000000|(addr&0x3FFFFF); `lo > hi`
       disarms it. Arming also
       clears the hits, and only the FIRST kMemWatchMax overlapping stores are
       kept, because the writer wanted is the one that runs before any later
       one. It costs one predictable compare per store while disarmed, and the
       JIT's fast path writes main RAM directly, so a watch is only meaningful on
       the interpreter (MD_NOJIT=1). */
    void SetMemWatch(u32 lo, u32 hi);
    int FormatMemWatch(char* out, size_t cap) const;

    /* Debug: the code one CPU actually runs over a window `[lo, hi)`, with the
       registers it ran it on.

       The store watch above names the instruction that touched a variable; this
       names the instructions the CPU ran and the state they ran with, which is
       what a branch decision is made of. A downloaded image that boots, does
       its wireless bring-up and then stops has left the decision behind by the
       time anything is parked, so a snapshot cannot show it - the question is
       "which code ran on the way, and on which value".

       The PC is the one the CPU is about to execute, taken from the same value
       the JIT dispatches on (ARM.cpp), so it is a real guest address in every
       execution mode. The JIT dispatches whole basic blocks, so under it the
       trace records every block entry - each a real PC, and the one every taken
       branch lands on - while the interpreter records every instruction. Each
       entry carries LR, CPSR and R0..R3 as they were at that PC.

       `cpu` is 0 for the ARM9 and 1 for the ARM7; `lo >= hi` disarms. Arming is
       requested from the control channel's thread and applied by the CPU thread
       at the top of the next RunFrame (ApplyPcTraceRequest), so nothing the CPU
       thread is walking is ever reallocated or cleared from another thread. */
    void SetPcTrace(int cpu, u32 lo, u32 hi);
    void ApplyPcTraceRequest();
    int FormatPcTrace(char* out, size_t cap) const;
    // A cheap guard for the per-instruction hook: the call itself is not free,
    // so the CPU loops only make it while a trace is applied.
    [[nodiscard]] bool PcTraceArmed() const noexcept { return PcTraceCpu >= 0; }
    // Called by the CPU loops (ARM.cpp) at each executed PC; a no-op unless a
    // trace is applied and the PC is in its window.
    void RecordPcVisit(int cpu, u32 pc, u32 lr);

protected:
    void InitTimings();
    u32 SchedListMask;
    u64 SysTimestamp;
    u8 WRAMCnt;
    u8 PostFlag9;
    u8 PostFlag7;
    u16 PowerControl7;
    u16 WifiWaitCnt;
    u8 TimerCheckMask[2];
    u64 TimerTimestamp[2];
    DMA DMAs[8];
    u32 DMA9Fill[4];
    u16 IPCSync9, IPCSync7;
    /* Whether this console booted a downloaded image (set by
       SetupDownloadPlayBoot, cleared by Reset). Gates the Download Play
       assists: a normal cartridge boot must never see them. */
    bool DPBootChild = false;
    u16 IPCFIFOCnt9, IPCFIFOCnt7;
    FIFO<u32, 16> IPCFIFO9; // FIFO in which the ARM9 writes
    FIFO<u32, 16> IPCFIFO7;

    // Traffic counters for the ARM9<->ARM7 IPC FIFO, which is the channel a DS
    // game hands work between its two CPUs over. Counted where the words
    // actually move - the pushing side's IPCFIFOSEND write and the receiving
    // side's IPCFIFORECV read - and deliberately not derived from the FIFO
    // levels: those are empty both before and after an exchange, so a level can
    // never tell a channel that has never been used from one that just finished.
    u64 IPCSent9 = 0; // words the ARM9 pushed out (IPCFIFO9)
    u64 IPCSent7 = 0; // words the ARM7 pushed out (IPCFIFO7)
    u64 IPCRecv9 = 0; // words the ARM9 popped from IPCFIFO7
    u64 IPCRecv7 = 0; // words the ARM7 popped from IPCFIFO9

    /* The last words pushed out through IPCFIFOSEND, in order, each with the
       pusher's PC and LR. The counters above say a channel is being used; this
       says *what was said through it and by which instruction*, which is how a
       console's own request to the other CPU can be read back as a value and a
       code address instead of inferred from its effect on the wireless block.
       The CPU's PC is recorded as the interpreter reports it (the address of
       the executing instruction plus 8, the ARM pipeline offset). */
    struct IPCTraceEntry { u32 pc; u32 lr; u32 word; };
    static const int kIPCTrace = 64;
    IPCTraceEntry IPCTrace9[kIPCTrace] {};
    IPCTraceEntry IPCTrace7[kIPCTrace] {};
    u64 IPCTraceN9 = 0;  // words ever traced on IPCFIFO9 (ARM9 -> ARM7)
    u64 IPCTraceN7 = 0;  // ... and on IPCFIFO7 (ARM7 -> ARM9)
    void RecordIPCTrace(int cpu, u32 word);

    /* The register file is kept because a watched store is usually a copy or a
       store out of a field, and the value alone then says nothing: the writer's
       source pointer is what names the thing being copied. It is stored for
       every hit (64 of them is a few kB) and printed for the first few. */
    struct MemWatchHit { u32 pc; u32 lr; u32 cpsr; u32 addr; u32 val; u16 size; u16 _pad; u32 regs[16]; };
    static const int kMemWatchMax = 64;
    static const int kMemWatchVerbose = 4;
    MemWatchHit MemWatchHits[kMemWatchMax] {};
    int MemWatchN = 0;     // hits kept (at most kMemWatchMax)
    u64 MemWatchTotal = 0; // overlapping stores seen since the window was armed
    u32 MemWatchLo = 1;    // empty window while Lo > Hi
    u32 MemWatchHi = 0;
    void RecordMemWatch(int cpu, u32 addr, u32 val, int size);

    /* The path tracer's state: one CPU, one window, and a rolling ring of the
       last visits to it with the registers at the moment of the visit. All
       fixed-size: the control thread only records a request, and the CPU thread
       applies it (see SetPcTrace/ApplyPcTraceRequest).

       The ring has to retain a whole boot transition, not just a steady state:
       a code window can run ~10k blocks per frame, so the old 16384-entry ring
       held under two frames and a trace armed across a Download Play boot had
       already forgotten the bring-up by the time anything could read it. 262144
       entries (7 MiB per console) covers tens of frames past the boot's Reset,
       which is the window this instrument exists to keep. */
    struct PcTraceEntry { u32 pc; u32 lr; u32 cpsr; u32 r0; u32 r1; u32 r2; u32 r3; };
    static const int kPcTraceRing = 262144;
    PcTraceEntry PcTraceRing[kPcTraceRing] {};
    u64 PcTraceTotal = 0;   // visits seen since the window was applied
    int PcTraceN = 0;       // entries kept (at most kPcTraceRing)
    int PcTraceCpu = -1;    // applied state: -1 while disarmed
    u32 PcTraceLo = 1;      // empty window while Lo >= Hi
    u32 PcTraceHi = 0;
    u32 PcTraceFirstFrame = 0;
    // Written by the control thread, then published through the generation;
    // read by the CPU thread only after it sees the new generation.
    std::atomic<u64> PcTraceReqGen { 0 };
    u64 PcTraceAppliedGen = 0;   // CPU thread only
    int PcTraceReqCpu = -1;
    u32 PcTraceReqLo = 1;
    u32 PcTraceReqHi = 0;

    u16 DivCnt;
    alignas(u64) u32 DivNumerator[2];
    alignas(u64) u32 DivDenominator[2];
    alignas(u64) u32 DivQuotient[2];
    alignas(u64) u32 DivRemainder[2];
    u16 SqrtCnt;
    alignas(u64) u32 SqrtVal[2];
    u32 SqrtRes;
    u16 KeyCnt[2];
    bool Running;
    bool RunningGame;
    u64 LastSysClockCycles;
    u64 FrameStartTimestamp;
    u64 NextTarget();
    u64 NextTargetSleep();
    void CheckKeyIRQ(u32 cpu, u32 oldkey, u32 newkey);
    void Reschedule(u64 target);
    void RunSystemSleep(u64 timestamp);
    void RunSystem(u64 timestamp);
    void HandleTimerOverflow(u32 tid);
    u16 TimerGetCounter(u32 timer);
    void TimerStart(u32 id, u16 cnt);
    void StartDiv();
    void DivDone(u32 param);
    void SqrtDone(u32 param);
    void StartSqrt();
    void RunTimer(u32 tid, s32 cycles);
    void UpdateWifiTimings();
    void SetWifiWaitCnt(u16 val);
    void SetExMemCnt(u32 cpu, u16 val, u16 mask);
    void SetGBASlotTimings();
    void EnterSleepMode();
    template <CPUExecuteMode cpuMode>
    u32 RunFrame();

public:
    NDS(NDSArgs&& args, void* userdata = nullptr) noexcept : NDS(std::move(args), 0, userdata) {}
    NDS() noexcept;
    virtual ~NDS() noexcept;
    NDS(const NDS&) = delete;
    NDS& operator=(const NDS&) = delete;
    NDS(NDS&&) = delete;
    NDS& operator=(NDS&&) = delete;

    static thread_local NDS* Current;
protected:
    explicit NDS(NDSArgs&& args, int type, void* userdata) noexcept;
    virtual u32 GetSavestateConfig();
    virtual void DoSavestateExtra(Savestate* file) {}
};

}
#endif // NDS_H

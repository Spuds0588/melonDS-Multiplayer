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

#ifndef WIFI_H
#define WIFI_H

#include "Savestate.h"

namespace melonDS
{
class WifiAP;
class NDS;
class Wifi
{
public:

    enum
    {
        W_ID = 0x000,

        W_ModeReset = 0x004,
        W_ModeWEP = 0x006,
        W_TXStatCnt = 0x008,
        W_IF = 0x010,
        W_IE = 0x012,

        W_MACAddr0 = 0x018,
        W_MACAddr1 = 0x01A,
        W_MACAddr2 = 0x01C,
        W_BSSID0 = 0x020,
        W_BSSID1 = 0x022,
        W_BSSID2 = 0x024,
        W_AIDLow = 0x028,
        W_AIDFull = 0x02A,

        W_TXRetryLimit = 0x02C,
        W_RXCnt = 0x030,
        W_WEPCnt = 0x032,

        W_TRXPower = 0x034,
        W_PowerUS = 0x036,
        W_PowerTX = 0x038,
        W_PowerState = 0x03C,
        W_PowerForce = 0x040,
        W_PowerDownCtrl = 0x48,

        W_Random = 0x044,

        W_RXBufBegin = 0x050,
        W_RXBufEnd = 0x052,
        W_RXBufWriteCursor = 0x054,
        W_RXBufWriteAddr = 0x056,
        W_RXBufReadAddr = 0x058,
        W_RXBufReadCursor = 0x05A,
        W_RXBufCount = 0x05C,
        W_RXBufDataRead = 0x060,
        W_RXBufGapAddr = 0x062,
        W_RXBufGapSize = 0x064,

        W_TXBufWriteAddr = 0x068,
        W_TXBufCount = 0x06C,
        W_TXBufDataWrite = 0x070,
        W_TXBufGapAddr = 0x074,
        W_TXBufGapSize = 0x076,

        W_TXSlotBeacon = 0x080,
        W_TXBeaconTIM = 0x084,
        W_ListenCount = 0x088,
        W_BeaconInterval = 0x08C,
        W_ListenInterval = 0x08E,
        W_TXSlotCmd = 0x090,
        W_TXSlotReply1 = 0x094,
        W_TXSlotReply2 = 0x098,
        W_TXSlotLoc1 = 0x0A0,
        W_TXSlotLoc2 = 0x0A4,
        W_TXSlotLoc3 = 0x0A8,
        W_TXReqReset = 0x0AC,
        W_TXReqSet = 0x0AE,
        W_TXReqRead = 0x0B0,
        W_TXSlotReset = 0x0B4,
        W_TXBusy = 0x0B6,
        W_TXStat = 0x0B8,
        W_Preamble = 0x0BC,
        W_CmdTotalTime = 0x0C0,
        W_CmdReplyTime = 0x0C4,
        W_RXFilter = 0x0D0,
        W_RXLenCrop = 0x0DA,
        W_RXFilter2 = 0x0E0,

        W_USCountCnt = 0x0E8,
        W_USCompareCnt = 0x0EA,
        W_CmdCountCnt = 0x0EE,

        W_USCount0 = 0x0F8,
        W_USCount1 = 0x0FA,
        W_USCount2 = 0x0FC,
        W_USCount3 = 0x0FE,
        W_USCompare0 = 0x0F0,
        W_USCompare1 = 0x0F2,
        W_USCompare2 = 0x0F4,
        W_USCompare3 = 0x0F6,

        W_ContentFree = 0x10C,
        W_PreBeacon = 0x110,
        W_CmdCount = 0x118,
        W_BeaconCount1 = 0x11C,
        W_BeaconCount2 = 0x134,

        W_BBCnt = 0x158,
        W_BBWrite = 0x15A,
        W_BBRead = 0x15C,
        W_BBBusy = 0x15E,
        W_BBMode = 0x160,
        W_BBPower = 0x168,

        W_RFData2 = 0x17C,
        W_RFData1 = 0x17E,
        W_RFBusy = 0x180,
        W_RFCnt = 0x184,

        W_TXHeaderCnt = 0x194,
        W_RFPins = 0x19C,

        W_RXStatIncIF = 0x1A8,
        W_RXStatIncIE = 0x1AA,
        W_RXStatHalfIF = 0x1AC,
        W_RXStatHalfIE = 0x1AE,
        W_TXErrorCount = 0x1C0,
        W_RXCount = 0x1C4,

        W_CMDStat0 = 0x1D0,
        W_CMDStat1 = 0x1D2,
        W_CMDStat2 = 0x1D4,
        W_CMDStat3 = 0x1D6,
        W_CMDStat4 = 0x1D8,
        W_CMDStat5 = 0x1DA,
        W_CMDStat6 = 0x1DC,
        W_CMDStat7 = 0x1DE,

        W_TXSeqNo = 0x210,
        W_RFStatus = 0x214,
        W_IFSet = 0x21C,
        W_RXTXAddr = 0x268,
    };

    Wifi(melonDS::NDS& nds);
    ~Wifi();
    void Reset();
    void DoSavestate(Savestate* file);

    void SetPowerCnt(u32 val);

    /* Carry a Download Play client's negotiated session across its boot into
       the downloaded image: the host's channel, the host's own BSSID, and the
       AID the host granted this console.

       Why all three and not just the channel: a real client does not come out
       of a single-card transfer as a stranger. Its firmware's Download Play
       client negotiated this session, and the hand-over into the downloaded
       image does not undo any of it - the channel, the association's BSSID and
       the assigned AID all live in the wireless block and survive the jump.
       This emulator's boot is a whole-console Reset() (see
       NDS::SetupDownloadPlayBoot), which clears all of it: `CurChannel` to 0
       and `IsMP`/`IsMPClient`/`MPMYAID` to nothing. Without this the child's
       radio is untuned *and* it is not a station the host's beacons or polls
       are addressed to, which is the shape measured in
       logs/v9dpsession_finding.txt: the host keeps beaconing and polling, and
       the freshly booted child is not in the conversation at all.

       It is a held fallback, exactly like the channel: a child that goes on to
       negotiate its own association replaces all of it through CheckRX's normal
       paths, and a deauthentication clears the association half (see the
       frametype 00C0 arm). `Wifi::Reset()` clears the whole hold. */
    void SetDownloadPlaySession(int channel, const u8* hostbssid, int aid);

    /* Run the booted child's own radio bring-up prefix on the Download Play
       hand-off, so its RX guards pass when its driver's frames start arriving.

       Measured live (to-do.md "Guest bring-up branch", 2026-10-12): on a
       real route the child sits at PowerState 0x0200 with TRX power 0, so it
       hears nothing, and its driver's own bring-up never advances past
       power-down on its own. The model reaches power-on through exactly one
       path (see UpdatePowerStatus): PowerDownCtrl bit 1 set plus a recompute
       with ModeReset set (reqflags 3: TRXPower up, PowerState bit 8, the
       -2048us delay, IRQ11) - and the delay only completes once the wifi
       event runs, which needs bus attach (MP_Begin via W_PowerUS bit 0 clear
       with the wifi power domain enabled). This replays the model inputs the
       host's own driver wrote live (the ModeWEP 004A's PowerDownCtrl side
       effect, the ModeReset/PowerTX prefix, the PowerUS 0 attach), through
       Write() so the power model transitions genuinely. Deliberately NOT
       carried: W_ModeWEP itself (driver-owned, overwritten by bring-up within
       seconds), the W_PowerForce pulse (a transient whose recomputes settle
       to nothing), and W_PowerState (the core drops the write unless
       ModeWEP&7==3 - the model reaches state 0 on its own through the
       delay). The RX window is seeded first (4000-4800, the value the powered
       radio latches) because the freshly Reset window is empty and the
       driver's self-test writes only zeros; and the RXCnt enable bit is set
       by direct store, never through Write(), whose bit-15 arm fires a TX.
       NDS::SetupDownloadPlayBoot enables the wifi power domain first - the
       PowerUS write below only attaches because of it. */
    void SetDownloadPlayLink();

    /* One-shot post-boot restore of the carried session, fired from USTimer.
       See the definition for the measured reason. */
    void ReseedDownloadPlayLink();

    /* One line per *change* to a register that decides whether this console's
       transceiver is on (the mode/WEP control, the power-management registers,
       the serial-port enables and the BB/RF power+mode registers), with the
       ARM7 PC that made the write. Bounded per register and per console, so one
       console's bring-up cannot exhaust the other's budget. Diagnostic only:
       it changes no behaviour and is silent unless MD_LINK_VERBOSE=1. */
    void TracePowerWrite(int id, u16 old, u16 val);
    u32 PowerWriteSeen[20] = {};

    void USTimer(u32 param);

    u16 Read(u32 addr);
    void Write(u32 addr, u16 val);

    const u8* GetMAC() const;
    const u8* GetBSSID() const;

    /* The RF channel this console's radio is currently tuned to (0 = not
       tuned). The local-multiplayer bridge reads it to restamp received
       frames: the virtual bus is channel-blind, but the games are channel-
       aware (CheckRX drops frames whose channel byte differs from CurChannel,
       which is how a host that picked channel 6 becomes invisible to a client
       still scanning on channel 1). */
    int GetCurChannel() const { return CurChannel; }

    /* ---- multiplayer flight recorder ----

       Every question the live MKDS attempts have had to answer by hand, kept as
       a counter instead: did the guest's game get the command at all, did it
       answer, was the answer its own or the firmware's stand-in, did the host
       read it back, and what did either body actually say. The counters cost a
       handful of increments per round, so they stay on always, and the host
       prints them as one line per console per health window (md_link_mp_digest)
       - a failed join should be diagnosable from the last couple of log lines,
       not from a forensic pass over megabytes of trace.

       The bodies are the point: thirteen runs were spent learning that the
       host's poll body and the guest's answer body never moved. A digest that
       carries the last body of each makes that visible at a glance. */
    struct MPDiag
    {
        int role;            // 0 = not in MP, 1 = MP host, 2 = MP client

        // guest side: commands received, answers produced
        u64 cmds_rx;         // command frames delivered to this console's game
        u64 cmds_rx_dup;     // ... of which carried the seqno of the last one
        u64 answers_sent;    // answers this console's game produced
        u64 answers_blank;   // zero length "nothing armed" frames returned
        u64 answers_canned;  // firmware stand-ins (fc=0158)
        u64 arms;            // W_TXSlotReply1 writes with bit 15 set
        u64 aids_set;        // W_AIDLow writes that changed the value
        u64 window_reads;    // reads of the wireless receive RAM window
        u64 deauth_sent;     // deauth frames this console transmitted
        u64 deauth_recv;     // deauth frames it accepted

        // host side
        u64 cmds_tx;         // command frames transmitted
        u64 cmds_tx_dup;     // ... of which repeated the previous seqno
        u64 acks_sent;       // acks transmitted
        u64 acks_client_fail;// ... of which reported a failed client
        u64 reply_deferred;  // rounds whose ack was held open for a client that owed an answer
        u64 reply_defer_exhausted; // ... that gave up after the frame budget (the violation path)
        u64 reply_defer_us;        // emulated microseconds of that waiting, summed

        /* ---- the host's own round loop (v0.1.9bg) ----

           A lobby's heartbeat is the host's *game* writing its CMD slot: one
           command is one round, and the game writes the next only after it has
           read the answers to the last. That makes the game's own departure
           from the lobby a counter rather than an inference - measured at
           three consoles, 60.0 rounds/s rock steady and then exactly zero, a
           few seconds after the second client associates, while the beacon
           slot stays armed (so the firmware goes on beaconing the game's last
           frame), the role stays 1, and every reply the lobby did exchange was
           credited and read. `loop_stop_since_join_us` is the number to read:
           how long after the newest client's association the game gave up. */
        u64 rounds_multi;    // rounds whose client mask addressed 2+ clients
        u64 client_joins;    // association responses this console sent as host
        u64 loop_stops;      // times the game stopped writing its CMD slot
        u64 loop_life_us;    // the last lobby's CMD-writing lifetime
        u64 loop_stop_since_join_us;  // that stop, minus the newest association
        u64 loop_start_us;   // when the current stretch of rounds began
        u64 last_client_join_us;      // when the newest client associated

        /* ---- AID bookkeeping (2026-10-23) ----

           The host's own game states which clients a command addresses in the
           frame itself (payload+2), and the transport polls that field narrowed
           by `MPClientFail`. Keeping both last values is what lets a change be
           printed once instead of every round, and a change is the readable
           form of "the host's game has dropped this client". */
        u16 last_cmd_mask_game;
        u16 last_cmd_mask_poll;

        /* ---- close slice (2026-10-22) ----

           The host's `06 01` command is what both games react to, and the
           three-console room splits on it - one guest re-associates, the
           other's game leaves - so the close is compared between the two
           consoles instead of reconstructed from counters afterwards. The
           last window that credited anyone is kept because "whose answer the
           close consumed last" is part of that comparison. */
        u16 last_collect_mask;
        u64 last_collect_us;
        u16 close_tx;        // `06 01` this console broadcast
        u16 close_rx;        // `06 01` this console was handed
        u64 close_rx_us;     // ... and when (0 = never)

        // last frames seen, bodies included (both roles)
        u16 last_cmd_seq;
        u16 last_cmd_len;    // body bytes, after the 24-byte 802.11 header
        u8  last_cmd_body[16];
        u16 last_answer_len;
        u8  last_answer_body[16];
        u16 last_deauth_reason;
        u16 last_ack_clientfail;
        u64 last_cmd_us;
        u64 last_answer_us;

        /* ---- interrupt level (2026-09-28) ----

           A picture that never changes while the CPUs keep running is what a
           game waiting on an interrupt it never receives looks like, and the
           byte counters above cannot tell that apart from a game that is
           simply idle: both keep the link busy. These two arrays make the
           question askable. `irq_raised[i]` counts every time the wifi block
           asked its CPU for attention on source `i` (`SetIRQ`, i.e. every
           producer: the USCOMPARE wake on 14, the beacon countdown on 2/13,
           the RX-buffer countdown on 9, a completed transfer on 0/6/8/15...),
           and `irq_acked[i]` counts every time the game wrote a 1 to clear
           that same bit. So "is this console's game still servicing the wifi
           IRQ?" is one subtraction, and "which source does the game wait on?"
           is `raised[i] == 0` on a source that is enabled and pending. */
        u64 irq_raised[16];
        u64 irq_acked[16];
        u64 irq_cpu_line;    // 0->nonzero transitions: wifi asserted ARM7 IRQ_Wifi

        /* Live register snapshot, refreshed by SampleMPIrqSnapshot() at the
           moment the digest is exported - a register value only means
           anything as "right now". */
        u16 if_now;          // W_IF: pending, before masking
        u16 ie_now;          // W_IE: enabled
        u16 uscntcnt_now;    // W_USCountCnt: the game's own us counter gate
        u16 uscmpcnt_now;    // W_USCompareCnt: bit0 = USCOMPARE may raise IRQ14
        u16 bcncount1_now;   // W_BeaconCount1: counts down to IRQ14 source 1
        u16 bcncount2_now;   // W_BeaconCount2: counts down to IRQ13 power-down
        u16 txslot_beacon_now;
        u16 txslot_reply1_now;
        u16 txslot_reply2_now;
        u16 txbusy_now;
        u16 txreqread_now;
        u16 powerstate_now;
        u16 powertx_now;
        u16 comstatus_now;
        u64 uscounter_now;
        u64 uscompare_now;
        int block_beacon_irq14_now;
    };

    const MPDiag& GetMPDiag() const { return MPD; }

    /* ---- what this console's own game reads out of the wireless block ----

       A value our emulation fabricates and a value the peer's game wrote are
       identical in every counter taken from the write side or from the wire,
       and different only where a game's own bytecode looks at them. So this
       keeps the reads: a count and last value per register halfword, the last
       `kRing` of them in order, and - for the frame region the console was just
       handed - every halfword it consumes out of it, in order. Always on, no
       logging; dumped as text on demand by FormatMPIoReport. */
    struct MPIo
    {
        static const int kRegWords = 0x800;   // halfword offsets 0x000..0xFFE
        static const int kRing = 512;
        static const int kZone = 256;

        u32 reg_reads[kRegWords];             // reads per register halfword
        u16 reg_last[kRegWords];              // ... and the value last read

        u16 ring_off[kRing];                  // the last reads, in order
        u16 ring_val[kRing];
        u64 ring_total;

        /* ---- which register, and from where ----

           The count/last-value table above says *what* the console's own code
           read; this says *where the decision was made*. A radio driver gates
           its bring-up on a register it read (a status bit, a chip id) or on
           one it believes it wrote, and that branch is an instruction with a
           PC. The last `kRegRing` accesses to the register window - reads and
           writes both, because a gate can sit on either - are kept in order
           with the accessing CPU's PC/LR and the console frame they happened
           on, so the child's post-boot bring-up is the entries after the frame
           the image was booted on. Always on, no logging; dumped by
           FormatMPIoReport. */
        static const int kRegRing = 1024;
        /* The window after a Download Play hand-off, printed on its own by the
           text report (see `Wifi::DpSessionFrame`). The ring cannot serve it
           by itself: 1024 entries is a few frames of a running game, so by
           the time a report is asked for, what the console did after the boot
           has usually rolled out of it. The report prints what the ring still
           holds from the hand-off frame on, states exactly how many entries
           that is, and says when the ring has rolled past the start of the
           window rather than showing an empty window that is indistinguishable
           from a console that never touched its radio again. */
        static const int kRegRingBoot = 256;
        /* How much of the ring the text report prints. The whole ring is kept
           (a run has to be sampled after the fact), but the report is read by
           a person and is size-checked by its caller, so only the tail goes
           into it. */
        static const int kRegRingTail = 192;

        struct MPIoReg
        {
            u32 off;      // halfword offset in the register window
            u32 pc;
            u32 lr;
            u16 val;
            u8  wr;       // 1 = this console's own software wrote it
            u32 frame;    // NDS::NumFrames at the access
        };

        MPIoReg reg_ring[kRegRing];           // the last register accesses, in order
        u64 reg_ring_total;

        u32 regr_pc[kRegWords];               // last reader PC/LR per halfword
        u32 regr_lr[kRegWords];
        u32 regw_pc[kRegWords];               // last writer PC/LR per halfword
        u32 regw_lr[kRegWords];
        u16 regw_val[kRegWords];

        /* Every read of the wireless RAM window, in order, with the value it
           returned. This is the ground truth the zone below annotates: the
           zone says which delivery a read belonged to, and this says what the
           game actually took, including the reads that fell outside it (a
           game reading a region the recorder did not expect is exactly the
           interesting case). */
        u16 rx_off[kRing];
        u16 rx_val[kRing];
        u8  rx_in[kRing];                     // inside the handed frame at read time
        u64 rx_total;

        /* The frame region this console's software was just handed:
           `[zone_start, zone_start + zone_len)` at the 0x1FFE wrap (the
           12-byte RX header the hardware wrote plus the frame behind it), and
           the halfwords the game has taken out of it since. `zone_is_cmd`/
           `zone_seq` are the command's own identity, so the reads can be tied
           back to the round they belong to. */
        u32 zone_start;
        u32 zone_len;
        u16 zone_fc;
        u16 zone_seq;
        int zone_is_cmd;
        u64 zone_gen;
        u64 zone_total;
        u16 zone_off[kZone];
        u16 zone_val[kZone];
        u32 zone_n;

        /* ---- who wrote the frame the host is about to send ----

           The read side above answers "what did this console's own code
           branch on"; this answers the other half of a parked state — "which
           of its instructions put *this* command in the slot". Every write
           the console's software makes into the low wireless RAM (the
           command buffer a Download Play host composes at offset 0) is kept
           with the writer's PC/LR, plus a per-offset last-writer table, so
           the code behind a class byte is a PC that can be disassembled
           instead of a guess. The TX-slot arms are kept separately because
           the arm is the step that makes the composed frame actually leave.
           Always on, no logging; dumped by FormatMPIoReport. */
        static const int kWrRing = 256;
        static const int kWrKeep = 0x40;      // command header + body start

        struct MPIoWr { u32 pc; u32 lr; u16 off; u16 val; };

        MPIoWr wr[kWrRing];                   // the last writes, in order
        u64 wr_total;

        u32 wr_pc[kWrKeep];                   // last writer PC per offset
        u32 wr_lr[kWrKeep];
        u16 wr_val[kWrKeep];
        u64 wr_n[kWrKeep];

        u32 wr_cmd_pc, wr_cmd_lr;             // last writer of W_TXSlotCmd
        u16 wr_cmd_val;
        u64 wr_cmd_n;

        /* The writer's whole register file at the moment it armed the command
           slot. The routine that arms the slot and the routine that composes
           the frame are the same code, and the frame's description lives in
           its registers, so the arm is the one instant where "what was this
           frame built from" is answerable without a debugger. */
        u32 wr_cmd_regs[17];
        u32 wr_cmd_cpsr;
        u32 wr_reply_pc, wr_reply_lr;         // last writer of W_TXSlotReply1
        u16 wr_reply_val;
        u64 wr_reply_n;
    };

    const MPIo& GetMPIo() const { return MPIO; }

    /* Record the frame region the console's own software is about to be
       handed, and start a fresh consumption list for it. Called from StartRX
       (every delivery) and from CheckRX's command branch (which builds its
       own header rather than going through StartRX). */
    void SetMPIoZone(u16 start, u16 len, u16 fc, u16 seq, bool is_cmd);

    /* One text report of the above plus the current interrupt snapshot, for
       the control channel and the log. Returns the characters written
       (excluding the terminator), or -1 if the buffer is too small. */
    int FormatMPIoReport(char* out, size_t cap) const;

    /* Fill the live interrupt snapshot in MPD. Called by the bridge when the
       digest is exported, because the register values above are only
       meaningful as "at the instant the digest was taken". */
    void SampleMPIrqSnapshot();

    /* The one-input probe of the parked handshake: kind 1 replaces the body
       bytes the console's game reads (player 1's answers it collects, player
       2's commands it is handed) with `body` for `value` deliveries - 0 or less
       means "until cleared" - and kind 0 clears it. `left` says how many
       deliveries the probe still has to touch, so a caller can watch it fire;
       it counts only a frame with a body behind its 802.11 header, and stays
       armed until it gets one. Diagnostics only, off unless the control channel
       asks (history.md v0.1.9k for what it measured). */
    void SetMPProbe(int kind, int value, const u8* body, int bodylen);
    void GetMPProbe(int* kind, int* left, u64* applied) const;

private:
    melonDS::NDS& NDS;
    u8 RAM[0x2000];
    u16 IO[0x1000>>1];

    static const u8 MPCmdMAC[6];
    static const u8 MPReplyMAC[6];
    static const u8 MPAckMAC[6];

    static const int kTimerInterval = 8;
    static const u32 kTimeCheckMask = ~(kTimerInterval - 1);

    bool Enabled;
    bool PowerOn;

    s32 TimerError;

    u16 Random;

    // general, always-on microsecond counter
    u64 USTimestamp;

    /* Clock probe: how many times this console's US timer has fired, and the
       last time it was reported. Two consoles in one process are supposed to
       tick the same number of times per emulated frame; without these two
       numbers there is no way to tell which one is off, because each console's
       own clock is only ever compared against the other's. */
    u64 ClockTicks;
    u64 ClockLastProbe;

    u64 USCounter;
    u64 USCompare;
    bool BlockBeaconIRQ14;

    u32 CmdCounter;

    u8 BBRegs[0x100];
    u8 BBRegsRO[0x100];

    u8 RFVersion;
    u32 RFRegs[0x40];

    /* The baseband/RF serial ports are not instantaneous. After the console
       starts a transfer - a write to W_BBCnt, or a data write that clocks an RF
       register - the chip is busy for a few microseconds and W_BBBusy /
       W_RFBusy read 1 until it finishes. Both flags were hard-wired to 0 ("TODO
       eventually"), which is invisible to a driver that polls for busy to
       clear, but a driver that reads the flag to learn the transfer has
       started spins forever, and a read of W_BB_READ while a transfer is in
       flight must return the previous transfer's data, not this one's.

       The deadlines are in the console's shared microsecond clock
       (NDS::GetSharedUSClock), the same clock Wifi::USTimestamp carries, so
       they are compared against the console's own emulated time and not the
       host's. They are transient (a transfer lasts microseconds) and are
       deliberately kept out of the savestate stream - see DoSavestate. */
    u64 BBBusyUntil;
    u64 RFBusyUntil;
    /// W_BB_READ: data from the last completed read transfer.
    u8 BBReadResult;

    u32 RFChannelIndex[2];
    u32 RFChannelData[14][2];
    int CurChannel;
    /// The channel kept while the RF registers name none, set by
    /// SetDownloadPlaySession() (0 = none). On hardware a handed-off child
    /// does not scan: it was already tuned to the host's when the image
    /// arrived, but this fork's boot is a whole-console Reset() (see
    /// NDS::SetupDownloadPlayBoot) that leaves CurChannel at 0, so without
    /// this fallback every frame it sends is dropped before a channel byte
    /// is stamped and everything it hears fails the CurChannel check.
    int HeldChannel;
    /// The rest of the session carried across a Download Play boot, set by
    /// SetDownloadPlaySession(): the host's own BSSID and the AID it granted
    /// this console. Kept here rather than only written once so the same fact
    /// can be re-seeded if the boot path is re-entered, and so a reader can ask
    /// what the session was without parsing a log. Cleared by Reset().
    u8 HeldBSSID[6];
    int HeldAID;
    /// The emulated frame the Download Play hand-off was performed on, so the
    /// one window that matters after a child boot - what its radio did from the
    /// reset onward - can be printed on its own (0 = no hand-off). Set with
    /// HeldAID by SetDownloadPlaySession(); cleared by Reset().
    u32 DpSessionFrame;
    /* Whether the post-boot re-seed below has disarmed (the game configured
       its own RX window, so the link is its own now). Cleared with
       DpSessionFrame by Reset() and armed by SetDownloadPlaySession(). Until
       then the re-seed re-applies the carried session every frame the window
       reads zero - the child's init zeroes the hand-off's registers within
       ~40 frames of the boot (measured live), so a delayed one-shot lands
       after the park has already formed. */
    bool DpReseeded;
    /* One-shot log guard for the re-seed (it otherwise runs every frame
       while the window reads zero). */
    bool DpReseedLogged;

    struct TXSlot
    {
        bool Valid;
        u16 Addr;
        u16 Length;
        u8 Rate;
        u8 CurPhase;
        int CurPhaseTime;
        u32 HalfwordTimeMask;
    };

    TXSlot TXSlots[6];
    u8 TXBuffer[0x2000];

    u8 RXBuffer[2048];
    u32 RXBufferPtr;
    int RXTime;
    u32 RXHalfwordTimeMask;

    u32 ComStatus; // 0=waiting for packets  1=receiving  2=sending
    u32 TXCurSlot;
    u32 RXCounter;

    int MPReplyTimer;
    u16 MPClientMask, MPClientFail;
    bool MPLoopAlive;      // the host's game is writing rounds (see MPDiag)

    /* ---- reply rendezvous (2026-10-23) ----

       The host's command slot used to decide the round in one instant: poll,
       then ack with whatever was there, and a client whose answer was one frame
       behind got `clientfail`. Measured at three consoles (9.5% of acks) the
       owed answer was almost always collected by the next window, so the round
       was being closed a moment too early. Phase 14 holds the slot open across
       frame boundaries - the ack is not sent, W_TXBusy stays set, the game keeps
       waiting exactly as it does for a slow radio - and re-polls eagerly once
       per timer tick until every client the command addressed has answered, or
       `MD_REPLY_DEFER_FRAMES` emulated frames have passed (the violation path).
       `MPDeferAsked` is the round's addressed mask, `MPDeferCredited` what has
       been collected so far, `MPDeferStart` the timestamp the round was opened. */
    u16 MPDeferAsked;
    u16 MPDeferCredited;
    u64 MPDeferStart;

    /* Which piece of code is starting a reception, so the delivery trace can
       name it: 0 none, 1 the MP reply handoff, 2 an association response,
       3 a deauth, 4 a frame the regular path took, 5 a held MP frame whose
       timestamp has come. See StartRX in Wifi.cpp. */
    int MPRXSite;

    u8 MPClientReplies[15*1024];

    u16 MPLastSeqno;

    /* The AID this console is associated with, and the answer the game has
       armed for the poll that is about to answer it.

       `MPMYAID` is what a reply is stamped with and what decides whether a
       poll is addressed to this console, because the live `W_AIDLow` is the
       game's own transient state (see MPReplyAID).

       `MPArmSlot`/`MPArmHeld` are the game's reply-slot arm, held until the
       poll it answers arrives: the game arms its answer from inside the
       handler that received the *previous* poll and clears the slot again in
       the same round, so at the next poll the live register is usually 0 while
       the answer the game made for that poll is not. Handing the held arm to
       stock's SendMPReply at the poll is what makes the answer the game's own
       without changing when the game sees the slot consumed - see the note in
       FinishRX for both measurements that rule the alternatives out. */
    u16 MPMYAID;
    u16 MPArmSlot;
    bool MPArmHeld;

    /* The provenance of the arm above, so the poll that consumes it can say
       which arm it took and when that arm was written rather than only that
       something was armed. The serials are what answer "is the answer the host
       collects the guest game's own newly-armed bytes, or one it already spent"
       without inferring anything from the bytes themselves: `MPArmSerial`
       counts every arm from the start of the session, so a serial that fails
       to advance across two polls is an arm served twice. Written by
       `W_TXSlotReply1`, read by `FinishRX` (2026-09-28). */
    u64 MPArmSerial;        // serial of the arm `MPArmSlot` holds, 0 = none
    u64 MPArmNextSerial;    // next serial to hand out
    u16 MPArmSeq;           // `MPLastSeqno` when that arm was written
    u64 MPArmUS;            // this console's clock when it was written
    /* The arm a poll actually consumed, kept until that answer has been sent:
       the reply's TX log then names the arm and the round it was armed in next
       to the frame's own 802.11 sequence number, which is the only field that
       survives to the host (see TXSendFrame, case 5). Without it the chain
       "round -> arm -> frame -> delivered answer" could only be read off the
       order of interleaved log lines from two console threads, which is not a
       thing to build an invariant on (2026-09-28). */
    u64 MPArmServedSerial;
    u16 MPArmServedSeq;

    /* Same-round reply (`MD_SAME_ROUND_REPLY`, default off; see the note above
       `reply_same_round` in Wifi.cpp). When on, a poll that arrives with no live
       arm is remembered instead of answered, and the answer is sent the moment
       the game's own arm for that round lands - so the host's window for round N
       is handed the state the guest reached after round N, not before it. A poll
       whose arm never arrives in its own round is answered by the next one, so
       no round is ever silent (`MPPendingPoll` is that fallback). */
    bool MPReplySameRound;
    bool MPPendingPoll;     // a poll whose arm has not been written yet
    u16 MPPendingTime;      // that poll's clienttime, for its reply
    u16 MPPendingMask;      // and its client mask
    u16 MPPendingSeq;       // its seq, for the trace

    /* The read recorder (see MPIo in the public section). `Read` is the public
       wrapper that records; `ReadRaw` is the register logic underneath it. */
    MPIo MPIO {};

    /* The probe state (see SetMPProbe). `MPProbeLogged` makes the first
       application of an armed probe print one line naming what it wrote and
       what the peer had said; after that the `applied` counter is the record. */
    int MPProbeKind = 0;
    int MPProbeLeft = 0;
    int MPProbeBodyLen = 0;
    u8 MPProbeBody[64] {};
    u64 MPProbeApplied = 0;
    bool MPProbeLogged = false;

    /* The probe's hook (see SetMPProbe). Called with the 802.11 frame length of
       a reply already sitting in this console's RXBuffer. */
    void MPProbeBodyHook(int framelen);

    u16 ReadRaw(u32 addr);
    void RecordMPIoRead(u32 addr, u16 val);
    void RecordMPIoWrite(u32 addr, u16 val);

    /* The register window's PC-tagged record, shared by both of the above:
       one ring entry plus the per-halfword last-accessor PC, so a driver's
       bring-up gate is an address that can be disassembled rather than a
       register name that can only be counted. */
    void RecordRegAccess(u16 addr, u16 val, bool write);

    /* The flight recorder's counters (see MPDiag in the public section). The
       "the game read the command it was handed" region lives in MPIO.zone now:
       one recorder for every delivery to this console's own software, rather
       than one for commands and none for the replies the host is handed. */
    MPDiag MPD {};

    int USUntilPowerOn;

    // MULTIPLAYER SYNC APPARATUS
    bool IsMP;
    bool IsMPClient;
    u64 NextSync;           // for clients: timestamp for next sync point
    u64 RXTimestamp;
    /* The bus timestamp of the frame the hold above is holding, kept un-clamped
       (`RXTimestamp` is raised to "now" when a frame arrives late), so the
       hand-over-once guard below identifies the frame the bus actually sent. */
    u64 MPHoldStamp;

    /* ---- handed-over-once guard (2026-09-30, v0.1.9be) ----

       A frame pulled off the shared bus is identified here by the tuple the bus
       carried it with, and the last few hand-overs are remembered so a second
       one to the same console's game can be recognised: `MPRedeliveries` counts
       exactly that. It is the counter the three-console failure was blamed on -
       one console's beacon deliveries showed 191 hand-overs against 151 distinct
       802.11 sequence numbers - and it is the counter that answers the blame,
       because it stays 0 in the healthy two-console session *and* in the failing
       three-console one.

       The sequence-number repeats are real but they are the *host's* 12-bit
       sequence counter wrapping: all 40 land exactly 80.1537s after the first,
       i.e. two genuinely different beacons that share a seqno (see the v0.1.9be
       row in history.md). So a hand-over only counts as repeated when the send
       timestamp matches as well, which a wrap cannot. Eight entries are enough:
       a second hand-over would have to survive eight other deliveries to escape
       the window. */
    struct MPDeliveredFrame
    {
        u64 Stamp;
        u32 Len;
        u16 FC;
        u16 Seq;
    };
    MPDeliveredFrame MPLastDelivered[8] {};
    int MPLastDeliveredNext;
    u64 MPBusDeliveries;   // frames handed to this console's game off the bus
    u64 MPRedeliveries;    // ... of which repeated one just handed over

    class WifiAP* WifiAP;

    void ScheduleTimer(bool first);
    void UpdatePowerOn();

    void CheckIRQ(u16 oldflags);
    void SetIRQ(u32 irq);
    void SetIRQ13();
    void SetIRQ14(int source);
    void SetIRQ15();

    void SetStatus(u32 status);

    void UpdatePowerStatus(int power);

    int PreambleLen(int rate) const;
    u32 NumClients(u16 bitmask) const;
    void IncrementTXCount(const TXSlot* slot);
    void ReportMPReplyErrors(u16 clientfail);

    void TXSendFrame(const TXSlot* slot, int num);
    void StartTX_LocN(int nslot, int loc);
    void StartTX_Cmd();
    void StartTX_Beacon();
    void FireTX();
    void SendMPDefaultReply();
    void SendMPReply(u16 clienttime, u16 clientmask);
    /* Answer one poll, taking the game's held arm if the live slot has none -
       the reply path both the poll and the same-round arm-write use. */
    void AnswerPoll(u16 seqno, u16 clienttime, u16 clientmask);
    u16 MPReplyAID();
    void SendMPAck(u16 cmdcount, u16 clientfail);
    bool ProcessTX(TXSlot* slot, int num);

    void IncrementRXAddr(u16& addr, u16 inc = 2);
    /* `bus_stamp` is the timestamp the shared bus carried the frame with, or 0
       when the frame did not come off the bus (the host's reply handoff). */
    void StartRX(u64 bus_stamp);
    void FinishRX();
    void MPClientReplyRX(int client);
    bool CheckRX(int type);

    void MSTimer();

    void ChangeChannel();

    void RFTransfer_Type2();
    void RFTransfer_Type3();
};

}
#endif

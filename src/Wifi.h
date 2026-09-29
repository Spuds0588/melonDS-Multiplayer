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

    u32 RFChannelIndex[2];
    u32 RFChannelData[14][2];
    int CurChannel;

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
    u16 MPReplyAID();
    void SendMPAck(u16 cmdcount, u16 clientfail);
    bool ProcessTX(TXSlot* slot, int num);

    void IncrementRXAddr(u16& addr, u16 inc = 2);
    void StartRX();
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

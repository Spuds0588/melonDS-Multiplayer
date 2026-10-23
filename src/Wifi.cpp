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

#include <chrono>
#include <cstdlib>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "NDS.h"
#include "SPI.h"
#include "Wifi.h"
#include "WifiAP.h"
#include "Platform.h"

namespace melonDS
{
using Platform::Log;
using Platform::LogLevel;


//#define WIFI_LOG printf
#define WIFI_LOG(...) {}

#define PRINT_MAC(pf, mac) Log(LogLevel::Debug, "%s: %02X:%02X:%02X:%02X:%02X:%02X\n", pf, (mac)[0], (mac)[1], (mac)[2], (mac)[3], (mac)[4], (mac)[5]);

/* How long a baseband or RF serial transfer takes, in the console's
   microsecond clock: 8 data bits plus the command/register bits over the
   chip's serial bus, plus the settle the firmware waits out. A driver's poll
   loop reads the flag every few hundred nanoseconds, so a window this size is
   seen as busy for a handful of polls and then clear - which is what the
   hardware does and what a driver that reads the flag to learn the transfer
   started needs to make progress. */
static const u64 kBBTransferUS = 6;
static const u64 kRFTransferUS = 6;


/* Narration of the DS multiplayer (MP) handshake's two sides, off unless
   MD_LINK_VERBOSE=1.

   Why this exists: when a client cannot join, the bus trace alone shows the
   host's CMD frames arriving at the client - which looks like the client
   simply ignoring the host. But the emulated wireless hardware filters those
   frames too (receive filter bits, a BSSID the client has not adopted yet, no
   armed reply slot), and the two cases are indistinguishable from outside the
   core while needing completely different fixes. The lines below name every
   drop, every answer and the clock the frame carried, so one failed join
   attempt is enough to say which side stopped talking. */
static bool mp_trace()
{
    static const bool on = []() {
        const char* v = std::getenv("MD_LINK_VERBOSE");
        return v && *v && (*v != '0');
    }();
    return on;
}

/* Which writes decide whether this console's transceiver is on: the mode/WEP
   control, the power-management registers, the serial-port enables and the
   baseband/RF power+mode registers. A "this console never raises TRX power"
   reading needs the write history next - which gate its own driver does or
   does not satisfy, and in what order - so every write that *changes* one of
   them is named once, with the ARM7 PC that made it. Bounded per register, so
   a long session cannot bury the run. */
enum MPPowerReg
{
    MPPW_ModeReset, MPPW_ModeWEP, MPPW_TRXPower, MPPW_PowerUS, MPPW_PowerTX,
    MPPW_PowerState, MPPW_PowerForce, MPPW_PowerDown, MPPW_BBMode, MPPW_BBPower,
    MPPW_RFCnt, MPPW_BBCnt, MPPW_MAC0, MPPW_MAC1, MPPW_MAC2,
    MPPW_BSSID0, MPPW_BSSID1, MPPW_BSSID2, MPPW_AIDLow, MPPW_AIDFull,
    MPPW_Count
};
static const char* mppw_name[MPPW_Count] =
{
    "W_ModeReset", "W_ModeWEP", "W_TRXPower", "W_PowerUS", "W_PowerTX",
    "W_PowerState", "W_PowerForce", "W_PowerDownCtrl", "W_BBMode", "W_BBPower",
    "W_RFCnt", "W_BBCnt", "W_MACAddr0", "W_MACAddr1", "W_MACAddr2",
    "W_BSSID0", "W_BSSID1", "W_BSSID2", "W_AIDLow", "W_AIDFull"
};
static_assert(MPPW_Count == 20, "PowerWriteSeen in Wifi.h is sized to match");
static const u32 kMPPowerKeep = 64;

void Wifi::TracePowerWrite(int id, u16 old, u16 val)
{
    if (!mp_trace()) return;
    if (old == val) return;
    if (PowerWriteSeen[id] >= kMPPowerKeep) return;
    PowerWriteSeen[id]++;
    Log(LogLevel::Debug,
        "[mp] power reg %-15s <- %04X (was %04X) pc7=%08X lr7=%08X\n",
        mppw_name[id], val, old, NDS.ARM7.R[15], NDS.ARM7.R[14]);
}


/* A deauth (fc=00C0) is how one console ends the other's membership of the
   local-MP group, and it is the decisive event in every 2026-09-25 MKDS
   attempt that got as far as the game's own command channel: the host deauthed
   its client 2.0-2.1s after that channel opened (2083, 1966, 2136 and 2088ms
   across four runs - too repeatable to be the player pressing a button), the
   client's wireless went off right after, and the host then polled the same
   command into an empty bus for the rest of the session. Which end sends it,
   and the reason code in the body, is what separates "the firmware or game gave
   up on this client" from "the game decided to leave the group". */
static void mp_trace_deauth(const char* what, const u8* frame, int framelen, u64 t)
{
    if (!mp_trace()) return;

    char rxmac[13] = "------------";
    char txmac[13] = "------------";
    if (framelen >= 10)
        snprintf(rxmac, sizeof(rxmac), "%02X%02X%02X%02X%02X%02X",
                 frame[4], frame[5], frame[6], frame[7], frame[8], frame[9]);
    if (framelen >= 16)
        snprintf(txmac, sizeof(txmac), "%02X%02X%02X%02X%02X%02X",
                 frame[10], frame[11], frame[12], frame[13], frame[14], frame[15]);

    const int body = framelen - 24;
    const unsigned reason = (body >= 2) ? (unsigned)(frame[24] | (frame[25] << 8)) : 0u;

    Log(LogLevel::Info,
        "[mp] deauth %s: %s -> %s at %llu us, %d byte frame, reason %04X%s\n",
        what, txmac, rxmac, (unsigned long long)t, framelen, reason,
        (body == 2) ? "" : " (body is not the usual 2-byte reason code)");
}

/* Records the last body of a command or answer frame for the flight recorder
   (Wifi::MPDiag, see Wifi.h). Sixteen bytes is the whole of the bodies these
   games send, with room to watch a longer one start to move. */
static void mp_record_body(u8* dst, u16* dstlen, const u8* src, int len)
{
    if (len < 0) len = 0;
    *dstlen = (u16)len;
    const int n = (len > 16) ? 16 : len;
    if (n > 0) memcpy(dst, src, (size_t)n);
    if (n < 16) memset(&dst[n], 0, (size_t)(16 - n));
}

/* The recorded body as hex, for the log lines that print one. What is *stored*
   stays the raw bytes (the digest carries them on to the Rust side, which
   formats its own), but printing them with `%s` put nothing on the line
   whenever the first byte was 00h - which is most of MKDS's answers (`00 80 ...`)
   - so a line reading `body[6]=` looked like a body that was never recorded and
   a "does the retry marker ride new content" check built on those lines compared
   one empty string to another (2026-09-28). `buflen` must fit 3*len. */
static const char* mp_body_hex(const u8* body, int len, char* buf, size_t buflen)
{
    if (len <= 0) { snprintf(buf, buflen, "-"); return buf; }
    size_t n = 0;
    for (int i = 0; (i < len) && ((n + 4) < buflen); i++)
        n += (size_t)snprintf(&buf[n], buflen - n, "%02X ", body[i]);
    if (n) buf[n - 1] = 0;
    return buf;
}

/* A client mask as the AIDs it addresses: `0006` -> "1,2", `0000` -> "none".
   The host's own game writes this field into every command (payload+2) and the
   answer to it comes back stamped with one of these AIDs, so a line that names
   them is the one place an AID mismatch can be read at both ends instead of
   inferred from which replies stopped arriving. */
static const char* mp_aid_list(u16 mask, char* buf, size_t buflen)
{
    size_t n = 0;
    buf[0] = 0;
    if (!mask) { snprintf(buf, buflen, "none"); return buf; }
    for (int i = 1; i < 16; i++)
    {
        if (!(mask & (1 << i))) continue;
        if (n && ((n + 1) < buflen)) buf[n++] = ',';
        if ((n + 2) < buflen) n += (size_t)snprintf(&buf[n], buflen - n, "%d", i);
    }
    return buf;
}

/* Whether a repeated CMD sequence number is handed to the game.

   Stock melonDS treats a repeated CMD seqno as a transport-level event: the
   wireless hardware answers it like any other CMD, but it never raises IRQ0,
   so the game is not told the command arrived a second time (see the cmd_dupe
   guard in FinishRX). That quietly breaks the retry it is meant to model. A
   guest can only answer a CMD by arming a reply slot *beforehand*, and it arms
   one when it is handed a command. A retry the guest never hears is therefore
   answered from a slot it already spent - or, once both of its reply slots
   are gone, with the firmware's canned default reply (SendMPDefaultReply,
   fc=0158). A host that repeats the same command until its peer's answer
   satisfies it then repeats it forever.

   Mario Kart DS's join is exactly that host. Measured on 2026-09-25: 135
   identical CMDs carrying seq=02B0, a seqno the guest preserved on purpose
   (W_TXSlotCmd bit 14, no retransmit bit), the client's first two replies the
   real thing and the remaining 129 the canned default, zero frames lost on
   the wire, then both games bailing out - the client deauthenticated and the
   host showed its join error. NSMB, whose host never repeats a seqno, plays
   fine with the same code, which is why this only ever showed up on MKDS.

   Delivering every retry is what an earlier change made the default, on the
   theory that a guest is only answerable if it hears each retry. It is not the
   fix it looked like: over the NSMB two-player script it delivers 30x as many
   commands to the guest's game for the same session, and the one thing it
   changes on the wire is how often the game is interrupted - the host's
   retries are answered by the hardware either way. Stock is the hardware's own
   behaviour (a retransmitted CMD is a transport event, not a new command), so
   that is the default. MD_CMD_RETRY_NOTIFY=1 restores the delivery behaviour
   for comparison; it remains the better choice if a guest's game ever needs a
   retry it cannot see. */
static bool cmd_retry_notify()
{
    static const bool on = []() {
        const char* v = std::getenv("MD_CMD_RETRY_NOTIFY");
        return (v && *v && (*v != '0'));
    }();
    return on;
}

/* The pre-fork transport, for the reference A/B of 2026-09-28: `MD_STOCK_MP=1`
   answers a poll exactly the way stock melonDS does - the AID test and the AID
   stamped on the reply are the *live* `W_AIDLow`, and nothing is restored from
   a held arm, so a poll that arrives after the driver cleared the slot is
   answered by the firmware's stand-in frame. Paired with `MD_STOCK_MP=1` in
   LocalMP.cpp, which restores stock's reply-queue window and its
   drop-everything re-anchor, this is the whole transport difference between
   this fork and upstream/master's `Wifi.cpp`/`LocalMP.cpp` for the two games to
   run against; the clock is deliberately *not* part of the arm (see
   Wifi::USTimer - a free-running clock in a frame-locked pair is a defect this
   fork measured, not a fork-only design choice, and the mission kept it). */
static bool stock_mp()
{
    static const bool on = []() {
        const char* v = std::getenv("MD_STOCK_MP");
        return v && *v && (*v != '0');
    }();
    return on;
}

/* Whether a poll that arrives with the reply slot as the game left it (cleared)
   is answered with the frame the hardware still holds in its own reply slot -
   the game's own bytes - or with the firmware's invented `00 00 00 00`
   stand-in.

   ON by default (2026-09-28): the stand-in is the one place a value a game
   reads is neither the peer's game's nor the hardware's, and it was 22% of the
   guest's answers through a 630s freeze (6267 of 28017, v0.1.9h). The reply
   registers the game arms (`W_TXSlotReply1`) are latched by the hardware into
   the slot it transmits (`W_TXSlotReply2`); the game clearing its own slot does
   not erase the hardware's. `MD_ARM_PERSIST=0` restores the stand-in byte for
   byte, which is the arm the two earlier runs measured - see history.md
   v0.1.9i for why the re-measurement under the FCS fix is what the default
   rests on. */
static bool arm_persist()
{
    static const bool on = []() {
        const char* v = std::getenv("MD_ARM_PERSIST");
        return !(v && *v == '0');
    }();
    return on;
}

/* Reply rendezvous (MD_REPLY_DEFER_FRAMES, default 3 frames; 0 = off).

   How long a host's command slot may hold its ack open for clients that have
   not answered yet, in emulated frames. Measured at three consoles (2026-10-23):
   the rounds that were reported as `clientfail` had their owed answer collected
   by the next window in 33 of 41 cases, i.e. the client's game produced it a
   frame or two after the round that asked for it. The rendezvous turns the
   round's close into "every client this command addressed has answered", with
   this bound as the only way out (counted as `reply_defer_exhausted`, a
   violation on a healthy link). */
static int reply_defer_frames()
{
    static const int frames = []() {
        const char* v = std::getenv("MD_REPLY_DEFER_FRAMES");
        if (v && *v) return (int)strtol(v, nullptr, 10);
        return 3;
    }();
    return frames;
}

/* How often the rendezvous re-asks the bus, as a multiple of the 4-tick
   (32us emulated) period it starts with: `MD_REPLY_POLL_TICKS=1` is the phase
   as first written, and larger values make it poll less often.

   This is a measurement knob, not a second policy. The 2026-10-23 four-console
   run measured the holds at 0.4 emulated frames each (~2% of the session) yet
   the room ran at 14fps against 39-42 with the rendezvous off, so the poll was
   a suspect. **It is cleared:** both ends of that difference polled 149-266
   times per hold (max 1,568) and differed only in the reply *window*'s
   wall-clock ceiling, and the ceiling - not this cadence - was the cost (see
   `SetReplyHold`). The knob stays because it is the one setting that would
   show the poll becoming a cost if it grew heavier. */
static int reply_poll_ticks()
{
    static const int ticks = []() {
        const char* v = std::getenv("MD_REPLY_POLL_TICKS");
        if (v && *v)
        {
            const int t = (int)strtol(v, nullptr, 10);
            if (t > 0) return t;
        }
        return 1;
    }();
    return ticks;
}

/* Wall clock in microseconds, for the rendezvous' own accounting. A steady
   clock with a process-wide epoch: only differences are ever used, and the
   value is never written to a savestate. */
static u64 mp_wall_us()
{
    static const auto epoch = std::chrono::steady_clock::now();
    return (u64)std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - epoch).count();
}

/* Same-round reply (MD_SAME_ROUND_REPLY, default OFF).

   The held arm answers each poll with the arm the game wrote while handling the
   *previous* one, so the host's reply window for round N is handed the state the
   guest held before round N's command. This flag tries the hardware's own
   ordering instead: the poll is answered with the arm the game writes during that
   same round. It is a measurement knob, not a new default - it changes when the
   game's own arm is consumed and whether the host's window has to wait for it,
   and only a run decides whether that is better. */
static bool reply_same_round()
{
    static const bool on = []() {
        const char* v = std::getenv("MD_SAME_ROUND_REPLY");
        return v && *v && (*v != '0');
    }();
    return on;
}

/* The A/B half of the hand-over-once guard (see MPLastDelivered in Wifi.h).
   `MD_MP_REDELIVER=1` hands the same held frame to the console's game a second
   time, so the guard's counter and its REDELIVERED line can be *seen* to fire
   rather than only never firing. It is the reference arm for "is this counter
   connected?", the same shape as MD_STOCK_MP/MD_MP_FCS, and the only thing it
   changes is the duplicate the guard exists to count. Run a session with it and
   without it and the two readings are the proof. */
static bool mp_redeliver_probe()
{
    static const bool on = []() {
        const char* v = std::getenv("MD_MP_REDELIVER");
        return v && *v && (*v != '0');
    }();
    return on;
}

/* The receive-side length crop: the *only* place in the delivery path where
   bytes the sender put in the frame are dropped on purpose. Every game here
   programs `W_RX_LEN_CROP` to exclude the 4-byte FCS the wireless hardware
   appends, and the sender's own length entry counts that FCS (GBATEK), so the
   peer's game reads `frame - 4` and the four bytes it does not read are the
   sender's FCS slot, not payload - which melonDS's own frame sizes corroborate
   (`SendMPAck` 32 bytes for a 4-byte body, `SendMPDefaultReply` 28 for an empty
   one, both with a zeroed tail).

   `MD_RX_NOCROP=1` is the reference arm: it hands the peer's game the whole
   on-wire length instead (combine with `MD_MP_FCS=0` for the pre-v0.1.9f wire
   with no crop). Measured on the NSMB route, same save: with the crop the route
   reaches gameplay; without it the peer's game is handed four bytes more per
   frame and the two games stop following each other. The crop is load-bearing
   (history.md v0.1.9s). */
static bool rx_nocrop()
{
    static const bool on = []() {
        const char* v = std::getenv("MD_RX_NOCROP");
        return v && *v && (*v != '0');
    }();
    return on;
}

/* The early reply (sending a guest's armed answer inside the round its CMD
   arrived in, from the W_AIDLow / W_TXSlotReply1 writes) is gone on purpose.
   Upstream tried it in 8772258f and reverted it in 193c7ed9; this fork
   restored it and measured what it costs: it answers on the guest's behalf, so
   the guest's driver never gets the event it is waiting for, and the guest's
   game is left starved (MKDS's guest arms once for the whole lobby and
   deauthenticates 4-5s later). It also hides the real question - whether the
   guest's game has an answer armed at its round boundary - behind a frame the
   game itself never produced. Stock melonDS's round-end reply is the only
   path, so every answer on the wire is one a console's own software sent. */

#define IOPORT(x) IO[(x)>>1]
#define IOPORT8(x) ((u8*)IO)[x]

// destination MACs for MP frames
const u8 Wifi::MPCmdMAC[6]   = {0x03, 0x09, 0xBF, 0x00, 0x00, 0x00};
const u8 Wifi::MPReplyMAC[6] = {0x03, 0x09, 0xBF, 0x00, 0x00, 0x10};
const u8 Wifi::MPAckMAC[6]   = {0x03, 0x09, 0xBF, 0x00, 0x00, 0x03};

// multiplayer host TX sequence:
// 1. preamble
// 2. IRQ7
// 3. send data
// 4. optional IRQ1
// 5. wait for client replies (duration: 16 + ((10 * CMD_REPLYTIME) * numclients) + ack preamble (96))
// 6. IRQ7
// 7. send ack (32 bytes)
// 8. optional IRQ1, along with IRQ12 if the transfer was successful or if
//    there's no time left for a retry
//
// if the transfer has to be retried (for example, didn't get replies from all clients)
// and there is time, it repeats the sequence
//
// if there isn't enough time left on CMD_COUNT, IRQ12 is triggered alone when
// CMD_COUNT is 10, and the packet txheader[0] is set to 5
//
// RFSTATUS values:
// 0 = initial
// 1 = waiting for incoming packets
// 2 = switching from RX to TX
// 3 = TX
// 4 = switching from TX to RX
// 5 = MP host data sent, waiting for replies (RFPINS=0x0084)
// 6 = RX
// 7 = switching from RX reply to TX ack
// 8 = MP client sending reply, MP host sending ack (RFPINS=0x0046)
// 9 = idle


// wifi TODO:
// * RXSTAT
// * TX errors (if applicable)


bool MACEqual(const u8* a, const u8* b)
{
    return (*(u32*)&a[0] == *(u32*)&b[0]) && (*(u16*)&a[4] == *(u16*)&b[4]);
}

bool MACIsBroadcast(const u8* a)
{
    return (*(u32*)&a[0] == 0xFFFFFFFF) && (*(u16*)&a[4] == 0xFFFF);
}


Wifi::Wifi(melonDS::NDS& nds) : NDS(nds)
{
    NDS.RegisterEventFuncs(Event_Wifi, this, {MakeEventThunk(Wifi, USTimer)});

    WifiAP = new class WifiAP(this, NDS.UserData);
}

Wifi::~Wifi()
{
    delete WifiAP; WifiAP = nullptr;

    NDS.UnregisterEventFuncs(Event_Wifi);
}

void Wifi::Reset()
{
    memset(RAM, 0, 0x2000);
    memset(IO, 0, 0x1000);

    Enabled = false;
    PowerOn = false;

    Random = 1;

    memset(BBRegs, 0, 0x100);
    memset(BBRegsRO, 0, 0x100);

    #define BBREG_FIXED(id, val)  BBRegs[id] = val; BBRegsRO[id] = 1;
    BBREG_FIXED(0x00, 0x6D);
    BBREG_FIXED(0x0D, 0x00);
    BBREG_FIXED(0x0E, 0x00);
    BBREG_FIXED(0x0F, 0x00);
    BBREG_FIXED(0x10, 0x00);
    BBREG_FIXED(0x11, 0x00);
    BBREG_FIXED(0x12, 0x00);
    BBREG_FIXED(0x16, 0x00);
    BBREG_FIXED(0x17, 0x00);
    BBREG_FIXED(0x18, 0x00);
    BBREG_FIXED(0x19, 0x00);
    BBREG_FIXED(0x1A, 0x00);
    BBREG_FIXED(0x27, 0x00);
    BBREG_FIXED(0x4D, 0x00); // 00 or BF
    BBREG_FIXED(0x5D, 0x01);
    BBREG_FIXED(0x5E, 0x00);
    BBREG_FIXED(0x5F, 0x00);
    BBREG_FIXED(0x60, 0x00);
    BBREG_FIXED(0x61, 0x00);
    BBREG_FIXED(0x64, 0xFF); // FF or 3F
    BBREG_FIXED(0x66, 0x00);
    for (int i = 0x69; i < 0x100; i++)
    {
        BBREG_FIXED(i, 0x00);
    }
    #undef BBREG_FIXED

    const Firmware& fw = NDS.SPI.GetFirmware();
    const auto& fwheader = fw.GetHeader();

    RFVersion = fwheader.RFChipType;
    memset(RFRegs, 0, 4*0x40);

    BBBusyUntil = 0;
    RFBusyUntil = 0;
    BBReadResult = 0;

    // load channel index/data from the firmware
    // the current channel will be determined by RF settings
    // so we compare the two 'most important' RF registers to these values to figure out which channel is selected

    if (RFVersion == 3)
    {
        RFChannelIndex[0] = fwheader.Type3Config.RFIndex1;
        RFChannelIndex[1] = fwheader.Type3Config.RFIndex2;

        for (int i = 0; i < 14; i++)
        {
            RFChannelData[i][0] = fwheader.Type3Config.RFData1[i];
            RFChannelData[i][1] = fwheader.Type3Config.RFData2[i];
        }
    }
    else
    {
        RFChannelIndex[0] = fwheader.Type2Config.InitialRF56Values[2] >> 2;
        RFChannelIndex[1] = fwheader.Type2Config.InitialRF56Values[5] >> 2;

        for (int i = 0; i < 14; i++)
        {
            RFChannelData[i][0] = fwheader.Type2Config.InitialRF56Values[i*6 + 0] |
                                  (fwheader.Type2Config.InitialRF56Values[i*6 + 1] << 8) |
                                  ((fwheader.Type2Config.InitialRF56Values[i*6 + 2] & 0x03) << 16);
            RFChannelData[i][1] = fwheader.Type2Config.InitialRF56Values[i*6 + 3] |
                                  (fwheader.Type2Config.InitialRF56Values[i*6 + 4] << 8) |
                                  ((fwheader.Type2Config.InitialRF56Values[i*6 + 5] & 0x03) << 16);
        }
    }

    CurChannel = 0;
    HeldChannel = 0;
    memset(HeldBSSID, 0, 6);
    HeldAID = 0;
    DpSessionFrame = 0;
    DpReseeded = false;
    DpReseedLogged = false;

    auto consoletype = fwheader.ConsoleType;
    if (consoletype == Firmware::FirmwareConsoleType::DS || consoletype == Firmware::FirmwareConsoleType::iQueDS)
        IOPORT(0x000) = 0x1440;
    else if (consoletype == Firmware::FirmwareConsoleType::DSLite || consoletype == Firmware::FirmwareConsoleType::iQueDSLite)
        IOPORT(0x000) = 0xC340;
    else if (NDS.ConsoleType == 1 && consoletype == Firmware::FirmwareConsoleType::DSi)
        IOPORT(0x000) = 0xC340; // DSi has the modern DS-wifi variant
    else
    {
        Log(LogLevel::Warn, "wifi: unknown console type %02X\n", consoletype);
        IOPORT(0x000) = 0x1440;
    }

    memset(&IOPORT(0x018), 0xFF, 6);
    memset(&IOPORT(0x020), 0xFF, 6);

    // TODO: find out what the initial values are
    IOPORT(W_PowerUS) = 0x0001;

    //IOPORT(W_BeaconInterval) = 100;

    USTimestamp = 0;
    ClockTicks = 0;
    ClockLastProbe = 0;

    USCounter = 0;
    USCompare = 0;
    BlockBeaconIRQ14 = false;

    memset(TXSlots, 0, sizeof(TXSlots));
    memset(TXBuffer, 0, sizeof(TXBuffer));
    ComStatus = 0;
    TXCurSlot = -1;
    RXCounter = 0;

    memset(RXBuffer, 0, sizeof(RXBuffer));
    RXBufferPtr = 0;
    RXTime = 0;
    RXHalfwordTimeMask = 0xFFFFFFFF;

    MPReplyTimer = 0;
    MPClientMask = 0;
    MPClientFail = 0;
    MPD.last_cmd_mask_game = 0xFFFF;
    MPD.last_cmd_mask_poll = 0xFFFF;
    MPD.last_collect_mask = 0;
    MPD.last_collect_us = 0;
    MPD.close_tx = 0;
    MPD.close_rx = 0;
    MPD.close_rx_us = 0;
    MPLoopAlive = false;
    memset(MPClientReplies, 0, sizeof(MPClientReplies));

    MPLastSeqno = 0xFFFF;

    MPMYAID = 0;
    MPArmHeld = false;
    MPArmSlot = 0;
    MPArmSerial = 0;
    MPArmNextSerial = 0;
    MPArmSeq = 0;
    MPArmUS = 0;
    MPArmServedSerial = 0;
    MPArmServedSeq = 0;

    MPReplySameRound = false;
    MPPendingPoll = false;
    MPPendingTime = 0;
    MPPendingMask = 0;
    MPPendingSeq = 0;

    CmdCounter = 0;

    USUntilPowerOn = 0;

    IsMP = false;
    IsMPClient = false;
    NextSync = 0;
    RXTimestamp = 0;
    MPHoldStamp = 0;

    MPRXSite = 0;

    memset(MPLastDelivered, 0, sizeof(MPLastDelivered));
    MPLastDeliveredNext = 0;
    MPBusDeliveries = 0;
    MPRedeliveries = 0;

    /* The flight recorder goes back to zero with everything else, so a digest
       read after a session starts cannot be from the session before it - and
       the read recorder with it, for the same reason (a report is read against
       a session, not against the app's whole lifetime). */
    MPD = MPDiag{};
    MPIO = MPIo{};

    /* The power-write trace's per-register budgets go back to zero with the
       recorder, so a console that boots a second time (the Download Play child
       does: the hand-over is a whole-console Reset(), then its own firmware
       bring-up) gets a fresh 64 writes to name instead of a trace that went
       silent during the run before it. */
    memset(PowerWriteSeen, 0, sizeof(PowerWriteSeen));

    WifiAP->Reset();
}

void Wifi::DoSavestate(Savestate* file)
{
    file->Section("WIFI");

    // berp.
    // not sure we're saving enough shit at all there.
    // also: savestate and wifi can't fucking work together!!
    // or it can but you would be disconnected

    file->VarArray(RAM, 0x2000);
    file->VarArray(IO, 0x1000);

    file->Bool32(&Enabled);
    file->Bool32(&PowerOn);

    file->Var16(&Random);

    file->Var32((u32*)&TimerError);

    file->VarArray(BBRegs, 0x100);
    file->VarArray(BBRegsRO, 0x100);

    file->Var8(&RFVersion);
    file->VarArray(RFRegs, 4*0x40);

    /* BBBusyUntil/RFBusyUntil/BBReadResult are transient - a serial transfer
       lasts microseconds - so they are kept out of the stream rather than
       shifting this section's layout for every existing state. A state is never
       meaningfully saved mid-transfer, so loading starts them clear; keeping a
       deadline from the run that saved would read as busy until the restored
       clock caught up to it. */
    if (!file->Saving)
    {
        BBBusyUntil = 0;
        RFBusyUntil = 0;
        BBReadResult = 0;
    }

    file->Var32((u32*)&CurChannel);
    // HeldChannel is deliberately *not* savestated: it is only a fallback for
    // an untuned radio and is re-derived from the same boot on load, and adding
    // it would shift the WIFI section's layout for every existing state (the
    // format has a version, and this is not worth bumping it).

    file->Var64(&USCounter);
    file->Var64(&USCompare);
    file->Bool32(&BlockBeaconIRQ14);

    file->Var32(&CmdCounter);

    file->Var64(&USTimestamp);

    for (int i = 0; i < 6; i++)
    {
        TXSlot* slot = &TXSlots[i];

        file->Bool32(&slot->Valid);
        file->Var16(&slot->Addr);
        file->Var16(&slot->Length);
        file->Var8(&slot->Rate);
        file->Var8(&slot->CurPhase);
        file->Var32((u32*)&slot->CurPhaseTime);
        file->Var32(&slot->HalfwordTimeMask);
    }

    file->VarArray(TXBuffer, sizeof(TXBuffer));

    file->VarArray(RXBuffer, sizeof(RXBuffer));
    file->Var32(&RXBufferPtr);
    file->Var32((u32*)&RXTime);
    file->Var32(&RXHalfwordTimeMask);

    file->Var32(&ComStatus);
    file->Var32(&TXCurSlot);
    file->Var32(&RXCounter);

    file->Var32((u32*)&MPReplyTimer);
    file->Var16(&MPClientMask);
    file->Var16(&MPClientFail);

    file->VarArray(MPClientReplies, sizeof(MPClientReplies));

    file->Var16(&MPLastSeqno);

    file->Var32((u32*)&USUntilPowerOn);

    file->Bool32(&IsMP);
    file->Bool32(&IsMPClient);
    file->Var64(&NextSync);
    file->Var64(&RXTimestamp);
}


void Wifi::ScheduleTimer(bool first)
{
    if (first) TimerError = 0;

    s32 cycles = 33513982 * kTimerInterval;
    cycles -= TimerError;
    s32 delay = (cycles + 999999) / 1000000;
    TimerError = (delay * 1000000) - cycles;

    NDS.ScheduleEvent(Event_Wifi, !first, delay, 0, 0);
}

void Wifi::UpdatePowerOn()
{
    bool on = Enabled;

    if (NDS.ConsoleType == 1)
    {
        // TODO for DSi:
        // * W_POWER_US doesn't work (atleast on DWM-W024)
        // * other registers like GPIO_WIFI may also control wifi power/clock
        // * turning wifi off via POWCNT2 while sending breaks further attempts at sending frames
    }
    else
    {
        on = on && ((IOPORT(W_PowerUS) & 0x1) == 0);
    }

    if (on == PowerOn)
        return;

    PowerOn = on;
    if (on)
    {
        Log(LogLevel::Debug, "WIFI: ON\n");

        ScheduleTimer(true);

        Platform::MP_Begin(NDS.UserData);
    }
    else
    {
        Log(LogLevel::Debug, "WIFI: OFF\n");

        NDS.CancelEvent(Event_Wifi);

        Platform::MP_End(NDS.UserData);
    }
}

void Wifi::SetPowerCnt(u32 val)
{
    Enabled = val & (1<<1);
    UpdatePowerOn();
}


void Wifi::CheckIRQ(u16 oldflags)
{
    u16 newflags = IOPORT(W_IF) & IOPORT(W_IE);

    if ((oldflags == 0) && (newflags != 0))
    {
        /* The wifi block pulling its CPU's interrupt line: the moment a game
           that is servicing the wifi IRQ would enter its handler. Counted
           separately from the per-source raises so "the game has been asked
           N times this window" is not confused with "the block raised N
           edges". */
        MPD.irq_cpu_line++;
        NDS.SetIRQ(1, IRQ_Wifi);
    }
}

void Wifi::SetIRQ(u32 irq)
{
    u16 oldflags = IOPORT(W_IF) & IOPORT(W_IE);

    if (irq < 16) MPD.irq_raised[irq]++;
    IOPORT(W_IF) |= (1<<irq);
    CheckIRQ(oldflags);
}

void Wifi::SetIRQ13()
{
    SetIRQ(13);

    // auto power-down should only happen in automatic power saving mode (0).
    // it must not happen when power saving is disabled (mode 2): stations
    // running in that mode (eg. dswifi in infrastructure mode) don't service
    // IRQ13/IRQ15, so a power-down triggered by the one-shot W_BeaconCount2
    // countdown (armed at 0xFFFF = ~67s) would turn the transceiver off
    // with nothing ever waking it up again
    if ((IOPORT(W_ModeWEP) & 0x7) == 0)
    {
        if (!(IOPORT(W_PowerTX) & (1<<1)))
        {
            UpdatePowerStatus(-1);
        }
    }
}

void Wifi::SetIRQ14(int source) // 0=USCOMPARE 1=BEACONCOUNT 2=forced
{
    // CHECKME: is this also done for USCOMPARE IRQ?
    if (source != 2)
        IOPORT(W_BeaconCount1) = IOPORT(W_BeaconInterval);

    if (BlockBeaconIRQ14 && source == 1)
    {
        /* The countdown cannot raise IRQ14 while the game has asked to be
           woken by USCOUNT reaching USCOMPARE instead (bit0 of USCOMPARE0, see
           the note in the Write handler). That is the shape of a console that
           belongs to a group: it takes the beacon it just received, adds its
           offset within the group's beacon schedule, writes that to USCOMPARE
           and lets the beacon IRQ fire then - which is also the only way this
           console's *own* beacon ever starts (see the StartTX_Beacon call
           below). A console that never does this is a console that never
           announces itself, and the log says so here rather than leaving it to
           be inferred from a beacon that never goes out. */
        if (mp_trace())
            Log(LogLevel::Debug,
                "[mp] beacon IRQ14 from the countdown is blocked until USCOUNT reaches %llX "
                "(USCOUNT is %llX): something is scheduling this console's own beacon\n",
                (unsigned long long)USCompare, (unsigned long long)USCounter);
        return;
    }
    if (!(IOPORT(W_USCompareCnt) & 0x0001))
    {
        if (mp_trace())
            Log(LogLevel::Debug,
                "[mp] beacon IRQ14 dropped: USCOMPARE is disabled (USCOMPARECNT=%04X), so no "
                "beacon can start from here - the game does not want one (source %d)\n",
                IOPORT(W_USCompareCnt), source);
        return;
    }

    SetIRQ(14);

    if (source == 2)
        Log(LogLevel::Debug, "wifi: weird forced IRQ14\n");

    IOPORT(W_BeaconCount2) = 0xFFFF;
    IOPORT(W_TXReqRead) &= 0xFFF2;

    if (IOPORT(W_TXSlotBeacon) & 0x8000)
    {
        StartTX_Beacon();
        if (mp_trace())
            Log(LogLevel::Debug, "[mp] beacon started from IRQ14 (source %d): this console is "
                "announcing itself in its slot\n", source);
    }
    else if (mp_trace())
        Log(LogLevel::Debug,
            "[mp] beacon IRQ14 fired (source %d) with no beacon slot armed (TXSLOTBEACON=%04X): "
            "this console has nothing to announce itself with\n", source, IOPORT(W_TXSlotBeacon));

    if (IOPORT(W_ListenCount) == 0)
        IOPORT(W_ListenCount) = IOPORT(W_ListenInterval);

    IOPORT(W_ListenCount)--;
}

void Wifi::SetIRQ15()
{
    SetIRQ(15);

    // unlike auto sleep, auto wakeup works under all power management modes
    if (IOPORT(W_PowerTX) & (1<<0))
    {
        UpdatePowerStatus(1);
    }
}

void Wifi::SampleMPIrqSnapshot()
{
    /* Read-only: every register here is one whose CPU read has no side effect.
       The ones that do have one (the W_CMDStat halfwords clear themselves, and
       W_RXBufDataRead advances the read cursor) are deliberately not touched.
       Called when the digest is exported, so the values are "at digest time"
       and the counters next to them are "since reset". */
    MPD.if_now = IOPORT(W_IF);
    MPD.ie_now = IOPORT(W_IE);
    MPD.uscntcnt_now = IOPORT(W_USCountCnt);
    MPD.uscmpcnt_now = IOPORT(W_USCompareCnt);
    MPD.bcncount1_now = IOPORT(W_BeaconCount1);
    MPD.bcncount2_now = IOPORT(W_BeaconCount2);
    MPD.txslot_beacon_now = IOPORT(W_TXSlotBeacon);
    MPD.txslot_reply1_now = IOPORT(W_TXSlotReply1);
    MPD.txslot_reply2_now = IOPORT(W_TXSlotReply2);
    MPD.txbusy_now = IOPORT(W_TXBusy);
    MPD.txreqread_now = IOPORT(W_TXReqRead);
    MPD.powerstate_now = IOPORT(W_PowerState);
    MPD.powertx_now = IOPORT(W_PowerTX);
    MPD.comstatus_now = (u16)ComStatus;
    MPD.uscounter_now = USCounter;
    MPD.uscompare_now = USCompare;
    MPD.block_beacon_irq14_now = BlockBeaconIRQ14 ? 1 : 0;
}


void Wifi::SetStatus(u32 status)
{
    // TODO, eventually: states 2/4/7
    u16 rfpins[10] = {0x04, 0x84, 0, 0x46, 0, 0x84, 0x87, 0, 0x46, 0x04};
    IOPORT(W_RFStatus) = status;
    IOPORT(W_RFPins) = rfpins[status];
}


void Wifi::UpdatePowerStatus(int power) // 1=on 0=no change -1=off
{
    // TRANSCEIVER POWER MANAGEMENT
    //
    // * W_PowerForce overrides all else
    // * W_ModeReset bit0 forcibly turns off the transceiver when cleared
    // * power is normally turned on or off either by IRQ15/IRQ13 or by W_PowerState
    //   depending on the power management mode selected in W_ModeWEP
    // * W_PowerDownCtrl controls how deep a regular power-down is

    int curflags = 0;
    if (IOPORT(W_TRXPower) == 1) curflags |= 1;
    if (!(IOPORT(W_PowerState) & (1<<9))) curflags |= 2;
    int reqflags = curflags;

    if (IOPORT(W_PowerForce) & (1<<15))
    {
        reqflags = (IOPORT(W_PowerForce) & (1<<0)) ? 0 : 3;
    }
    else if (!(IOPORT(W_ModeReset) & (1<<0)))
    {
        reqflags = 0;
    }
    else
    {
        if (power == 0)
        {
            if ((IOPORT(W_PowerState) & 0x0202) == 0x0202)
                power = 1;
            else if ((IOPORT(W_PowerState) & 0x0201) == 0x0001)
                power = -1;
        }

        // W_PowerDownCtrl:
        // * bit 0 inhibits a regular power-down
        // * bit 1 forces a wakeup, atleast partial

        if ((power == -1) && (IOPORT(W_PowerDownCtrl) & (1<<0)))
            power = 0;

        /*if (power == 1)
            reqflags = 3;
        else if (power == -1)
            reqflags = IOPORT(W_PowerDownCtrl);
        else if (IOPORT(W_PowerDownCtrl) & (1<<1))
            reqflags = (curflags == 3) ? 3 : IOPORT(W_PowerDownCtrl);*/

        // TODO: support partial power statuses (W_PowerDownCtrl=1 or 2)

        if (power == 1)
            reqflags = 3;
        else if (power == -1)
            reqflags = IOPORT(W_PowerDownCtrl) ? 3 : 0;
        else if (IOPORT(W_PowerDownCtrl) & (1<<1))
            reqflags = 3;
    }

    if (reqflags == curflags)
        return;

    if (reqflags & 1)
    {
        if (!(curflags & 1))
        {
            IOPORT(W_TRXPower) = 1;
            SetStatus(1);
        }
    }
    else
    {
        // signal the transceiver is going to turn off (checkme)
        IOPORT(W_TRXPower) = 2;

        if (!ComStatus)
        {
            IOPORT(W_TRXPower) = 0;
            SetStatus(9);
        }
    }

    if (reqflags & 2)
    {
        // power on

        IOPORT(W_PowerState) |= (1<<8);
        if ((!(curflags & 2)) && (USUntilPowerOn == 0))
        {
            Log(LogLevel::Debug, "wifi: TRX power ON\n");

            USUntilPowerOn = -2048;
            SetIRQ(11);
        }
    }
    else
    {
        // power off

        if (curflags & 2)
            Log(LogLevel::Debug, "wifi: TRX power OFF\n");

        IOPORT(W_PowerState) &= ~(1<<0);
        IOPORT(W_PowerState) &= ~(1<<8);
        IOPORT(W_PowerState) |= (1<<9);
        USUntilPowerOn = 0;
    }
}


int Wifi::PreambleLen(int rate) const
{
    if (rate == 1) return 192;
    if (IOPORT(W_Preamble) & 0x0004) return 96;
    return 192;
}

u32 Wifi::NumClients(u16 bitmask) const
{
    u32 ret = 0;
    for (int i = 1; i < 16; i++)
    {
        if (bitmask & (1<<i)) ret++;
    }
    return ret;
}

void Wifi::IncrementTXCount(const TXSlot* slot)
{
    u8 cnt = RAM[slot->Addr + 0x4];
    if (cnt < 0xFF) cnt++;
    *(u16*)&RAM[slot->Addr + 0x4] = cnt;
}

void Wifi::ReportMPReplyErrors(u16 clientfail)
{
    // TODO: do these trigger any IRQ?

    for (int i = 1; i < 16; i++)
    {
        if (!(clientfail & (1<<i)))
            continue;

        IOPORT8(W_CMDStat0 + i)++;
    }
}

/* The TX-slot ledger: every slot write (arm, re-arm, clear) and every frame
   that leaves, so "never relayed" and "relayed but never handed to the peer's
   game" can be told apart. History, measurements and the audit that consumed
   it: history.md v0.1.9p. */
static const char* mp_txslot_name(int num)
{
    switch (num)
    {
    case 0: return "LOC1";
    case 1: return "CMD";
    case 2: return "LOC2";
    case 3: return "LOC3";
    case 4: return "BEACON";
    case 5: return "REPLY";
    default: return "?";
    }
}

/* What a write to a transmit slot means. Bit 15 is the arm bit (the slot is
   loaded and waiting to be transmitted); without it the slot is disarmed. A
   re-arm at a *different* buffer address is the game replacing a frame it had
   queued, which is how a retry looks when the driver rebuilds the frame in
   between. */
static const char* mp_txslot_what(u16 val, u16 prev)
{
    if (val & 0x8000)
    {
        if (!(prev & 0x8000)) return "armed";
        return ((prev ^ val) & 0x0FFF) ? "re-armed (a new buffer)"
                                       : "re-armed (the same buffer)";
    }
    return (prev & 0x8000) ? "cleared (was armed)" : "cleared (was already clear)";
}

/* What identifies a frame on the air, and who decides it (history.md v0.1.9v
   has the sources and the measurements).

   GBATEK: the hardware owns the 802.11 sequence number (`W_TX_SEQNO`
   "increments on IRQ07" and, when `W_TXBUF_LOCn.Bit13` enables it, replaces the
   IEEE header's Sequence Control), and it edits only the Sequence Control,
   `FC.Bit12` and the protocol version - so every *transfer* is a new frame and
   the Retry bit belongs to the transfer. `TXHDR[04h]` - the byte stock melonDS
   reads to decide "this buffer was already transmitted" - is the *game's*: 00h
   use `W_TXSeqNo`, 01h/02h force not to, and "Entries [00h] and [05h] are
   modified by hardware, all other entries are kept unchanged". Stock reads any
   nonzero as a retry, and the emulator itself writes that count at the end of
   every reply transfer, so the flag landed on the second and later use of a
   reply buffer - making a fresh answer indistinguishable from a retransmission.

   So the reply slot's identity is decided here, and only here: a reply is a new
   transfer, so it takes a fresh `W_TXSeqNo` value and is never given a
   synthesized retry marker, whatever the buffer's own bytes say. The buffer's
   bytes are still written byte-for-byte as stock writes them, because they are
   load-bearing: two builds that dropped the count parked the NSMB route that
   plays with it (history.md v0.1.9v). Every other slot keeps stock's rule,
   where the byte models the games' own retry loops (MKDS's repeated commands,
   `W_TXSlotCmd` bit 14). */
void Wifi::TXSendFrame(const TXSlot* slot, int num)
{
    u32 noseqno = 0;

    if (num == 5)
    {
        // the reply slot: always a new transfer, never a retransmission - see
        // the note above
        noseqno = 0;
    }
    else if (RAM[slot->Addr + 0x4])
    {
        noseqno = 2;
    }
    else
    {
        if (num == 1) noseqno = (IOPORT(W_TXSlotCmd) & 0x4000) ? 1:0;
    }

    if (!noseqno)
    {
        if (!(IOPORT(W_TXHeaderCnt) & (1<<2)))
            *(u16*)&RAM[slot->Addr + 0xC + 22] = IOPORT(W_TXSeqNo) << 4;
        IOPORT(W_TXSeqNo) = (IOPORT(W_TXSeqNo) + 1) & 0x0FFF;
    }

    u16 framectl = *(u16*)&RAM[slot->Addr + 0xC];
    if (framectl & (1<<14))
    {
        // WEP frame
        // TODO: what happens when sending a WEP frame while WEP processing is off?
        // TODO: some form of actual WEP processing?
        // for now we just set the WEP FCS to a nonzero value, because some games require it

        if (IOPORT(W_WEPCnt) & (1<<15))
        {
            u32 wep_fcs = (slot->Addr + 0xC + slot->Length - 7) & ~0x1;
            *(u32*)&RAM[wep_fcs] = 0x22334466;
        }
    }

    int len = slot->Length;
    if ((slot->Addr + len) > 0x1FF4)
        len = 0x1FF4 - slot->Addr;

    memcpy(TXBuffer, &RAM[slot->Addr], 12+len);

    /* The retry bit is the hardware's own marker for a retransmission *inside*
       one transfer (the IEEE-header edits hardware does are the Sequence
       Control value, FC.Bit12 and the protocol version - GBATEK, DS Wifi
       Hardware Headers). A reply this transport relays is a whole new transfer
       - the poll it answers is new even when the game has not re-armed the
       buffer - and it never retransmits a frame inside one, so nothing is
       synthesized for the reply slot. Stock's coupling (`somebody left that
       byte nonzero, so this is a retry`) stays for the other slots, where it
       models the games' own retry loops (MKDS's repeated commands). */
    if ((noseqno == 2) && (num != 5))
        *(u16*)&TXBuffer[0xC] |= (1<<11);

    if (CurChannel == 0) return;
    TXBuffer[9] = CurChannel;

    /* One line per frame that leaves, with the slot registers at that instant,
       so a frame can be tied to the arm that produced it. The control word is
       read back out of TXBuffer rather than taken from `framectl`: the no-seqno
       bit is applied to the buffer above, after `framectl` was read from RAM,
       so a ledger built on `framectl` reports fc=0118 for frames the wire
       carries as fc=0918 (history.md v0.1.9p). */
    if (mp_trace())
        Log(LogLevel::Debug,
            "[mp] tx frame: slot %d (%s) fc=%04X%s len=%d seq=%04X bssid=%02X%02X%02X "
            "· slots beacon %04X cmd %04X reply1 %04X reply2 %04X\n",
            num, mp_txslot_name(num), *(u16*)&TXBuffer[12],
            (*(u16*)&TXBuffer[12] == framectl) ? "" : " (of the game's own fc, with no-seqno)",
            len,
            (len >= 26) ? *(u16*)&TXBuffer[12 + 22] : 0,
            TXBuffer[24], TXBuffer[25], TXBuffer[26],
            IOPORT(W_TXSlotBeacon), IOPORT(W_TXSlotCmd),
            IOPORT(W_TXSlotReply1), IOPORT(W_TXSlotReply2));

    if ((framectl & 0x00FF) == 0x00C0)
    {
        MPD.deauth_sent++;
        MPD.last_deauth_reason = (len >= 26) ? *(u16*)&TXBuffer[12 + 24] : 0;
        mp_trace_deauth("sent", &TXBuffer[12], len, USTimestamp);
    }

    /* A client's association request is the other half of the close slice: the
       room's close is followed by both guests re-associating, and whether one
       of them ever asks again is the difference between "the link would not
       take it" and "the game decided not to". Timestamped so the two guests'
       attempts line up against the close and against each other. Logged for a
       console that still thinks it is a client too (`role`): the request that
       follows a close is emitted by a game that has just been deauthed, but a
       request emitted before its own deauth lands was invisible to this line
       and read as "the guest never asked". */
    if (mp_trace() && (framectl & 0x00FF) == 0x0000)
        Log(LogLevel::Info,
            "[mp] association requested: %llu us (%lld after the close if it had one), slot %d, "
            "seq=%04X, role %s, deauths sent %llu\n",
            (unsigned long long)USTimestamp,
            MPD.close_rx_us ? (long long)(USTimestamp - MPD.close_rx_us) : 0LL,
            num, (len >= 26) ? *(u16*)&TXBuffer[12 + 22] : 0,
            IsMPClient ? "client" : (IsMP ? "host" : "idle"),
            (unsigned long long)MPD.deauth_sent);

    switch (num)
    {
    case 0:
    case 2:
    case 3:
        Platform::MP_SendPacket(TXBuffer, 12+len, USTimestamp, NDS.UserData);
        if (!IsMP) WifiAP->SendPacket(TXBuffer, 12+len);
        break;

    case 1:
        *(u16*)&TXBuffer[12 + 24+2] = MPClientMask;
        /* The host's side of the conversation, recorded at the one place a
           command frame actually leaves: how many, how many of them repeated
           the previous seqno (the game's own retry loop), and the body. */
        {
            const u16 cmdseq = *(u16*)&TXBuffer[12 + 22];
            if ((MPD.cmds_tx > 0) && (cmdseq == MPD.last_cmd_seq)) MPD.cmds_tx_dup++;
            MPD.cmds_tx++;
            MPD.last_cmd_seq = cmdseq;
            MPD.last_cmd_us = USTimestamp;
            mp_record_body(MPD.last_cmd_body, &MPD.last_cmd_len, &TXBuffer[12 + 24], len - 24);

            /* A round of this console's *own* game, counted where the command
               actually leaves. `MPLoopAlive` latches the stretch so its end is
               reportable once (see the watchdog in USTimer), and a round that
               addressed two clients is what tells a two-client lobby apart
               from one guest polling on its own. */
            if (IsMP && !IsMPClient)
            {
                if (!MPLoopAlive)
                {
                    MPLoopAlive = true;
                    MPD.loop_start_us = USTimestamp;
                }
                if (NumClients(MPClientMask) >= 2) MPD.rounds_multi++;
            }
        }
        Platform::MP_SendCmd(TXBuffer, 12+len, USTimestamp, NDS.UserData);
        break;

    case 5:
        /* Stock's count, kept exactly as stock writes it (the buffer's bytes are
           load-bearing - see the note above TXSendFrame); what the count no
           longer does is decide the frame's identity. */
        IncrementTXCount(slot);
        MPD.answers_sent++;
        MPD.last_answer_us = USTimestamp;
        mp_record_body(MPD.last_answer_body, &MPD.last_answer_len, &TXBuffer[12 + 24], len - 24);
        /* The answer's identity next to the arm it came from, so the mapping
           "round -> arm -> frame -> delivered answer" reads off the log rather
           than off the interleaving of two console threads' lines. `arm=#0` is
           a round the game armed nothing for, i.e. the firmware's stand-in. */
        if (mp_trace())
        {
            char hexbody[3 * 16 + 1];
            /* The reply-slot trace: which of the wireless RAM's slots this
               console's game wrote the answer into, and the AID the answer is
               stamped with. Both are needed to tell a client that answered
               correctly from one answering as an AID its host is not polling:
               the slot is per-console state, the AID is shared vocabulary, and
               a collision there files two clients' answers into one of the
               host's per-AID buffers (LocalMP::RecvReplies, packets[(aid-1)*1024]). */
            Log(LogLevel::Debug,
                "[mp] answer sent: reply slot %04X in wireless RAM (length byte %02X), "
                "stamped AID %d, frame seq=%04X arm=#%llu armed in round seq %04X, "
                "%d byte frame, body[%d]=%s\n",
                slot->Addr, (unsigned)((slot->Addr < 0x1FF0) ? RAM[slot->Addr + 0xA] : 0),
                MPReplyAID(),
                (len >= 24) ? *(u16*)&TXBuffer[12 + 22] : 0,
                (unsigned long long)MPArmServedSerial, MPArmServedSeq,
                len, (int)MPD.last_answer_len,
                mp_body_hex(MPD.last_answer_body, (int)MPD.last_answer_len, hexbody, sizeof(hexbody)));
        }
        /* The AID stamped on the answer is the association's, not the live
           register's: MKDS clears `W_AIDLow` in the same cycle it arms, the
           live register reads 0 here, and the host indexes its per-client
           buffer with it (`packets[(aid-1)*1024]`, LocalMP::RecvReplies) - a
           wild write that segfaulted the app on the first live run of this
           path (2026-09-27). */
        Platform::MP_SendReply(TXBuffer, 12+len, USTimestamp, MPReplyAID(), NDS.UserData);
        break;

    case 4:
        /* The beacon (TXSlots[4], W_TXSlotBeacon). The one field this console
           writes into the game's own frame is the 8-byte timestamp at payload
           offset 0, the field the hardware owns; the interval, capability and
           every IE are the game's (2026-09-27). */
        if (mp_trace() && len >= 24)
        {
            const int pl = len - 24;
            /* The emitter's own side of the beacon timing, on the same line as
               the frame: `USCounter` here is exactly what the line below
               stamps into the timestamp field, so the value every client's
               `beacon accepted` moves its own clock to is visible at the
               source. With `TXSLOTBEACON` and the countdown pair beside it,
               one line says which beacon slot this console armed, what its
               countdown read, and what time it announced - the three things a
               two-console split would have to show up in first. See
               `mph-teardown.py --wsync`, which pairs this with the client's
               reading of the same beacon on one wall clock. */
            Log(LogLevel::Debug,
                "[mp] beacon out: %d byte frame, fc=%04X dest=%02X%02X%02X%02X%02X%02X, "
                "seq=%04X, %d byte payload (interval %04X capability %04X) · my USCOUNT %llX "
                "(the timestamp this frame carries) · TXSLOTBEACON %04X countdown %04X/%04X\n",
                len, *(u16*)&TXBuffer[12], TXBuffer[16], TXBuffer[17], TXBuffer[18],
                TXBuffer[19], TXBuffer[20], TXBuffer[21], *(u16*)&TXBuffer[12 + 22], pl,
                (pl >= 10) ? *(u16*)&TXBuffer[12 + 24 + 8] : 0,
                (pl >= 12) ? *(u16*)&TXBuffer[12 + 24 + 10] : 0,
                (unsigned long long)USCounter, IOPORT(W_TXSlotBeacon),
                IOPORT(W_BeaconCount1), IOPORT(W_BeaconCount2));
        }

        *(u64*)&TXBuffer[0xC + 24] = USCounter;
        Platform::MP_SendPacket(TXBuffer, 12+len, USTimestamp, NDS.UserData);
        break;
    }
}

void Wifi::StartTX_LocN(int nslot, int loc)
{
    TXSlot* slot = &TXSlots[nslot];

    if (IOPORT(W_TXSlotLoc1 + (loc*4)) & 0x7000)
        Log(LogLevel::Warn, "wifi: unusual loc%d bits set %04X\n", loc, IOPORT(W_TXSlotLoc1 + (loc*4)));

    slot->Valid = true;

    slot->Addr = (IOPORT(W_TXSlotLoc1 + (loc*4)) & 0x0FFF) << 1;
    slot->Length = *(u16*)&RAM[slot->Addr + 0xA] & 0x3FFF;

    u8 rate = RAM[slot->Addr + 0x8];
    if (rate == 0x14) slot->Rate = 2;
    else              slot->Rate = 1;

    slot->CurPhase = 0;
    slot->CurPhaseTime = PreambleLen(slot->Rate);
}

void Wifi::StartTX_Cmd()
{
    TXSlot* slot = &TXSlots[1];

    if (IOPORT(W_TXSlotCmd) & 0x3000)
        Log(LogLevel::Warn,"wifi: !! unusual TXSLOT_CMD bits set %04X\n", IOPORT(W_TXSlotCmd));

    slot->Valid = true;

    slot->Addr = (IOPORT(W_TXSlotCmd) & 0x0FFF) << 1;
    slot->Length = *(u16*)&RAM[slot->Addr + 0xA] & 0x3FFF;

    u8 rate = RAM[slot->Addr + 0x8];
    if (rate == 0x14) slot->Rate = 2;
    else              slot->Rate = 1;

    /* The mask in the frame is the host's *game's* own text - it is the field
       the game's answers are matched against - and the transport's poll mask is
       that field narrowed by `MPClientFail`. Printing both when either changes
       is what makes "the host's game stopped addressing this client" a line in
       the log instead of a conclusion drawn from silence: the accept and the
       room's close are both commands whose mask is the only thing that differs
       between a client that is polled and one that is not. */
    const u16 game_mask = *(u16*)&RAM[slot->Addr + 12 + 24 + 2];
    MPClientMask = game_mask & MPClientFail;
    MPClientFail &= MPClientMask;

    if (mp_trace() && (game_mask != MPD.last_cmd_mask_game || MPClientMask != MPD.last_cmd_mask_poll))
    {
        char g[48], p[48];
        Log(LogLevel::Debug,
            "[mp] command addressing changes at %llu us: the game's own mask %04X (%s), "
            "polled as %04X (%s), clientfail %04X%s\n",
            (unsigned long long)USTimestamp,
            game_mask, mp_aid_list(game_mask, g, sizeof(g)),
            MPClientMask, mp_aid_list(MPClientMask, p, sizeof(p)), MPClientFail,
            (MPClientMask != game_mask) ? " - the transport narrowed it" : "");
        MPD.last_cmd_mask_game = game_mask;
        MPD.last_cmd_mask_poll = MPClientMask;
    }

    /* The close slice, host side. `06 01` is the command both games react to,
       and at three consoles the room splits on it, so what the host had
       credited when it sent the close is printed with the send rather than
       inferred from the reply lines around it in a 200 MB log. */
    if (mp_trace() && *(u16*)&RAM[slot->Addr + 12 + 24] == 0x0106)
    {
        char g[48], c[48];
        Log(LogLevel::Info,
            "[mp] close broadcast #%u at %llu us, game mask %04X (%s), last window "
            "credited %04X (%s) %lld us earlier, clients joined %u, clientfail %04X\n",
            (unsigned)MPD.close_tx, (unsigned long long)USTimestamp,
            game_mask, mp_aid_list(game_mask, g, sizeof(g)),
            MPD.last_collect_mask, mp_aid_list(MPD.last_collect_mask, c, sizeof(c)),
            (long long)(USTimestamp - MPD.last_collect_us),
            (unsigned)MPD.client_joins, MPClientFail);
        MPD.close_tx++;
    }


    u32 duration = PreambleLen(slot->Rate) + (slot->Length * (slot->Rate==2 ? 4:8));
    duration += 112 + ((10 + IOPORT(W_CmdReplyTime)) * NumClients(MPClientMask));
    duration += (32 * (slot->Rate==2 ? 4:8));

    if (CmdCounter > (duration + 100))
    {
        slot->CurPhase = 0;
        slot->CurPhaseTime = PreambleLen(slot->Rate);
    }
    else
    {
        slot->CurPhase = 13;
        slot->CurPhaseTime = CmdCounter - 100;
    }

    // starting a CMD transfer wakes up the transceiver automatically
    UpdatePowerStatus(1);
}

void Wifi::StartTX_Beacon()
{
    TXSlot* slot = &TXSlots[4];

    slot->Valid = true;

    slot->Addr = (IOPORT(W_TXSlotBeacon) & 0x0FFF) << 1;
    slot->Length = *(u16*)&RAM[slot->Addr + 0xA] & 0x3FFF;

    u8 rate = RAM[slot->Addr + 0x8];
    if (rate == 0x14) slot->Rate = 2;
    else              slot->Rate = 1;

    slot->CurPhase = 0;
    slot->CurPhaseTime = PreambleLen(slot->Rate);

    IOPORT(W_TXBusy) |= 0x0010;
}

void Wifi::FireTX()
{
    if (!(IOPORT(W_RXCnt) & 0x8000))
        return;

    u16 txbusy = IOPORT(W_TXBusy);

    u16 txreq = IOPORT(W_TXReqRead);
    u16 txstart = 0;
    if (IOPORT(W_TXSlotLoc1) & 0x8000) txstart |= 0x0001;
    if (IOPORT(W_TXSlotCmd ) & 0x8000) txstart |= 0x0002;
    if (IOPORT(W_TXSlotLoc2) & 0x8000) txstart |= 0x0004;
    if (IOPORT(W_TXSlotLoc3) & 0x8000) txstart |= 0x0008;

    txstart &= txreq;
    txstart &= ~txbusy;

    IOPORT(W_TXBusy) = txbusy | txstart;

    if (txstart & 0x0008)
    {
        StartTX_LocN(3, 2);
        return;
    }

    if (txstart & 0x0004)
    {
        StartTX_LocN(2, 1);
        return;
    }

    if (txstart & 0x0002)
    {
        MPClientFail = 0xFFFE;
        StartTX_Cmd();
        return;
    }

    if (txstart & 0x0001)
    {
        StartTX_LocN(0, 0);
        return;
    }
}

void Wifi::SendMPDefaultReply()
{
    u8 reply[12 + 28];

    *(u16*)&reply[0xA] = 28; // length

    // rate
    //if (TXSlots[1].Rate == 2) reply[0x8] = 0x14;
    //else                      reply[0x8] = 0xA;
    // TODO
    reply[0x8] = 0x14;

    /* The firmware's stand-in for an answer the game did not provide, sent
       when the guest has no reply slot armed. Seeing it on the wire (fc=0158)
       means the guest stopped answering - either it was never told a CMD
       arrived, or it has not re-armed since its last one. It is what a host
       reads in place of its peer's answer, so a run whose replies are mostly
       fc=0158 is a run whose guest was not being notified. */
    if (mp_trace())
        Log(LogLevel::Debug,
            "[mp] canned default reply (fc=0158): the game has no reply slot armed, so the "
            "firmware is answering for it\n");

    if (CurChannel == 0) return;
    reply[0x9] = CurChannel;

    *(u16*)&reply[0xC + 0x00] = 0x0158;
    *(u16*)&reply[0xC + 0x02] = 0x00F0;//0; // TODO??
    *(u16*)&reply[0xC + 0x04] = IOPORT(W_BSSID0);
    *(u16*)&reply[0xC + 0x06] = IOPORT(W_BSSID1);
    *(u16*)&reply[0xC + 0x08] = IOPORT(W_BSSID2);
    *(u16*)&reply[0xC + 0x0A] = IOPORT(W_MACAddr0);
    *(u16*)&reply[0xC + 0x0C] = IOPORT(W_MACAddr1);
    *(u16*)&reply[0xC + 0x0E] = IOPORT(W_MACAddr2);
    *(u16*)&reply[0xC + 0x10] = 0x0903;
    *(u16*)&reply[0xC + 0x12] = 0x00BF;
    *(u16*)&reply[0xC + 0x14] = 0x1000;
    *(u16*)&reply[0xC + 0x16] = IOPORT(W_TXSeqNo) << 4;
    *(u32*)&reply[0xC + 0x18] = 0;

    MPD.answers_canned++;
    MPD.last_answer_us = USTimestamp;
    mp_record_body(MPD.last_answer_body, &MPD.last_answer_len, &reply[12 + 24], 28 - 24);

    /* The same identity line as a real answer carries (TXSendFrame, case 5),
       with `arm=#0` because there was no arm to send: this round's answer is
       the firmware's stand-in, and the two must be told apart on the record,
       not by the shape of the frame. */
    if (mp_trace())
    {
        char hexbody[3 * 16 + 1];
        Log(LogLevel::Debug,
            "[mp] answer sent: frame seq=%04X arm=#0 (the firmware's stand-in), "
            "28 byte frame, body[4]=%s\n",
            *(u16*)&reply[12 + 22],
            mp_body_hex(MPD.last_answer_body, (int)MPD.last_answer_len, hexbody, sizeof(hexbody)));
    }
    MPArmServedSerial = 0;
    MPArmServedSeq = 0;

    int txlen = Platform::MP_SendReply(reply, 12+28, USTimestamp, MPReplyAID(), NDS.UserData);
    WIFI_LOG("wifi: sent %d/40 bytes of MP default reply\n", txlen);
}

/* The answer a CMD frame is answered with, and where it comes from.

   Stock answers a client's poll out of the reply slot as it stands when the
   CMD finishes arriving - a moment the game has not reached yet, because the
   frame's interrupt has not run. Measured on MKDS (2026-09-27): the driver arms
   its next answer from inside the handler for the *previous* poll and clears
   the slot again before that round ends, so it armed 5027 times for 5028 polls
   and only 2045 were answered. The answer is therefore *held* from the round it
   was armed in until the poll it answers (MPArmSlot/MPArmHeld), and the only
   window accepted is the game's own arm - never a firmware default frame.

   What must not change is *when* the game sees the slot consumed: taking the
   answer as it lands starves the driver (that session armed 16 times in total
   and then only cleared for the remaining 738 polls). Held to the poll instead,
   it armed once per poll for all 5028 of them. */
u16 Wifi::MPReplyAID()
{
    /* The AID this console answers as. `MPMYAID` (the association) first: the
       live register is 0 in the window MKDS arms its next answer in, and a
       reply stamped AID 0 is a wild write on the host (see TXSendFrame, case
       5). The register is still the answer when no association was seen, which
       is what stock used for every reply. */
    if (stock_mp()) return IOPORT(W_AIDLow) & 0x000F;
    if (MPMYAID) return MPMYAID;
    return IOPORT(W_AIDLow) & 0x000F;
}

/* Answer one poll. The arm the game made for it is the held one when the live
   slot is empty (see the note above MPReplyAID); with no arm at all the poll
   falls through to `SendMPDefaultReply`'s `00 00 00 00` body. Taken here rather
   than where the game wrote it, because this is when the game's driver expects
   the slot consumed and one arm answers exactly one poll; `MD_SAME_ROUND_REPLY`
   is the exception and calls this from the arm write instead, for the poll that
   arrived in that same round. */
void Wifi::AnswerPoll(u16 seqno, u16 clienttime, u16 clientmask)
{
    const bool live = (IOPORT(W_TXSlotReply1) & 0x8000) != 0;
    const bool held = !stock_mp() && MPArmHeld && !live;
    if (held)
        IOPORT(W_TXSlotReply1) = MPArmSlot;
    if (stock_mp() || !arm_persist())
        MPArmHeld = false;
    MPArmServedSerial = MPArmSerial;
    MPArmServedSeq = MPArmSeq;

    /* Which arm this poll took, and when the game made it. A game that is
       following the session arms a fresh answer every round, so the round of
       the arm and the round of the poll it answers are one apart - an arm from
       further back is the arm of an earlier poll, which means the host is being
       answered with the state the guest's game held *before* the command it is
       answering (2026-09-28). */
    if (mp_trace())
        Log(LogLevel::Debug,
            "[mp] answering cmd seq=%04X: AID %d, host mask %04X, clienttime %d, "
            "my clock %llu, %s (arm #%llu, armed in round seq %04X, %llu us before "
            "this poll)\n",
            seqno, MPMYAID, clientmask, clienttime,
            (unsigned long long)USTimestamp,
            held ? "the answer the game armed for this poll, held across its own clear" :
            (live ? "already in the reply slot at the poll"
                  : "nothing armed: the firmware's own reply"),
            (unsigned long long)MPArmSerial, MPArmSeq,
            (unsigned long long)(USTimestamp - MPArmUS));

    SendMPReply(clienttime, clientmask);
}

void Wifi::SendMPReply(u16 clienttime, u16 clientmask)
{
    TXSlot* slot = &TXSlots[5];

    // mark the last packet as success. dunno what the MSB is, it changes.
    //if (slot->Valid)
    if (IOPORT(W_TXSlotReply2) & 0x8000)
        *(u16*)&RAM[slot->Addr] = 0x0001;
    // CHECKME!!
    // can the transfer rate for MP replies be set, or is it determined from the CMD transfer rate?
    // how does it work for default empty replies?
    slot->Rate = 2;

    /* The hardware's own latch, on the ledger like every game write to a slot
       (2026-09-28): this is what puts the game's answer where the transmit
       machinery reads it, and the game's own slot is cleared beside it. */
    if (mp_trace())
        Log(LogLevel::Debug, "[mp] txslot REPLY2 <- %04X (the hardware's latch of REPLY1), "
            "REPLY1 <- 0000\n", IOPORT(W_TXSlotReply1));
    IOPORT(W_TXSlotReply2) = IOPORT(W_TXSlotReply1);
    IOPORT(W_TXSlotReply1) = 0;

    if (!(IOPORT(W_TXSlotReply2) & 0x8000))
    {
        slot->Valid = false;
    }
    else
    {
        slot->Valid = true;

        slot->Addr = (IOPORT(W_TXSlotReply2) & 0x0FFF) << 1;
        slot->Length = *(u16*)&RAM[slot->Addr + 0xA] & 0x3FFF;

        // the packet is entirely ignored if it lasts longer than the maximum reply time
        u32 duration = PreambleLen(slot->Rate) + (slot->Length * (slot->Rate==2 ? 4:8));
        if (duration > clienttime)
            slot->Valid = false;
    }

    // this seems to be set upon IRQ0
    // TODO: how does it behave if the packet addr is changed before it gets sent? (maybe just not possible)
    if (slot->Valid)
    {
        slot->CurPhase = 0;

        TXSendFrame(slot, 5);
    }
    else
    {
        slot->CurPhase = 10;

        SendMPDefaultReply();
    }

    u16 clientnum = 0;
    for (int i = 1; i < IOPORT(W_AIDLow); i++)
    {
        if (clientmask & (1<<i))
            clientnum++;
    }

    slot->CurPhaseTime = 16 + ((clienttime + 10) * clientnum) + PreambleLen(slot->Rate);

    IOPORT(W_TXBusy) |= 0x0080;
}

void Wifi::SendMPAck(u16 cmdcount, u16 clientfail)
{
    u8 ack[12 + 32];

    *(u16*)&ack[0xA] = 32; // length

    // rate
    if (TXSlots[1].Rate == 2) ack[0x8] = 0x14;
    else                      ack[0x8] = 0xA;

    if (CurChannel == 0) return;
    ack[0x9] = CurChannel;

    *(u16*)&ack[0xC + 0x00] = 0x0218;
    *(u16*)&ack[0xC + 0x02] = 0;
    *(u16*)&ack[0xC + 0x04] = 0x0903;
    *(u16*)&ack[0xC + 0x06] = 0x00BF;
    *(u16*)&ack[0xC + 0x08] = 0x0300;
    *(u16*)&ack[0xC + 0x0A] = IOPORT(W_BSSID0);
    *(u16*)&ack[0xC + 0x0C] = IOPORT(W_BSSID1);
    *(u16*)&ack[0xC + 0x0E] = IOPORT(W_BSSID2);
    *(u16*)&ack[0xC + 0x10] = IOPORT(W_MACAddr0);
    *(u16*)&ack[0xC + 0x12] = IOPORT(W_MACAddr1);
    *(u16*)&ack[0xC + 0x14] = IOPORT(W_MACAddr2);
    *(u16*)&ack[0xC + 0x16] = IOPORT(W_TXSeqNo) << 4;
    *(u16*)&ack[0xC + 0x18] = cmdcount;
    *(u16*)&ack[0xC + 0x1A] = clientfail;
    *(u32*)&ack[0xC + 0x1C] = 0;

    if (!clientfail)
    {
        u32 nextbeacon;
        if (IOPORT(W_TXBusy) & 0x0010)
            nextbeacon = 0;
        else
            nextbeacon = ((IOPORT(W_BeaconCount1) - 1) << 10) + (0x400 - (USCounter & 0x3FF));
        int runahead = std::min(CmdCounter, nextbeacon);
        if (CmdCounter < 1000) runahead -= 210;
        *(u32*)&ack[0] = std::max(runahead - (32*(TXSlots[1].Rate==2?4:8)), 0);
    }
    else
    {
        *(u32*)&ack[0] = PreambleLen(TXSlots[1].Rate);
    }

    /* The one field a guest can use to decide it has been dropped: `clientfail`
       is what the host's game tells each console about itself, so a guest that
       leaves a room it is still answering every round has usually read its own
       bit here. Printed with the mask the window actually credited, because the
       two disagreeing is the transport blaming a client the host's game has the
       answer from. */
    if (mp_trace())
    {
        char cf[48];
        Log(LogLevel::Debug,
            "[mp] ack sent: cmdcount %u, clientfail %04X (%s), last window credited %04X\n",
            cmdcount, clientfail, mp_aid_list(clientfail, cf, sizeof(cf)),
            MPD.last_collect_mask);
    }

    int txlen = Platform::MP_SendAck(ack, 12+32, USTimestamp, NDS.UserData);
    MPD.acks_sent++;
    if (clientfail) MPD.acks_client_fail++;
    MPD.last_ack_clientfail = clientfail;
    WIFI_LOG("wifi: sent %d/44 bytes of MP ack, %d %d\n", txlen, ComStatus, RXTime);
}

bool Wifi::ProcessTX(TXSlot* slot, int num)
{
    slot->CurPhaseTime -= kTimerInterval;
    if (slot->CurPhaseTime > 0)
    {
        if (slot->CurPhase == 1)
        {
            if (!(slot->CurPhaseTime & slot->HalfwordTimeMask))
                IOPORT(W_RXTXAddr)++;
        }
        else if (slot->CurPhase == 2)
        {
            MPReplyTimer -= kTimerInterval;
            if (MPReplyTimer <= 0 && MPClientMask != 0)
            {
                int nclient = 1;
                while (!(MPClientMask & (1 << nclient))) nclient++;

                u32 curclient = 1 << nclient;

                if (!(MPClientFail & curclient))
                    MPClientReplyRX(nclient);

                MPReplyTimer += 10 + IOPORT(W_CmdReplyTime);
                MPClientMask &= ~curclient;
            }
        }

        return false;
    }

    switch (slot->CurPhase)
    {
    case 0: // preamble done
        {
            SetIRQ(7);

            if (num == 5)
                SetStatus(8);
            else
                SetStatus(3);

            u32 len = slot->Length;
            if (slot->Rate == 2)
            {
                len *= 4;
                slot->HalfwordTimeMask = 0x7 & kTimeCheckMask;
            }
            else
            {
                len *= 8;
                slot->HalfwordTimeMask = 0xF & kTimeCheckMask;
            }

            slot->CurPhase = 1;
            slot->CurPhaseTime = len;

            // set TX addr
            IOPORT(W_RXTXAddr) = slot->Addr >> 1;

            if (num != 5)
            {
                TXSendFrame(slot, num);
            }

            // if the packet is being sent via LOC1..3, send it to the AP
            // any packet sent via CMD/REPLY/BEACON isn't going to have much use outside of local MP
            if (num == 0 || num == 2 || num == 3)
            {
                u16 framectl = *(u16*)&RAM[slot->Addr + 0xC];
                if ((framectl & 0x00FF) == 0x0010)
                {
                    u16 aid = *(u16*)&RAM[slot->Addr + 0xC + 24 + 4];
                    if (aid)
                    {
                        /* The newest client's association, timed: what the
                           host's game does when it gives the lobby up is
                           measured from here (see the loop watchdog in
                           USTimer and `loop_stop_since_join_us`). */
                        MPD.client_joins++;
                        MPD.last_client_join_us = USTimestamp;
                        Log(LogLevel::Debug, "[HOST] syncing client %04X, sync=%016llX\n", aid, USTimestamp);
                    }
                }
                else if ((framectl & 0x00FF) == 0x00C0)
                {
                    if (IsMPClient)
                    {
                        Log(LogLevel::Info, "[CLIENT] deauth\n");
                        IsMP = false;
                        IsMPClient = false;
                    }
                }
            }
        }
        break;

    case 10: // preamble done (default empty MP reply)
        {
            SetIRQ(7);
            SetStatus(8);

            slot->CurPhase = 11;
            slot->CurPhaseTime = 28*4;
            slot->HalfwordTimeMask = 0xFFFFFFFF;
        }
        break;

    case 1: // transmit done
        {
            // for the MP CMD and reply slots, this is set later
            if (num != 1 && num != 5)
                *(u16*)&RAM[slot->Addr] = 0x0001;
            RAM[slot->Addr + 5] = 0;

            if (num == 1)
            {
                if (IOPORT(W_TXStatCnt) & 0x4000)
                {
                    IOPORT(W_TXStat) = 0x0800;
                    SetIRQ(1);
                }
                SetStatus(5);

                MPReplyTimer = 16 + PreambleLen(slot->Rate);

                /* If this round is going to be held open for a client that
                   owes an answer, this window asks once and does not wait: the
                   hold is the mechanism that covers a late answer, and it does
                   it in emulated time (see MPInterface::SetReplyHold). */
                const int defer_frames = reply_defer_frames();
                Platform::MP_SetReplyHold(defer_frames > 0);

                u16 res = 0;
                if (MPClientMask)
                    res = Platform::MP_RecvReplies(MPClientReplies, USTimestamp, MPClientMask, NDS.UserData);

                Platform::MP_SetReplyHold(false);
                MPClientFail &= ~res;

                /* Which window the room was still crediting when its game sent
                   the close: the only host-side fact that tells "both guests
                   had answered" from "one of them had already stopped" at the
                   moment the close went out. */
                if (res)
                {
                    MPD.last_collect_mask = res;
                    MPD.last_collect_us = USTimestamp;
                }

                /* Reply rendezvous: a client that owes an answer gets the
                   round held open for it instead of being reported failed.
                   The ack is not sent yet and W_TXBusy stays set, so the game
                   waits exactly as it waits for a slow radio; phase 14 re-polls
                   eagerly until the round's clients have all answered or the
                   frame budget is spent. Healthy rounds (every asked client
                   already credited) never enter it, so the two-console path is
                   byte-for-byte what it was. */
                {
                    const u16 owed = (u16)(MPClientMask & ~res);
                    if ((defer_frames > 0) && owed)
                    {
                        MPDeferAsked = MPClientMask;
                        MPDeferCredited = res;
                        MPDeferStart = USTimestamp;
                        MPDeferPolls = 0;
                        MPDeferWallStart = mp_wall_us();
                        MPD.reply_deferred++;

                        if (mp_trace())
                            Log(LogLevel::Debug,
                                "[mp] reply rendezvous: round %04X held open - owed %04X, up to "
                                "%d frames\n",
                                MPDeferAsked, owed, defer_frames);

                        slot->CurPhase = 14;
                        slot->CurPhaseTime = 4 * kTimerInterval;
                        return true;
                    }
                }

                // TODO: 112 likely includes the ack preamble, which needs adjusted
                // for long-preamble settings
                slot->CurPhase = 2;
                slot->CurPhaseTime = 112 + ((10 + IOPORT(W_CmdReplyTime)) * NumClients(MPClientMask));

                break;
            }
            else if (num == 5)
            {
                if (IOPORT(W_TXStatCnt) & 0x1000)
                {
                    IOPORT(W_TXStat) = 0x0401;
                    SetIRQ(1);
                }
                SetStatus(1);

                IOPORT(W_TXBusy) &= ~0x80;
                FireTX();
                return true;
            }

            IOPORT(W_TXBusy) &= ~(1<<num);

            switch (num)
            {
            case 0:
            case 2:
            case 3:
                IOPORT(W_TXStat) = 0x0001 | ((num?(num-1):0)<<12);
                SetIRQ(1);
                IOPORT(W_TXSlotLoc1 + ((num?(num-1):0)*4)) &= 0x7FFF;
                break;

            case 4: // beacon
                if (IOPORT(W_TXStatCnt) & 0x8000)
                {
                    IOPORT(W_TXStat) = 0x0301;
                    SetIRQ(1);
                }
                break;
            }

            SetStatus(1);

            FireTX();
        }
        return true;

    case 11: // MP default reply transfer finished
        {
            IOPORT(W_TXSeqNo) = (IOPORT(W_TXSeqNo) + 1) & 0x0FFF;

            IOPORT(W_TXBusy) &= ~0x80;
            SetStatus(1);
            FireTX();
        }
        return true;

    case 2: // MP host transfer done
        {
            SetIRQ(7);
            SetStatus(8);

            IOPORT(W_RXTXAddr) = 0xFC0;

            if (slot->Rate == 2) slot->CurPhaseTime = 32 * 4;
            else                 slot->CurPhaseTime = 32 * 8;

            ReportMPReplyErrors(MPClientFail);

            /* The ack is the one frame the host's game reads to decide a client
               has gone: its `clientfail` field is the game's whole reason for
               dropping an AID from its mask. Printed with the emulated time
               and the mask the game addressed, because the question a dropped
               client turns into - "which round told the game it was gone" - is
               a timestamp no counter can answer. */
            if (mp_trace() && MPClientFail)
            {
                char f[48], a[48];
                Log(LogLevel::Info,
                    "[mp] ack reports failed clients at %llu us: clientfail %04X (%s), "
                    "addressed %04X (%s), last window credited %04X %lld us earlier\n",
                    (unsigned long long)USTimestamp,
                    MPClientFail, mp_aid_list(MPClientFail, f, sizeof(f)),
                    MPClientMask, mp_aid_list(MPClientMask, a, sizeof(a)),
                    MPD.last_collect_mask,
                    MPD.last_collect_us ? (long long)(USTimestamp - MPD.last_collect_us) : 0LL);
            }

            // send
            u16 cmdcount = (CmdCounter + 9) / 10;
            SendMPAck(cmdcount, MPClientFail);

            slot->CurPhase = 3;
        }
        break;

    case 3: // MP host ack transfer (reply wait done)
        {
            if (!MPClientFail)
                *(u16*)&RAM[slot->Addr] = 0x0001;
            else
                *(u16*)&RAM[slot->Addr] = 0x0005;

            // this is set to indicate which clients failed to reply
            *(u16*)&RAM[slot->Addr + 0x2] = MPClientFail;
            if (!MPClientFail)
                IncrementTXCount(slot);

            IOPORT(W_TXSeqNo) = (IOPORT(W_TXSeqNo) + 1) & 0x0FFF;

            if (IOPORT(W_TXStatCnt) & 0x2000)
            {
                IOPORT(W_TXStat) = 0x0B01;
                SetIRQ(1);
            }

            if (MPClientFail && false)
            {
                // if some clients failed to respond: try again
                // TODO: fix this (causes instability)
                StartTX_Cmd();
                break;
            }
            else
            {
                IOPORT(W_TXBusy) &= ~(1<<1);
                IOPORT(W_TXSlotCmd) &= 0x7FFF;

                SetStatus(1);
                SetIRQ(12);

                FireTX();
            }
        }
        return true;

    case 14: // MP host reply rendezvous (the ack is held open for a client that owes an answer)
        {
            /* Polled eagerly: the waiting here is this slot's own emulated
               time, so a poll must cost nothing (see SetReplyEager). Every
               tick asks the bus again; the round closes when every client the
               command addressed has answered, or when the frame budget is
               spent - the latter is the violation path a healthy link never
               takes, and it is counted so that "never" is a measurement. */
            MPDeferPolls++;
            Platform::MP_SetReplyEager(true);
            u16 res = 0;
            if (MPClientMask)
                res = Platform::MP_RecvReplies(MPClientReplies, USTimestamp, MPClientMask, NDS.UserData);
            Platform::MP_SetReplyEager(false);

            MPClientFail &= ~res;
            MPDeferCredited |= res;
            if (res)
            {
                MPD.last_collect_mask = res;
                MPD.last_collect_us = USTimestamp;
            }

            const u16 owed = (u16)(MPDeferAsked & ~MPDeferCredited);
            const u64 waited = USTimestamp - MPDeferStart;
            const u64 budget = (u64)reply_defer_frames() * 16715; // one emulated frame, in US ticks
            const u64 wall = mp_wall_us() - MPDeferWallStart;

            if (!owed || (waited >= budget))
            {
                if (owed)
                {
                    MPD.reply_defer_exhausted++;
                    if (mp_trace())
                        Log(LogLevel::Warn,
                            "[mp] reply rendezvous: gave up on %04X after %llu us (%.2f frames) - "
                            "the client did not answer the round it was handed\n",
                            owed, (unsigned long long)waited, waited / 16715.0);
                }
                else if (mp_trace())
                {
                    Log(LogLevel::Debug,
                        "[mp] reply rendezvous: round %04X closed with every client answered after "
                        "%llu us (%.2f frames)\n",
                        MPDeferAsked, (unsigned long long)waited, waited / 16715.0);
                }

                if (mp_trace())
                {
                    Log(LogLevel::Debug,
                        "[mp] rendezvous hold: round %04X, host frame %u, %llu us wall, %llu polls "
                        "for %llu us emulated (%.2f frames) - %.0f us wall per emulated frame held\n",
                        MPDeferAsked, NDS.NumFrames, (unsigned long long)wall,
                        (unsigned long long)MPDeferPolls, (unsigned long long)waited,
                        waited / 16715.0,
                        (waited > 0) ? (double)wall * 16715.0 / (double)waited : 0.0);
                }

                MPD.reply_defer_us += waited;

                // the ack goes out now, with whatever the round actually collected
                slot->CurPhase = 2;
                slot->CurPhaseTime = 112 + ((10 + IOPORT(W_CmdReplyTime)) * NumClients(MPClientMask));
            }
            else
            {
                slot->CurPhaseTime = reply_poll_ticks() * 4 * kTimerInterval;
            }
        }
        /* Not finished: the slot stays open (W_TXBusy keeps its command bit,
           TXCurSlot stays 1) until the ack has actually gone out in phase 2. */
        break;

    case 13: // MP transfer failed (timeout)
        {
            IOPORT(W_TXBusy) &= ~(1<<1);
            IOPORT(W_TXSlotCmd) &= 0x7FFF;

            *(u16*)&RAM[slot->Addr] = 0x0005;

            IOPORT(W_TXSeqNo) = (IOPORT(W_TXSeqNo) + 1) & 0x0FFF;

            SetStatus(1);
            SetIRQ(12);

            FireTX();
        }
        return true;
    }

    return false;
}


inline void Wifi::IncrementRXAddr(u16& addr, u16 inc)
{
    for (u32 i = 0; i < inc; i += 2)
    {
        addr += 2;
        addr &= 0x1FFE;
        if (addr == (IOPORT(W_RXBufEnd) & 0x1FFE))
            addr = (IOPORT(W_RXBufBegin) & 0x1FFE);
    }
}

void Wifi::StartRX(u64 bus_stamp)
{
    u16 framelen = *(u16*)&RXBuffer[8];

    /* Hand each frame off the shared bus to this console's own game exactly
       once (see the note on MPLastDelivered in Wifi.h). A repeated hand-over is
       counted and named rather than silently swallowed: the counter is the
       evidence, and suppressing the second copy would hide the very thing the
       counter exists to show. */
    if (bus_stamp)
    {
        const u16 gfc = (framelen >= 26) ? *(u16*)&RXBuffer[12] : 0;
        const u16 gseq = (framelen >= 26) ? *(u16*)&RXBuffer[12 + 22] : 0;

        bool repeat = false;
        for (int i = 0; i < 8; i++)
        {
            const MPDeliveredFrame& p = MPLastDelivered[i];
            if (p.Stamp == bus_stamp && p.Len == framelen && p.FC == gfc && p.Seq == gseq)
            {
                repeat = true;
                break;
            }
        }

        if (repeat)
        {
            MPRedeliveries++;
            if (mp_trace())
                Log(LogLevel::Warn,
                    "[mp] REDELIVERED: bus stamp %llu, %u byte frame, fc=%04X, seq=%04X already "
                    "handed to this console's game - %llu so far\n",
                    (unsigned long long)bus_stamp, (unsigned)framelen, gfc, gseq,
                    (unsigned long long)MPRedeliveries);
        }
        else
        {
            MPBusDeliveries++;
            MPLastDelivered[MPLastDeliveredNext] = { bus_stamp, (u32)framelen, gfc, gseq };
            MPLastDeliveredNext = (MPLastDeliveredNext + 1) & 7;
        }
    }

    RXTime = framelen;

    u16 txrate = *(u16*)&RXBuffer[6];
    if (txrate == 0x14)
    {
        RXTime *= 4;
        RXHalfwordTimeMask = 0x7 & kTimeCheckMask;
    }
    else
    {
        RXTime *= 8;
        RXHalfwordTimeMask = 0xF & kTimeCheckMask;
    }

    const u16 zone_start = (u16)((IOPORT(W_RXBufWriteCursor) << 1) & 0x1FFE);
    u16 addr = zone_start;
    IncrementRXAddr(addr, 12);
    IOPORT(W_RXTXAddr) = addr >> 1;

    RXBufferPtr = 12;

    /* Every delivery to this console's own software names the region the game
       is about to read out of the wireless RAM window, so the read recorder
       can keep the halfwords it takes out of it, in order. The 12-byte RX
       header the hardware wrote sits at the region's head (the command path
       writes it in CheckRX; the reply handoff does not, which is one of the
       things the report exists to show). */
    SetMPIoZone(zone_start, (u16)((12 + framelen < 0x1FFF) ? 12 + framelen : 0x1FFE),
                (framelen >= 26) ? *(u16*)&RXBuffer[12] : 0,
                (framelen >= 26) ? *(u16*)&RXBuffer[12 + 22] : 0, false);

    SetIRQ(6);
    SetStatus(6);
    ComStatus |= 1;

    /* Every packet the console's own software is about to be handed - the one
       delivery point for all received frames, MP commands included. Every one
       is listed, with the length the console will believe and the frame
       control the console will match on, because those two are what a game
       decides whether this is a packet it should answer: 2026-09-25, on the
       host, this log showed replies arriving with a length of 3354 bytes
       (`[mp] packet delivered ... 3354 byte frame, fc=0118`) next to the 30
       byte frames the same replies were supposed to be. */
    if (mp_trace())
    {
        static unsigned long rxstart = 0;
        ++rxstart;
        const u16 fc = (framelen >= 26) ? *(u16*)&RXBuffer[12] : 0;
        static const char* const sites[6] = {"nowhere", "the MP reply handoff (MPClientReplyRX)",
            "an association response", "a deauth", "the regular receive path (CheckRX)",
            "a held MP frame whose timestamp came (USTimer)"};
        Log(LogLevel::Debug,
            "[mp] packet delivered to the console's own software: %lu so far, %d byte frame, "
            "fc=%04X, %s, from %s\n",
            rxstart, framelen, fc,
            (IsMPClient || IsMP) ? (IsMPClient ? "as an MP client" : "as an MP host") : "outside multiplayer",
            sites[(MPRXSite >= 0 && MPRXSite <= 5) ? MPRXSite : 0]);

        /* The big broadcasts need their contents on the record *in full*:
           MKDS's host announces its group state in a 120-byte fc=0080 broadcast
           every ~217ms, and in every run so far the guest's game leaves within
           ~20ms of being handed one (2026-09-26, and again on the host's
           115th beacon of the 2026-09-27 session, 7s after the VS tap). Which
           field it refuses cannot be read off the first 48 bytes: the member
           table lives in the 52-byte vendor IE that starts at payload offset
           26, so the whole payload has to be on the record, in numbered lines,
           for the lobby beacon and the one that precedes the deauth to be
           compared byte for byte. */
        if (framelen >= 48)
        {
            const int total = framelen - 24;
            for (int off = 0; off < total; off += 48)
            {
                int n = total - off;
                if (n > 48) n = 48;
                if (12 + 24 + off + n > (int)sizeof(RXBuffer)) break;

                char hex[3 * 48 + 1];
                char text[49];
                for (int i = 0; i < n; i++)
                {
                    u8 b = RXBuffer[12 + 24 + off + i];
                    snprintf(&hex[i * 3], 4, " %02X", b);
                    text[i] = (b >= 32 && b < 127) ? (char)b : '.';
                }
                text[n] = 0;
                Log(LogLevel::Debug, "[mp]   body[%d..%d]%s  text \"%s\"\n",
                    off, off + n - 1, hex, text);
            }
        }
    }
}

void Wifi::FinishRX()
{
    ComStatus &= ~0x1;
    RXCounter = 0;

    if (!ComStatus)
    {
        if (IOPORT(W_PowerState) & (1<<9))
        {
            IOPORT(W_TRXPower) = 0;
            SetStatus(9);
        }
        else
            SetStatus(1);
    }

    // TODO: RX stats

    u16 framectl = *(u16*)&RXBuffer[12];
    u16 seqno = *(u16*)&RXBuffer[12 + 22];

    /* An MP CMD frame - the host polling its clients - is recognised by its
       destination MAC, the same test the data-frame filter below uses. Knowing
       it up here lets every early return say so by name (see mp_trace). */
    const bool mpcmd = MACEqual(&RXBuffer[12 + 4], MPCmdMAC);

    // check the frame's destination address
    // note: the hardware always checks the first address field, regardless of the frame type/etc
    // similarly, the second address field is used to send acks to non-broadcast frames

    u8* dstmac = &RXBuffer[12 + 4];
    if (!(dstmac[0] & 0x01))
    {
        if (!MACEqual(dstmac, (u8*)&IOPORT(W_MACAddr0)))
            return;
    }

    // reject the frame if it's a WEP frame and WEP is off
    // TODO: check if sending WEP frames with WEP off works at all?

    if (framectl & (1<<14))
    {
        if (!(IOPORT(W_WEPCnt) & (1<<15)))
            return;
    }

    // apply RX filtering
    // TODO:
    // * RXFILTER bits 0, 9, 10, 12 not fully understood
    // * port 0D8 also affects reception of frames
    // * MP CMD frames with a duplicate sequence number are ignored

    u16 rxflags = 0x0010;
    bool cmd_dupe = false;

    switch ((framectl >> 2) & 0x3)
    {
    case 0: // management
        {
            u8* bssid = &RXBuffer[12 + 16];
            if (MACEqual(bssid, (u8*)&IOPORT(W_BSSID0)))
                rxflags |= 0x8000;

            u16 subtype = (framectl >> 4) & 0xF;
            if (subtype == 0x8) // beacon
            {
                if (!(rxflags & 0x8000))
                {
                    /* A beacon whose BSSID is not ours. On a console that has
                       been associated with a group this is the client's beacon
                       sync being skipped: the branch at the end of this
                       function that copies the beacon's timestamp into USCOUNT
                       only runs for our own BSSID, and a client that does not
                       take it cannot schedule its own beacon either (see
                       SetIRQ14). The host's beacon is the only beacon on this
                       bus, so a client that logs this line is being offered
                       the host's beacon and refusing it. */
                    if (mp_trace() && (IsMPClient || MPMYAID))
                    {
                        const u8* ours = GetBSSID();
                        Log(LogLevel::Debug,
                            "[mp] beacon from another BSSID (%02X:%02X:%02X:%02X:%02X:%02X, ours "
                            "%02X:%02X:%02X:%02X:%02X:%02X): USCOUNT is not synced to it%s\n",
                            RXBuffer[12 + 16], RXBuffer[12 + 17], RXBuffer[12 + 18],
                            RXBuffer[12 + 19], RXBuffer[12 + 20], RXBuffer[12 + 21],
                            ours[0], ours[1], ours[2], ours[3], ours[4], ours[5],
                            (IOPORT(W_RXFilter) & (1<<0)) ? "" : " (and RXFilter bit 0 is clear, "
                            "so the frame is dropped right here)");
                    }

                    if (!(IOPORT(W_RXFilter) & (1<<0)))
                        return;
                }

                rxflags |= 0x0001;
            }
            else if ((subtype <= 0x5) ||
                     (subtype >= 0xA && subtype <= 0xC))
            {
                if (!(rxflags & 0x8000))
                {
                    // CHECKME!
                    if (!(IOPORT(W_RXFilter) & (3<<9)))
                        return;
                }
            }
        }
        break;

    case 1: // control
        {
            if ((framectl & 0xF0) == 0xA0) // PS-poll
            {
                u8* bssid = &RXBuffer[12 + 4];
                if (MACEqual(bssid, (u8*)&IOPORT(W_BSSID0)))
                    rxflags |= 0x8000;

                if (!(rxflags & 0x8000))
                {
                    if (!(IOPORT(W_RXFilter) & (1<<11)))
                        return;
                }

                rxflags |= 0x0005;
            }
            else
                return;
        }
        break;

    case 2: // data
        {
            u16 fromto = (framectl >> 8) & 0x3;
            if (IOPORT(W_RXFilter2) & (1<<fromto))
            {
                if (mpcmd && mp_trace())
                    Log(LogLevel::Debug,
                        "[mp] cmd seq=%04X dropped before any handling: RXFilter2 kills from/to=%d (RXFilter2=%04X)\n",
                        seqno, fromto, IOPORT(W_RXFilter2));
                return;
            }

            int bssidoffset[4] = {16, 4, 10, 0};
            if (bssidoffset[fromto])
            {
                u8* bssid = &RXBuffer[12 + bssidoffset[fromto]];
                if (MACEqual(bssid, (u8*)&IOPORT(W_BSSID0)))
                    rxflags |= 0x8000;
            }

            u16 rxfilter = IOPORT(W_RXFilter);

            if (!(rxflags & 0x8000))
            {
                if (!(rxfilter & (1<<11)))
                {
                    if (mpcmd && mp_trace())
                        Log(LogLevel::Debug,
                            "[mp] cmd seq=%04X dropped: BSSID is not ours (%02X:%02X:%02X:%02X:%02X:%02X vs "
                            "%04X%04X%04X) and RXFilter bit 11 is clear, so the reply never happens\n",
                            seqno, RXBuffer[12+10], RXBuffer[12+11], RXBuffer[12+12],
                            RXBuffer[12+13], RXBuffer[12+14], RXBuffer[12+15],
                            IOPORT(W_BSSID0), IOPORT(W_BSSID1), IOPORT(W_BSSID2));
                    return;
                }
            }

            if (framectl & (1<<11)) // retransmit
            {
                if (!(rxfilter & (1<<0)))
                {
                    if (mpcmd && mp_trace())
                        Log(LogLevel::Debug,
                            "[mp] cmd seq=%04X dropped: retransmit and RXFilter bit 0 is clear\n",
                            seqno);
                    return;
                }
            }

    // check for MP frames
    // the hardware simply checks for these specific MAC addresses
    // the reply check has priority over the other checks
    // TODO: it seems to be impossible to receive a MP reply outside of a CMD transfer's reply timeframe
    // if the framectl subtype field is 1 or 5
    // maybe one of the unknown registers controls that?
    // maybe it is impossible to receive CF-Ack frames outside of a CF-Poll period?
    // TODO: GBAtek says frame type F is for all empty packets?
    // my hardware tests say otherwise

            if (MACEqual(&RXBuffer[12 + 16], MPReplyMAC))
            {
                if ((framectl & 0xF0) == 0x50)
                    rxflags |= 0x000F;
                else
                    rxflags |= 0x000E;
            }
            else if (MACEqual(&RXBuffer[12 + 4], MPCmdMAC))
            {
                if (seqno == MPLastSeqno) cmd_dupe = true;
                MPLastSeqno = seqno;

                /* A duplicate CMD frame is answered by the wireless hardware
                   like any other, but stock melonDS never raises IRQ0 for it,
                   so the game is not told anything arrived (see the cmd_dupe
                   guard below). `cmd_retry_notify()` decides which behaviour
                   runs; its comment has the MKDS measurement behind the
                   default. The delivered case is the one that matters when a
                   join fails: it is the difference between a retry the peer
                   can answer afresh and a retry answered from a spent reply
                   slot. */
                if (cmd_dupe && mp_trace())
                {
                    if (cmd_retry_notify())
                        Log(LogLevel::Debug,
                            "[mp] cmd seq=%04X repeats the previous one: answered by the hardware "
                            "anyway, and handed to the game so it can re-arm its reply slot\n", seqno);
                    else
                        Log(LogLevel::Debug,
                            "[mp] cmd seq=%04X repeats the previous one: answered by the hardware, "
                            "but the game is not told of this one (stock; MD_CMD_RETRY_NOTIFY=1 "
                            "delivers retries to the game)\n", seqno);
                }

                rxflags |= 0x000C;
            }
            else if (MACEqual(&RXBuffer[12 + 4], MPAckMAC))
            {
                rxflags |= 0x000D;
            }
            else
            {
                rxflags |= 0x0008;
            }

            switch ((framectl >> 4) & 0xF)
            {
            case 0x0: break;

            case 0x1:
                if ((rxflags & 0xF) == 0xD)
                {
                    if (!(rxfilter & (1<<7))) return;
                }
                else if ((rxflags & 0xF) != 0xE)
                {
                    if (!(rxfilter & (1<<1))) return;
                }
                break;

            case 0x2:
                if ((rxflags & 0xF) != 0xC)
                {
                    if (!(rxfilter & (1<<2))) return;
                }
                break;

            case 0x3:
                if (!(rxfilter & (1<<3))) return;
                break;

            case 0x4: break;

            case 0x5:
                if ((rxflags & 0xF) == 0xF)
                {
                    if (!(rxfilter & (1<<8))) return;
                }
                else
                {
                    if (!(rxfilter & (1<<4))) return;
                }
                break;

            case 0x6:
                if (!(rxfilter & (1<<5))) return;
                break;

            case 0x7:
                if (!(rxfilter & (1<<6))) return;
                break;

            default:
                return;
            }
        }
        break;
    }

    if (!cmd_dupe || cmd_retry_notify())
    {
        // build the RX header

        const u16 rxzone_start = (u16)((IOPORT(W_RXBufWriteCursor) << 1) & 0x1FFE);
        const int rxlen = (int)(*(u16*)&RXBuffer[8]); // the length the header below hands the game

        /* The 12-byte hardware RX header, per GBATEK's "Hardware RX Header
           (RXHDR)" table: flags (bits0-3 frame type), unknown, *unchanged*,
           transfer rate, length, MAX/MIN RSSI.

           Offset `+4` is NOT written here on purpose - GBATEK records it as
           "not updated by hardware, contains old Wifi RAM content", and that is
           a decision, not an oversight: the v0.1.9i read recorder caught both
           games reading it (host `17DC`, guest `0FCC` in one parked round) and
           then consuming the whole body anyway, so the field does not gate
           consumption. Upstream melonDS skips the halfword identically
           (`IncrementRXAddr(headeraddr, 4)`) and no version of this fork has
           ever had a value for it; writing an invented one would be the one
           thing the record rules out. */
        u16 headeraddr = IOPORT(W_RXBufWriteCursor) << 1;
        *(u16*)&RAM[headeraddr] = rxflags;
        IncrementRXAddr(headeraddr);
        *(u16*)&RAM[headeraddr] = 0x0040; // 0040h = Normal (see the table above)
        IncrementRXAddr(headeraddr, 4); // +4: left as it stands, per GBATEK
        *(u16*)&RAM[headeraddr] = *(u16*)&RXBuffer[6]; // TX rate
        IncrementRXAddr(headeraddr);
        *(u16*)&RAM[headeraddr] = *(u16*)&RXBuffer[8]; // frame length
        IncrementRXAddr(headeraddr);
        *(u16*)&RAM[headeraddr] = 0x4080; // MAX RSSI 80h, MIN RSSI 40h

        // signal successful reception

        u16 addr = IOPORT(W_RXTXAddr) << 1;
        if (addr & 0x2) IncrementRXAddr(addr);
        IOPORT(W_RXBufWriteCursor) = (addr & ~0x3) >> 1;

        /* What this console's own game is about to be offered, in its terms:
           the 12-byte header the hardware just wrote at the write cursor and
           the frame behind it. A CMD frame is where the guest's side of a
           failed handshake has to be read, because the header is what the game
           matches on (frame control, length, the `0x0040` word) and the payload
           behind it is what it has to recognise as a state it can adopt. The
           write cursor can advance by a halfword between frames, so the region
           is recorded for `Read` to match the game's own reads against. */
        /* The region the game is about to be handed, with this round's
           identity: the recorder keeps what the game reads out of it (see
           MPIo), and the trace below names the frame for the log. */
        {
            const int zlen = 12 + rxlen;
            SetMPIoZone(rxzone_start, (u16)((zlen < 0x1FFF) ? zlen : 0x1FFE),
                        (u16)((rxlen >= 26) ? *(u16*)&RXBuffer[12] : 0), seqno, mpcmd != 0);
        }

        if (mp_trace() && mpcmd)
        {
            const u16 z = rxzone_start;

            /* Read the region back the way the console reads it: through
               IncrementRXAddr, which wraps at the ring's programmed end
               (`W_RXBufEnd` -> `W_RXBufBegin`), not at the end of the RAM
               window. The two differ, and printing the region linearly is how
               the 2026-10-23 Tetris storm (t2t6) came to hold 3 of 1,248
               deliveries whose header said `801C 0040 A5A5 5A5A A5A5 5A5A` and
               10 whose payload prefix was RAM fill: every one of them had its
               region straddle the ring end, and the page between the ring's
               end and 0x1FFF is where the fill lives. The frame was written
               correctly all along - the trace was reading past it, which is
               the one thing a field diff must never do (see
               tools/scripts/delivery-field-diff.py, which flags exactly those
               copies). */
            char hdr[5 * 6 + 1] = "-";
            u16 ha = z;
            for (int i = 0; i < 6; i++)
            {
                snprintf(&hdr[i * 5], 6, "%04X ", *(u16*)&RAM[ha & 0x1FFE]);
                IncrementRXAddr(ha);
            }
            hdr[6 * 5 - 1] = 0;

            /* The frame itself sits behind the 12-byte header as the console's
               own copy of it: a 24-byte 802.11 header and then the payload, so
               the payload the game matches on is at +12+24. Walked the same
               way, halfwords at a time, because the byte order the console sees
               is the halfword order the receive loop wrote. */
            char pay[3 * 16 + 1] = "-";
            const int paylen = (rxlen > 24) ? (rxlen - 24) : 0;
            const int payn = (paylen > 16) ? 16 : paylen;
            {
                u16 pa = z;
                IncrementRXAddr(pa, 12 + 24);
                for (int i = 0; i < payn; i += 2)
                {
                    const u16 hw = *(u16*)&RAM[pa & 0x1FFE];
                    snprintf(&pay[i * 3], 4, "%02X ", hw & 0xFF);
                    if (i + 1 < payn) snprintf(&pay[(i + 1) * 3], 4, "%02X ", hw >> 8);
                    IncrementRXAddr(pa);
                }
                if (payn > 0) pay[payn * 3 - 1] = 0;
            }

            /* Whether this region straddles the ring's end, and the ring's own
               geometry. A straddling region is the one case where the
               recorder's single `[start, start+len)` bracket (see MPIo) cannot
               account for every read, so a log that says `WRAPS` here is the
               log to re-take before anything is concluded from its read
               counts. */
            const u16 ringbegin = IOPORT(W_RXBufBegin) & 0x1FFE;
            const u16 ringend = IOPORT(W_RXBufEnd) & 0x1FFE;
            bool wraps = false;
            {
                u16 w = z;
                for (int i = 0; i < 12 + rxlen; i += 2)
                {
                    IncrementRXAddr(w);
                    if (w == ringbegin && ringend != ringbegin) { wraps = true; break; }
                }
            }
            char ring[48];
            snprintf(ring, sizeof(ring), " ring %04X..%04X wr=%04X rd=%04X%s",
                     ringbegin, ringend, IOPORT(W_RXBufWriteCursor),
                     IOPORT(W_RXBufReadCursor), wraps ? " WRAPS" : "");

            /* Whose poll this is has to be read the way the answer path reads it
               (see MPReplyAID): the live register first, and the remembered
               association when the register has been cleared. Printing only the
               register read is how this line came to say `AID 0 (associated 1:
               this poll is NOT ours)` about a poll the console then answered
               stamped AID 1 (logs/da3_app.log), i.e. the trace contradicted the
               path it exists to explain. */
            const u16 liveaid = IOPORT(W_AIDLow) & 0x000F;
            const u16 myaid = MPReplyAID();
            const u16 cmdmask = *(u16*)&RXBuffer[0xC + 26];

            Log(LogLevel::Debug,
                "[mp] cmd seq=%04X offered to this console's game: rxflags=%04X, %d byte "
                "802.11 frame, header[%s] at %04X, payload@24[%s], clienttime %d, mask %04X, "
                "AID %d%s (associated %d: %s)%s%s\n",
                seqno, rxflags, rxlen, hdr, rxzone_start, pay,
                *(u16*)&RXBuffer[0xC + 24], cmdmask,
                myaid,
                (!liveaid && myaid) ? " (from the association; W_AIDLow reads 0)" : "",
                MPMYAID,
                (myaid && (cmdmask & (1 << myaid))) ? "this poll is ours"
                                                   : "this poll is NOT ours",
                cmd_dupe ? ", a repeat of the previous seqno" : "",
                ring);

            /* The close slice, guest side: what this console held when the
               host's `06 01` was handed to its game. The two guests' lines are
               compared by their own clocks (the consoles run in lockstep), and
               the seqno says whether both were handed the same send, so
               "delivered to the same round" is read off the log, not assumed. */
            if (*(u16*)&RAM[(z + 12 + 24) & 0x1FFE] == 0x0106)
            {
                char m[48];
                Log(LogLevel::Info,
                    "[mp] close received #%u: cmd seq=%04X at my clock %llu, mask %04X (%s), "
                    "my AID %d, W_AIDLow %04X, clients joined %u, deauths sent %llu "
                    "received %llu%s\n",
                    (unsigned)MPD.close_rx, seqno, (unsigned long long)USTimestamp,
                    cmdmask, mp_aid_list(cmdmask, m, sizeof(m)),
                    MPReplyAID(), IOPORT(W_AIDLow),
                    (unsigned)MPD.client_joins,
                    (unsigned long long)MPD.deauth_sent, (unsigned long long)MPD.deauth_recv,
                    cmd_dupe ? ", a repeat of the previous seqno" : "");
                MPD.close_rx++;
                MPD.close_rx_us = USTimestamp;
            }
        }

        SetIRQ(0);
    }

    if ((rxflags & 0x800F) == 0x800C)
    {
        // reply to CMD frames

        u16 clientmask = *(u16*)&RXBuffer[0xC + 26];

        /* The AID that decides whether this poll is ours is the one this
           console is associated with. `W_AIDLow` is asked first, because that
           is stock melonDS and it is right whenever the game is holding the
           register up - but MKDS's driver clears it as part of the same
           handler that arms the next answer, and taking that transient as
           "this console is not addressed" threw away 75% of a live session's
           rounds before this (see the note above MPReplyAID). */
        u16 myaid = IOPORT(W_AIDLow) & 0x000F;
        if (!myaid && !stock_mp()) myaid = MPMYAID;

        if (myaid && (clientmask & (1 << myaid)))
        {
            /* The arm the game made for this poll, held since it was written
               (see the note above MPReplyAID). Taken here, not when it was
               written, because this is when the game's driver expects the slot
               consumed, and one arm answers exactly one poll; a poll addressed
               elsewhere leaves it alone. With no arm held the poll falls
               through to `SendMPDefaultReply`, whose `00 00 00 00` body
               asserts a state the peer's game never reported - 22% of the
               guest's answers through the MKDS freeze (2026-09-28), which is
               what the arm_persist() default above is about. */
            const bool same = reply_same_round() && !stock_mp();
            const bool live = (IOPORT(W_TXSlotReply1) & 0x8000) != 0;

            if (same && MPPendingPoll)
            {
                /* The poll before this one was never answered in its own round
                   - the game's arm for it never came. Answer it now with
                   whatever the slot holds rather than leave the round silent,
                   which is the held path's own fallback one poll late. */
                if (mp_trace())
                    Log(LogLevel::Debug,
                        "[mp] same-round reply: cmd seq=%04X's arm never came in its round, "
                        "answered by the next poll %llu us later\n",
                        MPPendingSeq, (unsigned long long)(USTimestamp - MPPendingTime));
                const u16 pt = MPPendingTime, pm = MPPendingMask, ps = MPPendingSeq;
                MPPendingPoll = false;
                AnswerPoll(ps, pt, pm);
            }

            if (same && !live)
            {
                /* The game's handler for this poll has not run yet - the
                   frame's interrupt has not fired - so its arm for this round
                   does not exist. Remember the poll and let the arm write
                   answer it inside this same round (`MD_SAME_ROUND_REPLY`). */
                MPPendingPoll = true;
                MPPendingTime = *(u16*)&RXBuffer[0xC + 24];
                MPPendingMask = clientmask;
                MPPendingSeq = seqno;
                if (mp_trace())
                    Log(LogLevel::Debug,
                        "[mp] same-round reply: cmd seq=%04X held for this round's own arm\n",
                        seqno);
            }
            else
            {
                /* Stock's own reply path, unchanged - including what it does
                   when the slot is not armed: send the firmware's reply
                   (`fc=0158`) far enough into the round for the host's
                   collection to credit it. Never silence: the host's game
                   counts an unanswered poll as a failed client, and 1932 of
                   those in one session is what put "Communication error.
                   Press the A Button." on both screens even though the link
                   itself never dropped a frame. */
                AnswerPoll(seqno, *(u16*)&RXBuffer[0xC + 24], clientmask);
            }
        }
        else
        {
            // send a blank
            // this is just so the host can have something to receive, instead of hitting a timeout
            // in the case this client wasn't ready to send a reply
            // TODO: also send this if we have RX disabled

            if (mp_trace())
            {
                char aids[48];
                Log(LogLevel::Debug,
                    "[mp] BLANK reply to cmd seq=%04X: this console is AID %d, the host's mask is "
                    "%04X (%s), so it is not addressed - the host hears silence and counts a "
                    "failed reply\n",
                    seqno, MPReplyAID(), clientmask, mp_aid_list(clientmask, aids, sizeof(aids)));
            }

            MPD.answers_blank++;
            Platform::MP_SendReply(nullptr, 0, USTimestamp, 0, NDS.UserData);
        }
    }
    else if ((rxflags & 0x800F) == 0x8001)
    {
        // when receiving a beacon with the right BSSID, the beacon's timestamp
        // is copied to USCOUNTER

        u32 len = *(u16*)&RXBuffer[8];
        u16 txrate = *(u16*)&RXBuffer[6];
        len *= ((txrate==0x14) ? 4 : 8);
        len -= 76; // CHECKME: is this offset fixed?

        u64 timestamp = *(u64*)&RXBuffer[12 + 24];
        timestamp += (u64)len;

        const u64 before = USCounter;
        USCounter = timestamp;

        /* This is the client's only source of the host's clock, and the value
           the game then schedules its own beacon against (see SetIRQ14 and the
           USCOMPARE0 write). Logging it separates a client that is being
           synced and doing nothing about it from one that is never synced at
           all - which is a BSSID mismatch, logged where the beacon arrives. */
        if (mp_trace())
            Log(LogLevel::Debug,
                "[mp] beacon accepted (our BSSID): USCOUNT %llX -> %llX, the beacon's own "
                "timestamp + %u cycles (I moved %lld us) · my TXSLOTBEACON %04X "
                "USCOMPARECNT %04X\n",
                (unsigned long long)before, (unsigned long long)timestamp, len,
                (long long)(timestamp - before), IOPORT(W_TXSlotBeacon),
                IOPORT(W_USCompareCnt));
    }
}

/* See SetMPProbe in Wifi.h. Diagnostics only, and never on the wire: both call
   sites hand it the frame the console's own game is about to read, so a probe
   can change what a game sees but never what a console sends. */
void Wifi::SetMPProbe(int kind, int value, const u8* body, int bodylen)
{
    /* Arm with the probe *off* and switch it on last. This runs on the control
       thread while the emulation thread is in MPClientReplyRX, and arming the
       kind first left a live window in which the new kind was already armed
       against the *previous* probe's bytes: one delivery would then carry the
       wrong body and still be counted against the probe being set. */
    MPProbeKind = 0;
    MPProbeLeft = value;
    MPProbeBodyLen = 0;
    MPProbeLogged = false;

    if (kind == 1 && body && bodylen > 0)
    {
        int n = (bodylen > (int)sizeof(MPProbeBody)) ? (int)sizeof(MPProbeBody) : bodylen;
        if (n > 16) n = 16;
        MPProbeBodyLen = n;
        memcpy(MPProbeBody, body, (size_t)MPProbeBodyLen);
    }

    /* A body probe with no body is nothing to run, and a probe with no
       deliveries left is the same as no probe. `value` <= 0 means "until
       cleared" and is stored as -1, because the hook tests `!= 0` to decide
       whether a probe is live - arming one with 0 was accepted and then never
       fired once, which is what the first probe runs of 2026-09-28 recorded. */
    if (kind == 1 && MPProbeBodyLen > 0)
    {
        if (MPProbeLeft <= 0) MPProbeLeft = -1;
        MPProbeKind = 1;
    }

    if (mp_trace())
        Log(LogLevel::Debug,
            "[mp] probe set: kind %d, %d deliveries, %d body bytes, %llu applied so far\n",
            MPProbeKind, MPProbeLeft, MPProbeBodyLen,
            (unsigned long long)MPProbeApplied);
}

/* The probe's hook (see SetMPProbe): `framelen` is the 802.11 length of the
   reply already sitting at `RXBuffer[12]`, FCS cropped. Nothing here changes
   what the peer's game sent - the wire still carries the peer's own bytes - and
   the line is written once per arm rather than once per delivery: with one cell
   armed for a whole run, sixty identical lines a second say nothing the
   `applied` counter does not. A frame with no body behind its header (a blank
   or cropped reply) leaves the probe armed and waits for one that has. */
void Wifi::MPProbeBodyHook(int framelen)
{
    if (MPProbeKind != 1 || MPProbeLeft == 0) return;

    const int bodylen = framelen - 24;
    const int n = (MPProbeBodyLen < bodylen) ? MPProbeBodyLen : bodylen;
    if (n <= 0) return;

    /* What the peer's own game said this round, kept before the override: it is
       the baseline the injected bytes are read against. */
    u8 peer[16] {};
    const int pn = (n > 16) ? 16 : n;
    memcpy(peer, &RXBuffer[12 + 24], (size_t)pn);
    memcpy(&RXBuffer[12 + 24], MPProbeBody, (size_t)pn);

    MPProbeApplied++;
    if (MPProbeLeft > 0) MPProbeLeft--;
    if (MPProbeLeft == 0) MPProbeKind = 0;

    if (mp_trace() && !MPProbeLogged)
    {
        MPProbeLogged = true;
        char cellhex[3 * 16 + 1] = "-";
        char said[3 * 16 + 1] = "-";
        for (int i = 0; i < pn; i++) snprintf(&cellhex[i * 3], 4, "%02X ", MPProbeBody[i]);
        for (int i = 0; i < pn; i++) snprintf(&said[i * 3], 4, "%02X ", peer[i]);
        cellhex[pn * 3 - 1] = 0;
        said[pn * 3 - 1] = 0;
        Log(LogLevel::Debug,
            "[mp] probe applied: inject [%s] over the peer's [%s] (%d of %d body bytes), "
            "applied %llu\n",
            cellhex, said, pn, bodylen, (unsigned long long)MPProbeApplied);
    }
}

void Wifi::GetMPProbe(int* kind, int* left, u64* applied) const
{
    if (kind) *kind = MPProbeKind;
    if (left) *left = MPProbeLeft;
    if (applied) *applied = MPProbeApplied;
}

void Wifi::MPClientReplyRX(int client)
{
    if (IOPORT(W_PowerState) & (1<<9))
        return;

    if (!(IOPORT(W_RXCnt) & 0x8000))
        return;

    if (IOPORT(W_RXBufBegin) == IOPORT(W_RXBufEnd))
        return;

    int framelen;
    u8 txrate;

    u8* reply = &MPClientReplies[(client-1)*1024];
    framelen = *(u16*)&reply[10];

    txrate = reply[8];

    // TODO: what are the maximum crop values?
    u16 framectl = *(u16*)&reply[12];
    if (framectl & (1<<14))
    {
        if (!rx_nocrop())
        {
            framelen -= (IOPORT(W_RXLenCrop) >> 7) & 0x1FE;
            if (framelen > 24) memmove(&RXBuffer[12+24], &RXBuffer[12+28], framelen);
        }
    }
    else if (!rx_nocrop())
        framelen -= (IOPORT(W_RXLenCrop) << 1) & 0x1FE;

    if (framelen < 0) framelen = 0;

    // TODO rework RX system so we don't need this (by reading directly into MPClientReplies)
    memcpy(RXBuffer, reply, 12+framelen);

    /* Order matters, and getting it wrong cost every reply the host received a
       frame and a half of emulated time. `StartRX` reads the frame length as a
       *16-bit* value at `RXBuffer[8]`, so byte 9 is the high half of it; the
       channel-transparency stamp below is a single byte at 9. Writing the
       stamp last - as this did until v0.1.8p - makes the length
       `(channel << 8) | len`: 3354 instead of 26 for a reply on channel 13,
       and `RXTime` (length * 4 at the 2Mbps reply rate) 13.4ms instead of
       208us, with `ComStatus` held busy for the whole of it so the next frame
       cannot start. `CheckRX` writes its stamp before this same length field,
       so only the host's reply path was affected. */
    RXBuffer[9] = (u8)CurChannel;
    *(u16*)&RXBuffer[6] = txrate;
    *(u16*)&RXBuffer[8] = framelen;

    /* Whatever a probe has to say about the body this console's game is about
       to read (see MPProbeBodyHook). */
    MPProbeBodyHook(framelen);

    /* What the host's own game is about to read, as opposed to what the
       client said it sent (its `[mp] answered cmd` line) and what the reply
       queue handed over (RecvReplies' `collected a reply` line). Three views
       of one answer: a disagreement here is the transport's doing, agreement
       means the games themselves are the ones not agreeing. */
    if (mp_trace())
    {
        char body[3 * 16 + 1] = "-";
        const int bodylen = framelen - 24;
        if (bodylen > 0)
        {
            const int n = (bodylen > 16) ? 16 : bodylen;
            for (int i = 0; i < n; i++)
                snprintf(&body[i * 3], 4, "%02X ", RXBuffer[12 + 24 + i]);
            body[n * 3 - 1] = 0;
        }

        Log(LogLevel::Debug,
            "[mp] handing client %d's reply to the console's game: %d byte frame, "
            "fc=%04X, seq=%04X, body[%d]=%s%s\n",
            client, framelen, *(u16*)&RXBuffer[12],
            (framelen >= 24) ? *(u16*)&RXBuffer[12 + 22] : 0, bodylen, body,
            (framelen <= 24) ? " - no body at all, so the game sees an empty answer" : "");
    }

    if (mp_trace())
        Log(LogLevel::Debug,
            "[mp] reply handoff check: local framelen=%d, RXBuffer[8]=%04X, RXBuffer[10]=%04X, "
            "reply[8]=%02X reply[10]=%04X\n",
            framelen, *(u16*)&RXBuffer[8], *(u16*)&RXBuffer[10], reply[8], *(u16*)&reply[10]);

    RXTimestamp = 0;
    MPRXSite = 1;
    StartRX(0);
}

bool Wifi::CheckRX(int type) // 0=regular 1=MP replies 2=MP host frames
{
    if (IOPORT(W_PowerState) & (1<<9))
        return false;

    if (!(IOPORT(W_RXCnt) & 0x8000))
        return false;

    if (IOPORT(W_RXBufBegin) == IOPORT(W_RXBufEnd))
        return false;

    int rxlen;
    int framelen;
    u16 framectl;
    u8 txrate, chan;
    u64 timestamp;

    for (;;)
    {
        timestamp = 0;

        if (type == 0)
        {
            rxlen = Platform::MP_RecvPacket(RXBuffer, &timestamp, NDS.UserData);
            if ((rxlen <= 0) && (!IsMP))
                rxlen = WifiAP->RecvPacket(RXBuffer);
        }
        else
        {
            rxlen = Platform::MP_RecvHostPacket(RXBuffer, &timestamp, NDS.UserData);
            if (rxlen < 0)
            {
                // host is gone
                // TODO: make this more resilient
                IsMP = false;
                IsMPClient = false;
                MPArmHeld = false;
                MPMYAID = 0;
            }
        }

        if (rxlen <= 0) return false;
        if (rxlen < 12+24) continue;

        // The local bus is channel-blind (one machine, one radio space): every
        // console receives every frame regardless of what channel the sender
        // was tuned to. Real DS hardware drops frames from other channels, so
        // CheckRX's channel check below would silently swallow peers that
        // picked a different channel - the "host on 6, client scanning on 1"
        // stall. Restamping the frame with OUR channel makes the virtual air
        // channel-transparent: each console hears every frame as if it had
        // been tuned correctly, which is what one shared radio actually means
        // for the games.
        RXBuffer[9] = (u8)CurChannel;

        framelen = *(u16*)&RXBuffer[10];
        if (framelen != rxlen-12)
        {
            Log(LogLevel::Error, "bad frame length %d/%d\n", framelen, rxlen-12);
            continue;
        }

        chan = RXBuffer[9];
        if (chan != CurChannel || CurChannel == 0)
        {
            Log(LogLevel::Debug, "received frame but bad channel %d (expected %d)\n", chan, CurChannel);
            continue;
        }

        // hack: ignore MP frames if not engaged in a MP comm
        if (type == 0 && (!IsMP))
        {
            if (MACEqual(&RXBuffer[12 + 16], MPReplyMAC) ||
                MACEqual(&RXBuffer[12 + 4], MPCmdMAC) ||
                MACEqual(&RXBuffer[12 + 4], MPReplyMAC))
            {
                continue;
            }
        }

        framectl = *(u16*)&RXBuffer[12+0];
        txrate = RXBuffer[8];

        // TODO: what are the maximum crop values?
        if (framectl & (1<<14))
        {
            if (!rx_nocrop())
            {
                framelen -= (IOPORT(W_RXLenCrop) >> 7) & 0x1FE;
                if (framelen > 24) memmove(&RXBuffer[12+24], &RXBuffer[12+28], framelen);
            }
        }
        else if (!rx_nocrop())
            framelen -= (IOPORT(W_RXLenCrop) << 1) & 0x1FE;

        if (framelen < 0) framelen = 0;

        break;
    }

    WIFI_LOG("wifi: received packet FC:%04X SN:%04X CL:%04X RXT:%d CMT:%d\n",
             framectl, *(u16*)&RXBuffer[12+4+6+6+6], *(u16*)&RXBuffer[12+4+6+6+6+2+2], framelen*4, IOPORT(W_CmdReplyTime));

    *(u16*)&RXBuffer[6] = txrate;
    *(u16*)&RXBuffer[8] = framelen;

    u16 frametype = (framectl & 0x00FF);
    bool macgood = (RXBuffer[12 + 4] & 0x01) || MACEqual(&RXBuffer[12 + 4], (u8*)&IOPORT(W_MACAddr0));

    // HACK: when receiving auth/assoc frames, extend the post-beacon interval
    // during MP comm, the host will periodically wake up to send a beacon, and stay awake during the
    // post-beacon interval to see if any clients are trying to connect
    // the auth/assoc procedure would normally fit during that window, but when we are emulating wifi
    // and not yet synced, these frames may lag behind, preventing a successful connection
    if ((frametype == 0x00B0 || frametype == 0x0010 || frametype == 0x0000) && timestamp && macgood)
    {
        if (IOPORT(W_BeaconCount2))
            IOPORT(W_BeaconCount2) += 10;
    }

    if ((frametype == 0x0010) && timestamp && macgood)
    {
        // if receiving an association response: get the sync value from the host

        u16 aid = *(u16*)&RXBuffer[12+24+4];

        if (aid)
        {
            Log(LogLevel::Debug, "[CLIENT %01X] host sync=%016llX\n", aid&0xF, timestamp);

            IsMP = true;
            IsMPClient = true;
            MPD.role = 2;

            /* The AID the host assigned this console. It is remembered here
               because the game's own `W_AIDLow` is not a stable answer to "is
               this poll ours" - MKDS clears it while it prepares the next
               answer, and a poll that lands in that window belongs to this
               console just as much as one that does not. */
            MPMYAID = aid & 0xF;

            /* What the host told this console it is, printed once per
               association: every answer it sends is stamped with this AID and
               every poll is "ours" only when the mask carries its bit. A
               three-console room is the case where the assignment itself can
               go wrong (two guests told the same AID), so the line names both
               the raw field and the bit the guest will test. */
            if (mp_trace())
                Log(LogLevel::Debug,
                    "[mp] association accepted: the host handed this console AID %d (raw %04X); "
                    "it stamps every answer with AID %d and answers a poll only when mask bit %d is set\n",
                    MPMYAID, aid, MPReplyAID(), (int)MPMYAID);

            /* No clock snap here any more. This used to copy the host's
               timestamp into the client's own clock - the one alignment the
               two consoles ever got, and the reason their clocks could part
               afterwards without bound. `USTimestamp` is emulated time now
               (see Wifi::USTimer), so it is already the host's base. */
            NextSync = RXTimestamp + (framelen * (txrate==0x14 ? 4:8));
        }

        RXTimestamp = 0;
        MPRXSite = 2;
        StartRX(timestamp);
    }
    else if ((frametype == 0x00C0) && timestamp && macgood && IsMPClient)
    {
        MPD.deauth_recv++;
        MPD.last_deauth_reason = (framelen >= 26) ? *(u16*)&RXBuffer[12 + 24] : 0;
        MPD.role = 0;

        /* Out of the group: no poll is ours to answer any more. */
        MPArmHeld = false;
        MPMYAID = 0;
        mp_trace_deauth("received", &RXBuffer[12], framelen, USTimestamp);
        if (mp_trace())
            Log(LogLevel::Info,
                "[mp] that deauth takes this console out of multiplayer (IsMPClient -> false), "
                "which is what the client's game-session teardown follows from; if the session "
                "was supposed to carry on into the game, this is the frame that has to be "
                "survived instead\n");

        IsMP = false;
        IsMPClient = false;
        NextSync = 0;

        RXTimestamp = 0;
        MPRXSite = 3;
        StartRX(timestamp);
    }
    else if (macgood && IsMPClient)
    {
        // if we are being a MP client, we need to delay this frame until we reach the
        // timestamp it came with
        // we also need to determine how far we can run after having received this frame

        RXTimestamp = timestamp;
        if (RXTimestamp < USTimestamp) RXTimestamp = USTimestamp;
        MPHoldStamp = timestamp;
        NextSync = RXTimestamp + (framelen * (txrate==0x14 ? 4:8));

        if (MACEqual(&RXBuffer[12 + 4], MPCmdMAC))
        {
            u16 clienttime = *(u16*)&RXBuffer[12+24];
            u16 clientmask = *(u16*)&RXBuffer[12+26];

            /* The two consoles' clocks, side by side. Negative means the frame
               carries a timestamp our own US clock has already passed: the
               client cannot meet the host's schedule, which on real hardware
               the sync frames prevent. */
            /* Whether this round repeats the previous command's sequence
               number. Tested against MPLastSeqno, which FinishRX updates to
               the frame it is processing right now - so this is the *current*
               frame's seqno against the previous frame's, which is exactly
               the retransmit test FinishRX itself makes. */
            const u16 cmdseq = *(u16*)&RXBuffer[12 + 22];
            const bool cmd_dupe_rx = (cmdseq == MPLastSeqno);

            if (mp_trace())
                Log(LogLevel::Debug,
                    "[mp] cmd from host: host clock %llu, my clock %llu (lead %lld), window %d us, "
                    "mask %04X, %s to the console's game\n",
                    (unsigned long long)timestamp, (unsigned long long)USTimestamp,
                    (long long)(timestamp - USTimestamp), clienttime, clientmask,
                    cmd_dupe_rx
                        ? (cmd_retry_notify() ? "the same seqno as the last command, still delivered"
                                              : "the same seqno as the last command, NOT delivered")
                        : "a new command, delivered");

            // include the MP reply time window
            NextSync += 112 + ((clienttime + 10) * NumClients(clientmask));

            /* The guest side of the flight recorder: a command reached this
               console's game, with its body, and whether it repeated the
               previous round's sequence number (see cmd_dupe_rx above). */
            MPD.cmds_rx++;
            if (cmd_dupe_rx) MPD.cmds_rx_dup++;
            MPD.last_cmd_seq = cmdseq;
            MPD.last_cmd_us = USTimestamp;

            /* The other half of the same experiment: the bytes the *guest's*
               game is about to read out of the host's command, available to the
               same body override as a reply. It runs before this console
               records the command, so `last_cmd_body` - and the `mp pN last:
               cmd` digest line - describe the body the game was actually handed
               rather than the one the wire carried. */
            MPProbeBodyHook((int)framelen);

            mp_record_body(MPD.last_cmd_body, &MPD.last_cmd_len, &RXBuffer[12 + 24], (int)framelen - 24);
        }
        else if (MACEqual(&RXBuffer[12 + 4], MPAckMAC))
        {
            u32 runahead = *(u32*)&RXBuffer[0];

            NextSync += runahead;
        }
    }
    else
    {
        // otherwise, just start receiving this frame now

        RXTimestamp = 0;
        MPRXSite = 4;
        StartRX(timestamp);
    }

    return true;
}


void Wifi::MSTimer()
{
    if (IOPORT(W_USCompareCnt))
    {
        if ((USCounter & ~0x3FF) == USCompare)
        {
            BlockBeaconIRQ14 = false;
            SetIRQ14(0);
        }
    }

    if (IOPORT(W_BeaconCount1) != 0)
    {
        IOPORT(W_BeaconCount1)--;
        if (IOPORT(W_BeaconCount1) == 0) SetIRQ14(1);
    }
    if (IOPORT(W_BeaconCount1) == 0)
        IOPORT(W_BeaconCount1) = IOPORT(W_BeaconInterval);

    if (IOPORT(W_BeaconCount2) != 0)
    {
        IOPORT(W_BeaconCount2)--;
        if (IOPORT(W_BeaconCount2) == 0) SetIRQ13();
    }
}

void Wifi::USTimer(u32 param)
{
    /* The wireless clock is the console's emulated time (NDS::GetSharedUSClock),
       not a counter that only advances while the radio is powered: every
       comparison this module makes across consoles goes through it (frame
       timestamps, the reply window, `NextSync`/`RXTimestamp`, the staleness
       test). The two consoles' clocks used to part by up to 254ms of emulated
       time over one MKDS join - the games write VCOUNT, which delays a frame,
       and a frame is what one session step advances - so the clock is now
       derived from the frame count, which is advanced in lockstep, and the two
       agree by construction. The game-visible `USCounter` is untouched. */
    USTimestamp = NDS.GetSharedUSClock();
    ClockTicks++;

    ReseedDownloadPlayLink();

    /* One line a second while tracing: this console's clock against its own
       frame count, which is how the two consoles' clocks are compared (a frame
       is 16,715us of it - see kReplyStaleWindow). The useful reading is that
       the frame count and the clock climb together on both consoles: while
       they did not, the client's clock ran 254ms ahead of its host over one
       MKDS join and stayed there. */
    if (mp_trace() && ((USTimestamp - ClockLastProbe) >= 1000000))
    {
        ClockLastProbe = USTimestamp;
        Log(LogLevel::Debug, "[mp] clock probe: us %llu ticks %llu arm7 %llu frames %u · bus deliveries %llu redelivered %llu\n",
            (unsigned long long)USTimestamp, (unsigned long long)ClockTicks,
            (unsigned long long)NDS.ARM7Timestamp, NDS.NumFrames,
            (unsigned long long)MPBusDeliveries, (unsigned long long)MPRedeliveries);
    }

    /* The host's own round loop, watched. Nothing else in this module can tell
       a host whose game has left the lobby from one whose game is merely idle:
       both read as "no commands this second", and both keep the beacon, the
       RX and the counters alive. The condition is two seconds without a round
       while this console is still the host of a group. Measured at three
       consoles (logs/dc3_app.log.gz, logs/dg3_app.log): the game wrote 60.0
       rounds/s and then exactly zero, a few seconds after the second client
       associated, with every round before the stop credited to both clients,
       both replies handed over and read, and the last ack reporting no failed
       client - so the link is not what ends the lobby, and this line says so
       at the moment it happens instead of leaving it to a forensic pass. */
    if (IsMP && !IsMPClient && MPLoopAlive && MPD.last_cmd_us && ((USTimestamp - MPD.last_cmd_us) > 2000000))
    {
        MPLoopAlive = false;
        MPD.loop_stops++;
        MPD.loop_life_us = MPD.last_cmd_us - MPD.loop_start_us;
        MPD.loop_stop_since_join_us = MPD.last_client_join_us ? (MPD.last_cmd_us - MPD.last_client_join_us) : 0;

        /* Both halves matter and both are counters, so neither is inferred
           from the other: how long the game kept its lobby running, and how
           long after the newest client's association it stopped. The second
           is the one that says whether a client's arrival is what ends it -
           measured at three consoles it is a few seconds, in a lobby that was
           otherwise healthy to the last round (`clientfail` 0). */
        if (MPD.last_client_join_us)
            Log(LogLevel::Info,
                "[mp] this console's own game has stopped sending commands: last command seq %04X at +%llu.%03us, "
                "%llu ms after the newest client associated, after %llu rounds (%llu addressing 2+ clients) over "
                "%llu.%03us. The link is not what ended it: %llu acks, %llu of them reporting a failed client, and the "
                "beacon slot is %s.\n",
                MPD.last_cmd_seq,
                (unsigned long long)(MPD.last_cmd_us / 1000000), (unsigned long long)((MPD.last_cmd_us / 1000) % 1000),
                (unsigned long long)(MPD.loop_stop_since_join_us / 1000),
                (unsigned long long)MPD.cmds_tx, (unsigned long long)MPD.rounds_multi,
                (unsigned long long)(MPD.loop_life_us / 1000000), (unsigned long long)((MPD.loop_life_us / 1000) % 1000),
                (unsigned long long)MPD.acks_sent, (unsigned long long)MPD.acks_client_fail,
                (IOPORT(W_TXSlotBeacon) & 0x8000) ? "still armed, so the firmware goes on beaconing the game's last frame" : "clear");
        else
            Log(LogLevel::Info,
                "[mp] this console's own game has stopped sending commands: last command seq %04X at +%llu.%03us, with no "
                "client ever associated this session (the session's join counter is %llu). The link is not what ended it: "
                "%llu acks, %llu of them reporting a failed client, and the beacon slot is %s.\n",
                MPD.last_cmd_seq,
                (unsigned long long)(MPD.last_cmd_us / 1000000), (unsigned long long)((MPD.last_cmd_us / 1000) % 1000),
                (unsigned long long)MPD.client_joins,
                (unsigned long long)MPD.acks_sent, (unsigned long long)MPD.acks_client_fail,
                (IOPORT(W_TXSlotBeacon) & 0x8000) ? "still armed, so the firmware goes on beaconing the game's last frame" : "clear");
    }

    if (IsMPClient && (!ComStatus))
    {
        if (RXTimestamp && (USTimestamp >= RXTimestamp))
        {
            RXTimestamp = 0;
            MPRXSite = 5;
            const u64 held = MPHoldStamp;
            StartRX(held);
            /* The reference arm: one deliberate second hand-over of the frame
               just delivered (see mp_redeliver_probe). */
            if (held && mp_redeliver_probe())
                StartRX(held);
        }

        if (USTimestamp >= NextSync)
        {
            // TODO: not do this every tick if it fails to receive a frame!
            CheckRX(2);
        }
    }

    if (!(USTimestamp & 0x3FF & kTimeCheckMask))
        WifiAP->MSTimer();

    if (USUntilPowerOn < 0)
    {
        USUntilPowerOn += kTimerInterval;

        if (USUntilPowerOn >= 0)
        {
            USUntilPowerOn = 0;

            IOPORT(W_PowerState) = 0;
            SetStatus(1);

            UpdatePowerStatus(0);
        }
    }

    if (IOPORT(W_USCountCnt))
    {
        USCounter += kTimerInterval;
        u32 uspart = (USCounter & 0x3FF);

        if (IOPORT(W_USCompareCnt))
        {
            u32 beaconus = (IOPORT(W_BeaconCount1) << 10) | (0x3FF - uspart);
            if ((beaconus & kTimeCheckMask) == (IOPORT(W_PreBeacon) & kTimeCheckMask))
                SetIRQ15();
        }

        if (!(uspart & kTimeCheckMask))
            MSTimer();
    }

    if (IOPORT(W_CmdCountCnt) & 0x0001)
    {
        if (CmdCounter > 0)
        {
            if (CmdCounter < kTimerInterval)
                CmdCounter = 0;
            else
                CmdCounter -= kTimerInterval;
        }
    }

    if (IOPORT(W_ContentFree) != 0)
    {
        if (IOPORT(W_ContentFree) < kTimerInterval)
            IOPORT(W_ContentFree) = 0;
        else
            IOPORT(W_ContentFree) -= kTimerInterval;
    }

    if (ComStatus == 0)
    {
        u16 txbusy = IOPORT(W_TXBusy);
        if (txbusy)
        {
            if (IOPORT(W_PowerState) & (1<<9))
            {
                ComStatus = 0;
                TXCurSlot = -1;
            }
            else
            {
                ComStatus = 0x2;
                if      (txbusy & 0x0080) TXCurSlot = 5;
                else if (txbusy & 0x0010) TXCurSlot = 4;
                else if (txbusy & 0x0008) TXCurSlot = 3;
                else if (txbusy & 0x0004) TXCurSlot = 2;
                else if (txbusy & 0x0002) TXCurSlot = 1;
                else if (txbusy & 0x0001) TXCurSlot = 0;
            }
        }
        else
        {
            if ((!IsMPClient) || (USTimestamp > NextSync))
            {
                if ((!(RXCounter & 0x1FF & kTimeCheckMask)) && (!ComStatus))
                {
                    CheckRX(0);
                }
            }

            RXCounter += kTimerInterval;
        }
    }

    if (ComStatus & 0x2)
    {
        bool finished = ProcessTX(&TXSlots[TXCurSlot], TXCurSlot);
        if (finished)
        {
            if (IOPORT(W_PowerState) & (1<<9))
            {
                IOPORT(W_TXBusy) = 0;
                IOPORT(W_TRXPower) = 0;
                SetStatus(9);
            }

            // transfer finished, see if there's another slot to do
            // checkme: priority order of beacon/reply
            // TODO: for CMD, check CMDCOUNT
            u16 txbusy = IOPORT(W_TXBusy);
            if      (txbusy & 0x0080) TXCurSlot = 5;
            else if (txbusy & 0x0010) TXCurSlot = 4;
            else if (txbusy & 0x0008) TXCurSlot = 3;
            else if (txbusy & 0x0004) TXCurSlot = 2;
            else if (txbusy & 0x0002) TXCurSlot = 1;
            else if (txbusy & 0x0001) TXCurSlot = 0;
            else
            {
                TXCurSlot = -1;
                ComStatus = 0;
                RXCounter = 0;
            }
        }
    }
    if (ComStatus & 0x1)
    {
        RXTime -= kTimerInterval;
        if (!(RXTime & RXHalfwordTimeMask))
        {
            u16 addr = IOPORT(W_RXTXAddr) << 1;
            if (addr < 0x1FFF) *(u16*)&RAM[addr] = *(u16*)&RXBuffer[RXBufferPtr];

            IncrementRXAddr(addr);
            IOPORT(W_RXTXAddr) = addr >> 1;
            RXBufferPtr += 2;

            if (RXTime <= 0) // finished receiving
            {
                FinishRX();
            }
            else if (addr == (IOPORT(W_RXBufReadCursor) << 1))
            {
                // TODO: properly check the crossing of the read cursor
                // (for example, if it is outside of the RX buffer)

                Log(LogLevel::Debug, "wifi: RX buffer full (buf=%04X/%04X rd=%04X wr=%04X rxtx=%04X power=%04X com=%d rxcnt=%04X filter=%04X/%04X frame=%04X/%04X len=%d)\n",
                       (IOPORT(W_RXBufBegin)>>1)&0xFFF, (IOPORT(W_RXBufEnd)>>1)&0xFFF,
                       IOPORT(W_RXBufReadCursor), IOPORT(W_RXBufWriteCursor),
                       IOPORT(W_RXTXAddr), IOPORT(W_PowerState), ComStatus,
                       IOPORT(W_RXCnt), IOPORT(W_RXFilter), IOPORT(W_RXFilter2),
                       *(u16*)&RXBuffer[0], *(u16*)&RXBuffer[12], *(u16*)&RXBuffer[8]);
                RXTime = 0;
                SetStatus(1);
                if (TXCurSlot == 0xFFFFFFFF)
                {
                    ComStatus &= ~0x1;
                    RXCounter = 0;
                }
                // TODO: proper error management
                if ((!ComStatus) && (IOPORT(W_PowerState) & (1<<9)))
                {
                    IOPORT(W_TRXPower) = 0;
                    SetStatus(9);
                }
            }
        }
    }

    ScheduleTimer(false);
}


void Wifi::ChangeChannel()
{
    u32 val1 = RFRegs[RFChannelIndex[0]];
    u32 val2 = RFRegs[RFChannelIndex[1]];

    CurChannel = 0;

    for (int i = 0; i < 14; i++)
    {
        if (val1 == RFChannelData[i][0] && val2 == RFChannelData[i][1])
        {
            CurChannel = i+1;
            break;
        }
    }

    if (CurChannel > 0)
        Log(LogLevel::Debug, "wifi: switching to channel %d\n", CurChannel);
    else if (HeldChannel > 0)
    {
        /* The registers name no channel, but this console has one to keep
           (a booted Download Play child - see SetDownloadPlaySession). */
        CurChannel = HeldChannel;
        Log(LogLevel::Debug,
            "wifi: rf registers %05X:%05X name no channel; holding channel %d\n",
            val1, val2, CurChannel);
    }
    else
        Log(LogLevel::Debug, "wifi: invalid channel values %05X:%05X\n", val1, val2);
}

void Wifi::SetDownloadPlaySession(int channel, const u8* hostbssid, int aid)
{
    HeldChannel = ((channel > 0) && (channel < 15)) ? channel : 0;
    ChangeChannel();

    if (hostbssid)
        memcpy(HeldBSSID, hostbssid, 6);
    else
        memset(HeldBSSID, 0, 6);

    HeldAID = ((aid >= 1) && (aid <= 15)) ? aid : 0;
    DpSessionFrame = NDS.NumFrames;
    DpReseeded = false;
    DpReseedLogged = false;

    Log(LogLevel::Info,
        "wifi: download-play session carried across the boot: channel %d, "
        "bssid %02X:%02X:%02X:%02X:%02X:%02X, aid %d\n",
        CurChannel, HeldBSSID[0], HeldBSSID[1], HeldBSSID[2],
        HeldBSSID[3], HeldBSSID[4], HeldBSSID[5], HeldAID);

    /* The association half. These are exactly the fields the core sets when a
       client takes a host's association response (see CheckRX, frametype
       0010): `IsMP`/`IsMPClient` are what let MP frames through the
       `!IsMP` filter and route polls to this console at all, and `MPMYAID` is
       the AID a reply is stamped with and the value that decides a poll is
       ours. A real client has them by the time it jumps into the downloaded
       image; seeding them here is what makes the hand-over keep the session
       instead of dropping it. Without an AID there is no association to carry,
       so the hold stays a bare channel. */
    if (HeldAID)
    {
        IsMP = true;
        IsMPClient = true;
        MPD.role = 2;
        MPMYAID = HeldAID;

        IOPORT(W_BSSID0) = HeldBSSID[0] | (HeldBSSID[1] << 8);
        IOPORT(W_BSSID1) = HeldBSSID[2] | (HeldBSSID[3] << 8);
        IOPORT(W_BSSID2) = HeldBSSID[4] | (HeldBSSID[5] << 8);

        Log(LogLevel::Info,
            "wifi: download-play association carried across the boot: this console "
            "comes up an MP client of %02X:%02X:%02X:%02X:%02X:%02X as AID %d\n",
            HeldBSSID[0], HeldBSSID[1], HeldBSSID[2],
            HeldBSSID[3], HeldBSSID[4], HeldBSSID[5], MPMYAID);
    }
}

/* Keep the hand-off present past the child's own zeroing, until the game
   configures its own window.

   Measured live (to-do.md "SFC guest park", 2026-10-13): the child's init
   zeroes the BSSID and RX-window registers SetDownloadPlaySession/Link
   seeded, runs its RF/BB prefix, then stalls with the radio down
   (PowerState 0x0200) and never touches a wifi register again - its last
   access is ~40 frames past the hand-off, so a delayed one-shot lands too
   late (measured: the park is fully formed by then). Everything re-applied
   here is a fact the hand-off already established (same values), restored
   through the same paths (Write(), so the power model transitions for
   real) - and a set window disarms this for good, so a child that completes
   its own bring-up is never touched. Deliberately NOT carried: W_AIDLow
   (never ours - the association path sets model state, the game owns the
   register) and anything the bring-up owns (mode, channel tune, filters). */
void Wifi::ReseedDownloadPlayLink()
{
    if (DpReseeded || !HeldAID || !DpSessionFrame) return;
    /* A window past the bring-up means the game finished it - clobbering its
       window, BSSID or power state would break a working child, so in that
       case disarm and leave it alone. But the hand-off's own window is still
       present for the first frames after the boot, so disarming on any window
       disarms before the game's zeroing (measured: it disarmed at once and
       the park formed anyway) - only disarm once the bring-up had time.
       Zero past that means the stall this exists for: the game zeroed the
       hand-off's window during its own init and never configured its own.
       This runs every frame (not once past a delay) because the park forms
       ~390 frames after the hand-off while a delayed shot lands too late. */
    const u32 nowframe = NDS.NumFrames;
    if (IOPORT(W_RXBufEnd) != 0)
    {
        if (nowframe > DpSessionFrame + 600)
            DpReseeded = true;
        return;
    }
    /* The game is mid-bring-up while it keeps touching the radio: forcing
       power state under an active sequence would fight its own writes (it
       reads back what it wrote), so only fill the silence. Our own Writes
       below log into the same ring, so the first apply self-suppresses the
       rest; a game that resumes writing re-opens the gate only while the
       window still reads zero. */
    if (MPIO.reg_ring_total > 0)
    {
        const u32 lastacc =
            MPIO.reg_ring[(u32)((MPIO.reg_ring_total - 1) % (u64)MPIo::kRegRing)].frame;
        if ((nowframe - lastacc) < 120) return;
    }

    IOPORT(W_BSSID0) = HeldBSSID[0] | (HeldBSSID[1] << 8);
    IOPORT(W_BSSID1) = HeldBSSID[2] | (HeldBSSID[3] << 8);
    IOPORT(W_BSSID2) = HeldBSSID[4] | (HeldBSSID[5] << 8);

    IOPORT(W_RXBufBegin) = 0x4000;
    IOPORT(W_RXBufEnd) = 0x4800;
    IOPORT(W_RXBufWriteCursor) = 0;
    IOPORT(W_RXBufWriteAddr) = 0;
    IOPORT(W_RXBufReadAddr) = 0;
    IOPORT(W_RXBufReadCursor) = 0;
    IOPORT(W_RXBufCount) = 0;
    IOPORT(W_RXCnt) |= 0x8000;

    /* Same power-domain restore as the boot prefix below: the child's init
       may have cleared POWCNT2's wifi bit after the hand-off, and without it
       UpdatePowerOn computes off no matter what the prefix writes. */
    SetPowerCnt(0x0002);

    Write(W_PowerDownCtrl, 0x0003);
    Write(W_ModeReset, 0x0001);
    Write(W_PowerTX, 0x0001);
    Write(W_PowerUS, 0x0000);

    if (!DpReseedLogged)
    {
        DpReseedLogged = true;
        Log(LogLevel::Info,
            "wifi: download-play link re-seeded %u frames after the hand-off: "
            "bssid/carried session restored, radio re-attached\n",
            NDS.NumFrames - DpSessionFrame);
    }
}

void Wifi::SetDownloadPlayLink()
{
    /* The window the powered radio latches (see the ModeReset 0x4000 arm for
       the hardware defaults): Reset() left it empty and the driver's own
       self-test writes only zeros, so without this the first receive has
       nowhere to land. */
    IOPORT(W_RXBufBegin) = 0x4000;
    IOPORT(W_RXBufEnd) = 0x4800;
    IOPORT(W_RXBufWriteCursor) = 0;
    IOPORT(W_RXBufWriteAddr) = 0;
    IOPORT(W_RXBufReadAddr) = 0;
    IOPORT(W_RXBufReadCursor) = 0;
    IOPORT(W_RXBufCount) = 0;

    /* Direct store, not Write(): this is the enable bit CheckRX and the two
       sibling RX paths test, and Write()'s bit-15 arm fires a TX. */
    IOPORT(W_RXCnt) |= 0x8000;

    /* The wifi power-domain bit is NDS-owned (POWCNT2); SetupDownloadPlayBoot
       already enabled it above, so this is a no-op restating the boot state.
       (The re-seed carries its own copy of this restore for the post-boot
       case, where the child's init may have cleared the bit.) */
    SetPowerCnt(0x0002);

    /* The transceiver-on prefix, in the order the host's own driver ran it
       live: PowerDownCtrl bit 1 arms the wakeup, the ModeReset 0->1 edge
       recomputes with it set (reqflags 3 - TRXPower up, PowerState bit 8,
       the -2048us power-on delay, IRQ11), PowerTX 1 is the state the prefix
       leaves behind. */
    Write(W_PowerDownCtrl, 0x0003);
    Write(W_ModeReset, 0x0001);
    Write(W_PowerTX, 0x0001);

    /* Attach to the bus and start the wifi event (needs the wifi power
       domain enabled - see SetupDownloadPlayBoot): the event's ticks are
       what complete the power-on delay above and turn PowerState 0, and the
       attach is what lets the host's frames reach CheckRX at all. A later
       PowerUS 0 from the driver's own bring-up is a no-op (on == PowerOn),
       and so is its ModeReset 1 (no 0->1 edge, no recompute). */
    Write(W_PowerUS, 0x0000);
}

void Wifi::RFTransfer_Type2()
{
    RFBusyUntil = NDS.GetSharedUSClock() + kRFTransferUS;

    u32 id = (IOPORT(W_RFData2) >> 2) & 0x1F;

    if (IOPORT(W_RFData2) & 0x0080)
    {
        u32 data = RFRegs[id];
        IOPORT(W_RFData1) = data & 0xFFFF;
        IOPORT(W_RFData2) = (IOPORT(W_RFData2) & 0xFFFC) | ((data >> 16) & 0x3);
    }
    else
    {
        u32 data = IOPORT(W_RFData1) | ((IOPORT(W_RFData2) & 0x0003) << 16);
        RFRegs[id] = data;

        if (id == RFChannelIndex[0] || id == RFChannelIndex[1])
            ChangeChannel();
    }
}

void Wifi::RFTransfer_Type3()
{
    RFBusyUntil = NDS.GetSharedUSClock() + kRFTransferUS;

    u32 id = (IOPORT(W_RFData1) >> 8) & 0x3F;

    u32 cmd = IOPORT(W_RFData2) & 0xF;
    Log(LogLevel::Debug, "rfw: cmd=%X id=%02X data=%02X chan=%d\n",
        cmd, id, IOPORT(W_RFData1) & 0xFF, CurChannel);
    if (cmd == 6)
    {
        IOPORT(W_RFData1) = (IOPORT(W_RFData1) & 0xFF00) | (RFRegs[id] & 0xFF);
    }
    else if (cmd == 5)
    {
        u32 data = IOPORT(W_RFData1) & 0xFF;
        RFRegs[id] = data;

        if (id == RFChannelIndex[0] || id == RFChannelIndex[1])
            ChangeChannel();
    }
}


u16 Wifi::Read(u32 addr)
{
    /* Every read the console's own software makes of the wireless block goes
       through here, so the recorder below sees the value the game saw rather
       than the value the emulator meant to store - the two differ exactly
       where a fabricated value reaches a game's bytecode, which is the whole
       question the v0.1.9h read left open. */
    const u16 val = ReadRaw(addr);
    RecordMPIoRead(addr, val);
    return val;
}

void Wifi::RecordRegAccess(u16 addr, u16 val, bool write)
{
    /* The register window's own ring, and the one place it is written: a
       driver's bring-up gate is an instruction that read (or wrote) one of
       these halfwords, so the PC that did it is the answer to "which value is
       this console's power-up path blocked on". One entry per access, so the
       accessor's PC/LR is exact; bounded, and the frame count ties an entry to
       the boot boundary. */
    const u32 i = (addr >> 1) & 0x7FF;
    if (write)
    {
        MPIO.regw_pc[i] = NDS.ARM7.R[15];
        MPIO.regw_lr[i] = NDS.ARM7.R[14];
        MPIO.regw_val[i] = val;
    }
    else
    {
        MPIO.regr_pc[i] = NDS.ARM7.R[15];
        MPIO.regr_lr[i] = NDS.ARM7.R[14];
    }

    const u32 r = (u32)(MPIO.reg_ring_total % (u64)MPIo::kRegRing);
    MPIO.reg_ring[r].off = addr;
    MPIO.reg_ring[r].val = val;
    MPIO.reg_ring[r].wr = write ? 1 : 0;
    MPIO.reg_ring[r].pc = NDS.ARM7.R[15];
    MPIO.reg_ring[r].lr = NDS.ARM7.R[14];
    MPIO.reg_ring[r].frame = NDS.NumFrames;
    MPIO.reg_ring_total++;
}

void Wifi::RecordMPIoRead(u32 addr, u16 val)
{
    addr &= 0x7FFE;

    if (addr < 0x1000)
    {
        /* The register window. Counted per halfword offset with the last value
           read, and kept in order for the last kRing reads so a round can be
           read back as the sequence of values the game actually consumed. The
           same access also takes a slot in the PC-tagged ring below. */
        const u32 i = (addr >> 1) & 0x7FF;
        MPIO.reg_reads[i]++;
        MPIO.reg_last[i] = val;

        const u32 r = (u32)(MPIO.ring_total % (u64)MPIo::kRing);
        MPIO.ring_off[r] = (u16)addr;
        MPIO.ring_val[r] = val;
        MPIO.ring_total++;

        RecordRegAccess((u16)addr, val, false);
        return;
    }

    /* The wireless RAM window: every read of it is kept, in order, with the
       value it returned. Only the last kRing are retained (the window is
       where the game's mail arrives and it reads tens of thousands of times a
       session), and the zone brackets below say which of them fell inside the
       frame the console was just handed. */
    if (addr >= 0x4000 && addr < 0x6000)
    {
        const u16 off = (u16)(addr & 0x1FFE);
        /* Whether this read fell inside the frame the console was handed is
           decided now, not at print time: the bracket is replaced by the next
           delivery, so a question asked later would answer for the wrong
           frame. */
        const bool inside = MPIO.zone_len &&
            (u16)((off - MPIO.zone_start) & 0x1FFE) < (u16)MPIO.zone_len;
        {
            const u32 r = (u32)(MPIO.rx_total % (u64)MPIo::kRing);
            MPIO.rx_off[r] = off;
            MPIO.rx_val[r] = val;
            MPIO.rx_in[r] = inside ? 1 : 0;
            MPIO.rx_total++;
        }

        if (inside)
        {
            MPIO.zone_total++;
            if (MPIO.zone_n < (u32)MPIo::kZone)
            {
                MPIO.zone_off[MPIO.zone_n] = off;
                MPIO.zone_val[MPIO.zone_n] = val;
                MPIO.zone_n++;
            }

            /* The game taking the command it was handed out of the buffer,
               header and payload alike. This is the read that separates "the
               guest's game looked at the transition and stayed where it was"
               from "the frame never reached the game's hands at all", and it
               is the first few of them that matter, so it is throttled hard
               while the recorder keeps all of them.

               The value the read returned is printed with the offset since
               v0.1.9ex: the offset alone says the game looked inside the frame
               the recorder bracketed, but not that what it took was the header
               this tree wrote (`801C 0040 .. TX rate, length, 4080`). With the
               value on the line, a storm can be checked field by field against
               a win from the log, which is the question `stall-report.py`
               answers and the one a live MPIo dump (`appctl.py wifiio`) can only
               answer for the delivery still in the buffer. */
            if (mp_trace() && MPIO.zone_is_cmd &&
                (MPIO.zone_total <= 6 || (MPIO.zone_total % 64) == 0))
                Log(LogLevel::Debug,
                    "[mp] the console's game reads the command it was handed: cmd seq=%04X, "
                    "%llu halfwords read so far, this one at %04X=%04X of the %u byte region "
                    "from %04X\n",
                    MPIO.zone_seq, (unsigned long long)MPIO.zone_total, off, val,
                    (unsigned)MPIO.zone_len, (unsigned)MPIO.zone_start);
        }
    }
}

void Wifi::RecordMPIoWrite(u32 addr, u16 val)
{
    /* The register window gets the same PC-tagged ring as the read side. A
       power-up gate can be on a value the driver wrote and expects to read
       back (the standard radio bring-up is "write a config, read a status"),
       so the write's PC is as load-bearing as the read's. This is the one
       place a register write is recorded at all - the power-register trace
       above keeps only the registers that decide transceiver state, and only
       when their value changes. */
    if (addr < 0x1000)
    {
        RecordRegAccess((u16)addr, val, true);
        /* Deliberately no `return`: the TX-slot arms below are register-window
           writes too and keep their own (full register file) record. */
    }

    /* Only the low wireless RAM matters here: a Download Play host composes
       its command frame at the slot's own offset, and `W_TXSlotCmd` reads
       0x8000 in that state, i.e. offset 0. The window above 0x40 is the scan
       table and the receive buffer, written by the game but not part of the
       frame it is about to send, so it is left out; the arms are kept
       separately below. */
    if (addr >= 0x4000 && addr < 0x6000)
    {
        const u16 off = (u16)(addr & 0x1FFE);
        if (off < (u16)MPIo::kWrKeep)
        {
            const u32 pc = NDS.ARM7.R[15];
            const u32 lr = NDS.ARM7.R[14];
            const u32 r = (u32)(MPIO.wr_total % (u64)MPIo::kWrRing);
            MPIO.wr[r].pc = pc;
            MPIO.wr[r].lr = lr;
            MPIO.wr[r].off = off;
            MPIO.wr[r].val = val;
            MPIO.wr_total++;

            MPIO.wr_pc[off] = pc;
            MPIO.wr_lr[off] = lr;
            MPIO.wr_val[off] = val;
            MPIO.wr_n[off]++;
        }
        return;
    }

    if (addr == W_TXSlotCmd)
    {
        MPIO.wr_cmd_pc = NDS.ARM7.R[15];
        MPIO.wr_cmd_lr = NDS.ARM7.R[14];
        MPIO.wr_cmd_val = val;
        MPIO.wr_cmd_n++;
        for (int i = 0; i < 16; i++) MPIO.wr_cmd_regs[i] = NDS.ARM7.R[i];
        MPIO.wr_cmd_cpsr = NDS.ARM7.CPSR;
    }
    else if (addr == W_TXSlotReply1)
    {
        MPIO.wr_reply_pc = NDS.ARM7.R[15];
        MPIO.wr_reply_lr = NDS.ARM7.R[14];
        MPIO.wr_reply_val = val;
        MPIO.wr_reply_n++;
    }
}

void Wifi::SetMPIoZone(u16 start, u16 len, u16 fc, u16 seq, bool is_cmd)
{
    /* A new frame replaces the last one and starts a fresh consumption list:
       what the game took out of the frame *it was just handed* is the unit the
       question is asked in, and the report is taken on demand, so in a steady
       freeze the frame on show is the one being exchanged right now. */
    MPIO.zone_start = start;
    MPIO.zone_len = len;
    MPIO.zone_fc = fc;
    MPIO.zone_seq = seq;
    MPIO.zone_is_cmd = is_cmd ? 1 : 0;
    MPIO.zone_n = 0;
    MPIO.zone_total = 0;
    MPIO.zone_gen++;
}

static const char* mpio_reg_name(u32 off)
{
    switch (off)
    {
    case 0x000: return "W_ID";
    case 0x004: return "W_ModeReset";
    case 0x006: return "W_ModeWEP";
    case 0x008: return "W_TXStatCnt";
    case 0x010: return "W_IF";
    case 0x012: return "W_IE";
    case 0x018: return "W_MACAddr0";
    case 0x01A: return "W_MACAddr1";
    case 0x01C: return "W_MACAddr2";
    case 0x020: return "W_BSSID0";
    case 0x022: return "W_BSSID1";
    case 0x024: return "W_BSSID2";
    case 0x028: return "W_AIDLow";
    case 0x02A: return "W_AIDFull";
    case 0x02C: return "W_TXRetryLimit";
    case 0x030: return "W_RXCnt";
    case 0x032: return "W_WEPCnt";
    case 0x034: return "W_TRXPower";
    case 0x036: return "W_PowerUS";
    case 0x038: return "W_PowerTX";
    case 0x03C: return "W_PowerState";
    case 0x040: return "W_PowerForce";
    case 0x044: return "W_Random";
    case 0x048: return "W_PowerDownCtrl";
    case 0x050: return "W_RXBufBegin";
    case 0x052: return "W_RXBufEnd";
    case 0x054: return "W_RXBufWriteCursor";
    case 0x056: return "W_RXBufWriteAddr";
    case 0x058: return "W_RXBufReadAddr";
    case 0x05A: return "W_RXBufReadCursor";
    case 0x05C: return "W_RXBufCount";
    case 0x060: return "W_RXBufDataRead";
    case 0x062: return "W_RXBufGapAddr";
    case 0x064: return "W_RXBufGapSize";
    case 0x068: return "W_TXBufWriteAddr";
    case 0x06C: return "W_TXBufCount";
    case 0x070: return "W_TXBufDataWrite";
    case 0x074: return "W_TXBufGapAddr";
    case 0x076: return "W_TXBufGapSize";
    case 0x080: return "W_TXSlotBeacon";
    case 0x084: return "W_TXBeaconTIM";
    case 0x088: return "W_ListenCount";
    case 0x08C: return "W_BeaconInterval";
    case 0x08E: return "W_ListenInterval";
    case 0x090: return "W_TXSlotCmd";
    case 0x094: return "W_TXSlotReply1";
    case 0x098: return "W_TXSlotReply2";
    case 0x0A0: return "W_TXSlotLoc1";
    case 0x0A4: return "W_TXSlotLoc2";
    case 0x0A8: return "W_TXSlotLoc3";
    case 0x0AC: return "W_TXReqReset";
    case 0x0AE: return "W_TXReqSet";
    case 0x0B0: return "W_TXReqRead";
    case 0x0B4: return "W_TXSlotReset";
    case 0x0B6: return "W_TXBusy";
    case 0x0B8: return "W_TXStat";
    case 0x0BC: return "W_Preamble";
    case 0x0C0: return "W_CmdTotalTime";
    case 0x0C4: return "W_CmdReplyTime";
    case 0x0D0: return "W_RXFilter";
    case 0x0DA: return "W_RXLenCrop";
    case 0x0E0: return "W_RXFilter2";
    case 0x0E8: return "W_USCountCnt";
    case 0x0EA: return "W_USCompareCnt";
    case 0x0EE: return "W_CmdCountCnt";
    case 0x0F0: return "W_USCompare0";
    case 0x0F2: return "W_USCompare1";
    case 0x0F4: return "W_USCompare2";
    case 0x0F6: return "W_USCompare3";
    case 0x0F8: return "W_USCount0";
    case 0x0FA: return "W_USCount1";
    case 0x0FC: return "W_USCount2";
    case 0x0FE: return "W_USCount3";
    case 0x10C: return "W_ContentFree";
    case 0x110: return "W_PreBeacon";
    case 0x118: return "W_CmdCount";
    case 0x11C: return "W_BeaconCount1";
    case 0x134: return "W_BeaconCount2";
    case 0x158: return "W_BBCnt";
    case 0x15A: return "W_BBWrite";
    case 0x15C: return "W_BBRead";
    case 0x15E: return "W_BBBusy";
    case 0x160: return "W_BBMode";
    case 0x168: return "W_BBPower";
    case 0x17C: return "W_RFData2";
    case 0x17E: return "W_RFData1";
    case 0x180: return "W_RFBusy";
    case 0x184: return "W_RFCnt";
    case 0x194: return "W_TXHeaderCnt";
    case 0x19C: return "W_RFPins";
    case 0x1A8: return "W_RXStatIncIF";
    case 0x1AA: return "W_RXStatIncIE";
    case 0x1AC: return "W_RXStatHalfIF";
    case 0x1AE: return "W_RXStatHalfIE";
    case 0x1C0: return "W_TXErrorCount";
    case 0x1C4: return "W_RXCount";
    case 0x1D0: return "W_CMDStat0";
    case 0x1D2: return "W_CMDStat1";
    case 0x1D4: return "W_CMDStat2";
    case 0x1D6: return "W_CMDStat3";
    case 0x1D8: return "W_CMDStat4";
    case 0x1DA: return "W_CMDStat5";
    case 0x1DC: return "W_CMDStat6";
    case 0x1DE: return "W_CMDStat7";
    case 0x210: return "W_TXSeqNo";
    case 0x214: return "W_RFStatus";
    case 0x21C: return "W_IFSet";
    case 0x268: return "W_RXTXAddr";
    default: return "?";
    }
}

int Wifi::FormatMPIoReport(char* out, size_t cap) const
{
    if (!out || cap < 256) return -1;

    int n = 0;
    const auto put = [&](const char* fmt, ...) {
        if ((size_t)n >= cap - 1) return;
        va_list ap;
        va_start(ap, fmt);
        const int w = vsnprintf(out + n, cap - (size_t)n, fmt, ap);
        va_end(ap);
        if (w <= 0) return;
        n += w;
        if ((size_t)n > cap - 1) n = (int)cap - 1;
    };

    put("mpio: role=%d%s%s  USCOUNT=%llX USCOMPARE=%llX  zone gen=%llu\n",
        MPD.role, IsMP ? " (host)" : "", IsMPClient ? " (client)" : "",
        (unsigned long long)USCounter, (unsigned long long)USCompare,
        (unsigned long long)MPIO.zone_gen);

    /* The frame this console was handed, what it contains now, and every
       halfword the game has taken out of it in the order it took them - the
       only form in which "what value did it branch on" is answerable. */
    put("  handed: start=%04X len=%u fc=%04X seq=%04X%s  consumed=%llu halfwords\n",
        MPIO.zone_start, MPIO.zone_len, MPIO.zone_fc, MPIO.zone_seq,
        MPIO.zone_is_cmd ? " (a command)" : " (not a command)",
        (unsigned long long)MPIO.zone_total);

    if (MPIO.zone_len)
    {
        put("  as it stands in RAM (the 12-byte RX header first):");
        const int zl = (MPIO.zone_len > 128) ? 128 : (int)MPIO.zone_len;
        for (int i = 0; i < zl; i += 2)
        {
            if ((i % 32) == 0) put("\n    %04X:", (MPIO.zone_start + i) & 0x1FFE);
            put(" %04X", *(const u16*)&RAM[(MPIO.zone_start + i) & 0x1FFE]);
        }
        put("\n");

        put("  read order, oldest first (offset:value, H=RX header, B=frame body):\n   ");
        for (u32 i = 0; i < MPIO.zone_n; i++)
        {
            const bool inbody = (u16)((MPIO.zone_off[i] - MPIO.zone_start) & 0x1FFE) >= 12;
            put(" %04X:%04X%c", MPIO.zone_off[i], MPIO.zone_val[i], inbody ? 'B' : 'H');
            if ((i % 8) == 7) put("\n   ");
        }
        put("\n");
    }

    /* The buffer's own pointers, written side: where the emulated hardware
       thinks the game is reading from and writing to. Next to the reads below
       they say whether the game's read position and the region the recorder
       bracketed agree - and where they disagree, that disagreement is a
       candidate for the thing a game's code branches on. */
    put("  RX buffer: begin=%04X end=%04X writecursor=%04X readaddr=%04X "
        "readcursor=%04X count=%04X rxtxaddr=%04X\n",
        IOPORT(W_RXBufBegin), IOPORT(W_RXBufEnd), IOPORT(W_RXBufWriteCursor),
        IOPORT(W_RXBufReadAddr), IOPORT(W_RXBufReadCursor), IOPORT(W_RXBufCount),
        IOPORT(W_RXTXAddr));

    /* Every halfword this console's game has taken out of the wireless RAM
       window, in the order it took them, with the value the emulation returned
       - the raw material for "what value did it branch on". The trailing
       letter marks whether the read fell inside the region of the frame the
       console was last handed (`I`) or outside it (`o`), so a game reading a
       region the delivery path never bracketed is visible at a glance. */
    put("  wireless-RAM reads in order, oldest first (last %d of %llu) "
        "(I=inside the last handed frame, o=outside it):\n   ", (int)MPIo::kRing,
        (unsigned long long)MPIO.rx_total);
    {
        const u64 keep = (MPIO.rx_total < (u64)MPIo::kRing) ? MPIO.rx_total
                                                           : (u64)MPIo::kRing;
        const u64 first = MPIO.rx_total - keep;
        for (u64 k = 0; k < keep; k++)
        {
            const u32 r = (u32)((first + k) % (u64)MPIo::kRing);
            put(" %04X:%04X%c", MPIO.rx_off[r], MPIO.rx_val[r], MPIO.rx_in[r] ? 'I' : 'o');
            if (((u32)k % 8) == 7) put("\n   ");
        }
        put("\n");
    }

    put("  wifi registers the game read, most-read first (offset name reads last):\n");
    u32 done[24];
    int ndone = 0;
    for (int rank = 0; rank < (int)(sizeof(done) / sizeof(done[0])); rank++)
    {
        u32 best = 0;
        u32 besti = 0;
        bool found = false;
        for (u32 i = 0; i < (u32)MPIo::kRegWords; i++)
        {
            if (MPIO.reg_reads[i] == 0) continue;
            bool seen = false;
            for (int d = 0; d < ndone; d++)
                if (done[d] == i) { seen = true; break; }
            if (seen) continue;
            if (!found || MPIO.reg_reads[i] > best)
            {
                best = MPIO.reg_reads[i];
                besti = i;
                found = true;
            }
        }
        if (!found) break;
        done[ndone++] = besti;
        put("    %03X %-18s %-9u %04X\n", besti << 1, mpio_reg_name(besti << 1), best,
            MPIO.reg_last[besti]);
    }

    put("  register reads in order, oldest first (last %d of %llu):\n   ", (int)MPIo::kRing,
        (unsigned long long)MPIO.ring_total);
    const u64 keep = (MPIO.ring_total < (u64)MPIo::kRing) ? MPIO.ring_total : (u64)MPIo::kRing;
    const u64 first = MPIO.ring_total - keep;
    for (u64 k = 0; k < keep; k++)
    {
        const u32 r = (u32)((first + k) % (u64)MPIo::kRing);
        put(" %03X:%04X", MPIO.ring_off[r], MPIO.ring_val[r]);
        if (((u32)k % 10) == 9) put("\n   ");
    }
    put("\n");

    /* The register window with the accessor's PC: what a driver read at the
       instant it decided not to power the transceiver up. Kept for both
       directions because a gate can sit on a value the driver wrote and
       expects to read back. The frame number ties an entry to the boot
       boundary, and the PC is the address to disassemble - the one thing no
       counter in this report can supply. */
    put("  registers the game wrote, last writer per offset (offset value "
        "pc7 lr7):\n");
    for (u32 i = 0; i < (u32)MPIo::kRegWords; i++)
    {
        if (MPIO.regw_pc[i] == 0) continue;
        put("    %03X %-18s %04X  %08X %08X\n", i << 1, mpio_reg_name(i << 1),
            MPIO.regw_val[i], MPIO.regw_pc[i], MPIO.regw_lr[i]);
    }

    put("  register accesses in order, oldest first (last %d of %llu; "
        "r/w = this console's own read/write, @pc7, f = frame):\n   ",
        (int)MPIo::kRegRingTail, (unsigned long long)MPIO.reg_ring_total);
    {
        const u64 keep2 = (MPIO.reg_ring_total < (u64)MPIo::kRegRingTail)
                        ? MPIO.reg_ring_total : (u64)MPIo::kRegRingTail;
        const u64 first2 = MPIO.reg_ring_total - keep2;
        for (u64 k = 0; k < keep2; k++)
        {
            const u32 r = (u32)((first2 + k) % (u64)MPIo::kRegRing);
            const MPIo::MPIoReg& e = MPIO.reg_ring[r];
            put(" %c %03X:%04X@%08X f%u", e.wr ? 'w' : 'r', e.off, e.val, e.pc, e.frame);
            if (((u32)k % 4) == 3) put("\n   ");
        }
        put("\n");
    }

    /* The window that decides a Download Play hand-off: every register access
       this console's own software made from the frame its downloaded image was
       booted on. Printed on its own because the tail above is usually other
       traffic - 1024 entries is a few frames of a running game. A count of
       zero here is the reading, not a gap in the instrument: it says the
       booted image never touched its radio, which is the difference between a
       game giving up on a chip that looks dead and a bus that went silent
       under it. The reset itself is a register access the child does make, so
       a window that opens at a write to W_IF or the power register is the
       child's own bring-up starting. */
    if (DpSessionFrame)
    {
        const u64 keep3 = (MPIO.reg_ring_total < (u64)MPIo::kRegRing)
                        ? MPIO.reg_ring_total : (u64)MPIo::kRegRing;
        const u64 first3 = MPIO.reg_ring_total - keep3;
        const bool clipped =
            (first3 > 0) &&
            (MPIO.reg_ring[(u32)(first3 % (u64)MPIo::kRegRing)].frame > DpSessionFrame);

        u64 nwin = 0;
        for (u64 k = 0; k < keep3; k++)
            if (MPIO.reg_ring[(u32)((first3 + k) % (u64)MPIo::kRegRing)].frame
                    >= DpSessionFrame)
                nwin++;

        put("  from the download-play hand-off at frame %u on: %llu of this "
            "console's %llu accesses%s\n",
            DpSessionFrame, (unsigned long long)nwin,
            (unsigned long long)MPIO.reg_ring_total,
            clipped ? " (the ring has rolled past the hand-off, its first "
                      "accesses are no longer held)" : "");
        put("   ");
        u64 shown = 0;
        for (u64 k = 0; k < keep3; k++)
        {
            const u32 r = (u32)((first3 + k) % (u64)MPIo::kRegRing);
            const MPIo::MPIoReg& e = MPIO.reg_ring[r];
            if (e.frame < DpSessionFrame) continue;
            if (shown >= (u64)MPIo::kRegRingBoot) break;
            put(" %c %03X:%04X@%08X f%u", e.wr ? 'w' : 'r', e.off, e.val, e.pc,
                e.frame);
            shown++;
            if ((shown % 4) == 0) put("\n   ");
        }
        put("\n");

        /* The window above is what the ring still holds, which is the tail of
           everything this console has done since the hand-off. The one number
           that stays exact however far the ring has rolled is *when* this
           console last touched its radio: a booted child whose software did
           its bring-up and then went quiet is a different finding from one
           still polling, and "N frames ago" is what separates them without
           reading 1024 entries. The frame the console is on now is printed
           beside it so the gap is a value in this report rather than one the
           reader has to bring. */
        if (MPIO.reg_ring_total > 0)
        {
            const u32 lastacc =
                MPIO.reg_ring[(u32)((MPIO.reg_ring_total - 1) % (u64)MPIo::kRegRing)].frame;
            const u32 nowframe = NDS.NumFrames;
            put("    last register access this console made: frame %u, %u frame(s) "
                "before this report (the console is at frame %u)\n",
                lastacc, (nowframe > lastacc) ? (nowframe - lastacc) : 0, nowframe);
        }
    }

    /* The write side: which of the console's own instructions put each byte
       of the frame it is about to send into the slot. The command body starts
       at offset 0x18 of the slot (12-byte TX header + 24-byte 802.11 header),
       so a Download Play command's class byte - GBATEK's CMD_DATA [06h], the
       one that says 00/01/03/04/05 - is at slot offset 0x1E. */
    put("  what the game wrote into the low wireless RAM (the slot a command "
        "frame is composed in is at offset 0; body+6 = offset 1E): last %d of "
        "%llu writes, oldest first (offset:value@pc/lr):\n   ",
        (int)MPIo::kWrRing, (unsigned long long)MPIO.wr_total);
    {
        const u64 keep = (MPIO.wr_total < (u64)MPIo::kWrRing) ? MPIO.wr_total
                                                              : (u64)MPIo::kWrRing;
        const u64 first = MPIO.wr_total - keep;
        for (u64 k = 0; k < keep; k++)
        {
            const u32 r = (u32)((first + k) % (u64)MPIo::kWrRing);
            put(" %04X:%04X@%08X/%08X", MPIO.wr[r].off, MPIO.wr[r].val,
                MPIO.wr[r].pc, MPIO.wr[r].lr);
            if (((u32)k % 4) == 3) put("\n   ");
        }
        put("\n");
    }

    put("  last writer per offset, where the writes actually landed "
        "(offset value count pc lr):\n");
    for (u32 i = 0; i < (u32)MPIo::kWrKeep; i++)
    {
        if (!MPIO.wr_n[i]) continue;
        put("    %04X val=%04X n=%-8llu pc=%08X lr=%08X\n", i, MPIO.wr_val[i],
            (unsigned long long)MPIO.wr_n[i], MPIO.wr_pc[i], MPIO.wr_lr[i]);
    }

    put("  TX slot arms: cmd last pc=%08X lr=%08X val=%04X n=%llu | "
        "reply1 last pc=%08X lr=%08X val=%04X n=%llu\n",
        MPIO.wr_cmd_pc, MPIO.wr_cmd_lr, MPIO.wr_cmd_val,
        (unsigned long long)MPIO.wr_cmd_n,
        MPIO.wr_reply_pc, MPIO.wr_reply_lr, MPIO.wr_reply_val,
        (unsigned long long)MPIO.wr_reply_n);

    /* The arm's register file: the frame's description is in these, and the
       command body it was built from lives at the descriptor's own pointers,
       so this is what a reader needs to follow the frame back to its source. */
    put("  arm registers at that command-slot write (cpsr=%08X):\n   ",
        MPIO.wr_cmd_cpsr);
    for (int i = 0; i < 16; i++)
    {
        put(" r%d=%08X", i, MPIO.wr_cmd_regs[i]);
        if ((i % 4) == 3) put("\n   ");
    }
    put("\n");

    put("  irq: cpu-line=%llu", (unsigned long long)MPD.irq_cpu_line);
    for (int i = 0; i < 16; i++)
        if (MPD.irq_raised[i] || MPD.irq_acked[i])
            put(" %d:raised=%llu acked=%llu", i, (unsigned long long)MPD.irq_raised[i],
                (unsigned long long)MPD.irq_acked[i]);
    put("  IF=%04X IE=%04X pending=%04X\n", MPD.if_now, MPD.ie_now,
        (u16)(MPD.if_now & MPD.ie_now));

    return n;
}

u16 Wifi::ReadRaw(u32 addr)
{
    if (addr >= 0x04810000)
        return 0;

    addr &= 0x7FFE;

    if (addr >= 0x4000 && addr < 0x6000)
    {
        /* Reads of the wireless RAM window. A game that consumes a received
           packet usually reads it HERE, directly out of the receive region,
           rather than through W_RXBufDataRead - which is why the v0.1.8o
           instrument saw zero reads on a working session and left "does the
           guest's game ever read the host's commands?" unanswered. Only the
           receive region is watched, and throttled, because the window is also
           where TX buffers live and a per-read log would bury the run. */
        u16 off = addr & 0x1FFE;
        u16 begin = IOPORT(W_RXBufBegin) & 0x1FFE;
        u16 end = IOPORT(W_RXBufEnd) & 0x1FFE;
        bool inrx = (begin <= end) ? (off >= begin && off < end)
                                   : (off >= begin || off < end);
        if (inrx && (IsMP || IsMPClient))
        {
            /* Always counted (the flight recorder), printed on a schedule: what
               this answers is whether the console's *game* ever reads the mail
               the link delivered, which the wire counters cannot see. */
            MPD.window_reads++;


            if (mp_trace())
            {
                static u64 lastreport = 0;

                if ((USTimestamp - lastreport) > 20000)
                {
                    lastreport = USTimestamp;
                    Log(LogLevel::Debug,
                        "[mp] the console's game reads its wireless receive RAM window: %llu so far, "
                        "this one at %04X (%s)\n",
                        (unsigned long long)MPD.window_reads, off,
                        IsMPClient ? "as an MP client" : "as an MP host");
                }
            }
        }
        return *(u16*)&RAM[addr & 0x1FFE];
    }
    if (addr >= 0x2000 && addr < 0x4000)
        return 0xFFFF;

    bool activeread = (addr < 0x1000);

    switch (addr)
    {
    case W_Random: // random generator. not accurate
        // TODO: rotate the sequence based on the ARM7 cycle counter (if this is important)
        Random = (Random & 0x1) ^ (((Random & 0x3FF) << 1) | (Random >> 10));
        return Random;

    case W_Preamble:
        return IOPORT(W_Preamble) & 0x0003;

    case W_USCount0: return (u16)(USCounter & 0xFFFF);
    case W_USCount1: return (u16)((USCounter >> 16) & 0xFFFF);
    case W_USCount2: return (u16)((USCounter >> 32) & 0xFFFF);
    case W_USCount3: return (u16)(USCounter >> 48);

    case W_USCompare0: return (u16)(USCompare & 0xFFFF);
    case W_USCompare1: return (u16)((USCompare >> 16) & 0xFFFF);
    case W_USCompare2: return (u16)((USCompare >> 32) & 0xFFFF);
    case W_USCompare3: return (u16)(USCompare >> 48);

    case W_CmdCount: return (CmdCounter + 9) / 10;

    case W_BBRead:
        if ((IOPORT(W_BBCnt) & 0xF000) != 0x6000)
        {
            Log(LogLevel::Error, "WIFI: bad BB read, CNT=%04X\n", IOPORT(W_BBCnt));
            return 0;
        }
        /* GBATEK: W_BB_READ holds "data received from chip (from previous
           W_BB_CNT transfer)". A driver pipelines - it reads the previous
           result, starts the next transfer and then waits on W_BBBusy - so
           while the transfer is still in flight the port must keep the
           previous result; once it has finished it holds this one's. */
        if (NDS.GetSharedUSClock() < BBBusyUntil)
            return BBReadResult;
        BBReadResult = BBRegs[IOPORT(W_BBCnt) & 0xFF];
        return BBReadResult;

    case W_BBBusy:
        return (NDS.GetSharedUSClock() < BBBusyUntil) ? 0x0001 : 0x0000;
    case W_RFBusy:
        return (NDS.GetSharedUSClock() < RFBusyUntil) ? 0x0001 : 0x0000;

    case W_RXBufDataRead:
        if (activeread)
        {
            u32 rdaddr = IOPORT(W_RXBufReadAddr);
            u16 ret = *(u16*)&RAM[rdaddr];

            rdaddr += 2;
            if (rdaddr == (IOPORT(W_RXBufEnd) & 0x1FFE))
                rdaddr = (IOPORT(W_RXBufBegin) & 0x1FFE);
            if (rdaddr == IOPORT(W_RXBufGapAddr))
            {
                rdaddr += (IOPORT(W_RXBufGapSize) << 1);
                if (rdaddr >= (IOPORT(W_RXBufEnd) & 0x1FFE))
                    rdaddr = rdaddr + (IOPORT(W_RXBufBegin) & 0x1FFE) - (IOPORT(W_RXBufEnd) & 0x1FFE);

                if (IOPORT(0x000) == 0xC340)
                    IOPORT(W_RXBufGapSize) = 0;
            }

            IOPORT(W_RXBufReadAddr) = rdaddr & 0x1FFE;
            IOPORT(W_RXBufDataRead) = ret;

            /* The game taking a packet out of the receive buffer. Every command
               the host sends is delivered this way, so a game that is following
               the session reads these bytes and a game that is not does not -
               which is the one thing no other log here can tell apart. Counted
               rather than listed: one line per 1000 halfwords, plus the first
               few, so `grep -c` still gives the total order of magnitude. */
            if (mp_trace())
            {
                static unsigned long rxreads = 0;
                ++rxreads;
                if (rxreads <= 8 || (rxreads % 1000) == 0)
                    Log(LogLevel::Debug,
                        "[mp] the console's game read %lu halfwords out of its receive buffer "
                        "(this one at %04X, %s in multiplayer)\n",
                        rxreads, rdaddr,
                        (IsMPClient || IsMP) ? "a console that is" : "NOT");
            }

            if (IOPORT(W_RXBufCount) > 0)
            {
                IOPORT(W_RXBufCount)--;
                if (IOPORT(W_RXBufCount) == 0)
                    SetIRQ(9);
            }
        }
        break;

    case W_TXBusy:
        return IOPORT(W_TXBusy) & 0x001F; // no bit for MP replies. odd

    case W_CMDStat0:
    case W_CMDStat1:
    case W_CMDStat2:
    case W_CMDStat3:
    case W_CMDStat4:
    case W_CMDStat5:
    case W_CMDStat6:
    case W_CMDStat7:
        {
            u16 ret = IOPORT(addr&0xFFF);
            IOPORT(addr&0xFFF) = 0;
            return ret;
        }
    }

    return IOPORT(addr&0xFFF);
}

void Wifi::Write(u32 addr, u16 val)
{
    if (addr >= 0x04810000)
        return;

    addr &= 0x7FFE;

    RecordMPIoWrite(addr, val);

    if (addr >= 0x4000 && addr < 0x6000)
    {
        *(u16*)&RAM[addr & 0x1FFE] = val;
        return;
    }
    if (addr >= 0x2000 && addr < 0x4000)
        return;

    switch (addr)
    {
    case W_ModeReset:
        {
            u16 oldval = IOPORT(W_ModeReset);
            TracePowerWrite(MPPW_ModeReset, oldval, val & 0x0001);
            IOPORT(W_ModeReset) = val & 0x0001;

            if (!(oldval & 0x0001) && (val & 0x0001))
            {
                IOPORT(0x27C) = 0x0005;
                // TODO: 02A2??

                UpdatePowerStatus(0);
            }
            else if ((oldval & 0x0001) && !(val & 0x0001))
            {
                IOPORT(0x27C) = 0x000A;

                UpdatePowerStatus(0);
            }

            if (val & 0x2000)
            {
                IOPORT(W_RXBufWriteAddr) = 0;
                IOPORT(W_CmdTotalTime) = 0;
                IOPORT(W_CmdReplyTime) = 0;
                IOPORT(0x1A4) = 0;
                IOPORT(0x278) = 0x000F;
                // TODO: other ports??
            }
            if (val & 0x4000)
            {
                IOPORT(W_ModeWEP) = 0;
                IOPORT(W_TXStatCnt) = 0;
                IOPORT(0x00A) = 0;
                IOPORT(W_MACAddr0) = 0;
                IOPORT(W_MACAddr1) = 0;
                IOPORT(W_MACAddr2) = 0;
                IOPORT(W_BSSID0) = 0;
                IOPORT(W_BSSID1) = 0;
                IOPORT(W_BSSID2) = 0;
                IOPORT(W_AIDLow) = 0;
                IOPORT(W_AIDFull) = 0;

                /* The registers are back to their defaults, so the group this
                   console was in is gone with them. */
                MPArmHeld = false;
                MPMYAID = 0;

                IOPORT(W_TXRetryLimit) = 0x0707;
                IOPORT(0x02E) = 0;
                IOPORT(W_RXBufBegin) = 0x4000;
                IOPORT(W_RXBufEnd) = 0x4800;
                IOPORT(W_TXBeaconTIM) = 0;
                IOPORT(W_Preamble) = 0x0001;
                IOPORT(W_RXFilter) = 0x0401;
                IOPORT(0x0D4) = 0x0001;
                IOPORT(W_RXFilter2) = 0x0008;
                IOPORT(0x0EC) = 0x3F03;
                IOPORT(W_TXHeaderCnt) = 0;
                IOPORT(0x198) = 0;
                IOPORT(0x1A2) = 0x0001;
                IOPORT(0x224) = 0x0003;
                IOPORT(0x230) = 0x0047;
            }
        }
        return;

    case W_ModeWEP:
        val &= 0x007F;
        TracePowerWrite(MPPW_ModeWEP, IOPORT(W_ModeWEP), val);
        IOPORT(W_ModeWEP) = val;

        if (IOPORT(W_PowerTX) & (1<<1))
        {
            if ((val & 0x7) == 1)
                IOPORT(W_PowerDownCtrl) |= (1<<1);
            else if ((val & 0x7) == 2)
                IOPORT(W_PowerDownCtrl) = 3;

            if ((val & 0x7) != 3)
                IOPORT(W_PowerState) &= 0x0300;

            UpdatePowerStatus(0);
        }
        return;

    case W_IE:
        {
            u16 oldflags = IOPORT(W_IF) & IOPORT(W_IE);
            IOPORT(W_IE) = val;
            CheckIRQ(oldflags);
        }
        return;
    case W_IF:
        {
            /* The game acknowledging sources: each 1 written clears that
               pending bit (`IOPORT(W_IF) &= ~val` below), which is a wifi IRQ
               handler returning from the source it was woken for. Counted per
               source, so raised-vs-acked says both whether the handler still
               runs at all and which source it keeps taking. */
            u16 clearing = val & IOPORT(W_IF);
            for (int i = 0; i < 16; i++)
                if (clearing & (1<<i)) MPD.irq_acked[i]++;
            IOPORT(W_IF) &= ~val;
        }
        return;
    case W_IFSet:
        {
            u16 oldflags = IOPORT(W_IF) & IOPORT(W_IE);
            for (int i = 0; i < 16; i++)
                if ((val & 0xFBFF) & (1<<i)) MPD.irq_raised[i]++;
            IOPORT(W_IF) |= (val & 0xFBFF);
            CheckIRQ(oldflags);
            Log(LogLevel::Debug, "wifi: force-setting IF %04X\n", val);
        }
        return;

    case W_MACAddr0:
        TracePowerWrite(MPPW_MAC0, IOPORT(W_MACAddr0), val);
        break;

    case W_MACAddr1:
        TracePowerWrite(MPPW_MAC1, IOPORT(W_MACAddr1), val);
        break;

    case W_MACAddr2:
        TracePowerWrite(MPPW_MAC2, IOPORT(W_MACAddr2), val);
        break;

    case W_BSSID0:
        TracePowerWrite(MPPW_BSSID0, IOPORT(W_BSSID0), val);
        break;

    case W_BSSID1:
        TracePowerWrite(MPPW_BSSID1, IOPORT(W_BSSID1), val);
        break;

    case W_BSSID2:
        TracePowerWrite(MPPW_BSSID2, IOPORT(W_BSSID2), val);
        break;

    case W_AIDLow:
        /* Part of the guest's MP timeline: a guest clears this while it has no
           answer prepared, and sets its AID when it does. Logged on change
           only, because the write is otherwise a per-round no-op. */
        if (IOPORT(W_AIDLow) != (val & 0x000F))
            MPD.aids_set++;

        /* The game holds its AID up for most of a round and clears it while it
           prepares the next answer; the value it holds is the association, so
           it is remembered for the rounds it is cleared in. */
        if (val & 0x000F)
            MPMYAID = val & 0x000F;

        if (mp_trace() && (IOPORT(W_AIDLow) != (val & 0x000F)))
            Log(LogLevel::Debug,
                "[mp] guest set its AID to %d at %llu us (last cmd seq %04X): %s\n",
                val & 0x000F, (unsigned long long)USTimestamp, MPLastSeqno,
                (val & 0x000F) ? "it is ready to answer" : "it has nothing to answer with");
        IOPORT(W_AIDLow) = val & 0x000F;
        return;

    case W_AIDFull:
        IOPORT(W_AIDFull) = val & 0x07FF;
        return;

    case W_PowerUS:
        TracePowerWrite(MPPW_PowerUS, IOPORT(W_PowerUS), val & 0x0003);
        IOPORT(W_PowerUS) = val & 0x0003;
        UpdatePowerOn();
        return;

    case W_PowerTX:
        TracePowerWrite(MPPW_PowerTX, IOPORT(W_PowerTX), val & 0x0003);
        IOPORT(W_PowerTX) = val & 0x0003;
        if (val & (1<<1))
        {
            if ((IOPORT(W_ModeWEP) & 0x7) == 1)
                IOPORT(W_PowerDownCtrl) |= (1<<1);
            else if ((IOPORT(W_ModeWEP) & 0x7) == 2)
                IOPORT(W_PowerDownCtrl) = 3;

            UpdatePowerStatus(0);
        }
        return;

    case W_PowerState:
        /* Traced before the mode gate: a write this core drops is exactly what
           a "never asks for the radio" reading has to be able to see. */
        TracePowerWrite(MPPW_PowerState, IOPORT(W_PowerState), val);
        if ((IOPORT(W_ModeWEP) & 0x7) != 3)
            return;

        val = (IOPORT(W_PowerState) & 0x0300) | (val & 0x0003);
        if ((val & 0x0300) == 0x0200)
            val &= ~(1<<0);
        else
            val &= ~(1<<1);

        if (!(val & (1<<9)))
            val &= ~(1<<8);

        IOPORT(W_PowerState) = val;
        UpdatePowerStatus(0);
        return;

    case W_PowerForce:
        val &= 0x8001;
        TracePowerWrite(MPPW_PowerForce, IOPORT(W_PowerForce), val);
        IOPORT(W_PowerForce) = val;
        UpdatePowerStatus(0);
        return;

    case W_PowerDownCtrl:
        TracePowerWrite(MPPW_PowerDown, IOPORT(W_PowerDownCtrl), val & 0x0003);
        IOPORT(W_PowerDownCtrl) = val & 0x0003;

        if (IOPORT(W_PowerTX) & (1<<1))
        {
            if ((IOPORT(W_ModeWEP) & 0x7) == 1)
                IOPORT(W_PowerDownCtrl) |= (1<<1);
            else if ((IOPORT(W_ModeWEP) & 0x7) == 2)
                IOPORT(W_PowerDownCtrl) = 3;
        }
        
        if (val != 0 && val != 3)
            Log(LogLevel::Warn, "wifi: unusual W_PowerDownCtrl value %04X\n", val);

        UpdatePowerStatus(0);
        return;

    case W_USCountCnt: val &= 0x0001; break;
    case W_USCompareCnt:
        if (mp_trace() && ((val & 0x0001) != (IOPORT(W_USCompareCnt) & 0x0001)))
            Log(LogLevel::Debug,
                "[mp] USCOMPARECNT %s: IRQ14 %s be raised by USCOUNT reaching USCOMPARE"
                "%s\n",
                (val & 0x0001) ? "enabled" : "disabled",
                (val & 0x0001) ? "can" : "cannot",
                (val & 0x0001) ? "" : " - the countdown cannot raise it either, so this console "
                                 "sends no beacon of its own");
        if (val & 0x0002) SetIRQ14(2);
        val &= 0x0001;
        break;

    case W_USCount0: USCounter = (USCounter & 0xFFFFFFFFFFFF0000) | (u64)val; return;
    case W_USCount1: USCounter = (USCounter & 0xFFFFFFFF0000FFFF) | ((u64)val << 16); return;
    case W_USCount2: USCounter = (USCounter & 0xFFFF0000FFFFFFFF) | ((u64)val << 32); return;
    case W_USCount3: USCounter = (USCounter & 0x0000FFFFFFFFFFFF) | ((u64)val << 48); return;

    case W_USCompare0:
        {
            const u64 oldcompare = USCompare;
            USCompare = (USCompare & 0xFFFFFFFFFFFF0000) | (u64)(val & 0xFC00);
            if (val & 0x0001) BlockBeaconIRQ14 = true;

            /* The group-membership dance described in SetIRQ14's comment: the
               value comes from the beacon the console just received, and bit0
               asks for the beacon IRQ to wait for it. A console that writes
               this is a console that took a beacon and is scheduling its own
               into the group's beacon schedule. */
            if (mp_trace() && ((USCompare != oldcompare) || (val & 0x0001)))
                Log(LogLevel::Debug,
                    "[mp] USCOMPARE0 <- %04X: USCOMPARE is now %llX, USCOUNT is %llX "
                    "(%s)%s\n",
                    val, (unsigned long long)USCompare, (unsigned long long)USCounter,
                    (USCompare >= USCounter) ? "ahead of USCOUNT" : "already passed by USCOUNT",
                    (val & 0x0001) ? ", bit0 set: the beacon countdown's IRQ14 is blocked until "
                                     "USCOUNT reaches it" : "");
        }
        return;
    case W_USCompare1: USCompare = (USCompare & 0xFFFFFFFF0000FFFF) | ((u64)val << 16); return;
    case W_USCompare2: USCompare = (USCompare & 0xFFFF0000FFFFFFFF) | ((u64)val << 32); return;
    case W_USCompare3: USCompare = (USCompare & 0x0000FFFFFFFFFFFF) | ((u64)val << 48); return;

    case W_CmdCount: CmdCounter = val * 10; return;

    case W_BBMode:
        TracePowerWrite(MPPW_BBMode, IOPORT(W_BBMode), val);
        break;

    case W_BBPower:
        TracePowerWrite(MPPW_BBPower, IOPORT(W_BBPower), val);
        break;

    case W_BBCnt:
        TracePowerWrite(MPPW_BBCnt, IOPORT(W_BBCnt), val);
        IOPORT(W_BBCnt) = val;
        if ((IOPORT(W_BBCnt) & 0xF000) == 0x5000)
        {
            u32 regid = IOPORT(W_BBCnt) & 0xFF;
            if (!BBRegsRO[regid])
                BBRegs[regid] = IOPORT(W_BBWrite) & 0xFF;
        }
        if (((IOPORT(W_BBCnt) & 0xF000) == 0x5000) ||
            ((IOPORT(W_BBCnt) & 0xF000) == 0x6000))
        {
            /* GBATEK: "transfer is started after writing to this register",
               direction 5 = write, 6 = read. The chip stays busy until it has
               clocked the byte through. */
            BBBusyUntil = NDS.GetSharedUSClock() + kBBTransferUS;
        }
        return;

    case W_RFData2:
        IOPORT(W_RFData2) = val;
        if (RFVersion == 3) RFTransfer_Type3();
        else                RFTransfer_Type2();
        return;
    case W_RFCnt:
        TracePowerWrite(MPPW_RFCnt, IOPORT(W_RFCnt), val);
        if (val & 0x0001)
        {
            /* Bit 0 starts an RF-chip operation, which takes as long as a
               register transfer over the RF serial bus. */
            RFBusyUntil = NDS.GetSharedUSClock() + kRFTransferUS;
        }
        val &= 0x413F;
        break;


    case W_RXCnt:
        if (val & 0x0001)
        {
            IOPORT(W_RXBufWriteCursor) = IOPORT(W_RXBufWriteAddr);
        }
        if (val & 0x0080)
        {
            /* The game's own copy request (GBATEK's reply latch), on the same
               ledger as the emulator's latch in SendMPReply: it is the second
               writer of the one transmit slot a game cannot write. */
            if (mp_trace())
                Log(LogLevel::Debug, "[mp] txslot REPLY2 <- %04X (RXCNT bit 7: the game's own "
                    "latch of REPLY1), REPLY1 <- 0000\n", IOPORT(W_TXSlotReply1));
            IOPORT(W_TXSlotReply2) = IOPORT(W_TXSlotReply1);
            IOPORT(W_TXSlotReply1) = 0;
        }
        if (val & 0x8000)
        {
            FireTX();
        }
        val &= 0xFF0E;
        if (val & 0x7FFF) Log(LogLevel::Warn, "wifi: unknown RXCNT bits set %04X\n", val);
        break;

    case W_RXBufDataRead:
        Log(LogLevel::Warn, "wifi: writing to RXBUF_DATA_READ. wat\n");
        if (IOPORT(W_RXBufCount) > 0)
        {
            IOPORT(W_RXBufCount)--;
            if (IOPORT(W_RXBufCount) == 0)
                SetIRQ(9);
        }
        return;

    case W_RXBufReadAddr:
    case W_RXBufGapAddr:
        val &= 0x1FFE;
        break;
    case W_RXBufGapSize:
    case W_RXBufCount:
    case W_RXBufWriteAddr:
    case W_RXBufReadCursor:
        val &= 0x0FFF;
        break;


    case W_TXReqReset:
        IOPORT(W_TXReqRead) &= ~val;
        return;
    case W_TXReqSet:
        IOPORT(W_TXReqRead) |= val;
        FireTX();
        return;

    case W_TXSlotReset:
        if (val & 0x0001) IOPORT(W_TXSlotLoc1) &= 0x7FFF;
        if (val & 0x0002) IOPORT(W_TXSlotCmd) &= 0x7FFF;
        if (val & 0x0004) IOPORT(W_TXSlotLoc2) &= 0x7FFF;
        if (val & 0x0008) IOPORT(W_TXSlotLoc3) &= 0x7FFF;
        // checkme: any bits affecting the beacon slot?
        if (val & 0x0040) IOPORT(W_TXSlotReply2) &= 0x7FFF;
        if (val & 0x0080) IOPORT(W_TXSlotReply1) &= 0x7FFF;
        if ((val & 0xFF30) && (val != 0xFFFF)) Log(LogLevel::Warn, "unusual TXSLOTRESET %04X\n", val);
        val = 0; // checkme (write-only port)
        break;

    case W_TXBufDataWrite:
        {
            u32 wraddr = IOPORT(W_TXBufWriteAddr);
            *(u16*)&RAM[wraddr] = val;

            wraddr += 2;
            if (wraddr == IOPORT(W_TXBufGapAddr))
                wraddr += (IOPORT(W_TXBufGapSize) << 1);

            //if (IOPORT(0x000) == 0xC340)
            //    IOPORT(W_TXBufGapSize) = 0;

            IOPORT(W_TXBufWriteAddr) = wraddr & 0x1FFE;

            if (IOPORT(W_TXBufCount) > 0)
            {
                IOPORT(W_TXBufCount)--;
                if (IOPORT(W_TXBufCount) == 0)
                    SetIRQ(8);
            }
        }
        return;

    case W_TXBufWriteAddr:
    case W_TXBufGapAddr:
        val &= 0x1FFE;
        break;
    case W_TXBufGapSize:
    case W_TXBufCount:
        val &= 0x0FFF;
        break;

    case W_TXSlotBeacon:
        if (mp_trace())
            Log(LogLevel::Debug, "[mp] txslot BEACON <- %04X %s\n",
                val, mp_txslot_what(val, IOPORT(W_TXSlotBeacon)));
        IsMP = (val & 0x8000) != 0;
        if (IsMP) MPD.role = 1;

        /* Every (re)arm of this console's own beacon, with the length it
           points at: the beacon is what a group's members announce themselves
           in, so which console is beaconing, and when it stopped, is the first
           thing to check in a session that ends with one member leaving - the
           host arms and sends one every ~217ms throughout, the client never
           arms one at all (2026-09-27). */
        if (mp_trace() && (val & 0x8000))
        {
            const u16 baddr = (val & 0x0FFF) << 1;
            const int blen = (int)(*(u16*)&RAM[baddr + 0xA] & 0x3FFF);
            Log(LogLevel::Debug,
                "[mp] beacon armed: slot %04X, %d byte frame, fc=%04X dest=%02X%02X..\n",
                val, blen, (blen >= 12) ? *(u16*)&RAM[baddr + 0xC] : 0,
                (blen >= 10) ? RAM[baddr + 0x10] : 0, (blen >= 10) ? RAM[baddr + 0x11] : 0);
        }
        else if (mp_trace() && (val != IOPORT(W_TXSlotBeacon)))
            Log(LogLevel::Debug,
                "[mp] TXSLOTBEACON <- %04X without bit 15: this console is disarming (or has "
                "never armed) the beacon it would announce itself with\n", val);
        break;

    case W_TXSlotReply1:
        if (mp_trace())
            Log(LogLevel::Debug, "[mp] txslot REPLY1 <- %04X %s\n",
                val, mp_txslot_what(val, IOPORT(W_TXSlotReply1)));
        /* Every arm, with the bytes it holds, because that is the one thing a
           frozen host command cannot tell apart: a guest whose game is done
           talking (it armed once and never again) and a guest whose game is
           answering into a buffer the host ignores look identical in the
           reply the host reads, but not here. Mario Kart DS's stuck lobby in
           the live session of 2026-09-25 had the *same* 6 bytes on 1368
           consecutive rounds; whether the guest armed a fresh answer for any
           of them is what this line answers. */
        if (mp_trace())
        {
            char body[3 * 16 + 1] = "-";
            const int armlen = (val & 0x8000) ? (int)(*(u16*)&RAM[((val & 0x0FFF) << 1) + 0xA] & 0x3FFF) : 0;
            const int bodylen = armlen - 24;
            if (bodylen > 0)
            {
                // +12 like the send log below, so the two can be compared byte for byte
                const int n = (bodylen > 16) ? 16 : bodylen;
                for (int i = 0; i < n; i++)
                    snprintf(&body[i * 3], 4, "%02X ", RAM[((val & 0x0FFF) << 1) + 12 + 24 + i]);
                body[n * 3 - 1] = 0;
            }

            Log(LogLevel::Debug,
                "[mp] guest arms an answer: arm #%llu, slot %04X, %d byte frame, body[%d]=%s "
                "(last cmd seq %04X, my clock %llu)\n",
                (unsigned long long)(MPArmNextSerial + 1), val, armlen, bodylen, body,
                MPLastSeqno, (unsigned long long)USTimestamp);
        }

        IOPORT(W_TXSlotReply1) = val;
        if (val & 0x8000)
        {
            MPD.arms++;

            /* The game's answer, held and not sent. Sending it from here (from
               inside the game's own write) is measured to starve its driver,
               see the note above MPReplyAID; FinishRX hands it to stock's
               SendMPReply when the poll it answers arrives. The driver's own
               clear (a write of 0) is deliberately not treated as a disarm:
               measured, it comes 100 us after every arm, in the same round, and
               is the driver's bookkeeping for a buffer it is done with, not a
               withdrawal of the answer - and each new arm replaces the held one
               (see the note in FinishRX for why it is not spent). */
            MPArmSlot = val;
            MPArmHeld = true;
            /* When this arm was made, and in which round: the poll that spends
               it can then say whose round it belongs to (see FinishRX). */
            MPArmSerial = ++MPArmNextSerial;
            MPArmSeq = MPLastSeqno;
            MPArmUS = USTimestamp;

            /* `MD_SAME_ROUND_REPLY`: the poll this arm answers arrived in this
               same round and is still waiting for it, so send it now instead
               of holding it to the next poll. The arm is already in the slot,
               so AnswerPoll takes it live. */
            if (reply_same_round() && !stock_mp() && MPPendingPoll)
            {
                const u16 pt = MPPendingTime, pm = MPPendingMask, ps = MPPendingSeq;
                MPPendingPoll = false;
                AnswerPoll(ps, pt, pm);
                /* Spent: the arm just answered its own round, so it must not be
                   re-served by the held path at the next poll. */
                MPArmHeld = false;
            }
        }
        return;

    case W_TXSlotCmd:
        if (mp_trace())
            Log(LogLevel::Debug, "[mp] txslot CMD <- %04X %s%s\n",
                val, mp_txslot_what(val, IOPORT(W_TXSlotCmd)),
                (val & 0x4000) ? ", with the retransmit bit" : "");
        /* A guest writing this is starting its own CMD transfer - either it is
           taking over as host, or the game is retransmitting. Bit 14 is the
           "same command again" flag, which is what a game that is waiting for
           an answer it does not get keeps setting. */
        if (mp_trace() && (val & 0x8000) && IsMPClient)
            Log(LogLevel::Debug,
                "[mp] guest started a CMD transfer of its own: slot %04X, %s\n",
                val, (val & 0x4000) ? "retransmitting the same command" : "a new command");

        if (CmdCounter == 0)
            val = (val & 0x7FFF) | (IOPORT(W_TXSlotCmd) & 0x8000);
        // fall-through
    case W_TXSlotLoc1:
    case W_TXSlotLoc2:
    case W_TXSlotLoc3:
        /* A CMD write falls through to here, and it has already logged itself:
           name this one only for the LOC slots it is actually the case for. */
        if (mp_trace() && (addr >= W_TXSlotLoc1))
            Log(LogLevel::Debug, "[mp] txslot LOC%d <- %04X %s\n",
                (int)(addr - W_TXSlotLoc1) / 4 + 1, val,
                mp_txslot_what(val, IOPORT(addr & 0xFFF)));
        // checkme: is it possible to cancel a queued transfer that hasn't started yet
        // by clearing bit15 here?
        IOPORT(addr&0xFFF) = val;
        FireTX();
        return;

    case 0x228:
    case 0x244:
        //printf("wifi: write port%03X %04X\n", addr, val);
        break;

    case W_TXSlotReply2:
        /* Read-only, and faithfully so: the hardware latches
           `W_TXSlotReply1` into this slot itself (see SendMPReply and the
           `W_RXCnt` bit-7 copy), so a game write here is ignored. It is on the
           ledger all the same, because "the game armed a slot and nothing was
           ever transmitted out of it" is one of the two shapes a missing frame
           can have, and a write that is silently swallowed is exactly what an
           audit has to be able to see. */
        if (mp_trace())
            Log(LogLevel::Debug, "[mp] txslot REPLY2 <- %04X IGNORED (read-only: the "
                "hardware latches REPLY1 into it)\n", val);
        return;

    // read-only ports
    case 0x000:
    case 0x034:
    case 0x044:
    case 0x054:
    case 0x0B0:
    case 0x0B6:
    case 0x0B8:
    case 0x15C:
    case 0x15E:
    case 0x180:
    case 0x19C:
    case 0x1A8:
    case 0x1AC:
    case 0x1C4:
    case 0x210:
    case 0x214:
    case 0x268:
        return;

    default:
        //printf("WIFI unk: write %08X %04X\n", addr, val);
        break;
    }

    IOPORT(addr&0xFFF) = val;
}


const u8* Wifi::GetMAC() const
{
    return (u8*)&IOPORT(W_MACAddr0);
}

const u8* Wifi::GetBSSID() const
{
    return (u8*)&IOPORT(W_BSSID0);
}

}
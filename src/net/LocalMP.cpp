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

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "LocalMP.h"

using namespace melonDS;
using namespace melonDS::Platform;

/* Defined with the other reply-window diagnostics below, but also used by the
   queue code above it. */
namespace melonDS {
static bool trace_replies();
static int reply_keep();
static bool stock_mp();
static bool mp_fcs();
static u32 mp_fcs32(const u8* frame, int len) noexcept;
}

using Platform::Log;
using Platform::LogLevel;

namespace melonDS
{

LocalMP::LocalMP() noexcept :
    MPQueueLock(Mutex_Create())
{
    memset(MPPacketQueue, 0, kPacketQueueSize);
    memset(MPReplyQueue, 0, kReplyQueueSize);
    memset(&MPStatus, 0, sizeof(MPStatus));
    memset(PacketReadOffset, 0, sizeof(PacketReadOffset));
    memset(ReplyReadOffset, 0, sizeof(ReplyReadOffset));
    memset(ReplyDiag, 0, sizeof(ReplyDiag));

    // prepare semaphores
    // semaphores 0-15: regular frames; semaphore I is posted when instance I needs to process a new frame
    // semaphores 16-31: MP replies; semaphore I is posted when instance I needs to process a new MP reply

    for (int i = 0; i < 32; i++)
    {
        SemPool[i] = Semaphore_Create();
    }

    Log(LogLevel::Info, "MP comm init OK\n");
}

LocalMP::~LocalMP() noexcept
{
    for (int i = 0; i < 32; i++)
    {
        Semaphore_Free(SemPool[i]);
        SemPool[i] = nullptr;
    }

    Mutex_Free(MPQueueLock);
}

void LocalMP::Begin(int inst)
{
    Mutex_Lock(MPQueueLock);
    PacketReadOffset[inst] = MPStatus.PacketWriteOffset;
    ReplyReadOffset[inst] = MPStatus.ReplyWriteOffset;
    Semaphore_Reset(SemPool[inst]);
    Semaphore_Reset(SemPool[16 + inst]);
    MPStatus.ConnectedBitmask |= (1 << inst);

    /* A new session for this console starts its half of the flight recorder
       from zero, so a digest read later cannot carry the run before it. */
    memset(&ReplyDiag[inst], 0, sizeof(ReplyDiag[inst]));

    Mutex_Unlock(MPQueueLock);
}

void LocalMP::GetReplyDiag(int inst, MPReplyDiag* out) const noexcept
{
    if (!out) return;
    if ((inst < 0) || (inst >= 16))
    {
        memset(out, 0, sizeof(*out));
        return;
    }

    Mutex_Lock(MPQueueLock);
    *out = ReplyDiag[inst];
    Mutex_Unlock(MPQueueLock);
}

void LocalMP::End(int inst)
{
    Mutex_Lock(MPQueueLock);
    MPStatus.ConnectedBitmask &= ~(1 << inst);
    Mutex_Unlock(MPQueueLock);
}

void LocalMP::Reset()
{
    Mutex_Lock(MPQueueLock);

    memset(&MPStatus, 0, sizeof(MPStatus));
    memset(PacketReadOffset, 0, sizeof(PacketReadOffset));
    memset(ReplyReadOffset, 0, sizeof(ReplyReadOffset));
    memset(MPPacketQueue, 0, kPacketQueueSize);
    memset(MPReplyQueue, 0, kReplyQueueSize);
    memset(ReplyDiag, 0, sizeof(ReplyDiag));
    LastHostID = -1;

    Mutex_Unlock(MPQueueLock);

    /* Semaphores are reset outside the queue lock: Begin() does the same, and
       each semaphore carries its own lock. */
    for (int i = 0; i < 32; i++)
        Semaphore_Reset(SemPool[i]);
}

void LocalMP::FIFORead(int inst, int fifo, void* buf, int len) noexcept
{
    u8* data;

    u32 offset, datalen;
    if (fifo == 0)
    {
        offset = PacketReadOffset[inst];
        data = MPPacketQueue;
        datalen = kPacketQueueSize;
    }
    else
    {
        offset = ReplyReadOffset[inst];
        data = MPReplyQueue;
        datalen = kReplyQueueSize;
    }

    if ((offset + len) >= datalen)
    {
        u32 part1 = datalen - offset;
        memcpy(buf, &data[offset], part1);
        memcpy(&((u8*)buf)[part1], data, len - part1);
        offset = len - part1;
    }
    else
    {
        memcpy(buf, &data[offset], len);
        offset += len;
    }

    if (fifo == 0) PacketReadOffset[inst] = offset;
    else           ReplyReadOffset[inst] = offset;
}

void LocalMP::FIFOWrite(int inst, int fifo, void* buf, int len) noexcept
{
    u8* data;

    u32 offset, datalen;
    if (fifo == 0)
    {
        offset = MPStatus.PacketWriteOffset;
        data = MPPacketQueue;
        datalen = kPacketQueueSize;
    }
    else
    {
        offset = MPStatus.ReplyWriteOffset;
        data = MPReplyQueue;
        datalen = kReplyQueueSize;
    }

    if ((offset + len) >= datalen)
    {
        u32 part1 = datalen - offset;
        memcpy(&data[offset], buf, part1);
        memcpy(data, &((u8*)buf)[part1], len - part1);
        offset = len - part1;
    }
    else
    {
        memcpy(&data[offset], buf, len);
        offset += len;
    }

    if (fifo == 0) MPStatus.PacketWriteOffset = offset;
    else           MPStatus.ReplyWriteOffset = offset;
}

int LocalMP::SendPacketGeneric(int inst, u32 type, u8* packet, int len, u64 timestamp) noexcept
{
    if (len > kMaxFrameSize)
    {
        Log(LogLevel::Warn, "wifi: attempting to send frame too big (len=%d max=%d)\n", len, kMaxFrameSize);
        return 0;
    }

    Mutex_Lock(MPQueueLock);

    u16 mask = MPStatus.ConnectedBitmask;

    // TODO: check if the FIFO is full!

    MPPacketHeader pktheader;
    pktheader.Magic = 0x4946494E;
    pktheader.SenderID = inst;
    pktheader.Type = type;
    pktheader.Length = len;
    pktheader.Timestamp = timestamp;

    type &= 0xFFFF;
    int nfifo = (type == 2) ? 1 : 0;

    /* The FCS (the 802.11 checksum) goes *into* the frame's own trailing four
       bytes, because that is where the hardware keeps it - not appended after
       the frame. GBATEK, W_TXBUF_LOCn[0Ah]: "TX length includes the length of a
       4-byte 'FCS' ... The hardware generates the FCS for you, but you still
       must include it in the packet length", and the receive side reports the
       length "excluding FCS checksum" and drops it with `W_RX_LEN_CROP` (see
       Wifi::CheckRX). So the travelling length stays the game's own and the
       peer's game is handed `frame - 4` bytes of header+body, exactly as the
       hardware leaves it.

       Measured on the NSMB route, same save, one env var apart: written in
       place the route reaches gameplay; *appended* (which also had to raise the
       length field to satisfy the receive path) every frame gained 4 trailing
       bytes in the length its peer reads and the route parked in the game's own
       discovery screen with a flawless link under it (history.md v0.1.9r).

       The value is computed rather than zero-filled, though nothing verifies
       it: stock melonDS computes no FCS at all. `MD_MP_FCS=0` leaves the game's
       own bytes in that slot, for the A/B. A zero-length entry is our own
       "nothing to send" marker and keeps its length of zero; a frame too short
       to hold an FCS slot is passed through unchanged. */
    const bool with_fcs = mp_fcs() && ((len - 12) >= 4);

    if (with_fcs)
    {
        u8 fcsbuf[12 + kMaxFrameSize];
        memcpy(fcsbuf, packet, len);
        *(u32*)&fcsbuf[len - 4] = mp_fcs32(&fcsbuf[12], len - 4 - 12);
        FIFOWrite(inst, nfifo, &pktheader, sizeof(pktheader));
        FIFOWrite(inst, nfifo, fcsbuf, len);
    }
    else
    {
        FIFOWrite(inst, nfifo, &pktheader, sizeof(pktheader));
        if (len)
            FIFOWrite(inst, nfifo, packet, len);
    }

    if (type == 1)
    {
        // NOTE: this is not guarded against, say, multiple multiplay games happening on the same machine
        // we would need to pass the packet's SenderID through the wifi module for that
        MPStatus.MPHostinst = inst;
        MPStatus.MPReplyBitmask = 0;

        /* Sending a command re-anchors the host's reply read offset, which
           decides which answers this round is allowed to see. Keeping the
           newest `MD_REPLY_KEEP` (default 1) per console the host asks covers
           the peer answering a whole frame's worth of rounds in one clump; the
           bound is what keeps an unbounded backlog from making every window
           read an older round first (history.md v0.1.8j, and reply_keep above
           for the measurement).

           NOTE: this is not guarded against, say, multiple multiplay games
           happening on the same machine - we would need to pass the packet's
           SenderID through the wifi module for that. */

        const u32 unread = (MPStatus.ReplyWriteOffset + kReplyQueueSize - ReplyReadOffset[inst])
                           % kReplyQueueSize;

        /* Read the queue's headers without consuming them, so that the entries
           behind the write offset are known before any of them is dropped. */
        auto peek = [this](u32 off, void* buf, u32 len) -> u32
        {
            if ((off + len) >= kReplyQueueSize)
            {
                const u32 part1 = kReplyQueueSize - off;
                memcpy(buf, &MPReplyQueue[off], part1);
                memcpy(&((u8*)buf)[part1], MPReplyQueue, len - part1);
                return len - part1;
            }
            memcpy(buf, &MPReplyQueue[off], len);
            return off + len;
        };

        u32 entries = 0;
        for (u32 off = ReplyReadOffset[inst];
             (off != MPStatus.ReplyWriteOffset) && (entries < 1024); entries++)
        {
            MPPacketHeader h = {};
            off = peek(off, &h, sizeof(h));
            off = (off + h.Length) % kReplyQueueSize;
        }

        /* Pre-fork arm (MD_STOCK_MP=1, see stock_mp above RecvHostPacket): a
           new command drops every unread answer, which is exactly what stock's
           single `ReplyReadOffset[inst] = MPStatus.ReplyWriteOffset;` line
           did - the reference half of the 2026-09-28 A/B. */
        int keep = stock_mp() ? 0 : reply_keep();
        if (keep > 0)
        {
            int peers = 0;
            for (int i = 0; i < 16; i++)
                if ((i != inst) && (MPStatus.ConnectedBitmask & (1 << i))) peers++;
            keep *= peers;
        }
        else keep = 0;

        const u32 to_drop = (entries > (u32)keep) ? (entries - (u32)keep) : 0;

        /* Drop the oldest entries only - keep what the rounds that just closed
           were asking for. */
        for (u32 i = 0; i < to_drop; i++)
        {
            MPPacketHeader h = {};
            ReplyReadOffset[inst] = peek(ReplyReadOffset[inst], &h, sizeof(h));
            ReplyReadOffset[inst] = (ReplyReadOffset[inst] + h.Length) % kReplyQueueSize;
        }

        if (trace_replies() && ((unread > 0) || (entries > 0)))
            Log(LogLevel::Debug,
                "reply window: starting a new command for p%d with %u unread %s of replies - "
                "dropped %u, kept %u (MD_REPLY_KEEP=%d)\n",
                inst + 1, entries, (entries == 1) ? "answer" : "answers",
                to_drop, entries - to_drop, reply_keep());

        ReplyDiag[inst].reanchor_dropped += to_drop;

        Semaphore_Reset(SemPool[16 + inst]);
        if (entries > to_drop)
            Semaphore_Post(SemPool[16 + inst], (int)(entries - to_drop));
    }
    else if (type == 2)
    {
        MPStatus.MPReplyBitmask |= (1 << inst);
        ReplyDiag[inst].replies_written++;
    }

    Mutex_Unlock(MPQueueLock);

    if (type == 2)
    {
        Semaphore_Post(SemPool[16 +  MPStatus.MPHostinst]);
    }
    else
    {
        for (int i = 0; i < 16; i++)
        {
            if (mask & (1<<i))
                Semaphore_Post(SemPool[i]);
        }
    }

    return len;
}

int LocalMP::RecvPacketGeneric(int inst, u8* packet, bool block, u64* timestamp) noexcept
{
    for (;;)
    {
        if (!Semaphore_TryWait(SemPool[inst], block ? RecvTimeout : 0))
        {
            return 0;
        }

        Mutex_Lock(MPQueueLock);

        MPPacketHeader pktheader = {};
        FIFORead(inst, 0, &pktheader, sizeof(pktheader));

        if (pktheader.Magic != 0x4946494E)
        {
            Log(LogLevel::Warn, "PACKET FIFO OVERFLOW\n");
            PacketReadOffset[inst] = MPStatus.PacketWriteOffset;
            Semaphore_Reset(SemPool[inst]);
            Mutex_Unlock(MPQueueLock);
            return 0;
        }

        if (pktheader.SenderID == inst)
        {
            // skip this packet
            PacketReadOffset[inst] += pktheader.Length;
            if (PacketReadOffset[inst] >= kPacketQueueSize)
                PacketReadOffset[inst] -= kPacketQueueSize;

            Mutex_Unlock(MPQueueLock);
            continue;
        }

        if (pktheader.Length)
        {
            FIFORead(inst, 0, packet, pktheader.Length);

            if (pktheader.Type == 1)
                LastHostID = pktheader.SenderID;
        }

        if (timestamp) *timestamp = pktheader.Timestamp;
        Mutex_Unlock(MPQueueLock);
        return pktheader.Length;
    }
}

int LocalMP::SendPacket(int inst, u8* packet, int len, u64 timestamp)
{
    return SendPacketGeneric(inst, 0, packet, len, timestamp);
}

int LocalMP::RecvPacket(int inst, u8* packet, u64* timestamp)
{
    return RecvPacketGeneric(inst, packet, false, timestamp);
}

int LocalMP::SendCmd(int inst, u8* packet, int len, u64 timestamp)
{
    return SendPacketGeneric(inst, 1, packet, len, timestamp);
}

int LocalMP::SendReply(int inst, u8* packet, int len, u64 timestamp, u16 aid)
{
    return SendPacketGeneric(inst, 2 | (aid<<16), packet, len, timestamp);
}

int LocalMP::SendAck(int inst, u8* packet, int len, u64 timestamp)
{
    return SendPacketGeneric(inst, 3, packet, len, timestamp);
}

/* Reply-window diagnostics, on with MD_LINK_VERBOSE=1. A reply the host throws
   away as "stale" is indistinguishable from one that never arrived, so the age
   that missed the window is printed too (negative = stamped later than the
   host's clock, i.e. the two consoles' US timers are out of phase). */
static bool trace_replies()
{
    static const bool on = []() {
        const char* v = std::getenv("MD_LINK_VERBOSE");
        return v && *v && (*v != '0');
    }();
    return on;
}

/* How far behind the host's own clock a client's reply may be and still be
   usable, in US-timer ticks - microseconds, since these timestamps are emulated
   time (NDS::GetSharedUSClock, see Wifi::USTimer).

   Stock's 32us is sensible on real hardware, where the client's US clock is
   slaved to the host's by the sync frames. Two emulated consoles are only
   locked to the same *frame*, so their US timers sit at whatever phase they
   reached in it and a good reply is routinely hundreds of microseconds "old" -
   measured on the 2026-09-25 MKDS join, replies arrived 64..4520us behind (a
   fixed phase offset, not loss) and a third were dropped, which the host
   reported to its game as `clientfail`. Two frames was the bound while an
   answer could only leave in the round *after* the one that asked; the answer
   now leaves in the poll's own round (see Wifi::MPReplyAID), and 4725 collected
   replies on 2026-09-27 were all inside one frame (p99 11016us, stale 0), so
   anything older really is a previous round's - which the re-anchor on sending
   a command discards anyway. */
constexpr u64 kReplyStaleWindow = 2 * 16715; // two emulated frames at 59.826fps, in us

/* How many already-arrived answers a new command keeps, per console it is
   asking. Zero restores stock's behaviour exactly (drop every unread answer
   when a command is sent).

   Two consoles are frame-locked but their US clocks are ~16ms apart, so the
   client drains the commands that arrived since its last poll in one go and
   answers a whole frame's worth of rounds as one clump; the window returns as
   soon as the console it asked has answered once, so a command that drops the
   rest unread loses the rounds whose ack then says `clientfail` (637 times in a
   5.7s live MKDS session, in bursts of 5-7). Measured on MKDS, one run per arm
   (2026-09-28): `MD_REPLY_KEEP=0` collects 2659 answers for a `clientfail` of
   239 and the host's game deauthenticates its client; with a retained answer the
   same route collects 11314 with `clientfail 8` and stays live. So the default
   is 1 - one answer per console survives a command, and it is the *newest*, so
   the reader can never be handed an answer older than one it has already had a
   chance to read (history.md v0.1.9n, and v0.1.8j for the unbounded-retention
   collapse the bound avoids).

   It cannot promise more than that: the client's replies carry no echo of the
   command's seqno, so which round an answer answers is known only by when it
   arrived. */
static int reply_keep()
{
    static const int keep = []() {
        const char* v = std::getenv("MD_REPLY_KEEP");
        if (v && *v) return (int)strtol(v, nullptr, 10);
        return 1;
    }();
    return keep;
}

/* The pre-fork transport, for the reference A/B the 2026-09-28 pass was asked
   to run: `MD_STOCK_MP=1` puts this file's two reply-queue decisions back to
   what stock melonDS does (drop every unread answer when a command goes out,
   and a 32-tick staleness window) so the same route can be driven through the
   same harness and instruments on both. It exists to answer one question -
   does MKDS's host game accept the join on stock transport behaviour and stall
   on ours - and the two arms must be run back to back, because the join is
   flaky on this title either way. */
static bool stock_mp()
{
    static const bool on = []() {
        const char* v = std::getenv("MD_STOCK_MP");
        return v && *v && (*v != '0');
    }();
    return on;
}

/* A/B switch for the FCS the wireless hardware generates into the frame's own
   trailing 4 bytes (see the note in SendPacketGeneric). Everything that does
   not set MD_MP_FCS=0 gets the computed FCS; `=0` leaves the game's own buffer
   bytes in that slot, which is what stock melonDS does and what the
   2026-09-28 A/B was measured against. */
static bool mp_fcs()
{
    static const bool on = []() {
        const char* v = std::getenv("MD_MP_FCS");
        return !(v && *v && (*v == '0'));
    }();
    return on;
}

/* The 802.11 frame check sequence: CRC-32 over the frame (header+body, i.e.
   everything before the FCS itself), transmitted least-significant byte first.
   Nothing in this emulation verifies it (nor does stock melonDS), and the
   console it is addressed to never reads it - its RX length entry excludes the
   FCS (see the note in SendPacketGeneric) - so this is computed so that the
   four bytes in the frame's FCS slot are the field the hardware would have put
   there, rather than whatever the game left in the buffer. */
static u32 mp_fcs32(const u8* frame, int len) noexcept
{
    u32 crc = 0xFFFFFFFF;
    for (int i = 0; i < len; i++)
    {
        crc ^= frame[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (u32)(-(int)(crc & 1)));
    }
    return ~crc;
}

int LocalMP::RecvHostPacket(int inst, u8* packet, u64* timestamp)
{
    if (LastHostID != -1)
    {
        // check if the host is still connected

        u16 curinstmask = MPStatus.ConnectedBitmask;

        if (!(curinstmask & (1 << LastHostID)))
            return -1;
    }

    return RecvPacketGeneric(inst, packet, true, timestamp);
}

u16 LocalMP::RecvReplies(int inst, u8* packets, u64 timestamp, u16 aidmask)
{
    u16 ret = 0;
    u16 myinstmask = (1 << inst);
    u16 curinstmask;

    curinstmask = MPStatus.ConnectedBitmask;

    // if all clients have left: return early
    if ((myinstmask & curinstmask) == curinstmask)
    {
        /* The quietest way for a join to fail: the host is still sending CMD
           frames and expecting replies (its client mask is not empty), but the
           bus believes no other console is attached, so it answers "no
           replies" without ever looking at the queue. The host then reports
           every client as failed in its ack, and the game's client shows a
           join error while every frame of the attempt looks fine on the wire.
           If this line appears while a game is hosting, the two views of who
           is attached have come apart - look at Begin/End, not at the reply
           queue. */
        if (trace_replies())
            Log(LogLevel::Debug,
                "reply window: not even looking for p%d's replies - the bus has no other console "
                "attached (asked for mask %04X, connected %04X)\n",
                inst + 1, aidmask, curinstmask);
        ReplyDiag[inst].not_looking++;
        return 0;
    }

    for (;;)
    {
        /* Waiting for a reply is wall-clock time spent inside an emulated
           frame, so it is only allowed to eat the slack the frame had anyway.
           Spending it freely once took a live MKDS join (1436 commands, 363
           empty windows) down to 9fps, because every peer console then waits
           for the slow one at the lockstep barrier and the host's own reply
           latency grows with the cost of a frame - the wait feeds itself. With
           a budget published per frame, an answer the peer cannot produce
           inside this frame's slack is an answer this frame does not get. */
        int waitms = ReplyTimeout;
        const long long budget_ns = GetReplyBudgetLeft(inst);
        if (budget_ns >= 0)
        {
            const int budget_ms = (int)(budget_ns / 1000000);
            if (budget_ms < waitms)
            {
                if (trace_replies())
                    Log(LogLevel::Debug,
                        "reply window: p%d would have waited %dms for a reply, but this frame only has "
                        "%lldus of slack left (nothing waiting, collected=%04X so far)\n",
                        inst + 1, waitms, budget_ns / 1000, ret);
                waitms = budget_ms;
            }
        }

        if (!Semaphore_TryWait(SemPool[16+inst], waitms))
        {
            // no more replies available
            if (trace_replies())
                Log(LogLevel::Debug, "reply window closed for p%d with collected=%04X\n",
                    inst + 1, ret);
            if (!ret) ReplyDiag[inst].windows_empty++;
            return ret;
        }

        Mutex_Lock(MPQueueLock);

        MPPacketHeader pktheader = {};
        FIFORead(inst, 1, &pktheader, sizeof(pktheader));

        if (trace_replies())
            Log(LogLevel::Debug,
                "reply window: p%d read a packet from p%d, %d bytes, aged %lld US-timer ticks "
                "(needs to be inside %llu)\n",
                inst + 1, pktheader.SenderID + 1, pktheader.Length,
                (long long)(timestamp - pktheader.Timestamp),
                (unsigned long long)kReplyStaleWindow);

        if (pktheader.Magic != 0x4946494E)
        {
            Log(LogLevel::Warn, "REPLY FIFO OVERFLOW\n");
            ReplyReadOffset[inst] = MPStatus.ReplyWriteOffset;
            Semaphore_Reset(SemPool[16 + inst]);
            Mutex_Unlock(MPQueueLock);
            return 0;
        }

        /* Stock's window was 32 ticks; this fork's is two frames (see
           kReplyStaleWindow). MD_STOCK_MP=1 restores stock's. */
        const u64 stalewindow = stock_mp() ? 32 : kReplyStaleWindow;

        if ((pktheader.SenderID == inst) || // packet we sent out (shouldn't happen, but hey)
            (pktheader.Timestamp + stalewindow < timestamp)) // stale packet
        {
            if (pktheader.SenderID != inst)
                ReplyDiag[inst].stale_dropped++;

            if (trace_replies() && pktheader.SenderID != inst)
                Log(LogLevel::Warn,
                    "reply window: DROPPED a reply for being %lld US-timer ticks stale \
(sender p%d, window %llu) - the client answered and the host could not use it\n",
                    (long long)(timestamp - pktheader.Timestamp), pktheader.SenderID + 1,
                    (unsigned long long)kReplyStaleWindow);

            // skip this packet
            ReplyReadOffset[inst] += pktheader.Length;
            if (ReplyReadOffset[inst] >= kReplyQueueSize)
                ReplyReadOffset[inst] -= kReplyQueueSize;

            Mutex_Unlock(MPQueueLock);
            continue;
        }

        /* A zero length entry is the blank a client sends so the host has
           something to receive. RecvReplies deliberately does not credit it
           (`if (pktheader.Length)` below), which is why a run can show the
           guest answering every round and the host's acks still reporting it
           as failed - so the two are counted apart. */
        if (!pktheader.Length)
            ReplyDiag[inst].collected_blank++;

        if (pktheader.Length)
        {
            u32 aid = (pktheader.Type >> 16);

            /* The aid is stamped by the *sender* (`SendMPReply` takes it from
               that console's own AID register), and 0 is a value it really
               reads: MKDS clears W_AIDLow while it prepares the next answer.
               The destination here is `packets[(aid-1)*1024]`, so an aid of 0
               is a write 1KB *before* the caller's buffer - it segfaulted the
               app on the first live run that sent a reply in that window
               (2026-09-27). An entry that cannot be filed is dropped here and
               the round goes to the game as an unanswered one, which is what
               the host does with any answer it cannot credit. */
            if ((aid < 1) || (aid > 15))
            {
                Log(LogLevel::Warn,
                    "reply window: p%d read a %d byte reply stamped with an unusable aid %u - "
                    "dropped, not credited\n", inst + 1, pktheader.Length, aid);
                Mutex_Unlock(MPQueueLock);
                continue;
            }

            FIFORead(inst, 1, &packets[(aid-1)*1024], pktheader.Length);
            ret |= (1 << aid);
            ReplyDiag[inst].collected++;

            if (trace_replies())
            {
                /* The collected bytes, so the client's own `[mp] answering cmd`
                   line can be compared with what the host's game is about to
                   read. If the two disagree, the reply was fine and the
                   transport mangled it; if they agree, the games themselves are
                   the ones not agreeing.

                   What used to be printed here was `payload=%016llX` read from
                   reply[12+24], i.e. an 8-byte read at payload offset 24. The
                   DS's local-MP reply frame carries a 24-byte 802.11 header and
                   a 6-byte body, so that read ran two bytes off the end of the
                   frame and printed zero for *every* reply - including the
                   healthy ones on the NSMB harness, where it answered this
                   question with "0000000000000000" 1512 times in a row. Print
                   the body for real instead, clamped to the frame. */
                const u8* reply = &packets[(aid - 1) * 1024];
                const int framelen = pktheader.Length - 12; // the 12 is Wifi's TX prefix
                const u16 fc = (framelen >= 2) ? (u16)(reply[12] | (reply[13] << 8)) : 0;
                const int bodyoff = 12 + 24;
                const int bodylen = (framelen > 24) ? (framelen - 24) : 0;

                char body[3 * 16 + 1] = "-";
                if (bodylen > 0)
                {
                    int n = (bodylen > 16) ? 16 : bodylen;
                    for (int i = 0; i < n; i++)
                        snprintf(&body[i * 3], 4, "%02X ", reply[bodyoff + i]);
                    body[n * 3 - 1] = 0;
                }

                Log(LogLevel::Debug,
                    "reply window: p%d collected a reply from p%d for aid %d, %d byte frame, "
                    "fc=%04X, seq=%04X, body[%d]=%s\n",
                    inst + 1, pktheader.SenderID + 1, aid, framelen, fc,
                    (framelen >= 24) ? (u16)(reply[12 + 22] | (reply[12 + 23] << 8)) : 0,
                    bodylen, body);
            }
        }

        myinstmask |= (1 << pktheader.SenderID);
        if (((myinstmask & curinstmask) == curinstmask) ||
            ((ret & aidmask) == aidmask))
        {
            // all the clients have sent their reply

            if (trace_replies())
                Log(LogLevel::Debug,
                    "reply window: p%d has everyone it asked for (collected=%04X of mask %04X)\n",
                    inst + 1, ret, aidmask);

            Mutex_Unlock(MPQueueLock);
            return ret;
        }

        Mutex_Unlock(MPQueueLock);
    }
}

}


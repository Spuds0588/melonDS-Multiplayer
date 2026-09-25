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
    FIFOWrite(inst, nfifo, &pktheader, sizeof(pktheader));
    if (len)
        FIFOWrite(inst, nfifo, packet, len);

    if (type == 1)
    {
        // NOTE: this is not guarded against, say, multiple multiplay games happening on the same machine
        // we would need to pass the packet's SenderID through the wifi module for that
        MPStatus.MPHostinst = inst;
        MPStatus.MPReplyBitmask = 0;

        /* Sending a command re-anchors the host's reply read offset, which
           decides which answers this round is allowed to see.

           Dropping every unread answer here is load-bearing in one direction:
           it is what makes a window read the *newest* answer instead of an
           older one, and keeping them all instead is a recorded negative result
           (2026-09-25, history.md v0.1.8j) - on the NSMB harness the same script
           went from 1525 collected replies and 2502 commands to 198 collected
           and 16626 commands, with 15015 replies dropped as stale, because the
           backlog grew without limit and every window then read a reply from an
           earlier round first.

           What is kept here is bounded: at most `MD_REPLY_KEEP` (default 1)
           answers per console the host is asking. That covers the case the runs
           actually show - the peer answers a whole frame's worth of rounds in
           one clump, this queue's readers take one answer per round, and the
           next command used to discard the rest of the clump unread, which the
           ack then reported to the game as `clientfail`. The bound is what
           keeps the v0.1.8j collapse out: the queue can never hold more than
           the round that just closed.

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

        int keep = reply_keep();
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

        Semaphore_Reset(SemPool[16 + inst]);
        if (entries > to_drop)
            Semaphore_Post(SemPool[16 + inst], (int)(entries - to_drop));
    }
    else if (type == 2)
    {
        MPStatus.MPReplyBitmask |= (1 << inst);
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

/* Reply-window diagnostics, on with MD_LINK_VERBOSE=1.

   A reply the host throws away for being "stale" is indistinguishable from one
   that never arrived: both leave the collection empty, and a game whose client
   cannot join looks identical either way. The 2026-09-25 MKDS attempt asked for
   replies 289 times, collected none 139 of those times, while the client sent
   267 replies - so the difference is worth printing, together with the age that
   missed the window. A negative age means the reply was stamped *later* than
   the host's clock, i.e. the two consoles' US timers are out of phase. */
static bool trace_replies()
{
    static const bool on = []() {
        const char* v = std::getenv("MD_LINK_VERBOSE");
        return v && *v && (*v != '0');
    }();
    return on;
}

/* How far behind the host's own clock a client's reply may be and still be
   usable, in US-timer ticks - which are microseconds (a DS frame takes
   `framelen * 8` ticks at 1Mbps, `* 4` at 2Mbps - see Wifi::SendMPFrame).

   This was 32, i.e. 32us, which is sensible on real hardware: there the
   client's US clock is slaved to the host's by the sync frames it receives, so
   a reply genuinely belongs to the round being collected. Two emulated
   consoles in one process are only locked to the same *frame* (melonDS
   Multiplayer's lockstep barrier) and their US timers sit at whatever phase
   within that frame they reached, so a perfectly good reply is routinely a few
   hundred microseconds "old" - and the host was throwing nearly all of them
   away.

   Measured on the 2026-09-25 MKDS join: replies arrived 64..4520us behind the
   host's clock (in a sawtooth, i.e. a fixed phase offset, not loss), a third of
   them were dropped with this window at 32, 63 collection rounds came back
   empty, the host's own acks reported `clientfail=0002` ("client 1 did not
   reply"), and the client deauthenticated 1.8s in - the error the user saw.
   One frame is the honest bound: both consoles are within a frame of each
   other by construction, and anything older is from a previous round anyway -
   sending a command (type 1 below) already re-anchors the host's reply read
   offset, which is what really discards last round's replies. */
constexpr u64 kReplyStaleWindow = 16715; // one emulated frame at 59.826fps, in us

/* How many already-arrived answers a new command keeps, per console it is
   asking. Zero restores the previous behaviour exactly (drop every unread
   answer when a command is sent).

   Dropping the lot made sense while a window could only ever be looking at an
   *older* round's answer: a command re-anchors the read offset to the write
   offset, so whatever arrives next is the newest thing the peer has said. The
   2026-09-25 runs show the other case, and it is the common one here. Two
   consoles are frame-locked but their US clocks are ~16ms apart, so the client
   drains the commands that arrived since its last poll in one go and answers a
   whole frame's worth of rounds as one clump. The window returns as soon as the
   console it asked has answered once (`(ret & aidmask) == aidmask`), so it
   credits one round with the clump's first answer and the next command discards
   the rest unread - 637 times in a 5.7s live MKDS session, in bursts of 5-7 - and
   the rounds it discarded are exactly the rounds whose ack says
   `clientfail=0002`. A host that tells its game its client failed 44% of the
   time, while the client answered every single round, is reporting a defect of
   this queue rather than of the link.

   Keeping a bounded backlog cannot reintroduce the unbounded-retention collapse
   recorded in history.md v0.1.8j (198 collected / 16626 commands on NSMB): that
   one let the backlog grow without limit, so every window read a much older
   round first. The default of 2 covers the clump sizes a live join actually
   produces (717 clumps of 1, 137 of 2, 13 of 3, 2 of 4 and 1 of 5 on the NSMB
   script) and the retain/drop decision is logged, so what is being given up is
   visible either way.

   NSMB two-player script, same save, 12000 frames, collected / empty windows:
   0 (stock) 1524/1456, 1 1904/1072, 2 1987/989, 8 2016/959, 16 2023/952 -
   zero stale drops at every setting, and the runs stop spending wall clock
   waiting (5.4-6.1x realtime against 3.0x). Past 2 it is flat, so 2 is the
   default rather than a bigger number: the residual third of empty windows is
   an answer that does not exist yet, not one being thrown away. */
static int reply_keep()
{
    static const int keep = []() {
        const char* v = std::getenv("MD_REPLY_KEEP");
        if (v && *v) return (int)strtol(v, nullptr, 10);
        return 2;
    }();
    return keep;
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

        if ((pktheader.SenderID == inst) || // packet we sent out (shouldn't happen, but hey)
            (pktheader.Timestamp + kReplyStaleWindow < timestamp)) // stale packet
        {
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

        if (pktheader.Length)
        {
            u32 aid = (pktheader.Type >> 16);
            FIFORead(inst, 1, &packets[(aid-1)*1024], pktheader.Length);
            ret |= (1 << aid);

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
                    "fc=%04X, body[%d]=%s\n",
                    inst + 1, pktheader.SenderID + 1, aid, framelen, fc, bodylen, body);
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


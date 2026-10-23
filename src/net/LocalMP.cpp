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

/*
   The local-multiplayer bus, and who owns what.

   This file used to be one process-wide record - one packet FIFO every console
   read from its own offset, one reply FIFO one (implicit) host read, one
   LastHostID, one MPHostinst, one MPReplyBitmask nothing read - that every
   console in the process read and wrote. With two consoles that is a bus; with
   three it is a record in which a second client's peer and reply state has no
   owner: which console an answer belongs to was decided by the *arrival order
   of a shared queue*, and "the console I am following" was one value for the
   whole process. Measured at three consoles: the host's own beacon party list
   never grew past two names while both clients were associated, polled and
   credited (history.md v0.1.9bg).

   Now every console owns its own state (`LocalMP::Console`) and one function
   decides what one console is told about another (`LocalMP::Route`):

     * a frame a console sends goes to every attached console except the
       sender - the air carries it to every station, which is where our job
       stops: which frames a *station* acts on is the wireless hardware's
       decision and stays in Wifi (address filtering, BSSID, and so on);
     * an answer goes to the console whose command the answering console is
       following. That is the one routing decision only the bus can make,
       because only the bus knows who is following whom, and it is what the
       shared reply queue could not express: it had no owner to attribute an
       answer to, so a second client's answer was an entry in a queue whose
       reader was whichever instance the last command came from.

   The interface the frontends use is unchanged: Begin/End/Reset, the seven
   send/receive calls, GetReplyDiag and the timeout/budget setters.
*/

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "LocalMP.h"

#include <chrono>

using namespace melonDS;
using namespace melonDS::Platform;

namespace melonDS {
static bool trace_replies();
static int reply_keep();
static int reply_extend_frames();
static bool stock_mp();
static bool mp_fcs();
static u32 mp_fcs32(const u8* frame, int len) noexcept;
}

using Platform::Log;
using Platform::LogLevel;

namespace
{
/* ---- ring helpers ---------------------------------------------------------

   Both queues are byte rings with their cursor and their length. Frames are at
   most kMaxFrameSize and every ring is much larger than one frame, so an entry
   never wraps more than once. One byte of every ring is left spare: `read ==
   write` is the empty test, so a ring filled to the brim would read as empty. */

u32 RingAppend(u8* ring, u32 ringlen, u32 write, const void* data, u32 len) noexcept
{
    if (!len) return write;

    const u32 part1 = ringlen - write;
    if (len >= part1)
    {
        memcpy(&ring[write], data, part1);
        memcpy(ring, (const u8*)data + part1, len - part1);
        return len - part1;
    }

    memcpy(&ring[write], data, len);
    return write + len;
}

u32 RingRead(const u8* ring, u32 ringlen, u32 read, void* buf, u32 len) noexcept
{
    const u32 part1 = ringlen - read;
    if (len >= part1)
    {
        memcpy(buf, &ring[read], part1);
        memcpy((u8*)buf + part1, ring, len - part1);
        return len - part1;
    }

    memcpy(buf, &ring[read], len);
    return read + len;
}

/* One entry: the bus header, then the frame bytes the sender handed over. */
u32 RingAppendEntry(u8* ring, u32 ringlen, u32 write,
                    const MPPacketHeader& h, const u8* body) noexcept
{
    write = RingAppend(ring, ringlen, write, &h, sizeof(h));
    return RingAppend(ring, ringlen, write, body, h.Length);
}

/* How much of the ring between `read` and `write` is holding entries. */
u32 RingUsed(u32 ringlen, u32 read, u32 write) noexcept
{
    return (write + ringlen - read) % ringlen;
}

/* Move the read cursor past the oldest whole entry - header first, so the
   entry's own length says where the next one starts. */
u32 RingDropOldest(const u8* ring, u32 ringlen, u32 read) noexcept
{
    MPPacketHeader h = {};
    const u32 body = RingRead(ring, ringlen, read, &h, sizeof(h));
    return (body + h.Length) % ringlen;
}
}

namespace melonDS
{

LocalMP::LocalMP() noexcept :
    MPQueueLock(Mutex_Create())
{
    memset(Consoles, 0, sizeof(Consoles));

    /* One receive semaphore and one answer semaphore per console: the receive
       one counts frames routed to that console, the answer one counts answers
       filed with it, and neither can be posted by anything but the console that
       owns it. */
    for (int i = 0; i < kMaxMPConsoles; i++)
    {
        Consoles[i].frameSem = Semaphore_Create();
        Consoles[i].answerSem = Semaphore_Create();

        /* -1, not the zero the memset above leaves: "no console has stamped
           this AID yet" is a state the collision check reads, and 0 would make
           the first guest that answers as AID 1 look like p1's own answer. */
        for (int a = 0; a < 16; a++) Consoles[i].aid_owner[a] = -1;
    }

    Log(LogLevel::Info, "MP comm init OK\n");
}

LocalMP::~LocalMP() noexcept
{
    for (int i = 0; i < kMaxMPConsoles; i++)
    {
        Semaphore_Free(Consoles[i].frameSem);
        Semaphore_Free(Consoles[i].answerSem);
        Consoles[i].frameSem = nullptr;
        Consoles[i].answerSem = nullptr;
    }

    Mutex_Free(MPQueueLock);
}

void LocalMP::Begin(int inst)
{
    if ((inst < 0) || (inst >= kMaxMPConsoles)) return;

    Mutex_Lock(MPQueueLock);

    Console& self = Consoles[inst];
    self.attached = true;

    /* Start from now: nothing can be queued for a console that has just come
       back, because the router only writes to consoles that are attached. */
    self.inboxRead = self.inboxWrite;
    self.answerRead = self.answerWrite;
    self.hostInst = -1;

    Semaphore_Reset(self.frameSem);
    Semaphore_Reset(self.answerSem);

    /* A new session for this console starts its half of the flight recorder
       from zero, so a digest read later cannot carry the run before it.

       The delivery ledger deliberately does *not* restart here: it is the
       router's own count of what it handed this console, so it lives as long
       as the counters beside it (a console's reads are not zeroed by a wireless
       power cycle either), and `reads <= delivered` stays true across one. A
       Link test compares the ledger before and after its own send, so a
       private epoch would buy nothing - while a log line showing more reads
       than deliveries reads as a contradiction. */
    memset(&self.diag, 0, sizeof(self.diag));
    memset(self.answer_us, 0, sizeof(self.answer_us));

    Mutex_Unlock(MPQueueLock);
}

bool LocalMP::IsAttached(int inst) const noexcept
{
    if ((inst < 0) || (inst >= kMaxMPConsoles)) return false;
    /* A single bool read, the same tolerance the other diagnostic reads in
       this class have: the answer is a state, and it is re-asked every tick. */
    return Consoles[inst].attached;
}

void LocalMP::End(int inst)
{
    if ((inst < 0) || (inst >= kMaxMPConsoles)) return;

    Mutex_Lock(MPQueueLock);
    Consoles[inst].attached = false;
    Mutex_Unlock(MPQueueLock);
}

void LocalMP::Reset()
{
    Mutex_Lock(MPQueueLock);

    for (int i = 0; i < kMaxMPConsoles; i++)
    {
        Console& c = Consoles[i];
        c.attached = false;
        c.inboxRead = 0;
        c.inboxWrite = 0;
        c.answerRead = 0;
        c.answerWrite = 0;
        c.hostInst = -1;
        c.frames_delivered = 0;
        c.bytes_delivered = 0;
        memset(c.inbox, 0, kPacketQueueSize);
        memset(c.answers, 0, kReplyQueueSize);
        memset(c.answer_us, 0, sizeof(c.answer_us));
        for (int a = 0; a < 16; a++) c.aid_owner[a] = -1;
        memset(c.aid_owner_us, 0, sizeof(c.aid_owner_us));
        memset(c.aid_collision_logged, 0, sizeof(c.aid_collision_logged));
        memset(&c.diag, 0, sizeof(c.diag));
    }

    Mutex_Unlock(MPQueueLock);

    /* Semaphores are reset outside the queue lock: Begin() does the same, and
       each semaphore carries its own lock. */
    for (int i = 0; i < kMaxMPConsoles; i++)
    {
        Semaphore_Reset(Consoles[i].frameSem);
        Semaphore_Reset(Consoles[i].answerSem);
    }
}

void LocalMP::GetReplyDiag(int inst, MPReplyDiag* out) const noexcept
{
    if (!out) return;
    if ((inst < 0) || (inst >= kMaxMPConsoles))
    {
        memset(out, 0, sizeof(*out));
        return;
    }

    Mutex_Lock(MPQueueLock);
    *out = Consoles[inst].diag;
    Mutex_Unlock(MPQueueLock);
}

void LocalMP::GetDelivered(int inst, u64* packets, u64* bytes) const noexcept
{
    if (packets) *packets = 0;
    if (bytes) *bytes = 0;
    if ((inst < 0) || (inst >= kMaxMPConsoles)) return;

    Mutex_Lock(MPQueueLock);
    if (packets) *packets = Consoles[inst].frames_delivered;
    if (bytes) *bytes = Consoles[inst].bytes_delivered;
    Mutex_Unlock(MPQueueLock);
}

u64 LocalMP::GetInboxDrops(int inst) const noexcept
{
    if ((inst < 0) || (inst >= kMaxMPConsoles)) return 0;

    Mutex_Lock(MPQueueLock);
    const u64 drops = Consoles[inst].frames_dropped;
    Mutex_Unlock(MPQueueLock);
    return drops;
}

/* --------------------------------------------------------------- the router */

u32 LocalMP::QueueAppend(int owner, const char* name, u8* ring, u32 ringlen,
                         u32& read, u32& write, const MPPacketHeader& h, const u8* body,
                         u64& dropped) noexcept
{
    const u32 need = (u32)(sizeof(MPPacketHeader) + h.Length);
    u32 went = 0;

    /* Make room for this entry by dropping the *oldest* entries this console
       has not read. It is the one bounded thing the router does to a console's
       own state, and it is bounded in the direction that costs least: a console
       whose game has stopped reading (its radio is on, its game is in a menu,
       it is being written to faster than it drains) loses the frames it was
       never going to read in time, and keeps the newest ones - the same frames
       the air would still be carrying if it came back now.

       What this replaces is the plain wrap-around write, which let the write
       cursor run over unread bytes. The reader then took its next header out of
       the middle of an entry: magic mismatch, `PACKET FIFO OVERFLOW`, and the
       whole queue dropped with its cursor snapped to the writer - so a console
       that stopped reading for a few seconds lost *everything* the others had
       sent it in that time, whole frames included, and could be handed a frame
       stitched out of two entries on the way there. A lagging console is not a
       corrupted one. */
    if (need < ringlen)
    {
        while ((RingUsed(ringlen, read, write) + need) >= ringlen)
        {
            read = RingDropOldest(ring, ringlen, read);
            went++;
        }
    }

    if (went)
    {
        const u64 before = dropped;
        dropped += went;

        /* Said once when it starts, then once per 1024 dropped, so a console
           that lags for a minute cannot bury the log in one line per frame. */
        if ((before == 0) || ((dropped / 1024) != (before / 1024)))
            Log(LogLevel::Warn,
                "%s: p%d is not keeping up - dropped its %llu oldest unread frame%s to make room "
                "for the newest (queue %u of %u bytes, %llu dropped in all). The frames it still "
                "holds are whole; what it loses is what it was not going to read anyway.\n",
                name, owner, (unsigned long long)went, (went == 1) ? "" : "s",
                (unsigned)RingUsed(ringlen, read, write), (unsigned)ringlen,
                (unsigned long long)dropped);
    }

    write = RingAppendEntry(ring, ringlen, write, h, body);
    return went;
}

void LocalMP::Route(int sender, u32 type, const u8* frame, int len, u64 timestamp,
                    Platform::Semaphore** posts, int* nposts) noexcept
{
    const u32 kind = type & 0xFFFF;
    const u16 aid = (u16)(type >> 16);

    if ((*nposts) >= kMaxMPConsoles) return;

    if (kind == 2)
    {
        /* An answer: it belongs to the console whose command its sender is
           following, filed under the AID the answer was stamped with. A console
           with no host to answer writes into nothing - it has no reader, and
           saying so here is the whole point of the router. */
        const int host = Consoles[sender].hostInst;

        if ((host < 0) || (host == sender) || !Consoles[host].attached)
        {
            if (trace_replies())
                Log(LogLevel::Debug,
                    "reply window: p%d's answer went nowhere - it is not following any console "
                    "that is still attached (host %d)\n", sender + 1, host);
            return;
        }

        Console& c = Consoles[host];
        MPPacketHeader h { 0x4946494E, (u32)sender, type, (u32)len, timestamp };
        QueueAppend(host + 1, "answers", c.answers, kReplyQueueSize,
                    c.answerRead, c.answerWrite, h, frame, c.diag.reanchor_dropped);
        c.frames_delivered++;
        c.bytes_delivered += (u64)len;
        posts[(*nposts)++] = c.answerSem;
        return;
    }

    /* A frame on the bus: everyone attached hears it except whoever sent it. */
    MPPacketHeader h { 0x4946494E, (u32)sender, type, (u32)len, timestamp };

    for (int i = 0; i < kMaxMPConsoles; i++)
    {
        if ((i == sender) || !Consoles[i].attached) continue;

        Console& c = Consoles[i];
        QueueAppend(i + 1, "inbox", c.inbox, kPacketQueueSize,
                    c.inboxRead, c.inboxWrite, h, frame, c.frames_dropped);

        /* Counted here, at the hand-over, because this is the only moment the
           bus can say a frame reached this console: whether the console's own
           game reads it, or a scripted drain does, or neither, comes later and
           is a different question (see GetDelivered). */
        c.frames_delivered++;
        c.bytes_delivered += (u64)len;

        /* A command is what makes the console it reaches a client of its
           sender; the reader takes the host over when it reads it (see
           RecvPacketGeneric), so the peer state follows the game's own use of
           the frame rather than the moment it was routed. */
        posts[(*nposts)++] = c.frameSem;
        if (*nposts >= kMaxMPConsoles) return;
    }
}

/* ----------------------------------------------------- answers, per console */

u32 LocalMP::TrimAnswers(int inst, int keep, u32* kept_out) noexcept
{
    Console& self = Consoles[inst];

    if (self.answerRead == self.answerWrite) return 0;

    /* Walk the queue's entries without consuming them: for each, who sent it
       and where the next one starts. */
    struct Entry { u32 next; int sender; };
    static constexpr u32 kMaxEntries = 1024;
    Entry entries[kMaxEntries];

    u32 n = 0;
    for (u32 off = self.answerRead; (off != self.answerWrite) && (n < kMaxEntries); n++)
    {
        MPPacketHeader h = {};
        /* `RingRead` returns the offset the header ended at - the entry's body
           starts there, and the next entry starts `Length` past it. */
        const u32 body = RingRead(self.answers, kReplyQueueSize, off, &h, sizeof(h));

        entries[n].sender = (int)h.SenderID;
        entries[n].next = (body + h.Length) % kReplyQueueSize;
        off = entries[n].next;
    }

    /* What has to be dropped: for every sender, everything before its own
       oldest of the `keep` newest entries. The cursor is a read position, so
       only a prefix can go, and this is the smallest prefix that satisfies
       every sender at once. */
    u32 drop = 0;
    for (u32 i = 0; i < n; i++)
    {
        u32 after = 0;
        for (u32 j = i; j < n; j++)
            if (entries[j].sender == entries[i].sender) after++;

        if (after > (u32)keep)
            drop = i + 1;
    }

    /* ... and a prefix that would take a sender's own last answer is not
       allowed to: an answer that is all its sender has is not surplus, and
       dropping it hands that console's round to its game as a failed client
       *after* the answer arrived. One console answering twice (its US clock is
       offset, so it answers a whole frame's worth of rounds in one clump) used
       to cost every console queued in front of it its only answer, which the
       host's game then reported as that client failing. The prefix shrinks
       until every entry it takes is surplus for its own sender; `keep` of 0 is
       stock's policy and still takes everything. */
    while (drop > 0)
    {
        bool all_surplus = true;

        for (u32 i = 0; i < drop && all_surplus; i++)
        {
            u32 total = 0;  // how many answers this sender has in the queue
            u32 taken = 0;  // ... of which this prefix would drop

            for (u32 j = 0; j < n; j++)
                if (entries[j].sender == entries[i].sender) total++;
            for (u32 j = 0; j < drop; j++)
                if (entries[j].sender == entries[i].sender) taken++;

            const u32 survives = ((u32)keep < total) ? (u32)keep : total;
            if (taken > total - survives) all_surplus = false;
        }

        if (all_surplus) break;
        drop--;
    }

    u32 cursor = self.answerRead;
    for (u32 i = 0; i < drop; i++)
        cursor = entries[i].next;
    self.answerRead = cursor;
    self.diag.reanchor_dropped += drop;

    /* Who lost those answers. The counter alone says the host's queue re-anchored;
       the three-console room needs to know whether it cost one guest its round
       and not the other, because that is what a guest's game reacts to. */
    if (drop && trace_replies())
    {
        char who[64];
        size_t w = 0;
        who[0] = 0;
        for (u32 i = 0; i < drop; i++)
        {
            if ((w + 6) >= sizeof(who)) break;
            w += (size_t)snprintf(&who[w], sizeof(who) - w, " p%d", entries[i].sender + 1);
        }
        Log(LogLevel::Debug, "reply window: p%d trimmed %u stale answer(s) from%s\n",
            inst + 1, drop, who);
    }

    if (kept_out) *kept_out = n - drop;
    return drop;
}

/* ------------------------------------------------------------------ sending */

int LocalMP::SendPacketGeneric(int inst, u32 type, u8* packet, int len, u64 timestamp) noexcept
{
    if (len > kMaxFrameSize)
    {
        Log(LogLevel::Warn, "wifi: attempting to send frame too big (len=%d max=%d)\n", len, kMaxFrameSize);
        return 0;
    }

    const u32 kind = type & 0xFFFF;

    Mutex_Lock(MPQueueLock);

    if (kind == 2)
        Consoles[inst].diag.replies_written++;

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
    u8 fcsbuf[12 + kMaxFrameSize];
    const u8* body = packet;

    /* The rule itself lives in ApplyWireFCS, because the bridge's Link-test
       probe has to build the same frame the receiver will see (see
       LocalMP.h). */
    if (ApplyWireFCS(fcsbuf, packet, (size_t)len))
        body = fcsbuf;

    if (kind == 1)
    {
        /* A command re-anchors this console's own answer queue, which decides
           which answers this round is allowed to see. Keeping the newest
           `MD_REPLY_KEEP` (default 1) *per console it is asking* covers the peer
           answering a whole frame's worth of rounds in one clump; the bound is
           what keeps an unbounded backlog from making every window read an
           older round first (history.md v0.1.8j, and reply_keep below for the
           measurement). It is per peer now: the bound used to be handed out to
           the queue as a whole (`keep x peers` entries kept, oldest dropped
           regardless of who sent them), which is the same thing only while the
           peers answer in turn. */
        const int keep = stock_mp() ? 0 : reply_keep();
        u32 kept = 0;
        const u32 dropped = TrimAnswers(inst, keep, &kept);
        const u32 unread = kept + dropped;

        if (trace_replies() && (unread > 0))
            Log(LogLevel::Debug,
                "reply window: starting a new command for p%d with %u unread %s of replies - "
                "dropped %u, kept %u (MD_REPLY_KEEP=%d)\n",
                inst + 1, unread, (unread == 1) ? "answer" : "answers",
                dropped, kept, reply_keep());

        /* The answers this command kept are what its reply window opens with:
           one semaphore per surviving entry, so a window with more than one
           answer to give still returns them all. */
        Semaphore_Reset(Consoles[inst].answerSem);
        if (kept)
            Semaphore_Post(Consoles[inst].answerSem, (int)kept);

        /* Every asked client owes this round a reply, and "owes" is counted
           from that client's own send side: the bus knows how many replies it
           has routed for each console, so "has this client put anything on the
           wire since this command went out" needs no clock at all. The
           baseline is per AID because that is the identity the command's mask
           speaks in; the AID's owner is the console that last stamped it (see
           the note on aid_owner), so a client that has not answered since the
           room opened is outside the count for its first round only. */
        for (int a = 1; a < 16; a++)
        {
            const int owner = Consoles[inst].aid_owner[a];
            Consoles[inst].aid_baseline[a] =
                ((owner >= 0) && (owner < kMaxMPConsoles))
                    ? Consoles[owner].diag.replies_written : 0;
            Consoles[inst].aid_poll_baseline[a] =
                ((owner >= 0) && (owner < kMaxMPConsoles))
                    ? Consoles[owner].host_polls : 0;
        }
    }

    Platform::Semaphore* posts[kMaxMPConsoles];
    int nposts = 0;
    Route(inst, type, body, len, timestamp, posts, &nposts);

    Mutex_Unlock(MPQueueLock);

    for (int i = 0; i < nposts; i++)
        Semaphore_Post(posts[i]);

    return len;
}

int LocalMP::SendPacket(int inst, u8* packet, int len, u64 timestamp)
{
    return SendPacketGeneric(inst, 0, packet, len, timestamp);
}

int LocalMP::SendCmd(int inst, u8* packet, int len, u64 timestamp)
{
    return SendPacketGeneric(inst, 1, packet, len, timestamp);
}

int LocalMP::SendReply(int inst, u8* packet, int len, u64 timestamp, u16 aid)
{
    return SendPacketGeneric(inst, 2 | (aid << 16), packet, len, timestamp);
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

/* How many frames a reply window may outlive its ceiling for a client that has
   not yet taken its own round (see RecvReplies). Default 0: the measurement
   that introduced the gate showed the extra wait does not get the answer the
   window is waiting for - the answer arrives once the *host's* frame ends - so
   the ceiling stays the honest close until a run says otherwise. */
static int reply_extend_frames()
{
    static const int frames = []() {
        const char* v = std::getenv("MD_REPLY_EXTEND_FRAMES");
        if (v && *v) return (int)strtol(v, nullptr, 10);
        return 0;
    }();
    return frames;
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

bool LocalMP::ApplyWireFCS(u8* dst, const u8* src, size_t len) noexcept
{
    if (!dst || !src) return false;
    if (dst != src) memcpy(dst, src, len);

    /* The CRC covers the frame's header and body, so it is read before the
       slot that holds it is written - which is what makes applying this in
       place safe. */
    if (!mp_fcs() || (len < 12 + 4)) return false;

    const u32 fcs = mp_fcs32(&dst[12], (int)(len - 12 - 4));
    memcpy(&dst[len - 4], &fcs, sizeof(fcs));
    return true;
}

/* ---------------------------------------------------------------- receiving */

int LocalMP::RecvPacketGeneric(int inst, u8* packet, bool block, u64* timestamp) noexcept
{
    Console& self = Consoles[inst];

    /* A blocking poll is a guest asking for its host's next command, i.e. it
       opening one of its own emulated rounds. Counted before the wait: a poll
       that finds nothing is still a round this console took, and the host's
       reply window uses the count to tell "this client has not run its round
       yet" from "this client ran its round and chose not to answer". */
    if (block) self.host_polls++;

    if (!Semaphore_TryWait(self.frameSem, block ? RecvTimeout : 0))
        return 0;

    Mutex_Lock(MPQueueLock);

    /* The wakeup can outlive the entry it stood for: the router drops a
       lagging console's oldest unread frames to make room (see QueueAppend),
       and a count posted for one of those is still a count. Nothing to read is
       answered as nothing rather than read from wherever the cursor happens to
       point. */
    if (self.inboxRead == self.inboxWrite)
    {
        Mutex_Unlock(MPQueueLock);
        return 0;
    }

    MPPacketHeader pktheader = {};
    self.inboxRead = RingRead(self.inbox, kPacketQueueSize, self.inboxRead, &pktheader, sizeof(pktheader));

    if (pktheader.Magic != 0x4946494E)
    {
        Log(LogLevel::Warn, "PACKET FIFO OVERFLOW\n");
        self.inboxRead = self.inboxWrite;
        Semaphore_Reset(self.frameSem);
        Mutex_Unlock(MPQueueLock);
        return 0;
    }

    if (pktheader.Length)
    {
        self.inboxRead = RingRead(self.inbox, kPacketQueueSize, self.inboxRead, packet, pktheader.Length);

        /* The frame that makes this console somebody's client is a command,
           and this is where it learns whose it is - the same moment the game
           does. */
        if ((pktheader.Type & 0xFFFF) == 1)
            self.hostInst = (int)pktheader.SenderID;
    }

    if (timestamp) *timestamp = pktheader.Timestamp;
    Mutex_Unlock(MPQueueLock);
    return pktheader.Length;
}

int LocalMP::RecvPacket(int inst, u8* packet, u64* timestamp)
{
    return RecvPacketGeneric(inst, packet, false, timestamp);
}

int LocalMP::RecvPacketFull(int inst, u8* packet, u64* timestamp, u32* senderOut, u32* typeOut)
{
    if ((inst < 0) || (inst >= kMaxMPConsoles)) return 0;
    Console& self = Consoles[inst];

    if (!Semaphore_TryWait(self.frameSem, 0))
        return 0;

    Mutex_Lock(MPQueueLock);

    /* Same short-read guard as RecvPacketGeneric: a semaphore posted for an
       entry the router has since dropped for room reads as nothing. */
    if (self.inboxRead == self.inboxWrite)
    {
        Mutex_Unlock(MPQueueLock);
        return 0;
    }

    MPPacketHeader pktheader = {};
    self.inboxRead = RingRead(self.inbox, kPacketQueueSize, self.inboxRead, &pktheader, sizeof(pktheader));

    if (pktheader.Magic != 0x4946494E)
    {
        Log(LogLevel::Warn, "PACKET FIFO OVERFLOW\n");
        self.inboxRead = self.inboxWrite;
        Semaphore_Reset(self.frameSem);
        Mutex_Unlock(MPQueueLock);
        return 0;
    }

    if (pktheader.Length)
    {
        self.inboxRead = RingRead(self.inbox, kPacketQueueSize, self.inboxRead, packet, pktheader.Length);

        /* Same ownership rule as the generic path: reading a command makes
           this console its sender's client, whoever the reader is. */
        if ((pktheader.Type & 0xFFFF) == 1)
            self.hostInst = (int)pktheader.SenderID;
    }

    if (timestamp) *timestamp = pktheader.Timestamp;
    if (senderOut) *senderOut = pktheader.SenderID;
    if (typeOut) *typeOut = pktheader.Type;
    Mutex_Unlock(MPQueueLock);
    return pktheader.Length;
}

int LocalMP::RequeuePacket(int inst, u32 sender, u32 type, u8* packet, int len, u64 timestamp)
{
    if ((inst < 0) || (inst >= kMaxMPConsoles)) return 0;
    if (!packet || len <= 0 || len > kMaxFrameSize) return 0;

    Mutex_Lock(MPQueueLock);
    Console& self = Consoles[inst];
    if (!self.attached)
    {
        Mutex_Unlock(MPQueueLock);
        return 0;
    }

    /* The entry exactly as the router filed it: same sender, same type, same
       timestamp, body already on-wire (FCS in its own tail slot). Appended to
       the tail, so the frames behind it keep their order and this one is read
       last - a scanner does not care about order, and a lagging console's
       oldest unread is what overflow drops first, which is the honest price. */
    MPPacketHeader h { 0x4946494E, sender, type, (u32)len, timestamp };
    QueueAppend(inst + 1, "inbox", self.inbox, kPacketQueueSize,
                self.inboxRead, self.inboxWrite, h, packet, self.frames_dropped);
    Semaphore_Post(self.frameSem);
    Mutex_Unlock(MPQueueLock);
    return len;
}

int LocalMP::RecvHostPacket(int inst, u8* packet, u64* timestamp)
{
    /* The console this one is following has left the bus: the game is told its
       host is gone rather than left waiting for a frame that cannot come. */
    const int host = Consoles[inst].hostInst;
    if (host >= 0)
    {
        Mutex_Lock(MPQueueLock);
        const bool gone = !Consoles[host].attached;
        Mutex_Unlock(MPQueueLock);
        if (gone) return -1;
    }

    return RecvPacketGeneric(inst, packet, true, timestamp);
}

/* The asked AIDs whose console has put nothing on the wire since the newest
   command went out: that client has not answered this round, by its own send
   counter rather than by a clock. An AID whose owner is unknown is not counted
   as owing - there is nothing to ask. */
u16 LocalMP::OwedReplies(int inst, u16 aidmask) const noexcept
{
    u16 owed = 0;
    for (int a = 1; (a < 16) && (a <= kMaxMPConsoles); a++)
    {
        if (!(aidmask & (1 << a))) continue;
        const int owner = Consoles[inst].aid_owner[a];
        if ((owner < 0) || (owner >= kMaxMPConsoles)) continue;
        if (Consoles[owner].diag.replies_written == Consoles[inst].aid_baseline[a])
            owed |= (u16)(1 << a);
    }
    return owed;
}

u16 LocalMP::RecvReplies(int inst, u8* packets, u64 timestamp, u16 aidmask)
{
    Console& self = Consoles[inst];

    u16 curinstmask = 0;
    for (int i = 0; i < kMaxMPConsoles; i++)
        if (Consoles[i].attached) curinstmask |= (1 << i);

    // if all clients have left: return early
    const u16 myinstmask = (u16)(1 << inst);
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
        self.diag.not_looking++;
        return 0;
    }

    u16 ret = 0;
    u16 heard = (u16)(1 << inst);

    /* Wall time a window spends waiting is time the group spends anyway when
       the client it waits for is working on the answer, and pure loss when it
       is not. Both sides are on the record here: what arrived, and what the
       asked clients still owed on their own send counters when the window
       closed. */
    /* How far a window may outlive its own ceiling for a client whose round has
       not come round yet, as a hard bound in emulated frames. The gate below
       decides *whether* to wait; this decides that the waiting always ends.
       MD_REPLY_EXTEND_FRAMES sets it, so the gate can be compared against the
       plain ceiling on the same binary (0 = never outlive the ceiling). */
    const int extend_budget = reply_extend_frames();
    constexpr int kExtendSliceMs = 16; // one emulated frame per probe
    int extended_frames = 0;

    const auto window_opened_at = std::chrono::steady_clock::now();
    const auto close_window = [&](const char* reason) {
        if (!trace_replies()) return;
        const long long spent = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - window_opened_at).count();
        const u16 owed = OwedReplies(inst, aidmask);
        char list[32] = "none";
        if (owed)
        {
            int w = 0;
            for (int a = 1; (a < 16) && (w < (int)sizeof(list) - 4); a++)
                if (owed & (1 << a)) w += snprintf(&list[w], sizeof(list) - (size_t)w, "%s%d", w ? "," : "", a);
        }
        Log(LogLevel::Debug,
            "reply window: p%d closed (%s) after %lldus wall, collected=%04X of %04X, "
            "still owed %04X (%s)\n",
            inst + 1, reason, spent, ret, aidmask, owed, list);
    };

    for (;;)
    {
        /* Waiting for a reply is wall-clock time spent inside an emulated
           frame. Spending it freely *on a client that is not answering* once
           took a live MKDS join (1436 commands, 363 empty windows) down to
           9fps, because every peer console then waits for the slow one at the
           lockstep barrier and the host's own reply latency grows with the
           cost of a frame - the wait feeds itself. So both the per-window
           ceiling and the per-frame budget below still apply to a window that
           nobody is answering; see the `answering` test under them for the
           case they must not apply to. */
        int waitms = ReplyTimeout;

        /* An extension probe is one frame long, not another full ceiling: the
           point of the probe is to ask the queue again after giving a peer one
           more frame's worth of wall clock, not to restart the ceiling. Six
           probes that each re-ran the 34ms ceiling turned one Tetris room's
           470ms windows into a 3fps stall group (2026-10-22). */
        if (extended_frames > 0) waitms = kExtendSliceMs;

        /* ... and only while the clients this poll names are the ones that have
           been answering.

           A wait is worth its wall clock when it is covering a peer's own
           progress: the peer answers this command from inside the same
           emulated round, on its own thread, so the host's wait is time the
           group had to spend anyway. It is pure loss when the client has gone
           quiet - nobody is going to post that semaphore, and the frame rate
           pays for every millisecond of it.

           Measured at three consoles on Tetris DS's card room (2026-10-22):
           with this cap applied to every window, 1,062 of 3,463 windows came
           back empty - exactly the number of `clientfail` bits the host's own
           CMD ACK reported - and the game answered that 30% loss rate by
           retransmitting one Download Play data packet until both guests gave
           up with `reason 0003`. The same route with the same two guests, the
           cap lifted only by making the budget unbounded, opened the room's
           setup table on all three consoles at 59.7fps each with 30 failures
           in 428 acks. The difference is not `clientfail` being tolerated; it
           is that the peers were answering all along and the window gave up
           on them one round early. */
        constexpr u64 kAnsweringRecent = 4 * 16715; // four emulated frames, in us
        constexpr int kQuietWaitMs = 16;             // one emulated frame
        bool answering = false;
        for (int a = 1; (a < 16) && !answering; a++)
        {
            if (!(aidmask & (1 << a)) || !self.answer_us[a]) continue;
            /* A negative age is an answer stamped later than this console's
               own clock - still recent, see kReplyStaleWindow. */
            if ((long long)(timestamp - self.answer_us[a]) < (long long)kAnsweringRecent)
                answering = true;
        }

        /* A window that is waiting on a client which is answering gets its
           full ceiling: the peer is producing this round's answer on its own
           thread and the group cannot go on without it. A window with no
           recent answer from anyone it names is the case the per-frame budget
           was written for, and it keeps both caps - one frame, and whatever
           the frame has left. */
        if (!answering)
        {
            if (waitms > kQuietWaitMs)
            {
                if (trace_replies())
                    Log(LogLevel::Debug,
                        "reply window: p%d's mask %04X has no client that answered in the last "
                        "%lluus - waiting one frame, not %dms\n",
                        inst + 1, aidmask, (unsigned long long)kAnsweringRecent, waitms);
                waitms = kQuietWaitMs;
            }

            const long long budget_ns = GetReplyBudgetLeft(inst);
            if (budget_ns >= 0)
            {
                const int budget_ms = (int)(budget_ns / 1000000);
                if (budget_ms < waitms)
                {
                    if (trace_replies())
                        Log(LogLevel::Debug,
                            "reply window: p%d would have waited %dms for a reply, but this frame only "
                            "has %lldus of slack left (nothing waiting, collected=%04X so far)\n",
                            inst + 1, waitms, budget_ns / 1000, ret);
                    waitms = budget_ms;
                }
            }
        }

        if (!Semaphore_TryWait(self.answerSem, waitms))
        {
            /* The ceiling is wall clock, and what the window actually means is
               emulated rounds: every client it asked has had one of its own
               rounds in which to answer. Those are not the same thing at three
               consoles - measured on Tetris DS's card room, 2026-10-22: 38 of
               571 windows closed empty after burning the full ceiling, and in
               almost every one of them the owed client's answer turned up
               within the next few windows, i.e. the client was working on a
               round of its own whose wall cost was longer than the ceiling.
               Each of those windows became a `clientfail` bit in the host's
               ack, and the title answered a handful of them by closing the
               room.

               So an expiry is re-asked as a question about the client's own
               progress, by counters only: has it polled since this command's
               round, and has it run a whole round past it? Two polls means it
               had its chance in its own round and still put nothing on the
               wire - a real miss, counted (closed_owed) rather than guessed
               at; fewer than two means the client may still be inside the
               round that answers this command.

               MD_REPLY_EXTEND_FRAMES (default 0) turns that answer into extra
               wait, one frame per probe. It defaults off because the probes do
               not deliver: on the three-console Tetris room (2026-10-22) 6
               probes cut `clientfail` from 9.5% to 6.1% (34/359 acks to
               28/460) but every one of the 28 remaining windows still closed
               owed after ~165ms, and the room died earlier than it does without
               the probes. The consoles advance their frames together at the
               Rust host's per-frame barrier, so the client whose answer is
               missing is held by the same barrier the host is sitting in: the
               wait cannot let it run, only the host ending its frame can, and
               the answer that was owed at the close is collected by the next
               window (measured: 33 of 41 owed AIDs within three windows). So
               the ceiling is the honest close even though it is wall clock,
               and what is left to fix is the cadence itself, not this wait. */
            const u16 owed = OwedReplies(inst, aidmask);
            bool before_chance = false;
            bool past_chance = false;
            for (int a = 1; (a < 16) && (a <= kMaxMPConsoles); a++)
            {
                if (!(owed & (1 << a))) continue;
                const int owner = Consoles[inst].aid_owner[a];
                if ((owner < 0) || (owner >= kMaxMPConsoles)) continue;
                /* One poll is the command's own round (the client may still be
                   inside it); two means it opened a round after this command
                   and still had nothing to put on the wire. */
                const u64 rounds = Consoles[owner].host_polls - Consoles[inst].aid_poll_baseline[a];
                if (rounds >= 2) past_chance = true;
                else before_chance = true;
            }

            if (owed && before_chance && !past_chance && (extended_frames < extend_budget))
            {
                if (extended_frames++ == 0) self.diag.windows_extended++;
                self.diag.extension_frames++;
                if (trace_replies())
                    Log(LogLevel::Debug,
                        "reply window: p%d outlived its ceiling with %04X still owed - the client "
                        "has not seen this round yet, waiting one more (%d of %d frames)\n",
                        inst + 1, owed, extended_frames, extend_budget);
                continue;
            }

            // no more replies available
            close_window(owed ? "no reply after the client's own round" : "no reply in the slice");
            if (!ret) self.diag.windows_empty++;
            if (owed) self.diag.closed_owed++;
            return ret;
        }

        Mutex_Lock(MPQueueLock);

        /* Same as the inbox above: an answer a lagging queue dropped (see
           QueueAppend) leaves a count behind it, and a window opened by one of
           those is a window with nothing in it. */
        if (self.answerRead == self.answerWrite)
        {
            Mutex_Unlock(MPQueueLock);
            close_window("queue empty");
            if (!ret) self.diag.windows_empty++;
            return ret;
        }

        MPPacketHeader pktheader = {};
        RingRead(self.answers, kReplyQueueSize, self.answerRead, &pktheader, sizeof(pktheader));

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
            self.answerRead = self.answerWrite;
            Semaphore_Reset(self.answerSem);
            Mutex_Unlock(MPQueueLock);
            return 0;
        }

        /* Stock's window was 32 ticks; this fork's is two frames (see
           kReplyStaleWindow). MD_STOCK_MP=1 restores stock's. */
        const u64 stalewindow = stock_mp() ? 32 : kReplyStaleWindow;

        /* The entry is consumed either way - the cursor moves by its length -
           but only an answer that survives both tests below reaches the game. */
        const u32 entrylen = (u32)(sizeof(MPPacketHeader) + pktheader.Length);
        self.answerRead = (self.answerRead + entrylen) % kReplyQueueSize;

        if ((pktheader.SenderID == inst) || // packet we sent out (shouldn't happen, but hey)
            (pktheader.Timestamp + stalewindow < timestamp)) // stale packet
        {
            if (pktheader.SenderID != inst)
                self.diag.stale_dropped++;

            if (trace_replies() && pktheader.SenderID != inst)
                Log(LogLevel::Warn,
                    "reply window: DROPPED a reply for being %lld US-timer ticks stale \
(sender p%d, window %llu) - the client answered and the host could not use it\n",
                    (long long)(timestamp - pktheader.Timestamp), pktheader.SenderID + 1,
                    (unsigned long long)kReplyStaleWindow);

            Mutex_Unlock(MPQueueLock);
            continue;
        }

        /* A zero length entry is the blank a client sends so the host has
           something to receive. RecvReplies deliberately does not credit it
           (`if (pktheader.Length)` below), which is why a run can show the
           guest answering every round and the host's acks still reporting it
           as failed - so the two are counted apart. */
        if (!pktheader.Length)
            self.diag.collected_blank++;

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

            /* Which console this AID belongs to, for the lifetime of the
               session. Two consoles stamping one AID is a real defect and a
               silent one: both answers land in the same per-AID buffer, so the
               host's game reads whichever arrived last as that AID's answer and
               neither client's state reaches it intact. Named once per AID, with
               both senders, because "the third client's hand-off reused an AID
               the other two still answer as" is exactly the thing a
               three-console accept cannot be diagnosed without. */
            /* Scoped to the last few hundred milliseconds, not the session:
               a client answers every round (~16 ms), so an owner that answered
               within this window is a console that is answering *now*. The
               room's normal close is followed by both guests re-associating and
               the host handing the first one back AID 1 - a legitimate reuse
               seconds later that a session-scoped check would call a collision. */
            const long long since = (long long)(timestamp - self.aid_owner_us[aid]);
            const bool owner_live = (self.aid_owner[aid] >= 0) &&
                                    (since >= 0) && (since < (long long)(kReplyStaleWindow * 8));
            if (owner_live && (self.aid_owner[aid] != pktheader.SenderID))
            {
                if (!self.aid_collision_logged[aid])
                {
                    Log(LogLevel::Warn,
                        "reply aid collision: p%d answered as AID %d, which p%d answered as %lld us "
                        "earlier - both are filed into the host's AID %d buffer, so the host's game "
                        "cannot tell their answers apart\n",
                        pktheader.SenderID + 1, aid, self.aid_owner[aid] + 1, since, aid);
                    self.aid_collision_logged[aid] = true;
                }
            }
            self.aid_owner[aid] = pktheader.SenderID;
            self.aid_owner_us[aid] = timestamp;

            self.answerRead = RingRead(self.answers, kReplyQueueSize,
                                       (self.answerRead + kReplyQueueSize - pktheader.Length) % kReplyQueueSize,
                                       &packets[(aid-1)*1024], pktheader.Length);
            ret |= (1 << aid);
            self.answer_us[aid] = pktheader.Timestamp;
            self.diag.collected++;

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

        /* Only an entry that carried a body counts as "this console has
           answered". A zero-length entry is the blank a console sends for a
           command that was not addressed to it (Wifi's `send a blank` arm), and
           at four consoles that is most of what a window sees: measured on a
           joined four-console MKDS room, the host consumed 433 blanks against
           452 `clientfail` acks. Counting a blank as an answer closed the window
           as soon as every console had said *something* - a blank from p3 and
           p4, in the window the run recorded - while the client the host had
           actually polled had its own answer read on the next command, or
           dropped unread (`reanchor-dropped` 392 in the same run). The window
           now ends when the answers it asked for are here, or when there is
           nothing left to wait for (the semaphore's timed wait). */
        if (pktheader.Length)
            heard |= (u16)(1 << pktheader.SenderID);

        /* `heard` counts consoles, `ret` counts aids; a console answers as one
           aid, so both conditions say "everyone asked for has answered" - the
           sender test is stock's and stays for a console whose aid the bus does
           not know. */
        if (((heard & curinstmask) == curinstmask) ||
            ((ret & aidmask) == aidmask))
        {
            // all the clients have sent their reply

            if (trace_replies())
                Log(LogLevel::Debug,
                    "reply window: p%d has everyone it asked for (collected=%04X of mask %04X)\n",
                    inst + 1, ret, aidmask);

            Mutex_Unlock(MPQueueLock);
            close_window("all asked for");
            return ret;
        }

        Mutex_Unlock(MPQueueLock);
    }
}

}

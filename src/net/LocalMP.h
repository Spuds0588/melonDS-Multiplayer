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

#ifndef LOCALMP_H
#define LOCALMP_H

#include "types.h"
#include "Platform.h"
#include "MPInterface.h"

namespace melonDS
{
constexpr u32 kPacketQueueSize = 0x10000;
constexpr u32 kReplyQueueSize = 0x10000;
constexpr u32 kMaxFrameSize = 0x948;
constexpr int kMaxMPConsoles = 16;

/*
   The local-multiplayer bus, as one console's own state per console plus one
   router between them.

   What each console owns (see `Console` below) is everything the bus knows
   about it: the frames routed to it and the cursor its driver thread reads
   them with, the console whose commands it is following, the answers the
   others sent to it (a queue only a console that has sent a command can have
   anything in), and its own half of the flight recorder. A console's state is
   written by that console's own calls and, for frames, by the router - which
   never writes bookkeeping into anyone, only frames.

   What the bus as a whole owns is the routing decision, in one function
   (`Route`): a frame a console sends goes to every attached console except
   the sender, as the air carries it, and an answer goes to the console whose
   command the answering console is following, because that is the only
   console it can belong to. Nothing else in the process decides who is told
   what about whom - see the note at the top of LocalMP.cpp for the
   process-wide record this replaces and what it could not express.
*/

class LocalMP : public MPInterface
{
public:
    LocalMP() noexcept;
    LocalMP(const LocalMP&) = delete;
    LocalMP& operator=(const LocalMP&) = delete;
    LocalMP(LocalMP&& other) = delete;
    LocalMP& operator=(LocalMP&& other) = delete;
    ~LocalMP() noexcept;

    void Process() {}

    void Begin(int inst);
    void End(int inst);

    /* Return the bus to its just-constructed state: no consoles attached,
       every read cursor and semaphore back to zero. Used by the Tauri bridge
       when a session ends. Kept separate from the destructor on purpose: the
       host holds a raw pointer to this bus from several threads, so the bus
       object outlives every session and is reset rather than destroyed (see
       md_link_shutdown in Bridge.cpp). */
    void Reset();

    int SendPacket(int inst, u8* data, int len, u64 timestamp);
    int RecvPacket(int inst, u8* data, u64* timestamp);
    /* Same as RecvPacket, plus the entry's own routing fields. A reader that
       hands frames back (see RequeuePacket) needs the header the bus filed
       them under: the body alone does not carry the sender, and rebuilding it
       from the 802.11 addresses would misattribute relayed frames. */
    int RecvPacketFull(int inst, u8* data, u64* timestamp, u32* senderOut, u32* typeOut);
    /* Return a frame this console read to the tail of its own inbox, under its
       original header. For a reader that must see some classes (a command it
       answers) but leave others for the console's own game (the beacons a
       booted Download Play client scans for): consumed-then-restored is still
       a delivery, and the ring's own overflow counting keeps any loss honest.
       The frame is already on-wire format (FCS applied at route time), so it
       is appended verbatim - re-applying the FCS would corrupt its tail. */
    int RequeuePacket(int inst, u32 sender, u32 type, u8* packet, int len, u64 timestamp);
    int SendCmd(int inst, u8* data, int len, u64 timestamp);
    int SendReply(int inst, u8* data, int len, u64 timestamp, u16 aid);
    int SendAck(int inst, u8* data, int len, u64 timestamp);
    int RecvHostPacket(int inst, u8* data, u64* timestamp);
    u16 RecvReplies(int inst, u8* data, u64 timestamp, u16 aidmask);

    /* The asked AIDs whose console has sent nothing since the newest command:
       by that console's own write counter, so "this client has not answered
       this round" is a fact about the bus, not a guess from a clock. */
    u16 OwedReplies(int inst, u16 aidmask) const noexcept;

    /* ---- reply-queue flight recorder (see Wifi::MPDiag for the other half) ----

       The host's side of the conversation, per instance: how many answers the
       guest actually handed to the bus, how many the host read back, and -
       this is the part that has cost live runs - how many were thrown away and
       by which mechanism. "The client answered every round" and "the host's
       game was told the client failed" are both true at once when answers are
       being discarded; these counters say so in one line. */
    struct MPReplyDiag
    {
        u64 replies_written;   // answers written by this console's own game
        u64 collected;         // answers read back with a payload
        u64 collected_blank;   // zero length credits (a reply the host cannot use)
        u64 windows_empty;     // collection attempts that came back with nothing
        u64 stale_dropped;     // answers skipped for being older than the window
        u64 reanchor_dropped;  // unread answers discarded when a command re-anchored, or
                               // dropped to make room for a newer one (see QueueAppend)
        u64  not_looking;      // windows that never looked: no peer on the bus
        u64  windows_extended; // windows that outlived the wall ceiling for an owed client's round
        u64  extension_frames; // emulated frames of extra wait those windows spent
        u64  closed_owed;      // windows that closed with answers owed after their clients ran a round
    };

    void GetReplyDiag(int inst, MPReplyDiag* out) const noexcept;

    /* What the router has handed one console since the bus was reset: frames
       and body bytes. A frame the receiving console's own game reads is still
       a delivery; this is the count that says the bus did its job. */
    void GetDelivered(int inst, u64* packets, u64* bytes) const noexcept;

    /* The frame as the wire leaves it: `dst` receives `len` bytes of `src`,
       with the hardware's FCS written into the frame's own trailing four
       bytes. Returns whether it wrote one (a frame too short to hold an FCS
       slot, or MD_MP_FCS=0, is a plain copy).

       Public because it is the one rule a sender and a receiver must agree on:
       `SendPacketGeneric` applies it to every frame it routes, and the bridge's
       Link-test probe arms its expectation with it, so the bytes a test waits
       for are the bytes the same code will put on the wire. Two copies of this
       rule is how the Link test came to compare a nonce against a checksum and
       call a healthy bus broken (2026-09-30). `dst` may be `src`. */
    static bool ApplyWireFCS(u8* dst, const u8* src, size_t len) noexcept;

    /* Frames the router had to drop out of a console's inbox because that
       console was not keeping up with it (see QueueAppend), for one instance.
       The contract this number exists to hold is that nothing is lost
       *silently*: every frame a console does not get back is one of these. The
       smoke test is the only reader. */
    u64 GetInboxDrops(int inst) const noexcept;

    /* Whether this console is on the bus right now (LocalMP::Begin has run and
       LocalMP::End has not). A console whose radio is off is not attached, and
       the router writes it nothing - so a reader that needs to know whether it
       is still hearing the bus has to ask rather than assume: the Download
       Play joiner attaches at the bus level, and a game's own robot power cycle
       (MP_Begin/MP_End) can detach it again underneath. */
    bool IsAttached(int inst) const noexcept;

private:
    struct Console
    {
        // powered on and part of the bus
        bool attached = false;

        /* Frames the router has handed this console, in arrival order, and the
           cursor its own driver thread reads them with. A console only ever
           reads its own inbox: the router is the only writer - and it is a
           writer that may not overwrite what this console has not read yet
           (see MakeRoom), so `frames_dropped` counts the oldest unread frames
           it had to give up instead.*/
        u8   inbox[kPacketQueueSize] {};
        u32  inboxRead = 0;
        u32  inboxWrite = 0;
        u64  frames_dropped = 0;

        /* Frames the router handed this console, whether or not its game ever
           asked for them. This is the bus's own ledger and the only place a
           delivery can be attributed to the bus: the counters on the receiving
           side (PlatformBridge's note_packet_received, md_link_recv_packet)
           both fire when a *consumer* reads, and a console whose game polls
           every frame consumes its inbox before any scripted reader gets a
           turn - measured 2026-09-30, the app's Link test drained nothing
           while these deliveries were 9 per console. */
        u64  frames_delivered = 0;
        u64  bytes_delivered = 0;
        Platform::Semaphore* frameSem = nullptr;

        /* The console whose commands this one is following - the only console
           an answer from this console can belong to. -1 until it has read a
           command, which is exactly when it becomes someone's client. */
        int  hostInst = -1;

        /* Answers the others sent to *this* console, because they were
           following *its* commands, and the cursor it reads them with. Empty
           on a console that has never sent a command. */
        u8   answers[kReplyQueueSize] {};
        u32  answerRead = 0;
        u32  answerWrite = 0;
        Platform::Semaphore* answerSem = nullptr;

        /* When each aid's last real answer was credited to this console, in
           this console's own US-tick domain (the timestamp Wifi hands
           RecvReplies). Zero means "never". RecvReplies reads it to tell a
           poll that is waiting on clients which are answering from one that
           is waiting on a client which has gone quiet: the first wait is time
           the peers were spending anyway, the second is frame rate thrown
           away (2026-10-22, LocalMP.cpp's reply-window cap). */
        u64  answer_us[16] {};

        /* Each answered AID's console send counter at the moment the newest
           command went out (see the note in SendPacketGeneric). An aid whose
           owner's counter has not moved since is a client that has put nothing
           on the wire for this round. */
        u64  aid_baseline[16] {};

        /* The same baseline for that console's own *rounds*: how many times it
           has polled for its host's command. A guest's answer for a command
           cannot come before it has polled that command, so this is what the
           host's reply window measures "has the client had its chance" with
           instead of a clock (see the note in RecvReplies). */
        u64  host_polls = 0;
        u64  aid_poll_baseline[16] {};

        /* Which console last stamped each AID, when it did, and whether a
           second console has been seen doing the same while the first was
           still answering. The host files every answer under its AID
           (`packets[(aid-1)*1024]`), so two consoles answering as one AID
           overwrite each other and the host's game reads one client's bytes as
           the other's - a collision a three-console hand-off can produce and
           nothing else reports.

           The owner is scoped to *time*, not to the session: a room's normal
           close is followed by both guests re-associating, and the host hands
           the first one back AID 1 - a legitimate reuse that a lifetime-scoped
           check reports as a collision. Only an owner that answered within a
           few hundred milliseconds (a live client answers every round, ~16 ms)
           counts, so the warning means two consoles really are answering as
           one AID at once. -1 = no console has stamped that AID yet, and
           `aid_collision_logged` keeps it to once per AID. */
        int  aid_owner[16];
        u64  aid_owner_us[16] {};
        bool aid_collision_logged[16] {};

        MPReplyDiag diag {};
    };

    /* The one place that decides what a console is told about another. Writes
       frames into the recipients' own state and returns the semaphores to post
       (posted by the caller, outside the queue lock, like the bus always
       has). */
    void Route(int sender, u32 type, const u8* frame, int len, u64 timestamp,
               Platform::Semaphore** posts, int* nposts) noexcept;

    /* Append one entry to a console's queue, making room for it by dropping the
       oldest entries that console has not read (see the note on MakeRoom) and
       counting them into `dropped`. Returns the bytes it moved the read cursor
       by, so a caller that needs to know the queue changed length can. */
    u32 QueueAppend(int owner, const char* name, u8* ring, u32 ringlen,
                    u32& read, u32& write, const MPPacketHeader& h, const u8* body,
                    u64& dropped) noexcept;

    /* Keep the newest `keep` answers from each console this one asked, drop the
       rest, and report how many were dropped and how many survive. Only a
       prefix of the queue can be dropped - the cursor is a ring read position -
       so this drops everything before the oldest entry that must survive, and
       then gives back any of that prefix that is not surplus for its own
       sender: a console's last answer is never dropped to make room for another
       console's extra one. */
    u32 TrimAnswers(int inst, int keep, u32* kept_out) noexcept;

    int SendPacketGeneric(int inst, u32 type, u8* packet, int len, u64 timestamp) noexcept;
    int RecvPacketGeneric(int inst, u8* packet, bool block, u64* timestamp) noexcept;

    Platform::Mutex* MPQueueLock;
    Console Consoles[kMaxMPConsoles];
};
}

#endif // LOCALMP_H

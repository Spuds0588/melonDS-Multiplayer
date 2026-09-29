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
struct MPStatusData
{
    u16 ConnectedBitmask; // bitmask of which instances are ready to send/receive packets
    u32 PacketWriteOffset;
    u32 ReplyWriteOffset;
    u16 MPHostinst; // instance ID from which the last CMD frame was sent
    u16 MPReplyBitmask;   // bitmask of which clients replied in time
};

constexpr u32 kPacketQueueSize = 0x10000;
constexpr u32 kReplyQueueSize = 0x10000;
constexpr u32 kMaxFrameSize = 0x948;

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

    /* Return the bus to its just-constructed state: no consoles attached, every
       FIFO read offset and semaphore back to zero. Used by the Tauri bridge
       when a session ends. Kept separate from the destructor on purpose: the
       host holds a raw pointer to this bus from several threads, so the bus
       object outlives every session and is reset rather than destroyed (see
       md_link_shutdown in Bridge.cpp). */
    void Reset();

    int SendPacket(int inst, u8* data, int len, u64 timestamp);
    int RecvPacket(int inst, u8* data, u64* timestamp);
    int SendCmd(int inst, u8* data, int len, u64 timestamp);
    int SendReply(int inst, u8* data, int len, u64 timestamp, u16 aid);
    int SendAck(int inst, u8* data, int len, u64 timestamp);
    int RecvHostPacket(int inst, u8* data, u64* timestamp);
    u16 RecvReplies(int inst, u8* data, u64 timestamp, u16 aidmask);

    /* ---- reply-queue flight recorder (see Wifi::MPDiag for the other half) ----

       The host's side of the conversation, per instance: how many answers the
       guest actually handed to the bus, how many the host read back, and - this
       is the part that has cost live runs - how many were thrown away and by
       which mechanism. "The client answered every round" and "the host's game
       was told the client failed" are both true at once when answers are being
       discarded; these counters say so in one line. */
    struct MPReplyDiag
    {
        u64 replies_written;   // answers written by this console's own game
        u64 collected;         // answers read back with a payload
        u64 collected_blank;   // zero length credits (a reply the host cannot use)
        u64 windows_empty;     // collection attempts that came back with nothing
        u64 stale_dropped;     // answers skipped for being older than the window
        u64 reanchor_dropped;  // unread answers discarded when a command re-anchored
        u64 not_looking;       // windows that never looked: no peer on the bus
    };

    void GetReplyDiag(int inst, MPReplyDiag* out) const noexcept;

private:
    void FIFORead(int inst, int fifo, void* buf, int len) noexcept;
    void FIFOWrite(int inst, int fifo, void* buf, int len) noexcept;
    int SendPacketGeneric(int inst, u32 type, u8* packet, int len, u64 timestamp) noexcept;
    int RecvPacketGeneric(int inst, u8* packet, bool block, u64* timestamp) noexcept;

    Platform::Mutex* MPQueueLock;
    MPStatusData MPStatus {};
    MPReplyDiag ReplyDiag[16] {};
    u8 MPPacketQueue[kPacketQueueSize] {};
    u8 MPReplyQueue[kReplyQueueSize] {};
    u32 PacketReadOffset[16] {};
    u32 ReplyReadOffset[16] {};

    int LastHostID = -1;
    Platform::Semaphore* SemPool[32] {};
};
}

#endif // LOCALMP_H

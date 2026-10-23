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

#ifndef MPINTERFACE_H
#define MPINTERFACE_H

#include <chrono>
#include <memory>
#include "types.h"

namespace melonDS
{

// TODO: provision for excluding unwanted interfaces at compile time
enum MPInterfaceType
{
    MPInterface_Dummy = -1,
    MPInterface_Local,
    MPInterface_LAN,
    MPInterface_Netplay,
};

struct MPPacketHeader
{
    u32 Magic;
    u32 SenderID;
    u32 Type;       // 0=regular 1=CMD 2=reply 3=ack
    u32 Length;
    u64 Timestamp;
};

class MPInterface
{
public:
    virtual ~MPInterface() = default;

    static MPInterface& Get() { return *Current; }
    static MPInterfaceType GetType() { return CurrentType; }
    static void Set(MPInterfaceType type);

    /* How long a console blocks waiting for a frame from a peer. melonDS's own
       frontend runs one real-time thread per console, so waiting is right there.
       An embedding host that advances several consoles in lockstep must set this
       to zero: a console waiting for a frame the peer cannot produce until it
       finishes the same frame is a deadlock, not a delay. */
    [[nodiscard]] int GetRecvTimeout() const noexcept { return RecvTimeout; }
    void SetRecvTimeout(int timeout) noexcept { RecvTimeout = timeout; }

    /* How long a host blocks collecting multiplayer replies. This one is worth
       a short wait even in a lockstep host: the host asks for replies once per
       command slot, and the clients' replies are racing it, so giving up
       instantly makes the host retry a handshake it could have completed. */
    [[nodiscard]] int GetReplyTimeout() const noexcept { return ReplyTimeout; }
    void SetReplyTimeout(int timeout) noexcept { ReplyTimeout = timeout; }

    /* How much of the *current frame's* budget this console may spend inside
       reply waits, published by the host once per frame (negative = no limit,
       i.e. stock melonDS). ReplyTimeout above bounds a single wait; this bounds
       their sum, and it exists because the waits are the only link timing
       measured in wall-clock time while the frame they sit inside is measured
       in emulated time.

       Why it matters, measured 2026-09-25: during an MKDS join the host asked
       for replies 1436 times in 15s and 363 of those windows were empty; at the
       then-current 34ms ceiling that is 12.3s of a 15s window spent blocked
       *inside the emulator*, so the session ran at 9fps instead of 60 and every
       peer console waited for it at the lockstep barrier. The host's own reply
       latency grows with the wall-clock cost of an emulated frame, so an
       unbounded wait feeds itself. Charging the waits to the frame's budget
       keeps the frame rate and lets a console wait exactly as long as it was
       going to be idle anyway. */
    void SetReplyBudget(int inst, long long ns) noexcept
    {
        if (inst < 0 || inst >= (int)(sizeof(ReplyBudgetNs) / sizeof(ReplyBudgetNs[0])))
            return;
        ReplyBudgetNs[inst] = ns;
        ReplyBudgetStart[inst] = std::chrono::steady_clock::now();
    }

    /* Nanoseconds of the current frame's reply budget that are still unspent,
       or -1 when no budget was published. */
    [[nodiscard]] long long GetReplyBudgetLeft(int inst) const noexcept
    {
        if (inst < 0 || inst >= (int)(sizeof(ReplyBudgetNs) / sizeof(ReplyBudgetNs[0])))
            return -1;
        const long long budget = ReplyBudgetNs[inst];
        if (budget < 0) return -1;

        const auto spent = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - ReplyBudgetStart[inst]).count();
        const long long left = budget - (long long)spent;
        return left > 0 ? left : 0;
    }

    /* Read the reply queue but never wait inside the poll.

       The host's Wifi can hold a round open across the frame boundary instead
       (`Wifi::ProcessTX` phase 14, the reply rendezvous): it re-polls once per
       timer tick until the clients that owe an answer have produced it, or a
       stated number of emulated frames has passed. In that mode each poll must
       cost nothing, because the waiting is now the slot's own emulated time -
       a poll that blocks for a wall-clock ceiling would spend the frame's budget
       on the first try and then still be empty. */
    void SetReplyEager(bool eager) noexcept { ReplyEager = eager; }
    [[nodiscard]] bool GetReplyEager() const noexcept { return ReplyEager; }

    /* The rendezvous is on, so a host's reply window must not wait on the wall
       clock at all: a round whose clients have not answered is held open by
       `Wifi::ProcessTX` phase 14 in *emulated* time, which is the clock the DS
       protocol being imitated is written in, and which the group can afford.

       This is separate from `ReplyEager` because it is a different statement.
       Eager means "this particular poll is the rendezvous speaking, do not log
       it as a window"; this one means "the frame's reply wait is covered by a
       hold, so there is nothing for a wall-clock ceiling to buy" - and the
       window still is a window: its counters and its trace line stay.

       Measured on Tetris DS's four-console room, 2026-10-23: the same route,
       the same four guests in the room, 211 rounds held at 6.6ms wall each
       (1.4s across the session), and the frame rate 59-60fps with a 2ms
       ceiling against 18fps with the shipped 67ms one, both with 0 failed acks.
       The wall wait was not buying a collected answer - the hold collected it -
       it was only making the group wait. See `LocalMP::RecvReplies`. */
    void SetReplyHold(bool hold) noexcept { ReplyHold = hold; }
    [[nodiscard]] bool GetReplyHold() const noexcept { return ReplyHold; }

    // function called every video frame
    virtual void Process() = 0;

    virtual void Begin(int inst) = 0;
    virtual void End(int inst) = 0;

    virtual int SendPacket(int inst, u8* data, int len, u64 timestamp) = 0;
    virtual int RecvPacket(int inst, u8* data, u64* timestamp) = 0;
    virtual int SendCmd(int inst, u8* data, int len, u64 timestamp) = 0;
    virtual int SendReply(int inst, u8* data, int len, u64 timestamp, u16 aid) = 0;
    virtual int SendAck(int inst, u8* data, int len, u64 timestamp) = 0;
    virtual int RecvHostPacket(int inst, u8* data, u64* timestamp) = 0;
    virtual u16 RecvReplies(int inst, u8* data, u64 timestamp, u16 aidmask) = 0;

protected:
    int RecvTimeout = 25;
    int ReplyTimeout = 25;
    bool ReplyEager = false;
    bool ReplyHold = false;

    // per-instance reply-wait budget, see SetReplyBudget above
    static constexpr int kMaxMPInstances = 16;
    long long ReplyBudgetNs[kMaxMPInstances];
    std::chrono::steady_clock::time_point ReplyBudgetStart[kMaxMPInstances];

private:
    static MPInterfaceType CurrentType;
    static std::unique_ptr<MPInterface> Current;

public:
    MPInterface()
    {
        for (int i = 0; i < kMaxMPInstances; i++)
        {
            ReplyBudgetNs[i] = -1;
            ReplyBudgetStart[i] = std::chrono::steady_clock::now();
        }
    }
};

}

#endif // MPINTERFACE_H

//! The synthesized single-card Download Play joiner.
//!
//! Why this exists: single-card Download Play (Milestone 4) is one cartridge
//! serving several consoles, and the consoles being served run a *downloaded*
//! image - they are not running the game. The client half of the protocol is
//! therefore the DS firmware's own Download Play applet, and this checkout has
//! no firmware image to boot it from (see to-do.md, Milestone 4, Route A). The
//! only implementable route left is to supply the client here: listen for the
//! host's beacon on one console and answer it with the two management frames a
//! client sends first - an open-system authentication and an association
//! request - built from what that beacon actually advertises.
//!
//! What this deliberately is *not*: it is not a receiver and not a downloader.
//! It is the smallest thing that can provoke the host, so that the host's own
//! answer (or its refusal) can be read off the wire and written down as the
//! protocol the rest of the client would be built against.
//!
//! Gating: nothing here runs unless the loaded title's profile says
//! `download_play` *and* the one Settings switch that gates per-game behavior is
//! on (`GameProfile::download_play_joiner`, src-tauri/src/game_profile.rs). When
//! it is off, `step` returns before it touches the bus, so a disarmed joiner
//! performs zero bus operations - not "no visible ones". The frame builders and
//! the state machine are pure (and unit-tested below) so the protocol can be
//! exercised without an emulator.

use std::collections::VecDeque;
use std::sync::atomic::{AtomicBool, AtomicU64, AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

/// The view of the link bus the joiner needs, implemented in `lib.rs` on top of
/// the console's own driver thread and by a mock in the tests below.
pub trait JoinerBus: Send + Sync {
    /// Everything queued for the joiner console right now, or `None` when its
    /// driver did not answer (the console is gone or wedged).
    fn drain(&self) -> Option<Vec<(Vec<u8>, u64)>>;
    /// Broadcast one frame from the joiner console. Returns the queued byte
    /// count, or `None` when the driver did not answer.
    fn send(&self, data: &[u8], ts: u64) -> Option<i32>;
    /// Send one *answer* from the joiner console, filed in the answer queue of
    /// the console it is following and keyed by `aid` - the queue a host's
    /// reply window reads (`LocalMP::RecvReplies` via
    /// `Platform::MP_RecvReplies`). A broadcast `send` never reaches it, which
    /// is why the first version's answer was never collected and the host
    /// reported the client failed every round. Returns the queued byte count,
    /// or `None` when the driver did not answer.
    fn send_reply(&self, data: &[u8], ts: u64, aid: u16) -> Option<i32>;
    /// This console's own wireless MAC.
    fn own_mac(&self) -> Option<[u8; 6]>;
    /// Whether this console's own game has its radio up. If it does, the game
    /// owns the bus and the joiner must not drain the frames it is about to
    /// read: a Download Play client is a console whose game is not in
    /// multiplayer, and that is exactly the state this tests for.
    fn wireless_on(&self) -> bool;
    /// Attach/detach this console to the bus *at the bus level*, so it hears
    /// frames without its own game's radio being on. This is the Download Play
    /// client's own state - a receiving radio with no game behind it - and it
    /// deliberately does not set the wireless-on bit, so nothing else about the
    /// session reads this console as being in multiplayer. Returns false when
    /// the bus refused.
    fn set_receiving(&self, on: bool) -> bool;
    /// Whether this console is on the bus right now. Distinct from
    /// `wireless_on`: a console attached at the bus level reads true here with
    /// its game idle, and a game's own wireless power cycle turns it false
    /// again - which is why the attachment is re-asserted, not assumed.
    fn receiving(&self) -> bool;
    /// This console's own bus slot index, when it has one. The beacon serve
    /// (see `maybe_serve`) hands the stored host beacon back under this
    /// sender so the drain can tell its own serves apart from the host's
    /// frames and keep them out of the host-silence clock and the counters.
    /// The default bus owns no console, so tests see `None` (no echo) unless
    /// a test bus says otherwise.
    fn own_slot(&self) -> Option<u32> {
        None
    }
    /// Everything queued, with each entry's routing fields (bus sender index,
    /// header type). The stand-in needs them to hand frames back (see
    /// `requeue`): the body alone does not carry the sender, and rebuilding it
    /// from the 802.11 addresses would misattribute relayed frames. Defaults
    /// to `drain` with zeroed fields - enough for the in-process test bus,
    /// whose requeues never reach a real ring.
    fn drain_full(&self) -> Option<Vec<(Vec<u8>, u64, u32, u32)>> {
        self.drain()
            .map(|v| v.into_iter().map(|(p, t)| (p, t, 0, 0)).collect())
    }
    /// Return a frame read by `drain_full` to the tail of this console's own
    /// inbox, under its original header - so the console's own game reads it
    /// next. A consumed-then-restored frame is still a delivery (the bus
    /// counts it at hand-over), and the ring's own overflow counting keeps any
    /// loss honest. Returns false when there is no console to hand back to;
    /// the default bus owns none, so tests record the call instead.
    fn requeue(&self, _pkt: &[u8], _ts: u64, _sender: u32, _ftype: u32) -> bool {
        false
    }

    /// Hand the completed child image to the console so it can run: write its
    /// segments to the destinations the RSA frame stated, carry the session it
    /// negotiated into the console's own radio state, and point both CPUs at
    /// the entrypoints (the boot half of GBATEK's "jump to entrypoints").
    ///
    /// `session` is everything the hand-over needs to keep, and nothing more
    /// (see `Session`): a real client boots with the association its firmware's
    /// Download Play client negotiated still in place, so the console comes up
    /// already associated rather than as a bare, tuned radio. Returns false
    /// when the bus cannot boot it. The default bus owns no console, so tests
    /// record the call instead.
    fn handoff(&self, _image: &[u8], _info: &BootInfo, _session: &Session) -> bool {
        false
    }

    /// Advance the joiner one turn. This is the run loop's only entry point, and
    /// it is where the answer schedule has its one owner: the default runs the
    /// turn right here, which is what the in-process test bus wants, and a real
    /// console overrides it so the drain, the decision and the sends all happen
    /// *inside that console's own frame turn*. That difference is the whole
    /// point - an answer sent from another thread only lands at the console's
    /// next turn, one frame after the host opened the window that wanted it,
    /// which is what left Super Mario 64 DS's host with an empty window on
    /// 2,522 of its 2,535 addressed rounds while the client kept answering.
    fn turn(&self, dp: &Arc<DownloadPlayJoiner>, st: &Arc<Mutex<State>>, now: Instant) {
        let mut guard = st.lock().unwrap();
        step(self, dp, &mut guard, now);
    }
}

/// How often an unanswered management frame is repeated.
const RESEND: Duration = Duration::from_millis(250);
/// How long the run loop sleeps between *turns*. This is the schedule's only
/// knob, and it is deliberately short: a beacon arrives every ~128ms, but a
/// reply has to answer the host's own command round, and a host that answers
/// on a 100ms poll left most windows empty (`empty 3243` against a handful of
/// collections, measured). What each turn *costs* is not measured here - a turn
/// runs on the console's own driver thread (see `JoinerBus::turn`), so this is
/// the ask rate, not a latency budget.
const TICK: Duration = Duration::from_millis(5);

/// Bound on the unanswered-command queue. A step drains up to 32 frames, so a
/// batch larger than this is a client that has stopped keeping up; the oldest
/// go first, because the host has stopped asking about them.
const MAX_QUEUE: usize = 64;

/// How long after the last host frame the stored beacon is served back
/// post-boot (see `maybe_serve`). A talking host commands at ~60 rounds/s
/// and beacons every ~128ms, so a full second of nothing is a silent host,
/// not a gap - and a guest scan lasts seconds, so the serve lands inside it.
const SILENCE_BEFORE_SERVE: Duration = Duration::from_millis(1000);
/// How often a stored beacon is re-served while the host stays silent.
/// Several beacon intervals, so a scanning guest meets one on whatever
/// channel timing it keeps, without flooding the inbox between scans.
const SERVE_EVERY: Duration = Duration::from_millis(100);

/// How far *ahead* of the freshest frame this client has read a reply is
/// stamped, in emulated US-timer ticks. The host drops an answer older than two
/// frames when its window reads it (`LocalMP::kReplyStaleWindow`), and a
/// synthesized client does not answer inside the round that asked - it answers
/// on its next poll, with the host's own clock already a few frames on. The
/// staleness test only rejects an answer that is too *old*, so stamping with the
/// newest host-clock value read plus a small margin is the honest equivalent of
/// "this answer exists now": measured, without it the host's own reply window
/// logged `stale 107` and collected nothing. */
const REPLY_AHEAD_US: u64 = 2 * 16715;

/// Frame codec (802.11 parse/build) lives in `dp_frames` - the owner of what the
/// bytes mean. This file owns the client: presence, phase, transfer tracking,
/// the boot, the post-boot stand-in, and the run loop. The import below is the
/// whole coupling between the two.
pub(crate) use crate::dp_frames::*;

/// Pick the reply for `command` - the single place the drain arm and the re-send
/// arm both call, so the two can never disagree about what a class is answered
/// with.
///
/// `01` NameRequest, and a `00` NameDone before RSA, get a Username snippet;
/// `03` gets the RsaReply; `04` DataPacket gets the DataReply that advances the
/// download (or the GotAllReply once the image is complete); `05` Final gets the
/// FinalReply.
///
/// Everything else gets the *post-RSA* reply, and that default is the fix this
/// pass measured: the host's per-client mode machine, once its RSA round has
/// moved it to mode 14, waits for **one more RsaReply** before it will begin the
/// download (mode 6). Answering those rounds with the two-byte dummy parks the
/// host at 14 forever; answering them with the RsaReply moves it to 6, and the
/// host then streams a `04` DataPacket (measured 179 bytes, not its 3-byte
/// stub). `MD_DP_REPLY_FORCE` (and `MD_DP_REPLY_FORCE_DATA` for `04`) override
/// the type instead, so one build can be swept across reply types.
fn answer_reply(
    joiner: &[u8; 6],
    host: &HostBeacon,
    seq: u16,
    command: u8,
    st: &mut State,
) -> (Vec<u8>, &'static str) {
    match command {
        0x01 => (
            build_username_reply(joiner, host, seq, st.username_idx, &st.client_name),
            "username",
        ),
        0x03 => (build_rsa_reply(joiner, host, seq), "rsa"),
        0x00 if !st.saw_rsa => (
            build_username_reply(joiner, host, seq, st.username_idx, &st.client_name),
            "username(NameDone)",
        ),
        // The download itself. GBATEK's DataReply carries the smallest missing
        // packet number and how many distinct packets have arrived; answering
        // with it is what makes the host step through the image one packet per
        // round instead of repeating one packet forever. Once the RSA frame's
        // stated total has all arrived, GotAllReply ends the transfer.
        0x04 => match st.force_data_reply_type {
            Some(t) => (build_typed_reply(joiner, host, seq, t), "typed(data)"),
            None if st.image.complete() => (
                build_got_all_reply(joiner, host, seq, st.image.next_u16(), st.image.packets_u16()),
                "got-all",
            ),
            None => (
                build_data_reply(joiner, host, seq, st.image.next_u16(), st.image.packets_u16()),
                "data",
            ),
        },
        // The host's Final command - GBATEK's last step, confirmed with type 0Bh.
        0x05 => (
            build_final_reply(joiner, host, seq, st.image.next_u16(), st.image.packets_u16()),
            "final",
        ),
        _ => match st.force_reply_type {
            Some(t) => (build_typed_reply(joiner, host, seq, t), "typed"),
            None if st.saw_rsa => {
                // The one player-body probe: a booted client's first `00`
                // poll is answered with the nickname, not the repeated
                // RsaReply. An explicit `MD_DP_REPLY_FORCE` still wins (arm
                // above), and a muted stand-in never consumes the shot.
                if st.player_body
                    && !st.player_body_sent
                    && st.booted
                    && command == 0x00
                    && !st.mute_replies
                {
                    st.player_body_sent = true;
                    log::info!(
                        "[dp] MD_DP_PLAYER_BODY: answering one post-boot 00 poll with a \
                         UsernameReply (snippet 1, nickname) instead of rsa(post)"
                    );
                    (
                        build_username_reply(joiner, host, seq, 1, &st.client_name),
                        "player-body",
                    )
                } else {
                    (build_rsa_reply(joiner, host, seq), "rsa(post)")
                }
            }
            None => (build_dummy_reply(joiner, host, seq), "dummy"),
        },
    }
}

/// Where the transfer stands, in the protocol's own terms - the states
/// GBATEK's download passes through, as moved by the host's own statements.
///
/// This is the single owner of "may this client boot?", and it is a named
/// *value* rather than a conjunction of loose flags checked at the boot site.
/// That distinction is not cosmetic: the earlier shape (`got_all_ack && command
/// != Some(0x04)`) had no state to reset, so a host that re-registered this
/// client carried the previous session's flags into the new one and could boot
/// it before the new image had even been fully answered. A state has a
/// transition table, one place to advance it, and one place to reset it, and
/// every step is logged once, so a run's log reads as the protocol's own
/// sequence rather than as counters to be decoded afterwards.
///
/// The last transition is the one this project had to *measure*. GBATEK ends a
/// transfer with the host's `05` Final, which the client confirms with a
/// FinalReply and then boots; measured on Star Fox's own host, that Final is
/// never sent. With the completing round answered and its GotAllReply
/// collected, the host's class goes `04 -> 00` for good, its per-client mode
/// advances `6 -> 7`, and it keeps polling class-00 Dummy rounds with this
/// client still registered and no deauthentication for the rest of the run
/// (v0.1.9de, `logs/v9dpfinal_finding.txt`). Its `00` is the engine's own
/// "nothing pending": it has finished *sending*, so `Collected -> Closed` is
/// the host leaving the data class after it read this client's answer, not a
/// command it does not emit. The leaving-class half is also what makes the
/// answer attributable without per-reply bookkeeping, even though an ACK lags
/// the round it reports: a host stops sending `04` only *because* it collected
/// the completion answer. That is measured both ways - the silent-client arm of
/// v0.1.9dd left the host re-sending its last packet forever
/// (`collected=0000` round after round), while the answering arm of v0.1.9de
/// stopped within two rounds of the ACK. A GBATEK host is covered by the same
/// table: it cannot send its Final without having received the client's "I
/// have everything" reply first, so `Collected` is already true when that
/// Final arrives and the Final itself is the leaving-class step.
#[derive(Debug, PartialEq, Eq, PartialOrd, Ord, Clone, Copy)]
enum Transfer {
    /// No RSA frame has parsed: the host has not yet stated what it will send,
    /// so whatever it asks belongs to the name phase.
    Unstated,
    /// The RSA frame stated the image; its `04` packets are arriving.
    Streaming,
    /// Every byte the RSA frame stated has arrived.
    Complete,
    /// The host collected this client's answer that everything arrived - its
    /// CMD ACK stopped reporting this slave missing while the image was
    /// already complete.
    Collected,
    /// The host stopped asking for data after that. It has finished sending,
    /// and this is the state the boot waits for.
    Closed,
}

/// The transfer's state as `Counters::transfer` stores it, mapped back to the
/// name the log uses - so the control channel and a run's log never disagree
/// about which state a number means.
///
/// This exists because `Transfer` itself is private: the counter is an atomic
/// integer (the same shape every other counter in this file has), and the one
/// place that knows the mapping is here, next to the enum.
pub fn transfer_name(value: u64) -> &'static str {
    match value {
        1 => Transfer::Streaming.name(),
        2 => Transfer::Complete.name(),
        3 => Transfer::Collected.name(),
        4 => Transfer::Closed.name(),
        _ => Transfer::Unstated.name(),
    }
}

impl Transfer {
    /// The one-line name a transition earns in the log.
    fn name(self) -> &'static str {
        match self {
            Transfer::Unstated => "Unstated (no RSA frame yet)",
            Transfer::Streaming => "Streaming (the host is sending data packets)",
            Transfer::Complete => "Complete (every byte the RSA frame stated has arrived)",
            Transfer::Collected => "Collected (the host read this client's completion answer)",
            Transfer::Closed => "Closed (the host has stopped asking for data: the client may boot)",
        }
    }
}

/// Counters, read by the `dpjoiner` control command. Every one of them is zero
/// on a disarmed joiner, which is what makes the gate provable rather than
/// asserted.
#[derive(Default)]
pub struct Counters {
    pub beacons: AtomicU64,
    pub auth_sent: AtomicU64,
    pub auth_recv: AtomicU64,
    pub assoc_sent: AtomicU64,
    pub assoc_recv: AtomicU64,
    /// Dummy replies sent after the association was accepted - the first frame
    /// of the download handshake.
    pub replies_sent: AtomicU64,
    /// Frames from the host that are not a beacon or a management response -
    /// i.e. the host moving on to the next step.
    pub host_frames: AtomicU64,
    /// The host's own command frames (`fc=0228`) seen, the class of the last
    /// one, and the slave flags it carried (bit 1 = slave 1 connected).
    pub commands_recv: AtomicU64,
    pub last_command: AtomicU64,
    pub last_cmd_slave_flags: AtomicU64,
    /// The hardware CMD ACKs seen and, of those, how many said this client's
    /// reply *was* collected (its error-flag bit clear). This is the host's own
    /// statement that the answer went down the right path, and the counter that
    /// separates "the reply left" from "the host read it".
    pub acks_recv: AtomicU64,
    pub replies_collected: AtomicU64,
    /// Deauthentications the host addressed to this console, and the reason
    /// code of the most recent one (its own words for the refusal).
    pub deauths: AtomicU64,
    pub last_deauth_reason: AtomicU64,
    /// The status the host's most recent authentication response carried: 0 is
    /// an accepted authentication, anything else is a refusal.
    pub last_auth_status: AtomicU64,
    /// The download: distinct packets and bytes the host has delivered, the
    /// next packet the joiner is asking for, and the total the RSA frame
    /// stated. Together they are the progress a transfer is judged on.
    pub image_packets: AtomicU64,
    pub image_bytes: AtomicU64,
    pub image_next: AtomicU64,
    pub image_total_bytes: AtomicU64,
    /// The status and AID of the host's most recent association response. A
    /// status of 0 with a nonzero AID is a registered client; the first live
    /// Star Fox run read `status 0001`, AID `0000` - a refusal the earlier
    /// record had mistaken for acceptance, because nothing read this field.
    pub last_assoc_status: AtomicU64,
    pub last_assoc_aid: AtomicU64,
    /// Where the transfer stands, as `Transfer`'s discriminant - the one value
    /// the boot waits on, reported by `dpjoiner` so a run states whether it
    /// downloaded or downloaded *and* played without decoding a log.
    pub transfer: AtomicU64,
    /// How many times the hand-off re-attached the booted client to the bus
    /// (see the post-boot hold in `step`). Each one is a point at which the
    /// child's own wireless power cycle had detached the console and the router
    /// had therefore stopped writing it frames; zero after a boot means the
    /// child's game kept the radio, which is the state the hold exists for.
    pub radio_hold: AtomicU64,
    /// Frames the stand-in handed back to the booted game after reading them
    /// (see the requeue in `drain_step`). Post-boot the drain and the game's
    /// own CheckRX read the same per-slot queue, so every beacon the stand-in
    /// consumes is one the game's scan never sees; the join/scan set (beacons,
    /// auth/assoc responses) is restored for the game while the stand-in keeps
    /// answering polls. Zero on a title whose game joins without them.
    pub frames_requeued: AtomicU64,
    /// Stored host beacons the serve handed back while the host itself was
    /// silent (see `maybe_serve`). Zero on every title whose host keeps
    /// talking post-boot - the serve never fires there.
    pub beacons_served: AtomicU64,
}

/// The joiner's shared state. `armed` is the one gate; `joiner` says which
/// console acts as the client.
pub struct DownloadPlayJoiner {
    pub armed: AtomicBool,
    pub joiner: AtomicUsize,
    pub counters: Counters,
    /// Set once the completed image has been handed to the console - the
    /// terminal state of a successful download, and the gate that keeps the
    /// handoff to exactly one boot. Readable off the control channel so a run
    /// states whether it downloaded or downloaded *and* played.
    pub booted: AtomicBool,
}

impl DownloadPlayJoiner {
    pub fn new(armed: bool, joiner: usize) -> Self {
        DownloadPlayJoiner {
            armed: AtomicBool::new(armed),
            joiner: AtomicUsize::new(joiner),
            counters: Counters::default(),
            booted: AtomicBool::new(false),
        }
    }

    pub fn is_armed(&self) -> bool {
        self.armed.load(Ordering::SeqCst)
    }

    pub fn is_booted(&self) -> bool {
        self.booted.load(Ordering::SeqCst)
    }

    /// Arm or disarm. Disarming also forgets the host, so a later arming starts
    /// from a fresh scan rather than sending into a stale address.
    pub fn set_armed(&self, on: bool) {
        self.armed.store(on, Ordering::SeqCst);
    }
}

#[derive(Debug, PartialEq, Clone, Copy)]
enum Phase {
    Seek,
    Beaconed,
    Authed,
    Associated,
}

impl State {
    /// Retire the run loop: the console this loop serves has stopped, so
    /// further turns would queue into a dead driver. Returns true on the
    /// first call (the one that logs), false once already retired.
    pub fn retire(&mut self) -> bool {
        if self.retired {
            return false;
        }
        self.retired = true;
        true
    }

    /// Whether `retire` has been called - the run loop's exit condition.
    pub fn is_retired(&self) -> bool {
        self.retired
    }

    /// The session the boot carries across it: the channel, BSSID, game
    /// serial, stream token, capability, interval and rate mask off the host's
    /// beacon, plus the AID its association response granted — one fact
    /// (`Session`), built in exactly one place. Unassociated (no beacon yet)
    /// it is the untuned stranger the boot's guards already handle.
    fn session(&self) -> Session {
        let h = self.host.as_ref();
        Session {
            channel: h.map(|h| h.channel).unwrap_or(0),
            bssid: h.map(|h| h.bssid).unwrap_or([0u8; 6]),
            aid: self.aid,
            serial: h.map(|h| h.serial).unwrap_or([0u8; 4]),
            token: h.map(|h| h.token).unwrap_or([0u8; 2]),
            capability: h.map(|h| h.capability).unwrap_or(0),
            interval: h.map(|h| h.interval).unwrap_or(0),
            rate_mask: h.map(|h| rates_mask(&h.rates)).unwrap_or(0),
            beacon_size: h.map(|h| beacon_size(h.dd_len)).unwrap_or(0),
        }
    }

    /// A fresh sequence-control number for one transmission, as the hardware
    /// assigns: the captured client's authentication carries `seq=0000` and its
    /// association `seq=0010`.
    fn next_seq(&mut self) -> u16 {
        let s = self.seq;
        self.seq = self.seq.wrapping_add(0x10);
        s
    }

    /// Move the transfer on by what this step has observed, and log each new
    /// state once. Called from exactly one place (`step`, after the drain),
    /// so the states below can only ever advance in the order its comment
    /// names - and a state can never be *reached* by the boot before the step
    /// that observed the fact it is named for.
    ///
    /// Monotone on purpose: the host's statements arrive out of order (its
    /// class can leave `04` in the same drain as the ACK that collected the
    /// completion answer, or a step earlier), and a state that could move
    /// backwards would let the boot fire, un-fire and fire again. Each arm is
    /// the single statement that earns it:
    ///
    /// * `Unstated -> Streaming`: the RSA frame parsed and stated the image.
    /// * `Streaming -> Complete`: every byte that frame stated has arrived.
    /// * `Complete -> Collected`: the host's ACK stopped reporting this slave
    ///   missing while the image was already complete.
    /// * `Collected -> Closed`: the host's newest command is no longer a `04`
    ///   DataPacket - it has stopped asking for data.
    fn advance_transfer(&mut self, dp: &DownloadPlayJoiner) {
        // Walked to a fixpoint, not stepped once: one drain can carry both
        // statements that close a transfer (the ACK that collected the
        // completion answer *and* the host's class leaving `04`), and a stepper
        // would then need a second turn to notice what the first one already
        // had in hand.
        loop {
            let next = match self.transfer {
                Transfer::Unstated if self.saw_rsa => Transfer::Streaming,
                Transfer::Streaming if self.image.complete() => Transfer::Complete,
                Transfer::Complete if self.got_all_ack => Transfer::Collected,
                Transfer::Collected if self.command != Some(0x04) => Transfer::Closed,
                _ => break,
            };
            log::info!("[dp] the transfer is now {}", next.name());
            // Readable off the control channel as a number, the same way every
            // other milestone in this file is: `dpjoiner` maps it back to a
            // name through `transfer_name`. Stored as the enum's discriminant
            // so a new variant cannot silently collide with an old one.
            dp.counters
                .transfer
                .store(u64::from(next as u8), Ordering::SeqCst);
            self.transfer = next;
        }
    }
}

/// The download the host streams, packet by packet.
///
/// GBATEK's CMD DATA carries the packet number at body `09h` and the image at
/// `0Bh`, and the client answers with the **smallest missing packet number** (a
/// DataReply, type `09`), which is what makes the host advance one packet per
/// round. This is the image itself - one payload per packet number - so the
/// terminal state is the bytes the host streamed, not a tally of them.
/// `assemble` lays them out in packet order: the client's own copy of the child
/// image, ready for the handoff to boot.
#[derive(Default)]
struct Image {
    /// Packet `n`'s payload, or `None` while it is still missing.
    packets: Vec<Option<Vec<u8>>>,
    /// Distinct packets received, and the distinct bytes in them.
    received: u64,
    bytes: u64,
    /// The whole image's size in bytes, from the RSA frame, or 0 until seen.
    total_bytes: u64,
    /// The load map the same RSA frame stated: where the segments go and where
    /// the client jumps. `None` until an RSA frame parses.
    boot: Option<BootInfo>,
    /// The smallest packet number still missing - what the next DataReply asks
    /// for. Packet 0 is the image header (GBATEK), so it starts there.
    next: u32,
    /// The highest packet number seen, so progress can be reported.
    highest: u32,
    /// The once-only completion line.
    noted_done: bool,
}

impl Image {
    /// Record one delivered packet; true when it was one not seen before.
    ///
    /// The packet number is a `u16` off the wire, so the map is bounded by
    /// 64 KiB however corrupt the field is - no separate guard is needed.
    fn add(&mut self, packet: u16, data: &[u8]) -> bool {
        let n = packet as usize;
        if self.packets.len() <= n {
            self.packets.resize_with(n + 1, || None);
        }
        if self.packets[n].is_some() {
            return false;
        }
        self.packets[n] = Some(data.to_vec());
        self.received += 1;
        self.bytes += data.len() as u64;
        if (n as u32) > self.highest {
            self.highest = n as u32;
        }
        // Advance the request past every packet now held, so the pointer always
        // names the smallest hole and a reply can never ask for something the
        // host already sent.
        while (self.next as usize) < self.packets.len()
            && self.packets[self.next as usize].is_some()
        {
            self.next += 1;
        }
        true
    }

    /// Whether the whole image has arrived: a stated total, all of its bytes,
    /// and no hole left. Only ever true once the RSA frame has stated a total,
    /// so an unparsed frame can never end the transfer.
    fn complete(&self) -> bool {
        self.total_bytes > 0
            && self.bytes >= self.total_bytes
            && (self.next as usize) >= self.packets.len()
    }

    /// The collected image as one contiguous buffer in packet order, or `None`
    /// while any packet is missing. This is the client's own copy of the child
    /// image - what the handoff boots.
    fn assemble(&self) -> Option<Vec<u8>> {
        if self.total_bytes == 0 || self.bytes < self.total_bytes {
            return None;
        }
        let mut out = Vec::with_capacity(self.bytes as usize);
        for chunk in &self.packets {
            out.extend_from_slice(chunk.as_ref()?);
        }
        Some(out)
    }

    /// The next packet number to ask for, clamped to the wire's 16 bits.
    fn next_u16(&self) -> u16 {
        self.next.min(u32::from(u16::MAX)) as u16
    }

    /// Distinct packets received, clamped to the wire's 16 bits.
    fn packets_u16(&self) -> u16 {
        self.received.min(u64::from(u16::MAX)) as u16
    }
}

/// Everything the client knows between turns: the host it follows, the phase
/// it is in, the download so far, and the reply overrides. One owner - the
/// `run` loop holds it behind a `Mutex`, and a turn advances it either in
/// process (tests) or on the console's own driver thread (the real bus).
/// Whether this client speaks the Download Play protocol on the bus:
/// authentication/association requests and command replies.
///
/// Presence (bus attachment, decided in the gate) and tracking (the drain)
/// never consult this — only the send stage does, at exactly one point. That
/// split is the retirement seam: a guest-owned radio is presence without
/// speech, and flipping this is what hands the conversation to the console's
/// own game. `Speak` is the default and the only posture exercised live; a
/// `Silent` client still attaches, drains and tracks the transfer, but sends
/// nothing — and with no reply ever collected the transfer can never reach
/// `Closed`, so it can never boot either.
///
/// Set in code, never from the environment: per-game behavior has one gate
/// already (the registry plus the arm switch), and this file adds no `MD_*`
/// knob for it.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum ClientSpeech {
    /// Speak every round the phase machine earns: the working download.
    Speak,
    /// Attach, drain and track, but send nothing.
    Silent,
}

impl Default for ClientSpeech {
    fn default() -> Self {
        ClientSpeech::Speak
    }
}

pub struct State {
    host: Option<HostBeacon>,
    phase: Phase,
    /// Whether this turn's send stage speaks (see `ClientSpeech`).
    speech: ClientSpeech,
    last_auth: Option<Instant>,
    last_assoc: Option<Instant>,
    /// When the post-association dummy reply last went out (rate-limited like
    /// the other management frames).
    last_reply: Option<Instant>,
    /// The 802.11 sequence-control number for the next transmission.
    seq: u16,
    attempts: u64,
    mac: Option<[u8; 6]>,
    /// The nickname this console advertises (`Player 1`..`Player 4`), used for
    /// both the association request's SSID and every UsernameReply, so the host
    /// never sees two different names from one console. Set once, from the
    /// console's own slot, and equal to the firmware nickname the booted
    /// console keeps after the hand-over.
    client_name: String,
    /// Whether this console is attached to the bus at the bus level (see
    /// JoinerBus::set_receiving), so we attach once and detach once.
    listening: bool,
    /// Each protocol milestone is logged once, in full; the rest are counted.
    noted_auth: bool,
    noted_deauth: bool,
    /// The first host frame after association is logged once, so a run's log
    /// names what the host did rather than only that "something arrived".
    noted_host: bool,
    /// The host's answer to the association is logged once, whatever it says.
    noted_assoc_status: bool,
    /// The AID the host assigned in its association response, low nibble - what
    /// a reply must be stamped with to be filed under this client. Zero until
    /// the host has registered us.
    aid: u16,
    /// The class of the most recent command the host asked (CMD DATA byte 6),
    /// or `None` until it asks one. Every new class is logged once, so a run's
    /// log reads as the handshake's own sequence.
    command: Option<u8>,
    /// Which username snippet the next reply carries (0..=4). Advanced only
    /// when the host's ack says it collected the previous reply, so a lost or
    /// stale answer is repeated rather than skipped.
    username_idx: u8,
    /// The host has collected a reply since the last one this client sent.
    collected_since_send: bool,
    /// `MD_DP_REPLY_MUTE=1` holds every reply back after the association. This
    /// is the diagnostic arm for a question no wire reading can answer on its
    /// own: whether the host's own Download Play phase machine advances by
    /// itself or because of what this client answers. The joiner joins, sees
    /// the host and drains its command queue exactly as usual - only the send
    /// is skipped - so the difference between the two arms is the host's own
    /// response to silence.
    mute_replies: bool,
    /// `MD_DP_REPLY_FORCE=<hex byte>` (diagnostic): answer a command the state
    /// machine has no specific reply for with a *normal* reply carrying this
    /// third body byte as its reply type, instead of the two-byte dummy. The
    /// host's own per-client mode machine dispatches on this byte (a Username
    /// reply is type 7, `RsaReply` type 8), so sweeping it names which reply
    /// type the host is waiting for at each step. Unset leaves the dummy.
    force_reply_type: Option<u8>,
    /// `MD_DP_REPLY_FORCE_DATA=<hex byte>` (diagnostic): the same override, but
    /// applied to the `04` DataPacket rounds only. The download's later packet
    /// loop runs through the type-9/10 replies while the mode machine sits at
    /// mode 6, so this lets that phase be swept without disturbing the RsaReply
    /// that gets the host *to* mode 6. Unset answers `04` with the RsaReply.
    force_data_reply_type: Option<u8>,
    /// `MD_DP_PLAYER_BODY=1` (diagnostic): answer the first post-boot `00`
    /// poll with a UsernameReply carrying the client's nickname instead of
    /// the RsaReply the stand-in otherwise repeats. The host's game reads
    /// every reply body live (measured: it consumes the stand-in's RsaReply
    /// frames halfword by halfword) while its roster waits on guest player
    /// data no tap can supply, so this tests whether one plausible player
    /// body advances it past Recruitment. One shot per session: the latch
    /// resets with the rest of the session on deauthentication.
    player_body: bool,
    /// Whether the one `player_body` reply already went out this session.
    player_body_sent: bool,
    /// Whether the host has asked its RSA command yet. Until it has, a `00`
    /// (NameDone) command is still part of the name phase and is answered with
    /// the username - measured: a host that got a dummy on that round sent no
    /// RSA at all and parked, twice; the run that (by accident, holding the
    /// stale `NameRequest`) sent the snippet there went on to RSA.
    saw_rsa: bool,
    /// The host has acknowledged this client's answer that the whole image
    /// arrived: a CMD ACK stopped reporting this slave missing while the image
    /// was already complete. One of the facts `advance_transfer` reads.
    got_all_ack: bool,
    /// Where the transfer itself stands (see `Transfer`). Advanced in exactly
    /// one place - `advance_transfer`, once per step - and reset in exactly one
    /// place - the deauthentication arm. The boot reads this and nothing else.
    transfer: Transfer,
    /// Commands received and not yet answered, oldest first. The host streams a
    /// command every round - about three per emulated frame - so a drain batch
    /// holds several and the class can change inside one. The client answers the
    /// *newest* class in the batch and drops the rest: the reply path keeps only
    /// the newest answer per client (`MD_REPLY_KEEP=1`), so a stale-class answer
    /// is what the host's window would read. Answering the oldest class made the
    /// client reply `Dummy` for the whole RSA round - the host abandoned RSA
    /// after ~19 windows and our RsaReply landed in a later dummy round, which
    /// its game read and ignored. Answering *every* command in the batch is
    /// worse still: it floods the single-slot answer queue (measured: the host
    /// tore the association down, deauth reason 0001).
    queue: VecDeque<u8>,
    /// The first collected reply is logged once, so a run's log states the
    /// milestone rather than leaving it to be inferred from counters.
    noted_collected: bool,
    /// The freshest emulated timestamp read off the bus this step - the host's
    /// own US clock as it last looked, which is what a reply is stamped with
    /// (see REPLY_AHEAD_US).
    rx_ts: u64,
    /// The download itself: the packets the host has delivered and the next one
    /// still missing.
    image: Image,
    /// The completed image has been handed to the console (see `handoff`).
    /// Set once, so the boot happens exactly once.
    booted: bool,
    /// The firmware stand-in's first turn has been logged once (see the
    /// booted arm of `gate_step`).
    noted_standin: bool,
    /// The first frame handed back to the booted game has been logged once
    /// (see the requeue in `drain_step`).
    noted_requeue: bool,
    /// The latest host beacon's raw bytes, with the routing fields they
    /// arrived under. Served back post-boot while the host itself is silent
    /// (see `maybe_serve`) so a scanning guest finds the room; refreshed on
    /// every real beacon, so a talking host is always served fresh.
    serve_beacon: Option<(Vec<u8>, u32, u32)>,
    /// When a frame from the host was last drained. The serve fires only
    /// after a full `SILENCE_BEFORE_SERVE` with nothing, so a host that is
    /// still beaconing or commanding never sees a served frame.
    last_host_rx: Option<Instant>,
    /// When a stored beacon was last served. Rate-limits the serve to
    /// `SERVE_EVERY` so the inbox is not flooded between the game's scans.
    last_serve: Option<Instant>,
    /// The first served beacon has been logged once (see `maybe_serve`).
    noted_serve: bool,
    /// The console this loop serves is gone (its driver reported a stop),
    /// so the run loop retires instead of queueing turns nobody answers.
    /// Set once, through `retire`.
    retired: bool,
}

impl Default for State {
    fn default() -> Self {
        State {
            host: None,
            phase: Phase::Seek,
            speech: ClientSpeech::Speak,
            last_auth: None,
            last_assoc: None,
            last_reply: None,
            seq: 0x0000,
            attempts: 0,
            mac: None,
            client_name: CLIENT_NAME.to_string(),
            listening: false,
            mute_replies: false,
            force_reply_type: None,
            force_data_reply_type: None,
            player_body: false,
            player_body_sent: false,
            noted_auth: false,
            noted_deauth: false,
            noted_host: false,
            noted_assoc_status: false,
            aid: 0,
            command: None,
            username_idx: 0,
            collected_since_send: false,
            saw_rsa: false,
            got_all_ack: false,
            transfer: Transfer::Unstated,
            queue: VecDeque::new(),
            noted_collected: false,
            rx_ts: 0,
            image: Image::default(),
            booted: false,
            noted_standin: false,
            noted_requeue: false,
            serve_beacon: None,
            last_host_rx: None,
            last_serve: None,
            noted_serve: false,
            retired: false,
        }
    }
}

/// The Download Play command's own name, for the one-line log a new command
/// class earns (GBATEK CMD DATA byte 6).
fn command_name(command: u8) -> &'static str {
    match command {
        0x00 => "Dummy",
        0x01 => "NameRequest",
        0x02 => "Error: Wrong Game Serial",
        0x03 => "RSA",
        0x04 => "DataPacket",
        0x05 => "Final",
        _ => "unknown",
    }
}

/// What a booted Download Play child must still know when it takes over its
/// own radio: the session it negotiated with the host.
///
/// A real client does not come out of the transfer as a stranger. Its firmware's
/// Download Play client negotiated this session - it is tuned to the host's
/// channel, it is associated with the host's BSSID, and the host assigned it an
/// AID - and the hand-over into the downloaded image does not undo any of it:
/// that state lives in the wireless block, which survives the jump. This
/// emulator's boot is a whole-console reset (`NDS::SetupDownloadPlayBoot`), so
/// without this the child comes up with a reset radio and the host's beacons and
/// polls are addressed to a station that no longer exists; that is the shape
/// v0.1.9dg measures. The channel alone was the earlier version of this and is
/// kept as `channel`'s own case.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Session {
    /// The channel the host's own beacon advertised. A downloaded child does
    /// not scan for one, so the boot keeps it on the child's radio (0 leaves it
    /// untuned).
    pub channel: u8,
    /// The host's own BSSID, off its beacon: the station this client is
    /// associated *with*.
    pub bssid: [u8; 6],
    /// The AID the host assigned this console in its association response, low
    /// nibble (0 = unregistered). `Wifi` reports the same value as `MPMYAID`
    /// when the console is an MP client, and a poll is only ours to answer
    /// while it is set.
    pub aid: u16,
    /// The game serial (0040xxxx) off the beacon's `DD` tag: the firmware
    /// hands it to the child in the 027FFC42h handoff block's SSID field.
    pub serial: [u8; 4],
    /// The two stream bytes after the serial in the same tag: the handoff's
    /// SSID stream field.
    pub token: [u8; 2],
    /// Beacon capability, verbatim: the handoff's capabilities field.
    pub capability: u16,
    /// Beacon interval, verbatim: the handoff's interval field.
    pub interval: u16,
    /// The 1/2-Mbit rate bitmask GBATEK's handoff stages twice (bit0 = 1Mbit,
    /// bit1 = 2Mbit), derived from the beacon's four raw rate bytes.
    pub rate_mask: u16,
    /// The handoff's beacon-size field, computed off the `DD` tag's size the
    /// way GBATEK documents (`(dd - 8 + 0x41) / 2`).
    pub beacon_size: u16,
}

/// The 1/2-Mbit rate bitmask for the handoff block out of the beacon's four
/// raw rate bytes: bit0 when a 1Mbit rate is present (0x82 basic or 0x01),
/// bit1 when a 2Mbit rate is present (0x84 basic or 0x02).
fn rates_mask(rates: &[u8]) -> u16 {
    let mut mask = 0u16;
    if rates.contains(&0x82) || rates.contains(&0x01) {
        mask |= 0x0001;
    }
    if rates.contains(&0x84) || rates.contains(&0x02) {
        mask |= 0x0002;
    }
    mask
}

/// The handoff's beacon-size field off the `DD` tag's total length, per
/// GBATEK's documented calculation (`(88h - 08h + 41h) / 2` for its example
/// 0x88-long tag).
fn beacon_size(dd_len: usize) -> u16 {
    ((dd_len.saturating_sub(8) + 0x41) / 2) as u16
}

/// Boot the collected image: the image is the joiner's own contiguous copy, so
/// this is where "the bytes arrived" becomes "the download plays". Runs once.
///
/// The handoff deliberately does **not** deauthenticate the host. A real client's
/// association ends when its console reboots - the firmware jumps to the
/// downloaded image and the wireless block goes down with it, which is the
/// `MP_End` the emulator already performs when that console's radio powers off -
/// so a client protocol frame here is an extra, emulator-only step, and it is
/// measured to cost the download its own peer. Live (`logs/v9sfc_dp_wait1.log`,
/// frames 5289..5318): within ten frames of receiving it the host's game
/// broadcast a deauthentication of its own (reason 3, `fc=00C0`, 30 byte frame,
/// `0009BF112233 -> FFFFFFFFFFFF`) and powered its access point down - it disarmed
/// its beacon (`txslot BEACON <- 0000`), disabled the beacon countdown
/// (`USCOMPARECNT disabled: IRQ14 cannot be raised ... this console sends no
/// beacon of its own`) and switched its radio off (`WIFI: OFF`), leaving the link
/// group empty (`link mask 0x0`). The title that had just booted on the client
/// therefore came up with no Download Play host to associate with and no frames
/// routed to it, made two four-frame attempts at its radio and ended on its own
/// "Communication error. / Please turn your DS off." screen.
fn handoff<B: JoinerBus + ?Sized>(bus: &B, dp: &DownloadPlayJoiner, st: &mut State, _us: &[u8; 6]) {
    if st.booted {
        return;
    }
    if st.host.is_none() {
        return;
    }
    // The session the child keeps: channel, BSSID and AID as one fact (see
    // `State::session`). A downloaded child does not scan for a channel - it
    // was tuned to the host's when the image arrived - and a child that came
    // up with only the channel was still a stranger to the host.
    let session = st.session();
    let Some(boot) = st.image.boot else {
        log::warn!(
            "[dp] the download completed but the host's RSA frame stated no load map; \
             there is nothing to boot"
        );
        st.booted = true;
        return;
    };
    let Some(image) = st.image.assemble() else {
        return;
    };
    st.booted = true;

    // No deauthenticate here, on purpose: see this function's own comment.
    let ok = bus.handoff(&image, &boot, &session);
    dp.booted.store(ok, Ordering::SeqCst);
    log::info!(
        "[dp] handoff: {} the {} byte image, carrying this console's session across the \
         boot (channel {}, BSSID {:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X}, AID {}, \
         serial {:02X}{:02X}{:02X}{:02X}, token {:02X}{:02X}, caps {:04X}, interval {:04X}, \
         rates {:04X}, beacon size {:04X}) \
         (ARM9 entry {:08X} -> {:08X}, ARM7 entry {:08X} -> {:08X}, temp {:08X}; header {:08X})",
        if ok { "booted" } else { "could not boot" },
        image.len(),
        session.channel,
        session.bssid[0], session.bssid[1], session.bssid[2],
        session.bssid[3], session.bssid[4], session.bssid[5],
        session.aid,
        session.serial[0], session.serial[1], session.serial[2], session.serial[3],
        session.token[0], session.token[1],
        session.capability,
        session.interval,
        session.rate_mask,
        session.beacon_size,
        boot.arm9_entry,
        boot.arm9_dest,
        boot.arm7_entry,
        boot.arm7_dest,
        boot.arm7_temp,
        boot.header_dest
    );
}

/// What one turn may do: presence (bus attachment) and speech (protocol
/// frames) are decided together, once, before anything drains or sends — so a
/// later pass can hold the console on the bus for its own game without
/// touching the protocol below.
enum StepGate {
    /// Nothing to do this turn: disarmed (after dropping any hold), the bus
    /// refused the attach, or the game's own radio is up and it owns the bus.
    Idle,
    /// The client is booted: presence is held across the child's own power
    /// cycles, but nothing is drained and nothing is answered — those frames
    /// belong to the booted game.
    Hold,
    /// Run the turn: drain, track, speak, maybe boot. Carries our MAC.
    Run([u8; 6]),
}

/// The gate: the only place that decides whether this turn touches the bus at
/// all, and in which posture. Everything below it (drain, send, boot) runs
/// only on `Run`.
fn gate_step<B: JoinerBus + ?Sized>(bus: &B, dp: &DownloadPlayJoiner, st: &mut State) -> StepGate {
    // A disarmed joiner touches the bus zero times (bar dropping an attachment
    // it already made).
    if !dp.is_armed() {
        if st.listening {
            bus.set_receiving(false);
            st.listening = false;
        }
        return StepGate::Idle;
    }
    if st.mac.is_none() {
        st.mac = bus.own_mac();
    }
    let Some(us) = st.mac else {
        return StepGate::Idle;
    };

    // A booted client is finished *downloading*, and the gate's two postures
    // below split on who owns the conversation now. While the booted game's
    // own radio is up, it owns the bus outright: the drain would steal the
    // frames its receive path is about to read, so the joiner holds presence
    // without touching a frame. While the radio is off, the game can hear
    // nothing - and a host that hears nothing deauthenticates the silent
    // client (~1.5 s post-boot on the live route: beacon, broadcast deauth,
    // then silence from a host that will not speak again). So the joiner
    // answers the host's polls as the firmware stand-in until the game's own
    // radio comes up: the same drain/track/send as the download, minus the
    // boot. The stand-in never fights the game for a frame (it only runs
    // while the radio is off, when the game's own CheckRX reads nothing) and
    // it retires by itself the moment the radio is up - Hold takes over and
    // nothing is drained or answered. Measured shape this answers: the
    // child's waits are all for external progress (peer nibbles, RX-driven
    // flags) while its radio sits down and the host has parked, so with no
    // stand-in there is no first CMD the game could ever hear.
    if st.booted {
        if bus.wireless_on() {
            return StepGate::Hold;
        }
        if !bus.receiving() {
            if !bus.set_receiving(true) {
                return StepGate::Hold; // the bus refused; hold presence only
            }
            st.listening = true;
            let n = dp.counters.radio_hold.fetch_add(1, Ordering::SeqCst) + 1;
            log::info!(
                "[dp] the booted client's own game is not on the radio; holding this console \
                 on the bus so the router keeps writing it frames (hold #{n}) - answering as \
                 the firmware stand-in until the game's own radio comes up"
            );
        }
        st.listening = true;
        if !st.noted_standin {
            st.noted_standin = true;
            log::info!(
                "[dp] firmware stand-in: answering the host's polls for the booted client \
                 while its game's radio is off, so the host keeps polling instead of \
                 deauthenticating a silent client; silent the moment the game owns the radio"
            );
        }
        return StepGate::Run(us);
    }

    // If this console's own game is on the radio, it owns the bus: draining
    // would steal the frames its own receive path is about to read. A Download
    // Play client is by definition a console whose game is not in multiplayer.
    if bus.wireless_on() {
        return StepGate::Idle;
    }

    // The client's radio, in the one form this architecture has that does not
    // power the game's: attach at the bus level, so the host's beacon is routed
    // to this console even though nothing on it is playing. Re-asserted rather
    // than assumed, because the console's own game can power its radio on and
    // off while booting, and the off half detaches it from the bus again (the
    // first live run of this joiner heard no beacon at all until this check
    // existed: the game's boot-time power cycle landed on top of the attach).
    if !bus.receiving() {
        if !bus.set_receiving(true) {
            return StepGate::Idle; // the bus refused; nothing to do this step
        }
        log::info!("[dp] joiner listening for a download-play beacon");
    }
    st.listening = true;
    StepGate::Run(us)
}

/// One step of the state machine: gate, drain, track, send, maybe boot.
///
/// The stages each have one owner (`gate_step`, `drain_step`,
/// `advance_transfer`, `send_step`, `maybe_boot`); this function only runs
/// them in order. The schedule is unchanged: a beacon earns authentication
/// and, once authenticated, an association request (when armed and this
/// console's game is not using the radio).
///
/// Kept private (the tests below drive it from inside this module); `run` is
/// its only other caller.
/// Serve the stored host beacon back into this console's own inbox while the
/// host itself is silent post-boot (see the `SILENCE_BEFORE_SERVE` gate).
///
/// Why this exists: a scan-first guest (Bomberman, Mario Party DS) brings
/// its radio up after the boot and scans for the room, but on those titles
/// the host has stopped transmitting by then - no beacons arrive, the
/// requeue has nothing to hand back, and the scan fails while the stand-in
/// answers polls for a client that never joins. Re-serving the last real
/// beacon gives the scan the room it is looking for; the guest's own
/// association then goes to the real host, which is what unparks the
/// session if it is still reading. Runs ahead of the gate on purpose: the
/// game scans with its radio *on*, which is the posture the gate holds on.
/// A talking host (Tetris, Star Fox: commands every round) never trips the
/// silence gate, so those titles never see a served frame.
pub fn maybe_serve<B: JoinerBus + ?Sized>(bus: &B, dp: &DownloadPlayJoiner, st: &mut State, now: Instant) {
    if !st.booted || !dp.is_armed() {
        return;
    }
    let Some((bytes, sender, ftype)) = &st.serve_beacon else {
        return;
    };
    let silent = st
        .last_host_rx
        .map_or(true, |t| now.saturating_duration_since(t) >= SILENCE_BEFORE_SERVE);
    if !silent {
        return;
    }
    if st
        .last_serve
        .map_or(false, |t| now.saturating_duration_since(t) < SERVE_EVERY)
    {
        return;
    }
    // The bus timestamp is the freshest host clock read plus the wall time
    // since, the honest equivalent of "the host's clock now" for a host
    // that has stopped speaking. Under our own slot, so the drain reads it
    // as an echo - bounced for the game's scan, kept out of the silence
    // clock and the counters.
    let elapsed_us = st
        .last_host_rx
        .map_or(0, |t| now.saturating_duration_since(t).as_micros().min(u128::from(u64::MAX)) as u64);
    let ts = st.rx_ts.wrapping_add(elapsed_us);
    let serve_sender = bus.own_slot().unwrap_or(*sender);
    if bus.requeue(bytes, ts, serve_sender, *ftype) {
        dp.counters.beacons_served.fetch_add(1, Ordering::SeqCst);
        st.last_serve = Some(now);
        if !st.noted_serve {
            st.noted_serve = true;
            log::info!(
                "[dp] serving the stored host beacon back while the host is silent, \
                 so the booted game's scan finds the room (every {:?}); the guest's \
                 own association still goes to the real host",
                SERVE_EVERY
            );
        }
    }
}

pub fn step<B: JoinerBus + ?Sized>(bus: &B, dp: &DownloadPlayJoiner, st: &mut State, now: Instant) {
    // Ahead of the gate: the serve is for the game's own scan, which runs
    // with its radio on - the posture the gate holds on.
    maybe_serve(bus, dp, st, now);
    let StepGate::Run(us) = gate_step(bus, dp, st) else {
        return;
    };

    drain_step(bus, dp, st, us, now);

    // The transfer's one writer, and it runs only here: after the drain has
    // recorded everything the host said this turn, before anything this step
    // decides to send. See `advance_transfer` for the transition table.
    st.advance_transfer(dp);

    let Some(host) = st.host.clone() else { return };
    send_step(bus, dp, st, host, us, now);
    maybe_boot(bus, dp, st, &us);
}

/// The drain: everything queued for this console, classified into the state.
/// Owns the read side and nothing else — what it records, the send stage
/// answers and the transfer machine advances on.
fn drain_step<B: JoinerBus + ?Sized>(bus: &B, dp: &DownloadPlayJoiner, st: &mut State, us: [u8; 6], now: Instant) {
    if let Some(packets) = bus.drain_full() {
        // Our own served beacons come back through this same drain (the serve
        // hands them to our own inbox, which is what the game reads). They
        // are availability, not evidence: bounce them for the game's scan
        // without touching the host-silence clock, the reply stamp, or any
        // counter - otherwise the serve would feed its own trigger.
        let own = bus.own_slot();
        for (pkt, ts, sender, ftype) in &packets {
            if own.map_or(false, |s| s == *sender) {
                bus.requeue(pkt, *ts, *sender, *ftype);
                continue;
            }
            // Any other frame in this inbox is the host's: the bus never
            // routes our own sends back to us. This is the silence clock the
            // serve reads - refreshed by beacons, commands, responses, and
            // deauths alike, because any of them is a talking host.
            st.last_host_rx = Some(now);
            // The newest frame's own timestamp is the closest thing to the
            // host's current clock a client can read, and it is what a reply is
            // stamped with (see REPLY_AHEAD_US). Read off every frame,
            // including ones handed back below - a restored beacon carries the
            // freshest host clock seen, and the stamp wants exactly that.
            st.rx_ts = *ts;
            match classify(pkt, &us) {
                Some(rx @ (Rx::Beacon(_) | Rx::AuthResponse { .. } | Rx::AssocResponse { .. }))
                    if st.booted =>
                {
                    // A real post-boot beacon refreshes the serve's copy, so a
                    // host that talks for minutes and then goes silent is
                    // served fresh, not stale. (Echoes never reach here.)
                    if let Rx::Beacon(_) = rx {
                        st.serve_beacon = Some(((*pkt).clone(), *sender, *ftype));
                    }
                    // Post-boot these frames belong to the booted game, not the
                    // stand-in. The drain and the game's own CheckRX read the
                    // same per-slot queue (`RecvPacket` both ways), so a beacon
                    // consumed here is one the game's scan never sees - and a
                    // game that scans before it powers up then waits forever
                    // while the stand-in answers polls for a client that never
                    // joins (measured: Bomberman's guest `answered 0` with the
                    // host's `06` rounds starving, while Tetris's guest, which
                    // associates off the staged parameters, joins and plays).
                    // Restored to the inbox tail, in arrival order behind the
                    // frames after them; the stand-in keeps answering polls, so
                    // the host stays fed while the game scans. The phase and
                    // AID stay the download's own - the game earns its own
                    // association now, and adopting its answers here would
                    // answer the host under two identities at once.
                    if bus.requeue(pkt, *ts, *sender, *ftype) {
                        dp.counters.frames_requeued.fetch_add(1, Ordering::SeqCst);
                        if !st.noted_requeue {
                            st.noted_requeue = true;
                            log::info!(
                                "[dp] leaving the join/scan frames for the booted game: \
                                 beacons and auth/assoc responses go back to this console's \
                                 inbox so its scan sees the host while the stand-in keeps \
                                 answering polls"
                            );
                        }
                    }
                    continue;
                }
                Some(Rx::Beacon(b)) => {
                    let counters = &dp.counters;
                    counters.beacons.fetch_add(1, Ordering::SeqCst);
                    if st.host.as_ref().map(|h| h.mac) != Some(b.mac) {
                        let hex = |v: &[u8]| {
                            v.iter()
                                .map(|x| format!("{x:02X}"))
                                .collect::<Vec<_>>()
                                .join(" ")
                        };
                        log::info!(                             "[dp] hearing a download-play beacon from {:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X} \
                              (bssid {:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X}, channel {}, game serial {:02X}{:02X}{:02X}{:02X}, \
                              client token {:02X}{:02X}, capability {:04X}, interval {:04X}, rates [{}], dd {}B, ssid [{}])",
                            b.mac[0], b.mac[1], b.mac[2], b.mac[3], b.mac[4], b.mac[5],
                            b.bssid[0], b.bssid[1], b.bssid[2], b.bssid[3], b.bssid[4], b.bssid[5],
                            b.channel, b.serial[3], b.serial[2], b.serial[1], b.serial[0],
                            b.token[0], b.token[1], b.capability, b.interval, hex(&b.rates), b.dd_len, hex(&b.ssid)
                        );
                    }
                    st.host = Some(b);
                    // The serve's copy, refreshed on every real beacon (see
                    // `maybe_serve`). Stored pre-boot because a silent host is
                    // exactly when no fresh one will arrive.
                    st.serve_beacon = Some(((*pkt).clone(), *sender, *ftype));
                    if st.phase == Phase::Seek {
                        st.phase = Phase::Beaconed;
                    }
                }
                Some(Rx::AuthResponse { seq, status }) => {
                    dp.counters.auth_recv.fetch_add(1, Ordering::SeqCst);
                    dp.counters
                        .last_auth_status
                        .store(u64::from(status), Ordering::SeqCst);
                    if !st.noted_auth {
                        st.noted_auth = true;
                        log::info!(
                            "[dp] the host's authentication response: sequence {seq}, status \
                             {status:04X} - {}. An association request now is a class-2 frame, \
                             which a host that considers this station unauthenticated answers \
                             with a deauthentication (reason 0006).",
                            if status == 0 {
                                "accepted, sending an association request"
                            } else {
                                "REFUSED - the transaction was answered but not granted"
                            }
                        );
                    }
                    // The association is sent either way, on purpose: if the
                    // status was non-zero the deauth it draws is the host's own
                    // statement of why, and that is the frame worth capturing.
                    if st.phase != Phase::Associated {
                        st.phase = Phase::Authed;
                    }
                }
                Some(Rx::AssocResponse { status, aid }) => {
                    dp.counters.assoc_recv.fetch_add(1, Ordering::SeqCst);
                    dp.counters
                        .last_assoc_status
                        .store(u64::from(status), Ordering::SeqCst);
                    dp.counters
                        .last_assoc_aid
                        .store(u64::from(aid), Ordering::SeqCst);
                    // A registered client is status 0 *with* an assigned AID -
                    // the same test the host's own `[HOST] syncing client` line
                    // and `IsMPClient` make. `status 0000, aid 0000` is not a
                    // registration; it is a host that answered without keying
                    // a client in, and a nonzero status is a stated refusal.
                    if status == 0 && aid != 0 {
                        st.aid = aid & 0x0F;
                        if st.phase != Phase::Associated {
                            log::info!(
                                "[dp] the host registered this client: association status 0000, \
                                 AID {aid:04X} (slave {}) - the download handshake is open; \
                                 anything the host sends next is the protocol",
                                st.aid
                            );
                            st.phase = Phase::Associated;
                        }
                    } else {
                        if !st.noted_assoc_status {
                            st.noted_assoc_status = true;
                            log::info!(
                                "[dp] the host REFUSED the association: status {status:04X}, \
                                 AID {aid:04X}. A registered client is status 0 with a nonzero \
                                 AID; this is not one, so its roster stays empty and its \
                                 download code never starts. The association is retried."
                            );
                        }
                        if st.phase != Phase::Associated {
                            st.phase = Phase::Authed;
                        }
                    }
                }
                Some(Rx::Deauth { reason }) => {
                    dp.counters.deauths.fetch_add(1, Ordering::SeqCst);
                    dp.counters
                        .last_deauth_reason
                        .store(u64::from(reason), Ordering::SeqCst);
                    if !st.noted_deauth {
                        st.noted_deauth = true;
                        log::info!(
                            "[dp] the host refused the association: it sent a deauthentication \
                             (fc=00C0) addressed to this console with reason {reason:04X}. That is \
                             the first frame the host refuses, and the point the rest of the client \
                             protocol has to be written against."
                        );
                    }
                    // Past the boot there is no association left to re-earn:
                    // the game owns it now (see the session the handoff
                    // carried), and tearing the stand-in down here would be
                    // the silence that draws the next deauth. Counted above,
                    // otherwise ignored.
                    if st.booted {
                        continue;
                    }
                    // A real client starts over: authenticate again before
                    // associating. That keeps the exchange alive and makes the
                    // refusal reproducible rather than a one-shot race; the
                    // resend rate limits keep it from becoming a flood.
                    st.phase = Phase::Beaconed;
                    st.last_auth = Some(now);
                    st.last_assoc = None;
                    st.last_reply = None;
                    st.saw_rsa = false;
                    st.username_idx = 0;
                    st.player_body_sent = false;
                    st.queue.clear();
                    st.collected_since_send = false;
                    // A refused client starts the download over: the collected
                    // packets belong to the session the host just tore down.
                    st.image = Image::default();
                    // ... and so does the transfer. This is the reset the old
                    // predicate had no home for: a host that deauthenticated
                    // and then re-registered this client used to leave
                    // `got_all_ack` (and the completion of the *old* image)
                    // standing, so the new session could satisfy the boot
                    // condition before it had answered a single new round.
                    st.got_all_ack = false;
                    st.transfer = Transfer::Unstated;
                    dp.counters.transfer.store(Transfer::Unstated as u64, Ordering::SeqCst);
                }
                Some(Rx::Command {
                    command,
                    size,
                    flags,
                    slave_flags,
                    packet,
                    data,
                    rsa_total,
                    rsa_boot,
                }) => {
                    // Only this client's host can ask: a command is a broadcast
                    // on the multimedia bus, so the sender is checked rather
                    // than the address.
                    dp.counters.commands_recv.fetch_add(1, Ordering::SeqCst);
                    dp.counters
                        .last_cmd_slave_flags
                        .store(u64::from(slave_flags), Ordering::SeqCst);
                    // The RSA frame states how big the whole image is; it is the
                    // benchmark the collected bytes are reported against, so it
                    // is taken before the ordinary "a new class arrived" log.
                    if let Some(total) = rsa_total {
                        if st.image.total_bytes == 0 {
                            st.image.total_bytes = total;
                            dp.counters.image_total_bytes.store(total, Ordering::SeqCst);
                            log::info!(
                                "[dp] the host's RSA frame states the image is {total} bytes \
                                 (header + ARM9 + ARM7); the download's progress is measured \
                                 against it"
                            );
                        }
                    }
                    // The RSA round is remembered where the frame is *read*, not
                    // where the newest answer is built. Only the newest command of
                    // a drain batch is answered (an older round's reply window has
                    // already passed), and the host's own scheduler is free to put
                    // the RSA and the dummy round after it in the same 5 ms tick -
                    // so latching this in the send arm alone leaves every round
                    // after a batched RSA answered as a Username reply, and the
                    // host's mode machine never leaves 14. Measured on Super Mario
                    // 64 DS's host, whose commands arrive in tight bursts.
                    if command == 0x03 {
                        st.saw_rsa = true;
                        if let Some(boot) = rsa_boot {
                            if st.image.boot.is_none() {
                                st.image.boot = Some(boot);
                            }
                        }
                    }
                    // The download itself: a `04` DataPacket's number and data are
                    // the image. Each new packet is folded in, and the milestones
                    // are logged once each so the log carries the growth rather
                    // than only the counters doing so.
                    if command == 0x04 {
                        if let Some(packet) = packet {
                            let first = st.image.received == 0;
                            if st.image.add(packet, &data) {
                                dp.counters.image_packets.store(st.image.received, Ordering::SeqCst);
                                dp.counters.image_bytes.store(st.image.bytes, Ordering::SeqCst);
                                dp.counters.image_next.store(u64::from(st.image.next), Ordering::SeqCst);
                                if first {
                                    let n = data.len().min(8);
                                    log::info!(
                                        "[dp] the download's first packet: #{} with {} bytes, \
                                         starting {:02X?}",
                                        packet,
                                        data.len(),
                                        &data[..n]
                                    );
                                } else if st.image.received % 64 == 0 {
                                    log::info!(
                                        "[dp] download progress: {} packets, {} of {} bytes, \
                                         next wanted #{} (highest #{})",
                                        st.image.received,
                                        st.image.bytes,
                                        st.image.total_bytes,
                                        st.image.next,
                                        st.image.highest
                                    );
                                }
                                if st.image.complete() && !st.image.noted_done {
                                    st.image.noted_done = true;
                                    log::info!(
                                        "[dp] the download is complete: {} distinct packets, {} \
                                         of the {} bytes the RSA frame stated",
                                        st.image.received,
                                        st.image.bytes,
                                        st.image.total_bytes
                                    );
                                    // *Not* booted here. This round still has to
                                    // be answered - answer_reply gives a ``04``
                                    // that completes the image the GotAllReply -
                                    // and booting the console would skip that
                                    // send (and the whole send path below it),
                                    // leaving the host's window with nothing and
                                    // the download unconcluded. Measured: with
                                    // the boot here, every round after this line
                                    // read `collected=0000` (v0.1.9dd); with the
                                    // round answered instead, the host collects
                                    // the GotAllReply (v0.1.9de). The boot is at
                                    // the end of the associated phase.
                                }
                            }
                        }
                    }                    // Every command is a round to answer, not only a new class.
                    if st.queue.len() >= MAX_QUEUE {
                        st.queue.pop_front();
                    }
                    st.queue.push_back(command);
                    if st.command != Some(command) {
                        st.command = Some(command);
                        dp.counters.last_command.store(u64::from(command), Ordering::SeqCst);
                        log::info!(
                            "[dp] the host's Download Play command is now {command:02X} \
                             ({}), size {size}, flags {flags:02X}, slave flags {slave_flags:04X}",
                            command_name(command)
                        );
                    }
                }
                Some(Rx::CmdAck { error_flags }) => {
                    dp.counters.acks_recv.fetch_add(1, Ordering::SeqCst);
                    // The host's error flags: bit N set means slave N did not
                    // answer. Our bit *clear* is the host saying it collected
                    // our reply - the one piece of feedback the protocol gives.
                    if st.aid >= 1 && st.aid <= 15 && (error_flags & (1 << st.aid)) == 0 {
                        st.collected_since_send = true;
                        dp.counters.replies_collected.fetch_add(1, Ordering::SeqCst);
                        // With the image already complete, this ACK is the host
                        // saying it read the answer that everything arrived -
                        // the fact that earns `Transfer::Collected` (see
                        // `advance_transfer`, which is the only reader of it).
                        if st.image.complete() {
                            st.got_all_ack = true;
                        }
                        if !st.noted_collected {
                            st.noted_collected = true;
                            log::info!(
                                "[dp] the host's reply window collected this client's answer: its \
                                 CMD ACK no longer reports slave {} missing (error flags \
                                 {error_flags:04X})",
                                st.aid
                            );
                        }
                    }
                }
                Some(Rx::Other { fc, from }) => {
                    if Some(from) == st.host.as_ref().map(|h| h.mac) {
                        dp.counters.host_frames.fetch_add(1, Ordering::SeqCst);
                        if !st.noted_host {
                            st.noted_host = true;
                            log::info!("[dp] a frame from the host that is not a beacon or a handshake response: fc={fc:04X}");
                        }
                    }
                }
                None => {}
            }
        }
    }
}

/// The send stage: the phase machine's answers — authentication, association,
/// and one reply per round the host asked, on the reply path its window reads.
///
/// Consults `ClientSpeech` first and only here: a `Silent` client keeps the
/// presence the gate gave it and everything the drain recorded, but speaks no
/// frame. That is the whole retirement seam — presence without speech — and
/// the default (`Speak`) is the old behavior, line for line.
fn send_step<B: JoinerBus + ?Sized>(
    bus: &B,
    dp: &DownloadPlayJoiner,
    st: &mut State,
    host: HostBeacon,
    us: [u8; 6],
    now: Instant,
) {
    if st.speech == ClientSpeech::Silent {
        return;
    }

    // Past the boot the joiner is the firmware stand-in (see the booted arm
    // of `gate_step`): it answers the host's polls and nothing else. The
    // association is the game's now, so authentication/association frames
    // from here would speak for a handshake that already happened.
    if st.booted && st.phase != Phase::Associated {
        return;
    }

    match st.phase {
        Phase::Beaconed => {
            // Not yet authenticated (or the response is still on its way): send
            // authentication, repeating it until the host answers.
            if st.last_auth.map_or(true, |t| now.duration_since(t) >= RESEND) {
                st.attempts += 1;
                let seq = st.next_seq();
                let pkt = build_auth_request(&us, &host, seq);
                let ts = host.clock.wrapping_add(st.attempts * 512);
                let queued = bus.send(&pkt, ts);
                if queued.map_or(false, |n| n > 0) {
                    let n = dp.counters.auth_sent.fetch_add(1, Ordering::SeqCst) + 1;
                    log::debug!(
                        "[dp] sent authentication #{n} ({} bytes, fc=00B0) to the host",
                        pkt.len()
                    );
                }
                st.last_auth = Some(now);
            }
        }
        Phase::Authed => {
            if st.last_assoc.map_or(true, |t| now.duration_since(t) >= RESEND) {
                st.attempts += 1;
                let seq = st.next_seq();
                let pkt = build_assoc_request(&us, &host, seq, &st.client_name);
                let ts = host.clock.wrapping_add(st.attempts * 512);
                let queued = bus.send(&pkt, ts);
                if queued.map_or(false, |n| n > 0) {
                    let n = dp.counters.assoc_sent.fetch_add(1, Ordering::SeqCst) + 1;
                    log::debug!(
                        "[dp] sent association request #{n} ({} bytes, fc=0000) to the host",
                        pkt.len()
                    );
                }
                st.last_assoc = Some(now);
            }
        }
        Phase::Associated => {
            // The host registered this client. Answer its commands the way a
            // real DS client does - as *replies*, on the reply path the host's
            // window reads, stamped with the AID the host assigned - and build
            // each answer from the command it answers. The handshake's first
            // command is a NameRequest (GBATEK), answered with UsernameReply
            // snippets; until one has been seen there is nothing to answer.
            //
            // Re-sent on the management frames' schedule rather than once: the
            // host keeps the newest answer per client, so a reply its window
            // missed is just repeated, and the snippet only advances when the
            // host's own ack says the previous one was collected.
            // Every round this batch carries is answered, and the answers leave
            // in the *same console turn* that drained them (`JoinerBus::turn`
            // runs the whole turn on the console's own thread), so they are in
            // the host's queue before its next window opens instead of arriving
            // a frame late. Answering only the newest round was the earlier
            // policy and it is what starved this host: a drain covers a whole
            // emulated frame and a real Download Play host asks several rounds
            // inside one (measured on Super Mario 64 DS: ~8.6), so the single
            // round that got an answer was usually one whose window had already
            // passed - the host collected 31 of 2,535 and voided 278 more as
            // stale. The surplus answers cost nothing now (they are direct sends
            // inside the turn, not one driver round trip each) and the host's
            // own re-anchor keeps the newest answer per client
            // (`MD_REPLY_KEEP=1`), so the round its window reads is always the
            // freshest one in the batch.
            let newest = st.queue.back().copied();
            // The host's ack says it collected the last reply this client sent,
            // and that is what advances the username snippet - but only when
            // the client is still being asked for the username.
            if st.collected_since_send {
                st.collected_since_send = false;
                if st.username_idx < 4 && newest == Some(0x01) {
                    st.username_idx += 1;
                }
            }
            let batch: Vec<u8> = st.queue.drain(..).collect();
            let mut sent_any = false;
            if !batch.is_empty() {
                st.attempts += 1;
                let ts = st.rx_ts.wrapping_add(REPLY_AHEAD_US);
                for command in batch {
                    let seq = st.next_seq();
                    // The body is built from the command it answers - see
                    // answer_reply, which owns that mapping for both arms.
                    // Whether the RSA has been seen is not decided here: it is
                    // latched where the frame is read (see the drain loop), so a
                    // batched RSA still governs the rounds that follow it.
                    let (pkt, what) = answer_reply(&us, &host, seq, command, st);
                    let queued = if st.mute_replies {
                        None
                    } else {
                        bus.send_reply(&pkt, ts, st.aid)
                    };
                    if queued.map_or(false, |n| n > 0) {
                        let n = dp.counters.replies_sent.fetch_add(1, Ordering::SeqCst) + 1;
                        log::debug!(
                            "[dp] sent reply #{n} ({} bytes, fc=0118, {} snippet {}) to the host as \
                             AID {}",
                            pkt.len(),
                            what,
                            st.username_idx,
                            st.aid
                        );
                    }
                }
                sent_any = true;
                st.last_reply = Some(now);
            }
            // Nothing new to answer: re-send the last class on the resend
            // schedule, so a reply the host's window missed is not lost.
            if !sent_any
                && st.last_reply.map_or(true, |t| now.duration_since(t) >= RESEND)
            {
                if let Some(command) = st.command {
                    st.attempts += 1;
                    let seq = st.next_seq();
                    let (pkt, what) = answer_reply(&us, &host, seq, command, st);
                    let ts = st.rx_ts.wrapping_add(REPLY_AHEAD_US);
                    let queued = if st.mute_replies {
                        None
                    } else {
                        bus.send_reply(&pkt, ts, st.aid)
                    };
                    if queued.map_or(false, |n| n > 0) {
                        let n = dp.counters.replies_sent.fetch_add(1, Ordering::SeqCst) + 1;
                        log::debug!(
                            "[dp] re-sent reply #{n} ({} bytes, fc=0118, {} snippet {}) to the host \
                             as AID {}",
                            pkt.len(),
                            what,
                            st.username_idx,
                            st.aid
                        );
                    }
                    st.last_reply = Some(now);
                }
            }

        }
        _ => {}
    }
}

/// Boot the collected image once the host closes the download.
///
/// The trigger is the transfer's terminal state rather than the last packet:
/// booting on the packet left that round's GotAllReply unsent and the child
/// inside a transfer that never closed (v0.1.9dd), while `Closed` is only
/// reachable after the send stage has run — the host leaves the data class
/// only because it collected this client's completion answer. `Closed` also
/// implies the client is still associated (only a deauthentication leaves
/// `Associated`, and it resets the transfer), so this reads the same condition
/// the send stage used to guard.
fn maybe_boot<B: JoinerBus + ?Sized>(bus: &B, dp: &DownloadPlayJoiner, st: &mut State, us: &[u8; 6]) {
    if !st.booted && st.transfer == Transfer::Closed {
        log::info!(
            "[dp] the host has closed the download (it collected this client's \
             completion answer and stopped asking for data); booting the {} byte image \
             it sent",
            st.image.bytes
        );
        handoff(bus, dp, st, us);
    }
}

/// Read a `MD_*` reply-type override that is a hex byte (with or without `0x`).
/// Unset or empty is `None`; a malformed value is logged and treated as unset
/// rather than failing the run.
fn env_hex_byte(name: &str) -> Option<u8> {
    match std::env::var(name) {
        Ok(v) if !v.is_empty() => match u8::from_str_radix(v.trim_start_matches("0x"), 16) {
            Ok(t) => Some(t),
            Err(_) => {
                log::warn!("[dp] {name}={v:?} is not a hex byte; ignoring");
                None
            }
        },
        _ => None,
    }
}

/// The run loop: it owns *when* a turn happens (`TICK`), and nothing else. The
/// turn itself is `JoinerBus::turn`, which for a real console runs on that
/// console's driver thread - so the schedule has one owner and the work has
/// one home, rather than a drain from here and a send from there.
pub fn run(bus: Arc<dyn JoinerBus>, stop: Arc<AtomicBool>, dp: Arc<DownloadPlayJoiner>) {
    let mut st = State::default();
    // The console's own name, not the firmware default: four stand-ins answering
    // one host should read as four players in its room list (2026-10-17).
    st.client_name = player_name(bus.own_slot());
    log::info!(
        "[dp] this console advertises itself as \"{}\" (slot {:?})",
        st.client_name,
        bus.own_slot()
    );
    st.mute_replies = std::env::var("MD_DP_REPLY_MUTE").map_or(false, |v| v == "1");
    if st.mute_replies {
        log::info!("[dp] MD_DP_REPLY_MUTE=1: this client will associate and then answer nothing");
    }
    st.force_reply_type = env_hex_byte("MD_DP_REPLY_FORCE");
    if let Some(t) = st.force_reply_type {
        log::info!(
            "[dp] MD_DP_REPLY_FORCE={t:02x}: post-RSA rounds are answered with a normal reply \
             of type {t:02x}, not the two-byte dummy"
        );
    }
    st.force_data_reply_type = env_hex_byte("MD_DP_REPLY_FORCE_DATA");
    if let Some(t) = st.force_data_reply_type {
        log::info!("[dp] MD_DP_REPLY_FORCE_DATA={t:02x}: 04 DataPacket rounds answer with type {t:02x}");
    }
    st.player_body = std::env::var("MD_DP_PLAYER_BODY").map_or(false, |v| v == "1");
    if st.player_body {
        log::info!("[dp] MD_DP_PLAYER_BODY=1: one post-boot 00 poll gets a UsernameReply");
    }
    let st = Arc::new(Mutex::new(st));
    while !stop.load(Ordering::SeqCst) {
        // The loop now outlives the boot on purpose: after the hand-off it is
        // what re-asserts the console's bus membership across the child's own
        // wireless power cycles (see `step`). A booted step is a couple of
        // reads and no bus work, so the tick costs nothing measurable, and a
        // console that is not attached at all is the failure this loop is here
        // to prevent.
        bus.turn(&dp, &st, Instant::now());
        // A console that stopped (power-off, session torn down on its side)
        // retires the loop instead of queueing turns into a dead driver -
        // that is the `did not answer a joiner turn` spam, one warning per
        // unanswered turn with no other effect.
        if st.lock().unwrap().is_retired() {
            log::info!("[dp] joiner loop retired; the console stopped");
            break;
        }
        std::thread::sleep(TICK);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Mutex;

    /// A beacon as the host's game actually writes one: 12-byte TX header, a
    /// 24-byte 802.11 header, the beacon's three fixed fields and the SSID and
    /// rates IEs, then a 4-byte checksum slot. The byte values are the ones the
    /// Star Fox host really beaconed (see logs/sfc_dp_wire.log), so the parser
    /// is tested against captured bytes rather than invented ones.
    fn beacon_packet(host: [u8; 6], channel: u8, ssid: &[u8], rates: &[u8]) -> Vec<u8> {
        let mut f = Vec::new();
        f.extend_from_slice(&0x0080u16.to_le_bytes());
        f.extend_from_slice(&0u16.to_le_bytes());
        f.extend_from_slice(&[0xFF; 6]); // broadcast receiver
        f.extend_from_slice(&host);
        f.extend_from_slice(&host);
        f.extend_from_slice(&0x0010u16.to_le_bytes());
        f.extend_from_slice(&0x0000_0009_00E5_2A00u64.to_le_bytes()); // US counter
        f.extend_from_slice(&0x00D7u16.to_le_bytes()); // beacon interval
        f.extend_from_slice(&0x0021u16.to_le_bytes()); // capability
        // Four raw rate bytes, then the information elements - Nintendo's own
        // layout, not a standard beacon's rates element.
        let rates: &[u8] = if rates.is_empty() { &[0x01, 0x02, 0x82, 0x84] } else { rates };
        f.extend_from_slice(rates);
        push_ie(&mut f, 0x00, ssid);
        f.extend_from_slice(&[0xDE, 0xAD, 0xBE, 0xEF]); // FCS slot
        wrap(channel, &f)
    }

    /// A management response with the given frame control, addressed to `us`.
    fn response_packet(fc: u16, us: [u8; 6], host: [u8; 6]) -> Vec<u8> {
        let mut f = mgmt_header(fc, &us, &host, &host, 0x00D0);
        f.extend_from_slice(&[0; 6]);
        wrap(7, &f)
    }

    /// An association response (fc=0010) with the given status and AID, in the
    /// standard body the host's own games write: capability, status, AID, rates.
    fn assoc_response_packet(us: [u8; 6], host: [u8; 6], status: u16, aid: u16) -> Vec<u8> {
        let mut f = mgmt_header(0x0010, &us, &host, &host, 0x00D0);
        f.extend_from_slice(&0x0021u16.to_le_bytes()); // capability
        f.extend_from_slice(&status.to_le_bytes());
        f.extend_from_slice(&aid.to_le_bytes());
        f.extend_from_slice(&[0x01, 0x02, 0x82, 0x84]); // rates
        f.extend_from_slice(&[0xDE, 0xAD, 0xBE, 0xEF]); // FCS slot
        wrap(7, &f)
    }

    /// One of the host's own command frames (fc=0228), addressed to the
    /// multicast command MAC, with the GBATEK CMD DATA header and the byte
    /// values the live Star Fox host sent (`06 01 02 00 03 11 <cmd>`).
    fn command_packet(host: [u8; 6], command: u8) -> Vec<u8> {
        const CMD_MAC: [u8; 6] = [0x03, 0x09, 0xBF, 0x00, 0x00, 0x00];
        let mut f = mgmt_header(0x0228, &CMD_MAC, &host, &CMD_MAC, 0x0050);
        f.extend_from_slice(&0x0106u16.to_le_bytes()); // reply time
        f.extend_from_slice(&0x0002u16.to_le_bytes()); // slave flags: slave 1 connected
        f.push(0x03); // size in halfwords
        f.push(0x11); // flags
        f.push(command);
        f.extend_from_slice(&[0u8; 5]);
        f.extend_from_slice(&[0xDE, 0xAD, 0xBE, 0xEF]); // FCS slot
        wrap(7, &f)
    }

    /// A `03` RSA command carrying the child image's load map at the offsets
    /// GBATEK names and `rsa_boot_info` reads (payload at body offset 7).
    fn rsa_command_packet(host: [u8; 6], info: &BootInfo) -> Vec<u8> {
        const CMD_MAC: [u8; 6] = [0x03, 0x09, 0xBF, 0x00, 0x00, 0x00];
        let mut payload = vec![0u8; 0x38];
        let mut put = |off: usize, v: u32| {
            payload[off..off + 4].copy_from_slice(&v.to_le_bytes());
        };
        put(0x00, info.arm9_entry);
        put(0x04, info.arm7_entry);
        // The temp destinations carry sentinels so a parser that reads them
        // instead of the actual ones (the live bug this fixes) cannot pass.
        put(0x0C, 0xDEAD_0C00);
        put(0x10, info.header_dest);
        put(0x14, info.header_size);
        put(0x1C, 0xDEAD_1C00);
        put(0x20, info.arm9_dest);
        put(0x24, info.arm9_size);
        put(0x2C, info.arm7_temp);
        put(0x30, info.arm7_dest);
        put(0x34, info.arm7_size);
        let mut f = mgmt_header(0x0228, &CMD_MAC, &host, &CMD_MAC, 0x0050);
        f.extend_from_slice(&0x0106u16.to_le_bytes()); // reply time
        f.extend_from_slice(&0x0002u16.to_le_bytes()); // slave flags
        f.push(((1 + 2 + payload.len()) / 2) as u8); // size in halfwords
        f.push(0x11); // flags
        f.push(0x03); // RSA
        f.extend_from_slice(&payload);
        f.extend_from_slice(&[0xDE, 0xAD, 0xBE, 0xEF]); // FCS slot
        wrap(7, &f)
    }

    /// The hardware-generated CMD ACK (fc=0218) with the given error flags.
    fn ack_packet(host: [u8; 6], error_flags: u16) -> Vec<u8> {
        const ACK_MAC: [u8; 6] = [0x03, 0x09, 0xBF, 0x00, 0x00, 0x03];
        let mut f = mgmt_header(0x0218, &ACK_MAC, &host, &ACK_MAC, 0x0210);
        f.extend_from_slice(&0x00B8u16.to_le_bytes()); // unknown / random
        f.extend_from_slice(&error_flags.to_le_bytes());
        f.extend_from_slice(&[0u8; 4]);
        f.extend_from_slice(&[0xDE, 0xAD, 0xBE, 0xEF]); // FCS slot
        wrap(7, &f)
    }

    /// A deauthentication addressed to `us`, with a reason code.
    fn deauth_packet(us: [u8; 6], host: [u8; 6], reason: u16) -> Vec<u8> {
        let mut f = mgmt_header(0x00C0, &us, &host, &host, 0x00D0);
        f.extend_from_slice(&reason.to_le_bytes());
        f.extend_from_slice(&[0xDE, 0xAD, 0xBE, 0xEF]); // FCS slot
        wrap(7, &f)
    }

    struct MockBus {
        inbox: Mutex<Vec<Vec<u8>>>,
        sent: Mutex<Vec<Vec<u8>>>,
        /// Answers, with the AID each was stamped with - the path the host's
        /// reply window reads.
        replies: Mutex<Vec<(Vec<u8>, u16)>>,
        mac: [u8; 6],
        wireless: AtomicBool,
        listening: AtomicBool,
        /// The image, load map and session the joiner handed over, if it did.
        booted: Mutex<Option<(Vec<u8>, BootInfo, Session)>>,
        /// Frames the stand-in handed back, in order - the requeue's proof.
        requeued: Mutex<Vec<Vec<u8>>>,
        /// This test bus's slot, when it pretends to have one. `None` (the
        /// default) is a bus that owns no console, like the real default:
        /// the echo guard stays off and every frame reads as the host's.
        slot: Option<u32>,
    }

    impl MockBus {
        fn new() -> Self {
            MockBus {
                inbox: Mutex::new(Vec::new()),
                sent: Mutex::new(Vec::new()),
                replies: Mutex::new(Vec::new()),
                mac: [0x00, 0x09, 0xBF, 0x12, 0x66, 0x43],
                wireless: AtomicBool::new(false),
                listening: AtomicBool::new(false),
                booted: Mutex::new(None),
                requeued: Mutex::new(Vec::new()),
                slot: None,
            }
        }
        fn push(&self, p: Vec<u8>) {
            self.inbox.lock().unwrap().push(p);
        }
        fn sent(&self) -> Vec<Vec<u8>> {
            self.sent.lock().unwrap().clone()
        }
        fn replies(&self) -> Vec<(Vec<u8>, u16)> {
            self.replies.lock().unwrap().clone()
        }
    }

    impl JoinerBus for MockBus {
        fn drain(&self) -> Option<Vec<(Vec<u8>, u64)>> {
            let mut q = self.inbox.lock().unwrap();
            let out: Vec<(Vec<u8>, u64)> = q.drain(..).map(|p| (p, 0)).collect();
            Some(out)
        }
        fn send(&self, data: &[u8], _ts: u64) -> Option<i32> {
            self.sent.lock().unwrap().push(data.to_vec());
            Some(data.len() as i32)
        }
        fn send_reply(&self, data: &[u8], _ts: u64, aid: u16) -> Option<i32> {
            self.replies.lock().unwrap().push((data.to_vec(), aid));
            Some(data.len() as i32)
        }
        fn own_mac(&self) -> Option<[u8; 6]> {
            Some(self.mac)
        }
        fn wireless_on(&self) -> bool {
            self.wireless.load(Ordering::SeqCst)
        }
        fn set_receiving(&self, on: bool) -> bool {
            self.listening.store(on, Ordering::SeqCst);
            true
        }
        fn receiving(&self) -> bool {
            self.listening.load(Ordering::SeqCst)
        }
        fn handoff(&self, image: &[u8], info: &BootInfo, session: &Session) -> bool {
            *self.booted.lock().unwrap() = Some((image.to_vec(), *info, *session));
            true
        }
        fn requeue(&self, pkt: &[u8], _ts: u64, _sender: u32, _ftype: u32) -> bool {
            self.requeued.lock().unwrap().push(pkt.to_vec());
            true
        }
        fn own_slot(&self) -> Option<u32> {
            self.slot
        }
    }

    const HOST: [u8; 6] = [0x00, 0x09, 0xBF, 0x11, 0x22, 0x33];
    const US: [u8; 6] = [0x00, 0x09, 0xBF, 0x12, 0x66, 0x43];

    #[test]
    fn beacon_parses_into_the_identity_the_client_answers() {
        let pkt = beacon_packet(HOST, 7, &[0x53, 0x46, 0x43], &[0x01, 0x02, 0x82, 0x84]);
        let b = parse_beacon(&pkt).expect("a beacon");
        assert_eq!(b.mac, HOST);
        assert_eq!(b.bssid, HOST);
        assert_eq!(b.channel, 7);
        assert_eq!(b.capability, 0x0021);
        assert_eq!(b.interval, 0x00D7, "the builder's beacon interval");
        assert_eq!(b.ssid, vec![0x53, 0x46, 0x43]);
        assert_eq!(b.rates, vec![0x01, 0x02, 0x82, 0x84]);
        assert_eq!(b.clock, 0x0000_0009_00E5_2A00);
        assert_eq!(b.token, [0x00, 0x00], "no DD tag means no client token");
        assert_eq!(b.dd_len, 0, "no DD tag means no tag length");
    }

    #[test]
    fn auth_request_is_a_well_formed_open_system_frame() {
        let b = parse_beacon(&beacon_packet(HOST, 7, &[1, 2], &[])).unwrap();
        let pkt = build_auth_request(&US, &b, 0x0000);
        // The header the receive path validates: channel, and the frame length
        // at offset 10 that CheckRX compares against `rxlen - 12`.
        assert_eq!(pkt[9], 7);
        assert_eq!(
            u16::from_le_bytes([pkt[10], pkt[11]]) as usize,
            pkt.len() - 12
        );
        let f = &pkt[12..];
        assert_eq!(u16::from_le_bytes([f[0], f[1]]), 0x00B0);
        assert_eq!(&f[4..10], &HOST); // receiver
        assert_eq!(&f[10..16], &US); // transmitter
        assert_eq!(&f[16..22], &HOST); // bssid
        assert_eq!(&f[24..26], &0u16.to_le_bytes()); // algorithm 0
        assert_eq!(&f[26..28], &1u16.to_le_bytes()); // sequence 1
        assert_eq!(&f[28..30], &0u16.to_le_bytes()); // status
    }

    #[test]
    fn assoc_request_carries_the_shape_a_real_client_sends() {
        // The host's own SSID is not echoed: every real client (NSMB, MKDS, MPH)
        // sends its *own* 32-byte SSID element carrying the DS nickname. What
        // the client does echo from the beacon is the capability and the rates.
        let b = parse_beacon(&beacon_packet(HOST, 7, &[0x41, 0x42], &[0x01, 0x02, 0x82, 0x84])).unwrap();
        let pkt = build_assoc_request(&US, &b, 0x0010, CLIENT_NAME);
        let f = &pkt[12..];
        assert_eq!(u16::from_le_bytes([f[0], f[1]]), 0x0000);
        assert_eq!(u16::from_le_bytes([f[2], f[3]]), 0x0000);
        assert_eq!(&f[4..10], &HOST);
        assert_eq!(&f[10..16], &US); // transmitter
        assert_eq!(&f[16..22], &HOST); // bssid
        assert_eq!(&f[22..24], &0x0010u16.to_le_bytes()); // 802.11 seq, as the hardware numbers it
        let body = &f[24..];
        assert_eq!(&body[0..2], &0x0021u16.to_le_bytes()); // capability echoed
        assert_eq!(&body[2..4], &0x0001u16.to_le_bytes()); // listen interval
        // The 32-byte client SSID, exactly as `client_ssid()` builds it.
        assert_eq!(body[4], 0x00);
        assert_eq!(body[5], 32);
        assert_eq!(&body[6..38], &client_ssid(b.serial, b.token, CLIENT_NAME)[..]);
        // ... then the four raw supported-rate bytes the host advertised (no
        // rates information element - Nintendo's own layout).
        assert_eq!(&body[38..42], &[0x01, 0x02, 0x82, 0x84]);
        // A real client's association request is 70 bytes on the air
        // (`24 header + 42 body + 4 FCS`), the length one captured frame carries.
        assert_eq!(pkt.len(), 12 + 70);
    }

    #[test]
    fn the_client_echoes_the_game_serial_the_beacon_advertises() {
        // The host's beacon carries its game serial (0040xxxx) in the `DD` tag's
        // data at offset 12; a real client echoes it as its own SSID's first
        // four bytes. Byte values are Star Fox's captured beacon and its
        // expected echo (`logs/sfc_dp_ser.log`, MPH/NSMB in `logs/*_app.log.gz`).
        let mut f = Vec::new();
        f.extend_from_slice(&0x0080u16.to_le_bytes());
        f.extend_from_slice(&0u16.to_le_bytes());
        f.extend_from_slice(&[0xFF; 6]);
        f.extend_from_slice(&HOST);
        f.extend_from_slice(&HOST);
        f.extend_from_slice(&0x0010u16.to_le_bytes());
        f.extend_from_slice(&0u64.to_le_bytes()); // timestamp
        f.extend_from_slice(&0x00D1u16.to_le_bytes()); // beacon interval
        f.extend_from_slice(&0x0021u16.to_le_bytes()); // capability
        f.extend_from_slice(&[0x01, 0x02, 0x82, 0x84]); // raw rates
        // The DD tag: 24 bytes, the serial at data offset 12.
        push_ie(&mut f, 0xDD, &[
            0x00, 0x09, 0xBF, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x18, 0x02, 0x40,
            0x00, 0x18, 0x00, 0x00, 0x00, 0xFE, 0x01, 0x08, 0x00,
        ]);
        f.extend_from_slice(&[0xDE, 0xAD, 0xBE, 0xEF]);
        let b = parse_beacon(&wrap(7, &f)).unwrap();
        assert_eq!(b.serial, [0x18, 0x02, 0x40, 0x00]);
        assert_eq!(b.dd_len, 26, "id + length + the 24 DD data bytes");
        // ... and the two bytes the beacon puts right after it, which every
        // captured client echoes into the next word of its own SSID.
        assert_eq!(b.token, [0x18, 0x00]);
        let pkt = build_assoc_request(&US, &b, 0x0010, CLIENT_NAME);
        assert_eq!(
            &pkt[12 + 24 + 6..12 + 24 + 12],
            &[0x18, 0x02, 0x40, 0x00, 0x18, 0x00]
        );
    }

    /// A real client's association-request SSID, as captured from three
    /// different titles, is `serial(4) + <DD bytes 16..18>(2) + 00 00 + name
    /// (24)`. This pins the builder against those bytes: the reference frames
    /// are MPH's client (`logs/mph4b_app_4con.log.gz`,
    /// `body[0..41] 21 00 01 00 00 20 0A 00 40 00 12 23 00 00 ...`), whose beacon
    /// advertises `12 23` at DD offset 16, and NSMB's (`50 01 40 00 DA A3 00 00`
    /// against a beacon's `DA A3`). The first version of this joiner sent
    /// `serial + 00 00 00 00`, which is what the host refused.
    #[test]
    fn the_ssid_echoes_the_beacons_dd_token_as_a_real_client_does() {
        let ssid = client_ssid([0x0A, 0x00, 0x40, 0x00], [0x12, 0x23], CLIENT_NAME);
        assert_eq!(ssid.len(), 32);
        assert_eq!(&ssid[0..6], &[0x0A, 0x00, 0x40, 0x00, 0x12, 0x23]);
        assert_eq!(&ssid[6..8], &[0x00, 0x00]);
        let nsmb = client_ssid([0x50, 0x01, 0x40, 0x00], [0xDA, 0xA3], CLIENT_NAME);
        assert_eq!(&nsmb[0..6], &[0x50, 0x01, 0x40, 0x00, 0xDA, 0xA3]);
        // The old shape - token zeroed - is what the host read as a request for
        // a title it did not have.
        assert_ne!(ssid, client_ssid([0x0A, 0x00, 0x40, 0x00], [0x00, 0x00], CLIENT_NAME));
    }

    /// The exact response the live Star Fox host sent: `status 0001, aid 0000`
    /// (trace `sync=0201000000010021`). It is a refusal, not a registration, and
    /// the joiner must not claim otherwise - nor answer it with a dummy reply.
    #[test]
    fn a_refused_association_is_not_read_as_acceptance() {
        let bus = MockBus::new();
        let dp = DownloadPlayJoiner::new(true, 1);
        let mut st = State::default();
        let t0 = Instant::now();
        bus.push(beacon_packet(HOST, 7, &[1], &[]));
        step(&bus, &dp, &mut st, t0);
        bus.push(response_packet(0x00B0, US, HOST));
        step(&bus, &dp, &mut st, t0 + RESEND);

        assert_eq!(
            classify(&assoc_response_packet(US, HOST, 1, 0), &US),
            Some(Rx::AssocResponse { status: 1, aid: 0 })
        );
        bus.push(assoc_response_packet(US, HOST, 1, 0));
        step(&bus, &dp, &mut st, t0 + 2 * RESEND);
        assert_eq!(dp.counters.assoc_recv.load(Ordering::SeqCst), 1);
        assert_eq!(dp.counters.last_assoc_status.load(Ordering::SeqCst), 1);
        assert_eq!(dp.counters.last_assoc_aid.load(Ordering::SeqCst), 0);
        assert_ne!(st.phase, Phase::Associated, "status 0001 is not a join");
        assert_eq!(
            dp.counters.replies_sent.load(Ordering::SeqCst),
            0,
            "no download handshake may start on a refused association"
        );

        // A real grant - the byte values a working host sends
        // (`sync=0201C00100000021`) - is read as exactly that, AID and all.
        assert_eq!(
            classify(&assoc_response_packet(US, HOST, 0, 0xC001), &US),
            Some(Rx::AssocResponse { status: 0, aid: 0xC001 })
        );
    }

    #[test]
    fn a_disarmed_joiner_touches_the_bus_zero_times() {
        let bus = MockBus::new();
        bus.push(beacon_packet(HOST, 7, &[1], &[]));
        let dp = DownloadPlayJoiner::new(false, 1);
        let mut st = State::default();
        step(&bus, &dp, &mut st, Instant::now());
        assert!(bus.sent().is_empty(), "disarmed must not send");
        assert_eq!(dp.counters.beacons.load(Ordering::SeqCst), 0, "disarmed must not even drain");
        assert_eq!(st.phase, Phase::Seek);
    }

    #[test]
    fn a_console_whose_game_owns_the_radio_is_left_alone() {
        let bus = MockBus::new();
        bus.wireless.store(true, Ordering::SeqCst);
        bus.push(beacon_packet(HOST, 7, &[1], &[]));
        let dp = DownloadPlayJoiner::new(true, 1);
        let mut st = State::default();
        step(&bus, &dp, &mut st, Instant::now());
        assert!(bus.sent().is_empty(), "must not steal a live game's bus");
        assert_eq!(dp.counters.beacons.load(Ordering::SeqCst), 0);
    }

    #[test]
    fn booted_standin_leaves_the_join_scan_frames_for_the_game() {
        // Post-boot the drain and the game's own CheckRX read the same queue:
        // a beacon the stand-in consumes is one the game's scan never sees, so
        // a game that scans before it powers up waits forever while the host
        // starves (Bomberman: guest `answered 0`). The join/scan set goes back
        // to the inbox; polls stay consumed and answered.
        let bus = MockBus::new();
        let dp = DownloadPlayJoiner::new(true, 1);
        let mut st = State::default();
        st.booted = true;
        let beacon = beacon_packet(HOST, 7, &[0x41], &[]);
        let assoc = assoc_response_packet(US, HOST, 0, 0xC001);
        bus.push(beacon.clone());
        bus.push(assoc.clone());
        bus.push(command_packet(HOST, 0x00));
        step(&bus, &dp, &mut st, Instant::now());
        let back = bus.requeued.lock().unwrap().clone();
        assert_eq!(back, vec![beacon, assoc], "join/scan frames go back, in order");
        assert_eq!(dp.counters.frames_requeued.load(Ordering::SeqCst), 2);
        assert_eq!(dp.counters.beacons.load(Ordering::SeqCst), 0, "restored, not tracked");
        assert_eq!(dp.counters.assoc_recv.load(Ordering::SeqCst), 0);
        assert!(st.host.is_none(), "the game earns its own association now");
        assert_eq!(st.phase, Phase::Seek);
        assert_eq!(dp.counters.commands_recv.load(Ordering::SeqCst), 1, "polls still answered");

        // Pre-boot the same frames are the handshake itself: consumed, tracked.
        let bus2 = MockBus::new();
        let dp2 = DownloadPlayJoiner::new(true, 1);
        let mut st2 = State::default();
        bus2.push(beacon_packet(HOST, 7, &[0x41], &[]));
        step(&bus2, &dp2, &mut st2, Instant::now());
        assert!(bus2.requeued.lock().unwrap().is_empty());
        assert_eq!(dp2.counters.beacons.load(Ordering::SeqCst), 1);
        assert!(st2.host.is_some());
    }

    #[test]
    fn serve_hands_the_stored_beacon_back_only_into_host_silence() {
        // A scan-first guest (Bomberman, Mario Party DS) brings its radio up
        // after the boot and scans for the room, but on those titles the
        // host has stopped transmitting by then - the requeue has nothing to
        // hand back and the scan fails. The serve replays the last real
        // beacon, but only after a full second with nothing from the host.
        let bus = MockBus::new();
        let dp = DownloadPlayJoiner::new(true, 1);
        let mut st = State::default();
        st.booted = true;
        let t0 = Instant::now();
        let beacon = beacon_packet(HOST, 7, &[0x41], &[]);
        bus.push(beacon.clone());
        step(&bus, &dp, &mut st, t0);
        // The real beacon went back for the game (requeue arm), nothing served.
        assert_eq!(bus.requeued.lock().unwrap().len(), 1);
        assert_eq!(dp.counters.beacons_served.load(Ordering::SeqCst), 0);
        // 1.5 s of silence: the stored beacon is served once...
        step(&bus, &dp, &mut st, t0 + Duration::from_millis(1500));
        assert_eq!(bus.requeued.lock().unwrap().len(), 2);
        assert_eq!(dp.counters.beacons_served.load(Ordering::SeqCst), 1);
        assert_eq!(
            bus.requeued.lock().unwrap()[1], beacon,
            "the serve replays the host's own bytes"
        );
        // ...rate-limited inside SERVE_EVERY...
        step(&bus, &dp, &mut st, t0 + Duration::from_millis(1550));
        assert_eq!(dp.counters.beacons_served.load(Ordering::SeqCst), 1);
        // ...and again on the schedule while the silence holds...
        step(&bus, &dp, &mut st, t0 + Duration::from_millis(1700));
        assert_eq!(dp.counters.beacons_served.load(Ordering::SeqCst), 2);
        // ...then the host speaks: one last serve races the command's own
        // arrival (the serve runs ahead of the drain, while the host has
        // still been silent 1.8 s), and from the command on the serve stops
        // by itself.
        bus.push(command_packet(HOST, 0x00));
        step(&bus, &dp, &mut st, t0 + Duration::from_millis(1800));
        assert_eq!(dp.counters.beacons_served.load(Ordering::SeqCst), 3);
        step(&bus, &dp, &mut st, t0 + Duration::from_millis(2300));
        assert_eq!(
            dp.counters.beacons_served.load(Ordering::SeqCst), 3,
            "a talking host is never served after its frames arrive"
        );
    }

    #[test]
    fn serve_needs_boot_and_arm_and_silence() {
        // Unbooted: the beacon is the live handshake, never a serve.
        let bus = MockBus::new();
        let dp = DownloadPlayJoiner::new(true, 1);
        let mut st = State::default();
        let t0 = Instant::now();
        bus.push(beacon_packet(HOST, 7, &[0x41], &[]));
        step(&bus, &dp, &mut st, t0);
        step(&bus, &dp, &mut st, t0 + Duration::from_secs(5));
        assert_eq!(dp.counters.beacons_served.load(Ordering::SeqCst), 0);
        // Disarmed: the switch gates the serve like everything else.
        let bus2 = MockBus::new();
        let dp2 = DownloadPlayJoiner::new(false, 1);
        let mut st2 = State::default();
        st2.booted = true;
        step(&bus2, &dp2, &mut st2, t0 + Duration::from_secs(5));
        assert_eq!(dp2.counters.beacons_served.load(Ordering::SeqCst), 0);
        assert!(bus2.requeued.lock().unwrap().is_empty());
    }

    #[test]
    fn own_serves_bounce_for_the_scan_without_side_effects() {
        // A served beacon comes back through this same drain (the serve hands
        // it to our own inbox, which is what the game reads). It must stay
        // available for the game's scan without refreshing the silence clock
        // - which would feed the serve's own trigger - or touching any
        // counter or the host identity.
        let mut bus = MockBus::new();
        bus.slot = Some(0); // the mock zeroes senders, so slot 0 is self
        let dp = DownloadPlayJoiner::new(true, 1);
        let mut st = State::default();
        st.booted = true;
        let t0 = Instant::now();
        bus.push(beacon_packet(HOST, 7, &[0x41], &[]));
        step(&bus, &dp, &mut st, t0);
        assert_eq!(bus.requeued.lock().unwrap().len(), 1, "echo bounces");
        assert_eq!(dp.counters.beacons.load(Ordering::SeqCst), 0, "not tracked");
        assert_eq!(dp.counters.beacons_served.load(Ordering::SeqCst), 0);
        assert!(st.host.is_none(), "the game earns its own association");
        assert!(st.last_host_rx.is_none(), "an echo is not a talking host");
        assert!(st.serve_beacon.is_none(), "an echo is not a fresh beacon");
    }

    #[test]
    fn retire_is_once_and_sticky() {
        let mut st = State::default();
        assert!(!st.is_retired());
        assert!(st.retire(), "first call retires");
        assert!(st.is_retired());
        assert!(!st.retire(), "second call is a no-op for the log-once");
    }

    #[test]
    fn beacon_then_auth_then_assoc_then_the_hosts_answer() {
        let bus = MockBus::new();
        let dp = DownloadPlayJoiner::new(true, 1);
        let mut st = State::default();
        let t0 = Instant::now();

        // 1. A beacon -> the console attaches at the bus level (the client's
        //    radio) and an authentication request goes out.
        bus.push(beacon_packet(HOST, 7, &[0x41], &[]));
        step(&bus, &dp, &mut st, t0);
        assert!(bus.receiving(), "the client must listen");
        let sent = bus.sent();
        assert_eq!(sent.len(), 1);
        assert_eq!(u16::from_le_bytes([sent[0][12], sent[0][13]]), 0x00B0);
        assert_eq!(dp.counters.auth_sent.load(Ordering::SeqCst), 1);
        assert_eq!(dp.counters.assoc_sent.load(Ordering::SeqCst), 0);

        // The repeat is rate-limited: a second step inside RESEND sends nothing.
        step(&bus, &dp, &mut st, t0 + Duration::from_millis(10));
        assert_eq!(bus.sent().len(), 1, "must not flood the host with auth frames");

        // 2. The host's authentication response -> an association request.
        bus.push(response_packet(0x00B0, US, HOST));
        step(&bus, &dp, &mut st, t0 + RESEND);
        let sent = bus.sent();
        assert_eq!(sent.len(), 2);
        assert_eq!(u16::from_le_bytes([sent[1][12], sent[1][13]]), 0x0000);
        assert_eq!(dp.counters.auth_recv.load(Ordering::SeqCst), 1);
        assert_eq!(dp.counters.assoc_sent.load(Ordering::SeqCst), 1);

        // 3. The host's association response - status 0 with an assigned AID,
        //    what a working host sends (`[HOST] syncing client C001`) -> the
        //    client is associated. Nothing is answered yet: the handshake's
        //    first command has not arrived.
        bus.push(assoc_response_packet(US, HOST, 0, 0xC001));
        step(&bus, &dp, &mut st, t0 + 2 * RESEND);
        assert_eq!(dp.counters.assoc_recv.load(Ordering::SeqCst), 1);
        assert_eq!(st.phase, Phase::Associated);
        assert!(bus.replies().is_empty(), "nothing to answer until a command arrives");

        // 4. The host's NameRequest -> answered as a *reply*, on the reply path
        //    the host's window reads, stamped with the AID it assigned.
        bus.push(command_packet(HOST, 0x01));
        step(&bus, &dp, &mut st, t0 + 3 * RESEND);
        assert_eq!(dp.counters.commands_recv.load(Ordering::SeqCst), 1);
        assert_eq!(dp.counters.last_command.load(Ordering::SeqCst), 0x01);
        let replies = bus.replies();
        assert_eq!(replies.len(), 1);
        assert_eq!(replies[0].1, 1, "stamped with the AID the host assigned");
        let reply = &replies[0].0;
        assert_eq!(u16::from_le_bytes([reply[12], reply[13]]), 0x0118);
        assert_eq!(u16::from_le_bytes([reply[10], reply[11]]) as usize, reply.len() - 12);
        assert_eq!(reply.len(), 12 + 38); // 24 header + 10 body + 4 FCS

        // ... and it is repeated on the same schedule, so a narrower reply
        // window can still be met. Nothing was collected, so the same snippet
        // goes again rather than being skipped.
        step(&bus, &dp, &mut st, t0 + 4 * RESEND);
        assert_eq!(bus.replies().len(), 2);
        assert_eq!(bus.replies()[1].0[12 + 27], 0x00);
    }

    /// The reply the client must send for a NameRequest, byte for byte from
    /// GBATEK's Download Play chapter: the six-byte UsernameReply snippets and
    /// the REPLY frame layout around them.
    #[test]
    fn the_username_reply_carries_the_game_id_and_the_clients_name() {
        let serial = [0x18, 0x02, 0x40, 0x00]; // Star Fox's, out of its beacon
        // Snippet 0: game ID, favourite colour, name length. The name is the
        // player's own, not the firmware default - "Player 1" is eight
        // characters, and that length is what the host's roster reads.
        assert_eq!(username_snippet(serial, 0, CLIENT_NAME), [0x18, 0x02, 0x40, 0x00, 0x00, 0x08]);
        // Snippets 1..3: three UTF-16LE characters each - "Pla", "yer", " 1".
        assert_eq!(username_snippet(serial, 1, CLIENT_NAME), [0x50, 0x00, 0x6C, 0x00, 0x61, 0x00]);
        assert_eq!(username_snippet(serial, 2, CLIENT_NAME), [0x79, 0x00, 0x65, 0x00, 0x72, 0x00]);
        assert_eq!(username_snippet(serial, 3, CLIENT_NAME), [0x20, 0x00, 0x31, 0x00, 0x00, 0x00]);

        let b = parse_beacon(&beacon_packet(HOST, 7, &[1], &[])).unwrap();
        let pkt = build_username_reply(&US, &b, 0x0020, 0, CLIENT_NAME);
        assert_eq!(u16::from_le_bytes([pkt[12], pkt[13]]), 0x0118);
        assert_eq!(&pkt[12 + 4..12 + 10], &HOST); // the host answers
        assert_eq!(&pkt[12 + 10..12 + 16], &US); // from this client
        assert_eq!(&pkt[12 + 16..12 + 22], &REPLY_MAC); // 03:09:BF:00:00:10
        // size 4 (normal), flags 1, type 7 (Username), snippet 0, then the
        // snippet's six bytes (the serial is zero here: this beacon has no DD).
        assert_eq!(
            &pkt[12 + 24..12 + 34],
            &[0x04, 0x01, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08]
        );
        assert_eq!(pkt.len(), 12 + 38); // 24 header + 10 body + 4 FCS
        assert_eq!(u16::from_le_bytes([pkt[10], pkt[11]]) as usize, pkt.len() - 12);
    }

    #[test]
    fn a_truncated_command_frame_is_not_read_as_a_command() {
        // The host writes a 6-byte body when it has no client to address; the
        // byte at the command's offset is then the wire's checksum, which the
        // first live run of this path logged as command bytes (`37`, `FD`, `BA`)
        // that no protocol defines, and answered with dummies.
        const CMD_MAC: [u8; 6] = [0x03, 0x09, 0xBF, 0x00, 0x00, 0x00];
        let mut f = mgmt_header(0x0228, &CMD_MAC, &HOST, &CMD_MAC, 0x0050);
        f.extend_from_slice(&[0x06, 0x01, 0x00, 0x00, 0x00, 0x00]); // reply time, no client
        match classify(&wrap(7, &f), &US) {
            Some(Rx::Command { .. }) => panic!("a 6-byte body carries no command byte"),
            Some(Rx::Other { fc, .. }) => assert_eq!(fc, 0x0228),
            other => panic!("unexpected {other:?}"),
        }
    }

    #[test]
    fn the_hosts_command_and_ack_are_read_as_the_protocol_says() {
        assert_eq!(
            classify(&command_packet(HOST, 0x01), &US),
            Some(Rx::Command {
                command: 0x01,
                size: 3,
                flags: 0x11,
                slave_flags: 0x0002,
                packet: None,
                data: Vec::new(),
                rsa_total: None,
                rsa_boot: None,
            })
        );
        assert_eq!(
            classify(&command_packet(HOST, 0x03), &US),
            Some(Rx::Command {
                command: 0x03,
                size: 3,
                flags: 0x11,
                slave_flags: 0x0002,
                packet: None,
                data: Vec::new(),
                rsa_total: None,
                rsa_boot: None,
            })
        );
        // The ack's error flags: bit 1 set = slave 1 did not answer.
        assert_eq!(
            classify(&ack_packet(HOST, 0x0002), &US),
            Some(Rx::CmdAck { error_flags: 0x0002 })
        );
        assert_eq!(
            classify(&ack_packet(HOST, 0x0000), &US),
            Some(Rx::CmdAck { error_flags: 0x0000 })
        );
    }

    /// The host streams several commands per drain and the class can change
    /// inside one. Every round is answered from its own class, and the answer
    /// the host's window actually reads is the newest round's - the reply path
    /// keeps only the newest answer per client (`MD_REPLY_KEEP=1`). Answering
    /// only the newest round starved this host: a drain covers a whole emulated
    /// frame and a real Download Play host asks several rounds inside one
    /// (measured on Super Mario 64 DS, ~8.6), so it collected 31 of 2,535. The
    /// old failure mode for the *other* extreme still holds - answering the
    /// oldest class for the whole batch replies `Dummy` through a whole RSA
    /// round, and the host abandoned RSA after ~19 windows.
    #[test]
    fn every_round_in_a_batch_is_answered_from_its_own_class() {
        let bus = MockBus::new();
        let dp = DownloadPlayJoiner::new(true, 1);
        let mut st = State::default();
        let t0 = Instant::now();
        bus.push(beacon_packet(HOST, 7, &[1], &[]));
        step(&bus, &dp, &mut st, t0);
        bus.push(response_packet(0x00B0, US, HOST));
        step(&bus, &dp, &mut st, t0 + RESEND);

        // A batch carrying the name phase's end: the class the host is asking
        // now is `00` (NameDone), and before RSA that round still wants the
        // username - measured, twice: a host that got a dummy there never sent
        // its RSA command at all.
        bus.push(assoc_response_packet(US, HOST, 0, 0xC001));
        bus.push(command_packet(HOST, 0x01));
        bus.push(command_packet(HOST, 0x00));
        step(&bus, &dp, &mut st, t0 + 2 * RESEND);
        assert_eq!(bus.replies().len(), 2, "every round of the batch is answered");
        assert_eq!(bus.replies()[0].0[12 + 26], 0x07, "NameRequest wants the name");
        assert_eq!(bus.replies()[1].0[12 + 26], 0x07, "NameDone still wants the name");

        // After RSA, `00` is RsaDone. It used to take the two-byte dummy, but a
        // live host parks at mode 14 on that - its mode machine wants one more
        // RsaReply before it will start the download (v0.1.9cv). So RsaDone now
        // takes the RsaReply too.
        bus.push(command_packet(HOST, 0x03));
        bus.push(command_packet(HOST, 0x00));
        step(&bus, &dp, &mut st, t0 + 2 * RESEND + Duration::from_millis(10));
        assert_eq!(bus.replies().len(), 4, "the RSA and the RsaDone round, both answered");
        assert_eq!(bus.replies()[2].0[12 + 26], 0x08, "the RsaReply");
        assert_eq!(
            bus.replies()[3].0[12 + 26],
            0x08,
            "RsaDone takes a second RsaReply: the host's mode 14 -> 6 step"
        );
    }

    /// The one entry point the run loop uses. `turn` owns the schedule: the
    /// default runs the turn on the bus it was called through (the in-process
    /// test bus), and a real console overrides it so the same turn runs *inside*
    /// that console's own frame. Either way the state it advances is the state
    /// the caller owns, so there is exactly one writer and one schedule.
    #[test]
    fn the_default_turn_advances_the_callers_owned_state() {
        let bus = MockBus::new();
        let dp = Arc::new(DownloadPlayJoiner::new(true, 1));
        let st = Arc::new(Mutex::new(State::default()));
        let t0 = Instant::now();

        bus.push(beacon_packet(HOST, 7, &[1], &[]));
        bus.turn(&dp, &st, t0);
        assert_eq!(dp.counters.beacons.load(Ordering::SeqCst), 1);
        assert_eq!(bus.sent().len(), 1, "the beacon earns the auth request in the same turn");
        assert!(matches!(st.lock().unwrap().phase, Phase::Beaconed));
    }

    /// The name phase is five snippets, and the host will not leave NameRequest
    /// until it has all of them - including the last. A live run that stopped
    /// answering NameRequest once the index reached 4 (and sent dummies instead)
    /// left the host asking NameRequest forever.
    #[test]
    fn every_name_request_is_answered_with_a_username_even_for_the_last_snippet() {
        let bus = MockBus::new();
        let dp = DownloadPlayJoiner::new(true, 1);
        let mut st = State::default();
        let t0 = Instant::now();
        bus.push(beacon_packet(HOST, 7, &[1], &[]));
        step(&bus, &dp, &mut st, t0);
        bus.push(response_packet(0x00B0, US, HOST));
        step(&bus, &dp, &mut st, t0 + RESEND);
        bus.push(assoc_response_packet(US, HOST, 0, 0xC001));
        step(&bus, &dp, &mut st, t0 + 2 * RESEND);

        // The index is capped at the fifth snippet; a NameRequest arriving now
        // must still be answered with that snippet, not a dummy.
        st.username_idx = 4;
        bus.push(command_packet(HOST, 0x01));
        step(&bus, &dp, &mut st, t0 + 3 * RESEND);
        let replies = bus.replies();
        assert_eq!(replies.last().unwrap().0[12 + 26], 0x07, "still a username reply");
        assert_eq!(replies.last().unwrap().0[12 + 27], 4, "the fifth snippet");
    }

    /// GBATEK's RSA step: the host sends its `03` command and waits for a reply
    /// whose type byte is `08`. A dummy's type is `00`, so a client that keeps
    /// answering with dummies leaves the host re-sending RSA forever - which is
    /// exactly what a live Star Fox host did until this reply existed.
    #[test]
    fn the_rsa_command_is_answered_with_an_rsa_reply() {
        let bus = MockBus::new();
        let dp = DownloadPlayJoiner::new(true, 1);
        let mut st = State::default();
        let t0 = Instant::now();
        bus.push(beacon_packet(HOST, 7, &[1], &[]));
        step(&bus, &dp, &mut st, t0);
        bus.push(response_packet(0x00B0, US, HOST));
        step(&bus, &dp, &mut st, t0 + RESEND);
        bus.push(assoc_response_packet(US, HOST, 0, 0xC001));
        bus.push(command_packet(HOST, 0x03));
        step(&bus, &dp, &mut st, t0 + 2 * RESEND);

        let replies = bus.replies();
        assert_eq!(replies.len(), 1);
        assert_eq!(replies[0].1, 1, "stamped with the AID the host assigned");
        let body = &replies[0].0[12 + 24..];
        assert_eq!(body[0], 0x04, "normal reply");
        assert_eq!(body[1], 0x01, "flags");
        assert_eq!(body[2], 0x08, "RsaReply, the type that ends the RSA phase");
        assert_eq!(replies[0].0.len(), 12 + 38); // 24 header + 10 body + 4 FCS
    }

    /// The step this pass measured on a live host. After the RSA round the host
    /// keeps asking a `00` dummy round, but its per-client mode machine (now at
    /// mode 14) is waiting for **one more** RsaReply before it will begin the
    /// download: the type is the class its dispatcher switches on, and the 08
    /// arm is the one that moves 14 -> 6. Answering those rounds with the
    /// two-byte dummy parks the host at 14 forever; answering them with the
    /// RsaReply moved the live host to 6 and it streamed a `04` DataPacket.
    #[test]
    fn a_post_rsa_dummy_round_is_answered_with_an_rsa_reply_not_a_dummy() {
        let bus = MockBus::new();
        let dp = DownloadPlayJoiner::new(true, 1);
        let mut st = State::default();
        let t0 = Instant::now();
        bus.push(beacon_packet(HOST, 7, &[1], &[]));
        step(&bus, &dp, &mut st, t0);
        bus.push(response_packet(0x00B0, US, HOST));
        step(&bus, &dp, &mut st, t0 + RESEND);
        bus.push(assoc_response_packet(US, HOST, 0, 0xC001));
        bus.push(command_packet(HOST, 0x03));
        step(&bus, &dp, &mut st, t0 + 2 * RESEND);

        // The host moves on to a `00` dummy round - the round that used to get
        // the two-byte dummy and leave the host parked.
        bus.push(command_packet(HOST, 0x00));
        step(&bus, &dp, &mut st, t0 + 3 * RESEND);
        let r = bus.replies().last().unwrap().0.clone();
        assert_eq!(r[12 + 24], 0x04, "a normal reply, not the 2-byte dummy");
        assert_eq!(r[12 + 26], 0x08, "RsaReply: the type that reaches mode 6");

        // The two diagnostic overrides still win over the default.
        let host = parse_beacon(&beacon_packet(HOST, 7, &[1], &[])).unwrap();
        let mut forced = State::default();
        forced.force_reply_type = Some(0x09);
        forced.saw_rsa = true;
        assert_eq!(
            answer_reply(&US, &host, 0, 0x00, &mut forced).0[12 + 26],
            0x09,
            "MD_DP_REPLY_FORCE overrides the post-RSA type"
        );
        let mut forced_data = State::default();
        forced_data.force_data_reply_type = Some(0x0A);
        assert_eq!(
            answer_reply(&US, &host, 0, 0x04, &mut forced_data).0[12 + 26],
            0x0A,
            "MD_DP_REPLY_FORCE_DATA overrides the 04 type"
        );

        // The player-body probe: one UsernameReply, then back to RsaReply.
        // Off by default the post-RSA type is untouched (the arm above).
        let mut probed = State::default();
        probed.saw_rsa = true;
        probed.booted = true;
        probed.player_body = true;
        let (first, what) = answer_reply(&US, &host, 0, 0x00, &mut probed);
        assert_eq!(what, "player-body");
        assert_eq!(first[12 + 26], 0x07, "Username reply type");
        assert_eq!(first[12 + 27], 0x01, "snippet 1 carries name chars");
        let (second, what) = answer_reply(&US, &host, 0, 0x00, &mut probed);
        assert_eq!(what, "rsa(post)", "one shot only");
        assert_eq!(second[12 + 26], 0x08);
    }

    /// The RSA and the round that follows it arriving in one drain batch.
    ///
    /// The joiner drains the console's inbox on a 5 ms tick and answers only the
    /// *newest* command in a batch, because an older round's reply window has
    /// already passed. The host's own scheduler is free to put the RSA frame and
    /// the dummy round after it in that one tick, and that is where `saw_rsa`
    /// was being set: in the send arm, for the newest command only. So a batched
    /// RSA was *read* (its size parsed, its command logged) but never
    /// *remembered*, and every round after it - including the ones the re-send
    /// arm repeats - was answered with a Username reply instead of the RsaReply
    /// that moves the host's mode machine from 14 to 6. Super Mario 64 DS's host
    /// parks exactly there (measured: `NameRequest -> 00 -> RSA -> 00`, then no
    /// further command and `download: 0 of 514896 bytes`), while Star Fox's own
    /// (faster, bespoke) host happened to land the two frames in separate ticks.
    #[test]
    fn an_rsa_is_remembered_even_when_the_round_after_it_arrives_in_the_same_batch() {
        let bus = MockBus::new();
        let dp = DownloadPlayJoiner::new(true, 1);
        let mut st = State::default();
        let t0 = Instant::now();
        bus.push(beacon_packet(HOST, 7, &[1], &[]));
        step(&bus, &dp, &mut st, t0);
        bus.push(response_packet(0x00B0, US, HOST));
        step(&bus, &dp, &mut st, t0 + RESEND);
        bus.push(assoc_response_packet(US, HOST, 0, 0xC001));
        step(&bus, &dp, &mut st, t0 + 2 * RESEND);

        bus.push(command_packet(HOST, 0x03));
        bus.push(command_packet(HOST, 0x00));
        step(&bus, &dp, &mut st, t0 + 3 * RESEND);
        let r = bus.replies().last().unwrap().0.clone();
        assert_eq!(
            (r[12 + 24], r[12 + 26]),
            (0x04, 0x08),
            "the round after a batched RSA still gets the RsaReply, not a Username reply"
        );

        // And with nothing new to answer, the re-send arm keeps asking as the
        // RSA client rather than falling back to the username.
        step(&bus, &dp, &mut st, t0 + 4 * RESEND);
        let r = bus.replies().last().unwrap().0.clone();
        assert_eq!(r[12 + 26], 0x08, "the re-sent answer is still the RsaReply");
    }

    /// A `04` DataPacket as GBATEK lays it out: reply time, slave flags, the size
    /// byte stating `[06h..end]` in halfwords, flags, the class, two unknown
    /// bytes, the packet number at `09h`, the data at `0Bh`, the spec's one
    /// padding byte, and the two-byte footer.
    fn data_command_packet(host: [u8; 6], packet: u16, data: &[u8]) -> Vec<u8> {
        const CMD_MAC: [u8; 6] = [0x03, 0x09, 0xBF, 0x00, 0x00, 0x00];
        assert_eq!(data.len() % 2, 0, "test data is a whole number of halfwords");
        let mut f = mgmt_header(0x0228, &CMD_MAC, &host, &CMD_MAC, 0x0060);
        f.extend_from_slice(&0x0106u16.to_le_bytes()); // reply time
        f.extend_from_slice(&0x0002u16.to_le_bytes()); // slave flags
        // [06h..end] = command(1) + unknown(2) + packet(2) + data + pad(1)
        f.push(((6 + data.len()) / 2) as u8);
        f.push(0x11); // flags
        f.push(0x04); // DataPacket
        f.extend_from_slice(&[0x00, 0x00]); // 07h..08h unknown, zero
        f.extend_from_slice(&packet.to_le_bytes()); // 09h..0Ah
        f.extend_from_slice(data);
        f.push(0x00); // the stated region's own padding byte
        f.extend_from_slice(&[0x02, 0x00]); // footer: slave flags again
        f.extend_from_slice(&[0xDE, 0xAD, 0xBE, 0xEF]); // FCS slot
        wrap(7, &f)
    }

    /// The download's parser: the packet number is at body `09h`, the data at
    /// `0Bh` and the trailing padding byte is not data. A real 510-byte packet
    /// comes out at GBATEK's 1F8h, not 1F9h.
    #[test]
    fn a_data_packet_yields_its_number_and_exactly_its_data() {
        let full = vec![0xA5u8; 0x1F8];
        let pkt = data_command_packet(HOST, 0x0123, &full);
        assert_eq!(
            classify(&pkt, &US),
            Some(Rx::Command {
                command: 0x04,
                size: ((6 + full.len()) / 2) as u8,
                flags: 0x11,
                slave_flags: 0x0002,
                packet: Some(0x0123),
                data: full.clone(),
                rsa_total: None,
                rsa_boot: None,
            })
        );
        // A packet whose stated size leaves no room for data still reports its
        // number rather than a slice taken out of the header.
        let short = data_command_packet(HOST, 7, &[]);
        assert_eq!(data_packet(&short), Some((7, Vec::new())));
        // A frame too short to carry the header is refused, not read as packet 0.
        assert_eq!(data_packet(&pkt[..12 + 24 + 4]), None);
    }

    /// The packet map: distinct packets count once, the request pointer always
    /// names the smallest hole, and completion needs both a stated total and the
    /// bytes to match it.
    #[test]
    fn the_image_map_dedups_and_walks_its_hole() {
        let mut img = Image::default();
        assert!(!img.complete(), "no total yet means never complete");
        assert!(img.add(0, &[0u8; 8]));
        assert!(!img.add(0, &[0u8; 8]), "a repeated packet adds nothing");
        assert_eq!((img.received, img.bytes, img.next), (1, 8, 1));
        // A packet beyond the hole is recorded but the hole does not move.
        assert!(img.add(3, &[0u8; 8]));
        assert_eq!((img.received, img.bytes, img.next), (2, 16, 1));
        // Filling the hole walks the pointer to the next one.
        assert!(img.add(1, &[0u8; 8]));
        assert_eq!(img.next, 2);
        assert!(img.add(2, &[0u8; 8]));
        assert_eq!(img.next, 4, "past the highest packet held");
        img.total_bytes = img.bytes;
        assert!(img.complete());
        // The image is held, not merely counted: the assembled buffer is every
        // packet's payload in packet order.
        assert_eq!(img.assemble().map(|v| v.len()), Some(img.bytes as usize));
        // A hole means no image to boot, however many bytes have arrived.
        let mut holed = Image::default();
        holed.add(0, &[0u8; 8]);
        holed.add(2, &[0u8; 8]);
        holed.total_bytes = holed.bytes;
        assert!(!holed.complete());
        assert_eq!(holed.assemble(), None);
        // A wildly out-of-range packet number is still bounded by the field's own
        // width, so a corrupt value cannot grow the map without limit.
        assert!(img.add(u16::MAX, &[0u8; 4]));
        assert!(img.packets.len() <= usize::from(u16::MAX) + 1);
    }

    /// The load map out of the RSA frame: the same frame the total comes from,
    /// read for the destinations and entrypoints the boot needs.
    #[test]
    fn the_rsa_frame_states_the_load_map_the_boot_uses() {
        let info = BootInfo {
            arm9_entry: 0x02000000,
            arm7_entry: 0x02380000,
            header_dest: 0x027E0000,
            header_size: 0x160,
            arm9_dest: 0x02000000,
            arm9_size: 0x12340,
            arm7_dest: 0x02380000,
            arm7_temp: 0x022C0000,
            arm7_size: 0x5678,
        };
        let pkt = rsa_command_packet(HOST, &info);
        assert_eq!(rsa_boot_info(&pkt), Some(info));
        assert_eq!(rsa_total_bytes(&pkt), Some(0x160 + 0x12340 + 0x5678));
    }

    /// The transfer's whole transition table, one arm per statement the host
    /// makes. Each boundary is a state a real host passes through, and each
    /// one below `Closed` must not boot a console.
    #[test]
    fn the_transfer_advances_only_on_the_statements_that_earn_each_state() {
        let dp = DownloadPlayJoiner::new(true, 1);
        let mut st = State::default();
        st.advance_transfer(&dp);
        assert_eq!(st.transfer, Transfer::Unstated, "a fresh state has no RSA yet");

        // The RSA frame is what states the image; until it parses, nothing
        // else moves the transfer, however many name rounds have gone by.
        st.saw_rsa = true;
        st.advance_transfer(&dp);
        assert_eq!(st.transfer, Transfer::Streaming);

        // An ACK that arrives before the image is complete is the collection
        // of some *earlier* reply, not of the completion answer: the table
        // cannot skip past `Complete`, whatever is set on the side.
        st.got_all_ack = true;
        st.advance_transfer(&dp);
        assert_eq!(
            st.transfer,
            Transfer::Streaming,
            "a collected reply before the image is complete is not the completion answer"
        );
        st.image.total_bytes = 4;
        st.image.add(0, &[0u8; 4]);
        // The host is still streaming (`04`) while its ACK collects the answer
        // that has just completed the image: one fixpoint walk legitimately
        // reaches `Collected` - and stops there, because the last command it
        // has seen is still a data round.
        st.command = Some(0x04);
        st.advance_transfer(&dp);
        assert_eq!(st.transfer, Transfer::Collected);

        // A host still asking for data has not closed the download...
        st.advance_transfer(&dp);
        assert_eq!(st.transfer, Transfer::Collected, "a `04` is still a data round");

        // ... and the collection plus the host leaving the data class is the
        // close: the state the boot reads and nothing else.
        st.command = Some(0x00);
        st.advance_transfer(&dp);
        assert_eq!(st.transfer, Transfer::Closed);

        // A GBATEK Final is the same last step (the host has left the data
        // class) - but only from `Collected`: a Final from a host that never
        // read this client's answer is not a close.
        st.transfer = Transfer::Complete;
        st.got_all_ack = false;
        st.command = Some(0x05);
        st.advance_transfer(&dp);
        assert_eq!(
            st.transfer,
            Transfer::Complete,
            "a Final from a host that never collected the answer is not a close"
        );
    }

    /// The transfer must move monotonically: a step that observes nothing new
    /// cannot walk it back, or a boot would be able to fire, un-fire and fire
    /// again. The host's statements arrive out of order, so this is the
    /// property the boot's single-reader design rests on.
    #[test]
    fn the_transfer_never_moves_backwards() {
        let dp = DownloadPlayJoiner::new(true, 1);
        let mut st = State::default();
        st.transfer = Transfer::Closed;
        // Every fact the table reads, cleared: nothing here may lower the
        // state.
        st.saw_rsa = false;
        st.got_all_ack = false;
        st.command = None;
        st.advance_transfer(&dp);
        assert_eq!(st.transfer, Transfer::Closed);

        // The deauthentication arm is the one place it resets, and it resets
        // all the way - a re-registered client starts the new session from
        // `Unstated` rather than inheriting the old one's completion.
        let bus = MockBus::new();
        let mut fresh = State::default();
        fresh.phase = Phase::Associated;
        fresh.aid = 1;
        fresh.transfer = Transfer::Closed;
        fresh.saw_rsa = true;
        fresh.got_all_ack = true;
        bus.push(deauth_packet(US, HOST, 0x0001));
        step(&bus, &dp, &mut fresh, Instant::now());
        assert_eq!(fresh.transfer, Transfer::Unstated);
        assert!(!fresh.got_all_ack, "the old session's facts go with the old session");
    }

    /// The handoff: once the image is complete the client deauthenticates the
    /// host (reason 3) and the bus boots the assembled bytes at the RSA
    /// destinations and entrypoints. This is the difference between downloading
    /// and Download Play, so it is asserted on the bytes, not a counter.
    #[test]
    fn a_completed_download_is_handed_off_and_booted() {
        let bus = MockBus::new();
        let dp = DownloadPlayJoiner::new(true, 1);
        let mut st = State::default();
        let t0 = Instant::now();

        bus.push(beacon_packet(HOST, 7, &[1], &[]));
        step(&bus, &dp, &mut st, t0);
        bus.push(response_packet(0x00B0, US, HOST));
        step(&bus, &dp, &mut st, t0 + RESEND);
        bus.push(assoc_response_packet(US, HOST, 0, 0xC001));
        step(&bus, &dp, &mut st, t0 + 2 * RESEND);

        // Header (0x160) then ARM9 then ARM7, as the RSA frame states them.
        let header = vec![0x11u8; 0x160];
        let arm9 = vec![0x22u8; 0x40];
        let arm7 = vec![0x33u8; 0x20];
        let info = BootInfo {
            arm9_entry: 0x02000000,
            arm7_entry: 0x02380000,
            header_dest: 0x027E0000,
            header_size: 0x160,
            arm9_dest: 0x02000000,
            arm9_size: 0x40,
            arm7_dest: 0x02380000,
            arm7_temp: 0x022C0000,
            arm7_size: 0x20,
        };
        bus.push(rsa_command_packet(HOST, &info));
        step(&bus, &dp, &mut st, t0 + 3 * RESEND);
        assert!(st.saw_rsa, "the RSA round is latched where it is read");

        bus.push(data_command_packet(HOST, 0, &header));
        step(&bus, &dp, &mut st, t0 + 4 * RESEND);
        assert!(bus.booted.lock().unwrap().is_none(), "not complete yet");
        bus.push(data_command_packet(HOST, 1, &arm9));
        bus.push(data_command_packet(HOST, 2, &arm7));
        step(&bus, &dp, &mut st, t0 + 5 * RESEND);

        // The completing round is *answered*, not booted on. Booting here was
        // the v0.1.9dd defect: it returned before the send path, so this
        // round's reply never left, the host's window read nothing, and the
        // host never sent the Final that ends the transfer - the child then
        // came up inside a download that never closed.
        assert!(
            bus.booted.lock().unwrap().is_none(),
            "the completing round answers the host; it does not boot"
        );
        assert!(
            bus.replies()
                .iter()
                .any(|(f, _)| f.len() > 12 + 26 && f[12 + 24] == 0x04 && f[12 + 26] == 0x0A),
            "the round that completes the image is answered with the GotAllReply"
        );

        // The boot waits for the host to close the download from its own side.
        // A host still sending `04` has not closed it, and booting mid-transfer
        // is exactly what lost the host's acknowledgement in v0.1.9dd.
        bus.push(data_command_packet(HOST, 2, &arm7));
        step(&bus, &dp, &mut st, t0 + 6 * RESEND);
        assert!(
            bus.booted.lock().unwrap().is_none(),
            "a host still asking for data has not closed the download"
        );

        // Its ACK collecting this client's answer, and its class moving off the
        // data rounds, are the two statements the boot needs - neither alone is
        // enough.
        bus.push(ack_packet(HOST, 0x0000));
        step(&bus, &dp, &mut st, t0 + 7 * RESEND);
        assert!(
            bus.booted.lock().unwrap().is_none(),
            "the ACK alone does not boot while the data class is still in use"
        );
        bus.push(command_packet(HOST, 0x00));
        step(&bus, &dp, &mut st, t0 + 8 * RESEND);

        let (image, got, session) = bus
            .booted
            .lock()
            .unwrap()
            .clone()
            .expect("the completed image is handed off");
        assert_eq!(got, info);
        // The session the hand-over carries is the one this client negotiated,
        // all of it: the host's channel (a downloaded child does not scan for
        // one, so without this its `CurChannel` is 0 - mute and deaf), the
        // host's own BSSID (the station it is associated *with*), and the AID
        // the host's association response granted (`0xC001` -> 1 above, the
        // same value `Wifi` uses as `MPMYAID`). The earlier version carried the
        // channel alone and the child was still a stranger to its host.
        assert_eq!(session.channel, 7, "the child keeps the channel the host beacons on");
        assert_eq!(session.bssid, HOST, "the child keeps the host it associated with");
        assert_eq!(session.aid, 1, "the child keeps the AID the host granted it");
        let mut expected = header.clone();
        expected.extend_from_slice(&arm9);
        expected.extend_from_slice(&arm7);
        assert_eq!(image, expected, "every packet's payload, in packet order");
        assert!(dp.is_booted());

        // The handoff must NOT deauthenticate the host. That extra frame is
        // what made the host's own game broadcast a deauth and power its access
        // point down - measured live - so the title it had just booted came up
        // with nothing to join. `handoff`'s comment carries the frames.
        assert!(
            !bus.sent()
                .iter()
                .any(|f| u16::from_le_bytes([f[12], f[13]]) == 0x00C0),
            "the handoff leaves the host's session up: it sends no deauthentication"
        );
    }

    /// After the handoff the joiner must stop speaking for the console - no
    /// drains, no replies - but it must keep *holding* it on the bus. Those are
    /// two different things and the live run separated them: a real client
    /// boots with its radio up, while this fork's boot is a whole-console
    /// `Reset()` and the console's bus membership is tied to the game's own
    /// wireless power register, so the booted game's own bring-up power-cycles
    /// the block - measured, three times, at frames 3591->3595 and
    /// 3800->3801 - and every "off" detaches it (`LocalMP::End`), after which
    /// `LocalMP::Route` writes it nothing and the delivery ledger freezes for
    /// the rest of the run. The child's game reads zero frames and no command
    /// crosses, which is what this hold exists to prevent.
    #[test]
    fn a_booted_client_is_held_on_the_bus_and_answered_for() {
        let bus = MockBus::new();
        let dp = DownloadPlayJoiner::new(true, 1);
        let mut st = State::default();
        let t0 = Instant::now();
        let header = vec![0x11u8; 0x160];
        let info = BootInfo {
            header_dest: 0x027FFE00,
            header_size: 0x160,
            arm9_dest: 0x02000000,
            arm9_size: 4,
            ..BootInfo::default()
        };

        // The host only addresses a registered client, so the association is
        // the precondition for every command below (and for the reply path the
        // boot waits on).
        bus.push(beacon_packet(HOST, 7, &[1], &[]));
        step(&bus, &dp, &mut st, t0);
        bus.push(response_packet(0x00B0, US, HOST));
        step(&bus, &dp, &mut st, t0 + RESEND);
        bus.push(assoc_response_packet(US, HOST, 0, 0xC001));
        step(&bus, &dp, &mut st, t0 + 2 * RESEND);
        bus.push(rsa_command_packet(HOST, &info));
        step(&bus, &dp, &mut st, t0 + 3 * RESEND);
        bus.push(data_command_packet(HOST, 0, &header));
        step(&bus, &dp, &mut st, t0 + 4 * RESEND);
        bus.push(data_command_packet(HOST, 1, &[0xAA; 4]));
        step(&bus, &dp, &mut st, t0 + 5 * RESEND);
        // The image is complete, but the client waits for the host's Final
        // before it boots (see `a_completed_download_is_handed_off_and_booted`).
        assert!(!dp.is_booted(), "complete is not the same as transferred");
        // The host closes the download: it collects this client's completion
        // answer and stops asking for data.
        bus.push(ack_packet(HOST, 0x0000));
        bus.push(command_packet(HOST, 0x00));
        step(&bus, &dp, &mut st, t0 + 6 * RESEND);
        assert!(dp.is_booted());

        // The child's own game powers its block off as its bring-up cycles it,
        // which detaches the console underneath the hand-off. While the radio
        // is off the joiner is the firmware stand-in: it holds the bus and
        // answers the host's polls so the host keeps polling instead of
        // deauthenticating a silent client. But the drain and the game's own
        // CheckRX read the same per-slot queue, so the join/scan frames go
        // back for the game - consuming a beacon here is one the game's scan
        // never sees, which is what parked Bomberman's guest with `answered 0`.
        bus.listening.store(false, Ordering::SeqCst);
        let sent_before = bus.sent().len();
        let replies_before = bus.replies().len();
        let beacon = beacon_packet(HOST, 7, &[1], &[]);
        bus.push(beacon.clone());
        step(&bus, &dp, &mut st, t0 + 7 * RESEND);
        assert!(
            bus.receiving(),
            "a booted client is held on the bus: the router must keep writing it frames"
        );
        assert_eq!(
            bus.requeued.lock().unwrap().clone(),
            vec![beacon],
            "a post-boot beacon goes back to the inbox for the game's scan"
        );
        assert_eq!(
            dp.counters.beacons.load(Ordering::SeqCst),
            1,
            "restored frames are not tracked as heard: only the pre-boot handshake is"
        );
        assert_eq!(bus.sent().len(), sent_before, "a booted joiner sends no management frames");
        assert_eq!(
            bus.replies().len(),
            replies_before + 1,
            "the stand-in keeps the last polled class answered while the game is off-radio"
        );

        // The hold is re-asserted, not re-done: a second step on an attached
        // console attaches no further, and inside the resend window it answers
        // no further.
        step(&bus, &dp, &mut st, t0 + 7 * RESEND + RESEND / 2);
        assert_eq!(dp.counters.radio_hold.load(Ordering::SeqCst), 1);
        assert_eq!(bus.replies().len(), replies_before + 1);

        // While the game's own radio is up it owns the bus, and the hold leaves
        // it alone rather than attaching a console its game has already
        // attached (and, later, detached) for itself.
        bus.wireless.store(true, Ordering::SeqCst);
        bus.listening.store(false, Ordering::SeqCst);
        step(&bus, &dp, &mut st, t0 + 9 * RESEND);
        assert!(
            !bus.receiving(),
            "a booted client whose own game is on the radio must not be re-attached by this joiner"
        );
        assert_eq!(dp.counters.radio_hold.load(Ordering::SeqCst), 1);

        // An unarmed joiner still drops the hold: the gate is the arm switch.
        dp.set_armed(false);
        bus.wireless.store(false, Ordering::SeqCst);
        step(&bus, &dp, &mut st, t0 + 10 * RESEND);
        assert!(!bus.receiving(), "disarm must drop the hold");
    }

    /// The RSA frame states the whole image's size, and the joiner only declares
    /// the transfer finished against it - so a frame that does not parse must
    /// leave the total at zero rather than approve an empty download.
    #[test]
    fn the_rsa_frame_states_the_image_size_the_download_is_measured_against() {
        let mut sig = vec![0u8; 0xE4];
        sig[0x14..0x18].copy_from_slice(&0x160u32.to_le_bytes()); // header
        sig[0x24..0x28].copy_from_slice(&0x12340u32.to_le_bytes()); // ARM9
        sig[0x34..0x38].copy_from_slice(&0x5678u32.to_le_bytes()); // ARM7
        let body = sig.clone();
        let mut f = mgmt_header(0x0228, &[0x03, 0x09, 0xBF, 0x00, 0x00, 0x00], &HOST, &[0x03, 0x09, 0xBF, 0x00, 0x00, 0x00], 0x0040);
        f.extend_from_slice(&0x0106u16.to_le_bytes());
        f.extend_from_slice(&0x0002u16.to_le_bytes());
        f.push(((1 + 2 + body.len()) / 2) as u8);
        f.push(0x11);
        f.push(0x03); // RSA
        f.extend_from_slice(&body);
        f.extend_from_slice(&[0xDE, 0xAD, 0xBE, 0xEF]);
        let pkt = wrap(7, &f);
        assert_eq!(rsa_total_bytes(&pkt), Some(0x160 + 0x12340 + 0x5678));
        // A frame too short to carry the sizes yields no total at all.
        assert_eq!(rsa_total_bytes(&pkt[..12 + 24 + 0x20]), None);
    }

    /// The reply that advances the download: GBATEK's DataReply carries the
    /// smallest missing packet at `03h` and the distinct-packet count at `05h`,
    /// and the GotAllReply replaces it once the image is complete.
    #[test]
    fn a_data_packet_is_answered_with_the_smallest_missing_packet() {
        let host = parse_beacon(&beacon_packet(HOST, 7, &[1], &[])).unwrap();
        let mut st = State::default();
        st.saw_rsa = true;
        st.image.add(0, &[0u8; 16]);
        st.image.add(1, &[0u8; 16]);
        st.image.add(3, &[0u8; 16]);
        let (pkt, what) = answer_reply(&US, &host, 0, 0x04, &mut st);
        let body = &pkt[12 + 24..];
        assert_eq!(what, "data");
        assert_eq!((body[0], body[1], body[2]), (0x04, 0x01, 0x09), "normal DataReply");
        assert_eq!(u16::from_le_bytes([body[3], body[4]]), 2, "the hole is #2");
        assert_eq!(u16::from_le_bytes([body[5], body[6]]), 3, "three distinct");

        // Complete (the hole at #2 filled): the same body shape, type 0Ah.
        st.image.add(2, &[0u8; 16]);
        st.image.total_bytes = st.image.bytes;
        let (pkt, what) = answer_reply(&US, &host, 0, 0x04, &mut st);
        assert_eq!(what, "got-all");
        assert_eq!((pkt[36], pkt[37], pkt[38]), (0x04, 0x01, 0x0A));

        // The host's Final command is confirmed with type 0Bh.
        let (pkt, what) = answer_reply(&US, &host, 0, 0x05, &mut st);
        assert_eq!(what, "final");
        assert_eq!((pkt[36], pkt[37], pkt[38]), (0x04, 0x01, 0x0B));
    }

    /// The host's ack is the only feedback the protocol gives a client, so it
    /// is what decides whether the username is repeated or advanced.
    #[test]
    fn the_username_snippet_advances_only_when_the_host_collects_our_reply() {
        let bus = MockBus::new();
        let dp = DownloadPlayJoiner::new(true, 1);
        let mut st = State::default();
        let t0 = Instant::now();
        bus.push(beacon_packet(HOST, 7, &[1], &[]));
        step(&bus, &dp, &mut st, t0);
        bus.push(response_packet(0x00B0, US, HOST));
        step(&bus, &dp, &mut st, t0 + RESEND);
        bus.push(assoc_response_packet(US, HOST, 0, 0xC001));
        bus.push(command_packet(HOST, 0x01));
        step(&bus, &dp, &mut st, t0 + 2 * RESEND);
        assert_eq!(bus.replies().len(), 1);
        assert_eq!(bus.replies()[0].0[12 + 27], 0x00, "the first snippet");

        // The host still reports slave 1 missing -> repeat, do not skip.
        bus.push(ack_packet(HOST, 0x0002));
        step(&bus, &dp, &mut st, t0 + 3 * RESEND);
        assert_eq!(bus.replies().len(), 2);
        assert_eq!(bus.replies()[1].0[12 + 27], 0x00, "a lost reply is repeated, not skipped");

        // The host now says slave 1 answered, and it is still asking
        // NameRequest -> the next snippet.
        bus.push(ack_packet(HOST, 0x0000));
        bus.push(command_packet(HOST, 0x01));
        step(&bus, &dp, &mut st, t0 + 4 * RESEND);
        assert_eq!(bus.replies().len(), 3);
        assert_eq!(bus.replies()[2].0[12 + 27], 0x01, "a collected reply advances the snippet");
        assert_eq!(dp.counters.replies_collected.load(Ordering::SeqCst), 1);
    }

    #[test]
    fn the_dummy_reply_is_the_first_frame_a_real_client_sends() {
        // Byte for byte the client's own first transmission in
        // logs/mph4b_app_4con.log.gz (`tx frame: slot 5 (REPLY) fc=0118 len=30
        // seq=0020`, read by the host as `26 byte frame, body[2]=00 00`).
        let b = parse_beacon(&beacon_packet(HOST, 7, &[1], &[0x01, 0x02, 0x82, 0x84])).unwrap();
        let pkt = build_dummy_reply(&US, &b, 0x0020);
        let f = &pkt[12..];
        assert_eq!(u16::from_le_bytes([f[0], f[1]]), 0x0118);
        assert_eq!(&f[4..10], &HOST); // receiver: the host
        assert_eq!(&f[10..16], &US); // transmitter: this client
        assert_eq!(&f[16..22], &REPLY_MAC); // BSSID 03:09:BF:00:00:10
        assert_eq!(&f[22..24], &0x0020u16.to_le_bytes());
        assert_eq!(&f[24..26], &[0x00, 0x00]); // dummy reply: short, no flags
        assert_eq!(pkt.len(), 12 + 30);
    }

    #[test]
    fn disarming_drops_the_client_radio_again() {
        let bus = MockBus::new();
        let dp = DownloadPlayJoiner::new(true, 1);
        let mut st = State::default();
        bus.push(beacon_packet(HOST, 7, &[1], &[]));
        step(&bus, &dp, &mut st, Instant::now());
        assert!(bus.receiving());

        dp.set_armed(false);
        step(&bus, &dp, &mut st, Instant::now());
        assert!(!bus.receiving(), "disarm must detach");
        assert_eq!(st.phase, Phase::Beaconed, "the host is remembered, not re-scanned");
    }

    /// The bus's checksum rule, copied so a test can model what the send path
    /// does to a frame (the real one is `LocalMP::mp_fcs32`).
    fn fcs32(frame: &[u8]) -> u32 {
        let mut crc = 0xFFFFFFFFu32;
        for &b in frame {
            crc ^= u32::from(b);
            for _ in 0..8 {
                crc = (crc >> 1) ^ (0xEDB8_8320u32 & (0u32.wrapping_sub(crc & 1)));
            }
        }
        !crc
    }

    /// The regression that cost a live run: a frame built without its own
    /// four-byte FCS slot has the bus write the checksum over the last four
    /// bytes of the body, and the host then reads the sequence number out of
    /// the CRC. A real client's authentication is `len=46` (24+6+4); this pins
    /// that shape and proves the body survives the send path.
    #[test]
    fn the_frames_carry_their_own_fcs_slot_so_the_bus_cannot_clobber_the_body() {
        let b = parse_beacon(&beacon_packet(HOST, 7, &[1, 2], &[0x01, 0x02, 0x82, 0x84])).unwrap();
        for pkt in [
            build_auth_request(&US, &b, 0x0000),
            build_assoc_request(&US, &b, 0x0010, CLIENT_NAME),
        ] {
            // The length the receiver validates against `rxlen - 12`, and the
            // four-byte slot the bus writes its checksum into.
            assert_eq!(u16::from_le_bytes([pkt[10], pkt[11]]) as usize, pkt.len() - 12);
            let f = &pkt[12..];
            assert_eq!(f.len(), 24 + (f.len() - 24 - 4) + 4);
            let body = f[24..f.len() - 4].to_vec();

            // Model exactly what ApplyWireFCS does at send time.
            let mut wire = pkt.clone();
            let end = wire.len();
            let crc = fcs32(&wire[12..end - 4]);
            wire[end - 4..].copy_from_slice(&crc.to_le_bytes());

            assert_eq!(
                &wire[12 + 24..wire.len() - 4],
                &body[..],
                "the checksum must land in its own slot, not over the body"
            );
        }

        // ... and the fields the host reads survive the send path byte for
        // byte: an authentication is algorithm 0, sequence 1, status 0 (what a
        // real client sends and what the host answers with status 0).
        let auth = build_auth_request(&US, &b, 0x0000);
        assert_eq!(u16::from_le_bytes([auth[12 + 24], auth[12 + 25]]), 0);
        assert_eq!(u16::from_le_bytes([auth[12 + 26], auth[12 + 27]]), 1);
        assert_eq!(u16::from_le_bytes([auth[12 + 28], auth[12 + 29]]), 0);
        // The association's fixed body: capability echoed, listen interval.
        let assoc = build_assoc_request(&US, &b, 0x0010, CLIENT_NAME);
        assert_eq!(u16::from_le_bytes([assoc[12 + 24], assoc[12 + 25]]), 0x0021);
        assert_eq!(u16::from_le_bytes([assoc[12 + 26], assoc[12 + 27]]), 0x0001);
        // A real client's authentication frame is 46 bytes on the air (12-byte
        // header + 24-byte 802.11 header + 6-byte body + 4-byte FCS); ours must
        // be the same shape.
        assert_eq!(auth.len(), 46);
    }

    #[test]
    fn an_auth_response_carries_the_status_that_decides_the_join() {
        let mut f = mgmt_header(0x00B0, &US, &HOST, &HOST, 0x00C0);
        f.extend_from_slice(&[0, 0, 2, 0, 1, 0]); // algorithm 0, sequence 2, status 1
        f.extend_from_slice(&[0; 4]);
        assert_eq!(
            classify(&wrap(7, &f), &US),
            Some(Rx::AuthResponse { seq: 2, status: 1 })
        );
    }

    #[test]
    fn a_deauth_is_read_as_the_hosts_refusal_and_restarts_the_client() {
        let bus = MockBus::new();
        let dp = DownloadPlayJoiner::new(true, 1);
        let mut st = State::default();
        let t0 = Instant::now();

        bus.push(beacon_packet(HOST, 7, &[1], &[]));
        step(&bus, &dp, &mut st, t0);
        bus.push(response_packet(0x00B0, US, HOST));
        step(&bus, &dp, &mut st, t0 + RESEND);
        assert_eq!(dp.counters.assoc_sent.load(Ordering::SeqCst), 1);

        // The host's answer to the association is a deauth with a reason code.
        bus.push(deauth_packet(US, HOST, 0x0003));
        step(&bus, &dp, &mut st, t0 + 2 * RESEND);
        assert_eq!(dp.counters.deauths.load(Ordering::SeqCst), 1);
        assert_eq!(dp.counters.last_deauth_reason.load(Ordering::SeqCst), 3);
        assert_eq!(st.phase, Phase::Beaconed, "a deauth restarts the client");

        // ... and it authenticates again rather than repeating the refusal
        // forever without a fresh handshake.
        step(&bus, &dp, &mut st, t0 + 3 * RESEND);
        assert_eq!(dp.counters.auth_sent.load(Ordering::SeqCst), 2);
    }

    #[test]
    fn a_response_addressed_to_someone_else_is_not_ours() {
        let other = [0x00, 0x09, 0xBF, 0x13, 0xAA, 0x53];
        assert_eq!(
            classify(&assoc_response_packet(other, HOST, 0, 0xC001), &US),
            Some(Rx::Other { fc: 0x0010, from: HOST })
        );
        assert_eq!(
            classify(&assoc_response_packet(US, HOST, 0, 0xC001), &US),
            Some(Rx::AssocResponse { status: 0, aid: 0xC001 })
        );
    }

    #[test]
    fn short_or_truncated_packets_are_ignored() {
        assert_eq!(parse_beacon(&[]), None);
        assert_eq!(parse_beacon(&[0u8; 20]), None);
        // A header that claims more frame than the packet carries.
        let mut pkt = beacon_packet(HOST, 7, &[1], &[]);
        pkt.truncate(pkt.len() - 10);
        assert_eq!(parse_beacon(&pkt), None);
        assert_eq!(classify(&[0u8; 8], &US), None);
    }

    /// The retirement seam, pinned: a `Silent` client keeps presence and tracks
    /// the protocol but speaks no frame — the posture a guest-owned radio
    /// needs, with the download still driven by the default `Speak`.
    #[test]
    fn a_silent_client_listens_tracks_and_answers_nothing() {
        let bus = MockBus::new();
        let dp = DownloadPlayJoiner::new(true, 1);
        let mut st = State::default();
        st.speech = ClientSpeech::Silent;
        let t0 = Instant::now();

        // Presence without speech: the beacon attaches the console, but no
        // authentication leaves.
        bus.push(beacon_packet(HOST, 7, &[0x41], &[]));
        step(&bus, &dp, &mut st, t0);
        assert!(bus.receiving(), "silence is not absence: the client must listen");
        assert!(bus.sent().is_empty(), "a silent client sends nothing");
        assert_eq!(dp.counters.beacons.load(Ordering::SeqCst), 1);

        // Tracking without speech: the handshake is followed to Associated,
        // still with nothing sent.
        bus.push(response_packet(0x00B0, US, HOST));
        step(&bus, &dp, &mut st, t0 + RESEND);
        bus.push(assoc_response_packet(US, HOST, 0, 0xC001));
        step(&bus, &dp, &mut st, t0 + 2 * RESEND);
        assert_eq!(st.phase, Phase::Associated);
        assert!(bus.sent().is_empty());

        // The host's command is read (counters move) and never answered.
        bus.push(command_packet(HOST, 0x01));
        step(&bus, &dp, &mut st, t0 + 3 * RESEND);
        assert_eq!(dp.counters.commands_recv.load(Ordering::SeqCst), 1);
        assert_eq!(dp.counters.last_command.load(Ordering::SeqCst), 0x01);
        assert!(bus.replies().is_empty(), "a silent client answers nothing");
        // ... and the queue it would have answered stays bounded.
        assert!(st.queue.len() <= MAX_QUEUE);
    }

    /// The boot carries the one negotiated session, built in one place: the
    /// channel, BSSID, serial, token, capability, interval and rate mask off
    /// the host's beacon, the AID off its association response.
    #[test]
    fn the_boot_carries_the_session_the_client_negotiated() {
        let bus = MockBus::new();
        let dp = DownloadPlayJoiner::new(true, 1);
        let mut st = State::default();
        let t0 = Instant::now();

        assert_eq!(
            st.session(),
            Session {
                channel: 0,
                bssid: [0u8; 6],
                aid: 0,
                serial: [0u8; 4],
                token: [0u8; 2],
                capability: 0,
                interval: 0,
                rate_mask: 0,
                beacon_size: 0,
            },
            "unassociated: the untuned stranger the boot's guards handle"
        );

        bus.push(beacon_packet(HOST, 7, &[0x41], &[]));
        step(&bus, &dp, &mut st, t0);
        bus.push(response_packet(0x00B0, US, HOST));
        step(&bus, &dp, &mut st, t0 + RESEND);
        bus.push(assoc_response_packet(US, HOST, 0, 0xC001));
        step(&bus, &dp, &mut st, t0 + 2 * RESEND);
        // The test beacon builder stages interval 0x00D7, capability 0x0021
        // and the default 1/2Mbit rates with no DD tag: serial and token
        // stay zero and the beacon size falls back to the tagless shape.
        assert_eq!(
            st.session(),
            Session {
                channel: 7,
                bssid: HOST,
                aid: 1,
                serial: [0u8; 4],
                token: [0u8; 2],
                capability: 0x0021,
                interval: 0x00D7,
                rate_mask: 0x0003,
                beacon_size: (0usize.saturating_sub(8) + 0x41) as u16 / 2,
            },
            "associated: the session the hand-off carries across the boot"
        );
    }

    #[test]
    fn the_rate_mask_names_the_1_and_2_mbit_rates() {
        assert_eq!(rates_mask(&[0x01, 0x02, 0x82, 0x84]), 0x0003);
        assert_eq!(rates_mask(&[0x82, 0x0C]), 0x0001);
        assert_eq!(rates_mask(&[0x02]), 0x0002);
        assert_eq!(rates_mask(&[0x0C, 0x12]), 0x0000);
        assert_eq!(rates_mask(&[]), 0x0000);
    }

    #[test]
    fn the_beacon_size_follows_gbatek_off_the_dd_tag() {
        // GBATEK's example: a 0x88-long DD tag stages beacon size 0x60.
        assert_eq!(beacon_size(0x88), 0x0060);
        // No DD tag: the saturating shape, not a wrap.
        assert_eq!(beacon_size(0), (0x41 / 2) as u16);
    }

    /// The firmware stand-in, pinned: past the boot, with the game's radio
    /// off, the host's polls are answered (so it keeps polling instead of
    /// deauthenticating a silent client); the moment the game's radio is up
    /// the joiner is silent (the game owns those frames).
    #[test]
    fn a_booted_client_answers_polls_until_its_game_owns_the_radio() {
        let bus = MockBus::new();
        let dp = DownloadPlayJoiner::new(true, 1);
        let mut st = State::default();
        let t0 = Instant::now();

        // Join, associate, boot: the download's own path, line for line.
        bus.push(beacon_packet(HOST, 7, &[0x41], &[]));
        step(&bus, &dp, &mut st, t0);
        bus.push(response_packet(0x00B0, US, HOST));
        step(&bus, &dp, &mut st, t0 + RESEND);
        bus.push(assoc_response_packet(US, HOST, 0, 0xC001));
        step(&bus, &dp, &mut st, t0 + 2 * RESEND);
        assert_eq!(st.phase, Phase::Associated);
        // Boot by hand: the mock records the handoff instead of resetting a
        // console, so mark it the way `handoff` does.
        st.booted = true;
        dp.booted.store(true, Ordering::SeqCst);
        bus.replies().clear();
        let sent_before = bus.sent().len();

        // Game's radio off: a Dummy poll earns the stand-in's answer as the
        // associated AID.
        bus.push(command_packet(HOST, 0x00));
        step(&bus, &dp, &mut st, t0 + 3 * RESEND);
        let replies = bus.replies();
        assert_eq!(replies.len(), 1, "the stand-in answers while the game is off-radio");
        assert_eq!(replies[0].1, 1, "answered as the associated AID");
        assert!(st.booted, "answering never un-boots");
        assert_eq!(st.phase, Phase::Associated);

        // Game's radio up: the same poll is the game's now - drained never,
        // answered never.
        bus.wireless.store(true, Ordering::SeqCst);
        bus.push(command_packet(HOST, 0x00));
        step(&bus, &dp, &mut st, t0 + 4 * RESEND);
        assert_eq!(bus.replies().len(), 1, "silent the moment the game owns the radio");
        assert_eq!(
            dp.counters.commands_recv.load(Ordering::SeqCst), 1,
            "the un-drained poll is never even read"
        );
    }

    /// A post-boot deauthentication is the host's words about the *game's*
    /// session, not the joiner's: it must not tear the stand-in down (phase,
    /// AID and image stay) or the next poll goes unanswered - the silence
    /// that draws the deauth after it.
    #[test]
    fn a_post_boot_deauth_does_not_tear_the_stand_in_down() {
        let bus = MockBus::new();
        let dp = DownloadPlayJoiner::new(true, 1);
        let mut st = State::default();
        let t0 = Instant::now();

        bus.push(beacon_packet(HOST, 7, &[0x41], &[]));
        step(&bus, &dp, &mut st, t0);
        bus.push(response_packet(0x00B0, US, HOST));
        step(&bus, &dp, &mut st, t0 + RESEND);
        bus.push(assoc_response_packet(US, HOST, 0, 0xC001));
        step(&bus, &dp, &mut st, t0 + 2 * RESEND);
        st.booted = true;
        dp.booted.store(true, Ordering::SeqCst);
        let sent_before = bus.sent().len();

        bus.push(deauth_packet(US, HOST, 0x0003));
        step(&bus, &dp, &mut st, t0 + 3 * RESEND);
        assert_eq!(dp.counters.deauths.load(Ordering::SeqCst), 1, "counted");
        assert_eq!(st.phase, Phase::Associated, "stand-in keeps its phase");
        assert_eq!(st.aid, 1, "stand-in keeps its AID");
        assert_eq!(
            bus.sent().len(),
            sent_before,
            "no re-authentication is ever sent past the boot"
        );

        // ... and the next poll is still answered.
        bus.push(command_packet(HOST, 0x00));
        step(&bus, &dp, &mut st, t0 + 4 * RESEND);
        assert_eq!(bus.replies().len(), 1);
    }
}

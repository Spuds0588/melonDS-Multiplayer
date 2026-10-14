# melonDS Multiplayer - Development Roadmap

This document tracks the development progress and planned features for melonDS Multiplayer.

> **Primary Goal:** Local splitscreen multiplayer (2-4 players on one machine, couch co-op)
> **Secondary Goal:** Online play for remote players (future enhancement)

## Project Status: In Development

---

## The road to a working 2, 3 and 4-player link

*2026-09-29, v0.1.9aq. Per-game results are in [compatibility.md](./compatibility.md).
This section is the order to fix things in and the evidence that would close each
item. The bar is an **in-game match with the link healthy** — a live link that
never reaches a match is not done, and neither is a game whose own screen ignores
both consoles' input.*

Where it stands: **NSMB works at 2 players** (race reached, one console's input
measured in the peer's picture). **MKDS couples 2, 3 and 4 consoles into one
group through the app's own route and stays coupled** (`link mask 0x3` / `0x7` /
`0xF`, every console's digest read at the same emulated frame); at 2 the group
survives the game's own pre-race wait in **6 of 9** runs of the documented route
(2/4 at `MD_REPLY_KEEP=3`, 1/5 at the defaults, so the knobs do not separate the
arms), the host game's own state walk starts 115-147 emulated frames after the
tap in 7 of 9 runs, and 2 of the 6 survivors never reach the cut-off state
`FF 00` at all - but no race has been started at any count — in the surviving state both games sit in the
same ARM9 state with both displays black and the link perfect — though that
state is **not the queue wait it was read as for ten passes**: `pc9 0200FD10` is
the SDK's `delay(r0)` helper with `r0 = 0` (an instant, not a loop), `lr` and
`sp` are identical in every sample, and the tuple is an SDK wait-for-interrupt
idle (`0x200DBD4` calls `0x200FD04` = `mcr 15,0,r0,c7,c0,4`), while the ARM9's
IPC channel to the ARM7 is busy at ~7 words per frame throughout (history.md
v0.1.9ax). **Metroid Prime - Hunters now plays at 2 players**: driven through
the app's own channel, both consoles reach the lobby, pick hunters, and the
host's `START GAME` starts a `BATTLE` on `COMBAT HALL` with the 7-minute clock
running and each console drawing its own first-person view with its own HUD —
39-42 fps at two (history.md v0.1.9ay). Its 117-second stall was that lobby, and
so was the v0.1.9ad app dead end (the attract loop ignores touch until the title
screen; `START` moves it at once). At three and four consoles the limit measured
so far is the app's own screen path (26.6 fps at four, 44% of DS speed), not the
link — the "second join into an already-linked group" reading (history.md
v0.1.4) was retracted in v0.1.9al as a route-ordering defect and the four-console
couple is in v0.1.9am. **For MPH at 3-4 the limit is now measured and it is the
guests' own games**: a joining guest aborts its own handshake (`deauth sent …
reason 0003` from the guest, then its radio goes off) 7-23 s after joining, while
the host's exchange stays healthy and every console keeps running at 39-50 fps.
The stall kick, the reply-window re-anchor, the lobby's idle clock, link capacity
and CPU are each ruled out by measurement (history.md v0.1.9az), and
**serialising the joins - waiting on each guest's own screen before tapping the
next - is strictly better than overlapping them**, holding a host+guest pair to
the hunter roster at four consoles.

**What the guest is waiting for is now measured, and it is the host game's own
data (v0.1.9bi).** The host's directed commands come in two forms, and the one it
uses is the *host game's* own TX-slot write, passed to the link at the length the
game gave it (`Wifi.cpp:1098`): a 46-byte frame with a 6-byte body is a bare
poll, a 78-byte frame with a 38-byte body carries its session data. In the
three-console lobby that loses its guests the host's game **stops attaching that
body to a guest's commands permanently**, seconds before the guest leaves - p2 in
`logs/v9f_lobbyh_3p` got nothing but bare polls for its last 324 rounds (~5.5 s)
and its own answers still carried its payload to within 7 ms of its deauth; p3 in
`logs/v9e_3p` for its last 251 rounds. In the lobby that held, two consoles in the
room and the third outside it for 60+ s (`logs/mph11`), the host's game never
stopped: 5,445 of 8,462 commands carried a body, the last one is the run's
second-to-last command, and its longest bare stretch is 38 rounds. The link
carries everything the games hand it in both. The driver's own clock agrees
independently: in v9f the host's game sat on page `MODE` from 09:39:32 with two
taps doing nothing, its data to p2 stopped in the same second, and both guests'
radios went off at 09:39:39/40. **The two-console match is a different failure and
in it the guest is not the one that leaves:** in `logs/v9h_2p` the host's game
started a match (roster `pill 414`, `page=INGAME` at t+5 and t+10, 1,247 of 3,463
commands carrying a body to the last one) and then wrote its own *broadcast*
deauth out of its own LOC3 slot, with the guest's deauth 0.13 s behind it as
teardown. **Two readings are withdrawn with this.** Serialising the joins is not what saves
the pair - the host's game still stalling is what loses it; and v0.1.9bi's "the
host's roster never registered the 3-console guests' picks (`pill 0`)" was read
18-46 s *after* both guests had already left the room with their radios off, so
its `pill=69 icons=333` guest screens were local menus, not picks.
`tools/scripts/mph-match.sh` now refuses to repeat that reading.

**The two-console teardown is the games' own decision, and that is now measured
on demand (v0.1.9bp, `logs/v9af_*`).** A fresh two-console MPH run reached a real
`INGAME` at 07:35:32.6, held four seconds, and the host's game stopped attaching
its 38-byte session payload at 07:35:36.1 (its own `TXSLOT_CMD` length); the
guest's own game wrote its `deauth ... reason 0003` 4.4 s later, out of its own
LOC2 slot, right after its own `beacon IRQ14 fired ... with no beacon slot armed`,
and the host followed with its own broadcast session-end deauth. The link
credited everything in that run - `collected 2076 == acks 2076`, `clientfail 0`,
`empty 0`, `stale 0`, `reanchor-dropped 0`, guest `cmds rx 2076 / answered 2075`,
`delivered == read_game`, `drain 0` - and the same holds on `logs/v9d` (~28 s of
`INGAME`, `collected 4633 == acks 4633`, everything else zero). The fork has no
deauth generator: its counters only *observe* an `fc=00C0` frame leaving a game's
own TX slot, and it rewrites exactly two bytes of the host's command (the client
mask) and never its length. The three link changes the pass was asked to convict
are exonerated at the code level: `ApplyWireFCS` writes the FCS into the frame's
own trailing four bytes and leaves the length alone (the peer's crop removes
exactly those four); the delivered/read split is counters only, never consulted
on the send path; `md_link_test_attach` is a bus `Begin/End` with no ARM7
register, TX slot or mask bit, reachable only from the UI Link test and the
`MD_LINK_PROBE=1` auto-verifier. `python3 tools/scripts/md-link.py verdict
<app log>` now prints that verdict from any app log. **So the two-console failure
is not the link and not a link change; it is the same games' own session state
that ends every other two-console session here, and it remains open at that
level.** The earlier `melonAP` lead is withdrawn: the local `WifiAP` beacon is
delivered to the guest *after* its deauth and only while `!IsMP`, so it is a
consequence of leaving the room, not a trigger.

**The teardown is not a lost beacon sync, and that is now measured rather than
argued (v0.1.9bq).** The pass was asked to treat the stop as the link's job:
instrument both consoles' own wireless state across it and find the first
counter where the two sides diverge. They do not diverge. The emitter stamps its
own `USCounter` into every beacon's timestamp field (`ProcessTX` case 4), so the
host's `beacon out ... my USCOUNT` and the guest's `beacon accepted ... USCOUNT
before -> after` are the same counter, and on `logs/v9ah_*` and `logs/v9af_*`
they agree to the microsecond on the same beacon (last pair: emitter
`07259C58`, child's sync target `07259C58`). On the wall clock, pairing the two
consoles' `mp pN hw:` readings second by second: **the two `USCOUNT`s never
differ by more than 2.5 ms while both radios are on** (v9ah: min -1036 / max
+2524 us over 21 paired seconds; v9af: max 92 us), and the child's sync moves
are small backward corrections (median -464 us) as expected from re-stamping on
each beacon. The `beacon IRQ14 fired ... with no beacon slot armed` the previous
passes kept reading is a *child's* normal post-beacon wake: a console that
belongs to a group never arms a beacon slot, and its `USCOMPARE` lands ~1 us
after the beacon it just took (the reply window), so the IRQ firing is the schedule
working. **So the premise of "the guest loses that sync" is falsified by the data
and by construction** - there is no divergence for the link to fix, and the
first event remains the host's game changing its own command slot. Two
instruments are left behind: `--wsync <app log>` pairs both consoles' wireless
counters and names the first second they part (or says they hold), and `--verdict`
now reads the digest window that *brackets the stop* instead of the log's last
one - the last one is minutes into a dead lobby, and reading its `(+0)` deltas
called a window that lost a round "clean". With the right window the stop is
clean on both runs (`logs/v9ah`: 5699 collected / 5699 acks, empty 0, clientfail
0 in the dying second; the one `empty 1`/`clientfail 1` is the host's poll after
the guest had already deauthed, i.e. a consequence). What is still open is
unchanged: the host's game stops its session payload, the guest leaves ~4 s
later, both radios go off.

**The test surface is enumerated and run, and the teardown is MPH's, not the
link's (v0.1.9br, the table at the top of [compatibility.md](./compatibility.md)).**
Each ROM was run through the driver that owns it: **NSMB holds the whole run** -
234.0 emulated s, 11,127 rounds carrying a 262-byte payload body to the last one,
host `collected 11126 / empty 1 / clientfail 1 / stale 0 / reanchor-drop 0`, guest
`answers 11127`, `deauth 0/0` on both - on the same binary and link that MPH's
session dies on at ~78 s of `INGAME`. MKDS couples (`mask 0x3`) and reaches
CUT OFF, then lands on the game's own park (both screens one flat colour,
`pc9 0200FD10`) and never reaches a race. Star Fox Command and Super Mario 64 DS
boot and run at 2 consoles but raise no radio at all (both are Download-Play
only, Milestone 4), LEGO Star Wars II has no multiplayer mode, and the diag ROM
is `134 checks, 0 failures`. The inventory exposed **no substantiated link
defect**: MKDS's 27 lost rounds in 6182 are 1:1 with re-anchor drops and the
order was checked at an instance - `dropped 1, kept 1` is followed in the same
round by `collected a reply from p2 for aid 1 ... seq=3510` - so they are late
answers being cleaned up, not withheld ones. What is left is the games' own
session state, and it is now known to be *per game*.

**Run live, the stall reproduces on demand and its cause is the host's own game
(v0.1.9bk, `logs/v9i_3p_*` and `logs/v9j_3p_*`).** About two seconds after the
*third* console associates, the host's game stops attaching its 38-byte session
data and sends bare 6-byte polls only; both guests' own games then leave ~5 s
later (their own `reason 0003` deauths, radios off), with every poll answered and
every reply handed over to the last round. **The tap is not the trigger**, which is
what the live run settled: in v9j the data stops four seconds *before* the driver
touches the host at all, and the `[input] frame` lines put the taps on the frame
clock that says so. **And while two guests are attached the host's own lobby pages
stop advancing** - v9j's HUNTER step took 17 s and two delivered taps (the
hunter-icon probe moved 318 -> 430, so they landed) and stayed on HUNTER, where
the same step reached ROSTER in ~3 s once both guests were gone. What is now
excluded as the reason a step fails: the route (the ladder's two pre-roster steps
are walked before the joins, so the host is a page from its roster before the
guests are in) and the link (clean, both directions, `stale 0`/`empty 0`/
`notlooking 0`, no drop line). The open question is the host's game's own: does its
silence come *because* the guests' games do not follow it to the next page (the
guests' own screens read HUNTER with `pill=69 icons=333` while the host's HUNTER
reads `pill=8 icons=318` - two different screens), or does its transition ask for
something the guests never send?

**The per-console timeline is now measured, and it partly answers that question in
the guests' own favour (v0.1.9bl, `logs/v9k_3p_*`, `logs/v9m_3p_*`,
`logs/v9n_3p_*`).** `tools/scripts/lobby-track.py` takes ONE composite `shot` per
sample - the same instant for all three consoles - about once a second, classifies
each console's own column, and brackets each read with the three consoles' own
presented-frame counters, so a console whose *picture* stops changing while its
counters keep advancing is a game standing still, not an emulation that is. What
it shows: in the losing runs the guests' pages never reach a lobby page at all
(their columns are their pre-join menus, then their own join/wait screen, then the
post-mortem hunter screen), and the host's HUNTER->ROSTER step spent 16 page reads
un-advanced with both guests attached against 3 reads with them gone (v9m). The
wire puts the whole guest window at 5-6 s - p2 synced 16:26:07Z and wrote its own
`reason 0003` deauth at 16:26:12Z, p3 at 16:26:10Z and 16:26:16Z - so the route's
picks, which ran after the ladder, were 20 s late (`p2 LEFT while it was being
picked`). A guest's page can also look *live* after it has gone: p2's own column
read `HUNTER pill=0` at 11:26:15, three seconds after its deauth, so every page
read needs the bus bit beside it. **Two fixes came out of it, and one produced a
first.** `lobby_page.py` now tests ADD BOT before the hunter-disc band (a
bot-bearing roster measured `icons=164 addbot=94` and the old order called it
HUNTER; selftest **18 labelled checks, 0 wrong**), and `recover()` waits for a
rejoined guest's own hunter page before picking instead of reading once inside the
join transition. With that, the watch phase registered a guest **for the first
time on record**: the host's roster read `pill=414` at 11:29:28 and at 11:36:04
against a host parked on its ROSTER, and a hand-driven repeat after the driver's
timeout read it again. What still fails is the *first* association: v9n parked the
host on its roster before the joins (the head start now walks to ROSTER and the
picks run before the ladder) and the guests' own pictures still froze for ten
seconds with their frame counters advancing, their own deauths following before
either page showed HUNTER. The same run's watch phase registered p2 with the same
rejoin-pick (`pill=414` at 11:45:02, six seconds after the pick) and the
registration was gone by 11:45:39, when p2 had left the room again - so the
rejoin-pick registers, and what does not hold is the guest (or a first
association, which never reaches its pick page). A third console's return then
resets the earlier guest's registration (`pill=414` -> `pill=0` in the same second
its bit came back). Next, in the order the evidence argues for it: what a *first*
association lacks that a rejoin has, and how to keep a registered guest in the
room once its pick has landed.

**The link was telling the host's game that a console had failed while that console's answer was sitting in its own queue (v0.1.9bm), and that is now fixed and exercised.** The re-anchor a new command performs (`LocalMP::TrimAnswers`) drops a *prefix* of the answer queue to keep the newest `MD_REPLY_KEEP` (1) answer per console. A console whose US clock is offset answers a whole frame's worth of rounds in one clump; when that clump sat *behind* another console's single answer, the prefix took the single answer with it, and the host's game was handed the round as a failed client after the answer had arrived - which is the one thing this stack must never do. Two counters in the app's own log grow in lockstep because of it (`reanchor-dropped` and `acks clientfail`, ~5 and ~6 per 5 s in `logs/v9m_3p_app.log.gz`), and the same game code in the harness reads `clientfail 0` over 7986 rounds, so the difference was ours. The prefix now shrinks until every entry it takes is surplus for its *own* sender: a clump's leftover answer becomes the next round's answer instead of a loss, and `MD_REPLY_KEEP=0` (stock's policy) still takes everything. Deterministic evidence first - a rebuilt reference arm fails four checks in `md_smoke_test` (a console that answered once is never credited) against the fixed arm's **111 checks, 0 failures** - then live at three consoles (`logs/v9o_3p_*`, `logs/v9o_track.log`): **both guests reach their own pages on a first join** (MODE then HUNTER, p2 87 s / p3 46 s of samples; in v9k/v9m/v9n a first join left their columns with no lobby page at all), `mask 0x7` holds for 44 s + 24 s of the 300 s sample, the host's game sends **9,870 commands** through the lobby while they are attached, and its roster registers them (`pill=414` for 27 s, previously only reachable by a rejoin). The two-console case is unregressed: `logs/v9p_2p_*` reaches a live match at `mask 0x3` with `clientfail 0 / empty 0 / stale 0 / reanchor-dropped 0` and `deauth 0/0`. What is still open is the hold: the guests leave on their own `reason 0003` about four minutes in, after their picks have registered.

**The per-console link ledger is now sampled on the same frame clock as the pages, and it moved the three-console question onto the games' own join (v0.1.9bn).** `tools/scripts/lobby-track.py` prints, per sample, each console's own counters beside its page - `tx/ack(fail)`, the reply window `collected/empty/blank/stale/reanchor/not_looking`, what its own game wrote/answered (with blanks), arms and AID writes, deauths in/out with the last reason, and the last command and answer lengths - each stamped with that console's own frame counter (`probe` now carries the ones that were missing). What it shows across a deauth: the guest's first association is credited every round (last ack `clientfail 0000`, `redelivered 0`), and then the guest's own game clears its own wireless interrupt (`W_IE 0000`), powers off, and writes `reason 0003` - 6 s in v9r, 11 s in v9o, 5-6 s in v9m, 7-10 s in v9q - while the host's own loop keeps climbing (169 → 576 → 9,870 commands in v9o's run). **Correction to what this file said last pass: no first association has ever held at three consoles.** v9o's guests left seven seconds after their first association; the `HUNTER` columns it showed for 87 s are their frozen last frames (the post-mortem read trap v0.1.9bl identified), and its `pill=414` registrations came from the driver's re-joins. So the open question is why a *rejoin* holds for minutes where a *first* association dies in 5-11 s; the pre-answer failure rounds are present at the rejoin too (`+6` empty windows), which rules them out as the discriminator, and the answer has to come from what the guests' own games do differently the second time.

**Playtest (real UI through the control channel): one product defect worth fixing first.** The "Link test" button reports **FAILED** on a healthy bus, in two different states (a two-console MPH session sitting in its own menus with the radios off, and a four-console `diag.nds` session): every direction answers `sent:true, received:false` and the status line says `link selftest FAILED (1→2 no reply ...)`, while the app's own health lines for that same second show the frames crossing - `tx 3(+3, 48B) · rx 7..9(+7..9, 112..144B)` on every console. The send half works; the receive half (`LinkOp::Drain` → `md_link_recv_packet` → `LocalMP::RecvPacket`) returns nothing. **Fixed in v0.1.9bo, and the root cause was not the receive half at all: the button waited for a byte the wire rewrites.** Its test frame carried its nonce in the frame's *trailing four bytes* - the hardware's FCS slot, where `SendPacketGeneric` writes the 802.11 checksum into the frame (one implementation now, `LocalMP::ApplyWireFCS`, used by the sender and by the test's expectation alike) - so the last word of the arriving frame was always the checksum and the matcher could not be true in any state. The receive total is also attributed now, because the ambiguity was the reason this could stay unexplained: `MDLinkStats` v3 adds `packets_delivered` (the router's own ledger, counted at the hand-over in `LocalMP::Route`) beside `packets_read_game` / `packets_read_drained`, printed per health window - in the four-console state the line that used to read `rx 9(+9)` reads `[game 0(+0), drain 9] · delivered 9(+9)`, i.e. the traffic the failing test was contradicting came from the test's own drains. The button now arms an expectation on the receiver (`md_probe_arm/disarm/result`) and reports the ledger and the read separately (`seenBy` names the consumer), so "nothing read it" and "never arrived" are different verdicts. Two further defects surfaced and were fixed on the way: the Rust mirror of `MDLinkStats` had the new fields in the wrong place (found by the instrument, `observed:true, delivered:false` in the first run; now guarded at session start by `md_link_stats_canary`, which checks each field by name), and **the injection path used to power the consoles' radios on and off under a running game, which wedges it** - reproduced 2/2: after one Link test a 2-console MPH walk never raised a radio in three attempts over three minutes, against 17 s to radios-up and 24 s to a joined guest with no test in front of it; it is now a bus-only attach (`md_link_test_attach`, no hardware register, no mask bit), and one run gets test → walk → join → `START GAME` → `INGAME` with `deauth 0/0` up to that point. **Both named states now pass every direction with the payload intact** (2-console MPH radios-off, 4-console diag), and the games' own reads are untouched - during the live match the log reads `delivered == game + drain` frame for frame (p1 `rx 1081(+274) [game 1080(+274), drain 1] · delivered 1081(+274)`). Standing, unchanged by this pass: the 2-console match reached `INGAME` and the host's own game tore it down at t+15 s (`deauth-sent 2`, both radios off) - the same two-console failure this file records from v0.1.9bi, where the host writes its own broadcast deauth. Also: the players selector looks live while a session runs but only applies at the next Start. What passed: no-ROM and bad-path Starts both explain themselves, a second Start is refused with a reason, and Stop returns the UI to ready.

### The link's shape, as of v0.1.9bi

*What a later pass should build with rather than against.* `src/net/LocalMP.h` and
`LocalMP.cpp` are **per-console state plus one router**, and nothing else:

- `LocalMP::Console` holds everything the bus knows about one console: its
  `inbox` (the frames routed to it) and read cursor, its `frameSem`, `hostInst`
  (the console whose command it last read; -1 until it has read one), its own
  `answers` queue and `answerSem`, and its own `MPReplyDiag`.
- `LocalMP::Route` is the **single** place that decides what one console is told
  about another. A frame goes to every attached console except its sender (the
  air carries it to every station; which station *acts* on it is the wireless
  hardware's business and stays in `Wifi`, address filtering included). An
  answer goes to the console whose command its sender is following, which is the
  one routing decision only the bus can make; an answer from a console that is
  following nobody is dropped and named, never credited to a host that merely
  spoke last.
- `LocalMP::TrimAnswers` is the only policy that touches an answer queue: a new
  command keeps the newest `MD_REPLY_KEEP` answers *per sender*.
- **Both queues are bounded against their reader** (v0.1.9bi, `QueueAppend`):
  before an entry is written, the *oldest unread* entries are dropped one at a
  time until it fits, counted (`Console::frames_dropped`, readable through
  `LocalMP::GetInboxDrops`; an answer queue's drops land in the reply diag's
  `reanchor_dropped`) and said once on the log, then once per 1024. A console
  that stops reading (radio on, game in a menu, being written to faster than it
  drains) therefore loses the frames it was never going to read and keeps the
  newest ones **whole** - it is never handed a header out of the middle of an
  entry, and never has the whole queue dropped under it. A wakeup whose entry
  was dropped is answered as "nothing to read" rather than read from wherever
  the cursor points (both the inbox and the reply window).
- **What the two queues know is not what the games know**: the clients transmit
  no beacons at all in a lobby (they answer polls), so a question about "the
  host hearing its clients" has to be asked of the *room beacon* (>= 140 bytes,
  transmitted by the hosting console over the bus) and never of the 60-byte
  `fc=0080` frames - those are each console's own `melonAP` access-point beacon,
  delivered to that console's own game only while it is outside multiplayer and
  never on the bus (`tools/scripts/beacon-party.py` splits the two; the counts
  and the census behind this are in `logs/v9_mp-link-finding.txt`).
- The interface the frontend uses is fixed: `Begin`/`End`/`Reset`, the seven
  send/receive calls, `GetReplyDiag`, and the timeout and reply-budget setters,
  reached through the nine `MD_*` wrappers in
  `src/frontend/tauri/PlatformBridge.cpp` (579-699) on `md::link_bus()` +
  `md::slot_for(userdata)`.
- The reply path is checked now - the `reply path` block in
  `tools/coretest/smoke_test.cpp`, **106 checks in total** - and **a change to
  `LocalMP` has to keep them green**. The two-groups checks are the ones the old
  bus failed (94/97): an answer from a console following nobody, and two consoles
  hosting a client each. The two queue-bound checks (a reader that stops
  mid-session, and a burst of answers larger than the ring) read **4 failures
  without `QueueAppend`'s room-making line and 106/0 with it** - they are the
  A/B for that policy, so do not remove the line without them.

### The joiner's shape (what owns what on the Download Play path)

*What a later pass should build with rather than against.* The Download Play
client is four concerns with one owner each, and the data flows one way —
`dp_joiner.rs` speaks, `lib.rs` carries, `Bridge.cpp` boots, `Wifi` remembers:

- `src-tauri/src/dp_joiner.rs` owns the *protocol*: pure frame parse/build
  (no bus), and one `step()` that only runs five stages in order —
  `gate_step` (the single presence/speech decision: `StepGate::Idle/Hold/Run`),
  `drain_step` (read side only), `advance_transfer` (the transfer machine's one
  writer), `send_step` (the phase machine's answers), `maybe_boot` (boots on
  `Transfer::Closed`, which implies still-Associated). `State` holds the
  protocol phase, the `Image` assembler, and the boot flag together because one
  turn advances all three — but the *session* the boot carries is built in one
  place (`State::session`: channel + BSSID off the beacon, AID off the assoc).
- The retirement seam is `ClientSpeech` on `State` (`Speak`/`Silent`, default
  `Speak`, set in code — never a new `MD_*` knob): only `send_step` consults
  it, at one point. `Silent` keeps presence and tracking but sends nothing, and
  can never reach `Closed` (no reply collected, no boot) — that is the whole
  guest-owned-radio posture, pinned by `a_silent_client_listens_tracks_and_answers_nothing`.
  Flipping it today would kill the working download (the host only streams
  while this client answers), so it stays `Speak` until a real guest DP client
  exists to take over.
- `src-tauri/src/lib.rs` owns the *transport*: `DriverTurnBus` (direct, on the
  console's driver thread — the only path fast enough for the reply window) vs
  `InstanceJoinerBus` (relay from other threads). They look duplicated but are
  not: the relay adds caller tagging and the `link_ops` path, so do not merge
  them without preserving that difference.
- `NDS::SetupDownloadPlayBoot` (`src/NDS.cpp:439`) owns the *boot staging*
  (whole-console `Reset()`, then image, then `Start()`), and
  `Wifi::SetDownloadPlaySession` (`src/Wifi.cpp:3340`) owns the *association
  memory* (channel/BSSID/AID re-seed). Neither parses protocol and neither
  decides when to boot — they only carry what the joiner handed them.
- The suite that guards all of it is `cargo test --lib` (41 checks, including
  the two seam pins) plus `md_smoke_test` (162 checks) — a change on this path
  keeps both green.

#### Guest bring-up branch, named 2026-10-12 (live Star Fox route, no source change)

*The 004A write is game-time role-1 behavior, not a bring-up fork — do not try
to seed it.* Both consoles run the downloaded ARM7 wifi lib identically through
power-down (`W_PowerUS <- 0000 @027EA3E8`); the host's `W_ModeWEP <- 004A
@027EA55C` comes later, from ARM9 IPC command handler `027EE3C8`: it validates
cmd fields +0x10/+0x12/+0x14 (all <= 1, else return 5), calls `027EA51C` with
r0 = cmd.+0x14 (which latches it to wifi-struct +0x352), then with cmd.+0x10==1
fires `W_PowerForce <- 8001` via `027EA4DC`. `027EA51C` writes 004A only on its
NE path (arg != 0) *and* struct+0x32E == 1 — and +0x32E mirrors role (host
stores 1, client 2; live reads: host `00010001`, child `00000102`). Forcing the
child's latch to 1 and +0x32E to 1 live changed nothing, correctly: without the
child ARM9's own IPC command the branch cannot fire, and the next gates
(+0x4A4 in `027EAFF8`, +0x604 assoc bits in `027EA66C`'s client path) are all
RX-driven. The child's radio receives nothing (live count: host link-tx 19,835
frames, child link-rx 0; its wifi RX buffer reads back zeros) — the joiner
answers the host *for* the child (`Speak`), so the host streams while the
child's game waits forever for beacons/assoc-response in its own RX FIFO.
Next work is the guest DP client the seam section names: deliver host frames
into the child's RX FIFO (and its TX onto the link). Until then, struct seeds
are whack-a-mole — the branch above is the map of which fields matter
(+0x352 latch, +0x32E role, +0x4A4, +0x604) when RX flows again.

##### Radio-up at handoff, landed 2026-10-12 (`Wifi::SetDownloadPlayLink`, POWCNT bit)

*The boot posture half of the above, proven live on the same Star Fox route —
the game-flow half is still open (see the next paragraph).* `SetupDownloadPlayBoot`
now keeps the wifi power domain on (`PowerControl7 = 0x0003`; the direct-boot
path keeps `0x0001`) and runs the child's own radio bring-up prefix after the
session carry: RX window 4000-4800, `RXCnt` enable bit by direct store (never
`Write()`, whose bit-15 arm fires a TX), then `PowerDownCtrl <- 3`,
`ModeReset <- 1`, `PowerTX <- 1`, `PowerUS <- 0` through `Write()` so the power
model transitions genuinely (PDC bit 1 + the ModeReset-edge recompute is the
model's one power-on path: TRX up, PowerState bit 8, the -2048us delay, IRQ11;
the PowerUS write attaches the bus and starts the event that completes the
delay). Deliberately not carried: `W_ModeWEP` (driver-owned, overwritten),
the `W_PowerForce` pulse (transient, settles to nothing), `W_PowerState` (the
core drops the write unless `ModeWEP&7==3`). Live trace shows the dance
executing at the handoff (`pc7=02380004`) and `wifi: TRX power ON` on console 2
— the only TRX-ON the child ever logs. The driver's own bring-up then runs
identically with and without it (bulk-zero init `@027EBA6C`, own MAC
`@027EA0CC`, deliberate park `ModeReset <- 0 @027E9A44` + `PowerUS <- 1
@027EA4C0`), so the dance changes the starting posture, not the driver's
trajectory. Gates: `md_smoke_test` 162/0, `cargo test --lib` 41/0, both on the
built binary.

##### Why the child still hears nothing real (measured 2026-10-12, same route)

*Do not re-seed boot state for this — the wipe below is the driver's own init,
and the blocker is attach timing, not register values.* The driver's bulk-zero
wipes the carried window/BSSID/AID (BSSID/AID regs read back `0000`; core
`IsMP/MPMYAID/role` survive — the wipe is not the `ModeReset|0x4000` arm,
which would leave window defaults). Both CPUs then Halt-idle (ARM9
`0201CD34` loop = IRQ-disable + wait-for-interrupt; ARM7 in the BIOS delay
under its `037F84F8` retry loop, gated on halfword `[03808490]`, still 0 after
25+ minutes) while the lib power-cycles every few minutes (`PowerUS` 0/1 from
`027EA3E8/027EA4A8/027EA4C0`, `wifi on/off 4/3`). Restoring window + BSSID +
`ModeReset` + `PowerUS` live by poke (RXCnt bit 15 was already set by the
driver) produces ~8 deliveries/s — but every one is a `WifiAP` fake beacon
(`00:F0:77:77:77:77`, site-5 hold, no `p2 rx` bus trace) dropped at the beacon
BSSID filter (`RXFilter` bit 0 clear), because `MP_RecvPacket` returns nothing:
the bus slot (held begun by `StepGate::Hold`, `slots 0x2`) is detached at poll
time, and the attach windows are rare and brief against the host's ~2.4 s
post-transfer beacon interval (`game 495(+0)`, `delivered 21322(+0)`, zero `p2
rx bcast` in the whole post-boot period). The fake-AP fallback firing at all
post-boot is a red herring generator — note it before chasing RX counts again.
Specified next work: (a) trace what the `037F84F8` loop's power-up attempts
wait for (caller of the lib power-on at `lr7=037F94CC`) and why they are
minutes apart; (b) whether lengthening the attach window (Hold already keeps
the slot begun — the flicker is the *game's* `PowerUS`, not the bus) or the
driver's listen dwell can meet the beacon interval; (c) only then a sticky
BSSID/window heal for the `@027EBA6C` wipe — the poke proved the values hold
once set (driver never re-wipes), so the heal is real but not the blocker.

##### The join task's wait chain, traced live 2026-10-13 (pokes, fully reverted/harmless)

*The 84F8 loop is the DP-child join task and every one of its arms is gated;
opening the gates by poke advances both CPUs exactly one step per gate, and
each step reveals the next wait — so there is no single register to seed, and
puppeting the chain is not a fix.* All addresses are the downloaded ARM7 image
(`037Fxxxx`/`0380xxxx`) unless noted, all disassembled live from mem dumps:

- Arm 1 (`038070C4`): bails at `+0x38` unless `[0380BA7C]==0` (live: 1, static
over 90 s). Arm 2 (`0380417C`): bails at `+0x24` unless `[027FFC40]!=2` — and
2 is the FAITHFUL Download Play value (GBATEK BIOS RAM map: 1 normal, 2 Wi-Fi;
upstream `SetupDirectBoot` writes 1), so this closure is the game's correct
normal-join skip, not a bug; do not "fix" the indicator. Arm 3 (the
`[03808490]` halfword read at `037FE4E8`): live 0; safe to stimulate (the
`038072A0` target null-checks its arg first) — poking it to 1 moved ARM9
`0201CD34`->`02008B60` and ARM7 into `038070C4/0380417C` subtrees, both
advancing. The power-state machine (`037F9468`, requested state in r0,
current at struct+8: 0 parks via `027EA468`, 16 powers up via `027EA4DC` +
`037F93B0` + wake `027EA3D4`, 64 does the delay-100 path) is reachable only
past these arms, which is structurally why power-up attempts are minutes
apart: the loop body itself never powers up.
- Past arm 3 the flow is a cross-CPU handshake chain, each step poke-verified:
ARM7 `03807ED8` posts IPCSYNC `0x100` and spins for peer nibble 1 (core
semantics `src/NDS.cpp:4482` / `:3821`: bit 13 clear means NO IRQ is sent —
ARM9 must poll, and its divide/idle pacing loop never does); answering for
ARM9 (`poke cpu9 04000180 <- 0x100`, no IRQ side effects) advanced both CPUs.
ARM7 `03807EF4` then spins for nibble != 1 ("wait for peer to consume");
clearing it (`cpu9 ... <- 0`) advanced ARM7 into the `037FE408` loop (short
BIOS delay spin, `lr=037FE41C`), where it has sat since — the current
frontier, condition above it unnamed.
- WARNING from a misstep: `[03807F24]` holds the POINTER `0x04000180`, not a
flag — poking it to 1 clobbered the pointer and parked ARM7 on garbage;
restored to `04000180` immediately, session verified healthy after (both
consoles frame-advancing, host beaconing, no crash). Read-before-write with
clean parsing; a value that looks like an address usually is one.
- Decision: attach-timing, not guest-behavior. The chain advances
step-by-step whenever given its inputs, the indicator is faithful, and every
remaining gate is an external-progress wait (peer nibble, RX-driven flags) in
a system whose RX never fires. No code changed for this — landing
any poke (IPC auto-answer, flag presets) would be puppetry over an
unfinished chain.

##### Overlap mission FALSIFIED live 2026-10-13 — and the correction that replaces it

*Do not build dwell/overlap AMR for this; both endpoints are silent.* Fresh
cold-build route (`logs/sfc_overlap_route.log`, exit 0, BOOTED, gates
162/0 + 41/0 on the binary): (1) the "~2.4 s beacon interval" in the
2026-10-12 note is wrong — measured median post-transfer beacon gap is
**0.21 s** (n=20, min/max 211337/212472 us), so beacons were never scarce;
(2) after the transfer the HOST parks too: last wire frames are a 204-byte
beacon then a broadcast deauth (`fc=00C0`, t=116332960), then silence, host
`wifi on/off 3/3`, `LINK OFF`, both consoles' games Halt-idle — the host's
game is done serving and will not speak again no matter how long the child
listens; (3) `link mask 0x0` with beacons on the wire is consistent (mask is
the game-level MP state; the link-trace `t=` emulated-us ordering, not log
line order, is what proves all 516 p2-rx lines predate the boot — stderr/bridge
buffering makes line ranges meaningless for causality). The Hold fired twice
then legitimately stopped (bus bool still begun; game balanced 3/3). So there
is no overlap to lengthen: by the time any child-side dwell could act, the
beacon source is gone, and the child's remaining waits are for its OWN ARM9
(IPC nibbles), which host frames cannot supply.
*Specified next experiment (bounded, one route):* in the seconds between boot
and host deauth, restore window+BSSID+attach by poke (proven to produce
deliveries) and watch for one REAL beacon in p2 RX + wifi IRQ + ARM9 unblock.
If the IRQ cascades (RX→ARM9 task→IPC nibble→ARM7 join→radio up), the fix is
a boot-time radio+session heal extending `SetDownloadPlayLink`. If not, the
guest-behavior verdict stands and the child needs its own DP client drive.

##### Cascade result 2026-10-13: NO CASCADE — guest-behavior verdict (`logs/sfc_cascade2_*`)

*Scripted race (`tools/scripts/sfc-cascade-race.sh`: poll BOOTED every 2 s,
restore within seconds) caught the window — and the cascade still fails at
step one, with the radio verifiably up.* Fresh cold-build route, BOOTED,
restore applied ~5 s after the flag (window 4800/4000, BSSID 00:09:BF:11:22:33,
ModeReset, PowerUS — all verified held afterwards; the driver never re-wipes).
Post-boot host beacons spanned t=116.1M→117.6M (~8 beacons) before the
broadcast deauth: **zero `p2 rx fc=0080` in the whole log** (the +2423 rxtrace
burst was transfer-tail cmd frames, confirmed by fc census + t-range — another
line-count trap). `TRX power ON` fired on p2 (5 total incl. restore dance), so
the radio came up and still heard nothing; IRQ6 raised +0/acked +0, `W_IF 0000`,
both CPUs back to idle pacing. With window+BSSID+TRX+attach all simultaneously
true during live beacons and zero RX, the block is inside `CheckRX`'s remaining
guards (game re-parks PowerState within ms of every attempt — its state machine
sits in state 0 — and its PowerUS cycles End the bus slot at beacon moments)
*and* inside the guest (ARM7 waits ARM9's IPC nibble; ARM9's setter task never
runs — likely itself RX-event-driven, the chicken-and-egg). No bus-side,
register-side, or timing intervention can deliver a beacon into this shape:
**the child needs its own DP client drive (guest code that joins), not more
host-side or boot-side posture.** `SetDownloadPlayLink` stays as the posture
fix it is; extending it further is whack-a-mole per the 2026-10-12 finding.
Do not re-run this experiment without a new mechanism — the next pass that
touches this path should be the guest-drive design, not another overlap attempt.

##### Context-block hypothesis FALSIFIED 2026-10-13: the guest never reads it

*Do not populate `27FFC42h+` — verified live before writing a line.* The
brainstorm's GBATEK layout is exact (researcher-web table: beacon size/RSSI at
+00/+02, BSSID +04, SSID size +0A, serial +0C, stream +10, caps/rates/interval/
listen/channel +2C–+36), and the plumbing audit showed the beacon parse already
holds nearly every field — but a 20K-word literal scan of the LIVE booted child
(16 KB ARM7 across the join-task regions + 16 KB ARM9 around every post-boot
PC) finds **zero references to any address in `027FFC42–027FFC7D`** on either
CPU. What the guest does read: `027FFC40` (indicator — ARM7 `03804244`, ARM9
`0201CAE8`/`02023AE4`/`02023E60`) and `027FFC30` (ARM7 `03806F64`/`03806FC4`/
`03807608`). This downloaded image never consults the firmware handoff context;
its join path takes BSSID/channel from nowhere in shared RAM — consistent with
the traced shape (internal flags + IPC waits, no scan). The reason-3 deauth
was not implemented either (nothing to hand off *to*). Session evidence kept
(`logs/sfc_ctx_*`); no source changed, correctly. Remaining guest-drive
designs must target the ACTUAL join inputs — the `[03808490]`-style flags and
the ARM9 IPC answer — not the firmware context.

##### Setter PCs named + firing event observed-absent, live 2026-10-13 (`logs/sfc_setter_*`)

*memwatch cannot trace either target (main-RAM stores only — confirmed in
`NDS::RecordMemWatch`; WRAM flag and IO-reg nibble are both outside it), so the
22K-word static literal scan is the instrument, and it names the full chain:*
ARM9 handshake pump `0201D044` (r3=IPCSYNC ptr, writes own nibble via
`strh [r3]=lr<<8`, bounded `ip>4` exit, spins on peer nibble with countdown) —
its pool holds `04000180` at `0201D0C8`; ARM9 waiter `0201D0CC` (spins on
`[0x02176274]==1`); ARM7 poster `03807ED8` (writes `0x100`, spins for peer
nibble 1 — the poke-answered step); ARM7 consumer `03807EF4` (spins for
nibble != 1); flag reader `037FE4E8` on `[03808490]` (only refs in the image
are its own pool — the WRITER uses a computed address or lives outside the
scanned 16 KB; unnamed, honest gap). *Firing event: observed absent.* pctrace
windows over the pump and the poster record **0 visits over ~7,000 frames**:
neither task runs in steady state — the chain is not stuck mid-handshake, it
is gated two levels up (`[0380BA7C]==1`, indicator-2 skip) and nothing
re-triggers it. Adversarial review of the landed diffs (same pass): ordering
sound (power→session→link→Start), savestate exclusion of holds deliberate and
documented, Hold re-Begin harmless (game off, nothing queued), RXCnt store
redundant-but-harmless. No source changed.

##### Firmware stand-in implemented 2026-10-13, live validation blocked (`logs/pass_*`, `src-tauri/src/dp_joiner.rs`)

*Re-envision of the weakest part: the post-boot Hold's total silence.* The
Hold answers nothing after the boot, so the host deauthenticates the silent
client ~1.5 s post-boot (beacon, broadcast deauth, then silence) and never
speaks again — and the child's remaining waits are all for external progress
(peer nibbles, RX-driven flags) that can now never arrive. Worse, the cascade
route proved beacons alone cannot unblock anything: the beacon path in
`FinishRX` (`rxflags 0x8001`, `src/Wifi.cpp:2543`) only syncs USCOUNT, never
`StartRX`, never an IRQ — so only a CMD frame can fire the ARM9 setter task,
and CMDs stop when the host parks. The rebuild: past the boot, while the
game's radio is OFF the joiner drains and answers the host's polls as the
firmware stand-in (same drain/track/send, minus the boot, as the AID the host
assigned); the moment the radio is up it goes silent (Hold — the game owns
those frames, and while off-radio the game reads nothing so there is nothing
to steal). A post-boot deauth no longer tears the stand-in down (counted,
otherwise ignored — it is the host's words about the game's session, and
tearing down would be the silence that draws the next deauth); `send_step`
never re-authenticates past the boot. Rust-only, no new reply logic, no new
knob. Pins: `a_booted_client_is_held_on_the_bus_and_answered_for` (re-specified
from the old never-answered Hold pin), `a_booted_client_answers_polls_until_
its_game_owns_the_radio`, `a_post_boot_deauth_does_not_tear_the_stand_in_down`.
Gates on the built tree: `md_smoke_test` 162/0, `cargo test --lib` 43/0.
*Live validation did not happen: three consecutive routes never completed the
transfer* (host Dummy-forever at ~165/s, 16% collection, RSA stated once or
never, 0 data packets over 5-10 min each; an extra host tap mid-park changed
nothing). This is NOT the new code: every new path requires `booted=true` and
all three runs stayed `not booted`, so the executed pre-boot stream is
identical to the old binary by construction. Visible correlate: the parked
runs needed 4 route taps to reach role=1, the last good transfer (same day,
`logs/sfc_setter_route.log`: RSA→2502 packets→Closed→boot in 75 s) needed 3 —
the 4th tap likely over-advances the host's Recruitment menu out of its
serving state, leaving its DP machine idling on Dummy (mode 14, never 6).
Specified next: fix/verify the host route (fewer taps / confirm the serving
screen, e.g. `shot` before walking) and re-run; only then can the stand-in's
retrigger visits (`0201D044`/`03807ED8` pctrace) and first child RX be judged.
Do NOT "fix" the stand-in or the reply path for the park — the client answers
and the host collects; the host's mode machine is game code waiting on its own
menu state.

##### Live verdict 2026-10-13: stand-in PROVEN, guest still gated (`logs/p2_*`, `logs/md_app.log`)

*The three parks were a wrong log path, not the link.* `sfc-dp-host.sh`'s
beacon verdict greps `APP_LOG`, which defaulted to `logs/sfc_dp_app.log`
(stale/nonexistent) instead of the real `logs/md_app.log` — so it always
FAILed before Cut Off and Cut Off was never tapped (the host idles on Dummy
until its own player taps it; RSA-then-Dummy is its waiting state, not mode
14). With `APP_LOG="$PWD/logs/md_app.log"`: 3 taps, PASS, client registered
instantly, Cut Off → first `0x04` within 1 s, full image in ~75 s, BOOTED with
the session carried (ch 7, BSSID 00:09:BF:11:22:33, AID 1). The 3-vs-4-tap
theory is dead; do not chase it.
*Stand-in, judged live by counters: working as designed.* Holds #1/#2 fire as
the game's power cycles detach; 72k commands drained and ~13k replies
collected post-boot; host polls 25+ min with ZERO deauths either direction
(vs broadcast deauth at +1.5 s pre-stand-in); silent whenever the game owns
the radio; session healthy throughout (clean quit, no strays).
*Guest re-trigger: judged absent, bounded precisely.* Pump `0201D040-50`
0 visits over multiple armed windows; ARM9 sits in its `0201C7FC` self-loop,
ARM7 BIOS-idle, game `cmds tx 0`, `arms 0`. Game-side `cmds rx` froze at 249
(+80 burst at boot, then +0 for 20+ min — all dup seqno) while the bus-level
stand-in kept collecting: the game hears nothing new.
*Register-heal poke: insufficient, honestly falsified.* BSSID regs were zero
(`04800020/24`), RX window empty (`04800050/52` = 0/0), RXFilter zero —
restored live to host BSSID + window 4000-4800 (writes verified by readback,
plain IOPORT stores, no side effects) and watched 75 s: `cmds rx` still +0.
The block is above the registers — the power-state machine sitting in state 0
parks TRX within ms (CheckRX's bit-9 exit fires first), and only the guest's
own join progress advances it. A sticky BSSID/window heal in core is therefore
NOT the next step; puppeting PowerState would be puppetry over the same
unfinished chain. The affirmative mechanism remains the guest-drive design
pass. No source changed this pass (probing + route usage only).

##### Join loop decoded + writer named, live 2026-10-13 (`logs/p3_*`, `/tmp/p3dump` scratch)

*The 0201C7FC 'self-loop' is a divide epilogue, not a wait* (disassembled
from the live dump: ldr-halfwords + orr-combine + `bx lr` 64-bit divide
return). ARM9 cycles C7FC (divide work) ↔ CD34/AC00 halt-spins; AC00 keeps
IRQs on (`bl C9C4` is not the DisableIRQ of C9D8) so VBlank-driven work
between parks explains the cycling — the ARM9 is healthy-idle, not stuck.
*The live ARM7 join loop is fully decoded* (`037F84F8`: delay → `037FE4E8`
reads `[03808490]` → if set `038072A0` → arm1 `038070C4` (bails, BA7C==1)
→ arm2 `0380417C` (bails, indicator==2) → repeat). *The `[03808490]` writer
is named*: image `0238665C` writes halfword 1 on event-code match
(`(r1>>8)&0x7F==0x10`); only 2 pool refs in the 164 KB image, none relocated
to WRAM (full WRAM scan: only the reader's pools at `037FE4E4/F4`) — and the
image copy shows 0 pctrace visits (instrument since trusted: the live loop
reads back 36 visits). `02386B9C` (the pump-loop's gate) drains ARM9→ARM7
IPCSYNC (`04000184` bit14/batch protocol), returning 0 only when idle.
*Pump caller hunt*: no BL-to-`0201D044` in `02010000-02030000` — the pump is
pointer-dispatched (task system), not directly called; raw-pointer table scan
still open. *Register-heal poke follow-up*: post-poke game `cmds rx` crept
249→257 (all dup seqno) — the pipe is open but the host re-sends one Dummy
round forever, which the stack dedupes (`dup 256`); a NOVEL command is what
would schedule the pump task, and only host-lobby progress (driven match)
can emit one. That, or the task-table scan, is the next live session.
Session held BOOTED to a clean quit (20,932 replies collected).

##### Host-driving fails closed: the host waits on guest data 2026-10-13 (`logs/p4_*`, `logs/md_app.log`)

*Blind taps are inert.* Post-transfer host dialog (message box + decorative
divider, no button in y140-192 — taps at (64,165)/(192,165) changed nothing:
class stayed `0x00`, guest `cmds rx` frozen). Screen drifts while idle
(animated background), so digest-gating is useless here; class + dup counters
are the instruments.
*Register pokes replicated*: BSSID (full `00:09:BF:11:22:33`, verified by
readback — an earlier partial poke missing BSSID2 taught the readback habit)
+ window 4000-4800, still +0 novel RX. *Starvation hypothesis dead*: there are
no novel rounds to starve — host re-sends one Dummy (`dup 256`, seq static)
because ITS lobby waits: 340k window reads, 11.9k collections (our bodies),
49k empties, 48.8k reanchor-dropped surplus, yet class never leaves `0x00`.
The host's game reads reply BODIES and finds no player data (nickname/picks
only the guest's game can give) — so no host tap can ever advance it. The egg
is now fully mapped at game-protocol level: host waits on guest data ← guest
waits on flag ← flag waits on unnamed writer caller ← pump waits on scheduler
← scheduler waits on novel RX. *Trace-fidelity caveat*: pctrace appears lossy
(live loop shows 84FC/8518/851C but never the 8514 `bl` between them), so '0
visits' alone proves nothing — every no-run claim here is paired with
ground-truth state (flag==0, arms==0, `cmds tx 0`). Next: raw-pointer scan
for `0201D044` (task table) or baiting the scheduler — not more host taps,
not more register pokes. No source changed (probing only).

##### Power dance opens game-core RX; host patience is finite 2026-10-13 (`logs/p5_*`, `logs/md_app.log`)

*Mid-run power dance works through the poke path* (ARM7 writes hit
`Wifi::Write`: `PowerDownCtrl` was already 3, wrote `ModeReset<-1`
(keeping `ModeWEP=0xA`), `PowerTX<-1`, `PowerUS<-0`; model answered `TRX
power ON`, `PowerState 0x0200->0`). *Game-core RX exploded*: `cmds rx`
243→1385 (+824, mostly novel) in 75 s, later 1898; pc9 left its idle cycle
for `0200859C` (RX-task area). *But the game never consumes*: `consumed`
only ever the boot burst, flag 0, `cmds tx 0`, pump 0 visits — 1600 novel
frames accepted by the core and ignored by the scheduler. Register-only
bait (BSSID+window+AID looped 15×) stays at 2 beacon-accepts: CheckRX's
bit-9 exit fires first while parked, so regs without power are nothing.
*Host patience is finite*: after ~20 min of dataless collected answers the
host game kicked the client itself (`deauth in 1, reason 0001`, console
`[not in MP]`, role 0) — 20k collected bodies buy time, not membership.
Stand-in's deauth-guard held (joiner Associated to clean quit). Bounds for
the next pass: the guest must produce player data within ~20 min of boot;
the scheduler advance is the whole game now — bait RX *consumption*, not
reception (window cursors/read path while hot), or find the task table.
Background `nohup &` processes do NOT survive tool-call return in this
environment (scan + bait loops both reaped silent) — run long loops in
foreground SYNC calls. Session quit clean; no source changed.

##### Consumption-baiting blocked: radio off, park is terminal 2026-10-13 (`logs/md_app.log`)

*No-dance-needed premise failed.* This session the guest game powered its own
radio DOWN mid-run (`W_ModeReset <- 0000` at `pc7=027E9A44` — the table
loader history.md v0.1.9dl already named — then BSSID zeroed, `TRX power
OFF`), so there was no hot radio to bait consumption on. Re-dance (4x IF
toggle, the p5 recipe) produced ZERO new `TRX power ON` transitions; restoring
`W_ModeWEP` (a 32-bit poke to `0x04` had clobbered the game's own `000A` to
`0000` — halfword neighbours are not safe, read the word back) to `000A` +
`ModeReset=1` still produced nothing. Isolated pokes cannot replay the
`PowerForce 8001/8000 + PowerTX 0003 + PowerState 0002` ladder the host's
driver runs; the p5 heat needed the game mid-bring-up-cycle, and this time it
was parked. *Park is terminal, not periodic*: 5-min foreground watch showed
power-reg lines frozen at 219, beacons at 1, mask `0x1`, pc9 pinned at
`0201CD34` / pc7 at BIOS idle `0000115C` — the bring-up does NOT re-run. Guest
digest `cmds rx 248 (+0, dup 247)`, 248 canned, 0 own; host `cmds rx 0`,
`answered 0`, 609k window reads, still patient (no kick by ~20 min this run).
*Lesson for the next pass*: consumption-baiting needs the game listening
(`IE!=0`, window armed) — while parked it reads zero wifi regs, so cursor
bait is moot. The live instrument with signal is the ARM9 branch the park
decides on (`0201CD34` caller chain / what `0x02087350` tests, history.md
v0.1.9dk leads) or catching a session where the game is still cycling and
heating THAT. No source changed; session quit clean through the channel.

##### ARM9 park named: two spinning loops + IRQ-guard callee; radio catch falsified 2026-10-13 (`logs/md_app.log`, `logs/p7_host.log`)

*Cold build + gates green*: cmake cfg/build exit 0, smoke 162/0 exit 0,
cargo release exit 0. Route exit 0 (`TRANSFER STARTED`), BOOTED 2502/2502.
*Park branch named (Star Fox child ARM9, all live-verified)*: loop at
`0201AC00` (`bl 201C9C4`; tight `AC0C: bl -> setter / AC10: b AC0C`) drives
the Thumb setter family (`0201CD2C`: store literal struct-ptr, write 0 to
`*ptr`, `bl 0208901C`, `bl 0203D5B8`) — probes catch `pc9=0201CD34` inside
it; sibling processing loop at `0201C7E0` (`bl 201C9C4`, bit-field combine)
holds `pc9=0201C7FC`. *Shared-callee hypothesis killed*: `0201C9EC` is
NitroSDK `OS_DisableIRQ` (CPSR `bic #0x80`, return old state), not a poll —
both loops just guard critical sections; nothing is tested there. *What
`02087350` tests*: nothing wifi — frame update routine (`sp+0xA8` buffer,
`bl 202EAA4/202EAE0`), confirming history. *Radio catch falsified*: post-boot
the game cycled bring-up twice more (full table-loader prefix both times) but
no cycle ever contains `ModeReset=1`/`PowerForce`, so TRX never re-arms
(beacons stuck at 1); 4x IF-toggle dance reproduced 0 new `TRX power ON`.
pctrace on `0201C000-0201E000` shows the loops SPINNING (+3168 visits/2 min,
regions C000+D000) — the game is alive, polling nothing, with `IE=0`.
Guest digest `cmds rx 254 (+0, dup 253)`; host never kicked this run.
*Procedural*: `pctrace --off` reports nothing — pull the path BEFORE
disarming. *Open oddity (not chased)*: the `AC0C` call is a plain ARM `bl`
(`0xEB000845`) targeting the Thumb setter's literal pool (`0x020AC270`) —
live execution provably lands in the setter, mechanism unnamed. *Next*: the
decision is no longer in wifi regs or ARM9 loops — it is the ARM7 state
machine's input (IPC from ARM9? a status bit?) between last `PowerUS<-0001`
and the next cycle start. No source changed; quit clean; no strays.

##### ARM7 dispatch input named to an address; mailbox poke falsified 2026-10-13 (`logs/md_app.log`, `logs/p8_host.log`)

*Review first*: sampled the worktree diff for the classic defects — Wifi
`Reset()` zeroes every new diagnostic ring/counter (`MPLastDelivered`, power
budgets), `DoSavestate` deliberately excludes only microsecond-transient
deadlines (documented, cleared on load), `StartRX`'s stamp-0 guard makes the
ring's zero-init safe, LocalMP ctor/dtor memset + range-guard (`inst` bounds
checked on attach/detach paths). No defect found; the cold build (cfg/build
exit 0), smoke (162/0) and cargo release (exit 0) are the end-to-end proof
(the reviewed tree compiles and the live route below runs it). Scope limit:
11k uncommitted lines across 28 files were NOT audited line-by-line — said
plainly so the next pass doesn't assume it.
*State machine fully mapped (Star Fox child ARM7, live disassembly)*:
`037F9468(newstate)` compares `[[0x0380FFF4]]+0x344+8`, exit-hooks state 64,
switches 0→park (`27EA468`), 16→power-up (`27EA4DC+37F93B0+27EA3D4`),
64→`[r4,#12]==2?27EA2E8` + `27E96F8(100)`, always commits. *TEN direct
callers* by `bl`-scan: WRAM `9184:→64, 9238/92B0/92D8/93CC:→32`, main-RAM
`ED7A0:→32` (jump-table case), `BFDC:→r4` (forwarded arg), `DE60:→48`
(guarded by `cmp #2000/#10` threshold reads), `EE074/EE114:→32`. SM(16)
PROVEN executed on both consoles (write `pc7=027EA3E8 lr7=037F94CC`). The
host ladder's steps are issued by the handlers directly (`strh` with
`pc7=92D8…`, `lr7=037F8FAC` → dispatcher `37FB610`). *Dispatcher found*:
`037FB610` waits (`bl 37FE67C`), polls halfword `[[struct]]+0x300+0x50`,
jump-tables on 0..3 (`B658`). *Live comparison*: child mailbox `0380B66C`
`= 0x00000002` (case 2) vs host `= 0x00010001` (case 1) — the input differs
on the wire. *Causal test, falsified*: poked child mailbox `2→1`, watched
60 s — zero new power writes (219→219), zero beacons, no TRX ON; restored
to 2 afterwards. So the mailbox is symptom as much as input: the value sits
unread (ARM7 idles in BIOS, the poll loop itself stalled — consistent with
`IE=0`, no wifi IRQ ever waking it). The producer (IRQ handler? timer? ARM9
IPC?) is the true gate and stays unnamed. `027EA51C` (004A step) arg-paths
converge — not the gate; `27E96F8` is timer math — not the gate. Guest digest
`cmds rx 249` all-dup; host never kicked. No source changed; quit clean.

##### Mailbox producer triaged: all three recurring hypotheses falsified 2026-10-13 (`logs/md_app.log`, `logs/p9_host.log`)

*Cold build + gates green* (cfg/build 0, smoke 162/0, cargo 0), route exit 0,
BOOTED 2502/2502. *IRQ plane mapped*: dispatcher `037FB8F4` (installed by the
boot stub at `02380000` into `[0380FFFC]`; reads IME/IE/IF, acks, vector-jumps
via table at `[03808138]`), wifi handler `037F9504` (IF&IE bit dispatch to
`37FA528…27EBC04`, exit marks `[0380FFF8]|=0x1000000`), and the `0x8000`-bit
handler `37F95E8` (couples SM state: always `[struct+0x354]=1`, counter-gated
`bl 37F85F8(0/1,13)`). *Event slots*: `37F85F8(r,ev)` allocates slot `ev`
(11/13 seen) in the table at `struct+0xBC`. ARM7 `IE7=0x0104009D` (VBlank,
timers, DMA2, wifi armed), `IF7=0` — armed but unfiring. *All three producer
hypotheses falsified as recurring writers*: IRQ (child `W_IE=0`, wifi handler
can never fire); timer (mailbox frozen at 2 across 3 min AND the p8 poke to 1
was never overwritten — no periodic writer exists); ARM9 (loops spin per
pctrace yet mailbox frozen). Dispatcher writes a wifi reg (`04808050`), not
the mailbox; SM writes `+0x34C`, not `+0x350`. *Verdict*: the producer is
ONE-SHOT boot code (wrote during f3542-3573, silent since — wifi ring tail
stuck at f3573 though emulation reached f5459; driver region 0 pctrace visits
in 3 min). Exact store PC still open — next pass must arm the ARM7 pctrace
BEFORE the transfer starts and catch the boot window. No source changed;
quit clean; no strays.

##### Producer NAMED: event-post at 027EB304 writes mailbox=event; instrument landed 2026-10-13 (`logs/md_app.log`, `logs/p10c_host.log`)

*Why an instrument was needed*: memwatch only hooked main-RAM writes, so the
pre-armed watch on the ARM7-WRAM mailbox could never fire — verified by reading
`ARM7Write8/16/32` (no hook in any `0x03800000` case) and `RecordMemWatch`'s
fold. *Change* (`src/NDS.cpp`, `src/NDS.h`, `tools/coretest/smoke_test.cpp`):
the same one-compare hook in all three ARM7-WRAM write cases, doc updated,
plus 4 smoke checks (cpu-7 write into the canonical window is named at its
folded address with its value). Guards: disarmed = one predictable compare;
first-64-hits kept; interpreter-only (JIT fast path bypasses — session ran
`MD_NOJIT=1`, `jit=false` in the log). *Validation*: build 0, smoke 166/0
(+4 new), cargo 0, route exit 0 + BOOTED under the hooked interpreter build.
*Trap result* (watch armed pre-transfer, 12 overlapping stores): boot stub
zeroes it (`0238002C`), firmware copies the struct pointer over it, driver
init zeroes both halfwords (`037FE658`), then **the producer, twice**:
`strh r0,[r1,#80]` at **`027EB304`** (watch reports R15 `027EB30C`, insn+8)
in event-post `027EB2D8`, `lr=027EA960`, writing `mailbox=(old&0xFFF8)|event`
(mask literal `0000FFF8` at `027EB358`, ptr from `04808006` at `027EB354`,
`event>3→5` clamp at entry). Caller `027EA95C` posts event 2
(`mov r0,#2; bl 027EB2D8`). Upper halfword zeroed after each post by
`027EA53C`. *Live proof toward power-up*: this run shows TRX power ON ×1,
beacon accepted ×1, mailbox `=2` — the event-2 post demonstrably precedes the
dispatcher case-2 path into the bring-up that powers the radio (SM(16) proven
last pass). What event 2 *means* to the dispatcher, and who calls the poster
with other events, is the next read — the gate is now a named function, not
a dark address. Hook kept: it is the trap, with tests.

##### Event-2 handler named: dispatcher case-2 programs the wake timers; review clean 2026-10-13 (`logs/md_app.log`, `logs/p11_host.log`)

*Review*: hooks single-placed and guarded; `RecordMemWatch` bounds correct
(inclusive window, no u32 wrap — canon ≤ 0x023FFFFF); fold collisions carry
the cpu tag in the report (`arm7` vs `arm9`); DMA routes through the hooked
`ARM7/9Write32` (`src/DMA.cpp:589,665`) so only JIT fast-path and
`Reset`-memset bypass (both documented). No fix needed; build 0, smoke
166/0, cargo 0 re-verified on the unchanged tree before the live run.
*Handler* (child ARM7 `037FB610` dispatcher, fully disassembled live):
`[struct+0x350]` jump-table at `037FB658` — case 2 (event 2) enters at
**`037FB678`** with `r4=0x794` (literal at `037FB738`; cases 0/1/3 take
`0xBFC`-arm/`04808030`/`000010C4`) into the common tail at **`037FB68C`**,
which programs the wake path: `[04804000]=r4+0xBFC=0x1390`,
`[04808056/5A]=0xF280`, `[struct+0x344+0x9A]=0x1F60-r4=0x17CC`,
`[04808062]=0x5F5E`, `[04808030]=0x8001`, `[struct+0x300]=0x8001` (all
literals resolved live at `037FB740-78`). These feed the `0x8000`-bit
handler (`37F95E8`) whose counter gates the SM(16) power-up call.
*Producer re-trapped* (watch armed pre-transfer, interpreter): `027EB30C`
value-2 stores ×2 again — reproducible across sessions. *Live proof toward
power-up this run*: TRX power ON ×1, beacons accepted ×2, mailbox `=2`,
SM(16) executed (prior pass); residue `[04808030]=0x8000` matches
fired-and-acknowledged (bit 0 consumed). Later phases overwrote the other
timer values (bus-test patterns), and both trace rings (pctrace 262k,
wifiio 192) had rolled past the boot by read time — so thesame-boot *visit* of `037FB678` is the one unclosed link; recipe for it is arming
BOTH memwatch and `pctrace --cpu 7 037FB610 037FB720` pre-transfer next
run. Guest digest `cmds rx 234` all-dup, 0 own — game frames still not
advancing; stated, not dressed. No source changed this pass; quit clean.

##### Dispatcher NEVER invoked: zero visits across the whole boot 2026-10-13 (`logs/md_app.log`, `logs/p12b_host.log`)

*Dual-arm done right*: memwatch + `pctrace --cpu 7 037FB610..037FB720` armed
pre-transfer (frame ~2675), route exit 0, BOOTED, then **bare reads only** —
a plain `pctrace lo hi` call RE-ARMS and wipes (first session's boot evidence
was destroyed exactly this way; the 0-since-re-arm poll then proved post-boot
silence, and the power log showed that run's TRX ON was boot-stub-only:
single `ModeReset<-0001` at `pc7=02380004`, no `004A`/`PowerForce-8001`).
*Result, second session*: trace accumulated live frames 5091→12662 with
**0 visits, 0 regions** — no roll, no miss: the dispatcher never executed.
Meanwhile the producer fired (`027EB30C` value-2 ×2, mailbox `=2`), both CPUs
parked (`pc9=0201CD34`, `pc7=BIOS 0000115C`), TRX ON ×1 (stub), beacons ×1,
guest `cmds rx 246` all-dup. *Correction to last pass*: the event-2 handler
does not run-and-park — it never runs. The posted event sits unread; the
consumer (whatever calls `037FB610`) is the gate, and it is NOT in the IRQ
vector table (`03808138` holds `037FBA6C…037F9504…`, no `037FB610`) — so the
dispatcher is main-loop/task-called, and the task never wakes (ARM7 sleeps
with wifi `IE=0`, the circular stall). *Next*: what wakes the ARM7 task —
IPC word from ARM9 (`IPCTrace`/`IPCSent` counters exist for exactly this),
or a timer both CPUs share. No source changed; quit clean; no strays.

##### Waker chain named; answer is NEITHER live IPC nor timer 2026-10-13 (`logs/md_app.log`, `logs/p13_host.log`)

*Cold build + gates green* (cfg/build 0, smoke 166/0, cargo 0), interpreter
route exit 0, BOOTED. IPC rides in the wifiio report — no new instrument.
*ARM9 side*: child ARM9 pushed exactly **5 words** ever (`@0201CF14`, the
generic IRQ-guarded IPC-send helper `str r2,[04000188]` at `0201CF04`), then
parked at `0201CD34`; child ARM7→ARM9 **592 words** (all `@037FEA78`) pile up
with one still sitting unread at `04100000`. Host, for scale: 7946 words
pumped from one pusher. *ARM7 side*: `IF=0000 IE=0000` (host: `IE=703F`,
`IF=88C2`, thousands acked) — no interrupt of any kind can wake the child;
IRQ6 raised 488×, acked once, never serviced. The host's only `W_IE` writer
is `strh @037F908C` (R15 `037F9088`, `[04808012]=0x703F` — the `0x7FFF`
mirror of `W_IE`); the child's only writer is the zeroing loader.
*Chain*: main-loop `037F8EF4` → dispatcher call `037F8FA8` → dispatcher
`037FB610` → case-2 `037FB678` → timers → SM(16). Who calls `037F8EF4`: no
ARM-`bl` in ~9 KB scanned (driver + main-RAM windows), not in the IRQ vector
table — function-pointer/task-table suspected (the `0201D044` lead from p4
is back on the table). *Verdict for the decider*: the waker is the task loop
itself and it never starts — neither IPC words (5, then silence) nor timers
(IE=0) are live candidates. Next is the task-table/pointer scan for
`037F8EF4`, or the ARM9→ARM7 boot-handshake word the main loop waits on.
Guest digest `cmds rx 244` all-dup. No source changed; quit clean.

##### SFC guest park named to the instruction; flag-poke wakes both CPUs 2026-10-13 (`logs/md_app.log`, `logs/p15_host.log`, `logs/p15_sfc.log`)

*Cold build + gates green* (build 0, smoke 166/0, cargo 0), SFC 2-console
route: driver exit 0 (host role + `0x04` streamed), download 1260340/1260340
bytes, BOOTED. All below is the Star Fox child (different game, different
park from the MKDS p13 entry above).
*Park*: ARM9 `0201CD34` = `{push lr; bl 0201C9D8 (irq-disable); bl 0201CD28
(mov r0,#0; mcr wait-for-interrupt); b loop}`, `lr 0201AC10`; ARM7 in BIOS
`0000115C` with `lr 037F84FC`, `sp 0380FB70`. Stack names the path:
`037FC44C` (after `bl 037FC320`) below, `037F84FC` (after `bl 037F852C`)
on top. `037F852C` is an indirect wait call (`ldr ip,[pc]; bx ip` through
`*037F8534`) into a Thumb stub `@038043E2` (`swi 6; bx lr`). The enclosing
loop (`037F8468` region) polls one halfword via `037FE4E8` (`ldrh r0,
[*03808490]`): while 0 it waits, nonzero takes the work branch — and
`*03808490` reads 0 live.
*Wake test POSITIVE*: poking the flag to 1 moves ARM7 to a WRAM IPC-handshake
spin `03807EDC` (`strh 0x100,[04000180]` = IPCSYNC, waits low-nibble == 1)
and ARM9 off its idle loop into real code (a divide routine `@020089C0`,
then `0201C7xx` IPC/counter handlers), cycling but not converging:
`04000180` stuck at `0x100`, and the input half is unfakeable (poke of bit 0
masked on readback). A second poke's side effect dropped both CPUs back to
the original park.
*Radio*: game zeroes the whole association at boot init (BSSID/AID/RXBuf
regs all 0 by game pcs `027EBA80`/`027E9A44`, overwriting the handoff's RX
window `4000-4800`); its last wifi access is frame 4400 (RF/BB init,
33.5k accesses, none after), `pwr 0200` (down), `W_IE 0000`. Live RX-window
re-seed (`48004000`) sticks but changes nothing (`window reads` stay 0).
*Falsified*: channel (held 7, zero bad-channel drops; session log carries
`bssid 00:09:BF:11:22:33, aid 1`), host beacons flow (`bcn 149`).
*Deadlock shape*: guest `cmds rx 247` (1 unique, 246 dup), `answered 0`,
`canned 247`, firmware `replies sent` 159k; host `cmds tx` 141k+705 of the
identical poll body, collecting canned empties. Both screens show (custom-
font, unreadable) message dialogs. Next: the handoff must leave
game-visible association the game's own zeroing does not erase (delayed
re-seed past frame 4400), or find the true setter of `*03808490` (timer vs
RX ISR) — and the ARM9↔ARM7 IPCSYNC ack that unblocks phase 2. No source
changed; session live at record time.

##### Re-seed fix lands: link restored, game-level wait named precisely 2026-10-13 (`logs/md_app.log`, `logs/p16d_host.log`, `logs/p16d_sfc.log`)

*Cold build + gates green twice* (build 0, smoke 166/0, cargo 0 both), SFC
2-console route driver exit 0, BOOTED, all on the fixed binary. Source
changed: `src/Wifi.h` + `src/Wifi.cpp` (`ReseedDownloadPlayLink`, held-
session fields) + `src/frontend/tauri/Bridge.cpp` (one call in
`md_run_frame`).
*Design, earned through two falsifications*: a one-shot at +600 never fires
(the child's wifi event dies with its radio - measured 2 USTimer ticks
total, clock probes stop at frame 4247 - so nothing event-driven runs), and
a naive always-on reseed disarms at once (the hand-off's own window is still
present at frame 0). What shipped: every frame step re-applies the carried
session while the window reads zero, gated on the game being quiet (no wifi
access in 120 frames, read off the MPIo ring - our own Writes log there, so
the first apply self-suppresses), disarming for good once a window exists
past frame +600. Power-domain bit (`SetPowerCnt(0x0002)`) restored first or
the prefix computes off.
*Live proof*: re-seed fired +151 frames post-handoff, guest `cmds rx`
245→807 (model-level delivery restored), radio re-attached; then lawful
auto-power-down (idle radio, game never reads). Combined flag-poke +
working radio reproduces p15 exactly (ARM7 IPC spin `03807EDC`, ARM9
divide, IPCSYNC stuck `0x100`, `window reads` 0).
*The remaining wall is game-logic, proved independent of wifi*: host
`W_IE=0` too, so SFC's driver polls - the guest simply never reaches its
poll loop. It parks on `*03808490` (memwatch: zero stores in 10+ min) with
ARM9 in VBlank-ISR + WFI-idle waiting on IPC neither side sends. Next:
statically scan the ARM7 image for stores to `03808490`, or find what
should ack IPCSYNC (ARM9 main never runs). Guest `window reads` still 0 -
the mission bar is not met. Quit clean.

##### Static scan: no writer, no callers; handshake protocol decoded 2026-10-13 (dumps `/tmp/p17_arm7.bin`, `/tmp/p17_wram.bin`, `/tmp/p17_arm9.bin`)

*Cold build current + gates green* (build no-op, smoke 166/0; no Rust
change, cargo untouched), SFC route driver exit 0 + BOOTED on the fixed
binary, full ARM7/WRAM/ARM9 dumps taken, session quit before analysis.
*Flag `*03808490`*: the ONLY static references in 384 KB are the two known
readers (pools `037FE4E4/4F4`); tight backward-dataflow store scan finds
ZERO writers (ARM or Thumb); the post-family (`037FE340`: `strh 1,[r0-arg]`
+ clear siblings) takes its field pointer as an argument, and the poster has
NO direct caller - like the wait loop `037F8468` itself (no `bl` anywhere):
both are reached by function-pointer/task-table dispatch only. A WRAM
dispatcher (`03804E00-03805000`) walks tables with `mla` and calls
`037FE318/328`, but never the wait loop.
*ARM9 IPCSYNC ack, fully decoded* (`0201CFC0`, sole `04000180` pool at
`0201D0C8` via `ldr r3 @0201D038`): read nibble → echo `nibble<<8` (which is
what the ARM7 side reads as its low nibble) → wait echo-received with
`ip`/`r5` timeouts. Correct code that never runs: no direct caller in
1 MB (function pointer again). ARM9's sleep (`0201AC00`) is an unconditional
idle AFTER `bl 0201C9C4`, and the idle body is a shared utility (8 callers)
- the park is every waiter at once, not one wrong turn.
*VBlank sightings explained*: `lr 02023F2D/45` is Thumb delay-loop code
(`blx 0201C780` tick reads, `cmp r0,#20` spin), not a dispatcher.
*Consequence*: there is no missing store to emulate and no caller to fix up
- both CPUs wait on table-driven dispatches that never fire. The substantiated
next fix is an IPCSYNC echo assist (C++ mirrors the posted nibble exactly as
`0201CFC0` would - idempotent when ARM9 later runs it), but it only helps
past the flag wait, so it must pair with the flag's true setter, still
unknown. No source changed this pass; dumps kept in `/tmp` for the table
hunt. Guest `window reads` still 0.

##### Echo assist drives IPCSYNC 1-7, then stalls matched; tables live elsewhere 2026-10-13 (`logs/md_app.log`, `logs/p18b_host.log`, `logs/p18b_sfc.log`)

*Table hunt (offline, `/tmp/p17_*` dumps)*: no raw table holds `037F8468` /
`037FE340` / `037FE4E8` / `037FE318` in the dumped ARM7+WRAM, nor in ARM9's
first MB - the dispatch tables live outside all three windows. Found instead:
the ARM9 handshake IS reachable via trampoline `0201CD48` (`ldr ip,[pc]; bx
ip`, pool `0201CFC0` - the sole `04000180` pool, one ARM user at `0201D038`);
the ARM7 wait/post go through a WRAM Thumb stub table (`038043C0`: `svc
0/3/4/5` + `bx lr` - the park wait is `svc 3` = Stop, strictly deeper than
the earlier `swi 6` halt); ARM `bl` and Thumb `bl` scans find no callers to
handshake or trampoline.
*Fix shipped* (`src/NDS.h`, `src/NDS.cpp:EchoDownloadPlaySync`, one call in
`Bridge.cpp:md_run_frame`): per frame step, a Download Play child whose
ARM7 posted a nibble ARM9 has not echoed gets the exact `ARM9IOWrite16`
store ARM9's own `strh` would do (IRQ gating identical, including absent -
pure nibbles never set the request bit). Self-limiting: DP children only
(`DPBootChild`, set in `SetupDownloadPlayBoot`, cleared in `Reset`, never
savestated), fires only on inequality (an awake ARM9 always matches first),
and follows the nibble back down - which the first version wrongly gated off
until live proof showed phase 2 waits on the change. Gates green (build 0,
smoke 166/0, cargo 0).
*Live proof*: with radio up (re-seed) + flag poke, IPCSYNC ping-pongs
`0x0101`→`0x0606`→`0x0707`, ARM7 advances into `037FC06C`-wait via `037FE408`
(`r6=8 r7=1000` timeouts live), ARM9 runs handlers, FIFO `1541`→`6298`.
Then frozen matched at `0x0707`: both sides posted 7, neither posts 8 -
"echo suffices" is FALSIFIED with counters. `window reads` still 0; no
match. The remaining wait is whoever moves after 7-7 (a FIFO message or IRQ
only a running ARM9 sends). Quit clean.

##### Phase-8 theory dead: pristine park already holds 7F/7F/BF; the stall is upstream 2026-10-13 (`logs/md_app.log`, `logs/p19_host.log`, `logs/p19_sfc.log`)

*No source changed, so no rebuild* (tree identical to the gated p18b tree).
*Cold facts first, all live on the fixed binary*: the pristine DL-parked guest
(BOOTED, 1260340/1260340) reads `[027FFFF0]=0x7F, [F4]=0x7F, [F6]=0xBF` -
the COMPLETED-handshake values, while ARM7 waits in the *flag* wait
(`*03808490`=0, ARM7 r4=`0x1F` proving it is NOT in the phase wait) and ARM9
idles. The completed words are STALE image data (snapshot of the host's own
finished cart handshake, shipped inside the downloaded image): ARM9 never
attempts the post (8 pctrace polls over 35 s, 0 visits to the `0201A064-90`
retry loop) and never runs the init orchestrator (0 visits to `0201BB74`).
Healthy-cart control reads IDENTICAL words - the cart boot runs `BB74` to
completion and leaves `F0=7F/F4=7F/F6=BF` as its steady state.
*Static decodes that stand* (objdump off `/tmp/p17_*`, grounds for the
above): ARM7 `037FC078` = r4=`027FFFF0`, clear `[F6]`, svc, poll `[F4]==0x7F`,
then trailers `[FFB8]=-1 [FFBC]=-0x10000 [F6]=0xBF`. ARM9's post primitive
`02019F10` is an swp-lock (`[F0]<->value`, post `[F4]=value` only if old
was 0; else retry forever), `02019FA0` the consume (expect, clear both
words, unlock). `0201A094` is ONE function: post `0x7E` -> wait `[F6]==0` ->
trailers -> consume -> post `0x7F` (`0201A150`). `0201BB74` is the straight-
line init orchestrator (`BF38` -> `CD48` IPCSYNC -> `A094` -> `BEB8` ...) with
NO caller - task-dispatched, never scheduled at park.
*Poke experiment* (flag->1 once): IPC burst +35k msgs in ~10 s, settles
matched `0x0606/0607`, flag consumed back to 0, ARM9 back to `CD34` idle,
ARM7 timer-cycling through BIOS wait with transient wild pc `03807AA4`
(inside a data pattern - uninitialized dispatch, needs the real init that
never ran). Guest game still reads nothing (RX count 0, A5A5 fills), host
streams lobby polls (tx 85k, last `0x00`). `window reads` still 0; no match.
*Falsification*: p18b's "phase 8 never posted" mistook a poke-driven detour
for the pristine park - nothing is missing at `[F4]`. The true wall is the
flag's setter plus the unscheduled `BB74` (radio ISR on a match-start the
host never sends while parked on its roster? a task the link never kicks?).
*Adversarial review* (fresh eyes on the diff: echo assist, reseed, Bridge
call order): no defects - frame-boundary echo matches ARM9's own store value
and IRQ gating bit-for-bit, inequality gate provably silent in pristine park
(matched nibbles), `DPBootChild` set/clear/save semantics correct, no
clobber case reachable while ARM9 sleeps. No trims made. Next: hunt the
flag setter from the HOST side (drive the roster past match-start and watch
the child's flag/RX), not from deeper ARM9 disassembly. Quit clean.

##### Prune pass: 50 paths to 29, DataAbort churn and legacy scripts out 2026-10-13 (`logs/md_app.log`, `logs/p20_host.log`, `logs/p20_sfc.log`)

*Backup first*: `/tmp/preprune_full.diff` (1.4 MB, the whole 28-file diff) +
`/tmp/preprune_untracked.tar.gz` (all 50 paths at current content - any
reverted file restores with `tar -xzf`). Then triage, file by file.
*Pruned*: `src/ARM.h` + `src/CP15.cpp` fully and the `DataAbort(u32 addr)`
definition in `src/ARM.cpp` (log-only signature churn, nothing parses the
new format - verified by grep; the PcTrace hooks in the same file stay);
`tools/mkds_join.py` + `tools/scripts/mkds-park-probe.py` (MKDS-era, zero
references from any route script); 17 untracked one-off experiment scripts
(beacon-party, link-commit-trace, lobby_page, md-link, mkds-*, mp-arm7-watch,
mph-*, sfc-cascade-race, sfc-dp-arm-class, sfc-dp-data-frame,
sfc-dp-poke-search, sm64-* - none referenced, none in the diff).
*Kept, with reasons the decider should check*: the observability harness
(IPCTrace/MemWatch/PcTrace + probe/poke C API + appctl + its smoke tests)
stays - it IS the gates' instrumentation, every to-do evidence line was
gathered with it, and the flag hunt needs memwatch/pctrace next; the
`MD_DP_REPLY_*` / `MD_MP_REDELIVER` knobs stay - reply-sweeping is the named
tool for the host-mode question and they are diagnostics, not per-game
behavior; all docs stay (records + cited evidence, never part of a handoff
diff); LocalMP/Wifi link stack stays whole (each hunk was measured live -
reverting any of it voids "identical live behavior"); the Link-test backend
stays (it fixes a pre-existing product button, and its smoke tests are the
gates' bulk). Kept untracked: `sfc-dp-host.sh` (the route), `lobby-track.py`
(the ledger), `clean_repo.sh`, `dp_joiner.rs`, `game_profile.rs`.
*Gates on the pruned tree*: cmake build exit 0, smoke **166/0** exit 0,
cargo release exit 0 (relinked after touch). *Live proof*: full SFC 2-console
route on the pruned binary, BOOTED 1260340/1260340, park signature identical
to p19 (words `7F/00BF007F/00010000/0`, flag 0, ARM7 BIOS-wait r4=`6000003F`,
ARM9 VBlank-park lr `02023F2D`, RX count 0). 28 modified files -> 24,
11386 added lines -> 10927, 50 paths -> 29. Quit clean.

##### Host-driving fails: 11 taps + buttons, child never moves; flag can't be radio-driven 2026-10-13 (`logs/md_app.log`, `logs/p21_host.log`, `logs/p21_sfc.log`, shots `logs/shots/p21_*.png`)

*Prune repair first*: the prune pass deleted `lobby_page.py`, which the kept
`lobby-track.py` imports - restored from `/tmp/preprune_untracked.tar.gz`
(16 scripts stay deleted). Triage lesson: grep imports before deleting.
*Playtest*: SFC 2-console route, BOOTED 1260340/1260340, then drove the host
Recruitment dialog by hand - 11 taps across the whole bottom screen
((128,96) (200,150) (56,150) (128,150) (100,150) (156,150) (128,130)
(128,170) (40,96) (40,130) (80,110)) plus START/SELECT/A. Three taps caused
transient host-pc excursions (`0208903A`, `02023FA4`, `0202E6CC`) that fell
back to the menu spin `02030488`; the dialog never advanced past Recruitment
(same box in `p21_roster`/`p21_aftertap`/`p21_phase05` at ASCII resolution).
*Child counters, every read*: flag `*03808490` = 0 all 15+ reads, RX count 0
/ consumed 0, game collected 0, link acks only (40357/40358), IPC only slow
timer-driven ARM7->ARM9 FIFO growth (21192 -> 30720, 1:1 paired, queue never
drained). Host streams `0x05`-class polls (transition timing unlogged -
not attributable to any tap).
*Upstream-wake theory DISPROVEN by structure, not just by taps*: the flag
cannot arrive by radio - the child's radio is down (`pwr 0200`, game never
reads) and its game is parked, so nothing from the air can unpark it; p15's
poke proved the whole downstream chain works starting FROM the flag. The
wall is the child's own unscheduled boot task (`BB74` never runs, 0 visits
in two passes) - a local scheduler kick, not a host command. Next: why the
DL task loop never starts (entry point? missed timer IRQ? WFI with IME off?),
not more host taps.
*User-still-stumbles*: the host dialog gives no feedback that the joined
child is stuck (link-level assoc accepted, game roster empty); the roster's
touch geometry is unmapped and its font defeats every screen reader.
Quit clean.

##### Scheduler kick: three variants tried, task-ran counters via poke, tree back to echo-only 2026-10-13 (`logs/md_app.log`, `logs/p22`-`p27_*`)

*Gate falsified*: A094's early-return needs `[[02175E78]]!=0`; the DL child
reads 0 (BSS cleared, healthy cart reads nonzero garbage) - the gate passes,
so that theory is dead. *Stack names the park chain*: ARM9's stack holds
`AC10/CD34` + `AD80/B5BC` - the ACFC function Gates on CPU mode == `0x12`
(IRQ): non-IRQ callers take the deep AD-path (`CA30` returns the mode bits;
`bl` needs return 18). Its caller AC48 walks a task list. *Countdown loop
decoded* (`037FEBB4`): r4=8/r6=8/r7=1000, post r4, sleep, read peer, match
counts down else resets to 8 - **ARM7 originates EVERY phase including 8**
(`strh r4<<8`), killing the turn-alternation theory the first kick was built
on. *Kick v1* (`OriginateDownloadPlayPhase`, matched-7 writes `0x0800`):
FIRED live (`04000180==0x0807`) then proved a wedge - it runs after the echo
each frame, pinning peer at 8 so ARM7 (posting 7) mismatches forever.
REMOVED (all 3 sites, zero refs). *Eager echo* (mirror ARM7's post into its
peer field inside `ARM7IOWrite16`) + *Post kick* (`[F0]/[F4]=0x7F` when ARM7
sits in `037FC078` with the lock open): built, gates green, but NEITHER ever
fired in two sessions (IPCSYNC stayed zero / lock stayed shut) - REMOVED as
unproven per the no-speculative-code rule. Final tree == echo-only; build 0,
smoke 166/0, cargo 0 relinked (tree/binary consistent).
*Task-ran counters* (p27, stale-lock image): ARM9 sat in its real `0201D044`
phase driver (frozen r12=1, r5 live-decrementing `0x154->0xF8`), ARM7 in a
`[struct+16]==1` wait (`03807560`, 6-slot table at `0380BA84`, no static
writers). Poking `[0380BA94]=1` advanced ARM7 into game code (`037FEA9C`)
with a first FIFO heartbeat (7sends 0->1), and `[F4]` cycled `7F->0->7F` -
ARM9-side consume+post EXECUTED (neither assist can write `[F4]`; gates held
 throughout). Trigger attribution: the poke (only intervention); the assists
were provably silent. *Read-trap lesson*: 8-byte `mem` at `027FFFF4` returns
`F4,F8` - months of "F6" series were really F8; use 16-byte reads.
Steady end-state: handshake complete both sides, ARM7 in game, ARM9 in
driver, reads still 0 (game never starts; host never starts it). Next: what
releases ARM9's D044 (the nibble ARM7's countdown would post once it runs
freely) and what starts the game on the host. Quit clean.

##### Player-body probe: one UsernameReply moves nothing; roster needs the guest's game 2026-10-13 (`logs/md_app.log`, `logs/p30_host.log`, `logs/p31_host.log`, `logs/p31_sfc.log`, shot `logs/shots/p31_postbody.png`)

*Read first, then probe*: p30's host `wifiio` shows the host game consuming
the stand-in's RsaReply bodies live (`fc=0118` body `04 01 08 6F 00 6E 00
44 00 00`, byte-correct RsaReply + 'onD' filler, `consumed=30`) while its
roster waits - so the roster input channel is confirmed open and ignored.
*Fix shipped* (`src-tauri/src/dp_joiner.rs`, docs + forward in
`tools/run_app.sh`): `MD_DP_PLAYER_BODY=1` answers the first post-boot
`00` poll with a UsernameReply (snippet 1, nickname chars) instead of
`rsa(post)`. One shot per session (latch resets in the deauth arm with
`saw_rsa`/`username_idx`); explicit `MD_DP_REPLY_FORCE` still wins; a muted
stand-in never consumes the shot; pre-boot rounds untouched (gated on
`booted`). Selftest asserts first→`player-body`/type 07/snippet 1 then
second→`rsa(post)`. Gates: dp unit 39/0, cargo release 0, smoke 166/0.
*Live proof (p31)*: env reached the app (startup log), one-shot fired once
(log count 1, right after BOOT 1260340/1260340), host collected replies
throughout — yet 6 min at menu spin `02030488`, class `0x00`, roster
unchanged; child flag 0, RX count 0. Polls-answered-yet-roster-unchanged:
the falsification branch. A firmware-level nickname is not what the roster
waits on; it needs the guest's *running game* (game-level session join),
which is the parked boot task again. Keep the knob (it is the instrument
for any future body sweep); next is the child-side boot work, not more
reply types. Quit clean.

##### Architecture pass: two dead functions out, hold/rings/Bridge verdicts 2026-10-13 (`logs/md_app.log`, `logs/p32_host.log`, `logs/p32_sfc.log`)

*Removed*: `Wifi::SetDownloadPlayChannel` (one caller - `SetDownloadPlaySession`
itself - inlined; decl + 20-line def + doc gone, channel-fallback note kept
on `HeldChannel`) and `note_packet_received` (zero callers; every site uses
`_kind`; decl + def gone). Verified 0 refs repo-wide; build 0, smoke 166/0,
cargo 0. *Live proof* (p32, pruned binary): BOOT 1260340/1260340, park
signature identical (words `7F/00BF007F/00010000/0`, flag 0, ARM9 VBlank-park
lr `02023F2D`, ARM7 BIOS-wait, host menu spin streaming `0x00`, IPC 17/1619).
*Verdicts against the brief*: the Wifi hold fields already have exactly one
writer each and one reset site (`Reset`) + one arm site (`SetDownloadPlaySession`)
- regrouping into a struct is churn, refused; the trace rings stay (branch-guarded
hooks, one struct block, the active instruments - extraction would cost the
workflow that produced every finding); Bridge stays (a C API's forwarding
surface, all of it called by app/smoke). Resulting structure for later
passes: session flows one way (`dp_joiner::Session` -> `Wifi` hold -> live
regs, re-seed re-applies while the window reads zero); debug reads flow the
other (`NDS` rings -> Bridge C API -> appctl/smoke); product writes never go
through debug paths. Quit clean.

##### BA80-BB3F premise FALSIFIED live 2026-10-13: region populated and host-identical, no populate landed (`logs/md_app.log`, `logs/p33_*`)

*Mission brief assumed the child bring-up region BA80-BB3F is zeroed and
populating it plus the task struct would leave the flag wait. Live reads on
the BOOTED route (1260340/1260340, re-seed fired +151f) prove the premise
false, so nothing was written.* Child (p2 ARM7) `0380BA80: 00000001
002EAE41 00000000 00000000 / 00000000 00000001 00000000 00000000` +
`0380BAC0`-`0380BB3F` task pointers (`8000009F`, `0380BAD0`, `027F7B8A`
...) against host (p1 ARM7) `0380BA80: 00000001 00166C41 ...` with the same
shape — populated on both, gate word `[0380BA7C]=1` on both, `BB00-BB3F`
matching except one async word (`038085DC` vs `027F9884`, `9` vs `5`). The
park is unchanged (flag `[03808490]=0`, ARM9 VBlank-park lr `02023F2D`,
ARM7 BIOS-wait lr `037F84FC`, `cmds tx 0`, wifiio `consumed=0`, RX
`A5A5/5A5A` boot pattern, window 4000-4800 held by the re-seed). *Verdict:*
there is no zeroed region to populate and no divergence to heal — writing
host bytes over identical child bytes is puppetry over a chain whose waits
are all for external progress (peer nibbles, RX-driven flags) per the
2026-10-12/13 findings. No source changed (correctly: no gates to repeat
on an untouched tree); the blocker stays the unscheduled guest task
(`BB74` never runs), not guest memory contents. Do not re-propose a
BA80-BB3F populate without a new divergence measurement. Quit clean.

##### BB74 waker hunt: orchestrator never runs, scheduler alive, queuer unnamed 2026-10-13 (`logs/md_app.log`, `logs/p34_cmake_cfg.log`, `logs/p34_cmake_build.log`, `logs/p34_cargo.log`, `logs/p34_sfc.log`, `logs/p35_sfc.log`, `/tmp/p34_bb74.txt`, `/tmp/p35_entry.txt`)

*Cold rebuild first*: last pass's `clean_repo --apply` deleted `build-tauri/` + `src-tauri/target/`, so this pass rebuilt cold (cmake cfg/build 0, smoke 166/0 on `diag.nds`, cargo release 0, cargo lib 43/0 — all on this tree). *Adversarial review of the worktree diff*: `EchoDownloadPlaySync` ordering sound (`DPBootChild` set after `Reset()`, cleared in it, frame-thread only, nibble-only stores), `ReseedDownloadPlayLink` holds armed/cleared in exactly the two places, `SetDownloadPlaySession` memcpy bounded, `md_dp_boot` arg-guarded, `dp_joiner` unwraps all in tests or on mutexes — no defect found, nothing changed.
*BB74 pinned static* (`/tmp/p17_arm9.bin`, base verified via BB84→A094 call): straight-line orchestrator (`BF38→CD48→A094→BEB8→…`, no branches) whose callees `BF38/BEB8/9DB4/9B70` have NO other caller — the DL init chain is reachable only through BB74. Zero ARM + zero Thumb BL callers in 1 MB, zero raw `0201BB74` pointers in WRAM/ARM7 dumps: unscheduled, not mis-gated.
*Live proof it never runs*: pctrace window exactly `[0201BB74,0201BBC4)` recorded **0 visits over ~1400 frames** (f5269→6658); boot-spanning entry trace (armed pre-transfer, Reset-cleared, regions in order `02000000→02001000`) shows the normal SDK-init order into the main loop with no BB74-region visit in 13.3M. Park identical (flag 0, VBlank-park lr `02023F2D`, `cmds tx 0`, `consumed=0`).
*Scheduler alive*: steady state executes a Thumb string-worker chain (`0200DCC8` strlen-ish → `blx 02001C24` memmove, live BSS `021E85A2`-buffers, `lr=0200DCFF`) — main loop runs and never queues BB74. *Falsified side-lead, recorded so nobody repeats it*: host-vs-child same-address BSS compare (`02175E70`: child zeros vs host `F6C6A839…`) is INVALID — downloaded image and cart are different binaries with different layouts; same address is different variables. Mission bar (window reads >0) not met; falsification branch with counters delivered. Next: full-RAM dump scan for the runtime-built task table (outside all scanned windows), or a wider entry-path trace to the skip branch — both need a fresh route, not more steady-state watches. Quit clean.

##### Full-RAM scan: BB74 referenced nowhere; skip branch + flag gate named, poke moves CPUs but not BB74 2026-10-13 (`logs/md_app.log`, `logs/p36_sfc.log`, `logs/p36_dump.log`, `/tmp/p36_upper.bin`)

*Exhaustive pointer hunt (parked child, BOOT 1260340/1260340):* dumped live `02100000–02400000` in 4 KB reads (768/768, `mem` cap is 4096 B — probed, not assumed) and scanned with the first-MB/WRAM/ARM7 dumps: **zero** references to `0201BB74` (or `D044/BF38/BEB8/A094/CD48`) in the entire address space, and zero dense code-pointer runs in the static first MB (315 exist upstairs, none touch the DL chain). A pointer-dispatched queuer for BB74 does not exist as data — the gate is a computed branch on the entry path, not a table.
*Skip branch named (static, `/tmp/p17_arm9.bin`, Thumb-decoded):* init at `020121B0` checks `[027FFC40]==2` (`ldr→ldrh→cmp #2→beq` at `020121F6–FC`, pool `02012214`) and in DL mode SKIPS `bl 02012150` — a setter storing 1 to global `[02174E08]` (pool `02012158`). Live parked child reads `[02174E08]==0`, consistent. Sibling indicator pools (`020145EC/02018FA0/0201EA20/0201CAE8/02023AE4/60`) not yet decoded — next pass's list. Consumer `0201215C`: flag 0 → `blx 0201CD34` (WFI idle), matching the park.
*Poke test, falsification with counters:* set `[02174E08]=1` (BB74-window trace armed first): ARM9 left VBlank-park (`02023F4C`/`027E3B38`) for `0201CD34`-via-`AC10` (`sp 021760A0`, `r1=0x1F`), ARM7 took a transient excursion (`037FBC44`, back to BIOS idle) — both CPUs provably moved — but BB74 stayed **0 visits/~2000 frames**, flag 0, `consumed=0`. Flag is one gate in a chain, not the queuer. Restored to 0 afterwards (ARM9 stayed on the new path — one-way, stated honestly). Mission bar not met; no source changed (probing only). Next: decode the sibling indicator branches + find `0201215C`'s callers to map the rest of the chain. Quit clean.

##### Gate map closed: BB74 reachable only via dead stub; six indicator gates decoded; context pointer reopens 2026-10-13 (static `/tmp/p17_arm9.bin`, `/tmp/p36_*.bin`, BLX map `/tmp/p36_thumbblx.pkl`)

*Breakthrough first — earlier scans missed BLX-immediate (`E800`) callees:* `0201BB74` HAS one caller, `blx` at `02027568` inside wrapper `02027564` (calls BB74→BE90→BEA4 + bookkeeping, returns normally). But the wrapper is itself dead: no ARM BL/BLX, no Thumb BL/BLX, no raw pointers anywhere (first MB, upper RAM dump, WRAM, ARM7). Its only referrer is boot stub `02000900` (cache/MMU + `blx 02027564` + `bl 020F4438` + computed `bx r1`), which is equally unreferenced — zero callers of any kind. The RSA-entry boot (`02000850`, linear prologue, verified no branch to `02000900`) bypasses the entire DL-init chain. Residual risk, stated: a computed `bx` could reach `02000900`, not statically excludable.
*Chain below the (live) entry, all Thumb, all `ldrh [027FFC40]; cmp #2`:* `020121F6` BEQ-skips the store-1 to `[02174E08]` (consumer `0201215C` ← sole caller `020123C2` ← `020123B4` ← `0201466C/02014684`, parks in WFI on flag 0 — the poke path); `0201454E` BNE: DL fall-through runs struct init writes; `02018F2C` BNE: DL passes pointer `027FFC42` (firmware context!) downstream, normal passes NULL; `02023964`/`02023E3C` BNE-then-`movs 1`: DL-only init blocks; ARM `0201CA70` BNE: DL falls into the CD34 wait. So DL mode is not inert — it runs its own init — but BB74's road stays closed.
*Correction to the 2026-10-13 context verdict:* `027FFC42` IS read — by main-init `02018F2C`, outside the previously scanned join-task windows. Our boot leaves it zero; the next falsifiable test is poking the context fields live and watching flag/RX, not writing boot code yet. *Adversarial review:* this pass changed no source; fresh-eyes check on the standing diff (echo/reseed/Reset, bounded memcpy, arg guards, test-only unwraps) found nothing new; the `02000900`-dead claim's one hole (computed branch) is named above. Mission bar not met; map delivered instead. No session ran (static only) — nothing to quit; disk 52%, binary kept.

##### Context poke FALSIFIED live 2026-10-13 (`logs/md_app.log`, `logs/p37_sfc.log`)

*Fresh BOOT (1260340/1260340), BB74-window trace armed pre-poke:* baseline context zero beyond indicator 2, flag 0, VBlank-park. Poked `027FFC44+` with the host's beacon values (`00600002 0900FE82 332211BF 40000020 00191802 00210000 00000007`, readback-verified, indicator untouched) — the words the `02018F2C` consumer hands downstream as the firmware context.
*4-minute watch, all counters:* flag stayed 0, BB74 **0 visits over ~6200 frames**, `consumed=0`, poked words held untouched (game never re-reads them). ARM9 drifted `02023F2A→0201C784→0201CD34-via-AC10` — documented natural park-cycling (p7/p15), not poke-attributed: the end state matches the flag-poke signature by coincidence of the shared wait, with flag/BB74/RX all unmoved. So late context is not the missing input — consistent with the consumer running once at boot (zeros already consumed) — and pre-boot context seeding is the remaining variant, untested. Restored all 7 words to 0 (verified). No source changed. Next: seed context pre-boot (pause-at-entry or boot-staging write in `SetupDownloadPlayBoot`, SFC-registry-gated per AGENTS.md) or trace the `02018F2C` downstream to see what the zeros broke. Quit clean.

##### Pre-boot staging lands: BB74 runs its first pass ever, flag/RX still parked 2026-10-13 (`logs/md_app.log`, `logs/p38_sfc.log`, `logs/p38_build.log`, `logs/p38_cargo.log`)

*Change* (`src/NDS.cpp`, `SetupDownloadPlayBoot`, no new knob/registry — the whole DP boot is already joiner-gated to DL titles, and real firmware stages this block for every child): 7 context words at `027FFC44+`, identical values the late poke proved harmless, so this is purely the timing half of that experiment. Adversarial: fixed addresses (no input indexing; clear of indicator/`FFC30`/header-copy), behind existing null/size guards, after `Reset()`, RAM-savestated consistently. Gates on final tree: cmake 0, smoke 166/0, cargo release 0, cargo lib 43/0; binary proven current by mtime chain (`libcore.a` newer than edit, binary relinked after touch — comments don't survive in `strings`, so timestamp causality, not string-matching, is the check).
*Live (fresh BOOT, BB74 window armed pre-boot):* staging verified in RAM post-boot; **`0201BB74` recorded 20 visits (BB74→BBC0, one full pass, lr=`0202756D`) — the first execution ever** (all prior runs: 0). Flag stayed 0, `consumed=0`, handshake words stale `7F/00BF007F`, `[02174E08]` 0, park cycling continues; no second pass through f11359. Reading: the context unlocked the road to the wrapper (via the `02018F2C` DL path), BB74's 12 calls all executed including A094 — but the run doesn't cascade (A094's post not visible; the flag gate `0201215C` still parks since nothing sets `[02174E08]`). Staging stays: guest's own DL init executing is strictly closer to hardware than 0 visits, and it is boot-faithful, not a title hack. Next: why one pass doesn't cascade — whether A094 needs the ARM7 side (which waits flag `03808490`=0, the cross-CPU deadlock) or the flag needs BB74's chain to set it. Quit clean.

##### Cascade blocker named: consumer never returns, flag has zero writers 2026-10-13 (`logs/md_app.log`, `logs/p39_sfc.log`)

*Fresh BOOT with staging, both instruments pre-armed:* memwatch on `[02174E08]` caught exactly 5 stores — all boot-prologue byte-copies (`pc 02000A08/020009E4`, `lr 020008E0`), none from the flag setter `02012150` (skipped, as mapped) and none from BB74's chain. Post-boot: **zero stores ever**. BB74 reproduced one pass (20 visits, f4561); steady-state memwatch on `027FFFF0–F7` (folds to `023FFFF0`, same RAM) shows 0 stores/60 s; flag 0, `[02174E08]` 0, `consumed=0`, park cycling.
*Verdict — the terminal gate is the main thread, not BB74:* `020123B4` calls consumer `0201215C` first thing; flag 0 → `blx CD34` never returns, so the main thread parks regardless of BB74 having run. BB74 necessary-but-insufficient proven with counters. BB74's chain ruled OUT as the flag setter (watch would have caught it). Remaining writers on real HW: a firmware-state-dependent path that never reaches the consumer, or an IRQ writer (child `IE=0` — unfirable here). Next: enter `020123B4` with `[r5+r6]` state from a real handoff, or find what makes the caller skip the consumer. No source changed; session quit, no strays.

##### Both gates open fails: consumer is one-shot, flag must predate it 2026-10-13 (`logs/md_app.log`, `logs/p40_sfc.log`)

*Fresh BOOT (staging binary, BB74 reproduced at 20 visits):* with BB74 already run, poked `[02174E08]=1` and watched 120 s with a trace on the consumer region `[02012150,02012400)` — **0 visits over ~2900 frames**. The main thread never re-enters the consumer: it checked once at boot (flag 0 → parked that chain in CD34; pc9 cycling since is VBlank-ISR + WFI loop, consistent with two stacks `027E3B38`/`021760A0`). Late flag is unread, same as late context. Flag 0 (ARM7), `consumed=0` throughout; restored to 0, verified.
*Timing ambiguity found, stated not solved:* the 5 memwatch hits on `[02174E08]` (prologue byte-copies) were recorded by a watch armed PRE-transfer — likely the CART's prologue noise, not the DL boot's — so post-boot writers remain un-generalized; a post-boot-armed watch is still owed. But the decisive point stands regardless: the consumer reads once, so the flag must be 1 *before* the boot-time check. Candidates: stage `[02174E08]=1` at boot (same pattern as context staging; DL-mode skip means only staging provides it) — falsifiable in one route. Deeper puzzle, recorded: prologue data-copy should leave nonzero (as on real HW) yet live reads 0 — something zeroes it mid-boot here; the post-boot watch + BSS-clear ordering is the next read. No source changed this pass. Quit clean.

##### Staging erased: skip fired live, BSS-clear kills it, consumer task never dispatched 2026-10-13 (`logs/md_app.log`, `logs/p41_sfc.log`, `logs/p41_build.log`, `/tmp/p41_cons.txt`)

*Change, then removal (`src/NDS.cpp`):* staged `[02174E08]=1` behind a gate-shape check (`020121F6..FC` halfwords + pool `→027FFC40`; mismatch = silent skip; joiner-armed-only so fixes-off stages nothing). Gates green (cmake 0, smoke 166/0, lib 43/0, cargo 0, mtime-chained binary).
*Live proofs from the boot-spanning consumer-region trace (108 visits):* (1) the skip FIRED — `020121F6 (r0→…027FFC00) → 020121FC (r0=2) → 02012204`, store bypassed, so validation matched and staging wrote; (2) `[02174E08]` still reads 0 — the game's prologue BSS-clear runs after staging and erases it (the mid-boot zeroing mechanism, found — resolves the previous entry's puzzle); (3) the consumer call `020123C2`/`0201215C` has **0 visits ever** — its task is never dispatched; instead `02012360+` runs twice with stale `lr=0201BBB4`, i.e. **bx-dispatched tasks** (dispatcher leaves lr untouched), same family as BB74's wrapper run. So the flag is doubly moot: erased before any reader, and its reader task never scheduled.
*Correction to two earlier readings:* the p36 flag-poke "movement" is now unattributed (consumer never runs — nothing could have read the poked flag; natural park-cycling per p7/p15 stands); the "main thread parks in consumer" verdict is refined — the park is dispatcher-idle + VBlank-ISR, and the missing piece is the undispatched task set, not the flag value.
*Removal:* staging hunk replaced with a why-not note (unproven-inert code out, evidence kept in this entry). Next: enumerate the bx-dispatched tasks (stale-lr sites) and find which task slot that should poll/advance the join stays empty — the dispatcher and its table are the hunt now. Quit clean.

##### Task-set map: consumer path is statically reachable; its struct-init is guard-skipped 2026-10-13 (static only — `/tmp/p17_arm9.bin`, no live session, no source changed)

*Method:* full Thumb-BL callmap (3578 targets, `/tmp/p42_bl.pkl`), ARM-BL callmap (1148, `/tmp/p42_armbl.pkl`), 70-entry `ldr r3,[pc]; bx r3` veneer table, pool-word + loader scans (Thumb and ARM).
*Corrections to p41 (stated plainly, all three):* (1) the "0201235C/5E visits" were veneer DATA words, not code — `02012358` veneers to `020120F5`, `02012350` veneers to `0201255D`; (2) the "bx-dispatched tasks, stale lr" reading is withdrawn — the whole chain is plain static `bl` calls; (3) "reader task never dispatched" is refined — the consumer IS statically reachable; what never runs is *guarded*, not undispatched.
*Map — one linear static chain:* roots `02012EE8` / `02013024` (both once-guarded SDK-style one-time inits, no static callers — thread/table entries) → `02012E14` → `02000822` → `02095AE4` → `02023B0C` → `02023DB4` → `020144FC` → `0201451C` (MP-init; head calls once-guard `02012360` + struct-inits, all unconditional) → tail with TWO nested guards: outer `@02014550-54` skips the whole tail iff halfword `[027FFC40] == 2`; inner beq-chain `@020145A4-B2` skips the second struct-init iff double-deref `[[027FFE40]]` ∈ {0, 0xFFFFFFFF}. Tail does: `bl 02013BB0 @02014598` (struct-init wiring entry `02014619`, the small r1-dispatcher) and `bl 02013D04 @020145C6` (struct-init wiring the consumer path). Consumer path itself is unconditional straight-line code: `0201466C` (no static callers; its pointer `0201466D` sits at table word `0201460C` with no static Thumb/ARM loader — invoked via pointer) → `bl 020123B4 @02014684` → `bl 0201215C @020123C2` (consumer prologue). So if `0201466C` ever runs, the consumer runs — and live (p41, 108-visit boot-spanning trace) it has 0 visits ever.
*The missing slot, named:* the `02013D04` struct-init — the only thing standing between the ran MP-init head and the consumer is its guard chain. Disambiguation is one live parked session and two words, no new code: `mem` halfword `[027FFC40]` (outer guard) and `mem [027FFE40]`→`[that]` (inner guard) on player 2. Whichever guard bites tells whether the game thinks it is not-a-client (outer) or its session struct never filled (inner).
*Adversarial note on this pass's own work:* analysis only, zero source diff — the risk is misattribution by heuristic (nearest-start function mapping misfired twice: `020123B4` first assigned to `0201451C`, `0201305A` folded into `02012EE8`). Both corrected by push-opcode (`B5xx`) boundary scans + pool-region checks before recording; every edge above was re-verified at the instruction level after the heuristic failed. Scratch lives in `/tmp` (p42 bins/pickles) — outside the tree, nothing to clean in-repo.

##### Guard bitten, live with host control: outer guard — child reads 2, host reads 1 2026-10-13 (`logs/md_app.log`, `logs/sfc_dp_host.log`, `logs/p43_run.log`)

*Route:* fresh BOOT, driver to Recruitment host → Cut Off → `TRANSFER STARTED` (0x04 at once) → download 1260340/1260340 bytes, 2502 packets → `BOOTED`. (Driver needed `MD_LINK_VERBOSE=1` — without it the beacon check saw 0 and exited 1 after reaching host role; first attempt's failure was instrumentation, not the game.)
*Reads on the BOOTED child (player 2):* halfword `[027FFC40]` = **2** → outer guard (`cmp r0,#2; bne tail @02014550-54`) does NOT take the tail — both struct-inits skipped, consumer path never wired. Inner guard moot but also-biting: `[027FFE40]` = `0x00137C00` → `[[027FFE40]]` = **0** ∈ {0,−1} → the `02013D04` init would be skipped even inside the tail. Fresh consumer-region trace: **0 visits**; `[02174E08]` = 0.
*Control on the cart host (player 1):* `[027FFC40]` = **1** (≠2 → tail entered, both structs wired), `[027FFE40]` = `0x00487C00`.
*Reading:* the word is a role/state — 1 = full init (host), 2 = limited init (download child) — and the game deliberately takes a different MP-init path as a child. Two hypotheses, stated not solved: (a) 2-is-correct-child-behavior and the park's cause is link-level (child waits for something the host never sends); (b) a real child would read ≠2 and our staged boot omits the firmware/MP state that sets it (ARM7 side or handshake). Discriminator for next pass: stage `[027FFC40]=1` at boot-staging time (same pattern as the p41 attempt, now aimed at the outer guard instead of the flag) and watch for tail entry → consumer visits. One-shot timing applies — a late poke cannot re-run init (p40 lesson), so it must be staged. No source changed this pass.

##### Indicator staging fires the tail but the consumer stays undispatched 2026-10-13 (`logs/md_app.log`, `logs/sfc_dp_host.log`, `logs/p44_*`; staging REMOVED from `src/NDS.cpp`, why-not note left)

*Change, then removal:* `ASFE`-gated `[027FFC40] = 1` (header game-code content identity — the same 4 bytes the registry keys on — so no other title's child could see it; joiner-armed-only by construction). Gates on the tree with the hunk: cmake 0, smoke 166/0, cargo 0, lib 43/0, binary mtime-chained.
*Live, three sessions, same binary:* staging fires deterministically — child `[027FFC40]` reads 1 in all three (game never re-writes it), no undefined/data-abort/prefetch fault anywhere (the cart-path risk did not materialize), transfer byte-identical every run (2502 pkts / 1260340 B, driver exit 0, `BOOTED`). The game's own setter runs: `[02174E08]` reads 1 (the `020121B0` DL-skip not taken — a store through the game's own path, post-BSS-clear, so it survives where p41's staging died). Struct `02175D80` holds both table records including consumer entry `0201466D`.
*The 45-visit boot-spanning tail trace (session 3, window `[02014540,02014690)`, all visits kept):* `02014550/52/54` → `0201458E` (outer guard OPENED by the staged 1) → `02014598` (first init `02013BB0` ran) → inner beq-chain `020145A4-B2` FALLS THROUGH (inner guard PASSED at boot — `[[027FFE40]]` was neither 0 nor −1 when the tail ran, though it reads 0 now; the first init most likely fills what the inner check tests) → `020145C6` (second init `02013D04` ran) → return. `0201466C`/`02014684` NEVER visited: the registered consumer entry is never dispatched.
*Game state unchanged:* pc9 `FFFF0108`, ARM7 in own code, `mp collected 0`, reply slot never armed, host on 0x00 dummies — no window reads. So the done-branch is falsified with counters: forcing the tail wires the structs but does not dispatch the consumer. The wall moved one step down: not the struct-init (done) but the DISPATCH of the registered entry — no static caller exists (p42), so it fires via the game's task/IRQ scheduler or not at all, and the prime suspect is back on the ARM7 side (the v0.1.9dc bring-up stop: child never arms TX/associates, so a scheduler gated on association never runs the entry).
*Instrument lesson, recorded so the next pass does not repeat it:* `pctrace --off` CLEARS the record and re-arming with lo/hi RESETS it — session 1's boot trace was destroyed this way. Protocol: arm once, read back with BARE `pctrace --player N --cpu C` (no window, no --off). Also: one window per CPU — the wide `[02012150,02014700)` span flooded (13037 visits, format caps at last 320); the narrow tail cluster kept all 45.
*Removal:* hunk out unproven-inert-at-the-game-level (firmware-unfaithful + title-specific-without-registry + no progress = out per the rules); rebuilt + regated after removal (below). Recipe preserved here. Next: find what dispatches `[02175D80+0x48/+0x50]` — load-watch on the registered entry or the scheduler that owns it.

##### ARM7-gating falsified as cause: the dead direction is ARM9→ARM7, host control included 2026-10-13 (stock binary, `logs/md_app.log`, `logs/sfc_dp_host.log`, `logs/p45_run.log`, `/tmp/p45_disp.txt`)

*Route:* fresh BOOT, driver exit 0, transfer byte-identical (2502/1260340), `BOOTED`. No staging, no source changed.
*The live asymmetry, host-controlled, over matched ~20 s windows (~1200 frames):* ARM9→ARM7 IPC — child **0** (frozen at 17 boot messages forever) vs host **3006** (~2.5/frame). ARM7→ARM9 flows on BOTH (child ~1848, host ~7594) — the ARM7 side is alive and passing traffic. RX buffer on the child is untouched fill (`A5A5`, count 0, `consumed=0`, `cmds tx 0`) vs host's live RX (`consumed=30`, `cmds tx 27712`). Wifi role carried fine (`role=2 client`), game just never engages it. ARM7 pc stuck at BIOS `115C` on BOTH consoles — not a discriminator (host identical), so ARM7-idle was never the wall.
*Dispatcher probe (60 s steady-state trace, window `[020FD600,020FE800)`, 22176 visits):* region `020FE000` NEVER entered — the struct-callback dispatcher `020FE6E4` (`blx [r2+32]`, the shape that could walk the registered entries) and its caller `020FD6C4` have 0 visits. The heat is a live per-frame state poller (`020FDF00`: halfword gates 14/10, bit0-gated SDK call, clears `[r4+0x48/+0x4C]`) spinning in `020FDFE2-020FDFF0` — the game runs, it just never sends.
*Verdict:* the mission's ARM7-side hypothesis is falsified as the cause — the ARM7 behaves exactly like the working host's. The dead direction is owned ARM9-side: the MP task that would dispatch registered entry `0201466D` (and with it the first ARM9→ARM7 send past boot) never runs, so the child never gets far enough to need the link. The wall is link-independent. Next: the scheduler owning `0201466C` — thread list/wakeup on the live child, or what the host's ARM9 runs (its poll at `02030488` + sends) that the child's park-cycle (`0201CD34/02008B90/020085C4/0201C784`) never reaches. Quit clean.

##### Scheduler narrowed to the tick family: IRQ installed, gates pass, dispatch pointer never set 2026-10-13 (stock binary, `logs/md_app.log`, `logs/sfc_dp_host.log`, `logs/p46_run.log`; no source changed)

*Route:* fresh BOOT, driver exit 0, transfer byte-identical, `BOOTED`. Probe-only.
*IRQ vector, child vs host:* `[027FFD9C]` = `0201C554` (child) / `0202F6A8` (host) — both nonzero, in-image. The child DID install its game IRQ handler; "no ISR, no tick" is dead. Both ARM7s idle in BIOS `115C` regardless.
*The scheduler is the tick family itself (`020FDF00`/`020FE620`), running per-frame:* its gates read LIVE-nonzero — `[021B3508]` = 1 (the `020FDF64` spin skipped), `[021B3A94]` = 1 (SDK-global reader `02101B04`, decoded live — it lives at `02101B04+`, outside the 1 MB dump, hence invisible to every static scan so far), halfword gates 14/10 pass, state byte `+0x39` gets set, counter runs to 30. It advances; its MP transitions just never select. No separate missing scheduler — the machine that would dispatch IS running and waiting.
*What it waits on (static shape):* `020FE6E4`-style callback dispatch fires only when a registered pointer (`[r7+0x4C]` → `[that]`, both nonzero) is set, then `blx [r2+32]` — the wakeup is a POINTER WRITE by someone else, and no static caller/loader feeds our `0201466D` registration. The writer of the dispatch pointer is the named next hunt (store-watch on the registration, or forcing the tick's transition inputs).
*Self-caught:* queried callers of `0201B04` (empty) before noticing the real address is `02101B04` (callers present, incl. the tick) — own typo, not a callmap bug; p42's maps stand. SDK band above `02100000` is outside `/tmp/p17_arm9.bin` — future static work needs a bigger dump.
*Counters:* RX untouched, `consumed=0`, ARM9→ARM7 still 0 past boot, park-cycle unchanged. Quit clean.

##### First-read falsified: the child never enters its IRQ/RX loop, so no frame is ever waited on 2026-10-13 (stock binary, `logs/md_app.log`, `logs/sfc_dp_host.log`, `logs/p48_run.log`; no source changed)

*Route:* fresh BOOT, driver exit 0, transfer byte-identical, `BOOTED`. Probe-only.
*Register-read census, child vs host:* child — MAC/BSSID/AID/RX/TX regs exactly 5 reads each (boot bring-up only), then ONLY `W_BBBusy` 27876 / `W_RFBusy` 1256 / `W_BBRead` 1018, and those counts FROZE over a 20 s resample (bring-up polling ended, nothing replaced it). Host — `W_IF` 166435 / `W_IE` 136330 (IE=`703F`, full IRQ loop), TXStat/TXSlot/USCount/CMDStat tens of thousands each, live RX frame reads. The child's game never reads RX/TX status after boot and never enters the IRQ loop its own bring-up was heading for.
*Verdict:* "what frame does the RX path need first" is the wrong question — falsified with counters. No frame is ever waited on because the RX path never starts: the wifi lib stalls at baseband bring-up exit (TRX power stays x0 vs host x8 per v0.1.9di; power-up attempted twice then `WIFI: OFF` per v0.1.9dd) and settles into the ARM9 idle cycle (`0201C784/02023F4C/020085C0`) with the radio unarmed. The named wait is bring-up completion — plausibly a status bit in the power/RF sequence our core never satisfies — not a missing RX frame. Next: the power-up abort — which status the sequence polls between the RF block write and `WIFI: OFF` (Wifi-core emulation gap, not game state). Quit clean.

##### Power-up abort premise retired: v0.1.9dl already built, measured, and reverted the fix 2026-10-13 (no session, no source change — static verification only)

*Why no new route:* the mission's premise (an emulated status bit aborting power-up) was settled 2026-10-04 by v0.1.9dl with a PC-tagged register trace: the child's ARM7 driver runs power-up state 16 then park state 0 entirely on its own logic — it reads NO power register (`W_ModeReset`/`W_PowerState`/`W_TRXPower`/`W_PowerForce` all unread), never runs the force-transceiver routine `027EA66C`, and its table loader writes the out-of-reset bit back to 0 itself. The carry-the-bit fix was BUILT, measured not to help (`TRX power ON` still 0, digest unmoved), and REVERTED. Verified standing on this tree: `src/Wifi.cpp`/`Wifi.h` untouched since (log top = app/link work + upstream #2681), no carry code present (only the stock bit0 comment at `:883`), all six sessions since consistent with dl's stop. Re-running the power trace would re-prove settled ground; inventing a Wifi value with no reader behind it is exactly what dl refused. No code changed, correctly.
*The synthesis dl didn't have (p42–p48):* the ARM9 side of the same stall, mapped to the instruction — indicator guards → struct wiring (staged-vs-stock struct signature proves which inits fired) → registered-but-never-dispatched entry `0201466D` → tick family running with gates nonzero waiting on a pointer-write that never comes (0 stores/5400 frames) → ARM9→ARM7 IPC frozen at 17 boot messages vs host's stream (missing the `00014007` interleaved word per v0.1.9do; the words are runtime-composed, no static literals — checked both dumps).
*Deadlock shape, stated as the next hunt (not a finding):* ARM9 waits on ARM7/link-driven state (association progress filling `[[027FFE40]]` / the dispatch pointer); the ARM7 parked after bring-up, its park-state back in BIOS idle. Each side's next step needs the other to move first — the v0.1.9df shape ("I have finished sending, now you" both ways). What breaks it: what ARM9's message #18 is conditioned on (needs IPC *content* capture — counts exist, contents don't), or what the ARM7 park-state polls (IPC mailbox vs pure sleep). That instrument, not another wifi register, is the next build.

##### IPC sender dissected, circle closed at the instruction level 2026-10-13 (static `/tmp/p17_arm9.bin` + `/tmp/p47_sdk.bin`; no session, no source change)

*The 17-send primitive (`0201CE78`, ARM):* bitfield-assembles the word from args, then `ldrh [04000184]; ands #0x4000` — tests the FIFO-ERROR bit: set → clear it (`orr #0xC000`, ack+enable) and return WITHOUT sending; clear → DisableIRQ (`0201C9D8` = `and #0x80` helper family) → `ldrh; ands #2` (SEND-FIFO-FULL) → full: `mvn r0,#1` error-return; room: `str [04000188]` (= IPCFIFOSEND, pool `0201CF28` — the "@0201CF14" trace pc is this store +8) → success 0. Robust sender, gate provably open live (`fifocnt9 8501`, empty). The primitive is NOT the wall.
*Who issued the 17:* tiny init-time tail-call wrappers (`02012E44`-family: set r0/r1/r2, `ldr r3,=0201CE78` (pool `02012E54`), `bx r3`) hanging off the p42 init chain — reachable only at boot, never in steady state. Word 18 needs a steady-state caller that does not exist.
*Circle, every edge cited:* tick runs, gates pass (p46) → its MP transition needs association state (`[[027FFE40]]`=0, dispatch pointer unwritten p47) → association needs radio/RX (never armed; RX unconfigured) → radio needs ARM7 power ladder (parked after bring-up, dl) → ARM7 park needs ARM9 session start (ARM9→ARM7 frozen at 17) → ARM9 send #18 needs the tick to mature → back to start. Each wait is real code, correctly implemented, waiting on an event no one emits.
*What breaks it (next):* stage the tick's transition inputs at boot (association-shaped structs, p41/p44 pattern — the structs, not the flag) so the tick matures → send #18 → ARM7 wakes → radio arms; or poke send #18's path live and watch the cascade. No emulator value is missing — a state kick is.

##### Struct staging rejected as inert; consumer posts to an OS queue via shared WRAM 2026-10-13 (static `/tmp/p17_arm9.bin`; no session, no source change)

*Mission-literal rejected with prior counters:* boot-staging the association structs produces exactly the state p44 measured live (tail-written `0201466D/02014619/00000602`) — consumer undispatched, 0 sends, park unchanged. Rebuilding it would re-prove the falsified. Not built, correctly.
*Consumer downstream, new (CORRECTED p53 — the consumer→CF54 edge was overstated):* `0201CF54` (ARM) IS an OS message-queue post (`str r5,[021761F0+r4*4]`, ready-bit r4 in `[027FFC00+0x388]`, pools `0201CFB8=021761F0`/`0201CFBC=027FFC00`, IRQ-guarded) with 7 ARM callers (`0201CB64/D7B8/DF38/F008/0FB8/102C/2A3C`, each via one upper wrapper, all 7 uppers roots) — but NONE is in the consumer chain. The earlier "callees of 15C/174" list that suggested the edge mixed in neighboring ranges; re-checked, no overlap. So the post paths exist and are callback-rooted like everything else, but the consumer is not proven to reach them. The p52 poke (bits 0–7, no waiter) stands regardless — it tested the flag directly. `02013024`'s blx pair is not thread creation either — `0201CD48` is an import stub (→`0201CFC0` IPCSYNC-ack) and `0201CF2C` reads the same flag family. Thread creation is still unlocated — and may not exist (see p53).
*Refined breaker (no source change, live-testable):* the missing event is a queue-post/flag-bit, not struct values — memwatch `[027FFC00+0x380..0x390]` on a parked child to see who touches the flag (ARM7? tick? nobody?), or poke the consumer's bit and watch for dispatch (pctrace `0201466C`), send #18 (IPC counts), ARM7 wake. If the flag is never touched by anyone, the queue has no waiter and the thread was never created — then the hunt is thread creation on the DL path (but see p53: the pattern is callback/IRQ/main-loop, not threads at all).

##### Spawner falsified twice: IRQ tables empty on BOTH, host comparison was apples-to-oranges 2026-10-13 (`logs/md_app.log`, `logs/sfc_dp_host.log`; tables/host-handler bytes read live, scratch removed; no source changed)

*Route:* fresh BOOT, driver exit 0, transfer byte-identical, `BOOTED`. Host cart ARM9 extracted from the ROM (468 KB, `ASFE`) — but raw bytes past the header don't decode (secure-area/encryption), so host static comes from live RAM reads instead.
*IRQ handlers, same SDK shape on both:* child `[027FFD9C]=0201C554`, host `=0202F6A8`; both begin `ldr ip,[table]; ldr ip,[ip]; cmp #0; bxne ip` (live RAM dump of the host's confirms). Tables: child `[02176120]` = 0, host `[020B40FC]` = **0**. The earlier "host nonzero at `02176118`" was different BSS, not the table — withdrawn. NEITHER console dispatches IRQ callbacks; the host's progress comes from its main-loop poll (`02030488` + IPC sends), not from IRQ.
*Called-off:* no OS threads (none found anywhere — every candidate was callback/IRQ-shaped), no IRQ-table difference. The "spawner" doesn't exist: both consoles run main-loop + registered callbacks; the child's callbacks never fire because its state never matures, not because a spawner skipped it. The hunt is state all the way down — what matures the host's poll that the child's tick lacks (association/session progress from a radio that never arms).
*Adversarial corrections to own record (this pass):* (1) consumer→`0201CF54` edge withdrawn above (over-wide range scan); (2) host-`02176118`-nonzero withdrawn above (wrong struct); (3) Thumb/ARM mode tax paid twice more (host ROM bytes, `0201C554` region) — decoded live RAM instead. No source changes anywhere to review; the only worktree edits since p44 are doc entries + the removed staging (why-not note verified present, hunk verified absent).

##### Maturing input named: the guest's radio never arms 2026-10-13 (`logs/md_app.log`, `logs/sfc_dp_host.log`; no source changed)

*Route:* fresh BOOT, driver reached host role on P1 (role=1, assoc accepted, AID assigned, 29993×0x00 polls, joiner replies collected 4910×) but no 0x04 — the parked state, reproduced. P2 = undriven second cart (download 0 bytes, "not booted").
*Host P1 (progressing):* `wifiio` role=1, USCOUNT ticking (`E7D7E0`), zone gen 1841, 46-byte 802.11 frames handed and consumed (79k wireless-RAM reads), RX buffer live; `probe` tx 27745, collected 4548, IPC 9sends 28196 — all growing.
*Guest P2 (parked):* `wifiio` role=0, USCOUNT=0/USCOMPARE=0, zone gen 0, handed nothing, RX buffer all zeros, wireless-RAM reads pure A5A5/5A5A virgin checkerboard (12k reads, all `o`utside any frame); `probe` mp all zero, IPC 9sends frozen at 86 (init-only); ARM9 oscillates between an IRQ-off spin at `02030050` (NitroSDK disable/restore-IRQ helpers: `MRS/BIC/ORR/SVC`) and `02030488`, ARM7 idle at `115C` like the host's.
*Verdict:* the missing input is the radio itself — the guest's game never initialized its wifi block (no RX buffer, US timer never started, role 0), so its tick consumes nothing while the host's poll consumes live RX + association progress. The state-all-the-way-down thesis (p53) holds: no spawner, no waiter, no armed radio — each layer waits on the one below.
*Boundary (honest):* today's P2 is the undriven cart, not a booted DL image — this names the stall CLASS, not the DL-guest-specific tick. The same `wifiio` treatment is still owed on a session where P2 runs download code (see p55).

##### DL guest diffed live: its wifi clock STOPS after boot 2026-10-13 (`logs/md_app.log`, `logs/sfc_dp_host.log`; no source changed)

*Route:* booted with `MD_DP_PLAYER_BODY=1` — the UsernameReply was handed (04 01 07 ×3+) plus 581 RsaReplies, host still parked at 0x00: the arm does not advance Recruitment. Driver FAILed on its beacon-tx check (reads 0) while the joiner HAD associated (beacons 9, polls flowing) — the driver's check is misleading, session was fine. Manual Cut Off tap (91,178 on P1) started it: 0x04 streamed, download 1260340/1260340 B in 2502 packets, **P2 BOOTED**. (Also: my earlier "driver exit 0" reads were tail's exit, not the driver's — pipe without pipefail. Both noted so the next pass doesn't trust either.)
*BOOTED guest P2:* role=2 (client), RX buffer posted (begin 4000/end 4800), one 50B fc=0228 frame handed — but USCOUNT frozen at `0x319DEFC` across 3 samples/4+ min, zone gen frozen 1677, same frame seq `5AF0`, consumed=0, body virgin A5A5, game reads empty; mp all zero; IPC 9sends frozen at 17 (the p42 inits, no #18). ARM9 RUNS (0201CD34 → 020085A8 table-scan loop → 0201C798), ARM7 idle at 115C like the host's.
*Host control P1:* USCOUNT ticks (`5F2BF00` → `68605A8` in 25 s), zone gen 14129 → 15333, tx/collect/IPC all growing.
*Verdict:* the guest arms its radio at boot then its wifi microsecond clock stops — RX rots in the descriptor, the game's poll starves, nothing is ever sent. The DL-guest-specific tick is a DEAD CLOCK, one layer below every earlier suspect (tick, consumer, queue, radio-armed). Next hunt: why the clock stops — guest ARM7 wifi driver never (re)starts the timer post-boot, or emulator Wifi timing keys off something the guest never triggers. Both ARM7s sit at 115C, so the difference is under the idle loop: HW-timer/FW state, not game code.
*Playtest note:* sfc-dp-host.sh FAILs working sessions on its beacon-tx read (left as-is deliberately — verdict logic mid-hunt; next pass may make the beacon check fall back to joiner counters).

##### Mission premise falsified: the identity re-seed already exists and fires 2026-10-13 (`logs/md_app.log`; no source changed)

*Cause of the error:* p58 read the wifiio "last writer per offset" table as the whole register truth — it records game-initiated accesses only, while the re-seed writes by direct `IOPORT` store. The table showed the loader's zeroes; the live registers showed the re-seed's values. Lesson: ring attribution is access-path-specific; cross-check live regs before claiming a value absent.
*Live counters (fifth BOOTED run, same 2502/1260340):* ARM7 `04800020` = `11BF0900 00003322` (carried BSSID live), `04800050` = `48004000` (window live), `048000E8` = `0` (US gate still clear — game's own, correctly untouched); log `re-seeded 151 frames after the hand-off` exactly once; park identical (mp 0, IPC 9sends 17, USCOUNT `20DC2FC` frozen, RX descriptor unconsumed).
*Adversarial review:* no source changed this pass — correctly, since the requested change already exists (`ReseedDownloadPlayLink`: one writer per hold, reset-cleared, game-window disarm at +600f, silence-gated so it never fights an active bring-up; reviewed, no defect found). The remaining gap is the one the 2026-10-12 note named: host frames into the child's RX FIFO (guest DP client), not more seeds (see p60).

##### Frames reach the bus, never the game's FIFO — drop point is the wifi-block gate 2026-10-13 (`logs/md_app.log`; no source changed)

*Route:* sixth BOOTED run (same 2502/1260340). Re-seed confirmed firing again (window `4000/4800`, log line present).
*Bus level (works):* health p2 `rx 39950 (+1496/5s)`, `tx 19301` — the joiner speaks for the child (`Speak` posture), link traffic flows both ways; 199 `beacon out` vs 1 `beacon accepted`.
*Game level (dead):* health p2 `[game 1623 (+0)]` — zero new frames to the game while drain climbs; `mp p2 hw: pwr 0200/0002`, `uscnt 0`, stale `cmd seq 0EA0`; wifiio RX writecursor `03F6` → identical 30 s later, USCOUNT `1EE87C4` identical, gate regs identical (`RXCnt 8000` ✓, `PowerTX 2`, `PowerState 0200` bit9 = RX-off SET).
*Verdict — falsification with counters:* host frames reach the console's link layer but never the child RX FIFO; the CheckRX power gate (`PowerState` bit9, computed from the game's own parked power regs) plus US-gate 0 and IE 0 stop them, faithfully. The stale seq-`0EA0` descriptor is all the game ever got. Next: the guest DP client — something must drive the game's join (or deliver bus frames past its parked power state), and every bypass so far has been correctly refused.

##### Clock-freeze cause closed: the guest's own gate bit is clear 2026-10-13 (`logs/md_app.log`, `logs/sfc_dp_host.log`; no source changed)

*Mechanism (code):* the emulator advances `USCounter` only under `W_USCountCnt` (`src/Wifi.cpp:3159-3161`); the write path is game-only (`:4563`, masked to bit0), and `src/Wifi.h:337` already names it "the game's own us counter gate". A frozen USCOUNT with a clear gate is hardware-faithful, not an emulator timing defect.
*Live (fresh BOOT, 2502-packet byte-identical transfer):* guest `W_USCountCnt` (`048000E8`, ARM7) = `0x00000000`, host = `0x00010001` (gate + compare both set). Guest USCOUNT frozen at `0x1FDDB6C` across 20 s (different freeze value than p55's `0x319DEFC` — the counter runs during bring-up, then stops when the driver parks), zone gen frozen, same unconsumed RX descriptor; guest ARM9 in its table-scan loop (`020085AC`, IPC still 17), ARM7 at BIOS idle.
*Verdict — emulator timing FALSIFIED with counters:* the dead clock is the guest driver's own park decision (state 0, never sets the gate; cf. history v0.1.9dl's zero `W_USCount*` reads and TRX-ON 0). Poking the gate would bypass the game's decision — unfaithful, correctly not attempted. The hunt stays upstream: why the guest's ARM9 never starts a session (no send #18) and its ARM7 never re-arms. Next discriminator: what the guest ARM9's session-start path waits on (its IPC peers are drained and open, p52; its RX descriptor rots, p55) — i.e. back to game code, not the wifi block (see p57).

##### Waiter named: the guest polls a 2-node session list nothing ever extends 2026-10-13 (`logs/md_app.log`; no source changed)

*Route:* third BOOTED run (2502-packet identical transfer again — reproducible). Guest ARM9 wanders `020085Bx` scan code ↔ `0201Cx` list-walk code; IPC 17 frozen; ARM7 at BIOS idle.
*The list at `02179880`:* 16-byte nodes, two live then zeros — `[02169B78 02179890 02024AA1 0217989C]` / `[02173704 02024A51 02179880 021E8920]` (words 3 are Thumb callback pointers, words 2/4 link fields; r5 held `0217989C`, a link field, at one sample). State words `021761B8..C4` (written by the `0201C980` walker) hold live values `95 00 01 00 / 00 00 00 01` — set, motionless. List byte-identical across 25 s+ samples; **memwatch 50 s: 0 stores**. My decode of the exact scan predicate (`020085A0` loop) is rough — stated as pattern, not fact: recurrent execution over a motionless list while emitting nothing.
*Verdict:* the session-start path waits on inbound session traffic — a list extension / collected session that only the MP receive path can deliver, and that path is clock-dead (p56: game-owned gate clear, RX rots, consumed=0). Every layer now points one way: parked radio → no RX → no session entry → no send #18 → host never sees a guest → download present but game never starts.
*Not attempted (correctly):* delivering the rotting frame or setting the gate — both bypass game-owned state / hardware-faithful guards. Next: why the guest driver parks instead of joining — its bring-up inputs, not its outputs (see p58).

##### Decision input named: the association identity is wiped after the hand-off and never re-seeded 2026-10-13 (`logs/md_app.log`; ring captures quoted inline, scratch removed; no source changed)

*Route:* fourth BOOTED run (same 2502/1260340 — reproducible). Full `wifiio` rings captured both consoles: guest 33k accesses, last touch 77 frames before report (nothing touches the block after boot); ring holds the boot burst (all frame 6774) plus 4 unattributed post-boot power/mode writes at frame 6894 (pc `0000115C` — author unnamed in the record; power/mode only, no session identity).
*Guest bring-up in order:* RF/BB config sweep (`W_BBWrite`/`W_BBCnt` 5062→5068, BB-busy polls at `037FB8A0`), own MAC set (`0900/12BF/4366`), `ModeWEP 0002→000A` — then park: `W_BBPower←800D`, `W_PowerUS←block-off`, and it never writes BSSID/AID/RX-window/US-gate/IE. Last writers: BSSID `0000` ×3, AID `0000` ×2, RXBegin/End `0000`, US-gate `0`, IE `0000` — all zeroed by its own table loader (`027EBA80`, frame 6774).
*Host control (same fields):* BSSID `0900/11BF/3322`, RX `50C4/5F60`, US-gate `1` (game pc), IE `703F`, PowerTX `0003` (TRX on).
*Verdict (CORRECTED p59 — the "never re-seeded" half was wrong):* the loader DOES wipe the carry at frame 6774, but `Wifi::ReseedDownloadPlayLink` (every frame from `USTimer`, `src/Wifi.cpp:3054/3402`) re-applies BSSID + RX window + power — its writes use direct `IOPORT` stores that bypass the reg ring, so the "last writer" table (game writes only) hid them and the record misread absence. Live proof this pass: BSSID reads `0900/11BF/3322`, window `4000/4800`, log `re-seeded 151 frames after the hand-off` — and the park persists anyway (mp 0, IPC 17, USCOUNT frozen). The identity was never the input; the blocker is downstream (RX delivery into the game FIFO / guest DP client, per the 2026-10-12 whack-a-mole note). Do NOT re-propose an identity re-seed — it exists.

##### Flag poke falsified: no waiter exists for the queue flag 2026-10-13 (`logs/md_app.log`, `logs/sfc_dp_host.log`, `logs/p52_run.log`; nothing poked left — restored verified)

*Route:* fresh BOOT, driver exit 0, transfer byte-identical, `BOOTED`. Baseline: flag `[027FFF88]` = `0x00027DC0`, queue `021761F0` all zeros, IPC 17/12562, pc9 park-cycling, pc7 `115C`.
*Watch (60 s, ~1400 frames):* memwatch `[027FFF80–027FFF8F]` 0 stores; pctrace `[02014660,02014690)` 0 visits. Nobody touches the flag in steady state.
*Poke bit0 (30 s, ~2400 frames):* flag held `0x27DC1`, unconsumed — 0 dispatch visits (span 7833..10274), IPC still 17/17, ARM7 still `115C`.
*Poke bits 0–7 (`0x27DFF`, 30 s):* same — 0 visits (span ..11263), IPC 17, ARM7 `115C`, flag intact. Restored to `0x27DC0` (verified readback), zero faults (`undefined/data-abort/prefetch` grep = 0), session healthy.
*Verdict:* no waiter exists for this flag on the parked child — set bits are never cleared, dispatch never fires, ARM7 never wakes, send #18 never emits. The queue has no waiter: the OS thread that should block on it was never created on the DL path (thread creation still unlocated; `02013024`'s pair ruled out). Next: thread creation — what creates OS threads post-boot on the host cart path that the DL child never reaches (SDK `OS_CreateThread` caller in the host image vs child image diff), or the VBlank/task scheduler that should spawn the entry.

##### Writer hunt closed: registration is write-once, SDK band adds no dispatch path 2026-10-13 (stock binary, `logs/md_app.log`, `logs/sfc_dp_host.log`, `logs/p47_run.log`, `/tmp/p47_sdk.bin`; no source changed)

*Route:* fresh BOOT, driver exit 0, transfer byte-identical, `BOOTED`. Stock binary (indicator 2, tail skipped — and the struct proves it: `02175DC0+` holds the HEAD's pool values `02014615/02014669/02014611` with `+0x58=FFFFFFFF`, i.e. the second init never ran here — the staged-vs-stock struct diff is itself a live signature of which inits fired).
*memwatch on the registration (`02175DC0–02175DDF`, interpreter so every store is seen):* **0 stores over 90 s (~5400 frames)** after boot. The dispatch pointer sits unwritten in steady state — nobody re-arms it, nobody consumes it. (Protocol kept: bare `memwatch --player 2` to read; arming clears.)
*Bigger dump (`/tmp/p47_sdk.bin`, 45 KB covering `02100000–0210B400` — the whole ARM9 image past the 1 MB dump, pulled live in 12 verified 4 KB reads):* no `bl` from the SDK band to the consumer chain, no new refs to the registration struct or its entries. New: an 8-function family reading SDK-global `021B3A94` (pools at `02100BAC…02101458`) and one user of wait-word `021B3508` (pool `021000A8`) — the tick's inputs, all reading live-nonzero, so the tick advances and waits.
*Verdict — falsification with counters, both halves:* there IS no post-boot writer to name (0/5400 frames), and the complete static map (1 MB + 45 KB) has no second path to the entry. The wakeup the mission sought doesn't exist as a writer: dispatch needs a state transition (association/link event) the parked game never reaches, because the game never sends (ARM9→ARM7 still 0). The hunt moves from "who writes the pointer" to "what link event the tick waits for" — the host's 0x00 Dummy polls flow past an unarmed reply slot, so the child's radio never hears anything it answers. Next: what the child's RX path needs to first read (beacon/command the host emits that the child ignores), or the ARM7 bring-up that never arms TX. Quit clean.

##### Guest joins: the firmware handoff block is staged from the live beacon 2026-10-14 (source changed, gates green, lobby live)

*Route:* six BOOTED runs (2502/1260340 every time), then the fix, then a seventh run to a two-player named lobby. Falsifications folded in along the way (`logs/p61_run2.log` timeline, `logs/p61_run3.log` retry arm, `logs/p61_run5.log` misaligned staging, `logs/p61_run6.log` lobby; shots `p61_booted`/`p61_r5boot`/`p61_r6live2`).
*The guest's own words:* first BOOTED screen read by OCR is not a park but `"Connection error. Please turn your DS off."` (`logs/shots/p61_booted.png`), host meanwhile `"Please wait a moment..."`. Timeline (`logs/p61_timeline.log`): `"Please wait"` digest at +5 s, error digest by +10 s, USCOUNT frozen from bring-up residue, `PowerState` 0-read-then-`0200` is the re-seed's mask expiring at its +600f disarm — the game never powers its radio: post-boot bus shows zero auth/assoc/scan TX from the game, one Dummy poll delivered, `game 1623 (+0)`.
*Retry-notify falsified (`MD_CMD_RETRY_NOTIFY=1`, env-only, `logs/p61_run3.log`):* `game 1848 (+225)` — the game reads the repeats and still errors in <10 s. Starvation was never the input; knob stays a diagnostic, off by default.
*Cause (GBATEK BIOS RAM Usage / Download Play / Beacons):* the 60-byte block at `027FFC42h` the `02018F2C` consumer copies whole was staged as 7 u32 guesses at wrong offsets (BSSID +4, channel at 5Ch for 78h, caps at 5Eh for 6Eh, size/RSSI/interval/listen/rates zero) — a validator fails pre-radio, hence no TX. Fix threads the live beacon through `Session` → `MDDPSession` → `DownloadPlayBootInfo` (serial, token, capability, interval, rate mask, DD-derived size `(dd-8+0x41)/2`) and stages all 60 bytes at GBATEK offsets; RSSI is the only constant (`0082`, commented — the bus models no signal). First cut wrote words (`ARM9Write32` aligns down, block landed -2, indicator read back `0061`, child wedged in BIOS) — caught by readback, fixed to halfword writes (`logs/p61_run5.log`). Unit tests pin the derivations (`rates_mask`, `beacon_size`, session carry); `cargo test --lib` 45/0, `md_smoke_test` 166/0.
*Live proof (`logs/p61_run6.log`, `logs/shots/p61_r6live2.png`):* handoff stages `size 0061 … bssid 00:09:BF:11:22:33 … serial 00400218 … caps 0021 rates 0003/0003 interval 00D1 channel 0007`; guest `window reads 4842693 (+35195/5s)`, `arms 34627`, `cmds rx 40536`, game-owned 70-byte UTF-16 `"melonDS"` replies; LINK ACTIVE both consoles (host `game 40292`, guest `game 82778`, both climbing); taps on P2 move P1's screen — shared lobby state. Stand-in retired by the `wireless_on` gate.
*Remaining gap (unchanged, now unblocked):* pre-match menus (SHIELDS/CONTROL, icon-only buttons ~y172–184) were not driven to gameplay — next pass scripts that route and asserts both screens leave the lobby. Single title (ASFE), single success session; no repeat-run proof yet. Quit clean, disk 53%, scratch removed.

##### Match start: both consoles fly the dogfight, route scripted 2026-10-14 (drivers changed, no source change)

*Route:* `sfc-dp-host.sh` exit 0 (transfer streams `0x04`, `BOOTED` 2502/1260340, mask `0x3`) then new `tools/scripts/sfc-dp-launch.sh`: after boot both games enter setup alone — SHIELDS, range Short/Normal/Long, CONTROL TYPE Normal/inverted, roster `PLAYER NAME WINS SHIELD` naming `melonDS` on top screens — and each is confirmed by the LEFT bottom icon DS `(64,179)`. Option rows only toggle the cursor (`_`/`{`, taps and A/START never confirm; START backs out of staging to setup). Two driver bugs fixed along the way: `sfc-dp-host.sh` grepped the app log for a `link-trace … fc=0080` line the app no longer prints, so it failed healthy runs at the beacon check — it now reads the joiner's heard-beacon count off `dpjoiner` (exit 0 twice since); the launch verdict counted all text bands, but the 3D map keeps 7 weak bands — it counts strong bands (score ≥ 30) after a 25 s settle (setup 4+, `Please wait` 1, match 0).
*Hard-won warning (one session lost):* the RIGHT icon `(196,179)` is Back/quit — it raises `This will terminate your connection`, whose LEFT button proceeds with the disconnect (mask `0x0`, both games `Your connection has been terminated. / Please turn your DS off.`, `logs/shots/p62_k10–k11.png`). Never tap right; never tap left on that dialog.
*Live proof, twice (`logs/p63_app.log`, `logs/p64_app.log`; shots `p63_lobby2`/`p63_n2`/`p63_Dup`/`p63_final`, `p64_check`/`p64_up2`):* both runs BOOT → setup → 3D (top cockpit, bottom tactical map, no text, 150–165 KB shots). Steering: host UP-vs-DOWN views differ 24.96, guest UP 18.83 (p63); repeat run 10.66 after a ~2 min launch settle (ships start parked, then free flight). LINK ACTIVE throughout, frames lockstepped (~33k), game counters climbing both sides (host `game +240/5s`, guest `+499/5s`), no teardown. OCR (`tesseract`, 2–3x upscale) reads the stylized font fine — `SHIELDS`, `CONTROL TYPE`, `Please wait a moment...`, roster — and is now the menu-driving instrument; blind-digest tapping cost a session (digests animate, so CHANGED proves nothing — compare text-region pixels or layouts).
*Remaining gap:* single title (ASFE), two sessions, dogfight flown but no shots fired (no fire button found — A/L/R/X dead, Y mild, B transient 20.91 unreproduced) and no visual cross-proof (ships never co-located for a screenshot). Quit clean, savs unchanged, disk 53%, probe shots pruned to cited files.

##### Green launch plus fire sweep; guest attach is flaky 2026-10-14 (scripts only, no source change)

*`sfc-dp-launch.sh` went green end to end (`logs/p68_launch.log`, LAUNCHED exit 0):* BOOT → guest signature in 20 s (7 strong bands) → confirm → `Please wait` (1 band) → host signature → confirm → both 0 strong bands → 3D match, mask `0x3`, frames lockstepped (~18k), game counters climbing (host `cmds tx 39325`, guest `answered 30467`, window reads 3.7M), no teardown (`p68_final.png`). Confirms are gated on the setup signature now — the script taps nothing unidentified (see p65 lesson below).
*Fire-button sweep, live match, P1 top diffs across 2.5 s holds (no-input baseline ~4–6):* A 7.50 (dead), L 32.78, R 24.21, X 18.10, Y 19.29, B 17.02. Y held: full-view diff 37.36, hot pixels 4641 → 6624 (+43%). Every button but A visibly maneuvers the ship; none is proven to be *fire* (view swing ≠ projectiles — L/R look like rolls, Y like boost; laser bolts unobserved). Named result: all-controls-respond, fire unlocated.
*Guest attach flakiness, the session lottery (`logs/p65–p68_app.log`):* p63/p64 linked, then p65/p66/p67 failed identically — guest game blips its radio 3× in ~15 s post-boot, reads ~1700 frames, lands on `Connection error / Please turn your DS off`, host idles into its own error (mask `0x0`; see the frozen `game ~1700` health lines and deauth counters in `logs/p65_app.log`). Token/interval ruled out (1900 appears in both a success and a failure); rebuilt binary is byte-size-identical to the working one; sav md5 unchanged; stand-in chatter identical (3 lines) in all runs. p68 linked on the fourth try with no changes — rate 3/6. Cause unlocated: either a host/guest timing race in the ~15 s attach window or environmental. The p65 failure also proved the gate's worth: the old script tapped a dying 2-band screen (taps pre-signature could hit Back/quit); the gated script (p66) correctly fired zero taps and exited 1. Next: instrument the guest's 4th radio power-up (what it scans for and what answers) or retry-loop the session until the signature appears.

*Discriminator narrowed with frame counts, no code fix (p69 1 s trace, `logs/p69_trace.log`, LAUNCHED exit 0 — second green launch):* at the decision moment host and guest link state are IDENTICAL across outcomes — host tx 8173 vs 8228, collected 3091 both, clientfail ~62% both, deauth 0 both; guest rx 845 vs 843, answered 0 both; host class stays `00` post-handoff in every run; blip cadence deterministic (same log lines every run). The divergence: host cmds tx freezes ~10 s post-handoff in fail runs (8864, +0 forever, one deauth out, `IsMP` apparently dropped so the stop watchdog stays silent) vs climbs monotonically in success (41–56k, with normal stop/resume phase messages); fail ends in simultaneous mutual deauth reason 0003. Ruled out definitively: token/interval (p66 FAIL and p68 LINK share 1900/00D2), binary (byte-identical), sav, stand-in chatter (3 lines always), transfer duration (~70 s always), post-handoff class, ack health at decision. The 1 s trace adds: success-side guest pc9 cycles `CD34`↔`C7xx`/`02xxxx`/`03xxxx`/`04xxxx` throughout (never parks; pc7 alternates BIOS idle with game WRAM code), host tx smooth ~300/s→70/s. Remaining suspects, all sub-digest-resolution: host-poll vs guest-scan micro-timing, guest ARM7 bring-up race, wall-clock reply-window effects on stand-in answers. Rate 4/7. Actionable early predictor: host tx with no climb for 5 s after boot+15 s predicts failure minutes before the error dialog — a retry loop should abort there instead of waiting out the 100 s setup gate. No emulator change made (nothing substantiated enough to touch).

##### Super Mario 64 DS downloads, boots and attaches — the host's START bar was the missing tap 2026-10-14 (scripts only, no source change)

*The v0.1.9cy verdict is overturned, by a host-side tap no client reply could ever be:* with slave 1 associated and collected on ~100% of rounds the host still sat in `00` Dummy at RsaDone — so the gate was never a reply, it was the waiting room's own bottom bar. Tapping DS `(128,168)` (the `TOUCH TO START` bar, text rows y152–184, read off `logs/shots/sm64_t3.png`) with the client registered starts the download where the old route only waited: `last 0x04, slave flags 0002`, then `download: 514896 of 514896 bytes in 1023 distinct packets · BOOTED` (`logs/sm64_app.log`). The old `sm64-dp-host.sh`/`sm64-dp-force-sweep.sh` were never committed and are not in the tree — recreated below with the START step included. Geometry re-measured live: title tap anywhere, VS bar `(37,172)`, waiting room matches the `sm64_vs1.png` reference exactly (bottom mean 175.8).
*Guest attached first try (no p65 lottery this run):* post-boot the child's ARM9 runs inside the image (`pc9 02058ADC`, moving), IPC flows both ways (9sends 9348, 7sends 72815, all received), both games exchange live MP frames (guest `cmds rx 32825/answered 32800`, host collecting client 1's reply), mask `0x3`, frames lockstepped. Guest renders the downloaded game's white logo/menu screens, host advances past the waiting room to a headline + portrait-box selection screen.
*Selection geometry (all DS pixels, measured off `logs/shots/sm64_boot1/c1/c2.png`):* portrait tap `(166,118)` advances BOTH consoles (guest follows the host) to a 2-button confirm (buttons x40–112 / x144–216, y152–184); LEFT `(76,168)` advances again to a converged screen (patterned wallpaper, box + 6-dot row, identical bands both consoles) — and there the session dies: host's last addressed round seq `A270` then one unaddressed `A280`, no deauth ever, both CPUs alive but off-radio (`pc7 0000115C` both), mask `0x0`, B/A dead. LEFT was almost certainly Back/quit, the SFC lesson re-learned on a new title. The RIGHT button was never tried.
*Next:* relaunch, re-drive (script below), tap RIGHT `(180,168)` at the 2-button screen and watch bands + host cmd class; if it confirms into character/stage select, drive both consoles to the arena the SFC way (signature-gated taps only). Route script `tools/scripts/sm64-dp-host.sh` recreated this pass (title → VS → assoc → START bar → `04`/download growth verdicts 0/2/1). Quit clean exit 0, sav unchanged (`ad192e5d`), disk 52%.

##### SM64DS full menu route mapped; post-confirm the host aborts off-radio (2 runs, scripts only, no source change)

*Route, second session (`logs/sm64b_app.log`, script exit 0, BOOTED 1023/514896 in ~40 s):* char select shows FOUR panels (y40–96); P1 `(34,68)` picks panel 1 (others dim on both consoles), P2 `(98,68)` picks panel 2 → stage screen (3D preview top, bubble headline bottom, both consoles). D-pad RIGHT cycles the stage (headline digest changes, idle digest stable); A/START/headline-tap do nothing. After ~60 s settle, P2 headline tap `(128,60)` advances both to the hub (P1: two labeled boxes y56–88 / y104–136, P2: waiting message box — the same text as the post-boot message); an immediate P2 tap does nothing, so the settle is required. P1 box1 `(128,72)` = back to char select (measured); P1 box2 `(128,120)` = forward to the 2-button confirm. RIGHT `(180,168)` = Back (returns to hub, digest-proven) so LEFT `(76,168)` = confirm.
*The wall (`logs/sm64_app.log` run 1, `logs/sm64b_app.log` run 2, identical):* LEFT converges both consoles to the box+6-dots screen and the host aborts within seconds — last addressed rounds (188 B + 50 B, mask 0002) then one unaddressed frame, no deauth ever, `WIFI: OFF` host-first both runs (run 1: p1 L1355820 → p2 L1355975; run 2: p1 L1895104 → p2), mask `0x0`, both CPUs alive but off-radio, both converged boxes untappable post-mortem (diff 0.00). The guest receives the final rounds (rx counters) but its game never visibly advances; run 1's digest shows `canned 25` among its 32800 answers. Reading: the host starts battle-sync and quits waiting for guest game-state the guest never posts — either the guest needs its own timely confirm on the converged screen (never given in either run: run 1 confirmed P1 only, run 2's P2 tap landed post-mortem), or the rendezvous is game-logic this stack cannot satisfy.
*Next (one variable):* third run, LEFT on P1 then P2 box `(128,92)` within 3 s; link alive past 15 s = guest-confirm hypothesis proven, identical death = game-logic wall, record it. Full post-boot route belongs in a new `tools/scripts/sm64-dp-launch.sh` (char picks → settle → P2 headline → hub box2 → LEFT → immediate P2 box). Quit clean exit 0, sav unchanged, disk 52%.

##### The three teardown runs tapped QUIT VS — box1 CONTINUE is the untested path 2026-10-14 (scripts only, no source change)

*OCR correction (`tesseract --psm 7`, 3–5x upscale):* the hub's box2 reads `QUIT VS` (bubble-font `Qu` → `Gu`), box1 reads `CONTINUE:`. So runs 1–3 never attempted a battle confirm at all: box2 → 2-button `Are you sure?` → LEFT = YES = quit (converged box+dots = session-over notice whose line ends `Turn power OFF.`, radios off host-first, no deauth) / RIGHT = NO = back to hub. Every measurement still stands (char picks, stage cycle, settle-gated P2 headline entry, assoc-settle fix) — only the last two taps were wrong. The guest's end state is the firmware's own `DS Download Play Failed! / Turn power OFF and ON to reset system. / Face your DS partner, move closer together, and try again.` (`logs/shots/sm64c_end.png`, OCR-verified) — the standard host-gone dialog, which also clears the P2-tap-killed-it theory for run 3 (the dialog appears whenever the host is gone, tap or not). The guest-confirm hypothesis is therefore untested, not disproven — but it is moot until CONTINUE is tried.
*Script fixes this pass:* `sm64-dp-host.sh` gains `SETTLE_AFTER_ASSOC_S=15` (run 3 proved the same-second START tap is swallowed: assoc + tap parked at `0x00`/download 0, a re-tap minutes later streamed `0x04` at once) and `sm64-dp-launch.sh` now taps box1 CONTINUE and scripts nothing past it (whatever follows is unmapped; the old LEFT/RIGHT taps are documented as the quit confirm). `bash -n` clean on both.
*Next:* run 4 with the corrected script — CONTINUE → observe (battle? final confirm?); confirm P2 only if its screen offers a control. Then the shelf triage (NSMB/MPH/MKDS multi-card stands, LEGO no-MP) and compatibility.md.

### The four-console MPH question: answered - it is the host's own game

**Closed 2026-09-30 (v0.1.9bb), by running the control this section named.** The
control was three radios with only *two* players joined, and then the host's
lobby driven. **A third radio is not the variable.** With the host's game left
sitting on its join screen after `CREATE GAME`, the guest still left ~10 s after
joining when the third console's radio was **up and scanning, transmitting
nothing** (`tx 0`, no commands, no replies, no blanks), when the reply window was
made flawless (`MD_REPLY_KEEP=0`: `collected == cmds tx`, `empty 0 stale 0 blank
0 clientfail 0`), and when the third console **never raised its radio at all**
(bus mask `0x3`, the two-radio configuration this project calls healthy, app at
56-58 fps). With the host **driven** (BATTLE, the ARENA check, a hunter each,
`START GAME`) and the same three radios up, the guest never left: `mask 0x7`
held past the 60 s watch, **zero deauth frames in the whole run**, the two games
exchanging live 42-byte bodies and a changing answer counter, host `cmds tx 8207
/ collected 8201`, guest `answered 7155`. The suspicion this section carried -
that one of the per-host or machine-global members (`MPStatus`, the reply queue
and `ReplyReadOffset[]`, `MPStatus.MPHostinst`/`MPReplyBitmask`, `LastHostID`)
was the trigger - is settled by the code path: the bus is one object per session
(`md::link_bus()`, with `slot` = console index), so that state *is* shared by
all consoles in the process, but with one host and one command sender (guests
send none) that is what the emulated hardware is, `MPReplyBitmask` is read
nowhere, and the code already names the only case where it is wrong (two games
on one machine). Runs 2 and 3 show no anomaly in it and the guest still left, so
re-owning it would change nothing measurable.

**So the road to a 3- and 4-player MPH match is a route, not a link change: drive
the host's lobby as the joins land, not after them.** Every failing route in the
record - v0.1.9az's overlapped and serial joins, v0.1.9ba's decisive run, this
pass's three failures - brought the joins in first and reached the host's lobby
last, leaving the host's game on its join screen while the guests waited; the one
run that held a pair at four consoles already drove the host. The instrument is
`tools/scripts/mph-two-guests.sh` (`WALK_PLAYERS`, `PHASE_B`, `DRIVE=1`), which
takes every step decision from the emulator's own state. Still open, and now one
question rather than five: **three guests at once, with the host driven between
joins, into a real 3- and 4-player match.**

**2026-09-30 (v0.1.9bi): the route was driven as the joins landed and the guests
still left, so the next question is what *their* games are waiting for.** Three
consoles, `tools/scripts/mph-match.sh`'s paced route (`logs/v9f_lobbyh_3p_app.log`,
held before `START GAME`): the host walked its ladder while both joins landed,
its round loop ran and collected **every** answer either guest wrote (744 of 744,
597 of 597, `clientfail 0`), and the host's own beacon carried **three party
entries** for the whole time both guests were in the room (34 and 38 delivered
room beacons) - and then each guest's own game wrote its own deauth (reason 0003)
and left, 18 s and 17 s after joining. The pick never registered on the host's
roster (`pill 0` for 15 s twice, `icons 0`) while the guests' own screens read
`page=HUNTER pill=69 icons=333` after their picks, so what to look at next is
that divergence - what each game believes about the other's readiness, and which
message would carry it - not the bus, which delivered and credited everything.
A third attempt (`logs/v9g_3p_app.log`) had guest 1 fail to join at all (3 tries)
and the host end up on its own `DIALOG` page. The wireless counts behind all of
this, and the correction of the previous pass's "client beacons" reading, are in
`logs/v9_mp-link-finding.txt`.

**2026-09-30 (v0.1.9bc): the pace fix was run, it holds, and the blocker is now
the route's own last tap.** `tools/scripts/mph-match.sh` drives each join's host
step as the join lands; with it, **three consoles hold `mask 0x7` through the
whole lobby** (host `role=1`, both guests `role=2`, each guest's replies credited
under its own aid, zero deauth frames through the picks) and four consoles reach
`mask 0xf`. The match still does not start, and the reason is visible on the
game's own pixels: `appctl.py shot` composes every console at native 256x192, so
`render --mono` and `glyphs.py` read the game's text, and the host's bottom
screen **at the moment the driver taps `START GAME`** has nothing at that
coordinate (`200,26`; maximum luminance 133 of 255, no text or button in the
region) - that coordinate belongs to the two-player layout. The tap is a no-op,
the host's command bodies stay the 6-byte management form (`FE 00 06 00 00 80`),
and each guest's own game deauthenticates (reason 0003) ~2-10 s later; at four
consoles the guests then evict in rotation and the driver's recovery rejoins each
one.

**Next, and it is a reading job rather than a link job:** find the real `START
GAME` control on the 3- and 4-player host screen (the reader above exists now;
the route's other coordinates - BATTLE, the ARENA check, the hunter columns -
want the same check) and re-run `mph-match.sh` at three, then four. Measured
alongside and deliberately *not* called this blocker: **re-association after a
drop can collide AIDs** - the host hands `C001` to whichever guest returns
(`C001` x6 / `C002` x3 over twelve auth-assoc cycles at three consoles; `C001`
x8 / `C002` x4 / `C003` x1 at four) and both guests' replies are then credited
under aid 1 - a real defect for the pass that gets a match reachable. Frame rate
is not the limit: 36-48 fps per console at four, 46-50 at three.

**2026-09-30 (v0.1.9bd): the reading job was done from the host's own pixels, and
it was not the whole blocker.** Every coordinate on the 3- and 4-console host route
was re-read off the host's own bottom screen (`appctl.py shot` composes each console
at native 256x192), and three of them were wrong for the 3-4 player layout: the
`BATTLE` disc is `(44,68)` (not `52,78`) on the `MODE` page; the `ARENA` page's
green check is `(212,172)` - its own tick's centroid, bbox `x 207..217 y 167..177`,
so the two-player route's `(222,172)` sat on the ring's edge and v0.1.9bc's
`(220,178)` missed it entirely, leaving the host on `ARENA` so the *next* tap opened
**`ADVANCED OPTIONS`** (whose own green tick at `(129,166)` is what every previous
3-console `picks` shot actually showed); and the roster's `START GAME` pill is
`(198,40)` (orange pill bbox `x 148..245 y 28..53`; the two-player `(200,26)` sat 2
px above its top edge). **And a lobby tap cannot be verified by a picture digest**:
each console's lobby bottom screen changes ~83% of its pixels per emulated second
(animated film background), so `waitfor --settle` never fires and `waitfor --change`
is always true - measured, not assumed. What does work is the host's own
**tab-completion dots**, three cyan marks at `(67,17)`, `(128,17)`, `(187,17)` that
light 0..3 as `MODE -> ARENA -> HUNTER -> roster` complete; `mph-match.sh` reads them
(`B>170 and B-R>70` in a 9x9 crop) and fires each host step only when the previous
dot is lit, so the ladder is idempotent. A *guest's* tab row never lights a dot at 2
or 3 consoles, so guests use two spaced taps rather than the dot ladder (the
dot-verified guest step burned ~17 s of retries with the host idle, long enough to
start dropping guests).

With the corrected geometry **a real 2-console match runs**: the driver walks the
host ladder with every dot confirming in ~14 s, then both consoles' own `RANK 1`,
`POINTS 0/7`, `TIME 6:47` counting down from the 7-minute limit, player 1 green HUD
and player 2 yellow in the same `COMBAT HALL` view (`logs/shots/healthy2.png`), `mask
0x3`, the guest still up at 113 s. **3 and 4 consoles still fail**, now with a clean
host: the host ladder completes with all three dots lit (`mask 0x7` through the walk,
`deauth-sent 0` at the roster), then each guest's own game deauthenticates (`reason
0003`) seconds after its hunter pick and `mask` collapses to `0x1`; at four the same
at `mask 0xf`. **The in-game marker this section suggested does not exist for this
title**: through the *working* 2-console match the host's command bodies stay 6 and
38 bytes (`logs/mphB_app.log.gz`), identical to the lobby - only 2-console NSMB shows
262-byte bodies - so "bodies left the management form" cannot verify a match here;
the shots do.

**Answered and cleared: the AID collision is not the 3-console blocker.** With the
corrected route at three consoles the host synced cleanly (`[HOST] syncing client
C001` + `C002` twice each; `[CLIENT 1] host sync` and `[CLIENT 2] host sync` twice
each) - v0.1.9bc's collision is a *re-association* artefact and needs a returning
guest while another is still present. No change was made to AID bookkeeping.

**New lead, measured and deliberately not fixed:** in the failing 3-console run
console **p2 received 191 of the host's 160-byte session beacons but only 151
distinct sequence numbers - 40 duplicate deliveries** - while p3 received 195, all
distinct (`logs/mphC_app.log.gz`); in the healthy 2-console session p2 received 1035,
all distinct. The guest's deauth follows one such beacon reaching its own software
within a frame. The delivery site is `Wifi::StartRX` (the "a held MP frame whose
timestamp came (USTimer)" entry of the delivery-site table at `src/Wifi.cpp:1772`),
i.e. the emulated bus hands the same held frame over twice; the hold/`SetMPIoZone`
state that would do so was not reproduced under a debugger, so no speculative change
was made. **Next: isolate the duplicate delivery in the held-frame path, then re-run
the corrected 3- and 4-console route.**

Also this pass: a playtest of the app's own control surface (34 flows,
`logs/ui2_playtest.log` / `logs/ui3_playtest.log`) - empty states, bad and
space-padded ROM paths, a directory as a ROM, double-clicks, `reload()` mid-session
and mid-boot, out-of-range console indices and throwing `eval`s all behaved. The one
real defect found: `click` on a missing selector or a disabled control returned
success, so a scripted no-op looked identical to a working step. `D.click` in
`src-tauri/src/lib.rs` now throws like `D.key` (rebuilt and re-verified).

**2026-09-30 (v0.1.9be): the duplicate-delivery lead is cleared by a counter, and the
remaining blocker is the route, not the link.** The v0.1.9bd lead - one console taking
191 of the host's 160-byte beacons with only 151 distinct sequence numbers - is **not a
delivery defect**: every one of the 40 repeats lands exactly **80.1537 s** after the
first (the host's 12-bit 802.11 sequence counter coming round), and every one of the 191
hand-overs carries a **distinct** bus timestamp, so no frame was handed to that console
twice. `Wifi::StartRX` now takes the timestamp the shared bus carried the frame with and
keeps the last eight `(stamp, length, fc, seq)` tuples it has handed to that console's
game, counting any repeat in `MPRedeliveries` and naming it in a WARN line; the hold
keeps its own **un-clamped** stamp (`MPHoldStamp`), because `RXTimestamp` is raised to
"now" when a frame arrives late. The counters ride the existing once-a-second `[mp]
clock probe` line. Measured: **two consoles 7,341 hand-overs with 0 redelivered**; **three
consoles 16,494 (p2) and 12,172 (p3), 0 redelivered**; and with the reference arm
`MD_MP_REDELIVER=1` (forwarded by `run_app.sh`, same shape as `MD_STOCK_MP`/`MD_MP_FCS`)
that hands one held frame over twice on purpose, **239 redeliveries in 258 hand-overs**
with 266 named lines - so the counter demonstrably fires when the condition is real.

**With the delivery path cleared, the failure is on the host's own pixels and it is the
route.** In the three-console run the lobby was the healthiest this project has recorded
right up to the start tap (`mask 0x7`, host `role=1`, both guests `role=2`,
`deauth-sent 0`, every join first try) and then evicted over ten seconds
(`mask 0x7 -> 0x1`); the shot at that moment (`logs/shots/mph3_picks.png`, host read as
"dot 3 of 3") shows the roster's `START GAME` region (x 148..245, y 28..53 of the host's
bottom screen) **empty - maximum luminance 61 of 255, zero orange pixels** - against the
reference roster `F_start.png` there at **182 with 832 orange pixels**. `mph2_picks.png`
reads 65 and zero orange. So the host was **not on the roster when the route tapped it**,
the tap was a no-op, its game never left the lobby, and the guests' own games deauthed ~10
s later (p2's own `reason 0003`, then the host's `reason 0007`). The dot oracle is why the
route believed otherwise: it saturates at three, so any page at or past the roster reads
"3", and `host_step`'s retries on the HUNTER page (one tap selects and one confirms) can
push the host past the roster onto `ADVANCED OPTIONS`. **This screen-verification job was
done in v0.1.9bf** (see below): the route is now driven by a page classifier, not the dots.
Not achieved and recorded plainly: **no match started at two, three or
four consoles this pass**, so v0.1.9bd's two-console match is not re-demonstrated here (both
attempts failed the same way - the host ladder's retries cost ~25 s, the guest deauthed
while it waited). Nothing else was changed: the AID collision stays a re-association-only
defect and no speculative change was made to the delivery path.

**2026-09-30 (v0.1.9bg): the two-console control is re-established, and at three consoles the
second guest's join is *not* refused - the host serves it and then abandons its own round
loop.**

**The control first.** v0.1.9bf's oracle called a *real* match a dialog: `TOPCOLOUR_MIN` was
10000, taken from in-play frames, so a match's `LAWS OF BATTLE` briefing (6388-6551 colourful
pixels) read as `DIALOG`, while the dialogs it was built for read 915-928 and play reads
16384-32768. With it at 3000 the live two-console match (`SPAWNING IN 5...`, `RANK 1`,
`TIME 6:54`, distinct green/yellow HUDs, `logs/shots/ctl2.png` / `ctl_match.png`) classifies as
`INGAME` on both consoles, and it is in the selftest - now **17 labelled frames, 0 wrong**. The
route gained `STOP_AFTER=ready` (every guest picked, host on its roster, before `START GAME`)
and its two guards were fixed: both tested `-n "$STOP_AFTER"`, so `ready` was swallowed by
`joins`.

**The three-console question, answered from the wire.** A second guest is admitted, addressed
and credited, every round, for as long as the lobby lives: association -> `MPMYAID=2`; each
round offers the poll to *both* clients with the right AID (`AID 1 (from the association;
W_AIDLow reads 0)` / `AID 2 ... (associated 2: this poll is ours)`), each answers from the arm
it made for that poll (`arm #2429` / `#326`), the host collects **both** (`collected a reply
from p2 for aid 1` + `...from p3 for aid 2`, `collected=0006 of mask 0006`, `has everyone it
asked for`), the combined ack goes out with `clientfail 0`, and both clients' games receive it.
Beacons reach **both** clients at 100% in every steady second, counted by the frame's
own `t=` stamp over the whole run - and that finer measurement **refuted this pass's own first
reading** of "1 of 6", which was an artifact of bucketing by line number inside a teardown
window.

**How the group ends, and the counter that shows it.** Inside 6 ms: the host's round loop -
60.0 commands and 60 acks per second, rock steady - stops mid-second (28 of 60) and **never
resumes**, while its
beacon keeps firing 4-5 per bucket for the rest of the session and its own `role` stays 1
(host's last ack `t=84 462 514`, its beacon `84 465 330`, both guests' `deauth sent ... reason
0003` on the wire at `84 468 018`/`84 468 100`). Each guest's own chain for that last beacon: it
is delivered as a 160-byte `fc=0080`, `beacon accepted (our BSSID): USCOUNT 2CB584C ->
2CB8E8C` (so its clock *is* synced), `USCOMPARE0 <- 9001` (`the beacon countdown's IRQ14 is
blocked until USCOUNT reaches it`), `beacon IRQ14 fired (source 0) with no beacon slot armed`,
then the deauth out of `slot 2 (LOC2)` and `wifi: TRX power OFF`. The host's game *receives*
both deauths (`p1 rx bcast ... fc=00C0`, delivered "as an MP host") and then stops polling. So
**the refusing condition is the host's own game abandoning the round loop seconds after the
second client associates** (the injected inputs either side of the death are 9 s apart,
10:57:53 and 10:58:02, and the deauths are inside that gap; an earlier run measured 3-5 s), and
the counters are the host's `cmds_tx`/ack rate going to zero
while its beacon IRQ14 path keeps firing - the fork's beacon is not gated on the game's round
loop, which is why a beacon every 53 ms and `role=1` outlive a lobby that no longer exists.
Refuted with counters, one at a time: beacon starvation (100% reception to the death),
reply-window starvation (`collected == cmds_tx` per client, `clientfail 0`, `stale 0`), AID
mix-ups (the `AID ... this poll is NOT ours` trace read the raw `W_AIDLow` and lied about polls
the code correctly answered - **instrument fixed** to print the resolved AID, including the
`MPMYAID` fallback), the frontend's watchdog (`kicks 0`, `stall kick disabled
(MD_KICK_AFTER_WINDOWS=0)`), and our own synthetic input (the app's own `[control]` log has **no
input at all** between 10:57:53 and 10:58:02, with the death inside that gap - which is the
confound v0.1.9bf left behind when both guests' screens showed `YOU HAVE BEEN DISCONNECTED
FROM THE HOST.`). **The host's own view, instrumented (the pass's second half).** The next question was the host's
side of that moment, and whether the loop can be made to survive it. `MPDiag` gained a round
ledger and `Wifi::USTimer` a watchdog: a round is one *transmitted* CMD of this console's own
game (`MPLoopAlive` latches the stretch, `rounds_multi` counts the rounds that addressed 2+
clients, `client_joins`/`last_client_join_us` time the newest association), and two seconds
without a round while this console is still a host prints **one** Info line naming the moment:
last seq, when, ms since the newest client associated, rounds sent (and how many addressed two
clients), the lobby's CMD-writing lifetime, `acks_sent`/`acks_client_fail`, and whether the game's
beacon slot is still armed. Exercised live (`logs/di3_app.log.gz`): `last command seq ED20 at
+116.454s, 59758 ms after the newest client associated, after 3795 rounds (405 addressing 2+
clients) over 63.316s. The link is not what ended it: 3795 acks, 6 of them reporting a failed
client, and the beacon slot is still armed...`. Its first cut was wrong in a way only running it
showed - `loop_start_us`/`last_client_join_us` of zero underflowed into 1.8e16 ms - so a round is
now counted at the *transmission* (`MPD.last_cmd_us` nonzero) rather than at the game's slot
write, and the line says so when no client ever associated. **Four three-console runs, one
shape**: the host's game stops writing its command slot (arms stop, and arms had equalled
transmissions all run) and both guests' own `reason 0003` deauths are on the wire inside the same
millisecond, with the beacon still going out from the firmware's armed slot - the game's last
composed frame, byte-identical except the TSF the fork writes - and `role` still 1. The stop
arrived **4.5-60 s after the newest client's association** across the four (5-6 s, ~5 s, ~17 s,
59.8 s), so it is not a fixed join timeout, and **the one route hypothesis worth testing was
tested and refuted**: a client arriving after the host's roster already offered `START GAME` is
not the trigger, because moving the whole join loop ahead of the host's ladder and the picks (so
every guest is in before any pick) died the same way, ~5 s after the second association. Nothing
on the wire precedes it either: every round credited, both replies handed over and read (~30
halfwords each), the last ack's `clientfail` 0, and the guest's own `answer sent` body
byte-identical to the host's `collected a reply` body. The two games disagreed about *state*, not
about bytes - which is why "make the loop survive" is not a change this link can make.

**The host's own bookkeeping, dumped and compared (the pass's third part).** The last question
was whether a value *this link supplies* is what the host's game reads just before it stops, so
the host's own two-client bookkeeping was read out of main RAM across the stop and against the
same game in a healthy two-console session. Found by scanning main RAM for the console MACs
(page `02101000`, sweeps kept as `logs/kw_sweeps.tgz`): one peer record per console - `+0xC8` a
count whose byte at `+0xCA` is the number of clients that console has, `+0xCC` the newest
client's MAC (or the host's MAC, on a guest), `+0xD4` that peer's record pointer, `+0xD8` this
console's own index in the party. **It is correct in both sessions**: the host reads `01` with
one client associated and `02` with two, the MAC is the newest client's, each guest reads the
host's MAC and its own index (1, then 2), and the record is stable across the stop for the rest
of the session. `W_CMDStat0..2` read `0000` at every sample before and after the stop, and every
reply was handed over byte-identically. **And the walk-away is not three-console-specific**: in
this pass's two-console run (`logs/ct2_match.log`) the guest's pick did not land either, the
host's roster never listed it (`pill 0` for 80 s), and the guest was then dropped **four times in
a row** - its own deauth and radio off each time, 07:25:36, 07:26:04, 07:27:58, 07:28:32 - with
no third console in the session at all. v0.1.9bb measured that drop rule on the host's *page*;
here the host was driven all the way to its roster and the guest still went, so what the rule
tracks is the guest's entry landing in the host's roster - a lobby that does not progress loses
its guest at any console count. At three consoles the lobby cannot progress at
all - `logs/dc3_match.log` has the first guest's pick register and the roster offer `START GAME`
(`pill 413` at 05:57:51), then the *second* client associates and the same roster reads `pill 0`
(05:58:33), the ready player gone - and `logs/dk3_app.log` has both guests' own answers stop
carrying state (`00 00 00 00 B8 B6`, from +83.90 s and +93.72 s) while every round is still
credited to both. **So the divergence that precedes the walk-away is in the games' own lobby
state, not in anything this link hands them: the verdict is the mission's fallback branch, and
there is no supplied value to correct.** What 4-player compatibility would need that this stack
cannot give is therefore not a better value on the wire - the link already carries a second
client's bytes, counted and credited - but the games' own lobby admitting a second remote client
at all, which this stack cannot supply from the outside. Raw reads: `logs/bg_memory-vs-2console.txt`.

**No three-console match this pass**, and four consoles were not reached. The raw windows and the
measured rate tables are kept as `logs/dc3_deauth-finding.txt` and `logs/di3_host-loop-finding.txt`,
the memory comparison as `logs/bg_memory-vs-2console.txt`.

**2026-09-30 (v0.1.9bf): the route is now driven by a page classifier, three of our own
instruments were convicted by it, and the blocker has moved to the guests' own screens.**
`tools/scripts/lobby_page.py` reads each console's page from its own pixels - `MODE` by the
BATTLE disc's orange, `ARENA` by its green tick, `HUNTER` by the seven hunter discs,
`ROSTER` by the roster's own ADD BOT ring, `INGAME` by the match HUD's ink together with a
colourful top screen, `DIALOG` for that ink over a plain top screen, `UNKNOWN` otherwise -
and carries its own test (`--selftest`, 15 labelled frames, 0 wrong). The completion dots
are gone from the driver: they saturate at three, so the roster and every page after it read
alike.

*Three defects it caught, all ours.* (1) The ladder's steps named the page they *tap on*
rather than the page they are trying to *reach*, so the BATTLE step read `already at MODE`
and never tapped - leaving the host parked on MODE, which is exactly the state that drops a
guest in ~10 s (`logs/or2_match.log` shows the guest leaving and being rejoined while the
host never moved). Steps now name their destination and tap only from the page exactly one
before it, so a retry cannot walk past the roster. (2) `ROSTER` as the "nothing else matched"
fallback made a **mid-transition frame** - during which a console matches no page at all
(`logs/shots/ref_transition.png`: 0 ink, 0 discs, 0 hunter discs, 0 ADD BOT) - read as the
roster: in `logs/or3_match.log` the host's ARENA step read `ROSTER` one second after its own
tap, the HUNTER step then refused to tap "past" it, and the host was left on its hunter page
for the rest of the run. ADD BOT is positive evidence instead, and clean: **90 orange pixels
on every real host roster** (one player, two, old and live) against 24 for ARENA and 0 for
MODE, HUNTER and the transition. (3) **The match oracle was wrong**, and only a look at the
frame showed it: a host showing *`THERE ARE NOT ENOUGH PLAYERS TO CONTINUE`* over the
in-match HUD reads **2729** ink pixels against a real match's **2730**, because the bottom
screen is the same HUD either way - the top screen is what differs (16473-32724 colourful
pixels for the recorded matches, 915 for the dialog). The oracle now requires both and
reports `DIALOG`.

**Two consoles: not re-demonstrated, and the failure is not the route.** A live session
reached the best lobby this project has recorded - both players listed in the host's roster,
the `START GAME` pill up (`logs/shots/sw.png`, pill 414; the guest's own screen `PLEASE WAIT
FOR ALL PLAYERS TO BE READY`) - and tapping the pill still failed the handshake:
`logs/shots/stm.png` is the host's "not enough players" box and the guest's "disconnected
from the host". A roster that lists a second player and offers START GAME is therefore **not
sufficient evidence** that the guest's side of the join completed. The pill itself is still
the right join test for everything *upstream* of the start (measured directly: it appears
**2 s after a guest's confirm**, host idle, pill 0 -> 414).

**Three consoles: the host is verifiably on the roster and the guests are the blocker.** The
ladder now runs one verified tap per step (`MODE -> ARENA -> HUNTER`, re-reading through two
`UNKNOWN` transition frames), and the host then sits on the roster for the whole watch (ADD
BOT 90, pill 0) listing **only itself**. The reason is on the guests' screens: after `select`
*and* `confirm`, and after the new guarded retry, **p2 and p3 both still read `HUNTER`** -
their hunter chosen, their confirm not accepted - so neither ever appears in the host's
roster and `START GAME` never exists (`logs/or3b_match.log`). The retry is deliberately
narrow: it fires only for a guest whose own screen still says `HUNTER`, never as a blind tap
into a console somewhere else. Four consoles were not reached - the pass's budget went into
the classifier and this run. **Next: find why a guest's own hunter confirm is refused when a
third console is in the group** - that is now the whole distance to a three-player match, and
it is the guests' games rather than our route. The two experiments worth running: (a) the
same route with only **two** guests in the lobby and the third radio merely scanning
`AVAILABLE GAMES`, and (b) a guest-side log around the confirm frame to see whether its
choice reaches the host at all before the retry. Docs and validation for this pass are in
`history.md`; **no match started at any console count**, so the two-console match of
v0.1.9bd remains the last confirmed one.

*The v0.1.9az/v0.1.9ba text, kept as written (v0.1.9bb and v0.1.9bf answered it above):* The open question v0.1.9az leaves is narrow and worth stating as one line: **two
radios in a session link MPH's multi-card lobby fine (three times now); a third
one in the room kills the guest's join.** v0.1.9ba has since closed the route side of it: with the host already
hosting, three overlapped joins and the whole lobby driven in ~8 s, the guests
still fall away, and the host credited every answer it was sent (278/278,
328/328, 457/463) with no deauth ever *received* by a guest - so the trigger is
not delivery, not crediting and not the route. The named candidates now sit in
the code: a reply queue and read offset that are per *host* and shared by every
client (`LocalMP::RecvReplies` files by `aid`), the host's global
`MPStatus.MPHostinst`/`MPReplyBitmask`, the single `LastHostID`, and the blank
reply a *non-addressed* console still transmits into that shared queue
(`Wifi::SendMPReply(nullptr, 0, ...)`, stock behaviour). The next pass should
answer *what the third radio changes* rather than retry the route: the emulated bus hands each
guest only the polls addressed to its AID (`mask 0002, AID 1 … this poll is
ours`), the host's `MD_REPLY_KEEP` window dropped nothing, and the guests answer
at the same per-guest rate with three peers as with one — so the difference is in
what a *peer's* presence does to the answer the host's game receives, or to the
probe/association traffic the third radio's own scan puts on the bus. The two
concrete experiments: (a) three radios on the bus with only **two** of them in
the lobby (the third merely scanning `AVAILABLE GAMES`), which separates "a third
radio's traffic" from "a third player in the group"; and (b) the same route with
`MD_MP_FCS`/`MD_RX_NOCROP` and the guest-side verbose log on hand, to see whether
the host's answer to the failing guest is delivered at all in the frame before
it deauthes.

### The standing trade: stability and playability before accuracy

**Product decision (2026-09-29):** this emulator exists to run as many DS games
as possible, in multiplayer, **with 4 players, on the least powerful hardware
that can be made to work.** Where emulation accuracy and that goal conflict, the
accuracy can be spent — deliberately, measurably, and written down — rather than
guarded. The link already spends several of them:

| Departure from stock melonDS | Why | Where |
| --- | --- | --- |
| `SendMPEarlyReply` — send the answer armed *during* the round, not at its start | the guest's game could otherwise never answer in time | history.md v0.1.8k |
| `MD_CMD_RETRY_NOTIFY` — deliver duplicate commands so retries can be answered | a guest can only answer from a reply slot it armed beforehand | history.md v0.1.8h |
| `MD_REPLY_KEEP` — keep a bounded backlog of answers instead of dropping the clump | frame-locked consoles answer a frame's rounds in one clump | history.md v0.1.8n |
| `MD_REPLY_BUDGET_FRAMES` — a per-window wait in *emulated* frames | wall-clock windows are the only surface that differs between app and harness | history.md v0.1.9ah |
| `NDS::GetSharedUSClock` — one emulated clock for every console | the two timestamps agreed by construction instead of drifting 275 ms | history.md v0.1.8x |

What the permission does **not** cover: anything that silently loses data a game
is waiting for, anything that makes two players' saves disagree, and any change
shipped without the measurement that justifies it. Every departure keeps the
smoke test at **66 checks / 0 failures**, still ends with every `Test Roms/*.sav`
byte-identical to its recorded md5, and gets a history.md row.

Concrete departures this unlocks, in rough order of value:

- **Frame-locked reply windows.** The app waits 34 ms where the harness waits
  2 ms, and that wall clock is the only suspect left for the `Connection
  interrupted` drop's signature. Make the window a function of emulated time
  only, so a slow machine changes *speed*, never *behaviour*.
- **Lockstep scheduling.** Every console advances exactly one emulated frame per
  tick, so no reply window can straddle two frames on a loaded machine.
- **Per-instance link state** (below) — correctness rather than accuracy, but it
  is the same rule: link bookkeeping with one owner per console.
- **Cheap rendering when 3-4 consoles run.** Skip drawing consoles that are not
  focused, cap unfocused consoles at 30 fps while their input still runs at full
  rate, turn off upscaling/AA/threaded rendering above 2 consoles. Accuracy
  spent for frames is exactly the permission above.
- **Per-title tolerances.** A wider budget, "tolerate N unanswered rounds before
  retrying", or "do not let a peer's game kick us" are legitimate knobs once a
  title's own flow is understood.
- **Single-card Download Play** (Milestone 4) — the largest compatibility win
  available, and it happens to be the cheapest four-player configuration too.

### The hardware rule: 4 players, on the weakest machine that can hold them

**Product decision (2026-09-29):** the target configuration is four players on
one machine, running on **the least powerful hardware that can be made to
work** — not on the dev laptop, which has headroom (4 linked 3D consoles at
~2.2× realtime). This is a requirement, not an optimisation pass, so it outranks
convenience and accuracy whenever the two conflict, and it is the tiebreak when a
design choice is otherwise even:

- **Frames are the budget.** Work whose cost scales with the *number of consoles*
  has to justify itself: rendering, texture uploads, audio mixing, screen
  composition, the frontend's own canvas work.
- **Prefer not doing a console's work at all over doing it faster.** A console
  nobody is looking at does not need its screens drawn; a player not touching a
  button does not need input sampled every frame.
- **The link must never depend on wall-clock slack** (see the frame-locked window
  above). On a weak machine the wall clock stretches; behaviour that changes with
  the machine is the worst failure a couch co-op session can have.
- **Cheap wins that cost accuracy are in scope:** no upscaling/AA/threaded
  rendering above 2 consoles, 30 fps for unfocused consoles, skipping off-screen
  work, and — the largest — **single-card Download Play**, where three of four
  consoles run a small downloaded image instead of a full cartridge (Milestone 4).
- **Every claim under this rule is a measurement:** 4 linked consoles, frame time
  and fps per console, on the weakest machine available.

### Instruments to build first (every milestone needs them)

- [x] **IPC FIFO traffic counters, counted where a word moves.** **Built
      2026-09-29** (`NDS::IPCSent9/7` + `IPCRecv9/7` beside the FIFOs, read by
      `md_get_ipc_stats` and exposed as `ipc` in the driver's probe JSON, with
      `fifocnt9`/`fifocnt7`/`sync9`/`sync7` beside them). They falsified the
      previous pass's central reading in one run: the FIFOs read empty at rest,
      so a *level* can never say whether a channel was never used or merely
      finished — the counts say the ARM7 pushed ~7 words per frame into the FIFO
      for a whole session (53,466 words over 7,730 frames) and the ARM9 popped
      every one of them, *including while both games sat at the park*. The
      smoke suite drives the real ports for it (13 checks): a push with the FIFO
      disabled is not counted, an enabled push is counted once, a peek by a
      receiver whose own enable is off is not a receive, an empty poll is not a
      receive, and the counters are per console rather than per process.
- [x] **A CPU's whole register file, not just its PC.** **Built 2026-09-29**
      (`md_get_regs`, `regs9`/`regs7`/`sp9`/`lr9`/`sp7`/`lr7` on the probe, and
      `tools/scripts/mkds-stack-probe.py` which waits for the park, reads the
      registers and scans the stack for `bl`-preceded return addresses). A
      program counter names a loop; `r13` locates the frame of the function
      that is waiting in it and `r14` is that frame's innermost return address.
      It is what identified `0200FD10` as an instant of the SDK delay rather
      than a wait, and what showed the parked guest still switching between the
      idle context and its own code
- [ ] **The park is an idle instant, so the question is which event the game
      sleeps on.** Both ARM9s at a live park report one frozen register tuple
      (`pc 0200FD10`, `r0 0`, `r1 0x1F`, `lr 0200DBE4`, `sp 02170148`) whose `lr`
      places the last call in `0x200DBD4`'s wait-for-interrupt loop, while the
      guest alternates with its own code (`0x02023EB8`, `sp 0x027E2B6C`). Next
      pass, in this order: (1) burst-sample `pc9` with the register file at a
      park verified by `link 0x3` *and* frames past the route (`tools/scripts/
      mkds-park-burst.py`, whose trigger now requires both after it falsely
      fired on the boot path at frame 1,379); (2) read the **guest's** stack at
      `sp 0x027E2B6C` for a call chain, since the guest is the console that is
      still running; (3) if the ARM9 is idling in wait-for-interrupt, watch
      `IME`/`IE`/`IF` (already exposed) across the park and name which of the
      game's own conditions would raise the interrupt it is sleeping on.
      **Never again read a park from one PC sample a second** — that is the
      mistake this item exists to prevent.

- [x] **A memory-read command on the control channel** (the named open item,
      every pass since v0.1.9y). **Built 2026-09-29** (`tools/appctl.py mem
      <hex addr> [len]`, plus the ARM9's own `IME`/`IE`/`IF` and on-demand
      `pc9`/`pc7` on the driver's probe): it read a parked console's `IME 0001` /
      `IE 00040019` at the instant its ARM9 sat in the BIOS vblank wait and
      killed the previous pass's "interrupts are disabled" reading, and it
      refuses a misaligned address, an over-long length and a non-hex address
      with a sentence each. It has since read plenty of *game* values - the
      kernel's own clock globals, the netcode queue's producer/consumer counts,
      `IPCFIFOCNT` - so that half is no longer open.

      The original note, kept because it is what the work was: the emulator
      half already existed and is proved by the smoke test -
      `md_read_ram(inst, addr, out, len)`
      ([src/frontend/tauri/Bridge.cpp](src/frontend/tauri/Bridge.cpp), 4-byte
      alignment enforced), while the Rust side declares only `md_write_ram` (and
      uses it for the diag identity block) — so the work is an FFI declaration, a
      control command (`mem <console> <hex addr> <len>`), and the page prelude's
      helper, not new emulator code. This is what turns "the peer's world
      changed" from a pixel measurement into a game value — the star gauge in
      NSMB, the racer count / menu state in MKDS, the player list in MPH. It is
      also the instrument that makes Milestone 1's routes findable instead of
      guessed (the MKDS park matrix is the proof: 24 cells of injected peer
      state moved the parked host zero times)
- [x] **The write half of `mem`, and it is what settled the park.** **Built
      2026-09-29** (`tools/appctl.py poke <hex addr> <hex words>`, over the
      bridge's existing `md_write_ram`): a read can show that a loop's exit
      condition is false, only a write can show that the loop is what holds the
      console. It did exactly that - one word written to the parked guest moved
      `pc9` from `0200FD10` to `02021544`, the branch that test guards, with the
      consumer count advancing by the dequeue's own increment (history.md
      v0.1.9as). Diagnostics only, never on the product path.
- [ ] **`appctl.py` needs a player selector on every input path.** `appctl.py key`
      has no `--player`; it drives whatever console the page has *active*
      (`index N` first). Automation for P3/P4 is unsafe until every input
      subcommand takes an explicit console
- [ ] **`press`/`touch` write a console's whole input state**, so a driver that
      asks for buttons lifts a pen the channel just put down. The `input`
      command can already carry both; no `appctl.py` subcommand reaches it
- [ ] **Expose `md_touch_probe` on the control channel.** The bridge can already
      read a guest-visible pen-down bit back, and the smoke test asserts
      ADC = pixel x 16, but a guest-visible touch proof through the app needs the
      probe on the channel
- [x] **A wait that waits on the game instead of on a frame count.** **Built
      2026-09-29** (`tools/appctl.py digest --player N` = an md5 of that
      console's two decoded screens, `waitfor --change DIGEST` = did the step I
      just took land, `waitfor --settle` = has the picture stopped moving, and
      `sleep` now takes `--ms` like every other duration). It is the fix for the
      project's most expensive habit: every route here was a list of frames, and
      **the same Metroid Prime Hunters join took ~7 s at one moment and ~13 s at
      another**, so a 5 s guess either went on before the screen had changed
      (silently driving nothing) or after the lobby had already dropped the
      guest. Exercised live — `--change` fired 0.1 s after a tap that landed,
      and `--settle` correctly refused to settle on a console playing a match.
      The same defect made the driver's own `sleep --ms 500` die in argparse
      while `press --ms 500` worked
- [ ] **No route can branch on what a console's screen *says*.** `waitfor`
      compares a digest and reads the window's status line; nothing answers "is
      this console on `SELECT HUNTER`?". That is what stops 3- and 4-player MPH:
      a guest is dropped after a minute or two of lobby inactivity, three
      sequential joins at ~13 s each outlive the first guest's window, and the
      route cannot notice a dropped guest and re-join it because it cannot see
      which screen a console is showing. The cheapest useful form is a predicate
      over an existing reading — `waitfor --text-on-screen "PLEASE WAIT"
      --player 2` over `bands`/`ascii` of a named row — so a route can wait for a
      state and retry a step, instead of guessing its frame
- [ ] **Nothing can hold a session still.** Real time advances between two driver
      commands, so reading a screenshot races the game — this dropped the MPH
      guest three times while a PNG was being inspected, and it is why a
      screenshot-driven route needs `waitfor` rather than sleeps. A pause/resume
      on the control channel would make a route reproduce at a frame instead of
      a wall-clock moment
- [ ] **A 3/4-console digest diff: one line per console at the same frame**, so a
      group that splits halfway is visible without reading four logs at once

### Milestone 1 — 2 players, in-game, on both of the folder's multi-card titles

NSMB is the working half. These close the other two and the drop:

- [ ] **MKDS: get the group past its own screens into a race.** What
      2026-09-29 (v0.1.9an) settled, all from the games' own state rather than
      from pictures: the park is **not** a hung CPU and **not** the boot. At the
      park both ARM9s sit in FreeBIOS's vblank-wait loop (`pc9 FFFF0238`) with
      their own `IME 0001` / `IE 00040019` read live through `mem`, and one A
      press takes each console out of it and back to MKDS's title screen; with
      no input at all the game boots to its title by frame ~686 and animates its
      attract demo forever. At two consoles the link is **not** the constraint:
      67 emulated seconds and 3820 commands at `clientfail 6 (0.16%)`, and 294
      commands at `clientfail 0` in a shorter run - and in both, the client's
      game turned its own radio off and landed on that park screen with the wire
      clean and no deauth sent (at 8.5 s in one run, past 67 s in the other, so
      not a fixed timeout).
      **2026-09-29 (v0.1.9ao) closed the "find the input" question and left the
      wait itself as the gap.** CUT OFF does what its own label says - the
      host's own screen reads "Touch Cut Off to stop accepting entries and begin
      racing" - and tapping it moves **both** consoles to the game's pre-race
      wait, whose touch screen then reads **"PLEASE WAIT"** glyph for glyph.
      That wait is where the group dies: the host's game gives up and sends
      `deauth out 1` reason `0003` at frame 19351 in one run and 7329 in another
      (58-128 emulated seconds, so not a fixed timeout), the guest following 2-3
      frames later onto the park. It is not input-gated - a tap at the driver's
      old VS point `(128,22)` moves nothing, and A on each console separately
      moves that console's own top screen not at all - and the link is clean
      throughout (`cmds tx 15582 / acks 15582 / clientfail 10` against the
      guest's `rx 15582 / answered 15581 / canned 1`, with all 9 failures of the
      other run inside its first 1009 commands). The route now measures the tap
      instead of announcing it. The next moves, in order: (a) **settle what the
      host's game is handed for the guest's answer.** The guest arms answers
      from a 29-value vocabulary (4,476 arms) while the host's windows log 4,082
      distinct collected bodies (4,474), at the same seq and a length both sides
      call 30 bytes, and the handoff log shows 30 bytes becoming 26 - so either
      the host reads a payload the guest never wrote, or the two logs mean
      different offsets. One `wifiio` dump on both consoles inside one window
      decides it; (b) settle the blank-reply bookkeeping
      (`W_AIDLow == 0` vs the host's `clientmask` AID bit) — see "Next gap after
      the clock fix" below, which names both conditions and the log line that
      separates them
      **2026-09-29 (v0.1.9as) named the poll itself and moved a console out of
      it.** The pinned PC is the SDK's `delay(100)` helper (`0200FD10`), and of
      the five callers a whole-4 MB `bl`-scan finds, three are retry loops whose
      exits test (A) bit 7 of `[0x027FFF8C]` (**set**), (B) `IPCFIFOCNT` bit 14
      (**clear**), or (C) `*0x2173300 != [0x21732AC]` - **equal**, 3F/3F on p1
      and 32/32 on p2, reproducibly. So the wait is (C): `0x2021488(arg & 1)`,
      the blocking dequeue of the netcode's message queue, which
      `DC_InvalidateRange`s the word before every read (so the writer is not the
      ARM9 core) and `delay(100)`s until the producer's count at `0x2173300`
      differs from the consumer's at `0x21732AC`. Writing that word through the
      new `appctl.py poke` moved the guest's `pc9` from `0200FD10` (37/40
      samples) to `02021544` (40/40 - the list walk inside the branch the test
      guards) and advanced `[0x21732AC]` 32 -> 33, the dequeue's own increment;
      20 s later nothing had written `0x2173300` back. **So the wall is a
      message, and the next question is why it stops being posted.** *(v0.1.9at
      corrected the ARM7 half of this paragraph: `IE7 0104` has `IRQ_Wifi`
      **enabled**. `IRQ_Wifi` is bit 24, not 23 - `Wifi.cpp` raises it with
      `NDS.SetIRQ(1, IRQ_Wifi)` and `NDS::SetIRQ` is `IF[cpu] |= 1 << irq`, so
      it is bit 8 of `IE7`, while the probe's `& 0x80` test read bit 23 =
      `IRQ_SPI`. `0104` is wifi + IPC-recv, the configuration a game has when it
      has started the wireless firmware. The digest's `W_IF & W_IE == 0000` at
      every sample says the same thing from the block's side: nothing enabled
      was left unserved. See the corrected item below.)* The two games stop
      reading the wireless block (`handed seq` frozen) while the link stays
      `0x3` - i.e. the two games stop being *fed*, rather than the link
      dropping. What to look at next, in order: (a) identify the producer of
      `0x2173300` (it is written by the ARM7, DMA, or the same code through a
      computed pointer - no ARM9 literal stores to it exists) and see what it
      waits on; (b) the dead variant's screen is a system box reading "An error
      has occurred." whose string is in no ARM9 byte of main RAM - worth
      identifying, because that box is the game admitting it gave up
- [ ] **MKDS: the session ends because the game leaves, and the one remaining
      lever is the game-level wait - not delivery.** Settled 2026-09-29
      (v0.1.9at) from the ARM7's own registers on both consoles in matched
      windows, plus the three archived sessions (a survivor and two failures):
      `Platform::MP_End` has **exactly one caller** (`Wifi::UpdatePowerOn`, i.e.
      the game clearing wifi POWCNT), so nothing in our tree can end a session
      on its own. In both failures the first-moving console's *own game* armed a
      deauth frame in its own TX slot (`LOC2` on the client, `LOC3` on the host,
      `reason 0003`, 30 bytes) and then wrote the WLAN shutdown sequence (TRX
      power off, RF register writes at 0x4C/0x4F, POWCNT clear -> `WIFI: OFF`);
      the *other* console then tore down because our path **delivered** that
      deauth to it (`[mp] deauth received` / `fc=00C0` handed to its software),
      in arm7_b 6 log lines later and in arm7_a 11 frames later. Both directions
      occur (arm7_a: guest first; arm7_b: host first), so the direction is
      not a rule, only the mechanism is. Delivery was complete to the last
      round (host: `collected=0002 of mask 0002`, reply aged 15751 of 33430
      ticks, handoff OK; the 12,725-frame survivor shows the same
      `IRQ_Wifi` enabled and `W_IF & W_IE == 0000` in 82/82 and 75/75 digests,
      and the park's `handed seq` moved in 23 distinct values in arm7_b), so
      nothing was owed-and-undelivered at the transition. **Ruled out as the
      discriminator:** the reply window's carried-over unread answer (1 in
      22853/22853 rounds in the survivor vs 1 in 4792/4799 in arm7_b - same
      state, so not the cause). **The exact fix site for the next pass:** the
      game-level wait both games sit in while the link layer heartbeats - the
      host's command body changes only in a 4-byte field while the guest
      answers byte-identically for thousands of rounds. **The ARM7 asymmetry
      this item first proposed is retracted (v0.1.9au).** It came from `pc7`
      samples - one instant every 0.3 s - and both ARM7s halt in the BIOS between
      scheduled wakeups, so an instant lands there most of the time on *either*
      console. The emulator's own per-register read tally, differenced over the
      park, says the opposite of idle and says it identically in the survivor and
      the failure: host 31.1 reads/frame (spin 6.6) in `vs1` vs 28.5 (6.9) in
      `arm7_b`; **guest 219.6 reads/frame (spin 174.7) vs 232.4 (191.5)**, where
      the spin is `W_USCount0`+`W_RXTXAddr` - the client's WM receive-wait, ~7x
      the host's total traffic in a healthy *and* a failing session, and the same
      order in a third run (6.2 host / 158.3 guest). **The gate itself, verified
      in one window on both consoles (v0.1.9av):** the two words the blocking
      dequeue compares - `0x2173300` (producer) and `0x21732AC` (consumer) - read
      `41/41` on p1 and `3F/3F` on p2 at the park, with both ARM9s pinned at
      `0200FD10` (40/40 and 38/40 samples), i.e. nothing is queued and each game
      waits for a message. A whole-4 MB scan for the address `0x2173300` finds it
      as a code literal in exactly one place - the dequeue's own read at
      `0x020216CC` - so **no ARM9 code stores it**, and the ARM9<->ARM7 IPC is
      enabled but idle (`IPCFIFOCNT 0x8501`, `IPCSYNC 0` on both, 12 samples over
      6 s): the producer is the ARM7. Poking the word (`41 -> 42`) advanced the
      consumer to `42` and the console went straight back to `0200FD10` (30/30),
      so the loop is the hold but the entry's content is not what the game wants.
      **The instrument this needed now exists:** `tools/appctl.py mem --cpu 7`
      reads the ARM7's own address space (`ARM7Read32`; the ARM9 walk could not see
      it at all), 320 KB of ARM7 WRAM + shared WRAM read and disassembled in one
      pass. Next with it: sample the ARM7's PC on both consoles at a live park
      (the host sat at `0x06008A68`), disassemble around it, and follow the store
      into the IPC send FIFO - the ARM7 builds that pointer as base+offset
      (`add r0, r1, #392` near `0x060092F8`; `str r0, [r1, #384]` near
      `0x0600397C`), so a literal scan will not find it. So the ARM7s
      are not the failing side and are not a lever; `tools/scripts/mp-arm7-watch.py
      --activity <driver stdout>` reproduces this in one command - and did, live, in
      the third run this pass (`arm7_c`, default knobs: host spin
      6.0/frame, guest spin 146.6-148.8/frame, within 15% of the offline table;
      the TSV's `io_rate` rates every register `wifiio` lists while `--activity`
      rates only those both blocks printed, so compare the spin, not the totals). That run also **pressed at the park** - A twice on each console,
      START on both, a touch at the VS position on the host's bottom screen - and
      nothing moved: link still `0x3` at frame 13,213 with **zero** deauths, the
      host 100% black and unchanged for 4,490 frames, the guest on its static
      bright screen, `collected 9555 empty 4` (99.96% delivery). So input at this
      park is not the lever either; note the contrast with v0.1.9an, where one A
      press *did* leave the park for the title - that park was `pc9 FFFF0238`
      (BIOS vblank wait), this one is the queue wait at `0200FD10`, and a press
      is only meaningful where the game is waiting for one. **Minimal reproducer:** two commands - the
      route under `MD_REPLY_BUDGET_FRAMES=12` (`tools/mkds_join.py --restart
      --no-dialogs --players 2 --cut-off --watch 240`, watched live by
      `tools/scripts/mp-arm7-watch.py --out logs/<run>.tsv --interval 0.3`) - and
      then `tools/scripts/mp-arm7-watch.py --teardown <app log>` prints the whole
      verdict from the log alone: who deauthed first, who saw that frame,
      whether anything was ever left pending, and whether `IE7` has `IRQ_Wifi`
      enabled (`--selftest` checks it against the three archived sessions,
      3/3). **Unproven, stated as such:** *why* the first mover leaves - the
      queue message at `0x2173300` is still unattributed, and the game-level
      keepalive both games sit in is the same wall as the park, approached now
      from the side that has a reproducer. **Outside our control:** the fact that
      a game may end a session it started - a real cartridge does deauth and
      power its radio down - so "both consoles went black and the link came
      down" is only a bug when it happens without the game asking
- [x] **The ARM7 post, named and disproved as the race-start message (v0.1.9aw).**
      The post path is `0x060092D8`: the gate is
      `[*(0x0380FFF4)+0x3F4]` (that is `0x0380B3A4`), read at `0x060092EC` and
      branched on at `0x060092F4`; when nonzero it allocates an 18-byte message
      (`type 0x0186`, the gate's value as payload) and hands it to `0x0600F19C`
      with the wifi context's descriptor `ctx+0x188` (`0x06009348`; that function
      enqueues at `ctx+0x1F4` or direct-sends through `0x06008E50`).
      The gate reads zero in **every** sample of a whole session (293/293 per
      console, frames 1446-18719, boot through teardown) and the only references
      to it in the whole 128 KB ARM7 image are the poster's own read/read/clear -
      **nothing writes it**. Poking it to `1` *is* consumed within 5-10 frames and
      is followed by both ARM9s leaving the dequeue and by the game's own deauth
      (`reason 0003`), so the post fires when given a value but is not the message
      the parked games wait for. Next on this item: trace the wifi context's
      outgoing queue (`ctx+0x1F4`, where `0x0600F19C` enqueues when its direct
      path is not taken) and the other fourteen call sites' descriptors across a
      live park with `tools/scripts/arm7-ctx-watch.py` (the counters are the
      head/tail-style halfwords, e.g. `q188_8 = 0x00020002`), not this gate
- [x] **The park's CPUs, named.** In the same pass: the digest's `IF`/`IE` are
      the wireless block's registers and are now printed `W_IF`/`W_IE` (the old
      label is what produced the false "interrupts are disabled" reading), and a
      step the driver calls `coupled` now has to show a bus that carries every
      console before it is allowed to say so - a run whose every tap fired
      before the game left its boot screen printed `[coupled] … link mask 0x0`
      with both consoles `[host]`/`[guest]`, and that is the shape of report
      this project keeps paying for
- [ ] **Metroid Prime - Hunters: lobby → match.** The guest's hunter portrait
      and the host's start, both on the lobby's bottom screen, are the taps to
      find. Drive it through the **app** this time with a screen read before
      every tap — the app's attract phase differs from the harness's on the same
      save, which is why every frame-timed tap missed (16 blind `touch 128 96`
      taps produced no screen change)
- [ ] **The rare NSMB drop** (`last deauth reason=0003`, guest leaves, host
      follows ~1 ms later, the guest's last answer `len 6` against the normal
      70 B; 2 occurrences in ~3.5 ks across 18 runs). Make it happen on demand
      with a long linked run, then classify it — is the guest's game reacting to
      a value we hand it, or is the wire losing a round? The baseline to compare
      against is v0.1.9ah: 4 `empty` windows in 14,503 commands that never grow
- [ ] **Make the app's reply window frame-locked** (the first item under the
      trade above). Its known signature is the `Connection interrupted` dialog
      that ends the app's long NSMB runs at ~1,830 s
- [ ] **Assert a match rather than a picture, at least once.** With the memory
      read in, quote one game's own value for "player 2 is in this match" and
      for "the match has a result" — no row in compatibility.md can say that yet

### Milestone 2 — 3 players

- [x] **Three consoles coupled through the app** (2026-09-29, `logs/mkds3_v3.log`
      and `logs/mkds3_ab_stock.log`): one host, two guests, `link mask 0x7` with
      every console's digest read at the same emulated frames, both clients'
      answers credited by the host, held across the host's own CUT OFF.
      **What it took was route ordering, not link code:** every joiner must be
      walked to its group list *before* the first one joins, because an MKDS
      client leaves the lobby on its own 13–21 emulated seconds after joining —
      the old order had the two clients never in the room together (`0x3` →
      `0x1` → `0x5`, never `0x7`). Route: `tools/mkds_join.py --players 3`
- [ ] **The reply window under two askers** — what a three-player *match* hits
      first, and the one link defect the three-console couple actually exposed.
      The host's window gives up short a reply 216 times in ~1,300 windows of one
      three-console run (146 collected nothing, 35 one client, 35 the other), and
      every short window is a client the host's own ack then reports as failed:
      `clientfail` runs at **4.3–5.8% of collected replies** with two clients
      against **1.3%** on the same build and route at two consoles (an earlier
      two-console run read 0.08%, so compare rates, never raw counts — the count
      moves with what the game is doing). **The mechanism is in the host's own
      counters**: `collected 8635 empty 146 reanchor-dropped 208 blank 186
      clientfail 367` — an answer the window closed without is either read by the
      next command or discarded by it, and blanks plus re-anchor drops account
      for the failed-client acks almost exactly. Measured *not* to be the reply
      budget (only 97 of 9,037 clamps hit zero slack; most are 34ms → 33ms), so
      the window is choosing to give up: start from whether it waits for a
      *second* client at all, and prove a fix by the clientfail rate collapsing
      on this route
- [x] **Per-instance routing in `LocalMP` — done in v0.1.9bh, and the shape to
      build with is written down in "The link's shape, as of v0.1.9bh" above.**
      The first attempt (v0.1.9al) was narrowed by measurement and reverted
      because a machine-global `MPHostinst`/`LastHostID` was *not* what broke the
      two-joiner session on the MKDS route; this pass was asked for the full
      ownership anyway, on the audit's finding that one packet FIFO, one reply
      FIFO and one `LastHostID` are shared by every console in the process. What
      it bought, measured: the reply path has checks for the first time and the
      old bus fails three of them (a follower-less answer credited to a host;
      two consoles hosting a client each), the working two-console MPH match is
      re-demonstrated live, and at three consoles the host's own beacon carries
      three party entries for the first time on record (53 frames; zero in five
      pre-rewrite runs) with the host's round loop writing for the whole session
      and zero watchdog stops. Still open from the same runs: the host's console
      delivers almost no client beacon to its own game (35 in a five-minute
      three-console session against 2,011 delivered to the other guest), which is
      the first thing to look at when a host game ages a client out of its own
      room table
- [ ] **`MPStatus.MPReplyBitmask` is dead state**: written on both the command and
      the reply paths and read nowhere in the tree. Delete it or give it a
      reader. A field that looks like reply accounting and is not invites exactly
      the wrong diagnosis, and this roadmap carried that one for a pass
- [ ] **AID and reply-slot allocation beyond two clients.** Half answered by the
      couple above — a third console does get its own `clientmask` AID bit and
      its answers are credited — but the blank-reply/anonymous-aid handling is
      still written for one client at a time, which is the item above
- [ ] **Per-console assumptions left in the driver and the app.** Three consoles
      are now driven end to end — frames, health line, digests, screens and the
      couple state all read per console at 3 (`tools/mkds_join.py --players 3`)
      — but `wifiio`, the touch/input paths and the layout have not been walked at
      3, and `appctl.py key` still has no `--player`
- [~] **Regression at 2 after every change here.** MKDS join re-run at 2 on this
      pass's driver and the reverted (stock) core: `link mask 0x3` held through
      CUT OFF and VS, `collected 9257 empty 0`, guest `answered 9376`, one
      teardown deauth (`logs/mkds2_final.log`). The NSMB harness and NSMB app
      halves of this item were **not** re-run this pass — the link core has no
      diff, so only the driver needed the check

### Milestone 3 — 4 players

- [x] **The same routing at 4.** Four consoles couple into one group and stay
      coupled (2026-09-29, `logs/mkds4_run1.log` and `logs/mkds4_keep3.log`):
      `link mask 0xF`, `p1=host` with `p2=guest p3=guest p4=guest`, every
      console's digest read at the same emulated frame, the host's own command
      body asking for three clients (`0E` in its AID-mask byte where three consoles
      sent `06`), 684 emulated frames in one group, and no deauth inside the
      couple — the three client deauths are the game's own CUT OFF/VS teardown.
      `tools/mkds_join.py --players 4` needed nothing beyond the joiner ordering
      the three-console route already used
- [ ] **The reply backlog under three askers.** Measured and still open: the
      host's `clientfail` runs at **9.9% of collected replies** with three clients
      against 4.3% at two and 1.3% at one — `collected 3208 empty 107
      reanchor-dropped 232 blank 228 clientfail 317` at four, where the re-anchor
      drops are the bigger half. `MD_REPLY_KEEP=3` (nine entries at three peers)
      cut it to 4.3% with `reanchor-dropped 5` on the same route: a measured
      improvement, not yet a change to the default, and it is *not* a fix for
      dropped players — the arm with half the failures lost two clients at CUT
      OFF against one, so the departures are the game's own transition
- [ ] **The least-hardware budget, measured.** In the app on the dev laptop, the
      same MKDS scene at **43.1 fps with two consoles, 33.2 with three, 26.6 with
      four** (72/55/44% of DS speed), while the same game in the headless harness
      does **112.5 emulated frames/s per console at two and 76.7 at four** (1.28×
      realtime across four). So the app's own per-frame screen path costs about
      two thirds of a four-console frame, and the frames are there rather than in
      the emulator or the link: the cheap wins are no rendering for a console
      nobody is looking at, 30 fps off-focus with input still at full rate, and no
      upscaling/AA above two consoles — the hardware rule above is the standing
      version of this item
- [ ] **Input at 4.** Four keyboard zones, four gamepads, each player's own flip
      key, and no way for one player's input to reach another console — which is
      the `appctl.py --player` item above
- [ ] **Saves at 4.** "Global save/load syncs across all players" is a Phase 3
      goal; today each console writes its own `.sav` and hygiene is a per-file
      md5 check. Decide and implement the shared-save model

### Milestone 4 — Download Play (single-card) support

**Why this is a feature to build rather than a limitation to document.** Two of
the six titles in `Test Roms/` have *no* multi-card mode at all — Super Mario 64
DS's VS mode and Star Fox Command's only local mode — and in the wider library
the single-card titles are where the 4-player party games live: DS Download Play
is how **one cartridge serves four consoles**. Widening the compatible game set is
the product goal, so this is the largest piece of it still missing
([compatibility.md](./compatibility.md#download-play-single-card-planned-not-supported-yet)).

**It is also the cheapest way to run four players**, which is the hardware rule
above: the consoles that joined a Download Play session are not running the full
cartridge at all — they are running a small image the host sent over the air.
Cheaper cart emulation, less RAM, usually a much smaller game.

**What is already true: a reproducible host, and the wire.** Star Fox Command's
Recruitment screen now has a driver — `tools/scripts/sfc-dp-host.sh` takes a cold
two-console launch into it by touch geometry and decides success only from link
state (`role=1`, console on the bus, 204-byte `fc=0080` beacons leaving), so it is
one command with a state-based pass/fail (cold run `PASS ... beacons on the wire
(23, +23)`). **v0.1.9cg corrected an overstatement in this section.** The earlier
"complete bidirectional exchange" (`sent=489 (99,532 B)` / `received=501
(101,980 B)`, `logs/rom_sfc_harness.log`) is not a host serving a child image:
both consoles logged as `[host]` — there was never a client — and `md-link.py
census` over the captured session shows the host sends **only** beacons
(`logs/sfc_dp_wire.log`: 42 `fc=0080` 204-byte broadcasts, no command/reply/
download class). So the transport works and the host can be reached, but the
download conversation itself has **never been observed** — the client's protocol
is unknown, not merely unimplemented. **And the receive path does not exist, in
this tree or upstream:** `NDS::SetupDirectBoot()` boots the cartridge's own ROM
(`src/NDS.cpp:297`; `upstream/master:src/NDS.cpp:297`, identical), a repo-wide
grep of stock melonDS finds no multiboot/download-play/child-image code, and the
only `MB_*` symbols are the DSi wifi *mailbox* (`src/DSi_NWifi.h:89`,
`Mailbox[n].Read()`), unrelated to DS multiboot. The joiner really must boot the
cartridge today, and the client must be written from scratch.

**What is missing is one boot path, not an emulator rewrite.** On hardware the
joining console runs the **firmware menu's** Download Play client: it receives the
child image over the air and jumps into it. Two routes, and the second should be
preferred:

- **Route A — a real firmware image (faithful, costs the user a file).** The
  bridge already has `md_set_bios` and `md_set_firmware`, and
  `NDS::NeedsDirectBoot()` already returns false for a bootable firmware, so
  [src/frontend/tauri/Bridge.cpp](src/frontend/tauri/Bridge.cpp) skips direct boot
  when one is supplied. What is missing: a **scripted path through the menu
  itself** (first-boot language/date flow, then Download Play's game list) so a
  joiner can be driven with no human, and a check that the per-console MAC offset
  still lands right when the base MAC comes from a user firmware instead of
  FreeBIOS.
- **Route B — a built-in Download Play client (accuracy spent, no user files).**
  Ship a small ARM9/ARM7 boot image, or implement the client half of the download
  protocol in the emulator, so a joiner accepts a child image and jumps into it
  with **no firmware at all**. The shape this needs already exists in the tree:
  `SetupDirectBoot()` in [src/NDS.cpp](src/NDS.cpp) is exactly "start a program
  that is not a cartridge" — copy the header into the BIOS, map memory, set the
  CPUs' entry points. **Prefer this route:** it puts compatibility above
  faithfulness, asks nothing of the user, and makes a four-player single-card
  session one click — and a user-supplied BIOS/firmware stays a legal matter the
  project should not push onto players when it can be avoided.

**Route A was tried first (v0.1.9ch) and it stops one step short of the app: there
is no DS firmware blob to boot.** The firmware menu *is* the Download Play client,
so Route A only works with a real firmware image, and this machine has none (and
the project cannot ship one): a filesystem sweep of `$HOME` and the repo finds no
`firmware.bin`/`bios7.bin`/`bios9.bin`, and the app's own data dir
(`~/.melonDS-multiplayer`) does not exist. melonDS cannot stand in for it —
`Firmware::IsBootable()` returns false for its generated firmware
(`src/SPI_Firmware.cpp:345`, rejecting `GENERATED_FIRMWARE_IDENTIFIER`), and
`NDS::NeedsDirectBoot()` (`src/NDS.cpp:280`) returns true unless a *bootable*
firmware plus known-native BIOS are loaded, so BIOS blobs alone do not produce a
menu. The app side is missing too, but that is downstream of the blob: it requires
a cartridge for a session at all — `MD_AUTOSTART_PLAYERS` without
`MD_AUTOSTART_ROM` logs `skipping autostart` and starts nothing (measured) — it
loads the **same** ROM into every console (`src-tauri/src/lib.rs:1138`), and
`md_set_firmware`/`md_set_bios` have **no callers** anywhere in `src/` or
`src-tauri/`, so no command or env var can reach a firmware image even if one
existed. Minimal per-console support would therefore be: a per-console boot spec
(cartridge / no cartridge), a firmware path wired to `md_set_firmware`, and
skipping `SetupDirectBoot` when a bootable firmware is present. That plumbing is
straightforward but cannot be exercised on any machine that lacks the blob, which
is why it was not written speculatively here.

**Route B, first half built (v0.1.9ci): the synthesized joiner, and the first
frame the host refuses.** With Route A closed, the client half is supplied by the
app instead of by a firmware applet. `src-tauri/src/dp_joiner.rs` is a small
state machine driven by one new background thread: it listens on one console for
the host's `fc=0080` beacon, then answers it with an **open-system authentication
request** and, once answered, an **association request** built from the beacon's
own capability, SSID and rates IEs. It is the only thing that needed adding: the
frames go out through the existing bus (`md_link_send_packet` on the console's own
driver thread), and the attachment it needs — a console that receives with no
game on its radio — is the existing `md_link_test_attach`, re-asserted because
Star Fox's own boot-time wireless power cycle detaches it (the first live run
heard nothing at all until `md_link_attached` existed to notice). **Gating is the
per-game registry, not an env var:** the title resolves through
`game_profile.rs` to a `download_play` spec (Star Fox Command, `ASFE`), and the
single Settings switch that gates every per-game behavior arms or disarms the
thread. Every other title gets no thread at all, and the emulator core is
untouched, so their link path is byte-identical by construction.

**What the host does, measured on the wire** (`logs/sfc_dp_join.log.gz`, 2
consoles, Star Fox Recruitment host, joiner on console 2): the host's game reads
**every** frame the joiner sends, answers the authentication **205 times of 205**
(`fc=00B0`, unicast to `0009BF126643`), and answers the association **zero
times**. It is not silence any more — the host's own words are on the wire, and
both of them are refusals:

- its authentication response carries `sequence 0002`, **`status 000E`** — 802.11
  status 14, "authenticated transaction sequence number out of expected sequence";
  the transaction is answered but not granted, so the joiner is never
  authenticated;
- its answer to the association request is a **deauthentication** (`fc=00C0`,
  unicast, 30-byte frame) with **`reason 0006`** — "class-2 frame received from
  nonauthenticated station" (`logs/sfc_dp_join.log.gz`, census: `p1 deauth
  0009BF112233 -> 0009BF126643 reason 0006 x205`).

So the first frame the host refuses is the **association request**, and it says
why: the authentication before it was answered with a non-zero status. The next
client step is therefore the *authentication* transaction the DS expects (a
sequence/algorithms the host will grant), not the association. The gating itself
is proven, not asserted: with the Settings switch off the same route produces
`beacons 0 · auth sent 0 / recv 0 · assoc sent 0 / recv 0`, **zero frames from the
joiner console on the wire** while the host beacons normally
(`logs/sfc_dp_off.log.gz`), and on New Super Mario Bros. the command answers
`this title has no download-play joiner` — no thread exists
(`logs/nsmb_dp.log.gz`).

**The client gets in (v0.1.9cj): authentication granted, association accepted,
and the host's next wait named.** The `status 000E` of v0.1.9ci was the frame
builders omitting the hardware's 4-byte FCS slot, so the bus wrote the wire CRC
over the last four bytes of the body and the host read its transaction sequence
number out of a checksum; with the slot restored the authentication is a 34-byte
frame (`24 header + 6 body + 4 FCS`), exactly a real client's, and the host
answers **`sequence 0002, status 0000`**. The association request was then made
byte-for-byte a captured DS client's: a real request
(`logs/ab_ctl_app.log.gz`, `body[0..41]`, `fc=0000 len=70 seq=0010`) is
`capability + listen interval + 32-byte SSID IE + the four supported-rate bytes
raw` — **no rates information element** (Nintendo's own layout,
`WifiAP::RecvPacket`) — where ours had been a 72-byte frame with a rates IE and
the host's empty SSID echoed. It is now 70 bytes on the air and traces `len=82
fc=0000 seq=0010 sync=0025200000010021`, identical to the captured client; the
beacon parser reads the same layout. **Measured** (`logs/sfc_dp_join3.log.gz`, 2
consoles, Recruitment host): **47 authentications, 47 answers, all `status
0000`**; **47 associations sent, 46 answered with `fc=0010`**; **46 host
deauthentications `reason 0001`**. So the host accepts the joiner and enters the
download conversation. **What it waits on, named from its own counters:** an MP
*command* frame is one whose address-1 is the multicast `MPCmdMAC =
03:09:BF:00:00:00` (`src/Wifi.cpp:2006`); the host reads `cmds rx 0` while its
game polls the receive window (`window reads 3700 (+74/cycle)`, `polls 9869
(empty 9865)`), hands our association to its game marked `(not a command)`, and
~3.9 s later deauthenticates and re-accepts. The next client step is therefore a
**command frame addressed to `03:09:BF:00:00:00`**.

**That command inference is wrong, and the client is now as faithful as the
captures allow (v0.1.9ck).** Two corrections, both from captured bytes. (1) The
protocol direction, per GBATEK's Download Play chapter and every multi-card log
in `logs/`: commands are host-to-client (`03:09:BF:00:00:00`) and clients send
*replies* (`03:09:BF:00:00:10`); the only console that ever writes a command is
the host. The client's real first post-association frame is a short dummy reply
(`fc=0118`, body `00 00`) captured in `logs/mph4b_app_4con.log.gz`
(`tx frame: slot 5 (REPLY) fc=0118 len=30 seq=0020`), and the joiner now builds
and sends it. (2) Every captured host advertises its game serial in the beacon's
`DD` tag at data offset 12 (MPH `0A 00 40 00`, NSMB `50 01 40 00`, Star Fox
`18 02 40 00`) and its clients echo that word as their SSID's first four bytes;
the joiner had sent `25 00 00 00` and now parses and echoes the host's serial.
**The `cmds rx 0` premise is structurally impossible:** `MPD.cmds_rx` is
incremented only on CheckRX's `(macgood && IsMPClient)` branch, so a host can
only ever print zero. A probe command to the multicast MAC reaches the host's
game (`36 byte frame, fc=0228` delivered) and changes nothing; the real dummy
reply is delivered and consumed (`handed ... fc=0118 consumed=26 halfwords`);
the host's game never writes its command slot (`cmd 0000`, `cmds tx 0`), sits at
`arm9pc 0x02030488`, and deauthenticates `reason 0001` ~3.9s later. So what the
host waits on is not reachable from the wire by the client, and the host's own
Download Play code is what never starts the transfer. Evidence:
`logs/sfc_dp_final.log.gz`, `logs/sfc_dp_cmd.log.gz`.

**The host now starts the transfer, and the field it was refusing on was in the
client's SSID (v0.1.9cl).** The previous record read the host's association
response as an acceptance, but its body was never decoded: the trace's `sync=`
is that body's first eight bytes as one little-endian u64, so a working host's
response reads `sync=0201C00100000021` — capability `0021`, status `0000`, **AID
`C001`** — while every Star Fox joiner run read `sync=0201000000010021`, i.e.
status **`0001`**, **AID `0000`**: a refusal, corroborated by `[HOST] syncing
client` (which needs a nonzero AID) firing in every real session and never here.
The joiner's `Rx::AssocResponse` discarded the body, so it logged an acceptance
for a refusal. The missing field: real game clients' association-request SSIDs
across three titles are `serial(4) + <beacon DD bytes 16..18>(2) + 00 00 +
name/padding` — MPH `0A 00 40 00 12 23 00 00` against a beacon's `... 0A 00 40 00
12 23 54 01`, NSMB `50 01 40 00 DA A3 00 00` against `... DA A3 1E 01`, a third
`25 00 00 00 A9 D4 00 00` against `... A9 D4 1C 01` — and the joiner had left
the two bytes after the serial zero. It now parses the beacon's `DD` token and
echoes it, and acceptance is defined as **status 0 *and* a nonzero AID**, the
host's own test. Measured live twice (`logs/sfc_dp_token.log.gz`,
`logs/sfc_dp_token2.log.gz`): one clean handshake each (`last assoc status
0x0000, AID 0xc001`), the host logs `[HOST] syncing client C001`, arms its command
slot (`slots beacon 811C cmd 8000`) and transmits `fc=0228` commands decoding to
reply-time `0106`, **slave flags `0002` (slave 1 connected)**, size 3, command
`01` = **NameRequest** — the handshake, started by the host's game. **Where it
now sits:** the host repeats that `NameRequest` while its reply window collects
nothing (`collected 0`, `clientfail` rising), because the joiner's answers leave
as broadcast packets (`md_link_send_packet` -> `MP_SendPacket`, the regular
receive path) rather than through the reply path the host reads
(`Platform::MP_RecvReplies` -> `LocalMP::RecvReplies`, keyed by AID). So the next
step is to send the client's reply through that path with the AID the host
assigned (`C001` -> aid 1), which needs a bridge surface the core already has
(`MP_SendReply`) but nothing exposes yet, and to reconstruct the reply body from
captures.

**The reply path is exposed, the handshake advances, and the next gate is the
host's own button (v0.1.9cm).** `md_link_send_reply` is the bridge surface that
reaches `LocalMP::SendReply` — the reply path `LocalMP::RecvReplies` reads, keyed
by AID — where `md_link_send_packet` only ever reached the broadcast path. The
joiner stamps the reply with the AID the host assigned (`C001` -> aid 1) and with
`rx_ts + 2*16715`, inside the host's own `kReplyStaleWindow` (without it every
reply was dropped as stale: `stale 107`, `collected 0`). The bodies are rebuilt
from real bytes: a captured client's 38-byte UsernameReply
(`logs/mph4b_app_4con.log.gz`, body `04 01 07 <snippet>` + six-byte snippet) and
GBATEK's RsaReply (a normal reply whose type byte is `08`, seven filler bytes, no
key material). Measured live (`logs/sfc_reply5.log.gz`, `logs/sfc_rsa2.log.gz`): from
`status 0000, AID C001` the host's command class advances `01 NameRequest` ->
`00 Dummy` (NameDone) -> **`03 RSA`** (body `06 01 02 00 75 11 03 ...`, size 117
halfwords) -> `00 Dummy` (RsaDone), all five username snippets collected
(`04 01 07 00`…`04`), the RSA reply collected, `our reply collected 711 times`,
and the host digest `collected 470 (+61/5s) ... stale 0 deauth out 0` (Star Fox
has no save, so nothing to restore). A regression this pass introduced and the
live run caught: a `username_idx < 4` guard stopped the *fifth* snippet from ever
being sent, leaving the host on `NameRequest` forever; the guard is gone and a
unit test holds it (`cargo test --lib` **27 passed, 0 failed**; `md_smoke_test`
**141 checks, 0 failures**). **The next command class is gated by a host button,
not another client frame.** After RsaDone the host sends no `04 DataPacket`: its
Recruitment lobby ("Searching for opponents ... Touch the Cut Off button to move
on. Entrants 2/6") only begins the transfer after the host's own **Cut Off** tap
— the left of the two buttons at DS `(91,178)` on the host's bottom screen,
found by rendering the lobby and tapping inside the button. After that tap the
host leaves the lobby onto "Please wait a moment..." and holds at command `00`
for minutes with no `04`. So the next thing to make work is what that
RsaDone/"please wait" state waits on, now that the host is past its own button.

**The host's parked command frame is now read off the host's own code, and the
next target is named (v0.1.9co).** `wifiio` gained a **write-side recorder**
(every write into the low wireless RAM, with the writer's PC/LR, a per-offset
last-writer table, and the command-slot arm's PC/LR plus the writer's register
file) and an **IPC FIFO word trace** (both directions, each word with the
pushing PC/LR); `MD_NOJIT=1` autostarts on the interpreter, which is what makes
those PCs instruction addresses rather than JIT block starts. With it on: the
host's command frame is at wifi-RAM offset 0, the body at slot offset `0x24`,
so the DP class byte is at `0x2A` - the first three body halfwords are written
by the ARM7 library routine at `0x027EFE94`/`0xE9C`/`0xEA4`, and `0x2A` onward
by one halfword copy at `0x037FE66C` (reached from the `bl` at `0x027EFEC8`),
**9,529 writes of that byte against 9,529 slot arms, always the same value**, so
the class in the slot is fixed and no guest frame can change it; the arm is
`strh r2,[r1]` at `0x027F001C` with `r1 = 0x04808090` (`W_TXSlotCmd`). The IPC
trace settles what the previous pass read backwards: the ARM9 is not idling on
the wire (its `pc9 0x02030488` is the SDK loop `bl scheduler; bl cache drain`),
it is pushing **one word per command round** (~154/s against ~166 commands/s)
from the single store at `0x02030660`, values stepping by `0x400`, while the
ARM7 pushes ~3 words per round back from `0x037FEA70`. So the host's own MP
scheduler runs and schedules the dummy; the gate is inside its Download Play
code, between that scheduler and the ARM7 composer. A RAM diff across Cut Off
(`0x020A0000..0x020C0000`, two baselines then post-tap: 788 words changed, 652
new) could not isolate a state variable, because the DP title records rewrite
themselves between the baseline dumps - a negative result. **The next pass
should follow the ARM9 side of that channel** (the site computing the per-round
word, and the descriptor the ARM7 composer is handed), not the wire.
`logs/sfc_nojit.log.gz`, `logs/sfc_regs.log.gz`, `logs/sfc_ipc.log.gz`.

**The gate was then probed empirically instead of read, and the probe says it is
not a plain single word in the host's records (v0.1.9cp).**
`tools/scripts/sfc-dp-poke-search.py` writes one word at a time into the host's
entrant record (`0x020B4C00`), received-name record (`0x020B17A0`) and DP title
records (`0x020B3D40`/`0x020B5640`), holds it a second, restores it, and scores
each trial on the joiner's received command class (`dpjoiner.lastCommand`) and on
the class byte the host's composer last wrote into its slot (the `002A` row of
`wifiio`), checking `commandsRecv` still advances so a freeze is reported as a
stall. Over four parked sessions the classes ever seen were **`00`, `01`, `03` -
never `04`**, and the `002A` byte stayed `5000` (class `00`) through every trial:
no single word arms the transfer. The words that do anything are the structural
ones and what they do is **freeze** the host - `0x020B4C00` word 0 set to 0 makes
it drop to the `fc=0228 len=34` six-byte "no client to address" command form
(the joiner's `classify` length guard then refuses them and `other host frames`
jumps `0 -> 56634`), while `0x020B4C40`, `0x020B4C48` and `0x020B17A0` word 0 each
stall the command stream 10-30s and recover when restored. So the host re-reads
these records every round and corrupting a consulted field breaks it; the phase
flag is not one of them. A second finding, from the host's own counters: its
reply windows are almost all **empty** (`window reads` 175k, `collected` 3k,
`empty` 108k, `clientfail` == `empty`) because its own trace prints `this frame
only has 0us of slack left` - the per-frame reply budget
(`MD_REPLY_BUDGET_FRAMES`, default 2 frames) is spent before the window in Star
Fox's fast MP loop - yet replies *are* collected in bursts each round (body
`00 00`, reply type Dummy) and the host still repeats `00`. The strongest
remaining lead is therefore the shape of that exchange, but it is a reply-level
question and reply bodies were exhausted in v0.1.9cn. **Next pass:** read the
ARM9 code at the per-round word site (`0x02030660`, from v0.1.9co) rather than
perturb the data it consumes. `logs/sfc_poke*.log`.

**Reading the decider instead of perturbing the data names the byte, and forcing
it arms the transfer (v0.1.9cq).** The per-round site v0.1.9co located
(`0x02030660`) is the ARM9 SDK's IPC-FIFO send primitive - it busy-polls
`IPCFIFOCNT` at `0x04000184` and writes `IPCFIFOTOARM7` at `0x04000188` - and the
word it pushes is not the class but a *pointer* to a WM message, so reading it
led onward but is not itself the answer. Nintendo's Download Play engine turns
out to be a compact **Thumb** module at `0x02029C00`-`0x0202A000`; its phase-2
handler (`0x02029DD0`) picks `4` or `9` from `buffer[0x4b2]` and calls the WM
send at `0x02037D24`, and its phase-5 handler writes `titleA[6] = state+0x0E`.
Both were held and **neither moved the wire class** (forcing `buffer[0x4B2] != 0`
did change the module - `state+0x10` became `4` and the phase machine began
cycling `01 -> 03 -> 04` - while the wire stayed class `00`), so those are
recorded as negative measurements. The command body that *is* the wire class was
then found structurally: **no code anywhere in ARM9 `0x02000000..0x02400000`
contains the literal `0x023722E0`**, so the pointer to the body is computed, not
loaded from a pool - which is precisely why perturbing the *records* (v0.1.9cp)
could never have found it. The body is the 6-byte block at ARM9 `0x023722E0`
(`class, 50 08 00 02 00`), referenced by the TX descriptor table at `0x0237A7EC`
(`+0x08`) and by the WM message ring at `0x020B5440` (`+0x04`, length 6); its
first byte is exactly the byte GBATEK's CMD_DATA `[06h]` carries into wifi-RAM
slot offset `0x2A`. The host rewrites it to `00` every round (~166/s, restored
within 20 ms of any perturbation), and **holding it at `04` makes the host arm
and stream `04 DataPacket`**: 15,356 wire frames of
`06 01 02 00 03 11 04 50 08 00 02 00` against 309,913 of the unpatched
`06 01 02 00 03 11 00 50 08 00 02 00`, with the joiner announcing class `04` 968
times and collecting it 245,705 times over a 75 s hold. **This is a host memory
patch, not a client behaviour** - `tools/scripts/sfc-dp-arm-class.py` writes the
one byte through the existing `poke` path, holds it, reports the observed
classes and restores it, and is not wired into the app. **Why it cannot be
satisfied from the wire:** the joiner already answers NameRequest and RsaDone,
its replies are collected in bursts every round (`our reply collected 22300`,
`stale 0 deauth out 0`), the association is accepted (`status 0000, AID c001`)
and the host logs `[HOST] syncing client C001` - and the class byte is still
`00`. The flag is the *output* of the host's own DP phase decision, written after
it reads its replies, so no frame this client can emit moves it. That is why the
host never selects the data class after RsaDone: the client's side is complete
and acknowledged, and the class is the host's own choice. **The captured frame
layout, as the spec for the capture side:** `06 01 02 00 | 03 | 11 | CLASS |
50 08 00 02 00` - GBATEK's fixed CMD DATA header, the six-byte DS header
(`03 11` written by the ARM7 library from its own descriptor, v0.1.9co), the
class byte the host's DP engine owns, then the host's own five-byte stub. What
is absent - and stays absent until the host's own engine builds a payload - is
GBATEK's real class-`04` payload (`09h u16 Packet Number, 0Bh .. data`), so the
frame layout is real and the packet contents are the host's placeholder.
**Next pass:** find the writer that produces that byte - the DP phase decision
that selects the class - since the flag is now named, forced and proven, and the
next step is the branch that chooses to write `00` rather than `04`.
`tools/scripts/sfc-dp-arm-class.py`.

**The writer of the class byte is named by a store watch, and behind it the gate
is a per-client mode field (v0.1.9cr).** No static search could find the writer,
because the pointer to the byte is computed (the literal `0x023722E0` appears
nowhere in ARM9 code) - so `appctl.py memwatch` armed a **store watch on a span
of a console's own main RAM** and reported the instructions that wrote into it
with their registers (`NDS::SetMemWatch`/`RecordMemWatch`/`FormatMemWatch`, off
by default, called from the main-RAM case of all six `ARM9Write`/`ARM7Write`
entry points, first 64 hits kept). It needs the interpreter (`MD_NOJIT=1`),
because the JIT writes main RAM on its fast path. **The writer, and the only
one:** `strb r3,[r1]` at `0x0202BBDE` in the publisher at `0x0202BBD8`, with
`r1 = 0x023722E0`, `r3 = 0` read from `[r0]`, and `cpsr = 0x000000F2` - **IRQ
mode**. `lr = 0x02028661` puts the call inside `SendCommand(cls, arg)` at
`0x0202864C`, which publishes its class into the outgoing buffer at
`*(0x020B3DBC)` (live `0x023722E0`) - so **the DP state struct begins with its
own command buffer**. Every caller of `SendCommand` is one function, the
engine's per-round poll: four 16-bit flag groups send `5, 1, 6, 2`, and class
**`0`** is sent exactly when a decider returns **21**. **Which decider is live
was measured, not assumed:** forcing the first one's scan test
(`poke 0x02028350 = 04E02900`) changed nothing across 108,853 samples, so the
live path is the other decider, `0x020852C`. **Its gate is the answer:** it
returns 21 when it picks no client whose mode field
`*(u32*)(base + 0x14E8 + 4*(i-1))` equals **5**. Live, the registered client's
mode reads **14** - so the host sends Dummy. **The field is a class selector**
(measured by holding it at each value for 2 s and reading the class the joiner
received): `2 -> 01`, `4 -> 02`, `5 -> 03` (streamed: 60,216 of 60,380 samples),
`8 -> 05`, `11 -> 06`, and every other value including the natural `14` -> `00`.
It is the host's own per-client engine state, so no frame a guest puts on the
wire reaches it. **Does the engine build a real payload? Yes.** The class-`04`
path exists at `0x020283A2` (client slot index and a `u16` out of the slot),
gated by `0x0202913C`, a **seek into a segmented image** (binary search over
`u16` segment starts at `desc+12`; returns 0 past `*(u16*)(desc+18)`, else fills
segment start / clamped length / source pointer / segment index and returns 1).
It hangs off the decider the live engine does not call, so no value of the mode
field reaches class `04`. **Next pass:** find the input that opens the first
decider's path (the flag struct at `sp+8` whose `+2` halfword routes between the
two deciders, and the four flag groups that select `5, 1, 6, 2`), since the
class-4 gate and its image format are now named.
`tools/scripts/sfc-dp-arm-class.py`, `logs/sfc_memwatch.log.gz`.

**The downloaded title now starts from a real reset, and the client leaves the
BIOS for good - but its screen still does not advance (v0.1.9da).**
`SetupDownloadPlayBoot` was missing both ends of the emulator's own boot path
(`md_load_rom` runs `Reset()` -> `SetupDirectBoot(rom)` -> `Start()`), so the
child ran on top of the previous game's RAM, TCM, IPC, timers and CPUs. That is
what the *nondeterministic* faults said: the same image faulted a few hundred
bytes into its entry at a different address every run (`02008BEC`, `02000A28`,
`02008B98`), while the image is provably consistent - the child's own header in
RAM states ARM9 `0x10B324` + ARM7 `0x286B0` + header `0x160`, exactly the
`1260340` bytes transferred. `NDS::SetupDownloadPlayBoot` now calls
`NDS::Reset()` *before* it writes the image, re-runs
`SPI.GetFirmwareMem()->SetupDirectBoot()` afterwards (the firmware header and
user settings live in main RAM, which the reset clears), copies the FreeBIOS
ARM9 logo, and ends with `Start()`. Live on the new path
(`logs/v9sfc_dp_boot7.log.gz`, `logs/v9sfc_dp_route7.log.gz`, route exit 0,
nothing set): the transfer is unchanged at `2502 packets / 1260340 bytes`, the
client logs **zero** warns or errors, and its ARM9 samples *inside the
downloaded image* on 96-99 of 100 samples (`0201C7B4`..`02023F4C`) with its
stack in the DTCM the child itself set up and the VBlank IRQ serviced.
**A second defect, found by reading the live log, is also fixed:** `step`'s
booted gate sat *after* the receive block, and that block powers the console's
radio on - so one frame after the handoff the joiner logged `listening for a
download-play beacon` and re-attached the radio the child had just taken over.
The gate is now the top of `step`, the handoff's own turn returns instead of
falling through to the handshake send path, `run` breaks once booted, and the new
`a_booted_client_stops_touching_its_radio` pins it. **The "display-off" reading
this pass shipped is wrong (corrected in v0.1.9db):** `dispcntA 00011108` has
bits 16-17 = `01`, i.e. *graphics display ON*, and only `11` is melonDS's
display FIFO (`src/GPU.cpp:1084`) - so the child was never dark, and it is not a
child that never reached its first frame. It is showing the downloaded game's own
two-line error box, decoded glyph by glyph off the framebuffer:
**`Communication error. / Please turn your DS off.`** The host's bottom screen
carries the same first line, so both games are in a network-error state. The
child's own wireless init is what aborts - its radio comes up at frames 5306 and
5517 and is powered down within 4 and 1 emulated frames after writing RF values
melonDS calls `invalid channel values 0004C:0004F`, and it never polls the
receive window again (`read_game 0`, `polls 0`) - while both its CPUs stay busy.
The three `unknown ARM9 IO write32 04001060/64/68` writes are ordinary boot
behaviour on **both** consoles (SDK stub `0x01FF8028`), not DP-specific. Two
mechanisms are on the record, neither fixed: the handoff deauthenticates and the
host tears its access point down, leaving the downloaded title nothing to join;
and `Wifi::Reset()` leaves `CurChannel = 0`, which makes a console mute and deaf
(`src/Wifi.cpp:1057`, `2738`) until the game writes a matching RF pair.
`cargo test --lib` 37/0, `md_smoke_test` 149/0 (not re-run in v0.1.9db - no code
changed).

**The RF-write trace names where the booted child's wifi bring-up stops - the
register block, then the radio off, never the channel select (v0.1.9dc).** The
`rfw:` instrument in `Wifi::RFTransfer_Type3` (`src/Wifi.cpp`) was run for the
first time on a fresh two-console Star Fox route (`logs/v9sfc_rfw2.log.gz`,
route exit 0; `logs/v9sfc_rfw2_finding.txt` digests it): the host and the booted
child both write the firmware's RF register block (ids `00..28`, ending `id=01
4C`, `id=02 4F`), and then they part. The host continues into a channel scan -
`4B:6C -> channel 1`, `4C:4A -> channel 7`, `4D:08 -> channel 13` - and settles
on **channel 7**. The child powers the radio off at the end of the block and in
all of its 205 `rfw:` lines writes **no channel pair at all** (its only
`id=01/id=02` writes are the block's own `4C:4F`, each printing `invalid channel
values 0004C:0004F`). So `CurChannel` is never set and the downloaded title is
mute and deaf (`src/Wifi.cpp:1057`/`1339`/`1523`/`2738`) - v0.1.9db's mechanism
(2), located to the single missing step (the channel select) rather than "its
wireless init aborts". Its parked state is that bring-up stopped: `pc7 0000115C`
(the ARM7 in the BIOS idle loop) beside `pc9 02008B60` (the ARM9 near the
downloaded entry `02000850`), `irq IME=0001 IE=2009 IF=0000`. **And the no-deauth
handoff was run live: it is not enough.** The tree's handoff no longer
deauthenticates the host (pinned by `the handoff leaves the host's session up: it
sends no deauthentication`), and this pass ran it - the host's own game still
deauthenticates the client (`reason 0001`) and then broadcasts its session end
(`reason 0003`, `USCOMPARECNT disabled`) about five seconds after the child's
radio cycles. Two mechanisms, both measured on the current build; the missing
channel select is the one with a target. **Next:** what the child's ARM7 waits on
after the block load (`pc7 0000115C` / `pc9 02008B60`) that keeps it from
scanning. `cargo test --lib` 37/0, `md_smoke_test` 149/0.

**The child now keeps the host's channel across the boot, and it is measured - and the next blocker is the hand-off firing too early (v0.1.9dd).** `Wifi` gained one held channel (`SetDownloadPlayChannel`/`HeldChannel`, `src/Wifi.cpp`, `src/Wifi.h`): `ChangeChannel()` keeps it while the RF registers name no channel, a child that selects its own replaces it, and `Wifi::Reset()` clears it. The hand-off sets it once from the host's beacon (`st.host.channel` -> `JoinerBus::handoff` -> `md_dp_boot(..., channel)` -> `DownloadPlayBootInfo.Channel` -> `NDS::SetupDownloadPlayBoot`), gated by the same registry/joiner gate (no env var, no ROM path). Live (`logs/v9dpchan.log.gz`, route exit 0, nothing set): `wifi: rf registers 00000:00000 name no channel; holding channel 7`, `radio tuned to channel 7`, later writes `rf registers 0004C:0004F name no channel; holding channel 7`, and the child's `rfw:` trace shows `chan=7` where v0.1.9dc had `chan=0` - the mute/deaf condition is gone. **It is not sufficient.** The child still never arms its radio (one RF block per power-up then `WIFI: OFF`, twice, no TX slot armed, `game 0` frames delivered to it), its picture freezes ~350 frames after boot while its ARM9 runs (pc9 `0201C7FC` in a tight self-loop) and its ARM7 alternates BIOS idle `0000115C` with its own WRAM `038006A0`; the host is not the blocker (it went `04 -> 00`, kept beaconing `fc=0080` with `link mask 0x0`, then deauthed `0001`/`0003`). **The blocker is in the joiner:** `dp_joiner.rs` `step()` logs `the download is complete`, calls `handoff()` and `return`s *before* the send path, so the GotAllReply for that round never goes out; every later host round reads `collected=0000` and the run's class census is `00` 1003, `01` 36, `03` 5, `04` 7104 - **`05` Final never** - so the child boots into a transfer that never closed, where a real client boots after Final and continues the association (BSSID/AID/role) the firmware's client negotiated. **Next:** answer the completion round, hold the boot until the host's `05` Final, then carry the association across the boot. `cargo test --lib` 37/0 (the hand-off test now also asserts the child keeps channel 7), `md_smoke_test` 149/0. `logs/v9dpchan_finding.txt` digests the run.

**The completion round is answered and the host collects it, but the host never sends `05` Final - so the boot cannot be gated on it (v0.1.9de).** `dp_joiner.rs` `step()` no longer `handoff()`s and `return`s on the round that logs `the download is complete`: that round is queued and answered like any other (a completing `04` gets the GotAllReply), and the boot moved to the end of the associated phase, gated on `final_replied && image.complete()`. Live (`logs/v9dpfinal.log.gz`, route exit 0, nothing set): the host collects the completion's answer - `body[14]=04 01 0A C6 09 C6 09 00 00 00` (`0A` GotAllReply, next/count 2502 = the RSA total) - where v0.1.9dd read `collected=0000` on every round after completion (78,332 rounds answered, 14,167 collected here). **But the class census is `00` 59,333 / `01` 36 / `03` 5 / `04` 5,980 - no `05`**, so v0.1.9dd's reading is falsified: the host's class still goes `04 -> 00` and no Final ever comes. Instead (read live, no pokes): `*(0x020B3DBC)=023722E0`, client-1 mode `0x023737C8` = **7** (was 6, the download direction; sampled four times over ~25 s, stays 7), gate halfword `0x02373A34` = `0000 0005`, class stuck at `00` with slave 1 registered, 1,013 beacons, 0 deauths; the joiner reports `not booted`. **Next:** derive the boot from the host's own completion state (mode 6 -> 7 / class `04` -> `00` after it collects the GotAllReply), and test the deadlock reading - the host holds the association and polls a registered client it never tears down, which reads as "I have finished sending, now you". `cargo test --lib` 37/0 (the hand-off test now asserts the completing round is answered with the GotAllReply and is *not* the boot trigger, and that the Final is confirmed before the hand-over; the quiet-joiner test associates first and boots on the Final), `md_smoke_test` 149/0. `logs/v9dpfinal_finding.txt` digests the run.

**The booted Download Play child's radio bring-up is diffed against a normal join
and the skipped step is isolated (v0.1.9dh).** Both consoles were sampled live in one
Star Fox run (`logs/v9dgdiff.log.gz`, `logs/v9dgdiff_wifiio.txt`, route exit 0, nothing
set) with the read-side recorder already in the tree (`Wifi::RecordMPIoRead` -> `appctl
wifiio`: every wifi register a console's own game read, by offset, with the last value).
The host lives in the wifi IRQ/receive loop (`W_IF` 50,017 reads, `W_IE` 41,332,
`W_USCount0` 18,618, `W_TXStat` 17,556, `W_CMDStat0` 11,341, `W_TXSlotLoc1/2/3` 17,555
each); the booted child reads **none of them** - zero reads of every one - and reads only
the baseband/RF bring-up (`W_BBBusy` 2,253, `W_BBRead` 1,018, `W_RFBusy` 123,
`W_ModeWEP` 11, `W_Random`/`W_Preamble` 6 each, `W_MACAddr`/`W_BSSID`/`W_AIDFull` 5
each), with those totals frozen across two samples 12 s apart. Its read ring ends on a
spin on the baseband busy flag (`15E:0000` x337 with the transceiver off, last `W_BBRead`
`003F`). **The sharpest fact: `TRX power ON` fires x4 on the host and x0 on the child** -
the child never asks its transceiver to come up, so there is no channel select, no beacon
arm and no MP loop, while the host's *same* firmware RF block leads on to a scan (channels
1/7/13, settling on 7) and a beacon arm. **Two of the three candidates are settled:**
(2) is falsified - the child *does* reach `MP_Begin` (`WIFI: ON` -> `wireless ON (begin #2,
slots now 0x3)`, twice) - and (3) is refuted as stated (the sequence never requests
power-on rather than aborting). What remains is (1) narrowed: **the transition from
baseband/RF init into the multiplayer loop in the child's own Download Play client
driver**, inside the downloaded game's code and not reachable from a frame the joiner
sends. A secondary risk is recorded, not claimed: the child reads no association register
post-boot, so this trace cannot say whether the carried BSSID/AID survive its own init -
and `W_ModeReset` bit 14 (`0x4000`) is a core branch that would zero exactly them plus
`MPMYAID`; a store watch on `0x020-0x024` across the boot settles it. No code changed;
`cargo test --lib` 39/0 and `md_smoke_test` 149/0 re-run green on the unchanged tree.`logs/v9dgdiff_finding.txt` digests the run.

**The carry risk v0.1.9dh recorded is settled and retired, and the child's stop is located
with the power-register trace live for the first time (v0.1.9dk).** `TracePowerWrite` did not
compile until v0.1.9dj, so every earlier run's log has zero `[mp] power reg` lines; two fresh
Star Fox routes (`logs/v9carry.log.gz`, `logs/v9code.log.gz`, both exit 0 and `BOOTED`) carry
479 each, giving both consoles' power/mode write history for the first time. **The carry
question:** the child writes `W_ModeReset` **zero** times in the whole run - every such line is
`p1` - so the bit-14 (`0x4000`) branch that zeroes `W_MACAddr0..2`/`W_BSSID0..2`/`W_AIDLow`/
`W_AIDFull`/`MPMYAID` (`src/Wifi.cpp:4088-4101`) never fires on the child, and it would not have
mattered: the child walks those registers through its own bus-test patterns and then writes its
own MAC and `W_ModeWEP`, so the carried association is a value its driver replaces anyway.
Nothing was changed for it (hardening a branch this child never takes would be complexity for a
risk that is not real). **A prior reading corrected:** `pc7 0000115C / lr7 037F84FC` is not the
child's retry loop - `0x037F852C` thunks into `0x038043E2`, which is Thumb `svc 6`, the first
entry of the image's own one-instruction-per-SWI stub table, and the **host** sits in the same
loop in the same run; it is the game's shared ARM7 idle loop. **The stop, against a working
console:** the host's ladder - `W_ModeWEP <- 004A`, `W_PowerForce <- 8001,0000`, `W_PowerTX <-
0003`, `W_ModeReset <- 0001`, `W_PowerDownCtrl <- 0000`, `W_PowerTX <- 0001`, `W_PowerState <-
0002`, `W_PowerForce <- 8000,0000`, `W_ModeReset <- 0000` - raises `TRX power ON`; the child
never writes `W_PowerState`/`W_PowerForce`/`W_PowerTX`/`W_PowerDownCtrl`/`W_ModeReset` at all,
power-cycles its block three times (`wireless on/off 3/3`) and gives up, then its game shows its
own `Communication error. / Please turn your DS off.` Everything after that is empty (`W_BBRead`
1018, `W_BBBusy` ~16000, `W_RFBusy` 643, zero reads of `W_IF`/`W_IE`/`W_USCount`/`W_TXStat`, RX
buffer `0000/0000`, `USCOUNT=0`), and the child is alive in its own main loop (pc9 moves through
`0201C790`/`0201C7FC`/`0201CD34`/`02023F2E`, IPC `7sends 20647`), not faulting. **No emulator
defect is substantiated** (busy flags toggle, the baseband read path returns the fixed chip id
`0x006D`, the channel hold and carried registers are present and written by the child), so no
fix was made. **Next:** a read/store watch on the child's wifi block across its three power
cycles that names the read it acts on between the last `W_PowerUS <- 0001` and its first error
frame - the ring this run captured holds only busy-flag polls. `cmake --build build-tauri -j8`
exit 0, `md_smoke_test` 149/0, `cargo test --lib` 39/0, transfer unchanged (2502 packets /
1260340 bytes). `logs/v9carry_finding.txt` digests both runs.

**The path trace is made truthful in the shipped JIT config, and the site the previous pass
named is disproved: the child's ARM9 never executes `0x0201CA70` or the `0x0201CD34` IRQ-off
loop after the boot (v0.1.9dn).** The recorded PC is the address of the instruction about to run
in every mode - `instrAddr = R[15] - ((CPSR & 0x20) ? 2 : 4)`, computed once at the top of
`ARMv5::Execute`/`ARMv4::Execute` and reused by the JIT branch as the block address it
dispatches on (the same value `ARMJIT::CompileBlock` uses) - so under the JIT it records whole
basic-block entries and on the interpreter each instruction. **What was not trustworthy, and is
the source of the wrong site, is `probe`'s raw `R[15]`:** melonDS keeps it 4 above the running
instruction (`JumpTo` sets `R[15] = addr+4`, `FillPipeline` prefetches from `R[15]-4`), so a raw
`pc9` reading is block entry + 4, and the previous pass disassembled at that raw value and named
code 4 bytes off. **Race-free by construction now:** `SetPcTrace` only publishes a request
(release-incrementing `PcTraceReqGen`); the CPU thread applies it at the top of `RunFrame`
(`ApplyPcTraceRequest`); fixed-size arrays only (`PcTraceRing[16384]` of `{pc,lr,cpsr,r0..r3}`),
so nothing is reallocated or cleared from the control thread - the vector reallocation behind the
earlier SIGSEGV is gone. **Measured in the run that ships** (JIT on, `logs/v9jit2.log.gz`, route
exit 0, `BOOTED`, transfer byte-identical 2502 packets / 1260340 bytes, 0 warns): child
`0x027FFC40 = 2` (host `= 1`), so the test at `0x0201CA70` (`ldrh r0,[0x027FFC40]; cmp r0,#2;
bne skip; bl 0x0201CD34`) would divert - but tight windows held 366 frames each record **0
visits** to the loop body `[0x0201CD34,0x0201CD48)` and the test `[0x0201CA70,0x0201CA8C)`. A JIT
block with a backward branch returns to the dispatcher each timeslice, and the child's own
`0x0201C780` pacing loop is the positive control (~11k entries/frame), so 0 means never entered.
The wide window shows the child's real steady cycle (`0x0201C780`->`0x0201C78C`->`0x0201C7F8`,
`r3 += 0x40` per pass, the `0x0201C9D8`/`0x0201C9EC` IRQ helpers, then `0x0201CD30`), and the
last entry `0x0201CD30` with `lr 0201AC10` is exactly the block entry the old raw `pc9 0201CD34`
names - disassembly of the child's dump shows `0x0201CD30` is `bx lr`, the return from the
cache-drain helper `0x0201CD28`. So the child is in an active main loop with IRQs serviced
(`IE=2009 IME=0001 IF=0000 can_take=True`), not parked in the DP error path; its ARM7 stays in
BIOS idle and `mp cmds tx 0`. **Honest result:** not "localized, not named" - the named site is
disproved. **The one remaining measurement:** arm the trace before the hand-off so it spans the
boot transition, not just the post-boot steady state. `cmake --build build-tauri -j8` exit 0,
`cargo build --release` exit 0, `md_smoke_test` 159/0, `cargo test --lib` 39/0.
`logs/v9jit2_finding.txt` digests the pass.

**The child's ARM9 is traced inside its own boot window, the "stop is in the child's own MP
bring-up" conclusion is demoted to a lead, and one code site is named (v0.1.9dm).** New
`pctrace` records the first visit to each PC in a window for one CPU, in order, with LR and
frame (`NDS::SetPcTrace`, the hook at the top of `ARMv5::Execute`/`ARMv4::Execute`,
`ctrl_pctrace`, `appctl pctrace`); `NDS::Reset()` clears the record so a trace armed across
the boot follows the image that comes out of it, and `md_smoke_test` pins the contract
(**159/0**, up from 152). **A real defect found building it:** the first version reallocated
its visited-set from the control thread while the CPU thread was recording, and the app died
with SIGSEGV seconds after arming; rebuilt with fixed-size arrays only, the same run survived.
**Measured (JIT, the real config; `logs/v9jit.txt`, `logs/v9samp.txt`):** the child's ARM9
records 8303 distinct block entries across 165 code regions and then stops growing; sixteen
pc9 samples over 33 s cycle through `0201C7FC`, `0201C790`, `0201CD34`, `02023F2E` - it is
running a steady in-image loop, not parked at a give-up branch, with `mp cmds tx 0` and the
ARM7 idle in the shared BIOS loop. **The one site named, from the child's own image:**
`0x0201CA70` is `ldr r1,=0x027FFC40; ldrh r0,[r1]; cmp r0,#2; bne 0x201CA8C; bl 0x201CD34`,
and `0x0201CD34` is an IRQ-off self-loop - and `0x027FFC40 == 2` is exactly what
`SetupDownloadPlayBoot` writes (`src/NDS.cpp:514`). Stated with its limit: under the JIT
`R[15]` is a block address, so the samples localize the child to that loop but do not prove it
spins inside `0x201CD34`. **The interpreter is a confound and is reported as one:** with
`MD_NOJIT=1` the child parks at a different address (`02008B60/02008B6C`, inside the 64-bit
divide `0x02008B40..0x02008B7C`), so noJIT is used only to make the hook exact. **Ruled out
with addresses:** not a wifi-register gate, not the BIOS/not a fault, not "it never notices it
was downloaded". **Correction:** `logs/v9dgdiff_finding.txt`'s `W_BBBusy`-hard-wired-to-0 claim
is wrong (`src/Wifi.cpp:4047`/`:4440`) and the file now says so. `cmake --build build-tauri -j8`
exit 0 (cold), `md_smoke_test` 159/0, `cargo test --lib` 39/0, four routes exit 0 / `BOOTED` /
transfer unchanged (2502 packets / 1260340 bytes). `logs/v9arm_finding.txt` digests the pass.

**The wifi block is PC-tagged now, the child's stop is located in its own code, and the one
emulator-side candidate was built, measured not to help, and reverted (v0.1.9dl).**
`Wifi::RecordRegAccess()` records every access to the register window with the ARM7 PC and LR
that made it (surviving the child's own `Reset()`), and `appctl wifiio` prints a
last-writer-per-offset table plus an ordered access ring; the report still fits the control
channel (1575 of 65536 bytes) and `md_smoke_test` pins both the fit and the new sections
(**152/0**, up from 149). **A prior reading corrected:** the change-only `power reg` trace
never logged a write of the value a register already held, so "the child writes `W_ModeReset`
zero times" was an artifact - the raw record shows `W_ModeReset <- 0000 @pc7 037F93F0` and
`W_PowerForce <- 0000 @pc7 027EA4E8 lr7 037F94C4`; bit0 is still never raised. **From the live
dump of the child's ARM7 code** (`appctl mem` -> objdump, `logs/v9dis_*`): `0x027E9A1C` is its
register table loader (25 offset/value pairs, one of them `W_ModeReset = 0`), `0x037F9468(state)`
is its power state machine (0 = park `W_BBPower <- 800D`/`W_PowerUS <- 1`; 16 = power up:
`W_PowerForce <- 0`, the reset routine `0x037F93B0`, the wake `0x027EA3D4`: `W_PowerUS <- 0`,
`W_BBPower <- 0`), and `0x027EA66C` is the routine that would force the transceiver
(`W_PowerForce <- 8001`, poll `W_PowerState >> 8 == 2`) - which the child never runs (its
`W_PowerForce` offset has exactly one writer). The child runs state 16 and then parks, and its
ARM7 returns to the BIOS idle loop (`pc7 0000115C`) with its register history frozen (zero reads
of `W_IF`/`W_IE`/`W_USCount`/`W_TXStat`/`W_CMDStat`; `TRX power ON` 0 against the host's 8).
**No emulated value is behind it:** the trace carries every read with its PC and the child's
driver reads no power register at all. The candidate the trace suggested - carrying
`W_ModeReset` bit0 across the boot, as the channel/BSSID/AID are already carried - was built and
run (`logs/v9pow.log.gz`): the child's own table loader writes the bit straight back to 0,
`TRX power ON` still never fires, and the screen digest does not move (`ef647ee7...`); reverted,
since the child's driver clears it by design. **Next:** the child's ARM9 view of its own
download-play library's session start - the ARM7 side is finished and idle, so another wifi
register cannot name this. Build/smoke/test all exit 0 on the reverted tree;
`logs/v9reg_finding.txt` digests the pass.




**The baseband/RF busy flags are now emulated, and the flag theory the previous pass left open is falsified (v0.1.9di).** `W_BBBusy` (0x15E) and `W_RFBusy` (0x180) read 0 hard-wired ("TODO eventually"); a transfer now starts the moment the console writes `W_BBCnt` (direction 5 = write, 6 = read) or clocks an RF register (`W_RFData2`, `W_RFCnt` bit 0), the flag reads 1 until the byte is clocked through (`kBBTransferUS`/`kRFTransferUS` = 6us in the console's own shared microsecond clock), and then 0; `W_BB_READ` holds the previous transfer's data while one is in flight (GBATEK), which is the pipelined shape the child's driver uses. The deadlines are transient and kept out of the savestate stream (layout unchanged), cleared by `Reset()` and on load, so no state inherits a stale deadline. Live, on the unchanged route (`logs/v9bbrf.log.gz`, `logs/v9bbrf_wifiio.txt`, route exit 0, nothing set; transfer still 2502 packets / 1260340 bytes; carry still channel 7, BSSID `00:09:BF:11:22:33`, AID 1): the child's busy polling now fires - `W_BBBusy` 2,253 -> 15,932, `W_RFBusy` 123 -> 643 - and **everything else is byte-for-byte what v0.1.9dh measured** (`W_BBRead` 1018 last `003F`, `W_ModeWEP` 11 last `000A`, `W_Random` 6 last `007F`, the association registers 5 each, the buffer registers 5 each), with `TRX power ON` still **x0 on the child (x8 on the host)**. So the child is not blocked on the flag. `probe --player 2` four times over 6 s reads the *same* `pc7 0000115C` / `r0 04000000`, and `lr7 037F84FC` lands in the child's own ARM7 code, read live and disassembled: a retry loop at `0x037F84F8`-`0x037F851C` that calls the thunk `0x037F852C` (`bx 0x038043E3`) and `0x037FE4E8`/`0x038072A0`/`0x037FE428`/`0x038070C4`/`0x0380417C` each pass and loops back, with `0x0000115C` the FreeBIOS ARM7 delay (`subs r0,r0,#1; bgt`); the ARM9 alternates `0x0201C7FC` (`bl 0x02087350`) with `0x0201CD34`. **Next:** the child's own ARM7/ARM9 retry loop, which never raises the transceiver - three leads are on the record (what `0x038043E3` returns; what the ARM9's `0x02087350` tests; and the unclosed v0.1.9dh carry risk, `W_ModeReset` bit 14, now cheap to settle with a store watch on `0x020-0x024`). Adversarial: the change is exercised against the *working* console in the same run (the host read `W_BBBusy` 15,616 times and completed the whole DP handshake), `W_BB_READ`'s previous-transfer semantics is what hardware does (a driver that ignores it would fail there too), and clearing on load kills the one real hazard of a non-savestated absolute deadline. `cargo test --lib` 39/0, `md_smoke_test` 149/0. `logs/v9bbrf_finding.txt` digests the run.

**The negotiated Download Play session is now carried across the boot as one named fact, and the carry is shown insufficient (v0.1.9dg).** A real DS client does not merely boot after a Download Play transfer: it continues the association its firmware already negotiated (BSSID, AID, role, channel), and this fork's boot is a whole-console `NDS::Reset()` that clears all of them. The session is now one value (`dp_joiner::Session { channel, bssid, aid }`), threaded through `JoinerBus::handoff` -> `md_dp_boot(..., MDDPSession)` -> `DownloadPlayBootInfo.HostBSSID`/`AID` -> a new `Wifi::SetDownloadPlaySession(channel, bssid, aid)` that re-seeds the core's association outright (`IsMP`/`IsMPClient`/`MPD.role=2`/`MPMYAID`/`W_BSSID0..2`) beside the v0.1.9dd held channel; `Reset()` clears all three holds. Live (`logs/v9dpsession.log.gz`, route exit 0, nothing set): line 70329 carries channel 7, BSSID `00:09:BF:11:22:33`, AID 1, and 70330 logs the child coming up an MP client of the host as AID 1, with `holding channel 7` on every later RF write. **And the consoles still do not link.** ~6 minutes after boot `probe --player 2` reads pc9 `0201CD34` / pc7 `0000115C` (BIOS idle) with `mp: cmds tx 0 ... collected 0`, `status` reads `link mask 0x0`, `wifiio` reads `role=2 (client)` but `handed start=0000 len=0` with USCOUNT 0 and an all-zero RX buffer, and the app's own line reads `health p2: LINK OFF ... slots 0x0 ... pwr 0200/0002` and `mp p2 [guest]: cmds rx 0 ... replies sent 0 collected 0`. The host's beacons still flow (1,731 beacon lines after the boot) but not one frame reaches the child (p2 rx frames = 0 for the whole run) and the child sends nothing. **Reading: the obstacle is the child's own ARM7 bring-up, not the session state** - the session is present and the host still transmits, yet the child's game powers the radio on and off twice more after boot, loads one RF block, never selects a channel or arms a TX slot, and leaves the ARM7 in the BIOS idle loop. **Named next blocking step:** what the booted child's ARM7 waits on after its RF block load - at `pc7 0000115C` beside `pc9 0201CD34` - that keeps it from scanning, attaching and arming a TX slot (its own DP client init waiting on an ARM9/ARM7 handshake or IRQ its boot never raises; the emulator attachment lifetime, which ends at the hand-off while the child's game never reaches `MP_Begin`; or its power sequence aborting before the channel select). `cargo test --lib` 39/0 (the hand-off test now also asserts channel 7 / BSSID HOST / aid 1), `md_smoke_test` 149/0. `logs/v9dpsession_finding.txt` digests the run.

**The boot is rebuilt from the host's own completion state, and the transfer is now an explicit state machine (v0.1.9df).** The deadlock question was settled both ways: the answering arm of v0.1.9de stopped the host's `04` within ~2 rounds of the collected GotAllReply, the silent arm of v0.1.9dd left it re-sending its last packet forever (`collected=0000`), so the host stops asking for data *because* it collected the completion answer - "I have finished sending, now you" - its class-00 Dummy polls are its own "nothing pending", and a boot gated on a `05` Final it never sends deadlocks by construction. The trigger is the host's two statements: a CMD ACK that stopped reporting this slave missing while the image was complete, *and* its newest command no longer a `04` - both required (the transfer reaches `Transfer::Closed` only from both, and the transition-table test pins each). **The re-envision:** the weakest part was that "may I boot?" was a flag conjunction with no state to own it - no reset (a deauthenticated-then-re-registered client could boot on the previous session's facts) and nothing observable (a stalled run could only be explained by census-ing classes out of the log). It is now `enum Transfer { Unstated, Streaming, Complete, Collected, Closed }`, advanced in exactly one place (`State::advance_transfer`, a fixpoint walk once per step after the drain, so a drain carrying both the ACK and the class change closes in one step), reset in exactly one place (the deauth arm), logged once per transition, and reported off the control channel (`dpjoiner` -> `transfer`); the boot reads `st.transfer == Transfer::Closed` and nothing else. Live (`logs/v9dptrig.log.gz` line 70659, `logs/v9dpstate3.log.gz`, both routes exit 0): `Streaming` -> `Complete` -> `Collected` -> `Closed` in order, then `booted the downloaded image (1260340 bytes) ... channel 7`, and the port answers `transfer = "Closed ..."`, `booted = true`; transfer unchanged (2502 packets, 1260340 of 1260340 bytes, no `05` in the census). The joiner's answering now ends at hand-off (`run()` breaks on `is_booted()`). **Next, unchanged:** the booted child still never arms its radio, so the consoles do not link - `probe --player 2` reads pc9 `0201CD34`, pc7 `0000115C` (BIOS idle), `cmds tx 0` - the same bring-up stop v0.1.9dc/dd recorded (one RF block per power-up, `WIFI: OFF`, no TX slot armed); that child bring-up is the next thing to attack. `cargo test --lib` 39/0 (2 new, boot-boundary test rewritten onto the table), `md_smoke_test` 149/0. `logs/v9dptransfer_finding.txt` digests both runs.

**The joiner now keeps the downloaded image and boots the client into it, so the
Star Fox download ends by running the transferred title instead of a log line
(v0.1.9cz).** `Image` used to fold each DataPacket's payload into a byte counter
and drop it - a presence flag per packet, a total, an eight-byte `first_prefix` -
which is why the client could never run what it downloaded. It now stores the
payloads and `assemble()` returns one contiguous, packet-ordered buffer; `rsa_boot_info()`
parses the same RSA frame for the load map (entrypoints `00h`/`04h`, sizes
`14h`/`24h`/`34h`, the **actual** destinations `10h`/`20h`/`30h`, and the ARM7
temp staging address `2Ch`); and `JoinerBus::handoff` hands the buffer and map to
the console. On completion the client sends GBATEK's deauthentication (reason 3)
and `NDS::SetupDownloadPlayBoot` / `md_dp_boot` writes the header/ARM9/ARM7 to
the actual destinations (plus the ARM7 temp staging copy the ARM9 SHA1s), sets
the `27FFC40h` boot indicator to `0002` ("booted from DS Download Play via
Wifi"), resets both CPUs to the ARM Supervisor state a boot starts from, and
jumps to the RSA entrypoints. Nothing raced it into place: the live Star Fox run
(`logs/v9sfc_dp_boot4.log`, route exit 0, no `MD_DP_*` override) still completes
`2502 distinct packets, 1260340 of 1260340 bytes`, then `dpjoiner` reads `BOOTED`
and the bridge logs `booted the downloaded image (1260340 bytes)`. Two real
defects were found and fixed by the live runs: `JumpTo` keeps the caller's CPU
mode, so the first jump entered ARM code while the game was in Thumb and faulted
at `0200081C` (fixed by `ARM9.Reset()`/`ARM7.Reset()` first), and the writes went
to the RSA frame's *temp* destinations rather than the actual ones GBATEK names
- ARM7 must end at `30h` (`02380000h`), not its `2Ch` staging address - which
faulted the child at `02008BEC` (fixed by writing actual + temp). After both, the
client faults zero times and its ARM9 reaches the downloaded entry. **What is
still not true:** both CPUs then settle in the BIOS idle loop (`pc9 FFFF0108`)
with a static picture, so the client *executes* the downloaded title but is not
observed running its game loop; the next input is what the child ARM9 waits on
after `02000A28` (an interrupt or ARM7 handshake the emulator is not delivering).
`logs/v9sfc_dp_boot4.log`, `logs/v9sfc_dp_route4.log`; `cargo test --lib` 36/0,
`md_smoke_test` 149/0.

**The Download Play client's turn runs inside the console's own frame, and the
host is then heard on every round - while still refusing to download
(v0.1.9cy).** The delivery path is restructured: `JoinerBus::turn` is the single
entry point, the run loop owns only *when* a turn happens, and a real console
overrides `turn` with one `LinkOp::JoinerTurn` that, on the console's own driver
thread, reads the inbox (`DriverTurnBus`), advances `dp_joiner::step` and sends
every answer it decided on in the same turn. One owner for the state (the
`Arc<Mutex<State>>` the run loop holds) and one home for the schedule; no new
env knobs, and the new path is the only path. On the same no-poke route
(`logs/v9sm64d.log.gz`) `stale` goes **278 -> 0**, `collected` **31 -> 1711**,
and the host's eviction is **gone** - zero 6-byte-body no-client frames where the
old build emitted 60,055; it keeps addressing slave 1 all run. What is left of
the starvation is transport retention, and it is measured: at `MD_REPLY_KEEP=16`
(a frame's worth of the ~8.6 rounds this host asks per frame) the host collects
**26,968 of 26,977 rounds, `empty 9`, `stale 0`** (`logs/v9sm64e.log.gz`). **And
the host still never emits a `04`:** it sits at `00` RsaDone, `download: 0 of
514896 bytes`, and with collection healthy the reply-type sweep finally means
something - `MD_DP_REPLY_FORCE=00` and `=09` both park with ~15,700 of ~15,800
rounds collected and no deauth (`logs/v9sm64f00.log.gz`, `logs/v9sm64f09.log.gz`).
So reply path, type, body and freshness are all ruled out on a client the host can
hear, and the input that starts this engine's download is inside its own code.
**Next input:** the host's own DP phase/state machine for `ASME` (its equivalent
of Star Fox's `DP_SetClientMode` walk) - or, if that is confirmed host-owned, the
anchor's one-branch memory hold. A frame-sized reply retention is the transport
lever and is deliberately **not** made the default here: it is a cross-title
change that was not re-validated, and on this title it does not by itself start
the download. Star Fox re-ran end to end on the restructured binary
(`logs/v9sfc2.log.gz`, 2502 packets / 1260340 bytes).

**Super Mario 64 DS's post-RSA eviction is named: the client's answers arrive
too late, not wrong (v0.1.9cx).** The second standard-library Download Play
implementation this project drives was asked why it stops addressing the client
after its RSA round. **It is not the reply.** The five-way `MD_DP_REPLY_FORCE`
sweep (`09`, `0a`, `0b`, `07`, `00`) parks on every value, a forced `09` shows the
host *collecting* 21 type-`09` replies and still evicting, and the untouched run
already has it collecting **23 RsaReplies** before it gives up - so no reply type
and no reply body moves it, and **there is no single host branch to hold**. The
unmodified run measures the real input: the host opens a window on 2,535
addressed rounds and **collects 31, finds 2,522 empty, and voids 278 reads as
older than the transport's two-frame window**, its CMD ACK reporting the client
missing on 2,522; after ~2,424 rounds it evicts the client to the 6-byte-body
no-client command form (60,055 frames) with `download: 0 of 514896 bytes`. The
cause is that the client answers about **one round per emulated frame** (a drain
and a send are one driver-thread round trip each, and
`Instance::service_link_ops` runs once per frame) while this host asks **~8.6
rounds per frame** - and 2,204 of the rounds open with `0us of slack` already, so
nothing can be waited for. **The change that followed was measured and
reverted:** answering *every* round of a drain through a batched reply send
(`JoinerBus::send_replies`, one driver turn for N answers) triples the reads
(`147 -> 309`) but `collected` goes `30 -> 31` and **278 of 309 reads are still
voided as stale**, so it was unproven churn and the tree went back to 33/33.
`MD_REPLY_BUDGET_FRAMES=20` is worse (`collected 0`). **Next input:** the answer
has to be in the host's queue *before* its window opens - a drain/send path that
does not cost an emulated frame, or a reply window that waits for an answer
already on its way - because the host behaves correctly for a client it cannot
hear. Evidence: `logs/v9sm64b.log.gz`, `logs/v9sm64c.log.gz`,
`logs/sm64_fixed.log.gz`, `logs/sm64_force_sweep.txt`. Star Fox re-ran end to end
on the rebuilt binary (`logs/v9sfc.log.gz`, 2502 packets / 1260340 bytes).

**Star Fox's download completes, and the DataReply is the shipped default
(v0.1.9cw).** The GBATEK packet loop is implemented rather than probed: a `04`
DataPacket is parsed (packet number at body `09h`, data at `0Bh`, the spec's one
padding byte dropped so a full packet is `1F8h`), and every `04` round is
answered with a **DataReply** carrying the *smallest missing* packet at `03h` and
the distinct-packet count at `05h` - the field the v0.1.9cv note left to place,
now the natural default instead of sitting behind `MD_DP_REPLY_FORCE_DATA=09`. The
RSA frame states the whole image (`14h` header + `24h` ARM9 + `34h` ARM7 of its
payload), the **GotAllReply** (`0Ah`) replaces the DataReply the moment the stated
total has arrived, and the host's `05` Final is confirmed with `0Bh`. Measured on
two consoles through `tools/scripts/sfc-dp-host.sh` with **no `MD_DP_REPLY_*` set
and no pokes**: the RSA states `1260340` bytes, the first packet is `#0` with 352
bytes starting `53 54 41 52 46 4F 58 20` (`"STARFOX "` - real image data, not the
host's stub), progress walks packet by packet, and it ends **complete: 2502
distinct packets, 1260340 of the 1260340 bytes the RSA frame stated**, after which
the host's class goes `04 -> 00` and the joiner answers the GotAllReply.
`dpjoiner` reports the growth as `download: <bytes> of <total> bytes in <packets>
distinct packets · next wanted #<n>`. **The rest of the test set, driven or gated
live:** New Super Mario Bros., Metroid Prime - Hunters, Mario Kart DS and LEGO
Star Wars II get **no joiner thread at all** (each launched at 2 consoles:
`this title has no download-play joiner`, zero joiner lines, both consoles
running) - their own multi-card links are the ones prior passes measured, and the
registry gate that keeps auth frames out of them is a unit test now. **Super Mario
64 DS (ASME) is the second single-card title and it is registered but not yet
working:** its VS screen *is* Download Play, the joiner associates (`AID C001`),
the host runs `NameRequest -> 00 -> RSA -> 00` and states a 514896-byte image, and
then it stops addressing the client - 52,480 of its command frames are the
6-byte-body, no-client form the joiner refuses to read (against 2,536 it reads as
commands) - so `download: 0 of 514896 bytes` and never a `04`. Its touch route is
now one command (`tools/scripts/sm64-dp-host.sh`, exit 0/2/1, geometry measured
off framebuffer captures). **A real defect fell out of driving it, and is fixed:**
the joiner answers only the *newest* command of a 5 ms drain batch, but `saw_rsa`
was latched in the send arm - so an RSA landing in the same tick as the dummy
round after it was *read* (size parsed, class logged) and never *remembered*, and
every later round, including the re-sent one, was answered with a Username reply
instead. The new test
`an_rsa_is_remembered_even_when_the_round_after_it_arrives_in_the_same_batch`
fails on the old code with type `07` and passes with the latch moved to where the
frame is read; after the fix Star Fox's transfer still completes end to end.
Validation: `cargo test --lib` **33 passed, 0 failed**; `md_smoke_test` **149
checks, 0 failures**; every session quit through the control channel, no strays.
Evidence: `logs/sfc_fix.log.gz`, `logs/sfc_fix_route.log.gz`, `logs/sfc_dl.log.gz`,
`logs/sm64_fixed.log.gz`, `logs/gate_*.log.gz`.

**The mode-6 gate is pinned and the client opens it: the byte the host's
dispatcher switches on is the client reply's *type*, and the post-RSA round needs
a second RsaReply (v0.1.9cv).** The dispatcher is
`0x02086F4({message, client})`; it reads the client's mode as
`word[base + 0x14E8 + 4*(client-1)]`, jumps through the table at `0x020873C`,
and its `cmp r6,#5 / cmp r6,#14` handler runs with `r1 = base` and `r5 =
client-1` - so the halfword the v0.1.9cu note left open is
**`ldrh [base + 0x1754 + 2*(client-1)]`**, for the registered client exactly
`0x02373A34`, and it already reads **`2`** when the host is parked at 14: the
address was right, and `r1` is not clobbered on the jump-table path. What does
the moving is the reply's type byte, taken at `0x0208726` from the per-client
message and split on `7 / 8 / 9` by the helper at `0x020BB14` - `07` is
`dp_joiner`'s UsernameReply and `08` its RsaReply, and that matches every
measured walk: Username drives `2 -> 10 -> 5`, one RsaReply drives `5 -> 14`, and
the **same class-8 arm (`0x0208906`) drives `14 -> 6`** through
`DP_SetClientMode(client, 6)` at `0x0208946` when that halfword equals `2`. The
host keeps asking a `00` dummy round after RSA, and the joiner was answering it
with a type-`00` dummy, which the mode-14 arm ignores - so it parked. Answering
it with the **RsaReply** is the whole fix: with no pokes and no override the
mode field walks `2 -> 10 -> 5 -> 14 -> 6` (store watch over `0x023737C8`, the
extra `<- 00000006 @pc 02028DA4`) and the host streams a real **`04 DataPacket`,
size 179**, with `tools/scripts/sfc-dp-host.sh` returning exit 0 (`TRANSFER
STARTED`) - so a host the joiner has never poked now enters the download
direction on its own. The duplicated reply-selection match in the joiner's two
arms is now one `answer_reply` function, and two diagnostic overrides sweep the
type per round: `MD_DP_REPLY_FORCE` (post-RSA rounds) and
`MD_DP_REPLY_FORCE_DATA` (the `04` rounds only). **Next input, already probed:**
a type-`09` reply on the `04` rounds advances the host's packet loop (`04
DataPacket` size 179 then **255**) but the type-9 body's next-packet halfword
(`ldrh [sp+18]` at `0x020896C`, compared and stored against
`base + idx*0x5D4 + 0x1D4A`) still carries a username snippet, so place that
field and the download can be pulled packet by packet.
`logs/sfc_natural.log.gz`, `logs/sfc_force8.log.gz`, `logs/sfc_data9.log.gz`.

**The host's Download Play phase machine is driven by the client's replies, and
mode 14 is where a *working* joiner puts it (v0.1.9cu; its "next input" is now
answered by v0.1.9cv above).** The earlier conclusion
that "the gate is host-internal and nothing a guest sends reaches it" is
falsified for the phase machine. Unpatched and correlated on one clock, the
host's per-client phase walks `2 -> 10 -> 5 -> 14` (four stores from the same
instruction, `pc 0x02028DA4`, plus two init clears) while its command class walks
`01 -> 00 -> 03 -> 00` in **243 ms of emulated time**, each phase store landing in
the same 150 ms sampling window as its class change. With the joiner associating
and then answering **nothing** (`MD_DP_REPLY_MUTE=1`, the new diagnostic arm, so
nothing before the association differs), the host's phase cycles `2 -> 3 -> 0`
and never reaches 10, 5 or 14, its class changes exactly once - to `01` - and
stays there for 20,683 commands with 0 replies collected in 20,069 empty windows
and 13 deauths (`reason 0001`). So the client is **causally upstream**: 14 is
reached because the joiner answers the name phase and the RSA round, not by any
host clock and not as an abort. **The next input, named but not pinned:** the
Thumb dispatcher at `0x02028906` sets mode 14 on `phase == 5`, and sets **mode 6**
(the download direction) on `phase == 14` **only if** a per-client halfword read
as `ldrh [r1 + 2*(2*client) + 0x1754]` equals `2`; otherwise it returns with the
phase left at 14. Pinning that halfword's address (a live read with the
dispatcher's `r1` captured) is the first step of the next pass, and the phase
word itself is written by `0x02028E36` (`3`, then `2` repeatedly) and cleared to
`0` by `0x020288CA` just before `movs r1,#5`. No host-memory patch was used this
pass. `logs/sfc_base.log.gz`, `logs/sfc_mute.log.gz`.

**The hand-over is the WM message length, and holding it makes the host stream real
`04` DataPacket frames (v0.1.9ct).** The composed message is not lost at the
ARM9->ARM7 boundary - it crosses it as `(slot << 6) | 0x0A`, the type-`0A` WM
message the ARM9 pushes down the IPC FIFO. The address is one of **eleven
0x100-byte slots** at `0x020B4C40 + 0x100*k`, which the ARM7's own wireless
library lists in a descriptor at `0x023F8338` `{table 0x023F8348, size 0x20,
count 0x0B}` (found by scanning the dumped library for the ring literal
`0x020B5440`). Each slot holds the same record - the ARM7 never clears one -
`{id 0x800F; buffer 0x023722E0; length; 0xFFFF; 1; 3; ARM9 callback 0x0202B085}`,
where the buffer is the host's DP struct whose first byte is the class. The
composer that builds the wifi-RAM TX slot is `0x023EFC18`, ARM7 code in main RAM
(the wifiio pc `0x027EFExx` is the `0x02000000` mirror - `ARM7Read32` maps
`addr & 0xFF800000`), and its body copy is gated by `[sp+4] != 0` **and**
`[r5+0x1C] != 0`, with slot `0x0A` set to `[r5+0x1C] + 0x22`. **That length is
the sender's own input, and holding it drives the body:** measured with
`tools/scripts/sfc-dp-data-frame.py`, message length `6` -> a 14-byte frame body,
`0x40` -> **72**, `0x1F8` -> **512**, `0` -> **no body at all**. So v0.1.9cs's
"358 bytes never reaches the wire" was a property of the forced engine path, not
of the hand-over; the host even emits a 242-byte class-`03` frame ten times during
the handshake, so long bodies were never the obstacle. **The block layout, as the
capture-side spec:** `06 01 02 00 | PP | 11 | CC | <payload>`, six-byte DS header,
`PP` the DP packet number the host increments, `CC` the class, payload the host's
own buffer from `0x023722E0`. Captured: `body[72]=06 01 02 00 20 11 04 50 08 00 02 00 00 38 02 00 00 00` (93 in a 6 s hold, the joiner collecting `04`) and
`body[512]=06 01 02 00 FC 11 04 50 08 00 02 00 00 38 02 00 00 00` (38). **Still
open, and it is the whole remaining distance to a playable download:** the payload
is the host's own stub, because the host's engine still never fills
`0x023722E0` with image bytes - the transport is proven, the download is not. The
patch also perturbs the host's own state machine (after the `0x1F8` sweep it
deauthenticated the joiner, reason `0001`, and moved to another DP phase), so a
real fix has to come from the engine's own path rather than from held words.
`logs/sfc_ho2.log.gz`.

**The per-client mode is set by one host routine, and forcing the class-04 chooser
builds a real payload that is never handed over (v0.1.9cs).** Pointed at the mode
field (`base + 0x14E8`), the store watch sees **zero** stores after the handshake
- the field is written once per state - so it has to be armed first, and it then
shows the engine walking its own machine: two init clears, then **2 -> 10 -> 5 ->
14**, each from the store at `0x02028DA0` in IRQ mode. The writer is
`DP_SetClientMode(client, mode, ctx)` at `0x02028D84`, whose guard `0x0202817C`
is only `1 <= client <= 15` - a bounds check, not a content gate, so **no frame a
guest sends takes part in choosing the mode**. Its twelve call sites carry one
mode each (`10, 11, 5, 4, 14, 6, 7, 8, 1, 2, 9, 3`). **Why mode 5 means class 3
is now code:** the poll (`0x02028228..0x020282FC`) routes on one halfword to one
of two choosers; the live one is `0x020852C`, which histogrammes clients whose
mode is **5** and emits class **3** itself at `0x020285FC`. The class-**4**
emitter (`0x020283A2`) is inside the *other* chooser, `sub_020830C`, which the
live engine never calls. **Forcing the routing** (`poke 020282DC = E0022800`,
a `beq`->`b.n`, plus the slot's `+0x1D4C` u16 = 1) makes the engine build a
**real payload**: the class byte is written `04` from `lr = 0x020283C9` (the
class-4 site), the body is `04 00 00 00 00 53` followed by **"STA RFOX CMD "**
(the game's own data), and the WM message ring `0x020B5440` goes from length `6`
to **`0x166`** - 358 bytes. **But nothing reaches the wire:** the TX slot's class
byte stays frozen at `n=39720` while the slot is still armed `73,008 -> 73,415`
(retransmitting its old body), the ARM9->ARM7 IPC keeps flowing, and the only
frames in the tail are `tx frame: slot 1 (CMD) fc=0228 len=34` - the short form
the joiner's own length guard refuses to read as a command, so `commandsRecv`
freezes and the class stays `00`. **Next pass:** the hand-over. The class-4
decider fills the ARM9 buffer and the wireless library never picks it up, so the
missing piece is whichever call hands the 358-byte message to the ARM7 (the
message ring at `0x020B5440` and the IPC push at `0x02030660`, from v0.1.9co)
- and whether the poll's flag halfword at `+2`, which selects the chooser, is
reachable from any client state.
**Superseded in part by v0.1.9ct:** the message does cross the boundary (as
`(slot << 6) | 0x0A`, with the length at `slot+8`), and holding that length puts
the host's payload on the wire, so nothing was stopping at the boundary. What
that pass measured instead was a *forced* engine path whose descriptor length
came out zero, which is the composer's own `[r5+0x1C] != 0` gate.
`logs/sfc_modewriter.log.gz`.

**The route is one unattended command, the reply timing is fixed, and the host
still parks at RsaDone (v0.1.9cn).** `tools/scripts/sfc-dp-host.sh` now runs the
whole route - walk the menus to the host role, wait for the joiner to be
registered (`AID != 0`), tap Cut Off at the measured DS `(91,178)`, then poll
for the transfer's first `0x04` - and its exit code is the state (0 = `0x04`
seen, 2 = host role but parked, 1 = no host role). The joiner's reply timing was
the real defect: answering one command per step (oldest first, ~12.5 replies/s
against the host's ~166 commands/s) left all 19 RSA windows empty, so the host
abandoned RSA and our `RsaReply` was read against a later dummy round and
ignored. It now answers the **newest** class in the drain batch - the reply path
keeps only the newest answer per client (`MD_REPLY_KEEP=1`) - and the host
collects the `RsaReply` *inside* the RSA round and moves on
(`logs/sfc_final.log.gz`). A second measured rule: the name phase's `00`
(NameDone) must be answered with the username (a dummy there and the host never
sends RSA at all - measured twice; the username there and it sends `03` -
measured twice). Three reply-level guesses were tested and **removed** because
they bought nothing: answering every command in a batch (floods the single-slot
answer queue; the host deauths, reason 0001), a normal dummy on RsaDone, and a
DataReply (`type 09, next 0`) on RsaDone. **What the parked state is, as
numbers:** the host's command class stays `00`, it never writes a Data command
(its CMD slot stays armed with the dummy; every command frame is 54 bytes), its
reply window collects *every* reply we send (715/715) with `stale 0 deauth out
0`, its ARM9 alternates `02030488`/`020A35A0` with `IME=0001 IE=2009 IF=0000`,
and its own wifi read loop is ordinary MP polling (`W_IF`/`W_IE`, `W_TXStat`,
`W_TXSlotLoc1/2/3`, `W_CMDStat0`, `W_TXReqRead`) - not a Download-Play register.
The park begins *before* Cut Off, so the host's own game holds it, not the wire.
The received name lives in an entrant record at `0x020B4C20` and the DP title
records keyed by serial `00400218` sit at `0x020B3D40`/`0x020B5640`, so the host's
DP state is addressable - the next pass starts by watching what changes in it at
Cut Off, not by guessing another reply. `cargo test --lib` **27 passed, 0
failed**; `md_smoke_test` **141 checks, 0 failures**. New instrument:
`appctl.py probe --player N` prints both CPUs' PCs, register files, interrupts
and MP counters.

**Gated on work above it:** the instruments (the memory read is also how an
arrived image is confirmed); **Milestone 2's per-instance routing**, because
Download Play is one parent and *N* children and a machine-global `MPHostinst`
cannot route for two joiners; and its own **bulk-transfer measurement** — a
Download Play transfer is a large one-shot download, a different shape from the
small polled exchange `MD_REPLY_KEEP`'s backlog and the reply budget were tuned
for, so it needs its own counters rather than inferred ones.

- [ ] **A 2-console single-card join.** Star Fox Command's Recruitment is the
      ready-made target: its host half is already proven, so the client is the
      only unknown
- [ ] **Assert the download from the joiner's own RAM** (the memory read above):
      the joiner is running the *host's* code, so "the image arrived" can be a
      value instead of a picture
- [ ] **Super Mario 64 DS's VS mode** — the folder's other single-card title, and
      the more useful one, because it is a 4-player mode
- [ ] **The same at 4 players**, which is where Download Play pays off, and where
      its per-console cost can be compared against four full-ROM consoles

**The booted child's own IPC traffic is now measurable, and what it says is that the child's ARM9 stops mid-sentence while its ARM7 keeps offering frames (v0.1.9dn).** The instruments that answer "does the booted child's ARM7 service its ARM9's IPC" are the FIFO counters and the traced words, and `NDS::Reset()` - the boundary a Download Play child crosses to boot the image it downloaded - cleared the FIFOs without clearing either, while `src/NDS.h` documents them as "since boot". A frontend reading them after a hand-off therefore saw the firmware Download Play client's traffic with the booted child's mixed in, which is where the record's "the child's ARM9 sent 103 words" came from: 103 was the firmware client, the child's own number is **17**. Fixed at the source (`src/NDS.cpp`, `NDS::Reset()`: zero `IPCSent9/7`, `IPCRecv9/7`, `IPCTraceN9/7` and both rings, the same per-boot rule the wifi register record already had) and pinned by a smoke-test check that crosses the real boot path (`md_reset`): **162 checks, 0 failures**. Live (`tools/scripts/sfc-dp-host.sh` exit 0, transfer byte-identical at 2502 packets / 1260340 of 1260340 bytes, child booted 20:54:53): four matched probe samples read the child's ARM9 at **5 → 17** words against its ARM7's **742 → 3614** (~360 words/s), and the child's stored trace holds all 17 with their pushing instruction (`@0201CEF4 lr=0201CEE0`, one from `@0201CF14`) - a short run of four pattern words then twelve at stride `0x4000` that repeats its own first two and stops. **The silence after word 17 is the child's decision, not a dropped push:** at every sample the ARM9's own control reads `fifocnt9 8501` (enable set, send FIFO empty) and `7recvs` has taken every word `9sends` offered, so the emulator's send gate (`src/NDS.cpp:3997`, in the ARM9's `0x04000188` case) is open and the queue is drained. That moves the blocker off the IPC channel and leaves the child's own wait - consistent with its register record, which stops with `USCOUNT 0`, RX buffer begin/end 0 and no TX slot ever armed. `logs/audit5_finding.txt` digests the run.

**The booted child's own game is never handed a frame, and the frames the router keeps writing to its slot are counted and dropped rather than latched where the game looks (v0.1.9do).** v0.1.9dn left one condition to name - what the child's ARM9 waits for after word 17 - and two live routes (child = console 2, 1,260,340 bytes booted, session carried at the register level: channel 7, BSSID 00:09:BF:11:22:33, AID 1, own MAC 12:BF:09:00:43:66) put it at the bus-to-console boundary, not in the IPC channel. **Falsified, live:** the GBATEK Download Play session block at `0x027FFC40` is what this fork's boot leaves zero beyond the `2` it writes at the indicator, so all seven fields were poked 0.4s after the boot with the host's own beacon values (`00600002`, `0900FE82`, `332211BF`, `40000020`, `00191802`, `00210000`, `7`) and re-poked at +10s; `mem` confirms the words stuck and **nothing changed** - `9sends` stayed 17 and pc9 stayed `0x0201CD34` for the next 2,000 frames. **Named, with the ledger:** on the frame of the boot the child's own counters freeze (`tx 7083`, `rx 14314`, `game 0`, `drain 14314`, and its own picture freezing about 150 frames past the boot, still frozen 9,578 frames later when the ledger was read) while the router **keeps delivering to its slot** (`delivered` 14,435 -> 17,611); the child's game reads zero frames for the whole run, and its own wireless-RAM reads say why - 512 consecutive halfwords at offsets `0x1C00`-`0x1F7E` all return the boot pattern `A5A5/5A5A`, its RX window is unconfigured (`begin=end=writecursor=readaddr=count=0`) and `handed len=0`. The mechanism, and now the state, are pinned: `LocalMP::Route` queues a frame only for consoles that are still `attached` (`src/net/LocalMP.cpp:372`), queues it into that console's inbox (`:383`) and counts it delivered (`:390`) - at which point its own comment says whether "the console's own game reads it, or a scripted drain does, or neither ... comes later and is a different question (see `GetDelivered`)". The child's answer to that question is `game 0` with unconfigured RX registers: **an attached console whose own reader (`RecvPacketGeneric`, named at `:397`) is not armed.** The boot is what detaches it - a whole-console `Reset()`, after which its bring-up power-cycles the wifi block (`Wifi::UpdatePowerOn` -> `Platform::MP_Begin/MP_End` -> `LocalMP::Begin/End`, three times at frames 3591->3595 and 3800->3801) - and the joiner's hold re-attaches the bus (`src-tauri/src/dp_joiner.rs:1503`) but not the game's receive path; the same hand-off states it at `src-tauri/src/dp_joiner.rs:1477`. **The child is neither deaf nor blocked:** its ARM7 firmware pushes 33k words from the same site as the working host's (`@037FEA60 lr=037FEA4C`) and its ARM9 has read 34,909 of them (`9recvs == 7sends`, `fifocnt9 8501` open); its 17 words are a complete protocol burst in the same encoding the host's working ARM9 uses (`85DB100A ... 85DD500A`, second byte stepping `0x40`, against the host's `82D3D00A ... 82D5500A`). One word separates the two firmware streams: the host's interleaves `00014007` with its `C02400xx` cycle, the child's never emits it. **Next:** arm the booted child's own receive path (or latch the delivered frames into its wireless RAM) instead of only counting them, then re-run the route and watch `game` leave 0 on console 2. `logs/audit8_finding.txt` digests the run. **Superseded by v0.1.9dp:** measurement shows there is no receive path to arm at that point - the window is unconfigured at every moment, before the boot too, so the frames cannot be latched by design and the shape above is expected rather than the defect.

**The booted child's receive path is not armed at any moment, so "arm it after the boot / after the joiner's re-attach" is falsified as a fix, and the bring-up stop is narrowed to the single branch between TRX-on and not (v0.1.9dp).** v0.1.9do named the state (an attached console whose own reader is not armed) and prescribed arming it; this pass measured the gate and killed that prescription. The gate is three lines at the top of `Wifi::CheckRX` (`src/Wifi.cpp:2756`): `W_PowerState` bit 9 clear of RX-off, `W_RXCnt` bit 15 set, and **`W_RXBufBegin != W_RXBufEnd`** - only past those does it call `MP_RecvPacket`/`MP_RecvHostPacket` (`:2778`/`:2784`) and latch into the wireless RAM, which is faithful (hardware cannot latch into an unconfigured window either), so bypassing it is not the fix. **The child's window is zero at every one of 26 samples spanning before and after its boot** (`appctl wifiio --player 2`, `logs/audit9_p2_regs.txt`: `begin=0000 end=0000` with `role=0` pre-boot and `role=2 (client)` post-boot), against the running host's `role=1 · USCOUNT=3510FC8 · USCOMPARE=2383800 · zone gen=5471 · handed start=1638 len=72 fc=0080`. So the console that received the 1,260,340-byte download was never this console: it was the app's joiner at the bus level (`game 0 drain 14314` for the whole run), there is no pre-boot configuration to carry across the boot, and a bus re-attach cannot re-arm what the game never armed. **What the fix must therefore be is the child's own bring-up, and the stop is now one branch wide:** both consoles run the same sequence with the same values from the same PCs (`W_ModeWEP` ramp `003F/1A/25/34/00`, `W_PowerUS 0000`, `W_PowerTX 0002`, `W_RFCnt 0114`, `W_ModeWEP 0002/000A`, `W_BBPower 800D`, byte-identical RF read sweeps), and at `pc7=027EA55C` (`W_ModeWEP <- 004A`) the host continues with the transceiver-on sequence - `W_PowerForce <- 8001`, `W_PowerForce <- 0000`, `W_PowerTX <- 0003`, `W_ModeReset <- 0001` ("TRX power ON"), after which `CheckRX` hands its game a frame - while the child never writes `W_PowerForce` or `W_ModeReset` at all, ends on `W_BBPower <- 800D` / `W_PowerUS <- 0001` (block off) and parks in the BIOS idle (`pc7 0000115C lr7 037F84FC`) with its wifi raising nothing (`raised none · W_IF 0000 W_IE 0000 · bcn 0 0 · uscnt 0`). The branch between `pc7=027EA55C` and `pc7=027EA6AC` is the next read: the still-open v0.1.9di leads are what the thunk at `0x038043E3` returns in the ARM7 retry loop (`0x037F84F8`-`0x037F851C`) and what the ARM9's `0x02087350` callee tests at its `0x0201C7FC` site. **Nothing was landed:** every surviving candidate either bypasses a hardware-faithful guard (inventing frames the game would not look at) or patches a decision point without knowing the value it branches on, and both would be unverifiable claims. Validation: cold build exit 0, route exit 0 with `TRANSFER STARTED` and the child booted, app exit status 0, five `Test Roms/*.sav` byte-identical, no strays. `logs/audit9_finding.txt` digests the measurement.

**Upstream melonDS has no Download Play path at all, so the divergence is architectural: this fork implements the *client* outside the console, and the console whose radio was never brought up has no reason to listen (v0.1.9dq).** Measured against the local `upstream/master` (906e9ebb): `git grep -i "downloadplay|download play|multiboot|027FFC4|0x04003000"` over upstream's `src/` finds only the *direct* boot indicator (`upstream/master:src/NDS.cpp:329`, `ARM9Write16(0x027FFC40, 0x0001)`) - no boot-a-downloaded-image path, no joiner, no carry. `Wifi::CheckRX` and its `MP_RecvPacket`/`MP_RecvHostPacket` receive path are upstream's own (`upstream:src/Wifi.cpp:1564/1587/1593`); what is the fork's is `src-tauri/src/dp_joiner.rs` (the Rust client that speaks the protocol), `NDS::SetupDownloadPlayBoot` (`src/NDS.cpp:439`, a whole-console `Reset()` plus staging, called from `src/frontend/tauri/Bridge.cpp:1618`) and `Wifi::SetDownloadPlaySession` (`src/Wifi.cpp:3340`, the identity carry). Upstream needs none of it because on hardware the *guest's own code* is the Download Play client and the emulator only carries frames - and the same run proves the emulator's path works when the guest owns the radio: the host console, running this fork's own Wifi code and the same game, has `W_RXCnt 8000`, `W_PowerUS 0000`, window `50C4/5F60`, its microsecond clock at `23D5D98`, `bcn 204 8` beacons heard, `cpu line 20682` wifi interrupts and frames handed to its game, while the child - whose radio the joiner stood in for - reads `W_RXCnt 0000`, `W_PowerUS 0001` (off), window `0000/0000`, `us 0/0`, `bcn 0 0`, `cpu line 0`, `W_IF 0000`. **Arming the child's block from outside is measured insufficient:** poking the host's own block into it (`04800030..60` with `W_RXCnt 8000`, the `50C4/5F60` window, split read/write cursors) took, and the console came back onto the bus (`LINK OFF` -> `LINK IDLE`), but `game` stayed `0` for the following 60 s with `rx 14358 drain 14358 handed len=0`, because `Wifi::CheckRX`'s first guard is `W_PowerState` bit 9 (`src/Wifi.cpp:2758`) - a guest-driven power state that reads `0200` on the child and that only the console's own driver clears (the host writes `W_PowerState <- 0002`; the child never does). **What has to go, and the smallest correct change:** the client side must be the console's own guest code, not the joiner. Run the child console into its own Download Play path (the firmware's DP applet, or the game's own DP client) instead of autostarting a cartridge into it and letting `dp_joiner.rs` speak for its radio; its client-side rounds (auth/assoc/replies/DataReply) then have no job, and once that console's own driver has brought its block up and received the image over the ordinary receive path, `md_dp_boot` / `SetupDownloadPlayBoot` / `SetDownloadPlaySession` are unnecessary too. That replacement is a real guest DP client, larger than one pass, and is deliberately not half-landed. Validation: cold build exit 0, route exit 0 with the transfer complete and the child booted, five `Test Roms/*.sav` byte-identical, app exit status 0, no strays; `logs/audit10_finding.txt` digests the comparison.

### Parked deliberately

- **Online / WebRTC** (Phase 5) — still secondary. Download Play is *not* parked:
  it is Milestone 4 above, because it is compatibility work

---

## Phase 1: Native Bridge (Host Infrastructure)

### Todo
- [x] **A player's input is now attributed to the peer's picture at frame
      granularity - the app-side claim has pixels, not just counters**
      (history.md v0.1.9ai). Three runs of `tools/nsmb_join.py --restart
      --couple-fine` (four phases, sampled in the page at ~70/s, counting only
      pairs where both consoles advanced exactly one frame): with the guest
      alone holding RIGHT and the host's own input trace owning **0** lines in
      that phase, the host console's own rendered pictures moved **1021 / 1739 /
      1021 / 0 px** of its top view (a band at y 155..178) and **1927 / 1910 /
      1857 / 740 px** of the shared bottom screen that moved in *neither* no-input
      baseline - so the bottom screen is what holds in all four runs and the
      host's own view in three of them, the fourth's guest having walked less -
      while the same press moved the guest's own screens by 29,000-31,700 px, so
      the control is loud. The mirror phase (the host alone holding RIGHT) is the
      same shape (the guest's pictures move 526 / 385 / 582 px), which is what
      rules out an animation the baselines happened to miss, and the two
      consoles' bottom screens differ by **0 px per pair**: the bottom screen is
      one picture both consoles render, so its figures are one picture moving.
      What is still open: a value out of NSMB's own RAM for "the peer's world
      changed" (the app still has no memory-read command), and the fact that this
      measurement needs the *race* state - ten minutes later in the same session
      the guest's RIGHT no longer moved its own screen at all, and the run then
      says `inconclusive` on purpose rather than claiming the peer from noise.
- [x] **The lobby-phase reply loss was a reader bug, and the larger timeout is
      not what loses replies - the per-window ceiling and the per-frame budget
      were measured against each other and the budget is what binds**
      (history.md v0.1.9ah). `collected 0 empty 1021` was never a counter: the
      digest's capture groups were read positionally one field to the left, and
      the app's own digest at those same frames (4081/5100) reads
      `collected 1021 empty 4` / `collected 2217 empty 4` - 99.8% of replies, and
      the 4 never grow again in a 14,503-command session. Fresh runs of both
      surfaces at the same frame: app (frame 5101) `cmds tx 2221 acks 2221`,
      `collected 2217 empty 4 clientfail 4 reanchor-dropped 3`; harness
      (frame 5200, `logs/nsmb_harness_frame5200.log`) `cmds tx 2325 acks 2325`,
      `collected 2325 empty 0 clientfail 0`. The timeout is irrelevant to that
      gap in both directions: the app at `MD_REPLY_TIMEOUT_MS=2` reads
      `empty 1` at the same frames and the app at the default 34 reads `empty 4`.
      What the ceiling *does* decide, measured on the app's own MKDS route:
      with 34ms and a 2-frame budget the budget capped the wait in **2678**
      windows and the ceiling never bound, closing 43; with 2ms the ceiling was
      below the budget, capped the wait in **0** windows (nothing was ever
      budget-limited) and closed **394**, taking `clientfail` and
      `reanchor-dropped` to 394/393 with it - answers the peer *did* send, thrown
      away by the next command. So keep the ceiling above the budget
      (`MD_REPLY_TIMEOUT_MS=34` against a 2-frame budget) and note the knob was
      mislabelled: it is `MD_REPLY_BUDGET_FRAMES` now, takes emulated frames, and
      `run_app.sh` forwards it (it forwarded only the timeout before, so the
      budget could not be swept through the app at all).
- [ ] **The residual lobby-phase difference is a four-round startup transient,
      not a collection failure, and no reply knob moves it** (history.md
      v0.1.9ah). At the same frames the app loses 4 of 2221 rounds and the
      harness loses 0 of 2325; the app's `empty` is 4 at the default ceiling and
      1 at `MD_REPLY_TIMEOUT_MS=2`, and it stops growing by frame ~4081 and stays
      at 4 for the rest of the session. The guest answers every command it is
      handed (`answered 14503 = cmds rx 14503`, `canned 0`), so the 4 answers
      exist and are dropped host-side; 3 of them are `reanchor-dropped` (arrived
      after the host moved on). Next measurement: the first ~1025 command rounds
      only, with `MD_LINK_VERBOSE=1`, watching the first window that closes with
      `collected=0000` and whether the answer for that seq arrives before or
      after the next command goes out - the guest's radio comes up at frame
      ~2735 and the counts move between the frame-3580 and frame-4081 digests,
      so the rounds in question are the first few after the bus comes up.
- [ ] **The +1830 s drop is neither a fixed offset nor input-driven, and delivery
      is clean all the way through it: a linked harness run crossed the whole
      window with no input and never dropped, while the same drop signature
      reproduced at +67.9 s of emulated time in a run of the identical route - so
      what is left to find is the nondeterminism itself** (history.md v0.1.9aa).
      The documented `md_game_session` NSMB route was the blocker and is fixed: it
      had been timed against a *fresh* save's boot flow and linked nothing
      (`slots on the bus: 00000`, `sent=0 received=0`, no radio ever powered a
      second time). The route that reaches the game's local-wireless play on this
      tree and this save is the one the app's live session actually walked
      (`--touch 612:128:95 --touch 1292:128:72 --touch 2012:127:119@1
      --touch 2012:127:160@2 --touchtap 128:171:2600:160 --autotap A:2700:60`),
      and with it the harness links both consoles, holds one command per frame
      and runs at 3.5-3.7x realtime. Two instruments came out of needing a
      controlled A/B on a route made of repeating taps: `--stop-input FRAME` ends
      every input that begins at or before FRAME, and `asserted()` now honours a
      periodic input's last frame (it never did - a repeating tap was
      unstoppable, which is what made the A/B inexpressible). The harness now
      also prints the emulated offset of the session's last command and answer
      (`at +NNN.NNs`), the same reading the app prints, which is what makes two
      arms comparable at all.
      **The no-input arm: no drop, and no delivery defect.** 125,000 frames =
      2,089.4 emulated seconds, 122,126 commands, 122,126 answers, one answer per
      command, `deauth out/in=0/0` on both consoles, `stale 0`,
      `reanchor-drop 0`, `clientfail 1` (the startup round), both slots on the
      bus at the end and the last command at **+2089.37 s** - 259 s *past* the
      app's drop. So nothing in a linked NSMB session ends at +1830 s.
      **The reproduced drop, and it is not input either.** A second run of the
      identical route (the only difference being the guest tapping RIGHT every
      two seconds from frame 13000, i.e. 150 s *after* the drop) ended at
      **+67.87 s**: `deauth out/in 1/0` on both consoles, `last deauth reason
      0003` on both, both radios off inside 1 ms (`wireless off (end #3)` /
      `(end #2)`), and the guest's last answer a six-byte farewell
      (`body[00 80 F0 04 B8 B6]`) instead of the usual 70-byte one - the app's
      signature exactly, at a different offset. `[CLIENT] deauth` precedes both
      power-offs, so the guest's own game leaves first and the host follows.
      Sixteen further runs of the same route (eight sequential, eight as two
      concurrent pairs, 8,000 frames each) dropped **0 times**. So the event is
      rare and nondeterministic: two drops in roughly 3.5 ks of linked session,
      which is the rate the app's single 30-minute run fits.
      **What still is not explained, and where to look.** Nothing in the counters
      shows a lost, stale or re-anchored frame at either drop, and the app's
      `clientfail` had not moved since its first seconds - so delivery is not
      implicated by any number the tree records, and the remaining suspect is the
      one wall-clock-dependent thing in the path: the reply window's per-frame
      budget, which binds in the app and never binds in `md_game_session` (the
      harness publishes a fixed 33 ms budget against ~5 ms frames). Catching a
      drop with `MD_LINK_VERBOSE=1` in a run paced like the app's is the next
      measurement; and the app's own digests at the drop show the wireless was
      **off**, not on, so the earlier reading of "wireless still powered" is
      wrong. The screen the
      earlier passes read as "where the app route ends" is three centered lines of
      the game's own font on black, read off the capture that is pixel-identical
      to it (`nsmb-play2` f10800 at mean abs 0.0; `nsmb-play4` f24500 at 4.3):
      line 1 "Connection interrupted", line 2 "Press the . Button to", line 3
      "return to the title ..." - the font calibrated on the documented
      `nsmb-park-guest-pleasewait` capture, whose text its filename gives. It is
      *not* a race both consoles are in: it is the game's post-drop dialog, and it
      waits for a button (pressing A on the guest cleared it - the top screen's
      red sum went 412,531 -> 3,077,277, the title screen - and the host's picture
      resumed changing while the link stayed down). The drop itself is in the
      app's own log: at +1829.95s the last exchange (`cmd seq 1940`), then
      `deauth out/in 1/0` on *both* consoles at once with the wireless still
      powered (2/2 and 3/3), `last ack clientfail`, tx/rx frozen, `link mask 0x0`,
      and the monitor's own `p1 looks wedged: the program counter moved in only
      6/100 samples while 300 frames ran`. Before that drop the app route *was* in
      the harness's state: 34.2/38.4 mean abs from `nsmb-fix` f11500 - the capture
      the docs call the course - against 126.7 from `nsmb-play4` f29500, with the
      app's own two consoles 35.7 apart and the harness's own two course captures
      44.5 apart, the same spread. The peer effect, on the instrument the last
      pass named as sufficient: the full 102-byte frame the host's game is handed
      carries the guest's keypad at byte 40, time-resolved (0x00 -> 0x10 exactly
      at the press, 0x00 again exactly at the release) and 36/36 clean frames
      against 0x00 in all 129 baseline samples; holding RIGHT on the guest also
      left 1,125 px of the host's top screen (bbox 3,120-201,180) that moved under
      the input and never in the three no-input baselines.      **Still open from this item:** a value
      out of NSMB's own RAM for "the peer's world changed" (the app has no
      memory-read command, so the full-frame dump is the closest instrument), and
      `key Bogus` on the control channel, closed in v0.1.9ab (`key`/`index` now
      ask the page's own `window.__mdkey`/`window.__mdselect`).
- [ ] **The app's own surface is verified on the post-prune tree, and a driver
      needs two facts it did not have** (history.md v0.1.9x). Run as a player
      would (`tools/run_app.sh` with autostart, everything after that through
      `tools/appctl.py`): the documented NSMB route reaches the course through
      the control channel, with the wire in the session-data phase (262/266-byte
      commands, 70-byte answers, `clientfail 0`) and both pictures changing every
      frame; MKDS through the app lands where it always lands; every surviving
      command (`status` `ui` `see` `bands` `shot` `render` `press` `touch`
      `click` `text` `eval` `advance` `waitfor` `index` `vary` `wifiio` `quit`)
      behaves as documented, and `MD_SHOTS_DIR` is forwarded (`shot` lands in it).
      Two empty-state sentences were wrong and are fixed: `see` and `bands` on an
      app with no session answered "player 1 out of range: 1..0" / "player 1 of 0
      -- out of range" - a range with no player in it - where the truth is that
      nothing is running; both now say `(no session: no console is running)`.
      **The trap, in one line:** do not `pkill -f melonDS-Multipl` - the pattern
      matches the calling shell's own command line, so it kills the shell and
      leaves the app alive with its session, still holding the control port and
      answering `status` with *its* frame counter; a route paced on that number
      lands every tap at the wrong emulated frame (that was the park two
      app-driven runs were mis-attributed to). Use `tools/appctl.py quit`, or
      `run_app.sh` (which kills the previous pid and waits one second), and check
      `status`'s `up Ns` against its frames first: a 60 fps console cannot have
      run more than ~90 frames per elapsed second. Still open from this item: the
      Link test's injection arm. `key Bogus` was accepted silently here and is
      closed in v0.1.9ab (`key`/`index` ask the page's own
      `window.__mdkey`/`window.__mdselect`, and a name the page cannot act on is a
      command failure, not data).
- [ ] **MKDS's post-VS latch is inside the games, and the frame-identity lever
      is now measured and closed** (history.md v0.1.9v). What a reply's identity
      fields carry is settled from GBATEK and upstream melonDS: the sequence
      number is stamped per *transfer* from `W_TX_SEQNO`, the retry bit is the
      transfer's own and never a field the hardware invents, and the byte stock
      melonDS reads to decide "this is a retransmission" (`TXHDR[04h]`) is the
      game's, which the hardware leaves alone. The fork had been letting its own
      reply count decide that identity, so a fresh answer came out of a used
      buffer with the previous frame's sequence number and the retry bit set -
      and after the fix (a reply is always a new transfer: fresh sequence number,
      no synthesized retry marker, the buffer's own bytes left byte-for-byte as
      stock writes them, because two builds that dropped the count parked the
      NSMB route that plays with it) the re-run answers the question rather than
      leaving it open: **MKDS still latches** - join, CUT OFF, VS, then the same
      static screen on both consoles with `LINK OFF` and the host frozen -
      while every one of the guest's 15085 replies goes out as a new frame
      (`fc=0118`, zero repeated sequence numbers, against 1261-6633 retry-marked
      deliveries per pre-fix run). So the latch is not a deduplication artifact:
      the host's game has the guest's answers, as new frames, and still does not
      act on them. The next leg is what the two games do with each other's data
      once the lobby closes - MKDS's drivers never open the session-data phase
      NSMB's do (see below) - rather than anything left on the wire, and the
      smaller open items (the Link test's injection arm, and `appctl.py`'s
      `key Bogus` until v0.1.9ab closed it) are unchanged by this pass. The latch is reproducible on
      demand and nothing on either console or in the delivery moves it: with
      the host's *frozen* screen as the read, the full button set
      on p1, then on p2, then six touches across each console's own bottom
      screen changed neither the screen nor the exchanged state, and the link
      stayed up throughout. The differential against the title that plays, on
      the same transport in the same build, is quantitative: once NSMB's shared
      screen is up its two games switch to **290-byte commands and 94-byte
      replies** (266/70-byte session bodies), while MKDS never sends a frame
      larger than the lobby's **34-byte command / 30-byte reply** - the biggest
      MKDS frames (238/402 B) are the join's own roster exchange, seconds
      *before* the transition. So the games' drivers never open the
      session-data phase, and no size limit, refusal or loss on our side can
      explain that (the same transport carries NSMB's 290-byte frames and
      MKDS's 402-byte ones).      **The value half of that is now closed, and v0.1.9s's "the matrices wrote
      into the FCS slot" is retracted (history.md v0.1.9u).** Measured off the
      archived runs byte for byte: the command body the guest's game is handed is
      **ten** bytes - `BE 01 | 02 00 | 00 80 | FF 00 | B8 B6`, i.e. clienttime,
      clientmask, flags, state, marker - so the six bytes `BE 01 02 00 00 80` the
      v0.1.9t item calls "the real command body" are its first three fields, and
      the field it wants varied at offset 4 is the *flags* halfword, not a state.
      Mask `0x00C0` is body bytes 6-7 = the host's state, inside the delivered
      body; the guest's own digest proves the cells arrived (one cell per five
      seconds: `02 00`, `03 00`, `01 00`, `00 00`, `06 00`, `04 00`, `5A 5A`,
      `11 01` at offsets 6-7 of `last: cmd ... body`, recorded after the hook out
      of the same `RXBuffer` the game DMAs, while the wire still carried the
      host's own `FF 00`). The reply the host's game is handed is a 30-byte frame
      with a **six**-byte body (`00 80 | 00 00 | B8 B6`), so mask `0x000C` is its
      bytes 2-3 = the guest's state, also delivered, and the host-side cells show
      up there too (`body[6]=00 80 03 00 B8 B6`, `00 80 04 00 B8 B6` - values the
      guest's own walk never wrote). So every state halfword either console is
      handed has now been varied, on both sides, with null and echo controls, and
      nothing moves: the state *value* is not what MKDS is waiting for.
      **The identity of the answer was the next lever, and it is now measured and
      closed** (history.md v0.1.9v): every reply already goes out as a new frame
      with a fresh `W_TXSeqNo` value and no synthesized retry bit, and MKDS still
      latches, so the host's game is not deduplicating anything.

      The probe kinds that would have separated "the host ignores the answer"
      from "the host never sees a first answer" (a dropped reply, an answer
      delivered a round late) were removed with the matrix tools that drove them,
      because their question is closed; what survives is the one-input body probe
      over the control channel (`tools/appctl.py vary`, see BUILD.md).
- [ ] **The "Link test" button reports `FAILED` on a healthy app - and its
      injection arm cannot pass at all while a session is running** (history.md
      v0.1.9q, corrected by v0.1.9s). v0.1.9q read the failure as honest-but-
      badly-worded ("the games are not in multiplayer yet") and left the wording
      as the fix. Re-measured 2026-09-28: it is **not** the games' state. With
      two consoles autostarted on the **diag ROM** - a game that never touches
      the wireless registers - `Link test` still prints `link selftest FAILED
      (1 -> 2 no reply, 2 -> 1 no reply)` three times out of three, and the same
      on NSMB. It always lands in the `inject` arm (`on_bus == 0`), which powers
      both radios on, waits for both slots on the bus, sends one tagged packet
      each way and drains the peer's FIFO for 400ms: both directions report
      `sent: true, received: false`. Two facts from the app log point at the
      mechanism: the peer's own health line in that window reads `rx 2(+2)`, so
      the packets *did* cross the bus, and `md_link_recv_packet` competes for
      that FIFO with the console's **emulated radio, which the test itself just
      powered on** - the app's driver threads keep running ~24 frames during the
      400ms drain, and every frame's receive path can consume the injected
      frame before the drain op (queued to the same thread, serviced between
      frames) gets to look. The C++ smoke test's equivalent passes precisely
      because it steps frames by hand and never powers the radios on. So the
      test is unworkable by construction on a live session, and the fix is a
      design choice, not a word: either verify at a level the radio cannot eat
      (the always-on verification's own `tx`/`rx` counters, which already show
      the frames crossing), or keep injection for sessions that are genuinely at
      rest (no game). Until then the button is the one control that never says
      `PASSED`.
- [x] **A real game's *own* discovery step is where the round dies, and the two
      consoles disagree about it** (history.md v0.1.9q, resolved v0.1.9r). The
      park was *not* the games' discovery step and *not* the broadcast
      direction: it was four bytes of framing the transport added to every
      frame. The audit first, because it is the negative result that matters:
      the two consoles' beacons, association frames and announcements all
      crossed and were handed to the peer's *game* in the parked run exactly as
      in the run that plays - the host sent 763 beacons and the guest's game was
      handed 763 of them (plus 97 of its own console's `melonAP` beacons, which
      the old relay audit had counted together with them), and the guest's two
      broadcasts (`fc=00B0` association request, `fc=0000`) were both handed to
      the host's game. Nothing is broadcast-dependent: `MP_RecvPacket` and
      `MP_RecvHostPacket` read the same queue, and `TXSendFrame`'s beacon/LOC
      cases call the same `MP_SendPacket` a unicast frame does, so a broadcast
      has no separate delivery path to fail on. What the two runs differed by
      was the FCS handling (v0.1.9f appended the CRC and raised the frame's
      length by 4, leaving every *received* length 4 bytes long) - see the
      resolved `+08h` item below for the measurement and the fix. What is left
      open here is only reach: the same A/B has not been run on MKDS's own route
      (its park predates the appended FCS, so nothing recorded says whether it
      is helped or untouched), and the NSMB route's flake rate is still one run
      per arm rather than a table.
- [ ] **The `02`-stall shape has never been produced with the slot ledger on,
      and the handoff-miss *rate* is the one measurement that separates the two
      shapes** (history.md v0.1.9p). The relay audit is closed on the frames the
      games write - 0 lost in either direction while both were in multiplayer,
      in two live stable runs (11439 and 16906 commands) and in the archived
      stall shape - so what is left in the link's own numbers is the reply
      handoff (`empty` windows: 14/11438, 8/16905, 61/7693), and its shape is
      the one variable that still differs: in both live runs every miss falls in
      the first tenth of the park (`empty` then frozen for 6000+ and 10000+
      rounds), in the archived stall run the 61 are spread with a ramp toward
      the guest's departure ([3,3,5,0,1,5,4,7,33,0] by tenth). Two things to do,
      both cheap now that the audit is a single command: (a) re-run the route
      with `MD_LINK_VERBOSE=1` and keep re-running until a live run actually
      stalls at the host's `02` step (the last pass's two runs both landed on
      `FF 00`), then read the same per-tenth histogram - if the ramp reproduces,
      the host's game is being told "client failed" more and more often as it
      sits there, which is the first thing in this whole investigation that
      *changes* during the stall; (b) run the audit's section 1 on that live run,
      because every archived stall log predates the `tx frame` ledger and so has
      no slot-line half to pair (the tool prints that it does not rather than
      counting it as loss).
- [ ] **Is the walk past the host's own `02` step what the guest's game is
      waiting on - and does an *unprobed* run that stalls at `02` lose the guest
      anyway?** (history.md v0.1.9o). The state halfword is now excluded on both
      sides (24 host-side cells in v0.1.9n, 13 guest-side cells and 2662 injected
      deliveries in v0.1.9o, with an unprobed control parked and stable for 230+
      seconds). What the runs separate on instead is the **host's own walk**:
      `06 00 -> 00 FF -> 00 FE -> 00 00/00 01 -> 01 00/01 01 -> 02 00`, then
      either `05 00 -> 03 00 -> FF 00` and a stable park, or a stop at `02 00` -
      and every run that stopped at `02` lost the guest to its own
      `reason 0003` deauth seconds later. Two things are needed, in this order,
      and the second is a *control* for the first: (a) reach a `02 00` park with
      **no cell armed** and watch it for 60 s - if the guest leaves anyway, the
      departure is the host's stall and not our delivery, and the whole
      departure question moves to "why does the host's game not take its own next
      step"; (b) only then, arm the guest-side matrix on a `02 00` park and see
      whether any cell lets the host advance (the guest-side matrix has never
      been run on that shape: run C aborted 22 s in). Note the probe's
      measurement shape for (b): a named cell pins the peer for its dwell, and
      `echo` (masked write of the peer's own bytes, kind 4) is the control that
      does not - use both, and read `applied` back to prove the cell fired. The
      echo control has *never fired live*: run D armed it against a build whose
      hook computed its reach from a body length of zero (so it returned without
      writing), which the unchanged `applied` counter gave away. v0.1.9o fixed
      the reach; the fix passed smoke and was not re-armed, so the first thing
      the next run should do is check the echo row's `applied` moves.
- [ ] **The parked host does not wait on the peer's state halfword - so what
      does it wait on?** (history.md v0.1.9n). The adjacent-state matrix is now
      run clean and twice, on two park shapes (host `FF 00` / guest `00 00`, and
      host `02 00` / guest `01 xx -> 01 FD`), with the peer's own flags halfword
      and its `B8 B6` marker left live: twelve cells each - the peer's own value,
      the host's own value, `00 00`, `01 00`, `02 00`, `03 00`, `04 00`, `06 00`,
      `FF 00`, `5A 5A`, `11 01`, `FD 01` - applied 181 and ~1130 times per cell
      respectively, and the host's own state did not change once in any of the
      24 cells. So the reply body's *state* field is excluded as the decision
      input (**and the same field is now excluded on the guest's side too - see
      the `02`-step item above; read that one first**), and the next candidates
      are the ones the matrix deliberately held
      live: (1) the **flags halfword** at body offset 0, which alternates
      `00 80`/`00 00` about once a second on both consoles at once - inject it
      alone (mask `0003`) at both phases and then frozen; (2) the **`B8 B6`
      marker** itself (mask `0030`), on the off-chance it is a version or a
      capability word; (3) the **command's own `clientmask`**, which is the
      emulator's field: the host's lobby nickname frame carries `0006` (AIDs 1
      *and* 2) while its routine command carries `0002`, so a host game that
      believes two clients are present is worth testing by overwriting the mask
      with `0002`/`0006` at the park; (4) the **`clienttime`** halfword at body
      offset 0 of a command, which is the field the guest's own `NextSync`
      arithmetic consumes. Same rules: one field at a time, a null control first,
      arm only once the host's state has held for 2 s, and a new deauth (not a
      pre-existing one) aborts.
- [x] **The log can no longer misattribute a line to the wrong console**
      (history.md v0.1.9m). `Platform::Log` wrote its tag and its body as two
      `fprintf` calls and `trace_frame` as four, while both console threads
      share one `stderr` - 1021 double-prefixed lines and 415 orphaned bodies in
      one 157k-line run, concentrated on the delivery lines a both-sides
      analysis reads. One `fwrite` per line now; **0** in a 581k-line run of the
      same route.
- [x] **Screen-level: the two-player round never becomes a race** (history.md
      v0.1.9l). Driven end to end and read back per console: the lobby is reached
      with both consoles present, the guest's game adopts the host's transition
      (`00 80 5A 5A` -> `00 80 04 00` beside the host's `... 80 06 00` ->
      `... 80 03 00`), and then both screens go to the identical frozen black
      (99-100% black, `powcnt1 820F`), **both ARM9s sit in the BIOS
      (`pc9 FFFF0238`)**, the wifi block is idle with `IE 0000`, both radios are
      off at `+136.6s` and the guest reads `[not in MP]` - there is no live
      parked handshake at the end of the route either, the session is torn down
      before a race exists. New and worth knowing: **the black is not a dead
      console - a button press wakes both games** (`pc9` back to `0200FD10`, the
      pictures return at 0% black and animate their own front-ends), just with
      no link between them. The parked shape this run left behind is host `03` /
      guest `04`, one state apart, the same relation as the earlier `05`/`04`
      and `FF`/`00`.
- [ ] **Every per-console gameplay check is blocked on a race existing.** "Each
      console's own input moves that console's character and the other console
      shows it" cannot be exercised until a race screen is reached - the only
      input path verified this pass was the app's own (console selection through
      the frontend keymap and key events reaching the emulated games, plus the
      wake-up above). The next route run that reaches a race should press a
      direction on one console and read both screens back, not the counters.
- [x] **Two UI readouts that could never tell the truth, fixed** (history.md
      v0.1.9l). The status bar latched any error forever (`pollStatus` wrote the
      running line only while `!statusError`, so one uncaught UI error pinned
      `ui error: ...` to the bar for the rest of the run while the frame pump was
      demonstrably healthy); errors now expire after 6s unless their condition
      keeps re-asserting itself every poll (console-stopped still stays up). And
      the per-console fps readout had **never** shown a number: the first sample
      divided by zero-by-construction (`c.fpsTimer ?? now`) and only the
      reporting branch started the clock, so `dt` stayed 0 forever - `.fps` read
      `—` on a 29545-frame session; the bar now reads `55 fps` per console.
      Both verified on the rebuilt app.
- [x] **The one-input-at-a-time probe existed but could never fire, and the
      park it targets is a ~1-minute window, not a state** (history.md v0.1.9k).
      A body probe armed with `value 0` ("until cleared") shares its field with
      the hooks' `MPProbeLeft != 0` gate, so it was accepted, logged, and then
      never applied: on a live park at ~60 replies/s, four body probes read
      `applied 0` while `skip` read 40 and `hold` read 27 in the same session,
      and the log had zero `replaced with` lines. Fixed (`value <= 0` is stored
      as `-1`), along with the body hook crediting a frame that has no body and
      `SetMPProbe` arming the new kind before copying its body. The window itself:
      the host's command counter stopped at 2499 / 4786 / 5257 / 6480 in four
      runs, 3 of 4 before a probe could be issued, and every run ends with the
      **guest** deauthenticating first (`reason 0003`) and the host broadcasting
      one back and disarming its beacon - with `MD_ARM_PERSIST` on *and* off, so
      it is not the arm. Any future probe pass must therefore (a) start the
      driver before the join and trigger on "command counter climbing", not on a
      frozen picture, and (b) treat `applied 0` as "this run measured nothing".
- [x] **The parked host excludes three inputs, and the "no reply" body is the
      one lead** (history.md v0.1.9k). On the single live park that could be
      probed, the host's command body did not change to its peer's own bytes
      (`00 80 04 00`), to a 40-reply gap (`skip`), or to answers delivered a
      round late (`hold`), each over ~10s with `LINK ACTIVE` throughout. What did
      correlate: a body of `00 00 00 00` - the peer reporting *no* reply, bit 15
      clear - was followed by `LINK OFF` within 9s, where the 12s control step
      before it left the link `ACTIVE`. n=1, and two other parks in the same
      session ended on their own, so the next experiment is to repeat the pair
      (null control, then `00 00 00 00`) on at least three fresh parks and record
      the time from probe to `LINK OFF`. If it reproduces, the host's game does
      branch on the reply's bit 15/state halfword, which is a value only the
      peer's game can supply - and the answer to "what is the host waiting for"
      becomes "its peer to leave the parked state, which its peer is waiting for
      it to do first".
- [x] **The transition is reachable, and the guest's game does take it**
      (history.md v0.1.9e). The press was the problem. CUT OFF is now pressed on
      the host's own screen: its patch reads luma 103 while the room holds one
      racer and 129-142 the moment the second racer's join puts the button there,
      and the tap fires ~0.5s after the offer instead of ~10s. With that press
      the host's command state byte walks 06 -> **FF** (`p1 handed seq B8B0 body
      BE 01 02 00 00 80 FF 00 B8 B6 at +74.15s`) and the guest's game *changes
      its answer* with it - the lobby's `00 80 5A 5A B8 B6` becomes `00 80 00 00
      B8 B6` - so the guest's game reads the host's transition and writes a new
      state of its own. The session stays alive for a minute or more afterwards
      (host ACTIVE, guest ACTIVE, host `collected 499 -> 1387 -> 1986`, guest
      `answered 351 -> 1104 -> 1647`, `blank 0` and `stale 0` on both), which is
      not the dead-session end the earlier passes measured.
- [ ] **The host's picture goes black at the transition while its link stays
      live.** After the host reaches the `FF` state both of its LCDs read pure
      black through `see` and its picture stops changing, while its link stays
      ACTIVE, its command counter keeps advancing and the guest keeps answering
      it; A on either console does not move it. The guest is on a modal (light
      panel, one line of text) rather than the game-select list. So the two games
      are not on the same screen, and the emulator is healthy (`frames +300` per
      5s, `pc 12-53/100`, `picture FROZEN` but never `wedged` - the health line
      says `picture still` since v0.1.9ae, and the note is info, not a warning,
      because a still picture with live CPUs is a screen waiting, not a hang).
      Next: decide
      whether that black is the host's own blank/LCD-off state during the
      race-data distribution (game-level, legitimate) or our renderer blanking a
      living console (ours), and read what each game is still waiting for. The
      `FF` command body is now reproducible on demand, which is what made the
      question askable.
- [x] **The black post-VS state is not an unserved interrupt, and not a wedge**
      (history.md v0.1.9h). Read at the interrupt level instead of in bytes: the
      flight recorder now carries `irq_raised[16]`/`irq_acked[16]` (every
      `SetIRQ` per source, every `W_IF` write that cleared that bit), the
      `IRQ_Wifi` line transitions, and a register snapshot taken when the digest
      is exported (`W_IF`/`W_IE`, USCOUNT/USCOMPARE, beacon counts, the four TX
      slots, `TXBusy`/`TXReqRead`, `PowerState`/`PowerTX`, `ComStatus`,
      `BlockBeaconIRQ14`), printed as two extra lines per console per window.
      Over a 630s / 34284-round freeze: every enabled source is raised **and
      acked** round after round on both consoles, `IF & IE == 0000` at every
      sample (nothing pending-and-enabled), the guest's `arms` climbs one per
      poll, and ARM7 `IE7 0104` **does** enable `IRQ_Wifi` - bit 24, i.e. bit 8
      of `IE7`, which is what the corrected probe reads: the old `& 0x80` test
      read bit 23 (`IRQ_SPI`) and reported the wifi interrupt as disabled in
      every run it was used on (v0.1.9at). `IRQ_Wifi` is enabled and `W_IF &
      W_IE == 0000` at every sample, so the wifi's per-source IRQs are all
      consumed inside the games' own polling. `BlockBeaconIRQ14` is never
      latched. And the one PC that looked
      like a spin (`pc9 0x0200FD10` in 31 of 36 samples on P1) is the same
      address at the *title screen* before any link exists, so it is the game's
      main loop, not a stall. The freeze is the games' own handshake: the host's
      command body parks at `BE 01 02 00 00 80 FF 00 B8 B6` and the guest's own
      answer at `00 00 00 00 B8 B6` for the whole 630s.
- [ ] **Which state each game is waiting for the other to reach, and why
      `FF 00` never unlatches.** The link is now cleared as the cause of the
      freeze, so the next read is the games' own protocol: the host's command
      carries its own state and its view of the peer in separate halfwords, and
      the peer halfword has been `FF 00` for 34000 rounds while the guest's own
      answers report `00 80`. **The fabricated stand-in is settled as not the
      cause (v0.1.9i)**: `MD_ARM_PERSIST` defaults on now, so every round is
      answered with the peer's own bytes (`canned` went 22% -> 1 in the run,
      that one being the cold-start round), both consoles stayed `ACTIVE`, and
      the parked state was unchanged to the byte - same `45d15630` on both
      screens, same command body at +85.97s and +100.99s. What is left is the
      games' own arithmetic, and the read recorder now has both halves of it per
      round: what the sender's game *armed* (`guest arms an answer: slot ...
      body[...]`, `[mp] answer sent: ... arm=#N`) and what the receiver's game
      *read back* out of the frame it was handed (the per-round consumption list
      in `FormatMPIoReport`). **That correlation has since been taken
      (v0.1.9j) and has a positive answer and a negative one:** the host reads
      the guest's reply descriptor (`801Eh`), 802.11 header and body
      (`80 00 00 00` + FCS) in full, the guest reads the host's beacon/CMD
      frames the same way, and on both consoles `W_RXBufReadCursor` equals
      `W_RXBufWriteCursor` at every sample - so both games consume everything
      and neither withholds anything. What is left is not consumption at all:
      it is that the host's game sends the *same* command body
      (`BE 01 02 00 00 80 FF 00`) for 34000 rounds while the guest's own answers
      alternate, so the next read is the games' own state machines - what each
      *does* with a peer state it has read - not the link.
- [x] **The RX header's `+4` halfword: the hardware does not write it**
      (history.md v0.1.9j). Settled from the references rather than by invention.
      GBATEK's "Hardware RX Header (12 bytes) (RXHDR)" table says of `04h`:
      *"Unchanged (not updated by hardware, contains old Wifi RAM content)"*.
      Every melonDS branch skips the halfword identically
      (`IncrementRXAddr(headeraddr, 4)`), and the pre-refactor code copied the
      received frame's words 0/2 and 6/8/10 while skipping 4. The one
      implementation that fills it - DeSmuME, with a `timeStamp` field - is
      contradicted by its own constants (MAX/MIN RSSI FFh/F0h where the frames
      games read carry the 80h/40h of GBATEK's decode) and writes 0 in its
      emulated receive path. So the values the v0.1.9i recorder saw are the
      hardware's semantics; the delivery site now carries the table, the
      citation and the instruction, and both games were measured reading `+4`
      and then consuming the whole body anyway - it does not gate consumption.
- [x] **The descriptor's `+08h` length is documented as excluding the FCS, and
      the receiver's `W_RXLenCrop` assumes it includes it.** *Settled 2026-09-28
      (history.md v0.1.9r), not from a trace but from the games.* Both
      descriptions are right because they describe the two ends of the same
      field: GBATEK's **TX** note (W_TXBUF_LOCn[0Ah]) says "TX length includes
      the length of a 4-byte 'FCS' (checksum) for the packet. The hardware
      generates the FCS for you, but you still must include it in the packet
      length", so the *sender's* length counts the FCS and its slot is the last
      four bytes of the frame; the *receiver's* header says the length is
      "excluding FCS checksum", which is what `W_RX_LEN_CROP` ("Used to exclude
      the FCS ... from the length entry") implements, and what
      `Wifi::CheckRX`'s `framelen -= (W_RXLenCrop << 1) & 0x1FE` does with the
      2 halfwords both games program. So the wire must carry the sender's own
      length - and the FCS the hardware computes belongs *in the frame's
      trailing 4 bytes*, which is where the transport now writes it.
      v0.1.9f had appended the CRC after the frame and raised the frame's length
      field by 4 to match, which left the two checks consistent but every
      frame's *received* length 4 bytes longer than the protocol expects. The
      NSMB two-player route is the verdict, 12000 frames and one env var apart:
      `MD_MP_FCS=0` (the game's own bytes in the FCS slot) and the in-place
      write both reach gameplay on both consoles; the appended form parks both
      in the game's own discovery screen (4452-9208 rounds, 0 empty / 0 canned /
      0 clientfail / 0 deauth, `FROZEN PICTURE`). No length arithmetic is left
      to reconcile; what a hardware trace could still add is whether a game
      ever reads the four FCS bytes the receiving console's length excludes
      (none observed - both games read to the length and stop).
- [ ] **The empty reply is documented as having no body, and melonDS sends
      four zero bytes.** GBATEK: "0Fh: REPLY frame (empty) ;FC=0158h" and "0Fh:
      Also ALL empty packets (raw IEEE header, with 0-byte body)", while
      `SendMPDefaultReply` builds a 28-byte frame (12-byte hardware header + a
      24-byte IEEE header + a 4-byte zero body), and that body is what the host's
      game reads in place of the guest's state. Since v0.1.9i that frame is sent
      at most once per session (the cold-start round), so this is fidelity rather
      than behaviour - and it must not be changed without first working out the
      length/crop arithmetic above, because a 24-byte length is exactly the case
      the crop mishandles.
- [ ] **The route's own flake rate is still unmeasured.** The reason to measure
      it has changed (v0.1.9i): `MD_ARM_PERSIST=1` no longer reproduced the
      guest's `reason 0003` deauth (3300+ rounds, `deauth out 0 in 0`), so the
      two runs that ended that way in v0.1.9h predate the FCS fix and
      `MD_REPLY_KEEP=1`. One run per arm cannot tell a regression from the route
      flaking (a run stalled at the title in an earlier pass), so an A/B still
      needs three runs per arm before any of its numbers is a verdict. Worth
      building: a `--repeat N` on `mkds_join.py` that runs the route back to
      back, restores the ROM's `.sav` from a copy before each attempt, and
      prints one line per attempt (rounds reached, canned fraction, deauth y/n,
      final screen hashes) so a two-arm comparison is a table instead of a
      single log read.

- [x] **MKDS: the two games park one state apart after the join**
      (history.md v0.1.8z). Not a field we synthesize: the host's 120-byte
      `fc=0080` broadcast is its own bytes with only the standard timestamp
      stamped, it goes out every ~217ms from radio-on, and the guest tolerates
      all of them but the last. On the wire the host's command bodies go
      06 -> FF -> FE -> 00 -> 01 -> 02 -> **05** (parked from seq 4750) and the
      guest's answers go 5A5A -> 01 -> 03 -> **04** (parked from seq 4670),
      with `01 FD` interleaved on every other answer; the host's state changed
      at t=45.204s, 3.6s before the route's CUT OFF tap and 19s before the
      guest's `0003` deauth, while the link answered 7221/7228 polls with
      0 blanks. **Settled 2026-09-28, and the 3.6s was an anchoring error**: the
      session clock starts at *app launch*, not at radio-up. Run A's digest at
      `05:04:40Z` reports its last command `at +48.23s` (and `05:04:32Z` reports
      `+40.23s`), so t=0 is `05:03:51.8Z` - the harness's own `launched app pid
      88143` at `00:03:53` local - which puts the route's CUT OFF press at
      t=44-45s, the *same second* as the host's 06 -> 05 change (t=45.204s). The
      control run settles it: the same route run without `--cut-off --vs`
      (log `05:26-05:31`, session 53.74s) holds the host's command body at
      `BE 01 02 00 00 80 06 00 B8 B6` for its whole session, so the host's game
      moves when the player closes the lobby and never on its own - the guest
      followed to `04` within ~3s of the press. (c) answered: the two numbers
      are the two games' own lobby/game-phase states, one apart because only
      the host closes the lobby. (a)/(b) are moot for this run (the guest's
      answer is a *reply* to a poll, one poll behind by construction).
- [x] **Is MKDS's join failure our regression or an emulation gap?**
      (history.md v0.1.9b). Answered with the reference the pass was asked to
      run: `MD_STOCK_MP=1` restores upstream/master's reply-path decisions (the
      live `W_AIDLow` for both the "is this poll ours" test and the AID stamped
      on the answer, no held arm, the reply queue's drop-everything re-anchor,
      stock's 32-tick staleness window), and on the same route it **cannot hold
      the join**: 2966 of 6576 polls answered with silence, `clientfail 2989`,
      `deauth out 2` from the host, and both consoles out of MP before the
      CUT OFF press, while ours answers 7221 of 7228 with 0 blanks and gets the
      two games' states to move together. The transport is therefore not the
      regression - upstream's semantics are strictly worse on this title. What
      the A/B cannot attribute is what kills the session at the VS transition,
      because the reference never gets there.
- [x] **The route map is measured and genuine; the first frame the guest's
      game stops following is named** (history.md v0.1.9c). Every route tap was
      re-measured against the emulated screens one tap at a time: the menu rows
      are y 16..31/63..75/109..121/157..169 of the bottom screen, CREATE GROUP's
      button is y 156..187 on the group list, NORMAL's block is y 24..81 on the
      mode select, the group bar P2 taps is slot 1 at y 11..43, and the two
      coordinates the last passes suspected are both real controls - CUT OFF is
      the host's own button (light button x 84..214, y 156..187, **present only
      once a second racer has joined**; at 1/8 there is nothing there at all)
      and the host's top screen says "Touch Cut Off to stop accepting entries
      and begin racing", and VS is the top block of the game-select screen
      (y 6..45, label y 21..33) so (128,22) lands on it. So the taps are not the
      problem. **The first frame where the guest stops following** is the host's
      first post-CUT-OFF transition command, `seq=8FC0 body 02 1E 00 FF 00 00
      01 00 02 00 6F 00 B8 B6`: the host's game wrote it at emulated clock
      t=765038599 (wall 13:07:20-13:07:21, immediately on the press), the
      emulator handed it to the guest's game at guest clock 765038324
      (`cmd from host ... my clock 765038324, lead 275, window 446 us, a new
      command, delivered to the console's game`), and the guest answered 424us
      later with `00 80 01 00 B8 B6` - an answer whose state byte stayed in the
      `00`/`01` alternation it had used in the lobby. The full host-command ->
      guest-answer table for one run (5707 exchanges, 37 distinct host bodies)
      runs 06 -> **FF** -> FE -> 00 00 -> 01 00 -> 01 01 -> 02 00 -> 05 00 ->
      03 00 -> FF 00 (game select) on the host, with the guest's answers going
      5A 5A -> 01 0D/0E -> 01 FD -> 03/04 -> 00 00; the guest *does* move, but a
      state or more behind, never into the host's FF/FE handshake. Next target:
      why the guest's game does not adopt the FF/FE transition command it is
      demonstrably handed and demonstrably answers - i.e. read the guest's own
      receive-window handling of that 38-byte frame, and check whether the host
      expects the guest to echo FF/FE before it will leave the post-lobby wait
      (in one manual run the host sat on its "PLEASE WAIT..." modal for 61s and
      then deauthenticated the client itself, reason 0003, leaving both screens
      black).

      **This pass answered the two halves that were measurable, and both came
      out in our favour (history.md v0.1.9d).** *Which* bytes the host collects:
      every armed answer is a distinct, freshly written arm - NSMB 9196 answers
      with the arm serial strictly increasing and 0 repeats, MKDS 571 likewise -
      and the arm a poll consumes is the one the game wrote in the previous
      round (harness: `armed in round seq 00E0, 4091 us before this poll`), so
      one poll of pipeline is the game's own design and a transition command is
      necessarily first answered with the pre-transition state. *Whether the
      guest's game is offered the frame and reads it*: the delivery dump shows
      the header the game matches on (`rxflags=801C, 0040, rate 0014, len 001E,
      rssi 4080`) and the payload at `payload@24`, and the new RX-zone matcher
      shows the game reading both on every poll - including every poll it then
      declines to answer. The transport is byte-faithful at every size (150x30B
      + 21x68B collected exactly as sent). What remains open is therefore not
      ours and not the route: **the guest's game stops answering while still
      polling**, and the host follows 1.5s later.
- [ ] **The answer mapping is structural and verified; the games still freeze
      on the same black screen after VS** (history.md v0.1.9g). Per round the
      guest sends one answer, the host collects it once and hands it to its game
      once, and the arm serial the guest logged says which arm it was - so
      reordering and stale copies are off the table as explanations, and the two
      reasons the host's game still sees the guest *flip* are both the games' own:
      the guest's arms alternate between `00 80 00 00` and `00 00 00 00` on
      alternate rounds, and on ~15% of rounds it arms nothing, so the round is
      answered by the firmware's stand-in (`fc=0158`, a 4-byte zero body, logged
      as `arm=#0`) which the host's game reads as state `00 00`. Next: what the
      host's game wants before it leaves the black screen - the two candidates
      worth instrumenting are whether it keys on the frame control (`fc=0158`
      versus `fc=0118`) to tell a real answer from the stand-in, and whether it
      needs the guest's `00 80`/`00 00` echo to match a specific value before it
      advances its own `FF`/`01` state.
- [ ] **Both games now freeze on the *same* black screen after VS, one state
      apart** (history.md v0.1.9f). With the missing hardware FCS restored on
      the wire (the receiver was being handed 4 fewer bytes than the sender
      wrote, which cost each game the halfword the other keeps its state in),
      the two consoles leave the lobby together, exchange whole frames, and
      then sit on an identical all-zero picture with the host's command state
      at `01 00` and the guest answering `00 00` indefinitely - link `ACTIVE`,
      host still polling, guest answering 87% (13% with the firmware's
      stand-in), both pictures frozen on the same frame, `powcnt1 820F`. The
      screen question is settled and is not our path: the emulated framebuffer
      is all-zero and the renderer's copy matches it (`probe`). Open: what
      stimulus each game is waiting for, now that neither is being lied to
      about the frame length. First thing to rule out, because it is ours and it
      is visible: the host's game reads the guest's answer *alternating*
      between two arms (`00 80 01 11` / `00 80 01 FD`) on every other round.
      **Answered (v0.1.9g)**: the alternation is the games' own (the guest's
      arms alternate between two bodies, and ~15% of rounds are answered by the
      firmware's stand-in), and `MD_REPLY_KEEP=0` does not remove it - it removes
      the answers (`collected 2659`, `clientfail 239`, the host deauthenticates
      the client). The default is now 1 (the newest answer per peer survives a
      command), which keeps the mapping tight without the loss.
- [ ] **Name what makes the guest's game go quiet, now that its inputs are
      ruled out** (history.md v0.1.9d). In the MKDS run that got furthest, the
      guest's game walked its member-record sweep (`12 8E 01 00..11 20 00`, the
      index in body byte 3) through all 17 slots and then part of a second
      sweep, stopped arming after arm #574 in round seq 5DD0 at t=94763048us,
      and then read every offered poll and wrote the reply slot every round
      *with 0* while the host repeated the same `00 80 06 00 B8 B6` command
      (host byte 1 toggling 80/00 from seq 5CF0 onwards). The host's own client
      mask went `0002 -> 0000` at seq=A1F0 and it deauthenticated (`0003`). Open:
      what in the host's command stream the guest's game is waiting for - the
      two runs seen so far stop at different points in the same sweep (index 06
      and index 0B), so it is not a fixed index but a missing stimulus.
- [ ] **A join can die before the driver can reach CUT OFF** (history.md
      v0.1.9d). Run 1's guest went inert at +87.53s of a session whose join
      completed at +84.03s, and the host deauthenticated it at +89.09s, while
      the driver's CUT OFF tap did not land until ~+94.8s (the walk's
      `wait_stable()` calls plus the post-lobby screenshot and settle). Run 2
      survived 10.8s and the tap still arrived after the host's game had parked.
      So the route needs the lobby closed *immediately* after the target role
      appears (and a re-check that `--settle` and the pre-cut-off screenshot are
      not between the two), or the harness needs to treat "the session died
      during the walk" as a distinct outcome rather than reporting
      `p1=host p2=guest` screens that are no longer linked.
- [ ] **The join driver can tap into the attract demo** (history.md v0.1.9c,
      fixed in `tools/mkds_join.py`). MKDS's title screen is an attract loop: it
      plays a demo race, returns to the menu by itself, and a tap that lands
      while the demo is up is discarded. Two route taps in a row were swallowed
      that way in one manual run, and the run then read as "the game ignored the
      tap" - the most misleading shape this harness has. Every tap now goes
      through `wait_stable()` (the target console's *touch* screen must hold
      still first: the demo animates it, every menu and the lobby hold it still
      even while the top screen animates). Open: the same trap applies to any
      hands-free driver of the other test games.
- [ ] **A joining MKDS client no longer announces itself with a beacon, and
      the emulation is not what stops it** (history.md v0.1.9a). Measured on
      the 2026-09-28 instrumented build, MKDS 2-console, both a tapped and a
      no-tap run: the guest takes the host's beacons and does the whole group
      dance - 63 `beacon accepted (our BSSID)` with USCOUNT forced to the
      host's beacon timestamp (one foreign-BSSID reject, before
      association), 64 `USCOMPARE0` writes with bit0 set (its own slot), 121
      `beacon IRQ14` firings at that slot - and never once writes
      `W_TXSlotBeacon` (0 arms, 0 disarms, 0 starts). The grant path in this
      fork is `SetIRQ14 -> StartTX_Beacon`, gated on `W_USCompareCnt & 1` and
      `W_TXSlotBeacon & 0x8000`; it started 469 beacons on the host in the same
      session and found nothing armed 121 times on the guest. So MKDS's client
      simply does not beacon in the lobby phase, and the group's membership
      there is carried by the pick/reply traffic instead (join request as the
      guest's answer, the host's acceptance as its next commands). Open: does a
      *working* link's client ever arm a beacon? NSMB's guest-vs-host beacon
      behaviour is still unmeasured (needs one verbose harness run).
- [ ] **The join request reaches the host's game and it does not act on it**
      (history.md v0.1.9a). In the no-tap run the guest answered 787 of 787
      polls (`blank 0`, `canned 1`) with its 44-byte join request
      (`12 8E 01 0A 20 00 ..`) and the host collected 787 of 787 with
      `clientfail 0` - every reply the client sent, read back by the host's own
      game - while the host's lobby state never moved for 53.74s and the guest
      then deauthenticated itself (reason 0003). **The per-round comparison
      asked for here was taken 2026-09-29 (v0.1.9ap) and the host's game does
      read what the guest wrote:** over all 4096 distinct replies of a coupled
      run the handed body is byte-identical to the first N bytes of the
      collected body (4096/4096; crop histogram `4`), and the 4 bytes the
      handoff does not show are the FCS slot the sender's length counts and the
      receiver's crop excludes - proved on live frames by
      `crc32(handed frame) == the collected body's last 4` on the one reply both
      sites printed in full (seq `EAA0`: body `00 80 81 C7 F5 97` -> handed
      `00 80`, `crc32 = 97F5C781`). The same 4 bytes account for the whole
      "guest arms a 29-value vocabulary while the host logs thousands of
      distinct bodies" reading: with them excluded the run has 38 distinct guest
      payloads and the same 38 collected, where the printed collected bodies
      alone show 545. Remaining: run once with `MD_CMD_RETRY_NOTIFY=1`.
- [ ] **MKDS: the pre-race wait is survived 6 times in 9, nothing in the link
      says which, and the residual deaths have a location but no cause**
      (history.md v0.1.9ar). Nine runs of the documented route, four at
      `MD_REPLY_KEEP=3` and five at the defaults, each sampled every 12 ms by
      `tools/scripts/link-commit-trace.py sample` (`logs/{d1,d2,d3,d4,k1..k4,r1}_link.tsv`):
      6 survived the watch, 3 lost a console from the bus. The two knob settings
      do not separate the arms (2/4 died against 1/5, Fisher p ≈ 0.6) and
      neither do the reply counters, so the v0.1.9aq readings above (budget-12
      "survives 307+ s",      `MD_REPLY_KEEP=3` "stops the empties dead") are
      **retracted as single samples**. A tenth run then drove the route's own
      `--vs` and `--press-all` steps on top of a survived wait (`logs/vs1_*`): it
      walked at tap + 150 and committed at tap + 203, held `link mask 0x3` for
      12,725 frames past the tap with `empty 1`, and **every remaining step did
      nothing** - the games' own output went black ~1.3 s into the wait on both
      consoles (`45d15630`; `logs/shots/mkds-after-vs.png` is one flat colour
      across all four screens against the bright wait screen in
      `mkds-after-cut-off.png`), and a VS tap plus A on either console moved no
      pixel of it. So the wall in front of a race is now a measurement: after the
      wait the games render nothing and ignore input, with the link perfect.
      The one clean correlate is the host
      game's own state walk, which starts at **tap + 115-147 emulated frames**
      in 7 of 9 runs and is identical every time (`06 00` -> `00 FF` -> `00 FE`
      -> ... -> `05 00`/`03 00` -> `FF 00`; the guest's data phase follows at
      tap + 109-123) - but it is not the gate either, because 2 of the 6
      survivors stall at `02 00` and never reach `FF 00`, which is the value the
      item below promoted to a rule. **Where to look next, because the location
      is now narrow:** in 2 of the 3 deaths the host's command body is the lobby
      value in *every* sample from the tap to the guest's departure (161 and 269
      frames later) - the touch, the patch it was aimed at and the screens after
      it are identical to the runs where it moved - so the open question is why
      a tap the bridge logs as delivered is acted on in 7 runs and not in those
      2. The instruments that could see it are a game-side read (`mem`) of
      MKDS's own input/state fields around the tap, or a driver that verifies
      the lobby state moved and presses again; the reply path is ruled out
      (identical in both) and is not to be re-opened.
- [ ] **MKDS: whether the host's game emits its cut-off state** (history.md
      v0.1.9aq) - **the `FF 00 <=> alive` half of this is retracted by v0.1.9ar,
      which measured 2 survivors that never emitted it; the retraction of the
      `clientfail` reading stands.**
      The v0.1.9ap heading - "what ends the pre-race wait is `clientfail` in the
      host's ack" - is **retracted by its own A/B**, which the mission then asked
      for. Four runs of the same 2-console route (`logs/ab_ctl_*`,
      `logs/ab_keep_*`, `logs/ab_keep2_*`, `logs/race7_*`) split perfectly on one
      game value and not at all on the reply counters: the two runs that died
      (`empty 20 / reanchor-dropped 18 / deauth out 1`, and `empty 6`) never once
      emitted `FF 00` in the host's command body, while the two survivors emitted
      it 5,287 and 19,764 times and the guest adopted the state; and the
      survivors carried **more** clientfails at the same point in the run (14
      within ~4 emulated seconds of CUT OFF) than the run that died with 4. So
      `clientfail` is neither sufficient nor necessary, and the two knobs are not
      the gate either: `MD_REPLY_KEEP=3` at the default budget does stop the
      empties at the transition (`empty 4` then `(+0)`, `reanchor-dropped 1`) and
      that run stays coupled - but a second `MD_REPLY_KEEP=3` run died 8 frames
      after CUT OFF, *faster* than the default-knob control that died at 97. The
      budget-12 survival in `logs/race7_*` (307+ emulated seconds, 94% of DS
      speed) is therefore a single sample, not a result. Next: dump the
      command/answer bodies around CUT OFF with a frame number attached, and find
      what makes the host's game emit `FF 00` - or why the CUT OFF tap the route
      fires the instant the offer patch changes does not always reach it. Not to
      be fixed before that.
- [ ] **The NSMB harness flake that blocks every regression check**
      (history.md v0.1.8z). ~3 of 8 runs this session, at two different
      `kReplyStaleWindow` settings, end with `collected 0`, every collection
      counter zero (including `empty`, i.e. no window ever ran), the guest
      counting its answers anyway, and the run at 1.7-1.8x realtime instead of
      2.4-2.5x because both consoles sit in blocking reply waits. Healthy runs
      in the same session: 9196/9197 collected, `clientfail` 1. Find what
      makes `MPClientMask` zero (or the collection path unreachable) in those
      runs before trusting any A/B run on the harness.
- [ ] **MKDS: what makes both games end the session at the VS transition**
      (history.md v0.1.8y). The reply path is no longer the reason: the guest
      answers 4726 of 4729 polls (blanks 0, canned 3), the host collects 4725
      with 5 empty windows, `clientfail` 5 of 4731 (was 1932 of 5956), 0
      deauths for 59s. Then, 7s after the VS tap, the host's game broadcasts a
      120-byte `fc=0080` beacon/roster frame, the guest's game deauthenticates
      3.4ms later (`reason 0003`), both radios go off and both screens sit on
      MKDS's "Communication error. Press the A Button." Next measurements:
      dump that beacon's 104 delivered payload bytes field by field against
      what the guest's game writes back in its own beacon, and check whether
      the guest's game ever reached the course-select state (its reply bodies
      stay 30-byte lobby answers) - i.e. whether the two games disagree about
      the group's roster or the guest never left the group screen.
- [x] ~~Name what a one-frame `kReplyStaleWindow` actually breaks~~ -
      **stale, corrected 2026-09-28**: the collapse attributed here (answers
      1498/275, `collected 0`, `stale 0`) is the *same* signature as the NSMB
      harness flake recorded above it (`collected 0` with every collection
      counter zero, including `empty`) - it is that flake, hit twice in a row,
      not an effect of the one-frame bound. The bound stays at two frames; the
      flake is the thing to name, and until it is, a single `collected 0` run is
      not evidence about `kReplyStaleWindow` at all.
- [ ] Verify a full MKDS lobby + race on the channel-transparent build
      (history.md v0.1.8c): both freezes the user hit are attributed - one
      auto-cured by the watchdog kick, one caused by RF channel mismatch,
      now impossible by construction. Anything that still freezes will have
      a clean STALL/trace signature to chase
- [ ] MITM traffic manager (stall-triggered, kind-aware): on LINK STALL,
      replay the last host/cmd frame a bounded number of times to re-trigger
      the game's retry; on wireless toggle, reset per-instance FIFO read
      offsets; never echo during steady traffic (mgba-splitscreen: echo past
      discovery makes games reset their handshake phase). Partially done:
      the auto-resync kick already cured a live STALL unattended
- [ ] Reduce perceived input latency ("a smig slow" per human test): measure
      the keydown → frame-applied path, then consider applying buttons
      immediately instead of at frame start (history.md v0.1.7)
- [ ] Reproduce the three-console join bug with real multiplayer ROMs
      (MKDS/NSMB) — the 12/12 mesh selftest clears the host-side bus, so the
      bug lives in the games' own WiFi handshake over it
- [ ] Bundle release artifacts (`cargo tauri build`; deb/appimage configured,
      never built)

### In Progress
- [x] Wire the Rust side of the Tauri app to the bridge (hand-written FFI over
      `melonds_bridge.h`); start/stop/session/link commands live
      (2026-09-23, history.md v0.1.6)
- [x] Frame channel picked and implemented: raw-byte IPC responses
      (`tauri::ipc::Response`) from double-buffered per-console snapshots - the
      WebSocket alternative was not needed (2026-09-23, history.md v0.1.6)
- [x] Framebuffer piping to the webview: per-player canvas pair, repaint on
      sequence-number change (2026-09-23, history.md v0.1.6)
- [x] Target met and exceeded: two consoles render side by side through the
      app, lockstepped, at DS rate (2026-09-23, history.md v0.1.6)
- [x] Frame pacing: run loop paces to the DS's 59.826 Hz after the barrier;
      measured 59.8fps (2026-09-23, history.md v0.1.6)

### In Progress (prior entries)
- [x] Tauri v2 app builds, launches and serves the frontend window
      (2026-09-22, history.md v0.1.5)

### Done
- [x] Fork melonDS repository
- [x] Update project documentation (README, AGENTS.md, etc.)
- [x] Configure git remotes (origin: this repo, upstream: melonDS)
- [x] Review mgba-splitscreen for lessons learned
- [x] Build the melonDS core with the Qt/SDL frontend switched off (`-DBUILD_QT_SDL=OFF`)
- [x] Write the C bridge (`src/frontend/tauri/melonds_bridge.h`): lifecycle, ROM
      loading, per-instance input, per-frame stepping, savestates, screen readback
- [x] Link the bridge against the core as a static library
- [x] Create a diagnostic NDS ROM from scratch (`tools/mkdiagrom`), since no test
      ROM exists and no ARM toolchain is guaranteed
- [x] Headless smoke test (`tools/coretest`): 58 checks covering render,
      animation, determinism, input routing, savestates and the link
- [x] Visual capture + contact sheet (`md_capture`, `frames_to_html.py`) so the
      rendering can be inspected by eye and not only asserted on
- [x] Local multiplayer link: melonDS's in-tree LocalMP, with per-instance slot
      routing, broadcast delivery, and consoles joining/leaving the bus as their
      emulated wireless hardware powers up and down
- [x] Bridge API for the link: connected-slot mask, receive timeout, begin/end
      counters, and a non-blocking packet send/receive used by the tests
- [x] Document the supported build path in BUILD.md- [x] **Two real games linked**: Mario Kart DS on two consoles discovers, joins
      the same group, both appear on character select, and race each other on
      the same course with complementary placings
- [x] **Re-verified on the current bridge with the user's Test Roms
      (2026-09-24, history.md v0.1.8a)**: NSMB 2P link play byte-exact
      (3,259,490 bytes both sides) at 3.69x realtime; MKDS boots and runs in
      the app (flip layout, 59.8fps lockstep, both slots on the bus);
      scripted MKDS lobby joins remain timing-fragile — interactive test is
      the next proof (Todo above)
- [x] **Human link-play milestone + link watchdog (2026-09-24,
      history.md v0.1.8b)**: user drove two MKDS consoles into a group in
      the app (first human join) despite freezes; log shows emulators
      healthy (lockstep 59.8fps) while game WiFi stalls; bridge now counts
      packets per kind with [link-event] timestamps and the health thread
      classifies ACTIVE/STALL/IDLE per window (MD_LINK_VERBOSE=1 for per-
      kind deltas)
- [x] Real-game session runner (`md_game_session`) that can drive a game's menus
      by button and by touch screen, per console, and report stability, link
      traffic and speed
- [x] Split the link's receive timeouts: peer frames poll, the host's reply
      collection waits briefly. Both are needed; either one alone deadlocks or
      strands a handshake (history.md v0.1.3)
- [x] `--grid` / `--mark` / `--only` on the contact sheet, so a game's touch
      targets are measured off a ruled screenshot instead of guessed

### Not yet done, and known to be missing
- [ ] The Rust frontend does not call the bridge yet (the app window builds and
      opens; every command is a placeholder)
- [ ] Nothing asserts that two consoles stay *in sync* through a whole race -
      kart positions, items and rankings. A desync would currently go unnoticed
- [x] **A second joiner broke a three-console MKDS group** ("An error has
      occurred." on both joiners, history.md v0.1.4) — **closed 2026-09-29**: the
      cause was the **route's ordering**, not the link. An MKDS client leaves the
      lobby on its own 13–21 emulated seconds after joining, so the third console
      has to be walked to the group list *before* the first one joins; the old
      order had the two clients never in the room together. With that fixed all
      three couple (`link mask 0x7`, `p1=host`/`p2=guest`/`p3=guest`) on the
      **unmodified** link core. The machine-global `MPHostinst`/`LastHostID`
      diagnosis this item used to carry was built, A/B'd and shown to change
      nothing — see Milestone 2 above
- [ ] **Single-card (Download Play) games cannot link at all — now a roadmap
      feature, not a parked limitation (Milestone 4).** Super Mario 64
      DS's VS mode is host + Download Play clients only, and DS Download Play
      lives in the firmware menu, which FreeBIOS cannot boot (`NeedsDirectBoot`).
      Two routes exist and the preferred one spends accuracy instead of asking
      the user for a BIOS/firmware file: a built-in Download Play client that
      accepts a child image and jumps into it, for which `SetupDirectBoot()` is
      the existing shape. Star Fox Command is the second title in the
      folder with the same shape, and v0.1.9ac measured it: its only local
      option is Download Play -> Recruitment (host), both consoles ran the host
      lobby and the wire carried a complete bidirectional exchange
      (`sent=489/501` frames, `99,532/101,980` bytes each way, `slots 0x003`),
      but the joining console has no in-cartridge path - it is the firmware
      menu's Download Play client, i.e. the same blocker
- [x] **Metroid Prime - Hunters: the guest joins, and the route is on the record**
      (2026-09-29, history.md v0.1.9ad). `logs/rom_mph_join.log`: host
      `cmds tx=7986 acks=7986` with `collected=7986 empty=0`, guest
      `cmds rx=7986 answers=7986 (blank 0, canned 0)` and `replies sent=7986`,
      `slots 0x003`, `deauth out/in=0/0`, host `sent=17219 (1,127,344 bytes)` /
      guest `received=17219 (1,127,344 bytes)` and guest `sent=7988 (399,388
      bytes)` / host `received=7988` - byte-complementary in both directions -
      last command `seq=3410` at `+585.01s` with the guest's answer at the same
      offset, `all consoles kept running`, both consoles on arm9
      `pc=0x02088ba4`. The tap that joins is **JOIN GAME `(123,72)`**, not the
      list row: the two consoles' stored frames step apart at frame 27000->27250,
      which is that tap. The title's `TOUCH TO START` sits behind a cycling
      attract loop, so it takes a bounded repeating tap (`--touchtap` with
      `--stop-input`), not one tap at a fixed frame
- [ ] **Metroid Prime - Hunters: get from the joined lobby into a match.** Both
      consoles list two players and the guest sits on `SELECT HUNTER` for the
      117 emulated seconds after the join - no hunter is chosen and no game is
      started (`game-captures/mph-lobby/sheet.html`), so "both consoles in the
      same *match*" is still unproven. The next taps to find are the guest's
      hunter portrait and the host's start, both on the lobby's bottom screen
- [x] **The app's own two consoles hold a real MKDS link, at the harness's
      evidence bar** (history.md v0.1.9af). `tools/mkds_join.py --restart
      --no-dialogs --cut-off --vs` drives the *app* - `run_app.sh` with two
      autostarted consoles and the ROM, every tap preceded by a screen read
      (`wait_stable` on the touch screen; CUT OFF and VS aimed at the offer's
      own luma change, `CUT OFF on p1's screen after 1.1s (patch luma 103 ->
      149)`). The join lands (guest role at the group-bar tap, the host handing
      `BE 01 02 00 00 80 06 00 B8 B6` and the guest answering `00 00 5A 5A B8
      B6` at `+71.48s`), CUT OFF and VS land, the host's body walks to `... 80 FF
      00 ...`, the guest's answer adopts it, both consoles end on the same
      picture (`45d15630`), and the link holds: `cmds tx 2687 = cmds rx 2687`,
      `acks 2687(+299, clientfail 4)`, `answered 2686 + canned 1 = replies sent
      2687 = collected 2683 + empty 4`, `deauth out/in 0/0`, `slots 0x3`, both
      `LINK ACTIVE`. The bar's byte axis is now readable in the app too: the
      health line prints `tx 5670(+621, 281938B(+29670)) · rx 2685(+299,
      128B(+0))`, and host-to-guest the window deltas match exactly (`+29,670` /
      `+29,760`), the cumulative 15,480-byte difference being the host's traffic
      from before the guest's radio came on (`wireless ON (begin #2)` at frame
      4012). The host's `bytes_received` is the documented lower bound
      (`RecvReplies` cannot size a payload) and is not comparable directly.
      Captures: `game-captures/mkds-app/`. Two app-path defects came out of it
      and are fixed: the driver's picture regex knew only `changing|FROZEN` and
      had silently lost the field since v0.1.9ae renamed it to `still`; and the
      health line had no byte counters, so no app run could be held to the bar
      the harness states in bytes. Where the route stops is the games' park
      (next item), re-measured through the app's own input path this pass
      (`tools/scripts/mkds-park-probe.py`: A, START and a touch on each console
      left both framebuffers byte-identical while `[input] frame 12524 player 2
      touch -> 128,96` shows the input arriving)
- [x] **The app's touch path had two owners, and a keypress lifted the pen**
      (history.md v0.1.9ae). `sendButtons()` - every keydown, every keyup, a
      window blur, every console switch - sent `{buttons, touch: null}` to the
      console while the control channel wrote `set_input` straight from Rust, so
      a keypress lifted a pen the channel had just put down, and a pen push
      carried stale buttons: with a pen held for 2.5s, `key KeyJ` one second in
      released the guest's pen on the key's own frame (`[input] frame 1284
      player 1 touch released`, `logs/rom_mph_app3.log`) and no longer does
      (`logs/rom_mph_app4.log`: pen down frames 1246-1391, one release at 1392,
      the key's `buttons -> A` in between). The page now owns a console's whole
      input state and every writer goes through it (`window.__mdinput`, called
      by the prelude's `D.input` - the same "ask the page" pattern as
      `__mdkey`/`__mdselect` from v0.1.9ab), so stylus and buttons can be held at
      once, which is the DS's normal grip. Per-console buttons still reproduce
      v0.1.9ab's diag-ROM checksums exactly (`FB1BAEEE…`/`A195CA30…` idle,
      `556E89D8…` with A on console 1, `D57D029E…` on console 2, the peer
      unchanged each time), and a switch mid-drag lifts the old console's pen
      instead of leaving it stuck down
- [ ] **Whether the guest sees a pen the app put down is still unmeasured.** The
      bridge can answer it - `md_touch_probe` reads back the pen-down bit and the
      two touchscreen channels, and the smoke test asserts ADC = pixel x 16 - but
      it is not on the control channel, so a guest-visible touch proof through
      the app needs the probe exposed there. `press`/`touch` still write the
      *whole* input state (a driver asking for buttons clears the pen, and the
      other way round); the channel's `input` command can carry both, but no
      `appctl.py` subcommand reaches it
- [ ] **The app's two consoles have not been driven into the MPH match.** MKDS
      is now driven end to end through the app (the item above) and lands on the
      games' own park; MPH is the one still standing short, and it needs the same
      treatment - a screen read before every tap, and a route that starts from
      whatever phase the app's own attract loop is in rather than from a frame
      number. The app's game *is* on the multiplayer screen - console 1's top
      screen is ~95% block-identical to `game-captures/mph-mp1` frame 20800, only
      the list area differing - but its phase differs from the harness run's on
      the same save, so frame-timed taps miss it. Also settled: the app binary
      was from 00:45 while the harness's was from 08:51, so every app-vs-harness
      comparison before 2026-09-29 compared builds nine hours apart (rebuilt), and
      a rebuild only reaches the webview if the Tauri context is re-expanded
      (`eval "typeof window.__mdinput"` is the cheap proof)
- [ ] Instances still run the built-in FreeBIOS. It has not blocked a multi-card
      commercial game, but it is exactly what blocks the single-card ones
- [ ] Frame budget is no longer the open question it was: four linked 3D consoles
      run at ~2.2x realtime on the dev laptop. The remaining 3/4-console work is
      correctness, not speed

---

## Phase 2: Splitscreen Display & Controls

### Done
- [x] **Per-player screen flip (hold-to-peek) — the splitscreen layout.** Each
      player's card shows ONE screen at a time, top by default, rendered big;
      each player holds their own flip key to show their bottom screen
      (inventory / map / menus), RELEASE snaps back to the top screen where
      the action is (2026-09-24, history.md v0.1.8). Flip keys ship from the
      backend as data (P1 RShift, P2 Num3, P3 RCtrl, P4 NumDel), outside the
      button keymaps so they can never shadow a game binding; Tab is now the
      layout-mode switch (flip ↔ side-by-side, persisted). Still open: flip
      keys are not remappable yet (keymap-UI item below) and have no gamepad
      path (gamepad item below).
- [x] **Touch input always works, independent of bottom-screen visibility.**
      In flip mode the whole card is the pen surface: pointer position maps to
      the logical 256x192 bottom surface whether it is shown or hidden, so
      gestures land exactly where melonDS expects them (backend contract
      unchanged: `set_input` touch → `md_touch`/`md_release_touch`) (2026-09-24,
      history.md v0.1.8). A virtual pen cursor (SVG nib overlay + a pen dot
      stamped on the hidden canvas while the pen is down) makes aiming possible
      with the screen hidden; shown screens use the real cursor, melonDS-style.
      Still open: the gamepad right-stick + trigger cursor/pen-down extension
      (gamepad item below).
- [ ] More view modes (alongside flip and side-by-side): speaker (1 large +
      smalls), focus (single screen, full size), overlay/PiP
      (picture-in-picture); the grid/equal layout exists as today's default
- [ ] Per-player keyboard remapping UI (P1 WASD/JKL, P2 arrows+numpad are
      hardcoded defaults in lib.rs; the keymap structure already ships to the
      frontend as data) — includes the per-player flip key above
- [ ] Implement gamepad support (auto-assign controllers to players)
- [ ] Implement global hotkeys (turbo, save/load, pause) - remappable
- [ ] Cycle view mode hotkey (F8/F9)
- [ ] Deep link support via URL parameters (?players=N)

### In Progress
- [ ]

### Done
- [x] Render multiple NDS screens side by side (2-4 instances supported; 2
      players verified live) (2026-09-23, history.md v0.1.6)
- [x] Per-player keyboard layouts (fixed defaults: P1 WASD/JKL/UI/QE +
      Space/LShift, P2 arrows + numpad + PgUp/PgDn; touch via mouse on the
      bottom canvas) (2026-09-23, history.md v0.1.6)
- [x] Link selftest UI ("Link test" button) proving inter-console packet
      delivery inside the running app (2026-09-23, history.md v0.1.6)
- [x] Keyboard input verified by a human session (diag button blocks light)
      (2026-09-23, history.md v0.1.7)
- [x] Full-mesh link selftest at any player count: 2/6/12 directed pairs pass
      at 2/3/4 players, unattended (2026-09-23, history.md v0.1.7)
- [x] Hidden-bottom-screen default layout with Tab toggle + persisted choice;
      hidden screens skip their per-frame pixel copy (2026-09-23,
      history.md v0.1.7) — **superseded**: replaced by the per-player
      hold-to-peek flip (2026-09-24, history.md v0.1.8); Tab is the
      layout-mode switch, and the hidden-screen copy-skip carries over
- [x] Layout mode "side-by-side" (both screens stacked per card, nothing
      hidden) alongside flip; Tab cycles between them, choice persisted with
      the old `showBottom` value migrated (2026-09-24, history.md v0.1.8)

---

## Phase 3: Game Library & UX

### Todo
- [ ] Build game library UI with box art display
- [ ] Implement ROM folder scanning
- [ ] Add persistent recent games list
- [ ] Implement quick save (F5) / quick load (F7) for all players together
- [ ] Auto-load battery save files (.sav) next to ROMs
- [ ] Implement pause menu (Escape) with controller-navigable options
- [ ] Add turbo mode (Q key fast-forward)
- [ ] Save import/export per instance or as a set
- [ ] Per-player button remapping UI

### In Progress
- [ ] 

### Done
- [ ] 

---

## Phase 4: Polish & Testing

### Todo
- [ ] Test with actual multiplayer games:
  - Mario Kart DS (Wi-Fi multiplayer)
  - Pokémon games (trading/battling)
  - Other multiplayer DS titles
- [ ] Performance testing with 2-4 instances
- [ ] Memory/CPU usage optimization
- [ ] Bug fixes from testing
- [ ] User documentation / help screens
- [ ] Beta release preparation

### In Progress
- [ ] **Classify the remaining freeze/crash class** (history.md v0.1.8d). The
      host now has the tools to label a failure instead of guessing at it: a
      native fault writes `/tmp/md_crash_<pid>.txt` with a backtrace (proven end
      to end with deliberate ABRT/SEGV), a frozen emulation shows as
      `heartbeat: frames [..] (+[0,0])`, a dead webview as
      `the webview has been quiet for Ns`, and a clean close as
      `window 'main': close requested`. Nothing spontaneous has been captured
      yet - the next hands-on session is the experiment. Note the caveat found
      here: the ARM JIT owns SIGSEGV for fastmem, so a genuine fault chains to
      Rust's std handler and ends the process without our report - the
      supervisor's exit-status line is the only identifier. Closing that means
      installing the reporter from C before `main` (see v0.1.8e notes); the
      heartbeat now only *audits* handler ownership, it never re-installs
- [x] **MKDS join: the reply window now collects** (history.md v0.1.8g). The
      newest attempt (2026-09-25) reached a full MP exchange: 11 host CMD frames,
      9 pulled off the bus by P2's hardware, 3 answered, then P2 deauthenticated
      2.0s in - with **zero** `DROPPED a reply`, **zero** `bad channel`, zero
      resyncs and zero kicks, and the host's acks carrying `clientfail=0000`
      once the client's replies started landing. Both earlier bugs (the 32us
      staleness window and the 2ms wall-clock reply timeout) are therefore
      confirmed fixed in the evidence.
- [x] **MKDS join: the host's retries were never reaching the guest**
      (history.md v0.1.8h - v0.1.8g's suspect #2, confirmed and fixed). The next
      attempt showed the loop exactly: the host sends 135 byte-identical CMDs
      all carrying `seq=02B0`, the client answers 131, the host collects 79,
      then both games bail (client deauth 195ms in). Only the first two of
      those 131 answers were the game's own reply (`fc=0118`); the other 129
      were `SendMPDefaultReply`'s canned stand-in (`fc=0158`). Cause: melonDS's
      `cmd_dupe` suppresses the RX notification for a duplicate CMD seqno, but a
      guest can only answer a command out of a reply slot it armed *beforehand* -
      so every retry is answered from a spent slot, the host reads something
      that is not its peer's answer, and retries forever. Nothing was lost on
      the wire. Fix: `cmd_retry_notify()` / `MD_CMD_RETRY_NOTIFY` (default on).
      A/B'd on the documented NSMB script with the same save and frame count:
      retries delivered 2505 answers / 1522 replies collected / 754 canned, vs
      stock 1081 / 629 / 1059 - same speed, calmer radio (p1 on/off 3/2 vs
      13/12), more frames drawn. The retry loop is normal for NSMB too, so
      v0.1.8g's "0 dupes in 1078 commands" was a different phase of that
      session, not a baseline. NSMB regression-checked, smoke 66/0 both ways
- [x] **MKDS join: the client now answers every retry, and P2 got in**
      (history.md v0.1.8h - the question answered by the next attempt). The
      client answered 1436 of 1436 commands with its own reply and the canned
      default reply count fell from 129 of 131 to **zero**; P2 joined the room
      and the session reached the end of the joining phase. That closes v0.1.8g's
      suspect #2 for good
- [x] **MKDS join: P2 gets in, then the session dies when P1 leaves the lobby**
      (history.md v0.1.8k). P2 appearing in P1's room is real progress; the
      failure then moved one step later. Cause: the client answered every
      *second* command - its guest toggles `W_AIDLow` (53 changes over the join)
      and the reply path is gated on that register, so the wire shows
      `answering ... AID 1` alternating with `BLANK reply ... AID 0` (1302 blanks
      against 83 answers), the host's acks report `clientfail=0002` in the blank
      rounds, and its game re-sent one byte-identical command 1383 times until
      the client's game gave up. The answer was leaving one round late: stock
      melonDS sends whatever `Reply1` held when the round *started*, not the
      answer the guest arms *during* the round. Upstream had fixed exactly this
      in `8772258f` and reverted it in `193c7ed9`; the early reply is restored
      (`Wifi::SendMPEarlyReply`, `MD_EARLY_REPLY=0` to disable). NSMB A/B, same
      save and script: every answer now leaves inside its own round (2505/2505),
      canned `fc=0158` stand-ins 0 vs 752, same speed and collection, smoke 66/0
      both ways. Live join unproven
- [x] **The host discards the answers that arrive in a clump** (history.md
      v0.1.8n). The acks reported the client as failed in 44% of rounds while the
      client answered every round: the consoles are frame-locked but ~16ms apart
      in US clock, so the client answers a whole frame's worth of rounds as one
      clump, `RecvReplies` returns after one answer per asked console, and the
      next command's re-anchor discarded the rest unread. Fix: `MD_REPLY_KEEP`
      (default 2, `0` restores stock) keeps a bounded backlog and drops only the
      oldest beyond K answers per asked console. NSMB, same save, 12000 frames,
      collected/empty: 0 1524/1456, 1 1904/1072, **2 1987/989**, 8 2016/959,
      16 2023/952; zero stale drops at every setting and 5.4-6.1x realtime
      against 3.0x. Smoke 66/0. **Live MKDS still unproven** - if the host's
      abort really keys on `clientfail`, this is the change that should stop it;
      the residual third of empty windows is an answer that does not exist yet
- [x] **The guest's game is never told its answer left** (history.md v0.1.8o).
      The early reply skipped the reply-slot transfer, and with it the TX status
      word, the RF status, the `TXBusy` bit and the transfer-completion interrupt
      (`W_TXStat=0x0401` + IRQ1) that the hardware gives the *guest* when its
      answer goes out. Fix: `MD_EARLY_REPLY_ACK` (default 1; `2` also consumes
      the arm, `0` = v0.1.8k behaviour). Measured: indistinguishable on the NSMB
      harness (answered 77202/77101/1645, collected 73437/73330) and the guest
      there reports `W_TXStatCnt=0000`, so it ships as faithfulness rather than
      as a proven fix
- [x] **The frozen answer is normal, and so is the repeated command**
      (history.md v0.1.8o). New per-arm and per-delivery logging on a *working*
      NSMB session: the guest's game writes the reply-arm register **once** in
      3000 frames (twice under stock), sets its AID once, and answers all ~1700
      rounds from that one template; the host repeats one byte-identical command
      for the whole session. Neither game ever reads its receive buffer
      (`W_RXBufDataRead` reads: 0), so the games' own handling of MP traffic is
      invisible to the wire. Two negatives recorded: consuming the arm is not
      the handshake (1645/3000 rounds fell back to canned `fc=0158` and the
      guest still never re-armed) and delivering retries is not the lever
      (`MD_CMD_RETRY_NOTIFY=1` delivers all 1737 of them, guest still arms once
      - kept for live runs since it is free and removes a question)
- [x] **The host spent a frame and a half receiving every reply** (history.md
      v0.1.8p). The "3354-byte `fc=0118`" frame from v0.1.8o was the *length*:
      `StartRX` reads the frame length as a 16-bit value at `RXBuffer[8]`, and
      the v0.1.8c channel stamp is a single byte at `RXBuffer[9]` - written
      *after* the length in `MPClientReplyRX`, so the host read
      `(channel << 8) | 26` (3354 on channel 13, 1818 on channel 7). `RXTime`
      then became `length * 4` at the 2Mbps reply rate: **13.4ms to receive a
      208us reply**, with `ComStatus` busy so the next frame's reception could
      not begin. The client was fine (`CheckRX` stamps before the same length
      field). Fix: stamp the channel before the length. Harness, same NSMB script
      and save, 12000 frames: **answered 9202, collected 9196, empty 6, canned 0,
      blank 0, stale 0, discs 0, 3.29x realtime** vs 1630/1587/**801 empty** at
      3000 frames and ~2016/~950 empty at 12000 before - 4.5x the MP rounds in
      the same frames, all answered. Smoke 66/0. Live MKDS still unproven
- [x] **The ~2s host deauth is gone; the transport is now at reference quality**
      (history.md v0.1.8q). Live MKDS on the v0.1.8p build: 982 commands over
      4.29s of host clock, client answered 970, host collected 967 of 970
      (99.7%), 3 empty windows, 0 canned/stale/discards, sequence numbers walking
      forward with no retry loop - the failure the user has been reporting since
      0.1.8e is no longer on the wire
- [x] **The poll says nothing: bodies decoded, games talk, host's game stalls
      in a poll-only state** (history.md v0.1.8r). With `cmd body` logging live:
      the guest joins (AID at seq 0220), the host's game broadcasts P1's
      nickname "melonDS" in UTF-16 (3 cmds, flag bytes `1F -> 9F` after the
      guest joined - so guest answers reach it), then polls the same 6 bytes
      `00 80 06 00 B8 B6` 238 times; the guest answers `00 00` throughout; 245
      collected of 245, then the guest's game leaves (`reason 0003`). Neither
      body ever moves after the name phase. The transport is done - the stall is
      in the host's *game*, which never sends the content its client waits for
- [x] **P1's press reaches the emulated game and the host's game responds**
      (history.md v0.1.8s). `[input]` logging proves A presses land at the core,
      and the host's poll body changed for the first time in twelve runs
      (`00 80 06 00` -> `00 00 06 00`, 468 -> 1260 rounds) right after the last
      press. Input path cleared
- [ ] **MKDS: does the guest's game ever read its received mail?** (history.md
      v0.1.8s/v0.1.8t; now a number in the digest's `window reads`, so the next
      run answers it without counting log lines). The host's poll changed and the
      guest's firmware received the
      new bodies, but all 871 guest answers stayed `00 00`. The earlier
      `W_RXBufDataRead` instrument watched the wrong door - games read received
      packets directly out of the wireless RAM window. New throttled window-read
      instrument fires immediately on working NSMB (guest 261+ reads, host 1).
      Next MKDS run decides: guest window reads stay 0 while the changed poll is
      in the air -> the guest's RX-delivery IRQ path is ours to fix; guest reads
      but answer stays `00 00` -> the guest's game itself refuses the poll
      content, compare a real-DS DS-to-DS capture of the lobby handoff
- [x] **Quiet the touch-release log spam** (history.md v0.1.8s) - `md_release_touch`
      now logs only a real active->inactive transition; the frontend still calls
      it every frame, which is free
- [x] **Logs and evidence must survive a reboot** (history.md v0.1.8t). A reboot
      ate the only copy of the fourteenth run's log (`/tmp` is tmpfs) along with
      `.prev`, four archived runs and `/tmp/md_rt/`. `run_app.sh` now defaults to
      `<repo>/logs/md_app.log`, keeps `.prev`, gzips each outgoing run into
      `logs/archive/` (ten kept) and `logs/` is gitignored. `MD_LOG` still
      overrides. Nothing else about a session is durable - no state file, no
      capture unless `md_capture` runs - so anything wanted from a live attempt
      has to reach the log
- [x] **The multiplayer flight recorder** (history.md v0.1.8t). `Wifi::MPDiag` +
      `LocalMP::MPReplyDiag` counters - commands rx/tx with dup detection, answers
      sent/early/blank/canned, arms, AID changes, wireless-RAM-window reads,
      deauths both ways, acks with clientfail, and the four ways a reply is lost
      (re-anchor-dropped / stale / blank-credit / not-looked-for) - exported by
      `md_link_mp_digest` and printed as two lines per console per health window,
      with deltas and the last command/answer bodies, plus an immediate print
      whenever a console's MP state changes (radio, deauth, role). Smoke 66/0.
      Every grep-based read-back of the previous thirteen runs is now a line in
      the log. **Calibrated on the NSMB reference** (`md_game_session` dumps the
      same digest at the end of a run): guest answers 9208/9208 all early, host
      collects 9208/9208, 0 blank/canned/stale/reanchor-dropped, window reads
      630k/239k - matching the wire counters exactly. Two readings fixed by that
      calibration: `dup` is *not* a retry signal (the working reference repeats
      one seqno 9207/9208 times by design - look for `dup` with a frozen body),
      and the two consoles' bodies differ by the 4-byte FCS (`W_RXLenCrop`), so
      compare the first `len - 4` bytes
- [ ] **MKDS premise, checked: local wireless works on stock melonDS 0.9.5+**
      (history.md v0.1.8t). GBAtemp's 0.9.5 write-up names MKDS as *the example
      of the bug that release fixed* and a 0.9.5 hands-on lists MKDS (and its
      romhacks) as playable, tested with four instances; the 2021 board thread
      that calls local wireless "screwey and unstable" predates that. So this is
      our defect, not an unsupported game. The one architectural difference that
      fits our symptoms: upstream runs one OS process per console, paced by the
      OS, while we run a **frame-level lockstep barrier** that leaves consoles at
      arbitrary sub-frame phase - the 1.5-25ms US-clock lead v0.1.8j measured,
      which `CheckRX`'s deferred-delivery path then clamps away
      (`if (RXTimestamp < USTimestamp) RXTimestamp = USTimestamp`). Next: make
      the consoles share one MP time base (or spread their phases deliberately)
      and A/B it on a live join, since the harness cannot drive MKDS's lobby
- [x] **Empower the agent to drive the app itself** (history.md v0.1.8u). The
      user's ask: *"is there a library you are able to add to be able to test
      drive the tauri app yourself?"* - press buttons, capture screenshots,
      interact. Shipped without a new dependency, because the usual tools fit
      this machine badly (Wayland: no `xdotool`, and `tauri-driver` would need
      `webkit2gtk-driver` from apt plus a `cargo install` and would still only
      reach the web UI, never the emulated consoles). `MD_CONTROL_PORT` opens a
      loopback JSON channel (off by default, every command logged as
      `[control] ...`); `tools/appctl.py` drives it: `see` renders the emulated
      screens **as ASCII text** (inverted ramp for DS menus; `--crop ... --aspect
      1` reads the 8px font), `shot`/`window`/`render` handle PNGs, `press`/
      `input`/`touch` inject game input, `key`/`tap`/`index`/`click` drive the
      real UI paths, `advance`/`waitfor` wait on frames or text, and `script`
      replays a whole list. Verified live on MKDS: `press start` produced the
      app's own `[input] player 1 buttons -> START` and the screens changed.
      **Still to do:** turn the manual MKDS route into one such script (boot ->
      title -> MULTIPLAYER `(128,68)` -> group bar `(127,23)` -> lobby), which is
      what retires the ten-minute manual join. Route fact found while proving the
      loop: the title screen answers a **tap at its centre (`touch 128 96`)**, not
      START - after it the bottom screen shows the four-row play-mode menu, and
      the second row is where MULTIPLAYER sits (`(128,68)` as recorded)
- [x] **MKDS lobby close: the guest's own game leaves, and no radio comes back**
      (history.md v0.1.8u). Fifteenth run, flight recorder live: 664 commands,
      658 answered, 0 stale, 0 empty windows - transport clean through the last
      round - and then at P1's lobby-close press the **guest's game writes AID=0
      itself**, sends deauth `reason 0003` *to* the host, and both radios shut
      down and stay down while both games park on a spinning PC (health thread:
      `looks wedged`, 8/100 PC samples). The host's reply is a *broadcast* deauth
      - it is tearing its access point down because its client left. So the link
      is not implicated, and the v0.1.8t "packet transmission failure during the
      join" hypothesis is dead for this run too. Deciding test, now finally
      cheap: run the join with `appctl.py see` watching **both** consoles and
      `shot` on every state change (the digest already warns within 500ms of
      one), and answer what the guest's game shows in the two seconds before it
      leaves - a normal transition screen (MKDS re-forming the group for the
      race, in which case the host must follow) or a communication-error dialog
      (the guest's game giving up on the host's silence)
      **SOLVED - it was ours, and it was one environment default (history.md
      v0.1.8v).** The guest was not dropped, it was starved: with
      `MD_EARLY_REPLY` on (the default since v0.1.8k) the answer goes out on the
      guest's behalf and the guest's own game never gets the event its lobby
      state machine waits for. `arms` says it in one number - 1 with the early
      reply on, 9792 with it off - and with it off the guest's answer starts
      moving (`00 00` -> `00 80`), the host collects normally, and **`CUT OFF` +
      `VS` complete with both consoles still linked 30s later, 0 deauths**,
      verified on the default build with no env knobs. Two v0.1.8u readings to
      unlearn: the digest's `mp pN [guest]` role is only cleared by a deauth the
      console *receives*, so it still says guest after the guest's own game has
      left (grep the events, not the role), and the tap route was dying in every
      run too - this failure was never about the route, the input type, or the
      wire. Also recorded: the host's 120-byte broadcast the guest dies right
      after is a genuine 802.11 beacon (rates, DS channel, TIM, Nintendo vendor
      IE), not something we build
- [x] **The early reply is gone, and the eviction's timer is now measured**
      (2026-09-27). `Wifi::SendMPEarlyReply` and both `MD_EARLY_REPLY*` knobs are
      deleted, so stock's round-end reply is the only path and every frame on the
      wire is one a console's own software produced. Measured this pass: the
      host's game tears the client down **exactly 30s after its own command
      stream stops** (two stock runs: last command +64.55s -> deauth +94.697s,
      and a traffic stop -> deauth +52.592s), reason 0001 to the client then 0003
      broadcast, the host's screen on a "communication error / Press the A
      Button" dialog and the guest's screen carrying no dialog. That is MKDS's
      own no-response timeout on the game-select screen, not our transport.
      Driving the **guest** (10 presses of A after the lobby close, `appctl.py
      press a --player 2`) removed the teardown entirely: 55s of watch,
      `deauth out 0 in 0`, `link mask 0x3` throughout, the host polling
      continuously (12,974 commands and climbing, +841 per 5s window), both
      radios on, `p2=guest` the whole time. The games were parked (picture
      FROZEN 4,203 frames ago, black top screens), so a race is still unproven -
      the guest needs driving through the game-select, not just A. Also
      falsified this pass: MKDS's host does **not** retransmit (host `cmds tx dup
      0`, guest `cmds rx dup 0` over 12,974 rounds); the guest-side dup counter
      had been comparing a value against itself, so every earlier "retry storm"
      reading was an artifact of that test.
- [ ] **OBSOLETE, kept as the record of the early-reply hypothesis** (v0.1.8v/w,
      the
      real fix behind the whole MKDS saga). Stock replies leave the guest's game
      participating but only get an answer out on **a third** of the host's rounds
      (`answered 2510, blank 4283`, while the game arms on every one: `arms 6776`
      vs `cmds rx 6793`), because the window opens before the arm lands; the host
      then gives up on a client that is answering "too rarely" - which is exactly
      how both current endings begin (a join dying after 13 answers, and the
      post-game-select teardown). The early reply fixed the rate by answering on
      the guest's behalf - and that starved the game (`arms 1`). Target: send the
      armed answer inside its round *and* leave the guest's game in the state
      stock leaves it in - arm consumed, TX status word written, completion IRQ
      raised, reply-slot pointer and `TXBusy` moved. `MD_EARLY_REPLY_ACK` mode 2
      tried this and is provably insufficient (still `arms 1`), so the difference
      is elsewhere in the early path; instrument = the digest's `arms` (1 = starved,
      tracking `cmds rx` = participating). NSMB is the regression guard (it plays
      fine either way); MKDS's lobby is the acceptance test
- [ ] **MKDS game select: pressing A after CUT OFF + VS ends the session**
      (history.md v0.1.8v, the frontier the lobby fix exposed). With the lobby
      now surviving, the **host's** game sends deauth `reason 0001` to the
      client and then `reason 0003` broadcast, both radios go off and both games
      spin. Kicks correlate (1-3 bus resyncs in those runs), which points at the
      guard: `KICK_AFTER_WINDOWS = 2` (two 10s stalled windows) can fire during
      the quiet post-lobby loading phase and re-anchor a FIFO the game is
      mid-transfer on. Tests, in order: (1) make the kick threshold env-tunable
      and raise it for the transition, (2) press A on **both** consoles (MKDS's
      game select is a synchronised choice - the host's A may only mean "leave"
      when the guest has not confirmed, and the one-sided press is ours, not
      the game's), (3) if the lobby link is legitimately torn down when the race
      starts, watch whether a fresh beacon/association follows the teardown and
      fix the *rejoin* instead

      Measured since (history.md v0.1.8w): the phase is **stable when untouched**
      (40s, ~300 answers per window, host collecting every one, 0 deauths), and
      it is *host-side* input that ends it - A and START both do, the guest's A
      alone does not. So before anything else: **read the host's post-VS screen
      message** (`tools/glyphs.py`, bright-on-dark) and find out whether it is
      waiting for the guest, a course pick, or a give-up prompt. Note the same
      ending also happens *before* CUT OFF in about one join in four (host
      deauths a guest that answered only 13 rounds), so it may all be one bug:
      the host's patience with a client that answers ~1/3 of rounds - see the
      faithful-early-reply item above
- [x] **Delivery-log fc guard off by one** (history.md v0.1.8q). 26-byte reply
      handoffs printed `fc=0000` because the guard was `> 26`; now `>= 26`, so
      the next log names the frame control of every handoff. Smoke 66/0
- [ ] **MKDS join: P1's DS deauths P2 at a fixed ~2s, and that is the error**
      (history.md v0.1.8m). Read back from the event rather than the link: the
      join is healthy (client answers every command it receives, 6 blanks, 0
      canned, 0 stale drops, host collects 59%) and then **one frame** ends it -
      the host transmits a deauth to P2's MAC, P2's `CheckRX` clears
      `IsMP`/`IsMPClient`, P2's game switches its radio off 0.6s later, and the
      host re-sends the same byte-identical command into an empty bus for the
      rest of the session (that stall is the error screen the player sees). The
      deauth lands **2083/1966/2136/2088ms** after the first game-level command
      across the four runs that get that far, so it is MKDS's own ~2s window to
      accept a client that is present and answering the whole time - not the
      player's button. Ruled out by measurement: the reply loss alone (the
      *working* NSMB reference loses the same amount and plays on) and the reply
      waits (harness 2ms -> 34ms moved collection 60.8% -> 62.6% and cost
      speed). The diagnostics are now in place to settle it in one run: the
      client's `[mp] answered cmd` line prints its reply body, `RecvReplies`
      prints the same body as collected, `[mp] handing client N's reply to the
      console's game` prints what the host's game is handed, and both ends log a
      deauth with the body's reason code. Baseline from the working NSMB
      session: the client's body there is constant `00 00` (an empty answer is
      what a game *sitting in a lobby* sends), so if MKDS's bodies are also
      constant the client's game never reached the lobby and the ~2s timeout is
      the client's fault, while bodies that move mean the host is rejecting an
      answer it can read - i.e. the collection loss after all
- [ ] **Give the two consoles one shared time base** (history.md v0.1.8j). The
      reply now leaves on time, but the two sides still disagree about what time
      it is: the beacon stamps `USCounter` while the frame scheduling,
      `NextSync`/`RXTimestamp` and the reply staleness check all run on
      `Wifi::USTimestamp`, and the measured `lead` between the two was ~15ms in
      one run (twice the entire 446us reply window), 3-25ms across a whole live
      MKDS join, and ~128ms on the NSMB harness. Two fixes were tried (v0.1.8l)
      and both regressed the harness, so this is not a matter of moving one
      clock: pulling the client's clock back to the host's timestamp re-arms the
      deadline test (2503 answers -> 25539, 24399 stale drops, 2.07x realtime),
      and keeping the client's deadline on the host's clock without the clamp
      costs the pacing the reply windows depend on (0.77x realtime, 95904 empty
      windows). The
      measurement to judge it by exists: the NSMB harness collects ~1500 of
      ~2500 replies in a session that *works*, so a real fix should move that
      number too
- [x] **MKDS join: the host's frame rate collapsed to 9fps on the way out of
      the joining phase** (history.md v0.1.8i). P2 joined, then P1 dropped to
      single-digit fps and the game errored out before character select. The
      health lines show both consoles at identical frame counts (+44/+45/+141
      per 5s window), so the session was the bottleneck; in the same 15s the
      host's reply windows came back empty 363 times, and at the 34ms ceiling
      that is 12.3s of the 15s spent blocked inside the emulator - the only
      blocking call in a frame (`recv_timeout=0`). Reply waits are now charged
      to the frame they happen in (`MPInterface::SetReplyBudget`,
      `md_link_set_reply_budget`, published per frame by the run loop;
      `MD_REPLY_BUDGET_FRAMES` to sweep - under its old name
      `MD_REPLY_BUDGET_MS`, see history.md v0.1.9ah), so a frame may spend its
      idle slack and no
      more. NSMB A/B from one save, 12000 frames: 3.18x vs 3.14x realtime, reply
      collection unchanged, the new cap line fired zero times; smoke 66/0 both
      ways
- [x] **MKDS join: the frame rate holds through a live join** (history.md
      v0.1.8j - confirmed). Health lines through the join: `+292`, one `+180`
      (36fps), everything else `+299/+300`, against 9fps before the budget - and
      the budget fired 1542 times, which is the cap earning its place rather
      than a theory. It was also never the collection problem: the log shows ~30ms
      of slack left when it capped, i.e. it trimmed a 34ms ceiling to 30ms
- [ ] **MKDS join: the host collects only half the answers it is sent**
      (history.md v0.1.8j - the current open problem). The client's radio-on
      window is fully accounted for: host sent 1542 CMDs + 1541 ACKs, the
      client's bridge received 1504 + 1506 (98%), its core processed 1503 and
      answered **every one** with its own reply (canned replies: zero), and the
      host collected **734**. So the host's game reports `clientfail` and
      re-sends the same command (seq=02C0, 5741 times) until the client's game
      gives up and leaves (+59.3s). Cause: the DS reply window is 446us of shared
      time while the two consoles' clocks are ~15ms apart (`lead` p50 -14856us),
      so answers land after the round that asked for them has closed and the next
      command discards what the host never read. Direction: give the consoles a
      shared time base - the beacon stamps `USCounter` and `FinishRX` copies it,
      but frame timestamps, `NextSync`/`RXTimestamp` and the staleness check all
      run on `Wifi::USTimestamp`, which the beacon never touches. Measurement to
      judge any fix by: the NSMB harness's collected-reply count (1525 of 2502 in
      a session that works, so a real fix should move it). A negative result is
      already recorded: removing the re-anchor (keeping unread replies for the
      next round) collapses that harness to 198 collected / 16626 commands - do
      not retry that
- [ ] **The stalled-kick threshold needs a longer look** (history.md v0.1.8e):
      the watchdog fired 1.45s into a join (`clientfail=0002` in the host's next
      six acks). It now needs two consecutive stalled windows (10s) and logs the
      ones it withholds, but the underlying question stays open - a resync kick
      throws away that console's queued packets by design, so it should never be
      able to land inside a handshake
- [ ] **Keep the diagnostics out of the game's way** (history.md v0.1.8e,
      2026-09-25): the Link test button used to force the loaded game's radio on
      and off and leave `MST0` frames in the bus - one was read by MKDS's WiFi
      in the middle of a lobby, and a driver interrupted mid-bring-up is a
      plausible cause of the user's "otherwise p1 hung". Now: injection only
      when no console has wireless on (otherwise a resync kick), autostart
      selftest opt-in, and every test packet flushed before the test ends.
      Remaining question for the user - was "p1 hung" the app's picture or the
      game itself - decides whether a frontend stall is also in play
- [ ] **MKDS room discovery, watched with the frame trace** (history.md
      v0.1.8e). The user's "froze up trying to get p2 to find and join" was not a
      freeze at all: both consoles held 59.8fps while the games exchanged three
      frames in seven minutes and the host's radio went off 70ms after coming
      up. Each console now has its own MAC (they all booted the same firmware
      before, so a joiner could not tell the host's room from its own console)
      and `MD_LINK_VERBOSE=1` narrates every bus frame. Next attempt decides:
      a `[link-trace]` `tx reply` from the joiner means the bridge owes the fix,
      no reply means the game's own identity logic needs the next look
- [ ] **Re-test the MKDS kart-selection freeze on the channel-transparent
      build** (the last freeze before this round). The channel fix should remove
      that trigger; if the freeze survives, the watchdog now names the layer
      (STALL vs LINK OFF vs no polls) with no channel noise hiding it
- [ ] **Re-run the three-console MKDS join** to pin the `MPHostinst` routing
      bug (Phase 1 todo) - the mesh selftest proves the bus carries three
      consoles, so this is the games' own handshake

### Done
- [x] **Always-on link verification** (history.md v0.1.8d): a 10s monitor
      classifies every window - real traffic crossing the bus = verified, a
      console polling hard while accepting nothing = stalled (resynced through
      its own driver thread). Counters surface in `link_status` and the status
      bar (`link check ok 12 · resyncs 1 (last 3s ago)`), so the link test runs
      continuously instead of when a button is clicked. Packet injection is now
      opt-in (`MD_LINK_PROBE=1`) because a probe is a foreign frame in the
      game's own receive stream
- [x] **Crash forensics + liveness instrumentation** (2026-09-24, history.md
      v0.1.8d): SIGSEGV/BUS/ILL/FPE/ABRT/TRAP handlers with backtraces into
      `/tmp/md_crash_<pid>.txt` (verified by deliberate signal), Rust panic hook,
      `-rdynamic` for symbol names, 30s heartbeat (frames/RSS/threads/kicks/UI
      freshness), frontend heartbeat + uncaught-error forwarding, window
      lifecycle logging, logged session teardown steps
- [x] **Per-console console identity** (2026-09-25, history.md v0.1.8e): every
      instance loaded the same firmware, so all consoles presented the same MAC
      (`MAC: 00:09:BF:11:22:33` twice in one boot log) - and DS games identify
      peers by MAC, so MKDS's joiner could not pick the host's room out from its
      own console. `md_load_rom` now applies melonDS-Qt's per-instance offset
      (`mac[3] += id; mac[4] += id*0x44; mac[5] += id*0x10`) at ROM load, before
      the guest reads the firmware; verified in the smoke test and in a real
      two-console NSMB session (each console's frames now carry its own address)
- [x] **Per-frame bus narration** (2026-09-25, history.md v0.1.8e): with
      `MD_LINK_VERBOSE=1` every frame is logged from both ends with its frame
      control word, channel, three address fields and MP sync/client mask, which
      separates "the joiner never replied" from "the host never collected the
      reply" - the two failure modes the counters could not distinguish
- [x] **Two-console linked soak test** (2026-09-24): 16.7 emulated minutes of
      NSMB in the harness, 60000 frames per console at 2.41x realtime,
      `all consoles kept running`, both slots on the bus, traffic both ways, no
      FIFO overflows, no channel-mismatch drops on the channel-transparent build

---

## Phase 5: Online Play (Optional/Future)

*This phase is optional and not the core focus. Only pursue if there's demand.*

### Todo
- [ ] Research WebRTC implementation for remote players
- [ ] Build simple web client for remote viewing
- [ ] Implement frame streaming to remote clients
- [ ] Input routing back from remote players
- [ ] Connection management UI
- [ ] Testing with remote players

### In Progress
- [ ] 

### Done
- [ ] 

---

## Future Enhancements (Phase 6+)

### Additional View Modes
- [ ] Custom layouts (user-defined positions/sizes)
- [ ] Background image support
- [ ] Per-player outline toggles
- [ ] Debug log toggle

### Platform Support
- [ ] Android (tablets, Chromebooks, Android TV) - thin Tauri shell around WASM
- [ ] Web version (optional - same frontend in browser)

### Advanced Features
- [ ] Netplay-style synchronization for online (if Phase 5 pursued)
- [ ] Spectator mode
- [ ] Replay/recording functionality

---

## Notes

### Technical Considerations

**Performance:**
- Running 2-4 NDS instances simultaneously requires significant CPU/GPU resources
- Frame pacing and synchronization between instances is critical
- Consider hardware requirements for different player counts

**Controls:**
- Keyboard zones work well for 2 players (left/right hand)
- Gamepad support essential for 3-4 players
- Per-player remapping is a must-have for accessibility

**Synchronization:**
- Use melonDS's native Wi-Fi emulation for link cable (don't build custom)
- Lockstep synchronization where applicable keeps instances in perfect sync
- Virtual Wi-Fi should handle the in-game networking

**View Modes (from mgba-splitscreen lessons):**
- Grid: Best for 2-4 players with equal screen needs
- Speaker: Good when one player needs larger view (competitive games)
- Focus: Single player view, useful for watching or solo play
- Overlay/PiP: Compact view, good for small screens

### Testing Games
- Mario Kart DS (Wi-Fi multiplayer races)
- Pokémon Diamond/Pearl/Platinum (trading/battling)
- Mario Party DS
- Other multiplayer NDS titles with Wi-Fi support

### Per-ROM coverage of the local `Test Roms/` folder (2026-09-29, v0.1.9ac)

Every ROM in the folder has now been run on two consoles. What each one showed,
with the artifact that records it:

| ROM | Two-console multiplayer | Evidence |
| --- | --- | --- |
| New Super Mario Bros. (USA) | yes, byte-exact link, game-level | `logs/route_recheck.log` (`slots 0x003`, 11,129 cmds ↔ answers ↔ acks, `+234.00s`) |
| 0168 - Mario Kart DS (U)(SCZ) | yes, group join (human-driven), lobby level | `logs/mkds_r9.out`, `logs/route_persist1_run1.log` (`slots 0x3`) |
| Star Fox Command (USA) | **bidirectional transport proven**, join blocked by single-card-only design | `logs/rom_sfc_harness.log`, `game-captures/sfc-dp/sheet.html` |
| Metroid Prime - Hunters (USA) (Rev 1) | yes, guest joins and both consoles sit in the same two-player lobby | `logs/rom_mph_join.log` (`slots 0x003`, 7,986 cmds ↔ answers ↔ acks, `+585.01s`), `game-captures/mph-lobby/sheet.html` |
| Super Mario 64 DS (USA) (Rev 1) | bus level only; VS mode is single-card Download Play | history.md v0.1.4 |
| LEGO Star Wars II - The Original Trilogy (USA) (Rev 1) | **no multiplayer mode exists** | `logs/rom_lego_harness.log`, `game-captures/lego-set2/sheet.html` |

Each of the three was driven by `md_game_session` with `--touch`/`--autotap` and
read off `frames_to_html.py` sheets; the touch coordinates that land, and the
frames v0.1.9ad measured them at, are in BUILD.md's "Routes for the other titles
in `Test Roms/`". All five `.sav` files were verified byte-identical to their
pre-pass md5s after every run (`bdbb2dfe557b30b4fda1a7dc7d28dce0` for NSMB). The
gaps left open - reaching a *match* in Metroid Prime - Hunters rather than its
lobby, whether the app's touch channel reaches the game at all for this title,
and any single-card Download Play title - are the bullets in Phase 1 above.

### Resource Files
- BIOS/firmware must be provided by users (not included in repo)
- Legal ROMs must be provided by users (not included in repo)

---

## References

- [PRD-melonDS-multiplayer.md](./PRD-melonDS-multiplayer.md) - Product Requirements Document
- [README.md](./README.md) - Project overview
- [BUILD.md](./BUILD.md) - Build instructions
- [history.md](./history.md) - Project history and lessons from mgba-splitscreen

---

## Next gap after the clock fix (2026-09-27, v0.1.8x)

The MP clock is emulated time now (`NDS::GetSharedUSClock`, used by
`Wifi::USTimer`), so the two consoles' timestamps agree by construction and the
offset stays inside one frame (measured -16,576us..+864us over 26,898 commands,
against -275,520us and growing before). What is left in MKDS is the games' own
lobby state, and it is what the host's `reason 0001` deauth is really about:

* In an eight-minute session the guest's game answered 13,844 of 30,881 rounds
  (45%) - flat for the whole run, no decay - and the link held `mask 0x3` with
  zero deauths. In a 60-second rerun the same route ended with the guest
  answering 22 of its last 732 rounds and the host deauthing it.
* The guest's own counters say the console is alive: `arms 30,881` == `cmds rx`
  (it sets up a reply slot every round) and `aids 27,689` (AID written in 90% of
  rounds), yet it sends a blank in the rest - and a blank is what
  `FinishRX` sends when `W_AIDLow == 0` or when the *host's* `clientmask` does
  not have the guest's AID bit set.

So the next question is not timing, it is bookkeeping between two games: which
of the two conditions is blanking the reply, and is the host's own client mask
(the 16-bit field at `12+24+2` of its command frame) ever cleared for a joined
client? Instrument the two conditions apart - they are already distinguishable
in the log (`[mp] BLANK reply ... AID %d, host mask %04X, not addressed to us
(mask bit %d)`) - and drive the post-VS screen on *both* consoles while reading
which one is set.

---

## Lessons from mgba-splitscreen (Quick Reference)

1. **Start with local splitscreen** - Don't build online first
2. **Use native link cable** - melonDS Wi-Fi, not custom sync
3. **Per-player controls** - Fully remappable keyboard + gamepad
4. **View modes matter** - Grid, speaker, focus, overlay for different setups
5. **Global save/load** - Sync saves across all players
6. **Game library** - Box art + recent games = good UX
7. **Tauri v2 + WebSocket** - Clean architecture, frontend works in browser too
8. **Keep it simple** - Thin client, emulation on host

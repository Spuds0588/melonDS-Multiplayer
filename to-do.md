# melonDS Multiplayer - Development Roadmap

This document tracks the development progress and planned features for melonDS Multiplayer.

> **Primary Goal:** Local splitscreen multiplayer (2-4 players on one machine, couch co-op)
> **Secondary Goal:** Online play for remote players (future enhancement)

## Project Status: In Development

---

## The road to a working 2, 3 and 4-player link

*2026-09-29, v0.1.9aj. Per-game results are in [compatibility.md](./compatibility.md).
This section is the order to fix things in and the evidence that would close each
item. The bar is an **in-game match with the link healthy** — a live link that
never reaches a match is not done, and neither is a game whose own screen ignores
both consoles' input.*

Where it stands: **NSMB works at 2 players** (race reached, one console's input
measured in the peer's picture), **MKDS joins at 2 players and both games stop on
their own park screen**, **Metroid Prime - Hunters joins at 2 players and stops in
its own lobby**, and **nothing has been made to work at 3 or 4 players** — the
second join into an already-linked group is what breaks (history.md v0.1.4).

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

### Instruments to build first (all three milestones need them)

- [ ] **A memory-read command on the control channel** (the named open item,
      every pass since v0.1.9y). Read N bytes of a console's main RAM at an
      address, through `ctrl_eval` + the page prelude's `__mdctl`, e.g.
      `mem <console> <hex addr> <len>`. This is what turns "the peer's world
      changed" from a pixel measurement into a game value — the star gauge in
      NSMB, the racer count / menu state in MKDS, the player list in MPH. It is
      also the instrument that makes Milestone 1's routes findable instead of
      guessed (the MKDS park matrix is the proof: 24 cells of injected peer
      state moved the parked host zero times)
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
- [ ] **A 3/4-console digest diff: one line per console at the same frame**, so a
      group that splits halfway is visible without reading four logs at once

### Milestone 1 — 2 players, in-game, on both of the folder's multi-card titles

NSMB is the working half. These close the other two and the drop:

- [ ] **MKDS: get past the games' own park into a race.** Both consoles halt on
      the same black screen with the link perfect; A, START and a touch on each
      console's bottom screen leave both framebuffers byte-identical while the
      input demonstrably arrives, and 24 cells of injected peer state across two
      park shapes moved the host's state zero times. The next moves: (a) walk the
      *single-console* flow through that same screen and record what it does
      there — if a solo race starts from it, the difference is in what the MP
      flow wants from the peer; (b) re-run the post-VS screen on a live app
      session while reading both games' RAM for a menu/state value (needs the
      memory read above); (c) settle the blank-reply bookkeeping
      (`W_AIDLow == 0` vs the host's `clientmask` AID bit) — see "Next gap after
      the clock fix" below, which names both conditions and the log line that
      separates them
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

- [ ] **Make `LocalMP`'s host and reply routing per-instance.** Today `MPStatus.MPHostinst`
      and `LastHostID` are one machine-global record while every other piece of
      link state is per-instance (`PacketReadOffset[]`, `ReplyReadOffset[]`,
      `SemPool[16 + inst]`), so two consoles that both send commands overwrite
      each other's reply routing and one of them never wakes up. This is the
      leading suspect for the two-joiner break and it is a *correctness* fix, not
      an accuracy trade
- [ ] **Re-run the three-console MKDS join with two joiners** (also Phase 1's
      own In Progress item, below). The question
      is not only whether the second join lands, but whether the *first* joiner
      survives it — in the recorded failure the group was healthy at frame 2500
      and broke when the third console tapped at 3000
- [ ] **AID and reply-slot allocation beyond two clients.** Confirm a third
      console gets its own `clientmask` AID bit and that the guest's blank-reply
      condition is not a two-client assumption; the arm handling is load-bearing
      and must keep behaving for 2 while it learns about 3
- [ ] **Per-console assumptions in the app.** The control channel, the digests,
      the health line and `wifiio` all assume two consoles in places, and P1–P4
      input/screens/layout have never been walked end to end at 3
- [ ] **Regression at 2 after every change here.** NSMB harness + NSMB app +
      MKDS join, re-run on the same build. A routing fix that breaks the working
      two-player session is not a fix

### Milestone 3 — 4 players

- [ ] **The same routing at 4, plus the reply backlog under three askers.**
      `MD_REPLY_KEEP`'s backlog and the per-frame budget were measured with one
      client answering; three answer in the same clump
- [ ] **The least-hardware budget, measured.** Four linked 3D consoles run at
      ~2.2× realtime on the dev laptop (2 ~2.4-4.9×, 3 ~2.9-3.4×). The goal is
      not parity but headroom on a weak machine: profile 4 linked consoles and
      take the cheap wins (no rendering for unfocused consoles, 30 fps off-focus
      with input still full rate, no upscaling/AA above 2 consoles)
- [ ] **Input at 4.** Four keyboard zones, four gamepads, each player's own flip
      key, and no way for one player's input to reach another console — which is
      the `appctl.py --player` item above
- [ ] **Saves at 4.** "Global save/load syncs across all players" is a Phase 3
      goal; today each console writes its own `.sav` and hygiene is a per-file
      md5 check. Decide and implement the shared-save model

### Parked deliberately

- **Download Play / single-card support** — needs a user-supplied BIOS + firmware
  image (FreeBIOS cannot boot the firmware menu). It is a product decision that
  gates two of the folder's six titles, not a bug ([compatibility.md](./compatibility.md#download-play-single-card-is-unsupported))
- **Online / WebRTC** (Phase 5) — still secondary

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
      poll, and ARM7 `IE7 0104` says `IRQ_Wifi` (bit 23) is not even enabled -
      so the wifi's per-source IRQs are all consumed inside the games' own
      polling. `BlockBeaconIRQ14` is never latched. And the one PC that looked
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
      then deauthenticated itself (reason 0003). Next: compare, per round, the
      bytes the host's game is handed (`handing client 1's reply ... body`) with
      the bytes the guest armed, and run once with `MD_CMD_RETRY_NOTIFY=1`
      (MKDS's host repeats identical seqnos, and stock does not deliver a repeat
      to the game).
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
- [ ] **A second joiner breaks a three-console MKDS group** ("An error has
      occurred." on both joiners). One joiner works, and the bus carries three
      consoles fine. Leading suspect: `LocalMP` routes replies through a single
      machine-global `MPHostinst` / `LastHostID` instead of per-instance state,
      so two consoles sending commands overwrite each other's reply routing.
      See history.md v0.1.4
- [ ] **Single-card (Download Play) games cannot link at all.** Super Mario 64
      DS's VS mode is host + Download Play clients only, and DS Download Play
      lives in the firmware menu, which FreeBIOS cannot boot (`NeedsDirectBoot`).
      Supporting it means a user-supplied BIOS + firmware image - a product
      decision, not a bug fix. Star Fox Command is the second title in the
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

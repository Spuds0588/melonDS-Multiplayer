# melonDS Multiplayer - Development Roadmap

This document tracks the development progress and planned features for melonDS Multiplayer.

> **Primary Goal:** Local splitscreen multiplayer (2-4 players on one machine, couch co-op)
> **Secondary Goal:** Online play for remote players (future enhancement)

## Project Status: In Development

---

## Phase 1: Native Bridge (Host Infrastructure)

### Todo
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
      decision, not a bug fix
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
      v0.1.8s). The host's poll changed and the guest's firmware received the
      new bodies, but all 871 guest answers stayed `00 00`. The earlier
      `W_RXBufDataRead` instrument watched the wrong door - games read received
      packets directly out of the wireless RAM window. New throttled window-read
      instrument fires immediately on working NSMB (guest 261+ reads, host 1).
      Next MKDS run decides: guest window reads stay 0 while the changed poll is
      in the air -> the guest's RX-delivery IRQ path is ours to fix; guest reads
      but answer stays `00 00` -> the guest's game itself refuses the poll
      content, compare a real-DS DS-to-DS capture of the lobby handoff
- [ ] **Quiet the touch-release log spam** - the app's frontend calls
      release every frame (102385 lines/run); gate it on an actual transition
      when next touching the input code
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
      `MD_REPLY_BUDGET_MS` to sweep), so a frame may spend its idle slack and no
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

## Lessons from mgba-splitscreen (Quick Reference)

1. **Start with local splitscreen** - Don't build online first
2. **Use native link cable** - melonDS Wi-Fi, not custom sync
3. **Per-player controls** - Fully remappable keyboard + gamepad
4. **View modes matter** - Grid, speaker, focus, overlay for different setups
5. **Global save/load** - Sync saves across all players
6. **Game library** - Box art + recent games = good UX
7. **Tauri v2 + WebSocket** - Clean architecture, frontend works in browser too
8. **Keep it simple** - Thin client, emulation on host

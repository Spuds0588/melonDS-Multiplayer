# Building melonDS Multiplayer

This project is based on melonDS. See the original [BUILD.md](https://github.com/melonDS-emu/melonDS/blob/master/BUILD.md) for detailed build instructions for the emulator core.

The build process below applies to the melonDS core. For the Tauri frontend, see the Tauri documentation.

## Building the multiplayer bridge

The Tauri host does not use the Qt/SDL frontend. It talks to the emulator core
through a small C API (`src/frontend/tauri/melonds_bridge.h`), one emulator
instance per player. That path needs no Qt and no SDL:

```bash
cmake -S . -B build-tauri \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_QT_SDL=OFF \
  -DBUILD_TAURI_BRIDGE=ON \
  -DENABLE_OGLRENDERER=OFF
cmake --build build-tauri -j$(nproc)
```

Three of those flags are not optional:

* `-DENABLE_OGLRENDERER=OFF` — the OpenGL renderer compiles against the glad
  headers that ship inside the Qt/SDL frontend, so it cannot build without it.
  Configure stops with an explicit message if you leave it on. The bridge uses
  the software renderer anyway, because the host reads every instance's
  framebuffers back on the CPU each frame.
* `-DBUILD_QT_SDL=OFF` — leaves out the desktop frontend we do not ship.
* `-DBUILD_TAURI_BRIDGE=ON` — builds the bridge and the headless core tests.

### Tauri host application

The Tauri app (`src-tauri/`) is the desktop shell that will embed the bridge. It
builds separately from the core with cargo; no node/npm toolchain is used and
none is required:

```bash
cargo build --release --manifest-path src-tauri/Cargo.toml
./src-tauri/target/release/melonDS-Multiplayer
```

A first build takes a few minutes (the whole dependency tree compiles);
afterwards it is incremental. Run it with `RUST_LOG=info` to see startup
logging - env_logger defaults to error-only, so a silent log with a working
window is normal.

**Cargo cannot see the core, so it will not relink for it.** The bridge and the
emulator core reach the app as prebuilt CMake archives (`build-tauri/libcore.a`,
`melonds_bridge`, `teakra`) passed to the linker by `build.rs`. Cargo tracks none
of them: after a core-only change `cargo build --release` can report success in
0.4s and leave the *old* emulator inside the app. The symptom is a log line you
expected and never saw. Touch a Rust source file so cargo relinks, then confirm
the new code is really in the binary:

```bash
cmake --build build-tauri -j$(nproc)                       # core + bridge
cargo build --release --manifest-path src-tauri/Cargo.toml # may no-op!
touch src-tauri/src/lib.rs
cargo build --release --manifest-path src-tauri/Cargo.toml # now it relinks
strings src-tauri/target/release/melonDS-Multiplayer | grep -c 'a string you just added'
```

What exists today (0.1.5-alpha.1): the app is a working two-player host. Pick
a ROM, a player count, press Start; each player gets a top+bottom console
rendered from the real emulator core, lockstepped and paced to DS speed
(59.8fps), with keyboard + mouse-touch input and a "Link test" button that
proves packet delivery between consoles. For unattended runs:

```bash
RUST_LOG=info \
MD_AUTOSTART_PLAYERS=2 \
MD_AUTOSTART_ROM=/path/to/rom.nds \
./src-tauri/target/release/melonDS-Multiplayer
```

The link between the app and the core is a **three-part linker arrangement**
in `src-tauri/build.rs` - static archives are passed to the bin link by full
path (`rustc-link-arg-bins`), the link is forced through GNU ld
(`-fuse-ld=bfd`) because rust-lld cannot consume GCC LTO objects, and
`-lstdc++ -lm` are re-emitted after the archives for `--as-needed` DSO
ordering. If the core's build flags change, that file is the first place to
look. Three maintenance notes:

* **Icons.** `src-tauri/icons/` is generated from `res/melon512.png`; tauri's
  build script validates every icon `tauri.conf.json` names, so an empty or
  partial icons folder fails the build before any Rust is compiled. Regenerate
  after changing the source art:
  ```bash
  python3 tools/make_icons.py
  ```

* **The smoke test takes the ROM as an argument.** Run it from the repo root:
  `./build-tauri/md_smoke_test tools/mkdiagrom/diag.nds` - run bare it fails
  with "failed to read ROM file" (cwd, not a regression).

* **Autostart env vars** (`MD_AUTOSTART_PLAYERS`, `MD_AUTOSTART_ROM`) start a
  session at boot and log a link selftest after 3s - how the alpha was
  verified without touching the UI. `MD_SHOW_BOTTOM=1` only sets the
  first-run layout default to side-by-side (per-player hold-to-peek flip is
  the default; the user's Tab layout choice, persisted in localStorage,
  always wins).

#### Running a session you can diagnose afterwards

Use the launcher instead of a hand-written `setsid nohup` line - it rotates the
log, kills a previous instance safely, and tells you the pid and the crash file
for the run:

```bash
MD_AUTOSTART_PLAYERS=2 MD_AUTOSTART_ROM="$PWD/Test Roms/game.nds" tools/run_app.sh
```

It writes:

| file | what it is |
| --- | --- |
| `/tmp/md_app.log` | this run: host log, core logs, `[link-event]` lines, health + heartbeat |
| `/tmp/md_app.log.prev` | the previous run, kept |
| `/tmp/md_crash_<pid>.txt` | a native fault or a Rust panic, with a backtrace |

Reading a log after a bad session, in order of usefulness:

* `heartbeat #N: frames [..] (+.. per 30s) · consoles 2/2 · kicks N · rss NMB ·
  threads N · ui Ns ago` - every 30s. Frames advancing = emulation alive.
  **Frames frozen** in that line = a wedged console (a peer stopped, or a
  deadlock). **`ui Ns ago` climbing** = the window/webview stopped talking while
  emulation continues. **The log stopping mid-line** = the process died: look
  for a crash report.
* `crash handlers (stage): ours for [...]` / `signal 11 is already owned by
  another component (handler at 0x...) - left alone on purpose`. **Do not
  "fix" that by re-installing our handler over SIGSEGV.** The emulator's ARM
  JIT owns SIGSEGV once a session runs (fast-memory faults are how it remaps
  lazily-mapped regions) and saves the previous handler in `OldSaSegv`. An
  earlier revision re-asserted our handler every 30s, took those faults from
  the JIT, and killed the app at heartbeat #1 in three consecutive runs. The
  heartbeat now audits ownership instead of claiming it back. Because the JIT
  chains unhandled faults to Rust's std handler rather than to ours, a genuine
  SIGSEGV ends the process *without* a crash report - which is what the
  supervisor's `exited with status 139 (killed by signal 11)` line is for.
  Our reports do cover SIGABRT/SIGILL/SIGFPE/SIGTRAP, plus any SIGSEGV on a
  build where nothing else owns the signal.
* `LINK ACTIVE / STALL / IDLE / OFF` health lines classify each console every
  5s; `p_n stalled with peers on the bus - resynced its bus state (kick #n)`
  records an automatic cure. `MD_LINK_VERBOSE=1` adds per-kind packet counters.
* `[link-event]` lines are timestamped wireless power on/off with slot masks -
  session restarts and re-joins are visible as events, not counter resets.

#### Link operations run on the console's own thread

Everything that touches the link bus from outside a console - the selftest, the
health watchdog's resync kick, the link monitor's probes - is queued as a
`LinkOp` and executed by that console's driver thread between frames. This is
not tidiness: the emulated WiFi drains its packet FIFO from inside
`md_run_frame`, so a drain from another thread steals the frames the game was
about to read and moves the read offset its receive path is tracking. If you add
a feature that needs to send, receive or resync, **queue it** (`link_send_now`,
`link_drain_now`, `link_resync_now`); do not call `md_link_*` from a new thread.

`MD_LINK_PROBE=1` enables the link monitor's packet-injecting probe (off by
default: it puts a foreign frame into the game's receive stream). The always-on
verification does not need it - real traffic crossing the bus is the proof, and
the counters appear in the status bar as `link check ok N · resyncs M`.

#### Soak testing

The harness is the only way to run a two-console session for a long time
unattended (the app needs a human for the menus):

```bash
./build-tauri/md_game_session "Test Roms/game.nds" --players 2 --frames 60000 \
  --pc-every 4000 --touch ... --touchtap ...
```

60000 frames is ~16.7 emulated minutes per console; a clean run ends with
`all consoles kept running` and a `=== link ===` block of byte counts. Back up
`Test Roms/*.sav` first (`game-captures/sav-backups/` holds copies): a scripted
run mutates the sav and changes the *next* run's boot flow.
* **`MD_LINK_VERBOSE=1` turns the health log into a link watchdog.** Every 5s
  each console is classified `LINK ACTIVE` / `LINK STALL` (wireless on, polls
  running, nothing accepted - a game waiting on a peer that never answers)
  / `LINK IDLE` / `LINK OFF`, with per-kind packet deltas when verbose. A
  STALL that persists 15s while peers are on the bus triggers an automatic
  bus resync (FIFO re-anchor + semaphore clear) - the automated form of the
  old "click Link test to unfreeze" workaround, and it has already cured a
  live stall unattended. The bridge also prints timestamped `[link-event]`
  lines for every wireless power transition, resync, and bus timeout change,
  and tags every core log line with the emitting console
  (`[melonDS p1:Debug] ...`). To triage a freeze: read the last STALL window
  and the surrounding `bad channel` / wireless-transition lines - the MKDS
  freezes were a channel mismatch (now fixed, see history.md v0.1.8c), not a
  host bug.* **The same flag narrates every frame the bus carries**, from both ends:
  `[link-trace] p2 tx cmd len= 46 chan=13 fc=0228 seq=00F0 rx=0309BF000000
tx=0009BF126643 bssid=0009BF126643 t=2893584 sync=5A5A8000000201DE
mask=0002`. `fc`/addresses say what the frame is and who it is for, `seq` is
the wireless sequence control word (a host that repeats it makes every client
  frame a duplicate - see `cmd_dupe`), `t` is the emulated timestamp the frame
  carries (the two consoles' clocks, side by side, is how a clock that no
  longer lines up shows itself) and for MP frames `sync`/`mask` are the first
  payload bytes: a CMD frame carries `clienttime` then the client mask, an ACK
  carries `cmdcount` then `clientfail` - so `mask=0002` on an ACK literally
  reads "client 1 failed to reply". Zero-length frames print as
  `len=  0 ... (empty frame)`: that is a blank reply, which the host reads as a
  packet but not as a reply. The matching `p1 rx` line says whether the peer
  picked it up. When a lobby will not fill, this is the difference between
  "the joiner never replies" (game side) and "it replied and the host never
  collected it" (bridge side) - the counters alone cannot tell those apart.
  It is off by default because it prints every frame: a busy two-console
  session produces thousands of lines.
* **The MP handshake itself is narrated too** (same flag), because the bus
trace stops at the platform boundary. Inside the emulated wireless hardware a
  frame can be dropped by receive filters, a BSSID the client has not adopted,
  or a duplicate sequence number - all of which look identical from outside
  while needing different fixes:
  * `[mp] cmd from host: host clock ... my clock ... (lead ...)` - both clocks
    for every command a client receives. A negative lead means our own clock is
    already past the host's schedule: the client cannot meet it, and it is the
    measurement to make before blaming the reply window.
  * `[mp] answering cmd seq=... AID ... dupe ...` / `[mp] BLANK reply to cmd
    seq=...` - what the client decided to send. Mandatory reading for a failed
    join: a blank is a zero-length frame that the host's reply collection does
    **not** credit (`if (pktheader.Length)`), so "the client answered" and "the
    host saw an answer" are different statements.
  * `[mp] cmd seq=... dropped before any handling: RXFilter2 kills from/to=...`,
    `... BSSID is not ours ... RXFilter bit 11 is clear`, `... retransmit and
    RXFilter bit 0 is clear` - the three drops inside `Wifi::FinishRX`, each
    naming the register that caused it.
  * `[mp] cmd seq=... repeats the previous one: answered by the hardware
    anyway, and handed to the game so it can re-arm its reply slot` - a
    duplicate CMD frame, which is how a game retries a command it did not get a
    usable answer to. This is **retry traffic, not loss**: NSMB's host repeats
    ~99% of its commands in a session that plays fine. `MD_CMD_RETRY_NOTIFY=0`
    makes the same line read `... but the game is not notified of this one`,
    which is stock melonDS and the state that deadlocked MKDS's join.
  * `[mp] canned default reply (fc=0158): the game has no reply slot armed` -
    the wireless hardware answering a command on the game's behalf because the
    guest had nothing armed. On the wire that reply is `fc=0158` and the game's
    own answers are `fc=0118`; a client whose answers are mostly `fc=0158` is a
    client that stopped being asked.
  * `[mp] answered cmd seq=... in the same round: the guest armed its reply
    while the command was arriving ... (AID 1, mask 0002, 30 byte frame,
    fc=0118, body[6]=00 00 5A 5A B8 B6)` - the early reply (see
    `MD_EARLY_REPLY` below) answering within the round that asked, with the
    bytes it is sending. On a joining client this should appear once per CMD,
    with no `[mp] BLANK reply` next to it; a `BLANK` next to it means the answer
    is late again and the host will retry.
  * `reply window: not even looking for p1's replies - the bus has no other
    console attached (asked for mask 0002, connected 0001)` - the host is
    reporting every client as failed without reading its queue once. If this
    appears while a game is hosting, the bus's connected mask and the session
    have come apart; look at `[link-event]` wireless transitions, not at the
    reply queue.
  * `reply window: starting a new command for p1 with 2 unread answers of
    replies - dropped 1, kept 1 (MD_REPLY_KEEP=2)` - the command is re-anchoring
    the host's reply read offset with answers still in the queue. `dropped` is
    what the host gave up on; with the default (`MD_REPLY_KEEP`) that is only the
    overflow beyond two answers per console it is asking, so a high `dropped`
    count on a live join is the peer answering several rounds at once (see
    history.md v0.1.8n). `MD_REPLY_KEEP=0` reproduces the old behaviour, where
    every unread answer is dropped.
  * `reply window: p1 collected a reply from p2 for aid 1, 30 byte frame,
    fc=0118, body[6]=00 00 5A 5A B8 B6` - the same answer one hop later, as the
    reply queue hands it over.
  * `[mp] handing client 1's reply to the console's game: 26 byte frame,
    fc=0118, body[2]=00 00` - the same answer as the host's *game* receives it.
    Three views of one reply, and they are meant to be compared: the client's
    `answered cmd` body, the queue's `collected a reply` body, and this one. If
    the first two agree and this one is shorter, `W_RXLenCrop` did that (the DS
    crops the FCS, so a 6-byte body arrives as 2); if the first two disagree,
    the transport mangled the reply. `no body at all` on this line means the
    game was handed an empty answer.
  * `[mp] deauth sent: <tx> -> <rx>, 42 byte frame, reason 0000` / `[mp] deauth
    received: ...` - a console ending the other's membership (see `fc=00C0` in
    the frame types below). This is the single decisive event in the 2026-09-25
    MKDS attempts, so it prints on both ends with the body's reason code: the
    sender tells you which game gave up, the receiver's line is followed by a
    note that the console has just left multiplayer.

  For scale, a healthy reference session (NSMB, documented script below) prints
  ~2500 answers - nearly all of them, ~2500, retries of the previous seqno - of
  which the host collects ~1500; with the early reply in (the default) all ~2500
  leave inside their own round and the hardware's canned default frame is never
  used, without it ~750 (30%) of them are that canned frame; both consoles play
  a normal two-player round throughout. A partial collection rate is therefore
  expected, and a retry rate near 100% is normal; a client that stops answering,
  or whose answers are mostly `fc=0158`, is not.
* **`MD_CMD_RETRY_NOTIFY` (default on) hands a repeated CMD to the game.**
  A guest answers a command out of a reply slot it armed *before* that command
  arrived, so a retry its ARM9 never hears about can only be answered from a
  spent slot - and once a game's armed slots are gone, `SendMPDefaultReply`
  answers with a canned frame the host cannot use. Stock melonDS's `cmd_dupe`
  suppresses exactly that notification, which is what deadlocked MKDS's join
  (135 identical `seq=02B0` commands, the peer's first two replies real and the
  rest canned, then both games bailing out). Set `=0` only to reproduce stock
  behaviour for comparison. Both settings keep the smoke test at 66/0.
* **`MD_EARLY_REPLY` (default on) sends a client's answer inside the round its
  command arrived in.** A DS answers a command from a reply slot the guest armed
  *before* it arrived, and the guest arms one when it is handed a command - so
  the answer to round N is armed *during* round N and can only leave in round N
  if the hardware sends it the moment it is armed. Stock melonDS waits for the
  round to end and sends whatever `Reply1` held when the round started, which
  puts the answer in round N+1 and leaves round N with a blank (`[mp] BLANK
  reply ... AID 0`; the gate is on `W_AIDLow`, which the guest writes around its
  arm) - measured on MKDS's join 2026-09-25: 1302 blanks against 83 real
  answers, the host re-sending one byte-identical command 1383 times until the
  client's game gave up. Upstream fixed this in `8772258f` and reverted it in
  `193c7ed9`; this restores the early reply only, not the GPU-line-aligned
  transfer timing that came with it, and the round the early reply answers sends
  nothing else (upstream still ran the round-end slot bookkeeping, which was
  measured to make the guest's next arm miss its round: 734 of 2504 rounds fell
  back to the canned frame). `=0` goes back to stock. NSMB two-player
  script, same save, 12000 frames: every answer inside its own round (2505/2505)
  and **zero** canned `fc=0158` stand-ins, against 0 and 752 (30%) with `=0`, at
  the same speed and collection rate; smoke test 66/0 both ways.

  The early reply has a second half, **`MD_EARLY_REPLY_ACK` (default 1)**: sending
  an answer inside its round skips the reply-slot transfer, and with it all four
  things the hardware does to the *guest* when its answer leaves - the TX status
  word (`RAM[replybuf] = 0x0001`, which stock also writes, one round late, at the
  top of `SendMPReply`), the RF status, the `TXBusy` bit, and the
  transfer-completion interrupt (`ProcessTX`, `num == 5`: `W_TXStat = 0x0401` +
  IRQ1 when `W_TXStatCnt` bit 12 is set). `1` restores both, `2` additionally
  consumes the arm as stock does, `0` behaves exactly as v0.1.8k-v0.1.8n did.
  Measured on NSMB: all three settings are indistinguishable (answered
  77202/77101/1645, and `W_TXStatCnt=0000` there, so that guest's game had not
  asked for the interrupt at all), which is why 1 is the default - it cannot be
  worse on the wire and it is what the hardware does. `[mp] early replies
  complete the guest's reply the way the hardware does ... W_TXStatCnt=...`
  reports which of the two a run is in, once.
* **`MD_CMD_RETRY_NOTIFY=1` is what live runs use.** It costs nothing measurable
  (NSMB, 3000 frames: answered 1633 vs 1648, collected 1577 vs 1560) and it is
  the only setting in which the guest's *game* is told the host is still asking,
  which rules a delivery problem out when nothing else in the log explains a
  stall. The `[mp] cmd from host:` line now ends with `a new command, delivered`
  / `the same seqno as the last command, still delivered` / `... NOT delivered`,
  so that count is a `grep -c` away.
* **A byte written at the wrong end of a 16-bit field cost the host a frame and a
  half per reply.** Worth knowing before touching either path: `StartRX` reads
  the frame length as a 16-bit value at `RXBuffer[8]`, so `RXBuffer[9]` is its
  high half - while the channel-transparent air stamps a *single* byte at 9. In
  `MPClientReplyRX` (the host's reply path) that stamp was written after the
  length, making the length `(channel << 8) | len`: 3354 instead of 26 for a
  reply on channel 13, 1818 on channel 7. `RXTime` is `length * 4` at the 2Mbps
  reply rate, so a 208us reply took **13.4ms** of emulated time to receive, with
  `ComStatus` busy throughout and the next frame's reception unable to start.
  `CheckRX` writes its stamp *before* the same field and was never affected,
  which is why only the host's replies were slow. Fixed in v0.1.8p (stamp first,
  length last); if a receive-time mystery ever appears again, check this order
  before anything else and read `[mp] reply handoff check: local framelen=...,
  RXBuffer[8]=...` in a verbose run - `local framelen` and `RXBuffer[8]` must
  agree.
* **Watch what the *guest's game* does, not just the wire.** Two verbose-only
  lines answer questions the packet traces cannot: `[mp] guest arms an answer:
  slot ... body[...]` fires on every write to the reply-arm register (with the
  bytes it holds, so a frozen answer shows up as a body that never changes), and
  `[mp] packet delivered to the console's own software: N so far, ... byte frame,
  fc=..., as an MP client|host|outside multiplayer` lists every packet handed to
  a console's own software - the one place where a frame the console will not
  recognise is visible. Measured on a *working* NSMB session: the guest's game
  arms **once** in 3000 frames and neither console ever reads
  `W_RXBufDataRead`, so an unchanging answer is normal and its body is not
  evidence of a failure by itself.
* **`MD_REPLY_TIMEOUT_MS` tunes how long a host waits for a client's reply**
  (default **34**, i.e. two emulated frames). This is the one link number
  measured in *wall-clock* milliseconds while the DS protocol it imitates is
  measured in *emulated* time, so it moves with the emulation speed: 2ms of wall
  clock is 0.8ms of emulated time at 2.4x - less than the 1-2ms DS slot window
  it stands in for, and less than a frame, which is when the client's answer
  actually goes out. Too small and the host reports `clientfail=0002` to the
  game and the game drops the client; too large and a *missing* reply stretches
  the frame (the wait returns instantly when a reply is posted, so a healthy
  session pays nothing). Watch `reply window closed for p1 with
  collected=0000` in the log: if those are frequent while the client's
  `tx_reply` keeps climbing, the host is hanging up early - raise this.
  This number is the ceiling for a *single* window. How much a frame may spend
  on all of its windows together is `MD_REPLY_BUDGET_MS` below, and that is what
  keeps a busy join from slowing the session down.
* **`MD_REPLY_BUDGET_MS` caps what one frame may spend inside reply waits**
  (default **two emulated frames, ~33ms**; `MD_REPLY_BUDGET_NS` and
  `MD_REPLY_BUDGET_MS=-1` also exist for the harness and for "no cap"). The
  waits above are wall-clock time spent inside an emulated frame, so leaving
  them unbounded is a feedback loop: a slower frame makes the peer's reply
  later, the longer wait makes the frame slower. Measured cost of getting this
  wrong, on a real MKDS join 2026-09-25: 1436 commands asked for replies in 15s,
  363 windows came back empty, and at a 34ms ceiling that is **12.3s of the 15s
  spent blocked** - the session ran at 9fps and every peer waited for it at the
  lockstep barrier. Budgeted, the wait can use the slack the frame was going to
  idle away anyway and no more; a reply the peer cannot produce inside that
  slack is reported to the game as `clientfail` the usual way. The cap announces
  itself in the log when it bites: `reply window: p1 would have waited 34ms for
  a reply, but this frame only has 12000us of slack left` - none in a healthy
  session, and one per empty window when a game is retrying hard.
* **Each console has its own MAC.** `md_load_rom` assigns a per-slot MAC out of
  the firmware image (slot 0 keeps the firmware's, the rest get offsets - the
  same scheme melonDS's Qt frontend uses for its extra instances), because two
  consoles that present the same identity cannot be told apart by a game: MKDS
  builds its group list out of host MACs, so a joiner sharing the host's MAC
  cannot see the group. The bridge logs the address it gave each console
  (`player2: wireless MAC 00:09:BF:12:66:43`). The identity is settled at ROM
  load: a console that is already running keeps the MAC it booted with.
* **The link selftest is a full mesh.** It powers every console's wireless on
  through their own run loops, then sends a tagged packet between every
  directed pair, draining each receiver until *its* packet arrives (the bus
  is broadcast, so stale traffic must be flushed past). Verified 2/6/12
  pairs pass at 2/3/4 players.
* **But it walks the hardware of whatever is loaded, so it stays out of a live
  game.** The forced power-on writes ARM7 `0x04000304` / `0x04800036` under a
  running title, and its `MST0` frames land in the stream that title's own WiFi
  driver is reading - the 2026-09-25 MKDS log has a test frame surfacing inside
  a lobby minutes later, and a game whose driver is mid-bring-up when those
  registers move can simply hang. Consequently: the selftest is **opt-in at
  autostart** (`MD_AUTOSTART_SELFTEST=1`), the **Link test button never injects
  while any console has its wireless on** (it does the resync kick instead, and
  the status bar says which happened), and every injected packet is flushed
  from both consoles before the test ends. To verify the link on a game you
  are actually playing, read the status bar's `link check ok N · resyncs M` and
  the `[link-trace]` lines - real traffic crossing the bus is the stronger
  proof anyway.
* **Commands must not be `pub`.** A `#[tauri::command] pub async fn` in the
  crate root collides with the hidden `__cmd__*` macro the attribute itself
  exports and fails with E0255 on every toolchain. Keep them module-private;
  `generate_handler!` does not need them public. (Details: history.md v0.1.5.)

### Diagnostic ROM

No test ROM ships with the repository and no ARM toolchain is assumed, so
`tools/mkdiagrom` hand-assembles a small NDS program covering both screens,
per-instance input, frame pacing and per-instance identity:

```bash
python3 tools/mkdiagrom/mkdiagrom.py -o tools/mkdiagrom/diag.nds
python3 tools/mkdiagrom/mkdiagrom.py --verify tools/mkdiagrom/diag.nds
```

### Core smoke test

`tools/coretest` boots that ROM through the same bridge the Tauri host uses and
checks the parts of the splitscreen pipeline that can be verified without a
window — both screens render, frames advance, two identically-configured
instances stay pixel-identical, input reaches exactly one instance, savestates
round-trip:

```bash
./build-tauri/md_smoke_test tools/mkdiagrom/diag.nds
```

Set `MD_TRACE=1` for a per-frame trace of the ROM's progress markers, which is
what you want when a check fails. Build and run this after any change to the
bridge or the core integration; it is the fastest signal that the emulator
still does what the host assumes it does.

### Visual capture

Assertions prove that something changed; they are poor at proving it is *right*.
`md_capture` drives the same bridge and writes out the real frames, and
`frames_to_html.py` turns them into a single self-contained contact sheet:

```bash
./build-tauri/md_capture tools/mkdiagrom/diag.nds captures
python3 tools/coretest/frames_to_html.py captures
# then open captures/sheet.html
```

Each card is one emulated console with the top screen above the bottom screen.
The sheet also samples the diagnostic ROM's button blocks and reports which
buttons are lit on which console, so per-instance input routing is visible at a
glance rather than merely asserted, and it tabulates the link bus state.

This is not decoration: the contact sheet caught a bug the smoke test had been
passing straight through, where every console rendered the ROM's fallback colour
and was only distinguishable by its player pips.

Both tools take options that matter when you are reading a real game rather than
the diagnostic ROM:

```bash
# a real game: skip the button-block sensors, and rule the screens to measure them
python3 tools/coretest/frames_to_html.py --plain --grid game-captures/run
# show only these host frames, and mark a candidate touch point
python3 tools/coretest/frames_to_html.py --plain --grid \
  --only 1200,1230,1300 --mark 128,157 game-captures/run
```

`--grid` draws a coordinate ruler on both screens and `--mark X,Y` draws a
crosshair (add `top:` for the top screen). That turns "where is that button"
from guesswork into reading a number off the picture, which is the difference
between one run and five.

### Real game session

`md_game_session` boots a commercial ROM into one or more consoles and reports
what happened: whether each console kept producing frames, whether the game
powered its wireless on, whether the link carried traffic, and how fast it ran.
It can also drive the game's menus, by button **and by touch screen**, so a
session can be walked to an in-game multiplayer lobby and beyond. No ROM is
shipped or required by the build; point it at one you own.

```bash
./build-tauri/md_game_session "path/to/game.nds" --players 2 --frames 14000 \
  --shot-dir game-captures/run --shot-every 200 \
  --all-players --touch 600:128:68 --touch 1000:128:157@1 \
  --touch 1400:128:68@1 --touch 1700:127:23@2 \
  --autotap A:6500:90
python3 tools/coretest/frames_to_html.py --plain game-captures/run
```

Input options (`--tap`, `--hold`, `--autotap`, `--touch`, `--holdtouch`,
`--touchtap`) each take an optional `@P` suffix naming which consoles to drive,
1-based and comma separated, so a host can be driven differently from the
consoles joining it. Touch coordinates are touch-screen pixels: 0..255 across,
0..191 down, origin top-left.

Three settings are load-bearing, and `--serial` or a non-zero `--recv-timeout`
will respectively crawl or hang rather than fail cleanly. Three script-writing
traps cost a day each the first time (history.md v0.1.3, re-learned 2026-09-24):

* **A fresh `.sav` runs the game's first-time wizard before the main menu**, so
  every fixed frame number shifts. Worse, running a session mutates the sav,
  which changes the *next* run's boot flow. Back up the sav before scripted
  runs, and prefer repeating taps (`--touchtap`) tuned to be role-safe on every
  screen the console might be on when they fire.
* **`--touchtap` repeats until the session ends**, not a fixed number of
  taps - a repeating tap past a screen can walk the console somewhere else
  (MKDS's joiner tapping the group bar too long ends up creating its own
  group). One-shot coordinates only survive identical boot timing.
* **MKDS NORMAL vs SIMPLE are one tap apart** (`(128,68)` vs `(67,145)`) and
  the wrong one still reaches *a* lobby - a single-card one the other console
  can never see. `(128,68)` repeats are safe on both the main menu
  (MULTIPLAYER) and mode select (NORMAL) for every console; the joiner should
  then only ever tap the cyan group bar `(127,23)`.

| option | default | why |
| --- | --- | --- |
| threading | one thread per console | a console waiting for a peer's wireless frame can only be answered while the peer runs; in sequence every blocking receive waits out its full timeout |
| `--recv-timeout` | `0` (poll) | melonDS's 25ms blocking receive assumes real-time consoles and deadlocks a lockstep host |
| `--reply-timeout` | `2` | the host's reply collection genuinely races the clients, so polling there strands handshakes |

Expect a little under 3x realtime for two linked consoles of a 3D game on a
fairly ordinary laptop, and around 6x for a single console.

### Linking two real games

`tools/coretest` also documents a worked 2-console run of Mario Kart DS, menu
coordinates included, in [history.md](./history.md). It is a useful starting
point for driving any DS game into local wireless play.

## Building the melonDS Core

* [Linux](#linux)
* [Windows](#windows)
* [macOS](#macos)

## Linux
1. Install dependencies:
   * Ubuntu:
     * All versions: `sudo apt install cmake extra-cmake-modules libcurl4-gnutls-dev libpcap0.8-dev libsdl2-dev libarchive-dev libenet-dev libzstd-dev libfaad-dev`
     * 24.04: `sudo apt install qt6-{base,base-private,multimedia,svg}-dev`
     * 22.04: `sudo apt install qtbase6-dev qtbase6-private-dev qtmultimedia6-dev libqt6svg6-dev`
     * Older versions: `sudo apt install qtbase5-dev qtbase5-private-dev qtmultimedia5-dev libqt5svg5-dev`  
       Also add `-DUSE_QT6=OFF` to the first CMake command below.
   * Fedora: `sudo dnf install gcc-c++ cmake extra-cmake-modules SDL2-devel libarchive-devel enet-devel libzstd-devel faad2-devel qt6-{qtbase,qtbase-private,qtmultimedia,qtsvg}-devel wayland-devel`
   * Arch Linux: `sudo pacman -S base-devel cmake extra-cmake-modules git libpcap sdl2 qt6-{base,multimedia,svg} libarchive enet zstd faad2`
2. Download the melonDS repository and prepare:
   ```bash
   git clone https://github.com/melonDS-emu/melonDS
   cd melonDS
   ```
3. Compile:
   ```bash
   cmake -B build
   cmake --build build -j$(nproc --all)
   ```

## Windows
1. Install [MSYS2](https://www.msys2.org/)
2. Open the MSYS2 terminal from the Start menu:
   * For x64 systems (most common), use **MSYS2 UCRT64**
   * For ARM64 systems, use **MSYS2 CLANGARM64**
3. Update the packages using `pacman -Syu` and reopen the same terminal if it asks you to
4. Install git and clone the repository
   ```bash
   pacman -S git
   git clone https://github.com/melonDS-emu/melonDS
   cd melonDS
   ```
5. Install dependencies:  
   Replace `<prefix>` below with `mingw-w64-ucrt-x86_64` on x64 systems, or `mingw-w64-clang-aarch64` on ARM64 systems.
   ```bash
   pacman -S <prefix>-{toolchain,cmake,SDL2,libarchive,enet,zstd,faad2}
   ```
6. Install Qt and configure the build directory
   * Dynamic builds (with DLLs)
     1. Install Qt: `pacman -S <prefix>-{qt6-base,qt6-svg,qt6-multimedia,qt6-svg,qt6-tools}`
     2. Set up the build directory with `cmake -B build`
   * Static builds (without DLLs, standalone executable)
     1. Install Qt: `pacman -S <prefix>-qt5-static`  
        (Note: As of writing, the `qt6-static` package does not work.)
     2. Set up the build directory with `cmake -B build -DBUILD_STATIC=ON -DUSE_QT6=OFF -DCMAKE_PREFIX_PATH=$MSYSTEM_PREFIX/qt5-static`
7. Compile: `cmake --build build`

If everything went well, melonDS should now be in the `build` folder. For dynamic builds, you may need to run melonDS from the MSYS2 terminal in order for it to find the required DLLs.

## macOS
1. Install the [Homebrew Package Manager](https://brew.sh)
2. Install dependencies: `brew install git pkg-config cmake sdl2 qt@6 libarchive enet zstd faad2`
3. Download the melonDS repository and prepare:
   ```zsh
   git clone https://github.com/melonDS-emu/melonDS
   cd melonDS
   ```
4. Compile:
   ```zsh
   cmake -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6);$(brew --prefix libarchive)"
   cmake --build build -j$(sysctl -n hw.logicalcpu)
   ```
If everything went well, melonDS.app should now be in the `build` directory.

### Self-contained app bundle
If you want an app bundle that can be distributed to other computers without needing to install dependencies through Homebrew, you can additionally run `
../tools/mac-libs.rb .` after the build is completed, or add `-DMACOS_BUNDLE_LIBS=ON` to the first CMake command.

## Nix (macOS/Linux)

melonDS provides a Nix flake with support for both macOS and Linux. The [Nix package manager](https://nixos.org) needs to be installed to use it.

* To run melonDS, just type `nix run github:melonDS-emu/melonDS`.
* To get a shell for development, clone the melonDS repository and type `nix develop` in its directory.

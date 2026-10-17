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

### Building on macOS and Windows

The core build is the same CMake line on every platform, with `--config
Release` for MSVC's multi-config generator:

```bash
cmake -S . -B build-tauri -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_QT_SDL=OFF -DBUILD_TAURI_BRIDGE=ON -DENABLE_OGLRENDERER=OFF
cmake --build build-tauri --config Release --parallel
cargo tauri build            # from src-tauri/, bundles the host
```

The C++ half is portable already (`src/frontend/tauri/CMakeLists.txt` links
`ole32 comctl32 wsock32 ws2_32` on Windows and skips `-lm`/`dl` on Apple), and
`src-tauri/build.rs` carries the per-platform *link* half, which is where the
three platforms actually differ:

| platform | archives | what the final link adds |
| --- | --- | --- |
| Linux | `lib{melonds_bridge,core,teakra}.a` | `-fuse-ld=bfd` (GCC LTO), `-rdynamic`, `-lstdc++ -lm`, `stdc++/m/dl/rt` |
| macOS | the same `.a` files | `-lc++`; no `-fuse-ld`, no `-rdynamic` (GNU-ld flags Apple's linker rejects) |
| Windows | `{melonds_bridge,core,teakra}.lib` under `Release/` | nothing: MSVC's `.lib`s carry their own `/DEFAULTLIB` records |

If a platform fails to link with `undefined symbol: md_*`, `build.rs` found the
wrong archive layout - it probes `Release/`, `RelWithDebInfo/` and the flat
directory in that order, so a new generator is a branch there, not a puzzle in
the linker output.

### Online play (remote players)

A remote player plays *one* console of the host's session, over the internet.
The host stays authoritative and keeps running every console: it streams that
seat its own two screens (composited into one 256x384 JPEG, latest frame wins)
and its own console's audio, and takes back nothing but a button mask and an
optional touch point. Emulation never happens twice, so a guest cannot desync
the game, and a guest that falls behind drops frames instead of slowing anyone.

Transport is PeerJS (WebRTC DataChannels) with the vendored build at
`frontend/vendor/peerjs.min.js`; only the signalling handshake touches the
public broker, payloads go peer to peer. Two doors into a session:

* **Join code.** The host reads out six digits; the guest opens the guest page,
types them, and the host approves the request by hand. The code is derived from
the peer id and carries no secret, so approval is what actually admits a guest.
* **Magic link.** One capability per seat, minted when hosting starts: the token
travels in the URL *fragment* (never a referrer), is good for one use and ten
minutes, and is checked against that seat's own invite. Send P2's link to P2;
consuming it does not consume P3's.

Both end at the same page: `frontend/guest.html`, which needs `online.js` and
the vendored PeerJS next to it and nothing else - no backend, no build step.
`.github/workflows/pages.yml` publishes those three files to GitHub Pages, and
the host's Online panel defaults its invite base to
`https://spuds0588.github.io/melonDS-Multiplayer/guest.html`, with a field to
point it at your own copy instead (remembered in `localStorage`). Without a
base URL the join code still works - only the clickable link needs the page.

**A WebView without WebRTC hosts in a browser instead.** WebRTC is not a given:
Debian and Ubuntu build `webkit2gtk` with `ENABLE_WEB_RTC` off, so a Linux
window has PeerJS loaded, no `RTCPeerConnection`, and any host attempt fails
with a sentence that sounds like an ancient browser. Measured on 2026-10-17 with
a 25-line plain WebKitGTK program (no Tauri involved) against 2.52.6: with
`enable-webrtc` set to TRUE the page still reported
`typeof RTCPeerConnection === undefined`. Windows (WebView2) and macOS
(WKWebView) do expose WebRTC, so hosting works in the app window there. The app
also sets `WebKitSettings:enable-webrtc` at startup (`enable_webrtc` in
`src-tauri/src/lib.rs`, the one platform-gated line in the host): WebKitGTK
ships that setting *off* by default, so on a distro that does compile WebRTC in,
the app turns it on rather than assuming it. Such a distro also needs
`gstreamer1.0-plugins-bad` (`webrtcbin`) and `gstreamer1.0-nice` (`nicesrc`) at
runtime.

For the case that is left - a WebView with no WebRTC at all - the Online panel
has **Open in a browser…**. That starts `browser_host.rs`: a loopback HTTP +
WebSocket server that serves the *same* frontend (embedded in the binary with
`include_str!`, so no directory has to exist next to the executable) and carries
the same command interface. `frontend/browser-host.js` defines
`window.__TAURI__.core.invoke` on top of that socket, so `index.html` does not
know which backend it is talking to: same command names, same arguments, same
payloads, and the two bulk reads (`get_frames`, `get_audio`) still go as raw
bytes - eight bytes of request id in a binary WebSocket frame, then the payload
the Tauri IPC would have carried. The emulator never moves: every console keeps
running in the app, and the browser is only a view and a transport.

Three properties are load-bearing, because a socket that can start emulators and
press their buttons is worth being careful with: it binds `127.0.0.1` only, it
listens only after the user asks for it, and every command needs a token minted
at start that travels only inside the URL the app opens (a page that guesses the
port gets `403`, and the page strips the token out of the address bar as soon as
it has read it). The server hands out exactly five embedded files and never
opens a path from a request - `curl` for `/etc/passwd` answers `404`. Two
environment knobs exist for testing: `MD_BROWSER_PORT` pins the port instead of
letting the OS choose, and `MD_BROWSER` names the browser to launch (default
`xdg-open`, `open` on macOS, `start` on Windows). `MD_ROM_DIR` is where the
Browse button looks in browser mode, since a browser cannot open a file dialog
on someone else's disk - it lists `.nds` files as suggestions for the ROM field
instead (`list_roms`).

What was verified, and how. First the transport, with the app's own `online.js`
driving three synthetic consoles in a browser host: two real browsers joined
through the public broker - one by magic link and one by join code plus host
approval - and both received decoded video and audio, while a keypress in a
guest arrived at the host as `input p2: 0x1` (the A button). Then the whole
thing, on Linux, with the emulator in the app: the app was started with three
console threads on the diagnostic ROM, `browser_host_start` answered with
`http://127.0.0.1:35343/?token=…`, Chromium opened it and adopted the running
session, **Host online** in that page produced a join code and a peer id, a
second browser tab joined by code (host approval included) and decoded the
streamed screen, and a held A on that guest moved the *emulated* console's own
keypad read - the diag ROM publishes what its console sees from the keypad at
`0x02FFF01C`, read here through `tools/appctl.py mem`: `00000000 -> 00000001`
on console 2, back to `00000000` on release, with consoles 1 and 3 still reading
zero. The loopback transport measured 47 reads/s at 1.13 MB per read (~53 MB/s)
when called in a tight loop.

Two real bugs came out of that test and are fixed: PeerJS delivers the JPEG as
an **ArrayBuffer**, not the Blob the host encoded, so the guest now normalises
before `createImageBitmap` (before the fix: 31 frames arrived, none decoded,
black screen, "connected" in the status line); and a guest's touch drag needs
`pointermove`, not just a press.

Not verified: frame rate under a real game. The stream loop is built so slow
hardware costs frames rather than latency, but the numbers measured here came
from a sandboxed browser whose JPEG encode was pathological (1.9s per 256x384
frame, and `requestAnimationFrame` throttled to 2/s in a preview panel that was
not the focused tab), so no frame-rate claim is being made.

One more gap worth naming: the host's own players are still silent. The SPU's
ring is read for the first time by this feature (`md_read_audio` in
`src/frontend/tauri/Bridge.cpp`, `get_audio` in `src-tauri/src/lib.rs`, both
covered by the smoke test), but nothing plays it locally yet.

### Cutting an alpha release

An alpha is built by CI, not on a dev machine: the core has to be compiled by
each target's own toolchain (MSVC, AppleClang, GCC), and only a runner of that
OS can do it. `.github/workflows/release-tauri.yml` builds Linux (AppImage,
.deb), macOS (dmg) and Windows (**nsis** - WiX's MSI refuses a non-numeric
pre-release identifier like `alpha.3`) on every tag matching `v*-alpha*`,
run the smoke test on Linux before packaging, and publishes the installers as a
**pre-release**. It can also be run by hand (`workflow_dispatch`) to shake a
build out without tagging.

```bash
# bump the version in src-tauri/{Cargo.toml,tauri.conf.json}, then:
git tag -a v0.1.5-alpha.3 -m "melonDS Multiplayer 0.1.5-alpha.3"
git push origin master v0.1.5-alpha.3     # the tag is what starts the build
```

Two Windows-specific notes, both learned by building it: the core compiles
with **clang-cl** (the project's own Windows presets do the same, because MSVC
rejects `__builtin_trap`, `__aligned__` and the core's C++20 designated
initializers at `/std:c++17`), and it needs vcpkg's `dirent` header, which
`vcpkg.json` already declares for Windows. IPO/LTO stays off on that job: 
clang-cl's LTO output is LLVM bitcode and would need `lld-link` to archive and
link it.

Builds are unsigned on purpose (an alpha is not distributable as a signed
release): macOS needs right-click-Open the first time, and Windows SmartScreen
will warn. Nothing else about the app differs between the platforms - same
frontend, same bridge, same core.

What exists today (0.1.5-alpha.3): the app is a working two-to-four-player host. Pick
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

* **Bringing several guests in at once: turn the stall kick off.**
  `MD_KICK_AFTER_WINDOWS=0` stops the health watchdog resyncing a console that
  has stalled for N windows (default **2**, and it fires once per stall episode).
  A console *waiting for a peer to associate* looks stalled window for window,
  so the kick can land inside a join handshake; the app logs which arm it is in
  (`link stall kick: disabled (MD_KICK_AFTER_WINDOWS=0) …` at startup, and
  `p2 stalled (window 4) - left alone (stall kick disabled …)` per window with
  `MD_LINK_VERBOSE=1`). It is a diagnostic, not a fix: with it off, MPH's
  four-console join loses its guests exactly as before (history.md v0.1.9az),
  and with it on the 2026-09-25 MKDS join was torn up by the kick.

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
| `logs/md_app.log` | this run: host log, core logs, `[link-event]` lines, health + heartbeat |
| `logs/md_app.log.prev` | the previous run, kept |
| `logs/archive/md_app.log-<stamp>.gz` | older runs, gzipped, ten kept |
| `/tmp/md_crash_<pid>.txt` | a native fault or a Rust panic, with a backtrace |

Logs are **in the repo** (`logs/`, gitignored), not `/tmp`, and that is on
purpose: `/tmp` is tmpfs, and a reboot in 2026-09-26 took a whole run's evidence
with it (history.md v0.1.8t). `MD_LOG` overrides the location. The crash files
are still in `/tmp` - if a run matters, read them before rebooting.

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

  **And a *live* session used to end in exactly that 139 (fixed v0.1.9aq).**
  Quitting the app with a coupled group whose games were still exchanging
  replies crashed three times out of three (two core dumps this pass, one on
  `logs/fcs_app.log.gz`), while quitting a group whose game had already turned
  its own radio off crashed zero times out of three - which is why the
  correlation looked like a link defect. The core names it: `bt` is
  `Platform::MP_SendReply` -> `0x0` on the run-loop thread, a call on the link
  bus after process teardown had freed it. The console threads were simply never
  stopped: at the wait the guest's game arms an answer and calls that hook about
  sixty times a second, and the exit path reached the process teardown without
  running the session's `Drop` (its `session teardown:` lines are absent from
  every pre-fix log). Fixed by stopping the session in the `RunEvent::Exit` arm
  of `app.run` - the last event on *every* exit path, so the window's close
  button and the control channel's `quit` are both covered. The verification is
  deterministic rather than probabilistic, which a race needs: the log now
  carries `session teardown: stopping N console thread(s)` through
  `session teardown: complete` *before* `event loop exiting`, and the supervisor
  line reads `status 0`. Pre-fix, none of those four lines appeared at all.
  Quitting an app with no session is unaffected (the drop is a no-op on `None`)
  and takes the ~8s the health/linkmonitor threads need to notice the stop flag,
  after the window is already gone.
* `LINK ACTIVE / STALL / IDLE / OFF` health lines classify each console every
  5s; `p_n stalled with peers on the bus - resynced its bus state (kick #n)`
  records an automatic cure. `MD_LINK_VERBOSE=1` adds per-kind packet counters.
  Each line carries both directions in both units - `tx 5670(+621,
  281938B(+29670)) · rx 2685(+299, 128B(+0))`. The frame counts alone could not
  be compared with a harness run, whose link block is stated in bytes
  (`sent=17219 (1,127,344 bytes)`), so a run driven through the app could not be
  held to the harness's bar on the axis the bar is written in. Read the two
  directions differently: the *sender's* `bytes_sent` is exact, while a
  **host's** `bytes_received` is a lower bound (its clients' replies arrive
  through `LocalMP::RecvReplies`, which counts the packet and cannot size the
  payload - 128 bytes against 2,685 frames is that bound, not a loss); compare
  the host's `bytes_sent` with the guest's `bytes_received`, window for window.
  The two cumulative totals differ by the traffic sent before the guest's radio
  came on, which is why the per-window deltas are the comparable number.
* `[link-event]` lines are timestamped wireless power on/off with slot masks -
  session restarts and re-joins are visible as events, not counter resets.
* `mp pN [host|guest]: ...` followed by `mp pN last: ...` - the **multiplayer
  flight recorder**, once per console per 5s window *and again immediately*
  whenever that console's multiplayer state changes (radio on or off, a deauth
  sent or received, its role changing). For a failed join these two lines are
  the first thing to read, not the last: they are what each game did with the
  link, as opposed to what the wire carried.
  * `cmds rx / cmds tx (+delta, dup)` - did the guest's game get a command, and
    how often the seqno repeated. Careful: `dup` is **not** a retry indicator on
    its own - the working NSMB reference repeats one seqno for 9207 of its 9208
    commands, because melonDS's CMD retry mechanism and a polling host both do
    that by design. The signal is `dup` *with a frozen body* (see the `last:`
    line). The two consoles' own last-body views differ by the 4-byte FCS the
    frame carries in its trailing slot (`W_RXLenCrop`), which the *receiving*
    side's length entry excludes: compare the first `len - 4` bytes against each
    other. The rule the transport follows is GBATEK's - the sender's length
    includes the FCS, the receiver's excludes it (history.md v0.1.9r).
  * `answered (+delta, early, blank, canned)` - did the guest's game answer,
    inside its own round or late, with its own bytes or the firmware's `fc=0158`
    stand-in (`canned > 0` means the guest had nothing armed).
  * `window reads` - how many times the console's game has read its wireless
    receive RAM window. This is the one number that separates "the mail was
    never delivered" from "the game read it and refused it".
  * `replies sent / collected / empty / stale / reanchor-dropped / blank /
    notlooking` - the host's side of the answer queue, with the four distinct
    ways an answer is lost kept apart. `replies sent` climbing on the guest
    while `collected` stays flat on the host is a transport/queue defect;
    both climbing while `acks (clientfail N)` climbs on the host means the host's
    game is being told its peer failed.
  * `deauth out/in`, and on the second line `cmd seq/len/body`, `answer
    len/body`, `last ack clientfail`, `last deauth reason` - the actual bytes and
    the last decision either game made, with the US-clock time each was seen.

  Counters are always on (`Wifi::MPDiag`, `LocalMP::MPReplyDiag`; exported by
  `md_link_mp_digest`). A console that has never touched the link prints nothing.

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

**Its window is narrow, and it is worth knowing before you go looking for probe
lines in a log.** The monitor's first gate is `md_link_connected_mask()` needing
two bits, and that mask *is* the consoles' wireless power (`begin_count >
end_count`), so the window is "two consoles have powered their own radio and
none of them has received a packet for ten seconds" - a *linked pair gone quiet*.
Everywhere else the monitor takes the always-on arm instead: a game at a title
screen never powers the radio (mask `0x0`, and the diag ROM does not either),
a live NSMB session has packets in every window (mask `0x3`, counted as
`verifiedWindows`), and a `Link test` click powers the radios on and then puts
them back, so the mask returns to `0x0`. On success the probe logs nothing - it
bumps `verifiedWindows`/`lastOkMsAgo`, which the session state's `monitor` block
carries; on failure it logs `link monitor: bus probe p1->p2 not delivered -
resyncing both consoles` and bumps `resyncs`. v0.1.9y tried to reach a window
through the app (title screens, the diag ROM, a live session, a Link-test click)
and reached none of them.

#### Soak testing

The harness is the only way to run a two-console session for a long time
unattended (the app needs a human for the menus):

```bash
FRAMES=60000 tools/scripts/nsmb-route.sh --pc-every 4000
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
  * `[mp] answering cmd seq=... (AID 1, host mask 0002, clienttime 446, dupe 0)`
    next to `[mp] BLANK reply to cmd seq=... (AID 0, host mask 0002)` - the two
    halves of a guest's round. The `answering` one means the guest had a reply
    armed and its AID set at the boundary, so its own answer went out; the
    `BLANK` one means `W_AIDLow` was 0 and the guest's game had nothing to
    answer with that round. Both are the guest's game's doing, and their ratio
    is what the host's `clientfail` count in the acks is made of.
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
    crops the FCS, so a 6-byte body arrives as 2 - which is the documented
    behaviour, not a loss; the sender's length counts the FCS, the receiver's
    excludes it); if the first two disagree,
    the transport mangled the reply. `no body at all` on this line means the
    game was handed an empty answer.

    **Measured 2026-09-29 (v0.1.9ap), because those four lines were read as if
    their `body[N]` were the same span and it is not.** Over 4,096 distinct
    replies of a coupled run, `collected length - handed length = 4` in
    4096/4096 and the handed body is byte-identical to the first N bytes of the
    collected body in 4096/4096: nothing is substituted or lost. The 4 bytes are
    the FCS slot, proved by the one reply both sites printed in full (seq `EAA0`:
    collected body `00 80 81 C7 F5 97`, handed body `00 80`,
    `crc32(handed frame) = 97F5C781` = the dropped bytes). So
    `guest arms an answer`, `answer sent` and `collected a reply` count the FCS
    slot in their body and the handoff cannot; when those lines are compared,
    compare *prefixes*, and never read "the collected bodies are all distinct" as
    payload diversity - excluding the slot, the same run's 545 distinct collected
    bodies reduce to the 38 payloads the guest actually sent.

    **And the command direction does not crop the same way (v0.1.9ar).** Sampled
    over a coupled run's whole session, the 10-byte command type is handed to the
    guest's game as its first 6 bytes (`10 -> 6` in 1,767 samples) while the
    16-byte session-data type arrives whole (`16 -> 16` in 22), so `FF 00` in the
    host's state halfword *does* reach the guest's game - in a
    `BE 01 02 00 22 9C FF 00 03 00 ...` frame - and v0.1.9aq's "the crop means
    the client never sees the host's committed state" was about the 10-byte type
    and is wrong as a general claim. Read the two directions' crops separately.
  * `[mp] deauth sent: <tx> -> <rx>, 42 byte frame, reason 0000` / `[mp] deauth
    received: ...` - a console ending the other's membership (see `fc=00C0` in
    the frame types below). This is the single decisive event in the 2026-09-25
    MKDS attempts, so it prints on both ends with the body's reason code: the
    sender tells you which game gave up, the receiver's line is followed by a
    note that the console has just left multiplayer.

  * `mp p1 irq: cpu line 110754(+962) · raised 0:+299 1:+620 6:+299 7:+620
    12:+299 13:+22 14:+22 · acked 0:+299 1:+620 12:+299 13:+22 14:+22 · IF 88C0
    IE 703F pending 0000` - the interrupt level, one line per console per window.
    `raised` is every time the wifi block asked that CPU for attention on that
    source (`SetIRQ`, and the bits forced through `W_IFSet`), `acked` every time
    the game wrote a 1 to clear it, each with its delta since the last window and
    listing only sources that have ever fired. `cpu line` is the 0->nonzero
    transitions that pull ARM7 `IRQ_Wifi`. `IF`/`IE` are the block's own flags
    and `pending` is `IF & IE`. Read it as: a source whose `raised` and `acked`
    climb together is a source the game keeps servicing; a nonzero `pending` with
    no `raised` movement is a source raised and never taken; `acked` flat while
    `raised` climbs is a game that stopped entering its handler.
  * `mp p1 hw: uscnt 1 uscmp 1 us 2345CA74/2342E800 · bcn 34 0 · slot b 0000 r1
    0000 r2 811A · txbusy 0000 req 0002 com 0 pwr 0000/0003` - the register state
    at the instant the digest was taken: the US counter gate and compare gate,
    `USCOUNT`/`USCOMPARE` (the value a game waiting on the USCOMPARE -> IRQ14
    wake is waiting for; `bcn-irq14-blocked` appears here when `W_USCompare0`
    bit 0 has latched `BlockBeaconIRQ14`), the beacon counts, the four TX slot
    registers, `TXBusy`/`TXReqRead`, `ComStatus` and `PowerState`/`PowerTX`.
    This is what separates "the game is waiting on the link" from "the game is
    waiting on its own next frame".
  * `{"cmd":"probe","player":N}` (the control channel's `probe`, see
    `tools/mkds_join.py`'s `screen_truth`) carries the same wifi block plus `pc9`/`pc7`, so the
    interrupt state and both program counters can be read at any instant from a
    running session rather than only per 5s window - `tools/mkds_join.py` prints
    it as its `irq` line on every step report.

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
* **`MD_ARM_PERSIST` (default ON since 2026-09-28) answers a poll the guest's
  game armed nothing for with the frame the game itself last armed.** With it
  on, the reply slot the driver clears (`W_TXSlotReply1`) is re-supplied from
  the arm the driver made, because the slot it clears is not the slot the
  hardware transmits (`W_TXSlotReply2`) and the driver's clear is its own
  bookkeeping, not a withdrawal of the answer. With it off, an arm answers
  exactly one poll, and a poll arriving before the game has armed for it is
  answered by the firmware's `fc=0158` stand-in whose body is `00 00 00 00` -
  a *fabricated* peer state, measured at 22% of one MKDS freeze's rounds. On the
  full route with it on, the stand-in fires **once** per run (the cold-start
  round before the guest's game first armed): `canned 1`, `blank 0`, `stale 0`,
  `deauth out 0 in 0`, both consoles `LINK ACTIVE` for 3300+ rounds, and the
  parked post-VS state unchanged to the byte (same `45d15630` screen hash). The
  `reason 0003` deauth that two v0.1.9h runs hit with it on did **not**
  reproduce - those runs predate the FCS fix and `MD_REPLY_KEEP=1`. Set `=0`
  only to reproduce the pre-2026-09-28 wire for comparison; both settings keep
  the smoke test at 66/0. See history.md v0.1.9i.
* **`MD_SAME_ROUND_REPLY` (default OFF) sends a poll's answer in the round the
  poll arrived in, instead of holding the game's arm to the next poll.** It is
  the reference arm for the one hypothesis round alignment left open - that the
  fork's one-poll reply pipeline, where the host's window for round N sees the
  guest's state from before N, is a fidelity gap against hardware - and it
  answers it: with the flag on, MKDS's guest driver **starves** (its own game
  arms 5 times for 394 polls while the host's windows collect 385 replies, 380
  of them the firmware's `fc=0158` stand-in) and the group collapses before CUT
  OFF completes; NSMB's healthy 234-second two-console session also loses its
  guest at ~213 emulated s (`reason 0003`, `slots 0x000` against the flag-off
  run's `0x003` and 11,126 replies). Both games are harmed, so the pipeline is
  the games' own cadence and the flag stays off. It is a measurement knob, not
  a behaviour switch - both settings keep the smoke test at 134/0 (the default
  path is the same code, factored through `Wifi::AnswerPoll`). See history.md
  v0.1.9bz.
* **`MD_MP_FCS` (default on) writes the frame's FCS into its own trailing four
  bytes.** GBATEK: the sender's TX length *includes* a 4-byte FCS that "the
  hardware generates for you", and the receiver's RX header length is
  "excluding FCS checksum" - which is what `W_RX_LEN_CROP` and `Wifi::CheckRX`'s
  crop implement. So the transport must leave the frame's own length alone and
  put the CRC in the slot the length already counts. `MD_MP_FCS=0` leaves the
  game's own bytes there (stock melonDS computes no FCS at all); it is a
  fidelity A/B, not a behaviour switch - both settings hand the peer's game the
  same header+body. What *was* a behaviour switch is the form v0.1.9f shipped,
  which appended the CRC *after* the frame and raised the length by 4: every
  frame's received length then came out four bytes long, and the NSMB
  two-player route parked in the game's own discovery screen for that reason
  (12000 frames, one env var apart: appended = `FROZEN PICTURE`, last change
  (today's health lines call a picture that has stopped `still`, at `info`, and
  leave `looks wedged` for a console whose program counter really stopped - a
  still screen with live CPUs is a menu or a dialog, not a fault, v0.1.9ae)
  9215 frames before the end; in place or `=0` = both consoles `running` with
  262-byte session payloads to the last frame), and a second in-place run
  reproduces the pre-fix-era reference to within one picture beat (7915/7773
  beats, 19402920/18472983 pixels moved, against that run's 7916/7766 and
  19399829/18041680). See history.md v0.1.9r.
* **`MD_RX_NOCROP=1` skips the receive-side length crop, and it is the arm that
  proves the crop is load-bearing.** Every game here programs `W_RX_LEN_CROP`
  to 2, and `framelen -= (IOPORT(W_RXLenCrop) << 1) & 0x1FE` in `Wifi::CheckRX`
  (and `MPClientReplyRX`) is what turns the sender's length - which counts the
  4-byte FCS, see `MD_MP_FCS` above - into the length the peer's game reads. It
  is the only place in delivery where bytes the sender wrote are dropped on
  purpose, so with `MD_MP_FCS=0` the knob is the A/B for "are those four bytes
  payload or the FCS slot": the NSMB two-player route that plays with the crop
  **parks without it** (guest's answer frozen at `00 80 5A 5A B8 B6`, host state
  `0F 00`, a console dies), i.e. giving the peer's game four more bytes per
  frame reproduces the park. Both arms keep the smoke test at 66/0. See
  history.md v0.1.9s.
* **`MD_EARLY_REPLY` and `MD_EARLY_REPLY_ACK` were REMOVED on 2026-09-27.** Both
  the early reply (`Wifi::SendMPEarlyReply`, sending a client's armed answer
  inside the round its command arrived in) and the knob that controlled its
  guest-side bookkeeping are gone, so stock melonDS's round-end `SendMPReply` is
  the only reply path and every frame on the wire is one a console's own
  software produced. Why it went: it answers on the guest's behalf, so the
  guest's driver never gets the event it waits for (MKDS's guest arms once per
  lobby and deauthenticates seconds after joining), and it hides the question
  the digest exists to answer - whether the guest's game has an answer armed at
  its round boundary. It was default-off since v0.1.8v and never fixed a run.
  Its last measurement, which is why nothing should re-add it without a new one:
  NSMB's two-player script with `MD_EARLY_REPLY=1`, same save, 12000 frames, had
  both consoles' screens **byte-identical from frame 3200 to 12000** (a game
  parked, not playing) while the digest reported a flawless link - every one of
  the 9196 rounds answered, 0 blank, 0 canned, 0 deauth - at the idle-screen
  speed of 4.86x, against 3.28x with the picture moving. `md_game_session` now
  fails a run whose picture stops changing, so that shape cannot be mistaken for
  health again. History: `history.md` v0.1.8k-v0.1.8w, upstream `8772258f`
  (added) and `193c7ed9` (reverted).

  **Why the early reply is off by default (v0.1.8v).** It answers *on the
  guest's behalf*: the arm is turned into a frame by our fast path, so the
  guest's own driver never gets the event its state machine is waiting for. A
  game that keeps a guest sitting in a lobby then starves: MKDS's guest arms
  **once** for the whole lobby (`arms 1`, answer body frozen at `00 00 5A 5A B8
  B6`) and deauthenticates (`reason 0003`) 4-5s after joining - every run, with a
  perfect wire. Stock behaviour re-arms the game every round (`arms 9792`,
  `aids 6757`, answer moving to `00 80`), the host collects normally and the
  lobby closes into game select with both consoles still linked. Cost: about
  two-thirds of the guest's rounds go out blank (`answered 3378, blank 6417`) -
  the game's own pacing, which MKDS accepts. NSMB tolerates the early answer
  (12000 frames, 3.29x realtime), so this only shows up in a lobby-style game;
  use `=1` when speed matters and a join does not. `[mp] early replies
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
  on all of its windows together is `MD_REPLY_BUDGET_FRAMES` below, and at the
  default settings **the budget is what actually decides a window, not this**
  (2026-09-29, measured: with a 34ms ceiling and a 2-frame budget, `RecvReplies`
  waits `min(34, budget left)`, so the budget capped the wait in **2678** windows
  of one MKDS lobby and this ceiling never bound once; at `MD_REPLY_TIMEOUT_MS=2`
  the reverse held - the ceiling was below the budget, it capped the wait every
  time, and the same route closed **394** windows empty against 43, with `empty`,
  `clientfail` and `reanchor-dropped` all climbing together from 43 to 393). The
  ceiling still matters: it is what a window is allowed to spend *before* the
  budget is consulted, so setting it below the frame's own slack throws away
  replies the frame could have afforded. Keep it above the budget.
  The line the app prints now says where the value sits in emulated frames and
  where it came from - `link reply timeout: 34ms = 2.03 emulated frames
  (default)` - because it used to end "(2 emulated frames at 59.826fps)" for
  every value, so a sweep at 2ms read as two frames when it is an eighth of one.
* **`MD_DP_REPLY_MUTE=1` makes the Download Play joiner answer nothing after the
  association** (default off). Everything up to and including the association is
  unchanged - the joiner still beacons, authenticates, associates and drains its
  command queue as usual - and only the two `send_reply` calls are skipped, so the
  two arms differ in nothing the client transmits before the association and the
  comparison isolates the host's own response to silence. It is the diagnostic
  arm for a question the wire alone cannot answer: *does the host's Download Play
  phase machine advance by itself, or because of what this client answers?*
  Measured (`logs/sfc_mute.log.gz` vs `logs/sfc_base.log.gz`, 2 consoles, Star Fox
  Recruitment, no pokes): with replies the host's per-client phase walks
  `2 -> 10 -> 5 -> 14` and its command class `01 -> 00 -> 03 -> 00` in 243 ms
  emulated; muted, the phase cycles `2 -> 3 -> 0`, never reaches 10/5/14, the
  class stays `01` for 20,683 commands, 0 replies are collected in 20,069 empty
  windows and the host deauthenticates 13 times (`reason 0001`). `tools/run_app.sh`
  forwards it, and the joiner logs one line when it is on, because a knob this
  script does not name never reaches the app.
* **`MD_DP_REPLY_FORCE=<hex byte>` and `MD_DP_REPLY_FORCE_DATA=<hex byte>` sweep
  the Download Play joiner's reply *type*** (both default off; `run_app.sh`
  forwards both). The host's per-client mode machine switches on the reply's
  third body byte - `07` UsernameReply, `08` RsaReply, `09` DataReply, `0x00`
  dummy - so the byte decides which of the host's mode transitions fires.
  `MD_DP_REPLY_FORCE` answers a *post-RSA* command the joiner has no specific
  reply for with a normal reply of that type, and `MD_DP_REPLY_FORCE_DATA` does
  the same for the `04` DataPacket rounds alone, so the download's later packet
  loop can be swept without disturbing the reply that gets the host *to* the
  download.  The shipped default is the measured one and needs no knob: `01` gets the
  UsernameReply, `03` the RsaReply, a *post-RSA* command it has no specific
  reply for the **RsaReply** (the type that moves the host's mode machine out of
  14), a `04` DataPacket the **DataReply**, and the host's `05` Final the
  **FinalReply**. Measured as the default with nothing set (`logs/sfc_dl.log.gz`,
  `logs/sfc_fix.log.gz`, 2 consoles, Star Fox Recruitment, no pokes): the mode
  field walks `2 -> 10 -> 5 -> 14 -> 6`, the host streams real `04` DataPackets,
  and the image completes (see the download-loop entry below). The knobs remain
  for sweeping: with the dummy default of before, the mode field stops at `14`
  and the host streams nothing, and `MD_DP_REPLY_FORCE_DATA=09` was the probe
  that first moved the packet loop before the DataReply body was built.
* **The Download Play joiner's download loop is the shipped default, not a probe.**
  A `04` DataPacket is parsed the way GBATEK lays it out - packet number at body
  `09h`, data at `0Bh` up to the frame's own stated size, less the spec's one
  padding byte, so a full packet yields `1F8h` bytes - and the RSA frame's
  payload states the whole image (`14h` header + `24h` ARM9 + `34h` ARM7), which
  is the benchmark the collected bytes are judged against. Each round is
  answered with the DataReply (smallest missing packet at `03h`, distinct-packet
  count at `05h`); the GotAllReply replaces it the moment the stated total has
  arrived, and the host's Final is confirmed. The milestones are logged once each
  - the RSA's total, the first packet with its first eight bytes, every 64th
  packet, and the completion line - and `tools/appctl.py dpjoiner` prints the
  growth as `download: <bytes> of <total> bytes in <packets> distinct packets ·
  next wanted #<n>`. The gate is unchanged: only a title the registry marks
  `download_play` gets a joiner thread at all, so New Super Mario Bros., Metroid
  Prime - Hunters, Mario Kart DS and LEGO Star Wars II run with no joiner and no
  frame of ours on their link.
* **`MD_REPLY_BUDGET_FRAMES` caps what one frame may spend inside reply waits**
  (default **2** = two emulated frames, ~33ms; negative = no cap, the same thing
  `MD_REPLY_BUDGET_NS=-1` means in the harness). It is in *emulated frames*,
  which is the unit the budget is stated in - it was called `MD_REPLY_BUDGET_MS`
  while its value was read as a frame count, so a sweep at `17` bought 17 frames
  (284ms) rather than 17ms, and `-1` bought the default rather than "no cap".
  `tools/run_app.sh` now forwards it (it forwarded the timeout but not this, so
  the knob could not be reached through the app at all before - the app reads
  its environment directly, and an unforwarded variable never arrives). The
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
  session, and one per empty window when a game is retrying hard. (The line
  prints the *ceiling* it would have used, so at a 2ms ceiling it reads "would
  have waited 2ms ... only 33000us of slack left" - there the budget was never
  the binding constraint, which is exactly what an over-tight ceiling looks
  like.)
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
# (this mark is MKDS's CREATE_GROUP, whose coordinates live in tools/mkds_join.py)
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
# the reference route, one session, then nothing after frame 12000
tools/scripts/nsmb-route.sh --shot-dir game-captures/run --shot-every 200
python3 tools/coretest/frames_to_html.py --plain game-captures/run
```

That route is New Super Mario Bros.'s, on *this* tree and *this* save state, and
[tools/scripts/nsmb-route.sh](./tools/scripts/nsmb-route.sh) is the **only**
copy of it: the touch coordinates, the repeating tap, and the frame input stops
at all live there, and it restores the save before running and passes every
option straight through to `md_game_session`, so `FRAMES=8000`, `BIN=...` and
`--shot-dir` still work as documented below (history.md v0.1.9aa). It is one
copy on purpose - this route used to be written out here and again in
`nsmb-drop-rate.sh`, and the version here was the stale one: a route timed for a
*fresh* save's boot flow reaches the game's local-wireless entry on no other
save, links nothing, powers no radio a second time and reports `slots on the
bus: 00000`, `sent=0 received=0`. Any route is one-shot taps at the exact frame
a given boot reaches a given screen, so the note in the NSMB section below
applies to this one as much as to any script: a fresh `.sav` shifts every
frame, and the route has to be re-timed off the first run's own log when it
stops landing - in one place, not four.

#### Routes for the other titles in `Test Roms/`

Three titles were walked to their multiplayer surface for the first time on
2026-09-29 (history.md v0.1.9ac); these are the coordinates, re-readable off the
sheets cited there. All of them are touch coordinates on the bottom screen.

**Star Fox Command.** The title ignores buttons, so skip its attract with
`--autotap A:300:200` and then touch. Main menu: Single Player `(135, 26)`,
Download Play `(135, 82)`, Wi-Fi Battle `(135, 137)`, Options `(135, 180)`.
Download Play has one option, Recruitment `(135, 100)`, which opens the host
lobby and starts transmitting.

**Metroid Prime - Hunters.** The two-console route is proven end to end
(history.md v0.1.9ad, `logs/rom_mph_join.log`):

```sh
build-tauri/md_game_session "Test Roms/Metroid Prime - Hunters (USA) (Rev 1).nds" \
  --players 2 --frames 35000 --all-players \
  --touchtap 128:96:9000:400 --stop-input 11500 \
  --touch 13000:184:90 --touch 15000:134:122 --touch 17000:136:68 \
  --touch 19000:45:71@1 --touch 24000:184:58@2 \
  --touch 27000:123:72@2 --touch 30000:123:72@2 --touch 33000:123:72@2
```

Title `TOUCH TO START` `(128, 96)` - buttons do nothing on it; SELECT A GAME MODE,
where Multiplayer is `(184, 90)`; the nickname notice's `check` `(134, 122)`;
CHOOSE MULTIPLAYER TYPE, Multi-Card Play `(136, 68)`; then the two consoles part
ways: the host's CREATE GAME is `(45, 71)` and the guest's `AVAILABLE GAMES`
list row - `MELONDS`, 58 down, *not* the 46 an earlier guess had - is
`(184, 58)`, with **JOIN GAME `(123, 72)`** doing the joining. The row only
selects: the pair's stored frames step apart at frame 27000->27250, which is the
`(123, 72)` tap, and a run that omitted it left the guest on the join screen with
`sent=0` while the host beaconed. Allow ~3,000 frames between the row and the
confirm.

The title is the trap. It sits behind an attract loop that alternates
cinematics with the `TOUCH TO START` card every ~1,000 frames, and *which* phase
a given frame lands in moves with the save on the card - on the save v0.1.9ac
left behind, every `--touch` frame measured against the previous save landed
inside a cinematic that discards input (`slots 00000`, no second radio
power-on). Hence the repeating `--touchtap ...:9000:400` bounded by
`--stop-input 11500`: it lands within a few cycles and stops before it can stray
onto the screen after. Everything past the title waits for input, so one tap per
stage is enough.

**LEGO Star Wars II** has no multiplayer mode, so there is no route to keep:
its main menu is New Game `(132, 68)`, Load Game `(132, 102)`, Settings
`(132, 135)`, and Settings offers only volume sliders and Credits. A running
run of it is still useful as a negative control - the radio powers on and off
once at boot and never again.

Input options (`--tap`, `--hold`, `--autotap`, `--touch`, `--holdtouch`,
`--touchtap`) each take an optional `@P` suffix naming which consoles to drive,
1-based and comma separated, so a host can be driven differently from the
consoles joining it. Touch coordinates are touch-screen pixels: 0..255 across,
0..191 down, origin top-left.

`--stop-input FRAME` ends every input scheduled to *begin* at or before FRAME at
FRAME; an input that begins after it is left alone. It exists to separate a run's
menu route from the thing it is measuring: the route above is a repeating tap,
and a repeating tap otherwise runs to the end of the session, so "the route, and
then nothing" was not expressible before (v0.1.9aa).

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
* **MKDS NORMAL vs SIMPLE are one tap apart** (`NORMAL` vs `SIMPLE` in
  [tools/mkds_join.py](./tools/mkds_join.py), which is the only copy of the
  route's coordinates) and the wrong one still reaches *a* lobby - a single-card
  one the other console can never see. Those two constants share their
  coordinate with `MULTIPLAYER` and `DIALOG_YES` respectively, so a repeat of
  either is safe on both the main menu and mode select for every console; the
  joiner should then only ever tap the cyan group bar (`JOIN_GROUP`).

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

That same route drives the **desktop app** and not only the harness:
[tools/mkds_join.py](./tools/mkds_join.py) starts the app through
`tools/run_app.sh` (`MD_AUTOSTART_PLAYERS=2` plus the ROM), speaks the control
channel, reads each console's screen between taps, and presses CUT OFF and VS on
*the screen* - the CUT OFF patch is sampled and the tap fires when its luma
changes - rather than on a clock. Through the app on 2026-09-29 the join, CUT
OFF and VS all landed, both consoles ended on the same screen, and the link held
with `cmds tx 2687 = cmds rx 2687`, `answered 2686 + canned 1 = replies sent
2687 = collected 2683 + empty 4`, `acks 2687(+299, clientfail 4)`, `deauth
out/in 0/0` and `slots 0x3` on both consoles - the app's counters in the
harness's units. Captures: `game-captures/mkds-app/` (`mkds-lobby.png`,
`mkds-after-cut-off.png`, `mkds-after-vs.png`; the window is a 2x2 grid, each
console's top and bottom screen). It stops exactly where the harness stops - see
"MKDS's post-VS latch" in [to-do.md](./to-do.md) - and input from the app's own
path does not move it: A, START and a touch on each console's bottom screen left
both framebuffers byte-identical while the log shows the input being delivered
(`[input] frame 12524 player 2 touch -> 128,96` ... `frame 12534 player 2 touch
released`).

The same driver links **three** consoles: `tools/mkds_join.py --players 3` boots
three, walks each joiner to its own group list, then joins them. The order is the
whole trick and it is not cosmetic — an MKDS client leaves the lobby on its own
about 13–21 emulated seconds after joining, so a route that walks the third
console *after* the second has joined never has both clients in the room at once
(the bus mask went `0x3` → `0x1` → `0x5` and never `0x7`). Walk every joiner
first, then join them back to back, and the mask reaches `0x7` with `p1=host`,
`p2=guest`, `p3=guest` and the host's `collected` matching what its two clients
say they wrote. Report verbosity matters for the same reason: the full per-console
register dump is thirteen control-channel round trips, and taking it at every
menu step put ~20s of wall clock between two consoles that have to join into the
*same* lobby, so the menu walk reports the frames/mask/screens line and the dumps
are taken where the state is being judged.

New Super Mario Bros. has its own worked route there too (`New Super Mario Bros.
- two consoles link and play`), with the screens and the touch coordinates of
every step. Read its **Re-run 2026-09-28 (v0.1.9r)** note before trusting it:
for one day (v0.1.9f-v0.1.9q) the same command parked both consoles in the game's
own discovery screens (`Searching for ...`, `Please Wait.`) with a flawless link
under them, because the transport was appending its own FCS after every frame
and so lengthening every received frame by four bytes - the route reaches a
round again now that the FCS is written into the frame's own trailing slot, so
the note is a warning about the *framing* and not about the route. The second
console's `(127,160)` character tap still lands below the card it used to hit,
and the screen sequence it walks is tabulated there. A one-shot tap is only good for the boot timing that measured
it; prefer repeating taps (`--touchtap`) where a screen can be missed.

That route also drives the **desktop app**, and does it in the harness's own
frames: [tools/nsmb_join.py](./tools/nsmb_join.py) starts the app through
`run_app.sh`, waits for the *frame counter* rather than a wall clock, taps both
consoles on the same frame (one control connection per console - a single
connection serialises the two taps ~11 frames apart), and reports every route
frame the session had already passed. The frame rule is the whole of it: the
route's first tap is at frame 612 and the app runs at 59.8fps, so waiting 18s for
boot put the tap at frame 1225 and the app's *game* ~600 frames behind the
harness's for the whole session - three runs parked that way with a flawless link
under them (both consoles `[host]`, `rx bcast/cmd/reply/ack 1596/0/0/0`). Driven
from the frame counter, the app's own radio timeline lands within 5 frames of the
harness's (`wireless ON (begin #2)` at 2050 and 2735 against 2045 and 2733), and
the two consoles hold a real session: one host, one guest, guest `cmds rx 7916
answered 7916`, host `cmds tx 7916 acks 7916 collected 7612 empty 4`, `deauth 0`,
`slots 0x3`, and both screens on the game's own course - at route frames
3780/4200/5000 the captures in `game-captures/nsmb-app/` are the same frames the
harness's own shots are (canvas mean 168, nearest harness frame the same number).
Keep driving after the wire couples (`--stop-when-coupled` stops there): the
harness's repeating A/CONFIRM taps until frame 11500 are what take the games from
the lobby into the race, and stopping at the couple leaves them in the lobby.

### Does one player's input reach the *other* console's picture?

`tools/nsmb_join.py --restart --couple-fine` answers that at frame granularity,
and `--no-drive --couple-fine` asks it again of a session that is already up
(cheap: ~20s). Four phases, each four seconds, sampled **inside the page** by
`SAMPLER_JS` - one `__mdctl.frames()` per iteration, ~70 samples/s, and only
pairs where *both* consoles advanced exactly one emulated frame are counted, so
the comparison is in the game's own clock rather than the sampler's:

    A no input   B the guest alone holds RIGHT   C no input   D the host alone does

Each phase returns a bitmask per console and screen of every pixel that moved,
plus per-pair moved-pixel counts. The two no-input phases are the baseline, and
the question is which pixels moved under one console's input and in *neither*
baseline. Measured on the app's own NSMB race (2026-09-29, three runs):

* the pressed console's own screens move 29,000-31,700 px (top, the whole view)
  and ~1,900 px (bottom),
* the **other** console's pictures move 740-1,927 px of the bottom screen (it
  moved in all four runs) and, in three of the four, 1,000-1,700 px of its own
  top view in a band at y 155..178 - the run where the pressed console walked
  least is the one whose cross-effect is smallest, which is what a causal
  effect should look like,
* the shared map's pixels are where the bottom figure lives: the two consoles'
  bottom screens differ by **0 px per pair**, so the bottom screen is *one*
  picture both consoles render, and a change there is one picture moving on both.

Two things about reading it. **The control gates the claim**: if the console that
was pressed does not move its *own* picture, the state cannot answer the question
and the run says `inconclusive` instead of reading a stray handful of pixels as
attribution (a session ten minutes on printed `attributed ... 11 px` under a
`> 0` test before this was a gate; the guest's own screens had moved 6 px). And
**the state matters**: the game moves on from the race the measurement needs -
later in the same session the guest's RIGHT stopped moving its own screen
entirely - so an `inconclusive` result means re-run the route, not that the app
failed. The `D` phase is what makes a *null* readable: it shows the host's own
screen responding to its own input in the same state.

### Driving the app from a shell (`tools/appctl.py`)

The app carries an opt-in control channel so it can be test-driven without a
human at the keyboard - pressing buttons, injecting input, reading the emulated
screens, saving screenshots (history.md v0.1.8u). `tools/run_app.sh` turns it on
by default:

```sh
MD_CONTROL_PORT=7373 tools/run_app.sh     # default; MD_CONTROL_PORT= turns it off
```

It binds `127.0.0.1` only, speaks one JSON object per line, and logs every
command as `[control] <cmd>: <result>` in the ordinary app log so a scripted run
and its post-mortem tell one story. **Never expose the port**: it can press
buttons and quit the app.

**The link sampler, and how to ask *when* rather than *whether*.** The digest
prints each console's last command and answer body every five seconds, and the
`[link-trace]` lines that print them have no frame number - so the digest can say
"the host's command body shows `FF 00` somewhere in this run" and cannot say
whether that happened before or after the event being explained. `probe` already
carries the same two bodies to Rust (`mp.cmd_body`/`mp.answer_body`), so
`tools/scripts/link-commit-trace.py sample` just polls it: one TSV row per ~12 ms
with the emulated frame, the bus mask, each console's *own* view (on the host its
command, on the client the command handed to its game) and the arm counters, so
one `sample` and one `summarise` later the timing is a frame number. Per console,
`cmd_body` is the command that console saw and `answer_body` the answer a client
armed - one TSV holds all three legs of the handshake.

```sh
# while a route runs (the server threads per connection, so the driver and this
# can talk at once):
python3 tools/scripts/link-commit-trace.py sample --seconds 200 --out logs/x_link.tsv
python3 tools/scripts/link-commit-trace.py summarise logs/x_link.tsv   # + its app log automatically
python3 tools/scripts/link-commit-trace.py window logs/x_link.tsv     # the rows at a death
python3 tools/scripts/link-commit-trace.py selftest                   # the reader, on fixtures
```

`summarise` exits non-zero if its checks fail and `selftest` drives the reader
against fixtures (a surviving walker, a lobby-only host whose bus goes away, an
empty TSV, a tap after the last sample, a foreign TSV, a short row, all three
states of `window`) *and* against the archived runs, so a claim in these docs is
a command rather than an argument. Two things it exists to keep straight: the
state halfword lives at body bytes 6-7 of **both** command types, and the 10-byte
type's tail is not the state (`FF 00 B8 B6` there against `FF 00 03 00` in the
16-byte one).

**The app writes the game's save file; the harness does not.** A session leaves
the card alone until it ends, so an app run looks harmless right up to the
moment `Test Roms/<game>.sav` comes back with a different md5 - a 40-minute
Metroid Prime - Hunters session did exactly that (v0.1.9ae, `ffa024d5…` ->
`96720058…`, restored from `/tmp/romtest-sav-before/`). `tools/run_app.sh` now
pins the ROM's `.sav` to `game-captures/sav-backups/<name>.sav.before-app-<stamp>`
before the app starts and, when the app exits, prints either `sav … unchanged
(md5 …)` or `sav … CHANGED (md5 A -> B)` with the one `cp` that puts it back. It
does not restore by itself - the same launcher is used to play - so a scripted
run that needs byte-identity restores from the printed pin. The harness needs
none of this: every `Test Roms/*.sav` survived all of its runs unchanged. **And
MKDS is not the only title that does it**: a Metroid Prime - Hunters match wrote
`ffa024d5…` -> `b4966db3…` at session exit (2026-09-29), so read the supervisor
line before assuming a game left its card alone.

**Wait on the picture, not on a frame count.** Every route in this project was
written as a list of frames, and a frame count is a guess with two ways to be
wrong: a tap that lands on nothing still costs its frames, so a route that slept
too little goes on before the screen changed and silently drives nothing, and one
that slept too long goes on after the state it wanted has gone. The same
Metroid Prime - Hunters `JOIN GAME` tap took ~7 s to land at one moment and
~13 s at another, in the same session (2026-09-29) - a 5 s sleep passed in one
case and lost the lobby in the other.

```sh
h=$(tools/appctl.py digest --player 2)             # hash this console's screens
tools/appctl.py touch 122 64 --player 2            # the step whose landing is in doubt
tools/appctl.py waitfor --change "$h" --player 2   # did it land, and when
tools/appctl.py waitfor --settle --player 1        # wait out a menu transition
```

`digest` is an md5 of that console's two decoded screens (not of the PNG, whose
bytes change with the encoder), taken off the same shot `shot` writes. `waitfor
--change` exits non-zero with "the step before this one did not land" — which is
the thing a frame-timed route could never say — and `waitfor --settle` refuses to
settle while something on the screen is still animating, which is the honest
answer for a console that is playing a match. `waitfor TEXT` (the original form)
still reads the window's status line. `sleep` now takes `--ms` as well as a bare
count, like every other duration command.

**Real time passes between two driver commands.** There is no pause: a console
keeps emulating while a driver reads a screenshot, so a route that inspects
between steps races the game — this is what dropped a joined MPH guest three
times while a PNG was being looked at. Never diagnose a timing-sensitive state
from a picture taken by hand; take the picture inside the script, or wait on the
picture with the commands above.

```sh
tools/appctl.py status                     # session, frame counters, link mask
python3 tools/appctl.py ui                 # the window's own status line, active console
tools/appctl.py see                        # both DS screens as ASCII, 128 cols
tools/appctl.py see --player 2 --screen bottom --cols 96
tools/appctl.py see --crop 0 60 256 20 --cols 256 --aspect 1   # read one menu line
tools/appctl.py shot --name lobby          # logs/shots/lobby.png (all consoles, both screens)
tools/appctl.py window --name desktop      # whole screen via spectacle, app window included
tools/appctl.py render logs/shots/lobby.png --cols 120         # any PNG back to ASCII (no app needed)
tools/appctl.py press a --player 1 --ms 120
tools/appctl.py touch 128 95 --ms 150      # pen down at a DS coordinate
tools/appctl.py tap KeyJ                      # a real key event on console 1's zone (KeyJ is its A)
tools/appctl.py tap Comma                     # ... and console 2's zone (Comma is its A)
tools/appctl.py click '#start'             # click UI by CSS selector
tools/appctl.py advance --player 1 --frames 300   # wait for the console to actually run
tools/appctl.py wifiio --player 1          # what player 1's game reads out of the block, writes into it, and says over IPC
tools/appctl.py mem --player 1 04000208 12 # 12 bytes of that console's own memory (hex addr, 4-byte steps)
tools/appctl.py vary body --body "00 00 00 00"  # diagnostics: perturb one input (see below)
tools/appctl.py script tools/scripts/example.txt  # a file of these, one per line
```

`render` is the one subcommand that reads a file and nothing else, so it runs
with **no app and no control port** - which is the case it exists for, because
the PNGs it reads are usually what a finished `md_game_session --shot-dir` run
left behind. `--crop x y w h` (commas tolerated) and `--mono 140 --bright --zoom
2` are the combination that turns an 8px DS font into something you can read:
`--mono` thresholds, one character per pixel, `--bright` for bright ink on a dark
panel (a DS dialog box) and the default for dark ink on a bright one (a menu).
`tools/glyphs.py` is the next step when a word still needs to be certain - it
splits a crop into one block per character.

Two things a driver hits before it hits a game, both measured on 2026-09-28.
Reloading the window mid-session (`eval location.reload()`, or a user pressing
reload) is **survivable**: the Rust side and the session keep running and the UI
comes back, but the webview stops answering for over 20 s, so the command that
triggered the reload fails and every webview read after it used to fail too. The
read-only webview commands (`ui`, `see`, `shot`, `text`, `bands`) are now retried
once after 3 s, and commands that change state (`click`, `press`, `touch`, `key`)
are deliberately not - a duplicated click is a duplicated input. They say so in
the failure instead. Careless arguments are also sentences now rather than
tracebacks or silent successes: `press 0xZZZZ`, `script /missing.txt`, `press` or
`touch` with no session running, `click` on a disabled control, and `advance N`
(the bare count, which argparse used to reject). `key Bogus` is refused with a
sentence too, and it names what the page does act on: the digits, Tab, the peek
key and the running session's own keymap. `key`/`index` ask the frontend's own
`window.__mdkey`/`window.__mdselect` rather than keeping a list of their own, so
there is no second list to drift - which is the trap that let `key Bogus`
succeed in silence until now (history.md v0.1.9ab).

`vary` is the one-input-at-a-time probe of a parked handshake (history.md
v0.1.9k for what it was built for and v0.1.9u-v for what it measured): `body`
replaces the body bytes the console's game reads, `off` clears it and reports the
cumulative `applied`. Which bytes those are follows from the console - player 1's
game reads the answers it collects, player 2's the commands it is handed - so one
arm perturbs one input, and `applied` (read back from the probe, not from the
caller's own arithmetic) is how a cell is known to have fired:

```
tools/appctl.py vary body --body "00 00 00 00" --player 1 --value 2000
tools/appctl.py vary off --player 1          # cumulative applied
```

A probe set on a dead link reads `applied 0`, which is a measurement of nothing -
check a digest line before believing a probe.

`mem` reads a console's own memory out of the emulator, which is the instrument
the roadmap named as the unblocker for every game question a picture cannot
answer (the MKDS park, MPH's `SELECT HUNTER`, NSMB's star gauge). The address is
a hex string with or without `0x`; `addr` and `len` must both be multiples of 4
(the bridge reads whole ARM9 words) and `len` is 4..4096. Everything else is
refused with the reason - `mem … 0400020B 8`, `mem … 02000000 9999` and
`mem … zz 8` are three sentences, not a rounded read or a traceback. The read is
proof against the thing a picture is bad at: on 2026-09-29 a console sat on a
black screen with its ARM9 in the BIOS's vblank-wait loop, and
`mem --player 1 04000208 12` read `00000001 00000000 00040019` - IME 1, IE with
vblank enabled - at exactly that instant, which is how "this CPU can never be
woken" was ruled out instead of argued about.

`poke` is the write half of that, and it exists because of a limit the read half
cannot cross: a read can show that a loop's exit condition is **false**, but only
a write can show that the loop is what *holds* the console. `poke <hex addr>
<hex words> --player N` writes whole 32-bit words (space- or comma-separated,
1..1024 of them) through the bridge's existing `md_write_ram` and answers the
bytes as they now stand, so what the bus kept can be compared with what was
asked for. Words rather than bytes, because a variable reads here the way it
reads in `mem` and there is no endianness to get wrong. Used once, it settled
the MKDS park in a single step: `pc9` was `0200FD10` on 37 of 40 samples, one
`poke 2173300 00000000` moved it to `02021544` on **40 of 40** - the branch that
loop's own test guards - and the consumer count beside it advanced by exactly the
dequeue's increment. Diagnostics only; nothing on the product path calls it.

`tools/scripts/sfc-dp-poke-search.py` is `poke` turned into a search: it writes
one word at a time into the Star Fox host's own Download Play records (entrant
record `0x020B4C00`, received-name record `0x020B17A0`, DP title records
`0x020B3D40`/`0x020B5640`), holds it, restores it, and scores each trial on the
wire - the joiner's received command class (`dpjoiner.lastCommand`) and the class
byte the host's composer last wrote into its slot (the `002A` row of `wifiio`).
`commandsRecv` must keep advancing, so a poke that freezes the host is reported
as a stall at that word rather than scored as no effect; because those records
are live state the host rewrites while it runs, each word's value is re-read
immediately before its trial and the one in place is what gets restored.
`--dry-run` lists the candidates; `--only`/`--region`/`--skip`/`--resume-from`
scope a run; `--json` logs it. The parked state is a prerequisite (drive it there
with `tools/scripts/sfc-dp-host.sh`), and the harness refuses to run if the
joiner is the console it is about to perturb. What it measured (v0.1.9cp): no
single word arms the transfer - the words the host consults live are structural
and changing one freezes its command stream 10-30 s and then recovers - which is
the honest shape of the negative result, not a missing measurement. Note that
`dpjoiner`'s `--player` argument *moves* the joiner console; omit it to read its
state.

`tools/scripts/sm64-dp-host.sh` is the same kind of state-based route as
`sfc-dp-host.sh`, for the test set's other single-card title, Super Mario 64 DS.
Its VS screen is the game's Download Play entry, and its font defeats the ASCII
screen readers, so the walk is touch geometry measured off framebuffer captures
(`appctl.py shot` plus `render`; the composite is 256 px per console, the top
screen above the bottom one): a tap anywhere on the title screen, then the **VS**
bar at about `(37,172)` on the bottom screen. Once the joiner is registered
the waiting room's bottom bar - the host's own "TOUCH TO START" - starts the
download (with no client it answers "Practice alone?"): the script waits for
registration, settles `SETTLE_AFTER_ASSOC_S` (15 s, for the host's own roster -
a same-second tap is swallowed) and taps `(128,168)`. Success is read only
from the link and the joiner's counters, never from a screen: exit 0 when a
`04` DataPacket is seen (or the `download:` count moves), 2 when the host
reached its waiting room, registered the client and stated its RSA image but
never streamed data, and 1 when the waiting room was never reached. Overrides
`TAP_X`/`TAP_Y`, `VS_X`/`VS_Y`, `START_X`/`START_Y`, `BOOT_S`, `HOST_WAIT_S`,
`TRANSFER_WAIT_S`, `SETTLE_AFTER_ASSOC_S`, `APP_LOG`, `LOG`.
`tools/scripts/sm64-dp-launch.sh` continues past BOOT through character picks
(P1 panel `(34,68)`, P2 panel `(98,68)`), a ~70 s stage settle, the guest's
headline tap `(128,60)` to the hub (gated on the hub's known digest), and the
hub's CONTINUE box `(128,72)` - never the QUIT VS box `(128,120)`, whose
2-button screen tears the session down (LEFT tears down, RIGHT backs out,
both digest-proven). The hub boxes were named by OCR (`tesseract --psm 7`).
`tools/scripts/sm64-dp-force-sweep.sh` runs that route once per reply type
(`MD_DP_REPLY_FORCE`), launching and quitting the app per value and writing a
one-line verdict per value - the same question `sfc_force8`/`sfc_data9` answered
for Star Fox.

`tools/scripts/dp_attach_gate.py` is the shared early predictor for the guest
attach lottery both single-card titles lose (SFC 4/7, SM64 2/6): past boot+15 s
the host's reply-window reads and the guest's cmds rx must both climb (healthy:
thousands per 5 s; dead: exactly +0 on both). Single-shot off the app log
(`MD_LINK_VERBOSE=1` required): exit 0 attached, 1 dead, 2 undecided (no boot
or boot+15 s unreached - poll again). Validated by replay on five saved
sessions across both titles with zero false calls, including a mid-session
slice (attached) vs the same log's tail (dead after teardown).
`tools/scripts/dp-session-retry.sh` loops the whole session around that gate:
env `ROM`, `HOST_SCRIPT`, optional `NEXT_SCRIPT`, `MAX_TRIES` (default 3) - it
launches, drives the host route, polls the gate, and relaunches on refusal
instead of working menus on a dead session. Proven live (two refused tries,
clean relaunch, exit 1 with no strays).

`tools/scripts/sfc-dp-arm-class.py` is `poke` aimed at the one byte that decides
the Download Play command class - byte 0 of the host's 6-byte DP command body at
ARM9 `0x023722E0`, which is exactly the byte GBATEK's CMD DATA `[06h]` carries to
wifi-RAM slot offset `0x2A`. **It is a host memory patch, not a client
behaviour**, and it says so in its own header: it holds that byte at a chosen
class (`--class`, default `04`) while the host runs, reports the classes the
joiner saw, then restores the original byte. The host rewrites the byte to `00`
every round (~166/s, back within 20 ms), so a hold has to keep re-writing it -
the tool does, and prints how many holds it used. `--natural` just reports the
byte and writes nothing. Nothing is sent to the host over the link and
`dp_joiner.rs` is not involved; the tool is not wired into the app. With the
byte held at `04` the wire carries `06 01 02 00 03 11 04 50 08 00 02 00`
(15,356 frames measured against 309,913 of the natural `... 11 00 50 ...`), and
the joiner logs `the host's Download Play command is now 04 (DataPacket), size 3,
flags 11, slave flags 0002`. What the host streams is its own five-byte stub
body, not a real payload: the frame layout is the spec for the capture side, the
packet contents are the host's placeholder. A parked host is a prerequisite
(drive it there with `tools/scripts/sfc-dp-host.sh`).

`tools/scripts/sfc-dp-data-frame.py` moves the same experiment one step earlier,
to the sender's own *length* input rather than the class byte it produces. The
ARM9 hands the ARM7 a frame by posting a WM message `{id 0x800F, buffer
0x023722E0, length}` into the next of eleven 0x100-byte slots at `0x020B4C40 +
0x100*k` and pushing `(slot << 6) | 0x0A` down the IPC FIFO; the ARM7 wireless
library's composer (`0x023EFC18`) copies `length + 2` bytes of that buffer into
the wifi-RAM TX slot, and does not copy at all when the length is zero. The tool
holds that length word (`--len`, default `40`) and, optionally, the class byte
(`--class`, default `04`; `none` to leave it), reports the classes the joiner saw
*and* the frame body lengths the host sent (read out of the new part of the app
log named by `--log`/`$MD_LOG`), then restores every word. **It is a host memory
patch, not a client behaviour.** Only slots that really carry the DP command
message (id `0x800F`, buffer `0x023722E0`) are touched, so the one slot holding a
different message is left alone; every word is read back after restore and the
tool says so loudly if it is not the value it started from. `--natural` reports
and writes nothing. Measured: `--len 0x40` sends `body[72]=06 01 02 00 20 11 04
50 08 00 02 00 00 38 02 00 00 00` (93 in a 6 s hold), `--len 0x1F8` sends
`body[512]` (38), and `--len 0` sends only the 14-byte header form - the
composer's own gate. The six-byte DS header, the incrementing packet number, the
class and the payload are the capture-side layout; the payload itself is still
the host's stub, because the host's engine never fills `0x023722E0` with image
bytes. Holding the words perturbs the host's own state machine: after the `0x1F8`
sweep the host deauthenticated the joiner (reason `0001`) and moved to another DP
phase, so the tool is a measurement, not a fix.

`appctl.py memwatch <lo> <hi>` answers the question no static search can: which
instruction stores into a span of a console's own main RAM. `NDS::SetMemWatch`
arms an inclusive window (off while `lo > hi`) and the main-RAM case of all six
`ARM9Write8/16/32` and `ARM7Write8/16/32` entry points records the writer's PC,
LR, CPSR and register file (`MemWatchHit`; the first `kMemWatchMax = 64` hits are
kept, because the writer wanted runs first, and the register file is printed for
the first `kMemWatchVerbose = 4`). `memwatch` with no window, or `--off`,
reports without arming; arming clears the hits. Both CPUs' main-RAM spellings are
folded onto one window (the ARM7 also sees it at `0x02800000`), so a mirror
address cannot arm a watch that matches nothing and answers `0 stores` - which
reads exactly like "nothing writes this" and was a real defect, caught by using
it - and the report prints the folded address for the same reason. It costs one
predictable compare per store while disarmed. **The JIT writes main RAM on its
fast path and never reaches these functions, so arm it on the interpreter**
(`MD_NOJIT=1`). Used that way it named the writer of Star Fox's Download Play
class byte on its first try (v0.1.9cr): a `strb` in the DP engine's publisher,
running in **IRQ mode**, reached through a computed pointer that no literal
search could find. Six smoke checks hold its contract (`md_smoke_test`): a store
in the window is named, one outside it is not, a window armed at a mirror matches
the folded address, re-arming drops the previous hits, a store ending at the
window's low edge is not claimed, and an undersized report buffer is refused
rather than truncated.

The same registers are now on the probe the drivers call: `arm9_ime`,
`arm9_ie`, `arm9_if`, `arm9_vblank_pending` and `arm9_can_take_irq`, read live
rather than from the five-second digest, plus `pc9`/`pc7` sampled on demand
(the digest's cadence is the wrong resolution for "is this CPU stuck in that
loop or visiting it?"). **The digest line's `W_IF`/`W_IE` are the *wireless
block's* registers, not the CPU's.** They were printed as a bare `IF`/`IE` next
to `arm7 IME … IE7 …` until 2026-09-29, and that label is what turned a parked
console's `IE 0000` into a false reading of "interrupts are disabled, nothing can
wake it" for a whole pass - the console was awake, in its vblank wait, and one
button press returned it to the game's title screen. A label that can be
mistaken for another register is a defect even when the value is right.
**The same paragraph had a second defect, and it was a wrong bit rather than a
wrong label: `wifi_ie_bit` tested `IE7 & 0x80`.** `IRQ_Wifi` is **24** -
`Wifi.cpp` raises it with `NDS.SetIRQ(1, IRQ_Wifi)` and `NDS::SetIRQ` is
`IF[cpu] |= 1 << irq` - so the wifi line is **bit 8 of the high halfword**
(`IE7 & 0x0100`), and bit 23 is `IRQ_SPI`. Every log printed before 2026-09-29
that says `wifi ie no` is reading bit 23; the archived runs' own raw `IE7`
values say the wifi line was enabled the whole time. `tools/scripts/mp-arm7-watch.py --teardown <log>...`
now says so from a log alone - it also prints, per console, every wireless
on/off and deauth in/out in frame order, who deauthed first, whether the other
console was handed that frame, and how many five-second digests left anything
pending (`W_IF & W_IE != 0000`); `--selftest` checks it against the three
archived sessions.

**A single-instant PC sample cannot answer what a CPU is doing, and this file's
own tooling learned that the hard way.** Both ARM7s halt in the BIOS between
their scheduled wakeups, so a `pc7` sampled once every 0.3 s lands in the halt
loop most of the time on *either* console; that produced "the guest's ARM7 is
idle while the host's runs its code" out of 193/226 versus 100/333 samples,
which the emulator's own per-register read tally then contradicted (v0.1.9au:
the guest does ~7x the host's wireless traffic, and the spin pair - client 158-192
reads/frame against host 6.2-6.9 - is the same order in a survivor and in two
failures). So the live TSV now carries an aggregate beside the instant
(`io_rate`, `io_spin`, `io_top`, from the `wifiio` counts differenced over the
frames the sample covers), and
`tools/scripts/mp-arm7-watch.py --activity logs/<run>_stdout.txt` differences the
same counters between the driver's own blocks - which is how that table was
produced, and repeatable in one command. It differences **only registers that
appear in both blocks** (it prints how many that was) and says so: a register
entering the driver's top-ten list has no baseline, and differencing it would
report its whole history as one window's work - the first version of this mode
did exactly that and inflated the host's total by ~35% until it was fixed in the
same pass. When the question is "is this CPU idle or busy", ask it of a tally,
never of a snapshot - and then check what the tally left out.

The same report answers the opposite question - *which instruction wrote this*
- for a console whose game is stuck (v0.1.9co). Beside the read side it prints a
**write-side recorder**: every write the console's own software makes into the
low wireless RAM (offsets below `0x40`, the header and the head of a frame body)
in order with the writer's PC and LR, a per-offset last-writer table, and the
command-slot arm's PC/LR **with the writer's whole register file** - so a frame
that keeps going out with the wrong class byte can be traced to the code that
composed it rather than guessed at from the wire. It also prints an **IPC FIFO
trace**: the last 64 words each way through `IPCFIFOSEND`, each with the pushing
CPU's PC and LR, which is the channel a DS game hands work between its two CPUs
over. Two things to know before trusting a `pc=`: (1) the **JIT** reports `R[15]`
as the start of the block an instruction compiled into, so run the session with
**`MD_NOJIT=1`** and the signal becomes instruction addresses - the ARM
interpreter reports the executing instruction plus 8, the pipeline offset, and
the report says so; (2) toggling the JIT *mid-session* through `set_jit`
segfaults (measured), which is why the knob is an autostart variable and not a
runtime command. `probe` **is** an `appctl.py` subcommand
(`tools/appctl.py probe --player N`, register files, interrupt state, MP and IPC
counters); the raw form `{"cmd":"probe","player":N}` still works and is what
`tools/mkds_join.py` uses.

The frame ledger (`[mp] tx frame: slot N (NAME) fc=... seq=...`, the transport's
`[link-trace] pN tx/rx`, the reply window's credit and the delivery lines) is the
per-frame record, and it is joined **by frame identity, never by line order**,
because a slot line and the transport line that follows it are the same frame.
Two things it taught, both still load-bearing (history.md v0.1.9p): the ledger
has to print the control word it is actually about to send (the no-seqno bit is
applied after the game's copy is read, so 3439 of 8536 replies were logged
`fc=0118` while the wire carried `fc=0918`), and a broadcast is not a separate
path - the same `fc=0080` arrives as the peer's beacon (110B, delivered `as an MP
client`) *and* as this console's own `melonAP` beacon (60B, `outside
multiplayer`), and counting them together once read as the host's broadcasts
being undelivered.

Notes from building it:

* **Why not xdotool or tauri-driver.** This is a Wayland session: there is no
  `xdotool`, synthetic X11 events would depend on compositor focus, and
  `tauri-driver` needs `webkit2gtk-driver` from apt plus a `cargo install` - and
  would still only drive the web UI, never the emulated consoles. Tauri's own
  `eval`/`invoke` reach the webview from any thread regardless of the display
  server, so the channel goes through the page: `key` dispatches a real
  `KeyboardEvent` and the frontend's own keymap/handler does the rest, which is
  why a scripted `press a` exercises the same path a user's finger does.
* **The screen reader matters more than the screenshot.** An agent can see a PNG
  exactly as well as it can see a photograph - not at all, but text it can read.
  The ASCII renderer inverts the luminance ramp on purpose (DS menus are dark
  text on bright panels, so dense characters must mean dark pixels), and for
  actual menu text, crop the strip and pass `--aspect 1` for one character per
  pixel: the DS's 8-pixel font is only legible at that ratio.
* **`status` is answered by the Rust side**, so it still reports when the webview
  or the UI is wedged - which is exactly when a test drive wants to ask.
* **Screenshots go next to the log**: `MD_LOG`'s directory + `/shots`, or
  `MD_SHOTS_DIR` when set.
* **One command per line in a script** lets a whole run - boots, waits, presses,
  taps, screenshots - be replayed and diffed between builds. `--player` is
  1-based everywhere in `appctl.py`; the button names are `a b select start
  right left up down r l x y`, composable with `+`, or a raw mask in hex.

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

**`mem --cpu 7` reads the ARM7's own address space, and until 2026-09-29 nothing
could.** `md_read_ram` walked `ARM9Read32`, so every question about the ARM7 side
(WRAM at `0x03800000`, the game's own ARM7 image in shared WRAM at `0x06000000`)
was unanswerable from the control channel - and the routine that would have to feed
the ARM9's message queue lives there. The bridge now has `md_read_ram_cpu(inst,
cpu, ...)` (9 = ARM9, 7 = ARM7), the `mem` command takes a `cpu` field and echoes
it in the answer, and `tools/appctl.py mem --cpu 7 <addr> [len]` is the caller.
Exercise: 80 reads of 4 KB cover all 320 KB of ARM7 memory in about a second, and
the result disassembles as real code. Two traps worth knowing before using it on
this question: the ARM7 reaches I/O through a `0x04000000` base plus an offset
(`add r0, r1, #392` for `IPCFIFOSEND`), so scanning for the absolute address finds
nothing; and a listing built by concatenating two regions into one buffer gets the
second region's VMA wrong by the gap between them - read and disassemble each
region separately.

**The same space can be written, and the parked-game probes read it too.**
`md_write_ram_cpu` (behind `tools/appctl.py poke --cpu 7`) is the write half; the
read-back comes from the same space, so a poke answers what the bus kept. The
first ARM7-side "satisfy it and watch" experiment used it: the gate at
`0x0380B3A4` (`*(0x0380FFF4)+0x3F4`) is consumed within 2-5 emulated frames of the
write and the two ARM9s leave their dequeue at once. Two tools carry the method:
`tools/scripts/mkds-park-probe.py --cpu 7` samples `pc7` and reads whose address
space asked for (`--var ADDR[:LEN]` on both consoles in one window), and
`tools/scripts/arm7-ctx-watch.py` traces the wifi context's fields (the gate and
the queues at `+0x188`/`+0x1F4`/`+0x200`, plus the `+0x300` pointers) live, one
row per console per sample, so "is this ever nonzero" is answerable over a whole
session rather than at one instant. Three traps this pass paid for: the ARM7's
`0x06000000` window is **VRAM** in melonDS's map and MKDS maps its wide ARM7 wifi
library into it, so its 128 KB image plus its data tail is what `mem --cpu 7`
returns there (`0x06020000`-`0x0603FFFF` is zero, `0x06040000+` is the image's
mirror, and the tail from `0x06019000` holds live per-console data - two consoles'
"images" differ only in that tail); `pc7` is one instant and lands in the BIOS
most of the time on either console, so only the histogram or the read counters
say what an ARM7 is doing; and a post path found by disassembly is one of fifteen
call sites of `0x0600F19C` - each hands it a different descriptor inside the
wifi context, so naming one is not naming the channel.

**IPC FIFO traffic counters, because a level cannot answer "was this channel ever
used".** `NDS::IPCSent9`/`IPCSent7` and `IPCRecv9`/`IPCRecv7` (src/NDS.h, bumped
where `IPCFIFO9.Write`/`IPCFIFO7.Write` and the two receive reads actually happen
in src/NDS.cpp), read by `md_get_ipc_stats` and exposed in the driver's probe JSON
as `ipc` (`arm9_sends`, `arm7_sends`, `arm9_recvs`, `arm7_recvs`, plus each CPU's
`fifocnt` and `sync`). The per-CPU FIFO members are protected, so the bridge reads
through `NDS::GetIPCTraffic`. What it bought in one run: the ARM7 pushes ~7 words
per frame into the FIFO for a whole session and the ARM9 pops every one of them,
*including at the park* - so "both FIFOs read `0x8501` (empty)" says nothing about
whether a channel is in use, and the earlier reading of it as "IPC idle" was
wrong. `tools/scripts/arm7-ctx-watch.py` carries these columns next to the wifi
context's queue counts and the retry-queue count `q1f4_8` (the halfword
`0x600F19C` tests before deferring a message: `ctx+0x1F4+8`).

**`md_get_regs` reads a CPU's whole register file, and a park needs it.** A
program counter names a loop, not the function waiting in it, and one sample a
second cannot tell a stuck loop from a sampler that always lands on the same
instruction. The probe now carries `regs9`/`regs7` and `sp9`/`lr9`/`sp7`/`lr7`
(`md_get_regs`, cpu 9 or 7; the two cores are unrelated C++ types - ARMv5 and
ARMv4 - so the copy is written per CPU). `tools/scripts/mkds-stack-probe.py`
waits for a park, prints the register file and scans the bytes above `sp9` for
return addresses (a word in the game's code range whose preceding instruction is
an ARM `bl`/`blx`, or a Thumb `bl` high half), dumping the raw stack beside the
report. `tools/scripts/mkds-park-burst.py` samples a park twelve times a second
with the queue words `0x02173300`/`0x021732AC`/`0x021732CC`, which is how
`pc9 0200FD10` was shown to be an *instant* of the SDK's `delay(r0)` helper
(`r0 = 0`, `lr` and `sp` identical in every sample) rather than a wait. Three
traps this pass paid for: the trigger "both consoles' `pc9 == 0200FD10`" also
matches the **boot path** (the first live run of the burst script "found" a park
at frame 1,379 with the link down - require `link 0x3` *and* frames past the route
as well); the player field on the control channel is **1-based on the wire**
(`ctrl_json_player` subtracts one), so a 0-based index is refused with
`player 0 is out of range 1..4`; and the ARM7's *shared WRAM* - a third code copy
at `0x037F8000` where the SDK's IPC library lives, holding the `0x04000188` and
`0x04000184` literals and the `WaitForInterrupt` loop - is a different memory from
ARM7 WRAM (`0x03800000`, which reads zero at those offsets) and is what
`mem --cpu 7 037F8000` returns.

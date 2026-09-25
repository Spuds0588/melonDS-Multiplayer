# melonDS Multiplayer - Project History & Lessons Learned

This document tracks the history and evolution of the melonDS Multiplayer project, including lessons learned from the sister project mgba-splitscreen.

---

## Project Origins

**Date**: September 2026

**Context**: melonDS Multiplayer is a fork of the [melonDS](https://github.com/melonDS-emu/melonDS) Nintendo DS emulator project, adapted for **local splitscreen multiplayer** — similar to how [mgba-splitscreen](https://github.com/Spuds0588/mgba-splitscreen) adapted mGBA for GBA splitscreen.

### Inspiration: mgba-splitscreen

The mgba-splitscreen project demonstrated a successful approach to emulator-based splitscreen multiplayer:

> "mGBA fork with multiplayer focus. Run multiple GBA instances side by side, linked together over a virtual link cable, so two to four players can play multiplayer GBA games on a single machine — each player gets their own screen and their own controls."

Key takeaways from mgba-splitscreen:
1. **Primary focus is local splitscreen** — online is a bonus, not the core
2. **Use the emulator's native link cable** — mGBA's lockstep link-cable support, melonDS's Wi-Fi
3. **Per-player controls with remapping** — accessibility for all players
4. **View modes matter** — grid, speaker, focus, overlay/PiP for different setups
5. **Global save/load** — saves sync across all players
6. **Game library with box art** — smooth user experience
7. **Tauri v2 + WebSocket** — clean architecture, same frontend works in browser
8. **Keep the web client simple** — thin client, emulation on host

---

## Version History

### v0.1.0 - Initial Project Setup (September 2026)

**Status**: Documentation and project configuration

**Changes**:
- Forked melonDS repository to melonDS-Multiplayer
- Updated README.md with splitscreen-focused information
- Created AGENTS.md for AI coding agent guidance
- Created to-do.md with development roadmap (5 phases, local first)
- Created history.md (this file) with lessons from mgba-splitscreen
- Renamed PRD file to fix typo (PRD-melonDS-multiplayer.md)
- Updated BUILD.md to reference this project
- Updated CMakeLists.txt project name and description
- Updated flake.nix with new project description and version
- Updated GitHub Actions workflows with new project name
- Updated FUNDING.yml for the new project

**Technological Direction**:
- Host application: Tauri v2 (Rust + WebView)
- Emulator core: melonDS C++ code (static library)
- Display: Canvas-based rendering of multiple NDS screens
- Controls: Per-player keyboard zones + gamepad support
- Future: WebRTC for optional online play (not primary focus)

---

### v0.1.1 - Native Bridge and Headless Core Tests (September 2026)

**Status**: The emulator core runs headless, one instance per player, driven
through a C API. Nothing user-visible yet — the Rust frontend is not connected.

**What was built**:

* `src/frontend/tauri/melonds_bridge.h` / `Bridge.cpp` / `PlatformBridge.cpp` — a
  plain-C API over the melonDS core, deliberately free of Qt and SDL. Instances
  are independent objects, so one emulator per player is a matter of creating
  several. Covers: create/destroy, ROM loading with `NDS::SetupDirectBoot` for
  homebrew, BIOS/firmware overrides, reset, main-RAM peek/poke, button and touch
  input, lid, per-frame stepping, screen readback, savestates, battery saves,
  and an error/title/frame-counter diagnostic surface.
* `src/frontend/tauri/CMakeLists.txt` — builds the bridge against the core as a
  static library, reachable via `-DBUILD_TAURI_BRIDGE=ON`.
* `tools/mkdiagrom/mkdiagrom.py` — generates a diagnostic NDS ROM. No test ROM
  ships with the repo and no ARM toolchain is guaranteed to be installed, so the
  script hand-assembles an ARM9 and ARM7 program and writes valid DS headers
  around them (including the logo and header CRC16s). It also self-validates.
* `tools/coretest/smoke_test.cpp` — boots that ROM through the same bridge the
  host will use. 36 checks: both screens render, the picture advances, two
  identically-configured instances stay pixel-identical, a different player
  identity renders differently, input reaches exactly one instance, and
  savestates round-trip to the same picture when replayed.

**Decisions worth keeping**:

* **Software renderer, not OpenGL.** The host reads each instance's framebuffers
  back on the CPU every frame, so the OpenGL renderer buys nothing and costs a
  dependency. It also cannot link without the Qt frontend's glad headers, which
  is why BUILD.md pins `-DENABLE_OGLRENDERER=OFF`.
* **A diagnostic ROM instead of a borrowed one.** A hand-assembled ROM gives us
  a known pixel pattern, a known identity block the host stamps into RAM, and
  progress markers in RAM, so a failing check names the exact stage that broke.
  It also keeps us clear of ROM legality questions.
* **Assert on emulated state, not just pixels.** The test reads the button mask
  the emulated console actually saw out of its own RAM. Inferring input from
  pixels produced false passes; reading the console's owed view did not.

**Bugs this caught (all in our own code, none upstream)**:

1. **Keypad polarity.** melonDS's `NDS::SetKeyMask` takes an *active-low* mask —
   its own frontend holds `inputMask = 0xFFF` for "nothing pressed" and clears
   bits on press. The bridge was feeding it an active-high mask, so every button
   was inverted. The conversion now lives in one commented place in the bridge.
2. **The keypad's inversion mask is 10 bits, not 12.** `~raw & 0xFFF` over a
   10-bit register yields `0xC00` for "nothing pressed". That phantom value was
   read as X and Y being held, which produced a *false pass* on the X/Y check
   and sent the investigation after a non-existent emulator bug.
3. **X/Y are ARM7-only.** EXKEY (`0x04000136`) is not decoded on the ARM9 —
   melonDS matches the hardware and GBATEK here. A game that wants X/Y has to
   get them from its ARM7. The ROM does exactly that, via a mailbox in main RAM.
4. **A 32-bit load at `0x04000136` reads the wrong register.** ARM word loads are
   aligned down, so `LDR` at `0x04000136` reads `0x04000134` — the RTC counter,
   not the keypad. It has to be `LDRH`. (`rawEXKEY` was `0x7F`: bits 0-1 are the
   real X/Y bits, the high bits are melonDS's reset value for `KeyInput`.)
5. **Bitmap layers need their DISPCNT bit.** The first build rendered a blank top
   screen because the ROM enabled the wrong layer bit; on the DS, bit 8 is OBJ,
   and BG2 is bit 10. Worth remembering when debugging blank screens later.
6. **Sample the ROM at its own boundaries.** The ROM redraws its screens many
   times per second, so assertions that sampled mid-iteration were flaky and
   produced misleading failures. The test now keys off the ROM's own end-of-loop
   heartbeat counter.

---

### v0.1.2 - Local Link and Visual Verification (September 2026)

**Status**: Instances can be linked. The bus is wired up, tested and observable.

**Visual verification.** Assertions say "this changed", not "this is right", so
there is now a second pair of eyes: `md_capture` writes real frames out of the
renderer and `frames_to_html.py` builds a self-contained contact sheet. It also
samples the diagnostic ROM's button blocks and reports which buttons are lit on
which console, turning "the picture is different" into "player 1 holds A and L,
players 2 and 3 hold nothing".

That paid for itself immediately. Every console had been rendering the ROM's
*fallback* colour, so all three players had identical stripes and were only
distinguishable by their player pips - and the smoke test passed the whole time,
because it checked that the stamp survived in RAM and its pixel comparison was
satisfied by the pips alone. Two fixes came out of it:

* **A silent assembler trap.** The ROM compared its magic with `cmp(r0, 1)`, but
  `cmp` takes `(register, immediate)` - so it assembled `CMP r0, #1`, compared
  the magic against the literal 1, never matched, and quietly used the fallback.
  `cmp_reg()` now exists for the register form and `cmp()` documents the trap.
* **A test that proved the wrong thing.** The ROM now publishes the colour it
  resolved for itself, and the smoke test asserts each instance resolves what the
  host stamped - the claim we actually care about.

**The link.** melonDS's in-tree `LocalMP` is the virtual link cable, and the core
reaches it through the `Platform::MP_*` hooks with `NDS.UserData`, not through the
`MPInterface` singleton. That matters: the singleton design (used by melonDS's own
Qt frontend) has no room for several consoles on different threads, whereas
routing on `userdata` lets the bridge map each instance to its own slot. The
bridge also compiles `LocalMP.cpp` directly instead of going through
`MPInterface.cpp`, which would drag in `LAN` and therefore enet.

Things worth knowing about the bus itself:

* **Broadcast, not point-to-point.** A packet goes to every other connected slot,
  and a console never receives its own packet back. That is how local wireless
  actually behaves, and it is the right default for splitscreen.
* **Attached is not connected.** A slot assignment only says where a console
  *would* sit on the bus. A console joins when its game powers the wireless
  hardware on. Until then it receives nothing at all, because `LocalMP` only
  signals instances in its connected mask. This is why a broadcast to three
  attached-but-powered-down consoles delivers to nobody.
* **Wireless power is an ARM7 register.** `PowerControl7` (0x04000304) bit 1
  enables the hardware and `W_PowerUS` (0x04800036) releases the modem; both are
  ARM7-side. The ARM9-side register at the same address is `POWCNT9` and does not
  touch the wireless at all. Poking the wrong one is a silent no-op.

**New bridge surface**: connected-slot mask, receive timeout, per-instance
wireless begin/end counters (so "the game never started wireless" is
distinguishable from "wireless ran and found nobody"), and a non-blocking packet
send/receive used by the tests to exercise slot routing without needing a full DS
wireless stack in the diagnostic ROM.

**Known gaps — pick up here**:

* **No multiplayer link yet.** `md_link_attach` / `md_link_slot` exist and assign
  slots, but the actual local Wi-Fi bus between instances is not implemented.
  This is the single most important missing piece for the product.
* **The Rust side does not call the bridge.** The Tauri scaffolding is not wired
  to any of the C API yet.
* **The bridge has no threading story beyond a comment.** Each instance is meant
  to be driven by one thread at a time, but nothing enforces it. With the JIT
  enabled, melonDS routes memory accesses through a `thread_local NDS::Current`
  that `RunFrame` sets, so instances on *different* threads are fine, but
  interleaving two instances on one thread mid-frame is not.
* **No BIOS/firmware handling beyond overrides.** Instances currently run the
  built-in FreeBIOS, which is enough for homebrew but will not be enough for
  commercial games.
* **No real game has linked yet.** The bus is verified with synthetic packets and
  with the genuine wireless power-on path, but a real multiplayer game needs a
  ROM whose own wireless stack runs. That is the next real milestone, and it is
  where the receive timeout and frame pacing will start to matter.

### v0.1.3 - A Real Game Actually Links (September 2026)

**Status**: Two commercial ROMs play a linked multiplayer race against each other.

Mario Kart DS, two consoles, one process: both boot, discover each other over the
emulated local wireless, join the same group, pick a game, both appear on the
character-select screen, and race each other on the same course at 50cc - the
same racetrack, the same clock, and complementary placings (1st and 2nd).

That is the milestone the whole project exists for, and it needed three fixes
that only a real game could have exposed. All three were in our own code or
configuration, and all three failed *silently* - the game simply reported a
communication error.

**1. Consoles must run at the same time, not in turn.** The first harness drove
console 1's frame, then console 2's, in a loop. That is fine for the diagnostic
ROM and fatal for a link: a console that has to wait for its peer's wireless
frame can only be answered *while the peer is running*. The harness now gives
each console its own thread and meets at a per-frame barrier. It is also two to
three times faster, which is a nice side effect of not deadlocking.

**2. melonDS's blocking receive is wrong for a lockstep host.** `LocalMP` waits
up to 25ms for a peer's frame. That is right for melonDS's own frontend, which
runs one console per real-time thread, where the host really does put a beacon on
the wire a few milliseconds later. In a lockstep host it is a deadlock by
construction: the client blocks waiting for the host's beacon, the host is at the
frame barrier waiting for the client to finish its frame, and `Wifi::USTimer`
only advances its sync counter when a receive *succeeds*, so the client blocks
again on the very next tick. Measured at roughly four seconds per frame, which is
indistinguishable from a hang.

The receive timeout is now settable, and a lockstep host sets it to 0. A frame
the peer has not produced yet simply arrives next frame.

**3. ...but the host's reply collection is the opposite case.** With everything
polling, the session reached the character-select screen and then died with a
communication error. The character-select handshake needs the host to collect its
clients' replies, and it asks for them once per command slot while the clients are
genuinely racing it - so polling meant the host kept giving up on handshakes it
could have completed. A second, separate timeout now covers the reply path
(`GetReplyTimeout`), defaulting to 2ms, while the peer-frame path stays at 0. Two
milliseconds on the host is affordable because it is one wait per command slot,
and the session went from "communication error" to a running race.

With all three fixed: **two linked consoles of a 3D game at just under 3x
realtime**, one console alone at around 6x.

#### What the link carried

The counters tell a clean story, and the byte totals matching exactly is the
single most reassuring number in this project so far:

| | player 1 (host) | player 2 (client) |
| --- | --- | --- |
| frames/session | 14000 | 14000 |
| wireless on/off | 4/4 | 3/3 |
| packets sent | 36876 | 18059 |
| bytes sent | 2471216 | 550616 |
| bytes received | (see note) | 2471054 |

Player 2 received 2471054 bytes of the 2471216 player 1 sent. The 162-byte gap is
frames in flight when the session ended, and it is the clearest possible evidence
that the two consoles were talking to each other rather than merely both running.

Note the asymmetry in the received column: a host collects its clients' replies
through `LocalMP::RecvReplies`, which returns a bitmask of *who* replied and
leaves the payload in the caller's buffer, so the platform layer never learns how
many bytes arrived that way. The host's byte count is a lower bound; the sender's
`bytes_sent` is the true figure. This is documented on the `MDLinkStats` field.

#### Driving a real game's menus

The harness can press buttons and the touch screen, with an optional `@P` suffix
selecting which consoles to drive (`--touch 1000:128:157@1`). Mario Kart DS's
route to local play, as measured off captured frames rather than guessed:

1. **MULTIPLAYER** on the main menu - touch `(128, 68)`.
2. It asks to confirm your nickname, then that it may show your name during
   races, then whether to make an emblem. Yes is `(67, 145)`, roughly six seconds
   in the lobby's own time. **The touch screen is the only way through these** -
   they do not respond to the d-pad, which is what stalled the first attempts.
3. **The group list**, whose four empty slots each show their own icon. A console
   that stays here *scans*; a group appears as a cyan bar at the top, and joining
   it is a tap at `(127, 23)`.
4. **CREATE GROUP** is the button at the bottom of that same list, `(128, 157)` -
   and it is at the *same* x as the group bar, so a coordinate guessed for one
   silently presses the other.
5. **NORMAL vs SIMPLE** (`(128, 68)` / `(67, 145)`): NORMAL is multi-card, every
   console runs the ROM. SIMPLE is single-card download play. The two sit
   vertically adjacent, and picking the wrong one still gets you to *a* lobby, so
   the mistake is easy to miss.
6. The host taps **CUT OFF** (`(149, 167)`) to stop accepting racers, then
   **VS** (`(128, 22)`) on the game-select screen.
7. Character select and game settings follow, and both consoles advance together.
   Pressing A starts the race.

The order surprised us: CREATE GROUP comes *before* the NORMAL/SIMPLE choice, not
after. A first-time setup wizard also runs once and is then saved into the game's
`.sav`, so the same script behaves differently on a second run - which is worth
knowing before concluding that a menu was skipped.

Because none of that geometry is guessable, `frames_to_html.py` grew `--grid`
(a coordinate ruler on both screens) and `--mark X,Y` (a crosshair), plus `--only`
to show a handful of chosen frames. Reading a button's position off a ruled
screenshot is the difference between one run and five; two of the coordinates
above were wrong before the ruler existed, both by less than 20 pixels.

#### Smaller findings

* **A one-shot tap used to stick.** The old harness only wrote buttons on the
  frames a tap was asserted, so afterwards they stayed held forever. Each
  console's whole input state is now recomputed and written every frame.* **`--touchtap` without `--all-players` drove only console 1**, which left the
  second console sitting on a dialog while the first raced ahead. The symptom was
  wildly different "frames drawn" between consoles, which reads as a crash until
  the frames are actually looked at.
* **A screenshot is not a measurement.** Several coordinates were "read" off the
  contact sheet by eye and were off by enough to press the neighbouring button.
  The ruler (`--grid`) exists so positions come off the picture instead.

#### Known gaps — pick up here

* **The race runs; the harness does not watch it play.** The consoles were driven
  into a race and left there. Nothing yet asserts that player 1's kart and player
  2's kart stay in sync for the whole three laps, or that items and rankings
  agree - which is where a genuine desync would show up next.
* **Only one game has been tried.** New Super Mario Bros. and Super Mario 64 DS
  are in the local test set and have their own menus and their own wireless
  quirks. A second title is the real proof the link is general and not a
  Mario Kart-shaped special case.
* **Three and four consoles are untested.** Two linked consoles cost about a
  third of realtime each; four of a 3D game will not hold realtime on the current
  machine, and the per-frame barrier and the host's 2ms reply window both scale
  with player count.
* **The Rust frontend still does not call the bridge**, and instances still run
  the built-in FreeBIOS. Neither has been in the way of a real game, which is
  worth noting given how often FreeBIOS was assumed to be a blocker.
* **No frame-pacing story.** The link's timing is now driven entirely by frame
  order, which is what makes a lockstep host work, but a console left to run free
  is not slowed to 60fps and `Platform::Sleep` is unused.

---

### v0.1.4 - Three Games, and What Breaks at Three Consoles (September 2026)

**Status**: Two real games link and play, verified visually. A third console
joins, but a *second joiner* breaks the group. One title turns out to be
structurally unreachable without a firmware image.

#### The test set

Three commercial ROMs, supplied locally and **never committed** (`Test Roms/` is
ignored as a folder, plus `*.nds` / `*.sav` / `*.dsv`). Nothing here ships a ROM,
and `git log --all -- '*.nds'` is empty - worth re-checking after any change to
`.gitignore`.

#### New Super Mario Bros. — two consoles link and play

Its Vs. mode works multi-card: both consoles boot the ROM, both walk the menu,
and both end up in the same stage with the same star gauge. Route, measured off
ruled captures:

1. **Mario Vs. Luigi** on the title's bottom-screen menu - touch `(128, 95)`.
2. **Multi-card** ("Compete with another New Super Mario Bros. owner") -
   `(128, 72)`. This is the path we want; the other entry is Download Play.
3. **Character select** - `(127, 119)` picks Mario, `(127, 160)` picks Luigi.
   Give each console a different one or the game has nothing to pair.
4. Each side then asks "melonDS Mario has appeared. Play a game?" - **Yes** is
   `(128, 71)`, **No** `(128, 120)`. The peer's dialog appears a variable number
   of frames later, so a single timed tap *will* miss it. `--touchtap` repeating
   every ~150 frames is what makes this reliable.
5. **Vs. setup** - **OK** is `(128, 171)`.
6. Gameplay follows, driven with `A`.

One command that reached a running two-player round:

```bash
./build-tauri/md_game_session "path/to/New Super Mario Bros. (USA).nds" \
  --players 2 --frames 12000 --shot-dir game-captures/nsmb --shot-every 400 \
  --touch 600:128:95@1,2 --touch 1300:128:72@1,2 \
  --touch 2000:127:119@1 --touch 2000:127:160@2 \
  --touchtap 128:71:2600:150@1,2 --touchtap 128:171:4200:150@1,2 \
  --autotap A:6500:90@1,2
```

Result: both consoles in the same cave course, same `0/8` star gauge, both
racers visible on both screens, and player 2 had sent 3.25 MB over the link that
player 1 received byte for byte.

#### Super Mario 64 DS — VS mode cannot be tested as things stand

SM64DS's **VS mode is single-card Download Play only**: player 1 hosts from the
game, and players 2-4 join from the *DS main menu's* Download Play client. Two
cartridges cannot join each other, and the game says so plainly - with both
consoles in the lobby, each shows only its own `melonDS` entry, and touching
**TOUCH TO START** asks **"Practice alone?"** on both.

The evidence that this is the game and not our link:

* Both consoles' wireless powered up, both joined the bus, and each received
  **exactly** the other's 15,688 bytes of traffic. The link carried beacons.
* Neither lobby listed the other. In Mario Kart DS the equivalent screen *does*
  list a peer's group, so the difference is the game's join rule, not delivery.
* The wiki and Nintendo's own support pages agree: VS mode is host + Download
  Play clients, one card.

Why it is out of reach today, in melonDS's own words (`NDS::NeedsDirectBoot`):

```cpp
// FreeBIOS requires direct boot (it can't boot firmware)
if (!IsLoadedARM9BIOSKnownNative() || !IsLoadedARM7BIOSKnownNative())
    return true;
```

DS Download Play lives in the **firmware's menu**, and FreeBIOS cannot boot a
firmware menu at all. The bridge already has the two pieces needed to change that
(`md_set_bios`, `md_set_firmware`), so single-card support is a *user-supplied
BIOS + firmware image* away - but it is a product decision, not a bug fix, and it
is the difference between "most DS multiplayer games" and "the multi-card ones".

#### Three consoles: the join works, the *second* join does not

Performance first, because it settles the hardware question:

| consoles | MKDS throughput |
| --- | --- |
| 1 | ~6x realtime |
| 2 | ~2.4-4.9x realtime |
| 3 | ~2.9-3.4x realtime |
| 4 | ~2.2x realtime |

The dev laptop runs **four** 3D consoles at over twice realtime. Frame budget is
not the limit.

Behaviour:

* **Three consoles, one joiner**: works. The host shows `CURRENT RACERS 2/8` with
  two `melonDS` entries, the joiner shows "Awaiting other racers", and the third
  third console's group list shows the host's group as `NORMAL melonDS` with two
  racers. So the bus itself scales past two instances.
* **Three consoles, two joiners**: both joiners get **"An error has occurred."**
  Staggering the joins by ~18 emulated seconds changes nothing, so it is not a
  simultaneous-tap collision.
* The sequence is diagnostic. With one joiner, the joiner was *in* the group at
  frame 2500 (`Awaiting other racers`) and only errored after the third console
  tapped to join at 3000. The group was healthy, then the second join broke it -
  including the player who had already joined.

#### The suspect: LocalMP's host and reply routing are machine-global

`src/net/LocalMP.cpp` keeps one record of "who is the host":

```cpp
if (type == 1)          // someone sent a command
{
    MPStatus.MPHostinst = inst;      // single u16, shared by every instance
    MPStatus.MPReplyBitmask = 0;
    ReplyReadOffset[inst] = MPStatus.ReplyWriteOffset;
    Semaphore_Reset(SemPool[16 + inst]);
}
else if (type == 2)     // someone sent a reply
{
    MPStatus.MPReplyBitmask |= (1 << inst);
}
...
Semaphore_Post(SemPool[16 + MPStatus.MPHostinst]);  // reply goes to "the" host
```

Every other piece of link state is per-instance (`PacketReadOffset[]`,
`ReplyReadOffset[]`, `SemPool[16 + inst]`) - this one is not. Two consoles that
are both command senders overwrite `MPHostinst`, so the earlier one's replies get
posted to the later one's semaphore and it never wakes up. `LastHostID` has the
same shape: one `int`, set by whichever instance last received a command, then
consulted by every instance's `RecvHostPacket`.

That is a coherent explanation for the two-joiner failure and it predicts the
single-joiner success, but it has **not been proven** - the experiment that would
prove it (make the routing per-instance, re-run three and four consoles, re-run
the two-console games) has not been done. Treat it as the leading hypothesis.

#### Also fixed

* **`--all-players` was order-dependent**, so `--touch 400:128:85 --all-players`
  silently drove *player 1 only* - the flag was read at parse time, before the
touch options that depended on it. Input consoles are now resolved after every
argument has been read, and `--hold`'s end frame likewise. This cost a full
three-console run that looked exactly like a touch bug.
* **The touch path had no test at all.** `md_touch_probe` reads back what the
  guest actually sees - the pen-down bit and the two touchscreen ADC channels -
  and the smoke test now asserts the guest reads the touched pixel exactly
  (`ADC = pixel * 16`). 66 checks, 0 failures. It is what turned "SM64DS ignores
  touches" into "SM64DS ignores *that* coordinate": a tap at the title screen's
  text `(128, 135)` does nothing, while the star above it at `(128, 85)` works.
* `md_set_jit` / `md_jit_enabled` expose the JIT toggle the way melonDS's own
  frontend does, so a title that misbehaves under the JIT can be checked without
  a rebuild. (Super Mario 64 DS was *not* a JIT problem; turning it off changed
  nothing.)

#### Known gaps - pick up here

* **Fix or confirm the three-console join.** Per-instance host/reply routing in
  `LocalMP` is the first thing to try.
* **Single-card (Download Play) titles need a product decision.** SM64DS's VS
  mode, and every other single-card game, needs a user-supplied BIOS + firmware
  image so the DS menu - and its Download Play client - can boot.
* **Nothing watches a real match stay in sync.** Every result above is a
  screenshot plus link counters. A race that desyncs at lap 2 would look exactly
  like a race that works.

---

### v0.1.5 - The Tauri Host Builds for the First Time (September 2026)

**Status**: The Rust/Tauri side compiles, launches, and serves a frontend
window. The emulator core and its tests are untouched and still green (66
checks, 0 failures).

#### Where the break actually was

The stuck point everyone was feeling was **not the C++ side**. The last
incremental build (Sep 13) had already compiled the v0.1.4 touch-probe work:
the `.o` files were *newer* than the modified sources, `cmake --build` had
nothing to do, and the smoke test passed 66/0 on the first run. Checking object
timestamps against source timestamps decided this in one command - a clean
rebuild on a hunch would have cost twenty minutes and taught nothing.

The Rust side, meanwhile, **had never been built at all**: no `Cargo.lock`, no
`target/`. "Getting stuck while building" was really four separate first-build
failures, each one hiding the next. None of them was a toolchain regression;
the scaffolding had been written and never once compiled.

#### The four failures, in the order they surfaced

**1. The build script dies before any Rust is type-checked.**

`cargo build` failed inside `tauri_build::build()` with `Permission
shell:allow-open not found` followed by a wall of every valid permission name.
`src-tauri/capabilities/default.json` granted `shell:*`, `dialog:*` and `fs:*`
permissions, but none of those plugins were dependencies - and `lib.rs`
called `tauri_plugin_shell::init()` without `tauri-plugin-shell` in
`Cargo.toml` at all. Tauri validates capabilities at build time, so this fails
before `rustc` ever runs.

Fix: added `tauri-plugin-shell`, `tauri-plugin-dialog`, `tauri-plugin-fs`; the
builder now registers all three (it had only ever called shell's `init()`).

**2. `pub` commands collide with their own generated macro (E0255).**

Every `#[tauri::command] pub async fn` in `lib.rs` failed with ``the name
`__cmd__init_emulator` is defined multiple times``. The attribute emits a
hidden `__cmd__*` macro and, for a `pub` function in the crate root, exports
it crate-wide - then its own expansion tries to re-import the same name. Known
Tauri footgun, not a version issue: **this file has never compiled on any
toolchain.**

Fix: the commands are no longer `pub` (they are only reachable through
`generate_handler!` anyway), with a comment on the module explaining the trap
so it does not come back.

**3. The config named five icons that did not exist.**

`src-tauri/icons/` was an empty directory while `tauri.conf.json` listed
`32x32.png`, `128x128.png`, `128x128@2x.png`, `icon.icns`, `icon.ico`. This is
the next failure `cargo build` would have hit after #2.

Fix: `tools/make_icons.py` derives all five from `res/melon512.png` - resized
PNGs, a multi-resolution ICO, and an ICNS built from bare-PNG chunks (ic07/
ic08/ic09) - then re-opens every output with PIL as a self-check. Re-run it
after changing the source art.

**4. `frontendDist` pointed at the C++ core.**

The config pointed at `../src` (the melonDS emulator sources) and configured a
dev server on `localhost:1420` with no `package.json` anywhere to run one.

Fix: a real `frontend/index.html` - two player cards, each a 256x192 top
canvas over a 256x192 touch canvas, a status chip, placeholder "no signal"
animation - and `frontendDist: ../frontend` with the phantom dev server
removed. There is deliberately no node toolchain in this project, so the page
is one self-contained HTML file (inline CSS + JS under an explicit CSP). The
page says what it is: "waiting for the native bridge".

#### How each step was verified

* After each Rust fix: `cargo build` again, reading the whole error, not the
  first line. Failure #1's error was 300+ lines of valid-permission listing
  with the cause in the first 10.
* After the command fix: dev build green, then warnings cleared (underscore
  prefixes for stub parameters, `#![allow(non_snake_case)]` for the configured
  crate name) so the next failure stands out against silence.
* C++ regression gate: `cmake --build build-tauri` + `md_smoke_test` - 66
  checks, 0 failures.
* Runtime: dev and release binaries both ran under `timeout` without crashing;
  `RUST_LOG=info` shows the startup line (env_logger defaults to error-only,
  so a silent log with a working app is normal).
* **A screenshot of the real window**: launched the release binary, captured
  the desktop with `spectacle`, and confirmed the actual Tauri window renders
  the shell (player cards, placeholder bars mid-animation). Build, launch,
  serve, render - proven end to end, not inferred from a zero exit code.

#### Debugging notes worth keeping

* **`timeout 12 ./app | head` reports `head`'s exit code, not the app's.**
  Redirect to a file and `echo $?` separately. Under `timeout`, exit 124 means
  "ran until killed", which for a GUI app means "launched fine".
* **A stale cache looks exactly like a CSP violation.** While checking the
  frontend in a preview tab, the console reported an inline-script CSP refusal
  for a policy that had already been fixed on disk. `curl` of the served bytes
  showed the new content; `document.scripts` in the tab showed the old. Closing
  the tab and reopening fresh was the only thing that cleared it. Evidence
  hierarchy: server bytes, then DOM, then console.
* **CSP blocks inline scripts silently** - the page renders, only the console
  names the directive. With `default-src 'self'`, every `<script>` and
  `<style>` needs an explicit directive or an external file.
* **A backgrounded job inside a synchronous shell dies with its shell.** For
  anything that must outlive the command: `setsid nohup ... < /dev/null &`, and
  verify with `ss -tlnp`, not `pgrep` (transient and name-colliding pids lie).
* **`BACKGROUND` process execution is not available in this environment**;
  long-lived servers for visual checks have to be started detached as above
  and explicitly killed afterwards.

#### Known gaps - pick up here

* **The Rust side still does not call the C bridge.** Every `#[tauri::command]`
  is a stub. `init_emulator` -> `md_create`/`md_load_rom`, a frame thread ->
  `md_run_frame` per console with the per-frame barrier, and framebuffers to
  the window is the actual Phase 1/2 work and the next milestone.
* **Pick the frame channel before wiring.** `get_frames` returning a Vec per
  IPC poll will not hold 60fps for four consoles; the mgba-splitscreen answer
  was a WebSocket - decide between that and painting from the Rust side into a
  shared buffer before building on the stub shape.
* **`frontend/index.html` is one self-contained file on purpose** (single-file
  preview servers serve only the registered file). If it grows, split it only
  when something serves the directory.
* The window is 1280x720 with no view modes, no input mapping - Phase 2 per
  `to-do.md`.

---

### v0.1.6 - Two Players, Live, In the App (September 2026) — alpha 0.1.5-alpha.1

**Status**: The Tauri host drives the real emulator core end to end. Two
consoles run in lockstep at DS speed, render through the webview, and exchange
packets over the link bus inside the running app - all proven with logs,
pixel analysis and screenshots, not assumptions. Tagged `0.1.5-alpha.1`.

#### What the app does now

* `start_session(players, rom)` drives the C bridge for real: `md_create` one
  console per player, `md_link_attach` to slot N, `md_load_rom`, stamp the diag
  identity block, then one thread per console meeting at the same per-frame
  barrier the game_session harness proved. Stopping the session joins every
  thread before `md_destroy` - destroying a console mid-frame is undefined.
* **Frame channel decided**: `get_frames` returns a raw-byte IPC response
  (`tauri::ipc::Response`) - per player an 8-byte sequence number plus both
  256x192 RGBA framebuffers. Every console has a double-buffered snapshot its
  run loop refreshes, so the IPC thread never touches emulator internals and a
  read is always internally consistent. The frontend `putImageData`s only when
  the sequence moves. JSON at this rate was never viable; this is.
* **Input**: player 1 on WASD/JKL/UI/QE/Space/LShift, player 2 on arrows +
  numpad + PgUp/PgDn (hints render under each card from the session keymap).
  Touch: pointer events on the bottom canvas scale into 256x192. Input reaches
  the consoles through atomics the run loop applies every frame.
* **Link selftest, in-app**: the toolbar's "Link test" button (and the
  autostart hook) broadcasts a tagged packet both ways between consoles 1 and 2
  and checks payload plus timestamp survive. It uses the same host-side packet
  API the smoke test uses (`md_link_send_packet`/`md_link_recv_packet`) - and
  it taught the seam the hard way, see below. Result: **passed both directions,
  in the running app** (`tx 1 / rx 1, wifi on/off 1/1` on both consoles).
* **Autostart for unattended verification**: `MD_AUTOSTART_PLAYERS=2
  MD_AUTOSTART_ROM=/path/to.nds` starts a session as the window opens; a 5s
  health thread logs per-console frame counters and link stats, which is how
  pacing and lockstep were measured without touching the UI.
* **Frame pacing**: the first live runs advanced ~400 fps - fast-forward.
  The run loop now paces to the DS's 59.826 Hz (a 528-line scanout at
  315/889 MHz) after the barrier, dropping debt if a frame runs long:
  measured **299 frames / 5s = 59.8 fps**, identical counters on both consoles.

#### The linker took a whole session: three stacked failures

The Rust compiled clean; the final link failed with every `md_*` symbol
undefined even though `nm` proved the symbols inside
`libmelonds_bridge.a`. Three separate causes, each masking the next - worth
recording because none of them is guessable from the error text alone:

1. **cargo 1.95 drops `-l static=...` from the binary's link.** Comparing the
   two rustc invocations (`cargo build --verbose` and read the whole thing):
   the *library* target got the build script's `-l static=melonds_bridge/core/
   teakra`, but the *binary* - which performs the actual link - received only
   the `-L` search dirs and the dylibs re-emitted from rlib metadata. The
   static archives never reached the linker. Fix: emit the full archive paths
   via `cargo:rustc-link-arg-bins=...` so they land verbatim on the bin's
   link line, after the rlibs that reference them.
2. **rust-lld cannot consume GCC LTO objects.** rustc 1.95 defaults to its
   bundled lld; this project builds everything with GCC `-flto=auto`, whose
   object code lives in GCC IR sections. lld has no GCC LTO plugin - it
   reported the symbols undefined even with the archive on the line and even
   under `--whole-archive`, while plain `gcc` linked the same archives fine
   (the decisive A/B: same objects, two drivers, opposite outcomes). Fix:
   `cargo:rustc-link-arg=-fuse-ld=bfd`; the gcc driver's later `-fuse-ld`
   overrides rustc's, and GNU ld's LTO plugin materializes the IR
   (visible as a `.ltrans0.ltrans.o` temp file).
3. **`--as-needed` DSO ordering.** With bfd actually linking, the
   LTO-expanded core pulled `libstdc++` symbols but cargo emits `-lstdc++`
   *before* the archives; GNU ld refuses with "DSO missing from command
   line". Fix: re-emit `-lstdc++ -lm` after the archives via
   `cargo:rustc-link-arg`. A duplicate DT_NEEDED is harmless.

All three live in `src-tauri/build.rs` with comments; `build-tauri` must be
configured `-DCMAKE_BUILD_TYPE=Release` (asserted, not assumed - a debug core
mixed into a release host is a subtle way to lose a weekend).

#### The link selftest taught the wireless seam

The first selftest failed oddly: `sent=false` but the health log showed
`tx 1`. Both were honest - and the gap between them was the lesson:

* `md_link_send_packet` returns the **queued byte count** (`>0` = accepted),
  not a boolean status. The first version checked `== 0` as success.
* **A console is not on the bus until its emulated wireless hardware powers
  on.** `LocalMP::Begin` is what sets `ConnectedBitmask`, and it is reached
  from the core's callback when the ARM7 register write happens -
  `PowerControl7` (0x04000304) bit 1, then `W_PowerUS` (0x04800036). The
  smoke test does exactly this in its `wireless()` helper; the selftest now
  does too, and waits for each slot's bit to appear in
  `md_link_connected_mask` before trusting delivery.
* Those register writes are emulated state, so they are routed through a
  request flag the console's **own run loop** applies between frames - the
  host thread never pokes a running console's registers directly.
* Delivery itself is semaphore-based (`LocalMP` posts to the slots in
  `ConnectedBitmask`), which is why a console with wireless off receives
  nothing at all rather than old packets.

The selftest powers the consoles' wireless back off afterwards, leaving the
machines as it found them (the diag ROM never touches wireless itself).

#### Verification trail

* `cargo build` and `cargo build --release` both green; smoke test re-run:
  **66 checks, 0 failures** (note: it takes the ROM as `argv[1]` -
  `./build-tauri/md_smoke_test tools/mkdiagrom/diag.nds` from the repo root;
  running it bare fails with "failed to read ROM file", which is cwd, not a
  regression).
* Screenshot of the real window with both consoles showing the diag identity
  stripes (P1 red, P2 blue on the teal diag background) - pixel-counted,
  then eyeballed: ~12k red / ~12.6k blue sampled pixels.
* In-app link selftest passed both directions with payload and timestamp
  intact; health thread shows identical lockstep frame counters throughout.
* Pacing measured at 59.8 fps from the health logs after the fix.

#### Debugging notes worth keeping

* **`pkill -f <pattern>` matches your own wrapper shell** when the pattern
  appears in the shell's own command line (it killed the launch mid-command;
  the "failure" was self-inflicted). Use `pkill -x <exact name>` and `pgrep
  -x` for this binary.
* **`$OLDPWD` is not a stable project path** in these shells - it expanded to
  `/` once and the app got `ROM not found: /tools/...`. Always spell absolute
  paths out in launch scripts.
* **A fullscreen screenshot only shows the top window.** When the capture
  looks wrong, check whether the app is merely occluded before doubting the
  renderer. KWin can raise it headlessly:
  `loadScript` a one-liner that walks `workspace.windowList()` and activates
  the window whose caption contains the app name, then `spectacle -b -n -f`.
* `sleep N; grep log` inside a single tool call is fine, but launching a
  detached GUI app and waiting in the same call hits the 40s tool ceiling -
  launch with `setsid nohup ... < /dev/null &` and check the log in the
  *next* call.

#### Known gaps - pick up here

* **Keyboard/touch routing is wired but unproven with a human.** The bridge
  layer is tested (smoke test asserts the guest reads button bits and touch
  ADC), the webview sends `set_input`, but no key-injection tool exists on
  this machine (`xdotool` absent) - one human session should confirm keys
  light the diag ROM's button blocks.
* **The three-console join bug from v0.1.4 is still open** and now has a
  convenient reproduction surface: start a 3-player session in the app.
* **No gamepad support yet** (Phase 2 per to-do.md), no view modes, no
  bundled release artifacts (`cargo tauri build` untested - deb/appimage
  targets are configured but unbuilt).
* The frontend is still one self-contained HTML file on purpose; split it
  only when something serves a directory.

---

### v0.1.7 - Mesh Selftest to 4 Players, Hidden Touch Screens (September 2026)

**Status**: Input verified by a human (button presses light the diag blocks
"maybe a smig slow" - perceived latency is the new performance target). The
link selftest now exercises the **full mesh** at any player count: 2/6/12
directed pairs all pass with payload and timestamp intact. The default layout
hides the touch screens behind a Tab toggle, per the product call that most
games need only the top screen.

#### The selftest became a mesh

The two-console selftest proved one pair; scaling it exposed the assumption
to fix: **the bus is broadcast**, so every console receives every packet and
a naive N×N test reads earlier pairs' packets out of the FIFO and false-fails.
The fix is the pattern real games use anyway: send a tagged payload
(magic + from + to + nonce), then **drain until you see YOUR packet**, which
proves delivery and flushes stale traffic in one pass. Health numbers confirm
the semantics: at 4 players every console shows `tx 3 / rx 9` (3 packets sent,
3 peers × 3 packets received).

Results by player count (autostart, unattended): **2 players 2/2, 3 players
6/6, 4 players 12/12**. Scope note, unchanged from v0.1.6: this proves the
host-side bus end to end; the v0.1.4 three-console *game* join bug lives in
the emulated games' own handshake over this bus and still needs real ROMs.

#### Hidden bottom screens by default

Product decision (user-approved): the touch screens are **hidden by default**
and Tab reveals them, because most games' action lives on the top screen and
the layout gets much bigger. The frontend skips the bottom-canvas pixel copy
per frame when hidden (the IPC payload still carries both screens - the
emulator is untouched). Persistence order: the user's Tab choice
(localStorage) beats `MD_SHOW_BOTTOM=1` beats hidden. A dashed "touch screen
hidden - Tab to show" strip stands in for the hidden canvas, so discovery
doesn't depend on knowing the hotkey. First reveal repaints immediately from
the last snapshot, never black.

**Design correction (same day, user clarification, refined):** Tab as built
is a *global* reveal, but the intended feature is **per-player hold-to-peek
flip**: each player's card shows one screen at a time (top by default, big);
a per-player key HELD shows that player's bottom screen (inventory/map), and
releasing snaps back to the top. That is the layout that makes two-screen
consoles behave like classic 4-player splitscreen. The global toggle ships
as an interim stopgap only; the per-player flip is the real design and is
topped in to-do.md Phase 2. Tab will become the flip-vs-side-by-side layout
switch, and a side-by-side mode (both screens per player, always visible)
stays available as today's default layout.

**Second correction, critical:** touch input is **not** tied to visibility.
Many DS games use touch - especially gesture drags - as navigation, so touch
must always work even when a player is looking only at the top screen: the
player toggles what is *visible*, never whether touch works. melonDS's own
frontend (src/frontend/qt_sdl/Screen.cpp) is the reference for mouse touch:
press -> touchScreen(x,y), drag -> continuous pen moves (gestures), release
-> releaseScreen(), cursor with optional auto-hide. One gap: melonDS only
accepts touch where the touch screen is *displayed* (layout.GetTouchCoords),
so supporting hidden screens needs an extension - a virtual pen cursor so
players can see where they are aiming on a surface they cannot see, plus
mouse-position (and later right-stick) mapping onto the logical bottom
screen at all times. Full spec lives in to-do.md Phase 2.

Also: `.console` cards hug content (`align-items: flex-start`) instead of
stretching to full height, which the hidden-bottom layout made obvious.

#### Debugging notes worth keeping

* **A `for` loop with a `continue` needs its own closing brace** - nesting
  the mesh loops by string-edit left the old single brace in place and the
  compiler's "unclosed delimiter" pointed at the file end, not the real
  spot. Count braces when restructuring loops by edit, and re-run the build
  before the next change.
* **Preview tabs need `reuse` when navigating between HTTP and htmlPath
  sources** - a second `preview_open` silently reuses the tab and the
  screenshot can capture the previous page. Check the returned `url` against
  what you expected.
* A window on another virtual desktop defeats both `activate()` and
  fullscreen captures; don't burn cycles re-raising - the user can see it,
  and logs carry the proof.

#### Known gaps - pick up here

* **Perceived input latency** ("a smig slow"): the pipeline is keydown →
  `set_input` invoke → atomic → next frame boundary. Worth measuring, then
  candidates: apply buttons immediately instead of at frame start, or a
  local echo rule for UI feedback. Do not measure with the health thread;
  instrument the keydown→frame-applied path.
* **The v0.1.4 three-console game join bug still needs real multiplayer
  ROMs** (MKDS/NSMB) to reproduce through the app; the mesh proves the bus is
  not the culprit.
* Gamepads, view modes, `cargo tauri build` bundles - Phase 2/3 as before.

---

### v0.1.8 - Per-Player Peek Flip, Touch That Never Sleeps (September 2026)

**Status**: The two Phase 2 design corrections from v0.1.7 are now shipped
behavior. Each player's card shows ONE screen at a time - top by default,
rendered big - and each player holds their own flip key to peek at their touch
screen. Touch input never depends on what is visible.

#### What changed

* **Per-player hold-to-peek flip replaces the global Tab toggle.** Flip keys
  ship from the backend as data (`flip_keys` on SessionInfo,
  `default_flip_key` in lib.rs): P1 RShift, P2 Num3, P3 RCtrl, P4 NumDel -
  deliberately outside the button keymaps so a peek key can never shadow a
  game binding. Tab became the layout-mode switch it was always meant to be:
  flip ↔ side-by-side (both screens stacked per card), persisted in
  localStorage (`layoutMode`, migrating the old `showBottom` value).
* **Touch always works, shown or hidden.** The whole card is the pen surface
  in flip mode: pointer position maps to the logical 256x192 bottom surface
  regardless of visibility, so gestures land exactly where melonDS expects
  them. Backend contract unchanged (`set_input` touch → `md_touch`/
  `md_release_touch`).
* **Virtual pen cursor when the bottom screen is hidden**: an SVG nib overlay
  tracks the pen over the visible screen, and a white dot with a dark core is
  stamped onto the hidden canvas at every repaint while the pen is down (it
  draws over fresh game pixels each frame). When the screen is shown, the
  real cursor takes over - melonDS-style (Screen.cpp), no overlay.
* The backend keeps snapshotting both screens every frame; only the frontend
  copy is skipped, so a peek is instant on any frame.

#### Bugs caught by scripted browser verification (untrusted-event lessons)

* **`setPointerCapture` throws NotFoundError for untrusted pointer events** -
  a scripted `dispatchEvent` carries no pointer id, and the exception aborted
  the rest of the handler. Wrapped it; real mice were never affected, but now
  every entry path works.
* **Sticky peek across Tab**: the first `updatePeek` guarded both press and
  release with `layoutMode === "flip"`, so a flip key released while in
  side-by-side mode never cleared the peek - the card returned to flip mode
  already peeking. Releases must clear in every layout; only presses are
  gated.
* **Window blur cleared buttons but left peeks raised**; the blur handler now
  resets both.
* A CSS typo made side-by-side mode hide the top screen (both-screens mode
  showing one screen). Caught by asserting computed display on both canvases
  per mode.
* Distinct default flip keys per player - the first cut gave P2-P4 the same
  key, which would have had three players peeking together.

#### Verification trail

* `cargo build --release` green; release binary relaunched with
  `MD_AUTOSTART_PLAYERS=2` + diag ROM: link selftest PASSED 2/2 in both
  directions; health shows identical lockstep frame counters at ~59.8fps.
* Screenshot of the real window: one big screen per card, diag teal
  background with balanced P1-red/P2-blue stripes (flip layout confirmed by
  pixel counts).
* Browser-verified via scripted events: P1 RShift peeks only P1, P2 Num3 only
  P2; Tab flips layout both ways and persists; the pen cursor tracks
  hover/press at the right logical coordinates and clears on release; blur
  and Tab-crossing releases leave nothing stuck.

#### Real-ROM test campaign (2026-09-24, same day)

The user's `Test Roms/` (MKDS, NSMB, SM64DS - gitignored, verified with
`git check-ignore` and `git ls-files` before use) went through the current
build end to end. No code changes were needed; the findings are operational.

* **NSMB 2P link play re-confirmed on the current bridge**: the v0.1.3 route
  replayed unmodified and both consoles ended in the same cave course, same
  star gauge, both racers visible on both screens. Link signature identical
  to v0.1.3: player 2 sent 3,259,490 bytes, player 1 received 3,259,490 -
  byte-exact. 3.69x realtime at 2 consoles under the JIT.
* **MKDS runs in the app**: real Mario Kart DS booted via
  `MD_AUTOSTART_ROM`, rendering in the new flip layout, lockstep at 59.8fps,
  both consoles on the bus (`slots 0b11` in the status bar) while the game's
  demo ran.
* **Scripted MKDS lobby joins are timing-fragile; the link is not the
  problem.** Six headless attempts to walk two consoles into one group
  produced every failure mode except a joined lobby: both consoles hosting
  separate groups (mutually byte-exact beacon exchange - the bus works), the
  joiner stuck solo, the joiner in a SIMPLE (download-play) lobby it can
  never leave. Three traps, all self-inflicted by blind scripts:
  * **NORMAL vs SIMPLE is `(128,68)` vs `(67,145)` and one tap too many
    walks the joiner into its own single-card lobby** - the v0.1.3 warning,
    re-learned the hard way.
  * **The first-time setup wizard runs on a fresh `.sav` before the main
    menu**, so every fixed frame number shifts by the wizard's length - and
    the test runs themselves mutate the sav, changing the next run's boot
    flow. (The pre-test MKDS sav is preserved at
    `game-captures/mkds-sav-backup.sav`.)
  * **`--touchtap` repeats until session end**, which is what got the
    joiner through a fresh boot and also what walked it into creating its
    own group once the target moved.
  The reliable recipe is role-safe repeating taps: `(128,68)` repeats are
  correct on both the main menu (MULTIPLAYER) and mode select (NORMAL) for
  both consoles; the joiner then only repeat-taps the cyan group bar
  `(127,23)`; the host creates `(128,157)`. That run reached mutual 169,708
  byte exchange at 5.26x realtime before the joiner's continued taps carried
  it into create-group. Walking the last mile (stopping at the right moment)
  needs interactive input - which is exactly what the app's keyboard and
  touch path is for, humans driving at their own pace.
* **SM64DS status unchanged**: still single-card Download Play only, still
  blocked on a user-supplied BIOS/firmware (product decision, not a bug).

#### Human-verified MKDS lobby join, then freezes → the link watchdog (v0.1.8b)

**The user drove the app themselves and got two MKDS consoles into a group -
the first human link-play milestone - but hit repeated freezes.** The current
session's log told the story in its health lines:

* Emulators stayed healthy through every freeze: frame counters advanced
  lockstep at ~59.8fps the whole time, and both consoles' wireless stayed
  powered (wifi 3/3). The freezes are the GAME's WiFi state machine stalling,
  not the host, the barrier, or the pacing.
* At the stall (17:09:49), both consoles' tx/rx went flat simultaneously
  while frames kept counting - the game-level signature of "waiting for a
  peer frame that never satisfies it".
* The same log shows a suspicious pathology: the client's rx counter climbed
  20k-48k packets per 5s window while the host's tx climbed only ~1-2k, and
  one window showed ~8.2k packets received while every console's tx sat at
  zero. Either the bus duplicated deliveries or a counter lies; the aggregate
  counters cannot distinguish those. mgba-splitscreen's counter-anomaly hunts
  ("7,341 FEFE probes at ~120/s") read the same way.

**Answer: make the log able to attribute a freeze by itself.** The bridge now
keeps per-kind packet counters (LinkStats v2: rx broadcast/cmd/reply/ack,
tx cmd/reply/ack, receive polls and empty polls, last-packet age) and logs a
timestamped `[link-event]` line on every wireless power transition and bus
timeout change; the app's health thread classifies each console per 5s window
as `LINK ACTIVE` (traffic flowing), `LINK STALL` (wireless on, polls running,
nothing accepted - the game is waiting and nobody is answering), `LINK IDLE`
(polls stopped: WiFi not even asking), or `LINK OFF`. `MD_LINK_VERBOSE=1`
adds per-kind deltas per window. With that, the next freeze prints its own
diagnosis: STALL with cmd flowing but no replies = reply routing; STALL with
no polls = the game gave up; ACTIVE with a flood = duplication.

**MITM design note (next step, informed by mgba-splitscreen).** Their hard-
won rules transfer directly: assist echoes and repeats help during DISCOVERY
(when each side waits to see the other) but are actively harmful once the
handshake progresses ("the echo makes the games reset phase 1<->2 instead of
parking"); resets must drop queues, not just restart state. Our manager
should therefore be stall-triggered and kind-aware: on LINK STALL, replay the
last cmd/host frame a bounded number of times (echo) to re-trigger the
game's retry, never echoing during steady ACTIVE traffic; on wireless
toggle (link-event), clear per-instance read offsets so a rejoining console
cannot read stale FIFO bytes. Instrument first, then echo only what a STALL
window proves is missing.

#### The freezes attributed, and the channel fix (v0.1.8c)

The next human session froze twice - at game-mode selection, then at kart
selection - and this time the watchdog caught both in the act:

* **Freeze 1 (game mode)**: p1 went `LINK STALL` for 10s - polls running,
  nothing accepted - and the new auto-kick resynced its bus state without
  human help. Traffic flowed again within 5s and stayed `LINK ACTIVE` for
  23 straight minutes with byte-complementary counters (p1 tx 32,738 ↔ p2 rx
  32,693). The user reached menus and races they had never gotten to before.
* **Freeze 2 (kart selection)**: different signature - the game powered its
  wireless OFF cleanly after a burst of `bad channel 1 (expected 7)`. The
  host gave up waiting and dropped to its offline path; the UI kept showing
  a lobby the game had already abandoned.
* **Root cause, confirmed across both freezes**: the consoles independently
  hop RF channels (the log shows 1, 6, 7, 13 in one session). Real DS
  hardware drops frames from other channels (`Wifi::CheckRX` channel check),
  so whenever the peer's channel differed, its frames vanished - the host
  waiting for joiners could not hear scans, the client could not hear the
  host's group. mkba-splitscreen's "handshake desync" and our intermittent
  NSMB-success/MKDS-failure pattern both reduce to this.
* **The fix: channel-transparent virtual air.** The bus physically cannot
  have channels (one machine = one radio space), so received frames are now
  restamped with the *receiver's* current channel before the game sees them
  (`Wifi::CheckRX` and `MPClientReplyRX`). Every console then hears every
  frame as if tuned correctly - which is what one shared radio means for the
  games. Channel-mismatch drops become impossible by construction.
* **Observability that made this solvable**: per-kind packet counters,
  `[link-event]` power transitions, STALL/ACTIVE classification, and
  per-console log tags (`[melonDS p1:...]`) - the interleaved wifi logs were
  unaatributable before the tags, which is why earlier sessions looked like
  noise.
* Operational note: the watchdog + auto-resync pair already fixed one live
  freeze unattended; the channel fix removes the largest remaining trigger.

#### Known gaps - pick up here

* **Verify a full lobby + race on the channel-transparent build** (human
  session): freeze 2's trigger should be gone; anything remaining will now
  show a STALL/trace signature without channel noise hiding it.
* **Gamepad support**: right-stick cursor + trigger pen-down is specced in
  to-do.md Phase 2 but not built; flip keys have no gamepad path yet.
* Peek keys are not remappable yet (they ship as data; the remap UI is the
  same Phase 2 item).
* No xdotool on this machine, so the desktop app's keyboard path (incl. flip
  keys) is verified by bridge-level tests and human session only; the
  frontend logic was verified by scripted browser events.
* The three-console join bug and the perceived-latency item remain open
  (Phase 1 todo).

---

### v0.1.8d - Crash forensics, driver-thread link ops, a lockstep that cannot hang (2026-09-24)

**Trigger**: three sessions in a row ended with the app *gone* and nothing in the
log - no panic, no kernel line (`dmesg` is restricted on this machine), no
coredump (`coredumpctl`: none, because systemd-coredump's size limit is smaller
than a JIT'd pair of consoles). The last one died 15 minutes in with the log
healthy to the final line: both consoles at 59.8fps, in lockstep, wireless off,
nothing suspicious. Guessing was no longer a strategy.

#### Here is what the instrumentation found

**1. melonDS's own ARM JIT owns SIGSEGV, and fighting it for the signal kills
the app.** The first version of this work installed handlers for
SEGV/BUS/ILL/FPE/ABRT/TRAP and then *re-asserted them on the 30s heartbeat* to
survive anything that replaced them. That "repair" was the bug: `ARMJIT_Memory`
installs `SigsegvHandler` while a console starts, because the JIT's fast-memory
path relies on faults - the guest touching a lazily-mapped region faults, the
JIT remaps it and resumes (it saves the previous handler in `OldSaSegv` and
chains anything it cannot handle). Re-installing ours on top took those faults
away from the JIT, so the emulator's *normal* lazy-map traffic became an exit:
**the app died 30 seconds into every session, exactly at heartbeat #1**, three
runs in a row. Now the rule is: install only where the disposition is still
SIG_DFL, never overwrite an owner (log `signal 11 is already owned by another
component (handler at 0x...) - left alone on purpose`), and on the heartbeat
audit the ownership rather than claim it back.
* Practical consequence worth knowing: the JIT chains unhandled faults to
  whoever owned the signal when it registered - Rust's std stack-overflow
  handler, which was installed before `main` - not to us. A *genuine* SIGSEGV
  therefore ends the process without one of our reports. That gap is why
  `tools/run_app.sh` now runs the app under a supervisor that records the exit
  status: `[supervisor] app pid N exited with status 139 (killed by signal 11)`
  is a segfault, `status 0` is a clean close, and no line at all means it was
  still running.

**2. The crash path is now proven end to end.** `kill -ABRT <pid>` (and SIGSEGV,
while our handler owned it) on a live session produced `/tmp/md_crash_<pid>.txt`
plus the same text in the session log with a native backtrace, and `-rdynamic`
(added to `build.rs`) resolved symbols - shared-library frames by name
(`libwebkit2gtk`, `libgtk-3`, `libc`) and core frames like
`melonDS::ARMJIT_Global::CodeMemory`, which is exactly the region a
self-modifying-code fault would land in. Rust-internal frames print as
`+0xoffset`; the file header says how to symbolize them
(`addr2line -e <binary> 0x...`).

**3. Every cross-thread link call was a bug waiting to happen.** `LocalMP`'s
packet FIFO is read by the console's *own* thread from inside `md_run_frame`
(`Wifi::CheckRX` → `MP_RecvPacket`). The health watchdog's resync kick, the link
monitor's probes and the user-facing selftest all reached into that same bus from
*other* threads. Reads are not the problem; draining is: `md_link_recv_packet`
advances `PacketReadOffset[slot]`, so a probe run from outside steals the frames
the game was about to read and perturbs the read offset its receive path is
tracking. That is the mechanism behind "clicking Link test unsticks the lobby"
(the kick is real) *and* "clicking Link test breaks things" (the drain is a
robbery). All of it now goes through `LinkOp`s - a small per-console queue that
the driver thread services between frames - so the only thread that ever touches
a console's bus state is the thread that owns it.

**4. `md_link_recv_packet` was declared `-> usize` in Rust while the bridge
declares `int`.** The upper half of the return register is undefined in that
case, so every "did my packet arrive" comparison was a coin toss. Sends looked
reliable and receives looked flaky - exactly backwards from the truth. Fixed to
`i32` (the send side was always correct, which is why nobody noticed).

**5. The bus object was freed while other threads could still hold its
pointer.** `md::link_bus()` returns the raw pointer *after* releasing the lock
that guards it, and the loader's `MP_*` hooks use it without holding that lock.
`md_link_shutdown()` used to `reset()` it. The bus now outlives every session and
is soft-reset instead (`LocalMP::Reset()`: status, read offsets, queues and
semaphores back to zero), so no thread can ever use a freed bus.

**6. A stopped console used to hang the whole app.** `std::sync::Barrier` has no
timeout: when one console left its run loop (power-off, emulation error, a
thread ending), the survivors waited at the barrier *forever* while the frontend
kept repainting the last frame - the exact "the emulator froze and stopped
responding" report. The lockstep is now a condvar with a 50ms timeout that
checks a live-console count and dissolves loudly
(`p2: a peer console stopped - lockstep dissolved`), and the UI says
"console stopped ... press Stop then Start" instead of showing a dead picture.

#### The always-on link check, as asked for

The 10s link monitor no longer injects packets by default. It classifies each
window: real game traffic crossing the bus = **verified**, a console polling hard
while accepting nothing = **stalled** (the health watchdog resyncs it). The
window counters show up in `link_status` and in the status bar
(`link check ok 12 · resyncs 1 (last 3s ago)`), so "is the link being tested?"
is now visible instead of a button you press. Packet injection still exists
behind `MD_LINK_PROBE=1` for diagnostics, through the driver threads like
everything else.

#### Liveness, so a failure is classifiable

* `heartbeat #N: frames [..] (+.. per 30s) · consoles 2/2 · kicks N · rss NMB ·
  threads N · ui Ns ago` every 30s. Frames still advancing + a quiet window = a
  webview problem; frames frozen = lockstep/emulation; the log simply stopping
  mid-line = a native fault.
* The frontend calls `ui_heartbeat` every 5s and forwards uncaught errors
  (`frontend_log`), so a webview that dies is visible from the host log.
* Window lifecycle (`close requested`, `destroyed`, `exit requested`, `event
  loop exiting`) is logged: a clean close now leaves a trace, which the earlier
  silent deaths did not.
* `session teardown: ...` tells "stopped after joining console threads" apart
  from "never even got the stop signal".

#### Evidence collected along the way

* **A 16.7-emulated-minute, two-console NSMB linked soak finished clean**: 60000
  frames per console, 2.41x realtime, `all consoles kept running`, both slots on
  the bus, 4694 packets one way and 17225 the other, zero FIFO overflows and -
  notably - zero `bad channel` lines on the channel-transparent build.
* The in-app selftest now passes both directions **through the driver threads**
  (`payloadOk: true` both ways), which is also the regression test for the
  `LinkOp` routing.
* The frontend heartbeat reads `ui 3-4s ago` on a healthy window.

#### Known gaps - pick up here

* **No spontaneous native fault has been captured yet.** The handlers are armed
  and verified, so the next one writes a report; the `[11] stolen` finding means
  every earlier crash may have been swallowed rather than absent.
* The MKDS freeze that survived the channel fix (the kart-selection one) has not
  been re-tested on this build.
* **MKDS room discovery has still not been watched end to end.** The consoles
  now present distinct MACs and the bus can narrate every frame, but the next
  MKDS attempt is what decides it: 
  the `[link-trace]` lines will show either a joiner that never replies (game
  side) or a reply the host never collects (bridge side).
* The three-console join bug and the perceived-latency item remain open
  (Phase 1 todo).
* SIGSEGV/SIGBUS are only ours on a build where nothing else owns them. Once
  a session runs they belong to the ARM JIT (fastmem) and unhandled faults
  chain to Rust's std handler, so a genuine SIGSEGV ends the process without
  our report: the supervisor's exit-status line is the identifier. Making our
  report cover that case means being the handler the JIT saves, i.e. installing
  before `main` - a C-level shim in the bridge, not a Rust-side install.

---

### v0.1.8e - Console identity, and a virtual air that narrates itself (2026-09-25)

**Trigger**: the user's report - *"froze up trying to get p2 to find and join the
second session"* in MKDS - and nothing left running to inspect, because the
process was gone. The previous run's log survived, so the report could be read
back frame by frame.

#### What the log actually says

**Nothing froze.** Over the whole 7-minute session both consoles advanced at
59.8fps (`+299` frames per 5s window, in lockstep, to the last line), the UI
heartbeat stayed at 3-4s, `consoles 2/2`, RSS flat at 517MB, and the process
ended with `window 'main': close requested` → `event loop exiting` → exit status
0: the user closed the window after the join did not work. **A report of "froze"
was a report of "the lobby never filled", and the emulator was healthy
throughout.** Worth remembering before the next crash hunt.

**The games barely spoke.** MKDS powers its radio on for a lobby twice in that
run (`begin #3` at +397.2s, `begin #4` at +413.0s). Each cycle, one console
came up on channel 1, the other followed onto channel 1, and then the whole
thing unwound inside a second: one console powered its radio fully off, the
other hopped 1 → 7 → 13 → 1 with `TRX power` toggling, and the session ended.
Cumulative traffic for the *entire run*: `tx 3 · rx 3` frames. In one cycle the
counters were unambiguous - **player1 sent a single frame, player2 received a
single frame, and player2 never answered.** A joiner that hears a room and does
not answer it is a joiner that did not recognize the room as somebody else's.

#### The defect: every console was the same console

The bridge never gave the instances distinct identities, so all of them booted
the same generated firmware and presented the same MAC - proven by the boot log
printing `MAC: 00:09:BF:11:22:33` twice, i.e. the firmware's default
(`SPI_Firmware.h: DEFAULT_MAC`). A DS game identifies the consoles it is talking
to by MAC: the host's beacons carry it, the joiner addresses its replies to it,
and the room list is built out of it. A joiner whose own MAC equals the host's
cannot distinguish the host's group from its own console - which is exactly
"P2 can't find the group". melonDS's own frontend already solves this and we had
simply not copied it: `EmuInstance::customizeFirmware` offsets the MAC of every
instance past the first (`mac[3] += id; mac[4] += id*0x44; mac[5] += id*0x10`).

The bridge now does the same (`apply_console_mac`, called from `md_load_rom`
before `Reset()`, because that is when the guest reads its MAC out of the
firmware over SPI and programs it into the wireless hardware). Slot 0 keeps the
firmware MAC; the rest get distinct low bytes. The base MAC is cached in the
instance and the offset re-derived from it on every boot, so a second ROM load
cannot walk the address further. Verified in the smoke test and in a real NSMB
session - the frames now show each console's own identity: p2's beacons go out
`tx=0009BF126643 bssid=0009BF126643`, and p1 answers `rx=0009BF126643
tx=0009BF112233`.

#### The instrument: `MD_LINK_VERBOSE=1` now traces frames

The counters could say *that* discovery stalled but not *why*. Every frame the
bus carries is now narrated from both ends - frame control, length, the frame's
channel, all three address fields, and the MP sync/client-mask block:

```
[link-trace] p2 tx bcast len=122 chan=13 fc=0080 rx=FFFFFFFFFFFF tx=0009BF126643 bssid=0009BF126643 sync=... mask=0003
[link-trace] p1 rx cmd   len= 46 chan=13 fc=0228 rx=0309BF000000 tx=0009BF126643 bssid=0009BF126643 sync=... mask=0002
[link-trace] p1 tx reply len= 42 chan=13 fc=0118 rx=0009BF126643 tx=0009BF112233 bssid=0309BF000010
```

That is the whole discovery handshake in three lines - beacon from the host's
MAC, command from the fixed MP command MAC, reply addressed back to the host -
and it is what the next MKDS attempt will be judged on: either the joiner never
sends a reply (game-side identity problem) or it does and the host never pulls
it off the bus (bridge-side). 5326 such lines in a 9000-frame two-console NSMB
run, with 0 smoke-test regressions (66/0).

#### Housekeeping

* The health line's `wifi 4/4` was reading as "four of four consoles" when it
  meant `MP_Begin`/`MP_End` counts; it now prints `wifi on/off 4/4`.
* Diagnostics for this report came from `/tmp/md_app.log` while the app was
  gone, and the fix was validated without it: harness sessions run on **copies
  of the ROM and sav in `/tmp`**, so a scripted run can no longer mutate the
  user's saves (the old advice was "back them up first").

#### Second report, same day: "p1 hosted, p2 can't see it; I had to hit Link test

or p1 hung" - and this one points at our own tooling.**

The log from the relaunched app is small enough to account for every packet in
it, and the accounting is uncomfortable:

* **No console ever hosted anything the emulator could see.** Five wireless
  sessions in 4.5 minutes: `#1` 47ms (the game's own boot probe), `#2` 84ms,
  then three at 67ms / 468ms / 468ms. At every 5s health sample the radios were
  off (`LINK OFF`, 128 times, zero `LINK ACTIVE`). A game waiting for players
  holds its radio up for as long as the screen is up; nothing here lasted half
  a second.
* **Not one frame in the run came from a game.** Every send was a 16-byte
  `MST0` test packet, and the two `[link-trace]` lines are the joiner's WiFi
  reading a test frame (`len=16 chan=0` - a game frame is never smaller than
  36 bytes). `polls 1491 (empty 1491)` says MKDS's WiFi polled 1491 times and
  accepted nothing. Whatever the game was doing, it exchanged nothing.
* **The test frames were lying in wait.** The receiving console's WiFi read one
  of them *inside* a lobby attempt (`p2 rx` between `begin #4` and its channel
  hops), minutes after the selftest that sent it. `link_selftest_inner` drained
  only the counterpart console, so leftovers stayed queued where the next thing
  to poll would find them - and what polls next is the game.
* **The button used to leave no trace.** The autostart path logged its selftest
  result; the `link_selftest` command did not. So the three 468ms cycles above
  (400ms drain timeout + overhead - the selftest's own signature) looked exactly
  like a game's lobby behaviour, and the log gave the user's own clicks zero
  attribution. That is what made this log take an hour to read.

The mechanism that fits every symptom, including "otherwise p1 hung": the
selftest walks a live game's **hardware**. It writes ARM7 `0x04000304` /
`0x04800036` to force the radio on, so the game's WiFi driver is brought up
under it, then powers it back down, then leaves test frames in a bus FIFO the
game reads. A DS WiFi driver that is mid-bring-up when its power register moves
under it has every reason to sit waiting for an interrupt that no longer comes -
which is a hung lobby - and pressing the button again is what moves the state
on, so the cure and the cause were the same button.

#### Third attempt, with nothing of ours on the bus: the join gets all the way

and then the watchdog interrupts it.**

With the selftest out of the way the picture changes completely, and it is the
best log this project has produced:

* **P1 hosts for real.** `p1 tx bcast len=120 fc=0080` every ~130ms on channel 1,
  `tx=0009BF112233 bssid=0009BF112233` - the per-console MAC reaching actual game
  frames - hundreds of beacons over minutes.
* **P2 hears it and answers.** `p2 rx bcast ... tx=0009BF112233` (603 frames),
  then an auth request `p2 tx bcast fc=00B0`, P1's auth response, an association
  request, and an association response carrying `[HOST] syncing client C001`
  and `[CLIENT 1] host sync=...`. `LINK ACTIVE` appears in the health log for
  the first time.
* **Then the client deauthenticates** 1.8-2s in: `p2 tx bcast fc=00C0` →
  `[CLIENT] deauth`, twice, identically. That is the error the user sees.
* **The reply-window instrumentation came back clean**: no reply dropped for
  staleness, no empty collection window. The hypothesis that LocalMP's 32-tick
  staleness rule was eating replies is dead - the bus delivers, the host collects.

What the timeline did show, on both attempts, is our own watchdog kicking a
console **in the middle of the handshake**:

```
+304.354s  p1 (player2): wireless ON (begin #2, slots now 0x3)   <- join starts
+305.804s  p1 stalled with peers on the bus - resynced its bus state (kick #1)
+305.9xx   [HOST] syncing client C001
next 6 acks  clientfail=0002  ("client 1 did not reply in time")
+306.1s    [CLIENT] deauth
```

A kick is `LocalMP::Begin`: it sets the console's packet and reply read offsets
to the current write offsets - **discarding everything queued for it** - and
clears both of its semaphores. On a host that is 1.45s into waiting for a client
to associate, that throws away the very replies it then reports as missing. A
host waiting for a new client is indistinguishable, window for window, from the
drifted-FIFO stall the kick was built to cure; only the *duration* tells them
apart, and the historic cure came after ~10s of continuous STALL while a join is
over in under 2.

So the first kick now needs `KICK_AFTER_WINDOWS = 2` consecutive stalled windows
(10s), with the withheld ones logged in verbose mode so a later failure can never
be blamed on a kick that did not happen. Whether that is the whole story is the
next experiment: if the join still deauthenticates at the same point with no kick
in the log, the cause is the DS-level MP timing itself.

Two reply-window diagnostics were added to `LocalMP::RecvReplies` under
`MD_LINK_VERBOSE=1` for exactly this question - a reply dropped as stale prints
the age that missed the 32-tick window, and an empty collection prints its
collected mask - because "the client answered and the host could not use it" and
"the client never answered" are different bugs that look the same in a counter.

#### Fourth attempt: the reply window was the bug, and now it has numbers

With the kick held back, the join still deauthenticated at the same point
(`kick # 0` in the log, so the watchdog is exonerated) - and the diagnostics
written for the previous attempt answered it immediately:

```
reply window: DROPPED a reply for being   64 ... 4520 US-timer ticks stale (sender p2, window 32)
```

33 replies dropped, 63 collection rounds empty. The ticks are **microseconds**
(a DS frame costs `framelen * 8` ticks at 1Mbps and `* 4` at 2Mbps - see
`Wifi::SendMPFrame`), so `LocalMP::RecvReplies` was discarding client replies
that were 64-4520us behind the host's clock, with a 32us window. The ages form a
sawtooth (4520 -> 3512 -> 2496 -> 1488 -> 480), which is the signature of a fixed
phase offset rather than loss. The host then set `clientfail=0002` in its acks -
"client 1 did not reply" - and MKDS's client deauthenticated 1.8s into a join
whose every frame had in fact been delivered.

That 32us window is correct for hardware: there the client's US clock is slaved
to the host's by the sync frames it receives, so replies genuinely belong to the
round being collected. Two consoles in one process are only locked to the same
*frame*, and their US timers sit at whatever phase within it they reached, so a
perfectly good reply is routinely a few hundred microseconds "old". The window
is now `kReplyStaleWindow = 16715` - one emulated frame - and the honest argument
for it is that both consoles are within a frame of each other by construction,
while anything genuinely older is already discarded by the real mechanism:
sending a command re-anchors the host's reply read offset (`SendPacketGeneric`,
type 1), which is what throws away the previous round's replies.

This is the first failure in this project that was *measured* before it was
fixed: the counter said replies existed, the skip path said they were being
discarded, the clock domain said why, and the drop ages said how wide the window
had to be.

---

Three changes, in decreasing order of how much they matter:

1. **"Link test" never injects into a live game.** If any console has its
   wireless on, the command now performs the safe half - the resync kick (FIFO
   read offset re-anchored, semaphores cleared) that is what actually cured hung
   lobbies - and says so in the status bar. Injection is reserved for consoles
   nobody is playing. The frontend renders both shapes.
2. **The autostart selftest is opt-in** (`MD_AUTOSTART_SELFTEST=1`). MKDS brings
   its radio up 0.7s into a session, so a diagnostic that fires unconditionally
   at 3s lands on top of the game's own wireless bring-up every single time.
3. **An injected packet cannot outlive its test.** After the round trip every
   console is drained and its read offset re-anchored, so nothing of ours is
   left on the bus for a game to find later. The command logs its result, so a
   click is never anonymous in the log again.

#### Fifth attempt: the window was the right size but the wrong clock

The user hit the error state a third time, identically. This time the stale-drop
counter was silent (`zero DROPPED a reply`) - the widened window was working -
but the collection rounds were still coming back empty: **123 collections, 71
with a reply, 52 empty**, against 117 replies the client demonstrably sent. So
the replies were neither lost nor discarded: **the host was giving up before
they could arrive.**

The number that was wrong was never the staleness window at all. It was
`md_link_set_reply_timeout(2)`, and it was wrong in a way the staleness reasoning
made obvious only in hindsight: it is measured in **wall-clock milliseconds**
while the protocol it stands in for is measured in **emulated** time. At the
measured 2.4x emulation speed, 2ms of wall clock is **0.8ms of emulated time** -
less than the 1-2ms DS slot window it is imitating, and far short of the client's
next frame boundary, which is where its answer actually goes out. A perfectly
synchronised client cannot reply inside a window that closes first.

`Semaphore_TryWait(SemPool[16+inst], ReplyTimeout)` in `LocalMP::RecvReplies`
uses that budget for exactly one question - is a reply waiting right now - and
returning "no" makes the host tell the game `clientfail=0002`, which makes the
game drop the client. The bound is now **34ms (two emulated frames)**, exposed as
`MD_REPLY_TIMEOUT_MS` so it can be swept without a rebuild, and the wait still
returns the instant a reply is posted, so a healthy session pays nothing for the
larger ceiling.

**Regression check, instrumented:** the risk of a longer wait is that a missing
reply stretches the frame and the emulator goes slow-motion. The harness settles
it - `6000 frames x 2 console(s) in 40.5s -> 100.3 emulated seconds, 2.48x
realtime`, against a 2.41x baseline for the same ROM at 2ms. Frame pacing is
unchanged, because the bound is only ever spent when a reply never comes.

**What is proven and what is not.** Pacing: proven. That the host now collects
replies it previously abandoned: not yet - it needs a live MKDS join, because the
harness's scripted route into the game's multiplayer menus is fragile enough that
the honest test is a human one. The distinguishing line in the next log is
`reply window closed for p1 with collected=0000`: if those collapse while the
client's replies stay at ~117, the host was the one hanging up.---

### v0.1.8g - The join now fails at the client, and NSMB says what normal looks like (2026-09-25)

**Trigger**: the user's report - player 2 can see the host's group, tries to join,
gets an error, and the game drops back to "seeking opponents". The previous run's
log was still on disk (`/tmp/md_app.log`, preserved as
`/tmp/md_mkds_join_2026-09-25.log`) and this time it contains an MP session that
got much further than any before it.

#### What the run shows

The whole handshake now happens: P2 scans channels 1/7/13, finds the room, sends
auth (`fc=00B0`) and association (`fc=0000`), the host answers with
`[HOST] syncing client C001`, the client logs `[CLIENT 1] host sync=`, and then
the DS's real multiplayer exchange runs - host CMD frames (`fc=0228`) carrying
`clienttime 478 / mask 0002`, the client's replies (`fc=0118`), the host's acks
(`fc=0218` with the `clientfail` word). Then P2 deauthenticates (`fc=00C0` +
`[CLIENT] deauth`) 2.0s after switching its radio on, which is the error the user
sees.

Counted from both ends and matched up:

* P1 sent **11** CMD frames while P2's radio was on; P2's wireless hardware pulled
  **9** of them off the bus. The missing two are the ones sent *after* P2's radio
  went off. **Nothing was lost in the bus.**
* P2 answered **3** of those 9: two real replies and, at the end, the firmware's
  default empty reply (`fc=0158`) that goes out when the game has not armed one.
  The other six produced no frame at all.
* The host's acks say exactly that: the first six carry `clientfail=0002` ("client
  1 did not reply"), and the three after the client started answering carry
  `clientfail=0000`. The reply window closed empty **once**, as P2 was already
  leaving.
* Zero `DROPPED a reply`, zero `bad channel`, zero `bad frame length`, zero
  resyncs, zero kicks. The v0.1.8f changes held: the host no longer abandons
  replies it has, and the wire is clean.

**So the failure moved.** It is no longer "the host hangs up on a client that
answered" - it is "the client answers three times and then quits". Six of the
nine commands it pulled off the bus never produced a reply frame, and the
bus trace cannot say why, because those six were dropped *inside* the emulated
wireless hardware (receive filters, BSSID, duplicate sequence numbers) where
nothing was being logged.

#### The comparison that makes the numbers readable

The documented NSMB scripted run reaches a live two-player round on this same
bridge, so it is the reference for what a *working* MP session looks like with
this tracing on:

```
[mp] answering cmd seq=00F0: AID 1, host mask 0002, clienttime 478, dupe 1   x1078
reply window: p2 collected a reply from p1 for aid 1, 42 bytes, fc=0118 ...  x638
reply window closed for p2 with collected=0000                               x828
[link-trace] p2 tx ack ... sync=000000000002056E mask=0002
```

That game plays fine - both consoles in the same course, byte-for-byte link
traffic - while the host's acks routinely report `clientfail=0002` and a third of
its collection rounds come back empty. Two conclusions follow, and they change
what to look for:

1. **A partial reply-collection rate is normal in this emulation, and a working
game tolerates it.** `clientfail=0002` on its own is therefore *not* why MKDS
errors out.
2. **What is different about MKDS is that its client stops answering.** NSMB's
client answered every one of 1078 commands; MKDS's answered 3 of 9.

Two systemic properties showed up in that same reference run:

* **The client's clock runs about 15ms ahead of the host's frame timestamps**
  (`[mp] cmd from host: host clock 2894928, my clock 2910360 (lead -15432)`). On
  hardware the client's US clock is slaved to the host's by the sync frames it
  receives; here it is only *seeded* once, at association, and then free-runs
  with its own emulation. Because it is ahead, `CheckRX`'s
  `if (RXTimestamp < USTimestamp) RXTimestamp = USTimestamp` clamp discards the
  host's timing completely, and every MP frame the client receives carries a
timestamp its own clock has already passed. NSMB does not care. MKDS's join,
  which is a strict request/response state machine, may.
* **Every CMD frame after the first is a hardware duplicate.** The host reuses one
  sequence number (`seq=00F0` for the entire NSMB session) and melonDS mirrors
  real hardware by answering duplicates while suppressing IRQ0 for them - so the
  guest is never told those commands arrived, even though the hardware replies.
  A lobby waiting for its next command would sit still while the wire looks busy.

#### What was shipped: the decision points, named

Six silent drops out of nine frames is not diagnosable from the outside, so the
core now narrates every decision that can swallow or answer an MP command frame,
all behind `MD_LINK_VERBOSE=1`:

* `[mp] cmd from host: host clock ... my clock ... (lead ...) window ... mask ...`
  - both clocks, once per command received, which is the clock-domain measurement
  the reply-window work needed and did not have.
* `[mp] answering cmd seq=... AID ... host mask ... dupe ...` and
  `[mp] BLANK reply to cmd seq=...` - what the client decided to send, and the
  discovery that a blank is a *zero length* frame: the host reads it as a packet
  but not as a reply, so it keeps reporting that client as failed.
* `[mp] cmd seq=... dropped before any handling: RXFilter2 kills from/to=...`,
  `... BSSID is not ours ... and RXFilter bit 11 is clear`, `... retransmit and
  RXFilter bit 0 is clear` - the three places `Wifi::FinishRX` discards a command
  frame, each naming its filter register so a drop can be tied to a game's
  bring-up order instead of guessed at.
* `[mp] cmd seq=... repeats the previous one: answered by the hardware, but the
  game is not notified` - the duplicate case above, named for what it is.
* `reply window: not even looking for p1's replies - the bus has no other console
  attached (asked for mask 0002, connected 0001)` - the quiet way a host can be
  told "no replies" without a single queue read. If this line appears while a
  game is hosting, the bus's connected mask and the session have come apart, and
  the reply queue is the wrong place to look.
* `reply window: p1 collected a reply from p2 for aid 1, 42 bytes, fc=0118,
  payload=...` - the collected bytes on the host side, so they can be compared
  with the client's own `tx reply` line: if the two disagree the transport
  mangled the reply, and if they agree the games are the ones disagreeing.
* `[link-trace]` gained `seq=` (the wireless sequence control word) and `t=` (the
  emulated timestamp the frame carries), and now prints zero-length frames as
  `(empty frame)` - a blank reply used to be invisible in a trace that only
  printed whole frames.

**Verification**: the smoke test is 66/0 after every one of these changes, and
the NSMB reference run above was re-run with all of it on - 1078 commands, 1078
replies, no crashes, no drops, 2.93x realtime - so the new logs are trustworthy
on a session that works before being trusted on one that does not.

**What is proven and what is not.** Proven: the handshake reaches a full
cmd/reply/ack exchange, the wire is clean, and the host credits the replies it
receives. Not proven: *why* MKDS's client answers only three commands. The build
with this instrumentation is running; one interactive join writes the answer into
the `[mp]` lines, and the fix that follows should target the client's clock
slaving (the 15ms lead) or its reply scheduling - not the link, which is clean.

---

### v0.1.8h - The retry nobody could hear (2026-09-25)

The next attempt ("error at the same point, but it did look for a second like the
join was going to work") is the first run whose `[mp]` lines name the failure
outright - and the failure is not in the link.

**What the wire shows.** The attempt runs the whole protocol (auth, association,
sync, then the host's CMD frames) and then locks into a loop: the host sends
**135 command frames carrying the identical sequence number `02B0`** with
byte-identical payloads, the client answers 131 of them, the host collects 79 of
those answers, and both games then give up (the client sends a deauth 195ms in,
the host shows its join error). Of the 131 answers only the **first two were the
game's own reply** (`fc=0118`); the other 129 were the firmware's canned stand-in
(`SendMPDefaultReply`, `fc=0158`).

**Why the same seqno, and why the client keeps answering.** melonDS's own TX path
explains the frame: `TXSendFrame` preserves the sequence number and skips the
increment when `W_TXSlotCmd` bit 14 is set, and sets the 802.11 retransmit bit
only when the slot's TX count is non-zero. These frames have neither a new seqno
nor the retransmit bit (`fc=0228`), so this is the **game** retrying its own
command - exactly what the upstream commit "wifi: implement CMD retries"
(9c5cde81) exists to support.

**Why it can never end.** The hardware half of that support is sound: a repeated
CMD is answered like any other, from the guest's armed reply slot, or from
`SendMPDefaultReply` when there is none. The game half is the problem. A guest
can only answer a CMD by *arming a reply slot beforehand*, and it arms one when
it is handed a command; but melonDS's `cmd_dupe` flag suppresses the RX
notification for a duplicate seqno (`MPLastSeqno`, also from 9c5cde81). So every
retry is answered from a slot the guest has already spent, and once MKDS's two
armed slots are gone the hardware answers for it with the canned reply. The host
reads a reply that is not its peer's answer, and retries again. Nothing is lost
on the wire - the loop is closed by an emulation decision.

**The fix.** `cmd_retry_notify()` (`MD_CMD_RETRY_NOTIFY`, default on) hands the
repeat to the game, so the guest can re-arm a reply slot for the retry it is
being asked to answer. `=0` restores stock melonDS behaviour for comparison.

**Measured before shipping it as the default.** A/B on the documented NSMB
two-player script - same start save, same script, 12000 frames:

| | retries delivered (new) | retries suppressed (stock) |
|---|---|---|
| commands answered | 2505 | 1081 |
| `repeats the previous one` | 2504 | 1070 |
| canned default replies | 754 | 1059 |
| replies the host collected | 1522 | 629 |
| player 1 wireless on/off | 3/2 | 13/12 |
| frames p1 drew | 2629 | 2012 |
| pacing | 3.08x | 2.98x |

Two things fall out of this. First, **the retry loop is not an MKDS quirk**:
NSMB's host retries ~99% of its commands in *both* variants, so v0.1.8g's "0
dupes in 1078 commands" golden was a different phase of that session (the same
run shows p1 cycling its radio 13 times), not evidence about retries. Second,
delivering the retry more than doubles the replies the host collects (629 ->
1522) at the same speed, with a calmer radio and more frames actually drawn -
NSMB now gets more of its *real* replies than before, which is the direction a
fidelity fix should point.

**Unproven until the next join.** Whether MKDS's client now answers each retry
with a fresh reply and the join completes. The build is running and one
interactive attempt writes the answer into the same `[mp]` lines: look for
`handed to the game so it can re-arm its reply slot`, for the client's replies
staying `fc=0118` instead of collapsing to `fc=0158`, and for the host's CMD
seqno finally moving off `02B0`.

---

### v0.1.8i - A frame pays for its own reply waits (2026-09-25)

P2 got in. The retry fix did exactly what it was supposed to: the client answered
**1436 of 1436** commands with its own reply (`canned default replies: zero`,
down from 129 of 131 in v0.1.8h) and the join completed. What killed the session
next was the host's frame rate - P1 dropped to **single-digit fps** the moment it
tried to leave the joining phase, and the game errored out before ever reaching
character select.

**The ledger.** The health lines go `frames 17965 (+44) -> 18054 (+45) ->
18195 (+141)` - about 9fps for ten seconds - and *both* consoles report
identical frame counts, so it was the session, not one console. In that same 15s
the link carried 1438 CMDs, 1436 replies and 1438 acks, and **363 of the host's
reply windows came back empty**. Each empty window blocks in
`LocalMP::RecvReplies` for `ReplyTimeout` = 34ms of *wall clock*: 363 x 34ms =
**12.3s of the 15s window spent blocked inside the emulator**. Nothing else in a
frame blocks (`recv_timeout=0`, logged at boot), so the arithmetic accounts for
the frame rate exactly: 230 frames took 15s, 12.3s of it waiting.

**Why a fixed wall-clock number cannot be right here.** A DS command's reply
window is one or two milliseconds of *emulated* time. melonDS's own frontend runs
one real-time thread per console, so a wall-clock timeout is a fair stand-in
there. This host locksteps consoles and paces them to DS speed, so the wait is
wall-clock time spent inside an emulated frame - and the host's own reply latency
grows with the cost of a frame. That is a feedback loop: the longer a frame
takes, the later the peer's reply, the longer the wait, the longer the frame.
v0.1.8f raised the bound from 2ms to 34ms to stop the host giving up early, which
fixed collection and left the loop intact. This attempt is what the loop cost.

**The fix.** The waits are charged to the frame they happen in.
`MPInterface::SetReplyBudget` publishes a per-frame wall-clock budget (the app:
two emulated frames, `MD_REPLY_BUDGET_MS` to sweep; the harness: the same, or
`MD_REPLY_BUDGET_NS=-1` for the old behaviour), the Rust run loop calls
`md_link_set_reply_budget` immediately before every frame, and
`LocalMP::RecvReplies` waits for `min(ReplyTimeout, budget left)`. So a frame may
spend the slack it was going to idle away at the pace timer and no more. A reply
the peer cannot produce inside that slack is a reply this frame does not get, and
the game hears about it the usual way (`clientfail` in the host's ack). The
per-window timeout still ceiling-bounds a single window; this bounds their sum.

**Measured.** NSMB, documented script, same save, 12000 frames, budgeted vs
unbounded: **3.18x vs 3.14x** realtime, 2502 vs 2504 commands answered, 1525 vs
1523 replies collected, 746 vs 759 canned - and the new cap line (`... would have
waited 34ms for a reply, but this frame only has Nus of slack left`) fired
**zero** times in both runs. A healthy session never waits long enough to be
capped, which is what makes this a bound on the pathological case rather than a
change to the normal one. Smoke test 66/0 with the budget and with `-1`.

**Unproven.** That the cap actually holds the frame rate at DS speed *through* an
MKDS join, and that the join then completes. The running build shows it directly:
health lines staying near +300 per 5s while the CMD/reply traffic is that busy,
`would have waited ...` only where a window went empty, and the game reaching
character select.

---

### v0.1.8j - Both fixes hold, and the answer is a clock (2026-09-25)

The next attempt answered both open questions and then failed in a new place.

**Confirmed: the reply budget works.** Frame rate through the join: `+292`, then
`+180` (36fps) for one window, everything else `+299/+300`. Against v0.1.8i's
`+44/+45` (9fps) that is the cap doing its job - and the cap fired 1542 times
during the join, which is the proof it was needed rather than a theory. The
budget was never the collection problem either: the log shows the host still had
~30ms of slack when it capped, so it was trimming a 34ms ceiling to 30ms, not
giving up early.

**Confirmed: the retry fix works.** Counted over the 6.46s the client's radio was
on: the host sent 1542 CMDs and 1541 ACKs, the client's bridge received 1504 and
1506 of them (98% - the virtual air is not the problem), the client's core
processed 1503 as commands and answered **every one** with its own reply, and the
canned default count was **zero**.

**And the new failure.** The host collected only **734 of those 1503 answers**, so
its game reported `clientfail` and re-sent the same command (seq=02C0, 5741 times
in the same window) until the client's game gave up and switched its radio off
(+59.3s), which is the error the user saw. Nothing is lost on the wire: the DS's
reply window is 446us of *shared* time and the two consoles' clocks are ~15ms
apart (`lead` p50 -14856us, min -20080), so an answer is routinely produced after
the round that asked for it has already been closed - and the next command then
discards whatever the host had not read.

**A negative result worth keeping.** The obvious fix - stop re-anchoring the
host's reply read offset on each new command, so a straggler answer survives into
the next round - is *wrong*, and the harness caught it before it reached the user:
same NSMB script and save, 1525 collected replies and 2502 commands became **198
collected and 16626 commands**, with 15015 replies dropped as stale, because every
window then read an answer from an earlier round first. Upstream's re-anchor is
load-bearing: it is what makes a window read the *newest* answer. Reverted, and
verified back at baseline (2507 commands, 1652 collected, 0 stale drops, smoke
66/0); the same spot now logs how much it gives up, which on NSMB is one answer's
worth (66 bytes) about 30% of the time.

**Where the next fix has to go.** Not the link, not the frames: the two consoles
need a shared time base, which is what the DS gets from its beacon's timestamp.
`Wifi::TXSendFrame` stamps the beacon with `USCounter` and `FinishRX` copies it
back on the client, but our frame timestamps, `NextSync`/`RXTimestamp` and the
reply-window staleness check all run on `Wifi::USTimestamp` - a clock the beacon
never touches, and the two consoles' copies of it differ by twice the entire
reply window. Making that clock shared (or making the host's reply window span
the skew, in emulated time) is the next thing to try, with the NSMB harness's
collected-reply count as the measurement: it is 1525 of 2502 in a session that
*works*, so any real fix should move that number too.

### v0.1.8k - The answer that arrived one round late (2026-09-25)

The user's report: *"error at same point, when p1 trying to progress the game
after p2 joins"*. So the join itself now succeeds - P2 appears in P1's lobby -
and the session dies when P1 presses the button that leaves the lobby. The log
says why, and it is one number: **the client answered every second command.**

**What the client did.** Over the 6s its radio was on, the client's guest wrote
`W_AIDLow` alternating 1, 0, 1, 0 - 53 changes - and the reply path is gated on
that register, so the wire shows `[mp] answering cmd ... AID 1` followed by
`[mp] BLANK reply ... AID 0` all the way down. 1302 blank answers against 83
real ones. The host's acks agree: `clientfail=0002` in the blank rounds, and the
host's game re-sent the same byte-identical command 1383 times (`seq=01E0`, no
802.11 retransmit bit, `W_TXSlotCmd` bit 14, which is a guest-level retry). The
frame rate held (~60fps, the budget cap doing its job in 1542 rounds), nothing
was lost on the wire, no stale drops, no resyncs, no kicks - and MKDS is strict
enough that a host whose peer answers half its polls gives up: the client's game
switched its radio off 2s later and the host showed the error screen.

**Why the answer was late, in upstream's own history.** A DS answers a CMD from
a reply slot the guest armed *beforehand*, and it arms one when it is handed a
command - so the answer to round N is armed *during* round N and only leaves in
round N if the hardware sends it as soon as it is armed. Stock melonDS instead
waits for the round to end and sends whatever `Reply1` held when the round
*started*: the answer lands in round N+1, and round N gets a blank - or, when the
guest's arm is not in place at the round boundary (it writes `W_AIDLow` around
its arm, and the reply path is gated on `W_AIDLow`), nothing at all.

Upstream had this exact problem and this exact fix. Commit `8772258f` ("attempt
at making local multiplayer faster") added `SendMPEarlyReply()`: the armed reply
is sent from the `W_AIDLow` / `W_TXSlotReply1` writes, timestamped to the end of
the CMD's reception, with a state machine (`MPEarlyReply` 1 = window open, 2 =
already sent) so the round-end path does not send a second frame. Commit
`193c7ed9` reverted it two weeks later - with no reason in the message - and took
the GPU-line-aligned transfer timing (`PreTXAdjust`) with it. That revert is why
our client answers every other round.

**The fix.** `Wifi::SendMPEarlyReply()` is back, and only the early reply: the
transfer timing stays as upstream has it now. It fires from the CMD-arrival point
in `CheckRX` and from the two writes a guest arms with (`W_AIDLow`,
`W_TXSlotReply1`), and it hands the guest's frame to the bus verbatim (only the
channel byte is stamped in, as `TXSendFrame` does, because the virtual air is
channel-transparent but the frame is not) with the timestamp of the end of the
CMD's reception. When the early reply has gone out, the round-end path in
`FinishRX` sends nothing at all for that round - neither the reply nor the blank -
and, unlike upstream's version, does not run its slot bookkeeping either:
letting it consume the arm and run slot 5's transfer made the guest's *next* arm
miss its round (734 of 2504 rounds fell back to the canned `fc=0158` frame,
against zero when the early send owns the round). `MD_EARLY_REPLY=0` restores
stock melonDS to A/B it.

**Measured, NSMB two-player script, same save, 12000 frames:**

| | early reply (new default) | stock |
|---|---|---|
| answers sent inside their own round | **2505 of 2505** | 0 |
| answers sent a round late (round end) | 0 | 2502 |
| of those, the firmware's canned stand-in (`fc=0158`) | **0** | 752 (30%) |
| frames carrying the right channel and type (13, `fc=0118`) | 2505 | 1750 |
| replies the host collected | 1498 | 1513 |
| stale drops | 0 | 0 |
| speed | 3.15x realtime | 3.08x |

So the answer now leaves in the round that asked for it, the firmware's stand-in
disappears entirely, and nothing else moves. Smoke test 66/0 both ways.

**What to watch on the next live join.** `[mp] answered cmd seq=.... in the same
round` should appear for every CMD the host sends, with no `[mp] BLANK reply`
next to it; the host's CMD `seq=` should then start *changing* (its game
advancing) instead of staying on one value for thousands of frames. If it still
stalls, the next suspect is the ~15ms clock skew between the two consoles
(v0.1.8j), which is still unaddressed - the reply now leaves on time, but the two
sides still do not agree on what time it is.

### v0.1.8l - The client answers; the host cannot read it in time (2026-09-25)

Two more live MKDS attempts, same failure point, and the early reply of v0.1.8k
is now visibly *working* - which moved the break one step further in.

**What the early reply fixed.** In the second attempt the client sent **1389
answers, every one of them inside the round its command arrived in**, against
**6 blanks** (the previous attempt had 1302 blanks and 83 answers). The host's
game also advanced for the first time: its CMD sequence numbers moved
`01B0, 01D0, 01F0, 0210, 0230, 0250` before sticking on `0270` for 1244 rounds.
Zero stale drops, zero resyncs, zero kicks, ~60fps throughout.

**What is left: the host cannot read the answers in time.** The host collected
861 of 1389 answers (62%; the previous attempt managed 504 of 1468), and 498
rounds logged `starting a new command for p1 discards <N> unread bytes of
replies` - the answer *did* arrive, one command too late, and the re-anchor threw
it away. The cause is the same clock skew v0.1.8j identified, now measured
across a whole join: the client's clock ran from **1.5ms to 25ms ahead** of the
host's frame timestamps (`lead` -1496 ... -25128). Every host reply window opens
before its answer can exist, so each window *waits*, the per-frame reply budget
(v0.1.8i) is spent by the first rounds of the frame, and the later ones close
instantly with `0us of slack left`.

**Two clock fixes tried and rejected, both by the harness.**

1. *Pull the client's clock back to the host's timestamp* (the naive reading of
   "the DS slaves the client's clock"). NSMB collapsed: 2503 answers became
   **25539** with **24399** stale drops and 277 collected replies, at 2.07x
   realtime against 4.76x. A clock that jumps backwards re-arms the "deadline
   reached" test, so the console re-processes rounds it has already run.
2. *Keep the client's deadline on the host's clock* instead of clamping it to
   the client's own (`RXTimestamp = max(timestamp, USTimestamp)`). Also worse:
   0.77x realtime, 2963 canned replies, 95904 empty windows - the client then
   takes frames as fast as they appear and loses the pacing the reply windows
   depend on.

Both are reverted; the tree carries only the early reply, and the verify run
confirms the good state is intact (2503 answers, all in their own round, 0
blanks, 0 canned stand-ins, 1472 collected, 0 stale drops, 5.56x realtime, smoke
66/0).

**Where the next fix has to go.** Not the client: it answers every command, on
time, with its own reply. The host's side needs both halves of one idea - a reply
window that is anchored to the *host's* clock rather than the client's (so it
opens when the answer can exist), and a way to accept an answer that arrives
just after its window closed instead of discarding it on the next command. The
re-anchor that discards it is load-bearing (v0.1.8j's negative result), so the
change has to keep the queue anchored to the newest round while letting a
straggler *for the round that just closed* count. Attempts should be judged on
the harness first - two of the three ideas tried today were disproved there in
under two minutes each.

### v0.1.8m - Every failure ends in the same deauth (2026-09-25)

The seventh live MKDS attempt, and the first one read back with the question
"what *event* ends the session" rather than "what did the link lose".

**The join itself now looks healthy.** P2's radio came on at +19.951s and the
host's game channel opened (its first 74-byte CMD, `seq=0200`). Over the 6.08s
until P2's radio went off: the client answered **every** command it received
(1465 answers, 6 blanks, zero canned `fc=0158` stand-ins, zero stale drops), the
host collected **896 of 1511 answers** (59%), 615 rounds reported
`clientfail=0002` in their acks, and 538 commands re-anchored over unread bytes.
That is the v0.1.8l shape, nothing new.

**What ends it is one frame.** At host clock 9256992 the host transmitted a
**deauth** to P2's MAC (`[link-trace] p1 tx bcast len= 42 fc=00C0
rx=0009BF126643 tx=0009BF112233`). P2 received it, its `CheckRX` deauth branch
cleared `IsMP`/`IsMPClient`, and P2's game switched its radio off 0.6s later
(`[link-event] +26.033s p1 (player2): wireless off`). The host then re-sent the
same byte-identical command into a bus with no peer on it for the rest of the
session - 69843 transmissions, 53498 of them closing with `not even looking for
p1's replies - the bus has no other console attached` - which is the error
screen the player sees. The host never deauthed itself and never re-joined: it
is waiting for a client that its own firmware just evicted.

**It is a timeout, not the player.** In all four runs where the host deauths,
the deauth lands **2083, 1966, 2136 and 2088 ms** after that run's first
74-byte game command. A human pressing the button that leaves the lobby cannot
reproduce a mark that tightly across four sessions, so the "same point" the
reports name is MKDS's own ~2s window for the host to accept its client - and
the client is present the whole time, answering every round. (The fifth run is
the different failure: there the *client* deauthed 196ms in, before any of this.)

**What it is not.** Two of the obvious levers were measured and are not it:

1. *The reply loss.* The working NSMB reference loses exactly as much - 1524
   collected against 1456 empty windows and 697 discards, with `clientfail`
   reported the same way - and plays on happily for 200 emulated seconds. A
   partial collection rate is normal here; MKDS's join is simply less forgiving
   of it than NSMB's lobby is.
2. *The reply waits.* The harness defaults to a 2ms reply timeout where the app
   uses 34ms; raising it to the app's value moved collection from 60.8% to
   **62.6%** and cost speed (2.98x -> 2.81x realtime). The loss is not a wait
   that ends too early: the window outcomes come in bursts (runs of collected
   rounds interleaved with 4-12 empty ones, the `aged` values sawtoothing), i.e.
   the host's command rounds and the client's answers phase-lock in bursts and
   lose the rounds at each hand-off.

**Shipped: the two views that were missing.** The collected-reply line printed
`payload=%016llX`, an 8-byte read at payload offset 24 of a frame that carries a
24-byte header and a 6-byte body - two bytes past the end, which is why it
printed `0000000000000000` for **all 1512** healthy NSMB replies and answered
nothing. It now prints the frame length, `fc`, and the body clamped to the
frame, and two more views of the same answer were added so the three can be
compared: the client's own `[mp] answered cmd` line now carries its body, and
the host logs what its *game* is handed (`[mp] handing client N's reply to the
console's game`) - which is where an answer stops being a frame and becomes
something the game reads. `mp_trace_deauth` logs a deauth on both ends with the
body's reason code, so the next live run can separate "the firmware or game gave
up on this client" from "the game decided to leave the group".

**The baseline those views establish.** On the working NSMB session the
client's reply body is **constant, `00 00`** (4&nbsp;029 views, byte-for-byte the
same through the reply queue, the game seeing a 26-byte frame after
`W_RXLenCrop` strips the `5A 5A B8 B6` FCS), and no blank is ever handed to the
game. So an empty answer body is what a game *sitting in a lobby* sends. That is
the question the next MKDS run answers: if its client's bodies are also constant,
the client's game never joined the lobby it thinks it is in, and the ~2s timeout
is the client's fault; if they move, the client is there and the host is
rejecting an answer it can read - which points at the collection loss after all.

Verify: smoke 66/0, NSMB 12000-frame script unchanged (1524 collected / 1456
closed / 697 discards / 3.00x realtime), and the three views agree end to end on
the working case.

### v0.1.8n - The answers the host threw away (2026-09-25)

The eighth live MKDS attempt, run on the build from v0.1.8m with the new
diagnostics in, and the first one where the failure is explained by something
this project controls rather than by the games.

**What the run shows.** P2 joined at +222.732s. For 5.7s the client answered
**every** round it was handed (1554 answers, in their own round, zero blanks)
with its own game's data (`body[6]=00 80 5A 5A B8 B6` - the idle NSMB baseline is
`00 00`, so the client's game was in the lobby, not stuck on its way in) and the
host's game was handed that answer 874 times. Then P1's DS sent a deauth to P2's
MAC (`reason 0001`), then a broadcast one (`reason 0003`, "the station is leaving
the group"), and **both** consoles powered their radios off within 166ms of each
other (P2 +228.441s, P1 +228.607s). Reasons in hand, this is not a client being
evicted: it is the host closing the 802.11 group, and nothing re-creating one for
the race.

**The number that matters is `clientfail`.** The host read 874 of the client's
answers (56%) and its acks reported the client as failed in **686 rounds (44%)** -
against a client that answered all 1554. That reporting is a defect of this
queue, and the log says which one: the rounds came back in bursts of 5-7, and
637 commands logged discarding unread bytes on the way in.

**Why.** Two consoles are frame-locked but their US clocks are ~16ms apart, so
the client drains the commands that arrived since its last poll in one go and
answers a whole frame's worth of rounds as **one clump**. `RecvReplies` returns
as soon as the console it asked has answered once (`(ret & aidmask) == aidmask`),
so the round is credited with the clump's first answer - and the next command's
re-anchor (`ReplyReadOffset = MPStatus.ReplyWriteOffset`) throws the rest of the
clump away unread. Every discarded answer is a round the game is told failed.

**Fix: `MD_REPLY_KEEP` - keep the clump, bounded.** A command now walks the
unread entries (headers only, no consuming) and drops only the oldest ones
beyond *K* answers per console it is asking, counting the semaphore down to what
it kept so the window can read them (`LocalMP::SendPacketGeneric`, type 1).
Default **2**, because that is the clump size a live join actually produces.
Measured on the NSMB script, same save, 12000 frames, collected / empty windows:
**0 (stock) 1524/1456, 1 1904/1072, 2 1987/989, 8 2016/959, 16 2023/952**, zero
stale drops at every setting, and the runs stop spending wall clock blocked in
the reply wait (5.4-6.1x realtime against 3.0x). Past 2 it is flat - the residual
third of empty windows is an answer that does not exist yet rather than one being
thrown away, which is the clock question v0.1.8j/v0.1.8l left open, not this one.
The bound is also what keeps v0.1.8j's negative result out: that experiment kept
*all* unread answers, so the backlog grew without limit and every window read a
much older round first (198 collected / 16626 commands). `MD_REPLY_KEEP=0`
restores the old behaviour exactly.

Smoke 66/0; the session still reaches the lobby with all consoles running and
zero stale drops at every setting.

---

### v0.1.8s - The host's game hears the press and changes its poll; the guest's game never reads its mail (2026-09-25)

Thirteenth live attempt, on the v0.1.8r input-logging build, and the first one that
shows both sides of the stall. The user reports the error again; the log now says
where it lives:

**P1's press reaches the emulated game and the game responds.** The new `[input]`
lines show `player 1 buttons -> A (mask 00000001)` (8 presses), and **the host's
poll body changes for the first time in twelve runs**: from `00 80 06 00 B8 B6`
(468 rounds) to `00 00 06 00 B8 B6` (1260 rounds), starting at seq `10B0`, right
after the last A press. Whatever that bit means, the host's game consumed the
press and changed what it sends. The v0.1.8r "host-input" hypothesis is dead: the
input path works.

**The guest's game never reacts to any of it.** All 871 guest answers are still
`00 00` - including the 1260 rounds *after* the host's poll changed - and its
single armed answer (body `00 00 5A 5A B8 B6`, armed at seq 01B0) is never
refreshed. Yet the guest's *firmware* demonstrably received the changed commands:
the `cmd body slot=2` lines print the `00 00 06 00` bodies arriving at P2's
console, StartRX raised the right IRQs, and the frames were delivered. So the
break is between the guest's wireless RAM and its game - the game is not
consuming the mail.

**Why the v0.1.8o instrument saw nothing: it watched the wrong door.** Games read
received packets directly out of the wireless RAM window (`04804000+RXBufBegin`),
not only through `W_RXBufDataRead`. The new instrument (throttled to one line per
20ms of emulated time) counts reads of the receive region while multiplayer is
active, and on the working NSMB reference it fires immediately: **p1 (guest)
261+ reads, p2 (host) 1** in 3000 frames. That is the expected asymmetry - the
guest's game reads its mail, the host's game gets its replies pushed via the
handoff. This is now the deciding instrument: on the next MKDS run, if the
guest's reads stay at zero while the host's poll is `00 00 06 00`, the guest's
game is not being told mail arrived (IRQ0/route problem, ours to fix); if it
reads but its answer stays `00 00`, the guest's game itself refuses the host's
poll content (game-level, compare a real-DS capture).

App rebuilt and restarted on this build (pid 1361192); smoke 66/0; previous run
preserved at `/tmp/md_mkds_prev_input.log.gz`. Note the app's own touch-release
spam (102385 `[input] player N touch released` lines) is the frontend re-asserting
release every frame - harmless but loud; quiet it when next touching the input
code.

---

### v0.1.8r - The poll says nothing: the host's game asks the same 6 bytes 238 times and the guest's game gives up (2026-09-25)

Twelfth live attempt, on the v0.1.8q body-dump build, and the first run where the
*content* of the two games' exchange is on the record. The user's report is still
"error at the same point", and the failure is still the guest's game leaving -
but the leave is now explained down to the bytes, and the bytes say the host's
game never says anything.

**The full decoded conversation.** `cmd body` prints every new command body;
this session's, in order:

| phase | seq | body | meaning |
|---|---|---|---|
| join | 0220 | (guest sets AID=1 here) | guest's game enters the group |
| lobby announce | 0240/0260/0280 | `0C 1F 06 00` + UTF-16 `melonDS`, then `0C 9F 06 00 ...` | the host's game broadcasts **P1's firmware nickname ("melonDS")** to the room |
| poll | 238 rounds | `00 80 06 00 B8 B6` | a fixed 6-byte status poll, **never changes** |

The guest's answers: `00 00` (its lobby template) for all 245 rounds, including
during the name phase. The host's ack runahead values decode cleanly
(`0x0616/0x060F` alternating, one with `0x020616` = the runahead with the
client-joined bit - the flag dance works). **Neither side's body ever moves**
after the name phase; 251 commands, 245 answered, 245 collected (100%), 3.4s,
and then the guest's game transmits its leave (`reason 0003`).

**What this proves.** The games' protocol is running *above* the transport, both
sides are consuming each other's frames (the host re-broadcasts its name with
changed flag bytes `1F -> 9F` only after the guest's game joined - so the guest's
answers DO reach the host's game and change its behaviour), and then the host's
game enters a state where it polls status and never sends content. The guest's
game, which joined a room it can see (P1's name was literally in the air), waits
for its host to advance and, getting nothing but the same 6-byte poll for ~2.5s,
abandons the session. **The suspected mechanism is in our host input stack, not
the link**: P1's app-side game is sitting in its room waiting for a *host*
transition ("close lobby"/"start game") that its player is pressing, and the
press is either not reaching the game or the game's transition is blocked by
something emulation-side (touch coordinates, input focus, a dialog). The link
log would show a *different* body if the host's game tried to advance - and it
never does. The next instrument must read the host's *input* side, not the wire.

Corrections to v0.1.8q's ledger this version: the "join" AID write is at seq
0220 (not 03C0), the name phase is 3 commands (0240-0280, not "name broadcast
generic"), and the body[-24] `slot 0000` arm lines are the guest *clearing* its
arm each round before re-arming - a normal per-round cycle, amended from
"re-arm with no data". The guest's game-side watchdog consistent with a
stalled host is now the primary hypothesis; the emulator-transport is done.

Also this version: the harness's reply_budget options were located
(`--reply-timeout`, `--reply-budget`) for offline sweeps of MKDS timing if
needed; the MKDS scripted route remains too fragile per v0.1.8a (wizard,
NORMAL-vs-SIMPLE traps), so the app's interactive input path stays the way to
test the host-input hypothesis.

---

### v0.1.8q - The transport works; the guest's game joins and its answer never moves (2026-09-25)

Eleventh live attempt, on the v0.1.8p build, with the user reporting "error in the
same place" - and asking that every try and finding be recorded so we do not go in
circles. The error is *not* in the same place; the failure has moved twice now and
the ledger below is where things stand.

**What changed with the v0.1.8p length fix.** The host's ~2s-timeout deauth of the
client (`reason 0001`, every run since v0.1.8m) is **gone**. This run: the join
completed at +216.2s, the host's game-command channel opened (first 46-byte
game cmd at host clock 7223456), and the transport carried **982 commands over
4.29s of host clock (~4367us per command)** with the client answering 970, the
host collecting 967 of 970 (**99.7%** - 1630/1587 at 3000 frames and 9196/9202
at 12000 frames before), 3 empty windows, 12 blanks (the guest's pre-AID rounds),
0 canned, 0 stale, 0 discards. The host's command sequence numbers walk forward
the whole time - roughly one unique sequence per command, no retry loop, no
single sequence hammered. The channel fix + reply keep + early reply + notify
stack now gives MKDS the same transport the working NSMB reference has.

**What failed instead: the client's own game left the group.** At host clock
11511752 (~4.3s after the command channel opened) the **client** transmitted a
deauth to the host (`0009BF126643 -> 0009BF112233, reason 0003` = "station
leaving"), its wireless went off at +222.1s, and the host then broadcast its own
leave (`-> FFFFFFFFFFFF, reason 0003`) and shut the group down. The guest's last
round was fully healthy - it answered `seq=7D20` in-round, the host collected it
and handed its game `body[2]=00 00` - so this was a decision, not a loss.

**Why, per the guest's own logs.** MKDS's guest arms its answer **once** (body
`00 00 5A 5A B8 B6` at cmd seq 02C0), answers every subsequent round from that
one template - **the body is `00 00 ...` for all 970 answers; the guest's game
never says anything new while the host's game polls** - and its pre-answer rounds
before the arm are 0-byte writes to the arm register with the AID still 0. The
host's game asks ~982 times and the guest's game never changes its answer; then
the guest's game gives up. Two hypotheses, in order:

1. **The poll cadence is 10x too slow.** On real hardware, and in NSMB's
   emulation, a game round is one command per ~2ms (446us window + reply +
   ack + interframe). Here the host spends **~4367us per command** - the
   frame-locked host only reaches the next command once per ~7 emulated frames,
   so the guest's game sees a poll rate of ~230Hz instead of the ~2kHz it
   expects. A game that expects 8-16 poll exchanges to complete a handshake
   would see its 4.3s budget expire after only ~1-2 seconds' worth of the
   exchanges it wanted. The cadence is a *consequence of the emulation being
   slower than realtime* (the lockstep host runs ~1.4x realtime, so each
   emulated frame is ~10.5ms of wall clock and the game's poll-per-frame becomes
   a poll per 10.5ms of game time - wait, no: the poll cadence is in *emulated*
   time, which is self-consistent) - so this hypothesis needs a different
   mechanism: the guest's game's own *wall-clock inside emulation* matches the
   host's, both being the emulated DS. Discard this unless the next log shows
   the guest's game waiting on something wall-clock.
2. **The guest's game is stuck waiting for a command *content* it never
   receives.** The 970 answers all being the same template says the guest's
   game never saw anything that changed its state; its game-level session with
   the host is therefore either (a) waiting for the host's game to send a
   different *body* than it does, or (b) not reading the commands at all - and
   v0.1.8o established neither game ever reads `W_RXBufDataRead`, so (b) cannot
   be confirmed from the wire. What the guest's game DOES see is the delivery
   (`fc=0228` commands, 30 bytes, at every round). Whether the *content* of
   those 46-byte commands is what MKDS's guest expects is now the question -
   and the only remaining knob is ours: the reply the host's game gives its own
   console (`body[2]=00 00`, i.e. an empty 2-byte body) may be what a real
   MKDS host would fill with data.

**Also fixed this version (cosmetic but misleading):** the delivery log printed
`fc=0000` for every 26-byte reply handoff because the `framelen > 26` guard was
off by one for a 26-byte frame whose `fc` lives in the last 2 bytes it protects.
`>= 26` now, so the next log names the frame control of the host's reply
handoffs. Smoke 66/0.

**The v0.1.8o arms/deliveries interpretation is amended here:** "the guest's game
arms once" is MKDS-specific (its NSMB counterpart does the same, so it may be
game-dependent), and the `slot 0000` arm lines this run are the guest clearing
its reply arm every round before re-arming - not noise, but the guest's
per-round re-arm cycle with no data. The full MKDS attempt ledger is now:

| version | transport state | game state | failure |
|---|---|---|---|
| 0.1.8e-f | consoles never talked | n/a | identical MACs (fixed in 0.1.8e) |
| 0.1.8g | 3 answers then silence | client gave up | client deauth at 2.0s |
| 0.1.8h | canned replies answered retries | both bailed | `cmd_retry_notify` fixed this |
| 0.1.8i | 9fps during join | host too slow to poll | reply budget bounded it |
| 0.1.8j | 1503 answered, 734 collected | host's game stalled | clock skew; re-anchor load-bearing |
| 0.1.8k | 83 of 1385 real answers | client answered every other round | early reply restored |
| 0.1.8l | 861 of 1389 collected | host read 62% | clock skew again; keep/budget not the lever |
| 0.1.8m | 896 of 1511 | host evicted client at 2.0s | ~2s timeout, transport not the cause |
| 0.1.8n | 874 of 1554, acks lied 44% | host evicted client at 2.1s | reply keep fixed the lying acks |
| 0.1.8o | 1682 correct handoffs + 1674 bogus-length | host evicted client at 2.1s | 3354-byte mystery led to... |
| 0.1.8p | 9196 of 9202 on harness; length bug found | host evicted client at 2.1s | channel-stamp/length field order fixed |
| **0.1.8q** | **967 of 970, ~982 unique commands, no stalls** | **client's game left after 4.3s** | **game-level protocol (answer never changes)** |

Next: read the 46-byte command bodies the host's game sends (they are in the
log; the `[link-trace]` line does not carry them, so add a one-per-seq body dump
on the host's `tx cmd` path) and compare against what a real MKDS DS-to-DS
exchange carries; and log the guest's game's *reads* of the commands it is
delivered, which requires counting `W_RXBufDataRead` reads (zero today, meaning
the games use DMA or the RX buffer cursor - needs the right instrument).

---

### v0.1.8p - The host spent a frame and a half receiving every reply (2026-09-25)

Tenth attempt, and the first one where a *number* rather than a behaviour gives the
bug away. The v0.1.8o delivery log printed the frames the host's game is handed as
`3354 byte frame, fc=0118, as an MP host` - next to the `handing client 1's reply
... 26 byte frame, body[2]=00 00` line describing the very same call. A 3354-byte
reply made no sense, and the chase for it started from the frame's *destination*
and ended at the frame's *length*:

`StartRX` reads the frame length as a **16-bit** value at `RXBuffer[8]`, so byte 9
is the high half of it. The channel-transparency stamp from v0.1.8c is a single
byte written at 9 - and in `MPClientReplyRX` (the host's reply path) it was written
*after* the length:

```
*(u16*)&RXBuffer[8] = framelen;   // 26
RXBuffer[9] = (u8)CurChannel;     // ...and byte 9 of that 16-bit length
```

so the host's hardware read the length as `(channel << 8) | 26` - **3354** on
channel 13, 1818 on channel 7, different in every run, which is why it looked like
stale memory. `StartRX` then set `RXTime = length * 4` (the 2Mbps reply rate):
**13.4ms of emulated time to receive one 208us reply**, with `ComStatus` held busy
for the whole of it, so the host could not start the next frame's reception until
most of a frame had passed. The client was untouched - `CheckRX` writes its stamp
*before* the same length field, which is exactly why the client's deliveries in the
same log were sane 30- and 28-byte frames.

Fix: write the channel stamp before the length in `MPClientReplyRX`, so the length
field is what `StartRX` and the game read. Harness, same NSMB script from the same
save, 12000 frames, keep=2/notify=1: **answered 9202, collected 9196, empty
windows 6, canned 0, blank 0, stale 0, discards 0, 3.29x realtime** - against
1630 answered / 1587 collected / **801 empty** at 3000 frames and ~2016 collected /
~950 empty at 12000 frames before the fix. The host's rounds are no longer paced by
a phantom 13.4ms reception, so the same 12000 frames carry 4.5x as many MP rounds
and every one of them gets its answer. Smoke 66/0. Live MKDS still unproven at the
time of writing - and note this bug was *ours*: it arrived with the channel fix
(v0.1.8c) and is invisible to every counter that only looks at the wire.

---

### v0.1.8o - The answer the guest's game was never told it sent, and a 3354-byte frame on the host (2026-09-25)

Ninth attempt, and the first time the *guest's game* has been watched instead of
the wire. A new line reports every write to the reply-arm register with the bytes
it holds (`[mp] guest arms an answer: slot ... body[...]`), and a counter reports
every packet delivered to a console's own software
(`[mp] packet delivered to the console's own software: ... byte frame, fc=..., as
an MP client|host|outside multiplayer`). Two facts came out of the working NSMB
reference that reframe the whole MKDS problem:

1. **A working session arms exactly one answer.** Over 3000 frames the guest's
game wrote `W_TXSlotReply1` **once** (twice with stock's round-end reply path),
set its AID once, and answered all ~1700 rounds - every one of them a repeat of
the same command - from that single armed template. The host repeating one
byte-identical command for a whole session is not a stall: it is what NSMB's
working 2P link does too. So "the command is stuck" was never the symptom it
looked like, and an answer that does not change is not by itself a bug.
2. **Neither game reads its receive buffer.** Zero reads of `W_RXBufDataRead`
(both consoles, 3000 frames, and in the 66-check smoke test) while packets are
delivered to them, so what the emulated games do with MP traffic cannot be read
from the wire at all.

What the early reply (v0.1.8k) does differently is visible against stock: it
sends inside the round but skips the reply-slot transfer, and with it every
effect the hardware has on the *guest* when its answer leaves - the TX status
word (`RAM[replybuf] = 0x0001`, which stock writes one round late in
`SendMPReply`), the RF status, the `TXBusy` bit, and the completion interrupt
(`ProcessTX`, `num == 5`: `W_TXStat = 0x0401` + IRQ1 when `W_TXStatCnt` bit 12 is
set). `MD_EARLY_REPLY_ACK` (default 1) now performs both. Honest result: on the
NSMB harness it changes nothing measurable - answered 77202/77101, collected
73437/73330, zero blanks, zero canned replies, and the one-shot line reports
`W_TXStatCnt=0000`, i.e. the NSMB guest's game had not enabled that interrupt at
all. It is shipping as the *faithful* behaviour, not as a proven fix.

Two negative results, recorded so they are not retried:

- **Consuming the arm is not the handshake.** With `MD_EARLY_REPLY_ACK=2` the
guest still never re-armed: 1645 of 3000 rounds fell back to the firmware's
canned `fc=0158` reply, and the arm count stayed at 1.
- **Telling the guest's game about retries is not the lever either.**
`MD_CMD_RETRY_NOTIFY=1` delivers all 1737 repeats of the host's command (the new
`cmd from host:` line counts them) and the guest still arms once. It costs
nothing measurable (answered 1633 vs 1648, collected 1577 vs 1560 at 3000 frames)
and it is the only setting in which the guest's game is told the host is still
asking, so live runs now use it - but the frozen answer is not caused by it.

**The new lead is on the host, and it is malformed input, not lost input.** In
the same working NSMB session the host's game is handed each client reply
*twice*: once correctly (`[mp] handing client 1's reply to the console's game:
26 byte frame, fc=0118, body[2]=00 00`, 1682 times) and once as
`packet delivered to the console's own software: N so far, 3354 byte frame,
fc=0118, as an MP host` (1674 times). A 3354-byte frame control 0118 is not
something any console built - the reply is 30 bytes - so the host's receive queue
is being fed a second, impossible frame for every answer, and MKDS's host state
machine is the one that has to make sense of it. The client is handed only its
own commands (30 bytes, `fc=0228`) and acks (28 bytes, `fc=0218`), both sane.
Next: find which of `CheckRX`'s delivery paths produces that frame (the regular
path's `FinishRX` length is the frame length, so 3354 arrives from somewhere
else) and keep MP frames off it.

Smoke 66/0 throughout; the live app was rebuilt and restarted on this build with
`MD_CMD_RETRY_NOTIFY=1`, every delivery listed rather than sampled, so one more
join attempt names the source of that frame.

---


## Lessons from mgba-splitscreen

Detailed implementation guidance based on the mgba-splitscreen project:

### 1. Architecture

**mgba-splitscreen approach:**
```
libmgba (C) — mGBA core, compiled as static library
Rust backend (src-tauri) — wraps libmgba via bindgen, manages instances, runs frame loop, serves frames + input over WebSocket
Frontend (src) — HTML/JS canvas UI, renders screens, maps input to GBA buttons
```

**melonDS Multiplayer should follow:**
```
melonDS core (C++) — NDS emulation, Wi-Fi link cable
Rust backend (src-tauri) — wraps melonDS, manages 2-4 instances, frame loop, WebSocket
Frontend (src) — HTML/JS canvas UI, renders screens side by side, handles input
```

**Why this works:**
- Clean separation: emulation in C++, app logic in Rust, UI in web tech
- WebSocket allows same frontend to work in browser and Tauri app
- Easy to test frontend independently

### 2. View Modes (Essential Feature)

mgba-splitscreen implemented four view modes, all essential:

| Mode | Description | Use Case |
|------|-------------|----------|
| **Grid** | All screens equal size, side by side | 2-4 players, fair view |
| **Speaker** | 1 large screen + smaller others | Competitive games, one player needs larger view |
| **Focus** | Single screen, full size | Watching, solo play, presentation |
| **Overlay/PiP** | Picture-in-picture, compact | Small screens, monitoring multiple |

**Implementation notes:**
- Cycle through modes with hotkey (F8/F9 in mgba-splitscreen)
- Should be remappable
- Each mode needs different canvas layout calculations
- Background image option is nice-to-have

### 3. Controls Design

**mgba-splitscreen control scheme:**

Player 1 (left hand): W/A/S/D for D-Pad, K for A, J for B, H for L, L for R, Enter for Start, Backspace for Select

Player 2 (right hand/arrows): Arrow keys for D-Pad, M for A, N for B, V for L, B for R, P for Start, O for Select

Players 3-4: Additional clusters (T/G/F/R and I/Q/C/E zones)

**Key lessons:**
- Keyboard zones should be intuitive (left hand vs right hand)
- All controls must be fully remappable
- Global hotkeys (turbo, save/load, pause) should also be remappable
- Gamepad support: auto-assign controllers to player slots

**For melonDS:**
- Player 1: WASD + nearby keys (left side of keyboard)
- Player 2: Arrow keys + nearby keys (right side of keyboard)
- Players 3-4: Additional zones or rely on gamepads
- Consider DS's dual-screen nature: touch screen input for bottom screen

### 4. Save State Management

**mgba-splitscreen approach:**
- Quick save (F5): Captures ALL players' states together
- Quick load (F7): Restores ALL players' states together
- Battery saves: Auto-loaded from .sav files next to ROM
- Save import/export: Per instance or as a set across all running instances

**Why global save/load matters:**
- In multiplayer games, all players need to be at the same point
- Separate save states would cause desync
- Loading one player's state without others breaks the game

**For melonDS:**
- Quick save: Save all NDS instance states to one file
- Quick load: Load all states from one file
- Battery saves: .sav files per instance, auto-loaded

### 5. Game Library UX

**mgba-splitscreen features:**
- Recent games list (persisted)
- ROM folder scanning (add folders to library)
- Box art display (local sibling images or online fallback)
- Controller/keyboard navigation in library
- Launch with player count selection from library

**Implementation guidance:**
- Scan ROM folders for .nds files
- Extract title from ROM header for display
- Look for box art images (PNG/JPG) with same name as ROM
- Fallback to online box art service if no local image
- Store game library config persistently (localStorage or file)

### 6. Pause Menu

**mgba-splitscreen pause menu:**
- Pausing (Escape) freezes ALL players at once
- Menu is controller-navigable
- Options: Resume, Save, Load, Players, Library, Quit ROM

**Why pause all players:**
- In multiplayer, one player pausing should pause everyone
- Prevents one player from gaining advantage by pausing

### 7. Turbo Mode

**mgba-splitscreen:**
- Turbo (Q key): Fast-forward past 60fps for grinding through menus/animations
- Useful for: navigating game menus, fast-forwarding through dialogue, grinding

**For melonDS:**
- Implement similar turbo mode
- Should affect all instances equally (or be per-player?)

### 8. Deep Links / URL Parameters

**mgba-splitscreen:**
- `?players=N` — Start with N players (1-4, default 2)
- `?rom=<url>` — Fetch and start a ROM immediately

**For melonDS:**
- `?players=N` — Useful for quick launch
- Could add `?rom=` for direct launch

### 9. Platform Support

**mgba-splitscreen platforms:**
- **Desktop (Tauri)**: Native app, full performance
- **Web (WASM)**: Full emulator in browser, no install
- **Android (Tauri)**: Thin shell around WASM engine

**For melonDS:**
- **Desktop (Tauri)** — Primary target
- **Web (WASM)** — Optional, could be future enhancement
- **Android** — Optional, tablets/TVs could benefit

### 10. Build Considerations

**mgba-splitscreen build notes:**
- First build compiles entire libmgba from source (takes minutes, several GB RAM)
- Subsequent builds are incremental
- Android build is fast because it uses WASM, not native compilation

**For melonDS:**
- melonDS is already large; consider build times
- Static linking of melonDS core into Tauri app
- CMake for melonDS, cargo for Rust/Tauri

---

## Relationship to Original melonDS

melonDS Multiplayer is a derivative work based on melonDS. Key points:

1. **License**: Inherits GNU GPL v3 from melonDS
2. **Code Base**: Uses melonDS emulator core
3. **Architecture**: Adds Tauri wrapper for splitscreen display
4. **Independence**: Separate project with different goals (splitscreen vs single-player)

### Upstream Relationship
- `upstream` remote: https://github.com/melonDS-emu/melonDS.git
- Track upstream for emulator core updates
- Adapt changes as needed for splitscreen architecture

### Credits
Based on melonDS by Arisotura (https://github.com/melonDS-emu/melonDS)

Additional credits from original melonDS:
- Martin for GBAtek documentation
- Cydrak for 3D GPU research
- limittox for the icon
- Community testers and contributors

**Inspired by:** [mgba-splitscreen](https://github.com/Spuds0588/mgba-splitscreen) — demonstrated the viability of emulator-based splitscreen multiplayer.

---

## Future Milestones

### Near-term Goals (Phase 1-2)
1. Implement the local link between instances — the core of the product
2. Wire the Rust side of the Tauri app to the C bridge
3. Pipe framebuffers to the webview and draw one screen per player
4. Extend to multiple screens side by side
5. Implement view modes (grid, speaker, focus, overlay)
6. Decide and enforce the emulator threading model

### Medium-term Goals (Phase 3-4)
1. Per-player keyboard controls with remapping
2. Gamepad support
3. Game library with box art
4. Save state management (global save/load)
5. Pause menu
6. Testing with multiplayer games

### Long-term Goals (Phase 5+)
1. Beta release
2. Optional: WebRTC for remote players
3. Optional: Android/Web support
4. Additional view mode customization

---

## Document History

| Date | Version | Author | Changes |
|------|---------|--------|---------|
| 2026-09-25 | 0.1.8s | The host's game hears the press; the guest's game never reads its mail | Thirteenth live attempt on the input-logging build. **P1's A presses reach the emulated game** (`[input] player 1 buttons -> A`) and the host's game **changes its poll for the first time in twelve runs**: `00 80 06 00` -> `00 00 06 00` (468 -> 1260 rounds) right after the last press - the v0.1.8r host-input hypothesis is dead, the input path works. But the guest's game never reacts: all 871 answers stay `00 00` even through the 1260 changed-poll rounds, while its firmware demonstrably received the new bodies (`cmd body slot=2` shows them arriving). Break is between the guest's wireless RAM and its game. Cause of the earlier blind spot found: games read packets directly out of the wireless RAM window, not only via `W_RXBufDataRead`; new throttled instrument on the window read path fires immediately on the working NSMB reference (guest 261+ reads, host 1 - the expected asymmetry). Deciding test next run: guest reads stay 0 while the changed poll is in the air = delivery IRQ problem (ours); guest reads but answer stays `00 00` = game-level refusal (compare a real-DS capture). Also noted: the app's frontend re-asserts touch-release every frame (102385 log lines) - harmless, quiet it later |
| 2026-09-25 | 0.1.8r | The poll says nothing | Twelfth live attempt, first with command bodies on the record. The decoded conversation: the guest's game joins (AID at seq 0220), the host's game broadcasts P1's firmware nickname **"melonDS"** in UTF-16 three times (seq 0240-0280, flag bytes `1F -> 9F` changing only after the guest joined - so the guest's answers reach the host's game and change its behaviour), then settles into a fixed 6-byte poll `00 80 06 00 B8 B6` for **238 rounds**; the guest answers `00 00` throughout; 251 commands, 245 answered, 245 collected (100%), 3.4s, then the guest's game leaves (`reason 0003`). Neither body ever moves after the name phase. Interpretation: the games' protocol runs above the transport and both consume each other's frames; the host's game enters a poll-only state and never sends content, so the guest's game - which joined a room whose name it could literally see - waits for its host to advance and abandons the session when the poll never changes. The suspected stall is **host-input-side** (P1's player press not reaching the emulated game, or the game's transition blocked emulation-side), not the link; the next instrument must read the app's input path, not the wire. Ledger corrections recorded (AID at 0220; `slot 0000` arm lines are the guest's normal per-round clear/re-arm cycle) |
| 2026-09-25 | 0.1.8q | The transport works; the guest's game joins and its answer never moves | Eleventh live attempt on the v0.1.8p build. **The ~2s host deauth is gone and the transport now matches the working NSMB reference**: 982 commands over 4.29s of host clock (~4367us each), the client answered 970, the host collected 967 of 970 (**99.7%**), 3 empty windows, 0 canned/stale/discards, command sequence numbers walking forward with no retry loop. **The failure moved to the game level**: at host clock 11511752 the *client's* game transmitted a deauth to the host (`reason 0003` = "station leaving"), its radio went off, and the host broadcast its own leave - the guest's last round was fully healthy (answered in-round, collected, handed to the host's game), so this was a decision, not a loss. The guest's game armed its answer **once** (body `00 00 5A 5A B8 B6`) and answered all 970 rounds from that template - its answer never moves while the host's game polls - and its pre-arm rounds write `slot 0000` with AID 0, i.e. a per-round re-arm cycle with no data. Also fixed: the delivery log's `fc=0000` on 26-byte reply handoffs was an off-by-one guard (`> 26` -> `>= 26`). Full 12-row attempt ledger (0.1.8e..q) recorded in the section body: transport rows all now green, the open question is game-level - what the 46-byte command bodies carry and whether the guest's game reads them. Smoke 66/0 |
| 2026-09-25 | 0.1.8p | The host spent a frame and a half receiving every reply | Tenth live attempt. The v0.1.8o delivery log reported the host being handed `3354 byte frame, fc=0118, as an MP host` beside the `handing client 1's reply ... 26 byte frame` line for the same call - and the 3354 was the length: `StartRX` reads the frame length as a **16-bit** value at `RXBuffer[8]`, so byte 9 is its high half, and v0.1.8c's single-byte channel stamp at 9 was written *after* it in `MPClientReplyRX`, making the length `(channel << 8) | 26` (3354 on channel 13, 1818 on channel 7 - run-dependent, which is why it read as garbage). `StartRX` then set `RXTime = length * 4` at the 2Mbps reply rate: **13.4ms of emulated time to receive a 208us reply**, with `ComStatus` busy throughout, so the host could not begin the next frame's reception until most of a frame had gone by. The client was unaffected (`CheckRX` writes its stamp *before* the same length field - its deliveries were sane 30/28-byte frames in the same log). Fix: stamp the channel before the length. Harness, same NSMB script and save, 12000 frames: **answered 9202, collected 9196, empty 6, canned 0, blank 0, stale 0, discards 0, 3.29x realtime** vs 1630/1587/**801 empty** at 3000 frames and ~2016/~950 empty at 12000 before - 4.5x the MP rounds in the same frames, all of them answered. The bug was ours, introduced by the channel fix, and invisible to every wire-only counter. Smoke 66/0 |
| 2026-09-25 | 0.1.8o | The answer the guest's game was never told it sent | Ninth live attempt, watched from the guest's side for the first time. Two lines added: every reply-arm write with the bytes it holds, and every packet delivered to a console's own software. The **working** NSMB reference answers all ~1700 rounds from a **single** armed template (one `W_TXSlotReply1` write in 3000 frames, 2 under stock) and repeats one command for the whole session, so neither the frozen answer nor the repeated command is the bug - and neither game ever reads its receive buffer (`W_RXBufDataRead` reads: 0), so the games' own handling cannot be read from the wire. What the v0.1.8k early reply *does* skip is everything the hardware does to the guest when its answer leaves: the TX status word, the RF status, the `TXBusy` bit, and the reply-transfer completion (`W_TXStat=0x0401` + IRQ1 when `W_TXStatCnt` bit 12). `MD_EARLY_REPLY_ACK` (default 1) now does both; on the harness it is measurably identical (answered 77202/77101, collected 73437/73330, 0 blank/0 canned) and the guest reports `W_TXStatCnt=0000`, so it ships as faithfulness, not as a proven fix. Two negatives recorded: consuming the arm is not the handshake (mode 2 -> 1645/3000 canned replies, guest still never re-armed) and telling the game about retries is not the lever (`MD_CMD_RETRY_NOTIFY=1` delivers all 1737 repeats, guest still arms once; kept for live runs because it is the only setting where the guest knows the host is still asking, at no measurable cost). **New lead, host-side and malformed rather than lost:** the host's game is handed every reply twice - once correctly (`handing client 1's reply ... 26 byte frame, body[2]=00 00`, 1682 times) and once as `packet delivered ... 3354 byte frame, fc=0118, as an MP host` (1674 times), a 3354-byte reply that no console built. Smoke 66/0 |
| 2026-09-25 | 0.1.8n | The answers the host threw away | Eighth live MKDS attempt, on the v0.1.8m diagnostics. The join is healthy (client answers **every** round, 1554 answers, own game data `00 80` - where the idle NSMB baseline is `00 00`, so the client's game really is in the lobby), the host reads 874 (56%) and its acks call the client failed in **686 rounds (44%)**. Then P1's DS closes the 802.11 group (deauth `reason 0001` to P2, then `reason 0003` broadcast = "leaving the group") and **both** consoles power their radios off within 166ms - not a client being evicted, the host ending the group with nothing re-creating one for the race. The `clientfail` rate is this queue's doing: the consoles are frame-locked but ~16ms apart in US clock, so the client drains the commands that piled up since its last poll and answers a whole frame of rounds as one **clump**; `RecvReplies` returns after one answer per asked console, and the next command's re-anchor discards the rest of the clump unread (637 discards), so every thrown-away answer is a round the game is told failed. Fix: `MD_REPLY_KEEP` (default 2, `0` restores stock) — a command now walks the unread entries and drops only the oldest beyond K answers per asked console, counting the semaphore down to what it kept. NSMB script, same save, 12000 frames, collected/empty: **0 1524/1456, 1 1904/1072, 2 1987/989, 8 2016/959, 16 2023/952**, zero stale drops at every setting, and the runs stop spending wall clock blocked (5.4-6.1x realtime vs 3.0x). Past 2 it is flat, so the default is the clump size a live join produces, not a bigger number; the residual third of empty windows is an answer that does not exist yet (the clock question), not one being discarded. Smoke 66/0 |
| 2026-09-25 | 0.1.8m | Every failure ends in the same deauth | Seventh live MKDS attempt read back asking what *event* ends the session. The join looks healthy for 6.08s: client answers every command it receives (1465 answers, 6 blanks, 0 canned, 0 stale drops), host collects 896 of 1511 (59%), 615 rounds report `clientfail=0002`. What ends it is **one frame**: the host transmits a deauth to P2's MAC (host clock 9256992), P2's `CheckRX` deauth branch clears `IsMP`/`IsMPClient`, P2's radio goes off 0.6s later (+26.033s), and the host then re-sends the same byte-identical command into an empty bus for 200+s (69843 transmissions, 53498 closing with `not even looking ... no other console attached`) - that stall is the error screen the player sees. **The deauth is a timeout, not the player**: 2083/1966/2136/2088ms after each run's first 74-byte game command across the four runs that get that far, far too tight for a button press - so the "same point" the reports name is MKDS's own ~2s window to accept a client that is present and answering every round. Two levers measured and ruled out: the reply loss is not sufficient to explain it (the *working* NSMB reference loses the same amount - 1524 collected / 1456 empty / 697 discards, `clientfail` likewise - and plays on for 200s), and the reply waits are not the lever either (harness 2ms -> 34ms moved collection 60.8% -> 62.6% and cost speed, 2.98x -> 2.81x); the window outcomes come in bursts, so the host's rounds and the client's answers phase-lock in bursts rather than losing replies at random. Shipped: the collected-reply diagnostic was **broken** (`payload=%016llX` read 8 bytes at payload offset 24 of a 24-byte-header + 6-byte-body frame, two past the end - it printed zeros for all 1512 healthy NSMB replies and answered nothing); it now prints frame length/`fc`/clamped body, the client's `answered cmd` line carries its body, `[mp] handing client N's reply to the console's game` shows what the host's game is handed, and `mp_trace_deauth` logs the deauth on both ends with the body's reason code so the next run separates "gave up on this client" from "left the group". Baseline those views establish: in the working NSMB session the client's body is **constant `00 00`** (4029 views, the game seeing a 26-byte frame after `W_RXLenCrop` strips the `5A 5A B8 B6` FCS) with no blanks - so an empty answer is what a game *sitting in a lobby* sends, and whether MKDS's bodies move is the decisive question for the next run. Verify: smoke 66/0, NSMB script unchanged (3.00x realtime), three views agree end to end |
| 2026-09-25 | 0.1.8l | The client answers; the host cannot read it in time | Sixth MKDS attempt: the v0.1.8k early reply is visibly working (client sends **1389 answers, every one inside the round its command arrived in**, against 6 blanks where the previous attempt had 1302 blanks/83 answers) and the host's game advanced for the first time (`01B0..0250`, then stuck on `0270` for 1244 rounds), zero stale drops/resyncs/kicks, ~60fps. What is left: the host collected 861 of 1389 (62%) and 498 rounds logged `discards <N> unread bytes` - the answer arrives one command late and the re-anchor throws it away. Cause is the v0.1.8j clock skew measured across a whole join: the client's clock runs 1.5ms -> 25ms *ahead* of the host's frame timestamps (`lead` -1496 ... -25128), so each window opens before its answer can exist, waits, and the per-frame budget is spent by the frame's first rounds (the rest close at `0us of slack left`). Two clock fixes tried on the harness and both rejected: pulling the client's clock back to the host's timestamp re-arms the deadline test and collapses NSMB (2503 answers -> 25539 with 24399 stale drops, 277 collected, 2.07x realtime); keeping the client's deadline on the host's clock instead of clamping it also regresses (0.77x realtime, 2963 canned replies, 95904 empty windows, the client taking frames as fast as they appear). Both reverted, verify run back at the good state (2503 in-round answers, 0 blanks/0 canned, 1472 collected, 0 stale, 5.56x realtime, smoke 66/0). Next fix must be host-side: a reply window anchored to the host's clock, plus a way to accept a straggler for the round that just closed without dropping the load-bearing re-anchor |
| 2026-09-25 | 0.1.8k | The answer that arrived one round late | Fifth MKDS join attempt: P2 joined and the session died the moment P1 pressed the button that leaves the lobby. The log names it - the client answered every **second** command. Its guest wrote `W_AIDLow` 1,0,1,0 (53 changes) and the reply path is gated on that register, so the wire shows `[mp] answering cmd ... AID 1` / `[mp] BLANK reply ... AID 0` down the whole join: 1302 blanks against 83 real answers, `clientfail=0002` in the blank rounds' acks, the host re-sending one byte-identical command 1383 times (`seq=01E0`, `W_TXSlotCmd` bit 14, a guest-level retry), frame rate steady ~60fps, nothing lost on the wire, no drops/resyncs/kicks - and MKDS is strict enough that half-answered polls are not enough, so the client's game switched its radio off 2s later. Cause, found in upstream's own history: a DS answers a CMD from a reply slot armed *beforehand* and arms one when handed a command, so the answer can only leave in its own round if the hardware sends it as soon as it is armed; stock melonDS waits for the round to end and sends what `Reply1` held when the round *started*, which lands the answer in round N+1 and leaves round N blank (or, when the arm is missing at the boundary, silent - the gate is on `W_AIDLow`, which the guest writes around its arm). Commit `8772258f` added exactly this early reply (`SendMPEarlyReply`), commit `193c7ed9` reverted it two weeks later with no reason, taking the GPU-line-aligned transfer timing with it - that revert is why our client answers every other round. Fix: the early reply is back (`Wifi::SendMPEarlyReply`, fired from the CMD-arrival point and from the `W_AIDLow`/`W_TXSlotReply1` writes, frame sent verbatim with the channel byte stamped in, timestamped to the end of the CMD's reception, round-end path suppressed from sending a second frame), transfer timing left alone, `MD_EARLY_REPLY=0` to A/B. NSMB two-player script, same save, 12000 frames: answers sent inside their own round **2505/2505** vs 0, canned `fc=0158` stand-ins **0** vs 752 (30%), correct channel+type on 2505 vs 1750 frames, collected 1498 vs 1513, speed 3.15x vs 3.08x, stale drops 0 both, smoke 66/0 both ways |
| 2026-09-25 | 0.1.8j | Both fixes hold; the answer is a clock | Third attempt after the retry fix: P2 joined and the frame rate held (health `+292`, one `+180`, rest `+299/+300` vs 9fps before) while the reply budget fired 1542 times - and the budget was never the collection problem (the log shows ~30ms of slack left when it capped). Over the 6.46s the client's radio was on: host sent 1542 CMDs + 1541 ACKs, client's bridge received 1504 + 1506 (98%), core processed 1503 and answered **all** of them, canned replies **zero**. New failure: the host collected only 734 of those 1503 answers, reported `clientfail`, re-sent seq=02C0 5741 times, and the client's game switched its radio off at +59.3s. Cause: the DS reply window is 446us of shared time and the consoles' clocks are ~15ms apart (`lead` p50 -14856us), so answers land after the round that asked for them closed and the next command discards what was unread. Negative result recorded: removing the re-anchor instead of fixing the skew collapsed the NSMB harness (1525 collected/2502 commands -> 198/16626, 15015 stale drops) because every window then read an older round's answer; reverted, verified back at baseline (2507/1652/0 stale, smoke 66/0), and the re-anchor now logs how much it gives up (66 bytes ~30% of commands on NSMB). Next fix: give the consoles a shared time base (the beacon stamps `USCounter` but our scheduling runs on `Wifi::USTimestamp`, which the beacon never touches) |
| 2026-09-25 | 0.1.8i | A frame pays for its own reply waits | The join that 0.1.8h unblocked got all the way in (client answered 1436/1436 commands, zero canned replies) and then P1 fell to single-digit fps as it left the joining phase; the game errored out before character select. Ledger: in that same 15s window 363 reply windows came back empty, each blocking the 34ms wall-clock timeout in `LocalMP::RecvReplies` = 12.3s of the 15s spent inside the emulator, the only blocking call in a frame (`recv_timeout=0`) - and both consoles showed identical frame counts, so the session was the bottleneck, not a console. A wall-clock reply timeout is fair for melonDS's one-real-time-thread-per-console frontend but not for a lockstepped DS-paced host, where it is a feedback loop: a slower frame makes the peer's reply later, the longer wait makes the frame slower. Fix: waits are charged to their own frame - `MPInterface::SetReplyBudget` + `md_link_set_reply_budget`, published by the Rust run loop before every frame (app default two emulated frames, `MD_REPLY_BUDGET_MS`; harness `MD_REPLY_BUDGET_NS`), `RecvReplies` waits `min(ReplyTimeout, budget left)`, and a reply that misses the slack is reported to the game as usual. NSMB A/B from one save, 12000 frames: 3.18x vs 3.14x realtime, reply collection unchanged, the new cap line fired zero times - it bounds the pathological case without touching the normal one. Smoke 66/0 both ways. Frame rate through a live join still unproven |
| 2026-09-25 | 0.1.8h | The retry nobody could hear | Fourth MKDS join attempt read back and the loop named: the host sends 135 byte-identical CMDs all carrying seq=02B0 (the game's own retry - `W_TXSlotCmd` bit 14, no 802.11 retransmit bit, so upstream's "CMD retries" mechanism), the client answers 131, the host collects 79, and both games then bail (client deauth at 195ms). Of the 131 answers only the first two were the game's real reply (`fc=0118`); the other 129 were `SendMPDefaultReply`'s canned stand-in (`fc=0158`). Cause: melonDS's `cmd_dupe` suppresses the RX notification for a duplicate CMD seqno, but a guest can only answer a CMD by arming a reply slot *in advance* - so every retry is answered from a spent slot, the host reads something that is not its peer's answer, and retries forever; nothing is lost on the wire. Fix: `cmd_retry_notify()` (`MD_CMD_RETRY_NOTIFY`, default on) delivers the repeat to the game; `=0` restores stock behaviour. A/B'd on the documented NSMB script (same save, same 12000 frames): retries delivered 2505 answers / 1522 collected / 754 canned / p1 radio 3-2 / 2629 frames vs stock 1081 / 629 / 1059 / 13-12 / 2012 - so the retry loop is normal for NSMB too (v0.1.8g's "0 dupes" was a different session phase), and delivering nearly doubles the real replies the host reads at the same speed. NSMB regression-checked, smoke 66/0 both ways; MKDS join completion still unproven |
| 2026-09-25 | 0.1.8g | The client quits, not the host | Newest MKDS join read back: the full DS MP handshake now runs (auth/assoc/sync, then 11 host CMD frames, 9 pulled by P2's hardware, 3 answered, then P2 deauthenticates 2.0s in). Nothing lost in the bus, no drops/resyncs/kicks, host acks credit the replies it gets - the failure moved from "host hangs up on a client that answered" to "client answers three times and quits". The documented NSMB two-player run was used as the reference for a *working* session under the same tracing: 1078 commands all answered, 638 replies collected, 828 empty collection rounds, host acks reporting `clientfail=0002` routinely - and the game plays fine, so a partial collection rate is normal and `clientfail=0002` alone is not the MKDS blocker. Two systemic properties measured: the client's clock free-runs ~15ms ahead of the host's frame timestamps (hardware slaves it), and the host reuses one CMD sequence number so every command after the first is a duplicate the guest is never notified of. Shipped: `[mp]` narration of every decision that can swallow or answer a command frame (both clocks, the three receive-filter drop paths, blank replies, duplicates), `reply window:` logging of what the host actually reads and collects (with bytes), and `[link-trace]` `seq=`/`t=`/`(empty frame)`. Smoke 66/0 throughout; NSMB reference run clean with all tracing on |
| 2026-09-11 | 0.1.0 | Project Initiation | Initial documentation created, mgba-splitscreen lessons added |
| 2026-09-12 | 0.1.1 | Native bridge | C bridge over the headless core, hand-assembled diagnostic ROM, 36-check smoke test; build path documented |
| 2026-09-12 | 0.1.2 | Local link | LocalMP wired in with per-slot routing and broadcast delivery, visual capture + contact sheet, 58-check smoke test |
| 2026-09-13 | 0.1.3 | Real game links | Per-console threads + per-frame barrier, split receive timeouts, MKDS two-console race, menu-driving harness |
| 2026-09-13 | 0.1.4 | Three consoles | NSMB links; SM64DS VS mode needs firmware (product decision); second joiner breaks the group, per-instance MPHostinst hypothesized; touch probe + JIT toggle; 66-check smoke test |
| 2026-09-22 | 0.1.5 | Tauri host builds | Fixed four first-build failures (missing plugin deps, E0255 pub commands, missing icons, frontendDist), frontend shell, release binary launches and renders; smoke test still 66/0 |
| 2026-09-23 | 0.1.6 | Two players, live | Rust host drives the core: FFI session, lockstep threads, raw-byte frame pump, keyboard+touch input, in-app link selftest passed both ways, DS-rate pacing (59.8fps), three stacked linker failures solved, alpha 0.1.5-alpha.1 |
| 2026-09-23 | 0.1.7 | Mesh selftest, hidden touch | Full-mesh link selftest (2/6/12 pairs pass at 2/3/4 players), input verified by human, bottom screens hidden by default with Tab toggle, mesh drain pattern documented |
| 2026-09-24 | 0.1.8 | Per-player peek flip | Hold-to-peek flip keys ship as backend data, Tab = layout-mode switch, touch always mapped to the (even hidden) bottom surface, virtual pen cursor; scripted browser verification caught four frontend bugs |
| 2026-09-24 | 0.1.8a | Real-ROM retest | User's Test Roms verified gitignored then used: NSMB 2P link play byte-exact on current bridge (3.69x), MKDS boots and runs in-app in the flip layout at 59.8fps with both slots on the bus; scripted MKDS lobby joins are timing-fragile (wizard/sav/NORMAL-vs-SIMPLE traps) - interactive test is the next proof |
| 2026-09-24 | 0.1.8b | Link watchdog | User's human MKDS lobby join froze repeatedly; log proves emulators healthy while game WiFi stalls; bridge gains per-kind packet counters, [link-event] timestamps, STALL/IDLE/ACTIVE health classification (MD_LINK_VERBOSE); MITM echo manager designed against mgba-splitscreen's echo-only-during-discovery lesson |
| 2026-09-24 | 0.1.8c | Channel fix | Watchdog auto-kick cured a live STALL unattended (23min ACTIVE after); root cause isolated to RF channel mismatches (consoles hop channels, CheckRX drops foreign-channel frames); fix = channel-transparent virtual air (restamp rx frames with receiver's channel); per-console core-log tags added |
| 2026-09-25 | 0.1.8e | Console identity, frame trace, reply window | User's "froze up trying to get p2 to find and join" run read back from /tmp/md_app.log: no freeze (59.8fps throughout, UI 3s, clean exit) - the games simply never talked (1-3 frames per lobby attempt in 7 minutes). Root cause class: every instance boots the same firmware, so all consoles presented the identical MAC and a joiner could not tell the host's room from its own console; bridge now assigns a distinct MAC per slot (upstream `EmuInstance::customizeFirmware` offset scheme) at ROM load; `MD_LINK_VERBOSE=1` now narrates every bus frame from both ends (fc/addresses/sync/mask), which is what makes the next lobby attempt diagnosable; health line's misleading `wifi N/M` relabelled `wifi on/off` |
| 2026-09-25 | 0.1.8f | Reply window in the wrong clock domain | Third identical MKDS join failure read back with numbers: the client sent 117 replies, the host collected 71, and 52 of 123 collection rounds came back empty - so replies were neither lost nor discarded, the host was giving up before they arrived. `LocalMP::RecvReplies`'s staleness bound had been fixed to one frame (zero `DROPPED a reply` lines since), but the wait itself was still `md_link_set_reply_timeout(2)` **wall-clock** ms, i.e. 0.8ms of *emulated* time at the measured 2.4x - less than the 1-2ms DS slot window it stands in for and far short of the client's next frame boundary, where the answer actually goes out; a host that times out says `clientfail=0002` and the game drops the client. Now 34ms (two emulated frames), `MD_REPLY_TIMEOUT_MS` to sweep, wait still returns the moment a reply is posted; pacing regression-checked on the harness (2.48x vs 2.41x baseline). Collection improvement still needs a live join to prove |
| 2026-09-24 | 0.1.8d | Crash forensics + safe link ops | Discovered melonDS's ARM JIT owns SIGSEGV for fastmem and that re-asserting our handler stole it (app died at heartbeat #1 three runs in a row) - handlers now installed only where unowned and audited, never re-installed; crash reports with backtraces proven end to end (-rdynamic, deliberate ABRT/SEGV); launcher supervisor records exit status/signal for the faults no handler sees; all resync/probe/selftest bus access moved onto the consoles' driver threads (LinkOp queue) - cross-thread drains were stealing game frames; `md_link_recv_packet` i32/usize FFI mismatch fixed; bus never destroyed while threads hold it; lockstep with peer-death detection (no more silent hang); always-on link verification counters in the UI; 30s heartbeat + UI liveness + window lifecycle logging; 16.7-min NSMB 2P soak clean |

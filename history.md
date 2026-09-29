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

**Re-run 2026-09-28: it played again after v0.1.9r, and the two v0.1.9q parks
were the framing, not the route.** v0.1.9q's captures were taken with the wire
four bytes too long in every frame (the v0.1.9f append), and on that build the
route parked in the discovery state - both consoles on `Searching for <peer>...`,
`Please Wait.`, 4452 flawless rounds underneath. With the FCS moved into the
frame's own slot (v0.1.9r) the *same command and the same save* reaches
gameplay again, and every re-run since - including the 2026-09-28 v0.1.9s
control arm, 12000 frames, `all consoles kept running`, 9208 rounds, 262/266-byte
session payloads - has played. So read the coordinates below as corrections to
the *tap map*, not as evidence that the route stopped working: the park was a
transport bug that the route merely made visible. Two of the coordinates above
were still wrong, measured off the captures rather than
inferred: on the current build and save the two character cards on the *bottom*
screen sit at y ~ 56..80 and y ~ 100..134, so `(127,119)` picks the lower card but
`(127,160)` lands **26 px below it, on nothing** - and the second console's
character comes out of its *dialog* tap `(128,71)` instead, which happens to sit on
the upper card. Screen sequence and the coordinates worth knowing:

| screen | top | bottom | tap |
| --- | --- | --- | --- |
| character select | `Select a character.` | two cards, y 56..80 / 100..134 | either card's centre, e.g. `(127,69)` or `(127,117)` |
| searching | `Searching for <peer's character>...` | the connecting checkerboard | none |
| the finder's dialog | `melonDS <peer> has appeared. Play a game?` | Yes/No buttons | Yes `(128,71)`, No `(128,120)` |
| after Yes | `Please Wait.` | connecting checkerboard | none |

`md_game_session`'s frozen-picture detector is the instrument that makes a park
visible (`FROZEN PICTURE ... last change N frames ago`), and `tools/appctl.py
render <png> --mono 140 --bright` reads the text out of any `--shot-dir` frame,
with no app running (v0.1.9q).

**Re-run 2026-09-28 (v0.1.9r): the same command reaches a round again, and the
park it replaced has a measured cause.** The route above plays on the current
build - both consoles `running` to the last frame, the same course with each
console's own HUD, 262-byte game payloads in both directions for 200 emulated
seconds - once the transport stops appending its own FCS *after* every frame
(see the v0.1.9r row). `MD_MP_FCS=0` and the current default are now the same
framing, differing only in the four bytes inside the frame's own FCS slot, which
the receiving console's length entry excludes; the *appended* form (v0.1.9f to
v0.1.9q) is what parked both consoles on `Searching for ...`/`Please Wait.` The
screen sequence captured every 100 frames (`game-captures/nsmb-discover`, plus
`nsmb-fix` at 500-frame spacing) is the one a driver of this route needs:
each console's own `Searching for <peer>...` (frame 2600), each console's own
`<peer> has appeared. Play a game?` (frame 2800, two different frames - this is
the pair that says both games found each other), one shared screen on both
(frame 3000, byte-identical), the Vs. setup (4300) and the course (11500).

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
two emulated frames, `MD_REPLY_BUDGET_MS` to sweep - renamed
`MD_REPLY_BUDGET_FRAMES` and read as frames in v0.1.9ah, which is what the
value always meant; the harness: the same, or
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

### v0.1.8w - After the fix: the post-lobby phase is stable, and the two ways a join still ends (2026-09-26)

The same evening as v0.1.8v, answering "what is left once the guest stops dying".
The answer is two different endings, both of them the **host's game** giving up on
a client, and neither of them on the wire.

#### The phase the fix exposed is healthy

`tools/mkds_join.py --restart --no-dialogs --cut-off --vs`, then 40s of touching
nothing, sampled every 2s:

```
t+2s  rx=2373 ans=911  window reads=90761  host collected=908   deauths=0
t+20s rx=5697 ans=2107 window reads=212325 host collected=2105  deauths=0
t+40s rx=9183 ans=3308 window reads=338669 host collected=3304  deauths=0
```

Steady ~300 answers per 5s digest window, the host collecting every one of them,
zero deauths, the guest's payload at `00 80` (lobby/race-ready) with the counters
in both bodies walking forward. **The lobby-close → game-select transition is
stable for as long as nobody presses anything**, which is the state thirteen runs
of MKDS never reached.

#### Ending one: any *host* press on the post-VS screen tears the group down

Reproduced three times (A on P1 twice, START on P1 once): within a few seconds the
**host's** game sends deauth `reason 0001` to the client's MAC, then `reason 0003`
broadcast, both radios go off and both games park on a spinning PC. The guest's own
presses are *safe* - A on P2 alone leaves the session up for 18s+ with 0 deauths
and both screens still animating - so this is not "input kills it", it is
**host-side advance = cancel** on whatever screen MKDS is showing there (its top
screen is a black panel with one short bright message; `tools/glyphs.py` was
written to read exactly that, bright-on-dark, glyph by glyph).

What that screen *is* decides the next move. Candidates: the VS setup waiting for
the guest to be ready (in which case something on the guest must confirm first),
a course-select that wants a *course* pick rather than a confirm press, or a
timeout screen whose A really does mean "give up".

#### Ending two: a join can end before CUT OFF, the same way

One run in four with the new default never reached the lobby: the guest joined
(`guest set its AID to 1`, 13 rounds), received the 34-byte lobby announce
(`BE 01 02 00 0C 9F 06 00` + UTF-16 `melonDS`), answered `00 80 5A 5A B8 B6` - and
the host's game deauthed it. Thirteen answers is the same shape as the post-VS
ending: **the host stops being answered often enough and gives up on the client.**
The documented fragility of scripted joins is a separate thing (taps landing a
screen early); this one is the same timeout as the other ending.

#### Why the host is not answered often enough, and what that implies

With `MD_EARLY_REPLY=0` the guest's *game* arms on every round (`arms 6776`
against `cmds rx 6793`) but only about a third of the rounds get an answer out:
`answered 2510, blank 4283`. The stock reply window opens when the host's round
starts and the guest's arm usually lands after it, so the hardware sends the blank
in the slot the answer should have had. That is *precisely* the problem the early
reply (v0.1.8k) was added to fix - and the early reply is what starved the guest's
game in v0.1.8v. So the shape of the real fix is now clear: **an early reply that
leaves the guest's game in the state stock leaves it in** - arm consumed, TX status
word written, completion IRQ raised, reply-slot pointer and `TXBusy` moved - so the
answer goes out inside its round *and* the game's lobby state machine keeps running.
`MD_EARLY_REPLY_ACK` (modes 1/2, v0.1.8o) was the first attempt at that and it is
not sufficient (mode 2 still left `arms 1`), so the difference is somewhere else in
the early path, and the digest's `arms` counter is the instrument: `arms 1` for a
whole lobby means starved, `arms` tracking `cmds rx` means participating.

#### Tooling added

`tools/glyphs.py`: segments a text line out of a screenshot and prints one block
per glyph, in dark- or bright-ink mode. `appctl.py bands` says where the text is,
`render --mono` says what the screen looks like, and this says what the words are -
which is what the host's post-VS message needs before the next experiment is chosen.

---

### v0.1.8v - The early answer was starving the guest's game (2026-09-26)

The sixteenth live attempt, and the one that closes the question this project has
been chasing since v0.1.8m. **The cause was ours, it was one environment
default, and it had been in every MKDS run since v0.1.8k.**

#### First: the failure was never intermittent, and v0.1.8u's "the guest survives" reading was wrong

v0.1.8u recorded a hands-free tap run in which "the guest survived 16s". It did
not. The digest's role field (`mp p2 [guest]`) is only cleared by a deauth the
console *receives*, so it still reads `guest` after the guest's own game has
left - and the join driver's `p1=host p2=guest` line is built from that field.
Grepping for the *event* instead (`wireless off (end #2)`, `deauth sent`,
`guest set its AID to 0`) shows the tap route reproducing the death in **every
run**: the guest's game deauthenticates 4-5s after its radio comes up, with the
watchdog quiet and the wire perfect. Two separate user runs and four of ours
have the same shape; nothing about the route matters.

#### The death's shape, and the body dump that pinned the trigger

Both consoles' deaths - the user's button-driven ones and our tap-driven ones -
are byte-for-byte the same story:

1. the host's game sends its periodic broadcast (`p1 tx bcast len=120 chan=7
   fc=0080`, every ~209ms, 802.11 seq +0x130);
2. the guest's `CheckRX` hands it to the guest's game as an MP client frame
   (104 bytes);
3. **19ms later** the guest's game sends deauth `reason 0003` to the host, then
   writes AID=0 and switches its radio off. Both games park on a spinning
   program counter; the emulator itself is healthy.

The trace showed lengths but not payloads, so the delivery log grew a
hex-plus-text dump of the first 48 bytes of every frame of 48 bytes or more.
That settles what the host's broadcast actually is: a **real 802.11 beacon** -
`rates 82 84`, `DS parameter channel 7`, a `TIM` element, and a vendor IE
(element 221, OUI `00:09:BF`) carrying the MP payload - i.e. the game's own
software AP, unchanged by us. The nickname rides separately, in 34-byte
directed commands whose bodies read `BE 01 02 00 0C 9F 06 00` + UTF-16
`melonDS` + a counter byte, matching the v0.1.8r decode. **Nothing in what the
guest is handed is malformed.**

#### The discriminator was `arms`, and the culprit was `MD_EARLY_REPLY`

What separates a dying run from a live one is not the wire, it is whether the
guest's *game* is doing anything:

| | early reply ON (default until this version) | `MD_EARLY_REPLY=0` (stock) |
|---|---|---|
| guest `arms` for the whole lobby | **1** | **9792** (re-arms every round) |
| guest `aids` | 2 | 6757 |
| guest answer body | frozen at `00 00 5A 5A B8 B6` | moves - `00 00` -> `00 80` (lobby-ready) |
| guest `window reads` | 6345 | 365859 |
| host collected / empty | 3375 / 14 | 3375 / 14 |
| **deauths** | 1, 4-5s after the join, every run | **0** |
| after CUT OFF + VS | guest gone, both radios off | both consoles still linked 30s later |

`SendMPEarlyReply` transmits the armed template *on the guest's behalf* inside
the round the command arrives in. The round-end path still does the hardware's
bookkeeping, but the guest's own driver never gets the event its lobby state
machine is waiting for: it arms its answer once and then sits there while our
fast path answers 248 rounds for it. Five seconds of that and MKDS decides the
room is dead and leaves. With stock `SendMPReply` (arm consumed, TX status word,
completion IRQ, one round at a time) the game re-arms every round and the lobby
runs; the ~65% blank replies that result (`answered 3378, blank 6417`) are what
the game's own pacing looks like, and MKDS is content with them.

**The fix is the default, not a new mechanism**: `MD_EARLY_REPLY` now defaults
to **off** (stock melonDS), with `MD_EARLY_REPLY=1` restoring the fast path for
A/B. That is faithful *and* it is the only safe default: New Super Mario Bros
plays 12000 frames at 3.29x realtime with the early answer on, which is exactly
why thirteen runs of review never caught this. It is a guest-lobby hazard, and
only a game that keeps a guest sitting in a room exercises it.

#### Verified on the default build

`tools/mkds_join.py --restart --no-dialogs --cut-off --vs --watch 25` with no
environment knobs at all: P2 joins, the lobby runs, P1 taps CUT OFF and VS, and
**25s later both consoles are still linked** - `deauths 0`, guest `arms 6140 /
aids 4306 / window reads 230907`, guest answer `00 80`, host `collected 2150`,
1 kick. Smoke test 66/0. The gate this project set in v0.1.8e ("P2 joins P1's
room and survives the lobby close into the game-select screen") is met for the
first time.

#### The new frontier, and what the next run has to answer

Pressing A on the game-select screen (after CUT OFF + VS) still ends the session:
the **host's** game sends deauth `reason 0001` to the client, then `reason 0003`
broadcast, both radios go off and both games spin. Kicks correlate (1-3 room
resyncs in those runs, from the 10s-window stall watchdog), which suggests the
quiet loading phase after CUT OFF looks like a stall to our monitor and the
resync lands in the middle of the transition. Candidates, in order:

1. **The watchdog's kick during the post-lobby quiet phase.** `KICK_AFTER_WINDOWS
   = 2` (20s) fires while the host's game loads, re-anchoring a FIFO the game is
   mid-transfer on. Test: raise the threshold (or make it env-tunable) and see
   whether the transition survives.
2. **Both consoles have to confirm.** MKDS's game-select screen is a synchronised
   choice: the host's A may only mean "leave" if the guest has not confirmed
   too. Test: press A on both consoles, in both orders, and read the screens
   with `render --mono`.
3. **The lobby link is supposed to be torn down when the race starts.** If MKDS
   legitimately ends the *lobby* group and builds a new one for the race, the
   deauth is normal and what fails is the rejoin - in which case the next thing
   to watch is whether a fresh beacon/association follows the teardown.

#### Tools that made this session possible

* `tools/appctl.py render PNG --mono 110 --crop X Y W H --zoom 1`: a **threshold**
  renderer (dense = dark) on top of Pillow, one character per pixel. The ASCII
  ramp on its own cannot resolve an 8px DS font; this reads menu text directly,
  which is how the MKDS layouts were confirmed instead of guessed.
* `tools/scripts/mkds-user-route.txt`: the user's button-driven route replayed
  with its original 1-3s spacing (with the note that the first A only lands
  correctly ~23s after launch).
* `tools/mkds_join.py --no-dialogs`: the first-time nickname/emblem dialogs are
  consumed by the save file, so the default `--dialogs 3` now overshoots and
  turns P2 into its own host. Any hands-free join from this save must pass
  `--no-dialogs`.
* the delivery body dump in `Wifi::FinishRX` (48 bytes hex + text for frames
  >= 48 bytes), which is what turned "the guest leaves after the host's
  broadcast" into "the guest leaves 19ms after a well-formed beacon".

---

### v0.1.8u - The lobby-close death reads itself out of the flight recorder, and the app becomes drivable from a shell (2026-09-26)

Fifteenth live attempt, first on the v0.1.8t flight recorder, and the first
failure that explains itself from the log it left behind. Two things shipped:
the read-back of the death, and the test-drive channel that ends the era of
*"every MKDS join costs a human"*.

**The failure (user: "errored out when p1 tried to close the lobby and progress
the game") - back at the lobby exit, and now legible.** MKDS, 2 consoles, a
fresh session, 12:44 local. P2 joined P1's room and the lobby ran healthy for
about four seconds: **664 commands received, 658 answered** (6 blank), every
answer an early in-round reply, **0 stale, 0 empty windows**, host collected 658
with 8 `clientfail` acks - transport at NSMB-reference quality, no retries, no
drops anywhere in the recorder. Then P1 pressed A to close the lobby (the log's
`[input] player 1 buttons -> A`), and ~2s later, at +14.50s:

1. **The guest's game wrote AID = 0 itself** - `[mp] guest set its AID to 0
   (round state 0, cmd seq 5650): it has nothing to answer with`. AID is the
   game's own register write; this is the game leaving the network, not the link
   losing it.
2. The guest sent **deauth, reason 0003** to the host (`0009BF126643 ->
   0009BF112233`, 30-byte frame, *"body is not the usual 2-byte reason code"*),
   then `[CLIENT] deauth`, `WIFI: OFF`.
3. The host answered with its own **broadcast** deauth (reason 0003, 30 bytes),
   then `WIFI: OFF`.
4. Neither radio ever came back on. Both games went to a PC-spinning stall the
   health thread flagged as `looks wedged: the program counter moved in only
   8/100 samples while 300 frames ran` - the emulator running perfectly, the
   games parked on the screen a dropped session leaves them on.

Same wire signature as v0.1.8q's 4.29s death, now with the order of events and
the responsible party on the record: **the guest's game decides to leave, the
host's game tears its access point down in response, and both games stay down**.
Nothing in the transport is implicated - which kills the v0.1.8t hypothesis
("a packet transmission failure somewhere") for this run too. What is still
unknown is *what the guest's game saw* in the two seconds before it left, and
that is a question about pixels on a screen, which is what the second half of
this version is for.

**Drivable: the test-drive control channel (`MD_CONTROL_PORT`, tools/appctl.py).**
The request was blunt: *"is there a library you are able to add to be able to
test drive the tauri app yourself? I would like to empower you to be able to
press buttons, capture screenshots, and interact with the app."* The honest
answer for this machine was *not* the usual tools. It is a Wayland session:
`xdotool` is not installed and X11 synthetic events would depend on compositor
focus, and `tauri-driver` would need `webkit2gtk-driver` from apt plus a
`cargo install`, and would still only drive the web UI, never the emulated
consoles. The webview is reachable directly, though - Tauri's own `eval` and
`invoke` do not care about Wayland - so the channel is 250 lines of Rust in
`lib.rs` (off unless `MD_CONTROL_PORT` is set, loopback only) plus a stdlib
Python client. No new crates, no new system packages.

What it can do, all of it exercised live in this session:

| command | what it does |
| --- | --- |
| `status` / `ui` | session, frame counters and link mask from Rust; the window's own status line, active console and canvas count from the frontend |
| `see` | **the emulated screens as ASCII text** - the renderer inverts the luminance ramp because DS menus are dark text on bright panels, and `--crop x y w h --aspect 1` gives one character per pixel, which is the only setting that reads an 8px DS font |
| `shot` / `window` / `render` | PNG of every console's two screens (512x384 composite) next to the log in `logs/shots/`; full-screen capture through KDE's spectacle; and any PNG back to ASCII through Pillow |
| `press` / `input` / `touch` / `untouch` | hold DS buttons for a given time, inject a raw button mask, put the pen down at a coordinate - all through `set_input`, the same command the UI calls |
| `key` / `tap` / `index` / `click` | real KeyboardEvents dispatched at the window (the frontend's own keymap and handler run), select which console the keyboard drives, click UI elements by CSS selector |
| `eval` / `text` | arbitrary JS in the page; read any element's text |
| `advance` / `waitfor` / `sleep` | wait for a console to actually run N frames (boot, loading), wait for text to appear in the status line |
| `script` | a file of the above, one command per line - the shape a hands-free MKDS run will take |

Every control command is logged as `[control] <cmd>: <result>` in the same log
as everything else, so a scripted test drive and the post-mortem read the same
story. Verified end to end on the live app: `ping` -> `pong`; `press start
--player 1 --ms 120` produced the app's own `[input] player 1 buttons -> START
(mask 00000008)`; the screens changed; `shot` wrote a 52 KB PNG; `see` rendered
the MKDS title screen and then a four-row menu; `window` captured the whole
desktop, app window included.

Why the ASCII reader matters more than the screenshot: an agent can *see* a
PNG exactly as well as it can see a photograph - not at all. Text it can read.
A 256x192 screen at 128 columns is enough to tell a four-row menu from a dialog
from a race countdown, and cropping one text line at one character per pixel is
enough to read the menu item itself.

---

### v0.1.8t - The run whose log a reboot ate, and the flight recorder built so that cannot matter again (2026-09-26)

Fourteenth live attempt, and the first one that cannot be read back at all.

The user reports a **new** failure point: *"this time p2 errored out before p1
could close the lobby and continue to game selection"* - i.e. the session now dies
during the join itself rather than at the lobby exit the previous runs died on
(v0.1.8q/r/s all reached a complete, healthy join and then lost the guest's game
3.4-4.3s later). Their hypothesis: a packet transmission failure during the join.

That run's log is gone. It was `/tmp/md_app.log`, `/tmp` is tmpfs, and the
machine rebooted between the run and the read-back - taking `md_app.log`,
`md_app.log.prev`, the four gzipped previous runs and `/tmp/md_rt/` with it. This
is the same class of loss `tools/run_app.sh` was written to prevent in the first
place ("written after losing three sessions to 'the window vanished and the log
just stops'"), which makes the fix straightforward rather than a mystery:

* **Logs live in the repo now.** `run_app.sh` defaults to
  `<repo>/logs/md_app.log`, keeps `<log>.prev` beside it, and gzips each outgoing
  run into `<dir>/archive/` with a timestamp (last ten kept). `MD_LOG` still
  overrides, so nothing that passes an explicit path changes behaviour. `logs/`
  is gitignored: it is chatty (>1MB a run with `MD_LINK_VERBOSE=1`) and
  not something to commit, but it must survive a reboot, and now it does.
* **The next run has to be self-diagnosing**, because the alternative - one human
  join attempt per instrument, read back by hand from a multi-megabyte log - is
  the thing that has made this go in circles. See the flight recorder below.

Worth recording for the *next* time a run is lost: nothing else about a session
is durable. The app writes no state file, the captures were not on this run (they
need `md_capture`), and the crash-report files `/tmp/md_crash_<pid>.txt` are in
the same tmpfs. Any evidence we want from a live attempt has to be in the log,
and the log has to be in the repo.

#### Before spending the next human attempt: is MKDS local wireless even possible on melonDS?

Thirteen attempts is enough of a sunk cost to check the premise, so this was
looked up rather than assumed. **Yes - MKDS local wireless works on stock melonDS
0.9.5 and later**, which changes what our failure means:

* melonDS 0.9.5 (Nov 2022) is the release that "improved local multiplayer".
  GBAtemp's write-up of it names MKDS as the *example* of the old problem: the
  previous versions had local multiplayer "but as Arisotura says it, it was
  'finicky', with one example being Mario Kart DS staying connected...". So the
  exact failure we have been chasing - MKDS connects, then drops - is the one
  0.9.5 was released to fix.
* A hands-on write-up of 0.9.5 (arcadestrikerblog, 2022-11-21) lists MKDS,
  including romhacks, as playable, and says MKDS was tested **with four
  instances**; the games it flags as still fragile are Contra 4 and the
  touchscreen-heavy ones.
* The melonDS board's MKDS thread is older (2021, pre-0.9.5) and says local
  wireless is "screwey and unstable", with an admin noting it is "more or less
  entirely reliant on the OS scheduler executing the individual melonDS
  instances right". That is the 0.9.1-era answer, and it is also the one
  architectural hint that matters to us: upstream's design is **one OS process per
  console, each paced by the host OS**, while ours is one process with a
  **frame-level lockstep barrier**. The DS's MP rounds are ~448us of shared
  timing; a barrier that only aligns consoles to the same *frame* leaves them at
  whatever sub-frame phase they reached - which is exactly the 1.5-25ms US-clock
  "lead" v0.1.8j measured, and exactly the quantity the deferred-delivery path
  clamps away (`if (RXTimestamp < USTimestamp) RXTimestamp = USTimestamp`).

So the working hypothesis is no longer "MKDS is unsupported"; it is that our
consoles are not slaved to a shared time base the way separate OS processes
effectively are. That is a testable claim, and the harness is the wrong place to
test it (it cannot script MKDS's lobby - see below); the live run is.

#### The flight recorder (what changed in this version)

Every question the last thirteen read-backs had to answer by grep is now a
counter in the core, exported through one bridge call (`md_link_mp_digest`) and
printed by the host's health thread as **two lines per console per window**:

```
mp p2 [guest]: cmds rx 470(+95, dup 12) · answered 462(+95, early 462, blank 8, canned 0)
  · arms 3 aids 4 · window reads 261(+40) · replies sent 470 collected 470(+95) empty 3(+1)
  · stale 0 reanchor-dropped 0 blank 0 notlooking 0 · deauth out 0 in 1
mp p2 last: cmd seq 0140 len 6 body 00 00 06 00 B8 B6 at +3.21s · answer len 2 body 00 00 at +3.20s
  · last deauth reason 0003
```

What each part is for, in the order the runs have needed them:

* `cmds rx` / `answered` / `early` / `blank` / `canned` - the v0.1.8h-k questions
  (is the guest being told a command arrived, is it answering, is the answer its
  own or the firmware's `fc=0158` stand-in). `answers_early` is counted where the
  early reply actually leaves, `arms` where the guest arms a reply slot, and
  `aids_set` on the W_AIDLow changes that gate the whole path.
* `window reads` - the v0.1.8s deciding instrument (does the console's *game*
  ever read the wireless receive RAM window), now a number in the digest instead
  of a throttled log line you have to count by hand.
* `replies ... collected / empty / stale / reanchor-dropped / blank / notlooking` -
  the host-side half, counted inside `LocalMP`'s reply queue rather than inferred
  from the wire, with the four different ways an answer can be lost kept apart
  (dropped at the re-anchor, dropped as stale, delivered blank so the ack calls it
  a failed client, or never looked for because the bus thinks no peer is attached).
  `replies sent` is the guest's own count, so "the guest answered every round and
  the host's game was told it failed" is visible as a disagreement between two
  adjacent numbers instead of a conclusion after an hour of reading.
* `cmds tx (dup)`, `acks (clientfail)` - the host's retry loop and its own view of
  its peer, which is what the game actually looks at before deciding to deauth.
* The **last bodies** on the second line, for both directions, with the US-clock
  time they were seen. Thirteen runs were spent learning that neither body moves
  after the name phase; the next run says so in one line, and says it *after* the
  failure too, because the digest is printed again the moment a console's
  multiplayer state changes - radio on or off, a deauth sent or received, its role
  changing - rather than waiting for the next five-second window. A run that dies
  at second three now leaves its cause in the last four lines of the log.

Cost is a handful of increments per round (the counters are always on) plus two
log lines per console per five seconds; the one place a counter sits on a hot
path, the wireless-window read, is a `u64++` next to a comparison that was
already there. `md_smoke_test` is 66/0 on the build.

#### Calibrating it against the reference before trusting it on a live run

A counter that lies is worse than no counter, so the digest was checked on the
one session that is known to work - the NSMB two-player script, same save, 12000
frames - with `md_game_session` now dumping the same digest at the end of a run:

```
  player 1 [guest]: cmds rx=9208 (dup 9207)  answers=9208 (early 9208, blank 0, canned 0)
            arms=1  aids=1  window reads=630708  deauth out/in=0/0
            replies sent=9208  collected=0  empty=0  blank=0  stale=0  reanchor-drop=0  notlooking=0
            last cmd: seq=AEB0 len=6 body[DE 01 02 00 00 80]  last answer: len=6 body[00 00 5A 5A B8 B6]
  player 2 [host]: cmds tx=9208 (dup 0)  acks=9208 (clientfail 0)
            replies sent=0  collected=9208  empty=0  blank=0  stale=0  reanchor-drop=0  notlooking=0
            last cmd: seq=AEB0 len=10 body[DE 01 02 00 00 80 0F 00 B8 B6]
```

Every number matches the wire counters the same run has always reported (9208
answered and 9208 collected is the v0.1.8p "99.7% collection, 0 canned/stale"
state, now with no empty windows at all), and three details are worth keeping:

* **`dup` does not mean what it looks like it means.** The reference repeats one
  CMD seqno for 9207 of its 9208 commands and that is *normal* - melonDS's CMD
  retry mechanism reuses the seqno, and a polling host does exactly this. The
  interesting signal is the pair: a repeated seqno *and* a frozen body. Read `dup`
  as "same seqno as the previous command", never as "the game is stuck".
* **The two consoles' bodies differ by the FCS**, as they should: the host's view
  of its own command ends `0F 00 B8 B6` where the guest's received copy ends
  `00 80`. That is `W_RXLenCrop` stripping the 4-byte checksum on receipt, and it
  is the same 6-vs-10 asymmetry v0.1.8m recorded from the other direction. When
  comparing bodies between the two consoles, compare the first
  `last_cmd_len - 4` bytes.
* **`window reads` is enormous when a game is really reading** (630k for the
  guest, 239k for the host over 200 emulated seconds) - so "0 window reads" on a
  live MKDS guest is a real signal and not just a slow counter.

The one thing the harness cannot calibrate is the *host* half of a bad session
(`clientfail`, `reanchor-dropped`, `stale`), because a working session has none of
them. Those numbers come from the live run, and the first thing to check will be
whether they are rising while the guest's `answers`/`replies sent` stay level.

#### What the next run has to answer

With the log now surviving the run, the digest should decide between the three
candidate causes of "p2 errored out before p1 could close the lobby", none of
which the lost run can be asked about any more:

1. **The host's game kicked the guest** (our acks told it the client failed, or it
   never saw a valid answer). Signature: `acks (clientfail N)` climbing on p1 with
   `answered` climbing on p2, then a deauth *out* on p1.
2. **The guest's game gave up** the way v0.1.8q/r/s showed, only earlier. Signature:
   a deauth *out* on p2 with reason 0003, and - the deciding number - `window reads`
   on p2 either frozen (delivery/IRQ problem, ours) or climbing while `answered`
   stays flat (the game is reading the mail and refusing it, game-level).
3. **A transmission failure in the join**, as the user suspects. Signature: the
   wire counters (health line, not the digest) losing frames in one direction
   while both games are still trying, with no deauth on either side until the
   timeout.

None of these is a guess that costs another instrument: all three are decided by
the two digest lines that will be sitting next to the failure in
`logs/md_app.log`.

#### Still open / next

* The harness still cannot script MKDS's lobby (the save-state wizard, the
  NORMAL-vs-SIMPLE start menu at `(128,68)` vs `(67,145)`, and `--touchtap`
  repeating forever), so MKDS joins still cost a human. That is the next thing
  worth automating: an in-app "route" mode that injects the same touches the
  human makes would turn a 10-minute manual join into an unattended run, and the
  500ms digest window is already fast enough to catch the moment it dies.
* Nothing has been committed; `logs/` is new and gitignored.

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
| 2026-09-29 | 0.1.9aj | The mission's game-by-game record exists as a file a reader can find, the next work is written down in the order it has to happen, and the accuracy-for-playability trade is a stated product decision rather than an assumption - and all of it is committed, the first commit since `cf080a1a` | Asked to (1) commit and push the accumulated progress so it cannot be lost, (2) add a `compatibility.md` listing the games tested and whether and how well they work, (3) add to `to-do.md` what is needed next to "fix" the multiplayer link for working 2, 3 and then 4-player sessions, and (4) treat **stability and playability as outranking emulation accuracy** - changes to the emulation are in scope when they let more games run, with 4 players, on the least powerful hardware, with the widest compatible game set. **The per-game record, and the honesty that had to go into it.** `compatibility.md` is new: a summary table plus a section per title, each citing the log or capture it comes from, with four explicit outcomes rather than a pass/fail - **NSMB ✅ playable at 2** (race reached, the guest's press measured in the host's own pictures over four runs, link `deauth 0/0` with 7,916 commands answered 7,916 times), **MKDS 🟡 partial at 2** (join, CUT OFF and VS all land and the link holds 2,687 commands with `deauth 0/0`, then both games stop on their own park screen where A, START and a touch leave both framebuffers byte-identical while the input demonstrably arrives), **Metroid Prime - Hunters 🟡 partial at 2** (a byte-complementary 7,986-command exchange and both consoles in the same lobby, then `SELECT HUNTER` for 117 emulated seconds and no match), **Star Fox Command ⛔ and Super Mario 64 DS ⛔** (their only local mode is Download Play, so the joining console is the firmware menu's client, which FreeBIOS cannot boot - a product decision, not a bug), and **LEGO Star Wars II ⬜** (18,000 frames on two consoles with no multiplayer entry anywhere in its menus). The file states plainly what "works" means here - a real in-game match, not a live link - and lists the limits of the method: the peer's world is still asserted from pixels because the app has no memory read, and several figures need the game in a specific phase and correctly report `inconclusive` outside it. **The roadmap, and the instruments it is gated on.** `to-do.md` gains a section at the top, "The road to a working 2, 3 and 4-player link": the standing trade (with the five departures the link already spends, in a table, and what the permission does *not* cover - silently losing data a game waits for, or players' saves diverging), five instruments to build before anything else (a **memory-read command on the control channel**, the open item every pass since v0.1.9y has named; an explicit player selector on every `appctl.py` input path, which `key` still lacks; `press`/`touch` writing a console's whole input state; `md_touch_probe` exposed so a guest-visible pen can be proven; and a 3/4-console digest diff), then **Milestone 1** (MKDS past the park, three named routes to try; MPH lobby to match; the rare `reason 0003` NSMB drop made reproducible; the app's reply window made frame-locked; and one match asserted from a game's own RAM), **Milestone 2** (make `LocalMP`'s `MPHostinst`/`LastHostID` routing per-instance - the leading suspect for the two-joiner break, a correctness fix rather than an accuracy trade - then re-run the three-console join with two joiners and check AID/reply-slot allocation beyond two clients, with a 2-player regression on every change), and **Milestone 3** (the same at four, the reply backlog under three askers, the least-hardware budget - 4 linked 3D consoles run at ~2.2x realtime today, and the goal is headroom, not parity - input at four, and a shared-save model). README and AGENTS.md point at both new sections so the next reader lands on them, and AGENTS.md records the accuracy trade in its DO list. **Validation and hygiene, and what is deliberately not claimed.** No C++ and no Rust changed this pass; smoke `md_smoke_test` is **66 checks, 0 failures**, `python3 -m py_compile` passes on the three drivers, and the tree was committed whole (`git status` clean afterwards) with `logs/` and `game-captures/` still ignored so 456 MB of run logs and 1.6 GB of captures are not in it, and no BIOS or ROM. Every figure in the new file is one already recorded in this table with its evidence file named - nothing was re-measured this pass, so nothing new is claimed, and the gaps that stay open (a match asserted from a game's memory, the MKDS park, MPH's match, 3 and 4 players) are written as open with the item that would close each. |
| 2026-09-29 | 0.1.9ai | A player's input is attributed to the *peer's* picture in the app's own New Super Mario Bros. race - with the guest alone holding RIGHT and the host's own input trace empty, the host console's rendered top view moved 1,021 px (a band at y 155..178) and the shared bottom screen 1,857 px that moved in neither no-input baseline, reproduced in three runs and mirrored by the host's own press - and the test that measures it refuses to claim from noise | Asked to take, on the app's own live session, the one measurement the whole mission still rested on inference: reach the coupled course state, baseline each console's picture with no input, then hold one direction on the guest alone with the host sending nothing, sampling fast enough to separate the guest's input from the game's own animation, and if the two cannot be separated to say so with the numbers instead of calling it proven. **The instrument, and why it can separate them.** The old coupling test could not: sampling over the control channel took one sample per 250 ms round trip, and a 2.5 s no-input baseline produced 10 distinct pictures per console, so "the picture changed" meant nothing (v0.1.9ag). The sampler now runs *inside the page* (`SAMPLER_JS`, sent as one `eval` - the channel is not asked per sample): one `__mdctl.frames()` per iteration, 64-74 samples/s against a 59.8 fps session, and a pair is counted **only when both consoles advanced exactly one emulated frame**, so every number is the game's own one-frame delta and not the sampler's. Each phase returns a bitmask per console and screen of every pixel that moved (one bit per pixel, so the intersection and the difference are byte operations) plus per-pair moved-pixel counts; the two no-input phases are the baseline, and the question is which pixels moved under one console's input and in *neither* baseline. Four phases of 4 s: A no input, B the guest alone holds RIGHT, C no input, **D the host alone holds RIGHT** - the mirror is what makes a null readable ("the host's screen does not show the guest" and "the host's screen does not respond to anything in this state" look identical without it). **Where each input landed is joined on the frame, not on phase order:** each phase reports the frames its pairs covered and the app's own `[input] frame N player P` trace says when every line went out, so "the host sent nothing in the guest's phase" is checked - phase B owns `p1 1` and the host's trace is empty there, phase D owns `p2 2`. **The measurement, four runs (12:00, 12:07, 12:14 - the third after every fix, `logs/nsmb_join_fine_canon.log` - and 12:22, which also ran the coarse test).** Roles read off the digest, not assumed: `{1: 'guest', 2: 'host'}`. Identical shape each time, input-only pixels (top, bottom) - the guest's own screens under the guest's input: **(30512, 1927)**, **(29000, 1895)**, **(31679, 1874)**; the host's screens under the guest's input: **(1021, 1927)**, **(1739, 1910)**, **(1021, 1857)**; the host's own screens under the host's input: (32632, 1943), (31311, 1880), (35361, 1879); the guest's screens under the host's input: (526, 1943), (385, 1842), (582, 1874). A fourth run (12:22) reads (18581, 711) / **(0, 740)** / (31595, 1747) / (627, 1746): the guest walked less in it, and the host's *top view* did not move at all while the shared map still did - so the robust half of this result is the shared map and the mirror, and the host's own view reacting is three runs out of four, not four. The no-input baselines for the host's top screen were 611 and 612 px over a whole 4 s phase - i.e. ~3 px per frame, against 1021-1739 px that never appeared there - and for **both consoles' bottom screens, 0 px in every baseline**. The top-screen change sits in a band at y 155..178 (bbox (199,155)-(255,178) and (167,155)-(255,178)) and the mirror's on the guest's side is the same band (x 0..200), which is the game's own layout, not a sampler artifact. The 2x2 is the whole argument: each console's own view responds to its own input (18,500-31,700 px, and 31,600-35,400 px for the host), each responds to the *other's* input in a small, localized way, and neither cross-effect is an animation the baselines happened to miss because the mirror is the same shape (the guest's pictures move 385-627 px under the host's input in all four runs). **The bottom screen moved in all four runs (1927, 1910, 1857, 740 px) and the host's own top view in three of them (1021, 1739, 1021, 0)** - the fourth run's guest was walking less, so its cross-effect is smaller everywhere, which is what a causal effect should look like and is why the run's own report leads with the 2x2 rather than with a single number. **The shared map, stated precisely.** The two consoles' bottom screens differed by **0 px per pair** in both input phases (and by 0 px in the run's own capture, `game-means 191.8` both), while their top screens differed by 24,000-26,600 px per pair - so the bottom screen is *one* picture both consoles render, and its 1,857-1,927 px is one picture moving rather than two views agreeing. The attribution to the host's *game* therefore rests on two things together: the host console renders that picture from its own game state, and its own top view moved 1,021 px under the guest's input alone. The counters were healthy throughout (`link mask 0x3`, guest `answered 12104`, host `collected 12100 empty 0 clientfail 4`, `deauth 0/0`). **Three defects in this pass's own instrument, all found by being wrong rather than by inspection.** (1) **The verdict claimed attribution from noise.** `if sum(cross) > 0` printed `attributed ... 11 px` for a session ten minutes on whose guest's own screens had moved **6 px** under the guest's own RIGHT - a state that cannot answer the question at all. The control now gates the claim (the pressed console's own screens must move >= 100 px) and the cross-effect must reach 100 px, both far above that noise floor and far below the 29,000-31,700 px the working runs show; the refusal was then exercised on real data (a follow-up run on the same session: control **1 px**, `VERDICT: inconclusive ... nothing is claimed about the peer`). Without this gate the pass would have shipped a claim built on eleven pixels. (2) **The input attribution window was too tight.** A line is logged when the channel applies it, and the sampler's first pair is a frame or two later, so the host's own RIGHT press (frame 14907) fell outside the phase D window (14908..15145) and the run reported "input lines in that window: none" for the very press it was looking for - which reads as "the host sent nothing during its own input phase". Lines are now attributed to the nearest phase window within 8 frames (`INPUT_JOIN_SLACK`), and the gap between windows here is under 3 frames. (3) **A one-letter shadow crashed the analysis:** the attribution loop reused `d` for its frame distance, overwriting phase D's report, and the next use of it died with `'int' object is not subscriptable` (renamed `gap`). (4) **"Nearest window" mis-attributed a press across a phase boundary.** A line takes effect a frame or two before its phase's sampling starts and a frame or two after the previous phase's ends, so distance alone is the wrong rule: the host's own RIGHT for phase D logged at frame 15271 against C ending at 15270 and D starting at 15273, and the report read "phase C owns p2 1" - input inside a no-input baseline that the driver never sent. Lines are now attributed only when they fall *inside* a phase's covered frames, everything else is printed as "at a phase boundary ... (nearest window X)", and the per-console totals with their frame ranges are printed so the claim is checkable by eye (a re-run printed `the guest console's own input over the whole measurement: 2 line(s), frames 24678..24919` against `the host console's own input: 1 line(s), frames 25160..25160` - the host's only line is 481 frames after the guest's phase closed). **The coarse test it replaces was also wrong, and had been for a pass:** `couple_test` hard-coded player 2 as the guest and player 1 as the host, so it pressed the *host* while reporting "the guest alone holds RIGHT", then counted the *guest's* input lines to prove "the host sent nothing" - on this ROM the digest says the opposite (`p1 [guest]`, `p2 [host]`). It now reads the roles off the digest. **What is still open, and what this does not claim.** A value out of NSMB's own RAM for "the peer's world changed" - the app still has no memory-read command, so the full-frame instrument is what exists. And the measurement needs the game in the race: ten minutes later in the same session the guest's RIGHT no longer moved the guest's own screen (1 px), which the run now reports as `inconclusive` rather than as a null about the app; future runs re-drive the route rather than reusing an old session. **Validation and hygiene.** `python3 -m py_compile tools/nsmb_join.py`, smoke **66 checks, 0 failures** (the C++ core is untouched), and the runs themselves - four measurements across four app sessions, three of them attributed and one deliberately refused. No emulator, link, transport, clock, cropping or touch-coordinate line was touched, and the app was not rebuilt: the only changed source is `tools/nsmb_join.py` (the sampler, the four phases, the frame-joined input attribution, the control gate, the roles fix), plus `BUILD.md`, `to-do.md` and this row. All five `Test Roms/*.sav` are byte-identical to their pinned md5s after every session (`bdbb2dfe` for NSMB included, and the launcher reported `sav ... unchanged` at each exit). Evidence kept: `logs/nsmb_join_fine_canon.log` (the canonical run's timeline), `logs/nsmb_join_fine.log` / `logs/nsmb_join_fine2.log` / `logs/nsmb_join_fine_repeat.log` / `logs/nsmb_join_fine_extra.log` (the other three measurements, including the refused one), `logs/nsmb_couple_keylines.log` (the app's own input trace, digests and radio events for all three runs, 390MB of raw logs reduced to 360K), and `game-captures/nsmb-app/run-fine-course.png` (the canonical run's own course capture: mean luma 155.0/155.6 top, 191.8 both bottoms, the two bottom screens 0 px apart and the two top screens 52% apart). Nothing committed. |
| 2026-09-29 | 0.1.9ah | The app's reply waits have one owner and it is the per-frame budget, not the 34ms per-window ceiling - measured by sweeping the ceiling on the app's own MKDS route - and the NSMB lobby "reply loss" the last pass flagged was a misread capture group: the app's own digest at those frames reads `collected 1021 empty 4`, and the same session on the rebuilt binary holds 14,503 commands answered 14,503 times with `deauth 0/0` | Asked to find the owner of the app's reply budget, decide with numbers whether the larger timeout is costing the peer's replies, fix it at its cause and re-prove the app's own NSMB session if it is, or say what the 34ms protects and what the lobby-phase `empty` counts really count if it is not - with the verdict in these docs and every `Test Roms/*.sav` byte-identical. **The premise was an instrument bug, and the app's own digest says so at its own frames.** v0.1.9ag read `collected 0 empty 1021` at frame 4081 and `0 / 2218` at 5100 out of the digest positionally, so the `collected` number it printed was the `empty` number it should have printed - the same off-by-one the sibling tool never had because it parses by name. The counter itself, on a fresh run on this build: `collected 1021(+298) empty 4(+4)` at frame ~4081 with `cmds tx 1025(+302, dup 0) acks 1025(+302, clientfail 4)`, and `collected 2217(+299) empty 4(+0)` at frame 5101 with `cmds tx 2221 acks 2221`. The 4 do not grow again: the session ends at `collected 14499 empty 4 reanchor-dropped 3` of 14,503 commands. So the app collected 99.6% of replies in the phase it was accused of collecting none in, the guest answered every command it was handed, and the difference from the harness is a **command rate**, not a loss - the app is asked for 2221 rounds by frame 5101 where the harness is asked for 2325 by frame 5200, which is the game's own asking, not the transport's answer. Re-measured on the harness side this pass, same save, same route, same window: `logs/nsmb_harness_frame5200.log` - host `cmds tx 2325 acks 2325 (clientfail 0)`, `collected 2325 empty 0 reanchor-drop 0`, guest `replies sent 2325`, 5200 frames x 2 consoles in 18.2s, 4.78x realtime. **Which knob ends a window, measured instead of argued.** `LocalMP::RecvReplies` waits `min(ReplyTimeout, budget left)`, so the ceiling can only matter when it is *smaller* than the slack the frame has left - and the trace counts say exactly that on the app's own MKDS route (`tools/mkds_join.py --restart --no-dialogs --cut-off --vs`, the same route both arms, one variable). At the default 34ms ceiling: **2678** windows logged `would have waited 34ms for a reply, but this frame only has 33xxxus of slack left` - the budget capping the wait - and **43** windows closed with nothing collected. At `MD_REPLY_TIMEOUT_MS=2`: **0** windows were budget-capped (the wait was always over before the budget could be consulted) and **394** closed empty. The counters move with them, which is what makes them the peer's replies rather than the host's arithmetic: at 2ms the host read `cmds tx 9815 acks 9815 (clientfail 394)`, `collected 9419 empty 394 reanchor-dropped 393` while the guest `answered 9812 (+ canned 1) = replies sent 9813` - the answers were sent, arrived late, and were thrown away by the next command; at 34ms the same counters read 43/43/42 over the part of the route both arms reached. Both arms also ended with `deauth out 1` on both consoles, so that is route timing rather than the ceiling (v0.1.9af's run of this route read `deauth 0/0`), and the arms are not matched in length - the 2ms arm ran ~3.7x more rounds in the same wall clock because it never blocks, and its loss rate climbs with the game's own retry storm (1/239 = 0.4% in the first window, 154/1800 = 8.6% in the last). **So: not costing anything, and here is what the 34ms protects.** It is the amount a *window* may spend before the budget is consulted, and it is deliberately above the budget (34ms against 33.4ms of two emulated frames) so that the budget - not the ceiling - is what decides a window; at that setting the ceiling never bound once in 2682 rounds. It exists because a client's answer goes out at the client's own frame boundary, up to one **emulated frame** of wall clock after the host asks (16.73ms at the DS's 59.826Hz), and the harness's 2ms is affordable only because the harness runs at 4.78x realtime where a frame is ~3.5ms; the app paces to real time. Set the ceiling below the frame's own slack, as `MD_REPLY_TIMEOUT_MS=2` does, and the host hangs up with 31ms of its own unused budget in hand - 394 times on one MKDS route. **Three defects on that path, all fixed at the cause.** (1) The budget knob was called `MD_REPLY_BUDGET_MS` and its value was read as a **frame count**, so a sweep at `17` bought 17 frames (284ms) and a sweep at `-1` bought the 2-frame default rather than the "no cap" the docs promised; it is now `MD_REPLY_BUDGET_FRAMES`, takes emulated frames, and a negative value means unbounded, which is what `MD_REPLY_BUDGET_NS=-1` already meant in the harness. (2) `tools/run_app.sh` forwarded `MD_REPLY_TIMEOUT_MS` but **not** the budget variable at all - the app reads its environment directly and an unforwarded variable never arrives - so the one knob the previous pass wanted to sweep could not be set through the app's own launcher; both are forwarded and documented in the script's header now. (3) The startup line printed `link reply timeout: {N}ms (2 emulated frames at 59.826fps)` for **every** value, so this pass's own first reading of its 2ms arm had 2ms described as two frames when it is an eighth of one; it now says `link reply timeout: 34ms = 2.03 emulated frames (default)` and names `MD_REPLY_TIMEOUT_MS` when that is where the value came from. **Boundaries, through the launcher, four ways:** default, `0`, `-1` and `abc` each started two consoles on the diag ROM and each ran ~600 frames, logging `budget: 2 emulated frame(s)` / `0` / `unbounded (MD_REPLY_BUDGET_FRAMES<0)` / `2` respectively - the junk value falling back to the default rather than to 0 or to unbounded (`logs/budget_*.log`). **The re-proof, on the rebuilt binary and the default settings.** Built `cargo build --release` (2m03s: the crate is the only thing that changed), then the app's own NSMB route via `tools/nsmb_join.py --restart --couple --shots-at 3780,4200,5000`. The coupled state survives: guest `cmds rx 14503 answered 14503 (blank 0, canned 0)` with `replies sent 14503 arms 14501`, host `cmds tx 14503 acks 14503` with `collected 14499 empty 4 stale 0 reanchor-dropped 3`, `deauth out 0 in 0` on **both** consoles and **0** `[link-event]` deauth lines in the run, **`link mask 0x3`**, both consoles still running at frame 17602 after 294s when it was quit, and one control-channel read showing both consoles coupling the guest's own input (`couple`: 2.5s of nothing gives 10 distinct pictures per console, then the guest alone holds RIGHT for 2.5s with **0** host input lines in the window and both consoles' screens ending on the same checksum, `p1:e7f2acc5 p2:e7f2acc5` - NSMB's shared-screen shape). The playable state is on screen and in this pass's own captures: `run9-f3780.png` / `run9-f5000.png` (canvas mean **164.6** and **164.5**) against `nsmb-f2012.png` / `nsmb-f2600.png` at **120.6** / **123.1** for the dialog screens, kept in `game-captures/nsmb-app/`. **Saves.** The supervisor's pin announced the session (`sav pinned before the session: ... md5 bdbb2dfe...`), reported `sav ... unchanged (md5 bdbb2dfe...)` and `app exited with status 0` at the end, and all five `Test Roms/*.sav` are byte-identical to the pinned originals (`identical` for MKDS `5e180fda`, LEGO `5af705e8`, MPH `ffa024d5`, NSMB `bdbb2dfe`, SM64DS `ad192e5d`). **What is still open, with the number that bounds it.** A four-round startup transient: at the same frames the app loses 4 of 2221 rounds and the harness 0 of 2325; it is 4 at the default ceiling and 1 at 2ms, so no reply knob causes it, it does not grow after frame ~4081 over 14,503 commands, and the guest answers every command it receives - so the answers exist and are dropped host-side, 3 of them arriving after the host had moved on (`reanchor-dropped 3`). The rounds in question are the first few after the guest's radio comes up (frame ~2735; the counts move between the 3580 and 4081 digests), which is the next measurement this file records. **Validation and hygiene.** Smoke **66 checks, 0 failures** (the C++ core is untouched; only the app crate and one shell launcher changed), `python3 -m py_compile` on the three drivers, `bash -n tools/run_app.sh`, `sh -n tools/scripts/nsmb-route.sh`, and the boundary launches above. Changed: `src-tauri/src/lib.rs` (the budget variable's name, unit and negative case; the timeout's log line), `tools/run_app.sh` (forward the budget variable; document both), `BUILD.md` (the two reply-tuning bullets, now saying which knob binds and what the ceiling protects), `to-do.md` (the sweep item closed with the verdict; the transient added as the next measurement), this row. No link, transport, clock, cropping or touch-coordinate line was touched, and no harness source changed. Evidence kept: `logs/mkds_timeout_arms_keylines.log` (both arms: every window closure, the budget-capped windows, both roles' digests), `logs/nsmb_app_run9_keylines.log` (the re-proof's digests, input trace and link events - the full 96M log was deleted), `logs/nsmb_harness_frame5200.log`, `logs/nsmb_run9.driver.txt`, `logs/mkds_app_join3.log` / `logs/mkds_app_join4.log` (the two arms' route timelines), `logs/budget_{DEFAULT,0,-1,abc}.log`, and the captures named above. Uncited bulk deleted: `route_persist0.log` (155M), `nsmb_verbose.log` (56M), `nsmb_bcast_audit.log` (42M), `nsmb_app_run7.log` (25M), the two full arm logs (61M) and the 96M re-proof log; `/` is at 4.2G free. Nothing committed. |
| 2026-09-29 | 0.1.9ag | The app's own two consoles hold a real New Super Mario Bros. session - one host, one guest, 7,916 commands answered 7,916 times, `collected 7,612 empty 4`, no deauth, both screens on the game's own course - after the reason every earlier route through the app parked 600 frames behind the harness's was found and fixed, and the app-versus-harness divergence the last pass left as a suspect now has numbers on both surfaces at the same frames | Asked to drive the NSMB route through the app's own control channel on this build, with `tools/scripts/nsmb-route.sh`'s coordinates, reading both consoles' screens between touches, fixing at its cause any app-path defect that blocks it, and finishing with a two-console session in a playable state whose evidence this pass captured (link counters balanced, the guest's input coupling into the host's picture, frames cited) or the exact blocker named and the pass closed - with the verdict in the docs, every `Test Roms/*.sav` byte-identical, and throwaway scratch deleted. **The defect was the driver's clock, and it was mine.** `tools/nsmb_join.py` waited a flat 18s for the app to boot - at 59.8fps that is frame ~1076, past the route's own 612 title tap - so the first one-shot tap went down at frame 1252 (`[input] frame 1252 player 1 touch -> 128,95`, the log's first input line; the driver's own record of the same run is `waiting 18.0s for boot` then `[boot] frames {1: 1221, 2: 1221}` - it began driving at frame 1221, 609 frames past the title tap) and the app's *game* ran ~600 frames behind the harness's for the rest of the session without a word from anything. Three symptoms, all one cause: the app's screens at route frame 2012 were **pixel-identical to the harness's at frame ~900** (0 differing pixels in the dialog's text band), its wireless came on at frame **2738** where the harness's came on at **2045** on the same build and the same save, and both consoles ended `[host]` with `rx bcast/cmd/reply/ack` climbing (`17/0/0/0`, `41/0/0/0`, `66/0/0/0`) while `tx cmd/reply/ack` stayed `+0/+0/+0` - the park (`logs/nsmb_app_park_keylines.log`). `wait_frame` could not say so either: a target frame the session had already passed was acted on quietly. Fixed at the cause and made loud. `wait_boot()` waits for the *frame counter* (not the wall clock: it starts driving as soon as a console is running, ~120 frames before the first tap), `wait_frame()` reports a missed frame as `frame N was MISSED (the session is at M, K frames on)` and the run ends with `route frames missed (delivered late): ...` or `none - every input landed inside its frame`. Before/after, same build, same ROM, same save: three runs parked before and three coupled after, and the app's own radio timeline now sits within 5 frames of the harness's - app `wireless ON (begin #2)` at frames **2050** (player1) and **2735/2745** (player2) against the harness's **2045** and **2733/2741**, and the command counts at the same frames match too (app 540 commands at frame 3411 against the harness's 526 at 3400). **The session, in the harness's units.** Guest `cmds rx 7916 answered 7916` (no blank, no canned), host `cmds tx 7916 acks 7916` with `collected 7612 empty 4 stale 0 reanchor-dropped 3`, guest `arms 7904 aids 1`, `window reads` 1,248,735 (guest) / 383,738 (host), `deauth out 0 in 0` on both, `slots 0x3` on both, both consoles `ACTIVE` with their pictures changing 0 frames ago. The playable state is not inferred from the counters: the app's own captures at route frames 3780/4200/5000 are bright course screens (canvas mean 168/168/168) whose **nearest harness frame is the same frame number** (`nsmb-f3780` against the harness's f3780 at 4,355/4,142 differing sampled pixels of 12,288, then f4060 for the later two - the race animates, so the same frame is the claim, not pixel identity), and the two consoles share one bottom screen (`821217F3668DC863 -> 5BA49A62CB12AB48` on both), which is NSMB's own shared-screen shape. **The coupling test, and what it does not prove.** With the race on both screens, the guest alone held RIGHT for 2.5s (`[input] frame 9244 player 2 buttons -> RIGHT (mask 00000010)`), the host's input trace in that window is **0 lines**, and both consoles' pictures advanced - but the baseline immediately before it (2.5s with no input at all) shows **10 distinct picture states per console**, because a race scrolls whether or not anyone presses anything. So this pass proves delivery and co-residency, not attribution: the guest's own pixels are not isolated in the host's picture, which is exactly what the last pass's `nsmb-play` captures did with a per-frame luma baseline (1,125 px of the host's top screen moved under the guest's input and never in three no-input baselines) and what this driver's coarse sampling cannot. **The divergence the last pass left as a suspect, now with numbers at the same frames.** The app's own route stops feeding input the moment the wire couples, the harness's keeps tapping A/CONFIRM to frame 11500 - and that difference is what takes the games out of the lobby and into the race, so it had to be made a flag rather than a default (`--stop-when-coupled`). With the harness's whole route driven, the app's *lobby* phase shows what the previous pass could only suspect: host `collected 0 empty 1021` at frame 4081 and `collected 0 empty 2218` at 5100 against the harness's **`collected 1325 empty 1` at frame 4200**, on the same route, the same save, the same frame - while the guest answered every command it was handed (`answered 1025`, `2222`). In the race phase that follows the game's third radio power-on the same app counter reads `collected 7612 empty 4`, so the collection is not broken, it is *phase*-dependent, and the phase it fails in is the one whose windows are wall-clock-bound (`ReplyTimeouts` are 34ms in the app against 2ms in the harness, and the app's own wall clock per frame is 16.7ms against the harness's ~6ms) - the one thing the tree has already named as the only wall-clock-dependent suspect, and the signature the `Connection interrupted` drop is known to leave. Not claimed as the drop's cause: measured, at the same frames, on both surfaces, and left as the next measurement. **Two instrument defects, both found by being misled.** `input_trace` and `link_lines` read the *tail* of the log (600 and 4,000 lines) while the app writes 40MB in a session and thousands of wifi-io lines a second, so the window was always saturated: `trace[600:]` is always empty and `session_data()` answered `False` on a session 2,909 commands deep, skipping the coupling test entirely. Both now read forward from a byte offset, and the driver's `press` no longer drops the app's own confirmation on the floor (`held buttons 0x0` used to read as success - the same silence the mask bug was hidden by). **Saves and hygiene.** All five `Test Roms/*.sav` byte-identical after a full coupled NSMB session (`bdbb2dfe…` included), checked against the pinned set in `/tmp/romtest-sav-before/`; the app was launched through `tools/run_app.sh` every time, so the pin announced each session's start and end. The 51MB app log was reduced to the lines this row cites (`logs/nsmb_app_run6_keylines.log`) and the giant uncited logs of this work (`nsmb_app.log`, `nsmb_app2.log`, `nsmb_harness_step.log`, ~125MB) plus the harness's raw `.bgra` shots were deleted; `/` is at 3.9G free. Changed: `tools/nsmb_join.py` (the boot clock, missed-frame reporting, `send_pair`, `--shots-at`/`--stop-at`/`--stop-when-coupled`, the two trace readers), `tools/mkds_join.py` (the same-frame tap pair, the press confirmation), `BUILD.md`, `to-do.md`, this row. No harness, link, transport, clock, cropping or touch-coordinate line was touched, and no emulator source changed: smoke **66 checks, 0 failures** on the unchanged build. Captures kept: `game-captures/nsmb-app/` (`nsmb-f3780.png`, `nsmb-f4200.png`, `nsmb-f5000.png`, `nsmb-couple-before.png`, `nsmb-couple-after.png`, `nsmb-route-2600.png`, `nsmb-route-3080.png`). Evidence: `logs/nsmb_join_run3.log` (the pre-fix driver's own record: the 18s wait, boot at frame 1221, the app's radio at 2738), `logs/nsmb_join_run5.log`, `logs/nsmb_join_run6.log`, `logs/nsmb_app_park_keylines.log` (the parked runs' app lines) and `logs/nsmb_app_run6_keylines.log` (the coupled run's). Nothing committed. |
| 2026-09-29 | 0.1.9af | The desktop app's own two consoles hold a real Mario Kart DS link - join, CUT OFF and VS all landed through the control channel, and the app's counters now stand in the same units as the harness's - and where it stops is the game's own park, re-measured from the app's own input path | Asked to drive one ROM end to end through the app, picking a scripted route already in the repo rather than Metroid Prime - Hunters' attract-gated title, reading each console's screen over the control channel between touches so the next input comes from what is drawn, fixing at its cause any app-path defect that blocks the link, and finishing with the app holding a real two-console link at the harness's evidence bar - both consoles `LINK ACTIVE` with slots on the bus, byte-complementary tx/rx, one answer per command, no deauth - with captured frames to cite, the result in these docs, the app launched through `tools/run_app.sh`, and every `Test Roms/*.sav` byte-identical, or a named and cited reason the app cannot get there. **The app's MKDS link, in the harness's units.** `tools/mkds_join.py --restart --no-dialogs --cut-off --vs` drives the app (not the harness): it starts it through `run_app.sh` with two autostarted consoles and the ROM, and every tap is preceded by a screen read - `wait_stable` on the touch screen, and CUT OFF and VS aimed at the *offer* (the patch's luma sampled through the channel, the tap fired when it changes: `CUT OFF on p1's screen after 1.1s (patch luma 103 -> 149)`). The route landed: title `(128,96)`, MULTIPLAYER `(128,68)`, CREATE GROUP `(128,157)`, NORMAL `(128,68)` - the host role appears at frame 2316 - then the second console's title and MULTIPLAYER, the cyan group bar at `(127,23)`, and **the guest role appears** with the host handing it `seq 5230 len 10 body BE 01 02 00 00 80 06 00 B8 B6` and the guest's own game answering `00 00 5A 5A B8 B6` at the same emulated offset (`+71.48s`). CUT OFF is taken, and VS after the game-select screen replaces the lobby: the host's command body walks to `BE 01 02 00 00 80 **FF** 00 B8 B6`, the guest's answer changes with it (`00 00 00 00 B8 B6`), and both consoles' screens become the same picture (`45d15630`, both framebuffers 100% black). The link's own numbers, from the app's digest and health lines: `cmds tx 2687 = cmds rx 2687`, `acks 2687(+299, clientfail 4)`, guest `answered 2686 + canned 1 = replies sent 2687 = collected 2683 + empty 4`, `arms 2687 aids 4425`, `window reads 158004(+17867)`, **`deauth out 0 in 0` on both**, **`slots 0x3` on both**, both `LINK ACTIVE`, and no `[link-event] deauth` anywhere in the run. A larger earlier run of the same route held the same state for 4,756 commands' worth of windows before it was quit. **The bytes, which is the axis the bar is written in.** The harness states a link in bytes (`sent=17219 (1,127,344 bytes)` downstream, `received=17219 (1,127,344 bytes)` back) and the app's health line printed only frame counts, so no run driven through the app could be compared with a harness run on that axis at all - the figures were already in `MDLinkStats` and simply were not shown. It now prints both: `tx 5670(+621, 281938B(+29670)) · rx 2685(+299, 128B(+0))` on the host and `tx 2689(+299, 114406B(+12558)) · rx 5541(+621, 266458B(+29670))` on the guest. Window for window the two directions are **byte-identical** - `+29,760` against `+29,760`, then `+29,670` against `+29,670` - and the cumulative difference is a constant 15,480 bytes over 129 frames accumulated *before* the guest's radio came on (`[link-event] +69.034s wall frame 4012 p1 (player2): wireless ON (begin #2, slots now 0x3)`, while the host had been up since frame 2172): traffic that was never receivable, not loss. The guest's receive side is where the exact figure lives; the **host's `bytes_received` is a lower bound by construction** (128 bytes against 2,685 frames) because its clients' replies arrive through `LocalMP::RecvReplies`, which counts the packet and cannot size the payload - the caveat `MDLinkStats` already documented, now visible on the line a reader actually sees. **Two defects, both on the app's own path, both fixed at the cause.** (1) The app's driver had been silently losing a field for a pass: `tools/mkds_join.py`'s picture regex knew only `changing|FROZEN`, and v0.1.9ae renamed the app's wording to `still` - so every report the app's own driver printed had *no* picture state in it, which is the silent-miss shape v0.1.9ab and v0.1.9x exist to remove. Fixed, and confirmed live rather than by reading: the run now prints `ACTIVE still (picture last changed 399 frames ago)`. (2) The health line's missing bytes, above. **Where it stops, and why it is not ours.** Both games halt on the same black screen with the link still perfect - the park this project has already pinned to the games (v0.1.9t: the full button set and six touches on each console's own bottom screen move neither the screen nor the exchanged state; v0.1.9v and v0.1.9j: the two state machines have each read the other's whole state and neither changes it). Re-measured **through the app's own input path** rather than assumed: a battery of A on both consoles, START on both, and a touch at `(128,96)` on each left both framebuffers byte-identical (`358609219F480325`) with the `copy` beats unchanged, while the log shows the input arriving - `[input] frame 12524 player 2 touch -> 128,96` through `frame 12534 player 2 touch released`, and the same for the pad - so the app delivers and the game ignores, on both consoles, exactly as the harness found. So the ROM's own flow, driven end to end through the app, ends here; a race needs a title whose two games open the session-data phase after the shared screen, which is NSMB (`tools/scripts/nsmb-route.sh`, already reached through the app in v0.1.9x). **What the run also proved, without meaning to: the save pin.** The session wrote Mario Kart DS's card at exit - `5e180fda…` to `95ae60d7…`, the game's own save - and `run_app.sh`'s pin announced it in the log's last line with the one command to restore it; the file was restored from the pinned copy and all five `Test Roms/*.sav` are byte-identical to their recorded md5s (`5e180fda…`, `5af705e8…`, `ffa024d5…`, `bdbb2dfe…`, `ad192e5d…`). Health of the run: **0** `looks wedged`, **0** watchdog kicks, and the 13 `WARN` lines are all `[mp-digest] state changed` role/radio transitions. Validation: smoke **66/0**, `python3 -m py_compile` on the drivers, `bash -n`/`sh -n` on the launchers and routes, the app crate rebuilt (`cargo build --release`, 1m23s) and the route re-run after the health-line change. Changed: `tools/mkds_join.py` (the picture regex), `src-tauri/src/lib.rs` (the health line's two byte counters), this row, `BUILD.md`, `to-do.md`, and the new `tools/scripts/mkds-park-probe.py` (the park battery). Captures kept: `game-captures/mkds-app/` (`mkds-lobby.png`, `mkds-after-cut-off.png`, `mkds-after-vs.png`; md5s `221fed17…`, `b81b1771…`, `02675bad…`). Evidence logs: `logs/md_app.log` + `logs/mkds_app_join.log` (the 4,756-command run, and the park battery against it), `logs/md_app_mkds2.log` + `logs/mkds_app_join2.log` (the run with the byte counters). No link, transport, clock, cropping or touch-coordinate line was touched. Nothing committed. |
| 2026-09-29 | 0.1.9ae | The app's touch path had two owners - `sendButtons()` sent `touch: null` on every keypress, blur and console switch while the control channel wrote `set_input` straight from Rust, so a keypress lifted a pen the channel had just put down - and the pen can no longer be held with a button. Fixed at the cause: the page owns a console's whole input state and every writer goes through it | Asked to drive the app's two consoles through v0.1.9ad's Metroid Prime - Hunters route, reading each console's screen between taps so the hunter select and the match start land where the game wants them, and to finish with either the match running (commands/answers, `slots 0x003`, no deauth) or **a named defect on the app's touch path fixed at its cause**, the verdict in the repo's docs, and every `Test Roms/*.sav` byte-identical. **The defect, and what it did.** The pen had two owners. `sendButtons()` in `frontend/index.html` - the page's keyboard path, called on every keydown, keyup, window blur and console switch - sent `{buttons, touch: null}` to the console, while the control channel's `touch` wrote the Rust side's copy directly through `set_input`. So the two could not both be right: a keypress lifted a pen the channel had just put down, and a pen push carried whatever buttons happened to be stale. Reproduced on the app before it was understood, with the log as the only instrument: pen held on console 1 for 2,500ms (`touch 128 96 --player 1 --ms 2500`), `key KeyJ` one second in, and the guest's pen went **up on the key's own frame** - `[control] key: keydown KeyJ` immediately followed by `[input] frame 1284 player 1 touch released` (`logs/rom_mph_app3.log`), 1.4 seconds before the hold was over, with no `touch ->` line after it. That is the DS's normal grip (a stylus and a d-pad at once) silently losing the stylus, and it is also why any driver route that interleaves `key`/`press` with `touch` cannot be trusted. **The fix, and how it was proved.** One owner, in the page, beside the hooks v0.1.9ab put there: a console's whole input state lives in `canvases[p].buttons` + `pen`, one function (`pushInput`) sends both halves together, and every writer goes through it - the keyboard, the pointer, and `window.__mdinput`, which the prelude's `D.input` now calls so the control channel lands in the same state the player's own input does. Console switching lifts the old console's pen deliberately, because the pointer handlers ignore every console but the selected one and a switch that only cleared the page's copy would leave the guest's pen down for good. The same test, after the rebuild: pen down frames **1246-1391** (the full 2.5s), `key KeyJ` at 1300 giving `buttons -> A (mask 00000001)` with **no release**, and exactly one release at 1392 when the hold ended (`logs/rom_mph_app4.log`). Boundaries, all through the app's real entry points: switching consoles mid-hold lifts console 1's pen at the switch (frame 2097) instead of sticking it; `touch --player 3` on a two-console session exits 1 with `touch: player 3 has no console (running: [1, 2])`; and per-console buttons reproduce v0.1.9ab's diag-ROM checksums exactly - idle `FB1BAEEE…` (console 1) / `A195CA30…` (console 2), `556E89D8…` while A is held on console 1 with the peer unchanged, `D57D029E…` while A is held on console 2 - so the new route is the same route, not a new one (`logs/diag_app_afterfix.log`). `md_smoke_test`: **66 checks, 0 failures**. **The app's game is reachable; its phase is not the harness's.** Console 1's top screen on the app is ~95% block-identical to the harness's own multiplayer-menu frame (`game-captures/mph-mp1`, frame 20800) with only the list area differing, so the app's game sits on the screen v0.1.9ad's route navigates with taps - but the frame-timed taps miss it, which is a *reading* problem (a screen must be identified before it can be tapped), not a broken input path. Two instrument facts came out of the same work and are recorded as such: the app binary was built at **00:45** while the harness binary was **08:51**, so every app-versus-harness comparison before this pass compared builds nine hours apart (and the app's logs did not carry the `frame N` trace v0.1.9ad added) - both rebuilt here; and the page's rebuilt asset can be checked by asking it (`eval "typeof window.__mdinput"`), which is the one cheap way to know a rebuild actually reached the webview. **A save file changed and nothing said so.** The app leaves the game in charge of its own card, and the card is written when the session ends: a 40-minute Metroid Prime - Hunters session took `Test Roms/Metroid Prime - Hunters (USA) (Rev 1).sav` from `ffa024d5…` to `96720058…` silently - the file had been identical at every earlier check, because it only hits disk at the end. Restored from the pinned set an earlier pass left in `/tmp/romtest-sav-before/` (md5 `ffa024d5…`), so all five `Test Roms/*.sav` are byte-identical to their recorded md5s again - and `tools/run_app.sh` now pins the ROM's `.sav` to `game-captures/sav-backups/<name>.sav.before-app-<stamp>` before it launches and prints at exit either `sav … unchanged (md5 …)` or `sav … CHANGED (md5 A -> B)` plus the `cp` that restores it. It does not restore by itself (the same launcher is used to play); the point is that the change can no longer be invisible. **A misleading warning, made honest.** The health thread's `p{} looks FROZEN` fired on any picture that had stopped for 300 frames, including a game waiting on a menu with its CPUs running (`pc 39-54/100` while player 2's picture held still for 6,856 frames) - and reading that as a hang sent this pass down the same blind alley twice. A still picture is now `picture still (…)` and the note is `info`, naming the program-counter movement beside it; `looks wedged` (a console whose PC really stopped) is unchanged and is the only one that is a warning. **What is still open, stated plainly.** Not a match through the app: the app's consoles have not been driven past the screen they sit on, because that needs the screen read before each tap and the identification the previous pass also could not do without eyes on the pixels. Whether the *guest* sees a pen the app put down is unmeasured too - `md_touch_probe` in the bridge reads exactly that back (the smoke test asserts `ADC = pixel x 16`) but it is not on the control channel, which is the instrument the next pass needs. Changed: `frontend/index.html` (the one input owner), `src-tauri/src/lib.rs` (`D.input` through the page; the health wording), `tools/run_app.sh` (the save pin and report), `BUILD.md`, `to-do.md`, this row. No link, transport, clock, cropping or touch-coordinate line was touched, and every harness result - the MPH join, NSMB, MKDS, Star Fox, SM64DS, LEGO - is unaffected: the harness binary was not rebuilt. Evidence: `logs/rom_mph_app3.log` (the pre-fix reproduction), `logs/rom_mph_app4.log` (the same test after it), `logs/diag_app_afterfix.log` (the per-console checksums). The screenshots taken while reading the app's screens were deleted; the http server started to view them was stopped. Nothing committed. |
| 2026-09-29 | 0.1.9ad | The Metroid Prime - Hunters guest join is done and now reproducible: 7,986 commands answered 7,986 times, byte-complementary both ways, `slots 0x003`, no deauth - and the two consoles sit in the same two-player lobby | Asked to drive the one half-working multiplayer in the folder's ROMs to completion on the real surface, reading the guest's screen between inputs instead of guessing the confirm, and to finish with the guest's counters off zero, host and guest byte-complementary, one answer per command, `slots on the bus 0x003` with no deauth, the two consoles on the same screen on the frame sheets, the result in the repo's own docs, any driver defect fixed rather than worked around, and every `.sav` in `Test Roms/` byte-identical. **The join, on a route anyone can re-run.** `logs/rom_mph_join.log` (35,000 frames x 2 consoles in 228.1s = 585.0 emulated seconds, 2.56x realtime): console 1 `sent=17219 (1,127,344 bytes)` / `received=7988`, console 2 `sent=7988 (399,388 bytes)` / `received=17219 (1,127,344 bytes)` - each console received exactly what the other sent, byte for byte in both directions - `slots on the bus: 0x003`, `deauth out/in=0/0` on both consoles, host `cmds tx=7986 acks=7986 (clientfail 0)` with `collected=7986 empty=0`, guest `cmds rx=7986 answers=7986 (blank 0, canned 0)` with `replies sent=7986`, `arms=7987`, `aids=15973`, and `window reads` 239,634 (host) / 621,301 (guest) - where v0.1.9ac's run had `sent=0`, `window reads=0` and `[not in MP]` on that console. The last command is `seq=3410` at `+585.01s` with the guest's answer (`len=14 body[03 8C 00 00 C0 00 00 00 1F 17 5A 5A B8 B6]`) at the same offset, i.e. the exchange was still live at the end of the run rather than having stopped early; both consoles read `running` with the picture changing 0 frames ago, `all consoles kept running`, and both end on arm9 `pc=0x02088ba4` - the same code path on both. **The route, and where v0.1.9ac's was wrong.** `--all-players --touchtap 128:96:9000:400 --stop-input 11500 --touch 13000:184:90 --touch 15000:134:122 --touch 17000:136:68 --touch 19000:45:71@1 --touch 24000:184:58@2 --touch 27000:123:72@2 --touch 30000:123:72@2 --touch 33000:123:72@2` - frames, not a claim: every one of those taps is in the log beside its coordinate and its frame. Two corrections came out of it. (1) **The title is behind a cycling attract loop, so one tap at a fixed frame is a coin toss.** The guest's own frames (`game-captures/mph-lobby/strip.html`) show cinematics alternating with the `TOUCH TO START` card every ~1,000 frames, and the loop's phase depends on the save on the card: on the save v0.1.9ac left behind, a *fresh* run put a cinematic where the earlier run had the title, and the first attempt at this pass put all six taps into cinematics the game ignores - `slots 00000`, `sent=0` on both, not even a second radio power-on. A bounded repeating tap (`--touchtap ...:9000:400`, stopped at 11,500 by `--stop-input`) lands inside a few cycles and cannot run on into the next screen. (2) **The join is the `JOIN GAME` tap, not the list row.** The row at `(184,58)` was v0.1.9ac's `(198,46)` guessed too far right - the measured row is 58 down, not 46 - but the row only *selects*: the peer difference steps at frame 27000->27250 (`top mean|p1-p2|` 4.34 -> 1.14, `bottom` 25.86 -> 32.48 over the two consoles' stored frames), which is the `(123,72)` tap at 27,000, and the run that omitted that tap got the host as far as CREATE GAME and beaconing (`slots 0x003`, `sent=1,245 (219,036 bytes)`) with the guest never sending. **Where it ends, stated plainly.** Not a match: both consoles list two players (`PLAYERS melonds melonds` on both top screens) and the guest sits on `SELECT HUNTER` from the join to the end of the run - 117 emulated seconds - because no hunter is chosen and no game is started (`game-captures/mph-lobby/sheet.html`, frames 30,000-34,750 of both consoles). The joined state is the same one the console reached by hand: the same lobby, on both screens, with the exchange live underneath it. **Two instrument defects, both found by being misled rather than by inspection, both fixed here.** (1) The harness's input trace named the coordinate and not the emulated frame, so a route that landed could not be read back off its own log - which is why v0.1.9ac's working taps could not be recovered and this pass had to re-derive them; `md_touch`, `md_release_touch` and `push_buttons` in `src/frontend/tauri/Bridge.cpp` (the app's and the harness's shared input path) now print `frame N` on every line, and that is what makes the route above citable. (2) `[link-event] +NNN.NNs` is **wall** clock since the bus was initialized, not the emulated offset the digest prints as `at +NNN.NNs`, and nothing said so - this pass read them as one clock and put the guest's join 130 emulated seconds away from the frame it is visible on in the captures. Every `link-event` line now says `wall`, and the three that involve a console print its own frame beside it. **The app surface, driven and left open.** The same route through the desktop app's own channel (`MD_AUTOSTART_PLAYERS=2` + the ROM, then `tools/appctl.py`, `logs/rom_mph_app.log`) loaded the same 262,144-byte save on both consoles and reached the title, but sixteen `touch 128 96` taps spread over 2,600 frames (6 frames each, both consoles, every one printed in that log) produced no screen change and no second radio power-on, while the harness's identical coordinate at a comparable frame does advance it. That is recorded as an open dead end, not a diagnosis: the screen could not be identified past "has text bands at y 148..172" without eyes on the pixels, and the app's turn was cut off there deliberately rather than guessed at. Next pass has the instrument to settle it (the diag ROM answers a touch with a changed bottom-screen checksum, the same way v0.1.9ab proved `press a` and `key KeyJ` per console). **Saves and hygiene.** All five `Test Roms/*.sav` are byte-identical to their pre-pass md5s - Mario Kart `5e180fda…`, LEGO `5af705e8…`, Metroid Prime - Hunters `ffa024d5…`, New Super Mario Bros. `bdbb2dfe…`, Super Mario 64 DS `ad192e5d…` - so neither the joined session nor the app run wrote the card, and the `SAVING TO THE GAME CARD` dialog v0.1.9ac saw is not on this path. Changed: `src/frontend/tauri/Bridge.cpp` (the two log shapes only), `BUILD.md` (the MPH route, now with the confirm tap and the attract note), `to-do.md`, this row. No link, transport, clock or cropping line was touched, and the NSMB, MKDS, Star Fox, SM64DS and LEGO results are unchanged. Verified: smoke **66 checks, 0 failures**, both binaries rebuilt, the route re-run end to end. Kept as evidence: `logs/rom_mph_join.log`, `logs/rom_mph_first_join.log` (the first observation, a separate 35,000-frame run in which the guest answered 8,773 times and the host collected 8,773), `logs/rom_mph_app.log`, `game-captures/mph-lobby/sheet.html`, `game-captures/mph-lobby/strip.html`. The raw captures from the three reproduction attempts (~300 MB) were deleted, which is what took `/` from 4.1 G to 4.4 G free. Nothing committed. |
| 2026-09-29 | 0.1.9ac | The folder's own coverage, ROM by ROM: the three titles that had never been launched were run on two consoles, and each now has a verdict rather than an assumption. **LEGO Star Wars II - The Original Trilogy has no multiplayer mode at all** - 18,000 frames x 2 consoles in 46.9s = 300.9 emulated seconds at 6.41x, `slots on the bus: 00000`, radios `on/off=1/1`, `sent=0 (0 bytes) received=0 (0 bytes)` on both, both `[not in MP]`, and its own screens show exactly three menu entries (New Game, Load Game, Settings) and a Settings pane of Music Volume / SFX Volume / Credits - no link, wireless or multiplayer entry anywhere (`logs/rom_lego_harness.log`, `game-captures/lego-set2/sheet.html`; the title sits on `TOUCH TO CONTINUE` for the whole 300 emulated seconds, `game-captures/lego-idle/sheet.html`). **Metroid Prime - Hunters reaches its own lobby and discovers the other console.** The route is title (`TOUCH TO START`, a touch - buttons do nothing) -> SELECT A GAME MODE -> MULTIPLAYER MODE -> the `YOUR NICKNAME IS melonds` notice -> CHOOSE MULTIPLAYER TYPE -> MULTI-CARD PLAY -> CREATE OR JOIN A GAME, and the game powers its radio for it (`link-event wireless ON begin #2` at +112.6 s, `WIFI: ON` at boot then off again 6 ms later). Console 1 tapped CREATE GAME and became `[host]`, transmitting 1,053 frames / 185,244 bytes; console 2 received exactly 1,053 frames / 185,244 bytes, and console 1's game appears in console 2's OWN `AVAILABLE GAMES` panel as `MELONDS`. `slots on the bus: 0x003`, 34,000 frames -> 568.3 emulated seconds, 2.44x realtime, `all consoles kept running` (`logs/rom_mph_harness.log`, `game-captures/mph-mc2/sheet.html`, `game-captures/mph-mp1/sheet.html`). What did *not* happen, stated plainly: the guest never transmitted (`player 2 sent=0`, `[not in MP]`), so no command/answer exchange exists for this title - two blind-tap attempts at the guest's own confirm (select the `MELONDS` row, then JOIN GAME) did not complete the join, and the run's own counters say the guest's radio never even tried, so this is a UI-driving gap and not a transport one (0 `bad channel` drops, 15,242 `switching to channel` lines, both channel 1 and channel 7 used). **Star Fox Command went furthest of the three.** Its local surface is Single Player / Download Play / Wi-Fi Battle / Options, and Download Play offers one option, `Recruitment` (host); driven on both consoles it produced a **complete bidirectional exchange**: console 1 `sent=489 (99,532 bytes) received=501 (101,980 bytes)`, console 2 `sent=501 (101,980 bytes) received=489 (99,532 bytes)` - byte-complementary in both directions, i.e. each console received exactly what the other sent and nothing else - with `slots on the bus: 0x003`, both consoles running the MP host state machine (`[host]`, `window reads` 53,495 and 52,211), the radio power-cycled three times (`wireless ON begin #3` at +79.715 s when Recruitment opened), 26,000 frames -> 434.6 emulated seconds, 3.56x realtime, 0 `bad channel` drops (`logs/rom_sfc_harness.log`, `game-captures/sfc-dp/sheet.html`). Its recruitment lobby lists `melonDS` with the same `Cut Off` / `Back` shape MKDS's does, and the game offers no in-cartridge join path: the joining console is the *firmware menu's* Download Play client, which FreeBIOS cannot boot - the same product decision already recorded for Super Mario 64 DS, now with wire evidence behind it. Saves: the two titles that initialise saves wrote new files (LEGO 512 B, Metroid Prime - Hunters 262,144 B, its own `SAVING TO THE GAME CARD` dialog visible at frame 16,400); New Super Mario Bros.'s `.sav` is byte-identical to the pinned backup (md5 `bdbb2dfe557b30b4fda1a7dc7d28dce0`) and Mario Kart DS's and Super Mario 64 DS's are unchanged. No source line was touched. The throwaway captures (about 400 MB across eleven directories) were deleted; what was kept is the four sheets cited above and the harness logs beside them. |
| 2026-09-29 | 0.1.9ab | Three facts this thread had grown twice each now have one owner - the key-name vocabulary, the screen-reader guards and the two documented test routes - and `key Bogus` is refused with a sentence instead of succeeding in silence | Asked to give the keypad name list, the empty/session guards and the documented routes exactly one owner each, delete the copies, change no behaviour on the link, and re-verify. **The key vocabulary.** `key` accepted any string, so a name nothing listens for dispatched an event and reported success. `key` and `index` now ask the page's own owners, `window.__mdkey`/`window.__mdselect`, which sit next to the handlers in `frontend/index.html` that dispatch on that vocabulary (the keymap the backend sent, the console digits, Tab, the peek key); a name the page cannot act on throws, and `ctrl_eval` now unwraps the `__ERR__` marker its wrapper has always produced - so a throw inside the page is a command failure instead of data whose first word was the marker. **Verified live, and the point is that the refusal did not cost the working path:** `key Bogus` exits 1 with the sentence; on the diagnostic ROM's button blocks, `key KeyJ` produces the *byte-identical* bottom-screen checksum as `press a --player 1` (crc `556E89D8…` for both, `key KeyA` a third, the peer unchanged), and `index 2` then `key KeyJ` moves console 2's blocks while console 1 keeps its idle checksum - so a key delivered through the page's own vocabulary drives exactly the emulator state the channel's own input command drives, per console. **The guards.** "Is there anything to read" and "is this console index in range" are now `D.frameSet()`/`D.console(player)` in the channel's page-side prelude, one owner each, with `D.shot`, `D.ascii` and the new `D.bands` all answering from them; `tools/appctl.py`'s `BANDS_JS` (a second copy of the row reader *and* of both guards, whose wording had already drifted) is deleted and `cmd_bands` dispatches to `__mdctl.bands`. An idle app answers `(no session: no console is running)` from both `see` and `bands`; `see --player 3` and `bands --player 3` both answer `(player 3 out of range: 1..2)` on a live two-console session. **The routes.** The NSMB harness route lives once, in `tools/scripts/nsmb-route.sh` - coordinates, repeating tap, `--stop-input`, save restore, and every other harness option passed through - which BUILD.md's "Real game session" section now runs instead of restating, and which `nsmb-drop-rate.sh` calls per run instead of keeping its own copy; the stale route's numbers are gone from the prose, which keeps only the lesson. MKDS's coordinates live once, in `tools/mkds_join.py`'s constants, now that its docstring, BUILD.md's MKDS bullet and `example.txt` name the constants instead of repeating the numbers. BUILD.md's claim that `key Bogus` is accepted in silence is corrected. **Re-verified in order:** `cargo build --release` clean (1m18s, and 1m20s after the sweep below); smoke **66/0**; the documented route through its new owner (`tools/scripts/nsmb-route.sh`): `slots on the bus: 0x003`, one command per frame, guest `cmds rx 11129` answered 11129, last cmd `at +234.00s`, `all consoles kept running`, 3.5-4.1x realtime; `nsmb-drop-rate.sh` through the owner (`0 of 1`, no deauth); the app driven for real through `tools/appctl.py` (`see` `bands` `text` `shot` `key` `index` `probe`, `key Tab` moving the layout mode, `index` moving the active console); the Test Roms `.sav` byte-identical (`bdbb2dfe…`) before and after. **The maintainer sweep over this pass's own diff:** `D.select`, a one-use pass-through whose missing-function branch returned a *success* string - the silent-success shape this pass exists to remove - is gone and `index` calls `window.__mdselect` directly; `D.key`'s `typeof` guard against a page without `__mdkey` is gone too, since a built artifact cannot be in that state and the natural throw already reports; and the refusal sentence no longer spells out `ShiftRight` and six keymap names, because a message that lists the vocabulary is a second copy of it. `cargo fmt --check` is not a gate this tree passes - 24 pre-existing hunks in `lib.rs`/`build.rs`, none in the regions this pass touched - while `cargo build`, smoke, `python3 -m py_compile` and `sh -n` are. Nothing committed. |
| 2026-09-29 | 0.1.9aa | The +1830 s NSMB drop is not a fixed offset and not input: the harness route links again, a no-input run crosses the whole window clean (2,089.4 emulated seconds, `deauth 0/0`, zero loss), and the identical drop signature turns up at +67.9 s in another run - so what is left is the nondeterminism, not the transport | Asked to separate our delivery from the title at the app's +1829.95 s drop (a counter, sequence space, reply window, stale-reply rule or watchdog, against the game's own behaviour) and to note that the 120,000-frame harness run built for it never linked at all (`slots on the bus: 00000`, every MP counter zero) - so either make that route link again or get the crossing some other way; done meant a run that links and crosses that offset with a verdict on whether the drop lands at the same emulated offset with input and without it, plus a fix in the transport if delivery is implicated. **The blocker, fixed: the documented route was timed for a save state that no longer exists.** `md_game_session`'s NSMB route (`--touch 600:128:68 --touch 1000:128:157@1 --touch 1400:128:68@1 --touch 1700:127:23@2 --autotap A:6500:90`) reproduces exactly as v0.1.9z left it: 14,000 frames, `slots on the bus: 00000`, `sent=0 received=0`, both `[not in MP]`, one radio power-on at +0.26 s and never a second - none of its taps reaches the game's local-wireless entry. Running it *as an argument list* rather than as a claim showed why: `--touchtap 128:71` and `--touchtap 128:171` in the v0.1.9z-era command list looked like two alternating taps and were one, because `apply_input` keeps only the last asserting touch per frame and both had the same start and period. The route that *does* reach the game is the one the live app session walked, recovered from `logs/route_fix.log`'s `[control] touch` lines (title menu `(128,95)` at ~frame 630, multi-card `(128,72)` at ~1310, character select `(127,119)`/`(127,160)` at ~2030, then a repeating `(128,171)` and repeating `A`), and it links on the current tree first try: both radios on, `[CLIENT 1] host sync=0000000002DD09AC`, `slots 0x003`, 1 command per emulated frame, `stale 0`, `clientfail 0-1`, 3.5-3.7x realtime. **Two instruments, both required by the A/B and both defects before they were added.** (1) `asserted()` checked an input's last frame only on the non-repeating branch, so a periodic input could not be stopped at all - a repeating menu tap runs to the end of the session by design, which made "the route, and then nothing" inexpressible; it now honours `last_frame` for both kinds, which changes nothing for the documented behaviour (`last_frame` is `frames+1` for a repeating input) and is what makes `--stop-input FRAME` work. (2) `--stop-input FRAME` ends every input that *begins* at or before FRAME at FRAME and leaves later ones alone, so a run can finish its route and then inject the input it is actually measuring. (3) The harness now prints the emulated offset of the session's last command and answer (`at +NNN.NNs`, from `MDMPDigest::last_cmd_us`, the same field the app's `mp pN last:` line prints) - without it two arms cannot be compared at all, since the harness prints its digest only once, at the end. **The no-input arm: it does not drop, and nothing was ever lost.** 125,000 frames = 2,089.4 emulated seconds: 122,126 commands, 122,126 answers, 122,126 acks, one answer per command, `deauth out/in=0/0` on both consoles, `stale 0`, `reanchor-dropped 0`, `empty 1`/`clientfail 1` (the first round, before the guest has armed), both slots on the bus and **the last command at +2089.37 s** - 259 s past the app's drop, with the link still live at the end of the run. So a linked NSMB session does not end at +1830 s, and the `+1830 s` offset is not a property of the session. **The reproduced drop, in a run whose extra input had not started yet.** The second arm was byte-identical in input through the drop window - the only difference was the guest tapping RIGHT every two seconds **from frame 13000**, and the drop was at frame ~4,045. Its digest is the app's drop read off the harness: `deauth out/in=1/0` on *both* consoles, `last deauth reason=0003` on both, `last cmd seq 9AC0 len 262/266 body[DE 01 02 00 7E 9D 0F 00 03 00 EF 04 00 00 03 00 ..]` at **+67.87 s** (guest) / +67.86 s (host), the guest's last answer `len 6 body[00 80 F0 04 B8 B6]` where its normal answer is 70 bytes, and the link-event log `[CLIENT] deauth` (the guest's own TX path clearing `IsMPClient`) then `wifi off (end #3)` on p2 and `(end #2)` on p1 within 1 ms. The app's last answer at its own drop is the same object: `len 6 body 00 00 FC 19 B8 B6` against normal 70-byte answers, both radios off in the same digest tick. So the sequence is "the guest's game leaves, the host's game follows", and it is *not* a function of elapsed time. **How rare, measured.** Sixteen further runs of the same route - eight sequential, eight as two concurrent pairs, 8,000 frames each - dropped **zero** times (the script is `tools/scripts/nsmb-drop-rate.sh`, which derives the emulated offset from the route's own repeating tap). Two drops in roughly 3.5 ks of linked session, one of them at 20 s and one at 1,785 s: the app's single 30-minute run is the same order. **Delivery, on the numbers, was never the problem - and one premise in the request was wrong.** At the app's drop the transport had delivered 106,663 commands, 106,663 acks and 106,663 answers with `stale 0`, `reanchor-dropped 3` (startup), `empty 4` and `clientfail 4` unchanged since the first seconds, `last ack clientfail 0000`; the harness's drop the same. And the app's own digest at the drop reads `wifi on/off 2/2` (p1) and `3/3` (p2), with `[link-event] +1831.774s p1 (player2): wireless off (end #3)` and `+1831.782s p0 (player1): wireless off (end #2)`: the wireless was **off** on both consoles, not still powered - both games turned their own radio off. **What is left, named rather than guessed.** The one wall-clock-dependent thing in the path is the reply window's per-frame budget (`MPInterface::SetReplyBudget`, 33 ms in the app, published per frame), and it does not bind in the harness at all, whose frames are ~5 ms - so the two arms and the app do not agree on the one timing rule that could produce a rare, environment-dependent drop. Catching a drop under the app's pacing with `MD_LINK_VERBOSE=1` is the next measurement. **Hygiene.** Changes this pass: `tools/coretest/game_session.cpp` (`asserted()`'s last-frame rule, `--stop-input`, the last-command/answer offset in the digest dump), BUILD.md (the working route plus the `--stop-input` note, and the old route marked as stale), `to-do.md`, this row, and the new `tools/scripts/nsmb-drop-rate.sh`. No `src/` line was touched, so the load-bearing fixes (FCS placement, receive crop, reply identity, shared clock, reply-slot arm handling) are untouched as instructed; the MKDS latch was not reopened. `md_smoke_test` **66 checks, 0 failures** on the tree; the NSMB route re-run after every harness change; the Test Roms `.sav` restored from a pinned copy before every run and byte-identical afterwards. Nothing committed. |
| 2026-09-29 | 0.1.9z | The app route's end screen is the game's own "Connection interrupted" dialog and the route did reach the harness's course before it; one console's input at the peer is now measured, not inferred | Asked to resolve the two routes' disagreement on one build and to settle the peer effect with the instrument the last pass named as sufficient (a full dump of the session frame the host's game is handed, or a character/state value out of the game's RAM), against a no-input baseline so heartbeat traffic cannot be mistaken for coupling; to read what the app route's end screen actually is and say whether it is a race both consoles are in; to treat a divergence from the harness's state as *the* defect to fix; and not to reopen the MKDS latch, reply identity, the receive crop or the FCS slot. **What was run.** The two-console NSMB session started through `tools/run_app.sh` (autostart 2 players + ROM) was still live and was driven only through `tools/appctl.py`: a time-resolved coupling run of five phases - no input / console 1 holds RIGHT for 8 s starting 2 s in / no input / console 2 holds LEFT the same way / no input - ~4 samples/s, each sample the console's own 1:1 canvas through the control channel (`__mdctl.shot()`, 512x384 for two consoles, cropped per console) plus `wifiio` for both consoles, with the verbose session log (`logs/route_fix.log`) kept for the event narrative. Offline: the end screen was read with `tools/glyphs.py`'s method plus a per-glyph splitter over the captures, matched over the whole `nsmb-*` corpus by downsampled signature, and the app's own digest/health lines were read as the event log. The settling test for the drop was a 120,000-frame `md_game_session` run of the documented route with `--hold 9000:RIGHT@1`. **What the app route's end screen is, read off it.** Three centered lines of the game's own font, bright on black, on *both* consoles: line 1 "Connection interrupted", line 2 "Press the . Button to", line 3 "return to the title ..." - read glyph by glyph (C-o-n-n-e-c-t-i-o-n, i-n-t-e-r-r-u-p-t...; P-r-e-s-s / t-h-e / B-u-t-t-o-n / t-o; r-e-t-u-r-n / t-o / t-h-e / t-i-t-l-e) with the font *calibrated* on the documented `nsmb-park-guest-pleasewait` capture, whose text its own filename gives and whose P, l, e, a, s, w, i, t templates then identify the same shapes in the dialog. The screen is the capture `nsmb-play2` f10800 pixel for pixel (mean abs 0.0 over the full 256x384, top screen included) and `nsmb-play4` f24500 at 4.3 - the capture the previous pass matched the app's "end state" to, which is why that capture's frames are pixel-identical to one another: it is a dialog waiting for a button, not a frozen emulator. **It is not a race both consoles are in - and the route reached the race first.** Measured on the live session about twenty minutes in, both consoles' top screens were 34.2 (p2) and 38.4 (p1) mean abs from `nsmb-fix` f11500, the capture the docs call the course, against 126.7 from `nsmb-play4` f29500 - and the app's own two consoles differ from each other by 35.7 while the harness's own two course captures differ by 44.5, i.e. the same per-console view spread. So the app route *is* in the harness's state before it ends, and the previous pass's "the two routes do not end in the same state" was reading this dialog. **The drop, from the app's own log.** At +1829.95 s the last exchange of the session: `cmd seq 1940 len 262/266`, then `deauth out/in 1/0` on *both* consoles simultaneously in the same digest tick with the wireless still powered (2/2 on p1, 3/3 on p2), `last ack clientfail`, tx/rx frozen for the rest of the run (tx 221592(+0), rx 106662(+0)), `link mask 0x0`, and the monitor's own verdicts: `p1 looks wedged: the program counter moved in only 6/100 samples while 300 frames ran - the game is spinning rather than running` and `the picture has not changed in 23,230 frames`. That dialog waits for input: pressing A on the guest cleared it (the top screen's red sum went 412,531 -> 3,077,277, the title screen) and the host's picture resumed changing at +300 px per beat while the link stayed down. **The peer effect, on the named instrument.** The complete 102-byte frame the *host's* game is handed carries the guest console's own keypad at byte 40: time-resolved in this run it reads `0x00` in every sample before the press, `0x10` from +2.0 s (the press) through +6.1 s, and `0x00` again from +10.0 s (the release); phase-level in the earlier run it was `0x10` in 36/36 clean frames against `0x00` in all 129 baseline samples (30+34+28+37), and `0x20`/`0x01` for LEFT and A, three buttons and three DS key bits. On the picture side, holding RIGHT on the guest left 1,125 px of the *host's* top screen (bbox 3,120-201,180) that moved under the input and never in any of the three no-input baselines (129 pairs); the session's own traffic shape and rate did not change with the input. **Still inference, and what would settle it:** a value out of NSMB's own RAM for "the peer's world changed" - the app has no memory-read command, so the full-frame dump is the closest instrument it has - and *why* the link drops at +1830 s, which the 120,000-frame harness run was meant to answer and did *not*: that run (120,000 frames, 2,006 emulated seconds, 1.74x realtime, `all consoles kept running`) **never linked at all** - `slots on the bus: 00000`, `sent=0 received=0`, every MP counter zero on both consoles, `deauth out/in=0/0`, both `[not in MP]` - so the documented route did not reproduce in it and it says nothing about the drop; the `.sav` is unchanged in size *and* mtime from the runs that did link, which points at the route's own timing sensitivity (the wizard/boot-flow warning this file already carries) rather than at a shifted save state, and the window itself was covered (frame 120,000 is ~1,830 s of session, the drop's offset). A linked long run is therefore the settling test, not a formality. **Hygiene.** No source line was touched this pass (`history.md`, `to-do.md` and one new evidence file excepted); the load-bearing fixes are untouched as instructed. The decisive evidence is archived as `logs/archive/2026-09-29-app-end-screen-and-keytrace.txt` (the dialog's glyph runs, the keypad trace, the capture distances and the log lines at the drop), the raw live session's log stays in `logs/route_fix.log`, and every throwaway analyzer was deleted. Nothing committed. |
| 2026-09-29 | 0.1.9y | One console's input reaches the shared session byte-for-byte - the guest's own reply carries its keypad as a DS bit mask - and the half that is still inference is the peer's *picture*, because the app route and the harness route do not end in the same state | Asked to close exactly the gap the last pass left as inference: drive a live NSMB session through the app's own control channel, hold distinct input on one console at a time, and show from the game's own output that the input reached the shared session - the peer's received frames and the round-by-round answered traffic changing in step with it, and the peer's picture or state changing as a result, against a no-input baseline so heartbeat traffic cannot be mistaken for coupling; to say plainly what could only be inferred and name what would settle it; to try `MD_LINK_PROBE=1` (only ever code-read) and report what it prints; not to re-litigate the MKDS latch, reply identity, the receive crop or the FCS slot; and not to restructure anything. **What was run, live.** A two-console NSMB session started through `tools/run_app.sh` (autostart 2 players + ROM) and driven only through `tools/appctl.py`: the documented route to the session-data phase, then five timed phases - no input, console 1 holds RIGHT, no input, console 2 holds LEFT, no input - and a second run repeating it with LEFT and A on console 1, ~3.7 samples/s per phase, each sample a `shot` plus `wifiio` for both consoles, with the verbose log's byte offset recorded at every phase boundary (the 742 MB log was read by seeking those offsets, then deleted). Both consoles stayed lockstepped throughout (`p1@61859, p2@61859` and equal at every phase end). **The wire result, and it is byte-exact.** The frame the *host's game* is handed for the guest's reply (`handing client 1's reply to the console's game`, `fc=0118`, 102 bytes as read out of RAM) carries the guest console's own keypad at the byte right after its two-byte counter: `0x10` in 65/66 samples with RIGHT held, `0x20` in 65/66 with LEFT, `0x01` in 69/72 with A, and `0x00` in **every** sample of every no-input phase (45/45, 36/36, 47/48, 32/32, 48/48, 45/45 across the two runs) - three buttons, three DS key bits, nothing in the baselines, and the earlier run's RIGHT phase (0x10) is the same field. The release lag is the driver's, not the game's: the hold runs two seconds past the phase, and the byte returns to `0x00` exactly at that point (`base2` samples 0-5 `0x10`, sample 8 onward `0x00`). So the input reaches the shared session and the frame the peer's game reads changes in step with it. **The traffic does not change shape, which is the negative half of "changing in step":** rounds per phase 706 / 1141 / 712 / 1142 / 711 over 12 / 18 / 12 / 18 / 12 s = 59-63 rounds/s, one per emulated frame, with the same size mix (286-byte session commands and 30-byte management commands, 94- and 30-byte answers) and no `clientfail` in the summary; the button rides an existing round rather than adding one. The host's *visible* state does not move either: the 16 bytes the verbose log prints of the host's 286-byte command (`DE 01 02 00 7E 9D 0F 00 03 00 <counter> 00 00 03 00`) change only in the counter and in the message-kind byte (`7E 9D` for the session command, `00 80` for the management one) in every phase - the host's world is beyond those 16 bytes, so this log cannot answer whether the host's world changed. **The picture, and the state both routes end in.** The app route's end state is pixel-identical to `game-captures/nsmb-play4`'s static screen (mean abs 2.2 for console 2, 11.8 for console 1, and 0.7-2.0 against eighteen of its frames) and ~60 away from the capture the docs call the course (`nsmb-fix` f11500: 60.8) and from the park (`nsmb-park-guest-pleasewait`: 80.2) - while `md_game_session`'s documented route on the *same build* ends 6.9/23.3 from `nsmb-fix` f11500. **The two routes do not end in the same state**, which is the correction this pass makes to v0.1.9x's "the end screen is the course": that reading came from band structure and a 92% distance from the connecting frame, and a direct comparison against the reference capture does not support it. In the app state, holding LEFT on the guest does move the guest's own screen a lot - per-pair change p90 5,147 px against 514-788 px in the baselines, 9,200 px (19% of the top screen) changed cumulatively within two seconds, the scene's repeating banner pattern shifted ~13 px - and holding A adds nothing the baselines lack (median 146 px/pair, max 446, against baseline max 193), the shape of a walk and a jump on a screen whose only other motion is a small animation plus a fade to black every few seconds. The peer's screen moved too, but only a little and not in a way that survives scrutiny: its cumulative change over the hold's first 40 samples is 4,544 px against 1,506 px over the same span in the pre-input baseline, and the two consoles' views *converged* - a steady 16.6 before the input, 9-10 within a second of it (min 4.2), and 8.9-9.0 through the following 60 s of no-input phases. **That convergence is where I stop, and I am labelling it an inference rather than a measurement:** pixels cannot separate "the host's game applied the guest's movement and the shared view reconciled" from "the host's screen changed on its own". What would settle it is reading a character's position out of the game's own RAM (or a full 286-byte dump of the host's command - the log prints 16 bytes and `wifiio`'s handed frame for the guest is a 40-byte `fc=0218` frame, not the session command). **The harness A/B, which is where the negative is.** Same documented route in `md_game_session`, two arms, `--shot-every 100`, one difference: console 1 holds RIGHT from frame 9000. End frames' screens both match the documented course state (6.9/23.3), rounds 9,197 vs 9,212, `all consoles kept running`, 2.16x and 2.19x realtime. Per-100-frame change on the top screen, f9000-11900 vs f5000-8900: input arm p1 3,743/3,821, p2 3,150/3,231; control arm p1 3,778/3,785, p2 3,176/3,225 - i.e. **holding the D-pad for 3,000 frames changes neither console's screen measurably** on a scene that animates ~3,800 px per 100 frames (an order of magnitude more per frame than the app session's state). Caveat stated plainly: the harness's digest prints only the *last* answer and in the input arm that was the 6-byte management one (`00 80 FA 1A B8 B6`), so the harness's own output does not show the keypad byte there - the mechanism was verified in the app, on the same core. **`MD_LINK_PROBE=1`, tried as asked, and what it prints.** Not a line, and no window: the monitor's first gate is `md_link_connected_mask()` needing two bits, and that mask *is* the consoles' wireless power, so the probe needs a linked pair that has received nothing for ten seconds. With `MD_LINK_PROBE=1 MD_LINK_VERBOSE=1` on a two-console NSMB session the link mask was `0x0` (a title screen never powers the radio), on the diag ROM `0x0` as well, and in a live linked session `0x3` with packets in every window - the monitor's always-on arm, which is real traffic and needs no probe. A `Link test` click powers radios on and back off (`link mask 0x0` again 30 s later) and its own injection reports `{"injected":true,"mode":"inject","results":[{"from":1,...,"received":false},{"from":2,...,"received":false}]}` with a resync on both consoles - the same unhelpful FAILED v0.1.9x named, and here on a game that does not poll its own radio. On success the probe logs nothing at all (it bumps `verifiedWindows`/`lastOkMsAgo`); on failure it logs `link monitor: bus probe p1->p2 not delivered - resyncing both consoles`. BUILD.md now carries that gate chain next to the knob, because the next person to go looking for probe lines should not have to read the monitor to learn that they cannot reach a window from a title screen. **Hygiene.** No source line was touched this pass (BUILD.md, `to-do.md` and this row excepted): the change under review is untouched by design, and the load-bearing fixes were left alone as instructed. Every thrownaway analyzer was deleted, the decisive captures are archived as `logs/archive/2026-09-29-input-coupling.tar.gz` (90 KB: the five phases' screens at nine points each plus the four harness end-state frames), both app instances were closed by pid (never `pkill -f`), and a 742 MB verbose app log was deleted - the disk was at 99%. Nothing committed. |
| 2026-09-29 | 0.1.9x | The app's own surface, driven as a player: the documented NSMB route plays through the control channel on the leaner tree, and the only things broken were two empty-state sentences | Asked to stop trusting the prune and exercise the app's own surface on the tree that now exists - start it as a player would, drive the documented NSMB route through its control channel until both consoles are genuinely in the same race, land the MKDS route where it lands, and check that the surviving commands (`shot`, `see`, `ui`, `vary`, the digests) and `run_app.sh`'s env forwarding behave as documented, saying explicitly what was run and what could not be. **What was run.** `cargo build --release` clean after the prune (1m22s, then 1m37s for the one fix below); smoke **66/0** twice. **MKDS through the app** (`tools/mkds_join.py --restart --no-dialogs --cut-off --vs --watch 60`): the join holds, CUT OFF lands, VS is picked, then the host's command walks to `FF 00`, the guest adopts it (`00 80 00 00 B8 B6`), both consoles stay `ACTIVE` with `deauth 0/0` and slots `0b11` - where it always lands, unchanged. **The documented NSMB route through the app's own control channel** (autostart two players plus the ROM, then `appctl.py` only - title menu `(128,95)` at frame 600, multi-card `(128,72)` at 1300, character select at 2000, Yes `(128,71)` every 150 frames 2600-4200, OK `(128,171)` 4200-6500, `A` on both 6500-9000): by the end the wire is in the *session-data* phase - commands 262/266 bytes with body `DE 01 02 00 7E 9D 0F 00 03 00 ..`, answers 70 bytes `1F 8D ..`, `clientfail 0000` - both pictures change on nearly every frame (`picture changing (0f ago)(+285)` of 300), and the end screen is the course: it carries the course's band structure (HUD band y 3..14 on the top screen, banner bands y 40..57 and y 151..155 on the bottom) and differs from the documented connecting frame by 92%, while the core run's own two course captures differ from each other by 3.8%. **The same route through `md_game_session` on the same build** is the control and it plays: 12000 frames, 9208 rounds, 262/266-byte commands, 70-byte answers, 0 `deauth`, 0 `clientfail`, 3.82x realtime, `all consoles kept running`, and its captures match the reference course frame at 3-4% from frame 5000 on. **Every surviving command was driven live:** `status`, `ui`, `see`, `bands`, `shot`, `render`, `press`, `touch`, `untouch`, `click`, `text`, `eval`, `advance`, `waitfor`, `index`, `vary`, `wifiio`, `quit`; a `vary body --value 5` armed on a live game and read back `applied 5` on `vary off`; `wifiio` printed the handed frame and the RX window; the careless arguments answered with sentences (`press 0xZZZZ`, `touch 300 190` with its outside-the-digitizer warning, `script /missing.txt`, `see --player 9`, `click '#nope'`, `vary bogus`, `advance 30`); `shot` on an app with no session exits 1 with `the page returned no PNG (no session, or empty frames)`; and `MD_SHOTS_DIR` was verified end to end - `shot --name fwdshot` wrote `/tmp/mdshots/fwdshot.png` - so at least one of the two env forwards the prune wired is reachable through the documented launcher. **Two defects, one empty state, both fixed and re-verified.** On an app with no session, the first two commands a driver reaches for answered nonsense: `see` said `(player 1 out of range: 1..0)` and `bands` said `(player 1 of 0) -- out of range` - a range with no player in it, where the truth is that nothing is running. Both now say `(no session: no console is running)`: `D.ascii` in the control channel's page-side reader gained the guard `D.shot` already had (it was the one path that checked `pl.n < 1`), and the client's `BANDS_JS` gained the same check before its range test. Verified on an idle app and re-verified with a live two-console session in the same run. **What parked twice before that, and why it was my driving and not the app.** The first two app-driven attempts ended in the game's own discovery screen, and the reason was the environment, not the surface: `pkill -f melonDS-Multipl` - the pattern this repo's own scripts warn about, twice, in writing - killed the *calling shell* (its command line carries the pattern) and left the app it was meant to kill running with its session, still holding the control port; every later `status` was then answered by that stale session, whose frames I read as the route's (11209 frames at a moment when the new app's own health line said 1198, and `111532` later). A route paced on a counter that belongs to another session lands every tap at the wrong emulated frame, which is exactly the park I then spent two runs blaming the app for. With one app, and a sanity check that the frames cannot outrun the session age (a 60 fps console cannot have run more than 90 frames per elapsed second), the same route reached the course first try. The lesson is cheap and worth keeping: `status` prints `up Ns` next to the frames, and `run_app.sh` kills the previous instance by pid with a one-second grace and no verification - a driver should compare the two numbers before believing a route, and should never use `pkill -f` on this binary. **Left open, honestly.** `key Bogus` still succeeds silently (a real check needs the frontend's own key map, and a second hardcoded list is the trap v0.1.9t already named); the Link test's injection arm is unchanged; the MKDS latch is unchanged and is still the project's biggest question; and `to-do.md`'s closed-item narrative (72 items, ~641 lines) is still the largest block of duplication in the change. **Structural health, from the files this pass worked through:** `src-tauri/src/lib.rs` (~3.5k lines) now owns the app, its test-drive control surface (including the page-side reader as a JS string) and the health/recorder/link monitor in one file - that is the file a maintainer would split next; `src/Wifi.cpp` (~4.3k) carries the fork's instrumentation inside melonDS's emulation; `src/net/LocalMP.cpp` owns the bus and the framing; the prune's edits landed in `lib.rs`, `Bridge.cpp`, `melonds_bridge.h`, `Wifi.cpp/.h`, `LocalMP.cpp`, `appctl.py` and `run_app.sh`, and this pass added exactly two guards plus this row. Nothing committed. |
| 2026-09-28 | 0.1.9w | Deletion-first cleanup: the superseded instruments are gone, the probe chain is one kind on one path, the measurement narratives live in this table instead of in the code, and the NSMB route still plays | Asked to pay for the proving - delete the superseded instrumentation, dead helpers and redundant comments from a diff that had grown to ~9k added lines across 28 files, consolidate what remains into one diagnostic path, and keep every load-bearing fix intact (the FCS placement, the receive crop, the reply identity, the shared clock, the reply-slot arm handling, the replacement of the fabricated stand-in), with smoke at 66/0 and the NSMB route still playing on the leaner tree. **What was deleted.** The masked-probe machinery that four matrix passes were built on and that nothing left in the tree drives: probe kinds 2/3/4 (a dropped reply, an answer delivered a round late, the peer-echo control), the `mask` argument and the 16-bit reach computation, the `side` selection (it was redundant by construction - a guest never collects a reply: `MPClientReplyMask` is set only in the command slot's own TX path, so player 1's probe can only fire on the reply path and player 2's only on the command path), the per-cell log with its state-halfword change detection, and `MPProbeStash`/`MPProbeSkipped`. That chain ran through seven places - `Wifi::SetMPProbe`/`MPProbeBodyHook`/`GetMPProbe`, the bridge's `md_link_mp_vary`, the Rust FFI declaration, `ctrl_mp_vary`, the `vary` dispatcher's kind/side/mask spellings, and `appctl.py`'s `vary` - and all seven are now the one kind that fires (kind 1: replace the body the console's game reads, `000` `value` for "until cleared"), with the one-line-per-arm log line (`probe applied`) instead of a line per delivery. Deleted with their drivers: `tools/probe_transition.py` (its own docstring named `probe_matrix.py` as its successor and that file is gone), plus the `probe_matrix`/`probe_parked`/`guest_exit`/`relay_audit` sections of BUILD.md, which had been describing tools that no longer exist. Also deleted: a duplicate `arm_persist()` comment in `Wifi.cpp` that had been left above the real one and said the exact opposite of the code ("*Off by default*", where the default is on and has been since v0.1.9i) - found by reading, and unfixable by measurement; and a literally duplicated line in `to-do.md`'s top item. **What remains, and why each is not replaceable.** The code diff is now ~5.2k added lines, and what is left is capability rather than residue: the control channel (`MD_CONTROL_PORT`, `tools/appctl.py`, the `CTRL_PRELUDE` page-side reader) as one test-drive surface; the app's own log and digest (the flight recorder, the `mp pN`/`mp pN irq`/`mp pN hw`/`mp pN last` lines, `md_link_mp_digest` with its struct-size self-check, the liveness `picture` in the health line); the `MPIo` read recorder behind `wifiio` (kept rather than cut: it answers the question that is still open - which value a game's own bytecode branches on - and it is the one instrument whose consumer is the open MKDS item, not a closed one); the documented A/B arms (`MD_MP_FCS`, `MD_RX_NOCROP`, `MD_STOCK_MP`, `MD_REPLY_KEEP`, `MD_ARM_PERSIST`, `MD_CMD_RETRY_NOTIFY`, `MD_LINK_VERBOSE`, `MD_LINK_PROBE`) with, for each, the arm and the result it produced; and the load-bearing fixes themselves, untouched (only their prose was cut). The narrative measurements that used to sit in `Wifi.cpp`/`Wifi.h`/`LocalMP.cpp` as multi-paragraph comments (~1095 added comment lines, of which the largest single block was 50) are now one-sentence rationales with a `history.md v0.1.9x` pointer: GBATEK's TX note, the crop's load-bearing measurement, the reply-identity investigation, the reply-window staleness sweep, the arm-persist A/B and the `MPIo` rationale all read from this table instead, which is where the evidence was already written down twice. **Verified on the leaner tree, in order.** `cmake --build build-tauri` clean; smoke **66/0** after every C++ edit (three times); `cargo build --release` clean, so the reduced `md_link_mp_vary` FFI declaration and the `vary` dispatcher match what the core now provides; `tools/appctl.py vary --help` shows only `off`/`body`; `python3 -m py_compile` on the three kept tools. The route that the whole project is judged by: `md_game_session` on New Super Mario Bros., 12000 frames, the documented script - **`all consoles kept running`**, 9196 rounds, commands 262/266 bytes, answers 70 bytes, `blank 0`, `canned 0`, `clientfail 0`, `deauth out/in 0/0`, both pictures `last change 0 frames ago`, 4.04x realtime. Nothing committed. **One knob was wired up rather than deleted, because it had a consumer and no way to reach it:** `run_app.sh`'s supervisor passes an explicit environment list to the app, and that list had been missing `MD_LINK_PROBE` (documented in the script's own header comment *and* in BUILD.md, and read by the link monitor in `lib.rs`) and `MD_SHOTS_DIR` (read by the control channel's `shot`), so both were unreachable through the documented launcher; they are forwarded now. **Named, not fixed:** the largest remaining redundancy is `to-do.md`, whose 72 closed items (~641 lines) restate, in less detail, what this table's rows already record - collapsing them to one line each with a version pointer is the next deletion pass, and it was left alone here because the roadmap's open items are interleaved with them and each closed item had to be matched to its row by hand. |
| 2026-09-28 | 0.1.9v | A reply is a new transfer: the retry marker that made a fresh answer look like a retransmission is gone, the reply buffer's own bytes are still stock's, and MKDS's latch is not an identity problem | Asked to settle what real hardware puts in a reply's identity fields for a given round - the 802.11 sequence number and retry bit, the DS frame control, the reply slot, the round the answer belongs to - from GBATEK, upstream melonDS and the frames the games themselves write, then fix whatever we synthesize that makes a fresh answer indistinguishable from a retransmission, and re-run MKDS with NSMB as the control in the same build. **What the hardware does, from the sources.** GBATEK (DS Wifi Transmit Control, `4808210h W_TX_SEQNO`) has the sequence number "increments on IRQ07 (Transmit Start Interrupt)" and, when enabled in `W_TXBUF_LOCn.Bit13`, "replaces the upper 12bit of the IEEE Frame Header's Sequence Control value" - so every *transfer* gets a fresh number and a retransmission inside one transfer keeps the number it started with. GBATEK's list of what the hardware edits in the IEEE header (DS Wifi Hardware Headers) is the Sequence Control value, `FC.Bit12` and the protocol version, so the Retry bit is the transfer's own and not a field the hardware invents. The byte at the transmit buffer's offset 4 is `TXHDR[04h]`, `00h = use W_TX_SEQNO, 01h = force NOT to use W_TX_SEQNO, 02h = same, 03h..FFh = error`, and "Entries [00h] and [05h] are modified by hardware, all other entries are kept unchanged" - the hardware never writes it. The reply slot's own values are the hardware's: `0Eh REPLY frame (data) ;FC=0118h`, `0Fh REPLY frame (empty) ;FC=0158h`, and the fork's `SendMPDefaultReply`/`SendMPReply` already match. Upstream melonDS's `TXSendFrame` and `IncrementTXCount` are byte-identical to this fork's; what they do on the reply slot is treat *any* nonzero `TXHDR[04h]` as "this buffer was already transmitted", keep the frame's old sequence number and set the retry bit - and the emulator itself writes that byte (`IncrementTXCount` at the end of every reply transfer), so the flag lands on the second and later use of any reply buffer. **Measured before and after, on the receiving side as well as the sending side.** Delivered replies that carry a retry marker and a sequence number that repeats the previous one: 1261, 2891, 3388 and 6633 in four pre-fix MKDS runs (of 11425, 16898, 8479 and 17535 delivered) - and of those, only 0-3 per run rode a body the previous frame did not have, which is the exact "a fresh answer is indistinguishable from a retransmission" shape the request names. The commoner shape is a *fresh round* answered by a byte-identical re-send: in the 18:11 run the guest's answer for `cmd seq=0880` was the very arm (`#23`) that had already answered `cmd seq=0850`, 2327 of 16905 answers being sent from an arm an earlier poll had spent. After the change, on the same route in the same build: **15085 of 15085 replies carry `fc=0118`, 0 sequence numbers repeat, 0 retry markers reach the host's game** (`logs/archive/md_app.log-20260928-latch8-postfix.gz`), with the 1490 re-serves of an already-spent arm now going out as new frames. **The reply buffer's own bytes turn out to be load-bearing, and that is why the fix is the identity decision and not the write.** Two builds this pass that stopped writing stock's reply count parked the NSMB route that plays with it - one writing neither byte of the hardware header, one keeping only GBATEK's documented `TXHDR[05h]` clear; both froze at ~frame 2800 of 12000 with 9200+ answers still crossing the link (the same shape as the mirror-image v0.1.9s crop finding) - while both builds that kept the write played (`all consoles kept running`, 9208/9209 rounds, both pictures `last change 0 frames ago`). So the code change is one decision: the reply slot's `noseqno` is 0, i.e. every reply takes a fresh `W_TXSeqNo` value, and the retry bit is never synthesized for it (`num != 5`); every other slot keeps stock's behaviour, because there the byte models the games' own retry loops (MKDS's repeated commands, `W_TXSlotCmd` bit 14). A game that deliberately asks for its own sequence number cannot be honoured while stock's count occupies that byte, and neither game here was seen to need it: a build that read the byte at arm time - the one moment it is not ours - put a retry marker on 6633 of MKDS's 17535 answers, i.e. it was mostly reading the count back. **The re-run, and the answer to the reopening question: no.** MKDS with the fix reaches the same place as without it - join holds, CUT OFF lands, VS is pressed, both consoles end on the **same** static screen (`45d15630`), `LINK OFF`, the host's picture frozen, `clientfail 122`, the guest answering every round (`answered 15085`, `blank 0`, `canned 1`) up to the moment its own radio goes off - so with every answer a brand-new frame the host's game still does not register the guest's changed state and no race starts. NSMB is the control and plays in the same build (`all consoles kept running`, 9209 rounds, 0 retry markers on its replies before and after: it never re-uses a reply buffer in a way that trips stock's rule). The lever is therefore closed as an explanation of the latch, not as a defect: the wire is now what the hardware would put there, and the latch is in the games. **Two instrument defects, both found by producing nonsense first.** (1) The `answer sent` log line printed the recorded body with `%s` on raw bytes, so any body whose first byte is `00h` - most of MKDS's answers - printed as `body[6]=` and nothing; a check of "does a retry marker ride new content" built on those lines was comparing one empty string to another. It is hex now (`mp_body_hex`, display only; the digest still carries the raw bytes for the Rust side). (2) The app binary links `build-tauri`'s static libraries and is only relinked when the Rust crate is rebuilt, so the first MKDS run of this pass measured the *previous* pass's core while believing it was the new one; the app was rebuilt (`touch src-tauri/src/lib.rs && cargo build --release --manifest-path src-tauri/Cargo.toml`) before the post-fix run, and that step is now needed after every core change that is going to be driven through the app. Validation: smoke **66/0** after every C++ build; the NSMB control arm twice plus two discarded builds A/B'd on it; MKDS pre-fix and post-fix route runs of the documented command, both traces archived as gzip; nothing committed. Left alone as instructed: the corrected framing, the shared clock, the verified tap map and the reply path's arm handling. |
| 2026-09-28 | 0.1.9u | The masked matrices were never writing into the FCS slot: on both delivery paths the mask lands inside the body the peer's game is handed, and the recorded bodies prove it byte for byte - so MKDS's state value *was* varied, on a delivered field, and both games still do not move | Asked to re-run the masked-state matrix against the field the peer's game actually receives (v0.1.9t's reading was that four passes had been writing into the FCS slot and measuring nothing), on both sides, with a null control, repeated cells and both park shapes, watching each game's own body, screen hash and the wire - and to settle what the latched picture is on each console by reading it, not by assuming the stock arm's. **The premise is false and the retraction is measured, not argued.** The command body the guest's game is handed is **ten** bytes, not six: the digest's own line reads `mp p2 last: cmd len 10 body BE 01 02 00 00 80 FF 00 B8 B6` - clienttime, clientmask, flags, state, marker - so the six bytes `BE 01 02 00 00 80` that v0.1.9t read as "MKDS's real command body" are its first three fields, and the two-byte field at offset 4 that v0.1.9t wanted varied is the *flags*, not a state. Mask `0x00C0` is body bytes 6-7 = the host's state, inside the delivered body; the cell log says so in one line (`probe cell (commands): inject [00 00 00 00 00 00 02 00] mask 00C0 over the peer's [BE 01 02 00 00 80 FF 00] (8 of 10 body bytes), ... applied 242`), and the stronger proof is the guest's *own* record of what its game was handed, written by `mp_record_body` **after** the hook out of the same `RXBuffer` the game DMAs: one cell per five seconds, at offsets 6-7 of `last: cmd ... body` - `02 00` (+89.8s), `03 00` (+94.8s), `01 00` (+99.8s), `00 00` (+104.8s), `06 00` (+109.9s), `04 00` (+114.9s), `5A 5A` (+119.9s), `11 01` (+124.9s) - while the *wire* still carried the host's own `FF 00` (the cell log's "over the peer's" field), and the guest's answer stayed `00 80 00 00 B8 B6` through every one of them. **The host side is the same experiment and the same result.** The reply the host's game is handed is a 30-byte frame with a **six**-byte body (`handing client 1's reply to the console's game: 30 byte frame, fc=0118, body[6]=00 80 00 00 B8 B6`), so mask `0x000C` is its bytes 2-3 = the guest's state, delivered - and the host-side cells are visible in that same trace, as values the guest's own walk (which went `01 00`..`01 08`) never wrote: `body[6]=00 80 03 00 B8 B6` and `00 80 04 00 B8 B6`. So 24 host-side and 13+ guest-side cells did move delivered bytes, `applied` climbed into the thousands, and no cell moved either console. **What that retracts and what it leaves standing:** v0.1.9s's crop finding stands unchanged - `MD_RX_NOCROP=1` parks the NSMB route that plays with the crop, so the crop is load-bearing - but the inference drawn from it does not: the "last four bytes of the sender's buffer" are the last four of the *sender's* 14-byte wire frame, which the receiver's crop drops, and they are not payload offsets 6-7 of the 10-byte body the receiver's *game* is handed. **The latched picture, read this time** (`logs/shots/mkds-latch2.png`, bright-on-dark and dark-on-bright mono renders + per-glyph blocks): the **guest's top screen** is a dark panel (y 49..191) holding two left-aligned lines of bright text, and line 2 decodes glyph by glyph as **`Please wait a moment`** (word widths 43 / 26 / 6 / 51 px = Please / wait / a / moment, each letter read off its own block); line 1 is a second short sentence starting `A` (52 / 34 / 51 px words) whose letters the reads did not resolve, and it is reported as undecoded rather than guessed. The guest's **bottom** screen is a black list box (x 290..477, y 192..336) with eight 18-px rows and a two-button strip at y 365..371. The **host's** screens are different ones: its top is a smooth vertical gradient (row mean 225 at y 0 to 100 at y 168) with a single small bright patch (x 183..247, y ~100..125) and no text bands at all, and its bottom is a large light panel (y 16..119) with one small dark block inside (x 96..175, y 104..115) over a structured dark strip (y 163..184). Stated plainly: at the latch the two consoles are on **different** screens, the guest is on MKDS's own *waiting* message rather than an error dialog, and no error string is decodable on either console - MKDS is waiting, not failing. **The live re-run was attempted and the route's join failed this time** (fourth run of the documented command): the host entered its room and beaconed alone for 21369 frames / ~6 min (`link mask 0x1`, `slots 0b01`, only `slot 4 (BEACON)` frames) while the guest's console logged **zero** `[mp]` lines - it never powered its radio at all. The route's own output stops after `[p1-normal]` plus `screenshot logs/shots/p1-room.png` - the end of *P1's* walk - and says nothing else in the 4.5 minutes before the run was stopped by hand; there is no traceback and no error line in it, and the app itself was healthy throughout (it answered the control channel in that window: `status` reporting `link mask 0x1` and both consoles still producing frames, `ui`, `see` and an input injection all working). So whether the route was stalled inside its first tap on the *second* console or was only slow is **unresolved**, and the reason it is unresolvable is the tooling: `walk()` prints its per-step report only *after* each tap, and the `--out` timeline is written only in `main`'s `finally`, so a run that is killed (here by hand after the stall) leaves nothing behind except the last flushed report. That is fixed in this pass rather than worked around - `walk()` now logs one line *before* every tap, naming the console, the step and the coordinate - so the next attempt can say where it stopped. What the attempt did establish stands on its own: **the guest's console never entered multiplayer while the host sat in its room** (zero `[mp]` lines on p2, `slots 0000`, `pwr 0200/0002`, its `irq [none]` all through; `link mask 0x1`), which is the known ~1-in-4 join flake seen from a couch - player 2's console in a menu while player 1 waits in the room, with nothing on screen saying why. Input to that console is still delivered while it has no MP role (`held buttons 0x1 on player 2 for 120ms`, its own picture changing at 57 fps). **Next lever, named in to-do.md and not guessed:** the identity of the answer rather than its content - every reply after the guest's first goes on the wire with the *same* 802.11 sequence number and the retry bit set (our own `IncrementTXCount`-driven `noseqno = 2`, upstream's behaviour too), where the hardware would have stamped a fresh sequence number per transmission, so a host stack that dedupes retransmissions would consume one answer and wait forever - which is the shape of the latch exactly. Validation: smoke **66/0** re-run after the doc edits; the archived cell runs re-read byte for byte; the latch screenshot read through the offline renderer (`appctl.py render --mono`/`--bright`, `tools/glyphs.py`); the failed-join run's report and its last 40 `[mp]` lines kept as `logs/mkds_r6_join-evidence.txt` with the 1.8 MB verbose trace deleted. One tooling fix out of the failed attempt: `tools/mkds_join.py`'s `walk()` logs a line before every tap now, so a run that is killed mid-step says which console and which tap it had reached. Nothing committed. Left alone as instructed: the framing fix, the shared clock, the verified tap map and the reply path's arm handling. |
| 2026-09-28 | 0.1.9t | MKDS's latch, tested the way the NSMB route has to be tested: no input from either console moves it, and the differential is a session-data phase NSMB's games open and MKDS's never do | Asked to drive each console's own input at MKDS's post-VS latch - the full button set *and* a touch where the screen's own button sits, on the console that displays it, not just P1's A - and to name what NSMB exchanges once its shared screen appears that MKDS's never does. **The latch is reproducible on demand** (`tools/mkds_join.py --restart --no-dialogs --cut-off --vs`, three runs): CUT OFF lands, the host's command state reaches `FF 00`, the guest's answer adopts it (`00 80 00 00 B8 B6`), and there the two games sit - host polling, guest answering, `deauth 0/0`, `blank 0`, `canned 1`, `clientfail` a handful - for the rest of the run (one held 18550 frames / 314 s). **The input test, both consoles, at the latch: nothing responds.** The host's screen is *frozen* there, so any change would be a response; the full button set (`0xFFF`, 1.5 s) on p1, then on p2, then six touches across each console's own *bottom* screen (including (128,160) and (128,96), where a modal's buttons sit) changed neither the host's screen (md5 `a5d0b8f1` before and after every step) nor the exchanged state (host cmd still `FF 00`, guest answer still `00 00 00 00`), and the link stayed up (`slots 0b11`) across the whole battery. A and START on both consoles: same. So neither game is waiting on its player, on either console - which is what the NSMB route does differently (it drives *both* consoles' touch screens, and its games answer). **The differential, measured on both titles in this build** (frame ledger, `MD_LINK_VERBOSE=1`, the documented routes): NSMB 9209 commands (6209 x **290 B**, 3000 x 34 B) / 9207 replies (6320 x **94 B**, 2887 x 30 B) / 763 beacons x 110 B / 4 LOC2; MKDS 3664 commands (3653 x **34 B**, plus 4 x 62, 2 x 42, 2 x 238, 2 x 402 - every one of the big ones in the join's own roster exchange, seconds *before* the transition) / 3662 replies (3636 x **30 B**, 22 x 68, 3 x 48) / 197 beacons x 108 B / 5 LOC2-3. Once the shared screen is up, NSMB's two games open a **session-data phase** (266-byte command bodies, 70-byte answers) and MKDS's never write a frame larger than the lobby's 34-byte command / 30-byte reply. Nothing about that can be a size limit, a refusal or a loss on our side: the same transport carries NSMB's 290-byte session frames *and* MKDS's own 402-byte join frames, and every MKDS frame that is written is delivered and answered (`blank 0`, `canned 1`, no deauth while the latch held). **Stated plainly: MKDS's latch is inside the games** - nothing a player at either console can do, and nothing our delivery does, changes it. The next lever is named in to-do.md rather than guessed at: with the FCS slot known invisible to the peer (v0.1.9s), MKDS's real command body is six bytes - `BE 01 | 02 00 | 00 80` - so the masked-state experiment should be re-run with its mask inside the one two-byte field at offset 4, where four passes of matrices were writing into the FCS slot and measuring nothing. **Playtest:** reloading the window mid-session (`eval location.reload()`) leaves the Rust side and the session intact (status, UI and frames all fine afterwards) but silences the *webview* for over 20 s, which aborted the command that triggered the reload; `appctl.py` now retries the read-only webview commands once and names the reload in the failure. `press 0xZZZZ` and `script /missing.txt` were tracebacks and are sentences now. Left open and named: `key Bogus` is still accepted in silence - a proper check needs the frontend's own key map, and a second hardcoded list is the drift trap this repo has already been bitten by. Validation: smoke **66/0**; three MKDS latch runs; the NSMB control arm; the frame ledgers above. Nothing committed. |
| 2026-09-28 | 0.1.9s | MKDS under the corrected framing: the two games adopt the transition together and stand on the *same* screen, and the four bytes a receiver drops are measured to be the sender's FCS slot rather than payload | Asked to re-run the MKDS route under v0.1.9r's framing with both consoles' screens read back at every step, name the first divergence from the NSMB reference, fix what traces to us, and re-confirm NSMB in the same build. **NSMB is the control and it plays**: the documented 12000-frame route ends `all consoles kept running`, both consoles `last change 0 frames ago`, 9208 command rounds, 262/266-byte payloads, `deauth 0/0`. **MKDS now reaches the transition on *both* consoles and stops there.** Two runs of `tools/mkds_join.py --restart --no-dialogs --cut-off --vs`: the join holds, CUT OFF lands with the group alive (`patch luma 103 -> 149`), the host's command walks to `FF 00`, the guest's answer adopts it (`00 80 5A 5A` -> `00 80 00 00`), and the two consoles' screens become **byte-identical** - run 1's `6b542277` is byte-for-byte the picture the stock-reference arm left behind as `logs/shots/stock-postvs.png`, run 2 landed on `45d15630` (all black) - where the last recorded state had the host black and the guest on its own modal, i.e. *different* screens. Then nothing: the picture never changes again (25-30s of watch), `A` on P1 changes nothing, and the link under it stays perfect - `answered 3550 of 3551`, `blank 0`, `canned 1`, `clientfail 0-5`, `collected` 1748 -> 3246, **0 deauths in either run**. So the two games adopt the transition together, end on one screen, and neither advances; no race, and nothing in the delivery traces to us. **What the four dropped bytes are, measured rather than argued.** New reference arm `MD_RX_NOCROP=1` (skips the receive-side `W_RX_LEN_CROP` crop in `Wifi::CheckRX`/`MPClientReplyRX`) with `MD_MP_FCS=0`: the NSMB route that plays with the crop **parks without it** - the guest's answer freezes at `00 80 5A 5A B8 B6`, the host's state at `0F 00`, and a console dies - so handing the peer's game four *more* bytes reproduces the park, and the crop is load-bearing. GBATEK's TX note (the sender's length counts the 4-byte FCS; the last four buffer bytes are the hardware's slot) and the fork's own frames agree: `SendMPAck` is 32 bytes for a 4-byte body, `SendMPDefaultReply` 28 bytes for an empty one. Consequence, and it corrects three passes of model: the "host state at payload offset 6" and "guest state at offset 2" the masked probe matrices moved through are the **last four bytes of the sender's buffer, which the peer never receives** - so those matrices (24 host-side cells, 13 guest-side cells, 2662 injections) wrote into a region that is not on the wire, which is why no cell ever moved either console; `MD_ARM_PERSIST`'s rationale (a firmware stand-in "asserting an invented peer state") wants re-reading even though its measured effect on blanks/deauths stands. **Playtest through the app's own surface, four defects fixed**, all in the tooling and all substantiated by driving it: `click` on a **disabled** control reported `clicked #selftest` while nothing happened (the button is disabled on an idle app); `press`/`touch`/`untouch` reported success with **no session running** (`held buttons 0x1 on player 1 for 120ms` against an app with no consoles); `advance 5000` - how it gets typed - died in argparse with `unrecognized arguments`; and `run_app.sh` did not pass `MD_RX_NOCROP` through. The Link-test finding was promoted to the top of to-do.md with its measurement, which also corrects v0.1.9q's reading of it. Validation: smoke **66/0** with the knob on and off; the NSMB control arm; two full MKDS route runs; the app rebuilt and driven through its control channel for every fix. Nothing committed. |
| 2026-09-28 | 0.1.9r | The park was four bytes of framing, not the broadcast direction: the FCS now goes *in* the frame's own trailing 4 bytes, and the NSMB two-player route reaches gameplay on both consoles again | Asked to audit the broadcast direction properly - which frame types each game broadcasts, how many of each actually reach the peer's *game*, what our delivery does differently for a broadcast than for a unicast command or reply, including any dependence on the console's associating state - fix whatever keeps the guest's game from seeing the host's announcements, and then re-run the NSMB route, whose observable is unambiguous: `Searching for <peer>...` becoming `<name> has appeared. Play a game?` on both consoles and then a playable round. **The broadcast direction is intact, and that is a measured negative result rather than an assumption.** On the parked run with the slot ledger and `MD_LINK_VERBOSE=1` (12000 frames, 43 MB of trace, the route that parks reproduced to the counter): the host armed its beacon once and sent 763 `fc=0080` beacons, and the guest's *game* was handed **763** of them (110 of them before it associated, through `CheckRX`; the rest held to their timestamps through the USTimer delivery); the guest sent exactly two broadcasts - the `fc=00B0` association request and an `fc=0000` frame - and the host's *game* was handed **both**; the host's 9212 commands and 9212 acks and the guest's 9212 replies were handed over count for count. The 97 extra `fc=0080` frames the earlier relay audit had lumped in with the host's beacons are the *console's own* `melonAP` beacons (`outside multiplayer, from the regular receive path`), which is why the earlier reading of `21 delivered` was about the local AP path and not the host's broadcasts. There is no separate broadcast path to lose frames on: `MP_RecvPacket` and `MP_RecvHostPacket` read the same packet queue, and `TXSendFrame` sends the beacon/LOC slots out of the same `MP_SendPacket` as a unicast frame, with no dependence on `IsMP`/`IsMPClient` (the only associating-state gates are the management-frame BSSID/RXFilter check and the client's hold, both of which the delivery counts show working). **What actually blocked the games was the FCS the transport added in v0.1.9f.** GBATEK's TX note (W_TXBUF_LOCn[0Ah]) says the *sender's* length *includes* a 4-byte FCS and "the hardware generates the FCS for you, but you still must include it in the packet length", while the *receiver's* RX header length is "excluding FCS checksum" - which is what `W_RX_LEN_CROP` ("Used to exclude the FCS ... from the length entry") does with the 2 halfwords both games program. v0.1.9f appended the CRC *after* the frame and raised the frame's length by 4 so the receive path's own check stayed consistent, which made the length every game *read* 4 bytes longer than the protocol expects (the guest's offered command body went from `DE 01 02 00 00 80` to `DE 01 02 00 00 80 5A 5A B8 B6`). The fix writes the computed CRC **into the frame's own trailing 4 bytes** and leaves the length alone: the wire length is the game's own length field, the peer's crop takes the FCS off the length entry exactly as the hardware does, and the peer's game is handed header+body. **The A/B, same command, same save, 12000 frames, one env var apart:** `MD_MP_FCS=0` (the game's own bytes left in the FCS slot, i.e. stock's wire) - both consoles `running`, 6942/6498 picture beats, `last change 0 frames ago`, commands growing to 262-byte session payloads, 9212 rounds, `deauth 0/0`, 0 empty/0 canned windows; the *appended* form (the previous default) - both consoles `FROZEN PICTURE`, last change 9215/9206 frames before the end, `Searching for ...`/`Please Wait.` on screen, 9208 rounds with the same flawless counters; the in-place write - both consoles `running` again, 6994/6575 beats, `last change 0 frames ago`, 262-byte payloads, 9212 rounds, `clientfail 4`, `all consoles kept running`. **The observable, at the screen level:** every 100 frames of the route are captured (`game-captures/nsmb-discover`), and the two consoles' screens show each console's own two-line discovery text at frames 2600 and 2800 (the frames differ - each console names the peer it found), become **byte-identical** at frame 3000 (both consoles on the same screen, md5 `4679a441e6b31502bdf99f2a179f7af9`), and from there each console shows its own view of the same game - the bottom screen's Vs. setup UI at 4300, the same course with the HUD at 11500 (`game-captures/nsmb-fix`, 44 frames). So both games pass the discovery step (each console's own "has appeared" dialog is the 2600/2800 pair, and the shared screen follows), and a round is playable: 200 emulated seconds of a live linked match at 4.35-6.74x realtime, with the game's own 262-byte session data in both directions and no deauth on either console. `MD_MP_FCS=0` and the default are now the same framing (differing only in the four bytes inside the FCS slot, which the receiving console's length excludes), so the knob is a fidelity A/B and not a behaviour switch. Validation: smoke **66/0** before and after; the app rebuilt and driven through its own control channel (autostart of two consoles, lockstep frame counters, the Link-test button still printing its known unhelpful `FAILED` on games that have not powered their radio - the open item at the top of to-do.md); three route arms plus the 100-frame-granularity screen run; nothing committed. Left alone as instructed: the shared clock, the verified tap map and the reply path's arm handling. |
| 2026-09-28 | 0.1.9q | The NSMB two-player route now parks *before gameplay* too, and the screens say why: one console's own scan finds the peer and offers to play, the other never finds it | Asked to drive the documented New Super Mario Bros. two-player route and answer at the screen level whether a playable round happens - are both characters in the same game, does each console's input move its own character while the other shows it, does the round progress - and to leave the clock, the tap map, the FCS fix and the arm handling alone with smoke at 66/0. **The answer is no, plainly, with the screens named.** Three runs, two of them byte-identical to each other: the route reaches the character select (`--touch 2000:127:119@1 --touch 2000:127:160@2`), both consoles pick a character, and each then sits in the *game's own discovery state* - `Searching for Luigi...` (player 2, top screen, a four-phase animation that freezes mid-spin when the other side answers) and `Searching for Mario...` (player 1) which becomes `Please Wait.` - forever, while the link below them carries 4452 command rounds with **0 empty, 0 canned, 0 clientfail, 0 deauth**, at 6.6x realtime. Player 1 also shows the dialog this project has seen before, `melonDS Mario has appeared. Play a game?`, reads its Yes at `(128,71)` at frame 4550 and moves to `Please Wait.` - and player 2 never gets the matching dialog, so the pair never starts. `md_game_session`'s frozen-picture detector is what makes this legible (both consoles `FROZEN PICTURE`, last change 4465/4450 frames before the end); the digest's own one-state-apart shape is the same one MKDS shows, reached from NSMB's menus: the guest still answers the lobby marker `00 80 5A 5A B8 B6` while the host's command state sits at `0F 00`. **An input test at the park, as asked:** `--hold 7000:RIGHT@1 --hold 7000:LEFT@2` for 2000 frames moves *nothing* on either console - the games are inside a link wait, not an idle menu, so "does each console's input move its own character" cannot be answered as a positive because there is no character on screen to move. **What the run corrected about the route itself, which is now stale in two places.** (a) On this build and save the two character cards on the *bottom* screen sit at y ~ 56..80 and y ~ 100..134, so the route's `127,119` lands on the lower card and its `127,160` lands **26 px below the lower card, on nothing** - and the character that console 2 ends up with is the one its *dialog* tap at `(128,71)` selects, i.e. the documented script reaches two characters by accident. (b) The board is not one screen but a sequence: `Select a character.` (top) over the two cards (bottom) -> each console's own `Searching for <peer's character>...` -> the finder's `melonDS <peer> has appeared. Play a game?` with Yes at `(128,71)`, No at `(128,120)` -> `Please Wait.`. A one-shot tap tuned to a frame works only for the boot timing that measured it, which is why the route's own note says so. **Two defects found by driving the real surface, both fixed.** (1) `tools/appctl.py render` - a pure PNG-to-ASCII subcommand, and the only way to read an 8px DS font - refused to run without the app, i.e. exactly when a finished run's `--shot-dir` PNGs are what you have: `cannot reach the control channel on 127.0.0.1:7373 (Connection refused)`. `render` is now in an `OFFLINE` set that `main()` skips the channel for, a missing PNG exits with `no such PNG: <path>` instead of a PIL traceback, and `--crop` takes `nargs='+'` with commas tolerated (`--crop 60,88,160,20` was a traceback, and with `nargs=4` argparse silently ate the *following options* as crop values when a token went missing). (2) The harness's link block printed a host's `bytes_received` as if it were a total - `received=4454 (136 bytes)` next to a peer's `received=9457 (506338 bytes)`, because replies arrive through `LocalMP::RecvReplies`, which counts the packet and cannot size the payload (documented on the `MDLinkStats` field, but not where a reader of a session report sees it). It now says `+ replies, counted as packets only` on a console that sends commands. **The app surface itself, driven the way a first user would** (`MD_AUTOSTART_PLAYERS=2 MD_AUTOSTART_ROM=... tools/run_app.sh`, then `appctl.py`): autostart with two consoles plus a ROM, per-player button input with both consoles lockstepped (`p1@1721, p2@1721`), `see`/`shot` reading the emulated screens, Stop -> Start returning a clean session, and every careless command answered with a sentence rather than a traceback (`press zzz`, `see --player 9`, `index 9`, a missing `--crop` value). Two more small defects came out of it and are fixed. (3) `appctl.py ui` reported each console's title as **`Player 148 fps`**: the card's `<h2>` holds the name and the `.fps` span as siblings, so `textContent` glues them together where the flex layout shows a gap - read as a UI bug for a minute, and a diagnostic is not allowed to invent one. The dump now joins the child nodes with a space (`Player 1 48 fps`), verified live after rebuilding the app. (4) `appctl.py touch 300 190` reported success for a coordinate 44 px off the digitizer, which is a *silent* miss in a scripted route: it now appends `WARNING: x,y is outside the 256x192 touch screen`. **Left as a candidate rather than changed:** the toolbar's only user-facing link health button reports `link selftest FAILED (1 -> 2 no reply, 2 -> 1 no reply)` on a healthy app whose games have not entered multiplayer yet - the detail is honest (the consoles are not polling their radio) but the headline reads as a broken app; it wants a state-aware sentence, and that is a copy decision with no measurement behind it yet. Validation: smoke **66/0**, the core and harness rebuilt, the app crate rebuilt and relaunched (`cargo build --release`, 1m27s) with the new title dump read back live, three route runs (two identical to the byte: 4454/9457 frames, 4452 rounds, same screen hashes), the decisive frames kept as `game-captures/nsmb-park-*.bgra`. Left alone as instructed: the shared clock, the verified tap map, the FCS fix and the reply path's arm handling. No race. Nothing committed. |
| 2026-09-28 | 0.1.9p | Every frame either game transmits reaches the peer's console, audited frame by frame on both park shapes; the only frames that do not reach the peer's *game* are the reply-handoff misses, and each of those crossed the link | Asked to instrument every transmission a console makes per round (all four TX slots, their arm and clear writes, the frames actually sent), follow each frame to the peer's game, give particular attention to a direction the fork's MP path may never carry - a client's own command frame, a client broadcast, or a slot other than the reply slot it does carry - and run the audit on both park shapes, fixing whatever goes out and is never relayed. **The result is closed and stated plainly: nothing the games write is dropped in the relay.** Live run A (park host `FF 00` / guest `00 00`, held from +134s to the end of the session, 11359 rounds): the host's command slot wrote 11439 frames, 11439 reached the wire, **11439 were received by the guest's transport and 11439 were offered to the guest's game** - the same count four times over; its acks 11439/11439 with none missing; its beacons 1003 sent, 807 received (every one of the 807 after the guest joined), 196 not received - **all 196 while the guest's console was still associating**, and the guest's game was handed 820 of them (799 through the MP path, 21 through the local AP path the joiner's own radio uses before it is in the group); the guest's replies 11438 plus one firmware stand-in, 11424 handed to the host's game; slot lines paired with a transport line 12444 of 12444. Run B (same shape, park held +89s to +364s, 16906 rounds): commands 16906 written / 16906 on the wire / 16905 received / 16905 offered, its one missing frame being one sent *after the guest's game had already deauthenticated*; replies 16904 + 1 stand-in, 16897 handed; beacons 1374 sent / 1306 received, the 68 missing all during association; slot lines paired 16907 of 16907. The archived `02`-stall shape (`md_app.log-20260928-150325`, host `02 00`, guest deauths at +96.2s, 7699 rounds): commands 7699 sent, 7693 received and 7693 offered - **the 6 that were never offered are the 6 the host transmitted after the guest's own `reason 0003` deauth (host commands at lines 240954-240981, the guest's deauth at 240944)**, and its replies were 7692 + 1 stand-in with 7631 handed. So: **0 frames lost in either direction while both games were in multiplayer, in all three runs**, and the only frames that reached no game are accounted for by name - frames sent after the peer's game left the group, and association-phase beacons for a game that was not in the group yet. **The directions the path has never carried are directions nothing ever sends into.** Across every live and archived run on record the guest's console never writes the command slot (`[mp] txslot CMD` from p2: 0 events) and never arms a beacon (0), and neither console ever uses LOC1 or a host reply slot - so the client's own command frame, the client broadcast and the host's reply slot are never *attempted*, and the one client-to-host direction the games do use (the reply slot) is now measured at 16907 of 16907. **The one genuine shortfall, quantified and mechanism named:** the reply handoff, i.e. answers that crossed the link and were then never handed to the host's game - 14 of 11438 in run A, 8 of 16905 in run B, 61 of 7693 in the stall shape - and each equals that run's `empty` reply windows minus the one still queued: the answer arrived after the window for its round had closed, and `MD_REPLY_KEEP=1` discarded 13/7/61 of them unread at the next command's re-anchor (one was still in the queue at teardown). Every one of those rounds put a client-fail bit in the host's ack (`clientfail 14`, `8`, `62`, matching `empty` to the round). This is the pipeline the fork documents, not a relay loss: the answer does cross, and keeping it for a later round would hand the host's game an answer belonging to a round it has already closed - the regression v0.1.9g measured when it compared `MD_REPLY_KEEP=0/1/2`. **Where the two shapes differ, as the next lead rather than a finding:** in both stable runs every handoff miss is in the first tenth of the park (14 and 7, then `empty` frozen at 14 and 8 for the remaining 6000+ and 10000+ rounds), while the stall shape's 61 are spread with a ramp toward the guest's departure (by tenth of the park: 3, 3, 5, 0, 1, 5, 4, 7, 33, 0) - one run, so the audit's per-tenth histogram needs to be read on a live `02`-stall before anything is concluded from it. **Two instrument defects, both found by the audit producing nonsense rather than by inspection, and both fixed here.** (1) The slot ledger printed the *game's* frame control word, read out of RAM before `TXSendFrame` applies the no-seqno bit to the buffer it sends, so 3439 of one run's 8536 client replies were on the ledger as `fc=0118` while the wire carried `fc=0918` - and an audit that joins the two halves of one frame by identity then cannot put them together (pairing 5099 of 8538 before, 16907 of 16907 after). The line now prints the value being sent and names the mask when it differs from the game's own. (2) `SendMPReply`'s latch line printed the *previous* `W_TXSlotReply2` under `REPLY2 <-` (the `W_RXCnt` bit-7 latch beside it printed the value actually latched), so the ledger showed the emulator's own latching as a game arming a read-only slot; both sites print the value latched now, and the audit counts a game's slot writes apart from the emulator's latches and from a game write to a read-only slot. `tools/relay_audit.py` was rebuilt around the join (slot line to transport line by fc+seq, transport line to the peer's receive by fc+seq+the sender's own US stamp, replies credited through the reply window's own `collected` line, and every residual classified as while-associating / while-both-in-multiplayer / after-the-peer-left with the marker that says so). Validation: smoke **66/0** before and after each C++ change; the core rebuilt and the app relinked (the new string confirmed in the binary); two full route runs, both reaching the park and holding it for 230s and 275s, and the archived `02`-stall shape re-read with the same tool. Left alone as instructed: the shared clock, the verified tap map, the FCS fix, and the reply path's arm handling. No race. Nothing committed. |
| 2026-09-28 | 0.1.9o | The guest-side matrix: no host state value moves the guest either, and an *unprobed* park is stable for minutes, so the departure belongs to the runs that stall at the host's own `02` step | Asked to run the masked state experiment in the other direction - vary only the host's state halfword in what the *guest's* game is handed, with a null control, repeated cells and both park shapes - and to settle whether the guest's exit is a value it never receives or a timeout of its own. **The guest-side matrix, complete, on a live park.** `tools/probe_matrix.py --side guest` arms on player 2 with `mask 00C0`, i.e. the host's state at payload offset 6 of the ten-byte command the guest's game is handed, and watches the *guest's own* answer body. Gate at app `+74.80s`: host cmd `BE 01 02 00 00 80 FF 00 B8 B6`, guest answer `00 00`, guest `arms 1451`, deauth `0/0` on both consoles. Thirteen cells ran - `null` (the peer's own `FF 00`), `echo`, `dead` (mask `0000`: armed, provably firing zero times) and the ten named values `00 00`, `01 00`, `02 00`, `03 00`, `04 00`, `06 00`, `FF 00`, `5A 5A`, `11 01`, `FD 01` - each for 4 s, with the delivery count read back from the cumulative `applied` counter: 242 on the null, then 485/726/969/1212/1454/1696/1939/2182/2425/2667 across the ten named cells, i.e. ~242 deliveries per cell and **2662 injected deliveries** on a park that stayed alive. **The guest's own body did not take a single other value in any cell** (`own states ['0000']` in all thirteen; a park where the guest answers `01 FD` alternates `FD01`/`0F01` instead, which is the game's own fresh-arm/held-arm pair, not the cell). So the value hypothesis is now closed on *both* sides: v0.1.9n closed the peer's state halfword as the input the parked **host** branches on (24 cells, two shapes, both orders) and this closes the host's state halfword for the parked **guest**. **The guest's own ladder, per arm, over the whole run:** `5A 5A` (lobby, 941 arms) -> `01 0E` (66) -> `01 11` (54+47) -> `04 00` (26) -> `00 00`, and then `00 00` for **11,000 consecutive arms**. It is not a ladder that stalls one step short of something: it is a value the guest reaches within seconds of the lobby and holds for the rest of the session. **The host's own walk, per command frame, is where the two fates differ.** The game's own six bytes go `06 00 -> 00 FF -> 00 FE -> 00 00/00 01 -> 01 00/01 01 -> 02 00`, and from there: in run D (**and** in the unprobed dry run) it continues `05 00 -> 03 00 -> FF 00` and holds; in run C it **never reaches `FF 00`** - the walk stops at `02 00`. The runs that stalled at `02 00` are exactly the runs that lost the guest (run C: guest deauth at app `+96.266s`, ~22 s after the gate; run B: ~14 s after the join; both `[mp] deauth sent: <guest MAC> -> <host MAC>, 30 byte frame, reason 0003`, the *guest's* game leaving first, the host's broadcast deauth following 0.2 s later), and the runs that reached `FF 00` never lost the guest at all. **The correction the control run forces, stated against v0.1.9n's framing:** with *no cell armed at all* (`--dry`), the park is **stable** - gate at app `+75.07s` on the same shape (`FF 00` / `00 00`), and 120 s later both consoles are still in MP (`cmds rx 8182`, +299 per 5 s window, `roles host/guest`, `deauth 0/0`), and the app was still running it 230 s in with `cmd seq 3DC0` and the guest answering `00 80 00 00` every round. So "the guest's game leaves first and that exit is what ends every run" is a property of the runs that stall at the host's `02` step, not of the link; an unprobed park is a *stable* deadlock of the two games' own state machines, and it is not a timer of either game's. **The guest's exit, in the terms the request asked for.** The last frame it was handed is the host's own command at the moment it stopped (`cmd seq 23B0 len 10 body BE 01 02 00 00 80 02 00 B8 B6` in run B, `seq 1920`/`BE 01 02 00 00 00 02 00 B8 B6` in the v0.1.9n run) and its own counters at that instant are in the digest's last line before the deauth (run C: `cmds rx 7693`, `answered 7692`, `arms 7692`, `aids 8456`, both reply slots in use); the host keeps transmitting its one unchanging command into the void for the rest of the session (`seq 1940 ... 19C0` after the guest's radio is off) and then deauthenticates the room. **Whether our delivery changes when it goes is not established, and saying so is the result:** the `FF 00`-shape park survives all thirteen cells and 2662 injected deliveries (run D) *and* survives unprobed (the dry run), so for that shape the probe path is exonerated; the `02 00`-shape runs left the session during a cell that wrote **bytes identical to the peer's own** (run B's `null` cell: `inject [00 00 00 00 00 00 02 00] mask 00C0 over the peer's [BE 01 02 00 00 00 02 00]`, 1074 deliveries), which cannot be a value effect - but no *unprobed* run has ever been seen to stall at `02 00` and then sit there, so "the stall at 02 is the guest's cue to leave" and "something about probing a `02 00` park ends it" are both consistent with the four runs measured. The experiment that separates them is named in to-do.md: reach a `02 00` park with nothing armed and watch for 60 s. **Four instrument and plumbing defects, each caught by producing nonsense rather than by inspection.** (1) `md_link_mp_vary` in `Bridge.cpp` validated `kind` as 0..3 while the Rust control channel had just learned `echo` = 4, so the guest matrix stopped after its first cell on `md_link_mp_vary failed (1)`; the bound is 0..4 now and the two spelling lists are kept in step. (2) The echo control **could never fire**: `n = min(MPProbeBodyLen, bodylen)` with `MPProbeBodyLen == 0` gives `n = 0`, `reach == 0`, and the hook returns before writing anything - an *armed* control that touches nothing, which is exactly what its `applied` reading showed (242, unchanged across the cell). The echo kind now reaches the frame's whole body when it has none of its own. **That one fix is verified by construction, compilation and smoke (66/0) but not by a live arm** - run D's echo row is the *broken* build's, so the next pass should re-run a matrix and see the echo row's `applied` move while the park behaves exactly as it does with nothing armed. (3) The prober's digest regex demanded the trailing `s` that `mp_secs` prints only when there *is* a timestamp, so every **host** line (`answer len 0 body - at -`, which is what a host prints for the whole of a park) failed to match: 45 s of `[wait] host None` against a park that was right there in the file, because the guest-side experiment reads the host's state from that line. (4) The departure watch started counting log events from its own start, so the run whose guest deauthenticated *during* the matrix reported "0 marks" for a session that had visibly deauthenticated; it counts from the gate now, and the gate's counters are snapshotted at the gate instead of being read at the end of the run (which had quoted the departure's own deauth as the baseline). Also added: `tools/guest_exit.py`, which reads a run (or the gzipped archives) and prints the guest's and host's state walks against the app's own uptime clock, with the deauth and radio-off markers - the tool that produced the `02 00`-stall result above from the archives. Validation: smoke **66/0** after every C++ change; the release app rebuilt; four route runs (one dry control, run B and run C aborted by the guest leaving, run D a complete thirteen-cell matrix) plus the v0.1.9n archives re-read. Left alone as instructed: the shared clock, the verified tap map, the FCS fix and the reply path's arm handling. No race. Nothing committed. |
| 2026-09-26 | 0.1.8w | After the fix: the post-lobby phase is stable, and the two ways a join still ends | The same evening's follow-up to v0.1.8v, answering what is left once the guest stops dying. **The phase the fix exposed is healthy**: `--cut-off --vs` then 40s of touching nothing gives a steady ~300 answers per digest window (911→3308), the host collecting every one (908→3304), window reads climbing (90761→338669), guest payload `00 80` with both bodies' counters walking forward, and **zero deauths** - the lobby-close → game-select transition is stable for as long as nobody presses anything. **Ending one**: any *host* press on the post-VS screen (A twice, START once) makes the host's game send deauth `reason 0001` to the client then `0003` broadcast, both radios off, both games spinning; the *guest's* presses are safe (A on P2 alone: 0 deauths, session alive 18s+, screens still animating) - so it is host-side "advance = cancel" on whatever screen MKDS shows there (black panel, one short bright message). **Ending two**: one join in four now dies before CUT OFF with the same shape - guest joins, gets the 34-byte `melonDS` announce, answers `00 80 5A 5A B8 B6`, and the host's game deauths it after 13 answers, i.e. the host stops being answered often enough and gives up on the client. **Why**: with the early reply off the guest's game arms every round (`arms 6776` vs `cmds rx 6793`) but only a third of rounds get an answer out (`answered 2510, blank 4283`) because the stock window opens before the arm lands - the exact problem the early reply was built for, and the early reply is what starved the game. So the shape of the real fix is an early reply that leaves the game in the state stock leaves it in (arm consumed, TX status, completion IRQ, slot pointer, TXBusy), with the digest's `arms` counter as the instrument (`arms 1` = starved, `arms` tracking `cmds rx` = participating); `MD_EARLY_REPLY_ACK` modes 1/2 were the first attempt and are not sufficient. Tooling: `tools/glyphs.py` (segment a text line into one block per glyph, dark or bright ink) reads the host's message that way. Smoke 66/0 |
| 2026-09-26 | 0.1.8v | The early answer was starving the guest's game | Sixteenth live attempt, and the cause of every MKDS guest death since v0.1.8k: **`MD_EARLY_REPLY` now defaults off (stock melonDS)**. v0.1.8u's "the guest survives with the tap route" was a misread - the digest's `mp p2 [guest]` role is only cleared by a deauth the console *receives*, so it reads `guest` after the guest's own game has left, and the join driver's role line is built from it. Grepping the *events* shows the tap route dying in every run too: the guest deauthenticates 4-5s after its radio comes up, in the user's two button runs and in four of ours. The death's shape is identical every time - the guest's game sends `deauth reason 0003` **19ms after** `CheckRX` hands it the host's periodic 120-byte broadcast - so the delivery log grew a hex+text dump of the first 48 bytes of every frame >= 48 bytes, which identified that broadcast as a genuine 802.11 **beacon** (rates `82 84`, DS channel 7, TIM, vendor IE 221 with Nintendo's OUI) and the `melonDS` nickname as arriving separately in 34-byte commands; nothing the guest is handed is malformed. The discriminator is `arms`: with the early answer on, the guest's game arms **once** for the whole lobby (answer body frozen at `00 00 5A 5A B8 B6`, `window reads` 6345) and then leaves; with it off, the game re-arms every round (`arms 9792`, `aids 6757`, `window reads 365859`), its answer moves to `00 80` (lobby-ready), the host collects 3375 with 14 empty windows, there are **0 deauths**, and `CUT OFF` + `VS` complete with both consoles still linked 30s later. `SendMPEarlyReply` answers on the guest's behalf inside the round, so the guest's own driver never gets the event its lobby state machine waits for - NSMB tolerates that (12000 frames, 3.29x), which is why thirteen runs of review missed it. Fix: default off, `MD_EARLY_REPLY=1` re-enables. Verified on the default build with no env knobs (`tools/mkds_join.py --restart --no-dialogs --cut-off --vs --watch 25`: deauths 0, guest `arms 6140`, host `collected 2150`, 1 kick). New frontier: pressing A on the game-select screen still ends the session - the **host's** game sends `reason 0001` then `0003` broadcast and both games spin, with 1-3 stall kicks (10s windows, `KICK_AFTER_WINDOWS = 2`) correlating. Tools added: `appctl.py render --mono/--crop/--zoom` (threshold renderer, one char per pixel - the only way to read an 8px DS font), `tools/scripts/mkds-user-route.txt`, and the note that any hands-free join must now pass `--no-dialogs` (the save has consumed its first-time dialogs, so the default 3 overshoots and makes P2 its own host). Smoke 66/0 |
| 2026-09-26 | 0.1.8u | The lobby-close death reads itself out, and the app becomes drivable | Fifteenth live attempt, on the flight-recorder build, and the first failure whose cause is in its own log. MKDS 2-console: P2 joins, the lobby runs 4s at reference quality (664 commands received, 658 answered, 0 stale, 0 empty windows, nothing retried), P1 presses A to close the lobby - then **the guest's game writes AID=0 itself** (`guest set its AID to 0 (round state 0, cmd seq 5650)`), sends deauth reason 0003 to the host, and both radios go off and stay off while both games park on a spinning program counter (the emulator itself healthy). The transport is not implicated: the guest's game leaves and the host's game tears down its access point in response, the same signature as the v0.1.8q 4.29s death - so the v0.1.8t "packet transmission failure" hypothesis is dead for this run too. Then the answer to *"is there a library you are able to add to test-drive the tauri app yourself"*: **yes, and without a new dependency** - Wayland rules out the usual X11 synthetic input, so the channel drives the webview directly through Tauri's own eval/invoke. `MD_CONTROL_PORT` (loopback only, off by default) plus `tools/appctl.py`: press buttons through the frontend's real key handlers, inject raw DS button masks and pen coordinates, click UI by CSS selector, read session/frame/link state from Rust even when the UI is wedged, save composite PNGs of every console's screens to `logs/shots/`, capture the whole desktop with spectacle, and - the part that matters for an agent - **read the emulated screens back as ASCII text**, including a one-character-per-pixel crop mode that resolves the DS's 8px font. Every command lands in the app log as `[control] ...`. Verified live on MKDS: `press start` produced the app's own `[input] player 1 buttons -> START`, the screens changed, a 52 KB screenshot was written. Smoke 66/0 |
| 2026-09-26 | 0.1.8t | The run a reboot ate, and the flight recorder built so it cannot matter again | Fourteenth live attempt and the first one that cannot be read back: the user reports a **new, earlier** failure ("p2 errored out before p1 could close the lobby") and that run's log was `/tmp/md_app.log`, which the reboot took with it (as it took `.prev`, four gzipped older runs, and `/tmp/md_rt/`). Fix, in the same spirit as `run_app.sh`'s original reason for existing: logs now default to `<repo>/logs/md_app.log` with `.prev` and a gzipped ten-run `archive/`, gitignored. **Premise checked rather than assumed: MKDS local wireless works on stock melonDS 0.9.5+** (GBAtemp's 0.9.5 write-up names MKDS as the *example of the bug that release fixed*; a 0.9.5 hands-on lists MKDS and its romhacks as playable and says it was tested with four instances) - so the failure is ours, and the one architectural difference that matters is that upstream runs one OS process per console paced by the OS while we run a frame-level lockstep barrier, which leaves consoles at arbitrary sub-frame phase (the 1.5-25ms US-clock lead v0.1.8j measured, which the deferred-delivery path then clamps away). **Shipped: the multiplayer flight recorder** - `Wifi::MPDiag` + `LocalMP::MPReplyDiag` counters (commands rx/tx with dup detection, answers sent/early/blank/canned, arms, AID changes, wireless-RAM-window reads, deauths both ways, acks with clientfail, and the four distinct ways a reply is lost: re-anchor-dropped, stale, blank-credit, not-looked-for) exported through `md_link_mp_digest` and printed by the host health thread as **two lines per console per window**, with deltas, plus the last command and answer bodies with their timestamps - printed again immediately whenever a console's MP state changes (radio, deauth, role), so a session that dies at second three leaves its cause in the last four lines. Every question the last thirteen manual read-backs answered by grep is now in those lines. Smoke 66/0 |
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
| 2026-09-27 | 0.1.8x | The MP clock is emulated time | Chased "the host freezes its clock in the reply wait" to the ground with a probe in `Wifi::USTimer` and one in the bridge's frame runner, and the premise was wrong: both consoles' wifi clocks advance exactly 2083.33 ticks per 60 frames in every trailing window, on both the Tauri host and the harness. What actually parted was *emulated cycles per frame*: the guest advanced up to 8,511,480 ARM7 cycles (0.63%, 15.2 DS frames = 254ms) more than its host over 12,000 frames, all of it during the join, and never came back. Attribution: `GPU::SetVCount` - MKDS writes VCOUNT = the current value every 61 frames (host) and far more often on the client while joining, and a VCOUNT write delays the line counter, so that console's video frame runs longer than 263 lines (270 measured, 575,100 cycles) and a frame is exactly what one lockstep step advances. The clocks inherited it because `USTimestamp` advanced 8us per wifi tick and the radio's power state, not with the frame grid, and the only alignment the two consoles ever got was one snap at association. Fix: `NDS::GetSharedUSClock()` - whole DS frames (identical on every console, since the frame count is advanced in lockstep) plus the console's own progress through the frame - and `Wifi::USTimer` now derives `USTimestamp` from it, so every cross-console comparison (frame timestamps, `NextSync`/`RXTimestamp`, the reply staleness window, the reply budget) is in emulated time. The association snap is gone with it. `kReplyStaleWindow` is two frames rather than one, measured: a reply that answers the round the host is collecting is one to two frames old now that ages are real elapsed time, and stale-dropped stays at 0-5 in a full session at that setting. Measured after: clock offset between the consoles stays inside one frame (min -16,576us / max +864us over 26,898 commands; it used to reach -275,520us and grow), the guest's answer rate is flat for eight minutes (37-55% per 5s window, no decay) instead of collapsing with the drift, and MKDS held `mask 0x3` with **zero** deauths through the lobby -> game-select transition for 271s/14,077 frames. NSMB's harness digest went from 1525 collected / 2502 commands to **9196 collected / 9197 commands, 1 empty window, 0 stale, 1 clientfail**. Reruns are not uniformly clean: one 60s run lost the guest to a host deauth (`reason 0001`) after the guest's game had gone to 22 answers / 710 blanks in the preceding 732 rounds - a game-level timeout on a client that stopped participating, which is the next gap, not a clock. |
| 2026-09-27 | 0.1.8y | The answer the poll waits for | The guest's ~45% answer rate was rebuilt rather than compensated, and the rebuild's first design was falsified by the run that tested it. Read the 2026-09-27 baseline back first (`logs/archive/md_app.log-20260927-223130.gz`): the guest's game arms a fresh answer *inside the handler that received the previous poll* and then **clears the slot again in the same round** (`guest arms an answer: slot 8000` / `slot 0000` on every round, 5027 arms for 5028 polls), so by the time stock's reply path decides, the live register reads 0 and the arm is invisible - the rounds that were answered were the ones where the arm happened to survive into the next poll. First rebuild took the arm the moment it landed (from inside the game's own write): 99% answered, but the game **stopped arming after 16 rounds** while going on receiving every poll (16 arms / 738 blanks) - the same starvation the 2026-09-25 early-reply drafts hit. Removing that one call brought the driver back (arms 5027 for 5028 polls) but left 59% blanks, which pinned the cause: consumption must stay at the poll - the game's driver expects the slot to be consumed there. Final shape: the arm is **held** (`MPArmSlot`/`MPArmHeld`, taken at the poll it answers, replaced by the game's next arm), the AID that decides whether a poll is addressed to us is the remembered association (`MPMYAID`, not the live `W_AIDLow` that the driver clears inside the same handler), and an addressed poll always goes through stock's `SendMPReply`, which now only reaches its firmware stand-in (`fc=0158`) for a poll the game armed nothing for. Measured, 2026-09-27 MKDS through the real app (`mkds_join.py --restart --no-dialogs --cut-off --vs`): **answered 4726 of 4729 polls, blanks 0, canned 3, arms 4725, host collected 4725, empty 5, clientfail 5 of 4731 (was 1932 of 5956)**, and 0 deauths for the first 59s with both consoles reaching the post-VS screen with a full picture (47820 / 47848 ink pixels). The reply window question answered itself once the reply was in-round: reply ages over 4725 collected replies are p50 1573us / p99 11016us / max 11497us - inside a single frame, stale-dropped 0 - so `kReplyStaleWindow` was tightened to one frame; the harness says **not yet**: one frame collapsed NSMB twice in a row (answers 1498 and 275, collected 0, stale 0) where two frames reproduces 9197 commands / **9196 collected / 1 empty / 1 clientfail / 2.47x realtime** (four runs, two per arm, no overlap), so it stays at two, and what a one-frame bound actually breaks (`replies sent=0` with the guest counting 1498 answers - nothing reads, rather than something being dropped) is the next thing to name. Remaining blocker is no longer the reply path: the session still ends in MKDS's own **"Communication error. Press the A Button."** (read out of the framebuffer, both consoles identical, the dialog that the parked screens have shown all along) 7s after the VS tap, and the log names the trigger - the host's game broadcasts a 120-byte `fc=0080` beacon/roster frame (body `58 00 A0 01 ...` dumped) and the guest's game deauthenticates 3.4ms later (`reason 0003`, then the host's own 0003 broadcast, both radios off at +59.232s / +59.415s). Smoke 66/0 |
| 2026-09-28 | 0.1.9n | The adjacent-state matrix, run clean and twice: no peer state value moves the parked host, on either park shape | Asked to run the matrix v0.1.9m could not - keep the peer's own `00 80` prefix and its `B8 B6` suffix, vary only the state halfword, arm strictly after the answer has left the lobby's `5A 5A`, include a null control injected with the peer's own bytes, repeat every cell - and then to answer why the guest's game never reports the state the host waits for, fixing it if it traces to us. **First the frame layout had to be pinned from the code, because the previous pass's read was off by one field.** `Wifi::TXSendFrame`'s `case 1` writes the emulator's client mask with `*(u16*)&TXBuffer[12 + 24 + 2] = MPClientMask` and `FinishRX` reads it back at the same `RXBuffer[12+26]`, and the sync arithmetic takes `clienttime` at `RXBuffer[12+24]`; so a command payload is `[clienttime:2][clientmask:2][flags:2][state:2] B8 B6 [FCS:4]`, a reply payload is `[flags:2][state:2] B8 B6 [FCS:4]`, and the game's own state halfword is at payload offset 4 of a command but offset 0 of a reply. That also explains the shapes that looked contradictory: a nickname frame is `[clienttime][clientmask]` followed by UTF-16 text (`0C 9F 06 00 6D 00 65 00 ...`, mask `0006`), and the `reply window` line's ten-byte body is a reply's six game bytes plus its four-byte FCS. **The request's phrase "keep the peer's own `00 80` prefix" cannot be taken literally, and measuring is what showed it:** the first halfword alternates `00 80`/`00 00` about once a second on both consoles simultaneously (logged on both sides, in every park shape), so freezing it at either phase is a perturbation of a field the experiment is not about - and a run that froze it *did* perturb: it stalled the join so that the host never left `06 00` for 77 s (`clientfail 40`, then a `reason 0003` deauth), which is the same confound v0.1.9m's injection had. The probe therefore gained a **byte mask**: bit `i` set replaces payload byte `i`, everything else is delivered exactly as the peer sent it this round. **That also retires v0.1.9m's one positive result as a confound, stated plainly:** its injection (`00 80 02 00`) armed while the answer still carried the lobby's `5A 5A` readiness marker and changed four body bytes at once, so the walk it moved (the host never passing through `01`) is explained by the readiness bytes being overwritten - which is exactly what the frozen-flags run above reproduces - and not by the state value it wrote. With the state halfword varied alone, nothing moves. A cell is now `--mask 000C` with a four-byte body, i.e. only the state halfword. The cell log confirms it per line, e.g. `inject [00 80 01 05] mask 000C over the peer's [00 80 01 11]` - the peer's own flags and marker preserved, the state replaced. **The matrix, complete, on two park shapes and in both orders.** Run A (park host `FF 00`, guest `00 00`): the gate fired at the first moment the host's state held a non-lobby value for 2 s, and twelve cells - the peer's own value, the host's own value, and `00 00`, `01 00`, `02 00`, `03 00`, `04 00`, `06 00`, `FF 00`, `5A 5A`, `11 01`, `FD 01` - each applied on ~181 consecutive replies (three seconds at the measured delivery rate). Run B (park host `02 00`, guest `01 01` climbing to `01 FD`), the same twelve cells walked backwards, each applied on ~1130 replies. **The host's own state did not change once in any of the 24 cells** (one probe line per cell: the arming line, and nothing after it), including the cell where the peer reports the host's own parked value and the cell where it reports the peer's own value. The walk the injection was supposed to move is, from the gate's own baseline history: `06 00 -> 01 01 -> 02 00` within ~4 s of the lobby, then `FF 00` in run A and a stop at `02 00` in run B - so the host passes 01 and 02 *by itself*, and what it then holds does not depend on the peer's state field. **So the request's first outcome does not exist, and saying so is the result:** there is no value or range at which the host's walk continues past 01 into the race setup, because the walk continues past 01 unaided and stops at a value no peer state moves. The reply body's state halfword is excluded as the decision input, over `FF 00`, `00 00`, `01 00`, `02 00`, `03 00`... - and the exclusion is measured, not inferred: 181 and ~1130 injections per cell, with the applied counter read back from the log rather than from the probe's own counters. **The second question's premise is answered by the same data: there is no state the guest is short of.** The host's expectation is not a peer state value, so "the guest never reports it" is not the blocker. What *is* measured about the delivery, and excludes the transport for this field: at the park, the guest's own record of its answers and the host's record of the same answers agree on the state bytes - the guest's digest reads `00 80 00 00 B8 B6` and `00 00 00 00 B8 B6` while the host's `reply window` lines read `00 80 00 00 B8 B6` plus the four-byte FCS its own record omits - so 4096 collected replies were delivered with their state field neither rewritten nor shortened. And the guest's ladder is its own progression rather than anything stale we hand it: during the join the host collects `01 00`, `01 01`, `01 02` ... `01 11`, `01 FD`, with consecutive values alternating once each (a fresh answer and a held echo of the previous one), while the guest's own counters read `arms 4266` against `cmds rx 4267` - one arm per poll, so the alternation is the game's own double-buffering, not a dropped delivery. **Three instrument defects the runs found, each by producing nonsense rather than by inspection.** (1) The gate read the host's *nickname* frame body as a state (`0C 9F 06 00 6D 00 ...`) and armed the whole matrix inside the join: the host then held `06 00` for the rest of the run with `clientfail 40`, so all twelve cells measured a join that never completed. Bodies of any length other than the two game shapes (6 and 10 bytes) are no longer read as states, and the gate waits for the host's state to *hold* for 2 s rather than merely leave its lobby value. (2) The abort tested the digest's **cumulative** deauth counter, so a healthy park - the run's earlier teardown still in the count - aborted after the null control; the counter is snapshotted at the gate and only an increase stops the matrix. (3) The cell log triggered on the whole body, so the `00 80`/`00 00` toggle produced ~500 lines per cell of pure alternation; it triggers on the state halfword now. Two smaller ones fixed at the same time, the first introduced and caught in this pass's own code: a probe body longer than 16 bytes overran the change detector's 16-byte copy and its 16-byte hex buffers (the mask is 16 bits, so the body is clamped to 16); and the harness's log tail now rewinds when the runner rotates the log, instead of seeking past the end of the new file for the whole session. Validation: smoke **66/0** with the masked probe in; the release app rebuilt; five route runs, of which two produced a complete twelve-cell matrix; the masked cell confirmed line by line in the log. Left alone as instructed: the shared clock, the tap map, the FCS fix and the reply path's arm handling. Nothing committed. |
| 2026-09-28 | 0.1.9m | Two threads wrote two halves of one log line, and the first probe that actually fired moves the parked pair (`02`/`01` -> `05`/`04`) | Asked to take the transition apart moment by moment from *both* sides - the instant each console's own state byte changes, the frame that changed it, what it reports and what it keeps re-sending - and to test the adjacent peer states as diagnostics, saying which console waits for which datum from the other. **The instrument was corrupt, and that had to be fixed first, because a both-sides account is exactly what reads the lines it corrupts.** `Platform::Log` wrote the tag and the body as two `fprintf` calls and `trace_frame` as four, and the two consoles run on their own threads sharing one `stderr`: in one 12-minute, 157k-line run that produced **1021 lines carrying two `[melonDS pN:Debug]` prefixes and 415 bodies with none** (1.2% of tagged lines, concentrated on the delivery lines), where a merged line attributes p2's body to p1 and leaves p1's body unattributed. Both now build the whole line and issue a single `fwrite`; a 581k-line run of the same route has **0** of either. **The transition, from both sides, in four runs.** Each console transmits its own `fc=0228` command frame per round and both carry the same 6-byte shape `00 80 <state> <counter> B8 B6` (the host's prefixed with `BE 01 02 00`, the guest's reply being the bare 6 bytes). The host's own state walks `06 00 -> 01 00 -> 02 00` and then never changes again while it keeps transmitting (measured twice), or `06 00 -> 01 00 -> 03 00` (the archive run), or `06 00 -> 05 00` (probed); the guest's answer walks `00 00 5A 5A` (lobby) -> its own countdown (`12 0E 01 00` ... `12 8E 01 11`, `02 8E 01 FD`, each fresh arm interleaved with the held echo) -> `00 80 04 00` / `00 00 04 00` / `01 FD`. The pair the two settle on is always one apart and *which* pair is not fixed: `02`/`01`, `03`/`04`, `05`/`04`, `FF 00`/`00 00` were each measured. **The old probe could not work, and that is now measured rather than suspected:** `probe_parked.py` (v0.1.9k) waits for "frozen picture + host still sending", and in 3 of 3 attempts on 2026-09-28 the host had already stopped transmitting before its first sample - in one park shape the host's own command counter is *zero* (the link is silent, so no reply is delivered and no probe can fire at all). The decision is made *at* the transition, so the probe now has to be armed before it: `tools/probe_transition.py` arms the reply-body probe on the guest's own answer leaving `5A 5A`, watches the host's own state walk, and compares it against a baseline run of the same route. **The first controlled injection that actually fired:** `applied` 5347 replies carried `00 80 02 00` (confirmed in the log line by line, not just by counters, which is what v0.1.9k could not do). With it the host's state walk was `06 00 -> 05 00` - it never passed through `01`, where both unprobed runs go - and the guest's own answer reached `00 00 04 00`, a value the unprobed runs' countdown never reached; the effect therefore propagates to the peer through the host's own command, and the pair that parks is a function of four bytes of the reply body. Confound, stated: the probe armed while the answer was still the lobby marker, so the `5A 5A` readiness bytes were overwritten too - this names the *input class*, not the value, and the run ended the same way (the guest's own `reason 0003` deauth) as every baseline. **Which datum, from the read side - and one claim withdrawn.** At the end state the host's game reads the guest's reply frame down to its whole 6-byte body (`00 80 01 FD B8 B6` are the last halfwords it takes). The guest reads its command frames *substantially* too: the snapshot counts are `consumed=28` reads per 42-byte reply frame on the host (3/3 samples), `29`-`33` per 44/46-byte command frame on the guest and `43` per 72-byte broadcast - so **neither console withholds consumption, and an earlier draft of this row's claim that the guest never reads the host's state halfword is withdrawn**: it came from the tail of the 512-read list, which cycles over successive frames' header/address/mask fields, and the per-frame counts contradict it. What the two read *orders* still differ in is where they stop, not whether they read. **Exclusion list, each measured:** the parked host does not move on its peer's own body bytes (v0.1.9k), on a 40-reply gap, on answers one round late, or on the guest reporting the host's own state value - which the archive's park delivered on alternate rounds (guest `03` beside a host parked at `03`) with no effect. Everything else was left alone: the shared clock, the tap map, the FCS fix, the arm handling. Validation: smoke **66/0** after the log fix, the release app rebuilt, four route runs, `tools/probe_transition.py` compiles and carries its own staleness fix (it reads only what the current run appended, after arming against a previous run's digest was observed). Nothing committed. |
| 2026-09-28 | 0.1.9l | Screen-level: the 2P route never becomes a race, and two readouts that could never tell the truth (fps, status errors) | Asked to prove or falsify, at the screen level, that the project's two-player MKDS round is genuinely playable - both characters present and moving, each console's own input moving that console's character and the other console showing it - and to capture the working exchange round by round as the reference the parked transition lacks; plus a playtest of the app's own surface with the defects it turns up fixed worst-first. **The verdict is a falsification, and the evidence is the screens.** The route (P2 joins P1's room -> CUT OFF -> VS) reaches the lobby with both consoles present (per-step hashes and ASCII reads in the run output, screenshots `logs/shots/pl-*.png`), the guest's game adopts the host's transition on the wire (guest answers `00 80 5A 5A B8 B6` in the lobby, then `00 80 04 00 B8 B6` from +91.20s, next to the host's command body `BE 01 02 00 00 80 06 00 B8 B6` -> `BE 01 02 00 00 80 03 00 B8 B6`), and then no race appears: after the VS tap both screens go to the *identical frozen black* (both framebuffers `5C2DB22F` top / `35860921` bottom, 99-100% black, means 0-1, `powcnt1 820F` so the LCDs are on), **both ARM9s stop running game code and sit in the BIOS (`pc9 FFFF0238` on both)**, the wireless block is idle with its interrupts off (`IF 0800 IE 0000`, every raised/acked delta 0), both radios are off (`WIFI: OFF` at +136.6s) and the guest reads `[not in MP]`. So there is no live parked handshake at the end of this route either - the session is torn down before a race exists, and the counters agree with the screens rather than the other way round. **The black is not a dead console, though, and that is new: input wakes both games.** Pressing A/START/d-pad/B on both consoles puts `pc9` back in game code (`0200FD10`), brings the pictures back (means 96-150, 0% black, a different crc at every sample, both consoles animating their own front-ends) - but with no link between them, because both radios are off. **The round-by-round reference this run provides** (host command body -> guest answer body, from the digest): `+68.72s` `... 80 06 00` -> `00 80 5A 5A` (lobby), `+91.20s` `... 80 03 00` -> `00 80 04 00` (the transition is adopted), `+116.24s` and `+132.38s` unchanged on both sides - the host re-sends `03` while the guest answers `04` for ~45s - then the radios go off at `+136.6s`. So this pass's parked shape is host `03` / guest `04`, one state apart, the same relation earlier passes measured at `05`/`04` and `FF`/`00`. **What could not be exercised, stated plainly:** the per-console character test (each input moving that console's character and the other console showing it) needs a race screen, and there is none, so what was exercised is the app's input path (Digit1/2 selecting the active console through the frontend keymap, and key events reaching the emulated games, whose own menus respond) plus the wake-up above. **Two app defects found by driving the product, both of the same family - a readout that can never tell the truth - now fixed.** (1) The status bar *latched* any error for the rest of the run: `pollStatus` only wrote the running line while `!statusError`, so one uncaught UI error pinned `ui error: SyntaxError ...` to the bar forever while the frame pump was measurably healthy (`copy.seq` climbing at 60 beats/s); an error is now shown for 6s and a condition that is still true re-asserts itself every poll (the console-stopped message does), so that one still stays up. (2) The per-console **fps readout never showed a number at all** - the first sample computed `dt` against itself (`c.fpsTimer ?? now`) and only the branch that *reports* an fps started the clock, so `dt` stayed 0 for the life of the page: observed on a session that had pumped 29545 frames, every `.fps` element read `—`. The clock now starts on the first sample; the bar reads `55 fps` per console (measured with the control channel also running). Both re-verified on the rebuilt app: error text present 1s after the event, gone 9s after; fps `["55 fps","55 fps"]`. **One cosmetic artifact left alone on purpose:** the `ui` command's `console_titles`/`fps` fields read the card's DOM text, so they concat the name and the fps span (`"Player 155 fps"`) - that is the test-drive reader's shape, not what the window draws. Validation: smoke **66/0**; one full route run to the post-VS state; the app driven through its control channel for the status/console-selection/keyboard/layout surfaces. Nothing committed. |
| 2026-09-28 | 0.1.9k | The probe could not fire, and the park is a one-minute window rather than a state | Asked to name what the host's game branches on by varying one input at a time on the parked handshake - the peer's state bytes, the round an answer arrives in, a gap like a radio drop - and to record which variation moves it. Built the driver for that (`tools/probe_parked.py`, on the `vary` control command) and **found the instrument itself broken before any game was measured.** A body probe armed with `value 0` ("until cleared") is stored in the same field the hooks test as "deliveries left", and every hook is gated on `MPProbeLeft != 0` - so a body probe set to 0 was accepted, logged as armed, and then never applied once. On a **live** park delivering ~60 replies/s, four body probes in one session read `applied 0` while `skip` in the same session read 40 and `hold` read 27, and the log held zero `[mp] probe: reply body ... replaced with` lines; the same probe re-issued with `--value 100000` then applied at 60/s (`applied` 27 -> 751 -> 1179). Fixed: a body probe's `value <= 0` is stored as `-1`, which is both "armed" and "do not run out". Two smaller ones fixed at the same time: the body hook counted a frame with no body behind its 802.11 header as applied (spending quota it never spent and logging a delivery it never touched), and `SetMPProbe` armed the new kind *before* copying its body, a window in which one delivery could carry the previous probe's bytes and still be counted against the new one (armed last now). **The window, measured.** The premise - a host re-sending one unchanging command body with the link running underneath - is real but short: in four route runs today the host's command counter stopped at 2499 / 4786 / 5257 / 6480, and in 3 of the 4 it had already stopped by the time a probe could be issued (the driver refused with "never saw a parked host that was still sending" rather than reporting unordered probes as "unchanged"). Every one of those runs ends the same way: the **guest's** game deauthenticates first (`[mp] deauth sent: 0009BF126643 -> 0009BF112233, 30 byte frame, reason 0003`, `[CLIENT] deauth`, wifi off at +92.9s in an arm-off run and +71.1s in an arm-on one) and the host follows with a broadcast `0003` and disarms its own beacon (`TXSLOTBEACON <- 0000`), after which both pictures are the same frozen black (screen hash `45d15630` on both, unchanged for 200s). That teardown is *not* an `MD_ARM_PERSIST` artifact - it happened with the knob on (2499 rounds, `reanchor-dropped 32`, `clientfail 33`, `canned 2`) and off (6480 rounds, `reanchor-dropped 35`, `clientfail` in the same range, `canned 461`) - so the previous passes' "the host had stopped transmitting" is this park expiring, not a probe artifact. **The parked body is not one value either:** measured `80 FF 00` (arm off - the mission's shape), `80 03 00`, `80 02 00` and `80 01 01` across runs, with the guest's copy of the same command reading `BE 01 02 00 00 00 01 01 B8 B6`; the constant parts are `BE 01` and the trailing `B8 B6`. **The one session where the probes did land** (arm off, body `BE 01 02 00 00 80 FF 00 B8 B6`, host polling ~60 cmd/s, `LINK ACTIVE`, picture frozen, probed through `LINK ACTIVE` for ~90s): `body 00 80 04 00` - the peer's own bytes, a null control - applied to ~720 consecutive deliveries with the host's body unchanged and the link `ACTIVE`; `skip 40` - the host kept polling straight through the 40-reply gap (`tx` +368 in that window) with its body unchanged; `hold 40` - 27 answers delivered one round late, same. So the exclusion list, each measured: **the parked host's decision does not move on its peer's own body bytes, on a 40-reply gap, or on answers arriving one round late, within ~10s of each.** One lead, stated as a lead and not a result: switching the body to `00 00 00 00` - the peer reporting *no* reply, bit 15 clear - was followed within 9s by `LINK OFF` with the host's last command at `+213.44s`, where the control step 12s earlier had left it `ACTIVE`; that is n=1, and two other parks in the same session ended on their own, so attribution is not established and it is the next experiment. Validation: smoke **66/0** with the probe code in and after both fixes; the release app rebuilt and driven through three route runs. Nothing committed. |
| 2026-09-28 | 0.1.9j | The RX header's `+4` halfword is documented as *not written by the hardware*, and the parked games consume every byte of each other's frames | Asked to find what the hardware truly puts in the RX descriptor's `+4` halfword - the one value both games read that our delivery never writes - from GBATEK, upstream melonDS and an independent implementation, write it, and then check per round whether each game consumes the other's body. **The field's answer is that the hardware writes nothing there, and that is now recorded where the decision is made.** GBATEK's "Hardware RX Header (12 bytes) (RXHDR)" table lists the descriptor field by field and says of `04h`: *"Unchanged (not updated by hardware, contains old Wifi RAM content)"* - so the values the v0.1.9i recorder saw (`17DC:F57C`, `0FCC:F526`, `0x0808`, `0x0C02`) are the hardware's semantics and not a stray. Every branch of this fork's history skips the halfword (`IncrementRXAddr(headeraddr, 4)` is byte-identical in upstream), and the pre-refactor code copied the received frame's words 0/2 and 6/8/10 while skipping 4, so no version has ever had a value for it. The only implementation that fills it - DeSmuME, with a `timeStamp` field - is the weakest of the three sources: it hardcodes MAX/MIN RSSI to FFh/F0h where the frames games read carry 80h/40h, and its own emulated receive path writes 0. Writing an invented value was therefore the one thing the record rules out; `Wifi.cpp` now carries the table, the citation, the three sources' treatments and the instruction at the write site. **Measured, so the argument need not be taken on trust:** both games read `+4` (host `77C2`, guest `635D` in one parked round) *and then* read the whole body behind it, halfword by halfword to the FCS, so the field does not gate consumption. **The per-round correlation, in bytes:** the host's game is handed the guest's REPLY frame (`flags 801Eh` = BSSID + `0Eh`, fc `0118h`, GBATEK's "MP reply (data)") and reads descriptor, 802.11 header and the body `80 00 00 00` + FCS `B6B8` - exactly the bytes the guest's game armed; the guest's game is handed the host's frames (`flags 8011h` beacon, `801Ch` CMD) and reads them the same way to the FCS (`01BE 0002 8000 00FF B6B8`, the host's command body `BE 01 02 00 00 80 FF 00`). Neither console withholds anything: `W_RXBufReadCursor` **equals** `W_RXBufWriteCursor` at every sample (`0DE0/0DE0` host, `0B96/0B96` guest). So there is no consumption defect to fix - the parked state is two state machines that have each read the other's whole state and neither changes it. **Route, re-run to the post-VS state:** identical parked black on both consoles (same `45d15630` screen hash), `canned 1` (the cold-start round), `clientfail 3`, guest `answered 2905` of 2905 with its own bytes, host command body unchanged at +99.91s, guest's own answer `00 00 00 00`. **No race, and they do not leave `FF 00` / `00 00`.** Two GBATEK findings kept: the reply latch (`REPLY1 -> REPLY2`, clear `REPLY1`) is software-triggered by a `W_RXCNT` bit-7 write, already implemented faithfully at `case W_RXCnt` beside stock's poll-time move; and the descriptor's `+08h` length is documented as *excluding* the FCS while `W_RXLenCrop` assumes it is included, which is now a to-do item (the FCS fix itself was left untouched). Validation: smoke **66/0**; one full route run (comment-only change, so the run's binary is behaviourally identical to the one the smoke test built). Nothing committed. |
| 2026-09-28 | 0.1.9i | What each game actually reads: the parked state is fed the peer's own bytes now and does not move, so the fabricated reply was never what parked it | Asked what each console's game reads out of the wireless block in the parked state - every I/O register and every RX halfword, in order, per round - so that a value our emulation fabricates can be told from one the peer's own game wrote, and to fix the fabrication. Built the read recorder on the block's own read path (`Wifi::Read` -> `ReadRaw`, so the value recorded is the value the game's bytecode saw): `Wifi::MPIo` keeps a per-halfword read count and last value for the register window, every read of the wireless-RAM window in order (last 512), and - the part that answers the question - the reads that fall **inside the frame the console was just handed**, bracketed at the moment the RX header is written (`SetMPIoZone`), each tagged `I`/`o` so a read of a region the delivery path never bracketed is visible at a glance. It rides the digest (FFI guard: `mp digest struct: 552 bytes in C and Rust alike`), the health thread prints it on demand, and `mkds_join.py` prints the consumption list per step. **The measurements.** (1) *The named suspect, in the games' terms:* the guest reads the frame handed to it (72 bytes, `fc=0080`) as a 12-byte header then its body, in order - and in 22% of rounds the host is answered with the firmware's `fc=0158` frame whose 4-byte body is `00 00 00 00`. Verified the alignment behind that: every `answering cmd seq=S ... armed in round seq R` pair in a run is `R = S - 32` (2297 of 2433; the round stride is 32), i.e. the guest's game arms its answer for the *next* poll while handling the current one, so the reply path is aligned and the 22% are rounds where the guest's game armed nothing for that poll at all (its own behaviour: 459 of ~3100 rounds). (2) *A fabrication nobody had named:* both games read header halfword `+4` (p1 `17DC:F57C`, p2 `0FCC:F526`), which the delivery never writes - upstream's `IncrementRXAddr(headeraddr, 4)` skips it, so the value is leftover RAM from an earlier lap, neither the peer's nor the hardware's. Left in place (there is no source for the hardware's value; stock melonDS has the same skip) and written down in to-do.md with the read-order proof. **The fix, and what it changed.** `MD_ARM_PERSIST` now defaults **on**: a poll that arrives with the reply slot as the game left it is answered with the frame the game itself last armed, because the reply slot the driver clears (`W_TXSlotReply1`) is not the slot the hardware transmits (`W_TXSlotReply2`) - the driver's clear is its own bookkeeping, and the hardware still holds the game's frame. The firmware's invented `00 00 00 00` body asserted a peer state the peer's game never reported. Measured on a full event-driven route run (3300+ rounds, both consoles `LINK ACTIVE`): `canned 1` for the *whole* run - that one being the cold-start round before the guest's game first armed - against 6267/28017 (22%) before, with `blank 0`, `stale 0`, `reanchor-dropped 0` and `deauth out 0 in 0` at every one of the 55 sampled windows. **The v0.1.9h deauth hazard did not reproduce**: two runs then lost the guest to its own `reason 0003`; this one ran the session out with both consoles active and every round answered by the guest's own bytes (both runs predate the FCS fix and `MD_REPLY_KEEP=1`, so that note is now “not reproduced with the current transport” rather than a verdict). **And the outcome, plainly: the games did not leave `FF 00` / `00 00`.** Same screen hash on both (`45d15630`), 100% black, `picture last changed 4430 frames ago`, `powcnt1 820F`, host command body `BE 01 02 00 00 80 FF 00` unchanged at +85.97s through +100.99s while the guest answers with its own `00 80 00 00` every round - so a fabricated peer state was *not* what kept the handshake latched, and the parked state is the games' own: the guest sits in its beacon/power-save dance (`IF 4800 IE E0FF`, `W_BeaconCount2 FFFE`, `W_CmdCount 0000`) with AID 1 and both reply slots in use, while the host re-sends one unchanging command body. Validation: smoke **66/0** with the knob on and with it off; NSMB 10000 frames unchanged (`cmds tx=7209, collected=7208, empty 1, clientfail 1, canned 0`, 3.85x realtime); one full MKDS route run with the new default. Nothing committed. |
| 2026-09-28 | 0.1.9h | The black screen is not an interrupt: both games service the wifi through the whole post-VS freeze, and the one address that looked like a spin is the game's own main loop | Asked whether each console is waiting on an interrupt it never receives at the black post-VS state, and built the instrument that answers it in the interrupt's own terms rather than in bytes. `Wifi::MPDiag` gains `irq_raised[16]`/`irq_acked[16]` (every `SetIRQ` per source, and every `W_IF` write that cleared that bit - i.e. a game entering and leaving its handler, per source) plus `irq_cpu_line` (the 0->nonzero transitions that pull ARM7 `IRQ_Wifi`), and `Wifi::SampleMPIrqSnapshot()` records the register state at the instant the digest is taken: `W_IF`/`W_IE`, `W_USCountCnt`/`W_USCompareCnt`, `W_BeaconCount1/2`, all four TX slot registers, `TXBusy`/`TXReqRead`, `PowerState`/`PowerTX`, `ComStatus`, `USCounter`/`USCompare` and `BlockBeaconIRQ14`. All of it rides the existing digest (`md_link_mp_digest`), so the health thread prints two new lines per console per 5s window (`mp pN irq:` / `mp pN hw:`) and `mkds_join.py` prints the same block per step; the control channel's `probe` carries a `wifi` object plus `pc9`/`pc7` (`probe` already read `POWCNT1`/`DISPCNT`/`VCOUNT`, and `md_get_pc` was already declared). **The answer, from a full event-driven route run that reached the post-VS black and held it for 630s and 34284 rounds** (host `cmds tx 34447`, `collected 34439`, `empty 8`, `clientfail 8`; guest `answered 28017`, `arms 34283`): *neither console is waiting on a wifi interrupt.* Every enabled source is raised and acked, round after round - p1 `IF 88C0 IE 703F pending 0000` with `raised`/`acked` climbing together on 0, 1, 6, 7, 12, 13 and 14; p2 `IF 0800 IE E0FF pending 0000` the same on 0, 1, 6, 7, 13, 14 and 15 - and `pending` (IF&IE) is `0000` at every sample on both consoles, so nothing is left *pending and enabled*. `irq_cpu_line` climbs throughout (110754 on p1, 186017 on p2), so the block keeps asserting the CPU's line; `BlockBeaconIRQ14` is never latched. The guest's `arms` counter climbs one per round beside `cmds rx` (34283 arms / 34284 polls), which is its game's handler running every single round. And the CPU's own wifi line is not even enabled: ARM7 `IME 0001 IE7 0104 IF7 0000` on both consoles - `IRQ_Wifi` is bit 23, i.e. bit 7 of IE7, and it is clear - so every wifi IRQ is consumed inside the game's own polling of the block, which is exactly what the per-source counters show happening. **The PCs, and the trap in reading them:** p1's ARM9 reads `0x0200FD10` in 31 of 36 samples - and in every sample from the *title screen*, before any link existed, which is what makes it a negative result rather than a finding: it is where MKDS's ARM9 sits in its own main loop, not a stall (p2's spreads over `0200FD10`/`0200DE88`/`01FFFF78`/`02147F48`/`02024850`/`020211F8`), and both ARM7s alternate between the BIOS idle loop `0000115C` and game code (`06012764`, `06008A68`, `037F88BC`...). No CPU is wedged and no CPU is inside a wait the emulator is not serving. So the freeze is neither our display path (v0.1.9f) nor an unserved interrupt: the two games are exchanging whole rounds and each has parked in its own state machine - the host's command body reads `BE 01 02 00 00 80 **FF 00** B8 B6` at +629.27s and the guest's own answer `**00 00** 00 00 B8 B6`, both unchanged for the whole freeze. **What the instrument did find, and the fix that was measured and *not* kept.** The guest's replies are 6267 parts stand-in (`fc=0158`, 28 bytes, body `00 00 00 00`) out of 28017 - 22% of rounds answered with a *fabricated* peer state (`00 00`), while the guest's own answers report `00 80` in ~79% of rounds. Cause, in code: stock's `SendMPReply` moves `W_TXSlotReply1` into `W_TXSlotReply2` on the way out (`IOPORT(W_TXSlotReply2) = IOPORT(W_TXSlotReply1); IOPORT(W_TXSlotReply1) = 0`), the fork's held arm was then consumed by the poll it answered, and any poll arriving before the game's next arm fell through to the stand-in. `MD_ARM_PERSIST=1` keeps the arm held until the game replaces it, so that poll is answered with the game's own most recent bytes instead. It does what it says - the stand-in falls to 1.6% and 6.5% of the guest's replies in the two runs that had it on - and **both of those runs then lost the guest to its own deauth (`reason 0003`) at 1535 and 4810 rounds, where the run with it off held both consoles `ACTIVE` for 34447 commands and 630s**. Two runs against one is not a verdict and this route is known to flake, but the hazard is mechanisable (a poll served an answer armed for the *previous* poll hands the peer a stale state as this round's answer, which a lockstep handshake cannot tell from a fresh one), so the default stays off and the knob ships as the reproducible experiment. Validation: smoke **66/0** with the knob off and with it on; NSMB 12000-frame reference unchanged (`cmds tx=9197, collected=9196, empty 1, clientfail 1, canned 0`, 3.25x realtime); one full MKDS route run with it off and two with it on. Tooling: `tools/run_app.sh` passes `MD_ARM_PERSIST` through. |
| 2026-09-28 | 0.1.9g | Round to arm to answer: the alternating partner is the games' own, and the reference arm falsifies the premise | Traced every collected answer back to the arm that produced it, using the arm serial the guest's own log has carried since v0.1.8y and a new identity line on the send side (`[mp] answer sent: frame seq=%04X arm=#%llu armed in round seq %04X, ...`, TXSendFrame case 5, and the same line with `arm=#0` for the firmware's stand-in in `SendMPDefaultReply`), with the frame's 802.11 sequence number added to the host's two receive logs (`reply window: p1 collected a reply ... seq=%04X`, `[mp] handing client 1's reply ... seq=%04X`). The chain is now readable off single fields instead of the interleaving of two console threads' lines, and it comes out clean: **one answer sent per round, one collected per round, one handed to the host's game per round, each carrying the arm it was sent from** - e.g. `answer sent: frame seq=CB10 arm=#3506 armed in round seq CC40` (guest) -> `collected ... 34 byte frame, fc=0118, seq=CB10, body[10]=00 80 00 00 B8 B6 <4-byte FCS>` -> `handing client 1's reply ... 30 byte frame, fc=0118, seq=CB10, body[6]=00 80 00 00 B8 B6` (host). **The three mechanisms the pass was asked to choose between, tested one at a time.** (1) *The held arm*: the rounds measured are served from the live reply slot (`already in the reply slot at the poll (arm #N, armed in round seq S-0x20)`), i.e. the arm the game wrote while handling the previous poll, which is the game's own one-poll pipeline and not a stale hop of ours - and the arm serials advance one per round, so no arm is served twice. (2) *A kept copy of a previous round's arm*: real in the code (`MD_REPLY_KEEP=2` retained two unread answers per peer and the reader is FIFO, so a round could be served the *older* of a clump) and measured on both titles. NSMB, 10000 frames, collected of commands sent at 0/1/2: **7208/7208, 7208/7208, 7196/7196, with `empty 0`, `clientfail 0`, `stale 0` at all three** - the retention the default of 2 was chosen for buys nothing now that frames arrive whole (the pre-v0.1.9f table was 1524/1904/1987). MKDS, same event-driven route, one run per arm: **`MD_REPLY_KEEP=0` collects 2659 with `clientfail 239` and the host deauthenticates the client**, `=1` collects 3087 with `clientfail 11` and both stay `ACTIVE`, `=2` collects 11314 with `clientfail 8` - so the clump is *not* a pre-FCS artifact and dropping everything unread is not affordable, while keeping one is. **The default is therefore 1**, not 0 and not 2: exactly one answer per peer survives a command and the drop loop discards the oldest entries, so the newest survives and the reader can never be handed an answer older than one it has already had a chance to read. What the queue *cannot* promise - and does not pretend to - is that an answer belongs to the round collecting it: the client's replies carry no echo of the command's seqno (the CMD path stamps the client mask into the *command*, not the reply), so a host can only tell an answer's round by when it arrived. (3) *The FCS-amended descriptor sized on one path and not the other*: measured consistent in both directions - the collected frame is the sender's plus the four FCS bytes (`collected ... body[10]=00 80 01 0E B8 B6 <crc>` next to `handing ... body[6]=00 80 01 0E B8 B6`), and the reply path's pre-crop sizing from the same descriptor gives the same numbers. **And the alternation itself, named:** the host's game alternates between `00 80 00 00 B8 B6` and `00 00 00 00 B8 B6` because the *guest's own arms* alternate between those two bodies on alternate rounds (counted over the last 2000 arms: 1378 vs 372, and both buffers `slot 8000`/`slot 811A` carry both shapes - so it is not a buffer artefact), and because on **459 of ~3100 rounds the guest's game arms nothing and the round is answered with the firmware's stand-in** (`fc=0158`, 4-byte body, `answer sent: ... arm=#0`), a differently-shaped answer the host's game reads as state `00 00`. Both are the games'/stock's own design (never silence - an unanswered poll is a counted client failure), not a stale copy in the queue: the reference arm `MD_REPLY_KEEP=0` does **not** remove the alternation, it removes the answers. The route was re-driven to the transition with the tight mapping in place: CUT OFF and VS both land, both consoles leave the lobby together, and they end on the black screen after VS as before - host `ACTIVE` commanding `BE 01 02 00 00 80 FF 00 B8 B6` (state FF) and the guest `ACTIVE` answering `00 80 00 00 B8 B6`, both pictures the same `crc 45d15630` and frozen on the same frame, `powcnt1 820F`, `collected 3087`, `clientfail 11`, `blank 0`, `stale 0`. So the mapping is now structural and verified; the stall is in the games' own handshake and is *not* answer reordering. One trap for the next pass, found while building the chain: **the reply frame's 802.11 sequence number is not a usable identity** - every reply after the first is sent with the same seq and the retry bit, because `TXSendFrame`'s own `IncrementTXCount` makes the next send look like a retransmission (`noseqno = 2`), and upstream does the same; use the arm serial, which is why it is on the send line. Validation: smoke **66/0**; NSMB as above; two MKDS route runs (arms 0 and 1) plus the earlier 2. |
| 2026-09-28 | 0.1.9f | The wire was four bytes short: the FCS the hardware appends, which the crop was eating out of the game's own frame | Asked first whether the black, frozen host is the game's own state or our display path, and it is the game's: with the control channel reporting the host black and frozen, the **emulated** framebuffer reads all-zero on both screens and the renderer's own copy matches it byte for byte (`crc 45d15630` on both consoles, `powcnt1 820F` = LCDs on, engines A and B on, `asleep no`, `frames +300` per 5s), and the same probe reads a *picture* (lobby runs: top mean 168, 2% black) when the game has one - so the read discriminates and our path is faithful. That probe is a new control command (`{"cmd":"probe","player":N}`: both framebuffers, the copy, `powcnt1`, `DISPCNT` A/B, brightness, `VCOUNT`, frame counter, sleep state) and it answers this question as numbers instead of pixels. **Then the divergence, and it was ours.** Both games set `W_RXLenCrop=2`, and the crop (`framelen -= (W_RXLenCrop << 1) & 0x1FE` in `Wifi::CheckRX`, stock melonDS) exists to trim the **4-byte FCS the wireless hardware appends to every frame it transmits** - which our transport never appended, because `LocalMP` carried exactly the bytes the game wrote. So the crop removed the sender's *last four* bytes instead: the host's game wrote a 34-byte command and the guest's game was handed 30 (`[link-trace] p1 tx cmd len=46` next to `[mp] cmd seq=... offered to this console's game: 30 byte 802.11 frame`), and the guest's 6-byte answer reached the host's game as 26 bytes / a 2-byte body. The lost halfword is exactly where each game keeps the state it is telling the other about (`BE 01 02 00 00 80 **01 00** B8 B6`), which is the "one state apart, parked" both previous passes measured. Fixed in `LocalMP::SendPacketGeneric`: append the FCS (a real CRC-32 over the frame, LSB first - nothing verifies it, upstream included, but the four bytes on the wire are then the field the hardware would have computed) and amend the length field in the frame's own 12-byte TX descriptor to match, because the regular receive path checks that field against the bytes it got (`bad frame length`) and the reply path sizes the frame from it *before* cropping, so leaving it alone would truncate the host's copy of the answer twice. A zero-length entry (the blank reply) is our own "nothing to send" marker, not a hardware frame, and keeps its length of zero. `MD_MP_FCS=0` restores the pre-fix wire as the reference arm. **A/B, same route back to back, event-driven press:** baseline = the host's game deauthenticates, its screen goes black and frozen, its body parks on the lobby's `BE 01 00 00 00 00 06 00 B8 B6`, `collected 1516`, and the guest is handed 6 of the host's 10 payload bytes; fixed = `collected 11314`, `answered 10749`, `blank 0`, `stale 0`, `clientfail 8` of 11314, host still `ACTIVE` and never deauthenticated through a 25s watch, and the bodies match the senders exactly (guest handed `len 10 body BE 01 02 00 00 80 02 00 B8 B6`, host handed `00 80 01 11 B8 B6` as a 30-byte frame with a 6-byte body). **What the fix changes in the games' own behaviour**: the guest's game now *follows* the host - its answers walk the lobby's `00 80 5A 5A B8 B6` -> `00 80 01 00`..`01 0F` (each with a 44-byte companion frame, the member-record sweep) -> `01 FD` -> `03 00`/`04 00` -> `00 00`, i.e. it adopts the post-CUT-OFF transitions instead of answering the lobby's template forever, which is what four earlier passes could not get out of it. **What is left, stated as the state both games are in at once**: after VS the two pictures go black *together* (same crc `45d15630`, both frozen on the same frame, `powcnt1 820F`, link `ACTIVE` on both sides, host still polling, guest still answering 87% and 13% with the firmware's stand-in) with the host's command state at `01 00` and the guest answering `00 00` for 250 rounds and more - so the two games are now on the **same** screen and exchanging **whole** frames, each waiting for the other's next state. One asymmetry recorded for the next pass: the host's game reads the guest's answer *alternating* between two arms (`00 80 01 11` / `00 80 01 FD`) every other round, which is what `MD_REPLY_KEEP=0` should be tested against before the games are blamed again. Validation: smoke **66/0**; NSMB harness with the fix `cmds tx=9209, acks=9209 (clientfail 1), collected 9208, empty 1, blank 0, stale 0, 3.70x realtime` (the canonical baseline, so a working title is unaffected); two event-driven MKDS route runs plus a 60s-watch run whose mid-run screen reads answered the black-screen question. Tooling: `tools/run_app.sh` now passes `MD_MP_FCS`, `MD_STOCK_MP` and `MD_REPLY_KEEP` through to the app (it passed none of them before, so an A/B through the real app could not be armed without editing the script). |
| 2026-09-28 | 0.1.9e | The press was the problem: with CUT OFF pressed at the offer, the host reaches FF and the guest's game takes it | Pressed CUT OFF the moment the host's own screen offers it, then read both consoles' frames, writes and states off the log at each step - and the transition the last four passes could not reach happens on demand. The press was late for two tooling reasons, both fixed. **(1) The control channel read a screen *name* as a failed number and silently fell back to "both"**: `{"screen":"bottom"}` asked for the touch screen and got the top one as well, so the driver's "wait until the touch screen holds still" was watching MKDS's animating top screen and its CUT OFF press landed ~10s after the join; the channel now takes `top`/`bottom`/`both` as well as 0/1/2 and *refuses* a value that is neither, because a silent fallback to a different screen is exactly the kind of feedback a driver cannot detect. **(2)** `tools/mkds_join.py` presses on the screen now: `CUT_OFF_RECT` (x 84..214, y 156..187 plus margin) is sampled through the channel's eval, the baseline is taken while the host's room holds one racer (`host's CUT OFF patch reads 103 with one racer`), and the tap fires on the change (`CUT OFF on p1's screen after 0.5s (patch luma 103 -> 142), tapping (149, 167)`). Two more parse defects fell out of reading the report for the transition's evidence: the digest parse only knew `[host]`/`[guest]`, so a console that had left the group said `[not in MP]` and simply *vanished* from the report - and the report now prints, per console, the frame it was handed (seq, len, body, clock), what it wrote back, `arms`/`answered`/`blank`/`canned`, link state and picture state (`changing`/`FROZEN`), which is the four questions this pass had to answer, answered without reading a pixel. **The transition**: CUT OFF is reached with the group alive - `p1 handed seq 4820 len 10 body BE 01 02 00 00 80 06 00 B8 B6 at +69.17s` (still the lobby's 06), then the page is turned - `p1 handed seq B8B0 len 10 body BE 01 02 00 00 80 FF 00 B8 B6 at +74.15s` - and **the guest's game adopts it**: its answer stops being the lobby's `00 80 5A 5A B8 B6` and becomes `00 80 00 00 B8 B6`, a state byte the guest's own game wrote after the host's transition frame was delivered. The session then runs on for a minute and more *live*: host `ACTIVE` and guest `ACTIVE`, host `collected` 499 -> 1387 -> 1986, guest `answered` 351 -> 1104 -> 1647, `blank 0` and `stale 0` on both sides - the opposite of the dead session the previous pass ended on. **Where it stops now, stated as a difference between the two consoles**: the host's picture goes black (both LCDs read pure black through `see`) as it enters FF and then stops changing, while its link stays ACTIVE and its counters keep advancing, and A on either console does not move it; the guest's screens stop on a modal (light panel, one line of text) rather than the game-select list. So the two games are **not** on the same screen, and the emulator underneath is healthy (`frames +300` per 5s, `pc 12-53/100` through the freeze, `picture FROZEN` but never `looks wedged`). The earlier passes' "host sat on its PLEASE WAIT modal then deauthenticated" is this same state an exit later. One counter-example kept for the record: when the press *is* late (a fixed settle), the host's game has already left its lobby loop (`last cmd seq 8180 ... mask 0000`) and the tap hits a game that is not polling - so an event-driven press is not polish, it is the difference between reaching the transition and not. Validation: smoke 66/0; NSMB harness `cmds tx=9197, acks=9197 (clientfail 1), collected 9196, empty 1, blank 0, stale 0, 3.85x realtime` (the canonical baseline); two event-driven MKDS runs, the second reaching the transition with the link alive. |
| 2026-09-28 | 0.1.9d | The arm a poll takes is always the game's own, and the guest's game reads every frame it is handed | Answered "is the answer the host collects the guest's own newly-armed bytes or our held arm from before the transition" by giving every arm an identity, because body bytes cannot tell the two apart. Each `W_TXSlotReply1` write with bit 15 now carries a serial plus the round (`MPLastSeqno`) and the emulated clock it was written at, and `FinishRX` reports which arm it consumed, the round that arm was armed in and how long before the poll it was written. Measured, this is the whole answer: **every armed answer is a distinct, freshly-written arm** - NSMB harness 9196 armed answers with the arm serial strictly increasing and **0 repeats** (last arm #9196, `arms 9196`), MKDS 571 armed answers likewise with 0 repeats (last arm #574, `arms 574`). And the arm a poll consumes is the one the game wrote in the *previous* round: on the harness every exchange reads `answering cmd seq=0100 ... already in the reply slot at the poll (arm #2, armed in round seq 00E0, 4091 us before this poll)`, i.e. one poll of pipeline that is the game's own (it arms the answer for the next poll from inside the handler for the current one). So a transition command is by construction first answered with the pre-transition state, and the reaction can only appear on the wire one poll later - that is the protocol, not our reply path. **The frame is offered to the guest's game and the game reads it.** New delivery dump at the RX-header site, in the guest's terms: `cmd seq=2FF0 offered to this console's game: rxflags=801C, 30 byte 802.11 frame, header[801C 0040 0228 0014 001E 4080] at 1A10, payload@24[BE 01 02 00 00 80], clienttime 446, mask 0002, AID 0 (associated 1: this poll is NOT ours)`, and `Read` now matches the game's own reads against that delivered region - the guest's game reads the header and the payload inside it on **every** poll, including every poll it declines to answer. The transport is byte-faithful at every size: MKDS run 2 collected 150x30B + 21x68B frames exactly as the guest sent them (30 -> 30, 68 -> 68, no truncation, no reorder) plus 130x28B firmware stand-ins. **One real defect fixed:** `MPArmHeld` was cleared only in the `held` branch, so a poll answered out of the *live* register left the arm held and the next poll that found an empty slot re-sent bytes the game had already had sent - the first silent round after a live answer cost one fabricated answer. It is now cleared on every consumption (one arm, one poll), which makes the 0-repeats result structural rather than lucky. **One MKDS run caught the death from the guest's side**: 1190 commands over 10.8s, the guest's game walking its member-record sweep `12 8E 01 00..11 20 00` (17 slots, index in body byte 3, byte 1 toggling 0E/8E) twice, then it stops arming after arm #574 in round seq 5DD0 at t=94763048us while **still reading every poll and still writing the reply slot every round - with 0**. For the next 618 polls the host is fed stock's canned `fc=0158` empty frame (`nothing armed: the firmware's own reply`, arm #574, 1.5s old at the end), the host's own client mask goes `0002 -> 0000` at `seq=A1F0`, and it deauthenticates (`reason 0003`, then a broadcast `0003` and its beacon disarmed) - so the host gives up 1.5s after the guest's game goes quiet, which is the same end state the earlier passes saw, now with the silent side named and timestamped. Route timing worth recording: the driver's CUT OFF tap lands ~6s after a join, and in run 1 the session had already died at +5.1s (guest inert from arm #175 in round seq 2440, t=87532053us), so CUT OFF reached a game that was no longer in multiplayer - the FF/FE transition is only reachable in a run whose join survives the driver's walk. Validation: smoke 66/0, NSMB harness 9197 commands / 9196 collected / 1 empty / 1 clientfail / 2.39x realtime (baseline). Instrumentation kept (all behind `MD_LINK_VERBOSE`): arm serials and provenance, the offered-frame dump, and the RX-zone read matcher; the dispatch price is a handful of fields on a handful of events. |
| 2026-09-28 | 0.1.9c | Every route tap measured against the screen it lands on, and the frame the guest stops following | Walked the hands-free MKDS route one tap at a time on the emulated screens and read each console's own picture back at every step, because seven passes of failures could all have been a route tapping nothing. **The map is genuine, with two corrections worth keeping.** The main menu's rows sit at y 16..31 / 63..75 / 109..121 / 157..169 of the bottom screen and (128,68) is inside MULTIPLAYER; the group list's CREATE GROUP button is y 156..187 and (128,157) is inside it; the group bar P2 taps is slot 1 (y 11..43) and (127,23) is inside it; the mode select's NORMAL block is y 24..81 and (128,68) is inside it. **CUT OFF is real and is the host's own control** - the host's top screen says "Touch Cut Off to stop accepting entries and begin racing" and the button is the light rect at x 84..214, y 156..187 - but it **only exists once a second racer has joined**: at 1/8 the area under (149,167) is empty blue, and the 2/8 screen (the only state the harness taps it in) has the button there. Pressing it makes the host's game send `00 FF` within the same frame and its screen go to a modal wait, so the tap is not a stray. **VS is real too**: on the game-select screen the VS block is y 6..45 (label y 21..33) and (128,22) lands on it. One swallow cost this pass two taps and is now fixed in the driver: MKDS's title screen is an attract loop that plays a demo race and returns to the menu by itself, and a tap that lands during the demo is discarded - `tools/mkds_join.py` now waits (`wait_stable()`) for the target console's *touch* screen to hold still before every tap, since the demo animates it while every menu, the group list and the lobby hold it still even when the top screen animates. **The first frame where the guest stops following is named.** The host's first post-CUT-OFF transition command is `seq=8FC0 body 02 1E 00 FF 00 00 01 00 02 00 6F 00 B8 B6`, written by the host's game at emulated clock `t=765038599` (wall 13:07:20-13:07:21, on the press), handed by the emulator to the guest's game at guest clock `765038324` (`cmd from host: host clock 765038599, my clock 765038324, lead 275, window 446 us, a new command, delivered to the console's game`), answered 424us later at `765038748` with `00 80 01 00 B8 B6` - an answer whose state byte is still the `00`/`01` alternation it used in the lobby, i.e. the guest's game takes the command and answers it without adopting it. The complete host-command -> guest-answer table for one run (5707 exchanges, 37 distinct host bodies) shows the host walking 06 -> **FF** -> FE -> 00 00 -> 01 00 -> 01 01 -> 02 00 -> 05 00 -> 03 00 -> FF 00 (game select, its frames then carrying the host's own touch point, which is why both screens mirror the VS highlight) while the guest's answers walk 5A 5A -> 01 0D/0E -> 01 FD -> 03/04 -> 00 00: the guest moves, but a state behind, and never into the host's FF/FE handshake. Re-run end to end with the fixed driver (`--cut-off --vs --watch 25`): both games reached the game-select screen with the link alive and **0 blanks, 0 deauths** through the watch, then sat there (no race). In a manual run of the same route the host instead sat on its "PLEASE WAIT..." modal for 61s and then deauthenticated the client itself (`deauth out 1`, reason 0003), after which **both screens went fully black** (all-zero framebuffers while both consoles kept producing frames) rather than showing MKDS's "Communication error" dialog - worth knowing before the next run reads a black screen as a crash. Validation: smoke 66/0, NSMB harness 9197 commands / 9196 collected / 1 empty / 1 clientfail, 2.41x realtime (baseline, not the flake). Tooling kept: `appctl.py render --bright` (mono renderer for bright-on-dark ink) and `--mark X Y` (draw the tapped point onto a screen crop) - the two views that made "which control does this coordinate hit" answerable; the throwaway probe script was deleted. |
| 2026-09-28 | 0.1.9b | The reference says the transport is not the regression | Built the comparison this pass was asked for: **`MD_STOCK_MP=1`** puts the fork's reply-path decisions back to upstream/master's - the AID that decides whether a poll is ours *and* the AID stamped on the answer are the live `W_AIDLow` again, nothing is restored from a held arm (so a poll that arrives after the driver cleared the slot is answered by the firmware's stand-in), a new command drops every unread answer, and the staleness window is stock's 32 ticks rather than two frames - so the same route, the same driver and the same instruments run against both. Same MKDS 2-console route (`--cut-off --vs --watch 35`), and **the reference cannot hold the join at all**: guest `cmds rx 6576, answered 3610, blank 2966, canned 0` with its answer frozen at `00 00 5A 5A B8 B6` (searching) while arming 6570 times - in stock the arm is invisible at the poll, which is exactly the v0.1.8y defect; host `cmds tx 6576, acks 6576, clientfail 2989` (45%), `collected 3587, empty 38, stale 22, reanchor-dropped 16, blank 2951`, and **`deauth out 2`** - the host kicks the client, the guest reports `deauth in 1`, and both consoles are out of MP *before* the CUT OFF tap (harness reads `p1=host p2=guest` at the lobby, `no mp activity` at the cut-off read). Ours on the same route: 7221 of 7228 polls answered, 0 blanks, `clientfail` 0-7, no deauth until the exit, and the games' own states move *together* (host 06 -> 05, guest `5A5A`/join-request -> `04`) once the player closes the lobby. So **the join failure is not our diff** - upstream's semantics are strictly worse on this title, and the reply-path rebuild is what lets a group form at all. The VS transition is *not* comparable through this A/B: the reference never reaches it, so what kills the session there stays unattributed (this pass's own measurement stands unchanged - host 05 / guest 04 parked one apart, the host's beacon content changing at that moment, the guest's `0003` deauth ~12s later). Scope, stated in the code: the arm reverts the reply path and the reply queue only and deliberately keeps the fork's emulated-time clock, because a free-running clock in a frame-locked pair is a defect this fork measured (254ms of parted time over one join), not a fork-only design choice. One run per arm; MKDS's join is flaky by itself (the previous pass's no-tap control died in the lobby too), so a second reference run would harden the result. Smoke 66/0 |
| 2026-09-28 | 0.1.9a | The client is not supposed to beacon, and the lobby-close is the player's | Instrumented the beacon path end to end (`SetIRQ14`'s three exits, `StartTX_Beacon`, the beacon-with-our-BSSID branch, `USCOMPARE0`/`USCOMPARECNT` writes, and `TXSLOTBEACON` writes without bit 15) and ran MKDS twice - once on the usual route, once on the same route without `--cut-off --vs`. Three findings, one of them a correction. **(1) The host did not leave the lobby on its own.** The session clock starts at app launch, not at radio-up: run A's digest at `05:04:40Z` reports its last command `at +48.23s` (and `05:04:32Z` reports `+40.23s`), so t=0 is `05:03:51.8Z` - the harness's own `launched app pid 88143` at `00:03:53` local - which puts the route's CUT OFF press at t=44-45s, the same second as the host's 06 -> 05 change (t=45.204s); and the no-tap run holds the host's command body at `BE 01 02 00 00 80 06 00 B8 B6` (lobby) for the whole 53.74s of its session. The group does not expire a member that never announces itself; the player closes the lobby, and the guest follows to its `04` within ~3s. **(2) The guest never arms a beacon, and nothing in this fork denies it one.** The guest takes the host's beacons and does the whole group dance - 63 `beacon accepted (our BSSID)` with `USCOUNT` forced to the beacon's own timestamp (one foreign-BSSID reject, before association), 64 `USCOMPARE0` writes with bit0 set (its slot in the schedule), 121 `beacon IRQ14` firings at exactly that slot - and writes `W_TXSlotBeacon` zero times, not even a disarm. The grant path is `SetIRQ14 -> StartTX_Beacon`, gated on `W_USCompareCnt & 1` and `W_TXSlotBeacon & 0x8000`: in the same session it started **469** beacons on the host and found the slot empty **121** times on the guest. So MKDS's client does not beacon in the lobby, and membership there is carried by the poll/reply traffic (its join request as the answer to a poll, the host's acceptance as its next commands). **(3) The host's beacon carries no roster**: its 84-byte payload is byte-identical for a whole session - interval, capability, SSID/DS-param/TIM IEs and the 54-byte vendor IE with Nintendo's OUI and the `melonDS` nickname - except the hardware-owned 8-byte timestamp; the bytes that moved in run A moved only once the host's game had advanced past the lobby. Also observed: **without the press the host's game never acts on the guest's join request** (787 of 787 replies collected with `clientfail 0`, its body `12 8E 01 0A 20 00 ..` read back by the host's own game) and the guest deauthenticates itself at 53.74s - the known ~1-in-4 pre-CUT-OFF join death, not a new one. NSMB harness unaffected by the trace-only additions: **9197 commands / 9196 collected / 1 empty / 1 clientfail, 3.94x realtime** (not the flake). Smoke 66/0 |
| 2026-09-27 | 0.1.8z | The frame is the game's own; the two games stop in their own states | Dumped the MKDS session's big broadcast and the games' own state bytes field by field, and the answer to "is it a field we synthesize" is no. The 120-byte `fc=0080` the host's game sends is its own bytes: with the frame dumped either side of our stamp (`game wrote:` then `sent:` at the TX site, and the full 80-byte payload on delivery instead of just its first 48), the only difference is the 8-byte timestamp at payload offset 0, which is the field the hardware owns (`*(u64*)&TXBuffer[0xC+24] = USCounter`; interval `00D2`, capability `0021`, SSID `01 02 82 84`, DS-param channel, TIM and the 52-byte vendor IE with Nintendo's OUI and the console's UTF-16 nickname `melonDS` all come from the game). It is also not a transition frame: the host's game arms and sends one every ~217ms from the moment its radio comes up (115 in the session, `beacon armed` on every one, the guest never arms one), the guest tolerated all of them but the last, and the only bytes that differ between the tolerated and the fatal one are the game's own state (TIM bitmap `00`->`01`, a 32-bit counter, and a flag byte in the vendor IE). What actually diverges is the games' own protocol state, and the wire shows it: the host's command bodies and the game-written byte 7 of the MP header went `06` (lobby) -> `FF` -> `FE` -> `00` -> `01` -> `02` -> **`05`, parked from seq 4750 for 5574 commands**, while the guest's answers went `5A5A` -> `01` (join) -> `03` -> **`04`, parked from seq 4670**, with its own `FD` marker interleaved on every other answer (`01 FD` / `04 00`). Both games stopped advancing within ~80 polls of each other, in their own states, and everything after that is arithmetic: the guest deauthenticates `reason 0003` at t=64.31s, the host broadcasts its own `0003` 3ms later, both radios go off, both screens sit on MKDS's "Communication error. Press the A Button." (read out of the framebuffer). **The route is not the trigger and neither is the reply path**: the host's game had already left the lobby state at t=45.204s, 3.6s *before* the harness' CUT OFF tap (t~48.8s) and 19s before the deauth, and the link was carrying 7221 of 7228 answers with 0 blanks and `clientfail` 7 the whole time. Correction to v0.1.8y's window A/B: the NSMB harness is **flaky**, not window-sensitive - it reproduced 9197/9196-collected twice before that change and twice after it, and its "0 collected, counters all zero, 1.7-1.8x realtime" failure mode also appeared with the window back at two frames, so the four runs were ordered, not matched, and the tightening was rejected on evidence that does not support it; five healthy runs at two frames across the session say the flake is real and orthogonal (the harness is the only place it reproduces, and it is what actually blocks a clean regression check). Diagnosis added: `beacon armed` per (re)arm of a console's own beacon, full-payload delivery dump, and the beacon's game-wrote/sent pair; smoke 66/0 |
| 2026-09-24 | 0.1.8b | Link watchdog | User's human MKDS lobby join froze repeatedly; log proves emulators healthy while game WiFi stalls; bridge gains per-kind packet counters, [link-event] timestamps, STALL/IDLE/ACTIVE health classification (MD_LINK_VERBOSE); MITM echo manager designed against mgba-splitscreen's echo-only-during-discovery lesson |
| 2026-09-24 | 0.1.8c | Channel fix | Watchdog auto-kick cured a live STALL unattended (23min ACTIVE after); root cause isolated to RF channel mismatches (consoles hop channels, CheckRX drops foreign-channel frames); fix = channel-transparent virtual air (restamp rx frames with receiver's channel); per-console core-log tags added |
| 2026-09-25 | 0.1.8e | Console identity, frame trace, reply window | User's "froze up trying to get p2 to find and join" run read back from /tmp/md_app.log: no freeze (59.8fps throughout, UI 3s, clean exit) - the games simply never talked (1-3 frames per lobby attempt in 7 minutes). Root cause class: every instance boots the same firmware, so all consoles presented the identical MAC and a joiner could not tell the host's room from its own console; bridge now assigns a distinct MAC per slot (upstream `EmuInstance::customizeFirmware` offset scheme) at ROM load; `MD_LINK_VERBOSE=1` now narrates every bus frame from both ends (fc/addresses/sync/mask), which is what makes the next lobby attempt diagnosable; health line's misleading `wifi N/M` relabelled `wifi on/off` |
| 2026-09-25 | 0.1.8f | Reply window in the wrong clock domain | Third identical MKDS join failure read back with numbers: the client sent 117 replies, the host collected 71, and 52 of 123 collection rounds came back empty - so replies were neither lost nor discarded, the host was giving up before they arrived. `LocalMP::RecvReplies`'s staleness bound had been fixed to one frame (zero `DROPPED a reply` lines since), but the wait itself was still `md_link_set_reply_timeout(2)` **wall-clock** ms, i.e. 0.8ms of *emulated* time at the measured 2.4x - less than the 1-2ms DS slot window it stands in for and far short of the client's next frame boundary, where the answer actually goes out; a host that times out says `clientfail=0002` and the game drops the client. Now 34ms (two emulated frames), `MD_REPLY_TIMEOUT_MS` to sweep, wait still returns the moment a reply is posted; pacing regression-checked on the harness (2.48x vs 2.41x baseline). Collection improvement still needs a live join to prove |
| 2026-09-24 | 0.1.8d | Crash forensics + safe link ops | Discovered melonDS's ARM JIT owns SIGSEGV for fastmem and that re-asserting our handler stole it (app died at heartbeat #1 three runs in a row) - handlers now installed only where unowned and audited, never re-installed; crash reports with backtraces proven end to end (-rdynamic, deliberate ABRT/SEGV); launcher supervisor records exit status/signal for the faults no handler sees; all resync/probe/selftest bus access moved onto the consoles' driver threads (LinkOp queue) - cross-thread drains were stealing game frames; `md_link_recv_packet` i32/usize FFI mismatch fixed; bus never destroyed while threads hold it; lockstep with peer-death detection (no more silent hang); always-on link verification counters in the UI; 30s heartbeat + UI liveness + window lifecycle logging; 16.7-min NSMB 2P soak clean |

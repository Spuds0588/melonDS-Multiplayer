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
| 2026-09-11 | 0.1.0 | Project Initiation | Initial documentation created, mgba-splitscreen lessons added |
| 2026-09-12 | 0.1.1 | Native bridge | C bridge over the headless core, hand-assembled diagnostic ROM, 36-check smoke test; build path documented |
| 2026-09-12 | 0.1.2 | Local link | LocalMP wired in with per-slot routing and broadcast delivery, visual capture + contact sheet, 58-check smoke test |

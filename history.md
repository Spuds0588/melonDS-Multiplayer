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
1. Complete Tauri v2 project setup
2. Integrate melonDS as a static library in the Tauri app
3. Implement framebuffer capture and WebSocket piping
4. Render single NDS screen in webview
5. Extend to multiple screens side by side
6. Implement view modes (grid, speaker, focus, overlay)

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

# Product Requirements Document (PRD)
**Project:** melonDS Multiplayer (Splitscreen Focus)
**Platform:** Tauri/Rust (Desktop Host App) + Canvas/WebView (Local Display)
**Core Concept:** A native desktop application that runs multiple linked melonDS emulator instances side by side for local splitscreen multiplayer on a single machine, using melonDS's virtual Wi-Fi for in-game connectivity.

> **Primary Goal:** Local splitscreen couch co-op multiplayer (2-4 players on one machine)
> **Secondary Goal:** Online play for remote players (optional future enhancement)

---

## 1. Overview & Objectives

### The "What"
A platform for playing NDS multiplayer games (Mario Kart DS, Pokémon trading/battles, etc.) on a single computer with multiple players. The system runs multiple melonDS instances side by side, each with their own screen and controls — like mgba-splitscreen but for Nintendo DS.

### The "Why"
- NDS has great multiplayer games that require link cable/Wi-Fi connectivity
- melonDS has excellent Wi-Fi emulation for in-game multiplayer
- No existing solution for easy local splitscreen DS multiplayer
- Couch co-op is the primary use case — online is bonus, not core

### Target Audience
- Players who want to play multiplayer DS games on one computer
- Retro gaming enthusiasts with multiple controllers or keyboard setups
- Groups gathered around a single machine (couch co-op)

---

## 2. System Architecture

### 2.1 The Host (Tauri Desktop App)

**Backend (Rust):**
- Manages 1-4 instances of the melonDS core (C++ library)
- Routes inputs to specific player instances
- Handles frame loop synchronization
- Manages save states (global save/load across all players)
- Optional: WebSocket server for frame data to frontend

**Frontend (WebView/JS/Canvas):**
- Renders all player screens side by side
- Implements view modes: Grid, Speaker, Focus, Overlay/PiP
- Handles keyboard input zones (per-player, remappable)
- Handles gamepad input (auto-assign to players)
- UI: Game library, settings, view controls, pause menu
- Global hotkeys: turbo, save/load, pause, view cycling

### 2.2 Link Cable / In-Game Connectivity

**Primary mechanism:** melonDS's native virtual Wi-Fi emulation
- Instances connect via virtual Wi-Fi just like real DS hardware
- Games see each other in multiplayer lobbies
- No custom sync needed — melonDS handles it
- Works for: Mario Kart DS, Pokémon, Mario Party DS, etc.

**Why use native Wi-Fi:**
- Don't reinvent synchronization
- melonDS's Wi-Fi emulation is mature
- Games work exactly as they would on real hardware

---

## 3. Core Features

### 3.1 Splitscreen Display

**Display modes (inspired by mgba-splitscreen):**

| Mode | Description | Use Case |
|------|-------------|----------|
| **Grid** | All screens equal size, side by side | Default for 2-4 players |
| **Speaker** | 1 large screen + smaller others | Competitive games, attention focus |
| **Focus** | Single screen, full size | Watching, solo play |
| **Overlay/PiP** | Picture-in-picture layout | Small screens, monitoring |

**Technical requirements:**
- Canvas-based rendering for each NDS screen
- Layout calculations for each view mode
- Cycle through modes with hotkey (F8/F9)
- Hotkey should be remappable

### 3.2 Controls

**Per-player keyboard layouts:**
- Player 1: Left hand cluster (WASD + nearby keys)
- Player 2: Right hand cluster (Arrow keys + nearby keys)
- Players 3-4: Additional zones or gamepad-only

**Default layout (from mgba-splitscreen pattern):**

Player 1 (left hand):
| DS Button | Key |
|-----------|-----|
| D-Pad | W/A/S/D |
| A | K |
| B | J |
| L | H |
| R | L |
| Start | Enter |
| Select | Backspace |

Player 2 (right hand):
| DS Button | Key |
|-----------|-----|
| D-Pad | Arrow keys |
| A | M |
| B | N |
| L | V |
| R | B |
| Start | P |
| Select | O |

**Requirements:**
- All controls fully remappable in UI
- Global hotkeys also remappable
- Gamepad support: auto-assign controllers to player slots
- Touch input for bottom screen (mouse clicks mapped to touch coordinates)

### 3.3 Save State Management

**Global save/load (synced across all players):**
- Quick save (F5): Saves state of ALL running NDS instances
- Quick load (F7): Loads state of ALL running NDS instances
- Saves stored together as a set

**Battery saves:**
- .sav files loaded automatically from same folder as ROM
- One .sav per instance
- Persisted between sessions

**Save import/export:**
- Option to export/import saves per instance or as a set

### 3.4 Game Library

**Features:**
- Recent games list (persisted locally)
- ROM folder scanning (add folders to watch)
- Box art display:
  - Look for local images (PNG/JPG with same name as ROM)
  - Fallback to online box art service if no local image
- Controller/keyboard navigable
- Launch with player count selection

### 3.5 Pause Menu

**Behavior:**
- Pausing (Escape) freezes ALL players simultaneously
- Menu is controller-navigable
- Options: Resume, Save State, Load State, Players (change count), Game Library, Quit ROM

**Why pause all:**
- In multiplayer, one player pausing shouldn't give advantage
- Keeps all players in sync

### 3.6 Turbo Mode

- Turbo (Q key): Fast-forward all instances past 60fps
- Useful for: menu navigation, dialogue skipping, grinding
- Should be toggleable (press Q to toggle on/off)

### 3.7 Deep Links (URL Parameters)

- `?players=N` — Start with N players (1-4, default 2)
- Optional: `?rom=<path>` — Launch specific ROM

---

## 4. Development Implementation Plan

### Phase 1: Native Bridge (Host Infrastructure)
- [ ] Set up Tauri v2 + Rust project structure
- [ ] Integrate melonDS as C++ static library (via CMake or direct build)
- [ ] Implement framebuffer piping from melonDS to frontend
  - Option A: WebSocket (same frontend works in browser too)
  - Option B: Shared memory / direct canvas (faster, Tauri-only)
- [ ] Target: Render single NDS screen in Tauri webview at 60FPS

### Phase 2: Splitscreen Display & Controls
- [ ] Render multiple NDS screens side by side (2-4 instances)
- [ ] Implement view modes: Grid, Speaker, Focus, Overlay/PiP
- [ ] Implement per-player keyboard layouts (with remapping UI)
- [ ] Implement gamepad support (auto-assign to players)
- [ ] Implement global hotkeys (turbo, save/load, pause, view cycle)
- [ ] Implement touch input for bottom screen
- [ ] Deep link support (?players=N)

### Phase 3: Game Library & UX
- [ ] Build game library UI with box art
- [ ] Implement ROM folder scanning
- [ ] Add persistent recent games list
- [ ] Implement global quick save/quick load (F5/F7)
- [ ] Auto-load battery saves (.sav) next to ROMs
- [ ] Implement pause menu (controller-navigable)
- [ ] Add turbo mode (Q key)
- [ ] Save import/export functionality

### Phase 4: Polish & Testing
- [ ] Test with actual multiplayer games:
  - Mario Kart DS (Wi-Fi races)
  - Pokémon games (trading/battling)
  - Mario Party DS
  - Other multiplayer NDS titles
- [ ] Performance testing with 2-4 instances
- [ ] Memory/CPU usage optimization
- [ ] Bug fixes
- [ ] User documentation / in-app help
- [ ] Beta release

### Phase 5: Online Play (OPTIONAL/FUTURE)
*Only pursue if there's user demand. Not the core focus.*

- [ ] Research WebRTC for remote frame streaming
- [ ] Build simple web client for remote viewing
- [ ] Input routing back from remote players
- [ ] Connection management UI
- [ ] Testing with remote players

---

## 5. Non-Functional Requirements

### Performance
- 2-4 NDS instances at 60fps each
- Frame pacing synchronized between instances
- Minimum: Hardware that can run 2 instances at full speed
- Recommended: Hardware that can run 4 instances at full speed

### Usability
- Easy to set up (load ROM, select player count, play)
- Intuitive controls (keyboard zones make sense)
- Flexible view modes for different setups
- Game library for quick access to 자주 played games

### Compatibility
- Preserves melonDS's game compatibility
- Uses melonDS's native Wi-Fi (no special handling needed)
- Works with any DS game that has multiplayer

---

## 6. Out of Scope (For Now)

- **Online multiplayer** — Future optional enhancement, not core
- **Netplay with remote opponents** — Different use case
- **DSi-specific features** — melonDS handles this, may or may not work in splitscreen
- **Cross-platform play** — Desktop focus first
- **Replay/recording** — Nice-to-have, not essential

---

## 7. Success Criteria

1. **Core functionality**: Can run 2-4 melonDS instances side by side
2. **Link cable works**: Multiplayer games see each other via virtual Wi-Fi
3. **Controls work**: Each player can control their instance with keyboard or gamepad
4. **View modes work**: Grid, speaker, focus, overlay all functional
5. **Save states work**: Global save/load keeps all players in sync
6. **Game library works**: Can browse and launch games easily
7. **Testing passes**: Can play Mario Kart DS / Pokémon multiplayer on one machine

---

## References

- [mgba-splitscreen](https://github.com/Spuds0588/mgba-splitscreen) — Sister project, source of architectural patterns and feature inspiration
- [melonDS](https://github.com/melonDS-emu/melonDS) — Underlying emulator core
- [history.md](./history.md) — Detailed lessons learned from mgba-splitscreen

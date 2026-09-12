# melonDS Multiplayer

<p align="center">
  <img src="res/icon/melon_128x128.png" width="128">
</p>

<h2 align="center"><b>melonDS Multiplayer</b></h2>
<p align="center">
  <a href="https://github.com/Spuds0588/melonDS-Multiplayer" alt="GitHub Repository"><img src="https://img.shields.io/badge/GitHub-melonDS--Multiplayer-%23333.svg"></a>
  <a href="https://tauri.app/" alt="Built with Tauri"><img src="https://img.shields.io/badge/Built%20with-Tauri-FFC131?logo=tauri"></a>
</p>

**DS emulator, splitscreen multiplayer edition.**

A Tauri-based desktop application that runs multiple linked melonDS emulator instances side by side for local splitscreen multiplayer on a single machine. Up to 4 players can play multiplayer DS games (Mario Kart DS, Pokémon trades/battles, etc.) with each player getting their own screen and controls — all linked together via melonDS's virtual Wi-Fi.

> **Note:** This is a fork of the [melonDS](https://github.com/melonDS-emu/melonDS) emulator, adapted for splitscreen multiplayer functionality. Inspired by the success of [mgba-splitscreen](https://github.com/Spuds0588/mgba-splitscreen).

---

## About

melonDS Multiplayer brings couch co-op multiplayer to Nintendo DS emulation. The core use case: multiple players on one machine, each with their own screen and input controls, playing linked DS games together.

### Primary Goal: Local Splitscreen Multiplayer

The primary audience is players who want to play multiplayer DS games on a single computer with multiple controllers or keyboard setups:

- **2-4 players** on one machine, each with their own screen
- **Virtual link cable** via melonDS's Wi-Fi emulation for in-game connectivity
- **Per-player controls** — keyboard zones or multiple gamepads
- **Side-by-side display** with configurable layouts (grid, speaker, focus, overlay)

### Secondary Goal: Online Play (Future)

Online multiplayer for remote players is a potential future enhancement, not the core focus. When implemented, remote players would connect via WebRTC to see their assigned screen and send inputs back to the host — similar to how mgba-splitscreen's roadmap mentions a "hosted multiplayer" mode.

---

## Features

### Splitscreen Display
- Multiple melonDS instances displayed side by side
- Configurable view modes: Grid, Speaker (1 large + smalls), Focus (single), Overlay/PiP
- Each player gets their own screen real estate

### Virtual Link Cable
- melonDS's native Wi-Fi emulation for in-game multiplayer connectivity
- Perfect synchronization between instances (lockstep where applicable)
- Play trading, link battles, co-op, and competitive multiplayer games

### Controls
- Per-player keyboard layouts (fully remappable)
- Gamepad support (multiple controllers, one per player)
- Global hotkeys for turbo, save/load, pause (remappable)

### Game Management
- Game library with box art display
- Quick save/load for all players together (F5/F7)
- Battery save files auto-loaded next to ROMs

### Play Modes
- **Local splitscreen** (primary): Multiple players on one machine
- **Online** (future): Remote players via WebRTC (roadmap item)

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    melonDS Multiplayer (Tauri App)              │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                    Frontend (WebView/Canvas)             │   │
│  │  - Renders all player screens side by side              │   │
│  │  - View mode management (grid, speaker, focus, overlay)│   │
│  │  - Input handling (keyboard zones, gamepad routing)    │   │
│  │  - UI: Game library, settings, view controls           │   │
│  └────────────────────────────┬────────────────────────────┘   │
│                               │                                 │
│  ┌────────────────────────────┼────────────────────────────┐   │
│  │                    Backend (Rust)                        │   │
│  │  - Manages 1-4 melonDS instances                        │   │
│  │  - Routes inputs to specific player instance            │   │
│  │  - Frame loop synchronization                           │   │
│  │  - Save state management                                │   │
│  └────────────────────────────┬────────────────────────────┘   │
│                               │                                 │
│  ┌────────────────────────────┼────────────────────────────┐   │
│  │              melonDS Core (C++) Library                  │   │
│  │  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐      │   │
│  │  │ NDS #1  │ │ NDS #2  │ │ NDS #3  │ │ NDS #4  │      │   │
│  │  │ Player1 │ │ Player2 │ │ Player3 │ │ Player4 │      │   │
│  │  └────┬────┘ └────┬────┘ └────┬────┘ └────┬────┘      │   │
│  │       └───────────┼───────────┘                      │   │
│  │                   │                                   │   │
│  │    [Virtual Wi-Fi / Link Cable Emulation]            │   │
│  └────────────────────────────┬────────────────────────────┘   │
└───────────────────────────────┼─────────────────────────────────┘
                                │
                    [Optional: WebRTC for remote players]
                                │
                    [Future: Remote web clients]
```

---

## Requirements

### For Local Play (Primary Use Case)
- Windows, macOS, or Linux
- One computer with enough resources for 2-4 NDS instances
- 1-4 controllers OR keyboard (multiple input zones)
- Nintendo DS ROM files
- BIOS/firmware files (for firmware boot)

### For Remote Players (Future Feature)
- Any modern web browser
- Stable internet connection
- No installation required (when implemented)

---

## Controls Overview

Default keyboard layouts (fully remappable in-app):

### Player 1 (Left hand cluster)
| DS Button | Key |
|-----------|-----|
| D-Pad Up/Down/Left/Right | W/S/A/D |
| A | K |
| B | J |
| L | H |
| R | L |
| Start | Enter |
| Select | Backspace |

### Player 2 (Right hand / arrows cluster)
| DS Button | Key |
|-----------|-----|
| D-Pad | Arrow keys |
| A | M |
| B | N |
| L | V |
| R | B |
| Start | P |
| Select | O |

### Player 3 & 4
Defaults listed in-app Help; all keys remappable.

### Global Hotkeys (remappable)
| Action | Default |
|--------|---------|
| Turbo | Q |
| Quick save (all players) | F5 |
| Quick load (all players) | F7 |
| Pause/resume (all players) | Escape |
| Cycle view mode | F8 |
| Cycle focus player | F9 |

---

## Building from Source

See [BUILD.md](./BUILD.md) for detailed build instructions.

---

## Lessons from mgba-splitscreen

This project follows the successful patterns established by [mgba-splitscreen](https://github.com/Spuds0588/mgba-splitscreen):

1. **Primary focus is local splitscreen** — online is a bonus, not the core
2. **Use the emulator's native link cable** — don't reinvent sync; use melonDS's Wi-Fi
3. **Per-player controls with remapping** — accessibility for all players
4. **View modes matter** — grid, speaker, focus, overlay/PiP for different setups
5. **Global save/load** — saves sync across all players
6. **Game library with box art** — smooth user experience
7. **Tauri v2 + WebSocket** — clean architecture, same frontend works in browser
8. **Keep the web client simple** — thin client, emulation on host

See [history.md](./history.md) for more detailed lessons learned and implementation guidance.

---

## Project Status

This project is currently in development. See [to-do.md](./to-do.md) for the development roadmap.

---

## Credits

Based on [melonDS](https://github.com/melonDS-emu/melonDS) by Arisotura.

- Martin for GBAtek, a good piece of documentation
- Cydrak for the extra 3D GPU research
- limittox for the icon
- All of you comrades who have been testing melonDS, reporting issues, suggesting shit, etc

Inspired by the approach of [mgba-splitscreen](https://github.com/Spuds0588/mgba-splitscreen).

## License

This project incorporates code from melonDS which is licensed under the GNU GPL v3. See the [LICENSE](./LICENSE) file for details.

### External
* Images used in the Input Config Dialog - see `src/frontend/qt_sdl/InputConfig/resources/LICENSE.md`

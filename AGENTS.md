# AGENTS.md - AI Agent Guidelines for melonDS Multiplayer

This document provides guidance for AI coding agents working on the melonDS Multiplayer project.

## Project Overview

**melonDS Multiplayer** is a Tauri-based desktop application that enables **local splitscreen multiplayer** for Nintendo DS games using a thin-client architecture. The app runs multiple melonDS emulator instances side by side on a single machine, with each player getting their own screen and controls — all linked via melonDS's virtual Wi-Fi.

> **Primary Goal:** Local splitscreen couch co-op multiplayer (2-4 players on one machine)
> **Secondary Goal:** Online play for remote players (future enhancement, not core)

## Project Structure

```
.
├── README.md              # Project overview and documentation
├── AGENTS.md              # This file - AI agent guidelines
├── to-do.md               # Development roadmap and tasks
├── history.md             # Project history, lessons from mgba-splitscreen
├── BUILD.md               # Build instructions (update as project evolves)
├── PRD-melonDS-multiplayer.md  # Product Requirements Document
├── LICENSE                # GNU GPL v3 (inherited from melonDS)
├── .github/               # GitHub workflows and configuration
├── src/                   # Source code (melonDS core)
├── res/                   # Resources (icons, etc.)
├── freebios/              # BIOS generation tools
├── cmake/                 # CMake configuration
├── tools/                 # Build tools
├── melonDLDI/             # DLDI patch tool
└── .freebuff/             # Freebuff project metadata
```

## Key Characteristics

1. **Fork Relationship**: This is a fork of [melonDS](https://github.com/melonDS-emu/melonDS), adapted for splitscreen multiplayer functionality
2. **Technology Stack**: 
   - Host: Tauri (Rust + WebView) + melonDS C++ core
   - Display: Canvas-based rendering of multiple NDS screens side by side
   - Future Online: WebRTC for remote web clients (not primary focus)
3. **Architecture**: Thin-client model where emulation happens on the host, display and input on the frontend

## Primary Use Case: Local Splitscreen

The main user scenario is:
- Multiple players (2-4) gathered around one computer
- Each player has their own keyboard zone or gamepad
- Screens displayed side by side in configurable layouts
- Games linked via melonDS's virtual Wi-Fi for in-game multiplayer

## Code Conventions

- **C++ Code**: Follows melonDS conventions (4-space indentation, PascalCase for classes/functions, camelCase for parameters)
- **Rust Code**: Standard Rust conventions (snake_case, 4-space indentation)
- **Web Code**: Standard web conventions

See [CONTRIBUTING.md](./CONTRIBUTING.md) for detailed C++ style guidelines from the original melonDS project.

## Important Considerations

### DO:
- **Prioritize local splitscreen over online features** — this is the core use case
- Use melonDS's native Wi-Fi emulation for link cable functionality (don't reinvent sync)
- Implement per-player controls with full remapping support
- Add view modes (grid, speaker, focus, overlay) for different setups
- Keep saves synchronized across all players
- Build a game library with box art display
- Maintain compatibility with the original melonDS build system where possible

### DON'T:
- Build online/WebRTC features before local splitscreen works
- Modify the core emulator without understanding implications for multiplayer
- Add complexity for "remote players" when the primary audience is local
- Remove essential melonDS functionality without good reason
- Commit BIOS/ROM files (legal considerations)

## Development Phases

Refer to [to-do.md](./to-do.md) for the current development roadmap. The phases are:

1. **Phase 1**: Native Bridge (Host Infrastructure) - Tauri setup, melonDS integration, framebuffer piping
2. **Phase 2**: Splitscreen Display & Controls - Side-by-side rendering, per-player input, view modes, gamepad support
3. **Phase 3**: Game Library & UX - ROM management, box art, save states, pause menu
4. **Phase 4**: Polish & Testing - Testing with actual multiplayer games, bug fixes, performance
5. **Phase 5**: Online Play (Optional/Future) - WebRTC for remote players if desired

## Lessons from mgba-splitscreen

For implementation guidance, see [history.md](./history.md) which documents lessons learned from the sister project [mgba-splitscreen](https://github.com/Spuds0588/mgba-splitscreen):

- Start with local splitscreen as the primary focus
- Use the emulator's native link cable (melonDS Wi-Fi) — don't build custom sync
- Per-player controls must be fully remappable
- View modes (grid, speaker, focus, overlay/PiP) are essential for different setups
- Global save/load syncs across all players
- Game library with box art improves UX significantly
- Tauri v2 + WebSocket is a clean architecture
- The same frontend can work in browser and desktop app

## Git Workflow

- Branch from `master` for new features
- Keep commits focused and descriptive
- Update documentation when making significant changes
- This repo has `upstream` pointing to the original melonDS repo

## Communication

- For questions about melonDS core: Refer to original melonDS project channels
- For project-specific questions: Use GitHub Issues
- Discord: TBD

## Legal Notes

- This project incorporates melonDS code (GPL v3)
- BIOS/ROM files must be provided by users (do not include in repo)
- Respect intellectual property rights for Nintendo DS games

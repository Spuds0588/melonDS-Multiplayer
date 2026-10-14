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
├── compatibility.md       # Per-game multiplayer compatibility list + evidence
├── PRD-melonDS-multiplayer.md  # Product Requirements Document
├── LICENSE                # GNU GPL v3 (inherited from melonDS)
├── .github/               # GitHub workflows and configuration
├── src/                   # Source code (melonDS core)
├── res/                   # Resources (icons, etc.)
├── freebios/              # BIOS generation tools
├── cmake/                 # CMake configuration
├── tools/                 # Build tools (incl. clean_repo.sh disk cleanup)
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
- **Attach game-specific behavior through the per-game registry, not env vars.**
  Game identity has one owner: `src-tauri/src/game_profile.rs` maps the
  cartridge's 4-byte game code (read by the bridge's `md_rom_game_code`) to a
  `GameFixSpec`. A title-specific patch is a new registry entry plus the code at
  its named attach point; the single Settings switch (`per_game_fixes`) gates
  it. Do not branch on a ROM path or add another `MD_*` knob for a per-game
  behavior.
- **Prioritize local splitscreen over online features** — this is the core use case
- **Stability and playability outrank emulation accuracy.** The product goal is
  the widest game set, with 4 players, on the least hardware that can work — so a
  measured, documented departure from stock melonDS accuracy is allowed when it
  buys that (see the trade-off section at the top of to-do.md). Never at the cost
  of silently losing data a game is waiting for, or of players' saves diverging
- **Single-card Download Play is core compatibility work, not an edge case**
  (Milestone 4 of to-do.md). One cartridge serving four consoles is where the
  widest 4-player game set lives, and the three joined consoles run a small
  downloaded image instead of a full cartridge — the cheapest four-player
  configuration to emulate
- **Weigh design decisions against the hardware rule** in to-do.md (4 players on
  the weakest machine that can be made to work, not on the dev laptop): work that
  scales with the number of consoles — rendering, texture uploads, audio mixing,
  screen composition — has to justify itself, and spending accuracy to buy frames
  is allowed
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

The current focus is at the top of [to-do.md](./to-do.md):
["The road to a working 2, 3 and 4-player link"](./to-do.md#the-road-to-a-working-2-3-and-4-player-link).
Per-game results live in [compatibility.md](./compatibility.md).

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

## Repo & Disk Hygiene

This checkout collects gigabytes of regenerable debris (11 GB `src-tauri/target/`
alone at one point, plus hundreds of MB of run logs and frame captures). Keeping
the dev environment clean is **part of doing the work, not a separate chore**: a
bloated checkout slows builds, fills the disk, and makes the working tree hard to
read. Leave the tree lighter than you found it.

**Cleaning up is part of wrapping up every task.** Before you call a task done:

1. Measure what is actually there — `du -sh src-tauri/target build-tauri logs
   game-captures`, `df -h /`, `free -h`, and `pgrep` for any app/driver/session
   you started. Shut down test sessions aggressively: quit the app
   (`python3 tools/appctl.py quit`) the moment a live session is no longer
   needed — do not leave it running "in case" while you read code, disassemble
dumps, or write docs. A running 2-4 console session eats CPU/RAM the build
   and the next session need.
2. Run the script; apply it when it frees meaningful space or when disk is
elevated (`df` above ~85%):

   ```bash
   tools/clean_repo.sh          # dry run — shows what would go and how much it frees
   tools/clean_repo.sh --apply  # delete it
   ```

3. Remove your own scratch: throwaway probe/driver scripts and disassembly
   helpers (in `tools/scripts/` and outside the repo, e.g. `/tmp/*.sh`, `/tmp/*.py`),
   one-off binary dumps you are not citing, and half-written outputs of failed
   commands. `logs/` and `game-captures/` are for evidence you actually cite —
   un-cited question-of-the-moment dumps should go.
4. Confirm nothing necessary was lost: uncommitted source still present (`git
   status`), no doc-cited evidence deleted, no ROMs/saves touched.
5. Say in your wrap-up what you freed, or that there was nothing to free.

Run the script periodically too — at minimum at the start of a new session,
before any big build, and whenever disk pressure shows up (slow builds, `df`
above ~85%, obviously bloated checkouts).

The script only deletes rebuildable or superseded artifacts:
- `src-tauri/target/`, `build-tauri/`, `__pycache__/` — regenerable build output
  and caches (cargo/CMake/object files and their downloaded deps included)
- `logs/` older than `LOG_DAYS` (default 14) — run logs from `tools/run_app.sh`
- `game-captures/` sets older than `KEEP_DAYS` (default 14), except sets cited
  as evidence in history.md / to-do.md / compatibility.md / BUILD.md
- `game-captures/sav-backups/` keeps only the newest backup per base .sav

It never touches git-tracked files, `Test Roms/`, ROMs/saves outside that, or
anything under `src/`, `tools/`, or the docs. If you delete evidence manually,
update the docs that reference it in the same change.

Two boundaries worth stating because over-eager cleanup is worse than none:
- **Never delete uncommitted work.** This tree routinely carries large,
  uncommitted changes and new files from earlier passes (`src-tauri/src/*.rs`,
  `tools/scripts/*`, doc edits). A clean environment is not a clean `git status`
  — if it is not rebuildable build output or your own scratch, leave it alone.
- **Deleting build output means the next build starts cold.** `cargo build
  --release --manifest-path src-tauri/Cargo.toml` and the `cmake` line in
  [BUILD.md](./BUILD.md) bring it back; budget for that instead of assuming a
  binary is lying around.

## Git Workflow

- Branch from `master` for new features
- Keep commits focused and descriptive
- Update documentation when making significant changes
- This repo has `upstream` pointing to the original melonDS repo

## Communication

- Status updates are succinct and bullet-pointed: what you found, what it
  means, what you will do next. No play-by-play, no status-line chatter.
  One short prose line plus bullets beats a paragraph every time.

- For questions about melonDS core: Refer to original melonDS project channels
- For project-specific questions: Use GitHub Issues
- Discord: TBD

## Legal Notes

- This project incorporates melonDS code (GPL v3)
- BIOS/ROM files must be provided by users (do not include in repo)
- Respect intellectual property rights for Nintendo DS games

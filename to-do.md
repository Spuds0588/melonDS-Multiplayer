# melonDS Multiplayer - Development Roadmap

This document tracks the development progress and planned features for melonDS Multiplayer.

> **Primary Goal:** Local splitscreen multiplayer (2-4 players on one machine, couch co-op)
> **Secondary Goal:** Online play for remote players (future enhancement)

## Project Status: In Development

---

## Phase 1: Native Bridge (Host Infrastructure)

### Todo
- [ ] Set up Tauri v2 + Rust project structure
- [ ] Integrate melonDS as a C++ library (static build for the core)
- [ ] Implement framebuffer piping from melonDS to frontend (WebSocket or shared memory)
- [ ] Target: Render single NDS screen in Tauri webview at 60FPS

### In Progress
- [ ] 

### Done
- [x] Fork melonDS repository
- [x] Update project documentation (README, AGENTS.md, etc.)
- [x] Configure git remotes (origin: this repo, upstream: melonDS)
- [x] Review mgba-splitscreen for lessons learned

---

## Phase 2: Splitscreen Display & Controls

### Todo
- [ ] Render multiple NDS screens side by side (2-4 instances)
- [ ] Implement view modes:
  - Grid (all screens equal size)
  - Speaker (1 large + smalls)
  - Focus (single screen, full size)
  - Overlay/PiP (picture-in-picture)
- [ ] Implement per-player keyboard layouts (remappable)
- [ ] Implement gamepad support (auto-assign controllers to players)
- [ ] Implement global hotkeys (turbo, save/load, pause) - remappable
- [ ] Cycle view mode hotkey (F8/F9)
- [ ] Deep link support via URL parameters (?players=N)

### In Progress
- [ ] 

### Done
- [ ] 

---

## Phase 3: Game Library & UX

### Todo
- [ ] Build game library UI with box art display
- [ ] Implement ROM folder scanning
- [ ] Add persistent recent games list
- [ ] Implement quick save (F5) / quick load (F7) for all players together
- [ ] Auto-load battery save files (.sav) next to ROMs
- [ ] Implement pause menu (Escape) with controller-navigable options
- [ ] Add turbo mode (Q key fast-forward)
- [ ] Save import/export per instance or as a set
- [ ] Per-player button remapping UI

### In Progress
- [ ] 

### Done
- [ ] 

---

## Phase 4: Polish & Testing

### Todo
- [ ] Test with actual multiplayer games:
  - Mario Kart DS (Wi-Fi multiplayer)
  - Pokémon games (trading/battling)
  - Other multiplayer DS titles
- [ ] Performance testing with 2-4 instances
- [ ] Memory/CPU usage optimization
- [ ] Bug fixes from testing
- [ ] User documentation / help screens
- [ ] Beta release preparation

### In Progress
- [ ] 

### Done
- [ ] 

---

## Phase 5: Online Play (Optional/Future)

*This phase is optional and not the core focus. Only pursue if there's demand.*

### Todo
- [ ] Research WebRTC implementation for remote players
- [ ] Build simple web client for remote viewing
- [ ] Implement frame streaming to remote clients
- [ ] Input routing back from remote players
- [ ] Connection management UI
- [ ] Testing with remote players

### In Progress
- [ ] 

### Done
- [ ] 

---

## Future Enhancements (Phase 6+)

### Additional View Modes
- [ ] Custom layouts (user-defined positions/sizes)
- [ ] Background image support
- [ ] Per-player outline toggles
- [ ] Debug log toggle

### Platform Support
- [ ] Android (tablets, Chromebooks, Android TV) - thin Tauri shell around WASM
- [ ] Web version (optional - same frontend in browser)

### Advanced Features
- [ ] Netplay-style synchronization for online (if Phase 5 pursued)
- [ ] Spectator mode
- [ ] Replay/recording functionality

---

## Notes

### Technical Considerations

**Performance:**
- Running 2-4 NDS instances simultaneously requires significant CPU/GPU resources
- Frame pacing and synchronization between instances is critical
- Consider hardware requirements for different player counts

**Controls:**
- Keyboard zones work well for 2 players (left/right hand)
- Gamepad support essential for 3-4 players
- Per-player remapping is a must-have for accessibility

**Synchronization:**
- Use melonDS's native Wi-Fi emulation for link cable (don't build custom)
- Lockstep synchronization where applicable keeps instances in perfect sync
- Virtual Wi-Fi should handle the in-game networking

**View Modes (from mgba-splitscreen lessons):**
- Grid: Best for 2-4 players with equal screen needs
- Speaker: Good when one player needs larger view (competitive games)
- Focus: Single player view, useful for watching or solo play
- Overlay/PiP: Compact view, good for small screens

### Testing Games
- Mario Kart DS (Wi-Fi multiplayer races)
- Pokémon Diamond/Pearl/Platinum (trading/battling)
- Mario Party DS
- Other multiplayer NDS titles with Wi-Fi support

### Resource Files
- BIOS/firmware must be provided by users (not included in repo)
- Legal ROMs must be provided by users (not included in repo)

---

## References

- [PRD-melonDS-multiplayer.md](./PRD-melonDS-multiplayer.md) - Product Requirements Document
- [README.md](./README.md) - Project overview
- [BUILD.md](./BUILD.md) - Build instructions
- [history.md](./history.md) - Project history and lessons from mgba-splitscreen

---

## Lessons from mgba-splitscreen (Quick Reference)

1. **Start with local splitscreen** - Don't build online first
2. **Use native link cable** - melonDS Wi-Fi, not custom sync
3. **Per-player controls** - Fully remappable keyboard + gamepad
4. **View modes matter** - Grid, speaker, focus, overlay for different setups
5. **Global save/load** - Sync saves across all players
6. **Game library** - Box art + recent games = good UX
7. **Tauri v2 + WebSocket** - Clean architecture, frontend works in browser too
8. **Keep it simple** - Thin client, emulation on host

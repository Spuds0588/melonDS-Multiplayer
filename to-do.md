# melonDS Multiplayer - Development Roadmap

This document tracks the development progress and planned features for melonDS Multiplayer.

> **Primary Goal:** Local splitscreen multiplayer (2-4 players on one machine, couch co-op)
> **Secondary Goal:** Online play for remote players (future enhancement)

## Project Status: In Development

---

## Phase 1: Native Bridge (Host Infrastructure)

### Todo
- [ ] Wire the Rust side of the Tauri app to the bridge (bindgen or hand-written FFI)
- [ ] Implement framebuffer piping from the bridge to the webview (canvas per player)
- [ ] Target: Render a single NDS screen in the Tauri webview at 60FPS
- [ ] Decide the emulator-thread model (one thread per instance, pinning, frame pacing)

### In Progress
- [ ] Tauri v2 scaffolding exists but is not yet connected to the bridge

### Done
- [x] Fork melonDS repository
- [x] Update project documentation (README, AGENTS.md, etc.)
- [x] Configure git remotes (origin: this repo, upstream: melonDS)
- [x] Review mgba-splitscreen for lessons learned
- [x] Build the melonDS core with the Qt/SDL frontend switched off (`-DBUILD_QT_SDL=OFF`)
- [x] Write the C bridge (`src/frontend/tauri/melonds_bridge.h`): lifecycle, ROM
      loading, per-instance input, per-frame stepping, savestates, screen readback
- [x] Link the bridge against the core as a static library
- [x] Create a diagnostic NDS ROM from scratch (`tools/mkdiagrom`), since no test
      ROM exists and no ARM toolchain is guaranteed
- [x] Headless smoke test (`tools/coretest`) booting that ROM through the bridge:
      36 checks covering render, animation, determinism, input routing, savestates
- [x] Document the supported build path in BUILD.md

### Not yet done, and known to be missing
- [ ] The Rust frontend does not call the bridge yet
- [ ] No multiplayer link between instances: `md_link_*` assigns slots, but the
      local Wi-Fi bus itself is not wired up yet (see history.md)

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

# melonDS Multiplayer - Development Roadmap

This document tracks the development progress and planned features for melonDS Multiplayer.

## Project Status: In Development

## Phase 1: Native Bridge (Host Infrastructure)

### Todo
- [ ] Set up Tauri + Rust project structure
- [ ] Integrate melonDS headless core (C/C++ bindings via Rust)
- [ ] Implement Rust-to-Frontend framebuffer piping (WebSockets/Shared Memory)
- [ ] Target: 60FPS canvas rendering in Tauri webview

### In Progress
- [ ] 

### Done
- [x] Fork melonDS repository
- [x] Update project documentation (README, AGENTS.md, etc.)
- [x] Configure git remotes (origin: this repo, upstream: melonDS)

---

## Phase 2: Host Networking & UI

### Todo
- [ ] Build Host settings UI (file pickers for BIOS, Firmware, ROMs)
- [ ] Integrate PeerJS into Tauri frontend
- [ ] Generate Peer ID and host connections
- [ ] Bind WebRTC `call()` to canvas `captureStream()` for remote players

### In Progress
- [ ] 

### Done
- [ ] 

---

## Phase 3: The Web Client

### Todo
- [ ] Build standalone web client (HTML/JS/CSS)
- [ ] Host web client on GitHub Pages
- [ ] Implement URL parsing (`?host=ID`)
- [ ] Implement PeerJS client connection
- [ ] Build Touch-Math algorithm:
  - Capture mouse events on video player
  - Calculate aspect ratio scaling
  - Determine if click falls within bottom screen's 256x192 bounding box
- [ ] Map keyboard inputs (D-Pad, A, B, X, Y, L, R, Start, Select, 'M' for Mic)
- [ ] Serialize inputs over WebRTC DataChannels

### In Progress
- [ ] 

### Done
- [ ] 

---

## Phase 4: Integration & Virtual Wi-Fi

### Todo
- [ ] Ensure inputs received by Host's Tauri frontend are passed to specific melonDS C++ instance
- [ ] Configure melonDS instances to bridge their virtual local Wi-Fi
- [ ] Enable in-game multiplayer lobbies to see each other
- [ ] Testing with actual multiplayer games (Mario Kart DS, Pokémon, etc.)

### In Progress
- [ ] 

### Done
- [ ] 

---

## Future Enhancements (Phase 5+)

### Save State & SRAM Features
- [ ] Phase 1: Saves retained solely by Host (baseline)
- [ ] Phase 2: Remote players can request export of their `.sav` file via WebRTC

### Additional Features
- [ ] Microphone support (keyboard key 'M' toggles mic active flag)
- [ ] Configurable player slots (2-4 players)
- [ ] ROM list management UI
- [ ] Connection quality indicators
- [ ] Chat or lobby system

---

## Notes

### Technical Considerations
- Framebuffer piping needs to be efficient for 60FPS
- WebRTC latency should be minimized for playable experience
- Virtual Wi-Fi routing must maintain game compatibility
- Multiple emulator instances will require significant system resources

### Testing Games
- Mario Kart DS (Wi-Fi multiplayer)
- Pokémon games with trading/battling
- Other multiplayer NDS titles

### Resource Files
- BIOS/firmware must be provided by users (not included in repo)
- Legal ROMs must be provided by users (not included in repo)

---

## References

- [PRD- melonDS-multiplayer.md](./PRD- melonDS-multiplayer.md) - Product Requirements Document
- [README.md](./README.md) - Project overview
- [BUILD.md](./BUILD.md) - Build instructions

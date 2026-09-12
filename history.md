# melonDS Multiplayer - Project History

This document tracks the history and evolution of the melonDS Multiplayer project.

---

## Project Origins

**Date**: September 2026

**Context**: melonDS Multiplayer is a fork of the [melonDS](https://github.com/melonDS-emu/melonDS) Nintendo DS emulator project, adapted for online multiplayer gaming using a thin-client architecture.

### Initial Fork
- Forked from melonDS-emu/melonDS repository
- Original melonDS is a mature DS emulator focused on accuracy and performance
- This fork redirects the project toward multiplayer functionality using Tauri

### Project Goals
- Enable seamless online multiplayer for Nintendo DS games
- Use a thin-client architecture: emulation on host, web browsers as clients
- Leverage WebRTC for low-latency video streaming
- Maintain compatibility with melonDS's virtual Wi-Fi for in-game networking

---

## Version History

### v0.1.0 - Initial Development Setup (September 2026)

**Status**: Initial project configuration

**Changes**:
- Forked melonDS repository to melonDS-Multiplayer
- Updated README.md with project-specific information
- Created AGENTS.md for AI coding agent guidance
- Created to-do.md with development roadmap
- Created history.md (this file)
- Updated project metadata to reflect new purpose

**Technological Direction**:
- Host application: Tauri (Rust + WebView)
- Emulator core: melonDS C++ code
- Remote clients: Web browsers via WebRTC
- Networking: PeerJS for WebRTC signaling

---

## Relationship to Original melonDS

melonDS Multiplayer is a derivative work based on melonDS. Key points:

1. **License**: Inherits GNU GPL v3 from melonDS
2. **Code Base**: Uses melonDS emulator core
3. **Architecture**: Adds Tauri wrapper and WebRTC layer on top
4. **Independence**: Separate project with different goals

### Upstream Relationship
- `upstream` remote: https://github.com/melonDS-emu/melonDS.git
- Track upstream for emulator core updates
- Adapt changes as needed for multiplayer architecture

### Credits
Based on melonDS by Arisotura (https://github.com/melonDS-emu/melonDS)

Additional credits from original melonDS:
- Martin for GBAtek documentation
- Cydrak for 3D GPU research
- limittox for the icon
- Community testers and contributors

---

## Future Milestones

### Near-term Goals
1. Complete Tauri project setup
2. Integrate melonDS as a library in the Tauri app
3. Implement framebuffer capture and piping

### Medium-term Goals
1. Build and test web client
2. Implement WebRTC connectivity
3. Test with actual multiplayer games

### Long-term Goals
1. Public beta release
2. Support for 2-4 player sessions
3. Additional features (microphone, save export, etc.)

---

## Document History

| Date | Version | Author | Changes |
|------|---------|--------|---------|
| 2026-09-11 | 0.1.0 | Project Initiation | Initial documentation created |

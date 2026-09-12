# AGENTS.md - AI Agent Guidelines for melonDS Multiplayer

This document provides guidance for AI coding agents working on the melonDS Multiplayer project.

## Project Overview

**melonDS Multiplayer** is a Tauri-based desktop application that enables online multiplayer for Nintendo DS games using a thin-client architecture. The host runs multiple melonDS emulator instances natively, while remote players connect via web browsers through WebRTC.

## Project Structure

```
.
├── README.md              # Project overview and documentation
├── AGENTS.md              # This file - AI agent guidelines
├── to-do.md               # Development roadmap and tasks
├── history.md             # Project history and changelog
├── BUILD.md               # Build instructions (update as project evolves)
├── PRD- melonDS-multiplayer.md  # Product Requirements Document
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

1. **Fork Relationship**: This is a fork of [melonDS](https://github.com/melonDS-emu/melonDS), adapted for multiplayer functionality
2. **Technology Stack**: 
   - Host: Tauri (Rust + WebView) + melonDS C++ core
   - Web Client: HTML/CSS/JavaScript with WebRTC
3. **Architecture**: Thin-client model where emulation happens on the host

## Code Conventions

- **C++ Code**: Follows melonDS conventions (4-space indentation, PascalCase for classes/functions, camelCase for parameters)
- **Rust Code**: Standard Rust conventions (snake_case, 4-space indentation)
- **Web Code**: Standard web conventions

See [CONTRIBUTING.md](./CONTRIBUTING.md) for detailed C++ style guidelines from the original melonDS project.

## Important Considerations

### DO:
- Maintain separation between host app logic and emulator core
- Keep the web client simple (thin client architecture)
- Document changes to the PRD as features are implemented
- Update to-do.md when completing significant tasks
- Preserve compatibility with the original melonDS build system where possible

### DON'T:
- Modify the core emulator without understanding implications for multiplayer
- Add features that increase complexity for remote players (they're thin clients)
- Remove essential melonDS functionality without good reason
- Commit BIOS/ROM files (legal considerations)

## Development Phases

Refer to [to-do.md](./to-do.md) for the current development roadmap. The PRD outlines these main phases:

1. **Phase 1**: Native Bridge (Host Infrastructure) - Tauri setup, melonDS integration
2. **Phase 2**: Host Networking & UI - Settings UI, PeerJS integration
3. **Phase 3**: Web Client - Standalone web client for remote players
4. **Phase 4**: Integration & Virtual Wi-Fi - Full integration and testing

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

# melonDS Multiplayer

<p align="center">
  <img src="res/icon/melon_128x128.png" width="128">
</p>

<h2 align="center"><b>melonDS Multiplayer</b></h2>
<p align="center">
  <a href="https://github.com/Spuds0588/melonDS-Multiplayer" alt="GitHub Repository"><img src="https://img.shields.io/badge/GitHub-melonDS--Multiplayer-%23333.svg"></a>
  <a href="https://tauri.app/" alt="Built with Tauri"><img src="https://img.shields.io/badge/Built%20with-Tauri-FFC131?logo=tauri"></a>
</p>

**DS emulator, multiplayer edition.**

A Tauri-based desktop application that runs multiple linked melonDS emulator instances for a splitscreen multiplayer experience. Remote players connect via a web browser through WebRTC, while the host handles all emulation in native code.

> **Note:** This is a fork of the [melonDS](https://github.com/melonDS-emu/melonDS) emulator, adapted for multiplayer functionality using a thin-client architecture. This is a separate project from the main melonDS emulator.

---

## About

melonDS Multiplayer is a project that adapts the melonDS Nintendo DS emulator for online multiplayer gaming. It uses a "thin client" architecture where:

- **Host (Tauri App):** Runs the native desktop application that handles all emulation and virtual Wi-Fi routing
- **Web Clients:** Remote players join via a simple web URL and receive low-latency WebRTC video streams

The goal is to enable seamless, zero-desync local multiplayer over the internet for games like Mario Kart DS, Pokémon, and other multiplayer NDS titles.

## Features

- Native desktop application built with Tauri (Rust + Webview)
- Multiple simultaneous melonDS emulator instances
- Virtual Wi-Fi networking for in-game multiplayer
- WebRTC streaming for remote players
- Zero-install for remote players (just a web browser)
- Keyboard/mouse input mapping for touch screens
- Save state support on the host

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      HOST (Tauri App)                       │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐    │
│  │ MelonDS #1   │  │ MelonDS #2   │  │ MelonDS #N   │    │
│  │ (NDS #1)     │  │ (NDS #2)     │  │ (NDS #N)     │    │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘    │
│         │                 │                 │             │
│         └─────────────────┼─────────────────┘             │
│                           │                                 │
│                    [Virtual Wi-Fi Routing]                  │
│                           │                                 │
│         ┌─────────────────┼─────────────────┐             │
│         │       Frontend (WebView)          │             │
│         │   - WebRTC Gateway                │             │
│         │   - Framebuffer rendering         │             │
│         │   - Input routing                 │             │
│         └──────────────┬────────────────────┘             │
└────────────────────────┼───────────────────────────────────┘
                         │
              ┌──────────┴──────────┐
              │   WebRTC (P2P)      │
              └──────────┬──────────┘
                         │
        ┌────────────────┼────────────────┐
        │                │                │
   [Player 2]      [Player 3]      [Player N]
   Web Browser     Web Browser     Web Browser
   (Thin Client)   (Thin Client)   (Thin Client)
```

## Requirements

### For Host
- Windows, macOS, or Linux
- Rust (for building from source)
- Nintendo DS ROM files
- BIOS/firmware files (for firmware boot)

### For Remote Players
- Any modern web browser
- Stable internet connection
- No installation required

## Building from Source

See [BUILD.md](./BUILD.md) for detailed build instructions.

## Project Status

This project is currently in development. See [to-do.md](./to-do.md) for the development roadmap.

## Credits

Based on [melonDS](https://github.com/melonDS-emu/melonDS) by Arisotura.

- Martin for GBAtek, a good piece of documentation
- Cydrak for the extra 3D GPU research
- limittox for the icon
- All of you comrades who have been testing melonDS, reporting issues, suggesting shit, etc

## License

This project incorporates code from melonDS which is licensed under the GNU GPL v3. See the [LICENSE](./LICENSE) file for details.

### External
* Images used in the Input Config Dialog - see `src/frontend/qt_sdl/InputConfig/resources/LICENSE.md`

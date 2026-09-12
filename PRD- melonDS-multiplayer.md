# Product Requirements Document (PRD)
**Project:** MelonDS-Multiplayer (Thin Client Architecture)
**Platform:** Tauri/Rust (Desktop Host App) + Web Browser (Thin Client)
**Core Concept:** A native desktop application that emulates multiple NDS instances and uses WebRTC to stream individual player screens to remote web-based clients for seamless, zero-desync local multiplayer over the internet.

## 1. Overview & Objectives
**The "What":** A platform for playing NDS multiplayer games (e.g., Mario Kart DS, Pokémon) over the internet. The system utilizes a "Thin Client" architecture. The Host runs a native desktop application (Tauri) that handles all emulation and virtual Wi-Fi routing. Remote players join via a simple web URL and receive a low-latency WebRTC video stream of their specific screen, sending back inputs.
**The "Why":** NDS emulation is highly demanding. Running multiple instances in a browser (WASM) is impractical for most users. By offloading emulation to a native desktop Host, we utilize hardware acceleration and multithreading, ensuring perfect cycle accuracy and network sync via MelonDS's virtual local Wi-Fi, while keeping the barrier to entry for remote players at zero (just a web browser).

## 2. System Architecture

### 2.1 The Host (Tauri Desktop App)
*   **Backend (Rust/C++):** 
    *   Manages multiple instances of the MelonDS core natively.
    *   Routes virtual Wi-Fi (Ni-Fi) packets between the local instances so the games think they are physically next to each other.
    *   Pipes raw video/audio framebuffers to the frontend via local WebSockets or Shared Memory.
*   **Frontend (WebView/JS):** 
    *   Acts as the WebRTC gateway. Uses PeerJS to generate a Magic Link.
    *   Renders the Rust framebuffers to hidden `<canvas>` elements and captures them via `captureStream(60)`.
    *   Receives incoming WebRTC DataChannel inputs (keys/touch) from remote clients and passes them to Rust.

### 2.2 The Web Client (Browser-based)
*   **Role:** Join-only. Acts as a "dumb terminal".
*   **Capabilities:** 
    *   Connects to Host via Magic Link URL `?host=[PEER_ID]`.
    *   Receives and displays a standard `<video>` stream of their assigned NDS instance.
    *   Captures keyboard inputs and translates mouse clicks/taps on the lower half of the video into mapped NDS touch coordinates (X/Y relative to 256x192).
    *   Sends inputs via WebRTC DataChannel back to the Host.

## 3. Core Features

### 3.1 Dual-Screen Layout & Touch Support
*   **Display:** The Web Client receives a single video stream stacked vertically (Top Screen + Bottom Screen).
*   **Touch Translation:** JavaScript event listeners on the video element detect clicks. The app mathematically scales the click coordinates to match the emulated bottom screen and sends them to the Host.

### 3.2 Simplified "Faked" Microphone
*   **What:** Bypassing actual WebRTC microphone streams to save bandwidth and complexity.
*   **How:** A dedicated keyboard key (e.g., 'M') sends a "Mic Active" flag to the Host, triggering MelonDS's native microphone noise injection (essential for games like Zelda: Phantom Hourglass).

### 3.3 Seamless BIOS & ROM Management
*   **Host Only:** Only the Host needs to provide the NDS ROM file and the required BIOS/Firmware files. 
*   **Persistence:** The Tauri app stores BIOS paths in the native OS configuration directory. 

### 3.4 Save State & In-Game Saves (SRAM)
*   All save files (`.sav` and states) live on the Host machine. 
*   **Phase 1:** Saves are retained solely by the Host. 
*   **Phase 2 (Future):** Remote players can request an export of their specific instance's `.sav` file via WebRTC to keep their personal progress (e.g., Pokémon).

## 4. Development Implementation Plan

### Phase 1: The Native Bridge (Host Infrastructure)
- [ ] Setup Tauri + Rust project.
- [ ] Integrate a MelonDS headless core (C/C++ bindings via Rust `cc` or pre-compiled DLLs).
- [ ] Implement Rust-to-Frontend framebuffer piping (WebSockets/Shared Memory) to achieve 60FPS canvas rendering in the Tauri webview.

### Phase 2: Host Networking & UI
- [ ] Build the Host settings UI (File pickers for BIOS, Firmware, ROMs).
- [ ] Integrate PeerJS into the Tauri frontend. Generate Peer ID and host connections.
- [ ] Bind WebRTC `call()` to the canvas `captureStream()` for remote players.

### Phase 3: The Web Client
- [ ] Build the standalone web client (HTML/JS/CSS) hosted on GitHub Pages.
- [ ] Implement URL parsing `?host=ID` and PeerJS client connection.
- [ ] Build the Touch-Math algorithm: capture mouse events on the video player, calculate aspect ratio scaling, and determine if the click falls within the bottom screen's 256x192 bounding box.
- [ ] Map keyboard inputs (D-Pad, A, B, X, Y, L, R, Start, Select, 'M' for Mic) and serialize them over WebRTC DataChannels.

### Phase 4: Integration & Virtual Wi-Fi
- [ ] Ensure inputs received by the Host's Tauri frontend are successfully passed down to the specific MelonDS C++ instance.
- [ ] Configure MelonDS instances to bridge their virtual local Wi-Fi, enabling in-game multiplayer lobbies to see each other.
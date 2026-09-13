// melonDS Multiplayer - Tauri Backend
// This module provides the Rust backend for the splitscreen DS emulator

use tauri::State;
use serde::{Deserialize, Serialize};
use std::sync::Arc;
use parking_lot::RwLock;

// App state to hold our emulator instances
pub struct AppState {
    pub instances: Arc<RwLock<Vec<EmulatorInstance>>>,
}

// Represents a single melonDS emulator instance
pub struct EmulatorInstance {
    pub player_id: u8,
    pub rom_path: Option<String>,
    // Framebuffer data will be added as we integrate melonDS
}

#[derive(Debug, Serialize, Deserialize)]
pub struct PlayerInput {
    pub player_id: u8,
    pub buttons: u32, // Bitmask of pressed buttons
    pub touch_x: Option<f32>,
    pub touch_y: Option<f32>,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct FrameData {
    pub player_id: u8,
    pub framebuffer: Vec<u8>, // Raw framebuffer data
    pub timestamp: u64,
}

// Command to initialize the emulator
#[tauri::command]
pub async fn init_emulator(state: State<'_, AppState>, player_count: u8) -> Result<String, String> {
    log::info!("Initializing emulator with {} players", player_count);
    
    // For now, just create placeholder instances
    // In Phase 2, we'll integrate actual melonDS instances here
    let mut instances = state.instances.write();
    instances.clear();
    
    for i in 0..player_count {
        instances.push(EmulatorInstance {
            player_id: i + 1,
            rom_path: None,
        });
    }
    
    Ok(format!("Initialized {} emulator instances", player_count))
}

// Command to load a ROM
#[tauri::command]
pub async fn load_rom(state: State<'_, AppState>, player_id: u8, rom_path: String) -> Result<String, String> {
    log::info!("Loading ROM for player {}: {}", player_id, rom_path);
    
    // In Phase 2, this will actually load the ROM into the melonDS instance
    let instances = state.instances.read();
    if let Some(instance) = instances.iter().find(|i| i.player_id == player_id) {
        // Placeholder - will integrate with melonDS here
    }
    
    Ok(format!("ROM loaded for player {}", player_id))
}

// Command to send input to a player
#[tauri::command]
pub async fn send_input(state: State<'_, AppState>, input: PlayerInput) -> Result<(), String> {
    log::debug!("Input from player {}: buttons={:#x}, touch={:?}/{:?}", 
        input.player_id, input.buttons, input.touch_x, input.touch_y);
    
    // In Phase 2, this will route input to the specific melonDS instance
    Ok(())
}

// Command to get frame data (for rendering)
#[tauri::command]
pub async fn get_frames(state: State<'_, AppState>) -> Result<Vec<FrameData>, String> {
    // In Phase 2, this will return actual framebuffer data from melonDS
    let instances = state.instances.read();
    let mut frames = Vec::new();
    
    for instance in instances.iter() {
        frames.push(FrameData {
            player_id: instance.player_id,
            framebuffer: vec![0u8; 256 * 192 * 3], // Placeholder
            timestamp: 0,
        });
    }
    
    Ok(frames)
}

// Command to save state (all players)
#[tauri::command]
pub async fn save_state(state: State<'_, AppState>, save_path: String) -> Result<String, String> {
    log::info!("Saving state to {}", save_path);
    // In Phase 3, will save all instance states
    Ok(format!("State saved to {}", save_path))
}

// Command to load state (all players)
#[tauri::command]
pub async fn load_state(state: State<'_, AppState>, save_path: String) -> Result<String, String> {
    log::info!("Loading state from {}", save_path);
    // In Phase 3, will load all instance states
    Ok(format!("State loaded from {}", save_path))
}

// Command to toggle turbo mode
#[tauri::command]
pub async fn toggle_turbo(state: State<'_, AppState>) -> Result<bool, String> {
    // In Phase 3, will toggle turbo mode for all instances
    Ok(false)
}

// Command to change view mode
#[tauri::command]
pub async fn set_view_mode(state: State<'_, AppState>, mode: String) -> Result<String, String> {
    log::info!("Setting view mode to {}", mode);
    // View modes: grid, speaker, focus, overlay
    Ok(format!("View mode set to {}", mode))
}

// Command to quit the application
#[tauri::command]
pub async fn quit_app(state: State<'_, AppState>) -> Result<(), String> {
    log::info!("Quitting application");
    // In Tauri v2, we can use app.exit() from the event loop
    Ok(())
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    env_logger::init();
    log::info!("melonDS Multiplayer starting...");

    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .manage(AppState {
            instances: Arc::new(RwLock::new(Vec::new())),
        })
        .invoke_handler(tauri::generate_handler![
            init_emulator,
            load_rom,
            send_input,
            get_frames,
            save_state,
            load_state,
            toggle_turbo,
            set_view_mode,
            quit_app,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}

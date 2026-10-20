// What the page pulls: the frame and audio payloads, and the two commands that
// answer with them.
//
// The wire formats are described where they are written (below). The rule that
// keeps them cheap is the page's own request: it names the consoles and screens
// it is about to paint, so a layout that hides a screen stops paying for it at
// the source. An empty request means every console, both screens - that is what
// the browser host's transport and the diagnostic readers ask for.

use crate::{md_read_audio, AppState, MD_SCREEN_PIXELS};

/// One console's slice of a frame request: which seat, and which of its two
/// screens the page is actually showing (`1` top, `2` bottom, `3` both).
/// Deserialized from the page's own request, so a layout that hides a screen
/// stops paying for it at the source rather than at the canvas.
#[derive(serde::Deserialize)]
pub struct FrameSeat {
    pub seat: u32,
    #[serde(default = "both_screens")]
    pub screens: u32,
}

/// Both of a console's screens, for a request that names a seat but no screen.
fn both_screens() -> u32 {
    3
}

/// Write one screen as 15-bit RGB, two bytes per pixel.
///
/// The DS's own output is 5 bits per channel plus an alpha byte that is always
/// 0xFF, so four bytes per pixel carried a constant and three bits of padding
/// per channel. Same picture, half the bytes - and the bytes are what the trip
/// to the webview costs (see `frames_payload`).
fn push_screen_555(out: &mut Vec<u8>, pixels: &[u32]) {
    let start = out.len();
    out.resize(start + pixels.len() * 2, 0);
    for (chunk, px) in out[start..].chunks_exact_mut(2).zip(pixels) {
        // Bridge words are 0xAARRGGBB.
        let r = ((px >> 16) & 0xF8) >> 3;
        let g = ((px >> 8) & 0xF8) >> 3;
        let b = (px & 0xF8) >> 3;
        let packed = (r | (g << 5) | (b << 10)) as u16;
        chunk.copy_from_slice(&packed.to_le_bytes());
    }
}

/// Frame request, in the shape the page actually needs.
///
/// Layout (little endian): `u32` entry count, then per entry `u32` seat, `u32`
/// screens, `u64` snapshot sequence number, then the included screens in order
/// (top, then bottom), each [`MD_SCREEN_PIXELS`] RGB555 words.
///
/// An empty request means "every console, both screens", which is what the
/// browser host's transport and the diagnostic readers ask for; the page sends
/// only the seats and screens it is about to paint.
///
/// Two shapes were wrong here and each cost the primary use case frame rate
/// (measured 2026-10-20, four consoles: the page painted 16 fps while every
/// console ran 59.9, and a single `get_frames` pull took 70.1 ms at ~22 MB/s):
/// every console went over the wire on every pull including the screens the
/// layout hides, and every pixel crossed as four RGBA bytes. Both are fixed at
/// the request, not at the canvas: cost now follows what is on screen.
pub(crate) fn frames_payload(state: &AppState, wanted: &[FrameSeat]) -> Vec<u8> {
    let current = state.session.read();
    let Some(session) = current.as_ref() else {
        return Vec::new();
    };

    let seats: Vec<(usize, u32)> = if wanted.is_empty() {
        (0..session.frames.len()).map(|seat| (seat, 3)).collect()
    } else {
        wanted
            .iter()
            .filter(|w| (w.seat as usize) < session.frames.len())
            .map(|w| (w.seat as usize, w.screens & 3))
            .filter(|(_, screens)| *screens != 0)
            .collect()
    };

    let mut payload = Vec::with_capacity(4 + seats.len() * (16 + MD_SCREEN_PIXELS * 4));
    payload.extend_from_slice(&(seats.len() as u32).to_le_bytes());
    for (seat, screens) in seats {
        let buf = session.frames[seat].lock();
        payload.extend_from_slice(&(seat as u32).to_le_bytes());
        payload.extend_from_slice(&screens.to_le_bytes());
        payload.extend_from_slice(&buf.seq.to_le_bytes());
        if screens & 1 != 0 {
            push_screen_555(&mut payload, &buf.top);
        }
        if screens & 2 != 0 {
            push_screen_555(&mut payload, &buf.bottom);
        }
    }
    payload
}

/// Diagnostic: reject every Nth frame pull, so the page's recovery path can be
/// exercised rather than assumed. Off unless `MD_FRAME_PULL_FAIL` is set; `=3`
/// rejects every third pull. A rejected pull must cost one frame, not the pump
/// (the shape this file replaced stopped every player's picture for the rest of
/// the session), and the only honest way to show that is to make one fail on
/// purpose.
fn frame_pull_fails_now() -> bool {
    use std::sync::OnceLock;
    static EVERY: OnceLock<u32> = OnceLock::new();
    static SEEN: std::sync::atomic::AtomicU32 = std::sync::atomic::AtomicU32::new(0);
    let every = *EVERY.get_or_init(|| {
        std::env::var("MD_FRAME_PULL_FAIL")
            .ok()
            .and_then(|v| v.trim().parse::<u32>().ok())
            .unwrap_or(0)
    });
    if every == 0 {
        return false;
    }
    let seen = SEEN.fetch_add(1, std::sync::atomic::Ordering::Relaxed) + 1;
    seen % every == 0
}

#[tauri::command]
pub(crate) fn get_frames(
    state: tauri::State<'_, AppState>,
    seats: Option<Vec<FrameSeat>>,
) -> Result<tauri::ipc::Response, String> {
    if frame_pull_fails_now() {
        return Err("frame pull failed on purpose (MD_FRAME_PULL_FAIL)".to_string());
    }
    Ok(tauri::ipc::Response::new(frames_payload(
        state.inner(),
        &seats.unwrap_or_default(),
    )))
}

/// The most one console's audio fetch can carry (~170 ms at 48 kHz; a DS frame
/// is ~1092 samples).
const MD_AUDIO_MAX_FRAMES: usize = 8192;

/// One fetch of audio, in the same shape as `get_frames`: a header, then one
/// chunk per requested console. Polled by the frontend only while online play
/// is hosting, so a local session pays nothing for a stream nobody is watching.
///
/// Layout (little endian): `u32 rate`, `u32 seatCount`, then per seat
/// `u32 seat`, `u32 byteLen`, `byteLen` bytes of interleaved stereo s16.
/// Split out of the command for the same reason as `frames_payload`.
pub(crate) fn audio_payload(state: &AppState, seats: &[u32]) -> Vec<u8> {
    let current = state.session.read();
    let Some(session) = current.as_ref() else {
        return Vec::new();
    };

    let mut samples = [0i16; MD_AUDIO_MAX_FRAMES * 2];
    let mut rate: i32 = 48000;
    let mut chunks: Vec<(u32, Vec<u8>)> = Vec::with_capacity(seats.len());
    for &seat in seats {
        let Some(inst) = session.instances.get(seat as usize) else {
            continue;
        };
        let n = unsafe {
            md_read_audio(
                inst.ptr,
                samples.as_mut_ptr(),
                MD_AUDIO_MAX_FRAMES as i32,
                &mut rate,
            )
        };
        if n <= 0 {
            continue;
        }
        let mut bytes = Vec::with_capacity(n as usize * 2);
        for value in &samples[..n as usize] {
            bytes.extend_from_slice(&value.to_le_bytes());
        }
        chunks.push((seat, bytes));
    }

    let mut payload = Vec::with_capacity(
        8 + chunks.iter().map(|(_, b)| 8 + b.len()).sum::<usize>(),
    );
    payload.extend_from_slice(&(rate as u32).to_le_bytes());
    payload.extend_from_slice(&(chunks.len() as u32).to_le_bytes());
    for (seat, bytes) in chunks {
        payload.extend_from_slice(&seat.to_le_bytes());
        payload.extend_from_slice(&(bytes.len() as u32).to_le_bytes());
        payload.extend_from_slice(&bytes);
    }
    payload
}

#[tauri::command]
pub(crate) fn get_audio(
    state: tauri::State<'_, AppState>,
    seats: Vec<u32>,
) -> Result<tauri::ipc::Response, String> {
    Ok(tauri::ipc::Response::new(audio_payload(
        state.inner(),
        &seats,
    )))
}

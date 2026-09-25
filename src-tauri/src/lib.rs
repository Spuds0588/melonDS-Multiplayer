// melonDS Multiplayer - Tauri backend
//
// Drives real melonDS instances through the C bridge (src/frontend/tauri/
// melonds_bridge.h): one emulated console per player, each on its own thread,
// meeting at a per-frame lockstep barrier exactly like the proven
// tools/coretest/game_session.cpp harness. The webview renders double-buffered
// framebuffer snapshots; keyboard events are routed back into the matching
// instance.
//
// NOTE: #[tauri::command] functions must NOT be `pub`. A public command in the
// same module collides with the hidden `__cmd__*` macro the attribute itself
// exports and fails with E0255 on every toolchain - see history.md v0.1.5.

#![allow(non_snake_case)] // lib name is melondSMultiplayer_lib by config

use parking_lot::{Condvar, Mutex, RwLock};
use tauri::Manager;
use serde::{Deserialize, Serialize};
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, AtomicI32, AtomicI64, AtomicU64, AtomicUsize, Ordering};
use std::sync::Arc;
use std::thread::JoinHandle;
use std::time::Duration;

// ---------------------------------------------------------------------------
// FFI: the C bridge (see src/frontend/tauri/melonds_bridge.h)
// ---------------------------------------------------------------------------

const MD_SCREEN_WIDTH: usize = 256;
const MD_SCREEN_HEIGHT: usize = 192;
const MD_SCREEN_PIXELS: usize = MD_SCREEN_WIDTH * MD_SCREEN_HEIGHT;
const MD_SCREEN_BYTES: usize = MD_SCREEN_PIXELS * 4;

// Button bit layout, matches the emulated DS keypad register.
const MD_BTN_A: u32 = 1 << 0;
const MD_BTN_B: u32 = 1 << 1;
const MD_BTN_SELECT: u32 = 1 << 2;
const MD_BTN_START: u32 = 1 << 3;
const MD_BTN_RIGHT: u32 = 1 << 4;
const MD_BTN_LEFT: u32 = 1 << 5;
const MD_BTN_UP: u32 = 1 << 6;
const MD_BTN_DOWN: u32 = 1 << 7;
const MD_BTN_R: u32 = 1 << 8;
const MD_BTN_L: u32 = 1 << 9;
const MD_BTN_X: u32 = 1 << 10;
const MD_BTN_Y: u32 = 1 << 11;

const MD_STOP_NONE: i32 = 0;

// Diagnostic ROM ABI (tools/mkdiagrom/mkdiagrom.py): identity block the ROM
// reads at boot so each player's console renders in its own colour.
const DIAG_BASE: u32 = 0x02FFF000;
const DIAG_MAGIC: u32 = 0x4D44474D; // 'MDGM'

#[repr(C)]
struct MDInstance {
    _private: [u8; 0],
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
struct MDLinkStats {
    connected: u32,
    begin_count: u32,
    end_count: u32,
    packets_sent: u64,
    packets_received: u64,
    bytes_sent: u64,
    bytes_received: u64,
    // v2 per-kind split (melonds_bridge.h):
    rx_broadcast: u64,
    rx_cmd: u64,
    rx_reply: u64,
    rx_ack: u64,
    rx_polls: u64,
    tx_cmd: u64,
    tx_reply: u64,
    tx_ack: u64,
    rx_empty_polls: u64,
    last_rx_us: u64,
    epoch_us: u64,
}

extern "C" {
    fn md_create(console_type: i32, name: *const i8) -> *mut MDInstance;
    fn md_destroy(inst: *mut MDInstance);
    fn md_load_rom(inst: *mut MDInstance, rom_path: *const i8) -> i32;
    fn md_rom_title(inst: *mut MDInstance, out: *mut i8, cap: usize) -> i32;
    fn md_set_jit(inst: *mut MDInstance, enabled: i32) -> i32;
    fn md_write_ram(inst: *mut MDInstance, addr: u32, data: *const u8, len: usize) -> i32;
    fn md_set_buttons(inst: *mut MDInstance, mask: u32);
    fn md_touch(inst: *mut MDInstance, x: i32, y: i32);
    fn md_release_touch(inst: *mut MDInstance);
    fn md_run_frame(inst: *mut MDInstance) -> u32;
    fn md_get_screens(
        inst: *mut MDInstance,
        top: *mut *const u32,
        bottom: *mut *const u32,
    ) -> i32;
    fn md_frame_counter(inst: *mut MDInstance) -> u32;
    // Program counters, for the health thread's "is the game still executing
    // code or just producing frames?" check. Read-only, so sampling them from
    // another thread is no different from md_link_stats.
    fn md_get_pc(inst: *mut MDInstance, cpu: i32) -> u32;
    fn md_console_asleep(inst: *mut MDInstance) -> i32;
    fn md_stop_reason(inst: *mut MDInstance) -> i32;
    fn md_last_error(inst: *mut MDInstance) -> *const i8;
    fn md_link_init();
    fn md_link_shutdown();
    fn md_tag_current_thread(tag: *const i8);
    fn md_link_attach(inst: *mut MDInstance, slot: i32);
    fn md_link_set_recv_timeout(ms: i32);
    fn md_link_set_reply_timeout(ms: i32);
    fn md_link_set_reply_budget(inst: *mut MDInstance, budget_ns: i64);
    fn md_link_stats(inst: *mut MDInstance, out: *mut MDLinkStats) -> i32;
    fn md_link_resync(inst: *mut MDInstance);
    fn md_link_connected_mask() -> u32;
    fn md_link_send_packet(inst: *mut MDInstance, data: *const u8, len: usize, timestamp: u64) -> i32;
    // Returns the byte count, 0 when nothing is queued. Declared `int` in
    // melonds_bridge.h, so it MUST be read as i32 here: this used to be
    // declared `usize`, which left the upper half of the return register
    // undefined and turned every "did my packet arrive" comparison into a coin
    // toss (the send side was always declared i32, which is why sends looked
    // reliable while receives looked flaky).
    fn md_link_recv_packet(inst: *mut MDInstance, out: *mut u8, cap: usize, timestamp: *mut u64) -> i32;
    fn md_write_io16(inst: *mut MDInstance, cpu: i32, addr: u32, value: u16) -> i32;
    fn md_link_slot(inst: *mut MDInstance) -> i32;
}

fn cstr(s: &str) -> std::ffi::CString {
    std::ffi::CString::new(s).unwrap_or_else(|_| std::ffi::CString::new("?").unwrap())
}

fn last_error(inst: *mut MDInstance) -> String {
    unsafe {
        let p = md_last_error(inst);
        if p.is_null() {
            "unknown bridge error".to_string()
        } else {
            std::ffi::CStr::from_ptr(p).to_string_lossy().into_owned()
        }
    }
}

// ---------------------------------------------------------------------------
// Session state
// ---------------------------------------------------------------------------

/// A link operation a host thread (health watchdog, selftest, link monitor)
/// wants done *by the console's own driver thread*.
///
/// Why the indirection: the emulated WiFi pulls frames out of its own packet
/// FIFO from inside md_run_frame. A second thread calling into the bus at the
/// same time - to drain a probe, or to re-anchor a stalled read pointer after a
/// freeze - consumes frames the game was about to read and perturbs the read
/// offset the game's own receive path is tracking. Every operation below is
/// therefore queued and executed inside the run loop, between frames, where
/// nothing else is touching that console. This is also why the earlier
/// "click Link test and it unsticks, or falls over" behaviour existed: the
/// selftest used to reach straight into a running console's FIFO.
enum LinkOp {
    /// Re-anchor this console's FIFO read offset and clear its receive
    /// semaphores (the "kick" that cures a stalled WiFi state machine).
    Resync { reply: std::sync::mpsc::Sender<()> },
    /// Broadcast a packet on this console's slot. Replies with the queued byte
    /// count (>0 sent, 0 refused).
    Send {
        data: Vec<u8>,
        ts: u64,
        reply: std::sync::mpsc::Sender<i32>,
    },
    /// Drain everything currently queued for this console, oldest first.
    Drain {
        reply: std::sync::mpsc::Sender<Vec<(Vec<u8>, u64)>>,
    },
}

/// How long a host thread waits for the driver thread to service a link op.
/// The driver turns frames at 59.8Hz, so a healthy console answers in ~17ms;
/// the slack is for a machine hiccup, not for a wedged console.
const LINK_OP_TIMEOUT: Duration = Duration::from_millis(250);

/// One emulated console plus the input state the webview writes into it.
struct Instance {
    ptr: *mut MDInstance,
    /// Buttons currently held, applied every frame by the run loop.
    buttons: AtomicU64,
    /// Pen state while down: (touching, x, y) in touch-screen pixels.
    touch: Mutex<(bool, i32, i32)>,
    /// Wireless power request for the link selftest: -1 none, 0 off, 1 on.
    /// Applied by the console's own run loop (emulated state is only touched
    /// by its driver thread); the selftest polls wireless_applied.
    wireless_req: AtomicI32,
    /// Last request value the run loop applied, -1 = nothing yet.
    wireless_applied: AtomicI32,
    /// Link operations waiting for the driver thread (see LinkOp).
    link_ops: Mutex<Vec<LinkOp>>,
}

impl Instance {
    /// Execute one queued link op. Driver thread only.
    fn run_link_op(&self, op: LinkOp) {
        match op {
            LinkOp::Resync { reply } => {
                unsafe { md_link_resync(self.ptr) };
                let _ = reply.send(());
            }
            LinkOp::Send { data, ts, reply } => {
                let queued = unsafe {
                    md_link_send_packet(self.ptr, data.as_ptr(), data.len(), ts)
                };
                let _ = reply.send(queued);
            }
            LinkOp::Drain { reply } => {
                // Bounded per turn: a probe wants "what is queued right now",
                // not an unbounded sweep that could starve the frame.
                let mut out: Vec<(Vec<u8>, u64)> = Vec::new();
                let mut buf = [0u8; 2048];
                let mut out_ts = 0u64;
                while out.len() < 32 {
                    let n = unsafe {
                        md_link_recv_packet(self.ptr, buf.as_mut_ptr(), buf.len(), &mut out_ts)
                    };
                    if n <= 0 {
                        break;
                    }
                    out.push((buf[..(n as usize).min(buf.len())].to_vec(), out_ts));
                }
                let _ = reply.send(out);
            }
        }
    }

    /// Service every op that arrived since the last frame. Driver thread only.
    fn service_link_ops(&self) {
        let pending: Vec<LinkOp> = std::mem::take(&mut *self.link_ops.lock());
        for op in pending {
            self.run_link_op(op);
        }
    }

    /// Ask the driver thread to re-anchor this console's bus state. False when
    /// the driver did not answer in time (its console is gone or wedged).
    fn link_resync_now(&self, what: &str) -> bool {
        let (tx, rx) = std::sync::mpsc::channel();
        self.link_ops.lock().push(LinkOp::Resync { reply: tx });
        match rx.recv_timeout(LINK_OP_TIMEOUT) {
            Ok(()) => true,
            Err(_) => {
                log::warn!("{what}: console driver did not answer a resync request");
                false
            }
        }
    }

    /// Queued byte count for a broadcast from this console, or None when the
    /// driver did not answer.
    fn link_send_now(&self, data: &[u8], ts: u64, what: &str) -> Option<i32> {
        let (tx, rx) = std::sync::mpsc::channel();
        self.link_ops.lock().push(LinkOp::Send {
            data: data.to_vec(),
            ts,
            reply: tx,
        });
        match rx.recv_timeout(LINK_OP_TIMEOUT) {
            Ok(queued) => Some(queued),
            Err(_) => {
                log::warn!("{what}: console driver did not answer a send request");
                None
            }
        }
    }

    /// Everything queued for this console right now, or None when the driver
    /// did not answer.
    fn link_drain_now(&self, what: &str) -> Option<Vec<(Vec<u8>, u64)>> {
        let (tx, rx) = std::sync::mpsc::channel();
        self.link_ops.lock().push(LinkOp::Drain { reply: tx });
        match rx.recv_timeout(LINK_OP_TIMEOUT) {
            Ok(packets) => Some(packets),
            Err(_) => {
                log::warn!("{what}: console driver did not answer a drain request");
                None
            }
        }
    }
}

// The bridge's contract is one driver per instance *at a time* (see the header
// comment in melonds_bridge.h): the emulator itself is driven exclusively by
// this handle's player thread, while other threads only make diagnostic reads
// (frame counters, link stats) and route input, which the bridge tolerates.
unsafe impl Send for Instance {}
unsafe impl Sync for Instance {}

/// Double-buffered copy of a console's screens, refreshed by its run loop.
/// Reading this instead of the emulator's live framebuffer keeps the IPC thread
/// out of the emulator's internals entirely: a snapshot is always internally
/// consistent and never tears mid-frame.
struct FrameBuf {
    top: Vec<u32>,
    bottom: Vec<u32>,
    /// Console frame counter at snapshot time; the frontend repaints only when
    /// this moves.
    seq: u64,
}

impl FrameBuf {
    fn new() -> Self {
        Self {
            top: vec![0; MD_SCREEN_PIXELS],
            bottom: vec![0; MD_SCREEN_PIXELS],
            seq: 0,
        }
    }
}

/// Which key each player holds to peek at their touch screen (release snaps
/// back to the top screen). Deliberately outside the button keymap: flip is a
/// display gesture, not an emulated button, and must never collide with a
/// game binding. It still ships as per-player data so the frontend hints can
/// label it and the remapper can move it like any other binding.
fn default_flip_key(player: usize) -> &'static str {
    // Peek applies to whichever session the keyboard currently controls
    // (digits 1-4 switch it in the frontend), so every player gets the same
    // key - right of the JKL cluster.
    let _ = player;
    "ShiftRight"
}

/// Keyboard bindings for a player's console. Every player gets the same
/// default keys: one keyboard drives whichever session is selected (digits
/// 1-4 in the frontend switch between them), so per-player keyboard zones
/// (the old P2 arrows+numpad) are wrong until someone owns per-session
/// remapping. Remapping is a Phase 2 todo; when it lands, these become just
/// the defaults it edits.
fn default_keymap(player: usize) -> Vec<(&'static str, u32)> {
    let _ = player;
    vec![
        ("KeyA", MD_BTN_LEFT),
        ("KeyD", MD_BTN_RIGHT),
        ("KeyW", MD_BTN_UP),
        ("KeyS", MD_BTN_DOWN),
        ("KeyJ", MD_BTN_A),
        ("KeyK", MD_BTN_B),
        ("KeyU", MD_BTN_Y),
        ("KeyI", MD_BTN_X),
        ("KeyQ", MD_BTN_L),
        ("KeyE", MD_BTN_R),
        ("Space", MD_BTN_START),
        ("ShiftLeft", MD_BTN_SELECT),
    ]
}

#[derive(Serialize, Clone)]
struct SessionInfo {
    players: usize,
    rom: String,
    title: String,
    jit: bool,
    keymaps: Vec<Vec<(String, u32)>>,
    /// Per-player hold-to-peek key for the touch screen (frontend-display only).
    flip_keys: Vec<String>,
}

struct Session {
    players: usize,
    rom: PathBuf,
    title: String,
    jit: bool,
    keymaps: Vec<Vec<(String, u32)>>,
    flip_keys: Vec<String>,
    instances: Vec<Arc<Instance>>,
    frames: Vec<Arc<Mutex<FrameBuf>>>,
    stop: Arc<AtomicBool>,
    /// Consoles still inside their run loop. Dropping below `players` means one
    /// console stopped and the lockstep can no longer be completed.
    alive: Arc<AtomicUsize>,
    /// Always-on link verification state (see spawn_link_monitor).
    monitor: Arc<LinkMonitor>,
    threads: Vec<JoinHandle<()>>,
    health: Option<JoinHandle<()>>,
    linkmon: Option<JoinHandle<()>>,
    started_at: std::time::Instant,
}

#[derive(Default)]
pub struct AppState {
    session: RwLock<Option<Arc<Session>>>,
}

impl Drop for Session {
    fn drop(&mut self) {
        // Signal the run loops, then join before touching any console. The
        // framebuffers are only safe to read between frames, and destroying a
        // console while its thread is inside md_run_frame is undefined. The
        // health thread joins too: it reads console state, so it must be gone
        // before md_destroy. Each step is logged, so a teardown that wedges
        // ("stopped after joining console threads") is visible after the fact.
        log::info!(
            "session teardown: stopping {} console thread(s) after {}s",
            self.threads.len(),
            self.started_at.elapsed().as_secs()
        );
        self.stop.store(true, Ordering::SeqCst);
        for t in self.threads.drain(..) {
            let _ = t.join();
        }
        log::info!("session teardown: console threads joined");
        if let Some(t) = self.health.take() {
            let _ = t.join();
        }
        if let Some(t) = self.linkmon.take() {
            let _ = t.join();
        }
        log::info!("session teardown: watcher threads joined, destroying consoles");
        unsafe {
            for inst in &self.instances {
                md_destroy(inst.ptr);
            }
            md_link_shutdown();
        }
        log::info!("session teardown: complete");
    }
}

// ---------------------------------------------------------------------------
// Session lifecycle
// ---------------------------------------------------------------------------

fn start_session_inner(
    state: &AppState,
    players: u8,
    rom_path: String,
    jit: bool,
) -> Result<SessionInfo, String> {
    if players < 1 || players > 4 {
        return Err(format!("players must be 1..4, got {players}"));
    }
    let rom = PathBuf::from(&rom_path);
    if !rom.is_file() {
        return Err(format!("ROM not found: {rom_path}"));
    }

    // Replace any running session first: dropping it stops its threads and
    // tears the link bus down cleanly before we rebuild everything.
    *state.session.write() = None;

    log::info!("starting session: {players} players, rom {rom_path}, jit={jit}");

    unsafe {
        md_link_init();
        // Poll-mode receive plus a host reply timeout measured against ONE
        // EMULATED FRAME, not against a wall clock guess.
        //
        // md_link_set_recv_timeout(0) stays: a console that blocks on receive
        // deadlocks its lockstep partner (game_session.cpp header notes,
        // history.md v0.1.4).
        //
        // The reply timeout is a different clock domain. It bounds how long a
        // host waits for a client's answer to a command it just sent, and the
        // unit is *wall clock* milliseconds while the DS protocol it imitates
        // is measured in *emulated* time. 2ms of wall clock at ~2.4x emulation
        // speed is only ~0.8ms of emulated time - less than the 1-2ms DS slot
        // window being mirrored, and well short of the client's next frame
        // boundary, which is where its answer actually goes out.
        //
        // Measured on the 2026-09-25 MKDS join: the client replied 117 times,
        // the host collected 71 of them, and 52 collection rounds came back
        // empty - the host was asking (and giving up) before the answer could
        // arrive, then reporting `clientfail=0002` to the game, which dropped
        // the client. One emulated frame is 16.7ms; allow two frames of slack
        // so a client that answers at its frame boundary is never missed. The
        // wait returns the instant a reply is posted, so a healthy session pays
        // nothing for the larger bound.
        // Override with MD_REPLY_TIMEOUT_MS to sweep without a rebuild.
        md_link_set_recv_timeout(0);
        let reply_timeout_ms = std::env::var("MD_REPLY_TIMEOUT_MS")
            .ok()
            .and_then(|v| v.trim().parse::<i32>().ok())
            .filter(|ms| *ms >= 0)
            .unwrap_or(34);
        log::info!("link reply timeout: {reply_timeout_ms}ms (2 emulated frames at 59.826fps)");
        md_link_set_reply_timeout(reply_timeout_ms);

        // ...and the same quantity again, as a per-frame budget instead of a
        // per-window timeout, because a window's wait is wall-clock time spent
        // inside an emulated frame and one MKDS join showed what leaving that
        // unbounded costs: 1436 commands asked for replies inside 15s, 363 of
        // those windows were empty, and at a 34ms ceiling that is 12.3s of the
        // 15s spent blocked - the session ran at 9fps and every peer waited for
        // it at the lockstep barrier. A frame is now allowed to spend up to two
        // of its own frames' worth of wall time waiting for replies, which is
        // what the per-window bound was trying to say in the first place: on
        // schedule the wait eats idle slack, off schedule the frame comes
        // first and the game hears `clientfail` the usual way. See
        // MPInterface::SetReplyBudget.
        // Override with MD_REPLY_BUDGET_MS to sweep without a rebuild.
        let reply_budget_ms = std::env::var("MD_REPLY_BUDGET_MS")
            .ok()
            .and_then(|v| v.trim().parse::<i64>().ok())
            .unwrap_or(-1);
        let frames_of_slack = if reply_budget_ms >= 0 { reply_budget_ms } else { 2 };
        REPLY_BUDGET_NS.store(
            (frames_of_slack * 1_000_000_000 * 889 / 53_150) as i64,
            Ordering::SeqCst,
        );
        log::info!(
            "link reply budget: {frames_of_slack} emulated frame(s) of wall clock per frame for a whole console's reply waits"
        );
    }

    let n = players as usize;
    let mut instances: Vec<Arc<Instance>> = Vec::with_capacity(n);
    for i in 0..n {
        let name = cstr(&format!("player{}", i + 1));
        let ptr = unsafe { md_create(0, name.as_ptr()) };
        if ptr.is_null() {
            for inst in &instances {
                unsafe { md_destroy(inst.ptr) };
            }
            unsafe { md_link_shutdown() };
            return Err("md_create failed - the C bridge refused to make a console".into());
        }
        unsafe { md_link_attach(ptr, i as i32) };
        if !jit {
            unsafe { md_set_jit(ptr, 0) };
        }
        instances.push(Arc::new(Instance {
            ptr,
            buttons: AtomicU64::new(0),
            touch: Mutex::new((false, 0, 0)),
            wireless_req: AtomicI32::new(-1),
            wireless_applied: AtomicI32::new(-1),
            link_ops: Mutex::new(Vec::new()),
        }));
    }

    // Load the ROM into every console (while nothing runs), then stamp the diag
    // identity block - the smoke test's init order.
    for (i, inst) in instances.iter().enumerate() {
        let path = cstr(&rom_path);
        let rc = unsafe { md_load_rom(inst.ptr, path.as_ptr()) };
        if rc != 0 {
            let err = last_error(inst.ptr);
            for inst in &instances {
                unsafe { md_destroy(inst.ptr) };
            }
            unsafe { md_link_shutdown() };
            return Err(format!("player {}: failed to load ROM: {err}", i + 1));
        }
    }

    for (i, inst) in instances.iter().enumerate() {
        // Per-player colour in BGR555: red P1, blue P2 - the colours the diag
        // ROM renders its stripes in, so identity is visible on screen.
        let colour: u32 = match i {
            0 => 0x001F, // red
            1 => 0x7C00, // blue
            2 => 0x03E0, // green
            _ => 0x7C1F, // magenta
        };
        let block: [u32; 4] = [DIAG_MAGIC, i as u32, colour, 0];
        let rc = unsafe {
            md_write_ram(
                inst.ptr,
                DIAG_BASE,
                block.as_ptr().cast::<u8>(),
                std::mem::size_of::<[u32; 4]>(),
            )
        };
        if rc != 0 {
            for inst in &instances {
                unsafe { md_destroy(inst.ptr) };
            }
            unsafe { md_link_shutdown() };
            return Err("failed to stamp diagnostic identity block".into());
        }
    }

    let title = read_title(&instances[0]);

    let keymaps: Vec<Vec<(String, u32)>> = (0..n)
        .map(|p| default_keymap(p).into_iter().map(|(k, b)| (k.into(), b)).collect())
        .collect();
    let flip_keys: Vec<String> = (0..n).map(|p| default_flip_key(p).into()).collect();

    // All n consoles run on their own threads and meet at a per-frame barrier:
    // a console can only be answered by the link while its peers are actually
    // running, which is what makes the harness structure load-bearing.
    let stop = Arc::new(AtomicBool::new(false));
    let lockstep = Arc::new(Lockstep::new(n));
    let alive = Arc::new(AtomicUsize::new(n));
    let mut monitor_state = LinkMonitor::default();
    // Probe injection is opt-in: a probe is a foreign frame in the game's own
    // receive stream, and the part that actually cured freezes - the stall kick
    // - needs no packet at all.
    monitor_state.inject = std::env::var("MD_LINK_PROBE")
        .map(|v| v == "1" || v.eq_ignore_ascii_case("true"))
        .unwrap_or(false);
    let monitor = Arc::new(monitor_state);
    let frames: Vec<Arc<Mutex<FrameBuf>>> =
        (0..n).map(|_| Arc::new(Mutex::new(FrameBuf::new()))).collect();

    let mut threads = Vec::with_capacity(n);
    for p in 0..n {
        let inst = Arc::clone(&instances[p]);
        let fb = Arc::clone(&frames[p]);
        let lockstep = Arc::clone(&lockstep);
        let alive = Arc::clone(&alive);
        let stop = Arc::clone(&stop);
        threads.push(
            std::thread::Builder::new()
                .name(format!("player{}", p + 1))
                .spawn(move || {
                    // Tag this thread so every core log line it emits carries
                    // the console name - interleaved per-console wifi logs
                    // ("which console was on the wrong channel") become
                    // attributable at a glance.
                    let tag = format!("p{}\0", p + 1);
                    unsafe { md_tag_current_thread(tag.as_ptr().cast::<i8>()) };
                    run_loop(p, inst, fb, &lockstep, &alive, &stop);
                })
                .expect("spawn player thread"),
        );
    }

    // Background health logger: every 5s, per-console frame counters and link
    // stats - extended into a link-watchdog. Read-only on purpose - injecting
    // host packets into the queues the emulated WiFi reads could confuse a
    // real game. It classifies each console's link state per window so a
    // freeze is attributable from the log alone:
    //   LINK STALL  - wireless on, no packet accepted from a peer all window
    //                 (the game's WiFi state machine is stuck: the frozen MKDS
    //                 lobby looked exactly like this)
    //   LINK IDLE   - no traffic and few polls: the game's WiFi isn't even
    //                 asking (menu, powered off, or asleep)
    //   LINK ACTIVE - packets flowing; verbose mode adds per-kind rates
    // Holds Arc<Instance> clones, never the Session itself: Drop must be able
    // to set `stop` and join this thread before the consoles are destroyed.
    let verbose = std::env::var("MD_LINK_VERBOSE")
        .map(|v| v == "1" || v.eq_ignore_ascii_case("true"))
        .unwrap_or(false);
    let health = {
        let insts = instances.clone();
        let health_stop = Arc::clone(&stop);
        let health_monitor = Arc::clone(&monitor);
        std::thread::Builder::new()
            .name("health".into())
            .spawn(move || {
                #[derive(Clone, Copy, Default)]
                struct Snapshot {
                    frames: u32,
                    tx: u64,
                    rx: u64,
                    rx_bcast: u64,
                    rx_cmd: u64,
                    rx_rep: u64,
                    rx_ack: u64,
                    rx_polls: u64,
                    rx_empty: u64,
                    tx_cmd: u64,
                    tx_rep: u64,
                    tx_ack: u64,
                    begin: u32,
                    end: u32,
                    connected: u32,
                    last_rx_us: u64,
                }
                let mut prev: Vec<Snapshot> = vec![Snapshot::default(); insts.len()];
                let mut stall_windows: Vec<u32> = vec![0; insts.len()];
                // PC churn: a game that has wedged keeps producing frames while
                // executing the same handful of instructions forever, waiting on
                // a hardware interrupt that never arrives. Frame counters cannot
                // see that - the frozen MKDS lobby ran at a full 59.8fps - so the
                // window is slept in 50ms slices and both program counters are
                // sampled instead, counting how often they move. A game that is
                // running changes PC on nearly every sample; a wedged one does
                // not. (Sampled here rather than in the run loop: this thread is
                // read-only and never touches emulated state.)
                let mut pc_prev: Vec<(u32, u32)> = vec![(0, 0); insts.len()];
                let mut pc_moved: Vec<u32> = vec![0; insts.len()];
                let mut pc_samples: Vec<u32> = vec![0; insts.len()];
                while !health_stop.load(Ordering::SeqCst) {
                    for slot in pc_moved.iter_mut() {
                        *slot = 0;
                    }
                    for slot in pc_samples.iter_mut() {
                        *slot = 0;
                    }
                    for _ in 0..100 {
                        std::thread::sleep(std::time::Duration::from_millis(50));
                        if health_stop.load(Ordering::SeqCst) {
                            break;
                        }
                        for (i, inst) in insts.iter().enumerate() {
                            // An asleep console legitimately holds its PC still.
                            if unsafe { md_console_asleep(inst.ptr) } != 0 {
                                continue;
                            }
                            let pc9 = unsafe { md_get_pc(inst.ptr, 9) };
                            let pc7 = unsafe { md_get_pc(inst.ptr, 7) };
                            pc_samples[i] += 1;
                            if pc_samples[i] > 1 && (pc9, pc7) != pc_prev[i] {
                                pc_moved[i] += 1;
                            }
                            pc_prev[i] = (pc9, pc7);
                        }
                    }
                    if health_stop.load(Ordering::SeqCst) {
                        break;
                    }
                    for (i, inst) in insts.iter().enumerate() {
                        let mut stats = MDLinkStats::default();
                        unsafe { md_link_stats(inst.ptr, &mut stats) };
                        let frames = unsafe { md_frame_counter(inst.ptr) };
                        let snap = Snapshot {
                            frames,
                            tx: stats.packets_sent,
                            rx: stats.packets_received,
                            rx_bcast: stats.rx_broadcast,
                            rx_cmd: stats.rx_cmd,
                            rx_rep: stats.rx_reply,
                            rx_ack: stats.rx_ack,
                            rx_polls: stats.rx_polls,
                            rx_empty: stats.rx_empty_polls,
                            tx_cmd: stats.tx_cmd,
                            tx_rep: stats.tx_reply,
                            tx_ack: stats.tx_ack,
                            begin: stats.begin_count,
                            end: stats.end_count,
                            connected: stats.connected,
                            last_rx_us: stats.last_rx_us,
                        };
                        let p = &mut prev[i];
                        let d_frames = snap.frames.wrapping_sub(p.frames);
                        let d_tx = snap.tx.wrapping_sub(p.tx);
                        let d_rx = snap.rx.wrapping_sub(p.rx);
                        let d_polls = snap.rx_polls.wrapping_sub(p.rx_polls);
                        // Wireless is "on" between a begin and its matching end.
                        let wireless_on = stats.begin_count > stats.end_count;
                        let rx_active = d_rx > 0;
                        // Seconds since this console last accepted a packet:
                        // the stall clock. epoch_us <= last_rx_us holds once
                        // anything has been received.
                        let since_rx_secs = if snap.last_rx_us >= stats.epoch_us && stats.epoch_us > 0 {
                            (snap.last_rx_us - stats.epoch_us) as f64 / 1e6
                        } else {
                            -1.0
                        };
                        let state = if !wireless_on {
                            "LINK OFF  "
                        } else if rx_active {
                            "LINK ACTIVE"
                        } else if d_polls > 60 {
                            "LINK STALL"
                        } else {
                            "LINK IDLE "
                        };

                        // Auto-kick: a console polling hard but accepting
                        // nothing while its peers sit on the bus has a drifted
                        // FIFO read pointer or an unsignaled semaphore. This
                        // is exactly the state a human "Link test" click
                        // cured (the selftest's MP_Begin lands in
                        // LocalMP::Begin, which re-anchors the read offset).
                        //
                        // It waits for KICK_AFTER_WINDOWS consecutive stalled
                        // windows before the first kick, because a host
                        // *waiting for a client to associate* looks identical
                        // window for window and is not broken at all. The
                        // 2026-09-25 MKDS join is the proof: the kick fired
                        // 1.45s after the peer's radio came up (mid-association),
                        // the host's next six acks said clientfail=0002 - "client
                        // did not reply" - and the client deauthenticated 1.75s
                        // in. The kick discards the very replies it then misses.
                        // The historic cure it was built for came after ~10s of
                        // continuous STALL, so two windows keeps that cure and
                        // stops interrupting handshakes.
                        const KICK_AFTER_WINDOWS: u32 = 2;
                        if state == "LINK STALL" {
                            stall_windows[i] = stall_windows[i].saturating_add(1);
                        } else {
                            stall_windows[i] = 0;
                        }

                        let kick_due = stall_windows[i] >= KICK_AFTER_WINDOWS
                            && stall_windows[i] <= KICK_AFTER_WINDOWS + 2;
                        if kick_due && snap.connected.count_ones() > 1 {
                            // Kicked through the driver thread: this used to
                            // call straight into the bus, mutating the very
                            // FIFO read offset the console's own receive path
                            // was using.
                            if inst.link_resync_now("health watchdog") {
                                health_monitor.resyncs.fetch_add(1, Ordering::SeqCst);
                                health_monitor.stalled.fetch_add(1, Ordering::SeqCst);
                                log::warn!(
                                    "p{} stalled with peers on the bus for {} windows - resynced its bus state (kick #{})",
                                    i + 1,
                                    stall_windows[i],
                                    stall_windows[i] - KICK_AFTER_WINDOWS + 1
                                );
                            }
                        } else if state == "LINK STALL" && verbose {
                            // Attribution: a stall we deliberately did NOT kick,
                            // so a later failure cannot be blamed on a kick that
                            // never happened.
                            log::info!(
                                "p{} stalled (window {}/{}) - below the kick threshold, left alone",
                                i + 1,
                                stall_windows[i],
                                KICK_AFTER_WINDOWS
                            );
                        }
                        // A live game moves the PC on essentially every one of
                        // the hundred samples in the window. When it stops doing
                        // so while frames keep coming, the emulator is fine and
                        // the game is spinning - the failure the user sees as
                        // "the picture froze but the other screen kept playing".
                        let asleep = unsafe { md_console_asleep(inst.ptr) } != 0;
                        let wedged = !asleep
                            && pc_samples[i] > 20
                            && pc_moved[i] * 10 < pc_samples[i];
                        if wedged {
                            log::warn!(
                                "p{} looks wedged: the program counter moved in only {}/{} samples while {} frames ran - the game is spinning rather than running (emulator healthy; game-level stall)",
                                i + 1,
                                pc_moved[i],
                                pc_samples[i],
                                d_frames,
                            );
                        }

                        log::info!(
                            "health p{}: {} · frames {}(+{}) · tx {}(+{}) · rx {}(+{}) · slots 0x{:x} · wifi on/off {}/{} · pc {}/{}",
                            i + 1,
                            state,
                            snap.frames,
                            d_frames,
                            snap.tx,
                            d_tx,
                            snap.rx,
                            d_rx,
                            snap.connected,
                            snap.begin,
                            snap.end,
                            pc_moved[i],
                            pc_samples[i],
                        );
                        if verbose && (rx_active || d_tx > 0 || d_polls > 0) {
                            log::info!(
                                "  verbose p{}: rx bcast/cmd/reply/ack {}/{}/{}/{} (+{}/+{}/+{}/+{}) · polls {} (empty {}) · tx cmd/reply/ack +{}/+{}/+{} · rx-age {}us · arm9pc {:#010X} arm7pc {:#010X}",
                                i + 1,
                                snap.rx_bcast,
                                snap.rx_cmd,
                                snap.rx_rep,
                                snap.rx_ack,
                                snap.rx_bcast.wrapping_sub(p.rx_bcast),
                                snap.rx_cmd.wrapping_sub(p.rx_cmd),
                                snap.rx_rep.wrapping_sub(p.rx_rep),
                                snap.rx_ack.wrapping_sub(p.rx_ack),
                                snap.rx_polls,
                                snap.rx_empty,
                                snap.tx_cmd.wrapping_sub(p.tx_cmd),
                                snap.tx_rep.wrapping_sub(p.tx_rep),
                                snap.tx_ack.wrapping_sub(p.tx_ack),
                                since_rx_secs,
                                pc_prev[i].0,
                                pc_prev[i].1,
                            );
                        }
                        *p = snap;
                    }
                }
            })
            .expect("spawn health thread")
    };

    // Continuous link verification + auto-resync: the always-on "Link test".
    // Needs only the instances and the stop flag; Drop joins it with the rest.
    let linkmon = spawn_link_monitor(
        instances.clone(),
        n,
        Arc::clone(&stop),
        Arc::clone(&monitor),
    );

    let session = Arc::new(Session {
        players: n,
        rom: rom.clone(),
        title,
        jit,
        keymaps,
        flip_keys,
        instances,
        frames,
        stop: Arc::clone(&stop),
        alive: Arc::clone(&alive),
        monitor: Arc::clone(&monitor),
        threads,
        health: Some(health),
        linkmon: Some(linkmon),
        started_at: std::time::Instant::now(),
    });

    *state.session.write() = Some(Arc::clone(&session));

    Ok(SessionInfo {
        players: n,
        rom: rom.display().to_string(),
        title: session.title.clone(),
        jit,
        keymaps: session.keymaps.clone(),
        flip_keys: session.flip_keys.clone(),
    })
}

fn read_title(inst: &Instance) -> String {
    let mut buf = [0i8; 32];
    let rc = unsafe { md_rom_title(inst.ptr, buf.as_mut_ptr(), buf.len()) };
    if rc != 0 {
        return String::new();
    }
    let bytes: Vec<u8> = buf.iter().take_while(|b| **b != 0).map(|b| *b as u8).collect();
    String::from_utf8_lossy(&bytes).trim_end().to_string()
}/// Per-frame lockstep across consoles, with peer-death detection.
///
/// std::sync::Barrier has no timeout: if one console leaves the loop (the game
/// powered off, an emulation error, its thread ended) the others block in
/// wait() forever, which looks exactly like "the emulator froze and stopped
/// responding" - the frame pump keeps repainting the last frame and the health
/// log goes quiet. This version gives up once a peer is known to have stopped,
/// so a dead console ends the session loudly instead of hanging it silently.
struct Lockstep {
    state: Mutex<(usize, u64)>,
    cv: Condvar,
    players: usize,
}

impl Lockstep {
    fn new(players: usize) -> Self {
        Self {
            state: Mutex::new((0, 0)),
            cv: Condvar::new(),
            players,
        }
    }

    /// True when every console finished this frame, false when a peer stopped.
    fn wait(&self, alive: &AtomicUsize) -> bool {
        let mut g = self.state.lock();
        g.0 += 1;
        if g.0 == self.players {
            g.0 = 0;
            g.1 = g.1.wrapping_add(1);
            self.cv.notify_all();
            return true;
        }

        let generation = g.1;
        while g.1 == generation {
            if self.cv.wait_for(&mut g, Duration::from_millis(50)).timed_out()
                && alive.load(Ordering::SeqCst) < self.players
            {
                // Nobody will ever complete this generation.
                g.0 = g.0.saturating_sub(1);
                return false;
            }
        }
        true
    }
}

/// Decrements the live-console count however the run loop returns, so an early
/// exit can never leave the survivors waiting for a console that is gone.
struct AliveGuard(Arc<AtomicUsize>);

impl Drop for AliveGuard {
    fn drop(&mut self) {
        self.0.fetch_sub(1, Ordering::SeqCst);
    }
}

/// Wall-clock time a console may spend inside reply waits within a single
/// frame, in nanoseconds (negative = unbounded, stock melonDS). Set at session
/// setup, read by every console's run loop; see the comment there and
/// MPInterface::SetReplyBudget for the measurement behind it.
static REPLY_BUDGET_NS: AtomicI64 = AtomicI64::new(-1);

/// State of the always-on link verification, surfaced to the UI through
/// link_status. Plain counters: the monitor never holds the emulator.
#[derive(Default)]
struct LinkMonitor {
    /// 10s windows in which the bus was proven good - either real game traffic
    /// flowed, or (with injection on) a probe round-tripped.
    verified: AtomicU64,
    /// Windows in which a console was stalled and needed a resync kick.
    stalled: AtomicU64,
    /// Total resync kicks issued (health watchdog + monitor + monitor probes).
    resyncs: AtomicU64,
    /// Unix ms of the last verified window, 0 = none yet.
    last_ok_ms: AtomicU64,
    /// Inject probe packets while no game traffic is flowing (MD_LINK_PROBE=1).
    /// Off by default: a probe is a foreign frame in the game's own stream, and
    /// the stall kick - the part that actually cured freezes - needs no packet.
    inject: bool,
}

fn run_loop(
    player: usize,
    inst: Arc<Instance>,
    fb: Arc<Mutex<FrameBuf>>,
    lockstep: &Arc<Lockstep>,
    alive: &Arc<AtomicUsize>,
    stop: &Arc<AtomicBool>,
) {
    // Leaves the live-console count correct on every exit path below.
    let _alive_guard = AliveGuard(Arc::clone(alive));

    let mut top: *const u32 = std::ptr::null();
    let mut bottom: *const u32 = std::ptr::null();

    // Pace to the DS's real frame rate (59.826 Hz, a 528-line scanout at
    // 315/889 MHz). Without this the consoles run as fast as the CPU allows
    // - ~400 fps under the JIT - and every game plays in fast-forward.
    const FRAME_BUDGET: std::time::Duration =
        std::time::Duration::from_nanos(1_000_000_000 * 889 / 53_150);
    let mut next_frame = std::time::Instant::now();
    // How long this console may spend inside reply waits this frame, published
    // to the bridge immediately before each frame (see the session setup for
    // why it is a frame budget and not a per-window timeout).
    let reply_budget_ns = REPLY_BUDGET_NS.load(Ordering::SeqCst);

    loop {
        if stop.load(Ordering::SeqCst) {
            return;
        }

        // Link operations requested by other host threads (health watchdog,
        // selftest, link monitor) run here, on the thread that owns this
        // console, immediately before the frame that will consume their
        // effects. Nothing else may touch this console's bus state.
        inst.service_link_ops();

        // Selftest-originated wireless power requests are applied here, on the
        // thread that owns this console: wireless power is ARM7 state
        // (PowerControl7 + W_PowerUS), and the core's callback is what puts
        // the console on the link bus under its slot - see smoke_test.cpp.
        let req = inst.wireless_req.swap(-1, Ordering::SeqCst);
        if req >= 0 {
            unsafe {
                md_write_io16(inst.ptr, 7, 0x04000304, if req == 1 { 0x0002 } else { 0x0000 });
                md_write_io16(inst.ptr, 7, 0x04800036, if req == 1 { 0x0000 } else { 0x0001 });
            }
            inst.wireless_applied.store(req, Ordering::SeqCst);
        }

        // Apply this console's whole input state every frame - the same rule
        // the harness learned the hard way (one-shot taps left buttons held).
        let buttons = inst.buttons.load(Ordering::Relaxed) as u32;
        unsafe { md_set_buttons(inst.ptr, buttons) };
        {
            let touch = inst.touch.lock();
            if touch.0 {
                unsafe { md_touch(inst.ptr, touch.1, touch.2) };
            } else {
                unsafe { md_release_touch(inst.ptr) };
            }
        }

        // Charge this frame's reply waits to this frame's own budget. A reply
        // wait is wall-clock time spent inside an emulated frame, and during an
        // MKDS join 363 empty windows at 34ms came to 12.3s of a 15s window -
        // the session ran at 9fps and every peer waited for it at the barrier.
        // With the budget published the wait can use the slack the frame had
        // anyway and no more, so an unanswered window costs collection, not
        // frame rate. Only the host ever asks for replies; publishing it to
        // every console keeps the rule uniform (MPInterface::SetReplyBudget).
        unsafe { md_link_set_reply_budget(inst.ptr, reply_budget_ns) };

        unsafe { md_run_frame(inst.ptr) };

        // Snapshot the fresh picture while it is valid, then release it back to
        // the console by entering the barrier.
        unsafe {
            if md_get_screens(inst.ptr, &mut top, &mut bottom) == 0 {
                let mut buf = fb.lock();
                if !top.is_null() && !bottom.is_null() {
                    std::ptr::copy_nonoverlapping(top, buf.top.as_mut_ptr(), MD_SCREEN_PIXELS);
                    std::ptr::copy_nonoverlapping(bottom, buf.bottom.as_mut_ptr(), MD_SCREEN_PIXELS);
                }
                buf.seq = u64::from(md_frame_counter(inst.ptr));
            }
        }

        // Lockstep: every console must finish this frame before anyone starts
        // the next, which is what keeps the link's receives answerable. A peer
        // that has stopped ends the session instead of hanging it.
        if !lockstep.wait(alive) {
            log::error!(
                "p{}: a peer console stopped - lockstep dissolved ({} of {} consoles still running); \
                 frames freeze here because a lockstep session cannot continue without all players",
                player + 1,
                alive.load(Ordering::SeqCst),
                lockstep.players
            );
            return;
        }

        // Then pace: sleeping here means a faster console simply waits at this
        // sleep rather than at the next barrier, so the session advances at
        // DS speed no matter how much headroom the machine has. If we fell
        // behind (slow frame, machine load), drop the debt instead of trying
        // to catch up frame after frame.
        let now = std::time::Instant::now();
        if now < next_frame {
            std::thread::sleep(next_frame - now);
        } else {
            next_frame = now;
        }
        next_frame += FRAME_BUDGET;

        if unsafe { md_stop_reason(inst.ptr) } != MD_STOP_NONE {
            log::warn!(
                "console stopped on its own (reason {}): ending its lockstep loop, the other consoles will follow",
                unsafe { md_stop_reason(inst.ptr) }
            );
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

#[tauri::command]
fn start_session(
    state: tauri::State<'_, AppState>,
    players: u8,
    rom_path: String,
) -> Result<SessionInfo, String> {
    start_session_inner(state.inner(), players, rom_path, true)
}

/// Env-driven autostart, for CI, screenshots and hands-free verification:
/// MD_AUTOSTART_PLAYERS=2 MD_AUTOSTART_ROM=/path/to.nds
fn try_autostart(app: &tauri::AppHandle) {
    let players: u8 = std::env::var("MD_AUTOSTART_PLAYERS")
        .ok()
        .and_then(|v| v.parse().ok())
        .unwrap_or(0);
    if players == 0 {
        return;
    }
    let Some(rom) = std::env::var("MD_AUTOSTART_ROM").ok().filter(|s| !s.is_empty()) else {
        log::warn!("MD_AUTOSTART_PLAYERS set but no MD_AUTOSTART_ROM; skipping autostart");
        return;
    };
    log::info!("autostart: {players} player(s), rom {rom}");
    let state = app.state::<AppState>();
    match start_session_inner(&state, players, rom, true) {
        Ok(info) => {
            log::info!(
                "autostart ok: {} player(s), title '{}'",
                info.players,
                info.title
            );
            // The 3s-after-autostart selftest is opt-in (MD_AUTOSTART_SELFTEST=1).
            // It is a diagnostic that pokes the loaded console's emulated
            // wireless hardware - powering its radio on and off and putting test
            // frames on the bus - which is fine for the diag ROM and invasive
            // for a commercial title: MKDS powers its own radio at boot (see the
            // `wireless ON (begin #1)` line 0.7s into a session), so an
            // automatic poke lands right on top of its own wireless bring-up.
            if std::env::var("MD_AUTOSTART_SELFTEST").is_ok() {
                let app = app.clone();
                std::thread::spawn(move || {
                    std::thread::sleep(std::time::Duration::from_secs(3));
                    let state = app.state::<AppState>();
                    match link_selftest_inner(&state) {
                        Ok(v) => log::info!("link selftest: {v}"),
                        Err(err) => log::warn!("link selftest unavailable: {err}"),
                    }
                });
            } else {
                log::info!(
                    "autostart: automatic link selftest left off (it pokes the game's \
                     wireless hardware; set MD_AUTOSTART_SELFTEST=1 to run it)"
                );
            }
        }
        Err(err) => log::error!("autostart failed: {err}"),
    }
}

#[tauri::command]
fn stop_session(state: tauri::State<'_, AppState>) -> Result<(), String> {
    if state.session.write().is_some() {
        log::info!("stopping session");
        *state.session.write() = None;
    }
    Ok(())
}

#[tauri::command]
fn session_info(state: tauri::State<'_, AppState>) -> Result<Option<SessionInfo>, String> {
    Ok(state.session.read().as_ref().map(|s| SessionInfo {
        players: s.players,
        rom: s.rom.display().to_string(),
        title: s.title.clone(),
        jit: s.jit,
        keymaps: s.keymaps.clone(),
        flip_keys: s.flip_keys.clone(),
    }))
}

/// Frame request. The response is raw bytes: per player, an 8-byte little-endian
/// snapshot sequence number followed by the top framebuffer and then the bottom
/// one, each 256x192 RGBA (the bridge's 0xAARRGGBB words reinterpreted as
/// BGRA-in-memory bytes, which is exactly what a canvas ImageData wants after
/// the frontend handles the alpha channel). Raw bytes keep a 60fps read at tens
/// of MB/s instead of JSON's gigahertz of commas.
#[tauri::command]
fn get_frames(state: tauri::State<'_, AppState>) -> Result<tauri::ipc::Response, String> {
    let current = state.session.read();
    let Some(session) = current.as_ref() else {
        // No session: empty body tells the frontend to show the placeholder.
        return Ok(tauri::ipc::Response::new(Vec::new()));
    };

    let mut payload = Vec::with_capacity(session.players * (8 + MD_SCREEN_BYTES * 2));
    for fb in &session.frames {
        let buf = fb.lock();
        payload.extend_from_slice(&buf.seq.to_le_bytes());
        for px in buf.top.iter().chain(buf.bottom.iter()) {
            // Bridge pixels are 0xAARRGGBB u32s; canvas ImageData is RGBA bytes.
            let [b, g, r, a] = px.to_le_bytes();
            payload.extend_from_slice(&[r, g, b, a.max(0xFF)]);
        }
    }
    Ok(tauri::ipc::Response::new(payload))
}

#[derive(Debug, Deserialize)]
pub struct PlayerInput {
    pub player: u8,
    pub buttons: u32,
    pub touch: Option<(i32, i32)>,
}

#[tauri::command]
fn set_input(state: tauri::State<'_, AppState>, input: PlayerInput) -> Result<(), String> {
    let current = state.session.read();
    let Some(session) = current.as_ref() else {
        return Ok(()); // no session: nothing to route, the shell still animates
    };
    let Some(inst) = session.instances.get(input.player as usize) else {
        return Err(format!("no such player {}", input.player));
    };
    inst.buttons.store(input.buttons as u64, Ordering::Relaxed);
    match input.touch {
        Some((x, y)) => *inst.touch.lock() = (true, x.clamp(0, 255), y.clamp(0, 191)),
        None => *inst.touch.lock() = (false, 0, 0),
    }
    Ok(())
}

#[tauri::command]
fn link_status(state: tauri::State<'_, AppState>) -> Result<serde_json::Value, String> {
    let current = state.session.read();
    let Some(session) = current.as_ref() else {
        return Ok(serde_json::json!({ "active": false }));
    };

    let mut players = Vec::with_capacity(session.players);
    for inst in &session.instances {
        let mut stats = MDLinkStats::default();
        unsafe { md_link_stats(inst.ptr, &mut stats) };
        players.push(serde_json::json!({
            "frames": unsafe { md_frame_counter(inst.ptr) },
            "packetsSent": stats.packets_sent,
            "packetsReceived": stats.packets_received,
            "rx": {
                "broadcast": stats.rx_broadcast,
                "cmd": stats.rx_cmd,
                "reply": stats.rx_reply,
                "ack": stats.rx_ack,
                "polls": stats.rx_polls,
                "emptyPolls": stats.rx_empty_polls,
            },
            "tx": {
                "cmd": stats.tx_cmd,
                "reply": stats.tx_reply,
                "ack": stats.tx_ack,
            },
            "lastPacketAgeMs": if stats.last_rx_us >= stats.epoch_us && stats.epoch_us > 0 {
                ((stats.last_rx_us - stats.epoch_us) / 1000) as i64
            } else {
                -1i64
            },
            "wirelessOn": stats.begin_count,
            "wirelessOff": stats.end_count,
            "stopReason": unsafe { md_stop_reason(inst.ptr) },
        }));
    }

    let last_ok = session.monitor.last_ok_ms.load(Ordering::SeqCst);
    Ok(serde_json::json!({
        "active": true,
        "upSeconds": session.started_at.elapsed().as_secs(),
        "connectedSlots": unsafe { md_link_connected_mask() },
        "consolesRunning": session.alive.load(Ordering::SeqCst),
        // The always-on link verification: how many 10s windows were proven
        // good (real game traffic crossing the bus), how many needed a resync
        // kick, and when the last good window was.
        "monitor": {
            "verifiedWindows": session.monitor.verified.load(Ordering::SeqCst),
            "stalledWindows": session.monitor.stalled.load(Ordering::SeqCst),
            "resyncs": session.monitor.resyncs.load(Ordering::SeqCst),
            "lastOkMsAgo": if last_ok == 0 {
                -1i64
            } else {
                (now_ms().saturating_sub(last_ok)) as i64
            },
            "injectProbe": session.monitor.inject,
        },
        "players": players,
    }))
}

/// Host-side link selftest: power every console's wireless on, then broadcast
/// a tagged packet from every console to every peer (full mesh) and check each
/// arrives intact, timestamp included. Uses the same primitive the emulated
/// WiFi hardware calls into, so a pass proves slot routing and the LocalMP
/// queues work inside the app - not just in the C++ smoke test. Runs only on
/// demand (button / verification runs): firing packets while a real game is
/// mid-handshake could confuse it.
fn link_selftest_inner(state: &AppState) -> Result<serde_json::Value, String> {
    let current = state.session.read();
    let Some(session) = current.as_ref() else {
        return Err("no session running".into());
    };
    if session.players < 2 {
        return Err("link selftest needs at least 2 players".into());
    }

    // A live game owns this hardware, so "Link test" does the safe half while a
    // game has its wireless on. Injection means forcing a console's radio on
    // under a running game and dropping MST0 frames into the stream its own
    // driver is reading; the 2026-09-25 MKDS log shows what that costs - a test
    // frame sat on the bus for four minutes and was read by the joiner's WiFi
    // the instant its radio came up, in the middle of a lobby attempt. The half
    // that is both safe and useful there is the resync kick (FIFO re-anchor +
    // semaphore clear), which is the part that actually cured hung lobbies.
    // Injection is reserved for consoles nobody is playing: the diag ROM, or a
    // session sitting at rest with every radio off.
    let on_bus = unsafe { md_link_connected_mask() };
    if on_bus != 0 {
        let mut results = Vec::new();
        for (i, inst) in session.instances.iter().enumerate() {
            let slot = unsafe { md_link_slot(inst.ptr) };
            let resynced = inst.link_resync_now("link test (resync)");
            results.push(serde_json::json!({
                "player": i + 1,
                "wirelessOn": (on_bus & (1u32 << slot)) != 0,
                "resynced": resynced,
            }));
        }
        return Ok(serde_json::json!({
            "mode": "resync",
            "injected": false,
            "results": results,
        }));
    }

    // Wireless power-on, exactly as the smoke test does it: an ARM7 register
    // write, then wait for the core's callback to put each console on the bus
    // (its connected bit appears in md_link_connected_mask).
    let power_on = |inst: &Arc<Instance>| -> Result<(), String> {
        inst.wireless_applied.store(-1, Ordering::SeqCst);
        inst.wireless_req.store(1, Ordering::SeqCst);
        let deadline = std::time::Instant::now() + std::time::Duration::from_secs(2);
        while inst.wireless_applied.load(Ordering::SeqCst) != 1 {
            if std::time::Instant::now() > deadline {
                return Err("console never applied wireless power-on (run loop stalled?)".into());
            }
            std::thread::sleep(std::time::Duration::from_millis(2));
        }
        Ok(())
    };
    for inst in &session.instances {
        power_on(inst)?;
    }

    let wait_on_bus = |inst: &Arc<Instance>| -> Result<(), String> {
        let slot = unsafe { md_link_slot(inst.ptr) };
        let deadline = std::time::Instant::now() + std::time::Duration::from_secs(2);
        while unsafe { md_link_connected_mask() } & (1 << slot) == 0 {
            if std::time::Instant::now() > deadline {
                return Err(format!("slot {slot} never joined the bus"));
            }
            std::thread::sleep(std::time::Duration::from_millis(2));
        }
        Ok(())
    };
    for inst in &session.instances {
        wait_on_bus(inst)?;
    }

    const MAGIC: u32 = 0x4D535430; // 'MST0'
    let mut results = Vec::new();
    for from in 0..session.players {
        for to in 0..session.players {
            if from == to {
                continue;
            }
            let (from_inst, to_inst) = (&session.instances[from], &session.instances[to]);
            let nonce = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map(|d| d.subsec_nanos())
                .unwrap_or(0);
            let payload: [u32; 4] = [MAGIC, from as u32, to as u32, nonce];
            let ts = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map(|d| d.as_millis() as u64)
                .unwrap_or(0);

            // Both halves of the round trip run on the owning consoles' driver
            // threads (see LinkOp): a probe that reaches into a live console's
            // bus state from here is what made the old selftest unreliable.
            // Send reports the queued byte count: > 0 means the bus took it
            // (0 = refused, e.g. slot not attached).
            let wire = words_to_bytes(&payload);
            let sent = from_inst
                .link_send_now(&wire, ts, "link selftest")
                .map(|queued| queued > 0)
                .unwrap_or(false);

            // The bus is broadcast: every console sees every packet, and
            // earlier pairs' packets may still be queued in this receiver's
            // FIFO. Drain until we find OUR packet (magic + nonce), which both
            // proves delivery and flushes the stale ones in one pass.
            let mut received = false;
            let mut payload_ok = false;
            if sent {
                let deadline = std::time::Instant::now() + Duration::from_millis(400);
                while std::time::Instant::now() < deadline {
                    let Some(packets) = to_inst.link_drain_now("link selftest") else {
                        break;
                    };
                    for (bytes, packet_ts) in packets {
                        if bytes.len() != std::mem::size_of::<[u32; 4]>() {
                            continue;
                        }
                        let got: [u32; 4] =
                            unsafe { std::ptr::read_unaligned(bytes.as_ptr().cast()) };
                        if got[0] == MAGIC && got[3] == nonce {
                            received = true;
                            payload_ok = got == payload && packet_ts == ts;
                            break;
                        }
                        // not ours: a stale or peer packet, keep draining
                    }
                    if received {
                        break;
                    }
                    std::thread::sleep(Duration::from_millis(5));
                }
            }
            results.push(serde_json::json!({
            "from": from + 1,
            "to": to + 1,
            "sent": sent,
            "received": received,
            "payloadOk": payload_ok,
        }));
        }
    }

    // Leave the machines as we found them: the diag ROM never touches
    // wireless, so power it back off through the same run-loop channel. Then
    // flush every console - drain what is queued, then re-anchor the read
    // offset - because a test frame that outlives the test is a foreign frame
    // lying in wait for whatever the game does next, which is exactly how one
    // of these turned up inside an MKDS lobby minutes later.
    for inst in &session.instances {
        inst.wireless_applied.store(-1, Ordering::SeqCst);
        inst.wireless_req.store(0, Ordering::SeqCst);
    }
    for inst in &session.instances {
        let _ = inst.link_drain_now("link selftest flush");
    }
    for inst in &session.instances {
        let _ = inst.link_resync_now("link selftest flush");
    }

    Ok(serde_json::json!({ "mode": "inject", "injected": true, "results": results }))
}

/// Continuous link verification: the selftest the user asked to "always be
/// running". Every 10s, when at least two consoles have their wireless on
/// (they are in a multiplayer menu or session) but NO console accepted any
/// packet over the last window, it fires one probe packet and expects it on
/// the next console. Delivered = the bus works; undelivered = the same
/// drifted-FIFO/semaphore state a manual "Link test" click cured, so both
/// consoles get the resync kick. Constraints honored on purpose:
/// - never probe while game traffic flows (a probe interleaves with the
///   game's own packets and the probe read shares the console's FIFO),
/// - never probe when wireless is off (the packet would sit unread and the
///   failure would be meaningless),
/// - the game only ever sees a probe when it is idle; it validates and drops
///   malformed frames, and the semaphore post itself is the useful kick.
fn spawn_link_monitor(
    insts: Vec<Arc<Instance>>,
    _players: usize,
    stop: Arc<AtomicBool>,
    monitor: Arc<LinkMonitor>,
) -> JoinHandle<()> {
    std::thread::Builder::new()
        .name("linkmon".into())
        .spawn(move || {
            let mut last_rx: Vec<u64> = vec![0; insts.len()];
            while !stop.load(Ordering::SeqCst) {
                std::thread::sleep(Duration::from_secs(10));
                if stop.load(Ordering::SeqCst) {
                    return;
                }
                let connected = unsafe { md_link_connected_mask() };
                if connected.count_ones() < 2 {
                    continue; // nobody to verify against
                }

                // Window classification. Real traffic is the strongest possible
                // proof - packets crossed the bus and a game accepted them - so
                // a window with traffic needs no probe at all.
                let mut game_active = false;
                let mut wireless_on = false;
                for (i, inst) in insts.iter().enumerate() {
                    let mut stats = MDLinkStats::default();
                    unsafe { md_link_stats(inst.ptr, &mut stats) };
                    if stats.begin_count > stats.end_count {
                        wireless_on = true;
                    }
                    if stats.packets_received > last_rx[i] {
                        game_active = true;
                    }
                    last_rx[i] = stats.packets_received;
                }
                if game_active {
                    monitor.verified.fetch_add(1, Ordering::SeqCst);
                    monitor.last_ok_ms.store(now_ms(), Ordering::SeqCst);
                    continue;
                }
                if !wireless_on || !monitor.inject {
                    continue; // nothing to verify: radios off, or injection disabled
                }

                // Optional probe (MD_LINK_PROBE=1). Runs only in a window with
                // no game traffic, and sends/receives through the consoles'
                // own driver threads like everything else here.
                let nonce = now_ms() as u32 | 1;
                let payload: [u32; 4] = [0x4D535430 /* 'MST0' */, 0, 1, nonce];
                let wire = words_to_bytes(&payload);
                let ts = now_ms();
                let sent = insts[0]
                    .link_send_now(&wire, ts, "link monitor")
                    .map(|queued| queued > 0)
                    .unwrap_or(false);
                let mut ok = false;
                if sent {
                    let deadline = std::time::Instant::now() + Duration::from_millis(300);
                    while std::time::Instant::now() < deadline {
                        match insts[1].link_drain_now("link monitor") {
                            Some(packets) => {
                                if packets.iter().any(|(bytes, _)| *bytes == wire) {
                                    ok = true;
                                    break;
                                }
                            }
                            None => break,
                        }
                        std::thread::sleep(Duration::from_millis(20));
                    }
                }
                if ok {
                    monitor.verified.fetch_add(1, Ordering::SeqCst);
                    monitor.last_ok_ms.store(now_ms(), Ordering::SeqCst);
                } else {
                    log::warn!(
                        "link monitor: bus probe p1->p2 not delivered - resyncing both consoles"
                    );
                    let a = insts[0].link_resync_now("link monitor");
                    let b = insts[1].link_resync_now("link monitor");
                    if a || b {
                        monitor.resyncs.fetch_add(1, Ordering::SeqCst);
                    }
                }
            }
        })
        .expect("spawn link monitor")
}

#[tauri::command]
fn link_selftest(state: tauri::State<'_, AppState>) -> Result<serde_json::Value, String> {
    // Logged here as well as in the autostart path: without this, a "Link test"
    // click leaves no trace in /tmp/md_app.log, and its side effects (power
    // pokes, frames on the bus) show up attributed to nobody. That cost an hour
    // of log reading on 2026-09-25.
    let out = link_selftest_inner(state.inner());
    match &out {
        Ok(v) => log::info!("link selftest: {v}"),
        Err(err) => log::warn!("link selftest unavailable: {err}"),
    }
    out
}

#[tauri::command]
fn set_jit(state: tauri::State<'_, AppState>, enabled: bool) -> Result<(), String> {
    let current = state.session.read();
    let Some(session) = current.as_ref() else {
        return Err("no session running".into());
    };
    for inst in &session.instances {
        if unsafe { md_set_jit(inst.ptr, enabled as i32) } != 0 {
            return Err("md_set_jit failed".into());
        }
    }
    log::info!("JIT {}", if enabled { "enabled" } else { "disabled" });
    Ok(())
}

// Kept from the placeholder shell; wired to real implementations in Phase 3.
#[tauri::command]
async fn save_state(save_path: String) -> Result<String, String> {
    log::info!("save_state stub: {save_path}");
    Ok("not implemented yet (Phase 3)".into())
}

#[tauri::command]
async fn load_state(save_path: String) -> Result<String, String> {
    log::info!("load_state stub: {save_path}");
    Ok("not implemented yet (Phase 3)".into())
}

#[tauri::command]
async fn set_view_mode(mode: String) -> Result<String, String> {
    log::info!("view mode: {mode}");
    Ok(mode)
}

#[tauri::command]
fn quit_app(app: tauri::AppHandle) -> Result<(), String> {
    log::info!("quitting");
    app.exit(0);
    Ok(())
}

/// Initial UI layout config. The persisted frontend layout choice (Tab key,
/// localStorage) always wins; MD_SHOW_BOTTOM=1 only sets the first-run default
/// for the "side-by-side" layout mode.
#[tauri::command]
fn ui_config() -> Result<serde_json::Value, String> {
    let show_bottom = std::env::var("MD_SHOW_BOTTOM")
        .map(|v| v == "1" || v.eq_ignore_ascii_case("true"))
        .unwrap_or(false);
    Ok(serde_json::json!({ "showBottom": show_bottom }))
}

// ---------------------------------------------------------------------------
// Crash forensics, liveness, and the always-on link report
// ---------------------------------------------------------------------------
//
// Three sessions in a row ended with the app gone and *nothing* in the log: a
// native crash leaves no Rust panic message, no kernel line (dmesg is
// restricted here), and no coredump (systemd-coredump's size limit is smaller
// than a JIT'd pair of consoles). The tools below exist so the next failure
// explains itself.

/// Unix milliseconds since the epoch, 0 on a clock error.
fn now_ms() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_millis() as u64)
        .unwrap_or(0)
}

/// Little-endian bytes of a word array - the wire form the link bus speaks.
fn words_to_bytes(words: &[u32]) -> Vec<u8> {
    let mut out = Vec::with_capacity(words.len() * 4);
    for w in words {
        out.extend_from_slice(&w.to_le_bytes());
    }
    out
}

/// (resident KiB, thread count) from /proc, best effort - a leak or a thread
/// explosion shows up in the heartbeat before it shows up as a crash.
fn proc_stats() -> (u64, u64) {
    let rss_kb = std::fs::read_to_string("/proc/self/statm")
        .ok()
        .and_then(|s| s.split_whitespace().nth(1).and_then(|v| v.parse::<u64>().ok()))
        .map(|pages| pages * 4)
        .unwrap_or(0);
    let threads = std::fs::read_to_string("/proc/self/status")
        .ok()
        .and_then(|s| {
            s.lines()
                .find(|l| l.starts_with("Threads:"))
                .and_then(|l| l.split_whitespace().nth(1).and_then(|v| v.parse::<u64>().ok()))
        })
        .unwrap_or(0);
    (rss_kb, threads)
}

/// Append a crash/panic report to the log and to its own file. The file
/// matters: a crash that takes the process down can lose whatever stderr had
/// not been flushed yet, and an unflushed log is exactly the evidence we need.
fn write_crash_report(text: &str) {
    eprintln!("{text}");
    let path = std::env::var("MD_CRASH_LOG")
        .unwrap_or_else(|_| format!("/tmp/md_crash_{}.txt", std::process::id()));
    if let Ok(mut f) = std::fs::OpenOptions::new().create(true).append(true).open(&path) {
        use std::io::Write;
        let _ = writeln!(
            f,
            "[{:?}] {text}",
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map(|d| d.as_secs())
                .unwrap_or(0)
        );
    }
}

#[cfg(unix)]
static CRASH_FD: AtomicI32 = AtomicI32::new(-1);

#[cfg(unix)]
extern "C" {
    fn backtrace(buffer: *mut *mut libc::c_void, size: i32) -> i32;
    fn backtrace_symbols_fd(buffer: *const *mut libc::c_void, size: i32, fd: i32);
}

/// Signal handler for fatal faults in the C++ core. Async-signal-safe only: no
/// allocation, no locks, no Rust formatting - write(2) and the unwinder.
#[cfg(unix)]
extern "C" fn crash_signal_handler(sig: i32, _info: *mut libc::siginfo_t, _ctx: *mut libc::c_void) {
    const PREFIX: &[u8] = b"md-crash: FATAL SIGNAL ";
    const SUFFIX: &[u8] = b" - native backtrace follows (build with -rdynamic for names)\n";

    unsafe {
        let fd = CRASH_FD.load(Ordering::Relaxed);
        for out in [2i32, fd] {
            if out < 0 {
                continue;
            }
            let _ = libc::write(out, PREFIX.as_ptr().cast(), PREFIX.len());

            let mut digits = [0u8; 8];
            let mut n = 0usize;
            let mut v = sig.unsigned_abs();
            if v == 0 {
                digits[0] = b'0';
                n = 1;
            } else {
                let mut tmp = [0u8; 8];
                let mut t = 0usize;
                while v > 0 {
                    tmp[t] = b'0' + (v % 10) as u8;
                    v /= 10;
                    t += 1;
                }
                while t > 0 {
                    t -= 1;
                    digits[n] = tmp[t];
                    n += 1;
                }
            }
            let _ = libc::write(out, digits.as_ptr().cast(), n);
            let _ = libc::write(out, SUFFIX.as_ptr().cast(), SUFFIX.len());

            let mut frames = [std::ptr::null_mut::<libc::c_void>(); 96];
            let count = backtrace(frames.as_mut_ptr(), frames.len() as i32);
            if count > 0 {
                backtrace_symbols_fd(frames.as_ptr(), count, out);
            }
            let end: &[u8] = b"md-crash: end of report\n";
            let _ = libc::write(out, end.as_ptr().cast(), end.len());
        }
        libc::_exit(128 + sig);
    }
}

#[cfg(unix)]
const FATAL_SIGNALS: [i32; 6] = [
    libc::SIGSEGV,
    libc::SIGBUS,
    libc::SIGILL,
    libc::SIGFPE,
    libc::SIGABRT,
    libc::SIGTRAP,
];

/// Who currently owns a signal: us, nobody, or another component.
#[cfg(unix)]
#[derive(Clone, Copy, PartialEq, Eq)]
enum SignalOwner {
    Ours,
    Default,
    Ignored,
    Other(usize),
}

#[cfg(unix)]
fn signal_owner(sig: i32) -> SignalOwner {
    let mut current: libc::sigaction = unsafe { std::mem::zeroed() };
    unsafe { libc::sigaction(sig, std::ptr::null(), &mut current) };
    let handler = current.sa_sigaction;
    if handler == crash_signal_handler as *const () as usize {
        SignalOwner::Ours
    } else if handler == libc::SIG_DFL {
        SignalOwner::Default
    } else if handler == libc::SIG_IGN {
        SignalOwner::Ignored
    } else {
        SignalOwner::Other(handler)
    }
}

/// Claim the fatal signals - but only the ones nobody is using.
///
/// This distinction is load-bearing, and getting it wrong cost a session:
/// **melonDS's own ARM JIT owns SIGSEGV.** Its fast-memory path installs
/// `ARMJIT_Memory::SigsegvHandler` and remaps lazily-mapped regions when the
/// guest touches them; faults it cannot handle are chained to whichever handler
/// was current when it registered (see `OldSaSegv` in ARMJIT_Memory.cpp).
/// Installing our handler *after* the JIT's, or re-installing it on a timer,
/// seizes fastmem's faults from it and turns the emulator's normal lazy-map
/// traffic into a process exit - which is exactly what an earlier revision of
/// this file did, killing the app about 30 seconds after every session start.
///
/// The right order is therefore: install first, at startup, while SIGSEGV is
/// still SIG_DFL. The JIT then registers on top, saves us as the previous
/// handler, and chains genuine faults down to us - so a real crash still
/// produces a report, and fastmem keeps working.
#[cfg(unix)]
fn install_signal_handlers(stage: &str) {
    let mut claimed: Vec<i32> = Vec::new();
    let mut left_alone: Vec<(i32, usize)> = Vec::new();
    for sig in FATAL_SIGNALS {
        match signal_owner(sig) {
            SignalOwner::Ours => claimed.push(sig),
            SignalOwner::Other(addr) => left_alone.push((sig, addr)),
            SignalOwner::Default | SignalOwner::Ignored => unsafe {
                let mut action: libc::sigaction = std::mem::zeroed();
                action.sa_sigaction = crash_signal_handler as *const () as usize;
                action.sa_flags = libc::SA_SIGINFO | libc::SA_ONSTACK;
                libc::sigemptyset(&mut action.sa_mask);
                if libc::sigaction(sig, &action, std::ptr::null_mut()) == 0 {
                    claimed.push(sig);
                }
            },
        }
    }

    log::info!("crash handlers ({stage}): ours for {claimed:?}");
    for (sig, addr) in left_alone {
        log::info!(
            "crash handlers ({stage}): signal {sig} is already owned by another component (handler at \
             {addr:#x}) - left alone on purpose; that component chains to us for faults it cannot handle"
        );
    }
}

/// Audit only: reports when a signal we used to own changed hands. It
/// deliberately does NOT re-install - the ARM JIT legitimately takes SIGSEGV
/// when a session starts, and fighting it for the signal is what broke fastmem.
#[cfg(unix)]
fn audit_signal_handlers() {
    static LAST: AtomicU64 = AtomicU64::new(0);

    let mut code = 0u64;
    let mut detail = Vec::new();
    for (i, sig) in FATAL_SIGNALS.iter().enumerate() {
        if let SignalOwner::Other(addr) = signal_owner(*sig) {
            code |= 1 << i;
            detail.push(format!("sig {sig} -> {addr:#x}"));
        }
    }
    if code != LAST.swap(code, Ordering::Relaxed) {
        if code == 0 {
            log::info!("crash handlers: all fatal signals are ours again");
        } else {
            log::warn!(
                "crash handlers: another component owns {detail:?}. While a session runs, SIGSEGV belongs to \
                 the emulator's ARM JIT fastmem handler - expected, and left alone on purpose. Caveat worth \
                 knowing: the JIT chains unhandled faults to whichever handler was current when it registered \
                 (Rust's std stack-overflow handler, installed before main), not to ours, so a genuine SIGSEGV \
                 ends the process without one of our reports - the launcher's exit-status line is what \
                 identifies that case"
            );
        }
    }
}

#[cfg(unix)]
fn install_crash_handlers() {
    // Its own file descriptor for the report, opened before anything can crash.
    let path = std::env::var("MD_CRASH_LOG")
        .unwrap_or_else(|_| format!("/tmp/md_crash_{}.txt", std::process::id()));
    if let Ok(cpath) = std::ffi::CString::new(path.clone()) {
        let fd = unsafe {
            libc::open(
                cpath.as_ptr(),
                libc::O_WRONLY | libc::O_CREAT | libc::O_APPEND,
                0o644,
            )
        };
        if fd >= 0 {
            CRASH_FD.store(fd, Ordering::Relaxed);
            log::info!("crash reports will be written to {path}");
        }
    }

    unsafe {
        // A stack of our own: a stack-overflow SIGSEGV cannot run its handler
        // on the stack that overflowed.
        let size = 256 * 1024;
        let stack = libc::malloc(size);
        if !stack.is_null() {
            let ss = libc::stack_t {
                ss_sp: stack,
                ss_flags: 0,
                ss_size: size,
            };
            libc::sigaltstack(&ss, std::ptr::null_mut());
        }

        // Ask for cores as well, when the system permits them.
        let lim = libc::rlimit {
            rlim_cur: libc::RLIM_INFINITY,
            rlim_max: libc::RLIM_INFINITY,
        };
        libc::setrlimit(libc::RLIMIT_CORE, &lim);
    }

    install_signal_handlers("before webview");

    // Rust-side panics (command handlers, worker threads) get the same
    // treatment: full message plus a symbolic backtrace, in the log file. Set
    // once - setting it twice would chain the previous hook and print twice.
    let default_hook = std::panic::take_hook();
    std::panic::set_hook(Box::new(move |info| {
        let bt = std::backtrace::Backtrace::force_capture();
        write_crash_report(&format!("HOST PANIC: {info}\n{bt}"));
        default_hook(info);
    }));
    log::info!("panic hook installed (backtrace goes to the crash report)");
}

#[cfg(not(unix))]
fn install_crash_handlers() {}

#[cfg(not(unix))]
fn install_signal_handlers(_stage: &str) {}

/// Liveness beacon from the webview (see ui_heartbeat).
static UI_BEAT_MS: AtomicU64 = AtomicU64::new(0);
static UI_BEAT_SEQ: AtomicU64 = AtomicU64::new(0);

/// Called by the frontend on a timer. Its absence is a signal: the heartbeat
/// thread below reports how long the window has been quiet, which separates
/// "the webview died / froze" from "the emulation died".
#[tauri::command]
fn ui_heartbeat(seq: u64) {
    UI_BEAT_SEQ.store(seq, Ordering::Relaxed);
    UI_BEAT_MS.store(now_ms(), Ordering::Relaxed);
}

/// Any problem the frontend notices (uncaught error, failed IPC, a repaint that
/// threw) reported into the host log, where the post-mortem looks.
#[tauri::command]
fn frontend_log(level: String, message: String) {
    match level.as_str() {
        "error" => log::error!("[ui] {message}"),
        "warn" => log::warn!("[ui] {message}"),
        _ => log::info!("[ui] {message}"),
    }
}

/// Every 30s: frames per console, live-console count, RSS, thread count, and how
/// recently the webview checked in. Frames advancing + a quiet window is a
/// webview problem; frames frozen is a lockstep/emulation problem; the process
/// simply stopping mid-line is a native fault (see install_crash_handlers).
fn spawn_heartbeat(app: tauri::AppHandle) {
    let spawned = std::thread::Builder::new()
        .name("heartbeat".into())
        .spawn(move || {
            let mut beat = 0u64;
            let mut last_frames: Vec<u32> = Vec::new();
            loop {
                std::thread::sleep(Duration::from_secs(30));
                beat += 1;

                // Audit (never re-install): the ARM JIT owns SIGSEGV once a
                // session starts, and taking it back would break fastmem.
                #[cfg(unix)]
                audit_signal_handlers();

                let (rss_kb, threads) = proc_stats();
                let ui_age = {
                    let t = UI_BEAT_MS.load(Ordering::Relaxed);
                    if t == 0 {
                        -1i64
                    } else {
                        (now_ms().saturating_sub(t) / 1000) as i64
                    }
                };

                let state = app.state::<AppState>();
                let current = state.session.read();
                match current.as_ref() {
                    None => log::info!(
                        "heartbeat #{beat}: no session · rss {}MB · threads {threads} · ui {}s ago",
                        rss_kb / 1024,
                        ui_age
                    ),
                    Some(session) => {
                        let frames: Vec<u32> = session
                            .instances
                            .iter()
                            .map(|i| unsafe { md_frame_counter(i.ptr) })
                            .collect();
                        // The first beat only establishes the baseline: a zero
                        // delta there would be read as a wedge that has not
                        // happened.
                        let have_baseline = last_frames.len() == frames.len();
                        let deltas: Vec<u32> = frames
                            .iter()
                            .enumerate()
                            .map(|(i, f)| {
                                last_frames
                                    .get(i)
                                    .map(|prev| f.wrapping_sub(*prev))
                                    .unwrap_or(0)
                            })
                            .collect();
                        log::info!(
                            "heartbeat #{beat}: frames {:?} (+{:?} per 30s) · consoles {}/{} · kicks {} · rss {}MB · threads {threads} · ui {}s ago",
                            frames,
                            deltas,
                            session.alive.load(Ordering::SeqCst),
                            session.players,
                            session.monitor.resyncs.load(Ordering::SeqCst),
                            rss_kb / 1024,
                            ui_age
                        );
                        if have_baseline && deltas.iter().any(|d| *d == 0) {
                            log::warn!(
                                "heartbeat: a console produced no frames in the last 30s - wedged or waiting on a peer"
                            );
                        }
                        if ui_age > 20 {
                            log::warn!(
                                "heartbeat: the webview has been quiet for {ui_age}s - the window may be frozen or gone while emulation continues"
                            );
                        }
                        last_frames = frames;
                    }
                }
            }
        });
    if let Err(err) = spawned {
        log::warn!("could not start the heartbeat thread: {err}");
    }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    env_logger::init();
    install_crash_handlers();
    log::info!(
        "melonDS Multiplayer {} starting (pid {})",
        env!("CARGO_PKG_VERSION"),
        std::process::id()
    );
    if let Ok(exe) = std::env::current_exe() {
        let built = std::fs::metadata(&exe).and_then(|m| m.modified()).ok();
        log::info!("binary: {} (built {:?})", exe.display(), built);
    }

    let app = match tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_fs::init())
        .manage(AppState::default())
        .invoke_handler(tauri::generate_handler![
            start_session,
            stop_session,
            session_info,
            get_frames,
            set_input,
            link_status,
            link_selftest,
            set_jit,
            save_state,
            load_state,
            set_view_mode,
            quit_app,
            ui_config,
            ui_heartbeat,
            frontend_log,
        ])
        .setup(|app| {
            try_autostart(app.handle());
            Ok(())
        })
        .build(tauri::generate_context!())
    {
        Ok(app) => app,
        Err(err) => {
            write_crash_report(&format!("FATAL: could not build the Tauri app: {err}"));
            return;
        }
    };

    // Again after the webview is up, but only for signals still unowned: a
    // component that took one keeps it, and the JIT (which registers when a
    // console starts) must be able to save us as the previous handler.
    install_signal_handlers("after webview");

    spawn_heartbeat(app.handle().clone());

    // Window and exit events are the difference between "the user closed it"
    // and "it died": a clean close leaves a line here, a crash leaves none.
    app.run(|_app, event| match event {
        tauri::RunEvent::WindowEvent { label, event, .. } => match event {
            tauri::WindowEvent::CloseRequested { .. } => {
                log::warn!("window '{label}': close requested")
            }
            tauri::WindowEvent::Destroyed => log::warn!("window '{label}': destroyed"),
            _ => {}
        },
        tauri::RunEvent::ExitRequested { .. } => log::warn!("exit requested"),
        tauri::RunEvent::Exit => log::warn!("event loop exiting - process ending normally"),
        _ => {}
    });
}

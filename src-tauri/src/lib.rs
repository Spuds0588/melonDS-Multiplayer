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
use std::collections::BTreeMap;
use std::io::{BufRead, BufReader, Write};
use std::net::{TcpListener, TcpStream};
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

/// The multiplayer flight recorder (md_link_mp_digest in melonds_bridge.h):
/// what each game actually did with the link, as opposed to what the wire
/// carried. Field order must match the C struct exactly.
#[repr(C)]
#[derive(Clone, Copy, Default)]
struct MDMPDigest {
    role: u32, // 0 = not in MP, 1 = host, 2 = guest
    cmds_rx: u64,
    cmds_rx_dup: u64,
    answers_sent: u64,
    answers_blank: u64,
    answers_canned: u64,
    arms: u64,
    aids_set: u64,
    window_reads: u64,
    deauth_sent: u64,
    deauth_recv: u64,
    cmds_tx: u64,
    cmds_tx_dup: u64,
    acks_sent: u64,
    acks_client_fail: u64,
    replies_written: u64,
    replies_collected: u64,
    replies_blank: u64,
    windows_empty: u64,
    stale_dropped: u64,
    reanchor_dropped: u64,
    not_looking: u64,
    last_cmd_seq: u16,
    last_cmd_len: u16,
    last_cmd_body: [u8; 16],
    last_answer_len: u16,
    last_answer_body: [u8; 16],
    last_deauth_reason: u16,
    last_ack_clientfail: u16,
    last_cmd_us: u64,
    last_answer_us: u64,
    // interrupt level (see the C struct): per-source raises and acks, then the
    // wifi register state at the instant the digest was taken.
    irq_raised: [u64; 16],
    irq_acked: [u64; 16],
    irq_cpu_line: u64,
    block_beacon_irq14: i32,
    w_if: u16,
    w_ie: u16,
    w_uscntcnt: u16,
    w_uscmpcnt: u16,
    w_bcncount1: u16,
    w_bcncount2: u16,
    w_txslot_beacon: u16,
    w_txslot_reply1: u16,
    w_txslot_reply2: u16,
    w_txbusy: u16,
    w_txreqread: u16,
    w_powerstate: u16,
    w_powertx: u16,
    comstatus: u16,
    uscounter: u64,
    uscompare: u64,
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
    fn md_link_mp_digest(inst: *mut MDInstance, out: *mut MDMPDigest) -> i32;
    fn md_link_mp_digest_size() -> u64;
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
    fn md_read_io16(inst: *mut MDInstance, cpu: i32, addr: u32) -> u16;
    fn md_wifi_io_report(inst: *mut MDInstance, out: *mut i8, cap: i32) -> i32;
    fn md_link_mp_vary(
        inst: *mut MDInstance,
        kind: i32,
        value: i32,
        body: *const u8,
        bodylen: i32,
        armed: *mut i32,
        left: *mut i32,
        applied: *mut u64,
    ) -> i32;
    fn md_read_gpu(inst: *mut MDInstance, addr: u32) -> u32;
    fn md_link_slot(inst: *mut MDInstance) -> i32;
}

/// Hex for a frame body, clamped to the 16 bytes the digest carries.
fn mp_body_hex(body: &[u8; 16], len: u16) -> String {
    let n = (len as usize).min(body.len());
    if n == 0 {
        return "-".to_string();
    }

    let mut s = String::with_capacity(n * 3);
    for (i, b) in body[..n].iter().enumerate() {
        if i > 0 {
            s.push(' ');
        }
        s.push_str(&format!("{:02X}", b));
    }
    if (len as usize) > n {
        s.push_str(" ..");
    }
    s
}

fn mp_secs(us: u64) -> String {
    format!("{:.2}s", us as f64 / 1e6)
}

/// One console's multiplayer flight recorder, as two log lines.
///
/// This exists because thirteen live attempts were each read back by hand from
/// a multi-megabyte log to answer the same short list of questions: did the
/// guest's game get a command, did it answer with its own bytes, did the host
/// read that answer back, and did either body ever move. Whoever reads the next
/// failed run should get all of it, plus deltas since the last window, from the
/// last few lines of the log.
fn mp_digest_lines(p: usize, d: &MDMPDigest, prev: &MDMPDigest) -> Vec<String> {
    let role = match d.role {
        1 => "host",
        2 => "guest",
        _ => "not in MP",
    };
    let d64 = |now: u64, then: u64| now.wrapping_sub(then);

    let mut counters = format!(
        "mp p{} [{}]: cmds rx {}(+{}, dup {}) · answered {}(+{}, blank {}, canned {}) · arms {} aids {} · window reads {}(+{}) · replies sent {} collected {}(+{}) empty {}(+{}) stale {} reanchor-dropped {} blank {} notlooking {} · deauth out {} in {}",
        p + 1,
        role,
        d.cmds_rx,
        d64(d.cmds_rx, prev.cmds_rx),
        d.cmds_rx_dup,
        d.answers_sent,
        d64(d.answers_sent, prev.answers_sent),
        d.answers_blank,
        d.answers_canned,
        d.arms,
        d.aids_set,
        d.window_reads,
        d64(d.window_reads, prev.window_reads),
        d.replies_written,
        d.replies_collected,
        d64(d.replies_collected, prev.replies_collected),
        d.windows_empty,
        d64(d.windows_empty, prev.windows_empty),
        d.stale_dropped,
        d.reanchor_dropped,
        d.replies_blank,
        d.not_looking,
        d.deauth_sent,
        d.deauth_recv,
    );

    // The host half only when the console is (or was) one: a console that never
    // transmitted a command has nothing to say here.
    if d.cmds_tx > 0 {
        counters.push_str(&format!(
            " · host side: cmds tx {}(+{}, dup {}) · acks {}(+{}, clientfail {})",
            d.cmds_tx,
            d64(d.cmds_tx, prev.cmds_tx),
            d.cmds_tx_dup,
            d.acks_sent,
            d64(d.acks_sent, prev.acks_sent),
            d.acks_client_fail,
        ));
    }

    // A console that has never touched the link (menus, single player) prints
    // nothing at all: the digest is meant to be the two lines that explain a
    // multiplayer failure, not four lines of zeros every window.
    let silent = (d.role == 0)
        && (d.cmds_rx == 0)
        && (d.cmds_tx == 0)
        && (d.answers_sent == 0)
        && (d.window_reads == 0);
    if silent {
        return Vec::new();
    }

    /* The interrupt level, on its own line: which wifi source asked this
       console's CPU for attention since the last window (`raised`, with the
       sources that have ever fired), which ones its handler acknowledged
       (`acked`), and the register state where a game that is waiting would be
       waiting. A console whose handler still runs shows `raised` and `acked`
       climbing together and its arm counter above climbing with them; a
       console waiting on an interrupt it never gets shows the source it needs
       at a flat zero next to a nonzero pending bit in `W_IF`/`W_IE`.
       Only sources that have ever fired are listed, so the line stays
       readable, and `pending` is the subset of W_IF the game has enabled. */
    let irq_list = |arr: &[u64; 16], pv: &[u64; 16]| -> String {
        let mut out = Vec::new();
        for i in 0..16 {
            if arr[i] > 0 || pv[i] > 0 {
                out.push(format!("{}:{:+}", i, d64(arr[i], pv[i])));
            }
        }
        if out.is_empty() {
            "none".to_string()
        } else {
            out.join(" ")
        }
    };
    let irq = format!(
        "mp p{} irq: cpu line {}(+{}) · raised {} · acked {} · IF {:04X} IE {:04X} pending {:04X}",
        p + 1,
        d.irq_cpu_line,
        d64(d.irq_cpu_line, prev.irq_cpu_line),
        irq_list(&d.irq_raised, &prev.irq_raised),
        irq_list(&d.irq_acked, &prev.irq_acked),
        d.w_if,
        d.w_ie,
        d.w_if & d.w_ie,
    );
    let hw = format!(
        "mp p{} hw: uscnt {} uscmp {} us {:X}/{:X}{} · bcn {} {} · slot b {:04X} r1 {:04X} r2 {:04X} · txbusy {:04X} req {:04X} com {} pwr {:04X}/{:04X}",
        p + 1,
        d.w_uscntcnt,
        d.w_uscmpcnt,
        d.uscounter,
        d.uscompare,
        if d.block_beacon_irq14 != 0 { " bcn-irq14-blocked" } else { "" },
        d.w_bcncount1,
        d.w_bcncount2,
        d.w_txslot_beacon,
        d.w_txslot_reply1,
        d.w_txslot_reply2,
        d.w_txbusy,
        d.w_txreqread,
        d.comstatus,
        d.w_powerstate,
        d.w_powertx,
    );

    let at = |us: u64| {
        if us > 0 {
            format!("+{}", mp_secs(us))
        } else {
            "-".to_string()
        }
    };

    let mut last = format!(
        "mp p{} last: cmd seq {:04X} len {} body {} at {}",
        p + 1,
        d.last_cmd_seq,
        d.last_cmd_len,
        mp_body_hex(&d.last_cmd_body, d.last_cmd_len),
        at(d.last_cmd_us),
    );
    last.push_str(&format!(
        " · answer len {} body {} at {}",
        d.last_answer_len,
        mp_body_hex(&d.last_answer_body, d.last_answer_len),
        at(d.last_answer_us),
    ));
    // The host's own view of whether its game was told the guest failed.
    if d.acks_sent > 0 {
        last.push_str(&format!(" · last ack clientfail {:04X}", d.last_ack_clientfail));
    }
    if (d.deauth_sent > 0) || (d.deauth_recv > 0) {
        last.push_str(&format!(" · last deauth reason {:04X}", d.last_deauth_reason));
    }

    vec![counters, irq, hw, last]
}

fn mp_digest(p: usize, inst: *mut MDInstance, prev: &MDMPDigest) -> MDMPDigest {
    let mut digest = MDMPDigest::default();
    if unsafe { md_link_mp_digest(inst, &mut digest) } != 0 {
        return digest;
    }

    for line in mp_digest_lines(p, &digest, prev) {
        log::info!("{}", line);
    }

    digest
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
    /// Picture liveness. The console frame counter above keeps climbing in a
    /// game that has wedged or is parked on a static screen - the frame pump
    /// cannot tell - so liveness is measured from the picture itself: how many
    /// frames changed at all (`beats`), how long it has been since the last one
    /// (`last_beat_frame`, in console frames), and how much of the sampled
    /// picture moved (`changed`, in pixels). A run whose digest is green and
    /// whose picture has not changed in 500 frames is a frozen game, not a
    /// healthy link.
    beats: u64,
    changed: u64,
    last_beat_frame: u64,
    /// Last frame's strided pixel sample (see SAMPLE_STRIDE).
    sample: Vec<u32>,
}

/// Every Nth pixel of each screen is compared. 1 in 16 detects any sprite or
/// text movement at a cost small enough to run on every frame of every
/// console.
const SAMPLE_STRIDE: usize = 16;
const SAMPLE_PIXELS: usize = MD_SCREEN_PIXELS / SAMPLE_STRIDE;

impl FrameBuf {
    fn new() -> Self {
        Self {
            top: vec![0; MD_SCREEN_PIXELS],
            bottom: vec![0; MD_SCREEN_PIXELS],
            seq: 0,
            beats: 0,
            changed: 0,
            last_beat_frame: 0,
            sample: vec![0; SAMPLE_PIXELS * 2],
        }
    }

    /// Counts this frame against liveness. Returns the console frame counter
    /// the picture last changed at, so a caller can say how long ago that was.
    fn note_picture(&mut self) {
        let mut moved = 0u64;
        for i in 0..SAMPLE_PIXELS {
            let idx = i * SAMPLE_STRIDE;
            let top = self.top[idx];
            let bottom = self.bottom[idx];
            if self.sample[i] != top {
                self.sample[i] = top;
                moved += 1;
            }
            if self.sample[SAMPLE_PIXELS + i] != bottom {
                self.sample[SAMPLE_PIXELS + i] = bottom;
                moved += 1;
            }
        }

        if moved > 0 {
            self.beats += 1;
            self.changed += moved;
            self.last_beat_frame = self.seq;
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

        // Prove the digest structs agree before any of their numbers are
        // believed. A layout mismatch would not crash - it would just make the
        // flight recorder lie, which is the one thing it cannot be allowed to do
        // (this project already lost a session to an FFI declaration mismatch:
        // md_link_recv_packet's `int` read as `usize`).
        {
            let c_size = md_link_mp_digest_size();
            let rust_size = std::mem::size_of::<MDMPDigest>() as u64;
            if c_size == rust_size {
                log::info!(
                    "mp digest struct: {} bytes in C and Rust alike - the flight recorder's numbers can be trusted",
                    c_size
                );
            } else {
                log::error!(
                    "mp digest struct MISMATCH: C says {} bytes, Rust says {} - every digest number will be wrong until md_link_mp_digest's struct is fixed in both places",
                    c_size,
                    rust_size
                );
            }
        }
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
        const DEFAULT_REPLY_TIMEOUT_MS: i32 = 34;
        let timeout_override = std::env::var("MD_REPLY_TIMEOUT_MS")
            .ok()
            .and_then(|v| v.trim().parse::<i32>().ok())
            .filter(|ms| *ms >= 0);
        let reply_timeout_ms = timeout_override.unwrap_or(DEFAULT_REPLY_TIMEOUT_MS);
        // One emulated frame is 1000*889/53150 = 16.73ms of wall clock, so say
        // where this number sits in those units and where it came from. The
        // line used to end "(2 emulated frames at 59.826fps)" for *every*
        // value, so a sweep at MD_REPLY_TIMEOUT_MS=2 read as two frames when it
        // is an eighth of one - which is how "the app runs a 2ms ceiling" got
        // as far as it did.
        log::info!(
            "link reply timeout: {reply_timeout_ms}ms = {:.2} emulated frames ({})",
            reply_timeout_ms as f64 * 53_150.0 / (1000.0 * 889.0),
            if timeout_override.is_some() { "MD_REPLY_TIMEOUT_MS" } else { "default" },
        );
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
        // Override with MD_REPLY_BUDGET_FRAMES to sweep without a rebuild, or
        // set it negative to leave the waits unbounded (the same thing
        // MD_REPLY_BUDGET_NS=-1 means in the harness). The knob counts
        // *emulated frames* - the unit the budget is stated in - and it used to
        // be called MD_REPLY_BUDGET_MS while its value was read as a frame
        // count, so a sweep at 17 bought 17 frames (284ms) rather than 17ms and
        // a sweep at -1 bought the default rather than "no cap".
        let frames_of_slack = std::env::var("MD_REPLY_BUDGET_FRAMES")
            .ok()
            .and_then(|v| v.trim().parse::<i64>().ok())
            .unwrap_or(2);
        REPLY_BUDGET_NS.store(
            if frames_of_slack < 0 {
                -1
            } else {
                frames_of_slack * 1_000_000_000 * 889 / 53_150
            },
            Ordering::SeqCst,
        );
        if frames_of_slack < 0 {
            log::info!(
                "link reply budget: unbounded (MD_REPLY_BUDGET_FRAMES<0) - every window may spend its full reply timeout"
            );
        } else {
            log::info!(
                "link reply budget: {frames_of_slack} emulated frame(s) of wall clock per frame for a whole console's reply waits"
            );
        }
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
        let health_frames = frames.clone();
        let health_stop = Arc::clone(&stop);
        let health_monitor = Arc::clone(&monitor);
        std::thread::Builder::new()
            .name("health".into())
            .spawn(move || {
                #[derive(Clone, Copy, Default)]
                struct Snapshot {
                    frames: u32,
                    beats: u64,
                    changed: u64,
                    last_beat_frame: u64,
                    tx: u64,
                    rx: u64,
                    // Bytes, beside the frame counts: the harness states a
                    // link's two directions in bytes (`sent=17219 (1,127,344
                    // bytes)`), so a run driven through the app could not be
                    // compared with a harness run on the same axis at all. The
                    // numbers were already in MDLinkStats and the health line
                    // printed only the packets.
                    tx_bytes: u64,
                    rx_bytes: u64,
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

                // The multiplayer flight recorder, sampled apart from the
                // five-second window as well: a radio going off or a deauth
                // leaving is the moment a session dies, and it deserves a digest
                // in the log at that moment rather than up to five seconds after
                // it. `mp_prev` is also the delta base for the window's line, so
                // "since the last time this was printed" stays true either way.
                let mut mp_prev: Vec<MDMPDigest> = vec![MDMPDigest::default(); insts.len()];
                let mut mp_state: Vec<(u32, u32, u32, u64, u64)> =
                    vec![(0, 0, 0, 0, 0); insts.len()];
                let mut mp_tick: u32 = 0;
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

                        // Every 500ms of the window: has any console's
                        // multiplayer state moved - radio on or off, a deauth
                        // sent or received, its role changed? If so, print the
                        // digest now, while the reason is still the present
                        // tense.
                        mp_tick += 1;
                        if mp_tick % 10 == 0 {
                            for (i, inst) in insts.iter().enumerate() {
                                let mut probe = MDMPDigest::default();
                                if unsafe { md_link_mp_digest(inst.ptr, &mut probe) } != 0 {
                                    continue;
                                }
                                let mut stats = MDLinkStats::default();
                                unsafe { md_link_stats(inst.ptr, &mut stats) };

                                let state = (
                                    probe.role,
                                    stats.begin_count,
                                    stats.end_count,
                                    probe.deauth_sent,
                                    probe.deauth_recv,
                                );
                                if state != mp_state[i] {
                                    log::warn!(
                                        "[mp-digest] p{} multiplayer state changed (wireless on/off {}/{}, deauth out/in {}/{}, role {}) - the digest as it stands:",
                                        i + 1,
                                        state.1,
                                        state.2,
                                        state.3,
                                        state.4,
                                        state.0,
                                    );
                                    mp_prev[i] = mp_digest(i, inst.ptr, &mp_prev[i]);
                                    mp_state[i] = state;
                                }
                            }
                        }
                    }
                    if health_stop.load(Ordering::SeqCst) {
                        break;
                    }
                    for (i, inst) in insts.iter().enumerate() {
                        let mut stats = MDLinkStats::default();
                        unsafe { md_link_stats(inst.ptr, &mut stats) };
                        let frames = unsafe { md_frame_counter(inst.ptr) };
                        // Liveness as the picture sees it: a wedged or
                        // screen-locked game keeps producing frames, so the
                        // frame counter cannot be the health signal on its own.
                        let (beats, changed, last_beat_frame) = {
                            let buf = health_frames[i].lock();
                            (buf.beats, buf.changed, buf.last_beat_frame)
                        };
                        let snap = Snapshot {
                            frames,
                            beats,
                            changed,
                            last_beat_frame,
                            tx: stats.packets_sent,
                            rx: stats.packets_received,
                            tx_bytes: stats.bytes_sent,
                            rx_bytes: stats.bytes_received,
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
                        let d_tx_bytes = snap.tx_bytes.wrapping_sub(p.tx_bytes);
                        let d_rx_bytes = snap.rx_bytes.wrapping_sub(p.rx_bytes);
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

                        /* "last change N frames ago" is the number that makes a
                           stopped game visible: green link counters say nothing
                           about whether either console's game is still moving.
                           It is called "still", not "FROZEN": a menu, a dialog
                           and a loading screen all hold one image, so a still
                           picture on its own says a screen is waiting, not that
                           anything is wrong (the CPU's own movement, printed
                           beside it, is what separates a wait from a hang). */
                        let since_beat = snap.frames.saturating_sub(snap.last_beat_frame as u32) as u64;
                        let picture = if since_beat > 300 {
                            format!("still (picture last changed {} frames ago)", since_beat)
                        } else {
                            format!("changing ({}f ago)", since_beat)
                        };

                        log::info!(
                            "health p{}: {} · frames {}(+{}) · picture {}(+{}) · tx {}(+{}, {}B(+{})) · rx {}(+{}, {}B(+{})) · slots 0x{:x} · wifi on/off {}/{} · pc {}/{}",
                            i + 1,
                            state,
                            snap.frames,
                            d_frames,
                            picture,
                            snap.beats.wrapping_sub(p.beats),
                            snap.tx,
                            d_tx,
                            snap.tx_bytes,
                            d_tx_bytes,
                            snap.rx,
                            d_rx,
                            snap.rx_bytes,
                            d_rx_bytes,
                            snap.connected,
                            snap.begin,
                            snap.end,
                            pc_moved[i],
                            pc_samples[i],
                        );

                        // Info, not a warning: a still picture is the one state
                        // the picture cannot judge. A game waiting on a peer
                        // looks the same from here as MPH's and MKDS's menus do,
                        // and measured on the app on 2026-09-29 player 2 sat on a
                        // still screen for 6,856 frames with its program counter
                        // moving in half the samples - reading that as FROZEN
                        // sent the same pass down a hang hunt twice. The CPUs'
                        // own movement is printed with it, and `wedged` above is
                        // the case that is actually a stall.
                        if since_beat > 300 {
                            log::info!(
                                "p{}: the picture has not changed in {} frames ({} changed pixels over {} beats) while the program counter moved in {}/{} samples - a screen that does not animate and a game waiting on something look the same from here",
                                i + 1,
                                since_beat,
                                snap.changed,
                                snap.beats,
                                pc_moved[i],
                                pc_samples[i],
                            );
                        }

                        // The flight recorder, once per window: what each game
                        // did with the link, which is what the wire counters
                        // above cannot see.
                        mp_prev[i] = mp_digest(i, inst.ptr, &mp_prev[i]);

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
                    buf.seq = u64::from(md_frame_counter(inst.ptr));
                    // Liveness, from the picture: the frame counter alone cannot
                    // tell a running game from one parked on a static screen.
                    buf.note_picture();
                }
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
// Test-drive control channel (opt-in, loopback only)
// ---------------------------------------------------------------------------
//
// A second, scriptable way into the app, for driving it from a shell: press
// keys through the frontend's *real* handlers, inject game input, ask what the
// app is doing, and read the emulated screens back as text or PNG.
//
// Why not X11/Wayland input injection or tauri-driver: this session runs on
// Wayland (XWayland is present but xdotool is not, and synthetic events would
// depend on compositor focus), while the webview can be driven directly and
// deterministically through Tauri's own eval()/invoke() from any thread. The
// screen reader matters more than the screenshot: an agent reads text, so the
// ASCII renderer below turns a 256x192 DS screen into characters it can
// actually interpret (a PNG it cannot).
//
// Off unless MD_CONTROL_PORT is set. Binds 127.0.0.1 only, one JSON object per
// line, one response line each; tools/appctl.py wraps it.

/// JavaScript installed before every control-channel evaluation: frame access
/// and the screen reader. Kept in the page so the Rust side needs no image
/// crate at all - the webview already has the pixels in canvas form.
const CTRL_PRELUDE: &str = r#"
window.__mdctl = window.__mdctl || {};
(function (D) {
  D.W = 256; D.H = 192; D.BPS = 256 * 192 * 4;
  D.frames = async function () {
    const b = await window.__TAURI__.core.invoke('get_frames');
    return (b instanceof ArrayBuffer) ? b : null;
  };
  D.unpack = function (buf) {
    const u8 = new Uint8Array(buf);
    const n = Math.floor(buf.byteLength / (8 + 2 * D.BPS));
    return { n: n, u8: u8, at: function (p) { return p * (8 + 2 * D.BPS) + 8; } };
  };
  // The one place "is there anything to read" is decided, and the one place a
  // console index is range-checked: `see`, `bands` and `shot` all answer from
  // here, so an idle app and an out-of-range player read the same wherever they
  // are asked. Those used to be three checks in two languages and the wording
  // drifted: on an empty app the client's copy answered "player 1 of 0 -- out of
  // range", naming a range with no player in it where the truth is that nothing
  // runs (2026-09-29).
  D.frameSet = async function () {
    const buf = await D.frames();
    if (!buf) return { err: '(no session: get_frames returned an empty body)' };
    const pl = D.unpack(buf);
    if (pl.n < 1) return { err: '(no session: no console is running)' };
    return { pl: pl };
  };
  D.console = async function (player) {
    const s = await D.frameSet();
    if (s.err) return s;
    if (!(player >= 1 && player <= s.pl.n))
      return { err: '(player ' + player + ' out of range: 1..' + s.pl.n + ')' };
    return { pl: s.pl, off: s.pl.at(player - 1) };
  };
  D.shot = async function () {
    const s = await D.frameSet();
    if (s.err) return '';
    const pl = s.pl;
    const cv = document.createElement('canvas');
    cv.width = D.W * pl.n; cv.height = D.H * 2;
    const ctx = cv.getContext('2d');
    for (let p = 0; p < pl.n; p++) {
      const off = pl.at(p);
      ctx.putImageData(new ImageData(new Uint8ClampedArray(pl.u8.subarray(off, off + D.BPS)), D.W, D.H), p * D.W, 0);
      ctx.putImageData(new ImageData(new Uint8ClampedArray(pl.u8.subarray(off + D.BPS, off + 2 * D.BPS)), D.W, D.H), p * D.W, D.H);
    }
    return cv.toDataURL('image/png');
  };
  D.ascii = async function (player, screen, cols, crop, aspect) {
    const c = await D.console(player);
    if (c.err) return c.err;
    const pl = c.pl, off = c.off;
    const want = [];
    if (screen === 0 || screen === 1) want.push(['top', pl.u8.subarray(off, off + D.BPS)]);
    if (screen === 0 || screen === 2) want.push(['bottom', pl.u8.subarray(off + D.BPS, off + 2 * D.BPS)]);
    const ramp = ' .:-=+*#%@';
    const out = [];
    for (const [name, px] of want) {
      const x0 = crop ? crop.x : 0, y0 = crop ? crop.y : 0;
      const w = crop ? crop.w : D.W, h = crop ? crop.h : D.H;
      // Terminal cells are about twice as tall as wide, so 0.5 keeps the
      // picture's proportions - but DS text is an 8px font, and halving the
      // vertical resolution smears it into soup. aspect 1 (one character per
      // pixel) makes menu text readable at the cost of looking stretched.
      const rows = Math.max(1, Math.round(cols * (h / w) * (aspect || 0.5)));
      out.push('-- player ' + player + ' ' + name + ' (' + w + 'x' + h + ' at ' + x0 + ',' + y0 + ' -> ' + cols + 'x' + rows + ') --');
      for (let ry = 0; ry < rows; ry++) {
        let line = '';
        for (let rx = 0; rx < cols; rx++) {
          const sx0 = x0 + Math.floor(rx * w / cols), sx1 = Math.max(sx0 + 1, x0 + Math.floor((rx + 1) * w / cols));
          const sy0 = y0 + Math.floor(ry * h / rows), sy1 = Math.max(sy0 + 1, y0 + Math.floor((ry + 1) * h / rows));
          let sum = 0, cnt = 0;
          for (let y = sy0; y < sy1; y++) {
            for (let x = sx0; x < sx1; x++) {
              const i = (y * D.W + x) * 4;
              sum += 0.2126 * px[i] + 0.7152 * px[i + 1] + 0.0722 * px[i + 2];
              cnt++;
            }
          }
          const lum = cnt ? sum / cnt : 0;
          // Inverted on purpose: DS menus are dark text on bright panels, so
          // dense characters must mean dark pixels or the text turns into
          // blanks against the space character.
          const idx = Math.min(9, Math.max(0, Math.round((255 - lum) / 255 * 9)));
          line += ramp[idx];
        }
        out.push(line);
      }
    }
    return out.join('\n');
  };
  // The rows that look like text: a DS menu line is the one place on a screen
  // with many dark/light transitions across it, which is what makes it
  // readable at one character per pixel (the caller crops each band and reads
  // the strip one character per pixel - that is how the MKDS route was found).
  // Beside D.ascii because it is the same reading of the same bytes: it used to
  // be a string of JavaScript in the Python client, with its own copy of the
  // empty-state and range checks (2026-09-29).
  D.bands = async function (player, screen, edge, rows) {
    const c = await D.console(player);
    if (c.err) return c.err;
    const px = c.pl.u8, base = c.off + (screen === 2 ? D.BPS : 0);
    const score = [];
    for (let y = 0; y < D.H; y++) {
      let e = 0;
      for (let x = 0; x < D.W - 1; x++) {
        const a = base + (y * D.W + x) * 4, b = a + 4;
        const la = 0.2126 * px[a] + 0.7152 * px[a + 1] + 0.0722 * px[a + 2];
        const lb = 0.2126 * px[b] + 0.7152 * px[b + 1] + 0.0722 * px[b + 2];
        if (Math.abs(la - lb) > edge) e++;
      }
      score.push(e);
    }
    const out = [];
    let start = null;
    for (let y = 0; y <= D.H; y++) {
      const hot = y < D.H && score[y] >= rows;
      if (hot) { if (start === null) start = y; }
      else if (start !== null) {
        let best = 0;
        for (let k = start; k < y; k++) best = Math.max(best, score[k]);
        out.push('y ' + start + '..' + (y - 1) + '   h ' + (y - start) + '   score ' + best);
        start = null;
      }
    }
    return out.length ? out.join('\n') : '(no text bands: a solid or empty screen)';
  };
  // A console's <h2> holds its name and a .fps span side by side, so plain
  // textContent glues them together and reports "Player 148 fps" (measured on a
  // running 2-player session 2026-09-28, and read as a UI bug for a minute).
  // Join the child nodes with a space instead - the visual gap the flex layout
  // shows.
  const titleText = function (h) {
    if (!h) return '';
    return Array.from(h.childNodes)
      .map(function (n) { return (n.textContent || '').trim(); })
      .filter(Boolean)
      .join(' ');
  };
  D.ui = function () {
    const cards = Array.from(document.querySelectorAll('.console'));
    return JSON.stringify({
      status: (document.getElementById('status') || {}).textContent || '',
      canvases: document.querySelectorAll('canvas').length,
      bodyClass: document.body.className,
      active: titleText(document.querySelector('.console.active h2')) || null,
      console_titles: cards.map(function (c) { return titleText(c.querySelector('h2')); }),
      fps: Array.from(document.querySelectorAll('.fps')).map(function (f) { return f.textContent; }),
      start_disabled: !!(document.getElementById('start') || {}).disabled,
      stop_disabled: !!(document.getElementById('stop') || {}).disabled
    });
  };
  D.click = function (sel) {
    const e = document.querySelector(sel);
    if (!e) return 'not found: ' + sel;
    // A disabled control ignores the click entirely, so reporting success for
    // one is a lie the driver cannot detect: `click #selftest` on an idle app
    // (the button is disabled when nothing runs) read as "clicked #selftest"
    // and nothing happened (2026-09-28 playtest).
    if (e.disabled) return 'disabled: ' + sel + ' (nothing to click - is a session running?)';
    e.click();
    return 'clicked ' + sel;
  };
  /* `key` and `index` ask the page about its own vocabulary instead of keeping
     a list of their own: `window.__mdkey` (does the page act on this name?) and
     `window.__mdselect` (which console does it drive?) are defined next to the
     handlers in frontend/index.html that own it - the keymap the backend sent,
     the console digits, Tab and the peek key. Those two halves used to be the
     frontend's map and the channel's willingness to accept any string at all,
     and they had drifted: `key Bogus` dispatched an event nothing listens for
     and reported success (2026-09-29). Nothing here names a key the frontend
     does not, so there is no second list left to drift. */
  D.key = function (code, down) {
    if (!window.__mdkey(code)) {
      throw new Error("this page cannot act on key '" + code + "': it acts on the console " +
        "digits, Tab, the peek key, and the keys in the running session's own console " +
        "keymap - so a console key needs a session, and a name the map does not carry " +
        "moves nothing");
    }
    window.dispatchEvent(new KeyboardEvent(down ? 'keydown' : 'keyup', { code: code, key: code, bubbles: true }));
    return (down ? 'keydown ' : 'keyup ') + code;
  };
  /* Input goes through the page's own per-console state, not straight at the
     core: the page is what holds a console's buttons and pen together, and a
     write from here that skipped it left the page's copy behind - the next
     key event then pushed that stale copy over the guest's pen. The page
     throws for a console that is not running, which this passes through. */
  D.input = async function (player, buttons, touch) {
    return await window.__mdinput(player, buttons, touch);
  };
})(window.__mdctl);
"#;

static CTRL_NEXT_ID: AtomicU64 = AtomicU64::new(1);
static CTRL_REPLIES: Mutex<BTreeMap<u64, String>> = Mutex::new(BTreeMap::new());

/// The webview half of a control evaluation: the injected IIFE reports its
/// result (or its error) through this command, keyed by the request id.
#[tauri::command]
fn md_ctrl_report(id: u64, data: String) {
    CTRL_REPLIES.lock().insert(id, data);
}

/// Evaluate `body` in the webview and wait for its value. The body runs inside
/// an async wrapper with the prelude in scope, so it may await.
fn ctrl_eval(app: &tauri::AppHandle, body: &str, timeout: Duration) -> Result<String, String> {
    let id = CTRL_NEXT_ID.fetch_add(1, Ordering::Relaxed);
    let win = app
        .get_webview_window("main")
        .ok_or_else(|| "the app has no 'main' window".to_string())?;
    // The body goes into the page as source, not through `eval`/`new Function`:
    // the window's CSP is `script-src 'self' 'unsafe-inline'` with no
    // `unsafe-eval`, so anything that compiles a string at runtime is refused
    // (measured 2026-09-28: a `new Function` wrapper turned every `eval`, valid
    // or not, into "Refused to evaluate a string as JavaScript"). That has one
    // cost: a body with a *syntax* error fails the whole script's parse, so it
    // cannot report its own error. The timeout path below asks the window a
    // trivial question to tell that apart from a window that is gone or busy,
    // instead of blaming the window for the caller's typo.
    let js = format!(
        "{prelude}(async function(){{try{{const v=await ({body});window.__TAURI__.core.invoke('md_ctrl_report',{{id:{id},data:(typeof v==='string')?v:JSON.stringify(v)}});}}\
         catch(e){{window.__TAURI__.core.invoke('md_ctrl_report',{{id:{id},data:'__ERR__ '+((e&&e.message)?e.message:String(e))}});}}}})().catch(function(){{}});",
        body = body,
        id = id,
        prelude = CTRL_PRELUDE
    );
    win.eval(&js)
        .map_err(|e| format!("eval into the webview failed: {e}"))?;
    let deadline = std::time::Instant::now() + timeout;
    loop {
        if let Some(v) = CTRL_REPLIES.lock().remove(&id) {
            // The wrapper above reports a thrown expression with this marker,
            // and this is the only producer, so unwrapping it here is what
            // makes a failure inside the page a failure of the command - a
            // `key Bogus` that threw, or an `eval` of a bad expression, used to
            // come back as successful data whose first word was the marker.
            return match v.strip_prefix("__ERR__ ") {
                Some(err) => Err(err.to_string()),
                None => Ok(v),
            };
        }
        if std::time::Instant::now() >= deadline {
            // Is the webview there at all? A yes/no question it cannot fail to
            // answer separates "your expression never ran" from "the window is
            // not talking".
            let alive = ctrl_eval_once(app, "1", Duration::from_secs(2)).is_some();
            return Err(if alive {
                format!(
                    "the webview answered nothing for this expression in {:?} while still answering other \
                     questions - so either the expression failed before it could report (a syntax error \
                     cannot report itself: the CSP forbids runtime compilation, so a bad body fails the \
                     whole script's parse) or the page went away under it (a `location.reload()` does that, \
                     and the session survives it - retry and the channel answers again)",
                    timeout
                )
            } else {
                format!("the webview did not answer within {:?} (window gone or busy?)", timeout)
            });
        }
        std::thread::sleep(Duration::from_millis(2));
    }
}

/// One injection, no fallbacks: `Some` when the webview reported within
/// `timeout`, `None` otherwise. Used to classify a timeout above.
fn ctrl_eval_once(app: &tauri::AppHandle, body: &str, timeout: Duration) -> Option<String> {
    let id = CTRL_NEXT_ID.fetch_add(1, Ordering::Relaxed);
    let win = app.get_webview_window("main")?;
    let js = format!(
        "{prelude}window.__TAURI__.core.invoke('md_ctrl_report',{{id:{id},data:String({body})}});",
        prelude = CTRL_PRELUDE,
        id = id,
        body = body
    );
    win.eval(&js).ok()?;
    let deadline = std::time::Instant::now() + timeout;
    while std::time::Instant::now() < deadline {
        if let Some(v) = CTRL_REPLIES.lock().remove(&id) {
            return Some(v);
        }
        std::thread::sleep(Duration::from_millis(2));
    }
    None
}

/// Where `shot` writes: next to the log file when MD_LOG points one out, so a
/// screenshot is found by the same post-mortem instinct as the log itself.
fn ctrl_shots_dir() -> PathBuf {
    if let Ok(dir) = std::env::var("MD_SHOTS_DIR") {
        if !dir.is_empty() {
            return PathBuf::from(dir);
        }
    }
    if let Ok(log) = std::env::var("MD_LOG") {
        if let Some(parent) = PathBuf::from(log).parent() {
            if !parent.as_os_str().is_empty() {
                return parent.join("shots");
            }
        }
    }
    PathBuf::from("logs/shots")
}

/// Minimal base64 decoder for the one thing that needs it: a screenshot
/// returning as a `data:image/png;base64,...` URL. 40 lines instead of a crate.
fn ctrl_b64(data: &str) -> Option<Vec<u8>> {
    fn val(c: u8) -> Option<u32> {
        match c {
            b'A'..=b'Z' => Some((c - b'A') as u32),
            b'a'..=b'z' => Some((c - b'a') as u32 + 26),
            b'0'..=b'9' => Some((c - b'0') as u32 + 52),
            b'+' => Some(62),
            b'/' => Some(63),
            _ => None,
        }
    }
    let mut out = Vec::with_capacity(data.len() / 4 * 3);
    let mut acc: u32 = 0;
    let mut bits = 0u32;
    for &c in data.as_bytes() {
        if c == b'=' || c.is_ascii_whitespace() {
            continue;
        }
        let v = val(c)?;
        acc = (acc << 6) | v;
        bits += 6;
        if bits >= 8 {
            bits -= 8;
            out.push(((acc >> bits) & 0xFF) as u8);
        }
    }
    Some(out)
}

/// Rust-side state, readable without the webview (so it still answers when the
/// UI is wedged - which is exactly when a test drive wants to ask).
fn ctrl_status(app: &tauri::AppHandle) -> serde_json::Value {
    let state = app.state::<AppState>();
    let current = state.session.read();
    let Some(session) = current.as_ref() else {
        return serde_json::json!({ "active": false });
    };
    let mut frames = Vec::with_capacity(session.frames.len());
    for (i, fb) in session.frames.iter().enumerate() {
        let buf = fb.lock();
        frames.push(serde_json::json!({ "player": i + 1, "seq": buf.seq }));
    }
    serde_json::json!({
        "active": true,
        "players": session.players,
        "rom": session.rom.display().to_string(),
        "title": session.title.clone(),
        "elapsed_s": session.started_at.elapsed().as_secs(),
        "link_mask": unsafe { md_link_connected_mask() },
        "frames": frames,
    })
}

/// Statistics over one screen: mean luma, share of the screen that is black,
/// and a checksum that changes whenever a sampled pixel does.
///
/// Pixels are read in the frontend's byte order (R,G,B,A in memory, because the
/// webview turns them straight into ImageData), and every 8th pixel is enough
/// for "is this screen black, and is it the same black as last time".
fn screen_stats(px: *const u32) -> serde_json::Value {
    const STRIDE: usize = 8;
    let mut sum: u64 = 0;
    let mut black: u32 = 0;
    let mut crc: u64 = 0xcbf2_9ce4_8422_2325;
    let mut n: u32 = 0;
    for i in (0..MD_SCREEN_PIXELS).step_by(STRIDE) {
        // Read-only sample of the renderer's own buffer. The pump copies this
        // between frames; another thread reading it can see a torn line, which
        // for a mean and a checksum is noise, not a wrong answer.
        let v = unsafe { *px.add(i) };
        let (r, g, b) = (v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF);
        sum += u64::from((2126 * r + 7152 * g + 722 * b) / 10000);
        if r < 8 && g < 8 && b < 8 {
            black += 1;
        }
        crc = (crc ^ u64::from(v)).wrapping_mul(0x0000_0100_0000_01B3);
        n += 1;
    }
    serde_json::json!({
        "mean": (sum / u64::from(n)) as u32,
        "black_pct": black * 100 / n,
        "crc": format!("{crc:016X}"),
    })
}

/// What a console's own picture and display registers say, right now.
///
/// `see` renders the last frame the pump copied, so a black screen there could
/// be a black frame, a stale copy, or a console whose pump has stopped. This
/// reads the renderer's framebuffers directly *and* the registers that decide
/// what a screen shows, because a black screen has three very different
/// authors: the game drawing black, the game turning the LCDs off
/// (`POWCNT1` bit 0 - the soft renderer fills both framebuffers black for that,
/// GPU_Soft.cpp), or a 2D unit disabled in `POWCNT1` (unit A black, unit B
/// white). Comparing the framebuffer against the copy says whether our path is
/// faithful; the registers say what the console was asked to display.
fn ctrl_probe(app: &tauri::AppHandle, player: usize) -> Result<serde_json::Value, String> {
    let state = app.state::<AppState>();
    let current = state.session.read();
    let session = current.as_ref().ok_or("no session")?;
    let inst = session
        .instances
        .get(player)
        .ok_or_else(|| format!("player {} is not running", player + 1))?;

    let (mut top, mut bottom) = (std::ptr::null(), std::ptr::null());
    let got = unsafe { md_get_screens(inst.ptr, &mut top, &mut bottom) } == 0
        && !top.is_null()
        && !bottom.is_null();

    // DISPCNT is 32-bit and the bits that matter most (the display mode at 16-
    // 17, the layer enables at 8-12) span both halves, so read it whole.
    let reg16 = |addr: u32| unsafe { u32::from(md_read_io16(inst.ptr, 9, addr)) };
    let reg32 = |addr: u32| unsafe { md_read_gpu(inst.ptr, addr) };
    let powcnt1 = reg16(0x0400_0304);
    let dispcnt_a = reg32(0x0400_0000);
    let dispcnt_b = reg32(0x0400_1000);

    let (copy, live) = {
        let buf = session.frames[player].lock();
        (
            serde_json::json!({
                "seq": buf.seq,
                "beats": buf.beats,
                "changed": buf.changed,
                "last_beat_frame": buf.last_beat_frame,
                "top": screen_stats(buf.top.as_ptr()),
                "bottom": screen_stats(buf.bottom.as_ptr()),
            }),
            serde_json::json!({
                "top": if got { screen_stats(top) } else { serde_json::Value::Null },
                "bottom": if got { screen_stats(bottom) } else { serde_json::Value::Null },
            }),
        )
    };

    // The interrupt level, from the same flight recorder the health thread
    // prints: the per-source wifi raise/ack counters and the wifi register
    // snapshot taken at export time, plus the two program counters. A black
    // picture with the CPUs still moving is a game waiting on an interrupt,
    // and this is where that shows as numbers instead of an inference.
    let mut wifi = MDMPDigest::default();
    unsafe { md_link_mp_digest(inst.ptr, &mut wifi) };
    let irq_list = |arr: &[u64; 16]| -> String {
        let mut out = Vec::new();
        for (i, v) in arr.iter().enumerate() {
            if *v > 0 {
                out.push(format!("{i}:{v}"));
            }
        }
        if out.is_empty() { "none".to_string() } else { out.join(" ") }
    };

    // The wifi IRQ is ARM7 IRQ_Wifi (bit 23), so it lives in the high halfword
    // of the ARM7's IE/IF; IME says whether that CPU can take interrupts at all.
    let ie7_hi = unsafe { md_read_io16(inst.ptr, 7, 0x0400_0212) };
    let if7_hi = unsafe { md_read_io16(inst.ptr, 7, 0x0400_0216) };

    // Built as its own value rather than inline: serde_json's `json!` expands
    // recursively, and one level of nesting around this many keys is enough to
    // hit the macro's recursion limit.
    let wifi_json = serde_json::json!({
        "role": wifi.role,
        "irq_cpu_line": wifi.irq_cpu_line,
        "raised": irq_list(&wifi.irq_raised),
        "acked": irq_list(&wifi.irq_acked),
        "arms": wifi.arms,
        "answered": wifi.answers_sent,
        "canned": wifi.answers_canned,
        "cmds_rx": wifi.cmds_rx,
        "cmds_tx": wifi.cmds_tx,
        "w_if": format!("{:04X}", wifi.w_if),
        "w_ie": format!("{:04X}", wifi.w_ie),
        "w_pending": format!("{:04X}", wifi.w_if & wifi.w_ie),
        "arm7_ime": format!("{:04X}", unsafe { md_read_io16(inst.ptr, 7, 0x0400_0208) }),
        "arm7_ie7": format!("{ie7_hi:04X}"),
        "arm7_if7": format!("{if7_hi:04X}"),
        "wifi_ie_bit": ie7_hi & 0x80 != 0,
        "wifi_if_bit": if7_hi & 0x80 != 0,
        "us_cntl": format!("{} / {}", wifi.w_uscntcnt, wifi.w_uscmpcnt),
        "us_at": format!("{:X} / {:X}", wifi.uscounter, wifi.uscompare),
        "beacon_irq14_blocked": wifi.block_beacon_irq14 != 0,
        "bcn_count": format!("{:04X} / {:04X}", wifi.w_bcncount1, wifi.w_bcncount2),
        "txslots": format!("b {:04X} r1 {:04X} r2 {:04X}",
                          wifi.w_txslot_beacon, wifi.w_txslot_reply1, wifi.w_txslot_reply2),
        "txbusy": format!("{:04X}", wifi.w_txbusy),
        "txreqread": format!("{:04X}", wifi.w_txreqread),
        "comstatus": wifi.comstatus,
        "powerstate": format!("{:04X}", wifi.w_powerstate),
        "powertx": format!("{:04X}", wifi.w_powertx),
    });

    Ok(serde_json::json!({
        "player": player + 1,
        "frames": unsafe { md_frame_counter(inst.ptr) },
        "asleep": unsafe { md_console_asleep(inst.ptr) } != 0,
        "pc9": format!("{:08X}", unsafe { md_get_pc(inst.ptr, 9) }),
        "pc7": format!("{:08X}", unsafe { md_get_pc(inst.ptr, 7) }),
        "powcnt1": format!("{powcnt1:04X}"),
        "screens_enabled": powcnt1 & 1 != 0,
        "engine_a": powcnt1 & (1 << 1) != 0,
        "engine_b": powcnt1 & (1 << 9) != 0,
        "screen_swap": powcnt1 & (1 << 15) != 0,
        "dispcnt_a": format!("{dispcnt_a:08X}"),
        "dispcnt_b": format!("{dispcnt_b:08X}"),
        "bright_a": format!("{:04X}", reg16(0x0400_006C)),
        "bright_b": format!("{:04X}", reg16(0x0400_106C)),
        "vcount": reg16(0x0400_0006) & 0x1FF,
        "wifi": wifi_json,
        "framebuffer": live,
        "copy": copy,
    }))
}

/// What a console's own game is reading out of its wireless block, as text.
///
/// A value the emulator fabricates and a value the peer's game wrote look
/// identical in every counter taken from the write side or from the wire; the
/// only place they can be told apart is where the game's own bytecode reads
/// them. So this returns the reads themselves - the wifi registers the game
/// has touched with their counts and last values, the last few hundred of them
/// in order, and every halfword it consumed out of the frame it was most
/// recently handed (see `Wifi::MPIo`). Read it next to `probe` (which gives the
/// same instant's ports and program counters) and the two say whether the game
/// is looking at something we made up.
fn ctrl_wifi_io(app: &tauri::AppHandle, player: usize) -> Result<String, String> {
    let state = app.state::<AppState>();
    let current = state.session.read();
    let session = current.as_ref().ok_or("no session")?;
    let inst = session
        .instances
        .get(player)
        .ok_or_else(|| format!("player {} is not running", player + 1))?;

    // The report is bounded by construction (24 registers, 512 ring entries,
    // 128 zone bytes, 256 zone reads: a few kB), so one buffer sized well
    // above that is enough and the C side reports truncation as a full buffer.
    const CAP: usize = 64 * 1024;
    let mut buf = vec![0i8; CAP];
    let n = unsafe { md_wifi_io_report(inst.ptr, buf.as_mut_ptr(), CAP as i32) };
    if n < 0 {
        return Err("the wifi io report did not fit its buffer".to_string());
    }
    let bytes: Vec<u8> = buf[..n as usize].iter().map(|b| *b as u8).collect();
    Ok(String::from_utf8_lossy(&bytes).into_owned())
}

/// Parse a hex string ("00 80 00 00" or "0080 0000", either case) into bytes.
/// Refuses a non-hex spelling rather than guessing, because a probe whose body
/// silently came out empty would look like a probe that did nothing.
fn ctrl_hex_bytes(s: &str) -> Vec<u8> {
    let mut out = Vec::new();
    let mut hi: Option<u8> = None;
    for c in s.chars() {
        if c == ' ' || c == ',' || c == '_' || c == '-' {
            continue;
        }
        match c.to_digit(16) {
            Some(d) => match hi.take() {
                None => hi = Some(d as u8),
                Some(h) => out.push((h << 4) | d as u8),
            },
            None => return Vec::new(),
        }
    }
    out
}

fn ctrl_mp_vary(
    app: &tauri::AppHandle,
    player: usize,
    kind: i32,
    value: i32,
    body: &[u8],
) -> Result<serde_json::Value, String> {
    let state = app.state::<AppState>();
    let current = state.session.read();
    let session = current.as_ref().ok_or("no session")?;
    let inst = session
        .instances
        .get(player)
        .ok_or_else(|| format!("player {} is not running", player + 1))?;

    let mut armed = 0i32;
    let mut left = 0i32;
    let mut applied = 0u64;
    let rc = unsafe {
        md_link_mp_vary(
            inst.ptr,
            kind,
            value,
            if body.is_empty() {
                std::ptr::null()
            } else {
                body.as_ptr()
            },
            body.len() as i32,
            &mut armed,
            &mut left,
            &mut applied,
        )
    };
    if rc != 0 {
        return Err(format!("md_link_mp_vary failed ({rc})"));
    }
    // `kind` is what the probe is *actually* armed as (0 when a body-less body
    // probe was accepted as nothing), not what the caller asked for.
    Ok(serde_json::json!({
        "player": player + 1,
        "kind": armed,
        "left": left,
        "applied": applied,
    }))
}

fn ctrl_json_player(req: &serde_json::Value) -> Result<u8, String> {
    let p = req.get("player").and_then(|v| v.as_u64()).unwrap_or(1);
    if !(1..=4).contains(&p) {
        return Err(format!("player {p} is out of range 1..4"));
    }
    Ok((p - 1) as u8)
}

fn ctrl_array4(req: &serde_json::Value, key: &str) -> Option<[i64; 4]> {
    let a = req.get(key)?.as_array()?;
    if a.len() != 4 {
        return None;
    }
    let mut out = [0i64; 4];
    for (i, v) in a.iter().enumerate() {
        out[i] = v.as_i64()?;
    }
    Some(out)
}

/// One request line -> one response object.
fn ctrl_dispatch(app: &tauri::AppHandle, line: &str) -> serde_json::Value {
    let reply = |data: serde_json::Value| serde_json::json!({ "ok": true, "data": data });
    let failed = |err: String| serde_json::json!({ "ok": false, "error": err });

    let req: serde_json::Value = match serde_json::from_str(line) {
        Ok(v) => v,
        Err(err) => return failed(format!("bad JSON: {err}")),
    };
    let id = req.get("id").cloned().unwrap_or(serde_json::Value::Null);
    let cmd = req.get("cmd").and_then(|v| v.as_str()).unwrap_or("");
    let arg_str = |key: &str| -> Option<String> {
        req.get(key).and_then(|v| v.as_str()).map(|s| s.to_string())
    };

    // Every eval goes through the same prelude, and every command logs: the
    // post-mortem and the test script then read the same story.
    let ev = |body: &str| ctrl_eval(app, body, Duration::from_secs(20));

    let mut resp = match cmd {
        "ping" => reply(serde_json::json!("pong")),
        "status" => reply(ctrl_status(app)),
        "ui" => match ev("__mdctl.ui()") {
            Ok(v) => reply(serde_json::json!(v)),
            Err(e) => failed(e),
        },
        "eval" => match arg_str("expr") {
            Some(expr) => match ev(&expr) {
                Ok(v) => reply(serde_json::json!(v)),
                Err(e) => failed(e),
            },
            None => failed("eval needs an 'expr' string".into()),
        },
        "key" => {
            let code = arg_str("code").unwrap_or_default();
            let down = req.get("down").and_then(|v| v.as_bool()).unwrap_or(true);
            if code.is_empty() {
                failed("key needs a 'code' (a KeyboardEvent.code, e.g. KeyA)".into())
            } else {
                match ev(&format!("__mdctl.key('{code}', {down})")) {
                    Ok(v) => reply(serde_json::json!(v)),
                    Err(e) => failed(e),
                }
            }
        }
        "index" => {
            // The console selector is the page's own (window.__mdselect, beside
            // the digit handling and setActive in frontend/index.html); the
            // channel used to dispatch a digit by hand and then read the DOM to
            // see whether that had worked.
            match ctrl_json_player(&req) {
                Ok(player) => match ev(&format!("window.__mdselect({player})")) {
                    Ok(v) => reply(serde_json::json!(v)),
                    Err(e) => failed(e),
                },
                Err(e) => failed(e),
            }
        }
        "click" => match arg_str("selector") {
            Some(sel) => match ev(&format!("__mdctl.click('{sel}')")) {
                Ok(v) => reply(serde_json::json!(v)),
                Err(e) => failed(e),
            },
            None => failed("click needs a 'selector'".into()),
        },
        "input" => {
            let player = match ctrl_json_player(&req) {
                Ok(p) => p,
                Err(e) => return failed(e),
            };
            let buttons = req.get("buttons").and_then(|v| v.as_u64()).unwrap_or(0) as u32;
            let touch = match ctrl_array4(&req, "touch") {
                Some(t) => format!("[{}, {}]", t[0], t[1]),
                None => "null".to_string(),
            };
            match ev(&format!("__mdctl.input({player}, {buttons}, {touch})")) {
                Ok(v) => reply(serde_json::json!(v)),
                Err(e) => failed(e),
            }
        }
        "press" => {
            // Hold buttons for a while: DS games read the keypad far faster
            // than a shell spawns processes, so the pause lives here.
            let player = match ctrl_json_player(&req) {
                Ok(p) => p,
                Err(e) => return failed(e),
            };
            // Refused rather than defaulted. The channel takes a mask and the
            // *client* turns names into masks (tools/appctl.py's BUTTONS); a
            // name that arrives here used to be read as 0, so the command held
            // nothing, answered `held buttons 0x0`, and left no `[input]` line
            // for a reader to notice it by - the silent-success shape this
            // project keeps having to remove (2026-09-29: `--press-a` and the
            // park battery pressed A four times and held nothing at all).
            let buttons = match req.get("buttons").and_then(|v| v.as_u64()) {
                Some(b) => b as u32,
                None => {
                    return failed(format!(
                        "press needs a button mask as a number (1 is A, 16 is right); got {:?} - names belong to the client (tools/appctl.py parses them)",
                        req.get("buttons").cloned().unwrap_or(serde_json::Value::Null)
                    ))
                }
            };
            let ms = req.get("ms").and_then(|v| v.as_u64()).unwrap_or(120);
            if let Err(e) = ev(&format!("__mdctl.input({player}, {buttons}, null)")) {
                return failed(e);
            }
            std::thread::sleep(Duration::from_millis(ms));
            match ev(&format!("__mdctl.input({player}, 0, null)")) {
                Ok(_) => reply(serde_json::json!(format!(
                    "held buttons {buttons:#x} on player {} for {ms}ms",
                    player + 1
                ))),
                Err(e) => failed(e),
            }
        }
        "touch" => {
            let player = match ctrl_json_player(&req) {
                Ok(p) => p,
                Err(e) => return failed(e),
            };
            let x = req.get("x").and_then(|v| v.as_i64()).unwrap_or(0);
            let y = req.get("y").and_then(|v| v.as_i64()).unwrap_or(0);
            let ms = req.get("ms").and_then(|v| v.as_u64());
            if let Err(e) = ev(&format!("__mdctl.input({player}, 0, [{x}, {y}])")) {
                return failed(e);
            }
            match ms {
                Some(ms) => {
                    std::thread::sleep(Duration::from_millis(ms));
                    match ev(&format!("__mdctl.input({player}, 0, null)")) {
                        Ok(_) => reply(serde_json::json!(format!(
                            "touched player {} at {x},{y} for {ms}ms",
                            player + 1
                        ))),
                        Err(e) => failed(e),
                    }
                }
                None => reply(serde_json::json!(format!(
                    "player {} touch down at {x},{y} (until 'untouch')",
                    player + 1
                ))),
            }
        }
        "untouch" => match ctrl_json_player(&req) {
            Ok(player) => match ev(&format!("__mdctl.input({player}, 0, null)")) {
                Ok(_) => reply(serde_json::json!(format!("player {} pen up", player + 1))),
                Err(e) => failed(e),
            },
            Err(e) => failed(e),
        },
        "probe" => match ctrl_json_player(&req) {
            Ok(player) => match ctrl_probe(app, player as usize) {
                Ok(v) => reply(v),
                Err(e) => failed(e),
            },
            Err(e) => failed(e),
        },
        "wifiio" => match ctrl_json_player(&req) {
            Ok(player) => match ctrl_wifi_io(app, player as usize) {
                Ok(v) => reply(serde_json::json!(v)),
                Err(e) => failed(e),
            },
            Err(e) => failed(e),
        },
        // The one-input probe of the parked handshake: `body` replaces the body
        // bytes the console's game reads with the given hex, `off` clears it and
        // reports the cumulative `applied` counter. Which bytes those are follows
        // from the console: player 1's game reads the answers it collects, player
        // 2's the commands it is handed. Diagnostics, never on unless asked for
        // here.
        "vary" => match ctrl_json_player(&req) {
            Ok(player) => {
                let kind = match arg_str("kind").unwrap_or_else(|| "off".into()).as_str() {
                    "off" | "0" => 0,
                    "body" | "1" => 1,
                    other => return failed(format!("vary kind must be off/body, not {other}")),
                };
                let value = req.get("value").and_then(|v| v.as_i64()).unwrap_or(0) as i32;
                let body_arg = arg_str("body").unwrap_or_default();
                let body = ctrl_hex_bytes(&body_arg);
                // A misspelled body used to be accepted and then armed nothing,
                // answering `kind 1` for a probe that could never fire - the
                // same silent miss the coordinate check exists for
                // (2026-09-28 playtest: `vary body --body "zz"`).
                if kind == 1 && body.is_empty() {
                    return failed(format!(
                        "vary body needs hex bytes (e.g. --body \"00 80 00 00\"), got '{body_arg}'"
                    ));
                }
                match ctrl_mp_vary(app, player as usize, kind, value, &body) {
                    Ok(v) => reply(v),
                    Err(e) => failed(e),
                }
            }
            Err(e) => failed(e),
        },
        "see" => {
            let player = match ctrl_json_player(&req) {
                Ok(p) => p,
                Err(e) => return failed(e),
            };
            // The CLI spells screens out and the channel takes numbers, and
            // the two used to disagree silently: `{"screen":"bottom"}` failed
            // the u64 parse, fell back to 0 and rendered *both* screens, so a
            // caller asking for the touch screen got the top one as well (a
            // driver waiting for the touch screen to hold still was really
            // waiting for an animating top screen - 2026-09-28). Take either
            // spelling, and refuse a value that is neither.
            let screen = match req.get("screen") {
                None => 0,
                Some(v) => match v.as_u64() {
                    Some(n) if n <= 2 => n,
                    _ => match v.as_str().map(|s| s.to_ascii_lowercase()).as_deref() {
                        Some("top") => 1,
                        Some("bottom") => 2,
                        Some("both") => 0,
                        _ => {
                            return failed(
                                "'screen' is 0/1/2 or \"top\"/\"bottom\"/\"both\"".into(),
                            )
                        }
                    },
                },
            };
            let cols = req
                .get("cols")
                .and_then(|v| v.as_u64())
                .unwrap_or(128)
                .clamp(16, 256);
            let aspect = req
                .get("aspect")
                .and_then(|v| v.as_f64())
                .unwrap_or(0.5)
                .clamp(0.25, 4.0);
            let crop = match ctrl_array4(&req, "crop") {
                Some([x, y, w, h]) => format!("{{x:{x},y:{y},w:{w},h:{h}}}"),
                None => "null".to_string(),
            };
            let want_player = (player + 1) as u64;
            match ev(&format!(
                "__mdctl.ascii({want_player}, {screen}, {cols}, {crop}, {aspect})"
            )) {
                Ok(v) => reply(serde_json::json!(v)),
                Err(e) => failed(e),
            }
        }
        "shot" => match ev("__mdctl.shot()") {
            Ok(url) => {
                let bytes = url
                    .strip_prefix("data:image/png;base64,")
                    .and_then(ctrl_b64);
                match bytes {
                    Some(png) if !png.is_empty() => {
                        let name = arg_str("name").unwrap_or_else(|| {
                            format!("shot-{}", now_ms())
                        });
                        let dir = ctrl_shots_dir();
                        let path = dir.join(if name.ends_with(".png") {
                            name.clone()
                        } else {
                            format!("{name}.png")
                        });
                        let written = std::fs::create_dir_all(&dir)
                            .and_then(|_| std::fs::write(&path, &png));
                        match written {
                            Ok(()) => reply(serde_json::json!({
                                "path": path.display().to_string(),
                                "bytes": png.len(),
                            })),
                            Err(err) => failed(format!("cannot write {}: {err}", path.display())),
                        }
                    }
                    _ => failed("the page returned no PNG (no session, or empty frames)".into()),
                }
            }
            Err(e) => failed(e),
        },
        "text" => {
            let sel = arg_str("selector").unwrap_or_else(|| "#status".to_string());
            match ev(&format!(
                "(function(){{const e=document.querySelector('{sel}');return e?e.textContent:'(no {sel})';}})()"
            )) {
                Ok(v) => reply(serde_json::json!(v)),
                Err(e) => failed(e),
            }
        },
        "sleep" => {
            let ms = req.get("ms").and_then(|v| v.as_u64()).unwrap_or(100);
            std::thread::sleep(Duration::from_millis(ms));
            reply(serde_json::json!(format!("slept {ms}ms")))
        }
        "quit" => match ev("window.__TAURI__.core.invoke('quit_app')") {
            Ok(_) => reply(serde_json::json!("quit requested")),
            Err(e) => failed(e),
        },
        other => failed(format!(
            "unknown command '{other}' (ping, status, ui, eval, key, index, click, input, press, touch, untouch, see, shot, text, sleep, quit)"
        )),
    };

    if let Some(obj) = resp.as_object_mut() {
        obj.insert("id".into(), id);
    }
    // The log line is the run's post-mortem, and it is documented as
    // `[control] <cmd>: <result>` - so an object reply has to be printed, not
    // reduced to "ok" (which is what `vary`/`status`/`probe` used to log, losing
    // the `applied` counter a probe is read by).
    let brief = if let Some(err) = resp.get("error").and_then(|v| v.as_str()) {
        format!("ERROR {err}")
    } else {
        match resp.get("data") {
            Some(serde_json::Value::String(s)) => s.chars().take(120).collect(),
            Some(other) => other.to_string().chars().take(120).collect(),
            None => String::from("ok"),
        }
    };
    if resp.get("ok").and_then(|v| v.as_bool()).unwrap_or(false) {
        log::info!("[control] {cmd}: {brief}");
    } else {
        log::warn!("[control] {cmd}: {brief}");
    }
    resp
}

fn ctrl_serve(app: tauri::AppHandle, stream: TcpStream) {
    let reader = match stream.try_clone() {
        Ok(s) => BufReader::new(s),
        Err(_) => return,
    };
    let mut writer = stream;
    for line in reader.lines() {
        let Ok(line) = line else { break };
        if line.trim().is_empty() {
            continue;
        }
        let mut out = serde_json::to_string(&ctrl_dispatch(&app, &line))
            .unwrap_or_else(|_| String::from("{\"ok\":false,\"error\":\"unserializable reply\"}"));
        out.push('\n');
        if writer.write_all(out.as_bytes()).is_err() {
            break;
        }
        let _ = writer.flush();
    }
}

fn spawn_control_server(app: tauri::AppHandle) {
    let Ok(requested) = std::env::var("MD_CONTROL_PORT") else {
        return; // off by default: this channel can press buttons and quit the app
    };
    let port: u16 = match requested.trim().parse() {
        Ok(p) => p,
        Err(_) => {
            log::error!(
                "MD_CONTROL_PORT='{requested}' is not a port number; the control channel stays off"
            );
            return;
        }
    };
    let spawned = std::thread::Builder::new()
        .name("control".into())
        .spawn(move || {
            let listener = match TcpListener::bind(("127.0.0.1", port)) {
                Ok(l) => l,
                Err(err) => {
                    log::error!("control channel: cannot bind 127.0.0.1:{port}: {err}");
                    return;
                }
            };
            log::warn!(
                "control channel listening on 127.0.0.1:{port} - a test-drive interface that can \
                 press buttons and read the screens. Loopback only; never expose it."
            );
            for stream in listener.incoming() {
                let Ok(stream) = stream else { continue };
                let app = app.clone();
                std::thread::spawn(move || ctrl_serve(app, stream));
            }
        });
    if let Err(err) = spawned {
        log::error!("could not start the control channel thread: {err}");
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
            md_ctrl_report,
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

    // Opt-in test-drive channel (MD_CONTROL_PORT): off in normal runs.
    spawn_control_server(app.handle().clone());

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

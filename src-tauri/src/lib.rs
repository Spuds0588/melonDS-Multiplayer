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

// Per-game identity and the fix registry a title-specific patch attaches to
// (the one owner of "which game is loaded and what do we do differently").
mod game_profile;
// The synthesized client half of single-card Download Play. Present only for a
// title whose profile says `download_play`; on every other title it does not
// exist, so its link path is unchanged.
mod dp_frames;
mod dp_joiner;
// The same frontend, served to a browser the user already has: the way online
// play is hosted on a platform whose WebView has no WebRTC (see the module).
mod browser_host;

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

/// One armed Link-test expectation, as melonds_bridge.h reports it.
#[repr(C)]
#[derive(Clone, Copy)]
struct MDProbeResult {
    armed: u32,
    observed: u32,
    payload_ok: u32,
    ts_ok: u32,
    seen_by: [i8; 12],
}

impl Default for MDProbeResult {
    fn default() -> Self {
        MDProbeResult { armed: 0, observed: 0, payload_ok: 0, ts_ok: 0, seen_by: [0; 12] }
    }
}

/// Names the first field whose canary value does not match this side's mirror
/// of MDLinkStats, or None when the whole struct lines up.
///
/// This exists because a hand-written mirror of a 24-field struct in a second
/// language fails *quietly*: on 2026-09-30 the v3 fields were inserted in the
/// wrong place on this side, so `packets_delivered` here read the C struct's
/// `packets_read_game` and the Link test called a healthy bus broken with every
/// number in the log looking perfectly plausible. The canary catches that at
/// session start, by name.
fn stats_layout_mismatch(c: &MDLinkStats) -> Option<&'static str> {
    macro_rules! chk {
        ($idx:expr, $name:ident) => {
            if c.$name != $idx {
                return Some(stringify!($name));
            }
        };
    }
    chk!(0, connected);
    chk!(1, begin_count);
    chk!(2, end_count);
    chk!(3, packets_sent);
    chk!(4, packets_received);
    chk!(5, bytes_sent);
    chk!(6, bytes_received);
    chk!(7, packets_delivered);
    chk!(8, bytes_delivered);
    chk!(9, packets_read_game);
    chk!(10, bytes_read_game);
    chk!(11, packets_read_drained);
    chk!(12, bytes_read_drained);
    chk!(13, rx_broadcast);
    chk!(14, rx_cmd);
    chk!(15, rx_reply);
    chk!(16, rx_ack);
    chk!(17, rx_polls);
    chk!(18, tx_cmd);
    chk!(19, tx_reply);
    chk!(20, tx_ack);
    chk!(21, rx_empty_polls);
    chk!(22, last_rx_us);
    chk!(23, epoch_us);
    None
}

impl MDProbeResult {
    fn seen_by_str(&self) -> String {
        let bytes: Vec<u8> = self
            .seen_by
            .iter()
            .take_while(|c| **c != 0)
            .map(|c| *c as u8)
            .collect();
        String::from_utf8_lossy(&bytes).into_owned()
    }
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
    // v3 receive attribution (melonds_bridge.h): the bus's own delivery ledger
    // and the same total split by which consumer read the frame.
    packets_delivered: u64,
    bytes_delivered: u64,
    packets_read_game: u64,
    bytes_read_game: u64,
    packets_read_drained: u64,
    bytes_read_drained: u64,
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

/// IPC FIFO traffic between one console's two CPUs (md_get_ipc_stats in
/// melonds_bridge.h). Counted where the words move, because an empty FIFO looks
/// the same whether the channel was never used or is merely idle.
#[repr(C)]
#[derive(Clone, Copy, Default)]
struct MDIPCStats {
    arm9_sends: u64,
    arm7_sends: u64,
    arm9_recvs: u64,
    arm7_recvs: u64,
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
    fn md_rom_game_code(inst: *mut MDInstance, out: *mut i8, cap: usize) -> i32;
    fn md_set_jit(inst: *mut MDInstance, enabled: i32) -> i32;
    fn md_write_ram(inst: *mut MDInstance, addr: u32, data: *const u8, len: usize) -> i32;
    // The read side of the same call. Declared for the `mem` control command,
    // which is the instrument every pass since v0.1.9y has named: without it
    // "the game is waiting on something" can only be inferred from a picture,
    // and a parked game and a busy one look the same from there.
    fn md_read_ram(inst: *mut MDInstance, addr: u32, out: *mut u8, len: usize) -> i32;
    // The same read on either CPU's own address space (cpu 9 = ARM9, 7 = ARM7).
    // The ARM7's WRAM and the game's own ARM7 image are invisible to an ARM9
    // read, which is why "what is the ARM7 side doing" had no instrument until
    // 2026-09-29.
    fn md_read_ram_cpu(
        inst: *mut MDInstance,
        cpu: i32,
        addr: u32,
        out: *mut u8,
        len: usize,
    ) -> i32;
    // The write half of md_read_ram_cpu, for the `poke` control command. A
    // read can show that a branch's exit condition is false; only changing the
    // value it tests can show that the branch is what holds the CPU - and when
    // that value lives in the ARM7's own space, an ARM9 write cannot reach it.
    fn md_write_ram_cpu(
        inst: *mut MDInstance,
        cpu: i32,
        addr: u32,
        data: *const u8,
        len: usize,
    ) -> i32;
    fn md_get_ipc_stats(inst: *mut MDInstance, out: *mut MDIPCStats) -> i32;
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
    // One CPU's whole register file, for the question a bare program counter
    // cannot answer: which of the game's own functions is waiting in the loop
    // the PC points into (r13 locates its frame, r14 its innermost return).
    fn md_get_regs(inst: *mut MDInstance, cpu: i32, out16: *mut u32) -> i32;
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
    // Bus attach/detach for a Link test, without touching the game's wireless
    // hardware (see melonds_bridge.h).
    fn md_link_test_attach(inst: *mut MDInstance, on: i32) -> i32;
    fn md_link_stats(inst: *mut MDInstance, out: *mut MDLinkStats) -> i32;
    fn md_link_stats_canary(out: *mut u8, cap: usize) -> u64;
    fn md_probe_arm(inst: *mut MDInstance, expect: *const u8, len: usize, ts: u64) -> i32;
    fn md_probe_disarm(inst: *mut MDInstance);
    fn md_probe_result(inst: *mut MDInstance, out: *mut MDProbeResult) -> i32;
    fn md_link_mp_digest(inst: *mut MDInstance, out: *mut MDMPDigest) -> i32;
    fn md_link_mp_digest_size() -> u64;
    fn md_link_resync(inst: *mut MDInstance);
    fn md_link_connected_mask() -> u32;
    fn md_link_send_packet(inst: *mut MDInstance, data: *const u8, len: usize, timestamp: u64) -> i32;
    fn md_link_send_reply(
        inst: *mut MDInstance,
        data: *const u8,
        len: usize,
        timestamp: u64,
        aid: u16,
    ) -> i32;
    // Returns the byte count, 0 when nothing is queued. Declared `int` in
    // melonds_bridge.h, so it MUST be read as i32 here: this used to be
    // declared `usize`, which left the upper half of the return register
    // undefined and turned every "did my packet arrive" comparison into a coin
    // toss (the send side was always declared i32, which is why sends looked
    // reliable while receives looked flaky).
    fn md_link_recv_packet(inst: *mut MDInstance, out: *mut u8, cap: usize, timestamp: *mut u64) -> i32;
    // Same, plus the entry's routing fields for readers that hand frames back
    // (see md_link_requeue_packet). `sender`/`type` may be null. `cap` must
    // hold a whole entry (0x948) - a short buffer reads as empty, never torn.
    fn md_link_recv_packet_full(
        inst: *mut MDInstance,
        out: *mut u8,
        cap: usize,
        timestamp: *mut u64,
        sender: *mut u32,
        ftype: *mut u32,
    ) -> i32;
    // Return a drained frame to this console's own inbox tail, verbatim.
    fn md_link_requeue_packet(
        inst: *mut MDInstance,
        sender: u32,
        ftype: u32,
        data: *const u8,
        len: usize,
        timestamp: u64,
    ) -> i32;
    fn md_read_io16(inst: *mut MDInstance, cpu: i32, addr: u32) -> u16;
    fn md_wifi_io_report(inst: *mut MDInstance, out: *mut i8, cap: i32) -> i32;
    fn md_mem_watch(inst: *mut MDInstance, lo: u32, hi: u32) -> i32;
    fn md_mem_watch_report(inst: *mut MDInstance, out: *mut i8, cap: i32) -> i32;
    fn md_pc_trace(inst: *mut MDInstance, cpu: i32, lo: u32, hi: u32) -> i32;
    fn md_pc_trace_report(inst: *mut MDInstance, out: *mut i8, cap: i32) -> i32;
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
    // This console's own wireless MAC. The Download Play joiner addresses its
    // management frames from it, so the host's answer comes back to a console
    // that exists on the bus.
    fn md_console_mac(inst: *mut MDInstance, out: *mut u8) -> i32;
    fn md_read_audio(inst: *mut MDInstance, out: *mut i16, max_frames: i32, out_rate: *mut i32) -> i32;
    fn md_console_name(inst: *mut MDInstance, out: *mut i8, cap: i32) -> i32;
    fn md_link_attached(inst: *mut MDInstance) -> i32;
    // Boot a single-card Download Play child image on this console: write the
    // header/ARM9/ARM7 segments to the RSA destinations, set the DS-Download-
    // Play boot indicator, carry the negotiated session into the console's radio
    // state, and point both CPUs at the entrypoints. The joiner's terminal state
    // (the assembled image) is the input; this is what makes the download play.
    // Returns 0 on success.
    fn md_dp_boot(
        inst: *mut MDInstance,
        image: *const u8,
        len: usize,
        header_dest: u32,
        header_size: u32,
        arm9_dest: u32,
        arm9_size: u32,
        arm9_entry: u32,
        arm7_dest: u32,
        arm7_temp: u32,
        arm7_size: u32,
        arm7_entry: u32,
        session: *const MDDPSession,
    ) -> i32;
}

/// The C side of a Download Play session: `MDDPSession` in
/// `src/frontend/tauri/melonds_bridge.h`. `#[repr(C)]` with the fields in the
/// header's own order, so the two declarations cannot drift apart silently -
/// the padding after `host_bssid` is the compiler's on both sides, and the
/// struct is only ever read, never written, by the core.
#[repr(C)]
#[derive(Clone, Copy, Default, PartialEq, Eq, Debug)]
struct MDDPSession {
    channel: u32,
    host_bssid: [u8; 6],
    aid: u32,
    // Beacon serial/token/capability/interval/rate-mask/beacon-size, in that
    // order: field semantics live on `dp_joiner::Session`, offsets on
    // `DownloadPlayBootInfo`. u32s throughout so no padding can drift against
    // the C header's matching uint32_ts.
    host_serial: u32,
    host_token: u32,
    host_capability: u32,
    host_interval: u32,
    host_rate_mask: u32,
    host_beacon_size: u32,
}

impl From<&dp_joiner::Session> for MDDPSession {
    fn from(s: &dp_joiner::Session) -> Self {
        MDDPSession {
            channel: u32::from(s.channel),
            host_bssid: s.bssid,
            aid: u32::from(s.aid),
            host_serial: u32::from_le_bytes(s.serial),
            host_token: u32::from(s.token[0]) | (u32::from(s.token[1]) << 8),
            host_capability: u32::from(s.capability),
            host_interval: u32::from(s.interval),
            host_rate_mask: u32::from(s.rate_mask),
            host_beacon_size: u32::from(s.beacon_size),
        }
    }
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
    // `W_IF`/`W_IE`, not `IF`/`IE`: these are the *wireless block's* registers,
    // and on a line that already prints `arm7 IME ... IE7 ...` a bare `IF`/`IE`
    // reads as the CPU's own pair. It was read that way on 2026-09-29 - a parked
    // console's `IE 0000` was taken for "this CPU can never be woken" when the
    // ARM9's real registers were IME 1 / IE 0009 (read through `mem`), i.e.
    // awake and taking vblank the whole time. The name is the whole fix: a
    // label that cannot be mistaken for another register cannot mislead.
    let irq = format!(
        "mp p{} irq: cpu line {}(+{}) · raised {} · acked {} · W_IF {:04X} W_IE {:04X} pending {:04X}",
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
    /// Send an *answer* on this console's slot, filed in the answer queue of
    /// the console it is following and keyed by `aid` - the path a host's reply
    /// window reads (see md_link_send_reply). Replies with the queued byte
    /// count (>0 sent, 0 refused).
    SendReply {
        data: Vec<u8>,
        ts: u64,
        aid: u16,
        reply: std::sync::mpsc::Sender<i32>,
    },
    /// Drain everything currently queued for this console, oldest first.
    Drain {
        reply: std::sync::mpsc::Sender<Vec<(Vec<u8>, u64)>>,
    },
    /// One turn of the Download Play joiner, run *inside* this console's own
    /// turn: read the inbox, advance the client state machine, and send whatever
    /// it decided - all before this console's next frame. This is the whole
    /// reason the variant exists: an answer sent as its own op is only serviced
    /// at the next turn, one frame after the host opened the window that wanted
    /// it, which is what left Super Mario 64 DS's host empty on 2,522 of 2,535
    /// addressed rounds. The state rides in and stays there (the turn is its
    /// only writer), so `State` has one owner and the schedule has one place.
    JoinerTurn {
        state: Arc<std::sync::Mutex<dp_joiner::State>>,
        dp: Arc<dp_joiner::DownloadPlayJoiner>,
        reply: std::sync::mpsc::Sender<()>,
    },
    /// Boot a completed Download Play child image on this console. Carries the
    /// assembled image (sent once) and runs on the driver thread like every
    /// other op, so the memory writes and the CPU jump land at a frame boundary.
    BootDownloaded {
        image: Vec<u8>,
        info: dp_joiner::BootInfo,
        /// The session the client negotiated with the host (channel, the host's
        /// BSSID, and the AID it was granted). See `dp_joiner::Session`.
        session: MDDPSession,
        reply: std::sync::mpsc::Sender<bool>,
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
            LinkOp::SendReply { data, ts, aid, reply } => {
                let queued = unsafe {
                    md_link_send_reply(self.ptr, data.as_ptr(), data.len(), ts, aid)
                };
                let _ = reply.send(queued);
            }
            LinkOp::Drain { reply } => {
                // Bounded per turn: a probe wants "what is queued right now",
                // not an unbounded sweep that could starve the frame.
                let out = DriverTurnBus { ptr: self.ptr }.drain_now();
                let _ = reply.send(out);
            }
            LinkOp::JoinerTurn { state, dp, reply } => {
                // One bus for the whole turn, talking to the bridge directly:
                // the drain and every send it earns happen here, on this thread,
                // before the console's next frame.
                let bus = DriverTurnBus { ptr: self.ptr };
                if let Ok(mut st) = state.lock() {
                    dp_joiner::step(&bus, &dp, &mut st, std::time::Instant::now());
                }
                let _ = reply.send(());
            }
            LinkOp::BootDownloaded { image, info, session, reply } => {
                let ok = unsafe {
                    md_dp_boot(
                        self.ptr,
                        image.as_ptr(),
                        image.len(),
                        info.header_dest,
                        info.header_size,
                        info.arm9_dest,
                        info.arm9_size,
                        info.arm9_entry,
                        info.arm7_dest,
                        info.arm7_temp,
                        info.arm7_size,
                        info.arm7_entry,
                        &session,
                    ) == 0
                };
                let _ = reply.send(ok);
            }
        }
    }

    /// Hand the completed image to the driver thread to boot. Returns false when
    /// the driver did not answer in time or the bridge refused the image.
    fn link_boot_now(
        &self,
        image: &[u8],
        info: &dp_joiner::BootInfo,
        session: &dp_joiner::Session,
    ) -> bool {
        let (tx, rx) = std::sync::mpsc::channel();
        self.link_ops.lock().push(LinkOp::BootDownloaded {
            image: image.to_vec(),
            info: *info,
            session: MDDPSession::from(session),
            reply: tx,
        });
        rx.recv_timeout(LINK_OP_TIMEOUT).unwrap_or(false)
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

    /// Queued byte count for an answer from this console, or None when the
    /// driver did not answer. Unlike `link_send_now` the frame does not go out
    /// on the bus's broadcast path: it is filed in the following console's
    /// answer queue under `aid`, where a host's reply window reads it.
    fn link_send_reply_now(&self, data: &[u8], ts: u64, aid: u16, what: &str) -> Option<i32> {
        let (tx, rx) = std::sync::mpsc::channel();
        self.link_ops.lock().push(LinkOp::SendReply {
            data: data.to_vec(),
            ts,
            aid,
            reply: tx,
        });
        match rx.recv_timeout(LINK_OP_TIMEOUT) {
            Ok(queued) => Some(queued),
            Err(_) => {
                log::warn!("{what}: console driver did not answer a reply send request");
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

/// The Download Play joiner's bus as seen *from inside a console's own turn*.
/// Every call goes straight to the bridge on whichever thread is running this
/// turn - the console's driver thread - so a command drained and the answer it
/// earns happen in the same frame, and the answer is in the host's reply queue
/// before its next window opens. That is the difference this type exists for:
/// the queue-based `InstanceJoinerBus` below can only hand work to the driver
/// thread, so its answer always landed one frame late.
///
/// Driver thread only: it holds a raw instance pointer and blocks on nothing.
struct DriverTurnBus {
    ptr: *mut MDInstance,
}

unsafe impl Send for DriverTurnBus {}
unsafe impl Sync for DriverTurnBus {}

impl DriverTurnBus {
    /// Everything queued for this console right now, oldest first. Bounded per
    /// turn: a turn wants "what is queued now", not an unbounded sweep that
    /// could starve the frame.
    fn drain_now(&self) -> Vec<(Vec<u8>, u64)> {
        let mut out: Vec<(Vec<u8>, u64)> = Vec::new();
        let mut buf = [0u8; 2048];
        let mut ts = 0u64;
        while out.len() < 32 {
            let n = unsafe { md_link_recv_packet(self.ptr, buf.as_mut_ptr(), buf.len(), &mut ts) };
            if n <= 0 {
                break;
            }
            out.push((buf[..(n as usize).min(buf.len())].to_vec(), ts));
        }
        out
    }
    /// Same, with each entry's routing fields, for a reader that hands some
    /// classes back (see `JoinerBus::requeue`). Same bound, same order.
    fn drain_full_now(&self) -> Vec<(Vec<u8>, u64, u32, u32)> {
        let mut out: Vec<(Vec<u8>, u64, u32, u32)> = Vec::new();
        let mut buf = [0u8; 2048];
        let mut ts = 0u64;
        let mut sender = 0u32;
        let mut ftype = 0u32;
        while out.len() < 32 {
            let n = unsafe {
                md_link_recv_packet_full(
                    self.ptr,
                    buf.as_mut_ptr(),
                    buf.len(),
                    &mut ts,
                    &mut sender,
                    &mut ftype,
                )
            };
            if n <= 0 {
                break;
            }
            out.push((buf[..(n as usize).min(buf.len())].to_vec(), ts, sender, ftype));
        }
        out
    }
    /// Hand one drained frame back to this console's own inbox tail.
    fn requeue_now(&self, pkt: &[u8], ts: u64, sender: u32, ftype: u32) -> bool {
        if pkt.is_empty() {
            return false;
        }
        unsafe {
            md_link_requeue_packet(self.ptr, sender, ftype, pkt.as_ptr(), pkt.len(), ts) > 0
        }
    }
}

impl dp_joiner::JoinerBus for DriverTurnBus {
    fn drain(&self) -> Option<Vec<(Vec<u8>, u64)>> {
        Some(self.drain_now())
    }
    fn drain_full(&self) -> Option<Vec<(Vec<u8>, u64, u32, u32)>> {
        Some(self.drain_full_now())
    }
    fn requeue(&self, pkt: &[u8], ts: u64, sender: u32, ftype: u32) -> bool {
        self.requeue_now(pkt, ts, sender, ftype)
    }

    fn send(&self, data: &[u8], ts: u64) -> Option<i32> {
        Some(unsafe { md_link_send_packet(self.ptr, data.as_ptr(), data.len(), ts) })
    }

    fn send_reply(&self, data: &[u8], ts: u64, aid: u16) -> Option<i32> {
        Some(unsafe { md_link_send_reply(self.ptr, data.as_ptr(), data.len(), ts, aid) })
    }

    fn own_mac(&self) -> Option<[u8; 6]> {
        let mut mac = [0u8; 6];
        if unsafe { md_console_mac(self.ptr, mac.as_mut_ptr()) } == 0 {
            Some(mac)
        } else {
            None
        }
    }

    fn wireless_on(&self) -> bool {
        let mut stats = MDLinkStats::default();
        unsafe { md_link_stats(self.ptr, &mut stats) };
        stats.begin_count > stats.end_count
    }

    fn set_receiving(&self, on: bool) -> bool {
        unsafe { md_link_test_attach(self.ptr, if on { 1 } else { 0 }) == 0 }
    }

    fn receiving(&self) -> bool {
        unsafe { md_link_attached(self.ptr) == 1 }
    }

    fn handoff(&self, image: &[u8], info: &dp_joiner::BootInfo, session: &dp_joiner::Session) -> bool {
        let session = MDDPSession::from(session);
        unsafe {
            md_dp_boot(
                self.ptr,
                image.as_ptr(),
                image.len(),
                info.header_dest,
                info.header_size,
                info.arm9_dest,
                info.arm9_size,
                info.arm9_entry,
                info.arm7_dest,
                info.arm7_temp,
                info.arm7_size,
                info.arm7_entry,
                &session,
            ) == 0
        }
    }
}

/// The Download Play joiner's view of one console's bus from *another* thread.
/// It is only a relay: `turn` hands the whole turn to the console's driver
/// thread (see `DriverTurnBus`), because the joiner's work has to happen inside
/// the console's own frame to be in time. The one thing read directly is this
/// console's own MAC, which is fixed at boot.
struct InstanceJoinerBus {
    inst: Arc<Instance>,
}

impl dp_joiner::JoinerBus for InstanceJoinerBus {
    fn drain(&self) -> Option<Vec<(Vec<u8>, u64)>> {
        self.inst.link_drain_now("download-play joiner")
    }

    fn send(&self, data: &[u8], ts: u64) -> Option<i32> {
        self.inst.link_send_now(data, ts, "download-play joiner")
    }

    fn send_reply(&self, data: &[u8], ts: u64, aid: u16) -> Option<i32> {
        self.inst.link_send_reply_now(data, ts, aid, "download-play joiner")
    }


    fn own_mac(&self) -> Option<[u8; 6]> {
        let mut mac = [0u8; 6];
        if unsafe { md_console_mac(self.inst.ptr, mac.as_mut_ptr()) } == 0 {
            Some(mac)
        } else {
            None
        }
    }

    fn wireless_on(&self) -> bool {
        let mut stats = MDLinkStats::default();
        unsafe { md_link_stats(self.inst.ptr, &mut stats) };
        stats.begin_count > stats.end_count
    }

    fn set_receiving(&self, on: bool) -> bool {
        // Attach at the bus, never by powering the game's radio: the client is
        // a console whose game is not in multiplayer (see md_link_test_attach).
        unsafe { md_link_test_attach(self.inst.ptr, if on { 1 } else { 0 }) == 0 }
    }

    fn receiving(&self) -> bool {
        unsafe { md_link_attached(self.inst.ptr) == 1 }
    }

    fn own_slot(&self) -> Option<u32> {
        let slot = unsafe { md_link_slot(self.inst.ptr) };
        (slot >= 0).then_some(slot as u32)
    }

    /// The whole turn, handed to the console's driver thread in one op. `now`
    /// is not passed through: the turn is stamped where it runs, so the reply
    /// schedule and the clock it is measured against share one home.
    fn turn(
        &self,
        dp: &Arc<dp_joiner::DownloadPlayJoiner>,
        st: &Arc<std::sync::Mutex<dp_joiner::State>>,
        _now: std::time::Instant,
    ) {
        // A console that stopped (power-off, error screen) answers no turns:
        // retire the loop instead of queueing into a dead driver, which is
        // the `did not answer a joiner turn` warning once per turn. The
        // driver loop exits on this same reason, so a nonzero reason here
        // always means the other side is gone, never merely slow.
        if unsafe { md_stop_reason(self.inst.ptr) } != MD_STOP_NONE {
            if st.lock().unwrap().retire() {
                log::info!("download-play joiner: console stopped; retiring the joiner loop");
            }
            return;
        }
        let (tx, rx) = std::sync::mpsc::channel();
        self.inst.link_ops.lock().push(LinkOp::JoinerTurn {
            state: st.clone(),
            dp: dp.clone(),
            reply: tx,
        });
        if rx.recv_timeout(LINK_OP_TIMEOUT).is_err() {
            log::warn!("download-play joiner: console driver did not answer a joiner turn");
        }
    }

    /// Booting the image edits CPU state and RAM, so it belongs on the driver
    /// thread too - relay it like a turn rather than writing from here.
    fn handoff(&self, image: &[u8], info: &dp_joiner::BootInfo, session: &dp_joiner::Session) -> bool {
        self.inst.link_boot_now(image, info, session)
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
/// Consecutive stalled windows after which the health watchdog resyncs a
/// console's bus state (its FIFO read offset and receive semaphores).
///
/// The cure is real - the historic freeze it was built for came after ~10s of
/// continuous STALL - but a console that is *waiting for a peer to finish
/// associating* looks identical window for window, and kicking it then discards
/// the very replies it goes on to miss: the 2026-09-25 MKDS join was kicked
/// 1.45s after the peer's radio came up, the host's next six acks said
/// `clientfail=0002`, and the client deauthenticated. **Metroid Prime - Hunters
/// four-console multi-card reproduces it exactly** (2026-09-29): kick #1 fires
/// 265ms after the host forms its group (`+161.244s`, frame 8655, right on the
/// host's `wireless ON (begin #3)`), and both guests that were mid-join
/// deauthenticate (reason `0003`) 7.8s later at frame 9068.
///
/// Override with `MD_KICK_AFTER_WINDOWS` to sweep without a rebuild; `0`
/// disables the kick entirely, which is what a route that has to bring three
/// guests in at once needs while the policy is being decided.
fn kick_after_windows() -> u32 {
    static KICK_AFTER_WINDOWS: std::sync::OnceLock<u32> = std::sync::OnceLock::new();
    *KICK_AFTER_WINDOWS.get_or_init(|| {
        std::env::var("MD_KICK_AFTER_WINDOWS")
            .ok()
            .and_then(|v| v.trim().parse::<u32>().ok())
            .unwrap_or(2)
    })
}


fn default_flip_key(player: usize) -> &'static str {
    // Peek applies to whichever session the keyboard currently controls
    // (digits 1-4 switch it in the frontend), so every player gets the same
    // key - right of the JKL cluster.
    let _ = player;
    "ShiftRight"
}

/// Keyboard bindings for a player's console: **one disjoint zone per console**,
/// because local splitscreen is several people at one keyboard, each with their
/// own keys. The frontend routes a key to the console whose zone names it (a
/// code in no zone moves nothing), which is what makes four players independent
/// - until 2026-10-15 all four consoles carried this same list and every key
/// drove only the console the mouse had selected, so a second player's presses
/// landed on player 1's console and stopped there (measured on a four-console
/// diag session: four players' zone keys held at once moved console 1's own
/// keypad mask to `0x000000C3` and left consoles 2-4 reading zero).
///
/// Each zone is a complete DS pad so any console can be played alone. The
/// selector digits (`Digit1..4`) pick the console the *mouse and peek* act on,
/// and are deliberately absent from every zone; `Numpad1..4` are not selectors
/// any more because 2/4/6/8 are console 3's d-pad.
///
/// Remapping is still a Phase 2 todo; these are the defaults it will edit.
fn default_keymap(player: usize) -> Vec<(&'static str, u32)> {
    match player {
        // Console 1: WASD cluster, right hand on J K / U I.
        0 => vec![
            ("KeyW", MD_BTN_UP),
            ("KeyA", MD_BTN_LEFT),
            ("KeyS", MD_BTN_DOWN),
            ("KeyD", MD_BTN_RIGHT),
            ("KeyJ", MD_BTN_A),
            ("KeyK", MD_BTN_B),
            ("KeyU", MD_BTN_Y),
            ("KeyI", MD_BTN_X),
            ("KeyQ", MD_BTN_L),
            ("KeyE", MD_BTN_R),
            ("Space", MD_BTN_START),
            ("ShiftLeft", MD_BTN_SELECT),
        ],
        // Console 2: arrow keys, right-hand punctuation row.
        1 => vec![
            ("ArrowUp", MD_BTN_UP),
            ("ArrowLeft", MD_BTN_LEFT),
            ("ArrowDown", MD_BTN_DOWN),
            ("ArrowRight", MD_BTN_RIGHT),
            ("Comma", MD_BTN_A),
            ("Period", MD_BTN_B),
            ("Semicolon", MD_BTN_Y),
            ("Slash", MD_BTN_X),
            ("BracketLeft", MD_BTN_L),
            ("BracketRight", MD_BTN_R),
            ("Enter", MD_BTN_START),
            ("Backslash", MD_BTN_SELECT),
        ],
        // Console 3: the numeric keypad, d-pad on 8/2/4/6.
        2 => vec![
            ("Numpad8", MD_BTN_UP),
            ("Numpad4", MD_BTN_LEFT),
            ("Numpad2", MD_BTN_DOWN),
            ("Numpad6", MD_BTN_RIGHT),
            ("Numpad0", MD_BTN_A),
            ("NumpadDecimal", MD_BTN_B),
            ("Numpad7", MD_BTN_Y),
            ("Numpad9", MD_BTN_X),
            ("NumpadDivide", MD_BTN_L),
            ("NumpadMultiply", MD_BTN_R),
            ("NumpadEnter", MD_BTN_START),
            ("NumpadSubtract", MD_BTN_SELECT),
        ],
        // Console 4: TFGH cluster, O/P/N/M buttons.
        _ => vec![
            ("KeyT", MD_BTN_UP),
            ("KeyF", MD_BTN_LEFT),
            ("KeyG", MD_BTN_DOWN),
            ("KeyH", MD_BTN_RIGHT),
            ("KeyO", MD_BTN_A),
            ("KeyP", MD_BTN_B),
            ("KeyN", MD_BTN_Y),
            ("KeyM", MD_BTN_X),
            ("KeyZ", MD_BTN_L),
            ("KeyX", MD_BTN_R),
            ("KeyV", MD_BTN_START),
            ("KeyC", MD_BTN_SELECT),
        ],
    }
}

#[derive(Serialize, Clone)]
struct SessionInfo {
    players: usize,
    rom: String,
    title: String,
    /// The loaded cartridge's 4-byte game code ("AMCE", ...), or "" when the
    /// ROM carries none. The identity a per-game profile is resolved from.
    game_code: String,
    /// The per-game profile for the loaded title, with the Settings switch
    /// folded in: `None` when the game code is not in the registry, in which
    /// case the link path is unchanged.
    per_game: Option<game_profile::GameProfile>,
    jit: bool,
    keymaps: Vec<Vec<(String, u32)>>,
    /// Per-player hold-to-peek key for the touch screen (frontend-display only).
    flip_keys: Vec<String>,
}

struct Session {
    players: usize,
    rom: PathBuf,
    title: String,
    /// The cartridge game code read once at load. Owned by the session so the
    /// per-game profile can be re-resolved when the Settings switch is flipped
    /// without touching the running consoles.
    game_code: String,
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
    /// The synthesized single-card Download Play joiner, present only for a
    /// title whose profile says `download_play`. `None` on every other title,
    /// where nothing about the link path changes.
    dp: Option<Arc<dp_joiner::DownloadPlayJoiner>>,
    dp_thread: Option<JoinHandle<()>>,
    started_at: std::time::Instant,
}

pub struct AppState {
    session: RwLock<Option<Arc<Session>>>,
    /// The Settings switch for per-game fixes: the one gate a game-specific
    /// patch reads. It lives on the app (not a session) so flipping it does not
    /// restart emulation and the next session inherits it. Default on.
    per_game_fixes: AtomicBool,
}

impl Default for AppState {
    fn default() -> Self {
        AppState {
            session: RwLock::new(None),
            per_game_fixes: AtomicBool::new(true),
        }
    }
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
        if let Some(t) = self.dp_thread.take() {
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

        // Prove the link-stats struct agrees field by field before any of its
        // numbers are believed: `delivered` beside a `rx` that only grew on the
        // drain side is exactly where a mis-mirrored field hides.
        {
            // u64 words, not bytes: the canary is read back as MDLinkStats, so
            // the buffer has to carry the struct's own alignment.
            let mut words = vec![0u64; std::mem::size_of::<MDLinkStats>() / 8];
            let c_size = md_link_stats_canary(words.as_mut_ptr().cast::<u8>(), words.len() * 8);
            let rust_size = std::mem::size_of::<MDLinkStats>() as u64;
            // Already inside the session's own unsafe block.
            let canary: &MDLinkStats = &*(words.as_ptr().cast::<MDLinkStats>());
            if c_size != rust_size {
                log::error!(
                    "link stats struct MISMATCH: C says {} bytes, Rust says {} - every link number will be wrong until md_link_stats is fixed in both places",
                    c_size,
                    rust_size
                );
            } else if let Some(field) = stats_layout_mismatch(canary) {
                log::error!(
                    "link stats struct FIELD MISMATCH at `{field}`: the Rust mirror of MDLinkStats no longer matches melonds_bridge.h - that field, and every one after it, now means something else"
                );
            } else {
                log::info!(
                    "link stats struct: {} bytes, every field in the place the other language keeps it",
                    c_size
                );
            }
        }

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

        // The stall kick tears a peer's join handshake when the host is simply
        // waiting for it to associate (see kick_after_windows), so say which
        // arm a run is in without having to infer it from the absence of a log
        // line: "never" is a policy, not a missing feature.
        log::info!(
            "link stall kick: {}",
            match kick_after_windows() {
                0 => "disabled (MD_KICK_AFTER_WINDOWS=0) - a stalled console is never resynced".to_string(),
                n => format!("after {n} stalled window(s) ({})", if std::env::var("MD_KICK_AFTER_WINDOWS").is_ok() { "MD_KICK_AFTER_WINDOWS" } else { "default" }),
            }
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

    // What each console calls itself on the air. The games' own room lists print
    // this string, so it is logged once per session: a room that still shows one
    // name four times is visible in the log, not only in the game.
    let console_names: Vec<String> = instances.iter().map(|i| read_console_name(i)).collect();
    log::info!("console names: {}", console_names.join(", "));

    let title = read_title(&instances[0]);
    let game_code = read_game_code(&instances[0]);
    // Resolve once, here, so the profile is fixed for the session and the log
    // carries the same answer the Settings panel shows. No registry entry means
    // `None` and the default path, unchanged.
    let per_game = game_profile::resolve(&game_code, state.per_game_fixes.load(Ordering::SeqCst));
    log::info!(
        "loaded game: {} (code {}) - {}",
        if title.is_empty() { "?" } else { &title },
        if game_code.is_empty() { "????" } else { &game_code },
        game_profile::describe(per_game.as_ref()),
    );

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
                    // The receive total, attributed (melonds_bridge.h v3):
                    // frames this console's own game read, frames a scripted
                    // drain read (the Link test, the link monitor), and frames
                    // the router handed the console whether or not anyone read
                    // them. `rx` is the sum of the first two, so a console
                    // whose rx climbs while its game reads nothing is a console
                    // a host-side probe has been draining.
                    rx_game: u64,
                    rx_drained: u64,
                    delivered: u64,
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
                            rx_game: stats.packets_read_game,
                            rx_drained: stats.packets_read_drained,
                            delivered: stats.packets_delivered,
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
                        let d_rx_game = snap.rx_game.wrapping_sub(p.rx_game);
                        let d_delivered = snap.delivered.wrapping_sub(p.delivered);
                        // Wireless is "on" between a begin and its matching end.
                        let wireless_on = stats.begin_count > stats.end_count;
                        // "Active" means a game accepted a packet, which is what
                        // the stall clock beside it measures: a host-side drain
                        // (the Link test, the link monitor) reads packets too,
                        // and counting those as traffic would hide the stall it
                        // was sent to find.
                        let rx_active = d_rx_game > 0;
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
                        // 0 = never kick (MD_KICK_AFTER_WINDOWS=0), which is the
                        // arm a multi-guest join needs: see kick_after_windows.
                        let kick_after_windows: u32 = kick_after_windows();
                        if state == "LINK STALL" {
                            stall_windows[i] = stall_windows[i].saturating_add(1);
                        } else {
                            stall_windows[i] = 0;
                        }

                        let kick_due = kick_after_windows > 0
                            && stall_windows[i] >= kick_after_windows
                            && stall_windows[i] <= kick_after_windows + 2;
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
                                    stall_windows[i] - kick_after_windows + 1
                                );
                            }
                        } else if state == "LINK STALL" && verbose {
                            // Attribution: a stall we deliberately did NOT kick,
                            // so a later failure cannot be blamed on a kick that
                            // never happened - and which arm left it alone, since
                            // "the kick is off" and "this console already had its
                            // one kick" are different runs to compare.
                            log::info!(
                                "p{} stalled (window {}) - left alone ({})",
                                i + 1,
                                stall_windows[i],
                                if kick_after_windows == 0 {
                                    "stall kick disabled (MD_KICK_AFTER_WINDOWS=0)".to_string()
                                } else {
                                    format!(
                                        "at, or past, the {kick_after_windows}-window threshold, which fires only once"
                                    )
                                }
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
                            "health p{}: {} · frames {}(+{}) · picture {}(+{}) · tx {}(+{}, {}B(+{})) · rx {}(+{}, {}B(+{})) [game {}(+{}), drain {}] · delivered {}(+{}) · slots 0x{:x} · wifi on/off {}/{} · pc {}/{}",
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
                            snap.rx_game,
                            d_rx_game,
                            snap.rx_drained,
                            snap.delivered,
                            d_delivered,
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

    // The synthesized single-card Download Play joiner. Present only for a
    // title whose profile says `download_play` (and only when there is a peer
    // to be the host), so every other title's link path is untouched; within
    // that title it is armed by the same Settings switch as every other
    // per-game behavior (GameProfile::download_play_joiner).
    //
    // **Two consoles only** (2026-10-15). The joiner exists for this project's
    // measured single-card route, which is a two-console configuration: console
    // 1 hosts, console 2's radio plays a Download Play client. Arming it in a
    // three- or four-console session puts that synthesized client on the *last*
    // console, where it is a fourth radio with no game of its own - and it does
    // not sit idle. Measured on Tetris DS (ATRE) with four consoles and the
    // shipped defaults (`logs/r2_tetris.log.gz`): as soon as console 1's room came
    // up, console 4's joiner heard its beacon on channel 13, answered it
    // (`the host registered this client: association status 0000, AID C001`) and
    // pulled console 1's own game into its Download Play engine - the host's
    // next frames are a DP NameRequest, an RSA frame stating 1,004,732 bytes and
    // a stream of image packets whose first payload is `NINTENDO` - while two
    // real consoles sat in the same room as AIDs 2 and 3 (`mask 0006`). The
    // room was hosting a download instead of hosting the room. With the joiner
    // disarmed (`logs/r2b_tetris.log.gz`, the Settings switch, nothing else
    // changed) only the two real consoles associate and no download starts. A
    // session of 3-4 consoles is a link session: the user has that many carts,
    // which is what the phantom client takes away.
    let (dp, dp_thread) = match per_game.as_ref().filter(|p| p.download_play && n == 2) {
        None => (None, None),
        Some(profile) => {
            let joiner = n - 1;
            let dp = Arc::new(dp_joiner::DownloadPlayJoiner::new(
                profile.download_play_joiner(),
                joiner,
            ));
            let bus: Arc<dyn dp_joiner::JoinerBus> = Arc::new(InstanceJoinerBus {
                inst: Arc::clone(&instances[joiner]),
            });
            log::info!(
                "download-play joiner: console {} is the synthesized client, {}",
                joiner + 1,
                if profile.download_play_joiner() {
                    "armed - it will answer a host's beacon with auth + assoc"
                } else {
                    "disarmed (per-game fixes off)"
                }
            );
            let stop = Arc::clone(&stop);
            let d = Arc::clone(&dp);
            let handle = std::thread::Builder::new()
                .name("dpjoiner".into())
                .spawn(move || dp_joiner::run(bus, stop, d))
                .expect("spawn download-play joiner");
            (Some(dp), Some(handle))
        }
    };

    let session = Arc::new(Session {
        players: n,
        rom: rom.clone(),
        title,
        game_code,
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
        dp,
        dp_thread,
        started_at: std::time::Instant::now(),
    });

    *state.session.write() = Some(Arc::clone(&session));

    Ok(SessionInfo {
        players: n,
        rom: rom.display().to_string(),
        title: session.title.clone(),
        game_code: session.game_code.clone(),
        per_game,
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
}

/// The loaded cartridge's 4-byte game code, verbatim ("AMCE", "A2DE", ...).
/// Empty when no cart carries one. Kept untrimmed: the code is fixed-width, so a
/// trailing space is part of it and not padding to strip.
fn read_game_code(inst: &Instance) -> String {
    let mut buf = [0i8; 8];
    let rc = unsafe { md_rom_game_code(inst.ptr, buf.as_mut_ptr(), buf.len()) };
    if rc != 0 {
        return String::new();
    }
    let bytes: Vec<u8> = buf.iter().take_while(|b| **b != 0).map(|b| *b as u8).collect();
    String::from_utf8_lossy(&bytes).to_string()
}

/// The name this console advertises to the others ("Player 1".."Player 4"),
/// read back out of the firmware the guest itself reads - see
/// `apply_console_mac` and `md_console_name`. Empty before a ROM is loaded, and
/// when a build's bridge predates the accessor.
fn read_console_name(inst: &Instance) -> String {
    let mut buf = [0i8; 32];
    let rc = unsafe { md_console_name(inst.ptr, buf.as_mut_ptr(), buf.len() as i32) };
    if rc < 0 {
        return String::new();
    }
    let bytes: Vec<u8> = buf.iter().take_while(|b| **b != 0).map(|b| *b as u8).collect();
    String::from_utf8_lossy(&bytes).to_string()
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
    // MD_NOJIT=1 runs the autostarted consoles on the ARM interpreter. The
    // JIT reports `R[15]` as the start of the block an instruction ended up in,
    // so a hook that records the writer of a register or a buffer (the wifi io
    // recorder's `pc=` fields) needs this to name the instruction rather than
    // the block. The coretest harness has the same switch as `--no-jit`.
    let jit = !matches!(std::env::var("MD_NOJIT").as_deref(), Ok(v) if !v.is_empty() && v != "0");
    log::info!("autostart: {players} player(s), rom {rom}, jit={jit}");
    let state = app.state::<AppState>();
    match start_session_inner(&state, players, rom, jit) {
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
                    "autostart: automatic link selftest left off (it puts test frames on \
                     the consoles' bus; set MD_AUTOSTART_SELFTEST=1 to run it)"
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
    let enabled = state.per_game_fixes.load(Ordering::SeqCst);
    Ok(state.session.read().as_ref().map(|s| SessionInfo {
        players: s.players,
        rom: s.rom.display().to_string(),
        title: s.title.clone(),
        game_code: s.game_code.clone(),
        per_game: game_profile::resolve(&s.game_code, enabled),
        jit: s.jit,
        keymaps: s.keymaps.clone(),
        flip_keys: s.flip_keys.clone(),
    }))
}

/// The per-game answer for the app: the switch, the loaded game code, and the
/// profile it resolves to. One shape for both the read and the toggle, so the
/// UI never sees two different representations of the same state.
fn per_game_payload(state: &AppState, enabled: bool) -> serde_json::Value {
    let current = state.session.read();
    let (code, profile) = match current.as_ref() {
        Some(s) => (s.game_code.clone(), game_profile::resolve(&s.game_code, enabled)),
        None => (String::new(), None),
    };
    serde_json::json!({
        "enabled": enabled,
        "gameCode": code,
        "profile": profile,
        "description": game_profile::describe(profile.as_ref()),
    })
}

/// Read the per-game-fix switch and the profile it currently resolves to.
/// Answers without a session too, so Settings can show the switch before any
/// ROM is loaded.
#[tauri::command]
fn per_game_state(state: tauri::State<'_, AppState>) -> Result<serde_json::Value, String> {
    let enabled = state.per_game_fixes.load(Ordering::SeqCst);
    Ok(per_game_payload(&state, enabled))
}

/// Flip the per-game-fix switch. The profile is re-resolved against the loaded
/// game code and returned, so a caller sees the new answer without restarting.
#[tauri::command]
fn set_per_game_fix(
    state: tauri::State<'_, AppState>,
    enabled: bool,
) -> Result<serde_json::Value, String> {
    state.per_game_fixes.store(enabled, Ordering::SeqCst);
    let payload = per_game_payload(&state, enabled);
    // The switch is the one gate a per-game behavior reads, so it has to reach
    // a running session's joiner too, not just the next session's resolution.
    if let Some(session) = state.session.read().as_ref() {
        if let Some(dp) = session.dp.as_ref() {
            let on = game_profile::resolve(&session.game_code, enabled)
                .map(|p| p.download_play_joiner())
                .unwrap_or(false);
            if dp.is_armed() != on {
                dp.set_armed(on);
                log::info!(
                    "per-game fix switch {} the download-play joiner",
                    if on { "arms" } else { "disarms" }
                );
            }
        }
    }
    log::info!(
        "per-game fixes {} - {}",
        if enabled { "on" } else { "off" },
        payload["description"].as_str().unwrap_or(""),
    );
    Ok(payload)
}

/// The synthesized Download Play joiner's own state, as a value: whether this
/// title has one, whether it is armed, and every frame it has sent or seen.
/// A disarmed joiner reads all-zero here, and that is the gate's proof - the
/// off arm is "zero bus operations", not "no visible change".
fn dp_state(session: &Session) -> serde_json::Value {
    use dp_joiner::Counters;
    match session.dp.as_ref() {
        None => serde_json::json!({
            "present": false,
            "note": "this session has no download-play joiner (a profile with download_play, and a two-console session, are both required)",
        }),
        Some(dp) => {
            let Counters {
                beacons,
                auth_sent,
                auth_recv,
                assoc_sent,
                assoc_recv,
                replies_sent,
                host_frames,
                deauths,
                last_deauth_reason,
                last_auth_status,
                last_assoc_status,
                last_assoc_aid,
                commands_recv,
                last_command,
                last_cmd_slave_flags,
                acks_recv,
                replies_collected,
                image_packets,
                image_bytes,
                image_next,
                image_total_bytes,
                transfer,
                radio_hold,
                frames_requeued,
                beacons_served,
            } = &dp.counters;
            serde_json::json!({
                "present": true,
                "armed": dp.is_armed(),
                "booted": dp.is_booted(),
                "radioHold": radio_hold.load(Ordering::SeqCst),
                "transfer": dp_joiner::transfer_name(transfer.load(Ordering::SeqCst)),
                "joiner": dp.joiner.load(Ordering::SeqCst) + 1,
                "beacons": beacons.load(Ordering::SeqCst),
                "authSent": auth_sent.load(Ordering::SeqCst),
                "authRecv": auth_recv.load(Ordering::SeqCst),
                "assocSent": assoc_sent.load(Ordering::SeqCst),
                "assocRecv": assoc_recv.load(Ordering::SeqCst),
                "repliesSent": replies_sent.load(Ordering::SeqCst),
                "hostFrames": host_frames.load(Ordering::SeqCst),
                "deauths": deauths.load(Ordering::SeqCst),
                "lastDeauthReason": last_deauth_reason.load(Ordering::SeqCst),
                "lastAuthStatus": last_auth_status.load(Ordering::SeqCst),
                "lastAssocStatus": last_assoc_status.load(Ordering::SeqCst),
                "lastAssocAid": last_assoc_aid.load(Ordering::SeqCst),
                "commandsRecv": commands_recv.load(Ordering::SeqCst),
                "lastCommand": last_command.load(Ordering::SeqCst),
                "lastCmdSlaveFlags": last_cmd_slave_flags.load(Ordering::SeqCst),
                "acksRecv": acks_recv.load(Ordering::SeqCst),
                "repliesCollected": replies_collected.load(Ordering::SeqCst),
                "requeued": frames_requeued.load(Ordering::SeqCst),
                "beaconsServed": beacons_served.load(Ordering::SeqCst),
                "imagePackets": image_packets.load(Ordering::SeqCst),
                "imageBytes": image_bytes.load(Ordering::SeqCst),
                "imageNext": image_next.load(Ordering::SeqCst),
                "imageTotalBytes": image_total_bytes.load(Ordering::SeqCst),
            })
        }
    }
}

/// Frame request. The response is raw bytes: per player, an 8-byte little-endian
/// snapshot sequence number followed by the top framebuffer and then the bottom
/// one, each 256x192 RGBA (the bridge's 0xAARRGGBB words reinterpreted as
/// BGRA-in-memory bytes, which is exactly what a canvas ImageData wants after
/// the frontend handles the alpha channel). Raw bytes keep a 60fps read at tens
/// of MB/s instead of JSON's gigahertz of commas.
///
/// Split out of the command so the browser host (`browser_host.rs`) can put the
/// identical payload on its own wire: the two transports must not drift, and
/// the page decodes one format regardless of which one it arrived on. No session
/// means an empty body, which is what tells the frontend to show the placeholder.
fn frames_payload(state: &AppState) -> Vec<u8> {
    let current = state.session.read();
    let Some(session) = current.as_ref() else {
        return Vec::new();
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
    payload
}

#[tauri::command]
fn get_frames(state: tauri::State<'_, AppState>) -> Result<tauri::ipc::Response, String> {
    Ok(tauri::ipc::Response::new(frames_payload(state.inner())))
}

/// One fetch of audio, in the same shape as `get_frames`: a header, then one
/// chunk per requested console. Polled by the frontend only while online play
/// is hosting, so a local session pays nothing for a stream nobody is watching.
///
/// Layout (little endian): `u32 rate`, `u32 seatCount`, then per seat
/// `u32 seat`, `u32 byteLen`, `byteLen` bytes of interleaved stereo s16.
const MD_AUDIO_MAX_FRAMES: usize = 8192; // ~170ms at 48kHz; a DS frame is ~1092

/// The bytes `get_audio` answers with (see the layout note above). Split out of
/// the command for the same reason as `frames_payload`.
fn audio_payload(state: &AppState, seats: &[u32]) -> Vec<u8> {
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
fn get_audio(
    state: tauri::State<'_, AppState>,
    seats: Vec<u32>,
) -> Result<tauri::ipc::Response, String> {
    Ok(tauri::ipc::Response::new(audio_payload(
        state.inner(),
        &seats,
    )))
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
            "name": read_console_name(inst),
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

/// What one directed link probe saw. `delivered` is the bus's own ledger (the
/// router handed the frame to the receiving console) and `observed`/
/// `payload_ok` say which consumer read it back and whether the bytes were the
/// ones the wire carried.
struct ProbeOutcome {
    sent: bool,
    delivered: bool,
    observed: bool,
    payload_ok: bool,
    ts_ok: bool,
    seen_by: String,
}

/// One directed verification over the shared bus: arm an expectation on the
/// receiver, send from the sender, then ask what happened to the frame.
///
/// Why it is not "send, then drain the peer's inbox and look for the bytes":
/// the peer's own game polls its receive slot from inside the emulation every
/// frame, so it wins that race whenever it is polling - and its read is just as
/// much a proof of delivery as a drain's. Measured 2026-09-30 on the app: the
/// Link test drained nothing in two states while every console's delivery
/// ledger was climbing, because the games had already consumed the frames. The
/// ledger is where a delivery can be attributed; the armed expectation is how
/// the payload is checked whichever consumer did the reading.
///
/// The expectation is the frame *as the wire leaves it*: `md_probe_arm`
/// applies the same trailing-FCS transform the send path does. Arming it with
/// the sender's bytes instead is what made the old button report FAILED on a
/// healthy bus - it waited for a nonce the wire had already overwritten with a
/// checksum, in every state, while its own health lines showed the traffic.
fn probe_direction(
    from: &Arc<Instance>,
    to: &Arc<Instance>,
    payload: &[u32],
    ts: u64,
    wait: Duration,
) -> ProbeOutcome {
    let wire = words_to_bytes(payload);
    let mut out = ProbeOutcome {
        sent: false,
        delivered: false,
        observed: false,
        payload_ok: false,
        ts_ok: false,
        seen_by: String::new(),
    };

    let before = link_delivered(to);

    // Refuses (rather than silently passing) a payload bigger than the probe's
    // own expectation buffer.
    if unsafe { md_probe_arm(to.ptr, wire.as_ptr(), wire.len(), ts) } != 0 {
        return out;
    }

    out.sent = from
        .link_send_now(&wire, ts, "link probe")
        .map(|queued| queued > 0)
        .unwrap_or(false);

    if out.sent {
        let deadline = std::time::Instant::now() + wait;
        loop {
            let mut pr = MDProbeResult::default();
            if unsafe { md_probe_result(to.ptr, &mut pr) } == 0 && pr.observed != 0 {
                out.observed = true;
                out.payload_ok = pr.payload_ok != 0;
                out.ts_ok = pr.ts_ok != 0;
                out.seen_by = pr.seen_by_str();
                break;
            }
            if std::time::Instant::now() >= deadline {
                break;
            }
            // No consumer has taken it yet, so be one: a frame the receiving
            // console is not polling for is still a delivery, and reading it
            // here is what proves its payload rather than leaving it queued.
            let _ = to.link_drain_now("link probe");
            std::thread::sleep(Duration::from_millis(5));
        }
    }

    out.delivered = link_delivered(to) > before;
    unsafe { md_probe_disarm(to.ptr) };
    out
}

/// The bus's delivery count for one console: frames the router handed it,
/// whether or not its game ever asked for them.
fn link_delivered(inst: &Arc<Instance>) -> u64 {
    let mut stats = MDLinkStats::default();
    unsafe { md_link_stats(inst.ptr, &mut stats) };
    stats.packets_delivered
}

/// Host-side link selftest: attach every console to the bus (at the bus, never
/// by powering the game's radio - see md_link_test_attach), then verify one
/// packet from every console to every peer (full mesh) - delivered to the peer
/// and read back with the bytes and timestamp the sender queued. Uses the same
/// primitive the emulated WiFi hardware calls into, so a pass proves slot
/// routing and the LocalMP queues work inside the app - not just in the C++
/// smoke test. Runs only on demand (button / verification runs), and while a
/// game has its wireless on it does the safe half instead (see the resync path
/// below): a test frame inside a live lobby is foreign traffic.
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
    // Attach every console to the bus for the duration, at the bus itself:
    // no ARM7 register writes, and no bit in md_link_connected_mask. The
    // console's radio and the game's own driver are left exactly as they were.
    // This is the half the button used to do by powering each console's
    // wireless on and off under the game's own WiFi driver, which left a
    // running game unable to bring its radio up again (see md_link_test_attach).
    for inst in &session.instances {
        if unsafe { md_link_test_attach(inst.ptr, 1) } != 0 {
            return Err("the link bus is not up".into());
        }
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
            let ts = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map(|d| d.as_millis() as u64)
                .unwrap_or(0);

            // The frame's identity lives in its first four words, and its last
            // word is the wire's own FCS slot - so the nonce goes in the body
            // (word four), which the wire carries through untouched, and the
            // trailing word is left for SendPacketGeneric/ApplyWireFCS to fill.
            // A 20-byte frame is also below the core's 12+24 minimum, which is
            // what keeps the peer's game from logging or acting on a probe.
            let payload: [u32; 5] = [MAGIC, from as u32, to as u32, nonce, 0];
            let out = probe_direction(from_inst, to_inst, &payload, ts, Duration::from_millis(400));

            results.push(serde_json::json!({
                "from": from + 1,
                "to": to + 1,
                "sent": out.sent,
                "delivered": out.delivered,
                "observed": out.observed,
                "payloadOk": out.payload_ok,
                "tsOk": out.ts_ok,
                "seenBy": out.seen_by,
            }));
        }
    }

    // Leave the machines as we found them: the consoles come off the test
    // attachment, and then every one is flushed - drain what is queued, then
    // re-anchor the read offset - because a test frame that outlives the test
    // is a foreign frame lying in wait for whatever the game does next, which
    // is exactly how one of these turned up inside an MKDS lobby minutes
    // later. Detaching after the flush also means the frames are read while
    // this console is still on the bus, rather than left for its game.
    for inst in &session.instances {
        let _ = inst.link_drain_now("link selftest flush");
    }
    for inst in &session.instances {
        let _ = inst.link_resync_now("link selftest flush");
    }
    for inst in &session.instances {
        unsafe { md_link_test_attach(inst.ptr, 0) };
    }

    Ok(serde_json::json!({ "mode": "inject", "injected": true, "results": results }))
}

/// Continuous link verification: the selftest the user asked to "always be
/// running". Every 10s, when at least two consoles have their wireless on
/// (they are in a multiplayer menu or session) but NO console accepted any
/// packet over the last window, it fires one probe packet and expects it on
/// the next console. Delivered and read back intact = the bus works;
/// undelivered = the same drifted-FIFO/semaphore state a manual "Link test"
/// click cured, so both consoles get the resync kick. Constraints honored on
/// purpose:
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
                // a window with traffic needs no probe at all. The counter that
                // decides this is the *game* read half, not the total: a probe
                // of our own is a drain, and counting drains here would let one
                // probe make the next window look like traffic and silence the
                // verifier for good (the reason the receive total is split).
                let mut game_active = false;
                let mut wireless_on = false;
                for (i, inst) in insts.iter().enumerate() {
                    let mut stats = MDLinkStats::default();
                    unsafe { md_link_stats(inst.ptr, &mut stats) };
                    if stats.begin_count > stats.end_count {
                        wireless_on = true;
                    }
                    if stats.packets_read_game > last_rx[i] {
                        game_active = true;
                    }
                    last_rx[i] = stats.packets_read_game;
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
                // no game traffic, and sends/observes through the consoles'
                // own driver threads like everything else here. Same instrument
                // as the button: the delivery ledger says the frame reached the
                // peer, the armed expectation says its bytes did too. Matching
                // a drain's payload against the sender's bytes (what this used
                // to do) can never succeed - the wire rewrites the frame's
                // trailing word with its FCS - and so reported every probe as
                // undelivered.
                let nonce = now_ms() as u32 | 1;
                let payload: [u32; 5] = [0x4D535430 /* 'MST0' */, 0, 1, nonce, 0];
                let probe = probe_direction(
                    &insts[0],
                    &insts[1],
                    &payload,
                    now_ms(),
                    Duration::from_millis(300),
                );
                let ok = probe.sent && probe.delivered && probe.observed && probe.payload_ok;
                if !ok {
                    log::warn!(
                        "link monitor: bus probe p1->p2 sent={} delivered={} observed={} (by {}) payloadOk={} - resyncing both consoles",
                        probe.sent,
                        probe.delivered,
                        probe.observed,
                        if probe.seen_by.is_empty() { "nobody" } else { &probe.seen_by },
                        probe.payload_ok,
                    );
                }
                if ok {
                    log::info!(
                        "link monitor: bus probe p1->p2 delivered and read back intact by {} - the bus works with no game talking",
                        if probe.seen_by.is_empty() { "nobody" } else { &probe.seen_by },
                    );
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

/// Serve the app's own UI on loopback, for a browser to open. This is how a
/// Linux host plays online at all: its WebView has no WebRTC, but Chrome and
/// Firefox do (see browser_host.rs).
#[tauri::command]
fn browser_host_start() -> Result<serde_json::Value, String> {
    let host = browser_host::start()?;
    let url = browser_host::url_for(&host);
    log::warn!(
        "browser host listening on 127.0.0.1:{} - it carries the emulator's own \
         command interface, so the token in the URL is the credential",
        host.port
    );
    Ok(serde_json::json!({ "port": host.port, "url": url }))
}

/// Hands the URL to the desktop's browser. Failure is not fatal: the panel
/// shows the URL either way, and the user can paste it.
#[tauri::command]
fn browser_host_open(url: String) -> Result<(), String> {
    // MD_BROWSER names a browser explicitly (a test does that, and so does
    // anybody whose default browser is the one without WebRTC).
    let explicit = std::env::var("MD_BROWSER").ok().filter(|s| !s.is_empty());
    let attempts: Vec<(&str, Vec<&str>)> = match explicit.as_deref() {
        Some(browser) => vec![(browser, vec![url.as_str()])],
        None if cfg!(target_os = "macos") => vec![("open", vec![url.as_str()])],
        None if cfg!(target_os = "windows") => vec![
            ("cmd", vec!["/C", "start", "", url.as_str()]),
        ],
        None => vec![
            ("xdg-open", vec![url.as_str()]),
            ("gio", vec!["open", url.as_str()]),
        ],
    };
    let mut last = String::new();
    for (program, args) in attempts {
        match std::process::Command::new(program).args(args).spawn() {
            Ok(_) => {
                log::info!("browser host: opened {url} with {program}");
                return Ok(());
            }
            Err(err) => last = format!("{program}: {err}"),
        }
    }
    Err(format!("could not open a browser ({last}) - the URL is in the panel"))
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
    // A click that cannot happen *throws*, the way `key` already does for a key
    // this page cannot act on, so a caller can tell it apart from one that
    // happened. It used to return the message and exit 0, which left the exact
    // trap every route in this project has fallen into: a scripted step that did
    // nothing looks exactly like one that worked (2026-09-30 playtest - `click
    // '#nope'` and `click '#selftest'` on an idle app both read as success).
    if (!e) throw new Error('not found: ' + sel);
    // A disabled control ignores the click entirely, so reporting success for
    // one is a lie the driver cannot detect: `click #selftest` on an idle app
    // (the button is disabled when nothing runs) read as "clicked #selftest"
    // and nothing happened (2026-09-28 playtest).
    if (e.disabled) {
      throw new Error('disabled: ' + sel + ' (nothing to click - is a session running?)');
    }
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
    //
    // `data` must always be a *string*. `JSON.stringify(undefined)` returns
    // undefined, so an expression whose value is `undefined` used to arrive as
    // no key at all - Tauri rejected the invoke with "invalid args `data` for
    // command `md_ctrl_report`" (an unhandled rejection, so an ERROR line in the
    // log) and the reply never came, which turned an ordinary query into the
    // full timeout under a message blaming the window. Found by driving the
    // app's own empty state as a user (2026-09-30).
    let js = format!(
        "{prelude}(async function(){{try{{const v=await ({body});window.__TAURI__.core.invoke('md_ctrl_report',{{id:{id},data:(typeof v==='string')?v:((JSON.stringify(v)===undefined)?String(v):JSON.stringify(v))}});}}\
         catch(e){{window.__TAURI__.core.invoke('md_ctrl_report',{{id:{id},data:'__ERR__ '+((e&&e.message)?e.message:String(e))}}).catch(function(){{}});}}}})().catch(function(){{}});",
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
    let enabled = state.per_game_fixes.load(Ordering::SeqCst);
    let per_game = game_profile::resolve(&session.game_code, enabled);
    serde_json::json!({
        "active": true,
        "players": session.players,
        "rom": session.rom.display().to_string(),
        "title": session.title.clone(),
        "game_code": session.game_code.clone(),
        "per_game": per_game,
        "per_game_desc": game_profile::describe(per_game.as_ref()),
        "elapsed_s": session.started_at.elapsed().as_secs(),
        "link_mask": unsafe { md_link_connected_mask() },
        "dp": dp_state(session),
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
/// A hex address with or without `0x`/`0X`, the way a caller writes one on a
/// command line (`02000000`, `0x2000000`).
fn ctrl_parse_hex(s: &str) -> Option<u32> {
    let t = s.trim();
    let t = t.strip_prefix("0x").or_else(|| t.strip_prefix("0X")).unwrap_or(t);
    u32::from_str_radix(t, 16).ok()
}

/// Read a console's main RAM through the bridge's `md_read_ram`, which takes a
/// 4-byte-aligned address and length. Answers the bytes and the ARM9's own
/// 32-bit words, so a caller can read a game's variables rather than argue
/// about its pictures.
fn ctrl_mem(
    app: &tauri::AppHandle,
    player: usize,
    addr: u32,
    len: usize,
    cpu: i32,
) -> Result<serde_json::Value, String> {
    let state = app.state::<AppState>();
    let current = state.session.read();
    let session = current.as_ref().ok_or("no session")?;
    let inst = session
        .instances
        .get(player)
        .ok_or_else(|| format!("player {} is not running", player + 1))?;

    if len == 0 || len > 4096 {
        return Err(format!("len must be 4..=4096 bytes, got {len}"));
    }
    if (addr & 3) != 0 || (len & 3) != 0 {
        return Err(
            "addr and len must be multiples of 4: the bridge reads whole words".into(),
        );
    }
    if cpu != 7 && cpu != 9 {
        return Err(format!("cpu must be 9 (ARM9, the default) or 7 (ARM7), got {cpu}"));
    }

    let mut buf = vec![0u8; len];
    // The ARM9 path is the established one, so it keeps its own call rather than
    // being routed through the two-CPU entry point.
    let rc = unsafe {
        if cpu == 7 {
            md_read_ram_cpu(inst.ptr, cpu, addr, buf.as_mut_ptr(), len)
        } else {
            md_read_ram(inst.ptr, addr, buf.as_mut_ptr(), len)
        }
    };
    if rc != 0 {
        let err = unsafe { md_last_error(inst.ptr) };
        let msg = if err.is_null() {
            String::new()
        } else {
            unsafe { std::ffi::CStr::from_ptr(err) }
                .to_string_lossy()
                .into_owned()
        };
        return Err(format!("md_read_ram_cpu failed ({rc}): {msg}"));
    }

    let hex = buf
        .iter()
        .map(|b| format!("{b:02X}"))
        .collect::<Vec<_>>()
        .join(" ");
    let words = buf
        .chunks_exact(4)
        .map(|c| format!("{:08X}", u32::from_le_bytes([c[0], c[1], c[2], c[3]])))
        .collect::<Vec<_>>();

    Ok(serde_json::json!({
        "player": player + 1,
        "cpu": cpu,
        "addr": format!("{addr:08X}"),
        "len": len,
        "hex": hex,
        "words": words,
    }))
}

/// Write a console's main RAM through the bridge's `md_write_ram`, then answer
/// the bytes as they now stand (the same shape `ctrl_mem` answers).
///
/// This exists because "the game is waiting for X" is a claim about a *value*,
/// and the only way to settle it is to change the value and watch the console
/// move: a read-only instrument can show that a poll's exit condition is false
/// but never that the poll is what holds the console. The bridge has had the
/// write half since the identity-stamp block (see melonds_bridge.h), this just
/// puts it on the same channel as the read. Diagnostics only, 4-byte words.
fn ctrl_poke(
    app: &tauri::AppHandle,
    player: usize,
    addr: u32,
    words: &[u32],
    cpu: i32,
) -> Result<serde_json::Value, String> {
    if words.is_empty() || words.len() > 1024 {
        return Err(format!("poke needs 1..=1024 words, got {}", words.len()));
    }
    if (addr & 3) != 0 {
        return Err("poke's addr must be a multiple of 4: the bridge writes whole words".into());
    }
    if cpu != 7 && cpu != 9 {
        return Err(format!("poke's cpu must be 9 (ARM9, the default) or 7 (ARM7), got {cpu}"));
    }
    let mut buf = Vec::with_capacity(words.len() * 4);
    for w in words {
        buf.extend_from_slice(&w.to_le_bytes());
    }

    let state = app.state::<AppState>();
    let current = state.session.read();
    let session = current.as_ref().ok_or("no session")?;
    let inst = session
        .instances
        .get(player)
        .ok_or_else(|| format!("player {} is not running", player + 1))?;

    let rc = unsafe {
        if cpu == 7 {
            md_write_ram_cpu(inst.ptr, cpu, addr, buf.as_ptr().cast(), buf.len())
        } else {
            md_write_ram(inst.ptr, addr, buf.as_ptr().cast(), buf.len())
        }
    };
    if rc != 0 {
        let err = unsafe { md_last_error(inst.ptr) };
        let msg = if err.is_null() {
            String::new()
        } else {
            unsafe { std::ffi::CStr::from_ptr(err) }
                .to_string_lossy()
                .into_owned()
        };
        return Err(format!("write failed ({rc}): {msg}"));
    }
    drop(current);

    // Read it back rather than echo the request: what the console's bus kept can
    // differ from what was asked for, and that difference is the interesting
    // part. The read-back is on the same CPU's space as the write.
    ctrl_mem(app, player, addr, buf.len(), cpu)
}

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

    // The program counters, read on demand. The five-second digest carries
    // them too, and five seconds is the wrong resolution for the question the
    // parked game asks - "is the CPU stuck in this loop, or visiting it?" - so
    // a caller can sample them as fast as it can talk.
    let pc9 = unsafe { md_get_pc(inst.ptr, 9) };
    let pc7 = unsafe { md_get_pc(inst.ptr, 7) };

    let mut regs9 = [0u32; 16];
    let mut regs7 = [0u32; 16];
    unsafe {
        md_get_regs(inst.ptr, 9, regs9.as_mut_ptr());
        md_get_regs(inst.ptr, 7, regs7.as_mut_ptr());
    }
    let hex_regs = |r: &[u32; 16]| -> Vec<String> {
        r.iter().map(|v| format!("{v:08X}")).collect()
    };

    // The interrupt level, from the same flight recorder the health thread
    // prints: the per-source wifi raise/ack counters and the wifi register
    // snapshot taken at export time, plus the two program counters. A black
    // picture with the CPUs still moving is a game waiting on an interrupt,
    // and this is where that shows as numbers instead of an inference.
    let mut wifi = MDMPDigest::default();
    unsafe { md_link_mp_digest(inst.ptr, &mut wifi) };

    // The link's own ledger for this console, beside the game's: frames the bus
    // handed it, how many of those its own game read, how many a scripted drain
    // read. The health line prints the same three every five seconds; a timeline
    // across a deauth needs them at whatever rate the caller can talk.
    let mut link = MDLinkStats::default();
    unsafe { md_link_stats(inst.ptr, &mut link) };
    let irq_list = |arr: &[u64; 16]| -> String {
        let mut out = Vec::new();
        for (i, v) in arr.iter().enumerate() {
            if *v > 0 {
                out.push(format!("{i}:{v}"));
            }
        }
        if out.is_empty() { "none".to_string() } else { out.join(" ") }
    };

    // The wifi IRQ is ARM7 `IRQ_Wifi`, which is bit 24 - `Wifi.cpp` raises it
    // with `NDS.SetIRQ(1, IRQ_Wifi)` and `NDS::SetIRQ` sets `IF[1] |= 1 << irq` -
    // so it is bit 8 of the high halfword, NOT bit 7. (Bit 23 is `IRQ_SPI`: this
    // read `& 0x80` until 2026-09-29 and reported the wifi interrupt as disabled
    // on both consoles while `IE7 0104` had it enabled.) IME says whether that
    // CPU can take interrupts at all.
    const IRQ_WIFI_IE7: u16 = 1 << (24 - 16);
    let ie7_hi = unsafe { md_read_io16(inst.ptr, 7, 0x0400_0212) };

    // The ARM9's own interrupt registers, read at this instant rather than from
    // the digest's snapshot. The digest's `IF`/`IE` fields are not these
    // registers (2026-09-29: they read `IE 0000` while IME was 1 and IE was
    // 0x04000009), so a caller asking "can this CPU be woken?" needs the real
    // ones: IME at 0x04000208, IE at 0x04000210, IF at 0x04000214.
    let ime9 = unsafe { md_read_io16(inst.ptr, 9, 0x0400_0208) };
    let ie9 = unsafe { md_read_io16(inst.ptr, 9, 0x0400_0210) };
    let if9 = unsafe { md_read_io16(inst.ptr, 9, 0x0400_0214) };
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
        "aids_set": wifi.aids_set,
        "answered": wifi.answers_sent,
        "canned": wifi.answers_canned,
        "answers_blank": wifi.answers_blank,
        "cmds_rx": wifi.cmds_rx,
        "cmds_rx_dup": wifi.cmds_rx_dup,
        "cmds_tx": wifi.cmds_tx,
        "w_if": format!("{:04X}", wifi.w_if),
        "w_ie": format!("{:04X}", wifi.w_ie),
        "w_pending": format!("{:04X}", wifi.w_if & wifi.w_ie),
        "arm7_ime": format!("{:04X}", unsafe { md_read_io16(inst.ptr, 7, 0x0400_0208) }),
        "arm7_ie7": format!("{ie7_hi:04X}"),
        "arm7_if7": format!("{if7_hi:04X}"),
        "wifi_ie_bit": ie7_hi & IRQ_WIFI_IE7 != 0,
        "wifi_if_bit": if7_hi & IRQ_WIFI_IE7 != 0,
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

    // The two bodies the games are exchanging *as of this sample*: the last
    // command frame this console saw (`cmd_*` - its own command on the host, the
    // command handed to its game on the client) and the last answer a client
    // armed (`answer_*`). The five-second digest prints the same bytes, and five
    // seconds is the wrong resolution for the question v0.1.9aq left open: how
    // long after CUT OFF does the host's game commit to its cut-off state, and
    // does the client follow it before it gives up? That window is 8-97 emulated
    // frames wide, so a caller has to be able to sample as fast as it can talk.
    let body_hex = |b: &[u8; 16], n: u16| -> String {
        let n = (n as usize).min(b.len());
        b[..n].iter().map(|v| format!("{v:02X}")).collect::<Vec<_>>().join(" ")
    };
    let link_json = serde_json::json!({
        "delivered": link.packets_delivered,
        "delivered_bytes": link.bytes_delivered,
        "read_game": link.packets_read_game,
        "read_drain": link.packets_read_drained,
        "received": link.packets_received,
        "sent": link.packets_sent,
        "begin": link.begin_count,
        "end": link.end_count,
        "polls": link.rx_polls,
        "empty_polls": link.rx_empty_polls,
    });
    let mp_json = serde_json::json!({
        "cmd_seq": format!("{:04X}", wifi.last_cmd_seq),
        "cmd_len": wifi.last_cmd_len,
        "cmd_body": body_hex(&wifi.last_cmd_body, wifi.last_cmd_len),
        "answer_len": wifi.last_answer_len,
        "answer_body": body_hex(&wifi.last_answer_body, wifi.last_answer_len),
        "cmd_us": wifi.last_cmd_us,
        "answer_us": wifi.last_answer_us,
        "deauth_out": wifi.deauth_sent,
        "deauth_in": wifi.deauth_recv,
        "deauth_reason": format!("{:04X}", wifi.last_deauth_reason),
        "collected": wifi.replies_collected,
        "empty": wifi.windows_empty,
        "reanchor_dropped": wifi.reanchor_dropped,
        "clientfail": wifi.acks_client_fail,
        "cmds_tx": wifi.cmds_tx,
        "cmds_rx": wifi.cmds_rx,
        /* The rest of the reply queue's own ledger. The per-second log line
           carries these as one digest per health window; a caller reading
           `probe` gets them at *this* instant and next to this console's frame
           counter, which is what a timeline across a deauth needs - "the guest
           answered every round and the host's game was told it failed" is only
           visible when the two sides' counters can be put on one clock. */
        "reply_written": wifi.replies_written,
        "reply_blank": wifi.replies_blank,
        "stale": wifi.stale_dropped,
        "not_looking": wifi.not_looking,
        "acks": wifi.acks_sent,
        "cmds_tx_dup": wifi.cmds_tx_dup,
        "last_ack_clientfail": format!("{:04X}", wifi.last_ack_clientfail),
    });

    // What has actually crossed the ARM9<->ARM7 IPC FIFO since boot. The FIFO
    // levels are empty at rest whether nothing was ever sent or the last word
    // was read a frame ago, so only the counts can say which side has gone
    // quiet - and the frame at which a count stops rising names that side.
    let mut ipc = MDIPCStats::default();
    unsafe { md_get_ipc_stats(inst.ptr, &mut ipc) };
    let ipc_json = serde_json::json!({
        "arm9_sends": ipc.arm9_sends,
        "arm7_sends": ipc.arm7_sends,
        "arm9_recvs": ipc.arm9_recvs,
        "arm7_recvs": ipc.arm7_recvs,
        // Each CPU's own IPCFIFOCNT: bit 15 = the FIFO is enabled, bit 10 = it
        // will interrupt on a received word, bit 8 = nothing left to receive,
        // bit 0 = nothing left to send. 0x8501 is "enabled, armed, both empty".
        "fifocnt9": format!("{:04X}", unsafe { md_read_io16(inst.ptr, 9, 0x0400_0184) }),
        "fifocnt7": format!("{:04X}", unsafe { md_read_io16(inst.ptr, 7, 0x0400_0184) }),
        "sync9": format!("{:04X}", unsafe { md_read_io16(inst.ptr, 9, 0x0400_0180) }),
        "sync7": format!("{:04X}", unsafe { md_read_io16(inst.ptr, 7, 0x0400_0180) }),
    });

    Ok(serde_json::json!({
        "player": player + 1,
        "frames": unsafe { md_frame_counter(inst.ptr) },
        "asleep": unsafe { md_console_asleep(inst.ptr) } != 0,
        "pc9": format!("{pc9:08X}"),
        "pc7": format!("{pc7:08X}"),
        // r0..r15 for each CPU. r13 is the frame of whichever game function is
        // waiting, r14 its innermost return address.
        "regs9": hex_regs(&regs9),
        "regs7": hex_regs(&regs7),
        "sp9": format!("{:08X}", regs9[13]),
        "lr9": format!("{:08X}", regs9[14]),
        "sp7": format!("{:08X}", regs7[13]),
        "lr7": format!("{:08X}", regs7[14]),
        "arm9_can_take_irq": ime9 != 0,
        "arm9_ime": format!("{ime9:04X}"),
        "arm9_ie": format!("{ie9:04X}"),
        "arm9_if": format!("{if9:04X}"),
        "arm9_vblank_pending": (if9 & ie9 & 1) != 0,
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
        "ipc": ipc_json,
        "mp": mp_json,
        "link": link_json,
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

/// Which instruction stores into a watched span of a console's own main RAM.
///
/// The blind spot this closes: a live variable whose writer reaches it through a
/// computed pointer has no literal to search the code for, so reading the code
/// cannot find it and perturbing the data cannot either. Recording the stores
/// themselves answers it directly. `lo`/`hi` are the inclusive window as a hex
/// pair, and `lo > hi` (or `off`) disarms it - arming clears the hits, and the
/// first 64 overlapping stores are kept, because the writer wanted runs first.
/// The JIT writes main RAM on its fast path, so a watch only sees the whole
/// story on the interpreter (start a session with `MD_NOJIT=1`).
fn ctrl_memwatch(
    app: &tauri::AppHandle,
    player: usize,
    lo: Option<u32>,
    hi: Option<u32>,
) -> Result<String, String> {
    let state = app.state::<AppState>();
    let current = state.session.read();
    let session = current.as_ref().ok_or("no session")?;
    let inst = session
        .instances
        .get(player)
        .ok_or_else(|| format!("player {} is not running", player + 1))?;

    if let (Some(lo), Some(hi)) = (lo, hi) {
        if unsafe { md_mem_watch(inst.ptr, lo, hi) } != 0 {
            return Err("md_mem_watch failed".to_string());
        }
    }

    const CAP: usize = 64 * 1024;
    let mut buf = vec![0i8; CAP];
    let n = unsafe { md_mem_watch_report(inst.ptr, buf.as_mut_ptr(), CAP as i32) };
    if n < 0 {
        return Err("the memwatch report did not fit its buffer".to_string());
    }
    let bytes: Vec<u8> = buf[..n as usize].iter().map(|b| *b as u8).collect();
    Ok(String::from_utf8_lossy(&bytes).into_owned())
}

/// The code path one CPU ran over a window, read back in order.
///
/// The store watch names the instruction that wrote a variable; this names the
/// instructions the CPU executed, in the order it first executed them. A
/// downloaded image that boots, does its wireless bring-up and then declines a
/// session has left that decision behind by the time anything is parked, so a
/// snapshot cannot show it - this answers "which code did it run on the way",
/// which is where a give-up branch lives. `cpu` is 9 or 7 (default 9), `lo`/`hi`
/// are hex and arm it, and `off` or a missing window reports without arming. It
/// only sees everything on the interpreter (start a session with MD_NOJIT=1).
fn ctrl_pctrace(
    app: &tauri::AppHandle,
    player: usize,
    cpu: i32,
    arm: Option<(u32, u32)>,
) -> Result<String, String> {
    let state = app.state::<AppState>();
    let current = state.session.read();
    let session = current.as_ref().ok_or("no session")?;
    let inst = session
        .instances
        .get(player)
        .ok_or_else(|| format!("player {} is not running", player + 1))?;

    let cpu_arg = if cpu == 7 { 1 } else { 0 };
    /* Only touch the trace when the caller asked to change it: a report with no
       window must read the path, not disarm the trace it is about to print.
       (Measured: the first version disarmed on every report and printed the
       child's whole post-boot window as "off".) */
    if let Some((lo, hi)) = arm {
        if unsafe { md_pc_trace(inst.ptr, cpu_arg, lo, hi) } != 0 {
            return Err("md_pc_trace failed".to_string());
        }
        // SetPcTrace only publishes a request; the CPU thread applies it at the
        // top of its next RunFrame. Give it a few frames so this one command can
        // arm *and* read back a live trace, instead of reporting the state it
        // just replaced. The control channel runs on its own thread, so this
        // sleep does not hold up emulation.
        std::thread::sleep(std::time::Duration::from_millis(50));
    }

    const CAP: usize = 256 * 1024;
    let mut buf = vec![0i8; CAP];
    let n = unsafe { md_pc_trace_report(inst.ptr, buf.as_mut_ptr(), CAP as i32) };
    if n < 0 {
        return Err("the pc trace report did not fit its buffer".to_string());
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
        // The memory read the roadmap has named since v0.1.9y: a game's own
        // state, in numbers, instead of an inference from its picture. `addr`
        // is hex (with or without 0x) and both it and `len` are multiples of 4,
        // which is what the bridge reads.
        "mem" => {
            let player = match ctrl_json_player(&req) {
                Ok(p) => p,
                Err(e) => return failed(e),
            };
            let addr = match arg_str("addr").as_deref().map(ctrl_parse_hex) {
                Some(Some(v)) => v,
                Some(None) => {
                    return failed(format!(
                        "mem needs a hex 'addr' (e.g. \"02000000\"); got {:?}",
                        arg_str("addr")
                    ))
                }
                None => return failed("mem needs an 'addr' (hex, e.g. \"02000000\")".into()),
            };
            let len = req.get("len").and_then(|v| v.as_u64()).unwrap_or(4) as usize;
            // Which CPU's address space to read. 9 is the default because every
            // caller before 2026-09-29 meant the ARM9's main RAM; 7 is what the
            // ARM7 side needs (WRAM at 0x03800000, the game's ARM7 image at
            // 0x06000000), which an ARM9 read cannot see at all.
            let cpu = match req.get("cpu") {
                None => 9,
                Some(v) => match v.as_i64() {
                    Some(n) if n == 7 || n == 9 => n as i32,
                    other => {
                        return failed(format!(
                            "mem's 'cpu' must be 9 (ARM9, the default) or 7 (ARM7); got {other:?}"
                        ))
                    }
                },
            };
            match ctrl_mem(app, player as usize, addr, len, cpu) {
                Ok(v) => reply(v),
                Err(e) => failed(e),
            }
        }
        // The write half of `mem`: change a console's own variable and watch it
        // react. `words` is a list of hex 32-bit words, written from one address
        // upward; the answer lists them back one per address in the same order,
        // so a read and a poke of the same address print the same way.
        "poke" => {
            let player = match ctrl_json_player(&req) {
                Ok(p) => p,
                Err(e) => return failed(e),
            };
            let addr = match arg_str("addr").as_deref().map(ctrl_parse_hex) {
                Some(Some(v)) => v,
                Some(None) => {
                    return failed(format!(
                        "poke needs a hex 'addr' (e.g. \"021732AC\"); got {:?}",
                        arg_str("addr")
                    ))
                }
                None => return failed("poke needs an 'addr' (hex, e.g. \"021732AC\")".into()),
            };
            let words_arg = arg_str("words").unwrap_or_default();
            let mut words = Vec::new();
            for tok in words_arg.split(|c: char| c == ' ' || c == ',' || c == '_' || c == '\t') {
                if tok.is_empty() {
                    continue;
                }
                match ctrl_parse_hex(tok) {
                    Some(v) => words.push(v),
                    None => {
                        return failed(format!(
                            "poke's 'words' must be hex 32-bit words (e.g. \"0000003E 00000000\"); got '{tok}'"
                        ))
                    }
                }
            }
            if words.is_empty() {
                return failed("poke needs 'words': hex 32-bit words, e.g. \"0000003E\"".into());
            }
            // Which CPU's address space to write, the same shape as `mem`'s
            // `cpu`: 9 is the default (every caller before 2026-09-29 meant
            // the ARM9's main RAM); 7 is the ARM7's own space, which is where
            // a variable that gates an ARM7->ARM9 post lives.
            let cpu = match req.get("cpu") {
                None => 9,
                Some(v) => match v.as_i64() {
                    Some(n) if n == 7 || n == 9 => n as i32,
                    other => {
                        return failed(format!(
                            "poke's 'cpu' must be 9 (ARM9, the default) or 7 (ARM7); got {other:?}"
                        ))
                    }
                },
            };
            match ctrl_poke(app, player as usize, addr, &words, cpu) {
                Ok(v) => reply(v),
                Err(e) => failed(e),
            }
        }
        "wifiio" => match ctrl_json_player(&req) {
            Ok(player) => match ctrl_wifi_io(app, player as usize) {
                Ok(v) => reply(serde_json::json!(v)),
                Err(e) => failed(e),
            },
            Err(e) => failed(e),
        },
        // The store watch: arm it on a span of a console's own main RAM and read
        // back the instructions that wrote into it, newest state first. `lo` and
        // `hi` are hex and both are needed to arm; `off` or no window reports
        // without arming. It is a debug instrument for a variable whose writer
        // is only reachable through a computed pointer.
        "memwatch" => {
            let player = match ctrl_json_player(&req) {
                Ok(p) => p,
                Err(e) => return failed(e),
            };
            let lo_arg = arg_str("lo");
            let hi_arg = arg_str("hi");
            let armed = !matches!(lo_arg.as_deref(), None | Some("off"))
                && !matches!(hi_arg.as_deref(), None | Some("off"));
            let (lo, hi) = if armed {
                match (
                    lo_arg.as_deref().and_then(ctrl_parse_hex),
                    hi_arg.as_deref().and_then(ctrl_parse_hex),
                ) {
                    (Some(lo), Some(hi)) => (Some(lo), Some(hi)),
                    _ => {
                        return failed(format!(
                            "memwatch needs hex 'lo' and 'hi' (e.g. \"023722E0\" \"023722E3\"); \
                             got lo={lo_arg:?} hi={hi_arg:?}"
                        ))
                    }
                }
            } else {
                (None, None)
            };
            match ctrl_memwatch(app, player as usize, lo, hi) {
                Ok(v) => reply(serde_json::json!(v)),
                Err(e) => failed(e),
            }
        }
        // The path trace: arm a first-visit PC recorder on one CPU's window and
        // read back the ordered code path it ran. `cpu` is 9 or 7 (default 9);
        // `lo`/`hi` are hex and arm it; `off` or no window reports without
        // arming. It is the instrument for "which code did the console run", a
        // question a parked snapshot cannot answer.
        "pctrace" => {
            let player = match ctrl_json_player(&req) {
                Ok(p) => p,
                Err(e) => return failed(e),
            };
            let cpu = req.get("cpu").and_then(|v| v.as_i64()).unwrap_or(9) as i32;
            let lo_arg = arg_str("lo");
            let hi_arg = arg_str("hi");
            let off = req.get("off").and_then(|v| v.as_bool()).unwrap_or(false)
                || matches!(lo_arg.as_deref(), Some("off"))
                || matches!(hi_arg.as_deref(), Some("off"));
            // `off` disarms explicitly; a window arms; neither just reports.
            let arm = if off {
                Some((1u32, 0u32))
            } else if !matches!(lo_arg.as_deref(), None | Some("off"))
                && !matches!(hi_arg.as_deref(), None | Some("off"))
            {
                match (
                    lo_arg.as_deref().and_then(ctrl_parse_hex),
                    hi_arg.as_deref().and_then(ctrl_parse_hex),
                ) {
                    (Some(lo), Some(hi)) => Some((lo, hi)),
                    _ => {
                        return failed(format!(
                            "pctrace needs hex 'lo' and 'hi' (e.g. \"02000000\" \"02400000\"); \
                             got lo={lo_arg:?} hi={hi_arg:?}"
                        ))
                    }
                }
            } else {
                None
            };
            match ctrl_pctrace(app, player as usize, cpu, arm) {
                Ok(v) => reply(serde_json::json!(v)),
                Err(e) => failed(e),
            }
        }
        // The synthesized Download Play joiner: its counters, and (with
        // `player`) which console acts as the client. Arming is the Settings
        // switch, not this command - see set_per_game_fix - so this is read-only
        // apart from moving the client console.
        "dpjoiner" => {
            let state = app.state::<AppState>();
            let current = state.session.read();
            match current.as_ref() {
                None => failed("no session running".into()),
                Some(session) => {
                    if let Some(dp) = session.dp.as_ref() {
                        if let Some(p) = req.get("player").and_then(|v| v.as_u64()) {
                            if (p as usize) < session.players {
                                dp.joiner.store(p as usize, Ordering::SeqCst);
                            } else {
                                return failed(format!(
                                    "player {} is not running (1..{})",
                                    p + 1,
                                    session.players
                                ));
                            }
                        }
                    }
                    reply(dp_state(session))
                }
            }
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
            "unknown command '{other}' (ping, status, ui, eval, key, index, click, input, press, touch, untouch, see, shot, text, sleep, quit, probe, mem, poke, wifiio, memwatch, pctrace, vary, dpjoiner)"
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

/// WebKitGTK ships WebRTC compiled in and switched off.
///
/// `WebKitSettings:enable-webrtc` has no positive default (it is FALSE, and
/// turning it on is also what enables media-stream), and wry never touches it -
/// so a Linux Tauri window is a browser with no `RTCPeerConnection` at all.
/// PeerJS reports that exactly as "The current browser does not support
/// WebRTC", which reads like an old browser rather than a setting. Measured on
/// 2026-10-17 against WebKitGTK 2.52.6: `typeof RTCPeerConnection` was
/// `undefined` in a secure context (`tauri://localhost`), and defined after
/// this call.
///
/// WebView2 (Windows) and WKWebView (macOS) expose WebRTC by default and need
/// nothing here, which is why this is the one platform-gated line in the host.
#[cfg(target_os = "linux")]
fn enable_webrtc(window: &tauri::WebviewWindow) {
    use webkit2gtk::{SettingsExt, WebViewExt};
    // The closure runs on the main thread, which is where GTK objects may be
    // touched; `inner()` hands back a clone of the WebKitWebView the window
    // owns, not a borrowed pointer.
    let ok = window
        .with_webview(|webview| {
            match webview.inner().settings() {
                Some(settings) => {
                    settings.set_enable_webrtc(true);
                    log::info!("webview: WebRTC enabled for online play");
                }
                None => log::warn!("webview: WebKitWebView has no settings object"),
            }
        })
        .is_ok();
    if !ok {
        log::warn!("webview: could not reach the webview to enable WebRTC");
    }
}

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
            per_game_state,
            set_per_game_fix,
            get_frames,
            get_audio,
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
            browser_host_start,
            browser_host_open,
            md_ctrl_report,
        ])
        .setup(|app| {
            // Before any page script runs a health check worth believing: the
            // setting has to be in place for `RTCPeerConnection` to exist when
            // online.js looks for it at load. See enable_webrtc.
            #[cfg(target_os = "linux")]
            match app.get_webview_window("main") {
                Some(window) => enable_webrtc(&window),
                None => log::warn!("webview: no 'main' window to enable WebRTC on"),
            }
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

    // The browser host is started on demand (the Online panel's button), but it
    // needs the handle before it can run a single command.
    browser_host::attach(app.handle().clone());

    // Window and exit events are the difference between "the user closed it"
    // and "it died": a clean close leaves a line here, a crash leaves none.
    app.run(|app, event| match event {
        tauri::RunEvent::WindowEvent { label, event, .. } => match event {
            tauri::WindowEvent::CloseRequested { .. } => {
                log::warn!("window '{label}': close requested")
            }
            tauri::WindowEvent::Destroyed => log::warn!("window '{label}': destroyed"),
            _ => {}
        },
        tauri::RunEvent::ExitRequested { .. } => log::warn!("exit requested"),
        tauri::RunEvent::Exit => {
            // Stop the session here instead of leaving it to a `Drop` that this
            // path may never reach. The console threads have to be joined
            // before the process frees the link bus: one still inside
            // `md_run_frame` calls `Platform::MP_SendReply` on a bus that is
            // already gone, which is a call through 0x0 (`bt`: MP_SendReply ->
            // 0x0, from the run-loop thread). Measured on 2026-09-29: three
            // core dumps out of three quits with a *live* group, none out of
            // three with a group whose game had turned its own radio off - the
            // correlation that made it look like a link defect rather than a
            // teardown-order one (history.md v0.1.9aq). `Exit` is the last event
            // on every exit path, so the window's close button and `quit_app`
            // are both covered by this one place.
            let state = app.state::<AppState>();
            *state.session.write() = None;
            log::warn!("event loop exiting - process ending normally");
        }
        _ => {}
    });
}

#[cfg(test)]
mod tests
{
    use super::*;

    /// The keyboard zones are what makes four players at one keyboard
    /// independent, so the property that matters is that no code appears in two
    /// zones: a shared code would drive both consoles at once, and the whole
    /// point of the change is that each player's press reaches their own
    /// console and only it.
    #[test]
    fn default_keymaps_are_disjoint_and_complete()
    {
        let maps: Vec<Vec<(&str, u32)>> = (0..4).map(default_keymap).collect();

        for (p, km) in maps.iter().enumerate()
        {
            assert_eq!(km.len(), 12, "console {} has {} bindings, not 12", p + 1, km.len());
            let mut bits: Vec<u32> = km.iter().map(|(_, b)| *b).collect();
            bits.sort_unstable();
            bits.dedup();
            assert_eq!(bits.len(), 12, "console {} binds the same button twice", p + 1);
        }

        for a in 0..maps.len()
        {
            for b in (a + 1)..maps.len()
            {
                for (code, _) in &maps[a]
                {
                    assert!(
                        !maps[b].iter().any(|(c, _)| c == code),
                        "'{code}' is in console {}'s zone and console {}'s - one key cannot drive two consoles",
                        a + 1,
                        b + 1
                    );
                }
            }
        }
    }

    /// A zone must be a whole DS pad, not a partial one: four players on one
    /// keyboard each need their own d-pad and all eight buttons, or someone
    /// cannot play.
    #[test]
    fn every_zone_has_the_whole_pad()
    {
        let want = [
            MD_BTN_A,
            MD_BTN_B,
            MD_BTN_X,
            MD_BTN_Y,
            MD_BTN_L,
            MD_BTN_R,
            MD_BTN_START,
            MD_BTN_SELECT,
            MD_BTN_UP,
            MD_BTN_DOWN,
            MD_BTN_LEFT,
            MD_BTN_RIGHT,
        ];
        for p in 0..4
        {
            let km = default_keymap(p);
            for bit in want
            {
                assert!(
                    km.iter().any(|(_, b)| *b == bit),
                    "console {} has no binding for button bit {bit:#x}",
                    p + 1
                );
            }
        }
    }
}

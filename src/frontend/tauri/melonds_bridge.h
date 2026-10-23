/*
    melonDS Multiplayer - native bridge C API

    This is the only surface the Tauri (Rust) host talks to. Everything here is
    plain C so it can be consumed through FFI without a binding generator.

    Threading contract: each instance is expected to be driven by exactly one
    thread at a time. Different instances may run concurrently on different
    threads (that is the whole point - one emulator per player), and the
    instances are linked together through the local multiplayer bus so that
    in-game wireless play works between them.
*/

#ifndef MELONDS_MULTIPLAYER_BRIDGE_H
#define MELONDS_MULTIPLAYER_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MD_SCREEN_WIDTH   256
#define MD_SCREEN_HEIGHT  192
#define MD_SCREEN_PIXELS  (MD_SCREEN_WIDTH * MD_SCREEN_HEIGHT)
#define MD_MAX_INSTANCES  16

/* Button bit layout, matches the emulated DS keypad register. */
#define MD_BTN_A      (1u << 0)
#define MD_BTN_B      (1u << 1)
#define MD_BTN_SELECT (1u << 2)
#define MD_BTN_START  (1u << 3)
#define MD_BTN_RIGHT  (1u << 4)
#define MD_BTN_LEFT   (1u << 5)
#define MD_BTN_UP     (1u << 6)
#define MD_BTN_DOWN   (1u << 7)
#define MD_BTN_R      (1u << 8)
#define MD_BTN_L      (1u << 9)
#define MD_BTN_X      (1u << 10)
#define MD_BTN_Y      (1u << 11)

/* Stop reasons reported by md_stop_reason(). */
enum
{
    MD_STOP_NONE = 0,
    MD_STOP_EXTERNAL = 11,          /* md_reset()/md_link_end() style stop */
    MD_STOP_GBAMODE_NOT_SUPPORTED = 12,
    MD_STOP_BAD_EXCEPTION_REGION = 13,
    MD_STOP_POWER_OFF = 14,
    MD_STOP_UNKNOWN = 15,
};

typedef struct MDInstance MDInstance;

/* ---------------------------------------------------------------- lifecycle */

/* console_type: 0 = NDS, 1 = DSi. Returns NULL on failure. */
MDInstance* md_create(int console_type, const char* name);
void md_destroy(MDInstance* inst);

/* Loads a .nds file. Battery RAM is picked up from "<rom>.sav" when present.
   Returns 0 on success, non-zero on failure (see md_last_error). */
int md_load_rom(MDInstance* inst, const char* rom_path);
void md_eject_rom(MDInstance* inst);

/* Optional BIOS/firmware overrides. Pass NULL to keep the built-in FreeBIOS. */
int md_set_bios(MDInstance* inst, const char* arm9_path, const char* arm7_path);
int md_set_firmware(MDInstance* inst, const char* firmware_path);

void md_reset(MDInstance* inst);

/* Enables or disables the ARM JIT at runtime. The JIT is on by default because
   four consoles on one machine need it, but a title that misbehaves under it can
   be checked from the same host by turning it off - melonDS's own frontend
   exposes the same toggle. */
int md_set_jit(MDInstance* inst, int enabled);
/* Non-zero while the JIT is active. */
int md_jit_enabled(MDInstance* inst);

/* Writes into the emulated console's main RAM. addr and len must both be
   multiples of 4. Used to stamp the diagnostic identity block (see
   tools/mkdiagrom) before the first frame. */
int md_write_ram(MDInstance* inst, uint32_t addr, const void* data, size_t len);

/* The same write on either CPU's own address space (cpu 9 = ARM9, 7 = ARM7),
   the write half of md_read_ram_cpu. A diagnostic poke into ARM7 state - a
   variable that gates an ARM7->ARM9 post - is the case this exists for; an
   ARM9 write cannot reach the ARM7's WRAM or the game's ARM7 image. */
int md_write_ram_cpu(MDInstance* inst, int cpu, uint32_t addr, const void* data, size_t len);

/* Reads back main RAM (addr, len must be multiples of 4). Handy for diagnostics
   and for tests that watch a ROM's progress markers. */
int md_read_ram(MDInstance* inst, uint32_t addr, void* out, size_t len);

/* The same read on either CPU's own address space (cpu 9 = ARM9, 7 = ARM7).
   The two spaces do not overlap: the ARM7's WRAM and the game's own ARM7 image
   at 0x06000000 are invisible to an ARM9 read, so a question about what the
   ARM7 side is doing needs this rather than md_read_ram. */
int md_read_ram_cpu(MDInstance* inst, int cpu, uint32_t addr, void* out, size_t len);

/* IPC FIFO traffic between this console's two CPUs, counted since boot at the
   instant a word moves: pushes at each side's IPCFIFOSEND write, pops at each
   side's IPCFIFORECV read.

   This exists because the levels cannot answer the question. MKDS's ARM9s park
   in a blocking message dequeue while both FIFOs read empty and enabled
   (IPCFIFOCNT 0x8501), and an empty FIFO looks identical whether the channel
   has never carried a word or carried its last one a frame ago. Counts tell
   those apart, and the frame at which each side's count stops rising is exactly
   "which side went quiet". */
typedef struct MDIPCStats
{
    uint64_t arm9_sends;   /* words the ARM9 pushed into the FIFO */
    uint64_t arm7_sends;   /* words the ARM7 pushed into the FIFO */
    uint64_t arm9_recvs;   /* words the ARM9 popped out of the FIFO */
    uint64_t arm7_recvs;   /* words the ARM7 popped out of the FIFO */
} MDIPCStats;

/* Fills in out with this console's IPC FIFO counters. Returns 0 on success. */
int md_get_ipc_stats(MDInstance* inst, MDIPCStats* out);

/* One CPU's whole register file (r0..r15, r15 = the program counter) into a
   caller-provided array of 16 words. Returns 0 on success.

   This exists because a program counter names a *loop*, not a *waiter*: MKDS's
   two ARM9s sit at one address inside the SDK's retry dequeue, which is the code
   the game is in, not the game function that is waiting. r13 says where that
   function's frame is (so the return addresses on it can be read back with
   md_read_ram_cpu), and r14 is its innermost return address. */
int md_get_regs(MDInstance* inst, int cpu, uint32_t* out16);

/* What this console's own software has been reading out of the wireless block,
   as text: every wifi register halfword it has taken (a count and the last
   value per offset, plus the last 512 reads in order), and every halfword it
   consumed out of the frame it was most recently handed, in that order.

   This exists because a value the emulator fabricates and a value the peer's
   game wrote are indistinguishable in any counter taken from the write side or
   from the wire, and separable only where a game's own bytecode looks at them
   (history.md v0.1.9h; the recorder is Wifi::MPIo in src/Wifi.h). Writes at
   most `cap` bytes including the terminator and returns the length written,
   or -1 if `cap` is too small for even the header. */
int md_wifi_io_report(MDInstance* inst, char* out, int cap);

/* Debug: arm a main-RAM store watch (see NDS::SetMemWatch) and read it back.
   `lo`/`hi` are the inclusive window (0x02000000..0x023FFFFF); `lo > hi`
   disarms. Arming clears the hits. `md_mem_watch_report` writes the hits as
   text and returns the length, or -1 when the buffer is too small. It answers
   "which instruction wrote this variable", which is otherwise unanswerable when
   the writer reaches it through a computed pointer. Only the interpreter routes
   every store through it, so a watch is meaningful with the JIT off. */
int md_mem_watch(MDInstance* inst, uint32_t lo, uint32_t hi);
int md_mem_watch_report(MDInstance* inst, char* out, int cap);

/* Debug: trace one CPU's own code path over a window `[lo, hi)` (see
   NDS::SetPcTrace). `cpu` is 0 for the ARM9 and 1 for the ARM7; `lo >= hi`
   disarms. Arming clears the record and keeps the last N first-visit PCs with
   the frame each landed on, which names the code a console executed on the way
   to a parked state - the decision a snapshot cannot show. Only the
   interpreter runs the hook, so a trace is meaningful with the JIT off. */
int md_pc_trace(MDInstance* inst, int cpu, uint32_t lo, uint32_t hi);
int md_pc_trace_report(MDInstance* inst, char* out, int cap);

/* The parked-handshake probe (see Wifi::SetMPProbe): kind 1 replaces the body
   bytes the console's game reads with `body` for `value` deliveries, kind 0
   clears. Returns 0 on success and writes the probe state back out so a caller
   can watch it fire: `armed` is the kind actually armed (0 when there was
   nothing to run), `left` how many deliveries it still has to touch and
   `applied` how many it has changed. Diagnostics only. */
int md_link_mp_vary(MDInstance* inst, int kind, int value, const uint8_t* body,
                    int bodylen, int* armed, int* left, uint64_t* applied);

/* -------------------------------------------------------------------- input */

void md_set_buttons(MDInstance* inst, uint32_t mask);
void md_touch(MDInstance* inst, int x, int y);
void md_release_touch(MDInstance* inst);
void md_set_lid(MDInstance* inst, int closed);

/* What the emulated console can actually observe about a touch.

   A game never sees "a touch happened at (x,y)"; it sees two ADC readings out
   of the touchscreen controller and a pen-down bit in its key register. Those
   are the whole interface, so a test that wants to prove input reached the
   guest has to read them back rather than infer it from pixels. */
typedef struct MDTouchProbe
{
    uint32_t pen_down;  /* 1 while the key register reports the pen against the screen */
    uint32_t adc_x;     /* the touchscreen's X channel, as the guest reads it */
    uint32_t adc_y;
} MDTouchProbe;

/* Touches (or releases) the screen and reports back what the guest would read.
   Values are the raw 12-bit conversions, which melonDS drives as screen pixel
   times 16 - see melonDS::TSC::SetTouchCoords. */
int md_touch_probe(MDInstance* inst, int touching, int x, int y, MDTouchProbe* out);

/* ---------------------------------------------------------------- execution */

/* Runs one DS frame (both screens). Returns the number of scanlines executed. */
uint32_t md_run_frame(MDInstance* inst);

/* Borrowed pointers to the current top/bottom framebuffers, 256x192 each,
   stored as 0xAARRGGBB in native byte order (BGRA in memory), exactly as the
   software renderer produces them. Valid until the next md_run_frame(). */
int md_get_screens(MDInstance* inst, const uint32_t** top, const uint32_t** bottom);

/* Drain this console's audio as interleaved stereo s16. `max_frames` counts
   stereo frames (4 bytes each); the return value is the number of s16 values
   written (frames * 2), 0 when nothing is buffered, -1 on a bad instance.
   `out_rate` receives the mixer's sample rate (the core's 48 kHz default),
   which a sink has to resample to its own device rate. */
int md_read_audio(MDInstance* inst, int16_t* out, int max_frames, int* out_rate);

/* -------------------------------------------------------------------- state */

/* Size of the scratch buffer md_save_state() needs. */
size_t md_max_state_size(void);

/* Serializes the whole console into out. Returns bytes written, 0 on error. */
size_t md_save_state(MDInstance* inst, uint8_t* out, size_t cap);
/* Restores a state produced by md_save_state(). Returns 0 on success. */
int md_load_state(MDInstance* inst, const uint8_t* data, size_t len);

/* Battery-backed save RAM (borrowed, may be NULL for homebrew with no save). */
const uint8_t* md_get_save(MDInstance* inst, size_t* len);
int md_flush_save(MDInstance* inst, const char* path);

/* --------------------------------------------------------------- link cable */

/* Enables the local multiplayer bus and resets all slot assignments. */
void md_link_init(void);
void md_link_shutdown(void);
/* Assigns/releases a slot (0-15) on the shared link bus. All instances taking
   part in a multiplayer game must be assigned slots before their first frame.
   Slots are the identity the emulated wireless hardware uses, so two consoles
   sharing a slot cannot see each other. */
void md_link_attach(MDInstance* inst, int slot);
void md_link_detach(MDInstance* inst);
int  md_link_slot(MDInstance* inst);

/* Slots whose console has actually switched its wireless hardware on (bit N =
   slot N). Attaching a slot is not the same as the game starting wireless: a
   console sits here only while its emulated WiFi is powered, which is what a
   lobby UI wants to show. */
uint32_t md_link_connected_mask(void);

/* How long a console waits for a partner's reply before giving up, in
   milliseconds. Every console in a session should use the same value, or
   results become timing dependent. */
void md_link_set_recv_timeout(int milliseconds);
int  md_link_recv_timeout(void);

/* Host-side reply timeout, separate from the above. A host advances once per
   command slot and its clients' replies race it, so giving up instantly makes it
   retry handshakes it could have finished. 1-4ms is enough when the consoles run
   on their own threads; 0 makes it a pure poll. */
void md_link_set_reply_timeout(int milliseconds);
int  md_link_reply_timeout(void);

/* How long, in total, this console may spend inside reply waits during the
   frame it is about to run (negative = no limit, stock melonDS). The host
   publishes this immediately before each frame: the per-window timeout above is
   wall-clock time spent inside an emulated frame, and an unbounded wait there
   drags the whole lockstep session down with it - 363 empty windows at 34ms
   cost 12.3s of a 15s window during a real MKDS join, which ran the session at
   9fps. Budgeted, a console waits only as long as the frame was going to be
   idle anyway; a reply the peer cannot produce inside that slack is a reply
   this frame does not get, and the game hears about it the usual way
   (clientfail in the host's ack). Reach for MD_REPLY_TIMEOUT_MS, not this, when
   tuning how generous a single window is. */
void md_link_set_reply_budget(MDInstance* inst, int64_t budget_ns);

/* How many times this console has powered its wireless hardware on and off.
   Tells "the game never started wireless" apart from "wireless ran and found
   nobody", which are very different faults to chase. */
uint32_t md_link_begin_count(MDInstance* inst);
uint32_t md_link_end_count(MDInstance* inst);

/* Everything a host (or a test) needs to tell whether a link is actually
   carrying traffic. A game that has powered its wireless on and is exchanging
   nothing is a very different situation from one that never started wireless.
   v2 splits the packet counters by LocalMP packet kind so a stall can be
   pinned to a layer: a game stuck after discovery shows broadcasts flowing
   but no cmds; a host whose replies never arrive shows tx_cmd with rx_reply
   frozen, etc. */
typedef struct MDLinkStats
{
    uint32_t connected;        /* slots on the bus right now, as a bitmask */
    uint32_t begin_count;      /* times this console powered wireless on */
    uint32_t end_count;        /* times it powered wireless off */
    uint64_t packets_sent;     /* wireless frames this console transmitted */
    uint64_t packets_received; /* frames a consumer read out of its inbox */
    uint64_t bytes_sent;
    /* Only counts client-side and broadcast frames. A host collects its
       clients' replies through LocalMP::RecvReplies, which returns a bitmask of
       who replied and leaves the payload in the caller's buffer, so the platform
       layer never learns how many bytes arrived that way. Read this as a lower
       bound; use the sender's bytes_sent for the true figure. */
    uint64_t bytes_received;
    /* --- v3: the receive total, attributed ---

       `packets_received` is a sum over *consumers*, and before v3 nobody could
       say which consumer: the game reading its own receive slot and a scripted
       drain (md_link_recv_packet) incremented the same counter. On 2026-09-30
       that ambiguity let an app Link test read a healthy bus as FAILED - its
       drains collected nothing because the consoles' own games had already
       consumed the frames, and the growth it should have trusted was theirs.
       Now: packets_read_game / packets_read_drained split that total by who
       read the frame, and packets_delivered is the *bus's* own ledger (counted
       by LocalMP::Route at the hand-over, before any consumer can touch it).
       A frame can therefore be: delivered and read by the game, delivered and
       read by a drain, or delivered and never read - three different
       situations one number used to hide. */
    uint64_t packets_delivered;      /* frames the router handed this console */
    uint64_t bytes_delivered;
    uint64_t packets_read_game;      /* frames its own game read out of them */
    uint64_t bytes_read_game;
    uint64_t packets_read_drained;   /* frames a scripted drain read out of them */
    uint64_t bytes_read_drained;
    /* --- v2: per-kind split (LocalMP packet types 0-3) --- */
    uint64_t rx_broadcast;     /* type 0: MP frames pulled off the packet FIFO */
    uint64_t rx_cmd;           /* type 1: host command frames */
    uint64_t rx_reply;         /* type 2: client reply frames */
    uint64_t rx_ack;           /* type 3: ack frames */
    uint64_t rx_polls;         /* all MP host-frame + broadcast receive calls */
    uint64_t tx_cmd;           /* host commands this console sent */
    uint64_t tx_reply;         /* replies this console sent */
    uint64_t tx_ack;           /* acks this console sent */
    uint64_t rx_empty_polls;   /* polls that returned nothing */
    /* Steady-clock microseconds of the last accepted-from-a-peer packet, and
       of the bus epoch (md_link_init), so hostside code can compute seconds
       since the last traffic and log relative timestamps consistently. */
    uint64_t last_rx_us;
    uint64_t epoch_us;
} MDLinkStats;

/* Attach (on!=0) or detach this console to the link bus for a Link test,
   touching no emulated hardware and no connected-mask bit.

   The button used to power each console's wireless on - through the same ARM7
   registers the game's own WiFi driver uses - to get the console onto the bus,
   and power it off again afterwards. With a game running that is a hair
   trigger: MPH sitting on its menu with its radio off *is* a game running, and
   after one such test the two consoles never raised their radios again (three
   walk attempts over three minutes, against 17 seconds for the same walk with
   no test in front of it, measured 2026-09-30). Verifying the *bus* needs the
   bus, not the radio: a frame is routed to a console whose bus attachment is
   set, which is exactly what this sets. The mask the health line prints and
   the selftest's own gate reads is deliberately untouched - it reports what
   the games' radios are doing, and a test must not appear in it. Returns 0 on
   success, -1 when there is no bus or the console has no slot. */
int md_link_test_attach(MDInstance* inst, int on);

/* Fills in out with this console's link state. Returns 0 on success. */
int md_link_stats(MDInstance* inst, MDLinkStats* out);

/* The struct's field order, proven rather than hoped: writes the struct into
   `out` with every field set to a distinct number (its position in this
   declaration, starting at 0) and returns its size, or 0 when `out` is too
   small. A reader in another language checks each of its own fields against
   the number this side put in *that name*: a field moved, added or dropped
   then shows up as a named mismatch instead of as a number that quietly means
   something else. Hand-mirroring this struct in Rust has already produced
   exactly that once (2026-09-30, v0.1.9bo: the Link test read the drain's
   count as the bus's delivery ledger and reported a healthy bus broken). */
uint64_t md_link_stats_canary(uint8_t* out, size_t cap);

/* ---- one-shot Link-test probe --------------------------------------------

   The receive half of a Link test used to be "send a tagged frame, then drain
   the peer's inbox", which is a race the peer's own game wins: it polls from
   inside the emulation every frame, while a drain only runs between frames. So
   a test could send a frame that the bus delivered perfectly, watch the drain
   come up empty, and report the link broken (measured 2026-09-30 in two
   states). Arm the expectation on the *receiver* instead, send, then ask what
   happened: `packets_delivered` says the bus handed it over, and the probe says
   whether some consumer read those exact bytes back. */
typedef struct MDProbeResult
{
    uint32_t armed;        /* the expectation is (still) in place */
    uint32_t observed;     /* a consumer read a frame carrying this tag */
    uint32_t payload_ok;   /* the bytes are exactly the ones that were armed */
    uint32_t ts_ok;        /* ... and the timestamp the sender queued it with */
    char     seen_by[12];  /* "game" or "drain" (empty when unobserved) */
} MDProbeResult;

/* Arms (or clears) this console's expectation: `expect` is the frame body the
   sender will queue, `ts` the timestamp it will be queued with. The wire's own
   trailing-four-byte FCS is applied here (LocalMP::ApplyWireFCS), so the
   expectation is the frame a consumer on this console would read - the same
   transform the send path applies - rather than the bytes as handed over. */
int md_probe_arm(MDInstance* inst, const void* expect, size_t len, uint64_t ts);
void md_probe_disarm(MDInstance* inst);
/* Reports the expectation's state. Returns 0 on success. */
int md_probe_result(MDInstance* inst, MDProbeResult* out);

/* Everything the multiplayer flight recorder knows about one console
   (Wifi::MPDiag + LocalMP::MPReplyDiag), in one struct.

   This exists because the previous thirteen live attempts were each read back
   by hand from a multi-megabyte log: the questions that matter (did the guest's
   game receive a command, did it answer with its own bytes, did the host read
   that answer back, and what did either body say) are all small integers, and
   the host prints them as one line per console per health window. A failed join
   should therefore be diagnosable from the last two lines of the log instead of
   a forensic pass over the whole file. Counters are always on; bodies are the
   last ones seen, truncated to 16 bytes. */
typedef struct MDMPDigest
{
    /* 0 = not in multiplayer, 1 = MP host, 2 = MP client */
    uint32_t role;

    /* guest side: what the console's own game did */
    uint64_t cmds_rx;          /* command frames delivered to its game */
    uint64_t cmds_rx_dup;      /* ... of which repeated the previous seqno */
    uint64_t answers_sent;     /* answers its game produced */
    uint64_t answers_blank;    /* zero length "nothing armed" frames */
    uint64_t answers_canned;   /* firmware stand-ins (fc=0158) */
    uint64_t arms;             /* reply slots it armed (bit 15 of W_TXSlotReply1) */
    uint64_t aids_set;         /* W_AIDLow writes that changed the value */
    uint64_t window_reads;     /* reads of its wireless receive RAM window */
    uint64_t deauth_sent;      /* deauth frames it transmitted */
    uint64_t deauth_recv;      /* deauth frames it accepted */

    /* host side */
    uint64_t cmds_tx;          /* command frames it transmitted */
    uint64_t cmds_tx_dup;      /* ... of which repeated the previous seqno */
    uint64_t acks_sent;        /* acks it transmitted */
    uint64_t acks_client_fail; /* ... of which reported a failed client */

    /* the shared reply queue, from this console's point of view */
    uint64_t replies_written;  /* answers its own game handed to the bus */
    uint64_t replies_collected;/* answers it read back with a payload */
    uint64_t replies_blank;    /* zero length credits it read back */
    uint64_t windows_empty;    /* collection attempts that came back with nothing */
    uint64_t stale_dropped;    /* answers skipped for being too old */
    uint64_t reanchor_dropped; /* unread answers discarded by a new command */
    uint64_t not_looking;      /* windows that never ran: no peer attached */
    uint64_t windows_extended; /* windows that outlived the ceiling for an owed client's round */
    uint64_t extension_frames; /* emulated frames of extra wait those windows spent */
    uint64_t closed_owed;      /* windows that closed with answers owed after their clients ran a round */

    /* the last frames seen, bodies included */
    uint16_t last_cmd_seq;
    uint16_t last_cmd_len;     /* body bytes after the 24-byte 802.11 header */
    uint8_t  last_cmd_body[16];
    uint16_t last_answer_len;
    uint8_t  last_answer_body[16];
    uint16_t last_deauth_reason;
    uint16_t last_ack_clientfail;
    uint64_t last_cmd_us;
    uint64_t last_answer_us;

    /* interrupt level (2026-09-28): a picture that never changes while the CPUs
       keep running is what a game waiting on an interrupt it never receives
       looks like, and the byte counters cannot tell that apart from a game
       that is simply idle. `irq_raised[i]` is every time the wifi block asked
       its CPU for attention on source i, `irq_acked[i]` every time the game
       wrote a 1 to clear that bit. The rest is the live register state at the
       instant the digest was taken, refreshed by SampleMPIrqSnapshot(). */
    uint64_t irq_raised[16];         /* SetIRQ/IFSet writes per source */
    uint64_t irq_acked[16];          /* W_IF writes that cleared that bit */
    uint64_t irq_cpu_line;           /* wifi asserting ARM7 IRQ_Wifi (bit 23) */
    int32_t  block_beacon_irq14;     /* W_USCompare0 bit0 bookkeeping */
    uint16_t w_if;                   /* W_IF: pending bits */
    uint16_t w_ie;                   /* W_IE: enabled bits */
    uint16_t w_uscntcnt;             /* W_USCountCnt: us-counter gate */
    uint16_t w_uscmpcnt;             /* W_USCompareCnt: bit0 allows IRQ14 */
    uint16_t w_bcncount1;            /* counts down to IRQ14 (source 1) */
    uint16_t w_bcncount2;            /* counts down to IRQ13 (power-down) */
    uint16_t w_txslot_beacon;
    uint16_t w_txslot_reply1;
    uint16_t w_txslot_reply2;
    uint16_t w_txbusy;
    uint16_t w_txreqread;
    uint16_t w_powerstate;
    uint16_t w_powertx;
    uint16_t comstatus;
    uint64_t uscounter;              /* the game-visible us counter */
    uint64_t uscompare;              /* what it is waiting for */
} MDMPDigest;

/* Fills in out with this console's multiplayer digest. Returns 0 on success. */
int md_link_mp_digest(MDInstance* inst, MDMPDigest* out);

/* sizeof(MDMPDigest), so the host can check its own struct against this one at
   startup. The host reads the digest into a struct it declares by hand, and this
   project has already lost a session to an FFI declaration mismatch (an `int`
   read as `usize` in md_link_recv_packet, which made every "did my packet
   arrive" comparison a coin toss). A mismatch here would be quieter: the numbers
   would simply be wrong, so it is worth one line at startup to prove the two
   layouts agree. */
uint64_t md_link_mp_digest_size(void);

/* Re-anchor this console onto the bus: LocalMP::Begin resets its packet-FIFO
   read offset to the bus's write head and clears its receive semaphores. The
   host calls this when the watchdog detects a stalled console whose peers are
   still on the bus - the automated form of "clicking Link test unfroze it"
   (the selftest's wireless power cycle lands here via the core's MP_Begin).
   Harmless on a healthy console: it only discards packets queued for a
   console that demonstrably stopped reading them. */
void md_link_resync(MDInstance* inst);

/* Set the core-log tag used by the CURRENT thread: all melonDS log lines
   emitted from this thread will be prefixed [melonDS:<tag>]. The host calls
   this at the top of each console's run thread so interleaved per-console
   logs can be told apart. */
void md_tag_current_thread(const char* tag);

/* ------------------------- link self-test ------------------------------- */
/* Broadcasts a packet on this console's slot, and pulls the next packet
   addressed to it. These are the same calls the emulated wireless hardware
   makes through the platform layer. They exist so tests can exercise slot
   routing without a full DS wireless stack in the diagnostic ROM.

   Delivery is broadcast: every other connected slot gets a copy, and a console
   never receives its own packet. md_link_recv_packet does not block; it returns
   0 when nothing is queued. */
int md_link_send_packet(MDInstance* inst, const void* data, size_t len, uint64_t timestamp);
int md_link_recv_packet(MDInstance* inst, void* out, size_t cap, uint64_t* timestamp);

/* Same as md_link_recv_packet, plus the entry's own routing fields. A reader
   that hands frames back with md_link_requeue_packet needs the header the bus
   filed them under: the body alone does not carry the sender, and rebuilding
   it from the 802.11 addresses would misattribute relayed frames. `sender` and
   `type` are the MPPacketHeader SenderID/Type (0=regular 1=CMD 2=reply 3=ack);
   either out-pointer may be NULL. Same buffer contract as md_link_recv_packet
   (a frame-size buffer; the bus writes whole or nothing). Returns the body
   length, 0 when empty. */
int md_link_recv_packet_full(MDInstance* inst, void* out, size_t cap, uint64_t* timestamp,
                             uint32_t* sender, uint32_t* type);

/* Return a frame this console read to the tail of its own inbox, under its
   original header (see LocalMP::RequeuePacket). For a reader that must see
   some classes but leave others for the console's own game: the booted
   Download Play joiner answers the host's commands as the firmware stand-in
   while leaving the beacons its game scans for, because consuming them starves
   the scan - every frame the drain reads is one the game's CheckRX never sees
   (both read the same per-slot queue). The frame is already on-wire format and
   is appended verbatim. Returns `len` on success, 0 when refused (bad slot,
   detached console, empty or oversize frame). */
int md_link_requeue_packet(MDInstance* inst, uint32_t sender, uint32_t type,
                           const void* data, size_t len, uint64_t timestamp);

/* Send an *answer* rather than a broadcast: the bus files it in the answer
   queue of the console this one is following, keyed by `aid`, which is exactly
   where a host's reply window (LocalMP::RecvReplies, reached through
   Platform::MP_RecvReplies) looks for a client's reply. `md_link_send_packet`
   cannot reach that queue - it goes out on the broadcast receive path - which
   is why a synthesized client that answered with it was never collected and
   the host's ack reported it as failed every round. `aid` is the AID the host
   assigned in its association response, low nibble (1..15); 0 is refused by
   the receiver as unusable (it would file 1KB before the buffer). */
int md_link_send_reply(MDInstance* inst, const void* data, size_t len, uint64_t timestamp, uint16_t aid);

/* Boot a single-card Download Play child image on this console: write its
   header/ARM9/ARM7 segments to the destinations the host's RSA frame stated,
   set the RAM boot indicator to the DS-Download-Play value, tune the radio to
   `channel`, and point both CPUs at the entrypoints. This is the "jump to
   entrypoints" half of GBATEK's post-transfer step; the caller (the joiner)
   deliberately sends no reason-3 deauthentication (see the handoff's own
   comment). `image` is the streamed packet payloads concatenated in packet
   order, so the segments are `header_size`, then `arm9_size`, then
   `arm7_size`, back to back.

   `session` is what the client negotiated with the host while it was
   downloading, and it is carried into the boot on purpose: a real client comes
   out of a single-card transfer still tuned to the host's channel, still
   associated with the host's BSSID, and still holding the AID the host granted
   it, because all three live in the wireless block and the jump into the
   downloaded image does not undo them. This emulator's boot is a whole-console
   reset, so without them the child comes up as a stranger to the host it was
   just talking to - the shape measured in logs/v9dpsession_finding.txt.

   Returns 0 on success, -1 when the image does not fit the stated load map or
   there is no session. */
typedef struct MDDPSession {
    uint32_t channel;        /* the host's own channel, off its beacon (0 = untuned) */
    uint8_t  host_bssid[6];  /* the host's own BSSID: the station associated with */
    uint32_t aid;            /* the AID the host granted this console (0 = none) */
    uint32_t host_serial;    /* the beacon's game serial (0040xxxx), little-endian */
    uint32_t host_token;     /* the two stream bytes after the serial, low halfword */
    uint32_t host_capability;/* beacon capability, verbatim */
    uint32_t host_interval;  /* beacon interval, verbatim */
    uint32_t host_rate_mask; /* 1/2-Mbit rate bitmask off the beacon's rate bytes */
    uint32_t host_beacon_size;/* handoff beacon-size field, computed off the DD tag */
} MDDPSession;

int md_dp_boot(MDInstance* inst, const void* image, size_t len,
               uint32_t header_dest, uint32_t header_size,
               uint32_t arm9_dest, uint32_t arm9_size, uint32_t arm9_entry,
               uint32_t arm7_dest, uint32_t arm7_temp, uint32_t arm7_size,
               uint32_t arm7_entry, const MDDPSession* session);

/* This console's own wireless MAC (6 bytes), as the bridge assigned it at ROM
   load (see apply_console_mac). The synthesized single-card Download Play
   joiner addresses its management frames from this identity, so the host's
   answer comes back to a console that exists on the bus, and the joiner's own
   receive path recognises the reply as addressed to it. Returns -1 when no
   console has booted yet. */
int md_console_mac(MDInstance* inst, unsigned char* out6);

/* The name this console advertises on the air ("Player 1".."Player 4"): the
   firmware nickname every instance boots with, which is what a game's own room
   list shows for each console it can see. Read back out of the firmware the
   guest reads, so a reader sees the same bytes a game sees. `out` is NUL-
   terminated UTF-8; returns the byte count written, or -1 before a ROM load
   (no identity yet). */
int md_console_name(MDInstance* inst, char* out, int cap);

/* Whether this console is on the bus right now (1/0; -1 when there is no bus).
   A console whose radio is off is not attached and the router writes it
   nothing, so a reader that attaches at the bus level (md_link_test_attach)
   has to be able to notice that a game's own wireless power cycle detached it
   again. */
int md_link_attached(MDInstance* inst);

/* -------------------------------------------------------------- diagnostics */

const char* md_last_error(MDInstance* inst);
/* ROM internal title (12 chars max) or "" when no cart is inserted. */
int md_rom_title(MDInstance* inst, char* out, size_t cap);
/* ROM game code: the four ASCII bytes at header offset 0x0C ("AMCE" for Mario
   Kart DS, "A2DE" for New Super Mario Bros., ...), the cartridge's own
   identity. Returns -1 and an empty string when no cart is inserted. This is
   what the host resolves a per-game profile from; it is deliberately a plain
   code and not a decision, so the registry and its policy stay in one place
   (src-tauri/src/game_profile.rs). */
int md_rom_game_code(MDInstance* inst, char* out, size_t cap);
/* Zero-based index of the frame currently being displayed. */
uint32_t md_frame_counter(MDInstance* inst);
/* Non-zero once the emulated console asked to stop (crash/power off). */
int md_stop_reason(MDInstance* inst);

/* Reads a 32-bit word out of the GPU address space (VRAM windows, palettes,
   OAM). Useful for diagnostics and tests that need to see what the emulated
   console actually stored. */
uint32_t md_read_gpu(MDInstance* inst, uint32_t addr);
/* Same, for the many 16-bit display registers (BGxPA, BGxCNT, ...). */
uint16_t md_read_gpu16(MDInstance* inst, uint32_t addr);

/* Reads a 16-bit register from either CPU's I/O space (cpu is 9 or 7). The two
   cores do not see the same register map - X/Y and the ARM7's peripherals live
   in registers the ARM9 does not decode at all - so testing keypad handling
   means being able to ask each core separately. */
uint16_t md_read_io16(MDInstance* inst, int cpu, uint32_t addr);

/* Writes into a CPU's address space (cpu is 9 or 7, value is 32-bit). Unlike
   md_write_ram this is not limited to RAM, so a host or a test can poke I/O
   registers. That matters for the wireless hardware, which is ARM7-only and
   therefore unreachable through the ARM9's view of the machine. */
int md_write_io32(MDInstance* inst, int cpu, uint32_t addr, uint32_t value);
/* The same for a halfword, since most wireless registers are 16-bit. */
int md_write_io16(MDInstance* inst, int cpu, uint32_t addr, uint16_t value);

/* Program counter of the ARM9 (cpu 9) or ARM7 (cpu 7) core. */
uint32_t md_get_pc(MDInstance* inst, int cpu);
/* Non-zero while the emulated console is asleep (nothing will advance). */
int md_console_asleep(MDInstance* inst);

#ifdef __cplusplus
}
#endif

#endif /* MELONDS_MULTIPLAYER_BRIDGE_H */

# Game Compatibility — melonDS Multiplayer

Which Nintendo DS titles hold up **linked, in the app, with more than one console
running at once**, and how far each one actually got. Every entry below was run
on the local `Test Roms/` folder on this tree; nothing here is extrapolated from
single-console play.

> **What "works" means in this file.** A link that is *alive* (frames crossing
> the virtual Wi-Fi, commands answered, no deauthentication) is **not** the same
> as a game *playing*. The bar this file grades against is an actual in-game
> match: both players in the same game session, able to act, with the link
> healthy around them. Several titles below clear the wire and stop at the
> game's own screen — that is recorded as its own result, not rounded up.

- Last updated: 2026-10-23 (Tetris DS two-cart play is reproducible and reaches a live match; three and four carts now have every asked client credited in its own round - `clientfail` 0 at three and at four consoles - and the three-player room still ends on the title's own decision, not on a reply the link failed to deliver)
- Legend: ✅ playable · 🟡 partial (link or lobby, no match) · ⛔ blocked by
  single-card *Download Play* (not supported **yet** — it is a planned feature,
  Milestone 4 of [to-do.md](to-do.md#the-road-to-a-working-2-3-and-4-player-link)) ·
  ⬜ no multiplayer mode at all
- Scope: **multi-card local play** (both consoles have the ROM) first, because it
  works today. Single-card *Download Play* is the next compatibility step, not a
  permanent limitation — see
  [Download Play: planned](#download-play-single-card-planned-not-supported-yet).

---

## Summary

| ROM | Players | Result | Where it stops |
| --- | --- | --- | --- |
| New Super Mario Bros. (USA) | 2 | ✅ **playable** | Nothing blocking — race reached, input coupled into the peer's picture. No race has been run to a *result* yet |
| 0168 - Mario Kart DS (U)(SCZ) | 2, 3, then 4 | 🟡 partial | **2026-10-23 (v0.1.9em): the reply path is cleared here by re-run, not by argument** - on the binary with the reply rendezvous, a four-console joined room ran **2,755 rounds with 1 failed ack (0.0%)** against the 6-8% short windows the record had behind this failure, and the room still ends at the title's own cut-off walk; four no-press holds (two with the rendezvous, two without) leave the bus `0xf` at absolute frames 12,867-13,057, the same range as the pre-change record (12,813-12,922), so the leave is on the title's own clock and not on anything the link does that round.  At two, three and four, every console couples into one group and stays coupled; CUT OFF moves **both** consoles to the game's own pre-race wait. **At three the group only ever couples in the record too - it then reaches the same park as two consoles, with no character select and no race (v0.1.9bw, measured).** **6 of 9 runs of the documented route survive that wait** (0.3–2.5 s of it in the rest), and nothing in the link decides it: the two reply knobs do not separate the arms (p ≈ 0.6), `clientfail` is neither sufficient nor necessary (falsified), and the one clean correlate — the host game's own state walk, which starts 115–147 emulated frames after the tap in 7 of 9 runs — is **not** required either, because 2 of the 6 survivors stall at `02 00` without ever reaching the cut-off state `FF 00` (v0.1.9ar). What every death shares is that the guest's game leaves first and the host's 11–12 frames later, and 2 of the 3 are runs where the host's game never left its lobby state at all — reason unproven. The longest survivor held the wait with `link mask 0x3` and both games parked in the same ARM9 poll loop (`pc9 0200FD10`) with both displays black — and that poll is now identified and was moved by hand: it is the game's own **blocking dequeue**, spinning on `*(u32*)0x2173300 == [0x21732AC]` (equal on both consoles, reproducibly), so the wall is a message its radio never posts, not a hung CPU (v0.1.9as). **After the wait, both consoles' own output goes black** ~1.3 s in and stays black (`45d15630`, and `logs/shots/mkds-after-vs.png` is one flat colour across all four screens) while the link holds `mask 0x3` for 12,725 frames past the tap — and neither the route's VS tap nor A on either console moves a pixel of it (v0.1.9ar, `logs/vs1_stdout.txt`). No race has been started at any count, and at four the app runs them at 26.6 fps (44% of DS speed). The ARM7's own message path to the ARM9 is now located and its gate disproved: the poster at `0x060092D8` branches on `[*(0x0380FFF4)+0x3F4]`, that halfword is zero in every sample of a whole session (293/293 per console, frames 1446-18719) and **no code in the 128 KB ARM7 image writes it** - poking it is followed within 2-5 frames by both ARM9s leaving the dequeue and by the game's own deauth, so the race-start message is one of this poster's ten siblings, not this one (v0.1.9aw). **v0.1.9bu drove both consoles' real input surface at that park - 10 presses and 15 touches per console, both sides, screenshot after every step, the frozen guest as the oracle - and neither game answers** (the guest's screens changed once, from `1564d6e5` to `790742fa`, on the first press and never again; the host's own command body stayed `00 80 FF 00 B8 B6` through a VS tap, a START press, an A press on the guest and a further touch), **and the button the host is blocked on is named: VS on the game-select screen**, which is never drawn - the cut-off walk is a fixed ten-step sequence (`06 00 -> 00 FF -> 00 FE -> 00 00 -> 00 01 -> 01 00 -> 01 01 -> 02 00 -> 05 00 -> 03 00 -> FF 00`) that settles 2-5 s after the tap, and 1.3 s into it **all four screens are 100 % black** for the rest of the session (`powcnt1 820F`, LCDs on). The harness defect that hid it is fixed: `--vs` waited on the host's **top** screen, whose first change after the tap *is* that black, so it tapped mid-walk and MKDS does not queue a touch; it now waits on the game's own recorded value until it settles, taps, and judges the tap by that value (`--vs-both` taps every console). Re-run `--cut-off --vs --vs-both --watch 120`: walk settled at `FF 00` after 2.1 s, VS tapped on **both** consoles, value still `FF 00` 4 s later, link perfect to frame 12207 with zero deauth (`logs/v9as_*`). **v0.1.9ca counted the park's frame classes at both ends (`md-link.py census`, `logs/v9f0`): in the parked window CMD 3,590 written/3,590 read, ACK 3,590/3,590, REPLY 3,591/3,590, BEACON 269/269 - every class the games write is forwarded and read, group-addressed destinations included - and both ARM9s are halted in the BIOS wait loop (`pc9 FFFF0238`) with the dequeue's queue empty (`0x2173300 == 0x21732AC`) and `W_IE 0`, so no frame of any class would move them**. **v0.1.9cc wired the per-game seam into the link path and tried the first AMCE-gated behaviour on that wall.** The host's client mask is derived at `Wifi::StartTX_Cmd` as `game bits & MPClientFail`, so a late reply hands the host's own game a pruned mask - its "that client is gone" signal. For MKDS only, `md_set_per_game_fix` (registry bit `MD_PG_NO_CLIENT_FAIL`) stops that narrowing and keeps polling every client. Measured live with the bit applied (`per-game link behaviour: flags 0x1 applied to 2 console(s)`, `logs/pg6_*`): the group still couples, CUT OFF still moves both consoles to the wait, and the group still dies there - radios off, `mask 0x0`, both screens `45d15630`, no deauth - while a same-route run with the switch off (`logs/pg7_*`) died the same way. Survival after CUT OFF varied (260-788 emulated frames) inside the record's known spread, so **no movement to character select or a race can be attributed to it**; the entry stays `implemented: false` and the bit is gated off in the shipped default. The first live run without the narrowing did reach a 38-byte name-carrying command (`6D 00 65 00 6C 00 6F 00` = "melo..."), but so did the baseline runs' own long frames, so it is the game's join phase, not a new state. **Next, with the measurement that supports it:** at the park both ARM9s are idle with the dequeue empty and `W_IE 0` while the ARM7's poster gate `[*(0x0380FFF4)+0x3F4]` reads zero in every sample and no ARM7 image byte writes it (v0.1.9aw) - so trace the wifi context's outgoing queue (`ctx+0x1F4`, where `0x0600F19C` enqueues when its direct path is not taken) and the other fourteen call sites' descriptors across a live park with `tools/scripts/arm7-ctx-watch.py`, to name which sibling message the host waits for. **v0.1.9cd closed that item and moved the verdict off the link for good.** Reading the ARM7's own wifi library against a live AMCE session (`tools/scripts/arm7-ctx-watch.py`, `logs/pg9_ctx.tsv`, 1,032 samples over both consoles) settled where the missing message comes from and why the link layer cannot supply it. The poster at `0x60092D8` is one entry of the ARM7 wifi library's handler table at `0x6018A40`; when its gate `ctx+0x3F4` (`ctx = *(0x0380FFF4)`) is nonzero it allocates an 18-byte message through `0x6009138` and enqueues it into the wifi context at `ctx+0x188` via `0x600F19C`, then clears the gate itself. Across the whole session the gate is **zero in every sample** (`0/1032` for p1, ditto p2), `ctx+0x188` reads `0x00020002` (head == tail, an empty queue) **including during the successful join**, `ctx+0x1F4` is never touched (all `FFFFFFFF`), and the ARM7's IPC send counter stays `0` throughout - so the ARM7 wifi library posts **nothing** on this channel even when the session is working. The ARM9 netcode queue is used and then stalls: its producer/consumer (`0x2173300`/`0x21732AC`, ring at `0x2174000`) advanced to 50 and then read equal (empty) on both consoles. The ARM7 is executing MKDS's own image while coupled (`pc7 0x0600E1DC` / `0x06012764`) and falls back to the BIOS wait (`0x0000115C`) only after the group dies. **Verdict, and it is the mission's second branch: the obligation sits inside MKDS's own ARM7/ARM9 code, not in what this fork provides.** The fork supplies the hardware (frames, registers, interrupts) and the census showed it carries every class the two games exchange; the ARM7's post is gated on a condition its own state machine never reaches, so no change to the link or firmware path can produce the message. **What remains, as a handoff:** (1) identify the exact message type the ARM9's dequeue expects at the wall - the ARM7 handler table at `0x6018A40` (entries `0x0600DA78`, `0x060092D8`, `0x06009234`, `0x0600E1F4`, `0x06015C50`, ...) is where a future RE pass would match a handler to the expected type; (2) find what sets `ctx+0x3F4` - the poster clears it and no literal store to `+0x3F4` appears in the 128 KB ARM7 image, so it is written through a computed pointer by the game's own ARM7 code, which is why the link cannot reach it. **v0.1.9ce deleted the v0.1.9cc plumbing rather than leave it gated off:** `MD_PG_NO_CLIENT_FAIL`/`MP_PG_NO_CLIENT_FAIL`, `md_set_per_game_fix`, `Wifi::SetMPPerGameFlags`/`MPPerGameFlags` and the mask branch in `Wifi::StartTX_Cmd` are gone, so the link is byte-for-byte stock again and every title's default path is provably unchanged (with every entry unimplemented the old gate always returned 0, so the branch always took the stock arm). The per-game seam itself - game identity, the honest registry, the Settings toggle - stays. **2026-10-16d re-opened the four-console question and refuted the premise that this title's walls needed it: MKDS is `download_play: false` and never had the synthesized client (see the section below), yet the route could not be re-driven to the room at four carts this pass - no page oracle for this title plus waits tuned for two consoles at ~100% while four run it at 44%. The park below is therefore neither confirmed nor refuted at four; what is owed is an MKDS page classifier.** **v0.1.9du (2026-10-22) re-drove the four-console room with the app's own joined-room recipe and measured it end to end for the first time:** every console couples (`mask 0x1 -> 0x3 -> 0x7 -> 0xf`, each join proved from its own `role` and frames) and the group then ends inside the game's own cut-off walk - in runs 1 and 3 all four radios off on the same frame (v0.1.9du's own numbers), while the final post-fix run is a cascade: p3's radio off +210.47 s, p4 +211.88 s, p2 +220.55 s, p1 +220.72 s, each console writing its own `deauth … reason 0003` immediately before its own switch-off, the guests' deauths following the host's own last round in the order in which it had stopped addressing them (v0.1.9dv) - at 58.7 fps average per console with lockstep never dissolving. The run that waited for the walk's settled page before tapping VS found the bus already `0x0`, so **VS is not what ends the room and the walk is**, and no race is reached at four. Two link defects the run exposed are fixed (a reply window that credited a blank answer as the answer it asked for, and stall kicks on consoles that had been handed nothing); the link carried every class of frame through the walk and the guests kept answering until their own radios went off. **v0.1.9dv (2026-10-22) read that run as rounds and ran the same route at two consoles as the link-quality control, and both halves move the blame off the link.** The final four-console run's 4,619 rounds ask all three clients (`0x000e` x1,208) for ~1,800 rounds and then **`0x0002` x2,896**: the host's game *pruned* p3 and p4 and finished the session with p2 alone, in the same three command sizes it had used all along (46, 74 and 54 bytes; 4,550 / 18 / 4 of the run's 4,619 commands), so it ran a one-client lobby to its last round rather than moving on. The 563 `clientfail` acks are a burst, not a background: 458 by round 1,523, **502 by 1,733 - 44 in those 210 rounds, 21%** - then 61 over the remaining 2,886 (2.1%), with the prune starting between rounds 1,733 and 1,801 where the burst ends; 210 rounds is 3.5 s of emulation, the size of the cut-off walk. The four switch-offs span 10.25 s and each console writes its own `reason 0003` just before its own, the guests' following the host's last round in the order in which it had stopped addressing them, so **the host's session end is the event and the guests' deauths are a cascade, not four radios reacting to one frame**. The control is decisive for the mission's suspect: the same route, binary and taps at **two** consoles give **1,953 rounds, 1,952 collected, one empty window, `clientfail 1` of 1,953 acks, zero deauth, 115 beacons sent / 114 accepted** - and both consoles still go to a pure-black page (`790742fa`, mean 0, 100% black) and stay there for all 360 watched frames, i.e. **no race on the healthiest link this record has**. So the ~6-8% short windows at four cannot be what stands between the room and a race, and v0.1.9cc's prune-suppression arm already showed the same (the group died at the walk regardless). The wall stays where v0.1.9cd put it - inside MKDS's own race-start handshake - and the four-console death is that wall's four-console form; the open work is the message trace (`0x6018A40` handler table, the setter of `ctx+0x3F4`), not the link. **v0.1.9dw (2026-10-22) instrumented that black page itself and the console turns out not to be running the game at all.** Five reads a second apart (`tools/scripts/mkds-park-probe.py --samples`): emulated frames climb **16,811 -> 16,931 in 2 s on both consoles** while `pc9 FFFF0238` and `pc7 0000115C` - the BIOS wait on **both** CPUs - never change; every wifi counter (`cpu line 19703`, per-source raised/acked) is identical across the 2 s because the radios are off (`link mask 0x0`, the host handed a `00C0` deauth, `handed=00C0/38b`); and the picture is not stale - the app's copy reports `changed 2754x last 10844` against frames at 16,931, i.e. the last *change* is the frame the game cleared to black and the identical crc `5C2DB22F...` has been carried since, so the copy path took the new frame and the game draws nothing after it. The memory the park waits on reads: `*(0x0380FFF4) = 0x0380AFB0`, the ARM7 poster's gate `ctx+0x3F4` = **0**, the wifi context queue at `ctx+0x188` = head `0x0380B700` / tail `0x0380B7C0` (**192 bytes queued between them and never drained**), and the ARM9's netcode queue at `0x2173300`/`0x21732AC` = `0x40`/`0x40` (empty) with `0x2174000` unchanged. So no CPU writes the gate, the poster is never called, and what the ARM7 did queue sits unread. **And "the two-console park" is two states:** this run's CUT OFF+VS ended with both radios off (`mask 0x0`) on the same black page, while v0.1.9dv's identical route left both radios up and polling (mask `0x3`, 60 rounds/s) - the difference is whether the host's game leaves the room or waits in it, and the screen looks the same either way (`logs/park2_*`). |
| Metroid Prime - Hunters (USA) (Rev 1) | 2 | ✅ **playable** | **A real 2-player multi-card match runs in the app** (history.md v0.1.9ay): both consoles reach the lobby, both pick hunters, the host's `START GAME` starts a `BATTLE` on `COMBAT HALL` with a 7-minute clock, and each console draws its own first-person view with its own HUD. At 2 consoles the app runs it at **39-42 fps** (55-56 in menus). At **3 and 4** a joining guest aborts its own handshake (`deauth sent ... reason 0003` from the guest, then its radio goes off) somewhere between 7 and 23 s after joining **when the host's game is left on its join screen - v0.1.9bb revised the cause: it is not the extra radios, and a guest left in ~10 s even when the third console's radio was never raised (bus mask `0x3`), while with the host *driven* through its lobby the guest never left at all (`mask 0x7` held past 60 s, zero deauth frames). See the MPH section's v0.1.9bb control below.** (All three of its v0.1.9az configurations left the host idle, which is the mechanism above.) While the host stays healthy and every console keeps running at 39-50 fps. Ruled out by measurement: the stall kick (`MD_KICK_AFTER_WINDOWS=0`, 0 kicks, same drops, guests leaving in the same frame as each other), the reply-window re-anchor (`dropped 0` all run), the lobby's idle timeout (an argument v0.1.9bb reverses: the drops happen exactly when the host's game is left on its join screen, which is what serialising the joins does to it), link capacity and CPU. Serial joins - one guest at a time, waiting for each guest's own screen to leave `AVAILABLE GAMES` - are strictly better than overlapping them and were the first thing to hold a second console at four consoles (host + p4 reached the hunter roster). The limit is **not** the link's delivery path, and that is now measured per client per AID: the host credited 1063 of the answers its three guests sent (278 to aid 1, 328 to aid 2, 457 of 463 to aid 3; `stale 0`, `blank 10`, `reanchor-dropped 5`, no FIFO overflow, no unusable aid), no guest ever *received* a deauth (`deauth out 1 in 0` on each of them), the host was still polling each guest at its last poll, and three *non-joined* radios sat on `AVAILABLE GAMES` for 100+ s untouched - so only a join starts the clock, and what remains is a code-level asymmetry no host counter can see (history.md v0.1.9ba). The v0.1.9bb control then showed the lobby **holds** a guest with three radios up when the host is driven (`mask 0x7` past 60 s, zero deauth frames) and loses it in ~10 s when the host is not - even with the third console's radio never raised. **No 3- or 4-player match yet.** v0.1.9bc ran the pace fix with a new driver (`tools/scripts/mph-match.sh`, the host's next lobby step fired as each join lands): **three consoles hold `mask 0x7` through the whole lobby** (host `role=1`, both guests `role=2`, each guest credited under its own aid, zero deauth frames through the picks) and four reach `mask 0xf`, but the match still does not start - the driver's `START GAME` tap lands on an **empty region** of the host's bottom screen (`appctl.py shot` is native 256x192, so `render --mono` reads the game's own pixels: maximum luminance 133 of 255 at that coordinate, no text or button), so the host's command bodies stay the 6-byte management form and each guest's own game deauthenticates (reason 0003) ~2-10 s later; at four consoles the guests then evict in rotation and the driver's recovery rejoins each. The coordinate is the two-player layout's. Frame rate is not the limit: **36-48 fps per console at four, 46-50 at three**. Measured, and not this blocker: after a drop the host hands `C001` to whichever guest returns (`C001` x6 / `C002` x3 over twelve auth-assoc cycles at three consoles; `C001` x8 / `C002` x4 / `C003` x1 at four), so both guests' replies are then credited under aid 1 - re-association can collide AIDs (history.md v0.1.9bc). v0.1.9bd then re-read every 3-4 host coordinate off the host's own framebuffer and fixed three of them (`BATTLE` `(44,68)`, the `ARENA` green check `(212,172)`, the roster's `START GAME` `(198,40)`; v0.1.9bc's `(220,178)` had been landing on **ADVANCED OPTIONS**), plus a dot oracle for the host ladder (three cyan tab dots at `(67,17)`/`(128,17)`/`(187,17)` that light 0..3 with the lobby steps). With the corrected route **a real 2-console match runs** (both consoles' own `RANK 1`, `POINTS 0/7`, `TIME 6:47` ticking, distinct green/yellow HUDs, `logs/shots/healthy2.png`), and **3 and 4 still fail** the same way: the host ladder completes (`mask 0x7`/`0xf`, zero deauth *sent* at the roster) and then each guest's own game deauthenticates (`reason 0003`) seconds after its hunter pick, `mask` collapsing to `0x1`. The AID collision is *not* this blocker (clean `C001`/`C002` sync at three) and the in-game 262-byte body marker exists only for NSMB, not MPH (the working 2-console host's bodies stay 6 and 38 bytes). Measured lead, not fixed: in the failing three-console run p2 took 191 of the host's 160-byte beacons but only 151 distinct seqs (40 duplicates) while p3 took 195, all distinct (history.md v0.1.9bd). **v0.1.9be cleared that lead with a counter and moved the blocker to the route.** `Wifi::StartRX` now identifies every frame by the timestamp the shared bus carried it with, keeps the last eight hand-overs and counts any repeat (`MPRedeliveries`, printed in the once-a-second clock probe), with `Wifi::MPHoldStamp` holding the **un-clamped** stamp so a late frame is still identified by what the bus sent. Measured: **two consoles 7,341 hand-overs, 0 redelivered; three consoles 16,494 and 12,172, 0 redelivered**; and with the reference arm `MD_MP_REDELIVER=1` (which hands one held frame over twice on purpose) **239 redeliveries in 258 hand-overs**, so the counter demonstrably fires. The 40 "duplicates" are the host's 12-bit 802.11 sequence counter wrapping - each lands exactly **80.1537 s** after the first and every hand-over has a **distinct** bus timestamp. What remains is the route: at the moment of the `START GAME` tap the host's own bottom screen has **nothing** in the roster's pill region (x 148..245, y 28..53: maximum luminance 61-65 of 255, zero orange pixels) against the reference roster `logs/shots/F_start.png` there at 182 with 832 orange pixels - so the host was not on the roster, the tap was a no-op, and the guests' own games deauthed ~10 s later (p2's `reason 0003`, then the host's `reason 0007`). The dot oracle saturates at 3, so a page past the roster also reads "3". **No match started at 2, 3 or 4 consoles this pass** (history.md v0.1.9be). **v0.1.9bf replaced the driver's screen oracle with a per-page pixel classifier** (`tools/scripts/lobby_page.py`, 15 labelled frames in its own `--selftest`) because the completion dots saturate at three and so cannot tell the roster from any page after it; the ladder is now destination-named and idempotent (one verified tap per step, and no retry can advance past the roster). That convicted three of the project's own instruments - a step that named the page it tapped on instead of the one it was reaching (host parked on MODE, the state that drops a guest in ~10 s), `ROSTER` as the "nothing else matched" fallback (a mid-transition frame matches no page at all and read as the roster, so the host's HUNTER step refused to tap and the host stayed on its hunter page all run), and the match oracle itself (a `THERE ARE NOT ENOUGH PLAYERS TO CONTINUE` box over the in-match HUD reads **2729** ink pixels against a real match's **2730** - only the top screen differs, 915 colourful pixels against 16k-33k, so the oracle now requires both and reports `DIALOG`). **The blocker has moved off the route:** at three consoles the host's ladder now lands on the roster with one verified tap per step (ADD BOT 90, pill 0 for the whole watch, listing only itself) while **both guests' own hunter confirms are refused** - p2 and p3 each stay on their own `HUNTER` page after select *and* confirm, so neither is listed and `START GAME` never exists. A live two-console lobby that listed both players and offered the pill still failed the handshake on the tap (`THERE ARE NOT ENOUGH PLAYERS TO CONTINUE` on the host, `YOU HAVE BEEN DISCONNECTED FROM THE HOST` on the guest), so a roster listing a second player is *not* sufficient evidence that the guest's join completed. **No match started at any console count this pass**, and four consoles were not reached (history.md v0.1.9bf). **v0.1.9bg re-established the two-console control with a recalibrated oracle** (a real match's `LAWS OF BATTLE` briefing had been reading as a dialog, so the pass's first "failure" was the threshold, not the game) **and answered the three-console question from the wire: the second guest's join is not refused.** It is admitted, addressed and credited every round - both clients polled under their own AIDs (1 and 2), both arms answered, `collected a reply from p2 for aid 1` + `...from p3 for aid 2`, `collected=0006 of mask 0006`, `clientfail 0` - and beacons reach both clients at **100%** in every steady second right up to the end. The group ends on the *host's* side: its round loop (60.0 commands and 60 acks per second, rock steady) stops mid-second and never resumes while its beacon keeps firing and its `role` stays 1, and both guests' own `reason 0003` deauths are on the wire within 6 ms of that stop (each after accepting the host's beacon, syncing its clock to it, and taking the countdown IRQ14 its own game armed). A watchdog now names that moment in the log with its counters (`cmds_tx`, `rounds_multi`, the lobby's lifetime, `acks_sent`/`acks_client_fail`, and how long after the newest client's association it came - **4.5-60 s across four runs**, so it is not a fixed join timeout), and the one route hypothesis was tested and refuted: a client arriving after the host's roster already offered `START GAME` is not the trigger. Refuted with counters: beacon starvation, reply-window starvation, AID mix-ups (the `AID ... this poll is NOT ours` trace was reading the raw `W_AIDLow` - **fixed**), the frontend watchdog (`kicks 0`), and our own injected input (none in the window; the app's own `[control]` log proves it). **Then the host's own two-client bookkeeping was dumped from main RAM across the stop and compared with a healthy two-console session**: the per-console peer record on page `02101000` counts the clients correctly in both (the host `01` with one client, `02` with two; the newest client's MAC; each guest's own party index 1 and 2), `W_CMDStat0..2` read `0000` at every sample before and after the stop, and the record is stable across the stop - no supplied value is stale or wrong. **And the walk-away is not three-console-specific**: a two-console run this pass lost its guest four times in a row (its own deauth and radio off, the host's roster never listing it - `pill 0` for 80 s), so an unprogressed lobby loses its guest at any console count, and at three consoles the second client's association *removes* the first's ready state (`logs/dc3_match.log`: `pill 413` -> `pill 0` as the third console joins) while both guests' own answers stop carrying state. Verdict: the divergence that precedes the walk-away is in the games' own lobby state, not in anything this link hands them - the mission's fallback branch - and what 4-player compatibility would need is the games' own lobby admitting a second remote client, which this stack cannot supply from outside (`logs/bg_memory-vs-2console.txt`) (history.md v0.1.9bg). **v0.1.9bh, with the link restructured per console:** the same route at three consoles now starts a match (`mask 0x7` held to the end of a 150 s watch, one guest leaving once and walked back in) and the host's *own* beacon carries a **three-entry party list** for the first time on record - 53 frames around the second join, delivered to both guests, against **zero** three-name beacons in five pre-rewrite runs. It then falls back to two names while the second guest is still attached and credited (414 of 18,007 rounds polled it), which is the host's game's own table and not the bus (`collected == cmds tx`, `clientfail 12` of 18,007, `stale 0`, `notlooking 0`, zero watchdog stops). A three-*player* match is still not re-demonstrated: the second client is polled rarely once the match is running, so what plays is host plus one guest with the third attached (history.md v0.1.9bh). **v0.1.9bi corrected that pass's party-list reading and named what ends the three-entry window: it is the guests' own membership.** The "host hears almost none of its clients' beacons" number (35 to the host's game against 2,011 to the other guest) was measurement, not routing: **the clients transmit no beacons at all** in a lobby - only the host does (1,446 room beacons in that match run, 941 in this pass's lobby hold) - and the 60-byte `fc=0080` frames it was counted from are each console's *own* `melonAP` access-point beacon (one per console, every 128 ms, delivered to that console's own game only while it is outside multiplayer, never on the bus; `tools/scripts/beacon-party.py` now separates the room beacon from the AP beacon, and this is the third time a per-console local frame has been read as bus traffic). What the host's game does hear from each guest is everything the guest sends: the guests' association/deauth/data frames, and 17,996 of 18,006 answers from aid 1 and 414 of 420 from aid 2 in that run with `clientfail 12`, `stale 0`. Three party entries hold for exactly as long as the guests' games stay in the room (26/27 delivered three-name room beacons then, 34/38 in this pass's lobby hold) - and each guest's own game then writes its own `deauth sent ... reason 0003` and powers its radio off **11-18 s after associating, with every poll it received answered and every answer it wrote collected** (`clientfail 0`), which is what makes the table fall back to two names; the host never deauths a client in any of these runs. **No three-player match yet**, and the divergence to look at next is that in the lobby hold the host's roster never registered either guest's pick (`pill 0`, `icons 0` for 15 s twice) while the guests' own screens read `page=HUNTER pill=69 icons=333` after their picks (history.md v0.1.9bi). **v0.1.9cf exercised the three-console gap live through the app's own route and took the two-console control on the same binary.** Two consoles: the route walks the lobby, the host's roster offers `START GAME` (`pill 414`), the tap starts a real match and it holds - `mask 0x3`, `deauth-sent 0`, the host's own bottom screen reads `INGAME` with a colourful top screen for 25 s+ and the host's round loop never stops (`cmds tx` still climbing at the last sample, `clientfail 0`) (`logs/mp2c_app.log`, `logs/mp2c_match.log`, `logs/shots/mph2_match_a.png`). Three consoles: both guests associate (`role=2`), the host walks its ladder, and then **the host's own game stops writing commands** - its own watchdog line reads `last command seq 5920 at +62.832s, 7113 ms after the newest client associated, after 640 rounds (426 addressing 2+ clients) over 10.680s ... 640 acks, 0 of them reporting a failed client, and the beacon slot is still armed` - and both guests' own games then deauth (`reason 0003`, `p2 0009BF126643 -> 0009BF112233`, `p3 0009BF13AA53 -> 0009BF112233`) and power their radios off; the host's roster never lists a second player (`pill 0`) `logs/mp3c_app.log`, `logs/mp3c_match.log`, `logs/shots/mph3_picks.png`. **The instruments say the link is not what breaks it.** `md-link.py census` over the trace-verbose run: the host wrote 640 CMD and 640 ACK, and **every frame a console wrote reached its peer's game** (`frames the fork itself did not carry: none`); both guests received all 640 commands (p2 640 direct; p3 426 addressed to it plus 214 delivered as a group frame) and each answered from its own arm (p2 639, p3 426); the host's reply windows read `has everyone it asked for` throughout and its acks report `clientfail 0`; the host addressed both clients (`mask 0x0006`) for 426 rounds. `md-link.py rounds` pairs the guest answers with the host's collects as **identical 1065, DIFFERENT 0**. Group addressing, per-client reply, ordering and the join-time mask all work for the extra console; what fails is MPH's own host game, which stops its round loop ~7 s after the second client associates regardless of what the link hands it. **Verdict: the mission's second branch - MPH's own code, not the link - so no link change was made; every other title's path is untouched.** Measurement note: the three-console runs used `MD_KICK_AFTER_WINDOWS=0` because the stall-kick watchdog resyncs a console during multi-guest joins (its own documented hazard), but the same failure appears with the watchdog at its default (the host kicked three times pre-join, the same guest deauths follow), so the kick is not the trigger (history.md v0.1.9cf). **2026-10-16d: the four-console re-run this title was owed is blocked on an instrument, not on the link - MPH was never a phantom-client session (`download_play: false`, no log in the archive carries the line for it), the recorded route does not align across four consoles when re-driven, and `lobby_page.py` reads this title's own menus as `INGAME`, so neither the room nor a match can be certified from that classifier. A four-console MPH measurement needs a HUD-text or body oracle first.** |
| Star Fox Command (USA) | 2 | 🟡 **v0.1.9di emulates the baseband/RF busy flags (`W_BBBusy` 0x15E, `W_RFBusy` 0x180) instead of returning 0, and the flag theory is falsified by the trace.** A transfer starts on a `W_BBCnt` write (5 = write, 6 = read) or an RF register clock (`W_RFData2`, `W_RFCnt` bit 0), the flag reads 1 until the byte is clocked through and then 0, and `W_BB_READ` holds the previous transfer's data while one is in flight (GBATEK's "from previous W_BB_CNT transfer") - which is the pipelined shape the child's driver already uses. Live on the unchanged route (`logs/v9bbrf.log.gz`, `logs/v9bbrf_wifiio.txt`, route exit 0, nothing set; transfer still 2502 packets / 1260340 bytes; carry still channel 7, BSSID `00:09:BF:11:22:33`, AID 1): the child's busy polling now fires (`W_BBBusy` 2,253 -> **15,932**, `W_RFBusy` 123 -> **643**), and **every other number is byte-for-byte what v0.1.9dh measured** - `W_BBRead` 1018 last `003F`, `W_ModeWEP` 11 last `000A`, `W_Random` 6 last `007F`, `W_Preamble` 6, the `W_MACAddr`/`W_BSSID`/`W_AIDFull`/`W_XBuf` registers 5 each - with `TRX power ON` still **x0 on the child against x8 on the host**. So the child is not blocked on the baseband busy flag. It is not spinning on it either: four `probe --player 2` samples 6 s apart read the same `pc7 0000115C` / `r0 04000000`, and `lr7 037F84FC` lands in the child's own ARM7 code, read live and disassembled - a retry loop at `0x037F84F8`-`0x037F851C` that calls the thunk `0x037F852C` (`bx 0x038043E3`) and loops back, ending in the FreeBIOS ARM7 delay at `0x115C`, while the ARM9 alternates `0x0201C7FC` (`bl 0x02087350`) with `0x0201CD34`. No regression on the working console: the host read `W_BBBusy` 15,616 times and completed the whole DP handshake and transfer. `cargo test --lib` 39/0, `md_smoke_test` 149/0. one unattended command now takes the route to its parked state (host → joiner registered → Cut Off), and the whole handshake clears with every reply collected and no deauth — `NameRequest` → all five name snippets → **`RSA`** → RsaDone — with the `RsaReply` landing *inside* the RSA round; the host still never streams `04 DataPacket`, and it parks at RsaDone *before* Cut Off, so the transfer's trigger is inside the host's own game. **v0.1.9co reads that host code instead of inferring it:** the parked command frame is composed by Nintendo's ARM7 wireless library into wifi-RAM slot 0, the class byte at slot offset `0x2A` is written every round by one halfword copy with the same value `00`, and the host's ARM9 is meanwhile pushing one word per command round into the ARM7 IPC FIFO — so the host is scheduling a dummy, not waiting for a frame, and the condition that would move the class off `00` is still unnamed. **v0.1.9cp probed that gate empirically instead of reading it** (`tools/scripts/sfc-dp-poke-search.py`: one word at a time through the host's entrant/received-name/DP-title records, held and restored, scored on the joiner's received class and the composer's `002A` class byte, with `commandsRecv` liveness): over four parked sessions the classes ever seen were `00`, `01`, `03` — **never `04`** — and the `002A` byte stayed class `00` through every trial, so the gate is **not a plain single word in those records**; the words that do anything are structural and only **freeze** the host (`0x020B4C00` word 0 set to 0 makes it forget its client and fall back to the 34-byte six-byte-body `fc=0228` command form, while `0x020B4C40`/`0x020B4C48`/`0x020B17A0` word 0 each stall the command stream 10–30 s and recover). The host's own counters also show its reply windows almost all **empty** (`collected 3k` vs `empty 108k`, `clientfail == empty`) because its trace prints `this frame only has 0us of slack left` — the per-frame reply budget is spent before the window — yet replies *are* collected in bursts each round (body `00 00`, reply type Dummy) and it still repeats `00`. **v0.1.9cq then read the decider instead of the data, and named the byte that sets the class.** The per-round site v0.1.9co located (`0x02030660`) is the ARM9 SDK's IPC-FIFO send primitive, pushing a *pointer* to a WM message rather than the class itself; the host's Download Play engine is a compact **Thumb** module at `0x02029C00`–`0x0202A000` whose phase-2 handler (`0x02029DD0`) picks `4` or `9` from `buffer[0x4b2]` and whose phase-5 handler writes `titleA[6] = state+0x0E` — both held and **neither moved the wire class** (forcing `buffer[0x4B2] != 0` changed the module, `state+0x10` became `4` and the phase machine began cycling `01 -> 03 -> 04`, while the wire stayed class `00`), so those are negative measurements. The real command body was found structurally — **no code in ARM9 `0x02000000..0x02400000` contains the literal `0x023722E0`**, so the pointer to it is computed and perturbing the *records* could never have found it: the body is the 6-byte block at ARM9 `0x023722E0` (`class, 50 08 00 02 00`), referenced by the TX descriptor table at `0x0237A7EC` (`+0x08`) and the WM message ring at `0x020B5440` (`+0x04`, length 6), and its first byte is exactly the byte GBATEK's CMD DATA `[06h]` carries to wifi-RAM slot offset `0x2A`. The host rewrites it to `00` every round (~166/s, restored within 20 ms), and **holding it at `04` makes the host arm and stream `04 DataPacket`** — 15,356 wire frames of `06 01 02 00 03 11 04 50 08 00 02 00` against 309,913 of the unpatched `06 01 02 00 03 11 00 50 08 00 02 00`, with the joiner announcing class `04` 968 times and collecting it 245,705 times over a 75 s hold. **This is a host memory patch, not a client behaviour** (`tools/scripts/sfc-dp-arm-class.py`, not wired into the app): the joiner already answers NameRequest and RsaDone, its replies are collected (`our reply collected 22300`, `stale 0 deauth out 0`), the association is accepted (`status 0000, AID c001`), the host logs `[HOST] syncing client C001` — and the class is still `00`, because it is the *output* of the host's own DP phase decision, written after it reads its replies, so no frame this client can emit moves it. **Captured frame layout (the spec for the capture side):** `06 01 02 00 \| 03 \| 11 \| CLASS \| 50 08 00 02 00`; GBATEK's real class-`04` payload (`09h u16 Packet Number, 0Bh .. data`) is absent because the host's own engine never builds it — the frame layout is real, the contents are the host's placeholder. **v0.1.9cr then found the writer of that byte and the gate behind it.** A new store watch (`appctl.py memwatch`, `NDS::SetMemWatch`) reports which instruction stores into a span of a console's own main RAM, with its registers — needed because the writer reaches the byte through a computed pointer, so no literal exists to search for. It named one writer, on the interpreter: `strb r3,[r1]` at `0x0202BBDE`, in the publisher at `0x0202BBD8`, `r1 = 0x023722E0`, `r3 = 0`, `cpsr = 0x000000F2` (**IRQ mode** — the engine composes its command from an interrupt), called from `SendCommand(cls, arg)` at `0x0202864C` (`0x0202865C`), which writes into the outgoing buffer at `*(0x020B3DBC)` — live `0x023722E0`, i.e. **the DP state struct begins with its own command buffer**. All five callers of `SendCommand` are one function, the engine's per-round poll, and they send classes `5, 1 (NameRequest), 6, 2, 0` — `0` exactly when a decider returns **21**, the engine's "nothing pending" answer. Which decider is live was measured: forcing the first one's scan test changed nothing across 108,853 samples, proving the live path is `0x020852C`; and that one returns 21 when it picks no client whose **mode field** `*(u32*)(base + 0x14E8 + 4*(i-1))` equals **5** — live, the registered client's mode reads **14**, so the host sends Dummy every round. Holding that one field at each value takes the host off Dummy: **`2 -> 01`, `4 -> 02`, `5 -> 03` (streamed, 60,216 of 60,380 samples), `8 -> 05`, `11 -> 06`, everything else — including the natural `14` — `-> 00`**. So the field is a class selector, and it is the host's own per-client engine state: nothing a guest puts on the wire reaches it. The class-`04` path **does exist** and is not a stub — `0x020283A2`, gated by `0x0202913C`, a seek into a **segmented image** (binary search over `u16` segment starts at `desc+12`, returns 0 past `*(u16*)(desc+18)`, else fills segment start/length/source/index and returns 1) — but it hangs off the decider the live engine does not call, so no value of the mode field reaches class `04`. The branch is named exactly, with the values it reads; the input that opens the data path is host-owned and still unnamed. **v0.1.9cs watched that mode field and named its writer, and then forced the routing.** The field is set by one host routine, `DP_SetClientMode` at `0x02028D84`, whose guard (`0x0202817C`) is only `cmp r0,#1 / bcc / cmp r0,#15 / bhi` — a bounds check, not a content gate: **no frame a guest sends takes part in choosing the mode.** Arming the store watch *after* the handshake sees zero stores — the field is written once per state and never rewritten — so the watch has to be armed first, and it then shows the engine walking its own machine: two init clears, then **2 → 10 → 5 → 14**, each from the store at `0x02028DA0` in IRQ mode. The twelve call sites carry one mode each (`10, 11, 5, 4, 14, 6, 7, 8, 1, 2, 9, 3`). Why mode 5 means class 3 is now code, not just measurement: the poll (`0x02028228..0x020282FC`) routes on one halfword to one of two choosers, and the **live** chooser is `0x020852C`, which emits class **3** at `0x020285FC` after histogramming clients whose mode is **5**. The class-**4** emitter (`0x020283A2`) is inside the *other* chooser, `sub_020830C`, which the live engine never calls. Forcing the poll's routing there (`poke 020282DC = E0022800`, a `beq`→`b.n`, plus the client slot's `+0x1D4C` u16 set to 1) makes the engine build a **real payload**: the store watch confirms the class-4 site (`lr = 0x020283C9`), the composed body is `04 00 00 00 00 53`, the bytes beyond the 6-byte header read **`"STA RFOX CMD "`** (the game's own data), and the WM message ring `0x020B5440` goes from length **6 to `0x166`** — 358 bytes. **But nothing reaches the wire, and that is measured:** the TX slot's class byte stays frozen at `n=39720` while the slot is still armed (`73,008 → 73,415`, i.e. retransmitting its old body), the ARM9→ARM7 IPC keeps flowing, and the only frames in the log tail are `tx frame: slot 1 (CMD) fc=0228 len=34` — the short form the joiner's own length guard refuses to read as a command, which is why `commandsRecv` freezes and the class stays `00`. So the hand-over to the wireless library does not happen on that path, and **nothing a guest can put on the wire reaches it**: this half of the problem cannot be solved from the joiner. **v0.1.9ct finds the hand-over and shows it working, and corrects that conclusion.** The ARM9 crosses to the ARM7 by pushing `(slot << 6) \|\| 0x0A` down the IPC FIFO, where the slot is one of **eleven 0x100-byte message buffers** at `0x020B4C40 + 0x100*k` that the ARM7's own wireless library lists in a descriptor at `0x023F8338` (`{table 0x023F8348, size 0x20, count 0x0B}`, found by scanning the dumped library for the ring literal `0x020B5440`). Every slot holds the same record `{id 0x800F; buffer 0x023722E0; length; 0xFFFF; 1; 3; ARM9 callback 0x0202B085}`, and the composer that builds the wifi-RAM TX slot (`0x023EFC18`, ARM7 code in main RAM run through the `0x027E0000` mirror) copies the body only when `[sp+4] != 0` **and** its descriptor's `[r5+0x1C] != 0`, writing slot `0x0A` as `[r5+0x1C] + 0x22`. **The message's length at `slot+8` is that length**, and holding it drives the composed body: `6` -> a 14-byte frame body, `0x40` -> **72**, `0x1F8` -> **512**, `0` -> no body at all (`tools/scripts/sfc-dp-data-frame.py`, a labelled host patch that touches only slots really carrying the DP command and restores every word). So the `0x166` of v0.1.9cs did reach the wireless library; it was the forced engine path's descriptor that came out zero-length. **Captured `04 DataPacket` frames, with the layout a capture-side reader needs:** `06 01 02 00 / PP / 11 / CC / <payload>`, six-byte DS header, `PP` the host's incrementing DP packet number, `CC` the class, payload the host's own buffer from `0x023722E0` - e.g. `body[72]=06 01 02 00 20 11 04 50 08 00 02 00 00 38 02 00 00 00` (93 in one 6 s hold, the joiner collecting `04`) and `body[512]=06 01 02 00 FC 11 04 50 08 00 02 00 00 38 02 00 00 00` (38). The host also emits a 242-byte class-`03` frame ten times during the handshake, so long bodies were never the obstacle. **What is still missing is the download itself, not the transport:** the payload is the host's own stub, because the host's engine never fills `0x023722E0` with image bytes - and the held words perturb the host's own state machine (after the `0x1F8` sweep it deauthenticated the joiner, reason `0001`, and moved to another DP phase). **v0.1.9cu tests the "the gate is host-internal" conclusion from the link, and it does not hold.** Correlated on one clock and with **no memory pokes**, the unpatched host's per-client phase walks `2 -> 10 -> 5 -> 14` (four stores, all from `pc 0x02028DA4`) while its command class walks `01 -> 00 -> 03 -> 00` in **243 ms emulated**, each phase store in the same 150 ms window as its class change. With the joiner associating and then answering **nothing** (`MD_DP_REPLY_MUTE=1`, a new diagnostic arm: nothing before the association differs), the host's phase cycles `2 -> 3 -> 0` and never reaches 10, 5 or 14, its class changes exactly once - to `01` - and stays there for **20,683 commands** with **0 replies collected in 20,069 empty windows** and 13 deauths (`reason 0001`). So phase 14 is where a *working* joiner puts the host: the client is causally upstream of the host's progression, and the earlier reading holds only for the class byte, which the host derives from its own phase. **The next input:** the Thumb dispatcher at `0x02028906` sets mode 14 on `phase == 5` and sets **mode 6** - the download direction - on `phase == 14` only when a per-client halfword read as `ldrh [r1 + 2*(2*client) + 0x1754]` equals `2`; the address is recorded as open rather than asserted. The host still never streams a download payload. **v0.1.9cv pins that halfword and opens it from the joiner.** The dispatcher `0x02086F4({message, client})` reads the client mode as `word[base + 0x14E8 + 4*(client-1)]`, jumps through the table at `0x020873C`, and runs its `cmp r6,#5 / cmp r6,#14` handler with `r1 = base` and `r5 = client-1`, so the tested halfword is `ldrh [base + 0x1754 + 2*(client-1)]` - for the registered client exactly `0x02373A34`, which the parked host reads as `2`, so the gate's precondition already held and `r1` is not clobbered on that path. The driver is the reply's **type**: the dispatcher takes it at `0x0208726` from the per-client message and the helper at `0x020BB14` splits it on `7 / 8 / 9`; `07` is `dp_joiner`'s UsernameReply and `08` its RsaReply. Username replies drive the walk `2 -> 10 -> 5`, one RsaReply drives `5 -> 14`, and the **same class-8 arm (`0x0208906`) drives `14 -> 6`** (`DP_SetClientMode(client, 6)` at `0x0208946`) when that halfword equals `2`. The host keeps asking a `00` dummy round after RSA, and the joiner was answering it with a type-`00` dummy - which the mode-14 arm ignores - so the host parked forever. It now answers every post-RSA round it has no specific reply for with the **RsaReply**, and with **no pokes and no override** the mode field walks `2 -> 10 -> 5 -> 14 -> 6` (store watch over `0x023737C8`, the extra `<- 00000006 @pc 02028DA4`) and the host **streams a real `04 DataPacket` (size 179, not its 3-byte stub)** with `tools/scripts/sfc-dp-host.sh` reporting `TRANSFER STARTED` (exit 0). So an unpatched host enters the download direction on its own, and the older ending is superseded. Two diagnostic overrides sweep the reply type per round (`MD_DP_REPLY_FORCE`, and `MD_DP_REPLY_FORCE_DATA` for the `04` rounds): a type-`09` reply on the `04` rounds already advances the host's packet loop (`04 DataPacket` size 179 then **255**), but the type-9 body's next-packet halfword (`ldrh [sp+18]` at `0x020896C`, compared and stored against `base + idx*0x5D4 + 0x1D4A`) still carries a username snippet, so placing that field is the next input - the download is no longer blocked, only its packet loop is not yet driven. Validation: `cargo test --lib` **28 passed, 0 failed**; `md_smoke_test` **149 checks, 0 failures**. Evidence: `logs/sfc_natural.log.gz`, `logs/sfc_force8.log.gz`, `logs/sfc_data9.log.gz`. **v0.1.9cw: the download now completes naturally, with no override set and nothing poked.** The packet loop is implemented rather than probed: a `04` DataPacket is parsed (packet number at body `09h`, data at `0Bh`, the spec's one padding byte dropped so a full packet comes out at `1F8h`), every `04` round is answered with the **DataReply** carrying the smallest missing packet at `03h` and the distinct-packet count at `05h`, the **GotAllReply** (`0A`) replaces it the moment the RSA-stated total has arrived, and the host's `05` Final is confirmed with `0B`. Two consoles through `tools/scripts/sfc-dp-host.sh` (exit 0): the RSA states 1260340 bytes, the first packet is `#0` with 352 bytes starting `53 54 41 52 46 4F 58 20` (`"STARFOX "` - real image data, not the host's 3-byte stub), progress walks packet by packet at roughly 12 packets/s, and the image ends **complete - 2502 distinct packets, 1260340 of the 1260340 bytes the RSA frame stated** - after which the host's class goes `04 -> 00` and the GotAllReply is answered (`logs/sfc_dl.log.gz`, `logs/sfc_fix.log.gz`). `cargo test --lib` **33 passed, 0 failed**; `md_smoke_test` **149 checks, 0 failures**. | The only local mode is Download Play; a multi-card join does not exist in the cartridge. **v0.1.9cg drove the host to its Recruitment screen reproducibly and captured what it sends** — `tools/scripts/sfc-dp-host.sh` takes a cold two-console launch into Download Play → Recruitment by touch geometry, deciding success only from link state (`role=1`, console on the bus, 204-byte `fc=0080` beacons leaving), and it is one command with a state-based pass/fail (cold run: `PASS ... beacons on the wire (23, +23)`, `logs/sfc_dp_host.log`). `md-link.py census` over the captured session (`logs/sfc_dp_wire.log`) shows the host writes **42 BEACON frames (`fc=0080`, 204 bytes, broadcast `FFFFFFFFFF`) and nothing else** — no command, reply or download class is on the wire, and the peer's game reads 0 of them because it is not in Download Play. So what exists is an *advertisement*: the host broadcasts and waits for a client that has never existed here. **And the receive path is genuinely absent, in this fork and in stock melonDS** (see the receive-path finding in to-do.md): `NDS::SetupDirectBoot()` boots the cartridge's own ROM (`src/NDS.cpp:297`, `upstream/master:src/NDS.cpp:297`), there is no multiboot/download-path keyword anywhere in either tree, and the only `MB_*` symbols are the DSi wifi *mailbox* (`src/DSi_NWifi.h:89`), not DS multiboot. The client must be written from scratch — which is the whole of the joiner's side. **v0.1.9ci built the client's first half and got the host's answer: it is a refusal, and it is the host's own words.** `src-tauri/src/dp_joiner.rs` (gated by the `ASFE` registry entry and the one Settings switch) listens for the beacon and answers it with an open-system authentication request, then an association request echoing the beacon's own IEs. On the wire (`logs/sfc_dp_join.log.gz`): the host reads every frame the joiner sends, answers the authentication **205/205** (`fc=00B0`, unicast to the joiner) with **sequence 0002, `status 000E`** — 802.11 status 14, "authenticated transaction sequence number out of expected sequence" — so the transaction is answered but never granted, and its answer to the association is a **deauthentication `reason 0006`** ("class-2 frame from nonauthenticated station"). Zero association responses. So the download conversation is still not reached; what is now known is the exact refusal — the authentication transaction the DS expects — and that the transport carries a synthesized client correctly. Gating proven: switch off ⇒ zero joiner frames on the wire while the host beacons normally (`logs/sfc_dp_off.log.gz`), and a non-Download-Play title gets no joiner thread at all. **v0.1.9cj got the client in: the authentication is granted and the host accepts the association, and the exact frame it waits on next is named.** Two defects were fixed on the client side. First, the frame builders omitted the hardware's 4-byte FCS slot, so the bus wrote the wire CRC over the last four bytes of the body and the host read its transaction sequence number out of a checksum - the `status 000E` of v0.1.9ci; with the slot restored the authentication is a 34-byte frame (`24 header + 6 body + 4 FCS`), exactly a real client's, and the host answers **`sequence 0002, status 0000`**. Second, the association request now matches a captured DS client byte for byte: a real request (`logs/ab_ctl_app.log.gz`, `body[0..41]`, `tx frame: slot 2 (LOC2) fc=0000 len=70 seq=0010`) is `capability + listen interval + 32-byte SSID IE + the four supported-rate bytes raw` - **no rates information element** (Nintendo's own layout, `WifiAP::RecvPacket`) - where ours had been a 72-byte frame with a rates IE and the host's empty SSID echoed. It is now 70 bytes on the air and traces `len=82 fc=0000 seq=0010 sync=0025200000010021`, identical to the captured client; `parse_beacon` reads the same layout (the four raw rate bytes at body `12..16`, the SSID IE scanned from body 16). **On the wire** (`logs/sfc_dp_join3.log.gz`, 2 consoles, Recruitment host, joiner on console 2): **47 authentications, 47 answers, all `status 0000`**; **47 associations sent, 46 answered with `fc=0010`** (`p1 tx len=50 fc=0010 rx=0009BF126643 ... sync=0201000000010021`); **46 host deauthentications `reason 0001`** (30-byte frame, 6-byte body). So the host accepts the joiner and the download conversation is entered. **What it waits on:** an MP *command* frame is one whose address-1 is the multicast `MPCmdMAC = 03:09:BF:00:00:00` (`src/Wifi.cpp:2006`); the host's own digest reads `cmds rx 0` continuously while its game polls the receive window (`window reads 3700 (+74/cycle)`, `polls 9869 (empty 9865)`), it hands the association to its game marked `fc=0000 ... (not a command)`, and ~3.9 s later it deauthenticates (reason `0001`) and re-accepts. The missing next piece is therefore a **client command frame addressed to `03:09:BF:00:00:00`** - the association already reached the host game's hands, and the deauth is its timeout for the command after it, not a refusal. **v0.1.9ck made the client as faithful as the captures allow and disproved that inference.** Two things changed on the client. (1) The captured protocol's direction: GBATEK's Download Play chapter has commands as *host-to-client* (`03:09:BF:00:00:00`, `W_TXBUF_CMD`) and clients sending *replies* (`03:09:BF:00:00:10`, `W_TXBUF_REPLY1`), and a scan of every captured multi-card log confirms the only console that ever writes a command is the host - so the client's first frame to reconstruct is the short dummy **reply** (`fc=0118`, body `00 00`) a real client sends right after associating, captured in `logs/mph4b_app_4con.log.gz` (`tx frame: slot 5 (REPLY) fc=0118 len=30 seq=0020`; the host logs it cropped as `26 byte frame, body[2]=00 00`). `dp_joiner.rs` now builds and sends exactly that. (2) Every captured host's beacon carries its game serial in the `DD` tag at data offset 12 (MPH `0A 00 40 00`, NSMB `50 01 40 00`, Star Fox `18 02 40 00`) and each host's clients echo that same word as their SSID's first four bytes; ours had sent `25 00 00 00`, so it was asking for a title the host did not have. The serial is now parsed from the beacon and echoed (`[dp] hearing ... game serial 00400218`, association body `21 00 01 00 00 20 18 02 40 00 ...`). **The premise does not hold:** `MPD.cmds_rx` is incremented only on CheckRX's `(macgood && IsMPClient)` branch, so a host can *only ever* print `cmds rx 0` - the inference from it was invalid. A command sent to the multicast command MAC reaches the host's game (`36 byte frame, fc=0228` delivered) and changes nothing (and is also dropped by the host's own filter, `RXFilter2 kills from/to=2`). The real dummy reply is delivered and **consumed** (`handed ... fc=0118 consumed=26 halfwords`), the association is accepted every time, and the host's game never writes its command slot (`cmd 0000` on every `tx frame`, `cmds tx 0`), sits in `arm9pc 0x02030488`, and deauthenticates `reason 0001` ~3.9s later. So the host's Download Play code never starts the transfer, and no frame class reachable from the wire has unblocked it. `logs/sfc_dp_final.log.gz` **v0.1.9cl found the field the host was actually refusing on, and the host now starts the transfer.** The previous record read the host's association response as an acceptance; it never decoded the body. The trace's `sync=` field is that body's first eight bytes as one little-endian u64 (`PlatformBridge.cpp`: `sync = *(u64*)&frame[24]`), so a working host's response decodes to capability `0021`, status `0000`, **AID `C001`**, rates `01 02` (`sync=0201C00100000021`, what every real-game log carries) - while every Star Fox joiner run had been reading `sync=0201000000010021`, i.e. status **`0001`** (unspecified failure), **AID `0000`**: a refusal, confirmed independently by `[HOST] syncing client` (which requires a nonzero AID) firing in every real session and never once here. The joiner could not have known: its `Rx::AssocResponse` was a unit variant that discarded the body. The missing field was in the client's SSID. Real game clients' association requests, byte for byte from three titles, are `serial(4) + <beacon DD bytes 16..18>(2) + 00 00 + name/padding`: MPH's client sends `0A 00 40 00 12 23 00 00` against a beacon whose `DD` tag reads `... 0A 00 40 00 12 23 54 01` (`logs/mph4b_app_4con.log.gz`); NSMB's sends `50 01 40 00 DA A3 00 00` against `... 50 01 40 00 DA A3 1E 01` (`logs/nsmb_app.log.gz`); a third sends `25 00 00 00 A9 D4 00 00` against `... 25 00 00 00 A9 D4 1C 01`. The joiner had echoed only the serial and left the next two bytes zero. It now parses the beacon's `DD` token and echoes it (`HostBeacon.token`, `client_ssid(serial, token)`), and `Rx::AssocResponse { status, aid }` carries the fields that decide the join, with acceptance defined as **status 0 *and* a nonzero AID** - the host's own test - so a refusal is never logged as an acceptance. Measured live twice (`logs/sfc_dp_token.log.gz`, `logs/sfc_dp_token2.log.gz`; 2 consoles, Recruitment host on console 1, joiner on console 2): each run is one clean handshake (`auth 1/1`, `assoc 1/1`, `last assoc status 0x0000, AID 0xc001`), the host logs `[HOST] syncing client C001`, arming its command slot (`slots beacon 811C cmd 8000`) and transmitting `fc=0228` commands whose body decodes (GBATEK CMD layout) to reply-time `0106`, **slave flags `0002` (slave 1 connected)**, size 3 and command `01` = **NameRequest**. **Where it now sits.** The host repeats that `NameRequest` (13,614 commands in run 1) while its reply window collects nothing (`collected 0`, `clientfail` rising each window), because the joiner's answers go out as broadcast packets via `md_link_send_packet` -> `MP_SendPacket` - the regular receive path - not the reply path the host reads through `Platform::MP_RecvReplies` -> `LocalMP::RecvReplies`, which is keyed by AID. No bridge exposes the reply send yet (the core has `MP_SendReply`); that, with the AID the host assigned, is the next step. **v0.1.9cm exposes it, and the handshake now advances.** The new bridge surface `md_link_send_reply` calls `LocalMP::SendReply`, so the joiner answers on the reply path the host reads (`LocalMP::RecvReplies`, keyed by AID) instead of the broadcast path `md_link_send_packet` reaches; the reply carries the AID from the host's association response. One more live defect fell out: replies were stamped with the drained frame's own timestamp and every one was **stale** (`stale 107`, `collected 0`) - the host's staleness window is `LocalMP::kReplyStaleWindow = 2*16715`, and stamping `rx_ts + 2*16715` puts the reply inside it. The bodies are reconstructed, not invented: a captured client's 38-byte UsernameReply (`logs/mph4b_app_4con.log.gz`, `collected a reply ... 38 byte frame`, body `04 01 07 <snippet>` + six-byte snippet) and GBATEK's RsaReply (a normal reply whose type byte is `08`, seven filler bytes, no key material). Measured live (`logs/sfc_reply5.log.gz`, `logs/sfc_rsa2.log.gz`): from `status 0000, AID C001` the host's command class advances `01 NameRequest` → `00 Dummy` (NameDone) → **`03 RSA`** (body `06 01 02 00 75 11 03 ...`, size 117 halfwords) → `00 Dummy` (RsaDone); all five username snippets are collected (`04 01 07 00`…`04`), the RSA reply (`04 01 08 …`) is collected, the joiner's counters read `our reply collected 711 times`, and the host's digest reads `collected 470 (+61/5s) ... stale 0 reanchor-dropped 0 deauth out 0` (`cargo test --lib` **27 passed, 0 failed**; `md_smoke_test` **141 checks, 0 failures**). **A regression introduced and caught live:** a `username_idx < 4` guard stopped the *fifth* username snippet from being sent, so a run sat on `NameRequest` forever after collecting snippets 0-3; the guard is gone (covered by a unit test). **The next command class is gated by a host button, not another client frame.** After RsaDone the host sends no `04 DataPacket`: its Recruitment lobby ("Searching for opponents ... Touch the Cut Off button to move on. Entrants 2/6") only begins the transfer after the host's own **Cut Off** tap - the left of the two buttons at DS `(91,178)` on the host's bottom screen. After that tap it leaves the lobby onto "Please wait a moment..." and holds at command `00` (`06 01 02 00 03 11 00 ...`) for minutes with no `04 DataPacket`. So the next thing to make work is what that RsaDone/"Please wait" state waits on, with the host past its own button. **v0.1.9cn makes the route one command and fixes the reply timing; the host still parks at RsaDone.** `tools/scripts/sfc-dp-host.sh` now walks the menus, waits for the joiner to be registered (`AID != 0`), taps Cut Off at the measured DS `(91,178)`, and polls for the transfer's first `0x04`; its exit code is the state (0 = `0x04` seen, 2 = host role but parked, 1 = no host role). **The reply-timing defect and its fix:** the joiner answered one command per step, oldest unanswered first, at ~12.5 replies/s against the host's ~166 commands/s - and usually for a class the host had already left, so all 19 RSA windows collected nothing, the host abandoned RSA, and our `RsaReply` was read against a later dummy round and ignored. It now answers the **newest** class in the drain batch (the reply path keeps only the newest answer per client, `MD_REPLY_KEEP=1`), and in `logs/sfc_final.log.gz` the host collects `04 01 08 6F 00 6E 00 44 00` *between* its `03` and `00` transitions and moves on immediately. **A second measured rule:** the name phase's `00` (NameDone) must be answered with the username - a dummy there and the host never sends RSA at all (measured twice: classes `01 -> 00`, parked), the username there and it sends `03` (measured twice: `01 -> 00 -> 03 -> 00`). **Three reply-level guesses tested and removed** (answering every command in a batch floods the single-slot answer queue and the host deauths with reason 0001; a normal dummy and a DataReply on RsaDone change nothing). **The parked state, as numbers:** the host's class stays `00`, it never writes a Data command (its CMD slot stays armed with the dummy, every command frame 54 bytes), its window collects **every** reply we send (715/715) with `stale 0 deauth out 0`, its ARM9 alternates `02030488`/`020A35A0` with `IME=0001 IE=2009 IF=0000`, and its own wifi read loop is ordinary MP polling (`W_IF`/`W_IE`, `W_TXStat`, `W_TXSlotLoc1/2/3`, `W_CMDStat0`, `W_TXReqRead`) - not a Download-Play register. The park begins before Cut Off, so the host's own game holds it, not the wire. **Memory:** the received name is in an entrant record at `0x020B4C20` and the DP title records keyed by serial `00400218` sit at `0x020B3D40`/`0x020B5640`, so the state is addressable even though none of it proved to be the trigger. **v0.1.9co stops guessing reply bodies and reads the host's code, and the reading is what the pass produced.** Two new instruments: `wifiio` now reports a **write-side recorder** - every write this console's own software makes into the low wireless RAM with the writer's PC/LR, a per-offset last-writer table, and the command-slot arm's PC/LR plus the writer's whole register file - and the **IPC FIFO word trace**, both directions, each word with the pushing PC; `MD_NOJIT=1` autostarts on the interpreter, because the JIT reports `R[15]` as a block start and every `pc=` the recorder printed before this was therefore a block address, not an instruction address. With the interpreter on, the host's command frame is at wifi-RAM offset 0 (body at slot offset `0x24`, so GBATEK's CMD_DATA class byte `[06h]` is at `0x2A`): the body's first three halfwords (reply time `0106`, slave flags `0002`, size and flags `1103`) are written by the library routine at `0x027EFE94`/`0xE9C`/`0xEA4`, and everything from `0x2A` (`00 50 08 00 02 00 02 00 02 00 B8 B6`) by one halfword copy at `0x037FE66C` reached from the `bl` at `0x027EFEC8` - **9,529 writes of that byte against 9,529 slot arms, always the same value**, so the class in the slot is fixed and no guest frame can change it. The arm is `strh r2,[r1]` at `0x027F001C` with `r1 = 0x04808090` (`W_TXSlotCmd` in the ARM7's mapping) and `r2 = 0x8000`; the register file at that instant reads `r4 = 0x04804000` (the slot base). The IPC trace is the other half: the ARM9 is *not* waiting on the wire (its `pc9 0x02030488` is the SDK idle loop, `bl scheduler; bl mcr p15,0,r0,c7,c0,4; b`, a cache drain), it is pushing **one word per command round** (~154/s against ~166 commands/s) from the single store at `0x02030660` with values stepping by `0x400` (`82D4500A`, `82D4900A`, ...), while the ARM7 pushes about three words per round back from `0x037FEA70` (`8DEB780A`, `C0240006`...). So the host's own MP scheduler is running and what it schedules is the dummy - the missing step is inside the host's Download Play code, and the previous pass's "the host is idling, waiting for a guest frame" reading is disproved by its own counters. The park reproduces in the interpreter too, so the JIT is not the cause (`logs/sfc_nojit.log.gz`, `logs/sfc_regs.log.gz`, `logs/sfc_ipc.log.gz`). A RAM diff across the Cut Off tap (two baseline dumps, then post-tap, over `0x020A0000..0x020C0000`) changes 788 words of which 652 are new, but the DP title records at `0x020B3D40`/`0x020B5640` rewrite themselves between the two baseline dumps as well, so no state variable could be separated from the churn - reported as a negative result rather than dressed up. **The gate is still not named**, which was the honest state of this entry in the host-patch era. That ending is **superseded** - v0.1.9cv opened the mode machine from the client side and v0.1.9cw carried the download to completion, both recorded in this row's Result column - and what follows here records the host-patch era rather than the current state. `cargo test --lib` **27 passed, 0 failed**; `md_smoke_test` **141 checks, 0 failures** (that era's numbers). **v0.1.9cz makes the download play, and the client now jumps into the transferred title.** The joiner used to fold each packet's payload into a byte counter and drop it, so the run ended at a log line; it now holds the image as one contiguous, packet-ordered buffer and, on completion, sends GBATEK's deauthentication (reason 3) and boots it - `NDS::SetupDownloadPlayBoot` / bridge `md_dp_boot` write the header/ARM9/ARM7 to the RSA frame's **actual** destinations (`10h`/`20h`/`30h`, plus the ARM7 temp staging copy at `2Ch` the ARM9 SHA1s), set the boot indicator at `27FFC40h` to `0002` ("booted from DS Download Play via Wifi"), reset both CPUs to the ARM Supervisor state a boot starts from, and jump to the RSA entrypoints. Measured two consoles, route exit 0, nothing set (`logs/v9sfc_dp_boot4.log`, `logs/v9sfc_dp_route4.log`): the transfer still completes (`2502 distinct packets, 1260340 of 1260340 bytes`), `dpjoiner` reads `BOOTED`, the bridge logs `booted the downloaded image (1260340 bytes) - header -> 027FFE00, ARM9 -> 02000000 (entry 02000850), ARM7 -> 02380000 (entry 02380000)`, and the client's ARM9 faults **zero** times and reaches the downloaded entry. Two live defects were found and fixed on the way (the first jump kept the game's Thumb mode and faulted at `0200081C`; the writes used the RSA frame's temp destinations and faulted the child at `02008BEC`). **v0.1.9da: the client now starts the downloaded title the way a real boot does, and stops landing in BIOS - but its screen still does not advance.** `SetupDownloadPlayBoot` was missing both ends of the emulator's own boot path (`md_load_rom` runs `Reset()` -> `SetupDirectBoot(rom)` -> `Start()`), so the child executed on top of the previous game's leftovers; that is what the *nondeterministic* faults said - the same image faulted a few hundred bytes into its entry at a different address every run (`02008BEC`, `02000A28`, `02008B98`) - while the image is provably self-consistent (the child's own header in RAM states ARM9 `0x10B324` + ARM7 `0x286B0` + header `0x160`, exactly the `1260340` bytes transferred). It now calls `NDS::Reset()` before the image is written, re-runs `SPI.GetFirmwareMem()->SetupDirectBoot()` afterwards (the firmware header and user settings live in main RAM, which the reset clears), copies the FreeBIOS ARM9 logo, and ends with `Start()`. **A second defect in v0.1.9cz's own handoff was found by reading the live log and fixed:** `step`'s booted gate sat *after* the receive block, which powers the console's radio on - so one frame after `booted the downloaded image` the joiner logged `listening for a download-play beacon` and re-attached the radio the child had just taken over. **Measured on the new path** (`logs/v9sfc_dp_boot7.log.gz`, route exit 0, nothing set): the transfer is unchanged (`2502 distinct packets, 1260340 of 1260340 bytes`), the client logs **zero** warns or errors for the whole run, and `probe --player 2` samples its ARM9 *inside the downloaded image* on 96-99 of 100 samples (`0201C7B4`..`02023F4C`) with the stack in the DTCM the child itself set up, the VBlank IRQ serviced, and an ARM7 running the game's own ARM7 code in shared WRAM. **Still open:** the picture digest is byte-identical over 20 s (`ef647ee79121ad33df5ec9337d8ee562`) while the PC moves on 98-99 of 100 samples. **v0.1.9db corrects that pass's reading of why: `dispcntA 00011108` has bits 16-17 = `01` (graphics display ON - only `11` is melonDS's display FIFO, `src/GPU.cpp:1084`), so the child was never in display-off mode and never dark.** What it is really showing, decoded glyph by glyph off the client's bottom screen (`logs/shots/sfc_wait_resume.png`), is the downloaded game's own fatal dialog: **`Communication error. / Please turn your DS off.`** (its top screen is 100% black - the 3D view never starts). The host's own bottom screen carries the same first line, so **both** games are in a network-error state, and the host's changing picture is its own animated dialog. The child's wireless init is what aborts: its radio comes up at frames 5306 and 5517 and is powered down within 4 and 1 emulated frames after it writes RF values melonDS calls `invalid channel values 0004C:0004F`, and it then never polls the receive window again (`link read_game 0`, `polls 0`) while both its CPUs stay busy (ARM9 sampled inside the 64-bit divide `0x200857C` from the tick-pacing loop `0x2023F28`; ARM7 running the game's own code, `pc7 037FEFDC`, 1,573,480 IPC words pushed). The three `unknown ARM9 IO write32 04001060/64/68` writes are ordinary boot behaviour on **both** consoles (SDK stub `0x01FF8028`), not Download-Play-specific. Two mechanisms are on the record and neither is fixed: the handoff deauthenticates (reason `0003`) and the host's game tears its own access point down (`USCOMPARECNT disabled ... sends no beacon of its own`), so the downloaded title has no host to join; and `Wifi::Reset()` leaves `CurChannel = 0`, which makes a console mute and deaf (`src/Wifi.cpp:1057`, `2738`) until the game writes a matching RF pair, which this client never does. `cargo test --lib` 37/0 (one new), `md_smoke_test` 149/0; v0.1.9db changed no code and re-ran neither. **v0.1.9dc runs that pass's own unbuilt instrument and names the exact missing step.** The `rfw:` RF-write trace in `Wifi::RFTransfer_Type3` (`src/Wifi.cpp`) printed 459 lines on a fresh two-console route (`logs/v9sfc_rfw2.log.gz`, route exit 0): the host and the booted child both write the firmware RF register block (ids `00..28`), and then the host scans channels (`4B:6C -> 1`, `4C:4A -> 7`, `4D:08 -> 13`) and settles on **channel 7**, while the child powers the radio off at the end of the block and never writes a channel pair at all (its only `id=01/id=02` writes are the block's `4C:4F`, five times, each `invalid channel values 0004C:0004F`). So `CurChannel` is never set and the downloaded title is mute/deaf (`src/Wifi.cpp:1057`/`1339`/`1523`/`2738`), and its parked state is the bring-up stopped: `pc7 0000115C` (ARM7 in the BIOS idle loop) beside `pc9 02008B60` (ARM9 near the downloaded entry). **The no-deauth handoff was also run live and the host still ends the session** - its own game deauthenticates the client (`reason 0001`) and then broadcasts `reason 0003` with `USCOMPARECNT disabled` ~5 s after the child's radio cycles - so that change alone does not save the download. The missing step is now single and named (the channel select); next is what the child's ARM7 waits on after the block load. `cargo test --lib` 37/0, `md_smoke_test` 149/0. **v0.1.9dd holds that channel on the child, and it is live and measured - and it is not enough, with the next blocker named from the trace.** `Wifi` gained one held channel (`SetDownloadPlayChannel`/`HeldChannel`, `src/Wifi.cpp`, `src/Wifi.h`): `ChangeChannel()` keeps it while the RF registers name no channel, and a child that selects a channel of its own replaces it (the fallback self-disarms; `Wifi::Reset()` clears it). The hand-off sets it once from the host's beacon (`st.host.channel`, threaded through `JoinerBus::handoff` -> `md_dp_boot(..., channel)` -> `DownloadPlayBootInfo.Channel` -> `NDS::SetupDownloadPlayBoot`). Live (`logs/v9dpchan.log.gz`, route exit 0, nothing set): the boot logs `wifi: rf registers 00000:00000 name no channel; holding channel 7` and `radio tuned to channel 7`, every later RF write reads `rf registers 0004C:0004F name no channel; holding channel 7`, and the child's `rfw:` trace shows `chan=7` where v0.1.9dc had `chan=0` - so the mute/deaf condition is gone. **The link still does not form, and the reason is located.** The child never arms its radio: one RF block per power-up then `WIFI: OFF`, twice, no TX slot ever armed (`cmds tx 0 / collected 0`), `game 0` frames delivered to it, its picture frozen ~350 frames after boot while its ARM9 runs (pc9 `0201C7FC`, `lr9 0201C7F8`, IPC 9sends 103 / 9recvs 29904) and its ARM7 alternates BIOS idle `0000115C` with its own WRAM `038006A0`. The host is not the blocker once the transfer ends (it went `04 -> 00`, kept beaconing `fc=0080` with `link mask 0x0`, then deauthed `0001`/`0003`). **The next blocker is in the joiner, not the radio:** `dp_joiner.rs` `step()` logs `the download is complete`, calls `handoff()` and `return`s *before* the send path, so the GotAllReply for that round is never sent; after it every host round reads `collected=0000` and the host's class census over the run is `00`/`01`/`03`/`04` only - **`05` Final never** - so the child boots into a Download Play transfer that never closed, whereas a real client boots after Final and continues the association (BSSID/AID/role) the firmware's client negotiated. `cargo test --lib` 37/0 (the hand-off test now also asserts the child keeps channel 7), `md_smoke_test` 149/0. **v0.1.9de answers the completion round - and the host still never sends `05` Final, so that naming was wrong and the child now never boots.** In `dp_joiner.rs` `step()`, the branch that logs `the download is complete` called `handoff()` and `return`ed *before* the send path, so its GotAllReply never left; it now only logs, the round is answered like any other, and a `final_replied` flag plus `image.complete()` gates the boot at the end of the associated phase. Live (`logs/v9dpfinal.log.gz`, route exit 0, nothing set): the host **collects** the completion's answer - `collected a reply from p2 ... body[14]=04 01 0A C6 09 C6 09 00 00 00` (`0A` GotAllReply, next/count 2502 against the RSA total) - where v0.1.9dd read `collected=0000` on every round after completion (this run: 78,332 rounds answered, 14,167 collected). **But the class census is still `00` 59,333 / `01` 36 / `03` 5 / `04` 5,980, no `05`**: the v0.1.9dd premise is falsified. What the host does instead (read live, no pokes): `*(0x020B3DBC)=023722E0`; client-1 mode `0x023737C8` = **7** (was 6, the download direction; sampled 4x over ~25s, stays 7); gate halfword `0x02373A34` = `0000 0005`; class stays `00` with slave 1 registered, 1,013 beacons, 0 deauths. The joiner reports `not booted`. So a `05` Final cannot be this title's boot trigger; the boot must be derived from the host's own completion state (mode 6 -> 7 / class `04` -> `00` after it collects the GotAllReply), and that transition must be tested for the deadlock reading (the host polls a registered client it never tears down - "I have finished sending, now you"). One real cost reported: with the boot no longer firing on completion the joiner keeps answering for as long as the host polls. `cargo test --lib` 37/0 (the hand-off test now asserts the completing round is answered with the GotAllReply and is *not* the boot trigger, and that the Final is confirmed before the hand-over), `md_smoke_test` 149/0. **v0.1.9df resolves the reading and rebuilds the trigger from what this host emits, and the child boots again.** The two readings were settled by measurement both ways - the answering arm of v0.1.9de stopped the host's `04` within ~2 rounds of the collected GotAllReply, while the silent arm of v0.1.9dd left it re-sending its last packet forever (`collected=0000` round after round) - so the host stops asking for data *because* it collected the completion answer ("I have finished sending, now you"), its class-00 Dummy polls are its own "nothing pending", and a boot gated on a `05` Final the host never sends deadlocks by construction. The trigger is now the host's own two statements: a CMD ACK that stopped reporting this slave missing while the image was complete, *and* its newest command no longer a `04` - neither alone boots. Maybe more importantly, the transfer became a named state machine (`Transfer::{Unstated, Streaming, Complete, Collected, Closed}`), advanced in one place, reset in one place (the deauth arm - the old flag conjunction had no reset, so a re-registered client could boot on the previous session's facts), logged once per transition, and readable off the control channel (`dpjoiner` -> `transfer`). Live: `logs/v9dptrig.log.gz` boots the 1260340 byte image at line 70659 with `holding channel 7`; `logs/v9dpstate3.log.gz` shows `Streaming` -> `Complete` -> `Collected` -> `Closed` then `booted the downloaded image ... channel 7`, with `transfer = "Closed ..."`, `booted = true` off the port. Transfer unchanged (2502 packets, 1260340 bytes, no `05` in the census). The child still does not link: it boots but never arms its radio (pc7 `0000115C` BIOS idle, `cmds tx 0`), the same bring-up stop v0.1.9dc/dd recorded. `cargo test --lib` 39/0, `md_smoke_test` 149/0. **v0.1.9dg carries the negotiated session across the boot as one named fact, live and exact - and it is shown insufficient, with the next blocking step named.** A real DS client does not merely boot after a Download Play transfer: it continues the association its firmware already negotiated (BSSID, AID, role, channel), and this fork's boot is a whole-console `NDS::Reset()` that clears every one of them. The session is now one value (`dp_joiner::Session { channel, bssid, aid }`), threaded through `JoinerBus::handoff` -> `md_dp_boot(..., MDDPSession)` -> `DownloadPlayBootInfo.HostBSSID`/`AID` -> a new `Wifi::SetDownloadPlaySession(channel, bssid, aid)`, which re-seeds the core's association outright (`IsMP`/`IsMPClient`/`MPD.role=2`/`MPMYAID`/`W_BSSID0..2`) alongside the v0.1.9dd held channel; `Reset()` clears all three holds. Live (`logs/v9dpsession.log.gz`, route exit 0, nothing set): line 70329 `wifi: download-play session carried across the boot: channel 7, bssid 00:09:BF:11:22:33, aid 1`, 70330 `coming up an MP client of 00:09:BF:11:22:33 as AID 1`, and the channel hold reads `holding channel 7` on every later RF write. **And the consoles still do not link.** ~6 minutes after boot, `probe --player 2` reads pc9 `0201CD34` / pc7 `0000115C` (BIOS idle) with `mp: cmds tx 0 acks None collected 0`; `status` reads `link mask 0x0` (the child is not on the bus); `wifiio` reads `role=2 (client)` but `handed start=0000 len=0`, USCOUNT 0, RX buffer all zero; the app's own line reads `health p2: LINK OFF ... slots 0x0 ... pwr 0200/0002` and `mp p2 [guest]: cmds rx 0 ... replies sent 0 collected 0`. The host's beacons still flow (1,731 beacon lines after the boot) but not one frame reaches the child (p2 rx frames = 0 for the whole run) and the child sends nothing. **Reading: the obstacle is the child's own ARM7 bring-up, not the session state** - the session is genuinely present and the host is still transmitting, yet the child's game powers the radio on and off twice more after boot (`WIFI: ON` x2), loads one RF block, never selects a channel or arms a TX slot, and leaves the ARM7 in the BIOS idle loop. **Named next blocking step:** what the booted child's ARM7 waits on after its RF block load - at `pc7 0000115C` beside `pc9 0201CD34` - that keeps it from scanning, attaching and arming a TX slot (its own DP client init waiting on an ARM9/ARM7 handshake or IRQ its boot never raises; the emulator attachment lifetime, which ends at the hand-off while the child's game never reaches `MP_Begin`; or its power sequence aborting before the channel select). `cargo test --lib` 39/0 (the hand-off test now also asserts `session.channel == 7`, `session.bssid == HOST`, `session.aid == 1`), `md_smoke_test` 149/0. **v0.1.9dh diffs the booted child's radio bring-up against a normal joining client's and isolates the skipped step with a captured trace - no code changed.** Both consoles were sampled live in one run (`logs/v9dgdiff.log.gz`, `logs/v9dgdiff_wifiio.txt`, route exit 0, nothing set): the host, a console running its own game in multiplayer, lives in the wifi IRQ/receive loop (`W_IF` 50,017 reads, `W_IE` 41,332, `W_USCount0` 18,618, `W_TXStat` 17,556, `W_CMDStat0` 11,341, `W_TXSlotLoc1/2/3` 17,555 each), while the booted child reads **none of them** - zero reads of every one - and instead reads only the baseband/RF bring-up (`W_BBBusy` 2,253, `W_BBRead` 1,018, `W_RFBusy` 123, `W_ModeWEP` 11, `W_Random`/`W_Preamble` 6 each, `W_MACAddr`/`W_BSSID`/`W_AIDFull` 5 each) with those totals **frozen** across two samples 12 s apart. Its read ring ends on a spin on the baseband busy flag (`15E:0000` x337 with the transceiver off, last `W_BBRead` `003F`). The sharpest fact: `TRX power ON` fires **x4 on the host and x0 on the child** - the child never asks its transceiver to come up at all, so there is no channel select, no beacon arm and no MP loop, while the host's *same* firmware RF block leads on to a scan (channels 1/7/13, settling on 7) and a beacon arm. **Two of the three candidates are settled:** (2) is falsified - the child does reach `MP_Begin` (`WIFI: ON` -> `wireless ON (begin #2, slots now 0x3)`, twice) - and (3) is refuted as stated (the sequence never requests power-on rather than aborting). What remains is (1) narrowed: the transition from baseband/RF init into the multiplayer loop in the child's own Download Play client driver, inside the downloaded game's code. A secondary risk is recorded, not claimed: the child reads no association register post-boot, so the trace cannot say whether the carried BSSID/AID survive its own init - and `W_ModeReset` bit 14 (`0x4000`) is a core branch that would zero exactly them plus `MPMYAID`. `cargo test --lib` 39/0, `md_smoke_test` 149/0 (re-run, unchanged tree). **v0.1.9dk settles that secondary `W_ModeReset` risk and locates the stop with the power-register trace live for the first time** (`TracePowerWrite` had never compiled before v0.1.9dj, so every earlier run's log has zero `[mp] power reg` lines; two fresh routes carry 479 each - `logs/v9carry.log.gz`, `logs/v9code.log.gz`, both exit 0 and `BOOTED`). The child writes `W_ModeReset` **zero** times in the whole run, so the bit-14 (`0x4000`) branch that zeroes `W_MACAddr0..2`/`W_BSSID0..2`/`W_AIDLow`/`W_AIDFull`/`MPMYAID` never fires on it - and it would not have mattered, because the child walks those registers through its own bus-test patterns and then writes its own MAC and `W_ModeWEP`, replacing the carried association itself. The host's ladder - `W_ModeWEP <- 004A`, `W_PowerForce <- 8001,0000`, `W_PowerTX <- 0003`, `W_ModeReset <- 0001`, `W_PowerDownCtrl <- 0000`, `W_PowerTX <- 0001`, `W_PowerState <- 0002`, `W_PowerForce <- 8000,0000`, `W_ModeReset <- 0000` - is exactly what raises `TRX power ON`; the child never writes `W_PowerState`/`W_PowerForce`/`W_PowerTX`/`W_PowerDownCtrl`/`W_ModeReset` at all, power-cycles its block three times (`wireless on/off 3/3`) and gives up, then its game shows its own `Communication error. / Please turn your DS off.` (zero reads of `W_IF`/`W_IE`/`W_USCount`/`W_TXStat`, RX buffer `0000/0000`, `USCOUNT=0`). A prior reading is also corrected: `pc7 0000115C / lr7 037F84FC` is not the child's retry loop - `0x037F852C` thunks into `0x038043E2`, Thumb `svc 6`, the first entry of the image's own one-instruction-per-SWI stub table, and the **host** sits in the same loop in the same run. The child is alive in its own main loop (pc9 moves through `0201C790`/`0201C7FC`/`0201CD34`/`02023F2E`, IPC `7sends 20647`), not faulting, and **no emulator defect is substantiated** (busy flags toggle, the baseband read path returns the fixed chip id `0x006D`, the channel hold and carried registers are present and written by the child), so no fix was made. Next: a read/store watch on the child's wifi block across its three power cycles. `cmake --build build-tauri -j8` exit 0, `cargo test --lib` 39/0, `md_smoke_test` 149/0, transfer unchanged (2502 packets / 1260340 bytes). `logs/v9carry_finding.txt` digests both runs. **v0.1.9dl then made the block PC-tagged** (`Wifi::RecordRegAccess` -> `appctl wifiio`), which corrected one reading and located the stop in the child's own code: the change-only `power reg` trace had hidden a write of a value a register already held, so the child's `W_ModeReset <- 0000` (`pc7 037F93F0`) and `W_PowerForce <- 0000` (`pc7 027EA4E8 lr7 037F94C4`) were on record after all - bit0 is still never raised. Disassembling the live-dumped child ARM7 shows its register table loader (`0x027E9A1C`, 25 pairs, one `W_ModeReset = 0`), its power state machine (`0x037F9468`: state 0 parks `W_BBPower <- 800D`/`W_PowerUS <- 1`; state 16 powers up via `0x037F93B0` + the wake `0x027EA3D4`), and `0x027EA66C` - the routine that would force the transceiver (`W_PowerForce <- 8001`, poll `W_PowerState >> 8 == 2`) - which the child never runs. It runs state 16, then state 0, then its ARM7 idles (`pc7 0000115C`) with the register history frozen and `TRX power ON` 0 against the host's 8. The trace carries every read with its PC and the child reads **no power register at all**, so no emulated value is behind the stop. The one candidate - carrying `W_ModeReset` bit0 across the boot - was built and run: the child's own table loader writes it back to 0, `TRX power ON` still never fires, the screen digest does not move; reverted. `md_smoke_test` 152/0 (two new section checks). `logs/v9reg_finding.txt` digests the pass. **v0.1.9dm watched the child's ARM9 instead, and demotes "the stop is in the child's own MP bring-up" to a lead.** New `pctrace` records the first visit to each PC in a window for one CPU, in order, with LR and frame; under the JIT (the real config) the booted child records 8303 distinct block entries across 165 code regions and then stops growing, and sixteen pc9 samples over 33 s cycle through `0201C7FC`, `0201C790`, `0201CD34`, `02023F2E` with `mp cmds tx 0` - a steady in-image loop, not a single give-up branch. One site is named from the child's own image: `0x0201CA70` reads the DP boot indicator `0x027FFC40` and, when it reads 2 (exactly what `SetupDownloadPlayBoot` writes), calls an IRQ-off self-loop at `0x0201CD34`; the JIT's block-address `R[15]` localizes the child to that loop but does not prove it spins there, and `MD_NOJIT=1` (a confound) parks the child at a different address (`02008B60`, inside the 64-bit divide `0x02008B40..0x02008B7C`). The record's `W_BBBusy`-hard-wired-to-0 claim is corrected (`src/Wifi.cpp:4047`/`:4440`). `md_smoke_test` 159/0. `logs/v9arm_finding.txt` digests the pass. **v0.1.9dn makes that trace truthful in the shipped JIT config and disproves the site it named.** The recorded PC is the address of the instruction about to run in every mode (`instrAddr = R[15] - ((CPSR & 0x20) ? 2 : 4)`, the same value the JIT dispatches on), so under the JIT it records whole basic-block entries; what was untrustworthy is `probe`'s *raw* `R[15]`, which is 4 above the running instruction, so the previous pass named code 4 bytes off. Arming is race-free (request published from the control thread, applied by the CPU thread at the top of `RunFrame`; fixed-size arrays only). Measured in the run that ships (JIT on, `logs/v9jit2.log.gz`, route exit 0, `BOOTED`, transfer byte-identical 2502 packets / 1260340 bytes, 0 warns): child `0x027FFC40 = 2` (host `= 1`), so the test at `0x0201CA70` would divert - but tight windows over 366 frames each record **0 visits** to `[0x0201CD34,0x0201CD48)` and `[0x0201CA70,0x0201CA8C)`, so the child's ARM9 never executes either after the boot. Its real steady cycle is `0x0201C780`->`0x0201C78C`->`0x0201C7F8` (a pacing loop) plus the `0x0201C9D8`/`0x0201C9EC` IRQ helpers, with IRQs serviced (`IE=2009 IME=0001 IF=0000`); the old raw `pc9 0x0201CD34` is block entry `0x0201CD30`, the `bx lr` return from the cache-drain helper `0x0201CD28`. The stop is not this ARM9 site; ARM7 stays in BIOS idle and `mp cmds tx 0`. `md_smoke_test` 159/0, `cargo test --lib` 39/0; `logs/v9jit2_finding.txt` digests the pass. **v0.1.9dp: the prescribed fix for that dead receive path is falsified by measurement, and the bring-up stop is one branch wide.** `Wifi::CheckRX` (`src/Wifi.cpp:2756`) refuses every frame while `W_RXBufBegin == W_RXBufEnd` (and `W_RXCnt` bit 15 clear of RX-off), and that is faithful - hardware cannot latch into an unconfigured window either - but the booted child's window reads `begin=0000 end=0000` in **26 of 26 samples spanning before as well as after its boot** (`role=0` pre-boot, `role=2 (client)` post-boot, `logs/audit9_p2_regs.txt`), against the running host's live `role=1 · USCOUNT=3510FC8 · USCOMPARE=2383800 · zone gen=5471 · handed start=1638 len=72 fc=0080`. So the console that received the 1,260,340-byte download was never this console (the app's joiner took it at the bus level; the child's own ledger reads `game 0 drain 14314` all run): there is no pre-boot configuration to carry across the boot, a bus re-attach cannot re-arm what the game never armed, and the `delivered`-climbing-with-`game 0` shape is expected rather than the defect. Both consoles run the same sequence from the same PCs and the same values up to `pc7=027EA55C` (`W_ModeWEP <- 004A`, after the `003F/1A/25/34/00` ramp, `W_PowerUS 0000`, `W_PowerTX 0002`, `W_RFCnt 0114`, `W_ModeWEP 0002/000A`, `W_BBPower 800D`, byte-identical RF read sweeps); the host then enters the transceiver-on sequence (`W_PowerForce <- 8001`, `W_PowerForce <- 0000`, `W_PowerTX <- 0003`, `W_ModeReset <- 0001`) and its game is handed a frame through `CheckRX`, while the child writes **neither `W_PowerForce` nor `W_ModeReset` at any point**, ends on `W_BBPower <- 800D` / `W_PowerUS <- 0001` (block off), parks in BIOS idle (`pc7 0000115C`, `lr7 037F84FC`) and raises no wifi interrupt at all (`raised none · W_IF 0000 W_IE 0000 · bcn 0 0 · uscnt 0`). No code changed this pass; the branch between `pc7=027EA55C` and `pc7=027EA6AC` is the next thing to read (the `0x038043E3` thunk's return in the ARM7 retry loop, and the ARM9's `0x02087350` callee). Cold build exit 0, route exit 0 with `TRANSFER STARTED` and the child booted, all five `Test Roms/*.sav` byte-identical, app exit status 0, no strays; `logs/audit9_finding.txt` digests the measurement. **v0.1.9dq: the fork's client side is a Rust stand-in for the console's own radio, and arming the console's block from outside is measured insufficient.** Upstream melonDS has no Download Play path at all - the only related line in `upstream/master` (906e9ebb) is the *direct* boot indicator (`upstream:src/NDS.cpp:329`, `0x027FFC40 <- 0001`), and `Wifi::CheckRX` plus its `MP_RecvPacket`/`MP_RecvHostPacket` receive path are upstream's own (`upstream:src/Wifi.cpp:1564/1587/1593`); the fork's own additions are `src-tauri/src/dp_joiner.rs`, `NDS::SetupDownloadPlayBoot` (`src/NDS.cpp:439`) and `Wifi::SetDownloadPlaySession` (`src/Wifi.cpp:3340`), which together implement the *client* outside the console. The same run shows both sides: the host, whose own game brought its radio up, has `W_RXCnt 8000`, `W_PowerUS 0000`, window `50C4/5F60`, wifi clock `23D5D98`, `bcn 204 8`, `cpu line 20682` and frames handed to its game, while the child reads `W_RXCnt 0000`, `W_PowerUS 0001` (off), window `0000/0000`, `us 0/0`, `bcn 0 0`, `cpu line 0`, `W_IF 0000`. Poking the host's own block into the child's registers took (enable and window visible, `LINK OFF` -> `LINK IDLE`) and changed nothing in the ledger - `game 0` for 60 s, `rx 14358 drain 14358 handed len=0` - because `CheckRX`'s first guard is `W_PowerState` bit 9 (`src/Wifi.cpp:2758`), a guest-driven power state the child's driver never clears (the host writes `W_PowerState <- 0002`; the child never writes it, nor `W_PowerForce`, nor `W_ModeReset`, and parks with pc7 `0000115C`). **So the client side has to be the console's own guest code**: run the child console into its own Download Play path (the firmware applet or the game's own DP client) rather than autostarting a cartridge and having `dp_joiner.rs` speak for its radio; then `md_dp_boot`/`SetupDownloadPlayBoot`/`SetDownloadPlaySession` become unnecessary. That replacement is a real guest DP client and is not half-landed. Cold build exit 0, route exit 0 with the transfer complete and the child booted, five saves byte-identical, no strays; `logs/audit10_finding.txt` digests the comparison. |
| Super Mario 64 DS (USA) (Rev 1) | 2 | 🟡 transfer completes and menus route to CONTINUE, no battle yet | VS mode **is** single-card Download Play, and the title is now registered (`ASME`, `download_play`), so the joiner runs for it. With the new route (`tools/scripts/sm64-dp-host.sh`) the host screen is reached by touch geometry alone - the game's font defeats the ASCII readers, so the coordinates were measured off framebuffer captures (title screen, then the **VS** bar at about `(37,172)`; do not tap the bottom bar, which is the host's own "TOUCH TO START" and asks "Practice alone?") - and the joiner hears the beacon, authenticates, associates (`status 0000, AID C001`) and the host runs `NameRequest -> 00 -> RSA -> 00`, stating a **514896-byte** image. Then it stops addressing the client: 52,480 of its transmitted command frames are the 6-byte-body, no-client form (len 46) that `classify` refuses to read as a command, against 2,536 it reads, so `download: 0 of 514896 bytes` and never a `04` - and the reply-type overrides could not reach that round on the build the sweep ran on, because the `00 if !saw_rsa` arm of `answer_reply` matches before the forced arm (a symptom of the batching defect below, not a permanent property). **Driving it found and fixed a real client defect** (an RSA arriving in the same drain batch as the round after it was *read* but never *remembered*, so every later round was answered as a Username reply instead of the RsaReply); Star Fox's complete transfer re-runs on the fixed binary. **v0.1.9cx names why it stops addressing the client, and it is not the reply.** Re-run on the fixed binary, the same route still parks (`logs/v9sm64b.log.gz`): the host sends `NameRequest -> 00 -> RSA -> 00` to slave 1 and then opens a reply window on **2,535 addressed rounds** of which **31 are collected, 2,522 are empty and 278 reads are voided as stale**, its own CMD ACK reporting this client missing on 2,522 of them (`clientfail 2522`, `collected 31`), and after ~2,424 addressed rounds it evicts the client to the 6-byte-body no-client form (`60,055` frames) - `download: 0 of 514896 bytes`, never a `04`. **The reply's contents are not the lever and never were:** the five-way `MD_DP_REPLY_FORCE` sweep (`09`, `0a`, `0b`, `07`, `00`; `logs/sm64_force_sweep.txt`) parks on every value, `MD_DP_REPLY_FORCE=09` with the window trace shows the host *collecting* 21 type-`09` replies and still dropping the client, and the host collects 23 RsaReplies (type `08`) before it ever evicts - so no type, and no body, moves it. **The lever is when the answers arrive, and that is a client/transport deficit, not a host branch.** Each command round is answered from a drain of the console's inbox, and a drain and a send are one round trip each to the console's driver thread (`service_link_ops` runs once per emulated frame) - so the client answers about **one round per emulated frame while the host asks ~8.6** (2,424 addressed rounds over a ~4.9 s emulated span). Answering *every* round of the batch through a batched send was implemented, built and measured this pass (`logs/v9sm64b.log.gz`): the answers do reach the host (`reads 147 -> 309`, `reanchor-dropped` from ~0 to 2,367 as the host's re-anchor trims the surplus) but the round its window reads is still its own - `collected 30 -> 31`, and **278 of the 309 reads are voided as older than the transport's two-frame window**, so the change was reverted rather than kept as unproven churn. `MD_REPLY_BUDGET_FRAMES=20` is worse still (`collected 0`, `logs/v9sm64c.log.gz`). So the host is behaving correctly for a client it cannot hear, and the way it hears a client is the emulator's reply window, which is already spent (2,204 of the rounds open with `0us of slack`) by the time each round arrives. **Next input:** the answer has to be *already in the host's queue* when its window opens, i.e. the client has to answer ahead of the round rather than once per frame behind it - a drain/send path that does not cost a frame, or a reply window that waits for answers already on their way. **Nothing was poked:** no value of the reply moved this host, so there is no single host branch to hold - the earlier verdict's shape does not hold here. **No regression:** Star Fox's transfer was re-run end to end on the rebuilt binary (`logs/v9sfc.log.gz`, 2,502 distinct packets, 1,260,340 of 1,260,340 bytes). **v0.1.9cy fixes the delivery path, and the host is then heard on every round - and still does not start the data loop, which is the one remaining fact.** The joiner's turn now runs *inside the console's own driver turn*: `JoinerBus::turn` is the single entry point (the run loop owns only *when*), a real console overrides it with one `LinkOp::JoinerTurn` that drains the inbox, advances the client state machine and sends every answer in that same turn, and the whole batch of a drain is answered because those sends are direct calls inside the turn rather than one driver round trip each (`DriverTurnBus`). Effect, same no-override no-poke route (`logs/v9sm64d.log.gz`): **`stale` goes 278 -> 0, `collected` 31 -> 1711, and the eviction disappears entirely - zero 6-byte-body no-client frames where the previous build had 60,055**; the host keeps addressing slave 1 (`slave flags 0002`) for the whole run instead of dropping the client after ~2,424 rounds. The remaining starvation is the transport's retention, and it is now measured rather than guessed: with `MD_REPLY_KEEP=16` (a frame's worth of the ~8.6 rounds this host asks per frame; the shipped default is 1) the host collects **26,968 of 26,977 rounds, `empty 9`, `clientfail 9`, `stale 0`** (`logs/v9sm64e.log.gz`) - the client is heard on essentially every round. **And the host still stays in `00 (Dummy)` at RsaDone forever: no `04`, `download: 0 of 514896 bytes`.** With collection at ~100% the reply-type sweep is finally meaningful, and it now says the same thing with a healthy path as it did with a starved one: `MD_DP_REPLY_FORCE=00` and `=09` each park with the client collected on ~15,700 of ~15,800 rounds, `stale 0`, no deauth (`logs/v9sm64f00.log.gz`, `logs/v9sm64f09.log.gz`). So the reply path, the reply type, the reply body and the reply's freshness are all ruled out **by measurement on a client the host can hear**, and the gate that starts the download lives in the host's own engine and is not reachable from anything this client sends. The frame-sized retention is the transport's next lever and is deliberately **not** made the default here: it is a cross-title change (MKDS/NSMB/MPH answer without any joiner) that this pass did not re-validate, and on this title it does not by itself start the download. **No regression:** Star Fox re-ran end to end on the restructured binary (`logs/v9sfc2.log.gz`: `#0`, 352 bytes, `"STARFOX "`, then `2502 distinct packets, 1260340 of the 1260340 bytes`). **2026-10-14: the v0.1.9cy verdict is overturned — the download gate was a host-side tap, and the transfer now completes.** Tapping the waiting room's TOUCH TO START bar DS `(128,168)` with slave 1 registered moves the host `00 -> 04` and streams `514896 of 514896 bytes in 1023 distinct packets · BOOTED` (`logs/sm64_app.log`, `logs/sm64b_app.log`, `logs/sm64d_app.log`; 3/3 transfers, script `tools/scripts/sm64-dp-host.sh` exit 0 — it needs `SETTLE_AFTER_ASSOC_S=15` because a same-second START tap is swallowed while the host's roster fills). Post-boot menu route mapped by digest-gated taps (`tools/scripts/sm64-dp-launch.sh`): 4-panel character picks P1 `(34,68)` / P2 `(98,68)`, D-pad stage cycle, guest headline tap `(128,60)` after ~60 s settle to the hub (known digest `fbdc62…`), hub boxes OCR-named `CONTINUE` (128,72) vs `QUIT VS` (128,120). Three sessions tapped QUIT and tore down identically (2-button YES=LEFT/NO=RIGHT digest-proven; converged session-over notice; radios off host-first, no deauth; guest ends on the firmware's `DS Download Play Failed!` dialog, OCR-verified `logs/shots/sm64c_end.png`). CONTINUE into the battle is mapped but reached only in runs that then lost the attach lottery — guest attach is 2/4 on this title (runs 4–5: rx frozen 1.5–2.7k, host TX stops ~30 s post-boot, the p65–p67 shape), so no battle has started yet. The launch route now gates the attach (guest rx climbing) and the hub (digest) and refuses untapped on a miss (run 5: exit 1 in 100 s, zero taps). |
| Tetris DS (USA) | 2 (card, and Download Play), 4 (card room reached) | ✅ **two cart consoles play a live STANDARD match on demand**: the route's own `--start` now presses A with every press proven landed by that console's own panel, and two cold starts today ran 5,519 and 1,079 match-family commands with both boards live on screen (`replay-after.png`, `driver2p-after.png`); single-card Download Play still boots at 2; 🟡 3 carts reach the title's own three-player table with all three players listed, every asked client credited in its own round (`clientfail` **0/560** with the reply rendezvous, against 9.5% before it), and the room still ends: the guests' own games write `reason 0003` about 34 emulated seconds in, with the link's ledger clean at that instant - so the leave is the title's decision, not a reply the transport lost. **2026-10-23 (v0.1.9em):** four consoles now hold all four guests in the room with **0/721 failed acks**, at a measured cost - the room runs at **14 fps** with the rendezvous on (unchanged by a 1-frame budget) against **39-42 fps** with it off, where 85/622 acks (13.7%) fail and the room collapses to the menus | First Download Play title to reach real gameplay: the guest boots the downloaded image, joins the host's STANDARD lobby as player 2, and both consoles play a live versus match (distinct boards, mask 0x3). **The route's room, read off the games' own screens (2026-10-16): the game's *card* room, which also serves DS Download Play guests** - its own page says so ("To host a game, tap Invite. To join a game already being hosted, tap Join. Players without a Tetris DS Game Card can get in on the action by using DS Download Play."), INVITE opens a mode list (STANDARD, MISSION…) with no Download Play branding, and a **cart** console's own join browser lists that room (host `melonDS`, `STANDARD`, player count rising with each cart guest) and enters it ("Accepting players now. Please wait a moment."). All four consoles reach the room with three measured inputs per guest (`tools/scripts/tetris-4p.sh`), the host's game assigns AIDs 1-3 to three simultaneous guests, and the host's own room screen lists them under a `NAME` column. **What fails is the start press, at 3 and at 4 cart consoles as much as at 2:** the host sends one packet of the **download image** to the room (a `04` DataPacket, packet `#0`, 250 bytes starting `NINTENDO` - byte-for-byte the packet the working two-console Download Play route's client boots from; at four consoles a `05` Final plus the host's own commands), each guest's own game answers it once and then stops receiving, writes `deauth sent … reason 0003` and lands on its own `COMMUNICATION ERROR`, and the host follows it there; the host's reply path was never starved (13,961 commands / 13,961 acks / `clientfail` 2.4 % at three, 25,863 / 25,863 / 2.6 % at four). Without a start press each guest's own game gives up **32-47 s after its own association** and later guests no longer evict earlier ones (the phantom-client eviction is gone with the gate). The two-console Download Play route re-runs on the same binary to a **booted image** (`1004732/1004732 in 4021 packets · BOOTED`), with one precondition now on record: the client console's own game must be left alone. See the Tetris DS section |
| LEGO Star Wars II - The Original Trilogy (USA) (Rev 1) | — | ⬜ none | The game has no multiplayer mode to link |
| 0595 - Club House Games (U)(WRG) | 1 | 🟡 boots, multiplayer untested | Title screen renders ("Balance", touch prompt); no menu route driven, no link attempted |
| Bomberman (USA) | 2 | 🟡 transfer completes 2/2, guest errors post-boot, no battle | Registered (`ABME`, `download_play`); Battle leads straight to a Download host lobby, the guest associates (`AID C001`), the roster lists both players, host Start streams `672764/672764 bytes in 2693 packets · BOOTED` — then both consoles show `Communication error`: the booted guest game never gets on the radio (`answered 0`, stand-in covers), the host's `06` rounds go unanswered and it quits. Systematic (2/2 identical), not the attach lottery. See the Bomberman section |
| Diddy Kong Racing DS (USA) | 1 | 🟡 boots, multiplayer untested | Title/start menu renders; no route driven. **Four consoles (2026-10-22, v0.1.9dw): all four boot to the save wheel and START/A reaches the game-type wheel whose label reads `SINGLE PLAYER` (or `OPTIONS` after one RIGHT), but neither the D-pad, L, nor taps on the wheel's rim rotate it past those two, so the multiplayer category was not reached** (`logs/shots/dkr_00..08.png`, `logs/dkr4_app.log`) |
| Kirby Super Star Ultra (USA) | 1 | 🟡 boots, multiplayer untested | Intro cutscene plays; no route driven |
| Mario Hoops 3 on 3 (USA) | 1 | 🟡 boots, multiplayer untested | Title menu renders (EXHIBITION etc.); no route driven |
| Mario Party DS (USA) (Rev 2) | 2 | 🟡 Download Play transfer completes, guest stalls post-boot, no minigame | DP-only title (no multi-card mode), so the joiner is the only route. Host reaches its lobby, the joiner authenticates (`0000`), associates (`AID C001`) and the **transfer completes**: `623160 of 623160 bytes in 2494 distinct packets · BOOTED` with no host start press needed. Post-boot the booted guest never rejoins the radio (its own `begin #2` then `end #2` 5 s later) and both games freeze on static screens — the host's game is alive (`cmds tx 10432` climbing, IPC flowing) but ignores every button and touch. Same class as Bomberman: a scan-first guest, no served scan results and no staged wifi regs. See the title's section |
| Meteos (USA) | 1 | 🟡 boots, multiplayer untested | Main menu renders; 2-console session constructs with the joiner correctly disarmed (`logs/dpnew_Meteos_USA.log`). Submenu unopened. **The earlier "WIRELESS button at bottom-left (60,108)" is wrong for this build (v0.1.9dw): that coordinate selects STAR TRIP** (its message line becomes "Defeat planet Meteos! Reach the end!"). Four consoles (2026-10-22): all four show the intro then the same main menu - SIMPLE, STAR TRIP, TIME WAR, DELUGE, TUTOR - and SIMPLE opens a single-player rules page (rules, stock, difficulty, CPU level, team); no wireless entry is drawn on that page, so no lobby was reached (`logs/shots/met_00..03.png`, `metb_00..01.png`, `logs/meteos4_app.log`, `logs/meteos4b_app.log`) |
| `tools/mkdiagrom/diag.nds` (test ROM, not a game) | 2–4 | ✅ | — (the link self-test the build is verified against) |

### Test-file inventory (v0.1.9br, 2026-10-01)

Every ROM the repo ships plus the diag ROM, run this pass through the app or the
harness that owns it. The point of the table is *how long each held* and where it
broke, not a re-derivation of the sections below.

| Test file | Driver | How long it held | Where it broke | Link counters in that run |
| --- | --- | --- | --- | --- |
| `New Super Mario Bros. (USA).nds` | `tools/scripts/nsmb-route.sh` (harness, 14000 frames) | **the whole run**: 234.0 emulated s, 11,127 rounds carrying a 262-byte payload body to the last one, `all consoles kept running` | nothing — no deauth on either console, `slots on the bus 0x003` to the end | host `cmds tx 11127 / collected 11126 / empty 1 / clientfail 1 / stale 0 / reanchor-drop 0`; guest `cmds rx 11127 / answers 11127`; `logs/v9aj_nsmb_stdout.txt` |
| `Metroid Prime - Hunters (USA) (Rev 1).nds` | `tools/scripts/mph-match.sh` (app, 2 consoles) | **~78 s of `INGAME`** (ROSTER -> HUNTER -> INGAME 12 s -> DIALOG -> INGAME 66 s -> DIALOG) | the host's game stops attaching its session payload, guest deauth `reason 0003` 4 s later, host's broadcast deauth 1 s after that, both radios off — the recorded shape | host `collected 5699 == acks 5699 / empty 0 / clientfail 0` in the dying second; cumulative `5772 / 5774` (the one loss is the host's poll *after* the guest left); `logs/v9ah_*` |
| `0168 - Mario Kart DS (U)(SCZ).nds` | `tools/mkds_join.py --restart --no-dialogs --cut-off --vs --watch 25` (app, 2 consoles) | coupled `link mask 0x3` from frame 4035 to 5729 (~28 s), reached CUT OFF | the VS step found no game-select screen (`the VS tap moved nothing on P1's top screen`), then the mask collapsed to `0x0` with both framebuffers one flat colour (`45d15630`) — the recorded park, no race | `collected 6152 / acks 6182`, `empty 27`, `clientfail 27`, `reanchor-dropped 26`, `stale 0`; the 27 are spread across the coupled seconds (~0.44 %), each a round whose window closed before its answer arrived, and **every window that dropped still collected its reply (24 of 24, v0.1.9bs)**; `logs/v9ai_mkds_*` |
| `Star Fox Command (USA).nds` | none (boot sweep, harness, 2 consoles / 2400 frames) | 40.1 emulated s, `all consoles kept running` | no wireless raised, no bus traffic — its only local mode is Download Play and no joiner client exists yet | `slots on the bus 00000`; `logs/v9ak_inventory.txt` |
| `Super Mario 64 DS (USA) (Rev 1).nds` | none (boot sweep, harness, 2 consoles / 2400 frames) | 40.1 emulated s, `all consoles kept running` | same: VS mode is Download Play (and 4-player), not implemented | `slots on the bus 00000`; `logs/v9ak_inventory.txt` |
| `LEGO Star Wars II - The Original Trilogy (USA) (Rev 1).nds` | none (boot sweep, harness, 2 consoles / 2400 frames) | 40.1 emulated s, `all consoles kept running` | no multiplayer mode to reach | `slots on the bus 00000`; `logs/v9ak_inventory.txt` |
| `tools/mkdiagrom/diag.nds` | `tools/coretest/md_smoke_test` (and the app's own Link test) | the whole check | nothing | **134 checks, 0 failures** |

**What the inventory settles: the teardown is not the link's universal behaviour.**
NSMB — the one title here that reaches a real race — carries a 262-byte
payload-per-round session for **234 emulated seconds with one lost round in
11,127** and no deauth on either console, on the same binary, the same link and
the same machine that MPHs dies on. So "the 2-console session ends" is a property
of MPH's own session state at the point the match starts, not of the transport:
on MPH the *first* event in every recorded run is the host's game changing its
own command-slot length (nothing the link writes), and the guest's answer to it
is its own `reason 0003` deauth; on NSMB neither game ever decides to leave. MKDS
is a different failure again (it never reaches a race at all: the route lands on
the game's own park, both screens one flat colour, `pc9 0200FD10`), and its 27
lost rounds are 1:1 with re-anchor drops where the host's window had already
closed — a late answer, not a withheld one; the per-round ordering was checked
and the windowthat dropped a surplus answer then *collected* a reply in the same round
(`logs/v9ai_mkds_app.log`, the `dropped 1, kept 1` line at 33787 followed by
`collected a reply from p2 for aid 1, 30 byte frame, fc=0118, seq=3510`).
**That one instance is now a count** (v0.1.9bs): pairing every window's own
`starting a new command for pN … dropped D, kept K` line with the `collected a
reply from` lines that follow it, up to that console's next window, gives
**24 windows that dropped an unread answer and 24 that collected a reply in the
same round — 0 that dropped and collected nothing**, over 26 dropped answers in
`logs/v9ai_mkds_app.log.gz`, and 4 of 4 in the fresh `logs/v9an_mkds_app.log.gz`.
So the drops are surplus by count and not by anecdote: they are the same
phenomenon as NSMB's single `empty 1`, and no round in either run lost its reply
to a drop. (The instrument had to catch its own bug first — its first form read
the `from pN` in `collected a reply from p2` as the window's console and
reported 0 of 24 — which is the same class of error this file keeps recording,
found by disbelieving a number that disagreed with the one quoted instance.)

**3 and 4 players: four consoles have now been linked into one group, on MKDS,
and no match has been played at either count.** The experiment in [Three and
four players](#three-and-four-players-four-consoles-link-and-the-app-is-the-limit)
is the state of the art — at four, one host and three guests all couple from the
app's own route (`link mask 0xF`, every console's digest read at the same
emulated frame, the host's game asking for three clients) and stay coupled
through CUT OFF into the pre-race wait. Nothing has been tried at four on any
other title, and no race has been started at any count.

### Four consoles, four independent players: input is per-console (2026-10-15)

The question here is narrower than a match: with four consoles of a title
running at once, does each console read **its own** player's input, or are
players 2-4 dropped, duplicated onto one input, or misrouted to another console?
The instrument is the hardware's own answer - every console reports its keypad
in `0x04000130` (active-low, bit 0 is A, `0x3FF` is nothing pressed), the
register the game itself reads, and `tools/appctl.py mem 04000130 --player N`
reads it back per console. `tools/scripts/four-console-input.py` holds one
console's own key (or channel press), reads all four registers, releases, and
reads again.

**What was broken, measured before the fix.** All four consoles shared one
keymap and the page sent every key event to the console the mouse had selected.
On a four-console `tools/mkdiagrom/diag.nds` session: holding four players' keys
at once moved console 1's own keypad to `0x000000C3` (UP+DOWN+A+B) and left
consoles 2-4 at `0x00000000` (`logs/gt4_diag_app.log`), and a second player
pressing `Digit2` while `KeyW` was physically down dropped console 1's UP
(`0x40` -> `0x0`) with no press left to bring it back until the key was released
and pressed again.

**The fix** is one disjoint keyboard zone per console, routed by the console
whose zone carries the key (`default_keymap` in `src-tauri/src/lib.rs`,
`ownersOf` in `frontend/index.html`): console 1 `W A S D` + `J K U I`, console 2
arrows + `, . ; / [ ]`, console 3 the numpad (`Num8/2/4/6` d-pad), console 4
`T F G H` + `O P N M`. `Digit1..4` now pick only the console the **mouse and
peek** act on (`Numpad1..4` are console 3's d-pad, so they are no longer
selectors), and selecting a console no longer releases another player's held
keys. `cargo test --lib` holds the zones disjoint and complete (52 pass).

Every title in `Test Roms/` was then run at four consoles through the app and
tested on both surfaces (`channel` = the control channel's own input,
`keyboard` = a real key event in the page's own path); each read is that
console's keypad register, and a pass means exactly one console's register moved
and all four read `0x3FF` again after the release (`logs/gt4_roms2.txt`;
each title's own app log is `logs/gt4_<name>.log`, which is where the
frame rates in the note column come from):

| ROM | 4 consoles | channel | keyboard zone | note |
| --- | --- | --- | --- | --- |
| New Super Mario Bros. (USA) | ✅ 60 fps/console | ✅ | ✅ | multi-card title, 2 players in-game |
| 0168 - Mario Kart DS (U)(SCZ) | ✅ (dips to ~30 fps once its attract loop plays) | ✅ | ✅ | 4 consoles in-group; no race (see above) |
| Metroid Prime - Hunters (USA) (Rev 1) | ✅ | ✅ | ✅ | 2-player match; 3-4 blocker is the game's own |
| Tetris DS (USA) | ✅ | ✅ | ✅ | single-card Download Play route is green at 2 (re-verified to BOOTED 2026-10-16); the card room takes three cart guests, but the host's start press hands the room a download packet, so a full-cart party drops at the start press |
| Star Fox Command (USA) | ✅ | ✅ | ✅ | single-card Download Play, 2-console route |
| Super Mario 64 DS (USA) (Rev 1) | ✅ | ✅ | ✅ | VS mode is Download Play |
| Mario Party DS (USA) (Rev 2) | ✅ | ✅ | ✅ | Download Play only; transfer completes, guest stalls post-boot |
| Bomberman (USA) | ✅ | ✅ | ✅ | Download Play; same post-boot class |
| Meteos (USA) | ✅ | ✅ | ✅ | four consoles boot to the main menu and SIMPLE's rules page; no wireless entry on that page |
| Diddy Kong Racing DS (USA) | ✅ | ✅ | ✅ | four consoles boot to the game-type wheel; multiplayer category not reached |
| Mario Hoops 3 on 3 (USA) | ✅ | ✅ | ✅ | multiplayer mode not driven yet |
| Kirby Super Star Ultra (USA) | ✅ | ✅ | ✅ | multiplayer mode not driven yet |
| 0595 - Club House Games (U)(WRG) | ✅ | ✅ | ✅ | multiplayer mode not driven yet |
| LEGO Star Wars II (USA) (Rev 1) | ✅ | ✅ | ✅ | ⬜ no multiplayer mode - negative control |

`diag.nds` additionally proves *simultaneous* distinct input, not one player at a
time: with `KeyJ`, `ArrowUp`, `NumpadDecimal` and `KeyT` held together, the ROM
itself reads `0x1`, `0x40`, `0x2`, `0x40` on the four consoles and all four
return to `0` on release (`logs/gt4_diag_final.log`, shot
`logs/shots/gt4_zones_held.png`).

**What this does not say.** It is input *reading* that is verified per title,
not a four-player match: every title boots to its own screen with four consoles
each reading their own input, and the in-game 3-4 player blockers recorded above
(MPH's host game, MKDS's park) are untouched by it, because those are the games'
own state machines rather than the input path. Two input gaps remain and are
named in [to-do.md](./to-do.md): per-player **remapping** (the zones are fixed
defaults, Phase 2) and any **gamepad** path at all - the page has no Gamepad API
handler, so four pads have no route today. The mouse remains a single pointer on
the selected console; per-console touch is available to instrumented runs only
(`appctl.py touch --player N`).

### What four players has actually been verified to do, title by title (2026-10-16s)

The section above verifies *input* - each of four consoles reading its own player's keys.
This is the separate question the summary table's `Players` column answers, taken per title
and stated as *verified* or *not verified* rather than as a capability. Every app log carries
its own startup line (`starting session: N players, rom <path>`), so the counts below are read
off the archive rather than remembered - **59 of its 280 runs are four-console sessions** -
and no title was driven to produce this list.

| ROM | four-console runs on disk | what four consoles has actually done |
| --- | --- | --- |
| 0168 - Mario Kart DS | 30 | Couples into **one group** (`mask 0xf`) on the durable route with every join verified from its own state, then loses its consoles one at a time with **nothing pressed** (the first out 107-167 emulated frames after coupling) on each console's own `reason 0003`; **no character select and no race at four** - nor at three or two. At four the app runs it at 44% of DS speed. |
| Tetris DS | 12 | The card room takes **three cart guests** (`tools/scripts/tetris-4p.sh`), the host's own room screen lists them under its `NAME` column and AIDs 1-3 are assigned - then the host's start press hands the room a packet of the **download image**, after which each guest's own game deauths and lands on its own `COMMUNICATION ERROR`. **No match at four.** |
| Metroid Prime - Hunters | 2 | The lobby reaches `mask 0xf` at four with every reply credited under its own AID and zero deauth sent at the roster, and then the host's own game stops its round loop; **no match at four** (a real match is verified at two). |
| New Super Mario Bros. | 1 | Four consoles boot and each reads its own input at 60 fps/console. Its multiplayer path is multi-card and a **real race is verified at two players only**; four were never driven past the start. |
| Star Fox Command | 2 | Four consoles boot and read their own input; its multiplayer path is single-card Download Play, verified at **two** consoles. |
| Super Mario 64 DS | 1 | Same shape as Star Fox: VS mode is Download Play, the transfer completes at two, no battle yet. |
| Mario Party DS | 1 | Four consoles boot and read their own input; its Download Play transfer completes at two and the guest stalls post-boot. |
| Bomberman | 1 | Four consoles boot and read their own input; Download Play again, the same post-boot class. |
| Meteos | 1 | Four consoles boot, read their own input and reach the main menu; **no wireless entry is drawn on the main menu or SIMPLE's page**, so wireless mode is not reachable from there (v0.1.9dw). |
| Diddy Kong Racing DS | 1 | Four consoles boot, read their own input and reach the game-type wheel; it would not rotate past SINGLE PLAYER/OPTIONS, so multiplayer is not reached (v0.1.9dw). |
| Mario Hoops 3 on 3 | 1 | Four consoles boot and read their own input; multiplayer mode never driven. |
| Kirby Super Star Ultra | 1 | Four consoles boot and read their own input; multiplayer mode never driven. |
| 0595 - Club House Games | 1 | Four consoles boot and read their own input; multiplayer mode never driven. |
| LEGO Star Wars II | 1 | Four consoles boot and read their own input; the title has **no multiplayer mode at all** (negative control). |
| `tools/mkdiagrom/diag.nds` | 3 | The link self-test at four consoles, including four simultaneous distinct inputs. |

**What no row above can claim:** a four-player *match* in any mode of any title, and any
four-console Download Play session - the synthesized joiner is constructed only in a two-console
session (`src-tauri/src/lib.rs:2167`, `p.download_play && n == 2`), so every Download Play row
here is the cart path at four and the single-card path at two. The three titles with a working
multiplayer path (NSMB, MPH and Tetris DS) are verified at two players; MKDS is the only title
whose four-console *room* is under measurement at all, and what ends it is its own games'
teardown rather than the link (see its section).

---

## New Super Mario Bros. (USA) — ✅ playable, 2 players

The reference title. It is the one game in the folder where two linked consoles
reach a real match and stay there.

**Route:** title → file select → **Mario Vs. Luigi** on the title's bottom-screen
menu `(128,95)` → the **Multi-card** entry `(128,72)` (not Download Play) →
character select `(127,119)` / `(127,160)`, a different one per console → the
peer's "has appeared. Play a game?" **Yes** `(128,71)` (it arrives a variable
number of frames later, so it needs a repeating tap, not one timed tap) →
**Vs. setup OK** `(128,171)` → gameplay driven with **A**. In the app this is
`tools/scripts/nsmb-route.sh`, driven by `tools/nsmb_join.py --restart` through
the control channel.

**Link, in the harness's units** (`logs/nsmb_app_run6_keylines.log`, history.md
v0.1.9ag): guest `cmds rx 7916 answered 7916` with no blanks and no canned
stand-ins, host `cmds tx 7916 acks 7916` with `collected 7612 empty 4 stale 0
reanchor-dropped 3`, `deauth out 0 in 0` on both, `slots 0x3` on both, both
consoles `LINK ACTIVE`. The harness reaches the same state on the same save
(`logs/route_recheck.log`: `slots 0x003`, 11,129 commands ↔ answers ↔ acks,
+234.00s).

**Playable, not just linked** (history.md v0.1.9ai, four runs): with the guest
alone holding RIGHT — and the host's own input trace owning **0** lines in that
phase — the host console's own picture moves. Its top view moves 1021 / 1739 /
1021 / 0 px (a band at y 155..178) and the shared bottom screen moves 1927 /
1910 / 1857 / 740 px in no-input baselines that read 611/612 and 0 px. The same
press moves the guest's own screens by 29,000–31,700 px, so the control is
loud. The mirror phase (host alone holding RIGHT) is the same shape, which is
what rules out an animation the baselines happened to miss.

**Known issues:**

- **A rare link drop.** Twice in ~3.5 ks of linked session (once at ~20 s, once
  at ~1785 s) the guest's game leaves and the host follows ~1 ms later —
  `last deauth reason=0003`, the guest's last answer `len 6` against the normal
  70 B. It is not a function of elapsed time, and 16 further runs were clean.
  Unreproduced by request.
- After the drop, both consoles land on the game's own `Connection interrupted`
  dialog; pressing A on the guest clears it and returns to the title.
- **No race has been run to a result.** Nothing yet asserts kart/character
  positions; sync is inferred from pictures and counters.

---

## 0168 - Mario Kart DS (U)(SCZ) — 🟡 partial, 2, 3 and 4 players

The join works end to end through the app. The race does not start.

### 2026-10-16d — the phantom client was never in these walls, and the four-console re-run is blocked by the route, not the link

**The contamination premise, checked against the code and the archive instead of
assumed: it is false for both titles this pass started with.** The synthesized
Download Play client is constructed only for a title whose profile says
`download_play` **and** only for a two-console session
(`src-tauri/src/lib.rs:2167`, `per_game.filter(|p| p.download_play && n == 2)`);
Mario Kart DS (`AMCE`) and Metroid Prime - Hunters (`AMHE`) are
`download_play: false` in the registry (`src-tauri/src/game_profile.rs:58` and
`:76`). The archive agrees on 181 logs: of every archived app log that carries
the `synthesized client` line, the titles are Star Fox Command (119),
Super Mario 64 DS (25), Tetris DS (24), Bomberman (7) and Mario Party DS (5),
166 of them two-console sessions and 14 at four consoles (the Tetris arms and the
`gt4_*` Download Play runs). **Not one is an MKDS or MPH session.** So the gate
that was added on 2026-10-15 removed a radio these two titles' sessions never
had: it cannot have changed any of their 2-, 3- or 4-console runs, and the walls
in this file stand on their own measurements rather than on a re-run being owed.
The title whose four-console wall *was* contaminated is Tetris DS, and its arms
are already re-measured above (`2026-10-16`, `2026-10-16b`, `2026-10-16c`).

**MKDS at four carts, on the current binary: the room was not re-reached, and
what blocks the re-run is named.** Four drivings of the documented route
(shipped defaults, `MD_LINK_VERBOSE=1`, four cart consoles, 44% of DS speed):

- `mkds_join.py` driving consoles 1 and 2 while four consoles run
  (`logs/mkds4c_app.log.gz`, `logs/mkds4c_p12.log`) ends at *"no host role yet -
  P1's room screen may want another tap"*: console 1's CREATE GROUP never took.
- The same route driving all four consoles with mkds_join's own wall-clock waits
  (`logs/mkds4_app.log.gz`, `logs/mkds4_route1.log.gz`) put **console 1** in a
  room - its game armed a beacon (`handed ... fc=0080 seq=4350`, `bcn 01AD / FFEB`,
  `us cntl 1 / 1`) - while consoles 2-4 never left the title's attract loop, which
  discards touches (`wait_stable` timed out on each and every tap was swallowed),
  and the run ends `mask 0x0`, `role 0` on those three.
- A change-gated variant (`logs/mkds4b_app.log.gz`, `logs/mkds4_route2.log.gz`)
  and a variant with the waits scaled 2.5x and A on the title
  (`logs/mkds4d_app.log.gz`, `logs/mkds4_route3.log.gz`) both *aligned* all four
  consoles - after the MULTIPLAYER tap all four `shot` columns carry the same
  digest, `a1591fa5`, and every tap is answered (`p1..p4 answered the tap at
  (128, 68)`) - but on the page they reach neither the group-list touch
  coordinates nor the group bar moves anything for 25 s each (`create-group: p1's
  bottom screen still a1591fa5 after 25s`; `join-p2: p2's bottom screen still
  a1591fa5 after 25s`, three attempts), and that page *is* input-live: driving it
  by buttons walks pages (each A press changes the digest, `c1df2de0 17fc82f1
  bf2ac560 e6a4ecf6 97efa57c 322613fa 767bbeb4`) without ever raising a radio
  (`mask 0x0`, `role 0`, no `wireless on` past `begin #1`).

So at four consoles the recorded route's wall-clock waits (title 2.5 s, menu
3.0 s) cover ~2.3x less emulated time than they were tuned for, and this fork has
no page oracle for this title: the one instrument that would let a driver gate
each step on the page it is actually on exists for MPH's lobby
(`tools/scripts/lobby_page.py`) and not for MKDS. **No four-console MKDS run
reached the room this pass, so the pre-race park below is neither confirmed nor
refuted at four carts** - it is untouched by the gate for the reason above, and
what stands between this tree and a four-cart measurement is an MKDS page
classifier (reference frames of the title's own menu screens) plus speed-scaled
waits, not anything in the link.

**MPH at four carts: same class of result, and one instrument of ours convicted.**
The recorded route (three `START` presses, `TOUCH TO START` (128,96),
`MULTIPLAYER MODE` (172,85), `MULTI-CARD PLAY` (132,80), the host's
`CREATE GAME` (44,89), each guest's row in `AVAILABLE GAMES` (186,59) then
`JOIN GAME` (122,64)) was re-driven on four consoles
(`logs/mph4_app.log.gz`, `logs/mph4_route.log.gz`). Two facts came out of it, and
neither is a four-player match:

- **It does not align**: after the MULTI-CARD taps the four consoles are on
different pages (`page=HUNTER`, `page=UNKNOWN`, `page=INGAME` on three of them,
and the first console's column empty), no room is created and the session ends
`mask 0x0`, `role 0` - so this pass has no four-console MPH measurement either.
- **`lobby_page.py`'s page names are calibrated for the lobby and the match HUD,
not for this title's menus**, and it reads MPH's *menu* screens as `INGAME`:
at the title step three consoles report `page=INGAME` with `topcolour` 40k-48k
and `inktop` 1.7k-5.3k, against the classifier's own in-match threshold (top ink
> 800 *and* colour above the HUD's floor). A four-player MPH confirmation cannot
be read off that label; it has to come from the game's own HUD (its `TIME`/`RANK`
text) or the 42-byte application bodies, and that is the instrument this title's
next pass needs before its wall can be re-measured at four.

### 2026-10-16i — the create press is half the create: the console powers its own radio down and waits for a confirm, and the confirm is what raises the room

**The host's radio going down at the create press reproduces, twice, on the
console's own side.** Two instrumented two-console sessions (`logs/mkdscreate_r1_probe.log.gz`,
`logs/mkdscreate_r2_probe.log.gz`, app logs beside them) took console 1 onto the
group list (`193382FD`) and pressed its bottom control. Both runs, same frames:
the page moves to `B9F40C89`, and in that window the console's **own game** writes
its radio down - `W_PowerState`, `W_PowerTX`, `W_PowerUS` (the ledger carries the
writing PC, `pc7=0600ADB8`), `W_IE` cleared to `0000`, `us_cntl` 0 - the US
counter stops (`USCOUNT` frozen at `A34550` / `B23930` across thirty emulated
seconds), and the fork's bus records `wireless off (end #2, slots now 0x0)`.
**What it read, and what it did not:** in that window the console made 371 more
wifi register accesses and then none at all for 30 s - its last baseband access
is a single `W_BBRead` (1017 -> 1018 reads, last value `000E`). So the game
stopped driving the radio of its own accord; it did not spin on a frozen
register, and nothing about that state is the link dropping a session.

**And it was not a hang: the game was waiting for one more input.** Pressing `A`
on that parked console - nothing else, no emulator change - brought its radio back
up as host: `wireless ON (begin #3, slots now 0x1)`, `role=1 (host)`, a fresh
72-byte `fc=0080` frame further into RAM (`start=12CC`), and the page moving on.
So the previous entry's reading, that the host's radio "ends at the create press
and never turns back on", was the route stopping **one input short**: MKDS's
create sequence is the touch on the list's bottom control *and then a confirm*.
The durable runner carries that pair now (`create_room()`: the touch, then `A`,
gated on the radio coming back with the room frame in hand - a radio that stays
down is reported as the stop).

**Still not reached: a joiner in that room.** The follow-up join attempt in the
same session misaligned console 2 on the joiner side (its `MULTI` tap at
`(128,68)` landed on the main menu, `1ECFD2D0 -> EF0760E8`, and three taps at the
group bar then walked it through three pages with its radio down throughout,
`role=0 handed=0000/0b`). That is a page-relative alignment defect of the
joiner route, not a link result - **fixed in `2026-10-16j`**, which anchors the
joiner on the page it is actually showing and on its own radio, and takes the
four consoles into one room.

### 2026-10-16l — the coupled room does not survive untouched: the first guest leaves 167 emulated frames after coupling with no input at all, and its own last second is a roster exchange it answers with the lobby stub

**The experiment.** Four consoles are walked into the room by the durable route's own
helpers (`mask 0xf`; host `role=1` handed `0118/38b`, every joiner verified from its own
state — `role=2`, handed `0218/40b` / `0228/42b`), and then **nothing is pressed at all**.
The probe reads each console's own page CRCs, its own count of polls received
(`cmds_rx`), its own deauth counter and the bus mask every 15 emulated frames
(`logs/mkdshold_hold.log`, app log `logs/mkdshold_app.log.gz`, end shots
`logs/shots/mkdshold_hold_end_p1..p4.png`).

**Result: the room tears down on its own.** Coupled at frame 12646; at **frame 12813 -
167 emulated frames later, about 2.8 emulated seconds - the bus left `0xf` for `0x7`**
because **p4's own game wrote `deauth ... reason 0003`** (`deauth_out 0 -> 1`, its own
handed frame dropping `0228/42b` -> `0080/72b`, back on its group list), and consoles 2
and 3 followed **37 emulated frames later - both of them at frame 12850** - with their own
`reason 0003`; the session ends at `link mask 0x1` - the host alone and still on its own
page (`[link-event] ... wireless off` frames 12813 and 12850). The probe stopped at the
first divergence, so its own end shots and page reads hold consoles 2 and 3 *before*
their leave (frame 12816) rather than on their stop pages - the one reading this run
leaves open, and the reason to let the next one run past the first console out. So the **CUT OFF/VS pair is not what
ends the room**: it ends with no input at all, before either press can matter. The
route's own comment ("a guest that is left waiting leaves on its own (the record's
2,740-frame floor)") is corrected in place: this run's floor was **167 frames**, and the
press lands inside that window rather than before it.

**What the client's own game read in its last second** (p4, from its own log): the host
had just handed it a **58-byte roster frame** (`BE 01 0E 00 0C 9F 06 00` then the host's
name in UTF-16, AID-mask `000E`) - the fourth of a burst at `seq 5D20`-`5D80` - and it
**answered it with the lobby stub** `00 00 5A 5A B8 B6`; its own game then **read six halfwords of
that frame** (`1EDC`, `1EDE`, `1EE4`, `1EDC`, `1EE8`, `1EFE` - the last two well into the
name field); the host **collected its reply** (`seq=1510`); the host's ack was delivered
to it; and then its own game wrote `LOC2 fc=00C0 seq=1520` - the deauth - and powered its
radio down. The link's own numbers over that window are clean: host `collected 1058 /
empty 11 / stale 0 / reanchor-dropped 49` for the whole session, the round the client
answered last was collected (`seq=1510`), and the guest's own pointers show no loss -
p4 `answered 333 of 362` (26 blank = rounds it was not addressed in, 3 canned).

**The roster frame is not the trigger, and neither is the press.** The same
`5D20`-`5D80` burst went to all three guests as one group-addressed command, p2 and p3
kept being polled to `seq 6270` afterwards, and every guest received 32-36 such roster
frames over the session in **seven bursts of four (one of eight)** - the last of them the
`5D20`-`5D80` one - with only the host's own name in them and no client roster; what
differs between the guests is their own count of rounds since their own
join (p4 was the last in, 362 polls received against p2's 412). So the decision is a
client-side counter inside MKDS's own code, reached during a roster exchange it answers
with the lobby stub - the position the `AMCE` registry entry already states ("the
remaining obligation is inside MKDS's own ARM7/ARM9 code") and the one `0.1.9bp`
reached for MPH's two-console teardown. **No code changed for it**, and the four-kart
outcome is still not reached: what this pass adds is that the input-based explanation is
measured false and the client's own leave is now pinned to a frame and a frame of its own
log.

**The mission's own first test, run per round instead of by aggregate (2026-10-16m): the
reply path carries no divergence, and "the previous round's answer" is not what the
stub was.** The hypothesis to test was that a console answered a round with the *wrong*
round's bytes - a live clients-fork divergence lockstep play would abort on. Both coupled
four-console logs were compared round by round: every answer sent, the arm it came from,
the round the game armed it in, the round the poll landed in, and the host's own copy of
it. On the hold run (`mask 0xf`, nothing pressed): **1,088 answers, 1,088 of them
carrying the body of their own arm, each served in the very round its game armed it in
(lag `+0` x1088 on all three guests), and not one round carrying two different answers** -
the guests' games armed **one** body for their entire joined life (`00 00 5A 5A B8 B6`,
411/399/359 arms), so "the same answer as the previous round" is what the *game* writes,
not a stale arm the fork re-used. The pressed run (7, `mask 0xf` held far longer,
**10,607 answers**) is the same: all 10,607 carry their own arm's body - 9,103
byte-identical and 1,504 prefix-equal, the latter being the fork's own 49-character body
print truncating the 16th byte's trailing space (**0 real differences**) - lag `+0`
x10,607, and
**0** rounds with two different arms. The one deviation from "exactly once per round" is
this fork's documented hold policy: a round the game armed nothing for gets a re-send of
the previous arm (hold run 55/49/30 rounds, run 7 273/869/106) - same arm, same bytes,
never another round's *different* bytes - and it is not the trigger: the console that left
**first** had the *lowest* repeat rate (30 of 303 against p2's 55 of 330). Answers the host
never collected are early-session only (hold run 35 of 1,088, all before round 5200; run 7
338 of 10,607) and none sit in the leave window, and no answer was ever collected twice.
Evidence: `logs/mkdshold_hold.log` (round script output in `logs/mkdsleave_rounds.txt`) and
run 7's own app log.

**And the leaving console's own code, read live (2026-10-16m): the ARM9 enters no new code
region, and the reason-`0003` frame is armed by the console's own ARM7 driver, at the same
code address, on all three leavers.** Three more coupled sessions were run with the fork's
own instruments armed (`logs/mkdsleave{,2,3}_route.log`: coupled at `mask 0xf`, nothing
pressed, `pctrace` on every console's ARM9 across the whole window plus a per-16-frame
region diff, and the wireless register table read in the very frame the bus dropped a
console). The leave reproduced in all three instrumented sessions: the bus left `0xf` at
frames **11646, 11664 and 11704**, i.e. **107, 115 and 148 emulated frames after the
traces were armed** (the hold begins a few frames before the arming), against the
untraced probe's **167 emulated frames after coupling** - the same ~2-3 emulated seconds,
every time with nothing pressed and the hold's own budget (600/700/1200 frames) nowhere
near spent.
**A claim this file carried is withdrawn rather than repeated, and the withdrawal is the
first finding below: the per-bucket "the ARM9 enters no new code region" series never
existed.** The instrument that was to produce it - a diff of the trace report's 4 KiB band
list every 16 frames - had a parser that stopped at the *first* band row, so every bucket
came back empty and the absence of any "new region" line in the log was a blind
instrument read as a measurement; found by review on 2026-10-16 and fixed in
`tools/scripts/mkds-4p.py` (`trace_regions` reads the rows now, and reports a report with
no band section at all as blind instead of as empty). What the saved reports *do* support
is the reach list each console had at its leave, and it leans the other way: the four
consoles of the leave3 session hold **60, 64, 64 and 64 bands** of main RAM at the leave
(`logs/mkdsleave3_pctrace_leave1_p1..p4.txt`), and the last entries of those ordered lists are
bands the console first entered late in the window (`0212E000`, `02131000` on p1) - reached
without a date attached, so they neither prove nor exclude a first entry in the leave's own
frames. Bounded to main RAM, which is where this title's code runs. What
*is* named is the write that takes the console out: the wireless block is ARM7-only in the
emulator's map (`NDS.cpp` maps `0x04800000` as `Mem7_Wifi0/1`), and the register table read
at the leave reads **`W_TXSlotLoc2 (0A4) val=8248 pc7=06015528 lr7=06015518`** on all
three leavers - the same value the app log's own `txslot LOC2 <- 8248 armed` line records
immediately before each `deauth sent ... reason 0003`, i.e. the identity is value-for-value,
not inferred. That slot is the driver's outgoing-management one: each guest arms it exactly
three times in the session - the authentication, the association, and then the deauth - from
the same ARM7 image at `0x0601xxxx` (the same image writes the reply frame's `5A5A/A5A5`
filler from `0x0600C158`/`0x0600C1C4`). **One claim from the first traced session is
corrected here rather than repeated:** "the ARM7 stayed inside six WRAM bands across the
leave" is an artifact of that trace's window (`[03800000,03810000)`, the only place
`RecordPcVisit` could record), not a measurement - the ARM7's driver is executing in
`0x0600xxxx`-`0x0601xxxx` throughout, as its own register-write PCs show. **The ARM7 trace that paragraph asked for then ran, and it names the arm's two call sites
and rules the fork's register reads out, but not yet the branch.** One more coupled session
(`logs/mkdsleave4_route.log`, `tools/scripts/mkds-4p.py --hold 900 --trace-leave`): four
consoles into the room, nothing pressed, `pctrace` armed on **every console's ARM7 over
`[06000000,06040000)`** - the driver's own image, 256 KiB, not the WRAM window - with the
wireless register table and a 1 KiB code window read back in the frame the bus lost each
console. The leave reproduced (bus `0xf -> 0x1` at frame 13618, 837 emulated frames after
coupling, nothing pressed). The register table names it exactly as before - `W_TXSlotLoc2
(0A4) val=8248 pc7=06015528 lr7=06015518` on all three guests - and now carries the
**host's own last arm beside it: `val=84AC pc7=06015530 lr7=0601552C`**. `lr` is an
ordinary register (the JIT writes it faithfully) and the two LRs are two different return
addresses four instructions apart: the guests' arm happened inside the function called at
`0x06015514` (`bl 0x06015220`), the host's inside the one called at `0x06015528` (`bl
0x0600E824`), with the test between them read out of the code window itself - `0x06015518
ldrh r0,[r9,#0x0C]`, `0x0601551C cmp r0,#1`, `0x06015520 bne 0x0601553C` (the target is
read off the branch's own encoding, `1A000005`, not from the window's edge). (`pc7` is the raw
`R[15]` at the write, block-local under the shipped JIT, so it locates the code but not the
storing instruction; the dump is a symmetric window around it for exactly that reason.) So
the leaves are reached through the *first* of those two calls while the host's own last arm
is reached through the second - the guests and the host must not be read as one code path -
and **`[r9,#0x0C]` is a word of the driver's own state block, not a wireless register**
(`r9` is the same base for `+0xA8`, `+0xB0`, `+0xB8`, `+0xBC` in that routine), and no
console reads register `0x00A` or `0x00C` anywhere in the retained read tail of any of the
four (`logs/mkdsleave4_wifiio_leave1_p*.txt`): the firmware offsets melonDS leaves
unimplemented are not what that test consumed. **The branch was still unnamed at that point, and the two passes since replaced "the printout
is too short" with something narrower.** A `pctrace` report used to print only its **last 320
visits** (about half an emulated frame of this ARM7) while the recorder's ring held **411
frames** of history (532,880 visits over frames 12784..13621, 262,144 kept, ~640 visits per
emulated frame) and the report is read one to three frames after the leave, so the arm was
inside the recorder and outside the printout - all four leave4 reports end in the same
post-leave idle loop (last pc `06001F94`). **The tail is 3,072 rows now**
(`NDS::FormatPcTrace`'s `kPcTraceTail`, with the row count derived from the caller's buffer
so a smaller caller gets a smaller tail instead of a cut row, and a report that fills the
buffer returning -1 instead of looking short). The next coupled session then measured where
those rows actually go (`logs/mkdsleave5_route.log`, four consoles, nothing pressed): p2 spent
**2,375 of its 3,072 rows in `0600F000`** - the transceiver teardown's busy polls - 289 in
`0600E000`, 132 in `06018000`, 125/122 in `06009000`/`06008000`, and **none at all in
`06015000`-`06016000`**, the band its own arm site sits in; that is about two emulated frames
of printout, so a wider tail buys more teardown, and the window - not the printout - is the
depth control. The window is therefore the decision code itself, `[06015200,06015600)`: the
routine at `0601536C` and the callee every guest arms through, `06015220`, called from
`06015514`, with the test and the taken branch's target inside, and the host's second callee
(`0600E824`, inside `0600Exxx`'s 289-row teardown band) deliberately outside, where the
register table's `lr` already attributes that arm. **That window works, and what it then
measured is that the arm is rare rather than buried** (`logs/mkdsleave6_route.log`): the
leave reproduced (bus `0xf -> 0x7` at frame 11673, **119 emulated frames** after coupling,
nothing pressed, leaver p4), and p4's own ARM7 recorded **9 rows across frames
11565..11673** - through `06015484`/`06015498`/`060154B8` into the callee's own
`06015238 lr=06015518` (the first arm call, whose return address is the `bl` at `06015514`)
and then past the test at `06015518` to the routine's epilogue - while p1, p2 and p3
recorded **0 rows** in that window over the same span, their own register tables still
naming the `pc7=06015528` arm from earlier in the session. So the decision code runs about
**once per 100 emulated frames**, and the value its `ldrh` loaded **is on none of those
rows** for a reason that is mechanical rather than short: `pctrace` records at the
dispatcher, and under the shipped JIT the code between that load and the epilogue runs as
linked blocks that never return to it (p4's rows jump from `06015518` straight to
`060155A0`). The route now also reads the trace *before* the wireless dumps at a leave,
because only the trace's window is that short.

**Then the value arrived, and the branch is named.** The same route run on the interpreter
(`MD_NOJIT=1` - the project's documented way to make this hook exact, every instruction being
a dispatcher entry there) with the window armed *before* the walk (`--trace-early`, so the
ring spans the host's own arm at the coupling instead of only the hold):
`logs/mkdsleave7_route.log`, four consoles coupled `0xf`, bus steps `(11460, 0xb)`,
`(11550, 0x3)`, `(11563, 0x1)`, 900 emulated frames asked with nothing pressed (the session's
own supervisor line: app `exited with status 0`, save `unchanged`,
`logs/mkdsleave7_app.log.gz`). Every
console's own `ldrh` is on a row now, and the values divide exactly as the roles do:
**p1, the host, reads `1`** at `06015518` on every one of the **8** passes in its printed
tail and **falls through** the `bne` - the rows at `06015524`/`06015528` are there, so it
does make the second call at `06015528` into `0600E824`; **p2, p3 and p4, the guests, read
`2`** (2, 3 and 2 passes in their tails) and **take the `bne` at `06015520`** - not one row
at `06015524` or `06015528` in any guest's printed tail, in any of the session's three
leaves, so what they keep is the arm the first call at `06015514` placed. The tails are the
report's last 3,072 rows of larger rings (6,501 rows kept for p1, 39,502 for p2, 20,554 for
p3), so the *unprinted* part of the session is covered by the other instrument rather than
by this one: the same run's own register tables carry `lr=0601552C` for the host's last `0A4`
arm and `lr=06015518` for each guest's, which is the same division of the branch. The field is the driver's own idea of which side of the room
this console is: **`1` for the group's host, `2` for a client.** The guests' `8248` against
the host's `84AC` is therefore not two consoles disagreeing about one value - the host makes
one more call than a client, and that call re-arms `W_TXSlotLoc2` for the host's own side.
The two instrumentations agree where they overlap, by unrelated means: the JIT session
(leave5/leave6) attributed each console's last `0A4` arm through `lr` - the guests' to the
call made at `06015514`, the host's to the call at `06015528` - which is the same branch
outcome these values show. **The decision is the game's own and is not a fork artifact, so no
fork change is proposed for it:** `[r9+0x0C]` is a word of the title's own ARM7 driver state
(the base is the block named by the fixed ARM7-WRAM pointer `0380FFF4`, live `0380AFB0` on all
four consoles and in the ARM7's private 64 KiB WRAM - 2026-10-16t), no
console reads `0x00A`/`0x00C` in the retained read tail of any of the four, and both paths
are the ones MKDS's own image carries for a host and for a client. **What this leaves, named
rather than implied:** the leave the instrument was built around - each guest's own
`reason 0003` deauth some 100-170 emulated frames after the room couples, on every no-press
run since 2026-10-16l - is *not* this branch and is not decided here; it sits downstream of a
client-side counter in MKDS's own code, with the reply path itself already cleared round by
round (2026-10-16m). One confound, stated because it is real: the values above are read on
the interpreter, because the shipped JIT links this code past the dispatcher that the
recorder hooks. That run reproduced the session shape (the same coupling, the same leavers)
and the same path outcome the JIT runs show, but the interpreter's own timing difference is
why it is presented as the cross-check it is rather than as the measurement of record.

**The count is the trigger, and it is now measured against the control the record never had
(2026-10-16o).** Two coupled consoles, nothing pressed, 900 emulated frames:
`HOLD VERDICT: coupled=0x3 bus steps=[] final mask=0x3` - **no leave inside the hold window**, the
bus still holding both consoles at the end, and the single guest answering every one of its 4,753
polls. That is now **five consecutive runs** (`logs/mkds2p_hold_route.log`,
`logs/mkds2p_hold2_route.log`, `logs/mkds2p_hold3_route.log`, `logs/mkds2p_hold4_route.log` and
`logs/mkds2p_hold5_route.log`), every one of them `bus steps=[]` and every
one ending with the room still coupled. Four consoles in the same shape lose a guest at **107-167
emulated frames** after coupling on every run recorded (leave6: 119 frames, leaver p4). **Corrected
by `2026-10-16v`:** `bus steps=[]` is a statement about the hold window, not about the room. The
two-console guest does leave - `logs/mkds2p_hold2_route.log` already ends its own watch at
`mask=0x1`, and a two-console run this pass left its guest **145 emulated frames after coupling**
inside a 1,400-frame hold - so the five runs above stopped watching before it; and the walk's own
callers and values are element-for-element the same at one client and at three. So the count does
not decide whether the guest leaves, and it does not set the leave's clock either (the spread is
107-325 frames across the four-console runs, 145 here, and the record's own oldest floor for a guest
left waiting was 2,740).
**What the fork does is the same in both, so the delivery path is not what differs** (the
counts below are read out of `logs/mkds2p_hold_app.log.gz`, `logs/mkds2p_hold2_app.log.gz`,
`logs/mkdsleave6_app.log.gz` and `logs/mkds4p_sameround_app.log.gz`): the
host handed its game 1,725 / 13,868 / 258 replies from clients 1 / 2 / 3 against the guests'
own 1,727 / 13,872 / 259 sends (four consoles), and 4,747 against 4,752 (two) - ~99.9%
delivered either way - and the handoff order is a strict round-robin 1,2,3 in the four-console
run against all-1s in the two-console one. **The freshness of the guest's answer at the poll is the one number the
fork logs per round that could separate the console counts**, and read across every coupled run
on disk it does **not** separate the console that leaves from the ones that hold. In the run this paragraph first read
it on (`logs/mkdsleave6_app.log.gz`, re-derived this pass with every value reproduced): two
consoles, 3,973
answers "already in the reply slot at the poll" against 779 "held across its own clear"
(**16%** stale; the repeat run - `logs/mkds2p_hold2_route.log`, `bus steps=[]` again -
measured 904/368, **29%**, and the run that holds with no leave at all,
`logs/mkds2p_hold5_app.log`, 1,233/156, **11%**) and four consoles, p2
1,030/697 (**40%**), p3 10,707/3,165 (**23%**), and p4 -
the client that leaves - **6/253, i.e. 97% of its answers are the arm its own game had already
cleared**. The other three coupled four-console runs, read the same way, are what settles the
reading: `logs/mkdshold_app.log.gz` 55/330, 49/321 and 30/303 (**86/87/91%** held),
`logs/mkds4p_run7_app.log.gz` 481/2,040, 2,562/3,976 and 129/1,419 (**81/61/92%**), and
`logs/mkdsstate5_app.log.gz` 25/274, 24/289 and 6/267 (**92/92/98%**) - so a room that dies can
have every console near 90% held (state5) or a 23-97% spread (leave6), and a room that holds can
sit at 11-29%. The held arm is the shape of a *run*, not of the console that dies. What it does
rank, in all six coupled runs that lose anyone, is **who goes first**: the largest held share is
the first leaver in every one of them (leave6 97% out at frame 11663, hold 91% at 12779, run 7
92% at 11780, state5 97% at 12846; at three consoles `logs/v9d5_mkds_app.log.gz` 92% at 5445
against 84% at 5450 and `logs/v9d3_mkds_app.log.gz` 79% at 5525 against a 33% guest that never
left) - a correlation across six runs rather than a cause.
**Its one lever is measured and it is worse, so the default stands.**
`MD_SAME_ROUND_REPLY=1` (`logs/mkds4p_sameround_route.log`) does change the ordering - the
"held" kind goes to **0** on every guest - and what takes its place is the firmware's invented
stand-in: 110/114, 112/130 and 113/133 answers become "nothing armed", and this time the
**host** deauthorizes all three clients (`reason 0003`) *before* the hold begins, the hold
opening at `mask 0x1` against `0xf` in every shipped-default run. So the held arm is
load-bearing rather than the defect, and the answer-freshness ordering is not the fixable
cause. **What is left is named rather than implied:** the mechanism is the
*phase* between each guest's game and the host's rounds - a client whose arm is never live at
the poll is a client whose game is out of step with the cadence the host's round length sets
(`112 + (10 + W_CmdReplyTime) * NumClients(mask)`, stock's own formula, so a third client
stretches every round) - and no knob in the fork moves it. **The leaver's own polls are served exactly like the ones that stay, and the fork hands it
nothing once its own game leaves (2026-10-16s).** The question this pass had to settle was
whether the fork serves the console that leaves its own poll late, in the wrong round, or
addressed to the wrong client once three clients are present - a client that is not served its
own poll in the round it expects has exactly that symptom. Read per round out of the three
coupled four-console logs on disk (`logs/mkdshold_app.log.gz`, `logs/mkds4p_run7_app.log.gz`,
`logs/mkdsstate5_app.log.gz`) against the two-console control (`logs/mkds2p_hold5_app.log`), the
answer is no on all four counts, and the leaver's own numbers are the cleanest in the room.
**Offered and delivered are the same number**: p4 362 = 362, 1,749 = 1,749 and 292 = 292, with
**not one of them delivered again as an older command** (`0 as an old command` for every console
in every run). **Every poll of its own carries its own AID bit**, and the frames that do not are
the host's single-client frames for the *other* clients, which it answers with one blank each and
nothing else (p4's 26 / 199 / 18 blanks against 26 / 199 / 18 such frames, p2's 26 / 303 / 17 and
p3's 29 / 309 / 14 - exactly the counts the app's own digest prints per console). **Its cadence is
one poll per round, like the holders'** (one command frame between its own polls in 361 / 1,748 /
291 of them, against p2's 410 / 2,822 and p3's 399 / 6,847 in the same runs). **Nothing is handed
to it after its own game leaves**: in all three runs the last frame the fork delivers to it is the
one its own `reason 0003` deauth lands in (frames 12779, 11780, 12846) and the count afterwards is
0, so a departed console is not fed. **And its armed answers are consumed at least as often as the
holders'**: 303/359, 1,442/1,750 and 267/291 of the arms it wrote (84 / 82 / 92%), against p2's
330/411, 2,248/2,877 and 287/316 (80 / 78 / 91%) and p3's 321/399, 5,669/6,982 and 289/327
(80 / 81 / 88%), the remainder being arms its own game superseded. The one asymmetry the run holds
is not delivery at all: the host's own game addresses a four-console room unevenly, giving the
newest client (AID 3) 25% of run 7's rounds against AID 2's 99.5% (that run's own mask counts:
`0004` x4,217, `000E` x1,377, `0006` x912, `0002` x191, `0008` x98, `000A` x42), and that mask is
the host game's own bits ANDed with stock melonDS's failed-client mask rather than a schedule of
ours. **So the mission's fallback branch is the answer and no fork change is proposed**: MKDS's
four-console leave is not a client that failed to be served.

**The socket that sentence left open is now read on the
leaver's own side, and it is a code the title's own driver stamps, not a deadline of ours
(2026-10-16p).** Two things make that driver readable statically rather than through trace
rows: the ARM7 code a console runs at `06000000+` is **the cartridge's own ARM7 image
relocated by exactly `+0xF8F8`** (checked instruction for instruction across four windows of
one live session), and the frame each leaver sends comes from **one site** - `mov r0,#0xC0` at
`0601364C` is the only one in the image (6,861 instructions scanned), called from ten places,
among them the periodic frame-slot routine and the TX-done handler. **What the leaver stamps,
and when:** the console's own state block (`base = *(0380FFF4)`, live `0380AFB0`, so the block
starts at `0380AFB0` and the field below lands at `0380B2FC`; readers reach it as `+0x344`
then `+8`, an addend rather than the block's base) carries a transaction code at `+0x404` that it writes for about one beacon tick each time it queues a management
frame and clears immediately after. Read every ~9 emulated frames across a four-console
session with nothing pressed (`mask 0xf` at frame 11896, the bus leaving `0xf` for `0x5` at
**12897**, leavers p2 and p4, then `0x5 -> 0x1` at 12922 - `logs/mkdsstate5_route.log`), the
codes are `0x11`/`0x13`, `0x21`, `0x31` and finally **`0x41`** - high nibble a management
transaction (auth, assoc, deauth), low nibble its sub-state - and **every leaver's last code
before it leaves is `0x41`**, written 10-19 emulated frames earlier and gone again in the
frame the bus recorded the leave (p4 `f12892 -> 0x41`, `f12902 -> 0`; p2 `f12883 -> 0x41`,
`f12902 -> 0`; p3 `f12911 -> 0x41`, `f12919 -> 0`; `logs/mkdsstate5_delta.log`). The two fast
counters beside it are only how long the console had been in the room - `+0x3EC` at `0x30`
(48 ticks of a ~10 Hz counter) and `+0x400` at `0x124` (292, ~1 per emulated frame) at the
leave, and in this run they only ever advanced (they are time-in-room markers, and the sampler
has never been pointed at the two-console arm, so what separates a room that holds from one
that dies is the code, not these) - so a round-length deadline is excluded as the counter: the
codes are seconds apart (p4's `0x13`, `0x21` and `0x41` at ~119.5 s, ~122.2 s and ~126.7 s of
the run's own clock), tens of rounds apart, not one per round. **The frame then leaves on the console's own beacon tick**, with the fork's
slot machine naming it: `beacon IRQ14 fired ... TXSLOTBEACON=0000` immediately before
`tx frame: slot 2 (LOC2) fc=00C0 len=30 ... reply2 811A` - reply2 holds the reason-`0003`
management frame and LOC2 sends it, which is the same slot and return address the register
table dates to `lr7=06015518`, the client side of the branch leave7 named. **And the leave is
the guest's own transmission, not something done to it** (`logs/mkdsstate5_app.log.gz`): the
fork leaves a console out of multiplayer where it *transmits* a `0x00C0` frame as a client
(`src/Wifi.cpp:1735`, `[CLIENT] deauth`), and the first `fc=00C0` frame in the whole session
is **p4's own** (`109684-109686`, nothing deauthed at it first); p2 then transmits only after
being *handed* p4's frame (`109695` rx -> `109699` tx: 0009BF14EE63), and p3, handed both,
follows in the route's own timeline (bus `0x5 -> 0x1` 25 emulated frames after them; the app
log's per-console timestamps are each console's own clock, so they are not compared here). So the condition is the title's own management transaction `0x41` - a
deauthorization the console decides for itself - and **no fork change is proposed for it**:
the delivery path is cleared round by round (2026-10-16m), its one lever measured worse
(2026-10-16o), and the two-console guest runs the same `set_stage` callers to the same values in the
same order (2026-10-16v) - the leaver here and the one-client guest differ in *when* their own clock
reaches the step, not in the code that steps them. **What is still owed, stated rather than implied:** the fork never sees the
byte-level test that selects `0x41` - the code that decides the transaction is inside the
title's own image, reached under the JIT as linked blocks, and p4's own rungs are the written
codes rather than their reason. The measurement that would settle the fork's role is the one
count between the two controls: a **three-console** no-press run, which has never been run
with the state sampler armed.

**And the rule that writes `0x41` is now read out of the cartridge's own image instead of
recognised by correlation (2026-10-16q).** The image the ARM7 runs is the cartridge's ARM7
blob relocated by exactly **`+0x3C70708`**, and that delta is proof rather than inference:
three word-for-word fingerprints taken from this run's own live code dumps
(`logs/mkdsstate5_arm7code_leave1_p*.txt`, including the `0380FFF4` literal that appears at
running `0x0600C6E0`) all land on it, so the whole driver can be read with
`arm-none-eabi-objdump -D -b binary -m armv4t --adjust-vma=0x02380000` over the blob at ROM
`0x141600` (`0x289a4` bytes). **The store site:** `0x06010AB0 mov r1,#0x41` ->
`0x06010ABC strh r1,[r0,#4]` with `r0 = *(0x0380FFF4)+0x400`, i.e. the value lands at
**`S+0x404`** - the address and the value the independent state sampler measured 10-19 frames
before each leave (`logs/mkdsstate5_delta.log`), two-way corroboration of the same read.
**The branch that guards the stamp is the driver's own TX queue:** the routine at `0x060109FC`
reserves 54 bytes from the queue at `S+0x188` (`mov r1,#0x36` / `bl 0x06009138`), and
`movs r4,r0` / `moveq r0,#8` / `beq` (`0x06010A90`) abandons the transaction when the queue had
no room - so the input that branch tests is **a slot/queue status set inside the image**, not
a frame field or cadence the link supplies. Three gates precede it, all on the console's own
state: `S+0x32E` must be 1, 2 or 3 (`0x06010A1C`), **`S+0x34C` must be >= 48** (`0x06010A40`;
measured at `0x40` = 64 on every leaver, and cleared with the transaction), and for a client
bit 0 of the request's `+0x10` halfword must be clear (`0x06010A60`). **Which transaction this
is** is fixed by the driver's own descriptor tables at the image's tail (`0x06018EB0`,
`0x06018DD8`, `0x06018FC0`, `0x06018E30`, `0x06018F08`, `0x06018E00`, `0x06018E60`, one per
type-word group, all loaded from the pool at `0x0600F528`-`0x0600F548`), 8-byte entries
`{u16 min_len, u16 len; ptr handler}`, dispatched at `0x0600F43C` (`add r2, ip, r3, lsl #3` /
`ldr r2,[r2,#4]` / `bx r2`, the entry's first halfword validating the request's own length at
`0x0600F448`): **the row this handler is reached through is `{min_len 4, len 4; 0x060109FC}` at
`0x06018ED8`** - index 5 of the group-0 table based at `0x06018EB0`, i.e. the **type word 5**
below, with the row's two halfwords being its length window rather than a `(type, step)` pair -
a management frame whose body is 4 bytes, the deauth's `00 03 00 00` reason word the fork logs,
and the frame it builds now reads too (`0x0601364C`): header, body from the request,
`strh r5,[r4,#0x2C]`, then `mov r0,#0xC0` / `strh r0,[r4,#20]`, the frame control the fork
sees.
So **`0x41` is not a waiting state: it is a code the transaction's own handler stamps while the
request is in flight**, and the siblings stamp *other* codes into the same field (`0x25`,
`0x35`, `0x53`, `0x63` at `0x0601017C`, `0x06010058`, `0x0600FF50` and `0x0600FE50`; a
clear-to-zero site at `0x06009288`), so the value names which transaction the console last
queued rather than being a `(type, step)` pair - note that the `7` stored next door at each of
those sites goes to a per-transaction record at `[+0x1C]+4`, not to this field. It is **the
record that a deauth was successfully queued**, and every gate between the request and that
record reads the console's own state or its own queue - **no link-supplied value is read there,
so no fork change is proposed for it**.

**And the site that asks for that transaction is now named out of the same image (2026-10-16r).**
The request the dispatcher consumes is a frame image: its block lives at `S+0x424`
(`0x0600F248 add r6, r3, r0`, the pool holding `0x00000424`), its `+0` is the frame posted at
`S+0x200` (`0x0600F260`, and corrected in `2026-10-16w` to what it is: the anchor of the driver's
own request *queue*, whose nodes are the images, fed from the game's mailbox at `S+0x308`), its
**type word sits at `+0x0C`** and its payload length at `+0x0E`
(`0x0600F278`), and the dispatcher picks the group from the type word's high byte
(`and r1,r3,#0xff00` at `0x0600F2C0`) and the row from its low byte. **The constructor that
raises the deauth's request is `0x060029F8`**, and it settles what the type word is: it zeroes
the header, writes `+0x0C = 5` and `+0x0E = 4`, copies a 6-byte address in at `+0x10`, writes the
caller's third argument at `+0x16`, and echoes `{type, len}` at `+0x18` - the echo the dispatcher
checks against the type word before it dispatches (`0x0600F2A8`, error 13) and applies the row's
length window to (`0x0600F448`, error 4). So the row above is **index 5 of the group-0 table at
`0x06018EB0`** (the pool literal at `0x0600F530`): `0x06018EB0 + 5*8 = 0x06018ED8`, which is why
that row's `(4,4)` is its length pair and not a type. **Six sites call that constructor**, all
inside the driver's own session and peer machinery: `0x06002FB0` and `0x06003018` (both in the
function at `0x06002DF0`, both with reason 3), `0x06003730`, `0x06004C24`, `0x06004E94`, and
`0x06007DA4` - that last one passing **19**, so the reason on the wire names which caller fired
rather than being a constant of the family, and the `reason 0003` the fork logs is one of the two
sites in `0x06002DF0`.
**What drives it is the console's own stage and its own role, not anything received.**
`0x06002DF0` asks two questions of the driver's own API - a *type-`0x0308` request*
(`0x06002084` -> the generic builder `0x06001FC0`) and a *type-`0x0284` one (`0x0600213C`) - and
branches on both. The two handlers are pure accessors of the same state block: type `0x0308` is
`0x06012314`, whose whole body is `ldrh r0,[*(0x0380FFF4) + 0x300 + 0x4C]` stored back as the
result (`strh r0,[r1,#6]`), and type `0x0284` is `0x06011528`, which returns
`S+0x32E` the same way. So the two inputs are **the console's own stage (`S+0x34C`) and its own
role (`S+0x32E`)**: stage 48 or 64 takes the deauth path (`0x06002F64`; the MAC comes from the
driver's own RAM structure at `+0x186`, reached through the global at `0x06019088`, not from a
received frame), the request is built **twice in a retry loop** (`cmp r8,#2` / `blt` at
`0x06002FE8`, retrying on statuses 7 and 12, writing 3 into the driver's own block when the
request is accepted), and role 2 or 3 chooses that per-peer form while role 1 (the host) takes
the broadcast form (`6 x 0xFF` at `0x06003000`) - both with reason 3; stages 0, 16, 17, 18 and 32
take other branches, so the deauth is issued once this console's own stage is the coupled one.
**Both fields are measured, not inferred:** the sampler's own bracket puts the host at `0x40` on
`S+0x34C` from its first sample and at `0x0001` on `S+0x32E`, with each guest `0x00` -> `0x20` at
its own join -> `0x40` as the room coupled -> `0x00` with its own deauth and `0x0002` on the role
(`logs/mkdsstate5_delta.log`, `base+34C` and the high half of `base+32C`) - host 1, clients 2, the
division the code branches on. And the handler that sends the deauth gates on the same stage at
`>= 48`. It is **read at 30 sites across the driver**, and **the "written at none" that stood
here is corrected by the writer the store watch caught (2026-10-16u)**: 30 resolved loads
(the earlier 32 was that pass's form-specific count; the rescan below also resolves split adds,
stack-parked bases and register-register adds), no direct `[rX, #844]` access at all (the only
`#844` in the image is the pc-relative offset of one pool load), and no store the passes' base
tracking could resolve - read-honest, write-wrong: **the listing does contain the store**, at
`0x0600A9F4 strh r5,[r4,#8]` with `r4` set in a prologue no pass here followed, which is what the
next record shows. The previous
record's claim that "the
driver writes `0x30` (48) into it at a session milestone" is corrected here rather than repeated:
`0x060087C0 mov r1,#48` / `strh r1,[r0]` writes the **IO register `0x04000206`** (the pool literal
at `0x06008910`), not `S+0x34C`. **The console that leaves is running the driver's own handler for transaction 1 of the
second family:** `0x06002DF0` is entry 1 of the 68-pointer table at `0x060189A0`, indexed by the
dispatcher at `0x06001F50` (`ldrh r9,[r0]`, the `0x8000` in-progress bit stripped, `cmp r9,#44`,
`ldr r1,[r5,r9,lsl #2]` / `bx r1`, then the transaction re-marked at `0x06001FA0`) - so the leave
is a step of the title's own state machine reacting to its own stage reaching the coupled level.
**A received frame, an AID, a mask and our poll rotation are read nowhere on the path from that
stage to `reason 0003`**, and the earlier pass's log evidence stands beside it: the same roster
burst went to all three guests with p2/p3 polled to `seq 6270` afterwards (2026-10-16l), the
guests differing only in their own counts since joining, and the response path was cleared round
by round (2026-10-16m), where the variable that ranks the console which leaves is the guests'
games being out of step with the host's rounds rather than this code - and it is not the console
count, since every console in a dying four-console room can sit at the same held share
(2026-10-16s). **What a four-console MKDS session would need, said
plainly:** the last joiner's own driver must not take the deauth path once its own stage reaches
48/64 - i.e. the game's own peer/room bookkeeping (`0x06002DF0`'s peer table and the stage field
it reads) has to keep the peers it walked as joined - and since nothing the fork delivers is read
on that path, **no fork change is proposed**; the field-level answer to the question asked is that
the request is the title's own decision on state it already holds. One reading from the previous
pass is corrected inline rather than repeated: `0x11 -> 0x21 -> 0x31 -> 0x41` is not a retry
ladder, it is the record of the *management transactions this console performed*, the last of them
the deauth. One correction to the older reading comes
from the same logs: the host's poll mask loses a client's bit **at** that client's deauth
(000E -> 0006 exactly when p4 deauthorizes, and again later for p2), so the permanent
narrowing is the effect rather than the cause - the `0004`/`000A` excursions are the host's own
rotation, a handful of rounds each.

**And the route now closes the session it launched** (`--attach` excepted). A route that
returns while four emulated consoles keep running at full tilt is how a stale session reaches
the next pass - measured here, when a four-console hold was still up and still logging **15
minutes** after its own route had returned. `main()` now quits it and writes the supervisor's
own exit status and save digest into the timeline (`logs/mkds2p_hold5_route.log`: `shutdown:
[supervisor] app pid ... exited with status 0` / `sav ... unchanged`). Two things about that
were wrong first and are recorded in the code: a fixed two-second wait read the app log 12 ms
before the supervisor's lines arrived, and the quit's own reply can never arrive - the app
closes the control connection as it exits - so the closed connection is the expected outcome
and the log is what proves the session died. Evidence: `logs/mkdsleave3_wifiio_leave1_p*.txt` (the register table
at the leave), `logs/mkdsleave3_pctrace_leave1_p*.txt`, `logs/mkdsleave*_code_*.txt` (the
code under the traced PCs), `logs/mkdsleave3_app.log.gz`.

**Re-run, and reproducible: the no-press arm and the instrument are now in the durable
route** (`tools/scripts/mkds-4p.py --hold N [--trace-leave]`), so the experiment that
corrected the press's premise does not depend on a scratch driver again.

### 2026-10-16t — the stage field's region is settled from the emulator's own map and the live pointer, and the rescan that found no store to it (refuted below, 2026-10-16u)

**The address and the region, from the emulator rather than from the code's arithmetic.** The
state block the driver reaches through the fixed pointer at ARM7-WRAM slot `0380FFF4` holds
`0380AFB0` on all four consoles (`logs/mkdsstate5_app.log.gz:13715-13721`, four consecutive
`mem` reads, each `cpu 7` - the ARM7 - with `words 0380AFB0`), so the field the deauth gates on
is at **`0380B2FC`** (`base+0x34C`; the same block's `+0x404` is `0380B3B4`), and the sampler's
own reads during that coupled run are the window at `0380B2B0` (`base+0x300`, its `+0x4C` the
field) - 3,840 reads, every one of them `cpu 7`. **The region is the ARM7's private 64 KiB
WRAM, and the title's ARM9 cannot touch it:** `case 0x03800000` exists in all six ARM7 access
functions (`ARM7Read8/16/32`, `ARM7Write8/16/32` - `src/NDS.cpp:2993`, `3060`, `3126`, `3190`,
`3251`, `3323`, each mapping `ARM7WRAM[addr & (ARM7WRAMSize-1)]` with `ARM7WRAMSize` 64 KiB) and
in **none** of the six ARM9 ones (`2585`, `2644`, `2704`, `2767`, `2807`, `2868`), where the
address falls to the function's final `return 0;` and a write is dropped - the same map the JIT
uses (`ARM7GetMemRegion`, `src/NDS.cpp:3372`, and `memregion_WRAM7` in
`src/ARMJIT_Memory.cpp:1193`). So the field is not remembered by the room's players and it is
not the title's ARM9 game logic either: it is state the title's own ARM7 driver holds, which
answers the either/or this question was asked as. **The live values match the code:** `0x00` on
a console before it joins, `0x20` as it joins, `0x40` with the room coupled, back to `0x00`
with its own `reason 0003` deauth, and the host at `0x40` from its first sample - `0x40` = 64,
which is what `0x06010A40` compares against 48 and what `0x06002F64` branches on while role
`S+0x32E` is 2 or 3 (`logs/mkdsstate5_delta.log`).

**The rescan: five passes, each resolving a different way of reaching the field, and all five
returned zero - a null the store watch refuted the next pass (2026-10-16u, which names the
writer at `0x0600A9F4` in this same image and says why the passes missed it).** They are kept
here because the shape of the miss matters: each models one basic block (plus registers that
survive a `bl`), and the writer computes `r4 = ctx+0x344` once in its prologue and stores
through it after conditional branches. (1) An abstract interpreter over the whole relocated
listing (`arm-none-eabi-objdump -D -b binary -m armv4t --adjust-vma=0x02380000` over the
cartridge blob at ROM `0x141600`, running = objdump `+0x3C70708`; equivalently the code's
`0x06000000` base sits at blob offset `0xF8F8`, the two numbers are one mapping) follows the
`0380FFF4` literal into a ctx register and tracks adds, subs and stack-parked bases: for the
field it reports **0 stores**, while the same pass finds the six `+0x404` writers
(`0x06010ABC` among them), the counter at `ctx+0x3EC` (`0x06009950`) and the `+0x3F4` poster -
the positive controls that say the pass sees this block. (2) A second pass resolves
register-register adds against the pool (`add r1, r1, r0` with `r0` = `0x53C`): it finds the
`ctx+0x588` writer at `0x0600A6B8` that pass 1 cannot see, and **0 candidates** for the field.
(3) Both split idioms are enumerated over the whole image rather than sampled: **180**
`add rX, r?, #768` sites, of which the **39** that store off that base within the next six
instructions land on `0x322`-`0x3FE` and never on `#76`; the **54** `add rX, r?, #836`
(`ctx+0x344`) sites the same. (4) The 59
register-indexed stores in the image are a bounded set (9 of them in the driver), and exactly
one sits on a ctx-derived base - `0x06008C38 strh r0,[r4,r1]` inside the init loop whose own
`cmp r3,#4` / `bcc` bounds the index, so its totals are `ctx+0x0` through `ctx+0x6`. (5) The
span question - whether a wider store *covers* the field without landing on it - is the same
pass asked for byte ranges instead of totals: with every store charged 4 bytes, **0** bases fall
in `0x349`-`0x34C`, against the 6 the identical question returns for `+0x404` - the control, and
the reason a zero here is a negative rather than an unasked question. The 85
call sites that hand a ctx-derived pointer to a callee are enumerated too (offsets `0x0`, `0x18`,
`0x188`, `0x194`, `0x1a0`, `0x1f4`, `0x200`, `0x23c`, `0x300` x2, `0x31c`, `0x324`, `0x344`,
`0x384`, `0x3a4`, `0x3a8`, `0x500`, `0x660`, `0x690`); none passes the field's address, the
single `ctx+0x344` hand-off (`0x0600BD94`) goes to a copy routine that *reads* from it into a
caller-supplied destination, and the two `ctx+0x300` hand-offs are a table routine whose own
stores are at `ctx+0xBC` and its 8-byte rows above it (`0x06008A84`), and one that only builds
stack arguments for another call (`0x0600ECB0`). **The store that moves the field is therefore not an instruction in this
image, and the rescan is inconclusive about what does move it:** a writer must exist (the
transition was measured), and the mechanisms left are a store through a pointer the linear pass
cannot follow - a base *returned* by a callee, which no pass here models - or the title's own
ARM7 library entry thunks, which are `ldr pc,[pc,#-4]` veneers into WRAM (`0x037F888C`,
`0x037FA564`), i.e. code the title runs that is not this image. **What it would take to name
it - and what did name it the next pass (2026-10-16u):** a write watch on `0380B2FC` read back
with `MD_NOJIT=1`, because the fork's own instrument documents that it cannot see a play store
otherwise ("The JIT writes main RAM on its fast path, so a session started with `MD_NOJIT=1` is
the one where this sees every store", `tools/appctl.py` `cmd_memwatch`) - RAM regions are
compiled to direct mapped writes (`GetFuncForAddr` returns nothing for main RAM and WRAM7, the
ARM7 branch covering only `0x04000000` IO and `0x06000000` VRAM) while `RecordMemWatch` is called
only from the six interpreter write functions (`src/NDS.cpp:2773`-`3325`). The run was taken and
the answer is in that record; what the paragraph above got right is the residual and wrong is
that it is where the story ends.

**No fork change is proposed, and nothing about this pass needs a rebuild or a live run.**
The deauth's own guards - `S+0x34C` >= 48 (`0x06010A40`), role `S+0x32E` 1/2/3 (`0x06010A1C`),
the driver's own TX-queue status (`0x06010A90`), the request's own `+0x10` bit for a client
(`0x06010A60`) - read only state this console holds, in its own private WRAM, and the arching
`0x06002DF0` reads the same block through two accessors of the driver's own API. **What a
four-console MKDS session would need is unchanged:** the last joiner's own driver must not take
the deauth path once its own stage reaches 48/64 - the game's own peer/room bookkeeping
(`0x06002DF0`'s peer table and the stage field it reads) has to keep the peers it walked as
joined - and no leave of any console on that path reads a received frame, an AID, a mask or our
poll rotation.

### 2026-10-16w — the walk's other operands are named: the request image is one of the console's own queued commands, and the game's own mailbox wrote every field the guards read

**The gap this closes.** `2026-10-16v` attributed the walk's five `set_stage` calls to their callers and stopped
at three unnamed operands: `[r7+24]` in the 48-writer, `[r8]` and `[r7+4]` in the `0x31` completer, and
`[r4+2]` in the 64-writer. All four are now named, from the cartridge's own image and one live store watch.

**The image, read out of the dispatcher and corroborated live.** The dispatcher (`0x0600F234`) takes its
request from `*(S+0x200)` (`ldr r0,[r3,#512]`), records it at `S+0x424` (`str r0,[r6]`, with the in-flight
flag at `S+0x428`), then reads **the image's own** type word at `+0x0C` and length at `+0x0E`
(`ldrh r3,[r0,#12]` / `ldrh r3,[r0,#14]`), computes the handler's second argument as `image + 0x10 + 2*len`
(`add r3,r0,r3,lsl #1` / `add r5,r3,#16`), compares the halfword there against the type word (`cmp r3,r1`,
error 13) and dispatches the row for `type & 0xFF` (`and r1,r3,#0xff`, `ldr r1,[r5,r9,lsl #2]`). So an image is
`{+0: prev, +4: next, +0x0C: type, +0x0E: len, +0x10: payload (2*len bytes), +0x10+2*len: echo of the type
word, after it the status/errno out-area}`, the payload length is in halfwords, and the walk's guards read the
*payload*, not the console's state block. The live trace of the 48-writer agrees field for field: its argument
`0x06019EEC` with `0x06019F06 = image + 0x1A` is `len = 5`, and the values it compared are `[+0x10] = 0x0900`,
`[+0x16] = 1`, `[+0x18] = 0x7D0 = 2000` (`logs/mkdschain1_pctrace_holdend_p2.txt`).

**Which row the 48-writer is.** Its handler address occurs **once** in the image, at `0x06018EE4`, i.e. in the
8-byte row `{min_len 5, len 3; 0x06010914}` at **`0x06018EE0`** - **index 6 of the group-0 table at
`0x06018EB0`, so the request's type word is 6** - and the method is validated by the sibling the record
already knows: the deauth's own `{min_len 4, len 4; 0x060109FC}` sits at `0x06018ED8`, exactly as
`2026-10-16r` read it. The pool the record quoted (`0380FFF4` at `0x0601090C`, `0x404` at `0x060109F8`)
belongs to the *tracker* (`r4 = S + 0x404`), while `r7` is the handler's own first argument, the image. **The guards, in order:** role `S+0x32E` in `{2,3}` (`0x06010948`), stage `S+0x34C >= 48`
(`0x06010960`), payload `[+0x10]` bit 0 clear - a unicast destination rather than the `6 x 0xFF` broadcast
(`0x06010970`) - `[+0x16]` in `[1,255]` (`0x06010980`, `0x0601098C`), and **`[+0x18]` in `[10,2000]`**
(`0x0601099C`, `0x060109A8`) whose live value is the fence itself, `2000`. On success it calls
`set_stage(48)`, calls the radio-reset routine `0x0600AA54`, records `[+0x16]` into `S+0x3B4`/`S+0x3B6`,
remembers the image and its out-area at `S+0x41C`/`S+0x420` and stamps **`0x50`** into `S+0x404`.

**The other three operands.** `[r8]` is **`S+0x404`**, the driver's current management-transaction code: both
completers load the pool literal `0x404` into their base register (`0x06016634` -> `0x00000404`, `0x060172F0` ->
`0x00000404`, read out of the image's pool), and `[r8+24]`/`[r8+28]` address the very words the 48-writer stored
at `S+0x41C`/`S+0x420`. They compare it against **`0x31`** (`0x06016824`) and **`0x51`** (`0x06017310`), the
deauth's completer (`0x06014F90`) against the **`0x41`** `2026-10-16q` read out of the deauth handler - the codes
of the transactions these completions close. `[r7+4]` and `[r4+2]` are payload fields: both functions build
their frame pointer as `arg0 + 0x2C` (`add r7, sl, #0x2c`, `add r4, r7, #0x2c`), so they read **`[image+0x30]`**
(guarded `== 0`, and masked with the pool literal `0x00000FFF` in the 64-writer) and **`[image+0x2E]`** (guarded
`== 0`; the `0x31` completer also tests it `== 1` and `== 2`). The same payload carries the rest of the family's
shape: a 6-byte address at `+0x10`, the `+0x16` value, and a **name string at `+0x1E`** the 64-writer copies
into `S+0x3C6` with its length at `S+0x3CC` (`add r1, r7, #30`, `bl` copy, `bl` strlen, `strh r0,[r6,#136]`).

**Who writes them: the console's own game, through the driver's mailbox.** The store watch on the interpreter
(`MD_NOJIT=1`, the 4-console route attached, `logs/mkdschain3_probe_stdout.txt`) fired on `S+0x200` **1,335
times** and named exactly two sites, alternating (`logs/mkdschain3_watch_p2_post.txt`): **`0x06008FF0`**
(`streq r4,[r5]`, `r5 = 0x0380B1B0`, `r4 = 0x06019F28`) and **`0x06008EA8`** (the same anchor reset to
`0xFFFFFFFF`). Both are the ARM store *behind* their record: the watch stores the CPU's own `R[15]`, which for an
ARM instruction is the instruction's address + 8, so the rows read `@pc 06008FF8` / `@pc 06008EB0`. Reading them in the image says what they are: `0x06008FC8` is a **linked-queue append**
(`node->prev = tail`, `tail->next = node`, `node->next = -1`, count at `anchor+8`) and `S+0x200` is that queue's
anchor, with the request images as its nodes. **That routine has one caller**, `0x060089A0` inside the submit
routine `0x06008980` (`add r0, r0, #0x200` from `*(0x0380FFF4)`), and the submit routine has exactly two callers,
`0x060089E4` and `0x06008B98`, and **both read the driver's own mailbox `*(S+0x308)` and hand the message to the
queue** (`ldr r0,[r0,#776]`, `bl` the dequeue, `bl 0x06008980`). The dispatcher is called from the same reader
(the block copy at `0x0600F264` carries `lr = 0x06008BF8` in `logs/mkdschain3_watch_p3_blk.txt`), and the register
dump at the enqueue shows the interrupt context that explains the shape (`r2 = 0x04000210` IE, `r12 = 0x04000208`
IME parked). So **every field the walk's guards read is a parameter the console's own game put in the ARM9->ARM7
mailbox and the title's own ARM7 library parsed into an image** - not a received frame's field, not a register
the fork emulates, and not a counter the fork's delivery cadence sets. Nothing on the path is fork-supplied; the
only fork involvement is upstream of the game's *choice* of parameters, which is the causality the record
already names (the guests' own clocks, `2026-10-16b`) and which is why the timing spread is the title's.

**And the live four-console act that carries it** (`logs/mkdschain3_route.log`, `logs/mkdschain3_probe_stdout.txt`,
route exit 0, save unchanged, `MD_NOJIT=1`, nothing set): the create pair and the overlap join took all four
consoles onto the bus - `mask=0xf` at frame 11327, host `role=1 handed=0118/38b`, each joiner `role=2` - so the
room held four distinguishable screens (the route reads each console's own page, `section=True joined=[2,3,4]`,
end shot `logs/shots/mkdschain3_end.png`), and then, with no input at all, **the guests left in the order their
own clocks chose**: `0xf -> 0xb` at frame 11458 (131 emulated frames after coupling, leaver 3), `0xb -> 0x3` at
11511 (184, leaver 4), `0x3 -> 0x1` at 11524 (197, leaver 2), the host alone with `handed=00C0/38b` against each
guest's `0080/72b` and each console's own page (`logs/mkdschain3_route.log`: p1 `8BCAAB41`, p2 `A357BE41`,
p3 `ABAA8DF0`, p4 `898D1E57`). That is the same walk at the same callers with the same values, now with its
operands named end to end - **with one honest caveat about this run's instrument:** its own trace ring over the
walk's window reads `0 visit(s)` for the coupling-to-first-leave span (`logs/mkdschain3_pctrace_leave1_p3.txt`,
frames 11335-11459), because the route arms the trace at each leave and this run's walk is the title's own clock
rather than the same frame every time; the caller list is `2026-10-16v`'s run (`logs/mkdsattr4_*`), and what this
run carries is the operands' writers (the store watch), the coupling, each console's own radio state and page,
and the three leaves.

**What an MKDS local session would need, said plainly as of this pass.** Told in the same one line the record
already carries and now with no unnamed operand behind it: the guests' own drivers have to stop taking
their deauth/walk path once their own stage reaches 48/64, i.e. **the game's own peer/room bookkeeping has to
keep the peers it walked as joined** - because every condition on that path is now read as the console's own
state block or its own queued command's payload, with no received frame field, AID, mask, emulated register or
delivery cadence anywhere in it. **No fork change is proposed and no further exploratory run is owed for this
question**; the remaining work is the game's own exchange, not a fork knob. (**Run and confirmed, `2026-10-16x`:** the
one run that clause left owed - a coupled room driven through each console's *own* screen - found a client's page draws
no control at all and the guests' own leave in the same window as this section's no-press baseline, so the clause stands.)


### 2026-10-17 — every console now advertises itself as its player, not as `melonDS`

**What was wrong, and the one identity still shared.** The per-console MAC (2026-09, `apply_console_mac`) made the four
consoles distinct on the air, but the *name* stayed the firmware's: one image booted four times means `melonDS` four
times, and a title's player list is exactly that string. The bridge now sets the firmware nickname per slot -
`Player 1`..`Player 4`, eight characters inside the field's ten - for every slot including the first, and the
Download Play stand-in reports the same name (`dp_frames::player_name`) instead of a hard-coded one, so a console does
not rename itself across the hand-over.

**Measured, at four consoles, in the guest's own memory.** Four consoles, Mario Kart DS, one session
(`logs/md_app.log`): the bridge logged `player1..player4: wireless MAC ... user name 8 chars`, the session logged
`console names: Player 1, Player 2, Player 3, Player 4`, and the ARM9 user-settings block each guest reads was dumped
at `0x027FFC84` on all four consoles - the UTF-16LE nickname is `50 00 6C 00 61 00 79 00 65 00 72 00 20 00 31..34 00`
(`"Player 1"` .. `"Player 4"`) with `NameLength = 8` at `0x027FFC9A`. That is the copy `SetupDirectBoot` stages from
the firmware at `0x027FFC80`, i.e. the bytes the game itself reads; it is **not** an observation of a title's lobby
listing four names, which is still owed (see the open item below).

**What this does not prove.** That a given game *prints* the nickname in its room list differs per title, and no game
screen was read this pass. The evidence is the firmware field and the guest's RAM copy of it. A lobby that still shows
one name four times after this change would mean that title takes the name from somewhere else (the host's own
broadcast, for MKDS: see 2026-10-16-era entries), and that would be the next thing to read - not a silent failure of
this change.

### 2026-10-16x — the coupled room is driven from each console's own screen: a client's page offers no control at all, and the guests' own leave comes in the same window as the no-press baseline

**What this pass tests, and why it is a drive rather than an instrument.** Every run so far pressed the host's *known*
coordinates (CUT OFF, VS, A) and nothing at all on the guests, so the plainest explanation for the guests' own walk to
48/64 and deauth 130-200 emulated frames after coupling (`2026-10-16w`) had never been tested: the title wants an input
nobody sent. The durable route grew one arm for it, `tools/scripts/mkds-4p.py --press-room`: at the coupling it reads
each console's **own** touch screen, finds the control *that* console is drawing and presses its centre - joiners first,
because their leave is the question - with every press gated on that console's own bus bit and every wait on the app's
emulated frame clock. Two four-console runs, shipped JIT, no knob set, route exit 0, save unchanged, app exited 0:
`logs/mkds4p_pressroom_stdout.txt` / `..._route.log` and `logs/mkds4p_pressroom2_stdout.txt` / `..._route.log`.

**What each console's own screen showed before the press** (run 2, frame 11550, `mask=0xf`, host `role=1 handed=0118/38b`,
three joiners `role=2`). The reader lists every text band it finds on the bottom screen and says why each one was or was
not measured, so a screen that drew nothing pressable is distinguishable from a control that did nothing - which is the
whole point of the arm:

* **p2, p3, p4 (the joiners): no control at all.** Each drew two bands and neither is pressable: `y0..145 (h146, score 59)`
  is the racer list, above the lower half the reader looks at, and `y171..175 (h5, score 15)` is five rows tall - under a
  line of the DS font, i.e. a striped panel rather than a label. Nothing was pressed on any of them, and the two runs read
  the same two bands on all three - the joiners share one page within a run (`958FFD1D` for p2 and p3 against p4's
  `5FA369E7` in run 2, `A357BE41` for all three in run 1, each of them the page its own bottom digest was read on).
* **p1 (the host): one control, and the press was received.** Its band `y163..177 (h15, score 44)` reads as dark ink with
  6 word-width runs, the widest word cluster at **x 114..182**, so the arm pressed **(148,170) of its own 256x192** -
  inside the CUT OFF patch (`84,151,132,41`, tapped at (149,167) by every earlier pass) and inside the run's own
  `CUT_OFF_TAP`. **The coordinate is derived from that screen's pixels; nothing in the route told it where to press.**
  The press landed: the host's page moved `37CDC38A -> 35860921`, and the page it moved to (the pre-race wait) has no
  bands at all. Run 1 repeats it at the same coordinate.

**The reader was checked against screens whose controls the record already knows.** On the same run's joiner group-list
shot (`logs/shots/mkds2p_presssmoke1_the_group_list_a_joiner_is_standing_on_p2.png`, from a two-console smoke run of
 the same arm) it picks the bottom band `y152..164`, reads bright ink with 12 word-width runs and a widest cluster at
**x 91..151**, and would press **(121,158)** - against the route's own `CREATE_TAP = (128,157)`, the create control this
file named from that page's glyphs in `2026-10-16h`. So the arm re-derives two of this title's known controls from pixels
alone, and it *rejected* a third candidate rather than pressing it: on the joiner's coupled room page the only band in the
lower half is 5 rows tall, and its stripes are what a first version of the reader read as two letters (a press at
(283,173), i.e. off the console's own 256 columns, in the smoke run's log) - which is why the height floor and the
screen-local coordinates are both there.

**And the guests left anyway, inside the same window as the no-press baseline.** In run 2 the whole pass - three joiners
read and skipped, one host read and pressed - took **88 emulated frames** (frame 11550 -> 11638), and the bus read
**`mask=0xd` immediately after it**: p2's own bit was already gone, so its own leave had begun within ~90 frames of the
arm's start, inside the recorded no-press window (131/184/197 in `2026-10-16w`). The three after-reads then opened at
`mask=0xd`, `0x0`, `0x0` and found p3's own game handing **`fc=00C0/38b`** - the deauth frame shape this file names
(`2026-10-16q`) - p4 `0080/72b`, the host also `00C0/38b`, and all four at `mask=0x0`. The arm's own watch, which starts
after those reads, first saw the whole group off the bus at frame 12145 (595 emulated frames after the first press,
`leavers (1,2,3,4)`) and ended at 12151. Run 1 agrees frame for frame in shape: presses in 88 frames at
10927 -> 11015, every after-read already at `mask=0x0`, the teardown first observed at 11516. **So driving the room with
every control its consoles' own screens offer does not move the stop.**

One asymmetry in the arm's own reporting, stated rather than left implicit: a console whose screen drew a control is read
on *both* sides of its press (bands before, bands again after the page settles), while a console with nothing to press is
read for bands on the before side only and its after side is its own page digest and radio state (`p2 press: 958FFD1D ->
1BF34324 with nothing pressed on it`). That is enough to tell the two cases apart - a control that did nothing reads
`--its own control (x,y)--> <same digest>` - but it is a digest and not a band list, so a control that *appeared* on a
client's page during the pass would be visible in the report only as a page change.

**What that leaves, stated as plainly as the record's other MKDS conclusions.** (a) The guests' own pages draw **no
control** at this stage - a client in a coupled MKDS room has nothing pressable of its own to press, so "the title wanted
an input the route never sent" is not an input this room has on the client side; the only control any of the four consoles
draws is the host's own. The reader's scope is stated rather than implied: the lower half of the touch screen, and
labelled controls (text) - an unlabelled or upper-half control would be reported as "no control" by this arm, and the
band list with each band's reason ("above the lower half", "h5 under a text line") is what makes that a measurement
rather than a gap. (b) The host's own control *was* pressed and answered (its page moved), and the guests still left on
their own clocks. So the title's own stop stands with the interaction ruled out, exactly as the fixed-coordinate arm's
conclusion already said, now with the guest side actually driven rather than only held. **No fork change is proposed,
no knob is involved, and the run's own artifacts carry every number above.**

**Carried forward:** the route keeps the arm (`--press-room`, alongside `--hold`), and it is the arm any later title can
be driven with: it needs no coordinates, only the console's own screen. What it does not yet do is press on a console
whose page draws a control *after* the coupling (its one pass is the window this question is about). Evidence kept:
`logs/mkds4p_pressroom_stdout.txt`, `logs/mkds4p_pressroom2_stdout.txt`, their `_route.log`s and `_app.log.gz`, the
smoke run's timeline (`logs/mkds2p_presssmoke1_stdout.txt`, `..._route.log`) and the group-list shot above; the other
shots those three runs left are regenerable and were dropped.


### 2026-10-16v — the stage walk is attributed to its callers, and the two-console arm leaves too: the count changes *when*, not *whether*

**The instrument, and why the window is the whole function.** `MD_NOJIT=1` (the recorder hooks the
interpreter's dispatcher, so under the shipped JIT it would record linked blocks instead of
instructions), the window `--arm7-lo 0600A970 --arm7-hi 0600AA04` - the cartridge's own `set_stage`
from its prologue (`push {r4,r5,lr}` / `mov r5,r0` / `ldr r0,[pc]` -> `0380FFF4` / `add r4,r0,#0x344`
/ `ldrh r0,[r4,#8]` / `cmp r0,r5` / `beq` early return) to its epilogue - armed before the walk on
every console (`--trace-early`). Nothing between the entry and the switch touches `lr`, so **every
row at `pc=0600A970` is exactly one call**, carrying `lr` = the caller's return address and `r0` =
the requested stage. That is the attribution the static passes could not make and the store watch
never carried, and it is read here off the route's own dump (`tools/scripts/mkds-4p.py --trace-leave`).

**Four consoles, nothing pressed, coupled and then left alone** (`logs/mkdsattr4_route.log`, probe
stdout `logs/mkdsattr4_probe_stdout.txt`): coupled at frame 11310 (`mask 0xf`, host `role=1` handed
`0118/38b`, every joiner `role=2`), the bus left `0xf` for `0x1` at **frame 11636, 325 emulated
frames after coupling**, leavers `(2,3,4)`. The traces separate the two roles cleanly:

* **the host (p1, 195 calls)** never leaves the steady loop: `06009EEC` x97, `060124DC` x33 and
  `0600A374` x32 (all `0x20`), `06012548` x32 (`0x10`), and **one** `0600A220` -> `0x40`, the stage
  its own create writes. It is the same list in the two-console run below.
* **every guest (p2, p3, p4 - 249 calls each, and element-for-element identical to each other)**
  runs that loop and then the walk, in this order: `060124DC` -> `0x20`, `06010C70` -> `0x20`,
  `0600A2D4`/`060102FC` -> `0x20`, `06010BD0` -> `0x20`, **`06016848` -> `0x30`**, **`060109BC` ->
  `0x30`**, **`060173B0` -> `0x40`**, `06014F8C` -> `0x20`, `06009EEC` -> `0x20`, `06012548` ->
  `0x10`, `06009EEC` -> `0x20`, **`06012594` -> `0x00`**. Seven stores each: a call whose value
  equals the field returns early at `0600A990` and never reaches the store, so 7 stores are the
  7 transitions, the walk included.

**What each walk caller's decision depends on: the console's own state block.** The callers that
close the room are the driver's own management steps, and every one of them reads the console's own
memory. The addresses come from the cartridge's own ARM7 blob (`arm-none-eabi-objdump -D -b binary -m
armv4t --adjust-vma=0x02380000` over the blob at ROM `0x141600`, the reading `2026-10-16q`
established), re-anchored in this pass against the live code the previous pass read: those live bytes
at `0600A9A0` occur **once** in the ROM, at `0x15B898` = blob VA `0x0239A298`, which fixes
`blob = live - 0x3C70708` (entry `0x0600A970` = blob `0x0239A268`). A scan of the blob for calls to
that entry decodes **24 ARM `bl` sites and 0 THUMB**, 23 passing a literal and one - `0600E638` - a
register:

* `06016844` -> **48**, guarded by a helper returning non-zero, `[r8]==0x31` and `[r7+4]==0` - `0x31`
  is one of the transaction codes the driver stamps into its own request block (the `0x35`/`0x41`/
  `0x53`/`0x63` family). **Named in `2026-10-16w`:** `r8` is `S+0x404` itself (the pool literal
  `0x404`), `[r7+4]` is the request image's payload halfword at `+0x30` (`r7 = arg0 + 0x2C`).
* `060109B8` -> **48**, guarded by `[r7+24]` in `[10, 2000]` (`r7` is that function's own argument),
  with the function's pool holding **`0380FFF4`** and **`0x404`** - the console's own block pointer
  and the management-transaction offset a leaver's own deauth stamps `0x41` into. **Named in
  `2026-10-16w`:** the `0x404` belongs to `r4` (the tracker), while `r7` is the handler's first
  argument, the request image, so `[r7+24]` is its payload halfword at `+0x18` (live `0x7D0`).
* `060173B0` -> **64**, guarded by `[r4+2]==0` on a structure the function builds itself, after
  `mov r1,#64; bl 0600AD78`. **Named in `2026-10-16w`:** that structure is not built by the
  function - `r4 = arg0 + 0x2C`, so `[r4+2]` is the request image's payload halfword at `+0x2E`
  (and `[r4+4]`, its neighbour at `+0x30`, is masked with `0x00000FFF`).
* `06012590` -> **0**, guarded by the stage field itself (`[*(0380FFF4)+0x34C] == 0x10`), and its live
  row carries the compared value on every leaver: `r1=00000010`.

The live rows also confirm the pointer the code only implies: at the entry of the `060109BC` call
every leaver holds **`r1=0380AFB0`**, which is `*(0380FFF4)` - the console's own block - and the
2-console session below reads that slot live as exactly `0380AFB0` with `ctx+0x344 = 0380B2F4`
(`logs/mkdsattr2b_state_p2_0380FFF4.txt`, `logs/mkdsattr2b_state_p2_0380B2F4.txt`). So no received
frame, AID, mask or cadence of ours appears as an input on the walk: the value is the caller's own
literal and the condition under it is the caller's own block. (`2026-10-16w` finishes this: the two
operands the record left unnamed are payload fields of the console's *own queued request image*, whose
writer is the driver's mailbox reader - the game's own command parameters, still nothing delivered.)

**The comparison the record never had: one client, same reading.** The two-console session in this
pass (`logs/mkdsattr2c_*`, the durable route alone, one command) coupled at frame 6864 and **left its
guest at frame 7009** - `*** BUS LEFT 0x3 at frame 7009: mask=0x1 (145 emulated frames after
coupling), leavers (2,)` - with the leaver's last traced call the same `lr 06012594` (the `0` writer)
the four-console leavers end on, and **its `set_stage` call list element-for-element identical to
theirs**: same callers, same values, same order, 249 calls, 7 stores
(`logs/mkdsattr2c_pctrace_leave1_p2.txt` against `logs/mkdsattr4_pctrace_leave1_p2.txt`). The probe
run in the same pass (`logs/mkdsattr2b_*`, the route attached, 700 emulated frames of hold) adds the
live state read on the non-leaving side of that window: the guest's own deauth is exactly one
`tx frame: slot 2 (LOC2) fc=00C0 len=30 seq=33A0` (`logs/mkdsattr2b_app.log` 50231), its digest
records `wireless on/off 2/2, deauth out/in 1/0, role 2` (50312), the mask went `0x3 -> 0x1` in the
route's own post-hold watch (`watch 4..6 (mask=0x1)`, `VERDICT: section=True joined=[2] mask=0x1`),
and the block at the end is the same shape the leavers end on - `*(0380FFF4) = 0380AFB0`, stage `0`,
`role 2` (`logs/mkdsattr2b_state_p2_0380FFF4.txt`, `..._0380B2F4.txt`). So **the reading does not
depend on the console count at all**: one client and three write the same stages through the same
callers, and the leave happens at both counts. What is *not* pinned by this is the timing - 145
emulated frames after coupling here, after a 700-frame hold in the probe run, 107-167 and 325 across
the four-console runs, and the record's own oldest floor for a guest left waiting was 2,740 - so the
count does not set a fixed clock either, and the run-to-run spread is the title's own.

**That corrects two claims of the previous record, in place.** (1) "Two consoles hold" is a *window*
statement, not a property of the room: `logs/mkds2p_hold2_route.log` - already on disk from an
earlier pass - ends its own watch at `mask=0x1` with `VERDICT: ... mask=0x1`, the same leave this
pass reproduced inside a 1,400-frame hold, and the runs that reported `bus steps=[]` (hold, hold5)
simply stopped watching before it. (2) The count is not "the trigger" for the walk: at one client
and at three the same callers write the same values in the same order. **What a four-console MKDS
session needs is unchanged** (the title's own peer/room bookkeeping has to keep the peers it walked
as joined), and because no input of ours takes part in the walk and the same walk runs with one
client, **no fork change is proposed and no further exploratory run is owed for this question.**

**And the route reads this without a probe now.** `hold_room` dumps the traces at `HOLD END` as well
as at a bus change (`tools/scripts/mkds-4p.py`), because a hold that ends with the bus still up is not
a hold that never ends: the leave can land after the hold, and the first version of the probe above
lost a whole run's trace by calling `pctrace` *with* the window, which re-arms the recorder and clears
its ring. The comparison is now one command -
`tools/scripts/mkds-4p.py --players 2 --hold 1400 --trace-leave --trace-early --arm7-lo 0600A970
--arm7-hi 0600AA04` - and the `holdend` dump it writes is exercised in `logs/mkdsattr2c_*`
(`..._pctrace_holdend_p2.txt`, 3,072 rows; p1's own says `0 visit(s)`, which is what a recorder
re-armed at the leave and asked for a rare window since then should say).

### 2026-10-16u — the stage field's writer is caught: the cartridge's own `set_stage` stores it from the image, its own WRAM memset clears it, and no fork content is involved

**The instrument: the store watch on the interpreter, armed before the walk.** Probe in `/tmp`
(`fieldwatch.py`), durable route unchanged: the session starts with `MD_NOJIT=1` (the fork's own
`memwatch` help says a shipped-JIT watch reports a false zero), the watch is armed on all four
consoles - the field exactly (`0380B2FC..0380B2FD`) on p1/p2/p4, the field plus its neighbours
(`0380B2F8..0380B303`) on p3 - with all four reporting **0 stores at arm time**, and then
`tools/scripts/mkds-4p.py --attach --hold 400` drives the walk. It coupled four consoles (`mask
0xf`, every joiner `role=2 handed=0218/40b`, `p1` hosting) and reproduced the leave: the bus left
`0xf` at frame 12659 for `0xb` (**p3**, 207 emulated frames after coupling) and for `0x1` at
12772 (p2, p4). That is the first reproduction of the leave on the interpreter, same shape as the
JIT runs. Reports: `logs/mkdsfield_watch_p{1..4}.txt`, timeline `logs/mkdsfield_route.log`,
shots `logs/shots/mkdsfield_end.png`.

**Two store sites, three consoles identical (p1 95 stores / p2 11 / p4 11).** The reported PC is
the interpreter's `R[15]`, which is **instruction + 8 in ARM state** (`src/ARM.cpp:620`), so each
site is the instruction two words before it - and the memset hit below confirms that convention
independently, since the instruction it must be is the loop's store:

* **`0x0600A9F4 strh r5,[r4,#8]`** - in the cartridge's ARM7 image, with `r4 = ctx+0x344`
  (`0380B2F4`) set once in the prologue from the `0380FFF4` slot, so the store lands on
  **`0380B2FC`, the field**. The values it stored across the run are the stage transitions:
  `0x10`, `0x20` (join), `0x30`, `0x40` (coupled), `0x00` (cleared with the leave). **That function
  is the title's own `set_stage(new)`**: it compares the requested value with the field it is
  about to write and returns early when they are equal (which is why only transitions appear in
the watch), dispatches handlers for `0`/`0x10`/`0x40` and otherwise falls straight to the store -
  and it has **24 callers**, 23 of them passing their own decided literal (`mov r0,#0/#16/#32/#48/#64`;
  the 24th, `0x0600E638`, passes a register): `0x06014EA4` and `0x06014F88` read the field itself
  (`ldrh r0,[r4,#8]`) and advance it to 48/32, `0x06012590` reads it and drops to 0, and the `0x40`
  handler reads the **role** halfword `ctx+0x350` (`ldrh r0,[r4,#12]`) and only for role 2 calls
  `0x0600ABF8`.
* **`0x037FB220 strhlt r0,[r1,r3]`** - code the console runs **out of WRAM**, `lr = 0x0600BD98`,
  called from the image at `0x0600BD94` through its own veneer at `0x06000444`
  (`ldr pc,[pc,#-4]`, pool word `0x037FB218`) with `r1 = ctx+0x344` (`0380B2F4`), `r0 = 0` and
  `r2 = 0xC0`: the routine is a halfword fill loop (`mov r3,#0 / cmp r3,r2 / strhlt r0,[r1,r3] /
  addlt r3,r3,#2 / blt`), i.e. **`memset(ctx+0x344, 0, 192)`** - the 192-byte state block cleared,
  the field with it. p3's wider window shows the loop walking the block store by store
  (`0200B2F8`, `0200B2FA`, **`0200B2FC`**, `0200B2FE`, ... each `r3` two higher), which is also the
  positive control that the wide window works.

**Whose code: the cartridge's, byte for byte - `0x037Fxxxx` is not fork content.** The 64 bytes at
`0x037FB218` read live from the ARM7 (`logs/mkdsfieldread_wram_entry_b218_037FB218.txt`, identical
on all four consoles) are found **uniquely and byte for byte in the cartridge's own ARM7 image at
ROM `0x144AA4`, image offset `0x34A4`**. The image also *hardcodes* the WRAM entry it calls
(`0x037FB218` on disk at objdump `0x238FD40`), exactly as it hardcodes `0x037F8850`, `0x037F888C`,
`0x037FB390` and `0x037FA564` beside its own `ldr pc,[pc,#-4]` veneers - the title expects its own
code there. The fork has no `037Fxxxx` constant outside a dead debug dump (`src/NDS.cpp:2553`, from
the unused `dragonball.bin` writer, unreachable after its own `return;`), and the app's only RAM
write path is the diagnostic ROM's magic block (`src-tauri/src/lib.rs:1650`, `DIAG_BASE`), which a
cartridge session never runs. So the two mechanisms the previous record left open both resolve the title's
way: a store through a pointer block-local analysis could not follow, and code the title runs out
of WRAM - the cartridge's own in both cases.

**It also corrects `2026-10-16t`'s own null.** The five static passes returned zero for the field
because each models one basic block (plus registers surviving a `bl`), while `set_stage` computes
`r4 = ctx+0x344` once in its prologue and stores through it after conditional branches - a
function-level dataflow the passes did not have. The load count (30) stands; "written at none"
does not, and is corrected where it stood.

**Verdict: no fork change, and the question closes here.** The writer's inputs are the caller's own
stage literal, the field it already holds and, on the `0x40` path, the console's own role
halfword - no received frame, AID, mask or any cadence of ours takes part, which is the same
conclusion the leave's guards reached from the other side. **What a four-console MKDS session
needs is unchanged:** the last joiner's own driver must not take the deauth path once its stage
reaches 48/64, i.e. the title's own peer/room bookkeeping (`0x06002DF0`'s peer table and the stage
`set_stage` maintains) has to keep the peers it walked as joined. Nothing the fork delivers is
read on that path, and there is nothing for the fork to hand back - the value the title reads
where it expects session state is written by the title.

### 2026-10-16k — the teardown at its cheapest instance: the client's polls are intact and credited to its last one, and the "85% reply rate" is a four-console artifact

**Reproduced at two consoles with both consoles' own state sampled every ~15 emulated
frames across the tap** (host taps CUT OFF `149,167` on the documented route;
`logs/mkdsteardown_delta.log` is the sampler's own output, `logs/mkdsteardown_app.log.gz`
the session's app log). The sampler brackets the tap with the last clean sample
(f7377, `empty 0 / clientfail 0`) and the first after it (f7418, `empty 1 /
clientfail 1`), so the two consoles' own numbers are read either side of the
transition rather than at the end of a dead session.

**What the client's own game read, from its own log, over all 1,536 rounds it
received:** 1,185 polls `BE 01 02 00 00 00`, 333 polls `BE 01 02 00 00 80`, 7
nickname frames (`... 6D 00 65 00 6C 00 6F 00` - `melo` of the host's own name) -
every one of them with
`clienttime 446`, AID-mask byte **`02`** and AID 1. So the poll it is asked with is
never malformed, never missing and **never narrowed**: the AID-mask byte the client
reads is `02` in every round of the session, including the eleven rounds the host's
ack reported it as failed, which is the one signal the `MD_PG_NO_CLIENT_FAIL` arm
(v0.1.9cc) changed. Its own last read is the shape it had answered 1,500 times
(`seq D310`, `BE 01 02 00 00 00`); the host's command only changes shape (6 to 10
bytes, `BE 01 00 00 00 80 06 00 B8 B6`, mask `00`) *after* the client is gone.

**1,525 answers credited against 1,539 commands - 99.1% of rounds - not 85-88%.** Host `cmds tx 1539 /
acks 1539 / clientfail 11`, `collected 1525`, `empty 11`, `stale 0`,
`reanchor-dropped 10`, `blank 0`, `notlooking 0`; client `cmds rx 1536 · answered
1534 (blank 0, canned 2) · replies sent 1536`, with the app's own startup line
confirming `MD_REPLY_KEEP=1`. The four-console runs' 85-88% is therefore four
consoles sharing one frame's reply slack, not a property of the link - and the same
teardown happens at two consoles with the link all but perfect. The 11 empty rounds
are not the client's missing answers: with `keep=1` a drop is a clump's leftover by
construction, and all 10 of the trim's drops were counted as surplus for their own
sender.

**Who leaves first, with both radios' own state at the stop.** The *client's* game
writes `deauth ... reason 0003` at emulated frame 7656 (`role=2`, `pwr 0200/0002`,
`W_IF 0800`, `W_IE 0000` - its own game clearing the wireless interrupt enable -
`pc9 0200FD10`, `slot b 0000 r1 0000 r2 011A`), and the host follows 15 emulated
frames later (f7672, `deauth out 1`, `pc9 FFFF0238`, `dispcntA 00010210`, its own
command body already narrowed to client mask `00`); the bus reads `mask 0x3` to
f7656, `0x1` at f7657 and `0x0` at f7672. That is the same ordering the four-console
runs show (guest first, host 11-15 frames later) and the same client-side signature
the three-console runs recorded, at the cheapest instance and with nothing on the
wire changing for it. **What the client's last second does *not* contain:** the
`A5 A5`/`FF FF` bodies it reads are 10 of its 1,536 reads, spread right through the
session (~10-15 s apart), so they are a periodic exchange of this lobby, not a
death signature; and the host's own CUT OFF state walk never reaches the client's
poll before it leaves.

**The four-console instance re-ran to the same stop, and it adds the one number the
two-console arm cannot show.** `tools/scripts/mkds-4p.py --players 4` on the durable
route (`logs/mkds4p_run7_route.log` and `logs/mkds4p_run7_app.log.gz`, with the same
route's joins in a second sighting in `logs/mkds4p_run6_route.log`): the host's create
pair raises the room (`193382FD --(128,157)--> B9F40C89` with the radio down, the
confirm to `5FA369E7`, `role=1`, `mask=0x1`), every joiner's own list shows the group
(rows `145 87 85 86` on all three), the three bars go back to back and every join
verifies from its own state (`room taken: True`, `role=2`, handed `0218/40b` /
`0228/42b`), and the bus holds **`mask 0xf` with all four consoles on it**. The stop is
the same transition: the host's own CUT OFF patch reads **`111` with the joiners in**
(it reads `103` with one racer, and run 6 read `129`), the CUT OFF/VS pair is pressed
with the bus still at `0xf`, and the group then goes with the **guests leaving one at a
time on their own** (`reason 0003`; their radios off at emulated frames 11831, 12007
and 12681) and the **host last** (12692, answering the second departure with a targeted
`reason 0007` deauth and closing with a broadcast `reason 0003`). No karts: the
consoles finish on different pages and the watch's terminal mask is `0x0`. **What four
consoles add is the client mask:** the polls the guests are handed carry `000E` (all
three clients) for the coupled lobby's 1,377 rounds and then degenerate into subsets
(`0006` 912, `0004` alone 4,217, `0002`/`0008`/`000A` the rest), so a client the round
does not address answers **blank** - p2 303, p3 310, p4 199 of the 2,825 / 6,850 /
1,749 polls each was handed - while the host's window counts `collected 10079 / empty
250 / reanchor-dropped 672`. The two-console arm has none of it (one client, its mask
byte `02` in every round, `blank 0`), so this narrowing is a property of the count and
not of the transition - and it is the attach point the `AMCE` registry entry already
records as **tried live and measured as not moving the run** (`src-tauri/src/
game_profile.rs`), which is why this pass leaves the emulator alone.

**So the two readings this pass was asked to act on are both retired** - the
client's reply rate (99.1% credited) and the mask it is addressed with (never
pruned) - and what is left is the client's own decision, at two consoles exactly as
at three and four. The host's own close is what follows it, not what causes it: the
same position `0.1.9bp` reached for MPH's two-console teardown, by the same method.

### 2026-10-16j — four consoles couple into one room on a state-anchored joiner route, and what ends it is the host's own close

**The joiner's route is anchored on the console's own page and radio, not on a
coordinate sequence.** `join_route()` in `tools/scripts/mkds-4p.py` advances on
whatever page the console is showing - each input in `JOIN_ADVANCE` (the title's
own advance presses, and deliberately neither CREATE nor the mode pair) is tried
once per distinct page - and the walk ends when the radio arms (`in_section`: its
own bus bit plus a room-sized frame in its hand), which is the console's own
account of "I am in the section". It then waits for the room to be *listed on
that console's own screen* (`group_listed`: the list's first row against the
empty slots beside it in the same sample, margin 20). It is one title's route,
but the anchor is the general one this file has been asking for since
`2026-10-16e`: the page the console draws and the frames its own game hands.

**Four consoles in one room, live, twice** (`logs/mkdsjoin4_route.log.gz`,
`logs/mkdsjoin5_route.log.gz` and their app logs; the host's own room page with
the joiners in is kept as
`logs/shots/mkdsjoin5_the_host_s_own_room_page_with_the_joiner_p1.png`, and one
joiner's own list as
`logs/shots/mkdsjoin5_the_group_list_a_joiner_is_standing_on_p2.png`): the host's
create pair raises
the room (`193382FD --bottom control--> B9F40C89` with its radio down, then the
confirm to `5FA369E7`, `role=1`, radio up), and the three joiners walk up and
join it - `link mask 0xf` with `p1=host` and `p2/p3/p4=guest`, every joiner
verified from its own state (its bus bit, its own handed frames `0218/40b` /
`0228/42b`, `role=2`), and the host's own game saying the same thing from the
inside: its command body carries **`0E`** in the AID-mask byte - three clients,
the reading `2026-09-29` established for four consoles. Each joiner walked up
from a *different* page - run 5's three were showing `BF1226E1`, `847907FD` and
`B55B1C4C` when their first tap went out - which is exactly what the old fixed
order could not do: it tapped MULTIPLAYER on the assumption the console was on
the title screen, so a console that had already passed it (`2026-10-16i`'s
failure) never reached the list.

**The joins have to overlap, and that is a route property too.** Two runs of the
same route, both with every joiner standing on its own list before any bar was
tapped: with the join *proved* console by console (tap, settle, wait for the
client role) the first two were out of the room again by the time the third's bar
was tapped (`mask 0x1`, the host's own CUT OFF patch `103 -> 115`,
`logs/mkdsjoin3_route.log.gz`); with the taps
sent back to back and the proof after them (`tap_group_bar()` then
`await_room()`), all three verified at `mask 0xf`. A linked MKDS client leaves
the lobby on its own (2,740 emulated frames and up), and 20 s of per-joiner
verification is more than that window.

**Superseded: the group *does* reach a joiner's own list, and that is what the
create pair bought.** `2026-10-16h` recorded three joiners tapping a bar on a
list that never changed, because the host's radio had gone down at the half-create
and no group was ever on the air. With the confirm in the route, every joiner's
own list moves when the host creates its group - run 4: `D197ECD9 -> 8124A347` on
consoles 2 and 3 and `1E92CB66 -> 8124A347` on console 4; run 5:
`D197ECD9/1E92CB66 -> 4352309B` and `1E92CB66 -> 7EC75070` - and the row reading
that says the room is on it is `145 87 85 86` on all three consoles in both runs
(the room in row 0, the empty slots beside it). That entry's claim survives only in its
narrower form: no group on the air is nothing to tap.

**What ends it is the host's own close, not the link.** With all four in the room
the host's CUT OFF patch moves `103 -> 149`, the driver presses CUT OFF and then
VS - and that transition is the game's own session end: the host's radio leaves
the bus (`mask 0xf -> 0x9` / `0xe`), every console writes its own
`deauth ... reason 0003` (host `deauth out 1`, each guest `deauth out 1`), the
host's next command body carries client mask **`00`**, its screens go flat
(bottom `mean 0`, `black 100%`) - the same park the two-console route sits in -
and all three guests come back to the group list (`193382FD`, `role=2`, the
72-byte frame; read from the session's own frames at the stop, row 0 still lists
it at `159` against the empty slots' `85-87`). The
reply path is not the constraint in that window: host `cmds tx 1679 / acks 1679`,
each guest answering 1,161-1,467 of 1,363-1,662 received commands, and the
counters freeze together at the teardown (`+217.6 s`) rather than drifting.
**No karts**: the room now holds four consoles long enough to be closed, and
what closes it is the same transition that ends the two-console route.

**Also on record:** this title's own game wrote the shared card file during the
first four-console room (`Test Roms/0168 - Mario Kart DS (U)(SCZ).sav`,
`f482cb42...` -> `036e8b78...`, written at that session's end - the game reached a
path that saves, which no earlier MKDS session here did); it was restored from
the run's own pin afterwards.

### 2026-10-16h — the page is the group list, its bottom control reads CREATE GROUP, and the joiner's real stop is that no group reaches its list

**The page was named from the game's own text, and it is not a room.** On a live
four-console session all of consoles 1-3 sat on one page (`2a15240b` as the
composite digest, `193382FD` as the frame CRC the gates read - the same page,
since console 2's composite digest was unchanged from boot through every tap
below while its CRC stayed `193382FD`) with **each** of them handing
the 72-byte `fc=0080` frame at `role=0` - the anchor `2026-10-16f`/`2026-10-16g`
reads as "the room exists". `bands` on that page's bottom screen reports four row
bands (y 15..35, 47..60, 79..92, 111..124) and one band along the bottom
(y 152..164); the bottom band's glyphs, one character at a time
(`tools/glyphs.py <the session's own shot> 256 344 256 17 120 light` - the crop
is the live one, and the shot itself was a session artifact cleared by
`tools/clean_repo.sh`'s rules; the coordinates are what the read needs), are
**`C R E A T E   G…`** - the group list's **CREATE GROUP** - and the four rows
above it are the list's empty group slots. The tap those entries call the
creating press, `(128,96)`, sits in the **gap between two of those slots**:
pressed on the live session it moved no console's page at all (p1, p2 and p3 all
held `193382FD` across it), while the bottom control moved the host off the page
in one press (`p1 193382FD -> B9F40C89`, and its own bit left the bus with it,
`mask 0x7 -> 0x6`, `logs/mkds4p5_stdout.log.gz`). The runner's creating press is
that control now (`CREATE_TAP = (128, 157)`), and its reads rank a band's glyphs
by **letter width** (a run wider than a character is a bar or a border), which is
what stops a boxed row being read as text.

**The joiners' real stop is not a control: the host's group never reaches their
list.** Consoles 2 and 3 are on that same group list with their radios in the
section (each handing the same 72-byte frame, `role=0`), and three taps at the
list's first row (`(127,23)`, the row a group would occupy) leave each of them on
the same page with `role=0` - because the page never changed: when the host
created its group the joiners' screens did not move (`193382FD -> 193382FD`
while the host went to `B9F40C89`). The session's own log says why nothing
arrived: **at the creating press the host's game ended its own wireless session**
(`p0 (player1): wireless off (end #2, slots now 0x6)`, wall frame 35898) and no
later event in that log turns it back on (`logs/mkdsread_app.log.gz`). The joiner
sequence itself is now the title's own order - TITLE, MULTIPLAYER, then the group
drawn along the top of the list - with the press retried on the page rather than
the page abandoned, so the three identical `193382FD -> 193382FD` lines are the
*game's* silence, not the driver's. **Corrected by `2026-10-16i`/`-j`:** the
"ended its own wireless session" is the *first half* of the create - the game
raises the room again on the confirm - and with that confirm in the route every
joiner's own list does move when the group is created (`D197ECD9 -> 8124A347`).

**Four karts are still not reached, and what is owed is measured.** The whole
session: p1 created a group and left the shared session doing it, p2 and p3 sat
armed on the group list with no group to join, p4 never entered the section
(`handed=0000/0b`). *(The host's half of that is answered by `2026-10-16i`: the
radio comes back on the game's own confirm input, `begin #3` with `role=1`, so
what was owed was the second input of the create pair, not a radio the emulator
kept down.)* The runner
(`tools/scripts/mkds-4p.py`) carries all of it: page-settled presses on the
console's own framebuffer (`probe` CRCs, no PNG per poll), letter-width page
reads, `--attach` to a live session, and the per-console radio state on every
line.

### 2026-10-16g — the route is durable now (`tools/scripts/mkds-4p.py`), the host room is reproducible, and the joiners' two stop-modes are measured

**The route lives in the repo, not in /tmp.** `tools/scripts/mkds-4p.py` takes a
ROM and a console count, launches (or attaches to, `--attach`) a session, walks
console 1 to a room and consoles 2..N into it, and gates every step on the
console's own radio: every wait is the app's own emulated frames, the room is
named by the **72-byte `fc=0080` frame the host's game hands with the bus mask**
(not by `role`, which lags - `2026-10-16f`) - a gate `2026-10-16h` then measured
firing on the group list for **every** console in the section, one page short of
a room - a client is named by its bit onthe bus plus its own frames in its hand, and the walk is monotone over (page digest,
input) pairs so it cannot circle. This is the churn the previous passes paid for
in scratch drivers, ended, and `2026-10-16h` sharpens it: the room gate is a
*screen* state check now (`in_section`), the joiner runs the title's own route,
and the pages are read out loud by glyph width.

**The host room is reproducible with it, twice in a row.** Both runs took the
same two steps and the same pages - `p1 78485022 --MULTI--> 790742fa --TITLE-->
2a15240b`, `handed=0080/72b`, `mask=0x1` - with the console's own game arming the
room frame while `role` still read 0 (`logs/mkds4p_route.log.gz`,
`logs/mkds4p2_route.log.gz`). The `TITLE (128,96)` press on the page after
MULTIPLAYER is what creates the room; the older route's `MULTIPLAYER` press there
does not, which is why its pictures changed and its radio stayed idle. *(Both
halves of this paragraph are corrected by `2026-10-16h`: `2a15240b` is the group
list, every console in the section arms that frame on it, and `(128,96)` moves
nothing there - the creating press is the list's bottom control, `(128,157)`,
which reads CREATE GROUP.)*

**The joiners stop in exactly two places, both measured, and neither is the
link.** With the broad action set, all four radios reach the bus (`mask=0xf`) -
but consoles 2 and 3 *host their own rooms* instead of joining: their walks press
CREATE on the group page, their games arm their own 72/116-byte room frames and
their own `role` reads 1 (`logs/mkds4p_route.log.gz`, watch rows). Four radios on
the bus is not one room, and this runner's own predicate says so (`joined=[4]` -
only the console that took the join branch). With CREATE and NORMAL removed from
a joiner's action set (`JOINER_ACTIONS`, plus a back-out on any self-host), the
joiners never raise a radio at all: each ends `role=0 handed=0000/0b mask=0x1`
with its own page digest logged for every press it made
(`logs/mkds4p2_route.log.gz`) - they stop on the title/menu pages before the
multiplayer section, i.e. the swing between `78485022`/`790742fa`/`a8f32c00`
needs the deeper entry the host gets from CREATE's page, while a joiner must stop
one page short of it. **So a four-kart session is not yet reached, and the two
joiner stop-modes are named with their evidence rather than left as "the search
failed".**

### 2026-10-16f — the monotone search reaches the host's own room, and the room anchor is the frame the game hands, not its role

**The search was repaired to the letter, and it moved the run.** It is monotone
over (page digest, input) pairs - no input is ever pressed twice on a page - it
steps in the app's own emulated frames, and every step is logged with the
console's own radio. On the first three steps of a four-console session
(`logs/mkdsmono_route.log.gz`, `logs/mkdsmono_app.log.gz`):

```
p1 78485022 --MULTI--> 790742fa | role=0 handed=0000/0b  mask=0x0
p1 790742fa --TITLE--> 2a15240b | role=0 handed=0080/72b mask=0x1
p1 2a15240b --TITLE--> 2a15240b | role=0 handed=0080/72b mask=0x1
```

On the second step the console's own game handed the bus a **72-byte `fc=0080`
room frame** and the mask took that console's own bit - read here as "the room
exists on the bus", until `2026-10-16h` measured that page to be the group list -
while `role` still read 0; `role=1` appears only pages later
(`p1 759a5050 --TITLE--> 759a5050 | role=1 handed=0080/72b mask=0x1`). So for
this title **the room anchor is the frame the game hands (72 bytes, `fc=0080`)
plus the bus mask, not `role`** - which holds; what `2026-10-16h` corrects is that
this combination is *the room*, and that the control that creates it is the touch
at `TITLE (128,96)` on the page after MULTIPLAYER. On the page that press was
measured on, it moves nothing, the frame is armed by every console, and the
control that creates a group is the list's own bottom row. The other half of this
section still stands: a gate that reads "did the picture change" cannot tell two
documented presses on an animating title apart, which is why these searches are
anchored on the console's own radio and its settled page instead.

*(Superseded in part by `2026-10-16h`: the page this section reads as the room is
the group list - every console in the section hands that same 72-byte frame on
it, and the press that creates a group is the list's bottom control.)*

**Not reached, and the reasons are named.** No guest joined: the follow-up
driver was meant to attach to that room but launched a *fresh* session instead
(`logs/mkdsmono2_route.log.gz`, mask `0x0` throughout), so the joiners had no
room to find - a defect of the scratch driver, not of the link. And the dead page
is still unnamed: `ff7213cc`'s bottom band at y 0 is blank and the band-finder
picked a band with no text in it (the run logs "a picture, not text" first, then
a 0%-ink band), so what the page says has not been read yet. Four carts have
still not reached a room together; the two things the next pass needs are the
room anchor above and a text read of `ff7213cc`/`3eaa03af` off a better band or
the top screen. **Both are carried into the durable runner below (`2026-10-16g`),
which encodes the anchor and reproduces the host room - and which measures the
joiners' two stop-modes.**

### 2026-10-16e — the driving gear rebuilt: frame-paced presses, an oracle anchored on the radio, and a classifier that can fail

**Pacing is out of the picture.** The route drivers no longer sleep at all:
every wait is the app's own per-console frame counter (`status`'s `seq`, with
`appctl.py advance` already the primitive), so a step is N emulated frames and
the 44%-of-DS-speed problem the previous pass named is retired as a class.

**A digest alone is not an oracle on an animated screen, and that is measured.**
With the frame clock in place and every press gated on "this console's screen
changed", MKDS answered *every* input while no console ever raised its radio:
seven steps on console 1 - TITLE, MULTIPLAYER, CREATE GROUP, the group bar,
NORMAL, the dialog YES, and A - each changed the digest and left `role=0`,
`bcn=0`, `mask=0x0`, and two still pages alternate under a press (`78485022`
<-> `790742fa`), which is exactly how a route built on "did the picture change"
walks in a circle (`logs/mkdsoracle2_route.log.gz`, `logs/mkdsoracle_route.log.gz`).

**So the route was rebuilt as a search with the radio itself as the anchor** -
home only when the console's own game says so (`role 1` with its beacon counter
climbing to host, `role 2` plus the console's bit in the bus mask to join; the
*role* half of that anchor is corrected by `2026-10-16f` below, which measures
the room by the frame the game hands the bus, because `role` lags it) - and
the search found the wall a name-oracle would have to cross: console 1's page
`3eaa03af` answers **none** of those seven candidate inputs while the other three
consoles keep animating (`logs/mkdssearch_route.log.gz`). The console is not
crashed (its own framebuffer is bright, mean 102 of 255, 3% black) and the bus
is idle, so this is a page the game draws and does not take input on, reached by
a blind walk. **Four carts still have not reached a room**, and what remains is a
*page-name* oracle for this title: the digests persist, but naming them has to
come from the game's own text (or a labelled calibration set of its menu
screens), not from a picture that changes by itself.

**The MPH instrument this pass was asked to repair is repaired, and it can now
fail.** `tools/scripts/lobby_page.py` gains a `topmean` probe (whole top screen)
and a calibrated ceiling `TOPMEAN_MAX = 72` that separates a real match's
rendered 3D view from this title's own menu screens - measured over four match
frames and six menu frames, 2.3-61.9 against 82.2-216.8. Menus now classify as a
new `MENU` page instead of `INGAME`; a match still reads `INGAME`
(`logs/shots/mph2_match_a.png` columns 0 and 1, the recorded two-console match
HUD). Its selftest was rebuilt on frames that exist - the old list had gone
**18-for-18 missing**, so its "PASS: 18 labelled checks, 0 wrong" was 18 skips
and zero checks, an instrument that could not fail - and a missing reference is
now a failure: `lobby_page.py --selftest` gives 10 checks / 0 wrong / exit 0, and
with two of those ten frames present the same command gives
`FAIL: 10 labelled checks, 5 wrong` with exit 1.

**What lands** (history.md v0.1.9af, `tools/mkds_join.py --restart --no-dialogs
--cut-off --vs`): title → MULTIPLAYER → CREATE GROUP → NORMAL (host role at
frame 2316) → the second console's MULTIPLAYER → the cyan group bar at
`(127,23)`, where the **guest role appears**; the host hands it
`BE 01 02 00 00 80 06 00 B8 B6` and the guest's own game answers
`00 00 5A 5A B8 B6` at the same emulated offset (+71.48s). CUT OFF and VS both
land; the host's command body walks to `... 80 FF 00 ...`, the guest's answer
adopts it, and both consoles end on the same picture (`45d15630`).

**Link:** `cmds tx 2687 = cmds rx 2687`, `acks 2687(+299, clientfail 4)`,
`answered 2686 + canned 1 = replies sent 2687 = collected 2683 + empty 4`,
`deauth out 0 in 0`, `slots 0x3`, both `LINK ACTIVE` — and a larger run of the
same route held that state for 4,756 commands' worth of windows. Both directions
are byte-identical window for window (`+29,670` against `+29,670`) once the
guest's radio is up.

**Where it stops, and it is the game's own screen.** Both games halt on the same
black screen with the link still perfect. Measured rather than assumed: a
battery of A on both consoles, START on both, and a touch at `(128,96)` on each
left both framebuffers byte-identical (`358609219F480325`) while the log shows
the input arriving (`[input] frame 12524 player 2 touch -> 128,96`) — so the
app delivers and the game ignores, on both consoles. A 13-cell matrix of
injected peer-state values (history.md v0.1.9n, 2662 deliveries, two park
shapes) moved the host's state **zero** times, so the parked game is not waiting
on anything the peer sends it.

**What the park actually is, read out of the games' own state** (history.md
v0.1.9an, `logs/park_probe_app_keylines.log`, `logs/natural_app_keylines.log`).
The memory read the roadmap has asked for since v0.1.9y is built
(`tools/appctl.py mem`), and the same registers are on the driver's probe. At the
park, **both ARM9s are alive and idle in FreeBIOS's vblank-wait loop**: `pc9`
`FFFF0238` on both, sampled on demand, with the ARM9's own registers read through
`mem` at that instant saying `IME 0001`, `IE 00040019` - vblank enabled, so this
CPU can be, and is, woken every frame. The digest's `IE 0000` that earlier passes
read as "interrupts are disabled" is the *wireless block's* `W_IE`, printed under
a label that could be mistaken for the CPU's; it has been renamed `W_IE`/`W_IF`.
The park is therefore not a CPU that died: **one A press on each console takes it
out of that loop (`pc9` -> `0200FD10`), redraws it (mean luma 1 -> 150) and lands
on MKDS's title screen** (`a579d0a8`, the same hash the route's title tap
produces). It is the game's own "the session is over" screen, and it wants a
button - whereas the input battery this file recorded before (A, START, a touch,
byte-identical framebuffers) was taken while the game was on a screen that
ignores it. What the park is *not*: the boot. With no input at all, MKDS boots to
its title by frame ~686 and animates its attract demo indefinitely (means 82-163
at 4 s intervals, 40 samples), so a black screen is never the boot state.

**At two consoles the link is not the constraint, and that is measured.** A
coupled 2-player group (history.md v0.1.9an, `logs/verify_app_keylines.log`) held
for 67 emulated seconds - the client's radio on at frame 3798, the run ending at
7803 - with `cmds tx 3820 = cmds rx 3820`, `acks 3820 (clientfail 6, 0.16%)`,
`answered 3819 + canned 1 = replies sent 3820`, `collected 3814 empty 6`. Then
the client's game **turned its own radio off** and landed on the park screen
(`1389f41c`), with no deauth frame sent and the link clean at that moment. A
shorter run shows the same thing at 8.5 emulated seconds: the client associated
at frame 3738, `cmds tx 294 / acks 294 / clientfail 0 / collected 294 / empty 0`
and the guest `answered 293 + canned 1`, and at frame 4246 the client's radio was
off and its screen was the park's. So the departure is neither a fixed timeout
nor the reply path, and it is not that the consoles miss each other: at the
moment the client gives up, every command has been answered. What the group
never does is become a race.

**2026-09-29 (v0.1.9ao): the group does not end at CUT OFF, and the wait it
opens is the blocker.** Three runs of the same 2-console route
(`logs/race2_app_keylines.log`, `logs/race3_app_keylines.log`,
`logs/race4_app_keylines.log`):

* **CUT OFF is not a teardown and not a menu.** Tapping it (`149,167`) moves
  **both** consoles to the pre-race wait: the host's touch screen reads
  **"PLEASE WAIT"** (read glyph by glyph off `logs/shots/cut1.png`) and the
  guest's top screen carries its own two-line message. The screen the driver's
  `--vs` step has been tapping all week is *not* that screen: a tap at `(128,22)`
  then moved **nothing** — both consoles byte-identical 2 s later, in two runs.
* **The wait is not input-gated, on either console.** A on the host and A on the
  guest, each measured against that console's own top screen (the stable one —
  the touch screen blinks), moved nothing: `logs/race4.log` prints "P1's top
  screen did not move" and "P2's top screen did not move".
* **It ends with the games giving up.** `deauth out 1`, reason `0003`
  (STA is leaving), at frame 19351 in one run and 7329 in another, with the
  guest following 2–3 frames later and both consoles on the park. Re-measured
  2026-09-29 (v0.1.9ap, `logs/race5_stdout.txt`): the whole wait lasted **5.4
  emulated seconds** (CUT OFF at `+60.50s`, first deauth at `+65.87s`), so its
  length varies run to run — and in that run the **guest left first**: its
  `deauth sent … -> 0009BF112233, reason 0003` precedes the host's own broadcast
  deauth by one frame, and the host's game was handed the departing frame
  (`handed start=1EEC len=38 fc=00C0 … 1F10:0003`). The leave is what the
  `clientfail` item below explains, not a one-sided timeout.
* **The reply path is not why.** Whole-run maxima: host `cmds tx 15582 · acks
  15582 · clientfail 10` (0.06%) against guest `cmds rx 15582 · answered 15581 ·
  canned 1`; and all 9 failures of the second run happened in its first 1009
  commands, so its last 3,584 exchanges were clean.
* **The guest's own departure is real but not on a clock.** In one run the guest
  deauthed the host at frame 6798 (`deauth out 1 in 0`) after ~2,740 frames with
  the wire clean; in another it was still coupled 10,500 frames after joining.
  So "a client leaves the lobby 13–21 emulated seconds after joining" (below) is
  not a bound the game enforces.

**The reply-payload anomaly is settled, and it was the instruments, not the
payload (v0.1.9ap).** One coupled 2-console run with a matched `wifiio` dump on
both consoles (`logs/fcs_wifiio.txt`, three rounds) plus the whole run's four
body logs (`logs/fcs_app.log.gz`, 12,088 answers over 4,096 distinct replies):

* **No substitution, no loss: the host's game is handed the guest's own bytes.**
  The crop (collected frame length minus handed frame length) is `4` in
  **4096/4096** replies, and the handed body is byte-identical to the first N
  bytes of the collected body in **4096/4096**. The answer to "a payload the
  guest never wrote, or two logs reading different offsets" is the second, and
  the difference is exactly 4 bytes.
* **Those 4 bytes are the transport's FCS, and the crop is the game's own.**
  On the one reply whose body both sites printed in full (seq `EAA0`, 30-byte
  frame, handed 26) the collected body is `00 80 81 C7 F5 97`, the handed body
  is `00 80`, and `crc32(handed frame) = 97F5C781` → `81 C7 F5 97`: the bytes
  the handoff cannot show are exactly `mp_fcs32()` over the frame the peer's game
  receives. That is the arithmetic already cited from GBATEK in to-do.md ("the
  descriptor's `+08h` length …"), confirmed here on live frames.
* **That also was the "29-value vocabulary vs thousands of distinct bodies".**
  With the FCS slot excluded the run has **38 distinct guest payloads and the
  same 38 collected by the host**; as printed, the collected bodies alone show
  545 distinct values (4,082 in the earlier run), because the CRC covers the
  seqno and frame and changes every round while the payload does not. The
  arming log counts the slot too (`00 80 01 10 B8 B6` = payload `00 80` + slot),
  which is what made it look like a fixed vocabulary.
* **The command direction shows the same 4 bytes, which is the cleanest proof
  they are not payload.** At the wait the host's command frame is 34 bytes
  (`tx frame: slot 1 (CMD) fc=0228 len=34`) and the guest's game is handed 30
  (`handed … len 6 body BE 01 02 00 00 80`): the 4 bytes in between are
  `02 00 B8 B6`, the sender's reserved slot.

Nothing in the reply path was changed for this — the payload was innocent, so no
fix was made.

**That chain does not hold (v0.1.9aq, four runs of the same route).** The mission
that followed asked for the empty windows to be A/B'd on the wait itself, and
the answer is that their count predicts nothing. The run that survived longest
carried **`clientfail 14` within ~4 emulated seconds of CUT OFF**
(`logs/race7.log`, and 17 by the end of its watch) against the run that died
carrying **4** (`logs/ab_ctl_stdout.txt`: `collected 521 empty 4 … clientfail 4`
at CUT OFF, the group gone 97 frames later) — and a third died with 6
(`logs/ab_keep2_stdout.txt`). `clientfail` is neither sufficient nor necessary
for the leave, so *reporting* a failure to the game is not the thing to trade.
What the counters do separate is the **drop**: `MD_REPLY_KEEP=3` at the default
budget stops the empties dead at the transition (`logs/ab_keep_stdout.txt`:
`empty 4` then `(+0)` for the remaining ~3,000 commands, `reanchor-dropped 1`,
`deauth out 0/0`) and the group stays coupled through the whole watch; and
`MD_REPLY_BUDGET_FRAMES=12` survived **307+ emulated seconds** at 94% of DS speed
(`logs/race7_*`, `link mask 0x3` at `elapsed_s 391`). **But neither knob is the
gate**, which is why the budget reading in the paragraph above is retracted as a
single-sample artefact: two `MD_REPLY_KEEP=3` runs split, one surviving and one
dying 8 frames after CUT OFF — *faster* than the default-knob control.

**What the four runs separate cleanly is one game value.** Every outcome is
predicted by whether the host's game ever emitted the cut-off state in its own
command body — `BE 01 02 00 00 80 FF 00 B8 B6`, the `FF 00` that v0.1.9ao and
v0.1.9t call the host's cut-off state, counted with a byte `grep` over each run's
app log:

| run | knobs | host emitted `FF 00` | guest adopted it | outcome |
| --- | --- | --- | --- | --- |
| `logs/ab_ctl_*` | defaults | 0 | 0 | dead 97 frames after CUT OFF |
| `logs/ab_keep2_*` | `MD_REPLY_KEEP=3` | 0 | 0 | dead 8 frames after CUT OFF |
| `logs/ab_keep_*` | `MD_REPLY_KEEP=3` | 5,287 | 7,204 | **alive through the watch** |
| `logs/race7_*` | `MD_REPLY_BUDGET_FRAMES=12` | 19,764 | 25,711 | **alive 307+ emulated seconds** |

Zero means the host's command body never left its lobby state (`06 00`) for the
whole run. So the group's fate is decided by whether the host's game *enters* the
pre-race wait and the guest adopts it — not by how the reply window behaves.
Which of the two is cause and which is effect is the open question: the trace
lines carry no frame number, so their first occurrence cannot yet be placed
against CUT OFF. **Next measurement, named rather than guessed:** dump the
command/answer bodies around CUT OFF with a frame number attached, and find what
makes the host's game emit `FF 00` — or why the CUT OFF tap the route fires the
instant the offer patch changes does not always reach it.

**That `FF 00 <=> alive` rule is a four-run correlation and does not survive nine
runs (v0.1.9ar).** The frame numbers are now attached: a sampler polls the app's
control channel every 12 ms and writes each console's own command and answer
bodies with the emulated frame it saw them at
(`tools/scripts/link-commit-trace.py sample`, nine runs of the documented route,
four at `MD_REPLY_KEEP=3` and five at the defaults, `logs/{d1,d2,d3,d4,k1,k2,k3,k4,r1}_*`,
`logs/` TSVs 7.8k–14.9k samples each). What the nine runs say:

* **The wait is survived most of the time with the default knobs.** 6 of 9 runs
  still had both consoles on the bus at the end of the watch (2/4 died at
  `MD_REPLY_KEEP=3`, 1/5 at the defaults); Fisher exact over the two arms is
  p ≈ 0.6, so the knob does not separate them, and the reply counters do not
  either (`empty` at the end: 15, 14 on the two deaths, 2–38 across the
  survivors).
* **The host game's state walk happens at a fixed lag: 115–147 emulated frames
  (1.9–2.5 s) after the tap, in 7 of 9 runs.** The tap is byte-identical in every
  run (`[input] frame N player 1 touch -> 149,167` held 12 frames, patch luma
  103 → 129–149), the walk is identical (`06 00` → `00 FF` → `00 FE` → … →
  `05 00`/`03 00` → `FF 00`), and the guest's own session-data phase follows at
  tap + 109–123 frames.
* **`FF 00` is not the gate: 2 of the 6 survivors never reached it.** The four
  that did commit at tap + 165–181 frames (2.8–3.0 s) — i.e. ~50–60 frames
  *after* the guest's data phase began, so the client is ahead of the host's own
  commit, not behind it. The other two survivors stalled at the host state
  `02 00` and stayed coupled anyway. No death ever committed it.
* **What every death does share is that the bus lost a console** (guest radio off
  first, host 11–12 frames later): 3 of 9 runs, at tap + 161, +269 and +461
  frames, and no survivor lost one.
* **Two of the three deaths are runs where the host's game never left its lobby
  state at all** (`06 00` in every sample from the tap to the guest's departure,
  161 and 269 frames later). The third (`k2`) walked normally and died 333 frames
  after its walk. **Why the tap reaches the host's game in 7 runs and not in
  those 2 is not established** — the touch, the patch it was aimed at and the
  screens at the leave are all identical, and this sampler cannot see inside the
  console. What is ruled out is the reply path: it looks the same in both.

So the pass's honest summary is that the pre-race wait is *usually* survived and
that nothing in the link decides it; and the one clean correlate that remains —
the host game's walk — is dated, reproducible and pointed at a place the current
instruments cannot see (why a tap that is logged as delivered is not acted on).
The reply-budget and `MD_REPLY_KEEP` readings above are retracted as single
samples, not as mechanisms.

**Driving on anyway: a tenth run where nothing fails and no race can be started
(`logs/vs1_*`).** The route was run once more with its own `--vs` and
`--press-all` steps after CUT OFF. It survived, committed (`walk at tap + 150`,
`FF 00 at tap + 203`) and held `link mask 0x3` for 12,725 emulated frames past
the tap with `empty 1` - and then every remaining step did nothing, with the
route's own lines as the record:

```
[after-cut-off] frames {1: 4471, 2: 4471} · link mask 0x3 · screens {1: '4cb0a6c0', 2: '70208695'}
  before VS: p1's top screen left 486e658b for 45d15630 after 1.3s
  P1 taps VS (128, 22)
  VS: p1's top screen still 45d15630 after 4s
  the VS tap moved nothing on P1's top screen: this state is not the game-select screen
[after-a-p1] frames {1: 5335, 2: 5335} · link mask 0x3 · screens {1: '45d15630', 2: '45d15630'}
[after-a-p2] frames {1: 5608, 2: 5608} · link mask 0x3 · screens {1: '45d15630', 2: '45d15630'}
```

`45d15630` is the flat-black picture, and the three captures the run took show
the shape of the wait as pictures rather than hashes: `logs/shots/mkds-lobby.png`
(the coupled lobby, structured menus and text bands), `mkds-after-cut-off.png`
(250 frames after the tap: a *bright, filled* screen — the wait itself, and the
screen the driver's `--vs` step has been tapping at all week), and
`mkds-after-vs.png` (512x384, 1,699 bytes — one flat colour across both consoles'
four screens, which `appctl.py render` prints as 22 identical rows). So the wait
does not merely hold: the games' own output goes black about 1.3 s into it, on
both consoles, while the link underneath stays perfect — and neither a VS tap nor
A on either console moves a pixel of it. That is the wall in front of a race now,
stated as a measurement: not the link, not the reply path, and not either knob.

**And the surviving state is characterised, because one survivor was left
running** (`logs/race7_live_endstate.txt`, four probe samples eleven minutes in):
both games pinned at the *same* ARM9 PC (`0200FD10`), `powcnt1 820F` (LCD on),
not asleep, both framebuffers and the app's own screen copy 100% black and
unchanged for 3,690 frames at a stretch, `pc9`/`pc7` repeating across samples —
while the exchange underneath is perfect (`cmds tx 17488`, `answered 17487`,
`canned 1`, `deauth 0/0`, `link mask 0x3`). When the transition takes, the group
no longer dies at the wait; it waits in a game-side poll loop with the link
healthy and both displays black. That is the wall now in front of a race.

**That wall is now identified, and it is a message rather than a CPU**
(history.md v0.1.9as, `logs/park7_evidence.txt` and `logs/park7_vars.tsv`). The
pinned PC `0200FD10` is the SDK's `delay(100)` helper (`subs r0,r0,#4 / bcs`,
disassembled live from the console's RAM — the ROM file's ARM9 image matches
0/256 words at that address, so the listing has to come from the running
console). Of the five callers a whole-4 MB `bl`-target scan finds, three are
retry loops, and their exits separate against live values read in the same
window: one tests bit 7 of `[0x027FFF8C]` (`0000FFF0`, set), one tests bit 14 of
`IPCFIFOCNT` (`0x04000184` reads `8501`/`8601`, clear), and the third tests
`*(u32*)0x2173300 != [0x21732AC]` — **equal**, `3F`/`3F` on p1 and `32`/`32` on
p2, the same pair in three separate runs. So the wait is
`0x2021488(arg & 1)`: the **blocking dequeue** of the game's own message queue,
which `DC_InvalidateRange`s the word before every read (`0x200EE3C` is
`mcr 15,0,r0,cr7,cr6,{1}`, i.e. the writer is not the ARM9 core) and
`delay(100)`s until the producer's count at `0x2173300` differs from the
consumer's at `0x21732AC`, then dequeues ring slot
`[[0x21732CC] + idx*4]` and increments `[0x21732AC]`. No ARM9 code stores
the producer's word: only four references to the pointer global `0x2174DE0`
exist in 4 MB (the module's own init, which writes the buffer address into it
once, and three reads) and none of them stores through it.

**Satisfying it, directly:** the control channel gained the write half of `mem`
(`tools/appctl.py poke`, over the bridge's existing `md_write_ram`). On a parked
guest, `pc9` sat at `0200FD10` for 37 of 40 samples; one `poke 2173300 00000000`
moved it to `02021544` for **40 of 40** — the branch that test guards — and
`[0x21732AC]` advanced `32 → 33`, the dequeue's own increment. Twenty seconds
later nothing had written `0x2173300` back. So the console is held by the poll,
the poll is held by a message that never arrives, and the queue holds no live
entry (the nine ring pointers are `0x2173D00, 0x2, 0x5, 0x21E1A30, 0x953,
0x21DF880, …`, and the entry the poke consumed walked a list that does not
terminate). **Why the message is missing, from the same window** *(corrected 2026-09-29,
v0.1.9at: this paragraph used to read "the guest's ARM7 sits in the BIOS with
its wireless interrupt off (`IE7 0104`, \"wifi ie no\")" — that reading was the
probe's, and it was wrong. `IRQ_Wifi` is bit 24, not 23: `Wifi.cpp` raises it
with `NDS.SetIRQ(1, IRQ_Wifi)` and `NDS::SetIRQ` is `IF[cpu] |= 1 << irq`, so it
is bit 8 of `IE7`, and the old `& 0x80` test read bit 23 = `IRQ_SPI`. `IE7 0104`
is wifi **plus** IPC-recv — exactly the configuration a game has when it has
started the wireless firmware — and the corrected probe reads it as enabled in
**every** on-air sample of both runs measured: 143/143 and 32/32 in `arm7_a`,
328/333 and 226/226 in `arm7_b`.)* Both games stop reading the wireless block
(the healthy park's `handed seq` is frozen at `D0E0`/`DCE0` across every report)
while the link stays `0x3` for 12,725 frames — the two games stop being *fed*,
rather than the link dropping.

**And the teardown that ends a session is the game's own, delivered by us.**
Settled 2026-09-29 (v0.1.9at) from the ARM7's own registers in matched windows
of a survivor (`vs1`) and two failures (`arm7_a`, `arm7_b`), plus a new log-only
analyzer (`tools/scripts/mp-arm7-watch.py --teardown`, `--selftest` 3/3).
`Platform::MP_End` has **exactly one caller** — `Wifi::UpdatePowerOn`, reached
only when the game clears wifi `POWCNT` — so nothing in this tree can end a
session by itself. In both failures the first-moving console's *own game* armed
a deauth frame in its own TX slot (`LOC2` on the client, `LOC3` on the host,
`reason 0003`, 30 bytes) and then wrote the shutdown sequence the WLAN hardware
answers to (TRX power off, RF writes at `0x4C`/`0x4F`, `POWCNT` clear →
`WIFI: OFF`); the other console tore down because our path **handed it that
deauth** (`[mp] deauth received`, or `fc=00C0` delivered to its software) — 6 log
lines later in `arm7_b`, 11 frames later in `arm7_a`. Both directions occur
(`arm7_a`: guest first, `arm7_b`: host first), so only the mechanism is a rule.
Delivery was complete up to the last round (`collected=0002 of mask 0002`, reply
aged 15751 of the 33430-tick budget, handoff OK; the survivor shows `W_IF & W_IE
== 0000` in 82/82 and 75/75 digests and `handed seq` moving in 23 distinct values
through its park), so nothing was owed-and-undelivered at the transition. Ruled
out as the discriminator: the reply window's carried-over unread answer (1 in
22,853 of 22,853 rounds in the survivor, 1 in 4,792 of 4,799 in `arm7_b` — the
same state, so not the cause).

**And the ARM7 asymmetry that v0.1.9at left standing is retracted — it was the
instrument** (v0.1.9au). It came from `pc7` sampled once every 0.3 s, and both
ARM7s halt in the BIOS between their own scheduled wakeups, so an instant lands
in the halt loop most of the time on *either* console. Measured instead from the
emulator's own per-register read tally, differenced over the park with
`tools/scripts/mp-arm7-watch.py --activity`, in a **survivor** (`vs1`, 12,725
frames) beside a **failure** (`arm7_b`): the host's ARM7 does 31.1 wireless
reads/frame (of which its `W_USCount0`+`W_RXTXAddr` spin is 6.6) versus 28.5
(6.9); the guest's does **219.6 (174.7) versus 232.4 (191.5)** — the guest is
*busier*, ~7× the host, dominated by the client's WM receive-wait
(`W_USCount0` 87.6/frame, `W_RXTXAddr` 87.1/frame) beside functional reads, and
the shape is the same in a session that survives and one that dies: 174.7 against 191.5 on the guest (within 9%), and 6.6 against 6.9 on the host. A third run this pass (`arm7_c`, default knobs, park held to frame 13,213) reads 6.2 and 158.3, so across the three the spin is the same order within 20%.
(The totals are over the registers both of a run's end blocks printed, which is
what `--activity` reports and says so; the spin pair is in every one of them.)
So the ARM7s are working, the client's spin is its steady state rather than a
stall, and the ARM7 side is not the failing side. The consequence matters more
than the retraction: **a survivor and a failure are in the same state by every
aggregate measure taken** — delivery, radio, interrupt servicing, ARM7 activity —
so what separates them is the game's own patience/timing, not a machine-state
difference we can see at the park. What is left to explain is why the *host's*
game never emits the next game-level state while it heartbeats: its command body
changes only in a 4-byte field and the guest answers the same six bytes for
thousands of rounds.

**And the gate itself is the game's ARM9 message queue, with its producer on the
ARM7 side** (v0.1.9av, verified in one window on both consoles). At the park both
ARM9s are pinned in the blocking dequeue (`pc9 0200FD10`, 40/40 and 38/40 samples)
and the two words its test compares — `0x2173300` (producer) and `0x21732AC`
(consumer) — read `41/41` on p1 and `3F/3F` on p2: nothing is queued, both games
wait for a message. A whole-4 MB scan for the address `0x2173300` finds it as a
code literal in exactly one place, the dequeue's own read at `0x020216CC`, so **no
ARM9 code stores it**; the ARM9↔ARM7 IPC is enabled and idle on both consoles
(`IPCFIFOCNT 0x8501`, `IPCSYNC 0`, 12 samples over 6 s). Poking the word
(`41 → 42`) advanced the consumer to `42` and the console went straight back to
`0200FD10` (30/30), so the loop is what holds it, while the entry's content is not
what the game wants. The producer therefore runs on the ARM7 — and that address
space, invisible to `mem` until this pass, is now readable (`appctl.py mem --cpu
7`: 320 KB of ARM7 WRAM and shared WRAM read and disassembled in one go, the first
IPC touch points found at `0x0600397C` and `0x060092F8`, and the host's parked ARM7
PC `0x06008A68` inside that same module).

**Every frame class the two consoles exchange is carried, counted at both ends -
and at a longer park the game is not in that dequeue at all** (v0.1.9ca). Asked to
stop testing the *bytes* the fork carries and test the one class no byte view can
reach, a frame that is never carried, the session was counted instead of compared:
`md-link.py census` (new) counts the writer (`[mp] tx frame`: which of the six TX
slots each game wrote into) against the carrier (`[link-trace]`: what each console
put on the air and what each console's game read off it, with the destination's
kind) and separately counts every frame the fork itself refused. On a fresh
two-console run of the documented route (`--restart --no-dialogs --players 2
--cut-off --vs --watch 40`, `logs/v9f0`) the parked window - the last 60 emulated
seconds before the games gave up - counts **CMD 3,590 written by the host / 3,590
read by the guest's game · ACK 3,590/3,590 · REPLY 3,591/3,590 collected · BEACON
269/269**, with no refusal of any kind and no round lost. Slot 0 was never written
by either game's own code in the whole session, and slots 2 and 3 only for the four
association frames and the two closing deauths, so there is no game-written class
that nothing forwards. Over
the whole 62,818-command session the only unmatched traffic anywhere is **127 of
4,800 beacons (2.6 %)**, and those are not a lost class either: 125 of them were
written before the guest's radio ever read one (the host beacons from its own
`wireless ON` at t=37.3M us; the guest's first beacon read is at t=65.2M us), and
the same 125-130 turn up in the two recorded park runs (`logs/v9e0`: 126 of its 127
missing were before the guest's first read; `logs/v9d1`: 130) - the pre-coupling
stretch, not a frame the fork refused (its `frames_dropped` stayed 0 for the
session). Group-addressed frames are carried: the host's commands and acks go to
`03:09:BF:00:00:00` / `03:09:BF:00:00:03` and its beacons to `FF:FF:FF:FF:FF:FF`,
all of them read at the peer; the only management classes on the air are the four
association frames and, at the very end, the two deauths, which the fork's own
receive path consumes by design (`CheckRX`'s `fc=00C0` branch, not the game).

**What each game is waiting on, at that same park and read live**: both ARM9s are
inside the **ARM9 BIOS's own wait loop** (`pc9 0xFFFF0238`, the `SWI Halt` WFI path
- 37/40 and 40/40 samples, the rest brief excursions through the game's own poll
handler around `0x0212F4E8`), the wifi block's `W_IE` is `0000` on both consoles
and `W_IF` `88C0`/`0800`, and **the queue the v0.1.9as dequeue spins on is empty on
both** (`0x2173300` producer == `0x21732AC` consumer: `0x4B/0x4B` on p1 and
`0x49/0x49` on p2, read through `appctl.py mem`). So at this park the console is
*not* inside the blocking dequeue - it is halted with nothing queued, waiting for a
wake-up no frame of any class could deliver, and `W_IE 0` means the radio could not
raise one anyway. The payloads are frozen the same way: the last 6,000 command-body
views (3,000 rounds, both consoles) are byte-identical, `BE 01 02 00 00 80`, and
the guest's answer takes exactly two forms in the last 6,000 replies (`00 00 00 00
B8 B6` 4,584 / `00 80 00 00 B8 B6` 1,416) - **one alternating bit is the only thing
either game varies**. The link held that shape for ~1,039 emulated seconds and the
run ended the way the record says: the guest's own `reason 0003` deauth first
(t=1,108,287,136), the host's 223,521 us (13 frames) later, `mask 0x3` until then.
Evidence: `logs/v9f0_census_whole.txt`, `logs/v9f0_census_park.txt`,
`logs/v9f0_park_pcs.txt`.

**In the dead variant the guest draws a system box reading "An error has
occurred." with an OK button**, read letter by letter out of the framebuffer
(there is no OCR on this box, so the glyphs were segmented by column ink: 19
cells, an 11-row capital `A` against 8-row lowercase, `e`-bar and `d`-stem
cross-checked). That string is **nowhere in the 4 MB of ARM9 main RAM**, so
MKDS's own text bank did not draw it. CUT OFF itself is not the end: in that run
both consoles were still `ACTIVE` with 1436 commands collected at the
`[after-cut-off]` read, and the link only went `0x3 → 0x0` about 130 frames
later, so the pre-race wait outlives the press and the deauth follows the games,
not the tap.

**Three consoles: coupling only, never character select, never a race.**
history.md v0.1.9al logged three runs that coupled one host and two guests into
one group at the same emulated frames (`link mask 0x7`, frames 5003 / 5091 /
4803 in `logs/mkds3_v3.log`, `logs/mkds3_ab_stock.log`, `logs/mkds3_run2.log`),
all three still coupled at the read 175 frames after the host's CUT OFF tap
(frames 5241 / 5321 / 5003). What the group reaches after that is the **same park
two consoles reach** - the host's own screen is its pre-race wait (`84863aee`),
the guests' are their frozen park screens (`b02f9f9e`, `1389f41c`), and the group
then falls apart on the games' own `reason 0003` deauths a few hundred emulated
frames after the tap (v3: the second guest at +205, the first at +919; ab_stock
held the first guest past +4,300). **No recorded three-console run ever drew the
character-select screen and none started a race**, and the reply-window health at
three consoles was the documented 5.8 % `clientfail` (v3 end digest, 380 of
6,518). This exact route was re-run on the current build (v0.1.9bw,
`logs/v9d2..v9d5`, four runs): all four coupled at `0x7` (frames 5125-5184),
three of four still `0x7` at the after-CUT-OFF read and one lost both guests at
the tap itself, `clientfail` 5.0-6.3 % - the record's own rate. **So there is no
port-level divergence: today's build reaches exactly what the record reached, and
the park is the same wall at three consoles as at two.** The only difference is
per-run timing inside that coin-flip (which guest leaves first, and how long it
holds: 174-1,506 frames), not a different outcome.

**Four consoles, verified twice** (history.md v0.1.9am, `logs/mkds4_run1.log`
and `logs/mkds4_keep3.log`): one host and *three* guests, all four coupled at
frames 4961 / 5301 / 5323 in the first run and 5349 / 5368 in the second, every
console's digest read at the same emulated frame, `p1=host` with `p2=guest`,
`p3=guest`, `p4=guest`. The host's own game says the same thing from the inside:
the command it hands its wireless hardware carries `0E` in the AID-mask byte —
three clients — where at three consoles it carried `06`. All four sat on the bus
in one group for 684 emulated frames (wireless on at 4830, first client off at
5514), with **no deauth inside the couple**: the three `[CLIENT] deauth` events
come at 5514 / 5552 / 5615, which is the game's own CUT OFF / VS teardown, the
same shape a two-console session shows. The route needed no change beyond
`--players 4` — every joiner walks to its group list first and the four joins go
back to back. The host reads `p1=host` and both
others `p2=guest p3=guest`, and in the couple window the host's own digest
(`collected 120`) accounts for what its two clients say they wrote (`p2
answered 105` + `p3 answered 13`, read a frame apart). **What it costs to
reach:** every joiner has to be walked to its group list *before* the first one
joins, because an MKDS client leaves the lobby on its own about 13–21 emulated
seconds after joining — walking the third console afterwards never has both
clients in the room at once (the mask went `0x3` → `0x1` → `0x5` and never
`0x7`). **What does not work yet at three:** some of the host's reply windows
give up short a reply (216 of ~1,300 in one three-console run: 146 collected
nothing, 35 one client, 35 the other), and every short window is a client the
host's own ack reports as failed. Over a whole run that reads as
`clientfail` at **4.3–5.8% of collected replies** with two clients, against
**1.3%** on the same build and route at two consoles (and 0.08% in an earlier
two-console run, so the raw count varies with what the game is doing — only the
rate is comparable). **The mechanism is visible in the host's own counters**, on
the three-console run that went furthest: `collected 8635 empty 146
reanchor-dropped 208 blank 186 clientfail 367` — an answer the window closed
without is either read by the next command or discarded by it, and 186 blanks
plus 208 re-anchor drops account for the 367 failed-client acks almost exactly.
So roughly once a frame the host's game is told a client failed while all three
consoles are linked and playing. It is **not** the per-frame reply budget: most
budget clamps are 34ms → 33ms and only 97 of 9,037 landed on zero slack, so the
window chooses to give up rather than being cut off.

**The wait is reproduced cleanly, and the console that stops drawing is the
guest (v0.1.9bs, `logs/v9an_*`).** A fresh run of the documented route with
`--cut-off` and no `--vs` reached CUT OFF at emulated frame 4391 with
`link mask 0x3` — and unlike the recorded run it did **not** collapse at the
wait: it held for the whole 40 s watch with a wire that is perfect by every
counter (`host collected 3466 · empty 5 · clientfail 5` against `guest answered
3470 of 3471 · empty 0`), no deauth on either side, both consoles `ACTIVE` and
both `changing` at the tap. Inside that healthy session the two games are **not**
doing the same thing:

* the host's command state walks `06 00` → `03 00` at tap + ~5 s and then
  **sends the same `03 00` for the rest of the run** (76 rounds a second, the
  body never growing past its 10-byte management form), while its own picture
  **keeps changing** — `changing (0f ago)` at every sample to the end;
* the guest's own game **stops drawing at ~frame 5686, four to five emulated
  seconds after the tap**, and never draws another frame: the driver's own
  `picture last changed N frames ago` climbs 550 → 850 → 1149 → 1449 → … → 2347
  while the guest **answers every single poll** (`answered 3470 / cmds rx 3471`,
  `empty 0`, `clientfail 0`);
* the guest's own answers are not steady either: its written body **oscillates
  between two forms every other round** (`00 80 03 00` / `00 00 04 00`,
  `00 80 04 00` / `00 00 03 00`, …), i.e. its own state machine is toggling
  while the host repeats one state.

Read off the panels (`logs/shots/mkds-after-cut-off.png`, 250 frames after the
tap, both consoles): the host's touch screen is the game's own **`PLEASE WAIT`**
modal (read glyph by glyph), the guest's touch screen is **not that modal at all**
— two icon/text rows near its top and a wide plate at its bottom — and both
**top** screens carry the same layout and the game's own `CURRENT RACERS` label.
So the asymmetry this pass can name is: **the host has advanced to a wait and is
animating it; the guest has not left the screen it was on, has stopped drawing,
and is ping-ponging its own answer between two states.** Which side is missing a
message is therefore the **guest's own game**, and what it is waiting for is
still unnamed — its host's command is constant and every poll it receives is
answered. ~30 s past the watch the group collapsed on its own (`mask 0x0`, both
screens flat, `logs/shots/v9an_park.png`) with no race at any point.

**The fork is not carrying a stale or duplicated body, settled in bytes rather than
by correlation (v0.1.9bt; `tools/scripts/md-link.py rounds`).** The question the
oscillation raises is whether the alternation is the guest's *game* or the fork's
carrier, and the verbose log already holds both sides of it: the guest's own write
into its reply slot (`Wifi::TXSendFrame` case 5, recorded from that console's own
TXBuffer), the host's own write into its command slot (the link trace's `cmd body`),
the bytes the fork carried (LocalMP's per-client buffer), what the host's game was
handed, and the guest's own record of every command that reached it. Comparing them
round by round, by frame sequence in log order (the 12-bit seq is reused, so a
dictionary keyed on it pairs a wrap apart and invents mismatches — that is how this
instrument's first form reported 4,720 "DIFFERENT" rounds on a faithful link):

| direction | rounds compared | identical | different |
| --- | --- | --- | --- |
| guest's own reply-slot write vs the bytes the fork carried | 4,750 (`v9an`) + 6,150 (`v9ai`) | **all of them** | **0** |
| host's own command-slot write vs the command the guest's game read | 4,590 + 5,287 | all but 5 + 5 | 5 + 5 |
| the frame length, host command write vs guest's own frame | 4,613 of 4,756 | the exact `+4` of the wire's FCS slot | — |

* **The fork never duplicated or staled a command.** All **4,756** commands the
guest's game received are recorded *by the guest itself* as `a new command,
delivered`; **zero** are `the same seqno as the last command`. The host wrote
3,684 distinct command seqs.
* **The guest's own bytes reach the host unchanged**: no reply the host collected
differs from what the guest's game wrote, in either run, once the four bytes the
wire owns in the frame's trailing slot are excluded (the writer's buffer still
holds whatever the game left there — for the lobby template that is `5A 5A B8 B6`,
replaced on the wire by the computed CRC, which is why the raw comparison reads
`00 80 5A 5A B8 B6` against `00 80 AE 2C C6 31` and why the *only* honest
comparison excludes that slot).
* **The bytes are the game's own, not the carrier's**: the 49 distinct bodies the
guest's slot write carried in one run — `00 80 03 00 B8 B6` 1,448 times, `00 80 04
00 B8 B6` 1,427, `00 80 5A 5A B8 B6` 661, and their `00 00` siblings — are read out
of the *guest's* TX buffer, so the alternation is its own game's output.
* **The guest's own game is still running in the window its picture froze in**: it
armed and wrote its reply slot **4,755** times with the arm serial monotonic
`1 → 4755` and its own wireless clock monotonic (`68347282 → 132666600`); **4,700**
of those answers were written and 4,695 collected *after* the CUT OFF tap at frame
4,144; and its game read **28,542 halfwords** out of the 3,684 commands it was
handed. A game whose driver is arming, answering and reading on every poll is not a
game waiting on the emulator.

The residual, stated rather than buried: **5 rounds in each of the two runs**
(<0.15 %) where the trace's view of the frame the guest's game reads at payload
offset 24 holds `A5 A5` — stale buffer content — instead of the host's bytes, while
those rounds' lengths are the healthy `+4` and the guest's own answers in them still
move. The trace's own comment says the write cursor can advance by a halfword
between frames, and a 2-byte window of stale bytes in 5 rounds of ~4,600 is a trace
alignment this instrument cannot attribute, so **no change was made for it**: the
mission's rule is to fix what is substantiated, and this is not.

**The trace's body view was corrected and the thread's analysis tools consolidated
(v0.1.9bw).** The emitter printed this body from `frame[28]` — the MP payload minus
its 4-byte header and including the wire's 4-byte FCS slot — and
`tools/mkds-round-bytes.py` compared a guest's `payload@24` against it at that
offset, which is exactly where the earlier ten-step "state walk" was read from.
`trace_frame` now prints from `frame[24]` (header included, FCS slot excluded), the
length field is deliberately unchanged so every `body[N]` parser still works, and
the per-round byte pairing, the wireless counters and the teardown verdict now live
in **one entry point, `tools/scripts/md-link.py`** (`rounds`, `wsync`, `verdict`,
`watch`) — `mph-teardown.py` and `mkds-round-bytes.py` are deleted. On a fresh
session (`logs/v9d1`) the trace reads the corrected bytes — the group announce
`BE 01 02 00 0C 9F …` and the post-CUT-OFF command `BE 01 02 00 00 80`, against the
old view's `0C 9F …` and `00 80 06 00 B8 B6` — and `md-link.py rounds` finds the
same faithful link: **8,164 of 8,164 replies identical (0 different), 8,133 + 11
clamped of ~8,200 commands identical, 7 different** — the same `A5 A5` stale-read
residual as above.

**Round alignment is counted, and there is no off-by-one (v0.1.9by).** The one
link-side property the byte comparison does not state is whether each round's
payload is delivered and consumed **in the round its peer is waiting in** — not the
round before or after. `md-link.py rounds` now closes with that count and reads the
two directions as ordered streams (see the view's own docstring for the two
pairings):

| direction (rounds in log order) | shift −1 | shift 0 | shift +1 |
| --- | --- | --- | --- |
| command: host's CMD-slot write → guest's read | 0/9140 | **9141/9141** | 0/9140 |
| reply: guest's answer write → host's collected reply, matched by seq | — | **9136 identical, 0 different** | — |

On a fresh two-console run of the documented route (`logs/v9e0`, `--cut-off --vs`,
the same park at screen hash `45d15630` on both consoles), the command stream
aligns **exactly at shift 0** and **zero** positions align one round early or late;
the v9d1 record counts 8,209/8,209 at shift 0 and 0 at both neighbours. Every
collected reply is the guest's own write of that same seq (9,136 of 9,136, plus the
same single lost collection v9d1 showed). The legacy `A5 A5` residual shows here as
2 of 9,141 command bodies differing at payload offset 24 **in the offered view
only**, and the rounds where both views exist settle it: the delivered body
(`packet delivered to the console's own software … body[..]`) matches the host's
write byte for byte, so the difference is that view going stale, not a round that
carried the wrong command. The guest's own answer is a *pipeline*, not an offset —
it arms while handling one poll and the fork holds the arm to the next
(`arm_persist()`) — so the view prints that arm-to-poll lag as its own distribution
(1 round in 6,990, 2 rounds in 2,148, firmware stand-in 3) rather than letting it
read as a delivery shift. **This closes round alignment as the last link-side
hypothesis for MKDS's pre-race wait: delivery is exact, replies are exact, and the
wait is between the games.**

**The reply pipeline is tested directly and it is the games' own cadence, not a
fidelity gap (v0.1.9bz).** With alignment closed, the one link-side property left
is *when* the guest's answer leaves: the fork holds the game's arm to the next
poll, so the host's window for round N is handed the state the guest reached
before N. `MD_SAME_ROUND_REPLY=1` sends that answer in the round the poll arrived
in instead, and the instrument now reports the latency in the units the two sides
pace by: on the coupled v9e0 run the round is **16,672 µs**, an arm comes **1,832
µs** before the poll that carries it - i.e. ~14,960 µs into its own round, just
before the boundary - and the host's reply wait is ~446 µs, so the answer is only
ready in time for the *following* window by the game's own processing time.

| MKDS run (same route, `--cut-off --vs`) | guest arms | guest canned (firmware stand-in) | host collected | outcome |
| --- | --- | --- | --- | --- |
| `MD_SAME_ROUND_REPLY=0` (`logs/v9e0`, `v9e2`) | 8,207 / 1,677 | 1 / 2 | 8,165 / 1,664 | coupled, CUT OFF, the park `45d15630`, no race |
| `MD_SAME_ROUND_REPLY=1` (`logs/v9e1`) | **5** | **388** | 385 | the guest's driver starves; the group collapses *before* CUT OFF completes, mask `0x0` |

With the flag on the guest's own game arms **5 times in 394 polls** and the host's
windows collect 385 replies of which only 5 match a guest write - 380 are the
firmware's `fc=0158` stand-in. NSMB, the working control, is harmed the same way:
flag off it holds all 234 emulated seconds (`slots 0x003`, 11,126 replies, 0
death), flag on the guest's own game deauthenticates at ~213 emulated s (`reason
0003`, `slots 0x000`, 9,903 replies). **So same-round delivery does not move the
park - it breaks both games** - and the one-poll pipeline is what the games'
drivers require, not a hop this fork added. The change stays behind its default-off
flag (the default path is the same code factored through `Wifi::AnswerPoll`), and
the smoke test is 134/0 on both settings.

**Unknown:** whether the race starts if the pre-race wait is ever completed
(nothing has made it complete; both games sit in the same ARM9 poll loop with
both displays black and the link perfect), **what makes the host's game emit its
cut-off state at all** — the single value that separated the four v0.1.9aq runs —
whether a race would then stay in sync, and whether the client failures above
ever cost a player in a three-player match. The guest-side oscillation above is
the sharpest new thing to chase: it is the first state difference this project
has measured between the two games **in a session where the wire is perfect**.

**Both consoles were then driven through the app's own input surface at that
park, and neither game answers (v0.1.9bu, 2026-10-01).** The mission's order was
to stop reading trace and drive the real surface, so this pass did both, and the
battery is the largest one this title has had: **10 button presses and 15
touches per console** (the full a/b/start/select/x/y/l/r/d-pad set, then a
15-point grid across the whole touch screen including every position a button
has ever been measured at) on **both** consoles, every step followed by a fresh
screenshot of *both* screens, with the guest as the oracle - at the park its own
game has stopped drawing (its `picture last changed` climbs into the thousands
of frames) while the host's still animates, so **any** change on the guest's
screens would have been its game answering. Result: the guest's screens changed
**once**, on the very first press, from `1564d6e5` to `790742fa`, and then never
again for the remaining 24 steps; the host's 25 steps changed nothing. In a
separate live park, the host's own command body was read before and after a VS
tap, a START press, an A press on the guest and a further touch - **it stayed
`FF 00` through all four**.

**And what the tap is aimed at is now named: the host never presents the
game-select screen, so VS has nothing to land on.** [history.md](history.md)'s
original working route ("Driving a real game's menus") is explicit that after
CUT OFF the *host* taps **VS on the game-select screen**, and that "character
select and game settings follow, and both consoles advance together" - so VS is
the button the host is blocked on. What this pass adds is *when* it would have
to exist: the host's cut-off walk is now read from the link trace round by round
and it is a fixed ten-step sequence that **ends** - `06 00 -> 00 FF -> 00 FE ->
00 00 -> 00 01 -> 01 00 -> 01 01 -> 02 00 -> 05 00 -> 03 00 -> FF 00`, settling
two to five seconds after the tap - and by 1.3 s into it **all four screens are
100 % black** (`powcnt1 820F`, LCDs on, `framebuffer top 0 bottom 0`, and the
same flat `45d15630` on both consoles) and stay black for the rest of the
session. So the game-select screen is never drawn, at any point, in any of the
three runs measured this pass.

**The harness defect that hid this is fixed and re-run.** `mkds_join.py`'s
`--vs` step waited on the host's **top** screen, whose first change after the tap
*is* the transition's black - so the tap fired mid-walk, roughly a second before
the walk ended, and MKDS does not queue a touch. The step now waits on the
game's own moving value (`wait_for_host_state`, off the `cmd body slot=1` line
the link trace already writes) until it has settled, taps, and judges the tap by
whether that value moved rather than by a screen hash, and `--vs-both` taps every
console (the "both consoles have to confirm" hypothesis from history.md v0.1.8v,
never previously tested).
Re-run with `--cut-off --vs --vs-both --watch 120`: it coupled at `mask 0x3`,
reached CUT OFF, the walk settled at `FF 00` after 2.1 s, **VS was tapped on both
consoles after that**, and the recorded body was **still `FF 00`** four seconds
later, with the host's command body never moving off `BE 01 02 00 00 80 FF 00 B8
B6` (10 bytes, of which the last four are the wire's own FCS slot) for the rest
of the run. Second corroboration, live and driven by hand: at `mask 0x3` and the
same `FF 00`, a VS tap, a START press, an A press on the guest and a touch all
left it at `FF 00`, and the group then collapsed on its own about **300 s after
coupling** with no race at any point.

**The wire was perfect in the re-run, and the park is a wait that nothing in our
half decides:** host `collected 9218 / empty 5 / clientfail 5`, guest `cmds rx
9248 / answered 9247 / blank 0 / canned 1`, **zero deauth on either side**,
`mask 0x3` held to the end of a 120 s watch (frame 12207), both ARM9s in the
same SDK idle tuple (`pc9 0200FD10`), the host's beacon `bcn 0044` and its
request line armed. **Unknown, unchanged and now narrower:** what the game-select
screen is gated on. Both games are alive, both radios are on, every poll is
answered, no input on either console moves anything, and the screens are black -
so the gate is inside the games' own post-cut-off state machine and not in any
input a player at either console can supply.

**The harness's oracle was reading a region no game ever sees, and that is now
settled from the emitter's own code and from the trace's own fields (v0.1.9bv,
2026-10-01).** `trace_frame`
([PlatformBridge.cpp](src/frontend/tauri/PlatformBridge.cpp)) prints the body as
`frame[28 ..]` with `bodylen = framelen - 28`, where `frame = data + 12` (past the
12-byte TX header) and the 802.11 header is its first 24 bytes. The MP payload
starts at `frame[24]`, so the printed body is **the payload minus its first four
bytes and including its last four** - and those last four are exactly the slot
`LocalMP::ApplyWireFCS` overwrites with the wire's CRC and that the receiver drops
through its own `W_RX_LEN_CROP` before the frame is handed to its game (the crop
amount is the *game's* register value, `framelen -= (IOPORT(W_RXLenCrop) >> 7) &
0x1FE`, so it is faithful). Checked against the trace's own `sync=` field, which
reads payload bytes 0..7 independently: the two agree on the payload bytes and
differ precisely in the trailing four - the sample where they differ holds the
computed CRC `C8 8D FB 6A` against the game's own `5A 5A`.

So a `body[6]=00 80 06 00 B8 B6` is **two bytes of real payload (`00 80`) plus
the wire's four**, and the ten-step "walk" the previous pass anchored on
(`06 00 -> 00 FF -> ... -> FF 00`) lived **entirely inside the wire's slot**. The
anchor is gone. The real numbers, for the record: the post-cut-off command's
payload is 6 bytes `BE 01 02 00 00 80` and the guest's reply's is 2 bytes
`00 80`; the trace also omits the payload's 4-byte MP header, which is where this
file's older notes went looking for state (`payload offset 6`). **No game-written
byte changes anywhere in the transition or the park**, so no byte comparison can
judge a tap.

**The CUT OFF patch is exonerated by a controlled sweep, and the tap is timed
correctly (`--cut-off-xy`, five runs, `logs/v9c*`).** No tap at all: **no
transition whatsoever** (`walk: 06 00`). Tap at `(95,167)`, `(149,167)` or
`(200,167)` - the left end, the documented centre and the right end of the 132x41
patch: **the same transition**, `06 00 -> 00 FF -> 00 FE -> 00 00 -> 00 01 ->
01 00 -> 01 01 -> 02 00` and, in one run, on to `05 00 -> 03 00 -> FF 00` with
`mask 0x3` still held. Tap at the patch's top edge `(149,160)`: no transition. So
the patch is **one control**, the documented coordinate is inside it, and the
no-tap control proves the transition is the game's response to the tap rather
than something that happens anyway. And the tap's *timing* is already right: the
CUT OFF step's own settle puts the VS tap 4-5 s after the tap that started the
transition, by which time the game's own reshape is finished (measured in all
five runs) - so the previous pass's "the VS tap fires mid-transition" was a
misdiagnosis, and the honest statement is that the tap is correctly timed and
simply changes nothing.

**What the games' own display registers say about the black screen - and the
instrument limit that stopped one step short of a false claim.** DISPCNT, read
through the app's own GPU path on both engines of both consoles at the park:
engine A `00011E10`, engine B `00011F10`, `POWCNT1 820F` - LCDs powered, both
engines on, display mode 1, **forced blank clear**. So the hardware is not
blanking the picture: the game is producing black pixels. Blending is off
(`BLDCNT 0000`, `BLDY 0000`) and although windowing is enabled the windows have
zero area and the outside rule enables every layer (`WINOUT 322F`), so no mask is
hiding a live layer either. **A first attempt to go further - "the game cleared
its palette and its BG maps" - was withdrawn after its own control**: the same
reads on the *title screen*, which is visibly drawing (`logs/shots/sweep_live.png`,
73 KB of picture), also return all zeros at `0x05000000` and `0x06000000`, so
`mem` does not see palette or BG VRAM at all. The zeros were the instrument, not
the game. What survives is the register evidence above plus the framebuffer
itself - and the two CPUs on their own idles (the ARM9 in the SDK's delay loop,
`pc9 0200FD10`, the ARM7 in the BIOS idle) with the link perfect.

**Verdict: nothing on our side is indicted for the park.** The harness's oracle
was ours and is fixed (both wrong anchors are deleted, `--cut-off-xy` drives the
sweep); the cut-off patch is measured as one
correctly-tapped control; the receive crop is the game's own register value; and
the wire, screens and registers are all as the games chose them. What is left is
the condition the two games wait on together, and it is still unnamed.

---

## Metroid Prime - Hunters (USA) (Rev 1) — ✅ playable, 2 players

> **2026-10-16d:** this title never had a synthesized Download Play client
> (it is not a `download_play` title and no archived log carries the line for
> it), so its 3- and 4-console walls were never contaminated by the gate; the
> four-console re-run that was owed came back blocked on an instrument - the
> recorded route does not align across four consoles and `lobby_page.py` reads
> this title's own menus as `INGAME`. See `2026-10-16d` in the MKDS section.

Both consoles reach the same two-player lobby with a complete, byte-exact
exchange, **and the match now starts**: driven entirely through the app's own
control channel, the host's `START GAME` launches a `BATTLE` on `COMBAT HALL`
with the 7-minute clock running and each console drawing its own first-person
view with its own HUD (Samus green on player 1, Kanden yellow on player 2) —
`logs/shots/match_b.png` and `match_end.png`; `TIME 6:52`, `RANK 1`,
`POINTS 0/7`. Link `mask 0x3`, `slots 0b11` throughout, both consoles' `wifiio`
dumps live. **Cost at 2 consoles: 39-42 fps** against 55-56 in the menus.

The route, with the coordinates measured off the framebuffer (not eyeballed) —
three `START` presses and one tap at `TOUCH TO START` (128,96), then
`MULTIPLAYER MODE` (172,85), `MULTI-CARD PLAY` (132,80), the host's
`CREATE GAME` (44,89) (the group is named `MELONDS`), the guest's row in
`AVAILABLE GAMES` (186,59) then `JOIN GAME` (122,64), the host's game mode
(`BATTLE`, 52,78 — required before the green check exists), the `ARENA` page's
green check (222,172) which opens the `HUNTER` tab, a hunter (icons at y 121-146,
centre 133, x 26/60/93/127/160/194/227, the first three unlocked in this save),
then a *second* tap on the same hunter to confirm, and finally the host's
`START GAME` (200,26). A tap in `AVAILABLE GAMES` that lands below the row (e.g.
185,97) does nothing at all and says nothing.

The earlier note that the app's input path was suspect was a phase problem: the
attract loop ignores touch until the title screen, and `START` moves it
immediately.

**The v0.1.9az reading - that at 3 and 4 the limit is "the guests' own games",
not the link and not the clock - is superseded by the v0.1.9bb control at the end
of this section.** What it measured still stands (four configurations); what it
concluded about the cause does not: the guest leaves when the host's game is left
idle on its join screen, with or without a third radio.

*Kept as written (v0.1.9az):* a guest that joins a
group with more than two radios in the session aborts its own handshake and
deauthes the host — `deauth sent … reason 0003`, `W_IE 0000`, `wireless off` —
7 to 23 s after joining, while the host's side of the exchange stays healthy
(`cmds tx 5529`, `acks 5529`, `collected 5850`, `clientfail 12`) and every
console keeps running (39-50 fps; a dropped console runs *faster* afterwards,
back in its menus). The three earlier explanations are all ruled out by
measurement rather than by argument: the stall kick (`MD_KICK_AFTER_WINDOWS=0`,
0 kicks, same drops — and with the kick on, the guests still went in the same
frame as each other), the reply-window re-anchor (`dropped 0` in every window),
and the lobby's idle timeout (with the joins serialised the drops are *not*
synchronised, and the first guest died 8 s after joining while it was the only
joined guest).

**2026-09-30 (v0.1.9ba): the second guest is not dropped by the link's delivery
path, and that is now measured per client per AID.** The pass ran the four-console
route with the host already in its hosting state, all three joins overlapped, and
the whole lobby driven in ~8 s (`BATTLE`, the `ARENA` check, three hunter picks,
`START GAME`) - and the guests still fell away in order over the next ten seconds.
Over that run the host's own flight recorder reads `cmds tx 586`, `acks 586`,
`collected 1063` = 278 credited to aid 1 + 328 to aid 2 + 457 to aid 3, against the
three guests' own counts of 278, 328 and 463 answers sent: with p3's six
uncollected answers aside, **every answer a guest sent was credited to that
guest's AID**, with `stale 0`, `blank 10`, `reanchor-dropped 5`, `notlooking 0`, no
`REPLY FIFO OVERFLOW` and no unusable aid. The three-console run reaches the same
result without the per-AID tracker: host `collected 1748` against 1335 + 413 =
1748 answers, `clientfail 1` of 1350 acks. **The guests' deauths are their own**:
every digest reads `deauth out 1 in 0` for the guests and `0/0` for the host, so
what ends each guest's session is a frame its own SDK transmitted (`reason 0003`,
then `wireless off`), never one it received. **And the host's game had not dropped
them first**: each departing guest's last command still carries the host's own
client mask with that guest's bit set (`FE 00 0E` -> `FE 00 0C` -> `FE 00 08` as
clients leave; `FE 00 06` / `FE 00 04` in the three-console run), and the guest
answers that last poll. **Only joining starts the clock**: in the run whose host
never reached its hosting state - a driver tap that landed on MPH's *animating*
menu while `waitfor --change` reported success - three guests sat on `AVAILABLE
GAMES` with radios up for more than a hundred seconds with no deauth and no
`wireless off`. So delivery, crediting, route timing and player count are each
eliminated by measurement rather than by argument.

**What is left is a code-level asymmetry, named but not convicted.** The reply
queue is one per *host* and every client writes into it: `LocalMP::RecvReplies`
files an answer as `packets[(aid-1)*1024]` and the host reads it from a single
`ReplyReadOffset[host]`; `MPStatus.MPHostinst` and `MPStatus.MPReplyBitmask` are
machine-global (the semaphore an answer posts is chosen from the *last* console
that sent a CMD frame, read after the queue lock is dropped); `LastHostID` is a
single instance id; and a console that is *not* addressed by a poll still
transmits a blank reply into that shared queue (`Wifi::SendMPReply(nullptr, 0, ...)`,
stock behaviour). **Owned per console since v0.1.9bh**: `LocalMP` is now per-console
state (`Console`: inbox, read cursor, the host it follows, its own answers queue,
its own recorder) plus one router (`Route`), so an answer is filed with the
console whose command its sender is following instead of with whichever console
last sent a command, and the first checks on the reply path exist
(`tools/coretest/smoke_test.cpp`; the old bus fails three of them, the new one
none). Reading the window this pass removed the blank as a hazard to
*crediting* - a zero-length entry is dropped and never filed, and the window's
"every attached console has sent something" exit can only fire once every
console, the addressed one included, has had an entry read - so whatever the
blank costs when a third radio is listening, it is not a missing credit. None of
the remaining candidates is shown to be the trigger, which is why this row is
still 🟡 and not graded as a fixed defect.

**2026-09-30 (v0.1.9bb): the control was run, and the third radio is not the
variable - the host's own game is.** Three radios, two players, and the host's
lobby driven (`tools/scripts/mph-two-guests.sh`, which takes every step decision
from the emulator's own state: bit in the bus mask = in `MULTI-CARD PLAY`, `role=1`
= hosting, `role=2` = joined):

| run | third radio | host after `CREATE GAME` | guest |
| --- | --- | --- | --- |
| 1 | up, scanning `AVAILABLE GAMES` | left idle | left ~10 s after joining (its own `reason 0003`, then `WIFI: OFF`) |
| 2 | up, scanning (`MD_REPLY_KEEP=0`) | left idle | left ~45 s; the host's window was *flawless* (`collected == cmds tx`, `empty 0 stale 0 blank 0 clientfail 0`) |
| 3 | **never raised** (bus mask `0x3`, app at 56-58 fps) | left idle | left ~10 s; host `collected 785` against the guest's `785 answered`, nothing else anomalous |
| 4 | up, scanning | **driven** (BATTLE, ARENA check, a hunter each, `START GAME`) | never left: `mask 0x7` held past the 60 s watch, **zero deauth frames in the whole run**, host `cmds tx 8207 / collected 8201`, guest `answered 7155`, the two games exchanging live 42-byte bodies and a changing answer counter, 44-45 fps on the two playing consoles |

The third console in runs 1-2 received 5896 frames and transmitted **nothing**
(`tx 0`, no commands, no replies, no blanks), so no traffic of its own reached
the host's reply queue; run 3 shows bus membership and the app's frame rate are
not the variable either. The earlier suspicion about shared state is settled by
the code path rather than by a fix: the bus is one object per app session
(`md::link_bus()` in `PlatformBridge.cpp` hands the same `LocalMP` to every
console's `MP_*` call, with `slot` = console index), so `MPStatus`, the reply
queue, `ReplyReadOffset[]`, `LastHostID` and `SemPool[]` *are* shared by all
consoles in the process - but with one host and one command sender that is what
the emulated hardware is (one reply buffer belongs to the host console),
`MPStatus.MPReplyBitmask` is read nowhere, and runs 2 and 3 hold no anomaly in
that state while the guest still left. **What is still open is a 3- or 4-player
match** (three guests at once, with the host driven as they join), not the
lobby's ability to hold a guest.

**2026-09-30 (v0.1.9bc): the pace fix holds; the match stops on the route's own
`START GAME` tap.** `tools/scripts/mph-match.sh` fires each join's host-lobby
step as the join lands, and the result is the best three-console state this
project has held - `mask 0x7` through the walk, the picks and both guests'
replies, **zero deauth frames**, `role=1`/`role=2`/`role=2`. Then the match
does not start, and the instrument that says why is the screen itself: a
`shot` composes every console at native `256x192`, so `render --mono` at one
character per pixel (and `glyphs.py` per glyph) reads the game's own text
offline. At the moment the driver taps `START GAME` the host's bottom screen has
nothing at `(200,26)` - maximum luminance 133 of 255, no text and no button -
because that coordinate is the two-player layout's. The host's command bodies
stay the 6-byte management form (`FE 00 06 00 00 80`), the guests' own games
leave with `reason 0003` ~2-10 s later, and at four consoles they evict in
rotation (`mask 0xf -> 0x9 -> ...`, recovered and rejoined each time). Recorded
with it, so the next pass does not re-measure it: the re-association AID
collision (`C001` handed to whichever guest returns, both guests' replies then
credited under aid 1), the receive path's correctness (`FinishRX` drops a
unicast frame that is not addressed to this console; the `packet delivered` line
that suggests otherwise is emitted in `StartRX`, before that filter), and the frame rate (36-48 fps per console at four).

**2026-09-30 (v0.1.9bd): the reading job was done, the geometry fixed, and a real
2-console match now runs through the corrected route - 3 and 4 still fail.** Every
3-4 player host coordinate was re-read off the host's own bottom screen (`shot`
composes each console at native `256x192`): `BATTLE` is `(44,68)` on the `MODE`
page, the `ARENA` green check is `(212,172)` (its own tick's centroid, bbox
`x 207..217 y 167..177` - v0.1.9bc's `(220,178)` missed it and left the host on
`ARENA`, so the *next* tap opened `ADVANCED OPTIONS`, whose own tick at `(129,166)`
is what those `picks` shots showed), and the roster's `START GAME` pill is `(198,40)`
(the two-player `(200,26)` sat 2 px above it). A picture digest still cannot verify
a lobby tap - the animated background changes ~83% of the screen per emulated
second, so `waitfor --settle` never fires - but the host's own tab-completion
**dots** at `(67,17)`, `(128,17)`, `(187,17)` light 0..3 with the lobby steps
(`MODE -> ARENA -> HUNTER -> roster`), and the driver now reads them to fire each
host step only once the previous one is done. With that, **two consoles play a real
match again** (both own `RANK 1` / `POINTS 0/7`, `TIME 6:47` ticking, distinct
green/yellow HUDs, `logs/shots/healthy2.png`), while **three and four still fail**:
the host ladder completes (`mask 0x7` / `0xf`, zero deauth *sent* at the roster) and
then each guest's own game deauthenticates (`reason 0003`) seconds after its hunter
pick, `mask` collapsing to `0x1`. The **AID collision is not this blocker** (the
host synced `C001`/`C002` cleanly at three; that collision needs a *returning*
guest), and the in-game 262-byte body marker is **not available for this title**
(the working 2-console host's bodies stay 6 and 38 bytes, same as the lobby). The
one measured lead: in the failing three-console run p2 took **191 of the host's
160-byte session beacons but only 151 distinct seq numbers (40 duplicates)** while
p3 took 195, all distinct - and the guest deauthes within a frame of one such
beacon reaching its own software (`Wifi::StartRX`, `src/Wifi.cpp:1772`). Not fixed:
the hold state that would deliver a held frame twice was not reproduced, and no
speculative change was made.

**2026-09-30 (v0.1.9be): the duplicate-delivery lead is convicted and cleared; the
blocker is that the host is not on the roster when the route taps it.** The instrument is
permanent and sits in the delivery path itself: `Wifi::StartRX` takes the timestamp the
shared bus carried the frame with (`bus_stamp`; 0 for the host's reply handoff, which does
not come off the bus), keeps the last eight `(stamp, length, fc, seq)` tuples handed to
that console's game, and counts any repeat in `MPRedeliveries` - named in a `REDELIVERED`
WARN line and left alone, because suppressing the second copy would hide the thing the
counter exists to show. `Wifi::MPHoldStamp` carries the **un-clamped** bus stamp with the
hold (`RXTimestamp` is raised to "now" for a late frame, so the clamped value is not what
the bus sent). The counters print on the existing once-a-second `[mp] clock probe` line as
`bus deliveries N redelivered M`. **The readings:** *two consoles*, 7,341 hand-overs and
**0** redelivered; *three consoles*, 16,494 (console 2) and 12,172 (console 3) and
**0** redelivered, with no `REDELIVERED` line anywhere in either log; and the reference arm
`MD_MP_REDELIVER=1` (forwarded by `run_app.sh`, documented there, the same shape as
`MD_STOCK_MP`/`MD_MP_FCS`) hands one held frame over twice on purpose, giving **258
hand-overs and 239 redeliveries** with 266 named lines - the counter is connected. **The 40
"duplicate beacons" were never duplicates:** every repeat lands exactly **80.1537 s** after
the first (delta 80153699-80153763 us across all of them - the host's 12-bit 802.11
sequence counter wrapping) and each of the 191 hand-overs carries a **distinct** bus
timestamp, and the send-side trace shows the host repeating seqnos too (`p1 tx bcast
len=176`: 1054 sends, 908 distinct). **What is left, on the host's own pixels.** The
three-console run held the best lobby state this project has recorded to the start tap
(`mask 0x7`, host `role=1`, both guests `role=2`, `deauth-sent 0`, every join first try),
then the guests evicted over ten seconds (`mask 0x7 -> 0x1`, console 2's own `reason 0003`
followed by the host's `reason 0007`). The shot taken at that moment
(`logs/shots/mph3_picks.png`, the host read as "dot 3 of 3") shows the roster's `START
GAME` region (x 148..245, y 28..53 of the host's bottom screen) **empty - maximum
luminance 61 of 255, zero orange pixels** - where the reference roster `F_start.png`
(host and guest both ready in the v0.1.9ay match) reads **182 with 832 orange pixels**;
`mph2_picks.png` reads 65 and zero orange. So the host was **not on the roster** when the
route tapped it, the tap was a no-op, its game never left the lobby, and the guests timed
out. The dot oracle is why the route thought otherwise: it saturates at three, so any page
at or past the roster reads "3", and `host_step`'s retries on the HUNTER page (where one tap
selects and one confirms) can push the host past the roster onto `ADVANCED OPTIONS`. **That
screen-verification job was done in v0.1.9bf** (below). **Not
achieved, and recorded as such: no match started at two, three or four consoles this
pass**, so v0.1.9bd's two-console match is not re-demonstrated - both two-console attempts
failed the same way (the host ladder's retries cost ~25 s and the guest deauthed while it
waited, `mask 0x3 -> 0x1`).

**2026-09-30 (v0.1.9bg): the two-console control is re-established, and at three consoles the
second guest's join is *not* refused - the host serves it and then abandons its own round
loop.** v0.1.9bf's oracle called a *real* match a dialog (`TOPCOLOUR_MIN` 10000, taken from
in-play frames: a match's `LAWS OF BATTLE` briefing reads 6388-6551 colourful pixels, the
dialogs it was built for read 915-928, play reads 16384-32768), so the live two-console match
(`SPAWNING IN 5...`, `RANK 1`, `TIME 6:54`, distinct green/yellow HUDs) classified as `DIALOG`;
at 3000 both consoles read `INGAME` and the frame is in the selftest (17 labelled frames, 0
wrong). On the wire, a second guest at three consoles is admitted, addressed and credited every
round: polls offered to both clients under their own AIDs (`AID 1 (from the association;
W_AIDLow reads 0)` / `AID 2 ...`), both arms answered (`arm #2429` / `#326`), both replies
collected (`collected a reply from p2 for aid 1` + `...from p3 for aid 2`, `collected=0006 of
mask 0006`, `clientfail 0`), the combined ack delivered to both, and 100% of the host's beacons
received by each guest in every steady second to the end. The group ends on the host's
side: its round loop stops mid-bucket (28 of ~60 cmd/ack) and never resumes while the beacon
keeps firing 4-5 per bucket and `role` stays 1 - host's last ack `t=84 462 514`, beacon
`84 465 330`, both guests' `deauth sent ... reason 0003` at `84 468 018`/`84 468 100`. Each
guest's chain for that beacon: delivered (160-byte `fc=0080`) -> `beacon accepted (our BSSID):
USCOUNT 2CB584C -> 2CB8E8C` -> `USCOMPARE0 <- 9001` -> `beacon IRQ14 fired (source 0) with no
beacon slot armed` -> deauth from `slot 2 (LOC2)` -> `wifi: TRX power OFF`. Refuted by counter:
beacon starvation, reply-window starvation, AID mix-ups (the trace printed the raw `W_AIDLow`
and called polls "NOT ours" that the code correctly answered - fixed to the resolved AID),
frontend kicks (`kicks 0`), and our own input (the app's `[control]` log has no input in the
window). **The host's own view (the pass's second half).** `MPDiag` gained a round ledger and
`Wifi::USTimer` a watchdog that prints one Info line when the host's own game stops sending
commands for two seconds while this console is still a host: last seq, ms since the newest
client's association, rounds sent (and how many addressed 2+ clients), the lobby's CMD-writing
lifetime, acks and how many reported a failed client, and whether the game's beacon slot is still
armed. Exercised live (`logs/di3_app.log.gz`: `59758 ms after the newest client associated, after
3795 rounds (405 addressing 2+ clients) over 63.316s ... 3795 acks, 6 of them reporting a failed
client, and the beacon slot is still armed`). Four three-console runs share one shape - the host's
game stops writing its CMD slot and the guests' `reason 0003` deauths land in the same
millisecond, with the beacon still firing from the firmware's armed slot and `role` still 1 - and
the stop came 4.5-60 s after the newest association, so no fixed timeout. Moving every guest's
join ahead of the host's ladder and picks (so no client ever arrives at a lobby already offering
`START GAME`) was tested and **refuted**: the lobby died the same way. Nothing on the wire precedes
the stop - every round credited, both replies handed over and read, `clientfail` 0, and the guest's
`answer sent` body byte-identical to the host's `collected a reply` body - so the two games
disagreed about state, not bytes. **No three-console match this pass**; the death reproduces within
seconds of the second association. Fatal windows and rate tables: `logs/dc3_deauth-finding.txt`,
`logs/di3_host-loop-finding.txt`.

**Then the host's own bookkeeping was dumped and compared (the pass's third part), and it is what
settles the fork.** Found by scanning main RAM for the console MACs (page `02101000`, sweeps kept
as `logs/kw_sweeps.tgz`; the page-hash diff over four 260-page sweeps diverges on 22 pages, all
UI/text, none of them the record): one peer record per console - `+0xC8` a count whose byte at
`+0xCA` is how many clients that console has, `+0xCC` the newest client's MAC (the host's MAC on a
guest), `+0xD4` that peer's record pointer, `+0xD8` this console's own index in the party. **It is
correct in both sessions**: the host reads `01` with one client associated and `02` with two, the
MAC is the newest client's, each guest reads the host's MAC plus its own index (1, then 2), and
the record is stable across the stop and for the rest of the session. `W_CMDStat0..2` read `0000`
at every sample before *and* after the stop, and every reply was handed over byte-identically - so
no value this link supplies is stale, wrong or missing at the moment the lobby dies. **The
walk-away is also not three-console-specific**: in this pass's own two-console run
(`logs/ct2_match.log`) the guest's pick did not land either, the host's roster never listed it
(`pill 0` at 07:24:04, 07:24:24, 07:24:43, 07:25:23), and the guest was then dropped **four times
in a row** - its own deauth and radio off each time, 07:25:36, 07:26:04, 07:27:58, 07:28:32 - with
no third console in the session at all. v0.1.9bb measured that drop rule on the host's *page*;
here the host was driven all the way to its roster and the guest still went, so what the rule
tracks is the guest's entry landing in the host's roster - a lobby that does not progress loses
its guest at any console count. At three consoles the lobby
cannot progress at all, so the loss is guaranteed: `logs/dc3_match.log` has the first guest's pick
register and the roster offer `START GAME` (`pill 413` at 05:57:51), then the *second* client
associates and the same roster reads `pill 0` (05:58:33) with the ready player gone, and
`logs/dk3_app.log` has both guests' own answers stop carrying state (`00 00 00 00 B8 B6`, from
+83.90 s and +93.72 s) while every round is still credited to both. **Verdict (the mission's
fallback branch): the divergence that precedes the walk-away is in the games' own lobby state, not
in anything this link hands them, and there is no supplied value to correct.** What 4-player
compatibility would need that this stack cannot give is therefore not a better value on the wire -
the link already carries a second client's bytes, counted and credited - but the games' own lobby
admitting a second remote client at all, which this stack cannot supply from outside. Raw reads:
`logs/bg_memory-vs-2console.txt`.

**2026-09-30 (v0.1.9bf): the route is driven by a page classifier, three of our own
instruments were convicted, and the blocker moved to the guests' own screens.**
`tools/scripts/lobby_page.py` reads each console's page from its own pixels - `MODE` by
the BATTLE disc's orange, `ARENA` by its green tick, `HUNTER` by the seven hunter discs,
`ROSTER` by the roster's own ADD BOT ring, `INGAME` by the match HUD's ink together with
a colourful top screen, `DIALOG` for that ink over a plain top screen, `UNKNOWN` for
anything else - and carries its own test (15 labelled frames, 0 wrong). The completion
dots are gone from the driver.

*Three defects it caught, all ours.* (1) The ladder's steps named the page they *tap on*
rather than the one they are trying to *reach*, so the BATTLE step read `already at MODE`
and never tapped, leaving the host parked on MODE - which is exactly the state that drops
a guest in ~10 s (`logs/or2_match.log` shows the guest leaving and being rejoined while
the host never moved). Steps now name their destination and tap only from the page
exactly one before it, so a retry cannot walk past the roster. (2) `ROSTER` as the
"nothing else matched" fallback made a **mid-transition frame** - during which a console
matches no page at all (`logs/shots/ref_transition.png`: 0 ink, 0 discs, 0 hunter discs,
0 ADD BOT) - read as the roster: in `logs/or3_match.log` the host's ARENA step read
`ROSTER` one second after its own tap, the HUNTER step then refused to tap "past" it, and
the host was left on its hunter page for the rest of the run. ADD BOT is positive evidence
instead, and clean: **90 orange pixels on every real host roster** (one player, two, the
old `F_start`, a live one) against 24 for ARENA and 0 for MODE, HUNTER and the transition.
(3) **The match oracle was wrong**, and only looking at the frame showed it: a host showing
`THERE ARE NOT ENOUGH PLAYERS TO CONTINUE` over the in-match HUD reads **2729** ink pixels
against a real match's **2730**, because the bottom screen is the same HUD either way - the
top screen is what differs (16473-32724 colourful pixels for the recorded matches, 915 for
the dialog). The oracle now requires both and reports `DIALOG`.

**Two consoles: not re-demonstrated, and the failure is not the route.** A live session
reached the best lobby this project has recorded - both players listed in the host's
roster, the `START GAME` pill up (`logs/shots/sw.png`, pill 414; the guest's own screen
`PLEASE WAIT FOR ALL PLAYERS TO BE READY`) - and tapping the pill still failed the
handshake: `logs/shots/stm.png` is the host's "not enough players" box and the guest's
"disconnected from the host". **A roster that lists a second player and offers START GAME
is not sufficient evidence that the guest's side of the join completed.** The pill is
still the right test for everything upstream of the start (measured directly: it appears
**2 s after a guest's confirm** with the host idle, pill 0 -> 414).

**Three consoles: the host is verifiably on the roster and the guests are the blocker.**
The ladder runs one verified tap per step (`MODE -> ARENA -> HUNTER`, re-reading through
two `UNKNOWN` transition frames) and the host then sits on the roster for the whole watch
(ADD BOT 90, pill 0) listing **only itself**. The reason is on the guests' own screens:
after `select` *and* `confirm`, and after the new guarded retry, **p2 and p3 both still
read `HUNTER`** - their hunter chosen, their confirm not accepted - so neither is listed
and `START GAME` never exists (`logs/or3b_match.log`). Four consoles were not reached: the
pass's budget went into the classifier and this run. **Next: find why a guest's own hunter
confirm is refused with a third console in the group** - that is the whole distance to a
three-player match, and it is the guests' games rather than our route. **No match started
at any console count this pass**, so the two-console match recorded above remains the last
confirmed one.

***Superseded too* (v0.1.9bb): what "changed the outcome" below was never the
radios or the join order but the host's game - the routes that waited on each
guest's screen left the host idle on its join screen for as long as the waiting
took, and that is what the guest's own game times out on. Kept as written:**
what changed the outcome was the joins: **serialising them is strictly better
than overlapping them**. One guest tapped in, then the driver waits for that
guest's own screen to leave `AVAILABLE GAMES` before tapping the next. At four
consoles that kept the host and one guest (p1+p4) alive all the way to the
hunter roster and the host's `START GAME` (`mask 0x9`) — a second two-player
lobby where the overlapped run had lost all three guests — and at three consoles
both guests died anyway (`mask 0x1`). So the boundary is the number of radios on
the bus, not the order of the joins, and the next question is what in the
guest's handshake the emulated bus is failing to deliver when a third radio is
listening.

**What lands** (`logs/rom_mph_join.log`, history.md v0.1.9ad): host
`cmds tx=7986 acks=7986 collected=7986 empty=0`; guest `cmds rx=7986
answers=7986` (blank 0, canned 0), `replies sent=7986`; `slots 0x003`,
`deauth out/in 0/0`; host sent `17219 (1,127,344 bytes)` with the guest receiving
the same, and the guest sent `7988 (399,388 bytes)` with the host receiving the
same — byte-complementary both ways. Last command `seq=3410` at +585.01s, both
consoles on arm9 `pc=0x02088ba4`.

The v0.1.9ad harness found joining at `JOIN GAME (123,72)` and pinning the two
consoles' stored frames stepping apart at frame 27000→27250; the app's route
above is the same screen, and its row-then-button form (`186,59` then `122,64`)
supersedes those two coordinates.

**The 117-second stall was this lobby**, `game-captures/mph-lobby/sheet.html`:
the guest sat on `SELECT HUNTER` because nothing had chosen a hunter or pressed
the host's `START GAME` — both of which the app's route now does.

**Still open:** a 3- and 4-player match (see the limit above), and whether the
match runs to its own results screen (the clock was still at 6:52 when the
session was stopped, so the end-of-match screen is unmeasured).

---

## Star Fox Command (USA) — ✅ playable over single-card Download Play, 2 players

2026-10-14: the synthesized Download Play joiner (registry `ASFE`, `download_play`) attaches to the Recruitment host, completes the image transfer and reaches a live 2-player dogfight (to-do.md Milestone 4 record). What follows is the earlier bus-level record, kept because the numbers still describe the host half.

The wire works; the cartridge has no multi-card join to drive.

The title's only local option is **Download Play → Recruitment (host)**, and
both consoles ran the host lobby with a complete bidirectional exchange
(`logs/rom_sfc_harness.log`, history.md v0.1.9ac): console1
`sent=489 (99,532 B)` / `received=501 (101,980 B)` with console2 complementary,
`slots 0x3`, both `[host]`, radio power-cycled 3× over 26,000 frames (434.6
emulated s, 3.56× realtime), zero bad-channel drops.

**Why it still does not link:** the *joining* console is the **firmware menu's
Download Play client**, which FreeBIOS cannot boot, and there is no
in-cartridge join path for the second player. This is not a defect in the link —
see [Download Play: planned](#download-play-single-card-planned-not-supported-yet).

This title never wrote a `.sav` in the folder.

---

## Super Mario 64 DS (USA) (Rev 1) — 🟡 Download Play transfer completes, no battle yet, 2 players

VS mode is host plus Download Play clients only; there is no multi-card
multiplayer. 2026-10-14: registered (`ASME`, `download_play`), and the transfer now completes 3/3 (`514896/514896 bytes, 1023 packets, BOOTED`) once the host's TOUCH TO START bar is tapped with the guest registered. Menu route mapped to the CONTINUE/QUIT VS hub; no battle started (guest attach 2/4). Full record in to-do.md Milestone 4.

---

## Tetris DS (USA) — ✅ playable over single-card Download Play, 2 players

The first Download Play title to reach real gameplay, and the cheapest route on record: one registry entry (`ATRE`, `download_play`, no code change) and a menu walk with no geometry to measure — every step is a full-width banner that takes a centre tap, and selection vs confirmation are separate (tap highlights, A confirms).

Route (P1 hosts, console 2 is the synthesized client), all taps bottom-screen coords: MULTIPLAYER `(128,56)` then A → INVITE `(128,33)` → STANDARD `(128,96)` → the waiting room (`Waiting for other players to join... Press the A Button`). The joiner hears the host beacon (serial `00000005`, empty SSID, channel 7), authenticates (`0000`), associates (`AID C001`) and exchanges polls — but the download only starts when the host's A press begins the session: before it, `download: 0 of 1004732 bytes` with 5,669 commands answered; after it, `1004732 of 1004732 bytes in 4021 distinct packets · BOOTED`, `mask 0x3` (`logs/tetris_app.log`, shots `logs/shots/tetris_*.png`).

Past the boot both consoles show the STANDARD lobby with a 2-player roster, the host's A moves it to `Please wait until all of the players have set their rules`, the guest's A confirms, and a versus match runs on both screens with distinct live boards (different wells, different NEXT queues, gameplay-evolving 20 s apart), both frames advancing in lockstep, no further deauths. One host deauth (`reason 0x0013`) is on record mid-session with the session unaffected — unexplained, noted, nothing depends on it.

First boot writes the game's save (new `Test Roms/Tetris DS (USA).sav`; supervisor-pinned, exit 0). Higher player counts (the title supports up to 10 over Download Play) and the other modes (MISSION, PUSH) are unmeasured.

### 2026-10-23 — re-runs of the sessions that failed before the rendezvous: MKDS's reply path is cleared and its leave is unchanged, and Tetris's four-console room is honest about its cost

Same recipes, same routes, same measurements, on the binary with the reply rendezvous
(`game-captures/mp-rerun-2026-10-23/summary.txt`).

| title / route | rounds | failed acks | rendezvous | in-room fps | outcome |
|---|---|---|---|---|---|
| MKDS, 4 consoles, cut-off walk | 2,755 | 1 (0.0%) | 249 held, 2.25 s | 44.8 mean | room ends at the walk, as before |
| MKDS, 4 consoles, no press x2 | 509 / - | 0 / - | 0 | 58.4 mean | bus leaves `0xf` at frames 13,057 / 12,977 |
| MKDS, same, rendezvous off x2 | 390 / 372 | 7 / 7 | 0 | - | leaves at 12,907 / 12,867 |
| Tetris, 3 consoles | 623 | 1 (0.2%) | 23 held | 59-60 | guests leave at 33.77 emulated s |
| Tetris, 4 consoles | 721 | **0 (0.0%)** | 216 held, 1.39 s | **14** | all four in the room |
| Tetris, 4 consoles, rendezvous off | 622 | **85 (13.7%)** | 0 | 39-42 | room collapses to the menus |

**MKDS:** the leave now lands on the same absolute frames as the pre-change record (12,813-12,922), with
the rendezvous on *and* off, so it is scheduled by the title and was never a reply the link lost - the
6-8% short windows the record had behind it are gone (1 failed ack in 2,755).

**Tetris at four consoles:** every client is credited, and the room pays for it in frame rate - 14 fps
against 39-42 with the rendezvous off, where a seventh of the acks fail and the room dies. The cap is
not the lever: a one-frame budget gives the same 14 fps. The emulated bookkeeping says the holds are
short (0.4 emulated frames per held round, ~2% of the session), which does not explain the wall-clock
difference, so the open defect is the deferral's **wall** cost at four consoles.

### 2026-10-23 — the round is a rendezvous now, and every asked client is credited in its own round: `clientfail` 0 at three and at four consoles, and the room still ends

`Wifi::ProcessTX` phase 14 holds a command's ack open when its first poll leaves an
addressed client uncredited. The slot keeps `W_TXBusy` set (the game waits as it
would for a slow radio) and re-polls through `Platform::MP_SetReplyEager` - a poll
that never blocks, because the waiting is the slot's own emulated time - until the
round's mask is covered or `MD_REPLY_DEFER_FRAMES` emulated frames (default 3) are
spent. The give-up is the only timeout path left and it is counted
(`reply_defer_exhausted`), beside `reply_deferred` and `reply_defer_us` in the
reply digest and `probe`. Healthy rounds never enter phase 14.

| run | acks with a failed client | rounds held open | gave up |
|---|---|---|---|
| two consoles | 1/799 (0.1%) | 15 | 1 |
| three consoles | **0/560 (0.0%)** | 26 | 0 |
| three consoles, second run | 1/514 (0.2%) | - | 1 |
| four consoles | **0/440 (0.0%)** | 43 | 0 |
| three consoles, `MD_REPLY_DEFER_FRAMES=1` | 3/657 (0.5%) | 22 | 3 |

A round that has to wait holds for about **one emulated frame** (16,992 us measured
on the three-console run) - the one-frame-late answer the previous entry counted.
Forcing the bound to one frame makes three rounds give up and exactly those three
acks report `clientfail`, which is how the violation counter is known to be
connected rather than merely never firing. The two-console control still reaches a
live STANDARD match.

**The room still ends, and not because of a reply.** At three consoles the title
reaches its own table with all three players listed, and then the guests' games
write `reason 0003` at ~34 emulated seconds with `clientfail` 0 - one run kept the
host's table while both guests left, another put all three consoles on the game's
own communication error. So the leave is the title's decision about the room, not
a reply the link lost. Evidence: `game-captures/tetris-3p-rendezvous-2026-10-23/`.

### 2026-10-23 — the reply window measured at two and three consoles: it is not losing the round, and waiting for the client's own round cannot recover it

One binary, one route, three sessions (`tools/scripts/mp-ab-run.sh`, new;
`tools/scripts/mp-window-latency.py` reads the windows; evidence
`game-captures/tetris-3p-window-2026-10-23/summary.txt`).

| run | windows | acks with a failed client | outcome |
|---|---|---|---|
| two consoles, default ceiling | 916 | **10 (1.1%)** | live STANDARD match, both boards |
| three consoles, default ceiling | 357 | **34 (9.5%)** | no match; the title's communication error on all three |
| three consoles, ceiling + 6 probe frames | 459 | **28 (6.1%)** | still no match, and every remaining miss was owed after ~165ms |

The failing rounds owe **both** guests over the run (AID 1: 15 windows, AID 2: 26),
so the misses alternate instead of sitting on one console, and the owed answer is
collected **within the next three windows** in 33 of 41 cases - the answer exists,
one window late. A window now outlives its ceiling for an owed client whose own
emulated round has not come round yet (the gate is that client's `host_polls`
against a per-AID baseline taken when the command went out, counted as
`windows_extended` / `extension_frames` / `closed_owed` in the reply digest and
`probe`, behind `MD_REPLY_EXTEND_FRAMES`, default 0). It defaults off because the
probes do not deliver: at 6 frames they remove a third of the failures and leave
the rest owed after ~165ms, and the room dies earlier than with them off. A host
sitting in a reply window is holding the client that owes the answer - every
console finishes frame N before any starts N+1 (`Lockstep` in
`src-tauri/src/lib.rs`) - so no wait can let that client run; only ending the
frame can, which is why the owed answer always lands in the next window.

### 2026-10-23 — the roster record is not the wall: the host's three-player packet is fully populated, both guests' records reach its game in the same windows, and the room still closes on the rules confirmation

**The packet, read per player.** The host's three-player body is
`F6 00 | mask | 05 9D | 2N-1 | present-mask | <2 bytes per player> | counter` -
`0006 / 0007 / 0007` once both guests are credited, with three player slots. The
present mask climbs `0001` -> `0003` -> `0007` in the first three rounds, i.e. the
host's game learns about player 2 and then player 3 from their own answers; from
round 4 on the slots carry the same token the guests echo (`FF 7F`, `6A 82`,
`30 98`, `04 C9`, ... and finally `20 20` for all three), and at no point does a
slot stay blank or pending. **No side is dropping the record.** In the clean
three-console session both clients' replies were handed to the host's game in
444 of 454 reply windows (`scan4.log`), each guest's full name transfer arrived
(chunks 0-4 from both), and the host's own ack told the guests `clientfail 0000`
in 445 of 475 rounds. The two-console control runs the *same* exchange on the
same binary with `cnt 0003` instead of `0007` and goes on to a live match, so the
body's shape and content are not what the title rejects (`rounds.txt`).

**What still ends it.** After the second guest's rules confirmation the host's
screen stays on `Please wait until all of the players have set their rules`, the
host's game closes the room (`06 01`), both guests deauth, and all three consoles
land on `A communication error has occurred` (`please-wait-then-error.png`). The
one measured transport asymmetry is the rate at which a guest puts *nothing* on
the wire for a round: the host's ack reports a failed client in 6.3% of rounds at
three consoles (30 of 475) against 0.3% in the two-console control (15 of 4,394).
None of the 30 is a reply the transport lost: 20 closed with `collected=0000` (no
client answered that round at all - the host's next command overtook a guest
that answers on its own cadence) and 9 with one client's reply; the window's own
trace says so. The ack line that measures this (`[mp] ack sent: cmdcount ...
clientfail ... last window credited ...`, verbose-gated) is new in `SendMPAck`.

**Verdict:** the record is not dropped by the guest, the host, or the bus - all
three are on the record and in step - so the three-player start's gate is the
title's own, and the remaining measurable difference from the working
configuration is a guest that skips answers ~7% of rounds when two guests share
the room. No three-console match was played this pass.

### 2026-10-22j — the guest panel is swept with the menu harness and nothing on it moves the room: the answer byte follows the host's packet, and the host's game never leaves its three-player roster while both guests are credited

`tools/scripts/tetris-rules-sweep.py` (new) reuses `menu-map.py`'s machinery -
the bottom-panel signature, the settled read, the action applier - and adds the
one thing this question needs: an oracle. After every attempt it re-reads that
console's own last `answer sent` line from the verbose link log, so "did this
input set the rule state" is answered by the guest's game, not by a picture.
Each attempt starts from the panel's own snapshot (`restore`, verified against
the recorded signature), and leaving the panel is recorded rather than
mis-attributed.

**The sweep:** 48 touch points (32-pixel grid over the whole bottom screen), 12
buttons, and 6 ordered pairs on **each** guest's NAME/ITEMS/HANDICAP/TEAM panel,
the room re-formed under all three consoles (`panels-2p-vs-3p.png` shows the
same table at 2 and 3 players). Not one attempt changed the guest's answer:
`01 0D 20 20 36 00 46 1D B8 B6` before, and the same after every touch, button
and pair (`sweep-grid.txt`, the sweep's own transcript). The host stayed in
`05 9D` throughout the session that followed.

**Why the bit could never be "set":** it is not panel state. In a later session
the same guest answered `01 8D 00 00 01 00 ...` to a roster command carrying
`FF 7F` where it had answered `01 0D ...` to one carrying `01 00` - no input
between them, and the host still stayed in its roster (`sweep3.log.gz`, line
19637). So the answer byte tracks
the host's packet stage, and the previous entry's `20 A0` / `20 20` comparison
was two answers to different packets.

**Where the wall actually is, counted across the sessions:** with **both guests
credited** (`collected=0006 of mask 0006`) the host's game sends only `05 9D`
(277, 600, 1,079 sends by run) and never `04 9D`; with **one client credited** it
sends `04 9D` 2,430 times; at two consoles it sends `04 9D` 2,050-2,097 times and
`05 9D` not at all. The title's own three-player room machine never transitions
to its match while two clients are present, and nothing a guest's panel can send
moves it.

**The two-console control is unchanged and live:** 3,120 `04 9D`, mask 0x3, both
boards falling (`ctl2c.log.gz`, `2p-control-final.png`).

### 2026-10-22i — the three-client accept is not refused: all three are accepted, addressed as AIDs 1 and 2 and credited, and the room reaches a three-player table; the wall is the title's own re-entry after its pre-match close

The last pass's next instrument was built: `TXSendFrame` now prints the reply
slot its game wrote, the length byte in wireless RAM and the AID the answer is
stamped with; `CheckRX` prints the AID an association accepted; `StartTX_Cmd`
prints the host's own client mask and the transport's poll mask as the AID lists
they stand for whenever either changes; and `LocalMP::RecvReplies` names an AID
collision once, with both senders, when two consoles answer as one AID inside the
same few milliseconds. All of it is behind the existing verbose trace.

**The accept is accepted.** In every three-console run the host's `06 01 06 00 03
11 05 ... 4E 06 00` is preceded by `command addressing changes: the game's own
mask 0006 (1,2), polled as 0006 (1,2)`, both guests print `association accepted:
the host handed this console AID 1` / `AID 2`, every answer is `stamped AID 1` /
`AID 2`, the collision detector never fires, and the host collects both in the
same window (`collected=0006 of mask 0006`, 30-455 windows per run). The bookkeeping
the accept was blamed for is clean.

**The room then reaches a three-player table.** With the host's A pressed first
and both guests confirming, the host's own game lists all three players and shows
`Please wait until all of the players have set their rules`
(`tetris-3p-2026-10-22/3p-table-pleasewait.png`, `probe6.log.gz`). **The order is
the game's, not the driver's:** a guest's A does not open its rules panel until the
host has pressed (five attempts, panel never moved, `probe4`), so guests-first is
not a legal order at all - the two-console recipe's opening guest press only works
there because the host reaches its press a second later.

**The `06 01` is the room's handoff, and both guests take it identically.** A
close slice was added for exactly this comparison (host: `close broadcast #n`
with its game mask, the last window it credited and how long before; guest:
`close received #n` with its clock, its AID and its deauth counters; `deauth
sent/received ... at <us>`; `association requested ... at <us> (n after the
close)`; and the reply window now names which senders a trim dropped). What it
shows at three consoles: the host broadcasts `06 01` **15 ms after the second
guest associates and before either guest has answered a command**, repeating it
124-141 times; both guests clear their AID and deauth within **8.3 ms of each
other**; both re-associate together **0.93-1.3 s later**; both are handed their
old AIDs (1 and 2), the collision detector stays silent and the host credits both
again. The previous entry's "one guest re-associates while the other's game
leaves" was a misread: **both** guests leave and **both** come back, and the
screen difference was only which console's press had dismissed the game's own
error page. The link treats the two guests the same (`slice_run.log`,
`probe9.log.gz`, `probe10.log.gz`).

**2026-10-22j corrects what that bit was.** It is not a rules-set flag and there
was nothing for a guest to set: the answer byte follows the *host's packet*, not
the panel. The same guest answers `01 0D ...` to a roster command carrying
`... 01 00` and `01 8D ...` to one carrying `FF 7F` (`logs/sweep_run.log`, line
19637), with no input between them. The panel sweep below tries every input on
the panel and the answer never moves; the byte is a protocol stage marker.

**What the three-player roster waits on is not on the guests' panel at all.**
After the handoff the room re-forms and all three consoles sit on the
three-player table, each on its own NAME/ITEMS/HANDICAP/TEAM panel
(`panels-2p-vs-3p.png`) - and the host then runs `05 9D` 277-1,079 times and never
sends `04 9D`. Counted across the sessions in `game-captures/tetris-3p-2026-10-22/`:
with **both guests credited** (`collected=0006 of mask 0006`) the host sends only
`05 9D` (277, 600, 1,079 by run); with **one client credited** it sends `04 9D`
2,430 times; at two consoles it sends `04 9D` 2,050-2,097 times and no `05 9D` at
all. So the title's own room machine only starts a match when at most one client
is present, and a three-player room stays in its roster exchange indefinitely.
No AID, delivery frame, slot or trim differs between the two guests.

**The two-console control is live on the instrumented build and on the committed
binary**, re-run after this slice: the route's own proven presses, 3,062 and
**3,120** `04 9D` in the two runs, link mask 0x3 at +50/+53 s, both boards
falling, save byte-identical afterwards (`ctl2b.log.gz`, `ctl2c.log.gz`).

### 2026-10-22h — the 14:03 log was the room closing, not a match; the same timeline with every press proven plays a live match, and that is the whole difference

The press timeline was recovered from `logs/tetris2-accept.log` to the second (p2 A
at 19:03:05, p1 A at :06, p1 A at :08, p2 A at :10) and the log turned out not to
contain a match at all: at +29.56 s both consoles wrote their own
`deauth ... reason 0003` on the host's `06 01 02 00 03 11 05 00 ... 4E 02 00`
(lines 16110/16130), and the `F6 00 02 00 04 9D` family every earlier pass read as
"the match starting" (18 commands) runs *after* that deauth, on a dead link. The
session's own shots at 19:03:14 are 2,555-byte blank frames. The `2p-match-play`
snapshots at 14:45 are real gameplay, but no retained log produced them, so the
protocol record of that session is gone.

Replayed with `tools/scripts/tetris-confirm.py`'s proof (press, re-read that
console's panel, re-send until it moves) in the same order - guest A, host A, host
A, guest A - the match starts: every press on attempt 1, **5,519** `04 9D`
commands, the guest answering `01 8D ...` throughout, `LINK ACTIVE` at
+105 s with both boards live (`replay_timeline.log.gz`, `replay-after.png`). So the
wall was the press *landing*, not the tree. `tools/scripts/tetris-4p.py --start`
now runs that two-stage sequence with proven presses (the host's second press sent
without proof, because the room swallows it by design) and was tested cold on the
driver alone: **1,079** match commands, live match on both screens
(`driver2p_start.log.gz`, `driver2p-after.png`).

At three consoles the same driver dies **before any press**: the host's own accept
of a three-client room (`06 01 06 00 03 11 05 ... 4E 06 00`, 5 sends) makes all
three write `reason 0003`, and the link goes to mask 0 (`replay3p.log.gz`,
`replay3p-after.png`) - the `05` byte is not the cause, because the same accept
form is sent 5 times in the 14:03 log and 4 times in the working replay without
ending either room. The store watch that was to read the guest's "slot record"
found no such record in RAM: the answer frame lives in the wifi block's 8 KiB RAM
and the hw clears it as it consumes it (`accept-trace.txt` section 11).

### 2026-10-22e — presses proven landed: the rule confirm still ends a three-console room in the two orders measured, so the wall is the title's own handshake

`tools/scripts/tetris-confirm.py` presses A, waits for that console's own bottom
panel to move, re-sends until it does (five attempts), prints the attempt count and
exits non-zero when a press never lands. The room gate is the game's own protocol
(the host's log carrying `body[6]=F6 00`), not a screenshot: a batch run without
the verbose line read NO-TABLE on sessions that were at the table. With that gate,
every press landed on its first attempt and both measured orders still ended the
room - 1,2,3 (host first) with 5 deauths, 2,3,1 (guests first) with 3 - while the
link credited every reply (section 5). The two unmeasured interleavings were only
attempted against the blind check and are not counted; no fifth ordering was run.
Three-player multi-card play needs a fix in the title's room state machine (the
place the `01 8D` rule-set flag is written), not another press order.

### 2026-10-22g — the two-console control is bisected: the code variables are cleared, the guest's own room state is the difference

The 14:03 recipe was re-run with one variable changed at a time. Five arms, one
result: head binary verbose on (host runs `F6 00 02 00 04 9D ...`, 2,710-3,322
commands, both screens on the setup table), head verbose off, head with a fresh
save, head pinned `taskset -c 0-3`, and a worktree built at `f7ce6897` - the parent
of the reply-window commit - from its own tree. **The reply-window change is not
the regression**, and neither the link logging, the save, nor the CPU affinity
moves it. Frames are not lost: the guest reads every match command (six halfword
reads each) and its answer is collected every round (`collected=0002 of mask
0002`).

**Where the runs part:** the 14:03 log's guest reached `30B 00 00 0B 04` then
`30B 00 80 0A 04` for its table-poll answer *before* the host sent the match
family; today's stays at `00 80 0A 03`, `00 80 0B 03`, later `00 80 20 20`. So the
host starts and the guest's own game never joins the phase. No knob in this tree
restores the match, and the next instrument is the guest's writes after it
consumes the table poll (`accept-trace.txt` section 9, `b2_noverbose.log.gz`,
`b2_freshsav.log.gz`, `pre2_prevcommit.log.gz`).

### 2026-10-22f — the `06 01` command is the room's normal close, and the 04/05 phase after it is the whole difference

Takes 2026-10-22e's command apart against the last two-console match. The same
body (`06 01 02 00 03 11 05 00 00 00 00 4E 02 00`) appears five times in the 14:03
two-console log that started a match, the guest deauthenticates on it, and the
host then runs `F6 00 02 00 04 9D 03 00 03 00 ...` for the rest of the session. At
three consoles the same step happens with `06 01 06 00 ... 4E 06 00`, the three
deauths are written, and the host goes to `F6 00 06 00 05 9D 07 00 07 00 <names>
<counter>` and stays - 437 commands with the default reply window, 1,443 with the
old 34 ms / two-frame window, and **no** match-family command in either arm
(`accept-trace.txt` section 8, `ab3_default.log`, `ab3_old1.log`).

Byte 2 is the client mask (02/06) repeated at 14-15, byte 4 is `9D` in both, byte
3 is the phase (04 match, 05 roster/name exchange), and byte 5 is 03 at two
consoles against 07 at three - the player count, not a fault.

**The bookkeeping is clean:** every window with all three in reads
`collected=0006 of mask 0006` (558), the host hands client 1 and client 2
separately (648/563 handoffs), the guests answer with distinct ids (`01 0D 20 20
21 00`, `... 23 00`), nothing is credited twice, `stale` is 0, and the 20-byte
roster command is delivered to both guests, read by their own games and answered
in the same round.

**The A/B rules the reply window out:** four frames and the old two frames reach
the same roster loop and the same room end. **And the two-console control is no
longer reproducible:** both arms reached the match family at two consoles (2,710
and 3,322 commands) but the guest's answers stay at `00 80 20 20`/`00 80 00 00`
and both screens sit on the setup table after 30 s (`2p_default_end.png`,
`2p_route_start_end.png`), where the 14:03 guest answered `00 80 0A 04` through
the match. So 2026-10-22c's match stands as one run.

**Three-player multi-card play needs** the title's own three-player roster
exchange to complete inside its room state machine; no press order, reply window
or transport change moves it, and nothing measured points into this tree.

### 2026-10-22e — the room ends itself on one host command, pressed or not: the confirm press was never the trigger

Corrects 2026-10-22d. The four confirmation orders were all run at three
consoles with the setup table confirmed from the host's own `F6` command and every
press proven landed on its first attempt (order, table, outcome): 1,2,3 (43 `F6`),
2,3,1 (30 `F6`), 2,1,3 (21 -> 24 `F6`, 9 deauth lines), 3,1,2 (12 -> 15 `F6`, 9
deauth lines). All four end the room; none starts a match; all three consoles stay
within a frame of each other (p1 2298, p2 2299, p3 2299) with `LINK ACTIVE` up to
the deauth (`accept-trace.txt` section 6, `ord-213_end.png`, `ord-312_end.png`).

**The no-press control is what changes the reading.** The same room, the same
table, nothing pressed for 45 seconds, dies anyway: all three consoles land on the
game's own "A communication error has occurred" (`ctl_nopress_table.png`,
`ctl_nopress_error.png`). What ends it is one host command,
`06 01 06 00 03 11 05 00 00 00 00 4E 06 00` (sent in a 12-frame burst); each
guest's game clears its association on it (`guest set its AID to 0 ... it has
nothing to answer with`), arms its deauth slot and writes `reason 0003`, and the
host then broadcasts `0003`. Presses change *when* the host reaches that command
(+34.4s with presses, +41.9s without), not whether.

It is a different body family from the one the two-console match started from in
2026-10-22c (`F6 00 02 00 04 9D ...`); after the guests have left the host does
reach `F6 00 02 00 05 9D 07 00 ...` with its mask down to `0002`. A two-console
re-run of the same driver this pass died the same way (`06 01 02 00 03 11 05 00 00
00 00 4E 02 00` at +29.4s, `ctl_2p_end.png`), so 2026-10-22c's working two-console
match stands as one run, not as a control the driver reproduces.

**Three-player multi-card play needs** the host's room bookkeeping to accept the
third console's `01 8D` answer instead of abandoning the room with that `06 01 ...`
command - the same place the answer flag is written - not another press order,
reply window or transport change.

### 2026-10-22d — the guests' answer flag is their own rule-set state, and the press that completes the set is what still ends the room

Corrects 2026-10-22c. In a three-console room, a guest pressing A on its own
NAME/ITEMS/HANDICAP/TEAM panel (showing "Please wait until all of the players
have set their rules") flips that console's room answer to `01 8D` and keeps it:
the same room answers `01 0D` in 255 of 283 credited windows before any
confirmation and `8D` in every window after it, for both guests
(`3p_rules_guest_set.png`, `3p_rules_p1_panel.png`). So the byte is the game's own
"my rules are set" state, not a room-size condition.

**What still fails is completing the set.** Guests confirming while the host has
not (order p2, p3) ends the room at the last press - the host sends its own deauth
first (reason `0003`) to p3 and p2, then the guests, all three on the game's
communication error (`3p_rules_last_confirm_error_p1.png`); host and one guest
confirmed (order p1, p2) ends it at the guest's press. Two consoles with the same
presses start a match. In every failing run the host's command body never leaves
`F6 00 06 00 00 80`, the mask stays `0006`, every window is `collected=0006 of
mask 0006` with `stale` 0, and the host credits its replies to the last frame, so
the confirmations are not being lost by the transport. The one variable not yet
run cleanly is the order around the host - host first, then guests - which is the
next measurement (`accept-trace.txt` section 5).

### 2026-10-22c — the press after the room, run as a two-against-three differential: it is the title's own start condition, and the transport is clean up to the guests' deauths

**Two consoles and three consoles, same table, same presses, one at a time with a
screenshot after each** (`game-captures/tetris-accept-2026-10-22/accept-trace.txt`
section 4). **Two:** the host's A leaves it on "Please wait until all of the
players have set their rules", the guest's A advances the room - the host's
command body goes from `F6 00 02 00 00 80` to the 18-byte `F6 00 02 00 04 9D ...`
form in the same round it collects the guest's `01 8D 20 20 11 00 ...` answer -
and the STANDARD match starts on both (1,155 windows, `reanchor-dropped` 0,
`empty` 0). **Three:** the host's A does the same thing on the table (p1 to
"Please wait", p2 and p3 on their own editable panels, link clean), and the
guest's A then takes the room down - both guests write their own `deauth ...
reason 0003` within twenty lines of each other and land on the game's own "A
communication error has occurred".

**What differs is not the link.** The host's body never leaves the 6-byte form at
three consoles (170 commands logged, not one 18-byte body); the guests answer
`01 0D 20 20 ...` in **255 of the 283 windows where both are credited** (`01 8D`
only 14), where the two-console guest answers `01 8D` in **1,025 of 1,155**; and
the host's replies are credited to the last frame (507 from p2, 511 from p3,
`stale` 0, every window `collected=0006 of mask 0006` up to the guests' own
deauths). That flag byte is read out of the **guest's own wireless slot body** by
the trace, so it is the title's state and not something the transport supplies:
at three consoles both guests' games answer without the bit the two-console
handshake advances on, and the host's game therefore waits. **No code was
changed for it**, by design - the next question is the guest's own state machine
(what makes its room answer carry `80` at two consoles and not at three), not the
reply path.

### 2026-10-22b — the three-console room was the host's reply window, not the game: all three now open the setup table and reach the LINK mode select, and the confirm press is the next wall

**The two-console accept and the three-console accept, traced side by side**
(`MD_LINK_VERBOSE=1`, same ROM, same route; the counts and the evidence shots
are in `game-captures/tetris-accept-2026-10-22/accept-trace.txt`). Both guests
were in the host's room and answering all along - p2 3,323 answers of 3,459
polls, p3 3,016 of 3,463, 5,948 real replies (`fc=0118`) collected with no
firmware stand-ins (`fc=0158` 0) - and what differed was the host's own CMD ACK:
**18 `clientfail` of 3,154 acks at two consoles (0.6 %), 1,062 of 3,463 at three
(30.7 %)**. The line behind every one of those is `reply window: p1 would have
waited 34ms for a reply, but this frame only has ~27ms of slack left`: the
per-frame reply budget's clock starts at the frame's beginning while the poll it
guards is sent mid-frame, so a two-frame budget leaves about one frame of
waiting, and a peer answers from inside the same emulated round on its own
thread. The game reads that as a lost packet and **retransmits one Download Play
`04` DataPacket (packet 0, `NINTENDO`) until both guests leave with `reason
0003`** - so the download-image packet is the *consequence* of the failed round,
not a separate defect: it enters the path only after the window has already
reported a client that answered as failed.

**The fix** (`LocalMP::RecvReplies` plus the app's defaults): the reply timeout
is four emulated frames (67 ms) and the per-frame budget four frames instead of
two, and both caps now apply **only to a window nobody is answering** - the bus
keeps each AID's last credited answer and a window waits its full ceiling only
while the mask it polls has a client that answered within the last four rounds,
so a quiet client keeps the old cheap behaviour (the 2026-09-25 MKDS 9 fps case
stays protected). With the shipped defaults and no overrides: **three consoles
open the room's setup table on all three (7.8 % `clientfail`) at 59.7-59.9 fps
each, then reach the game's LINK mode select (STANDARD / PUSH / TOUCH / PUZZLE /
CATCH / MISSION) on all three at the same frame rate**, the host's `F6` match
protocol running and every poll collected (`collected=0006 of mask 0006`). The
two-console arm is unregressed: setup table on both and a live STANDARD match
(boards falling, 0.7 % `clientfail`, 59.8 fps each).

**Still open: the confirm press.** From the mode select, confirming a mode takes
the room down - all three consoles land on the game's own `A communication error
has occurred`, both guests after their own deauth `reason 0003`, with the link
clean to the last frame (commands, replies, acks, beacons all counted, `stale 0`,
`reanchor-dropped` 24). **No match at three yet**; what changed is that the
acceptance is no longer the wall, and the press is now reachable with the link
proven healthy.

### 2026-10-22 — two cart consoles play a live STANDARD match; three and four still never leave the room, and the press that ends them is now dated

**A route read off the game's own screens, and a match at two carts.** Both
consoles run the cartridge. Main menu (bottom panel): SINGLE PLAYER / MULTIPLAYER
/ Wi-Fi / OPTIONS / RECORDS at `y=40/70/96/124/152`; **every row takes two
touches** - the first moves the highlight, the second enters. Host: MULTIPLAYER
`(110,68)` twice → INVITE `(110,50)` → STANDARD `(110,56)` → waiting room.
Guest: the same two touches → JOIN `(110,110)` twice → `A` on the room entry.
Then **the host's A must be retried**: pressed 1.5 s after the guest's accept it
is swallowed, and pressed again 2-3 s later it opens the setup table on both
consoles (measured; `tools/scripts/tetris-4p.py` presses until its own panel
changes). Each player's A on the table starts the match.

What plays: both boards falling and stacking with items, **59.8 fps on each
console over a 10 s window with identical frame counters** (lockstep never broke),
host side **3,317 commands / 3,297 collected, `clientfail` 20 (0.6 %), one
deuth**, and p1's bottom panel - the *remote* player's board - changes by mean
35 gray over 2 s while p1 is untouched, which is remote state rendering without
local input (`game-captures/tetris-4p-2026-10-22/2p_match_start.png`,
`2p_remote_field_on_p1.png`, `2p_setup_table.png`). Neither console ever ran a
downloaded image (no `BOOTED` and no `download:` line anywhere in the session),
so this is the card room, not the Download Play route.

**This corrects the two-cart control of 2026-10-16c**, which recorded both
screens blanking at the press with a communication error 20 s later. Same
build family, same room; the difference the runs expose is *when* the host's A
lands. Pressed while the guest's own screen still says `Accepting players now`,
the press does not open the table (at two consoles it is simply swallowed; the
next press works), and at four consoles the same early press takes the room down
the download path instead (below).

**Three and four carts: every console reaches the room and no guest ever
finishes accepting.** The host's own `NAME` column lists Player 1-4 (`4p` run A),
the guests sit on `Accepting players now. Please wait a moment...` indefinitely,
and the host's panel does not open the setup table. Within seconds of the
host's press the host is on *"A communication error has occurred. Press the A
Button to return to the title screen"* and every guest on *"...select the game
you want to join"*, each having written its own `deauth ... reason 0003`
(`game-captures/tetris-4p-2026-10-22/4p_after_press_error.png`). In run A the
press also put a **264-byte command on p2's and p3's slots whose body starts
`06 01 0E 00 80 11 04 00 00 00 00 4E 49 4E 54 45`** - the download image's
first packet ("NINTENDO") - and the host displayed `Transmitting data...`
(`game-captures/tetris-4p-2026-10-22/tetris4-a-2026-10-22.log.gz`, the
`mp p2/p3 last: cmd seq 8050 len 264` lines at +41.88 s). A second four-console
run took the same press to the errors without that packet, so which path the
press takes at four consoles is not yet isolated; what is measured is that the
guests' acceptance never completes at 3+, and that the press is fatal at 3 and 4
while it opens the table at 2.

**Tooling this measurement needed and now has.** A menu can only be mapped by
trying each input from the same screen, so the control channel gained
`snapshot`/`restore` (the bridge's savestate, taken between frames) and
`tools/scripts/menu-map.py` is a sweep built on them; `tools/scripts/tetris-4p.py`
is the route above, reusable at 2-4 consoles. See BUILD.md.

### 2026-10-16c — the two-cart control: two carts fail too, the link is perfect to the last second, and a second title is green at the same count

**The control that had never been run: exactly two cart consoles, the same route,
shipped defaults.** Both screens go blank at the press and 20 s later *both*
consoles - host included - sit on the game's own *"A communication error has
occurred. Press the A Button to return to the title screen."*
(`logs/shots/two1_11_press_2.png`, `logs/shots/two1_13_press_20.png`,
`logs/two1.log.gz`). So the ceiling is the card-mode handshake itself, not
multi-client scaling: a second guest is not what breaks it.

**The link is perfect up to the second both games stop driving their radios.**
The guest received and answered **1,747 of 1,747** offered commands, the host
collected 1,746 and handed 1,746 to its own game, and neither side wrote a deauth
(`deauth out/in 0/0`; last `p2 rx cmd` and `p2 tx reply` in the same second, with
that second's counts 109 commands out / 107 replies back / 109 acks out). The
error screens appear eight seconds later.

**It is not the synthesized stand-in.** At two consoles this title's profile means
that joiner *is* constructed; its own control-channel report reads `console 2,
armed` with **every counter zero** (`beacons 0 · auth sent 0 / recv 0 · assoc sent
0 / recv 0 · replies sent 0 · our reply collected 0 times`), because console 2's
own game held the radio. The stand-in stood down as designed.

**The host's own engine still serves its child image, with one client.** In the
press's own second the host composes 5 x 264-byte `04` DataPackets and 5 x `05`
Finals, after a phase walk `01@0350` (NameRequest) -> `03/242` (RSA) -> `04@8050`
-> `05@8350`; the wire that follows the Final carries 400 more 18-byte class-`03`
frames and 6-byte bodies while the room's own 14-byte `00` polls
(`06 01 02 00 03 11 00 50 08 00 02 00 02 00`, 977 of them) stop being composed. A
cart console cannot consume a download, and both games then conclude communication
error.

**The attribution control, same count, another multi-card title.** NSMB's route
(`tools/scripts/nsmb-route.sh`, two players, 14,000 frames, exit 0, 65.5 s wall =
3.57x realtime) reaches `slots on the bus: 0x003` with the guest receiving and
answering **11,126 of 11,126** commands (`blank 0, canned 0`), the host
`cmds tx=11126 acks=11126 (clientfail 0)` and `replies sent 11126 collected 11126
(empty 0, stale 0)`, no deauth either way, last command at **+234.00 s**
(`logs/nsmb_2cart.log.gz`). So a two-console *card* session is not a fork limit:
the ceiling at two carts belongs to this title's own lobby.

**Superseded here:** the 2026-10-16b reading that what ends the party is each
guest's own clock. At two carts both consoles end *together* and the host has no
separate clock; the guest-clock reading holds only for the arms whose guests had
associated at different times.

### 2026-10-16b — three arms on the start press: what ends the party is each guest's own clock, and the download packet is the host game's own

**The question this pass was given, and the answer the arms force.** Is this
fork's Download Play machinery what puts a `04` DataPacket into a cart room's
start, or did the host game choose to serve a download? Three arms, three cart
consoles each, one route (`logs/abA.log.gz`, `logs/abB.log.gz`, `logs/abC.log.gz`):

| arm | configuration | 264-byte `04` DataPackets | 242-byte `03` RSA frames | `05` Final | outcome |
| --- | --- | --- | --- | --- | --- |
| A | shipped defaults | 4 (both guests, `06 01`) | 8 | 4 | press advanced the room: host and the surviving guest on the room's player panel 20 s later (`logs/shots/abA_13_press_20.png`), the first guest already on its own error screen |
| B | `MD_STOCK_MP=1` | **0** of 13,391 command frames | 7 | 0 | no download served at all; the host's room never advanced past *"Waiting for other players to join..."* (`logs/shots/abB_13_press_20.png`); both guests reached their own error screens |
| C | shipped defaults, press 1 s after the last join | 6 (both guests) | 10 | 6 | the host's own panel listed `melonDS-2` and `melonDS-3` 2 s after the press (`logs/shots/abC_11_press_2.png`); then p2's radio dropped at +12 s, p3's at +20 s, and all three consoles sat on the game's own error screen (`logs/shots/abC_13_press_20.png`) |

**The fork is not the author, measured three ways.** (1) The only Download Play
code in this fork is the *client* joiner, and both arms' own status line proves it
is not constructed here: *"this session has no download-play joiner (a profile
with download_play, and a two-console session, are both required)"* (`appctl.py
status`, three-console sessions), so the arm the mission asks for - the fork's DP
path disabled - is the state both arms ran in. (2) The host's command frame is the
game's own: melonDS copies `12+len` bytes out of the game's own wifi-RAM slot
(`src/Wifi.cpp`, `TXSendFrame`'s CMD arm) and ANDs the game's own mask halfword
with the fork's failure bitmap; nothing in the fork writes a command body. (3) The
payload is the cartridge's own child image: `4E 49 4E 54 45 4E 44 4F 20 20 20 20 4E
54 52 4A 30 31` = `NINTENDO    NTRJ01`, and `NTRJ01` occurs **4x in `Test Roms/Tetris
DS (USA).nds` and 0x anywhere in this repository** (`grep -rl NTRJ src/
src-tauri/src tools/scripts frontend/`); in arm B the same image sits in the host's
own main RAM at `0x02208A20` (4 MB ARM9 scan in 4 KB chunks). So the host game
itself chose to serve a download: this room is the game's dual-mode lobby, exactly
as its own page promises (*"...Players without a Tetris DS Game Card can get in on
the action by using DS Download Play."*).

**The start press is not what sends it.** Arm C puts the press 1 s after the last
join, inside the guests' own windows, and the host's own screen 2 s later is its
room's player panel listing both guests - the press advances the room. The
DataPackets are the host engine's dialogue with the consoles that associate: in
arm A the first goes out **~24 s into the host's transmission, about 2 s after the
first joiner associated**, at a moment when the host's own screen still reads
*"Waiting for other players to join... Press the A Button when enough players have
joined."* (`logs/shots/abA_10_pre_press.png`), addressed to that one guest
(`mask=0002`) and ~24 s *before* the press.

**What ends the party is each guest's own clock, and it does not need a download.**
Arm B serves **no** `04` packet at all and both guests still reach their own error
screens; arm A's first guest is already on *"A communication error has
occurred..."* at the pre-press shot (27 s after its own association) while the
second guest, which associated ~22 s later, survives the press and reaches the
panel; arm C: the host lists both guests, then p2's radio goes down 12 s after the
press and p3's 20 s after it (13 s and 21 s after their own joins), all three
consoles on the game's own error. This supersedes the previous retreat's causal
reading ("the start press hands the room a download packet" and the press's packet
as what ends the three-console party): the press's packet and the guests' leaving
were two clocks that coincided, and no configuration measured here starts a match.

**The fork's transport *is* upstream of the engine's walk, and the room needs it.**
At `MD_STOCK_MP=1` the guests' replies are dropped (the log's own `kept 0`), the
host's engine never reaches the download direction - 0 of 13,391 command frames are
`04` - and the room does not advance at the press either. So the reply-delivery
work is what lets a guest's own answers reach the host at all, and "confine
Download Play by answering nothing" would take the card room down with it - which
is why nothing was changed on this pass's evidence.

**The guests' replies are their games' own, and they are what the engine reads.**
`logs/mc3.log.gz`'s 17,705 answered polls: 14,669 came out of the live reply slot,
3,030 out of the arm the guest's own game made, and the firmware's own
`00 00 00 00` fallback was used **6 times in the whole run**. What the host was
handed carries reply type `08` with the console's own DS nickname in UTF-16
(`04 81 08 03 53 00 2D 00 32 00` = `S-2` from `melonDS-2`), the same reply class
that carries a real Download Play client through this engine (`dp_joiner.rs`,
v0.1.9cv-cw).

**Next:** the guests' own give-up, which is now the only thing between three carts
and a started match - what a guest is still waiting for after it associates (the
host's own panel already lists it, the guest's screen stays on *"Accepting players
now. Please wait a moment."*) during the 13-47 s before its own clock expires.

### 2026-10-16 — the room, read off the games' own screens: the card room, which also serves Download Play guests

**The route's mode, settled from the games rather than from the wire, and it
corrects the reading this section used to carry.** The page after MULTIPLAYER
carries the game's own rules for that room, and they name both kinds of guest:
*"To host a game, tap Invite. To join a game already being hosted, tap Join.
Players without a Tetris DS Game Card can get in on the action by using DS
Download Play."* (read off `logs/shots/p4_02_multipage.png` on the host's console
and on a guest's alike). INVITE opens the host's mode list (`STANDARD`, `MISSION`,
…, `logs/shots/p4_03_invite.png`, with the STANDARD description on the top screen)
and no Download Play branding anywhere on the host's screens; the room behind it
announces itself as *"Waiting for other players to join... Press the A Button
when enough players have joined."* (`logs/shots/p4_04_room.png`). A **cart** console that taps
JOIN gets its own browser — *"All of the games you can join will be displayed on
the Touch Screen. Press the A Button to enter a game…"* — and this very room is
in it (the host's `melonDS`, `STANDARD`, and a player count that rises with each
cart guest: `logs/shots/p4_05_p2_a1.png` reads `1/…`,
`logs/shots/p4_05_p3_a1.png` reads `2/…`); it enters the room and waits there —
*"Accepting players now. Please wait a moment."* on every guest's bottom screen
(`logs/shots/p4_05_p2_a2.png`, `logs/shots/p4_05_p3_a2.png`,
`logs/shots/p4_05_p4_a2.png`), with the host's own room screen listing the consoles that
joined under a `NAME` column. So the room is the game's **card** room, and the
Download Play admission the previous reading took as proof that the room is
single-card is the second half of what that room's own page promises. **Superseded
and removed here:** "the room is single-card, not a multi-card one", "a party of
more than two for this title is single-card", and the earlier reading that the
room's *type* is why a full-cart party drops.

**The link is not what fails, per console (phantom-joiner arm).** In the arm with the most guests
(`logs/ni_tetris.log.gz`) each guest's radio received every command and every ack of
its session; the guests held **distinct AIDs** (p3 aid 1, p4 aid 2, and the host
polled both with `mask 0006` for **4,917 rounds**); and the host's `clientfail`
mask - the DS ack's own "this client failed" field, decoded out of every ack each
guest received - was `0000` in the **last five acks before each guest left**
(p2: 9,420 acks, 9,294 × `0000`, 126 × `0002`; p3: 13,483, 13,115 × `0000`; p4:
10,499, 10,145 × `0000`). What leaves is the guest's own game, on its own clock -
in that arm p2 13 s, p3 40 s, p4 25 s after *its own* association, 32-47 s in the
gated re-check below - and each guest's own
`deauth sent … reason 0003` is the last MP frame of its session. The host's room
beacon never stops arriving either: in the four-console arms the host's 192-byte
`fc=0080` beacon is delivered to every guest right up to its last second
(`logs/r2b_tetris.log.gz`: 143 to each guest, the last of them after the start
press; `logs/ni_tetris.log.gz`: 915 / 203 / 131 to p2 / p3 / p4), so no console
leaves because the room stopped announcing itself to it.

**Re-checked with the phantom client gated out (2026-10-16), and the eviction is
gone.** With the joiner gated out of 3-4 console sessions and the same route
(`logs/p4_tetris.log.gz`, four consoles, no start press) each guest answers for
**32-47 s after its own association** and then leaves on its own clock (p2 47 s,
p3 32 s, p4 42 s; last commands the 14-byte library Dummies, `deauth sent …
reason 0003` each), and **a later guest no longer evicts an earlier one**: in the
three-console arm the first guest kept answering for the whole 15 s between the
second guest's association and the host's start press, and both left within 2 s
of that press (`logs/mc3.log.gz`). So the eviction this file used to record was
the phantom client's doing, and what remains is each guest's own give-up plus the
start press.

**Why they leave: the host's start press hands the room a download packet.** Two
guests sat in that room at once (`mask 0x7`, roles 1/2/2) and the host's A - the
press the two-console single-card route uses to begin its session - was fired
**4 s after the second guest associated** (`logs/r2b_tetris.log.gz`: joiner disarmed
through the Settings switch, nothing else changed). The host's game then did what
it does in the single-card route: a 264-byte class-`04` data frame to the room
(`06 01 06 00 80 11 04 00 00 00 00 4E 49 4E 54 45 …`, "NINTE…"), the 192-byte
room beacon carrying `TETRIS DS S…`, and within **one second** the newest guest's
own game wrote `deauth sent … reason 0003`, the host deauthed the other guest
(`reason 0003`) and then the broadcast, and turned its own radio off
(`wireless on/off 2/2`).

**The same press with cart guests only, at three and at four consoles
(2026-10-16).** Three cart consoles in the room, the host's A 13 s after the
second guest joined (`logs/mc3.log.gz`): the host sends one 264-byte command to
**slaves 2 and 3** whose body is byte-for-byte a Download Play `04` DataPacket -
reply time `06 01`, slave flags `0006` (both guests), stated size `80` halfwords,
command byte `04` at body 6, packet number `0` at body 9, then 250 bytes of image
data at body 11 starting `4E 49 4E 54 45 4E 44 4F` ("NINTENDO"). That packet is
the last command each guest's own game receives (both answer it once; `cmds rx`
then freezes at 13,957 and 3,862), both guests write their own `deauth sent …
reason 0003` within 2 s, the host deauths both (`deauth out 2`) and all three
consoles sit on the game's own error screens - host: *"A communication error has
occurred. Press the A Button to return to the title screen."*, guests: *"…Press
the A Button and select the game you want to join."* (`logs/shots/mc3_watch_60.png`).
**Read from the 2026-10-16b arms, the deauths and the press's packet are two
clocks that coincided here, not cause and effect: the guests leave on their own
clock with no packet served at all (`logs/abB.log.gz`), and the press itself advances
the room (`logs/abC.log.gz`).**
The host's reply path was never starved: 13,961 commands sent, 13,961 acks,
`clientfail 340` (2.4 %), 17,521 replies collected. At four consoles
(`logs/mc4.log.gz`, all three guests joined, each guest given one confirm `A`
before the host's) the press sends the packet's sibling instead - a `05` (Final)
command addressed to slave 2, then the host's own application-level commands
every ~5 s for forty seconds - while the two middle guests park on their browsers
(`pc9 02008064`) and the host and the last guest land on the same error screens
(`logs/shots/mc4_end.png`); host side 25,863 commands, 25,863 acks, `clientfail
669`. **No match has started at 2, 3 or 4 cart consoles on this route**, and what
the host's own dual-mode lobby engine sends alongside the room is a *download* in
every configuration measured before 2026-10-16b (which shows the engine, not the
press, choosing it; the press itself advances the room):
the synthesized Download Play client consumes that same packet and boots
(`logs/tetris6_app.log`: "the download's first packet: #0 with 250 bytes, starting
[4E, 49, 4E, 54, 45, 4E, 44, 4F]"), and a full-cart console's own game leaves
instead.

**The fork-side finding, and the fix it produced.** With the joiner armed (the
shipped default) that phantom client is not passive: it takes an AID in the room
(`C001`) and pulls the host's own game into the download engine **while two real
consoles sit in that room as AIDs 2 and 3** - the session was hosting a transfer
instead of hosting the room. The app now arms the joiner **only in a two-console
session** (`src-tauri/src/lib.rs`): that is the configuration this title's
single-card route is measured in, and in a 3-4 console session the last console
is a player, not a spare radio. Verified on the rebuilt app: a four-console
session creates no joiner at all (`logs/r3_tetris4.log.gz`, and re-read on every arm
of the 2026-10-16 pass: three and four consoles both answer `appctl.py dpjoiner`
with *this session has no download-play joiner*, `logs/mc3.log.gz` /
`logs/mc4.log.gz`), and the two-console route is unregressed: a two-console
session arms on console 2 (`download-play joiner: console 2, armed`) and its
A-driven transfer now completes end to end on that binary -
**`1004732 of 1004732 bytes in 4021 distinct packets · BOOTED`**
(`logs/dpa2.log.gz`), the same 4,021-packet transfer the route's own record has.
Two arms of that re-run are worth keeping: the joiner's client is registered
`AID 0xc001` and its beacons counter reaches 256, and the transfer is
byte-identical to the pre-gate one, so the gate cost the working route nothing.

**The route's one precondition, found on the way: leave the client console
alone.** A two-console run that drives *both* consoles onto the multiplayer page
- the natural thing for a driver to do, since every other route in this project
starts that way - gets **`beacons 0`** and no transfer at all, for 150 s
(`logs/dpa.log.gz`): the joiner attaches (`joiner: console 2, armed`, *listening
for a download-play beacon* logged), the host's beacons are demonstrably handed
to that console's slot (`delivered 1001` in its own health line), and the
stand-in never sees one, because that console's own game is in its multiplayer
page with its own MP state on the wireless block. Left at boot, the same route
boots the image on the first try. The Download Play section carries the same note
for the other titles.

**What the fork is *not* doing wrong, from the source.** Diffed against
`upstream/master`, every behavioural addition in the MP path is per-console state
(`MPMYAID`, `MPArmHeld`, `MPPendingPoll`) or diagnostics: `SendMPReply`,
`SendMPAck`, `ReportMPReplyErrors` and `NumClients` are stock, `StartTX_Cmd`
still derives the host's mask as `game bits & MPClientFail`, and the bus files
replies by sender id **and** per AID (`packets[(aid-1)*1024]`). No single-peer
slot was found, and the ack/failure accounting the mission named is per-AID, not
per-session.

Evidence: `logs/mc3.log.gz` (three cart consoles, host's A 13 s after the last
join: the `04` DataPacket to slaves 2 and 3, both guests' last command, both
deauths), `logs/mc4.log.gz` (four cart consoles, each guest given a confirm `A`:
the `05` Final to slave 2 and forty seconds of the host's own commands),
`logs/p4_tetris.log.gz` (four consoles, phantom client gated out, no start press:
the 32-47 s own-clock give-ups), `logs/dpa2.log.gz` (the two-console Download Play
route on the same binary: `1004732/1004732 in 4021 · BOOTED`), `logs/dpa.log.gz`
(the client console driven into multiplayer: `beacons 0` for 150 s against
`delivered 1001`) - the per-guest `cmds rx` / `answered` counters quoted above come
from `logs/p4_tetris.log.gz` and `logs/mc3.log.gz` too, whose uncompressed
originals the pass's housekeeping removed - plus the other pass's arms whose
numbers still stand:
`logs/r2_tetris.log.gz` (four consoles, joiner armed), `logs/r2b_tetris.log.gz` (the
same route with the joiner disarmed through the Settings switch),
`logs/ni_tetris.log.gz` (distinct console identities), `logs/r3_tetris4.log.gz` /
`logs/r3_tetris2.log.gz` (no joiner at four, armed at two),
`logs/m4_tetris2.log.gz` / `logs/m4_tetris3.log.gz` (the reply-knob arm, whose
result stands: the knobs move `clientfail` 4065 → 731 and nothing else). Shots:
`logs/shots/p4_*.png` (the page and room texts, the browser entry, the waiting
screens), `logs/shots/mc3_watch_60.png`, `logs/shots/mc4_end.png` (the error
screens), `logs/shots/t4p_*.png`, `logs/shots/r2_*.png`, `logs/shots/r2b_*.png`,
`logs/shots/ni_*.png`. Superseded and removed by the 2026-10-16 pass: "the room is
single-card, not a multi-card one", "a party of more than two for this title is
single-card", "the room holds one guest", "the room admits three guests", and the
earlier reading that the room's *type* is why a full-cart party drops; removed by
the 2026-10-15 pass: "the multi-card 4-console route is green".


## Mario Party DS (USA) (Rev 2) — 🟡 Download Play transfer completes, guest stalls post-boot, 2 players

DP-only by design (2-4 players, no multi-card mode), which makes it the cheapest test of the joiner on a second engine. A caveat for anyone driving it: **one tap only highlights a menu row** (it shows an explanation), so every step is tap-tap or tap + A, and the D-pad does nothing on the touch screens.

Route (P1 hosts, console 2 is the synthesized client): title A → file select (4 rows) → 3rd row `(128,108)` tap + A + A → the host's Download Play lobby, radio up (`begin #2`, frame 51296). The joiner hears the beacon (serial `00000390`, channel 7), authenticates (`0000`), associates (`AID C001`), and the download streams **without a host start press**: `623160 of 623160 bytes in 2494 distinct packets · BOOTED` (`logs/mpds2_app.log`).

Post-boot it stalls the same way Bomberman does: the booted guest never comes back on the radio (its own `begin #2` at frame 53289, then `end #2` 5 s later) and parks on a static screen; the host's game stays alive (`cmds tx 10432`, IPC flowing) but ignores every button and touch, its screen frozen from the same frame, `clientfail 6968`, `deauth out 1`. Stall addresses, triple-sampled static: host ARM9 `020652D8` (ARM7 BIOS-idle `0000115C`), guest ARM9 `0206AE78` (lr = pc, a tight loop). A blind tap+A on the guest's static screen was its error screen's power-off: `Stopping emulated console (Reason: PowerOff)` dissolved the lockstep at frames 68265/68264, and the resulting `did not answer a joiner turn` warnings (1505) are that consequence, not a joiner defect (Tetris's working session logs zero). No minigame was reached.

First boot created the game's save (`Test Roms/Mario Party DS (USA) (Rev 2).sav`, game-written, supervisor-pinned); quit exit 0.

---

## Bomberman (USA) — 🟡 Download Play transfer completes, guest errors post-boot, 2 players

Route (P1 hosts, console 2 synthesized client): START past the attract battle → title `TOUCH TO START` `(128,150)` → Battle `(190,98)` → the Download host lobby (`Starting a Battle Game! Waiting for players to enter!`, then `Please start the download once all players are together!`). The joiner hears the beacon (serial `00400022`, empty SSID, ch 7), auth/assoc `0000`/`C001`, roster lists `melonDS` twice; host Start streams `672764 of 672764 bytes in 2693 distinct packets · BOOTED` (`logs/bomb2_app.log`, `logs/bomb3_app.log`, shots `logs/shots/bomb{battle,2_lobby,booted,3_boot}.png`).

Post-boot, both consoles show the game's own `Communication error. Press A Button to turn the power OFF and try again.` Root-caused 2026-10-14 with a fix that fires where there is material: the stand-in drained the same queue the game's CheckRX reads, starving scan-first guests — fixed by handing the join/scan set back post-boot (`LocalMP::RequeuePacket`, `dpjoiner` `requeued` line; to-do.md). Live on the fixed binary (`logs/bomb6_app.log`, 3rd BOOTED transfer, answers flowing): `requeued 0` because the host stops beaconing post-boot (battle-sync commands only), so the guest must direct-assoc off the staged parameters like Tetris's does — and doesn't. Remaining gap is per-image bring-up (staged RAM context is carried; wifi regs come up Reset-cleared), not wire behavior. Clean quits (exit 0), no `.sav` written.

Update 2026-10-15 — serve-scans + poll-forcing live, narrower stall (`logs/bomb_rejoin.log`, rebuilt binary with the general mechanism): same route, `672764/672764 in 2693 · BOOTED` (4th transfer, counts identical), then the new lines fire — `holding this console on the bus` (poll-forcing while the booted game is off-radio) and `leaving the join/scan frames for the booted game`, `served 158 while the host was silent`, `join/scan frames left 4428`. First for Bomberman: the booted guest powers its radio post-boot (`begin #2` then `begin #3`, slots `0x3`) and its scan READS a served host beacon (`wifiio`: `fc=0080` `melonAP` frame handed, 43 halfwords consumed). But it never transmits (`cmds tx 0`), sends one `deauth out`, parks (ARM9 `020A1BC8`, ARM7 BIOS-idle, wifi clock frozen, no new reads in 25 s) on a dialog screen while the host sits frozen at 35450 commands (last `0x00`, last real `0x06` battle-sync). Zero `did not answer a joiner turn` warnings (joiner quiet-down holds). Verdict: the guest needs LIVE beacons to proceed past scan and the host game has stopped beaconing — next mechanism is stand-in re-beaconing as the host post-boot (plus answering the guest's auth), not more staging. Clean quit exit 0, no `.sav`.

---

## LEGO Star Wars II - The Original Trilogy (USA) (Rev 1) — ⬜ no multiplayer mode

Run on two consoles for 18,000 frames each with the menus walked end to end
(`logs/rom_lego_harness.log`, `game-captures/lego-set2/sheet.html`): the only
entries anywhere are **New Game**, **Load Game** and **Settings**. There is no
link entry, no wireless screen and no local option to enter. It cannot be
counted against the emulator — the retail game has no multiplayer mode.

---

## `tools/mkdiagrom/diag.nds` — ✅ the link self-test (not a game)

The ROM the build is verified against, not a title. `md_smoke_test` runs **79
checks, 0 failures** against it (per-console framebuffer checksums, per-console
MACs, the touch probe's ADC = pixel × 16), and the same ROM carries the frame
sampler used to measure input coupling (96 samples/s, 171 frame-pairs in 3 s).
If this ROM ever fails, the link itself is broken and every row above is stale.

---

## Download Play (single-card): working for three titles, expanding

Several DS titles offer a mode twice — once multi-card (both consoles own the
ROM, which is what works here) and once over **DS Download Play** (host owns the
ROM and the joiner downloads it over the air). New Super Mario Bros. is entered
through its **Multi-card** entry for exactly this reason. Two titles in the
folder, however, have *no* multi-card alternative at all:

- Super Mario 64 DS (VS mode) — a 4-player mode
- Star Fox Command (its only local mode)

**Update 2026-10-14: this milestone has landed for three titles.** The synthesized joiner (`src-tauri/src/dp_joiner.rs`, armed by the registry's `download_play` flag) completes Star Fox Command into a 2-player dogfight, Super Mario 64 DS's transfer 3/3 (no battle yet), and Tetris DS into a live versus match (see its section).

**One precondition of every synthesized join, pinned down on Tetris DS's re-run
(2026-10-16): the client console's own game must be left alone.** With the joiner
armed on a two-console session and that console driven into the game's own
multiplayer page - what every other route in this project does to every console -
the stand-in attaches and logs *listening for a download-play beacon* but hears
nothing: `beacons 0` for 150 s while the host's beacons are demonstrably handed to
that console's slot (`delivered 1001` in that console's own health line,
`logs/dpa.log.gz`). Left at boot, the same route completes the transfer
(`1004732 of 1004732 bytes in 4021 distinct packets · BOOTED`, `logs/dpa2.log.gz`).
A console that must both play and serve as a Download Play client is therefore a
separate problem: its own MP state is what stands in the way. Download Play
is how one cartridge serves four consoles, which makes it simultaneously the
widest-compatibility feature still missing and the cheapest 4-player
configuration to emulate (three of four consoles run a small downloaded image
instead of a full cartridge). The plan, its two routes and the evidence that would
close it are Milestone 4 of
[to-do.md](to-do.md#the-road-to-a-working-2-3-and-4-player-link).

What is **not** missing is the transport: Star Fox Command's host lobby already
carried a complete bidirectional exchange over the existing link. What is missing
is the joining console's client — on real hardware that is the **firmware menu's**
Download Play client, and the emulator's built-in FreeBIOS cannot boot the
firmware menu (`NeedsDirectBoot`). The faithful route is a user-supplied BIOS +
firmware image (the bridge already has `md_set_bios` and `md_set_firmware`); the
route the project should prefer is a built-in client that needs no user files and
spends emulation accuracy instead, which is the standing trade recorded in
[to-do.md](to-do.md#the-road-to-a-working-2-3-and-4-player-link).

---

## Three and four players: four consoles link, and the app is the limit

The bus itself scales — a 12/12 mesh self-test passes, and four linked 3D
consoles run at **~2.2× realtime** on the dev laptop (1 console ~6×, 2 ~2.4–4.9×,
3 ~2.9–3.4×, 4 ~2.2×), so frame budget is not the limit.

**MKDS at three consoles works, and did not need the link core changed**
(2026-09-29, history.md v0.1.9al; `tools/mkds_join.py --players 3`). All three
couple into one group: `link mask 0x7`, `p1=host` with `p2=guest` and
`p3=guest`, every console's own digest read at the same emulated frames, and the
host's `collected` matching what its two clients say they wrote. It stays
coupled across the host's own CUT OFF (frames 4793 → 5241), and it reproduces
(`logs/mkds3_v3.log`, `logs/mkds3_ab_stock.log`).

**The blocker was the route, not the link.** An MKDS client can leave the lobby
on its own — measured anywhere from ~2,740 emulated frames after joining to more
than 10,500, so the game enforces no bound (the 13–21 s figure this file carried
came from the app running at 44–72% of DS speed and was never a game timer) — so
every joiner has to be walked to its group list *before* the first one joins; the previous route walked
the third console afterwards, so the two clients were never in the room at once
and the mask went `0x3` → `0x1` → `0x5`, never `0x7`. v0.1.4's two-joiner failure
above was that ordering, not a transport defect: with the order fixed, three
consoles couple **on the unmodified link core**.

**The per-instance routing change is not the fix, and was reverted.** The
machine-global record this file used to name as the leading suspect is not what
broke a two-joiner session: `MPStatus.MPReplyBitmask` is written but read nowhere
in the tree, `MPHostinst` is only ever a *fallback*, and with a single command
sender (`cmds tx 0` on both guests) a machine-global `LastHostID` and a
per-console one resolve to the same console every time. An A/B on the same route
— stock `LocalMP.cpp` against the per-instance version — reached `0x7` both
ways, with the same window shapes and the same `clientfail` progression
(239 → 342 → 367 stock against 239 → 324 → 349 modified). Per-console routing
becomes necessary only when two consoles on one machine each *send* commands:
two groups, not two joiners. What is left of that item is in
[to-do.md](to-do.md#milestone-2--3-players).

**The reply path degrades with every client added, and the curve is measured.**
The host's own digests, same route and build, end of run: **1 client (2
consoles)** `collected 9257 empty 120 reanchor-dropped 119 clientfail 120` —
1.3% of collected replies reported as a failed client; **2 clients (3
consoles)** `collected 8635 empty 146 reanchor-dropped 208 blank 186 clientfail
367` — 4.3%; **3 clients (4 consoles)** `collected 3208 empty 107
reanchor-dropped 232 blank 228 clientfail 317` — **9.9%**. Blanks plus re-anchor
drops account for the failed-client acks almost exactly, and the re-anchor drops
are the bigger half at four, so the host's game is told a client failed on
roughly one ack in ten while four consoles are linked and playing. It is **not**
the per-frame reply budget (most clamps are 34ms → 33ms; only 97 of 9,037 landed
on zero slack at three consoles), which leaves the backlog of unread answers:
`MD_REPLY_KEEP` is `1 × connected peers` by default, and running the same
four-console route at `3` (nine entries at three peers) cut the host's
`reanchor-dropped` from 232 to 5, its empty windows from 107 to 45 and its
failed-client rate from 9.9% to 4.3%. **Those failures do not cost players,
though.** Halving them did not keep the group together any better — the arm with
*half* the client failures lost *two* clients at CUT OFF (mask `0x9`) against one
(mask `0xB`) at the default — so the departures at CUT OFF/VS are the game's own
transition, in every arm at every count, and the reply backlog is a link-health
improvement to be judged on its own numbers rather than a fix for dropped
players.

**Four consoles in the app cost 26.6 fps, and the app — not the emulator and not
the link — is what costs them.** Measured on the dev laptop, same ROM (MKDS),
same scene, all consoles lockstepped: the app delivers **43.1 fps at two, 33.2 at
three, 26.6 at four** (72%, 55%, 44% of the DS's own 59.826 Hz). The same game in
the headless harness, which renders nothing, does **112.5 emulated frames/s per
console at two and 76.7 at four** — so four consoles that the emulator can carry
at 1.28× realtime reach the screen at 44% of DS speed, i.e. the app's per-frame
screen path spends about two thirds of the frame. That is the mission's
hardware-limit clause answered with numbers, and it is a fixable overhead rather
than a wall: the cheap wins (draw nothing for a console nobody is looking at,
30 fps off-focus with input still at full rate, no upscaling/AA above two
consoles) are all in [to-do.md](to-do.md#milestone-3--4-players), and they scale
with the console count exactly as the hardware rule requires.

Separately, the 4-player *game set* is wider than the multi-card titles above: many
of the DS's party games are single-card, which is why Download Play is Milestone 4
of the roadmap rather than a footnote.

---

## How these were measured, and the limits of the method

Two independent surfaces, so a result never depends on one of them:

1. **The harness** — `md_game_session` (`tools/coretest/game_session.cpp`) runs
   the ROM headless on N consoles with scripted `--touch` / `--autotap` input
   and prints a per-console digest (commands, answers, acks, deauths, slots on
   the bus, frames). This is where role/AID behaviour and byte counts come from.
2. **The app** — `tools/run_app.sh` launches the real Tauri build with
   autostarted consoles, driven through `tools/appctl.py` /
   `tools/nsmb_join.py` / `tools/mkds_join.py` over the control channel
   (`MD_CONTROL_PORT`, loopback only). Every tap is preceded by a screen read,
   and the app's counters are printed in the same units as the harness's, so the
   two can be compared at the same frame.

**Limits, stated plainly:**

- **"The peer's world changed" is where a row says so, and pixels where it does
  not.** The app now has a memory read *and* write (`tools/appctl.py mem` / `poke`,
  `--cpu 7|9`, v0.1.9an and v0.1.9aw), and the MKDS rows above quote the game's own
  RAM words where that was the measurement; the rows still graded from pictures
  say so. No row is graded on RAM it does not name.
- **A link that is alive is graded as ⛔/🟡 if the game will not start a match.**
  Do not read `LINK ACTIVE` as playable.
- **Saves.** All five `Test Roms/*.sav` files are checked byte-identical to
  their recorded md5s after every session (`tools/run_app.sh` pins them and
  announces any write). Recorded md5s: NSMB `bdbb2dfe557b30b4fda1a7dc7d28dce0`,
  MKDS `f482cb428f36169b65c6dd8eea642f32` (its own game writes this card at
  session exit, so the pin advances when that happens),
  LEGO `5af705e8e8abee430e6be46c2e626070`,
  MPH `ffa024d59be23b0ceb6232cda31bdbac`, SM64DS `ad192e5d8adce899bd95ff5c19fa792e`.
- **Not every route is reproducible on demand.** Several measurements need the
  game in a specific phase (for example, the input-coupling figures need NSMB's
  *race*; ten minutes later in the same session the same press moves the guest's
  own screen 1–12 px and the test correctly reports `inconclusive` rather than
  claiming the peer from noise).
- **ROMs are not in the repo** (legal). Reproducing any row requires the same
  game and, where noted, the recorded save.

---

## Adding a title

1. Boot it on two consoles in the harness and walk its menus to find whether a
   link entry exists at all (LEGO's result here was found that way).
2. Script the route with `--touch` / `--autotap` (or, through the app, with a
   screen read before every tap) and record the digest.
3. Say which of the four states it reached: **no MP mode**, **transport only**,
   **lobby/group**, or **in-game match** — and cite the log or capture for it.
4. Note whether the joining console is a *multi-card* peer or a Download Play
   client. Only the former can link today; the latter is Milestone 4 in
   [to-do.md](to-do.md#the-road-to-a-working-2-3-and-4-player-link), and when it
   lands every ⛔ row above becomes worth re-running.
5. Add the row to the summary table above and a section with its evidence, then
   add the title to [to-do.md](to-do.md)'s coverage notes.

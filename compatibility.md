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

- Last updated: 2026-10-14 (v0.1.9dq + Tetris DS single-card pass)
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
| 0168 - Mario Kart DS (U)(SCZ) | 2, 3, then 4 | 🟡 partial | At two, three and four, every console couples into one group and stays coupled; CUT OFF moves **both** consoles to the game's own pre-race wait. **At three the group only ever couples in the record too - it then reaches the same park as two consoles, with no character select and no race (v0.1.9bw, measured).** **6 of 9 runs of the documented route survive that wait** (0.3–2.5 s of it in the rest), and nothing in the link decides it: the two reply knobs do not separate the arms (p ≈ 0.6), `clientfail` is neither sufficient nor necessary (falsified), and the one clean correlate — the host game's own state walk, which starts 115–147 emulated frames after the tap in 7 of 9 runs — is **not** required either, because 2 of the 6 survivors stall at `02 00` without ever reaching the cut-off state `FF 00` (v0.1.9ar). What every death shares is that the guest's game leaves first and the host's 11–12 frames later, and 2 of the 3 are runs where the host's game never left its lobby state at all — reason unproven. The longest survivor held the wait with `link mask 0x3` and both games parked in the same ARM9 poll loop (`pc9 0200FD10`) with both displays black — and that poll is now identified and was moved by hand: it is the game's own **blocking dequeue**, spinning on `*(u32*)0x2173300 == [0x21732AC]` (equal on both consoles, reproducibly), so the wall is a message its radio never posts, not a hung CPU (v0.1.9as). **After the wait, both consoles' own output goes black** ~1.3 s in and stays black (`45d15630`, and `logs/shots/mkds-after-vs.png` is one flat colour across all four screens) while the link holds `mask 0x3` for 12,725 frames past the tap — and neither the route's VS tap nor A on either console moves a pixel of it (v0.1.9ar, `logs/vs1_stdout.txt`). No race has been started at any count, and at four the app runs them at 26.6 fps (44% of DS speed). The ARM7's own message path to the ARM9 is now located and its gate disproved: the poster at `0x060092D8` branches on `[*(0x0380FFF4)+0x3F4]`, that halfword is zero in every sample of a whole session (293/293 per console, frames 1446-18719) and **no code in the 128 KB ARM7 image writes it** - poking it is followed within 2-5 frames by both ARM9s leaving the dequeue and by the game's own deauth, so the race-start message is one of this poster's ten siblings, not this one (v0.1.9aw). **v0.1.9bu drove both consoles' real input surface at that park - 10 presses and 15 touches per console, both sides, screenshot after every step, the frozen guest as the oracle - and neither game answers** (the guest's screens changed once, from `1564d6e5` to `790742fa`, on the first press and never again; the host's own command body stayed `00 80 FF 00 B8 B6` through a VS tap, a START press, an A press on the guest and a further touch), **and the button the host is blocked on is named: VS on the game-select screen**, which is never drawn - the cut-off walk is a fixed ten-step sequence (`06 00 -> 00 FF -> 00 FE -> 00 00 -> 00 01 -> 01 00 -> 01 01 -> 02 00 -> 05 00 -> 03 00 -> FF 00`) that settles 2-5 s after the tap, and 1.3 s into it **all four screens are 100 % black** for the rest of the session (`powcnt1 820F`, LCDs on). The harness defect that hid it is fixed: `--vs` waited on the host's **top** screen, whose first change after the tap *is* that black, so it tapped mid-walk and MKDS does not queue a touch; it now waits on the game's own recorded value until it settles, taps, and judges the tap by that value (`--vs-both` taps every console). Re-run `--cut-off --vs --vs-both --watch 120`: walk settled at `FF 00` after 2.1 s, VS tapped on **both** consoles, value still `FF 00` 4 s later, link perfect to frame 12207 with zero deauth (`logs/v9as_*`). **v0.1.9ca counted the park's frame classes at both ends (`md-link.py census`, `logs/v9f0`): in the parked window CMD 3,590 written/3,590 read, ACK 3,590/3,590, REPLY 3,591/3,590, BEACON 269/269 - every class the games write is forwarded and read, group-addressed destinations included - and both ARM9s are halted in the BIOS wait loop (`pc9 FFFF0238`) with the dequeue's queue empty (`0x2173300 == 0x21732AC`) and `W_IE 0`, so no frame of any class would move them**. **v0.1.9cc wired the per-game seam into the link path and tried the first AMCE-gated behaviour on that wall.** The host's client mask is derived at `Wifi::StartTX_Cmd` as `game bits & MPClientFail`, so a late reply hands the host's own game a pruned mask - its "that client is gone" signal. For MKDS only, `md_set_per_game_fix` (registry bit `MD_PG_NO_CLIENT_FAIL`) stops that narrowing and keeps polling every client. Measured live with the bit applied (`per-game link behaviour: flags 0x1 applied to 2 console(s)`, `logs/pg6_*`): the group still couples, CUT OFF still moves both consoles to the wait, and the group still dies there - radios off, `mask 0x0`, both screens `45d15630`, no deauth - while a same-route run with the switch off (`logs/pg7_*`) died the same way. Survival after CUT OFF varied (260-788 emulated frames) inside the record's known spread, so **no movement to character select or a race can be attributed to it**; the entry stays `implemented: false` and the bit is gated off in the shipped default. The first live run without the narrowing did reach a 38-byte name-carrying command (`6D 00 65 00 6C 00 6F 00` = "melo..."), but so did the baseline runs' own long frames, so it is the game's join phase, not a new state. **Next, with the measurement that supports it:** at the park both ARM9s are idle with the dequeue empty and `W_IE 0` while the ARM7's poster gate `[*(0x0380FFF4)+0x3F4]` reads zero in every sample and no ARM7 image byte writes it (v0.1.9aw) - so trace the wifi context's outgoing queue (`ctx+0x1F4`, where `0x0600F19C` enqueues when its direct path is not taken) and the other fourteen call sites' descriptors across a live park with `tools/scripts/arm7-ctx-watch.py`, to name which sibling message the host waits for. **v0.1.9cd closed that item and moved the verdict off the link for good.** Reading the ARM7's own wifi library against a live AMCE session (`tools/scripts/arm7-ctx-watch.py`, `logs/pg9_ctx.tsv`, 1,032 samples over both consoles) settled where the missing message comes from and why the link layer cannot supply it. The poster at `0x60092D8` is one entry of the ARM7 wifi library's handler table at `0x6018A40`; when its gate `ctx+0x3F4` (`ctx = *(0x0380FFF4)`) is nonzero it allocates an 18-byte message through `0x6009138` and enqueues it into the wifi context at `ctx+0x188` via `0x600F19C`, then clears the gate itself. Across the whole session the gate is **zero in every sample** (`0/1032` for p1, ditto p2), `ctx+0x188` reads `0x00020002` (head == tail, an empty queue) **including during the successful join**, `ctx+0x1F4` is never touched (all `FFFFFFFF`), and the ARM7's IPC send counter stays `0` throughout - so the ARM7 wifi library posts **nothing** on this channel even when the session is working. The ARM9 netcode queue is used and then stalls: its producer/consumer (`0x2173300`/`0x21732AC`, ring at `0x2174000`) advanced to 50 and then read equal (empty) on both consoles. The ARM7 is executing MKDS's own image while coupled (`pc7 0x0600E1DC` / `0x06012764`) and falls back to the BIOS wait (`0x0000115C`) only after the group dies. **Verdict, and it is the mission's second branch: the obligation sits inside MKDS's own ARM7/ARM9 code, not in what this fork provides.** The fork supplies the hardware (frames, registers, interrupts) and the census showed it carries every class the two games exchange; the ARM7's post is gated on a condition its own state machine never reaches, so no change to the link or firmware path can produce the message. **What remains, as a handoff:** (1) identify the exact message type the ARM9's dequeue expects at the wall - the ARM7 handler table at `0x6018A40` (entries `0x0600DA78`, `0x060092D8`, `0x06009234`, `0x0600E1F4`, `0x06015C50`, ...) is where a future RE pass would match a handler to the expected type; (2) find what sets `ctx+0x3F4` - the poster clears it and no literal store to `+0x3F4` appears in the 128 KB ARM7 image, so it is written through a computed pointer by the game's own ARM7 code, which is why the link cannot reach it. **v0.1.9ce deleted the v0.1.9cc plumbing rather than leave it gated off:** `MD_PG_NO_CLIENT_FAIL`/`MP_PG_NO_CLIENT_FAIL`, `md_set_per_game_fix`, `Wifi::SetMPPerGameFlags`/`MPPerGameFlags` and the mask branch in `Wifi::StartTX_Cmd` are gone, so the link is byte-for-byte stock again and every title's default path is provably unchanged (with every entry unimplemented the old gate always returned 0, so the branch always took the stock arm). The per-game seam itself - game identity, the honest registry, the Settings toggle - stays. |
| Metroid Prime - Hunters (USA) (Rev 1) | 2 | ✅ **playable** | **A real 2-player multi-card match runs in the app** (history.md v0.1.9ay): both consoles reach the lobby, both pick hunters, the host's `START GAME` starts a `BATTLE` on `COMBAT HALL` with a 7-minute clock, and each console draws its own first-person view with its own HUD. At 2 consoles the app runs it at **39-42 fps** (55-56 in menus). At **3 and 4** a joining guest aborts its own handshake (`deauth sent ... reason 0003` from the guest, then its radio goes off) somewhere between 7 and 23 s after joining **when the host's game is left on its join screen - v0.1.9bb revised the cause: it is not the extra radios, and a guest left in ~10 s even when the third console's radio was never raised (bus mask `0x3`), while with the host *driven* through its lobby the guest never left at all (`mask 0x7` held past 60 s, zero deauth frames). See the MPH section's v0.1.9bb control below.** (All three of its v0.1.9az configurations left the host idle, which is the mechanism above.) While the host stays healthy and every console keeps running at 39-50 fps. Ruled out by measurement: the stall kick (`MD_KICK_AFTER_WINDOWS=0`, 0 kicks, same drops, guests leaving in the same frame as each other), the reply-window re-anchor (`dropped 0` all run), the lobby's idle timeout (an argument v0.1.9bb reverses: the drops happen exactly when the host's game is left on its join screen, which is what serialising the joins does to it), link capacity and CPU. Serial joins - one guest at a time, waiting for each guest's own screen to leave `AVAILABLE GAMES` - are strictly better than overlapping them and were the first thing to hold a second console at four consoles (host + p4 reached the hunter roster). The limit is **not** the link's delivery path, and that is now measured per client per AID: the host credited 1063 of the answers its three guests sent (278 to aid 1, 328 to aid 2, 457 of 463 to aid 3; `stale 0`, `blank 10`, `reanchor-dropped 5`, no FIFO overflow, no unusable aid), no guest ever *received* a deauth (`deauth out 1 in 0` on each of them), the host was still polling each guest at its last poll, and three *non-joined* radios sat on `AVAILABLE GAMES` for 100+ s untouched - so only a join starts the clock, and what remains is a code-level asymmetry no host counter can see (history.md v0.1.9ba). The v0.1.9bb control then showed the lobby **holds** a guest with three radios up when the host is driven (`mask 0x7` past 60 s, zero deauth frames) and loses it in ~10 s when the host is not - even with the third console's radio never raised. **No 3- or 4-player match yet.** v0.1.9bc ran the pace fix with a new driver (`tools/scripts/mph-match.sh`, the host's next lobby step fired as each join lands): **three consoles hold `mask 0x7` through the whole lobby** (host `role=1`, both guests `role=2`, each guest credited under its own aid, zero deauth frames through the picks) and four reach `mask 0xf`, but the match still does not start - the driver's `START GAME` tap lands on an **empty region** of the host's bottom screen (`appctl.py shot` is native 256x192, so `render --mono` reads the game's own pixels: maximum luminance 133 of 255 at that coordinate, no text or button), so the host's command bodies stay the 6-byte management form and each guest's own game deauthenticates (reason 0003) ~2-10 s later; at four consoles the guests then evict in rotation and the driver's recovery rejoins each. The coordinate is the two-player layout's. Frame rate is not the limit: **36-48 fps per console at four, 46-50 at three**. Measured, and not this blocker: after a drop the host hands `C001` to whichever guest returns (`C001` x6 / `C002` x3 over twelve auth-assoc cycles at three consoles; `C001` x8 / `C002` x4 / `C003` x1 at four), so both guests' replies are then credited under aid 1 - re-association can collide AIDs (history.md v0.1.9bc). v0.1.9bd then re-read every 3-4 host coordinate off the host's own framebuffer and fixed three of them (`BATTLE` `(44,68)`, the `ARENA` green check `(212,172)`, the roster's `START GAME` `(198,40)`; v0.1.9bc's `(220,178)` had been landing on **ADVANCED OPTIONS**), plus a dot oracle for the host ladder (three cyan tab dots at `(67,17)`/`(128,17)`/`(187,17)` that light 0..3 with the lobby steps). With the corrected route **a real 2-console match runs** (both consoles' own `RANK 1`, `POINTS 0/7`, `TIME 6:47` ticking, distinct green/yellow HUDs, `logs/shots/healthy2.png`), and **3 and 4 still fail** the same way: the host ladder completes (`mask 0x7`/`0xf`, zero deauth *sent* at the roster) and then each guest's own game deauthenticates (`reason 0003`) seconds after its hunter pick, `mask` collapsing to `0x1`. The AID collision is *not* this blocker (clean `C001`/`C002` sync at three) and the in-game 262-byte body marker exists only for NSMB, not MPH (the working 2-console host's bodies stay 6 and 38 bytes). Measured lead, not fixed: in the failing three-console run p2 took 191 of the host's 160-byte beacons but only 151 distinct seqs (40 duplicates) while p3 took 195, all distinct (history.md v0.1.9bd). **v0.1.9be cleared that lead with a counter and moved the blocker to the route.** `Wifi::StartRX` now identifies every frame by the timestamp the shared bus carried it with, keeps the last eight hand-overs and counts any repeat (`MPRedeliveries`, printed in the once-a-second clock probe), with `Wifi::MPHoldStamp` holding the **un-clamped** stamp so a late frame is still identified by what the bus sent. Measured: **two consoles 7,341 hand-overs, 0 redelivered; three consoles 16,494 and 12,172, 0 redelivered**; and with the reference arm `MD_MP_REDELIVER=1` (which hands one held frame over twice on purpose) **239 redeliveries in 258 hand-overs**, so the counter demonstrably fires. The 40 "duplicates" are the host's 12-bit 802.11 sequence counter wrapping - each lands exactly **80.1537 s** after the first and every hand-over has a **distinct** bus timestamp. What remains is the route: at the moment of the `START GAME` tap the host's own bottom screen has **nothing** in the roster's pill region (x 148..245, y 28..53: maximum luminance 61-65 of 255, zero orange pixels) against the reference roster `logs/shots/F_start.png` there at 182 with 832 orange pixels - so the host was not on the roster, the tap was a no-op, and the guests' own games deauthed ~10 s later (p2's `reason 0003`, then the host's `reason 0007`). The dot oracle saturates at 3, so a page past the roster also reads "3". **No match started at 2, 3 or 4 consoles this pass** (history.md v0.1.9be). **v0.1.9bf replaced the driver's screen oracle with a per-page pixel classifier** (`tools/scripts/lobby_page.py`, 15 labelled frames in its own `--selftest`) because the completion dots saturate at three and so cannot tell the roster from any page after it; the ladder is now destination-named and idempotent (one verified tap per step, and no retry can advance past the roster). That convicted three of the project's own instruments - a step that named the page it tapped on instead of the one it was reaching (host parked on MODE, the state that drops a guest in ~10 s), `ROSTER` as the "nothing else matched" fallback (a mid-transition frame matches no page at all and read as the roster, so the host's HUNTER step refused to tap and the host stayed on its hunter page all run), and the match oracle itself (a `THERE ARE NOT ENOUGH PLAYERS TO CONTINUE` box over the in-match HUD reads **2729** ink pixels against a real match's **2730** - only the top screen differs, 915 colourful pixels against 16k-33k, so the oracle now requires both and reports `DIALOG`). **The blocker has moved off the route:** at three consoles the host's ladder now lands on the roster with one verified tap per step (ADD BOT 90, pill 0 for the whole watch, listing only itself) while **both guests' own hunter confirms are refused** - p2 and p3 each stay on their own `HUNTER` page after select *and* confirm, so neither is listed and `START GAME` never exists. A live two-console lobby that listed both players and offered the pill still failed the handshake on the tap (`THERE ARE NOT ENOUGH PLAYERS TO CONTINUE` on the host, `YOU HAVE BEEN DISCONNECTED FROM THE HOST` on the guest), so a roster listing a second player is *not* sufficient evidence that the guest's join completed. **No match started at any console count this pass**, and four consoles were not reached (history.md v0.1.9bf). **v0.1.9bg re-established the two-console control with a recalibrated oracle** (a real match's `LAWS OF BATTLE` briefing had been reading as a dialog, so the pass's first "failure" was the threshold, not the game) **and answered the three-console question from the wire: the second guest's join is not refused.** It is admitted, addressed and credited every round - both clients polled under their own AIDs (1 and 2), both arms answered, `collected a reply from p2 for aid 1` + `...from p3 for aid 2`, `collected=0006 of mask 0006`, `clientfail 0` - and beacons reach both clients at **100%** in every steady second right up to the end. The group ends on the *host's* side: its round loop (60.0 commands and 60 acks per second, rock steady) stops mid-second and never resumes while its beacon keeps firing and its `role` stays 1, and both guests' own `reason 0003` deauths are on the wire within 6 ms of that stop (each after accepting the host's beacon, syncing its clock to it, and taking the countdown IRQ14 its own game armed). A watchdog now names that moment in the log with its counters (`cmds_tx`, `rounds_multi`, the lobby's lifetime, `acks_sent`/`acks_client_fail`, and how long after the newest client's association it came - **4.5-60 s across four runs**, so it is not a fixed join timeout), and the one route hypothesis was tested and refuted: a client arriving after the host's roster already offered `START GAME` is not the trigger. Refuted with counters: beacon starvation, reply-window starvation, AID mix-ups (the `AID ... this poll is NOT ours` trace was reading the raw `W_AIDLow` - **fixed**), the frontend watchdog (`kicks 0`), and our own injected input (none in the window; the app's own `[control]` log proves it). **Then the host's own two-client bookkeeping was dumped from main RAM across the stop and compared with a healthy two-console session**: the per-console peer record on page `02101000` counts the clients correctly in both (the host `01` with one client, `02` with two; the newest client's MAC; each guest's own party index 1 and 2), `W_CMDStat0..2` read `0000` at every sample before and after the stop, and the record is stable across the stop - no supplied value is stale or wrong. **And the walk-away is not three-console-specific**: a two-console run this pass lost its guest four times in a row (its own deauth and radio off, the host's roster never listing it - `pill 0` for 80 s), so an unprogressed lobby loses its guest at any console count, and at three consoles the second client's association *removes* the first's ready state (`logs/dc3_match.log`: `pill 413` -> `pill 0` as the third console joins) while both guests' own answers stop carrying state. Verdict: the divergence that precedes the walk-away is in the games' own lobby state, not in anything this link hands them - the mission's fallback branch - and what 4-player compatibility would need is the games' own lobby admitting a second remote client, which this stack cannot supply from outside (`logs/bg_memory-vs-2console.txt`) (history.md v0.1.9bg). **v0.1.9bh, with the link restructured per console:** the same route at three consoles now starts a match (`mask 0x7` held to the end of a 150 s watch, one guest leaving once and walked back in) and the host's *own* beacon carries a **three-entry party list** for the first time on record - 53 frames around the second join, delivered to both guests, against **zero** three-name beacons in five pre-rewrite runs. It then falls back to two names while the second guest is still attached and credited (414 of 18,007 rounds polled it), which is the host's game's own table and not the bus (`collected == cmds tx`, `clientfail 12` of 18,007, `stale 0`, `notlooking 0`, zero watchdog stops). A three-*player* match is still not re-demonstrated: the second client is polled rarely once the match is running, so what plays is host plus one guest with the third attached (history.md v0.1.9bh). **v0.1.9bi corrected that pass's party-list reading and named what ends the three-entry window: it is the guests' own membership.** The "host hears almost none of its clients' beacons" number (35 to the host's game against 2,011 to the other guest) was measurement, not routing: **the clients transmit no beacons at all** in a lobby - only the host does (1,446 room beacons in that match run, 941 in this pass's lobby hold) - and the 60-byte `fc=0080` frames it was counted from are each console's *own* `melonAP` access-point beacon (one per console, every 128 ms, delivered to that console's own game only while it is outside multiplayer, never on the bus; `tools/scripts/beacon-party.py` now separates the room beacon from the AP beacon, and this is the third time a per-console local frame has been read as bus traffic). What the host's game does hear from each guest is everything the guest sends: the guests' association/deauth/data frames, and 17,996 of 18,006 answers from aid 1 and 414 of 420 from aid 2 in that run with `clientfail 12`, `stale 0`. Three party entries hold for exactly as long as the guests' games stay in the room (26/27 delivered three-name room beacons then, 34/38 in this pass's lobby hold) - and each guest's own game then writes its own `deauth sent ... reason 0003` and powers its radio off **11-18 s after associating, with every poll it received answered and every answer it wrote collected** (`clientfail 0`), which is what makes the table fall back to two names; the host never deauths a client in any of these runs. **No three-player match yet**, and the divergence to look at next is that in the lobby hold the host's roster never registered either guest's pick (`pill 0`, `icons 0` for 15 s twice) while the guests' own screens read `page=HUNTER pill=69 icons=333` after their picks (history.md v0.1.9bi). **v0.1.9cf exercised the three-console gap live through the app's own route and took the two-console control on the same binary.** Two consoles: the route walks the lobby, the host's roster offers `START GAME` (`pill 414`), the tap starts a real match and it holds - `mask 0x3`, `deauth-sent 0`, the host's own bottom screen reads `INGAME` with a colourful top screen for 25 s+ and the host's round loop never stops (`cmds tx` still climbing at the last sample, `clientfail 0`) (`logs/mp2c_app.log`, `logs/mp2c_match.log`, `logs/shots/mph2_match_a.png`). Three consoles: both guests associate (`role=2`), the host walks its ladder, and then **the host's own game stops writing commands** - its own watchdog line reads `last command seq 5920 at +62.832s, 7113 ms after the newest client associated, after 640 rounds (426 addressing 2+ clients) over 10.680s ... 640 acks, 0 of them reporting a failed client, and the beacon slot is still armed` - and both guests' own games then deauth (`reason 0003`, `p2 0009BF126643 -> 0009BF112233`, `p3 0009BF13AA53 -> 0009BF112233`) and power their radios off; the host's roster never lists a second player (`pill 0`) `logs/mp3c_app.log`, `logs/mp3c_match.log`, `logs/shots/mph3_picks.png`. **The instruments say the link is not what breaks it.** `md-link.py census` over the trace-verbose run: the host wrote 640 CMD and 640 ACK, and **every frame a console wrote reached its peer's game** (`frames the fork itself did not carry: none`); both guests received all 640 commands (p2 640 direct; p3 426 addressed to it plus 214 delivered as a group frame) and each answered from its own arm (p2 639, p3 426); the host's reply windows read `has everyone it asked for` throughout and its acks report `clientfail 0`; the host addressed both clients (`mask 0x0006`) for 426 rounds. `md-link.py rounds` pairs the guest answers with the host's collects as **identical 1065, DIFFERENT 0**. Group addressing, per-client reply, ordering and the join-time mask all work for the extra console; what fails is MPH's own host game, which stops its round loop ~7 s after the second client associates regardless of what the link hands it. **Verdict: the mission's second branch - MPH's own code, not the link - so no link change was made; every other title's path is untouched.** Measurement note: the three-console runs used `MD_KICK_AFTER_WINDOWS=0` because the stall-kick watchdog resyncs a console during multi-guest joins (its own documented hazard), but the same failure appears with the watchdog at its default (the host kicked three times pre-join, the same guest deauths follow), so the kick is not the trigger (history.md v0.1.9cf) |
| Star Fox Command (USA) | 2 | 🟡 **v0.1.9di emulates the baseband/RF busy flags (`W_BBBusy` 0x15E, `W_RFBusy` 0x180) instead of returning 0, and the flag theory is falsified by the trace.** A transfer starts on a `W_BBCnt` write (5 = write, 6 = read) or an RF register clock (`W_RFData2`, `W_RFCnt` bit 0), the flag reads 1 until the byte is clocked through and then 0, and `W_BB_READ` holds the previous transfer's data while one is in flight (GBATEK's "from previous W_BB_CNT transfer") - which is the pipelined shape the child's driver already uses. Live on the unchanged route (`logs/v9bbrf.log.gz`, `logs/v9bbrf_wifiio.txt`, route exit 0, nothing set; transfer still 2502 packets / 1260340 bytes; carry still channel 7, BSSID `00:09:BF:11:22:33`, AID 1): the child's busy polling now fires (`W_BBBusy` 2,253 -> **15,932**, `W_RFBusy` 123 -> **643**), and **every other number is byte-for-byte what v0.1.9dh measured** - `W_BBRead` 1018 last `003F`, `W_ModeWEP` 11 last `000A`, `W_Random` 6 last `007F`, `W_Preamble` 6, the `W_MACAddr`/`W_BSSID`/`W_AIDFull`/`W_XBuf` registers 5 each - with `TRX power ON` still **x0 on the child against x8 on the host**. So the child is not blocked on the baseband busy flag. It is not spinning on it either: four `probe --player 2` samples 6 s apart read the same `pc7 0000115C` / `r0 04000000`, and `lr7 037F84FC` lands in the child's own ARM7 code, read live and disassembled - a retry loop at `0x037F84F8`-`0x037F851C` that calls the thunk `0x037F852C` (`bx 0x038043E3`) and loops back, ending in the FreeBIOS ARM7 delay at `0x115C`, while the ARM9 alternates `0x0201C7FC` (`bl 0x02087350`) with `0x0201CD34`. No regression on the working console: the host read `W_BBBusy` 15,616 times and completed the whole DP handshake and transfer. `cargo test --lib` 39/0, `md_smoke_test` 149/0. one unattended command now takes the route to its parked state (host → joiner registered → Cut Off), and the whole handshake clears with every reply collected and no deauth — `NameRequest` → all five name snippets → **`RSA`** → RsaDone — with the `RsaReply` landing *inside* the RSA round; the host still never streams `04 DataPacket`, and it parks at RsaDone *before* Cut Off, so the transfer's trigger is inside the host's own game. **v0.1.9co reads that host code instead of inferring it:** the parked command frame is composed by Nintendo's ARM7 wireless library into wifi-RAM slot 0, the class byte at slot offset `0x2A` is written every round by one halfword copy with the same value `00`, and the host's ARM9 is meanwhile pushing one word per command round into the ARM7 IPC FIFO — so the host is scheduling a dummy, not waiting for a frame, and the condition that would move the class off `00` is still unnamed. **v0.1.9cp probed that gate empirically instead of reading it** (`tools/scripts/sfc-dp-poke-search.py`: one word at a time through the host's entrant/received-name/DP-title records, held and restored, scored on the joiner's received class and the composer's `002A` class byte, with `commandsRecv` liveness): over four parked sessions the classes ever seen were `00`, `01`, `03` — **never `04`** — and the `002A` byte stayed class `00` through every trial, so the gate is **not a plain single word in those records**; the words that do anything are structural and only **freeze** the host (`0x020B4C00` word 0 set to 0 makes it forget its client and fall back to the 34-byte six-byte-body `fc=0228` command form, while `0x020B4C40`/`0x020B4C48`/`0x020B17A0` word 0 each stall the command stream 10–30 s and recover). The host's own counters also show its reply windows almost all **empty** (`collected 3k` vs `empty 108k`, `clientfail == empty`) because its trace prints `this frame only has 0us of slack left` — the per-frame reply budget is spent before the window — yet replies *are* collected in bursts each round (body `00 00`, reply type Dummy) and it still repeats `00`. **v0.1.9cq then read the decider instead of the data, and named the byte that sets the class.** The per-round site v0.1.9co located (`0x02030660`) is the ARM9 SDK's IPC-FIFO send primitive, pushing a *pointer* to a WM message rather than the class itself; the host's Download Play engine is a compact **Thumb** module at `0x02029C00`–`0x0202A000` whose phase-2 handler (`0x02029DD0`) picks `4` or `9` from `buffer[0x4b2]` and whose phase-5 handler writes `titleA[6] = state+0x0E` — both held and **neither moved the wire class** (forcing `buffer[0x4B2] != 0` changed the module, `state+0x10` became `4` and the phase machine began cycling `01 -> 03 -> 04`, while the wire stayed class `00`), so those are negative measurements. The real command body was found structurally — **no code in ARM9 `0x02000000..0x02400000` contains the literal `0x023722E0`**, so the pointer to it is computed and perturbing the *records* could never have found it: the body is the 6-byte block at ARM9 `0x023722E0` (`class, 50 08 00 02 00`), referenced by the TX descriptor table at `0x0237A7EC` (`+0x08`) and the WM message ring at `0x020B5440` (`+0x04`, length 6), and its first byte is exactly the byte GBATEK's CMD DATA `[06h]` carries to wifi-RAM slot offset `0x2A`. The host rewrites it to `00` every round (~166/s, restored within 20 ms), and **holding it at `04` makes the host arm and stream `04 DataPacket`** — 15,356 wire frames of `06 01 02 00 03 11 04 50 08 00 02 00` against 309,913 of the unpatched `06 01 02 00 03 11 00 50 08 00 02 00`, with the joiner announcing class `04` 968 times and collecting it 245,705 times over a 75 s hold. **This is a host memory patch, not a client behaviour** (`tools/scripts/sfc-dp-arm-class.py`, not wired into the app): the joiner already answers NameRequest and RsaDone, its replies are collected (`our reply collected 22300`, `stale 0 deauth out 0`), the association is accepted (`status 0000, AID c001`), the host logs `[HOST] syncing client C001` — and the class is still `00`, because it is the *output* of the host's own DP phase decision, written after it reads its replies, so no frame this client can emit moves it. **Captured frame layout (the spec for the capture side):** `06 01 02 00 \| 03 \| 11 \| CLASS \| 50 08 00 02 00`; GBATEK's real class-`04` payload (`09h u16 Packet Number, 0Bh .. data`) is absent because the host's own engine never builds it — the frame layout is real, the contents are the host's placeholder. **v0.1.9cr then found the writer of that byte and the gate behind it.** A new store watch (`appctl.py memwatch`, `NDS::SetMemWatch`) reports which instruction stores into a span of a console's own main RAM, with its registers — needed because the writer reaches the byte through a computed pointer, so no literal exists to search for. It named one writer, on the interpreter: `strb r3,[r1]` at `0x0202BBDE`, in the publisher at `0x0202BBD8`, `r1 = 0x023722E0`, `r3 = 0`, `cpsr = 0x000000F2` (**IRQ mode** — the engine composes its command from an interrupt), called from `SendCommand(cls, arg)` at `0x0202864C` (`0x0202865C`), which writes into the outgoing buffer at `*(0x020B3DBC)` — live `0x023722E0`, i.e. **the DP state struct begins with its own command buffer**. All five callers of `SendCommand` are one function, the engine's per-round poll, and they send classes `5, 1 (NameRequest), 6, 2, 0` — `0` exactly when a decider returns **21**, the engine's "nothing pending" answer. Which decider is live was measured: forcing the first one's scan test changed nothing across 108,853 samples, proving the live path is `0x020852C`; and that one returns 21 when it picks no client whose **mode field** `*(u32*)(base + 0x14E8 + 4*(i-1))` equals **5** — live, the registered client's mode reads **14**, so the host sends Dummy every round. Holding that one field at each value takes the host off Dummy: **`2 -> 01`, `4 -> 02`, `5 -> 03` (streamed, 60,216 of 60,380 samples), `8 -> 05`, `11 -> 06`, everything else — including the natural `14` — `-> 00`**. So the field is a class selector, and it is the host's own per-client engine state: nothing a guest puts on the wire reaches it. The class-`04` path **does exist** and is not a stub — `0x020283A2`, gated by `0x0202913C`, a seek into a **segmented image** (binary search over `u16` segment starts at `desc+12`, returns 0 past `*(u16*)(desc+18)`, else fills segment start/length/source/index and returns 1) — but it hangs off the decider the live engine does not call, so no value of the mode field reaches class `04`. The branch is named exactly, with the values it reads; the input that opens the data path is host-owned and still unnamed. **v0.1.9cs watched that mode field and named its writer, and then forced the routing.** The field is set by one host routine, `DP_SetClientMode` at `0x02028D84`, whose guard (`0x0202817C`) is only `cmp r0,#1 / bcc / cmp r0,#15 / bhi` — a bounds check, not a content gate: **no frame a guest sends takes part in choosing the mode.** Arming the store watch *after* the handshake sees zero stores — the field is written once per state and never rewritten — so the watch has to be armed first, and it then shows the engine walking its own machine: two init clears, then **2 → 10 → 5 → 14**, each from the store at `0x02028DA0` in IRQ mode. The twelve call sites carry one mode each (`10, 11, 5, 4, 14, 6, 7, 8, 1, 2, 9, 3`). Why mode 5 means class 3 is now code, not just measurement: the poll (`0x02028228..0x020282FC`) routes on one halfword to one of two choosers, and the **live** chooser is `0x020852C`, which emits class **3** at `0x020285FC` after histogramming clients whose mode is **5**. The class-**4** emitter (`0x020283A2`) is inside the *other* chooser, `sub_020830C`, which the live engine never calls. Forcing the poll's routing there (`poke 020282DC = E0022800`, a `beq`→`b.n`, plus the client slot's `+0x1D4C` u16 set to 1) makes the engine build a **real payload**: the store watch confirms the class-4 site (`lr = 0x020283C9`), the composed body is `04 00 00 00 00 53`, the bytes beyond the 6-byte header read **`"STA RFOX CMD "`** (the game's own data), and the WM message ring `0x020B5440` goes from length **6 to `0x166`** — 358 bytes. **But nothing reaches the wire, and that is measured:** the TX slot's class byte stays frozen at `n=39720` while the slot is still armed (`73,008 → 73,415`, i.e. retransmitting its old body), the ARM9→ARM7 IPC keeps flowing, and the only frames in the log tail are `tx frame: slot 1 (CMD) fc=0228 len=34` — the short form the joiner's own length guard refuses to read as a command, which is why `commandsRecv` freezes and the class stays `00`. So the hand-over to the wireless library does not happen on that path, and **nothing a guest can put on the wire reaches it**: this half of the problem cannot be solved from the joiner. **v0.1.9ct finds the hand-over and shows it working, and corrects that conclusion.** The ARM9 crosses to the ARM7 by pushing `(slot << 6) \|\| 0x0A` down the IPC FIFO, where the slot is one of **eleven 0x100-byte message buffers** at `0x020B4C40 + 0x100*k` that the ARM7's own wireless library lists in a descriptor at `0x023F8338` (`{table 0x023F8348, size 0x20, count 0x0B}`, found by scanning the dumped library for the ring literal `0x020B5440`). Every slot holds the same record `{id 0x800F; buffer 0x023722E0; length; 0xFFFF; 1; 3; ARM9 callback 0x0202B085}`, and the composer that builds the wifi-RAM TX slot (`0x023EFC18`, ARM7 code in main RAM run through the `0x027E0000` mirror) copies the body only when `[sp+4] != 0` **and** its descriptor's `[r5+0x1C] != 0`, writing slot `0x0A` as `[r5+0x1C] + 0x22`. **The message's length at `slot+8` is that length**, and holding it drives the composed body: `6` -> a 14-byte frame body, `0x40` -> **72**, `0x1F8` -> **512**, `0` -> no body at all (`tools/scripts/sfc-dp-data-frame.py`, a labelled host patch that touches only slots really carrying the DP command and restores every word). So the `0x166` of v0.1.9cs did reach the wireless library; it was the forced engine path's descriptor that came out zero-length. **Captured `04 DataPacket` frames, with the layout a capture-side reader needs:** `06 01 02 00 / PP / 11 / CC / <payload>`, six-byte DS header, `PP` the host's incrementing DP packet number, `CC` the class, payload the host's own buffer from `0x023722E0` - e.g. `body[72]=06 01 02 00 20 11 04 50 08 00 02 00 00 38 02 00 00 00` (93 in one 6 s hold, the joiner collecting `04`) and `body[512]=06 01 02 00 FC 11 04 50 08 00 02 00 00 38 02 00 00 00` (38). The host also emits a 242-byte class-`03` frame ten times during the handshake, so long bodies were never the obstacle. **What is still missing is the download itself, not the transport:** the payload is the host's own stub, because the host's engine never fills `0x023722E0` with image bytes - and the held words perturb the host's own state machine (after the `0x1F8` sweep it deauthenticated the joiner, reason `0001`, and moved to another DP phase). **v0.1.9cu tests the "the gate is host-internal" conclusion from the link, and it does not hold.** Correlated on one clock and with **no memory pokes**, the unpatched host's per-client phase walks `2 -> 10 -> 5 -> 14` (four stores, all from `pc 0x02028DA4`) while its command class walks `01 -> 00 -> 03 -> 00` in **243 ms emulated**, each phase store in the same 150 ms window as its class change. With the joiner associating and then answering **nothing** (`MD_DP_REPLY_MUTE=1`, a new diagnostic arm: nothing before the association differs), the host's phase cycles `2 -> 3 -> 0` and never reaches 10, 5 or 14, its class changes exactly once - to `01` - and stays there for **20,683 commands** with **0 replies collected in 20,069 empty windows** and 13 deauths (`reason 0001`). So phase 14 is where a *working* joiner puts the host: the client is causally upstream of the host's progression, and the earlier reading holds only for the class byte, which the host derives from its own phase. **The next input:** the Thumb dispatcher at `0x02028906` sets mode 14 on `phase == 5` and sets **mode 6** - the download direction - on `phase == 14` only when a per-client halfword read as `ldrh [r1 + 2*(2*client) + 0x1754]` equals `2`; the address is recorded as open rather than asserted. The host still never streams a download payload. **v0.1.9cv pins that halfword and opens it from the joiner.** The dispatcher `0x02086F4({message, client})` reads the client mode as `word[base + 0x14E8 + 4*(client-1)]`, jumps through the table at `0x020873C`, and runs its `cmp r6,#5 / cmp r6,#14` handler with `r1 = base` and `r5 = client-1`, so the tested halfword is `ldrh [base + 0x1754 + 2*(client-1)]` - for the registered client exactly `0x02373A34`, which the parked host reads as `2`, so the gate's precondition already held and `r1` is not clobbered on that path. The driver is the reply's **type**: the dispatcher takes it at `0x0208726` from the per-client message and the helper at `0x020BB14` splits it on `7 / 8 / 9`; `07` is `dp_joiner`'s UsernameReply and `08` its RsaReply. Username replies drive the walk `2 -> 10 -> 5`, one RsaReply drives `5 -> 14`, and the **same class-8 arm (`0x0208906`) drives `14 -> 6`** (`DP_SetClientMode(client, 6)` at `0x0208946`) when that halfword equals `2`. The host keeps asking a `00` dummy round after RSA, and the joiner was answering it with a type-`00` dummy - which the mode-14 arm ignores - so the host parked forever. It now answers every post-RSA round it has no specific reply for with the **RsaReply**, and with **no pokes and no override** the mode field walks `2 -> 10 -> 5 -> 14 -> 6` (store watch over `0x023737C8`, the extra `<- 00000006 @pc 02028DA4`) and the host **streams a real `04 DataPacket` (size 179, not its 3-byte stub)** with `tools/scripts/sfc-dp-host.sh` reporting `TRANSFER STARTED` (exit 0). So an unpatched host enters the download direction on its own, and the older ending is superseded. Two diagnostic overrides sweep the reply type per round (`MD_DP_REPLY_FORCE`, and `MD_DP_REPLY_FORCE_DATA` for the `04` rounds): a type-`09` reply on the `04` rounds already advances the host's packet loop (`04 DataPacket` size 179 then **255**), but the type-9 body's next-packet halfword (`ldrh [sp+18]` at `0x020896C`, compared and stored against `base + idx*0x5D4 + 0x1D4A`) still carries a username snippet, so placing that field is the next input - the download is no longer blocked, only its packet loop is not yet driven. Validation: `cargo test --lib` **28 passed, 0 failed**; `md_smoke_test` **149 checks, 0 failures**. Evidence: `logs/sfc_natural.log.gz`, `logs/sfc_force8.log.gz`, `logs/sfc_data9.log.gz`. **v0.1.9cw: the download now completes naturally, with no override set and nothing poked.** The packet loop is implemented rather than probed: a `04` DataPacket is parsed (packet number at body `09h`, data at `0Bh`, the spec's one padding byte dropped so a full packet comes out at `1F8h`), every `04` round is answered with the **DataReply** carrying the smallest missing packet at `03h` and the distinct-packet count at `05h`, the **GotAllReply** (`0A`) replaces it the moment the RSA-stated total has arrived, and the host's `05` Final is confirmed with `0B`. Two consoles through `tools/scripts/sfc-dp-host.sh` (exit 0): the RSA states 1260340 bytes, the first packet is `#0` with 352 bytes starting `53 54 41 52 46 4F 58 20` (`"STARFOX "` - real image data, not the host's 3-byte stub), progress walks packet by packet at roughly 12 packets/s, and the image ends **complete - 2502 distinct packets, 1260340 of the 1260340 bytes the RSA frame stated** - after which the host's class goes `04 -> 00` and the GotAllReply is answered (`logs/sfc_dl.log.gz`, `logs/sfc_fix.log.gz`). `cargo test --lib` **33 passed, 0 failed**; `md_smoke_test` **149 checks, 0 failures**. | The only local mode is Download Play; a multi-card join does not exist in the cartridge. **v0.1.9cg drove the host to its Recruitment screen reproducibly and captured what it sends** — `tools/scripts/sfc-dp-host.sh` takes a cold two-console launch into Download Play → Recruitment by touch geometry, deciding success only from link state (`role=1`, console on the bus, 204-byte `fc=0080` beacons leaving), and it is one command with a state-based pass/fail (cold run: `PASS ... beacons on the wire (23, +23)`, `logs/sfc_dp_host.log`). `md-link.py census` over the captured session (`logs/sfc_dp_wire.log`) shows the host writes **42 BEACON frames (`fc=0080`, 204 bytes, broadcast `FFFFFFFFFF`) and nothing else** — no command, reply or download class is on the wire, and the peer's game reads 0 of them because it is not in Download Play. So what exists is an *advertisement*: the host broadcasts and waits for a client that has never existed here. **And the receive path is genuinely absent, in this fork and in stock melonDS** (see the receive-path finding in to-do.md): `NDS::SetupDirectBoot()` boots the cartridge's own ROM (`src/NDS.cpp:297`, `upstream/master:src/NDS.cpp:297`), there is no multiboot/download-path keyword anywhere in either tree, and the only `MB_*` symbols are the DSi wifi *mailbox* (`src/DSi_NWifi.h:89`), not DS multiboot. The client must be written from scratch — which is the whole of the joiner's side. **v0.1.9ci built the client's first half and got the host's answer: it is a refusal, and it is the host's own words.** `src-tauri/src/dp_joiner.rs` (gated by the `ASFE` registry entry and the one Settings switch) listens for the beacon and answers it with an open-system authentication request, then an association request echoing the beacon's own IEs. On the wire (`logs/sfc_dp_join.log.gz`): the host reads every frame the joiner sends, answers the authentication **205/205** (`fc=00B0`, unicast to the joiner) with **sequence 0002, `status 000E`** — 802.11 status 14, "authenticated transaction sequence number out of expected sequence" — so the transaction is answered but never granted, and its answer to the association is a **deauthentication `reason 0006`** ("class-2 frame from nonauthenticated station"). Zero association responses. So the download conversation is still not reached; what is now known is the exact refusal — the authentication transaction the DS expects — and that the transport carries a synthesized client correctly. Gating proven: switch off ⇒ zero joiner frames on the wire while the host beacons normally (`logs/sfc_dp_off.log.gz`), and a non-Download-Play title gets no joiner thread at all. **v0.1.9cj got the client in: the authentication is granted and the host accepts the association, and the exact frame it waits on next is named.** Two defects were fixed on the client side. First, the frame builders omitted the hardware's 4-byte FCS slot, so the bus wrote the wire CRC over the last four bytes of the body and the host read its transaction sequence number out of a checksum - the `status 000E` of v0.1.9ci; with the slot restored the authentication is a 34-byte frame (`24 header + 6 body + 4 FCS`), exactly a real client's, and the host answers **`sequence 0002, status 0000`**. Second, the association request now matches a captured DS client byte for byte: a real request (`logs/ab_ctl_app.log.gz`, `body[0..41]`, `tx frame: slot 2 (LOC2) fc=0000 len=70 seq=0010`) is `capability + listen interval + 32-byte SSID IE + the four supported-rate bytes raw` - **no rates information element** (Nintendo's own layout, `WifiAP::RecvPacket`) - where ours had been a 72-byte frame with a rates IE and the host's empty SSID echoed. It is now 70 bytes on the air and traces `len=82 fc=0000 seq=0010 sync=0025200000010021`, identical to the captured client; `parse_beacon` reads the same layout (the four raw rate bytes at body `12..16`, the SSID IE scanned from body 16). **On the wire** (`logs/sfc_dp_join3.log.gz`, 2 consoles, Recruitment host, joiner on console 2): **47 authentications, 47 answers, all `status 0000`**; **47 associations sent, 46 answered with `fc=0010`** (`p1 tx len=50 fc=0010 rx=0009BF126643 ... sync=0201000000010021`); **46 host deauthentications `reason 0001`** (30-byte frame, 6-byte body). So the host accepts the joiner and the download conversation is entered. **What it waits on:** an MP *command* frame is one whose address-1 is the multicast `MPCmdMAC = 03:09:BF:00:00:00` (`src/Wifi.cpp:2006`); the host's own digest reads `cmds rx 0` continuously while its game polls the receive window (`window reads 3700 (+74/cycle)`, `polls 9869 (empty 9865)`), it hands the association to its game marked `fc=0000 ... (not a command)`, and ~3.9 s later it deauthenticates (reason `0001`) and re-accepts. The missing next piece is therefore a **client command frame addressed to `03:09:BF:00:00:00`** - the association already reached the host game's hands, and the deauth is its timeout for the command after it, not a refusal. **v0.1.9ck made the client as faithful as the captures allow and disproved that inference.** Two things changed on the client. (1) The captured protocol's direction: GBATEK's Download Play chapter has commands as *host-to-client* (`03:09:BF:00:00:00`, `W_TXBUF_CMD`) and clients sending *replies* (`03:09:BF:00:00:10`, `W_TXBUF_REPLY1`), and a scan of every captured multi-card log confirms the only console that ever writes a command is the host - so the client's first frame to reconstruct is the short dummy **reply** (`fc=0118`, body `00 00`) a real client sends right after associating, captured in `logs/mph4b_app_4con.log.gz` (`tx frame: slot 5 (REPLY) fc=0118 len=30 seq=0020`; the host logs it cropped as `26 byte frame, body[2]=00 00`). `dp_joiner.rs` now builds and sends exactly that. (2) Every captured host's beacon carries its game serial in the `DD` tag at data offset 12 (MPH `0A 00 40 00`, NSMB `50 01 40 00`, Star Fox `18 02 40 00`) and each host's clients echo that same word as their SSID's first four bytes; ours had sent `25 00 00 00`, so it was asking for a title the host did not have. The serial is now parsed from the beacon and echoed (`[dp] hearing ... game serial 00400218`, association body `21 00 01 00 00 20 18 02 40 00 ...`). **The premise does not hold:** `MPD.cmds_rx` is incremented only on CheckRX's `(macgood && IsMPClient)` branch, so a host can *only ever* print `cmds rx 0` - the inference from it was invalid. A command sent to the multicast command MAC reaches the host's game (`36 byte frame, fc=0228` delivered) and changes nothing (and is also dropped by the host's own filter, `RXFilter2 kills from/to=2`). The real dummy reply is delivered and **consumed** (`handed ... fc=0118 consumed=26 halfwords`), the association is accepted every time, and the host's game never writes its command slot (`cmd 0000` on every `tx frame`, `cmds tx 0`), sits in `arm9pc 0x02030488`, and deauthenticates `reason 0001` ~3.9s later. So the host's Download Play code never starts the transfer, and no frame class reachable from the wire has unblocked it. `logs/sfc_dp_final.log.gz` **v0.1.9cl found the field the host was actually refusing on, and the host now starts the transfer.** The previous record read the host's association response as an acceptance; it never decoded the body. The trace's `sync=` field is that body's first eight bytes as one little-endian u64 (`PlatformBridge.cpp`: `sync = *(u64*)&frame[24]`), so a working host's response decodes to capability `0021`, status `0000`, **AID `C001`**, rates `01 02` (`sync=0201C00100000021`, what every real-game log carries) - while every Star Fox joiner run had been reading `sync=0201000000010021`, i.e. status **`0001`** (unspecified failure), **AID `0000`**: a refusal, confirmed independently by `[HOST] syncing client` (which requires a nonzero AID) firing in every real session and never once here. The joiner could not have known: its `Rx::AssocResponse` was a unit variant that discarded the body. The missing field was in the client's SSID. Real game clients' association requests, byte for byte from three titles, are `serial(4) + <beacon DD bytes 16..18>(2) + 00 00 + name/padding`: MPH's client sends `0A 00 40 00 12 23 00 00` against a beacon whose `DD` tag reads `... 0A 00 40 00 12 23 54 01` (`logs/mph4b_app_4con.log.gz`); NSMB's sends `50 01 40 00 DA A3 00 00` against `... 50 01 40 00 DA A3 1E 01` (`logs/nsmb_app.log.gz`); a third sends `25 00 00 00 A9 D4 00 00` against `... 25 00 00 00 A9 D4 1C 01`. The joiner had echoed only the serial and left the next two bytes zero. It now parses the beacon's `DD` token and echoes it (`HostBeacon.token`, `client_ssid(serial, token)`), and `Rx::AssocResponse { status, aid }` carries the fields that decide the join, with acceptance defined as **status 0 *and* a nonzero AID** - the host's own test - so a refusal is never logged as an acceptance. Measured live twice (`logs/sfc_dp_token.log.gz`, `logs/sfc_dp_token2.log.gz`; 2 consoles, Recruitment host on console 1, joiner on console 2): each run is one clean handshake (`auth 1/1`, `assoc 1/1`, `last assoc status 0x0000, AID 0xc001`), the host logs `[HOST] syncing client C001`, arming its command slot (`slots beacon 811C cmd 8000`) and transmitting `fc=0228` commands whose body decodes (GBATEK CMD layout) to reply-time `0106`, **slave flags `0002` (slave 1 connected)**, size 3 and command `01` = **NameRequest**. **Where it now sits.** The host repeats that `NameRequest` (13,614 commands in run 1) while its reply window collects nothing (`collected 0`, `clientfail` rising each window), because the joiner's answers go out as broadcast packets via `md_link_send_packet` -> `MP_SendPacket` - the regular receive path - not the reply path the host reads through `Platform::MP_RecvReplies` -> `LocalMP::RecvReplies`, which is keyed by AID. No bridge exposes the reply send yet (the core has `MP_SendReply`); that, with the AID the host assigned, is the next step. **v0.1.9cm exposes it, and the handshake now advances.** The new bridge surface `md_link_send_reply` calls `LocalMP::SendReply`, so the joiner answers on the reply path the host reads (`LocalMP::RecvReplies`, keyed by AID) instead of the broadcast path `md_link_send_packet` reaches; the reply carries the AID from the host's association response. One more live defect fell out: replies were stamped with the drained frame's own timestamp and every one was **stale** (`stale 107`, `collected 0`) - the host's staleness window is `LocalMP::kReplyStaleWindow = 2*16715`, and stamping `rx_ts + 2*16715` puts the reply inside it. The bodies are reconstructed, not invented: a captured client's 38-byte UsernameReply (`logs/mph4b_app_4con.log.gz`, `collected a reply ... 38 byte frame`, body `04 01 07 <snippet>` + six-byte snippet) and GBATEK's RsaReply (a normal reply whose type byte is `08`, seven filler bytes, no key material). Measured live (`logs/sfc_reply5.log.gz`, `logs/sfc_rsa2.log.gz`): from `status 0000, AID C001` the host's command class advances `01 NameRequest` → `00 Dummy` (NameDone) → **`03 RSA`** (body `06 01 02 00 75 11 03 ...`, size 117 halfwords) → `00 Dummy` (RsaDone); all five username snippets are collected (`04 01 07 00`…`04`), the RSA reply (`04 01 08 …`) is collected, the joiner's counters read `our reply collected 711 times`, and the host's digest reads `collected 470 (+61/5s) ... stale 0 reanchor-dropped 0 deauth out 0` (`cargo test --lib` **27 passed, 0 failed**; `md_smoke_test` **141 checks, 0 failures**). **A regression introduced and caught live:** a `username_idx < 4` guard stopped the *fifth* username snippet from being sent, so a run sat on `NameRequest` forever after collecting snippets 0-3; the guard is gone (covered by a unit test). **The next command class is gated by a host button, not another client frame.** After RsaDone the host sends no `04 DataPacket`: its Recruitment lobby ("Searching for opponents ... Touch the Cut Off button to move on. Entrants 2/6") only begins the transfer after the host's own **Cut Off** tap - the left of the two buttons at DS `(91,178)` on the host's bottom screen. After that tap it leaves the lobby onto "Please wait a moment..." and holds at command `00` (`06 01 02 00 03 11 00 ...`) for minutes with no `04 DataPacket`. So the next thing to make work is what that RsaDone/"Please wait" state waits on, with the host past its own button. **v0.1.9cn makes the route one command and fixes the reply timing; the host still parks at RsaDone.** `tools/scripts/sfc-dp-host.sh` now walks the menus, waits for the joiner to be registered (`AID != 0`), taps Cut Off at the measured DS `(91,178)`, and polls for the transfer's first `0x04`; its exit code is the state (0 = `0x04` seen, 2 = host role but parked, 1 = no host role). **The reply-timing defect and its fix:** the joiner answered one command per step, oldest unanswered first, at ~12.5 replies/s against the host's ~166 commands/s - and usually for a class the host had already left, so all 19 RSA windows collected nothing, the host abandoned RSA, and our `RsaReply` was read against a later dummy round and ignored. It now answers the **newest** class in the drain batch (the reply path keeps only the newest answer per client, `MD_REPLY_KEEP=1`), and in `logs/sfc_final.log.gz` the host collects `04 01 08 6F 00 6E 00 44 00` *between* its `03` and `00` transitions and moves on immediately. **A second measured rule:** the name phase's `00` (NameDone) must be answered with the username - a dummy there and the host never sends RSA at all (measured twice: classes `01 -> 00`, parked), the username there and it sends `03` (measured twice: `01 -> 00 -> 03 -> 00`). **Three reply-level guesses tested and removed** (answering every command in a batch floods the single-slot answer queue and the host deauths with reason 0001; a normal dummy and a DataReply on RsaDone change nothing). **The parked state, as numbers:** the host's class stays `00`, it never writes a Data command (its CMD slot stays armed with the dummy, every command frame 54 bytes), its window collects **every** reply we send (715/715) with `stale 0 deauth out 0`, its ARM9 alternates `02030488`/`020A35A0` with `IME=0001 IE=2009 IF=0000`, and its own wifi read loop is ordinary MP polling (`W_IF`/`W_IE`, `W_TXStat`, `W_TXSlotLoc1/2/3`, `W_CMDStat0`, `W_TXReqRead`) - not a Download-Play register. The park begins before Cut Off, so the host's own game holds it, not the wire. **Memory:** the received name is in an entrant record at `0x020B4C20` and the DP title records keyed by serial `00400218` sit at `0x020B3D40`/`0x020B5640`, so the state is addressable even though none of it proved to be the trigger. **v0.1.9co stops guessing reply bodies and reads the host's code, and the reading is what the pass produced.** Two new instruments: `wifiio` now reports a **write-side recorder** - every write this console's own software makes into the low wireless RAM with the writer's PC/LR, a per-offset last-writer table, and the command-slot arm's PC/LR plus the writer's whole register file - and the **IPC FIFO word trace**, both directions, each word with the pushing PC; `MD_NOJIT=1` autostarts on the interpreter, because the JIT reports `R[15]` as a block start and every `pc=` the recorder printed before this was therefore a block address, not an instruction address. With the interpreter on, the host's command frame is at wifi-RAM offset 0 (body at slot offset `0x24`, so GBATEK's CMD_DATA class byte `[06h]` is at `0x2A`): the body's first three halfwords (reply time `0106`, slave flags `0002`, size and flags `1103`) are written by the library routine at `0x027EFE94`/`0xE9C`/`0xEA4`, and everything from `0x2A` (`00 50 08 00 02 00 02 00 02 00 B8 B6`) by one halfword copy at `0x037FE66C` reached from the `bl` at `0x027EFEC8` - **9,529 writes of that byte against 9,529 slot arms, always the same value**, so the class in the slot is fixed and no guest frame can change it. The arm is `strh r2,[r1]` at `0x027F001C` with `r1 = 0x04808090` (`W_TXSlotCmd` in the ARM7's mapping) and `r2 = 0x8000`; the register file at that instant reads `r4 = 0x04804000` (the slot base). The IPC trace is the other half: the ARM9 is *not* waiting on the wire (its `pc9 0x02030488` is the SDK idle loop, `bl scheduler; bl mcr p15,0,r0,c7,c0,4; b`, a cache drain), it is pushing **one word per command round** (~154/s against ~166 commands/s) from the single store at `0x02030660` with values stepping by `0x400` (`82D4500A`, `82D4900A`, ...), while the ARM7 pushes about three words per round back from `0x037FEA70` (`8DEB780A`, `C0240006`...). So the host's own MP scheduler is running and what it schedules is the dummy - the missing step is inside the host's Download Play code, and the previous pass's "the host is idling, waiting for a guest frame" reading is disproved by its own counters. The park reproduces in the interpreter too, so the JIT is not the cause (`logs/sfc_nojit.log.gz`, `logs/sfc_regs.log.gz`, `logs/sfc_ipc.log.gz`). A RAM diff across the Cut Off tap (two baseline dumps, then post-tap, over `0x020A0000..0x020C0000`) changes 788 words of which 652 are new, but the DP title records at `0x020B3D40`/`0x020B5640` rewrite themselves between the two baseline dumps as well, so no state variable could be separated from the churn - reported as a negative result rather than dressed up. **The gate is still not named**, which was the honest state of this entry in the host-patch era. That ending is **superseded** - v0.1.9cv opened the mode machine from the client side and v0.1.9cw carried the download to completion, both recorded in this row's Result column - and what follows here records the host-patch era rather than the current state. `cargo test --lib` **27 passed, 0 failed**; `md_smoke_test` **141 checks, 0 failures** (that era's numbers). **v0.1.9cz makes the download play, and the client now jumps into the transferred title.** The joiner used to fold each packet's payload into a byte counter and drop it, so the run ended at a log line; it now holds the image as one contiguous, packet-ordered buffer and, on completion, sends GBATEK's deauthentication (reason 3) and boots it - `NDS::SetupDownloadPlayBoot` / bridge `md_dp_boot` write the header/ARM9/ARM7 to the RSA frame's **actual** destinations (`10h`/`20h`/`30h`, plus the ARM7 temp staging copy at `2Ch` the ARM9 SHA1s), set the boot indicator at `27FFC40h` to `0002` ("booted from DS Download Play via Wifi"), reset both CPUs to the ARM Supervisor state a boot starts from, and jump to the RSA entrypoints. Measured two consoles, route exit 0, nothing set (`logs/v9sfc_dp_boot4.log`, `logs/v9sfc_dp_route4.log`): the transfer still completes (`2502 distinct packets, 1260340 of 1260340 bytes`), `dpjoiner` reads `BOOTED`, the bridge logs `booted the downloaded image (1260340 bytes) - header -> 027FFE00, ARM9 -> 02000000 (entry 02000850), ARM7 -> 02380000 (entry 02380000)`, and the client's ARM9 faults **zero** times and reaches the downloaded entry. Two live defects were found and fixed on the way (the first jump kept the game's Thumb mode and faulted at `0200081C`; the writes used the RSA frame's temp destinations and faulted the child at `02008BEC`). **v0.1.9da: the client now starts the downloaded title the way a real boot does, and stops landing in BIOS - but its screen still does not advance.** `SetupDownloadPlayBoot` was missing both ends of the emulator's own boot path (`md_load_rom` runs `Reset()` -> `SetupDirectBoot(rom)` -> `Start()`), so the child executed on top of the previous game's leftovers; that is what the *nondeterministic* faults said - the same image faulted a few hundred bytes into its entry at a different address every run (`02008BEC`, `02000A28`, `02008B98`) - while the image is provably self-consistent (the child's own header in RAM states ARM9 `0x10B324` + ARM7 `0x286B0` + header `0x160`, exactly the `1260340` bytes transferred). It now calls `NDS::Reset()` before the image is written, re-runs `SPI.GetFirmwareMem()->SetupDirectBoot()` afterwards (the firmware header and user settings live in main RAM, which the reset clears), copies the FreeBIOS ARM9 logo, and ends with `Start()`. **A second defect in v0.1.9cz's own handoff was found by reading the live log and fixed:** `step`'s booted gate sat *after* the receive block, which powers the console's radio on - so one frame after `booted the downloaded image` the joiner logged `listening for a download-play beacon` and re-attached the radio the child had just taken over. **Measured on the new path** (`logs/v9sfc_dp_boot7.log.gz`, route exit 0, nothing set): the transfer is unchanged (`2502 distinct packets, 1260340 of 1260340 bytes`), the client logs **zero** warns or errors for the whole run, and `probe --player 2` samples its ARM9 *inside the downloaded image* on 96-99 of 100 samples (`0201C7B4`..`02023F4C`) with the stack in the DTCM the child itself set up, the VBlank IRQ serviced, and an ARM7 running the game's own ARM7 code in shared WRAM. **Still open:** the picture digest is byte-identical over 20 s (`ef647ee79121ad33df5ec9337d8ee562`) while the PC moves on 98-99 of 100 samples. **v0.1.9db corrects that pass's reading of why: `dispcntA 00011108` has bits 16-17 = `01` (graphics display ON - only `11` is melonDS's display FIFO, `src/GPU.cpp:1084`), so the child was never in display-off mode and never dark.** What it is really showing, decoded glyph by glyph off the client's bottom screen (`logs/shots/sfc_wait_resume.png`), is the downloaded game's own fatal dialog: **`Communication error. / Please turn your DS off.`** (its top screen is 100% black - the 3D view never starts). The host's own bottom screen carries the same first line, so **both** games are in a network-error state, and the host's changing picture is its own animated dialog. The child's wireless init is what aborts: its radio comes up at frames 5306 and 5517 and is powered down within 4 and 1 emulated frames after it writes RF values melonDS calls `invalid channel values 0004C:0004F`, and it then never polls the receive window again (`link read_game 0`, `polls 0`) while both its CPUs stay busy (ARM9 sampled inside the 64-bit divide `0x200857C` from the tick-pacing loop `0x2023F28`; ARM7 running the game's own code, `pc7 037FEFDC`, 1,573,480 IPC words pushed). The three `unknown ARM9 IO write32 04001060/64/68` writes are ordinary boot behaviour on **both** consoles (SDK stub `0x01FF8028`), not Download-Play-specific. Two mechanisms are on the record and neither is fixed: the handoff deauthenticates (reason `0003`) and the host's game tears its own access point down (`USCOMPARECNT disabled ... sends no beacon of its own`), so the downloaded title has no host to join; and `Wifi::Reset()` leaves `CurChannel = 0`, which makes a console mute and deaf (`src/Wifi.cpp:1057`, `2738`) until the game writes a matching RF pair, which this client never does. `cargo test --lib` 37/0 (one new), `md_smoke_test` 149/0; v0.1.9db changed no code and re-ran neither. **v0.1.9dc runs that pass's own unbuilt instrument and names the exact missing step.** The `rfw:` RF-write trace in `Wifi::RFTransfer_Type3` (`src/Wifi.cpp`) printed 459 lines on a fresh two-console route (`logs/v9sfc_rfw2.log.gz`, route exit 0): the host and the booted child both write the firmware RF register block (ids `00..28`), and then the host scans channels (`4B:6C -> 1`, `4C:4A -> 7`, `4D:08 -> 13`) and settles on **channel 7**, while the child powers the radio off at the end of the block and never writes a channel pair at all (its only `id=01/id=02` writes are the block's `4C:4F`, five times, each `invalid channel values 0004C:0004F`). So `CurChannel` is never set and the downloaded title is mute/deaf (`src/Wifi.cpp:1057`/`1339`/`1523`/`2738`), and its parked state is the bring-up stopped: `pc7 0000115C` (ARM7 in the BIOS idle loop) beside `pc9 02008B60` (ARM9 near the downloaded entry). **The no-deauth handoff was also run live and the host still ends the session** - its own game deauthenticates the client (`reason 0001`) and then broadcasts `reason 0003` with `USCOMPARECNT disabled` ~5 s after the child's radio cycles - so that change alone does not save the download. The missing step is now single and named (the channel select); next is what the child's ARM7 waits on after the block load. `cargo test --lib` 37/0, `md_smoke_test` 149/0. **v0.1.9dd holds that channel on the child, and it is live and measured - and it is not enough, with the next blocker named from the trace.** `Wifi` gained one held channel (`SetDownloadPlayChannel`/`HeldChannel`, `src/Wifi.cpp`, `src/Wifi.h`): `ChangeChannel()` keeps it while the RF registers name no channel, and a child that selects a channel of its own replaces it (the fallback self-disarms; `Wifi::Reset()` clears it). The hand-off sets it once from the host's beacon (`st.host.channel`, threaded through `JoinerBus::handoff` -> `md_dp_boot(..., channel)` -> `DownloadPlayBootInfo.Channel` -> `NDS::SetupDownloadPlayBoot`). Live (`logs/v9dpchan.log.gz`, route exit 0, nothing set): the boot logs `wifi: rf registers 00000:00000 name no channel; holding channel 7` and `radio tuned to channel 7`, every later RF write reads `rf registers 0004C:0004F name no channel; holding channel 7`, and the child's `rfw:` trace shows `chan=7` where v0.1.9dc had `chan=0` - so the mute/deaf condition is gone. **The link still does not form, and the reason is located.** The child never arms its radio: one RF block per power-up then `WIFI: OFF`, twice, no TX slot ever armed (`cmds tx 0 / collected 0`), `game 0` frames delivered to it, its picture frozen ~350 frames after boot while its ARM9 runs (pc9 `0201C7FC`, `lr9 0201C7F8`, IPC 9sends 103 / 9recvs 29904) and its ARM7 alternates BIOS idle `0000115C` with its own WRAM `038006A0`. The host is not the blocker once the transfer ends (it went `04 -> 00`, kept beaconing `fc=0080` with `link mask 0x0`, then deauthed `0001`/`0003`). **The next blocker is in the joiner, not the radio:** `dp_joiner.rs` `step()` logs `the download is complete`, calls `handoff()` and `return`s *before* the send path, so the GotAllReply for that round is never sent; after it every host round reads `collected=0000` and the host's class census over the run is `00`/`01`/`03`/`04` only - **`05` Final never** - so the child boots into a Download Play transfer that never closed, whereas a real client boots after Final and continues the association (BSSID/AID/role) the firmware's client negotiated. `cargo test --lib` 37/0 (the hand-off test now also asserts the child keeps channel 7), `md_smoke_test` 149/0. **v0.1.9de answers the completion round - and the host still never sends `05` Final, so that naming was wrong and the child now never boots.** In `dp_joiner.rs` `step()`, the branch that logs `the download is complete` called `handoff()` and `return`ed *before* the send path, so its GotAllReply never left; it now only logs, the round is answered like any other, and a `final_replied` flag plus `image.complete()` gates the boot at the end of the associated phase. Live (`logs/v9dpfinal.log.gz`, route exit 0, nothing set): the host **collects** the completion's answer - `collected a reply from p2 ... body[14]=04 01 0A C6 09 C6 09 00 00 00` (`0A` GotAllReply, next/count 2502 against the RSA total) - where v0.1.9dd read `collected=0000` on every round after completion (this run: 78,332 rounds answered, 14,167 collected). **But the class census is still `00` 59,333 / `01` 36 / `03` 5 / `04` 5,980, no `05`**: the v0.1.9dd premise is falsified. What the host does instead (read live, no pokes): `*(0x020B3DBC)=023722E0`; client-1 mode `0x023737C8` = **7** (was 6, the download direction; sampled 4x over ~25s, stays 7); gate halfword `0x02373A34` = `0000 0005`; class stays `00` with slave 1 registered, 1,013 beacons, 0 deauths. The joiner reports `not booted`. So a `05` Final cannot be this title's boot trigger; the boot must be derived from the host's own completion state (mode 6 -> 7 / class `04` -> `00` after it collects the GotAllReply), and that transition must be tested for the deadlock reading (the host polls a registered client it never tears down - "I have finished sending, now you"). One real cost reported: with the boot no longer firing on completion the joiner keeps answering for as long as the host polls. `cargo test --lib` 37/0 (the hand-off test now asserts the completing round is answered with the GotAllReply and is *not* the boot trigger, and that the Final is confirmed before the hand-over), `md_smoke_test` 149/0. **v0.1.9df resolves the reading and rebuilds the trigger from what this host emits, and the child boots again.** The two readings were settled by measurement both ways - the answering arm of v0.1.9de stopped the host's `04` within ~2 rounds of the collected GotAllReply, while the silent arm of v0.1.9dd left it re-sending its last packet forever (`collected=0000` round after round) - so the host stops asking for data *because* it collected the completion answer ("I have finished sending, now you"), its class-00 Dummy polls are its own "nothing pending", and a boot gated on a `05` Final the host never sends deadlocks by construction. The trigger is now the host's own two statements: a CMD ACK that stopped reporting this slave missing while the image was complete, *and* its newest command no longer a `04` - neither alone boots. Maybe more importantly, the transfer became a named state machine (`Transfer::{Unstated, Streaming, Complete, Collected, Closed}`), advanced in one place, reset in one place (the deauth arm - the old flag conjunction had no reset, so a re-registered client could boot on the previous session's facts), logged once per transition, and readable off the control channel (`dpjoiner` -> `transfer`). Live: `logs/v9dptrig.log.gz` boots the 1260340 byte image at line 70659 with `holding channel 7`; `logs/v9dpstate3.log.gz` shows `Streaming` -> `Complete` -> `Collected` -> `Closed` then `booted the downloaded image ... channel 7`, with `transfer = "Closed ..."`, `booted = true` off the port. Transfer unchanged (2502 packets, 1260340 bytes, no `05` in the census). The child still does not link: it boots but never arms its radio (pc7 `0000115C` BIOS idle, `cmds tx 0`), the same bring-up stop v0.1.9dc/dd recorded. `cargo test --lib` 39/0, `md_smoke_test` 149/0. **v0.1.9dg carries the negotiated session across the boot as one named fact, live and exact - and it is shown insufficient, with the next blocking step named.** A real DS client does not merely boot after a Download Play transfer: it continues the association its firmware already negotiated (BSSID, AID, role, channel), and this fork's boot is a whole-console `NDS::Reset()` that clears every one of them. The session is now one value (`dp_joiner::Session { channel, bssid, aid }`), threaded through `JoinerBus::handoff` -> `md_dp_boot(..., MDDPSession)` -> `DownloadPlayBootInfo.HostBSSID`/`AID` -> a new `Wifi::SetDownloadPlaySession(channel, bssid, aid)`, which re-seeds the core's association outright (`IsMP`/`IsMPClient`/`MPD.role=2`/`MPMYAID`/`W_BSSID0..2`) alongside the v0.1.9dd held channel; `Reset()` clears all three holds. Live (`logs/v9dpsession.log.gz`, route exit 0, nothing set): line 70329 `wifi: download-play session carried across the boot: channel 7, bssid 00:09:BF:11:22:33, aid 1`, 70330 `coming up an MP client of 00:09:BF:11:22:33 as AID 1`, and the channel hold reads `holding channel 7` on every later RF write. **And the consoles still do not link.** ~6 minutes after boot, `probe --player 2` reads pc9 `0201CD34` / pc7 `0000115C` (BIOS idle) with `mp: cmds tx 0 acks None collected 0`; `status` reads `link mask 0x0` (the child is not on the bus); `wifiio` reads `role=2 (client)` but `handed start=0000 len=0`, USCOUNT 0, RX buffer all zero; the app's own line reads `health p2: LINK OFF ... slots 0x0 ... pwr 0200/0002` and `mp p2 [guest]: cmds rx 0 ... replies sent 0 collected 0`. The host's beacons still flow (1,731 beacon lines after the boot) but not one frame reaches the child (p2 rx frames = 0 for the whole run) and the child sends nothing. **Reading: the obstacle is the child's own ARM7 bring-up, not the session state** - the session is genuinely present and the host is still transmitting, yet the child's game powers the radio on and off twice more after boot (`WIFI: ON` x2), loads one RF block, never selects a channel or arms a TX slot, and leaves the ARM7 in the BIOS idle loop. **Named next blocking step:** what the booted child's ARM7 waits on after its RF block load - at `pc7 0000115C` beside `pc9 0201CD34` - that keeps it from scanning, attaching and arming a TX slot (its own DP client init waiting on an ARM9/ARM7 handshake or IRQ its boot never raises; the emulator attachment lifetime, which ends at the hand-off while the child's game never reaches `MP_Begin`; or its power sequence aborting before the channel select). `cargo test --lib` 39/0 (the hand-off test now also asserts `session.channel == 7`, `session.bssid == HOST`, `session.aid == 1`), `md_smoke_test` 149/0. **v0.1.9dh diffs the booted child's radio bring-up against a normal joining client's and isolates the skipped step with a captured trace - no code changed.** Both consoles were sampled live in one run (`logs/v9dgdiff.log.gz`, `logs/v9dgdiff_wifiio.txt`, route exit 0, nothing set): the host, a console running its own game in multiplayer, lives in the wifi IRQ/receive loop (`W_IF` 50,017 reads, `W_IE` 41,332, `W_USCount0` 18,618, `W_TXStat` 17,556, `W_CMDStat0` 11,341, `W_TXSlotLoc1/2/3` 17,555 each), while the booted child reads **none of them** - zero reads of every one - and instead reads only the baseband/RF bring-up (`W_BBBusy` 2,253, `W_BBRead` 1,018, `W_RFBusy` 123, `W_ModeWEP` 11, `W_Random`/`W_Preamble` 6 each, `W_MACAddr`/`W_BSSID`/`W_AIDFull` 5 each) with those totals **frozen** across two samples 12 s apart. Its read ring ends on a spin on the baseband busy flag (`15E:0000` x337 with the transceiver off, last `W_BBRead` `003F`). The sharpest fact: `TRX power ON` fires **x4 on the host and x0 on the child** - the child never asks its transceiver to come up at all, so there is no channel select, no beacon arm and no MP loop, while the host's *same* firmware RF block leads on to a scan (channels 1/7/13, settling on 7) and a beacon arm. **Two of the three candidates are settled:** (2) is falsified - the child does reach `MP_Begin` (`WIFI: ON` -> `wireless ON (begin #2, slots now 0x3)`, twice) - and (3) is refuted as stated (the sequence never requests power-on rather than aborting). What remains is (1) narrowed: the transition from baseband/RF init into the multiplayer loop in the child's own Download Play client driver, inside the downloaded game's code. A secondary risk is recorded, not claimed: the child reads no association register post-boot, so the trace cannot say whether the carried BSSID/AID survive its own init - and `W_ModeReset` bit 14 (`0x4000`) is a core branch that would zero exactly them plus `MPMYAID`. `cargo test --lib` 39/0, `md_smoke_test` 149/0 (re-run, unchanged tree). **v0.1.9dk settles that secondary `W_ModeReset` risk and locates the stop with the power-register trace live for the first time** (`TracePowerWrite` had never compiled before v0.1.9dj, so every earlier run's log has zero `[mp] power reg` lines; two fresh routes carry 479 each - `logs/v9carry.log.gz`, `logs/v9code.log.gz`, both exit 0 and `BOOTED`). The child writes `W_ModeReset` **zero** times in the whole run, so the bit-14 (`0x4000`) branch that zeroes `W_MACAddr0..2`/`W_BSSID0..2`/`W_AIDLow`/`W_AIDFull`/`MPMYAID` never fires on it - and it would not have mattered, because the child walks those registers through its own bus-test patterns and then writes its own MAC and `W_ModeWEP`, replacing the carried association itself. The host's ladder - `W_ModeWEP <- 004A`, `W_PowerForce <- 8001,0000`, `W_PowerTX <- 0003`, `W_ModeReset <- 0001`, `W_PowerDownCtrl <- 0000`, `W_PowerTX <- 0001`, `W_PowerState <- 0002`, `W_PowerForce <- 8000,0000`, `W_ModeReset <- 0000` - is exactly what raises `TRX power ON`; the child never writes `W_PowerState`/`W_PowerForce`/`W_PowerTX`/`W_PowerDownCtrl`/`W_ModeReset` at all, power-cycles its block three times (`wireless on/off 3/3`) and gives up, then its game shows its own `Communication error. / Please turn your DS off.` (zero reads of `W_IF`/`W_IE`/`W_USCount`/`W_TXStat`, RX buffer `0000/0000`, `USCOUNT=0`). A prior reading is also corrected: `pc7 0000115C / lr7 037F84FC` is not the child's retry loop - `0x037F852C` thunks into `0x038043E2`, Thumb `svc 6`, the first entry of the image's own one-instruction-per-SWI stub table, and the **host** sits in the same loop in the same run. The child is alive in its own main loop (pc9 moves through `0201C790`/`0201C7FC`/`0201CD34`/`02023F2E`, IPC `7sends 20647`), not faulting, and **no emulator defect is substantiated** (busy flags toggle, the baseband read path returns the fixed chip id `0x006D`, the channel hold and carried registers are present and written by the child), so no fix was made. Next: a read/store watch on the child's wifi block across its three power cycles. `cmake --build build-tauri -j8` exit 0, `cargo test --lib` 39/0, `md_smoke_test` 149/0, transfer unchanged (2502 packets / 1260340 bytes). `logs/v9carry_finding.txt` digests both runs. **v0.1.9dl then made the block PC-tagged** (`Wifi::RecordRegAccess` -> `appctl wifiio`), which corrected one reading and located the stop in the child's own code: the change-only `power reg` trace had hidden a write of a value a register already held, so the child's `W_ModeReset <- 0000` (`pc7 037F93F0`) and `W_PowerForce <- 0000` (`pc7 027EA4E8 lr7 037F94C4`) were on record after all - bit0 is still never raised. Disassembling the live-dumped child ARM7 shows its register table loader (`0x027E9A1C`, 25 pairs, one `W_ModeReset = 0`), its power state machine (`0x037F9468`: state 0 parks `W_BBPower <- 800D`/`W_PowerUS <- 1`; state 16 powers up via `0x037F93B0` + the wake `0x027EA3D4`), and `0x027EA66C` - the routine that would force the transceiver (`W_PowerForce <- 8001`, poll `W_PowerState >> 8 == 2`) - which the child never runs. It runs state 16, then state 0, then its ARM7 idles (`pc7 0000115C`) with the register history frozen and `TRX power ON` 0 against the host's 8. The trace carries every read with its PC and the child reads **no power register at all**, so no emulated value is behind the stop. The one candidate - carrying `W_ModeReset` bit0 across the boot - was built and run: the child's own table loader writes it back to 0, `TRX power ON` still never fires, the screen digest does not move; reverted. `md_smoke_test` 152/0 (two new section checks). `logs/v9reg_finding.txt` digests the pass. **v0.1.9dm watched the child's ARM9 instead, and demotes "the stop is in the child's own MP bring-up" to a lead.** New `pctrace` records the first visit to each PC in a window for one CPU, in order, with LR and frame; under the JIT (the real config) the booted child records 8303 distinct block entries across 165 code regions and then stops growing, and sixteen pc9 samples over 33 s cycle through `0201C7FC`, `0201C790`, `0201CD34`, `02023F2E` with `mp cmds tx 0` - a steady in-image loop, not a single give-up branch. One site is named from the child's own image: `0x0201CA70` reads the DP boot indicator `0x027FFC40` and, when it reads 2 (exactly what `SetupDownloadPlayBoot` writes), calls an IRQ-off self-loop at `0x0201CD34`; the JIT's block-address `R[15]` localizes the child to that loop but does not prove it spins there, and `MD_NOJIT=1` (a confound) parks the child at a different address (`02008B60`, inside the 64-bit divide `0x02008B40..0x02008B7C`). The record's `W_BBBusy`-hard-wired-to-0 claim is corrected (`src/Wifi.cpp:4047`/`:4440`). `md_smoke_test` 159/0. `logs/v9arm_finding.txt` digests the pass. **v0.1.9dn makes that trace truthful in the shipped JIT config and disproves the site it named.** The recorded PC is the address of the instruction about to run in every mode (`instrAddr = R[15] - ((CPSR & 0x20) ? 2 : 4)`, the same value the JIT dispatches on), so under the JIT it records whole basic-block entries; what was untrustworthy is `probe`'s *raw* `R[15]`, which is 4 above the running instruction, so the previous pass named code 4 bytes off. Arming is race-free (request published from the control thread, applied by the CPU thread at the top of `RunFrame`; fixed-size arrays only). Measured in the run that ships (JIT on, `logs/v9jit2.log.gz`, route exit 0, `BOOTED`, transfer byte-identical 2502 packets / 1260340 bytes, 0 warns): child `0x027FFC40 = 2` (host `= 1`), so the test at `0x0201CA70` would divert - but tight windows over 366 frames each record **0 visits** to `[0x0201CD34,0x0201CD48)` and `[0x0201CA70,0x0201CA8C)`, so the child's ARM9 never executes either after the boot. Its real steady cycle is `0x0201C780`->`0x0201C78C`->`0x0201C7F8` (a pacing loop) plus the `0x0201C9D8`/`0x0201C9EC` IRQ helpers, with IRQs serviced (`IE=2009 IME=0001 IF=0000`); the old raw `pc9 0x0201CD34` is block entry `0x0201CD30`, the `bx lr` return from the cache-drain helper `0x0201CD28`. The stop is not this ARM9 site; ARM7 stays in BIOS idle and `mp cmds tx 0`. `md_smoke_test` 159/0, `cargo test --lib` 39/0; `logs/v9jit2_finding.txt` digests the pass. **v0.1.9dp: the prescribed fix for that dead receive path is falsified by measurement, and the bring-up stop is one branch wide.** `Wifi::CheckRX` (`src/Wifi.cpp:2756`) refuses every frame while `W_RXBufBegin == W_RXBufEnd` (and `W_RXCnt` bit 15 clear of RX-off), and that is faithful - hardware cannot latch into an unconfigured window either - but the booted child's window reads `begin=0000 end=0000` in **26 of 26 samples spanning before as well as after its boot** (`role=0` pre-boot, `role=2 (client)` post-boot, `logs/audit9_p2_regs.txt`), against the running host's live `role=1 · USCOUNT=3510FC8 · USCOMPARE=2383800 · zone gen=5471 · handed start=1638 len=72 fc=0080`. So the console that received the 1,260,340-byte download was never this console (the app's joiner took it at the bus level; the child's own ledger reads `game 0 drain 14314` all run): there is no pre-boot configuration to carry across the boot, a bus re-attach cannot re-arm what the game never armed, and the `delivered`-climbing-with-`game 0` shape is expected rather than the defect. Both consoles run the same sequence from the same PCs and the same values up to `pc7=027EA55C` (`W_ModeWEP <- 004A`, after the `003F/1A/25/34/00` ramp, `W_PowerUS 0000`, `W_PowerTX 0002`, `W_RFCnt 0114`, `W_ModeWEP 0002/000A`, `W_BBPower 800D`, byte-identical RF read sweeps); the host then enters the transceiver-on sequence (`W_PowerForce <- 8001`, `W_PowerForce <- 0000`, `W_PowerTX <- 0003`, `W_ModeReset <- 0001`) and its game is handed a frame through `CheckRX`, while the child writes **neither `W_PowerForce` nor `W_ModeReset` at any point**, ends on `W_BBPower <- 800D` / `W_PowerUS <- 0001` (block off), parks in BIOS idle (`pc7 0000115C`, `lr7 037F84FC`) and raises no wifi interrupt at all (`raised none · W_IF 0000 W_IE 0000 · bcn 0 0 · uscnt 0`). No code changed this pass; the branch between `pc7=027EA55C` and `pc7=027EA6AC` is the next thing to read (the `0x038043E3` thunk's return in the ARM7 retry loop, and the ARM9's `0x02087350` callee). Cold build exit 0, route exit 0 with `TRANSFER STARTED` and the child booted, all five `Test Roms/*.sav` byte-identical, app exit status 0, no strays; `logs/audit9_finding.txt` digests the measurement. **v0.1.9dq: the fork's client side is a Rust stand-in for the console's own radio, and arming the console's block from outside is measured insufficient.** Upstream melonDS has no Download Play path at all - the only related line in `upstream/master` (906e9ebb) is the *direct* boot indicator (`upstream:src/NDS.cpp:329`, `0x027FFC40 <- 0001`), and `Wifi::CheckRX` plus its `MP_RecvPacket`/`MP_RecvHostPacket` receive path are upstream's own (`upstream:src/Wifi.cpp:1564/1587/1593`); the fork's own additions are `src-tauri/src/dp_joiner.rs`, `NDS::SetupDownloadPlayBoot` (`src/NDS.cpp:439`) and `Wifi::SetDownloadPlaySession` (`src/Wifi.cpp:3340`), which together implement the *client* outside the console. The same run shows both sides: the host, whose own game brought its radio up, has `W_RXCnt 8000`, `W_PowerUS 0000`, window `50C4/5F60`, wifi clock `23D5D98`, `bcn 204 8`, `cpu line 20682` and frames handed to its game, while the child reads `W_RXCnt 0000`, `W_PowerUS 0001` (off), window `0000/0000`, `us 0/0`, `bcn 0 0`, `cpu line 0`, `W_IF 0000`. Poking the host's own block into the child's registers took (enable and window visible, `LINK OFF` -> `LINK IDLE`) and changed nothing in the ledger - `game 0` for 60 s, `rx 14358 drain 14358 handed len=0` - because `CheckRX`'s first guard is `W_PowerState` bit 9 (`src/Wifi.cpp:2758`), a guest-driven power state the child's driver never clears (the host writes `W_PowerState <- 0002`; the child never writes it, nor `W_PowerForce`, nor `W_ModeReset`, and parks with pc7 `0000115C`). **So the client side has to be the console's own guest code**: run the child console into its own Download Play path (the firmware applet or the game's own DP client) rather than autostarting a cartridge and having `dp_joiner.rs` speak for its radio; then `md_dp_boot`/`SetupDownloadPlayBoot`/`SetDownloadPlaySession` become unnecessary. That replacement is a real guest DP client and is not half-landed. Cold build exit 0, route exit 0 with the transfer complete and the child booted, five saves byte-identical, no strays; `logs/audit10_finding.txt` digests the comparison. |
| Super Mario 64 DS (USA) (Rev 1) | 2 | 🟡 transfer completes and menus route to CONTINUE, no battle yet | VS mode **is** single-card Download Play, and the title is now registered (`ASME`, `download_play`), so the joiner runs for it. With the new route (`tools/scripts/sm64-dp-host.sh`) the host screen is reached by touch geometry alone - the game's font defeats the ASCII readers, so the coordinates were measured off framebuffer captures (title screen, then the **VS** bar at about `(37,172)`; do not tap the bottom bar, which is the host's own "TOUCH TO START" and asks "Practice alone?") - and the joiner hears the beacon, authenticates, associates (`status 0000, AID C001`) and the host runs `NameRequest -> 00 -> RSA -> 00`, stating a **514896-byte** image. Then it stops addressing the client: 52,480 of its transmitted command frames are the 6-byte-body, no-client form (len 46) that `classify` refuses to read as a command, against 2,536 it reads, so `download: 0 of 514896 bytes` and never a `04` - and the reply-type overrides could not reach that round on the build the sweep ran on, because the `00 if !saw_rsa` arm of `answer_reply` matches before the forced arm (a symptom of the batching defect below, not a permanent property). **Driving it found and fixed a real client defect** (an RSA arriving in the same drain batch as the round after it was *read* but never *remembered*, so every later round was answered as a Username reply instead of the RsaReply); Star Fox's complete transfer re-runs on the fixed binary. **v0.1.9cx names why it stops addressing the client, and it is not the reply.** Re-run on the fixed binary, the same route still parks (`logs/v9sm64b.log.gz`): the host sends `NameRequest -> 00 -> RSA -> 00` to slave 1 and then opens a reply window on **2,535 addressed rounds** of which **31 are collected, 2,522 are empty and 278 reads are voided as stale**, its own CMD ACK reporting this client missing on 2,522 of them (`clientfail 2522`, `collected 31`), and after ~2,424 addressed rounds it evicts the client to the 6-byte-body no-client form (`60,055` frames) - `download: 0 of 514896 bytes`, never a `04`. **The reply's contents are not the lever and never were:** the five-way `MD_DP_REPLY_FORCE` sweep (`09`, `0a`, `0b`, `07`, `00`; `logs/sm64_force_sweep.txt`) parks on every value, `MD_DP_REPLY_FORCE=09` with the window trace shows the host *collecting* 21 type-`09` replies and still dropping the client, and the host collects 23 RsaReplies (type `08`) before it ever evicts - so no type, and no body, moves it. **The lever is when the answers arrive, and that is a client/transport deficit, not a host branch.** Each command round is answered from a drain of the console's inbox, and a drain and a send are one round trip each to the console's driver thread (`service_link_ops` runs once per emulated frame) - so the client answers about **one round per emulated frame while the host asks ~8.6** (2,424 addressed rounds over a ~4.9 s emulated span). Answering *every* round of the batch through a batched send was implemented, built and measured this pass (`logs/v9sm64b.log.gz`): the answers do reach the host (`reads 147 -> 309`, `reanchor-dropped` from ~0 to 2,367 as the host's re-anchor trims the surplus) but the round its window reads is still its own - `collected 30 -> 31`, and **278 of the 309 reads are voided as older than the transport's two-frame window**, so the change was reverted rather than kept as unproven churn. `MD_REPLY_BUDGET_FRAMES=20` is worse still (`collected 0`, `logs/v9sm64c.log.gz`). So the host is behaving correctly for a client it cannot hear, and the way it hears a client is the emulator's reply window, which is already spent (2,204 of the rounds open with `0us of slack`) by the time each round arrives. **Next input:** the answer has to be *already in the host's queue* when its window opens, i.e. the client has to answer ahead of the round rather than once per frame behind it - a drain/send path that does not cost a frame, or a reply window that waits for answers already on their way. **Nothing was poked:** no value of the reply moved this host, so there is no single host branch to hold - the earlier verdict's shape does not hold here. **No regression:** Star Fox's transfer was re-run end to end on the rebuilt binary (`logs/v9sfc.log.gz`, 2,502 distinct packets, 1,260,340 of 1,260,340 bytes). **v0.1.9cy fixes the delivery path, and the host is then heard on every round - and still does not start the data loop, which is the one remaining fact.** The joiner's turn now runs *inside the console's own driver turn*: `JoinerBus::turn` is the single entry point (the run loop owns only *when*), a real console overrides it with one `LinkOp::JoinerTurn` that drains the inbox, advances the client state machine and sends every answer in that same turn, and the whole batch of a drain is answered because those sends are direct calls inside the turn rather than one driver round trip each (`DriverTurnBus`). Effect, same no-override no-poke route (`logs/v9sm64d.log.gz`): **`stale` goes 278 -> 0, `collected` 31 -> 1711, and the eviction disappears entirely - zero 6-byte-body no-client frames where the previous build had 60,055**; the host keeps addressing slave 1 (`slave flags 0002`) for the whole run instead of dropping the client after ~2,424 rounds. The remaining starvation is the transport's retention, and it is now measured rather than guessed: with `MD_REPLY_KEEP=16` (a frame's worth of the ~8.6 rounds this host asks per frame; the shipped default is 1) the host collects **26,968 of 26,977 rounds, `empty 9`, `clientfail 9`, `stale 0`** (`logs/v9sm64e.log.gz`) - the client is heard on essentially every round. **And the host still stays in `00 (Dummy)` at RsaDone forever: no `04`, `download: 0 of 514896 bytes`.** With collection at ~100% the reply-type sweep is finally meaningful, and it now says the same thing with a healthy path as it did with a starved one: `MD_DP_REPLY_FORCE=00` and `=09` each park with the client collected on ~15,700 of ~15,800 rounds, `stale 0`, no deauth (`logs/v9sm64f00.log.gz`, `logs/v9sm64f09.log.gz`). So the reply path, the reply type, the reply body and the reply's freshness are all ruled out **by measurement on a client the host can hear**, and the gate that starts the download lives in the host's own engine and is not reachable from anything this client sends. The frame-sized retention is the transport's next lever and is deliberately **not** made the default here: it is a cross-title change (MKDS/NSMB/MPH answer without any joiner) that this pass did not re-validate, and on this title it does not by itself start the download. **No regression:** Star Fox re-ran end to end on the restructured binary (`logs/v9sfc2.log.gz`: `#0`, 352 bytes, `"STARFOX "`, then `2502 distinct packets, 1260340 of the 1260340 bytes`). **2026-10-14: the v0.1.9cy verdict is overturned — the download gate was a host-side tap, and the transfer now completes.** Tapping the waiting room's TOUCH TO START bar DS `(128,168)` with slave 1 registered moves the host `00 -> 04` and streams `514896 of 514896 bytes in 1023 distinct packets · BOOTED` (`logs/sm64_app.log`, `logs/sm64b_app.log`, `logs/sm64d_app.log`; 3/3 transfers, script `tools/scripts/sm64-dp-host.sh` exit 0 — it needs `SETTLE_AFTER_ASSOC_S=15` because a same-second START tap is swallowed while the host's roster fills). Post-boot menu route mapped by digest-gated taps (`tools/scripts/sm64-dp-launch.sh`): 4-panel character picks P1 `(34,68)` / P2 `(98,68)`, D-pad stage cycle, guest headline tap `(128,60)` after ~60 s settle to the hub (known digest `fbdc62…`), hub boxes OCR-named `CONTINUE` (128,72) vs `QUIT VS` (128,120). Three sessions tapped QUIT and tore down identically (2-button YES=LEFT/NO=RIGHT digest-proven; converged session-over notice; radios off host-first, no deauth; guest ends on the firmware's `DS Download Play Failed!` dialog, OCR-verified `logs/shots/sm64c_end.png`). CONTINUE into the battle is mapped but reached only in runs that then lost the attach lottery — guest attach is 2/4 on this title (runs 4–5: rx frozen 1.5–2.7k, host TX stops ~30 s post-boot, the p65–p67 shape), so no battle has started yet. The launch route now gates the attach (guest rx climbing) and the hub (digest) and refuses untapped on a miss (run 5: exit 1 in 100 s, zero taps). |
| Tetris DS (USA) | 2 | ✅ **playable over single-card Download Play** | First Download Play title to reach real gameplay: the guest boots the downloaded image, joins the host's STANDARD lobby as player 2, and both consoles play a live versus match (distinct boards, mask 0x3). See the Tetris DS section |
| LEGO Star Wars II - The Original Trilogy (USA) (Rev 1) | — | ⬜ none | The game has no multiplayer mode to link |
| 0595 - Club House Games (U)(WRG) | 1 | 🟡 boots, multiplayer untested | Title screen renders ("Balance", touch prompt); no menu route driven, no link attempted |
| Bomberman (USA) | 2 | 🟡 transfer completes 2/2, guest errors post-boot, no battle | Registered (`ABME`, `download_play`); Battle leads straight to a Download host lobby, the guest associates (`AID C001`), the roster lists both players, host Start streams `672764/672764 bytes in 2693 packets · BOOTED` — then both consoles show `Communication error`: the booted guest game never gets on the radio (`answered 0`, stand-in covers), the host's `06` rounds go unanswered and it quits. Systematic (2/2 identical), not the attach lottery. See the Bomberman section |
| Diddy Kong Racing DS (USA) | 1 | 🟡 boots, multiplayer untested | Title/start menu renders; no route driven |
| Kirby Super Star Ultra (USA) | 1 | 🟡 boots, multiplayer untested | Intro cutscene plays; no route driven |
| Mario Hoops 3 on 3 (USA) | 1 | 🟡 boots, multiplayer untested | Title menu renders (EXHIBITION etc.); no route driven |
| Mario Party DS (USA) (Rev 2) | 1 | 🟡 boots, multiplayer untested | Story intro plays; no route driven |
| Meteos (USA) | 1 | 🟡 boots, multiplayer untested | Main menu renders with a WIRELESS button (bottom-left ≈ `(60,108)`); 2-console session constructs with the joiner correctly disarmed (`logs/dpnew_Meteos_USA.log`). Submenu unopened |
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

---

## Bomberman (USA) — 🟡 Download Play transfer completes, guest errors post-boot, 2 players

Route (P1 hosts, console 2 synthesized client): START past the attract battle → title `TOUCH TO START` `(128,150)` → Battle `(190,98)` → the Download host lobby (`Starting a Battle Game! Waiting for players to enter!`, then `Please start the download once all players are together!`). The joiner hears the beacon (serial `00400022`, empty SSID, ch 7), auth/assoc `0000`/`C001`, roster lists `melonDS` twice; host Start streams `672764 of 672764 bytes in 2693 distinct packets · BOOTED` (`logs/bomb2_app.log`, `logs/bomb3_app.log`, shots `logs/shots/bomb{battle,2_lobby,booted,3_boot}.png`).

Post-boot, both consoles show the game's own `Communication error. Press A Button to turn the power OFF and try again.` Root-caused 2026-10-14 with a fix that fires where there is material: the stand-in drained the same queue the game's CheckRX reads, starving scan-first guests — fixed by handing the join/scan set back post-boot (`LocalMP::RequeuePacket`, `dpjoiner` `requeued` line; to-do.md). Live on the fixed binary (`logs/bomb6_app.log`, 3rd BOOTED transfer, answers flowing): `requeued 0` because the host stops beaconing post-boot (battle-sync commands only), so the guest must direct-assoc off the staged parameters like Tetris's does — and doesn't. Remaining gap is per-image bring-up (staged RAM context is carried; wifi regs come up Reset-cleared), not wire behavior. Clean quits (exit 0), no `.sav` written.

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

**Update 2026-10-14: this milestone has landed for three titles.** The synthesized joiner (`src-tauri/src/dp_joiner.rs`, armed by the registry's `download_play` flag) completes Star Fox Command into a 2-player dogfight, Super Mario 64 DS's transfer 3/3 (no battle yet), and Tetris DS into a live versus match (see its section). Download Play
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

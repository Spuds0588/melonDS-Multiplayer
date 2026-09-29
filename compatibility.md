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

- Last updated: 2026-09-29 (v0.1.9aj)
- Legend: ✅ playable · 🟡 partial (link or lobby, no match) · ⛔ blocked (game
  design, not the emulator) · ⬜ no multiplayer mode at all
- Scope: **multi-card local play** (both consoles have the ROM). Single-card
  *Download Play* is a separate product question — see
  [Download Play is unsupported](#download-play-single-card-is-unsupported).

---

## Summary

| ROM | Players | Result | Where it stops |
| --- | --- | --- | --- |
| New Super Mario Bros. (USA) | 2 | ✅ **playable** | Nothing blocking — race reached, input coupled into the peer's picture. No race has been run to a *result* yet |
| 0168 - Mario Kart DS (U)(SCZ) | 2 | 🟡 partial | Both games reach their own park screen after VS; input is ignored by the game there |
| Metroid Prime - Hunters (USA) (Rev 1) | 2 | 🟡 partial | Both consoles sit in the same 2-player lobby; no hunter chosen, no match started |
| Star Fox Command (USA) | 2 | ⛔ blocked | The only local mode is Download Play; a multi-card join does not exist in the cartridge |
| Super Mario 64 DS (USA) (Rev 1) | 2 | ⛔ blocked | VS mode is Download Play only |
| LEGO Star Wars II - The Original Trilogy (USA) (Rev 1) | — | ⬜ none | The game has no multiplayer mode to link |
| `tools/mkdiagrom/diag.nds` (test ROM, not a game) | 2–4 | ✅ | — (the link self-test the build is verified against) |

**3 and 4 players: no title has been made to work yet.** Two consoles is the
only configuration with a working game. The blocker is named and has a single
leading suspect — see [Three and four players](#three-and-four-players-are-not-working-yet)
and [to-do.md](to-do.md#the-road-to-a-working-2-3-and-4-player-link).

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

## 0168 - Mario Kart DS (U)(SCZ) — 🟡 partial, 2 players

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

**Unknown:** whether a different pre-VS route reaches the actual race (the park
is the same one the game's own single-console flow shows), and whether a real
race would then stay in sync.

---

## Metroid Prime - Hunters (USA) (Rev 1) — 🟡 partial, 2 players

Both consoles reach the same two-player lobby with a complete, byte-exact
exchange. The match never starts.

**What lands** (`logs/rom_mph_join.log`, history.md v0.1.9ad): host
`cmds tx=7986 acks=7986 collected=7986 empty=0`; guest `cmds rx=7986
answers=7986` (blank 0, canned 0), `replies sent=7986`; `slots 0x003`,
`deauth out/in 0/0`; host sent `17219 (1,127,344 bytes)` with the guest receiving
the same, and the guest sent `7988 (399,388 bytes)` with the host receiving the
same — byte-complementary both ways. Last command `seq=3410` at +585.01s, both
consoles on arm9 `pc=0x02088ba4`.

**The tap that joins is `JOIN GAME (123,72)`**, not the list row; the two
consoles' stored frames step apart at frame 27000→27250, which is that tap. The
title's `TOUCH TO START` sits behind a cycling attract loop, so it takes a
bounded repeating tap, not a tap at a fixed frame.

**Where it stops:** both consoles list two players and the guest sits on
`SELECT HUNTER` for the 117 emulated seconds after the join — no hunter is
chosen and no game is started (`game-captures/mph-lobby/sheet.html`).

**Unknown / next:** the guest's hunter portrait and the host's start, both on
the lobby's bottom screen. Driving this through the **app** is unfinished: the
app's console 1 is on the multiplayer screen (its top screen is ~95%
block-identical to `game-captures/mph-mp1` frame 20800) but on a different phase
of the attract loop than the harness run on the same save, so frame-timed taps
miss it, and blind `touch 128 96` taps (16 of them) produced no screen change at
all.

---

## Star Fox Command (USA) — ⛔ blocked by single-card design, 2 players

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
see [Download Play is unsupported](#download-play-single-card-is-unsupported).

This title never wrote a `.sav` in the folder.

---

## Super Mario 64 DS (USA) (Rev 1) — ⛔ blocked by single-card design, 2 players

VS mode is host plus Download Play clients only; there is no multi-card
multiplayer. Measured at the bus level (history.md v0.1.4), blocked by the same
Download Play restriction as Star Fox Command. Not re-attempted since, because
the blocker is a product decision rather than a route.

---

## LEGO Star Wars II - The Original Trilogy (USA) (Rev 1) — ⬜ no multiplayer mode

Run on two consoles for 18,000 frames each with the menus walked end to end
(`logs/rom_lego_harness.log`, `game-captures/lego-set2/sheet.html`): the only
entries anywhere are **New Game**, **Load Game** and **Settings**. There is no
link entry, no wireless screen and no local option to enter. It cannot be
counted against the emulator — the retail game has no multiplayer mode.

---

## `tools/mkdiagrom/diag.nds` — ✅ the link self-test (not a game)

The ROM the build is verified against, not a title. `md_smoke_test` runs **66
checks, 0 failures** against it (per-console framebuffer checksums, per-console
MACs, the touch probe's ADC = pixel × 16), and the same ROM carries the frame
sampler used to measure input coupling (96 samples/s, 171 frame-pairs in 3 s).
If this ROM ever fails, the link itself is broken and every row above is stale.

---

## Download Play (single-card) is unsupported

Several DS titles offer a mode twice — once multi-card (both consoles own the
ROM, which is what works here) and once over **DS Download Play** (host owns the
ROM and the joiner downloads it over the air). New Super Mario Bros. is entered
through its **Multi-card** entry for exactly this reason. Two titles in the
folder, however, have *no* multi-card alternative at all:

- Super Mario 64 DS (VS mode)
- Star Fox Command (its only local mode)

On real hardware the joining console is running the **firmware menu's Download
Play client**, and the emulator's built-in FreeBIOS cannot boot the firmware
menu (`NeedsDirectBoot`). Supporting this means a **user-supplied BIOS +
firmware image**, which is a product decision, not a bug fix: it is the
difference between "the multi-card games" and "most DS multiplayer games". The
bridge already has the two pieces required (`md_set_bios`, `md_set_firmware`).

---

## Three and four players are not working yet

The bus itself scales — a 12/12 mesh self-test passes, and four linked 3D
consoles run at **~2.2× realtime** on the dev laptop (1 console ~6×, 2 ~2.4–4.9×,
3 ~2.9–3.4×, 4 ~2.2×), so frame budget is not the limit.

What breaks is the **second** join into an already-linked group (history.md
v0.1.4):

- **MKDS, three consoles, one joiner: works.** The host shows
  `CURRENT RACERS 2/8` with two entries; the joiner shows "Awaiting other
  racers"; the third console sees the host's group as `NORMAL melonDS`.
- **MKDS, three consoles, two joiners: both joiners get "An error has
  occurred."** Staggering the joins by ~18 emulated seconds changes nothing. The
  sequence is diagnostic: the first joiner was *in* the group at frame 2500 and
  only errored once the third console tapped to join at 3000 — the group was
  healthy, then the second join broke it, including the player who had already
  joined.

**Leading suspect** (not yet proven): `src/net/LocalMP.cpp` keeps "who is the
host" in one machine-global record (`MPStatus.MPHostinst`, and `LastHostID` on
the receive side) while every other piece of link state is per-instance
(`PacketReadOffset[]`, `ReplyReadOffset[]`, `SemPool[16 + inst]`). Two consoles
that both send commands overwrite `MPHostinst`, so the earlier sender's replies
are posted to the later one's semaphore and it never wakes up. That predicts
both the single-joiner success and the two-joiner failure; the experiment that
would prove it — make the routing per-instance, then re-run three and four
consoles and re-run the two-console games as a regression — has not been run.
See [to-do.md](to-do.md#the-road-to-a-working-2-3-and-4-player-link).

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

- **"The peer's world changed" is still inferred from pixels.** The app has no
  memory-read command, so no row above can quote a game's own RAM value for
  "player 2 exists" or "the race started". Adding that command is the single
  highest-value instrument improvement named in [to-do.md](to-do.md).
- **A link that is alive is graded as ⛔/🟡 if the game will not start a match.**
  Do not read `LINK ACTIVE` as playable.
- **Saves.** All five `Test Roms/*.sav` files are checked byte-identical to
  their recorded md5s after every session (`tools/run_app.sh` pins them and
  announces any write). Recorded md5s: NSMB `bdbb2dfe557b30b4fda1a7dc7d28dce0`,
  MKDS `5e180fda265b966f94d0306f2a0da106`, LEGO `5af705e8e8abee430e6be46c2e626070`,
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
   client; the latter cannot work today.
5. Add the row to the summary table above and a section with its evidence, then
   add the title to [to-do.md](to-do.md)'s coverage notes.

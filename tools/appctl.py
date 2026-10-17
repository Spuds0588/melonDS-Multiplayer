#!/usr/bin/env python3
"""Drive the running melonDS Multiplayer app from a shell.

The app exposes a loopback control channel when it is started with
MD_CONTROL_PORT set (tools/run_app.sh does that by default):

    {"cmd": "see", "player": 1, "screen": 0, "cols": 128}

and answers one JSON object per line. This client turns that into commands a
person or an agent can actually use: press buttons, inject game input, select a
console, click UI, read the emulated screens as text, and save screenshots.

    tools/appctl.py status                 # is a session running, frame counters
    tools/appctl.py ui                     # what the window says right now
    tools/appctl.py see                    # both DS screens as ASCII
    tools/appctl.py see --player 2 --screen bottom --cols 96
    tools/appctl.py see --crop 0 8 256 16 --cols 256 --aspect 1   # read one menu line
    tools/appctl.py shot --name lobby      # logs/shots/lobby.png
    tools/appctl.py render logs/shots/lobby.png --cols 120   # no app needed
    tools/appctl.py press a --player 1 --ms 120
    tools/appctl.py touch 128 95 --ms 150
    tools/appctl.py index 2 && tools/appctl.py tap KeyA
    tools/appctl.py script tools/scripts/example.txt

A route that has to wait for the *game* can wait on the picture instead of on a
frame count, which is what every earlier route in this project guessed at:

    h=$(tools/appctl.py digest --player 2)     # before the tap
    tools/appctl.py touch 122 64 --player 2
    tools/appctl.py waitfor --change "$h" --player 2   # did it land, and when
    tools/appctl.py waitfor --settle --player 1        # wait out the transition

Button names (see MD_BTN_* in src-tauri/src/lib.rs):
    a b select start right left up down r l x y     (compose with '+': up+a)
    or a raw mask in hex: 0x1

Screen reader notes: the ASCII renderer inverts the luminance ramp because DS
menus are dark text on bright panels; --crop zooms into one region when a menu
line needs to be read exactly.

`render` is the one subcommand that reads a file and nothing else, so it runs
with no app and no control port (a run's own `--shot-dir` PNGs outlive it).
"""

import argparse
import json
import os
import shlex
import socket
import sys
import time

DEFAULT_PORT = int(os.environ.get("MD_CONTROL_PORT") or 7373)

BUTTONS = {
    "a": 1 << 0,
    "b": 1 << 1,
    "select": 1 << 2,
    "start": 1 << 3,
    "right": 1 << 4,
    "left": 1 << 5,
    "up": 1 << 6,
    "down": 1 << 7,
    "r": 1 << 8,
    "l": 1 << 9,
    "x": 1 << 10,
    "y": 1 << 11,
}

RAMP = " .:-=+*#%@"

# The DS frame rate, and what the app paces a session to. Anything that waits
# for emulated time can size its wait from this instead of guessing.
DS_FPS = 59.826


class Channel:
    """One request line, one response line, over loopback TCP."""

    def __init__(self, port, timeout=60.0):
        try:
            self.sock = socket.create_connection(("127.0.0.1", port), timeout=timeout)
        except OSError as err:
            raise SystemExit(
                f"cannot reach the control channel on 127.0.0.1:{port} ({err}).\n"
                "Start the app with MD_CONTROL_PORT set to that port - tools/run_app.sh\n"
                "does it by default, and logs 'control channel listening on ...' when it is up."
            )
        self.sock.settimeout(timeout)
        self.reader = self.sock.makefile("rb")
        self.writer = self.sock.makefile("wb")
        self.next_id = 1

    # Read-only commands that may be retried once when the webview goes silent.
    # A reload of the window (`eval location.reload()`, or a user pressing reload)
    # leaves the Rust side running - the session survives it - but the webview
    # stops answering for a few seconds, and every webview read then fails with
    # "did not answer within 20s". Observed 2026-09-28 mid-session: the reload's
    # own eval timed out and aborted the whole command that ran it, while the
    # same reads answered normally seconds later. Retrying is safe for these -
    # nothing changes state - and would not be for click/press/touch/key, which
    # is why the set is explicit rather than "everything".
    SILENT_RETRY = {"ui", "see", "shot", "text", "bands"}

    def call(self, **req):
        for attempt in (1, 2):
            try:
                return self._call_once(**req)
            except SystemExit as err:
                silent = "did not answer within" in str(err)
                if attempt == 2 or req.get("cmd") not in self.SILENT_RETRY or not silent:
                    if silent and req.get("cmd") not in self.SILENT_RETRY:
                        raise SystemExit(
                            f"{err} - if the window was just reloaded, wait a few seconds "
                            "and retry; the session keeps running"
                        )
                    raise
                time.sleep(3.0)

    def _call_once(self, **req):
        req["id"] = self.next_id
        self.next_id += 1
        self.writer.write((json.dumps(req) + "\n").encode())
        self.writer.flush()
        line = self.reader.readline()
        if not line:
            raise SystemExit("the app closed the control connection (did it exit?)")
        resp = json.loads(line)
        if not resp.get("ok"):
            raise SystemExit(f"{req.get('cmd')}: {resp.get('error', 'unknown error')}")
        return resp.get("data")

    def close(self):
        for f in (self.writer, self.reader):
            try:
                f.close()
            except OSError:
                pass
        self.sock.close()


def parse_buttons(text):
    parts = [p.strip() for p in text.replace(",", "+").split("+") if p.strip()]
    if not parts:
        raise SystemExit("no buttons given")
    mask = 0
    for p in parts:
        low = p.lower()
        if low in BUTTONS:
            mask |= BUTTONS[low]
        elif low.startswith("0x") or low.isdigit():
            try:
                mask |= int(low, 0)
            except ValueError:
                # `press 0xZZZZ` used to die in int() with a traceback, which is
                # a stack trace where a sentence belongs (2026-09-28 playtest).
                raise SystemExit(f"'{p}' is not a button mask (hex like 0x81, or a name: "
                                 "a b select start right left up down r l x y)")
        else:
            raise SystemExit(f"unknown button '{p}' (a b select start right left up down r l x y, or 0xNN)")
    return mask


def parse_crop(values):
    """Four numbers, x y w h. Comma-separated is accepted too - `--crop
    0,8,256,16` is what muscle memory from every other tool types, and until
    this it was a Python traceback rather than an error."""
    parts = [p for v in values for p in str(v).replace(",", " ").split()]
    if len(parts) != 4:
        raise SystemExit(f"--crop needs four numbers: x y w h (got {' '.join(parts)!r})")
    try:
        x, y, w, h = (int(p) for p in parts)
    except ValueError as err:
        raise SystemExit(f"--crop: {err}")
    if w <= 0 or h <= 0:
        raise SystemExit("--crop needs a positive w and h")
    return [x, y, w, h]


# --------------------------------------------------------------------------
# Local rendering: the composite PNGs are for the user's eyes; an agent reads
# the text form. Pillow is used only here, and only when it is available.
# --------------------------------------------------------------------------


def render_png(path, cols, invert, mono=None, crop=None, zoom=1, bright=False, mark=None):
    try:
        from PIL import Image
    except ImportError:
        raise SystemExit("render needs Pillow (python3 -m pip install --user pillow)")

    try:
        img = Image.open(path).convert("RGB")
    except FileNotFoundError:
        raise SystemExit(f"no such PNG: {path}")
    except OSError as err:
        raise SystemExit(f"render: {path}: {err}")
    if crop:
        x, y, w, h = crop
        img = img.crop((x, y, x + w, y + h))
    if mono:
        # Threshold mode: one character per pixel, '#' where the pixel is darker
        # than the threshold. A DS menu is dark text on a bright panel, so this
        # is the setting that turns an 8px font into something readable;
        # --bright flips it for the messages that are bright ink on a dark
        # panel. --mark X Y draws a dot at a screen coordinate (the touch point
        # being investigated), so 'which control does this tap hit' is a
        # picture and not an inference.
        grey = img.convert("L")
        px = grey.load()
        gw, gh = grey.size
        ox, oy = (crop[0], crop[1]) if crop else (0, 0)
        mx, my = (mark[0] - ox, mark[1] - oy) if mark else (None, None)
        ink = (lambda v: v > mono) if bright else (lambda v: v < mono)
        out = [
            f"-- {os.path.basename(path)} mono{'><' if bright else '<'}{mono} "
            f"({gw}x{gh}, zoom {zoom}{', mark ' + str(mark) if mark else ''}) --"
        ]
        for y in range(gh):
            row = "".join(("#" * zoom) if ink(px[x, y]) else (" " * zoom) for x in range(gw))
            if mx is not None and y == my and 0 <= mx < gw:
                row = row[: mx * zoom] + "o" + row[mx * zoom + 1 :]
            for _ in range(zoom):
                out.append(row)
        return "\n".join(out)

    w, h = img.size
    rows = max(1, round(cols * (h / w) * 0.5))
    small = img.resize((cols, rows), Image.BOX)  # BOX averages the cell
    px = small.load()
    out = [f"-- {os.path.basename(path)} ({w}x{h} -> {cols}x{rows}) --"]
    for y in range(rows):
        line = ""
        for x in range(cols):
            r, g, b = px[x, y]
            lum = 0.2126 * r + 0.7152 * g + 0.0722 * b
            t = lum / 255 if invert else (255 - lum) / 255
            line += RAMP[min(9, max(0, round(t * 9)))]
        out.append(line)
    return "\n".join(out)


# --------------------------------------------------------------------------
# Commands
# --------------------------------------------------------------------------


def cmd_ping(ch, args):
    return ch.call(cmd="ping")


def cmd_status(ch, args):
    st = ch.call(cmd="status")
    if not st.get("active"):
        # The channel's own answer for "nothing is running" is `{active: false}`;
        # printing it raw made the idle app the one place this command did not
        # answer like a sentence (found driving the empty state, 2026-09-30).
        return "(no session: no console is running)"
    frames = ", ".join(f"p{f['player']}@{f['seq']}" for f in st["frames"])
    return (
        f"session: {st['players']} player(s), {st['rom']}\n"
        f"  title {st['title']!r}, up {st['elapsed_s']}s, link mask {st['link_mask']:#x}\n"
        f"  console frames: {frames}"
    )


def cmd_ui(ch, args):
    return json.dumps(json.loads(ch.call(cmd="ui")), indent=2)


def cmd_wifiio(ch, args):
    """What the console's own game is reading out of its wireless block."""
    return ch.call(cmd="wifiio", player=args.player)


def cmd_memwatch(ch, args):
    """Which instruction stores into a span of this console's own main RAM.

    Arms a store watch on [lo, hi] (inclusive) and prints the stores that land
    in it with the writer's PC, LR and CPSR. This answers "who wrote this
    variable" when the writer reaches it through a computed pointer, so the
    address has no literal in the code to search for. Off with `--off`, and only
    held while the session's console is running - arming clears the hits, and the
    first 64 are kept. The JIT writes main RAM on its fast path, so a session
    started with MD_NOJIT=1 is the one where this sees every store.
    """
    req = {"cmd": "memwatch", "player": args.player}
    if not args.off and args.lo is not None and args.hi is not None:
        req["lo"] = args.lo
        req["hi"] = args.hi
    return ch.call(**req)


def cmd_pctrace(ch, args):
    """The code path one CPU ran over a window, in the order it first ran it.

    Arms a PC recorder on `[lo, hi)` and prints the ordered path: the 4 KiB
    code regions it entered in order, then the last few hundred visits as
    (pc lr cpsr r0 r1 r2 r3). A downloaded image that boots, does its wireless
    bring-up and then declines a session has left that decision behind by the
    time anything is parked, so a snapshot cannot show it - this answers "which
    code did it run on the way", which is where a give-up branch lives.

    The PC is a real guest address in every execution mode: it is the address
    the CPU is about to run, taken from the same value the JIT dispatches on. So
    under the shipped JIT a whole basic block is one entry (each a real PC),
    while on the interpreter (MD_NOJIT=1) every instruction is one. Run it in the
    shipped JIT config: the interpreter changes the timing and, for a link, where
    the console parks.

    Off with `--off`, and only held while the console is running. Arming is
    applied by the CPU thread at the top of its next frame, so this command
    waits a few frames before reading the report back.
    """
    req = {"cmd": "pctrace", "player": args.player, "cpu": args.cpu}
    if args.off:
        req["off"] = True
    elif args.lo is not None and args.hi is not None:
        req["lo"] = args.lo
        req["hi"] = args.hi
    return ch.call(**req)


def cmd_probe(ch, args):
    """A console's CPUs and interrupts as numbers: where each is and whether it
    can take an interrupt.

    The five-second digest carries the program counters, but five seconds is the
    wrong resolution for "is this CPU stuck in that loop, or visiting it?" - so
    a caller samples them as fast as it can talk. `pc9`/`lr9` name where the
    game is; `arm9_ime`/`ie`/`if` say whether a pending interrupt could move it.
    """
    out = ch.call(cmd="probe", player=args.player)
    r9 = out["regs9"]
    r7 = out["regs7"]
    names = ["r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
             "r8", "r9", "r10", "r11", "r12", "sp", "lr", "pc"]
    lines = [
        f"p{out['player']} frame {out['frames']} asleep={out['asleep']} "
        f"pc9 {out['pc9']} lr9 {out['lr9']} sp9 {out['sp9']} · pc7 {out['pc7']}",
        "  ARM9  " + " ".join(f"{n}={v}" for n, v in zip(names, r9)),
        "  ARM7  " + " ".join(f"{n}={v}" for n, v in zip(names, r7)),
        f"  irq: can_take={out['arm9_can_take_irq']} IME={out['arm9_ime']} "
        f"IE={out['arm9_ie']} IF={out['arm9_if']} vblank_pending={out['arm9_vblank_pending']}",
        f"  lcd: powcnt1={out['powcnt1']} dispcntA={out['dispcnt_a']} "
        f"dispcntB={out['dispcnt_b']} vcount={out['vcount']}",
    ]
    mp = out.get("mp", {})
    if mp:
        lines.append(
            f"  mp: cmds tx {mp.get('cmds_tx')} acks {mp.get('acks_tx')} "
            f"collected {mp.get('collected')} empty {mp.get('empty')} "
            f"clientfail {mp.get('clientfail')} deauth out {mp.get('deauth_out')}"
        )
    ipc = out.get("ipc", {})
    if ipc:
        lines.append(
            f"  ipc: 9sends {ipc.get('arm9_sends')} 9recvs {ipc.get('arm9_recvs')} "
            f"7sends {ipc.get('arm7_sends')} 7recvs {ipc.get('arm7_recvs')} "
            f"fifocnt9 {ipc.get('fifocnt9')} fifocnt7 {ipc.get('fifocnt7')}"
        )
    return "\n".join(lines)


def cmd_dpjoiner(ch, args):
    """The synthesized single-card Download Play joiner, as numbers.

    Present only for a title whose profile says `download_play` (Star Fox
    Command, ASFE). Arming is the Settings per-game switch, not a knob here, so
    a disarmed joiner prints all zeros - which is the gate's proof: off means
    zero bus operations, not "nothing visible". `--player` moves which console
    acts as the Download Play client.
    """
    req = {"cmd": "dpjoiner"}
    if args.player is not None:
        req["player"] = args.player - 1
    out = ch.call(**req)
    if not out.get("present"):
        return out.get("note", "no download-play joiner on this title")
    out_line = (
        f"download-play joiner: console {out['joiner']}, "
        f"{'armed' if out['armed'] else 'DISARMED'}\n"
        f"  beacons {out['beacons']} (served {out.get('beaconsServed', 0)} while the host was silent) · auth sent {out['authSent']} / recv {out['authRecv']} · "
        f"assoc sent {out['assocSent']} / recv {out['assocRecv']} · "
        f"replies sent {out.get('repliesSent', 0)} · "
        f"other host frames {out['hostFrames']}\n"
        f"  last auth status {out['lastAuthStatus']:#06x}"
        f" (0 = accepted, non-zero = refused)\n"
        f"  last assoc status {out.get('lastAssocStatus', 0):#06x}, AID {out.get('lastAssocAid', 0):#06x}"
        f" (status 0 with a nonzero AID = the host registered this client)\n"
        f"  join/scan frames left for the booted game {out.get('requeued', 0)}\n"
        f"  host commands {out.get('commandsRecv', 0)} (last {out.get('lastCommand', 0):#04x}, "
        f"slave flags {out.get('lastCmdSlaveFlags', 0):#06x}) · acks {out.get('acksRecv', 0)}, "
        f"our reply collected {out.get('repliesCollected', 0)} times\n"
        f"  download: {out.get('imageBytes', 0)} of {out.get('imageTotalBytes', 0)} bytes "
        f"in {out.get('imagePackets', 0)} distinct packets · next wanted #"
        f"{out.get('imageNext', 0)}"
        f" · {'BOOTED' if out.get('booted') else 'not booted'}"
    )
    if out.get("deauths"):
        out_line += (
            f"\n  deauths from the host {out['deauths']} "
            f"(last reason {out['lastDeauthReason']:#06x} - the host refused the association)"
        )
    return out_line


def cmd_mem(ch, args):
    """Read a console's main RAM through the bridge (the memory-read instrument).

    Answers the ARM9's own 32-bit words at that address, which is how a game's
    state becomes a number instead of an inference from a picture - a parked
    game and a busy one look identical from the screen. `addr` is hex, with or
    without `0x`, and both it and the length are multiples of 4.
    """
    out = ch.call(cmd="mem", player=args.player, addr=args.addr, len=args.len,
                  cpu=args.cpu)
    words = out["words"]
    base = int(out["addr"], 16)
    # The CPU is named in the answer, not assumed: an ARM7 read and an ARM9 read
    # of the same number are different consoles' memories.
    lines = [f"p{out['player']} {'ARM7' if out.get('cpu') == 7 else 'ARM9'} "
             f"{out['addr']} ({out['len']} bytes)"]
    for i in range(0, len(words), 4):
        row = " ".join(words[i:i + 4])
        lines.append(f"  {base + i * 4:08X}  {row}")
    return "\n".join(lines)


def cmd_poke(ch, args):
    """Write a console's own variable, then answer it as it now stands.

    The write half of `mem`. A read can show that a loop's exit condition is
    false; only changing the value it tests can show that the loop is what
    holds the console. `words` is a list of hex 32-bit words, so a variable
    reads as it will in `mem` rather than as raw bytes.
    """
    out = ch.call(cmd="poke", player=args.player, addr=args.addr, words=args.words,
                  cpu=args.cpu)
    words = out["words"]
    base = int(out["addr"], 16)
    # The count comes from the answer, not from re-splitting the argument: the
    # server also accepts comma-separated words, so `len(args.words.split())`
    # would report "wrote 1" for two words the console did take.
    # The CPU is named, like `mem`'s: an ARM7 write and an ARM9 write of the
    # same number are different consoles' memories.
    lines = [f"p{out['player']} {'ARM7' if out.get('cpu') == 7 else 'ARM9'} "
             f"{out['addr']} ({out['len']} bytes, wrote {len(words)})"]
    for i in range(0, len(words), 4):
        lines.append(f"  {base + i * 4:08X}  {' '.join(words[i:i + 4])}")
    return "\n".join(lines)


def cmd_vary(ch, args):
    """Perturb one input of the parked handshake on this console (diagnostic).

    kind=body replaces the body bytes that console's game reads with `--body`,
    which is where a game's own state halfword sits, so a cell can ask what that
    console's game does with a value the peer never sent. Which bytes those are
    follows from the console: on player 1 the answers it collects, on player 2
    the commands it is handed. `--value` is how many deliveries to touch (0 =
    until cleared), and the reply carries `left`/`applied` so a caller can watch
    the probe fire. kind=off clears it and reports the cumulative `applied`.
    """
    return ch.call(
        cmd="vary",
        player=args.player,
        kind=args.kind,
        value=args.value,
        body=args.body or "",
    )


def cmd_see(ch, args):
    screen = {"top": 1, "bottom": 2, "both": 0}[args.screen]
    return ch.call(
        cmd="see",
        player=args.player,
        screen=screen,
        cols=args.cols,
        crop=parse_crop(args.crop) if args.crop else None,
        aspect=args.aspect,
    )


def cmd_shot(ch, args):
    out = ch.call(cmd="shot", name=args.name)
    return f"{out['path']} ({out['bytes']} bytes)"


def screen_digest(ch, player):
    """A hash of one console's two screens as they are right now.

    The channel has no "what is on this console's screen" query, and reading one
    as ASCII is a lossy downscale; a shot is exact and already exists. The
    composite PNG is 512x384 - player N's screens are the 256x384 column at
    (N-1)*256 - so a digest is the decoded pixels of one column, not the PNG
    bytes (which change with the encoder, not the picture).
    """
    import hashlib

    from PIL import Image

    out = ch.call(cmd="shot", name="digest")
    im = Image.open(out["path"]).convert("RGB")
    left = (player - 1) * 256
    if left + 256 > im.width:
        raise SystemExit(
            f"digest: player {player} is out of range: this shot has {im.width // 256} console(s)"
        )
    return hashlib.md5(im.crop((left, 0, left + 256, im.height)).tobytes()).hexdigest()


def cmd_digest(ch, args):
    """Print the digest of one console's screens (hand it to `waitfor --change`).

    This is the missing half of a scripted route. Every route in this project
    was written as a fixed list of frames, and the frames are a guess: a tap
    that lands on nothing and a tap that works look the same to the driver, and
    the guest join in a Metroid Prime Hunters lobby takes seven seconds at one
    moment and five at another (2026-09-29), so a route that slept five either
    went on too early or, worse, went on late - the lobby drops an idle guest
    after a minute or two, and the route then drives a console that is not there
    any more. Ask for the digest, act, then wait on it.
    """
    return screen_digest(ch, args.player)


def cmd_quit(ch, args):
    """Ask the app to exit, the way the window's close button does.

    Without this the only way to stop a scripted run was to kill the process,
    and the obvious pattern for that - `pkill -f src-tauri/target/release/...`
    from a shell whose own command line contains the same path - kills the
    shell that issued it (2026-09-28 playtest; tools/run_app.sh's header has
    the same warning from a previous pass, which is why a driver should not
    have to reach for pkill at all).
    """
    try:
        ch.call(cmd="quit")
    except SystemExit as err:
        # The app exits while answering, which closes the connection: that is
        # this command working, not failing (the run's last log line records
        # "[supervisor] app pid N exited with status 0").
        if "closed the control connection" not in str(err):
            raise
    return "asked the app to exit; the log's last line records the exit status"


def cmd_render(ch, args):
    """PNG -> ASCII, offline: `ch` is never used.

    The PNG is usually what a *finished* run left behind (md_game_session
    `--shot-dir`, or `shot` from an app that has since exited), so requiring a
    live app for this one would make the common case impossible.
    """
    return render_png(
        args.png,
        args.cols,
        args.invert,
        mono=args.mono,
        crop=parse_crop(args.crop) if args.crop else None,
        zoom=args.zoom,
        bright=args.bright,
        mark=args.mark,
    )


def cmd_window(ch, args):
    """Full-window-ish capture through KDE's Spectacle, for eyeballing the UI."""
    import subprocess

    name = args.name or f"window-{int(time.time())}"
    path = os.path.join(args.dir, name + ".png")
    os.makedirs(args.dir, exist_ok=True)
    try:
        subprocess.run(
            ["spectacle", "-b", "-n", "-o", path], check=True, capture_output=True, timeout=30
        )
    except FileNotFoundError:
        raise SystemExit("window: spectacle is not installed; use 'shot' (canvas capture)")
    except subprocess.CalledProcessError as err:
        raise SystemExit(f"window: spectacle failed: {err.stderr.decode(errors='replace').strip()}")
    return f"{path} ({os.path.getsize(path)} bytes) - whole screen, the app window included"


def live_players(ch):
    """The players whose console is running right now (empty when idle).

    Input commands take an explicit `--player`, so they can address a console
    that does not exist - and until now they reported success while doing
    nothing: `press a` on an app with no session printed "held buttons 0x1 on
    player 1 for 120ms" (2026-09-28 playtest), which is exactly the kind of
    feedback a scripted route cannot check. `see` already refuses an
    out-of-range player; the input commands now do the same.
    """
    st = ch.call(cmd="status")
    if not st.get("active"):
        return set()
    return {f["player"] for f in st.get("frames", [])}


def require_player(ch, player, what):
    live = live_players(ch)
    if not live:
        raise SystemExit(f"{what}: no session running - nothing is listening for player {player}'s input")
    if player not in live:
        raise SystemExit(f"{what}: player {player} has no console (running: {sorted(live)})")


def cmd_key(ch, args):
    ch.call(cmd="key", code=args.code, down=not args.up)
    if args.ms is not None:
        time.sleep(args.ms / 1000)
        ch.call(cmd="key", code=args.code, down=False)
    return f"key {args.code} {'up' if args.up else 'down'}"


def cmd_tap(ch, args):
    ch.call(cmd="key", code=args.code, down=True)
    time.sleep(args.ms / 1000)
    ch.call(cmd="key", code=args.code, down=False)
    return f"tapped {args.code} for {args.ms}ms"


def cmd_index(ch, args):
    return ch.call(cmd="index", player=args.player)


def cmd_press(ch, args):
    require_player(ch, args.player, "press")
    return ch.call(cmd="press", player=args.player, buttons=parse_buttons(args.buttons), ms=args.ms)


def cmd_touch(ch, args):
    require_player(ch, args.player, "touch")
    got = ch.call(cmd="touch", player=args.player, x=args.x, y=args.y, ms=args.ms)
    # A tap outside the digitizer reads as success and lands nowhere, which is a
    # silent miss in a scripted route (the MKDS taps are all in range, but a
    # shot-derived coordinate can be a pixel or a screen off). Say so.
    if not (0 <= args.x <= 255 and 0 <= args.y <= 191):
        return f"{got} - WARNING: {args.x},{args.y} is outside the 256x192 touch screen"
    return got


def cmd_untouch(ch, args):
    require_player(ch, args.player, "untouch")
    return ch.call(cmd="untouch", player=args.player)


def cmd_click(ch, args):
    return ch.call(cmd="click", selector=args.selector)


def cmd_text(ch, args):
    return ch.call(cmd="text", selector=args.selector)


def cmd_eval(ch, args):
    return ch.call(cmd="eval", expr=args.expr)


def cmd_bands(ch, args):
    """The rows that look like text, read where the screen reader already is.

    A DS menu line is the one place on a screen with many dark/light
    transitions across a row, which is what makes it readable at one character
    per pixel; the caller then crops each band and reads it instead of guessing
    y coordinates (which is how the MKDS route was lost twice). The reading
    lives in the page (__mdctl.bands), beside the frame access and the
    empty-state check every screen reading shares, because this function used to
    carry a second copy of both (2026-09-29).
    """
    screen = {"top": 1, "bottom": 2}[args.screen]
    got = ch.call(cmd="eval",
                  expr=f"__mdctl.bands({args.player}, {screen}, {args.edge}, {args.rows})")
    return f"-- text bands, player {args.player} {args.screen} --\n{got}"


def cmd_sleep(ch, args):
    ms = args.ms if args.ms is not None else (args.count if args.count is not None else 100)
    if ms < 0:
        raise SystemExit("sleep: milliseconds cannot be negative")
    ch.call(cmd="sleep", ms=ms)
    return f"slept {ms}ms"


def cmd_advance(ch, args):
    """Wait for a console to actually run N more frames (boot, loading, menus).

    A console runs at DS_FPS, so the wait is sized from the request unless
    --timeout says otherwise: the old fixed 15s default could not cover 900+
    frames and reported a perfectly healthy session as "wedged?" - `advance
    1300` on a run measured at 59.7 fps, which is the frame rate it was asking
    for (2026-09-28 playtest).
    """
    frames = args.frames if args.frames is not None else (
        args.count if args.count is not None else 60
    )
    timeout = args.timeout if args.timeout is not None else max(10.0, frames / DS_FPS * 1.5 + 3.0)
    start = time.time()
    st = ch.call(cmd="status")
    if not st.get("active"):
        raise SystemExit("no session running")
    base = None
    while time.time() - start < timeout:
        st = ch.call(cmd="status")
        seq = None
        for f in st.get("frames", []):
            if f["player"] == args.player:
                seq = f["seq"]
        if seq is None:
            raise SystemExit(f"no console for player {args.player}")
        if base is None:
            base = seq
        if seq - base >= frames:
            return f"player {args.player}: +{seq - base} frames in {time.time() - start:.1f}s (now at {seq})"
        time.sleep(0.1)
    got = 0 if base is None else seq - base
    rate = got / max(1e-6, time.time() - start)
    raise SystemExit(
        f"player {args.player} advanced only {got} of {frames} frames in {timeout:.0f}s "
        f"({rate:.1f} fps) - a running console does ~{DS_FPS:.0f} fps, so this one is stalled "
        "(see the log's health line), or the wait was too short for the frames asked for "
        "(pass --timeout)"
    )


def cmd_waitfor(ch, args):
    """Wait for one of three things: window text, a settled screen, a change.

    `waitfor TEXT` reads the window's status line, which is the whole of what
    the app says about itself. `--settle` waits until a console's picture stops
    changing, which is how a caller finds the end of a menu transition without
    naming a frame, and `--change DIGEST` waits until it is no longer what
    `digest` returned - the answer to "did that tap land anywhere", which no
    frame count can give (a tap that lands on nothing still costs its frames).
    """
    want = [bool(args.text), args.settle, bool(args.change)]
    if sum(want) != 1:
        raise SystemExit(
            "waitfor: give exactly one of TEXT, --settle or --change DIGEST "
            "(see --help)"
        )
    start = time.time()
    prev = None
    while time.time() - start < args.timeout:
        if args.text:
            ui = json.loads(ch.call(cmd="ui"))
            hay = ui.get("status", "") + " " + (ui.get("active") or "")
            if args.text in hay:
                return f"found {args.text!r} after {time.time() - start:.1f}s: {ui.get('status')!r}"
        elif args.change:
            now = screen_digest(ch, args.player)
            if now != args.change:
                return (f"player {args.player} screen changed after "
                        f"{time.time() - start:.1f}s: {now}")
        else:
            now = screen_digest(ch, args.player)
            if prev is not None and now == prev:
                return (f"player {args.player} screen settled after "
                        f"{time.time() - start:.1f}s: {now}")
            prev = now
        time.sleep(0.2)
    if args.text:
        last = json.loads(ch.call(cmd="ui"))
        raise SystemExit(f"never saw {args.text!r} in {args.timeout}s; status is {last.get('status')!r}")
    if args.change:
        raise SystemExit(
            f"player {args.player} screen was still {args.change} after {args.timeout}s "
            "- the step before this one did not land"
        )
    raise SystemExit(
        f"player {args.player} screen never settled in {args.timeout}s "
        "- something on it is animating"
    )


SCRIPT_SKIP = {"script"}

# Subcommands that read a file and never touch the app. `main` skips opening
# the control channel for these, so `render` works with no app running.
OFFLINE = {"render"}


def cmd_script(ch, args):
    """One command per line, same syntax as this CLI, '#' comments allowed."""
    out = []
    try:
        with open(args.file) as fh:
            lines = [ln.strip() for ln in fh]
    except OSError as err:
        # A missing script file used to be a traceback (2026-09-28 playtest).
        raise SystemExit(f"script: cannot read {args.file}: {err.strerror}")
    for n, line in enumerate(lines, 1):
        if not line or line.startswith("#"):
            continue
        print(f"[{n}] {line}", flush=True)
        argv = shlex.split(line)
        if argv[0] in SCRIPT_SKIP:
            raise SystemExit(f"{args.file}:{n}: script cannot call script")
        dispatch(ch, argv, nested=True)
        out.append(line)
    return f"ran {len(out)} command(s) from {args.file}"


def dispatch(ch, argv, nested=False):
    parser = build_parser()
    args = parser.parse_args(argv)
    if not hasattr(args, "func"):
        parser.print_help()
        return None
    data = args.func(ch, args)
    if data is not None:
        print(data, flush=True)
    return data


# --------------------------------------------------------------------------


def build_parser():
    ap = argparse.ArgumentParser(
        prog="appctl.py",
        description="Drive the running melonDS Multiplayer app (see the module docstring).",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__.split("Notes:")[0].split("commands")[0],
    )
    ap.add_argument("--port", type=int, default=DEFAULT_PORT, help=f"control port (default {DEFAULT_PORT})")
    sub = ap.add_subparsers(dest="cmd")

    sub.add_parser("ping", help="is the channel there").set_defaults(func=cmd_ping)
    sub.add_parser("status", help="session + frame counters (rust side, works when the UI is wedged)").set_defaults(func=cmd_status)
    sub.add_parser("ui", help="what the window shows (frontend side)").set_defaults(func=cmd_ui)

    p = sub.add_parser("see", help="read the emulated screens as ASCII text")
    p.add_argument("--player", type=int, default=1, help="1-based (default 1)")
    p.add_argument("--screen", choices=["top", "bottom", "both"], default="both")
    p.add_argument("--cols", type=int, default=128, help="characters per row (default 128)")
    p.add_argument("--crop", nargs="+", metavar="X Y W H", help="zoom into a region of the 256x192 screen")
    p.add_argument(
        "--aspect",
        type=float,
        default=0.5,
        help="rows per character-row: 0.5 keeps proportions (default), 1.0 is one character per pixel - stretched, but the only way to read an 8px DS font",
    )
    p.set_defaults(func=cmd_see)

    p = sub.add_parser("shot", help="save a PNG of every console's screens")
    p.add_argument("--name", help="file name stem (default shot-<ms>), written under logs/shots/")
    p.set_defaults(func=cmd_shot)

    p = sub.add_parser("render", help="print an existing PNG as ASCII (Pillow)")
    p.add_argument("png")
    p.add_argument("--cols", type=int, default=120)
    p.add_argument("--invert", action="store_true", help="dense characters mean BRIGHT pixels (off by default)")
    p.add_argument(
        "--mono",
        type=int,
        nargs="?",
        const=128,
        help="threshold mode: '#' where the pixel is darker than this (default 128). One character per pixel - the way to read menu text",
    )
    p.add_argument("--crop", nargs="+", metavar="X Y W H", help="crop the PNG before rendering")
    p.add_argument("--zoom", type=int, default=1, help="repeat every pixel N times in mono mode (2 makes glyphs easy to read)")
    p.add_argument("--bright", action="store_true", help="mono mode: '#' where the pixel is BRIGHTER than the threshold (bright ink on a dark panel)")
    p.add_argument("--mark", nargs=2, type=int, metavar=("X", "Y"), help="mark a screen coordinate with 'o' (in the un-cropped PNG)")
    p.set_defaults(func=cmd_render)

    p = sub.add_parser("window", help="capture the whole screen with spectacle (KDE)")
    p.add_argument("--name")
    p.add_argument("--dir", default="logs/shots")
    p.set_defaults(func=cmd_window)

    p = sub.add_parser("dpjoiner", help="the synthesized Download Play joiner: its frame counters, and which console is the client")
    p.add_argument("--player", type=int, help="1-based console that acts as the Download Play client (default: the last one)")
    p.set_defaults(func=cmd_dpjoiner)

    p = sub.add_parser("wifiio", help="what the console's game reads out of its wifi block (registers, in order, and the frame it was handed)")
    p.add_argument("--player", type=int, default=1)
    p.set_defaults(func=cmd_wifiio)

    p = sub.add_parser("memwatch", help="which instruction stores into a span of this console's own main RAM")
    p.add_argument("lo", nargs="?", help="hex low address of the window (e.g. 023722E0)")
    p.add_argument("hi", nargs="?", help="hex high address, inclusive (e.g. 023722E3)")
    p.add_argument("--off", action="store_true", help="disarm and just report the hits so far")
    p.add_argument("--player", type=int, default=1)
    p.set_defaults(func=cmd_memwatch)

    p = sub.add_parser("pctrace", help="the code path one CPU ran over a window, in order (which code did the console run)")
    p.add_argument("lo", nargs="?", help="hex low address of the window (e.g. 02000000)")
    p.add_argument("hi", nargs="?", help="hex high address, exclusive (e.g. 02400000)")
    p.add_argument("--cpu", type=int, choices=(9, 7), default=9, help="which CPU to trace (default 9)")
    p.add_argument("--off", action="store_true", help="disarm and just report the path so far")
    p.add_argument("--player", type=int, default=1)
    p.set_defaults(func=cmd_pctrace)

    p = sub.add_parser("probe", help="a console's CPUs, registers, interrupts and MP counters right now")
    p.add_argument("--player", type=int, default=1)
    p.set_defaults(func=cmd_probe)

    p = sub.add_parser("mem", help="read a console's main RAM (hex addr, multiple of 4) - the game's own state, not its picture")
    p.add_argument("addr", help="hex address, with or without 0x, multiple of 4 (e.g. 02000000)")
    p.add_argument("len", nargs="?", type=int, default=16, help="bytes to read, multiple of 4 (default 16, max 4096)")
    p.add_argument("--player", type=int, default=1)
    p.add_argument("--cpu", type=int, choices=[9, 7], default=9,
                   help="which CPU's address space to read: 9 = ARM9 main RAM "
                        "(default), 7 = ARM7 (WRAM 0x03800000, the game's own ARM7 "
                        "image at 0x06000000), which an ARM9 read cannot see")
    p.set_defaults(func=cmd_mem)

    p = sub.add_parser("poke", help="write a console's own variable (hex words) - the write half of mem, for testing a poll")
    p.add_argument("addr", help="hex word address, multiple of 4 (e.g. 021732AC)")
    p.add_argument("words", help="hex 32-bit words, space/comma separated (e.g. '0000003E 00000000')")
    p.add_argument("--player", type=int, default=1)
    p.add_argument("--cpu", type=int, choices=[9, 7], default=9,
                   help="which CPU's address space to write: 9 = ARM9 main RAM "
                        "(default), 7 = ARM7 (WRAM 0x03800000, the game's own ARM7 "
                        "image at 0x06000000), which an ARM9 write cannot reach")
    p.set_defaults(func=cmd_poke)

    p = sub.add_parser("vary", help="perturb one input of the parked handshake: body (replace the body bytes this console's game reads), off")
    p.add_argument("kind", choices=["off", "body"])
    p.add_argument("--value", type=int, default=0, help="how many deliveries to touch (0 = until cleared, for body)")
    p.add_argument("--body", help="hex bytes for kind=body, e.g. '00 80 00 00'")
    p.add_argument("--player", type=int, default=1)
    p.set_defaults(func=cmd_vary)

    p = sub.add_parser("key", help="send a real KeyboardEvent into the app (KeyA, ArrowUp, Digit2, ...)")
    p.add_argument("code")
    p.add_argument("--up", action="store_true", help="keyup instead of keydown")
    p.add_argument("--ms", type=int, help="hold for this long, then release")
    p.set_defaults(func=cmd_key)

    p = sub.add_parser("tap", help="press and release a key")
    p.add_argument("code")
    p.add_argument("--ms", type=int, default=80)
    p.set_defaults(func=cmd_tap)

    p = sub.add_parser("index", help="select which console the keyboard drives (frontend Digit1..4)")
    p.add_argument("player", type=int)
    p.set_defaults(func=cmd_index)

    p = sub.add_parser("press", help="hold DS buttons for a while (a, up+a, 0x81)")
    p.add_argument("buttons")
    p.add_argument("--player", type=int, default=1)
    p.add_argument("--ms", type=int, default=120)
    p.set_defaults(func=cmd_press)

    p = sub.add_parser("touch", help="touch the bottom screen at x y (DS pixels)")
    p.add_argument("x", type=int)
    p.add_argument("y", type=int)
    p.add_argument("--player", type=int, default=1)
    p.add_argument("--ms", type=int, help="lift after this long (default: leave the pen down)")
    p.set_defaults(func=cmd_touch)

    p = sub.add_parser("untouch", help="lift the pen")
    p.add_argument("--player", type=int, default=1)
    p.set_defaults(func=cmd_untouch)

    p = sub.add_parser("quit", help="ask the app to exit (the window's close button, from a shell)")
    p.set_defaults(func=cmd_quit)

    p = sub.add_parser("click", help="click a UI element by CSS selector")
    p.add_argument("selector")
    p.set_defaults(func=cmd_click)

    p = sub.add_parser("text", help="read an element's text (default #status)")
    p.add_argument("selector", nargs="?", default="#status")
    p.set_defaults(func=cmd_text)

    p = sub.add_parser("eval", help="evaluate JS in the webview and print the result")
    p.add_argument("expr")
    p.set_defaults(func=cmd_eval)

    p = sub.add_parser("bands", help="find the screen rows that contain text (then 'see --crop' them)")
    p.add_argument("--player", type=int, default=1)
    p.add_argument("--screen", choices=["top", "bottom"], default="bottom")
    p.add_argument("--edge", type=int, default=40, help="luminance step that counts as an edge (default 40)")
    p.add_argument("--rows", type=int, default=12, help="edges per row to call it text (default 12)")
    p.set_defaults(func=cmd_bands)

    p = sub.add_parser("sleep", help="wait, in milliseconds: sleep 500, or --ms 500")
    # Every other command that takes a duration spells it `--ms` (`press --ms`,
    # `tap --ms`, `touch --ms`), and `sleep --ms 500` - the form that spelling
    # invites - died in argparse with a usage dump (2026-09-29 playtest). Both
    # forms work now, the way `advance` already accepts a bare count and a flag.
    p.add_argument("count", nargs="?", type=int, metavar="MS", help="milliseconds (same as --ms)")
    p.add_argument("--ms", type=int, help="milliseconds (default 100)")
    p.set_defaults(func=cmd_sleep)

    p = sub.add_parser("advance", help="wait for a console to run more frames: advance N, or --frames N")
    # The bare count is how it gets typed (`advance 5000`), and with only
    # `--frames` that form died in argparse with "unrecognized arguments" - a
    # usage error from a command whose help says "run more frames". Both forms
    # work now; the flag wins if both are given.
    p.add_argument("count", nargs="?", type=int, metavar="N", help="frames to wait for (same as --frames)")
    p.add_argument("--frames", type=int, help="frames to wait for (default 60)")
    p.add_argument("--player", type=int, default=1)
    p.add_argument("--timeout", type=float, default=None,
                   help="seconds to wait (default: sized from the frame count at ~60 fps)")
    p.set_defaults(func=cmd_advance)

    p = sub.add_parser("waitfor", help="wait for window text, a settled screen, or a screen change")
    p.add_argument("text", nargs="?", help="text to appear in the window's status line")
    p.add_argument("--settle", action="store_true",
                   help="wait until a console's picture stops changing")
    p.add_argument("--change", metavar="DIGEST",
                   help="wait until a console's picture differs from this digest (see `digest`)")
    p.add_argument("--player", type=int, default=1, help="for --settle/--change (1-based)")
    p.add_argument("--timeout", type=float, default=20.0)
    p.set_defaults(func=cmd_waitfor)

    p = sub.add_parser("digest", help="hash a console's screens now (for `waitfor --change`)")
    p.add_argument("--player", type=int, default=1, help="1-based")
    p.set_defaults(func=cmd_digest)

    p = sub.add_parser("script", help="run a file of these commands, one per line")
    p.add_argument("file")
    p.set_defaults(func=cmd_script)

    return ap


def main():
    ap = build_parser()
    args = ap.parse_args()
    if not hasattr(args, "func"):
        ap.print_help()
        return 1
    ch = None if args.cmd in OFFLINE else Channel(args.port)
    try:
        data = args.func(ch, args)
        if data is not None:
            print(data)
    finally:
        if ch is not None:
            ch.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

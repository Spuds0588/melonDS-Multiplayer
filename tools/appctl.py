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
        return json.dumps(st)
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
    ch.call(cmd="sleep", ms=args.ms)
    return f"slept {args.ms}ms"


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
    start = time.time()
    while time.time() - start < args.timeout:
        ui = json.loads(ch.call(cmd="ui"))
        hay = ui.get("status", "") + " " + (ui.get("active") or "")
        if args.text in hay:
            return f"found {args.text!r} after {time.time() - start:.1f}s: {ui.get('status')!r}"
        time.sleep(0.2)
    last = json.loads(ch.call(cmd="ui"))
    raise SystemExit(f"never saw {args.text!r} in {args.timeout}s; status is {last.get('status')!r}")


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

    p = sub.add_parser("wifiio", help="what the console's game reads out of its wifi block (registers, in order, and the frame it was handed)")
    p.add_argument("--player", type=int, default=1)
    p.set_defaults(func=cmd_wifiio)

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

    p = sub.add_parser("sleep", help="wait, in milliseconds")
    p.add_argument("ms", type=int)
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

    p = sub.add_parser("waitfor", help="wait for text to appear in the window's status line")
    p.add_argument("text")
    p.add_argument("--timeout", type=float, default=20.0)
    p.set_defaults(func=cmd_waitfor)

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

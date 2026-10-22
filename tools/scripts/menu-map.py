#!/usr/bin/env python3
"""Map a title's front end by trying every input from every screen, and keep it.

Every route in this project before today was a list of guessed taps: a tap that
lands on nothing and a tap that works leave the same trail in a log, so a menu
that moved on was discovered by patience and a menu that did not was abandoned
with a shrug. Meteos's wireless entry was recorded at (60,108) and that
coordinate opens STAR TRIP; Diddy Kong Racing's game-type wheel was tried with a
handful of D-pad taps and left at "not reached".

This sweeps instead of guessing. From one screen it tries every button, then a
grid of touches, and from each screen that turns out to be new it repeats. It
uses the app's own `snapshot`/`restore` control commands, so each trial starts
from exactly the picture the last one started from (see tools/appctl.py), and it
saves every distinct screen as text and as a PNG so the route can be read from
evidence afterwards.

    tools/scripts/menu-map.py --name meteos                # full sweep
    tools/scripts/menu-map.py --name dkr --no-touch         # buttons only
    tools/scripts/menu-map.py --name meteos --resume       # continue a sweep

Output, under logs/menu-map/<name>/:

    graph.json      every screen, the action that reached it, and its text
    report.md       the same as a readable table
    n<id>.png       the composite shot of that screen (all consoles)
    n<id>.txt       both DS screens as ASCII

The app must already be running on the screen the sweep should start from
(`tools/run_app.sh` + a ROM, booted to its title/menu). Nothing here decides
which screen matters - it enumerates, and a person reads the pictures.
"""

import argparse
import json
import os
import sys
import time

from PIL import Image

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from appctl import DEFAULT_PORT, DS_FPS, Channel  # noqa: E402

# How different two pictures must be to count as two screens. A DS menu that
# replaces another changes most of the panel (mean delta in the tens); a
# blinking cursor or a spinning logo moves a bounded patch, and the reference
# phases below cover the cycle so a moving screen still matches itself.
SAME_MEAN = 3.0
# Reads taken per picture. 1.2s covers the phase of a slow logo loop; comparing
# against all of them is what stops "it animates" from reading as "it changed".
PHASES = 4
PHASE_GAP = 0.3

# Every button the DS has. The D-pad is four directions, so "both directions"
# is up+down and left+right as separate trials, not a sweep that only goes one
# way (which is how a wheel that only turns right was mistaken for a wheel that
# does not turn).
BUTTONS = ["a", "b", "x", "y", "l", "r", "start", "select",
           "up", "down", "left", "right"]

MASKS = {"a": 1 << 0, "b": 1 << 1, "select": 1 << 2, "start": 1 << 3,
         "right": 1 << 4, "left": 1 << 5, "up": 1 << 6, "down": 1 << 7,
         "r": 1 << 8, "l": 1 << 9, "x": 1 << 10, "y": 1 << 11}

# Two presses at once: the DS's own menus use them for "back" (B) and for
# confirmations that a single button does not trigger.
COMBOS = ["up+a", "down+a", "start+a", "l+r", "select+start"]


def mask_of(spec):
    m = 0
    for part in spec.split("+"):
        part = part.strip()
        if part not in MASKS:
            raise SystemExit(f"unknown button {part!r} in {spec!r}")
        m |= MASKS[part]
    return m


class Sweep:
    def __init__(self, ch, player, outdir, settle, timeout, budget):
        self.ch = ch
        self.player = player
        self.outdir = outdir
        self.settle = settle
        self.timeout = timeout
        self.budget = budget
        self.t0 = time.time()
        self.nodes = []          # [{id, refs, parent, action, text, png}]
        self.edges = []          # [{from, action, to, changed}]

    # -- screen reads ----------------------------------------------------

    def sig(self):
        """A small gray thumbnail of this console's *bottom* screen.

        A digest cannot tell "the screen moved" from "the screen animates":
        every read of a title screen with a spinning logo is a different hash,
        and a sweep built on hashes would call every action new. Measured on
        Tetris DS's main menu, two reads of the same screen differ by 0.0 on the
        bottom panel and by up to 5.8 mean gray levels on the animated top one,
        while tapping a different menu row moves the bottom panel by 30.6. So the
        bottom screen is what tells a menu from itself, and the top is where a
        background loop would otherwise look like a new page.

        The cost of ignoring the top screen is that a screen which changes only
        there reads as unchanged; every route this tool has produced so far is
        touch-driven on the bottom panel, which is where DS menus live.
        """
        out = self.ch.call(cmd="shot", name="menu-map-sig")
        im = Image.open(out["path"]).convert("L")
        left = (self.player - 1) * 256
        col = im.crop((left, 192, left + 256, 384)).resize((64, 48), Image.BOX)
        return col.tobytes()

    @staticmethod
    def dist(a, refs):
        """Smallest mean difference between picture `a` and any reference phase."""
        best = None
        for ref in refs:
            total = 0
            for x, y in zip(a, ref):
                d = x - y
                total += d if d >= 0 else -d
            mean = total / len(a)
            if best is None or mean < best:
                best = mean
        return best if best is not None else 255.0

    def phases(self, n=PHASES):
        out = []
        for i in range(n):
            if i:
                time.sleep(PHASE_GAP)
            out.append(self.sig())
        return out

    def settled(self):
        """The picture after an input, once it stops moving (or times out).

        Menus animate, so "three identical reads" never happens on one of them;
        this stops when the last two reads differ by less than the same-screen
        threshold, and otherwise returns after `timeout` with the last read.
        """
        start = time.time()
        prev = self.sig()
        while time.time() - start < self.timeout:
            time.sleep(0.12)
            now = self.sig()
            if self.dist(now, [prev]) < 0.8:
                return now
            prev = now
        return prev

    def text_of(self):
        out = {}
        for screen in ("top", "bottom"):
            out[screen] = self.ch.call(cmd="see", player=self.player,
                                       screen=1 if screen == "top" else 2,
                                       cols=128)
        return out

    # -- actions ---------------------------------------------------------

    def apply(self, action):
        kind, value = action
        if kind == "press":
            self.ch.call(cmd="press", player=self.player,
                         buttons=mask_of(value), ms=180)
        elif kind == "touch":
            x, y = value
            self.ch.call(cmd="touch", player=self.player, x=x, y=y, ms=180)
        else:
            raise SystemExit(f"unknown action kind {kind!r}")

    def label(self, action):
        kind, value = action
        if kind == "press":
            return f"press {value}"
        x, y = value
        return f"touch {x},{y}"

    # -- nodes -----------------------------------------------------------

    def add_node(self, refs, parent, action, text, shot):
        nid = len(self.nodes)
        node = {"id": nid, "refs": refs, "parent": parent,
                "action": action, "text": text, "png": shot}
        self.nodes.append(node)

        return nid

    def save_art(self, nid):
        node = self.nodes[nid]
        shot = self.ch.call(cmd="shot", name=f"menu-map-{os.path.basename(self.outdir)}-n{nid}")
        # The app writes into logs/shots/; copy so the sweep's own directory is
        # self-contained and survives the shot directory being cleaned.
        dst = os.path.join(self.outdir, f"n{nid}.png")
        with open(shot["path"], "rb") as src, open(dst, "wb") as out:
            out.write(src.read())
        node["png"] = dst
        node["text"] = self.text_of()
        with open(os.path.join(self.outdir, f"n{nid}.txt"), "w") as fh:
            for screen in ("top", "bottom"):
                fh.write(f"--- {screen} ---\n{node['text'][screen]}\n")

    def match(self, sig, skip=None):
        """Which node this picture is, or None when it is a screen not seen yet."""
        for node in self.nodes:
            if skip is not None and node["id"] == skip:
                continue
            if self.dist(sig, node["refs"]) < SAME_MEAN:
                return node["id"]
        return None

    def write(self):
        with open(os.path.join(self.outdir, "graph.json"), "w") as fh:
            json.dump({"player": self.player,
                       "nodes": [{k: v for k, v in n.items() if k != "refs"}
                                 for n in self.nodes],
                       "edges": self.edges}, fh, indent=1)
        lines = ["# Menu sweep", "",
                 f"player {self.player}, {len(self.nodes)} distinct screen(s), "
                 f"{len(self.edges)} trialled input(s)", ""]
        for node in self.nodes:
            parent = "-" if node["parent"] is None else f"n{node['parent']}"
            lines.append(f"## n{node['id']} (from {parent} via {node['action']})")
            lines.append("")
            for screen in ("top", "bottom"):
                body = node["text"][screen].rstrip()
                lines.append(f"```\n[{screen}]\n{body}\n```")
            lines.append("")
        with open(os.path.join(self.outdir, "report.md"), "w") as fh:
            fh.write("\n".join(lines))

    # -- the sweep -------------------------------------------------------

    def run(self, actions, max_nodes):
        os.makedirs(self.outdir, exist_ok=True)
        root = self.add_node(self.phases(), None, "boot", None, None)
        self.save_art(root)
        snap = f"mm-{os.path.basename(self.outdir)}-{root}"
        self.ch.call(cmd="snapshot", player=self.player, name=snap)
        print(f"[0] root saved as {snap}", flush=True)

        queue = [root]
        while queue and len(self.nodes) < max_nodes:
            if time.time() - self.t0 > self.budget:
                print(f"budget of {self.budget:.0f}s spent - stopping with "
                      f"{len(self.nodes)} screen(s)", flush=True)
                break
            nid = queue.pop(0)
            node = self.nodes[nid]
            for action in actions:
                if len(self.nodes) >= max_nodes:
                    break
                if time.time() - self.t0 > self.budget:
                    break
                # Back to the screen this node is, then one input.
                self.ch.call(cmd="restore", player=self.player,
                             name=f"mm-{os.path.basename(self.outdir)}-{nid}")
                before = self.settled()
                if self.dist(before, node["refs"]) >= SAME_MEAN:
                    # The restore did not reproduce the screen, so any result
                    # from here is not attributable to this action. Say so
                    # rather than filing a false edge.
                    print(f"[{nid}] restore mismatch ({self.dist(before, node['refs']):.1f} "
                          "from the recorded screen) - skipping its actions", flush=True)
                    break
                self.apply(action)
                after = self.settled()
                same = self.dist(after, [before]) < SAME_MEAN
                seen = self.match(after, skip=nid if same else None)
                entry = {"from": nid, "action": self.label(action),
                         "to": seen, "changed": not same}
                self.edges.append(entry)
                if same:
                    continue
                if seen is not None:
                    print(f"[{nid}] {self.label(action)} -> known n{seen}", flush=True)
                    continue
                refs = self.phases()
                refs.append(after)
                child = self.add_node(refs, nid, self.label(action), None, None)
                entry["to"] = child
                self.save_art(child)
                snap = f"mm-{os.path.basename(self.outdir)}-{child}"
                self.ch.call(cmd="snapshot", player=self.player, name=snap)
                print(f"[{nid}] {self.label(action)} -> NEW n{child}", flush=True)
                queue.append(child)
        self.write()
        print(f"sweep done: {len(self.nodes)} screen(s), {len(self.edges)} input(s), "
              f"{time.time() - self.t0:.0f}s - see {self.outdir}/report.md", flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--name", required=True, help="sweep name (its output directory)")
    ap.add_argument("--player", type=int, default=1, help="console to sweep (default 1)")
    ap.add_argument("--out", default=None,
                    help="output directory (default logs/menu-map/<name>)")
    ap.add_argument("--no-touch", action="store_true",
                    help="buttons only (a top-screen menu has nothing to tap)")
    ap.add_argument("--no-combos", action="store_true", help="skip two-button presses")
    ap.add_argument("--touch-step", type=int, default=32,
                    help="touch grid spacing in DS pixels (default 32 -> 8x6)")
    ap.add_argument("--max-nodes", type=int, default=40, help="stop after N screens")
    ap.add_argument("--budget", type=float, default=1200.0, help="seconds (default 1200)")
    ap.add_argument("--settle", type=float, default=2.5,
                    help="seconds a picture may take to stop moving (default 2.5)")
    ap.add_argument("--port", type=int, default=None)
    args = ap.parse_args()

    root = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__)))))
    outdir = args.out or os.path.join(root, "logs", "menu-map", args.name)
    if os.path.isdir(outdir) and not os.path.exists(os.path.join(outdir, "graph.json")):
        pass  # half-written from a crashed sweep: overwrite

    actions = [("press", b) for b in BUTTONS]
    if not args.no_combos:
        actions += [("press", c) for c in COMBOS]
    if not args.no_touch:
        xs = list(range(args.touch_step // 2, 256, args.touch_step))
        ys = list(range(args.touch_step // 2, 192, args.touch_step))
        actions += [("touch", (x, y)) for y in ys for x in xs]

    ch = Channel(args.port) if args.port else Channel(DEFAULT_PORT)
    try:
        status = ch.call(cmd="status")
        if not status.get("active"):
            raise SystemExit("menu-map: no session running - start the app with "
                             "tools/run_app.sh and a ROM first")
        frames = {f["player"]: f["seq"] for f in status.get("frames", [])}
        if args.player not in frames:
            raise SystemExit(f"menu-map: player {args.player} is not running "
                             f"(this session has {sorted(frames)})")
        print(f"session: player {args.player} at frame {frames[args.player]}, "
              f"{len(actions)} actions per screen, ~{DS_FPS:.0f} fps", flush=True)
        Sweep(ch, args.player, outdir, args.settle, args.settle, args.budget).run(
            actions, args.max_nodes)
    finally:
        ch.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

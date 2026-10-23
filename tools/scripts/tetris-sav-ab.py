#!/usr/bin/env python3
"""A/B the two-cart start handshake by the save each trial starts from.

    tools/scripts/tetris-sav-ab.py --trials 8

The streak of eight wins that began right after the Tetris save was restored to
its pinned md5 left one open question: does the card's content decide whether the
guest's join-data chunk advances, or was the streak the machine's mood? Every
trial here restores a known save first, boots the app, runs the route, and
records the outcome, so the two arms differ in the save and in nothing else:

    pin      the pinned save (`a3c0bc2c...`)
    written  a save the game itself wrote in an earlier session (`7866ada3...`)

Arms alternate; each pair alternates which arm goes first. A losing trial gets a
post-mortem while the app is still up: a screenshot of every console and two
frame-counter samples 5 s apart, so the guest's own screens and the question "is
its emulator still running while its chunk is stuck" are recorded rather than
reconstructed.

Records go to a JSONL file (default `logs/tetris-sav-ab.jsonl`); the summary at
the end prints wins/losses per arm.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SAV = os.path.join(ROOT, "Test Roms", "Tetris DS (USA).sav")
ROM = os.path.join(ROOT, "Test Roms", "Tetris DS (USA).nds")
PIN = os.path.join(ROOT, "game-captures", "sav-backups",
                   "Tetris DS (USA).sav.before-app-20261023-094122")
WRITTEN = os.path.join(ROOT, "game-captures", "sav-backups",
                       "Tetris DS (USA).sav.before-app-20261023-102608")


def md5(path):
    import hashlib
    h = hashlib.md5()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def run(cmd, **kw):
    return subprocess.run(cmd, cwd=ROOT, shell=isinstance(cmd, str), **kw)


def cpu_seconds(pid):
    with open(f"/proc/{pid}/stat") as f:
        parts = f.read().rsplit(")", 1)[1].split()
    return (int(parts[11]) + int(parts[12])) / os.sysconf("SC_CLK_TCK")


def status():
    out = run("python3 tools/appctl.py status", capture_output=True, text=True)
    frames, elapsed = {}, None
    m = re.search(r"console frames: (.*)", out.stdout)
    if m:
        for part in m.group(1).split(","):
            q = re.match(r"\s*p(\d+)@(\d+)", part)
            if q:
                frames[int(q.group(1))] = int(q.group(2))
    m = re.search(r"up (\d+)s", out.stdout)
    if m:
        elapsed = int(m.group(1))
    return {"frames": frames, "up_s": elapsed}


def shot(name):
    out = run(f"python3 tools/appctl.py shot --name {name}", capture_output=True, text=True)
    m = re.search(r"(/\S+\.png)", out.stdout)
    return m.group(1) if m else None


def trial(tag, arm, sav, keep):
    shutil.copyfile(sav, SAV)
    want = md5(sav)
    if md5(SAV) != want:
        raise SystemExit(f"restoring {sav} into {SAV} failed")
    run("python3 tools/appctl.py quit >/dev/null 2>&1 || true")
    time.sleep(2)
    log = f"{ROOT}/logs/{tag}.log"
    env = dict(os.environ,
               MD_LINK_VERBOSE="1",
               MD_AUTOSTART_PLAYERS="2",
               MD_AUTOSTART_ROM=ROM,
               MD_LOG=log,
               MD_SHOTS_DIR=f"{ROOT}/logs/shots/{tag}")
    with open(f"{ROOT}/logs/{tag}_run.out", "w") as fh:
        subprocess.Popen(["tools/run_app.sh"], cwd=ROOT, env=env,
                         stdout=fh, stderr=subprocess.STDOUT, start_new_session=True)
    for _ in range(90):
        if run("python3 tools/appctl.py ping >/dev/null 2>&1").returncode == 0:
            break
        time.sleep(1)
    pid = None
    for _ in range(20):
        try:
            with open(f"{ROOT}/logs/{tag}_run.out") as fh:
                m = re.search(r"launched app pid (\d+)", fh.read())
            if m:
                pid = int(m.group(1))
                break
        except FileNotFoundError:
            pass
        time.sleep(1)
    time.sleep(6)
    st0 = status()
    t0 = time.time()
    c0 = cpu_seconds(pid) if pid else None
    route_log = open(f"{ROOT}/logs/{tag}_route.txt", "w")
    try:
        rc = subprocess.run(["python3", "tools/scripts/tetris-4p.py", "--players", "2",
                             "--host", "1", "--start"], cwd=ROOT, timeout=300,
                            stdout=route_log, stderr=subprocess.STDOUT).returncode
    except subprocess.TimeoutExpired:
        rc = "timeout"
    route_log.close()
    wall = time.time() - t0
    st1 = status()
    c1 = cpu_seconds(pid) if pid else None
    post = None
    if rc != 0:
        # The party is still up and stalled: capture what each console shows and
        # whether the guest's emulator is still producing frames while its chunk
        # sits at 08.
        s1 = shot(f"{tag}-stall")
        time.sleep(5)
        st2 = status()
        s2 = shot(f"{tag}-stall-late")
        post = {"shot": s1, "shot_late": s2,
                "frames_5s": {p: st2["frames"].get(p, 0) - st1["frames"].get(p, 0)
                              for p in st1["frames"]}}
    run("python3 tools/appctl.py quit >/dev/null 2>&1 || true")
    time.sleep(4)
    fps = {p: round((st1["frames"][p] - st0["frames"][p]) / wall, 1)
           for p in st0["frames"] if p in st1["frames"]}
    rec = {"tag": tag, "arm": arm, "sav": md5(sav), "exit": rc, "wall_s": round(wall, 1),
           "cpu_cores": round((c1 - c0) / wall, 2) if (c0 is not None and c1 is not None) else None,
           "frames": {p: st1["frames"][p] - st0["frames"][p] for p in st0["frames"]},
           "fps": fps, "post": post,
           "sav_written": md5(SAV), "log": log}
    with open(keep, "a") as out:
        out.write(json.dumps(rec) + "\n")
    return rec


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trials", type=int, default=8, help="trials per arm")
    ap.add_argument("--prefix", default="ab", help="tag prefix")
    ap.add_argument("--pin", default=PIN)
    ap.add_argument("--written", default=WRITTEN)
    ap.add_argument("--out", default=os.path.join(ROOT, "logs", "tetris-sav-ab.jsonl"))
    args = ap.parse_args()

    arms = {"pin": args.pin, "written": args.written}
    for name, path in arms.items():
        if not os.path.exists(path):
            raise SystemExit(f"missing {name} save: {path}")
        print(f"{name}: {path} ({md5(path)})", flush=True)

    for i in range(1, args.trials + 1):
        order = ["pin", "written"] if i % 2 else ["written", "pin"]
        for arm in order:
            tag = f"{args.prefix}{'p' if arm == 'pin' else 'w'}{i}"
            rec = trial(tag, arm, arms[arm], args.out)
            print(f"{tag:8s} {arm:8s} exit={rec['exit']} wall={rec['wall_s']:5.1f} "
                  f"cpu={rec['cpu_cores']} fps={rec['fps']}", flush=True)

    wins = {"pin": [0, 0], "written": [0, 0]}
    with open(args.out) as f:
        for line in f:
            r = json.loads(line)
            if not r["tag"].startswith(args.prefix):
                continue
            wins[r["arm"]][0 if r["exit"] == 0 else 1] += 1
    print("\narm        wins losses")
    for arm, (w, l) in wins.items():
        print(f"{arm:10s} {w:4d} {l:6d}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

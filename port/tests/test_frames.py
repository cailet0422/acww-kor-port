"""A build of the port against the decomp's recordings: the same frames, bit for bit.

    python3 port/tests/test_frames.py ROM BUILD [--quick] [--run "qemu-aarch64 -L /usr/aarch64-linux-gnu"]

Needs your ROM (the Korean release, as the game does), so it is run by hand, not by CI. It
splits the ROM with BUILD/acww-rom-prep, then plays the decomp's input recordings
(decomp/runtime/recordings/title-start.txt and new-game.txt) in BUILD/acww-run, the decomp's
headless program, in the Reference form, and compares the SHA-1 of the frame sequence with the
one the decomp expects (the .sha1 beside each recording). The decomp's own self-test
(decomp/tools/setup.py --check) does the same on a 32-bit x86 PC; this is how the port checks
the hosts the decomp was not written for: 64-bit, and ARM. --quick plays only the first
recording; --run names a program to run BUILD's programs with (qemu-user, for a cross build:
ROM-split included, so the host's own acww-rom-prep is not needed).
"""

from __future__ import annotations

import argparse
import os
import pathlib
import re
import shlex
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
RECORDINGS = ROOT / "decomp" / "runtime" / "recordings"
RTC = "2005-11-23 12:00:00"  # decomp/tools/setup.py
PLAYS = [("title-start", 1300), ("new-game", 13760)]


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rom")
    ap.add_argument("build")
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("--run", default="", help="a program to run the build's programs with")
    args = ap.parse_args()
    build = pathlib.Path(args.build).resolve()
    wrap = shlex.split(args.run)
    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp)
        p = subprocess.run([*wrap, str(build / "acww-rom-prep"), str(pathlib.Path(args.rom).resolve()), str(work / "modules")],
                           capture_output=True, text=True)
        if p.returncode != 0:
            sys.exit(p.stderr)
        rom, manifest = p.stdout.split("\n")[:2]
        for name, frames in PLAYS[:1] if args.quick else PLAYS:
            want = (RECORDINGS / f"{name}.sha1").read_text().split()[0]
            env = dict(os.environ, RUN_INPUT=str(RECORDINGS / f"{name}.txt"), RUN_RTC=RTC, RUN_NATIVE="0",
                       RUN_HASH=str(work / f"{name}-hashes.txt"))
            start = time.time()
            p = subprocess.run([*wrap, str(build / "acww-run"), rom, manifest, "", "0", str(frames)], cwd=work, env=env,
                               capture_output=True, text=True, errors="replace")
            got = re.findall(r"sequence sha1 ([0-9a-f]{40})", p.stdout)
            good = p.returncode == 0 and bool(got) and got[-1] == want
            ok &= good
            print(f"{'ok  ' if good else 'FAIL'} {name}: {frames} frames in {time.time() - start:.0f} s, "
                  f"sequence {got[-1] if got else 'none'}" + ("" if good else f", expected {want} (exit {p.returncode})"),
                  flush=True)
            if not good:
                print((p.stdout + p.stderr)[-2000:])
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()

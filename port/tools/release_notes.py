#!/usr/bin/env python3
"""A version's notes for its release: its section of CHANGELOG.ko.md, then of CHANGELOG.md.

    release_notes.py VERSION

The app shows the start of these notes when it offers the update, so the Korean comes first.
"""
import pathlib
import sys


def section(path, version):
    lines, inside = [], False
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("## "):
            if inside:
                break
            inside = line[3:].split()[0] == version
            continue
        if inside:
            lines.append(line)
    return "\n".join(lines).strip()


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    root = pathlib.Path(__file__).resolve().parents[2]
    korean = section(root / "CHANGELOG.ko.md", sys.argv[1])
    english = section(root / "CHANGELOG.md", sys.argv[1])
    if not korean or not english:
        sys.exit(f"no notes for {sys.argv[1]} in CHANGELOG.ko.md and CHANGELOG.md")
    print(korean + "\n\n---\n\n" + english)


if __name__ == "__main__":
    main()

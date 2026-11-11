"""The port's ROM split (port/src/rom_prep.c) against the decomp's own (decomp/tools/extract.py).

    python3 port/tests/test_rom_prep.py BUILD/acww-rom-prep

No ROM is needed: the test builds small synthetic DS ROMs with the layout extract.py reads
(a BLZ-compressed ARM9 with its build information and autoloads, plain and compressed
overlays), splits each with both, and compares every module byte for byte. It also checks the
port's SHA-1 against hashlib, through the message a wrong ROM gets.
"""

from __future__ import annotations

import hashlib
import pathlib
import random
import struct
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "decomp" / "tools"))
import extract  # noqa: E402  (the decomp's split, the reference)

ARM9_RAM = 0x02000000
MAGIC = bytes.fromhex("2106c0dedec00621")


def blz_compress(plain: bytes) -> bytes:
    """A backward-LZ stream that extract.blz_decompress reads back as `plain` (greedy matches,
    an 8-byte header). The decoder writes the output from its end, so the matching is done on
    the reversed data, and the tokens are stored in reverse of the order it reads them."""
    r = plain[::-1]
    tokens = bytearray()
    i = 0
    while i < len(r):
        flag_at = len(tokens)
        tokens.append(0)
        flags = 0
        for bit in range(8):
            if i >= len(r):
                break
            best, best_back = 0, 0
            for back in range(3, min(0x1002, i) + 1):
                n = 0
                while n < 18 and i + n < len(r) and r[i + n] == r[i + n - back]:
                    n += 1
                if n > best:
                    best, best_back = n, back
            if best >= 3:
                flags |= 0x80 >> bit
                pair = (best - 3) << 12 | (best_back - 3)
                tokens += bytes([pair >> 8, pair & 0xFF])
                i += best
            else:
                tokens.append(r[i])
                i += 1
        tokens[flag_at] = flags
    body = bytes(tokens[::-1])
    header = 8
    packed = len(body) + header
    extra = len(plain) - packed
    assert extra > 0, "test data does not compress"
    return body + struct.pack("<II", header << 24 | packed, extra)


def patterned(rng: random.Random, size: int) -> bytes:
    """Data like code: runs of repeated words and some noise, so it compresses."""
    out = bytearray()
    words = [rng.getrandbits(32) for _ in range(12)]
    while len(out) < size:
        if rng.random() < 0.2:
            out += bytes(rng.getrandbits(8) for _ in range(rng.randint(1, 6)))
        else:
            out += struct.pack("<I", rng.choice(words))
    return bytes(out[:size])


def synthetic_rom(seed: int, game_code: bytes = b"ADMK") -> bytes:
    rng = random.Random(seed)
    main = bytearray(patterned(rng, 0x3000))
    autoloads = [(0x01FF8000, patterned(rng, 0x180), 0x20), (0x027E0000, patterned(rng, 0x60), 0x40),
                 (0x02380000, patterned(rng, 0x90), 0)]
    table_at, bi = 0x2000, 0x200
    for k, (base, data, bss) in enumerate(autoloads):
        struct.pack_into("<III", main, table_at + 12 * k, base, len(data), bss)
    al_start = ARM9_RAM + table_at
    struct.pack_into("<7I", main, bi, al_start, al_start + 12 * len(autoloads), ARM9_RAM + len(main),
                     ARM9_RAM + 0x4000, ARM9_RAM + 0x4800, 0, 0x05000000)
    main[bi + 0x1C:bi + 0x24] = MAGIC
    image = bytes(main) + b"".join(d for _, d, _ in autoloads)

    # the static image: the first 0x400 bytes (with the build information) stored, the rest compressed
    prefix = bytearray(image[:0x400])
    body = blz_compress(image[0x400:])
    struct.pack_into("<I", prefix, bi + 0x14, ARM9_RAM + len(prefix) + len(body))
    arm9 = bytes(prefix) + body

    plain_ov = patterned(rng, 0x500)
    packed_plain = patterned(rng, 0x800)
    packed_ov = blz_compress(packed_plain)
    padded_ov = patterned(rng, 0x300) + b"\xff" * 0x40  # its file is longer than the overlay
    overlays = [  # (id, ram, size, bss, file data, flags)
        (0, 0x02100000, len(plain_ov), 0x10, plain_ov, 0),
        (1, 0x02100000, len(packed_plain), 0x80, packed_ov, 1 << 24 | len(packed_ov)),
        (2, 0x02200000, 0x300, 0, padded_ov, 0),
    ]

    rom = bytearray(0x200)
    rom[0x0C:0x10] = game_code
    arm9_off = 0x4000
    ovt_off = arm9_off + len(arm9) + 0x100
    fat_off = ovt_off + 32 * len(overlays) + 0x100
    files_off = fat_off + 8 * len(overlays) + 0x100
    struct.pack_into("<IIII", rom, 0x20, arm9_off, ARM9_RAM + 0x800, ARM9_RAM, len(arm9))
    struct.pack_into("<I", rom, 0x48, fat_off)
    struct.pack_into("<II", rom, 0x50, ovt_off, 32 * len(overlays))
    rom += bytes(files_off - len(rom))
    rom[arm9_off:arm9_off + len(arm9)] = arm9
    at = files_off
    for k, (oid, ram, size, bss, data, flags) in enumerate(overlays):
        struct.pack_into("<8I", rom, ovt_off + 32 * k, oid, ram, size, bss, 0, 0, k, flags)
        struct.pack_into("<II", rom, fat_off + 8 * k, at, at + len(data))
        rom += data + bytes((-len(data)) % 0x200)
        at = len(rom)
    return bytes(rom)


def run(tool: str, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run([tool, *args], capture_output=True, text=True)


def check_split(tool: str, seed: int, work: pathlib.Path) -> None:
    rom = synthetic_rom(seed)
    path = work / f"rom{seed}.nds"
    path.write_bytes(rom)
    out = work / f"modules{seed}"
    p = run(tool, "--no-check", str(path), str(out))
    assert p.returncode == 0, p.stderr
    used, manifest = p.stdout.split("\n")[:2]
    assert used == str(path), used

    want = extract.split(rom)
    got = {}
    for line in pathlib.Path(manifest).read_text().splitlines():
        name, base, file = line.split(" ", 2)
        got[name] = (int(base, 16), pathlib.Path(file).read_bytes())
    assert list(got) == list(want), (list(got), list(want))
    for name, (base, image, _bss) in want.items():
        assert got[name][0] == base, f"{name}: base {got[name][0]:#x}, want {base:#x}"
        assert got[name][1] == image, f"{name}: {len(got[name][1])} bytes differ from extract.py's {len(image)}"
    print(f"seed {seed}: {len(want)} modules match extract.py ({', '.join(want)})")


def check_refusals(tool: str, work: pathlib.Path) -> None:
    rom = synthetic_rom(7, b"ADME")
    path = work / "other.nds"
    path.write_bytes(rom)
    p = run(tool, str(path), str(work / "refused"))
    assert p.returncode == 1, p
    assert hashlib.sha1(rom).hexdigest() in p.stderr, p.stderr  # the port's SHA-1 is hashlib's
    assert "ADME" in p.stderr and "North American" in p.stderr, p.stderr
    tiny = work / "tiny.nds"
    tiny.write_bytes(b"not a rom")
    p = run(tool, str(tiny), str(work / "tiny"))
    assert p.returncode == 1, p
    p = run(tool, str(work / "missing.nds"), str(work / "missing"))
    assert p.returncode == 1 and "no such file" in p.stderr, p.stderr
    print("refusals: another release, a file that is not a ROM, no file")


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    tool = sys.argv[1]
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp)
        for seed in (1, 2, 3):
            check_split(tool, seed, work)
        check_refusals(tool, work)
    print("ok")


if __name__ == "__main__":
    main()

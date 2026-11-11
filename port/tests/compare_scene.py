#!/usr/bin/env python3
"""The Vulkan renderer's picture of a frame against the software tiles' (docs/renderer.md).

    compare_scene.py OUT.rgba TILES.bin [SHEET.png]

OUT.rgba: acww-scene-vk's output (colour multiplied by alpha, and alpha). TILES.bin:
acww-kor --resolution N --shot F's tiles-F.bin, the 3D picture as the decomp's rasteriser
draws it in tiles. Prints how far apart they are; with SHEET.png (needs Pillow), writes the two
side by side over a checked ground, and their difference."""
import struct
import sys

import numpy as np


def load(out_path, tiles_path):
    raw = open(tiles_path, "rb").read()
    w, h = struct.unpack_from("<II", raw)
    t = np.frombuffer(raw, dtype="<u4", offset=8).reshape(h, w)
    a = ((t >> 24) & 31) / 31.0
    ref = np.stack([((t >> s) & 63) / 63.0 * a for s in (0, 8, 16)] + [a], axis=-1)
    out = np.frombuffer(open(out_path, "rb").read(), dtype=np.uint8).reshape(h, w, 4) / 255.0
    return out, ref


def main():
    out, ref = load(sys.argv[1], sys.argv[2])
    d = np.abs(out - ref)
    both = (out[..., 3] > 0.5) & (ref[..., 3] > 0.5)
    print("mean difference: colour %.4f, alpha %.4f; where both are opaque (%.0f%% of the picture): colour %.4f; "
          "pixels more than 0.25 apart: %.2f%%; covered: %.1f%% against %.1f%%"
          % (d[..., :3].mean(), d[..., 3].mean(), 100 * both.mean(), d[..., :3][both].mean() if both.any() else 0,
             100 * (d[..., :3].max(axis=-1) > 0.25).mean(), 100 * (out[..., 3] > 0.5).mean(), 100 * (ref[..., 3] > 0.5).mean()))
    if len(sys.argv) > 3:
        from PIL import Image
        h, w = out.shape[:2]
        yy, xx = np.mgrid[0:h, 0:w]
        ground = np.where(((xx // 16 + yy // 16) % 2)[..., None] == 0, 0.35, 0.5) * np.ones((1, 1, 3))
        def over(p):
            return np.clip(p[..., :3] + ground * (1 - p[..., 3:4]), 0, 1)
        sheet = np.concatenate([over(ref), over(out), np.clip(d[..., :3] * 4, 0, 1)], axis=1)
        Image.fromarray((sheet * 255).astype(np.uint8)).save(sys.argv[3])


if __name__ == "__main__":
    main()

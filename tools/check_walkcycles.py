#!/usr/bin/env python3
"""Verify that the first N PED blocks are valid 8-frame walk cycles.

For each block, prints a compact grid: 8 frames side by side, each rendered
as a fixed 16-wide box (bottom-anchored) so leg motion is readable.

Usage: python3 tools/check_walkcycles.py [city] [nblocks]
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from analyze_ped_frames import load, records

def render(g, recs, idx, W=16):
    w, h, dc, fl, ptr = recs[idx]
    page = ptr >> 16
    xo = (ptr & 0xFFFF) & 255
    yo = (ptr & 0xFFFF) >> 8
    gfx = g.spriteGfx
    rows = []
    for y in range(h):
        line = []
        for x in range(w):
            i = gfx[page * 65536 + (yo + y) * 256 + xo + x]
            line.append('#' if i != 0 else '.')
        rows.append(''.join(line))
    # pad to W, bottom-anchor into an 18-tall box
    box = []
    pad = 18 - len(rows)
    for _ in range(pad):
        box.append('.' * W)
    for r in rows:
        if len(r) < W:
            r = r + '.' * (W - len(r))
        box.append(r[:W])
    return box[int(W / 2) if False else len(box) - 18:len(box)]  # last 18

def main():
    city = sys.argv[1] if len(sys.argv) > 1 else 'NYC'
    nblk = int(sys.argv[2]) if len(sys.argv) > 2 else 8
    g = load(city)
    recs = records(g)
    ped_a = sum(g.spriteNumbers[0:7])
    for b in range(nblk):
        print(f'=== block {b} (idx {ped_a + b*8}..{ped_a + b*8 + 7}) ===')
        frames = [render(g, recs, ped_a + b * 8 + f) for f in range(8)]
        H = 18
        for r in range(H):
            line = ''
            for f in frames:
                line += (f[r] if r < len(f) else '.' * 16) + ' '
            print('   ' + line)

if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""ASCII-dump GRY sprites from the CORRECT spriteGfx section.

Usage: python3 tools/dump_ped.py [city] <idx...>
"""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from parse_formats import GRY, png as write_png
from analyze_ped_frames import load, records, sprite_rgba

def main():
    city = sys.argv[1] if not sys.argv[1].isdigit() else 'NYC'
    idxs = [int(a) for a in sys.argv[1:] if a.isdigit()]
    out = None
    for a in sys.argv[2:]:
        if not a.lstrip('-').isdigit():
            out = a
    g = load(city)
    recs = records(g)
    pal = bytes(((v << 2) | (v >> 4)) for v in g.pal)
    frames = []
    for idx in idxs:
        w, h, rgba = sprite_rgba(g, recs, idx)
        lines = []
        for y in range(h):
            line = ''
            for x in range(w):
                i = (y * w + x) * 4
                if rgba[i + 3] == 0:
                    line += ' '
                else:
                    r, gg, b = rgba[i], rgba[i+1], rgba[i+2]
                    if r > 200 and gg > 200 and b > 200: line += 'W'
                    elif r > 180 and gg > 140 and b < 120: line += 's'
                    elif max(r, gg, b) < 50: line += '#'
                    elif b > r and b > gg: line += 'b'
                    elif r > 140 and gg < 90 and b < 90: line += 'R'
                    else: line += '.'
            lines.append(line)
        frames.append((f'{idx}:{w}x{h}', lines, w, h, bytes(rgba)))
    H = max(len(f[1]) for f in frames)
    for r in range(H):
        print(' | '.join(f[1][r] if r < len(f[1]) else '' for f in frames))
    print(' | '.join(f[0] for f in frames))
    if out:
        w, h, rgba = frames[0][2], frames[0][3], frames[0][4]
        write_png(out, w, h, bytes(rgba))
        print('wrote', out)

if __name__ == '__main__':
    main()

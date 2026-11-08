#!/usr/bin/env python3
"""Dump one or more GRY sprites as PNG + ASCII (record layout = src/data/formats.cpp).

Usage: python3 tools/dump_sprite.py STYLE001.GRY <idx> [idx ...] [out.png for first]
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from parse_formats import GRY, png as write_png

def main():
    path = sys.argv[1]
    idxs = [int(a) for a in sys.argv[2:] if a.isdigit()]
    out = None
    for a in sys.argv[2:]:
        if not a.lstrip('-').isdigit():
            out = a
    g = GRY(path)
    d = g.d
    recs = []
    p = g.objInfoOff + g.objectInfoSize + g.carInfoSize
    end = p + g.spriteInfoSize
    while p + 10 <= end:
        w, h, dc = d[p], d[p+1], d[p+2]
        ptr = struct.unpack_from('<I', d, p+6)[0]
        recs.append((w, h, dc, ptr))
        p += 10 + dc * 6
    pal = bytes(((v << 2) | (v >> 4)) for v in g.pal)
    for idx in idxs:
        w, h, dc, ptr = recs[idx]
        page = ptr >> 16
        xo = (ptr & 0xFFFF) & 255
        yo = (ptr & 0xFFFF) >> 8
        base_px = page * 256 * 256
        rgba = bytearray()
        colors = {}
        for y in range(h):
            sy = yo + y
            for x in range(w):
                sx = xo + x
                i = d[base_px + sy * 256 + sx]
                rgba += bytes((pal[i*3], pal[i*3+1], pal[i*3+2], 0 if i == 0 else 255))
                colors[i] = colors.get(i, 0) + 1
        print(f'--- sprite {idx}: w{w} h{h} deltas={dc} page={page} off=({xo},{yo})')
        top = sorted(colors.items(), key=lambda kv: -kv[1])[:8]
        print('    top idx->rgb,count:',
              [(i, pal[i*3:i*3+3], c) for i, c in top])
        for y in range(0, h, 2):
            row = ''
            for x in range(0, w, 1):
                i = (y * w + x) * 4
                if rgba[i+3] == 0:
                    row += ' '
                else:
                    r, gg, b = rgba[i], rgba[i+1], rgba[i+2]
                    if r > 150 and gg > 120 and b < 90: row += 'Y'
                    elif r > 140 and gg > 140 and b > 140: row += 'W'
                    elif r > 90 and gg > 90 and b < 70: row += 'y'
                    elif max(r, gg, b) < 45: row += '#'
                    elif b > r and b > gg: row += 'b'
                    elif g_ := gg > r and gg > b: row += 'g'
                    else: row += '.'
            print('    ' + row)
        if out and idx == idxs[0]:
            write_png(out, w, h, bytes(rgba))
            print('    wrote', out)

if __name__ == '__main__':
    main()

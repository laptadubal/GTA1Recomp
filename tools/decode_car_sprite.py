#!/usr/bin/env python3
"""Decode a car sprite from a GRY to PNG + ASCII map.

Usage: python3 tools/decode_car_sprite.py STYLE001.GRY <sprNum> [out.png]
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from parse_formats import GRY, png as write_png

def main():
    path, spr_num = sys.argv[1], int(sys.argv[2])
    out = sys.argv[3] if len(sys.argv) > 3 else None
    g = GRY(path)
    d = g.d
    sprites_off = g.objInfoOff + g.objectInfoSize + g.carInfoSize

    base = sum(g.spriteNumbers[:5]) + spr_num
    # walk records to reach base
    p = sprites_off
    recs = []
    while len(recs) <= base:
        w = d[p]; h = d[p+1]; dc = d[p+2]; fl = d[p+3]
        size, ptr = struct.unpack_from('<IH', d, p+4)[0], struct.unpack_from('<I', d, p+6)[0]
        recs.append((w, h, dc, fl, ptr))
        p += 10 + dc * 6
    w, h, dc, fl, ptr = recs[base]
    page = ptr >> 16
    xo = (ptr & 0xFFFF) & 0xFF
    yo = (ptr & 0xFFFF) >> 8
    print(f'sprNum {spr_num}: sprite {base}, w={w} h={h} deltas={dc} page={page} xoff={xo} yoff={yo}')
    base_px = page * 256 * 256
    pal = bytes(((v << 2) | (v >> 4)) for v in g.pal)
    rgba = bytearray()
    key = 0
    for y in range(h):
        sy = yo + y
        for x in range(w):
            sx = xo + x
            idx = d[base_px + sy * 256 + sx]
            rgba += bytes((pal[idx*3], pal[idx*3+1], pal[idx*3+2], 0 if idx == 0 else 255))
            if idx == 0:
                key += 1
    total = w * h
    print(f'key pixels: {key}/{total} = {key/total:.2%}')
    for y in range(h):
        row = ''
        for x in range(w):
            i = (y * w + x) * 4
            if rgba[i+3] == 0:
                row += '.'
            else:
                r, gg, b = rgba[i], rgba[i+1], rgba[i+2]
                if r > 150 and gg > 120 and b < 90: row += 'Y'
                elif r > 120 and gg > 100 and b < 90: row += 'y'
                elif max(r,gg,b) < 40: row += 'k'
                else: row += '#'
        print(row)
    if out:
        write_png(out, w, h, bytes(rgba))
        print('wrote', out)

if __name__ == '__main__':
    main()

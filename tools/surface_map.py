#!/usr/bin/env python3
"""Print a surface-tile luminance map around a point.

For each cell it finds the top-most block and reports the lid's mean
luminance (or the block type if there is no lid).  Dark cells (low lum)
are the likely 'black tile' patches.

Usage: python3 tools/surface_map.py <cmp> <gry> <cx> <cz> <radius>
"""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from parse_formats import CMP, GRY


def tile_lum(g, which, i):
    if i == 0:
        return None
    rgba = g.rgba(g.tile(which, i))
    s = 0
    n = 0
    for k in range(0, len(rgba), 4):
        a = rgba[k + 3]
        if a == 0:
            continue
        s += (rgba[k] + rgba[k + 1] + rgba[k + 2]) / 3
        n += 1
    return s / n if n else 0.0


def slope_of(tm):
    return (int(tm & 256) + int(tm & 512) + int(tm & 1024) +
            int(tm & 2048) + int(tm & 4096) + int(tm & 8192))


def main():
    cmp_, g, cx, cz, R = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])
    c = CMP(cmp_)
    g = GRY(g)
    g2 = g
    # legend: 0-99 lum -> space..@ ; A=air, O=block no lid, digits=slope
    print(f'surface luminance map around ({cx},{cz}) r={R}  ( .<10 -<20 +<40 =<60 _<90 :<120 #<170 %<220 @>=220 )')
    for y in range(cz - R, cz + R + 1):
        line = ''
        for x in range(cx - R, cx + R + 1):
            n = c.num_blocks(x, y)
            if n == 0:
                line += '.'
                continue
            b = c.block_at(x, y, n - 1)  # top-most
            s = slope_of(b[0])
            if s:
                line += 'A' if s >= 41 else 'r'
            elif b[6]:
                lum = tile_lum(g2, 'lid', b[6])
                if lum is None:
                    line += 'O'
                else:
                    if lum < 10: line += ' '
                    elif lum < 20: line += '-'
                    elif lum < 40: line += '+'
                    elif lum < 60: line += '='
                    elif lum < 90: line += '_'
                    elif lum < 120: line += ':'
                    elif lum < 170: line += '#'
                    elif lum < 220: line += '%'
                    else: line += '@'
            else:
                line += 'O'
        print(f'{y:3d}: {line}')
    # list the distinct dark lids used within the map
    dark = {}
    for y in range(cz - R, cz + R + 1):
        for x in range(cx - R, cx + R + 1):
            n = c.num_blocks(x, y)
            if n == 0:
                continue
            b = c.block_at(x, y, n - 1)
            if b[6] and not slope_of(b[0]):
                lum = tile_lum(g2, 'lid', b[6])
                if lum is not None and lum < 40:
                    dark[b[6]] = dark.get(b[6], 0) + 1
    print('dark (<40 lum) lid ids used on surface in map:', dark)


if __name__ == '__main__':
    main()

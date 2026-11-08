#!/usr/bin/env python3
"""Dump a world region: block count, top-block slope, and lid, for a CMP file.

Usage: python3 tools/dump_region.py <cmp> x0 x1 y0 y1
Prints one line per y: per-x char
  . = no blocks, o = block w/o lid, # = block w/ lid,
  S = slope 1..40 (ramp), D = slope 41..44 (45deg), digit variants show slope class
Also prints, for cells requested via --cells "x,y;x,y", the full column.
"""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from parse_formats import CMP


def slope_of(tm):
    return (int(tm & 256) + int(tm & 512) + int(tm & 1024) +
            int(tm & 2048) + int(tm & 4096) + int(tm & 8192))


def blocktype(tm):
    return (int(bool(tm & 16)) + int(bool(tm & 32)) + int(bool(tm & 64)))


def main():
    cmp_path, xa, xb, ya, yb = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])
    c = CMP(cmp_path)
    print(f'== {cmp_path} region x{xa}..{xb} y{ya}..{yb} ==')
    print('   x: ' + ' '.join(f'{x%100:1d}' for x in range(xa, xb + 1)))
    for y in range(ya, yb + 1):
        line = ''
        for x in range(xa, xb + 1):
            n = c.num_blocks(x, y)
            if n == 0:
                line += '.'
                continue
            b = c.block_at(x, y, n - 1)  # top-most block (z=n-1 = highest level)
            s = slope_of(b[0])
            bt = blocktype(b[0])
            if s >= 41:
                line += 'D'
            elif s >= 1:
                line += 's' if bt == 2 else 'S'
            else:
                line += '#' if b[6] else 'o'
        print(f'{y:3d}: {line}')
    # slope map for detail
    cells = [i for i in range(xa * 256 + ya, (xb + 1) * 256 + yb + 1) if False]
    slopes = {}
    for y in range(ya, yb + 1):
        for x in range(xa, xb + 1):
            n = c.num_blocks(x, y)
            for z in range(n):
                b = c.block_at(x, y, z)
                s = slope_of(b[0])
                if s:
                    slopes[(x, y, n - z)] = s
    print('nonzero-slope cells (x,y,surfLevel->slope):')
    for (x, y, zl), s in sorted(slopes.items()):
        print(f'   ({x},{y}) level {zl}: slope {s}')


if __name__ == '__main__':
    main()

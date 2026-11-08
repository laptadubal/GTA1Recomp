#!/usr/bin/env python3
"""Rank the tiles a city actually uses by mean luminance and black fraction.

Surfaces 'dark' tiles (not necessarily 100% black) that could read as black
patches on the map.  A tile counts once per reference; we report the dark
ones and how often each is used.

Usage: python3 tools/audit_dark_tiles.py [CITY ...]
"""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from collections import Counter
from parse_formats import CMP, GRY

DATA = os.path.join(os.path.dirname(__file__), '..', 'game_data', 'GTADATA')
CITIES = {'NYC': 'NYC.CMP', 'SANB': 'SANB.CMP', 'MIAMI': 'MIAMI.CMP'}


def tile_stats(rgba, w=64, h=64):
    n = w * h
    black = 0
    opaque = 0
    lum = 0
    for i in range(n):
        r, g, b, a = rgba[i*4], rgba[i*4+1], rgba[i*4+2], rgba[i*4+3]
        if a == 0:
            continue
        opaque += 1
        if r < 12 and g < 12 and b < 12:
            black += 1
        lum += (r + g + b) / 3
    if opaque == 0:
        return 0.0, 0.0
    return black / opaque, lum / opaque


def used_tiles(cmp_):
    lids, sides = Counter(), Counter()
    for x in range(256):
        for y in range(256):
            b = cmp_.base[x*256+y] // 2
            n = cmp_.column[b]
            if n == 0:
                continue
            for z in range(cmp_.num_blocks(x, y)):
                blk = cmp_.blocks[cmp_.column[b + cmp_.num_blocks(x, y) - z]]
                if blk[6]:
                    lids[blk[6]] += 1
                for f in (blk[2], blk[3], blk[4], blk[5]):
                    if f:
                        sides[f] += 1
    return lids, sides


def main():
    only = sys.argv[1:] or list(CITIES)
    for city in only:
        cmp_ = CMP(os.path.join(DATA, CITIES[city]))
        style = cmp_.style
        g = GRY(os.path.join(DATA, f'STYLE00{style}.GRY'))
        lids, sides = used_tiles(cmp_)
        print(f'=== {city} (style {style}) ===')
        for name, ids, which, limit in (
                ('lid', lids, 'lid', g.nlid), ('side', sides, 'side', g.nside)):
            rows = []
            for i in sorted(ids):
                if i > limit:
                    print(f'  {name} id {i} OUT-OF-RANGE uses={ids[i]}')
                    continue
                frac, lum = tile_stats(g.rgba(g.tile(which, i)))
                rows.append((lum, frac, i, ids[i]))
            rows.sort()
            print(f'  {name}: {len(rows)} distinct used tiles. Darkest 15:')
            for lum, frac, i, uses in rows[:15]:
                print(f'    id {i:4d}  lum={lum:5.1f}  black={frac*100:5.1f}%  uses={uses}')
        print()


if __name__ == '__main__':
    main()

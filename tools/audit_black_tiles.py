#!/usr/bin/env python3
"""Find black / near-black map tiles.

CMP+GRY are the single source of truth. This script tells us which tile ids the
cities actually reference, and which of those tiles decode to (nearly) all-black.
A 100%-black tile that a city uses is either (a) faithful dark art, or (b) an
unallocated slot in the GRY that the original engine would not reference.

Usage: python3 tools/audit_black_tiles.py            (all cities)
       python3 tools/audit_black_tiles.py NYC SANB MIAMI
"""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from collections import Counter
from parse_formats import CMP, GRY

DATA = os.path.join(os.path.dirname(__file__), '..', 'game_data', 'GTADATA')
CITIES = {'NYC': 'NYC.CMP', 'SANB': 'SANB.CMP', 'MIAMI': 'MIAMI.CMP'}

def tile_stats(rgba, w=64, h=64):
    # Only OPAQUE pixels count (palette index 0 is the transparent colour key).
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

def main():
    only = sys.argv[1:] or list(CITIES)
    for city in only:
        cmp_ = CMP(os.path.join(DATA, CITIES[city]))
        style = cmp_.style
        g = GRY(os.path.join(DATA, f'STYLE00{style}.GRY'))
        lids, sides = Counter(), Counter()
        for x in range(256):
            for y in range(256):
                b = cmp_.base[x*256+y] // 2
                n = cmp_.column[b]
                if n == 0:
                    continue
                for z in range(n):
                    tm, ext, L, R, T, B, lid = cmp_.blocks[cmp_.column[b+n-z]]
                    if lid:
                        lids[lid] += 1
                    for f in (L, R, T, B):
                        if f:
                            sides[f] += 1
        print(f'=== {city} (style {style}, GRY {g.nside} side / {g.nlid} lid tiles) ===')
        oob_l = [i for i in lids if i > g.nlid]
        oob_s = [i for i in sides if i > g.nside]
        if oob_l:
            print(f'  OUT-OF-RANGE lid ids in CMP: {sorted(oob_l)} (uses: {[lids[i] for i in sorted(oob_l)]})')
        if oob_s:
            print(f'  OUT-OF-RANGE side ids in CMP: {sorted(oob_s)} (uses: {[sides[i] for i in sorted(oob_s)]})')
        def report(name, ids, which, limit):
            bad, oob = [], [i for i in ids if i > limit]
            for i in sorted(ids):
                frac, lum = tile_stats(g.rgba(g.tile(which, i)))
                if i > limit:
                    print(f'    #{name} id {i}: OUT OF RANGE (GRY has {limit})')
                    continue
                if i not in oob and (frac > 0.99 or lum < 6):
                    bad.append((i, round(frac, 4), round(lum, 1), ids[i]))
            if bad:
                print(f'    #{name} black/near-black used tiles (id, black%, lum, uses): {bad}')
            else:
                print(f'    #{name} no fully-black used tiles')
        report('lid', lids, 'lid', g.nlid)
        report('side', sides, 'side', g.nside)
        print()

if __name__ == '__main__':
    main()

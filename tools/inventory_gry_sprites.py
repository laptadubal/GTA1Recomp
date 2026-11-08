#!/usr/bin/env python3
"""Full GRY sprite-table inventory (record layout verified against src/data/formats.cpp).

Usage: python3 tools/inventory_gry_sprites.py [city ...]   (NYC SANB MIAMI)
Prints sprite ranges with w/h/delta stats and flags distinctive groups
(cars / objects / peds / player candidates / effects).
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from parse_formats import GRY

DATA = os.path.join(os.path.dirname(__file__), '..', 'game_data', 'GTADATA')

def records(g):
    d = g.d
    p = g.objInfoOff + g.objectInfoSize + g.carInfoSize
    end = p + g.spriteInfoSize
    recs = []
    while p + 10 <= end:
        w, h, dc, fl = d[p], d[p+1], d[p+2], d[p+3]
        size, ptr = struct.unpack_from('<HI', d, p+4)
        recs.append((w, h, dc, fl, ptr))
        p += 10 + dc * 6
    return recs

def main():
    cities = sys.argv[1:] or ['NYC', 'SANB', 'MIAMI']
    for city in cities:
        style = {'NYC': 1, 'SANB': 2, 'MIAMI': 3}[city]
        g = GRY(os.path.join(DATA, f'STYLE00{style}.GRY'))
        recs = records(g)
        counts = list(g.spriteNumbers)
        print(f'=== {city} STYLE00{style}: {len(recs)} sprites, '
              f'spriteNumbers={counts}')
        # cumulative start offsets of the 21 groups
        starts = [0]
        for c in counts:
            starts.append(starts[-1] + c)
        for gi in range(21):
            a, b = starts[gi], starts[gi+1] - 1
            if a >= len(recs) or counts[gi] == 0:
                if a < len(recs) and a <= b:
                    print(f'  group {gi:2d}: (empty count 0)')
                continue
            if b >= len(recs):
                b = len(recs) - 1
            seg = recs[a:b+1]
            ws = [r[0] for r in seg]; hs = [r[1] for r in seg]
            dcs = [r[2] for r in seg]
            nz = sum(1 for r in seg if r[0] and r[1])
            print(f'  group {gi:2d}: idx {a:4d}..{b:4d} n={counts[gi]:3d} '
                  f'w {min(ws)}-{max(ws)} h {min(hs)}-{max(hs)} '
                  f'deltas {min(dcs)}-{max(dcs)} nonempty={nz}')
        # detailed dump of the last groups (player/effects candidates)
        print('  detail 915+. (w,h,deltas):')
        for i in range(915, len(recs)):
            w, h, dc, fl, ptr = recs[i]
            if w or h or dc:
                print(f'    {i}: w{w} h{h} dc{dc} fl{fl}')

if __name__ == '__main__':
    main()

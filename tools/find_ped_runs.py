#!/usr/bin/env python3
"""Find valid 8-frame runs in the PED group (candidate walk cycles).

A run s..s+7 is 'valid' when every frame looks like an upright humanoid of
consistent size (width 8..26, height 10..30, ink 40..400, size variance <= 8).

Usage: python3 tools/find_ped_runs.py [city]
"""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from analyze_ped_frames import load, records, sprite_rgba

def main():
    city = sys.argv[1] if len(sys.argv) > 1 else 'NYC'
    g = load(city)
    recs = records(g)
    ped_a = sum(g.spriteNumbers[0:7])
    ped_b = ped_a + g.spriteNumbers[7] - 1
    # frame stats
    info = {}
    for i in range(ped_a, ped_b + 1):
        w, h, rgba = sprite_rgba(g, recs, i)
        ink = sum(1 for j in range(0, len(rgba), 4) if rgba[j + 3])
        info[i] = (w, h, ink)
    runs = []
    for s in range(ped_a, ped_b - 7 + 1):
        ok = True
        hs = []
        ws = []
        for i in range(s, s + 8):
            w, h, ink = info[i]
            if not (8 <= w <= 26 and 10 <= h <= 30 and 40 <= ink <= 420):
                ok = False
                break
            hs.append(h)
            ws.append(w)
        if ok and (max(hs) - min(hs)) <= 10 and (max(ws) - min(ws)) <= 10:
            runs.append(s)
    # merge overlapping runs into ranges
    ranges = []
    for s in runs:
        if ranges and s <= ranges[-1][1] + 2:
            ranges[-1][1] = max(ranges[-1][1], s + 7)
        else:
            ranges.append([s, s + 7])
    print(f'{city}: {len(runs)} valid starts, merged into {len(ranges)} ranges:')
    for a, b in ranges:
        print(f'  {a}..{b}')

if __name__ == '__main__':
    main()

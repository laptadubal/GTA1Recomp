#!/usr/bin/env python3
"""Match F_PLAY1..8 portraits to PED 8-frame blocks by color signature.

Usage: python3 tools/match_portraits.py [city]
"""
import sys, os, collections
sys.path.insert(0, os.path.dirname(__file__))
from parse_formats import read, png as write_png
from analyze_ped_frames import load, records, sprite_rgba

DATA = os.path.join(os.path.dirname(__file__), '..', 'game_data', 'GTADATA')

def signature(rgba, w, h, region='torso'):
    """Dominant colors of the torso region (skip transparent, top 25% head)."""
    cnt = collections.Counter()
    for y in range(h):
        if region == 'torso' and y < h // 4:
            continue
        for x in range(w):
            i = (y * w + x) * 4
            if rgba[i + 3] == 0:
                continue
            cnt[bytes(rgba[i:i+3])] += 1
    return cnt

def main():
    city = sys.argv[1] if len(sys.argv) > 1 else 'NYC'
    g = load(city)
    recs = records(g)
    pal = bytes(((v << 2) | (v >> 4)) for v in g.pal)
    # F_PAL.RAW is a plain 256xRGB palette (768 bytes), unlike the packed
    # 6-bit GRY palette.
    fpal = read(os.path.join(DATA, 'F_PAL.RAW'))
    fpa = fpal

    ped_a = sum(g.spriteNumbers[0:7])
    # portraits
    print('=== portraits ===')
    port_sigs = {}
    for n in range(1, 9):
        rat = read(os.path.join(DATA, f'F_PLAY{n}.RAT'))
        w, h = 102, 141
        rgba = bytearray()
        for i in rat:
            rgba += bytes((fpa[i*3], fpa[i*3+1], fpa[i*3+2], 0 if i in (0, 255) and False else 255))
        # F_PAL idx 255 = white bg? keep all opaque, background check later
        cnt = signature(rgba, w, h)
        port_sigs[n] = cnt
        top = cnt.most_common(5)
        print(f'F_PLAY{n}: ' + ', '.join(f'{tuple(c)}x{k}' for c, k in top))

    # ped blocks: frame 0 of each of first 12 blocks
    print('=== ped blocks (frame 0) ===')
    for b in range(12):
        idx = ped_a + b * 8
        if idx >= ped_a + g.spriteNumbers[7]:
            break
        w, h, rgba = sprite_rgba(g, recs, idx)
        cnt = signature(rgba, w, h)
        top = cnt.most_common(5)
        line = f'block {b} (idx {idx}): ' + ', '.join(f'{tuple(c)}x{k}' for c, k in top)
        # match score vs each portrait
        best = []
        for n, pcnt in port_sigs.items():
            common = sum(min(k, pcnt[c]) for c, k in cnt.items()
                         if c in pcnt)
            tot = min(sum(cnt.values()), sum(pcnt.values()))
            best.append((n, round(common / max(1, tot) * 100)))
        best.sort(key=lambda t: -t[1])
        line += f'   matches: {best[:3]}'
        print(line)

if __name__ == '__main__':
    main()

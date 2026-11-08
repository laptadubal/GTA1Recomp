#!/usr/bin/env python3
"""Order check: match F_PLAY1..8 to the first 8 character windows using ONLY
colored (non-gray, non-dark) coarse keys, weighted overlap.

Usage: python3 tools/check_order.py [city]
"""
import sys, os, colorsys, collections
sys.path.insert(0, os.path.dirname(__file__))
from parse_formats import read
from analyze_ped_frames import load, records, sprite_rgba

def ckey(r, g, b):
    h, s, v = colorsys.rgb_to_hsv(r / 255.0, g / 255.0, b / 255.0)
    if s < 0.28:
        return None
    if v < 0.2:
        return None
    return (int(h * 6), int(s * 2), int(v * 2))

def main():
    city = sys.argv[1] if len(sys.argv) > 1 else 'NYC'
    wins = {'NYC': [620, 628, 639, 658, 678, 687, 695, 703],
            'SANB': [622, 630, 641, 660, 680, 689, 697, 705],
            'MIAMI': [635, 643, 654, 673, 693, 702, 710, 718]}
    g = load(city)
    recs = records(g)
    fpal = read(os.path.join(os.path.dirname(__file__), '..', 'game_data', 'GTADATA', 'F_PAL.RAW'))

    def keys_rgba(rgba):
        cnt = collections.Counter()
        for i in range(0, len(rgba), 4):
            if rgba[i + 3] == 0:
                continue
            k = ckey(rgba[i], rgba[i+1], rgba[i+2])
            if k:
                cnt[k] += 1
        return cnt

    # windows (torso: bottom 75% of frame)
    win = []
    for w0 in wins[city]:
        cnt = collections.Counter()
        for f in range(8):
            w, h, rgba = sprite_rgba(g, recs, w0 + f)
            sub = bytearray()
            for y in range(h // 4, h):
                for x in range(w):
                    i = (y * w + x) * 4
                    sub += rgba[i:i + 4]
            c = keys_rgba(bytes(sub))
            for k, v in c.items():
                cnt[k] += v
        win.append(cnt)

    for n in range(8):
        rat = read(os.path.join(os.path.dirname(__file__), '..', 'game_data', 'GTADATA', f'F_PLAY{n+1}.RAT'))
        w, h = 102, 141
        cnt = collections.Counter()
        for y in range(h // 4, h):
            for x in range(w):
                c = rat[y * w + x]
                k = ckey(fpal[c * 3], fpal[c * 3 + 1], fpal[c * 3 + 2])
                if k:
                    cnt[k] += 1
        top = cnt.most_common(5)
        scores = []
        for wi, wc in enumerate(win):
            tot = min(sum(cnt.values()), sum(wc.values()))
            if tot == 0:
                scores.append((0, wi))
                continue
            sc = sum(min(cnt.get(k, 0), wc.get(k, 0)) for k in set(cnt) | set(wc)) / tot
            scores.append((sc, wi))
        scores.sort(reverse=True)
        print(f'F_PLAY{n+1}: {top}')
        print('   ', '  '.join(f'w{wi}:{sc:.2f}' for sc, wi in scores[:4]))

if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Find character-consistent 8-frame windows in the PED group using
size + color-signature stability across all 8 frames.

A window scores high when every frame is an upright humanoid and the 8
frames share a stable clothing color histogram (same person).

Usage: python3 tools/find_chars.py [city] [min_score]
"""
import sys, os, colorsys, collections
sys.path.insert(0, os.path.dirname(__file__))
from analyze_ped_frames import load, records, sprite_rgba

def ckey(r, g, b):
    h, s, v = colorsys.rgb_to_hsv(r / 255.0, g / 255.0, b / 255.0)
    if s < 0.25:
        return ('g', int(v * 2))
    if v < 0.18:
        return ('d',)
    return ('c', int(h * 6), int(s * 2), int(v * 2))

def hist(rgba, w):
    cnt = collections.Counter()
    n = 0
    for i in range(0, len(rgba), 4):
        if rgba[i + 3] == 0:
            continue
        cnt[ckey(rgba[i], rgba[i+1], rgba[i+2])] += 1
        n += 1
    if n == 0:
        return {}, 0
    # top-5 keys as the signature, keep counts
    return dict(cnt.most_common(5)), n

def main():
    city = sys.argv[1] if len(sys.argv) > 1 else 'NYC'
    min_score = float(sys.argv[2]) if len(sys.argv) > 2 else 0.6
    g = load(city)
    recs = records(g)
    ped_a = sum(g.spriteNumbers[0:7])
    ped_b = ped_a + g.spriteNumbers[7] - 1
    data = {}
    for i in range(ped_a, ped_b + 1):
        w, h, rgba = sprite_rgba(g, recs, i)
        sig, ink = hist(rgba, w)
        data[i] = (w, h, ink, sig)
    scores = []
    for s in range(ped_a, ped_b - 7 + 1):
        ok = True
        hmax = hmin = wmax = wmin = 0
        sigs = []
        for i in range(s, s + 8):
            w, h, ink, sig = data[i]
            if not (8 <= w <= 26 and 10 <= h <= 30 and 40 <= ink <= 420):
                ok = False
                break
            hmax = max(hmax, h); hmin = min(hmin, h) if hmin else h
            wmax = max(wmax, w); wmin = min(wmin, w) if wmin else w
            sigs.append(sig)
        if not ok or (hmax - hmin) > 10 or (wmax - wmin) > 10:
            continue
        # color stability: pairwise similarity of signatures (Jaccard-ish on top keys weighted)
        sim_total = 0.0
        pairs = 0
        for a in range(8):
            for bidx in range(a + 1, 8):
                sa, sb = sigs[a], sigs[bidx]
                if not sa or not sb:
                    sim_total -= 0.3
                    pairs += 1
                    continue
                ka = set(sa) | set(sb)
                common = sum(min(sa.get(k, 0), sb.get(k, 0)) for k in ka)
                tot = sum(sa.values()) + sum(sb.values())
                sim_total += common / max(1, tot) * 2
                pairs += 1
        sc = sim_total / pairs
        scores.append((sc, s))
    scores.sort(reverse=True)
    # greedy non-overlapping pick
    picked = []
    for sc, s in scores:
        if sc < min_score:
            break
        if all(s + 8 <= p or s > p + 7 for p in picked):
            picked.append(s)
    picked.sort()
    print(f'{city}: {len(picked)} character windows (score>={min_score}):')
    print('  ' + ' '.join(str(s) for s in picked))
    # show scores around known sets
    for name, s in (('player', 620), ('flame', 738), ('gun', 756), ('rocket', 774), ('cop', 811)):
        m = [ (sc, ss) for sc, ss in scores if ss == s ]
        print(f'  known {name} @{s}: score = {m[0][0] if m else "n/a":.3f}')

if __name__ == '__main__':
    main()

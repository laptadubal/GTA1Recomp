#!/bin/zsh
# Sweep the three cities with fixed camera spawns and capture a screenshot grid.
# Each run is ~10s; total ~5-8 min. Output: /tmp/sweep_<CITY>_<x>_<y>.png
cd /Users/kitbag/Documents/DOS_GTA1_RecompProject/DOS_GAME_RECOMP || exit 1
OUT=${1:-/tmp}
pkill -f dos_gta_recomp; sleep 1
for city in NYC SANB MIAMI; do
  for x in 64 128 192; do
    for y in 64 128 192; do
      GTA_NOSWAP=1 ./build/dos_gta_recomp --data game_data/GTADATA --city $city \
        --screen game --spawn $x,$y --screenshot $OUT/sweep_${city}_${x}_${y}.bmp \
        --frames 12 --fixed-dt 0.016 >/dev/null 2>&1
      sips -s format png $OUT/sweep_${city}_${x}_${y}.bmp --out $OUT/sweep_${city}_${x}_${y}.png >/dev/null 2>&1
      rm -f $OUT/sweep_${city}_${x}_${y}.bmp
    done
  done
done
echo "sweep done: $(ls $OUT/sweep_*.png 2>/dev/null | wc -l | tr -d ' ') frames"

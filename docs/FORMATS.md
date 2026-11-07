# GTA DOS data formats used by this engine

These notes document the subset of the original formats that the reimplementation
reads. They were validated against the retail files and cross-checked with the
DMA CityScape Data Structure specification and OpenGTA. All integers are
little-endian.

## CMP v331 — city map

```
offset  size  field
0       4     version (331)
4       1     style        (used to pick STYLE00N.GRY)
5       1     sample
6       2     reserved
8       4     routeSize
12      4     objectPosSize
16      4     columnSize    (bytes)
20      4     blockSize     (bytes, 8 bytes per block)
24      4     navDataSize
28      262144  base grid: 256*256 uint32 offsets into the column array
```

Layout after the header:

1. `base[256*256]` (`uint32`, byte offsets, always even).
2. `column[columnSize/2]` (`uint16`).
3. `blocks[blockSize/8]` (`uint8[8]` per record).
4. Object positions (`objectPosSize / 14` records of 14 bytes).
5. Route records (`routeSize` bytes), then 36 location vertices (108 bytes),
   then the `navDataSize` sector records.

### Column lookup

`column[base[y * 256 + x] / 2]` holds the number of **empty levels above the
column**, not the block count.  GTA1 columns are six levels tall, so the real
height is `n = 6 - column[index]`; a value of 0 means six blocks and 3 means
three.  The blocks are stored **top to bottom** after that value, so the bottom
block (level 0) is `column[index + n]` and the top block is `column[index + 1]`.

`blockAt(x, y, z)` in `src/data/formats.h` uses `z = 0` for the lowest block
and the engine's map frame:
`column[index + n - z]`.

The CMP base is column-major relative to its stored dimensions. All game
coordinates use `base[y * 256 + x]`; using `base[x * 256 + y]` transposes the
whole city. `tools/audit_map_orientation.py` compares both interpretations with
MISSION.INI phones, CMP route coordinates and a Liberty City reference.

Reading the stored value as a block count (an easy mistake) truncates every
column whose value is 1 or 2 and over-reads columns whose value is 4 or 5,
which makes road surfaces and building heights come from the wrong level.
The engine renders water, land, pavements and bridge decks at one flat base
height (`World::visualHeight`) and extrudes only columns that rise clearly
above it, which matches the original's flat-ground look.

### Block record (8 bytes)

```
0  uint16 typeMap
2  uint8  typeMapExt
3  uint8  left      side texture index (1-based, 0 = none)
4  uint8  right
5  uint8  top
6  uint8  bottom
7  uint8  lid       roof/road texture index (1-based, 0 = none)
```

`typeMapExt` bits 0..2 are traffic-light data, bits 3..4 select lid remap
selector 0..3 (selectors 1..3 resolve through that tile's GRY
`remap_index` row), bit 5 flips top/bottom, bit 6 flips left/right, and bit 7
marks railway. The selector is not a direct remap-table number, and remaps do
not apply to side faces.

`typeMap` helpers:

* bits 0..3: direction permissions for AI vehicles (up, down, left, right)
* bits 4..6: block type (0 = air, 1 = water, 2 = road, 3 = pavement,
  4 = field, 5 = building, 6..7 unused)
* bit 7: flat/transparent flag
* bits 8..13: slope type
* bits 14..15: rotation

Lid rotation is applied as a cyclic offset of the four UV corners in map
space, `(rotation + cornerIndex) & 3`; it must not reverse the corner order.
This matches the independent OpenGTA renderer's `gl_cityview.cpp` mapping and
the DMA definition of quarter turns.

The block type, direction bits, railway and traffic-light flags for a graphic
are usually stored in the block directly above the block that carries the
graphic. Retail data also has road cells where the visible lid block itself is
type 2. An explicit type 2 on the visible lid takes precedence over a different
type above; otherwise the nonzero type above supplies the surface class. The
engine uses the resolved type-2 road tag, so gray
pavement tiles are not mistaken for roads. Pedestrian passability remains a
separate check; it can allow pavement and other walkable ground without turning
them into vehicle lanes. The road tile classifier remains useful for visual
cleanup of plain asphalt, where lane paint is absent.

### CMP routes and locations

Each route record is `u8 vertexCount, u8 routeType`, followed by
`vertexCount * (u8 x, u8 y, u8 z)`. Route types `0..253` are numbered roadblock
routes referenced by traffic-light boxes; type `255` is a police route. Type
`254` occurs once in NYC but is not classified by the DMA specification, which
defines roadblock IDs only through 253. Do not treat these records as ordinary
civilian traffic or pedestrian routes. See [§2.6 of the DMA specification](https://misc.daniel-marschall.de/spiele/gta1/cds.pdf).

After the route bytes, the CMP stores 36 `(x,y,z)` location vertices (police,
hospital and fire service positions), followed by 35-byte navigation-sector
records. `loadCityMap()` parses the route records and sectors; the location
vertices are not yet exposed in `CityMap`.

### Object record (14 bytes)

```
0  uint16 x, 2 y, 4 z   (8.8 fixed point)
6  uint8 type
7  uint8 remap
8  uint16 rotation
10 uint16 pitch
12 uint16 roll
```

`objectPosSize` is a multiple of 14, not 16. Striding by 16 silently
misaligns every record after the first (objects pile up at the map corner
and props land in the wrong streets). The record is exactly 14 bytes with no
padding. `type` / `remap` select the GRY object (or car) sprite via the
object-info and car-info tables; `remap >= 128` marks a parked car. Retail
traffic-light boxes (object type 16) are special: their `rotation` field is a
roadblock route ID, not a rotation angle. Their associated type `0..253` CMP
route contains junction waypoints. Objects whose GRY status is 3 are invisible
logic markers and must not be drawn as sprites.

## GRY v325 — graphics archive

```
offset  size  field
0       4     version (325)
4       4     sideSize          (bytes, multiple of 4096)
8       4     lidSize
12      4     auxSize
16      4     animSize
20      4     paletteSize       (768)
24      4     remapSize         (256 * 256)
28      4     remapIndexSize    (1024)
32      4     objectInfoSize
36      4     carInfoSize
40      4     spriteInfoSize
44      4     spriteGraphicsSize
48      4     spriteNumberSize  (42 = 21 uint16 groups)
52      ...   side tiles, lid tiles, aux tiles
```

### Tile storage

Tiles are interleaved in groups of four. For global slot `s`:

```
group  = s / 4
column = s % 4
offset = group * 16384 + column * 64
row y  = offset + y * 256      // 64 bytes per row
```

The tile sections are concatenated. Map indices are 1-based:

* side tile `N`  → slot `N`
* lid tile `N`   → slot `numSide() + N`
* aux tile `N`   → slot `numSide() + numLid() + N`

Slot 0 is unused by the shipped maps. The full tile blob is padded to a whole
number of four-tile groups so the decoder can always read complete groups.

Some maps reference tile indices past the end of their own section.  The
original engine simply continues into the next section of the same blob, so
`SANB.CMP` lid indices 150..167 are really auxiliary tiles
(`aux = lid - numLid`).  The atlas loader therefore resolves every map index by
its global slot (side → lid → aux) instead of rejecting out-of-range values.

### Synchronous tile animations and palette

After the tiles comes `ceil(totalTiles / 4) * 4 * 4096 - tileBytes` padding,
then the animation block, then a 768-byte palette. The animation block starts
with a count, followed by records `block, which, speed, frameCount, frames[]`;
`which` selects the side or lid bank, frame IDs address auxiliary tiles, and
`speed` is in game cycles per frame. The original tile is shown before the AUX
frames loop. The loader parses complete records and uploads the active frame
into the shared atlas, so all map instances animate in sync. Playback uses a
30 Hz compatibility clock pending comparison against the DOS executable.

The shipped `STYLE003.GRY` declares three animation records but its 24-byte
animation block contains only two complete records. The loader keeps those two
and marks the incomplete tail rather than rejecting Miami's graphics archive.

Palette values are 6-bit VGA components; the loader expands them with
`(v << 2) | (v >> 4)`. Palette index 0 is transparent for sprites and flat
world decals; opaque map faces keep their black palette pixels.

The 65,536-byte remap table follows the palette and contains 256 mappings of
256 palette indices. The following 1,024-byte `remap_index` table has four
entries per lid ID; for CMP selector 1..3, the entry gives the actual remap
table ID. The atlas resolves each lid's variants through this lookup. Selector
0 and table ID 0 preserve the original palette. CMP remaps apply to lids, not
side faces.

### Car records

Each `CarInfo` starts with 174 bytes and is followed by `numDoors * 8` bytes of
door data (doors are read from offset 172). Fields used by the engine:

```
+0   int16 width
+2   int16 height
+4   int16 depth
+6   int16 sprite number (relative to the car sprite group)
+8   int16 weight descriptor
+10  int16 max speed
+12  int16 min speed
+14  int16 acceleration
+16  int16 braking
+18  int16 grip
+20  int16 handling
+94  uint8 remap8[12]
+106 uint8 vtype
+107 uint8 model
+108 uint8 turning
+109 uint8 damagable
+172 int16 numDoors
```

`STYLE001.GRY` contains 38 car records.

### Sprite records

```
0  uint8  width
1  uint8  height
2  uint8  deltaCount
3  uint8  flags (compression flag, always 0)
4  uint16 size (width * height)
6  uint32 ptr   page = ptr / 65536, x = (ptr % 65536) % 256,
                y = (ptr % 65536) / 256
10 deltaCount * 6 bytes: uint16 size, uint32 ptr
```

Sprite graphics are 256×256 palette-index pages. A sprite's base frame is cropped
from its page at `(x, y, width, height)`; delta frames patch pixels into the page
before cropping.

The 21 `uint16` sprite number groups at the end are counts. Absolute sprite
indices are cumulative:

```
arrow, digits, boat, box, bus, car, object, ped, speedo, tank, ...
```

For `STYLE001.GRY` the car group starts at `24 + 0 + 1 + 0 + 6 = 31`,
so car sprite `s` is at absolute index `31 + s`.

## Named districts (nav data)

After the route and the 36×3-byte location table comes the navigation data:
`navDataSize / 35` records of

```
0  uint8 x, 1 y, 2 width, 3 height
4  uint8 sample       (FXT key suffix)
5  30 bytes           name (the engine resolves it through ENGLISH.FXT)
```

The sample number addresses an area string `[001areaNNN]` in `ENGLISH.FXT`,
e.g. `001area014` → `Ardler`.  The engine uses these rectangles to place
mission targets in the district named by the briefing text.

## Coordinate conventions

* Map cell `(x, y)` covers world `x .. x+1`, `z .. z+1`.
* A block at column level `z` occupies `y = z .. z+1`; its lid is drawn on the
  top face at `y = z + 1`.
* Car physics treats `+x` as east and `+z` as south; yaw 0 faces north (`-z`).

## Object info (inside GRY)

`objectInfoSize / 20` records, each 20 bytes:

```
0   uint32 width
4   uint32 height
8   uint32 depth
12  uint16 sprNum    (relative to the OBJECT sprite group)
14  uint16 weight
16  uint16 aux
18  int8   status
19  uint8  numInto
```

Map object records reference these by `type` when `remap < 128`.  The engine
places props on the map using `x/64`, `y/64` as world X/Z and
`6.05 - z/64` as world height.

## FON bitmap fonts

Header:

```
0  uint8 numChars
1  uint8 charHeight
```

Then for each character:

```
uint8 width
width * charHeight bytes of palette indices
```

Finally a 768-byte RGB palette.  `BIG1.FON` (h=35) and `BIG2.FON` (h=65) use
the original mapping: `!`=0, `-`=12, `A..Z`=32..57.  Glyph index 0 is the
transparent background.

## FXT text (mission briefings / UI strings)

`ENGLISH.FXT` is a NUL-separated string table where every byte is shifted by
one: `decoded = (byte - 1) & 0xff`.  Strings beginning with `[key]` are looked
up by key, e.g. `[1002]`, `[cut00]`, `[3501]`, `[car22]`.

## SDT/RAW sound banks

`*.SDT` is a table of 12-byte records:

```
0  uint32 sample start offset in the .RAW file
4  uint32 sample size in bytes
8  uint32 sample rate (Hz)
```

The `.RAW` payload is 8-bit unsigned mono (silence = 128). The runtime loads
`LEVEL001`, `LEVEL002`, or `LEVEL003` according to the selected city's CMP style
byte. Sample playback advances by each record's authored rate and linearly
interpolates adjacent 8-bit samples at the output rate. Engine loops use the
original car-specific records when available, with synthesis as a fallback.

## RAW/RAT/ACT images

* `F_LOGO*.RAW`, `F_UPPER.RAW`, `F_LOWER*.RAW` are uncompressed 24-bit RGB:
  * `F_LOGO*.RAW` / `F_UPPER.RAW`: 640×168
  * `F_LOWER0/1.RAW`: 640×312
* `CUT0..CUT5.RAT` are 640×480 8-bit palette indices, `CUT*.ACT` are 768-byte
  RGB palettes (8-bit components, not 6-bit).
* `F_PAL.RAW` is the shared 768-byte palette for the `F_*` intro images.

The cutscene renderer displays each authored RAT frame at its original 4:3
composition with aspect-preserving scaling. It must not synthesize face/mouth
movement or camera motion over the still image; those effects are not present
in the shipped frame data.

## MISSION.INI

A large text file split into `[section]` blocks.  Each block starts with a
description line such as:

```
Easy Liberty City NEW, 22, nyc.cmp, 0,
100 1 1 1 1 0
```

Object lines use:

```
id flags (x,y,z) TYPE param value
```

For the single-player city chapters the engine reads `TELEPHONE` entries and
turns them into payphone mission markers.  Other types (`PARKED`, `PLAYER`,
`TRIGGER`, `POWERUP`, `CANNON_START`, ...) are used by multiplayer/race modes
and are not yet implemented.

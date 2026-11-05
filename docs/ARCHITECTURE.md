# Engine architecture

## Frame flow

```
main()
 ├─ SDL_Init(video, events, gamecontroller, audio)
 ├─ load user config (SDL pref dir) + command line overrides
 ├─ Renderer::create       window, GL context, shaders, MSAA/FXAA targets
 ├─ InputManager::init     keyboard + hot-pluggable SDL game controllers
 ├─ AudioManager::init     procedural engine synthesis
 ├─ Game::init             data discovery, CMP/GRY load, world mesh, car setup
 └─ loop
     ├─ SDL_PollEvent → InputManager / Renderer::resize
     ├─ InputManager::newFrame
     ├─ Game::update       toggles, driving physics, camera, audio parameters
     ├─ Game::render       beginFrame → world → car → HUD → endFrame
     └─ screenshot / quit handling
```

## Rendering

The renderer owns:

* the SDL window and OpenGL 3.3 core context;
* a multisample scene framebuffer (configurable, disabled at MSAA 0);
* a resolve framebuffer with an RGBA8 texture;
* an embedded sprite shader and an FXAA shader;
* a 1×1 white helper texture and a fullscreen triangle.

`beginFrame()` binds the scene target and clears. `endFrame()` resolves MSAA,
then either runs the FXAA pass into the default framebuffer or blits the resolve
texture. The render target can follow a render-scale (`--scale`) independently
of the window size.

The city is split into 16×16 cell chunks. Each chunk is an indexed VAO with
position, UV and RGBA vertex attributes.  Water, land, pavements and bridge
decks are rendered flat at the city's base height (a one-unit CMP step is a
kerb, not a wall); only columns that rise above that are extruded into
buildings, with continuous façades from the neighbouring ground to the roof.
Chunk geometry contains:

* one roof quad per authored lid block, at its own level (so elevated decks
  under structural blocks render correctly); horizontal lids are sampled with
  their texture's north edge on the −Z side;
* facade quads only where the map block actually carries a side texture.  A
  generic facade is used only for solid blocks that own a lid; invisible
  structural "crowns" above the streets are never given invented walls;
* a base-height floor for columns with no drawn lid, so open sea and park
  lawns meet the streets instead of forming sunken pits.  Water bodies are
  classified by flood-filling empty regions from the map border and by the
  nearest real water tile.

City geometry is drawn with back-face culling: horizontal lids are wound to
face up, wall quads face outwards, so a two-sided fence decal only draws the
side facing the camera.  Without culling every fence, railing and sign doubles
up and the streets turn into a forest of overlapping quads.

Car sprites are alpha-tested billboards lying on the ground plane, rotated by
the vehicle yaw. The HUD is drawn in screen space with the same shader using a
built-in 5×7 bitmap font and untextured rectangles.

## World queries

`World` precomputes `surface_` (the top face of the highest *drawn* non-flat
lid, ignoring transparent decals and invisible crowns) for rendering and
terrain fill. `roadSurface()` scans down the column for the first known asphalt
lid and returns the top face height of that block. This handles the structural
"crown" blocks that GTA places above many street cells.

`World::roadSurface()` is the strict road network used by traffic, mission and
pickup placement.  `World::isPassable()` is the wider driving surface used by
the player's car and on-foot movement: roads, pavements, parks and bridge decks
are passable, buildings and water are not.  `Vehicle` probes its four corners
against it and slides/stops on collisions.

## Input

`InputManager` maps physical keys and controller axes to `GameAction` values,
keeps current/previous states for edge detection and exposes normalized throttle
and steering values. Controller connect/disconnect events are handled at runtime.
The mapping is intentionally data-free; a remapping UI can be layered on top of
`GameAction` later.

## Audio

`AudioManager` opens an SDL audio device and synthesises a two-oscillator engine
note whose pitch and volume follow speed and throttle. Pausing fades the engine
out. The callback only reads atomics, so no locking is required. Original
`AUDIO`/`SND` banks are not consumed yet.

## Configuration

`GameConfig` is split into graphics and audio sections. CLI options override the
user config file, which lives in the SDL preference path. On exit the renderer's
fullscreen state and logical window size are written back. The file format is a
simple `key = value` text file; unknown keys are ignored.

## Portability

* **Desktop GL**: OpenGL 3.3 core, macOS/Linux/Windows (Windows needs a loader
  such as GLAD/GLEW).
* **Android GLES**: CMake sets `GTA_GLES=1`, which selects `GLES3/gl3.h`, an ES
  3.0 context and translates `#version 330 core` shaders to `#version 300 es`
  with precision qualifiers.
* **High DPI**: the render target uses `SDL_GL_GetDrawableSize`, so Retina and
  other scaled displays are rendered at native resolution with an optional
  render scale.

The file and format layers are platform independent; only `renderer.cpp`,
`gl.h`, `audio_manager.cpp` and `input_manager.cpp` touch OS APIs, all through
SDL2.

## Extension points

* `World` can accept object placement from `CityMap::objects` (props, parked
  cars, pickups).
* `Vehicle` exposes sprite/model indices and can be extended with damage deltas
  (`GrySpriteInfo::deltas`) for doors and sirens.
* Traffic and pedestrians can share the same sprite cache and chunk culling.
* A minimap can be rendered from the CMP base grid or cached roof textures.

## Original presentation layer

`Game::loadOriginalMenuAssets()` reads the retail intro/menu art directly:

* `F_UPPER.RAW` + `F_LOWER0.RAW` → 640×480 menu background;
* `F_LOGO0..7.RAW` → animated logo frames;
* `BIG1.FON` / `BIG2.FON` → original gold menu fonts (`BitmapFont`);
* `CUT0.RAT` + `CUT0.ACT` → intro cutscene image;
* `ENGLISH.FXT` → dialogue and mission briefing strings;
* `MISSION.INI` → payphone coordinates for the current city.

The intro sequence is logo animation → cutscene with three dialogue lines →
main menu.  All of it is skippable with Enter/A/Esc, and the menu uses the
original 4:3 background letterboxed on modern screens.

## Missions

`Game::loadMissionData()` parses the first single-player section for the loaded
city, snaps every `TELEPHONE` position to the nearest road cell and stores the
result as `Phone` markers.  A mission goes through:

```
Idle (phone rings, "ENTER TO ANSWER")
  → Briefing (original FXT text)
  → Active (target marker + countdown)
  → Result (success/failure banner)
  → Idle (next phone or replay)
```

Each city chapter has 22 jobs (the count comes from the `MISSION.INI` section
header).  Briefings are the original `ENGLISH.FXT` strings; the engine parses
the district named in the text (e.g. `North East Park`), resolves it to the
CMP named-sector rectangle and places the target in that sub-area, so the HUD
objective reads `STEAL THE CAR IN NORTH EAST PARK`.  Mission types are inferred
from the briefing keywords with the authored seed as a fallback.  The exact
original triggers are still not recovered from `GTA8.EXE`.

Progress autosaves to `config.ini` (city, job, cash, health, weapons and
position); the main menu offers CONTINUE.  Completing all 22 jobs shows the
CITY COMPLETE screen and travels to the next city through its intro cutscene.

## Ambient simulation

* **Traffic**: eight cars spawn on nearby road cells, move cell-to-cell on the
  real road grid, choose a new direction at intersections and respawn when far
  from the player.
* **Pedestrians**: 24 sprites walk the same grid at a slower speed.
* **Props**: every CMP object with a valid `GRY` object/car sprite is rendered
  as a ground-aligned billboard.  Objects are culled to a 42-unit radius around
  the camera target.
* **Pickups**: cash diamonds remain as optional collectibles.

All sprites use the same lazy RGBA texture cache (`AssetManager::spriteTexture`)
built from the original GRY sprite pages.

## On-foot mode

The game starts with the player on foot next to a parked car and the nearest
ringing payphone.  `Game::updateOnFoot()` moves the player over the same CMP
road grid, selects one of the eight original player-walking sprites
(`620..627`) and slides along blocked cells.  `F`/`E` enters or exits the car;
`Enter` answers a phone (or enters the car when no phone is in range).

The camera follows the active entity (`playerPosition()`), and missions,
pickups, traffic and pedestrians all use that position.  The car is always
rendered, so it remains parked while the player walks around.

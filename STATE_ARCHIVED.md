# pzformat — STATE archive

**Split out of `STATE.md` on 2026-09-01 by owner decision.** `STATE.md` had
reached 5,096 lines and the live facts were no longer findable inside it — the
14,680 figure survived in five documents partly because nobody could re-read
the whole thing.

**Nothing here was deleted. Everything was moved, whole, unedited.** Section
numbers are preserved, so every `(§N)` cross-reference in `STATE.md`,
`CHUNKS.md` and the prompt files still resolves — to a stub in `STATE.md` that
points here.

**The test applied, per the owner's instruction:** *will a future session need
this?* Anything not a clear yes moved here. Session narrative, superseded
models and closed todo lists moved. **Domain knowledge about PZ's formats and
engine behaviour did NOT move, regardless of the language it was learned in** —
those facts outlive the port.

**This file is read-only history.** Do not paste it into a session by default.
Read it when `STATE.md` points you here, or when tracing why a decision was
made.

---

## 3. What is next, in order

1. **Verify `outlineRoom` against a real room.** Cheap, and everything about
   building generation and room authoring sits on it. Editor track.

2. **Cleanup with evidence already in hand.** `TreeScatter` / `TreePalette`
   place ~7,800 trees that `genMapSquare` deletes on load, and
   `WorldGenOverride.lua` is superseded by the biome map. Delete both,
   regenerate, and confirm in game that nothing changes — that *proves* they
   were dead weight rather than assuming it.

3. **Buildings.** The substantial piece. Read a vanilla house first
   (`Probe square` / `findprop` against Muldraugh) before writing anything.
   There is an open design fork:
   - a room-splitting generator that decomposes footprints into rectangles,
     places roofs, doors on street-facing walls, windows, interior doorways; or
   - `StaticModule.prefab`, the engine's own structure placement mechanism,
     **never tried, not yet read in the decompiler.**

   For the editor these are not equivalent. A room-splitting generator produces
   authored geometry the editor can then edit, validate and undo. `prefab`
   produces something the engine assembles at load time, which the editor cannot
   inspect the same way. **Read `StaticModule` in the decompiler before choosing** —
   the decision should not be made from the name alone.

4. **Room type names.** Generic `"room"` gives no loot tables. Meaningful types
   are needed for a generated map to be playable and for the editor to validate.

---

---

## 14. Test log

| # | Test | Result |
|---|---|---|
| 1 | Round-trip 4065 vanilla cells | Byte-identical — **but** x/y were transposed and it passed anyway |
| 2 | Edit a vanilla cell, load in game | Rendered the intended change only |
| 3–4 | GIS import with/without area clipping | Clipping required; projection sound |
| 5–6 | Mod structure | Maplewood layout registers the map |
| 7 | Play generated map | Buried in forest |
| 8 | Render cell 201_200 | Buildings correct |
| 9 | `WorldGenOverride.lua` grass_plain | Open grassland; no buildings seen |
| 10 | chunkdata probes ×4 | All hypotheses killed |
| 11 | Decompile the engine | Found the real gate |
| 12 | Edge fill + assertion | 2,474,010 → **3,145,728** squares; assertion passes |
| 13 | Palette sprite requirement | 167 candidates dropped |
| 14 | Load after edge fill | Still nothing — chunk dirs 7530–7570 |
| 15 | **Spawn coordinate fix** | Chunk dirs **6396+**. On the map |
| 16 | Spawn on a road square | **Road and building correct in game.** Blocker disproved |
| 17 | Palette by semantics | Grass green, floor `"Hardwood Floor"` |
| 18 | Legacy `.pack` | **24/24**, byte-identical, 224 tests pass |
| 19 | `SpriteAtlas.MAP_PACKS` extended | `sprites not found: 0 / 50` |
| 20 | Tree species machinery ×3 | All wrong |
| 21 | `findprop` vs vanilla 35_35 | Generic `vegetation_trees_01_*` |
| 22 | Ground survey, 262,144 squares | Four-tile groups, 43.3% overlay, never >1 |
| 23 | Ground variation authored | Patchwork — groups rolled per square, not per region |
| 24 | Parse WorldGen Lua | GROUND features === the surveyed groups |
| 25 | Decompile `BiomeMap` / `BiomeRaster` | R=biome, G=zone, one pixel per tile |
| 26 | Write biome maps, load in game | **Gradient visible, no seam, ore veins generating** |
| 27 | Drop dirt groups | Bare diamonds gone; forest floor continuous |
| 28 | Spawnpoint -> cell arithmetic, then `square` | Landed inside `livingroom`, cell 42_40. Absolute-tile reading confirmed |
| 29 | `roomgeom` on vanilla 42_40 | 86 rooms; far offsets win 67.0 vs 10.2 and 83.9 vs 3.7. `weldingworkshop` exact on all four corners |
| 30 | `roomgeom` on generated 200_200 | **Void, not negative.** Diagonal walls inside bbox rects; 24.1 vs 22.2 is noise |
| 31 | Guards added, rerun 42_40 and 200_200 | Vanilla unchanged and CORRECT at 6.6x/22.4x; 200_200 refuses to conclude |
| 32 | `PaletteScan --prop Facing` | Objects only: N/S/E/W, ~6,200 tiles, no diagonals. Walls carry orientation as `Wall*` flags instead |
| 33 | `findprop WallSE` on 42_40 | `PaintingType = pillar`. A post, not an edge. `edgeOf` returns NONE for it |
| 34 | Alignment sweep, attempt 1 (interior fraction) | 674 non-aligned. **All false positives** — bathrooms, halls, barns with one partition |
| 35 | Alignment sweep, attempt 2 (slope of per-row mean) | 0 non-aligned, but also cleared the known diagonal. r2 0.227 |
| 36 | Alignment sweep, attempt 3 (concentration) | GIS caught at 0.19/0.20; vanilla down to 53, still false positives |
| 37 | Alignment sweep, attempt 4 (both axes required) | **0 non-aligned / 29,928 tested.** All 3 GIS diagonals still caught. §17 check 1 closed |
| 38 | Load with `WorldGenOverride.lua` removed | **No seam, foliage flows cleanly across the boundary.** The file was doing no work (§21) |
| 39 | 8 adjacent ground squares, generated 200_200 | Dark, Dark, Dark, **Light**, Dark, Dark, **Medium**, Medium — three changes in eight |
| 40 | 16 adjacent ground squares, vanilla 35_35 | `Grass_Dark` 16/16. No alternation at all |
| 41 | 16 samples spaced 16 apart across vanilla 35_35 | Grass region, then road, then a dirt region alternating `Dirt`/`Dirt_Grass` per square |
| 42 | Observed zombie spawns at the map boundary | Zombies on vanilla ground, none on ours, boundary exactly where our road ends |
| 43 | `survey` chunk grid histogram, vanilla | 0..10 present. 96.4% zero; 1/2/3 dominate the rest; 8/9/10 are ~0.005% |
| 44 | `survey` chunk grid histogram, generated | **All 4096 bytes zero across all 4 cells** |
| 45 | `writeChunkDensity`, then regenerate and survey | 0→3935 (96.1%), 1→89, 2→72. Predicted 40-70 twos and 80-150 ones before running |
| 46 | Fresh world, walk to a generated building | **Zombies at the building, none on the way.** First ever seen on the generated map |
| 47 | 6 samples across vanilla town cell 42_40 | `Grass_Medium`, Sand, `Grass_Dark` — regions vary WITHIN a cell, contiguous at 40-tile spacing |
| 48 | Fine transect x=100..140 in 42_40 | **Squares carrying up to five stacked ground tiles.** Not a clean region boundary |

---

---

## 15. Todo

### Immediate — evidence already in hand

- [x] Verify `outlineRoom` far-side wall placement against a real Muldraugh room
      — **done 2026-08-10, offsets correct (§18). A3, A4, A5 unblocked.**
- [ ] Delete `TreeScatter` and `TreePalette`; the engine deletes their output
      — **not wholesale: `BiomeMapWriter` needs `distanceToStructure` (§20)**
      — **BLOCKED. Do not start. Tree ownership is unresolved (§25).**
- [ ] **Settle tree ownership.** Walk a line of known authored tree
      positions in game and see whether trees stand at exactly those
      coordinates. Unblocks or kills A2 step 1 (§25)
- [x] Stop writing `WorldGenOverride.lua`; the biome map supersedes it
      — **CONFIRMED 2026-08-11 in game: removed, no seam, foliage clean.**
      Remove the write at `GisCells:220` and `writeWorldGenOverride` (§21)
- [ ] **Ground region layer.** Group selection must be spatial, not per
      square. Grass_Dark / Medium / Light are region distinctions, not
      texture (§21)
- [ ] Check whether the biome map already supplies the region signal before
      building a noise field (§21)
- [ ] Restore the dirt groups once regions exist; gate them to tracks and
      yards rather than open country (§21)
- [ ] Explain 25 output lines from 16 ground probes — some vanilla squares
      carry two ground tiles, which the survey's "never more than one
      overlay" result does not obviously allow (§21)
- [ ] Open question surfaced by A2: do authored tree tiles and engine biome
      vegetation target the same squares? (§20)
- [ ] Regenerate and confirm in game that nothing changes — that proves they
      were dead weight rather than assuming it

### Buildings — the next substantial piece

Current output is one bounding box per footprint with derived perimeter walls.
**Read a vanilla house first** (`Probe square` / `findprop` against Muldraugh)
before writing anything.

- [ ] Read `StaticModule.prefab` in the decompiler; decide the fork on evidence
- [ ] Count Muldraugh rooms with `rectCount > 1` forming a diagonal run — is
      the orientation constraint hard or merely dominant? (§17)
- [ ] Scan wall tiles for their declared orientation values (§17)
- [ ] `FootprintSnap` — one module, called by GIS import and by the editor
- [ ] Decompose footprints into multiple room rectangles
- [ ] Roofs; exterior doors on street-facing walls; windows
- [ ] Interior subdivision and doorways
- [ ] Meaningful room *type* names — generic `"room"` gives no loot tables
- [ ] Vary wall and floor materials by occupancy class

### Editor — the actual goal

- [ ] Auto wall-joining: pick the right variant from neighbours (corner, end,
      junction). All the information needed is already present.
- [ ] Validation rules: doorway with no adjacent floor, room with no exit, wall
      gap that isn't a door. **This is where the editor earns its place.**
- [ ] TMX read/write for interop with the official tools
- [ ] `.tiles` writers
- [ ] Interactive rendering — pan, zoom, live edit. Lower risk now that the PNG
      renderer proved the geometry. Knox County is ~1,300 cells and B42 added
      negative z-levels, so this needs sprite batching, an atlas cache and
      viewport streaming from day one; a naive per-tile draw dies immediately.
- [ ] **UI architecture is undecided.** Options discussed but never chosen:
      Spring Boot backend + WebGL canvas (gets multi-user editing, the one thing
      WorldEd can't do; costs atlas transfer bandwidth), or native LWJGL/libGDX.
      **The multi-user clause above is superseded** — CHARTER §3 ruled out
      concurrent editing on 2026-08-08 (see §13). The fork itself is still
      open; only that argument for the Spring Boot side is dead.
      A real working store (SQLite or chunked binary) rather than thousands of
      TMX files, with TMX/PZW kept purely as an interop boundary.

### Biome map quality

- [ ] Drive the **floor** from the same biomemap pixel, via `biomes_map`
      `placements`, so ground and vegetation come from one source
- [ ] Use `OCC_CLS` / `PRIM_OCC` to choose biome per parcel (Agriculture → Farm)
      rather than distance bands alone
- [ ] Dither or band the biome transition rather than hard distance thresholds
- [ ] Consider the ZONE band properly — it drives foraging

### Other

- [ ] Scene rotation pass in GIS import: dominant-grid histogram, then rotate
      buildings, roads and area polygon together before rasterizing (§17)
- [x] `roomgeom` guards: refuse to conclude when the two offsets are within
      a few points, and when wall runs are not axis-aligned (§18)
      — **margin guard done and working. Alignment guard took four attempts
      and still has 53 known false positives; one-token fix pending (§19).**
- [ ] **A3 prerequisite:** `TileIndex.edgeOf` falls back to
      `attachedN`/`attachedW` for tiles with no `Wall*` flag — the exact
      proxy its own comment warns against. Unreachable from `wallOn`, but
      `edgeOf` is public and A3 will call it on neighbours. Split it into a
      separate `decorationEdge()` or return NONE (§19)
- [ ] **A3 prerequisite:** confirm the tileset variant cycle. In
      `walls_exterior_house_01` the pattern is `WallW, WallN, WallNW,
      WallSE` every 4, openings every 16. Wall-joining needs that structure;
      per-tile flags alone do not say corner-vs-end-vs-junction (§19)
- [x] Apply the `&&` fix to `alignment()` and rerun the sweep (§19)
      — **done 2026-08-11. 53 false positives to 0; §17 check 1 closed.**
- [ ] Fix the stale class comment on `LotHeader` — it says the B42 trailer is
      left unparsed; `readB42Meta` parses all of it (§18)
- [x] Test whether `chunkGrid` is zombie density: mean over a town cell vs a
      forest cell should differ sharply (§18)
      — **CONFIRMED 2026-08-11 by three independent lines (§22).**
- [x] **Write zombie density into `chunkGrid`.** Currently all zeros, which
      is why our cells have no zombies. Must follow land use, not the
      vanilla histogram (§22)
      — **done 2026-08-11, confirmed in game (§23). Mechanism proven;
      calibration untested.**
- [ ] Calibrate density values. 2 near buildings is at the low end of
      vanilla's range and one hamlet is not a town (§23)
- [ ] Should density vary by occupancy class? The import distinguishes
      Agriculture from Residential and currently treats them alike (§23)
- [ ] Measure how vanilla's nonzero density correlates with what a place
      is — the recipe, not the histogram (§4, §23)
- [ ] Read the biome map's town/edge/forest/deep classification as the
      region signal for ground groups (§21, §23)
      — **do not start here. The region driver is not known to be distance
      from habitation (§24).**
- [ ] **Ground blending investigation.** How does vanilla stack ground
      tiles, and what decides which pairs blend? Prefer the recipe:
      `blends_natural_01` naming and properties, and the engine code that
      assembles ground squares at load. Measuring more of Muldraugh has
      now produced three complicated hypotheses in one session (§24)
- [ ] Identify what `Sand` represents mid-cell in 42_40 — parking, yard,
      or shore. If land use rather than distance, the region design
      changes again (§24)
- [ ] Extract `BiomeMapWriter`'s distance banding into a reusable method.
      Worth doing regardless: three consumers want a region signal (§24)
- [ ] Room rects must cover interior squares only — a bbox over a
      non-rectangular footprint marks outdoor squares as room members (§18)
- [ ] Road auto-tiling — corner, T-junction, end, edge by neighbour bitmask
- [ ] Populate `objects.lua` (currently `{}`; vanilla's is 4 MB) — likely
      related to room loot tables and worth checking alongside room type names
- [ ] `worldmap.xml` — `mapdir` reports it missing; imported areas do not appear
      on the in-game map without it
- [ ] Fast round-trip regression (currently ~14 min)
- [ ] Publish B42 format documentation (PZwiki is B41 and wrong)
- [ ] Verify GIS dataset licensing per-state; choose a licence

---

---

## 17. Building orientation — the grid admits no rotation

Raised 2026-08-10. Partly CONFIRMED from the format already recorded here,
partly UNVERIFIED and needing a vanilla sample.

### CONFIRMED, from §10 and the renderer

A room is `int32 x, y, w, h`. No rotation field, no polygon, no vertex list.
A room can only be expressed as a union of axis-aligned rectangles. Walls are
north/west edges per square, so the geometry has no diagonal wall primitive
whatever sprite art exists. The camera is fixed isometric: a wall sprite is
drawn for one facing and cannot be rotated at render time.

So an off-axis footprint is not a cosmetic problem. Walls are objects on
squares and will happily stair-step, but the room they enclose either fails
to register or registers as a staircase of 1-wide rects with wall runs
between the steps. That breaks room detection, room-type loot tables, and
every validation rule planned for the editor. **The current generated map
has this pattern.**

### UNVERIFIED — the two checks, before any code

1. **Is the constraint hard or merely dominant?** Count Muldraugh rooms with
   `rectCount > 1` whose rects form a diagonal run (successive rects offset
   by ~1 on both axes, with `w` or `h` near 1). **Zero across 90,827 rooms
   means the snap may refuse outright. Nonzero means it is a strong default
   with an override**, and one such room must be read before deciding which.
2. **Which orientations do wall tiles declare?** `PaletteScan "$PZ/media"
   walls_exterior_house_01` and read the discriminators the tiles carry.
   Expectation: north, west, and a corner post; nothing diagonal.
   **Falsifier: any orientation value that is none of those.** If one
   exists, the wall model underneath A3 is wrong before A3 is written.

Note §11: `wall` is a bare flag with no orientation, and `attachedN` /
`attachedW` validated at 99.5% while being wrong. Whatever discriminator
check 2 turns up must be shown to be the tile's own declaration, not another
correlated proxy.

### `FootprintSnap` — one module, two callers

The same invariant serves GIS import and interactive authoring, which is the
§2 test for whether GIS work earns its place. A proposed footprint goes
through the snap and comes out aligned, or is refused.

- **GIS import.** Per-footprint min-area-rectangle alignment is **wrong** —
  it squares each building against itself and randomises them against each
  other and the roads, which looks deliberate and is worse than the zigzag.
  Real towns align to a street grid. Take each footprint's min-area-rect
  angle mod 90 deg (a rectangle's orientation is 90 deg-periodic), histogram
  weighted by footprint area, and rotate the **entire scene** — buildings,
  roads, area polygon — by the dominant mode before rasterizing. Residual
  per-building deviation then snaps to the nearest 90 deg.
  **Prediction to write down before running it:** a US grid town shows one
  mode holding well over half the footprint area within +/-3 deg. Flat or
  bimodal means the area has no single grid, and whole-scene rotation is the
  wrong move for it — that is the case needing per-cluster rotation.
  Roads stair-step fine in vanilla, so rotating them costs nothing.
- **Editor.** Rectangle tools axis-aligned; free rotation in 90 deg
  increments only, because arbitrary rotation is unrepresentable rather than
  discouraged. The snap is the ergonomic face of a rule the validator
  enforces anyway.
- **A4 rule: wall run not expressible as a room rect.** This is the
  enforcement point. It catches hand-painted zigzags whatever tool made
  them, and it catches imported data that skipped the rotation pass. Snap
  without the rule leaves the invariant unenforceable on imported data.

### Consequence to remember

Rotating the scene divorces the map from true north. Nothing in game depends
on that today, but `worldmap.xml` (§15) and any future GIS overlay do — the
rotation angle must be stored with the import, not discarded.

---

---

## 18. Session 2026-08-10 — A1 closed, and what it turned up

### A1: `outlineRoom` offsets are CORRECT

Two independent lines, neither of which is our own writer checked through
our own reader:

**Vanilla measurement.** `Probe roomgeom "$PZ/media" "$MAPS/Muldraugh, KY"
42_40`, 86 rooms. The discriminating comparison is the same classifier at
two positions: south `ry+rh` 67.0% vs `ry+rh-1` 10.2%; east `rx+rw` 83.9%
vs `rx+rw-1` 3.7%. **Ratio, not rate** — this is what makes it immune to the
§11 `attachedN` failure, where 99.5% absolute was wrong. A miscalibrated
wall classifier would have to be positionally biased to flip those.

The worked example carries more weight than the percentages.
`weldingworkshop [57,169 12x12] z=1`, against four predictions written down
before it was run:

| Predicted | Observed |
|---|---|
| `(rx, ry)` carries both edges | `+` at 57,169 |
| `(rx+rw, ry)` west only | `W` at 69,169 |
| `(rx, ry+rh)` north only | `N` at 57,181 |
| **`(rx+rw, ry+rh)` carries neither** | `.` at 69,181 |
| runs of `rw` and `rh` | 12 and 12 |

48 placements on 47 distinct squares.

**Source inspection.** `CellEditor.outlineRoom` loops north over
`x0 .. x0+w-1` at `y0` and `y0+h`, west over `y0 .. y0+h-1` at `x0` and
`x0+w`, and never touches `(x0+w, y0+h)`. That is `2w + 2h` placements on
`2w + 2h - 1` squares — exactly what vanilla measures.

**The instrument already existed.** `Probe roomgeom` is where §10's
"86 rooms, both far sides confirmed" came from. §2's open thread was stale
when it was written. *Check what this project already does, not only what
vanilla does.*

### Test 30 is void, not negative

`roomgeom` on generated 200_200 printed `outlineRoom's offsets are WRONG`.
Discard that conclusion. Two reasons:

1. **GIS buildings never call `outlineRoom`.** They are a bbox rect with
   walls traced round the footprint polygon — a different code path.
2. **The input is the §17 zigzag.** `room [199,221 26x15]` is a 390-square
   bounding box containing a diagonal parallelogram of walls. Walls sit at
   neither `ry+rh` nor `ry+rh-1`; the diagonal crosses both rows once. 24.1
   vs 22.2, and 10.4 vs 14.6 flipping the other way, are two coin flips.

**`roomgeom` has no margin test and no axis-alignment test, so it stated a
confident conclusion from noise and nearly retired a correct convention.**
A probe that cannot say "I don't know" manufactures findings. Both guards
are in §15.

### Room rects can exceed their building

Independent of orientation: a bbox over a non-rectangular footprint marks
~200 outdoor squares as members of `room`. Membership drives loot spawning
and every A4 rule. The fix is the §17 constraint arriving from the other
side — a room must be a union of rects covering interior squares only.

### Vanilla `spawnpoints.lua` — CONFIRMED, and not what §7 said

`$MAPS/Muldraugh, KY/spawnpoints.lua` is `posX`/`posY`/`posZ` only, no
`worldX`/`worldY`, values in absolute world tiles:

```
cellX = posX / 256      localX = posX % 256
```

Verified: `posX=10770, posY=10271` -> cell 42_40, local 18,31 -> an interior
carpet square inside `room 23 'livingroom' rect [14,31 5x6]`. All 21
spawnpoints fall in cells 41-43 x 36-41, a coherent town block. Sets are
`poor_houses` (10), `medium_houses` (5), `rich_houses` (3),
`doctor_houses` (2), `police_station` (1), merged per profession.

**Our write path emits the legacy `worldX`/`worldY` form and works
(test 15), so the reader accepts both.** Likely `worldX` defaults to 0,
making `posX` absolute. **UNVERIFIED** — confirm in the decompiler before
switching `GisCells` to the absolute form. If it holds, the 300-tile
conversion that caused the three-session spawn bug can be deleted outright.

### The per-square room id is dead

`room id: -1` on every interior square sampled — 3 squares, 3 rooms, 2
cells. Room membership is derivable **only** from lotheader rects. Two
consequences: A4 rules need a spatial index over `RoomDef` rects built per
cell (the same index §17's snap needs), and `CellEditor` should not be
writing a field vanilla ignores. Three samples is not a sweep; widen it
when the rooms dump exists.

### Smaller notes

- **`LotHeader`'s class comment is stale.** It says the B42 trailer is
  "deliberately left unparsed and exposed as raw bytes"; `readB42Meta`
  parses it completely and `Probe lotheader` reports `0 unidentified
  bytes`. Same drift class as the CHARTER problem, one level down.
- **The §10 3-int32 room object records are in better shape than
  UNVERIFIED implies.** The trailer fit `leftover = 1 + buildings +
  roomRefs` across 4064 cells and consumes to zero remainder. That is
  structural evidence for the record width; it still says nothing about
  meaning (§4).
- **`chunkGrid` is probably zombie density.** B41 reads
  `(width/10)*(height/10)` into a field named `zombieDensity`; B42 reads
  `byte[1024]` = 32x32 into `chunkGrid`, values 0..10. Same range, same
  role, resolution moved to per-chunk. Test in §15.
- **`unknown12`** in the B42 trailer is still unnamed.
- **Objects carry four facings, walls two edges.** The chair at 42_40 16,33
  has `Facing W` and a `chairW` flag. When §17 check 2 runs, scan `Facing`
  across wall tiles specifically — if walls only ever take N/W while
  objects take all four, that is the cleanest statement of the constraint
  the data will give.
- **`Probe` argument shapes are undocumented.** `lotheader` takes a file,
  `square` and `roomgeom` take a media dir plus a map dir. Two commands
  were run wrong this session for want of a usage line.
- **`grep` is aliased to `ugrep`**, which is POSIX-strict. GNU `\|`
  alternation matches LITERALLY and returns nothing. Two greps this session
  silently reported "not found" for symbols that were present. Use `-e`
  repeated, or `-E`. Same class as the `ls`/eza gotcha in §5.

---

---

## 21. Ground is regions, not a per-square mix

### A2 step 2 — CONFIRMED, `WorldGenOverride.lua` does nothing

Moved out of the generated mod directory, loaded in game 2026-08-11.
**No seam. Foliage flows cleanly across the boundary between authored cells
and engine-generated land** — dense mixed forest on one side, open grass
with saplings on the other, trees crossing coherently. The biome map is
doing that work. Remove the write.

Incidental confirmation for A2 step 1: the trees look right, and they are
the engine's, not ours. `genMapSquare` deleted our authored trees and
generated its own from the biome map — which is the claim the deletion
rests on.

**RETRACTED, same day. This paragraph is wrong, or at least unproven —
see §25.** It was written before `BiomeMapWriter`'s scope note was read and
before the generated cell was probed for tree tiles. Both point the other
way. A2 step 1 is BLOCKED, not ready.

### The ground defect, and what causes it

Generated ground reads as scattered tan diamonds in green grass. Probing
eight adjacent squares in generated 200_200:

```
Grass_Dark, Grass_Dark, Grass_Dark, Grass_Light,
Grass_Dark, Grass_Dark, Grass_Medium, Grass_Medium
```

Sixteen adjacent squares in vanilla 35_35: **`Grass_Dark`, all sixteen.**
No alternation whatsoever.

Sixteen samples spaced 16 apart across the same vanilla row: a long grass
region, a `Road_04` crossing, then a dirt region alternating `Dirt` and
`Dirt_Grass` square by square.

**So vanilla ground is two-level, and `GroundPalette` collapses it into
one:**

1. **Region** decides the ground TYPE — grass here, dirt there, road there.
   Large-scale, coherent, driven by what the place is.
2. **Within a region**, mix a small set of tiles that belong together:
   `Dirt` with `Dirt_Grass` to make a worn unpaved surface, or the four
   interchangeable variants of one grass colour.

`Grass_Dark`, `Grass_Medium` and `Grass_Light` are **level-1 distinctions.**
We are treating them as level-2 texture. That is backwards, and it is why
generated ground reads as noise.

### Why the survey did not catch it

The Muldraugh survey measured how OFTEN each group appears — 70 / 21 / 10 —
and reproduced those proportions faithfully, per square. It never measured
how they are ARRANGED. A 14.5% share can be 14.5% scattered uniformly or
14.5% in coherent regions, and those look nothing alike.

**Same failure shape as §4's round-trip caveat and the §11 `attachedN`
bug: a measurement that validates in aggregate while being wrong about the
thing that matters.** Frequency is not distribution.

### Consequence: the dirt groups should come back

Test 27 dropped `dirt` (64/69/70/71) and `dirt_grass` (80/85/86/87) because
they read as bare diamonds scattered through forest. That fixed the symptom
correctly, but the cause was the same one-level collapse — and
`GroundPalette`'s own comment already said so: *"Dirt is still the right
floor for a track, a yard or an unpaved road. It just should not be
scattered through open country at random."*

Vanilla confirms it: the dirt region in 35_35 alternates `Dirt` with
`Dirt_Grass` per square, exactly the level-2 mix. Once regions exist, dirt
returns and unpaved tracks and yards become possible — a capability the
current palette gave up.

### Before building a noise field

**Check whether the region signal already exists.** The biome map drives
vegetation and removed the seam; if biome type predicts ground group, the
region layer is already there and unread, and the fix is a lookup rather
than new noise. Falsifier: sample ground material against biome across a
vanilla cell and see whether they correlate.

Only if they do not is a value-noise field over world coordinates the right
approach — selecting the GROUP at a scale of roughly 8-20 squares, with
uniform choice among the four variants within a group, which is genuinely
per-square. Overlays can stay per-square; they read as texture, not colour.
Either way the measured proportions are preserved exactly.

### Loose thread

Sixteen ground probes on vanilla 35_35 produced 25 `FloorMaterial` lines.
Some squares carry two ground tiles. The survey recorded that no square out
of 257,703 had two overlays; a stacked BASE is a different claim and is not
obviously allowed by it. Unexplained.

---

---

## 23. Density written — the mechanism works

`GisCells.writeChunkDensity`, added 2026-08-11. A chunk holding building
tiles gets 2, a chunk orthogonally adjacent gets 1, everything else stays
0. Footprints come from `buildingRects(g, ox, oy)`, already clipped to the
cell, so the density write does not depend on the raster loop.

### Predicted before running, from building geometry alone

1530 building tiles over 8x8 chunks is ~24 chunks packed perfectly,
realistically 40-70 once footprints straddle boundaries, each with up to
four orthogonal neighbours. So: **40-70 twos, 80-150 ones, 95%+ zero.**

```
0 -> 3935  (96.1%)     1 -> 89     2 -> 72
```

**72 and 89, and 96.1% zero against vanilla's 96.4%** — a number not tuned
for, since the rule was written from building geometry rather than from
matching the histogram.

### In game, fresh world

**Zombies at the building, none on the way to it.** First ever seen on the
generated map. Both halves held, including the negative one: they appear
where we wrote 2 and are absent where we wrote 0.

### What this does and does not establish

**Confirmed:** `chunkGrid` gates zombie spawning, and writing nonzero
values makes zombies appear at those chunks. The plumbing works.

**Not established:** whether 2 is the right value. Vanilla's range runs to
10 and seven buildings at density 2 is a hamlet, not a town. Three zombies
is evidence the mechanism fires, not evidence the quantity is right.

**Open, and worth a proper measurement:** how vanilla's nonzero values
correlate with what a place is. §4 prefers the recipe to the output, so
the engine's own spawn code is a better source than measuring Muldraugh —
but Muldraugh at least says whether density tracks building footprints,
road frontage, or something else. And the import already distinguishes
`Agriculture` from `Residential`; a farm outbuilding and six houses
probably should not carry identical density.

### A region signal already exists

From the `giscells` output:

```
town 17374, edge 28242, forest 77031, deep 87233, beyond-raster 52264
```

**The biome map already computes a four-class per-tile classification.**
That is precisely the region signal §21 needs for ground groups and §22
argued should be built once and shared. It exists, it is per tile, and
`GroundPalette` does not read it.

Whether those four classes map usefully onto grass dense / medium / light
is untested — but reading an existing classification is a much smaller
piece of work than building a noise field, and it should be ruled out
first.

---

---

## 24. Ground stacking — §21's model is incomplete

### Regions vary within a cell

Six samples across vanilla town cell 42_40 at y=200:

```
x= 20   Grass_Medium
x= 60   Sand + Grass_Medium
x=100   Sand
x=140   Grass_Dark
x=180   Grass_Dark
x=220   Grass_Dark
```

Contiguous regions at 40-tile spacing, three materials in one cell. This
kills the fallback that regions might be arbitrary hand-picked fields —
there is structure. Forest cell 35_35 is uniformly `Grass_Dark` across
every sample taken (§21).

### But the driver is NOT distance from habitation

That comparison suggested it: 42_40's houses are in the west (spawnpoints
at local x 18-58) and `Grass_Medium` sits at x=20-60 with `Grass_Dark`
from x=140 out. A fine transect does not bear it out:

```
x=100  Sand
x=104  Sand
x=108  Grass_Dark
x=112  Grass_Medium + Grass_Dark x4      <- FIVE ground tiles
x=116  Grass_Medium + Grass_Dark
x=120  Grass_Medium + Grass_Dark
x=124  Grass_Medium + Grass_Dark
x=128  Grass_Dark
x=132  Grass_Dark
```

The Medium band at 112-124 sits **inside** Dark, with Dark on both sides.
That is not one region meeting another, and it is not a habitation
gradient. It may be a path, a mown verge, or the blend around the Sand
feature. Unknown.

### The finding that matters: vanilla stacks ground tiles

**A square can carry several base ground tiles, not one base plus at most
one overlay.** x=112 carries five. This is the explanation for the
"25 lines from 16 probes" thread left open in §21, and it is the blend
mechanism — overlapping tiles from neighbouring regions soften the
boundary.

**So §21's model is incomplete.** Ground is region, then texture within
the region, **then a blend layer of stacked tiles at boundaries.** We
write exactly one base plus at most one overlay, so even with perfect
regions our transitions would be hard-edged where vanilla's are soft.

### Why the next step is the recipe, not more measurement

Three hypotheses about ground were formed and complicated by data in a
single session: per-square frequency (§21), distance banding, and now a
clean region boundary. Each time the measurement was of **Muldraugh, a
hand-authored town** — which §13 already warns against imitating.

§4: *prefer the recipe to the output.* The blending logic lives in the
engine or in TileZed's authoring behaviour, and reading it will settle in
one pass what four transects have not. Two candidates:

- `blends_natural_01` tile naming and properties — is there a convention
  encoding which pairs blend, and in which direction?
- The engine code that assembles ground squares at load.

**Ground appearance matters** — it is currently the most immersion-breaking
defect in the generated map — so this is a real chunk, not a curiosity.

---

## Track C — application layer, session-by-session (2026-08-21 to 2026-08-25)

**Archived 2026-09-01.** These are the build logs for C1 through C4. What the
app can do today is carried live in §6; the measured render numbers that
justified the architecture are in `C1_ARCHITECTURE.md` and
`harness/FINDINGS_harness_2026-08-22.md`. **F7 — the GIS window in the Qt app —
needs to know `MapView`, `SpriteAtlas`, picking and the stamp brush exist, and
that is in §6. It does not need the order they were built in.**

---

### C1 DONE + C2 UNDERWAY — application layer begun (2026-08-21)

The port is complete (14 library units, all verified byte-identical against the
Java tree). The application layer has now started.

**C1 — architecture decision: DONE.** C1_ARCHITECTURE.md committed. Five
decisions, each with a falsifier: Qt6 Widgets + OpenGL 4.6 (UI-toolkit fit, not
measured performance); instanced draw + two-tier LOD with a MANDATORY 500k-
instance-at-1440p harness before any viewport code; the game's own
.lotpack/.lotheader files ARE the working store (no DB, flush on save, LRU
evict); in-memory per-session undo; one process. CHUNKS C1 ticked [x].

**C2 — working store + first UI slice: DONE and running on real data.**

MapProject (library, Qt-free, 36 tests):
- Enumerates cells from a map dir; a cell counts only if BOTH X_Y.lotheader and
  world_X_Y.lotpack exist (orphan files ignored).
- LRU cache with a cap; clean cells evict LRU-first, DIRTY CELLS ARE NEVER
  EVICTED (evicting one would silently drop unsaved edits).
- Atomic save: write to <file>.tmp then rename, so a crash mid-write cannot
  corrupt the existing map file (C2 "crash safety").
- Verified: edit through CellEditor → markDirty → save → reopen from scratch →
  edit present on disk. saveAll flushes every dirty cell.

MainWindow (Qt6 app):
- File → Open Map… (Qt's own dialog with DontUseNativeDialog — the KDE Wayland
  portal fails silently otherwise), cell-list dock, load-on-click showing room
  count / non-empty squares / level range, status bar with resident+dirty count.
- Recent Maps: File → Open Recent, last 10 map dirs, QSettings-persisted to
  ~/.config/PZMapMaker, de-duped, most-recent-first, with Clear Recent.
- menuBar()->setNativeMenuBar(false) forces the menu into the window; a toolbar
  (Open Map…/Save Cell) backs it up. Both needed on KDE, which otherwise hoists
  or hides the menu bar.
- VERIFIED on real Muldraugh: opened the map dir, cell list populated (0_18…
  4065 cells), clicked cells and saw correct room counts on building cells,
  16384 non-empty squares on wilderness cells (128×128, correct).

Library stays Qt-free: libpzformat.a has zero Qt. Only the app target links Qt6.
CMake uses find_package(Qt6 ... QUIET) so library + 10 test suites build with or
without Qt; the app builds when Qt is present.

**C3 — viewport: UNBLOCKED but GATED.** Do not write the QOpenGLWidget shell
until the 500k-instance-at-1440p harness is built and confirmed <4ms (C1 §1.2).
The one falsification action outstanding before rendering.

**Remaining C2 polish:** cell-search/jump box (4065 cells is a lot to scroll),
a dirty marker in the cell list, and a close-with-unsaved-changes guard on the
main window (confirmDiscardIfDirty exists and is used on open; wire it to
closeEvent too).

### Build/run notes (bit us, recorded so they don't again)
- Files copied to disk sometimes land EMPTY (main.cpp, .gitignore both hit this).
  ALWAYS `wc -l` a dropped file before building. Reliable in-place write:
  fish `cat > file <<'END_OF_FILE' … END_OF_FILE`.
- Stale build: if a rebuilt binary shows old behaviour, check the RIGHT file
  changed (`grep -c "old string" file`), then `rm -rf build && cmake -S . -B
  build -G Ninja && cmake --build build`.
- The binary is build/pzmapmaker; fish needs `./build/pzmapmaker` (the ./).
- Muldraugh map dir on this machine:
  ~/.local/share/Steam/steamapps/common/ProjectZomboid/projectzomboid/media/maps/Muldraugh, KY
  media dir (for TileIndex/atlas): .../projectzomboid/media

### Handoff — SUPERSEDED (was: C2 begun; options C2-polish / harness / SpriteJoin). Current handoff is at end of file.

### C2 polish — cell-search, dirty markers, close guard (2026-08-22)

All three remaining C2 items are implemented. C2 is DONE.

**Cell-search box.** A QLineEdit sits above the QListWidget in the Cells dock.
Substring filter as the user types — items whose cell name doesn't match are
hidden via QListWidgetItem::setHidden. Enter in the search box loads the first
visible match (jumpToFirstMatch). Ctrl+G (File → Go to Cell…) focuses and
selects the search box. Clearing the field shows all cells again
(setClearButtonEnabled).

**Dirty marker in cell list.** refreshDirtyMarkers() walks the list widget and
sets bold font + " *" suffix on any cell in project_->dirtyCells(). Called
after populateCellList, saveCurrent, and saveAll. Dirty state is read from the
CellCoord stored in Qt::UserRole, not from the display text, so the marker
never interferes with cell identification.

**Close-with-unsaved guard.** closeEvent overridden; delegates to the existing
confirmDiscardIfDirty(). event->ignore() if the user cancels, event->accept()
otherwise. Same guard that openMap and openRecent already use.

**Not changed:** CMakeLists.txt, library code, tests. Only app/mainwindow.hpp
and app/mainwindow.cpp were touched.

### Handoff — SUPERSEDED (was: C2 done; options harness / SpriteJoin). Current handoff is at end of file.

### C3 render gate MEASURED — instanced draw confirmed, C1 §1.2 threshold corrected (2026-08-22)

The C1 §1.2 falsifier (the one action outstanding before any C3 viewport code)
is resolved. Full write-up: harness/FINDINGS_harness_2026-08-22.md. Summary:

**The gate PASSES for instanced draw, and its threshold was mis-specified.**
Built harness/instance_harness.cpp (throwaway, no project code): N instances,
one glDrawElementsInstanced, 1440p FBO, alpha-blended, texture-array sampled,
GL_TIME_ELAPSED median over 300 frames, vsync off. RTX 3070 Ti, GL 4.5 core.

Measured (500k instances unless noted, median GPU ms):
- 3px sprite,   4.5M frag,  1.2x overdraw: 0.655 ms
- 9px sprite,  40.5M frag, 11.0x overdraw: 2.543 ms
- 18px sprite,162.0M frag, 43.9x overdraw: 4.814 ms
- 18px, blend OFF, same frag:              4.221 ms
- 200k @ 18px, 64.8M frag, 17.6x:          1.903 ms  (C1's predicted 1:1 load) PASS
- 1M   @ 18px,324.0M frag, 87.7x:          9.634 ms  (linear vs 500k)

CONFIRMED (measured):
- Instancing has no draw-call cliff: 500k->1M is linear. One instanced draw of
  a million instances is fine. C1's "naive per-sprite draw dies" was about
  per-sprite DRAW CALLS, not instanced draw.
- The bound is FILL (fragments on screen), not instance count. 500k instances
  at near-zero fill = 0.655ms (~1.3ns/inst floor); holding count fixed and only
  growing sprite size took it to 4.814ms (7.3x). The 500k FAIL at 18px is a 44x
  overdraw artifact of full-surface scatter, not an instancing limit.
- Blending is minor: 0.59ms of 4.81 (12%). Dominant cost is shaded-fragment
  throughput. (Refutes an earlier working guess that blend RMW was the lever.)
- C1's actual predicted 1:1 load (150-200k inst) passes at 1.9ms.

CORRECTION to C1 §1.2:
- Old: "500k instances <4ms or instanced draw is wrong; fall back to chunked
  merged geometry."
- Actual: the 500k-<4ms gate conflated instance count and overdraw into one
  number. Instanced draw is VIABLE; the fill ceiling at 44x overdraw is what
  fails, and the named fallback would NOT have helped (it cuts draw-call/vertex
  work while this is fragment-bound). The harness earned its cost by ruling out
  the wrong fallback before it was built.

UNVERIFIED / imperfect:
- The fill curve is concave (saturating), not linear: the 9px point is 0.94ms
  above a line through 3px and 18px. Three points don't justify a fitted law;
  the table is the datum. Sweep 3-24px to pin it.
- Real viewport overdraw is NOT known to be below the harness's. A dense PZ cell
  (8 z-levels, blended back-to-front) could approach it. The 200k PASS is the
  COUNT C1 predicted; the harness can't measure real OVERDRAW. First C3 task:
  decode one real dense cell to instances and read its on-screen fragment count.

CONSEQUENCE for C3 (the bound is fragments, so overdraw control is the work):
- Opaque pre-pass (front-to-back, depth write) for opaque ground/floor tiles;
  only translucent sprites take the blended back-to-front pass. Biggest lever.
- Tier-2 LOD crossover is load-bearing, not polish: must fire before on-screen
  fragment count exceeds fill budget. Choose the crossover against measured
  fill, not the assumed 1:32-1:64.

Instanced draw stands. The QOpenGLWidget/QOpenGLWindow shell may proceed on that
path. C3 is now truly unblocked (gate cleared), not merely "unblocked, gated".

### C3 step 1 — GL shell live + dense-cell census MEASURED (2026-08-23)

Delivered: app/mapview.hpp, app/mapview.cpp wired into MainWindow via a
QStackedWidget (placeholder index 0, GL viewport index 1). On first cell load
the stack switches to the GL surface. QOpenGLWindow in createWindowContainer —
NOT QOpenGLWidget, per C1 §1.2 line 60 (Wayland copy-path avoidance). Context
comes up clean on Garuda/KDE/Wayland, no XWayland fallback, no errors.

MEASURED — dense downtown cell 43_26, Muldraugh, Build 42 (256x256, 3 levels):
  instances:     135,635
  non-empty sq:   70,688  (2.07 tiles/square avg)
  levels:         0..2

Fragment estimate (bounding 64x32 quad, 0.5 diamond-alpha factor — both
assumed, not measured; instance count is exact):
  whole cell drawn: ~139M drawn fragments = ~38x overdraw on 2560x1440
  at 1:1 (~2.7% of cell on screen): ~3,700 instances, ~3.8M frag, ~1x overdraw

WHAT THIS CONFIRMS:
1. At 1:1 zoom (the editing view) the viewport is trivially safe. ~1x overdraw
   at normal editing zoom. Enormous headroom. No optimisation needed there.
2. Zoomed-out whole-cell view is the fill risk, not 1:1. 135k instances is well
   under the harness's ~415k instance ceiling, but whole-cell drawn at once is
   ~38x overdraw (~139M frag) — by the harness's 44x→4.8ms reference, ~4.1ms
   for one cell at full zoom. Over budget, and the editor will show multiple
   cells when zoomed out.
3. Instance count is a non-issue for real cells. The risk is fragments/overdraw
   at low zoom. This confirms and sharpens the harness finding.

CALIBRATION TARGET FOR LOD (now concrete, was "1:32-1:64 assumed"):
  LOD must fire before on-screen drawn fragments approach the fill budget.
  Per the harness, fill budget ≈ 415k blended 18px-equivalent fragments.
  Per the census, whole 43_26 at 1:1 = ~139M drawn frag.
  LOD crossover needed before zoom-out brings >~3x the 1:1 on-screen tile size,
  i.e. well before the whole cell is visible at full resolution.
  Exact crossover zoom should be measured against the real tile draw (step 2),
  not estimated from these bounding-quad numbers.

HONESTY NOTE — what is exact vs assumed:
  EXACT: instance count (135,635), square count (70,688), level range (0..2).
         These are a direct walk of the tile data via CellData::tilesAt().
  ASSUMED: kSpriteFragments = 64x32 = 2048px bounding quad; 0.5 diamond factor.
           Fragment/overdraw numbers are ±2x until real textured tiles are drawn
           and the actual fragment cost is GPU-timed. That is step 2's job.

NEXT STEP (C3 step 2):
  Build the textured tile draw with the opaque pre-pass from the start:
  - Pass 1: opaque tiles, front-to-back, depth write ON. Early-Z kills most
    overdraw before it reaches the fragment shader. Biggest fill lever.
  - Pass 2: translucent tiles only, back-to-front, depth write OFF.
  GPU-time both passes on 43_26 and record real fragment cost. This replaces
  the bounding-quad estimate above with a measured number.
  Atlas step deferred: use a 1x1 white texture per tile type initially so the
  draw call structure and timing are real without needing the atlas ported first.

### C3 step 2 — textured two-pass draw MEASURED + QOpenGLWindow retracted (2026-08-23)

Delivered: MapView now does the real two-pass instanced draw. Opaque floors
first (depth write, front-to-back, early-Z), then translucent (walls/objects/
vegetation) blended back-to-front. Placeholder atlas: 1x1 solid tint per tile
name (real atlas is a later step); draw-call structure and GPU timing are real.
Per-pass GL_TIME_ELAPSED timing. Cell renders correctly on Garuda/Wayland.

MEASURED (43_46, whole cell fit-to-window ~940px, 99,830 instances):
  opaque      72,294 inst: ~0.28ms   (floors, depth-write pass)
  translucent 27,536 inst: ~0.14ms   (blended pass)
  total:                   ~0.4ms    (range 0.24-0.60 across frames)

This supersedes the step-1 census ESTIMATE (~4.1ms whole-cell, bounding-quad
guess): the estimate was ~8x too high. Real fill at this zoom is ~0.4ms — huge
headroom under the 4ms budget. Opaque/translucent per-instance cost is ~4.6 vs
~5.1 ns/inst; the blended pass is ~1.1x costlier per instance, as expected.

IMPORTANT SCOPE LIMIT (unmeasured case): this is the whole cell FIT TO WINDOW,
i.e. every tile shrunk to ~3px, so fragment count is capped by the window, not
the cell. It is the cheap zoomed-OUT case. The expensive case — whole cell at
full 1:1 with real per-tile overdraw stacking — needs pan/zoom and is NOT yet
measured. That measurement belongs to step 3. Do not read 0.4ms as the 1:1 cost.

CORRECTION — QOpenGLWindow path RETRACTED (C1 §1.2 line 60 was wrong for this
platform):
  C1 §1.2 said use QOpenGLWindow-in-createWindowContainer because QOpenGLWidget
  has "a copy path that kills frame time" on Wayland. On this Garuda/KDE/Wayland
  setup the OPPOSITE held: QOpenGLWindow-in-container rendered NOTHING VISIBLE.
  Draws executed (timed, zero GL errors, geometry on-screen per CPU projection),
  but the container's native surface and the GL context surface diverged —
  readback of the draw target (draw_fbo=0) returned all-zero including the clear
  colour. Nothing was ever presented.
  Switching to QOpenGLWidget fixed it immediately: draw_fbo=1 (Qt's managed FBO),
  readback returned drawn pixels, cell visible. Measured cost ~0.4ms — the
  "copy path that kills frame time" is not killing anything here. The C1 claim
  was an unmeasured assumption; it is now falsified for this platform and
  QOpenGLWidget is the confirmed path. (If the copy cost ever bites at higher
  zoom/resolution, revisit WITH NUMBERS, not assumptions.)

HOW IT WAS FOUND (method note): three wrong guesses (baseInstance null-ptr,
projection off-screen, depth-clear rejection) were each disproven by
instrumentation rather than argued about. The decisive tools were glGetError
(clean), a CPU-side projection print (geometry on-screen), and glReadPixels of
the draw target (all-zero incl. clear -> wrong surface -> the FBO/present path,
not the render code). Real fixes that survived: depth mapped to a safe [0.2,0.8]
interior range (the old ~1.0 depths were rejected against the depth-clear of 1.0,
blanking the opaque pass), and fit-to-window scale so the 16384px-wide cell fits
a ~940px viewport.

### C3 step 3 — interactive pan/zoom + 1:1 overdraw MEASURED (2026-08-23)

Delivered: wheel zoom (cursor-anchored), left-drag pan, F to re-frame, 1 to jump
to exact 1:1. Camera state zoom_/panX_/panY_, seeded by fitToWindow() on load.

MEASURED — 1:1 zoom, cell 43_46, 99,830 instances: ~0.4-0.5ms total (opaque
~0.33ms, translucent ~0.16ms) — same band as fit-to-window.

ALL FILL CASES COVERED. Bounded from two sides: zoomed out, tiles shrink;
zoomed in, only ~2.7% of the cell is visible. Both ~0.4ms. The expensive case
(all tiles at full size) is unreachable in a single-cell viewport at any zoom.

CONSEQUENCE for LOD: the step-1 census called Tier-2 LOD "load-bearing" on a
~4.1ms whole-cell estimate (bounding-quad, since retracted). Measured fill at
every reachable zoom is ~0.4ms — 10x under budget. LOD is NOT needed for
single-cell editing. It remains warranted only for a multi-cell world-overview
mode (C4/C5). The step-1 LOD-crossover calibration item is CLOSED for C3.

### C3 step 4a — sprite atlas bridge working (2026-08-23)

The library->pixels bridge for real tile sprites works. New app-layer SpriteAtlas
(app/spriteatlas.hpp/.cpp): indexes every .pack via ported PackFile (entry tables
only), then buildLayers() decodes just the pages a cell needs via QImage and blits
each sprite's rect. Library stays dependency-free; QImage decodes PNG in the app
layer only. File -> Set Texturepacks... picks the dir (defaults to Steam path).

MEASURED on real install + cell 43_46:
  indexed 46,540 sprite names from 24/24 packs  (matches oracle count exactly —
    PackFile decode + name indexing confirmed on the full retail set)
  built 114 layers, 10 missing  (104/114 cell tile names resolved to real pixels)

The 10 missing are the "authored tile with no atlas sprite" case flagged during
the port (STATE ~line 116-123). buildLayers now prints each MISSING name so they
can be checked against that known set, not assumed. The renderer must
substitute/flag, not crash — the independent check the port predicted.

Texturepacks path (doubled projectzomboid segment is real, B42):
  ~/.local/share/Steam/steamapps/common/ProjectZomboid/projectzomboid/media/texturepacks

### C3 step 4b — real sprite art rendering (2026-08-23)

The viewport now draws actual PZ tile art. uploadSpriteAtlas() sizes the
GL_TEXTURE_2D_ARRAY to the cell's max sprite dims (126x144 for 43_46), blits each
sprite bottom-left, and stores per-layer (uvW,uvH,ox,oy) in an Nx1 RGBA32F meta
texture sampled by layer index. Vertex shader sizes each quad to the sprite's
real pixel w*h (uv*uAtlasDims), places it at the iso anchor with the entry ox/oy
offset, bottom-centre aligned. Fragment shader alpha-tests in the opaque pass
(transparent sprite pixels must not punch depth holes) and blends in the
translucent pass.

VERIFIED visually on 43_46: grass, foliage, paved road, sidewalk, sand ground
all render as real art in correct iso position. Zoomed in, tiles sit right.
Performance holds ~0.3-0.5ms (first-frame atlas upload is a one-time ~7ms spike).

Missing-sprite handling: the 10 no-sprite vegetation tiles render as small
(16x16) semi-transparent magenta markers — flagged, not hidden. First attempt
filled the whole 126x144 layer opaque magenta, which (with tens of thousands of
vegetation instances) blanketed the map; the small-marker fix cleared it.

Known cosmetic gaps (NOT bugs, deferred):
- Tree/groundcover species substitution (STATE §11) not done — those tiles have
  no direct atlas sprite by design; markers show where they are.
- Depth ordering between overlapping sprites may have minor quirks at some zooms;
  not obviously broken. Revisit if editing needs precise pick order.

### C3 step 5 — level selector + downtown verification + layer-cap limit (2026-08-23)

Level selector (Model B) done and VERIFIED. QSpinBox in the status bar + keys
([ ] step, 0-7 direct) show all tiles with z <= chosen level. Shader discards
z > uMaxLevel. Two-way synced with the view; range auto-matches the cell's
[minLevel,maxLevel]; grays out on single-level cells. Verified on the KNOX BANK
in 41_37: level 1 shows the furnished ground floor (desks, ATM, counter), level
2 shows the upper structure — floor peeling works.

Also this session: default view is now 1:1 centred on the cell (readable) not
fit-to-window confetti (F still gives the whole-cell overview); missing sprites
render invisible not magenta markers; texturepacks path persists via QSettings
and auto-loads on startup (no more re-picking).

Verified the renderer on real geometry: 43_46 is rural (content census:
blends_natural=68k, walls=0, floors=0 — correctly all grass/trees/road, not a
bug). 41_37 is downtown (floors=8912 walls=8035 roofs=3779, levels -1..4) and
renders as a full furnished residential+commercial block. The renderer was never
broken; earlier "black" cells were either rural or the layer-cap bug below.

KNOWN LIMITATION — texture-array layer cap (the real remaining C3 issue):
  The atlas is one GL texture-array layer per distinct sprite. A downtown cell
  uses ~3972 distinct sprites, but GL_MAX_ARRAY_TEXTURE_LAYERS is 2048 (NVIDIA).
  glTexImage3D failed with GL_INVALID_VALUE -> blank atlas -> near-black render.
  DIAGNOSED via a post-alloc glGetError guard (prints the GB + error instead of
  silently going black) — it was layer COUNT, not memory (0.8 GB was fine).
  INTERIM FIX: query the cap, clamp storage to it, skip blitting past it, shader
  discards sprites with layer >= uLayerCount. Result: most of a dense cell
  renders; sprites past 2048 are blank (visible as dark patches on 41_37).
  Also cap individual sprite size at 256px (one 744x982 jumbo sprite sizes every
  layer, so downscale oversized sprites in SpriteAtlas via QImage first).
  PROPER FIX (own step, next): atlas PACKING — pack many sprites into each 2D
  layer of a small array (e.g. 4096x4096 pages) instead of one-per-layer. This
  removes the layer wall entirely and is the right long-term design.

Perf note: first frame after a cell load spikes (~390ms on 41_37) — the atlas
upload (decode+scale ~4000 PNGs) blocks the render thread. One-time per cell;
steady-state is 0.2-1.7ms. Fine for now; async upload later if it annoys.

### C3 polish — pan fix + sprite projection corrected from PZ source (2026-08-23)

PAN BUG (post-pan tiles vanishing):
  After panning, nearly all tiles (floors, roads, walls) vanished; only
  translucent-pass tiles (grass, trees) survived. Resize fixed it; F did not.
  The surviving tiles were exactly the depth-test-OFF pass; the vanishing ones
  were the depth-test-ON (opaque) pass — conclusive. The depth buffer on
  QOpenGLWidget's FBO on this Wayland setup was not clearing/persisting
  correctly between paints that didn't involve a resize. Fix: disable depth test
  in both passes and rely on painter's order (opaque first, then translucent).
  The two-pass structure + alpha-test discard handles layering correctly without
  depth. Also added glViewport() at the top of every paintGL() — QOpenGLWidget
  on Wayland did not reliably preserve the GL viewport between paint calls.

SPRITE PROJECTION CORRECTED (fence stagger, slide in two pieces):
  The iso transform was guessed, not sourced. Decompiled IsoUtils.java from the
  B42 jar gave the exact PZ calculation:
    XToScreen: sx = (x - y) * 32      -- was correct
    YToScreen: sy = (x + y) * 16 - z * 96  -- z factor was wrong (had 48)
  And IsoSprite.prepareToRenderSprite gave the anchor:
    sprite top-left = (ax - 32 + ox, ay - 96 + oy)
  where ox/oy are the pack entry offsets. The old anchor was "bottom-centre"
  (invented, not sourced), which misplaced every tall sprite independently.
  Both values are now exact from the decompiled source. Fences align; multi-tile
  objects (slides, walls) connect correctly.
  METHOD NOTE: PZMapCreation (the Java oracle) never implemented rendering, so
  there was no oracle for sprite placement. The right source was the decompiled
  PZ game binary directly (IsoUtils.class, IsoSprite.class from the B42 jar).
  This is the correct approach for any future rendering questions.

### Status pointer — 2026-08-23 (live, not a handoff)
Port: DONE. C1: DONE (corrected). C2: DONE.
C3: COMPLETE as a viewer. Real PZ art with correct sprite placement, pan/zoom,
  level selector, persistent texturepacks, readable 1:1 default. One known
  limitation: 2048-layer array cap blanks some sprites on dense cells (interim
  clamp; proper fix = atlas packing).
NEXT, choose:
  (a) Atlas packing — remove the layer cap. Pure rendering-quality work.
  (b) C4 — picking/editing: click a tile, see/change it. Turns viewer to editor.


### C4 picking — click-to-identify DONE (2026-08-25)

The read side of C4 is complete and verified. The write side (tile replacement,
brushes, undo UI) is next and depends on A3.

**What was built (app layer only, no library changes):**

`MapView` — picking subsystem:
- `screenToTile(cx, cy)` — inverse of the PZ iso transform (IsoUtils.java,
  sourced from the decompiled B42 jar). At z=0 ground plane:
    wx = (ax/32 + ay/16) / 2,  wy = (ay/16 - ax/32) / 2
  where ax/ay = (screen - pan) / zoom. Returns cell-local (tx, ty) or (-1,-1)
  outside the cell.
- `squareTiles_` — flat CPU-side retention of every tile index per (x,y,z),
  filled during `buildInstances`. Enables picking without retaining the
  full CellData pointer.
- `cellTileNames_` — mirrors `cell.header().tileNames` for name resolution.
- `tileClicked(int tx, int ty, QVector<QPair<int,QString>> tiles)` signal —
  emitted on left-click (not drag; 3px threshold). Carries (z, name) pairs
  in painter's order, low z first.
- `setMouseTracking(true)` in constructor — required for mouseMoveEvent to
  fire without a button held. Missing this caused hover to only work during
  drags.

`MapView` — hover and selection overlays:
- A second minimal GL program (`overlayProg_`, `overlayVao_`, `overlayVbo_`)
  draws coloured `GL_LINE_LOOP` diamonds. Separate from the sprite shader;
  4 pre-projected NDC vertices uploaded per frame via `glBufferSubData`.
- `hoverTile_` — updated in `mouseMoveEvent` (every move, not just drags).
  Cleared in `leaveEvent`. Yellow diamond.
- `selectedTile_` — set on click, persists until next click. Cyan diamond,
  drawn before hover so yellow sits on top when both occupy the same square.
- `glLineWidth(2.0f)` is illegal in GL 4.5 core profile and was removed.
  Line width stays at 1.

`MainWindow`:
- `TileIndex tiles_` — loaded from `$PZ/media` when texturepacks are
  indexed. Path derived by stripping trailing slash from the texturepacks
  dir and calling `parent_path()`. B42 puts `.tiles` files directly in
  `media/` (no subdirectory); `TileIndex::load` scans for `*.tiles` there.
  Confirmed: 61,418 tiles loaded on startup.
- `tileClicked` lambda — formats the Tile Info dock: coordinate header, then
  for each z-level a `z = N` header, indented tile name, and doubly-indented
  properties (`key` or `key = value`).
- Tile Info dock — `QTextEdit`, read-only, monospace 9pt, right dock area.

**CONFIRMED (verified in the running app):**
- `screenToTile` is accurate: clicking a tile whose name is visible in the
  viewport returns the correct (tx, ty) and the Tile Info panel shows the
  expected tile names and properties.
- Hover diamond tracks the cursor continuously and disappears on leave.
- Selection (cyan) persists across mouse moves; yellow hover sits on top.
- z-level grouping is correct: a doorway square showed floor (z=0), interior
  walls, door frame, door, then ceiling and roof (z=1) in the correct order.
- Property display is correct: `blends_natural_01_38` showed `FloorMaterial
  = Grass_Medium`, `grassFloor`, `solidfloor`, etc. matching known tile data.

**OPEN (not bugs, deferred):**
- `GL_INVALID_OPERATION` (0x0501) fires every frame during hover/selection
  draw. Source is not yet identified — `glLineWidth` was the known cause and
  was removed; a second call site may exist elsewhere in the overlay path, or
  it may be a uniform or VAO state issue. Does not affect correctness; fix
  before errors become noise that hides real problems.
- `selectedTile_` is not cleared when a new cell is loaded. After loading a
  different cell, the cyan diamond may appear at stale coordinates. Fix:
  call `selectedTile_ = {-1,-1}` in `clearCell()` and at the top of
  `setCell()`.

**NEXT:** C4 write side — tile replacement through `CellEditor`. Depends on
A3-pre1 and A3-pre2 (wall joining prerequisites) per CHUNKS.md, but a
simpler first step is floor replacement (no wall-join logic needed) to
prove the edit → dirty → save → reload loop works end-to-end in the UI.

### C4 write side — floor replacement DONE (2026-08-25)

First real editing action works end to end: click a tile, type a floor name,
Set Floor -> CellEditor::setFloor -> markDirty -> refreshCell. Verified in-app on
41_37 (downtown). Ctrl+S saves; reload from disk confirms persistence.

**What was built:**
- Tile Info dock gained a write row: QLineEdit (tile name) + "Set Floor" button.
- Set Floor handler in MainWindow: validates the name against TileIndex, reads
  the target z from the level spinbox (working level == view level, as intended),
  calls `lc.editor->setFloor(tx, ty, z, name)`, marks the cell dirty, and
  refreshes the viewport.
- `MapView::refreshCell(cell)` — NEW. Rebuilds instances + picking data from
  edited cell data WITHOUT touching zoom/pan/level or re-framing. This is the
  edit path; `setCell` remains the load path (which does re-frame). Fixes the
  camera-recenter-on-edit bug.
- `MapView::clearSelection()` — clears the cyan selection diamond; called on
  cell load so selection doesn't persist across cells (the deferred C4 item).
- Working level now persists via QSettings("workingLevel"): the spinbox no
  longer jumps to the cell's max on every load. It restores the last-used level,
  clamped to the new cell's [min,max]. Fixes "always opens on level 4".

**Sprite atlas rebuild on edit — conditional, and why:**
`CellData::tileIndex(name)` appends a new name to tileNames if not present,
giving it a new index past the atlas layers built at load. So an edit that
introduces a genuinely new name needs a sprite-layer rebuild. BUT rebuilding
unconditionally re-packs the layer array, and on a cell exceeding the 2048
GPU layer cap the re-pack shuffles which sprite shows on layers past the cap,
making unrelated squares appear to change. Fix: rebuild ONLY when
`tileNames.size()` actually grew (namesAfter != namesBefore). In the normal
workflow (painting a tile the cell already contains) there is no rebuild and
the edit is instant. The residual case (new name on a cap-exceeding cell) is
the 2048-cap limitation, whose real fix is atlas packing (own chunk).

**IMPORTANT non-bug — multi-tile floor sprites:**
Spent a while chasing a "paints 4 tiles instead of 1" report. It is NOT a bug.
Proven by dumping the 3x3 data block after each edit: the DATA always changed
exactly one square (setFloor is correct; CellData::setSquare writes one slot).
The visual "4 tiles" comes from sprites whose logical tile size (fx,fy) exceeds
64x128 — e.g. some "Beige Checkered Tiles" floors are 2x2-block sprites whose
art covers a 2x2 area. Painting one square with such a sprite shows art over
four tile positions. Confirmed by printing sprite metadata:
  floors_interior_tilesandwood_01_21: w=63 h=32 fx=64 fy=128 -> single tile,
    paints one square (correct).
  floors_interior_tilesandwood_01_54 / _35: larger sprites -> art spans 2x2.
The shader already places sprites by per-sprite (ox,oy,fx,fy) from PZ's
prepareToRenderSprite, so this matches how PZ itself renders. TAKEAWAY for the
tool palette: floors are not all 1x1; palette thumbnails must show the sprite's
real extent so the user knows whether a tile is single or block-spanning.

**Palette preload — the planned next step (design settled, not yet built):**
The paint workflow should draw from a fixed tool palette of paintable tiles,
loaded into the atlas UP FRONT. Then painting never grows the atlas, never
rebuilds, never shuffles. Constraints worked out this session:
- Cannot load all 46,540 pack sprites: at 197x256 RGBA8 (~200KB/layer) that is
  ~9.3 GB and 22x over the 2048-layer cap. Not viable as one-per-layer.
- Palette-scoped preload IS viable: a floor palette is a few hundred sprites
  (tens of MB, well under the cap). Load the palette's sprites at startup on
  top of the cell's own layers. This is the smallest correct fix for editing.
- The general fix remains atlas PACKING (many sprites per 4096x4096 page: a
  197x256 sprite packs ~320/page, so 46,540 sprites -> ~146 pages, trivially
  under any cap). Packing removes the layer wall for both viewing and editing
  and is the right long-term design. LRU paging sits on top of packing if the
  full tileset is ever needed resident at once.

**Still open (deferred, not blocking):**
- GL_INVALID_OPERATION (0x0501) per-frame: FIXED earlier by removing
  glLineWidth (core profile). Confirmed gone.
- setSquare/setFloor only handle floors so far. setWall/removeWall/addObject
  exist in CellEditor but have no UI yet. Next editing actions.
- Undo/redo: CellEditor has the journal; no UI binding yet (Ctrl+Z/Y).

**NEXT:** either (a) build the tool palette + palette-preload atlas so painting
is a first-class action with a picker, or (b) wire more CellEditor ops (walls,
objects, undo) to the UI. (a) is the higher-value path toward a usable editor.

### Design note — tile footprint preview (2026-08-25)

**Idea (owner):** when a tile name is loaded in the Set Floor edit box, the
viewport's cyan selection diamond should expand to show the actual footprint
of that sprite BEFORE the user clicks Set Floor. If the tile is a 2×2 block
sprite, the outline covers 4 squares. This warns the user they are about to
overpaint more than the single square they designated.

**Why this matters:** confirmed this session that some floor sprites are
multi-tile (e.g. `floors_interior_tilesandwood_01_54` covers a 2×2 area).
Painting one square with such a tile visually changes 4 squares — the data
is correct (one square changed) but the art spills over. Without a footprint
preview, the user has no way to know the tile they are about to place will
cover more than the selected square.

**How to implement:** the metadata is already available.
`SpriteAtlas::Layer` carries `fx, fy` (logical tile size). A single-tile
floor is `fx=64, fy=128`. Tile span = `fx/64` wide × `fy/128` deep. To get
this without a full PNG decode: `atlas_.buildLayers({name})` on a single name
is fast (just metadata lookup for the entry dimensions) — or better, add a
`SpriteAtlas::queryMeta(name) -> Layer` that reads the pack entry dimensions
without decoding pixels (pack entries carry w/h/ox/oy/fx/fy already in the
entry table; no decode needed).

**Viewport change:** `drawHoverOverlay` currently draws a fixed single-tile
diamond. Change it to accept a footprint size `(tw, td)` (tiles wide, tiles
deep) and draw a parallelogram covering `tw × td` tile positions starting
from the hovered/selected square. For `tw=td=1` this is the existing diamond.

**When to implement:** this is the first item in the palette/tool chunk. It
should ship alongside the tile picker so the footprint preview works for
palette selections, not just typed names. Logging here so it is not forgotten.

### Design note — stamp brush interaction model (2026-08-25)

**Confirmed interaction design** (researched against Tiled, Godot tilemap editor,
Tilesetter, and other industry-standard tile editors — all converge on the same
pattern):

| Gesture | Action |
|---|---|
| Left-click (no brush loaded) | Inspect tile — current behavior |
| Left-click (brush loaded) | Place brush at that square |
| Left-drag (brush loaded) | Paint stroke across multiple squares |
| Right-click any tile in viewport | Pick up floor tile as active stamp brush |
| Middle-drag OR Alt+left-drag | Pan — works in ANY mode, brush or not |
| Scroll wheel | Zoom — unchanged |
| Escape | Clear brush, return to inspect mode |

**Why Alt+drag for pan:** this is the standard that frees left-click entirely
for painting without mode collision. Pan is available at any point during a
paint operation without dropping the brush or switching modes. This directly
satisfies the requirement: pan freely between picking up a material and placing
it, while the brush stays loaded.

**What needs to be built:**
- Brush state object in MapView (active tile name + sprite metadata for
  footprint sizing). Null = inspect mode, non-null = paint mode.
- Right-click in viewport: if tile under cursor has a floor, load it as brush.
  Show the tile name + footprint in the Tile Info dock immediately.
- Alt+left-drag: pan (same math as current left-drag pan, gated on Alt held).
  Current left-drag pan remains as-is when no brush is loaded; Alt+drag is
  the universal pan that works regardless of brush state.
- Left-click while brush loaded: call setFloor at the clicked square, z =
  levelSpin value. Drag threshold (3px, already in code) separates click-place
  from drag-stroke.
- Left-drag while brush loaded: paint stroke — call setFloor on each new square
  entered during the drag (track last-painted square to avoid redundant writes).
- Footprint preview: hover diamond expands to the brush tile's footprint
  (fx/64 wide × fy/128 deep) using the already-designed queryMeta path. This
  is the visual warning for multi-tile sprites (see earlier design note).
- Escape: clear brush, restore inspect mode hover diamond.

**Interaction with existing code:**
- `mousePressEvent`, `mouseMoveEvent`, `mouseReleaseEvent` all need brush-state
  awareness. The 3px drag threshold (pressX_/pressY_) already exists.
- `drawHoverOverlay` already handles the diamond; footprint sizing is additive.
- `setFloor` write path already works; stroke painting calls it per square.
- Middle-drag pan: Qt delivers middle-button events via `Qt::MiddleButton` in
  `mousePressEvent` — same pan math as current left-drag, separate button check.

**NOT yet built. Logged for the palette/tool chunk.**

### C4 stamp brush + pan — DONE (2026-08-25)

Items 1–4 of the stamp brush chunk are complete and verified.

**Middle-drag pan (item 1).** `Qt::MiddleButton` in `mousePressEvent` sets `midDragging_`; `mouseMoveEvent` pans when `midDragging_` is true. Self-contained in `MapView`; no MainWindow change needed.

**Alt+left-drag pan.** `altHeld_` tracked via `keyPressEvent`/`keyReleaseEvent`. Alt+drag pans instead of painting. KDE intercepts Alt+drag at the compositor level as a window-move gesture — confirmed not working on this setup. Kept in code for non-KDE use; Space or Ctrl alternatives not yet tried.

**Brush state + right-click pickup (item 2).** Right-click in the viewport emits `floorPickedUp(name, z)`. `MainWindow` receives it, calls `view_->setBrush(name, atlas)` and syncs the level spinbox to the z where the tile was found. `setBrush` calls `atlas.queryMeta(name)` (metadata-only, no PNG decode) to compute `brushW_ = fx/64`, `brushD_ = fy/128` for footprint sizing.

**Footprint preview (item 3).** `drawHoverOverlay` draws a green `brushW_ × brushD_` parallelogram in brush mode instead of the single yellow diamond. 1×1 for standard floors; expands for multi-tile sprites. Selection diamond hidden while brush is active.

**Left-click/drag to paint (item 4).** In brush mode, left-click emits `paintTile(tx, ty)` on release. Left-drag emits `paintTile` for each new square entered past the 3px threshold. `MainWindow` calls `CellEditor::setFloor` at the spinbox z, marks dirty, refreshes viewport. Escape clears brush and returns to inspect mode.

**Bugs found and fixed during this session:**

- `mainwindow.cpp` connections (`floorPickedUp`, `paintTile`) were not in the binary when the patch was first applied — middle-drag worked (self-contained) but 2-7 silently did nothing. Fixed by delivering full files.
- `Qt::DefaultContextMenu` policy would have intercepted right-click before `mousePressEvent`. Fixed with `setContextMenuPolicy(Qt::NoContextMenu)` in the `MapView` constructor.
- Right-click pickup hardcoded `z_slot=0` (minLevel), but ground-floor tiles are at z=0 game coordinates which is `z_slot = 0 - minLevel`. For this cell (minLevel=-1) the grass was at z_slot=1, so slot 0 was always empty. Fixed by scanning all z_slots from minLevel upward.
- Painting landed 3 squares up-right from the green outline because the level spinbox was at z=1 while pickup found the tile at z=0. Each extra z-level shifts the iso render by ~3 tile-widths visually. Fixed by emitting the found z in `floorPickedUp` and syncing the spinbox on pickup.

**Files changed:** `app/mapview.hpp`, `app/mapview.cpp`, `app/spriteatlas.hpp`, `app/spriteatlas.cpp`, `app/mainwindow.cpp`.

**CHUNKS update:** C4 stamp brush items 1–4 done. Item 5 (tile picker panel) is next within C4.


### C4 brush footprint placement correction — 2026-08-25

A `MapView` update fixed the multi-tile brush placement offset. Previously,
painting a multi-tile material, such as a 4×4 floor sprite, over a 1×1 material
could place the visible art offset from the green footprint preview.

The placement direction is now `+(brushW_-1, brushD_-1)`, matching the preview
box. Verification: rebuilt, picked a 4×4 material, painted where the green box
showed, and checked the `[paint]` log line against the visible result.

**CONFIRMED:** the art lands perfectly in the green footprint box. The preview
and write placement now agree for multi-tile floor brushes.


### C4 brush footprint placement final correction — 2026-08-25

The first C4 brush footprint note was incomplete. The initial click-only fix
handled one 4x4-on-1x1 case, but drag painting and multi-tile-on-multi-tile
painting exposed that three coordinates had been conflated: cursor tile,
visible footprint box, and write tile.

Final verified model:
- `brushBoxOriginForCursor()` computes the visible green footprint origin from
  the cursor tile using `brushW_/2` and `brushD_/2`.
- The green preview, click paint, and drag paint all derive from that same box.
- `mouseMoveEvent` updates `hoverTile_` before emitting drag paint, so the
  preview no longer lags one mouse event behind the write.
- `1x1` brushes write at the box origin.
- Multi-tile brushes write at `box + (brushW_, brushD_)`.

**CONFIRMED:** `1x1` material painted onto `1x1` lands exactly at the intended
square, and multi-tile material painted onto multi-tile material lands correctly
in the green footprint box for both click and drag painting.

Method note: several plausible formulas were wrong in different directions.
The useful diagnostics were the printed `cursor`, `box`, `write`, `brush`, and
`hover` values plus visual checks. Do not simplify this back to a single
`brushW_-1` style anchor without re-running both cases.
`
`### C4 stamp brush placement — final verified correction (2026-08-26)

The C4 stamp brush placement issue is now CONFIRMED fixed in the running
application.

The affected material is a **2×2 footprint (4 map cells)**, not a 4×4
four-cell footprint. Right-click pickup captures the four floor cells as a
stamp, the viewport shows the corresponding green 2×2 footprint, and
left-click placement now puts the rendered material inside that green
footprint.

The final placement model has three distinct coordinates and they must not
be conflated:

* `cursorTile` — the map square under the mouse.
* `brushBoxOriginForCursor()` — the visible footprint origin used by the
  green preview.
* the write anchor — the map coordinate used when the captured stamp is
  written.

`MapView` remains responsible for the cursor/footprint geometry. The
`paintStamp(boxX, boxY, cells)` signal passes the footprint origin and the
captured per-cell offsets to `MainWindow`.

The final stamp write correction is:

```text
1×1:
    write = box

multi-cell:
    write = box + (stampW - 1, stampD - 1) + cellOffset
```

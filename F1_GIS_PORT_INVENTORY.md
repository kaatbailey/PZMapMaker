# F1 — GIS port inventory and port order

**Chunk:** F1. **Date:** 2026-08-31. **Deliverable:** document, no code.

**Trace basis.** `github.com/kaatbailey/PZMapCreation` at `0247ddc` (2026-08-21,
"Refactor BuildingPlan to use hub-based dwelling layouts"), 65 files under
`src/main/java/pzformat/`, 17,655 lines. Verdicts come from a static call graph:
comments and string literals stripped, then every class name matched as a word
boundary in every other file, then transitive reachability computed from
`GisCells` — the entry `Probe giscells` dispatches to (`Probe.java:29` →
`GisCells.run`). Not from either existing list.

**Caveat to check before acting on this.** The public repo HEAD is dated
2026-08-21 and today is 2026-08-31. If the local tree has uncommitted GIS work,
this inventory is stale. One command settles it:

```fish
cd ~/Documents/PZMapCreation; and git status --short; and git log -1 --format='%h %ad' --date=short
```

---

## 1. Prediction vs result

Written before the trace (`F1_PREDICTIONS.md`, reproduced in §7).

| | predicted | actual |
|---|---|---|
| SHIPS units (excluding already-ported format layer) | 17 | **14** |
| Files touched by the generation path | 20–24 | **24** (14 GIS + 10 already ported) |

**Prediction 1 — WRONG, and this is the good news.** I predicted `RoomLayout`
and `RoomMinimums` would turn out to be called at generation time despite
STATE listing them as surveys. They are not. Neither is `HouseRules`,
`HallRule`, `RoomShapes`, `HouseLayouts`, or `DitherLaw`. All have a `main` and
**zero callers**. E13's layout engine was inlined into `BuildingPlan`, which has
**zero dependencies on any other `pzformat` class** — 3,347 lines of pure
function. STATE's survey list was right about these files for the wrong reason,
and is right for the port too.

**Prediction 2 — resolved the cheap way.** `WorldGenBiomes` and
`WorldGenFeatures` parse the game's Lua. I flagged them as the biggest cost
uncertainty in Track F, because if they ran at generation time the C++ port
would need a Lua-ish parser. They do not. Their only caller is `BiomePalette`,
which itself has **zero callers** and a `main`. All three are SURVEY.
**Track F needs no Lua parsing.**

**The prediction that mattered and I got wrong the other way:** I predicted
`WaterTiles` SHIPS. It does not — see §4.

---

## 2. Inventory — the generation path

24 files are reachable from `GisCells`. Ten are the already-ported format layer
and are listed here only as dependencies, per the F1 prompt.

### 2a. SHIPS — 14 units, 6,854 lines

| file | lines | depends on | depended on by | oracle |
|---|---|---|---|---|
| `GisCells` | 864 | `BiomeMapWriter`, `BuildingPlan`, `FootprintSnap`, `GisImport`, `GroundMaterial`, `GroundPalette`, `GroundRegions`, `TilePalette`, `TreePalette`, `TreeScatter`, *(ported: `CellData`, `LotHeader`, `SpriteNames`, `TileIndex`)* | `Probe` | **Byte-identical mod output** vs Java on the same GeoJSON. The strongest oracle in the project. Requires the LCG (§5). |
| `GisImport` | 491 | `GeoJson`, `FootprintSnap` | `GisCells`, `GroundRegions`, `TreeScatter`, `BiomeMapWriter`, `Probe` | Identical `Cover` grid cell by cell, plus identical `buildings` list in import order. Dump both to text and `diff`. |
| `BuildingPlan` | 3,347 | **(none)** | `GisCells` | **Its own self-test**, ported and matching: `java -cp out pzformat.BuildingPlan`, 14,680 layouts. Pure function, zero PZ deps. Needs the LCG. |
| `TilePalette` | 324 | *(ported: `TileDefs`, `TileIndex`)* | `GisCells` | Same `TileIndex` in → identical tile-name table out. |
| `GroundRegions` | 308 | `GroundMaterial`, `MaskRule`, `GisImport`, *(ported: `CellData`)* | `GisCells` | Identical region + mask assignment over a fixed `Cover` grid. Position-hashed dither (`hash01`, line 291) must reproduce exactly. |
| `FootprintSnap` | 299 | **(none)** | `GisCells`, `GisImport` | Has a `main` (line 256). Port it and match. Pure geometry, no RNG. |
| `MaskRule` | 257 | **(none)** | `GroundRegions` | Has a `main` (line 143) with fixed seeds — `new Random(1)`, `new Random(12345)`. Port the test; identical mask arrays. Needs the LCG. |
| `TreeScatter` | 210 | `GisImport`, `TreePalette` | `GisCells`, `BiomeMapWriter` | Identical tree grid for a fixed seed. **Gated — see §4.** |
| `GroundPalette` | 206 | *(ported: `TileIndex`)* | `GisCells` | Identical `Ground` draw for a fixed `Random` sequence. Needs the LCG. |
| `BiomeMapWriter` | 132 | `GisImport`, `TreeScatter` | `GisCells` | Byte-identical `biomemap_X_Y.png`. **PNG encode required** — see §6. |
| `TreePalette` | 120 | *(ported: `TileDefs`, `TileIndex`)* | `GisCells`, `TreeScatter` | Same `TileIndex` in → identical variant lists out. |
| `Json` | 117 | **(none)** | `GeoJson` | Same file → same parse tree. No PZ dependency at all. |
| `GroundMaterial` | 114 | **(none)** | `GisCells`, `GroundRegions` | Enum + table. Identical priority ordering (STATE §27's table, confirmed over 4,065 cells). |
| `GeoJson` | 65 | `Json` | `GisImport`, `FootprintAngles` | Same file → same features, same properties, same ring order. |

### 2b. Already ported — dependencies only, no work

`CellData`, `LE`, `LEW`, `LotHeader`, `LotPack`, `PackFile`, `SpriteNames`,
`TileBin`, `TileDefs`, `TileIndex`. All verified byte-identical against Java on
retail data.

### 2c. DEAD — 1 unit

| file | verdict | replaced by |
|---|---|---|
| `GisCells.writeWorldGenOverride` (method, `GisCells.java:516`) | **DEAD — do not port** | The biome map (`BiomeMapWriter`). A2b confirmed it inert 2026-08-11. |

**It is still called**, at `GisCells.java:371`. A2a/A2b were marked confirmed but
the deletion never happened. F6 must skip this method, and the byte-identical
oracle will then fail on one extra file in the Java output — either delete it in
Java first (A2b's actual remaining work) or exclude `WorldGenOverride.lua` from
the diff and say so.

### 2d. SURVEY — 41 files, do not port

Zero callers, `main` only, writes nothing the generator reads:

`BiomePalette`, `DitherLaw`, `DoorProbe`, `FootprintAngles`, `GroundCensus`,
`GroundSurvey`, `HallRule`, `HouseLayouts`, `HouseRules`, `LegacyPackProbe`,
`MaskAudit`, `PaletteScan`, `RoomCluster`, `RoomLayout`, `RoomMinimums`,
`RoomShapes`, `TreeSurvey`, `WallCycle`, `WaterTiles`, `WorldGenBiomes`,
`WorldGenFeatures`, `ChunkDataAnalysis`, `LotPackAnalysis`, `PackAnalysis`,
`TileBinAnalysis`, `TrailerAnalysis`, `PropsProbe`, `RoundTrip`, `Survey`,
`GroundCensus`.

Editor-track or already handled, not GIS: `CellEditor`, `CellRenderer`,
`EditDemo`, `Locate`, `MakeTestMod`, `MapValidator`, `RoomGeometry`, `SpawnMark`,
`SpriteAtlas`, `SpriteJoin`, `Square`.

`Probe` (293 lines) is the CLI harness. Not ported — **F7's menu action is its
replacement**, and that is the accessibility deliverable.

---

## 3. Port order

Leaves first, so nothing rests on something unverified. Seven steps; the
existing F2–F6 split holds, with two additions.

| step | units | lines | oracle |
|---|---|---|---|
| **1** | `Json`, `GeoJson` | 182 | Same `.geojson` → same features, properties, ring order. Feed it `~/pzgis/tokyo/buildings.geojson` (59 buildings) and `~/pzgis/buildings.geojson` (7). Dump features to text from both trees and `diff`. **No PZ install needed — this step is testable in isolation.** |
| **2** | **the LCG** (`java.util.Random`) | ~40 new | `nextDouble()` and `nextInt(n)` must match Java for a fixed seed over 10⁶ draws. **Do this before anything else that draws randomness or every later diff is noise.** Not a port of an existing file — new code. |
| **3** | `FootprintSnap` | 299 | Its own `main` (line 256), ported and matching. Pure geometry, no RNG, no deps. |
| **4** | `BuildingPlan` | 3,347 | Its own `main` (line 2614), 14,680 layouts. Depends on step 2 only. **Half the port's lines and it stands entirely alone** — it can be done in parallel with everything else. |
| **5** | `GroundMaterial`, `TilePalette`, `TreePalette`, `GroundPalette`, `MaskRule` | 1,021 | Same `TileIndex` in → identical tile-name tables out. `MaskRule` has a fixed-seed `main` (line 143). Needs a PZ media dir. |
| **6** | `GisImport` (`rasterise` only), `GroundRegions` | 799 | Identical `Cover` grid, dumped cell by cell and diffed. Then identical region/mask assignment over that grid. |
| **7** | `TreeScatter`, `BiomeMapWriter`, `GisCells` | 1,206 | **Byte-identical mod output vs Java** on both the Ohio and Tokyo datasets. Nowhere for an interpretation bug to hide. |

**Where this differs from CHUNKS Track F as written:** F2/F3 hold. F4 loses
`WaterTiles` (§4) and gains `TreePalette`, `GroundPalette`, `MaskRule`,
`GroundMaterial`. F5 gains `GroundRegions`. F6 gains `TreeScatter` and
`BiomeMapWriter`. And **the LCG is promoted from a footnote to its own step** —
six of the fourteen SHIPS units draw randomness.

---

## 4. Corrections

These are the findings. Each contradicts a written claim.

**1. `WaterTiles` is a SURVEY, not a port target.** `CHUNKS.md` F4 lists it as
one of three units to port. It has a `main`, takes a `MEDIA_DIR`, prints tiles
whose name or properties contain "water", and **has zero callers** — nothing in
the tree references it but itself. The shipped water path is `GisImport.Cover.WATER`
+ `waterLine()` (line 416) with the tile name resolved through `TilePalette`.
The file that taught the rule is not the file that applies it.
→ *Old: F4 ports `WaterTiles`. Actually: it is read-only; the water tile
selection is already inside `TilePalette`. Evidence: `grep -l WaterTiles *.java`
returns only `WaterTiles.java`.*

**2. `GisCells` does not write through `CellEditor` or `Square`.** STATE
2026-08-31 "what the next chunk needs to know" says `CellData`, `LotHeader`,
`LotPack`, `TileIndex`, `Square`, `CellEditor`, `MapProject` are what `GisCells`
writes through. The trace shows `CellEditor`'s only callers are `EditDemo` and
`RoomGeometry`; `Square`'s are `CellData`, `CellEditor`, `EditDemo`, `PropsProbe`.
`GisCells` manipulates `CellData` directly through its own private helpers
(`appendTile:827`, `replaceTile:790`, `clearSquare` equivalents).
→ *Consequence for F7, and it is not small: the GIS generator bypasses the
undo-capable edit layer entirely. A generated map cannot currently be undone in
the editor, and "generate into the open project" is a different piece of work
from "generate to disk".*

**3. There is no `fillPolygon` in `GisImport`.** `CHUNKS.md` E15 says areal water
fill is "small if `fillPolygon` is reusable — buildings already use it."
Buildings do not use it. Buildings go through `FootprintSnap.Rect` — they are
snapped to rectangles (E5, STATE §31), never rasterised as polygons. There is no
scanline fill anywhere in the file. E15 needs one written from scratch.
→ *Old: E15 is small because a fill primitive exists. Actually: no fill
primitive exists. Evidence: `grep -n fillPolygon GisImport.java` → nothing.*

**4. E15's premise is CONFIRMED by reading, without generating Tokyo.**
`GisImport.java:150–157`:

```java
for (GeoJson.Feature f : water.features) {
    int hw = waterWidth(f.prop("fcode"));
    for (List<double[]> ring : f.rings) {
        List<int[]> pts = g.project(...);
        for (int i = 0; i + 1 < pts.size(); i++)
            g.waterLine(pts.get(i), pts.get(i + 1), hw);
```

It walks each ring calling `waterLine` between consecutive points. That traces a
perimeter. A `natural=water` polygon comes out as a ring of water with dry ground
inside. **The Tokyo render is still worth running** — reading the loop is
reasoning, and reasoning is what produced the x/y transposition (STATE §4) — but
the defect is no longer UNVERIFIED-by-inspection.

**5. `writeWorldGenOverride` was never deleted.** A2b is marked
`✅ CONFIRMED INERT 2026-08-11` and STATE §3 item 2 says delete it. It is called
at `GisCells.java:371`. Inert in game, but still written to disk, and it will
appear in F6's byte-identical diff.

**6. STATE §6's Java file inventory is badly stale.** It lists roughly 35 files;
65 exist. Missing entirely: `BuildingPlan` (3,347 lines — the largest file in
the project), `GroundRegions`, `GroundMaterial`, `MaskRule`, `MaskAudit`,
`FootprintSnap`, `GeoJson`, `WaterTiles`, `DitherLaw`, `HouseRules`, `HallRule`,
`RoomLayout`, `RoomMinimums`, `RoomShapes`, `HouseLayouts`, `RoomCluster`,
`DoorProbe`, `GroundCensus`, `ChunkDataAnalysis`, `Locate`. §6 does say "refresh
the real list with `find`" — that instruction is load-bearing and should be
promoted, or the list should carry a date.

---

## 5. RNG — six units, not one

`java.util.Random` appears in six SHIPS units. `std::mt19937` reproduces none of
them. The LCG must be ported explicitly (step 2).

| unit | seeding |
|---|---|
| `GisCells` | `new Random(SEED*31 + cx*7919 + cy)` per cell (line 157); `SEED*131 + di` per dwelling (85); `SEED*131 + bi` per building (271) |
| `GroundRegions` | takes an `rng`, but the dither is `hash01(gx, gy, seed)` (line 291) — **position-hashed, not a sequential draw** |
| `MaskRule` | `masks(..., Random)`, fixed seeds in its self-test |
| `TreeScatter` | `new Random(seed)`, `nextDouble`/`nextInt` |
| `GroundPalette` | `roll(Random)` — `nextDouble` against cumulative tables |
| `BuildingPlan` | `nextDouble` against fixed thresholds (`LK_OPEN`, 0.62, 0.16) |

**Checked and clear:** no iteration-order hazard. `GisCells` uses
`LinkedHashMap` for `shared` (line 630), and the one `HashSet` (`doorAt`,
line 670) is membership-only — `.contains`, never iterated. `TilePalette` and
`BuildingPlan` use `LinkedHashMap`. C++ `std::unordered_map` will not silently
diverge here, but keep insertion-ordered containers anyway.

---

## 6. The PNG, and why it is smaller than STATE thinks

STATE and the F1 prompt both frame `GisImport`'s schematic PNG as a deferred
dependency that Qt's `QImage` resolves. Sharper: **the schematic is not on the
generation path at all.**

`GisCells.java:48` calls `GisImport.rasterise(...)`, not `GisImport.run(...)`.
`writeSchematic` (line 458) and the only `java.awt` / `ImageIO` imports in the
file are reached solely from `run`, which is the `Probe gisimport` diagnostic
path. F5 can port `rasterise` **dependency-free**, and F7 reimplements the
schematic with `QImage` as a preview pane if it wants one.

**PNG encode is still needed once, for a different reason.**
`BiomeMapWriter` (step 7) writes `maps/biomemap_X_Y.png` — biome + zone, one
pixel per tile — and the game reads it. That is a real output file, not a
diagnostic, and it must be byte-identical or the terrain continuity win (STATE
§2) regresses. Qt's `QImage` covers it; the generator is app layer so Charter §3
is unaffected. But note the constraint: **byte-identical PNG requires matching
Java's `ImageIO` encoder output exactly** — same colour type, bit depth, filter
choice, zlib level. If `QImage` will not match, the oracle for step 7 must
compare *decoded pixels*, not file bytes, and that weakening should be recorded
rather than discovered.

---

## 7. Predictions, as written before the trace

> Predicted SHIPS count: 17 (range 15–20). List: `GisCells`, `GisImport`,
> `GeoJson`, `Json`, `TilePalette`, `GroundPalette`, `WaterTiles`,
> `BiomePalette`, `BiomeMapWriter`, `WorldGenBiomes`, `WorldGenFeatures`,
> `MaskRule`, `FootprintSnap`, `BuildingPlan`, `RoomLayout`, `RoomMinimums`,
> `TreeScatter`+`TreePalette`.
>
> Most likely to be wrong: (1) `RoomLayout`/`RoomMinimums` — I predict SHIPS
> against STATE's SURVEY. (2) `WorldGenBiomes`/`WorldGenFeatures` — if these run
> at generation time the port needs a Lua parser. **This is the single biggest
> cost uncertainty in Track F and I do not know the answer.**

Missed entirely: `GroundRegions` and `GroundMaterial`, because neither appears
in STATE §6's inventory (correction 6). Wrongly included: `WaterTiles`,
`BiomePalette`, `WorldGenBiomes`, `WorldGenFeatures`, `RoomLayout`,
`RoomMinimums`.

---

## 8. Open, for the owner

- **`TreeScatter` / `TreePalette` — port or delete?** The trace says SHIPS: both
  are called by `GisCells`, and `TreeScatter` also by `BiomeMapWriter`. But
  A2a is BLOCKED on A2-gate, and STATE §3 says `genMapSquare` deletes ~7,800 of
  these trees on load. Porting 330 lines of code that may be dead is the wrong
  order. **A2-gate should resolve before step 7**, exactly as E15 should resolve
  before F5. Same shape of problem, same answer: settle it in Java first.
- **The two-repo divergence is unresolved.** Both trees carry `STATE.md` and
  `CHUNKS.md`. This document was produced against the C++ repo's copies plus the
  Java repo's source. `diff` them before folding these corrections in, or the
  corrections land in one copy only and the drift widens.
- **Java HEAD is 2026-08-21.** Confirm nothing GIS-related is uncommitted before
  treating this inventory as final.

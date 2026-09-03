# Session prompt — Port step 7 (part 3): `GisCells`

**How to use this:** open a new session. Paste, in this order:

1. `CHARTER.md`
2. `STATE.md`
3. **this file**

You do **not** need `STATE_ARCHIVED.md`.

---

## READ ALL OF `STATE.md`. NOT PART OF IT.

This is not boilerplate. The convention blocks — grep wrapping ugrep, future
mtimes, the PZ path — have each cost a full session when skipped. Read §36
(path reference and build/run notes) and §13 (Corrections table) before running
anything.

---

## The repos

| repo | role |
|---|---|
| `https://github.com/kaatbailey/PZMapMaker` (branch `Master`) | the C++ port. All new code flat in the repo root. Local: `~/Documents/PZMapMaker` |
| `https://github.com/kaatbailey/PZMapCreation` | the Java tree — the port oracle. Archive-only for new work. Local: `~/Documents/PZMapCreation` |

---

## What has been ported in step 7 so far

Step 7 covers 1,360 lines across four Java files (the 1,206 in the CHUNKS
prompt was written before the A2-gate resolved; §42 explains the difference).

| file | lines | status |
|---|---|---|
| `TreePalette.java` | 149→154 | **DONE** — also sorted the byName iteration (§43) |
| `TreeScatter.java` | 210 | **DONE** |
| `BiomeMapWriter.java` | 132 | **DONE** |
| `GisCells.java` | **864** | **REMAINING** |

`GisCells` is the pipeline integrator. It is the largest single file in the
step and its oracle is byte-identical mod output rather than a cross-language
digest, so it is verified differently from everything else in Track F.

---

## Key facts established before this session

**The mod output is deterministic.** Confirmed across three clean regenerations
of identical inputs; all 12 output files byte-identical each time (§44). Step
7's byte-identical oracle is viable.

**`BASELINE_ohio_2026-09-02.sha`** is committed to `PZMapMaker` root. This is
the reference. Do NOT regenerate against it until after `GisCells` is ported —
verify against it. The generator stdout companion is
`BASELINE_ohio_2026-09-02.log`.

**The generator baseline changed on 2026-09-02** when `TreePalette.pick` was
sorted. The pre-2026-09-02 mod files on disk are superseded. CRITICAL: the
lotpacks were byte-identical before and after the sort, and only the lotheaders
moved — because the lotpack stores INDICES into a name table whose names changed.
**Byte-identical lotpacks do not imply identical maps.** The oracle diffs all
12 files: 4 lotpacks AND 4 lotheaders AND 4 biome PNGs.

**A2-gate resolved (§42):** `TreeScatter` is LIVE. With the write at
`GisCells.java:237-239` commented out, the map has zero trees. Every tree in
a generated map comes from `TreeScatter`.

---

## Environment — read §36 before running anything

Three things that have each cost a session:

1. **`grep` is a fish function wrapping ugrep.** BRE alternation `a\|b`
   silently matches nothing. Use `command grep`, one `-e` per pattern.

2. **Files authored off-machine carry future timestamps.** Ninja prints
   `Configuring done` and then fails `manifest 'build.ninja' still dirty after
   100 tries`, builds nothing, and leaves the OLD binary in place. The fix:
   ```fish
   find ~/Documents/PZMapMaker ~/Documents/PZMapCreation -not -path '*/.git/*' \
        -type f -newermt now -exec touch {} +
   ```
   Then **delete `/tmp/noqt` and reconfigure** — the build dir records the
   stamps.

3. **PZ install path has an extra level:**
   `.../common/ProjectZomboid/projectzomboid/media`

4. **`$GISMAP` and `$PZ` empty between fish sessions.** Set them in the same
   command block.

5. **`javac` being silent is the gate.** A failed compile followed by a
   successful `java` run uses stale `.class` files. Same shape as Ninja leaving
   a stale binary. Delete the output, check the exit code.

---

## What the C++ side already has

Every dependency `GisCells` calls is ported and verified:

| dependency | C++ file | verified by |
|---|---|---|
| `GisImport` | `gisimport.cpp` | `pz_gisraster_oracle` |
| `TileIndex` | `tileindex.cpp` | format-layer tests |
| `TilePalette` | `tilepalette.cpp` | `pz_palettes_oracle` |
| `GroundPalette` | `groundpalette.cpp` | `pz_palettes_oracle` |
| `GroundRegions` | `groundregions.cpp` | `pz_groundregions_oracle` |
| `MaskRule` | `maskrule.cpp` | `pz_maskrule_selftest` |
| `TreePalette` | `treepalette.cpp` | `pz_treescatter_oracle` |
| `TreeScatter` | `treescatter.cpp` | `pz_treescatter_oracle` |
| `BuildingPlan` | `buildingplan.cpp` | `pz_buildingplan_oracle` |
| `BiomeMapWriter` | `biomemapwriter.cpp` | `pz_biomemap_oracle` |
| `CellData` | `celldata.cpp` | format-layer tests + roundtrip |
| `LotHeader` | `lotheader.cpp` | oracle + roundtrip |
| `SpriteNames` | `spritenames.cpp` | present |
| `FootprintSnap` | `footprintsnap.cpp` | `pz_footprint_oracle` |

`GisCells` is the one piece of `pzgen` that is NOT in the library yet.

---

## GisCells in brief

864 lines. The pipeline:

1. `GisImport.rasterise` — done before this unit is called
2. `TileIndex.load`, `SpriteNames.load`, `TilePalette.pick`, `GroundPalette.pick`,
   `TreePalette.pick`, `TreeScatter.place`
3. Building directory printout
4. Cell loop (2×2 = 4 cells):
   - per-cell `Random rng = new Random(SEED * 31 + cx * 7919 + cy)` — seeded
     so a cell regenerates identically whether or not its neighbours are written
   - `GroundRegions.build` and `addMasks` for every ground square
   - wall skins, rooms, buildings, ground, tufts, trees, masks — per-square
   - `CellData` written as `.lotpack` + `.lotheader`
5. `BiomeMapWriter.write` (4 PNGs)
6. `worldgen_override` Lua snippet
7. `spawnpoints.lua`
8. Stdout summary matching `BASELINE_ohio_2026-09-02.log`

**Three non-obvious things the Java comments flag (each found the hard way):**

- Every one of a chunk's 64 squares must carry an object at z=0 or
  `WorldGenChunk` hands it to `genRandomChunk`. Squares outside the raster
  are FILLED (edge-fill), not skipped.
- `spawnpoints.lua` uses the legacy 300-tile cell grid, not B42's 256.
- Ground is a weighted mix with a partial tuft layer (see `GroundPalette`).

---

## The oracle for this chunk

**NOT a cross-language digest. Byte-identical mod output.**

The Java side is already the reference: `BASELINE_ohio_2026-09-02.sha` is its
output, committed. The C++ side must match it.

```fish
set MM ~/Documents/PZMapMaker
set MC ~/Documents/PZMapCreation
set PZ ~/.local/share/Steam/steamapps/common/ProjectZomboid/projectzomboid
set GISMAP ~/Zomboid/mods/PZGisImport/common/media/maps/PZGisImport

cmake -S $MM -B /tmp/noqt -G Ninja -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON
cmake --build /tmp/noqt       # builds everything including the integrating binary

rm -rf ~/Zomboid/mods/PZGisImport
~/Documents/PZMapMaker/pzmapgen ~/pzgis/buildings.geojson ~/pzgis/roads.geojson \
    ~/pzgis/area.geojson "$PZ/media" ~/Zomboid/mods PZGisImport > /tmp/gen_cpp.log

find ~/Zomboid/mods/PZGisImport -type f | sort | xargs sha256sum > /tmp/cpp.sha
diff $MM/BASELINE_ohio_2026-09-02.sha /tmp/cpp.sha; and echo "CPP MATCHES BASELINE"
diff $MM/BASELINE_ohio_2026-09-02.log /tmp/gen_cpp.log; and echo "LOG MATCHES BASELINE"
```

**Predict: both diffs silent.** Any divergence has a file name and a sha256
attached; start from the lotheaders before assuming the lotpacks.

Then Tokyo:

```fish
rm -rf ~/Zomboid/mods/PZGisImport
~/Documents/PZMapMaker/pzmapgen ~/pzgis/tokyo/buildings.geojson \
    ~/pzgis/tokyo/roads.geojson ~/pzgis/tokyo/landuse.geojson \
    "$PZ/media" ~/Zomboid/mods PZGisImport > /tmp/gen_tokyo_cpp.log
```

There is no Tokyo Java baseline; this run IS the baseline. The point is that
the C++ generator must handle a second, differently-shaped raster cleanly —
specifically the taller-than-wide aspect ratio that exposes the `x*h+y` vs
`x*w+y` indexing (§NOTE 6 in `treescatter.hpp`).

---

## Three things that diverge and are expected

From the Track F findings:

1. **`minAreaRect` (Footprint oracle, step 3):** 2,385 divergences in the R
   section, all in the `minAreaRect` field. Accepted in §39. Does not affect
   cell output because `BuildingPlan` uses the AABB rect, not the OBB.

2. **`Math.cos` / `Math.sin` (Raster oracle, step 6a):** 18 COS divergences,
   15,750 PROJ. One ulp difference in `std::cos` vs `Math.cos`. Does not
   affect cell output; `GisImport.rasterise` uses it for coordinate projection
   and the clamp to the raster bounds absorbs the difference.

3. **The `40x20` self-test failure** in `BuildingPlan` (step 4 oracle, exit 1,
   both trees). Intentionally not fixed; fixing it would break the only oracle.
   `BuildingPlan` still generates valid buildings for the generator.

---

## Method — CHARTER §4

- **Every number carries the command that produced it.** No command, no number.
- **The Java tree is the port oracle.** Same input → byte-identical output.
- **Predict before running.** Write the expected result and what would falsify
  it, then run.
- **A stale artefact that looks green is the standing hazard.** Ninja, javac,
  and crashed mutations have all produced one today. Delete outputs first, check
  exit codes.
- **Byte-identical lotpacks do not imply identical maps.** The lotheader carries
  the tile name table. Both must match.

---

## Definition of done for step 7 (complete)

- [x] `TreePalette` ported, sorted, oracle passing on two compilers
- [x] `TreeScatter` ported, oracle passing on two compilers
- [x] `BiomeMapWriter` ported; `pzpng` reproduces `ImageIO` byte-for-byte on
      200 synthetic + real pipeline buffers; oracle passing on two compilers
- [ ] **`GisCells` ported** — this chunk
- [ ] **Ohio oracle: byte-identical mod output** against `BASELINE_ohio_2026-09-02.sha`
- [ ] **Tokyo oracle: clean run** on a differently-shaped raster
- [ ] A `FINDINGS` block in `PZMapMaker`
- [ ] Second compiler (GCC 16) — the biomemap and treescatter oracles ran on
      GCC 16 already; the mod-output oracle has not

## Still open from earlier chunks

- **A2-gate (§25):** RESOLVED. TreeScatter is live (§42).
- **PNG byte-identity (§37):** RESOLVED. pzpng matches ImageIO. Standing
  falsifier is the `PNG` section of `pz_biomemap_oracle`.
- **`YARD = 1` unmeasured (§28, §29):** not blocking.
- **`Cover.WATER` has no branch in `GroundRegions.build`:** not blocking.
- **GCC 16 mod-output oracle:** open after this chunk.

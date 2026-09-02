## FINDINGS — GIS Japan source + port-scope discovery — 2026-08-31

**Status:** partial. The fetch side is complete and verified. The port is scoped
but not started. Tokyo has not yet been generated with water.

**What was done:**

1. **`fetch_gis.py` gained a second source, selected automatically from the
   bounding box.** US box → USA Structures + TIGER/Line, unchanged. Anything
   else → OpenStreetMap via Overpass, which has global coverage. `--source
   us|osm` forces either. The US path was not modified except for one bug fix
   (below).

2. **OSM buildings are mapped onto the US vocabulary.** OSM tags are translated
   to the same `OCC_CLS` / `PRIM_OCC` fields USA Structures produces, so
   `GisImport` does not know or care which country the data came from. Function
   tags beat the physical tag: `building=yes` + `amenity=restaurant` →
   Commercial, not Unclassified. `PRIM_OCC` keeps the OSM key=value that
   produced the class, mirroring how PRIM_OCC splits Commercial in USA
   Structures.

3. **OSM water is written as `water.geojson`, not `rivers.geojson`,** because
   `GisImport` auto-discovers that exact filename beside `buildings.geojson`.
   Each feature carries an NHD-style `fcode` mapped from the OSM waterway type,
   so the existing `waterWidth()` switch picks a channel width without knowing
   the data is not NHD. Mapping: river/riverbank → 46000 (3 tiles),
   stream → 46006 (2), canal → 33600 (2), drain → 33601 (2), ditch → 33603 (2),
   default 46000.

4. **`landuse.geojson` is fetched but nothing reads it.** `Cover` has no
   landcover type. Written so the data is on disk when one exists.

**Confirmed** (verified by running it, or by reading the code):

- **US path unregressed.** Ohio box returns 7 buildings, 1 road, `Co Hwy 26` —
  matches the dataset recorded in STATE §6.
- **Japan path works.** Tokyo box (Moto-Akasaka, 305 × 171 m) returned 59
  buildings, 17 roads, 1 water body, 8 landuse polygons.
- **`GisImport.Cover` is `{NONE, WATER, ROAD, BUILDING}`.** Verified directly:
  `grep -n "enum Cover" src/main/java/pzformat/GisImport.java` → line 38.
  `WaterTiles.java` exists, dated 20 Aug.
- **The GIS pipeline exists only in the Java tree.** A full file listing of the
  C++ repo contains no `Gis*`, no `GeoJson`, no `Json`, no `TilePalette`, no
  `WaterTiles`. The only `Cover` match in C++ is `squaresCovered`, unrelated.

**Unverified** (believed, not tested — say what would test it):

- **Areal water probably rasterises as a perimeter, not a fill.** `GisImport`
  appears to walk each ring and call `waterLine` between consecutive points,
  which traces an outline. Ohio's creek was linear so this never showed. Tokyo's
  water body is `natural=water`, an area. **The test:** generate Tokyo and look
  at the water in the render or in game — a ring of water with dry ground inside
  confirms it. The fetch script marks these `AREAL=yes` and prints the count so
  the case is visible rather than silently wrong. Not yet run.
- **Whether OSM Japan can drive per-class recipes at all.** The Moto-Akasaka box
  returned Unclassified 58, Religion 1. Most Japanese buildings carry a bare
  `building=yes` with no `amenity`/`shop`/`office`, so there is nothing to
  classify from. This is not a mapping bug — the one Religion hit shows the
  mapping fires when tags exist — and that box is Akasaka Palace grounds, where
  minimal tagging is expected. **The test:** fetch a commercial box (Shinjuku,
  Akihabara) and compare the class histogram; businesses get tagged even where
  buildings do not. If a commercial box is still mostly Unclassified, then
  OSM Japan cannot drive E2, E13 room recipes, or per-class materials, and the
  driver has to be something else — footprint area, `building:levels`, or road
  class proximity.
- **Whether the two STATE.md copies have diverged.** `~/Documents/PZMapCreation`
  and `~/Documents/PZMapMaker` each carry one, with different water-mention
  counts (7 vs 8). The test: `diff` them.

**Corrections** (something in STATE.md is wrong):

- **§980 and §2466: "`GisImport.Cover` is `{NONE, ROAD, BUILDING}`" and "There
  is no landcover import" → the Cover half is FALSE and has been since
  2026-08-21.** `Cover` is `{NONE, WATER, ROAD, BUILDING}`. On 2026-08-21 a
  patch added `WATER` to the enum, created `WaterTiles.java`, wired
  auto-discovery of `water.geojson` beside `buildings.geojson`, and shipped:
  Little West Fork Ohio Brush Creek, 1 feature, **1,486 tiles**,
  `blends_natural_02_0`, confirmed in game, committed and pushed. Water wins
  over grass, loses to roads and buildings. Evidence: the enum at
  `GisImport.java:38` and `WaterTiles.java` on disk.
  **The landcover half remains TRUE** — there is still no landcover import.
- **The water work was never recorded anywhere.** A search across both repos for
  markdown mentioning water/river/hydro/fcode/NHD found only the palette table
  and passing references — no orphaned FINDINGS file, no folded section. A
  shipped, in-game-confirmed feature went undocumented for ten days. Reading
  §2466 as current in this session led to designing an enum extension that
  already existed, costing a round trip. Charter §4's "check what exists before
  building" applies to our own tree, not only to vanilla.
- **STATE's "Editor-track inventory (2026-08-21)" lists GIS plumbing as OUT OF
  SCOPE for the port.** That was correct under Charter §2 as written. The owner
  has now decided (2026-08-31) that the GIS generator ships as part of the
  application, reached from a menu item in its own window. This makes the
  out-of-scope list wrong going forward and requires a Charter §2 amendment,
  which only the owner may make (Charter §5).

**Files changed:**
- modified: `~/pzgis/fetch_gis.py` — OSM/Overpass source, bbox-based source
  selection, OSM→OCC_CLS mapping, `water.geojson` with fcode, `landuse.geojson`
- **US-side bug fixed in the same file:** road dedup was keyed on `LINEARID`,
  which drops distinct segments of a road that crosses the box twice. Now keyed
  on geometry, which is what STATE §6 says it should be. The shipped script had
  regressed.

**Commands worth keeping:**
```fish
# Fetch — source is chosen from the bbox, no flag needed
python3 ~/pzgis/fetch_gis.py ~/pzgis/tokyo.geojson ~/pzgis/tokyo
python3 ~/pzgis/fetch_gis.py ~/pzgis/area.geojson  ~/pzgis        # US, unchanged

# Generate Tokyo (Java tree — the GIS pipeline lives ONLY here)
cd ~/Documents/PZMapCreation
set PZ ~/.local/share/Steam/steamapps/common/ProjectZomboid/projectzomboid
java -cp out pzformat.Probe giscells \
    ~/pzgis/tokyo/buildings.geojson \
    ~/pzgis/tokyo/roads.geojson \
    ~/pzgis/tokyo.geojson \
    "$PZ/media" ~/Zomboid/mods PZTokyo
# water.geojson is auto-discovered beside buildings.geojson — not an argument.
# Look for a "water features:" line and a water tile count.

# Render a whole cell (256 wide), not the 64-tile crop the old command used
set GISMAP ~/Zomboid/mods/PZTokyo/common/media/maps/PZTokyo
java -Xmx4g -cp out pzformat.Probe render "$GISMAP" "$PZ/media/texturepacks" \
    200_200 0 0 256 0 0 ~/pzrender/tokyo_full.png

# Which markdown mentions water anywhere in either repo
for d in ~/Documents/PZMapCreation ~/Documents/PZMapMaker
    find $d -name '*.md' -not -path '*/.git/*' | while read -l f
        set -l n (grep -ci -e water -e river -e fcode -e NHD $f)
        test $n -gt 0; and printf "%6d  %s\n" $n $f
    end | sort -rn
end
```

**Noticed, out of scope:**
- **Roads render jaggy.** PZ ships dedicated road tiles with corner and edge
  variants that the importer does not use. Same shape of problem as A3
  wall-joining and E9's neighbour-rule engine — it should reuse that engine
  rather than grow a second one.
- **`FINDINGS_E13_2026-08-19.md` is still in the C++ repo root.** Its content is
  folded (E13 is ticked in CHUNKS, STATE §35/§36 carry it), so per Charter §5 it
  is disposable. Left alone rather than deleted, since deletion is an owner call.
- The `Probe render` invocation recorded in STATE takes a 64-tile crop
  (`200_200 80 157 64 0 0`). Pasted as-is it looks like a nearly empty map. Use
  `0 0 256 0 0` for a whole cell.

**What the next chunk needs to know:**
- **The GIS pipeline is Java-only and must be ported.** See Track F.
- The C++ tree already has everything GisCells writes *through* — `CellData`,
  `LotHeader`, `LotPack`, `TileIndex`, `Square`, `CellEditor`, `MapProject` are
  all ported and verified byte-identical. The port is mostly pure logic and
  palette selection, not format work.
- **The port has an unusually strong oracle:** run Java `giscells` and C++
  `giscells` on the same GeoJSON and compare the generated mod byte-for-byte.
  That is Charter §4's independent-source rule with no interpretation gap.
- `BuildingPlan` is a pure function with a standalone self-test over 14,680
  layouts (`java -cp out pzformat.BuildingPlan`). Port it against that test.
- `GisImport` writes a schematic PNG, which needs image encode. C++ std has
  none. STATE deferred this for `CellRenderer`. **If GIS ships as an app window,
  it is app-layer and Qt's `QImage` covers it** — the deferral resolves itself,
  and the library layer stays dependency-free per Charter §3.
- Water and Japan both work in Java *today*. Generating Tokyo does not wait on
  the port.

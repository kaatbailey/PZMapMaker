# Session prompt — Chunk F1: GIS port inventory and port order

**How to use this:** open a new session. Paste, in this order:

1. `CHARTER.md`
2. `STATE.md`
3. **this file** — and nothing else from `CHUNKS.md`

Per CHUNKS: never paste more than one chunk prompt. A session that can see F1,
F5 and F6 will half-do all three.

---

## The chunk

**F1 — Inventory and port order. Deliverable: a document. No code.**

The GIS pipeline exists only in Java at `~/Documents/PZMapCreation`. The
2026-08-21 C++ port deliberately excluded all of it as out of scope, which was
correct under Charter §2 as written then. The owner has since decided the GIS
generator **ships with the application** — its own window, reached from a menu
item — because it is the path to a playable map for non-technical and disabled
users who will not learn TileZed. So it has to be ported.

Nothing else in Track F can be scoped until this chunk runs. Some of that Java
tree ships, some was scaffolding for measurements now closed, and some is a
read-only survey that writes nothing. Porting all of it would be wrong; porting
the wrong half would be worse.

## What to produce

For every file under `~/Documents/PZMapCreation/src/main/java/pzformat/` that
the generation path touches:

| column | meaning |
|---|---|
| file | name |
| lines | `wc -l` |
| depends on | what it calls |
| depended on by | what calls it |
| verdict | **SHIPS** / **SURVEY** / **DEAD** |
| oracle | for SHIPS only: what will prove the port correct |

**The three verdicts:**

- **SHIPS** — reached at generation time. Port it.
- **SURVEY** — read-only. Taught a rule, writes nothing, `GisCells` never calls
  it. Do not port.
- **DEAD** — superseded. Do not port, and say what replaced it.

Then: **a port order**, naming the oracle for each SHIPS unit.

## Start here

```fish
cd ~/Documents/PZMapCreation
wc -l src/main/java/pzformat/*.java | sort -rn
grep -l -e GisCells -e GisImport -e Probe src/main/java/pzformat/*.java
```

Trace outward from `Probe giscells`, which is the entry point. `GisCells` has
no `main`.

## Two lists that exist, and why not to trust them

STATE's editor-track inventory (2026-08-21) lists as read-only surveys:
`RoomShapes`, `RoomMinimums`, `RoomLayout`, `HouseLayouts`, `FootprintAngles`,
`WallCycle`, plus the `*Probe` / `*Analysis` / `*Survey` harnesses. E13's
findings put `RoomCluster` and `DoorProbe` in the same bucket.

Both lists were written for a different purpose — deciding what the *editor*
needed — and neither was checked against a call trace. **Trust the trace over
both lists.** If the trace shows a "survey" being called at generation time, or
a supposedly-shipped unit that nothing reaches, that is the finding of this
chunk, and it is worth more than the table.

## Things already known — do not rediscover them

- **`Cover` is `{NONE, WATER, ROAD, BUILDING}`.** Water shipped 2026-08-21
  (Ohio Brush Creek, 1,486 tiles, confirmed in game). It went undocumented for
  ten days and cost a round trip when a later session read STATE §2466 as
  current. Do not "add" water.
- **The format layer is already ported and verified byte-identical:**
  `CellData`, `LotHeader`, `LotPack`, `TileDefs`, `TileBin`, `PackFile`,
  `SpriteNames`, `TileIndex`, `Square`, `MapValidator`, `CellEditor`,
  `MapProject`. `GisCells` writes *through* these. The GIS port is pure logic
  and palette selection, **not format work**. Mark these as already-ported in
  the dependency column rather than listing them as work.
- **`BuildingPlan` is a pure function with a standalone self-test over 14,680
  layouts** (`java -cp out pzformat.BuildingPlan`). That test is its oracle.
- **`GisImport` writes a schematic PNG**, which needs image encode that C++ std
  lacks. STATE deferred this for `CellRenderer` to keep the library layer
  dependency-free (Charter §3). The generator is **app layer**, so Qt's
  `QImage` covers it — note this in the inventory, do not treat it as a blocker.
- **`java.util.Random` will not be reproduced by `std::mt19937`.** `GisCells`
  seeds per cell, and the dither flip is driven by a position hash, not the
  sequential `Random` (STATE §28). Any SHIPS unit that draws randomness needs
  "port the LCG explicitly" in its oracle column.

## Definition of done

A verdict for every file, and a port order with a named oracle per SHIPS unit.

Not done if: a file is listed without a verdict, a SHIPS unit has no oracle, or
the verdicts came from the two lists above rather than from a call trace.

## Method

Charter §4 applies. Predict before you run: before tracing, write down which
files you expect to be SHIPS and how many there are. If the trace disagrees,
that gap is the interesting part of this chunk.

This is an inventory, not a port. Resist writing C++ this session — the whole
point of F1 is that we do not yet know what should be written.

## End of session

Produce a `FINDINGS` block in the format at the bottom of `CHUNKS.md`. The
**Corrections** section matters here: if either of the two existing lists is
wrong about a file, that is a correction against STATE and should be recorded as
one.

---

## Sequencing note for the owner — decide before F5, not after

**E15 (areal water fill) should land in Java before F5 ports `GisImport`.**
Otherwise the raster gets ported, then the water fix lands, then the fix gets
ported — the same work twice. The same applies to any other E-track chunk that
touches `GisImport` or `GisCells`.

**E15's premise is still unverified.** `GisImport` appears to rasterise water by
walking each ring and calling `waterLine` between consecutive points, which
traces a perimeter instead of filling. Ohio's creek was linear so this never
showed; Tokyo's water body is `natural=water`, an area. One command settles it:

```fish
cd ~/Documents/PZMapCreation
set PZ ~/.local/share/Steam/steamapps/common/ProjectZomboid/projectzomboid
java -cp out pzformat.Probe giscells \
    ~/pzgis/tokyo/buildings.geojson \
    ~/pzgis/tokyo/roads.geojson \
    ~/pzgis/tokyo.geojson \
    "$PZ/media" ~/Zomboid/mods PZTokyo

set GISMAP ~/Zomboid/mods/PZTokyo/common/media/maps/PZTokyo
java -Xmx4g -cp out pzformat.Probe render "$GISMAP" "$PZ/media/texturepacks" \
    200_200 0 0 256 0 0 ~/pzrender/tokyo_full.png
```

`water.geojson` is auto-discovered beside `buildings.geojson` — not an argument.
Look for a `water features:` line and a water tile count. A ring of water with
dry ground inside confirms the defect.

This is independent of F1 and can run in either order.

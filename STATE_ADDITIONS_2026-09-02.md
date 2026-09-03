# STATE additions — 2026-09-02

Append these to `STATE.md`. Nothing here deletes or edits an existing line; the
two corrections go in the §13 Corrections table as new rows, per CHARTER §5.

---

## §42. The A2-gate is RESOLVED — the engine does NOT re-scatter trees

**Status: CONFIRMED. A2 step 1 is REFUTED. `TreeScatter` and `TreePalette` are
live and must be ported. Step 7 remains 1,206 lines; there is no 330-line
saving.**

§25 left this open for eleven days because it cannot be settled by reading code
— it needs a positional test in game. It was run on 2026-09-02 against
`PZGisImport` (Ohio), cell 200_200.

### The test

A controlled comparison, one variable. The tree write at
`GisCells.java:237-239` was commented out:

```java
                            if (treeAt[gx][gy] != null) {
                                stack.add(cell.tileIndex(treeAt[gx][gy]));
                            }
```

`TreeScatter.place` at `GisCells.java:67` was left ALONE, so the run still
reports `trees: 7744 placed`. That line is the scatter, not the write, and
mistaking one for the other cost a cycle — a run that prints a tree count is
not evidence that trees reached the map.

Same seed, same GeoJSON, same tile, fresh world both times, world 51381,51380
(cell 200_200 local 181,180):

| tree write | in game at 51381,51380 |
|---|---|
| enabled | dense forest, canopy over most of the screen |
| commented out | **ZERO trees.** Grass, tufts and bushes only |

Not thinner. Not rearranged. None. The engine adds no trees of its own, does
not re-scatter ours, and does not substitute anything.

**Every tree in a generated map is ours.**

### Consequences

- **A2 step 1 (delete `TreeScatter`) must NOT proceed.** Both units get ported
  in step 7 as originally scoped.
- `distanceToStructure` no longer needs extracting as a precondition for
  deletion. It is still worth moving to a geometry helper on its own merits
  (`BiomeMapWriter` depends on it), but that is now optional cleanup rather
  than a blocking step.
- **`TreePalette:51`'s raw `byName` consumption goes LIVE.** §41 proved that
  hazard dormant on the grounds that nothing consumed raw order. This is what
  changed. See §43.

### The instrument, and why the old one could not run this test

`Probe findprop` is hard-capped at 3 hits per cell by `PropsProbe.find:209-211`
and scans from the origin outward. On a map whose x=0 column carries trees it
can only ever return x=0 — which is exactly the region the test must avoid.
§25 rules out the map edge (world 51200 is the map boundary), and §26's
`Blending.removeTrees` deletes trees within 10 squares of any edge shared with
a procedural chunk (`rnd.nextInt(100) >= y*10`). **A tool that can only report
the unusable region cannot run this test**, and its output looked perfectly
healthy while being useless. This is §27's lesson again: establish what a tool
CAN return before believing what it did return.

`TreeGrid.java` (120 lines, `PZMapCreation` only, not a port artefact) dumps
every authored tree in a chosen rectangle as an ASCII map with world
coordinates, plus the explicit tile list and the density. It exists so the walk
is a comparison against a printout rather than an impression.

```fish
cd ~/Documents/PZMapCreation
set PZ ~/.local/share/Steam/steamapps/common/ProjectZomboid/projectzomboid
set GISMAP ~/Zomboid/mods/PZGisImport/common/media/maps/PZGisImport
java -cp out pzformat.TreeGrid "$PZ/media" "$GISMAP" 200_200 180 180 40
```

Window 180..219 on both axes: clear of the block's outer perimeter, and the
internal boundaries of a 2x2 cell block are authored-to-authored so `Blending`
never fires on them. **With the write enabled: 48 trees in 1600 squares
(3.00%), 0 empty squares.** With it commented: 0 trees. Running this BEFORE
loading the game is the gate on whether the walk means anything, and it caught
a failed edit that would otherwise have produced a confident wrong answer.

### What the screenshots could NOT settle, and why it matters

The first in-game observation was read as evidence the engine ADDS trees,
because the canopy looked far denser than the 3% `TreeScatter` reports. That
reading was wrong twice over:

- 3% is a whole-map average. The bands are wildly uneven — `dense 5590` of
  7,744 in one band against `roadside 121` in another. 51381,51380 is far from
  any building and sits in the dense band, so the local rate is much higher.
- The two comparison screenshots differed in season and weather. "A tree that
  was not there before" between two shots taken at different times is exactly
  the eye-based comparison the printout exists to replace.

It was flagged as a hint rather than evidence at the time and the test was run
anyway, which is the only reason it cost nothing. **A density impression is not
a measurement**, and the deletion test — same world, one variable — is what
the charter specified for this gate in the first place.

---

## §43. `byName` iteration order — DECIDED: sort in both trees

**Owner decision, 2026-09-02: do not reproduce `java.util.HashMap` ordering in
C++. Sort instead, in both trees, and accept that generated mod output changes
once.**

`TileIndex.byName` is a `HashMap` (`TileIndex.java:20`). `TreePalette.pick`
iterated `ti.byName.keySet()` raw, which fixed the order of `bySize`'s lists,
which decides which tile `TreeScatter` selects via
`variants.get(rng.nextInt(variants.size()))` (`TreeScatter.java:115`) on each of
~7,700 squares.

HashMap order is a function of `String.hashCode`, table capacity and insertion
history. It is stable for a given JDK and input set — which is why the
generator measures deterministic (§44) — but it is **not a specification**.

### Why not reproduce it

Cloning Java's bucket layout, spread function, resize behaviour and
treeification threshold in C++ is achievable and testable. It was rejected
anyway: it buys byte-identity with an **arbitrary** ordering that nothing
depends on, and a JDK upgrade could change it under both trees at once, turning
a passing oracle into a failing one with no source change to blame. That is the
same objection §41 raised to `-fwrapv` — do not stabilise a hazard behind
something a later change can silently remove.

### The change

`TreePalette.pick` now sorts:

```java
List<String> names = new ArrayList<>(ti.byName.keySet());
Collections.sort(names);
for (String n : names) {
```

`TreePalette.java` is 149 lines after the edit.

**This matches existing practice rather than inventing a convention.**
`TilePalette.first` already collects into a list and calls `Collections.sort`
before taking `hits.get(0)` (`TilePalette.java:222-235`), and `TilePalette:238`
uses a `TreeSet`. The codebase had already solved this problem once.

Tile names are ASCII, so Java's `String.compareTo` (UTF-16 code units) and
C++'s `std::string operator<` (unsigned byte compare) agree. **The step 7
oracle must assert that rather than assume it** — the synthetic header oracle
deliberately carries a tile name with bytes `80 FF`, so non-ASCII names are not
hypothetical in this format.

### The cost, recorded rather than discovered

**Generated mod output changes.** The pre-2026-09-02 `PZGisImport` baseline is
superseded. Step 7's oracle — byte-identical mod output, Ohio and Tokyo —
compares against a REGENERATED baseline, not the one on disk before this date.
That is a real weakening of "we reproduce what the Java tree has always
produced" and it is deliberate.

The new baseline is `BASELINE_ohio_2026-09-02.sha` in the `PZMapMaker` root,
with the generator's stdout beside it. Confirmed deterministic across three
clean regenerations.

### MEASURED: what actually changed, and the surprise in it

Predicted: the four lotpacks differ, the four lotheaders do not.
**Measured: exactly the reverse. The lotpacks are BYTE-IDENTICAL and only the
lotheaders moved.**

The lotpack stores INDICES into the header's `tileNames` table, not names.
`TreeScatter` picks with `variants.get(rng.nextInt(variants.size()))` — same
RNG, same list size — so the sequence of SLOT NUMBERS drawn is identical either
way. Sorting permutes which name occupies each slot, consistently everywhere,
so the order in which distinct names are first interned is unchanged and every
square keeps the index it already had. The names at those indices changed; the
indices did not.

**THIS IS THE FINDING THAT OUTLIVES THE CHUNK. Byte-identical lotpacks do not
imply identical maps.** A permuted palette yields the same lotpack bytes and a
different world. CHARTER §4 states this in the abstract — *"byte-identical
round-tripping proves a format was read and written faithfully. It says nothing
about whether it was interpreted correctly"* — and this is a concrete instance
of it in the generator's own output rather than in a round-trip.

**Step 7's oracle must diff the lotheaders, not only the lotpacks.** It hashes
all twelve files today, so it is correct as written. The hazard is a future
session treating the header as "just a name table" and dropping it from the
comparison to reduce noise. That would make this entire class of change
invisible.

### The old HashMap order, recovered

The permutation is exact across all 48 sampled squares, with no exceptions:

```
_8 → _11    _9 → _10    _10 → _8    _11 → _17
_13 → _13   _14 → _15   _15 → _14   _17 → _9
```

All eight tiles of the sheet accounted for (`tree palette: size 2: 8 tiles`).
Cycles `(8 11 17 9 10)` and `(14 15)`, with `_13` a fixed point — which is why
four lines sit outside the diff hunks.

Inverting it recovers what `HashMap` iteration order actually was:

**`_9, _8, _13, _15, _14, _11, _10, _17`**

Recorded because it is a falsifiable claim about the old behaviour, and because
seeing it written out is the argument for §43's decision: there is nothing
meaningful about that sequence. It is an artefact of `String.hashCode` and
table capacity, and reproducing it in C++ would have bought byte-identity with
an ordering that means nothing and that a JDK upgrade could permute again.

### Verified, not assumed: this is the only live consumer

§41 claimed `TreePalette:51` was the only raw `byName` consumer. Re-derived
2026-09-02 rather than trusted:

```fish
command grep -rn -e 'byName' --include='*.java' src/main/java/pzformat/
```

Every other iteration is either order-independent or already sorted:

| site | why it is safe |
|---|---|
| `TilePalette.java:223` | collects to a list, `Collections.sort(hits)`, takes `get(0)` |
| `TilePalette.java:238` | `new TreeSet<>(...)` |
| `TileIndex.java:35` | `putIfAbsent` across packs; names are unique within a pack, so only the OUTER pack order matters |
| `TileIndex.java:200` | `reportVocabulary` accumulates into a `TreeMap` of counts |
| `PalettesOracle.java:668` | sorts explicitly |
| `PaletteScan`, `TreeSurvey`, `MaskAudit`, `DitherLaw`, `WallCycle`, `ClassifyOracle`, `PropsProbe`, `MakeTestMod`, `WaterTiles` | analysis and probe tools; none feeds generated map output |

---

## §44. The generator is DETERMINISTIC — step 7's oracle is viable

Two clean regenerations from identical inputs, 2026-09-02:

```fish
rm -rf ~/Zomboid/mods/PZGisImport
java -cp out pzformat.Probe giscells ~/pzgis/buildings.geojson ~/pzgis/roads.geojson \
     ~/pzgis/area.geojson "$PZ/media" ~/Zomboid/mods PZGisImport > /tmp/gen1.log
find ~/Zomboid/mods/PZGisImport -type f | sort | xargs sha256sum > /tmp/run1.sha
# repeat into run2.sha
diff /tmp/run1.sha /tmp/run2.sha; and echo "DETERMINISTIC"
```

**Result: `DETERMINISTIC`.** All 12 files identical, stdout identical.

This was run because an earlier `before.sha` / `after.sha` comparison showed all
four lotpacks AND all four lotheaders changing on what looked like an unchanged
source tree, which would have meant step 7's byte-identical mod-output oracle
could not exist as specified. It was instead measuring old code against new —
the working tree had changed since the mod on disk was generated. **Confirmed
deterministic; the step 7 oracle stands.**

Note the lotheaders changing when trees are removed is *confirmation*, not a
surprise: with no tree tiles written, their names never enter `tileNames`, so
the header's table shrinks.

---

## §13 Corrections — two new rows

| wrong belief | correction | how it was found |
|---|---|---|
| **`TreeScatter` may be dead weight; the engine re-scatters vegetation and A2 step 1 can delete 330 lines** (§20, §25, open since 2026-08-22) | **REFUTED.** The engine adds no trees. With the write at `GisCells.java:237-239` commented, world 51381,51380 has zero trees; with it enabled, dense forest. Both units must be ported. | Positional test in game, 2026-09-02. §42. |
| **`GisCells` places 7,797 trees** (§25, 2026-08-22) | **7,744** as of 2026-09-02 on identical inputs. A code change between sessions, not nondeterminism — two clean regenerations are byte-identical (§44). The 2026-08-22 figure was correct for the code of that date and is superseded rather than wrong. | Noticed while re-running the generator for §42. |

---

## Amend §37's OPEN list

- **PNG byte-identity — RESOLVED 2026-09-02, see `FINDINGS_F5_2026-09-01.md`.**
  Qt `QImage` does NOT reproduce Java `ImageIO`: four independent differences
  (a `pHYs` chunk Qt writes and Java does not, per-row filter selection, zlib
  level `785e` vs `789c`, IDAT chunking 32,768 vs 8,192). Decoded pixels are
  identical in every case.

  **But byte-identity is fully recoverable if C++ writes the PNG itself.**
  Java's `PNGImageWriter` for `TYPE_INT_RGB` is exactly reproducible: filter
  `None` on every row, **zlib level 4**, default strategy, 32,768-byte IDATs,
  no ancillary chunks. A ~60-line encoder plus zlib matched Java on **200 of
  200** varied 256x256 buffers — uniform, blocked, gradient, pure noise, and the
  four-band shape `BiomeMapWriter` actually produces.

  **Step 7's oracle does not need weakening, on one condition: the C++ side
  must not route through `QImage`.** That also keeps Qt off the oracle path
  entirely, and makes the result independent of the installed Qt version.

  Two caveats, both measured rather than assumed. zlib becomes a dependency —
  permitted (app layer, permissive licence) but `pzgen` is dependency-free
  today, so the PNG writer belongs in its own target. And the agreement is
  between two implementations rather than a specification: zlib output is not
  formally guaranteed stable across versions. Measured against zlib 1.3 and
  OpenJDK 21.0.12. **Falsifier, cheap and standing: re-run the 200-buffer sweep
  whenever either toolchain moves.** Level 4 is likewise measured, not
  documented — the zlib header only narrows it to FLEVEL 1, meaning levels 2-5.

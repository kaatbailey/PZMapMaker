# FINDINGS — F7, step 7 (part 2): `BiomeMapWriter` + `pzpng` — 2026-09-02

**Stamped at PZMapCreation post-§43 sort, javac 21.0.12.**
**Reproduced on g++ 16.2.1, zlib 1.3.2 — same line count, byte count, md5,
first try.**

---

## Result

```fish
java -cp out pzformat.BiomeMapOracle /tmp/bm.java.txt
/tmp/noqt/pz_biomemap_oracle           /tmp/bm.cpp.txt
cmp /tmp/bm.java.txt /tmp/bm.cpp.txt
```

**647 lines, 29,822 bytes, byte-identical, md5
`64c0b0f0ff7ea8b9fcb8ec9043e35337`.**

Identical at `-O0`, `-O2`, `-Ofast`. Zero ASan/UBSan reports, sanitizer build
still matches Java.

---

## The PNG question — closed on real data

`biomemap_X_Y.png` is shipped mod content. Qt's QImage does not reproduce
Java's ImageIO and cannot be configured to: pHYs chunk, per-row filter choice,
zlib level `785e` vs `789c`, IDAT chunking 32,768 vs 8,192 — four independent
differences on identical pixel buffers.

`pzpng` reproduces ImageIO exactly. The match holds on 200 synthetic buffers
AND on the buffers `BiomeMapWriter` actually produces (the `BMP` section).
That's the check the 200-buffer sweep couldn't make and §37's whole concern.

**Two caveats recorded, not hidden.**

1. **Level 4 is measured, not documented.** The zlib header narrows it to
   levels 2-5. Found by trying all nine levels. The falsifier: the `PNG`
   section diverges on any other level (mutations P1 and P2 caught 364 lines
   each; levels on both sides of 4 diverge, so it is not a lucky neighbour).

2. **The agreement is between two implementations.** Java's Deflater IS zlib.
   Measured against zlib 1.3 (container) and zlib 1.3.2 (owner's machine) with
   OpenJDK 21.0.12. Both agree — two zlib versions, same result, which is weak
   evidence the match is not version-sensitive at this step. The standing
   falsifier is the `PNG` section: re-run whenever either toolchain moves.

---

## Coverage — 100% of lines, 100% of branches, zero unreachable outcomes

Unusual for this project. `GroundRegions` had four dead guards; `TreePalette`
five. These two units have no defensive code that cannot fire. The `gx >=
g.width` beyond-raster branch, the `FARM_FOREST` and `PH_FOREST` bands, the
empty-raster path in `distanceToStructure` — all reached.

---

## Mutations — 12 run, 11 caught, 1 crashed, 0 not caught

Every one of the four ways QImage differs from ImageIO is caught as a named
mutation. That is the argument for `pzpng` made concrete: if any of those four
drifted, the oracle sees it.

| mutation | divergent lines |
|---|---|
| zlib level 4 → 6 | 364 |
| zlib level 4 → 3 | 364 (both neighbours, not just one) |
| filter None → Sub | 364 |
| colour type 2 → 6 | 364 |
| extra pHYs chunk added | 364 |
| blue byte left zero | 274 |
| TOWN_RADIUS <= → < | 166 |
| IDAT chunk 32768 → 8192 | 65 |
| ox = cx*255 | 74 |
| pixel index transposed | 113 |
| cell name cx/cy swapped | 24 |

The crash (`gx >= g.width` → `>`, indexes one past the raster) was caught by
the process rather than the digest. Same class as `GroundRegions`' `P[4]` read;
the runner deletes the output before each mutation and checks the exit code.

---

## Layering decision — committed to the amendment log

`pzpng` is its own CMake target linking zlib. `pzgen` remains dependency-free.
`find_package(ZLIB REQUIRED)` lands BEFORE `add_library(pzgen …)` in
`CMakeLists.txt`; `pzgen` links `pzpng PUBLIC`, so anything linking `pzgen`
transitively gets zlib. `Qt6 NOT found` still prints and every oracle target
still builds — confirmed on both g++ 13.3.0 and 16.2.1.

---

## Files

| file | `wc -l` | |
|---|---|---|
| `pzpng.hpp` | 72 | new — `pzpng` target only |
| `pzpng.cpp` | 88 | new — links zlib |
| `biomemapwriter.hpp` | 102 | new — `pzgen` |
| `biomemapwriter.cpp` | 102 | new — `pzgen` |
| `biomemap_oracle.cpp` | 249 | new |
| `BiomeMapOracle.java` | 323 | new — `PZMapMaker` root AND `PZMapCreation/src/main/java/pzformat/` |
| `CMakeLists.txt` | 143 | `pzpng` target + 2 lines |

---

## Open going into step 7 part 3 — `GisCells`

- **`GisCells.java`: 864 lines.** The pipeline integrator. Calls every ported
  unit and drives the cell writer. Its oracle is byte-identical mod output
  against `BASELINE_ohio_2026-09-02.sha`, then Tokyo.
- **The step 7 oracle diffs lotheaders AND lotpacks.** §43's measurement:
  sorting the palette left all four lotpacks byte-identical and moved only the
  headers. Byte-identical lotpacks do not imply identical maps. The oracle
  already hashes all 12 files; this is a warning not to drop headers as
  "just a name table" when scoping future checks.
- **`BASELINE_ohio_2026-09-02.sha` is committed to `PZMapMaker`.** Confirmed
  unmoved after the `bySizeKeys()` accessor landed. This is the reference.

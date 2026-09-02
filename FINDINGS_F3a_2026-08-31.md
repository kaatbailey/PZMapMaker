# FINDINGS — F3 part 1: `FootprintSnap` — 2026-08-31

**Status:** complete, with one characterised divergence and a verified fix
awaiting an owner decision.

**What was done:**
- Ported `FootprintSnap` (299 lines) to C++20, standard library only. Port order
  step 3. Pure geometry, no RNG, no dependencies.
- Wrote `FootprintOracle` (Java and C++) — Pattern B digest over three corpora:
  the 7 fixed cases from the Java `main`, 20,000 randomised polygons, and 379
  real building/landuse/water rings from the Ohio and Tokyo GeoJSON projected to
  metres (1 tile = 1 m, §32).

**Confirmed:**

| corpus | Rect divergences |
|---|---|
| 7 fixed self-test cases | **0** |
| 379 real projected footprints | **0** |
| 20,000 randomised polygons | 122 (0.61%) |

- **Area arithmetic is exact.** With bit-identical inputs, `area()` bits agree on
  all 20,000 — shoelace, `dedupeExact` and the centroid arithmetic carry no
  divergence.
- **The convex hull is exact.** Hull vertex counts agree on all 20,000. Using
  `std::stable_sort` was necessary and sufficient: Java's `Arrays.sort` on an
  object array is a stable mergesort, `std::sort` is not, and duplicate
  vertices survive `dedupeExact` (it drops only the closing vertex).
- **`Math.round` is `floor(x + 0.5)`, not `std::round`.** They disagree on every
  negative half-integer (Java gives -2 for -2.5, `std::round` gives -3).
  Footprint centroids go negative near a cell origin. Ported as `javaRound`.
  This was predicted as the most likely failure and the port would have failed
  on the fully-negative fixed case without it.
- **`centroid(p, area)` ignores its `area` parameter** — the denominator is
  recomputed from `shoelace(p)`, which is *signed* where the caller's `area` is
  absolute. Kept in the signature so the two stay line-comparable.
- **The degenerate branch in `centroid` accumulates onto already-nonzero
  `cx`/`cy`** rather than resetting them. Reproduced deliberately; resetting
  would be a silent behaviour change.

**KNOWN DIVERGENCE — `minAreaRect` edge selection under near-ties**

`minAreaRect` computes `ang = atan2(dy,dx)` then `cos(-ang)` / `sin(-ang)`.
Java's `Math.*` permits 2 ulp of error and does not match libm. With
bit-identical inputs, the resulting width/height differ on 2,385 of 20,000
polygons — usually harmlessly, but when two candidate hull edges give
near-equal enclosing area the `ar < bestArea` comparison flips and a completely
different rectangle wins.

- **Rate: 0.61% overall.** By hull size: 2.70% at 3, 0.81% at 4, 0.17% at 5,
  0% at 6+.
- **Failure shape:** the rectangle rotates 90°. e.g. Java `[232,-5 1x10]` vs
  C++ `[227,0 10x1]` — same area, transposed.
- **Zero occurrences on real data.** 379 real rings, no divergence. Only 1.32%
  of real footprints have a triangular hull, and none flipped.

**THE FIX, VERIFIED:** `cos(-ang)` and `sin(-ang)` are algebraically just
`dx/len` and `-dy/len`. That formulation uses only division and `sqrt`, both
correctly rounded under IEEE 754 in both languages. Tested with bit-identical
inputs over 20,000 polygons: **bit-identical width/height, all 20,000.**

```java
// instead of: ang = atan2(dy,dx); c = cos(-ang); s = sin(-ang);
double dx = b[0]-a[0], dy = b[1]-a[1], len = Math.sqrt(dx*dx + dy*dy);
if (len == 0) continue;
double c = dx/len, s = -dy/len;
```

**OWNER DECISION REQUIRED.** Applying this to C++ alone makes the two trees
*more* different, not less — Java would keep using `atan2`. To remove the
divergence, it must land in **both** trees. That is a change to the port oracle
itself, which is why it is not being made unilaterally. Three options:

1. **Apply to both trees.** Removes the divergence permanently, makes
   `minAreaRect` platform-independent, and costs four lines. It changes Java's
   output on the 0.61%, so E5's recorded building results would shift slightly.
2. **Accept and document.** Zero real-data impact today. Risk: a future dataset
   with triangular buildings flips a rectangle and breaks F6's byte-identical
   diff with no visible cause.
3. **Apply to C++ only and record a deliberate divergence.** Not recommended —
   it guarantees F6 diffs rather than merely risking them.

**Unverified:**
- Whether `atan2` divergence differs across JDK vendors or CPU architectures.
  Only OpenJDK 21 on x86-64 was tested, on two machines. If it varies, option 1
  becomes the only stable choice.

**Corrections (to my own earlier analysis in this session):**
- I first reported 81 Rect divergences and attributed all of them to
  `minAreaRect`. **That measurement was contaminated:** the corpus generator
  itself used `Math.cos`/`std::cos`, so the input polygons differed between
  trees before `FootprintSnap` ran. It also produced 33 spurious "area bit"
  divergences, which is impossible for pure arithmetic on identical points —
  that impossibility is what exposed the harness bug. Regenerated the corpus
  with no transcendentals; the real figure is 122, and area/hull divergence is
  zero. **A cross-language oracle must generate its corpus without
  transcendentals, or it measures the harness instead of the port.**
- I also first reported the divergence as triangle-only. It is not: it decays
  with hull size (2.70% / 0.81% / 0.17% / 0%).

**Files changed:**
- created `F3/src/footprintsnap.hpp`, `footprintsnap.cpp`, `footprint_oracle.cpp`
- created `F3/oracle_java/FootprintOracle.java`

**Build wiring:** add `footprintsnap.cpp` to the `pzgen` library and
`footprint_oracle` to the second `foreach` loop in `CMakeLists.txt`. The oracle
links `pzgen` (it uses `GeoJson` for the real-data corpus). `FootprintOracle.java`
goes in the C++ repo root and is copied into
`src/main/java/pzformat/` to compile.

```fish
java -cp out pzformat.FootprintOracle /tmp/fp.java.txt \
     ~/pzgis/buildings.geojson ~/pzgis/tokyo/buildings.geojson \
     ~/pzgis/tokyo/landuse.geojson ~/pzgis/tokyo/water.geojson
~/Documents/PZMapMaker/build/pz_footprint_oracle /tmp/fp.cpp.txt \
     ~/pzgis/buildings.geojson ~/pzgis/tokyo/buildings.geojson \
     ~/pzgis/tokyo/landuse.geojson ~/pzgis/tokyo/water.geojson
diff /tmp/fp.java.txt /tmp/fp.cpp.txt
```

Predict: 20,386 lines each. The `X` and `G` records identical; the `R` records
differing on ~122 Rect values and ~2,385 `minAreaRect` bit pairs, unless option
1 above has been applied, in which case expect zero.

**What the next chunk needs to know:**
- **Step 4 is `BuildingPlan`** — 3,347 lines, 49% of the port, zero
  dependencies, own `main` at line 2614 over 14,680 layouts. It draws from
  `JavaRandom` (verified) and needs no PZ install. Still the right chunk for a
  long uninterrupted session.
- **`BuildingPlan` will hit the same class of problem.** Any `Math.round` on a
  negative value needs `javaRound`; any `atan2`/`cos`/`sin` in a comparison
  needs the transcendental-free treatment. Check for both before writing.
- The corpus-contamination lesson applies to every remaining oracle: generate
  inputs with arithmetic and `JavaRandom` only.

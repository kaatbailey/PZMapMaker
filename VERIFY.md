# Verifying the C++ port

Self-tests prove internal consistency, which per Charter §4 proves very little
on its own: read and write can share a wrong assumption and agree perfectly.
Everything here is a comparison against an independent source — the Java tree,
and retail map data.

**Two corrections landed here on 2026-09-02**, both recorded in STATE §13 as
defects in this document. §1 pointed at a `tools/` directory that does not
exist, and §2 told the reader to predict `SPAN_LEVELS_MINIMAL`, which STATE's
own full-dataset round-trip had already refuted. A session following the old §2
would have predicted wrong, seen the "wrong" answer, and hunted a port bug that
does not exist — in the one document whose job is telling you what to expect.
Both are fixed below.

## Build

```fish
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

`ctest` covers the format-layer unit tests only. **None of the oracles in §1 or
§4 are in `ctest`** — they are separate binaries and must be run by hand.

## Environment, before running anything

These have each cost a session. STATE §36's "Path reference" and "Build/run
notes" blocks are the full list; these three are the ones that bite here.

- **`grep` is a fish function wrapping ugrep.** BRE alternation `a\|b` silently
  matches nothing, so an empty scan is indistinguishable from a clean one. Use
  `command grep`, one `-e` per pattern, and `-F` for anything containing a
  paren.
- **Files authored off-machine carry future timestamps.** Ninja then prints
  `Configuring done` and fails with `manifest 'build.ninja' still dirty after
  100 tries`, builds nothing, and leaves the OLD binary in place — a
  green-looking build producing a stale result.

  **An extension glob is the wrong instrument**, and `touch *.cpp *.hpp
  CMakeLists.txt` failed on the 2026-09-02 drop for two reasons: it does not
  match `.java`, and it cannot reach the Java tree's copy of an oracle at all.
  Run this instead, **after** every file is in place and across **both** repos:

  ```fish
  find ~/Documents/PZMapMaker ~/Documents/PZMapCreation -not -path '*/.git/*' \
       -type f -newermt now -exec touch {} +

  # Must print nothing:
  find ~/Documents/PZMapMaker ~/Documents/PZMapCreation -not -path '*/.git/*' \
       -type f -newermt now
  ```

  Then **delete the build directory rather than reconfiguring into it.** Its
  `build.ninja` has already recorded the future stamps and will keep looping
  after the sources are fixed.

- **A Java oracle lives in two repos and only one of them is the one you edited.**
  Every `*Oracle.java` sits in the `PZMapMaker` root and must be copied into
  `PZMapCreation/src/main/java/pzformat/` to compile. Skipping that gives
  `javac: file not found` and then `ClassNotFoundException`, which looks like a
  build problem and is not. `wc -l` both copies before believing a green run.
- **The PZ install path has an extra level:**
  `.../common/ProjectZomboid/projectzomboid/media`.

## 1. Cross-language oracle (no retail data needed)

`Oracle.java` lives in the **C++ repo root** — there is no `tools/` directory —
and is copied into `src/main/java/pzformat/` in the Java tree to compile. It
shares no code with the C++ side; agreement is evidence.

```fish
# Java tree
javac -d out src/main/java/pzformat/LE.java src/main/java/pzformat/LEW.java \
             src/main/java/pzformat/LotHeader.java src/main/java/pzformat/LotPack.java \
             src/main/java/pzformat/RoundTrip.java src/main/java/pzformat/Oracle.java

# Java writes a B42 lotheader; C++ reads it, checks the decoded values, rewrites it
java -cp out pzformat.Oracle emit /tmp/h_java.bin
./build/pz_oracle check /tmp/h_java.bin /tmp/h_cpp.bin
cmp /tmp/h_java.bin /tmp/h_cpp.bin

# and back the other way
java -cp out pzformat.Oracle check /tmp/h_cpp.bin

# C++ writes a lotpack; the Java decoder and encoder reproduce it
./build/pz_oracle emitpack /tmp/pack_cpp.lotpack
java -cp out pzformat.Oracle pack /tmp/pack_cpp.lotpack
```

The synthetic header is deliberately awkward: a tile name with bytes `80 FF`,
an empty tile name, an empty room name, a negative `floor`, a negative
`minLevel`, a negative object field, and a zero-room building. Those are the
cases where a Latin-1/UTF-8 slip or a sign error would show up.

## 2. Harness output diff (needs retail data)

`pz_roundtrip` mirrors `RoundTrip.java` including its output text, so the two
can be run over the same directory and diffed line for line.

```fish
set PZ ~/.local/share/Steam/steamapps/common/ProjectZomboid/projectzomboid
set MAP "$PZ/media/maps/Muldraugh, KY"

java -cp out pzformat.Oracle sweep $MAP > /tmp/rt_java.txt
./build/pz_roundtrip $MAP              > /tmp/rt_cpp.txt
diff /tmp/rt_java.txt /tmp/rt_cpp.txt
```

**Predict before running** (Charter §4): both should report the same cell count,
100% byte-identical lotheaders, and **`SPAN_LEVELS_FULL`** reproducing every
chunk — 4,162,560 of 4,162,560, 100%.

**This was wrong here until 2026-09-02 and said `SPAN_LEVELS_MINIMAL`.** The
encoder policy was assumed to be MINIMAL and the full-dataset round-trip proved
it is FULL; MINIMAL scores **76%**, matching only cells whose last data square
lands on a level boundary, and the synthetic test cell passed it by
coincidence. See STATE §13 and the "Ported and CONFIRMED (2026-08-21)" block.

Any difference at all is a port bug with a cell name attached. A `diff` that is
empty is the result that matters; the numbers themselves are already known from
the Java side.

## 3. The check that hasn't been run yet

The minimal-levels encode policy computes its level count from the last square
that holds data, then writes every square up to that level's boundary. So a
chunk body that stopped immediately after its last square — with no trailing
run — could not round-trip; it would gain one. Since round-tripping succeeds
across the dataset, retail must always pad to the end of the last encoded level.

That is inferred from the round-trip result rather than measured. Direct
falsifier: for every chunk, `squaresCovered` should be an exact multiple of 64.
If any chunk is not, the encode policy is not what we think it is and it scored
100% for a reason we do not understand.

## 4. Track F — the GIS port oracles

Every one of these is a Pattern B canonical text digest (STATE §38): the two
trees emit a tab-separated line per entity in a fixed field order, and the pass
condition is `cmp`. **None is in `ctest`.** Each Java side lives in the C++ repo
root and is copied into `src/main/java/pzformat/` to compile.

Run them from **absolute paths**. A block that spans both repos and uses `.`
resolves `cmake -S .` against whichever tree it last `cd`ed into; this cost time
on 2026-09-01.

| step | C++ binary | Java side | predict |
|---|---|---|---|
| 1 | `pz_geojson_oracle` | `GeoJsonOracle.java` | byte-identical on all ten GeoJSON files |
| 2 | `pz_rng_oracle` | `RngOracle.java` | 8,000 identical lines |
| 3 | `pz_footprint_oracle` | `FootprintOracle.java` | 20,386 lines; **X 0, G 0, area 0, hull 0, Rect 122, minAreaRect 2,385** |
| 4 | `pz_buildingplan_selftest` | `pzformat.BuildingPlan` | 62 lines, identical, **both exit 1 — Java FAILS today** |
| 4 | `pz_buildingplan_oracle` | `BuildingPlanOracle.java` | 160,657 lines |
| 5 | `pz_palettes_oracle` | `PalettesOracle.java` | synthetic 429,257 lines md5 `d28516268cdaae203275e778b2176563`; vanilla 20,036 lines md5 `ad3a18c5875bf2b908f826fc39a3e769` |
| 5 | `pz_maskrule_selftest` | `pzformat.MaskRule` | 15 lines, `9 / 9 cases pass`, **exit 0** |
| 6a | `pz_gisraster_oracle` | `GisRasterOracle.java` | 63,878 lines; divergent **only** `COS` (18) and `PROJ` (15,750) |
| 6b | `pz_groundregions_oracle` | `GroundRegionsOracle.java` | 14,327 lines, 738,904 bytes, md5 `9b8c27aff9144ae8e93397750ba21e14` |

Three of these predictions are not "everything matches", and that is the point.

- **Step 4's self-test must FAIL and exit 1 in both trees.** The Java suite has
  reported `no corridor-shaped rooms FAIL worst 5.7 NORTH 40x20
  bathroom[23,4 17x3]` since commit `0247ddc`. **A port that prints "all tests
  pass" has diverged.** Do not fix the underlying defect until after step 7; it
  would break the only oracle (STATE §40).
- **Step 5's vanilla leg needs the PZ install; its synthetic leg does not.**
  25 of 28 mutations were caught by synthetic data alone (STATE §41).
- **Step 6a is expected to diverge on `COS` and `PROJ` and nowhere else.** Those
  18 latitudes are seeded deliberately because `Math.cos` and `std::cos`
  disagree there by one ulp. Removing them makes the digest byte-identical at
  63,140 lines, which is the isolation test proving nothing hides behind the
  difference. A divergence in any **other** section is new information.

### The commands

```fish
set MM ~/Documents/PZMapMaker
set MC ~/Documents/PZMapCreation

cmake -S $MM -B /tmp/noqt -G Ninja -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON
cmake --build /tmp/noqt

cd $MC
javac -d out (find src -name '*.java')

java -cp out pzformat.GroundRegionsOracle /tmp/gr.java.txt
/tmp/noqt/pz_groundregions_oracle         /tmp/gr.cpp.txt
cmp /tmp/gr.java.txt /tmp/gr.cpp.txt; and echo "GROUNDREGIONS IDENTICAL"
```

### The falsifier that must keep passing

`pzgen` is the GIS layer and it must build with **no Qt at all**. It landed
inside `if(Qt6_FOUND)` once and built fine only because Qt6 happened to be
installed. `pzgen` must sit after `pzformat` and **before**
`find_package(Qt6)` in `CMakeLists.txt`.

```fish
cmake -S $MM -B /tmp/noqt -G Ninja -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON
cmake --build /tmp/noqt --target pz_groundregions_oracle pz_gisraster_oracle \
                                 pz_palettes_oracle pz_buildingplan_oracle
```

Expect `Qt6 NOT found — skipping pzmapmaker app` and every target building.

## 5. Two instruments a digest cannot replace

Both were established on 2026-09-02, each by a mutation that produced **no
digest change while being a real defect**. A cross-language digest compares two
programs' *output*; it says nothing about whether either program is
well-defined.

**Sanitizers.** Undefined behaviour can leave the output unchanged today and
change it under a different compiler or flag — which is exactly how the
`JavaRandom.nextInt` bug survived three port steps. Two mutations were caught
only this way: `hash01`'s multiplies transcribed into `int64_t` (8 signed
overflow reports, digest unchanged at `-O2`), and `d >= P.size()` relaxed to
`>` (out-of-bounds read of `P[4]`, digest unchanged).

```fish
cmake -S $MM -B /tmp/san -G Ninja -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON \
      -DCMAKE_CXX_FLAGS="-O1 -g -fsanitize=undefined,address" \
      -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=undefined,address"
cmake --build /tmp/san --target pz_groundregions_oracle
/tmp/san/pz_groundregions_oracle /tmp/gr.san.txt
```

**Predict: zero reports, and `/tmp/gr.san.txt` still byte-identical to the Java
digest.** The same run on `pz_gisraster_oracle` is worth doing and has not been.

**Branch coverage.** A passing digest is evidence about the code only to the
extent the corpus reaches it — the palettes corpus was rebuilt three times and
every bad version was green (§41). Measure coverage; do not infer it.

```fish
cmake -S $MM -B /tmp/cov -G Ninja -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="--coverage -O0" -DCMAKE_EXE_LINKER_FLAGS="--coverage"
cmake --build /tmp/cov --target pz_groundregions_oracle
cd /tmp/cov; and ./pz_groundregions_oracle /tmp/gr.cov.txt
gcov -b -c -o CMakeFiles/pzgen.dir/groundregions.cpp.gcda $MM/groundregions.cpp
```

**Predict for `groundregions.cpp`: 100% of 129 lines, 192 branches, 81.25%
taken at least once.** Excluding compiler-generated exception edges that is 156
of 160 outcomes; the four misses are proven-unreachable defensive guards, not
corpus gaps, and the proof is in `FINDINGS_F5_2026-09-01.md`. **Any new gap is a
weak corpus until shown otherwise** — that is the direction of the burden of
proof, per §40.

gcov measures the shipped source and changes nothing. Adding counters to a unit
to measure it would land in both trees and become part of the digest; prefer
gcov.

## 6. Optimisation sweep

Any unit that transcribes Java integer arithmetic must agree at **every**
optimisation level, not one. The `nextInt` bug matched at `-O0`, diverged at
`-O2`, and matched again at `-O2 -fwrapv`: same source, same compiler, three
answers. That table is what proved it was undefined behaviour rather than
arithmetic.

```fish
for o in -O0 -O1 -O2 -O3 -Ofast
    cmake -S $MM -B /tmp/opt$o -G Ninja -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON \
          -DCMAKE_CXX_FLAGS="$o"
    cmake --build /tmp/opt$o --target pz_groundregions_oracle
    /tmp/opt$o/pz_groundregions_oracle /tmp/gr.$o.txt
    cmp -s /tmp/gr.$o.txt /tmp/gr.java.txt; and echo "  OK   $o"; or echo "  DIFF $o"
end
```

**Do not reach for `-fwrapv`.** It hides the hazard behind a build flag that a
later `CMakeLists.txt` edit could drop silently, restoring the bug with no
source change to blame. Do the arithmetic in an unsigned type instead.

## 7. Second compiler

Everything above is reproduced on **g++ 13.3.0 and g++ 16.2.1** with OpenJDK 21,
step 6b included as of 2026-09-02. A result on one compiler is a result about
one compiler — the `nextInt` bug agreed with Java at `-O0` and disagreed at
`-O2` on the *same* compiler, so a single toolchain is not evidence of a correct
port, only of a self-consistent one.

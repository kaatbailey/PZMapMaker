## FINDINGS — F3b (port step 4, `BuildingPlan`) — 2026-09-01

**Status:** complete — code, self-test and digest oracle all matching. One item
unverified: the CMake wiring was written but not built through CMake (see
Unverified).

---

**What was done:**

- `BuildingPlan.java` (3,347 lines) ported to C++20, standard library only, no
  dependencies beyond `java_random.hpp` and `javaRound` from `footprintsnap.hpp`.
- The self-test (`main`, Java line 2614) ported. **62 lines, byte-identical to
  Java, both exiting 1.**
- `BuildingPlanOracle` (Pattern B digest) written on both sides. **160,657
  lines, byte-identical**, covering 20,052 layouts.
- Both oracles mutation-tested. Eight mutations caught by the digest; the two
  that were not were shown to be equivalent code, not blind spots.
- `buildingplan.cpp` added to the `pzgen` library; `buildingplan_oracle` and
  `buildingplan_selftest` added to the `pzgen` `foreach` loop (the one linking
  `pzgen`, not the older `pzformat` loop).

---

**Confirmed** (verified by running both trees, or by instrumented measurement):

- **The port matches Java on the self-test byte-for-byte on the first run,**
  including the FAIL line and exit code 1. Prediction before running was
  "62 lines, identical, exit 1"; it held.
- **The digest is byte-identical over 20,052 layouts.** Corpus generated with
  arithmetic and `JavaRandom` only — no transcendentals — per STATE §39.
- **`BuildingPlan` has zero dependencies.** `javac -d out
  src/main/java/pzformat/BuildingPlan.java` compiles the file alone, with no
  other source on the command line. This confirms F1's claim by construction
  rather than by call trace.
- **All six `Math.round` sites take provably non-negative arguments.**
  Instrumented every site and ran the full corpus: **116,502 calls, 0
  negative.** Every rounded expression is a function of `w`, `h` and a
  non-negative fraction (`depth*0.46`, `cross*0.38`, `cross*0.30`, `w*frac`,
  `h*frac`, `extra*weight/total`); none involves `x` or `y`.
- **`WEIGHT` is never iterated.** Every use is `getOrDefault` — Java lines
  1311, 2536, 2555. Verified by reversing the C++ table: digest unchanged.
- **`ENTRANCE` is membership-only**, Java lines 108, 479, 537, 1795, as F1
  said. Verified by removing `laundry` from the C++ set: **2,269 digest lines
  diverge**, so the set contents are load-bearing even though the order is not.
- **The digest catches what the self-test cannot.** Mutation results:

  | mutation | self-test | digest |
  |---|---|---|
  | `std::round` for `javaRound` (all 6 sites) | not caught | **not caught — unreachable, see above** |
  | `WEIGHT` table order reversed | not caught | **not caught — lookup-only, see above** |
  | `findOptionalRoomFromEnd` walks forwards | not caught | 234 lines |
  | nested `pick` in `recipe` reordered | not caught | 381 lines |
  | `openBetween` loses its short-circuit | not caught | 31 lines |
  | `reorderPrivateRooms` insert index → 0 | not caught | 900 lines |
  | `laundry` dropped from `ENTRANCE` | not caught | 2,269 lines |
  | `max(0, add)` → `max(1, add)` in `allocateWeightedSizes` | not caught | 27,583 lines |
  | `vScore <= hScore` → `<` in `split` | not caught | 1,230 lines |
  | `WEIGHT_DEFAULT` 15.0 → 16.0 | not caught | 5,622 lines |

  **Every one of the ten left the self-test byte-identical.** The self-test
  prints room counts, gaps and overlaps only; a wrong layout that still tiles
  the footprint exactly is invisible to it.
- **Two mutations survived the digest and both are equivalent code, not gaps.**
  `min(1, size)` computed before vs after the erase is identical whenever
  `bath > 0` (which forces size ≥ 2); `nextDouble() <= 0.35` requires an exact
  hit on a non-dyadic rational. Recorded so a later session does not read them
  as coverage holes.
- **The first digest version missed two mutations and was fixed.**
  `openBetween` escaped because every non-core pair sat last in the test
  vector, so the extra draw shifted a stream nothing printed. `reorderPrivateRooms`
  escaped because `recipe` always emits `bathroom` first, making the
  bathroom-forward branch unreachable from any recipe-generated corpus. Fixed by
  interleaving the pairs and adding 6,000 layouts built from arbitrary palette
  samples (digest section `P`). **A corpus drawn only from the unit's own
  upstream cannot reach branches that upstream never triggers.**

---

**Unverified** (believed, not tested — and what would test it):

- **The CMake wiring builds.** `CMakeLists.txt` was edited but everything here
  was compiled with direct `g++` calls. Test: `cmake -S . -B build -G Ninja;
  and cmake --build build`, then the falsifier
  `cmake -S . -B /tmp/noqt -G Ninja -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON` and
  `cmake --build /tmp/noqt --target pz_buildingplan_oracle
  pz_buildingplan_selftest` — the GIS layer must build without Qt.
- **Second-compiler reproduction.** All of this ran on GCC 13.3.0 /
  OpenJDK 21.0.10, x86-64. §39 reproduced step 3 on GCC 13 and GCC 16 before
  accepting it. Test: re-run both commands on the owner's GCC 16 machine and
  `cmp`. Predict identical.
- **`javaRound` is subtly not `Math.round`.** `footprintsnap.hpp` implements
  `floor(x + 0.5)`; Java's `Math.round` stopped being that in JDK 7.
  Measured: `Math.round(0.49999999999999994)` returns **0**, `floor(x+0.5)`
  returns **1**. Unreachable in `BuildingPlan` (arguments are non-negative and
  never near that value) but **latent in `FootprintSnap`**, where centroids do
  go negative. Test: feed `FootprintSnap` a ring whose centroid arithmetic
  lands on `0.49999999999999994`. Not attempted.
- **`%.1f` formatting.** Java's `Formatter` rounds HALF_UP over the shortest
  round-trip decimal; C's `printf` rounds half-to-even over the exact value.
  `javaFormat1f` reproduces Java. The current corpus reports `worst 5.7`, where
  the two agree, so the divergence is **not exercised by the passing test** —
  it was reasoned to, not measured. Test: force an aspect of exactly 1.35 or
  3.25 through the report line.

---

**Corrections** (something in STATE.md / CHUNKS.md is wrong):

1. **The 14,680-layout figure is wrong and matches nothing.**
   → Claimed in `FINDINGS_E13_2026-08-19.md`, and propagated to `CHUNKS.md` F3,
   `PROMPT_F1.md`, `PROMPT_BUILDINGPLAN.md` (×3) and `STATE.md` §37/§38/§39 (×4).
   → **Measured by instrumenting the file:** the current self-test makes
   **1,505 `plan` calls, 2,005 `recipe` calls, and emits 9,456 rooms.** The
   2026-08-19 version that produced the claim (commit `4bd8734`) makes
   **553 `plan` calls and 5,968 rooms.** Total squares tiled by the basic
   tiling test is 14,136 now and 6,072 then. **No quantity in either version
   is 14,680.**
   → Evidence: counters inserted at `plan` and `recipe` entry, both versions
   compiled and run. Also `git show 4bd8734:.../BuildingPlan.java`.

2. **The Java self-test does not pass. It has not passed since commit `0247ddc`.**
   → `FINDINGS_E13_2026-08-19.md` says "14,680 dwelling layouts pass with zero
   gaps, overlaps, corridor rooms, or missing core rooms," and every downstream
   document repeats the pass.
   → **Actual, run 2026-09-01:**
   `no corridor-shaped rooms  FAIL  worst 5.7  NORTH 40x20 bathroom[23,4 17x3]`,
   `1 BuildingPlan tests FAILED`, **exit code 1.**
   → The E13-era file (`4bd8734`) genuinely passed — verified by checking it
   out and running it: `worst 3.7  NORTH 20x17 closet[9,11 11x3]`, "all cases
   pass". The hub rewrite added four larger footprints (`18x12`, `25x16`,
   `30x20`, `40x20`) and the failure came with them.
   → **This changes the definition of done.** Matching Java means reproducing
   the failure and the exit code. A port that prints "all tests pass" has
   diverged.

3. **STATE §35 item 2's specific failing case is stale.**
   → §35 records the aspect failure as `NORTH 30x6 livingroom[0,0 30x2]`,
   aspect **15.0**, caused by the core being banded across the full frontage.
   → The hub rewrite fixed that: `NORTH 30x6` and `WEST 30x6` now PASS. The
   failure is a different case — `NORTH 40x20 bathroom[23,4 17x3]`, aspect 5.7,
   which is a **private-room packing** problem, not a core-banding one.
   → §35's OPEN item 2 ("core placed as a block, not a band") appears **done**;
   OPEN item 1 (the room-list loop) is what the 40x20 case now indicts.

4. **`WEIGHT` insertion order is not a contract.**
   → STATE §39 and `PROMPT_BUILDINGPLAN.md` both say "insertion order is the
   contract — use an insertion-ordered container in C++, not
   `std::unordered_map`."
   → It is read only through `getOrDefault` (lines 1311, 2536, 2555); nothing
   iterates it. Reversing the C++ table leaves the 160,657-line digest
   unchanged. **A `std::unordered_map` would in fact be safe today.**
   → An insertion-ordered container was used anyway, because the claim becomes
   true the moment anyone adds an iteration, and the cost is zero. The
   *instruction* was right for the wrong reason; recording which.

5. **`std::round` for `javaRound` is not a usable mutation test for this unit.**
   → `PROMPT_BUILDINGPLAN.md`: "For this unit the obvious mutation is
   `std::round` in place of `javaRound`."
   → It changes nothing. All six sites take non-negative arguments (116,502
   calls, 0 negative, measured), and the two functions agree there. Both the
   self-test and the digest are unchanged by it. A session following the prompt
   would mutation-test the oracle, see no diff, and conclude the oracle is
   broken.

6. **Neither `PZMapCreation` nor `PZMapMaker` is at the state STATE describes
   for the port branch.** `PZMapMaker` @ `18732b9` ("Port step 3: FootprintSnap
   to C++20") has no `BuildingPlan` files, as expected, but also still carries
   `FINDINGS_E13_2026-08-19.md` in the root, which §37 already flagged as
   disposable. Left alone — deletion is an owner call, and it is now the only
   surviving source of correction 1's bad number, so deleting it without
   fixing the five downstream copies would hide the provenance.

---

**Files changed:**

Created, in `~/Documents/PZMapMaker` (repo root, flat):
- `buildingplan.hpp` — Room, Facing, Rect, constants, full API + hazard notes
- `buildingplan.cpp` — the port
- `buildingplan_selftest.cpp` — ported `main` + `check` + `javaFormat1f`
- `buildingplan_oracle.cpp` — C++ digest side
- `BuildingPlanOracle.java` — Java digest side (354 lines; **`wc -l` it**)

Modified:
- `CMakeLists.txt` — `buildingplan.cpp` into `add_library(pzgen …)`;
  `buildingplan_oracle` and `buildingplan_selftest` into the `pzgen` `foreach`

Copied into the Java tree to compile:
- `~/Documents/PZMapCreation/src/main/java/pzformat/BuildingPlanOracle.java`

---

**Commands worth keeping:**

```fish
cd ~/Documents/PZMapMaker
cmake -S . -B build -G Ninja; and cmake --build build

cd ~/Documents/PZMapCreation
javac -d out src/main/java/pzformat/BuildingPlan.java \
             src/main/java/pzformat/BuildingPlanOracle.java

# Self-test — predict 62 lines, IDENTICAL, and BOTH exit 1 (Java FAILS today)
java -cp out pzformat.BuildingPlan > /tmp/bp.java.txt; echo $status
~/Documents/PZMapMaker/build/pz_buildingplan_selftest > /tmp/bp.cpp.txt; echo $status
cmp /tmp/bp.java.txt /tmp/bp.cpp.txt; and echo "SELFTEST IDENTICAL"

# Digest — predict 160,657 lines each, byte-identical, 20,052 layouts
java -cp out pzformat.BuildingPlanOracle /tmp/bpo.java.txt
~/Documents/PZMapMaker/build/pz_buildingplan_oracle /tmp/bpo.cpp.txt
cmp /tmp/bpo.java.txt /tmp/bpo.cpp.txt; and echo "DIGEST IDENTICAL"

# The falsifier: the GIS layer must build without Qt
cmake -S . -B /tmp/noqt -G Ninja -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON
cmake --build /tmp/noqt --target pz_buildingplan_oracle pz_buildingplan_selftest

# The empty-file guard (GeoJsonOracle.java landed at 0 lines on 2026-08-31)
wc -l ~/Documents/PZMapMaker/BuildingPlanOracle.java   # expect 354
find ~/Documents/PZMapMaker -maxdepth 1 -size 0 -not -path '*/.git/*'

# A mutation that DOES fail, to prove the digest can fail. Predict ~27,583
# divergent lines; revert afterwards.
#   in buildingplan.cpp: add = std::max(0, add);  ->  add = std::max(1, add);
```

---

**Noticed, out of scope:**

- **`BuildingPlan` never draws from the RNG in `packLinear` or `packSide`,**
  though both take a `Random`. Java passes it and never uses it. Reproduced
  (parameter kept, `(void)rng`) so the signature matches; worth removing in
  both trees together if the API is ever cleaned up.
- **`packSide`'s `livingSide` parameter is accepted and ignored** by Java too.
  Same treatment.
- **Fourteen symbols are dead — MEASURED, one occurrence each.**
  `weightSum`, `sideAHasRoom`, `roomCountNeedsMoreCapacity`, `depthAxis`,
  `findBedroomIndex`, `minRooms`, `ceilDiv`, `hallChance`, `A_LIVING`,
  `A_KITCHEN`, `A_BED`, `A_BATH`, `MIN_BEDROOM`, `MIN_CLOSET` each appear
  **exactly once** in `BuildingPlan.java` — their own declaration and nothing
  else. Checked externally too: `grep -ln` across all 65 Java files shows only
  `BuildingPlan.java`, `BuildingPlanOracle.java` and `GisCells.java` mention
  `BuildingPlan` at all, and `GisCells` uses only `recipe`, `plan`,
  `openBetween`, `Facing` and `Room`. `hallChance` carries a comment saying it
  is "retained because other code may call it" — **nothing calls it, in either
  tree.** All fourteen were ported and are digested anyway: deleting code
  during a port makes the oracle weaker for no gain. Candidates for a later
  cleanup chunk, applied to both trees at once.
- **The 40x20 aspect failure is a real layout defect, not a port artifact.**
  `bathroom[23,4 17x3]` is a 17×3 bathroom against a measured p90 of 12
  squares (STATE §34). `packAcross` gives every private room the full depth of
  the rear zone and splits only across, so a wide shallow rear zone produces
  strips. This is STATE §35 OPEN item 1 seen from a new angle. **Do not fix it
  during the port** — it would break the only oracle.

---

**What the next chunk needs to know:**

- **`GisCells` uses exactly five things from `BuildingPlan`** — MEASURED at
  `GisCells.java` lines 86, 272-276, 285, 600-639, 703-747: `recipe`, `plan`
  (the 7-arg overload only, never the SOUTH-default one), `openBetween`,
  `Facing` (including `.WEST/.EAST/.NORTH/.SOUTH` and `faceTheRoad`), and
  `Room` (`.type()`, and the record as a list element). **That is the whole
  F6 contract with this unit.** The other 40-odd exported symbols exist only
  for the self-test and the oracle. Nothing in F6 needs them to be right — but
  they are, and the digest proves it.
- **Port step 5 is next: the palettes** (`GroundMaterial`, `TilePalette`,
  `TreePalette`, `GroundPalette`, `MaskRule`, 1,021 lines). **This is the first
  step needing the PZ install** — its oracle is "same `TileIndex` in →
  identical tile-name tables out". Steps 1–4 needed no game files; step 5 does.
- **CHUNKS F3 should be split or re-worded.** It bundles `FootprintSnap` and
  `BuildingPlan` as one chunk; they were run as two sessions (F3a, F3b) and the
  deliverable line still cites the 14,680 figure.
- **The five documents carrying 14,680 need amending** (`CHUNKS.md` F3,
  `PROMPT_F1.md`, `PROMPT_BUILDINGPLAN.md`, `STATE.md` §37/§38/§39). Per
  Charter §5 the wrong belief moves to the Corrections table rather than being
  deleted. The replacement figure is **1,505 layouts** if the intent was
  `plan` calls, or **9,456 rooms** if it was rooms.
- **Two copies of STATE.md and CHUNKS.md still exist**, one per repo, and §37
  recorded they have diverged. This session read the `PZMapMaker` copies. The
  test is still `diff` them; still unresolved.
- **The pattern that keeps recurring, now four times.** §28's confounded
  run-length test, §30's first diagonal-run detector, §34's bounding-box BSP
  test, and §39's transcendental-contaminated corpus all produced clean,
  plausible numbers that agreed with the hypothesis under test. This session
  adds a fifth shape of the same thing: **an oracle whose corpus is generated
  by the unit's own upstream cannot reach branches that upstream never
  triggers.** `recipe` always emits `bathroom` first, so no recipe-driven
  corpus can ever exercise `reorderPrivateRooms`' bathroom-forward move. The
  general rule for the remaining Track F oracles: **feed the unit inputs its
  real caller would never produce, not only the ones it would.**

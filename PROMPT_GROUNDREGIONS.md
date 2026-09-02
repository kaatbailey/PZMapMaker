# Session prompt — Port step 6 (second half): `GroundRegions`

**How to use this:** open a new session. Paste, in this order:

1. `CHARTER.md`
2. `STATE.md`
3. **this file**

You do **not** need `STATE_ARCHIVED.md`.

---

## READ ALL OF `STATE.md`. NOT PART OF IT.

**This is not boilerplate. Skipping sections cost most of a session on
2026-09-01.**

That session read about ten sections of a 4,000-line file, skipped the
convention blocks, and then rediscovered — the hard way, over roughly a dozen
round trips — three things that were **already written down**:

- `grep` on this machine is a fish function wrapping **ugrep**. BRE alternation
  `a\|b` silently matches nothing, so an empty scan is indistinguishable from
  "no hazards found." Use `command grep` or `-e` per pattern.
- Files authored in a session container land **~4 hours in the future** (UTC
  mtimes against an EDT clock). Ninja then fails with `manifest 'build.ninja'
  still dirty` **after** printing `Configuring done`, builds nothing, and leaves
  the OLD binary in place — a green-looking build producing a stale result.
  `touch` everything, **including `CMakeLists.txt`**, which `touch *.cpp *.hpp`
  does not match.
- The PZ install path has an extra level:
  `.../common/ProjectZomboid/projectzomboid/media`.

All three are in §36's "Path reference" and "Build/run notes" blocks. Read them
before running anything. There is a Corrections row in §13 about precisely this
failure.

**Read the §13 Corrections table in full as well.** It is where wrong beliefs go
to be recorded rather than deleted, and several rows contradict things stated
confidently elsewhere in the same document.

---

## The repos

| repo | role |
|---|---|
| `https://github.com/kaatbailey/PZMapMaker` (branch `Master`) | the C++ port. All new code goes here, flat in the repo root. Local: `~/Documents/PZMapMaker` |
| `https://github.com/kaatbailey/PZMapCreation` | the Java tree — **the port oracle** (CHARTER §4). Archive-only for new work. Local: `~/Documents/PZMapCreation` |
| `https://github.com/kaatbailey/pzgis` | GIS test data + some Java oracles, living there temporarily |

Java oracle sources live in the C++ repo root **and** are copied into
`~/Documents/PZMapCreation/src/main/java/pzformat/` to compile.

---

## The chunk

**Port `GroundRegions` to C++20 — 308 lines, the second half of port step 6
(CHUNKS F5). Deliverable: code plus a passing cross-language oracle.**

Re-derive rather than trust:

```fish
wc -l ~/Documents/PZMapCreation/src/main/java/pzformat/GroundRegions.java
```

`GisImport`'s raster core (491 lines) is **done and verified on two compilers**.
See `FINDINGS_F5_2026-09-01.md` in the `PZMapMaker` repo root — read it before
starting, it is short and it contains the two things below.

---

## Read `FINDINGS_F5` first — two items bear directly on this chunk

**1. `java_random.hpp` was FIXED on 2026-09-01 and §38 is wrong about it.**
`nextInt`'s rejection loop transcribed Java's `u - (r = u % bound) + m < 0`,
where the overflow *is* the test. That is signed-overflow UB in C++, and at
`-O2` GCC deleted the branch so the loop never rejected. Measured: `-O0`
matches `java.util.Random`, `-O2` diverges on 6 of 20,000 seeds, `-O2 -fwrapv`
matches. Fixed in `uint32_t`. **Steps 3 and 4 have NOT been re-run against the
fixed version** — bound analysis says they are unaffected, but that is analysis,
not measurement, and it is two commands:

```fish
cmake --build /tmp/noqt --target pz_footprint_oracle pz_buildingplan_oracle
```

**2. Corpus bounds must be chosen ADVERSARIALLY.** Rejection probability is
`(2^31 mod bound) / 2^31`. `nextInt(100)` gives 2.2e-8; the palettes chunk
expected 0.02 rejections across a million draws and never fired one, so it
passed on luck. `nextInt(720000)` gives 2.1e-4 and found the bug in one run. A
convenient-looking bound is often the worst possible probe.

---

## What to check before writing code

**`GroundRegions` draws `java.util.Random`** (`GroundRegions.java:8, 161`).
`JavaRandom` reproduces it bit-exactly *since the fix*. Confirm the fixed header
is the one in your tree before relying on it.

**Ordering: `byMat` is an `EnumMap`** (`GroundRegions.java:163`), iterating in
ordinal order — deterministic. Reproduce that order; do not substitute a
`std::map` or `std::unordered_map`. This is the same resolution as `MaskRule`'s
`DirSet`. Verify it is still an `EnumMap` rather than assuming.

**Seeding is deliberate.** `GroundRegions.java:58-60` records that `GisCells`
seeds its `Random` per cell so a cell regenerates identically, and that
something here is driven by a **position hash**, not by that sequential
`Random`. Read those lines before designing the corpus — a position-hashed value
and a stream-drawn value fail in different ways, and only one of them shows up
in a shared-stream section.

**`GroundRegions` calls `MaskRule` and `GroundMaterial`**, both ported and
verified. Divergence there is very unlikely but the oracle should fingerprint
its inputs anyway, the way `PalettesOracle`'s `VIN` lines do, so a failure names
the right unit.

---

## Method — CHARTER §4, and the three that cost the most in F4/F5

- **Every number carries the command that produced it.** No command, no number.
- **Numbers are cited, never copied.** One document holds a figure; the rest
  point at it.
- **Byte-identical agreement is evidence only to the extent the corpus reaches
  the code.** The palettes corpus was rebuilt **three times**; every bad version
  produced a clean digest. One reached zero complete palettes. Measure branch
  coverage explicitly — count how many cases hit each branch and print it — do
  not infer coverage from a passing digest.
- **Mutation-test, and budget for the oracle needing to be strengthened once.**
  In F4 one no-diff mutation turned out to be an oracle hole rather than a null
  mutation, and adjudicating the three no-diffs individually gave three
  different verdicts.
- **Mutation verdicts do NOT transfer between steps.** §40 proved
  `std::round`-for-`javaRound` unreachable in step 4; it is reachable in step 6.
- **Predict before running.** Write down the expected result and what would
  falsify it, then run.

---

## Definition of done

- `GroundRegions` ported, C++20, standard library only, in the `pzgen` library.
- A cross-language digest, Java and C++, **byte-identical**, over a synthetic
  corpus with **measured and reported branch coverage**.
- The oracle mutation-tested; any no-diff mutation shown unreachable rather than
  assumed so.
- Reproduced on a second compiler (GCC 13 here, GCC 16 on the owner's machine).
- A `FINDINGS` block appended to `FINDINGS_F5_2026-09-01.md`.

**Also outstanding from earlier chunks, and small:**

- **The `GisImport` raster oracle has never been mutation-tested.** By this
  project's standard it has not been shown capable of failing. Do this — it is
  cheap and it guards work already declared done.
- `VERIFY.md` §4 still does not list `pz_palettes_oracle`,
  `pz_maskrule_selftest` or `pz_gisraster_oracle`.

**Not done if:** the digest matches but no branch-coverage numbers were
measured; or the oracle was never shown capable of failing.

---

## Delivery convention

Hand over **complete files, never edits to apply**, each with its expected
`wc -l` so the owner can confirm the right version landed before building.
Line counts distinguish drop N from drop N+1; **mtimes do not** — `find
-newermt` gave a wrong answer twice on 2026-09-01. Name drop notes
`DROP_<step>_<n>_<date>.md`; four files named `APPLY.md` silently overwrote each
other in one session.

---

## After this

Step 7 — `TreeScatter`, `BiomeMapWriter`, `GisCells`, 1,206 lines. Oracle:
**byte-identical mod output**, Ohio and Tokyo. This is where the C++ side can
first generate a map. Two things block or threaten it:

- **The A2-gate (§25) is still open** and gates `TreePalette` and `TreeScatter`.
  It needs a positional test in game; no amount of porting resolves it.
- **PNG byte-identity** (§37's second OPEN): Qt `QImage` may not reproduce Java
  `ImageIO` byte-for-byte — colour type, bit depth, filter, zlib level. Test it
  early: encode the same pixel buffer through both and `cmp`. Finding this out
  at the end of step 7 would be expensive.

# Session prompt — Port step 5: the palettes

**How to use this:** open a new session. Paste, in this order:

1. `CHARTER.md`
2. `STATE.md`
3. **this file** — and nothing else from `CHUNKS.md`

Per CHUNKS: never paste more than one chunk prompt.

---

## The chunk

**Port five files to C++20, 1,021 lines total. Deliverable: code plus a passing
cross-language oracle.**

| file | lines | what it is |
|---|---|---|
| `GroundMaterial.java` | 114 | material → tile-block mapping; one RNG call |
| `TilePalette.java` | 324 | picks floor/wall/door tiles out of the install |
| `GroundPalette.java` | 206 | picks ground groups and rolls a square |
| `MaskRule.java` | 257 | blend-mask index selection; has its own `main` |
| `TreePalette.java` | 120 | **see the A2-gate note below — may be out of scope** |

Line counts measured 2026-09-01, not inherited.

**`WaterTiles.java` (74 lines) is NOT a port target.** STATE §37 classified it
SURVEY — zero callers. Do not port it. If a session finds itself porting it, it
has misread the inventory.

## THIS IS THE FIRST STEP THAT NEEDS THE PZ INSTALL

Steps 1–4 needed no game files. This one does: every `pick()` takes a
`TileIndex` and a `Set<String>` of sprite names loaded from
`media/texturepacks`. The oracle is **"same `TileIndex` in → identical
tile-name tables out"**, which means both trees must read the same install.

**Check this before writing any code.** If the install is missing or the
version has moved, the oracle cannot run and the whole chunk stalls — find out
in the first five minutes, not the last hour.

## The A2-gate question — settle it before porting `TreePalette`

STATE §37 and §25 both leave this open: **`TreeScatter` / `TreePalette` — port
or delete?** The engine deletes the ~7,800 trees they place (§9), A2a is
BLOCKED on tree ownership, and porting a unit that is about to be deleted is
wasted work in both trees.

**Do not resolve A2-gate in this session** — it needs a positional test in game
and it is its own chunk. Instead:

- If A2-gate is still open when you start: **port the other four, leave
  `TreePalette`.** That is 901 lines, and the chunk is complete without it.
- Say so explicitly in FINDINGS so the next session does not think it was
  forgotten.

## Already scanned — do not rediscover

This grep was run on 2026-09-01:

- **`SpriteNames.load` returns a `HashSet`** (`SpriteNames.java:36`). Every
  `pick()` takes it. **`HashSet` iteration order is not part of the language
  contract and can move between JVM versions.** This is step 5's version of
  §40's `ENTRANCE` hazard, and it is bigger, because the set is the primary
  input rather than an internal constant.
- **`TilePalette` already defends against it** — `first()` collects into
  `hits`, calls `Collections.sort(hits)`, and its fallback iterates
  `new TreeSet<>(ti.byName.keySet())` (lines 234, 238). So it is deterministic
  today. **`TreePalette` does NOT** — line 51 iterates `ti.byName.keySet()`
  raw, and line 110 iterates `bySize.entrySet()`. **Establish what `byName` and
  `bySize` actually are before assuming either is safe.** If either is a
  `HashMap`, Java itself is nondeterministic there and no oracle will ever
  clear — that is a finding to report, not a bug to fix in the port.
- **RNG call sites: `GroundMaterial.solid(Random)` (line 100),
  `GroundPalette.roll(Random)` (line 173), `MaskRule.masks(...)` and
  `MaskRule.side(...)` (lines 77, 125).** `JavaRandom` reproduces
  `java.util.Random` bit-exactly (§38). `GroundMaterial` and `GroundPalette`
  have no other hazards.
- **`MaskRule` has its own `main` (line 143)** with fixed-seed cases
  (`new Random(1)` at 207, `new Random(12345)` at 233). Port it as a second
  oracle, the way step 4 ported `BuildingPlan`'s.
  **§29 records that this self-test passed 8/8 with N and W transposed** — it
  never exercises the neighbour lookup. **It is a weak oracle. Do not treat a
  green `MaskRule` self-test as evidence of anything.**
- **`MaskRule` uses `Arrays.sort` (line 254) and a `HashSet` (236)** inside its
  self-test only.
- **No transcendentals found in any of the five.** §39's `minAreaRect`
  divergence does not apply. Do not go looking for it.

## The lessons from steps 3 and 4 — both bit, both will bite again

**1. A cross-language oracle must generate its corpus using arithmetic and
`JavaRandom` only. No transcendentals.** Step 3's first measurement was
contaminated because the corpus generator used `Math.cos`/`std::cos`, so the
*inputs* differed before the unit under test ran. It reported 81 divergences;
the real number was 122.

**2. A corpus drawn from the unit's own upstream cannot reach every branch.**
Step 4's first digest missed two of ten mutations. `recipe` always emits
`bathroom` first, so no recipe-generated corpus could ever exercise
`reorderPrivateRooms`' bathroom-forward move; and every non-core pair sat last
in the `openBetween` vector, so a stream shift changed nothing printed.

**For this chunk that means: do not build the corpus only from tile names that
exist in the vanilla `TileIndex`.** The interesting branches are the
missing-tile and unknown-property paths — `first()` returning `null`,
`complete()` returning false, a material with no matching block. Feed the units
sprite sets the real install would never produce: empty, single-element, one
missing a required prefix, one with names that pass `ok.test` but are absent
from `sprites`.

**3. A named mutation can be unreachable.** Step 4's prompt named `std::round`
for `javaRound` as "the obvious mutation". It could not fail — all six sites
take non-negative arguments (116,502 calls, 0 negative, measured). A session
following that prompt would mutate, see no diff, and wrongly conclude its
oracle was broken. **If a mutation produces no diff, prove the mutated code is
unreachable before concluding the oracle is weak.**

## Method

Charter §4 applies.

**Predict before running.** Before writing the oracle, write down how many tile
names you expect each `pick()` to resolve, and which of the five units you
expect to diverge first. If the prediction is "none", say what would falsify
that.

**Mutation-test the oracle, and expect the obvious mutations to be
insufficient.** Step 4's self-test survived **all ten** deliberate mutations;
only the digest caught eight of them. Budget for the oracle needing to be
strengthened once, because it will be.

**Port in pieces and diff as you go.** `GroundMaterial` is 114 lines and has
one RNG call — get it matching before `TilePalette`. `MaskRule` is independent
of the other four and can go in parallel.

## Definition of done

- All four (or five, if A2-gate resolved) units ported, C++20, standard library
  only, in the `pzgen` library.
- The ported `MaskRule` self-test matches Java exactly, **including its exit
  code** — if Java's fails, the port must fail identically. Step 4's did.
- A `PalettesOracle` digest, Java and C++ sides, byte-identical over the full
  vanilla sprite set **plus at least 5,000 synthetic sprite sets** that the
  real install would never produce.
- The oracle has been mutation-tested, and any mutation that produced no diff
  has been shown to be unreachable rather than assumed to be.
- Reproduced on a second compiler. GCC 13 and GCC 16 for steps 3 and 4.
- A `FINDINGS` block in the format at the bottom of `CHUNKS.md`.

**Not done if:** the units match on the vanilla install but no synthetic corpus
exists (the vanilla install exercises one path per `pick`), or the oracle was
never shown to be capable of failing, or `TreePalette` was silently skipped
without a note.

## Build wiring

C++ repo root, flat. The palette `.cpp`/`.hpp` files join the `pzgen` library;
`palettes_oracle` and `maskrule_selftest` join the `pzgen` `foreach` loop — the
one that links `pzgen`, **not** the older loop that links `pzformat`.
`PalettesOracle.java` goes in the C++ repo root and is copied into
`src/main/java/pzformat/` to compile.

Three operational gotchas that have each cost time:

- **`wc -l` every dropped file before building.** `GeoJsonOracle.java` was
  committed at 0 lines on 2026-08-31. An empty *oracle* fails in the worst
  direction — a later session finds no Java side and assumes it was never
  written.
- **`touch` files authored off-machine**, or Ninja loops "manifest still dirty".
- **Use absolute paths in any command block that spans both repos.** Step 4's
  block `cd`'d into `PZMapCreation` for `javac`, so a later `cmake -S .`
  resolved to the Java tree and failed. Write
  `cmake -S ~/Documents/PZMapMaker -B ...`, not `-S .`.

Falsifier, every time: the GIS layer must still build without Qt.

```fish
cmake -S ~/Documents/PZMapMaker -B /tmp/noqt -G Ninja -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON
cmake --build /tmp/noqt --target pz_palettes_oracle pz_maskrule_selftest
```

## After this

Step 6 — `GisImport` raster (`Cover` grid, `fillPolygon`, `thickLine`,
`waterLine`, `deriveWalls`, projection). Oracle: identical `Cover` grid
compared cell by cell. That is CHUNKS F5 and it depends on this chunk plus F2.

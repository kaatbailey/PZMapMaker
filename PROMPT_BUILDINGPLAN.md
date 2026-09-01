> **THIS CHUNK IS COMPLETE — 2026-09-01. Do not run it again.**
> Outcome in `FINDINGS_F3b_2026-09-01.md` and STATE §40. Kept for provenance.
>
> **Three things in the prompt below are wrong. They are struck through in
> place rather than deleted, per CHARTER §5.**
>
> 1. **"14,680 layouts" matches nothing.** Measured: **1,505 `plan` calls,
>    2,005 `recipe` calls, 9,456 rooms.** The 2026-08-19 version that coined
>    the figure makes 553 `plan` calls.
> 2. **The Java self-test FAILS**, and has since commit `0247ddc`:
>    `no corridor-shaped rooms  FAIL  worst 5.7  NORTH 40x20 bathroom[23,4 17x3]`,
>    exit 1. Matching Java means REPRODUCING the failure.
> 3. **`std::round` for `javaRound` cannot fail as a mutation test.** All six
>    sites take non-negative arguments — 116,502 calls, 0 negative, measured.
>    A session following this prompt would mutate, see no diff, and wrongly
>    conclude its oracle was broken.
>
> Also: **`WEIGHT`'s insertion order is not a contract** (lookup-only via
> `getOrDefault`; reversing it changed nothing).

# Session prompt — Port step 4: `BuildingPlan`

**How to use this:** open a new session. Paste, in this order:

1. `CHARTER.md`
2. `STATE.md`
3. **this file** — and nothing else from `CHUNKS.md`

Per CHUNKS: never paste more than one chunk prompt.

---

## The chunk

**Port `BuildingPlan.java` (3,347 lines) to C++20. Deliverable: code plus a
passing cross-language oracle.**

This is **49% of the entire GIS port** and the single largest unit in Track F.
It is also the easiest to isolate: **zero `pzformat` dependencies**, no PZ
install, no media directory, no tile data. It needs only `JavaRandom`, which is
ported and verified (STATE §38).

It is the layout engine from E13/E14 — a BSP split plus a circulation pass,
inlined here rather than living in `RoomLayout` (STATE §37 established that
`RoomLayout`, `RoomMinimums`, `HouseRules` and `HallRule` all have zero callers
and are SURVEY).

## The oracle

**`BuildingPlan` has its own self-test over ~~14,680~~ 1,505 layouts, and it FAILS:**

```fish
cd ~/Documents/PZMapCreation
java -cp out pzformat.BuildingPlan
```

Port that `main` (line 2614) and make it match. That is the primary oracle and
it was named as such in F1.

**Then a Pattern B digest oracle** — `BuildingPlanOracle`, Java and C++ sides —
because the self-test only reports pass/fail, and a digest localises a
divergence to a specific layout. Follow `FootprintOracle` (already in the repo
root) for the shape: fixed cases first, then a randomised corpus, tab-separated,
fixed field order, no sorting.

## Already scanned — do not rediscover

This grep was run on 2026-08-31 (STATE §39):

- **No `atan2`, no `Math.cos`, no `Math.sin` anywhere in the file.** The
  `minAreaRect` transcendental divergence accepted in §39 **does not apply
  here**. Do not go looking for it.
- **6 `Math.round` sites** — lines 798, 860, 1309, 1383, 2317, 2327. **All need
  `javaRound`**, already in `footprintsnap.hpp`. `Math.round(x)` is
  `floor(x + 0.5)`; `std::round` is round-half-away-from-zero and they disagree
  on every negative half-integer.
- **No `HashMap`, no `HashSet`, no `Arrays.sort`, no streams.** The only map is
  `WEIGHT` (line 155), a `LinkedHashMap` filled in a static block. **Insertion
  order is the contract** — use an insertion-ordered container in C++, not
  `std::unordered_map`.
- **`ENTRANCE` is `java.util.Set.of(...)` (line 184).** `Set.of` iteration order
  is **randomised per JVM run** in Java 9+. It is only used via `.contains()`
  (lines 108, 479, 537, 1795), never iterated, so it is safe today. **If the
  port iterates it, or a future edit does, Java itself becomes nondeterministic
  and no oracle will ever clear.** A plain sorted set or array is the right C++
  choice.
- **10 RNG call sites.** `JavaRandom` reproduces `java.util.Random` bit-exactly
  — verified over 8,000 draws (§38). `nextDouble` against fixed thresholds
  (`LK_OPEN` 0.88, 0.62, 0.16) is the usage pattern.

## The lesson from step 3 — this one bit, and it will bite again

**A cross-language oracle must generate its corpus using arithmetic and
`JavaRandom` only. No transcendentals.** Step 3's first measurement was
contaminated because the corpus generator used `Math.cos`/`std::cos`, so the
*inputs* differed between trees before the unit under test ran. It reported 81
divergences; the real number was 122, and the 33 impossible "area bit"
differences were the clue.

If a digest shows differences in a field that is pure integer or pure arithmetic,
suspect the harness before the port.

## Method

Charter §4 applies.

**Predict before running.** Before writing the oracle, write down: how many of
the ~~14,680~~ 1,505 self-test layouts you expect to match on the first run, and which
construct you expect to break first. If the prediction is "all of them", say
what would falsify that.

**Mutation-test the oracle.** Both F2 oracles and the F3 oracle were checked by
deliberately breaking the C++ and confirming the diff caught it. A test that
cannot fail proves nothing. For this unit the obvious mutation is `std::round`
in place of `javaRound`.

**Port in pieces and diff as you go.** 3,347 lines is too large for one
big-bang comparison — a single divergence at the end tells you nothing about
where it came from. `openBetween`, `hallChance`, `recipe` and `pick` are small
and near the top; get those matching before `plan` and `hubLayout`.

## Definition of done

- The ported self-test matches Java over all ~~14,680~~ 1,505 layouts —
  **including Java's FAIL line and its exit code 1.**
- A `BuildingPlanOracle` digest is byte-identical across trees on a randomised
  corpus of at least 10,000 layouts.
- The oracle has been mutation-tested.
- A `FINDINGS` block in the format at the bottom of `CHUNKS.md`.

**Not done if:** the self-test passes but no digest oracle exists (pass/fail
cannot localise a bug), or the oracle was never shown to be capable of failing.

## Build wiring

C++ repo root, flat. `buildingplan.cpp`/`.hpp` join the `pzgen` library;
`buildingplan_oracle` joins the `pzgen` `foreach` loop — the one that links
`pzgen`, **not** the older loop that links `pzformat`. `BuildingPlanOracle.java`
goes in the C++ repo root and is copied into `src/main/java/pzformat/` to
compile.

Two operational gotchas that have each cost time:

- **`wc -l` every dropped file before building.** `GeoJsonOracle.java` was
  committed at 0 lines on 2026-08-31. An empty *oracle* fails in the worst
  direction — a later session finds no Java side and assumes it was never
  written.
- **`touch` files authored off-machine**, or Ninja loops "manifest still dirty".

## After this

Step 5 — the palettes (`GroundMaterial`, `TilePalette`, `TreePalette`,
`GroundPalette`, `MaskRule`, 1,021 lines). **That is the first step needing the
PZ install**, since its oracle is "same `TileIndex` in → identical tile-name
tables out".

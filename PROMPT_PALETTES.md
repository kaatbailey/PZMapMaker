# Session prompt — Port step 5: the palettes

**How to use this:** open a new session. Paste, in this order:

1. `CHARTER.md`
2. `STATE.md`
3. **this file** — and nothing else from `CHUNKS.md`

Per CHUNKS: never paste more than one chunk prompt.
You do **not** need `STATE_ARCHIVED.md`.

---

## The chunk

**Port the palette layer to C++20. Deliverable: code plus a passing
cross-language oracle.**

Units and line counts — command given, so you can re-derive rather than trust:

```fish
cd ~/Documents/PZMapCreation/src/main/java/pzformat
wc -l GroundMaterial.java TilePalette.java GroundPalette.java MaskRule.java TreePalette.java
```

Measured 2026-09-01 at `Master`: 114 / 324 / 206 / 257 / 120, total **1,021**.
**Re-run it.** These are counts of files you are about to edit; per CHARTER §4
they expire whenever the Java tree moves.

**`WaterTiles.java` is NOT a port target.** STATE §37 classified it SURVEY —
zero callers. Confirm rather than assume:

```fish
grep -rln "WaterTiles" --include='*.java' ~/Documents/PZMapCreation/src
```

## Do these two things first, before writing any code

**1. Confirm the PZ install is present and readable.** This is the first port
step that needs it. Steps 1–4 needed no game files; this one cannot run its
oracle without one. Find out in the first five minutes, not the last hour.

```fish
set MEDIA ~/.local/share/Steam/steamapps/common/ProjectZomboid/media
ls $MEDIA/texturepacks | wc -l
```

**2. Resolve or defer the A2-gate.** `TreePalette` places trees the engine
deletes on load (STATE §9), A2a is BLOCKED on tree ownership (§25), and porting
a unit that is about to be deleted is wasted work in two trees.

**Do not resolve A2-gate in this session** — it needs a positional test in game
and it is its own chunk. If it is still open: **port the other four, leave
`TreePalette`.** That is 901 lines and the chunk is complete without it. **Say
so explicitly in FINDINGS** so the next session does not read it as an
oversight.

---

## THE CENTRAL HAZARD — read this before designing the oracle

**`TileIndex.byName` is a `java.util.HashMap`.**

```fish
grep -n "byName" ~/Documents/PZMapCreation/src/main/java/pzformat/TileIndex.java
# TileIndex.java:20:  public final Map<String, TileDefs.Tile> byName = new HashMap<>();
```

Two units iterate it, and they are **not** equally safe:

| unit | line | iterates | outcome |
|---|---|---|---|
| `TilePalette` | 223 | `ti.byName.keySet()` raw | **SAFE** — collects into `hits`, then `Collections.sort(hits)` at 234 before `get(0)` |
| `TilePalette` | 238 | `new TreeSet<>(ti.byName.keySet())` | **SAFE** — sorted by construction |
| `TreePalette` | 51 | `ti.byName.keySet()` raw | **NOT SAFE — order reaches byte output** |

**Why `TreePalette` is not safe, traced end to end.** `pick()` appends to the
`bySize` lists and to `all` in HashMap iteration order (lines 74–75). `bySize`
is a `TreeMap`, so its *keys* are sorted — but its `List<String>` *values* are
not. Then:

- `TreeScatter.java:116` — `variants.get(rng.nextInt(variants.size()))`.
  **List order selects which tile is placed.**
- `GisCells.java:123` — `names.addAll(treePal.all)`, and `names` becomes the
  lotheader tile table. **List order IS the file's tile-index numbering.**

**What this means for the port, and what it does not.** Java's `HashMap`
iteration order is *deterministic* for a given insertion sequence — it is not
randomised per run the way `Set.of` is (§40). So the Java side reproduces
itself, and the Java tree remains a valid oracle. **But `std::unordered_map`
will not reproduce Java's bucket order**, and neither will `std::map`, which is
sorted. Three honest options; pick one deliberately:

1. **Prove the order never reaches the digest** for the subset you port. If
   `TreePalette` is deferred on the A2-gate, this may not apply to this chunk
   at all — **check, then say so.**
2. **Replicate Java's `HashMap` ordering** in C++ — bucket index from
   `String.hashCode`, the JDK's `h ^ (h >>> 16)` spread, plus capacity and
   resize history. Faithful, and genuinely awkward.
3. **Change both trees together** to iterate a `TreeSet`, making the order
   sorted and reproducible. This edits the oracle, so it must be a recorded
   decision, applied to Java and C++ in the same commit, with the digest
   re-measured on both sides afterwards.

**Do not pick silently.** Whichever you choose goes in FINDINGS with the reason.

---

## Already scanned — do not rediscover

Commands given so you can re-derive rather than trust. Run 2026-09-01.

```fish
cd ~/Documents/PZMapCreation/src/main/java/pzformat
grep -n "Random\|Math\.\|HashMap\|HashSet\|TreeMap\|Arrays.sort\|Collections.sort\|keySet()\|entrySet()" \
  GroundMaterial.java TilePalette.java GroundPalette.java MaskRule.java TreePalette.java
```

- **RNG sites:** `GroundMaterial.solid(Random)` line 100;
  `GroundPalette.roll(Random)` line 173; `MaskRule.masks(...)` line 77 and
  `MaskRule.side(...)` line 125. `JavaRandom` reproduces `java.util.Random`
  bit-exactly (§38). `GroundMaterial` and `GroundPalette` have no other
  hazards and are the right two to port first.
- **`SpriteNames.load` returns a `HashSet`** (`SpriteNames.java:36`) and every
  `pick()` takes it. It appears to be used only via `.contains()` — verify that
  before relying on it, the way §40 verified `ENTRANCE`.
- **`MaskRule` has its own `main`** at line 143, with fixed seeds
  `new Random(1)` at 207 and `new Random(12345)` at 233. Port it, the way step
  4 ported `BuildingPlan`'s.
- **`MaskRule`'s self-test is KNOWN WEAK.** §29 records it passing 8/8 with N
  and W transposed, because it never exercises the neighbour lookup. **A green
  `MaskRule` run is not evidence.** §40 then measured the general case: ten
  deliberate mutations of `BuildingPlan` left its self-test byte-identical.
- **`TilePalette` line 297 iterates `t.props.entrySet()`.** Establish what
  `props` is before assuming it is safe — same question as `byName`.
- **No transcendentals in any of the five.** §39's `minAreaRect` divergence
  does not apply here. Do not go looking for it.

---

## The lessons that have each cost a session

**1. The corpus must be built from arithmetic and `JavaRandom` only.** Step 3's
first measurement was contaminated because its generator used
`Math.cos`/`std::cos`, so the *inputs* differed between trees before the unit
under test ran. It reported 81 divergences; the real number was 122 (§39).

**2. A corpus drawn from the unit's own upstream cannot reach every branch.**
Step 4's first digest missed two of ten mutations: `recipe` always emits
`bathroom` first, so no recipe-generated corpus could exercise
`reorderPrivateRooms`' bathroom-forward move (§40).

**For this chunk that means: do not build the corpus only from the vanilla
`TileIndex`.** The real install exercises roughly one path per `pick()`. The
interesting branches are the failure paths — `first()` returning `null`,
`complete()` returning false, `tilesNear()` falling back through all eight
distance steps, a material with no matching block. **Feed the units sprite sets
the real install would never produce:** empty, single-element, one missing a
required prefix, one whose names pass `ok.test` but are absent from `sprites`.

**3. A named mutation can be unreachable.** Step 4's prompt named `std::round`
for `javaRound` as "the obvious mutation". It could not fire — all six sites
take non-negative arguments, measured at 116,502 calls and 0 negative. A
session following that prompt would mutate, see no diff, and wrongly conclude
its oracle was broken. **If a mutation produces no diff, prove the code is
unreachable before concluding the oracle is weak.**

---

## Method

CHARTER §4 applies, including the amendments of 2026-09-01. Three bear directly
on this chunk:

- **Every number you write carries the command that produced it.** If no
  command emits it, build the instrument or do not write the number.
- **Numbers are cited, not copied.** If a figure belongs in FINDINGS and in
  STATE, it lives in one and the other points at it.
- **Distinguish permanent facts from expiring ones.** "The install has N
  texturepacks" expires when the game updates. "`blends_natural_01` carries 12
  mask indices per material" is a fact about shipped data. Stamp the first kind.

**Predict before running.** Before writing the oracle, write down: how many
tile names you expect each `pick()` to resolve on the vanilla install, and
which of the four (or five) units you expect to diverge first. If the
prediction is "none", say what would falsify it.

**Mutation-test, and budget for the oracle needing to be strengthened once.**
That happened in step 4, and the first version looked fine.

**Port in pieces and diff as you go.** `GroundMaterial` is 114 lines with one
RNG call — get it matching before `TilePalette`. `MaskRule` is independent of
the other four and can go in any order.

---

## Definition of done

- Four units ported (five if the A2-gate resolved), C++20, standard library
  only, in the `pzgen` library.
- The ported `MaskRule` self-test matches Java exactly, **including its exit
  code** — if Java's fails, the port must fail identically. Step 4's did, and
  that was the correct outcome.
- A `PalettesOracle` digest, Java and C++ sides, byte-identical over the
  vanilla sprite set **plus at least 5,000 synthetic sprite sets** the real
  install would never produce.
- The oracle mutation-tested, and any mutation producing no diff shown to be
  unreachable rather than assumed so.
- **The `byName` ordering decision recorded** — which of the three options, and
  why.
- Reproduced on a second compiler. GCC 13 and GCC 16 for steps 3 and 4.
- A `FINDINGS` block in the format at the bottom of `CHUNKS.md`.

**Not done if:** the units match on the vanilla install but no synthetic corpus
exists; or the oracle was never shown capable of failing; or `TreePalette` was
skipped without a note; or the `byName` ordering question was left implicit.

---

## Build wiring

C++ repo root, flat. The palette `.cpp`/`.hpp` files join the `pzgen` library;
`palettes_oracle` and `maskrule_selftest` join the `pzgen` `foreach` loop — the
one linking `pzgen`, **not** the older loop linking `pzformat`.
`PalettesOracle.java` goes in the C++ repo root and is copied into
`src/main/java/pzformat/` to compile. Add the new oracles to `VERIFY.md` §4.

Three operational gotchas that have each cost time:

- **`wc -l` every dropped file before building.** `GeoJsonOracle.java` was
  committed at 0 lines on 2026-08-31. An empty *oracle* fails in the worst
  direction — a later session finds no Java side and assumes it was never
  written.
- **`touch` files authored off-machine**, or Ninja loops "manifest still dirty".
- **Absolute paths in any command block spanning both repos.** Step 4's block
  `cd`'d into `PZMapCreation` for `javac`, so a later `cmake -S .` resolved to
  the Java tree and failed. This has now cost time twice.

The falsifier, every time — the GIS layer must build without Qt:

```fish
cmake -S ~/Documents/PZMapMaker -B /tmp/noqt -G Ninja -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON
cmake --build /tmp/noqt --target pz_palettes_oracle pz_maskrule_selftest
```

---

## After this

Step 6 — `GisImport` raster: `Cover` grid, `fillPolygon`, `thickLine`,
`waterLine`, `deriveWalls`, projection. Oracle: identical `Cover` grid compared
cell by cell. That is CHUNKS F5, and it depends on this chunk plus F2.

# FINDINGS — C3 render harness (C1 §1.2 gate) — 2026-08-22

**Status:** complete

The C1 §1.2 falsifier is resolved. The headline is not the raw PASS/FAIL the
tool prints — it is that **the gate's threshold was mis-specified**, and the
measurements say what the real bound is.

## What was done

Built `harness/instance_harness.cpp` — a throwaway, links no project code.
Renders N sprite instances to a 1440p offscreen FBO in one
`glDrawElementsInstanced` call, alpha-blended, sampling a 256-layer
`GL_TEXTURE_2D_ARRAY`, timed with a `GL_TIME_ELAPSED` GPU query (median over 300
frames, vsync off). CLI: `instance_harness [instances] [spritePx] [blend0|blend1]`.

Hardware: RTX 3070 Ti, NVIDIA 610.43.03, GL 4.5 core.

## The measurements

All at 500k instances unless noted; median GPU ms:

| sprite | fragments | overdraw | median | verdict |
|---|---|---|---|---|
| 3px | 4.5M | 1.2× | **0.655** | floor-dominated |
| 9px | 40.5M | 11.0× | **2.543** | — |
| 18px | 162.0M | 43.9× | **4.814** | over 4ms |
| 18px, blend OFF | 162.0M | 43.9× | **4.221** | — |
| **200k @ 18px** | 64.8M | 17.6× | **1.903** | **PASS** |
| 1M @ 18px | 324.0M | 87.7× | 9.634 | linear vs 500k |

## Confirmed (measured, not inferred)

1. **The instancing mechanism has no draw-call cliff.** 500k → 1M is linear
   (4.81 → 9.63ms). One instanced draw pushing a million instances is fine.
   C1's fear that "a naïve per-sprite draw dies immediately" was correct about
   per-sprite *draw calls*, and does not apply to *instanced* draw.

2. **Instance count is cheap; the bound is fill (fragments on screen).**
   At near-zero fill (3px) 500k instances cost 0.655ms — a ~1.3ns/instance
   floor. Holding instance count fixed at 500k and changing only sprite size
   took the frame 0.655 → 4.814ms, a 7.3× swing driven entirely by fragment
   count. The 500k FAIL at 18px is a **44× overdraw** result — an artifact of
   the harness scattering sprites across the whole surface — not a statement
   about instancing.

3. **Blending is a minor contributor.** Same 162M fragments, blend on vs off:
   4.814 vs 4.221ms. The framebuffer read-modify-write is ~12% of the cost;
   the dominant cost is shaded-fragment throughput (texture fetch + shading),
   not the blend. This *refutes* an earlier working guess that the blend RMW
   was the expensive part.

4. **C1's actual predicted 1:1 load passes.** C1 §1.2 estimated 150–200k
   instances for an 80×80×8 viewport. 200k @ 18px = 1.903ms, well under budget.

## Correction to C1 §1.2

**Old claim:** "push 500k instances … if it is not comfortably under 4ms,
instanced draw is wrong and we fall back to chunked merged geometry."

**What is actually true:** the 500k-<4ms gate conflated two independent
variables — instance count and fragment/overdraw count — into one pass/fail
number. Instanced draw is **confirmed viable**; the fill ceiling at 44× overdraw
is what fails, and the named fallback (chunked merged geometry) would not have
helped, because it reduces draw-call and vertex work while this is
fragment-bound. Applying that fallback would have paid complexity for no gain.
The harness earned its cost precisely by showing the fallback was the wrong
lever *before* it was built.

## Unverified / imperfect (say what would test it)

- **The fill curve is concave (saturating), not linear.** A `floor + k·frag`
  model fit through 3px and 18px over-predicts nothing at the ends but the 9px
  point sits 0.94ms *above* the line — the middle is slower than linear, i.e.
  at very high overdraw extra fragments get cheaper per unit (ROP/blend
  saturation). Three points do not justify fitting a precise law; treat the
  table as the datum, not a formula. To pin the curve: sweep sprite size 3–24px
  at fixed 500k and plot.
- **Real viewport overdraw is NOT known to be lower than the harness's.** A PZ
  cell is dense (8 z-levels, floor+walls+objects, back-to-front blended), so
  built-up areas could genuinely approach the harness's overdraw. The 200k PASS
  is the *count* C1 predicted, but the harness cannot tell us the real
  *overdraw* — only a real cell decoded into instances can. That measurement
  belongs to early C3.

## What C3 needs to know

The bound is **fragments-on-screen**, so overdraw control is the real render
work, not draw-call batching. Two consequences, both promoting things C1 listed
as optional to load-bearing:

1. **Opaque pre-pass.** Most ground/floor tiles are opaque. Draw them
   front-to-back with depth write in a first pass; only genuinely translucent
   sprites need the blended back-to-front pass. This is the biggest available
   overdraw lever (the blend itself is only 12%, but *skipping shading of
   occluded opaque fragments* via early-Z is where the win is).
2. **Tier-2 LOD crossover is not polish.** It must fire before on-screen
   fragment count pushes fill past budget. The crossover zoom (C1 left it
   unmeasured) should be chosen against measured fill, not assumed at 1:32-1:64.

Instanced draw stands. Proceed with the `QOpenGLWidget`/`QOpenGLWindow` shell on
that path. First C3 measurement: decode one real dense cell to instances and
read its actual on-screen fragment count at 1:1.

## Files

- created `harness/instance_harness.cpp`
- created `harness/CMakeLists.txt` (standalone; not wired into the top-level
  build — throwaway, links no project code)
- created this findings file

## Commands worth keeping

```fish
cmake -S harness -B harness/build -G Ninja
cmake --build harness/build
./harness/build/instance_harness 500000 18 1   # baseline stress
./harness/build/instance_harness 200000 18 1   # C1's real 1:1 load -> PASS
./harness/build/instance_harness 500000 3 1    # near-zero fill -> per-instance floor
```

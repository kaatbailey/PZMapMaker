# C1 — Architecture Decision
## pzformat / PZMapMaker
**Date:** 2026-08-21  
**Status:** DECIDED — ticking C1 `[x]` in CHUNKS.md

---

## 1. Decisions

### 1.1 UI Toolkit: Qt6 Widgets + OpenGL 4.6

**Choice:** Qt6 Widgets for the application shell, `QOpenGLWidget` for the map viewport.

**Rationale (UI toolkit fit, not performance):** Qt6 is what TileZed, Qt Creator, and QGIS use. It is native on KDE/Wayland without XWayland workarounds. `QDockWidget`/`QTreeView`/`QAction` give us the panel layout TileZed users already know at zero design cost. `QFileDialog` reads from the user's PZ install without any extra work. The performance argument for leaving the JVM was *not measured* — no interactive viewport existed — and is not the basis for this decision. UI toolkit maturity and KDE-native feel are the basis.

**OpenGL version:** 4.6 core profile (confirmed: NVIDIA 610.43.03 on Garuda, `glxinfo` output 2026-08-21). Design target: 4.5 core as the floor, so it runs on Mesa/AMD for other users. All features we need (DSA, `GL_TEXTURE_2D_ARRAY`, persistent-mapped buffers, `glMultiDrawElementsIndirect`, SSBOs) are available from 4.5.

**What would make this wrong:**
- Qt6 becomes paid-only for the module we need. Mitigation: stick to Qt6 Core + Widgets + OpenGL, all LGPLv3. Never use Qt Charts or Qt Data Visualization (GPLv3-only for open-source users).
- Wayland support in Qt6 regresses badly. Fallback: XWayland. Not a blocker.
- We need a web-embeddable viewer or cross-compile to Windows/macOS as a priority. Qt is the right answer for the first; for the second it's still fine. Not currently a requirement.

---

### 1.2 Render Architecture: Instanced Draw + Two-Tier LOD

#### Frame budget (predict before building)

At 1:1 zoom on 2560×1440:
- Visible area ≈ 80×80 world tiles (assuming ~18px per tile at 1:1)
- 8 levels × 80 × 80 ≈ **51,200 squares**, each carrying ≈ 3–4 sprite layers
- ≈ **150–200k sprite instances** per frame

At full-world zoom (Knox County, ~1,300 cells, each 256×256 tiles):
- 1,300 × 256 × 256 ≈ **85 million squares**. Not drawable per-instance at any frame rate.
- Even at 4065 Muldraugh cells: ~266 million squares. A naïve per-sprite draw dies here immediately.

**The falsifier for the instanced approach:** build a throwaway harness that pushes 500k instances of a single atlas at 1440p and measure frame time. If it is not comfortably under 4 ms (leaving 12 ms for the rest of the frame at 60 fps), instanced draw is wrong and we find out in one day instead of after C3 is half-built. Run this harness *before writing the main viewport*.

#### Tier 1 — Working viewport (≤ a few hundred cells visible)

**One draw call per chunk-batch, not per sprite.** Every sprite becomes an instance:
```
struct SpriteInstance {
    vec2  world_pos;
    uint  tile_id;
    uint8 level;
};
```
Tile geometry (trimmed offsets, 1× vs 2× atlas half-scale) lives in an SSBO indexed by `tile_id`, so a trimmed sprite costs nothing extra per instance. The batch for all visible chunks at one level is one `glMultiDrawElementsIndirect` call.

**Atlas storage:** `GL_TEXTURE_2D_ARRAY`, one array per atlas page size (PZ ships mixed 1×/2× sheets). Bindless textures are available on GL 4.6 but array textures are simpler, portable to 4.5, and sufficient. The `.pack` page PNG is already decoded-to-RGBA by the atlas loader (when that is ported); load it once into a texture layer and keep the CPU copy only until committed to GL.

**Painter order:** isometric back-to-front per z-level; depth testing does not save us with alpha-blended sprites. Sort by `(level, y + x)` at batch-build time, not per frame. Build the sorted instance buffer once per dirty chunk; dirty-flag on `CellEditor` edits.

**Chunk streaming:** `MappedFile` already mmap-s a `.lotpack`; the viewport decodes chunks on demand on a worker context using `QOpenGLContext::shareContext`. Only the chunks whose screen footprint intersects the viewport frustum are decoded. Cache decoded chunks in an LRU keyed by `(cell, cx, cy, z)`.

**What would make Tier 1 wrong:**
- The 500k-instance harness exceeds 4 ms on the target hardware. Fall back to a chunked draw with merged geometry per chunk — more CPU work but still GPU-bound and not per-sprite.
- `QOpenGLWidget` on Wayland introduces a copy path that kills frame time. Use `QOpenGLWindow` wrapped in a `QWidget::createWindowContainer()` instead.

#### Tier 2 — Far-out LOD (full-map overview)

At full-map zoom, render a pre-baked raster tile per cell, not per sprite. Each cell tile is a 256×256 (or 128×128) RGBA raster, rendered once on a worker thread when the cell is first loaded or dirtied, cached to disk in a `<mapdir>/.pzmm/lod/` sidecar. The viewport at far zoom draws a grid of these rasters — one texture per cell, one quad per cell, trivially batched.

The crossover zoom (below which Tier 1 takes over, above which Tier 2 dominates) is the one number to measure rather than assume. Expected: somewhere around 1:32 to 1:64, where a cell is ~8×8 screen pixels.

**What would make Tier 2 wrong:**
- The LOD raster is too stale after edits (player edits one square, whole cell raster re-bakes). Mitigation: re-bake only the 8×8 chunk containing the edited square, not the whole cell. The `CellEditor` journal provides exactly the set of dirty chunks.
- Disk cache gets stale silently. Mitigation: hash the cell's `.lotpack` modification time into the LOD filename; if mismatched, re-bake before display.

---

### 1.3 Working Store: Map directory + in-memory CellData + per-session undo

**Choice:** The game's own format (`.lotpack` / `.lotheader`) *is* the working store. No intermediate database.

**Rationale:** The format layer round-trips byte-identically. `CellData::load` + `CellEditor` give us a clean in-memory model. Writing back is `writeLotPack()` + `writeLotHeader()` — already verified against the Java tree. Adding SQLite or a proprietary chunked format would be a translation layer that adds two more read/write paths and two more places for drift to enter. The game files are already the authoritative format; keeping them as the live format means "save" is just a flush, and the map is always in a state the game can load directly. No export step.

**Undo across sessions:** `CellEditor`'s journal does not persist. A session's undo history is lost on close. This matches the official tools (TileZed has no persistent undo). Persistent undo is a C2+ feature if there is demand; it is not a day-one requirement.

**What would make this wrong:**
- A large map has enough cells that loading them all into `CellData` objects simultaneously exhausts RAM. Mitigation already in the design: the viewport only loads the cells whose bounding boxes intersect the view frustum. An LRU evicts cells from memory when the cache limit is reached. `writeLotPack` flushes on eviction. This is the streaming model `MappedFile` was built for.
- Concurrent access from the game while the editor has a cell loaded corrupts the file. Not a concern: local single-user, game and editor are not run simultaneously. (If they were, the solution is lock files, not a database.)

---

### 1.4 Undo Persistence

**Choice:** In-memory only, per session.

`CellEditor` already has a grouped journal that restores byte-identical output. It does not persist across sessions. Acceptable for the same reason TileZed's doesn't: the map files themselves are the record of intent, and "undo past save" is a rarely-needed edge case that would require serialising the full before/after tile stack for every change across potentially thousands of squares.

If persistent undo is ever needed: serialise `CellEditor::Edit` to a per-cell sidecar file in `<mapdir>/.pzmm/undo/`. The format is trivial (x, y, z, oldTiles[], newTiles[]). Not building it now.

---

### 1.5 Process Boundary

**Choice:** One process.

The library layer (`pzformat`) is a static library linked into the Qt application. No subprocess, no IPC, no socket. The rationale from C1's constraints: local, single-user, no multi-user requirement. A second process would buy sandboxing (a crash in the renderer doesn't kill the editor) but the tooling overhead is not justified by the threat model. If the renderer crashes it takes the editor with it — acceptable.

**What would make this wrong:**
- We add Python scripting for automation (plausible — the Java tree has Python analysis harnesses). Solution then: a subprocess with a simple JSON-over-stdin/stdout protocol. Not built now; the interface is clear enough that adding it later costs one afternoon.

---

## 2. Alternatives considered and ruled out

| Alternative | Ruled out because |
|---|---|
| Spring Boot browser canvas | Multi-user rationale was the only argument; that was ruled out 2026-08-08. Adds a JS layer with no benefit for a local desktop tool. |
| LWJGL / libGDX | Java-only. Port was already decided. |
| Electron | Same issue as Spring Boot plus heavy. |
| Dear ImGui | Great for tools, but does not give us a native-looking Qt Creator-style panel layout. Would need significant hand-rolling for dock panels, tree views, property editors. |
| SDL2 + custom GL shell | No widget toolkit at all; we would be reimplementing Qt's panel layout. More work than Qt for worse result. |
| SQLite working store | Extra translation layer; format layer already round-trips byte-identical. Zero benefit over flushing to the game's own format. |
| Persistent undo | Serialises large before/after tile stacks for every edit. Complexity cost not justified by demand. Revisit if users ask. |

---

## 3. What this document does not decide

- **Tile palette UI** — how the user browses and selects tiles from the loaded atlas. That is C2 scope.
- **Room/building editing UI** — how rooms are drawn and their metadata edited. C2.
- **The LOD crossover zoom value** — measured when the harness is built, not decided now.
- **Whether `.pzmm/` sidecars go in the map dir or a user config dir** — C2.
- **Python scripting** — not now; noted as addable later.

---

## 4. Definition of done for C1

This document. C1 is `[x]` when this is committed to the repo and the CHUNKS entry is updated.

**C2 is now unblocked.** C3 is unblocked once C2's working store is in place.

The one remaining falsification action before C3 starts: **build the 500k-instance harness** (see §1.2) and record the frame time. Do not start the `QOpenGLWidget` shell until this number is known.

---

## 5. §1.2 gate — RESOLVED and threshold CORRECTED (2026-08-22)

The §1.2 falsifier was run. Do not re-read §1.2's "500k <4ms or instanced draw
is wrong" as a live gate — it was mis-specified. This section corrects it in
place of an edit (STATE governance: a wrong belief moves to a correction, it is
not silently rewritten). Full data: harness/FINDINGS_harness_2026-08-22.md,
folded into STATE.

**Result: instanced draw is CONFIRMED viable.** On the target RTX 3070 Ti (GL
4.5 core), one glDrawElementsInstanced showed no draw-call cliff (500k->1M
linear), and C1's actual predicted 1:1 load (200k instances) rendered in
1.9ms — well under the 4ms budget.

**The correction:** the 500k-<4ms threshold conflated two independent variables,
instance count and fragment/overdraw count, into one pass/fail number. The 500k
FAIL at 18px (4.81ms) is a 44x-overdraw fill-rate result from the harness
scattering sprites across the whole surface — not an instancing limit. Measured
facts: instance count is cheap (~1.3ns/instance floor; 500k at near-zero fill =
0.655ms); the bound is fragments on screen (7.3x swing from sprite size alone at
fixed count); blending is only ~12% of the cost.

**The §1.2 fallback ("chunked merged geometry") is retracted as the remedy.** It
reduces draw-call and vertex work; this workload is fragment-bound, so it would
have paid complexity for no gain. The harness's value was ruling this out before
it was built.

**What this changes for C3.** The render bottleneck is overdraw, so overdraw
control is the primary render work:
- Add an opaque pre-pass (front-to-back, depth write) for opaque ground/floor
  tiles; reserve the blended back-to-front pass for translucent sprites. This is
  the main fill lever and should be in C3 from the start.
- The Tier-2 LOD crossover (§1.2) is load-bearing, not polish: it must fire
  before on-screen fragment count exceeds the fill budget. Choose the crossover
  against measured fill, not the assumed 1:32-1:64.
- One overdraw number the harness could not supply: real dense-cell overdraw at
  1:1. A PZ cell is 8 z-levels of blended sprites and could approach the
  harness's overdraw in built-up areas. First C3 measurement: decode one real
  dense cell to instances and read its on-screen fragment count.

Everything else in §1 (Qt6, the game-format working store, in-memory undo, one
process) stands unchanged.

---

## 6. §1.2 QOpenGLWindow choice RETRACTED — use QOpenGLWidget (2026-08-23)

§1.2 line 60 said: "QOpenGLWidget on Wayland introduces a copy path that kills
frame time. Use QOpenGLWindow wrapped in a QWidget::createWindowContainer()
instead." This was an unmeasured assumption and it is now falsified for the
target platform.

On Garuda/KDE/Wayland, the QOpenGLWindow-in-createWindowContainer path rendered
NOTHING VISIBLE during C3 step 2. The draws executed — timed via
GL_TIME_ELAPSED, zero glGetError, geometry projected on-screen per a CPU-side
check — but glReadPixels of the draw target (draw_fbo=0) returned all-zero
including the clear colour. The container's native child surface and the GL
context's surface diverged; nothing drawn was ever presented.

Switching MapView from QOpenGLWindow to QOpenGLWidget fixed it immediately:
draw_fbo became Qt's managed non-zero FBO, readback returned the drawn pixels,
and the cell rendered. Measured draw cost for the whole cell fit to a ~940px
window: ~0.4ms — the "copy path that kills frame time" is not killing anything
at this workload.

**Decision:** C3 uses QOpenGLWidget. The §1.2 QOpenGLWindow guidance is retracted.
If the copy-path cost ever becomes a real problem at higher zoom or resolution,
it must be re-evaluated with a measurement, not an assumption — the same
discipline that turned the §1.2 500k gate from a guess into a finding.

Everything else in §1 stands.

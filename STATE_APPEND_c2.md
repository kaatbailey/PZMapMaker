<!-- APPEND to end of STATE.md. Append-only; nothing above changes. -->

---

### C1 DONE + C2 UNDERWAY — application layer begun (2026-08-21)

The port is complete (14 library units, all verified byte-identical against the
Java tree). The application layer has now started.

**C1 — architecture decision: DONE.** C1_ARCHITECTURE.md committed. Five
decisions, each with a falsifier: Qt6 Widgets + OpenGL 4.6 (UI-toolkit fit, not
measured performance); instanced draw + two-tier LOD with a MANDATORY 500k-
instance-at-1440p harness before any viewport code; the game's own
.lotpack/.lotheader files ARE the working store (no DB, flush on save, LRU
evict); in-memory per-session undo; one process. CHUNKS C1 ticked [x].

**C2 — working store + first UI slice: DONE and running on real data.**

MapProject (library, Qt-free, 36 tests):
- Enumerates cells from a map dir; a cell counts only if BOTH X_Y.lotheader and
  world_X_Y.lotpack exist (orphan files ignored).
- LRU cache with a cap; clean cells evict LRU-first, DIRTY CELLS ARE NEVER
  EVICTED (evicting one would silently drop unsaved edits).
- Atomic save: write to <file>.tmp then rename, so a crash mid-write cannot
  corrupt the existing map file (C2 "crash safety").
- Verified: edit through CellEditor → markDirty → save → reopen from scratch →
  edit present on disk. saveAll flushes every dirty cell.

MainWindow (Qt6 app):
- File → Open Map… (Qt's own dialog with DontUseNativeDialog — the KDE Wayland
  portal fails silently otherwise), cell-list dock, load-on-click showing room
  count / non-empty squares / level range, status bar with resident+dirty count.
- Recent Maps: File → Open Recent, last 10 map dirs, QSettings-persisted to
  ~/.config/PZMapMaker, de-duped, most-recent-first, with Clear Recent.
- menuBar()->setNativeMenuBar(false) forces the menu into the window; a toolbar
  (Open Map…/Save Cell) backs it up. Both needed on KDE, which otherwise hoists
  or hides the menu bar.
- VERIFIED on real Muldraugh: opened the map dir, cell list populated (0_18…
  4065 cells), clicked cells and saw correct room counts on building cells,
  16384 non-empty squares on wilderness cells (128×128, correct).

Library stays Qt-free: libpzformat.a has zero Qt. Only the app target links Qt6.
CMake uses find_package(Qt6 ... QUIET) so library + 10 test suites build with or
without Qt; the app builds when Qt is present.

**C3 — viewport: UNBLOCKED but GATED.** Do not write the QOpenGLWidget shell
until the 500k-instance-at-1440p harness is built and confirmed <4ms (C1 §1.2).
The one falsification action outstanding before rendering.

**Remaining C2 polish:** cell-search/jump box (4065 cells is a lot to scroll),
a dirty marker in the cell list, and a close-with-unsaved-changes guard on the
main window (confirmDiscardIfDirty exists and is used on open; wire it to
closeEvent too).

### Build/run notes (bit us, recorded so they don't again)
- Files copied to disk sometimes land EMPTY (main.cpp, .gitignore both hit this).
  ALWAYS `wc -l` a dropped file before building. Reliable in-place write:
  fish `cat > file <<'END_OF_FILE' … END_OF_FILE`.
- Stale build: if a rebuilt binary shows old behaviour, check the RIGHT file
  changed (`grep -c "old string" file`), then `rm -rf build && cmake -S . -B
  build -G Ninja && cmake --build build`.
- The binary is build/pzmapmaker; fish needs `./build/pzmapmaker` (the ./).
- Muldraugh map dir on this machine:
  ~/.local/share/Steam/steamapps/common/ProjectZomboid/projectzomboid/media/maps/Muldraugh, KY
  media dir (for TileIndex/atlas): .../projectzomboid/media

### Handoff — where the next session picks up
Port: DONE. C1: DONE. C2: working store + open/browse/load/recent DONE and
verified on real data. Next options, in rough priority:
1. C2 polish: cell-search box, dirty marker, close guard (small, high comfort).
2. The 500k-instance GL harness (the gate C3 waits on).
3. SpriteJoin — the properties-but-no-pixels 6th validation rule; needs only
   SpriteNames, not the deferred atlas pixels.
Then C3 (viewport) once the harness passes. The library is frozen and verified;
all remaining work is application construction on top of it.

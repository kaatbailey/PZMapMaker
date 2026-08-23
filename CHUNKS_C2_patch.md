<!-- Apply to CHUNKS.md — update the C-track status rows. Replace the C2 and C3
     rows shown below. C2 is now underway (working store + first UI slice done);
     C3 is unblocked but gated on the 500k-instance harness. -->

REPLACE:
| `[!]` | **C2** Working store and project format | C1 | Blocked |
| `[!]` | **C3** Interactive viewport | C1, C2 | Blocked |

WITH:
| `[~]` | **C2** Working store and project format | C1 | **Underway.** MapProject model (enumerate, LRU cache protecting dirty cells, atomic temp+rename save, edit→save→reopen no loss) — 36 tests. Qt6 MainWindow: open map, cell-list dock, load-on-click, Recent Maps (QSettings). Remaining: cell-search box, dirty marker in list, close-with-unsaved guard. |
| `[ ]` | **C3** Interactive viewport | C1, C2 | **Unblocked, gated.** Before writing the QOpenGLWidget shell, build the 500k-instance-at-1440p harness and confirm <4ms (C1 §1.2). Do not start the viewport until that number is known. |

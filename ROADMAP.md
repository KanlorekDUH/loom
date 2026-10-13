# LOOM — roadmap

Planned work, one coherent release at a time. Each version is committed and
tagged so any of them can be checked out and built:

```bash
git checkout v0.0.7 && make run
```

---

## Shipped

### v0.0.7-hotfix
- **Wires keep the same shape at every zoom.** The bezier's minimum handle
  distance was a fixed screen size, so curve angles drifted as you zoomed;
  it now scales with zoom like the endpoints do.
- **The floating `+$X` sale text scales with zoom** like everything else, and
  drops out at low zoom with the rest of the small text.
- **Nodes can't be dropped on top of one another** — a drag simply stops when
  the target cells are occupied.
- **Shift + drag on empty grid = drag-buy**, laying a run of nodes side by
  side and skipping cells that are already taken.

### v0.0.7 — camera, node variety & zoom-out rendering
- **WASD camera panning** (freed up by v0.0.6). Drag-panning still works.
- **Nodes of different sizes.** MINER / SMELTER / BROKER are 4×2, SELLER 3×2,
  FORGE 5×2, SILO 2×3, BOOSTER 2×2. Size comes from the node type, so the save
  format needed no migration.
- **Zoomed-out rendering fixed.** Detail was clamped to a floor of 0.4 while
  blocks shrink with zoom, so at 20% node dots, item dots and text were about
  **twice** their correct size — connector dots overlapped and labels
  overflowed. Detail now scales proportionally, and level-of-detail drops the
  throughput text, then the label, then the buffer dots below set zoom
  thresholds, keeping a minimum dot size so wires still read as connected.
- **Node inner grid** — faint cell divisions inside each node, a touch brighter
  than its face, so a node reads as the block of grid cells it occupies.
  Fades out when zoomed out.

### v0.0.6 — fixes & tweaks
- Removed the keyboard node-placement shortcuts (`Q W E R T Y U…`); placing is
  mouse-only, which also frees **W A S D** for the camera later.
- HUD: dropped the `NODES / LINKS` counters, kept the `ZOOM` readout.
- Minimum zoom lowered from 50% to **20%**; node dots, item dots and text now
  scale with zoom so a far-out board still reads.
- **Moved `save.dat` and `settings.cfg` into the per-user data folder**
  (`~/.local/share/loom/`), with a one-time migration from the old
  beside-the-executable location. Build cleanups can no longer touch them.

---

## Planned

### v0.0.8 — rewiring
- **Drag off a connected input** to detach that link. Drop it on **another
  input** to re-route, or release over nothing to **disconnect**.

### v0.0.9 — quests
- Objectives with rewards, and the QUESTS panel that will sit top-left in the
  v0.1.0 layout.

### v0.1.0 — UI overhaul (milestone)
A more in-game-focussed layout, with the menu restyled to match:

```
┌──────────────────────────────────────────────────────────┐
│ QUESTS        [ research | node scene ]        x$    xRP  │
├──────────────────────────────────────────────────────────┤
│                                                          │
│              ( the board  OR  the research tree )        │
│                                                          │
├──────────────────────────────────────────────────────────┤
│   [◻][◻][◻][◻] │ [◻]          ← nodes divided by types    │
└──────────────────────────────────────────────────────────┘
```

- **Top bar:** QUESTS (left) · a **research / node scene** view switcher
  (centre) · **$ and RP** (right).
- **Bottom bar:** the node palette, grouped by type — replacing today's tabs.
- **View switcher** swaps the whole main area between the board and the
  research tree.
- **Research as a tree:** connected nodes, tiered, with a hover tooltip
  describing what each unlock grants.

Still to pin down: whether board nodes also become circles, whether the view
switcher fully replaces the bottom tabs, and how deep the tree goes.

---

## Backlog (unscheduled ideas)

- **Node types from the depth pass**: splitter (1→2), merger (2→1), and an
  assembler that needs two different inputs.
- **Per-node upgrades** — spend on one machine rather than a global multiplier.
- **Quality of life**: undo (`Ctrl+Z`), box-select and move a group, hover
  tooltips on nodes, minimap, zoom-to-fit.
- **Prestige / rebirth** with a permanent multiplier.
- **More research tiers** and additional item tiers beyond plates.

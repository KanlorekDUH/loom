# LOOM — roadmap

Planned work, one coherent release at a time. Each version is committed and
tagged so any of them can be checked out and built:

```bash
git checkout v0.0.7 && make run
```

---

## Shipped

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

### v0.0.7 — camera & node variety
- **WASD camera panning** (freed up by v0.0.6).
- **Nodes of different sizes.** Size currently comes from the node type, and
  the save format stores only `col`/`row`, so this needs **no save migration** —
  but it touches all geometry (positions, hit-testing, wire anchors).
  Sizes to be chosen to leave room for wider machines later.

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

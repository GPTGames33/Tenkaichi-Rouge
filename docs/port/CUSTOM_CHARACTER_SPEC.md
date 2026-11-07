# Custom Character — design spec

Your own fighter creator for Budokai Tenkaichi. It's inspired by the feature set of the "Z Warriors" WIP video
(Tenkaichi 3 Decomp channel), but it's our own build: our own screens, names, layout and code. No assets or
code are taken from that project.

## Where it lives

- A new **Custom Character** entry in the main menu, drawn with the game's own menu art: the same panels,
  font, button prompts and sounds as the existing menus, so it looks like it shipped with the game.
- Saved fighters appear in **character select** in their own row/page, with a portrait, and play in every
  normal battle mode (offline at first; online once both players have the same fighter file).

## Screens (in order)

1. **My Fighters** — a list of saved fighters: New, Edit, Copy, Delete. Each one shows its portrait and name.
2. **Base** — a grid of the whole roster. The base character provides the skeleton, animations and default
   look. A 3D preview stands on the right and turns with the right stick.
3. **Look**, with tabs (L1/R1):
   - **Parts**: hair, head, face, torso, arms, legs, boots, gloves, accessories. For each slot, pick a
     character from the roster to borrow that piece from, or "Base's own". Pieces can be layered on top of
     each other (one press adds another layer).
   - **Build**: sliders for torso, arm and leg size, plus overall height.
   - **Fine tune** (advanced): per-bone stretch, move and turn for one part, e.g. a single hair strand.
   - **Colours**: pick a texture region (hair, skin, outfit pieces), then a colour, with buttons for
     "original" and "reset all".
4. **Moves** (tabs):
   - **Fighting**: melee/rush style, finishers, stats and ki, ki blasts and aura. Each is taken from any
     roster character.
   - **Blast 1** (2 slots), **Blast 2** (2 slots), **Ultimate** (1 slot). Each slot takes a technique from any
     character, with an optional recolour of its effects and an optional custom name.
5. **Transformations** (not in the video, our addition):
   - Up to 4 forms. Each form is a full look (its own Parts/Colours) plus its own Moves, aura and stats,
     either borrowed from an existing transformation or built from scratch.
   - Per form: ki cost and the transform/revert input, the same as the game's own transformations.
   - A cutscene/voice borrowed from an existing transformation (optional).
6. **Name & save** — an on-screen keyboard (the physical keyboard works too) for the fighter's name and title.
   It saves to its own file. A portrait is made from the preview.

## Controls (shown in the bottom bar on every screen)

Cross: change/pick · Circle: back · Square: default · Triangle: rename · SELECT: lock/colour ·
L1/R1: tabs · START: save · Right stick: turn the preview · L2/R2: zoom.

## Data

One file per fighter (JSON), so it's easy to share:
`{ name, title, base, parts[], build{}, bones[], colours[], moves{}, forms[] }`.
Everything in it points at existing game data by character id and part id. No game data is copied into the
file, so fighter files can be shared without sharing the game.

Each costume is built on its own (port/tools/custom_fighter.py `costume_spec`): the top level holds costume 1's
look, `"costume_edits": [null, {...}, ...]` holds costume n + 1's own look in item n (item 0 is unused; null, `{}` or
a missing item: the costume wears the top level's look). A costume's value of a look key K is its item's K when the
item has it, else the top level's: `parts`, `body`, `colours`, `paint` (keys `"<costume>:<texture>"`, and
`"<costume>:<texture>@<entry>"` for an expression's own texture: 13 the blink frames, 14..17 the mouth textures),
`extra_layers` (`[{chara, costume, piece, adjust}]`: any piece of any fighter stacked on top, "MORE LAYERS"), and
the other tools' keys (`shading`, `stance_pose`). The game's creator (port/src/gs/creator_ui.cpp) reads them through
`edit_view()` and writes them through `edit_spec()` (`set_edit_costume(n)`: the costume edited and shown); the first
write to a costume without a look of its own copies costume 1's.

The workshop's keys (port/tools/creator/mix.py `mix`): a layer's `adjust` and the body's `body` both take
`"nodes": {"<node>": {stretch, size, move, turn, shown}}` and `"copies": [{node, stretch, size, move, turn, shown}]`
(a strand duplicated: a new record beside it); a layer's adjust also `shown: false` (the whole layer taken off); the
body also `"regions": {"hair" | "head" | "torso" | ...: {...}}` (its own nodes of a piece at once), `"build"`, and
`"patch": false` (no strips or caps closing the gaps between pieces).

## Build phases (each one is playable before the next starts)

1. Load character files from a mods folder instead of the disc (the foundation).
2. A Custom Character entry in the main menu, plus My Fighters and Name & save. Saved fighters show up in
   character select as a copy of their base.
3. Moves: borrow any technique, stats and aura. Rename techniques.
4. Transformations.
5. Look: whole-outfit and whole-head swaps, then colours.
6. Look: per-part mixing and layering, build sliders, then per-bone fine tune (the hardest part: this needs the
   character model format worked out).
7. Online: sync fighter files between the two players.

## Open questions

- Which game: BT4 (the recomp) or BT3 (the decomp). See the session notes.
- Which disc image is on this machine, and where.

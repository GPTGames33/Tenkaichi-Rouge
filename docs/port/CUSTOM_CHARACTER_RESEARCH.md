# Custom Character: research notes (BT3 decomp)

Source: Tenkaichi3Decomp @ 48d66c1 (2026-11-03). "Verified" means matching C; everything else is read from the code.

## Disc and files
- There are no file names, only ids. Ids 1..0xD47 are PZS3US1.AFS (index = id-1); ids from 0xD48 are
  PZS3US2.AFS (sound). `File_OpenById`. On PC: `gamedata/pzs3us1/NNNNN.bin`.
- PZS3US1 holds 3,399 entries, including 742 character packs (first word 0xFC = 252 entries; [2] = "pmdl" model).
- Per character: a 10-file block at `0x590 + chara*10`:
  - +0..3: model pack for costume 0..3
  - +4..7: damaged version of each
  - +8, +9: shared by every costume (animations and one other file; check which is which)
- Character ids run 0..0xA0 (161 characters); 0xA1..0xA4 are grid markers.
- Load job (`BtlChange_RequestChara` 0x1D61F8) already carries separate chara / costume / animChara /
  file+9 / voiceChara, so model, animations and voice can already come from different characters.
  But `BtlObj_Setup` derives `obj->chara` from the model's file id.

## What a model pack holds (bobj_a.c BtlObj_BindTables, verified; game entry n = part n-1 of the file)
| Entry | Contents |
|---|---|
| 0x01 | HUD face |
| 0x04..0x0B, 0x0D | mouth textures, eyes |
| 0x12 | general params (BtlParam; byte 3 colour preset / aura, +0x98.. transformations) |
| 0x13 | melee attack records |
| 0x14 | 13 ki-blast records of 0x34 bytes |
| 0x15 | movement |
| 0x16 | pre/post-fight talk table (per opponent) |
| 0x17 | hair / cloth chains (belong to the model) |
| 0x18 | Blast 2 x2 + Ultimate (struct of arrays [3]) |
| 0x19 | Blast 1 x2 (struct of arrays [2]) |
| 0x1B | AI parameters |
| 0x1C | camera cuts, 0x24 each (0..2 starts of slots 2..4, 3 Ultimate intro, 9 rush finish) |
| 0x1D..0x24 | the model's own motions (fusion, partners) |
| 0x2D..0x34 | move list text, 8 languages (UTF-16; the n-th '*' line is move slot n) |
| 0x35.. | lip sync (0x35 + line; +0x64 other language) |

Per move slot: its elements of 0x18/0x19, motions in +8 (Blast 1: 0x103+slot; Blast 2/Ultimate: 0x22 from
0x105+(slot-2)*0x22, Ultimate also 0x16B; motion N = entry N+1), its effect pack (+9 entry slot+1), its cut.
The whole-moveset swap was checked in the game (2026-11-04).

## Model
- Skeleton of 71 nodes (head = node 0x33). Motions are retargeted by height against a reference skeleton, so
  sharing animations looks possible.
- Posing (src/battle/bobj_a.c): BtlObjAnim_SamplePose fills each node's local rotation (layer 0, layer 1 mixed
  in, then the blend from the last pose: `fromRot`), BtlObjPose_GetNodeMtx adds the look-at rotations (node 2,
  0x2E, 0x2F, the jaw), BtlObjPose_CalcMatrices chains them (world = local * parent, row vectors); a fighter runs
  the passes itself (anim.manual), the menus' viewer (ChrView, object type 3, model motion 7) through
  BtlObj_UpdateAll. The Stance Maker's pose goes in at the end of SamplePose (`pose * motion`, before the blend),
  so the motions' own blends ease it in and out. Motion ids: 0/1 idle, 2..9 walking, 0xC..0x1A dashes, 0x1C..0x1E
  steps, 0x1F..0x22 jump, 0x23..0x25 rush dash, 0x26/0x27 flying idle, 0x2B..0x33 ascend / descend, 0x34..0x36 ki
  charge, 0xEC..0xFD guards and evasions, 0x18B the hop of a fighter that cannot fly.
- Textures are 8-bit with a palette (CLUT), so recolouring = editing palettes. The game already draws palette
  passes (`ObjDraw_AddClutPass`).
- Mesh format is not fully understood, and mixing parts (hair, head) between models is new work. This is
  the hardest part of the project.

## Transformations
- Param block +0x98[4] targets, +0x9C[4] cost, +0xA0[4] sequence, +0xA4[4] kind (read from the accessors,
  not matched). Input: R3 + direction.
- btl_act_j.c ~l.270 fills `chr->form` (animChara = voiceChara = chara): the point to inject a custom form.
- Grid form list: `MChrCell{id, formCount, form[7]}`, CharSel pack section 42 (menu_c_e.c ~l.671).

## Character select and menus
- CharSel (menu_c_e.c, `gCharSel` 0x3B38D4), 7-column grid. `ChrGrid_Build` is still INCLUDE_ASM (a C attempt
  sits in `#if 0`).
- The game's own "custom characters" = 14 SaveRec (base character + Z-items) at gSaveData+0x2D40.
- The port has its own screen mode 80 (`PortScreen_Main`, port_screen.c). New screens follow port_tbo.c
  with the `Ps_*` kit.
- The main menu has **one free label row, already used by Online**, so the Custom Character entry needs a
  new label row or a sub-menu.
- `ChrView_Show(chara, costume, 0)` is the game's own 3D viewer (Character Reference), usable as the preview.

## Loading new files
- port/src/plat_file.c: stage / song / `BT3_FILE_ALIAS` aliases, then `<root>/mods/<rel>` overrides any
  disc file.
- Added stages get ids past the archive end (70000..) through a manifest. plat_stages.c is the template for
  a `characters/` manifest. New character ids must not collide with the voice files at 0xBDA+.
- Store custom fighters in a port-side file or folder, not in the 0x4000-byte console save.

## Build order this suggests
1. Characters manifest + new ids + grid cells (plat_stages.c pattern). A custom fighter = a copy of a base pack.
2. Moves: assemble entries 0x12..0x19 from chosen packs into the custom pack.
3. Transformations: forms pointing at other custom/base packs.
4. Main menu entry + creator screen (mode 80, Ps_* kit, ChrView preview).
5. Colours (palettes), then whole-model swaps, then part mixing (mesh format work).

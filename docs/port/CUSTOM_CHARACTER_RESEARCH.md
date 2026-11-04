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

## What a model pack holds (bobj_a.c)
| Entry | Contents | Battle object field |
|---|---|---|
| 0x12 | general params (BtlParam / pparam_com) | +0x91C |
| 0x13 | melee attacks | +0x920 |
| 0x14 | 13 ki-blast records of 0x34 bytes | +0x924 |
| 0x15 | movement | +0x928 |
| 0x18 | Blast 2 ×2 + Ultimate | +0x92C |
| 0x19 | Blast 1 ×2 (skills) | +0x930 |
| 0x17 | secondary-motion chains (hair, cloth, tails) | |
| 0x1D..0x24 | the model's own animations | |
| 0x0D / 0x04.. | eyes / mouth textures | |

**Moves and stats live inside each costume's pack, so building a moveset means copying those entries from
other packs.** The readers (btl_tech_a/b.c) are fully matched C.

## Model
- Skeleton of 71 nodes (head = node 0x33). Motions are retargeted by height against a reference skeleton, so
  sharing animations looks possible.
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

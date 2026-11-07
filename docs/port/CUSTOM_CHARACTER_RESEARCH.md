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

## Shading: cel shading, glow, outline (read 2026-11-06 for the creator's SHADING screen)
From the code (src/sys/gfxm_d_c.c, gfxm_e_b.c, gfxm_a.c, src/battle/btl_obj.c) and checked on the disc's models
(Goku (End), Goku (End) SSJ3, Vegeta, Vegeta SSJ and others).

**Cel shading is a second layer, subtracted per batch.**
- VU1 program 0 draws every batch (one triangle strip of a mesh record) twice. The batch header holds two TEX0:
  - TEX0_1, the material texture: layer 0, GS context 1. It writes the object id into the frame's alpha.
  - TEX0_2, the shading ramp: layer 1, GS context 2. It reads the light's ramp picture at 0x3D40 (an 8-bit index
    gradient) through a 256-colour CLUT at 0x3D00 + the ramp's offset, with u = 0.5 + 0.5 * (N.L + light.w)
    (port/src/gs/shaders/vu0.vert).
- ObjDraw_SetLight sets ALPHA_2 = 0x62 with FIX 0x80, which is **Cd - Cs**, and FRAME_2 masks alpha. So a ramp's
  colours are what is *taken away* in shadow.
  - Goku's skin ramp is 0x3b4a52 on the dark side, then 0x2b373d, 0x1e262b, then 0 on the lit side. It subtracts more
    blue than red, so the shadows come out warm.
  - A ramp of zeros (the eyes) leaves the part unshaded.
- Ramps are stored as 16x16 PSMCT32 entries [clutFirst, clutFirst + clutCount) of the texture file (entry 0xC),
  4 blocks each, uploaded at 0x3D00 + tbpOfs.
  - ObjDraw_UploadTextures uploads them with TexFile_UploadRange in the usual light modes. Mode 0x80000 swaps in the
    light's own ramps (BtlObjLight_FindRes1).
  - There are at most 16 (0x3D40 is the light ramp).
  - Their alpha is always 0x80 and is never seen, because the blend uses FIX.
- The ramp is chosen per batch, so per mesh part. A model has 6 to 9 of them (skin, gi, belt, hair, boots...).
- There is no shade texture per material, no vertex colour and no second CLUT of the texture.

**The Super Saiyan glow is the object glow pass, per material slot.**
- TexEntry +0x2C is the material's alpha slot (0..14) and +0x2D its glow (0..0x7F).
- BtlObjMdl_InitAlphaSlots (at bind):
  - writes slot + row * 15 into the alpha of the first 16 CLUT entries. That is the id the frame's alpha carries
    (row = the object's BtlObjVis bit).
  - sets model->alpha[slot] = glow. The last texture with that slot wins.
  - header +0x54 texNoAlpha keeps its own alphas and glows 0.
- BtlObj_UpdateAlpha and BtlObj_UploadAlphaTable turn this into a 256-entry table by id. Colour modes override it:
  black gives 0xFF, the powered-up look gives 65 on every slot.
- ObjGlow_Draw writes table[id] into the frame's alpha. GfxPost_DrawGlow then shrinks the picture weighted by it,
  blurs it 10 times and adds it back. The glow is the pixel's own colour, bloomed.
- On the disc:
  - Non-SSJ models have 0 on almost every material (Goku (End): one 32x32 texture at 0x64).
  - SSJ models have 0x14..0x64 on nearly all (SSJ3 Goku's hair 0x50 / 0x46, Vegeta SSJ's hair locks 0x50).
- Model header flag 0x01000000 (on every SSJ model) only lights the model from the camera in the powered-up look. The
  colour preset (param byte 3) tints the whole palette in that look.
- So **a non-SSJ part glows when +0x2D of its textures is set**. This works per texture slot, not per node, so a
  texture shared with another part needs a copy with a slot of its own.

**The outline is a pass over the picture, not geometry.**
- There is no inverted hull and no outline layer of program 0.
- After the models, BtlObj_BeginDraw runs GfxPost_CopyAlphaToDepth, GfxAlphaKey_Draw, ObjOutline_Draw, then
  ObjGlow_Draw.
- ObjOutline_Draw:
  1. turns the id in the frame's alpha into a value: (id << 3) & 0xFF, with 0 -> 0x80 and 0xFF ("nothing") -> 0;
  2. subtracts the image shifted half a pixel down and a pixel right;
  3. subtracts 0x64 grey through the result.
- That gives a line wherever the id changes, on the side with the larger value: round the silhouette and between
  materials of different slots.
- It has one width and one darkness, with nothing per model or part. The port draws it natively
  (shaders/outline.frag).
- The head (node 0x33) is drawn last with alpha protected and takes the id under it.

**What the creator does with this** (port/tools/creator/shade.py, spec "shading"):
- **Shadow colour:** the part gets a ramp of its own. Every entry is the chosen colour, and its alpha is the original
  ramp's strength there. The alphas are odd and below 0x80, values the game never has. The port's renderer draws such
  a ramp as Cs * As + Cd * (1 - As) instead of Cd - Cs (gs_draw.c tint_ramp / GsGpu_DrawVu0). Subtraction alone can
  never make yellow hair's shadows blue.
- **Same shading as:** the part's batches take the other part's main ramp (TEX0_2's CBP). This is pure data.
- **Glow:** +0x2D of the part's textures (true = 0x50, as SSJ hair). This is pure data and works in any build.
- **Outline none:** +0x2E of the part's textures = 1, a byte the game never reads. BtlObjMdl_InitAlphaSlots tells the
  port (Port_OutlineSlot), and outline.frag draws no line on those ids nor toward them.
- **Parts** are groups of mesh records by node:
  - face, chest, waist, upper arms, forearms, hands, hips, shins, feet, tail, accessories;
  - hair: the head's extras, plus the head batches drawn with a texture that is mostly theirs.

Limits:
- 16 ramps. A shadow colour costs one per ramp the part uses; when they are full, the colour's first ramp is shared.
- The copies that glow and outline need are limited by 15 slots, 0x780 texture blocks and 0x40 palette blocks.
- The face texture is never copied, because its eye frames replace it.
- Everything is rendering only and carried by the package, so both players of an online fight see the same.

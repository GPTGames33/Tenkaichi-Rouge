# Adding stages (maps) to the port

The port can load **stages that are not on the disc**: a map is a `.unk` file (the stage model BT3 modders
release, the same one that is replaced under PCSX2). The port gives it one more cell in the stage grid,
serves its model (and a sound bank) to the game, and draws its name in the menu.

Everything is driven by a **manifest** (`<data>/stages/maps.txt`) that the port reads at start-up. No code
change and no environment variable is needed.


## Where they go (main line)

A folder **`stages`** next to the game, like `textures`. Drop a `.unk` file in and it is used: its name in the
menu is the file's name (`My_Song.unk` is shown as "My Song"). `$BT3_STAGES` names another folder.

- The list (`maps.txt` in that folder, `file|Name In The Menu` per line) is optional: it gives other names and an
  order. Files it does not mention follow, by name without regard to case (the same order on every machine).
- The name strip (`names.rgba`, made by the script below) is optional too: without one for an entry the overlay
  writes the name as text in the same place.
- The folder of that name inside the game's data folder, which is where the pull request put them and what the
  rest of this document describes, is still used when there is none next to the game. The script now writes to
  the folder next to the game (the repository root) unless `--data` names another place.
- Code: `port/src/plat_extras.c` (the folder, the scan), `plat_stages.c`, `plat_songs.c`.
- Tried with a real custom map (a downloaded `.unk`, dropped into `stages`; the user, 2026-10-07): "worked fine".
  Until then only a copy of a disc stage under a new id had been loaded this way.

## File ids and size of an added stage (2026-10-07)

- **A bug of release 0.1.8, fixed.** An added stage's files were asked for under ids that belong to the disc's
  own files. Its sound bank at 0x14E + stage was redirected to the first stage's bank; but 0x14E + 0x24 is 0x172,
  the one-screen MODEL of disc stage 1, so with one stage added a one-player fight on disc stage 1 was given a
  sound bank as its model (the second added stage did the same to stage 2, and so on). And its model at
  0x171 + stage is, from the fourth added stage on, the split-screen model of a disc stage. Found by reading the
  code (the pull request had removed the bank redirection in a later commit of which only the renderer part was
  taken); not reproduced in a run of 0.1.8.
- Now: an added stage's model has a file id of its own, 30000 + n past the start of the second archive
  (`PORT_ADDED_STAGE_FILE` in battle_load.c, served by plat_stages.c), and for its sound bank the first stage's
  is asked for by its real id (`BTL_STAGE_BANK`). No redirection touches a disc file.
- Checked with a stage added, on one screen (`BT3_TEST_SCREEN=0`) and with `BT3_TEST_STAGE`: disc stage 1 opens
  its own model `00369.bin` and bank `00334.bin`; the added stage opens its file and bank `00333.bin`; all fight.
- **Size.** The game loads a battle's stage into one buffer of 0x6CB800 bytes (7.1 MB) and nothing checked a
  file against it: a larger file overran the buffer and the game crashed seconds later in its heap. Now the
  buffer is as large as the largest added stage (`Port_StageBufSize`, up to 64 MB); one larger than the game's
  own comes from the port's memory, not the game's 28 MB heap (`Port_GameBigAlloc`; a 10 MB buffer in the heap
  left too little for a battle's pools). A stage file that does not fit is refused with a message. With no
  larger stage installed, and in an online session, sizes and memory are the game's own.
- Tried with stage 12's split-screen file rebuilt at 2, 3 and 4 times its triangles (63,000 to 126,500; 8.2 to
  11.8 MB), put in its place through `mods/` with `BT3_STAGE_BUF`: all load and play the recorded fight with the
  same fight values as the disc file at each of 12,325 blanks; a frame's work goes from 7.3 to 9.3 ms.
- `BT3_TEST_STAGE=<id>` / `BT3_TEST_SCREEN=<mode>`: every battle on that stage / screen mode (testing).

## The name in the game's own lettering (main line)

The game has no font for these names: every stage name of the disc is a picture. `port/src/gs/namefont.c` reads
those pictures from the stage select's own pack when the menu is set up (36 stage names in `gCharSel->tex[39]`,
24 song names in `tex[19]` and again in the lit colours in `tex[20]`; 512 x 256 sheets, 8-bit with a table),
cuts them into letters in memory and writes an added name with them. Nothing made from the game's art is stored
or shipped; the letters exist while the program runs, made from the player's own disc.

- Fill letters are cut from the pictures; the outline and the shadow are drawn again around the composed name
  (neighbouring outlines run into each other on the disc). Spacing is solved from the disc names' own gaps.
- Characters the disc's names have: most letters, `' - !`. None of `J Q X Y Z j q x`, no digits, no other
  punctuation. A letter missing in one size is taken from the other and resampled. A name with a character
  that exists nowhere is written as plain styled text instead, whole.
- A strip made by the script (`names.rgba`) still comes first.
- The tables of what each picture says are in namefont.c; a disc whose pictures do not have those letter counts
  (another edition or language) leaves the names as plain text.
- `BT3_NAMEFONT_TEST="Stage Name|Song Name" BT3_NAMEFONT_OUT=<folder>` writes test pictures and the coverage.
- Checked: every disc name put together again from the kept letters against its own picture (mean difference
  under 1 of 128); an added stage's name seen in the menu under Vulkan and once under OpenGL. Not seen: an added
  song's name in the menu, in either of its two states (the lit one while the music list is open).

> **How this differs from the pull request it came from** (main line, 2026-10-07):
> - Nothing is written to the save: added stages are appended after the game has applied the save's unlocks to its
>   own list, and no unlock bit is set for them. A save stays valid for a copy of the game without them.
> - In an online session (Tenkaichi Budokai Online) added stages are not offered: both players must have the same list.
> - With nothing installed the game is unchanged.
> - The disc's own stages keep their lighter split-screen model; only an added stage (one model) is loaded as it
>   is for split screen too.
> - The styled name over the menu is drawn by the Vulkan renderer only; under OpenGL the game's plain font is used.

---

## Quick start (script)

From the repository root:

```sh
# install a map (the name is the one shown in the menu)
port/tools/add_stage.py add "/path/My Map.unk" "My Map"

# list the installed ones (with their id)
port/tools/add_stage.py list

# remove one
port/tools/add_stage.py remove "My Map"

# regenerate only the name strip (after a font/style change)
port/tools/add_stage.py rebuild
```

`add` copies the `.unk` into `gamedata/stages/`, adds the line to the manifest and **regenerates the name
strip** (`names.rgba`) so the name comes out in the game's style.

Options:

| option | effect |
|---|---|
| `--data <dir>` | data folder (default `gamedata`, or `$BT3_DATA`) |
| `--font <ttf>` | font for the name strip (default `$BT3_STAGE_FONT`, or `port/tools/compacta.ttf`, or a system one) |
| `--no-strip` | only edit the manifest; leave the strip alone |

### The font of the names

The game draws its stage names as **pre-rendered images** (it does not use a font with glyphs). So that the
added maps look the same, the name strip is **generated** from a TTF. Without Pillow or without a font the
manifest still works (the maps appear and are playable) but the name is drawn with the game's own plain font.

Put the TTF at `port/tools/compacta.ttf` (or pass it with `--font`) for the reference look. The style
(cream→orange fill, brown outline, shadow) lives in the constants of `port/tools/add_stage.py`.

---

## Manual use

1. Copy the `.unk` into `gamedata/stages/`.
2. Add a line to `gamedata/stages/maps.txt`:
   ```
   my_map.unk|Name In The Menu
   ```
3. Regenerate the strip: `port/tools/add_stage.py rebuild`.

Every **non-comment** line (`#`) adds a map; the order in the file is the order of the ids and of the strip's
images.

---

## How it works

### Stage ids

The game has 36 stages on the disc, ids `0x00`–`0x23`. The added ones take ids **`0x24`, `0x25`, …** (the
unlock field `stageBits` is 64 bits, but an added stage is never looked up in it; the port takes 64, `0x24`–`0x63`). So that
those ids are selectable the port moves the grid's sentinels:

```
STGGRID_ID_LOCKED 0x24 -> 0x7E      STGGRID_ID_EMPTY 0x25 -> 0x7F   (PORT only; 0x3E, 0x3F until 2026-11-05)
```

and the grid becomes **6×N** (`stageRows = ceil(count/6)`), filling the last row with "empty" cells.

### Files the game asks for

For a stage of id `S` the game asks for (see `docs/systems/files_and_assets.md`):

| asset | id | where the added one comes from |
|---|---|---|
| model (single screen) | `0x171 + S` | **the map's `.unk`** |
| model (split screen) | `0x198 + S` | the *single* model (the port never uses the split) |
| sound bank | `0x14E + S` | the first stage's bank (borrowed) |
| menu thumbnail | `0x39D + S` | an existing thumbnail (high ids fall on the loading screens) |
| name | movie atlas | drawn by the port (see below) |

The port turns those requests into disc files through an **alias table** built by `port/src/plat_stages.c`
from the manifest; `port/src/plat_file.c` consults it in `open_rel` (and in `Port_FilePath`, the streamed
path).

**The id ranges are packed and overlap.** File ids are contiguous per asset type and the next range starts
right after the disc's 36, so:

* the added stages' *split* model (`0x198 + S`) falls on the **menu archives** (`baseFile = 0x1C1`) and the
  transitions (`0x1BF`, `0x1C0`). That is why the port **always loads the *single* model** in battle
  (`battle_load.c`, under `#ifdef PORT`): the *split* range is never asked for, so there is no collision. In
  split screen the full model is used.
* the thumbnail (`0x39D + S`, with `S >= 0x24`) falls on the loading screens (`LOAD_FILE_FIRST = 0x3C1`). So
  an existing thumbnail is reused (`menu_d.h`, `CHARSEL_STAGE_FILE_ID`).

### The name in the menu

The movie's name atlas only covers the disc's 36 stages. For the added ones the clip is hidden and the name
is drawn by the **port** (`ui.cpp`) as an **image** over the frame:

* `gamedata/stages/names.rgba`: a header `w`, `h`, `count` (u32) and then `count` images of `w×h` in RGBA,
  one per map in the manifest's order.
* The menu (`menu_c_e.c`) publishes each frame the clip's rectangle in game pixels (512×448) and which image;
  `gs_gpu.c` publishes the presented picture's rectangle (the letterbox, in window pixels); `ui.cpp` combines
  them and draws with Dear ImGui.

When the overlay is not available (no `names.rgba`, or the **OpenGL** back end), the menu falls back to
printing the name with the **game's own font** (the name is placed from `gPortStageNames`, in
`port/src/plat_stages.c`).

### Compatibility with the earlier prototype

For testing, the prototype's environment variables still work:

* `BT3_EXTRA_STAGES="0x24,0x25"` — used **only when there is no manifest**; adds ids without names.
* `BT3_FILE_ALIAS="pzs3us1/00404.bin=/path/kaio.unk;…"` — a manual alias by relative path.
* `BT3_STAGE_REPLACE="0x1b=0x24,…"` — replaces the id of an existing cell with another (without adding).

---

## Limits and notes

* **64 maps** at most (`0x24`–`0x63`; it was 26, `0x24`–`0x3D`, until the stage packages of 2026-11-05: see the end).
* The added stages' **sound bank** is the first stage's (borrowed). For their own, a free id would have to be
  given or an existing bank replaced.
* The model is loaded into the game's buffer (`BTL_STAGE_BUF_SIZE = 0x6CB800`, ~7.1 MB). The original models
  reach ~7.12 MB, so the mods' `.unk` files fit; a **larger** one would be truncated.
* The **thumbnail** (the grid's small preview) is a reused game image: it is not the map's. Generating it
  would need its own art.
* The name overlay runs on **Vulkan (SDL_GPU)**. On OpenGL the game font's text is used.

## Checking it

```sh
port/tools/add_stage.py list
BT3_STAGE_FONT=/path/to/compacta.ttf port/tools/add_stage.py rebuild
BT3_64=1 port/run.sh menu     # go to Duel -> Character/Stage Select
```

At start-up the port logs how many maps it read:

```
bt3: stages: 11 map(s) from gamedata/stages/maps.txt
bt3: stage-name overlay: 11 names, 1024x128 each
```

## Only 24 of 26 added stages were listed (2026-10-08)

Reported by a player who filled every slot. The stage select's list was 64 cells and the adding stopped at 60,
the last whole row of 6 that fits; with all 36 disc stages unlocked that left 24. The list is 96 cells now: 36 +
26 = 62 stages are 11 rows, 66 cells with the last row's padding. Checked with 28 files in the folder: 26 are
taken, the list has 62 entries ending in ids 0x3C and 0x3D (it had 60, ending in 0x3B), and a fight starts on
0x3C and on 0x3D (`BT3_TEST_STAGE`). Not looked at on screen: the eleventh row.

## The added stages' file numbers were disc files (2026-10-08)

`PORT_ADDED_STAGE_FILE` was 0xD48 + 30000 + n, described above as a place where the disc has nothing. It has:
the second archive holds 65,201 files, and 30000.. are the training mode's guide voice lines
(`TRAIN_VOICE_BASE_B`, 0x8278). With n stages added, the first n of those lines were answered with a stage's
file (releases 0.1.9 to 0.1.11). Found while listing the file ids for added characters. Now 70000 + n, past the
archive's end (`PORT_STAGE_FILE_INDEX` in plat_stages.c, the same number in battle_load.c). Checked: fights start
on the first and the last added stage and on a disc stage; the replay check on the Linux programs. Not checked:
the training guide's lines heard.

## Added stages were missing in team and DP battles (2026-10-10)

The team select (src/menu/menu_e_b.c) builds its own stage list, and only the duel's select had been given the
added stages (the same gap the added songs had). It has them now, the same way: appended after the disc's, the
last row padded, the reel's rows counted from the list, a borrowed icon and picture, the name drawn by the port.
Checked: builds, the replay check. NOT seen: the team select with added stages (no recorded session reaches it).

## Stage packages, and the stages of the BT4 mod (2026-11-05)

A **stage package** is a folder in the stages folder with a `stage.txt`, like a character package
(`port/src/plat_stages.c`). The loose `.unk` files (and `maps.txt`) keep their ids; the packages follow, by folder
name without regard to case, so the same folders give the same ids on every machine.

```
stages/BT4 16 Tournament of Power/
    stage.txt        name=Tournament of Power
                     destroyed=BT4 17 Tournament of Power - Climax   (another package's folder, or disc:<id>)
                     placement=31   (the disc stage whose places the technique scenes take; 0 without)
                     rules=31       (the stage id the fight's stage rules see; without it none of the special cases)
                     enabled=0      (leaves it out)
    model.unk        the one-screen model                         file 70000 + n of the second archive
    split.unk        the split-screen model (optional)            70200 + n   (without it: the model)
    bank.bin         its sound bank (optional)                    70300 + n   (without it: stage 0's)
    thumb.bin        the select's picture (optional)              70100 + n
    icon.bin         the reel's small picture (optional, 5504 bytes)
    name.rgba        its name picture (optional): u32 w, h, 1, then RGBA; drawn where the disc's names are
                     with the menu font "Original" (port/src/gs/ui.cpp, stage_title), else the name as before
    transition.bin   the scene shown while it is destroyed (optional) 70400 + n (without it: the game's choice)
```

Each key may name another file. 64 added stages at most (ids 0x24..0x63); the grid's markers moved to 0x7E
(locked) and 0x7F (empty), and the select's list holds 128 cells.

What the battle asks of an added stage now comes from its package (all under `#ifdef PORT`):

- `BtlStage_GetChangeTarget` (src/battle/stg_b.c): its `destroyed=` (it was the disc's rule on the model's kind,
  which a BT4 model does not keep in that form; see below).
- `BtlLoad_StepStageChange`: the package's own destruction scene, for the stage being left.
- `BtlSuper_GetStagePlacement` (src/battle/btl_tech_b.c): `placement=`. The table of a character's technique
  scenes has the disc's stages only; an added stage read past it before (any added stage, since 0.1.8).
- `BtlChar_IsStage4Or27` (ring-out), `BtlScene_IsStageFlagOn`, the stage bonuses of abilities
  (btl_char_status.c): `rules=`.
- `BtlLoad_StepInitial`: a fight set up on an added stage that is not installed (a replay from another machine,
  a package removed) is fought on stage 0 instead of asking for a file that is not there.
- Online nothing changes: added stages are not offered (the selects), so a session never names one.
- Saves: nothing of an added stage is written (as before).

`BT3_STAGE=<id>` is a second name for `BT3_TEST_STAGE`: with `BT3_DEMO=1` the attract-demo fight is on that stage.

### What the BT4 disc has (research)

BT4 (Team BT4 Beta 14 Rev2) is BT3's ELF patched in place plus code caves in its `BIN/DBZP.BIN` (loaded at
0x334C00). Its stages:

| BT3 | BT4 |
|---|---|
| 36 stage ids (0x23 the select's "random") | 99 slots (0x00..0x62), 81 with a model |
| bank 0x14E, model 0x171, split 0x198, picture 0x39D + stage | bank 0x590, model 0x5F3, split 0x656, picture 0x6B9 + stage (BtlLoad_StepStageReload / StepStageChange / StepInitial, ChrView_StepLoadStage) |
| change target: kind at env +0xC: 1 -> 0xF, 2 -> 3 | the signed byte at env +0xD (BtlStage_GetChangeTarget, 0x243470) |
| destruction scene 0x1BF (to stage 3), else 0x1C0 | 0x1BB + the byte at env +0xC (0..5; new code at 0x240940); BT4's 0x1BF is BT3's 0x1C0 |
| ambient loop 0x10BA4 + env id | 0x916 + env id: the same 35 streams, byte for byte |
| technique scene places: rec[stage * 5 + n] | stage % 35 (a cave at 0x3BEBFC) |
| select: 36 entries (section 39 of the select's pack), 38 icons (46), 9 name sheets (6) | 66 entries, 99 icons, 25 name sheets (100 names) |
| music 0x10B16 + n, 25 entries in the list | 0x816 + n, 236 entries: songs, not a part of any stage |

The model format is BT3's (25 parts; four of BT4's have 24). Every environment id (which picks the ambience
handler and its loop) is one of BT3's 0..34. The stage-id rules (ring-out on 4 and 27, the ability bonuses, the
scene flag) are not patched in BT4: they see BT4's ids, which `rules=` gives back (an id below 0x24).

Of BT4's 65 selectable stages, 31 are BT3's own: the same model but for the environment record's two bytes and
at most 81 bytes elsewhere (Wasteland, Rocky Area, Kami's Lookout - Noon, Dying Planet Namek, Mountain Road -
Noon / Evening, Islands, Planet - Night / Evening, Glacier, Ruined Earth, Outer Space (Planet Vegeta), Penguin
Village - Noon, Desert, King's Castle, Muscle Tower, Mount Paozu, City Ruins, World Tournament, Cell Games Arena
- Noon / Evening, Supreme Kai's World). The other 34 are new maps or changed versions (BT4 id, name):

0x17 Plains - Noon, 0x3A Plains - Night, 0x28 Kami's Lookout - Night, 0x02 Planet Namek (changed: 2,097 bytes of
its first part; turns into Agonizing Planet Namek), 0x48 Agonizing Planet Namek, 0x2D Mountain Road - Night,
0x08 Hyperbolic Time Chamber (changed), 0x06 Cell Game Arena - Ruins, 0x27 Otherworld Tournament, 0x12 Hell
(changed), 0x18 South City, 0x1E City Ruins - Future, 0x2E Archipelago - Noon, 0x0B Archipelago - Evening,
0x20 Champa's Ring, 0x1F Tournament of Power, 0x11 Tournament of Power - Climax, 0x03 Tournament of Power - Final,
0x0E Ice Continent, 0x10 Outer Space - Earth, 0x34 Penguin Village - Night, 0x3D Baba's Palace - Noon,
0x19 Baba's Palace - Night, 0x0C Kame House - Noon (changed: 1,874 bytes), 0x2F Kame House - Night,
0x0F Dying Planet, 0x50 Open Field, 0x4E North City - Ruins, 0x4B Red Ribbon Base Yard - Noon, 0x4F Red Ribbon
Base Yard - Storm, 0x4C Red Ribbon Base - Ruins, 0x2C West City, 0x2A Beerus' Planet, 0x39 Planet Tuffle.

Destruction chains BT4 keeps in its models: Planet Namek -> Agonizing Planet Namek -> Dying Planet Namek (BT3's
stage 3); Tournament of Power -> Climax -> Final; Red Ribbon Base Yard (Noon, Storm) -> Red Ribbon Base - Ruins;
Open Field and North City - Ruins -> Ruined Earth (BT3's 15); most of the others -> Dying Planet.

`port/tools/bt4/import_bt4_stages.py --out <folder>` writes the 34 as packages "BT4 NN <name>" in the BT4
select's order (295 MB); `--list` lists them. It needs the BT4 disc image only.

Checked (Windows build, the 34 packages in a scratch stages folder, `BT3_STAGES`):
- `BT3_GS=none BT3_DEMO=1 BT3_STAGE=0x24..0x45`: all 34 load (model and bank from their package) and the
  demo fight runs to its end on each. `BT3_STAGE=0x50` (not installed): fought on stage 0.
- `BT3_TEST_DESTROY=500` (testing: the stage destroyed every 500 stage updates): Tournament of Power -> Climax ->
  Final, each with its own destruction scene and bank; Planet Namek (BT4) -> Agonizing Planet Namek -> Dying Namek
  (the disc's); Plains, Kame House - Night, Baba's Palace -> Dying Planet.
- Windowed, `BT3_SHOT`: fights on Tournament of Power, Beerus' Planet, Planet Tuffle, Red Ribbon Base Yard -
  Storm, Champa's Ring drawn whole (sky, ground, lights, rain).
- `BT3_TEST_MODMGR=select` (testing: the versus character select at once, 1P vs CPU) with `BT3_PAD`: the stage
  select's list has 70 entries (the disc's 36 then 0x24..0x45), 12 rows; the BT4 stages' icons and pictures show.
- NOT seen: the BT4 name pictures over the select (the overlay is not in `BT3_SHOT`'s pictures), the team
  select with them, split screen with a package's split.unk (the demo does not set up split screen).

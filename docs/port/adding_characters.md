# Characters from outside the disc (work in progress, branch `charslots`)

For people who make packages: `character_packages.md` is the standard. This file is the working notes.

Started 2026-10-08. The aim: a `characters` folder next to the game, one folder per character holding a
community character pack as it comes, each taking a slot of its own instead of replacing a disc character.

## State

- Battle side: works. With `BT3_TEST_CHARA=165` the recorded two-player session plays to its end with the pack's
  character on side 0: its model, animations, effects and voice bank are read from the folder, the rest from the
  base character; seen on screen (the model, its moves, hits).
- The duel's character select: an added character has a cell after the disc's (in the last row's filler cells,
  then rows of its own), with its base character's chip, portrait and name for now, the folder's number of
  costumes, and no item sets. Chosen there with a recorded session (`BT3_TEST_CELL=1` starts the cursor on it),
  the fight is the same as with `BT3_TEST_CHARA`; seen on screen. With a character installed and not chosen,
  the recorded session ends as it always does.

## The pack format (seen in one pack, "Gokhan", 2026-10-08)

Eight files in the game's own format, the same layout as the disc's files of a character:

| file | the disc's file |
|---|---|
| `*_1p.pak` .. `*_4p.pak` | model of costume 1..4: `0x590 + id * 10 + 0..3` |
| `*_1p_dmg.pak` .. | damaged model: `+ 4..7` |
| `*_anm.pak`, `*_eff.pak` | `+ 8`, `+ 9` |
| `*_Voice_US.pak`, `*_Voice_JP.pak` | voice bank, `0xBDA + id` and `0xC7B + id` |

Inferred, not checked: which of the two bank ranges is which language (`_Voice_US` is taken as 0xBDA, the one
used when `SAVE_FLAG_VOICE` is clear); that the `_0_` in the names is a form number. The pack has no portrait,
no icon, no name, no parameter file and no streamed voice lines.

## What hangs on a character id (survey of the sources, 2026-10-08; verified by reading unless marked)

- The disc has 161 characters, 0x00..0xA0. 0xA1 random, 0xA2 locked, 0xA3 custom, 0xA4 empty are cells of the
  character select, in the disc's list (pack section 42) as well as in the code, and are used as indices too
  (the chip pack has 165 lists, the portrait files and the name lines are taken with the cell's id). The first
  id without a meaning is 0xA5.
- Limits on an id: below 0x100 (from there the battle loads an object), not 0xFF ("none" in the byte lists of
  transformations, fusions and links), at most 191 for a bit in `charaBits` (3 x 64). So 0xA5..0xBF, 27 ids.
- File ids, each rule packed for 161 characters (the id after character 160's is the next kind's first):

| rule | files | character 161 would get |
|---|---|---|
| `8 + id * 2 + side` | parameter block (0x1000 bytes) | the two common sound banks |
| `0x2F9 + id` | portrait (menus) | an unused file; from 164 the stage pictures |
| `0x590 + id * 10 + n` | models, animations, effects | the voice banks of characters 0..9 |
| `0xBDA + id`, `0xC7B + id` | voice bank, two languages | the other language's first; the first battle object |
| `0x8D4E + id * 100 + line`, `0xCC32 + ...` | streamed voice lines | the other set; the music |

  The second archive ends at index 65200; ids from 0x10BF9 are free. `File_GetSize` waits for ever for a file
  that cannot be opened: an id must always be answered.
- `obj->chara` is taken back from the model's file id (`(id - 0x590) / 10`, btl_obj.c `BtlObj_Setup`).
- Tables with a row per character and no bound check: `paramTbl` (health, ki, blast stocks; common file 2),
  `ChrTblEntry` (AI type, costumes, cost, level, links, the viewer's camera; common file 4), and in every
  character's own model file `talkTbl[opponent * 4]` (what is said to whom).
- The save: `charaBits` has room; `custom[97]` is indexed by the CELL of the character select (column + row * 7),
  and is written by the customising screens: a cell from index 97 on writes into `rec[]`. An added cell must not
  have item sets.
- Other screens index their packs with a character id taken from a save or a replay (rankings, replay headers,
  passwords: 8 bits, checked `> 0xA0`).

## How it is done (include/port_chara.h, port/src/plat_charas.c)

- An added character is 0xC6 + n (0xA5 + n until 2026-10-09: see "57 characters"), the folders taken by name.
- Its ten model-rule files are ids `0xD48 + 71000 + n * 10 + k` and its two voice banks `0xD48 + 72000 + n * 2 +
  language`, past the archive's end; `PortCharas_Alias` answers them with the folder's file, or with the base
  character's disc file where the folder has none (a missing damaged model: the costume itself; one language
  only: that one for both). `CHARA_FILE10`, `CHARA_VOICE_FILE`, `CHARA_FROM_FILE` put this into the game's rules.
- For every table and for the streamed lines it stands as its base character (`CHARA_T`): `base.txt` in the
  folder holds the id, 0 without it.
- `BT3_TEST_CHARA=<id>[,<id>]`: the first fighter of side 0 (and 1) of any battle the menus set up.

## Where a character's pictures and name come from (traced in the sources, 2026-10-09)

Four things, in four places. Verified by reading the code unless marked.

| what | where | used by |
|---|---|---|
| the face on the battle HUD | entry 1 of the character's own model file (`*_1p.pak` ..), relocated by `Res_RelocateOffsets`; the fighter object keeps it at +0x50 and `HudGauge_DrawFace` (hud_b.c) and the team HUD (hud_a_b.c) take it from there every frame | the fight |
| the large picture | a file of its own, `0x2F9 + id` (`CHARSEL_FACE_FILE`), read into a 0x16800-byte buffer | character select (menu_c_e.c), tournament bracket (menu_k_f.c), entry select (menu_i_d.c), ranking (menu_n_c.c), character reference (menu_h_b.c) |
| the chip (the small square of the roster) | a pack of texture lists inside the screen's resource pack, entry `id + 1`: section 45 of the character select's pack, section 6 of the ranking screens' | every screen with a roster; each screen's pack has its own copy |
| the name | a text block inside the screen's resource pack, one line per id: section 43 of the character select's pack (`nameText`, drawn with `TextBox_Init(box, nameText, id + 1)`); sections 30, 28, 21, 22 in other screens | every screen that shows a name; each has its own copy |

The screens' resource packs come from the menu archives: `gMenuArc5` is file `baseFile` and `gMenuArc6` file
`baseFile + 13` (menu_g_b.c, menu_h_c.c); the character select takes its pack from one of them by section.

What this means for an added character:

- **The HUD face needs nothing more**: it comes with the model file. A pack whose model file has its own entry 1
  shows its own face. (Seen 2026-10-09 with a converted character from a mod: the face on the HUD was the pack's.)
- **The large picture** can be a file in the folder, answered for `0x2F9 + id` as the model files are
  (`PortCharas_Alias`); today `CHARA_T` asks for the base character's.
- **The chip and the name are not files**: they are entries of packs that each screen unpacks from the menu
  archive, so a folder's own chip and name have to be put into the screen's tables after it has unpacked them
  (as the names of added stages are drawn by the port), or drawn by the port over the cell. Today `CHARA_T` gives
  the base character's.

Located in the data (2026-10-09, read out of the disc's files with the game's own decompressor):

- The menu archive `gMenuArc5` is file id 0x1C1 (`pzs3us1/00448.bin`): a pack of two screens, each BPE-packed
  (`Bpe_Decode`: size, packed size, blocks). The second, 7,074,880 bytes unpacked, is the character select's pack
  of 56 sections.
- **Names**: its section 43 is itself a pack of 168 entries, one per id (0..160 the characters, then "Random",
  "? ? ?", "Password Character", "Not selectable" and three more). An entry is 64 bytes: the UTF-16 byte order
  mark FF FE, the name in UTF-16, zeros. Section 44, the same shape, is the form names.
- **Chips**: its section 45 is a pack of 165 entries of 5,504 bytes each, one texture list per id (a header of
  offsets, then the picture), all the same size.

So a folder's `name.txt` becomes a 64-byte entry and a folder's chip is one 5,504-byte entry in the disc's own
format: both can be put into the unpacked pack's tables (which then need room for more entries than the disc's
168 and 165) or kept beside them and handed out for the added ids.

Not looked at: the name shown in a fight (the versus screen before it, the pause menu, the result); the other
screens' copies (same shapes expected, not checked); the layout inside a chip's 5,504 bytes.

## The other screens with a roster (2026-10-09)

The added cells are now appended in `ChrGrid_Build` (view_a_e.c), which every screen with a grid calls, so they are
in all of them at once: the duel and team select, the one-fighter select of the Ultimate Battle modes and Sim
Dragon (`SoloSel`), the Ultimate Battle team select, the tournament's entry select. Left out on purpose: the
customising screens (modes 0x30..0x32), which write the save by a cell's place, and an online session.

What each of those screens does with a character id goes through `include/port_chara.h`: `PORT_CHIP` (its own
chip; `PortChips_Load` / `PortChips_Free` keep the folders' chips in the game's memory while a screen is open),
`PORT_NAME_LINE` with `Port_CharaNamePack` (its own name), `CHARA_FACE_FILE` (its own large picture),
`PORT_IS_CHARA` (the disc's `id < 0xA1` took an added character for a marker: no DP cost, never dimmed), and one
item set only (the sets are kept in the save by the cell's place, which an added cell does not have). The
tournament bracket shows the chosen character's chip, name and pictures the same way; the ranking screen of
Ultimate Battle and Sim Dragon's day screen its picture (the ranking's name line is the base character's: that
screen's text pack holds more than names).

The large pictures' file numbers moved from 73000 to 80000: 73000 is where added songs are, and with songs
installed the first characters' pictures were answered with song files.

Checked: builds; the replay check with and without characters; two recorded sessions with characters installed
(the grid is built with the added cell, no crash); the duel select as before. NOT seen: any of the newly covered
screens (no recorded session reaches them).

Not covered: the character reference (a list of 161 entries with its own tables, not a grid), the customising
screens, the replay and password screens.

## 57 characters (2026-10-09)

The limit was 27, ids 0xA5..0xBF. It is 57 now, ids **0xC6..0xFE** (`PORT_CHARA_FIRST`, `PORT_CHARA_MAX` in
include/port_chara.h and port/include/plat_charas.h). The range starts at 0xC6 and not at 0xA5 because the battle
takes a prop's "character" from its model file's number as it does a fighter's, which for the props' files comes
out as 0xC1..0xC5: a wider range from 0xA5 would have taken props for added characters. `BT3_TEST_CHARA` therefore
takes 198 for the first added character where it took 165. The folder scan reads 256 names (it stopped at 64),
and the large pictures' file numbers moved to 90000 (the spoken lines of 57 characters run from 74000 to 85400).

Checked with 54 folders installed (converted characters, four of them forms of others): all 54 are found, the
grid gets 50 cells for them (cells 99..148 of 165), the recorded session runs, a fight as the first and as the last
of them runs, the row of the select screen shows their chips; the replay check is unchanged with none and with 54.
Not checked: the other screens with that many, the menu's memory with 57 chips loaded (about 310 KB of it) on
screens that use more of it than the duel select.

## Random picks (2026-10-09)

The "random" cell of the select screens and the tournament's computer entrants are drawn from the grid with the
disc's test `id < 0xA1` (`<= 0xA0`), which passes over added characters. The setting "Added characters in random
picks" (F1, Mods; `chara_pool`, off unless set; `BT3_CHARA_POOL=1`) puts them in the draw: `PORT_IN_POOL` in the
duel and team select's random cell, the tournament entry select's two draws and the bracket's computer entrants.
A cell drawn that way lies past the save's item sets (`custom[97]`, by the cell's place): the reads that follow a
draw are guarded with `PORT_SAVE_CELL` (found while doing this: with the draw opened, they would have read past
the array).

Seen without a window: the computer playing an added character through a recorded fight (it moves, attacks and
is hit; its behaviour is its base character's). Seen by the user on 2026-10-10: a random draw landing on an added character, and added characters among a tournament's entrants.

## Fusion partners (2026-10-10)

A fusion needs a partner on the team, and the character's own table (entry 18, +0xBA, four ids for each of the three
fusions) names it by the numbers of whoever made the pack. For an added partner those numbers mean someone else
here, so the fusion could never be started. `forms.txt` takes `partner1=Folder,Folder,...` (also `partner2`,
`partner3`; up to four, `disc:<id>` for a character of the disc), resolved like the form lines
(`Port_CharaFormTarget` slots 7..18, used by `BtlParam_GetUnkBA`). Without the line the table's own ids are used.

The input (src/battle/btl_act_j.c): R3 HELD for 12 frames with left / up / right = fusion 1 / 2 / 3; a tap is the
transformation. Team battle only, the partner present and alive, the blast stock the fusion costs.

Seen by the user (2026-10-10): a fusion of two added characters into a third. Two faults seen with it, both
changed since and NOT seen again yet:

- **The partner was not drawn in the fusion's scene.** The scene shows the character at +0xB7 of the table, again
  its maker's number. For an added fighter the partner found on the team is shown (`BtlAct_QueueFusion`).
- **Team mates had their base characters' small pictures in the fight.** That picture is the fight's member block
  (file 8 + id * 2 + side, 0x1000 bytes, one for each side), which was asked for by the base character. A folder's
  `member_1p.bin` / `member_2p.bin` are served now (`CHARA_MEMBER_FILE`, file index 91000 + n * 2 + side); without
  them the base character's.

## Larger models, and a folder made from a data folder (2026-10-10)

- **The model buffers follow what there is to load.** They were 1 MB with any added character. A disc whose 161
  characters had all been replaced (same layout, standard archives, 804 bytes of its program changed) has 50 model
  files over the original's 0xCE000 and 16 over 1 MB, the largest 1,150,125 bytes. `Port_ModelSize`
  (port/src/plat_charas.c) takes the largest model among the added characters and the data folder's own
  (0x590 + id * 10 + 0..7), in whole pages, up to 1.5 MB (`PORT_MODEL_MAX`); an added character with a larger file
  is left out. Online: the original's size, as before.
- **Such a disc's data runs as a data folder** (`BT3_DATA`) with the original program file beside it (the port's
  tables come from that file, and its check passes): menus and a fight, windowless. Not seen on a screen. What the
  disc's own changes to the program do was not looked at.
- **`port/tools/import_chara.py <data folder> <id> <folder>`** copies everything of one character out of a data
  folder of the game's layout into a character folder: models, animations, effects, voice banks, the spoken lines,
  portrait, member blocks, chip, name, `base.txt`. From the player's own game it makes a copy of a character to
  start a pack from. It says which other ids the character's table names (forms, fusions).

- **`--with-forms`** imports the transformations too, and theirs, a folder each (`<prefix><id>`) with a `forms.txt`
  naming the others; `--with-fusions` follows the fusions, their partners and the forms of those (refused past 57).

Checked: three characters of that disc imported, read by the port, and a recorded fight with the 1.15 MB one run
to its end windowless; the replay check. Seen by the user (2026-10-10): a set of two made with `--with-forms` from
that disc, chosen and transformed in a fight. NOT seen: the disc's data run whole on a screen; `--with-fusions`.

## Forms in folders inside the character's (2026-10-10)

A folder inside a character's folder (other than `voice_us`, `voice_jp`) that holds a character is a form of it: a
character of its own, placed right after its owner. One level. What its `base.txt` and `name.txt` do not say is the
owner's. A name in `forms.txt` is looked for, in this order: `..` (the owner, from inside a form's folder), a form
inside the same character's folder, a character of the top, `Character/Form`. Folders side by side work as before;
a fusion's result, which has two parents, stays at the top.

`import_chara.py --with-forms` writes this layout, named from the disc: the character's name (section 43 of the
select's pack) for the folder, the form's label (section 44, "Super Saiyan") for the folders inside, the prefix and
the id where the disc gives none.

The script also takes a disc image (`.iso`) in place of the data folder, reading its two archives in place, and
`--all` makes a folder for every character of the disc (seen: 161 characters in 141 folders, 2.2 GB): more than
the port takes at once, a shelf to copy from.

An added stage's two pictures in the select (2026-10-10), each a file beside its `.unk`, each optional: `<name>.thumb`,
the large picture behind the reel (the disc's file 0x39D + stage; served at file index 70100 + n), and `<name>.icon`,
the small one in the reel (one 5504-byte entry of section 46 of the select's pack; copied into the game's memory
while the select is up, `PortStageIcons_*` in port/src/plat_stages.c). Without them the stage borrows a disc
stage's, as before. `--stages` writes both. Seen by the user: the large picture. NOT seen: the small one.

`--stages` also writes `stage names.png`: the disc's 36 stage-name pictures (section 6 of the select's pack, nine
512 x 256 sheets of four, decoded as namefont.c does) one under another in the stages' order, to read the names from.

`--stages` copies a disc's stages (file 0x171 + stage) as `<Name>.unk` files for the `stages` folder, named after
the original's places (a stage's name on a disc is a picture); `--against <original>` leaves out the unchanged
ones. Seen: 33 of a disc's 35, all within the stage buffer; two of them taken by the game as added stages. NOT seen: a fight on one (a windowless run with `BT3_TEST_STAGE` gave the same numbers for both, so it does not show the stage was used).

Checked: sets made from two data folders read by the port with every link found (none reported missing), folders
side by side still read, the replay check. NOT seen: a set of this layout in the menu or in a fight.

## Brought up to the release (2026-10-10)

The branch has the release's line merged in (0.1.19: the room codes and relay, the tournament crash fix, the clash
value, the added songs' and stages' fixes for team battles). Not merged the other way. With it the team select
serves an added stage's own pictures too (`.thumb`, `.icon`), as the duel's select does here.
Checked: builds, the replay check with and without folders installed, a recorded menu-to-fight session with added
characters and stages. NOT seen: the options menu after the merge, the team select's own pictures.

## The form's label (2026-10-10)

Under a character's name the selects show a second line, the form's label ("Super Saiyan"; section 44 of the duel
select's pack, one line a character, most empty). An added character showed its base character's. `form.txt` in
its folder is its own (first line, UTF-8; an empty file: no label; no file: the base's, as before).
`Port_CharaFormPack` makes the screen's pack with a line more for each added character, as for the names
(`PORT_FORM_LINE`; the six roster screens). Names and labels are now turned from UTF-8 into the game's 16-bit text
(they were cut to ASCII). `import_chara.py` writes `form.txt` from the disc.
Checked: builds, the replay check, a recorded session through the duel select with such folders. NOT seen: a label.

## The save and replays (audit of the sources, 2026-10-10)

Every writer of the save and of a replay was read for an added id or a cell past the disc's 97. Three faults, and
what was done:

- **Evolution Z could write outside `custom[97]`.** The selects share one remembered cursor (gProgress + 0x440). Left
  on an added cell, it lay past the end of Evolution Z's grid (which has no added cells); the restore tested only
  `ChrGrid_IsSelectable`, a cell past the count read as character 0, and confirming wrote "item sets" over `rec[]`,
  `item[]`, the money. Fixed at the root: every restore (menu_c_e, menu_e_b, menu_l_b, menu_m_b, menu_i_d, menu_u_g)
  drops a cursor outside its own grid (`PORT_CELL_IN`). The same stale cursor made the other selects read item sets
  from past the table; gone with it.
- **Sim Dragon's ranking kept an added id** and its top screen then indexed a 165-entry face pack with it. The ranking
  now keeps the base character's id (`PORT_SAVE_CHARA`, menu_r_c.c), and the top screen maps an id already stored
  (menu_r_d.c).
- **A replay keeps added ids** (the whole battle setup). Left so: a replay of a fight with an added character needs
  that character, in the same place in the list. The replay list's chip lookup read out of range with such an id;
  it shows the base character's chip now (menu_za_d.c). Played with the folder gone, the id answers as character 0:
  no fault, but not the recorded fight.

Found safe: `charaBits` (ids from disc tables only), `rec[]`, the tournament's prizes, the port's settings (a 0/1
flag). gProgress is never written to the card.

Checked: builds, the replay check with and without packs. NOT seen: any of the three paths, before or after. A
fifteen-minute comparison of the save with and without a pack chosen showed nothing, because the recorded session
never reaches a point where the game saves.

## More than that (researched 2026-10-09, nothing changed)

The three reasons given above for the limit do not bind an added character: every character id in the game's
state, the save, the replay and the menus is 32 bits wide; the only one-byte ids are in disc and pack DATA (the
transformation and fusion tables, the character table's links); the unlock bits are never asked about an added
character. What limits the number today is the port's own: `PORT_CHARA_MAX` (defined twice), a scan of the first
64 folders, the file-number ranges (ten model files per character from 71000 hold 100 before they reach the voice
banks at 72000), and the grid: every screen's buffer is 165 cells and the disc's grid is about 98, so about 60
more cells fit (forms share a cell).

- Up to 90 (ids to 0xFE): raise the constants and the scan, give the grids more cells or the added characters a
  page of their own. To look at first: the battle takes a prop's "character" from its model file's number, which
  comes out as 0xC1..0xC5, inside the wider range; and a pack's own tables naming partners by number.
- Hundreds: ids from 0x100 are taken by the battle's props (0x100..0x12B) in one path (`BtlLoad_StepObject`, the
  three throw getters). The least invasive way seen: wide numbers in the menus only, with a scrolling or paged
  section for added characters, and the few chosen for a battle (at most ten, and their forms) bound to the
  numbers 0xA5..0xFE when the battle is set up; a replay then has to carry which folders they were.

Done on 2026-11-06 that way: see "Hundreds of added characters" at the end.

## Not done

The character reference; fusion between added characters (wired, never tried); a form added to a character of the
disc; the save audit (what a play with added characters leaves in the save and in replays); zip files; a guide
to the pack format for people who make packs.

## Brought up to release 0.1.34 (2026-10-13, branch `charslots-next`)

`port` merged in (netcode version 4 with the desync check, the pause menu, MP3 songs, menu fonts, the screen
reader, macOS). Two conflicts: the settings window (the Mods tab is now the eighth, after Misc; the shoulder
buttons go round eight) and the file cache (an added character is answered in `Port_FileOpenHost`, the one place
that now decides which file is loaded).

Changed with it:

- **Online play does not know the added characters.** Before, a session only left their cells out of the grid; the
  folder was still read, and a select screen took memory of the game for the chips, so two copies of a match had
  different states as soon as one of them had a folder (measured: the whole-state checksum differs from the
  character select on; the fight values agreed). Now everything `plat_charas.c` answers in a session is what it
  answers without a folder (`live()`): no character, no cell, no name or label line, no chip, no memory taken,
  `BT3_TEST_CHARA` / `BT3_TEST_CELL` / the random-picks setting ignored. A session that starts the program again
  does not read the folder. The added stages' own icons (`PortStageIcons_*`) take no memory in a session either.
  The Mods tab says so. Measured with two copies over loopback, a folder on one side only (host, then joiner; a
  session begun in the running program, and one started with the program): whole state the same on every blank.
- **The PS2 build is what it was.** Four menu files used `PORT_CHARA_ADDED` outside `#ifdef PORT` (the PS2 build
  would not have linked) and `ChrTbl_IsRelated` had three statements more. Checked: every file under `src/` that
  differs from `port` gives the same assembly from the PS2 compiler as `port`'s.
- **The screen reader** reads an added character's name and label from the lines the port puts after the disc's
  (it read line `id`, which is another character's or nothing), and keeps it for the screens that show pictures only.
- `BT3_CHARA_LOG` also says, once for each, which file was served for an added character (the folder's own or the
  disc's it falls back on).

Checked: Linux and Windows builds, the replay check, a recorded session through a character select with the
cursor on the added cell (its name, portrait and chip on an offscreen picture; its model, effects, voice bank,
member block and spoken lines served from the folder), the title's demonstration fights with `BT3_TEST_CHARA`
twice with the same state on every blank.

Known: the direct battle of `BT3_DEMO` / `BT3_REPLAY` (no menus) never mounts the second archive, where every
file number of an added character lies: `BT3_CHARA=198` there waits for ever. Use `BT3_TEST_CHARA` with the menus.

## Character packages: replacement, one manifest, numbers (2026-10-13, branch `char-packages`)

The standard for people who make packs is now `docs/port/character_packages.md` (format 1). What changed in the
code, and what was found on the way:

- **`character.txt`**, a manifest of `key=value` lines: `format`, `name`, `form`, `base`, `replaces`, `author`,
  `version`, `about`, the links (`left` .. `partner3`), `health`, `ki`, `blast`. One reader (`apply` / `parse` in
  plat_charas.c) takes it and the older files (`name.txt`, `form.txt`, `base.txt`, `forms.txt`, each fed to it as
  its key); the manifest is read last and wins. Unknown keys are ignored. Everything is matched without regard to
  case now (the older files' names and the two voice folders were exact before).
- **Replacement** (`replaces=<id>`): the folder's files are answered for the disc character's own file numbers
  (`alias_replaced`), in the one opener of the loader and in the streamed-audio path (both go through
  `PortCharas_Alias`), ahead of `gamedata/mods/`. No id is taken. Its chip is copied over the disc's entry in the
  screen's own chip pack (`PortChips_Load(pack)`: same 5,504 bytes, no memory of the game's taken), its name and
  label are put on the character's own line of the text packs the six roster screens already get from
  `Port_CharaNamePack` / `Port_CharaFormPack`. Two folders for one id: the first by name. Inner folders with
  their own `replaces=` replace the forms.
- **Whole character or skin.** A replacement that has its own animations or effects serves its first costume for
  a costume it does not have; one without them leaves the disc's other costumes alone. Found by a crash: character
  55's model alone in slot 0 stops the game in `BtlObj_BindTables` when a move asks for an effect that slot 0's
  effect file does not have. The costume count of a replaced character is the disc table's (not opened up: the
  count is read in more places than `ChrTbl_WrapCostume` was checked for, and nothing of it can be seen windowless).
- **The links work for a replaced disc character too** (`BtlParam_PortTarget` no longer asks whether the fighter
  is an added one; `Port_CharaFormTarget` answers -1 for everyone without a package). So a replaced character can
  transform into an added one. Wired, NOT tried.
- **Numbers**: `PORT_STAT` around the three reads of `paramTbl` in `BtlMember_LoadParams` (health, starting ki,
  blast stocks). The disc's table (common file 2, 161 rows of 16 bytes): health 30000..50000, ki 0..100000, blast
  4..7; the third word is 100000 in every row.
- **Every file is checked against the buffer it is read into** when the folder is read; nothing in the game looks
  at a size before reading (`File_Request` ignores its third argument). Before, only the eight models were. The
  buffers: model `BtlRes_ModelSize()` (up to `PORT_MODEL_MAX`), animations `BTL_RES_ANIM0_SIZE` 0x160800, effects
  `BTL_RES_ANIM1_SIZE` 0xCE800, voice bank `BTL_BANK_BUF_SIZE` 0x90800 (the reload buffer; the first load takes
  memory by the file's size), portrait 0x16800 in every screen, member block `BTL_MEMBER_BUF_SIZE` 0x1000. A file
  that does not fit is left out and said. `chip.bin` is checked to be a texture file whose offsets stay inside
  its 5,504 bytes (the game turns them into pointers).
- **Two gaps closed with it.** `Port_ModelSize` did not look at `gamedata/mods/pzs3us1/`, so a larger model put
  there was read past the buffer; it does now. The reference viewer's two model buffers (`ChrView_Init`,
  view_a_d.c) were the original's 0xCE000 whatever was installed; they follow `Port_ModelSize` now.
  Still open: a model under `mods/` larger than the original's buffer in an ONLINE session, where the buffers
  are the original's on purpose.
- **The voice names were the wrong way round.** `_voice_us` was taken as the bank 0xBDA and `voice_us/` as the
  lines from 0x8D4E. The community's file lists of the disc call 0xBDA + id `<Name>_Voice_JP.pak` and 0xC7B + id
  `<Name>_Voice_US.pak` (compared byte for byte: five characters' banks, six spoken lines), and the game takes
  0xC7B / 0xCC32 with `SAVE_FLAG_VOICE` set, which a new save has and which "reset to default" in the sound
  options sets (menu_x_c.c). So US = 0xC7B = 0xCC32 + ..., JP = 0xBDA = 0x8D4E + ...; the port and the importer
  follow that now. A folder made with the importer before today has its languages exchanged.
- **Online.** `rlive()` beside `live()`; the fingerprint opens files with `Port_FileOpenData`, which knows no
  package (before, it would have hashed a replacement's model as the disc character's); the loader's table of open
  files (plat_fcache.c) is emptied when a session begins and when it ends (`PortCharas_Epoch`), because a file
  number that was open from a folder would otherwise stay the folder's in the session. That last one was shown
  to matter: with the emptying taken out, a session asked for in the middle of a fight with replaced characters
  differs from blank 1100 and ends; with it, the same on every blank.
- **F1 > Mods** lists the packages (`Port_CharaPkg`): name, adds / replaces which id, author, version, about, and
  what was found wrong. No switch per package: the ids of added characters come from the folder order at start,
  so a switch would need a restart and a place for names in the settings file. Next.
- **`import_chara.py`** writes `character.txt`, the corrected voice names, `--replace` (a replacement of the same
  id, also with `--with-forms`) and `--names` (the disc's ids and names).
- `BT3_TEST_CELL=disc:<id>` puts the select's cursor on a disc character. `BT3_CHARA_LOG` also says the names,
  labels and chips put on a select.
- `port/tests/chara_packages/packages.c`: the reader on its own under the address and undefined-behaviour
  sanitizers (fixed cases, made-up manifests, a folder of awkward packages).

Seen in the run-to-run comparison, not from this work: two runs of the title's demonstration fights with the
same binary and no folder at all agree in every fight value but can differ in the whole-state checksum from
blank 27160 on (the binary of 0.1.34 does the same; four runs fall into two groups). Not looked into.

Checked (all without a window): see the section of 2026-10-13 in README.md.
NOT seen on a screen by a person: any replacement; the Mods tab's list (one offscreen picture); a link from a
replaced character; numbers on the fight's display beyond the disc's range.

## The Mod Manager on character packages (2026-10-17): what is left

The contributed Mod Manager runs on the packages (port/src/gs/modmgr.c; src/menu/port_modmgr*.c, port_mmkit.c). Done:
the library (on / off, priority, rename, the form label, author, version, about, replace / new / its fighter, remove
and restore, the folder), the platform model of any package (on or not), the main menu plate, and the FORMS screen
(transformations and fusions into character.txt, put into the fighter's table in the fight; seen working by the
user). To do:

- The test fight (START / TEST) and the way back to the manager from its pause menu.
- The Mod Fighters grid on the character select (Square), on the added characters' ids instead of the
  contribution's costume codes; its order is the load order (the Mod Fighters order screen already writes it).
- (Done: the portrait maker. A PNG or BMP, or the package's own portrait, put whole into the frame (512 x 512 on
  the screen, stored 256 x 512), moved and sized as the screen shows it, brought down to 256 colours (fewer if packed
  it would not fit 92,160 bytes), packed with the game's byte pair coder (checked by unpacking it again) and written
  as portrait.bin; the package's own kept as portrait_original.bin (its name in portrait_was) and the picture as
  portrait_source.png / .bmp, so it can be framed again; the reset puts the own one back. Port_ModArt* in
  gs/modmgr.c. And the icon crop: the square of the portrait, averaged
  down to 64 x 64 in the portrait's colours, written into the package's chip.bin, its own kept as chip_original.bin
  and put back by "automatic"; else into the base character's chip of the disc. Port_ModSet 3..5 in gs/modmgr.c,
  with its own bounded unpacker; tested under the sanitizers with 600 damaged portraits.)
- (Done: adding packages, Port_ModImport* in gs/modmgr.c. A folder, a file of one or an archive dropped on the window
  (gs_input.c keeps the path, Port_DropTake) or picked in the browser; a thread copies each package found (the
  folder itself, else folders in it four deep) into characters/.import and renames it into place, so the library
  never reads half of one; archives are unpacked there by the system's tar (Windows) or bsdtar / unzip / 7z /
  unrar, links taken out before the move; links are never followed, in the copy or the clean up. Tested with
  folders, zip, 7z, tar.gz, a damaged rar and a zip with "../" names, under the address and undefined-behaviour
  sanitizers. Not done: the community's file names (port/src/plat_modmgr_prefix.inc, kept out of the tree).)
- Fusions: the scenes' motions (fusionN_scene is kept, not used), with the contribution's motion splicer made safe
  (a heap overflow from a mod's file); the skill list's transformation page written again.
- A fighter's moves on the library's panel (Port_ModMoves), and the name lists the contribution read from text files
  (to come from the player's disc, not the source).
- The credit of the manager's pictures (port/tbo/ui/CREDITS.md): the author's name, if they want it there.
- Then: docs, and the merge into main.

## Hundreds of added characters (2026-11-06, branch `roster-expand`)

The Custom Character build stopped at 85 added characters (ids 0xC6..0xFE, then 0xA5..0xC0): an 86th package was
not read ("there are already 85 added characters"), so a warrior added with the creator's "Add to roster" past that
never got a cell. Now up to **1024** added characters are read, every one gets a cell, and one battle meets up to 85
different ones. The design is the one "More than that" proposed:

- **Two kinds of id.** The 85 ids are the battle's *slots* (`PORT_CHARA_SLOTS`; below 0x100, not 0xFF, not the
  props' 0xC1..0xC5, not the markers). With 85 or fewer installed nothing changes: the n-th added character is slot
  n everywhere (`PORT_CHARA_SLOT_ID(n)`, the ids of earlier builds: saves, replays, `BT3_TEST_CHARA=198`). With more,
  the menus know the n-th by a *wide* id, `PORT_CHARA_WIDE0 + n` = 0x400 + n, and only battles use slots.
  `PORT_CHARA_ADDED` is true for both kinds, `PORT_CHARA_IDX` (now `Port_CharaIdx`) answers the package for both, and
  `PORT_CHARA_ID(n)` (`Port_CharaId`) is the menus' id.
- **Binding** (`Port_CharaBind*` in plat_charas.c, called by `PortBattle_Bind` in battle_load.c). `Battle_Main`
  binds before `Battle_Init` and unbinds after `Battle_Term`; `BattleSetup_FinishEx` binds again when it runs inside
  a battle (the battle script's set-up: story mode, the Story Creator's sagas; `Battle_Init` clears the work's
  `running`, so battle_load.c keeps a flag of its own). Wanted, in this order: each side's current and starting form
  and its members up to its count, then the opponent pool up to its count; with each package every package its links
  0..6 lead to (transformations, fusion results), and theirs. A slot id already in the set-up keeps its package
  (`BT3_TEST_CHARA` with a small number); one of the first 85 takes its own slot; the others take free slots from
  the top (0xC0 down). A package that finds no slot (more than 85 different added characters in one battle: only a
  long opponent pool can do that) fights as its base character and a link to it is "none" (said in the log). Every
  id of the set-up is then rewritten (a wide id to its slot, one without a slot to its base character), and back to
  wide ids when the battle ends, so with more than 85 installed the menus, the save and a replay never hold a slot
  id. The binding (`sSlotPkg`, `sPkgSlot`) is not game state: it is worked out from the set-up and the packages,
  the same on every machine. `BT3_BIND_LOG=1` (or `BT3_CHARA_LOG`) prints it.
- **Links** (a package's `target[]`) are kept as `PORT_CHARA_WIDE0 + n` for an added character whatever ids it has;
  `Port_CharaFormTarget` and `Port_CharaParamApply` (the bytes of the fighter's form table) give the slot it has in
  this battle (`battle_target`), 0xFF where it has none. `Port_CharaForms` (the select's form groups) gives the
  menus' ids. The groups are worked out once per reading of the folder (`forms_prepare`): asked per cell they were
  the whole roster again for every character, too slow for hundreds.
- **File numbers belong to the package, not to the id.** The first 85 keep the old ranges (71000, 72000, 74000,
  92000, 93000, 94000 + (161 + n) * 24); the 86th and on have a block of 256 numbers each from index 200000
  (`PORT_CHARA_HI_INDEX`: +0..9 the model rule's ten, +10/11 the voice banks, +12 the portrait, +14/15 the member
  blocks, +16..39 costumes 5..16, +56..255 the spoken lines). `CHARA_FILE10`, `CHARA_FROM_FILE`, `CHARA_VOICE_FILE`,
  `CHARA_FACE_FILE`, `CHARA_MEMBER_FILE`, `CHARA_LINE_FILE` call functions now (`Port_CharaFile10`,
  `Port_CharaFromFile`, `Port_CharaFile`); `CHARA_FROM_FILE` gives the battle's id of the model's package.
  story_ui.cpp asks `Port_CharaFileIndex` instead of keeping its own copy of the numbers.
- **The grids.** Every roster screen's grid buffer holds 165 cells (about 60 added cells). With added characters
  installed the six screens that add them (duel and team select, the tournament's entry select and bracket, the
  one-fighter select, the Ultimate Battle team select) build into a buffer of the port's (`PORT_GRID` /
  `PORT_GRIDBUF`, `Port_GridBuf(0..7)`: 1190 cells, room for all 1024), and `ChrGrid_Build` asks `Port_GridCap` how
  many cells the buffer it fills has. Port memory below 4 GB (not the game's heap, not game state: a grid is made
  when its screen opens and only read after). Without added characters, and so in every online session, a screen
  uses its own buffer as on the disc. The select scrolls row by row as before (the reel shows the cursor's row):
  133 added characters are about 14 more rows than the disc's.
- **Memory of the menus.** The chips (5,504 bytes each, in memory the renderer reads) are taken only for the
  packages that have a `chip.bin`; up to 85 of them from the game's heap as before (468 KB), more from the port's
  memory below 4 GB (`Port_GameBigAlloc`, as a large stage is), so hundreds of chips leave the menus' 28 MB heap
  alone (`Heap_Free` gives either back). Each roster screen still reads every chip file when it opens (133 small
  files: not noticeable). The name and label packs (`sNames`, `sForms`) are the port's static memory, 160 KB each
  for 1024 lines. The large portraits are loaded one at a time per side, as before.
- **Other limits raised with it**: the folder scan and the load order 1400 folders, the package list (F1 > Mods, the
  Mod Manager's library `LIB_MAX`) 1600 entries; story_ui.cpp's caches go by package (they were by id, 512).
- **Test aids**: `BT3_TEST_CHARA=added:<n>` and `BT3_TEST_CELL=added:<n>` name the n-th added character whatever id
  it has; plain numbers still work (1024 + n for a wide id).
- **Online**: unchanged. A session knows no package (`live()` is 0), so nothing is bound and every screen keeps its
  own grid.

Saves: an added character still goes into the save as its base character (`PORT_SAVE_CHARA`); nothing else of the
save holds an added id (the audit of 2026-10-10). A replay recorded with more than 85 installed holds wide ids (the
copy is made in the menus, or turned back with `PortBattle_Canon` when the battle made the set-up) and is bound
again when it is played (`BattleReplay_Load` puts it in the set-up, `Battle_Main` binds it); like every replay with
added characters it needs the same packages in the same order.

Checked (Windows build, `BT3_CHARACTERS` a copy of the user's folder with 45 of its BT4 packages copied again under
"ZZ" names: 133 added, 91 replaced): the folder is read whole (`133 added ... more than 85`); the duel select builds
96 added cells (cells 99..194) and shows a character past the 85th with its own chip, portrait and name
(`BT3_TEST_CELL=added:120`); a menu-driven fight of two characters past the 85th (ZZ Caulifla, n = 99, menu id 1123,
against ZZ Broly (DBS), n = 93) binds them and their forms to 192..187, fights, transforms into the added form and
back (`bt3: form: side 0 member 0: 192 ZZ Caulifla -> 191 ZZ Caulifla Super Saiyan`, then back to 192); a Story
Creator saga naming two characters past the 85th by folder plays its battle with them (the set-up made inside the
battle is bound: found with it, `Battle_Init` clearing `running`); "View Replay Data" after that fight plays the
replay with the same binding and goes back to the select. With 85 added (the same folder, a load order switching
48 off) the ids are 198.. and the file numbers 71000.. as before, and nothing is bound. (The user's own folder holds
88 packages that add a character: three of them were not read before.) Pictures: research/zw_roster/shots. NOT
checked: the team select, tournament, Ultimate Battle and Sim Dragon screens with that many; a fusion between
characters past the 85th; a replay saved to the card and loaded with more than 85; the creator's "Add to roster"
(not pressed: it writes a characters folder); the Linux test of the reader (port/tests/chara_packages, POSIX only).

#ifndef PORT_CHARA_H
#define PORT_CHARA_H

/* PC build: characters added from outside the disc (port/src/plat_charas.c).
 *
 * The disc has 161 characters, ids 0x00..0xA0, and the menus use 0xA1..0xA4 as markers (random, locked, custom,
 * empty), in the disc's data as well as in the code. An added character has an id from 0xC6: below 0x100 (from
 * there the battle takes an id for an object), not 0xFF ("none" in the byte-sized form lists), and with a bit in
 * the 192-bit unlock field.
 *
 * Two things hang on a character id:
 * - file ids. Every rule (`0x590 + id * 10 + n` for the models, `0xBDA + id` for the voice bank, ...) is packed
 *   for 161 characters: the id after the last character's is the first of the next kind of file. An added
 *   character's own files (models, animations, effects, voice banks) have ids past the end of the second
 *   archive, which the port answers from the character's folder.
 * - tables of 161 rows in the common files (health and ki, cost, costumes, AI type, links) and of 161 entries in
 *   every character's own file (what is said to which opponent), and the streamed voice lines. For these an
 *   added character stands as its BASE character, one of the disc's.
 */
#ifdef PORT
/* The range: 0xC6..0xFE, 57 characters. Not from 0xA5: the battle takes a prop's "character" from its model file's
   number as it does a fighter's ((id - 0x590) / 10), which for the props' files comes out as 0xC1..0xC5, and those
   must not be taken for added characters. Below 0xFF, which is "none" in the packs' byte-sized form tables. (Every
   character id in the game's state, the save and the menus is 32 bits wide; the unlock bits are never asked about
   an added character.) */
/* Custom Character build: a second range, 0xA5..0xC0 (28 more), after the first 57: the props' 0xC1..0xC5 and the
   markers' 0xA1..0xA4 stay out, as the note above asks. These 85 ids are the battle's SLOTS for added characters.
   More than 85 (2026-11-06): the menus give every added character a WIDE id, PORT_CHARA_WIDE0 + n (up to
   PORT_CHARA_MAX of them), and the characters of a battle (its fighters, the opponent pool, the forms and fusions
   they can turn into) are bound to slots when the battle starts (Battle_Main: PortBattle_Bind, battle_load.c) and
   back to their wide ids when it ends. With 85 or fewer installed nothing is bound: the n-th is PORT_CHARA_SLOT_ID(n)
   everywhere, the id earlier builds gave it (saves and replays). PORT_CHARA_IDX answers both kinds of id (a slot:
   the package bound to it); the file numbers belong to the package, not to the id (port/src/plat_charas.c). */
#define PORT_CHARA_FIRST 0xC6
#define PORT_CHARA_COUNT1 57                     /* 0xC6..0xFE */
#define PORT_CHARA_FIRST2 0xA5
#define PORT_CHARA_COUNT2 28                     /* 0xA5..0xC0 */
#define PORT_CHARA_SLOTS (PORT_CHARA_COUNT1 + PORT_CHARA_COUNT2)
#define PORT_CHARA_SLOT_ID(n) ((s32)(n) < PORT_CHARA_COUNT1 ? PORT_CHARA_FIRST + (s32)(n) : PORT_CHARA_FIRST2 + ((s32)(n) - PORT_CHARA_COUNT1))
#define PORT_CHARA_IS_SLOT(c) (((s32)(c) >= PORT_CHARA_FIRST && (s32)(c) < PORT_CHARA_FIRST + PORT_CHARA_COUNT1) || \
                               ((s32)(c) >= PORT_CHARA_FIRST2 && (s32)(c) < PORT_CHARA_FIRST2 + PORT_CHARA_COUNT2))
#define PORT_CHARA_WIDE0 0x400
#define PORT_CHARA_MAX 1024                      /* added characters at most (the packages that add one) */
#define PORT_CHARA_IS_WIDE(c) ((s32)(c) >= PORT_CHARA_WIDE0 && (s32)(c) < PORT_CHARA_WIDE0 + PORT_CHARA_MAX)
#define PORT_CHARA_ADDED(c) (PORT_CHARA_IS_SLOT(c) || PORT_CHARA_IS_WIDE(c))
extern int Port_CharaIdx(int chara); /* the package behind an added id; PORT_CHARA_MAX: none */
extern int Port_CharaId(int n);      /* the menus' id of the n-th added character */
#define PORT_CHARA_IDX(c) Port_CharaIdx(c)
#define PORT_CHARA_ID(n) Port_CharaId(n)
extern int Port_CharaBase(int chara); /* an added character's base character; any other id as it is */
/* the character id for the tables and the streamed lines */
#define CHARA_T(c) (PORT_CHARA_ADDED(c) ? Port_CharaBase(c) : (s32)(c))
/* `id * 10` of the model rule: CHARA_FILE10(c) + 0x590 + n is file n of the character (an added character's ten
   files have numbers past the archive's end, Port_CharaFile10) */
extern int Port_CharaFile10(int chara);
#define CHARA_FILE10(c) (PORT_CHARA_ADDED(c) ? Port_CharaFile10(c) : (s32)(c) * 10)
/* the character a model file belongs to (the battle takes an object's character from its file id); for an added
   character's file, the id it has in the battle */
extern int Port_CharaFromFile(int id);
#define CHARA_FROM_FILE(id) Port_CharaFromFile(id)
/* an added character's other files (port/src/plat_charas.c): PORT_CFILE_VOICE (k: 0 the bank, 1 the other
   language's), _FACE, _MEMBER (k: the side), _LINE (k: the line, + 100 the second set) */
enum { PORT_CFILE_VOICE, PORT_CFILE_FACE, PORT_CFILE_MEMBER, PORT_CFILE_LINE };
extern int Port_CharaFile(int chara, int kind, int k);
/* the voice bank: `base` is one of the two languages' first file ids, `alt` says whether it is the second */
#define CHARA_VOICE_FILE(c, base, alt) (PORT_CHARA_ADDED(c) ? Port_CharaFile(c, PORT_CFILE_VOICE, (alt) ? 1 : 0) : (s32)(c) + (base))
/* the large picture of the menus: 0x2F9 + id on the disc; an added character's own file, answered from its folder
   (`portrait.bin`) or with its base character's */
/* The member block of the fight (0x1000 bytes a side: the small picture of a team mate in the fight's display):
   the disc's file 8 + id * 2 + side; an added character's is served from its folder (`member_1p.bin`,
   `member_2p.bin`) or is its base character's. */
#define CHARA_MEMBER_FILE(c, side) \
    (PORT_CHARA_ADDED(c) ? Port_CharaFile(c, PORT_CFILE_MEMBER, side) : (s32)(c) * 2 + (side) + 8)
#define CHARA_FACE_FILE(c) (PORT_CHARA_ADDED(c) ? Port_CharaFile(c, PORT_CFILE_FACE, 0) : (s32)(c) + 0x2F9)
/* a spoken line (a sound stream): `base + id * 100 + line` on the disc, two sets (`alt` the second); an added
   character's own, answered from its folder (voice_us/NNN.adx, voice_jp/NNN.adx) or with its base character's */
#define CHARA_LINE_FILE(c, base, alt, line) \
    (PORT_CHARA_ADDED(c) ? Port_CharaFile(c, PORT_CFILE_LINE, ((alt) ? 100 : 0) + (line)) : (base) + ((s32)(c) * 100 + (line)))
/* More than four costumes. The disc has room for four a character (its model files 0x590 + id * 10 + 0..3, the
   damaged ones + 4); a package may have up to PORT_COSTUME_MAX (Team BT4's characters have up to 9): costume n >= 4
   of a character is the file PORT_CHARA_XCOST0 + slot * PORT_XCOST_PER + (n - 4) * 2 (+ 1 damaged), slot the disc
   id of a disc character (a package that replaces it brings them: `replaces=`) or 161 + n for the n-th added one
   (n < 85: those after it have theirs in their own block of file numbers, port/src/plat_charas.c).
   The port answers them from the package's folder (`*_5p.pak` .. `*_16p.pak`, `*_5p_dmg.pak` ..). How many a
   character has: ChrTbl_WrapCostume (the disc's table, or the package's own number: Port_CharaCostumeCount). */
#define PORT_COSTUME_MAX 16
#define PORT_CHARA_XCOST0 (0xD48 + 94000)
#define PORT_XCOST_PER ((PORT_COSTUME_MAX - 4) * 2)
#define PORT_XCOST_SLOTS (161 + PORT_CHARA_SLOTS) /* (the 86th added character and on: in its own block) */
extern s32 Port_CharaModelFile(s32 chara, s32 costume, s32 damaged); /* the model file of a costume, any costume */
extern int Port_CharaCostumeCount(int chara, int disc); /* the costumes of a character: `disc` its table's number */
#define CHARA_MODEL_FILE(c, costume, dmg) Port_CharaModelFile((s32)(c), (s32)(costume), (dmg) != 0)
/* For the screens with a roster (the same in each): an added character's own chip, name and number of item sets.
   PORT_CHIP: the texture list of a chip (`pack` = the screen's chip pack); the screen calls PortChips_Load(pack) after
   it has made the disc's chips ready and PortChips_Free() when it ends. PORT_NAME_LINE: the line of a name in the
   pack Port_CharaNamePack() made of the screen's. PORT_IS_CHARA: a cell id that is a character (the disc's tests
   are `id < 0xA1`, which would take an added character for a marker). */
extern void PortChips_Load(void *pack), PortChips_Free(void);
extern void *PortChips_Get(int chara), *Port_CharaNamePack(void *pack);
extern int Port_CharaNameLine(int chara), Port_CharaCount(void), Port_NetSession(void);
extern int Port_CharaForms(int chara, int *out, int max);
extern void Port_CharaCellLog(int cell, int chara, int forms); /* BT3_CHARA_LOG: the cells made for added characters */
#define PORT_CHIP(pack, id) \
    (PORT_CHARA_ADDED(id) && PortChips_Get(id) != NULL ? PortChips_Get(id) : (void *)MPACK_AT(pack, CHARA_T(id) + 1))
/* A disc character that a folder REPLACES (`replaces=` in its character.txt) keeps its id, and needs nothing of
   the above: its files are answered for the disc's own file numbers (PortCharas_Alias), PortChips_Load(pack) puts
   its chip over the disc's entry of the screen's pack, and Port_CharaNamePack / Port_CharaFormPack change its own
   line. */
/* A number of a character's package (`health=`, `ki=`, `blast=` in character.txt: which 0, 1, 2) in place of the
   value `v` of the game's table; `v` where no package says one, and always in an online session. */
extern int Port_CharaStat(int chara, int which, int value);
#define PORT_STAT(c, which, v) Port_CharaStat((s32)(c), which, v)
#define PORT_NAME_LINE(c) (PORT_CHARA_ADDED(c) ? Port_CharaNameLine(c) : (s32)(c))
/* The label under the name ("Super Saiyan"): the pack Port_CharaFormPack() made of the screen's, its line. */
extern void *Port_CharaFormPack(void *pack);
extern int Port_CharaFormLine(int chara);
#define PORT_FORM_LINE(c) (PORT_CHARA_ADDED(c) ? Port_CharaFormLine(c) : (s32)(c))
#define PORT_IS_CHARA(id) ((s32)(id) < 0xA1 || PORT_CHARA_ADDED(id))
/* Whether a cell may be drawn at random (the "random" cell of the selects, the tournament's computer entrants): a
   character of the disc, and an added one only with the setting "Added characters in random picks" (chara_pool,
   off unless set: the disc's tests are `id < 0xA1`). PORT_SAVE_CELL: whether a cell has item sets in the save
   (`custom[97]`, by the cell's place; an added cell lies past them). */
extern int Port_CharaPool(void);
#define PORT_IN_POOL(id) ((s32)(id) < 0xA1 || (PORT_CHARA_ADDED(id) && Port_CharaPool()))
#define PORT_SAVE_CELL(n) ((s32)(n) >= 0 && (s32)(n) < 97)
/* PORT_CELL_IN: whether a remembered cursor lies inside this screen's grid. The selects share one remembered
   cursor; left on an added character's cell, it lies past the end of a grid without added cells (Evolution Z,
   online), where the cell read as character 0 and its "item sets" were whatever follows custom[97] in the save:
   Evolution Z wrote there. PORT_SAVE_CHARA: the id to keep in the save for a character (an added one is kept as
   the character it is based on: the save stays good for a copy of the game without it). */
#define PORT_CELL_IN(n, count) ((s32)(n) >= 0 && (s32)(n) < (s32)(count))
#define PORT_SAVE_CHARA(c) (PORT_CHARA_ADDED(c) ? CHARA_T(c) : (s32)(c))
/* The grids of the screens with a roster hold 165 cells (the disc's ~98 and about 60 more). With added characters
   installed a screen builds its grid into PORT_GRID(which, own) instead of its own buffer `own`: room for every added
   character (port/src/plat_charas.c; `which` 0..7, one for each grid a screen can have open at once), and ChrGrid_Build
   asks Port_GridCap how many cells the buffer it fills holds. Without added characters (and so in every online
   session) the screen's own buffer, as on the disc. */
#define PORT_GRID_CELLS (((165 + PORT_CHARA_MAX) + 6) / 7 * 7)
extern void *Port_GridBuf(int which);
extern int Port_GridCap(const void *cells);
#define PORT_GRID(which, own) (Port_CharaCount() > 0 && Port_GridBuf(which) != NULL ? (void *)Port_GridBuf(which) : (void *)(own))
#define PORT_GRIDBUF(which, own) PORT_GRID(which, own)
#else
#define PORT_CHARA_ADDED(c) (0)
#define PORT_STAT(c, which, v) (v)
#define PORT_CHIP(pack, id) ((void *)MPACK_AT(pack, (id) + 1))
#define PORT_NAME_LINE(c) (c)
#define PORT_FORM_LINE(c) (c)
#define PORT_IS_CHARA(id) ((id) < 0xA1)
#define PORT_IN_POOL(id) ((id) < 0xA1)
#define PORT_SAVE_CELL(n) (1)
#define PORT_CELL_IN(n, count) (1)
#define PORT_SAVE_CHARA(c) (c)
#define PORT_GRIDBUF(which, own) (own)
#define CHARA_LINE_FILE(c, base, alt, line) ((base) + ((c) * 100 + (line)))
#define CHARA_FACE_FILE(c) ((c) + 0x2F9)
#define CHARA_MEMBER_FILE(c, side) ((c) * 2 + (side) + 8)
#define CHARA_T(c) (c)
#define CHARA_FILE10(c) ((c) * 10)
#define CHARA_FROM_FILE(id) (((id) - 0x590) / 10)
#define CHARA_VOICE_FILE(c, base, alt) ((c) + (base))
#endif

#endif

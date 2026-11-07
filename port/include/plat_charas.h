/* Characters added from outside the disc: one folder per character in `characters` next to the game, holding
   the files of the community's character packs as they come (see plat_charas.c). */
#ifndef PORT_PLAT_CHARAS_H
#define PORT_PLAT_CHARAS_H

/* the same numbers as include/port_chara.h: 57 ids from 0xC6, then 28 from 0xA5 (the Custom Character build) */
#define PORT_CHARA_FIRST 0xC6
#define PORT_CHARA_COUNT1 57
#define PORT_CHARA_FIRST2 0xA5
#define PORT_CHARA_COUNT2 28
#define PORT_CHARA_MAX (PORT_CHARA_COUNT1 + PORT_CHARA_COUNT2)
#define PORT_CHARA_ID(n) ((int)(n) < PORT_CHARA_COUNT1 ? PORT_CHARA_FIRST + (int)(n) : PORT_CHARA_FIRST2 + ((int)(n) - PORT_CHARA_COUNT1))
#define PORT_CHARA_IDX(c) ((int)(c) >= PORT_CHARA_FIRST ? (int)(c) - PORT_CHARA_FIRST : (int)(c) - PORT_CHARA_FIRST2 + PORT_CHARA_COUNT1)
#define PORT_CHARA_ADDED(c) (((int)(c) >= PORT_CHARA_FIRST && (int)(c) < PORT_CHARA_FIRST + PORT_CHARA_COUNT1) || \
                             ((int)(c) >= PORT_CHARA_FIRST2 && (int)(c) < PORT_CHARA_FIRST2 + PORT_CHARA_COUNT2))
#define PORT_CHARA_FILE_INDEX 71000  /* in the second archive: ten files per added character */
#define PORT_CHARA_VOICE_INDEX 72000 /* two per added character */
#define PORT_MODEL_MAX 0x180000 /* the largest model file taken (the original's buffer is 0xCE000) */
#define PORT_EFFECT_MAX 0x100000 /* the largest effects file taken (the original's buffer is 0xCE800) */
#define PORT_CHARA_MEMBER_INDEX 93000 /* two per added character: the fight's member block, a side each */
#define PORT_CHARA_FACE_INDEX 92000  /* one per added character: the large picture of the menus */
#define PORT_CHARA_LINE_INDEX 74000  /* two hundred per added character: its spoken lines, a hundred in each language */
/* Costumes 5 and on (include/port_chara.h): up to PORT_COSTUME_MAX a character, a disc character's from a package
   that replaces it, an added one's from its folder; their model files from the second archive's file 94000, 24 a
   character (two a costume: the model, the damaged one), the disc's 161 characters first, then the added ones. */
#define PORT_COSTUME_MAX 16
#define PORT_CHARA_XCOST_INDEX 94000
#define PORT_XCOST_PER ((PORT_COSTUME_MAX - 4) * 2)
#define PORT_XCOST_SLOTS (161 + PORT_CHARA_MAX)
int Port_CharaCostumeCount(int chara, int disc); /* how many a character has (`disc`: the disc table's number) */
int Port_CharaModelFile(int chara, int costume, int damaged); /* the file id of a costume's model */

void PortCharas_Init(void); /* reads the folder; once */
int PortCharas_Reload(void); /* reads it again (from the menus only; not in a session); 0 if not done */
int PortCharas_Root(char *out, unsigned size); /* the folder; 0 if there is none */
/* writes load_order.txt: the folders in order, each on or off; used by the next PortCharas_Reload */
int PortCharas_OrderWrite(const char (*folder)[128], const unsigned char *on, int n);
/* one key of a package's character.txt ("Name" or "Character/Form"): set (value NULL takes it out), read */
/* A package's forms (the folders inside it that are packages) and what one of them is, for the Mod Manager */
int PortCharas_Forms(const char *folder, char (*names)[128], int max);
int PortCharas_ManifestSet(const char *folder, const char *key, const char *value);
int PortCharas_ManifestGet(const char *folder, const char *key, char *out, unsigned size);
#define PORT_CHARA_FORMAT 1 /* the package format this game knows (`format=` of character.txt) */
enum { PORT_STAT_HEALTH, PORT_STAT_KI, PORT_STAT_BLAST, PORT_STAT_COUNT };
/* The folders looked at, for the settings window: every folder that is a package or was meant as one, in the
   order they were read (a character's inner folders after it). Also in a session, where none of them is used. */
enum { PORT_PKG_ADDS, PORT_PKG_REPLACES, PORT_PKG_OFF, PORT_PKG_DISABLED };
typedef struct PortCharaPkg {
    char folder[132];  /* "Name" or "Character/Form" */
    char name[64];     /* the name for the menus */
    char author[64], version[32], about[200]; /* the manifest's; "" where it says nothing */
    char problem[400]; /* what was found wrong with it, "" if nothing */
    int kind;          /* PORT_PKG_ADDS, PORT_PKG_REPLACES, PORT_PKG_OFF: not used (the problem says why), or
                          PORT_PKG_DISABLED: switched off in the load order (not read) */
    int id;            /* the id it got (an added character), the disc character it replaces; -1 */
} PortCharaPkg;
int Port_CharaPkgCount(void);
const PortCharaPkg *Port_CharaPkg(int index);
/* A number of a package (`health=`, `ki=`, `blast=`) for a character, or `value` where none is said. */
int Port_CharaStat(int chara, int which, int value);
int PortCharas_Epoch(void); /* changes when a session begins or ends: what a file number answers may change with it */
int Port_CharaCount(void);     /* the added characters; 0 in an online session, as everything here then answers */
int Port_CharaInstalled(void); /* how many the folder holds, in a session too */
int Port_CharaBase(int chara);           /* an added character's base character; any other id as it is */
int Port_CharaCostumes(int chara);       /* how many costumes an added character's folder has (0: not an added one) */
const char *Port_CharaName(int index);   /* the folder's name */
/* 1 if the file the game asks for is an added character's; `out` gets the path of the file in its folder, or of
   the base character's file where the folder has none (relative to the data root). */
int PortCharas_Alias(const char *rel, char *out, unsigned n);
/* The folder's chip for the character select (`chip.bin`: one entry of the disc's chip pack, 5,504 bytes), read
   afresh into memory the game can address; NULL if the folder has none. The caller relocates it as it does the
   disc's entries, so it is loaded once per visit of a screen and then asked for with Port_CharaChip. */
void *Port_CharaChipLoad(int index);
void *Port_CharaChip(int chara);
/* The same for a whole screen: PortChips_Load copies every folder's chip into the game's own memory (a picture
   is fetched by the renderer from the game's memory) and makes it ready as the screen does the disc's;
   PortChips_Get gives an added character's (NULL: none, the base character's is shown); PortChips_Free gives the
   memory back when the screen ends. One screen at a time. */
/* "Added characters in random picks" (setting chara_pool, off unless set; BT3_CHARA_POOL=1): the random cell of
   the select screens and the tournament's computer entrants may be added characters. */
int Port_CharaPool(void);
void Port_CharaPoolSet(int on);
void PortChips_Load(void *pack);
void *PortChips_Get(int chara);
void PortChips_Free(void);
/* The names of a screen with the added characters' after them: a copy of the screen's names pack (`pack`, the
   disc's: a count, offsets, entries of UTF-16 text) with one entry more per added character, from the folder's
   `name.txt` or else the folder's name. Port_CharaNameLine(chara) is an added character's line in it. */
/* Transformations and fusions between added characters. A pack's own parameter table names its other forms by the
   character numbers of whoever made it; `forms.txt` in the folder says which FOLDER each one is:
       left=Fighter_119        (the four transformation slots: left, up, right, down)
       fusion1=Fighter_104     (the three fusion results: fusion1..fusion3)
       down=disc:3         (or a character of the disc, by its id)
   Port_CharaFormTarget gives the id for a slot (0..3 transformations, 4..6 fusions): the folder's id if it is
   installed, 0xFF (none) if it is not, and -1 where forms.txt says nothing (the table's own number counts). */
int Port_CharaFormTarget(int chara, int slot);
int Port_CharaBurst(int chara, int slot); /* a transformation's burst colour 0xRRGGBB, -1 the game's */
/* For the character select, where the disc shows a character's forms under one cell: the forms of an added
   character, itself first, then those its transformations lead to (through forms.txt), at most `max`. Returns
   their number (1: a character on its own), or -1 if `chara` is itself a form of another installed character and
   has no cell of its own. A character nobody transforms into is the head of its group; "down" is not counted as
   leading to a form (it is how a form goes back). */
int Port_CharaForms(int chara, int *out, int max);
void *Port_CharaNamePack(void *pack);
int Port_CharaNameLine(int chara);

/* What a folder of `characters` holds (PortCharas_Describe): for the Mod Manager. */
enum { PORT_DESC_PORTRAIT = 12, PORT_DESC_CHIP, PORT_DESC_MEMBER, PORT_DESC_LINES, PORT_DESC_LINKS = 19 };
typedef struct PortCharaDesc {
    char name[64];     /* the name for the menus: the manifest's, else the folder's */
    int nameSet;
    char label[96];    /* the form's label */
    int labelSet;
    int replaces;      /* the disc character it replaces; -1 it adds one */
    int base;          /* the disc character it stands on (a replacement: the one it replaces) */
    int costumes;      /* model files in a row from the first (up to PORT_COSTUME_MAX) */
    int files;         /* bit k: file k of the twelve (0..3 costumes, 4..7 damaged, 8 animations, 9 effects, 10 / 11 the
                          voice banks), and PORT_DESC_PORTRAIT, _CHIP, _MEMBER, _LINES */
    int large;         /* a model larger than the original's buffer */
    char link[PORT_DESC_LINKS][64]; /* the links as written: left, up, right, down, fusion1..3, partners */
    int stat[PORT_STAT_COUNT];      /* health, ki, blast stocks; -1 not said */
    char problem[400]; /* what is wrong with it, "" nothing */
} PortCharaDesc;
int PortCharas_Describe(const char *folder, PortCharaDesc *out);
int PortCharas_DescribeForm(const char *folder, const char *form, PortCharaDesc *out); /* a form of it (PortCharas_Forms) */
int PortCharas_DescribePath(const char *path, PortCharaDesc *out); /* a folder anywhere (the browser's) */
/* the folders of `characters` in the order they are used (the load order's, then the others by name), each on or not */
int PortCharas_OrderGet(char (*folder)[128], unsigned char *on, int max);
/* the Mod Manager's preview: the folder at path answers for disc character as's files until changed (NULL / -1: none) */
int PortCharas_Preview(const char *path, int as);

#endif

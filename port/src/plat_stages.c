/* The manifest of stages added from outside the disc (see plat_stages.h). Runs before the game's menus: the ids,
   the display names and the file aliases the game and the file layer use all come from here.

   Two kinds of added stage, both in the stages folder:
     - a loose model, `<name>.unk` (optionally listed in maps.txt, with `<name>.thumb` and `<name>.icon` beside it);
     - a stage package: a folder with a `stage.txt` and the stage's files (docs/port/adding_stages.md, "Stage
       packages"): its model, split-screen model, sound bank, the select's picture, small picture and name picture,
       the scene of its destruction, and which stage it turns into when destroyed.
   The loose ones take their ids first (as before packages existed), then the packages, by folder name. */
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "plat_stages.h"

/* The added stages' files in the second archive, past its end (65,201 files); per kind a hundred numbers, one per
   added stage. The model's number is PORT_ADDED_STAGE_FILE in src/battle/battle_load.c, the picture's is in
   include/menu/menu_d.h. */
#define PORT_STAGE_FILE_INDEX 70000  /* the model (one screen) */
#define PORT_STAGE_THUMB_INDEX 70100 /* the select's picture of an added stage (include/menu/menu_d.h) */
#define PORT_STAGE_SPLIT_INDEX 70200 /* a package's split-screen model */
#define PORT_STAGE_BANK_INDEX 70300  /* a package's sound bank */
#define PORT_STAGE_TRANS_INDEX 70400 /* a package's destruction scene */
#define PORT_STAGE_THUMB_MAX 0x3B800 /* the select's buffer for it */
#define FILE_SECOND 0xD48            /* the second archive's first file id */
#define FILE_SND_STAGE 0x14E         /* + disc stage: its sound bank (BTL_FILE_SND_STAGE) */
#define FILE_TRANSITION 0x1C0        /* the disc's destruction scene (BTL_FILE_TRANSITION) */

char gPortStageNames[PORT_STAGE_MAX][64];
int gPortExtraStageCount;
int gPortExtraStages[PORT_STAGE_MAX];

typedef struct StageAlias {
    char rel[48];   /* the path the game asks for, under the data root */
    char target[448]; /* where it is served from */
} StageAlias;

/* What a package says beyond its files (a loose model has none of it). */
typedef struct StageInfo {
    char folder[128];    /* a package's folder name (what another package's destroyed= names), "" for a loose model */
    char destroyed[128]; /* stage.txt destroyed=: a folder, or disc:<id> */
    int target;          /* ... resolved: the stage it turns into, -1 none */
    int placement;       /* the disc stage whose places a technique's scene takes (BtlSuper_GetStagePlacement) */
    int rules;           /* the stage id the fight's stage rules see, -1 for none of the disc's special cases */
    int split, bank, trans; /* the package has its own split-screen model / sound bank / destruction scene */
    unsigned char *title;   /* its name picture (RGBA), NULL without */
    int titleW, titleH;
} StageInfo;

static StageAlias sAliases[PORT_STAGE_MAX * 5];
static int sAliasCount;
static StageInfo sInfo[PORT_STAGE_MAX];
static int sDone;
static long sLargest; /* the largest added stage file */

/* The game loads every stage of a battle into one buffer of 0x6CB800 bytes (BTL_STAGE_BUF_SIZE), sized for the
   disc's stages on a console with 32 MB. A stage made for the port can be larger: the buffer then is as large as
   the largest added stage, up to PORT_STAGE_BUF_MAX (it comes out of the game's own 28 MB heap, which a battle
   also needs for its fighters). With no larger stage installed, and in an online session (where added stages are
   not offered), it is the game's own size, so nothing about the game's memory changes. BT3_STAGE_BUF=<bytes>
   names a size (testing: a stage put in a disc stage's place through the mods folder). */
#define PORT_STAGE_BUF_GAME 0x6CB800
#define PORT_STAGE_BUF_MAX 0x4000000 /* 64 MB: a larger buffer than the game's is not in its heap (Port_GameBigAlloc) */
static char sDir[256]; /* the folder the stages are in (plat_extras.c) */
static char sFiles[PORT_STAGE_MAX][128];

extern int PortExtras_Dir(const char *kind, const char *env, char *out, unsigned size);
extern int PortExtras_Scan(const char *dir, const char *ext, char names[][128], int max);
extern void PortExtras_NameFromFile(const char *file, char *name, unsigned size);

void PortStages_Init(void);

const char *PortStages_Dir(void) {
    PortStages_Init(); /* (the overlay may ask before the game has started) */
    return sDir;
}

static char *trim(char *s) {
    char *e;
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) {
        *--e = '\0';
    }
    return s;
}

static long file_size(const char *path) {
    FILE *fp = fopen(path, "rb");
    long size = -1;
    if (fp != NULL) {
        fseek(fp, 0, SEEK_END);
        size = ftell(fp);
        fclose(fp);
    }
    return size;
}

enum { STAGE_ICON_SIZE = 5504 };
static unsigned char sIcon[PORT_STAGE_MAX][STAGE_ICON_SIZE + 1];
static unsigned char sIconHave[PORT_STAGE_MAX];
static unsigned char *sIconMem; /* the icons in the game's memory, while the select is up */
static void *sIconRes[PORT_STAGE_MAX];
extern void *Heap_Alloc(int size, int align, int fromTail, int heap); /* the game's (src/sys/heap.c) */
extern void Heap_Free(void *ptr);
extern void Res_RelocateOffsets(void *out, void *base, void *hdr); /* src/sys/gfxm_b_b.c */

/* The added stages' own icons, made ready as the select makes the disc's (in the game's memory: its 32-bit
   pointers must reach them). The select calls Load when it has set up the disc's and Free when it ends. */
void PortStageIcons_Free(void) {
    extern int Port_NetSession(void); /* gs/net.c */
    if (Port_NetSession()) { /* (online: no added stage, and none of the game's memory taken; as PortChips_Free) */
        return;
    }
    if (sIconMem != NULL) {
        Heap_Free(sIconMem);
        sIconMem = NULL;
    }
    memset(sIconRes, 0, sizeof(sIconRes));
}

void PortStageIcons_Load(void) {
    int i;
    extern int Port_NetSession(void); /* gs/net.c */
    if (Port_NetSession()) {
        return;
    }
    PortStageIcons_Free();
    if (gPortExtraStageCount == 0) {
        return;
    }
    sIconMem = Heap_Alloc(gPortExtraStageCount * STAGE_ICON_SIZE, 0x40, 0, 2);
    for (i = 0; sIconMem != NULL && i < gPortExtraStageCount; i++) {
        if (sIconHave[i]) {
            void *res = sIconMem + i * STAGE_ICON_SIZE;
            memcpy(res, sIcon[i], STAGE_ICON_SIZE);
            sIconRes[i] = res;
            Res_RelocateOffsets(&res, res, res);
        }
    }
}

/* The icon of stage `id` (0x24 on), or NULL where its file has none. */
void *PortStageIcons_Get(int id) {
    extern int Port_NetSession(void); /* gs/net.c */
    int i = id - PORT_STAGE_FIRST;
    return i >= 0 && i < gPortExtraStageCount && !Port_NetSession() ? sIconRes[i] : NULL;
}

static void alias(int index, const char *target) {
    if (sAliasCount >= (int)(sizeof(sAliases) / sizeof(sAliases[0]))) {
        return;
    }
    snprintf(sAliases[sAliasCount].rel, sizeof(sAliases[sAliasCount].rel), "pzs3us2/%05d.bin", index);
    snprintf(sAliases[sAliasCount].target, sizeof(sAliases[sAliasCount].target), "%s", target);
    sAliasCount++;
}

/* The files of one added stage, full paths ("" where it has none). */
typedef struct StageFiles {
    char model[512], thumb[512], icon[512], split[512], bank[512], trans[512], title[512];
} StageFiles;

/* A name picture: the 12-byte header of a name strip (width, height, count 1), then RGBA. */
static unsigned char *read_title(const char *path, int *w, int *h) {
    FILE *fp = fopen(path, "rb");
    unsigned int hdr[3];
    unsigned char *px = NULL;
    if (fp == NULL) {
        return NULL;
    }
    if (fread(hdr, sizeof(hdr), 1, fp) == 1 && hdr[0] > 0 && hdr[0] <= 2048 && hdr[1] > 0 && hdr[1] <= 512 && hdr[2] >= 1) {
        size_t n = (size_t)hdr[0] * hdr[1] * 4;
        px = malloc(n);
        if (px != NULL && fread(px, 1, n, fp) != n) {
            free(px);
            px = NULL;
        }
        *w = (int)hdr[0];
        *h = (int)hdr[1];
    }
    fclose(fp);
    return px;
}

/* Adds one added stage: its next id, its display name, and the file aliases it needs. `key` tells one from
   another (a loose model's file name, a package's folder). Returns its index, or -1. */
static int add_stage(const char *key, const char *name, const StageFiles *f) {
    int id, i;
    long size;

    if (gPortExtraStageCount >= PORT_STAGE_MAX) {
        fprintf(stderr, "bt3: stages: more than %d added maps, the rest are ignored (%s)\n", PORT_STAGE_MAX, key);
        return -1;
    }
    for (i = 0; i < gPortExtraStageCount; i++) {
        if (strcmp(sFiles[i], key) == 0) {
            return -1; /* (in the list and found in the folder: once) */
        }
    }
    /* its size decides the buffer the game loads stages into (Port_StageBufSize) */
    size = file_size(f->model);
    if (size <= 0) {
        fprintf(stderr, "bt3: stages: %s has no model (%s): left out\n", key, f->model);
        return -1;
    }
    if (size > PORT_STAGE_BUF_MAX) {
        fprintf(stderr, "bt3: stages: %s is %ld bytes, more than the %d a stage can have here: left out\n", key, size, PORT_STAGE_BUF_MAX);
        return -1;
    }
    i = gPortExtraStageCount;
    memset(&sInfo[i], 0, sizeof(sInfo[i]));
    sInfo[i].target = -1;
    sInfo[i].rules = -1;
    if (size > sLargest) {
        sLargest = size;
    }
    snprintf(sFiles[i], sizeof(sFiles[i]), "%s", key);
    id = PORT_STAGE_FIRST + i;
    snprintf(gPortStageNames[i], 64, "%s", name);
    gPortExtraStages[i] = id;

    /* Its model: the game asks for a file id of the stage's own, 70000 + n past the start of the second archive
       (battle_load.c, PORT_ADDED_STAGE_FILE), where the disc has nothing. (It used to be 0x171 + stage, which is
       also the split-screen model of a disc stage from the fourth added stage on; and a second redirection, for
       a sound bank at 0x14E + stage, took the place of the disc stages' own models, the first added stage that
       of stage 1: with a stage added, a one-player fight on that disc stage was given the wrong file.) */
    alias(PORT_STAGE_FILE_INDEX + i, f->model);
    {
        /* Its picture in the stage select (the disc's own format: a stage's file 0x39D + stage copied out), or the
           disc thumbnail an added stage has always borrowed. */
        char disc[48];
        long ts = f->thumb[0] != '\0' ? file_size(f->thumb) : -1;
        if (ts > 0 && ts <= PORT_STAGE_THUMB_MAX) {
            alias(PORT_STAGE_THUMB_INDEX + i, f->thumb);
        } else {
            if (ts > PORT_STAGE_THUMB_MAX) {
                fprintf(stderr, "bt3: stages: the picture of %s is larger than the select takes; a disc one is shown\n", key);
            }
            snprintf(disc, sizeof(disc), "pzs3us1/%05d.bin", 0x39D - 1 + i % 36);
            alias(PORT_STAGE_THUMB_INDEX + i, disc);
        }
    }
    /* Its small picture in the select's reel: one entry of the select's stage icons (section 46 of its pack, 5504
       bytes) as a disc has it; kept here until the select asks (PortStageIcons_Load). */
    sIconHave[i] = 0;
    if (f->icon[0] != '\0') {
        FILE *fp = fopen(f->icon, "rb");
        if (fp != NULL) {
            sIconHave[i] = fread(sIcon[i], 1, STAGE_ICON_SIZE + 1, fp) == STAGE_ICON_SIZE;
            fclose(fp);
        }
    }
    /* A package's own split-screen model, sound bank and destruction scene (without them: its one model in split
       screen too, the first disc stage's sound bank, the disc's scene) */
    if (f->split[0] != '\0' && (size = file_size(f->split)) > 0 && size <= PORT_STAGE_BUF_MAX) {
        alias(PORT_STAGE_SPLIT_INDEX + i, f->split);
        sInfo[i].split = 1;
        if (size > sLargest) {
            sLargest = size;
        }
    }
    if (f->bank[0] != '\0' && file_size(f->bank) > 0) {
        alias(PORT_STAGE_BANK_INDEX + i, f->bank);
        sInfo[i].bank = 1;
    }
    if (f->trans[0] != '\0' && file_size(f->trans) > 0) {
        alias(PORT_STAGE_TRANS_INDEX + i, f->trans);
        sInfo[i].trans = 1;
    }
    if (f->title[0] != '\0') {
        sInfo[i].title = read_title(f->title, &sInfo[i].titleW, &sInfo[i].titleH);
    }
    gPortExtraStageCount++;
    return i;
}

/* A loose model in the folder (or a line of maps.txt): `<file>` with `<name>.thumb` and `<name>.icon` beside it. */
static void add_loose(const char *file, const char *name) {
    StageFiles f;
    char *dot;
    memset(&f, 0, sizeof(f));
    snprintf(f.model, sizeof(f.model), "%s/%s", sDir, file);
    snprintf(f.thumb, sizeof(f.thumb), "%s", f.model);
    dot = strrchr(f.thumb, '.');
    if (dot != NULL && dot > strrchr(f.thumb, '/')) {
        strcpy(dot, ".thumb");
        snprintf(f.icon, sizeof(f.icon), "%s", f.thumb);
        strcpy(strrchr(f.icon, '.'), ".icon");
    } else {
        f.thumb[0] = '\0';
    }
    add_stage(file, name, &f);
}

/* A stage package: the folder `dir` (under the stages folder) with its stage.txt. Lines `key=value`:
     name=        the name in the menus (else the folder's name)
     model= split= bank= thumb= icon= title= transition=   its files (these names by default: model.unk,
                  split.unk, bank.bin, thumb.bin, icon.bin, name.rgba, transition.bin; each but the model may be
                  missing)
     destroyed=   the stage it turns into when destroyed: another package's folder, or disc:<id>
     placement=   the disc stage (0..34) whose places the techniques' scenes take on it (0 without)
     rules=       the disc stage id the fight's stage rules take it for (ring-out, the bonuses of some abilities);
                  without it none of the disc's special cases
     enabled=0    left out */
static void add_package(const char *dir) {
    char path[512], line[512], name[64];
    const char *key[7] = {"model", "split", "bank", "thumb", "icon", "title", "transition"};
    const char *def[7] = {"model.unk", "split.unk", "bank.bin", "thumb.bin", "icon.bin", "name.rgba", "transition.bin"};
    char file[7][128];
    char destroyed[128] = "";
    int placement = 0, rules = -1, k, i;
    StageFiles f;
    FILE *fp;

    snprintf(name, sizeof(name), "%s", dir);
    for (k = 0; k < 7; k++) {
        snprintf(file[k], sizeof(file[k]), "%s", def[k]);
    }
    snprintf(path, sizeof(path), "%s/%s/stage.txt", sDir, dir);
    fp = fopen(path, "rb");
    if (fp == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), fp) != NULL) {
        char *eq, *v, *t = trim(line);
        if (t[0] == '#' || (eq = strchr(t, '=')) == NULL) {
            continue;
        }
        *eq = '\0';
        v = trim(eq + 1);
        t = trim(t);
        if (strcmp(t, "name") == 0) {
            snprintf(name, sizeof(name), "%s", v);
        } else if (strcmp(t, "destroyed") == 0) {
            snprintf(destroyed, sizeof(destroyed), "%s", v);
        } else if (strcmp(t, "placement") == 0) {
            placement = (int)strtol(v, NULL, 0);
        } else if (strcmp(t, "rules") == 0) {
            rules = (int)strtol(v, NULL, 0);
        } else if (strcmp(t, "enabled") == 0 && strtol(v, NULL, 0) == 0) {
            fclose(fp);
            return;
        } else {
            for (k = 0; k < 7; k++) {
                if (strcmp(t, key[k]) == 0) {
                    snprintf(file[k], sizeof(file[k]), "%s", v);
                }
            }
        }
    }
    fclose(fp);
    memset(&f, 0, sizeof(f));
    {
        char *dst[7] = {f.model, f.split, f.bank, f.thumb, f.icon, f.title, f.trans};
        for (k = 0; k < 7; k++) {
            if (file[k][0] != '\0') {
                snprintf(dst[k], 512, "%s/%s/%s", sDir, dir, file[k]);
            }
        }
    }
    i = add_stage(dir, name, &f);
    if (i < 0) {
        return;
    }
    snprintf(sInfo[i].folder, sizeof(sInfo[i].folder), "%s", dir);
    snprintf(sInfo[i].destroyed, sizeof(sInfo[i].destroyed), "%s", destroyed);
    sInfo[i].placement = placement >= 0 && placement < 35 ? placement : 0;
    sInfo[i].rules = rules >= 0 && rules < PORT_STAGE_FIRST ? rules : -1;
}

static int ci_cmp(const void *a, const void *b) {
    const unsigned char *x = a, *y = b;
    for (; *x != '\0' && tolower(*x) == tolower(*y); x++, y++) {
    }
    return tolower(*x) != tolower(*y) ? tolower(*x) - tolower(*y) : strcmp(a, b);
}

/* The folders of the stages folder that hold a stage.txt, by name without regard to case (the same order, so the
   same ids, on every machine). */
static int scan_packages(char names[][128], int max) {
    DIR *d = opendir(sDir);
    struct dirent *de;
    int n = 0;
    if (d == NULL) {
        return 0;
    }
    while ((de = readdir(d)) != NULL && n < max) {
        char path[512];
        struct stat st;
        if (de->d_name[0] == '.' || strlen(de->d_name) >= 128) {
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s/stage.txt", sDir, de->d_name);
        if (stat(path, &st) == 0 && (st.st_mode & S_IFMT) == S_IFREG) {
            snprintf(names[n++], 128, "%s", de->d_name);
        }
    }
    closedir(d);
    qsort(names, (size_t)n, 128, ci_cmp);
    return n;
}

/* The destroyed= of every package, now that all have their ids. */
static void resolve_targets(void) {
    int i, k;
    for (i = 0; i < gPortExtraStageCount; i++) {
        const char *t = sInfo[i].destroyed;
        if (t[0] == '\0') {
            continue;
        }
        if (strncmp(t, "disc:", 5) == 0) {
            int id = (int)strtol(t + 5, NULL, 0);
            sInfo[i].target = id >= 0 && id < 0x23 ? id : -1;
        } else {
            for (k = 0; k < gPortExtraStageCount; k++) {
                if (k != i && sInfo[k].folder[0] != '\0' && ci_cmp(sInfo[k].folder, t) == 0) {
                    sInfo[i].target = PORT_STAGE_FIRST + k;
                }
            }
        }
        if (sInfo[i].target < 0) {
            fprintf(stderr, "bt3: stages: %s turns into \"%s\", which is not installed: it is not destroyed into another stage\n",
                    sInfo[i].folder, t);
        }
    }
}

/* No manifest: the old comma-separated BT3_EXTRA_STAGES=0x24,0x25 still names the ids (aliases then come from
   BT3_FILE_ALIAS, which plat_file.c reads). Kept so the tests and a manual run still work. */
static void from_env(void) {
    const char *p = getenv("BT3_EXTRA_STAGES");

    while (p != NULL && *p != '\0' && gPortExtraStageCount < PORT_STAGE_MAX) {
        char *end;
        long id = strtol(p, &end, 0);
        if (end == p) {
            break;
        }
        sInfo[gPortExtraStageCount].target = -1;
        sInfo[gPortExtraStageCount].rules = -1;
        gPortExtraStages[gPortExtraStageCount] = (int)id;
        gPortStageNames[gPortExtraStageCount][0] = '\0';
        gPortExtraStageCount++;
        p = end;
        while (*p == ',' || *p == ' ') {
            p++;
        }
    }
}

void PortStages_Init(void) {
    char path[512];
    char line[256];
    FILE *fp;
    int loose, packages = 0;

    if (sDone) {
        return;
    }
    sDone = 1;
    if (!PortExtras_Dir("stages", "BT3_STAGES", sDir, sizeof(sDir))) {
        from_env();
        if (gPortExtraStageCount > 0) {
            fprintf(stderr, "bt3: stages: %d from BT3_EXTRA_STAGES (no stages folder)\n", gPortExtraStageCount);
        }
        return;
    }
    /* the list first (its names and its order), then whatever else is in the folder, by name */
    snprintf(path, sizeof(path), "%s/maps.txt", sDir);
    fp = fopen(path, "rb");
    while (fp != NULL && fgets(line, sizeof(line), fp) != NULL) {
        char *bar, *file;
        if (line[0] == '\0' || line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        bar = strchr(line, '|');
        if (bar == NULL) {
            continue;
        }
        *bar = '\0';
        file = trim(line);
        add_loose(file, trim(bar + 1));
    }
    if (fp != NULL) {
        fclose(fp);
    }
    {
        static char found[PORT_STAGE_MAX * 2][128];
        int n = PortExtras_Scan(sDir, ".unk", found, PORT_STAGE_MAX * 2), k;
        for (k = 0; k < n; k++) {
            char name[64];
            PortExtras_NameFromFile(found[k], name, sizeof(name));
            add_loose(found[k], name);
        }
        loose = gPortExtraStageCount;
        /* then the packages, by folder name */
        n = scan_packages(found, PORT_STAGE_MAX * 2);
        for (k = 0; k < n; k++) {
            add_package(found[k]);
        }
        packages = gPortExtraStageCount - loose;
    }
    resolve_targets();
    if (gPortExtraStageCount > 0) {
        fprintf(stderr, "bt3: stages: %d added from %s (%d package%s, ids 0x%02x..0x%02x)\n", gPortExtraStageCount, sDir, packages,
                packages == 1 ? "" : "s", PORT_STAGE_FIRST, PORT_STAGE_FIRST + gPortExtraStageCount - 1);
    }
}

int PortStages_Count(void) {
    return gPortExtraStageCount;
}

const char *PortStages_Name(int index) {
    if (index < 0 || index >= gPortExtraStageCount) {
        return "";
    }
    return gPortStageNames[index];
}

/* The index-th added stage's own name picture (RGBA, *w x *h), NULL where it has none. */
const unsigned char *PortStages_Title(int index, int *w, int *h) {
    if (index < 0 || index >= gPortExtraStageCount || sInfo[index].title == NULL) {
        return NULL;
    }
    *w = sInfo[index].titleW;
    *h = sInfo[index].titleH;
    return sInfo[index].title;
}

int PortStages_Alias(const char *rel, char *out, unsigned n) {
    int i;
    for (i = 0; i < sAliasCount; i++) {
        if (strcmp(sAliases[i].rel, rel) == 0) {
            snprintf(out, n, "%s", sAliases[i].target);
            if (getenv("BT3_STAGES_DEBUG") != NULL) { /* (testing: which added stage's files a fight reads) */
                fprintf(stderr, "bt3: stages: %s is %s\n", rel, out);
            }
            return 1;
        }
    }
    return 0;
}

/* ---- what the game asks of an added stage (src/battle) ---------------------------------------------------- */

static int index_of(int stage) {
    int i = stage - PORT_STAGE_FIRST;
    return i >= 0 && i < gPortExtraStageCount ? i : -1;
}

/* 1 for a disc stage and an installed added one: a replay or a save naming an added stage this copy does not
   have gets a disc stage instead (Port_StageCheck). */
int Port_StageValid(int stage) {
    PortStages_Init();
    return (stage >= 0 && stage < PORT_STAGE_FIRST) || index_of(stage) >= 0;
}

/* The file ids of an added stage's split-screen model, sound bank and destruction scene. */
int Port_StageSplitFile(int stage) {
    int i = index_of(stage);
    return FILE_SECOND + (i >= 0 && sInfo[i].split ? PORT_STAGE_SPLIT_INDEX : PORT_STAGE_FILE_INDEX) + (i >= 0 ? i : 0);
}

int Port_StageBankFile(int stage) {
    int i = index_of(stage);
    return i >= 0 && sInfo[i].bank ? FILE_SECOND + PORT_STAGE_BANK_INDEX + i : FILE_SND_STAGE;
}

/* The scene shown while stage `from` is destroyed: the package's own, else 0 (the game's choice). */
int Port_StageTransitionFile(int from) {
    int i = index_of(from);
    return i >= 0 && sInfo[i].trans ? FILE_SECOND + PORT_STAGE_TRANS_INDEX + i : 0;
}

/* What an added stage turns into when destroyed (-1: nothing). */
int Port_StageChangeTarget(int stage) {
    int i = index_of(stage);
    return i >= 0 ? sInfo[i].target : -1;
}

/* The disc stage whose places the techniques' scenes take (their tables have the disc's stages only). */
int Port_StagePlacement(int stage) {
    int i;
    if (stage >= 0 && stage < PORT_STAGE_FIRST) {
        return stage;
    }
    i = index_of(stage);
    return i >= 0 ? sInfo[i].placement : 0;
}

/* The stage id the fight's stage rules see (ring-out, the abilities' stage bonuses, ...): a disc stage itself,
   an added one what its package says, else -1 (matches none of the disc's special cases). */
int Port_StageRules(int stage) {
    int i;
    if (stage >= 0 && stage < PORT_STAGE_FIRST) {
        return stage;
    }
    i = index_of(stage);
    return i >= 0 ? sInfo[i].rules : -1;
}

int Port_StageBufSize(void) {
    extern int Port_NetSession(void); /* gs/net.c */
    const char *e = getenv("BT3_STAGE_BUF");
    long size = PORT_STAGE_BUF_GAME;

    PortStages_Init();
    if (e != NULL) {
        size = strtol(e, NULL, 0);
    } else if (!Port_NetSession() && sLargest > size) {
        size = sLargest;
    }
    if (size < PORT_STAGE_BUF_GAME) {
        size = PORT_STAGE_BUF_GAME;
    }
    if (size > PORT_STAGE_BUF_MAX) {
        size = PORT_STAGE_BUF_MAX;
    }
    return (int)((size + 0x7FF) & ~0x7FFL); /* whole sectors, as the reads are */
}

/* A stage file about to be read: 0 if it does not fit the buffer (the caller says so and stops; reading it
   would overrun the buffer and corrupt the game's heap, which showed as a crash seconds later). */
int Port_StageFits(const char *rel, long size) {
    int index = -1;
    if (strncmp(rel, "pzs3us2/", 8) == 0 && sscanf(rel + 8, "%d", &index) == 1 &&
        ((index >= PORT_STAGE_FILE_INDEX && index < PORT_STAGE_FILE_INDEX + PORT_STAGE_MAX) ||
         (index >= PORT_STAGE_SPLIT_INDEX && index < PORT_STAGE_SPLIT_INDEX + PORT_STAGE_MAX))) {
        return size <= Port_StageBufSize(); /* an added stage */
    }
    if (strncmp(rel, "pzs3us1/", 8) != 0 || sscanf(rel + 8, "%d", &index) != 1) {
        return 1;
    }
    /* the disc's stage models: 368..402 for one screen, 407..441 split (a file in mods/ may stand in for one) */
    if ((index >= 368 && index <= 402) || (index >= 407 && index <= 441)) {
        return size <= Port_StageBufSize();
    }
    return 1;
}

/* BT3_TEST_STAGE=<id> (or BT3_STAGE=<id>): every battle is set up on that stage, whatever the menus chose
   (testing: a recorded session or the attract demo can then be played on any stage, an added one too: 0x24 is the
   first). -1 without it. */
int Port_TestStage(void) {
    const char *e = getenv("BT3_TEST_STAGE");
    if (e == NULL) {
        e = getenv("BT3_STAGE");
    }
    return e != NULL ? (int)strtol(e, NULL, 0) : -1;
}

/* BT3_TEST_DESTROY=<n>: the stage is destroyed every n stage updates of a fight (testing a destruction chain:
   src/battle/stg_b.c, BtlStage_Update). 0 without it. */
int Port_TestDestroy(void) {
    static int v = -1;
    if (v < 0) {
        const char *e = getenv("BT3_TEST_DESTROY");
        v = e != NULL ? atoi(e) : 0;
    }
    return v;
}

/* BT3_TEST_SCREEN=<mode>: the same for the screen mode (0 one screen, 1 split): a two-player recording then
   loads the one-screen model of its stage. */
int Port_TestScreen(void) {
    const char *e = getenv("BT3_TEST_SCREEN");
    return e != NULL ? (int)strtol(e, NULL, 0) : -1;
}

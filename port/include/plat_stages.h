/* Stages added from outside the disc: a manifest under the data root (<BT3_DATA>/stages/maps.txt), parsed once
   at start-up. Every line `<file>|<display name>` adds one map as the next stage id (PORT_STAGE_FIRST onward):
   the port serves its model (and a borrowed sound bank) for the game's file ids, and the menus list it. This
   replaces having to spell out BT3_EXTRA_STAGES / BT3_FILE_ALIAS by hand. */
#ifndef PORT_PLAT_STAGES_H
#define PORT_PLAT_STAGES_H

#define PORT_STAGE_FIRST 0x24 /* first added stage id: the disc's stages are 0x00..0x23 */
#define PORT_STAGE_MAX 64     /* ids 0x24..0x63 (the grid's locked / empty markers are 0x7E, 0x7F; added stages are
                                 never looked up in the save's 64-bit stage bits) */

void PortStages_Init(void); /* reads the manifest (or the BT3_EXTRA_STAGES fallback); once */
int PortStages_Count(void);
const char *PortStages_Name(int index); /* the display name of the index-th added map */
/* 1 if the file the game asks for (its path under the data root, e.g. "pzs3us1/00404.bin") is served from
   somewhere else; `out` gets the replacement path (under the data root, or absolute). */
int PortStages_Alias(const char *rel, char *out, unsigned n);
/* The index-th added stage's own name picture (a package's name.rgba: RGBA, *w x *h), NULL where it has none. */
const unsigned char *PortStages_Title(int index, int *w, int *h);

/* What the battle asks of a stage id (an added one from its package; a disc one is itself): */
int Port_StageValid(int stage);           /* a disc stage or an installed added one */
int Port_StageSplitFile(int stage);       /* file id of an added stage's split-screen model (its model without one) */
int Port_StageBankFile(int stage);        /* file id of an added stage's sound bank (the first disc stage's without) */
int Port_StageTransitionFile(int from);   /* file id of the scene of its destruction, 0: the game's */
int Port_StageChangeTarget(int stage);    /* the stage it turns into when destroyed, -1 none */
int Port_StagePlacement(int stage);       /* the disc stage whose places the technique scenes take */
int Port_StageRules(int stage);           /* the stage id the fight's stage rules see, -1 none of the special cases */

/* The game reads these (small data, a low address: see the note in menu_c_e.c). */
extern char gPortStageNames[PORT_STAGE_MAX][64];
extern int gPortExtraStageCount;
extern int gPortExtraStages[PORT_STAGE_MAX];

#endif

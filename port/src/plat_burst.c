/* A transformation's burst in a colour of the package's own (character.txt `<slot>_burst=rrggbb`, made by the Custom
 * Character screen's "burst colour").
 *
 * The burst is EftTransform (src/battle/eft_ab_b.c): one of eight emitter sets of common file 3 (entries 0x192,
 * 0x1AA, ...), the same for every fighter, and a white screen flash (stage tint slot 1, StgTint_Start in
 * src/battle/stg_c.c). Its colours are the RGBA float tracks at the start of the emitters' block "b" (part kinds 5, 9,
 * 10, 12, 15, 16, 17, 18), the u8 colours of the light rays (kind 2, block "a") and of the sprite keys (kind 14, a
 * "V000" pack in block "a"): port/tools/creator/fxcolour.py reads the same places. Its textures are greys, coloured by
 * those. The effect reads them from the loaded common file every frame, and only one transformation effect runs at a
 * time (EftTransformMgr.busy): so EftTransform_Init tints them in place for the fighter that transforms and
 * EftTransform_Term puts them back (also the manager's end, before the sets are freed).
 *
 * Visual only: nothing of the fight reads these colours. In an online session Port_CharaBurst answers -1 (packages are
 * not read there), so both players see the game's own burst.
 */
#include <string.h>
#include "types.h"
#include "battle/eft_h.h"
#include "plat_charas.h"

extern s32 BtlCharApi_GetChara(s32 objId);
extern void *BtlChar_FindByObjId(s32 objId);

enum { SITES_MAX = 1024 };
typedef struct {
    u8 *at;      /* the colour: 3 floats or 3 bytes */
    u8 isFloat;
    u8 save[12]; /* as it was */
} BurstSite;

static BurstSite sSite[SITES_MAX];
static int sSites;
static int sRgb = -1; /* the colour of the burst running now, -1 the game's */

/* The target colour with its brightest channel 1. */
static void unit(int rgb, float tn[3]) {
    float r = (float)((rgb >> 16) & 255), g = (float)((rgb >> 8) & 255), b = (float)(rgb & 255);
    float m = r > g ? (r > b ? r : b) : (g > b ? g : b);
    if (m < 1.0f) {
        tn[0] = tn[1] = tn[2] = 0.25f; /* (black: a dark grey burst) */
        return;
    }
    tn[0] = r / m;
    tn[1] = g / m;
    tn[2] = b / m;
}

/* A colour in the burst's colour: its brightness kept, a little white left in the greys (the cores). */
static void tint3(float c[3], const float tn[3]) {
    float m = c[0], mn = c[0], w;
    int k;
    for (k = 1; k < 3; k++) {
        m = c[k] > m ? c[k] : m;
        mn = c[k] < mn ? c[k] : mn;
    }
    if (m <= 0.5f) {
        return;
    }
    w = (m - mn) / m < 0.12f ? 0.2f : 0.0f;
    for (k = 0; k < 3; k++) {
        c[k] = m * (tn[k] * (1.0f - w) + w);
    }
}

static void site(u8 *at, int isFloat, const float tn[3]) {
    BurstSite *s;
    float c[3];
    int k;
    if (sSites >= SITES_MAX) {
        return;
    }
    s = &sSite[sSites++];
    s->at = at;
    s->isFloat = (u8)isFloat;
    memcpy(s->save, at, isFloat ? 12 : 3);
    if (isFloat) {
        memcpy(c, at, 12);
        tint3(c, tn);
        memcpy(at, c, 12);
    } else {
        for (k = 0; k < 3; k++) {
            c[k] = at[k];
        }
        tint3(c, tn);
        for (k = 0; k < 3; k++) {
            at[k] = (u8)(c[k] > 255.0f ? 255 : c[k] < 0.0f ? 0 : (int)(c[k] + 0.5f));
        }
    }
}

/* The float tracks of a block "b": n of 16 bytes (RGBA), taken only when they all look like colours. */
static void tracks(u8 *b, int n, const float tn[3]) {
    float v[4];
    int i, k;
    for (i = 0; i < n; i++) {
        memcpy(v, b + i * 16, 16);
        for (k = 0; k < 4; k++) {
            if (!(v[k] == v[k]) || v[k] < -1.0f || v[k] > 1024.0f) {
                return;
            }
        }
    }
    for (i = 0; i < n; i++) {
        site(b + i * 16, 1, tn);
    }
}

/* The sprite keys of a "V000" pack (kind 14). */
static void sprites(u8 *a, const float tn[3]) {
    int lines, i, j, k = 0;
    s32 keys;
    if (a == NULL || memcmp(a, "V000", 4) != 0) {
        return;
    }
    lines = a[4];
    memcpy(&keys, a + 8, 4);
    if (keys <= 0 || keys > 0x100000) {
        return;
    }
    for (i = 0; i < lines && i < 64; i++) {
        for (j = 0; j < a[0x20 + i * 0x20 + 4] && k < 512; j++, k++) {
            site(a + keys + k * 0x40 + 0x30, 0, tn);
        }
    }
}

/* Puts the colours back. */
void Port_BurstEnd(void) {
    while (sSites > 0) {
        BurstSite *s = &sSite[--sSites];
        memcpy(s->at, s->save, s->isFloat ? 12 : 3);
    }
    sRgb = -1;
}

/* EftTransform_Init: the set the burst of fighter objId plays (an EftSet), in the colour of its package's
   transformation, if it has one. */
void Port_BurstBegin(void *setp, s32 objId) {
    EftSet *set = setp;
    u8 *chr;
    s32 index, partner;
    int rgb, i, j;
    float tn[3];

    Port_BurstEnd();
    chr = BtlChar_FindByObjId(objId);
    if (set == NULL || set->head == NULL || chr == NULL) {
        return;
    }
    memcpy(&index, chr + 0x12CC, 4);   /* the form change asked (BtlActJForm, include/battle/btl_act_j.h): its slot */
    memcpy(&partner, chr + 0x12F8, 4); /* ... -1 for a transformation (a fusion: its partner) */
    if (partner != -1 || index < 0 || index > 3) {
        return;
    }
    rgb = Port_CharaBurst(BtlCharApi_GetChara(objId), index);
    if (rgb < 0) {
        return;
    }
    sRgb = rgb;
    unit(rgb, tn);
    for (i = 0; i < set->head->nGroups; i++) {
        EftSetGroupDef *def = &set->entries[i];
        EftSetGroup *g;
        int kind = def->kind;
        if (kind == 0 || kind >= EFT_SET_KIND_COUNT) {
            continue;
        }
        g = &set->group[kind];
        for (j = 0; j < def->nParts && g->firstPair + j < 40; j++) {
            EftSetPair *p = &set->pair[g->firstPair + j];
            u8 *a = (u8 *)p->a, *b = (u8 *)p->b;
            if (kind == 2) { /* the light rays: up to 10 keys of 0x78, three colours each */
                s32 n;
                int r, k;
                if (a == NULL) {
                    continue;
                }
                memcpy(&n, a + 0x4B0, 4);
                for (r = 0; r < n && r < 10; r++) {
                    for (k = 0; k < 3; k++) {
                        site(a + r * 0x78 + k * 0x10 + 0xC, 0, tn);
                    }
                }
            } else if (kind == 14) {
                sprites(a, tn);
            } else if (b != NULL) {
                if (kind == 5 || kind == 9 || kind == 10 || kind == 12 || kind == 15 || kind == 18) {
                    tracks(b, 12, tn);
                } else if (kind == 16) {
                    tracks(b, 3, tn);
                } else if (kind == 17) {
                    tracks(b, 6, tn);
                }
            }
        }
    }
}

/* StgTint_Start of slot 1 (the burst's flash): its colour while a coloured burst runs. 1 when out[] is set. */
int Port_BurstFlash(float out[3]) {
    float tn[3];
    int k;
    if (sRgb < 0) {
        return 0;
    }
    unit(sRgb, tn);
    for (k = 0; k < 3; k++) {
        out[k] = 255.0f * (tn[k] * 0.6f + 0.4f);
    }
    return 1;
}

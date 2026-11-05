#include "common.h"
#include "menu/menu_e.h"
#include "sys/pad.h"

#ifdef PORT
/*
 * PC only: more than four costumes in the select screens (include/menu/port_costume.h).
 *
 *   PortPlate_Text     a costume plate's number when the cursor is on a page after the first (the movie's picture of
 *                      numbers has four), and the arrows to the pages before and after
 *   PortCostume_Pick   the costume preview: the character in 3D in each of its costumes (the Mod Fighters grid's
 *                      viewer, src/menu/port_modmgr.c), opened with square on the costume plates
 */
#include "battle/col_c.h" /* FontStyle */
#include "menu/port_modui.h"
#include "menu/port_screen.h"
#include "menu/tbo_art.h"
#include "menu/port_costume.h"
#include <stdio.h>

extern void Flash_ClipGetPos(MFlash *flash, MFlashRef *ref, s32 *x, s32 *y);
extern void Flash_ClipSetOffset(MFlash *flash, MFlashRef *ref, s32 x, s32 y);
extern void Font_PrintAsciiAt(s32 x, s32 y, char *str);
extern s32 Font_GetGlyphHeight(void);
extern void Font_FlushAll(void);
extern FontStyle gFontStyle;
extern int Port_NetSession(void);
extern const char *Port_CharaShownName(int chara); /* port/src/plat_charas.c */

/* (where a plate's number goes from the plate's place: the middle of its picture, 45 pixels from the next plate) */
#define PLATE_TEXT_DX 24
#define PLATE_TEXT_DY 12
#define PLATE_ARROW_DX 31

/* What PortPlate_Text has to print, until PortPlate_Draw prints it right after its movie (the widescreen menus place
   what is drawn with a movie as they place the movie: the plates' window against its player's edge). */
typedef struct PlateLine {
    MFlash *flash;
    s32 x;
    s32 y;
    u32 color;
    char s[4];
} PlateLine;
static PlateLine sLine[24];
static s32 sLines;

static void Plate_Queue(MFlash *flash, s32 x, s32 y, char *s, u32 color) {
    PlateLine *l;

    if (sLines >= 24) { /* (a screen that never prints them: start again) */
        sLines = 0;
    }
    l = &sLine[sLines++];
    l->flash = flash;
    l->x = x;
    l->y = y;
    l->color = color;
    sprintf(l->s, "%.3s", s);
}

void PortPlate_Draw(MFlash *flash) {
    FontStyle saved = gFontStyle;
    s32 i;
    s32 k = 0;
    s32 any = 0;

    for (i = 0; i < sLines; i++) {
        PlateLine *l = &sLine[i];

        if (l->flash != flash) {
            sLine[k++] = *l;
            continue;
        }
        gFontStyle.align = FONT_ALIGN_CENTER;
        gFontStyle.color = l->color;
        gFontStyle.shadowMode = FONT_SHADOW_DROP;
        gFontStyle.shadowColor = 0xC0000000;
        gFontStyle.shadowDx = 1;
        gFontStyle.shadowDy = 1;
        Font_PrintAsciiAt(l->x, l->y - Font_GetGlyphHeight() / 2, l->s);
        any = 1;
    }
    gFontStyle = saved;
    if (any) {
        Font_FlushAll(); /* (now, with the movie; and the strings are this queue's, which the next lines reuse) */
    }
    sLines = k;
}

void PortPlate_Text(MFlash *flash, char *plate, s32 j, s32 costume, s32 count) {
    MFlashRef ref;
    s32 first = PORT_PLATE_FIRST(costume);
    s32 own = count > 4 && first > 0; /* (the first page keeps the movie's own numbers) */
    s32 x = -1000;
    s32 y = -1000;
    char num[8];

    Flash_FindLabel(flash, NULL, plate, &ref); /* (where the plate is on the screen: its text's place is in it) */
    if (count >= 4) {
        /* the page's plates in the middle, as the movie has two or three (and back in their places for four: the
           screen moves them only for a character with two or three) */
        static const s32 kShift[5] = {0, 0x43, 0x2D, 0x16, 0};

        Flash_ClipSetOffset(flash, &ref, kShift[PORT_PLATE_COUNT(costume, count)], 0);
    }
    if (ref.id >= 0) {
        Flash_ClipGetPos(flash, &ref, &x, &y);
        x += flash->unk14[1] + PLATE_TEXT_DX; /* (the movie's own offset: Flash.ofs, include/sys/gfxm_c.h) */
        y += flash->unk14[2] + PLATE_TEXT_DY;
    }
    Flash_FindLabel(flash, plate, "mc_color_text_off", &ref);
    Flash_ClipSetFlags(flash, &ref, 2, !own);
    Flash_FindLabel(flash, plate, "mc_color_text_on", &ref);
    Flash_ClipSetFlags(flash, &ref, 2, !own);
    if (x == -1000 || count <= 4) {
        return;
    }
    if (own) {
        sprintf(num, "%d", (int)(first + j + 1));
        Plate_Queue(flash, x, y, num, first + j == costume ? 0xFF40E0FF : 0xFFFFFFFF);
    }
    if (j == 0 && first > 0) {
        Plate_Queue(flash, x - PLATE_ARROW_DX, y, "<", 0xFF40E0FF);
    }
    if (j == PORT_PLATE_COUNT(costume, count) - 1 && first + 4 < count) {
        Plate_Queue(flash, x + PLATE_ARROW_DX, y, ">", 0xFF40E0FF);
    }
}

s32 PortCostume_CanPick(s32 chara, s32 count) {
    return PORT_IS_CHARA(chara) && count > 0 && !Port_NetSession();
}

/* ---- the preview ----
 * In the Mod Manager's look (the mockups' pixels, port_mmkit.c): its sky, the character's model large in the middle,
 * its name above, the costumes as a row of numbered buttons under it (two rows past eight), the buttons at the foot. */
#define CP_MODEL_X 688.0f
#define CP_MODEL_FEET 606.0f
#define CP_MODEL_H 530.0f
#define CP_PER_ROW 8

typedef struct CostumePick {
    s32 chara;
    s32 count;
    s32 cur;
    s32 frames;
    s32 rest;   /* frames the cursor has rested (the model is read once it has) */
    s32 pickT;  /* frames since the pick, 0 not yet */
    char name[64];
} CostumePick;

static void Cp_Draw(CostumePick *p) {
    f32 in = Mm_Ease((f32)p->frames / 14.0f);
    f32 drop = (1.0f - in) * -160.0f;
    s32 rows = (p->count + CP_PER_ROW - 1) / CP_PER_ROW;
    s32 k;
    static char line[48];

    Mx_Background(0);
    if (Mm_ViewKey() == p->cur) {
        Mx_Glow(CP_MODEL_X, CP_MODEL_FEET, CP_MODEL_H, Mx_Rgba(0xB8, 0xE4, 0xFF, 0x30), p->frames);
    }
    Mm_ViewDraw(CP_MODEL_X, CP_MODEL_FEET, CP_MODEL_H);
    Mx_Fit(MM_BALL, 100.0f, 72.0f + drop, 64.0f, 0x80);
    Mx_Heading(146.0f, 72.0f + drop, "COSTUMES", 50.0f, 500.0f, 0);
    Mx_Rect(110.0f, 110.0f + drop, 640.0f, 113.0f + drop, Mx_Rgba(0xF0, 0xC8, 0xFF, 0x70), Mx_Rgba(0xB0, 0x88, 0xE0, 0x70));
    Mx_Text(1300.0f, 72.0f + drop, p->name, 40.0f, 620.0f, MX_WHITE, 2);
    sprintf(line, "COSTUME %d OF %d", (int)(p->cur + 1), (int)p->count);
    Mx_Text(1300.0f, 116.0f + drop, line, 26.0f, 600.0f, MX_GOLD, 2);
    for (k = 0; k < p->count; k++) {
        s32 row = k / CP_PER_ROW;
        s32 inRow = row < rows - 1 ? CP_PER_ROW : p->count - row * CP_PER_ROW;
        f32 cx = CP_MODEL_X + 86.0f * ((f32)(k % CP_PER_ROW) - (f32)(inRow - 1) * 0.5f);
        f32 cy = 640.0f - 46.0f * (f32)(rows - 1 - row) + (1.0f - in) * 160.0f;
        static char num[4];

        sprintf(num, "%d", (int)(k + 1));
        if (k == p->cur) {
            Mx_Bar(MM_TAB_SEL, cx - 38.0f, cy - 17.0f, cx + 38.0f, cy + 17.0f, 0x80);
            Mx_TextC(cx, cy, num, 24.0f, 0.0f, Mx_Rgba(0xFF, 0xFF, 0xF0, 0x80), Mx_Rgba(0xFF, 0xF0, 0xC8, 0x80), Mx_Rgba(0x6C, 0x3C, 0x08, 0x80), 1,
                     0x80);
        } else {
            Mx_Bar(MM_NAVBAR, cx - 38.0f, cy - 15.0f, cx + 38.0f, cy + 15.0f, 0x80);
            Mx_TextA(cx, cy, num, 22.0f, 0.0f, MX_WHITE, 1, 0x80);
        }
    }
    Mm_PromptReset();
    Mm_Prompt(0, "PICK");
    Mm_Prompt(3, "COSTUME");
    Mm_Prompt(2, "BACK");
    Mx_Footer(388.0f, 988.0f, 694.0f, 750.0f);
    if (p->pickT > 0) { /* the pick: a flash of light */
        s32 a = p->pickT < 5 ? p->pickT * 22 : (14 - p->pickT) * 12;

        a = a < 0 ? 0 : a;
        Ps_ScreenQuad(0.0f, 0.0f, 512.0f, 0.0f, 0.0f, 448.0f, 512.0f, 448.0f, Mx_Rgba(0xFF, 0xF0, 0xFF, a), Mx_Rgba(0xFF, 0xF0, 0xFF, a),
                      Mx_Rgba(0xFF, 0xC0, 0xFF, a), Mx_Rgba(0xFF, 0xC0, 0xFF, a));
    }
}

s32 PortCostume_Pick(s32 padNo, s32 chara, s32 count, s32 *costume) {
    static CostumePick pick;
    CostumePick *p = &pick;
    Pad *pad = &gPad[padNo];
    s32 result = 0;
    s32 done = 0;
    const char *shown;

    if (!PortCostume_CanPick(chara, count)) {
        return 0;
    }
    memset(p, 0, sizeof(*p));
    p->chara = chara;
    p->count = count > PORT_COSTUME_MAX ? PORT_COSTUME_MAX : count;
    p->cur = *costume >= 0 && *costume < p->count ? *costume : 0;
    if ((shown = Port_CharaShownName(chara)) != NULL) {
        sprintf(p->name, "%.60s", shown);
    } else {
        Port_ModCharaName(chara, p->name, sizeof(p->name));
    }
    Snd_PlaySe(1, 1);
    Port_EscQuit(0); /* Esc goes back to the select, it does not close the game */
    Mm_ViewOpen();
    while (!done) {
        s32 was = p->cur;

        Gfx_BeginFrame();
        Mm_TexReset();
        Pad_Update();
        if (Port_EscTake()) {
            pad->gamePressed |= PADG_TRIANGLE;
        }
        Snd_Update();
        Mm_ViewWant(p->cur, p->chara, p->cur, -1, p->rest >= 6);
        Mx_Begin();
        Cp_Draw(p);
        File_Stub264D90();
        Gfx_EndFrame(1);
        Dma_Flush();
        p->frames++;
        if (p->rest < 1000) {
            p->rest++;
        }
        if (p->pickT > 0) {
            if (++p->pickT > 14) {
                done = 1;
            }
            continue;
        }
        if (p->frames < 10) {
            continue;
        }
        if (pad->gameRepeat & (PADG_RIGHT | PADG_RIGHT2 | PADG_R1)) {
            p->cur = (p->cur + 1) % p->count;
        } else if (pad->gameRepeat & (PADG_LEFT | PADG_LEFT2 | PADG_L1)) {
            p->cur = (p->cur + p->count - 1) % p->count;
        } else if ((pad->gameRepeat & (PADG_DOWN | PADG_DOWN2)) && p->cur + CP_PER_ROW < p->count) {
            p->cur += CP_PER_ROW;
        } else if ((pad->gameRepeat & (PADG_UP | PADG_UP2)) && p->cur >= CP_PER_ROW) {
            p->cur -= CP_PER_ROW;
        }
        if (p->cur != was) {
            p->rest = 0;
            Snd_PlaySe(1, 0);
        }
        if (pad->gamePressed & PADG_CROSS) {
            *costume = p->cur;
            result = 1;
            p->pickT = 1;
            Snd_PlaySe(1, 1);
        } else if (pad->gamePressed & (PADG_TRIANGLE | PADG_CIRCLE | PADG_SQUARE)) {
            done = 1;
            Snd_PlaySe(1, 2);
        }
    }
    sceGsSyncPath(0, 0);
    Mm_ViewClose();
    Port_EscQuit(1);
    return result;
}
#endif

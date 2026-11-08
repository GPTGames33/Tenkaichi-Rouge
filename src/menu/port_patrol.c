#include "common.h"
#include "menu/menu_e.h"
#include "sys/pad.h"
#include "sys/save.h"
#ifdef PORT
#include "menu/port_modui.h"

/*
 * PC only: Corrupt Patrol (main menu item 15; the Duel mode runs it with gPortDuelRole 5, as it runs the Story Creator
 * with 4: src/menu/menu_g_b.c).
 *
 * A game of its own: the player's warrior of one of six races, places to fly between, quests and a story of corrupted
 * fighters who become masters (every character of the game), trainings, bounties, the game's own item shop, levels,
 * a save file of its own. The screens are drawn with Dear ImGui over the game's picture (port/src/gs/patrol_ui.cpp);
 * this loop is the game's half, as menu/port_story.c is the Story Creator's: the place the player is at behind the
 * screens (its battle stage, as the Custom Character screen stands its warrior on one) with a character on it, or the
 * row of the six races to choose from, the screens' sounds, and, when a battle is to be fought, its setup: the mode's
 * battles (and its places to walk in) are episodes of a saga made by the screens, played by the Story Creator's engine
 * (script 900, src/battle/port_story_btl.c). After the battle the main menu's dispatcher sends the game back here
 * (src/menu/menu_a_b.c: PortPatrol_AfterBattle). The shop is the game's own (Evolution Z's, src/menu/menu_v_d.c), run
 * from here with the mode's zeni and items in the game's save for as long as it is open, the player's own put back
 * after it (the game's save is never written meanwhile).
 */

#define PP_SCRIPT 900 /* the Story Creator's script number (src/battle/port_story_btl.c: PortStory_Script) */

/* what PortPatrolUi_Get answers (port/src/gs/patrol_ui.cpp) */
#define PG_LEAVE 0   /* 1 back to the main menu, 2 fight the battle prepared, 3 the creator, 4 the shop */
#define PG_SOUND 1
#define PG_KEY 2
#define PG_CHARA 3
#define PG_COSTUME 4
#define PG_TURN 5
#define PG_BGM 6
#define PG_VOICE 7
#define PG_STAGE 8     /* the stage behind the screens, -1 the plain backdrop */
#define PG_PLACE_X 9   /* where the model stands (mockup pixels x10) */
#define PG_PLACE_FEET 10
#define PG_PLACE_H 11
#define PG_LINE 20     /* + k * 5: the row's k-th model: character (-1 none), x, feet, height (x10), yaw (x1000) */
#define PP_LINE_MAX 6

extern void PortPatrolUi_Open(void);
extern void PortPatrolUi_Close(void);
extern s32 PortPatrolUi_Get(s32 what);
extern s32 PortPatrolUi_Prepare(void);
extern void PortPatrolUi_Result(s32 result);
extern s32 PortPatrolUi_ShopMoney(void);
extern s32 PortPatrolUi_ShopLevel(void);
extern s32 PortPatrolUi_ShopOwns(s32 item);
extern void PortPatrolUi_ShopDone(s32 money);
extern void PortPatrolUi_ShopGot(s32 item);
extern s32 ChrView_IsLoading(void);
extern void Voice_PlayChara(s32 voice, s32 chara, s32 line);
extern void Voice_Stop(s32 voice);
extern void Port_EscQuit(s32 on);
extern s32 Port_NetSession(void);
extern void BattleSetup_Clear(void);
extern void BattleSetup_SetScript(s32 script);
extern s32 Shop_Run(s32 section);
extern void *gMenuArc7;

static s32 sPlaying; /* one of the mode's battles is set up or running */

/* (menu/port_story.c's PortStory_Playing: the Story Creator's script engine plays the mode's battles too) */
s32 PortPatrol_StoryPlaying(void) {
    return sPlaying;
}

/* Progress_Main (src/menu/menu_a_b.c), after any battle: 1 when it was the mode's (its screens follow). */
s32 PortPatrol_AfterBattle(s32 flags, s32 reason) {
    s32 result;

    if (!sPlaying) {
        return 0;
    }
    sPlaying = 0;
    if (flags & 8) {
        result = 3; /* left from the pause menu */
    } else if (flags & 1) {
        result = 1;
    } else {
        result = 2;
    }
    PortPatrolUi_Result(result);
    return 1;
}

/* The game's item shop with the mode's zeni and items: the save's own are kept aside and put back after. */
static void PortPatrol_Shop(void) {
    static u8 keep[SAVE_ITEM_COUNT];
    s32 keepMoney = gSaveData->money;
    s32 keepLevel = gSaveData->unk100C;
    s32 keep1008 = gSaveData->unk1008;
    u32 keepProgress = gProgress->flags;
    s32 loaded = 0;
    s32 i;

    memcpy(keep, gSaveData->item, SAVE_ITEM_COUNT);
    gSaveData->money = PortPatrolUi_ShopMoney();
    gSaveData->unk100C = PortPatrolUi_ShopLevel();
    gSaveData->unk1008 = (keep1008 & ~3) | 2; /* (2: the shop's stock is the mode's to raise, not its own) */
    for (i = 0; i < SAVE_ITEM_COUNT; i++) {
        gSaveData->item[i] = PortPatrolUi_ShopOwns(i) ? SAVE_ITEM_OWNED : 0;
    }
    if (gMenuArc7 == NULL) {
        gMenuArc7 = File_LoadSync(gProgress->baseFile + 4, NULL, 0);
        loaded = 1;
    }
    Bgm_Play(0x10B18); /* (Evolution Z's music) */
    Shop_Run(2);
    if (loaded && gMenuArc7 != NULL) {
        Heap_Free(gMenuArc7);
        gMenuArc7 = NULL;
    }
    for (i = 0; i < SAVE_ITEM_COUNT; i++) {
        if ((gSaveData->item[i] & SAVE_ITEM_OWNED) && !PortPatrolUi_ShopOwns(i)) {
            PortPatrolUi_ShopGot(i);
        }
    }
    PortPatrolUi_ShopDone(gSaveData->money);
    memcpy(gSaveData->item, keep, SAVE_ITEM_COUNT);
    gSaveData->money = keepMoney;
    gSaveData->unk100C = keepLevel;
    gSaveData->unk1008 = keep1008;
    gProgress->flags = (gProgress->flags & ~1u) | (keepProgress & 1u); /* (no save is asked for by this visit) */
}

/* The mode's screens until they are left: 0 back to the main menu, 1 a battle is set up, 3 the creator is wanted,
   4 the shop. */
static s32 PortPatrol_Screens(void);

/* From the main menu (or after one of its battles) until the mode is left. Returns 1 when a battle is to be fought:
   it is set up and the caller starts it. The warrior's look is made in the Custom Character screen's Patrol mode
   (port/src/gs/creator_ui.cpp: PortCreator_PatrolBegin), run from here between two openings of the screens. */
static s32 sWantCreator;

s32 PortPatrol_Run(void) {
    s32 r;

    while ((r = PortPatrol_Screens()) == 4) {
        PortPatrol_Shop(); /* (then the screens again) */
    }
    sWantCreator = r == 3;
    return r == 3 ? 0 : r;
}

/* (menu_g_b.c, after PortPatrol_Run) 1 once when the creator is wanted: the Duel mode is entered again for it (role 6),
   as from the main menu, and for the screens after it */
s32 PortPatrol_WantsCreator(void) {
    s32 w = sWantCreator;

    sWantCreator = 0;
    return w;
}

static s32 PortPatrol_Screens(void) {
    s32 leaving = 0;
    s32 play = 0;
    s32 stage;

    if (Port_NetSession()) {
        return 0;
    }
    sPlaying = 0;
    PortPatrolUi_Open();
    Port_EscQuit(0); /* Esc is "back" in here (the screen takes it), not the end of the game */
    Bgm_Play(0x10B16 + PortPatrolUi_Get(PG_BGM));
    stage = PortPatrolUi_Get(PG_STAGE);
    if (stage >= 0 && stage < 0x23) { /* (a stage of the disc: 0x171 + stage is its model, battle_load.c) */
        Mm_ViewUseStage(0x171 + stage);
    } else {
        stage = -1;
    }
    Mm_ViewOpen();
    ColorFade_StartIn(0, 0, 0, 0x14);
    while (1) {
        s32 turn;
        s32 se;
        s32 key;
        s32 k;
        f32 x = (f32)PortPatrolUi_Get(PG_PLACE_X) * 0.1f;
        f32 feet = (f32)PortPatrolUi_Get(PG_PLACE_FEET) * 0.1f;
        f32 h = (f32)PortPatrolUi_Get(PG_PLACE_H) * 0.1f;

        Gfx_BeginFrame();
        Mm_TexReset();
        Pad_Update(); /* (the screen reads the pads itself) */
        Snd_Update();
        ColorFade_Update();
        key = PortPatrolUi_Get(PG_KEY);
        Mm_ViewWant(key, PortPatrolUi_Get(PG_CHARA), PortPatrolUi_Get(PG_COSTUME), -1, 1);
        turn = PortPatrolUi_Get(PG_TURN);
        if (turn != 0) {
            Mm_ViewTurn((f32)turn * 0.001f);
        }
        for (se = PortPatrolUi_Get(PG_SOUND); se >= 0; se = PortPatrolUi_Get(PG_SOUND)) {
            Snd_PlaySe(1, se);
        }
        se = PortPatrolUi_Get(PG_VOICE);
        if (se >= 0) {
            Voice_Stop(0);
            Voice_PlayChara(0, se >> 8, se & 0xFF);
        }
        for (k = 0; k < PP_LINE_MAX; k++) {
            s32 b = PG_LINE + k * 5;

            Mm_ViewSetLine(k, PortPatrolUi_Get(b), (f32)PortPatrolUi_Get(b + 1) * 0.1f, (f32)PortPatrolUi_Get(b + 2) * 0.1f,
                           (f32)PortPatrolUi_Get(b + 3) * 0.1f, (f32)PortPatrolUi_Get(b + 4) * 0.001f);
        }
        Mx_Begin();
        Mx_Background(stage >= 0 ? 0 : 1);
        Mm_ViewSetStage(stage >= 0);
        if (key >= 0) {
            if (stage < 0) {
                Mx_Platform(x, feet + Mx_LenY(4.0f), h * 0.82f);
            }
            Mm_ViewDraw(x, feet, h);
        }
        ColorFade_Draw();
        File_Stub264D90();
        Gfx_EndFrame(1);
        Dma_Flush();
        if (!leaving && ColorFade_IsInDone() && PortPatrolUi_Get(PG_LEAVE)) {
            leaving = PortPatrolUi_Get(PG_LEAVE);
            play = leaving == 2 && !ChrView_IsLoading() && PortPatrolUi_Prepare();
            if ((leaving == 3 || leaving == 4) && ChrView_IsLoading()) {
                leaving = 0;
                continue;
            }
            if (leaving == 2 && !play) {
                leaving = 0; /* (it could not be made, or the viewer is still loading: the screen says why / tries again) */
                continue;
            }
            Snd_PlaySe(1, play || leaving == 4 ? 1 : 2);
            ColorFade_StartOut(0, 0, 0, 0x14);
        }
        if (leaving && ColorFade_IsOutDone()) {
            break;
        }
    }
    sceGsSyncPath(0, 0);
    Mm_ViewClose();
    Port_EscQuit(1);
    PortPatrolUi_Close();
    Dma_ResetBuffers();
    if (leaving == 3 || leaving == 4) {
        return leaving;
    }
    if (play) {
        BattleSetup_Clear();
        BattleSetup_SetScript(PP_SCRIPT); /* (the script's setup action sets the rule, the sides and the members) */
        sPlaying = 1;
    }
    return play;
}
#endif

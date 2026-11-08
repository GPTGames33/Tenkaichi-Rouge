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
 * A game of its own: the player's warrior of one of six races, the Patrol's headquarters, a story of corrupted
 * fighters who become masters, trainings, bounties, a shop, levels, a save file of its own. The screens are drawn with
 * Dear ImGui over the game's picture (port/src/gs/patrol_ui.cpp); this loop is the game's half, as menu/port_story.c
 * is the Story Creator's: the Mod Manager's backdrop and a character on its platform, the screens' sounds, and, when a
 * battle is to be fought, its setup: the mode's battles are episodes of a saga made by the screens, played by the Story
 * Creator's engine (script 900, src/battle/port_story_btl.c). After the battle the main menu's dispatcher sends the
 * game back here (src/menu/menu_a_b.c: PortPatrol_AfterBattle).
 */

#define PP_SCRIPT 900 /* the Story Creator's script number (src/battle/port_story_btl.c: PortStory_Script) */

/* what PortPatrolUi_Get answers (port/src/gs/patrol_ui.cpp) */
#define PG_LEAVE 0   /* 1 back to the main menu, 2 fight the battle prepared */
#define PG_SOUND 1
#define PG_KEY 2
#define PG_CHARA 3
#define PG_COSTUME 4
#define PG_TURN 5
#define PG_BGM 6
#define PG_VOICE 7

extern void PortPatrolUi_Open(void);
extern void PortPatrolUi_Close(void);
extern s32 PortPatrolUi_Get(s32 what);
extern s32 PortPatrolUi_Prepare(void);
extern void PortPatrolUi_Result(s32 result);
extern s32 ChrView_IsLoading(void);
extern void Voice_PlayChara(s32 voice, s32 chara, s32 line);
extern void Voice_Stop(s32 voice);
extern void Port_EscQuit(s32 on);
extern s32 Port_NetSession(void);
extern void BattleSetup_Clear(void);
extern void BattleSetup_SetScript(s32 script);

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

/* The mode's screens, from the main menu (or after one of its battles) until they are left. Returns 1 when a battle
   is to be fought: it is set up and the caller starts it. */
s32 PortPatrol_Run(void) {
    s32 leaving = 0;
    s32 play = 0;

    if (Port_NetSession()) {
        return 0;
    }
    sPlaying = 0;
    PortPatrolUi_Open();
    Port_EscQuit(0); /* Esc is "back" in here (the screen takes it), not the end of the game */
    Bgm_Play(0x10B16 + PortPatrolUi_Get(PG_BGM));
    Mm_ViewOpen();
    ColorFade_StartIn(0, 0, 0, 0x14);
    while (1) {
        s32 turn;
        s32 se;
        s32 key;

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
        Mx_Begin();
        Mx_Background(1);
        if (key >= 0) {
            Mx_Platform(1030.0f, 605.0f + Mx_LenY(4.0f), 390.0f * 0.82f);
            Mm_ViewDraw(1030.0f, 605.0f, 390.0f);
        }
        ColorFade_Draw();
        File_Stub264D90();
        Gfx_EndFrame(1);
        Dma_Flush();
        if (!leaving && ColorFade_IsInDone() && PortPatrolUi_Get(PG_LEAVE)) {
            leaving = PortPatrolUi_Get(PG_LEAVE);
            play = leaving == 2 && !ChrView_IsLoading() && PortPatrolUi_Prepare();
            if (leaving == 2 && !play) {
                leaving = 0; /* (it could not be made, or the viewer is still loading: the screen says why / tries again) */
                continue;
            }
            Snd_PlaySe(1, play ? 1 : 2);
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
    if (play) {
        BattleSetup_Clear();
        BattleSetup_SetScript(PP_SCRIPT); /* (the script's setup action sets the rule, the sides and the members) */
        sPlaying = 1;
    }
    return play;
}
#endif

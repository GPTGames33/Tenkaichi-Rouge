#include "common.h"
#include "menu/menu_e.h"
#include "sys/pad.h"
#include "sys/save.h"
#ifdef PORT
#include "menu/port_modui.h"

/*
 * PC only: the Story Creator (main menu item 14; the Duel mode runs it with gPortDuelRole 4, as it runs the Mod
 * Manager with 2 and Custom Character with 3: src/menu/menu_g_b.c).
 *
 * Sagas of the player's own: episodes of scenes (the fighters on the stage, talking in text boxes, with voices, poses,
 * camera shots and effects) and battles (teams, stage, health, time, how they are won, lines said during the fight)
 * that lead to one another by how the battle went. A saga is a JSON file in the `sagas` folder next to the game
 * (docs/port/sagas.md). The screens - My Sagas, the editor, the result of an episode - are drawn with Dear ImGui over
 * the game's picture (port/src/gs/story_ui.cpp); this loop is the game's half: the Mod Manager's backdrop and
 * platform with a character on it (the Character Reference's viewer, as the Mod Manager has it), the screen's sounds,
 * and, when an episode is to be played, its battle: a battle of the story mode with the episode's own script (made by
 * story_ui.cpp, played by src/battle/port_story_btl.c). After the battle the main menu's dispatcher sends the game
 * back here (src/menu/menu_a_b.c: PortStory_AfterBattle), where the saga goes on.
 */

#define PS_SCRIPT 900 /* the script number of a saga's battle (src/battle/port_story_btl.c: PortStory_Script) */

/* what PortStoryUi_Get answers (port/src/gs/story_ui.cpp) */
#define PG_LEAVE 0   /* 1 back to the main menu, 2 play the episode prepared */
#define PG_SOUND 1   /* the next sound of the screen (the menus' bank 1), -1 none */
#define PG_KEY 2     /* the model wanted: a key that changes with it, -1 none */
#define PG_CHARA 3
#define PG_COSTUME 4
#define PG_TURN 5    /* the model's turn asked since the last frame, thousandths of a radian */
#define PG_BGM 6     /* the music of the screen */
#define PG_VOICE 7   /* a spoken line to hear: chara << 8 | line, -1 none */

extern void PortStoryUi_Open(void);
extern void PortStoryUi_Close(void);
extern s32 PortStoryUi_Get(s32 what);
extern s32 PortStoryUi_Prepare(void); /* the episode chosen made into its battle's script; 0 if it could not be */
extern void PortStoryUi_Result(s32 result); /* 1 won, 2 lost, 3 left from the pause menu */
extern s32 ChrView_IsLoading(void);
extern void Voice_PlayChara(s32 voice, s32 chara, s32 line);
extern void Voice_Stop(s32 voice);
extern void Voice_PlayDefault(s32 voice, s32 id);
extern void Port_EscQuit(s32 on);
extern s32 Port_NetSession(void);
extern void BattleSetup_Clear(void);
extern void BattleSetup_SetScript(s32 script);

static s32 sPlaying; /* a saga's battle is set up or running */

/* (the battle side, src/battle/port_story_btl.c) */
s32 PortStory_Playing(void) {
    return sPlaying;
}

/* Progress_Main (src/menu/menu_a_b.c), after any battle: 1 when it was a saga's (the Story Creator follows). */
s32 PortStory_AfterBattle(s32 flags, s32 reason) {
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
    PortStoryUi_Result(result);
    return 1;
}

/* The Story Creator's screens, from the main menu (or after a saga's battle) until they are left. Returns 1 when an
   episode is to be played: its battle is set up and the caller starts it. */
s32 PortStory_Run(void) {
    s32 leaving = 0;
    s32 play = 0;

    if (Port_NetSession()) {
        return 0;
    }
    sPlaying = 0;
    PortStoryUi_Open();
    Port_EscQuit(0); /* Esc is "back" in here (the screen takes it), not the end of the game */
    Bgm_Play(0x10B16 + PortStoryUi_Get(PG_BGM));
    Mm_ViewOpen();
    ColorFade_StartIn(0, 0, 0, 0x14);
    while (1) {
        s32 turn;
        s32 se;
        s32 key;

        Gfx_BeginFrame();
        Mm_TexReset();
        Pad_Update(); /* (the pads are not the game's while the screen is up: the screen reads them itself) */
        Snd_Update();
        ColorFade_Update();
        key = PortStoryUi_Get(PG_KEY);
        Mm_ViewWant(key, PortStoryUi_Get(PG_CHARA), PortStoryUi_Get(PG_COSTUME), -1, 1);
        turn = PortStoryUi_Get(PG_TURN);
        if (turn != 0) {
            Mm_ViewTurn((f32)turn * 0.001f);
        }
        for (se = PortStoryUi_Get(PG_SOUND); se >= 0; se = PortStoryUi_Get(PG_SOUND)) {
            Snd_PlaySe(1, se);
        }
        se = PortStoryUi_Get(PG_VOICE); /* (a spoken line to hear, from the editor; -2 - n: Dragon History's voice n) */
        if (se >= 0) {
            Voice_Stop(0);
            Voice_PlayChara(0, se >> 8, se & 0xFF);
        } else if (se <= -2) {
            Voice_Stop(0);
            Voice_PlayDefault(0, ((gSaveData->flags & SAVE_FLAG_VOICE) ? 0x47E0 : 0xD48) + (-2 - se));
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
        if (!leaving && ColorFade_IsInDone() && PortStoryUi_Get(PG_LEAVE)) {
            leaving = PortStoryUi_Get(PG_LEAVE);
            play = leaving == 2 && !ChrView_IsLoading() && PortStoryUi_Prepare();
            if (leaving == 2 && !play) {
                leaving = 0; /* (it could not be made: the screen says why) */
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
    PortStoryUi_Close();
    Dma_ResetBuffers();
    if (play) {
        BattleSetup_Clear();
        BattleSetup_SetScript(PS_SCRIPT); /* (the script's setup action sets the rule, the sides and the members) */
        sPlaying = 1;
    }
    return play;
}
#endif

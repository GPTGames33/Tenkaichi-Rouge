#include "common.h"
#include "menu/menu_e.h"
#include "sys/pad.h"
#ifdef PORT
#include "menu/port_modui.h"

/*
 * PC only: the Custom Character screen (main menu item 13; the Duel mode runs it with gPortDuelRole 3, as it runs the
 * Mod Manager with 2: src/menu/menu_g_b.c).
 *
 * The screen itself - the tabs, the lists, the character grid, the keyboard, the tip box and the button prompts - is
 * drawn with Dear ImGui over the game's picture on the render thread (port/src/gs/creator_ui.cpp), as a client of the
 * creator's engine (port/tools/creator/creator.py, which it starts when none is open). This loop is the game's half:
 * the Mod Manager's backdrop and platform, and on it the warrior as a 3D model - the Character Reference's viewer
 * (ChrView, as the Mod Manager uses it: Mm_ViewWantPreview) showing the model the creator built of the warrior's
 * current look (its preview folder, served for the disc character it is shown as: PortCharas_Preview), or a disc
 * character's own while the grid's cursor rests on it. It also reads the characters folder again after "Add to
 * roster" (PortCharas_Reload, so the new cell is on the character select at once), plays the screen's sounds, and
 * leaves for a test fight with the warrior when it is asked to.
 */

/* what PortCreator_Get answers (port/src/gs/creator_ui.cpp) */
#define PC_LEAVE 0        /* 1: back to the main menu, 2: to the test fight */
#define PC_KEY 1          /* the model wanted: a key that changes with it, -1 none */
#define PC_CHARA 2        /* ... the disc character it is shown as */
#define PC_COSTUME 3
#define PC_RELOAD 4       /* 1 once: the characters folder is to be read again */
#define PC_TURN 5         /* the turn of the model asked since the last frame, thousandths of a radian */
#define PC_SOUND 6        /* the next sound of the screen (the menus' bank 1), -1 none */
#define PC_TEST_CHARA 7   /* the test fight's warrior (an added character's id), -1 none */
#define PC_TEST_COSTUME 8
#define PC_PLACE_X 9      /* where the model stands, tenths of the mockup's pixels (1376 x 768: port_mmkit.c): its feet */
#define PC_PLACE_FEET 10
#define PC_PLACE_H 11     /* ... and its height (the screen's camera zooms and moves the model: these change) */
#define PC_PLATFORM 12    /* 1: the platform under it (not in a close shot) */
#define PC_STANCE 13      /* 1: the Stance Maker is open (it wants to know where the model's joints are) */

/* the Stance Maker (src/battle/bobj_a.c): the viewer's motion, and where its joints were drawn */
extern u8 *Mm_ViewObj(void);
extern void BtlObj_PortStanceView(u8 *obj);
extern void BtlObj_PortStanceCapture(u8 *obj, void *world2screen, f32 cx, f32 cy);
extern u8 *gBtlCamView; /* (View, battle/btl_cam.h: world2screen at 0x140, the screen's centre at 0x240) */
#define PC_PHOTO 14       /* 1: a picture of the model alone is being taken (a plain key colour behind it, nothing else) */
#define PC_HOST 15        /* the host standing beside the warrior (a disc character), -1 none */
#define PC_STAGE 16       /* 1: the battle stage around the model (its backdrop), 0 the Mod Manager's sky */

/* The backdrop: a battle stage of the disc (setting creator_stage, the stage's number; default the grassy plain of stage 0),
   and where the host stands (mockup pixels: its feet and its height), turned towards the warrior. */
#define PC_STAGE_DEFAULT 0
#define PC_HOST_X 1206.0f
#define PC_HOST_FEET 420.0f
#define PC_HOST_H 200.0f
#define PC_HOST_YAW 3.85f

extern void PortCreator_Open(void);
extern void PortCreator_Close(void);
extern s32 PortCreator_Get(s32 what);
extern void PortCreator_Reloaded(s32 ok);
extern s32 PortCharas_Reload(void);
extern s32 ChrView_IsLoading(void);
extern void Port_EscQuit(s32 on);
extern s32 Port_NetSession(void);
extern s32 Port_ModTestStage(void);
extern s32 gPortModTest; /* menu/port_modmgr.c: 2 = the test fight is this screen's (its pause menu goes back here) */
extern void Battle_ClearWork(void);
extern void BattleSetup_SetRule(s32 screenMode, s32 mode, s32 bgm, s32 timeLimit, s32 announcer, s32 stage, s32 unk10);
extern void BattleSetup_SetSide(s32 sideNo, s32 control, s32 pad, s32 memberCount, s32 unk1FC, s32 unk200, s32 lead, void *unk);
extern void BattleSetup_SetMember(s32 sideNo, s32 idx, s32 chara, s32 costume, s32 variant, s32 cpuLevel, f32 health, void *items);
extern void BattleSetup_Finish(void);
extern s32 gMmTestFoe;
extern int Port_Setting(const char *name, int def);

/* The screen, from the main menu until it is left. Returns 1 when it was left for a test fight: the battle is set up
   (the warrior against the Mod Manager's test opponent, in training) and the caller starts it. */
s32 PortCreator_Run(void) {
    static u8 noItems[0x10]; /* an empty set of Z items */
    s32 leaving = 0;
    s32 test = 0;
    s32 testChara = -1;
    s32 testCostume = 0;
    s32 frames = 0;

    if (Port_NetSession()) {
        return 0; /* (character packages are not used online: there is nothing to make here) */
    }
    PortCreator_Open();
    Port_EscQuit(0); /* Esc is "back" in here (the screen takes it), not the end of the game */
    {
        s32 stage = Port_Setting("creator_stage", PC_STAGE_DEFAULT);

        if (stage >= 0 && stage < 0x23) { /* (a stage of the disc: 0x171 + stage is its model, battle_load.c) */
            Mm_ViewUseStage(0x171 + stage);
        }
    }
    Mm_ViewOpen();
    ColorFade_StartIn(0, 0, 0, 0x14);
    while (1) {
        s32 turn;
        s32 se;
        f32 x = (f32)PortCreator_Get(PC_PLACE_X) * 0.1f;
        f32 feet = (f32)PortCreator_Get(PC_PLACE_FEET) * 0.1f;
        f32 h = (f32)PortCreator_Get(PC_PLACE_H) * 0.1f;

        Gfx_BeginFrame();
        Mm_TexReset();
        Pad_Update(); /* (the pads are not the game's while the screen is up: the screen reads them itself) */
        Snd_Update();
        ColorFade_Update();
        if (!ChrView_IsLoading() && PortCreator_Get(PC_RELOAD)) { /* (not while the viewer reads a model) */
            PortCreator_Reloaded(PortCharas_Reload());
        }
        Mm_ViewWantPreview(PortCreator_Get(PC_KEY), PortCreator_Get(PC_CHARA), PortCreator_Get(PC_COSTUME));
        turn = PortCreator_Get(PC_TURN);
        if (turn != 0) {
            Mm_ViewTurn((f32)turn * 0.001f);
        }
        for (se = PortCreator_Get(PC_SOUND); se >= 0; se = PortCreator_Get(PC_SOUND)) {
            Snd_PlaySe(1, se);
        }
        Mx_Begin();
        {
            s32 photo = PortCreator_Get(PC_PHOTO) > 0;
            s32 stage = !photo && PortCreator_Get(PC_STAGE) > 0;

            if (photo) { /* (the model alone on a plain colour: the screen keys it out, gs/creator_ui.cpp) */
                Mx_Rect(-400.0f, -100.0f, 1776.0f, 868.0f, Mx_Rgba(0xFF, 0x00, 0xFF, 0x80), Mx_Rgba(0xFF, 0x00, 0xFF, 0x80));
            } else {
                Mx_Background(0);
            }
            if (!photo && !stage && PortCreator_Get(PC_PLATFORM)) {
                Mx_Platform(x, feet + Mx_LenY(4.0f), h * 0.82f);
            }
            Mm_ViewSetStage(stage);
            Mm_ViewSetHost(photo ? -1 : PortCreator_Get(PC_HOST), PC_HOST_X, PC_HOST_FEET, PC_HOST_H, PC_HOST_YAW);
        }
        if (Mm_ViewObj() != NULL) {
            BtlObj_PortStanceView(Mm_ViewObj()); /* (the fighting stance while the Stance Maker is open) */
        }
        Mm_ViewDraw(x, feet, h);
        if (PortCreator_Get(PC_STANCE) == 1 && Mm_ViewObj() != NULL && gBtlCamView != NULL) {
            BtlObj_PortStanceCapture(Mm_ViewObj(), gBtlCamView + 0x140, *(f32 *)(gBtlCamView + 0x240), *(f32 *)(gBtlCamView + 0x244));
        }
        ColorFade_Draw();
        File_Stub264D90();
        Gfx_EndFrame(1);
        Dma_Flush();
        frames++;
        if (!leaving && ColorFade_IsInDone() && PortCreator_Get(PC_LEAVE)) {
            leaving = PortCreator_Get(PC_LEAVE);
            if (leaving == 2) {
                testChara = PortCreator_Get(PC_TEST_CHARA);
                testCostume = PortCreator_Get(PC_TEST_COSTUME);
                test = testChara >= 0;
            }
            Snd_PlaySe(1, test ? 1 : 2);
            ColorFade_StartOut(0, 0, 0, 0x14);
        }
        if (leaving && ColorFade_IsOutDone()) {
            break;
        }
    }
    sceGsSyncPath(0, 0);
    Mm_ViewClose();
    Port_EscQuit(1);
    PortCreator_Close();
    Dma_ResetBuffers();
    if (test) {
        gPortModTest = 2;
        Battle_ClearWork();
        /* a training fight, set up as the Mod Manager's test fight is (PortModMgr_Run) */
        BattleSetup_SetRule(0, 6, 8, 0, 1, Port_ModTestStage(), 0);
        BattleSetup_SetSide(0, 0, 0, 1, 1, 1, 0, NULL);
        BattleSetup_SetSide(1, 2, 1, 1, 1, 1, 0, NULL);
        BattleSetup_SetMember(0, 0, testChara, testCostume >= 0 && testCostume < PORT_COSTUME_MAX ? testCostume : 0, 0, -1, 100.0f, noItems);
        BattleSetup_SetMember(1, 0, gMmTestFoe, 0, 0, -1, 100.0f, noItems);
        BattleSetup_Finish();
    }
    return test;
}
#endif

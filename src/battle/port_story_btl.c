#include "common.h"
#ifdef PORT
#include "sys/gsc.h"
#include "sys/pad.h"
#include "sys/save.h"
#include "battle/btl_script.h"
#include "battle/btl_seq.h"
#include "plat_songs.h"
#include "port_story.h" /* what the editor's half tells this one: a line, the rules, an event, an action */
#include <stdio.h>
#include <stdlib.h>
/* the referee's lines in the log, written at once (a test that ends by a timeout keeps them) */
#define PS_LOG(...) do { fprintf(stderr, __VA_ARGS__); fflush(stderr); } while (0)

/*
 * PC only: the battle half of the Story Creator (main menu item 14; menu/port_story.c is its menu half, and
 * port/src/gs/story_ui.cpp the editor, the saga files and the text boxes).
 *
 * A saga's episode is played as a battle of the story mode (battle mode 1), the way Dragon History plays its own: a
 * battle script (the GSC engine, sys/gsc.c) is made of the episode (story_ui.cpp: PortStoryUi_ScriptSize / Copy) and
 * stands in for the script file the battle would read (BtlLoad, state 0x5A: PortStory_Script). Its setup action
 * (1000) sets the rule, the sides and the members (the game's commands 10, 11, 12); its main action starts the opening
 * scene, played during the sequence's intro state (1), where mode 1 waits for the script: the two fighters stand on
 * the real stage, under the script's camera (DemoCam's fixed pose), with their poses (scripted moves, command 901's
 * BtlFacade_PlayCharMotion), their auras and their voices, as in the game's own story scenes. Its last command (8 1)
 * lets the fight start. The scenes after the fight are event actions (10030 / 10031 lost, 10040 / 10041 won; the
 * second of each when the clock decided), started through the game's own trigger list once the fight allows it
 * (BtlScript_StartPending), and played during the winner state (5). A scene in the middle of the fight is an event
 * action too (10001..): the fight is interrupted for it the way Dragon History interrupts its own, and goes on after.
 *
 * The commands of the game do the plumbing (BeginScene, EndScene, SetResult, SetTriggers, Battle); the Story Creator's
 * own (ids 9001..9007, added to the game's table for every battle: PortStory_Commands) do what a saga says, worked out
 * on the stage the episode is on:
 *   9001 scene      distance flags        the two fighters face each other `distance` apart in the middle of the
 *                                          stage's start places, standing; flags: 1 / 2 hide side 0 / 1
 *   9002 line       n                     line n of the episode: where the two stand, the camera shot (a cut or a
 *                                          move), poses, effects, sound and music, the voice (lip-synced for a
 *                                          fighter's own line), the text (a box typed out, or a card; Cross completes
 *                                          it, then goes on), the answers of a question, or its own time
 *   9003 end now    result                ends a battle that has no fight (a scene-only episode): result 1 won
 *   9004 watch      -                     the fight's referee (its own task, action 2000): the rules at the start,
 *                                          the win / lose conditions, the clock, and the fight's events (when
 *                                          something happens, a list of things done in turn: a line in a box while the
 *                                          fight goes on, a scene that stops it, health, ki, a transformation...)
 *   9005 title      -                     the episode's title card
 *   9006 scene end  -                     the letterbox goes, both fighters shown again
 * The text, the portraits and the letterbox are drawn by story_ui.cpp over the picture; what to draw is told to it
 * here (PortStoryUi_Show). Nothing of it runs outside a Story Creator battle: the commands are only in its scripts.
 */

#define PS_SCRIPT 900 /* the script number a saga's battle is set up with (menu/port_story.c) */

extern s32 PortStoryUi_ScriptSize(void);
extern s32 PortStoryUi_AutoWait(void);
extern s32 PortStoryUi_AutoPick(void);
extern void PortStoryUi_ScriptCopy(void *dst);
extern s32 PortStoryUi_Line(s32 n, s32 *out);
extern s32 PortStoryUi_Battle(s32 *out);
extern s32 PortStoryUi_Event(s32 n, s32 *out);
extern s32 PortStoryUi_Action(s32 n, s32 *out);
extern void PortStoryUi_Show(s32 what, s32 a, s32 b);
extern s32 PortStory_Playing(void); /* menu/port_story.c: a saga's battle is set up or running */

extern void *Heap_Alloc(u32 size, s32 align, s32 a, s32 b);
extern f32 atan2f(f32 y, f32 x);
extern f32 sinf(f32 x);
extern f32 cosf(f32 x);
extern f32 sqrtf(f32 x);

extern void *memset(void *dst, s32 c, u32 n);
extern BtlScriptWork gBtlScript;
extern BtlSeq *gBtlSeq;

extern s32 BtlStage_GetStartPlace(s32 side, Vec4 *pos, Vec4 *rot, s32 alt);
extern void BtlFacade_SetCharPos(s32 side, Vec4 *pos);
extern void BtlFacade_SetCharRot(s32 side, Vec4 *dir);
extern void BtlFacade_PlayCharMotion(s32 side, s32 type, s32 mode, f32 value);
extern void BtlFacade_SetFixedCamera(Vec4 *pos, Vec4 *rot);
extern void BtlFacade_GetFixedCamera(Vec4 *pos, Vec4 *rot);
extern s32 BtlFacade_ShakeCamera(f32 time);
extern void BtlFacade_SetCtrlFE(s32 side);
extern void BtlFacade_SetCtrlFF(s32 side);
extern void BtlFacade_SetCtrl101(s32 side);
extern void BtlFacade_ClearCtrl101(s32 side);
extern void BtlFacade_SetCtrl102(s32 side);
extern void BtlFacade_ClearCtrl100(s32 side);
extern void BtlFacade_SetCtrl100(s32 side);
extern s32 BtlFacade_GetActiveMember(s32 side);
extern void BtlFacade_EndObjSubState3(s32 side);
extern void BtlFacade_AddHp(s32 side, s32 member, f32 percent);
extern void BtlFacade_RaiseHp(s32 side, s32 member, f32 percent);
extern void BtlFacade_LowerHp(s32 side, s32 member, f32 percent);
extern void BtlFacade_AddGaugeC(s32 side, s32 member, s32 bars);
extern void BtlFacade_RaiseGaugeC(s32 side, s32 member, s32 bars);
extern void BtlFacade_LowerGaugeC(s32 side, s32 member, s32 bars);
extern void BtlFacade_AddGauge14(s32 side, s32 member, s32 stocks);
extern void BtlFacade_RaiseGauge14(s32 side, s32 member, s32 stocks);
extern void BtlFacade_LowerGauge14(s32 side, s32 member, s32 stocks);
extern void BtlFacade_SetMaxPower(s32 side, s32 on);
extern void BtlFacade_RequestStageChange(void);
extern void BtlFacade_SetCpuParam8(s32 level);
extern s32 BtlFacade_CanCharAct(s32 side);
extern s32 BtlCtrl_Transform(s32 player, s32 id);
extern s32 BtlCtrl_ChangeMember(s32 player, s32 member);
extern s32 BtlCtrl_UseTechnique(s32 player, s32 kind);
extern void BtlCtrl_SetCpuLevel(s32 player, s32 level);
extern s32 BtlCtrl_IsActiveDead(s32 player);
extern s32 BtlParam_GetSlotId(void *chr, s32 slot);
extern s32 BtlSide_GetKi(s32 side);
extern s32 BtlSide_GetBlast(s32 side);
extern s32 BtlSide_IsPoweredUp(s32 side);
extern s32 BtlSide_CountAlive(s32 side);
extern s32 BtlSide_GetComboHits(s32 side);
extern s32 BtlCharApi_GetMemberCount(s32 objId);
extern s32 BtlCharApi_GetMemberHpPercent(s32 objId, s32 member);
extern s32 BtlCharApi_IsInTechnique(s32 objId);
extern s32 BattleSide_GetObjId(s32 side);
extern void *BtlObj_Get(s32 id);
extern void BtlObj_SetSubState(void *obj, s32 mode, s32 arg);
extern void BtlCharApi_GetPos(s32 objId, Vec4 *out);
extern void BtlCharApi_GetRot(s32 objId, Vec4 *out);
extern void BtlCharApi_GetNodePos(s32 objId, s32 node, Vec4 *out);
extern s32 BtlCharApi_GetChara(s32 objId);
extern void *BtlChar_Get(s32 side);
extern void *BtlMember_GetActiveGauge(void *chr);
extern s32 BtlCtrl_IsTeamDead(s32 side);
extern s32 BtlClock_ToSeconds(BtlClock *clock);
extern void BattleResult_Set(s32 flags, s32 reason);
extern void Voice_PlayChara(s32 voice, s32 chara, s32 line);
extern void Voice_Stop(s32 voice);
extern s32 Voice_IsStopped(s32 voice);
extern s32 Adx_GetStat(s32 ch);
extern void Bgm_Play(s32 id);
extern void Bgm_Stop(void);
extern s32 Snd_PlaySe(u32 mask, s32 id);

#define PS_PI 3.14159265f

/* A line's voice: a character's spoken line (Voice_PlayChara: either language, an added character's own), or
   (chara -2) a voiced line of Dragon History, file 0xD48 + story * 300 + line (as BtlScript_GetVoiceFile). */
static void PortStory_PlayVoice(s32 ch, s32 chara, s32 line) {
    extern void Voice_PlayDefault(s32 voice, s32 id);

    if (chara == -2) {
        Voice_PlayDefault(ch, ((gSaveData->flags & SAVE_FLAG_VOICE) ? 0x47E0 : 0xD48) + line);
    } else {
        Voice_PlayChara(ch, chara, line);
    }
}

/* The music changes: a song of the music list (the game's or an added one), or (-2) silence. */
static void PortStory_Music(s32 song) {
    if (song == -2) {
        Bgm_Stop();
    } else if (song >= 0) {
        Bgm_Play(PORT_BGM_FILE(song));
    }
}

/* ---- the script --------------------------------------------------------------------------------------------- */

/* BtlLoad's script (state 0x5A): the episode's, in a block of the heap as the file would be (BtlLoad_FreeAll frees
   it); NULL for any other script. */
void *PortStory_Script(s32 script) {
    s32 size;
    void *p;

    if (script != PS_SCRIPT || !PortStory_Playing()) {
        return NULL;
    }
    size = PortStoryUi_ScriptSize();
    if (size <= 0) {
        return NULL;
    }
    p = Heap_Alloc((u32)size, 0x40, 0, 2);
    if (p != NULL) {
        PortStoryUi_ScriptCopy(p);
    }
    return p;
}

/* ---- the scene's places ------------------------------------------------------------------------------------- */

typedef struct PsScene {
    Vec4 pos[2];     /* where the fighters stand (their feet; y is down) */
    f32 yaw[2];      /* which way they face (radians) */
    s32 hidden[2];
    s32 lines;       /* lines said in this scene so far (the automatic camera's choice) */
    s32 how;         /* how far apart they were put (PortStory_Place) */
    s32 stand[2];    /* 0 on the ground, 1 in the air, 2 high up */
    s32 face[2];     /* 0 facing the other, 1 back turned, 2 side on */
    s32 black;       /* the picture has gone to black (a line's fade-out): cards are read on it, the next spoken line
                        brings it back */
} PsScene;
static PsScene sScene;

static Vec4 V(f32 x, f32 y, f32 z) {
    Vec4 v;

    v.x = x;
    v.y = y;
    v.z = z;
    v.w = 1.0f;
    return v;
}

/* How tall a fighter stands: from its feet to its highest model node (its head or hair), measured on its pose of now. */
static f32 PortStory_Height(s32 side) {
    s32 obj = BattleSide_GetObjId(side);
    Vec4 root;
    Vec4 n;
    f32 top = 0.0f;
    s32 i;

    if (BtlObj_Get(obj) == NULL) {
        return 16.0f;
    }
    BtlCharApi_GetPos(obj, &root);
    for (i = 0; i < 64; i++) {
        BtlCharApi_GetNodePos(obj, i, &n);
        if (root.y - n.y > top) {
            top = root.y - n.y;
        }
    }
    if (top < 6.0f || top > 400.0f) { /* (nothing measured, or something far away) */
        top = 16.0f;
    }
    return top;
}

/* Works out the two marks from how the scene has them (how far apart: 0 where the stage starts them, 1 close, 2 at
   talking distance, 3 far, in heights of the taller one; each on the ground or in the air; each facing the other or
   not), in the middle between the stage's start places, and puts the fighters on them. */
static void PortStory_Marks(void) {
    static const f32 kApart[4] = { 0.0f, 2.6f, 4.2f, 7.5f };
    static const f32 kLift[3] = { 0.0f, 1.6f, 4.5f };
    static const f32 kTurn[3] = { 0.0f, PS_PI, PS_PI * 0.5f };
    Vec4 s0;
    Vec4 s1;
    Vec4 r;
    f32 dx;
    f32 dz;
    f32 full;
    f32 dist;
    f32 h0 = PortStory_Height(0);
    f32 h1 = PortStory_Height(1);
    f32 hm = h0 > h1 ? h0 : h1;
    f32 t;
    s32 how = sScene.how;
    s32 i;

    BtlStage_GetStartPlace(0, &s0, &r, 0);
    BtlStage_GetStartPlace(1, &s1, &r, 0);
    dx = s1.x - s0.x;
    dz = s1.z - s0.z;
    full = sqrtf(dx * dx + dz * dz);
    if (full < 1.0f) {
        dx = 0.0f;
        dz = 1.0f;
        full = 1.0f;
    }
    dx /= full;
    dz /= full;
    dist = (how > 0 && how < 4 ? kApart[how] : 0.0f) * hm;
    if (dist <= 0.0f || dist > full) {
        dist = full;
    }
    t = 0.5f * dist / full;
    for (i = 0; i < 2; i++) {
        f32 k = i == 0 ? 0.5f - t : 0.5f + t;
        s32 st = sScene.stand[i] > 0 && sScene.stand[i] < 3 ? sScene.stand[i] : 0;
        s32 fc = sScene.face[i] > 0 && sScene.face[i] < 3 ? sScene.face[i] : 0;
        Vec4 rot;

        sScene.pos[i] = V(s0.x + (s1.x - s0.x) * k, s0.y + (s1.y - s0.y) * k - kLift[st] * hm, s0.z + (s1.z - s0.z) * k);
        sScene.yaw[i] = atan2f(i == 0 ? dx : -dx, i == 0 ? dz : -dz) + kTurn[fc];
        rot = V(0.0f, sScene.yaw[i], 0.0f);
        BtlFacade_SetCharPos(i, &sScene.pos[i]);
        BtlFacade_SetCharRot(i, &rot);
    }
}

/* A scene's start: the two put `how` far apart, on the ground, facing each other, standing. */
static void PortStory_Place(s32 how, s32 flags) {
    s32 i;

    sScene.how = how;
    for (i = 0; i < 2; i++) {
        sScene.stand[i] = 0;
        sScene.face[i] = 0;
    }
    PortStory_Marks();
    for (i = 0; i < 2; i++) {
        BtlFacade_PlayCharMotion(i, 0, 1, 0.0f);
        sScene.hidden[i] = (flags >> i) & 1;
        if (sScene.hidden[i]) {
            BtlFacade_SetCtrl100(i);
        } else {
            BtlFacade_ClearCtrl100(i);
        }
    }
    sScene.lines = 0;
}

/* A camera looking from `eye` at `at`: its pose as the fixed camera takes it (rotation x up, y round; radians). */
static void PortStory_Look(Vec4 *eye, Vec4 *at, Vec4 *rot) {
    f32 dx = at->x - eye->x;
    f32 dz = at->z - eye->z;
    f32 h = sqrtf(dx * dx + dz * dz);

    *rot = V(atan2f(eye->y - at->y, h > 0.01f ? h : 0.01f), atan2f(dx, dz), 0.0f);
}

/* The shot `kind` of speaker side s (the other is the listener): where the camera is and where it looks. */
static void PortStory_Shot(s32 kind, s32 s, s32 variant, Vec4 *eye, Vec4 *at) {
    s32 l = s ? 0 : 1;
    Vec4 S = sScene.pos[s];
    Vec4 L = sScene.pos[l];
    Vec4 M = V((S.x + L.x) * 0.5f, (S.y + L.y) * 0.5f, (S.z + L.z) * 0.5f);
    f32 hs = PortStory_Height(s);
    f32 hl = PortStory_Height(l);
    f32 dx = L.x - S.x;
    f32 dz = L.z - S.z;
    f32 d = sqrtf(dx * dx + dz * dz);
    f32 px;
    f32 pz;
    f32 side = (variant & 1) ? -1.0f : 1.0f; /* which side of the line the camera stays on */
    f32 hm = hs > hl ? hs : hl;

    if (d < 0.01f) {
        dx = 0.0f;
        dz = 1.0f;
        d = 1.0f;
    }
    dx /= d;
    dz /= d;
    px = dz * side; /* across the line between them */
    pz = -dx * side;
    switch (kind) {
    case PS_CAM_LISTENER:
        PortStory_Shot(PS_CAM_CLOSE, l, variant, eye, at);
        return;
    case PS_CAM_MEDIUM: /* the speaker from the waist up, from in front, a little to the side */
        *eye = V(S.x + dx * hs * 1.9f + px * hs * 0.7f, S.y - hs * 0.72f, S.z + dz * hs * 1.9f + pz * hs * 0.7f);
        *at = V(S.x, S.y - hs * 0.66f, S.z);
        break;
    case PS_CAM_SHOULDER: /* over the listener's shoulder at the speaker */
        *eye = V(L.x + dx * hl * 0.55f + px * hl * 0.32f, L.y - hl * 0.93f, L.z + dz * hl * 0.55f + pz * hl * 0.32f);
        *at = V(S.x, S.y - hs * 0.78f, S.z);
        break;
    case PS_CAM_TWO: /* both, from the side */
        *eye = V(M.x + px * (d * 0.62f + hm * 0.7f), M.y - hm * 0.6f, M.z + pz * (d * 0.62f + hm * 0.7f));
        *at = V(M.x, M.y - hm * 0.55f, M.z);
        break;
    case PS_CAM_WIDE: /* the stage with both in it, from high up and far */
        *eye = V(M.x + px * (d * 1.05f + hm * 2.0f) - dx * d * 0.3f, M.y - hm * 1.6f, M.z + pz * (d * 1.05f + hm * 2.0f) - dz * d * 0.3f);
        *at = V(M.x, M.y - hm * 0.45f, M.z);
        break;
    case PS_CAM_LOW: /* from the ground up at the speaker */
        *eye = V(S.x + dx * hs * 1.5f + px * hs * 0.45f, S.y - hs * 0.12f, S.z + dz * hs * 1.5f + pz * hs * 0.45f);
        *at = V(S.x, S.y - hs * 0.88f, S.z);
        break;
    case PS_CAM_HIGH: /* down on the speaker */
        *eye = V(S.x + dx * hs * 1.6f + px * hs * 0.5f, S.y - hs * 2.1f, S.z + dz * hs * 1.6f + pz * hs * 0.5f);
        *at = V(S.x, S.y - hs * 0.55f, S.z);
        break;
    case PS_CAM_FACE: /* the eyes: closer than close */
        *eye = V(S.x + dx * hs * 0.52f + px * hs * 0.16f, S.y - hs * 0.9f, S.z + dz * hs * 0.52f + pz * hs * 0.16f);
        *at = V(S.x, S.y - hs * 0.89f, S.z);
        break;
    case PS_CAM_BEHIND: /* from behind the speaker, past it at the listener */
        *eye = V(S.x - dx * hs * 1.25f + px * hs * 0.5f, S.y - hs * 1.08f, S.z - dz * hs * 1.25f + pz * hs * 0.5f);
        *at = V(L.x, L.y - hl * 0.7f, L.z);
        break;
    case PS_CAM_PROFILE: /* the speaker from the side */
        *eye = V(S.x + px * hs * 1.7f + dx * hs * 0.15f, S.y - hs * 0.8f, S.z + pz * hs * 1.7f + dz * hs * 0.15f);
        *at = V(S.x, S.y - hs * 0.76f, S.z);
        break;
    case PS_CAM_TOP: /* straight down on the two of them */
        *eye = V(M.x + px * hm * 0.5f, M.y - (d * 1.1f + hm * 3.2f), M.z + pz * hm * 0.5f);
        *at = V(M.x, M.y, M.z);
        break;
    case PS_CAM_GROUND: /* both, from down on the ground */
        *eye = V(M.x + px * (d * 0.7f + hm * 0.9f), M.y - hm * 0.07f, M.z + pz * (d * 0.7f + hm * 0.9f));
        *at = V(M.x, M.y - hm * 0.62f, M.z);
        break;
    case PS_CAM_FAR: /* two small figures in a big place */
        *eye = V(M.x + px * (d * 2.2f + hm * 7.0f) - dx * d * 0.6f, M.y - hm * 3.2f, M.z + pz * (d * 2.2f + hm * 7.0f) - dz * d * 0.6f);
        *at = V(M.x, M.y - hm * 0.5f, M.z);
        break;
    case PS_CAM_FULL: /* the speaker from head to foot */
        *eye = V(S.x + dx * hs * 2.7f + px * hs * 1.0f, S.y - hs * 0.62f, S.z + dz * hs * 2.7f + pz * hs * 1.0f);
        *at = V(S.x, S.y - hs * 0.5f, S.z);
        break;
    default: /* PS_CAM_CLOSE: the speaker's face, three quarters from in front */
        *eye = V(S.x + dx * hs * 0.95f + px * hs * 0.42f, S.y - hs * 0.86f, S.z + dz * hs * 0.95f + pz * hs * 0.42f);
        *at = V(S.x, S.y - hs * 0.84f, S.z);
        break;
    }
}

/* Sets the fixed camera to a shot, or starts a move to it from `from` over `seconds` (the script's camera move:
   gBtlScript.cam, run by BtlScript_UpdateView). */
static void PortStory_Camera(Vec4 *eye, Vec4 *at, Vec4 *fromEye, Vec4 *fromAt, f32 seconds) {
    BtlScriptCam *cam = &gBtlScript.cam;
    Vec4 rot;
    Vec4 rot0;
    s32 i;

    PortStory_Look(eye, at, &rot);
    cam->active = 0;
    cam->time = 0;
    cam->index = 0;
    for (i = 0; i < 9; i++) {
        cam->key[i].flags = 0;
    }
    if (fromEye == NULL || seconds <= 0.0f) {
        BtlFacade_SetFixedCamera(eye, &rot);
        return;
    }
    PortStory_Look(fromEye, fromAt, &rot0);
    while (rot.y - rot0.y > PS_PI) { /* (the short way round) */
        rot.y -= 2.0f * PS_PI;
    }
    while (rot.y - rot0.y < -PS_PI) {
        rot.y += 2.0f * PS_PI;
    }
    BtlFacade_SetFixedCamera(fromEye, &rot0);
    cam->key[0].pos = *fromEye;
    cam->key[0].rot = rot0;
    cam->key[0].time = 0.0f;
    cam->key[0].flags = 1;
    cam->key[1].pos = *eye;
    cam->key[1].rot = rot;
    cam->key[1].time = seconds;
    cam->key[1].flags = 1;
    cam->active = 1;
}

/* A line's shot, with its move. */
static void PortStory_LineCamera(s32 *li) {
    s32 kind = li[PL_CAMERA];
    s32 s = li[PL_SIDE];
    s32 variant = sScene.lines;
    f32 t = (f32)li[PL_MOVE_T] * 0.1f;
    Vec4 eye;
    Vec4 at;
    Vec4 e0;
    Vec4 a0;

    if (kind == PS_CAM_KEEP) {
        return;
    }
    if (s < 0) { /* a narrator: the two of them */
        s = 0;
        if (kind == PS_CAM_AUTO) {
            kind = PS_CAM_TWO;
        }
    }
    if (sScene.hidden[s]) { /* nobody to look at on that side: at the other */
        s = s ? 0 : 1;
    }
    if (kind == PS_CAM_AUTO) { /* the first line both of them, then the speaker close and over the shoulder in turn */
        static const s8 kAuto[4] = { PS_CAM_CLOSE, PS_CAM_SHOULDER, PS_CAM_MEDIUM, PS_CAM_CLOSE };

        kind = sScene.lines == 0 ? PS_CAM_TWO : kAuto[sScene.lines % 4];
        if (sScene.hidden[s ? 0 : 1] && kind == PS_CAM_SHOULDER) {
            kind = PS_CAM_MEDIUM;
        }
        if (sScene.lines == 0 && li[PL_MOVE] == PS_MOVE_CUT) {
            li[PL_MOVE] = PS_MOVE_FROM_WIDE;
            t = 3.0f;
        }
    }
    PortStory_Shot(kind, s, variant, &eye, &at);
    if (t <= 0.0f) {
        t = 3.0f;
    }
    switch (li[PL_MOVE]) {
    case PS_MOVE_PUSH: /* from further back, closing in */
        e0 = V(eye.x + (eye.x - at.x) * 0.6f, eye.y, eye.z + (eye.z - at.z) * 0.6f); /* (at its height: from a low shot, further back is not under the ground) */
        PortStory_Camera(&eye, &at, &e0, &at, t);
        break;
    case PS_MOVE_CRASH: /* the same from far and at once: a crash zoom */
        e0 = V(eye.x + (eye.x - at.x) * 1.8f, eye.y, eye.z + (eye.z - at.z) * 1.8f);
        PortStory_Camera(&eye, &at, &e0, &at, 0.35f);
        break;
    case PS_MOVE_PULL: /* from close, moving back */
        e0 = V(at.x + (eye.x - at.x) * 0.55f, eye.y, at.z + (eye.z - at.z) * 0.55f);
        PortStory_Camera(&eye, &at, &e0, &at, t);
        break;
    case PS_MOVE_ORBIT: /* round the one looked at, a quarter of the way */
    case PS_MOVE_ORBIT_BACK: /* (the other way) */
        {
            f32 rx = eye.x - at.x;
            f32 rz = eye.z - at.z;
            f32 c = cosf(0.6f);
            f32 sn = li[PL_MOVE] == PS_MOVE_ORBIT ? sinf(0.6f) : -sinf(0.6f);

            e0 = V(at.x + rx * c - rz * sn, eye.y, at.z + rx * sn + rz * c);
            PortStory_Camera(&eye, &at, &e0, &at, t);
        }
        break;
    case PS_MOVE_FROM_WIDE: /* from the stage's wide view down to the shot */
        PortStory_Shot(PS_CAM_WIDE, s, variant, &e0, &a0);
        PortStory_Camera(&eye, &at, &e0, &a0, t);
        break;
    case PS_MOVE_RISE: /* from low, rising */
        e0 = V(eye.x, eye.y + (sScene.pos[s].y - eye.y) * 0.8f, eye.z);
        PortStory_Camera(&eye, &at, &e0, &at, t);
        break;
    case PS_MOVE_DROP: /* from above, coming down */
        e0 = V(eye.x, eye.y - PortStory_Height(s) * 1.4f, eye.z);
        PortStory_Camera(&eye, &at, &e0, &at, t);
        break;
    case PS_MOVE_SLIDE: /* sideways past them, looking the same way */
        {
            f32 rx = (eye.z - at.z) * 0.45f;
            f32 rz = -(eye.x - at.x) * 0.45f;

            e0 = V(eye.x + rx, eye.y, eye.z + rz);
            a0 = V(at.x + rx, at.y, at.z + rz);
            PortStory_Camera(&eye, &at, &e0, &a0, t);
        }
        break;
    case PS_MOVE_PAN: /* from the other one over to this one, the camera standing still */
        {
            s32 l = s ? 0 : 1;

            a0 = V(sScene.pos[l].x, sScene.pos[l].y - PortStory_Height(l) * 0.8f, sScene.pos[l].z);
            PortStory_Camera(&eye, &at, &eye, &a0, t);
        }
        break;
    default:
        PortStory_Camera(&eye, &at, NULL, NULL, 0.0f);
        break;
    }
}

/* A line's effects, at its start. */
static void PortStory_Effects(s32 *li) {
    s32 fx = li[PL_FX];
    s32 s = li[PL_SIDE] < 0 ? 0 : li[PL_SIDE];
    s32 l = s ? 0 : 1;

    if (sScene.black && li[PL_CARD] <= 0) { /* (after a fade to black the picture comes back for the next spoken line) */
        PortStoryUi_Show(PS_SHOW_FADE, -1, -8);
        sScene.black = 0;
    }
    if (fx & PS_FX_SHAKE) {
        BtlFacade_ShakeCamera(0.6f);
    }
    if (fx & PS_FX_BIG_SHAKE) {
        BtlFacade_ShakeCamera(2.0f);
    }
    if (fx & PS_FX_RUMBLE) {
        BtlFacade_ShakeCamera(6.0f);
    }
    if (fx & PS_FX_AURA_ON) {
        BtlFacade_SetCtrlFE(s);
    }
    if (fx & PS_FX_AURA_OFF) {
        BtlFacade_SetCtrlFF(s);
    }
    if (fx & PS_FX_L_AURA_ON) {
        BtlFacade_SetCtrlFE(l);
    }
    if (fx & PS_FX_L_AURA_OFF) {
        BtlFacade_SetCtrlFF(l);
    }
    if (fx & PS_FX_CHARGE_ON) {
        BtlFacade_SetCtrl101(s);
    }
    if (fx & PS_FX_CHARGE_OFF) {
        BtlFacade_ClearCtrl101(s);
    }
    if (fx & PS_FX_BURST) {
        BtlFacade_SetCtrl102(s);
    }
    if (fx & PS_FX_L_BURST) {
        BtlFacade_SetCtrl102(l);
    }
    if (fx & PS_FX_HIDE) {
        BtlFacade_SetCtrl100(s);
        sScene.hidden[s] = 1;
    }
    if (fx & PS_FX_SHOW) {
        BtlFacade_ClearCtrl100(s);
        sScene.hidden[s] = 0;
    }
    if (fx & PS_FX_L_HIDE) {
        BtlFacade_SetCtrl100(l);
        sScene.hidden[l] = 1;
    }
    if (fx & PS_FX_L_SHOW) {
        BtlFacade_ClearCtrl100(l);
        sScene.hidden[l] = 0;
    }
    if (fx & PS_FX_FLASH) {
        PortStoryUi_Show(PS_SHOW_FLASH, 255, 0);
    }
    if (fx & PS_FX_RED_FLASH) {
        PortStoryUi_Show(PS_SHOW_FLASH, 255, 1);
    }
    if (fx & PS_FX_FADE_IN) {
        PortStoryUi_Show(PS_SHOW_FADE, 255, -3); /* (the slow one a saga has always had, over the text too) */
        sScene.black = 0;
    }
    if (fx & PS_FX_FADE_OUT) {
        PortStoryUi_Show(PS_SHOW_FADE, -1, 12);
        sScene.black = 1;
    }
    switch (li[PL_TRANS]) {
    case PS_TRANS_FADE_IN:
        PortStoryUi_Show(PS_SHOW_FADE, 255, -8);
        sScene.black = 0;
        break;
    case PS_TRANS_FLASH:
        PortStoryUi_Show(PS_SHOW_FLASH, 255, 0);
        break;
    case PS_TRANS_DIP: /* (a cut through black: black at once, then quickly back) */
        PortStoryUi_Show(PS_SHOW_FADE, 255, -20);
        sScene.black = 0;
        break;
    }
    PortStoryUi_Show(PS_SHOW_DARK, (fx & PS_FX_DARK) != 0, 0);
}

/* ---- the commands ------------------------------------------------------------------------------------------- */

/* 9001 "scene": distance flags. */
static s32 PortStoryCmd_Scene(u32 phase, void *work) {
    s32 dist;
    s32 flags;
    Vec4 eye;
    Vec4 at;

    if (phase == GSC_PHASE_BEGIN || phase == GSC_PHASE_BEGIN_ABORT) {
        dist = Gsc_GetInt();
        flags = Gsc_GetInt();
        PortStory_Place(dist, flags);
        sScene.black = 0;
        PortStory_Shot(PS_CAM_TWO, 0, 0, &eye, &at);
        PortStory_Camera(&eye, &at, NULL, NULL, 0.0f);
        PortStoryUi_Show(PS_SHOW_LETTERBOX, 1, 0);
    }
    return 1;
}

/* 9006 "scene end". */
static s32 PortStoryCmd_SceneEnd(u32 phase, void *work) {
    s32 i;

    if (phase == GSC_PHASE_BEGIN || phase == GSC_PHASE_BEGIN_ABORT) {
        for (i = 0; i < 2; i++) {
            if (sScene.hidden[i]) {
                BtlFacade_ClearCtrl100(i);
                sScene.hidden[i] = 0;
            }
        }
        PortStoryUi_Show(PS_SHOW_HIDE, 0, 0);
        PortStoryUi_Show(PS_SHOW_DARK, 0, 0);
        PortStoryUi_Show(PS_SHOW_FADE, -1, -12); /* (a scene that ended in black: the picture comes back) */
        sScene.black = 0;
        PortStoryUi_Show(PS_SHOW_LETTERBOX, 0, 0);
    }
    return 1;
}

/* Task work of 9002 (a command's work is 0x20 bytes). */
typedef struct PsLineWork {
    s16 line;
    s16 frames;
    s16 shown;     /* letters typed out, in sixteenths */
    s16 full;      /* frames since the text was all out */
    s8 voice;      /* the voice's channel, -1 none */
    s8 mouth;      /* 1 talking (a loop), 2 lip-synced, 3 the lip's start waited for */
    s8 obj;        /* the speaker is on the stage (its side) + 1 */
    s8 said;       /* the line has started (after its delay) */
    s16 len;
    s16 wait;
    s32 lip;
    s16 delay;     /* frames of silence before it is said */
    s8 choice;     /* how many answers it asks for */
    s8 cursor;
    s8 out;        /* frames of its fade to black done, -1 none to do */
    s8 picked;
} PsLineWork;

static void PortStory_LineEnd(PsLineWork *w) {
    if (w->voice >= 0 && !Voice_IsStopped(w->voice)) {
        Voice_Stop(w->voice);
    }
    if (w->obj > 0 && w->mouth != 0) {
        BtlObj_SetSubState(BtlObj_Get(BattleSide_GetObjId(w->obj - 1)), 0, -1);
    }
    w->mouth = 0;
    if (w->choice > 0 && !w->picked) { /* (a question skipped: its first answer) */
        w->picked = 1;
        PortStoryUi_Show(PS_SHOW_CHOSEN, w->line, 0);
    }
    PortStoryUi_Show(PS_SHOW_HIDE, 0, 0);
}

/* The line is said: its voice, its lips, its text. */
static void PortStory_LineSay(PsLineWork *w, s32 *li) {
    w->said = 1;
    if (li[PL_VOICE] >= 0) {
        w->voice = (s8)(li[PL_SIDE] > 0 ? 1 : 0);
        Voice_Stop(w->voice);
        PortStory_PlayVoice(w->voice, li[PL_VCHARA], li[PL_VOICE]);
    }
    if (w->obj > 0) {
        if (w->voice >= 0 && w->lip >= 0) {
            w->mouth = 3;
        } else if (w->len > 0) {
            BtlObj_SetSubState(BtlObj_Get(BattleSide_GetObjId(w->obj - 1)), 3, -1);
            w->mouth = 1;
        }
    }
    PortStoryUi_Show(PS_SHOW_SAY, w->line, 0);
}

/* 9002 "line": n. */
static s32 PortStoryCmd_Line(u32 phase, void *work) {
    PsLineWork *w = work;
    s32 li[PL_COUNT];
    s32 ret = 0;
    s32 i;

    switch (phase) {
    case GSC_PHASE_BEGIN:
        memset(w, 0, sizeof(*w));
        w->line = (s16)Gsc_GetInt();
        w->voice = -1;
        for (i = 0; i < PL_COUNT; i++) {
            li[i] = -1;
        }
        if (!PortStoryUi_Line(w->line, li)) {
            return 1;
        }
        /* where the two stand: as the line before left them, or moved by this one */
        if (li[PL_DIST] > 0) {
            sScene.how = li[PL_DIST];
        }
        if (li[PL_SIDE] >= 0 || li[PL_STAND] >= 0 || li[PL_FACE] >= 0 || li[PL_LSTAND] >= 0 || li[PL_LFACE] >= 0) {
            s32 s = li[PL_SIDE] > 0 ? 1 : 0;

            if (li[PL_STAND] >= 0) {
                sScene.stand[s] = li[PL_STAND];
            }
            if (li[PL_FACE] >= 0) {
                sScene.face[s] = li[PL_FACE];
            }
            if (li[PL_LSTAND] >= 0) {
                sScene.stand[1 - s] = li[PL_LSTAND];
            }
            if (li[PL_LFACE] >= 0) {
                sScene.face[1 - s] = li[PL_LFACE];
            }
        }
        if (li[PL_DIST] > 0 || li[PL_STAND] >= 0 || li[PL_FACE] >= 0 || li[PL_LSTAND] >= 0 || li[PL_LFACE] >= 0) {
            PortStory_Marks();
        } else {
            for (i = 0; i < 2; i++) { /* (back on their marks: a pose before may have carried a fighter off them) */
                Vec4 rot = V(0.0f, sScene.yaw[i], 0.0f);

                BtlFacade_SetCharPos(i, &sScene.pos[i]);
                BtlFacade_SetCharRot(i, &rot);
            }
        }
        PortStory_LineCamera(li);
        if (li[PL_SIDE] >= 0 && li[PL_POSE] >= 0) {
            BtlFacade_PlayCharMotion(li[PL_SIDE], li[PL_POSE], li[PL_POSE_LOOP] != 0, 0.0f);
        }
        if (li[PL_LPOSE] >= 0) {
            BtlFacade_PlayCharMotion(li[PL_SIDE] == 0 ? 1 : 0, li[PL_LPOSE], li[PL_LPOSE_LOOP] != 0, 0.0f);
        }
        PortStory_Effects(li);
        if (li[PL_SOUND] >= 0) {
            Snd_PlaySe(4, li[PL_SOUND]);
        }
        PortStory_Music(li[PL_MUSIC]);
        w->len = (s16)li[PL_LEN];
        w->wait = (s16)li[PL_WAIT];
        if (w->wait <= 0 && PortStoryUi_AutoWait() > 0) {
            w->wait = (s16)PortStoryUi_AutoWait();
        }
        w->lip = li[PL_LIP];
        w->delay = (s16)(li[PL_DELAY] > 0 ? li[PL_DELAY] : 0);
        w->choice = (s8)(li[PL_CHOICE] > 0 ? li[PL_CHOICE] : 0);
        w->out = (s8)(li[PL_TRANS] == PS_TRANS_FADE_OUT ? 0 : -1);
        if (li[PL_SIDE] >= 0 && !sScene.hidden[li[PL_SIDE]] && li[PL_CARD] <= 0 &&
            (li[PL_MEMBER] < 0 || li[PL_MEMBER] == BtlFacade_GetActiveMember(li[PL_SIDE]))) {
            w->obj = (s8)(li[PL_SIDE] + 1);
        }
        sScene.lines++;
        if (w->delay <= 0) {
            PortStory_LineSay(w, li);
        }
        break;
    case GSC_PHASE_UPDATE:
        w->frames++;
        if (!w->said) { /* its silence first */
            if (w->frames < w->delay) {
                break;
            }
            for (i = 0; i < PL_COUNT; i++) {
                li[i] = -1;
            }
            PortStoryUi_Line(w->line, li);
            PortStory_LineSay(w, li);
            w->frames = 0;
            break;
        }
        if (w->mouth == 3 && Adx_GetStat(w->voice + 4) == 3) { /* (the voice has started: its lips with it) */
            BtlObj_SetSubState(BtlObj_Get(BattleSide_GetObjId(w->obj - 1)), 2, w->lip);
            w->mouth = 2;
        }
        if (w->frames < 6) { /* (a press that ended the line before is not this one's) */
            break;
        }
        if (w->out > 0) { /* its fade to black, then on */
            w->out++;
            ret = w->out > 22;
            break;
        }
        if (w->shown < w->len * 16) {
            w->shown += 16 + 12; /* 1.75 letters a frame */
            if ((gPad[0].gamePressed & PADG_CROSS) || w->shown > w->len * 16) {
                w->shown = w->len * 16;
            }
            PortStoryUi_Show(PS_SHOW_SAY, w->line, w->shown / 16);
            if (w->shown >= w->len * 16 && w->mouth == 1) {
                BtlObj_SetSubState(BtlObj_Get(BattleSide_GetObjId(w->obj - 1)), 0, -1);
                w->mouth = 0;
            }
            break;
        }
        w->full++;
        if (w->choice > 0) { /* a question: up and down, Cross picks (a test run picks by itself) */
            if (gPad[0].gamePressed & PADG_UP) {
                w->cursor = (s8)(w->cursor > 0 ? w->cursor - 1 : w->choice - 1);
            }
            if (gPad[0].gamePressed & PADG_DOWN) {
                w->cursor = (s8)(w->cursor + 1 < w->choice ? w->cursor + 1 : 0);
            }
            if (PortStoryUi_AutoWait() > 0 && w->full >= PortStoryUi_AutoWait() + 30) {
                w->cursor = (s8)(PortStoryUi_AutoPick() % w->choice);
                ret = 1;
            } else if (w->full > 12 && (gPad[0].gamePressed & PADG_CROSS)) {
                ret = 1;
            }
            if (ret) {
                w->picked = 1;
                PortStoryUi_Show(PS_SHOW_CHOSEN, w->line, w->cursor);
            } else {
                PortStoryUi_Show(PS_SHOW_CHOICE, w->line, w->cursor);
            }
        } else if (w->full > 2 && (gPad[0].gamePressed & PADG_CROSS)) {
            ret = 1;
        } else if (w->wait > 0 && w->full >= w->wait && (w->voice < 0 || Voice_IsStopped(w->voice))) {
            ret = 1;
        } else if (w->len == 0 && w->voice >= 0 && Voice_IsStopped(w->voice) && w->full > 20) {
            ret = 1; /* (a voice without text: when it has been said) */
        }
        if (ret && w->out == 0) { /* (the line goes out into black) */
            PortStoryUi_Show(PS_SHOW_FADE, -1, 12);
            sScene.black = 1;
            w->out = 1;
            ret = 0;
        }
        break;
    case GSC_PHASE_END:
        PortStory_LineEnd(w);
        break;
    case GSC_PHASE_BEGIN_ABORT:
        if (Gsc_IsCurrentBegun()) {
            PortStory_LineEnd(w);
        } else { /* (a scene skipped: a question in it still gets its first answer, where the story goes depends on it) */
            for (i = 0; i < PL_COUNT; i++) {
                li[i] = -1;
            }
            i = Gsc_GetInt();
            if (PortStoryUi_Line(i, li) && li[PL_CHOICE] > 0) {
                PortStoryUi_Show(PS_SHOW_CHOSEN, i, 0);
            }
        }
        ret = 1;
        break;
    }
    return ret;
}

/* Goes to the sequence's end state from where it is (a battle that has no fight). */
static void PortStory_SeqTo(s32 state) {
    BtlSeq_Call(gBtlSeq->table[gBtlSeq->state].exit, gBtlSeq->state);
    gBtlSeq->state = state;
    BtlSeq_Call(gBtlSeq->table[gBtlSeq->state].enter, gBtlSeq->state);
}

/* 9003 "end now": result. */
static s32 PortStoryCmd_EndNow(u32 phase, void *work) {
    if (phase == GSC_PHASE_BEGIN || phase == GSC_PHASE_BEGIN_ABORT) {
        s32 won = Gsc_GetInt();

        BattleResult_Set(won ? 1 : 2, 0);
        PortStoryUi_Show(PS_SHOW_END, won, 0);
        PortStory_SeqTo(BTL_SEQ_END);
    }
    return 1;
}

/* 9005 "title": the episode's card, about three seconds (Cross shortens it). */
static s32 PortStoryCmd_Title(u32 phase, void *work) {
    s32 *w = work;
    s32 ret = 0;

    switch (phase) {
    case GSC_PHASE_BEGIN:
        w[0] = 0;
        PortStoryUi_Show(PS_SHOW_TITLE, 0, 0);
        break;
    case GSC_PHASE_UPDATE:
        w[0]++;
        if (w[0] > 12 && (gPad[0].gamePressed & PADG_CROSS) && w[0] < 150) {
            w[0] = 150;
        }
        PortStoryUi_Show(PS_SHOW_TITLE, w[0], 0);
        ret = w[0] >= 170;
        break;
    case GSC_PHASE_END:
        PortStoryUi_Show(PS_SHOW_TITLE, -1, 0);
        break;
    case GSC_PHASE_BEGIN_ABORT:
        PortStoryUi_Show(PS_SHOW_TITLE, -1, 0);
        ret = 1;
        break;
    }
    return ret;
}

/* ---- the referee -------------------------------------------------------------------------------------------- */

#define PS_EVENTS_MAX 96
#define PS_QUEUE 24

typedef struct PsWatch {
    s32 started;
    s32 decided;
    s32 frames;     /* frames of fight (a scene that stops it does not count) */
    s32 chara[2];   /* who was fighting on each side (a change: a transformation) */
    s32 member[2];
    s32 alive[2];   /* how many of each team stood, the frame before */
    s32 alive0;     /* and of the player's as the fight started */
    s32 maxed[2];
    s32 kiFull[2];
    u8 fired[PS_EVENTS_MAX];  /* 0 not yet, 1 happened (its actions wait or run), 2 done */
    s32 again[PS_EVENTS_MAX]; /* the fight frame from which an event that repeats can happen again */
    s32 hud;        /* the line in the box, -1 none */
    s32 hudFrames;
    s32 hudVoice;
    s32 card;       /* the line put up as a message, -1 none */
    s32 cardFrames;
    s32 objective;  /* frames the objective has been up, -1 gone */
    s32 queue[PS_QUEUE]; /* events whose actions wait their turn */
    s32 queued;
    s32 cur;        /* the event whose actions are being done, -1 none */
    s32 act;        /* its next action */
    s32 wait;       /* frames before the next action */
    s32 tries;      /* frames the action of now could not be done yet */
    s32 scene;      /* a scene that stops the fight: 0 none, 1 asked for, 2 playing */
    s32 sceneAction;
    s32 sceneFrom;  /* the clock when it started */
    s32 off;        /* seconds of the clock spent in such scenes */
    s32 bonus;      /* seconds the actions added to the limits */
    s32 forms[2];   /* transformations still to do at the start */
    s32 formWait;
    s32 ticks;      /* frames of the battle, its cinematics too */
    s32 down[2];    /* frames the side's fighter has been knocked out with team mates still standing */
    s32 blastTick;  /* the tick the player was last seen doing a blast (a technique: the ultimates' cinematics too) */
} PsWatch;
static PsWatch sWatch;

/* The share of its health the side's fighter has left, 0..100. */
static s32 PortStory_HealthPct(s32 side) {
    void *chr = BtlChar_Get(side);
    s32 *g;

    if (chr == NULL) {
        return 100;
    }
    g = BtlMember_GetActiveGauge(chr);
    if (g == NULL || g[1] <= 0) {
        return 100;
    }
    return g[0] * 100 / g[1];
}

/* The fight's seconds: the clock's, less what the scenes that stopped it took. */
static s32 PortStory_Seconds(void) {
    return BtlClock_ToSeconds(BtlSeq_GetClock()) - sWatch.off;
}

/* The trigger list's request for an event action (the list is emptied whenever an event starts: put back then). The
   trigger is one nothing raises: the referee starts these itself (BtlScript_StartPending, once the fight allows). */
static BtlScriptRequest *PortStory_Request(s32 action) {
    BtlScriptTalk *talk = &gBtlScript.talk;
    s32 i;

    for (i = 0; i < talk->requestCount; i++) {
        if (talk->requests[i].kind == BTL_SCRIPT_REQ_EVENT && talk->requests[i].u.start.action == action) {
            return &talk->requests[i];
        }
    }
    if (talk->requestCount >= 48) {
        return NULL;
    }
    BtlScript_AddEvent(0x7F, action);
    return &talk->requests[talk->requestCount - 1];
}

/* Starts the scene after the fight (event action 10040 won, 10030 lost; 10041 / 10031 when the clock decided) once
   the fight lets it: as the game's trigger list would. */
static void PortStory_Decide(s32 won, s32 byTime) {
    BtlScriptRequest *req = PortStory_Request((won ? 10040 : 10030) + (byTime ? 1 : 0));

    if (req != NULL) {
        gBtlScript.pending = req;
    }
    sWatch.decided = won ? 1 : 2;
    sWatch.scene = 0;
    PS_LOG("bt3: story: the fight is decided: %s%s (second %d, health %d%% / %d%%)\n", won ? "won" : "lost",
            byTime ? " on time" : "", PortStory_Seconds(), PortStory_HealthPct(0), PortStory_HealthPct(1));
    PortStoryUi_Show(PS_SHOW_FOUGHT, PortStory_Seconds(), PortStory_HealthPct(0));
}

/* A line said during the fight: in the box (story_ui.cpp), with its voice; the fight goes on. */
static void PortStory_HudLine(s32 line) {
    s32 li[PL_COUNT];

    if (!PortStoryUi_Line(line, li)) {
        return;
    }
    if (sWatch.hudVoice >= 0 && !Voice_IsStopped(sWatch.hudVoice)) {
        Voice_Stop(sWatch.hudVoice);
    }
    sWatch.hudVoice = -1;
    if (li[PL_VOICE] >= 0) {
        sWatch.hudVoice = li[PL_SIDE] > 0 ? 1 : 0;
        PortStory_PlayVoice(sWatch.hudVoice, li[PL_VCHARA], li[PL_VOICE]);
    }
    sWatch.hud = line;
    sWatch.hudFrames = 0;
    if (li[PL_FX] & (PS_FX_SHAKE | PS_FX_BIG_SHAKE)) {
        BtlFacade_ShakeCamera(li[PL_FX] & PS_FX_BIG_SHAKE ? 1.5f : 0.5f);
    }
    if (li[PL_FX] & PS_FX_FLASH) {
        PortStoryUi_Show(PS_SHOW_FLASH, 200, 0);
    }
    if (li[PL_FX] & PS_FX_RED_FLASH) {
        PortStoryUi_Show(PS_SHOW_FLASH, 200, 1);
    }
    if (li[PL_SOUND] >= 0) {
        Snd_PlaySe(4, li[PL_SOUND]);
    }
    PortStory_Music(li[PL_MUSIC]);
}

/* The side's next team member that still stands, -1 none. */
static s32 PortStory_NextMember(s32 side) {
    s32 obj = BattleSide_GetObjId(side);
    s32 n = BtlCharApi_GetMemberCount(obj);
    s32 a = BtlFacade_GetActiveMember(side);
    s32 i;

    for (i = 1; i < n; i++) {
        s32 m = (a + i) % n;

        if (BtlCharApi_GetMemberHpPercent(obj, m) > 0) {
            return m;
        }
    }
    return -1;
}

/* Does an action of an event. 1 done; 0 not yet (the fighter cannot do it this frame: tried again the next). */
static s32 PortStory_Do(s32 *a) {
    s32 side = a[PA_SIDE] ? 1 : 0;
    s32 v = a[PA_VALUE];
    s32 m = BtlFacade_GetActiveMember(side);
    void *chr = BtlChar_Get(side);

    switch (a[PA_KIND]) {
    case PS_DO_SAY:
        if (sWatch.hud >= 0 && sWatch.hudFrames <= 45) { /* (the line before it gets its time) */
            return 0;
        }
        PortStory_HudLine(v);
        break;
    case PS_DO_SCENE:
        sWatch.scene = 1;
        sWatch.sceneAction = v;
        break;
    case PS_DO_HEAL:
        BtlFacade_AddHp(side, m, (f32)v);
        break;
    case PS_DO_HURT: /* (the game's own scripted damage: it never takes the last of the health) */
        BtlFacade_AddHp(side, m, -(f32)v);
        break;
    case PS_DO_SET_HP:
        if (v < 1) {
            v = 1;
        }
        if (PortStory_HealthPct(side) < v) {
            BtlFacade_RaiseHp(side, m, (f32)v);
        } else {
            BtlFacade_LowerHp(side, m, (f32)v);
        }
        break;
    case PS_DO_KI:
        BtlFacade_AddGaugeC(side, m, v);
        break;
    case PS_DO_BLAST:
        BtlFacade_AddGauge14(side, m, v);
        break;
    case PS_DO_MAX:
        BtlFacade_SetMaxPower(side, 1);
        break;
    case PS_DO_TRANSFORM:
        {
            s32 id = chr != NULL ? BtlParam_GetSlotId(chr, v > 0 && v <= 4 ? v - 1 : 0) : 0xFF;

            if (id < 0 || id >= 0xFF) { /* (it has no such form) */
                break;
            }
            if (!BtlFacade_CanCharAct(side) || !BtlCtrl_Transform(side, id)) {
                return 0;
            }
            sWatch.wait = 45;
        }
        break;
    case PS_DO_SWITCH:
        {
            s32 to = v > 0 ? v - 1 : PortStory_NextMember(side);

            if (to < 0 || to == m || to >= BtlCharApi_GetMemberCount(BattleSide_GetObjId(side)) ||
                BtlCharApi_GetMemberHpPercent(BattleSide_GetObjId(side), to) <= 0) {
                break; /* (nobody to bring in) */
            }
            if (!BtlFacade_CanCharAct(side) || !BtlCtrl_ChangeMember(side, to)) {
                return 0;
            }
            sWatch.wait = 45;
        }
        break;
    case PS_DO_MUSIC:
        PortStory_Music(v);
        break;
    case PS_DO_SOUND:
        if (v >= 0) {
            Snd_PlaySe(4, v);
        }
        break;
    case PS_DO_STAGE:
        BtlFacade_RequestStageChange();
        break;
    case PS_DO_SHAKE:
        BtlFacade_ShakeCamera((f32)(v > 0 ? v : 6) * 0.1f);
        break;
    case PS_DO_FLASH:
        PortStoryUi_Show(PS_SHOW_FLASH, 255, v ? 1 : 0);
        break;
    case PS_DO_AURA:
        if (v) {
            BtlFacade_SetCtrlFE(side);
        } else {
            BtlFacade_SetCtrlFF(side);
        }
        break;
    case PS_DO_WIN:
        PortStory_Decide(1, 0);
        break;
    case PS_DO_LOSE:
        PortStory_Decide(0, 0);
        break;
    case PS_DO_CPU:
        BtlFacade_SetCpuParam8(v);
        BtlCtrl_SetCpuLevel(1, v);
        break;
    case PS_DO_TIME:
        sWatch.bonus += v;
        break;
    case PS_DO_WAIT:
        sWatch.wait = v * 6;
        break;
    case PS_DO_USE_BLAST:
        if (!BtlFacade_CanCharAct(side) || !BtlCtrl_UseTechnique(side, v == 2 ? 4 : v == 1 ? 3 : 2)) {
            return 0;
        }
        sWatch.wait = 30;
        break;
    case PS_DO_CARD:
        sWatch.card = v;
        sWatch.cardFrames = 0;
        break;
    }
    return 1;
}

/* Whether an event's "when" is so this frame. */
static s32 PortStory_Happens(s32 *ev, s32 n, s32 sec, s32 left) {
    s32 side = 0;
    s32 v = ev[PE_VALUE];

    switch (ev[PE_WHEN]) {
    case PS_WHEN_START:
        return sWatch.frames > 30;
    case PS_WHEN_ENEMY_HP:
        return PortStory_HealthPct(1) <= v;
    case PS_WHEN_PLAYER_HP:
        return PortStory_HealthPct(0) <= v;
    case PS_WHEN_ENEMY_FORM:
        side = 1;
    case PS_WHEN_PLAYER_FORM:
        return BtlCharApi_GetChara(BattleSide_GetObjId(side)) != sWatch.chara[side] &&
               BtlFacade_GetActiveMember(side) == sWatch.member[side];
    case PS_WHEN_ENEMY_MEMBER:
        side = 1;
    case PS_WHEN_PLAYER_MEMBER:
        return BtlFacade_GetActiveMember(side) != sWatch.member[side];
    case PS_WHEN_TIME:
        return sec >= v;
    case PS_WHEN_ENEMY_KI:
        side = 1;
    case PS_WHEN_PLAYER_KI:
        {
            s32 *g = BtlChar_Get(side) != NULL ? BtlMember_GetActiveGauge(BtlChar_Get(side)) : NULL;
            s32 full = g != NULL && g[4] > 0 && g[3] >= g[4];

            return full && !sWatch.kiFull[side];
        }
    case PS_WHEN_ENEMY_MAX:
        side = 1;
    case PS_WHEN_PLAYER_MAX:
        return BtlSide_IsPoweredUp(side) && !sWatch.maxed[side];
    case PS_WHEN_ENEMY_KO:
        side = 1;
    case PS_WHEN_PLAYER_KO:
        return BtlSide_CountAlive(side) < sWatch.alive[side];
    case PS_WHEN_AFTER:
        return v >= 1 && v <= PS_EVENTS_MAX && v - 1 != n && sWatch.fired[v - 1] == 2;
    case PS_WHEN_TIME_LEFT:
        return left >= 0 && left <= v;
    case PS_WHEN_ENEMY_COMBO:
        side = 1;
    case PS_WHEN_PLAYER_COMBO:
        return BtlSide_GetComboHits(side) >= (v > 1 ? v : 2);
    case PS_WHEN_ENEMY_BLAST:
        side = 1;
    case PS_WHEN_PLAYER_BLAST:
        return BtlSide_GetBlast(side) >= v * 100000;
    }
    return 0;
}

/* The fight's first frame: what the rules give each side to start with. */
static void PortStory_StartRules(s32 *bt) {
    s32 s;

    for (s = 0; s < 2; s++) {
        s32 m = BtlFacade_GetActiveMember(s);

        if (bt[PB_KI + s] >= 0) {
            BtlFacade_LowerGaugeC(s, m, bt[PB_KI + s]);
            BtlFacade_RaiseGaugeC(s, m, bt[PB_KI + s]);
        }
        if (bt[PB_BLAST + s] >= 0) {
            BtlFacade_LowerGauge14(s, m, bt[PB_BLAST + s]);
            BtlFacade_RaiseGauge14(s, m, bt[PB_BLAST + s]);
        }
        if (bt[PB_MAX + s] > 0) {
            BtlFacade_SetMaxPower(s, 1);
        }
        sWatch.forms[s] = bt[PB_FORM + s] > 0 ? bt[PB_FORM + s] : 0;
    }
    sWatch.formWait = 20;
}

/* 9004 "watch": every frame of the battle until it is decided. */
static s32 PortStoryCmd_Watch(u32 phase, void *work) {
    s32 bt[PB_COUNT];
    s32 ev[PE_COUNT];
    s32 act[PA_COUNT];
    s32 i;
    s32 sec;
    s32 left;
    s32 limit;

    static s32 serial;

    switch (phase) {
    case GSC_PHASE_BEGIN:
        memset(&sWatch, 0, sizeof(sWatch));
        sWatch.hud = -1;
        sWatch.hudVoice = -1;
        sWatch.card = -1;
        sWatch.objective = -1;
        sWatch.cur = -1;
        sWatch.blastTick = -100000;
        *(s32 *)work = ++serial;
        return 0;
    case GSC_PHASE_BEGIN_ABORT:
        return 1;
    case GSC_PHASE_UPDATE:
        if (*(s32 *)work != serial) { /* (the battle was started again: a new referee has its place) */
            return 1;
        }
        break;
    default:
        return 0;
    }
    if (!PortStoryUi_Battle(bt)) {
        return 1;
    }
    sWatch.ticks++;
    if (BtlCharApi_IsInTechnique(BattleSide_GetObjId(0))) { /* (looked at whatever the battle's state: an ultimate's
                                                               cinematic is not the fight's state) */
        sWatch.blastTick = sWatch.ticks;
    }
    if (sWatch.hud >= 0) { /* the box of a line said in the fight: as long as its voice, at least four seconds */
        sWatch.hudFrames++;
        if (sWatch.hudFrames > 120 && (sWatch.hudVoice < 0 || Voice_IsStopped(sWatch.hudVoice))) {
            sWatch.hud = -1;
        }
        PortStoryUi_Show(PS_SHOW_HUD, sWatch.hud, sWatch.hudFrames);
    }
    if (sWatch.card >= 0) { /* a message: three and a half seconds */
        sWatch.cardFrames++;
        if (sWatch.cardFrames > 210) {
            sWatch.card = -1;
        }
        PortStoryUi_Show(PS_SHOW_CARD, sWatch.card, sWatch.cardFrames);
    }
    if (sWatch.objective >= 0) {
        sWatch.objective++;
        if (sWatch.objective > 200) {
            sWatch.objective = -1;
        }
        PortStoryUi_Show(PS_SHOW_OBJECTIVE, sWatch.objective, 0);
    }
    if (BtlSeq_GetState() != BTL_SEQ_FIGHT) {
        if (sWatch.started && BtlSeq_GetState() > BTL_SEQ_FIGHT) {
            PortStoryUi_Show(PS_SHOW_TIMER, -1, 0);
            PortStoryUi_Show(PS_SHOW_HUD, -1, 0);
            PortStoryUi_Show(PS_SHOW_CARD, -1, 0);
            PortStoryUi_Show(PS_SHOW_OBJECTIVE, -1, 0);
            return 1;
        }
        return 0;
    }
    if (sWatch.decided) { /* (waiting for the fight to let the scene start) */
        return 0;
    }
    if (sWatch.scene == 2) { /* a scene has stopped the fight: nothing is judged until it is over */
        if (BtlScript_IsEventRunning()) {
            return 0;
        }
        sWatch.off += BtlClock_ToSeconds(BtlSeq_GetClock()) - sWatch.sceneFrom;
        sWatch.scene = 0;
        PS_LOG("bt3: story: the fight goes on (second %d)\n", PortStory_Seconds());
        sWatch.wait = 30; /* (the fighters get their feet back before the next thing) */
    }
    sec = PortStory_Seconds();
    if (!sWatch.started) {
        sWatch.started = 1;
        for (i = 0; i < 2; i++) {
            sWatch.chara[i] = BtlCharApi_GetChara(BattleSide_GetObjId(i));
            sWatch.member[i] = BtlFacade_GetActiveMember(i);
            sWatch.alive[i] = BtlSide_CountAlive(i);
        }
        sWatch.alive0 = sWatch.alive[0];
        PortStory_StartRules(bt);
        if (bt[PB_OBJECTIVE]) {
            sWatch.objective = 0;
        }
    }
    sWatch.frames++;
    /* the forms the rules start a side in: one transformation after the other, as soon as the fighter can */
    if (sWatch.formWait > 0) {
        sWatch.formWait--;
    } else {
        for (i = 0; i < 2; i++) {
            void *chr = BtlChar_Get(i);
            s32 id = chr != NULL ? BtlParam_GetSlotId(chr, 0) : 0xFF;

            if (sWatch.forms[i] <= 0) {
                continue;
            }
            if (id < 0 || id >= 0xFF) {
                sWatch.forms[i] = 0;
            } else if (BtlFacade_CanCharAct(i) && BtlCtrl_Transform(i, id)) {
                sWatch.forms[i]--;
                sWatch.formWait = 90;
                break;
            }
        }
    }
    /* the clock */
    limit = bt[PB_TIME] > 0 ? bt[PB_TIME] + sWatch.bonus : 0;
    left = -1;
    if (bt[PB_WIN] == PS_WIN_SURVIVE && bt[PB_SURVIVE] > 0) {
        left = bt[PB_SURVIVE] + sWatch.bonus - sec;
    }
    if (limit > 0 && (left < 0 || limit - sec < left)) {
        left = limit - sec;
    }
    if (left < 0 && (bt[PB_TIME] > 0 || (bt[PB_WIN] == PS_WIN_SURVIVE && bt[PB_SURVIVE] > 0))) {
        left = 0;
    }
    /* the events: what happens this frame joins the line of those whose actions wait their turn */
    for (i = 0; i < bt[PB_EVENTS] && i < PS_EVENTS_MAX; i++) {
        if (sWatch.fired[i] == 1 || !PortStoryUi_Event(i, ev)) {
            continue;
        }
        if (sWatch.fired[i] == 2 && (ev[PE_REPEAT] <= 0 || sWatch.frames < sWatch.again[i])) {
            continue;
        }
        if (sWatch.queued < PS_QUEUE && PortStory_Happens(ev, i, sec, left)) {
            sWatch.fired[i] = 1;
            sWatch.queue[sWatch.queued++] = i;
            PS_LOG("bt3: story: event %d happens (second %d, health %d%% / %d%%)\n", i + 1, sec,
                    PortStory_HealthPct(0), PortStory_HealthPct(1));
        }
    }
    for (i = 0; i < 2; i++) { /* (a change of form or member counted from the fighter of now on) */
        s32 *g = BtlChar_Get(i) != NULL ? BtlMember_GetActiveGauge(BtlChar_Get(i)) : NULL;

        sWatch.chara[i] = BtlCharApi_GetChara(BattleSide_GetObjId(i));
        sWatch.member[i] = BtlFacade_GetActiveMember(i);
        sWatch.alive[i] = BtlSide_CountAlive(i);
        sWatch.maxed[i] = BtlSide_IsPoweredUp(i);
        sWatch.kiFull[i] = g != NULL && g[4] > 0 && g[3] >= g[4];
    }
    /* the end of the fight */
    PortStoryUi_Show(PS_SHOW_TIMER, left < 0 ? -1 : left, bt[PB_WIN] == PS_WIN_SURVIVE);
    if (BtlCtrl_IsTeamDead(0)) {
        PortStory_Decide(0, 0);
    } else if (bt[PB_FAIL_MEMBER] && sWatch.alive[0] < sWatch.alive0) {
        PortStory_Decide(0, 0);
    } else if (bt[PB_FAIL_HEALTH] > 0 && PortStory_HealthPct(0) <= bt[PB_FAIL_HEALTH]) {
        PortStory_Decide(0, 0);
    } else if (BtlCtrl_IsTeamDead(1)) { /* (PS_WIN_BLAST: the last blow has to be a blast, fired not long before) */
        PortStory_Decide(bt[PB_WIN] != PS_WIN_BLAST || sWatch.ticks - sWatch.blastTick < 240, 0);
    } else if (bt[PB_WIN] == PS_WIN_HEALTH && PortStory_HealthPct(1) <= bt[PB_WIN_VALUE]) {
        PortStory_Decide(1, 0);
    } else if (bt[PB_WIN] == PS_WIN_SURVIVE && bt[PB_SURVIVE] > 0 && sec >= bt[PB_SURVIVE] + sWatch.bonus) {
        PortStory_Decide(1, 1);
    } else if (limit > 0 && sec >= limit) {
        PortStory_Decide(bt[PB_TIME_UP] == 1 ? 1 : bt[PB_TIME_UP] == 2 ? PortStory_HealthPct(0) >= PortStory_HealthPct(1) : 0, 1);
    }
    if (sWatch.decided) {
        return 0;
    }
    /* a fighter knocked out with team mates still standing: the next one comes in once the fall is over. (In a story
       battle the game leaves that to the script: Dragon History's bring the next member in with a command. Without
       this a team's fight stood still at its first knock-out.) */
    for (i = 0; i < 2; i++) {
        if (!BtlCtrl_IsActiveDead(i) || BtlCtrl_IsTeamDead(i)) {
            sWatch.down[i] = 0;
            continue;
        }
        if (++sWatch.down[i] % 120 == 90) { /* (and again every two seconds, should the first not take) */
            s32 to = PortStory_NextMember(i);

            if (to >= 0 && BtlCtrl_ChangeMember(i, to)) {
                PS_LOG("bt3: story: side %d's member %d comes in\n", i, to + 1);
            }
        }
    }
    /* a scene asked for: started by the game's trigger list once both fighters can be interrupted; not over a
       fighter that is down (the team's next comes in first) */
    if (sWatch.scene == 1) {
        if (BtlScript_IsEventRunning()) {
            sWatch.scene = 2;
            sWatch.sceneFrom = BtlClock_ToSeconds(BtlSeq_GetClock());
            PS_LOG("bt3: story: a scene stops the fight (second %d)\n", PortStory_Seconds());
            sWatch.hud = -1;
            PortStoryUi_Show(PS_SHOW_HUD, -1, 0);
            PortStoryUi_Show(PS_SHOW_TIMER, -1, 0);
        } else if (BtlCtrl_IsActiveDead(0) || BtlCtrl_IsActiveDead(1)) {
            gBtlScript.pending = NULL;
        } else if (gBtlScript.pending == NULL) {
            gBtlScript.pending = PortStory_Request(sWatch.sceneAction);
            if (gBtlScript.pending == NULL) {
                sWatch.scene = 0;
            }
        }
        return 0;
    }
    /* the actions of the events that happened, in turn */
    if (sWatch.wait > 0) {
        sWatch.wait--;
        return 0;
    }
    for (;;) {
        if (sWatch.cur < 0) {
            if (sWatch.queued <= 0) {
                break;
            }
            sWatch.cur = sWatch.queue[0];
            sWatch.queued--;
            for (i = 0; i < sWatch.queued; i++) {
                sWatch.queue[i] = sWatch.queue[i + 1];
            }
            sWatch.act = 0;
            sWatch.tries = 0;
        }
        if (!PortStoryUi_Event(sWatch.cur, ev) || sWatch.act >= ev[PE_ACTIONS]) {
            sWatch.fired[sWatch.cur] = 2;
            sWatch.again[sWatch.cur] = sWatch.frames + ev[PE_REPEAT] * 60;
            sWatch.cur = -1;
            continue;
        }
        if (PortStoryUi_Action(ev[PE_FIRST] + sWatch.act, act)) {
            if (!PortStory_Do(act)) {
                if (++sWatch.tries < 600) { /* (ten seconds: then it is let go, the others are not held up for ever) */
                    break;
                }
                PS_LOG("bt3: story: event %d, action %d (kind %d) given up\n", sWatch.cur + 1, sWatch.act + 1, act[PA_KIND]);
            } else {
                PS_LOG("bt3: story: event %d, action %d (kind %d, value %d) done\n", sWatch.cur + 1, sWatch.act + 1,
                        act[PA_KIND], act[PA_VALUE]);
            }
        }
        sWatch.act++;
        sWatch.tries = 0;
        if (sWatch.wait > 0 || sWatch.scene || sWatch.decided) {
            break;
        }
    }
    return 0;
}

/* ---- Corrupt Patrol's places: a battle without a fight ----------------------------------------------------------- */

/*
 * 9007 "hub": count, then count pairs of (character, costume), then `arrive` (1: the player flies in and lands) and
 * `spread` (how far over the stage the people stand, percent of the small places' ring). The battle is a place to walk
 * in: the player's fighter (side 0) walks, dashes, jumps and flies, with no lock-on and no attack (gPortHubMode:
 * btl_char_flag.c, btl_act_h_b.c); the other side is a stand-in kept far away and out of sight; the HUD is off; the
 * battle never ends by itself. The people of the place stand about it, each a plain model in its standing pose, turning
 * to the player who comes near. Circle near one of them, START, or a place chosen on the world map (Triangle) ends
 * the battle: gPortHubExit says which of them (0..count-1), the menu (PORT_HUB_MENU) or the place
 * (PORT_HUB_TRAVEL + n, after the player has taken off); Corrupt Patrol's screens take it from there
 * (port/src/gs/patrol_ui.cpp, which also draws the map and works its cursor: gPortHubMapOpen, gPortHubMapKeys,
 * gPortHubTravel). gPortHubNear is the one within talking range (-1 none), for the prompt over the picture;
 * gPortHubPlaces where everyone is, for the list of the people there.
 */
#define HUB_NPC_MAX 8
#define PORT_HUB_MENU 99
#define PORT_HUB_TRAVEL 100
#define HUB_LAND_FRAMES 70     /* coming down, to the touch of the ground */
#define HUB_LAND_SETTLE 34     /* the landing pose, the camera going round behind */
#define HUB_TAKEOFF_CHARGE 30
#define HUB_TAKEOFF_FRAMES 96
#define HUB_FLY_HEIGHT 300.0f
/* the fighters' own motions (src/battle/btl_act_d.c, btl_act_c.c) */
#define HUB_MO_FASTUP_START 0x2E
#define HUB_MO_FASTUP 0x2F
#define HUB_MO_FASTDOWN 0x32
#define HUB_MO_FASTDOWN_STOP 0x33
#define HUB_MO_LAND 0x2D
#define HUB_MO_CHARGE_START 0x34
#define HUB_MO_CHARGE 0x35
#define HUB_UP (-1.0f)          /* (the game's y is down) */
/* the map's buttons, as gPortHubMapKeys collects them */
#define MAPKEY_LEFT 1
#define MAPKEY_RIGHT 2
#define MAPKEY_UP 4
#define MAPKEY_DOWN 8
#define MAPKEY_OK 0x10
#define MAPKEY_BACK 0x20
#define MAPKEY_L1 0x40
#define MAPKEY_R1 0x80
s32 gPortHubMode;               /* a hub battle is up: no lock-on, no attacks (read by the battle's code) */
volatile s32 gPortHubNear = -1;
volatile s32 gPortHubExit = -1;
volatile s32 gPortHubWantMenu;  /* START was pressed (btl_seq.c sets it instead of opening the pause menu) */
volatile s32 gPortHubFreeze;    /* the player's fighter takes no buttons (btl_input.c): the map, taking off, landing */
volatile s32 gPortHubMapOpen;   /* the world map is up */
volatile s32 gPortHubMapKeys;   /* its buttons since the screens last took them (MAPKEY_) */
volatile s32 gPortHubTravel = -1; /* the place chosen on the map, -1 none */
volatile s32 gPortHubFade;      /* 0..255: black over the picture */
volatile s32 gPortHubPhase;     /* 0 walking, 1 landing, 2 the map, 3 taking off */
volatile f32 gPortHubPlaces[2 + HUB_NPC_MAX * 2]; /* the player's x z, then each person's (patrol_ui.cpp) */
typedef struct HubNpc {
    s32 chara;
    s32 costume;
    s32 obj;                    /* battle object id, -1 none yet */
    s32 state;                  /* 0 to load, 1 loading, 2 placed, 3 failed */
    f32 base;                   /* the model's own scale */
    f32 yaw;
    Vec4 pos;
    struct { struct { void *buf; s32 size; s32 id; void *orig; } file[3]; u32 flags; s32 number; } slot; /* (a BtlResSlot: battle/btl_obj.h) */
} HubNpc;
static struct {
    s32 count;
    s32 loading;                /* the one being read, -1 none */
    s32 frames;
    s32 ending;
    s32 arrive;
    s32 spread;
    s32 phase;                  /* as gPortHubPhase */
    s32 phaseFrames;
    s32 travel;                 /* the place taken off for */
    f32 stickX;
    f32 stickY;
    f32 height;                 /* how tall the player's fighter stands */
    f32 face;                   /* which way they face, landing / taking off */
    Vec4 eye;                   /* the camera of the landing / the takeoff */
    Vec4 center;
    Vec4 ground;                /* where the player lands / took off from */
    f32 radius;
    Vec4 park;
    HubNpc npc[HUB_NPC_MAX];
} sHub;

extern s32 BtlObj_Create(s32 type, void *res, s32 active);
extern s32 BtlObj_Destroy(s32 id);
extern void BtlObjAnim_PlayModel(void *obj, s32 motion, s32 mode);
extern void BtlRes_LoadSingle(s32 fromHeap, void *slot, s32 model);
extern void BtlRes_FreeSingle(void *slot);
extern s32 File_UpdateRequests(void);
extern s32 ChrView_GetModelFile(s32 chara, s32 costume, s32 damaged);
extern void Hud_ShowAll(s32 on);
extern s32 File_GetSize(s32 id);
extern unsigned Port_GameBigAlloc(int size);
extern void BtlFacade_StopCharMotion(s32 side);
extern void BtlFacade_ClearFixedCamera(void);
extern void BtlAct_PlayLandFx(void *chr, s32 hard);

static void PortHub_Free(void) {
    s32 i;

    for (i = 0; i < sHub.count; i++) {
        HubNpc *n = &sHub.npc[i];

        if (n->obj >= 0) {
            BtlObj_Destroy(n->obj);
            n->obj = -1;
        }
        if (n->state == 1 || n->state == 2) {
            BtlRes_FreeSingle(&n->slot);
        }
        n->state = 3;
    }
}

/* Where the people stand: about the middle of the stage, the player's side of it left free on a small place; on a
   larger one (spread over 100) all round, on two rings, so that they are to be looked for. */
static void PortHub_Layout(void) {
    Vec4 s0;
    Vec4 s1;
    Vec4 r;
    f32 dx;
    f32 dz;
    f32 full;
    f32 a0;
    s32 i;

    BtlStage_GetStartPlace(0, &s0, &r, 0);
    BtlStage_GetStartPlace(1, &s1, &r, 0);
    dx = s0.x - s1.x;
    dz = s0.z - s1.z;
    full = sqrtf(dx * dx + dz * dz);
    if (full < 1.0f) {
        full = 1.0f;
        dx = 0.0f;
        dz = 1.0f;
    }
    sHub.center = V((s0.x + s1.x) * 0.5f, s0.y, (s0.z + s1.z) * 0.5f);
    sHub.ground = s0;
    sHub.radius = full * 0.32f;
    if (sHub.radius > 70.0f) {
        sHub.radius = 70.0f;
    }
    if (sHub.radius < 25.0f) {
        sHub.radius = 25.0f;
    }
    if (sHub.spread > 100) {
        sHub.radius *= (f32)sHub.spread / 100.0f;
    }
    a0 = atan2f(dx, dz); /* (the player's side of the middle) */
    for (i = 0; i < sHub.count; i++) {
        HubNpc *n = &sHub.npc[i];
        f32 a;
        f32 rad = sHub.radius;

        if (sHub.spread > 100) {
            /* all round, every other one further out */
            a = a0 + PS_PI * 0.25f + (2.0f * PS_PI) * (f32)i / (f32)(sHub.count > 0 ? sHub.count : 1);
            rad *= (i & 1) ? 1.0f : 0.55f;
        } else {
            /* spread over the far part of the circle, facing the middle */
            a = a0 + PS_PI * 0.30f + (PS_PI * 1.40f) * (sHub.count > 1 ? (f32)i / (f32)(sHub.count - 1) : 0.5f);
        }
        n->pos = V(sHub.center.x + sinf(a) * rad, s0.y, sHub.center.z + cosf(a) * rad);
        n->yaw = atan2f(s0.x - n->pos.x, s0.z - n->pos.z); /* (looking at where the player comes in) */
    }
    sHub.park = V(sHub.center.x + 4000.0f, s0.y - 3000.0f, sHub.center.z + 4000.0f);
}

/* An NPC's object where it stands, turned by its yaw, at its own size (the matrix is what it is drawn with). */
static void PortHub_PlaceNpc(HubNpc *n) {
    u8 *o = BtlObj_Get(n->obj);
    f32 *m;
    f32 c;
    f32 sn;

    if (o == NULL) {
        return;
    }
    m = (f32 *)(o + 0x9A0);
    c = cosf(n->yaw);
    sn = sinf(n->yaw);
    *(f32 *)(o + 0x950) = n->pos.x;
    *(f32 *)(o + 0x954) = n->pos.y;
    *(f32 *)(o + 0x958) = n->pos.z;
    *(f32 *)(o + 0x964) = n->yaw;
    m[0] = c * n->base;
    m[1] = 0.0f;
    m[2] = -sn * n->base;
    m[4] = 0.0f;
    m[5] = n->base;
    m[6] = 0.0f;
    m[8] = sn * n->base;
    m[9] = 0.0f;
    m[10] = c * n->base;
    m[12] = n->pos.x;
    m[13] = n->pos.y;
    m[14] = n->pos.z;
}

/* Reads the people's models one at a time and makes them objects. */
static void PortHub_Load(void) {
    s32 i;

    if (sHub.loading >= 0) {
        HubNpc *n = &sHub.npc[sHub.loading];

        if (File_UpdateRequests() == 0) {
            return;
        }
        sHub.loading = -1;
        if (n->slot.file[0].buf == NULL) {
            n->state = 3;
            return;
        }
        n->obj = BtlObj_Create(3, &n->slot, 1);
        if (n->obj < 0) {
            BtlRes_FreeSingle(&n->slot);
            n->state = 3;
            return;
        }
        {
            u8 *o = BtlObj_Get(n->obj);
            f32 *m = (f32 *)(o + 0x9A0);

            BtlObjAnim_PlayModel(o, 7, 2); /* (its standing pose, round and round) */
            *(u32 *)(o + 0xA40) &= ~0x02000000;
            n->base = sqrtf(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]);
            if (n->base < 0.01f) {
                n->base = 1.0f;
            }
        }
        n->state = 2;
        PortHub_PlaceNpc(n);
        return;
    }
    for (i = 0; i < sHub.count; i++) {
        HubNpc *n = &sHub.npc[i];

        if (n->state == 0) {
            s32 file = ChrView_GetModelFile(n->chara, n->costume, 0);

            if (file <= 0) {
                n->state = 3;
                continue;
            }
            memset(&n->slot, 0, sizeof(n->slot));
            /* (a buffer of its own, outside the game's heap: the battle has filled that with the fighters, and a
               warrior of the player's own is a large model; a model with no room is left out) */
            n->slot.file[0].size = File_GetSize(file);
            n->slot.file[0].buf = (void *)Port_GameBigAlloc(n->slot.file[0].size);
            if (n->slot.file[0].buf == NULL) {
                n->state = 3;
                continue;
            }
            BtlRes_LoadSingle(0, &n->slot, file);
            n->state = 1;
            sHub.loading = i;
            return;
        }
    }
}

static s32 PortHub_Leave(s32 code) {
    gPortHubExit = code;
    gPortHubNear = -1;
    gPortHubFreeze = 0;
    gPortHubMapOpen = 0;
    gPortHubPhase = 0;
    PortHub_Free();
    gPortHubMode = 0;
    BattleResult_Set(1, 0);
    PortStory_SeqTo(BTL_SEQ_END);
    return 1;
}

static void PortHub_Phase(s32 phase) {
    sHub.phase = phase;
    sHub.phaseFrames = 0;
    gPortHubPhase = phase;
    gPortHubFreeze = phase != 0;
    gPortHubMapOpen = phase == 2;
}

/* The map's buttons this frame (the d-pad, the left stick pushed over, Cross, Circle / Triangle, L1, R1). */
static s32 PortHub_MapKeys(void) {
    Pad *pad = &gPad[0];
    u32 p = pad->gamePressed;
    s32 k = 0;
    f32 x = pad->left[0];
    f32 y = pad->left[1];

    if (p & PADG_LEFT) {
        k |= MAPKEY_LEFT;
    }
    if (p & PADG_RIGHT) {
        k |= MAPKEY_RIGHT;
    }
    if (p & PADG_UP) {
        k |= MAPKEY_UP;
    }
    if (p & PADG_DOWN) {
        k |= MAPKEY_DOWN;
    }
    if (p & PADG_CROSS) {
        k |= MAPKEY_OK;
    }
    if (p & (PADG_CIRCLE | PADG_TRIANGLE)) {
        k |= MAPKEY_BACK;
    }
    if (p & PADG_L1) {
        k |= MAPKEY_L1;
    }
    if (p & PADG_R1) {
        k |= MAPKEY_R1;
    }
    /* the stick: once each time it is pushed over (and let back) */
    if (x < -0.6f && sHub.stickX >= -0.6f) {
        k |= MAPKEY_LEFT;
    }
    if (x > 0.6f && sHub.stickX <= 0.6f) {
        k |= MAPKEY_RIGHT;
    }
    if (y < -0.6f && sHub.stickY >= -0.6f) {
        k |= MAPKEY_UP;
    }
    if (y > 0.6f && sHub.stickY <= 0.6f) {
        k |= MAPKEY_DOWN;
    }
    sHub.stickX = x;
    sHub.stickY = y;
    return k;
}

/* The player's fighter in the air at `lift` over the ground (landing: coming down; taking off: going up). */
static void PortHub_Lift(f32 lift) {
    Vec4 p = V(sHub.ground.x, sHub.ground.y + HUB_UP * lift, sHub.ground.z);
    Vec4 rot = V(0.0f, sHub.face, 0.0f);

    BtlFacade_SetCharPos(0, &p);
    BtlFacade_SetCharRot(0, &rot);
}

/* The camera of the landing and the takeoff: on the ground in front of the player and a little to the side (in
   heights of the fighter), looking up at them wherever they are in the air. */
static void PortHub_CamSet(f32 dist, f32 side) {
    f32 h = sHub.height;
    f32 fx = sinf(sHub.face);
    f32 fz = cosf(sHub.face);

    sHub.eye = V(sHub.ground.x + fx * h * dist + fz * h * side, sHub.ground.y + HUB_UP * h * 0.35f,
                 sHub.ground.z + fz * h * dist - fx * h * side);
}

static void PortHub_CamLook(f32 lift) {
    Vec4 at = V(sHub.ground.x, sHub.ground.y + HUB_UP * (lift + sHub.height * 0.6f), sHub.ground.z);

    PortStory_Camera(&sHub.eye, &at, NULL, NULL, 0.0f);
}

/* Ready for the landing / the takeoff: how tall the player is, which way they face. */
static void PortHub_Prepare(s32 takeoff) {
    f32 dx = sHub.center.x - sHub.ground.x;
    f32 dz = sHub.center.z - sHub.ground.z;

    sHub.height = PortStory_Height(0);
    if (sHub.height < 8.0f || sHub.height > 60.0f) {
        sHub.height = 16.0f;
    }
    if (takeoff) {
        Vec4 rot;

        BtlCharApi_GetRot(BattleSide_GetObjId(0), &rot);
        sHub.face = rot.y;
    } else {
        sHub.face = dx * dx + dz * dz > 1.0f ? atan2f(dx, dz) : 0.0f; /* (landing facing the middle) */
    }
}

/* 9007 "hub": every frame of the place's battle. */
static s32 PortStoryCmd_Hub(u32 phase, void *work) {
    static s32 serial;
    s32 i;
    s32 near;
    f32 best;
    Vec4 me;

    switch (phase) {
    case GSC_PHASE_BEGIN:
    case GSC_PHASE_BEGIN_ABORT:
        memset(&sHub, 0, sizeof(sHub));
        sHub.count = Gsc_GetInt();
        if (sHub.count > HUB_NPC_MAX) {
            sHub.count = HUB_NPC_MAX;
        }
        for (i = 0; i < sHub.count; i++) {
            sHub.npc[i].chara = Gsc_GetInt();
            sHub.npc[i].costume = Gsc_GetInt();
            sHub.npc[i].obj = -1;
        }
        sHub.arrive = Gsc_GetInt();
        sHub.spread = Gsc_GetInt();
        sHub.loading = -1;
        gPortHubMode = 1;
        gPortHubExit = -1;
        gPortHubNear = -1;
        gPortHubWantMenu = 0;
        gPortHubTravel = -1;
        gPortHubMapKeys = 0;
        gPortHubFade = sHub.arrive ? 255 : 0;
        PortHub_Layout();
        PortHub_Phase(sHub.arrive ? 1 : 0);
        *(s32 *)work = ++serial;
        return phase == GSC_PHASE_BEGIN_ABORT;
    case GSC_PHASE_UPDATE:
        if (*(s32 *)work != serial) {
            return 1;
        }
        break;
    default:
        return 0;
    }
    if (sHub.ending) {
        return 1;
    }
    if (BtlSeq_GetState() > BTL_SEQ_FIGHT) {
        PortHub_Free();
        gPortHubMode = 0;
        gPortHubNear = -1;
        gPortHubFreeze = 0;
        gPortHubMapOpen = 0;
        gPortHubPhase = 0;
        return 1;
    }
    gPortHubMode = 1;
    Hud_ShowAll(0);
    sHub.frames++;
    sHub.phaseFrames++;
    /* the stand-in of the other side: far off and out of sight */
    BtlFacade_SetCtrl100(1);
    BtlFacade_SetCharPos(1, &sHub.park);
    PortHub_Load();
    if (BtlSeq_GetState() < BTL_SEQ_FIGHT) { /* (still coming up: the landing waits, high over the ground, in black) */
        if (sHub.phase == 1) {
            sHub.phaseFrames = 0;
            gPortHubFade = 255;
            PortHub_Prepare(0);
            PortHub_Lift(HUB_FLY_HEIGHT);
        }
        return 0;
    }

    switch (sHub.phase) {
    case 1: { /* landing: down out of the sky fast, aura blazing, braking hard near the ground, the landing pose, dust;
                 the camera on the ground watching them come, then round behind them, and they are let go */
        s32 f = sHub.phaseFrames;

        if (f == 1) {
            PortHub_Prepare(0);
            PortHub_CamSet(4.2f, 1.6f);
            BtlFacade_PlayCharMotion(0, HUB_MO_FASTDOWN, 1, 0.0f);
            BtlFacade_SetCtrlFE(0); /* (the aura) */
        }
        gPortHubFade = f < 16 ? 255 - f * 16 : 0;
        if (f <= HUB_LAND_FRAMES) {
            f32 t = (f32)f / (f32)HUB_LAND_FRAMES;
            f32 e = 1.0f - t;
            f32 lift = HUB_FLY_HEIGHT * e * e * e; /* (fast, then braking) */

            if (f == HUB_LAND_FRAMES - 16) {
                BtlFacade_PlayCharMotion(0, HUB_MO_FASTDOWN_STOP, 0, 0.15f);
            }
            if (f == HUB_LAND_FRAMES) {
                lift = 0.0f;
                BtlFacade_PlayCharMotion(0, HUB_MO_LAND, 0, 0.1f);
                BtlFacade_SetCtrlFF(0);
                BtlAct_PlayLandFx(BtlChar_Get(0), 1); /* (the dust and the thud) */
                BtlFacade_ShakeCamera(0.4f);
            }
            PortHub_Lift(lift);
            PortHub_CamLook(lift);
        } else if (f == HUB_LAND_FRAMES + 12) { /* round behind them, as the game's camera will be */
            Vec4 from = sHub.eye;
            Vec4 fromAt = V(sHub.ground.x, sHub.ground.y + HUB_UP * sHub.height * 0.6f, sHub.ground.z);
            Vec4 at;

            PortHub_CamSet(-4.6f, 0.0f);
            sHub.eye.y = sHub.ground.y + HUB_UP * sHub.height * 1.5f;
            at = V(sHub.ground.x + sinf(sHub.face) * sHub.height * 3.0f, sHub.ground.y + HUB_UP * sHub.height * 0.8f,
                   sHub.ground.z + cosf(sHub.face) * sHub.height * 3.0f);
            PortStory_Camera(&sHub.eye, &at, &from, &fromAt, (f32)(HUB_LAND_SETTLE - 12) / 30.0f);
        } else if (f >= HUB_LAND_FRAMES + HUB_LAND_SETTLE) {
            BtlFacade_StopCharMotion(0); /* (out of the scripted pose: the player's again) */
            /* The camera back to the game. The move round behind them is stopped first: it runs a few frames past
               the landing, and if left going it sets the fixed camera again and the camera stays put. */
            gBtlScript.cam.active = 0;
            gBtlScript.cam.time = 0;
            gBtlScript.cam.index = 0;
            BtlFacade_ClearFixedCamera();
            gPortHubFade = 0;
            PortHub_Phase(0);
        }
        break;
    }
    case 2: /* the map: the screens draw it and move its cursor; Cross there sets gPortHubTravel */
        gPortHubMapKeys |= PortHub_MapKeys();
        if (gPortHubTravel >= 0) {
            sHub.travel = gPortHubTravel;
            gPortHubTravel = -1;
            BtlCharApi_GetPos(BattleSide_GetObjId(0), &me);
            sHub.ground = me;
            PortHub_Phase(3);
        } else if (!gPortHubMapOpen) {
            PortHub_Phase(0); /* (closed from the screens: Circle / Triangle) */
        }
        break;
    case 3: { /* taking off: powering up where they stand, then a burst up into the sky, faster and faster, into black */
        s32 f = sHub.phaseFrames;
        f32 lift = 0.0f;

        if (f == 1) {
            PortHub_Prepare(1);
            PortHub_CamSet(3.6f, 1.4f);
            BtlFacade_PlayCharMotion(0, HUB_MO_CHARGE_START, 0, 0.15f);
            BtlFacade_SetCtrl101(0); /* (the charge) */
            BtlFacade_SetCtrlFE(0);
        }
        if (f == 8) {
            BtlFacade_PlayCharMotion(0, HUB_MO_CHARGE, 1, 0.15f);
        }
        if (f == HUB_TAKEOFF_CHARGE) {
            BtlFacade_ClearCtrl101(0);
            BtlFacade_SetCtrl102(0); /* (a burst of ki) */
            BtlFacade_PlayCharMotion(0, HUB_MO_FASTUP_START, 0, 0.1f);
            BtlFacade_ShakeCamera(0.6f);
        }
        if (f == HUB_TAKEOFF_CHARGE + 10) {
            BtlFacade_PlayCharMotion(0, HUB_MO_FASTUP, 1, 0.15f);
        }
        if (f > HUB_TAKEOFF_CHARGE) {
            f32 t = (f32)(f - HUB_TAKEOFF_CHARGE) / (f32)(HUB_TAKEOFF_FRAMES - HUB_TAKEOFF_CHARGE);

            lift = HUB_FLY_HEIGHT * 1.8f * t * t;
        }
        PortHub_Lift(lift);
        PortHub_CamLook(lift);
        gPortHubFade = f > HUB_TAKEOFF_FRAMES - 26 ? (f - (HUB_TAKEOFF_FRAMES - 26)) * 10 : 0;
        if (gPortHubFade > 255) {
            gPortHubFade = 255;
        }
        if (f >= HUB_TAKEOFF_FRAMES) {
            sHub.ending = 1;
            gPortHubFade = 255;
            BtlFacade_SetCtrlFF(0);
            BtlFacade_SetCtrl100(0); /* (gone into the sky: hidden, the camera kept on the sky, until the place closes) */
            return PortHub_Leave(PORT_HUB_TRAVEL + sHub.travel);
        }
        break;
    }
    default:
        break;
    }

    BtlCharApi_GetPos(BattleSide_GetObjId(0), &me);
    gPortHubPlaces[0] = me.x;
    gPortHubPlaces[1] = me.z;
    if ((sHub.frames % 30) == 0 && getenv("BT3_HUB_LOG") != NULL) { /* (testing: where the player is) */
        fprintf(stderr, "bt3: hub: frame %d phase %d at %.1f %.1f %.1f\n", sHub.frames, sHub.phase, me.x, me.y, me.z);
    }
    near = -1;
    best = 30.0f;
    for (i = 0; i < sHub.count; i++) {
        HubNpc *n = &sHub.npc[i];
        f32 dx;
        f32 dz;
        f32 d;

        gPortHubPlaces[2 + i * 2] = n->pos.x;
        gPortHubPlaces[3 + i * 2] = n->state == 2 ? n->pos.z : 1.0e9f;
        if (n->state != 2) {
            continue;
        }
        dx = me.x - n->pos.x;
        dz = me.z - n->pos.z;
        d = sqrtf(dx * dx + dz * dz);
        if (d < best && __builtin_fabsf(me.y - n->pos.y) < 40.0f) {
            best = d;
            near = i;
        }
        if (d < 45.0f) { /* (turning to the player who comes near) */
            f32 want = atan2f(dx, dz);
            f32 diff = want - n->yaw;

            while (diff > PS_PI) {
                diff -= 2.0f * PS_PI;
            }
            while (diff < -PS_PI) {
                diff += 2.0f * PS_PI;
            }
            n->yaw += diff * 0.12f;
        }
        if (sHub.phase == 0 && d < 9.0f && d > 0.01f && __builtin_fabsf(me.y - n->pos.y) < 20.0f) { /* (not walked through) */
            Vec4 out = V(n->pos.x + dx / d * 9.0f, me.y, n->pos.z + dz / d * 9.0f);

            BtlFacade_SetCharPos(0, &out);
        }
        PortHub_PlaceNpc(n);
    }
    gPortHubNear = sHub.phase == 0 ? near : -1;
    if (sHub.phase != 0) {
        gPortHubWantMenu = 0;
        return 0;
    }
    if (sHub.frames > 30 && near >= 0 && (gPad[0].gamePressed & PADG_CIRCLE)) {
        sHub.ending = 1;
        return PortHub_Leave(near);
    }
    if (sHub.frames > 30 && (gPad[0].gamePressed & PADG_TRIANGLE)) {
        gPortHubMapKeys = 0;
        PortHub_Phase(2);
        PortHub_MapKeys(); /* (the stick as it is now: not a push) */
        return 0;
    }
    if (gPortHubWantMenu && sHub.frames < 60) {
        gPortHubWantMenu = 0; /* (a START from before the place was there: not for its menu) */
    }
    if (gPortHubWantMenu) {
        gPortHubWantMenu = 0;
        sHub.ending = 1;
        return PortHub_Leave(PORT_HUB_MENU);
    }
    return 0;
}

/* ---- the command table -------------------------------------------------------------------------------------- */

/* The game's table with the Story Creator's commands after it (Battle_Load hands it to the GSC engine). */
s32 *PortStory_Commands(s32 *game) {
    static GscCommand table[80];
    static s32 made;
    GscCommand *g = (GscCommand *)game;
    s32 n = 0;

    if (!made) {
        while (g[n].handler != NULL && n < 64) {
            table[n] = g[n];
            n++;
        }
        table[n].id = 9001;
        table[n++].handler = (GscHandler)PortStoryCmd_Scene;
        table[n].id = 9002;
        table[n++].handler = (GscHandler)PortStoryCmd_Line;
        table[n].id = 9003;
        table[n++].handler = (GscHandler)PortStoryCmd_EndNow;
        table[n].id = 9004;
        table[n++].handler = (GscHandler)PortStoryCmd_Watch;
        table[n].id = 9005;
        table[n++].handler = (GscHandler)PortStoryCmd_Title;
        table[n].id = 9006;
        table[n++].handler = (GscHandler)PortStoryCmd_SceneEnd;
        table[n].id = 9007;
        table[n++].handler = (GscHandler)PortStoryCmd_Hub;
        table[n].id = 0;
        table[n].handler = NULL;
        made = 1;
    }
    return (s32 *)table;
}
#endif

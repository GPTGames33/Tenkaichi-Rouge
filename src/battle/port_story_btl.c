#include "common.h"
#ifdef PORT
#include "sys/gsc.h"
#include "sys/pad.h"
#include "sys/save.h"
#include "battle/btl_script.h"
#include "battle/btl_seq.h"

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
 * lets the fight start. The scenes after the fight are event actions (10030 lost, 10040 won), started through the
 * game's own trigger list once the fight allows it (BtlScript_StartPending), and played during the winner state (5).
 *
 * The commands of the game do the plumbing (BeginScene, EndScene, SetResult, SetTriggers, Battle); the Story Creator's
 * own (ids 9001..9009, added to the game's table for every battle: PortStory_Commands) do what a saga says, worked out
 * on the stage the episode is on:
 *   9001 scene      distance flags        the two fighters face each other `distance` apart in the middle of the
 *                                          stage's start places, standing; flags: 1 / 2 hide side 0 / 1
 *   9002 line       n                     line n of the episode: camera shot (a cut or a move), poses, effects, the
 *                                          voice (lip-synced for a fighter's own line), the text box (typed out;
 *                                          Cross completes it, then goes on), or its own time
 *   9003 end now    result                ends a battle that has no fight (a scene-only episode): result 1 won
 *   9004 watch      -                     the fight's referee (its own task, action 2000): the episode's win / lose
 *                                          conditions, the clock, and the lines said during the fight (a box with
 *                                          the speaker's portrait, the fight goes on)
 *   9005 title      -                     the episode's title card
 *   9006 scene end  -                     the letterbox goes, both fighters shown again
 * The text, the portraits and the letterbox are drawn by story_ui.cpp over the picture; what to draw is told to it
 * here (PortStoryUi_Show). Nothing of it runs outside a Story Creator battle: the commands are only in its scripts.
 */

#define PS_SCRIPT 900 /* the script number a saga's battle is set up with (menu/port_story.c) */

/* what PortStoryUi_Line gives of a line */
#define PL_SIDE 0      /* the speaker's side, -1 a narrator */
#define PL_MEMBER 1    /* its team member, -1 the one fighting */
#define PL_CHARA 2
#define PL_VOICE 3     /* the voice: a spoken line of character PL_VCHARA (Voice_PlayChara), -1 none */
#define PL_LIP 4       /* lip track of the speaker's own spoken line, -1 none */
#define PL_CAMERA 5    /* PS_CAM_ */
#define PL_MOVE 6      /* PS_MOVE_ */
#define PL_MOVE_T 7    /* the move's time, tenths of a second */
#define PL_POSE 8      /* the speaker's motion, -1 as it is */
#define PL_POSE_LOOP 9
#define PL_LPOSE 10    /* the listener's */
#define PL_LPOSE_LOOP 11
#define PL_FX 12       /* PS_FX_ */
#define PL_LEN 13      /* the text's length in letters */
#define PL_WAIT 14     /* 0: until Cross; else frames after the text is out */
#define PL_VCHARA 15
#define PL_COUNT 16

enum { PS_CAM_KEEP, PS_CAM_AUTO, PS_CAM_CLOSE, PS_CAM_MEDIUM, PS_CAM_SHOULDER, PS_CAM_TWO, PS_CAM_WIDE, PS_CAM_LOW,
       PS_CAM_HIGH, PS_CAM_LISTENER, PS_CAM_COUNT };
enum { PS_MOVE_CUT, PS_MOVE_PUSH, PS_MOVE_ORBIT, PS_MOVE_PULL, PS_MOVE_FROM_WIDE, PS_MOVE_RISE };
#define PS_FX_SHAKE 0x1
#define PS_FX_FLASH 0x2
#define PS_FX_AURA_ON 0x4
#define PS_FX_AURA_OFF 0x8
#define PS_FX_CHARGE_ON 0x10
#define PS_FX_CHARGE_OFF 0x20
#define PS_FX_BURST 0x40
#define PS_FX_FADE_OUT 0x80
#define PS_FX_FADE_IN 0x100
#define PS_FX_BIG_SHAKE 0x200
#define PS_FX_HIDE 0x400
#define PS_FX_SHOW 0x800

/* what PortStoryUi_Show is told */
enum { PS_SHOW_SAY = 1, PS_SHOW_HIDE, PS_SHOW_TITLE, PS_SHOW_HUD, PS_SHOW_LETTERBOX, PS_SHOW_FLASH, PS_SHOW_FADE,
       PS_SHOW_TIMER, PS_SHOW_END };

/* the battle's conditions (PortStoryUi_Battle) */
#define PB_WIN 0       /* 0 knock the other team out, 1 hold out for PB_SURVIVE seconds */
#define PB_SURVIVE 1
#define PB_TIME 2      /* the time limit, seconds (0 none) */
#define PB_TIME_UP 3   /* at the time limit: 0 lost, 1 won, 2 by health */
#define PB_EVENTS 4    /* how many lines are said during the fight */
#define PB_COUNT 8
/* a line said during the fight (PortStoryUi_Event) */
#define PE_WHEN 0      /* PS_WHEN_ */
#define PE_VALUE 1
#define PE_LINE 2
#define PE_COUNT 4
enum { PS_WHEN_START, PS_WHEN_ENEMY_HP, PS_WHEN_PLAYER_HP, PS_WHEN_ENEMY_FORM, PS_WHEN_PLAYER_FORM, PS_WHEN_TIME,
       PS_WHEN_ENEMY_MEMBER, PS_WHEN_PLAYER_MEMBER };

extern s32 PortStoryUi_ScriptSize(void);
extern s32 PortStoryUi_AutoWait(void);
extern void PortStoryUi_ScriptCopy(void *dst);
extern s32 PortStoryUi_Line(s32 n, s32 *out);
extern s32 PortStoryUi_Battle(s32 *out);
extern s32 PortStoryUi_Event(s32 n, s32 *out);
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
extern s32 BattleSide_GetObjId(s32 side);
extern void *BtlObj_Get(s32 id);
extern void BtlObj_SetSubState(void *obj, s32 mode, s32 arg);
extern void BtlCharApi_GetPos(s32 objId, Vec4 *out);
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

/* Puts the two fighters `how` far apart (0 where the stage starts them, 1 close, 2 at talking distance, 3 far; in
   heights of the taller one), in the middle between the stage's start places, facing each other, standing. */
static void PortStory_Place(s32 how, s32 flags) {
    static const f32 kApart[4] = { 0.0f, 2.6f, 4.2f, 7.5f };
    Vec4 s0;
    Vec4 s1;
    Vec4 r;
    f32 dx;
    f32 dz;
    f32 full;
    f32 dist;
    f32 h0 = PortStory_Height(0);
    f32 h1 = PortStory_Height(1);
    f32 t;
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
    dist = (how > 0 && how < 4 ? kApart[how] : 0.0f) * (h0 > h1 ? h0 : h1);
    if (dist <= 0.0f || dist > full) {
        dist = full;
    }
    t = 0.5f * dist / full;
    for (i = 0; i < 2; i++) {
        f32 k = i == 0 ? 0.5f - t : 0.5f + t;
        Vec4 rot;

        sScene.pos[i] = V(s0.x + (s1.x - s0.x) * k, s0.y + (s1.y - s0.y) * k, s0.z + (s1.z - s0.z) * k);
        sScene.yaw[i] = atan2f(i == 0 ? dx : -dx, i == 0 ? dz : -dz);
        rot = V(0.0f, sScene.yaw[i], 0.0f);
        BtlFacade_SetCharPos(i, &sScene.pos[i]);
        BtlFacade_SetCharRot(i, &rot);
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
        *eye = V(M.x + px * (d * 0.95f + hm * 1.2f), M.y - hm * 0.62f, M.z + pz * (d * 0.95f + hm * 1.2f));
        *at = V(M.x, M.y - hm * 0.55f, M.z);
        break;
    case PS_CAM_WIDE: /* the stage with both in it, from high up and far */
        *eye = V(M.x + px * (d * 1.6f + hm * 3.5f) - dx * d * 0.3f, M.y - hm * 2.6f, M.z + pz * (d * 1.6f + hm * 3.5f) - dz * d * 0.3f);
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
        e0 = V(eye.x + (eye.x - at.x) * 0.6f, eye.y + (eye.y - at.y) * 0.6f, eye.z + (eye.z - at.z) * 0.6f);
        PortStory_Camera(&eye, &at, &e0, &at, t);
        break;
    case PS_MOVE_PULL: /* from close, moving back */
        e0 = V(at.x + (eye.x - at.x) * 0.55f, at.y + (eye.y - at.y) * 0.55f, at.z + (eye.z - at.z) * 0.55f);
        PortStory_Camera(&eye, &at, &e0, &at, t);
        break;
    case PS_MOVE_ORBIT: /* round the one looked at, a quarter of the way */
        {
            f32 rx = eye.x - at.x;
            f32 rz = eye.z - at.z;
            f32 c = cosf(0.6f);
            f32 sn = sinf(0.6f);

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
    default:
        PortStory_Camera(&eye, &at, NULL, NULL, 0.0f);
        break;
    }
}

/* A line's effects, at its start. */
static void PortStory_Effects(s32 *li) {
    s32 fx = li[PL_FX];
    s32 s = li[PL_SIDE] < 0 ? 0 : li[PL_SIDE];

    if (fx & PS_FX_SHAKE) {
        BtlFacade_ShakeCamera(0.6f);
    }
    if (fx & PS_FX_BIG_SHAKE) {
        BtlFacade_ShakeCamera(2.0f);
    }
    if (fx & PS_FX_AURA_ON) {
        BtlFacade_SetCtrlFE(s);
    }
    if (fx & PS_FX_AURA_OFF) {
        BtlFacade_SetCtrlFF(s);
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
    if (fx & PS_FX_HIDE) {
        BtlFacade_SetCtrl100(s);
        sScene.hidden[s] = 1;
    }
    if (fx & PS_FX_SHOW) {
        BtlFacade_ClearCtrl100(s);
        sScene.hidden[s] = 0;
    }
    if (fx & PS_FX_FLASH) {
        PortStoryUi_Show(PS_SHOW_FLASH, 255, 0);
    }
    if (fx & PS_FX_FADE_IN) {
        PortStoryUi_Show(PS_SHOW_FADE, 255, -12);
    }
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
        PortStoryUi_Show(PS_SHOW_LETTERBOX, 0, 0);
    }
    return 1;
}

/* Task work of 9002. */
typedef struct PsLineWork {
    s16 line;
    s16 frames;
    s16 shown;     /* letters typed out, in sixteenths */
    s16 full;      /* frames since the text was all out */
    s8 voice;      /* the voice's channel, -1 none */
    s8 mouth;      /* 1 talking (a loop), 2 lip-synced, 3 the lip's start waited for */
    s8 obj;        /* the speaker is on the stage (its side) + 1 */
    s8 pressed;
    s16 len;
    s16 wait;
    s32 lip;
} PsLineWork;

static void PortStory_LineEnd(PsLineWork *w) {
    if (w->voice >= 0 && !Voice_IsStopped(w->voice)) {
        Voice_Stop(w->voice);
    }
    if (w->obj > 0 && w->mouth != 0) {
        BtlObj_SetSubState(BtlObj_Get(BattleSide_GetObjId(w->obj - 1)), 0, -1);
    }
    w->mouth = 0;
    PortStoryUi_Show(PS_SHOW_HIDE, 0, 0);
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
        for (i = 0; i < 2; i++) { /* (back on their marks: a pose before may have carried a fighter off them) */
            Vec4 rot = V(0.0f, sScene.yaw[i], 0.0f);

            BtlFacade_SetCharPos(i, &sScene.pos[i]);
            BtlFacade_SetCharRot(i, &rot);
        }
        PortStory_LineCamera(li);
        if (li[PL_SIDE] >= 0 && li[PL_POSE] >= 0) {
            BtlFacade_PlayCharMotion(li[PL_SIDE], li[PL_POSE], li[PL_POSE_LOOP] != 0, 0.0f);
        }
        if (li[PL_LPOSE] >= 0) {
            BtlFacade_PlayCharMotion(li[PL_SIDE] == 0 ? 1 : 0, li[PL_LPOSE], li[PL_LPOSE_LOOP] != 0, 0.0f);
        }
        PortStory_Effects(li);
        w->len = (s16)li[PL_LEN];
        w->wait = (s16)li[PL_WAIT];
        if (w->wait <= 0 && PortStoryUi_AutoWait() > 0) {
            w->wait = (s16)PortStoryUi_AutoWait();
        }
        w->lip = li[PL_LIP];
        if (li[PL_SIDE] >= 0 && !sScene.hidden[li[PL_SIDE]] &&
            (li[PL_MEMBER] < 0 || li[PL_MEMBER] == BtlFacade_GetActiveMember(li[PL_SIDE]))) {
            w->obj = (s8)(li[PL_SIDE] + 1);
        }
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
        sScene.lines++;
        PortStoryUi_Show(PS_SHOW_SAY, w->line, 0);
        break;
    case GSC_PHASE_UPDATE:
        w->frames++;
        if (w->mouth == 3 && Adx_GetStat(w->voice + 4) == 3) { /* (the voice has started: its lips with it) */
            BtlObj_SetSubState(BtlObj_Get(BattleSide_GetObjId(w->obj - 1)), 2, w->lip);
            w->mouth = 2;
        }
        if (w->frames < 6) { /* (a press that ended the line before is not this one's) */
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
        if (w->full > 2 && (gPad[0].gamePressed & PADG_CROSS)) {
            ret = 1;
        } else if (w->wait > 0 && w->full >= w->wait && (w->voice < 0 || Voice_IsStopped(w->voice))) {
            ret = 1;
        } else if (w->len == 0 && w->voice >= 0 && Voice_IsStopped(w->voice) && w->full > 20) {
            ret = 1; /* (a voice without text: when it has been said) */
        }
        break;
    case GSC_PHASE_END:
        PortStory_LineEnd(w);
        break;
    case GSC_PHASE_BEGIN_ABORT:
        if (Gsc_IsCurrentBegun()) {
            PortStory_LineEnd(w);
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

typedef struct PsWatch {
    s32 started;
    s32 decided;
    s32 frames;
    s32 chara[2];   /* who was fighting on each side (a change: a transformation) */
    s32 member[2];
    s32 said[32];   /* which lines of the fight were said */
    s32 hud;        /* the line in the box, -1 none */
    s32 hudFrames;
    s32 hudVoice;
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

/* Starts the scene after the fight (event action 10040 won, 10030 lost) once the fight lets it: as the game's trigger
   list would (its request was put there by the script's command 7, with a trigger nothing raises). */
static void PortStory_Decide(s32 won) {
    BtlScriptTalk *talk = &gBtlScript.talk;
    s32 want = won ? 10040 : 10030;
    s32 i;

    for (i = 0; i < talk->requestCount; i++) {
        if (talk->requests[i].kind == BTL_SCRIPT_REQ_EVENT && talk->requests[i].u.start.action == want) {
            gBtlScript.pending = &talk->requests[i];
            break;
        }
    }
    sWatch.decided = won ? 1 : 2;
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
}

/* 9004 "watch": every frame of the battle until it is decided. */
static s32 PortStoryCmd_Watch(u32 phase, void *work) {
    s32 bt[PB_COUNT];
    s32 ev[PE_COUNT];
    s32 i;
    s32 sec;
    s32 left;

    static s32 serial;

    switch (phase) {
    case GSC_PHASE_BEGIN:
        memset(&sWatch, 0, sizeof(sWatch));
        sWatch.hud = -1;
        sWatch.hudVoice = -1;
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
    if (sWatch.hud >= 0) { /* the box of a line said in the fight: as long as its voice, at least four seconds */
        sWatch.hudFrames++;
        if (sWatch.hudFrames > 120 && (sWatch.hudVoice < 0 || Voice_IsStopped(sWatch.hudVoice))) {
            sWatch.hud = -1;
        }
        PortStoryUi_Show(PS_SHOW_HUD, sWatch.hud, sWatch.hudFrames);
    }
    if (BtlSeq_GetState() != BTL_SEQ_FIGHT) {
        if (sWatch.started && BtlSeq_GetState() > BTL_SEQ_FIGHT) {
            PortStoryUi_Show(PS_SHOW_TIMER, -1, 0);
            PortStoryUi_Show(PS_SHOW_HUD, -1, 0);
            return 1;
        }
        return 0;
    }
    if (sWatch.decided) { /* (waiting for the fight to let the scene start) */
        return 0;
    }
    sec = BtlClock_ToSeconds(BtlSeq_GetClock());
    if (!sWatch.started) {
        sWatch.started = 1;
        for (i = 0; i < 2; i++) {
            sWatch.chara[i] = BtlCharApi_GetChara(BattleSide_GetObjId(i));
            sWatch.member[i] = BtlFacade_GetActiveMember(i);
        }
    }
    sWatch.frames++;
    /* the lines said during the fight */
    for (i = 0; i < bt[PB_EVENTS] && i < 32; i++) {
        s32 hit = 0;

        if (sWatch.said[i] || !PortStoryUi_Event(i, ev)) {
            continue;
        }
        switch (ev[PE_WHEN]) {
        case PS_WHEN_START:
            hit = sWatch.frames > 30;
            break;
        case PS_WHEN_ENEMY_HP:
            hit = PortStory_HealthPct(1) <= ev[PE_VALUE];
            break;
        case PS_WHEN_PLAYER_HP:
            hit = PortStory_HealthPct(0) <= ev[PE_VALUE];
            break;
        case PS_WHEN_ENEMY_FORM:
        case PS_WHEN_PLAYER_FORM:
            {
                s32 side = ev[PE_WHEN] == PS_WHEN_ENEMY_FORM ? 1 : 0;

                hit = BtlCharApi_GetChara(BattleSide_GetObjId(side)) != sWatch.chara[side] &&
                      BtlFacade_GetActiveMember(side) == sWatch.member[side];
            }
            break;
        case PS_WHEN_ENEMY_MEMBER:
        case PS_WHEN_PLAYER_MEMBER:
            {
                s32 side = ev[PE_WHEN] == PS_WHEN_ENEMY_MEMBER ? 1 : 0;

                hit = BtlFacade_GetActiveMember(side) != sWatch.member[side];
            }
            break;
        case PS_WHEN_TIME:
            hit = sec >= ev[PE_VALUE];
            break;
        }
        if (hit && (sWatch.hud < 0 || sWatch.hudFrames > 45)) {
            sWatch.said[i] = 1;
            PortStory_HudLine(ev[PE_LINE]);
            break;
        }
    }
    for (i = 0; i < 2; i++) { /* (a change of form or member counted from the fighter of now on) */
        sWatch.chara[i] = BtlCharApi_GetChara(BattleSide_GetObjId(i));
        sWatch.member[i] = BtlFacade_GetActiveMember(i);
    }
    /* the end of the fight */
    left = -1;
    if (bt[PB_WIN] == 1 && bt[PB_SURVIVE] > 0) {
        left = bt[PB_SURVIVE] - sec;
    }
    if (bt[PB_TIME] > 0 && (left < 0 || bt[PB_TIME] - sec < left)) {
        left = bt[PB_TIME] - sec;
    }
    PortStoryUi_Show(PS_SHOW_TIMER, left < 0 ? -1 : left, bt[PB_WIN] == 1);
    if (BtlCtrl_IsTeamDead(0)) {
        PortStory_Decide(0);
    } else if (BtlCtrl_IsTeamDead(1)) {
        PortStory_Decide(1);
    } else if (bt[PB_WIN] == 1 && bt[PB_SURVIVE] > 0 && sec >= bt[PB_SURVIVE]) {
        PortStory_Decide(1);
    } else if (bt[PB_TIME] > 0 && sec >= bt[PB_TIME]) {
        PortStory_Decide(bt[PB_TIME_UP] == 1 ? 1 : bt[PB_TIME_UP] == 2 ? PortStory_HealthPct(0) >= PortStory_HealthPct(1) : 0);
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
        table[n].id = 0;
        table[n].handler = NULL;
        made = 1;
    }
    return (s32 *)table;
}
#endif

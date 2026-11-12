#ifndef PORT_STORY_H
#define PORT_STORY_H

/*
 * PC only: what the two halves of the Story Creator tell each other. The editor and the saga files are
 * port/src/gs/story_ui.cpp (the render thread); the battle half is src/battle/port_story_btl.c (the game's thread).
 * An episode is worked out once into flat tables of numbers (a line, the fight's rules, an event, an action) that the
 * battle half asks for by index; this header says what each number is. Plain C: both a C and a C++ file read it.
 */

/* ---- a line (PortStoryUi_Line) -------------------------------------------------------------------------------- */
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
#define PL_SOUND 16    /* a sound effect of the battle's bank at the line's start, -1 none */
#define PL_MUSIC 17    /* -1 the music stays, -2 it stops, else the song that starts */
#define PL_CARD 18     /* 0 the text box, 1 a narration card (the text alone, centred), 2 a title card (big) */
#define PL_TRANS 19    /* PS_TRANS_: how the picture comes in or goes out with the line */
#define PL_DIST 20     /* -1 they stay; 1 close, 2 at talking distance, 3 far: the two are put again */
#define PL_STAND 21    /* the speaker: -1 as it is, 0 on the ground, 1 in the air, 2 high up */
#define PL_LSTAND 22   /* the listener */
#define PL_FACE 23     /* the speaker: -1 as it is, 0 facing the other, 1 back turned, 2 side on */
#define PL_LFACE 24    /* the listener */
#define PL_DELAY 25    /* frames of silence before the line is said */
#define PL_CHOICE 26   /* how many answers the player picks from (0: not a question) */
#define PL_COUNT 32

enum { PS_CAM_KEEP, PS_CAM_AUTO, PS_CAM_CLOSE, PS_CAM_MEDIUM, PS_CAM_SHOULDER, PS_CAM_TWO, PS_CAM_WIDE, PS_CAM_LOW,
       PS_CAM_HIGH, PS_CAM_LISTENER, PS_CAM_FACE, PS_CAM_BEHIND, PS_CAM_PROFILE, PS_CAM_TOP, PS_CAM_GROUND,
       PS_CAM_FAR, PS_CAM_FULL, PS_CAM_COUNT };
enum { PS_MOVE_CUT, PS_MOVE_PUSH, PS_MOVE_ORBIT, PS_MOVE_PULL, PS_MOVE_FROM_WIDE, PS_MOVE_RISE, PS_MOVE_DROP,
       PS_MOVE_SLIDE, PS_MOVE_ORBIT_BACK, PS_MOVE_CRASH, PS_MOVE_PAN, PS_MOVE_COUNT };
enum { PS_TRANS_NONE, PS_TRANS_FADE_IN, PS_TRANS_FADE_OUT, PS_TRANS_FLASH, PS_TRANS_DIP, PS_TRANS_COUNT };

#define PS_FX_SHAKE 0x1
#define PS_FX_FLASH 0x2
#define PS_FX_AURA_ON 0x4
#define PS_FX_AURA_OFF 0x8
#define PS_FX_CHARGE_ON 0x10
#define PS_FX_CHARGE_OFF 0x20
#define PS_FX_BURST 0x40
#define PS_FX_FADE_OUT 0x80     /* to black as the line starts (it stays black until a fade in) */
#define PS_FX_FADE_IN 0x100
#define PS_FX_BIG_SHAKE 0x200
#define PS_FX_HIDE 0x400
#define PS_FX_SHOW 0x800
#define PS_FX_L_AURA_ON 0x1000  /* the same, on the listener */
#define PS_FX_L_AURA_OFF 0x2000
#define PS_FX_L_HIDE 0x4000
#define PS_FX_L_SHOW 0x8000
#define PS_FX_RED_FLASH 0x10000
#define PS_FX_DARK 0x20000      /* the picture is dimmed while the line is up */
#define PS_FX_RUMBLE 0x40000    /* the ground shakes for the whole line */
#define PS_FX_L_BURST 0x80000

/* ---- what the battle half tells the overlay (PortStoryUi_Show) ------------------------------------------------ */
enum { PS_SHOW_SAY = 1, PS_SHOW_HIDE, PS_SHOW_TITLE, PS_SHOW_HUD, PS_SHOW_LETTERBOX, PS_SHOW_FLASH, PS_SHOW_FADE,
       PS_SHOW_TIMER, PS_SHOW_END,
       PS_SHOW_CHOICE,    /* a: the line, b: the answer under the cursor (the answers are listed) */
       PS_SHOW_CHOSEN,    /* a: the line, b: the answer picked */
       PS_SHOW_OBJECTIVE, /* a: frames it has been up, -1 gone */
       PS_SHOW_DARK,      /* a: 1 the picture dimmed, 0 not */
       PS_SHOW_CARD,      /* a: the line whose text is put up as a message in the fight, b: frames, -1 gone */
       PS_SHOW_FOUGHT };  /* a: the fight's seconds, b: the share of health the player had left: for the record */

/* ---- the fight's rules (PortStoryUi_Battle) ------------------------------------------------------------------- */
#define PB_WIN 0          /* PS_WIN_ */
#define PB_SURVIVE 1      /* seconds to hold out (PS_WIN_SURVIVE) */
#define PB_TIME 2         /* the time limit, seconds (0 none) */
#define PB_TIME_UP 3      /* at the time limit: 0 lost, 1 won, 2 by health */
#define PB_EVENTS 4       /* how many events the fight has */
#define PB_WIN_VALUE 5    /* PS_WIN_HEALTH: the share of health the other fighter is brought down to */
#define PB_FAIL_MEMBER 6  /* 1: lost as soon as one of the player's team is knocked out */
#define PB_FAIL_HEALTH 7  /* lost when the player's fighter falls to this share of health (0: no such rule) */
#define PB_BLAST 8        /* + side: blast stocks at the start, -1 as the game gives them */
#define PB_MAX 10         /* + side: starts in max power */
#define PB_FORM 12        /* + side: transformations done as the fight starts */
#define PB_OBJECTIVE 14   /* 1: the objective is put up as the fight starts */
#define PB_KI 16          /* + side: ki bars at the start, -1 as the game gives them */
#define PB_COUNT 32
enum { PS_WIN_KO, PS_WIN_SURVIVE, PS_WIN_HEALTH, PS_WIN_BLAST, PS_WIN_COUNT };

/* ---- an event of the fight (PortStoryUi_Event): when something happens, its actions are done in turn ---------- */
#define PE_WHEN 0      /* PS_WHEN_ */
#define PE_VALUE 1
#define PE_FIRST 2     /* its first action (PortStoryUi_Action) */
#define PE_ACTIONS 3   /* how many */
#define PE_REPEAT 4    /* 0 once; else it can happen again after this many seconds */
#define PE_COUNT 8
enum { PS_WHEN_START, PS_WHEN_ENEMY_HP, PS_WHEN_PLAYER_HP, PS_WHEN_ENEMY_FORM, PS_WHEN_PLAYER_FORM, PS_WHEN_TIME,
       PS_WHEN_ENEMY_MEMBER, PS_WHEN_PLAYER_MEMBER,
       PS_WHEN_PLAYER_KI, PS_WHEN_ENEMY_KI,       /* ki full */
       PS_WHEN_PLAYER_MAX, PS_WHEN_ENEMY_MAX,     /* goes into max power */
       PS_WHEN_PLAYER_KO, PS_WHEN_ENEMY_KO,       /* a team member is knocked out */
       PS_WHEN_AFTER,                             /* event number PE_VALUE has been done */
       PS_WHEN_TIME_LEFT,                         /* PE_VALUE seconds are left on the clock */
       PS_WHEN_PLAYER_COMBO, PS_WHEN_ENEMY_COMBO, /* a combo of PE_VALUE hits */
       PS_WHEN_PLAYER_BLAST, PS_WHEN_ENEMY_BLAST, /* PE_VALUE blast stocks */
       PS_WHEN_COUNT };

/* an action (PortStoryUi_Action) */
#define PA_KIND 0      /* PS_DO_ */
#define PA_SIDE 1      /* 0 the player, 1 the opponent */
#define PA_VALUE 2
#define PA_COUNT 4
enum { PS_DO_SAY,        /* line PA_VALUE in the box at the side; the fight goes on */
       PS_DO_SCENE,      /* the fight stops for a scene: the script's event action PA_VALUE */
       PS_DO_HEAL,       /* PA_VALUE percent of its health back */
       PS_DO_HURT,       /* PA_VALUE percent taken (never the last of it) */
       PS_DO_SET_HP,     /* health set to PA_VALUE percent */
       PS_DO_KI,         /* PA_VALUE ki bars added (negative: taken) */
       PS_DO_BLAST,      /* PA_VALUE blast stocks added */
       PS_DO_MAX,        /* into max power */
       PS_DO_TRANSFORM,  /* 0 its next form, 1..4 that one of its forms */
       PS_DO_SWITCH,     /* 0 the next team member, 1..5 that one */
       PS_DO_MUSIC,      /* song PA_VALUE, -2 the music stops */
       PS_DO_SOUND,
       PS_DO_STAGE,      /* the stage's destroyed version */
       PS_DO_SHAKE,      /* PA_VALUE tenths of a second */
       PS_DO_FLASH,      /* 0 white, 1 red */
       PS_DO_AURA,       /* PA_VALUE 1 on, 0 off */
       PS_DO_WIN,
       PS_DO_LOSE,
       PS_DO_CPU,        /* the opponent's level becomes PA_VALUE */
       PS_DO_TIME,       /* PA_VALUE seconds more on the clock (negative: fewer) */
       PS_DO_WAIT,       /* PA_VALUE tenths of a second before the next action */
       PS_DO_USE_BLAST,  /* the side fires: 0 / 1 its blast 1 / 2, 2 its ultimate */
       PS_DO_CARD,       /* line PA_VALUE's text as a message over the fight */
       PS_DO_COUNT };

#define PS_SCENE_FIRST 10001 /* the event actions of the scenes that stop the fight: 10001.. (group 0 of the game's) */
#define PS_SCENE_MAX 24

#endif

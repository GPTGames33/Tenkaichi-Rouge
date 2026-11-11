#ifndef PORT_BOOT_H
#define PORT_BOOT_H

/*
 * The PC version's own pictures over the start-up and the loading screens (port/src/gs/boot_ui.cpp): the mod's
 * splash after the boot logos, the lights and the version line on the title screen, the loading picture between
 * the title and the main menu, the panel over the loading screen before a fight. The game says each frame which
 * of them is up; the overlay draws it. Ints only: these are called from the game's code, whose pointers are not
 * the host's.
 */

/* What the start-up asks before it changes anything: PORT_BOOT_CLASSIC = 1 when the original timings are asked for
   (BT3_BOOT_CLASSIC=1, a test switch for measuring before and after with one build), PORT_BOOT_SPLASH = the mod's
   splash is on ("Mod splash" in F1 > Misc), PORT_BOOT_TIPS = the loading tips are on ("Loading tips"). */
#define PORT_BOOT_CLASSIC 0
#define PORT_BOOT_SPLASH 1
#define PORT_BOOT_TIPS 2
extern int Port_BootOpt(int what);

/* A line in the log at a point of the start-up, with the time since the last one (BT3_BOOT_LOG=1). The numbers
   are named in boot_ui.cpp. */
extern void Port_BootMark(int id);

/* The scene that is up this frame (0 when its loop ends), with two numbers of its own. */
#define PORT_SCENE_NONE 0
#define PORT_SCENE_SPLASH 1   /* a = its frame, b = how black the fade at its end is (0..128) */
#define PORT_SCENE_TITLE 2    /* a = how black the game's fade is (0..128), b = the title's state */
#define PORT_SCENE_MENULOAD 3 /* a = how much of the loading picture shows (0..128), b = a frame count */
#define PORT_SCENE_BATTLE 4   /* a = how black the game's fade is (0..128), b = 1 while the loading jobs run */
extern void Port_BootScene(int scene, int a, int b);
extern int Port_BootSceneNow(void); /* the scene last named */

/* The two teams of the fight being loaded: cleared, then one call per member. */
extern void Port_BootTeamClear(void);
extern void Port_BootTeamAdd(int side, int chara, int costume);

/* Starts reading the port screens' pictures ahead on a worker (once; later calls do nothing). */
extern void Port_BootPreload(void);

#endif

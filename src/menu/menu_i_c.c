#include "common.h"
#include "menu/menu_i.h"
#include "sys/pad.h"
#ifdef PORT
#include "menu/port_boot.h"
#include "menu/port_screen.h"

/* The PC version's shorter boot pictures (Logo_ShowAllQuick): START ends the whole row, any other button ends the
   picture that is up. A press while a picture is still fading in is kept until it is in: starting the fade out
   at once would show the picture whole for a frame first (the fade starts again from clear). */
#define LOGO_SKIP_PC 3
#define LOGO_ANY_BUTTON \
    (PADG_START | PADG_SELECT | PADG_CROSS | PADG_CIRCLE | PADG_TRIANGLE | PADG_SQUARE | PADG_L1 | PADG_L2 | PADG_R1 | PADG_R2)
static s32 sLogoSkipRest; /* START was pressed: no more of the skippable pictures */
static s32 sLogoPressed;  /* the buttons pressed since this picture began */
#endif

Logo *gLogo = NULL; /* 0x3B590C */

/*
 * Logo / FirstRun, 0x35D948..0x35E0F8: what the overlay does the first time it runs after boot
 * (Progress_Main calls FirstRun_Main when gProgress->flags has MPROG_FIRST_RUN): start loading partition 2 in
 * the background, wait for a controller, check the memory card, show the boot pictures, wait for the partition
 * behind the loading screen if it is not in yet, and play the opening movie.
 */

/* Allocates the logo work and relocates picture `section` of the unpacked pack. */
void Logo_Init(u32 *pack, s32 section, s32 frames) {
    MTexRes *res = NULL;

    gLogo = Heap_Alloc(0x24, 0x20, 0, 2);
    memset(gLogo, 0, 0x24);
    res = (MTexRes *)MPACK_AT(pack, section);
    Res_RelocateOffsets(&res, res, res);
    gLogo->pic = res;
    gLogo->frames = frames;
}

/* Frees the logo work. */
void Logo_Term(void) {
    if (gLogo != NULL) {
        Heap_Free(gLogo);
        gLogo = NULL;
    }
}

#define LOGO_FADE_OUT(black, n) \
    if (black) { \
        ColorFade_StartOut(0, 0, 0, n); \
    } else { \
        ColorFade_StartOut(0xFF, 0xFF, 0xFF, n); \
    }

/*
 * Shows one full-screen picture: optional fade out of what is on screen first, fade in (from black or white),
 * `frames` + 2 frames, fade out (to black or white). The background partition load is polled every frame.
 */
void Logo_Show(u32 *pack, s32 section, s32 frames, s32 fade, s32 skip, s32 inBlack, s32 outBlack, s32 fadeFirst) {
    Logo_Init(pack, section, frames);
#ifdef PORT
    sLogoPressed = 0;
#endif
    if (fadeFirst) {
        LOGO_FADE_OUT(inBlack, fade);
        do {
            do {
                Gfx_BeginFrame();
                Pad_Update();
                ColorFade_Update();
                File_WaitPartitionTimeout(2);
                ColorFade_Draw();
                Gfx_EndFrame(1);
                Dma_Flush();
                File_Stub264D90();
            } while (ColorFade_IsFadingOut());
        } while (!ColorFade_IsOutDone());
    }
    if (inBlack) {
        ColorFade_StartIn(0, 0, 0, fade);
    } else {
        ColorFade_StartIn(0xFF, 0xFF, 0xFF, fade);
    }
    while (1) {
        Gfx_BeginFrame();
        Pad_Update();
        ColorFade_Update();
        Sprite_DrawPicture(gLogo->pic, 0, 0, 0x80);
        File_WaitPartitionTimeout(2);
        ColorFade_Draw();
        Gfx_EndFrame(1);
        Dma_Flush();
        File_Stub264D90();
        if (ColorFade_IsInDone()) {
            gLogo->flags |= LOGO_FADED_IN;
        }
        if (ColorFade_IsFadingOut()) {
            continue;
        }
        if (ColorFade_IsOutDone()) {
            break;
        }
        if (gLogo->flags & LOGO_FADED_IN) {
            gLogo->timer++;
            if (gLogo->timer >= gLogo->frames + 2) {
                LOGO_FADE_OUT(outBlack, fade);
            }
        }
        switch (skip) {
        case LOGO_SKIP_ANY:
            if (gPad[0].gamePressed & (PADG_START | PADG_CROSS)) {
                LOGO_FADE_OUT(outBlack, fade / 2);
            }
            break;
        case LOGO_SKIP_AFTER_60:
            if (gLogo->timer >= 60) {
                if (gPad[0].gamePressed & (PADG_START | PADG_CROSS)) {
                    LOGO_FADE_OUT(outBlack, fade);
                }
            }
            break;
#ifdef PORT
        case LOGO_SKIP_PC:
            sLogoPressed |= gPad[0].gamePressed & LOGO_ANY_BUTTON;
            if (sLogoPressed != 0 && (gLogo->flags & LOGO_FADED_IN)) {
                if (sLogoPressed & PADG_START) {
                    sLogoSkipRest = 1; /* (to black: what is shown next comes in from black) */
                    ColorFade_StartOut(0, 0, 0, fade / 2);
                } else {
                    LOGO_FADE_OUT(outBlack, fade / 2);
                }
                sLogoPressed = 0;
            }
            break;
#endif
        }
    }
    sceGsSyncPath(0, 0);
    Logo_Term();
    Dma_ResetBuffers();
}

/* The six boot pictures in order: sections 6, 7, 8, 3, 4, 5 of the unpacked pack, 120 frames each. */
void Logo_ShowAll(u32 *pack) {
    Logo_Show(pack, 6, 0x78, 0x3C, LOGO_SKIP_ANY, 1, 0, 0);
    Logo_Show(pack, 7, 0x78, 0x3C, LOGO_SKIP_ANY, 0, 0, 0);
    Logo_Show(pack, 8, 0x78, 0x3C, LOGO_SKIP_ANY, 0, 0, 0);
    Logo_Show(pack, 3, 0x78, 0x3C, LOGO_SKIP_ANY, 0, 0, 0);
    Logo_Show(pack, 4, 0x78, 0x3C, LOGO_SKIP_ANY, 0, 0, 0);
    Logo_Show(pack, 5, 0x78, 0x3C, LOGO_SKIP_NONE, 0, 1, 0);
}

#ifdef PORT
/* The same six pictures for the PC version, each for a second and a half instead of four: 20 frames in, 50 up,
   20 out (the original's are 60, 120, 60; the six of them were 24 seconds before the title). Any button ends the
   picture that is up and START ends the row; the last one (the middleware's notice, which the original does not
   let the player skip) is always shown, as short as the others. */
#define QLOGO_FRAMES 50
#define QLOGO_FADE 20
static void Logo_ShowAllQuick(u32 *pack) {
    static const s32 sections[5] = {6, 7, 8, 3, 4};
    s32 i;

    sLogoSkipRest = 0;
    for (i = 0; i < 5 && !sLogoSkipRest; i++) {
        Logo_Show(pack, sections[i], QLOGO_FRAMES, QLOGO_FADE, LOGO_SKIP_PC, i == 0, 0, 0);
    }
    Logo_Show(pack, 5, QLOGO_FRAMES, QLOGO_FADE, LOGO_SKIP_NONE, sLogoSkipRest, 1, 0);
}

/* The mod's own card after the companies' pictures: nothing of the game's is drawn, the overlay draws the whole
   picture (boot_ui.cpp, the splash) and this loop counts its frames. Two and a half seconds; a button after the
   first third of a second starts its fade to black at once. The game's data goes on loading beside it as beside
   the logos. No sound: none is loaded this early. */
#define SPLASH_FRAMES 150
#define SPLASH_FADE 15
static void FirstRun_Splash(void) {
    s32 n = 0;
    s32 out = 0; /* frames into the fade to black (0: not started) */

    do {
        Gfx_BeginFrame();
        Pad_Update();
        File_WaitPartitionTimeout(2);
        Port_BootScene(PORT_SCENE_SPLASH, n, out * 0x80 / SPLASH_FADE);
        Gfx_EndFrame(1);
        Dma_Flush();
        File_Stub264D90();
        n++;
        if (out > 0) {
            out++;
        } else if (n >= SPLASH_FRAMES - SPLASH_FADE || (n >= 20 && (gPad[0].gamePressed & LOGO_ANY_BUTTON))) {
            out = 1;
        }
    } while (out <= SPLASH_FADE + 1); /* (the last two frames are black: the next screen comes in from black) */
    Port_BootScene(PORT_SCENE_NONE, 0, 0);
    Dma_ResetBuffers();
    sceGsSyncPath(0, 0);
}
#endif

/* If partition `pt` is not loaded yet, shows the loading screen until it is. */
void FirstRun_WaitPartition(s32 pt) {
    s32 flags = 0;

    if (File_IsPartitionLoaded(pt)) {
        return;
    }
    Dma_ResetBuffers();
    Load_InitScreen();
    ColorFade_StartIn(0, 0, 0, 0xF);
    while (1) {
        Snd_Update();
        Gfx_BeginFrame();
        Pad_Update();
        ColorFade_Update();
        File_WaitPartitionTimeout(pt);
        if (File_IsPartitionLoaded(pt)) {
            if (flags & 1) {
                if (!(flags & 2)) {
                    ColorFade_StartOut(0, 0, 0, 0xF);
                    flags |= 2;
                }
            }
        }
        Load_UpdateScreen();
        Load_DrawScreen();
        ColorFade_Draw();
        Gfx_EndFrame(2);
        Dma_Flush();
        if (ColorFade_IsInDone()) {
            flags |= 1;
        }
        if (ColorFade_IsOutDone()) {
            break;
        }
        Load_ReadInput();
    }
    Dma_ResetBuffers();
}

#ifdef PORT
/* Two settings of the PC version (F1, UI): the logos and the opening movie can be left out at the start. Both
   can be skipped with a button anyway, and nothing is loaded by either: the loading runs beside the logos and is
   waited for after them. */
/* Not in an online session (port/src/gs/net.c): there the sequence is part of what both copies compute from the
   same first moment (it runs unseen and not held to real time, Port_NetWarp), and one player's setting must not
   make their copy's start-up shorter than the other's. With "Skip logos" on one side only that copy was in the
   character select 537 vertical blanks before the other and the two never agreed again (2026-10-13). */
extern int Port_Setting(const char *name, int def);
extern int Port_NetFixedStart(void);
#define FIRSTRUN_SHOW(name) (Port_NetFixedStart() || !Port_Setting(name, 0))
#else
#define FIRSTRUN_SHOW(name) 1
#endif

/* The first-run sequence. Returns 1. */
s32 FirstRun_Main(void) {
    u32 *res;

#ifdef PORT
    PortLanguage_Run(); /* first choose a language in the game window, before loading the game partition */
    Port_BootMark(1);
    Port_BootPreload(); /* the port screens' pictures are read on a worker while the logos and the title are up */
#endif
    File_LoadPartitionNw(2);
    FirstRun_WaitPad(0, 0);
    PadWatch_SetEnabled(0);
    if (gMenuArc0 == NULL) {
        gMenuArc0 = File_LoadSync(gProgress->baseFile + 0x19, NULL, 0);
    }
    BootCard_Run(2);
    res = Sprite_Unpack(MPACK_AT(gMenuArc0, 3), NULL, NULL);
#ifdef PORT
    if (Port_NetFixedStart() || Port_BootOpt(PORT_BOOT_CLASSIC)) {
        if (FIRSTRUN_SHOW("skip_logos")) {
            Logo_ShowAll(res); /* (as the original: an online session's start is the same on both sides) */
        }
    } else if (!Port_Setting("skip_logos", 0)) {
        Logo_ShowAllQuick(res);
        Port_BootMark(2);
        if (Port_BootOpt(PORT_BOOT_SPLASH)) { /* (with the logos only: "Skip logos" stays the straight way in) */
            FirstRun_Splash();
        }
    }
    Port_BootMark(3);
#else
    if (FIRSTRUN_SHOW("skip_logos")) {
        Logo_ShowAll(res);
    }
#endif
    if (res != NULL) {
        Heap_Free(res);
    }
    if (gMenuArc0 != NULL) {
        Heap_Free(gMenuArc0);
        gMenuArc0 = NULL;
    }
    FirstRun_WaitPartition(2);
#ifdef PORT
    Port_BootMark(4);
#endif
    if (FIRSTRUN_SHOW("skip_intro")) {
        Movie_PlayOpening();
    }
#ifdef PORT
    Port_BootMark(5);
#endif
    PadWatch_SetEnabled(1);
    return 1;
}

/* Runs empty frames until controller port 0 has a usable controller. Returns 1. */
s32 FirstRun_WaitPad() {
    Pad_Update();
    if (gPad[0].status == PAD_STATUS_NONE) {
        do {
            Gfx_BeginFrame();
            Pad_Update();
            File_WaitPartitionTimeout(2);
            Gfx_EndFrame(1);
            Dma_Flush();
            File_Stub264D90();
        } while (gPad[0].status == PAD_STATUS_NONE);
        Dma_ResetBuffers();
    }
    sceGsSyncPath(0, 0);
    return 1;
}

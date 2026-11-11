// The start-up's and the loading screens' own pictures, drawn on the window's background list over the game's picture:
// the mod's splash after the boot logos, the title screen's drifting lights and version line, the loading picture
// between the title and the main menu, and the panel over the game's loading screen before a fight (the two teams, a
// progress row, a tip). Also the little the game's code needs for them (src/menu/menu_i_c.c, menu_a_c.c, menu_a.c,
// src/sys/loading.c): every function the game calls takes and returns ints only, because the game's pointers are not
// this code's. The game says each frame which scene is up (Port_BootScene); ui.cpp's frame_build asks BootUi_Frame
// whether there is something to draw and calls BootUi_Draw. Nothing here waits or adds a frame: a scene is only ever
// shown while the game is in the loop that names it.
#include <SDL3/SDL.h>
#include <atomic>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include "imgui.h"
#include "imgui_internal.h" // (the draw list's white pixel, for shapes with a colour at each corner)
#include "ui.h"
#include "fight_panels.h"

extern "C" {
extern unsigned gGsFrame;                                      // the renderer's frame counter
int Port_GameMode(void);                                       // a11y_game.c: 1 title, 4 main menu
const char *Update_Version(void);                              // net_update.cpp: this build's number ("dev": built locally)
int Port_FilePath(int ptid, int flid, const char *fname, char *out, int size); // plat_file.c
const char *Port_CharaShownName(int chara);                    // plat_charas.c: an added character's name, else NULL
void GsGpu_ArtPreload(void);                                   // gs_draw.c: reads the port screens' pictures ahead
}
ImFont *Ui_FontBold(void); // ui.cpp: the first of the game's fonts (NULL: the window's own)
bool FightPanels_Art(const char *path, int chara, int costume, SDL_GPUTexture **tex, float *hx, float *hy, float *hs, float *fx,
                     float *fy);           // fight_panels_ui.cpp: a large portrait and where its head is
std::string StoryKit_CharaName(int chara); // story_ui.cpp: a character's name in English (this thread only)
#define ART_W 256.0f
#define ART_H 512.0f
#define FACE_FILE 0x2F9 // the large portraits: file FACE_FILE + character - 1 of partition 1 (as port/src/fight_panels.c)
#define CHARA_MAX 0xA1

enum { SC_NONE, SC_SPLASH, SC_TITLE, SC_MENULOAD, SC_BATTLE };

// -------------------------------------------------------------------------------------------------------- the tips
// One line each, shown on the loading screens ("Loading tips" in F1 > Misc switches them off). Every line was checked
// against what the game does (the buttons: include/battle/btl_input.h; the rest: docs/port and the screens' own text).
// `fight`: 1 = only before a fight (it is about fighting, or about the loading screen itself).
struct Tip {
    const char *text;
    int fight;
};
static const Tip kTips[] = {
    {"Hold L2 to charge your ki. Blast techniques use it up.", 1},
    {"Square is your rush attack, Triangle fires ki blasts and Circle guards.", 1},
    {"Cross dashes. R1 flies up and R2 flies down.", 1},
    {"Lost sight of your opponent? Press L1 to lock on again.", 1},
    {"L2 + Triangle and L2 + Up + Triangle are your two Blast 2 techniques.", 1},
    {"L2 + Down + Triangle is your Ultimate Blast.", 1},
    {"L2 + Circle and L2 + Up + Circle use your Blast 1 skills. They cost Blast Stocks.", 1},
    {"Press R3 (click the right stick) to transform. Some forms need a direction as well.", 1},
    {"On this screen, tap Cross to make the fighter in the corner train.", 1},
    {"Press F1, or START + SELECT on a controller, to open the settings window at any time.", 0},
    {"F1 > Video: pick the aspect ratio and the internal resolution.", 0},
    {"F11 switches full screen on and off.", 0},
    {"F1 > Controls: change any key or controller button.", 0},
    {"F1 > Audio: music, sound effects and voices each have their own volume.", 0},
    {"F1 > UI: change the language and the menu font.", 0},
    {"Seen the logos often enough? Tick \"Skip logos\" in F1 > Misc.", 0},
    {"F1 > Cheats: \"Game speed\" slows the whole game down or speeds it up.", 0},
    {"Custom Character: build your own warrior from the parts of other fighters.", 0},
    {"In Custom Character, \"Add to roster\" puts your warrior on the character select.", 0},
    {"Story Creator: write your own sagas, scene by scene and battle by battle.", 0},
    {"A saga is one file in the \"sagas\" folder next to the game. Copy the file to share it.", 0},
    {"Story Creator > My Sagas: Triangle makes a copy of the sample saga for you to edit.", 0},
    {"Corrupt Patrol: press Triangle while walking to open the world map.", 0},
    {"Corrupt Patrol: START opens the Patrol Menu.", 0},
    {"Corrupt Patrol: stand beside someone and press Circle to talk.", 0},
    {"Corrupt Patrol: every master has five lessons. Each win teaches you a technique.", 0},
    {"Corrupt Patrol: Mr. Satan sells items at the World Tournament Stage.", 0},
    {"Corrupt Patrol: a new game lets you pick one of six races and name your warrior.", 0},
    {"Corrupt Patrol: your warrior grows from level 1 up to level 60.", 0},
    {"Mod Manager: switch character mods on and off and set their order.", 0},
    {"To add a character mod, drop its folder into \"characters\" next to the game.", 0},
    {"Script mods live in the \"mods\" folder. Switch them on in F1 > Mods.", 0},
    {"On the title menu, Triangle asks whether to quit the game.", 0},
    {"A small icon in the bottom right corner means the game is saving.", 0},
    {"These tips can be switched off: F1 > Misc > Loading tips.", 0},
};
#define TIP_COUNT ((int)(sizeof(kTips) / sizeof(kTips[0])))

// The n-th tip of this run's order (every tip once before any comes again, starting somewhere new each run);
// fight = false leaves the fight-only ones out.
static int tip_at(unsigned n, bool fight) {
    static unsigned seed = 0, stride = 0;
    if (stride == 0) {
        seed = (unsigned)SDL_GetPerformanceCounter();
        stride = TIP_COUNT % 11 != 0 ? 11 : TIP_COUNT % 13 != 0 ? 13 : 17; // (a step that shares no factor with the count)
    }
    for (int tries = 0; tries < TIP_COUNT; tries++, n++) {
        int i = (int)((seed + (uint64_t)n * stride) % TIP_COUNT);
        if (fight || !kTips[i].fight) {
            return i;
        }
    }
    return 0;
}

// ------------------------------------------------------------------------------------- what the game's thread says
static std::atomic<int> sScene{SC_NONE}, sArgA{0}, sArgB{0};
static std::atomic<Uint64> sSceneAt{0};   // when the game last named the scene (a scene the game stopped naming goes away)
static std::atomic<Uint64> sSceneFrom{0}; // when this scene began
static std::atomic<unsigned> sTipNo{0};   // counts the tips given out
static std::atomic<Uint64> sLoadDone{0};  // the fight's loading: when its jobs were done (0: still running)
static std::atomic<int> sLoadExpect{3000}; // how long it is expected to take, ms: the last one's time
static std::atomic<int> sTitleState{-1};

struct BootMember {
    int chara, costume;
    char name[96];      // an added character's shown name ("": the disc's, asked for when drawn)
    char path[FP_PATH]; // its large portrait's file ("": none)
};
struct BootTeams {
    int n[2];
    BootMember m[2][FP_MEMBERS];
};
static BootTeams sTeams;
static SDL_SpinLock sTeamLock;

static int env_on(const char *name) {
    const char *v = getenv(name);
    return v != NULL && v[0] != '\0' && v[0] != '0';
}

// What the game asks before changing its start-up: 0 = "the original timings, please" (BT3_BOOT_CLASSIC=1, a test
// switch: one build then gives both the before and the after of a measurement), 1 = the mod's splash is on,
// 2 = the loading tips are on.
extern "C" int Port_BootOpt(int what) {
    static int classic = -1;
    if (classic < 0) {
        classic = env_on("BT3_BOOT_CLASSIC");
    }
    switch (what) {
    case 0:
        return classic;
    case 1:
        return !classic && Port_Setting("mod_splash", 1) != 0;
    case 2:
        return Port_Setting("loading_tips", 1) != 0;
    }
    return 0;
}

// A line in the log at a point of the start-up or of a loading screen, with the time since the last one
// (BT3_BOOT_LOG=1): how the timings in docs and reports are measured.
extern "C" void Port_BootMark(int id) {
    static int on = -1;
    static Uint64 last;
    static const struct {
        int id;
        const char *name;
    } names[] = {
        {1, "first run starts"},      {2, "logos done"},           {3, "splash done"},          {4, "game data in"},
        {5, "opening movie done"},    {10, "title: loading"},      {11, "title: loaded"},       {12, "copyright page done"},
        {14, "title: leaving"},       {15, "title: faded out"},    {16, "title: freed"},        {17, "title: press start shown"},
        {20, "main menu: loading"},   {21, "main menu: loaded"},   {22, "main menu: first frame"},
        {23, "main menu: faded in"},  {24, "main menu: takes input"}, {30, "loading screen: start"},
        {31, "loading screen: jobs done"},
        {32, "loading screen: end"},
    };
    if (on < 0) {
        on = env_on("BT3_BOOT_LOG");
    }
    if (!on) {
        return;
    }
    const char *name = "?";
    for (const auto &n : names) {
        if (n.id == id) {
            name = n.name;
        }
    }
    Uint64 now = SDL_GetTicks();
    fprintf(stderr, "bt3: boot: %7llu ms (+%5llu) frame %6u  %s\n", (unsigned long long)now, (unsigned long long)(now - last), gGsFrame,
            name);
    last = now;
}

// The game, each frame of a loop that has one of these scenes over it (0 when the loop ends):
//   1 the mod's splash:      a = its frame, b = how black the fade at its end is (0..128)
//   2 the title screen:      a = how black the game's fade is (0..128), b = the title's state (0 intro, 1 "press
//                            start", 2 the menu)
//   3 loading the main menu: a = how much of the loading picture shows (0..128); its frames are counted here (the
//                            title screen starts it and the main menu ends it, and its light must not jump between)
//   4 loading a fight:       a = how black the game's fade is (0..128), b = 1 while the loading jobs run
extern "C" void Port_BootScene(int scene, int a, int b) {
    static int menuFrames;
    Uint64 now = SDL_GetTicks();
    int was = sScene.load();
    if (scene == SC_MENULOAD) {
        menuFrames = was == SC_MENULOAD ? menuFrames + 1 : 0;
        b = menuFrames;
    }
    if (scene != was) {
        if (was == SC_BATTLE && sLoadDone.load() != 0) { // the time this one took is the next one's estimate
            int took = (int)(sLoadDone.load() - sSceneFrom.load());
            sLoadExpect = took < 300 ? 300 : took > 60000 ? 60000 : took;
        }
        sSceneFrom = now;
        sLoadDone = 0;
        if (scene == SC_MENULOAD || scene == SC_BATTLE) {
            sTipNo++; // a new tip for each loading screen
        }
    }
    if (scene == SC_BATTLE && b == 0 && sLoadDone.load() == 0) {
        sLoadDone = now;
    }
    if (scene == SC_TITLE && b != sTitleState.load()) {
        sTitleState = b;
        if (b == 1) {
            Port_BootMark(17);
        }
    }
    if (scene != SC_TITLE) {
        sTitleState = -1;
    }
    sArgA = a;
    sArgB = b;
    sSceneAt = now;
    sScene = scene;
}

// The scene last named (the main menu asks whether the title screen left the loading picture up for it).
extern "C" int Port_BootSceneNow(void) {
    return sScene.load();
}

// The two teams of the fight being loaded, for the panel over the loading screen: cleared, then one call per member
// (the game's thread; the names and the portraits' files are looked up here, where that is safe).
extern "C" void Port_BootTeamClear(void) {
    SDL_LockSpinlock(&sTeamLock);
    sTeams.n[0] = sTeams.n[1] = 0;
    SDL_UnlockSpinlock(&sTeamLock);
}

extern "C" void Port_BootTeamAdd(int side, int chara, int costume) {
    BootMember m;
    memset(&m, 0, sizeof(m));
    m.chara = chara;
    m.costume = costume;
    const char *shown = Port_CharaShownName(chara);
    if (shown != NULL) {
        snprintf(m.name, sizeof(m.name), "%s", shown);
    }
    if (chara >= 0 && chara < CHARA_MAX && !Port_FilePath(1, FACE_FILE + chara - 1, NULL, m.path, FP_PATH)) {
        m.path[0] = '\0';
    }
    SDL_LockSpinlock(&sTeamLock);
    if (side >= 0 && side < 2 && sTeams.n[side] < FP_MEMBERS) {
        sTeams.m[side][sTeams.n[side]++] = m;
    }
    SDL_UnlockSpinlock(&sTeamLock);
}

// The port screens' pictures are read ahead while the logos and the title are up, so that the first opening of the
// Mod Manager (and the screens that share its pictures) does not stop to read them. Not with the original timings
// asked for: that run is the "before" of the measurement.
extern "C" void Port_BootPreload(void) {
    if (!Port_BootOpt(0)) {
        GsGpu_ArtPreload();
    }
}

// --------------------------------------------------------------------------------------------------- small helpers
static float sat(float v) {
    return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}
static float smooth(float v) {
    v = sat(v);
    return v * v * (3.0f - 2.0f * v);
}
static ImU32 rgba(int r, int g, int b, float a) {
    return IM_COL32(r, g, b, (int)(sat(a) * 255.0f + 0.5f));
}
static ImU32 mixc(ImU32 a, ImU32 b, float k) {
    ImU32 out = 0;
    for (int s = 0; s < 32; s += 8) {
        float x = (float)((a >> s) & 0xFF), y = (float)((b >> s) & 0xFF);
        out |= (ImU32)(int)(x + (y - x) * k + 0.5f) << s;
    }
    return out;
}
// A steady number in 0..1 for `n` (the particles' places: the same every frame without keeping them anywhere).
static float rnd(unsigned n) {
    n = (n ^ 61u) ^ (n >> 16);
    n *= 9u;
    n ^= n >> 4;
    n *= 0x27d4eb2du;
    n ^= n >> 15;
    return (float)(n & 0xFFFFFF) / 16777215.0f;
}

// A four-cornered shape with a colour at each corner.
static void quad(ImDrawList *dl, ImVec2 a, ImVec2 b, ImVec2 c, ImVec2 d, ImU32 ca, ImU32 cb, ImU32 cc, ImU32 cd) {
    const ImVec2 uv = dl->_Data->TexUvWhitePixel;
    dl->PrimReserve(6, 4);
    unsigned i = dl->_VtxCurrentIdx;
    dl->PrimWriteVtx(a, uv, ca);
    dl->PrimWriteVtx(b, uv, cb);
    dl->PrimWriteVtx(c, uv, cc);
    dl->PrimWriteVtx(d, uv, cd);
    dl->PrimWriteIdx((ImDrawIdx)i);
    dl->PrimWriteIdx((ImDrawIdx)(i + 1));
    dl->PrimWriteIdx((ImDrawIdx)(i + 2));
    dl->PrimWriteIdx((ImDrawIdx)i);
    dl->PrimWriteIdx((ImDrawIdx)(i + 2));
    dl->PrimWriteIdx((ImDrawIdx)(i + 3));
}

// A soft round light: `in` at the middle, a third of it half way out, nothing at the rim.
static void glow(ImDrawList *dl, ImVec2 c, float rx, float ry, ImU32 in) {
    const int n = 28;
    const ImVec2 uv = dl->_Data->TexUvWhitePixel;
    ImU32 mid = (in & ~IM_COL32_A_MASK) | ((ImU32)(((in >> IM_COL32_A_SHIFT) & 0xFF) * 0.3f) << IM_COL32_A_SHIFT);
    ImU32 out = in & ~IM_COL32_A_MASK;
    if ((in >> IM_COL32_A_SHIFT) == 0 || rx <= 0.0f || ry <= 0.0f) {
        return;
    }
    dl->PrimReserve(n * 9, n * 2 + 1);
    unsigned base = dl->_VtxCurrentIdx;
    dl->PrimWriteVtx(c, uv, in);
    for (int i = 0; i < n; i++) {
        float a = (float)i * 6.2831853f / (float)n, cs = cosf(a), sn = sinf(a);
        dl->PrimWriteVtx(ImVec2(c.x + cs * rx * 0.45f, c.y + sn * ry * 0.45f), uv, mid);
        dl->PrimWriteVtx(ImVec2(c.x + cs * rx, c.y + sn * ry), uv, out);
    }
    for (int i = 0; i < n; i++) {
        unsigned m0 = base + 1 + i * 2, m1 = base + 1 + ((i + 1) % n) * 2;
        dl->PrimWriteIdx((ImDrawIdx)base);
        dl->PrimWriteIdx((ImDrawIdx)m0);
        dl->PrimWriteIdx((ImDrawIdx)m1);
        dl->PrimWriteIdx((ImDrawIdx)m0);
        dl->PrimWriteIdx((ImDrawIdx)(m0 + 1));
        dl->PrimWriteIdx((ImDrawIdx)(m1 + 1));
        dl->PrimWriteIdx((ImDrawIdx)m0);
        dl->PrimWriteIdx((ImDrawIdx)(m1 + 1));
        dl->PrimWriteIdx((ImDrawIdx)m1);
    }
}

// A line of text whose colour runs from `top` to `bot` down the letters and that leans to the right by `lean`
// (the letters' corners are moved after ImGui has laid them out).
static void fancy_text(ImDrawList *dl, ImFont *f, float size, ImVec2 pos, const char *s, ImU32 top, ImU32 bot, float lean) {
    int v0 = dl->VtxBuffer.Size;
    dl->AddText(f, size, pos, IM_COL32_WHITE, s);
    float y0 = pos.y + size * 0.15f, y1 = pos.y + size * 0.95f, base = pos.y + size * 0.85f;
    for (int i = v0; i < dl->VtxBuffer.Size; i++) {
        ImDrawVert &v = dl->VtxBuffer[i];
        v.col = mixc(top, bot, sat((v.pos.y - y0) / (y1 - y0)));
        v.pos.x -= lean * (v.pos.y - base);
    }
}

// The same line with a dark rim behind it, so that it reads on any picture.
static void rim_text(ImDrawList *dl, ImFont *f, float size, ImVec2 pos, const char *s, ImU32 top, ImU32 bot, float lean, float rim,
                     ImU32 dark) {
    for (int i = 0; i < 8; i++) {
        float a = (float)i * 0.7853982f;
        fancy_text(dl, f, size, ImVec2(pos.x + cosf(a) * rim, pos.y + sinf(a) * rim), s, dark, dark, lean);
    }
    fancy_text(dl, f, size, pos, s, top, bot, lean);
}

// Plain text with a one-pixel shadow.
static void shadow_text(ImDrawList *dl, ImFont *f, float size, ImVec2 pos, ImU32 col, const char *s) {
    float a = (float)((col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
    float d = size < 20.0f ? 1.0f : size * 0.06f;
    dl->AddText(f, size, ImVec2(pos.x + d, pos.y + d), rgba(0, 0, 0, a * 0.75f), s);
    dl->AddText(f, size, pos, col, s);
}

// Text with space between its letters (the small capitals over and under a title); returns its width, and draws
// nothing with dl == NULL.
static float spaced_text(ImDrawList *dl, ImFont *f, float size, ImVec2 pos, ImU32 col, const char *s, float gap) {
    float x = pos.x;
    for (const char *p = s; *p != '\0'; p++) {
        char one[2] = {*p, '\0'};
        if (dl != NULL) {
            dl->AddText(f, size, ImVec2(x, pos.y), col, one);
        }
        x += f->CalcTextSizeA(size, FLT_MAX, 0.0f, one).x + (p[1] != '\0' ? gap : 0.0f);
    }
    return x - pos.x;
}

// `s` broken into lines no wider than `width`.
static std::vector<std::string> wrap(ImFont *f, float size, float width, const char *s) {
    std::vector<std::string> lines;
    std::string line;
    const char *p = s;
    while (*p != '\0') {
        const char *e = p;
        while (*e != '\0' && *e != ' ') {
            e++;
        }
        std::string word(p, (size_t)(e - p));
        std::string tried = line.empty() ? word : line + " " + word;
        if (!line.empty() && f->CalcTextSizeA(size, FLT_MAX, 0.0f, tried.c_str()).x > width) {
            lines.push_back(line);
            line = word;
        } else {
            line = tried;
        }
        p = *e == ' ' ? e + 1 : e;
    }
    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
}

// One of the seven orbs of a progress row or ring: `lit` 0 (a dim bead) to 1 (glowing).
static void orb(ImDrawList *dl, ImVec2 c, float r, float lit, float alpha) {
    lit = sat(lit);
    glow(dl, c, r * 2.6f, r * 2.6f, rgba(255, 170, 40, 0.55f * lit * alpha));
    dl->AddCircleFilled(c, r, mixc(rgba(70, 48, 36, alpha), rgba(255, 152, 24, alpha), lit), 24);
    dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.18f), r * 0.78f, mixc(rgba(88, 60, 44, alpha), rgba(255, 190, 60, alpha), lit), 24);
    dl->AddCircleFilled(ImVec2(c.x - r * 0.32f, c.y - r * 0.36f), r * 0.26f, rgba(255, 255, 255, (0.18f + 0.6f * lit) * alpha), 12);
    dl->AddCircle(c, r, rgba(30, 14, 4, 0.8f * alpha), 24, r < 8.0f ? 1.0f : r * 0.14f);
}

// The picture's rectangle and a unit that is one PS2 line of it (448 to the height), so everything keeps its
// proportions at any window size.
struct View {
    float x, y, w, h, u;
    ImVec2 a, b;
};
static View view(void) {
    View v;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    v.x = (float)gUiPresentX;
    v.y = (float)gUiPresentY;
    v.w = (float)gUiPresentW;
    v.h = (float)gUiPresentH;
    if (v.w <= 0.0f || v.h <= 0.0f) { // (nothing presented yet: the whole window)
        v.x = v.y = 0.0f;
        v.w = ds.x;
        v.h = ds.y;
    }
    v.u = v.h / 448.0f;
    v.a = ImVec2(v.x, v.y);
    v.b = ImVec2(v.x + v.w, v.y + v.h);
    return v;
}
static ImFont *bold(void) {
    return Ui_FontBold() != NULL ? Ui_FontBold() : ImGui::GetFont();
}
static float px(float size) { // (letters are never asked for smaller than can be read)
    return size < 12.0f ? 12.0f : floorf(size + 0.5f);
}

// The dark backdrop of the splash and of the main menu's loading picture: deep blue running to black, with a warm
// light low in the middle.
static void backdrop(ImDrawList *dl, const View &v, float alpha, float warm) {
    float my = v.y + v.h * 0.55f;
    ImU32 top = rgba(5, 7, 20, alpha), mid = rgba(13, 17, 44, alpha), bot = rgba(2, 2, 8, alpha);
    quad(dl, v.a, ImVec2(v.b.x, v.a.y), ImVec2(v.b.x, my), ImVec2(v.a.x, my), top, top, mid, mid);
    quad(dl, ImVec2(v.a.x, my), ImVec2(v.b.x, my), v.b, ImVec2(v.a.x, v.b.y), mid, mid, bot, bot);
    glow(dl, ImVec2(v.x + v.w * 0.5f, v.y + v.h * 0.52f), v.w * 0.62f, v.h * 0.5f, rgba(255, 120, 30, 0.20f * alpha * warm));
    glow(dl, ImVec2(v.x + v.w * 0.5f, v.y + v.h * 0.52f), v.w * 0.34f, v.h * 0.22f, rgba(255, 200, 90, 0.16f * alpha * warm));
}

// Small lights that rise and fade: `n` of them in the rectangle from `a` to `b`, `t` seconds on the clock.
static void motes(ImDrawList *dl, ImVec2 a, ImVec2 b, float u, float t, int n, unsigned salt, ImU32 col, float alpha) {
    float w = b.x - a.x, h = b.y - a.y;
    for (int i = 0; i < n; i++) {
        unsigned k = salt + (unsigned)i * 7u;
        float life = 5.0f + rnd(k + 1) * 6.0f, ph = t / life + rnd(k + 2);
        float f = ph - floorf(ph);
        float x = a.x + w * rnd(k + 3 + (unsigned)floorf(ph) * 131u) + sinf(t * (0.4f + rnd(k + 4)) + (float)i) * u * 6.0f;
        float y = b.y - f * h * (0.55f + 0.45f * rnd(k + 5));
        float s = (1.0f + rnd(k + 6) * 1.8f) * u;
        float al = sinf(f * 3.1415927f) * alpha * (0.45f + 0.55f * rnd(k + 7));
        if (al <= 0.01f) {
            continue;
        }
        glow(dl, ImVec2(x, y), s * 4.5f, s * 4.5f, (col & ~IM_COL32_A_MASK) | ((ImU32)(al * 0.5f * 255.0f) << IM_COL32_A_SHIFT));
        dl->AddCircleFilled(ImVec2(x, y), s, (col & ~IM_COL32_A_MASK) | ((ImU32)(al * 255.0f) << IM_COL32_A_SHIFT), 10);
    }
}

// -------------------------------------------------------------------------------------------------- 1: the splash
// The mod's own card after the companies' logos: its name written in by a streak of light, an aura rising behind
// it, the version under it. It covers the picture (the game draws nothing under it) and ends on black, as the next
// screen starts from black.
static void splash_draw(ImDrawList *dl, const View &v, int frame, int cover) {
    float f = (float)frame;
    float in = sat(f / 12.0f), out = 1.0f - sat((float)cover / 128.0f), vis = in < out ? in : out;
    ImFont *big = bold(), *small = ImGui::GetFont();
    const char *name = "TENKAIVERSE";
    float cx = v.x + v.w * 0.5f, cy = v.y + v.h * 0.47f;
    float size = v.h * 0.17f;
    float tw = big->CalcTextSizeA(size, FLT_MAX, 0.0f, name).x;
    if (tw > v.w * 0.74f) {
        size *= v.w * 0.74f / tw;
        tw = v.w * 0.74f;
    }
    size = floorf(size);
    tw = big->CalcTextSizeA(size, FLT_MAX, 0.0f, name).x;
    ImVec2 pos(floorf(cx - tw * 0.5f), floorf(cy - size * 0.55f));
    float lean = 0.16f;
    float p = smooth((f - 12.0f) / 40.0f);                      // how far the streak has written the name
    float after = f - 52.0f;                                    // frames since it finished
    float left = pos.x - size * 0.35f, right = pos.x + tw + size * 0.35f, edge = left + (right - left) * p;
    float pulse = 0.85f + 0.15f * sinf(f * 0.11f);

    dl->PushClipRect(v.a, v.b, true);
    dl->AddRectFilled(v.a, v.b, IM_COL32(0, 0, 0, 255));
    backdrop(dl, v, 1.0f, sat(f / 50.0f) * pulse);
    // the aura behind the name, and its rising lights
    glow(dl, ImVec2(cx, cy + size * 0.1f), tw * 0.62f * (0.6f + 0.4f * p), size * 1.5f, rgba(255, 150, 40, 0.30f * p * pulse));
    motes(dl, ImVec2(pos.x - size * 0.4f, cy - size * 1.6f), ImVec2(pos.x + tw + size * 0.4f, cy + size * 0.9f), v.u, f / 60.0f * 2.2f, 44,
          900, rgba(255, 214, 120, 1.0f), sat((f - 16.0f) / 30.0f) * 0.9f);
    if (after > 0.0f && after < 36.0f) { // a ring of light leaves the name as the streak ends
        float k = after / 36.0f;
        dl->AddEllipse(ImVec2(cx, cy), ImVec2(tw * (0.3f + 0.75f * k), size * (0.5f + 2.2f * k)), rgba(255, 220, 150, 0.5f * (1.0f - k)), 0.0f,
                       64, v.u * (3.0f - 2.0f * k));
    }
    // the name, as far as the streak has gone
    dl->PushClipRect(ImVec2(v.x, v.y), ImVec2(edge < v.b.x ? edge : v.b.x, v.b.y), true);
    for (int i = 0; i < 10; i++) { // a soft warm halo around the letters
        float a = (float)i * 0.6283185f, r = size * 0.05f;
        fancy_text(dl, big, size, ImVec2(pos.x + cosf(a) * r, pos.y + sinf(a) * r), name, rgba(255, 120, 20, 0.10f), rgba(255, 80, 0, 0.10f),
                   lean);
    }
    rim_text(dl, big, size, pos, name, rgba(255, 246, 190, 1.0f), rgba(255, 138, 22, 1.0f), lean, size * 0.022f + 1.0f,
             rgba(46, 14, 0, 1.0f));
    dl->PopClipRect();
    if (p > 0.0f && p < 1.0f) { // the streak itself: a slanted bar of light at the edge of what is written
        float a = sinf(p * 3.1415927f), wdt = size * 0.5f, top = pos.y - size * 0.25f, bot = pos.y + size * 1.2f;
        float sl = lean * (bot - top);
        ImU32 core = rgba(255, 250, 225, 0.95f * a), none = rgba(255, 190, 90, 0.0f);
        quad(dl, ImVec2(edge - wdt + sl, top), ImVec2(edge + sl, top), ImVec2(edge, bot), ImVec2(edge - wdt, bot), none, core, core, none);
        quad(dl, ImVec2(edge + sl, top), ImVec2(edge + wdt * 0.25f + sl, top), ImVec2(edge + wdt * 0.25f, bot), ImVec2(edge, bot), core, none,
             none, core);
        glow(dl, ImVec2(edge + sl * 0.5f, cy), size * 0.9f, size * 1.3f, rgba(255, 220, 150, 0.55f * a));
    }
    if (after > 0.0f) {
        float k = sat(after / 22.0f), e = 1.0f - (1.0f - k) * (1.0f - k);
        float ly = floorf(pos.y + size * 1.08f), half = tw * 0.56f * e, th = v.u * 1.5f < 2.0f ? 2.0f : v.u * 1.5f;
        ImU32 gold = rgba(255, 196, 80, 0.95f), none = rgba(255, 196, 80, 0.0f);
        quad(dl, ImVec2(cx - half, ly), ImVec2(cx, ly), ImVec2(cx, ly + th), ImVec2(cx - half, ly + th), none, gold, gold, none);
        quad(dl, ImVec2(cx, ly), ImVec2(cx + half, ly), ImVec2(cx + half, ly + th), ImVec2(cx, ly + th), gold, none, none, gold);
        // the small lines over and under it
        const char *over = "TENKAICHI 3 PC PORT";
        float os = px(v.h * 0.036f), gap = os * 0.42f;
        float ow = spaced_text(NULL, small, os, ImVec2(0.0f, 0.0f), 0, over, gap);
        spaced_text(dl, small, os, ImVec2(floorf(cx - ow * 0.5f), floorf(pos.y - os * 1.5f)), rgba(214, 222, 246, 0.85f * sat(after / 20.0f)),
                    over, gap);
        char under[96];
        const char *ver = Update_Version();
        if (ver == NULL || ver[0] == '\0' || strcmp(ver, "dev") == 0) {
            snprintf(under, sizeof(under), "development build");
        } else {
            snprintf(under, sizeof(under), "version %s", ver);
        }
        float us = px(v.h * 0.034f);
        float uw = small->CalcTextSizeA(us, FLT_MAX, 0.0f, under).x;
        dl->AddText(small, us, ImVec2(floorf(cx - uw * 0.5f), ly + th + us * 0.7f), rgba(255, 214, 150, 0.8f * sat((after - 10.0f) / 20.0f)),
                    under);
        if (after < 14.0f) { // the flash as the streak ends
            dl->AddRectFilled(v.a, v.b, rgba(255, 240, 210, 0.22f * (1.0f - after / 14.0f)));
        }
    }
    if (vis < 1.0f) {
        dl->AddRectFilled(v.a, v.b, rgba(0, 0, 0, 1.0f - vis));
    }
    dl->PopClipRect();
}

// --------------------------------------------------------------------------------------------- 2: the title screen
// Over the game's title: small warm lights drifting up, a slow sweep of light across the picture now and then, and
// the version in the corner. All of it fades with the game's own fade, so it comes and goes with the screen.
static void title_draw(ImDrawList *dl, const View &v, int fade) {
    float vis = 1.0f - sat((float)fade / 128.0f);
    float t = (float)ImGui::GetTime();
    if (vis <= 0.0f) {
        return;
    }
    dl->PushClipRect(v.a, v.b, true);
    motes(dl, v.a, v.b, v.u, t, 30, 100, rgba(255, 250, 220, 1.0f), 0.7f * vis);
    {   // the sweep: a slanted band of light crossing from the left, once every nine seconds
        float ph = fmodf(t, 9.0f) / 1.8f;
        if (ph < 1.0f) {
            float cxs = v.x + v.w * (-0.25f + 1.5f * ph), half = v.w * 0.11f, sl = v.h * 0.32f;
            ImU32 core = rgba(255, 255, 255, 0.13f * vis * sinf(ph * 3.1415927f)), none = rgba(255, 255, 255, 0.0f);
            quad(dl, ImVec2(cxs - half + sl, v.y), ImVec2(cxs + sl, v.y), ImVec2(cxs, v.b.y), ImVec2(cxs - half, v.b.y), none, core, core, none);
            quad(dl, ImVec2(cxs + sl, v.y), ImVec2(cxs + half + sl, v.y), ImVec2(cxs + half, v.b.y), ImVec2(cxs, v.b.y), core, none, none, core);
        }
    }
    {   // the version, bottom right
        char line[96];
        const char *ver = Update_Version();
        ImFont *f = ImGui::GetFont();
        float size = px(v.u * 11.0f);
        if (ver == NULL || ver[0] == '\0' || strcmp(ver, "dev") == 0) {
            snprintf(line, sizeof(line), "Tenkaiverse dev build");
        } else {
            snprintf(line, sizeof(line), "Tenkaiverse %s%s", ver[0] >= '0' && ver[0] <= '9' ? "v" : "", ver);
        }
        ImVec2 ts = f->CalcTextSizeA(size, FLT_MAX, 0.0f, line);
        ImVec2 at(floorf(v.b.x - ts.x - v.u * 10.0f), floorf(v.b.y - ts.y - v.u * 7.0f));
        float m = size * 0.45f; // (a soft dark pill under it: the title's art is bright there)
        dl->AddRectFilled(ImVec2(at.x - m, at.y - m * 0.4f), ImVec2(at.x + ts.x + m, at.y + ts.y + m * 0.4f), rgba(10, 12, 28, 0.45f * vis),
                          ts.y);
        shadow_text(dl, f, size, at, rgba(255, 255, 255, 0.9f * vis), line);
    }
    dl->PopClipRect();
}

// ------------------------------------------------------------------------------- 3: loading the main menu
// Where there was black between the title and the main menu: a ring of seven orbs with a light running round it,
// "Now loading" and a tip. `cover` is how much of it shows (it comes in with the title's fade out and leaves with
// the menu's fade in, so it costs no time at all); `frame` moves the light.
static void tip_block(ImDrawList *dl, const View &v, float cx, float top, float width, const char *text, float alpha) {
    ImFont *f = ImGui::GetFont();
    float ls = px(v.u * 9.5f), ts = px(v.u * 13.0f), gap = ls * 0.5f;
    float lw = spaced_text(NULL, f, ls, ImVec2(0.0f, 0.0f), 0, "TIP", gap);
    spaced_text(dl, f, ls, ImVec2(floorf(cx - lw * 0.5f), floorf(top)), rgba(255, 196, 80, alpha), "TIP", gap);
    float y = floorf(top + ls * 1.7f);
    for (const std::string &line : wrap(f, ts, width, text)) {
        float w = f->CalcTextSizeA(ts, FLT_MAX, 0.0f, line.c_str()).x;
        shadow_text(dl, f, ts, ImVec2(floorf(cx - w * 0.5f), y), rgba(226, 232, 248, alpha), line.c_str());
        y += floorf(ts * 1.3f);
    }
}

static void menuload_draw(ImDrawList *dl, const View &v, int cover, int frame) {
    float c = sat((float)cover / 128.0f);
    float cx = v.x + v.w * 0.5f, cy = v.y + v.h * 0.40f, ring = v.h * 0.072f, r = v.h * 0.0165f;
    float head = (float)frame / 9.0f; // one orb every nine frames
    if (c <= 0.0f) {
        return;
    }
    dl->PushClipRect(v.a, v.b, true);
    backdrop(dl, v, c, 0.8f);
    motes(dl, ImVec2(v.x + v.w * 0.2f, v.y + v.h * 0.1f), ImVec2(v.x + v.w * 0.8f, v.y + v.h * 0.75f), v.u, (float)frame / 60.0f * 1.6f, 22,
          500, rgba(255, 214, 120, 1.0f), 0.6f * c);
    for (int i = 0; i < 7; i++) {
        float a = -1.5707963f + (float)i * 6.2831853f / 7.0f;
        float d = fmodf(head - (float)i, 7.0f); // how long ago the light passed this orb, in orbs
        d = d < 0.0f ? d + 7.0f : d;
        orb(dl, ImVec2(cx + cosf(a) * ring, cy + sinf(a) * ring), r, 1.0f - d / 4.0f, c);
    }
    {
        ImFont *f = bold();
        float size = px(v.u * 19.0f);
        float w = f->CalcTextSizeA(size, FLT_MAX, 0.0f, "Now loading").x;
        shadow_text(dl, f, size, ImVec2(floorf(cx - w * 0.5f), floorf(cy + ring + v.h * 0.05f)), rgba(255, 255, 255, 0.95f * c), "Now loading");
    }
    if (Port_BootOpt(2)) {
        float ly = floorf(v.y + v.h * 0.685f), half = v.w * 0.26f;
        ImU32 gold = rgba(255, 196, 80, 0.55f * c), none = rgba(255, 196, 80, 0.0f);
        quad(dl, ImVec2(cx - half, ly), ImVec2(cx, ly), ImVec2(cx, ly + 1.0f), ImVec2(cx - half, ly + 1.0f), none, gold, gold, none);
        quad(dl, ImVec2(cx, ly), ImVec2(cx + half, ly), ImVec2(cx + half, ly + 1.0f), ImVec2(cx, ly + 1.0f), gold, none, none, gold);
        tip_block(dl, v, cx, v.y + v.h * 0.715f, v.w * 0.74f, kTips[tip_at(sTipNo.load(), false)].text, c);
    }
    dl->PopClipRect();
}

// ------------------------------------------------------------------------------------------ 4: loading a fight
// A head from a large portrait in a circle, or the name's first letter where there is no portrait.
static void head(ImDrawList *dl, const BootMember &m, const std::string &name, ImVec2 c, float r, ImU32 ring, float alpha) {
    SDL_GPUTexture *tex;
    float hx, hy, hs, fx, fy;
    dl->AddCircleFilled(c, r + 1.0f, rgba(8, 10, 22, 0.9f * alpha), 40);
    if (m.path[0] != '\0' && FightPanels_Art(m.path, m.chara, m.costume, &tex, &hx, &hy, &hs, &fx, &fy) && hs > 0.0f) {
        float side = hs * 1.25f, mx = hx + hs * 0.5f, my = hy + hs * 0.5f; // (a little room around the head)
        float u0 = (mx - side * 0.5f) / ART_W, u1 = (mx + side * 0.5f) / ART_W;
        float v0 = (my - side * 0.5f) / ART_H, v1 = (my + side * 0.5f) / ART_H;
        v0 = v0 < 0.0f ? 0.0f : v0; // (inside the first of the three bands: the next is another picture)
        v1 = v1 > 1.0f ? 1.0f : v1;
        dl->AddImageRounded(ImTextureRef((ImTextureID)(intptr_t)tex), ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), ImVec2(u0, v0 / 3.0f),
                            ImVec2(u1, v1 / 3.0f), rgba(255, 255, 255, alpha), r);
    } else {
        char one[2] = {name.empty() ? '?' : name[0], '\0'};
        ImFont *f = bold();
        ImVec2 ts = f->CalcTextSizeA(r * 1.2f, FLT_MAX, 0.0f, one);
        dl->AddText(f, r * 1.2f, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), rgba(230, 236, 250, alpha), one);
    }
    dl->AddCircle(c, r, (ring & ~IM_COL32_A_MASK) | ((ImU32)(alpha * 255.0f) << IM_COL32_A_SHIFT), 40, r < 14.0f ? 1.5f : r * 0.09f);
}

// One team's part of the band: its first fighter's head and name, the others' heads small under the name.
// side 0 runs from the left edge towards the middle, side 1 from the right edge.
static void team_draw(ImDrawList *dl, const View &v, const BootTeams &t, int side, float y0, float bh, float alpha) {
    int n = t.n[side];
    if (n <= 0) {
        return;
    }
    float dir = side == 0 ? 1.0f : -1.0f;
    float edge = side == 0 ? v.x + v.w * 0.03f : v.b.x - v.w * 0.03f;
    float r1 = bh * 0.40f, cyh = y0 + bh * 0.5f;
    ImU32 ring = side == 0 ? rgba(110, 180, 255, 1.0f) : rgba(255, 120, 100, 1.0f);
    std::string names[FP_MEMBERS];
    for (int i = 0; i < n; i++) {
        names[i] = t.m[side][i].name[0] != '\0' ? std::string(t.m[side][i].name) : StoryKit_CharaName(t.m[side][i].chara);
    }
    head(dl, t.m[side][0], names[0], ImVec2(edge + dir * r1, cyh), r1, ring, alpha);
    // the name: as large as fits between the head and the "VS"
    ImFont *f = bold();
    float tx = edge + dir * (r1 * 2.0f + v.w * 0.018f), room = fabsf(v.x + v.w * 0.5f - dir * v.w * 0.075f - tx);
    float size = px(bh * 0.36f);
    float w = f->CalcTextSizeA(size, FLT_MAX, 0.0f, names[0].c_str()).x;
    if (w > room && w > 0.0f) {
        size = px(size * room / w < size * 0.55f ? size * 0.55f : size * room / w);
        w = f->CalcTextSizeA(size, FLT_MAX, 0.0f, names[0].c_str()).x;
    }
    float ny = n > 1 ? y0 + bh * 0.14f : cyh - size * 0.5f;
    ImVec2 c0(side == 0 ? tx : tx - room, y0), c1(side == 0 ? tx + room : tx, y0 + bh);
    dl->PushClipRect(c0, c1, true);
    shadow_text(dl, f, size, ImVec2(floorf(side == 0 ? tx : tx - (w < room ? w : room)), floorf(ny)), rgba(255, 255, 255, alpha),
                names[0].c_str());
    dl->PopClipRect();
    float r2 = bh * 0.17f;
    for (int i = 1; i < n; i++) {
        head(dl, t.m[side][i], names[i], ImVec2(tx + dir * (r2 + (float)(i - 1) * r2 * 2.35f), y0 + bh * 0.72f), r2, ring, alpha);
    }
}

// Over the game's loading screen: the two teams across the top (when it is a fight that is loading), and bottom
// right a card with "Now loading", seven orbs that light as the loading goes, and a tip that changes every few
// seconds. The game's own picture and its little game in the corner stay as they are.
static void battle_draw(ImDrawList *dl, const View &v, int fade, int busy) {
    float vis = 1.0f - sat((float)fade / 128.0f);
    Uint64 now = SDL_GetTicks(), from = sSceneFrom.load(), done = sLoadDone.load();
    if (vis <= 0.0f) {
        return;
    }
    (void)busy;
    dl->PushClipRect(v.a, v.b, true);
    BootTeams t;
    SDL_LockSpinlock(&sTeamLock);
    t = sTeams;
    SDL_UnlockSpinlock(&sTeamLock);
    if (t.n[0] > 0 && t.n[1] > 0) {
        float y0 = floorf(v.y + v.h * 0.045f), bh = floorf(v.h * 0.155f), cx = v.x + v.w * 0.5f;
        ImU32 blue = rgba(18, 58, 140, 0.84f * vis), red = rgba(150, 30, 34, 0.84f * vis), dark = rgba(8, 9, 20, 0.80f * vis);
        quad(dl, ImVec2(v.x, y0), ImVec2(cx, y0), ImVec2(cx, y0 + bh), ImVec2(v.x, y0 + bh), blue, dark, dark, blue);
        quad(dl, ImVec2(cx, y0), ImVec2(v.b.x, y0), ImVec2(v.b.x, y0 + bh), ImVec2(cx, y0 + bh), dark, red, red, dark);
        dl->AddRectFilled(ImVec2(v.x, y0), ImVec2(v.b.x, y0 + 1.0f), rgba(255, 196, 80, 0.75f * vis));
        dl->AddRectFilled(ImVec2(v.x, y0 + bh - 1.0f), ImVec2(v.b.x, y0 + bh), rgba(255, 196, 80, 0.75f * vis));
        team_draw(dl, v, t, 0, y0, bh, vis);
        team_draw(dl, v, t, 1, y0, bh, vis);
        ImFont *f = bold();
        float size = floorf(bh * 0.62f);
        float w = f->CalcTextSizeA(size, FLT_MAX, 0.0f, "VS").x;
        rim_text(dl, f, size, ImVec2(floorf(cx - w * 0.5f), floorf(y0 + bh * 0.5f - size * 0.52f)), "VS", rgba(255, 246, 190, vis),
                 rgba(255, 138, 22, vis), 0.16f, size * 0.03f + 1.0f, rgba(46, 14, 0, vis));
    }
    {
        bool tips = Port_BootOpt(2) != 0;
        ImFont *f = ImGui::GetFont(), *fb = bold();
        float pad = v.u * 9.0f, cw = v.w * 0.56f < v.u * 300.0f ? v.w * 0.56f : v.u * 300.0f;
        float x1 = v.b.x - v.w * 0.025f, x0 = x1 - cw;
        float ts = px(v.u * 12.5f), ls = px(v.u * 12.0f), row = v.u * 20.0f;
        // the tip: a new one every seven seconds, each fading in and out
        float secs = (float)(now - from) / 1000.0f, slot = 7.0f, in = fmodf(secs, slot);
        float ta = sat(in / 0.35f) < sat((slot - in) / 0.35f) ? sat(in / 0.35f) : sat((slot - in) / 0.35f);
        // (the fighting tips only before a fight: the same screen also covers other loads, the Duel menu's say)
        const char *text = kTips[tip_at(sTipNo.load() * 5u + (unsigned)(secs / slot), t.n[0] > 0 && t.n[1] > 0)].text;
        std::vector<std::string> lines;
        if (tips) {
            lines = wrap(f, ts, cw - pad * 2.0f, text);
        }
        float lh = floorf(ts * 1.3f), body = tips ? lh * 3.0f + pad * 0.6f : 0.0f; // (room for three lines, so the card does not jump)
        float y1 = v.b.y - v.h * 0.045f, y0 = y1 - (pad + row + body + pad * 0.9f);
        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), rgba(8, 10, 24, 0.80f * vis), v.u * 5.0f);
        dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), rgba(255, 196, 80, 0.55f * vis), v.u * 5.0f, 0, 1.0f);
        // how far the loading is: the last one's time is the guide, and the row only fills when the jobs are done
        float expect = (float)sLoadExpect.load(), el = (float)((done != 0 ? done : now) - from);
        float p = 0.9f * (1.0f - expf(-2.3f * el / expect));
        if (done != 0) {
            p += (1.0f - p) * sat((float)(now - done) / 350.0f);
        }
        shadow_text(dl, fb, ls, ImVec2(floorf(x0 + pad), floorf(y0 + pad + (row - ls) * 0.5f)), rgba(255, 255, 255, 0.95f * vis), "Now loading");
        float r = row * 0.30f, step = r * 2.7f, ox = x1 - pad - r - step * 6.0f;
        for (int i = 0; i < 7; i++) {
            orb(dl, ImVec2(ox + step * (float)i, y0 + pad + row * 0.5f), r, p * 7.0f - (float)i, vis);
        }
        float by = y0 + pad + row + pad * 0.45f; // a thin bar under the row, for the smooth part of the progress
        dl->AddRectFilled(ImVec2(x0 + pad, by), ImVec2(x1 - pad, by + 2.0f), rgba(255, 255, 255, 0.14f * vis));
        dl->AddRectFilled(ImVec2(x0 + pad, by), ImVec2(x0 + pad + (cw - pad * 2.0f) * sat(p), by + 2.0f), rgba(255, 196, 80, 0.95f * vis));
        if (tips) {
            float y = floorf(by + pad * 0.75f);
            dl->AddText(f, ts, ImVec2(floorf(x0 + pad), y), rgba(255, 196, 80, ta * vis), "Tip");
            float tw = f->CalcTextSizeA(ts, FLT_MAX, 0.0f, "Tip  ").x;
            lines = wrap(f, ts, cw - pad * 2.0f - tw, text);
            for (size_t i = 0; i < lines.size() && i < 3; i++) {
                dl->AddText(f, ts, ImVec2(floorf(x0 + pad + tw), y), rgba(226, 232, 248, ta * vis), lines[i].c_str());
                y += lh;
            }
        }
    }
    dl->PopClipRect();
}

// ------------------------------------------------------------------------------------------------ what ui.cpp calls
// Once a frame, before the overlay is built: 1 when one of the scenes has something to draw. A scene the game has
// stopped naming is dropped (the splash and the title are named every frame; the loading scenes stay through the
// load they cover, when the game's thread is busy), and the title's and the menu loading's are only ever shown in
// the title and main menu modes, so nothing of this can be left over a fight or one of the port's screens.
extern "C" int BootUi_Frame(void) {
    static int log = -1;
    static Uint64 last;
    Uint64 now = SDL_GetTicks();
    if (log < 0) {
        log = env_on("BT3_BOOT_LOG");
    }
    if (log && last != 0 && now - last > 50) { // (a picture that came late: what a stop to read a file looks like)
        fprintf(stderr, "bt3: boot: long frame %llu ms (mode %d, frame %u)\n", (unsigned long long)(now - last), Port_GameMode(), gGsFrame);
    }
    last = now;
    int sc = sScene.load();
    if (sc == SC_NONE) {
        return 0;
    }
    Uint64 age = now - sSceneAt.load(), limit = sc == SC_MENULOAD ? 30000 : sc == SC_BATTLE ? 8000 : 1500;
    int mode = Port_GameMode();
    if (age > limit) {
        sScene = SC_NONE;
        return 0;
    }
    if ((sc == SC_TITLE && mode != 1) || (sc == SC_MENULOAD && mode != 1 && mode != 4)) {
        return 0;
    }
    return 1;
}

extern "C" void BootUi_Draw(void) {
    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    View v = view();
    int a = sArgA.load(), b = sArgB.load();
    switch (sScene.load()) {
    case SC_SPLASH:
        splash_draw(dl, v, a, b);
        break;
    case SC_TITLE:
        title_draw(dl, v, a);
        break;
    case SC_MENULOAD:
        if (Port_GameMode() == 1) { // still the title under it: its lights go out with it instead of all at once
            title_draw(dl, v, a);
        }
        menuload_draw(dl, v, a, b);
        break;
    case SC_BATTLE:
        battle_draw(dl, v, a, b);
        break;
    }
}

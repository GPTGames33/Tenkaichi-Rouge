// "HUD look: Modern" (the default): the fight's HUD drawn by the port over the game's picture, at the window's own
// resolution, in place of the game's gauges, faces, team panel, timer and combo counter. Slanted health bars with the
// damage just taken running down behind them, a segmented ki gauge, blast stock pips, round portraits with the
// fighter's name, the waiting members with their health and the switch gauge, a timer plate, a combo counter.
//
// How it follows the game. The game builds, runs and "draws" its HUD as ever; the renderer leaves the pieces this
// file stands in for out of the picture (gs_draw.c, the markers of src/battle/hud_a.c, HudModern_Hidden) and notes
// each frame which of them the game asked for (GsDraw_HudPieces). So this HUD is there exactly while the game's
// would be: not in an intro, a win pose, a cutscene, a replay without HUD, and gone with it wherever a mode hides
// it. The numbers are fight_panels.c's copy of the fight, read on the game's thread; nothing here touches the game.
//
// What stays the game's own drawing: the button and technique prompts, READY / FIGHT / KO and the other notices,
// the captions, the technique's name over a combo. A script mod that stands in for a piece (hud.hide_game) wins:
// this file then leaves that piece alone (the gauges with the team panel, the timer, the combo counter).
//
// Cost: a few hundred vertices a frame into ImGui's background list, no allocation; the portraits are the fight
// panels' cached textures (fight_panels_ui.cpp).
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "imgui.h"
#include "imgui_internal.h" // (ShadeVertsLinearColorGradientKeepAlpha)
#include "fight_panels.h"
#include "mods.h"
#include "ui.h"

extern "C" {
int Port_Setting(const char *name, int def); // plat_settings.c
void Port_SettingSave(const char *name, int value);
void Port_SettingsWrite(void);
int Port_HudSwap(void);      // gs/net.c: the two sides are in each other's places on this copy
int Port_HudLayout(void);    // gs_draw.c: "HUD Style": 0 at the edges, 1 the same with long health bars, 2 in the middle
int GsDraw_HudPieces(void);  // gs_draw.c: the game's HUD pieces asked for in the last frames (1 gauges, 2 timer, 4 combo, 8 team)
int HudModern_Hidden(void);  // for gs_draw.c: the pieces this file stands in for
}
ImFont *Ui_FontBold(void); // ui.cpp: the first of the game's fonts (NULL: the window's own)
ImFont *Ui_FontName(void); // ... and the fight panels' "Name font"
bool FightPanels_Art(const char *path, int chara, int costume, SDL_GPUTexture **tex, float *hx, float *hy, float *hs, float *fx,
                     float *fy); // fight_panels_ui.cpp
bool HudModern_Active(void);   // ui.cpp, each frame: there is a fight to draw the HUD of
void HudModern_Draw(void);     // ... and then this
void HudModern_Settings(void); // ui.cpp, the settings tab's rows
#define ART_W 256.0f
#define ART_H 512.0f

// ------------------------------------------------------------------------------------------------------ settings
static struct {
    int loaded;
    int look;    // 0 Modern, 1 Original
    int size;    // percent
    int opacity; // percent
    int damage;  // the combo's damage number
    int timer;
    int names;
    int numbers; // health as a number next to the bar
} sSet;

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void settings_load(void) {
    if (sSet.loaded) {
        return;
    }
    sSet.loaded = 1;
    sSet.look = Port_Setting("hud_look", 0) != 0;
    sSet.size = clampi(Port_Setting("hud_size", 100), 70, 140);
    sSet.opacity = clampi(Port_Setting("hud_opacity", 100), 30, 100);
    sSet.damage = Port_Setting("hud_damage", 1) != 0;
    sSet.timer = Port_Setting("hud_timer", 1) != 0;
    sSet.names = Port_Setting("hud_names", 1) != 0;
    sSet.numbers = Port_Setting("hud_hp_numbers", 0) != 0;
}

void HudModern_Settings(void) {
    static const char *const kLook[] = {"Modern", "Original"};
    bool save = false;
    settings_load();
    if (ImGui::Combo("HUD look", &sSet.look, kLook, 2)) {
        Port_SettingSave("hud_look", sSet.look);
        save = true;
    }
    ImGui::SetItemTooltip("Modern: clean health, ki and team gauges drawn sharp at your window's size. Original: the game's own HUD.");
    if (sSet.look == 0) {
        bool b;
        ImGui::Indent();
        if (ImGui::SliderInt("HUD size", &sSet.size, 70, 140, "%d%%")) {
            Port_SettingSave("hud_size", sSet.size);
        }
        save |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetItemTooltip("How big the modern HUD is drawn.");
        if (ImGui::SliderInt("HUD opacity", &sSet.opacity, 30, 100, "%d%%")) {
            Port_SettingSave("hud_opacity", sSet.opacity);
        }
        save |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetItemTooltip("Lower it to see more of the fight through the HUD.");
        b = sSet.names != 0;
        if (ImGui::Checkbox("Show fighter names", &b)) {
            sSet.names = b;
            Port_SettingSave("hud_names", sSet.names);
            save = true;
        }
        b = sSet.timer != 0;
        if (ImGui::Checkbox("Show the timer", &b)) {
            sSet.timer = b;
            Port_SettingSave("hud_timer", sSet.timer);
            save = true;
        }
        b = sSet.damage != 0;
        if (ImGui::Checkbox("Show combo damage", &b)) {
            sSet.damage = b;
            Port_SettingSave("hud_damage", sSet.damage);
            save = true;
        }
        ImGui::SetItemTooltip("The damage a combo has done, under its hit count.");
        b = sSet.numbers != 0;
        if (ImGui::Checkbox("Show health as numbers", &b)) {
            sSet.numbers = b;
            Port_SettingSave("hud_hp_numbers", sSet.numbers);
            save = true;
        }
        ImGui::SetItemTooltip("Each fighter's health as a number next to the bar (10,000 a bar).");
        ImGui::Unindent();
    }
    if (save) {
        Port_SettingsWrite();
    }
}

// The game's pieces left out of the picture (gs_draw.c asks on the game's thread). Only while this file is being
// drawn: were the overlay not to run, the game's own HUD comes back by itself.
static volatile int sHide;
static volatile Uint64 sDrawnAt;
int HudModern_Hidden(void) {
    int hide = sHide;
    return hide != 0 && SDL_GetTicks() - sDrawnAt < 500 ? hide : 0;
}

// ------------------------------------------------------------------------------------------------------- drawing
static const float kPi = 3.14159265f;
static const float kSlant = 0.42f; // the bars lean toward the middle of the screen by this much of their height
static const float kSlantY = 84.0f; // (the row the lean is measured from: the health bar's foot)

// One side's frame of reference: HUD units from the side's outer top corner, x growing toward the middle.
struct View {
    ImDrawList *dl;
    float x0, y0; // window pixels of the corner
    float dir;    // +1 the left place, -1 the right
    float u;      // window pixels per unit
    float a;      // alpha of everything drawn
};

static float clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }
static float mixf(float a, float b, float t) { return a + (b - a) * t; }
static float ease_out(float t) { t = 1.0f - clamp01(t); return 1.0f - t * t * t; }
// toward `to` at `rate` a second, smoothly, whatever the frame rate
static float approach(float v, float to, float rate, float dt) { return to + (v - to) * expf(-rate * dt); }

struct Rgb {
    unsigned char r, g, b;
};
static ImU32 col(const View &v, Rgb c, float a, float k = 1.0f) {
    int r = (int)(c.r * k), g = (int)(c.g * k), b = (int)(c.b * k);
    return IM_COL32(r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b, (int)(255.0f * clamp01(a * v.a) + 0.5f));
}
static Rgb mix(Rgb a, Rgb b, float t) {
    Rgb o = {(unsigned char)mixf(a.r, b.r, t), (unsigned char)mixf(a.g, b.g, t), (unsigned char)mixf(a.b, b.b, t)};
    return o;
}
static const Rgb kWhite = {255, 255, 255}, kBlack = {0, 0, 0}, kInk = {6, 10, 22}, kPlate = {16, 22, 40};
static const Rgb kSideCol[2] = {{64, 170, 255}, {255, 92, 72}}; // the two sides' accents
// the health bar being spent, by the bars left (1 = the last): bottom and top of its shading
static const Rgb kBar[7][2] = {
    {{226, 52, 36}, {255, 150, 96}},   {{232, 186, 22}, {255, 240, 132}}, {{36, 190, 66}, {172, 255, 134}},
    {{24, 186, 172}, {150, 255, 238}}, {{40, 132, 240}, {150, 208, 255}}, {{140, 88, 240}, {212, 180, 255}},
    {{226, 80, 180}, {255, 180, 232}},
};
static const Rgb kKi[2] = {{240, 160, 12}, {255, 230, 96}};
static const Rgb kMp[2] = {{40, 110, 255}, {150, 220, 255}};
static const Rgb kBlast[2] = {{24, 150, 240}, {140, 232, 255}};
static const Rgb kLost = {235, 40, 34};

static ImVec2 pt(const View &v, float x, float y) {
    return ImVec2(v.x0 + v.dir * (x + (kSlantY - y) * kSlant) * v.u, v.y0 + y * v.u);
}
static ImVec2 pt_flat(const View &v, float x, float y) { return ImVec2(v.x0 + v.dir * x * v.u, v.y0 + y * v.u); }

// A leaning slab between two x and two y, shaded from `top` to `bot` (their alpha is the top's).
static void slab(const View &v, float x0, float x1, float y0, float y1, ImU32 top, ImU32 bot) {
    if (x1 - x0 < 0.05f || (top >> IM_COL32_A_SHIFT) == 0) {
        return;
    }
    ImVec2 p[4] = {pt(v, x0, y0), pt(v, x1, y0), pt(v, x1, y1), pt(v, x0, y1)};
    if (v.dir < 0.0f) { // (kept clockwise for the anti-aliased edge)
        ImVec2 t = p[0]; p[0] = p[1]; p[1] = t;
        t = p[2]; p[2] = p[3]; p[3] = t;
    }
    int at = v.dl->VtxBuffer.Size;
    v.dl->AddConvexPolyFilled(p, 4, top);
    if (top != bot) {
        ImGui::ShadeVertsLinearColorGradientKeepAlpha(v.dl, at, v.dl->VtxBuffer.Size, ImVec2(0.0f, v.y0 + y0 * v.u),
                                                      ImVec2(0.0f, v.y0 + y1 * v.u), top, bot);
    }
}
static void slab_line(const View &v, float x0, float x1, float y0, float y1, ImU32 c, float th) {
    if ((c >> IM_COL32_A_SHIFT) == 0) {
        return;
    }
    ImVec2 p[4] = {pt(v, x0, y0), pt(v, x1, y0), pt(v, x1, y1), pt(v, x0, y1)};
    v.dl->AddPolyline(p, 4, c, ImDrawFlags_Closed, th * v.u);
}
// a soft light round a slab: a few wider and fainter lines
static void slab_glow(const View &v, float x0, float x1, float y0, float y1, Rgb c, float a) {
    for (int i = 0; i < 3 && a > 0.01f; i++) {
        slab_line(v, x0, x1, y0, y1, col(v, c, a * (0.34f - 0.1f * i)), 3.0f + 4.0f * i);
    }
}

static ImFont *face(ImFont *f) {
    f = f != NULL ? f : Ui_FontBold();
    return f != NULL ? f : ImGui::GetFont();
}

// Text with a dark edge all round; `lean` slants it (each point moved sideways by its height over the baseline),
// `top` / `bot` shade the fill. `p` is the text's top left in window pixels.
static void text(const View &v, ImFont *f, float size, ImVec2 p, const char *t, ImU32 top, ImU32 bot, float edge, float lean = 0.0f) {
    if (size < 4.0f || (top >> IM_COL32_A_SHIFT) == 0) {
        return;
    }
    int at = v.dl->VtxBuffer.Size;
    ImU32 line = IM_COL32(4, 6, 14, (int)((top >> IM_COL32_A_SHIFT) * 0.86f));
    if (edge > 0.0f) {
        for (int i = 0; i < 8; i++) {
            float a = i * kPi / 4.0f;
            v.dl->AddText(f, size, ImVec2(p.x + cosf(a) * edge, p.y + sinf(a) * edge), line, t);
        }
    }
    int fill = v.dl->VtxBuffer.Size;
    v.dl->AddText(f, size, p, top, t);
    if (top != bot) {
        ImGui::ShadeVertsLinearColorGradientKeepAlpha(v.dl, fill, v.dl->VtxBuffer.Size, ImVec2(p.x, p.y + size * 0.2f),
                                                      ImVec2(p.x, p.y + size * 0.9f), top, bot);
    }
    if (lean != 0.0f) {
        float base = p.y + size * 0.8f;
        for (int i = at; i < v.dl->VtxBuffer.Size; i++) {
            v.dl->VtxBuffer[i].pos.x += (base - v.dl->VtxBuffer[i].pos.y) * lean;
        }
    }
}
static float text_w(ImFont *f, float size, const char *t) { return f->CalcTextSizeA(size, FLT_MAX, 0.0f, t).x; }

// A portrait's head in a disc (band 0 colour, 1 grey), or the name's first letter when there is no picture.
static void head(const View &v, const FpMember &m, ImVec2 c, float r, bool flip, int band, float a) {
    SDL_GPUTexture *tex = NULL;
    float hx, hy, hs, fx, fy;
    v.dl->AddCircleFilled(c, r, col(v, kPlate, 0.92f * a), 40);
    if (m.path[0] != '\0' && FightPanels_Art(m.path, m.chara, m.costume, &tex, &hx, &hy, &hs, &fx, &fy) && tex != NULL && hs > 1.0f) {
        // (the art's head square, well widened: the whole head in the disc, big hair and helmets included, not
        // a close-up that cuts the face or the hair off at the disc's edge)
        float sz = fminf(fmaxf(hs * 1.7f, ART_W * 0.42f), ART_W);
        float x = fminf(fmaxf(hx + hs * 0.5f - sz * 0.5f, 0.0f), ART_W - sz);
        float y = fminf(fmaxf(hy + hs * 0.5f - sz * 0.5f, 0.0f), ART_H - sz);
        float u0 = x / ART_W, u1 = (x + sz) / ART_W;
        float v0 = (band + y / ART_H) / 3.0f, v1 = (band + (y + sz) / ART_H) / 3.0f;
        if (flip) {
            float t = u0; u0 = u1; u1 = t;
        }
        v.dl->AddImageRounded(ImTextureRef((ImTextureID)(intptr_t)tex), ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), ImVec2(u0, v0),
                              ImVec2(u1, v1), col(v, kWhite, a), r);
    } else if (m.name[0] != '\0') {
        char one[2] = {m.name[0], '\0'};
        ImFont *f = face(Ui_FontBold());
        float size = r * 1.2f;
        ImVec2 ts = f->CalcTextSizeA(size, FLT_MAX, 0.0f, one);
        v.dl->AddText(f, size, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), col(v, kWhite, 0.85f * a), one);
    }
}

// What each side's gauges remember between frames (the render thread's own; not game state).
struct Anim {
    int key;         // which member this is for: another one starts afresh
    int lastHp, lastKi, lastStock, lastNext;
    float hpTrail, hpHold; // the health the trail shows; seconds before it starts running down
    float kiShow, kiTrail, kiHold;
    float blastShow;
    float hit;       // 1 at a hit taken, running to 0: the bar's flash and shake
    float stock;     // the same for a blast stock gained
    float power;     // 0..1: the powered-up look coming and going
    float mpShow;    // 0..30000: the charge toward Max Power, shown (it builds while charging at full ki)
    float skillsA;   // 0..1: the skill list's fade (shown while charging ki)
    float full;      // ... and the switch gauge's "ready"
    // the combo counter
    int seqSet;
    unsigned hitSeq, msgSeq;
    int hits;
    float pop;       // seconds since the count last changed
    float comboA;    // the plate's alpha
    float dmgFrom, dmgTo, dmgT, dmgDur; // (the count runs as long as the game's: 0.6 s, longer for a big step, at most 1.5 s)
    int msg;
    float msgT;      // seconds since the event message; < 0 none
};
static Anim sAnim[2];
static float sGaugeA, sTimerA; // the pieces' fade in and out
static double sTime;           // seconds this HUD has been drawn for (the pulses' clock)

static float pulse(float period) { return 0.5f + 0.5f * sinf((float)fmod(sTime, (double)period) / period * 2.0f * kPi); }

static void anim_step(Anim &an, const FpSide &s, float dt) {
    const FpMember &m = s.m[s.active];
    int key = (s.active << 20) | (m.chara + 1);
    if (key != an.key) { // another member, or a transformation: no trail from the one before
        an.key = key;
        an.hpTrail = (float)m.hp;
        an.lastHp = m.hp;
        an.hpHold = 0.0f;
        an.kiShow = an.kiTrail = (float)s.ki;
        an.lastKi = s.ki;
        an.blastShow = (float)s.blast;
        an.lastStock = s.blast / 100000;
        an.hit = an.stock = 0.0f;
    }
    if (m.hp < an.lastHp) {
        if (an.hpTrail <= (float)an.lastHp + 1.0f) {
            an.hpHold = 0.45f; // (a hit while the trail is already running does not stop it)
        }
        an.hit = 1.0f;
    }
    an.lastHp = m.hp;
    if (an.hpTrail < (float)m.hp) {
        an.hpTrail = (float)m.hp;
    } else if (an.hpTrail > (float)m.hp) {
        an.hpHold -= dt;
        if (an.hpHold <= 0.0f) {
            float gap = an.hpTrail - (float)m.hp;
            an.hpTrail -= (gap * 2.5f + 3000.0f) * dt;
            if (an.hpTrail < (float)m.hp) {
                an.hpTrail = (float)m.hp;
            }
        }
    }
    an.hit = an.hit > 0.0f ? an.hit - dt / 0.35f : 0.0f;
    // the ki moves smoothly; what was just spent stays a moment in red
    if (s.ki < an.lastKi - 1500) {
        an.kiHold = 0.3f;
    }
    an.lastKi = s.ki;
    an.kiShow = approach(an.kiShow, (float)s.ki, 22.0f, dt);
    if (fabsf(an.kiShow - (float)s.ki) < 60.0f) {
        an.kiShow = (float)s.ki;
    }
    if (an.kiTrail < an.kiShow) {
        an.kiTrail = an.kiShow;
    } else {
        an.kiHold -= dt;
        if (an.kiHold <= 0.0f) {
            an.kiTrail -= ((an.kiTrail - an.kiShow) * 4.0f + 6000.0f) * dt;
            if (an.kiTrail < an.kiShow) {
                an.kiTrail = an.kiShow;
            }
        }
    }
    an.blastShow = approach(an.blastShow, (float)s.blast, 14.0f, dt);
    if (s.blast / 100000 > an.lastStock) {
        an.stock = 1.0f;
    }
    an.lastStock = s.blast / 100000;
    an.stock = an.stock > 0.0f ? an.stock - dt / 0.6f : 0.0f;
    an.power = approach(an.power, s.poweredUp && !s.dead ? 1.0f : 0.0f, 10.0f, dt);
    an.mpShow = approach(an.mpShow, s.poweredUp ? 0.0f : (float)clampi(s.maxPower, 0, 30000), 16.0f, dt);
    an.full = approach(an.full, s.switchGauge >= 100000 && s.next >= 0 ? 1.0f : 0.0f, 10.0f, dt);
}

// How long the health bar may be (units) for the room between the portrait and the timer.
static float sBarLen;
static const float kBarX = 156.0f; // where the bars start, after the portrait
static const float kBarY0 = 56.0f, kBarY1 = 84.0f;

static void draw_gauges(const View &v, const FpSide &s, Anim &an, int side) {
    const FpMember &m = s.m[s.active];
    const Rgb accent = kSideCol[side];
    int hpMax = m.hpMax > 0 ? m.hpMax : 10000;
    int hp = clampi(m.hp, 0, hpMax);
    int bars = clampi((hpMax + 9999) / 10000, 1, 7);
    int left = hp > 0 ? clampi((hp + 9999) / 10000, 1, 7) : 0;
    float shake = an.hit > 0.0f ? sinf(an.hit * 26.0f) * 5.0f * an.hit : 0.0f;
    float x0 = kBarX + shake, x1 = x0 + sBarLen;
    bool low = hp > 0 && hp <= 10000 && hpMax > 10000;
    float lowP = low ? pulse(0.5f) : 0.0f;

    // ---- the portrait
    {
        ImVec2 c = pt_flat(v, 84.0f, 82.0f);
        float r = 56.0f * v.u;
        if (an.power > 0.02f) {
            for (int i = 0; i < 3; i++) {
                v.dl->AddCircle(c, r + (5.0f + 5.0f * i) * v.u, col(v, kMp[1], an.power * (0.4f - 0.11f * i) * (0.6f + 0.4f * pulse(0.6f))), 48,
                                (5.0f + 3.0f * i) * v.u);
            }
        }
        v.dl->AddCircleFilled(c, r + 6.0f * v.u, col(v, kInk, 0.9f), 48);
        head(v, m, c, r, v.dir < 0.0f, s.dead ? 1 : 0, 1.0f);
        v.dl->AddCircle(c, r + 2.2f * v.u, col(v, mix(accent, kMp[1], an.power), 1.0f), 48, 3.6f * v.u);
        if (low) {
            v.dl->AddCircle(c, r + 2.2f * v.u, col(v, kLost, 0.85f * lowP), 48, 3.6f * v.u);
        }
        if (an.hit > 0.0f) {
            v.dl->AddCircleFilled(c, r, col(v, kWhite, 0.2f * an.hit), 40);
        }
    }

    // ---- the name
    if (sSet.names && m.name[0] != '\0') {
        ImFont *f = face(Ui_FontName());
        float size = 27.0f * v.u, room = (sBarLen - 150.0f) * v.u;
        float w = text_w(f, size, m.name);
        if (w > room && w > 0.0f) {
            size *= room / w < 0.6f ? 0.6f : room / w;
            w = text_w(f, size, m.name);
        }
        ImVec2 p = pt(v, kBarX + 4.0f, 24.0f);
        p.y += (27.0f * v.u - size) * 0.8f;
        if (v.dir < 0.0f) {
            p.x -= w;
        }
        text(v, f, size, p, m.name, col(v, kWhite, 1.0f), col(v, {214, 224, 240}, 1.0f), 1.6f * v.u);
    }

    // ---- the bars in reserve: one pip each after the one being spent, in its own colour
    for (int i = 0; i < bars - 1; i++) {
        float px1 = x1 - 2.0f - i * 27.0f, px0 = px1 - 22.0f;
        int n = i + 1; // (pip n is lit while more than n bars are left)
        slab(v, px0 - 2.0f, px1 + 2.0f, 40.0f, 51.0f, col(v, kInk, 0.85f), col(v, kInk, 0.85f));
        if (n < left) {
            slab(v, px0, px1, 42.0f, 49.0f, col(v, kBar[n - 1 < 6 ? n - 1 : 6][1], 1.0f), col(v, kBar[n - 1 < 6 ? n - 1 : 6][0], 1.0f));
        } else {
            slab(v, px0, px1, 42.0f, 49.0f, col(v, {38, 46, 70}, 0.9f), col(v, {24, 30, 50}, 0.9f));
        }
    }

    // ---- the health bar: the bar being spent over the next one's colour, the damage just taken behind it
    {
        float span = x1 - x0;
        float fill = left > 0 ? clamp01(((float)hp - (left - 1) * 10000.0f) / 10000.0f) : 0.0f;
        float trail = left > 0 ? clamp01((an.hpTrail - (left - 1) * 10000.0f) / 10000.0f) : clamp01(an.hpTrail / 10000.0f);
        const Rgb *c = kBar[left > 0 ? left - 1 : 0];
        if (low) {
            slab_glow(v, x0 - 4.0f, x1 + 4.0f, kBarY0 - 4.0f, kBarY1 + 4.0f, kLost, lowP);
        }
        slab(v, x0 - 5.0f, x1 + 5.0f, kBarY0 - 4.0f, kBarY1 + 4.0f, col(v, kInk, 0.88f), col(v, kInk, 0.88f));
        slab(v, x0, x1, kBarY0, kBarY1, col(v, {30, 36, 58}, 0.9f), col(v, {14, 18, 34}, 0.9f));
        if (left > 1) { // the bar under this one
            const Rgb *n = kBar[left - 2];
            slab(v, x0, x1, kBarY0, kBarY1, col(v, n[1], 1.0f, 0.5f), col(v, n[0], 1.0f, 0.42f));
        } else if (low) {
            slab(v, x0, x1, kBarY0, kBarY1, col(v, kLost, 0.3f * lowP), col(v, kLost, 0.3f * lowP));
        }
        if (trail > fill) {
            slab(v, x0 + span * fill, x0 + span * trail, kBarY0, kBarY1, col(v, {255, 236, 226}, 0.95f), col(v, kLost, 0.95f));
        }
        if (fill > 0.0f) {
            float fx1 = x0 + span * fill;
            slab(v, x0, fx1, kBarY0, kBarY1, col(v, c[1], 1.0f), col(v, c[0], 1.0f));
            slab(v, x0, fx1, kBarY0 + 2.0f, kBarY0 + 10.0f, col(v, kWhite, 0.34f), col(v, kWhite, 0.05f)); // (a soft shine)
            if (an.hit > 0.0f) {
                slab(v, x0, fx1, kBarY0, kBarY1, col(v, kWhite, 0.55f * an.hit), col(v, kWhite, 0.55f * an.hit));
            }
        }
        slab_line(v, x0 - 5.0f, x1 + 5.0f, kBarY0 - 4.0f, kBarY1 + 4.0f, col(v, mix(accent, kWhite, 0.25f), 0.55f), 1.4f);
        if (sSet.numbers) {
            char num[16];
            ImFont *f = face(Ui_FontBold());
            float size = 21.0f * v.u;
            snprintf(num, sizeof(num), "%d", hp);
            ImVec2 p = pt(v, x1 - 10.0f, kBarY0 + 3.0f);
            if (v.dir > 0.0f) {
                p.x -= text_w(f, size, num);
            }
            text(v, f, size, p, num, col(v, kWhite, 1.0f), col(v, kWhite, 1.0f), 1.4f * v.u);
        }
    }

    // ---- the ki gauge: five cells of one bar each; powered up, the mode's time left in blue
    {
        const float y0 = 93.0f, y1 = 106.0f, gap = 5.0f;
        int cells = clampi((s.kiMax + 19999) / 20000, 1, 7);
        float len = sBarLen * 0.74f, cell = (len - gap * (cells - 1)) / cells;
        float per = 20000.0f, show = an.kiShow, trail = an.kiTrail;
        bool power = an.power > 0.5f;
        float fullP = !power && s.kiMax > 0 && s.ki >= s.kiMax ? pulse(1.0f) : 0.0f;
        const Rgb *c = power ? kMp : kKi;
        float kx = kBarX;
        if (power) { // (6000 of the mode's timer a cell, as the game's lamps)
            cells = 5;
            cell = (len - gap * 4) / 5;
            per = 6000.0f;
            show = trail = (float)s.maxPower;
        }
        slab(v, kx - 4.0f, kx + len + 4.0f, y0 - 3.0f, y1 + 3.0f, col(v, kInk, 0.85f), col(v, kInk, 0.85f));
        for (int i = 0; i < cells; i++) {
            float cx0 = kx + i * (cell + gap), cx1 = cx0 + cell;
            float f = clamp01((show - i * per) / per), t = clamp01((trail - i * per) / per);
            slab(v, cx0, cx1, y0, y1, col(v, {36, 42, 64}, 0.9f), col(v, {18, 22, 40}, 0.9f));
            if (t > f) {
                slab(v, cx0 + cell * f, cx0 + cell * t, y0, y1, col(v, kLost, 0.9f), col(v, kLost, 0.9f));
            }
            if (f > 0.0f) {
                float k = 1.0f + 0.35f * fullP;
                slab(v, cx0, cx0 + cell * f, y0, y1, col(v, c[1], 1.0f, k), col(v, c[0], 1.0f, k));
                slab(v, cx0, cx0 + cell * f, y0 + 1.0f, y0 + 5.0f, col(v, kWhite, 0.4f), col(v, kWhite, 0.05f));
            }
            if (!power && s.kiReserve > 0) { // the extra ki a few fighters hold: a red layer from the gauge's start
                float r = clamp01(((float)s.kiReserve - i * per) / per);
                if (r > 0.0f) {
                    float a = 0.35f + 0.5f * pulse(0.6f);
                    slab(v, cx0, cx0 + cell * r, y0 + 7.0f, y1, col(v, kLost, a), col(v, kLost, a));
                }
            }
        }
        // Charging at full ki with a blast stock builds Max Power (src/battle/btl_act_c.c): the gauge fills blue
        // from its start, cell by cell, and enters Max Power when the blue reaches the end.
        float build = !power ? clamp01(an.mpShow / 30000.0f) : 0.0f;
        if (build > 0.002f) {
            float reach = build * cells; // (in cells)
            for (int i = 0; i < cells; i++) {
                float f = clamp01(reach - i);
                if (f <= 0.0f) {
                    break;
                }
                float cx0 = kx + i * (cell + gap);
                slab(v, cx0, cx0 + cell * f, y0, y1, col(v, kMp[1], 1.0f, 1.0f + 0.25f * pulse(0.35f)), col(v, kMp[0], 1.0f));
                slab(v, cx0, cx0 + cell * f, y0 + 1.0f, y0 + 5.0f, col(v, kWhite, 0.5f), col(v, kWhite, 0.05f));
            }
            slab_glow(v, kx - 3.0f, kx + len * build + 3.0f, y0 - 3.0f, y1 + 3.0f, kMp[1], 0.55f + 0.35f * pulse(0.35f));
            fullP = 0.0f;
        }
        if (fullP > 0.0f) {
            slab_glow(v, kx - 3.0f, kx + len + 3.0f, y0 - 3.0f, y1 + 3.0f, kKi[1], 0.7f * fullP);
        }
        if (power) { // (over the cells: a blue light round the gauge, breathing)
            slab_glow(v, kx - 3.0f, kx + len + 3.0f, y0 - 3.0f, y1 + 3.0f, kMp[1], an.power * (0.8f + 0.2f * pulse(0.6f)));
            slab_line(v, kx - 4.0f, kx + len + 4.0f, y0 - 3.0f, y1 + 3.0f, col(v, kMp[1], an.power * 0.9f), 1.6f);
        }
        if (build > 0.002f && an.power <= 0.05f) { // how far the charge has come
            ImFont *f = face(Ui_FontBold());
            float size = 18.0f * v.u;
            char t[32];
            snprintf(t, sizeof(t), "MAX POWER %d%%", (int)(build * 100.0f));
            ImVec2 p = pt(v, kx + len + 14.0f, y0 - 2.0f);
            if (v.dir < 0.0f) {
                p.x -= text_w(f, size, t);
            }
            float a = 0.65f + 0.35f * pulse(0.35f);
            text(v, f, size, p, t, col(v, kWhite, a), col(v, Rgb{90, 190, 255}, a), 1.4f * v.u, -0.2f * v.dir);
        } else if (!power && s.chargeHeld && s.kiMax > 0 && s.ki >= s.kiMax && s.blast < 100000 && s.action == 0x37) {
            // full ki but no blast stock: Max Power cannot build; say what is missing
            ImFont *f = face(Ui_FontBold());
            float size = 16.0f * v.u;
            const char *t = "NEED 1 BLAST STOCK";
            ImVec2 p = pt(v, kx + len + 14.0f, y0 - 1.0f);
            if (v.dir < 0.0f) {
                p.x -= text_w(f, size, t);
            }
            text(v, f, size, p, t, col(v, Rgb{255, 214, 200}, 0.9f), col(v, kLost, 0.9f), 1.3f * v.u, -0.2f * v.dir);
        }
        if (an.power > 0.05f) {
            ImFont *f = face(Ui_FontBold());
            float size = 20.0f * v.u;
            const char *t = "MAX POWER";
            ImVec2 p = pt(v, kx + len + 14.0f, y0 - 3.0f);
            if (v.dir < 0.0f) {
                p.x -= text_w(f, size, t);
            }
            text(v, f, size, p, t, col(v, kWhite, an.power), col(v, Rgb{90, 190, 255}, an.power), 1.5f * v.u, -0.2f * v.dir);
        }
    }

    // ---- the blast stock: its count, a pip a stock, the next one filling
    {
        const float y0 = 115.0f, y1 = 127.0f;
        int max = clampi(s.blastMax / 100000, 1, 9);
        int have = clampi(s.blast / 100000, 0, max);
        float part = have < max ? clamp01((an.blastShow - have * 100000.0f) / 100000.0f) : 0.0f;
        float bx = kBarX + 34.0f;
        char num[8];
        ImFont *f = face(Ui_FontBold());
        float size = 30.0f * v.u;
        snprintf(num, sizeof(num), "%d", have);
        ImVec2 p = pt(v, kBarX - 2.0f, y0 - 9.0f);
        if (v.dir < 0.0f) {
            p.x -= text_w(f, size, num);
        }
        text(v, f, size, p, num, col(v, mix(kWhite, kBlast[1], 0.3f), 1.0f, 1.0f + an.stock), col(v, kBlast[1], 1.0f, 1.0f + an.stock),
             1.6f * v.u, -0.2f * v.dir);
        for (int i = 0; i < max; i++) {
            float px0 = bx + i * 30.0f, px1 = px0 + 24.0f;
            slab(v, px0 - 3.0f, px1 + 3.0f, y0 - 2.5f, y1 + 2.5f, col(v, kInk, 0.85f), col(v, kInk, 0.85f));
            slab(v, px0, px1, y0, y1, col(v, {34, 42, 66}, 0.9f), col(v, {18, 22, 40}, 0.9f));
            if (i < have) {
                float k = 1.0f + 0.9f * an.stock;
                slab(v, px0, px1, y0, y1, col(v, kBlast[1], 1.0f, k), col(v, kBlast[0], 1.0f, k));
                slab(v, px0, px1, y0 + 1.0f, y0 + 5.0f, col(v, kWhite, 0.4f), col(v, kWhite, 0.05f));
            } else if (i == have && part > 0.0f) {
                slab(v, px0, px0 + 24.0f * part, y0, y1, col(v, kBlast[1], 0.75f, 0.75f), col(v, kBlast[0], 0.75f, 0.75f));
            }
        }
        if (an.stock > 0.0f && have > 0) {
            float px0 = bx + (have - 1) * 30.0f;
            slab_glow(v, px0 - 2.0f, px0 + 26.0f, y0 - 2.0f, y1 + 2.0f, kBlast[1], an.stock);
        }

        // ---- attack, speed, defence, ki raised or lowered: a chip each, after the pips
        static const char *const kStat[4] = {"ATK", "SPD", "DEF", "KI"};
        static const Rgb kStatCol[4] = {{236, 78, 56}, {70, 200, 255}, {124, 152, 255}, {255, 208, 60}};
        float sx = bx + max * 30.0f + 12.0f;
        ImFont *sf = face(Ui_FontBold());
        float ss = 15.0f * v.u;
        for (int i = 0; i < 4; i++) {
            if ((s.statMask >> i & 1) == 0) {
                continue;
            }
            bool up = (s.statUp >> i & 1) != 0;
            float w = text_w(sf, ss, kStat[i]) / v.u + 26.0f;
            Rgb c = up ? kStatCol[i] : mix(kStatCol[i], kInk, 0.6f);
            slab(v, sx - 2.0f, sx + w + 2.0f, y0 - 4.5f, y1 + 4.5f, col(v, kInk, 0.88f), col(v, kInk, 0.88f));
            slab(v, sx, sx + w, y0 - 2.5f, y1 + 2.5f, col(v, c, 0.95f, 1.1f), col(v, c, 0.95f, 0.7f));
            ImVec2 tp = pt(v, v.dir > 0.0f ? sx + 6.0f : sx + w - 6.0f, y0 - 2.5f);
            text(v, sf, ss, tp, kStat[i], col(v, kWhite, 1.0f), col(v, kWhite, 1.0f), 1.0f * v.u);
            { // an arrow up (raised) or down (lowered)
                ImVec2 a = pt(v, v.dir > 0.0f ? sx + w - 11.0f : sx + 11.0f, (y0 + y1) * 0.5f);
                float h = 5.0f * v.u * (up ? -1.0f : 1.0f), hw = 5.0f * v.u;
                v.dl->AddTriangleFilled(ImVec2(a.x, a.y + h), ImVec2(a.x + hw, a.y - h), ImVec2(a.x - hw, a.y - h),
                                        col(v, up ? kWhite : Rgb{255, 150, 140}, 1.0f));
            }
            sx += w + 8.0f;
        }
    }
}

// ------------------------------------------------------------------------------------------------ the skill list
// While a player charges ki (or holds the charge button, which is also how the skills are fired), their Blast 1
// skills, Blast 2 techniques and Ultimate Blast are listed in their bottom corner, Xenoverse style: the buttons, the
// name, what it needs. A move that can be fired now is lit; one that cannot is dimmed, its missing need in red.
// Only for sides a person plays; small, at the screen's edge, clear of the fighters.

// a pad button: the face buttons as their shapes, the rest as a short word in a pill. Returns its width (pixels).
enum { PB_CIRCLE, PB_TRIANGLE, PB_L2, PB_UP, PB_DOWN };
static float pad_button(const View &v, int b, ImVec2 c, float r) {
    ImDrawList *dl = v.dl;
    ImU32 rim = col(v, {200, 212, 240}, 0.95f), fill = col(v, kInk, 0.92f);
    if (b == PB_L2) {
        ImFont *f = face(Ui_FontBold());
        float size = r * 1.15f, w = text_w(f, size, "L2") + r * 1.1f;
        dl->AddRectFilled(ImVec2(c.x, c.y - r * 0.82f), ImVec2(c.x + w, c.y + r * 0.82f), fill, r * 0.5f);
        dl->AddRect(ImVec2(c.x, c.y - r * 0.82f), ImVec2(c.x + w, c.y + r * 0.82f), rim, r * 0.5f, 0, 1.4f * v.u);
        dl->AddText(f, size, ImVec2(c.x + r * 0.55f, c.y - size * 0.52f), col(v, kWhite, 1.0f), "L2");
        return w;
    }
    ImVec2 m(c.x + r, c.y);
    dl->AddCircleFilled(m, r, fill, 24);
    dl->AddCircle(m, r, rim, 24, 1.4f * v.u);
    float k = r * 0.5f, t = 2.0f * v.u;
    if (b == PB_CIRCLE) {
        dl->AddCircle(m, k, col(v, {255, 110, 110}, 1.0f), 20, t);
    } else if (b == PB_TRIANGLE) {
        dl->AddTriangle(ImVec2(m.x, m.y - k * 1.1f), ImVec2(m.x + k * 1.1f, m.y + k * 0.75f), ImVec2(m.x - k * 1.1f, m.y + k * 0.75f),
                        col(v, {100, 230, 160}, 1.0f), t);
    } else { // a stick direction: an arrow
        float d = b == PB_UP ? -1.0f : 1.0f;
        ImU32 w = col(v, kWhite, 1.0f);
        dl->AddTriangleFilled(ImVec2(m.x, m.y + d * k * 1.15f), ImVec2(m.x + k, m.y - d * k * 0.2f), ImVec2(m.x - k, m.y - d * k * 0.2f), w);
        dl->AddRectFilled(ImVec2(m.x - k * 0.38f, m.y - d * k * 0.2f + (d < 0 ? 0.0f : -k * 0.9f)),
                          ImVec2(m.x + k * 0.38f, m.y - d * k * 0.2f + (d < 0 ? k * 0.9f : 0.0f)), w);
    }
    return r * 2.0f;
}

static void draw_skills(const View &v0, const FpSide &s, Anim &an, float dt, float bottom) {
    bool charging = !s.cpu && !s.dead && (s.action == 0x37 || s.chargeHeld);
    an.skillsA = charging ? fminf(1.0f, an.skillsA + dt / 0.12f) : fmaxf(0.0f, an.skillsA - dt / 0.25f);
    if (an.skillsA <= 0.01f) {
        return;
    }
    // the slots in the order the game takes them: 0, 1 Blast 1 (Circle), 2, 3 Blast 2 (Triangle), 4 the Ultimate
    static const int kSlot[5] = {0, 1, 2, 3, 4};
    static const int kDir[5] = {-1, PB_UP, -1, PB_UP, PB_DOWN};
    static const int kFace[5] = {PB_CIRCLE, PB_CIRCLE, PB_TRIANGLE, PB_TRIANGLE, PB_TRIANGLE};
    int rows = 0;
    for (int i = 0; i < 5; i++) {
        rows += s.moves[kSlot[i]].name[0] != ' ';
    }
    if (rows == 0) {
        return;
    }
    View v = v0;
    v.a = v0.a * ease_out(an.skillsA);
    ImFont *f = face(Ui_FontBold());
    const float rowH = 34.0f, w = 400.0f, pad = 10.0f, head = 24.0f;
    float slide = (1.0f - ease_out(an.skillsA)) * -30.0f;
    float y1 = bottom - 24.0f, y0 = y1 - head - rows * rowH - pad * 1.5f;
    float x0 = 24.0f + slide;
    { // the plate
        ImVec2 a = pt_flat(v, x0, y0), b = pt_flat(v, x0 + w, y1);
        ImVec2 lo(fminf(a.x, b.x), a.y), hi(fmaxf(a.x, b.x), b.y);
        v.dl->AddRectFilled(lo, hi, col(v, kInk, 0.72f), 8.0f * v.u);
        v.dl->AddRect(lo, hi, col(v, {90, 150, 230}, 0.55f), 8.0f * v.u, 0, 1.4f * v.u);
        ImVec2 la = pt_flat(v, x0 + 12.0f, y0 + head + 2.0f), lb = pt_flat(v, x0 + w - 12.0f, y0 + head + 2.0f);
        v.dl->AddLine(la, lb, col(v, {90, 150, 230}, 0.35f), 1.0f * v.u);
        float hs = 15.0f * v.u;
        const char *t = "SKILLS";
        ImVec2 hp = pt_flat(v, x0 + 14.0f, y0 + 5.0f);
        if (v.dir < 0.0f) {
            hp.x -= text_w(f, hs, t);
        }
        text(v, f, hs, hp, t, col(v, {150, 200, 255}, 1.0f), col(v, {150, 200, 255}, 1.0f), 0.0f);
    }
    float y = y0 + head + pad;
    float r = 11.0f * v.u, size = 17.0f * v.u, small = 14.0f * v.u;
    for (int i = 0; i < 5; i++) {
        const FpMove &mv = s.moves[kSlot[i]];
        if (mv.name[0] == ' ') {
            continue;
        }
        // what it needs, and whether that is there now
        char need[48];
        bool ok;
        if (i < 2) {
            int stocks = (mv.cost + 99999) / 100000;
            ok = s.blast >= mv.cost;
            if (mv.cost <= 0) {
                snprintf(need, sizeof(need), "FREE");
            } else {
                snprintf(need, sizeof(need), "%d STOCK%s", stocks, stocks == 1 ? "" : "S");
            }
        } else {
            float bars = (float)mv.cost / 20000.0f;
            ok = s.ki >= mv.cost;
            if (i == 4) {
                ok = ok && s.poweredUp;
                snprintf(need, sizeof(need), bars == (int)bars ? "MAX POWER + %.0f KI" : "MAX POWER + %.1f KI", bars);
            } else {
                snprintf(need, sizeof(need), bars == (int)bars ? "%.0f KI" : "%.1f KI", bars);
            }
        }
        float cy = (y + rowH * 0.5f) * v.u + v.y0;
        // the buttons, from the outer edge
        float bx = x0 + 12.0f;
        float px = v.x0 + v.dir * bx * v.u;
        int seq[3] = {PB_L2, kDir[i], kFace[i]}, n = 0;
        float bw[3];
        float total = 0.0f;
        for (int k = 0; k < 3; k++) {
            if (seq[k] < 0) {
                continue;
            }
            View probe = v;
            probe.a = 0.0f; // (measured, not drawn)
            bw[n] = seq[k] == PB_L2 ? pad_button(probe, PB_L2, ImVec2(-9999.0f, -9999.0f), r) : r * 2.0f;
            seq[n++] = seq[k];
            total += bw[n - 1] + 4.0f * v.u;
        }
        float bxStart = v.dir > 0.0f ? px : px - total;
        float cx = bxStart;
        View bv = v;
        bv.a = v.a * (ok ? 1.0f : 0.55f);
        for (int k = 0; k < n; k++) {
            pad_button(bv, seq[k], ImVec2(cx, cy), r);
            cx += bw[k] + 4.0f * v.u;
        }
        // the name, then what it needs at the far edge
        float nameX = bx * v.u + 84.0f * v.u;
        float needW = text_w(f, small, need);
        float room = (w - 24.0f) * v.u - 84.0f * v.u - needW - 10.0f * v.u;
        float ns = size, nw = text_w(f, ns, mv.name);
        if (nw > room && nw > 0.0f) {
            ns *= fmaxf(room / nw, 0.7f);
            nw = text_w(f, ns, mv.name);
        }
        ImVec2 np(v.dir > 0.0f ? v.x0 + nameX : v.x0 - nameX - nw, cy - ns * 0.55f);
        Rgb nc = i == 4 ? Rgb{255, 214, 110} : i < 2 ? Rgb{150, 220, 255} : kWhite;
        text(bv, f, ns, np, mv.name, col(bv, nc, 1.0f), col(bv, nc, 1.0f, 0.82f), 1.2f * v.u);
        float edge = (x0 + w - 12.0f) * v.u;
        ImVec2 qp(v.dir > 0.0f ? v.x0 + edge - needW : v.x0 - edge, cy - small * 0.55f);
        Rgb qc = ok ? Rgb{140, 255, 170} : Rgb{255, 120, 100};
        text(v, f, small, qp, need, col(v, qc, 0.95f), col(v, qc, 0.95f, 0.8f), 1.0f * v.u);
        y += rowH;
    }
}

// The members waiting, under the portrait: each one's face and health; the one a switch would bring in carries the
// switch gauge as a ring that closes, and lights up when the switch is allowed.
static void draw_team(const View &v, const FpSide &s, Anim &an, int side) {
    const Rgb accent = kSideCol[side];
    float y = 180.0f;
    ImFont *f = face(Ui_FontBold());
    if (s.count < 2) {
        return;
    }
    for (int i = 0; i < s.count && i < FP_MEMBERS; i++) {
        const FpMember &m = s.m[i];
        if (i == s.active || !m.present) {
            continue;
        }
        bool out = m.hp <= 0, next = i == s.next;
        ImVec2 c = pt_flat(v, 52.0f, y);
        float r = 27.0f * v.u, a = out ? 0.6f : 1.0f;
        float hpMax = m.hpMax > 0 ? (float)m.hpMax : 10000.0f, frac = clamp01((float)m.hp / hpMax);
        Rgb hc = frac > 0.5f ? kBar[2][0] : frac > 0.25f ? kBar[1][0] : kBar[0][0];
        v.dl->AddCircleFilled(c, r + 4.0f * v.u, col(v, kInk, 0.88f * a), 32);
        head(v, m, c, r, v.dir < 0.0f, out ? 1 : 0, a);
        v.dl->AddCircle(c, r + 1.5f * v.u, col(v, next ? mix(accent, kWhite, 0.2f) : Rgb{96, 108, 136}, a), 32, 2.4f * v.u);
        if (out) { // knocked out: greyed, crossed
            float d = r * 0.5f;
            v.dl->AddLine(ImVec2(c.x - d, c.y - d), ImVec2(c.x + d, c.y + d), col(v, kLost, 0.9f), 3.0f * v.u);
            v.dl->AddLine(ImVec2(c.x - d, c.y + d), ImVec2(c.x + d, c.y - d), col(v, kLost, 0.9f), 3.0f * v.u);
        }
        if (next) { // the switch gauge, from the top round
            float g = clamp01((float)s.switchGauge / 100000.0f);
            float rr = r + 6.5f * v.u;
            v.dl->AddCircle(c, rr, col(v, kInk, 0.8f), 40, 4.6f * v.u);
            if (g > 0.0f) {
                Rgb gc = mix(Rgb{120, 230, 255}, kWhite, an.full * pulse(0.7f));
                v.dl->PathArcTo(c, rr, -kPi * 0.5f, -kPi * 0.5f + g * 2.0f * kPi, 40);
                v.dl->PathStroke(col(v, gc, 1.0f), 0, 3.0f * v.u);
            }
            if (an.full > 0.02f) {
                v.dl->AddCircle(c, rr + 3.0f * v.u, col(v, {120, 230, 255}, an.full * 0.35f * (0.5f + 0.5f * pulse(0.7f))), 40, 5.0f * v.u);
            }
        }
        { // its health: a short bar beside the face, its bars left as a number
            float bx0 = 92.0f, bx1 = 176.0f, by = y - 5.0f;
            ImVec2 p0 = pt_flat(v, bx0, by), p1 = pt_flat(v, bx1, by + 10.0f), pf = pt_flat(v, bx0 + (bx1 - bx0) * frac, by + 10.0f);
            v.dl->AddRectFilled(ImVec2(fminf(p0.x, p1.x) - 2.0f * v.u, p0.y - 2.0f * v.u), ImVec2(fmaxf(p0.x, p1.x) + 2.0f * v.u, p1.y + 2.0f * v.u),
                                col(v, kInk, 0.85f * a), 3.0f * v.u);
            if (frac > 0.0f) {
                v.dl->AddRectFilled(ImVec2(fminf(p0.x, pf.x), p0.y), ImVec2(fmaxf(p0.x, pf.x), p1.y), col(v, hc, a), 2.0f * v.u);
            }
            if (!out && m.hpMax > 10000) {
                char num[8];
                float size = 19.0f * v.u;
                snprintf(num, sizeof(num), "x%d", (m.hp + 9999) / 10000);
                ImVec2 p = pt_flat(v, bx1 + 8.0f, by - 5.5f);
                if (v.dir < 0.0f) {
                    p.x -= text_w(f, size, num);
                }
                text(v, f, size, p, num, col(v, kWhite, 0.95f), col(v, kWhite, 0.95f), 1.0f * v.u);
            }
        }
        y += 66.0f;
    }
}

// The combo this side is landing: the hit count popping at each hit, the damage counting up, and the event
// messages (Counter!, MAX POWER!, ...). At the side's edge, clear of the fighters.
static const char *const kMsg[5] = {"COUNTER!", "MAX POWER!", "LOCK ON", "FIRST ATTACK!", "BOOST!"};
static const Rgb kMsgCol[5] = {{255, 120, 30}, {40, 160, 255}, {255, 170, 30}, {235, 70, 40}, {255, 140, 20}};

static void combo_step(Anim &an, const FpSide &s, float dt, bool on) {
    bool show = on && (s.hudHits >= 2 || s.hudDamage > 0);
    if (!an.seqSet) { // (what was there before this HUD first looked is not news)
        an.seqSet = 1;
        an.hitSeq = s.hitSeq;
        an.msgSeq = s.msgSeq;
        an.msgT = -1.0f;
    }
    if (s.hitSeq != an.hitSeq) {
        an.hitSeq = s.hitSeq;
        if (s.hudHits >= 2) {
            an.hits = s.hudHits;
            an.pop = 0.0f;
        }
    }
    if (s.hudHits < 2 && an.comboA <= 0.01f) {
        an.hits = 0;
    }
    if (s.hudDamage >= 0 && (float)s.hudDamage != an.dmgTo) {
        float shown = an.dmgDur > 0.0f ? mixf(an.dmgFrom, an.dmgTo, ease_out(an.dmgT / an.dmgDur)) : an.dmgTo;
        an.dmgFrom = an.comboA > 0.01f && (float)s.hudDamage > an.dmgTo ? shown : 0.0f; // (a new combo counts from 0)
        an.dmgTo = (float)s.hudDamage;
        an.dmgT = 0.0f;
        an.dmgDur = an.dmgTo - an.dmgFrom > 6000.0f ? fminf((an.dmgTo - an.dmgFrom) / 9500.0f, 1.5f) : 0.6f;
    }
    an.dmgT += dt;
    an.pop += dt;
    an.comboA = show ? fminf(1.0f, an.comboA + dt / 0.12f) : fmaxf(0.0f, an.comboA - dt / 0.3f);
    if (s.msgSeq != an.msgSeq) {
        an.msgSeq = s.msgSeq;
        an.msg = s.msg >= 0 && s.msg < 5 ? s.msg : 0;
        an.msgT = on ? 0.0f : -1.0f;
    }
    if (an.msgT >= 0.0f) {
        an.msgT += dt;
        if (an.msgT > 1.4f) {
            an.msgT = -1.0f;
        }
    }
}

static void draw_combo(const View &v0, const Anim &an) {
    View v = v0;
    ImFont *f = face(Ui_FontBold());
    const float y = 440.0f, x = 44.0f; // (below a team of five's chips)
    float lean = -0.22f * v.dir;
    if (an.comboA > 0.01f) {
        v.a = v0.a * an.comboA;
        float slide = (1.0f - ease_out(an.comboA)) * -40.0f;
        if (an.hits >= 2) {
            char num[8];
            float pop = 1.0f + 0.45f * (1.0f - ease_out(an.pop / 0.16f));
            float size = 92.0f * v.u * pop, word = 34.0f * v.u;
            snprintf(num, sizeof(num), "%d", an.hits > 999 ? 999 : an.hits);
            float w = text_w(f, size, num), ww = text_w(f, word, "HITS");
            ImVec2 p = pt_flat(v, x + slide, y - (size / v.u - 92.0f) * 0.7f);
            if (v.dir < 0.0f) {
                p.x -= w + 10.0f * v.u + ww;
            }
            text(v, f, size, p, num, col(v, {255, 250, 214}, 1.0f), col(v, {255, 150, 24}, 1.0f), 3.0f * v.u, lean);
            text(v, f, word, ImVec2(p.x + w + 10.0f * v.u, v.y0 + (y + 48.0f) * v.u), "HITS", col(v, kWhite, 1.0f), col(v, {255, 214, 120}, 1.0f),
                 2.0f * v.u, lean);
        }
        if (sSet.damage && an.dmgTo > 0.0f) {
            char num[24];
            float size = 38.0f * v.u, small = 19.0f * v.u;
            int dmg = (int)((an.dmgDur > 0.0f ? mixf(an.dmgFrom, an.dmgTo, ease_out(an.dmgT / an.dmgDur)) : an.dmgTo) + 0.5f);
            snprintf(num, sizeof(num), "%d", dmg > 999999 ? 999999 : dmg);
            float w = text_w(f, size, num), lw = text_w(f, small, "DAMAGE");
            ImVec2 p = pt_flat(v, x + 6.0f + slide, y + 100.0f);
            if (v.dir < 0.0f) {
                p.x -= w + 10.0f * v.u + lw;
            }
            text(v, f, size, p, num, col(v, kWhite, 1.0f), col(v, {255, 120, 90}, 1.0f), 2.0f * v.u, lean);
            text(v, f, small, ImVec2(p.x + w + 10.0f * v.u, p.y + 15.0f * v.u), "DAMAGE", col(v, {255, 190, 170}, 0.95f),
                 col(v, {255, 190, 170}, 0.95f), 1.4f * v.u, lean);
        }
    }
    if (an.msgT >= 0.0f) { // in 0.1 s, held, out 0.1 s (as the game's)
        float t = an.msgT, a = t < 0.1f ? t / 0.1f : t > 1.3f ? (1.4f - t) / 0.1f : 1.0f;
        float slide = (1.0f - ease_out(t / 0.14f)) * -60.0f;
        float size = 34.0f * v.u;
        const char *m = kMsg[an.msg];
        float w = text_w(f, size, m) / v.u;
        float by = y + 156.0f, bx = 30.0f + slide;
        v.a = v0.a * clamp01(a);
        View flat = v; // (the plate leans from its own foot)
        flat.y0 += (by + 44.0f - kSlantY) * v.u;
        slab(flat, bx - 4.0f, bx + w + 44.0f, kSlantY - 46.0f, kSlantY + 2.0f, col(v, kInk, 0.8f), col(v, kInk, 0.8f));
        slab(flat, bx, bx + w + 40.0f, kSlantY - 42.0f, kSlantY - 2.0f, col(v, kMsgCol[an.msg], 0.95f, 1.15f), col(v, kMsgCol[an.msg], 0.95f, 0.62f));
        ImVec2 p = pt_flat(v, bx + 24.0f, by + 5.0f);
        if (v.dir < 0.0f) {
            p.x -= w * v.u;
        }
        text(v, f, size, p, m, col(v, kWhite, 1.0f), col(v, {255, 244, 214}, 1.0f), 2.0f * v.u, lean);
    }
}

// The timer: a plate at the top middle; no limit shows the sign for it.
static void draw_timer(ImDrawList *dl, const FpState &st, float cx, float y0, float u, float a) {
    View v = {dl, cx, y0, 1.0f, u, a};
    ImFont *f = face(Ui_FontBold());
    char num[12];
    bool none = st.timerClock != 0 && st.timer < 0;
    bool hurry = st.timerClock != 0 && st.timer >= 0 && st.timer <= 10;
    Rgb edge = hurry ? mix(Rgb{120, 136, 170}, kLost, pulse(0.5f)) : Rgb{120, 136, 170};
    const float hw = 62.0f, top = 22.0f, bot = 74.0f, cut = 18.0f;
    ImVec2 p[6] = {ImVec2(cx - (hw - cut) * u, y0 + top * u), ImVec2(cx + (hw - cut) * u, y0 + top * u), ImVec2(cx + hw * u, y0 + (top + bot) * 0.5f * u),
                   ImVec2(cx + (hw - cut) * u, y0 + bot * u), ImVec2(cx - (hw - cut) * u, y0 + bot * u), ImVec2(cx - hw * u, y0 + (top + bot) * 0.5f * u)};
    dl->AddConvexPolyFilled(p, 6, col(v, kInk, 0.84f));
    dl->AddPolyline(p, 6, col(v, edge, 0.9f), ImDrawFlags_Closed, 2.0f * u);
    if (none) { // the endless sign, drawn: the fonts need not have it
        ImVec2 q[33];
        for (int i = 0; i <= 32; i++) {
            float t = i * 2.0f * kPi / 32.0f, d = 1.0f + sinf(t) * sinf(t);
            q[i] = ImVec2(cx + 26.0f * u * cosf(t) / d, y0 + (top + bot) * 0.5f * u + 30.0f * u * sinf(t) * cosf(t) / d);
        }
        dl->AddPolyline(q, 33, col(v, kInk, 0.9f), 0, 8.0f * u);
        dl->AddPolyline(q, 33, col(v, kWhite, 1.0f), 0, 4.2f * u);
        return;
    }
    snprintf(num, sizeof(num), "%d", st.timer < 0 ? 0 : st.timer > 99999 ? 99999 : st.timer);
    float size = (strlen(num) > 3 ? 32.0f : 42.0f) * u;
    ImVec2 ts = f->CalcTextSizeA(size, FLT_MAX, 0.0f, num);
    text(v, f, size, ImVec2(cx - ts.x * 0.5f, y0 + (top + bot) * 0.5f * u - ts.y * 0.5f), num, col(v, hurry ? Rgb{255, 214, 200} : kWhite, 1.0f),
         col(v, hurry ? kLost : Rgb{200, 214, 236}, 1.0f), 2.0f * u);
}

// The fight as the game's thread last noted it; read once a frame, in HudModern_Active.
static FpState sSt;

bool HudModern_Active(void) {
    settings_load();
    if (sSet.look != 0 || !PortPanels_Read(&sSt) || !sSt.battle) { // Original: the game's HUD, untouched
        sHide = 0;
        sGaugeA = sTimerA = 0.0f;
        memset(sAnim, 0, sizeof(sAnim));
        return false;
    }
    return true;
}

void HudModern_Draw(void) {
    const FpState &st = sSt;
    // A script mod that stands in for a piece keeps it: its gauges come without a team panel, so the game's stays.
    int mods = Mods_HudHidden();
    int mine = 15 & ~((mods & 1) != 0 ? 9 : 0) & ~(mods & 6);
    sDrawnAt = SDL_GetTicks();
    sHide = mine;
    float dt = ImGui::GetIO().DeltaTime;
    dt = dt < 0.0f ? 0.0f : dt > 0.1f ? 0.1f : dt;
    sTime += dt;
    if (gUiPresentW <= 0 || gUiPresentH <= 0) {
        return;
    }
    int pieces = GsDraw_HudPieces();
    bool fight = st.mode != 7 && st.replay != 1;
    bool gauges = fight && (st.hudFlags & 1) != 0 && (pieces & 1) != 0 && (mine & 1) != 0;
    bool timer = fight && (st.hudFlags & 2) != 0 && (pieces & 2) != 0 && (mine & 2) != 0 && sSet.timer && st.timerSet;
    bool combo = fight && (st.hudFlags & 8) != 0 && (mine & 4) != 0;
    sGaugeA = gauges ? fminf(1.0f, sGaugeA + dt / 0.25f) : fmaxf(0.0f, sGaugeA - dt / 0.2f);
    sTimerA = timer ? fminf(1.0f, sTimerA + dt / 0.25f) : fmaxf(0.0f, sTimerA - dt / 0.2f);

    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    float px = (float)gUiPresentX, py = (float)gUiPresentY, pw = (float)gUiPresentW, ph = (float)gUiPresentH;
    // (in a small window it keeps a size that can still be read: the names and numbers would be a few pixels high)
    float u = fmaxf(ph / 1080.0f, 0.46f) * (float)sSet.size / 100.0f;
    // "HUD Style: In the middle": about the middle 4:3 of a wider picture
    float rw = Port_HudLayout() == 2 && pw > ph * 4.0f / 3.0f ? ph * 4.0f / 3.0f : pw;
    float rx = px + (pw - rw) * 0.5f;
    // The health bars: a fixed length, or up to the timer with "long health bars"; never into the timer's plate.
    float room = rw * 0.5f / u - kBarX - 96.0f;
    sBarLen = Port_HudLayout() == 1 ? room : fminf(room, 560.0f);
    sBarLen = sBarLen < 180.0f ? 180.0f : sBarLen;
    // the game's own fades cover its HUD: this one goes with them (the pause menu leaves the HUD as it is, as the game's)
    float all = (float)sSet.opacity / 100.0f * (1.0f - (float)st.cover / 255.0f);
    float whole = clamp01((float)st.hudSlide / 1000.0f); // (a story battle's event slides the whole HUD away)
    int swap = Port_HudSwap() != 0;

    dl->PushClipRect(ImVec2(px, py), ImVec2(px + pw, py + ph), true);
    for (int place = 0; place < 2; place++) {
        int side = place ^ swap;
        const FpSide &s = st.side[side];
        Anim &an = sAnim[side];
        if (!s.found || s.active < 0 || s.active >= FP_MEMBERS) {
            continue;
        }
        anim_step(an, s, dt);
        combo_step(an, s, dt, combo && sGaugeA > 0.5f);
        float away = clamp01((float)s.slide / 1000.0f); // (this side's panel leaves while its members switch)
        View v = {dl, place == 0 ? rx : rx + rw, py, place == 0 ? 1.0f : -1.0f, u, 0.0f};
        if (sGaugeA > 0.0f) {
            View g = v;
            g.a = sGaugeA * all * (1.0f - whole) * (1.0f - away);
            g.x0 -= g.dir * away * 260.0f * u;
            g.y0 -= (whole * 150.0f + (1.0f - ease_out(sGaugeA)) * 26.0f) * u;
            if (g.a > 0.004f) {
                draw_gauges(g, s, an, side);
                if ((mine & 8) != 0) {
                    draw_team(g, s, an, side);
                }
            }
            v.a = sGaugeA * all;
            if (v.a > 0.004f) {
                draw_combo(v, an);
                View k = v;
                k.a = v.a * (1.0f - whole);
                draw_skills(k, s, an, dt, ph / u);
            }
        }
    }
    if (sTimerA > 0.0f) {
        float a = sTimerA * all * (1.0f - whole);
        if (a > 0.004f) {
            draw_timer(dl, st, px + pw * 0.5f, py - (whole * 150.0f + (1.0f - ease_out(sTimerA)) * 26.0f) * u, u, a);
        }
    }
    dl->PopClipRect();
}

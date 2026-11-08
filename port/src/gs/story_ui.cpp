// The Story Creator (main menu item 14): sagas of the player's own, made and played inside the game.
//
// A saga is a list of episodes. An episode has an opening scene, a battle (or none), and a scene after a win and one
// after a loss; what comes next depends on how the battle went (another episode, the same again, the end). A scene is
// the episode's two fighters standing on its stage, talking: each line has a speaker, a text of the player's own
// typed in the game, a voice (one of the speaker's spoken lines, or another character's, or a random one), a camera
// shot, poses and effects. Sagas are JSON files in the `sagas` folder next to the game (BT3_SAGAS another folder):
// docs/port/sagas.md has the format; a sample saga is built in (story_sample.h).
//
// This file is the screens (drawn with Dear ImGui into the background list on the render thread, laid out like the
// Custom Character screen's: gs/creator_ui.cpp), the saga files, what is said during an episode (the text box, the
// title card, the box of a line said in the fight, drawn over the battle's picture), and the making of an episode's
// battle script: the GSC file a battle of the story mode plays (src/battle/port_story_btl.c reads it, the game thread
// runs it). The game's half of the screens is src/menu/port_story.c.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <algorithm>
#include <functional>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "ui.h"
extern "C" {
#include "../../include/plat_charas.h"
#include "../../include/plat_stages.h"
#include "../../include/plat_songs.h"
}
#include "story_sample.h"

SDL_GPUTexture *Ui_Texture(uint8_t *pix, uint32_t w, uint32_t h); // ui.cpp (takes pix)
void Ui_TextureFree(SDL_GPUTexture *tex);
ImFont *Ui_FontBold(void);
extern "C" {
int Port_FilePath(int ptid, int flid, const char *fname, char *out, int size); // plat_file.c
int Port_NetSession(void);
}

namespace {

// =============================================================================================== numbers shared with the game
// (src/battle/port_story_btl.c and src/menu/port_story.c have the same)
enum { PL_SIDE, PL_MEMBER, PL_CHARA, PL_VOICE, PL_LIP, PL_CAMERA, PL_MOVE, PL_MOVE_T, PL_POSE, PL_POSE_LOOP, PL_LPOSE,
       PL_LPOSE_LOOP, PL_FX, PL_LEN, PL_WAIT, PL_VCHARA, PL_COUNT };
enum { CAM_KEEP, CAM_AUTO, CAM_CLOSE, CAM_MEDIUM, CAM_SHOULDER, CAM_TWO, CAM_WIDE, CAM_LOW, CAM_HIGH, CAM_LISTENER, CAM_COUNT };
enum { MOVE_CUT, MOVE_PUSH, MOVE_ORBIT, MOVE_PULL, MOVE_FROM_WIDE, MOVE_RISE, MOVE_COUNT };
enum { FX_SHAKE = 0x1, FX_FLASH = 0x2, FX_AURA_ON = 0x4, FX_AURA_OFF = 0x8, FX_CHARGE_ON = 0x10, FX_CHARGE_OFF = 0x20,
       FX_BURST = 0x40, FX_FADE_OUT = 0x80, FX_FADE_IN = 0x100, FX_BIG_SHAKE = 0x200, FX_HIDE = 0x400, FX_SHOW = 0x800 };
enum { SHOW_SAY = 1, SHOW_HIDE, SHOW_TITLE, SHOW_HUD, SHOW_LETTERBOX, SHOW_FLASH, SHOW_FADE, SHOW_TIMER, SHOW_END };
enum { PB_WIN, PB_SURVIVE, PB_TIME, PB_TIME_UP, PB_EVENTS, PB_COUNT = 8 };
enum { PE_WHEN, PE_VALUE, PE_LINE, PE_COUNT = 4 };
enum { WHEN_START, WHEN_ENEMY_HP, WHEN_PLAYER_HP, WHEN_ENEMY_FORM, WHEN_PLAYER_FORM, WHEN_TIME, WHEN_ENEMY_MEMBER,
       WHEN_PLAYER_MEMBER, WHEN_COUNT };
enum { PG_LEAVE, PG_SOUND, PG_KEY, PG_CHARA, PG_COSTUME, PG_TURN, PG_BGM, PG_VOICE };

const int kDiscCharas = 161;
const int kVoiceUs = 0xCC32, kVoiceJp = 0x8D4E; // a disc character's spoken lines: base + chara * 100 + line (100 each)
const int kPt2 = 0xD48;                          // the second archive's first file id
const int kFaceFile = 0x2F9;                     // the large portraits: + chara (first archive)
// an added character's files are in the second archive by its place n (Port_CharaFileIndex, port/include/plat_charas.h)
const int kCacheCharas = 161 + PORT_CHARA_MAX; // the caches below: a disc character by its id, an added one at 161 + n
int cache_key(int chara) {
    if (PORT_CHARA_ADDED(chara)) { int n = PORT_CHARA_IDX(chara); return n < PORT_CHARA_MAX ? 161 + n : -1; }
    return chara >= 0 && chara < 161 ? chara : -1;
}

// =============================================================================================== JSON
struct J {
    enum T { NUL, BOOL, NUM, STR, ARR, OBJ };
    T t = NUL;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<J> a;
    std::vector<std::string> k;

    static J str(const std::string &v) { J j; j.t = STR; j.s = v; return j; }
    static J num(double v) { J j; j.t = NUM; j.n = v; return j; }
    static J boolean(bool v) { J j; j.t = BOOL; j.b = v; return j; }
    static J arr() { J j; j.t = ARR; return j; }
    static J obj() { J j; j.t = OBJ; return j; }
    bool nul() const { return t == NUL; }
    bool isObj() const { return t == OBJ; }
    bool isArr() const { return t == ARR; }
    bool isStr() const { return t == STR; }
    bool isNum() const { return t == NUM; }
    size_t size() const { return t == ARR || t == OBJ ? a.size() : 0; }
    const J &operator[](size_t i) const { static J none; return (t == ARR || t == OBJ) && i < a.size() ? a[i] : none; }
    const J &operator[](int i) const { return (*this)[(size_t)(i < 0 ? 0 : i)]; }
    const J &operator[](const char *key) const {
        static J none;
        if (t == OBJ) {
            for (size_t i = 0; i < k.size(); i++) {
                if (k[i] == key) { return a[i]; }
            }
        }
        return none;
    }
    J *find(const char *key) {
        if (t == OBJ) {
            for (size_t i = 0; i < k.size(); i++) {
                if (k[i] == key) { return &a[i]; }
            }
        }
        return NULL;
    }
    bool has(const char *key) const { return !(*this)[key].nul(); }
    J &set(const char *key, const J &v) {
        if (t != OBJ) { *this = obj(); }
        J *f = find(key);
        if (f != NULL) { *f = v; return *f; }
        k.push_back(key);
        a.push_back(v);
        return a.back();
    }
    J &at(const char *key) { // the member, made an object if it is not one
        J *f = find(key);
        if (f == NULL || !f->isObj()) { return set(key, obj()); }
        return *f;
    }
    J &list(const char *key) { // the member, made a list if it is not one
        J *f = find(key);
        if (f == NULL || !f->isArr()) { return set(key, arr()); }
        return *f;
    }
    void erase(const char *key) {
        for (size_t i = 0; t == OBJ && i < k.size(); i++) {
            if (k[i] == key) { k.erase(k.begin() + i); a.erase(a.begin() + i); return; }
        }
    }
    void push(const J &v) { if (t != ARR) { *this = arr(); } a.push_back(v); }
    std::string text(const char *def = "") const { return t == STR ? s : t == NUM ? std::to_string((long long)n) : std::string(def); }
    double number(double def = 0) const { return t == NUM ? n : t == BOOL ? (b ? 1 : 0) : def; }
    bool truthy(bool def = false) const { return t == BOOL ? b : t == NUM ? n != 0 : t == STR ? !s.empty() : t == NUL ? def : true; }
};

void js_ws(const char *&p) {
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') { p++; }
        if (p[0] == '/' && p[1] == '/') { while (*p && *p != '\n') { p++; } continue; } // (comments are let through)
        break;
    }
}
void utf8_put(std::string &o, unsigned c) {
    if (c < 0x80) { o += (char)c; }
    else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
    else { o += (char)(0xF0 | (c >> 18)); o += (char)(0x80 | ((c >> 12) & 0x3F)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
}
bool js_str(const char *&p, std::string &o) {
    if (*p != '"') { return false; }
    p++;
    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            switch (*p) {
            case 'n': o += '\n'; p++; break;
            case 't': o += '\t'; p++; break;
            case 'r': p++; break;
            case 'b': case 'f': p++; break;
            case 'u': {
                unsigned c = (unsigned)strtoul(std::string(p + 1, 4).c_str(), NULL, 16);
                p += 5;
                if (c >= 0xD800 && c < 0xDC00 && p[0] == '\\' && p[1] == 'u') {
                    unsigned lo = (unsigned)strtoul(std::string(p + 2, 4).c_str(), NULL, 16);
                    p += 6;
                    c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                }
                utf8_put(o, c);
                break;
            }
            case '\0': return false;
            default: o += *p++; break;
            }
        } else {
            o += *p++;
        }
    }
    if (*p != '"') { return false; }
    p++;
    return true;
}
bool js_parse(const char *&p, J &v, int depth = 0) {
    js_ws(p);
    if (depth > 64) { return false; }
    if (*p == '{') {
        v = J::obj();
        p++;
        js_ws(p);
        if (*p == '}') { p++; return true; }
        for (;;) {
            js_ws(p);
            std::string key;
            if (!js_str(p, key)) { return false; }
            js_ws(p);
            if (*p != ':') { return false; }
            p++;
            J item;
            if (!js_parse(p, item, depth + 1)) { return false; }
            v.k.push_back(key);
            v.a.push_back(item);
            js_ws(p);
            if (*p == ',') { p++; js_ws(p); if (*p == '}') { p++; return true; } continue; }
            if (*p == '}') { p++; return true; }
            return false;
        }
    }
    if (*p == '[') {
        v = J::arr();
        p++;
        js_ws(p);
        if (*p == ']') { p++; return true; }
        for (;;) {
            J item;
            if (!js_parse(p, item, depth + 1)) { return false; }
            v.a.push_back(item);
            js_ws(p);
            if (*p == ',') { p++; js_ws(p); if (*p == ']') { p++; return true; } continue; }
            if (*p == ']') { p++; return true; }
            return false;
        }
    }
    if (*p == '"') { v = J::str(""); return js_str(p, v.s); }
    if (!strncmp(p, "true", 4)) { v = J::boolean(true); p += 4; return true; }
    if (!strncmp(p, "false", 5)) { v = J::boolean(false); p += 5; return true; }
    if (!strncmp(p, "null", 4)) { v = J(); p += 4; return true; }
    char *end = NULL;
    double d = strtod(p, &end);
    if (end == p) { return false; }
    v = J::num(d);
    p = end;
    return true;
}
bool js_read(const std::string &text, J &v) {
    const char *p = text.c_str();
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) { p += 3; }
    return js_parse(p, v);
}
void js_quote(const std::string &s, std::string &o) {
    o += '"';
    for (unsigned char c : s) {
        if (c == '"') { o += "\\\""; }
        else if (c == '\\') { o += "\\\\"; }
        else if (c == '\n') { o += "\\n"; }
        else if (c == '\t') { o += "\\t"; }
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
        else { o += (char)c; }
    }
    o += '"';
}
// written for people to read and share: objects one key a line, short lists of numbers on one line
bool js_flat(const J &v) {
    if (v.isArr()) {
        if (v.size() > 8) { return false; }
        for (const J &e : v.a) { if (e.isArr() || e.isObj()) { return false; } }
        return true;
    }
    if (v.isObj()) {
        if (v.size() > 3) { return false; }
        size_t len = 0;
        for (size_t i = 0; i < v.size(); i++) {
            if (v.a[i].isArr() || v.a[i].isObj()) { return false; }
            len += v.k[i].size() + v.a[i].s.size() + 8;
        }
        return len < 60;
    }
    return true;
}
void js_write(const J &v, std::string &o, int ind = 0) {
    switch (v.t) {
    case J::NUL: o += "null"; break;
    case J::BOOL: o += v.b ? "true" : "false"; break;
    case J::NUM: {
        char b[40];
        if (v.n == (double)(long long)v.n && fabs(v.n) < 1e15) { snprintf(b, sizeof(b), "%lld", (long long)v.n); }
        else { snprintf(b, sizeof(b), "%.6g", v.n); }
        o += b;
        break;
    }
    case J::STR: js_quote(v.s, o); break;
    case J::ARR:
    case J::OBJ: {
        bool obj = v.t == J::OBJ;
        if (v.a.empty()) { o += obj ? "{}" : "[]"; break; }
        bool flat = js_flat(v);
        o += obj ? '{' : '[';
        for (size_t i = 0; i < v.a.size(); i++) {
            if (flat) { if (i) { o += ", "; } }
            else { o += i ? ",\n" : "\n"; o.append((size_t)(ind + 2), ' '); }
            if (obj) { js_quote(v.k[i], o); o += ": "; }
            js_write(v.a[i], o, ind + 2);
        }
        if (!flat) { o += '\n'; o.append((size_t)ind, ' '); }
        o += obj ? '}' : ']';
        break;
    }
    }
}

// =============================================================================================== what there is to choose
const char *const kCharaNames[kDiscCharas] = { // by id, in English (the order of the character table)
    "Goku (Early)", "Goku (Med)", "Goku (Med) Super Saiyan", "Goku (End)", "Goku (End) Super Saiyan", "Goku (End) Super Saiyan 2",
    "Goku (End) Super Saiyan 3", "Goku (GT)", "Goku (GT) Super Saiyan", "Goku (GT) Super Saiyan 3", "Goku (GT) Super Saiyan 4", "Kid Goku",
    "Great Ape", "Kid Gohan", "Teen Gohan", "Teen Gohan Super Saiyan", "Teen Gohan Super Saiyan 2", "Gohan",
    "Gohan Super Saiyan", "Gohan Super Saiyan 2", "Great Saiyaman", "Ultimate Gohan", "Piccolo (Early)", "Piccolo (End)",
    "Nail", "Krillin", "Yamcha", "Tien", "Chiaotzu", "Vegeta (Scouter)",
    "Vegeta (Scouter) Great Ape", "Vegeta", "Vegeta Super Saiyan", "Super Vegeta", "Vegeta (2nd Form)", "Vegeta (2nd Form) Super Saiyan",
    "Vegeta (2nd Form) Super Saiyan 2", "Majin Vegeta", "Vegeta (2nd Form) Super Saiyan 4", "Trunks (Sword)", "Trunks (Sword) Super Saiyan", "Trunks",
    "Trunks Super Saiyan", "Super Trunks", "Kid Trunks", "Kid Trunks Super Saiyan", "Goten", "Goten Super Saiyan",
    "Gotenks", "Gotenks Super Saiyan", "Gotenks Super Saiyan 3", "Vegito", "Super Vegito", "Super Gogeta",
    "Gogeta Super Saiyan 4", "Mr Satan", "Videl", "Great Saiyaman 2", "Supreme Kai", "Kibito Kai",
    "Yajirobe", "Paikuhan", "Tapion", "Pan", "Uub", "Majuub",
    "Master Roshi", "Master Roshi Full Power", "Grandpa Gohan", "Namu", "Hatchan", "King Vegeta",
    "King Vegeta Great Ape", "Bardock", "Bardock Great Ape", "Celipa", "Celipa Great Ape", "Raditz",
    "Raditz Great Ape", "Saibamen", "Nappa", "Nappa Great Ape", "Zarbon", "Zarbon Transformed",
    "Dodoria", "Cui", "Ginyu", "Recoome", "Burter", "Jeice",
    "Guldo", "Frieza 1st Form", "Frieza 2nd Form", "Frieza 3rd Form", "Frieza Final Form", "Frieza 100% Power",
    "Mecha Frieza", "King Cold", "Appule", "Frieza Soldier", "Android 16", "Android 17",
    "Android 18", "Android 19", "Android 20", "Cell 1st Form", "Cell 2nd Form", "Perfect Cell",
    "Super Perfect Cell", "Cell Jr", "Babidi", "Dabura", "Majin Buu", "Evil Buu",
    "Super Buu", "Super Buu Gotenks Absorbed", "Super Buu Gohan Absorbed", "Kid Buu", "Garlic Jr", "Super Garlic Jr",
    "Wheelo", "Turles", "Turles Great Ape", "Slug", "Slug Giant", "Sauza",
    "Cooler", "Cooler Final Form", "Metal Cooler", "Android 13", "Android 13 Fusion", "Broly",
    "Broly Super Saiyan", "Broly Legendary Super Saiyan", "Zangya", "Bojack", "Bojack 100% Power", "Janemba",
    "Super Janemba", "Hildegarn", "Baby Vegeta", "Super Baby 1", "Super Baby 2", "Super Baby Great Ape",
    "Super 17", "Nuova Shenron", "Shin Shenron", "Omega Shenron", "Taopaipai", "Cyborg Tao",
    "General Blue", "Akuman", "Pilaf Machine", "Pilaf Machine Fusion", "Tambourine", "Demon King Piccolo",
    "Arale", "Chichi", "Spopovich", "Future Gohan", "Future Gohan Super Saiyan",
};
// The point of each disc portrait the face is at (gs/fight_panels_ui.cpp has the same table: set by hand)
const short kFace[kDiscCharas][2] = {
    {115,170}, {130,142}, {138,162}, {123,152}, {139,155}, {126,156}, {135,142}, {124,160}, {134,154}, {145,145},
    {121,145}, {127,158}, {125,170}, {130,165}, {123,148}, {125,150}, {138,141}, {125,136}, {145,160}, {134,150},
    {131,162}, {110,165}, {128,160}, {128,160}, {128,160}, {127,93}, {115,155}, {125,106}, {100,150}, {122,136},
    {115,160}, {124,138}, {126,101}, {115,160}, {121,139}, {106,139}, {120,170}, {129,130}, {113,136}, {122,138},
    {133,128}, {116,136}, {114,143}, {119,113}, {127,146}, {128,147}, {127,158}, {130,147}, {126,145}, {110,165},
    {132,147}, {124,144}, {131,150}, {95,155}, {124,152}, {125,151}, {130,152}, {115,140}, {130,165}, {130,155},
    {100,160}, {100,140}, {90,160}, {105,150}, {133,138}, {135,115}, {128,102}, {138,121}, {115,140}, {100,150},
    {135,118}, {122,130}, {115,165}, {125,176}, {140,175}, {113,151}, {125,180}, {119,122}, {125,185}, {128,160},
    {129,104}, {135,160}, {115,170}, {120,175}, {130,170}, {130,165}, {100,165}, {115,160}, {120,160}, {120,165},
    {130,165}, {110,160}, {90,170}, {110,165}, {115,160}, {130,155}, {75,165}, {110,165}, {140,160}, {120,170},
    {122,132}, {129,147}, {130,89}, {150,160}, {110,155}, {120,165}, {115,160}, {110,160}, {115,175}, {100,170},
    {110,160}, {125,160}, {120,170}, {110,160}, {120,190}, {115,165}, {120,195}, {115,190}, {115,165}, {110,160},
    {90,170}, {100,154}, {128,190}, {140,165}, {155,170}, {90,145}, {120,165}, {110,160}, {130,165}, {130,165},
    {110,170}, {144,157}, {125,175}, {128,139}, {140,165}, {115,180}, {125,175}, {110,170}, {130,175}, {130,165},
    {140,135}, {126,139}, {107,148}, {110,175}, {130,134}, {115,170}, {110,170}, {115,170}, {118,142}, {110,170},
    {115,165}, {130,175}, {130,160}, {110,170}, {110,160}, {130,165}, {120,160}, {110,170}, {133,118}, {128,140},
    {128,151},
};
const char *const kStageNames[35] = { // by stage id (port/src/a11y_game.c has the same)
    "Wasteland - Noon", "Rocky Area - Noon", "Planet Namek", "Dying Namek", "World Tournament Stage - Noon",
    "Kami's Lookout", "Cell Games Arena - Evening", "Supreme Kai's World", "Hyperbolic Time Chamber",
    "City Ruins - Noon", "Mountain Road - Noon", "Islands", "Kame House", "Planet - Night", "Glacier",
    "Ruined Earth", "Outer Space", "Penguin Village", "Hell", "Desert - Noon", "King's Castle", "Muscle Tower",
    "Mount Paozu", "Wasteland - Evening", "Wasteland - Night", "Rocky Area - Evening", "Rocky Area - Night",
    "World Tournament Stage - Evening", "Cell Game - Noon", "City Ruins - Evening", "City Ruins - Night",
    "Desert - Evening", "Desert - Night", "Mountain Road - Evening", "Planet - Evening",
};
const char *const kSongNames[20] = { // by music-list value 0x00..0x13 (gs/namefont.c)
    "The Meteor", "Vital Burner", "Innocent World", "After The Fire", "Sweet Vibration", "Survive", "Heat Capacity",
    "Overture", "Shine", "Power Scale", "Edge of Spirit", "Caution !", "Menace", "Hot Soul", "High and Scream",
    "Shootout In Meteor", "Dynamite Battle", "Burnin' Up", "Wild Rush", "Evolution",
};
struct Pose { const char *name; int motion; bool loop; };
// The scripted moves (motions of the fighters' own animation lists) the game's story scenes use. 0..54 are the
// stances every fighter has; 384.. are each character's own story gestures (what Dragon History's scenes play: they
// differ from one character to the next), looked at on Goku (End).
const Pose kPoses[] = {
    {"As it is", -1, false}, {"Battle stance", 0, true}, {"Stance, turned", 1, true}, {"Lean in", 38, true},
    {"Crouch", 39, true}, {"Brace", 52, true}, {"Lean forward", 53, true}, {"Arms open", 54, false},
    {"Story gesture 1", 384, false}, {"Story gesture 2", 385, false}, {"Story gesture 3", 386, false},
    {"Story gesture 4", 387, false}, {"Story gesture 5", 388, false}, {"Story gesture 6", 389, false},
    {"Story gesture 7", 406, false}, {"Story gesture 8", 413, false},
};
const int kPoseCount = (int)(sizeof(kPoses) / sizeof(kPoses[0]));
const char *const kCamNames[CAM_COUNT] = {"Keep the last", "Automatic", "Close-up", "Medium shot", "Over the shoulder",
                                          "Both of them", "Wide", "Low angle", "High angle", "On the listener"};
const char *const kCamKeys[CAM_COUNT] = {"keep", "auto", "close", "medium", "shoulder", "two", "wide", "low", "high", "listener"};
const char *const kMoveNames[MOVE_COUNT] = {"Cut", "Push in", "Orbit", "Pull back", "From the wide view", "Rise"};
const char *const kMoveKeys[MOVE_COUNT] = {"cut", "push", "orbit", "pull", "from_wide", "rise"};
struct Fx { const char *key, *name; int bit; };
const Fx kFx[] = {
    {"shake", "Camera shake", FX_SHAKE}, {"big_shake", "Big shake", FX_BIG_SHAKE}, {"flash", "White flash", FX_FLASH},
    {"aura_on", "Aura on", FX_AURA_ON}, {"aura_off", "Aura off", FX_AURA_OFF}, {"charge_on", "Charging ki", FX_CHARGE_ON},
    {"charge_off", "Stop charging", FX_CHARGE_OFF}, {"burst", "Ki burst", FX_BURST}, {"fade_in", "Fade in from black", FX_FADE_IN},
    {"hide", "Speaker leaves", FX_HIDE}, {"show", "Speaker appears", FX_SHOW},
};
const int kFxCount = (int)(sizeof(kFx) / sizeof(kFx[0]));
const char *const kWhenNames[WHEN_COUNT] = {"At the start", "Opponent's health below", "Your health below", "Opponent transforms",
                                            "You transform", "After seconds", "Opponent switches fighter", "You switch fighter"};
const char *const kWhenKeys[WHEN_COUNT] = {"start", "enemy_health", "player_health", "enemy_transform", "player_transform",
                                           "time", "enemy_switch", "player_switch"};
const char *const kCpuNames[5] = {"Easy", "Normal", "Hard", "Very hard", "Super"};
const int kCpuLevels[5] = {0, 6, 13, 21, 29}; // CpuLevel_FromSetting (src/battle/view_b_c.c)
const char *const kDistNames[4] = {"Where the stage starts them", "Close", "Talking distance", "Far apart"};

struct Chara { int id; std::string name; bool added; int costumes; };
std::vector<Chara> sCharas;
int sCharasEpoch = -1;
void charas_read(void) {
    if (sCharasEpoch == PortCharas_Epoch() && !sCharas.empty()) { return; }
    sCharasEpoch = PortCharas_Epoch();
    sCharas.clear();
    for (int i = 0; i < kDiscCharas; i++) { sCharas.push_back({i, kCharaNames[i], false, 4}); }
    int n = Port_CharaCount();
    for (int i = 0; i < n; i++) {
        int id = PORT_CHARA_ID(i);
        const char *name = Port_CharaName(i);
        int c = Port_CharaCostumes(id);
        sCharas.push_back({id, name != NULL && name[0] ? name : ("Added " + std::to_string(i + 1)), true, c > 0 ? c : 1});
    }
}
const Chara *chara_by_id(int id) {
    charas_read();
    for (const Chara &c : sCharas) { if (c.id == id) { return &c; } }
    return NULL;
}
// a character as a saga names it: a disc id, or "added:<folder>" (an added character's number depends on what is installed)
int chara_of(const J &v) {
    if (v.isNum()) { return (int)v.n; }
    std::string s = v.text();
    charas_read();
    if (s.compare(0, 6, "added:") == 0) { s = s.substr(6); }
    for (const Chara &c : sCharas) { if (c.name == s) { return c.id; } }
    return -1;
}
J chara_ref(int id) {
    const Chara *c = chara_by_id(id);
    if (c != NULL && c->added) { return J::str("added:" + c->name); }
    return J::num(id);
}
std::string chara_name(int id) {
    const Chara *c = chara_by_id(id);
    return c != NULL ? c->name : id < 0 ? std::string("(nobody)") : "Character " + std::to_string(id);
}
std::string short_name(int id) { // "Goku (End) Super Saiyan 2" -> "Goku": the name in a text box
    std::string n = chara_name(id);
    static const char *const cut[] = {" (", " Super", " Great Ape", " 1st", " 2nd", " 3rd", " Final", " 100%", " Legendary", " Full", " Transformed"};
    size_t at = n.size();
    for (const char *c : cut) {
        size_t p = n.find(c);
        if (p != std::string::npos && p > 0 && p < at) { at = p; }
    }
    return n.substr(0, at);
}
int stage_of(const J &v) {
    if (v.isNum()) { return (int)v.n; }
    std::string s = v.text();
    if (s.compare(0, 6, "added:") == 0) { s = s.substr(6); }
    for (int i = 0; i < PortStages_Count(); i++) { if (s == PortStages_Name(i)) { return PORT_STAGE_FIRST + i; } }
    for (int i = 0; i < 35; i++) { if (s == kStageNames[i]) { return i; } }
    return 0;
}
std::string stage_name(int id) {
    if (id >= 0 && id < 35) { return kStageNames[id]; }
    if (id >= PORT_STAGE_FIRST && id - PORT_STAGE_FIRST < PortStages_Count()) { return PortStages_Name(id - PORT_STAGE_FIRST); }
    return "Stage " + std::to_string(id);
}
J stage_ref(int id) { return id >= PORT_STAGE_FIRST ? J::str("added:" + stage_name(id)) : J::num(id); }
std::vector<int> stage_list(void) {
    std::vector<int> l;
    for (int i = 0; i < 35; i++) { l.push_back(i); }
    for (int i = 0; i < PortStages_Count(); i++) { l.push_back(PORT_STAGE_FIRST + i); }
    return l;
}
std::string song_name(int v) {
    if (v >= 0 && v < 20) { return kSongNames[v]; }
    if (v == 24) { return "Random"; }
    for (int i = 0; i < gPortSongCount && i < PORT_SONG_MAX; i++) { if (gPortSongOffsets[i] == v) { return gPortSongNames[i]; } }
    return "Track " + std::to_string(v);
}
std::vector<int> song_list(void) {
    std::vector<int> l;
    for (int i = 0; i < 20; i++) { l.push_back(i); }
    for (int i = 0; i < PortSongs_Count(); i++) { l.push_back(PortSongs_Offset(i)); }
    l.push_back(24);
    return l;
}
int key_index(const char *const *keys, int n, const std::string &k, int def) {
    for (int i = 0; i < n; i++) { if (k == keys[i]) { return i; } }
    return def;
}
int pose_index(const J &v) {
    if (v.nul()) { return 0; }
    if (v.isNum()) { for (int i = 0; i < kPoseCount; i++) { if (kPoses[i].motion == (int)v.n) { return i; } } return -1; }
    for (int i = 0; i < kPoseCount; i++) { if (v.s == kPoses[i].name) { return i; } }
    return 0;
}

// A spoken line of a character: is there one (the disc's empty places are files of 202 bytes)?
bool line_file(int chara, int line, char *path, int size) {
    if (chara < 0 || line < 0 || line > 99) { return false; }
    int flid = PORT_CHARA_ADDED(chara) ? Port_CharaFileIndex(PORT_CHARA_IDX(chara), PORT_CFILE_LINE, line) : kVoiceJp - kPt2 + chara * 100 + line;
    return Port_FilePath(2, flid, NULL, path, size) != 0;
}
std::vector<int> sLinesOf[kCacheCharas];
bool sLinesRead[kCacheCharas];
const std::vector<int> &voice_lines(int chara) {
    static std::vector<int> none;
    int key = cache_key(chara);
    if (key < 0) { return none; }
    if (!sLinesRead[key]) {
        sLinesRead[key] = true;
        for (int l = 0; l < 100; l++) {
            char path[600];
            struct stat st;
            if (line_file(chara, l, path, sizeof(path)) && stat(path, &st) == 0 && st.st_size > 1024) { sLinesOf[key].push_back(l); }
        }
    }
    return sLinesOf[key];
}
std::string line_kind(int l) { // what the game says the numbers of a character's spoken lines are for
    if (l <= 1) { return "Entrance"; }
    if (l == 3 || l == 4) { return "Victory"; }
    if (l >= 6 && l < 30) { return "Rival entrance"; }
    if (l >= 30 && l < 54) { return "Rival victory"; }
    return "Line";
}

// =============================================================================================== portraits
// A large portrait file: byte-pair packed (the game's sys/bpe.c), one 256 x 512 picture of 256 colours in the GS's
// 8-bit layout (the same reading as gs/fight_panels_ui.cpp's).
const int ART_W = 256, ART_H = 512;
bool take(const uint8_t *&p, const uint8_t *end, int &v) {
    if (p >= end) { return false; }
    v = *p++;
    return true;
}
uint8_t *bpe_decode(const uint8_t *d, long len, uint32_t *size) {
    uint32_t total, pack, n = 0;
    if (len < 8) { return NULL; }
    memcpy(&total, d, 4);
    memcpy(&pack, d + 4, 4);
    if (total == 0 || total > (8u << 20) || (uint64_t)pack + 8 > (uint64_t)len) { return NULL; }
    uint8_t *out = (uint8_t *)malloc(total + 16);
    if (out == NULL) { return NULL; }
    const uint8_t *p = d + 8, *end = p + pack;
    bool ok = true;
    uint64_t steps = 0, budget = (uint64_t)total * 512 + (1u << 20);
    while (ok && p < end && n < total) {
        uint8_t left[256], right[256], stack[512];
        int c = 0, count, sp = 0, i, size16, v, hi, lo;
        for (i = 0; i < 256; i++) { left[i] = (uint8_t)i; right[i] = 0; }
        ok = take(p, end, count);
        while (ok) {
            if (count > 127) { c += count - 127; count = 0; }
            if (c == 256) { break; }
            for (i = 0; i <= count && ok; i++) {
                ok = take(p, end, v);
                if (!ok) { break; }
                left[c] = (uint8_t)v;
                if (c != v) { ok = take(p, end, v); right[c] = (uint8_t)v; }
                c++;
            }
            if (!ok || c == 256) { break; }
            ok = take(p, end, count);
        }
        if (!ok) { break; }
        ok = take(p, end, hi) && take(p, end, lo);
        size16 = hi * 256 + lo;
        while (ok && size16 > 0 && n < total) {
            ok = take(p, end, v);
            sp = 0;
            stack[sp++] = (uint8_t)v;
            while (ok && sp > 0 && n < total) {
                if (++steps > budget) { ok = false; break; }
                v = stack[--sp];
                if (left[v] == v) { out[n++] = (uint8_t)v; }
                else if (sp < 510) { stack[sp++] = right[v]; stack[sp++] = left[v]; }
                else { ok = false; }
            }
            size16--;
        }
    }
    if (!ok || n != total) { free(out); return NULL; }
    *size = total;
    return out;
}
const uint8_t kBlock32[4][8] = {{0, 1, 4, 5, 16, 17, 20, 21}, {2, 3, 6, 7, 18, 19, 22, 23}, {8, 9, 12, 13, 24, 25, 28, 29}, {10, 11, 14, 15, 26, 27, 30, 31}};
const uint8_t kCol32[8][8] = {{0, 1, 4, 5, 8, 9, 12, 13}, {2, 3, 6, 7, 10, 11, 14, 15}, {16, 17, 20, 21, 24, 25, 28, 29}, {18, 19, 22, 23, 26, 27, 30, 31},
                              {32, 33, 36, 37, 40, 41, 44, 45}, {34, 35, 38, 39, 42, 43, 46, 47}, {48, 49, 52, 53, 56, 57, 60, 61}, {50, 51, 54, 55, 58, 59, 62, 63}};
const uint8_t kCol8First[4][16] = {{0, 4, 16, 20, 32, 36, 48, 52, 2, 6, 18, 22, 34, 38, 50, 54}, {8, 12, 24, 28, 40, 44, 56, 60, 10, 14, 26, 30, 42, 46, 58, 62},
                                   {33, 37, 49, 53, 1, 5, 17, 21, 35, 39, 51, 55, 3, 7, 19, 23}, {41, 45, 57, 61, 9, 13, 25, 29, 43, 47, 59, 63, 11, 15, 27, 31}};
uint32_t addr32(uint32_t bw, uint32_t x, uint32_t y) {
    return (((y & ~0x1Fu) * bw + ((x >> 1) & ~0x1Fu) + kBlock32[(y >> 3) & 3][(x >> 3) & 7]) << 6) + kCol32[y & 7][x & 7];
}
uint32_t addr8(uint32_t bw, uint32_t x, uint32_t y) {
    uint32_t col = (y & 15) >> 2, sx = (col & 1) ? ((x & 15) ^ 4) : (x & 15);
    return ((((y >> 1) & ~0x1Fu) * (bw >> 1) + ((x >> 2) & ~0x1Fu) + kBlock32[(y >> 4) & 3][(x >> 4) & 7]) << 8) + kCol8First[y & 3][sx] + col * 64;
}
int csm1(int i) { return (i & 0x18) == 8 ? i + 8 : (i & 0x18) == 16 ? i - 8 : i; }
bool portrait_rgba(const char *path, uint8_t *rgba) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) { return false; }
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *pk = len > 0 ? (uint8_t *)malloc((size_t)len) : NULL;
    if (pk == NULL || fread(pk, 1, (size_t)len, fp) != (size_t)len) { free(pk); fclose(fp); return false; }
    fclose(fp);
    uint32_t size = 0, f[2];
    uint8_t *raw = bpe_decode(pk, len, &size);
    free(pk);
    if (raw == NULL) { return false; }
    bool ok = false;
    uint8_t *mem = (uint8_t *)calloc(1, 1 << 20);
    if (mem != NULL && size >= 0x60) {
        memcpy(f, raw + 0x20, sizeof(f));
        if ((uint64_t)f[0] * 4 + 0x60 + 131072 <= size && (uint64_t)f[1] * 4 + 0x60 + 1024 <= size) {
            const uint8_t *data = raw + f[0] * 4 + 0x60, *clut = raw + f[1] * 4 + 0x60;
            uint8_t pal[1024];
            for (int i = 0; i < 256; i++) {
                memcpy(&pal[i * 4], clut + csm1(i) * 4, 4);
                pal[i * 4 + 3] = pal[i * 4 + 3] >= 0x80 ? 255 : (uint8_t)(pal[i * 4 + 3] * 2);
            }
            for (uint32_t y = 0; y < 256; y++) {
                for (uint32_t x = 0; x < 128; x++) { memcpy(&mem[(addr32(2, x, y) * 4) & 0xFFFFC], &data[(y * 128 + x) * 4], 4); }
            }
            for (uint32_t y = 0; y < (uint32_t)ART_H; y++) {
                for (uint32_t x = 0; x < (uint32_t)ART_W; x++) { memcpy(rgba + (y * ART_W + x) * 4, &pal[mem[addr8(4, x, y) & 0xFFFFF] * 4], 4); }
            }
            ok = true;
        }
    }
    free(mem);
    free(raw);
    return ok;
}
// A character's face: a square crop of its portrait (the texture is the whole portrait; uv says the square)
struct Face { int state = 0; SDL_GPUTexture *tex = NULL; ImVec2 uv0, uv1; };
Face sFaces[kCacheCharas];
int sFacesThisFrame;
bool face(int chara, ImTextureID *tex, ImVec2 *uv0, ImVec2 *uv1) {
    int key = cache_key(chara);
    if (key < 0) { return false; }
    Face &f = sFaces[key];
    if (f.state == 0 && sFacesThisFrame < 2) { // (a few a frame: each is a file to unpack)
        sFacesThisFrame++;
        f.state = 2;
        char path[600];
        bool got = PORT_CHARA_ADDED(chara) ? Port_FilePath(2, Port_CharaFileIndex(PORT_CHARA_IDX(chara), PORT_CFILE_FACE, 0), NULL, path, sizeof(path)) != 0
                                            : chara < kDiscCharas && Port_FilePath(1, kFaceFile + chara - 1, NULL, path, sizeof(path)) != 0;
        uint8_t *pix = (uint8_t *)malloc((size_t)ART_W * ART_H * 4);
        if (got && pix != NULL && portrait_rgba(path, pix)) {
            float cx = 128, cy = 150;
            if (chara < kDiscCharas) { cx = kFace[chara][0]; cy = kFace[chara][1]; }
            float half = 66.0f;
            f.uv0 = ImVec2((cx - half) / ART_W, (cy - half * 1.05f) / ART_H);
            f.uv1 = ImVec2((cx + half) / ART_W, (cy + half * 0.95f) / ART_H);
            f.tex = Ui_Texture(pix, ART_W, ART_H); // (takes pix)
            pix = NULL;
            f.state = f.tex != NULL ? 1 : 2;
        }
        free(pix);
    }
    if (f.state != 1) { return false; }
    *tex = (ImTextureID)(intptr_t)f.tex;
    *uv0 = f.uv0;
    *uv1 = f.uv1;
    return true;
}

// =============================================================================================== files
std::string sDir;          // the sagas folder
std::string sNotice;       // a line for the player (an error, "saved")
int sNoticeUntil, sNoticeErr;
int sFrames;

std::string sagas_dir(void) {
    const char *e = getenv("BT3_SAGAS");
    std::string d = e != NULL && e[0] ? e : "sagas";
    while (d.size() > 1 && (d.back() == '/' || d.back() == '\\')) { d.pop_back(); }
    return d;
}
bool read_file(const std::string &path, std::string &out) {
    FILE *fp = fopen(path.c_str(), "rb");
    if (fp == NULL) { return false; }
    char buf[16384];
    size_t n;
    out.clear();
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) { out.append(buf, n); }
    fclose(fp);
    return true;
}
bool write_file(const std::string &path, const std::string &text) {
    SDL_CreateDirectory(sDir.c_str());
    std::string tmp = path + ".tmp";
    FILE *fp = fopen(tmp.c_str(), "wb");
    if (fp == NULL) { return false; }
    bool ok = fwrite(text.data(), 1, text.size(), fp) == text.size();
    ok = fclose(fp) == 0 && ok;
    if (!ok) { remove(tmp.c_str()); return false; }
    remove(path.c_str());
    return rename(tmp.c_str(), path.c_str()) == 0;
}
void notice(const std::string &t, bool err = false) {
    sNotice = t;
    sNoticeErr = err;
    sNoticeUntil = sFrames + (err ? 60 * 7 : 60 * 4);
    fprintf(stderr, "bt3: story: %s\n", t.c_str());
}

struct Saga { std::string file; J doc; bool builtin = false; };
std::vector<Saga> sSagas;
J sProgress; // {"<file>": {"episode": n, "cleared": [..], "done": true}}

std::string progress_path(void) { return sDir + "/_progress.json"; }
void progress_save(void) {
    std::string o;
    js_write(sProgress, o);
    o += '\n';
    if (!write_file(progress_path(), o)) { fprintf(stderr, "bt3: story: the progress could not be saved in %s\n", progress_path().c_str()); }
}
std::string saga_key(const Saga &s) { return s.builtin ? "builtin:" + s.file : s.file; }

void sagas_read(void) {
    sSagas.clear();
    Saga sample;
    sample.file = "sample";
    sample.builtin = true;
    if (!js_read(kStorySample, sample.doc)) { fprintf(stderr, "bt3: story: the sample saga does not read\n"); }
    int count = 0;
    char **names = SDL_GlobDirectory(sDir.c_str(), "*.json", 0, &count);
    std::vector<std::string> files;
    for (int i = 0; names != NULL && i < count; i++) {
        if (names[i][0] != '_') { files.push_back(names[i]); }
    }
    SDL_free(names);
    std::sort(files.begin(), files.end());
    for (const std::string &f : files) {
        Saga s;
        s.file = f;
        std::string text;
        if (!read_file(sDir + "/" + f, text) || !js_read(text, s.doc) || !s.doc.isObj()) {
            notice(f + " could not be read: is it a saga's JSON?", true);
            continue;
        }
        sSagas.push_back(s);
    }
    sSagas.push_back(sample);
    std::string text;
    if (!read_file(progress_path(), text) || !js_read(text, sProgress) || !sProgress.isObj()) { sProgress = J::obj(); }
}

std::string file_for(const std::string &title) {
    std::string f;
    for (char c : title) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) { f += c; }
        else if (c >= 'A' && c <= 'Z') { f += (char)(c - 'A' + 'a'); }
        else if (!f.empty() && f.back() != '_') { f += '_'; }
    }
    while (!f.empty() && f.back() == '_') { f.pop_back(); }
    if (f.empty()) { f = "saga"; }
    std::string name = f + ".json";
    for (int i = 2; i < 1000; i++) {
        bool taken = false;
        for (const Saga &s : sSagas) { taken |= !s.builtin && s.file == name; }
        struct stat st;
        taken |= stat((sDir + "/" + name).c_str(), &st) == 0;
        if (!taken) { break; }
        name = f + "_" + std::to_string(i) + ".json";
    }
    return name;
}

#include "story_lines.h"

// =============================================================================================== a saga, made into a battle
// The GSC script of an episode (sys/gsc.c): chunks GSCF { GSHD, GSCD { GSAC... }, GSDT }; every chunk a 16-byte header
// (tag, header size 16, data size, argument), its data, and a 16-byte "EOFC".
struct Gsc {
    struct Act { int id; std::vector<uint32_t> w; };
    std::vector<Act> acts;
    std::vector<uint32_t> data;
    struct Op { bool f; int32_t i; float v; };
    static Op I(int32_t i) { Op o; o.f = false; o.i = i; o.v = 0; return o; }
    static Op F(float v) { Op o; o.f = true; o.i = 0; o.v = v; return o; }
    void action(int id) { acts.push_back({id, {}}); }
    uint32_t operand(const Op &o) {
        uint32_t raw;
        if (o.f) { memcpy(&raw, &o.v, 4); } else { raw = (uint32_t)o.i; }
        data.push_back(raw);
        return 10u | ((o.f ? 1u : 0u) << 4) | ((uint32_t)(data.size() - 1) << 8);
    }
    void cmd(int id, std::vector<Op> ops = {}, std::vector<std::pair<char, std::vector<Op>>> opts = {}) {
        std::vector<uint32_t> &w = acts.back().w;
        w.push_back(1u | ((uint32_t)ops.size() << 8) | ((uint32_t)id << 16));
        for (const Op &o : ops) { w.push_back(operand(o)); }
        for (auto &op : opts) {
            w.push_back(8u | ((uint32_t)(uint8_t)op.first << 8) | ((uint32_t)op.second.size() << 16));
            for (const Op &o : op.second) { w.push_back(operand(o)); }
        }
    }
    void call(int action) { acts.back().w.push_back(2u | ((uint32_t)(uint16_t)action << 16)); }
    void end(void) { acts.back().w.push_back(0); }
    static void put32(std::vector<uint8_t> &b, uint32_t v) { for (int i = 0; i < 4; i++) { b.push_back((uint8_t)(v >> (i * 8))); } }
    static void chunk(std::vector<uint8_t> &b, const char *tag, int32_t arg, const std::vector<uint8_t> &body) {
        b.insert(b.end(), tag, tag + 4);
        put32(b, 16);
        put32(b, (uint32_t)body.size());
        put32(b, (uint32_t)arg);
        b.insert(b.end(), body.begin(), body.end());
        b.insert(b.end(), "EOFC", "EOFC" + 4);
        for (int i = 0; i < 12; i++) { b.push_back(0); }
    }
    std::vector<uint8_t> bytes(void) {
        std::vector<uint8_t> head, code, dt, inner, out;
        put32(head, 3);
        put32(head, 2);
        put32(head, 0);
        put32(head, 0);
        for (const Act &a : acts) {
            std::vector<uint8_t> w;
            for (uint32_t x : a.w) { put32(w, x); }
            chunk(code, "GSAC", a.id, w);
        }
        for (uint32_t x : data) { put32(dt, x); }
        chunk(inner, "GSHD", 0, head);
        chunk(inner, "GSCD", 1, code);
        chunk(inner, "GSDT", 1, dt);
        // (GSCF's header is followed by its chunks directly: Gsc_LoadFile takes the GSHD at the header's end)
        out.insert(out.end(), "GSCF", "GSCF" + 4);
        put32(out, 16);
        put32(out, (uint32_t)inner.size());
        put32(out, 1);
        out.insert(out.end(), inner.begin(), inner.end());
        out.insert(out.end(), "EOFC", "EOFC" + 4);
        for (int i = 0; i < 12; i++) { out.push_back(0); }
        return out;
    }
};

struct CLine { int v[PL_COUNT]; std::string name, text; int portrait; bool onStage; };
struct CEp {
    std::vector<uint8_t> script;
    std::vector<CLine> lines;
    int battle[PB_COUNT];
    std::vector<std::vector<int>> events;
    std::string sagaTitle, label, title, desc;
    int number;
};
SDL_Mutex *sMx;
CEp sEp;      // the episode being played (read by the game's thread)
bool sInBattle; // its battle is set up or running (the overlay is drawn)
int sSaga = -1;  // the saga open in the editor or being played (index in sSagas)
bool sPlayMode;  // the saga is being played (not tested from the editor)

struct Team { std::vector<int> chara, costume; };
Team team_of(const J &ep, const char *key) {
    Team t;
    const J &l = ep[key];
    for (size_t i = 0; i < l.size() && t.chara.size() < 5; i++) {
        int c = chara_of(l[i].isObj() ? l[i]["chara"] : l[i]);
        if (c < 0) { continue; }
        t.chara.push_back(c);
        t.costume.push_back(l[i].isObj() ? (int)l[i]["costume"].number(0) : 0);
    }
    return t;
}
int utf8_len(const std::string &s) {
    int n = 0;
    for (unsigned char c : s) { n += (c & 0xC0) != 0x80; }
    return n;
}

// speaker: "player", "player2".."player5", "enemy", "enemy2".., "narrator"
void speaker_of(const std::string &sp, int *side, int *member) {
    *side = -1;
    *member = -1;
    if (sp.compare(0, 6, "player") == 0) { *side = 0; *member = sp.size() > 6 ? atoi(sp.c_str() + 6) - 1 : -1; }
    else if (sp.compare(0, 5, "enemy") == 0) { *side = 1; *member = sp.size() > 5 ? atoi(sp.c_str() + 5) - 1 : -1; }
    if (*member == 0) { *member = -1; }
}

CLine compile_line(const J &l, const Team *teams) {
    CLine c;
    for (int i = 0; i < PL_COUNT; i++) { c.v[i] = -1; }
    int side, member;
    speaker_of(l["speaker"].text("player"), &side, &member);
    int chara = -1;
    if (side >= 0) {
        const Team &t = teams[side];
        int m = member < 0 ? 0 : member;
        if (m < (int)t.chara.size()) { chara = t.chara[(size_t)m]; }
        else if (!t.chara.empty()) { chara = t.chara[0]; member = -1; }
    }
    if (l.has("as")) { chara = chara_of(l["as"]); } // (someone else's face and name, on the speaker's side)
    c.v[PL_SIDE] = side;
    c.v[PL_MEMBER] = member;
    c.v[PL_CHARA] = chara;
    c.portrait = chara;
    c.onStage = side >= 0 && member < 0;
    c.text = l["text"].text();
    c.name = l.has("name") ? l["name"].text() : side < 0 ? std::string() : short_name(chara);
    // the voice: one of a character's spoken lines (the speaker's by default), or a random one of them
    const J &v = l["voice"];
    int vchara = chara, vline = -1;
    if (v.isObj() && v.has("story")) { // a voiced line of Dragon History (story_lines.h)
        c.v[PL_VCHARA] = -2;
        c.v[PL_VOICE] = (int)v["story"].number(0) * 300 + (int)v["line"].number(0);
    } else if (v.isObj()) {
        if (v.has("chara")) { vchara = chara_of(v["chara"]); }
        if (v["line"].isNum()) { vline = (int)v["line"].n; }
        else if (v["line"].text() == "random") { vline = -2; }
    } else if (v.isNum()) {
        vline = (int)v.n;
    } else if (v.text() == "random") {
        vline = -2;
    }
    if (vline == -2) {
        const std::vector<int> &have = voice_lines(vchara);
        vline = have.empty() ? -1 : have[(size_t)(rand() % (int)have.size())];
    }
    if (vline >= 0 && vchara >= 0) {
        c.v[PL_VOICE] = vline;
        c.v[PL_VCHARA] = vchara;
        if (vchara == chara && c.onStage) { c.v[PL_LIP] = vline; }
    }
    c.v[PL_CAMERA] = key_index(kCamKeys, CAM_COUNT, l["camera"].text("auto"), CAM_AUTO);
    c.v[PL_MOVE] = key_index(kMoveKeys, MOVE_COUNT, l["move"].text("cut"), MOVE_CUT);
    c.v[PL_MOVE_T] = (int)(l["move_time"].number(3.0) * 10.0 + 0.5);
    int p = pose_index(l["pose"]);
    if (l["pose"].isNum() && p < 0) { c.v[PL_POSE] = (int)l["pose"].n; c.v[PL_POSE_LOOP] = l["pose_loop"].truthy(true); }
    else if (p > 0) { c.v[PL_POSE] = kPoses[p].motion; c.v[PL_POSE_LOOP] = l.has("pose_loop") ? l["pose_loop"].truthy() : kPoses[p].loop; }
    int q = pose_index(l["listener_pose"]);
    if (l["listener_pose"].isNum() && q < 0) { c.v[PL_LPOSE] = (int)l["listener_pose"].n; c.v[PL_LPOSE_LOOP] = 1; }
    else if (q > 0) { c.v[PL_LPOSE] = kPoses[q].motion; c.v[PL_LPOSE_LOOP] = kPoses[q].loop; }
    int fx = 0;
    const J &e = l["effects"];
    for (size_t i = 0; i < e.size(); i++) {
        for (int k = 0; k < kFxCount; k++) { if (e[i].text() == kFx[k].key) { fx |= kFx[k].bit; } }
    }
    c.v[PL_FX] = fx;
    c.v[PL_LEN] = utf8_len(c.text);
    c.v[PL_WAIT] = (int)(l["wait"].number(0) * 60.0);
    return c;
}

// What an episode is played as. preview: one of its scenes alone ("intro", "win", "lose"), no fight.
bool compile(const J &saga, int number, const std::string &preview, CEp &out, std::string &why) {
    const J &eps = saga["episodes"];
    if (number < 0 || number >= (int)eps.size()) { why = "That episode is not in the saga."; return false; }
    const J &ep = eps[number];
    Team teams[2] = {team_of(ep, "player"), team_of(ep, "enemy")};
    if (teams[0].chara.empty() || teams[1].chara.empty()) {
        why = "Episode " + std::to_string(number + 1) + " needs a fighter on each side.";
        return false;
    }
    out = CEp();
    out.number = number;
    out.sagaTitle = saga["title"].text("Saga");
    out.title = ep["title"].text();
    out.desc = ep["description"].text();
    out.label = "EPISODE " + std::to_string(number + 1);
    const J &bt = ep["battle"];
    bool fight = preview.empty() && bt["fight"].truthy(true);
    const J &hubDef = ep["hub"]; // (Corrupt Patrol's headquarters: a battle without a fight, people standing round: 9007)
    bool hub = hubDef.isObj() && preview.empty();
    if (hub) { fight = true; }
    Gsc g;
    // the setup: the rule, the sides, the members (the game's commands 10, 11, 12)
    g.action(1000);
    int stage = stage_of(ep["stage"]);
    int bgm = (int)ep["music"].number(8);
    g.cmd(10, {Gsc::I(stage), Gsc::I(bgm), Gsc::I(0), Gsc::I(fight ? 3 : 0), Gsc::I(0)});
    for (int s = 0; s < 2; s++) { g.cmd(11, {Gsc::I(s), Gsc::I((int)teams[s].chara.size())}); }
    int cpu = kCpuLevels[std::max(0, std::min(4, (int)bt["cpu"].number(1)))];
    if (bt.has("cpu_level")) { cpu = std::max(hub ? -1 : 0, std::min(40, (int)bt["cpu_level"].number(cpu))); } // (Corrupt Patrol: the AI's own scale)
    for (int s = 0; s < 2; s++) {
        int hp = std::max(1, std::min(100, (int)bt[s ? "enemy_health" : "player_health"].number(100)));
        const J &items = bt[s ? "enemy_items" : "player_items"]; // (Corrupt Patrol: Z items of the side's fighters, up to 8 ids)
        for (size_t m = 0; m < teams[s].chara.size(); m++) {
            std::vector<Gsc::Op> ops = {Gsc::I((s << 15) | (int)m), Gsc::I(teams[s].chara[m]), Gsc::I(teams[s].costume[m]), Gsc::I(0),
                                        Gsc::I(s ? cpu : 0), Gsc::I(items.size() > 7 ? (int)items[7].number(-1) : -1), Gsc::I(hp)};
            for (int i = 0; i < 7; i++) { ops.push_back(Gsc::I(i < (int)items.size() ? (int)items[i].number(-1) : -1)); }
            g.cmd(12, ops);
        }
    }
    g.end();
    // the main action starts the opening
    g.action(-1);
    g.call(10000);
    g.end();
    auto scene = [&](const J &sc, bool title) {
        int dist = std::max(0, std::min(3, (int)sc["distance"].number(2)));
        int flags = (sc["hide_player"].truthy() ? 1 : 0) | (sc["hide_enemy"].truthy() ? 2 : 0);
        g.cmd(9001, {Gsc::I(dist), Gsc::I(flags)});
        if (title) { g.cmd(9005); }
        const J &ls = sc["lines"];
        for (size_t i = 0; i < ls.size(); i++) {
            out.lines.push_back(compile_line(ls[i], teams));
            g.cmd(9002, {Gsc::I((int)out.lines.size() - 1)});
        }
        g.cmd(9006);
    };
    // the opening: the scene, then the fight (or the end, without one)
    g.action(10000);
    g.cmd(3);
    g.cmd(9, {Gsc::I(-1)});
    g.cmd(5);
    if (hub) {} // (no scene: the player is let loose at once)
    else if (preview.empty() || preview == "intro") { scene(ep["intro"], preview.empty()); }
    else { scene(ep[preview.c_str()], false); }
    g.cmd(6);
    if (fight) {
        g.cmd(7, {}, {{'a', {Gsc::I(0x7F), Gsc::I(10040)}}, {'a', {Gsc::I(0x7F), Gsc::I(10041)}},
                      {'a', {Gsc::I(0x7F), Gsc::I(10030)}}, {'a', {Gsc::I(0x7F), Gsc::I(10031)}}});
        for (int s = 0; s < 2; s++) {
            int ki = std::max(0, std::min(100, (int)bt[s ? "enemy_ki" : "player_ki"].number(0)));
            if (ki > 0) { g.cmd(s ? 14 : 13, {}, {{'F', {Gsc::I(ki)}}}); } else { g.cmd(s ? 14 : 13); }
        }
        g.call(2000);
        g.cmd(8, {Gsc::I(1), Gsc::I(-1)});
    } else {
        g.cmd(9003, {Gsc::I(1)});
    }
    g.cmd(4);
    g.end();
    if (fight) {
        g.action(2000); // the referee, a task of its own (the headquarters' own for a hub)
        if (hub) {
            const J &ppl = hubDef["npcs"];
            std::vector<Gsc::Op> ops = {Gsc::I((int)std::min((size_t)8, ppl.size()))};
            for (size_t i = 0; i < ppl.size() && i < 8; i++) {
                ops.push_back(Gsc::I(chara_of(ppl[i]["chara"])));
                ops.push_back(Gsc::I((int)ppl[i]["costume"].number(0)));
            }
            ops.push_back(Gsc::I(hubDef["arrive"].truthy() ? 1 : 0)); // (the player flies in and lands)
            ops.push_back(Gsc::I((int)hubDef["spread"].number(100))); // (how far apart the people stand)
            g.cmd(9007, ops);
        } else {
            g.cmd(9004);
        }
        g.end();
        // after the fight: won by a knock-out / on time (10040 / 10041), lost (10030 / 10031)
        static const int kActs[4] = {10040, 10041, 10030, 10031};
        for (int k = 0; k < 4; k++) {
            const J &sc = ep[k < 2 ? "win" : "lose"];
            g.action(kActs[k]);
            g.cmd(3);
            g.cmd(9, {Gsc::I(k == 0 ? 1 : k == 1 ? 3 : k == 2 ? 0 : 2)});
            if (sc["lines"].size() > 0) {
                g.cmd(5);
                g.cmd(1201, {Gsc::F(0), Gsc::F(0), Gsc::F(0), Gsc::F(0), Gsc::F(0), Gsc::F(0)}); // (a camera command: the fight
                // is interrupted for it and the winner state waits for the scene; the scene's own shot follows at once)
                scene(sc, false);
                g.cmd(6);
            }
            g.cmd(7);
            g.cmd(8, {Gsc::I(0x8002), Gsc::I(-1)});
            g.cmd(4);
            g.end();
        }
    }
    // the battle's conditions and the lines said in it
    for (int i = 0; i < PB_COUNT; i++) { out.battle[i] = 0; }
    out.battle[PB_WIN] = bt["win"].text("ko") == "survive" ? 1 : 0;
    out.battle[PB_SURVIVE] = (int)bt["survive"].number(60);
    out.battle[PB_TIME] = (int)bt["time"].number(0);
    std::string tu = bt["time_up"].text("health");
    out.battle[PB_TIME_UP] = tu == "win" ? 1 : tu == "lose" ? 0 : 2;
    const J &ev = bt["lines"];
    for (size_t i = 0; fight && i < ev.size() && out.events.size() < 32; i++) {
        std::vector<int> e(PE_COUNT, 0);
        e[PE_WHEN] = key_index(kWhenKeys, WHEN_COUNT, ev[i]["when"].text("start"), WHEN_START);
        e[PE_VALUE] = (int)ev[i]["value"].number(50);
        out.lines.push_back(compile_line(ev[i], teams));
        e[PE_LINE] = (int)out.lines.size() - 1;
        out.events.push_back(e);
    }
    out.battle[PB_EVENTS] = (int)out.events.size();
    out.script = g.bytes();
    if (getenv("BT3_STORY_DUMP") != NULL) { // (testing: the script made, for port/tools' disassembler)
        FILE *fp = fopen(getenv("BT3_STORY_DUMP"), "wb");
        if (fp != NULL) { fwrite(out.script.data(), 1, out.script.size(), fp); fclose(fp); }
    }
    return true;
}

// =============================================================================================== shared with the game's thread
volatile int sActive;    // the screens are open (PortStoryUi_Open .. Close)
volatile int sOpenGen;
struct Shared {
    int leave = 0;
    int sound[16] = {0}, sounds = 0;
    int key = -1, chara = 0, costume = 0;
    int turn = 0;
    int voice = -1;        // a spoken line to hear: chara << 8 | line, -1 none
    int bgm = 2;
    // the play wanted
    int saga = 0, episode = 0; // index in sSagas, episode number
    std::string preview;   // "" the episode, else one scene of it
    // the overlay: what the episode's battle shows
    int say = -1, shown = 0, sayGen = 0;
    int title = -1;        // frames since the title card came, -1 none
    int hud = -1, hudFrames = 0;
    int letterbox = 0;
    int flash = 0;
    int fadeAlpha = 0, fadeStep = 0;
    int timer = -1, timerSurvive = 0;
    int result = 0, resultGen = 0; // how the battle went (PortStoryUi_Result)
};
Shared sSh;

void sound(int id) { // 0 move, 1 choose, 2 back
    SDL_LockMutex(sMx);
    if (sSh.sounds < 16) { sSh.sound[sSh.sounds++] = id; }
    SDL_UnlockMutex(sMx);
}

} // namespace

extern "C" void PortStoryUi_Open(void) {
    if (sMx == NULL) { sMx = SDL_CreateMutex(); }
    SDL_LockMutex(sMx);
    sSh.leave = 0;
    sSh.sounds = 0;
    sSh.turn = 0;
    sSh.voice = -1;
    sOpenGen++;
    sActive = 1;
    sInBattle = false;
    SDL_UnlockMutex(sMx);
    // BT3_TEST_STORY=play:<saga file or "sample">:<episode, from 1>[:<scene>] (testing): that episode at once, once
    static bool tested;
    const char *t = getenv("BT3_TEST_STORY");
    if (!tested && t != NULL && strncmp(t, "play:", 5) == 0) {
        tested = true;
        std::string spec = t + 5, file = spec.substr(0, spec.find(':')), rest = spec.find(':') != std::string::npos ? spec.substr(spec.find(':') + 1) : "1";
        std::string scene = rest.find(':') != std::string::npos ? rest.substr(rest.find(':') + 1) : "";
        sDir = sagas_dir();
        sagas_read();
        for (size_t i = 0; i < sSagas.size(); i++) {
            if (sSagas[i].file == file || sSagas[i].file == file + ".json") {
                sSaga = (int)i;
                sPlayMode = scene.empty();
                SDL_LockMutex(sMx);
                sSh.saga = (int)i;
                sSh.episode = atoi(rest.c_str()) - 1;
                sSh.preview = scene;
                sSh.leave = 2;
                SDL_UnlockMutex(sMx);
                fprintf(stderr, "bt3: story: test: %s, episode %d %s\n", file.c_str(), atoi(rest.c_str()), scene.c_str());
            }
        }
    }
}
extern "C" void PortStoryUi_Close(void) {
    SDL_LockMutex(sMx);
    sActive = 0;
    SDL_UnlockMutex(sMx);
}
extern "C" int PortStoryUi_Active(void) { return sActive; }
extern "C" int PortStoryUi_Get(int what) {
    int v = -1;
    if (sMx == NULL) { return what == PG_BGM ? 2 : -1; }
    SDL_LockMutex(sMx);
    switch (what) {
    case PG_LEAVE: v = sSh.leave; break;
    case PG_SOUND:
        if (sSh.sounds > 0) { v = sSh.sound[0]; memmove(sSh.sound, sSh.sound + 1, sizeof(int) * (size_t)(--sSh.sounds)); }
        break;
    case PG_KEY: v = sSh.key; break;
    case PG_CHARA: v = sSh.chara; break;
    case PG_COSTUME: v = sSh.costume; break;
    case PG_TURN: v = sSh.turn; sSh.turn = 0; break;
    case PG_BGM: v = sSh.bgm; break;
    case PG_VOICE: v = sSh.voice; sSh.voice = -1; break;
    }
    SDL_UnlockMutex(sMx);
    return v;
}
// (the game's thread, as the screens are left for an episode) its script made; 0: it could not be (and why is said)
extern "C" int PortStoryUi_Prepare(void) {
    SDL_LockMutex(sMx);
    int saga = sSh.saga, episode = sSh.episode;
    std::string preview = sSh.preview;
    bool ok = false;
    std::string why;
    if (saga >= 0 && saga < (int)sSagas.size()) {
        ok = compile(sSagas[(size_t)saga].doc, episode, preview, sEp, why);
    } else {
        why = "No saga chosen.";
    }
    if (ok) {
        sInBattle = true;
        sSh.say = -1;
        sSh.title = -1;
        sSh.hud = -1;
        sSh.letterbox = 0;
        sSh.flash = 0;
        sSh.fadeAlpha = 0;
        sSh.timer = -1;
        sSh.result = 0;
        fprintf(stderr, "bt3: story: playing %s, episode %d%s%s (%d lines, script %d bytes)\n", sSagas[(size_t)saga].file.c_str(), episode + 1,
                preview.empty() ? "" : ", scene ", preview.c_str(), (int)sEp.lines.size(), (int)sEp.script.size());
    } else {
        sSh.leave = 0;
    }
    SDL_UnlockMutex(sMx);
    if (!ok) { notice(why, true); }
    return ok ? 1 : 0;
}
// (the game's thread) Corrupt Patrol's battles: an episode of a saga made by the mode (gs/patrol_ui.cpp) rather than
// one of the sagas folder, made into its script as PortStoryUi_Prepare makes the Story Creator's
J sExtDoc;
extern "C" int PortStoryUi_PrepareDoc(const char *json, int episode) {
    J doc;
    std::string why;
    if (json == NULL || !js_read(json, doc)) { fprintf(stderr, "bt3: story: a mode's saga could not be read\n"); return 0; }
    if (sMx == NULL) { sMx = SDL_CreateMutex(); }
    SDL_LockMutex(sMx);
    sExtDoc = doc;
    bool ok = compile(sExtDoc, episode, "", sEp, why);
    if (ok) {
        sInBattle = true;
        sSh.say = -1;
        sSh.title = -1;
        sSh.hud = -1;
        sSh.letterbox = 0;
        sSh.flash = 0;
        sSh.fadeAlpha = 0;
        sSh.timer = -1;
        sSh.result = 0;
    }
    SDL_UnlockMutex(sMx);
    if (ok) { fprintf(stderr, "bt3: story: a mode's episode %d (%d lines)\n", episode + 1, (int)sEp.lines.size()); }
    else { fprintf(stderr, "bt3: story: a mode's episode %d could not be made: %s\n", episode + 1, why.c_str()); }
    return ok ? 1 : 0;
}
// ... its battle ended (the overlay is put away; the mode reads the result itself)
extern "C" void PortStoryUi_EndDoc(void) {
    if (sMx == NULL) { return; }
    SDL_LockMutex(sMx);
    sInBattle = false;
    SDL_UnlockMutex(sMx);
}
extern "C" void PortStoryUi_Result(int result) {
    SDL_LockMutex(sMx);
    sInBattle = false;
    sSh.result = result;
    sSh.resultGen++;
    SDL_UnlockMutex(sMx);
    fprintf(stderr, "bt3: story: the episode's battle ended: %s\n", result == 1 ? "won" : result == 2 ? "lost" : "left");
}
// BT3_STORY_AUTO=<frames> (testing): every line goes on by itself that long after its text is out
extern "C" int PortStoryUi_AutoWait(void) {
    const char *e = getenv("BT3_STORY_AUTO");
    return e != NULL ? atoi(e) : 0;
}
extern "C" int PortStoryUi_ScriptSize(void) {
    SDL_LockMutex(sMx);
    int n = (int)sEp.script.size();
    SDL_UnlockMutex(sMx);
    return n;
}
extern "C" void PortStoryUi_ScriptCopy(void *dst) {
    SDL_LockMutex(sMx);
    memcpy(dst, sEp.script.data(), sEp.script.size());
    SDL_UnlockMutex(sMx);
}
extern "C" int PortStoryUi_Line(int n, int *out) {
    SDL_LockMutex(sMx);
    bool ok = n >= 0 && n < (int)sEp.lines.size();
    if (ok) { memcpy(out, sEp.lines[(size_t)n].v, sizeof(int) * 16); }
    SDL_UnlockMutex(sMx);
    return ok;
}
extern "C" int PortStoryUi_Battle(int *out) {
    SDL_LockMutex(sMx);
    memcpy(out, sEp.battle, sizeof(int) * PB_COUNT);
    SDL_UnlockMutex(sMx);
    return 1;
}
extern "C" int PortStoryUi_Event(int n, int *out) {
    SDL_LockMutex(sMx);
    bool ok = n >= 0 && n < (int)sEp.events.size();
    if (ok) { for (int i = 0; i < PE_COUNT; i++) { out[i] = sEp.events[(size_t)n][(size_t)i]; } }
    SDL_UnlockMutex(sMx);
    return ok;
}
extern "C" void PortStoryUi_Show(int what, int a, int b) {
    if (sMx == NULL) { return; }
    SDL_LockMutex(sMx);
    switch (what) {
    case SHOW_SAY: if (sSh.say != a) { sSh.sayGen++; } sSh.say = a; sSh.shown = b; break;
    case SHOW_HIDE: sSh.say = -1; break;
    case SHOW_TITLE: sSh.title = a; break;
    case SHOW_HUD: sSh.hud = a; sSh.hudFrames = b; break;
    case SHOW_LETTERBOX: sSh.letterbox = a; break;
    case SHOW_FLASH: sSh.flash = a; break;
    case SHOW_FADE: sSh.fadeAlpha = a; sSh.fadeStep = b; break;
    case SHOW_TIMER: sSh.timer = a; sSh.timerSurvive = b; break;
    case SHOW_END: sSh.say = -1; sSh.letterbox = 0; break;
    }
    SDL_UnlockMutex(sMx);
}

namespace {

// =============================================================================================== drawing
// The places are those of the Mod Manager's mockups (1376 x 768) spread over the game's picture, as the Custom
// Character screen has them (the model on the right stays where the game draws it).
float sX0, sY0, sW, sH, sS;
ImVec2 P(float x, float y) { return ImVec2(sX0 + x * sW / 1376.0f, sY0 + y * sH / 768.0f); }
float S(float l) { return l * sS; }
ImFont *sFont, *sBold;
ImDrawList *sDl;
const ImU32 kInk = IM_COL32(255, 255, 255, 255), kDim = IM_COL32(201, 184, 240, 255), kDark = IM_COL32(58, 18, 0, 255);
const ImU32 kGold1 = IM_COL32(255, 211, 77, 255), kGold2 = IM_COL32(243, 156, 18, 255);
bool sKeyboardLast;

void frame_setup(void) {
    ImGuiIO &io = ImGui::GetIO();
    sX0 = (float)gUiPresentX;
    sY0 = (float)gUiPresentY;
    sW = (float)gUiPresentW;
    sH = (float)gUiPresentH;
    if (sW <= 0 || sH <= 0) { sX0 = sY0 = 0; sW = io.DisplaySize.x; sH = io.DisplaySize.y; }
    sS = std::min(sW / 1376.0f, sH / 768.0f);
    sDl = ImGui::GetBackgroundDrawList();
    sFont = ImGui::GetFont();
    sBold = Ui_FontBold() != NULL ? Ui_FontBold() : sFont;
    sFacesThisFrame = 0;
}
void grad(ImVec2 a, ImVec2 b, ImU32 top, ImU32 bottom, float round) {
    int v0 = sDl->VtxBuffer.Size;
    sDl->AddRectFilled(a, b, IM_COL32(255, 255, 255, (top >> IM_COL32_A_SHIFT) & 0xFF), round);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(sDl, v0, sDl->VtxBuffer.Size, a, ImVec2(a.x, b.y), top, bottom);
}
float text_w(ImFont *f, float size, const char *t) { return f->CalcTextSizeA(size, FLT_MAX, 0.0f, t).x; }
void text(ImFont *f, float size, ImVec2 at, ImU32 c, const std::string &t, float maxW = 0.0f) {
    std::string s = t;
    for (char &ch : s) { if (ch == '\n') { ch = ' '; } }
    if (maxW > 0.0f && text_w(f, size, s.c_str()) > maxW) {
        while (!s.empty() && text_w(f, size, (s + "...").c_str()) > maxW) {
            s.pop_back();
            while (!s.empty() && ((unsigned char)s.back() & 0xC0) == 0x80) { s.pop_back(); }
        }
        s += "...";
    }
    sDl->AddText(f, size, at, c, s.c_str());
}
void outlined(ImFont *f, float size, ImVec2 at, ImU32 c, ImU32 edge, const std::string &t, float w = 2.0f) {
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            if (dx || dy) { sDl->AddText(f, size, ImVec2(at.x + dx * w, at.y + dy * w), edge, t.c_str()); }
        }
    }
    sDl->AddText(f, size, at, c, t.c_str());
}
void panel(ImVec2 a, ImVec2 b) {
    sDl->AddRectFilled(ImVec2(a.x, a.y + S(5)), ImVec2(b.x, b.y + S(5)), IM_COL32(0, 0, 0, 70), S(12));
    sDl->AddRectFilled(a, b, IM_COL32(46, 26, 92, 228), S(12));
    sDl->AddRect(a, b, IM_COL32(185, 165, 255, 255), S(12), 0, S(3));
}
void face_at(int chara, ImVec2 a, ImVec2 b, float round, ImU32 tint = IM_COL32_WHITE) {
    ImTextureID t;
    ImVec2 u0, u1;
    sDl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 22), round);
    if (face(chara, &t, &u0, &u1)) { sDl->AddImageRounded(ImTextureRef(t), a, b, u0, u1, tint, round); }
}
enum { B_CROSS, B_CIRCLE, B_SQUARE, B_TRIANGLE, B_L1R1, B_L2R2, B_START, B_DPAD, B_LR };
float glyph(int b, ImVec2 c, float r) {
    static const char *const kKeys[] = {"Enter", "Esc", "F", "R", "Q/E", "Z/C", "Ctrl+S", "Arrows", "Left/Right"};
    if (sKeyboardLast) {
        const char *k = kKeys[b];
        float tw = text_w(sFont, r * 1.1f, k), w = tw + r * 1.2f;
        sDl->AddRectFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + w, c.y + r), IM_COL32(42, 26, 85, 255), r * 0.35f);
        sDl->AddRect(ImVec2(c.x, c.y - r), ImVec2(c.x + w, c.y + r), IM_COL32(205, 189, 255, 255), r * 0.35f, 0, S(1.5f));
        sDl->AddText(sFont, r * 1.1f, ImVec2(c.x + r * 0.6f, c.y - r * 0.62f), kInk, k);
        return w;
    }
    if (b >= B_L1R1) {
        const char *k = b == B_L1R1 ? "L1 R1" : b == B_L2R2 ? "L2 R2" : b == B_START ? "START" : b == B_DPAD ? "D-PAD" : "< >";
        float tw = text_w(sFont, r * 1.0f, k), w = tw + r * 1.2f;
        sDl->AddRectFilled(ImVec2(c.x, c.y - r * 0.85f), ImVec2(c.x + w, c.y + r * 0.85f), IM_COL32(42, 26, 85, 255), r * 0.8f);
        sDl->AddRect(ImVec2(c.x, c.y - r * 0.85f), ImVec2(c.x + w, c.y + r * 0.85f), IM_COL32(205, 189, 255, 255), r * 0.8f, 0, S(1.5f));
        sDl->AddText(sFont, r * 1.0f, ImVec2(c.x + r * 0.6f, c.y - r * 0.56f), kInk, k);
        return w;
    }
    ImVec2 m(c.x + r, c.y);
    sDl->AddCircleFilled(m, r, IM_COL32(42, 26, 85, 255));
    sDl->AddCircle(m, r, IM_COL32(205, 189, 255, 255), 0, S(1.5f));
    float k = r * 0.45f, t = S(2.2f);
    if (b == B_CROSS) {
        ImU32 col = IM_COL32(140, 180, 255, 255);
        sDl->AddLine(ImVec2(m.x - k, m.y - k), ImVec2(m.x + k, m.y + k), col, t);
        sDl->AddLine(ImVec2(m.x - k, m.y + k), ImVec2(m.x + k, m.y - k), col, t);
    } else if (b == B_CIRCLE) {
        sDl->AddCircle(m, k * 1.05f, IM_COL32(255, 110, 110, 255), 0, t);
    } else if (b == B_SQUARE) {
        sDl->AddRect(ImVec2(m.x - k, m.y - k), ImVec2(m.x + k, m.y + k), IM_COL32(255, 150, 210, 255), 0, 0, t);
    } else {
        sDl->AddTriangle(ImVec2(m.x, m.y - k * 1.1f), ImVec2(m.x + k * 1.1f, m.y + k * 0.8f), ImVec2(m.x - k * 1.1f, m.y + k * 0.8f), IM_COL32(100, 230, 160, 255), t);
    }
    return r * 2.0f;
}
struct Prompt { int b; std::string label; };
void prompts(const std::vector<Prompt> &ps) {
    float r = S(12), size = S(17), gap = S(22);
    float total = 0;
    for (const Prompt &p : ps) { total += S(70) + text_w(sFont, size, p.label.c_str()) + gap; }
    ImVec2 at = P(1336, 742);
    float x = at.x - total;
    if (x < P(40, 0).x) { x = P(40, 0).x; }
    for (const Prompt &p : ps) {
        float w = glyph(p.b, ImVec2(x, at.y), r);
        x += w + S(6);
        outlined(sFont, size, ImVec2(x, at.y - size * 0.55f), kInk, IM_COL32(20, 8, 40, 255), p.label, S(1.2f));
        x += text_w(sFont, size, p.label.c_str()) + gap;
    }
}
void tip_box(const std::string &line, int host) {
    ImVec2 a = P(40, 664), b = P(1336, 720);
    sDl->AddRectFilled(a, b, IM_COL32(30, 18, 70, 235), S(8));
    sDl->AddRect(a, b, IM_COL32(199, 180, 255, 255), S(8), 0, S(2));
    float r = (b.y - a.y) * 0.5f - S(4);
    ImVec2 c(a.x + S(10) + r, (a.y + b.y) * 0.5f);
    sDl->AddCircleFilled(c, r + S(3), IM_COL32(255, 255, 255, 255));
    ImTextureID t;
    ImVec2 u0, u1;
    if (face(host, &t, &u0, &u1)) { sDl->AddImageRounded(ImTextureRef(t), ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), u0, u1, IM_COL32_WHITE, r); }
    else { sDl->AddCircleFilled(c, r, IM_COL32(44, 123, 214, 255)); }
    float size = S(19);
    sDl->PushClipRect(a, b, true);
    sDl->AddText(sFont, size, ImVec2(c.x + r + S(16), c.y - size * 0.55f - (text_w(sFont, size, line.c_str()) > (b.x - a.x) - r * 2 - S(40) ? size * 0.5f : 0)),
                 kInk, line.c_str(), NULL, (b.x - c.x) - r - S(30));
    sDl->PopClipRect();
}
void heading(const std::string &h, const std::string &sub) {
    ImVec2 e = P(76, 56);
    float er = S(30);
    sDl->AddCircleFilled(ImVec2(e.x, e.y + S(3)), er, IM_COL32(107, 58, 0, 255));
    sDl->AddCircleFilled(e, er, kGold2);
    sDl->AddCircleFilled(ImVec2(e.x - er * 0.25f, e.y - er * 0.3f), er * 0.55f, IM_COL32(255, 230, 128, 160));
    sDl->AddCircle(e, er, IM_COL32(255, 242, 176, 255), 0, S(3));
    for (int i = 0; i < 4; i++) { // (the four-star ball)
        float ang = i * 1.5708f + 0.785f;
        ImVec2 sp(e.x + cosf(ang) * er * 0.32f, e.y + sinf(ang) * er * 0.32f);
        sDl->AddCircleFilled(sp, er * 0.13f, IM_COL32(200, 40, 20, 255));
    }
    float hs = S(46);
    std::string up = h;
    for (char &c : up) { c = (char)SDL_toupper((unsigned char)c); }
    if (up.size() > 26) { up = up.substr(0, 26); }
    outlined(sBold, hs, P(120, 30), IM_COL32(255, 214, 70, 255), IM_COL32(90, 29, 0, 255), up, S(2.5f));
    if (!sub.empty()) {
        float size = S(16), w = text_w(sBold, size, sub.c_str()) + S(36), h2 = S(32);
        ImVec2 a(P(40, 0).x, P(0, 98).y), b(a.x + w, a.y + h2);
        grad(a, b, kGold1, kGold2, h2 * 0.5f);
        sDl->AddRect(a, b, IM_COL32(255, 241, 176, 255), h2 * 0.5f, 0, S(2));
        sDl->AddText(sBold, size, ImVec2(a.x + S(18), a.y + (h2 - size) * 0.5f), IM_COL32(74, 26, 0, 255), sub.c_str());
    }
}

// =============================================================================================== input
struct In { bool up, down, left, right, ok, back, sq, tri, l1, r1, l2, r2, start, enter, esc, backspace; float turn; };
In sIn;
bool sTyping;
int sScriptAt;
bool key(ImGuiKey k, bool repeat = true) { return ImGui::IsKeyPressed(k, repeat); }
void read_input(bool live) {
    In in;
    memset(&in, 0, sizeof(in));
    if (live) {
        bool pad = false, kb = false;
        auto Pd = [&](ImGuiKey k, bool repeat) { bool p = key(k, repeat); pad |= p; return p; };
        auto K = [&](ImGuiKey k, bool repeat) { bool p = key(k, repeat); kb |= p; return p; };
        in.up = Pd(ImGuiKey_GamepadDpadUp, true) || Pd(ImGuiKey_GamepadLStickUp, true);
        in.down = Pd(ImGuiKey_GamepadDpadDown, true) || Pd(ImGuiKey_GamepadLStickDown, true);
        in.left = Pd(ImGuiKey_GamepadDpadLeft, true) || Pd(ImGuiKey_GamepadLStickLeft, true);
        in.right = Pd(ImGuiKey_GamepadDpadRight, true) || Pd(ImGuiKey_GamepadLStickRight, true);
        in.ok = Pd(ImGuiKey_GamepadFaceDown, false);
        in.back = Pd(ImGuiKey_GamepadFaceRight, false);
        in.sq = Pd(ImGuiKey_GamepadFaceLeft, false);
        in.tri = Pd(ImGuiKey_GamepadFaceUp, false);
        in.l1 = Pd(ImGuiKey_GamepadL1, true);
        in.r1 = Pd(ImGuiKey_GamepadR1, true);
        in.l2 = Pd(ImGuiKey_GamepadL2, true);
        in.r2 = Pd(ImGuiKey_GamepadR2, true);
        in.start = Pd(ImGuiKey_GamepadStart, false);
        ImGuiKeyData *rl = ImGui::GetKeyData(ImGui::GetCurrentContext(), ImGuiKey_GamepadRStickLeft);
        ImGuiKeyData *rr = ImGui::GetKeyData(ImGui::GetCurrentContext(), ImGuiKey_GamepadRStickRight);
        in.turn = (rr != NULL ? rr->AnalogValue : 0.0f) - (rl != NULL ? rl->AnalogValue : 0.0f);
        ImGuiIO &io = ImGui::GetIO();
        in.up |= K(ImGuiKey_UpArrow, true);
        in.down |= K(ImGuiKey_DownArrow, true);
        in.left |= K(ImGuiKey_LeftArrow, true);
        in.right |= K(ImGuiKey_RightArrow, true);
        in.enter = K(ImGuiKey_Enter, false) || K(ImGuiKey_KeypadEnter, false);
        in.esc = K(ImGuiKey_Escape, false);
        in.backspace = K(ImGuiKey_Backspace, true);
        in.sq |= K(ImGuiKey_Delete, false);
        in.tri |= K(ImGuiKey_F2, false);
        in.l1 |= K(ImGuiKey_PageUp, true);
        in.r1 |= K(ImGuiKey_PageDown, true);
        in.l2 |= K(ImGuiKey_Home, true);
        in.r2 |= K(ImGuiKey_End, true);
        if (io.KeyCtrl && K(ImGuiKey_S, false)) { in.start = true; }
        if (!sTyping) {
            in.ok |= in.enter || K(ImGuiKey_Space, false);
            in.back |= in.esc || in.backspace;
            in.sq |= K(ImGuiKey_F, false);
            in.tri |= K(ImGuiKey_R, false);
            in.l1 |= K(ImGuiKey_Q, true);
            in.r1 |= K(ImGuiKey_E, true);
            in.l2 |= K(ImGuiKey_Z, true);
            in.r2 |= K(ImGuiKey_C, true);
            if (K(ImGuiKey_A, true)) { in.turn -= 4.0f; }
            if (K(ImGuiKey_D, true)) { in.turn += 4.0f; }
        }
        if (pad) { sKeyboardLast = false; }
        if (kb) { sKeyboardLast = true; }
    }
    const char *script = getenv("BT3_STORY_SCRIPT");
    while (script != NULL) { // "<frame>:<button>[,...]" from the screens' opening (testing)
        const char *p = script;
        for (int i = 0; i < sScriptAt && p != NULL; i++) {
            p = strchr(p, ',');
            p = p != NULL ? p + 1 : NULL;
        }
        if (p == NULL || atoi(p) > sFrames) { break; }
        const char *b = strchr(p, ':');
        sScriptAt++;
        if (b == NULL) { continue; }
        b++;
        std::string what(b, strcspn(b, ","));
        fprintf(stderr, "bt3: story: script %d: %s\n", sFrames, what.c_str());
        if (what == "up") { in.up = true; } else if (what == "down") { in.down = true; } else if (what == "left") { in.left = true; }
        else if (what == "right") { in.right = true; } else if (what == "ok") { in.ok = in.enter = true; } else if (what == "back") { in.back = in.esc = true; }
        else if (what == "sq") { in.sq = true; } else if (what == "tri") { in.tri = true; } else if (what == "l1") { in.l1 = true; }
        else if (what == "r1") { in.r1 = true; } else if (what == "l2") { in.l2 = true; } else if (what == "r2") { in.r2 = true; }
        else if (what == "start") { in.start = true; }
        else if (what.compare(0, 5, "type=") == 0) { for (char c : what.substr(5)) { ImGui::GetIO().AddInputCharacter((unsigned)(c == '_' ? ' ' : c)); } }
    }
    sIn = in;
}

// =============================================================================================== lists
struct Row {
    std::string label, value, sub, tip;
    bool sec = false, locked = false;
    int face = -1;
    int model = -1, modelCostume = 0; // the character the platform shows while the cursor is on the row
    std::function<void()> ok, sq, tri, l2, r2;
    std::function<void(int)> lr;
    const char *sqLabel = NULL, *triLabel = NULL, *lrLabel = NULL, *okLabel = NULL, *l2Label = NULL;
};
Row sec(const std::string &t) { Row r; r.sec = true; r.label = t; return r; }

int draw_rows(ImVec2 a, ImVec2 b, const std::vector<Row> &rows, int cur, int &top, bool active) {
    float rh = S(40), gap = S(4), pad = S(8);
    int fits = (int)((b.y - a.y - pad * 2) / (rh + gap));
    if (fits < 1) { fits = 1; }
    if (cur < top) { top = cur; }
    if (cur >= top + fits) { top = cur - fits + 1; }
    if (top > 0 && top > (int)rows.size() - fits) { top = (int)rows.size() - fits; }
    if (top < 0) { top = 0; }
    if (cur > 0 && cur - 1 < top && rows[(size_t)cur - 1].sec) { top = cur - 1; }
    int clicked = -1;
    sDl->PushClipRect(a, b, true);
    float y = a.y + pad;
    float labelW = (b.x - a.x) * 0.36f;
    for (int i = top; i < (int)rows.size() && i < top + fits; i++) {
        const Row &r = rows[(size_t)i];
        ImVec2 ra(a.x + pad, y), rb(b.x - pad, y + rh);
        if (r.sec) {
            float size = S(15), w = text_w(sBold, size, r.label.c_str()) + S(32);
            ImVec2 sa(ra.x + S(4), y + S(10)), sb(ra.x + S(4) + w, y + rh - S(4));
            grad(sa, sb, kGold1, kGold2, (sb.y - sa.y) * 0.5f);
            sDl->AddText(sBold, size, ImVec2(sa.x + S(16), sa.y + (sb.y - sa.y - size) * 0.5f), IM_COL32(74, 26, 0, 255), r.label.c_str());
            y += rh + gap;
            continue;
        }
        bool sel = i == cur && active;
        if (sel) { grad(ra, rb, kGold1, kGold2, rh * 0.5f); }
        else { sDl->AddRectFilled(ra, rb, IM_COL32(255, 255, 255, r.locked ? 8 : 16), rh * 0.5f); }
        float x = ra.x + S(14);
        float size = S(18);
        ImU32 ink = r.locked ? IM_COL32(170, 160, 200, 200) : sel ? kDark : kInk;
        if (r.face >= 0) {
            float fs = rh - S(8);
            face_at(r.face, ImVec2(x - S(6), y + S(4)), ImVec2(x - S(6) + fs, y + S(4) + fs), fs * 0.5f);
            x += fs + S(2);
        }
        bool wide = r.value.empty() && r.sub.empty();
        text(sFont, size, ImVec2(x, y + (rh - size) * 0.5f), ink, r.label, wide ? rb.x - x - S(16) : ra.x + labelW - x);
        float vx = ra.x + labelW + S(8), vmax = rb.x - vx - S(16);
        float subSize = S(13);
        float subW = r.sub.empty() ? 0 : std::min(text_w(sFont, subSize, r.sub.c_str()) + S(10), vmax * 0.45f);
        text(sBold, size, ImVec2(vx, y + (rh - size) * 0.5f), sel ? kDark : r.locked ? ink : kInk, r.value, vmax - subW);
        if (!r.sub.empty()) {
            text(sFont, subSize, ImVec2(rb.x - S(16) - subW + S(10), y + (rh - subSize) * 0.5f), sel ? IM_COL32(107, 58, 0, 255) : kDim, r.sub, subW - S(10));
        }
        if (r.lr && sel) {
            ImU32 c = IM_COL32(107, 58, 0, 255);
            float mx = vx - S(10), my = y + rh * 0.5f, k = S(5);
            sDl->AddTriangleFilled(ImVec2(mx - k, my), ImVec2(mx + k * 0.3f, my - k), ImVec2(mx + k * 0.3f, my + k), c);
        }
        if (ImGui::IsMouseClicked(0) && ImGui::IsMouseHoveringRect(ra, rb) && !ImGui::GetIO().WantCaptureMouse) { clicked = i; }
        y += rh + gap;
    }
    sDl->PopClipRect();
    if ((int)rows.size() > fits) {
        float h = b.y - a.y - pad * 2, k = h * fits / rows.size(), at = a.y + pad + h * top / rows.size();
        sDl->AddRectFilled(ImVec2(b.x - S(7), at), ImVec2(b.x - S(3), at + k), IM_COL32(245, 197, 66, 255), S(2));
    }
    return clicked;
}
void move_cursor(const std::vector<Row> &rows, int &cur, int d) {
    int n = (int)rows.size(), c = cur;
    for (int i = 0; i < n; i++) {
        c = (c + d + n) % n;
        if (!rows[(size_t)c].sec) { break; }
    }
    if (c != cur) { cur = c; sound(0); }
}
void fix_cursor(const std::vector<Row> &rows, int &cur) {
    if (rows.empty()) { cur = 0; return; }
    if (cur >= (int)rows.size()) { cur = (int)rows.size() - 1; }
    if (cur < 0) { cur = 0; }
    if (rows[(size_t)cur].sec) { int c = cur; move_cursor(rows, c, 1); cur = c; }
}
void list_input(std::vector<Row> &rows, int &cur, int clicked) {
    fix_cursor(rows, cur);
    if (rows.empty()) { return; }
    if (sIn.up) { move_cursor(rows, cur, -1); }
    if (sIn.down) { move_cursor(rows, cur, 1); }
    if (clicked >= 0 && !rows[(size_t)clicked].sec) { cur = clicked; sIn.ok = true; }
    Row &r = rows[(size_t)cur];
    if (r.locked) { if (sIn.ok) { sound(2); } return; }
    if (sIn.ok && r.ok) { sound(1); std::function<void()> f = r.ok; f(); return; }
    if ((sIn.left || sIn.right) && r.lr) { sound(0); r.lr(sIn.right ? 1 : -1); return; }
    if (sIn.sq && r.sq) { std::function<void()> f = r.sq; f(); return; }
    if (sIn.tri && r.tri) { std::function<void()> f = r.tri; f(); return; }
    if (sIn.l2 && r.l2) { std::function<void()> f = r.l2; f(); return; }
    if (sIn.r2 && r.r2) { std::function<void()> f = r.r2; f(); return; }
}

// ---- the screens over the page: a list, the keyboard, the character grid, a question
enum { OV_LIST, OV_KEYS, OV_GRID };
struct Overlay {
    int kind = OV_LIST;
    int id = 0;
    std::string title, text;
    std::vector<Row> rows;
    int cur = 0, top = 0;
    size_t max = 63;
    std::function<void(const std::string &)> typed;
    std::function<void(int)> picked;
    int kr = 0, kc = 0;
    int game = 0;       // the grid: 0 all, 1 the disc's, 2 added
    std::string search;
};
std::vector<Overlay> sOv;
int sOvIds;
void ov_push(Overlay &o) { o.id = ++sOvIds; sOv.push_back(o); sound(1); }
void ov_close(void) { if (!sOv.empty()) { sOv.pop_back(); } }
void open_list(const std::string &title, const std::vector<Row> &rows, int cur = 0, const std::string &text = "") {
    Overlay o;
    o.kind = OV_LIST;
    o.title = title;
    o.rows = rows;
    o.text = text;
    o.cur = cur;
    ov_push(o);
}
void open_keys(const std::string &title, const std::string &text, size_t max, std::function<void(const std::string &)> typed) {
    Overlay o;
    o.kind = OV_KEYS;
    o.title = title;
    o.text = text;
    o.max = max;
    o.typed = typed;
    ov_push(o);
}
void open_grid(const std::string &title, int now, std::function<void(int)> picked) {
    Overlay o;
    o.kind = OV_GRID;
    o.title = title;
    o.picked = picked;
    charas_read();
    for (size_t i = 0; i < sCharas.size(); i++) { if (sCharas[i].id == now) { o.cur = (int)i; } }
    ov_push(o);
}
void confirm(const std::string &question, std::function<void()> yes) {
    std::vector<Row> rows(2);
    rows[0].label = "Yes";
    rows[0].ok = [yes] { ov_close(); yes(); };
    rows[1].label = "No";
    rows[1].ok = [] { ov_close(); };
    open_list(question, rows, 1);
}

ImVec2 sBoxA, sBoxB;
void ov_title(const Overlay &o) {
    outlined(sBold, S(28), ImVec2(sBoxA.x + S(18), sBoxA.y + S(12)), IM_COL32(255, 210, 63, 255), IM_COL32(90, 29, 0, 255), o.title, S(1.5f));
}
void list_frame(Overlay &o, std::vector<Prompt> &ps) {
    float top0 = sBoxA.y + S(54);
    if (!o.text.empty()) {
        sDl->AddText(sFont, S(16), ImVec2(sBoxA.x + S(20), top0), kInk, o.text.c_str(), NULL, sBoxB.x - sBoxA.x - S(40));
        top0 += S(30);
    }
    int clicked = draw_rows(ImVec2(sBoxA.x + S(10), top0), ImVec2(sBoxB.x - S(10), sBoxB.y - S(10)), o.rows, o.cur, o.top, true);
    ps.push_back({B_CROSS, "Choose"});
    if (o.cur < (int)o.rows.size() && o.rows[(size_t)o.cur].sq) { ps.push_back({B_SQUARE, o.rows[(size_t)o.cur].sqLabel ? o.rows[(size_t)o.cur].sqLabel : ""}); }
    ps.push_back({B_CIRCLE, "Back"});
    if (sIn.back) { ov_close(); sound(2); return; }
    std::vector<Row> rows = o.rows;
    int cur = o.cur, id = o.id;
    list_input(rows, cur, clicked);
    if (!sOv.empty() && sOv.back().id == id) { sOv.back().cur = cur; }
}
const char *const kKeyRows[6] = {"ABCDEFGHIJKLM", "NOPQRSTUVWXYZ", "abcdefghijklm", "nopqrstuvwxyz", "0123456789.,'", "-!?&()+:/#%*~"};
void keys_frame(Overlay &o, std::vector<Prompt> &ps) {
    ImGuiIO &io = ImGui::GetIO();
    auto pop_char = [&o] {
        if (o.text.empty()) { return; }
        o.text.pop_back();
        while (!o.text.empty() && ((unsigned char)o.text.back() & 0xC0) == 0x80) { o.text.pop_back(); }
    };
    auto put = [&o](unsigned c) { if (o.text.size() < o.max) { utf8_put(o.text, c); } };
    for (int i = 0; i < io.InputQueueCharacters.Size; i++) {
        if (io.InputQueueCharacters[i] >= 32) { put(io.InputQueueCharacters[i]); }
    }
    if (sIn.backspace) { pop_char(); }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) { // (pasted text)
        const char *clip = ImGui::GetClipboardText();
        for (const char *p = clip; p != NULL && *p; p++) { if ((unsigned char)*p >= 32 || *p == '\n') { if (o.text.size() < o.max) { o.text += *p == '\n' ? ' ' : *p; } } }
    }
    float fy = sBoxA.y + S(60);
    bool tall = o.max > 80;
    ImVec2 fa(sBoxA.x + S(20), fy), fb(sBoxB.x - S(20), fy + (tall ? S(96) : S(46)));
    sDl->AddRectFilled(fa, fb, IM_COL32(0, 0, 0, 90), S(8));
    sDl->AddRect(fa, fb, IM_COL32(169, 147, 255, 255), S(8), 0, S(2));
    std::string shown = o.text + ((sFrames / 25) % 2 ? "|" : " ");
    if (tall) {
        sDl->PushClipRect(fa, fb, true);
        ImVec2 ts = sBold->CalcTextSizeA(S(20), FLT_MAX, fb.x - fa.x - S(24), shown.c_str());
        float y0 = fa.y + S(8) - std::max(0.0f, ts.y - (fb.y - fa.y - S(16)));
        sDl->AddText(sBold, S(20), ImVec2(fa.x + S(12), y0), kInk, shown.c_str(), NULL, fb.x - fa.x - S(24));
        sDl->PopClipRect();
    } else {
        text(sBold, S(24), ImVec2(fa.x + S(12), fa.y + S(10)), kInk, shown, fb.x - fa.x - S(24));
    }
    char count[32];
    snprintf(count, sizeof(count), "%d / %d", utf8_len(o.text), (int)o.max);
    sDl->AddText(sFont, S(13), ImVec2(fb.x - text_w(sFont, S(13), count), fb.y + S(4)), kDim, count);
    float ky = fb.y + S(26), kw = (sBoxB.x - sBoxA.x - S(40)) / 13.0f, kh = tall ? S(38) : S(44);
    int was = o.kr * 100 + o.kc;
    if (sIn.up) { o.kr = (o.kr + 6) % 7; }
    if (sIn.down) { o.kr = (o.kr + 1) % 7; }
    if (o.kr == 6) {
        if (was / 100 != 6) { o.kc = std::min(2, (was % 100) * 3 / 13); }
        if (sIn.left) { o.kc = (o.kc + 2) % 3; }
        if (sIn.right) { o.kc = (o.kc + 1) % 3; }
    } else {
        if (was / 100 == 6) { o.kc = (was % 100) * 13 / 3 + 2; if (o.kc > 12) { o.kc = 12; } }
        if (sIn.left) { o.kc = (o.kc + 12) % 13; }
        if (sIn.right) { o.kc = (o.kc + 1) % 13; }
    }
    if (o.kr * 100 + o.kc != was) { sound(0); }
    int clickedR = -1, clickedC = -1;
    for (int r = 0; r < 6; r++) {
        for (int c = 0; c < 13; c++) {
            ImVec2 a(sBoxA.x + S(20) + c * kw + S(2), ky + r * (kh + S(4))), b(a.x + kw - S(4), a.y + kh);
            bool sel = o.kr == r && o.kc == c;
            if (sel) { grad(a, b, kGold1, kGold2, S(6)); } else { sDl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 22), S(6)); }
            char ch[2] = {kKeyRows[r][c], 0};
            float size = S(20);
            sDl->AddText(sBold, size, ImVec2((a.x + b.x - text_w(sBold, size, ch)) * 0.5f, a.y + (kh - size) * 0.5f), sel ? kDark : kInk, ch);
            if (ImGui::IsMouseClicked(0) && ImGui::IsMouseHoveringRect(a, b)) { clickedR = r; clickedC = c; }
        }
    }
    static const char *const kSpecial[3] = {"SPACE", "DELETE", "OK"};
    for (int c = 0; c < 3; c++) {
        float w = (sBoxB.x - sBoxA.x - S(40)) / 3.0f;
        ImVec2 a(sBoxA.x + S(20) + c * w + S(2), ky + 6 * (kh + S(4))), b(a.x + w - S(4), a.y + kh);
        bool sel = o.kr == 6 && o.kc == c;
        if (sel) { grad(a, b, kGold1, kGold2, S(10)); } else { grad(a, b, IM_COL32(125, 100, 200, 255), IM_COL32(70, 40, 130, 255), S(10)); }
        float size = S(18);
        sDl->AddText(sBold, size, ImVec2((a.x + b.x - text_w(sBold, size, kSpecial[c])) * 0.5f, a.y + (kh - size) * 0.5f), sel ? kDark : kInk, kSpecial[c]);
        if (ImGui::IsMouseClicked(0) && ImGui::IsMouseHoveringRect(a, b)) { clickedR = 6; clickedC = c; }
    }
    bool okPad = sIn.ok && !sIn.enter;
    if (clickedR >= 0) { o.kr = clickedR; o.kc = clickedC; okPad = true; }
    bool done = sIn.enter || sIn.start;
    if (okPad) {
        if (o.kr < 6) { put((unsigned char)kKeyRows[o.kr][o.kc]); sound(0); }
        else if (o.kc == 0) { put(' '); sound(0); }
        else if (o.kc == 1) { pop_char(); sound(0); }
        else { done = true; }
    }
    if (sIn.sq) { pop_char(); sound(0); }
    if (sIn.tri) { put(' '); sound(0); }
    ps.push_back({B_CROSS, "Type"});
    ps.push_back({B_SQUARE, "Delete"});
    ps.push_back({B_TRIANGLE, "Space"});
    ps.push_back({B_START, "OK"});
    ps.push_back({B_CIRCLE, "Cancel"});
    if (done) {
        std::string t = o.text;
        while (!t.empty() && t.back() == ' ') { t.pop_back(); }
        while (!t.empty() && t[0] == ' ') { t.erase(0, 1); }
        std::function<void(const std::string &)> f = o.typed;
        ov_close();
        sound(1);
        f(t);
        return;
    }
    if (sIn.esc || (sIn.back && !sIn.backspace)) { ov_close(); sound(2); }
}
int sGridModel = -1; // the character the grid's cursor is on (the platform shows it)
void grid_frame(Overlay &o, std::vector<Prompt> &ps) {
    charas_read();
    std::vector<const Chara *> items;
    for (const Chara &c : sCharas) {
        if ((o.game == 1 && c.added) || (o.game == 2 && !c.added)) { continue; }
        if (!o.search.empty()) {
            std::string n = c.name, q = o.search;
            for (char &ch : n) { ch = (char)SDL_tolower((unsigned char)ch); }
            for (char &ch : q) { ch = (char)SDL_tolower((unsigned char)ch); }
            if (n.find(q) == std::string::npos) { continue; }
        }
        items.push_back(&c);
    }
    float top0 = sBoxA.y + S(56);
    float x = sBoxA.x + S(18);
    bool games = Port_CharaCount() > 0;
    if (games) {
        static const char *const kG[3] = {"ALL", "BT3", "ADDED"};
        for (int g = 0; g < 3; g++) {
            float w = text_w(sFont, S(14), kG[g]) + S(24), h = S(24);
            ImVec2 a(x, top0), b(x + w, top0 + h);
            if (g == o.game) { grad(a, b, kGold1, kGold2, h * 0.5f); } else { grad(a, b, IM_COL32(198, 181, 245, 255), IM_COL32(125, 100, 200, 255), h * 0.5f); }
            sDl->AddText(sFont, S(14), ImVec2(x + S(12), top0 + (h - S(14)) * 0.5f), g == o.game ? IM_COL32(74, 26, 0, 255) : IM_COL32(43, 22, 96, 255), kG[g]);
            x += w + S(6);
        }
        x += S(10);
    }
    std::string s = o.search.empty() ? "type to search" : "search: " + o.search + ((sFrames / 20) % 2 ? "_" : " ");
    sDl->AddText(sFont, S(15), ImVec2(x, top0 + S(3)), o.search.empty() ? kDim : kInk, s.c_str());
    ImVec2 ga(sBoxA.x + S(14), top0 + S(34)), gb(sBoxB.x - S(14), sBoxB.y - S(12));
    float cw = S(104), ch = S(122);
    int cols = std::max(1, (int)((gb.x - ga.x) / cw));
    cw = (gb.x - ga.x) / cols;
    int rowsFit = std::max(1, (int)((gb.y - ga.y) / ch));
    int n = (int)items.size();
    if (o.cur >= n) { o.cur = n ? n - 1 : 0; }
    int was = o.cur;
    if (sIn.left && o.cur > 0) { o.cur--; }
    if (sIn.right && o.cur < n - 1) { o.cur++; }
    if (sIn.up && o.cur >= cols) { o.cur -= cols; }
    if (sIn.down && o.cur + cols < n) { o.cur += cols; } else if (sIn.down && o.cur / cols < (n - 1) / cols) { o.cur = n - 1; }
    if (o.cur != was) { sound(0); }
    int row = o.cur / cols;
    if (row < o.top) { o.top = row; }
    if (row >= o.top + rowsFit) { o.top = row - rowsFit + 1; }
    int clicked = -1;
    sDl->PushClipRect(ga, gb, true);
    for (int i = o.top * cols; i < n && i < (o.top + rowsFit) * cols; i++) {
        int cx = i % cols, cy = i / cols - o.top;
        ImVec2 a(ga.x + cx * cw + S(3), ga.y + cy * ch + S(3)), b(ga.x + (cx + 1) * cw - S(3), ga.y + (cy + 1) * ch - S(3));
        bool sel = i == o.cur;
        if (sel) { grad(a, b, IM_COL32(255, 207, 77, 255), IM_COL32(232, 138, 0, 255), S(8)); }
        else { grad(a, b, IM_COL32(90, 63, 168, 255), IM_COL32(44, 27, 98, 255), S(8)); }
        sDl->AddRect(a, b, sel ? IM_COL32(255, 226, 122, 255) : IM_COL32(143, 122, 224, 255), S(8), 0, S(2));
        float fs = std::min((b.x - a.x) - S(14), ch - S(42));
        ImVec2 fa((a.x + b.x - fs) * 0.5f, a.y + S(5));
        face_at(items[(size_t)i]->id, fa, ImVec2(fa.x + fs, fa.y + fs), S(6));
        sDl->PushClipRect(a, b, true);
        sDl->AddText(sFont, S(12.5f), ImVec2(a.x + S(5), fa.y + fs + S(2)), sel ? kDark : kInk, items[(size_t)i]->name.c_str(), NULL, b.x - a.x - S(10));
        sDl->PopClipRect();
        if (ImGui::IsMouseClicked(0) && ImGui::IsMouseHoveringRect(a, b)) { clicked = i; }
    }
    sDl->PopClipRect();
    if (clicked >= 0) { o.cur = clicked; sIn.ok = true; }
    sGridModel = n > 0 ? items[(size_t)o.cur]->id : -1;
    ImGuiIO &io = ImGui::GetIO();
    for (int i = 0; i < io.InputQueueCharacters.Size; i++) {
        ImWchar c = io.InputQueueCharacters[i];
        if (c >= 32 && o.search.size() < 30) { utf8_put(o.search, c); o.cur = 0; o.top = 0; }
    }
    if (sIn.backspace && !o.search.empty()) {
        o.search.pop_back();
        o.cur = 0;
        sIn.back = false;
    } else if (sIn.backspace) {
        sIn.back = true;
    }
    if (sIn.esc) { sIn.back = true; }
    if (sIn.enter) { sIn.ok = true; }
    if (games && (sIn.l1 || sIn.r1)) { o.game = (o.game + (sIn.r1 ? 1 : 2)) % 3; o.cur = 0; o.top = 0; sound(0); }
    ps.push_back({B_CROSS, "Choose"});
    ps.push_back({B_CIRCLE, "Back"});
    if (games) { ps.push_back({B_L1R1, "BT3 / added"}); }
    if (sIn.back) { ov_close(); sound(2); return; }
    if (sIn.ok && n > 0) {
        int id = items[(size_t)o.cur]->id;
        std::function<void(int)> f = o.picked;
        ov_close();
        sound(1);
        f(id);
    }
}

// =============================================================================================== the screens
enum { SC_HOME, SC_SAGA, SC_EPISODE, SC_SCENE, SC_LINE, SC_FIGHTLINES, SC_RESULT };
struct Page { int screen; int episode; std::string scene; int line; int cur, top; };
std::vector<Page> sPages;     // the editor's way down (the last is up)
bool sDirty;                  // the saga in the editor has changes not saved
int sSeenGen, sSeenResult;
int sResult;                  // the episode's result shown (SC_RESULT): 1 won, 2 lost, 3 left
int sResultNext;              // what Continue does: the next episode, -1 the end of the saga, -2 again
int sHomeCur = 1, sHomeTop;
int sModel = -1, sModelCostume;
std::string sTip;

Page &page(void) { return sPages.back(); }
void push_page(int screen, int episode = -1, const std::string &scene = "", int line = -1) {
    Page p;
    p.screen = screen;
    p.episode = episode;
    p.scene = scene;
    p.line = line;
    p.cur = 0;
    p.top = 0;
    sPages.push_back(p);
}
Saga *saga(void) { return sSaga >= 0 && sSaga < (int)sSagas.size() ? &sSagas[(size_t)sSaga] : NULL; }
J &doc(void) { static J none; Saga *s = saga(); return s != NULL ? s->doc : none; }
J &episodes(void) { return doc().list("episodes"); }
J &episode(int i) { static J none; J &l = episodes(); return i >= 0 && i < (int)l.a.size() ? l.a[(size_t)i] : none; }
J &scene_of(int ep, const std::string &key) { return episode(ep).at(key.c_str()); }
J &lines_of(int ep, const std::string &key) { return key == "fight" ? episode(ep).at("battle").list("lines") : scene_of(ep, key).list("lines"); }
void touch(void) { sDirty = true; }

J new_line(const std::string &speaker, const std::string &text) {
    J l = J::obj();
    l.set("speaker", J::str(speaker));
    l.set("text", J::str(text));
    return l;
}
J new_episode(int n) {
    J e = J::obj();
    e.set("title", J::str("Episode " + std::to_string(n)));
    e.set("stage", J::num(0));
    e.set("music", J::num(8));
    J p = J::arr(), q = J::arr();
    J m = J::obj();
    m.set("chara", J::num(3));
    p.push(m);
    J f = J::obj();
    f.set("chara", J::num(31));
    q.push(f);
    e.set("player", p);
    e.set("enemy", q);
    J intro = J::obj();
    intro.set("distance", J::num(2));
    J ls = J::arr();
    ls.push(new_line("enemy", "So you came after all."));
    ls.push(new_line("player", "I'm not going to lose this time!"));
    intro.set("lines", ls);
    e.set("intro", intro);
    J bt = J::obj();
    bt.set("fight", J::boolean(true));
    bt.set("cpu", J::num(1));
    e.set("battle", bt);
    J win = J::obj();
    J wl = J::arr();
    wl.push(new_line("player", "That's how it's done!"));
    win.set("lines", wl);
    e.set("win", win);
    e.set("lose", J::obj());
    e.set("next_win", J::str("next"));
    e.set("next_lose", J::str("retry"));
    return e;
}

bool save_saga(void) {
    Saga *s = saga();
    if (s == NULL) { return false; }
    if (s->builtin) { // (the sample is the game's: a copy of it is the player's)
        s->builtin = false;
        s->file = file_for(s->doc["title"].text("saga"));
    }
    std::string o;
    if (!s->doc.has("format")) { s->doc.set("format", J::num(1)); }
    js_write(s->doc, o);
    o += '\n';
    if (!write_file(sDir + "/" + s->file, o)) {
        notice("The saga could not be saved in " + sDir + "/" + s->file, true);
        return false;
    }
    sDirty = false;
    notice("Saved as " + sDir + "/" + s->file);
    return true;
}

// the episode a saga's player is at, and what they have cleared
J &progress_of(const Saga &s) { return sProgress.at(saga_key(s).c_str()); }
std::string progress_text(const Saga &s) {
    const J &p = sProgress[saga_key(s).c_str()];
    int n = (int)s.doc["episodes"].size();
    if (p["done"].truthy()) { return "Cleared!"; }
    if (p.has("episode")) { return "Episode " + std::to_string((int)p["episode"].number(0) + 1) + " / " + std::to_string(n); }
    return std::to_string(n) + (n == 1 ? " episode" : " episodes");
}

void leave_for(int saga, int ep, const std::string &preview) {
    SDL_LockMutex(sMx);
    sSh.saga = saga;
    sSh.episode = ep;
    sSh.preview = preview;
    sSh.leave = 2;
    SDL_UnlockMutex(sMx);
}
void play_saga(int i, bool fromStart) {
    sSaga = i;
    sPlayMode = true;
    Saga &s = sSagas[(size_t)i];
    if (s.doc["episodes"].size() == 0) { notice("This saga has no episodes yet.", true); return; }
    J &p = progress_of(s);
    if (fromStart || p["done"].truthy() || (int)p["episode"].number(0) >= (int)s.doc["episodes"].size()) {
        p.set("episode", J::num(0));
        p.erase("done");
        p.set("cleared", J::arr());
        progress_save();
    }
    leave_for(i, (int)p["episode"].number(0), "");
}
// where a saga goes after an episode: the next episode's number, -1 the end, -2 the same again
int next_of(const J &ep, int number, bool won) {
    const J &n = ep[won ? "next_win" : "next_lose"];
    if (n.isNum()) { return (int)n.n - 1; }
    std::string t = n.text(won ? "next" : "retry");
    if (t == "end") { return -1; }
    if (t == "retry") { return -2; }
    return number + 1;
}

// ---- the rows of each page
std::string yes_no(bool b) { return b ? "Yes" : "No"; }
void cycle(J &obj, const char *key, int d, int lo, int hi, int step, int def) {
    int v = (int)obj[key].number(def) + d * step;
    if (v < lo) { v = hi; } else if (v > hi) { v = lo; }
    obj.set(key, J::num(v));
    touch();
}
template <typename F> Row text_row(const std::string &label, const std::string &value, const std::string &title, size_t max, F set) {
    Row r;
    r.label = label;
    r.value = value.empty() ? "-" : value;
    r.ok = [title, value, max, set] { open_keys(title, value, max, [set](const std::string &t) { set(t); touch(); }); };
    return r;
}

void rows_home(std::vector<Row> &rows) {
    rows.push_back(sec("MY SAGAS"));
    for (size_t i = 0; i < sSagas.size(); i++) {
        const Saga &s = sSagas[i];
        Row r;
        r.label = s.doc["title"].text("Untitled saga");
        r.value = progress_text(s);
        r.sub = s.builtin ? "sample" : s.doc["author"].text();
        const J &e0 = s.doc["episodes"][0];
        r.face = chara_of(e0["player"][0].isObj() ? e0["player"][0]["chara"] : e0["player"][0]);
        r.model = r.face;
        r.tip = s.doc["description"].text().empty() ? "Cross plays it (from where you left it), Square edits it." : s.doc["description"].text();
        int idx = (int)i;
        r.ok = [idx] { play_saga(idx, false); };
        r.okLabel = "Play";
        r.sq = [idx] { sSaga = idx; sPlayMode = false; sDirty = false; sPages.clear(); push_page(SC_SAGA); sound(1); };
        r.sqLabel = "Edit";
        r.tri = [idx] {
            std::vector<Row> rs(3);
            rs[0].label = "Play from the first episode";
            rs[0].ok = [idx] { ov_close(); play_saga(idx, true); };
            rs[1].label = "Make a copy to edit";
            rs[1].ok = [idx] {
                ov_close();
                Saga c = sSagas[(size_t)idx];
                c.builtin = false;
                c.doc.set("title", J::str(c.doc["title"].text("Saga") + " (copy)"));
                c.file = file_for(c.doc["title"].text());
                sSagas.insert(sSagas.begin(), c);
                sSaga = 0;
                save_saga();
                sSagas.clear();
                sagas_read();
            };
            rs[2].label = sSagas[(size_t)idx].builtin ? "(the sample cannot be deleted)" : "Delete this saga";
            rs[2].locked = sSagas[(size_t)idx].builtin;
            rs[2].ok = [idx] {
                ov_close();
                std::string file = sSagas[(size_t)idx].file;
                confirm("DELETE " + sSagas[(size_t)idx].doc["title"].text() + "?", [file] {
                    std::string from = sDir + "/" + file, to = sDir + "/" + file + ".deleted";
                    remove(to.c_str());
                    rename(from.c_str(), to.c_str()); // (kept beside, renamed: a saga is not lost by one press)
                    notice("Deleted (kept as " + file + ".deleted in the sagas folder)");
                    sagas_read();
                });
            };
            open_list("SAGA", rs);
        };
        r.triLabel = "More";
        rows.push_back(r);
    }
    rows.push_back(sec("MAKE YOUR OWN"));
    Row n;
    n.label = "New saga";
    n.tip = "A saga is episodes: scenes with your own lines, and battles that lead on to what comes next.";
    n.ok = [] {
        open_keys("NAME YOUR SAGA", "", 40, [](const std::string &t) {
            Saga s;
            s.doc = J::obj();
            s.doc.set("format", J::num(1));
            s.doc.set("title", J::str(t.empty() ? "My Saga" : t));
            s.doc.set("author", J::str(""));
            s.doc.set("description", J::str(""));
            J eps = J::arr();
            eps.push(new_episode(1));
            s.doc.set("episodes", eps);
            s.file = file_for(s.doc["title"].text());
            sSagas.insert(sSagas.begin(), s);
            sSaga = 0;
            sPlayMode = false;
            save_saga();
            sPages.clear();
            push_page(SC_SAGA);
        });
    };
    rows.push_back(n);
    Row f;
    f.label = "Sagas folder";
    f.value = sDir;
    f.tip = "Saga files are shared by copying them into this folder (JSON: docs/port/sagas.md). Cross reads it again.";
    f.ok = [] { sagas_read(); notice("Read " + std::to_string(sSagas.size() - 1) + " saga files from " + sDir); };
    rows.push_back(f);
}

void rows_saga(std::vector<Row> &rows) {
    J &d = doc();
    rows.push_back(sec("THE SAGA"));
    rows.push_back(text_row("Title", d["title"].text(), "THE SAGA'S TITLE", 40, [](const std::string &t) { doc().set("title", J::str(t)); }));
    rows.push_back(text_row("Author", d["author"].text(), "WHO MADE IT", 40, [](const std::string &t) { doc().set("author", J::str(t)); }));
    rows.push_back(text_row("Description", d["description"].text(), "WHAT IT IS ABOUT", 200, [](const std::string &t) { doc().set("description", J::str(t)); }));
    rows.back().tip = "Shown on My Sagas.";
    rows.push_back(sec("EPISODES"));
    J &eps = episodes();
    for (size_t i = 0; i < eps.a.size(); i++) {
        const J &e = eps.a[i];
        Row r;
        Team p = team_of(e, "player"), q = team_of(e, "enemy");
        r.label = std::to_string(i + 1) + ". " + e["title"].text("Untitled");
        r.value = (p.chara.empty() ? std::string("?") : short_name(p.chara[0])) + (e["battle"]["fight"].truthy(true) ? " vs " : " & ") +
                  (q.chara.empty() ? std::string("?") : short_name(q.chara[0]));
        r.sub = e["battle"]["fight"].truthy(true) ? "" : "scene";
        r.face = p.chara.empty() ? -1 : p.chara[0];
        r.model = r.face;
        int idx = (int)i;
        r.ok = [idx] { push_page(SC_EPISODE, idx); };
        r.okLabel = "Open";
        r.sq = [idx] { // up one place
            J &l = episodes();
            if (idx > 0) { std::swap(l.a[(size_t)idx], l.a[(size_t)idx - 1]); page().cur--; touch(); sound(0); }
        };
        r.sqLabel = "Move up";
        r.tri = [idx] {
            confirm("DELETE EPISODE " + std::to_string(idx + 1) + "?", [idx] {
                J &l = episodes();
                l.a.erase(l.a.begin() + idx);
                touch();
            });
        };
        r.triLabel = "Delete";
        r.l2 = [idx] { J &l = episodes(); l.a.insert(l.a.begin() + idx + 1, l.a[(size_t)idx]); touch(); notice("Episode copied"); };
        r.l2Label = "Copy";
        r.tip = e["description"].text().empty() ? "Cross opens the episode: its scenes, its battle, where it leads." : e["description"].text();
        rows.push_back(r);
    }
    Row add;
    add.label = "+ Add an episode";
    add.ok = [] { J &l = episodes(); l.push(new_episode((int)l.a.size() + 1)); touch(); push_page(SC_EPISODE, (int)l.a.size() - 1); };
    rows.push_back(add);
    rows.push_back(sec("PLAY"));
    Row play;
    play.label = "Play the saga from the start";
    play.tip = "Plays it as a player would (saved first). Your progress in it is kept apart from the editor's tests.";
    play.ok = [] { if (!sDirty || save_saga()) { play_saga(sSaga, true); } };
    rows.push_back(play);
    Row sv;
    sv.label = "Save";
    sv.value = sDirty ? "not saved" : "saved";
    sv.ok = [] { save_saga(); };
    rows.push_back(sv);
}

std::string team_text(const Team &t) {
    std::string s;
    for (size_t i = 0; i < t.chara.size(); i++) { s += (i ? ", " : "") + short_name(t.chara[i]); }
    return s.empty() ? std::string("nobody") : s;
}
std::string next_text(const J &ep, bool won) {
    const J &n = ep[won ? "next_win" : "next_lose"];
    if (n.isNum()) { return "Episode " + std::to_string((int)n.n); }
    std::string t = n.text(won ? "next" : "retry");
    return t == "end" ? "The end of the saga" : t == "retry" ? "Try again" : "The next episode";
}
void next_cycle(int ep, bool won, int d) {
    J &e = episode(ep);
    int count = (int)episodes().a.size();
    // the choices in order: next, retry (a loss only), end, then episode 1..count
    std::vector<J> ch;
    ch.push_back(J::str("next"));
    if (!won) { ch.push_back(J::str("retry")); }
    ch.push_back(J::str("end"));
    for (int i = 1; i <= count; i++) { ch.push_back(J::num(i)); }
    const J &now = e[won ? "next_win" : "next_lose"];
    int at = 0;
    for (size_t i = 0; i < ch.size(); i++) {
        if ((ch[i].isNum() && now.isNum() && (int)now.n == (int)ch[i].n) || (ch[i].isStr() && now.text(won ? "next" : "retry") == ch[i].s)) { at = (int)i; }
    }
    at = (at + d + (int)ch.size()) % (int)ch.size();
    e.set(won ? "next_win" : "next_lose", ch[(size_t)at]);
    touch();
}
void team_rows(std::vector<Row> &rows, int ep, const char *key, const char *what) {
    J &list = episode(ep).list(key);
    for (size_t i = 0; i < list.a.size(); i++) {
        J &m = list.a[i];
        if (!m.isObj()) { J o = J::obj(); o.set("chara", m); m = o; }
        int c = chara_of(m["chara"]);
        Row r;
        r.label = i == 0 ? std::string(what) + " (leader)" : std::string(what) + " " + std::to_string(i + 1);
        r.value = chara_name(c);
        int costume = (int)m["costume"].number(0);
        r.sub = "costume " + std::to_string(costume + 1);
        r.face = c;
        r.model = c;
        r.modelCostume = costume;
        std::string k = key;
        int idx = (int)i;
        r.ok = [ep, k, idx, c] {
            open_grid("WHO?", c, [ep, k, idx](int id) { episode(ep).list(k.c_str()).a[(size_t)idx].set("chara", chara_ref(id)); touch(); });
        };
        r.okLabel = "Change";
        r.lr = [ep, k, idx, c](int d) {
            const Chara *ch = chara_by_id(c);
            int n = ch != NULL ? std::max(1, ch->costumes) : 4;
            J &mm = episode(ep).list(k.c_str()).a[(size_t)idx];
            mm.set("costume", J::num(((int)mm["costume"].number(0) + d + n) % n));
            touch();
        };
        r.lrLabel = "Costume";
        if (list.a.size() > 1) {
            r.tri = [ep, k, idx] { J &l = episode(ep).list(k.c_str()); l.a.erase(l.a.begin() + idx); touch(); sound(2); };
            r.triLabel = "Remove";
        }
        r.tip = "The first one fights first (and stands in the scenes); the others come in when the one before is beaten.";
        rows.push_back(r);
    }
    if (list.a.size() < 5) {
        Row add;
        add.label = "+ Add to the team";
        std::string k = key;
        add.ok = [ep, k] {
            open_grid("WHO JOINS?", -1, [ep, k](int id) { J m = J::obj(); m.set("chara", chara_ref(id)); episode(ep).list(k.c_str()).push(m); touch(); });
        };
        rows.push_back(add);
    }
}

void rows_episode(std::vector<Row> &rows, int ep) {
    J &e = episode(ep);
    J &bt = e.at("battle");
    rows.push_back(sec("EPISODE " + std::to_string(ep + 1)));
    rows.push_back(text_row("Title", e["title"].text(), "THE EPISODE'S TITLE", 50, [ep](const std::string &t) { episode(ep).set("title", J::str(t)); }));
    rows.push_back(text_row("Description", e["description"].text(), "WHAT HAPPENS (THE TITLE CARD SHOWS IT)", 160, [ep](const std::string &t) { episode(ep).set("description", J::str(t)); }));
    {
        Row r;
        r.label = "Stage";
        int st = stage_of(e["stage"]);
        r.value = stage_name(st);
        r.tip = "Where it all happens: any stage, the ones you added too.";
        r.ok = [ep, st] {
            std::vector<Row> rs;
            std::vector<int> l = stage_list();
            int cur = 0;
            for (size_t i = 0; i < l.size(); i++) {
                Row x;
                x.label = stage_name(l[i]);
                x.sub = l[i] >= PORT_STAGE_FIRST ? "added" : "";
                int id = l[i];
                if (id == st) { cur = (int)i; }
                x.ok = [ep, id] { ov_close(); episode(ep).set("stage", stage_ref(id)); touch(); };
                rs.push_back(x);
            }
            open_list("STAGE", rs, cur);
        };
        r.lr = [ep, st](int d) {
            std::vector<int> l = stage_list();
            int at = 0;
            for (size_t i = 0; i < l.size(); i++) { if (l[i] == st) { at = (int)i; } }
            episode(ep).set("stage", stage_ref(l[(size_t)((at + d + (int)l.size()) % (int)l.size())]));
            touch();
        };
        rows.push_back(r);
        Row m;
        m.label = "Music";
        int mv = (int)e["music"].number(8);
        m.value = song_name(mv);
        m.lr = [ep, mv](int d) {
            std::vector<int> l = song_list();
            int at = 0;
            for (size_t i = 0; i < l.size(); i++) { if (l[i] == mv) { at = (int)i; } }
            episode(ep).set("music", J::num(l[(size_t)((at + d + (int)l.size()) % (int)l.size())]));
            touch();
        };
        rows.push_back(m);
    }
    rows.push_back(sec("YOUR SIDE"));
    team_rows(rows, ep, "player", "Fighter");
    rows.push_back(sec("THE OTHER SIDE"));
    team_rows(rows, ep, "enemy", "Opponent");
    rows.push_back(sec("STORY"));
    static const char *const kScenes[3] = {"intro", "win", "lose"};
    static const char *const kSceneLabel[3] = {"Opening scene", "After a win", "After a loss"};
    for (int k = 0; k < 3; k++) {
        Row r;
        r.label = kSceneLabel[k];
        int n = (int)e[kScenes[k]]["lines"].size();
        r.value = n == 0 ? (k == 0 ? std::string("none") : std::string("the game's own victory pose")) : std::to_string(n) + (n == 1 ? " line" : " lines");
        std::string key = kScenes[k];
        r.ok = [ep, key] { push_page(SC_SCENE, ep, key); };
        r.okLabel = "Open";
        r.sq = [ep, key] { if (!sDirty || save_saga()) { leave_for(sSaga, ep, key); } };
        r.sqLabel = "Preview";
        r.tip = k == 0 ? "Before the battle: the two leaders on the stage. Square shows it as it will play." : k == 1 ? "When the player has won." : "When the player has lost.";
        if (k > 0 && !bt["fight"].truthy(true)) { r.locked = true; r.value = "(no battle)"; }
        rows.push_back(r);
    }
    rows.push_back(sec("BATTLE"));
    bool fight = bt["fight"].truthy(true);
    {
        Row r;
        r.label = "Fight";
        r.value = fight ? "Yes" : "No: the scene alone";
        r.lr = [ep](int) { J &b = episode(ep).at("battle"); b.set("fight", J::boolean(!b["fight"].truthy(true))); touch(); };
        r.ok = [ep] { J &b = episode(ep).at("battle"); b.set("fight", J::boolean(!b["fight"].truthy(true))); touch(); };
        rows.push_back(r);
    }
    if (fight) {
        Row w;
        bool survive = bt["win"].text("ko") == "survive";
        w.label = "To win";
        w.value = survive ? "Hold out until the time is up" : "Knock out the other team";
        w.lr = [ep](int) { J &b = episode(ep).at("battle"); b.set("win", J::str(b["win"].text("ko") == "survive" ? "ko" : "survive")); touch(); };
        rows.push_back(w);
        if (survive) {
            Row s;
            s.label = "Hold out for";
            s.value = std::to_string((int)bt["survive"].number(60)) + " seconds";
            s.lr = [ep](int d) { cycle(episode(ep).at("battle"), "survive", d, 10, 600, 10, 60); };
            rows.push_back(s);
        }
        Row t;
        t.label = "Time limit";
        int tl = (int)bt["time"].number(0);
        t.value = tl == 0 ? std::string("None") : std::to_string(tl) + " seconds";
        t.lr = [ep](int d) { cycle(episode(ep).at("battle"), "time", d, 0, 600, 30, 0); };
        rows.push_back(t);
        if (tl > 0) {
            Row u;
            u.label = "When the time is up";
            std::string tu = bt["time_up"].text("health");
            u.value = tu == "win" ? "You win" : tu == "lose" ? "You lose" : "More health wins";
            u.lr = [ep](int d) {
                static const char *const k[3] = {"health", "win", "lose"};
                J &b = episode(ep).at("battle");
                int at = key_index(k, 3, b["time_up"].text("health"), 0);
                b.set("time_up", J::str(k[(at + d + 3) % 3]));
                touch();
            };
            rows.push_back(u);
        }
        Row ph, eh, pk, ek, cpu;
        ph.label = "Your health";
        ph.value = std::to_string((int)bt["player_health"].number(100)) + "%";
        ph.lr = [ep](int d) { cycle(episode(ep).at("battle"), "player_health", d, 10, 100, 10, 100); };
        eh.label = "Their health";
        eh.value = std::to_string((int)bt["enemy_health"].number(100)) + "%";
        eh.lr = [ep](int d) { cycle(episode(ep).at("battle"), "enemy_health", d, 10, 100, 10, 100); };
        pk.label = "Your ki at the start";
        pk.value = std::to_string((int)bt["player_ki"].number(0)) + "%";
        pk.lr = [ep](int d) { cycle(episode(ep).at("battle"), "player_ki", d, 0, 100, 25, 0); };
        ek.label = "Their ki at the start";
        ek.value = std::to_string((int)bt["enemy_ki"].number(0)) + "%";
        ek.lr = [ep](int d) { cycle(episode(ep).at("battle"), "enemy_ki", d, 0, 100, 25, 0); };
        cpu.label = "Opponent strength";
        cpu.value = kCpuNames[std::max(0, std::min(4, (int)bt["cpu"].number(1)))];
        cpu.lr = [ep](int d) { cycle(episode(ep).at("battle"), "cpu", d, 0, 4, 1, 1); };
        rows.push_back(ph);
        rows.push_back(eh);
        rows.push_back(pk);
        rows.push_back(ek);
        rows.push_back(cpu);
        Row fl;
        fl.label = "Lines during the fight";
        int n = (int)bt["lines"].size();
        fl.value = n == 0 ? std::string("none") : std::to_string(n);
        fl.tip = "Said in a box while they fight: at the start, when health drops, on a transformation...";
        fl.ok = [ep] { push_page(SC_FIGHTLINES, ep, "fight"); };
        rows.push_back(fl);
    }
    rows.push_back(sec("WHAT COMES NEXT"));
    Row nw;
    nw.label = fight ? "If you win" : "Then";
    nw.value = next_text(e, true);
    nw.lr = [ep](int d) { next_cycle(ep, true, d); };
    rows.push_back(nw);
    if (fight) {
        Row nl;
        nl.label = "If you lose";
        nl.value = next_text(e, false);
        nl.lr = [ep](int d) { next_cycle(ep, false, d); };
        nl.tip = "A loss can lead somewhere else: a story of what if.";
        rows.push_back(nl);
    }
    rows.push_back(sec("TRY IT"));
    Row play;
    play.label = "Play this episode";
    play.ok = [ep] { if (!sDirty || save_saga()) { sPlayMode = false; leave_for(sSaga, ep, ""); } };
    rows.push_back(play);
}

std::string speaker_label(const J &l, int ep) {
    std::string sp = l["speaker"].text("player");
    int side, member;
    speaker_of(sp, &side, &member);
    if (side < 0) { return "Narrator"; }
    Team t[2] = {team_of(episode(ep), "player"), team_of(episode(ep), "enemy")};
    int m = member < 0 ? 0 : member;
    std::string who = m < (int)t[side].chara.size() ? short_name(t[side].chara[(size_t)m]) : std::string("?");
    if (l.has("name")) { who = l["name"].text(); }
    return who;
}
int speaker_chara(const J &l, int ep) {
    int side, member;
    speaker_of(l["speaker"].text("player"), &side, &member);
    if (side < 0) { return -1; }
    if (l.has("as")) { return chara_of(l["as"]); }
    Team t = team_of(episode(ep), side ? "enemy" : "player");
    int m = member < 0 ? 0 : member;
    return m < (int)t.chara.size() ? t.chara[(size_t)m] : t.chara.empty() ? -1 : t.chara[0];
}
void line_rows_list(std::vector<Row> &rows, int ep, const std::string &key, bool fightLines) {
    J &ls = lines_of(ep, key);
    for (size_t i = 0; i < ls.a.size(); i++) {
        const J &l = ls.a[i];
        Row r;
        r.label = speaker_label(l, ep);
        r.value = l["text"].text().empty() ? std::string("(no text)") : "\"" + l["text"].text() + "\"";
        if (fightLines) {
            int w = key_index(kWhenKeys, WHEN_COUNT, l["when"].text("start"), 0);
            r.sub = kWhenNames[w];
            if (w == WHEN_ENEMY_HP || w == WHEN_PLAYER_HP) { r.sub += " " + std::to_string((int)l["value"].number(50)) + "%"; }
            if (w == WHEN_TIME) { r.sub = std::to_string((int)l["value"].number(30)) + " s"; }
        } else if (!l["voice"].nul()) {
            r.sub = "voice";
        }
        r.face = speaker_chara(l, ep);
        r.model = r.face;
        int idx = (int)i;
        r.ok = [ep, key, idx] { push_page(SC_LINE, ep, key, idx); };
        r.okLabel = "Edit";
        r.sq = [ep, key, idx] { // a new line after this one, the other one speaking
            J &l2 = lines_of(ep, key);
            std::string sp = l2.a[(size_t)idx]["speaker"].text("player");
            J n = new_line(sp.compare(0, 6, "player") == 0 ? "enemy" : "player", "");
            if (key == "fight") { n.set("when", J::str("enemy_health")); n.set("value", J::num(50)); }
            l2.a.insert(l2.a.begin() + idx + 1, n);
            touch();
            page().cur++;
            sound(1);
        };
        r.sqLabel = "New line after";
        r.tri = [ep, key, idx] { J &l2 = lines_of(ep, key); l2.a.erase(l2.a.begin() + idx); touch(); sound(2); };
        r.triLabel = "Delete";
        r.l2 = [ep, key, idx] { J &l2 = lines_of(ep, key); if (idx > 0) { std::swap(l2.a[(size_t)idx], l2.a[(size_t)idx - 1]); page().cur--; touch(); sound(0); } };
        r.l2Label = "Move up";
        r.r2 = [ep, key, idx] { J &l2 = lines_of(ep, key); if (idx + 1 < (int)l2.a.size()) { std::swap(l2.a[(size_t)idx], l2.a[(size_t)idx + 1]); page().cur++; touch(); sound(0); } };
        rows.push_back(r);
    }
    Row add;
    add.label = "+ Add a line";
    add.ok = [ep, key] {
        J &l2 = lines_of(ep, key);
        std::string sp = l2.a.empty() ? "player" : l2.a.back()["speaker"].text("player").compare(0, 6, "player") == 0 ? "enemy" : "player";
        J n = new_line(sp, "");
        if (key == "fight") { n.set("when", J::str("start")); }
        l2.push(n);
        touch();
        push_page(SC_LINE, ep, key, (int)l2.a.size() - 1);
        open_keys("WHAT IS SAID?", "", 240, [ep, key](const std::string &t) {
            J &l3 = lines_of(ep, key);
            if (!l3.a.empty()) { l3.a.back().set("text", J::str(t)); }
        });
    };
    rows.push_back(add);
}
void rows_scene(std::vector<Row> &rows, int ep, const std::string &key) {
    J &sc = scene_of(ep, key);
    static const char *const kNames[] = {"intro", "Opening scene", "win", "After a win", "lose", "After a loss"};
    std::string title;
    for (int i = 0; i < 6; i += 2) { if (key == kNames[i]) { title = kNames[i + 1]; } }
    rows.push_back(sec(title));
    Row d;
    d.label = "How far apart";
    d.value = kDistNames[std::max(0, std::min(3, (int)sc["distance"].number(2)))];
    d.lr = [ep, key](int dd) { cycle(scene_of(ep, key), "distance", dd, 0, 3, 1, 2); };
    rows.push_back(d);
    Row hp, he;
    hp.label = "Your leader on the stage";
    hp.value = sc["hide_player"].truthy() ? "Not there (comes in later)" : "There";
    hp.lr = [ep, key](int) { J &s = scene_of(ep, key); s.set("hide_player", J::boolean(!s["hide_player"].truthy())); touch(); };
    he.label = "Their leader on the stage";
    he.value = sc["hide_enemy"].truthy() ? "Not there (comes in later)" : "There";
    he.lr = [ep, key](int) { J &s = scene_of(ep, key); s.set("hide_enemy", J::boolean(!s["hide_enemy"].truthy())); touch(); };
    rows.push_back(hp);
    rows.push_back(he);
    rows.push_back(sec("LINES"));
    line_rows_list(rows, ep, key, false);
    rows.push_back(sec("SEE IT"));
    Row pv;
    pv.label = "Preview this scene";
    pv.tip = "Plays the scene on its stage with its fighters, as the saga will (saved first).";
    pv.ok = [ep, key] { if (!sDirty || save_saga()) { leave_for(sSaga, ep, key == "fight" ? "intro" : key); } };
    rows.push_back(pv);
}

void speaker_cycle(J &l, int ep, int d) {
    std::vector<std::string> ch;
    Team t[2] = {team_of(episode(ep), "player"), team_of(episode(ep), "enemy")};
    ch.push_back("player");
    for (size_t i = 1; i < t[0].chara.size(); i++) { ch.push_back("player" + std::to_string(i + 1)); }
    ch.push_back("enemy");
    for (size_t i = 1; i < t[1].chara.size(); i++) { ch.push_back("enemy" + std::to_string(i + 1)); }
    ch.push_back("narrator");
    std::string now = l["speaker"].text("player");
    int at = 0;
    for (size_t i = 0; i < ch.size(); i++) { if (ch[i] == now) { at = (int)i; } }
    l.set("speaker", J::str(ch[(size_t)((at + d + (int)ch.size()) % (int)ch.size())]));
    l.erase("as");
    touch();
}
std::string voice_text(const J &l, int ep) {
    const J &v = l["voice"];
    if (v.nul()) { return "None"; }
    int sc = speaker_chara(l, ep);
    if (v.isObj() && v.has("story")) {
        if (!story_lines_ready()) { return "Dragon History, story " + std::to_string((int)v["story"].number(0)); }
        for (const StoryLine &s : sStoryLines) {
            if (s.story == (int)v["story"].number(0) && s.line == (int)v["line"].number(0)) { return "Dragon History: \"" + s.text + "\""; }
        }
        return "Dragon History, story " + std::to_string((int)v["story"].number(0));
    }
    if (v.text() == "random") { return "A random line of " + short_name(sc); }
    if (v.isNum()) { return short_name(sc) + ": line " + std::to_string((int)v.n); }
    int c = v.has("chara") ? chara_of(v["chara"]) : sc;
    if (v["line"].text() == "random") { return "A random line of " + short_name(c); }
    return short_name(c) + ": line " + std::to_string((int)v["line"].number(0)) + " (" + line_kind((int)v["line"].number(0)) + ")";
}
void hear(int chara, int line) {
    SDL_LockMutex(sMx);
    sSh.voice = chara << 8 | line;
    SDL_UnlockMutex(sMx);
}
void open_voice_list(int ep, const std::string &key, int idx, int chara) {
    std::vector<Row> rs;
    Row none;
    none.label = "No voice";
    none.ok = [ep, key, idx] { ov_close(); lines_of(ep, key).a[(size_t)idx].erase("voice"); touch(); };
    rs.push_back(none);
    Row rnd;
    rnd.label = "A random line of " + short_name(chara);
    rnd.ok = [ep, key, idx, chara] {
        ov_close();
        J v = J::obj();
        v.set("chara", chara_ref(chara));
        v.set("line", J::str("random"));
        lines_of(ep, key).a[(size_t)idx].set("voice", v);
        touch();
    };
    rs.push_back(rnd);
    Row other;
    other.label = "Someone else's voice...";
    other.ok = [ep, key, idx] {
        ov_close();
        open_grid("WHOSE VOICE?", -1, [ep, key, idx](int c) { open_voice_list(ep, key, idx, c); });
    };
    rs.push_back(other);
    if (!story_lines_ready()) { Row w; w.label = "(Dragon History's lines are still being read: open this again in a moment)"; w.locked = true; rs.push_back(w); }
    bool any = false;
    for (const StoryLine &s : story_lines_ready() ? sStoryLines : std::vector<StoryLine>()) {
        if (s.chara != chara) { continue; }
        if (!any) { rs.push_back(sec("WHAT " + short_name(chara) + " SAYS IN DRAGON HISTORY")); any = true; }
        Row r;
        r.label = "\"" + s.text + "\"";
        int story = s.story, line = s.line;
        std::string words = s.text;
        r.ok = [ep, key, idx, story, line, words] {
            ov_close();
            J v = J::obj();
            v.set("story", J::num(story));
            v.set("line", J::num(line));
            J &x = lines_of(ep, key).a[(size_t)idx];
            x.set("voice", v);
            if (x["text"].text().empty()) { x.set("text", J::str(words)); } // (its words too, when the line has none yet)
            touch();
        };
        r.sq = [story, line] {
            SDL_LockMutex(sMx);
            sSh.voice = -2 - (story * 300 + line);
            SDL_UnlockMutex(sMx);
        };
        r.sqLabel = "Hear it";
        r.tip = "A line voiced in Dragon History: Cross takes it (and its words, if the line has none yet).";
        rs.push_back(r);
    }
    rs.push_back(sec(short_name(chara) + "'S BATTLE LINES (SQUARE: HEAR IT)"));
    for (int l : voice_lines(chara)) {
        Row r;
        r.label = "Line " + std::to_string(l);
        r.value = line_kind(l);
        r.ok = [ep, key, idx, chara, l] {
            ov_close();
            J v = J::obj();
            v.set("chara", chara_ref(chara));
            v.set("line", J::num(l));
            lines_of(ep, key).a[(size_t)idx].set("voice", v);
            touch();
        };
        r.sq = [chara, l] { hear(chara, l); };
        r.sqLabel = "Hear it";
        rs.push_back(r);
    }
    open_list("VOICE", rs, 0);
}
void rows_line(std::vector<Row> &rows, int ep, const std::string &key, int idx) {
    J &ls = lines_of(ep, key);
    if (idx < 0 || idx >= (int)ls.a.size()) { rows.push_back(sec("THIS LINE IS GONE: CIRCLE GOES BACK")); return; }
    J &l = ls.a[(size_t)idx];
    bool fightLine = key == "fight";
    rows.push_back(sec(fightLine ? "A LINE DURING THE FIGHT" : "LINE " + std::to_string(idx + 1)));
    if (fightLine) {
        Row w;
        int wi = key_index(kWhenKeys, WHEN_COUNT, l["when"].text("start"), 0);
        w.label = "When";
        w.value = kWhenNames[wi];
        w.lr = [ep, key, idx](int d) {
            J &x = lines_of(ep, key).a[(size_t)idx];
            int at = key_index(kWhenKeys, WHEN_COUNT, x["when"].text("start"), 0);
            x.set("when", J::str(kWhenKeys[(at + d + WHEN_COUNT) % WHEN_COUNT]));
            touch();
        };
        rows.push_back(w);
        if (wi == WHEN_ENEMY_HP || wi == WHEN_PLAYER_HP) {
            Row v;
            v.label = "Health below";
            v.value = std::to_string((int)l["value"].number(50)) + "%";
            v.lr = [ep, key, idx](int d) { cycle(lines_of(ep, key).a[(size_t)idx], "value", d, 10, 90, 10, 50); };
            rows.push_back(v);
        } else if (wi == WHEN_TIME) {
            Row v;
            v.label = "Seconds into the fight";
            v.value = std::to_string((int)l["value"].number(30));
            v.lr = [ep, key, idx](int d) { cycle(lines_of(ep, key).a[(size_t)idx], "value", d, 5, 600, 5, 30); };
            rows.push_back(v);
        }
    }
    Row sp;
    sp.label = "Who speaks";
    sp.value = speaker_label(l, ep);
    std::string spk = l["speaker"].text("player");
    sp.sub = spk == "narrator" ? "" : spk.compare(0, 5, "enemy") == 0 ? "their side" : "your side";
    sp.face = speaker_chara(l, ep);
    sp.model = sp.face;
    sp.lr = [ep, key, idx](int d) { speaker_cycle(lines_of(ep, key).a[(size_t)idx], ep, d); };
    sp.ok = [ep, key, idx] { speaker_cycle(lines_of(ep, key).a[(size_t)idx], ep, 1); };
    sp.tip = "Left / right: who says it. The leaders stand on the stage; a team mate or the narrator is heard and read.";
    rows.push_back(sp);
    rows.push_back(text_row("What is said", l["text"].text(), "WHAT IS SAID?", 240, [ep, key, idx](const std::string &t) { lines_of(ep, key).a[(size_t)idx].set("text", J::str(t)); }));
    rows.back().tip = "Up to 240 letters; the box shows them as they are typed out.";
    Row nm;
    nm.label = "Name in the box";
    nm.value = l.has("name") ? l["name"].text() : "(" + speaker_label(l, ep) + ")";
    nm.ok = [ep, key, idx] {
        open_keys("THE NAME SHOWN (EMPTY: THE CHARACTER'S)", lines_of(ep, key).a[(size_t)idx]["name"].text(), 30, [ep, key, idx](const std::string &t) {
            J &x = lines_of(ep, key).a[(size_t)idx];
            if (t.empty()) { x.erase("name"); } else { x.set("name", J::str(t)); }
        });
    };
    rows.push_back(nm);
    Row vo;
    vo.label = "Voice";
    vo.value = voice_text(l, ep);
    int sc = speaker_chara(l, ep);
    vo.ok = [ep, key, idx, sc] { open_voice_list(ep, key, idx, sc < 0 ? 3 : sc); };
    vo.sq = [ep, key, idx, sc] {
        const J &v = lines_of(ep, key).a[(size_t)idx]["voice"];
        if (v.isObj() && v.has("story")) {
            SDL_LockMutex(sMx);
            sSh.voice = -2 - ((int)v["story"].number(0) * 300 + (int)v["line"].number(0));
            SDL_UnlockMutex(sMx);
            return;
        }
        int c = v.isObj() && v.has("chara") ? chara_of(v["chara"]) : sc;
        int line = v.isObj() ? (int)v["line"].number(-1) : v.isNum() ? (int)v.n : -1;
        if (line < 0) { const std::vector<int> &h = voice_lines(c); if (!h.empty()) { line = h[(size_t)(rand() % (int)h.size())]; } }
        if (line >= 0 && c >= 0) { hear(c, line); }
    };
    vo.sqLabel = "Hear it";
    vo.tip = "One of the character's own spoken lines (lip-synced), someone else's, or a random one each time.";
    rows.push_back(vo);
    if (!fightLine) {
        rows.push_back(sec("CAMERA AND POSES"));
        Row cm;
        int ci = key_index(kCamKeys, CAM_COUNT, l["camera"].text("auto"), CAM_AUTO);
        cm.label = "Camera";
        cm.value = kCamNames[ci];
        cm.lr = [ep, key, idx](int d) {
            J &x = lines_of(ep, key).a[(size_t)idx];
            int at = key_index(kCamKeys, CAM_COUNT, x["camera"].text("auto"), CAM_AUTO);
            x.set("camera", J::str(kCamKeys[(at + d + CAM_COUNT) % CAM_COUNT]));
            touch();
        };
        cm.tip = "Automatic: both at the first line, then close-ups and over the shoulder, in turn.";
        rows.push_back(cm);
        Row mv;
        int mi = key_index(kMoveKeys, MOVE_COUNT, l["move"].text("cut"), MOVE_CUT);
        mv.label = "Camera move";
        mv.value = kMoveNames[mi];
        mv.lr = [ep, key, idx](int d) {
            J &x = lines_of(ep, key).a[(size_t)idx];
            int at = key_index(kMoveKeys, MOVE_COUNT, x["move"].text("cut"), MOVE_CUT);
            x.set("move", J::str(kMoveKeys[(at + d + MOVE_COUNT) % MOVE_COUNT]));
            touch();
        };
        rows.push_back(mv);
        if (mi != MOVE_CUT) {
            Row mt;
            mt.label = "Move time";
            char b[32];
            snprintf(b, sizeof(b), "%.1f seconds", l["move_time"].number(3.0));
            mt.value = b;
            mt.lr = [ep, key, idx](int d) {
                J &x = lines_of(ep, key).a[(size_t)idx];
                double v = x["move_time"].number(3.0) + d * 0.5;
                x.set("move_time", J::num(v < 0.5 ? 0.5 : v > 12 ? 12 : v));
                touch();
            };
            rows.push_back(mt);
        }
        for (int who = 0; who < 2; who++) {
            Row ps;
            const char *k = who ? "listener_pose" : "pose";
            int pi = pose_index(l[k]);
            ps.label = who ? "The other's pose" : "Speaker's pose";
            ps.value = pi >= 0 ? kPoses[pi].name : "Motion " + std::to_string((int)l[k].number(0));
            std::string kk = k;
            ps.lr = [ep, key, idx, kk](int d) {
                J &x = lines_of(ep, key).a[(size_t)idx];
                int at = std::max(0, pose_index(x[kk.c_str()]));
                at = (at + d + kPoseCount) % kPoseCount;
                if (at == 0) { x.erase(kk.c_str()); } else { x.set(kk.c_str(), J::str(kPoses[at].name)); }
                touch();
            };
            ps.ok = [ep, key, idx, kk] {
                std::vector<Row> rs;
                for (int i = 0; i < kPoseCount; i++) {
                    Row r;
                    r.label = kPoses[i].name;
                    r.sub = kPoses[i].motion >= 0 ? "motion " + std::to_string(kPoses[i].motion) : "";
                    r.ok = [ep, key, idx, kk, i] {
                        ov_close();
                        J &x = lines_of(ep, key).a[(size_t)idx];
                        if (i == 0) { x.erase(kk.c_str()); } else { x.set(kk.c_str(), J::str(kPoses[i].name)); }
                        touch();
                    };
                    rs.push_back(r);
                }
                Row num;
                num.label = "Another motion by its number...";
                num.ok = [ep, key, idx, kk] {
                    ov_close();
                    open_keys("THE MOTION'S NUMBER (OF THE CHARACTER'S ANIMATIONS)", "", 4, [ep, key, idx, kk](const std::string &t) {
                        if (!t.empty()) { lines_of(ep, key).a[(size_t)idx].set(kk.c_str(), J::num(atoi(t.c_str()))); }
                    });
                };
                rs.push_back(num);
                open_list("POSE", rs, std::max(0, pose_index(lines_of(ep, key).a[(size_t)idx][kk.c_str()])));
            };
            rows.push_back(ps);
        }
    }
    rows.push_back(sec("EFFECTS"));
    for (int k = 0; k < kFxCount; k++) {
        if (fightLine && !(kFx[k].bit & (FX_SHAKE | FX_BIG_SHAKE | FX_FLASH))) { continue; }
        Row f;
        bool on = false;
        const J &e = l["effects"];
        for (size_t i = 0; i < e.size(); i++) { on |= e[i].text() == kFx[k].key; }
        f.label = kFx[k].name;
        f.value = on ? "On" : "Off";
        std::string fk = kFx[k].key;
        auto toggle = [ep, key, idx, fk](int) {
            J &x = lines_of(ep, key).a[(size_t)idx];
            J &e2 = x.list("effects");
            bool was = false;
            for (size_t i = 0; i < e2.a.size(); i++) { if (e2.a[i].text() == fk) { e2.a.erase(e2.a.begin() + (long)i); was = true; break; } }
            if (!was) { e2.push(J::str(fk)); }
            if (e2.a.empty()) { x.erase("effects"); }
            touch();
        };
        f.lr = toggle;
        f.ok = [toggle] { toggle(1); };
        rows.push_back(f);
    }
    if (!fightLine) {
        rows.push_back(sec("TIMING"));
        Row wt;
        double w = l["wait"].number(0);
        wt.label = "Goes on";
        char b[48];
        if (w <= 0) { snprintf(b, sizeof(b), "When Cross is pressed"); } else { snprintf(b, sizeof(b), "By itself after %.1f s", w); }
        wt.value = b;
        wt.lr = [ep, key, idx](int d) {
            J &x = lines_of(ep, key).a[(size_t)idx];
            double v = x["wait"].number(0) + d * 0.5;
            if (v <= 0) { x.erase("wait"); } else { x.set("wait", J::num(v > 10 ? 10 : v)); }
            touch();
        };
        rows.push_back(wt);
    }
}

// =============================================================================================== the overlay of an episode
// What the battle shows over its picture: the letterbox, the title card, the text box, a line said in the fight.
Shared sView;
float sLetter;           // the letterbox's height, 0..1
int sLastSayGen = -1;
double sSayStart;
float sFlash, sFade;
void wrap_text(ImFont *f, float size, const std::string &t, int letters, float w, ImVec2 at, ImU32 c) {
    // the first `letters` letters of the text, wrapped at w (the whole text's wrapping, so no word jumps a line)
    std::string shown;
    int n = 0;
    for (size_t i = 0; i < t.size(); i++) {
        if (((unsigned char)t[i] & 0xC0) != 0x80) { if (n >= letters) { break; } n++; }
        shown += t[i];
    }
    const char *start = t.c_str(), *end = start + t.size(), *cut = start + shown.size();
    float y = at.y;
    while (start < end && start < cut) {
        const char *brk = f->CalcWordWrapPosition(size, start, end, w);
        if (brk == start) { brk++; }
        const char *stop = brk < cut ? brk : cut;
        sDl->AddText(f, size, ImVec2(at.x + S(2), y + S(2)), IM_COL32(0, 0, 0, 160), start, stop);
        sDl->AddText(f, size, ImVec2(at.x, y), c, start, stop);
        y += size * 1.22f;
        start = brk;
        while (start < end && *start == ' ') { start++; }
    }
}
void overlay_draw(void) {
    SDL_LockMutex(sMx);
    sView = sSh;
    CLine say, hud;
    bool haveSay = sView.say >= 0 && sView.say < (int)sEp.lines.size();
    bool haveHud = sView.hud >= 0 && sView.hud < (int)sEp.lines.size();
    if (haveSay) { say = sEp.lines[(size_t)sView.say]; }
    if (haveHud) { hud = sEp.lines[(size_t)sView.hud]; }
    std::string title = sEp.title, label = sEp.label, desc = sEp.desc, sagaTitle = sEp.sagaTitle;
    if (sSh.flash > 0) { sFlash = sSh.flash / 255.0f; sSh.flash = 0; }
    if (sSh.fadeAlpha > 0) { sFade = sSh.fadeAlpha / 255.0f; sSh.fadeAlpha = 0; }
    SDL_UnlockMutex(sMx);
    frame_setup();
    float dt = ImGui::GetIO().DeltaTime;
    double now = ImGui::GetTime();
    // the letterbox
    float want = sView.letterbox ? 1.0f : 0.0f;
    sLetter += (want - sLetter) * std::min(1.0f, dt * 6.0f);
    if (sLetter > 0.01f) {
        float h = sH * 0.085f * sLetter;
        sDl->AddRectFilled(ImVec2(sX0, sY0), ImVec2(sX0 + sW, sY0 + h), IM_COL32(0, 0, 0, 255));
        sDl->AddRectFilled(ImVec2(sX0, sY0 + sH - h), ImVec2(sX0 + sW, sY0 + sH), IM_COL32(0, 0, 0, 255));
    }
    // the title card
    if (sView.title >= 0) {
        float t = (float)sView.title;
        float in = std::min(1.0f, t / 18.0f), out = t > 150 ? std::max(0.0f, 1.0f - (t - 150) / 20.0f) : 1.0f;
        float a = in * out;
        float slide = (1.0f - in) * S(80);
        ImVec2 c(sX0 + sW * 0.5f, sY0 + sH * 0.45f);
        sDl->AddRectFilled(ImVec2(sX0, c.y - S(90)), ImVec2(sX0 + sW, c.y + S(100)), IM_COL32(0, 0, 0, (int)(150 * a)));
        grad(ImVec2(sX0, c.y - S(92)), ImVec2(sX0 + sW, c.y - S(88)), IM_COL32(255, 211, 77, (int)(255 * a)), IM_COL32(243, 156, 18, (int)(255 * a)), 0);
        grad(ImVec2(sX0, c.y + S(98)), ImVec2(sX0 + sW, c.y + S(102)), IM_COL32(255, 211, 77, (int)(255 * a)), IM_COL32(243, 156, 18, (int)(255 * a)), 0);
        float ls = S(24);
        std::string top = sagaTitle.empty() ? label : sagaTitle + "  -  " + label;
        outlined(sBold, ls, ImVec2(c.x - text_w(sBold, ls, top.c_str()) * 0.5f - slide, c.y - S(76)), IM_COL32(255, 214, 70, (int)(255 * a)), IM_COL32(60, 20, 0, (int)(255 * a)), top, S(1.5f));
        float ts = S(56);
        std::string tt = title.empty() ? label : title;
        float tw = text_w(sBold, ts, tt.c_str());
        if (tw > sW * 0.9f) { ts *= sW * 0.9f / tw; tw = text_w(sBold, ts, tt.c_str()); }
        outlined(sBold, ts, ImVec2(c.x - tw * 0.5f + slide, c.y - S(40)), IM_COL32(255, 255, 255, (int)(255 * a)), IM_COL32(30, 10, 60, (int)(255 * a)), tt, S(3));
        if (!desc.empty()) {
            float ds = S(19), w = std::min(sW * 0.7f, text_w(sFont, ds, desc.c_str()) + S(4));
            ImVec2 dsz = sFont->CalcTextSizeA(ds, FLT_MAX, w, desc.c_str());
            sDl->AddText(sFont, ds, ImVec2(c.x - std::min(w, dsz.x) * 0.5f, c.y + S(36)), IM_COL32(230, 220, 255, (int)(255 * a)), desc.c_str(), NULL, w);
        }
    }
    // the text box
    if (haveSay && (!say.text.empty() || !say.name.empty())) {
        if (sView.sayGen != sLastSayGen) { sLastSayGen = sView.sayGen; sSayStart = now; }
        float k = std::min(1.0f, (float)(now - sSayStart) * 8.0f);
        float bw = std::min(sW * 0.86f, S(1180)), bh = S(178);
        ImVec2 a(sX0 + (sW - bw) * 0.5f, sY0 + sH - bh - S(26) - (1.0f - k) * S(20)), b(a.x + bw, a.y + bh);
        int al = (int)(230 * k);
        grad(a, b, IM_COL32(18, 14, 40, al), IM_COL32(6, 4, 16, al), S(10));
        sDl->AddRect(a, b, IM_COL32(255, 200, 70, (int)(200 * k)), S(10), 0, S(2));
        grad(ImVec2(a.x + S(14), a.y + S(4)), ImVec2(b.x - S(14), a.y + S(7)), IM_COL32(255, 220, 100, (int)(120 * k)), IM_COL32(255, 160, 30, 0), S(1));
        float tx = a.x + S(30);
        if (!say.onStage && say.portrait >= 0) { // (someone not on the stage: their face beside the text)
            float fs = bh - S(36);
            ImVec2 fa(a.x + S(18), a.y + S(20));
            sDl->AddRectFilled(ImVec2(fa.x - S(3), fa.y - S(3)), ImVec2(fa.x + fs + S(3), fa.y + fs + S(3)), IM_COL32(255, 200, 70, al), S(8));
            face_at(say.portrait, fa, ImVec2(fa.x + fs, fa.y + fs), S(6), IM_COL32(255, 255, 255, (int)(255 * k)));
            tx = fa.x + fs + S(20);
        }
        if (!say.name.empty()) { // the name plate on the box's edge
            float ns = S(30), nw = text_w(sBold, ns, say.name.c_str()) + S(50), nh = S(46);
            ImVec2 na(a.x + S(22), a.y - nh * 0.6f), nb(na.x + nw, na.y + nh);
            ImU32 c1 = say.v[PL_SIDE] == 1 ? IM_COL32(240, 90, 70, al) : say.v[PL_SIDE] < 0 ? IM_COL32(150, 150, 170, al) : IM_COL32(255, 196, 60, al);
            ImU32 c2 = say.v[PL_SIDE] == 1 ? IM_COL32(150, 30, 30, al) : say.v[PL_SIDE] < 0 ? IM_COL32(70, 70, 90, al) : IM_COL32(214, 110, 10, al);
            sDl->AddQuadFilled(ImVec2(na.x + S(10), na.y), ImVec2(nb.x, na.y), ImVec2(nb.x - S(10), nb.y), ImVec2(na.x, nb.y), c1);
            grad(ImVec2(na.x + S(8), na.y + nh * 0.55f), ImVec2(nb.x - S(8), nb.y), IM_COL32(0, 0, 0, 0), c2, 0);
            outlined(sBold, ns, ImVec2(na.x + S(22), na.y + (nh - ns) * 0.5f), IM_COL32(255, 255, 255, (int)(255 * k)), IM_COL32(60, 20, 0, (int)(220 * k)), say.name, S(1.5f));
        }
        float size = S(33);
        wrap_text(sFont, size, say.text, sView.shown, b.x - tx - S(30), ImVec2(tx, a.y + S(30)), IM_COL32(255, 255, 255, (int)(255 * k)));
        if (sView.shown >= utf8_len(say.text)) { // done: the prompt to go on
            float bob = sinf((float)now * 6.0f) * S(3);
            ImVec2 m(b.x - S(30), b.y - S(22) + bob);
            sDl->AddTriangleFilled(ImVec2(m.x - S(9), m.y - S(6)), ImVec2(m.x + S(9), m.y - S(6)), ImVec2(m.x, m.y + S(6)), IM_COL32(255, 210, 80, 255));
        }
    }
    // a line said during the fight: a box with the face, at the bottom, on the speaker's side
    if (haveHud) {
        float t = (float)sView.hudFrames;
        float k = std::min(1.0f, t / 10.0f);
        bool right = hud.v[PL_SIDE] == 1;
        float bw = std::min(sW * 0.46f, S(620)), bh = S(96);
        float x = right ? sX0 + sW - bw - S(30) + (1.0f - k) * S(60) : sX0 + S(30) - (1.0f - k) * S(60);
        ImVec2 a(x, sY0 + sH - bh - S(40)), b(a.x + bw, a.y + bh);
        int al = (int)(220 * k);
        grad(a, b, IM_COL32(20, 14, 44, al), IM_COL32(8, 6, 20, al), S(12));
        sDl->AddRect(a, b, right ? IM_COL32(240, 110, 80, al) : IM_COL32(255, 200, 70, al), S(12), 0, S(2));
        float fs = bh - S(16);
        ImVec2 fa = right ? ImVec2(b.x - fs - S(8), a.y + S(8)) : ImVec2(a.x + S(8), a.y + S(8));
        face_at(hud.portrait, fa, ImVec2(fa.x + fs, fa.y + fs), fs * 0.5f, IM_COL32(255, 255, 255, (int)(255 * k)));
        float tx = right ? a.x + S(16) : fa.x + fs + S(14), tw = bw - fs - S(40);
        outlined(sBold, S(19), ImVec2(tx, a.y + S(8)), right ? IM_COL32(255, 160, 130, (int)(255 * k)) : IM_COL32(255, 214, 90, (int)(255 * k)), IM_COL32(0, 0, 0, al), hud.name, S(1));
        sDl->AddText(sFont, S(19), ImVec2(tx, a.y + S(34)), IM_COL32(255, 255, 255, (int)(255 * k)), hud.text.c_str(), NULL, tw);
    }
    // the clock of a battle with a time to it
    if (sView.timer >= 0 && sView.say < 0) {
        char b[48];
        snprintf(b, sizeof(b), "%s %d:%02d", sView.timerSurvive ? "HOLD OUT" : "TIME", sView.timer / 60, sView.timer % 60);
        float size = S(22), w = text_w(sBold, size, b) + S(30), h = S(34);
        ImVec2 a(sX0 + (sW - w) * 0.5f, sY0 + sH * 0.17f), bb(a.x + w, a.y + h);
        sDl->AddRectFilled(a, bb, IM_COL32(10, 6, 30, 190), h * 0.5f);
        sDl->AddRect(a, bb, sView.timer <= 10 ? IM_COL32(255, 90, 70, 255) : IM_COL32(255, 200, 70, 255), h * 0.5f, 0, S(2));
        outlined(sBold, size, ImVec2(a.x + S(15), a.y + (h - size) * 0.5f), kInk, IM_COL32(0, 0, 0, 255), b, S(1));
    }
    // flashes and fades
    if (sFlash > 0.0f) {
        sDl->AddRectFilled(ImVec2(sX0, sY0), ImVec2(sX0 + sW, sY0 + sH), IM_COL32(255, 255, 255, (int)(255 * sFlash)));
        sFlash -= dt * 2.5f;
    }
    if (sFade > 0.0f) {
        sDl->AddRectFilled(ImVec2(sX0, sY0), ImVec2(sX0 + sW, sY0 + sH), IM_COL32(0, 0, 0, (int)(255 * sFade)));
        sFade -= dt * 0.8f;
    }
}

// =============================================================================================== a frame of the screens
void on_open(void) {
    sSeenGen = sOpenGen;
    sFrames = 0;
    sScriptAt = 0;
    sOv.clear();
    sDir = sagas_dir();
    SDL_CreateDirectory(sDir.c_str());
    bool first = sSagas.empty();
    if (first) { sagas_read(); }
    story_lines_start(); // (Dragon History's voiced lines, for the voice lists: read in the background)
    charas_read();
    SDL_LockMutex(sMx);
    int gen = sSh.resultGen, result = sSh.result;
    SDL_UnlockMutex(sMx);
    if (gen != sSeenResult) { // back from an episode's battle
        sSeenResult = gen;
        sResult = result;
        Saga *s = saga();
        int ep = sSh.episode;
        if (s != NULL && sPlayMode && sSh.preview.empty() && result != 3) {
            const J &e = s->doc["episodes"][ep];
            bool fight = e["battle"]["fight"].truthy(true);
            bool won = result == 1 || !fight;
            sResultNext = next_of(e, ep, won);
            if (sResultNext >= (int)s->doc["episodes"].size()) { sResultNext = -1; }
            J &p = progress_of(*s);
            if (won) { p.list("cleared").push(J::num(ep)); }
            if (sResultNext == -1 && won) { p.set("done", J::boolean(true)); }
            else if (sResultNext >= 0) { p.set("episode", J::num(sResultNext)); }
            progress_save();
            sPages.clear();
            push_page(SC_RESULT, ep);
        } else if (s != NULL && sPlayMode && result == 3) {
            sPages.clear(); // (left from the pause menu: My Sagas; the saga waits where it was)
        }
        // (a test from the editor: back where it was)
    }
    if (!sPlayMode && sPages.empty()) { sSaga = -1; }
    fprintf(stderr, "bt3: story: the Story Creator (%d sagas in %s)\n", (int)sSagas.size() - 1, sDir.c_str());
}

void result_frame(std::vector<Prompt> &ps, std::string &tip) {
    Saga *s = saga();
    if (s == NULL) { sPages.clear(); return; }
    int ep = page().episode;
    const J &e = s->doc["episodes"][ep];
    bool fight = e["battle"]["fight"].truthy(true);
    bool won = sResult == 1 || !fight;
    ImVec2 c = P(688, 250);
    float t = std::min(1.0f, sFrames / 20.0f);
    std::string big = sResultNext == -1 && won ? "SAGA COMPLETE!" : won ? "EPISODE CLEAR!" : "DEFEATED...";
    float bs = S(84) * (0.7f + 0.3f * t);
    float bw = text_w(sBold, bs, big.c_str());
    outlined(sBold, bs, ImVec2(c.x - bw * 0.5f, c.y - bs * 0.5f), won ? IM_COL32(255, 214, 70, 255) : IM_COL32(200, 200, 230, 255), won ? IM_COL32(110, 40, 0, 255) : IM_COL32(30, 20, 60, 255), big, S(4));
    std::string sub = "Episode " + std::to_string(ep + 1) + ": " + e["title"].text();
    outlined(sFont, S(26), ImVec2(c.x - text_w(sFont, S(26), sub.c_str()) * 0.5f, c.y + S(60)), kInk, IM_COL32(20, 8, 40, 255), sub, S(1.5f));
    std::vector<Row> rows;
    if (sResultNext >= 0 || sResultNext == -2) {
        int nx = sResultNext == -2 ? ep : sResultNext;
        Row r;
        r.label = sResultNext == -2 ? "Try again" : "Continue";
        r.value = "Episode " + std::to_string(nx + 1) + ": " + s->doc["episodes"][nx]["title"].text();
        r.ok = [nx] { leave_for(sSaga, nx, ""); };
        rows.push_back(r);
    }
    Row b;
    b.label = "Back to My Sagas";
    b.ok = [] { sPages.clear(); sSaga = -1; };
    rows.push_back(b);
    ImVec2 a = P(388, 420), bb = P(988, 420 + 50.0f * rows.size() + 24);
    panel(a, bb);
    int clicked = draw_rows(a, bb, rows, page().cur, page().top, true);
    list_input(rows, page().cur, clicked);
    ps.push_back({B_CROSS, "Choose"});
    tip = won ? (sResultNext == -1 ? "That's the whole saga. Well fought!" : "On to the next part of the story.")
              : "The story is not over: try again, or the saga may take another turn.";
    int first = chara_of(e["player"][0].isObj() ? e["player"][0]["chara"] : e["player"][0]);
    sModel = first;
}

} // namespace

// Called by ui.cpp every frame (input: the screens have the keyboard and the controller). false when they are not open.
bool Story_Frame(bool input) {
    if (!sActive) { sTyping = false; return false; }
    if (sSeenGen != sOpenGen) { on_open(); }
    sFrames++;
    sTyping = !sOv.empty() && (sOv.back().kind == OV_KEYS || sOv.back().kind == OV_GRID);
    read_input(input);
    frame_setup();
    sBoxA = P(40, 138);
    sBoxB = P(720, 650);
    std::vector<Prompt> ps;
    std::string tip = sTip;
    sModel = -1;
    sModelCostume = 0;
    size_t ovBefore = sOv.size();
    if (!sPages.empty() && page().screen == SC_RESULT) {
        heading("Story Creator", saga() != NULL ? saga()->doc["title"].text() : "");
        if (sOv.empty()) { result_frame(ps, tip); }
    } else {
        std::vector<Row> rows;
        std::string head = "Story Creator", sub = "MY SAGAS";
        int screen = sPages.empty() ? SC_HOME : page().screen;
        if (screen != SC_HOME && saga() == NULL) { sPages.clear(); screen = SC_HOME; }
        switch (screen) {
        case SC_HOME: rows_home(rows); break;
        case SC_SAGA: rows_saga(rows); head = doc()["title"].text("Saga"); sub = "SAGA"; break;
        case SC_EPISODE: rows_episode(rows, page().episode); head = doc()["title"].text("Saga"); sub = "EPISODE " + std::to_string(page().episode + 1); break;
        case SC_SCENE: rows_scene(rows, page().episode, page().scene); head = doc()["title"].text("Saga"); sub = "EPISODE " + std::to_string(page().episode + 1) + " - SCENE"; break;
        case SC_FIGHTLINES:
            rows.push_back(sec("SAID DURING THE FIGHT"));
            line_rows_list(rows, page().episode, "fight", true);
            head = doc()["title"].text("Saga");
            sub = "EPISODE " + std::to_string(page().episode + 1) + " - BATTLE";
            break;
        case SC_LINE: rows_line(rows, page().episode, page().scene, page().line); head = doc()["title"].text("Saga"); sub = "EPISODE " + std::to_string(page().episode + 1) + " - LINE"; break;
        }
        if (!sPages.empty() && page().screen != screen) { rows.clear(); } // (the page went: drawn next frame)
        heading(head, sub);
        if (screen != SC_HOME) {
            const char *chip = sDirty ? "NOT SAVED" : "SAVED";
            float size = S(15), w = text_w(sBold, size, chip) + S(36);
            ImVec2 b = P(1336, 30), a(b.x - w, b.y), bb(b.x, b.y + S(30));
            grad(a, bb, sDirty ? IM_COL32(255, 140, 110, 255) : kGold1, sDirty ? IM_COL32(200, 60, 40, 255) : kGold2, S(15));
            sDl->AddText(sBold, size, ImVec2(a.x + S(18), a.y + (S(30) - size) * 0.5f), IM_COL32(74, 26, 0, 255), chip);
        }
        panel(sBoxA, sBoxB);
        int &cur = screen == SC_HOME ? sHomeCur : page().cur;
        int &top = screen == SC_HOME ? sHomeTop : page().top;
        fix_cursor(rows, cur);
        int clicked = draw_rows(sBoxA, sBoxB, rows, cur, top, sOv.empty());
        if (!rows.empty() && cur < (int)rows.size()) {
            const Row &r = rows[(size_t)cur];
            if (tip.empty()) { tip = r.tip; }
            sModel = r.model;
            sModelCostume = r.modelCostume;
        }
        if (sModel < 0 && screen >= SC_EPISODE && screen != SC_RESULT) {
            Team t = team_of(episode(page().episode), "player");
            sModel = t.chara.empty() ? -1 : t.chara[0];
        }
        if (sOv.empty()) {
            const Row *r = cur < (int)rows.size() ? &rows[(size_t)cur] : NULL;
            ps.push_back({B_CROSS, r != NULL && r->okLabel ? r->okLabel : "Choose"});
            if (r != NULL && r->lr) { ps.push_back({B_LR, r->lrLabel != NULL ? r->lrLabel : "Change"}); }
            if (r != NULL && r->sq) { ps.push_back({B_SQUARE, r->sqLabel != NULL ? r->sqLabel : ""}); }
            if (r != NULL && r->tri) { ps.push_back({B_TRIANGLE, r->triLabel != NULL ? r->triLabel : ""}); }
            if (r != NULL && (r->l2 || r->r2)) { ps.push_back({B_L2R2, r->l2Label != NULL ? r->l2Label : "Move"}); }
            ps.push_back({B_CIRCLE, screen == SC_HOME ? "Main menu" : "Back"});
            if (screen != SC_HOME) { ps.push_back({B_START, "Save"}); }
            if (screen != SC_HOME && sIn.start) {
                save_saga();
            } else if (sIn.back) {
                sound(2);
                if (screen == SC_HOME) {
                    SDL_LockMutex(sMx);
                    sSh.leave = 1;
                    SDL_UnlockMutex(sMx);
                } else if (screen == SC_SAGA && sDirty) {
                    confirm("LEAVE WITHOUT SAVING?", [] { sPages.clear(); sDirty = false; sagas_read(); sSaga = -1; });
                } else {
                    sPages.pop_back();
                    if (sPages.empty()) { sSaga = -1; }
                }
            } else {
                list_input(rows, cur, clicked);
            }
        }
    }
    // the screens over the page (the top one has the buttons)
    if (sOv.size() > ovBefore) { float turn = sIn.turn; memset(&sIn, 0, sizeof(sIn)); sIn.turn = turn; }
    if (!sOv.empty()) {
        std::vector<Prompt> ops;
        Overlay &o = sOv.back();
        ImVec2 a = sBoxA, b = sBoxB;
        bool small = o.kind == OV_LIST && o.rows.size() <= 3;
        if (small) {
            a = P(300, 240);
            b = P(1076, 520);
            sDl->AddRectFilled(P(0, 0), P(1376, 768), IM_COL32(10, 5, 30, 130));
        }
        ImVec2 keepA = sBoxA, keepB = sBoxB;
        sBoxA = small ? a : ImVec2(a.x, P(0, 98).y);
        sBoxB = b;
        sDl->AddRectFilled(ImVec2(sBoxA.x, sBoxA.y + S(5)), ImVec2(sBoxB.x, sBoxB.y + S(5)), IM_COL32(0, 0, 0, 90), S(12));
        sDl->AddRectFilled(sBoxA, sBoxB, IM_COL32(40, 22, 82, 255), S(12));
        sDl->AddRect(sBoxA, sBoxB, IM_COL32(245, 197, 66, 255), S(12), 0, S(3));
        ov_title(o);
        if (o.kind == OV_GRID) { tip = "Pick a character: the model shows whom the cursor is on. Type to search."; }
        else if (o.kind == OV_KEYS) { tip = "Type with the keys (or your keyboard; Ctrl+V pastes). START or Enter when it is right."; }
        else if (o.cur < (int)o.rows.size() && !o.rows[(size_t)o.cur].tip.empty()) { tip = o.rows[(size_t)o.cur].tip; }
        int kind = o.kind;
        if (kind == OV_GRID) { grid_frame(o, ops); if (sGridModel >= 0) { sModel = sGridModel; sModelCostume = 0; } }
        else if (kind == OV_KEYS) { keys_frame(o, ops); }
        else { list_frame(o, ops); }
        sBoxA = keepA;
        sBoxB = keepB;
        ps = ops;
    }
    if (input) { ImGui::GetIO().InputQueueCharacters.resize(0); }
    // the model on the platform
    SDL_LockMutex(sMx);
    sSh.key = sModel >= 0 ? sModel * 32 + sModelCostume + 1 : -1;
    sSh.chara = sModel;
    sSh.costume = sModelCostume;
    sSh.turn += (int)(sIn.turn * 0.05f * 1000.0f);
    SDL_UnlockMutex(sMx);
    if (sModel >= 0) {
        std::string n = chara_name(sModel);
        float size = S(30);
        ImVec2 c = P(1030, 96);
        outlined(sBold, size, ImVec2(c.x - text_w(sBold, size, n.c_str()) * 0.5f, c.y), kInk, IM_COL32(58, 22, 96, 255), n, S(2));
    }
    if (tip.empty()) { tip = "Make a saga of your own: your fighters, your stages, your words."; }
    tip_box(tip, sModel >= 0 ? sModel : 3);
    prompts(ps);
    if (!sNotice.empty() && sFrames < sNoticeUntil) {
        float size = S(17), wrap = P(1100, 0).x - P(300, 0).x;
        ImVec2 ts = sFont->CalcTextSizeA(size, FLT_MAX, wrap, sNotice.c_str());
        ImVec2 c = P(688, 24);
        ImVec2 a(c.x - ts.x * 0.5f - S(18), c.y), b(c.x + ts.x * 0.5f + S(18), c.y + ts.y + S(20));
        sDl->AddRectFilled(a, b, IM_COL32(28, 16, 64, 240), S(10));
        sDl->AddRect(a, b, sNoticeErr ? IM_COL32(255, 107, 107, 255) : IM_COL32(245, 197, 66, 255), S(10), 0, S(2));
        sDl->AddText(sFont, size, ImVec2(a.x + S(18), a.y + S(10)), kInk, sNotice.c_str(), NULL, wrap);
    }
    return true;
}

bool Story_Typing(void) { return sActive && sTyping; }

// The episode's battle is on: its overlay is drawn (ui.cpp, every frame).
bool Story_OverlayOn(void) { return sInBattle && !sActive; }
void Story_Overlay(void) {
    if (Story_OverlayOn()) { overlay_draw(); }
}

// ---- for Corrupt Patrol (gs/patrol_ui.cpp): the Story Creator's portraits and names, made public
bool StoryKit_Face(int chara, ImTextureID *tex, ImVec2 *uv0, ImVec2 *uv1) { return face(chara, tex, uv0, uv1); }
void StoryKit_FrameStart(void) { sFacesThisFrame = 0; }
std::string StoryKit_CharaName(int chara) { return chara_name(chara); }
std::string StoryKit_ShortName(int chara) { return short_name(chara); }
// an added character's id by its folder name in `characters` ("added:<folder>" in a saga), -1 when it is not there
int StoryKit_AddedId(const std::string &folder) { return chara_of(J::str("added:" + folder)); }

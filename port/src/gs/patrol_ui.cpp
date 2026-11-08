// Corrupt Patrol (main menu item 15): a game of its own inside the game.
//
// The player makes a warrior of one of six races (chosen from a row of them standing on Kami's Lookout) and joins the
// Corrupt Patrol: the Malice, the evil scrubbed from the dead and set loose from Hell, is turning the strongest
// fighters into the Corrupted (a violet glow round them). The player flies between twenty places on a world map
// (Triangle while walking about one; taking off, flying, landing), walks about each, and talks to the people there:
// the Supreme Kai's main story, side quests from everyone, the masters, the game's own item shop, bounties, the
// wardrobe. Every character of the game is a master once freed or won over: five lessons each, one technique learnt
// per win, their style and outfit at the end. Levels, experience and zeni; a save file of its own
// (patrol/save.json next to the game, BT3_PATROL another folder), never the game's.
//
// The story, the quests, the places and the masters' lines are data (patrol_content.inc, made from
// port/assets/patrol/*.json by port/tools/patrol_content.py; patrol/content.json next to the game in its place, if
// there is one). This file is the screens (Dear ImGui into the background list, on the render thread, as
// gs/story_ui.cpp draws the Story Creator's), the save, and the making of the battles: each is an episode of a saga
// made here (scenes before and after, the fight, the Z items of each side), played through the Story Creator's engine
// (PortStoryUi_PrepareDoc, src/battle/port_story_btl.c), the places to walk in too (its command 9007). The game's
// half is src/menu/port_patrol.c.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "ui.h"

ImFont *Ui_FontBold(void);
bool StoryKit_Face(int chara, ImTextureID *tex, ImVec2 *uv0, ImVec2 *uv1); // gs/story_ui.cpp
void StoryKit_FrameStart(void);
std::string StoryKit_CharaName(int chara);
std::string StoryKit_ShortName(int chara);
int StoryKit_AddedId(const std::string &name);
SDL_GPUTexture *Ui_Texture(uint8_t *pix, uint32_t w, uint32_t h); // ui.cpp (takes pix)
extern "C" {
int PortStoryUi_PrepareDoc(const char *json, int episode);
void PortCorrupt_SetNext(int mask); // port_corrupt.c
void PortCreator_PatrolBegin(const char *name, const char *file, const char *allowedCsv, const char *bodyRef, int bodyCostume,
                             const char *kitJson); // gs/creator_ui.cpp: its Patrol mode
void PortCreator_PatrolAuto(void);
int PortCreator_PatrolResult(char *name, int size);
int PortCreator_PatrolFile(char *file, int size);
extern int gPortHubMode;            // src/battle/port_story_btl.c: a place's battle is up
extern volatile int gPortHubNear;   // ... the person within talking range, -1 none
extern volatile int gPortHubExit;   // ... how it was left: a person, 99 the menu, 100 + a place flown to
void PortStoryUi_EndDoc(void);
int Port_NetSession(void);
}

namespace {

// =============================================================================================== JSON (as gs/story_ui.cpp)
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
    size_t size() const { return t == ARR || t == OBJ ? a.size() : 0; }
    const J &operator[](size_t i) const { static J none; return (t == ARR || t == OBJ) && i < a.size() ? a[i] : none; }
    const J &operator[](int i) const { return (*this)[(size_t)(i < 0 ? 0 : i)]; }
    const J &operator[](const char *key) const {
        static J none;
        if (t == OBJ) { for (size_t i = 0; i < k.size(); i++) { if (k[i] == key) { return a[i]; } } }
        return none;
    }
    J &set(const char *key, const J &v) {
        if (t != OBJ) { *this = obj(); }
        for (size_t i = 0; i < k.size(); i++) { if (k[i] == key) { a[i] = v; return a[i]; } }
        k.push_back(key);
        a.push_back(v);
        return a.back();
    }
    void push(const J &v) { if (t != ARR) { *this = arr(); } a.push_back(v); }
    std::string text(const char *def = "") const { return t == STR ? s : t == NUM ? std::to_string((long long)n) : std::string(def); }
    double number(double def = 0) const { return t == NUM ? n : t == BOOL ? (b ? 1 : 0) : def; }
};
void js_ws(const char *&p) {
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') { p++; }
        if (p[0] == '/' && p[1] == '/') { while (*p && *p != '\n') { p++; } continue; }
        return;
    }
}
bool js_str(const char *&p, std::string &o) {
    if (*p != '"') { return false; }
    p++;
    o.clear();
    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            switch (*p) {
            case 'n': o += '\n'; p++; break;
            case 't': o += '\t'; p++; break;
            case 'r': p++; break;
            case 'u': {
                unsigned v = 0;
                for (int i = 1; i <= 4 && p[i]; i++) { char c = p[i]; v = v * 16 + (unsigned)(c <= '9' ? c - '0' : (c | 32) - 'a' + 10); }
                p += 5;
                if (v < 0x80) { o += (char)v; }
                else if (v < 0x800) { o += (char)(0xC0 | (v >> 6)); o += (char)(0x80 | (v & 0x3F)); }
                else { o += (char)(0xE0 | (v >> 12)); o += (char)(0x80 | ((v >> 6) & 0x3F)); o += (char)(0x80 | (v & 0x3F)); }
                break;
            }
            default: if (*p) { o += *p++; } break;
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
    if (depth > 64) { return false; }
    js_ws(p);
    if (*p == '{') {
        v = J::obj();
        p++;
        js_ws(p);
        if (*p == '}') { p++; return true; }
        for (;;) {
            std::string key;
            js_ws(p);
            if (!js_str(p, key)) { return false; }
            js_ws(p);
            if (*p != ':') { return false; }
            p++;
            J item;
            if (!js_parse(p, item, depth + 1)) { return false; }
            v.k.push_back(key);
            v.a.push_back(item);
            js_ws(p);
            if (*p == ',') { p++; continue; }
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
            if (*p == ',') { p++; continue; }
            if (*p == ']') { p++; return true; }
            return false;
        }
    }
    if (*p == '"') { v = J(); v.t = J::STR; return js_str(p, v.s); }
    if (strncmp(p, "true", 4) == 0) { v = J::boolean(true); p += 4; return true; }
    if (strncmp(p, "false", 5) == 0) { v = J::boolean(false); p += 5; return true; }
    if (strncmp(p, "null", 4) == 0) { v = J(); p += 4; return true; }
    char *end;
    double d = strtod(p, &end);
    if (end == p) { return false; }
    v = J::num(d);
    p = end;
    return true;
}
bool js_read(const std::string &text, J &v) { const char *p = text.c_str(); return js_parse(p, v); }
void js_quote(const std::string &s, std::string &o) {
    o += '"';
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c == '\n') { o += "\\n"; }
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
        else { o += (char)c; }
    }
    o += '"';
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
        o += obj ? '{' : '[';
        for (size_t i = 0; i < v.a.size(); i++) {
            o += i ? ",\n" : "\n";
            o.append((size_t)(ind + 2), ' ');
            if (obj) { js_quote(v.k[i], o); o += ": "; }
            js_write(v.a[i], o, ind + 2);
        }
        o += '\n';
        o.append((size_t)ind, ' ');
        o += obj ? '}' : ']';
        break;
    }
    }
}

// =============================================================================================== the mode's content
struct TalkLn { int chara; std::string text; }; // chara -2: the player's warrior
struct Person { int chara; std::string role; };  // role: story, wardrobe, trainer, shop, board, quests
struct Loc {
    std::string id, name, region, size, desc, arrive;
    int stage = 5, music = 1;
    float mx = 0.5f, my = 0.5f;                  // on the world map (earth), 0..1
    std::vector<Person> people;
};
struct Mission {                                   // a chapter of the main story or a side quest
    bool chapter = false;
    int index = 0;
    std::string id, title, location, summary;
    int giver = -1;
    int reqChapter = 0, reqLevel = 1;
    std::vector<std::string> reqQuests;
    int stage = 0, music = 0, level = 1;
    std::vector<int> foes, allies;
    bool corrupt = true;
    std::vector<TalkLn> brief, after;
    J intro, win, lose;                            // saga scene lines
    int zeni = 0, xp = 0;
    std::vector<int> masters;
};
struct MasterInfo { std::string greet, done, home; };
std::vector<Loc> gLocs;
std::vector<Mission> gChapters, gQuests;
std::map<int, MasterInfo> gMasterInfo;
std::vector<TalkLn> gWelcome, gEnding;
std::map<std::string, std::vector<TalkLn>> gServices;
bool sContentRead;

struct Race {
    const char *name, *desc;
    int avatar;      // the disc fighter the warrior fights as until their own look is made (Custom Character's Patrol mode)
    int avatarCostume;
    int traits[4];   // attack, defense, ki, speed: 1..5 (shown on the choice; flavour)
};
// The fighters each race's parts come from (heads, hair, bodies, outfits): ranges of roster ids, BT4's Time Patroller of
// the race too (1000 + its BT4 id) when the BT4 disc is there. Saiyans and humans look the same: each has the other's.
const char *const kSaiyanHuman = "0-21,25-54,55-57,60,63,64,66-69,71-78,80,81,121,122,131-133,148-150,157-160,1242";
const char *const kRaceIds[6] = {
    kSaiyanHuman,
    "22-24,123,124,155,1243",
    "91-97,126-128,1244",
    kSaiyanHuman,
    "65,112-117,1245",
    "100-109,129,130,144",
};
const Race kRaces[6] = {
    {"Saiyan", "A warrior race that comes back stronger from every beating. Brutal up close, explosive with ki, and too proud to stay down.", 73, 0, {5, 3, 4, 3}},
    {"Namekian", "Calm, patient and hard to kill. Long reach, quick recovery, and the wisdom to outlast anyone who rushes them.", 24, 0, {3, 4, 4, 3}},
    {"Arcosian", "Frieza's kind: cold, fast and merciless. Devastating ki techniques and a body that shrugs off what would kill others.", 91, 0, {4, 4, 5, 4}},
    {"Human", "No tail, no transformations, just discipline. Humans learn technique faster than anyone, and never stop surprising people.", 27, 0, {3, 3, 3, 5}},
    {"Majin", "Born of magic and mischief. Rubbery, resilient and relentless; hits keep coming and so do they.", 65, 0, {4, 5, 3, 3}},
    {"Bio-Android", "Grown from the cells of the greatest fighters. Adapts to every opponent, and borrows a little of everyone it faces.", 109, 0, {4, 4, 4, 4}},
};
const char *const kTraitNames[4] = {"POWER", "TOUGHNESS", "KI", "SPEED"};

const int kLessons = 5; // trainings per master: one technique each, the last everything else too
const int kEquipMax = 4;
const int kItemCount = 350; // the game's items (0-based here; a battle's member items are 1-based)

// The bounties: fighters of the right strength for the player's level, on a random stage
const std::vector<std::vector<int>> kBountyTiers = {
    {79, 99, 77, 80, 98},           // Saibamen, soldiers, Raditz, Nappa, Appule
    {82, 84, 85, 87, 88, 89, 90},   // the Ginyu Force and Frieza's men
    {86, 103, 129, 118, 121, 120},  // Ginyu, Android 19, Android 13, Garlic Jr, Turles, Wheelo
    {126, 135, 123, 111, 101, 134}, // Cooler, Bojack, Slug, Dabura, Android 17, Zangya
    {133, 147, 144, 128, 136, 139}, // Broly, Omega Shenron, Super 17, Metal Cooler, Bojack, Hildegarn
};
const int kBountyStages[] = {0, 1, 9, 10, 11, 13, 14, 15, 19, 22, 23, 24, 25, 29, 31, 33};

int chara_of_line(const J &c) { return c.t == J::STR ? -2 : (int)c.number(-1); } // ("player": the warrior)
std::vector<TalkLn> talk_of(const J &a) {
    std::vector<TalkLn> v;
    for (size_t i = 0; i < a.size(); i++) {
        const J &l = a[i];
        if (l.t == J::STR) { v.push_back({58, l.s}); continue; }
        v.push_back({chara_of_line(l["chara"]), l["text"].text()});
    }
    return v;
}
std::vector<int> int_list(const J &a) { std::vector<int> v; for (size_t i = 0; i < a.size(); i++) { v.push_back((int)a[i].number(-1)); } return v; }
void mission_read(Mission &m, const J &q) {
    m.id = q["id"].text();
    m.title = q["title"].text("Untitled");
    m.location = q["location"].text("lookout");
    m.summary = q["summary"].text();
    m.giver = (int)q["giver"].number(-1);
    m.reqChapter = (int)q["requires"]["chapter"].number(0);
    m.reqLevel = std::max(1, (int)q["requires"]["level"].number(1));
    for (size_t i = 0; i < q["requires"]["quests"].size(); i++) { m.reqQuests.push_back(q["requires"]["quests"][i].text()); }
    m.stage = (int)q["stage"].number(0);
    m.music = (int)q["music"].number(0);
    m.level = std::max(1, (int)q["level"].number(m.reqLevel));
    m.foes = int_list(q["foes"]);
    m.allies = int_list(q["allies"]);
    m.corrupt = q["corrupt"].t == J::BOOL ? q["corrupt"].b : true;
    m.brief = talk_of(q["brief"]);
    m.after = talk_of(q["after"]);
    m.intro = q["intro"];
    m.win = q["win"];
    m.lose = q["lose"];
    const J &rw = q["reward"];
    m.zeni = (int)rw["zeni"].number(m.chapter ? 2500 + m.index * 1500 : 1000) * (m.chapter ? 1 : 2); // (the shop's items cost 2000..21000)
    m.xp = (int)rw["xp"].number(m.chapter ? 300 + m.index * 150 : 200);
    m.masters = int_list(m.chapter ? q["masters"] : rw["masters"]);
    m.foes.erase(std::remove_if(m.foes.begin(), m.foes.end(), [](int c) { return c < 0 || c >= 161; }), m.foes.end());
    if (m.foes.empty()) { m.foes.push_back(79); }
    if (m.foes.size() > 3) { m.foes.resize(3); }
    if (m.allies.size() > 2) { m.allies.resize(2); }
}
#include "patrol_content.inc" // kPatrolContent: the content, as port/tools/patrol_content.py wrote it
bool read_file(const std::string &path, std::string &out);
void content_read(void) {
    if (sContentRead) { return; }
    sContentRead = true;
    std::string text;
    if (!read_file("patrol/content.json", text)) { // (a content of the player's own in its place)
        text.clear();
        for (const char *const *p = kPatrolContent; *p != NULL; p++) { text += *p; }
    }
    J d;
    if (!js_read(text, d) || !d.isObj()) { fprintf(stderr, "bt3: patrol: the content could not be read\n"); return; }
    const J &ls = d["locations"];
    for (size_t i = 0; i < ls.size(); i++) {
        const J &l = ls[i];
        Loc c;
        c.id = l["id"].text();
        c.name = l["name"].text(c.id.c_str());
        c.region = l["region"].text("earth");
        c.size = l["size"].text("small");
        c.desc = l["desc"].text();
        c.arrive = l["arrive"].text();
        c.stage = (int)l["stage"].number(5);
        c.music = (int)l["music"].number(1);
        c.mx = (float)l["map"][0].number(0.5);
        c.my = (float)l["map"][1].number(0.5);
        for (size_t k = 0; k < l["people"].size(); k++) { c.people.push_back({(int)l["people"][k]["chara"].number(-1), l["people"][k]["role"].text()}); }
        gLocs.push_back(c);
    }
    for (size_t i = 0; i < d["chapters"].size(); i++) {
        Mission m;
        m.chapter = true;
        m.index = (int)i;
        mission_read(m, d["chapters"][i]);
        m.reqChapter = (int)i;
        m.giver = 58;
        gChapters.push_back(m);
    }
    for (size_t i = 0; i < d["quests"].size(); i++) {
        Mission m;
        m.index = (int)i;
        mission_read(m, d["quests"][i]);
        gQuests.push_back(m);
    }
    const J &ms = d["masters"];
    for (size_t i = 0; i < ms.k.size(); i++) {
        MasterInfo mi;
        mi.greet = ms.a[i]["greet"].text();
        mi.done = ms.a[i]["done"].text();
        mi.home = ms.a[i]["home"].text("timechamber");
        gMasterInfo[atoi(ms.k[i].c_str())] = mi;
    }
    gWelcome = talk_of(d["welcome"]);
    gEnding = talk_of(d["ending"]);
    for (size_t i = 0; i < d["services"].k.size(); i++) { gServices[d["services"].k[i]] = talk_of(d["services"].a[i]); }
    if (gLocs.empty()) { Loc l; l.id = "lookout"; l.name = "Kami's Lookout"; l.people.push_back({58, "story"}); gLocs.push_back(l); }
    fprintf(stderr, "bt3: patrol: %d places, %d chapters, %d quests, %d masters\n", (int)gLocs.size(), (int)gChapters.size(), (int)gQuests.size(),
            (int)gMasterInfo.size());
}
int loc_index(const std::string &id) { for (size_t i = 0; i < gLocs.size(); i++) { if (gLocs[i].id == id) { return (int)i; } } return 0; }
const Loc &loc_of(const std::string &id) { return gLocs[(size_t)loc_index(id)]; }
const MasterInfo &master_info(int chara) {
    static MasterInfo none;
    auto it = gMasterInfo.find(chara);
    return it != gMasterInfo.end() ? it->second : none;
}

// =============================================================================================== the save
struct ItemInfo { std::string name, desc; int type = 0, slots = 1, price = 0, flags = 0; bool known = false; };
ItemInfo sItems[kItemCount];
struct Save {
    bool exists = false;
    std::string name;
    int race = 0;
    std::string fighter;        // the warrior's character package (Custom Character's Patrol mode), "" none yet
    std::string fighterFile;    // ... its spec in custom_fighters
    int level = 1, xp = 0, zeni = 3000;
    std::string location = "lookout";
    int chapter = 0;            // chapters cleared (the next is this one)
    std::vector<std::string> done; // side quests cleared
    std::vector<int> masters;   // unlocked, in the order they were
    std::map<int, int> wins;    // trainings won with each master
    std::vector<int> owned, equipped; // the game's items, 0-based
    int bounties = 0, won = 0, lost = 0;
    double playtime = 0;
    J loadout;                  // the techniques chosen per slot: {"blast1_1": {"chara": c, "slot": s}, ...}
};
Save sSave;
std::string sSaveDir;
std::string save_dir(void) {
    const char *e = getenv("BT3_PATROL");
    std::string d = e != NULL && e[0] ? e : "patrol";
    while (d.size() > 1 && (d.back() == '/' || d.back() == '\\')) { d.pop_back(); }
    return d;
}
std::string save_path(void) { return sSaveDir + "/save.json"; }
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
    SDL_CreateDirectory(sSaveDir.c_str());
    std::string tmp = path + ".tmp";
    FILE *fp = fopen(tmp.c_str(), "wb");
    if (fp == NULL) { return false; }
    bool ok = fwrite(text.data(), 1, text.size(), fp) == text.size();
    ok = fclose(fp) == 0 && ok;
    if (!ok) { remove(tmp.c_str()); return false; }
    remove(path.c_str());
    return rename(tmp.c_str(), path.c_str()) == 0;
}
J ints(const std::vector<int> &v) { J a = J::arr(); for (int x : v) { a.push(J::num(x)); } return a; }
std::vector<int> ints_of(const J &a) { std::vector<int> v; for (size_t i = 0; i < a.size(); i++) { v.push_back((int)a[i].number()); } return v; }
bool has_int(const std::vector<int> &v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }
bool quest_done(const std::string &id) { return std::find(sSave.done.begin(), sSave.done.end(), id) != sSave.done.end(); }
void items_cache_read(void) {
    std::string text;
    J d;
    if (!read_file(sSaveDir + "/items.json", text) || !js_read(text, d)) { return; }
    for (size_t i = 0; i < d.size(); i++) {
        int id = (int)d[i]["item"].number(-1);
        if (id < 0 || id >= kItemCount) { continue; }
        ItemInfo &it = sItems[id];
        it.name = d[i]["name"].text();
        it.desc = d[i]["desc"].text();
        it.type = (int)d[i]["type"].number(0);
        it.slots = (int)d[i]["slots"].number(1);
        it.price = (int)d[i]["price"].number(0);
        it.flags = (int)d[i]["flags"].number(0);
        it.known = !it.name.empty();
    }
}
void items_cache_write(void) {
    J a = J::arr();
    for (int i = 0; i < kItemCount; i++) {
        if (!sItems[i].known) { continue; }
        J e = J::obj();
        e.set("item", J::num(i));
        e.set("name", J::str(sItems[i].name));
        e.set("desc", J::str(sItems[i].desc));
        e.set("type", J::num(sItems[i].type));
        e.set("slots", J::num(sItems[i].slots));
        e.set("price", J::num(sItems[i].price));
        e.set("flags", J::num(sItems[i].flags));
        a.push(e);
    }
    std::string o;
    js_write(a, o);
    write_file(sSaveDir + "/items.json", o + "\n");
}
void save_load(void) {
    sSave = Save();
    std::string text;
    J d;
    if (!read_file(save_path(), text) || !js_read(text, d) || !d.isObj()) { return; }
    int format = (int)d["format"].number(1);
    sSave.exists = true;
    sSave.name = d["name"].text("Patroller");
    sSave.race = std::max(0, std::min(5, (int)d["race"].number(0)));
    sSave.fighter = d["fighter"].text();
    sSave.fighterFile = d["fighter_file"].text();
    sSave.level = std::max(1, (int)d["level"].number(1));
    sSave.xp = (int)d["xp"].number(0);
    sSave.zeni = (int)d["zeni"].number(0);
    sSave.bounties = (int)d["bounties"].number(0);
    sSave.won = (int)d["wins"].number(0);
    sSave.lost = (int)d["losses"].number(0);
    sSave.playtime = d["playtime"].number(0);
    sSave.loadout = d["loadout"].isObj() ? d["loadout"] : J::obj();
    if (format < 2) { // (the first story's save: its warrior, level and zeni carry over; its items were 1-based)
        for (int id : ints_of(d["owned"])) { if (id >= 1 && id <= kItemCount) { sSave.owned.push_back(id - 1); } }
        for (int id : ints_of(d["equipped"])) { if (id >= 1 && id <= kItemCount && (int)sSave.equipped.size() < kEquipMax) { sSave.equipped.push_back(id - 1); } }
        return;
    }
    sSave.location = d["location"].text("lookout");
    sSave.chapter = std::max(0, std::min((int)gChapters.size(), (int)d["chapter"].number(0)));
    for (size_t i = 0; i < d["done"].size(); i++) { sSave.done.push_back(d["done"][i].text()); }
    sSave.masters = ints_of(d["masters"]);
    for (size_t i = 0; i < d["lessons"].k.size(); i++) { sSave.wins[atoi(d["lessons"].k[i].c_str())] = std::max(0, std::min(kLessons, (int)d["lessons"].a[i].number(0))); }
    sSave.won = (int)d["won"].number(sSave.won);
    sSave.lost = (int)d["lost"].number(sSave.lost);
    sSave.owned = ints_of(d["items"]);
    sSave.equipped = ints_of(d["equipped"]);
    if ((int)sSave.equipped.size() > kEquipMax) { sSave.equipped.resize(kEquipMax); }
}
bool save_write(void) {
    J d = J::obj();
    d.set("format", J::num(2));
    d.set("name", J::str(sSave.name));
    d.set("race", J::num(sSave.race));
    d.set("race_name", J::str(kRaces[sSave.race].name));
    d.set("fighter", J::str(sSave.fighter));
    d.set("fighter_file", J::str(sSave.fighterFile));
    d.set("level", J::num(sSave.level));
    d.set("xp", J::num(sSave.xp));
    d.set("zeni", J::num(sSave.zeni));
    d.set("location", J::str(sSave.location));
    d.set("chapter", J::num(sSave.chapter));
    J dn = J::arr();
    for (const std::string &s : sSave.done) { dn.push(J::str(s)); }
    d.set("done", dn);
    d.set("masters", ints(sSave.masters));
    J ls = J::obj();
    for (auto &kv : sSave.wins) { if (kv.second > 0) { ls.set(std::to_string(kv.first).c_str(), J::num(kv.second)); } }
    d.set("lessons", ls);
    d.set("items", ints(sSave.owned));
    d.set("equipped", ints(sSave.equipped));
    d.set("bounties", J::num(sSave.bounties));
    d.set("won", J::num(sSave.won));
    d.set("lost", J::num(sSave.lost));
    d.set("playtime", J::num((double)(long long)sSave.playtime));
    d.set("loadout", sSave.loadout.isObj() ? sSave.loadout : J::obj());
    std::string o;
    js_write(d, o);
    o += "\n";
    bool ok = write_file(save_path(), o);
    fprintf(stderr, "bt3: patrol: %s %s\n", ok ? "saved" : "could not save", save_path().c_str());
    if (ok) { sSave.exists = true; }
    return ok;
}
int xp_to_next(int level) { return 100 + level * 60; }
const int kLevelMax = 60;
bool owns(int item) { return has_int(sSave.owned, item); }
bool equipped(int item) { return has_int(sSave.equipped, item); }
bool master_unlocked(int chara) { return has_int(sSave.masters, chara); }
int master_wins(int chara) { auto it = sSave.wins.find(chara); return it != sSave.wins.end() ? it->second : 0; }
int slot_budget(void) { return std::min(7, 2 + sSave.level / 7); } // the item slots the warrior has (as the game's characters' items take)
int slots_used(void) { int n = 0; for (int it : sSave.equipped) { n += std::max(0, sItems[it].known ? sItems[it].slots : 1); } return n; }
// The player's stat items from their level (hidden: each stat steps up at its own levels) and the items worn
std::vector<int> player_items(void) {
    std::vector<int> v;
    int lv = sSave.level;
    const int base[4] = {1, 4, 7, 10}; // attack, defense, ki blast, ki: +5 / +10 / +15
    for (int s = 0; s < 4; s++) {
        int tier = std::min(3, (lv + s * 3) / 14);
        if (tier > 0) { v.push_back(base[s] + tier - 1); }
    }
    for (int it : sSave.equipped) { if ((int)v.size() < 8) { v.push_back(it + 1); } }
    return v;
}
int player_bars(void) { // health bars, as the items give them on top of the warrior's three
    int b = 3;
    for (int it : sSave.equipped) { b += it + 1 == 125 ? 1 : it + 1 == 126 ? 2 : 0; }
    return b;
}
std::string fighter_ref(J *out) { // the player's fighter in a saga: their package once made, else the race's stand-in
    int id = sSave.fighter.empty() ? -1 : StoryKit_AddedId(sSave.fighter);
    if (id >= 0) { *out = J::str("added:" + sSave.fighter); return sSave.fighter; }
    *out = J::num(kRaces[sSave.race].avatar);
    return "";
}
int fighter_chara(void) {
    int id = sSave.fighter.empty() ? -1 : StoryKit_AddedId(sSave.fighter);
    return id >= 0 ? id : kRaces[sSave.race].avatar;
}
std::string fill(const std::string &t) { // {name} and {race} in a line
    std::string s = t;
    for (size_t at; (at = s.find("{name}")) != std::string::npos;) { s.replace(at, 6, sSave.name); }
    for (size_t at; (at = s.find("{race}")) != std::string::npos;) { s.replace(at, 6, kRaces[sSave.race].name); }
    return s;
}

// The disc's move names, if the creator's roster cache is there (custom_fighters/.cache/roster2.json)
std::vector<std::vector<std::string>> sMoveNames;
void move_names_read(void) {
    if (!sMoveNames.empty()) { return; }
    sMoveNames.assign(161, std::vector<std::string>());
    std::string text;
    J d;
    if (!read_file("custom_fighters/.cache/roster2.json", text) || !js_read(text, d)) { return; }
    const J &list = d.isArr() ? d : d["roster"];
    for (size_t i = 0; i < list.size(); i++) {
        int id = (int)list[i]["id"].number(-1);
        if (id < 0 || id >= 161) { continue; }
        for (size_t k = 0; k < list[i]["moves"].size(); k++) { sMoveNames[(size_t)id].push_back(list[i]["moves"][k].text()); }
    }
}
const char *const kSlotKinds[5] = {"Blast 1", "Blast 1", "Blast 2", "Blast 2", "Ultimate"};
const char *const kSlotKeys[5] = {"blast1_1", "blast1_2", "blast2_1", "blast2_2", "ultimate"};
std::string move_name(int chara, int slot) {
    move_names_read();
    if (chara >= 0 && chara < 161 && slot < (int)sMoveNames[(size_t)chara].size() && !sMoveNames[(size_t)chara][(size_t)slot].empty()) {
        return sMoveNames[(size_t)chara][(size_t)slot];
    }
    return StoryKit_ShortName(chara) + "'s " + kSlotKinds[slot] + (slot == 0 || slot == 2 ? " A" : slot == 1 || slot == 3 ? " B" : "");
}
std::string item_name(int it) { return it >= 0 && it < kItemCount && sItems[it].known ? sItems[it].name : "Z Item No. " + std::to_string(it + 1); }

// =============================================================================================== shared with the game's thread
enum { PG_LEAVE, PG_SOUND, PG_KEY, PG_CHARA, PG_COSTUME, PG_TURN, PG_BGM, PG_VOICE, PG_STAGE, PG_PLACE_X, PG_PLACE_FEET, PG_PLACE_H,
       PG_LINE = 20 };
const int kLineMax = 6;
SDL_Mutex *sMx;
volatile int sActive;
struct Shared {
    int leave = 0;
    int sound[16] = {0}, sounds = 0;
    int key = -1, chara = 0, costume = 0, turn = 0, voice = -1, bgm = 6;
    int stage = 5;
    float place[3] = {1030.0f, 605.0f, 390.0f};
    int line[kLineMax] = {-1, -1, -1, -1, -1, -1};
    float linePlace[kLineMax][4] = {};
    int result = 0, resultGen = 0;
};
Shared sSh;
std::string sDoc;     // the battle to play (a saga of one episode)
enum { FIGHT_NONE, FIGHT_MISSION, FIGHT_TRAIN, FIGHT_BOUNTY, FIGHT_HUB };
int sFightKind, sFightA, sFightB; // what it is: mission (a: 1 chapter, 0 quest; b: its index) / master a lesson b / bounty of tier a
int sResultSeen;
void sound(int id) {
    SDL_LockMutex(sMx);
    if (sSh.sounds < 16) { sSh.sound[sSh.sounds++] = id; }
    SDL_UnlockMutex(sMx);
}
void want_model(int chara, int costume) {
    if (chara < 0) { chara = sSave.exists ? fighter_chara() : 58; } // (always someone on the stage behind the screens)
    SDL_LockMutex(sMx);
    if (sSh.chara != chara || sSh.costume != costume || sSh.key < 0) { sSh.chara = chara; sSh.costume = costume; sSh.key = (sSh.key < 0 ? 0 : sSh.key) + 1; }
    SDL_UnlockMutex(sMx);
}
void model_place(float x, float feet, float h) {
    SDL_LockMutex(sMx);
    sSh.place[0] = x;
    sSh.place[1] = feet;
    sSh.place[2] = h;
    SDL_UnlockMutex(sMx);
}
void lines_off(void) {
    SDL_LockMutex(sMx);
    for (int k = 0; k < kLineMax; k++) { sSh.line[k] = -1; }
    SDL_UnlockMutex(sMx);
}
void leave(int how) {
    SDL_LockMutex(sMx);
    sSh.leave = how;
    SDL_UnlockMutex(sMx);
}

// =============================================================================================== drawing (in the manner of Xenoverse's menus)
// Translucent navy panels with cut corners and a cyan edge over the place behind them, the cursor an orange bar,
// titles at the top left on a slanted band, the warrior's level and zeni at the top right, the buttons at the
// bottom right. Violet is kept for the Corrupted.
float sX0, sY0, sW, sH, sS;
ImVec2 P(float x, float y) { return ImVec2(sX0 + x * sW / 1376.0f, sY0 + y * sH / 768.0f); }
float S(float l) { return l * sS; }
ImFont *sFont, *sBold;
ImDrawList *sDl;
const ImU32 kInk = IM_COL32(255, 255, 255, 255), kDim = IM_COL32(176, 204, 236, 255), kLock = IM_COL32(112, 128, 156, 255);
const ImU32 kHi1 = IM_COL32(255, 182, 52, 255), kHi2 = IM_COL32(232, 96, 14, 255);   // the cursor's orange
const ImU32 kCyan = IM_COL32(96, 200, 255, 255), kNavy = IM_COL32(8, 20, 46, 222);
const ImU32 kGold = IM_COL32(255, 216, 80, 255), kVio = IM_COL32(196, 96, 255, 255), kGreen = IM_COL32(110, 236, 150, 255);
bool sKeyboardLast;
int sFrames;
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
    StoryKit_FrameStart();
}
void grad(ImVec2 a, ImVec2 b, ImU32 top, ImU32 bottom, float round) {
    int v0 = sDl->VtxBuffer.Size;
    sDl->AddRectFilled(a, b, IM_COL32(255, 255, 255, (top >> IM_COL32_A_SHIFT) & 0xFF), round);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(sDl, v0, sDl->VtxBuffer.Size, a, ImVec2(a.x, b.y), top, bottom);
}
void hgrad(ImVec2 a, ImVec2 b, ImU32 left, ImU32 right) {
    sDl->AddRectFilledMultiColor(a, b, left, right, right, left);
}
float text_w(ImFont *f, float size, const char *t) { return f->CalcTextSizeA(size, FLT_MAX, 0.0f, t).x; }
void text(ImFont *f, float size, ImVec2 at, ImU32 c, const std::string &t, float maxW = 0.0f) {
    std::string s = t;
    if (maxW > 0.0f && text_w(f, size, s.c_str()) > maxW) {
        while (!s.empty() && text_w(f, size, (s + "...").c_str()) > maxW) {
            s.pop_back();
            while (!s.empty() && ((unsigned char)s.back() & 0xC0) == 0x80) { s.pop_back(); }
        }
        s += "...";
    }
    sDl->AddText(f, size, at, c, s.c_str());
}
void wrapped(ImFont *f, float size, ImVec2 at, ImU32 c, const std::string &t, float w) { sDl->AddText(f, size, at, c, t.c_str(), NULL, w); }
void outlined(ImFont *f, float size, ImVec2 at, ImU32 c, ImU32 edge, const std::string &t, float w = 2.0f) {
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            if (dx || dy) { sDl->AddText(f, size, ImVec2(at.x + dx * w, at.y + dy * w), edge, t.c_str()); }
        }
    }
    sDl->AddText(f, size, at, c, t.c_str());
}
std::string upper(const std::string &s) { std::string u = s; for (char &c : u) { c = (char)SDL_toupper((unsigned char)c); } return u; }
// A shape with its top left and bottom right corners cut
void chamfer(ImVec2 a, ImVec2 b, ImU32 fill, float cut) {
    ImVec2 pts[6] = {ImVec2(a.x + cut, a.y), ImVec2(b.x, a.y), ImVec2(b.x, b.y - cut), ImVec2(b.x - cut, b.y), ImVec2(a.x, b.y), ImVec2(a.x, a.y + cut)};
    sDl->AddConvexPolyFilled(pts, 6, fill);
}
void chamfer_edge(ImVec2 a, ImVec2 b, ImU32 c, float cut, float th) {
    ImVec2 pts[6] = {ImVec2(a.x + cut, a.y), ImVec2(b.x, a.y), ImVec2(b.x, b.y - cut), ImVec2(b.x - cut, b.y), ImVec2(a.x, b.y), ImVec2(a.x, a.y + cut)};
    sDl->AddPolyline(pts, 6, c, ImDrawFlags_Closed, th);
}
// A bar leaning forward (the cursor, the title's band)
void slant(ImVec2 a, ImVec2 b, ImU32 left, ImU32 right, float lean) {
    int v0 = sDl->VtxBuffer.Size;
    ImVec2 pts[4] = {ImVec2(a.x + lean, a.y), ImVec2(b.x + lean, a.y), ImVec2(b.x, b.y), ImVec2(a.x, b.y)};
    sDl->AddConvexPolyFilled(pts, 4, IM_COL32(255, 255, 255, 255));
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(sDl, v0, sDl->VtxBuffer.Size, a, ImVec2(b.x, a.y), left, right);
}
void panel(ImVec2 a, ImVec2 b) {
    float cut = S(16);
    chamfer(ImVec2(a.x + S(4), a.y + S(6)), ImVec2(b.x + S(4), b.y + S(6)), IM_COL32(0, 0, 0, 70), cut);
    chamfer(a, b, kNavy, cut);
    chamfer_edge(a, b, IM_COL32(110, 186, 255, 120), cut, S(1.5f));
    hgrad(ImVec2(a.x + cut, a.y), ImVec2(b.x, a.y + S(3)), IM_COL32(96, 200, 255, 230), IM_COL32(96, 200, 255, 0));
}
void face_at(int chara, ImVec2 a, ImVec2 b, float round) {
    ImTextureID t;
    ImVec2 u0, u1;
    sDl->AddRectFilled(a, b, IM_COL32(40, 70, 120, 120), round);
    if (chara >= 0 && StoryKit_Face(chara, &t, &u0, &u1)) { sDl->AddImageRounded(ImTextureRef(t), a, b, u0, u1, IM_COL32_WHITE, round); }
    sDl->AddRect(a, b, IM_COL32(140, 200, 255, 160), round, 0, S(1.2f));
}
enum { B_CROSS, B_CIRCLE, B_SQUARE, B_TRIANGLE, B_L1R1, B_START, B_LR };
float glyph(int b, ImVec2 c, float r) {
    static const char *const kKeys[] = {"Enter", "Esc", "F", "R", "Q/E", "Tab", "Left/Right"};
    if (sKeyboardLast || b >= B_L1R1) {
        const char *k = sKeyboardLast ? kKeys[b] : b == B_L1R1 ? "L1 R1" : b == B_START ? "START" : "< >";
        float w = text_w(sFont, r * 1.05f, k) + r * 1.2f;
        sDl->AddRectFilled(ImVec2(c.x, c.y - r * 0.85f), ImVec2(c.x + w, c.y + r * 0.85f), IM_COL32(14, 30, 64, 255), r * 0.4f);
        sDl->AddRect(ImVec2(c.x, c.y - r * 0.85f), ImVec2(c.x + w, c.y + r * 0.85f), IM_COL32(150, 206, 255, 255), r * 0.4f, 0, S(1.5f));
        sDl->AddText(sFont, r * 1.05f, ImVec2(c.x + r * 0.6f, c.y - r * 0.58f), kInk, k);
        return w;
    }
    ImVec2 m(c.x + r, c.y);
    sDl->AddCircleFilled(m, r, IM_COL32(14, 30, 64, 255));
    sDl->AddCircle(m, r, IM_COL32(150, 206, 255, 255), 0, S(1.5f));
    float k = r * 0.45f, t = S(2.2f);
    if (b == B_CROSS) {
        sDl->AddLine(ImVec2(m.x - k, m.y - k), ImVec2(m.x + k, m.y + k), IM_COL32(140, 180, 255, 255), t);
        sDl->AddLine(ImVec2(m.x - k, m.y + k), ImVec2(m.x + k, m.y - k), IM_COL32(140, 180, 255, 255), t);
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
    float r = S(12), size = S(17), gap = S(22), total = 0;
    for (const Prompt &p : ps) { total += r * 2.0f + S(6) + text_w(sFont, size, p.label.c_str()) + gap; }
    if (sKeyboardLast) { total += S(30) * (float)ps.size(); }
    ImVec2 at = P(1340, 744);
    float x = std::max(at.x - total, P(40, 0).x);
    hgrad(ImVec2(x - S(40), at.y - S(18)), ImVec2(P(1376, 0).x, at.y + S(18)), IM_COL32(4, 12, 30, 0), IM_COL32(4, 12, 30, 200));
    for (const Prompt &p : ps) {
        x += glyph(p.b, ImVec2(x, at.y), r) + S(6);
        outlined(sFont, size, ImVec2(x, at.y - size * 0.55f), kInk, IM_COL32(2, 8, 24, 255), p.label, S(1.0f));
        x += text_w(sFont, size, p.label.c_str()) + gap;
    }
}
void tip_box(const std::string &line, int who) {
    ImVec2 a = P(40, 668), b = P(1000, 720);
    hgrad(a, b, IM_COL32(6, 16, 40, 235), IM_COL32(6, 16, 40, 120));
    sDl->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, a.y), IM_COL32(96, 200, 255, 160), S(1.5f));
    float r = (b.y - a.y) * 0.5f - S(5);
    ImVec2 c(a.x + S(10) + r, (a.y + b.y) * 0.5f);
    sDl->AddCircleFilled(c, r + S(2.5f), kCyan);
    ImTextureID t;
    ImVec2 u0, u1;
    if (who >= 0 && StoryKit_Face(who, &t, &u0, &u1)) { sDl->AddImageRounded(ImTextureRef(t), ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), u0, u1, IM_COL32_WHITE, r); }
    else { sDl->AddCircleFilled(c, r, IM_COL32(20, 50, 100, 255)); }
    float size = S(18);
    sDl->PushClipRect(a, b, true);
    bool two = text_w(sFont, size, line.c_str()) > (b.x - a.x) - r * 2 - S(40);
    sDl->AddText(sFont, size, ImVec2(c.x + r + S(16), c.y - size * 0.55f - (two ? size * 0.5f : 0)), kInk, line.c_str(), NULL, (b.x - c.x) - r - S(30));
    sDl->PopClipRect();
}
std::string sPlaceName; // shown at the top right
void status_bar(void) {
    if (!sSave.exists) { return; }
    std::string lv = "LV " + std::to_string(sSave.level);
    std::string z = std::to_string(sSave.zeni) + " Z";
    ImVec2 b = P(1350, 30), a(b.x - S(330), b.y);
    ImVec2 e(b.x, b.y + S(40));
    slant(ImVec2(a.x, a.y), e, IM_COL32(8, 20, 46, 60), IM_COL32(8, 20, 46, 230), S(-14));
    sDl->AddLine(ImVec2(a.x + S(40), e.y), ImVec2(b.x, e.y), IM_COL32(96, 200, 255, 200), S(1.5f));
    text(sBold, S(20), ImVec2(a.x + S(30), a.y + S(9)), kHi1, lv);
    text(sBold, S(18), ImVec2(a.x + S(100), a.y + S(10)), kInk, sSave.name, S(120));
    text(sBold, S(18), ImVec2(b.x - S(14) - text_w(sBold, S(18), z.c_str()), a.y + S(10)), kGold, z);
    if (!sPlaceName.empty()) { text(sFont, S(14), ImVec2(b.x - S(14) - text_w(sFont, S(14), sPlaceName.c_str()), e.y + S(6)), kDim, sPlaceName); }
}
void heading(const std::string &h, const std::string &sub) {
    ImVec2 a = P(0, 26), b = P(700, 96);
    hgrad(a, b, IM_COL32(8, 20, 46, 235), IM_COL32(8, 20, 46, 0));
    slant(ImVec2(P(34, 0).x, a.y + S(8)), ImVec2(P(44, 0).x, b.y - S(8)), kHi1, kHi2, S(8));
    sDl->AddLine(ImVec2(a.x, b.y), ImVec2(b.x, b.y), IM_COL32(96, 200, 255, 200), S(1.5f));
    if (!sub.empty()) { text(sBold, S(15), ImVec2(P(64, 0).x, a.y + S(8)), kCyan, upper(sub)); }
    outlined(sBold, S(38), ImVec2(P(62, 0).x, a.y + (sub.empty() ? S(16) : S(26))), kInk, IM_COL32(4, 12, 32, 255), upper(h), S(1.6f));
    status_bar();
}
std::string sNotice;
int sNoticeUntil;
void notice(const std::string &t) { sNotice = t; sNoticeUntil = sFrames + 60 * 4; fprintf(stderr, "bt3: patrol: %s\n", t.c_str()); }
void notice_draw(void) {
    if (sNotice.empty() || sFrames > sNoticeUntil) { return; }
    float size = S(18), w = text_w(sBold, size, sNotice.c_str()) + S(60);
    ImVec2 a(P(688, 0).x - w * 0.5f, P(0, 600).y), b(a.x + w, a.y + S(42));
    chamfer(a, b, IM_COL32(10, 26, 60, 240), S(10));
    chamfer_edge(a, b, kHi1, S(10), S(2));
    sDl->AddText(sBold, size, ImVec2(a.x + S(30), a.y + (S(42) - size) * 0.5f), kInk, sNotice.c_str());
}
void bar(ImVec2 a, float w, float h, float f, ImU32 c) {
    sDl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), IM_COL32(0, 0, 0, 150), h * 0.5f);
    if (f > 0.0f) { sDl->AddRectFilled(a, ImVec2(a.x + w * std::min(1.0f, f), a.y + h), c, h * 0.5f); }
}

// =============================================================================================== input (as the Story Creator's)
struct In { bool up, down, left, right, ok, back, sq, tri, l1, r1, start; float turn; };
In sIn;
int sScriptAt;
bool sTyping; // the name is being typed: letters are letters, not the screen's keys
bool key(ImGuiKey k, bool repeat = true) { return ImGui::IsKeyPressed(k, repeat); }
void read_input(bool live) {
    In in;
    memset(&in, 0, sizeof(in));
    if (live) {
        bool pad = false, kb = false;
        auto Pd = [&](ImGuiKey k, bool repeat) { bool p = key(k, repeat); pad |= p; return p; };
        auto K = [&](ImGuiKey k, bool repeat) { bool p = key(k, repeat); kb |= p; return p; };
        in.up = Pd(ImGuiKey_GamepadDpadUp, true) || Pd(ImGuiKey_GamepadLStickUp, true) || K(ImGuiKey_UpArrow, true) || (!sTyping && K(ImGuiKey_W, true));
        in.down = Pd(ImGuiKey_GamepadDpadDown, true) || Pd(ImGuiKey_GamepadLStickDown, true) || K(ImGuiKey_DownArrow, true) || (!sTyping && K(ImGuiKey_S, true));
        in.left = Pd(ImGuiKey_GamepadDpadLeft, true) || Pd(ImGuiKey_GamepadLStickLeft, true) || K(ImGuiKey_LeftArrow, true);
        in.right = Pd(ImGuiKey_GamepadDpadRight, true) || Pd(ImGuiKey_GamepadLStickRight, true) || K(ImGuiKey_RightArrow, true);
        in.ok = Pd(ImGuiKey_GamepadFaceDown, false) || (!sTyping && (K(ImGuiKey_Enter, false) || K(ImGuiKey_KeypadEnter, false) || K(ImGuiKey_Space, false)));
        in.back = Pd(ImGuiKey_GamepadFaceRight, false) || K(ImGuiKey_Escape, false) || (!sTyping && K(ImGuiKey_Backspace, false));
        in.sq = Pd(ImGuiKey_GamepadFaceLeft, false) || (!sTyping && K(ImGuiKey_F, false));
        in.tri = Pd(ImGuiKey_GamepadFaceUp, false) || (!sTyping && K(ImGuiKey_R, false));
        in.l1 = Pd(ImGuiKey_GamepadL1, true) || (!sTyping && K(ImGuiKey_Q, true));
        in.r1 = Pd(ImGuiKey_GamepadR1, true) || (!sTyping && K(ImGuiKey_E, true));
        in.start = Pd(ImGuiKey_GamepadStart, false) || K(ImGuiKey_Tab, false) || (sTyping && (K(ImGuiKey_Enter, false) || K(ImGuiKey_KeypadEnter, false)));
        ImGuiKeyData *rl = ImGui::GetKeyData(ImGui::GetCurrentContext(), ImGuiKey_GamepadRStickLeft);
        ImGuiKeyData *rr = ImGui::GetKeyData(ImGui::GetCurrentContext(), ImGuiKey_GamepadRStickRight);
        in.turn = (rr != NULL ? rr->AnalogValue : 0.0f) - (rl != NULL ? rl->AnalogValue : 0.0f);
        if (!sTyping && K(ImGuiKey_A, true)) { in.turn -= 4.0f; }
        if (!sTyping && K(ImGuiKey_D, true)) { in.turn += 4.0f; }
        if (pad) { sKeyboardLast = false; }
        if (kb) { sKeyboardLast = true; }
    }
    const char *script = getenv("BT3_PATROL_SCRIPT"); // "<frame>:<button>[,...]" from the screens' opening (testing)
    while (script != NULL) {
        const char *p = script;
        for (int i = 0; i < sScriptAt && p != NULL; i++) { p = strchr(p, ','); p = p != NULL ? p + 1 : NULL; }
        if (p == NULL || atoi(p) > sFrames) { break; }
        const char *b = strchr(p, ':');
        sScriptAt++;
        if (b == NULL) { continue; }
        b++;
        std::string what(b, strcspn(b, ","));
        fprintf(stderr, "bt3: patrol: script %d: %s\n", sFrames, what.c_str());
        if (what == "up") { in.up = true; } else if (what == "down") { in.down = true; } else if (what == "left") { in.left = true; }
        else if (what == "right") { in.right = true; } else if (what == "ok") { in.ok = true; } else if (what == "back") { in.back = true; }
        else if (what == "sq") { in.sq = true; } else if (what == "tri") { in.tri = true; } else if (what == "l1") { in.l1 = true; }
        else if (what == "r1") { in.r1 = true; } else if (what == "start") { in.start = true; }
    }
    sIn = in;
}

// =============================================================================================== lists
struct Row {
    std::string label, value, sub;
    bool sec = false, locked = false;
    int face = -1, model = -1, modelCostume = 0;
    ImU32 valueColor = 0;
    std::string tip;
    int tipFace = -1;
    std::function<void()> ok, sq;
};
struct List { int cur = 0, top = 0; };
// Draws rows in [a, b] and works the cursor; returns the row under it
int draw_rows(std::vector<Row> &rows, List &l, ImVec2 a, ImVec2 b, bool input) {
    if (rows.empty()) { return -1; }
    int n = (int)rows.size();
    auto selectable = [&](int i) { return i >= 0 && i < n && !rows[(size_t)i].sec; };
    if (l.cur >= n) { l.cur = n - 1; }
    if (!selectable(l.cur)) { for (int i = 0; i < n; i++) { if (selectable(i)) { l.cur = i; break; } } }
    if (input) {
        int was = l.cur;
        if (sIn.up) { int i = l.cur - 1; while (i >= 0 && !selectable(i)) { i--; } if (i >= 0) { l.cur = i; } }
        if (sIn.down) { int i = l.cur + 1; while (i < n && !selectable(i)) { i++; } if (i < n) { l.cur = i; } }
        int fitPage = std::max(1, (int)((b.y - a.y - S(28)) / S(46)));
        if (sIn.l1) { for (int k = 0; k < fitPage; k++) { int i = l.cur - 1; while (i >= 0 && !selectable(i)) { i--; } if (i >= 0) { l.cur = i; } } }
        if (sIn.r1) { for (int k = 0; k < fitPage; k++) { int i = l.cur + 1; while (i < n && !selectable(i)) { i++; } if (i < n) { l.cur = i; } } }
        if (l.cur != was) { sound(0); }
    }
    float rh = S(46), y = a.y + S(14);
    int fit = std::max(1, (int)((b.y - a.y - S(28)) / rh));
    if (l.cur < l.top) { l.top = l.cur; }
    if (l.cur >= l.top + fit) { l.top = l.cur - fit + 1; }
    if (l.top > 0 && l.top > n - fit) { l.top = std::max(0, n - fit); }
    for (int i = l.top; i < n && i < l.top + fit; i++, y += rh) {
        Row &r = rows[(size_t)i];
        ImVec2 ra(a.x + S(14), y), rb(b.x - S(14), y + rh - S(6));
        if (r.sec) {
            text(sBold, S(14), ImVec2(ra.x + S(4), ra.y + S(15)), kCyan, upper(r.label));
            sDl->AddLine(ImVec2(ra.x + S(4) + text_w(sBold, S(14), upper(r.label).c_str()) + S(10), ra.y + S(24)), ImVec2(rb.x, ra.y + S(24)), IM_COL32(96, 200, 255, 90), S(1.2f));
            continue;
        }
        bool on = i == l.cur;
        if (on) {
            slant(ra, rb, kHi1, kHi2, S(10));
            sDl->AddLine(ImVec2(ra.x + S(10), ra.y), ImVec2(rb.x + S(10), ra.y), IM_COL32(255, 236, 180, 255), S(1.5f));
        } else {
            sDl->AddRectFilled(ra, rb, IM_COL32(120, 180, 255, 16), 0.0f);
            sDl->AddRectFilled(ra, ImVec2(ra.x + S(3), rb.y), IM_COL32(96, 200, 255, 120));
        }
        float x = ra.x + S(14);
        if (r.face >= 0) {
            face_at(r.face, ImVec2(x, ra.y + S(4)), ImVec2(x + rh - S(14), rb.y - S(4)), S(4));
            x += rh;
        }
        ImU32 c = r.locked ? kLock : kInk;
        float size = S(19);
        if (on) {
            outlined(sBold, size, ImVec2(x, ra.y + (rh - S(6) - size) * 0.5f - (r.sub.empty() ? 0 : S(7))), kInk, IM_COL32(110, 40, 0, 255), r.label, S(1.0f));
        } else {
            text(sFont, size, ImVec2(x, ra.y + (rh - S(6) - size) * 0.5f - (r.sub.empty() ? 0 : S(7))), c, r.label, (rb.x - x) * 0.62f);
        }
        if (!r.sub.empty()) { text(sFont, S(13), ImVec2(x, ra.y + (rh - S(6)) * 0.5f + S(3)), on ? IM_COL32(255, 240, 220, 255) : r.locked ? kLock : kDim, r.sub, (rb.x - x) * 0.62f); }
        if (!r.value.empty()) {
            float w = text_w(sBold, S(16), r.value.c_str());
            ImU32 vc = r.valueColor != 0 ? r.valueColor : r.locked ? kLock : on ? kInk : kGold;
            text(sBold, S(16), ImVec2(rb.x - w - S(14), ra.y + (rh - S(6) - S(16)) * 0.5f), vc, r.value);
        }
    }
    if (l.top > 0) { sDl->AddTriangleFilled(ImVec2((a.x + b.x) * 0.5f - S(8), a.y + S(10)), ImVec2((a.x + b.x) * 0.5f + S(8), a.y + S(10)), ImVec2((a.x + b.x) * 0.5f, a.y + S(2)), kCyan); }
    if (l.top + fit < n) { sDl->AddTriangleFilled(ImVec2((a.x + b.x) * 0.5f - S(8), b.y - S(10)), ImVec2((a.x + b.x) * 0.5f + S(8), b.y - S(10)), ImVec2((a.x + b.x) * 0.5f, b.y - S(2)), kCyan); }
    Row &cur = rows[(size_t)l.cur];
    if (input && sIn.ok && cur.ok) {
        if (cur.locked) { sound(2); } else { sound(1); cur.ok(); }
    } else if (input && sIn.sq && cur.sq) {
        sound(1);
        cur.sq();
    }
    return l.cur;
}

// =============================================================================================== screens
enum Scr { S_HUB, S_TITLE, S_RACE, S_NAME, S_PEOPLE, S_STORY, S_QUESTS, S_MASTERS, S_MASTER, S_BOARD, S_MENU, S_STATS, S_GEAR, S_SKILLS,
           S_LOG, S_TALK, S_RESULT, S_WARDROBE, S_MAP, S_COUNT };
Scr sScr = S_TITLE;
std::vector<Scr> sStack;
List sLists[S_COUNT + 4];
void go(Scr s) { sStack.push_back(sScr); sScr = s; }
void back(void) { if (!sStack.empty()) { sScr = sStack.back(); sStack.pop_back(); } }
void reset_to(Scr s) { sStack.clear(); sScr = s; }

// A talk: lines said in turn (the speaker's face, name and text), then what follows
struct Say { int chara; std::string name, text; };
std::vector<Say> sTalk;
size_t sTalkAt;
std::function<void()> sTalkThen;
void talk(const std::vector<Say> &lines, std::function<void()> then) {
    if (lines.empty()) { if (then) { then(); } return; }
    sTalk = lines;
    sTalkAt = 0;
    sTalkThen = then;
    go(S_TALK);
}
Say say_of(const TalkLn &l) {
    if (l.chara == -2) { return {fighter_chara(), sSave.name, fill(l.text)}; }
    return {l.chara, StoryKit_ShortName(l.chara), fill(l.text)};
}
void talk_lines(const std::vector<TalkLn> &lines, std::function<void()> then) {
    std::vector<Say> v;
    for (const TalkLn &l : lines) { v.push_back(say_of(l)); }
    talk(v, then);
}
// A question with two answers
std::string sAsk;
std::function<void()> sAskYes;
bool sAsking;
int sAskCur;
void ask(const std::string &q, std::function<void()> yes) { sAsk = q; sAskYes = yes; sAsking = true; sAskCur = 0; }

// Results of the last battle (shown on S_RESULT), and what is said after it
std::vector<std::string> sResultLines;
std::string sResultTitle;
std::vector<TalkLn> sAfterTalk;

// ---- new game
int sRaceCur;
float sRaceAnim;
std::string sName;
int sKeyCur;
const char *const kKeys = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

// ---- battles: a saga of one episode, played by the Story Creator's engine
J line_fill(const J &src) { // a scene line of the content: names and voices put in
    J l = J::obj();
    for (size_t i = 0; i < src.k.size(); i++) { l.set(src.k[i].c_str(), src.a[i]); }
    std::string sp = src["speaker"].text("enemy");
    l.set("speaker", J::str(sp));
    l.set("text", J::str(fill(src["text"].text())));
    if (sp == "player" && !src["name"].isStr()) { l.set("name", J::str(sSave.name)); }
    if (sp.compare(0, 5, "enemy") == 0 && src["voice"].nul()) { l.set("voice", J::str("random")); }
    return l;
}
J scene_of(const J &lines, int distance) {
    J sc = J::obj();
    sc.set("distance", J::num(distance));
    J ls = J::arr();
    for (size_t i = 0; i < lines.size(); i++) { ls.push(line_fill(lines[i])); }
    sc.set("lines", ls);
    return sc;
}
J scene_said(int chara, const std::string &t, const char *fx = "") { // one line by the first foe
    J l = J::obj();
    l.set("speaker", J::str("enemy"));
    l.set("text", J::str(fill(t)));
    if (fx[0]) { J f = J::arr(); f.push(J::str(fx)); l.set("effects", f); }
    (void)chara;
    J a = J::arr();
    a.push(l);
    return a;
}
void play(J ep, const std::vector<int> &allies, int kind, int a, int b) {
    J me;
    fighter_ref(&me);
    J p = J::obj();
    p.set("chara", me);
    J pl = J::arr();
    pl.push(p);
    for (int c : allies) { if (c >= 0 && c < 161) { J x = J::obj(); x.set("chara", J::num(c)); pl.push(x); } }
    ep.set("player", pl);
    J saga = J::obj();
    saga.set("format", J::num(1));
    saga.set("title", J::str("Corrupt Patrol"));
    J eps = J::arr();
    eps.push(ep);
    saga.set("episodes", eps);
    sDoc.clear();
    js_write(saga, sDoc);
    sFightKind = kind;
    sFightA = a;
    sFightB = b;
    if (getenv("BT3_PATROL_DUMP") != NULL) { write_file(sSaveDir + "/_last_battle.json", sDoc); }
    leave(2);
}
J enemy_j(const std::vector<int> &foes, bool corrupt) {
    J l = J::arr();
    for (int c : foes) {
        J e = J::obj();
        e.set("chara", J::num(c));
        if (corrupt) { e.set("corrupt", J::boolean(true)); }
        l.push(e);
    }
    return l;
}
J battle_j(int cpuLevel, const std::vector<int> &enemyItems, int seconds) {
    J bt = J::obj();
    bt.set("fight", J::boolean(true));
    bt.set("win", J::str("ko"));
    bt.set("time", J::num(seconds));
    bt.set("time_up", J::str("health"));
    bt.set("cpu_level", J::num(cpuLevel));
    bt.set("player_items", ints(player_items()));
    bt.set("enemy_items", ints(enemyItems));
    if (getenv("BT3_PATROL_HP") != NULL) { bt.set("player_health", J::num(atoi(getenv("BT3_PATROL_HP")))); } // (testing: a quick loss)
    if (getenv("BT3_PATROL_FOE_HP") != NULL) { bt.set("enemy_health", J::num(atoi(getenv("BT3_PATROL_FOE_HP")))); } // (... a quick win)
    return bt;
}
// How strong a foe of a mission of `level` is: the AI, and every stat raised the further the story is
std::vector<int> foe_items(int level, bool chapter, int index) {
    int tier = std::min(3, level / 12);
    std::vector<int> items;
    if (tier > 0) { items = {tier, 3 + tier, 6 + tier, 9 + tier}; }
    if (chapter && index >= 5) { items.push_back(125); }
    if (chapter && index >= 10) { items.push_back(126); }
    return items;
}
int foe_cpu(int level) { return std::max(4, std::min(38, 4 + (level * 3) / 4)); }
const Mission &mission_of(int chapter, int i) { return chapter ? gChapters[(size_t)i] : gQuests[(size_t)i]; }
void play_mission(bool chapter, int i) {
    const Mission &m = mission_of(chapter, i);
    J ep = J::obj();
    ep.set("title", J::str(chapter ? "Chapter " + std::to_string(i + 1) + ": " + m.title : m.title));
    ep.set("description", J::str(m.summary.empty() ? std::string("Corrupt Patrol") : m.summary));
    ep.set("stage", J::num(m.stage));
    ep.set("music", J::num(m.music));
    ep.set("enemy", enemy_j(m.foes, m.corrupt));
    ep.set("intro", scene_of(m.intro, 2));
    ep.set("battle", battle_j(foe_cpu(m.level), foe_items(m.level, chapter, i), 0));
    ep.set("win", scene_of(m.win, 2));
    ep.set("lose", scene_of(m.lose, 2));
    play(ep, m.allies, FIGHT_MISSION, chapter ? 1 : 0, i);
}
const Loc &home_of(int chara) { return loc_of(master_info(chara).home.empty() ? "timechamber" : master_info(chara).home); }
void play_training(int chara, int lesson) {
    const MasterInfo &mi = master_info(chara);
    J ep = J::obj();
    ep.set("title", J::str(StoryKit_ShortName(chara) + "'s Training " + std::to_string(lesson + 1) + " of " + std::to_string(kLessons)));
    ep.set("description", J::str("Lesson: " + move_name(chara, lesson)));
    ep.set("stage", J::num(lesson == kLessons - 1 ? home_of(chara).stage : 8)); // (the Time Chamber; the last lesson at their home)
    ep.set("music", J::num(9 + lesson % 4));
    ep.set("enemy", enemy_j({chara}, false));
    ep.set("intro", scene_of(scene_said(chara, mi.greet.empty() ? "Show me what you've learned. Don't hold back." : mi.greet), 2));
    int tier = std::min(3, lesson / 2 + sSave.level / 20);
    std::vector<int> items;
    if (tier > 0) { items = {tier, 3 + tier, 6 + tier, 9 + tier}; }
    ep.set("battle", battle_j(std::min(38, 6 + lesson * 5 + sSave.level / 3), items, 0));
    ep.set("win", scene_of(scene_said(chara, lesson == kLessons - 1 ? (mi.done.empty() ? "That's everything I have. It's yours now." : mi.done)
                                                                      : "Good. That one's yours now."), 2));
    ep.set("lose", scene_of(scene_said(chara, "Not yet. Get up and come back when you're ready."), 2));
    play(ep, {}, FIGHT_TRAIN, chara, lesson);
}
int bounty_tier(void) { return std::min((int)kBountyTiers.size() - 1, (sSave.level - 1) / 10); }
void play_bounty(void) {
    int t = bounty_tier();
    const std::vector<int> &pool = kBountyTiers[(size_t)t];
    int foe = pool[(size_t)(SDL_rand((Sint32)pool.size()))];
    int stage = kBountyStages[SDL_rand((Sint32)(sizeof(kBountyStages) / sizeof(kBountyStages[0])))];
    static const char *const kTaunts[] = {
        "The Patrol's errand runner? Perfect. I was getting bored of hitting rocks.",
        "Feel that? The Malice says you're weak. I'm inclined to agree.",
        "Turn around and I'll let you keep your limbs. Final offer.",
        "Every one of you I break makes the whispers louder. Come here.",
    };
    J ep = J::obj();
    ep.set("title", J::str("Bounty: Corrupted " + StoryKit_ShortName(foe)));
    ep.set("description", J::str("Reward: " + std::to_string(600 + t * 700) + " zeni"));
    ep.set("stage", J::num(stage));
    ep.set("music", J::num(16));
    ep.set("enemy", enemy_j({foe}, true));
    ep.set("intro", scene_of(scene_said(foe, kTaunts[SDL_rand(4)], "aura_on"), 2));
    std::vector<int> items;
    if (t > 0) { items = {std::min(3, t), 3 + std::min(3, t)}; }
    ep.set("battle", battle_j(std::min(32, 6 + t * 6), items, 0));
    play(ep, {}, FIGHT_BOUNTY, t, foe);
}

// ---- what is open to the player
std::string mission_lock(const Mission &m) { // why it can't be taken yet, "" it can
    if (m.chapter) {
        if (sSave.chapter < m.index) { return "Clear the chapters before it"; }
        return "";
    }
    if (sSave.chapter < m.reqChapter) { return "After Chapter " + std::to_string(m.reqChapter) + " of the story"; }
    for (const std::string &q : m.reqQuests) {
        if (!quest_done(q)) {
            for (const Mission &o : gQuests) { if (o.id == q) { return "After \"" + o.title + "\""; } }
            return "After another quest";
        }
    }
    if (sSave.level < m.reqLevel) { return "Level " + std::to_string(m.reqLevel) + " needed"; }
    return "";
}
int quests_open_at(const std::string &loc) {
    int n = 0;
    for (const Mission &m : gQuests) { if (m.location == loc && !quest_done(m.id) && mission_lock(m).empty()) { n++; } }
    if (loc == "lookout" && sSave.chapter < (int)gChapters.size()) { n++; }
    return n;
}

// ---- the warrior's look and moves: the Custom Character screen in its Patrol mode (gs/creator_ui.cpp)
std::string race_ids_csv(void) { // the race's ranges written out, and the masters' outfits earned
    std::string out;
    const char *c = kRaceIds[sSave.race];
    while (*c) {
        int a = atoi(c), b = a;
        while (*c >= '0' && *c <= '9') { c++; }
        if (*c == '-') { c++; b = atoi(c); while (*c >= '0' && *c <= '9') { c++; } }
        for (int i = a; i <= b; i++) { out += (out.empty() ? "" : ",") + std::to_string(i); }
        if (*c == ',') { c++; }
    }
    for (int m : sSave.masters) {
        if (master_wins(m) >= kLessons) { out += "," + std::to_string(m); }
    }
    return out;
}
J move_ref(int chara, int slot) { J r = J::obj(); r.set("chara", J::num(chara)); r.set("slot", J::str(kSlotKeys[std::max(0, std::min(4, slot))])); return r; }
std::string kit_json(void) { // what the mode sets on the warrior: the techniques in their slots, the stats, the race
    J kit = J::obj();
    J moves = J::obj();
    moves.set("blast1_1", move_ref(31, 0));  // Explosive Wave
    moves.set("blast1_2", move_ref(3, 1));   // (Goku's second skill)
    moves.set("blast2_1", move_ref(73, 2));  // Full Power Energy Wave
    moves.set("blast2_2", move_ref(17, 3));  // Full Power Energy Blast Volley
    moves.set("ultimate", move_ref(31, 4));  // Super Explosive Wave
    for (int k = 0; k < 5; k++) { // (those chosen from the masters' lessons)
        const J &l = sSave.loadout[kSlotKeys[k]];
        if (l.isObj() && l["chara"].number(-1) >= 0) { moves.set(kSlotKeys[k], move_ref((int)l["chara"].number(), (int)l["slot"].number(k))); }
    }
    kit.set("moves", moves);
    const J &style = sSave.loadout["style"]; // (a master's fighting style, once their training is complete)
    if (style.isObj() && style["chara"].number(-1) >= 0) {
        J m = J::obj();
        m.set("chara", J::num(style["chara"].number()));
        kit.set("melee", m);
        kit.set("ki_blasts", m);
        kit.set("stance", m);
    }
    kit.set("health", J::num(30000));
    kit.set("ki", J::num(30000));
    kit.set("blast", J::num(3));
    kit.set("race", J::str(kRaces[sSave.race].name));
    std::string o;
    js_write(kit, o);
    return o;
}
bool sAwaitCreator; // the creator was opened for the warrior: its result is read when the screens open again
bool sCreatorNew;   // ... for a new warrior (the welcome follows)
bool sAfterShop;    // the game's shop was open: back to the place after it
void open_creator(bool auto_) {
    std::string kit = kit_json(), ids = race_ids_csv(), body = std::to_string(kRaces[sSave.race].avatar);
    PortCreator_PatrolBegin(sSave.name.c_str(), sSave.fighterFile.c_str(), ids.c_str(), body.c_str(), kRaces[sSave.race].avatarCostume, kit.c_str());
    if (auto_) { PortCreator_PatrolAuto(); }
    sAwaitCreator = true;
    leave(3);
}
void welcome(void) {
    if (!gWelcome.empty()) { talk_lines(gWelcome, nullptr); return; }
    talk({{58, "Supreme Kai", "Welcome to the Corrupt Patrol, " + sSave.name + ". Walk up to anyone and press Circle to talk; Triangle opens the world map."}}, nullptr);
}
std::vector<TalkLn> service(const char *what) { auto it = gServices.find(what); return it != gServices.end() ? it->second : std::vector<TalkLn>(); }
void service_line(const char *what, int chara, const char *def, std::function<void()> then) {
    std::vector<TalkLn> v = service(what);
    TalkLn l = v.empty() ? TalkLn{chara, def} : v[(size_t)SDL_rand((Sint32)v.size())];
    talk_lines({l}, then);
}

// ---- the places: each a battle without a fight to walk about in (src/battle/port_story_btl.c, 9007)
struct Who { std::string role; int chara; }; // a person of the place: a role of the content, or "master" (one at home here)
std::vector<Who> sHubWho;   // who is who in the place's battle
bool sHubQueued;
bool sArrive;               // the next place is flown to (the landing)
int sArriveFrom = -1;       // ... from this place (the flight on the map while it loads)
int trainer_chara(void) { return sSave.masters.empty() ? 59 : sSave.masters.back(); }
std::vector<Who> people_at(const Loc &l) {
    std::vector<Who> v;
    for (const Person &p : l.people) {
        int c = p.role == "trainer" ? trainer_chara() : p.chara;
        if (c >= 0) { v.push_back({p.role, c}); }
    }
    for (auto it = sSave.masters.rbegin(); it != sSave.masters.rend() && v.size() < 8; ++it) { // (the masters who live here, the newest first)
        if (master_info(*it).home != l.id) { continue; }
        bool there = false;
        for (const Who &w : v) { there |= w.chara == *it; }
        if (!there) { v.push_back({"master", *it}); }
    }
    return v;
}
std::string role_name(const Who &w) {
    if (w.role == "story") { return "Main Story"; }
    if (w.role == "wardrobe") { return "Wardrobe"; }
    if (w.role == "trainer") { return "Training Grounds"; }
    if (w.role == "shop") { return "Item Shop"; }
    if (w.role == "board") { return "Bounty Board"; }
    if (w.role == "quests") { return "Quests"; }
    return "Master";
}
void play_hub(void) {
    const Loc &l = loc_of(sSave.location);
    J ep = J::obj();
    ep.set("title", J::str(l.name));
    ep.set("stage", J::num(l.stage));
    ep.set("music", J::num(l.music));
    J foe = J::obj();
    foe.set("chara", J::num(79));
    J fl = J::arr();
    fl.push(foe);
    ep.set("enemy", fl);
    J bt = J::obj();
    bt.set("fight", J::boolean(true));
    bt.set("time", J::num(0));
    bt.set("cpu_level", J::num(-1));
    bt.set("player_items", ints(player_items()));
    ep.set("battle", bt);
    J hub = J::obj();
    J ppl = J::arr();
    sHubWho = people_at(l);
    for (const Who &w : sHubWho) {
        J n = J::obj();
        n.set("chara", J::num(w.chara));
        ppl.push(n);
    }
    hub.set("npcs", ppl);
    hub.set("arrive", J::boolean(sArrive));
    hub.set("spread", J::num(l.size == "large" ? 260 : l.size == "medium" ? 170 : 100));
    ep.set("hub", hub);
    sArrive = false;
    play(ep, {}, FIGHT_HUB, 0, 0);
}
void open_who(const Who &w); // (below the screens it opens)

// ---- after a battle: experience, zeni, lessons, the story
void gain(int xp, int zeni) {
    sSave.zeni = std::min(9999999, sSave.zeni + zeni);
    sResultLines.push_back("+" + std::to_string(xp) + " EXP     +" + std::to_string(zeni) + " ZENI");
    if (sSave.level >= kLevelMax) { return; }
    sSave.xp += xp;
    while (sSave.level < kLevelMax && sSave.xp >= xp_to_next(sSave.level)) {
        sSave.xp -= xp_to_next(sSave.level);
        sSave.level++;
        sResultLines.push_back("LEVEL UP! You are now level " + std::to_string(sSave.level) + ".");
        if (sSave.level % 8 == 1 && sSave.level / 8 <= 5) { sResultLines.push_back("The item shop has new stock."); }
        if (sSave.level % 7 == 0 && slot_budget() <= 8) { sResultLines.push_back("Item slots: " + std::to_string(slot_budget()) + "."); }
    }
}
void unlock_masters(const std::vector<int> &ms) {
    for (int c : ms) {
        if (c < 0 || c >= 161 || master_unlocked(c)) { continue; }
        sSave.masters.push_back(c);
        sResultLines.push_back("NEW MASTER: " + StoryKit_CharaName(c) + " (at " + home_of(c).name + ")");
    }
}
void on_result(int result) {
    sAfterTalk.clear();
    if (sFightKind == FIGHT_HUB) { // (left the place: for a person, the menu, another place, or out of the battle's own menu)
        int exit = gPortHubExit;
        fprintf(stderr, "bt3: patrol: left %s: %d\n", sSave.location.c_str(), exit);
        reset_to(S_HUB);
        sHubQueued = false;
        if (exit == 99) { go(S_MENU); }
        else if (exit >= 100 && exit - 100 < (int)gLocs.size()) { sArriveFrom = loc_index(sSave.location); sSave.location = gLocs[(size_t)(exit - 100)].id; sArrive = true; }
        else if (exit >= 0 && exit < (int)sHubWho.size()) { open_who(sHubWho[(size_t)exit]); }
        else { reset_to(S_TITLE); }
        save_write();
        return;
    }
    sResultLines.clear();
    bool won = result == 1;
    if (result == 3) {
        sResultTitle = "RETREATED";
        sResultLines.push_back("You pulled out of the fight. Nothing gained, nothing lost.");
    } else if (!won) {
        sResultTitle = "DEFEATED";
        sSave.lost++;
        sResultLines.push_back("You were beaten. Train with your masters, gear up, and try again.");
        gain(25, 0);
    } else {
        sResultTitle = "VICTORY";
        sSave.won++;
        if (sFightKind == FIGHT_MISSION) {
            bool chapter = sFightA == 1;
            const Mission &m = mission_of(chapter, sFightB);
            bool first = chapter ? sSave.chapter == sFightB : !quest_done(m.id);
            gain(first ? m.xp : m.xp / 3, first ? m.zeni : m.zeni / 4);
            if (first) {
                if (chapter) { sSave.chapter = sFightB + 1; } else { sSave.done.push_back(m.id); }
                unlock_masters(m.masters);
                sAfterTalk = m.after;
                if (chapter && sSave.chapter == (int)gChapters.size()) {
                    sResultLines.push_back("THE STORY IS COMPLETE. The Patrol still has work everywhere.");
                    sAfterTalk.insert(sAfterTalk.end(), gEnding.begin(), gEnding.end());
                }
            }
        } else if (sFightKind == FIGHT_TRAIN) {
            int c = sFightA;
            gain(140 + sFightB * 40 + sSave.level * 4, 400 + sFightB * 200);
            int &wins = sSave.wins[c];
            if (wins == sFightB) {
                wins++;
                sResultLines.push_back("TECHNIQUE LEARNT: " + move_name(c, sFightB) + " (" + kSlotKinds[sFightB] + ")");
                if (wins >= kLessons) {
                    sResultLines.push_back("Training with " + StoryKit_ShortName(c) + " complete!");
                    sResultLines.push_back("Their fighting style and outfit are unlocked.");
                }
            }
        } else if (sFightKind == FIGHT_BOUNTY) {
            sSave.bounties++;
            gain(90 + sFightA * 70, 600 + sFightA * 700);
        }
    }
    save_write();
    reset_to(S_HUB);
    go(S_RESULT);
}

// =============================================================================================== the world map
// A hologram of the world (made once, from noise and the places: land where they are), the places on it as markers,
// the other worlds beside it. The cursor moves to the nearest place in the direction pushed.
SDL_GPUTexture *sMapTex;
const int kMapW = 640, kMapH = 400;
float hash2(int x, int y) { unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u; h = (h ^ (h >> 13)) * 1274126177u; return (float)(h & 0xFFFFFF) / (float)0xFFFFFF; }
float vnoise(float x, float y) {
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - (float)xi, fy = y - (float)yi;
    float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
    float a = hash2(xi, yi), b = hash2(xi + 1, yi), c = hash2(xi, yi + 1), d = hash2(xi + 1, yi + 1);
    return a + (b - a) * sx + (c - a) * sy + (a - b - c + d) * sx * sy;
}
float land_at(float u, float v) {
    float n = 0, amp = 0.5f, f = 3.0f;
    for (int o = 0; o < 5; o++) { n += amp * vnoise(u * f + 11.0f, v * f * 0.62f + 7.0f); amp *= 0.5f; f *= 2.0f; }
    float bump = 0;
    for (const Loc &l : gLocs) {
        if (l.region != "earth") { continue; }
        float dx = u - l.mx, dy = (v - l.my) * 0.62f;
        bump += expf(-(dx * dx + dy * dy) / (0.006f));
    }
    float edge = std::min(std::min(u, 1 - u), std::min(v, 1 - v));
    return n + bump * 0.45f - 0.56f - (edge < 0.08f ? (0.08f - edge) * 4.0f : 0.0f);
}
void map_make(void) {
    if (sMapTex != NULL) { return; }
    uint8_t *pix = (uint8_t *)malloc((size_t)kMapW * kMapH * 4);
    if (pix == NULL) { return; }
    std::vector<float> f((size_t)kMapW * kMapH);
    for (int y = 0; y < kMapH; y++) {
        for (int x = 0; x < kMapW; x++) { f[(size_t)y * kMapW + x] = land_at((x + 0.5f) / kMapW, (y + 0.5f) / kMapH); }
    }
    for (int y = 0; y < kMapH; y++) {
        for (int x = 0; x < kMapW; x++) {
            float h = f[(size_t)y * kMapW + x];
            uint8_t *p = pix + ((size_t)y * kMapW + x) * 4;
            bool grid = (x % 40 == 0) || (y % 40 == 0);
            if (h > 0) {
                float k = std::min(1.0f, h * 4.0f);
                p[0] = (uint8_t)(30 + 40 * k); p[1] = (uint8_t)(130 + 70 * k); p[2] = (uint8_t)(170 + 50 * k); p[3] = (uint8_t)(150 + 60 * k);
                if (grid) { p[3] = 230; }
            } else {
                p[0] = 6; p[1] = 26; p[2] = 64; p[3] = grid ? 120 : (y % 3 == 0 ? 92 : 70);
            }
            // the coast: where land meets sea
            bool coast = false;
            if (x > 0 && y > 0 && x < kMapW - 1 && y < kMapH - 1) {
                float l0 = f[(size_t)y * kMapW + x - 1], l1 = f[(size_t)y * kMapW + x + 1], l2 = f[(size_t)(y - 1) * kMapW + x], l3 = f[(size_t)(y + 1) * kMapW + x];
                coast = ((h > 0) != (l0 > 0)) || ((h > 0) != (l1 > 0)) || ((h > 0) != (l2 > 0)) || ((h > 0) != (l3 > 0));
            }
            if (coast) { p[0] = 170; p[1] = 245; p[2] = 255; p[3] = 255; }
        }
    }
    sMapTex = Ui_Texture(pix, kMapW, kMapH); // (takes pix)
}
int sMapCur = -1;
// Where a place's marker is on the map drawn in [a, b] (the other worlds in the strip at its right)
ImVec2 map_at(const Loc &l, ImVec2 a, ImVec2 b) {
    float mw = (b.x - a.x) * 0.78f;
    if (l.region == "earth") { return ImVec2(a.x + l.mx * mw, a.y + l.my * (b.y - a.y)); }
    int k = 0, n = 0;
    for (const Loc &o : gLocs) { if (o.region != "earth") { if (&o == &l) { k = n; } n++; } }
    float sx = a.x + mw + (b.x - a.x - mw) * 0.5f;
    return ImVec2(sx, a.y + (b.y - a.y) * (0.16f + 0.7f * (n > 1 ? (float)k / (float)(n - 1) : 0.5f)));
}
// Moves the cursor (dx, dy one of -1 0 1) to the nearest place that way
void map_move(int dx, int dy, ImVec2 a, ImVec2 b) {
    if (sMapCur < 0 || sMapCur >= (int)gLocs.size()) { sMapCur = loc_index(sSave.location); return; }
    ImVec2 c = map_at(gLocs[(size_t)sMapCur], a, b);
    int best = -1;
    float bestD = 1e9f;
    for (size_t i = 0; i < gLocs.size(); i++) {
        if ((int)i == sMapCur) { continue; }
        ImVec2 p = map_at(gLocs[i], a, b);
        float vx = p.x - c.x, vy = p.y - c.y;
        float along = vx * dx + vy * dy, across = fabsf(vx * dy - vy * dx);
        if (along <= 0.0f || across > along * 1.4f) { continue; }
        float d = along + across * 2.0f;
        if (d < bestD) { bestD = d; best = (int)i; }
    }
    if (best >= 0) { sMapCur = best; sound(0); }
}
// The map in [a, b]; `fly` > 0: a flight drawn from `from` to the cursor (0..1 of the way)
void map_draw(ImVec2 a, ImVec2 b, float fly, int from) {
    map_make();
    float mw = (b.x - a.x) * 0.78f;
    chamfer(ImVec2(a.x - S(14), a.y - S(46)), ImVec2(b.x + S(14), b.y + S(110)), IM_COL32(4, 12, 32, 200), S(22));
    chamfer_edge(ImVec2(a.x - S(14), a.y - S(46)), ImVec2(b.x + S(14), b.y + S(110)), IM_COL32(96, 200, 255, 160), S(22), S(1.5f));
    text(sBold, S(22), ImVec2(a.x, a.y - S(38)), kInk, "WORLD MAP");
    text(sFont, S(14), ImVec2(a.x + S(150), a.y - S(32)), kDim, "Earth");
    text(sFont, S(14), ImVec2(a.x + mw + S(10), a.y - S(32)), kDim, "Other worlds");
    if (sMapTex != NULL) {
        int pulse = 200 + (int)(40 * sinf(sFrames * 0.05f));
        sDl->AddImage(ImTextureRef((ImTextureID)(intptr_t)sMapTex), a, ImVec2(a.x + mw, b.y), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, pulse));
    }
    sDl->AddRect(a, ImVec2(a.x + mw, b.y), IM_COL32(96, 200, 255, 120), 0.0f, 0, S(1));
    sDl->AddRectFilled(ImVec2(a.x + mw + S(8), a.y), b, IM_COL32(10, 16, 40, 170));
    float scan = fmodf(sFrames * 1.2f, b.y - a.y);
    sDl->AddLine(ImVec2(a.x, a.y + scan), ImVec2(a.x + mw, a.y + scan), IM_COL32(150, 230, 255, 50), S(2));
    int here = loc_index(sSave.location);
    for (size_t i = 0; i < gLocs.size(); i++) {
        const Loc &l = gLocs[i];
        ImVec2 p = map_at(l, a, b);
        bool on = (int)i == sMapCur;
        int open = quests_open_at(l.id);
        float r = S(l.region == "earth" ? 7 : 13);
        if (l.region != "earth") {
            sDl->AddCircleFilled(p, r, l.region == "space" ? IM_COL32(60, 150, 90, 230) : IM_COL32(150, 60, 60, 230));
            sDl->AddCircle(p, r, IM_COL32(220, 240, 255, 200), 0, S(1.2f));
        } else {
            sDl->AddCircleFilled(p, r, on ? kHi1 : IM_COL32(220, 245, 255, 235));
            sDl->AddCircle(p, r + S(3), on ? kHi1 : IM_COL32(96, 200, 255, 160), 0, S(1.5f));
        }
        if (on) {
            float k = S(16) + S(4) * sinf(sFrames * 0.15f);
            sDl->AddCircle(p, k, kHi1, 0, S(2.5f));
        }
        if ((int)i == here) {
            sDl->AddTriangleFilled(ImVec2(p.x - S(7), p.y - r - S(14)), ImVec2(p.x + S(7), p.y - r - S(14)), ImVec2(p.x, p.y - r - S(4)), kGreen);
        }
        if (open > 0) {
            ImVec2 q(p.x + r + S(2), p.y - r - S(2));
            sDl->AddCircleFilled(q, S(8), kHi2);
            text(sBold, S(11), ImVec2(q.x - S(3.5f), q.y - S(7)), kInk, open > 9 ? "!" : std::to_string(open));
        }
        if (on || l.region != "earth") {
            std::string nm = l.name;
            float w = text_w(sBold, S(15), nm.c_str());
            float tx = l.region != "earth" ? p.x - w * 0.5f : std::min(p.x + S(14), a.x + mw - w - S(4));
            outlined(sBold, S(15), ImVec2(tx, p.y + (l.region != "earth" ? r + S(4) : S(8))), on ? kHi1 : kInk, IM_COL32(2, 8, 24, 255), nm, S(1.2f));
        }
    }
    if (fly > 0.0f && from >= 0 && sMapCur >= 0) { // the flight: a trail from where the player was to where they go
        ImVec2 p0 = map_at(gLocs[(size_t)from], a, b), p1 = map_at(gLocs[(size_t)sMapCur], a, b);
        ImVec2 m((p0.x + p1.x) * 0.5f, std::min(p0.y, p1.y) - S(60));
        ImVec2 prev = p0;
        int steps = 24;
        for (int s = 1; s <= steps; s++) {
            float t = (float)s / steps * std::min(1.0f, fly);
            ImVec2 q((1 - t) * (1 - t) * p0.x + 2 * (1 - t) * t * m.x + t * t * p1.x, (1 - t) * (1 - t) * p0.y + 2 * (1 - t) * t * m.y + t * t * p1.y);
            sDl->AddLine(prev, q, kHi1, S(3));
            prev = q;
        }
        sDl->AddCircleFilled(prev, S(6), kInk);
    }
    // the place under the cursor
    if (sMapCur >= 0 && sMapCur < (int)gLocs.size()) {
        const Loc &l = gLocs[(size_t)sMapCur];
        ImVec2 ia(a.x, b.y + S(10));
        text(sBold, S(24), ia, kHi1, upper(l.name));
        std::string sz = l.size == "large" ? "Large area" : l.size == "medium" ? "Medium area" : "Small area";
        text(sFont, S(15), ImVec2(ia.x + text_w(sBold, S(24), upper(l.name).c_str()) + S(16), ia.y + S(7)), kDim,
             sz + (sMapCur == here ? "   -   You are here" : ""));
        wrapped(sFont, S(16), ImVec2(ia.x, ia.y + S(32)), kInk, l.desc, (b.x - a.x) * 0.62f);
        std::vector<Who> ppl = people_at(l);
        float fx = a.x + (b.x - a.x) * 0.66f;
        for (size_t k = 0; k < ppl.size() && k < 8; k++) {
            float s = S(34);
            face_at(ppl[k].chara, ImVec2(fx + (k % 4) * (s + S(6)), ia.y + S(4) + (k / 4) * (s + S(6))), ImVec2(fx + (k % 4) * (s + S(6)) + s, ia.y + S(4) + (k / 4) * (s + S(6)) + s), S(4));
        }
        int open = quests_open_at(l.id);
        if (open > 0) { text(sBold, S(14), ImVec2(fx, ia.y + S(84)), kHi1, std::to_string(open) + (open == 1 ? " quest open here" : " quests open here")); }
    }
}

// ---- the screens
const float kModelX = 1050.0f, kModelFeet = 640.0f, kModelH = 420.0f; // (the model at the right of most screens)
void place_default(void) { model_place(kModelX, kModelFeet, kModelH); lines_off(); }
void scr_title(bool input) {
    heading("Corrupt Patrol", "A Xenoverse-style adventure");
    ImVec2 a = P(40, 150), b = P(540, 420);
    panel(a, b);
    static std::vector<Row> rows;
    rows.clear();
    Row r;
    r.label = "Continue";
    r.locked = !sSave.exists;
    r.sub = sSave.exists ? sSave.name + ", " + kRaces[sSave.race].name + ", level " + std::to_string(sSave.level) : "No save yet";
    r.ok = [] { reset_to(S_HUB); sArrive = true; sArriveFrom = -1; };
    rows.push_back(r);
    r = Row();
    r.label = "New Game";
    r.sub = "Create a warrior and join the Patrol";
    r.ok = [] {
        auto start = [] { sRaceCur = 0; sRaceAnim = 0; sName.clear(); sKeyCur = 0; go(S_RACE); };
        if (sSave.exists) { ask("Start over? Your Corrupt Patrol save will be replaced.", start); }
        else { start(); }
    };
    rows.push_back(r);
    r = Row();
    r.label = "Return To Game";
    r.sub = "Back to the main menu";
    r.ok = [] { leave(1); };
    rows.push_back(r);
    int cur = draw_rows(rows, sLists[S_TITLE], a, b, input && !sAsking);
    if (sSave.exists) {
        ImVec2 sa = P(40, 440), sb = P(540, 620);
        panel(sa, sb);
        face_at(fighter_chara(), ImVec2(sa.x + S(18), sa.y + S(22)), ImVec2(sa.x + S(118), sa.y + S(122)), S(6));
        text(sBold, S(24), ImVec2(sa.x + S(136), sa.y + S(22)), kInk, sSave.name);
        text(sFont, S(16), ImVec2(sa.x + S(136), sa.y + S(56)), kDim, std::string(kRaces[sSave.race].name) + "   Level " + std::to_string(sSave.level) + "   " + loc_of(sSave.location).name);
        text(sFont, S(16), ImVec2(sa.x + S(136), sa.y + S(82)), kDim, "Story " + std::to_string(sSave.chapter) + " / " + std::to_string(gChapters.size()) + "   Masters " + std::to_string(sSave.masters.size()) + " / 161");
        int mins = (int)(sSave.playtime / 60);
        text(sFont, S(15), ImVec2(sa.x + S(136), sa.y + S(108)), kDim, "Played " + std::to_string(mins / 60) + "h " + std::to_string(mins % 60) + "m");
    }
    want_model(sSave.exists ? fighter_chara() : 138, 0);
    place_default();
    tip_box(cur == 0 ? "Carry on where you left off." : cur == 1 ? "The Malice is turning the strongest fighters into the Corrupted. Make a warrior of your own and stop it."
                                                              : "Return to the main menu of the game.", 58);
    prompts({{B_CROSS, "Choose"}, {B_CIRCLE, "Main menu"}});
    if (input && !sAsking && sIn.back) { sound(2); leave(1); }
}
// The six races in a row on the Lookout, the chosen one in front; left / right turns the row
void race_lineup(void) {
    float target = (float)sRaceCur, d = target - sRaceAnim;
    while (d > 3.0f) { d -= 6.0f; }
    while (d < -3.0f) { d += 6.0f; }
    sRaceAnim += d * 0.16f;
    if (sRaceAnim < 0) { sRaceAnim += 6.0f; }
    if (sRaceAnim >= 6.0f) { sRaceAnim -= 6.0f; }
    SDL_LockMutex(sMx);
    sSh.place[0] = 688.0f;
    sSh.place[1] = 590.0f;
    sSh.place[2] = 400.0f;
    for (int k = 0; k < 6; k++) {
        float o = (float)k - sRaceAnim;
        while (o > 3.0f) { o -= 6.0f; }
        while (o <= -3.0f) { o += 6.0f; }
        float ao = fabsf(o);
        bool front = k == sRaceCur;
        sSh.line[k] = front || ao > 2.6f ? -1 : kRaces[k].avatar;
        float across = ao <= 1.0f ? ao * 330.0f : 330.0f + (ao - 1.0f) * 250.0f; // (further apart in front, closer at the back)
        sSh.linePlace[k][0] = 688.0f + (o < 0 ? -across : across);
        sSh.linePlace[k][1] = ao <= 1.0f ? 590.0f - ao * 45.0f : 545.0f - (ao - 1.0f) * 35.0f;
        sSh.linePlace[k][2] = ao <= 1.0f ? 400.0f - ao * 130.0f : 270.0f - (ao - 1.0f) * 70.0f;
        sSh.linePlace[k][3] = 3.14159f - o * 0.35f;
    }
    SDL_UnlockMutex(sMx);
}
void scr_race(bool input) {
    heading("Choose Your Race", "New warrior");
    if (input) {
        if (sIn.left) { sRaceCur = (sRaceCur + 5) % 6; sound(0); }
        if (sIn.right) { sRaceCur = (sRaceCur + 1) % 6; sound(0); }
    }
    race_lineup();
    want_model(kRaces[sRaceCur].avatar, kRaces[sRaceCur].avatarCostume);
    const Race &r = kRaces[sRaceCur];
    // the name under the one in front, the arrows either side
    float size = S(46);
    std::string nm = upper(r.name);
    outlined(sBold, size, ImVec2(P(688, 0).x - text_w(sBold, size, nm.c_str()) * 0.5f, P(0, 112).y), kInk, IM_COL32(4, 12, 32, 255), nm, S(2));
    float pulse = S(6) * sinf(sFrames * 0.12f);
    ImVec2 l = P(470, 140), rr = P(906, 140);
    sDl->AddTriangleFilled(ImVec2(l.x - S(18) - pulse, l.y), ImVec2(l.x + S(4) - pulse, l.y - S(18)), ImVec2(l.x + S(4) - pulse, l.y + S(18)), kHi1);
    sDl->AddTriangleFilled(ImVec2(rr.x + S(18) + pulse, rr.y), ImVec2(rr.x - S(4) + pulse, rr.y - S(18)), ImVec2(rr.x - S(4) + pulse, rr.y + S(18)), kHi1);
    for (int k = 0; k < 6; k++) { // (the dots: which of the six)
        ImVec2 c = P(688 - 50 + k * 20, 182);
        sDl->AddCircleFilled(c, S(k == sRaceCur ? 6 : 4), k == sRaceCur ? kHi1 : IM_COL32(150, 190, 230, 180));
    }
    ImVec2 da = P(250, 608), db = P(1126, 712);
    panel(da, db);
    wrapped(sFont, S(17), ImVec2(da.x + S(22), da.y + S(18)), kInk, r.desc, (db.x - da.x) * 0.55f);
    for (int t = 0; t < 4; t++) {
        float y = da.y + S(16) + t * S(20);
        float tx = da.x + (db.x - da.x) * 0.60f;
        text(sBold, S(13), ImVec2(tx, y), kCyan, kTraitNames[t]);
        for (int k = 0; k < 5; k++) {
            ImVec2 ka(tx + S(100) + k * S(40), y + S(3)), kb(ka.x + S(34), ka.y + S(10));
            slant(ka, kb, k < r.traits[t] ? kHi1 : IM_COL32(40, 60, 90, 200), k < r.traits[t] ? kHi2 : IM_COL32(40, 60, 90, 200), S(4));
        }
    }
    prompts({{B_LR, "Turn"}, {B_CROSS, "Choose"}, {B_CIRCLE, "Back"}});
    if (input && sIn.ok) { sound(1); lines_off(); go(S_NAME); }
    else if (input && sIn.back) { sound(2); lines_off(); back(); }
}
void scr_name(bool input) {
    heading("Name Your Warrior", kRaces[sRaceCur].name);
    ImVec2 a = P(40, 150), b = P(660, 560);
    panel(a, b);
    ImVec2 na(a.x + S(20), a.y + S(24)), nb(b.x - S(20), a.y + S(84));
    sDl->AddRectFilled(na, nb, IM_COL32(0, 0, 0, 140));
    sDl->AddLine(ImVec2(na.x, nb.y), ImVec2(nb.x, nb.y), kHi1, S(2));
    std::string shown = sName + ((sFrames / 30) % 2 ? "_" : " ");
    text(sBold, S(32), ImVec2(na.x + S(16), na.y + S(12)), kInk, shown);
    if (input) { // typed letters (the keyboard)
        ImGuiIO &io = ImGui::GetIO();
        for (int i = 0; i < io.InputQueueCharacters.Size; i++) {
            ImWchar c = io.InputQueueCharacters[i];
            if (c >= 32 && c < 127 && sName.size() < 16 && c != '"' && c != '\\' && c != '/' && c != ':') { sName += (char)c; }
        }
    }
    const int cols = 10, count = 36 + 3; // the pad's keys: 36 letters and digits, then Space, Delete, Done
    if (input) {
        if (sIn.left && sKeyCur % cols > 0) { sKeyCur--; sound(0); }
        if (sIn.right && sKeyCur % cols < cols - 1 && sKeyCur + 1 < count) { sKeyCur++; sound(0); }
        if (sIn.up && sKeyCur >= cols) { sKeyCur -= cols; sound(0); }
        if (sIn.down && sKeyCur + cols < count) { sKeyCur += cols; sound(0); }
    }
    float kw = (nb.x - na.x) / cols, kh = S(52);
    for (int i = 0; i < count; i++) {
        ImVec2 ka(na.x + (i % cols) * kw + S(3), nb.y + S(20) + (i / cols) * kh), kb(ka.x + kw - S(6), ka.y + kh - S(8));
        bool on = i == sKeyCur;
        if (on) { slant(ka, kb, kHi1, kHi2, S(4)); } else { sDl->AddRectFilled(ka, kb, IM_COL32(120, 180, 255, 22)); }
        std::string k = i < 36 ? std::string(1, kKeys[i]) : i == 36 ? "SPC" : i == 37 ? "DEL" : "DONE";
        float size = S(i < 36 ? 22 : 15);
        text(sBold, size, ImVec2((ka.x + kb.x) * 0.5f - text_w(sBold, size, k.c_str()) * 0.5f, (ka.y + kb.y) * 0.5f - size * 0.55f), kInk, k);
    }
    want_model(kRaces[sRaceCur].avatar, kRaces[sRaceCur].avatarCostume);
    place_default();
    tip_box("Type a name (the keyboard works too). 16 letters at most.", 58);
    prompts({{B_CROSS, "Type"}, {B_SQUARE, "Delete"}, {B_START, "Done"}, {B_CIRCLE, "Back"}});
    auto done = [] {
        std::string n = sName;
        while (!n.empty() && n.back() == ' ') { n.pop_back(); }
        while (!n.empty() && n[0] == ' ') { n.erase(0, 1); }
        if (n.empty()) { notice("Give your warrior a name first."); return; }
        sSave = Save();
        sSave.name = n;
        sSave.race = sRaceCur;
        sSave.exists = true;
        sSave.location = "lookout";
        save_write();
        reset_to(S_HUB);
        sArrive = true;
        sArriveFrom = -1;
        if (getenv("BT3_PATROL_NOCREATOR") != NULL) { welcome(); return; } // (testing: the stand-in)
        sCreatorNew = true;
        open_creator(false);
    };
    if (input && sIn.ok) {
        if (sKeyCur < 36) { if (sName.size() < 16) { sName += (char)(sName.empty() || sName.back() == ' ' ? kKeys[sKeyCur] : SDL_tolower(kKeys[sKeyCur])); sound(0); } }
        else if (sKeyCur == 36) { if (!sName.empty() && sName.size() < 16) { sName += ' '; sound(0); } }
        else if (sKeyCur == 37) { if (!sName.empty()) { sName.pop_back(); sound(2); } }
        else { sound(1); done(); }
    } else if (input && sIn.sq) {
        if (!sName.empty()) { sName.pop_back(); sound(2); }
    } else if (input && sIn.start) {
        sound(1);
        done();
    } else if (input && sIn.back) {
        sound(2);
        back();
    } else if (input && ImGui::IsKeyPressed(ImGuiKey_Backspace, true) && !sName.empty()) {
        sName.pop_back();
    }
}
void stat_line(ImVec2 at, const char *label, const std::string &value) {
    text(sFont, S(17), at, kDim, label);
    text(sBold, S(17), ImVec2(at.x + S(220), at.y), kInk, value);
}
void player_card(ImVec2 a, ImVec2 b) {
    panel(a, b);
    face_at(fighter_chara(), ImVec2(a.x + S(16), a.y + S(16)), ImVec2(a.x + S(96), a.y + S(96)), S(4));
    text(sBold, S(24), ImVec2(a.x + S(112), a.y + S(18)), kInk, sSave.name, b.x - a.x - S(130));
    text(sFont, S(16), ImVec2(a.x + S(112), a.y + S(50)), kDim, std::string(kRaces[sSave.race].name) + "   Lv " + std::to_string(sSave.level) + "   " + std::to_string(sSave.zeni) + " Z");
    float w = b.x - a.x - S(130), f = sSave.level >= kLevelMax ? 1.0f : (float)sSave.xp / (float)xp_to_next(sSave.level);
    bar(ImVec2(a.x + S(112), a.y + S(78)), w, S(10), f, kCyan);
}
void open_who(const Who &w) {
    int c = w.chara;
    if (w.role == "story") {
        if (sSave.chapter >= (int)gChapters.size()) { service_line("story_idle", 58, "The Patrol's work never ends.", [] { go(S_STORY); }); }
        else { go(S_STORY); }
    } else if (w.role == "wardrobe") { service_line("wardrobe", c, "Want a new look?", [] { go(S_WARDROBE); }); }
    else if (w.role == "trainer") {
        if (sSave.masters.empty()) { notice("No masters yet. Free a Corrupted fighter first."); }
        else { service_line("trainer", 59, "Your masters are waiting.", [] { go(S_MASTERS); }); }
    } else if (w.role == "shop") {
        service_line("shop", c, "Welcome to the shop!", [] { sAfterShop = true; leave(4); });
    } else if (w.role == "board") { service_line("board", c, "Bounties, fresh today!", [] { go(S_BOARD); }); }
    else if (w.role == "quests") {
        static int giver;
        giver = c;
        sLists[S_QUESTS] = List();
        sLists[S_COUNT] = List();
        sLists[S_COUNT].cur = giver; // (whose quests the list shows)
        go(S_QUESTS);
    } else {
        sLists[S_COUNT + 1].cur = c; // (the master)
        sLists[S_MASTER] = List();
        go(S_MASTER);
    }
}
// The people of the place the player is at, without walking to them
void scr_people(bool input) {
    const Loc &l = loc_of(sSave.location);
    heading(l.name, "People here");
    ImVec2 a = P(40, 150), b = P(600, 640);
    panel(a, b);
    static std::vector<Row> rows;
    static std::vector<Who> who;
    rows.clear();
    who = people_at(l);
    for (size_t i = 0; i < who.size(); i++) {
        Row r;
        r.label = StoryKit_ShortName(who[i].chara);
        r.sub = role_name(who[i]);
        r.face = who[i].chara;
        if (who[i].role == "quests") {
            int open = 0;
            for (const Mission &m : gQuests) { if (m.giver == who[i].chara && !quest_done(m.id) && mission_lock(m).empty()) { open++; } }
            if (open > 0) { r.value = std::to_string(open) + " NEW"; r.valueColor = kHi1; }
        } else if (who[i].role == "master") {
            r.value = std::to_string(master_wins(who[i].chara)) + " / " + std::to_string(kLessons);
        }
        size_t k = i;
        r.ok = [k] { open_who(who[k]); };
        rows.push_back(r);
    }
    int cur = draw_rows(rows, sLists[S_PEOPLE], a, b, input);
    want_model(cur >= 0 ? who[(size_t)cur].chara : -1, 0);
    place_default();
    tip_box(l.desc, cur >= 0 ? who[(size_t)cur].chara : 58);
    prompts({{B_CROSS, "Talk"}, {B_CIRCLE, "Back"}});
    if (input && sIn.back) { sound(2); back(); }
}
// Back to the place: its battle is made again (a moment's loading), shown as the flight on the map when flying there
int sHubFrames;
void scr_hub(bool input) {
    const Loc &l = loc_of(sSave.location);
    sPlaceName = l.name;
    if (sArrive && sArriveFrom >= 0) {
        heading("Flying to " + l.name, "World map");
        sMapCur = loc_index(sSave.location);
        map_draw(P(140, 150), P(1236, 560), std::min(1.0f, sHubFrames / 50.0f), sArriveFrom);
    } else {
        heading(l.name, "Corrupt Patrol");
        ImVec2 a = P(388, 330), b = P(988, 420);
        panel(a, b);
        std::string t = sArrive ? "Flying in..." : "Heading back...";
        text(sBold, S(24), ImVec2((a.x + b.x) * 0.5f - text_w(sBold, S(24), t.c_str()) * 0.5f, a.y + S(30)), kInk, t);
    }
    sHubFrames++;
    want_model(-1, 0);
    place_default();
    if (!sHubQueued && !sAsking && sTalk.empty() && !sAwaitCreator && sHubFrames > (sArriveFrom >= 0 && sArrive ? 50 : 0)) {
        sHubQueued = true;
        sArriveFrom = -1;
        if (getenv("BT3_PATROL_NOHUB") != NULL) { go(S_PEOPLE); sHubQueued = false; return; } // (testing: the list instead)
        play_hub();
    }
    (void)input;
}
// A list of missions (the story's chapters, or one person's quests)
void mission_rows(std::vector<Row> &rows, bool chapter, int giver) {
    const std::vector<Mission> &all = chapter ? gChapters : gQuests;
    for (size_t i = 0; i < all.size(); i++) {
        const Mission &m = all[i];
        if (!chapter && m.giver != giver) { continue; }
        bool done = chapter ? sSave.chapter > (int)i : quest_done(m.id);
        std::string lock = done ? "" : mission_lock(m);
        if (chapter && !done && !lock.empty() && (int)i > sSave.chapter) {
            Row r;
            r.label = "Chapter " + std::to_string(i + 1) + ": ???";
            r.locked = true;
            r.value = "LOCKED";
            rows.push_back(r);
            continue;
        }
        Row r;
        r.label = chapter ? "Chapter " + std::to_string(i + 1) + ": " + m.title : m.title;
        r.sub = !lock.empty() ? lock : (chapter ? "At " + loc_of(m.location).name + "   Recommended Lv " + std::to_string(m.level) : m.summary);
        r.face = m.foes[0];
        r.locked = !lock.empty();
        r.value = done ? "CLEARED" : r.locked ? "LOCKED" : "NEW";
        r.valueColor = done ? kGreen : r.locked ? kLock : kHi1;
        r.model = m.foes[0];
        r.tip = m.summary;
        int mi = (int)i;
        bool ch = chapter;
        r.ok = [mi, ch] {
            const Mission &m = mission_of(ch, mi);
            talk_lines(m.brief, [mi, ch] {
                const Mission &m = mission_of(ch, mi);
                std::string who;
                for (size_t k = 0; k < m.foes.size(); k++) { who += (k ? (k + 1 == m.foes.size() ? " and " : ", ") : "") + StoryKit_ShortName(m.foes[k]); }
                ask((ch ? "Begin Chapter " + std::to_string(mi + 1) : "Accept \"" + m.title + "\"") + "? (" + who + (m.corrupt ? ", Corrupted" : "") + ")", [mi, ch] { play_mission(ch, mi); });
            });
        };
        rows.push_back(r);
    }
}
void scr_story(bool input) {
    heading("Main Story", "Supreme Kai");
    ImVec2 a = P(40, 150), b = P(660, 640);
    panel(a, b);
    static std::vector<Row> rows;
    rows.clear();
    mission_rows(rows, true, 58);
    int cur = draw_rows(rows, sLists[S_STORY], a, b, input && !sAsking);
    want_model(cur >= 0 && rows[(size_t)cur].model >= 0 ? rows[(size_t)cur].model : 58, 0);
    place_default();
    tip_box(cur >= 0 && !rows[(size_t)cur].tip.empty() ? rows[(size_t)cur].tip : "The Malice is spreading. Every Corrupted fighter we free is one more on our side.", 58);
    prompts({{B_CROSS, "Start"}, {B_CIRCLE, "Back"}});
    if (input && !sAsking && sIn.back) { sound(2); back(); }
}
void scr_quests(bool input) {
    int giver = sLists[S_COUNT].cur;
    heading(StoryKit_ShortName(giver) + "'s Quests", loc_of(sSave.location).name);
    ImVec2 a = P(40, 150), b = P(660, 640);
    panel(a, b);
    static std::vector<Row> rows;
    rows.clear();
    mission_rows(rows, false, giver);
    if (rows.empty()) { Row r; r.label = "Nothing right now"; r.locked = true; rows.push_back(r); }
    int cur = draw_rows(rows, sLists[S_QUESTS], a, b, input && !sAsking);
    want_model(cur >= 0 && rows[(size_t)cur].model >= 0 && !rows[(size_t)cur].locked ? rows[(size_t)cur].model : giver, 0);
    place_default();
    tip_box(cur >= 0 && !rows[(size_t)cur].tip.empty() && !rows[(size_t)cur].locked ? rows[(size_t)cur].tip : master_info(giver).greet.empty() ? "Come back later." : rows[(size_t)cur].sub, giver);
    prompts({{B_CROSS, "Accept"}, {B_L1R1, "Page"}, {B_CIRCLE, "Back"}});
    if (input && !sAsking && sIn.back) { sound(2); back(); }
}
void scr_masters(bool input) {
    heading("Training Grounds", std::to_string(sSave.masters.size()) + " of 161 masters");
    ImVec2 a = P(40, 150), b = P(660, 640);
    panel(a, b);
    static std::vector<Row> rows;
    rows.clear();
    for (auto it = sSave.masters.rbegin(); it != sSave.masters.rend(); ++it) {
        int c = *it;
        Row r;
        int w = master_wins(c);
        r.label = StoryKit_CharaName(c);
        r.sub = w >= kLessons ? "Training complete" : "Next: " + move_name(c, w) + "   (lives at " + home_of(c).name + ")";
        r.face = c;
        r.value = std::to_string(w) + " / " + std::to_string(kLessons);
        r.valueColor = w >= kLessons ? kGreen : 0;
        r.model = c;
        r.ok = [c] { sLists[S_COUNT + 1].cur = c; sLists[S_MASTER] = List(); go(S_MASTER); };
        rows.push_back(r);
    }
    int cur = draw_rows(rows, sLists[S_MASTERS], a, b, input);
    int c = cur >= 0 ? rows[(size_t)cur].model : 59;
    want_model(c, 0);
    place_default();
    tip_box(master_info(c).greet.empty() ? "Every master has five lessons, one technique each." : fill(master_info(c).greet), c);
    prompts({{B_CROSS, "Train"}, {B_L1R1, "Page"}, {B_CIRCLE, "Back"}});
    if (input && sIn.back) { sound(2); back(); }
}
void scr_master(bool input) {
    int c = sLists[S_COUNT + 1].cur;
    heading(StoryKit_CharaName(c), "Training");
    ImVec2 a = P(40, 150), b = P(660, 640);
    panel(a, b);
    static std::vector<Row> rows;
    rows.clear();
    if (!master_unlocked(c)) {
        Row r;
        r.label = "Not your master yet";
        r.sub = "Free them, or win them over in a quest";
        r.locked = true;
        rows.push_back(r);
    } else {
        Row s;
        s.sec = true;
        s.label = "Lessons";
        rows.push_back(s);
        int wins = master_wins(c);
        for (int k = 0; k < kLessons; k++) {
            Row r;
            r.label = move_name(c, k);
            r.sub = std::string(kSlotKinds[k]) + (k == kLessons - 1 ? "  +  fighting style and outfit" : "");
            r.value = k < wins ? "LEARNT" : k == wins ? "TRAIN" : "LOCKED";
            r.valueColor = k < wins ? kGreen : k == wins ? kHi1 : kLock;
            r.locked = k > wins;
            int kk = k;
            r.ok = [c, kk] {
                ask("Train with " + StoryKit_ShortName(c) + "? Lesson " + std::to_string(kk + 1) + (kk < master_wins(c) ? " (again, for experience)" : ""), [c, kk] { play_training(c, kk); });
            };
            rows.push_back(r);
        }
    }
    draw_rows(rows, sLists[S_MASTER], a, b, input && !sAsking);
    want_model(c, 0);
    place_default();
    const MasterInfo &mi = master_info(c);
    tip_box(fill(master_wins(c) >= kLessons ? (mi.done.empty() ? "You've learned all I can teach." : mi.done) : (mi.greet.empty() ? "Ready when you are." : mi.greet)), c);
    prompts({{B_CROSS, "Train"}, {B_CIRCLE, "Back"}});
    if (input && !sAsking && sIn.back) { sound(2); back(); }
}
void scr_board(bool input) {
    heading("Bounty Board", "Krillin");
    ImVec2 a = P(40, 150), b = P(660, 640);
    panel(a, b);
    static std::vector<Row> rows;
    rows.clear();
    int t = bounty_tier();
    static const char *const kTierNames[] = {"Saiyan scouts and soldiers", "Frieza's elite", "Rogue warriors", "Galactic threats", "Legendary monsters"};
    Row r;
    r.label = "Take a bounty";
    r.sub = std::string("Targets: ") + kTierNames[t];
    r.value = std::to_string(600 + t * 700) + " Z";
    r.ok = [] { ask("Take the next bounty? A random Corrupted target, at your strength.", [] { play_bounty(); }); };
    rows.push_back(r);
    Row s;
    s.sec = true;
    s.label = "Record";
    rows.push_back(s);
    Row d;
    d.label = "Bounties cleared";
    d.value = std::to_string(sSave.bounties);
    rows.push_back(d);
    draw_rows(rows, sLists[S_BOARD], a, b, input && !sAsking);
    want_model(25, 0);
    place_default();
    tip_box("They get tougher as you level up. Good money for the shop, too.", 25);
    prompts({{B_CROSS, "Choose"}, {B_CIRCLE, "Back"}});
    if (input && !sAsking && sIn.back) { sound(2); back(); }
}
void scr_wardrobe(bool input) {
    heading("Wardrobe", "Videl");
    ImVec2 a = P(40, 150), b = P(660, 640);
    panel(a, b);
    static std::vector<Row> rows;
    rows.clear();
    Row ch;
    ch.label = sSave.fighter.empty() ? "Make your warrior's look" : "Change your look";
    ch.sub = std::string("Heads, hair, outfits: ") + kRaces[sSave.race].name + " fighters' and your masters'";
    ch.ok = [] { open_creator(false); };
    rows.push_back(ch);
    Row s;
    s.sec = true;
    s.label = "Unlocked outfits";
    rows.push_back(s);
    Row r;
    r.label = std::string(kRaces[sSave.race].name) + " outfits";
    r.sub = sSave.race == 0 || sSave.race == 3 ? "Saiyan and human clothes" : "Your race's clothes";
    r.value = "OWNED";
    rows.push_back(r);
    for (int m : sSave.masters) {
        if (master_wins(m) < kLessons) { continue; }
        Row o;
        o.label = StoryKit_ShortName(m) + "'s outfit";
        o.sub = "From completing their training";
        o.face = m;
        o.value = "OWNED";
        rows.push_back(o);
    }
    draw_rows(rows, sLists[S_WARDROBE], a, b, input);
    want_model(fighter_chara(), 0);
    place_default();
    tip_box(sSave.fighter.empty() ? "You still fight as your race's stand-in. Make your own look here." : "Your race's outfits, and those of every master whose training you completed.", 56);
    prompts({{B_CROSS, "Choose"}, {B_CIRCLE, "Back"}});
    if (input && sIn.back) { sound(2); back(); }
}
void scr_menu(bool input) {
    heading("Patrol Menu", loc_of(sSave.location).name);
    ImVec2 a = P(40, 150), b = P(560, 560);
    panel(a, b);
    static std::vector<Row> rows;
    rows.clear();
    auto add = [&](const char *label, const std::string &sub, std::function<void()> ok) { Row r; r.label = label; r.sub = sub; r.ok = ok; rows.push_back(r); };
    add("World Map", "Fly somewhere else", [] { sMapCur = loc_index(sSave.location); go(S_MAP); });
    add("People Here", "Talk to anyone here without walking", [] { go(S_PEOPLE); });
    add("Status", "Level, health, attack, defense", [] { go(S_STATS); });
    add("Items", "Wear the Z items you own", [] { go(S_GEAR); });
    add("Techniques", "What your masters have taught you", [] { go(S_SKILLS); });
    add("Quest Log", "The story so far, the quests, the masters", [] { go(S_LOG); });
    add("Save", "Write your progress", [] { if (save_write()) { notice("Saved."); } else { notice("Could not save."); } });
    add("Return to Title", "Saves first", [] { save_write(); reset_to(S_TITLE); });
    draw_rows(rows, sLists[S_MENU], a, b, input);
    player_card(P(40, 576), P(560, 664 - 4));
    want_model(fighter_chara(), 0);
    place_default();
    prompts({{B_CROSS, "Choose"}, {B_CIRCLE, "Back"}});
    if (input && (sIn.back || sIn.start)) { sound(2); back(); }
}
void scr_map(bool input) {
    heading("World Map", "Choose where to fly");
    if (sMapCur < 0) { sMapCur = loc_index(sSave.location); }
    ImVec2 a = P(140, 150), b = P(1236, 560);
    if (input) {
        if (sIn.left) { map_move(-1, 0, a, b); }
        if (sIn.right) { map_move(1, 0, a, b); }
        if (sIn.up) { map_move(0, -1, a, b); }
        if (sIn.down) { map_move(0, 1, a, b); }
        if (sIn.l1) { sMapCur = (sMapCur + (int)gLocs.size() - 1) % (int)gLocs.size(); sound(0); }
        if (sIn.r1) { sMapCur = (sMapCur + 1) % (int)gLocs.size(); sound(0); }
    }
    map_draw(a, b, 0.0f, -1);
    want_model(fighter_chara(), 0);
    model_place(1500.0f, kModelFeet, kModelH); // (off the picture: the map has it all)
    prompts({{B_CROSS, "Fly there"}, {B_L1R1, "Next place"}, {B_CIRCLE, "Back"}});
    if (input && sIn.ok) {
        if (sMapCur == loc_index(sSave.location)) { notice("You're already here."); sound(2); }
        else {
            sound(1);
            sArriveFrom = loc_index(sSave.location);
            sSave.location = gLocs[(size_t)sMapCur].id;
            sArrive = true;
            save_write();
            reset_to(S_HUB);
            sHubQueued = false;
            sHubFrames = 0;
        }
    } else if (input && sIn.back) { sound(2); back(); }
}
void scr_stats(bool input) {
    heading("Status", sSave.name);
    ImVec2 a = P(40, 150), b = P(660, 640);
    panel(a, b);
    std::vector<int> items = player_items();
    int st[4] = {0, 0, 0, 0};
    for (int it : items) {
        if (it >= 1 && it <= 12) { st[(it - 1) / 3] += (it - 1) % 3 + 1; } // (Attack / Defense / Ki Power / Super Up 1..3)
        if (it == 13) { st[0] += 2; st[1] -= 1; }
        if (it == 14) { st[1] += 2; st[0] -= 1; }
    }
    float y = a.y + S(24);
    ImVec2 at(a.x + S(24), y);
    auto line = [&](const char *l, const std::string &v) { stat_line(ImVec2(at.x, y), l, v); y += S(33); };
    line("Race", kRaces[sSave.race].name);
    line("Level", std::to_string(sSave.level) + (sSave.level >= kLevelMax ? " (max)" : "  (" + std::to_string(sSave.xp) + " / " + std::to_string(xp_to_next(sSave.level)) + " EXP)"));
    line("Health bars", std::to_string(player_bars()));
    line("Blast stocks", "3");
    line("Attack", (st[0] >= 0 ? "+" : "") + std::to_string(st[0]));
    line("Defense", (st[1] >= 0 ? "+" : "") + std::to_string(st[1]));
    line("Ki Power", "+" + std::to_string(st[2]));
    line("Super", "+" + std::to_string(st[3]));
    line("Item slots", std::to_string(slots_used()) + " of " + std::to_string(slot_budget()) + " used");
    line("Zeni", std::to_string(sSave.zeni));
    line("Battles", std::to_string(sSave.won) + " won, " + std::to_string(sSave.lost) + " lost");
    line("Story", std::to_string(sSave.chapter) + " of " + std::to_string(gChapters.size()) + " chapters");
    line("Quests", std::to_string(sSave.done.size()) + " of " + std::to_string(gQuests.size()));
    line("Masters", std::to_string(sSave.masters.size()) + " of 161");
    want_model(fighter_chara(), 0);
    place_default();
    prompts({{B_CIRCLE, "Back"}});
    if (input && sIn.back) { sound(2); back(); }
}
void scr_gear(bool input) {
    heading("Z Items", std::to_string(sSave.equipped.size()) + " worn   " + std::to_string(slots_used()) + " / " + std::to_string(slot_budget()) + " slots");
    ImVec2 a = P(40, 150), b = P(660, 640);
    panel(a, b);
    static std::vector<Row> rows;
    rows.clear();
    std::vector<int> mine = sSave.owned;
    std::sort(mine.begin(), mine.end());
    for (int it : mine) {
        Row r;
        r.label = item_name(it);
        r.sub = sItems[it].known ? sItems[it].desc : "";
        r.value = (equipped(it) ? "WORN   " : "") + std::to_string(sItems[it].known ? sItems[it].slots : 1) + " slot" + ((sItems[it].known ? sItems[it].slots : 1) == 1 ? "" : "s");
        r.valueColor = equipped(it) ? kGreen : 0;
        r.tip = r.sub;
        r.ok = [it] {
            auto at = std::find(sSave.equipped.begin(), sSave.equipped.end(), it);
            if (at != sSave.equipped.end()) { sSave.equipped.erase(at); }
            else if ((int)sSave.equipped.size() >= kEquipMax) { notice("You can wear " + std::to_string(kEquipMax) + " at most. Take one off first."); return; }
            else if (slots_used() + (sItems[it].known ? sItems[it].slots : 1) > slot_budget()) { notice("Not enough item slots. They grow with your level."); return; }
            else { sSave.equipped.push_back(it); }
            save_write();
        };
        rows.push_back(r);
    }
    if (rows.empty()) {
        Row r;
        r.label = "No Z items yet";
        r.sub = "Buy them at Mr Satan's shop, World Tournament Stage";
        r.locked = true;
        rows.push_back(r);
    }
    int cur = draw_rows(rows, sLists[S_GEAR], a, b, input);
    want_model(fighter_chara(), 0);
    place_default();
    tip_box(cur >= 0 && !rows[(size_t)cur].tip.empty() ? rows[(size_t)cur].tip : "Cross puts an item on or takes it off. Each takes item slots.", 55);
    prompts({{B_CROSS, "Wear / remove"}, {B_L1R1, "Page"}, {B_CIRCLE, "Back"}});
    if (input && sIn.back) { sound(2); back(); }
}
void scr_skills(bool input) {
    heading("Techniques", "Learnt from your masters");
    ImVec2 a = P(40, 150), b = P(660, 640);
    panel(a, b);
    static std::vector<Row> rows;
    rows.clear();
    Row s;
    s.sec = true;
    s.label = "Starting techniques";
    rows.push_back(s);
    const struct { int chara, slot; } kStart[3] = {{73, 2}, {17, 3}, {31, 4}};
    for (const auto &st : kStart) {
        Row r;
        r.label = move_name(st.chara, st.slot);
        r.sub = kSlotKinds[st.slot];
        r.value = "KNOWN";
        rows.push_back(r);
    }
    for (auto mt = sSave.masters.rbegin(); mt != sSave.masters.rend(); ++mt) {
        int m = *mt;
        int w = master_wins(m);
        if (w == 0) { continue; }
        Row h;
        h.sec = true;
        h.label = "From " + StoryKit_ShortName(m);
        rows.push_back(h);
        for (int k = 0; k < w; k++) {
            Row r;
            r.label = move_name(m, k);
            r.sub = kSlotKinds[k];
            r.face = m;
            const J &cur = sSave.loadout[kSlotKeys[k]];
            bool set = (int)cur["chara"].number(-1) == m && (int)cur["slot"].number(-1) == k;
            r.value = set ? "SET" : "";
            r.valueColor = kGreen;
            int kk = k;
            r.ok = [m, kk] {
                J ref = J::obj();
                ref.set("chara", J::num(m));
                ref.set("slot", J::num(kk));
                if (!sSave.loadout.isObj()) { sSave.loadout = J::obj(); }
                sSave.loadout.set(kSlotKeys[kk], ref);
                save_write();
                notice(move_name(m, kk) + " set as your " + kSlotKinds[kk] + ". Triangle to apply.");
            };
            rows.push_back(r);
        }
        if (w >= kLessons) {
            Row r;
            r.label = StoryKit_ShortName(m) + "'s fighting style";
            r.sub = "Their melee, ki blasts and stance";
            r.face = m;
            r.value = (int)sSave.loadout["style"]["chara"].number(-1) == m ? "SET" : "";
            r.valueColor = kGreen;
            r.ok = [m] {
                J ref = J::obj();
                ref.set("chara", J::num(m));
                if (!sSave.loadout.isObj()) { sSave.loadout = J::obj(); }
                sSave.loadout.set("style", ref);
                save_write();
                notice(StoryKit_ShortName(m) + "'s style set. Triangle to apply it.");
            };
            rows.push_back(r);
        }
    }
    draw_rows(rows, sLists[S_SKILLS], a, b, input);
    want_model(fighter_chara(), 0);
    place_default();
    tip_box("Cross sets a technique in its slot. Triangle trains them into your warrior (a few seconds).", fighter_chara());
    if (input && sIn.tri) {
        if (sSave.fighterFile.empty()) { notice("Make your warrior's look first (Videl's Wardrobe)."); }
        else { sound(1); open_creator(true); }
    }
    prompts({{B_CROSS, "Set"}, {B_TRIANGLE, "Apply"}, {B_L1R1, "Page"}, {B_CIRCLE, "Back"}});
    if (input && sIn.back) { sound(2); back(); }
}
void scr_log(bool input) {
    heading("Quest Log", "The story so far");
    ImVec2 a = P(40, 150), b = P(660, 640);
    panel(a, b);
    static std::vector<Row> rows;
    rows.clear();
    Row s;
    s.sec = true;
    s.label = "Main story";
    rows.push_back(s);
    for (size_t m = 0; m < gChapters.size() && (int)m <= sSave.chapter; m++) {
        const Mission &ms = gChapters[m];
        Row r;
        bool done = sSave.chapter > (int)m;
        r.label = "Chapter " + std::to_string(m + 1) + ": " + ms.title;
        r.sub = done ? "Cleared" : "Next: talk to the Supreme Kai at Kami's Lookout";
        r.value = done ? "CLEARED" : "NEXT";
        r.valueColor = done ? kGreen : kHi1;
        r.face = ms.foes[0];
        r.tip = ms.summary;
        rows.push_back(r);
    }
    Row q;
    q.sec = true;
    q.label = "Quests open now";
    rows.push_back(q);
    int open = 0;
    for (const Mission &m : gQuests) {
        if (quest_done(m.id) || !mission_lock(m).empty()) { continue; }
        Row r;
        r.label = m.title;
        r.sub = StoryKit_ShortName(m.giver) + " at " + loc_of(m.location).name;
        r.face = m.giver;
        r.value = "NEW";
        r.valueColor = kHi1;
        r.tip = m.summary;
        rows.push_back(r);
        open++;
    }
    if (open == 0) { Row r; r.label = "None right now"; r.sub = "Push the story on, or level up"; r.locked = true; rows.push_back(r); }
    Row d;
    d.sec = true;
    d.label = "Quests cleared: " + std::to_string(sSave.done.size()) + " of " + std::to_string(gQuests.size());
    rows.push_back(d);
    int cur = draw_rows(rows, sLists[S_LOG], a, b, input);
    want_model(cur >= 0 && rows[(size_t)cur].face >= 0 ? rows[(size_t)cur].face : 58, 0);
    place_default();
    tip_box(cur >= 0 && !rows[(size_t)cur].tip.empty() ? rows[(size_t)cur].tip : "Quests open up as the story goes on and you grow stronger.", 58);
    prompts({{B_L1R1, "Page"}, {B_CIRCLE, "Back"}});
    if (input && sIn.back) { sound(2); back(); }
}
void scr_talk(bool input) {
    if (sTalkAt >= sTalk.size()) { sTalk.clear(); back(); if (sTalkThen) { auto f = sTalkThen; sTalkThen = nullptr; f(); } return; }
    const Say &s = sTalk[sTalkAt];
    status_bar();
    ImVec2 a = P(60, 560), b = P(1000, 690);
    chamfer(a, b, IM_COL32(6, 16, 40, 235), S(18));
    chamfer_edge(a, b, IM_COL32(96, 200, 255, 170), S(18), S(1.5f));
    // the name plate
    float nw = text_w(sBold, S(22), s.name.c_str()) + S(60);
    slant(ImVec2(a.x + S(20), a.y - S(22)), ImVec2(a.x + S(20) + nw, a.y + S(8)), kHi1, kHi2, S(10));
    outlined(sBold, S(22), ImVec2(a.x + S(44), a.y - S(20)), kInk, IM_COL32(110, 40, 0, 255), s.name, S(1.0f));
    face_at(s.chara, ImVec2(a.x + S(22), a.y + S(22)), ImVec2(a.x + S(114), a.y + S(114)), S(4));
    // the text, typed out
    static size_t shownFor = (size_t)-1;
    static int typed;
    if (shownFor != sTalkAt + (size_t)(uintptr_t)&s) { shownFor = sTalkAt + (size_t)(uintptr_t)&s; typed = 0; }
    typed += 2;
    std::string t = s.text.substr(0, std::min((size_t)typed, s.text.size()));
    wrapped(sFont, S(20), ImVec2(a.x + S(134), a.y + S(24)), kInk, t, b.x - a.x - S(160));
    if ((size_t)typed >= s.text.size() && (sFrames / 20) % 2) { sDl->AddTriangleFilled(ImVec2(b.x - S(36), b.y - S(26)), ImVec2(b.x - S(22), b.y - S(26)), ImVec2(b.x - S(29), b.y - S(16)), kHi1); }
    want_model(s.chara, 0);
    model_place(1120.0f, 700.0f, 520.0f);
    lines_off();
    prompts({{B_CROSS, sTalkAt + 1 < sTalk.size() ? "Next" : "OK"}});
    if (input && (sIn.ok || sIn.back)) {
        if ((size_t)typed < s.text.size()) { typed = (int)s.text.size(); }
        else { sound(1); sTalkAt++; }
    }
}
void scr_result(bool input) {
    heading(sResultTitle, sSave.name);
    ImVec2 a = P(40, 150), b = P(700, 560);
    panel(a, b);
    float y = a.y + S(24);
    for (const std::string &l : sResultLines) {
        bool big = l.compare(0, 8, "LEVEL UP") == 0 || l.compare(0, 10, "NEW MASTER") == 0 || l.compare(0, 9, "TECHNIQUE") == 0 || l.compare(0, 9, "THE STORY") == 0;
        wrapped(big ? sBold : sFont, S(19), ImVec2(a.x + S(24), y), big ? kHi1 : kInk, l, b.x - a.x - S(48));
        y += S(36);
        if (y > b.y - S(30)) { break; }
    }
    player_card(P(40, 576), P(700, 660));
    want_model(fighter_chara(), 0);
    place_default();
    prompts({{B_CROSS, "Continue"}});
    if (input && (sIn.ok || sIn.back)) {
        sound(1);
        back();
        if (!sAfterTalk.empty()) { std::vector<TalkLn> t = sAfterTalk; sAfterTalk.clear(); talk_lines(t, nullptr); }
    }
}

void ask_draw(bool input) {
    if (!sAsking) { return; }
    sDl->AddRectFilled(P(0, 0), P(1376, 768), IM_COL32(0, 0, 0, 130));
    ImVec2 a = P(358, 270), b = P(1018, 480);
    panel(a, b);
    wrapped(sBold, S(21), ImVec2(a.x + S(28), a.y + S(28)), kInk, sAsk, b.x - a.x - S(56));
    const char *opts[2] = {"Yes", "No"};
    for (int i = 0; i < 2; i++) {
        ImVec2 oa(a.x + S(80) + i * S(280), b.y - S(72)), ob(oa.x + S(220), oa.y + S(44));
        if (i == sAskCur) { slant(oa, ob, kHi1, kHi2, S(10)); } else { sDl->AddRectFilled(oa, ob, IM_COL32(120, 180, 255, 24)); }
        text(sBold, S(20), ImVec2((oa.x + ob.x) * 0.5f - text_w(sBold, S(20), opts[i]) * 0.5f, oa.y + S(11)), kInk, opts[i]);
    }
    if (!input) { return; }
    if (sIn.left || sIn.right) { sAskCur ^= 1; sound(0); }
    if (sIn.ok) {
        sAsking = false;
        sound(sAskCur == 0 ? 1 : 2);
        if (sAskCur == 0 && sAskYes) { auto f = sAskYes; sAskYes = nullptr; f(); }
    } else if (sIn.back) {
        sAsking = false;
        sound(2);
    }
}

Uint64 sLastTicks;
int sOpenGenSeen = -1;
volatile int sOpenGen;
// What is behind the screens: the stage of the place the player is at (the Lookout for a new warrior)
int backdrop_stage(void) { return sSave.exists ? loc_of(sSave.location).stage : 5; }
void on_open(void) {
    sSaveDir = save_dir();
    content_read();
    static bool itemsRead;
    if (!itemsRead) { itemsRead = true; items_cache_read(); }
    int gen;
    SDL_LockMutex(sMx);
    gen = sSh.resultGen;
    int result = sSh.result;
    SDL_UnlockMutex(sMx);
    sHubFrames = 0;
    if (sAwaitCreator) {
        sAwaitCreator = false;
        char name[128] = "", file[256] = "";
        int r = PortCreator_PatrolResult(name, sizeof(name));
        PortCreator_PatrolFile(file, sizeof(file));
        save_load();
        if (r == 1 && name[0]) {
            sSave.fighter = name;
            sSave.fighterFile = file;
            save_write();
            notice(std::string("Your warrior is ready: ") + name + ".");
        }
        reset_to(S_HUB);
        sArrive = true;
        sArriveFrom = -1;
        if (sCreatorNew) { sCreatorNew = false; welcome(); }
        return;
    }
    if (sAfterShop) {
        sAfterShop = false;
        reset_to(S_HUB);
        return;
    }
    if (gen != sResultSeen) {
        sResultSeen = gen;
        if (sFightKind != FIGHT_NONE) {
            on_result(result);
            sFightKind = FIGHT_NONE;
            return;
        }
    }
    save_load();
    sAsking = false;
    reset_to(S_TITLE);
    if (sSave.exists && getenv("BT3_PATROL_CONTINUE") != NULL) { reset_to(S_HUB); sArrive = true; sArriveFrom = -1; } // (testing: straight to the place)
}

} // namespace

// =============================================================================================== the game's half (src/menu/port_patrol.c)
extern "C" void PortPatrolUi_Open(void) {
    if (sMx == NULL) { sMx = SDL_CreateMutex(); }
    sSaveDir = save_dir();
    content_read();
    if (!sSave.exists) { save_load(); } // (the stage behind the screens is the place the save is at)
    SDL_LockMutex(sMx);
    sSh.leave = 0;
    sSh.sounds = 0;
    sSh.turn = 0;
    sSh.voice = -1;
    sSh.key = -1;
    sSh.stage = backdrop_stage();
    for (int k = 0; k < kLineMax; k++) { sSh.line[k] = -1; }
    sOpenGen++;
    sActive = 1;
    SDL_UnlockMutex(sMx);
}
extern "C" void PortPatrolUi_Close(void) {
    fprintf(stderr, "bt3: patrol: screens closed (%d)\n", sSh.leave);
    if (sMx == NULL) { return; }
    SDL_LockMutex(sMx);
    sActive = 0;
    SDL_UnlockMutex(sMx);
}
extern "C" int PortPatrolUi_Active(void) { return sActive; }
extern "C" int PortPatrolUi_Get(int what) {
    int v = -1;
    if (sMx == NULL) { return what == PG_BGM ? 6 : -1; }
    SDL_LockMutex(sMx);
    if (what >= PG_LINE && what < PG_LINE + kLineMax * 5) {
        int k = (what - PG_LINE) / 5, f = (what - PG_LINE) % 5;
        v = f == 0 ? sSh.line[k] : f == 4 ? (int)(sSh.linePlace[k][3] * 1000.0f) : (int)(sSh.linePlace[k][f - 1] * 10.0f);
        SDL_UnlockMutex(sMx);
        return v;
    }
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
    case PG_STAGE: v = sSh.stage; break;
    case PG_PLACE_X: v = (int)(sSh.place[0] * 10.0f); break;
    case PG_PLACE_FEET: v = (int)(sSh.place[1] * 10.0f); break;
    case PG_PLACE_H: v = (int)(sSh.place[2] * 10.0f); break;
    }
    SDL_UnlockMutex(sMx);
    return v;
}
// (the game's thread, as the screens are left for a battle) the battle made into its script; 0 if it could not be
extern "C" int PortPatrolUi_Prepare(void) {
    if (sDoc.empty() || !PortStoryUi_PrepareDoc(sDoc.c_str(), 0)) {
        leave(0);
        notice("That battle could not be made.");
        sFightKind = FIGHT_NONE;
        return 0;
    }
    bool corrupt = sFightKind == FIGHT_BOUNTY || (sFightKind == FIGHT_MISSION && mission_of(sFightA == 1, sFightB).corrupt);
    PortCorrupt_SetNext(corrupt ? 2 : 0); // (the foes glow violet)
    fprintf(stderr, "bt3: patrol: battle (kind %d, %d, %d)\n", sFightKind, sFightA, sFightB);
    return 1;
}
extern "C" void PortPatrolUi_Result(int result) {
    PortStoryUi_EndDoc();
    PortCorrupt_SetNext(0);
    if (sMx == NULL) { sMx = SDL_CreateMutex(); }
    SDL_LockMutex(sMx);
    sSh.result = result;
    sSh.resultGen++;
    SDL_UnlockMutex(sMx);
    fprintf(stderr, "bt3: patrol: the battle ended: %s\n", result == 1 ? "won" : result == 2 ? "lost" : "left");
}
// The game's shop with the mode's zeni and items (src/menu/port_patrol.c: PortPatrol_Shop, on the game's thread while
// the screens are closed). Its stock grows with the warrior's level (the game's own shop levels 0..5).
bool sShopOpen;
extern "C" int PortPatrolUi_ShopMoney(void) { sShopOpen = true; return sSave.zeni; }
extern "C" int PortPatrolUi_ShopLevel(void) { return std::min(5, (sSave.level - 1) / 8); }
extern "C" int PortPatrolUi_ShopOwns(int item) { return owns(item) ? 1 : 0; }
extern "C" void PortPatrolUi_ShopGot(int item) {
    if (item >= 0 && item < kItemCount && !owns(item)) {
        sSave.owned.push_back(item);
        fprintf(stderr, "bt3: patrol: bought %s\n", item_name(item).c_str());
    }
}
extern "C" void PortPatrolUi_ShopDone(int money) {
    sShopOpen = false;
    sSave.zeni = std::max(0, money);
    save_write();
}
extern "C" int PortA11y_ItemInfo(int item, char *name, int nn, char *desc, int nd, int *info); // a11y_game.c
// (src/menu/menu_v_d.c, when the shop has opened) the items' names and descriptions, while their text is loaded
extern "C" void PortPatrolUi_ShopOpened(void) {
    if (!sShopOpen) { return; }
    int got = 0;
    for (int i = 0; i < kItemCount; i++) {
        char name[256], desc[1024];
        int info[4] = {0, 1, 0, 0};
        if (!PortA11y_ItemInfo(i, name, sizeof(name), desc, sizeof(desc), info)) { continue; }
        ItemInfo &it = sItems[i];
        it.name = name;
        it.desc = desc;
        for (char &c : it.desc) { if (c == '\n') { c = ' '; } }
        it.type = info[0];
        it.slots = info[1];
        it.price = info[2];
        it.flags = info[3];
        it.known = !it.name.empty();
        got++;
    }
    if (got > 0) { items_cache_write(); }
    fprintf(stderr, "bt3: patrol: %d item names read\n", got);
}

// Every frame (ui.cpp) while the screens are open
bool Patrol_Frame(bool input) {
    if (!sActive) { return false; }
    frame_setup();
    if (sOpenGenSeen != sOpenGen) { sOpenGenSeen = sOpenGen; on_open(); }
    sFrames++;
    Uint64 now = SDL_GetTicks();
    if (sLastTicks != 0 && sScr != S_TITLE && sSave.exists) { sSave.playtime += (double)(now - sLastTicks) / 1000.0; }
    sLastTicks = now;
    sTyping = sScr == S_NAME && !sAsking;
    sPlaceName = sSave.exists ? loc_of(sSave.location).name : "";
    read_input(input);
    bool live = input && !sAsking;
    if (sScr != S_RACE) { lines_off(); }
    switch (sScr) {
    case S_TITLE: scr_title(live); break;
    case S_RACE: scr_race(live); break;
    case S_NAME: scr_name(live); break;
    case S_PEOPLE: scr_people(live); break;
    case S_HUB: scr_hub(live); break;
    case S_STORY: scr_story(live); break;
    case S_QUESTS: scr_quests(live); break;
    case S_MASTERS: scr_masters(live); break;
    case S_MASTER: scr_master(live); break;
    case S_BOARD: scr_board(live); break;
    case S_MENU: scr_menu(live); break;
    case S_STATS: scr_stats(live); break;
    case S_GEAR: scr_gear(live); break;
    case S_SKILLS: scr_skills(live); break;
    case S_LOG: scr_log(live); break;
    case S_TALK: scr_talk(live); break;
    case S_RESULT: scr_result(live); break;
    case S_WARDROBE: scr_wardrobe(live); break;
    case S_MAP: scr_map(live); break;
    case S_COUNT: break;
    }
    if (input && sIn.turn != 0.0f && sScr != S_RACE) {
        SDL_LockMutex(sMx);
        sSh.turn += (int)(sIn.turn * 40.0f);
        SDL_UnlockMutex(sMx);
    }
    ask_draw(input);
    notice_draw();
    return true;
}

// Over the picture while a place is walked about (ui.cpp): its name, who is there and how far, the prompt to talk, the
// world map (Triangle), the black of taking off and landing
extern "C" {
extern volatile int gPortHubMapOpen;
extern volatile int gPortHubMapKeys;
extern volatile int gPortHubTravel;
extern volatile int gPortHubFade;
extern volatile int gPortHubPhase;
extern volatile float gPortHubPlaces[];
}
bool Patrol_HubOverlayOn(void) { return gPortHubMode != 0 && sFightKind == FIGHT_HUB; }
void Patrol_HubOverlay(void) {
    if (!Patrol_HubOverlayOn()) { return; }
    frame_setup();
    sFrames++;
    const Loc &l = loc_of(sSave.location);
    static int landedAt = -1000, lastPhase = 0;
    int phase = gPortHubPhase;
    if (lastPhase == 1 && phase == 0) { landedAt = sFrames; }
    lastPhase = phase;
    if (phase != 3) {
        // the place's name, as Xenoverse shows it on arriving (bigger for a few seconds after landing)
        float since = (float)(sFrames - landedAt);
        if (since < 240.0f) {
            float al = since < 200.0f ? 1.0f : (240.0f - since) / 40.0f;
            int A = (int)(255 * al);
            float size = S(44);
            std::string nm = upper(l.name);
            ImVec2 c = P(688, 150);
            hgrad(ImVec2(c.x - S(420), c.y - S(8)), ImVec2(c.x, c.y + S(76)), IM_COL32(6, 16, 40, 0), IM_COL32(6, 16, 40, (int)(200 * al)));
            hgrad(ImVec2(c.x, c.y - S(8)), ImVec2(c.x + S(420), c.y + S(76)), IM_COL32(6, 16, 40, (int)(200 * al)), IM_COL32(6, 16, 40, 0));
            outlined(sBold, size, ImVec2(c.x - text_w(sBold, size, nm.c_str()) * 0.5f, c.y), IM_COL32(255, 255, 255, A), IM_COL32(4, 12, 32, A), nm, S(2));
            std::string sub = l.arrive.empty() ? l.desc : l.arrive;
            text(sFont, S(17), ImVec2(c.x - text_w(sFont, S(17), sub.c_str()) * 0.5f, c.y + S(52)), IM_COL32(176, 204, 236, A), sub);
        }
        outlined(sBold, S(22), P(40, 30), kInk, IM_COL32(4, 12, 32, 255), upper(l.name), S(1.5f));
        sDl->AddLine(P(40, 60), P(300, 60), kHi1, S(2));
        text(sFont, S(15), P(42, 64), kDim, l.region == "earth" ? "Earth" : l.region == "space" ? "Outer space" : "The other world");
        // who is here: their faces, what they do, how far
        float y = P(0, 100).y;
        for (size_t i = 0; i < sHubWho.size() && i < 8; i++) {
            float dx = gPortHubPlaces[2 + i * 2] - gPortHubPlaces[0], dz = gPortHubPlaces[3 + i * 2] - gPortHubPlaces[1];
            if (gPortHubPlaces[3 + i * 2] > 1.0e8f) { continue; }
            int m = (int)(sqrtf(dx * dx + dz * dz) * 0.3f);
            bool nearIt = gPortHubNear == (int)i;
            ImVec2 a(P(40, 0).x, y), b(P(300, 0).x, y + S(38));
            hgrad(a, b, nearIt ? IM_COL32(232, 96, 14, 210) : IM_COL32(6, 16, 40, 190), IM_COL32(6, 16, 40, 0));
            face_at(sHubWho[i].chara, ImVec2(a.x + S(3), a.y + S(3)), ImVec2(a.x + S(35), a.y + S(35)), S(3));
            text(sBold, S(15), ImVec2(a.x + S(44), a.y + S(2)), kInk, StoryKit_ShortName(sHubWho[i].chara), S(170));
            text(sFont, S(12), ImVec2(a.x + S(44), a.y + S(20)), sHubWho[i].role == "quests" && quests_open_at(l.id) > 0 ? kHi1 : kDim, role_name(sHubWho[i]));
            std::string d = std::to_string(m) + " m";
            text(sBold, S(14), ImVec2(b.x - S(16) - text_w(sBold, S(14), d.c_str()), a.y + S(11)), kCyan, d);
            y += S(42);
        }
        int near = gPortHubNear;
        if (phase == 0 && near >= 0 && near < (int)sHubWho.size()) {
            const Who &w = sHubWho[(size_t)near];
            std::string line = "Talk to " + StoryKit_ShortName(w.chara) + "  (" + role_name(w) + ")";
            float tw = text_w(sBold, S(22), line.c_str()) + S(110);
            ImVec2 a(P(688, 0).x - tw * 0.5f, P(0, 600).y), b(a.x + tw, a.y + S(56));
            chamfer(a, b, IM_COL32(6, 16, 40, 225), S(14));
            chamfer_edge(a, b, kHi1, S(14), S(2));
            face_at(w.chara, ImVec2(a.x + S(10), a.y + S(6)), ImVec2(a.x + S(54), a.y + S(50)), S(4));
            float x = a.x + S(62);
            x += glyph(B_CIRCLE, ImVec2(x, (a.y + b.y) * 0.5f), S(13)) + S(10);
            text(sBold, S(22), ImVec2(x, (a.y + b.y) * 0.5f - S(13)), kInk, line);
        }
    }
    if (gPortHubMapOpen) { // the world map, over the place; the game's thread collects its buttons (MAPKEY_)
        ImVec2 a = P(140, 150), b = P(1236, 560);
        if (sMapCur < 0 || sMapCur >= (int)gLocs.size()) { sMapCur = loc_index(sSave.location); }
        int keys = __atomic_exchange_n(&gPortHubMapKeys, 0, __ATOMIC_SEQ_CST);
        if (keys & 1) { map_move(-1, 0, a, b); }
        if (keys & 2) { map_move(1, 0, a, b); }
        if (keys & 4) { map_move(0, -1, a, b); }
        if (keys & 8) { map_move(0, 1, a, b); }
        if (keys & 0x40) { sMapCur = (sMapCur + (int)gLocs.size() - 1) % (int)gLocs.size(); }
        if (keys & 0x80) { sMapCur = (sMapCur + 1) % (int)gLocs.size(); }
        sDl->AddRectFilled(P(0, 0), P(1376, 768), IM_COL32(0, 4, 16, 110));
        map_draw(a, b, 0.0f, -1);
        prompts({{B_CROSS, "Fly there"}, {B_L1R1, "Next place"}, {B_CIRCLE, "Close"}});
        if (keys & 0x10) {
            if (sMapCur == loc_index(sSave.location)) { notice("You're already here."); }
            else { gPortHubTravel = sMapCur; gPortHubMapOpen = 0; }
        } else if (keys & 0x20) {
            gPortHubMapOpen = 0;
        }
    } else if (phase == 0) {
        prompts({{B_CIRCLE, "Talk"}, {B_TRIANGLE, "World Map"}, {B_START, "Patrol Menu"}});
    }
    if (phase == 3 && sMapCur >= 0 && sMapCur < (int)gLocs.size()) {
        std::string t = "Flying to " + gLocs[(size_t)sMapCur].name + "...";
        outlined(sBold, S(26), ImVec2(P(688, 0).x - text_w(sBold, S(26), t.c_str()) * 0.5f, P(0, 640).y), kInk, IM_COL32(4, 12, 32, 255), t, S(1.5f));
    }
    int fade = gPortHubFade;
    if (fade > 0) { sDl->AddRectFilled(ImVec2(sX0, sY0), ImVec2(sX0 + sW, sY0 + sH), IM_COL32(0, 0, 0, std::min(255, fade))); }
    notice_draw();
}

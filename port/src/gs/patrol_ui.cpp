// Corrupt Patrol (main menu item 15): a game of its own inside the game.
//
// The player makes a warrior of one of six races and joins the Corrupt Patrol: a power is amplifying the strongest
// fighters and turning them evil (a purple glow around them). Each chapter of the story is a fight with one of them;
// beaten and freed, they become a master, who trains the player in battles that get harder, one technique learnt per
// win, and everything they have (moves, fighting style, outfit) at the end. Between fights the player is at the Patrol's
// headquarters on the Lookout: the quest giver, the masters, a shop of accessories, a board of bounties, the wardrobe.
// Levels, experience and zeni; a save file of its own (patrol/save.json next to the game, BT3_PATROL another folder),
// never the game's.
//
// This file is the screens (Dear ImGui into the background list, on the render thread, as gs/story_ui.cpp draws the
// Story Creator's), the save, the mode's content, and the making of its battles: each is an episode of a saga made here
// (scenes before and after, the fight, the Z items of each side), played through the Story Creator's engine
// (PortStoryUi_PrepareDoc, src/battle/port_story_btl.c). The game's half is src/menu/port_patrol.c.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <algorithm>
#include <functional>
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
extern "C" {
int PortStoryUi_PrepareDoc(const char *json, int episode);
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
struct Race {
    const char *name, *desc;
    int avatar;      // the disc fighter the warrior fights as until their own look is made (Custom Character's Patrol mode)
    int avatarCostume;
};
const Race kRaces[6] = {
    {"Saiyan", "A warrior race that grows stronger after every battle. Strong melee and ki blasts, and a fighting spirit that never breaks.", 73, 0},
    {"Namekian", "Calm, wise and resilient. Long reach, quick recovery, and the patience to outlast any opponent.", 24, 0},
    {"Arcosian", "The race of Frieza: cold, fast and ruthless. Fearsome ki techniques and a body built to survive anything.", 91, 0},
    {"Human", "No tail, no transformations, just discipline. Humans master technique faster than any other race.", 27, 0},
    {"Majin", "Born of magic and mischief. Hard to hurt, harder to stop, and always hungry for a fight.", 65, 0},
    {"Bio-Android", "Built from the cells of the greatest fighters. Learns from every opponent it faces, like Cell himself.", 109, 0},
};

// A master: first a corrupted foe (their chapter of the story), then a teacher. Their five techniques are their own
// character's five (slots 0..4: two Blast 1 skills, two Blast 2, the ultimate); the last training gives the rest.
struct Line { int who; const char *text; const char *fx; }; // who: 0 the player, 1 the master / foe, 2 the narrator
struct Master {
    const char *key, *name;
    int chara, corruptChara, stage, music;
    const char *place;         // where the chapter happens
    const char *brief;         // the quest giver's briefing
    std::vector<Line> intro, win, lose;
    const char *greet;         // as a master, at the training grounds
    const char *done;          // when every lesson is learnt
};
const char *kFxCorrupt = "aura_on";
std::vector<Master> kMasters = {
    {"piccolo", "Piccolo", 23, 23, 0, 2, "the Wasteland",
     "Piccolo was meditating alone in the wasteland when the purple energy found him. He has not answered King Kai since. Bring him back.",
     {{1, "So the Patrol sent a rookie... This power. It's incredible. I can finally see how weak I was.", "aura_on"},
      {0, "Piccolo, that energy isn't yours. Fight it!", ""},
      {1, "Fight it? I've never felt this clear. Come at me, and I'll show you what a demon can do!", "charge_on"}},
     {{1, "Ngh... the voice in my head... it's gone. That purple light was feeding on my pride.", "aura_off"},
      {0, "You're free now. The Patrol needs fighters like you.", ""},
      {1, "Then I'll teach you. Find me at the Lookout when you're ready to train.", ""}},
     {{1, "Pathetic. Go home before you get hurt.", ""}},
     "Focus. Every technique starts with a calm mind. Again!",
     "There's nothing left I can teach you. Don't waste it."},
    {"vegeta", "Vegeta", 31, 31, 2, 3, "Planet Namek",
     "Vegeta flew to the ruins of Namek chasing a rumour of power. He found it. Now his pride burns purple.",
     {{1, "Hah! This is the power of a true Saiyan prince! Kakarot can never reach me now!", "aura_on"},
      {0, "Vegeta, you're being used!", ""},
      {1, "Used? I am the one using it! Get on your knees, insect!", "charge_on"}},
     {{1, "Tch... I don't need some cheap energy to be the strongest. I never did.", "aura_off"},
      {0, "Then help us stop whoever is spreading it.", ""},
      {1, "Fine. But if you train with me, you'd better keep up.", ""}},
     {{1, "Know your place. You're nothing next to a prince.", ""}},
     "Don't hold back! A Saiyan fights with everything he has!",
     "You've earned my techniques. Don't make me regret it."},
    {"gohan", "Gohan", 17, 17, 6, 6, "the Cell Games Arena",
     "Gohan's hidden power was always his greatest strength. The corruption has unlocked all of it, and none of his kindness.",
     {{1, "I don't want to hold back anymore. I'm tired of being gentle.", "aura_on"},
      {0, "This isn't you, Gohan!", ""},
      {1, "Maybe this is the real me. Let's find out!", "charge_on"}},
     {{1, "What... what was I doing? I could have really hurt you.", "aura_off"},
      {0, "You held back at the end. That was the real you.", ""},
      {1, "Thank you. Let me help, I'll teach you everything my father and Piccolo taught me.", ""}},
     {{1, "Sorry... no, I'm not.", ""}},
     "Stay calm and let your power rise when you need it.",
     "You're stronger than I was at your age. Use it to protect people."},
    {"frieza", "Frieza", 94, 94, 3, 5, "Dying Namek",
     "Frieza has returned from Hell, and the purple power has made him worse than ever. He wants the Patrol gone.",
     {{1, "Ohoho... a little patroller. Do you know what this energy has shown me? I was always meant to rule everything.", "aura_on"},
      {0, "Your rule ends here, Frieza.", ""},
      {1, "Such insolence. I'll enjoy this.", "charge_on"}},
     {{1, "Impossible... that power left me? How dare it!", "aura_off"},
      {0, "Now you're just Frieza again.", ""},
      {1, "Hmph. Fine. I'll share my techniques, if only to watch you destroy whoever humiliated me.", ""}},
     {{1, "Kneel. That is all you were made for.", ""}},
     "Faster! Do you think an emperor waits for anyone?",
     "You learn quickly... for a lesser being. Go."},
    {"cell", "Cell", 107, 107, 6, 7, "the Cell Games Arena",
     "Cell has absorbed the corruption itself. He is calling it perfection, and he is hosting a new Cell Games to prove it.",
     {{1, "Welcome to my games. This energy has made me more than perfect.", "aura_on"},
      {0, "Then let's see how perfect you are.", ""},
      {1, "Wonderful. Do try to entertain me.", "charge_on"}},
     {{1, "So the energy had a flaw after all. Fascinating... and irritating.", "aura_off"},
      {0, "Help us find the source.", ""},
      {1, "Very well. My cells hold every great technique. You may study them.", ""}},
     {{1, "As expected. Imperfect.", ""}},
     "Analyse. Adapt. That is how perfection is built.",
     "You've taken all I have to give. How... perfect."},
    {"buu", "Majin Buu", 112, 112, 4, 4, "the World Tournament Stage",
     "Majin Buu was happy and peaceful, until the purple energy reached him. Now he's angry, and he won't stop.",
     {{1, "Buu not happy! Buu want to smash everything!", "aura_on"},
      {0, "Buu, calm down! It's the energy talking!", ""},
      {1, "Buu turn you into candy!", "charge_on"}},
     {{1, "Ooh... Buu head feel better. You nice. Buu sorry.", "aura_off"},
      {0, "Want to help us?", ""},
      {1, "Buu teach you! Buu teach good!", ""}},
     {{1, "Hee hee! Buu win!", ""}},
     "Again, again! Buu like this game!",
     "You know everything Buu know! Buu proud!"},
    {"goku", "Goku", 3, 3, 7, 8, "Supreme Kai's World",
     "Goku went looking for the source of the corruption alone. He found it, and it found him. The strongest Saiyan is now its champion.",
     {{1, "Hey... I've been waiting for someone strong. This power is amazing, I just want to keep fighting!", "aura_on"},
      {0, "Goku, this isn't the kind of fight you love!", ""},
      {1, "Every fight is the kind I love! Let's go!", "charge_on"}},
     {{1, "Whoa... my head's clear again. Sorry about that! You're really strong!", "aura_off"},
      {0, "The one behind this is still out there.", ""},
      {1, "Then we'll go together. But first, let me teach you a few things!", ""}},
     {{1, "Heh, that was fun! Come back when you're stronger!", ""}},
     "Alright! Show me what you've got!",
     "You've learnt everything I know. You might even beat me now!"},
    {"janemba", "Janemba", 138, 138, 18, 5, "the depths of Hell",
     "We've found the source. A rift in Hell is pouring out the corruption, and something is standing in it. End this.",
     {{2, "The rift tears open. Raw evil pours out, and takes a shape.", "flash"},
      {1, "...DESTROY... ALL...", "aura_on"},
      {0, "So you're the one behind all of it. It ends now!", "charge_on"}},
     {{2, "The rift collapses. The purple light fades from every corner of the world.", "flash"},
      {1, "...", "aura_off"},
      {0, "It's over. The Patrol did it.", ""}},
     {{1, "...RETURN... TO... NOTHING...", ""}},
     "...TRAIN...",
     "...ENOUGH..."},
};
const int kLessons = 5; // trainings per master: one technique each, the last everything else too

struct ShopItem { const char *name, *desc; int item, price, level; };
const ShopItem kShop[] = {
    {"Battle Gloves", "Attack +5", 1, 400, 1},
    {"Patrol Vest", "Defense +5", 4, 400, 1},
    {"Ki Focus Ring", "Ki Blast +5", 7, 400, 1},
    {"Spirit Band", "Ki +5", 10, 400, 1},
    {"Weighted Gauntlets", "Attack +10", 2, 1500, 8},
    {"Saiyan Battle Armour", "Defense +10", 5, 1500, 8},
    {"Energy Amplifier", "Ki Blast +10", 8, 1500, 8},
    {"Spirit Crest", "Ki +10", 11, 1500, 8},
    {"Senzu Pouch", "One more health bar", 125, 5000, 12},
    {"Berserker Band", "Attack +10, Defense -5", 13, 900, 5},
    {"Legendary Gauntlets", "Attack +15", 3, 4500, 20},
    {"Royal Armour", "Defense +15", 6, 4500, 20},
    {"Destructo Core", "Ki Blast +15", 9, 4500, 20},
    {"Kai Earrings", "Ki +15", 12, 4500, 20},
    {"Sacred Water", "Two more health bars", 126, 14000, 28},
};
const int kShopCount = (int)(sizeof(kShop) / sizeof(kShop[0]));
const int kEquipMax = 3;

// The headquarters' people (the 3D model on the right while the cursor is on them)
enum { NPC_QUEST, NPC_MASTERS, NPC_SHOP, NPC_BOARD, NPC_WARDROBE, NPC_MENU, NPC_COUNT };
struct Npc { const char *place, *name; int chara; const char *hello; };
const Npc kNpcs[NPC_COUNT] = {
    {"Main Quest", "Supreme Kai", 58, "The corruption is spreading. Every master we save makes the Patrol stronger."},
    {"Training Grounds", "Your masters", 23, "The fighters you've freed are waiting to train you."},
    {"Hercule's Shop", "Mr Satan", 55, "Step right up! The world champion's personal gear, for a small fee!"},
    {"Bounty Board", "Krillin", 25, "Corrupted small fry are popping up all over. Clear them out for zeni and experience!"},
    {"Wardrobe", "Videl", 56, "Want a new look? Your race's outfits are all here, and your masters' once you earn them."},
    {"Patrol Menu", "Patrol Menu", -1, "Your stats, accessories, techniques, quest log and save."},
};

// The bounties: fighters of the right strength for the player's level, on a random stage
const std::vector<std::vector<int>> kBountyTiers = {
    {79, 99, 77, 80, 98},           // Saibamen, soldiers, Raditz, Nappa, Appule
    {82, 84, 85, 87, 88, 89, 90},   // the Ginyu Force and Frieza's men
    {86, 103, 129, 118, 121, 120},  // Ginyu, Android 19, Android 13, Garlic Jr, Turles, Wheelo
    {126, 135, 123, 111, 101, 134}, // Cooler, Bojack, Slug, Dabura, Android 17, Zangya
    {133, 147, 144, 128, 136, 139}, // Broly, Omega Shenron, Super 17, Metal Cooler, Bojack, Hildegarn
};
const int kBountyStages[] = {0, 1, 9, 10, 11, 13, 14, 15, 19, 22, 23, 24, 25, 29, 31, 33};

// =============================================================================================== the save
struct Save {
    bool exists = false;
    std::string name;
    int race = 0;
    std::string fighter;        // the warrior's character package (Custom Character's Patrol mode), "" none yet
    int level = 1, xp = 0, zeni = 500;
    int chapter = 0;            // chapters cleared (the next is this one)
    int masterWins[16] = {0};   // trainings won with each master
    std::vector<int> owned, equipped;
    int bounties = 0, wins = 0, losses = 0;
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
void save_load(void) {
    sSave = Save();
    std::string text;
    J d;
    if (!read_file(save_path(), text) || !js_read(text, d) || !d.isObj()) { return; }
    sSave.exists = true;
    sSave.name = d["name"].text("Patroller");
    sSave.race = std::max(0, std::min(5, (int)d["race"].number(0)));
    sSave.fighter = d["fighter"].text();
    sSave.level = std::max(1, (int)d["level"].number(1));
    sSave.xp = (int)d["xp"].number(0);
    sSave.zeni = (int)d["zeni"].number(0);
    sSave.chapter = std::max(0, std::min((int)kMasters.size(), (int)d["chapter"].number(0)));
    for (size_t m = 0; m < kMasters.size(); m++) { sSave.masterWins[m] = std::max(0, std::min(kLessons, (int)d["masters"][kMasters[m].key].number(0))); }
    sSave.owned = ints_of(d["owned"]);
    sSave.equipped = ints_of(d["equipped"]);
    if ((int)sSave.equipped.size() > kEquipMax) { sSave.equipped.resize(kEquipMax); }
    sSave.bounties = (int)d["bounties"].number(0);
    sSave.wins = (int)d["wins"].number(0);
    sSave.losses = (int)d["losses"].number(0);
    sSave.playtime = d["playtime"].number(0);
    sSave.loadout = d["loadout"].isObj() ? d["loadout"] : J::obj();
}
bool save_write(void) {
    J d = J::obj();
    d.set("format", J::num(1));
    d.set("name", J::str(sSave.name));
    d.set("race", J::num(sSave.race));
    d.set("race_name", J::str(kRaces[sSave.race].name));
    d.set("fighter", J::str(sSave.fighter));
    d.set("level", J::num(sSave.level));
    d.set("xp", J::num(sSave.xp));
    d.set("zeni", J::num(sSave.zeni));
    d.set("chapter", J::num(sSave.chapter));
    J m = J::obj();
    for (size_t i = 0; i < kMasters.size(); i++) { m.set(kMasters[i].key, J::num(sSave.masterWins[i])); }
    d.set("masters", m);
    d.set("owned", ints(sSave.owned));
    d.set("equipped", ints(sSave.equipped));
    d.set("bounties", J::num(sSave.bounties));
    d.set("wins", J::num(sSave.wins));
    d.set("losses", J::num(sSave.losses));
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
const int kLevelMax = 50;
bool owns(int item) { return std::find(sSave.owned.begin(), sSave.owned.end(), item) != sSave.owned.end(); }
bool equipped(int item) { return std::find(sSave.equipped.begin(), sSave.equipped.end(), item) != sSave.equipped.end(); }
bool master_unlocked(int m) { return sSave.chapter > m; }
// The player's stat items from their level (hidden: each stat steps up at its own levels) and the accessories worn
std::vector<int> player_items(void) {
    std::vector<int> v;
    int lv = sSave.level;
    const int base[4] = {1, 4, 7, 10}; // attack, defense, ki blast, ki: +5 / +10 / +15
    for (int s = 0; s < 4; s++) {
        int tier = std::min(3, (lv + s * 3) / 12);
        if (tier > 0) { v.push_back(base[s] + tier - 1); }
    }
    for (int it : sSave.equipped) { if ((int)v.size() < 8) { v.push_back(it); } }
    return v;
}
int player_bars(void) { // health bars, as player_items gives them on top of the warrior's three
    int b = 3;
    for (int it : sSave.equipped) { b += it == 125 ? 1 : it == 126 ? 2 : 0; }
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

// =============================================================================================== shared with the game's thread
enum { PG_LEAVE, PG_SOUND, PG_KEY, PG_CHARA, PG_COSTUME, PG_TURN, PG_BGM, PG_VOICE };
SDL_Mutex *sMx;
volatile int sActive;
struct Shared {
    int leave = 0;
    int sound[16] = {0}, sounds = 0;
    int key = -1, chara = 0, costume = 0, turn = 0, voice = -1, bgm = 6;
    int result = 0, resultGen = 0;
};
Shared sSh;
std::string sDoc;     // the battle to play (a saga of one episode)
enum { FIGHT_NONE, FIGHT_CHAPTER, FIGHT_TRAIN, FIGHT_BOUNTY };
int sFightKind, sFightA, sFightB; // what it is: chapter n / master m lesson k / bounty of tier
int sResultSeen;
void sound(int id) {
    SDL_LockMutex(sMx);
    if (sSh.sounds < 16) { sSh.sound[sSh.sounds++] = id; }
    SDL_UnlockMutex(sMx);
}
void want_model(int chara, int costume) {
    SDL_LockMutex(sMx);
    if (chara < 0) { sSh.key = -1; }
    else if (sSh.chara != chara || sSh.costume != costume || sSh.key < 0) { sSh.chara = chara; sSh.costume = costume; sSh.key = (sSh.key < 0 ? 0 : sSh.key) + 1; }
    SDL_UnlockMutex(sMx);
}

// =============================================================================================== drawing (the Story Creator's kit, in the Patrol's colours)
float sX0, sY0, sW, sH, sS;
ImVec2 P(float x, float y) { return ImVec2(sX0 + x * sW / 1376.0f, sY0 + y * sH / 768.0f); }
float S(float l) { return l * sS; }
ImFont *sFont, *sBold;
ImDrawList *sDl;
const ImU32 kInk = IM_COL32(255, 255, 255, 255), kDim = IM_COL32(214, 186, 240, 255), kLock = IM_COL32(150, 130, 170, 255);
const ImU32 kVio1 = IM_COL32(190, 90, 255, 255), kVio2 = IM_COL32(110, 30, 190, 255), kGold = IM_COL32(255, 214, 70, 255);
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
void panel(ImVec2 a, ImVec2 b) {
    sDl->AddRectFilled(ImVec2(a.x, a.y + S(5)), ImVec2(b.x, b.y + S(5)), IM_COL32(0, 0, 0, 80), S(12));
    sDl->AddRectFilled(a, b, IM_COL32(28, 10, 46, 232), S(12));
    sDl->AddRect(a, b, IM_COL32(200, 110, 255, 255), S(12), 0, S(3));
}
void face_at(int chara, ImVec2 a, ImVec2 b, float round) {
    ImTextureID t;
    ImVec2 u0, u1;
    sDl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 22), round);
    if (chara >= 0 && StoryKit_Face(chara, &t, &u0, &u1)) { sDl->AddImageRounded(ImTextureRef(t), a, b, u0, u1, IM_COL32_WHITE, round); }
}
enum { B_CROSS, B_CIRCLE, B_SQUARE, B_TRIANGLE, B_L1R1, B_START, B_LR };
float glyph(int b, ImVec2 c, float r) {
    static const char *const kKeys[] = {"Enter", "Esc", "F", "R", "Q/E", "Tab", "Left/Right"};
    if (sKeyboardLast || b >= B_L1R1) {
        const char *k = sKeyboardLast ? kKeys[b] : b == B_L1R1 ? "L1 R1" : b == B_START ? "START" : "< >";
        float w = text_w(sFont, r * 1.05f, k) + r * 1.2f;
        sDl->AddRectFilled(ImVec2(c.x, c.y - r * 0.85f), ImVec2(c.x + w, c.y + r * 0.85f), IM_COL32(48, 16, 80, 255), r * 0.4f);
        sDl->AddRect(ImVec2(c.x, c.y - r * 0.85f), ImVec2(c.x + w, c.y + r * 0.85f), IM_COL32(220, 170, 255, 255), r * 0.4f, 0, S(1.5f));
        sDl->AddText(sFont, r * 1.05f, ImVec2(c.x + r * 0.6f, c.y - r * 0.58f), kInk, k);
        return w;
    }
    ImVec2 m(c.x + r, c.y);
    sDl->AddCircleFilled(m, r, IM_COL32(48, 16, 80, 255));
    sDl->AddCircle(m, r, IM_COL32(220, 170, 255, 255), 0, S(1.5f));
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
    for (const Prompt &p : ps) { total += S(70) + text_w(sFont, size, p.label.c_str()) + gap; }
    ImVec2 at = P(1336, 742);
    float x = std::max(at.x - total, P(40, 0).x);
    for (const Prompt &p : ps) {
        x += glyph(p.b, ImVec2(x, at.y), r) + S(6);
        outlined(sFont, size, ImVec2(x, at.y - size * 0.55f), kInk, IM_COL32(20, 4, 30, 255), p.label, S(1.2f));
        x += text_w(sFont, size, p.label.c_str()) + gap;
    }
}
void tip_box(const std::string &line, int who) {
    ImVec2 a = P(40, 664), b = P(1336, 720);
    sDl->AddRectFilled(a, b, IM_COL32(24, 8, 40, 238), S(8));
    sDl->AddRect(a, b, IM_COL32(210, 150, 255, 255), S(8), 0, S(2));
    float r = (b.y - a.y) * 0.5f - S(4);
    ImVec2 c(a.x + S(10) + r, (a.y + b.y) * 0.5f);
    sDl->AddCircleFilled(c, r + S(3), IM_COL32(220, 170, 255, 255));
    ImTextureID t;
    ImVec2 u0, u1;
    if (who >= 0 && StoryKit_Face(who, &t, &u0, &u1)) { sDl->AddImageRounded(ImTextureRef(t), ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), u0, u1, IM_COL32_WHITE, r); }
    else { sDl->AddCircleFilled(c, r, kVio2); }
    float size = S(19);
    sDl->PushClipRect(a, b, true);
    bool two = text_w(sFont, size, line.c_str()) > (b.x - a.x) - r * 2 - S(40);
    sDl->AddText(sFont, size, ImVec2(c.x + r + S(16), c.y - size * 0.55f - (two ? size * 0.5f : 0)), kInk, line.c_str(), NULL, (b.x - c.x) - r - S(30));
    sDl->PopClipRect();
}
void heading(const std::string &h, const std::string &sub) {
    ImVec2 e = P(76, 56);
    float er = S(30);
    sDl->AddCircleFilled(ImVec2(e.x, e.y + S(3)), er, IM_COL32(40, 0, 60, 255));
    sDl->AddCircleFilled(e, er, kVio2);
    sDl->AddCircleFilled(ImVec2(e.x - er * 0.25f, e.y - er * 0.3f), er * 0.55f, IM_COL32(230, 150, 255, 150));
    sDl->AddCircle(e, er, IM_COL32(240, 200, 255, 255), 0, S(3));
    outlined(sBold, er * 1.1f, ImVec2(e.x - text_w(sBold, er * 1.1f, "CP") * 0.5f, e.y - er * 0.55f), kInk, IM_COL32(40, 0, 60, 255), "CP", S(1.5f));
    std::string up = h;
    for (char &c : up) { c = (char)SDL_toupper((unsigned char)c); }
    outlined(sBold, S(46), P(120, 30), IM_COL32(236, 180, 255, 255), IM_COL32(50, 0, 80, 255), up, S(2.5f));
    if (!sub.empty()) {
        float size = S(16), w = text_w(sBold, size, sub.c_str()) + S(36), h2 = S(32);
        ImVec2 a(P(40, 0).x, P(0, 98).y), b(a.x + w, a.y + h2);
        grad(a, b, kVio1, kVio2, h2 * 0.5f);
        sDl->AddRect(a, b, IM_COL32(240, 200, 255, 255), h2 * 0.5f, 0, S(2));
        sDl->AddText(sBold, size, ImVec2(a.x + S(18), a.y + (h2 - size) * 0.5f), kInk, sub.c_str());
    }
}
std::string sNotice;
int sNoticeUntil;
void notice(const std::string &t) { sNotice = t; sNoticeUntil = sFrames + 60 * 4; fprintf(stderr, "bt3: patrol: %s\n", t.c_str()); }
void notice_draw(void) {
    if (sNotice.empty() || sFrames > sNoticeUntil) { return; }
    float size = S(18), w = text_w(sBold, size, sNotice.c_str()) + S(40);
    ImVec2 a(P(688, 0).x - w * 0.5f, P(0, 610).y), b(a.x + w, a.y + S(40));
    sDl->AddRectFilled(a, b, IM_COL32(60, 10, 90, 240), S(20));
    sDl->AddRect(a, b, kVio1, S(20), 0, S(2));
    sDl->AddText(sBold, size, ImVec2(a.x + S(20), a.y + (S(40) - size) * 0.5f), kInk, sNotice.c_str());
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
    if (!selectable(l.cur)) { for (int i = 0; i < n; i++) { if (selectable(i)) { l.cur = i; break; } } }
    if (input) {
        int was = l.cur;
        if (sIn.up) { int i = l.cur - 1; while (i >= 0 && !selectable(i)) { i--; } if (i >= 0) { l.cur = i; } }
        if (sIn.down) { int i = l.cur + 1; while (i < n && !selectable(i)) { i++; } if (i < n) { l.cur = i; } }
        if (l.cur != was) { sound(0); }
    }
    float rh = S(46), y = a.y + S(14);
    int fit = std::max(1, (int)((b.y - a.y - S(28)) / rh));
    if (l.cur < l.top) { l.top = l.cur; }
    if (l.cur >= l.top + fit) { l.top = l.cur - fit + 1; }
    for (int i = l.top; i < n && i < l.top + fit; i++, y += rh) {
        Row &r = rows[(size_t)i];
        ImVec2 ra(a.x + S(12), y), rb(b.x - S(12), y + rh - S(6));
        if (r.sec) {
            text(sBold, S(15), ImVec2(ra.x + S(4), ra.y + S(14)), IM_COL32(230, 170, 255, 255), r.label);
            sDl->AddLine(ImVec2(ra.x + S(4) + text_w(sBold, S(15), r.label.c_str()) + S(10), ra.y + S(24)), ImVec2(rb.x, ra.y + S(24)), IM_COL32(150, 80, 200, 200), S(1.5f));
            continue;
        }
        bool on = i == l.cur;
        if (on) {
            grad(ra, rb, kVio1, kVio2, S(10));
            sDl->AddRect(ra, rb, IM_COL32(245, 215, 255, 255), S(10), 0, S(2));
        } else {
            sDl->AddRectFilled(ra, rb, IM_COL32(255, 255, 255, 14), S(10));
        }
        float x = ra.x + S(12);
        if (r.face >= 0) {
            face_at(r.face, ImVec2(x, ra.y + S(4)), ImVec2(x + rh - S(14), rb.y - S(4)), S(6));
            x += rh;
        }
        ImU32 c = r.locked ? kLock : kInk;
        float size = S(19);
        text(on ? sBold : sFont, size, ImVec2(x, ra.y + (rh - S(6) - size) * 0.5f - (r.sub.empty() ? 0 : S(7))), c, r.label, (rb.x - x) * 0.6f);
        if (!r.sub.empty()) { text(sFont, S(13), ImVec2(x, ra.y + (rh - S(6)) * 0.5f + S(3)), r.locked ? kLock : kDim, r.sub, (rb.x - x) * 0.6f); }
        if (!r.value.empty()) {
            float w = text_w(sBold, S(17), r.value.c_str());
            text(sBold, S(17), ImVec2(rb.x - w - S(14), ra.y + (rh - S(6) - S(17)) * 0.5f), r.locked ? kLock : kGold, r.value);
        }
    }
    if (l.top > 0) { sDl->AddTriangleFilled(ImVec2((a.x + b.x) * 0.5f - S(8), a.y + S(10)), ImVec2((a.x + b.x) * 0.5f + S(8), a.y + S(10)), ImVec2((a.x + b.x) * 0.5f, a.y + S(2)), kDim); }
    if (l.top + fit < n) { sDl->AddTriangleFilled(ImVec2((a.x + b.x) * 0.5f - S(8), b.y - S(10)), ImVec2((a.x + b.x) * 0.5f + S(8), b.y - S(10)), ImVec2((a.x + b.x) * 0.5f, b.y - S(2)), kDim); }
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
enum Scr { S_TITLE, S_RACE, S_NAME, S_HQ, S_QUEST, S_MASTERS, S_MASTER, S_SHOP, S_BOARD, S_MENU, S_STATS, S_GEAR, S_SKILLS, S_LOG,
           S_TALK, S_RESULT, S_WARDROBE };
Scr sScr = S_TITLE;
std::vector<Scr> sStack;
List sLists[32];
void go(Scr s) { sStack.push_back(sScr); sScr = s; }
void back(void) { if (!sStack.empty()) { sScr = sStack.back(); sStack.pop_back(); } }
void reset_to(Scr s) { sStack.clear(); sScr = s; }

// A talk: lines said in turn (the speaker's face, name and text), then what follows
struct Say { int chara; std::string name, text; };
std::vector<Say> sTalk;
size_t sTalkAt;
std::function<void()> sTalkThen;
void talk(const std::vector<Say> &lines, std::function<void()> then) {
    sTalk = lines;
    sTalkAt = 0;
    sTalkThen = then;
    go(S_TALK);
}
// A question with two answers
std::string sAsk;
std::function<void()> sAskYes;
bool sAsking;
int sAskCur;
void ask(const std::string &q, std::function<void()> yes) { sAsk = q; sAskYes = yes; sAsking = true; sAskCur = 1; }

// Results of the last battle (shown on S_RESULT)
std::vector<std::string> sResultLines;
std::string sResultTitle;

// ---- new game
int sRaceCur;
std::string sName;
int sKeyCur;
const char *const kKeys = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

// ---- battles: a saga of one episode, played by the Story Creator's engine
J line_j(const Line &ln, const std::string &playerName) {
    J l = J::obj();
    l.set("speaker", J::str(ln.who == 0 ? "player" : ln.who == 1 ? "enemy" : "narrator"));
    l.set("text", J::str(ln.text));
    if (ln.who == 0) { l.set("name", J::str(playerName)); }
    if (ln.who == 1) { l.set("voice", J::str("random")); }
    if (ln.fx != NULL && ln.fx[0]) { J fx = J::arr(); fx.push(J::str(ln.fx)); l.set("effects", fx); }
    return l;
}
J scene_j(const std::vector<Line> &lines, int distance) {
    J sc = J::obj();
    sc.set("distance", J::num(distance));
    J ls = J::arr();
    for (const Line &ln : lines) { ls.push(line_j(ln, sSave.name)); }
    sc.set("lines", ls);
    return sc;
}
void play(J ep, int kind, int a, int b) {
    J me;
    fighter_ref(&me);
    J p = J::obj();
    p.set("chara", me);
    J pl = J::arr();
    pl.push(p);
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
    SDL_LockMutex(sMx);
    sSh.leave = 2;
    SDL_UnlockMutex(sMx);
}
J enemy_j(int chara, bool corrupt) {
    J e = J::obj();
    e.set("chara", J::num(chara));
    if (corrupt) { e.set("corrupt", J::boolean(true)); }
    J l = J::arr();
    l.push(e);
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
void play_chapter(int m) {
    const Master &ms = kMasters[(size_t)m];
    J ep = J::obj();
    ep.set("title", J::str("Chapter " + std::to_string(m + 1) + ": " + ms.name + " Corrupted"));
    ep.set("description", J::str(std::string("A corrupted ") + ms.name + " waits in " + ms.place + "."));
    ep.set("stage", J::num(ms.stage));
    ep.set("music", J::num(ms.music));
    ep.set("enemy", enemy_j(ms.corruptChara, true));
    ep.set("intro", scene_j(ms.intro, 2));
    // the corrupted are stronger than they ever were: every stat raised, more so later in the story
    int tier = std::min(3, 1 + m / 3);
    std::vector<int> items = {tier, 3 + tier, 6 + tier, 9 + tier};
    if (m >= 4) { items.push_back(125); }
    if (m >= 7) { items.push_back(126); }
    ep.set("battle", battle_j(std::min(33, 13 + m * 3), items, 0));
    ep.set("win", scene_j(ms.win, 2));
    ep.set("lose", scene_j(ms.lose, 2));
    play(ep, FIGHT_CHAPTER, m, 0);
}
void play_training(int m, int lesson) {
    const Master &ms = kMasters[(size_t)m];
    J ep = J::obj();
    ep.set("title", J::str(std::string(ms.name) + "'s Training " + std::to_string(lesson + 1) + " of " + std::to_string(kLessons)));
    ep.set("description", J::str("Lesson: " + move_name(ms.chara, lesson)));
    ep.set("stage", J::num(lesson == kLessons - 1 ? ms.stage : 8)); // (the Time Chamber, the final lesson where they fought)
    ep.set("music", J::num(ms.music));
    ep.set("enemy", enemy_j(ms.chara, false));
    std::vector<Line> intro = {{1, ms.greet, ""}};
    std::vector<Line> win = {{1, lesson == kLessons - 1 ? ms.done : "Good. You've earned this technique.", ""}};
    std::vector<Line> lose = {{1, "Not yet. Come back and try again.", ""}};
    ep.set("intro", scene_j(intro, 2));
    int tier = std::min(3, lesson / 2 + m / 3);
    std::vector<int> items;
    if (tier > 0) { items = {tier, 3 + tier, 6 + tier, 9 + tier}; }
    ep.set("battle", battle_j(std::min(36, 6 + m * 3 + lesson * 4), items, 0));
    ep.set("win", scene_j(win, 2));
    ep.set("lose", scene_j(lose, 2));
    play(ep, FIGHT_TRAIN, m, lesson);
}
int bounty_tier(void) { return std::min((int)kBountyTiers.size() - 1, (sSave.level - 1) / 8); }
void play_bounty(void) {
    int t = bounty_tier();
    const std::vector<int> &pool = kBountyTiers[(size_t)t];
    int foe = pool[(size_t)(SDL_rand((Sint32)pool.size()))];
    int stage = kBountyStages[SDL_rand((Sint32)(sizeof(kBountyStages) / sizeof(kBountyStages[0])))];
    J ep = J::obj();
    ep.set("title", J::str("Bounty: Corrupted " + StoryKit_ShortName(foe)));
    ep.set("description", J::str("Reward: " + std::to_string(150 + t * 150) + " zeni"));
    ep.set("stage", J::num(stage));
    ep.set("music", J::num(24));
    ep.set("enemy", enemy_j(foe, true));
    std::vector<Line> intro = {{1, "The Patrol? You're too late. This power is mine!", "aura_on"}};
    ep.set("intro", scene_j(intro, 2));
    std::vector<int> items;
    if (t > 0) { items = {std::min(3, t), 3 + std::min(3, t)}; }
    ep.set("battle", battle_j(std::min(30, 6 + t * 6), items, 0));
    play(ep, FIGHT_BOUNTY, t, foe);
}

// ---- after a battle: experience, zeni, lessons, the story
void gain(int xp, int zeni) {
    sSave.zeni += zeni;
    sResultLines.push_back("+" + std::to_string(xp) + " experience   +" + std::to_string(zeni) + " zeni");
    if (sSave.level >= kLevelMax) { return; }
    sSave.xp += xp;
    while (sSave.level < kLevelMax && sSave.xp >= xp_to_next(sSave.level)) {
        sSave.xp -= xp_to_next(sSave.level);
        sSave.level++;
        sResultLines.push_back("LEVEL UP! You are now level " + std::to_string(sSave.level) + ".");
        for (const ShopItem &it : kShop) {
            if (it.level == sSave.level) { sResultLines.push_back(std::string("New in Hercule's Shop: ") + it.name + "."); }
        }
    }
}
void on_result(int result) {
    sResultLines.clear();
    bool won = result == 1;
    if (result == 3) {
        sResultTitle = "RETREATED";
        sResultLines.push_back("You left the fight. Nothing was gained or lost.");
    } else if (!won) {
        sResultTitle = "DEFEATED";
        sSave.losses++;
        sResultLines.push_back("You were beaten. Train, gear up, and try again.");
        gain(20, 0);
    } else {
        sResultTitle = "VICTORY";
        sSave.wins++;
        if (sFightKind == FIGHT_CHAPTER) {
            const Master &ms = kMasters[(size_t)sFightA];
            gain(300 + sFightA * 120, 800 + sFightA * 400);
            if (sSave.chapter == sFightA) {
                sSave.chapter = sFightA + 1;
                if (sFightA + 1 < (int)kMasters.size()) {
                    sResultLines.push_back(std::string(ms.name) + " is free, and is now your master.");
                    sResultLines.push_back("Train with them at the Training Grounds.");
                } else {
                    sResultLines.push_back("The source of the corruption is destroyed. The Patrol has won!");
                    sResultLines.push_back(std::string(ms.name) + " can now be trained with. Keep growing stronger.");
                }
            }
        } else if (sFightKind == FIGHT_TRAIN) {
            const Master &ms = kMasters[(size_t)sFightA];
            gain(150 + sFightA * 50 + sFightB * 30, 200 + sFightA * 100);
            int &wins = sSave.masterWins[sFightA];
            if (wins == sFightB) {
                wins++;
                sResultLines.push_back("Technique learnt: " + move_name(ms.chara, sFightB) + " (" + kSlotKinds[sFightB] + ").");
                if (wins >= kLessons) {
                    sResultLines.push_back(std::string("Training with ") + ms.name + " complete!");
                    sResultLines.push_back(std::string(ms.name) + "'s fighting style and outfit are unlocked.");
                }
            }
        } else if (sFightKind == FIGHT_BOUNTY) {
            sSave.bounties++;
            gain(80 + sFightA * 60, 150 + sFightA * 150);
        }
    }
    save_write();
    reset_to(S_HQ);
    go(S_RESULT);
}

// ---- the screens
void scr_title(bool input) {
    heading("Corrupt Patrol", "A NEW ADVENTURE");
    ImVec2 a = P(40, 150), b = P(560, 440);
    panel(a, b);
    static std::vector<Row> rows;
    rows.clear();
    Row r;
    r.label = "Return To Game";
    r.sub = "Back to the main menu";
    r.ok = [] { SDL_LockMutex(sMx); sSh.leave = 1; SDL_UnlockMutex(sMx); };
    rows.push_back(r);
    r = Row();
    r.label = "New Game";
    r.sub = "Create a warrior and join the Patrol";
    r.ok = [] {
        auto start = [] { sRaceCur = 0; sName.clear(); sKeyCur = 0; go(S_RACE); };
        if (sSave.exists) { ask("Start a new game? Your Corrupt Patrol save will be replaced.", start); }
        else { start(); }
    };
    rows.push_back(r);
    r = Row();
    r.label = "Continue";
    r.locked = !sSave.exists;
    r.sub = sSave.exists ? sSave.name + ", " + kRaces[sSave.race].name + ", level " + std::to_string(sSave.level) : "No save yet";
    r.ok = [] { reset_to(S_HQ); };
    rows.push_back(r);
    int cur = draw_rows(rows, sLists[S_TITLE], a, b, input && !sAsking);
    if (sSave.exists) {
        ImVec2 sa = P(40, 460), sb = P(560, 640);
        panel(sa, sb);
        face_at(fighter_chara(), ImVec2(sa.x + S(18), sa.y + S(18)), ImVec2(sa.x + S(118), sa.y + S(118)), S(10));
        text(sBold, S(24), ImVec2(sa.x + S(136), sa.y + S(20)), kInk, sSave.name);
        text(sFont, S(17), ImVec2(sa.x + S(136), sa.y + S(54)), kDim, std::string(kRaces[sSave.race].name) + "   Level " + std::to_string(sSave.level));
        text(sFont, S(17), ImVec2(sa.x + S(136), sa.y + S(80)), kDim, "Chapter " + std::to_string(std::min(sSave.chapter + 1, (int)kMasters.size())) + " of " + std::to_string(kMasters.size()) + "   " + std::to_string(sSave.zeni) + " zeni");
        int mins = (int)(sSave.playtime / 60);
        text(sFont, S(15), ImVec2(sa.x + S(136), sa.y + S(106)), kDim, "Played " + std::to_string(mins / 60) + "h " + std::to_string(mins % 60) + "m");
    }
    want_model(sSave.exists ? fighter_chara() : kMasters.back().chara, 0);
    tip_box(cur == 0 ? "Return to the main menu of the game." : cur == 1 ? "The strongest fighters are being corrupted by a purple power. Create a warrior of your own and stop it."
                                                                         : "Carry on where you left off.", 58);
    prompts({{B_CROSS, "Choose"}, {B_CIRCLE, "Main menu"}});
    if (input && !sAsking && sIn.back) { sound(2); SDL_LockMutex(sMx); sSh.leave = 1; SDL_UnlockMutex(sMx); }
}
void scr_race(bool input) {
    heading("Choose Your Race", "NEW GAME");
    float cw = S(196), ch = S(300), gap = S(14);
    ImVec2 o = P(40, 150);
    if (input) {
        if (sIn.left && sRaceCur > 0) { sRaceCur--; sound(0); }
        if (sIn.right && sRaceCur < 5) { sRaceCur++; sound(0); }
        if (sIn.up && sRaceCur >= 3) { sRaceCur -= 3; sound(0); }
        if (sIn.down && sRaceCur < 3) { sRaceCur += 3; sound(0); }
    }
    for (int i = 0; i < 6; i++) {
        ImVec2 a(o.x + (i % 3) * (cw + gap), o.y + (i / 3) * (ch * 0.62f + gap)), b(a.x + cw, a.y + ch * 0.62f);
        bool on = i == sRaceCur;
        if (on) { grad(a, b, kVio1, kVio2, S(12)); sDl->AddRect(a, b, IM_COL32(245, 215, 255, 255), S(12), 0, S(3)); }
        else { sDl->AddRectFilled(a, b, IM_COL32(28, 10, 46, 225), S(12)); sDl->AddRect(a, b, IM_COL32(150, 80, 200, 255), S(12), 0, S(2)); }
        face_at(kRaces[i].avatar, ImVec2(a.x + S(28), a.y + S(14)), ImVec2(b.x - S(28), a.y + S(14) + cw - S(56)), S(10));
        float size = S(22);
        outlined(sBold, size, ImVec2((a.x + b.x) * 0.5f - text_w(sBold, size, kRaces[i].name) * 0.5f, b.y - S(36)), kInk, IM_COL32(40, 0, 60, 255), kRaces[i].name, S(1.5f));
    }
    ImVec2 da = P(40, 560), db = P(660, 650);
    panel(da, db);
    wrapped(sFont, S(17), ImVec2(da.x + S(16), da.y + S(14)), kInk, kRaces[sRaceCur].desc, db.x - da.x - S(32));
    want_model(kRaces[sRaceCur].avatar, kRaces[sRaceCur].avatarCostume);
    tip_box("Your race decides the bodies, heads and outfits your warrior can wear. Masters' outfits unlock for every race.", 58);
    prompts({{B_CROSS, "Choose"}, {B_CIRCLE, "Back"}});
    if (input && sIn.ok) { sound(1); go(S_NAME); }
    else if (input && sIn.back) { sound(2); back(); }
}
void scr_name(bool input) {
    heading("Name Your Warrior", kRaces[sRaceCur].name);
    ImVec2 a = P(40, 150), b = P(660, 560);
    panel(a, b);
    ImVec2 na(a.x + S(20), a.y + S(20)), nb(b.x - S(20), a.y + S(80));
    sDl->AddRectFilled(na, nb, IM_COL32(0, 0, 0, 120), S(8));
    sDl->AddRect(na, nb, kVio1, S(8), 0, S(2));
    std::string shown = sName + ((sFrames / 30) % 2 ? "_" : " ");
    text(sBold, S(32), ImVec2(na.x + S(16), na.y + S(12)), kInk, shown);
    // typed letters (the keyboard)
    if (input) {
        ImGuiIO &io = ImGui::GetIO();
        for (int i = 0; i < io.InputQueueCharacters.Size; i++) {
            ImWchar c = io.InputQueueCharacters[i];
            if (c >= 32 && c < 127 && sName.size() < 16 && c != '"' && c != '\\' && c != '/' && c != ':') { sName += (char)c; }
        }
    }
    // the pad's keys: 36 letters and digits, then Space, Delete, Done
    const int cols = 10, count = 36 + 3;
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
        if (on) { grad(ka, kb, kVio1, kVio2, S(8)); } else { sDl->AddRectFilled(ka, kb, IM_COL32(255, 255, 255, 18), S(8)); }
        std::string k = i < 36 ? std::string(1, kKeys[i]) : i == 36 ? "SPC" : i == 37 ? "DEL" : "DONE";
        float size = S(i < 36 ? 22 : 15);
        text(sBold, size, ImVec2((ka.x + kb.x) * 0.5f - text_w(sBold, size, k.c_str()) * 0.5f, (ka.y + kb.y) * 0.5f - size * 0.55f), kInk, k);
    }
    want_model(kRaces[sRaceCur].avatar, kRaces[sRaceCur].avatarCostume);
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
        save_write();
        reset_to(S_HQ);
        const Npc &k = kNpcs[NPC_QUEST];
        talk({{k.chara, k.name, "Welcome, " + n + ". I am the Supreme Kai. You've arrived at a dark time."},
              {k.chara, k.name, "A purple power is spreading across the world. It finds the strongest fighters, amplifies them, and turns them evil."},
              {k.chara, k.name, "Piccolo was the first. Free him, and he'll teach you. Every master you save makes the Patrol stronger."},
              {k.chara, k.name, "This Lookout is the Patrol's headquarters. Speak to me when you're ready for your first mission."}}, nullptr);
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
    text(sBold, S(17), ImVec2(at.x + S(200), at.y), kInk, value);
}
void player_card(ImVec2 a, ImVec2 b) {
    panel(a, b);
    face_at(fighter_chara(), ImVec2(a.x + S(16), a.y + S(16)), ImVec2(a.x + S(96), a.y + S(96)), S(8));
    text(sBold, S(24), ImVec2(a.x + S(112), a.y + S(18)), kInk, sSave.name, b.x - a.x - S(130));
    text(sFont, S(16), ImVec2(a.x + S(112), a.y + S(50)), kDim, std::string(kRaces[sSave.race].name) + "   Lv " + std::to_string(sSave.level) + "   " + std::to_string(sSave.zeni) + " z");
    float w = b.x - a.x - S(130), f = sSave.level >= kLevelMax ? 1.0f : (float)sSave.xp / (float)xp_to_next(sSave.level);
    ImVec2 xa(a.x + S(112), a.y + S(78)), xb(xa.x + w, xa.y + S(10));
    sDl->AddRectFilled(xa, xb, IM_COL32(0, 0, 0, 140), S(5));
    sDl->AddRectFilled(xa, ImVec2(xa.x + w * f, xb.y), kVio1, S(5));
}
void scr_hq(bool input) {
    heading("Patrol HQ", "KAMI'S LOOKOUT");
    ImVec2 a = P(40, 150), b = P(560, 520);
    panel(a, b);
    static std::vector<Row> rows;
    rows.clear();
    for (int i = 0; i < NPC_COUNT; i++) {
        const Npc &n = kNpcs[i];
        Row r;
        r.label = n.place;
        r.sub = n.name;
        r.face = i == NPC_MASTERS ? (sSave.chapter > 0 ? kMasters[0].chara : -1) : n.chara;
        if (i == NPC_QUEST) {
            r.value = sSave.chapter < (int)kMasters.size() ? "Chapter " + std::to_string(sSave.chapter + 1) : "Cleared";
        } else if (i == NPC_MASTERS) {
            r.value = std::to_string(sSave.chapter) + " freed";
            r.locked = sSave.chapter == 0;
        } else if (i == NPC_BOARD) {
            r.value = std::to_string(sSave.bounties) + " done";
        }
        r.ok = [i] {
            const Npc &n = kNpcs[i];
            if (i == NPC_QUEST) { go(S_QUEST); }
            else if (i == NPC_MASTERS) { go(S_MASTERS); }
            else if (i == NPC_SHOP) { talk({{n.chara, n.name, "Hahaha! Welcome to the shop of the World Champion! Everything here was personally tested by yours truly!"}}, [] { go(S_SHOP); }); }
            else if (i == NPC_BOARD) { go(S_BOARD); }
            else if (i == NPC_WARDROBE) { go(S_WARDROBE); }
            else { go(S_MENU); }
        };
        rows.push_back(r);
    }
    int cur = draw_rows(rows, sLists[S_HQ], a, b, input);
    player_card(P(40, 536), P(560, 640));
    int who = cur == NPC_MASTERS ? (sSave.chapter > 0 ? kMasters[(size_t)std::max(0, sSave.chapter - 1)].chara : -1) : kNpcs[cur].chara;
    want_model(cur == NPC_MENU ? fighter_chara() : who, 0);
    tip_box(cur == NPC_MASTERS && sSave.chapter == 0 ? "No masters yet. Free a corrupted fighter in the Main Quest first." : kNpcs[cur].hello, who >= 0 ? who : fighter_chara());
    prompts({{B_CROSS, "Talk"}, {B_START, "Patrol Menu"}, {B_CIRCLE, "Title"}});
    if (input && sIn.start) { sound(1); go(S_MENU); }
    else if (input && sIn.back) { sound(2); save_write(); reset_to(S_TITLE); }
}
void scr_quest(bool input) {
    heading("Main Quest", "SUPREME KAI");
    ImVec2 a = P(40, 150), b = P(660, 640);
    panel(a, b);
    static std::vector<Row> rows;
    rows.clear();
    for (size_t m = 0; m < kMasters.size(); m++) {
        const Master &ms = kMasters[m];
        Row r;
        bool done = sSave.chapter > (int)m, next = sSave.chapter == (int)m;
        r.label = "Chapter " + std::to_string(m + 1) + ": " + ms.name + " Corrupted";
        r.sub = std::string("In ") + ms.place;
        r.face = done || next ? ms.chara : -1;
        r.value = done ? "CLEARED" : next ? "NEXT" : "LOCKED";
        r.locked = !done && !next;
        r.model = done || next ? ms.corruptChara : -1;
        int mi = (int)m;
        r.ok = [mi] {
            const Master &ms = kMasters[(size_t)mi];
            const Npc &k = kNpcs[NPC_QUEST];
            talk({{k.chara, k.name, ms.brief}, {k.chara, k.name, "Be careful. The corruption makes them far stronger than they should be."}}, [mi] {
                ask(std::string("Go to ") + kMasters[(size_t)mi].place + " and face " + kMasters[(size_t)mi].name + "?", [mi] { play_chapter(mi); });
            });
        };
        rows.push_back(r);
    }
    int cur = draw_rows(rows, sLists[S_QUEST], a, b, input && !sAsking);
    want_model(rows[(size_t)cur].model, 0);
    tip_box(rows[(size_t)cur].locked ? "Clear the chapters before it first." : kMasters[(size_t)cur].brief, 58);
    prompts({{B_CROSS, "Start"}, {B_CIRCLE, "Back"}});
    if (input && !sAsking && sIn.back) { sound(2); back(); }
}
int sMasterCur;
void scr_masters(bool input) {
    heading("Training Grounds", "YOUR MASTERS");
    ImVec2 a = P(40, 150), b = P(660, 640);
    panel(a, b);
    static std::vector<Row> rows;
    rows.clear();
    for (size_t m = 0; m < kMasters.size(); m++) {
        const Master &ms = kMasters[m];
        Row r;
        bool on = master_unlocked((int)m);
        r.label = on ? ms.name : "???";
        r.sub = on ? (sSave.masterWins[m] >= kLessons ? "Training complete" : "Next: " + move_name(ms.chara, sSave.masterWins[m])) : "Free them in the Main Quest";
        r.face = on ? ms.chara : -1;
        r.value = on ? std::to_string(sSave.masterWins[m]) + " / " + std::to_string(kLessons) : "";
        r.locked = !on;
        r.model = on ? ms.chara : -1;
        int mi = (int)m;
        r.ok = [mi] { sMasterCur = mi; go(S_MASTER); };
        rows.push_back(r);
    }
    int cur = draw_rows(rows, sLists[S_MASTERS], a, b, input);
    want_model(rows[(size_t)cur].model, 0);
    tip_box(master_unlocked(cur) ? kMasters[(size_t)cur].greet : "This master is still corrupted.", master_unlocked(cur) ? kMasters[(size_t)cur].chara : 58);
    prompts({{B_CROSS, "Train"}, {B_CIRCLE, "Back"}});
    if (input && sIn.back) { sound(2); back(); }
}
void scr_master(bool input) {
    const Master &ms = kMasters[(size_t)sMasterCur];
    heading(ms.name, "TRAINING");
    ImVec2 a = P(40, 150), b = P(660, 640);
    panel(a, b);
    static std::vector<Row> rows;
    rows.clear();
    Row s;
    s.sec = true;
    s.label = "LESSONS";
    rows.push_back(s);
    int wins = sSave.masterWins[sMasterCur];
    for (int k = 0; k < kLessons; k++) {
        Row r;
        r.label = move_name(ms.chara, k);
        r.sub = std::string(kSlotKinds[k]) + (k == kLessons - 1 ? "  +  fighting style and outfit" : "");
        r.value = k < wins ? "LEARNT" : k == wins ? "TRAIN" : "LOCKED";
        r.locked = k > wins;
        int kk = k;
        r.ok = [kk] {
            const Master &ms = kMasters[(size_t)sMasterCur];
            ask(std::string("Train with ") + ms.name + "? Lesson " + std::to_string(kk + 1) + (kk < sSave.masterWins[sMasterCur] ? " (again, for experience)" : ""), [kk] { play_training(sMasterCur, kk); });
        };
        rows.push_back(r);
    }
    draw_rows(rows, sLists[S_MASTER], a, b, input && !sAsking);
    want_model(ms.chara, 0);
    tip_box(wins >= kLessons ? ms.done : ms.greet, ms.chara);
    prompts({{B_CROSS, "Train"}, {B_CIRCLE, "Back"}});
    if (input && !sAsking && sIn.back) { sound(2); back(); }
}
void scr_shop(bool input) {
    heading("Hercule's Shop", std::to_string(sSave.zeni) + " ZENI");
    ImVec2 a = P(40, 150), b = P(660, 640);
    panel(a, b);
    static std::vector<Row> rows;
    rows.clear();
    for (int i = 0; i < kShopCount; i++) {
        const ShopItem &it = kShop[i];
        Row r;
        r.label = it.name;
        r.sub = it.desc;
        bool have = owns(it.item);
        r.locked = sSave.level < it.level;
        r.value = have ? "OWNED" : r.locked ? "LV " + std::to_string(it.level) : std::to_string(it.price) + " z";
        r.ok = [i] {
            const ShopItem &it = kShop[i];
            if (owns(it.item)) { notice("You already own that. Equip it in the Patrol Menu."); return; }
            if (sSave.zeni < it.price) { notice("Not enough zeni."); return; }
            ask(std::string("Buy ") + it.name + " for " + std::to_string(it.price) + " zeni?", [i] {
                const ShopItem &it = kShop[i];
                sSave.zeni -= it.price;
                sSave.owned.push_back(it.item);
                if ((int)sSave.equipped.size() < kEquipMax) { sSave.equipped.push_back(it.item); notice(std::string(it.name) + " bought and equipped."); }
                else { notice(std::string(it.name) + " bought. Equip it in the Patrol Menu."); }
                save_write();
            });
        };
        rows.push_back(r);
    }
    int cur = draw_rows(rows, sLists[S_SHOP], a, b, input && !sAsking);
    want_model(55, 0);
    tip_box(rows[(size_t)cur].locked ? "Hahaha, that one's for real fighters! Come back at level " + std::to_string(kShop[cur].level) + "." : std::string(kShop[cur].name) + ": " + kShop[cur].desc + ". You can wear up to 3.", 55);
    prompts({{B_CROSS, "Buy"}, {B_CIRCLE, "Back"}});
    if (input && !sAsking && sIn.back) { sound(2); back(); }
}
void scr_board(bool input) {
    heading("Bounty Board", "KRILLIN");
    ImVec2 a = P(40, 150), b = P(660, 640);
    panel(a, b);
    static std::vector<Row> rows;
    rows.clear();
    int t = bounty_tier();
    static const char *const kTierNames[] = {"Saiyan scouts and soldiers", "Frieza's elite", "Rogue warriors", "Galactic threats", "Legendary monsters"};
    Row r;
    r.label = "Take a bounty";
    r.sub = std::string("Targets: ") + kTierNames[t];
    r.value = std::to_string(150 + t * 150) + " z";
    r.ok = [] { ask("Take the next bounty? A random corrupted target, at your strength.", [] { play_bounty(); }); };
    rows.push_back(r);
    Row s;
    s.sec = true;
    s.label = "RECORD";
    rows.push_back(s);
    Row d;
    d.label = "Bounties cleared";
    d.value = std::to_string(sSave.bounties);
    rows.push_back(d);
    draw_rows(rows, sLists[S_BOARD], a, b, input && !sAsking);
    want_model(25, 0);
    tip_box("Bounties get tougher as you level up. Good way to earn zeni for gear!", 25);
    prompts({{B_CROSS, "Choose"}, {B_CIRCLE, "Back"}});
    if (input && !sAsking && sIn.back) { sound(2); back(); }
}
void scr_wardrobe(bool input) {
    heading("Wardrobe", "VIDEL");
    ImVec2 a = P(40, 150), b = P(660, 640);
    panel(a, b);
    static std::vector<Row> rows;
    rows.clear();
    Row s;
    s.sec = true;
    s.label = "UNLOCKED OUTFITS";
    rows.push_back(s);
    Row r;
    r.label = std::string(kRaces[sSave.race].name) + " outfits";
    r.sub = "Your race's clothes";
    r.value = "OWNED";
    rows.push_back(r);
    for (size_t m = 0; m < kMasters.size(); m++) {
        if (sSave.masterWins[m] < kLessons) { continue; }
        Row o;
        o.label = std::string(kMasters[m].name) + "'s outfit";
        o.sub = "From completing their training";
        o.face = kMasters[m].chara;
        o.value = "OWNED";
        rows.push_back(o);
    }
    draw_rows(rows, sLists[S_WARDROBE], a, b, input);
    want_model(fighter_chara(), 0);
    tip_box(sSave.fighter.empty() ? "Your warrior still fights as their race's stand-in. Their own look is made in the Custom Character screen's Patrol mode."
                                  : "Change your look in the Custom Character screen's Patrol mode.", 56);
    prompts({{B_CIRCLE, "Back"}});
    if (input && sIn.back) { sound(2); back(); }
}
void scr_menu(bool input) {
    heading("Patrol Menu", sSave.name);
    ImVec2 a = P(40, 150), b = P(560, 520);
    panel(a, b);
    static std::vector<Row> rows;
    rows.clear();
    auto add = [&](const char *label, const char *sub, std::function<void()> ok) { Row r; r.label = label; r.sub = sub; r.ok = ok; rows.push_back(r); };
    add("Stats", "Level, health, attack, defense", [] { go(S_STATS); });
    add("Accessories", "Wear up to 3 of the items you own", [] { go(S_GEAR); });
    add("Techniques", "What your masters have taught you", [] { go(S_SKILLS); });
    add("Customize", "Your warrior's look", [] { go(S_WARDROBE); });
    add("Quest Log", "The story so far and your masters", [] { go(S_LOG); });
    add("Save", "Write your progress", [] { if (save_write()) { notice("Saved."); } else { notice("Could not save."); } });
    add("Return to Title", "Saves first", [] { save_write(); reset_to(S_TITLE); });
    draw_rows(rows, sLists[S_MENU], a, b, input);
    player_card(P(40, 536), P(560, 640));
    want_model(fighter_chara(), 0);
    tip_box("Everything about your warrior. Progress is saved after every battle too.", fighter_chara());
    prompts({{B_CROSS, "Choose"}, {B_CIRCLE, "Back"}});
    if (input && (sIn.back || sIn.start)) { sound(2); back(); }
}
void scr_stats(bool input) {
    heading("Stats", sSave.name);
    ImVec2 a = P(40, 150), b = P(660, 640);
    panel(a, b);
    std::vector<int> items = player_items();
    int st[4] = {0, 0, 0, 0};
    for (int it : items) {
        if (it >= 1 && it <= 12) { st[(it - 1) / 3] += 5 * ((it - 1) % 3 + 1); }
        if (it == 13) { st[0] += 10; st[1] -= 5; }
    }
    float y = a.y + S(24);
    ImVec2 at(a.x + S(24), y);
    auto line = [&](const char *l, const std::string &v) { stat_line(ImVec2(at.x, y), l, v); y += S(34); };
    line("Race", kRaces[sSave.race].name);
    line("Level", std::to_string(sSave.level) + (sSave.level >= kLevelMax ? " (max)" : "  (" + std::to_string(sSave.xp) + " / " + std::to_string(xp_to_next(sSave.level)) + " exp)"));
    line("Health bars", std::to_string(player_bars()));
    line("Blast stocks", "3");
    line("Attack", "+" + std::to_string(st[0]));
    line("Defense", (st[1] >= 0 ? "+" : "") + std::to_string(st[1]));
    line("Ki Blast", "+" + std::to_string(st[2]));
    line("Ki", "+" + std::to_string(st[3]));
    line("Zeni", std::to_string(sSave.zeni));
    line("Battles", std::to_string(sSave.wins) + " won, " + std::to_string(sSave.losses) + " lost");
    line("Masters freed", std::to_string(sSave.chapter) + " of " + std::to_string(kMasters.size()));
    want_model(fighter_chara(), 0);
    tip_box("Your attack, defense, ki blast and ki rise with your level. Accessories add to them.", fighter_chara());
    prompts({{B_CIRCLE, "Back"}});
    if (input && sIn.back) { sound(2); back(); }
}
void scr_gear(bool input) {
    heading("Accessories", std::to_string(sSave.equipped.size()) + " OF " + std::to_string(kEquipMax) + " WORN");
    ImVec2 a = P(40, 150), b = P(660, 640);
    panel(a, b);
    static std::vector<Row> rows;
    rows.clear();
    for (int i = 0; i < kShopCount; i++) {
        const ShopItem &it = kShop[i];
        if (!owns(it.item)) { continue; }
        Row r;
        r.label = it.name;
        r.sub = it.desc;
        r.value = equipped(it.item) ? "WORN" : "";
        r.ok = [i] {
            int id = kShop[i].item;
            auto at = std::find(sSave.equipped.begin(), sSave.equipped.end(), id);
            if (at != sSave.equipped.end()) { sSave.equipped.erase(at); }
            else if ((int)sSave.equipped.size() >= kEquipMax) { notice("You can wear 3 at most. Take one off first."); return; }
            else { sSave.equipped.push_back(id); }
            save_write();
        };
        rows.push_back(r);
    }
    if (rows.empty()) {
        Row r;
        r.label = "Nothing yet";
        r.sub = "Buy accessories at Hercule's Shop";
        r.locked = true;
        rows.push_back(r);
    }
    draw_rows(rows, sLists[S_GEAR], a, b, input);
    want_model(fighter_chara(), 0);
    tip_box("Cross puts an accessory on or takes it off.", 55);
    prompts({{B_CROSS, "Wear / remove"}, {B_CIRCLE, "Back"}});
    if (input && sIn.back) { sound(2); back(); }
}
void scr_skills(bool input) {
    heading("Techniques", "LEARNT FROM YOUR MASTERS");
    ImVec2 a = P(40, 150), b = P(660, 640);
    panel(a, b);
    static std::vector<Row> rows;
    rows.clear();
    Row s;
    s.sec = true;
    s.label = "STARTING TECHNIQUES";
    rows.push_back(s);
    const struct { int chara, slot; } kStart[3] = {{73, 2}, {17, 3}, {31, 4}};
    for (const auto &st : kStart) {
        Row r;
        r.label = move_name(st.chara, st.slot);
        r.sub = kSlotKinds[st.slot];
        r.value = "KNOWN";
        rows.push_back(r);
    }
    for (size_t m = 0; m < kMasters.size(); m++) {
        if (sSave.masterWins[m] == 0) { continue; }
        Row h;
        h.sec = true;
        h.label = std::string("FROM ") + kMasters[m].name;
        for (char &c : h.label) { c = (char)SDL_toupper((unsigned char)c); }
        rows.push_back(h);
        for (int k = 0; k < sSave.masterWins[m]; k++) {
            Row r;
            r.label = move_name(kMasters[m].chara, k);
            r.sub = kSlotKinds[k];
            r.face = kMasters[m].chara;
            std::string slotKey = kSlotKeys[k];
            const J &cur = sSave.loadout[slotKey.c_str()];
            bool set = (int)cur["chara"].number(-1) == kMasters[m].chara && (int)cur["slot"].number(-1) == k;
            r.value = set ? "SET" : "";
            int mi = (int)m, kk = k;
            r.ok = [mi, kk] {
                J ref = J::obj();
                ref.set("chara", J::num(kMasters[(size_t)mi].chara));
                ref.set("slot", J::num(kk));
                if (!sSave.loadout.isObj()) { sSave.loadout = J::obj(); }
                sSave.loadout.set(kSlotKeys[kk], ref);
                save_write();
                notice(move_name(kMasters[(size_t)mi].chara, kk) + " set as your " + kSlotKinds[kk] + ".");
            };
            rows.push_back(r);
        }
    }
    draw_rows(rows, sLists[S_SKILLS], a, b, input);
    want_model(fighter_chara(), 0);
    tip_box("Cross sets a technique in its slot. Your warrior fights with it once their look is rebuilt in the Custom Character screen's Patrol mode.", fighter_chara());
    prompts({{B_CROSS, "Set"}, {B_CIRCLE, "Back"}});
    if (input && sIn.back) { sound(2); back(); }
}
void scr_log(bool input) {
    heading("Quest Log", "THE STORY SO FAR");
    ImVec2 a = P(40, 150), b = P(660, 640);
    panel(a, b);
    static std::vector<Row> rows;
    rows.clear();
    for (size_t m = 0; m < kMasters.size(); m++) {
        const Master &ms = kMasters[m];
        Row r;
        bool done = sSave.chapter > (int)m;
        r.label = "Chapter " + std::to_string(m + 1) + ": " + (done || sSave.chapter == (int)m ? ms.name : std::string("???"));
        r.sub = done ? std::string("Freed. Training ") + std::to_string(sSave.masterWins[m]) + " / " + std::to_string(kLessons) : sSave.chapter == (int)m ? "Current mission" : "Not yet";
        r.value = done ? "CLEARED" : "";
        r.face = done || sSave.chapter == (int)m ? ms.chara : -1;
        r.locked = !done && sSave.chapter != (int)m;
        rows.push_back(r);
    }
    int cur = draw_rows(rows, sLists[S_LOG], a, b, input);
    want_model(rows[(size_t)cur].locked ? -1 : kMasters[(size_t)cur].chara, 0);
    tip_box(rows[(size_t)cur].locked ? "Clear the missions before it to find out more." : kMasters[(size_t)cur].brief, 58);
    prompts({{B_CIRCLE, "Back"}});
    if (input && sIn.back) { sound(2); back(); }
}
void scr_talk(bool input) {
    if (sTalkAt >= sTalk.size()) { back(); if (sTalkThen) { auto f = sTalkThen; sTalkThen = nullptr; f(); } return; }
    const Say &s = sTalk[sTalkAt];
    heading(s.name, "");
    ImVec2 a = P(40, 520), b = P(1000, 650);
    panel(a, b);
    face_at(s.chara, ImVec2(a.x + S(16), a.y + S(16)), ImVec2(a.x + S(114), a.y + S(114)), S(10));
    text(sBold, S(22), ImVec2(a.x + S(134), a.y + S(14)), kGold, s.name);
    wrapped(sFont, S(19), ImVec2(a.x + S(134), a.y + S(46)), kInk, s.text, b.x - a.x - S(160));
    want_model(s.chara, 0);
    prompts({{B_CROSS, sTalkAt + 1 < sTalk.size() ? "Next" : "OK"}});
    if (input && (sIn.ok || sIn.back)) { sound(1); sTalkAt++; }
}
void scr_result(bool input) {
    heading(sResultTitle, sSave.name);
    ImVec2 a = P(40, 150), b = P(660, 560);
    panel(a, b);
    float y = a.y + S(24);
    for (const std::string &l : sResultLines) {
        wrapped(l.compare(0, 8, "LEVEL UP") == 0 ? sBold : sFont, S(20), ImVec2(a.x + S(24), y), l.compare(0, 8, "LEVEL UP") == 0 ? kGold : kInk, l, b.x - a.x - S(48));
        y += S(40);
    }
    player_card(P(40, 576), P(660, 680 - 40));
    want_model(fighter_chara(), 0);
    prompts({{B_CROSS, "Continue"}});
    if (input && (sIn.ok || sIn.back)) {
        sound(1);
        back();
        if (sFightKind == FIGHT_CHAPTER && sSave.chapter == (int)kMasters.size() && sResultTitle == "VICTORY" && sFightA == (int)kMasters.size() - 1) {
            const Npc &k = kNpcs[NPC_QUEST];
            talk({{k.chara, k.name, "You did it. The rift is sealed and the corruption is gone."},
                  {k.chara, k.name, "Every master you saved is training at the Lookout. Keep growing, " + sSave.name + ". The Patrol will always need you."}}, nullptr);
        }
    }
}

void ask_draw(bool input) {
    if (!sAsking) { return; }
    sDl->AddRectFilled(P(0, 0), P(1376, 768), IM_COL32(0, 0, 0, 120));
    ImVec2 a = P(388, 280), b = P(988, 470);
    panel(a, b);
    wrapped(sBold, S(21), ImVec2(a.x + S(24), a.y + S(24)), kInk, sAsk, b.x - a.x - S(48));
    const char *opts[2] = {"Yes", "No"};
    for (int i = 0; i < 2; i++) {
        ImVec2 oa(a.x + S(60) + i * S(260), b.y - S(70)), ob(oa.x + S(220), oa.y + S(46));
        if (i == sAskCur) { grad(oa, ob, kVio1, kVio2, S(10)); } else { sDl->AddRectFilled(oa, ob, IM_COL32(255, 255, 255, 20), S(10)); }
        text(sBold, S(20), ImVec2((oa.x + ob.x) * 0.5f - text_w(sBold, S(20), opts[i]) * 0.5f, oa.y + S(12)), kInk, opts[i]);
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
void on_open(void) {
    sSaveDir = save_dir();
    int gen;
    SDL_LockMutex(sMx);
    gen = sSh.resultGen;
    int result = sSh.result;
    SDL_UnlockMutex(sMx);
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
    if (sSave.exists && getenv("BT3_PATROL_CONTINUE") != NULL) { reset_to(S_HQ); } // (testing: straight to the headquarters)
}

} // namespace

// =============================================================================================== the game's half (src/menu/port_patrol.c)
extern "C" void PortPatrolUi_Open(void) {
    if (sMx == NULL) { sMx = SDL_CreateMutex(); }
    SDL_LockMutex(sMx);
    sSh.leave = 0;
    sSh.sounds = 0;
    sSh.turn = 0;
    sSh.voice = -1;
    sSh.key = -1;
    sOpenGen++;
    sActive = 1;
    SDL_UnlockMutex(sMx);
}
extern "C" void PortPatrolUi_Close(void) {
    fprintf(stderr, "bt3: patrol: screens closed\n");
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
// (the game's thread, as the screens are left for a battle) the battle made into its script; 0 if it could not be
extern "C" int PortPatrolUi_Prepare(void) {
    if (sDoc.empty() || !PortStoryUi_PrepareDoc(sDoc.c_str(), 0)) {
        SDL_LockMutex(sMx);
        sSh.leave = 0;
        SDL_UnlockMutex(sMx);
        notice("That battle could not be made.");
        sFightKind = FIGHT_NONE;
        return 0;
    }
    fprintf(stderr, "bt3: patrol: battle (kind %d, %d, %d)\n", sFightKind, sFightA, sFightB);
    return 1;
}
extern "C" void PortPatrolUi_Result(int result) {
    PortStoryUi_EndDoc();
    if (sMx == NULL) { sMx = SDL_CreateMutex(); }
    SDL_LockMutex(sMx);
    sSh.result = result;
    sSh.resultGen++;
    SDL_UnlockMutex(sMx);
    fprintf(stderr, "bt3: patrol: the battle ended: %s\n", result == 1 ? "won" : result == 2 ? "lost" : "left");
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
    read_input(input);
    bool live = input && !sAsking;
    switch (sScr) {
    case S_TITLE: scr_title(live); break;
    case S_RACE: scr_race(live); break;
    case S_NAME: scr_name(live); break;
    case S_HQ: scr_hq(live); break;
    case S_QUEST: scr_quest(live); break;
    case S_MASTERS: scr_masters(live); break;
    case S_MASTER: scr_master(live); break;
    case S_SHOP: scr_shop(live); break;
    case S_BOARD: scr_board(live); break;
    case S_MENU: scr_menu(live); break;
    case S_STATS: scr_stats(live); break;
    case S_GEAR: scr_gear(live); break;
    case S_SKILLS: scr_skills(live); break;
    case S_LOG: scr_log(live); break;
    case S_TALK: scr_talk(live); break;
    case S_RESULT: scr_result(live); break;
    case S_WARDROBE: scr_wardrobe(live); break;
    }
    if (input && sIn.turn != 0.0f) {
        SDL_LockMutex(sMx);
        sSh.turn += (int)(sIn.turn * 40.0f);
        SDL_UnlockMutex(sMx);
    }
    ask_draw(input);
    notice_draw();
    return true;
}

// The Story Creator's fight pages (included by story_ui.cpp inside its unnamed namespace, after story_tools.h): an
// episode's fight rules, and what happens during the fight: the events, each a "when" and a list of things it does.

// =============================================================================================== the fight's rules
// a number row: label, the value as shown, left / right steps it between lo and hi
Row num_row(const std::string &label, const std::string &value, const std::string &tip, std::function<void(int)> lr) {
    Row r;
    r.label = label;
    r.value = value;
    r.tip = tip;
    r.lr = lr;
    return r;
}
Row yes_row(int ep, const char *key, const std::string &label, const std::string &tip) {
    bool on = episode(ep)["battle"][key].truthy();
    std::string k = key;
    Row r;
    r.label = label;
    r.value = on ? "Yes" : "No";
    r.tip = tip;
    r.lr = [ep, k](int) { J &b = episode(ep).at("battle"); if (b[k.c_str()].truthy()) { b.erase(k.c_str()); } else { b.set(k.c_str(), J::boolean(true)); } touch(); };
    r.ok = [r] { r.lr(1); };
    return r;
}
// a side's things at the start of the fight (s 0 yours, 1 theirs)
void side_rows(std::vector<Row> &rows, int ep, int s) {
    J &bt = episode(ep).at("battle");
    const char *pre = s ? "enemy_" : "player_";
    auto key = [pre](const char *k) { return std::string(pre) + k; };
    std::string hk = key("health"), kk = key("ki"), bk = key("blast"), mk = key("max"), fk = key("form"), bo = key("boost");
    rows.push_back(num_row("Health at the start", std::to_string((int)bt[hk.c_str()].number(100)) + "%",
                           "Every fighter of the side starts with this much (a team member's own health is set on the episode page).",
                           [ep, hk](int d) { cycle(episode(ep).at("battle"), hk.c_str(), d, 10, 100, 10, 100); }));
    rows.push_back(num_row("Ki at the start", std::to_string((int)bt[kk.c_str()].number(0)) + "%", "How full the ki gauge is when the fight starts.",
                           [ep, kk](int d) { cycle(episode(ep).at("battle"), kk.c_str(), d, 0, 100, 25, 0); }));
    rows.push_back(num_row("Blast stock at the start", bt[bk.c_str()].isNum() ? std::to_string((int)bt[bk.c_str()].n) : std::string("As usual"),
                           "Blast stock bars to start with (7 is full).",
                           [ep, bk](int d) {
                               J &b = episode(ep).at("battle");
                               int v = b[bk.c_str()].isNum() ? (int)b[bk.c_str()].n + d : d > 0 ? 0 : 7;
                               if (v < 0 || v > 7) { b.erase(bk.c_str()); } else { b.set(bk.c_str(), J::num(v)); }
                               touch();
                           }));
    rows.push_back(yes_row(ep, mk.c_str(), "Start at MAX power", "The side starts the fight powered up to its maximum."));
    static const char *const kForms[4] = {"No", "Once (first form)", "Twice", "Three times"};
    rows.push_back(num_row("Start transformed", kForms[clampi((int)bt[fk.c_str()].number(0), 0, 3)],
                           "The fighter transforms as soon as the fight starts (as far as its forms go).",
                           [ep, fk](int d) { cycle(episode(ep).at("battle"), fk.c_str(), d, 0, 3, 1, 0); }));
    for (const Boost &b : kBoosts) {
        int lv = clampi((int)bt[bo.c_str()][b.key].number(0), 0, b.max);
        std::string bk2 = b.key;
        int mx = b.max;
        rows.push_back(num_row(std::string(b.name) + " boost", lv == 0 ? std::string("None") : "Level " + std::to_string(lv),
                               std::string("A Z-item for the side's fighters: ") + b.name + (b.max == 2 ? " (one or two more bars)." : " up a level or more."),
                               [ep, bo, bk2, mx](int d) {
                                   J &x = episode(ep).at("battle").at(bo.c_str());
                                   int v = clampi((int)x[bk2.c_str()].number(0) + d, 0, mx);
                                   if (v == 0) { x.erase(bk2.c_str()); } else { x.set(bk2.c_str(), J::num(v)); }
                                   if (x.size() == 0) { episode(ep).at("battle").erase(bo.c_str()); }
                                   touch();
                               }));
    }
}
void rows_fight(std::vector<Row> &rows, int ep) {
    J &bt = episode(ep).at("battle");
    rows.push_back(sec("TO WIN"));
    int win = key_index(kWinKeys, PS_WIN_COUNT, bt["win"].text("ko"), PS_WIN_KO);
    static const char *const kWinTips[PS_WIN_COUNT] = {"The usual way: the other side's whole team knocked out.",
                                                       "The player wins by still standing when the time runs out.",
                                                       "The player wins as soon as the opponent's health is down to a share.",
                                                       "The last blow has to be a blast (a super or ultimate move): a knock-out by anything else is a loss."};
    rows.push_back(num_row("To win", kWinNames[win], kWinTips[win], [ep](int d) {
        J &b = episode(ep).at("battle");
        int at = key_index(kWinKeys, PS_WIN_COUNT, b["win"].text("ko"), PS_WIN_KO);
        b.set("win", J::str(kWinKeys[(at + d + PS_WIN_COUNT) % PS_WIN_COUNT]));
        touch();
    }));
    if (win == PS_WIN_SURVIVE) {
        rows.push_back(num_row("Hold out for", clock_text((int)bt["survive"].number(60)), "How long the player has to last.",
                               [ep](int d) { cycle(episode(ep).at("battle"), "survive", d, 10, 600, 10, 60); }));
    }
    if (win == PS_WIN_HEALTH) {
        rows.push_back(num_row("Their health down to", std::to_string(clampi((int)bt["win_value"].number(50), 1, 99)) + "%",
                               "The fight is won when the opponent's health is this low.",
                               [ep](int d) { cycle(episode(ep).at("battle"), "win_value", d, 5, 90, 5, 50); }));
    }
    rows.push_back(sec("TO LOSE"));
    rows.push_back(yes_row(ep, "fail_member", "Lose if one of your team falls", "The fight is lost as soon as one of the player's team is knocked out."));
    int fh = clampi((int)bt["fail_health"].number(0), 0, 99);
    rows.push_back(num_row("Lose if your health drops to", fh == 0 ? std::string("Off") : std::to_string(fh) + "%",
                           "The fight is lost when the player's fighter has this little health left.",
                           [ep](int d) { cycle(episode(ep).at("battle"), "fail_health", d, 0, 90, 10, 0); }));
    rows.push_back(sec("THE CLOCK"));
    int tl = (int)bt["time"].number(0);
    rows.push_back(num_row("Time limit", tl == 0 ? std::string("None") : clock_text(tl), "The fight's clock; the player sees it count down.",
                           [ep](int d) { cycle(episode(ep).at("battle"), "time", d, 0, 600, 30, 0); }));
    if (tl > 0) {
        std::string tu = bt["time_up"].text("health");
        rows.push_back(num_row("When the time is up", tu == "win" ? "You win" : tu == "lose" ? "You lose" : "More health wins",
                               "What the end of the clock means.", [ep](int d) {
                                   static const char *const k[3] = {"health", "win", "lose"};
                                   J &b = episode(ep).at("battle");
                                   int at = key_index(k, 3, b["time_up"].text("health"), 0);
                                   b.set("time_up", J::str(k[(at + d + 3) % 3]));
                                   touch();
                               }));
    }
    rows.push_back(sec("THE OBJECTIVE"));
    {
        std::string auto_ = objective_text([&] { J b = bt; b.erase("objective"); return b; }());
        Row o = text_row("Objective", bt["objective"].text(), "THE OBJECTIVE (EMPTY: WRITTEN FROM THE RULES)", 120,
                         [ep](const std::string &t) { J &b = episode(ep).at("battle"); if (t.empty()) { b.erase("objective"); } else { b.set("objective", J::str(t)); } });
        if (!bt.has("objective")) { o.value = auto_; o.sub = "from the rules"; }
        o.tip = "Shown before the fight and on the result screen. Empty: written from the rules above.";
        rows.push_back(o);
        bool shown = objective_shown(bt);
        rows.push_back(num_row("Show it as the fight starts", bt.has("show_objective") ? (shown ? "Yes" : "No") : shown ? "Yes (the rules ask for it)" : "No (a plain fight)",
                               "A banner with the objective over the picture for a few seconds.", [ep](int) {
                                   J &b = episode(ep).at("battle");
                                   bool now = objective_shown(b);
                                   b.set("show_objective", J::boolean(!now));
                                   touch();
                               }));
    }
    rows.push_back(sec("THE OPPONENT"));
    rows.push_back(num_row("Strength", kCpuNames[clampi((int)bt["cpu"].number(1), 0, 4)], "How well the computer fights (the saga's difficulty moves it too).",
                           [ep](int d) { cycle(episode(ep).at("battle"), "cpu", d, 0, 4, 1, 1); }));
    {
        bool fine = bt.has("cpu_level");
        rows.push_back(num_row("Exact level", fine ? std::to_string(clampi((int)bt["cpu_level"].number(0), 0, 40)) + " of 40" : std::string("Off (the strength above)"),
                               "The computer's own scale, 0 to 40, for a finer choice than the five strengths.", [ep](int d) {
                                   J &b = episode(ep).at("battle");
                                   int v = b.has("cpu_level") ? (int)b["cpu_level"].number(0) + d * 2 : d > 0 ? 0 : 40;
                                   if (v < 0 || v > 40) { b.erase("cpu_level"); } else { b.set("cpu_level", J::num(v)); }
                                   touch();
                               }));
    }
    rows.push_back(sec("YOUR SIDE AT THE START"));
    side_rows(rows, ep, 0);
    rows.push_back(sec("THEIR SIDE AT THE START"));
    side_rows(rows, ep, 1);
}

// =============================================================================================== events
const char *const kWhenTips[PS_WHEN_COUNT] = {
    "As soon as the fight begins.", "When the opponent's health falls below a share.", "When your health falls below a share.",
    "When the opponent changes form.", "When your fighter changes form.", "So many seconds into the fight.",
    "When the opponent's next team member comes in.", "When your next team member comes in.", "When your ki gauge is full.",
    "When the opponent's ki gauge is full.", "When you go into MAX power.", "When the opponent goes into MAX power.",
    "When one of your team is knocked out.", "When one of their team is knocked out.", "Right after another event of this list has done all it does.",
    "When the clock shows so many seconds left (a time limit or a hold-out).", "When your combo reaches so many hits.",
    "When the opponent's combo reaches so many hits.", "When your blast stock reaches so many bars.", "When the opponent's blast stock reaches so many bars."};
// what an event's value is, for its "when" (an empty label: it has none)
struct WhenValue { const char *label; int lo, hi, step, def; const char *unit; };
WhenValue when_value(int w) {
    switch (w) {
    case PS_WHEN_ENEMY_HP: case PS_WHEN_PLAYER_HP: return {"Health below", 5, 95, 5, 50, "%"};
    case PS_WHEN_TIME: return {"Seconds into the fight", 1, 600, 1, 30, " s"};
    case PS_WHEN_TIME_LEFT: return {"Seconds left", 5, 300, 5, 10, " s"};
    case PS_WHEN_AFTER: return {"After event", 1, 96, 1, 1, ""};
    case PS_WHEN_PLAYER_COMBO: case PS_WHEN_ENEMY_COMBO: return {"Hits", 2, 60, 1, 5, ""};
    case PS_WHEN_PLAYER_BLAST: case PS_WHEN_ENEMY_BLAST: return {"Bars", 1, 7, 1, 3, ""};
    default: return {"", 0, 0, 0, 0, ""};
    }
}
std::string when_text(const J &ev) {
    int w = key_index(kWhenKeys, PS_WHEN_COUNT, ev["when"].text("start"), 0);
    WhenValue v = when_value(w);
    std::string t = kWhenNames[w], n = std::to_string((int)ev["value"].number(v.def));
    if (w == PS_WHEN_TIME) { t = "After " + n + " seconds"; }
    else if (w == PS_WHEN_TIME_LEFT) { t = n + " seconds left on the clock"; }
    else if (w == PS_WHEN_AFTER) { t = "After event " + n; }
    else if (w == PS_WHEN_PLAYER_COMBO || w == PS_WHEN_ENEMY_COMBO) { t += " " + n + " hits"; }
    else if (w == PS_WHEN_PLAYER_BLAST || w == PS_WHEN_ENEMY_BLAST) { t += " " + n + " bars"; }
    else if (v.label[0]) { t += " " + n + v.unit; }
    if (ev["repeat"].number(0) > 0) { t += " (again and again)"; }
    return t;
}
// what an action's value is, by its kind
struct DoValue { const char *label; int lo, hi, step, def; bool side; };
DoValue do_value(int kind) {
    switch (kind) {
    case PS_DO_HEAL: return {"How much (% of health)", 1, 100, 5, 25, true};
    case PS_DO_HURT: return {"How much (% of health)", 1, 100, 5, 25, true};
    case PS_DO_SET_HP: return {"Health", 1, 100, 5, 100, true};
    case PS_DO_KI: return {"Ki bars", -5, 5, 1, 5, true};
    case PS_DO_BLAST: return {"Blast stock bars", -7, 7, 1, 1, true};
    case PS_DO_MAX: return {"", 0, 0, 0, 0, true};
    case PS_DO_TRANSFORM: return {"Into form", 1, 4, 1, 1, true};
    case PS_DO_SWITCH: return {"Who comes in", 0, 5, 1, 0, true};
    case PS_DO_SOUND: return {"Sound number", 0, 400, 1, 0, false};
    case PS_DO_SHAKE: return {"Strength", 1, 50, 1, 6, false};
    case PS_DO_FLASH: return {"Colour", 0, 1, 1, 0, false};
    case PS_DO_AURA: return {"Aura", 0, 1, 1, 1, true};
    case PS_DO_CPU: return {"Opponent's level (of 40)", 0, 40, 2, 20, false};
    case PS_DO_TIME: return {"Seconds added", -300, 300, 5, 30, false};
    case PS_DO_WAIT: return {"Wait (seconds)", 1, 300, 5, 10, false};
    case PS_DO_USE_BLAST: return {"Which", 0, 2, 1, 2, true};
    default: return {"", 0, 0, 0, 0, false};
    }
}
std::string do_value_text(int kind, const J &a) {
    DoValue v = do_value(kind);
    int n = (int)a["value"].number(v.def);
    switch (kind) {
    case PS_DO_HEAL: case PS_DO_HURT: case PS_DO_SET_HP: return std::to_string(n) + "%";
    case PS_DO_KI: case PS_DO_BLAST: return (n > 0 ? "+" : "") + std::to_string(n);
    case PS_DO_TRANSFORM: return "Form " + std::to_string(clampi(n, 1, 4));
    case PS_DO_SWITCH: return n <= 0 ? std::string("The next one standing") : "Member " + std::to_string(n);
    case PS_DO_FLASH: return n ? "Red" : "White";
    case PS_DO_AURA: return a["value"].truthy(true) ? "On" : "Off";
    case PS_DO_WAIT: { char b[32]; snprintf(b, sizeof(b), "%.1f s", n / 10.0); return b; }
    case PS_DO_TIME: return (n > 0 ? "+" : "") + std::to_string(n) + " s";
    case PS_DO_USE_BLAST: { static const char *const k[3] = {"First blast", "Second blast", "Ultimate"}; return k[clampi(n, 0, 2)]; }
    case PS_DO_MUSIC: return a["value"].isNum() ? song_name((int)a["value"].n) : std::string("Stop the music");
    default: return v.label[0] ? std::to_string(n) : std::string();
    }
}
std::string action_text(const J &a) {
    int kind = key_index(kDoKeys, PS_DO_COUNT, a["do"].text("say"), PS_DO_SAY);
    switch (kind) {
    case PS_DO_SAY: case PS_DO_CARD: return "\"" + a["text"].text() + "\"";
    case PS_DO_SCENE: { int n = (int)a["lines"].size(); return std::to_string(n) + (n == 1 ? " line" : " lines"); }
    default: break;
    }
    std::string t = do_value_text(kind, a);
    if (do_value(kind).side) { t = std::string(a["side"].text("enemy") == "player" ? "you" : "them") + (t.empty() ? "" : ": " + t); }
    return t;
}
const char *const kDoTips[PS_DO_COUNT] = {
    "A line in a box at the bottom; the fight goes on.", "A scene: the fight stops, the fighters talk, then the fight goes on where it was.",
    "Gives health back.", "Takes health away (it never knocks out: the last blow is the player's).", "Sets the health to an amount.",
    "Fills (or drains) ki bars.", "Gives (or takes) blast stock bars.", "Powers the side up to MAX.", "The fighter changes form.",
    "Another member of the team comes in.", "Another song, or none.", "A sound effect of the battle.", "The stage is torn up.",
    "The screen shakes.", "A flash over the picture.", "The fighter's aura, on or off.", "The fight ends now, won.",
    "The fight ends now, lost.", "The opponent fights better or worse from now on.", "More (or less) time on the clock.",
    "The next action waits a moment.", "The fighter fires one of its blasts.", "Words across the picture (a title or a message); the fight goes on."};

J &events_of(int ep) { return episode(ep).at("battle").list("events"); }
// the events moved: map[old] = new place (-1 gone); "after another event" follows its event
void events_relink(int ep, const std::vector<int> &map) {
    for (J &e : events_of(ep).a) {
        if (e["when"].text() != "after_event") { continue; }
        int v = (int)e["value"].number(1) - 1;
        if (v >= 0 && v < (int)map.size()) {
            if (map[(size_t)v] < 0) { e.set("when", J::str("start")); e.erase("value"); }
            else { e.set("value", J::num(map[(size_t)v] + 1)); }
        }
    }
}
void event_swap(int ep, int a, int b) {
    J &l = events_of(ep);
    int n = (int)l.a.size();
    if (a < 0 || b < 0 || a >= n || b >= n) { return; }
    std::vector<int> map = same_places(n);
    map[(size_t)a] = b;
    map[(size_t)b] = a;
    events_relink(ep, map);
    std::swap(l.a[(size_t)a], l.a[(size_t)b]);
    touch();
}
void event_insert(int ep, int at, const J &e) {
    J &l = events_of(ep);
    int n = (int)l.a.size();
    at = std::max(0, std::min(at, n));
    std::vector<int> map = same_places(n);
    for (int i = at; i < n; i++) { map[(size_t)i] = i + 1; }
    events_relink(ep, map);
    l.a.insert(l.a.begin() + at, e);
    touch();
}
void event_delete(int ep, int at) {
    J &l = events_of(ep);
    int n = (int)l.a.size();
    if (at < 0 || at >= n) { return; }
    std::vector<int> map = same_places(n);
    map[(size_t)at] = -1;
    for (int i = at + 1; i < n; i++) { map[(size_t)i] = i - 1; }
    events_relink(ep, map);
    l.a.erase(l.a.begin() + at);
    touch();
}
// a new action of a kind, with what it needs to be seen at once
J new_action(int kind, int ep) {
    J a = J::obj();
    a.set("do", J::str(kDoKeys[kind]));
    DoValue v = do_value(kind);
    if (v.side) { a.set("side", J::str("enemy")); }
    if (v.label[0]) { a.set("value", J::num(v.def)); }
    if (kind == PS_DO_AURA) { a.set("value", J::boolean(true)); }
    if (kind == PS_DO_MUSIC) { a.set("value", J::num((int)episode(ep)["music"].number(8) == 12 ? 6 : 12)); }
    if (kind == PS_DO_SAY) { a.set("speaker", J::str("enemy")); a.set("text", J::str("")); }
    if (kind == PS_DO_CARD) { a.set("text", J::str("")); a.set("card", J::str("title")); }
    if (kind == PS_DO_SCENE) {
        a.set("distance", J::num(2));
        a.set("lines", lines({new_line("enemy", "Wait... something is different."), new_line("player", "What's going on?")}));
    }
    return a;
}
// opens what an action is edited on: a line's page, a scene's page, or its own
void open_action(int ep, int e, int a) {
    int kind = key_index(kDoKeys, PS_DO_COUNT, action_of(ep, e, a)["do"].text("say"), PS_DO_SAY);
    std::string k = std::to_string(e) + ":" + std::to_string(a);
    if (kind == PS_DO_SAY || kind == PS_DO_CARD) { push_page(SC_LINE, ep, "say:" + k, 0); }
    else if (kind == PS_DO_SCENE) { push_page(SC_SCENE, ep, "scene:" + k); }
    else { push_page(SC_ACTION, ep, "", e, a); }
}
// the list of kinds of action; picked(kind)
void open_kinds(const std::string &title, std::function<void(int)> picked) {
    std::vector<Row> rs;
    static const int kOrder[PS_DO_COUNT] = {PS_DO_SAY, PS_DO_SCENE, PS_DO_CARD, PS_DO_HEAL, PS_DO_HURT, PS_DO_SET_HP, PS_DO_KI,
                                            PS_DO_BLAST, PS_DO_MAX, PS_DO_TRANSFORM, PS_DO_SWITCH, PS_DO_USE_BLAST, PS_DO_AURA,
                                            PS_DO_MUSIC, PS_DO_SOUND, PS_DO_SHAKE, PS_DO_FLASH, PS_DO_STAGE, PS_DO_CPU, PS_DO_TIME,
                                            PS_DO_WAIT, PS_DO_WIN, PS_DO_LOSE};
    for (int i = 0; i < PS_DO_COUNT; i++) {
        int k = kOrder[i];
        Row r;
        r.label = kDoNames[k];
        r.tip = kDoTips[k];
        r.ok = [picked, k] { ov_close(); picked(k); };
        rs.push_back(r);
    }
    open_list(title, rs);
}
void open_whens(const std::string &title, int now, std::function<void(int)> picked) {
    std::vector<Row> rs;
    for (int w = 0; w < PS_WHEN_COUNT; w++) {
        Row r;
        r.label = kWhenNames[w];
        r.tip = kWhenTips[w];
        r.ok = [picked, w] { ov_close(); picked(w); };
        rs.push_back(r);
    }
    open_list(title, rs, std::max(0, now));
}
void add_action(int ep, int e, int at, int kind) {
    J &l = event_of(ep, e).list("do");
    at = std::max(0, std::min(at, (int)l.a.size()));
    l.a.insert(l.a.begin() + at, new_action(kind, ep));
    touch();
    open_action(ep, e, at);
    if (kind == PS_DO_SAY || kind == PS_DO_CARD) {
        std::string k = "say:" + std::to_string(e) + ":" + std::to_string(at);
        open_keys(kind == PS_DO_SAY ? "WHAT IS SAID?" : "THE MESSAGE", "", 240, [ep, k](const std::string &t) { line_ref(ep, k, 0).set("text", J::str(t)); });
    }
}

void rows_events(std::vector<Row> &rows, int ep) {
    J &evs = events_of(ep);
    rows.push_back(sec("WHAT HAPPENS DURING THE FIGHT"));
    for (size_t i = 0; i < evs.a.size(); i++) {
        const J &ev = evs.a[i];
        Row r;
        r.label = std::to_string(i + 1) + ". " + when_text(ev);
        std::string what;
        const J &acts = ev["do"];
        for (size_t k = 0; k < acts.size(); k++) {
            int kind = key_index(kDoKeys, PS_DO_COUNT, acts[k]["do"].text("say"), PS_DO_SAY);
            std::string n = kDoNames[kind];
            n = n.substr(0, n.find(" ("));
            what += (k ? ", " : "") + n;
        }
        r.value = what.empty() ? std::string("(does nothing yet)") : what;
        r.tip = kWhenTips[key_index(kWhenKeys, PS_WHEN_COUNT, ev["when"].text("start"), 0)];
        int e = (int)i;
        r.ok = [ep, e] { push_page(SC_EVENT, ep, "", e); };
        r.okLabel = "Open";
        r.sq = [ep, e] {
            std::vector<Row> rs;
            Row d;
            d.label = "Duplicate it";
            d.ok = [ep, e] { ov_close(); event_insert(ep, e + 1, events_of(ep).a[(size_t)e]); };
            rs.push_back(d);
            Row c;
            c.label = "Copy it";
            c.ok = [ep, e] { ov_close(); clip("event", events_of(ep).a[(size_t)e], "The event"); };
            rs.push_back(c);
            Row p;
            p.label = sClipKind == "event" ? "Paste the event copied after it" : "(no event copied)";
            p.locked = sClipKind != "event";
            p.ok = [ep, e] { ov_close(); event_insert(ep, e + 1, sClip); };
            rs.push_back(p);
            open_list("EVENT " + std::to_string(e + 1), rs);
        };
        r.sqLabel = "More";
        r.tri = [ep, e] { confirm("DELETE EVENT " + std::to_string(e + 1) + "?", [ep, e] { event_delete(ep, e); notice("Deleted. L1 undoes it."); }); };
        r.triLabel = "Delete";
        r.l2 = [ep, e] { if (e > 0) { event_swap(ep, e, e - 1); page().cur--; sound(0); } };
        r.r2 = [ep, e] { if (e + 1 < (int)events_of(ep).a.size()) { event_swap(ep, e, e + 1); page().cur++; sound(0); } };
        r.l2Label = "Move";
        rows.push_back(r);
    }
    Row add;
    add.label = "+ Add an event";
    add.tip = "First when it happens, then what it does (a cutscene, a line, healing, a transformation...).";
    add.ok = [ep] {
        open_whens("WHEN?", 0, [ep](int w) {
            J e = J::obj();
            e.set("when", J::str(kWhenKeys[w]));
            WhenValue v = when_value(w);
            if (v.label[0]) { e.set("value", J::num(w == PS_WHEN_AFTER ? std::max(1, (int)events_of(ep).a.size()) : v.def)); }
            e.set("do", J::arr());
            int at = (int)events_of(ep).a.size();
            event_insert(ep, at, e);
            push_page(SC_EVENT, ep, "", at);
            open_kinds("THEN WHAT HAPPENS?", [ep, at](int k) { add_action(ep, at, 0, k); });
        });
    };
    rows.push_back(add);
    if (sClipKind == "event") {
        Row p;
        p.label = "+ Paste the event copied";
        p.ok = [ep] { event_insert(ep, (int)events_of(ep).a.size(), sClip); };
        rows.push_back(p);
    }
    rows.push_back(sec("TRY IT"));
    Row play;
    play.label = "Play this episode";
    play.ok = [ep] { if (!sDirty || save_saga()) { sPlayMode = false; leave_for(sSaga, ep, ""); } };
    rows.push_back(play);
}

void rows_event(std::vector<Row> &rows, int ep, int e) {
    if (e < 0 || e >= (int)events_of(ep).a.size()) { rows.push_back(sec("THIS EVENT IS GONE: CIRCLE GOES BACK")); return; }
    J &ev = event_of(ep, e);
    int w = key_index(kWhenKeys, PS_WHEN_COUNT, ev["when"].text("start"), 0);
    rows.push_back(sec("EVENT " + std::to_string(e + 1) + ": WHEN"));
    Row wr;
    wr.label = "When";
    wr.value = kWhenNames[w];
    wr.tip = kWhenTips[w];
    wr.lr = [ep, e](int d) {
        J &x = event_of(ep, e);
        int at = key_index(kWhenKeys, PS_WHEN_COUNT, x["when"].text("start"), 0);
        at = (at + d + PS_WHEN_COUNT) % PS_WHEN_COUNT;
        x.set("when", J::str(kWhenKeys[at]));
        WhenValue v = when_value(at);
        if (v.label[0]) { x.set("value", J::num(clampi((int)x["value"].number(v.def), v.lo, v.hi))); } else { x.erase("value"); }
        touch();
    };
    wr.ok = [ep, e, w] {
        open_whens("WHEN?", w, [ep, e](int to) {
            J &x = event_of(ep, e);
            x.set("when", J::str(kWhenKeys[to]));
            WhenValue v = when_value(to);
            if (v.label[0]) { x.set("value", J::num(clampi((int)x["value"].number(v.def), v.lo, v.hi))); } else { x.erase("value"); }
            touch();
        });
    };
    rows.push_back(wr);
    WhenValue v = when_value(w);
    if (v.label[0]) {
        int val = (int)ev["value"].number(v.def);
        Row vr = num_row(v.label, std::to_string(val) + v.unit, kWhenTips[w], [ep, e, v](int d) { cycle(event_of(ep, e), "value", d, v.lo, v.hi, v.step, v.def); });
        if (w == PS_WHEN_AFTER) {
            const J &other = events_of(ep)[val - 1];
            vr.value = std::to_string(val) + ": " + (other.nul() ? std::string("(not there)") : when_text(other));
            vr.lr = [ep, e](int d) { cycle(event_of(ep, e), "value", d, 1, std::max(1, (int)events_of(ep).a.size()), 1, 1); };
        }
        rows.push_back(vr);
    }
    int rep = (int)ev["repeat"].number(0);
    rows.push_back(num_row("Happens", rep <= 0 ? std::string("Once") : "Again and again, " + std::to_string(rep) + " s apart",
                           "Once, or each time it is so again (no sooner than so many seconds after the last time).",
                           [ep, e](int d) { cycle(event_of(ep, e), "repeat", d, 0, 120, 5, 0); }));
    rows.push_back(sec("THEN, ONE AFTER THE OTHER"));
    J &acts = ev.list("do");
    for (size_t i = 0; i < acts.a.size(); i++) {
        const J &a = acts.a[i];
        int kind = key_index(kDoKeys, PS_DO_COUNT, a["do"].text("say"), PS_DO_SAY);
        Row r;
        r.label = kDoNames[kind];
        r.value = action_text(a);
        r.tip = kDoTips[kind];
        if (kind == PS_DO_SAY) { int c = speaker_chara(a, ep); r.face = c; r.model = c; }
        int k = (int)i;
        r.ok = [ep, e, k] { open_action(ep, e, k); };
        r.okLabel = kind == PS_DO_SAY || kind == PS_DO_CARD || kind == PS_DO_SCENE ? "Edit" : "Open";
        DoValue dv = do_value(kind);
        if (dv.label[0] || kind == PS_DO_MUSIC) {
            r.lr = [ep, e, k, kind, dv](int d) {
                J &x = action_of(ep, e, k);
                if (kind == PS_DO_MUSIC) {
                    std::vector<int> l = song_list();
                    int at = -1;
                    for (size_t i2 = 0; i2 < l.size(); i2++) { if (x["value"].isNum() && l[i2] == (int)x["value"].n) { at = (int)i2; } }
                    at += d;
                    if (at < 0 || at >= (int)l.size()) { x.erase("value"); } else { x.set("value", J::num(l[(size_t)at])); }
                    touch();
                } else if (kind == PS_DO_AURA) {
                    x.set("value", J::boolean(!x["value"].truthy(true)));
                    touch();
                } else {
                    cycle(x, "value", d, dv.lo, dv.hi, dv.step, dv.def);
                }
            };
        }
        r.sq = [ep, e, k] {
            std::vector<Row> rs;
            Row in;
            in.label = "Put another action after it...";
            in.ok = [ep, e, k] { ov_close(); open_kinds("WHAT HAPPENS?", [ep, e, k](int kind2) { add_action(ep, e, k + 1, kind2); }); };
            rs.push_back(in);
            Row d;
            d.label = "Duplicate it";
            d.ok = [ep, e, k] { ov_close(); J &l = event_of(ep, e).list("do"); l.a.insert(l.a.begin() + k + 1, l.a[(size_t)k]); touch(); };
            rs.push_back(d);
            Row c;
            c.label = "Copy it";
            c.ok = [ep, e, k] { ov_close(); clip("action", action_of(ep, e, k), "The action"); };
            rs.push_back(c);
            Row p;
            p.label = sClipKind == "action" ? "Paste the action copied after it" : "(no action copied)";
            p.locked = sClipKind != "action";
            p.ok = [ep, e, k] { ov_close(); J &l = event_of(ep, e).list("do"); l.a.insert(l.a.begin() + k + 1, sClip); touch(); };
            rs.push_back(p);
            if (key_index(kDoKeys, PS_DO_COUNT, action_of(ep, e, k)["do"].text("say"), PS_DO_SAY) == PS_DO_SCENE) {
                Row pv;
                pv.label = "Preview the cutscene";
                pv.ok = [ep, e, k] { ov_close(); if (!sDirty || save_saga()) { sPlayMode = false; leave_for(sSaga, ep, "event:" + std::to_string(e) + ":" + std::to_string(k)); } };
                rs.push_back(pv);
            }
            open_list("ACTION " + std::to_string(k + 1), rs);
        };
        r.sqLabel = "More";
        r.tri = [ep, e, k] { confirm("DELETE THIS ACTION?", [ep, e, k] { J &l = event_of(ep, e).list("do"); l.a.erase(l.a.begin() + k); touch(); notice("Deleted. L1 undoes it."); }); };
        r.triLabel = "Delete";
        r.l2 = [ep, e, k] { J &l = event_of(ep, e).list("do"); if (k > 0) { std::swap(l.a[(size_t)k], l.a[(size_t)k - 1]); page().cur--; touch(); sound(0); } };
        r.r2 = [ep, e, k] { J &l = event_of(ep, e).list("do"); if (k + 1 < (int)l.a.size()) { std::swap(l.a[(size_t)k], l.a[(size_t)k + 1]); page().cur++; touch(); sound(0); } };
        r.l2Label = "Move";
        rows.push_back(r);
    }
    Row add;
    add.label = "+ Add an action";
    add.tip = "Actions are done in order; \"Wait\" puts a pause between two of them.";
    add.ok = [ep, e] { open_kinds("WHAT HAPPENS?", [ep, e](int k) { add_action(ep, e, (int)event_of(ep, e).list("do").a.size(), k); }); };
    rows.push_back(add);
}

void rows_action(std::vector<Row> &rows, int ep, int e, int k) {
    J &acts = event_of(ep, e).list("do");
    if (k < 0 || k >= (int)acts.a.size()) { rows.push_back(sec("THIS ACTION IS GONE: CIRCLE GOES BACK")); return; }
    J &a = acts.a[(size_t)k];
    int kind = key_index(kDoKeys, PS_DO_COUNT, a["do"].text("say"), PS_DO_SAY);
    rows.push_back(sec("EVENT " + std::to_string(e + 1) + ", ACTION " + std::to_string(k + 1)));
    Row kr;
    kr.label = "What happens";
    kr.value = kDoNames[kind];
    kr.tip = kDoTips[kind];
    kr.ok = [ep, e, k] {
        open_kinds("WHAT HAPPENS?", [ep, e, k](int to) {
            J &x = action_of(ep, e, k);
            J n = new_action(to, ep);
            if (x.has("side") && n.has("side")) { n.set("side", x["side"]); }
            x = n;
            touch();
            if (to == PS_DO_SAY || to == PS_DO_CARD || to == PS_DO_SCENE) { sPages.pop_back(); open_action(ep, e, k); }
        });
    };
    kr.okLabel = "Change";
    rows.push_back(kr);
    DoValue dv = do_value(kind);
    if (dv.side) {
        bool you = a["side"].text("enemy") == "player";
        rows.push_back(num_row("To whom", you ? "Your side" : "Their side", "The side it is done to.", [ep, e, k](int) {
            J &x = action_of(ep, e, k);
            x.set("side", J::str(x["side"].text("enemy") == "player" ? "enemy" : "player"));
            touch();
        }));
    }
    if (kind == PS_DO_MUSIC) {
        rows.push_back(num_row("Song", do_value_text(kind, a), "The song the fight goes on with (or silence).", [ep, e, k](int d) {
            J &x = action_of(ep, e, k);
            std::vector<int> l = song_list();
            int at = -1;
            for (size_t i = 0; i < l.size(); i++) { if (x["value"].isNum() && l[i] == (int)x["value"].n) { at = (int)i; } }
            at += d;
            if (at < -1) { at = (int)l.size() - 1; }
            if (at < 0 || at >= (int)l.size()) { x.erase("value"); } else { x.set("value", J::num(l[(size_t)at])); }
            touch();
        }));
    } else if (kind == PS_DO_AURA) {
        rows.push_back(num_row("Aura", do_value_text(kind, a), "", [ep, e, k](int) { J &x = action_of(ep, e, k); x.set("value", J::boolean(!x["value"].truthy(true))); touch(); }));
    } else if (dv.label[0]) {
        rows.push_back(num_row(dv.label, do_value_text(kind, a), kDoTips[kind], [ep, e, k, dv](int d) { cycle(action_of(ep, e, k), "value", d, dv.lo, dv.hi, dv.step, dv.def); }));
    }
}

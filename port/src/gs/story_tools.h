// The Story Creator's tools for a whole saga (included by story_ui.cpp inside its unnamed namespace, after the line
// pages): the links between episodes kept right when episodes move, the saga's check, undo, episode templates, the
// What-if generator, sharing a saga, and the My Sagas and Saga pages that use them.

// =============================================================================================== links between episodes
// Every link of a saga, handed to f(J &): an episode's "next_win" / "next_lose", and the "goto" of a question's answers
// wherever the question is (a scene, a cutscene of the fight, the ending).
template <typename F> void each_goto(J &v, F &f) {
    if (v.isObj()) {
        J *c = v.find("choices");
        if (c != NULL && c->isArr()) {
            for (J &x : c->a) {
                J *g = x.isObj() ? x.find("goto") : NULL;
                if (g != NULL) { f(*g); }
            }
        }
        for (J &x : v.a) { each_goto(x, f); }
    } else if (v.isArr()) {
        for (J &x : v.a) { each_goto(x, f); }
    }
}
template <typename F> void each_link(J &d, F f) {
    J *eps = d.find("episodes");
    for (size_t i = 0; eps != NULL && eps->isArr() && i < eps->a.size(); i++) {
        J &e = eps->a[i];
        if (J *w = e.find("next_win")) { f(*w); }
        if (J *l = e.find("next_lose")) { f(*l); }
    }
    each_goto(d, f);
}
// The episodes moved: map[old place] = the new one (-1: gone). A link by number follows its episode; one to an episode
// that is gone ends the saga instead. How many were so cut.
int relink(J &d, const std::vector<int> &map) {
    int cut = 0;
    each_link(d, [&](J &g) {
        if (!g.isNum()) { return; }
        int n = (int)g.n - 1;
        if (n < 0 || n >= (int)map.size()) { return; } // (a link that already led nowhere stays: the check shows it)
        if (map[(size_t)n] < 0) { g = J::str("end"); cut++; }
        else { g = J::num(map[(size_t)n] + 1); }
    });
    return cut;
}
std::vector<int> same_places(int n) { std::vector<int> m((size_t)n); for (int i = 0; i < n; i++) { m[(size_t)i] = i; } return m; }
// two neighbours change places
void episode_swap(int a, int b) {
    J &l = episodes();
    int n = (int)l.a.size();
    if (a < 0 || b < 0 || a >= n || b >= n || a == b) { return; }
    std::vector<int> map = same_places(n);
    map[(size_t)a] = b;
    map[(size_t)b] = a;
    relink(doc(), map);
    std::swap(l.a[(size_t)a], l.a[(size_t)b]);
    touch();
}
// an episode put in at `at` (those from there move down one)
void episode_insert(int at, const J &e) {
    J &l = episodes();
    int n = (int)l.a.size();
    at = std::max(0, std::min(at, n));
    std::vector<int> map = same_places(n);
    for (int i = at; i < n; i++) { map[(size_t)i] = i + 1; }
    relink(doc(), map);
    l.a.insert(l.a.begin() + at, e);
    touch();
}
// a copy of an episode, put after it (its links, the copy's too, follow the episodes that moved)
void episode_duplicate(int i) {
    J &l = episodes();
    if (i < 0 || i >= (int)l.a.size()) { return; }
    std::vector<int> map = same_places((int)l.a.size());
    for (size_t k = (size_t)i + 1; k < map.size(); k++) { map[k] = (int)k + 1; }
    relink(doc(), map);
    J copy = l.a[(size_t)i];
    copy.set("title", J::str(copy["title"].text("Episode") + " (copy)"));
    l.a.insert(l.a.begin() + i + 1, copy);
    touch();
}
int episode_delete(int i) {
    J &l = episodes();
    int n = (int)l.a.size();
    if (i < 0 || i >= n) { return 0; }
    std::vector<int> map = same_places(n);
    map[(size_t)i] = -1;
    for (int k = i + 1; k < n; k++) { map[(size_t)k] = k - 1; }
    int cut = relink(doc(), map);
    l.a.erase(l.a.begin() + i);
    touch();
    return cut;
}

// Where a link leads, as a place: the episode (from 0), GO_END, GO_RETRY (the same again), GO_NEXT (as usual: a
// question's answer that changes nothing). An episode number that is not in the saga is returned as it is (>= n).
int link_place(const J &g, int self, bool won, int n) {
    if (g.isNum()) { return (int)g.n - 1 < 0 ? n : (int)g.n - 1; }
    std::string t = g.text(won ? "next" : "retry");
    if (t == "end") { return GO_END; }
    if (t == "retry") { return GO_RETRY; }
    return self + 1 < n ? self + 1 : GO_END;
}
std::string place_text(int p, int n) {
    if (p == GO_END) { return "end"; }
    if (p == GO_RETRY) { return "again"; }
    if (p == GO_NEXT) { return "on"; }
    if (p < 0 || p >= n) { return "??"; }
    return std::to_string(p + 1);
}
// the answers of the questions in an episode (anywhere in it), as places
std::vector<int> answer_places(const J &ep, int self, int n) {
    std::vector<int> out;
    J copy = ep; // (each_goto walks a J it may change: a copy, read only)
    auto f = [&](J &g) {
        if (g.isNum()) { out.push_back((int)g.n - 1 < 0 ? n : (int)g.n - 1); }
        else if (goto_of(g) != GO_NEXT) { out.push_back(goto_of(g) == GO_RETRY ? self : goto_of(g)); }
    };
    each_goto(copy, f);
    return out;
}
// "win > 3   lose > again", "then > end", what an episode leads to
std::string flow_text(const J &ep, int self, int n) {
    bool fight = ep["battle"]["fight"].truthy(true);
    std::string t = fight ? "win > " + place_text(link_place(ep["next_win"], self, true, n), n) + "  lose > " + place_text(link_place(ep["next_lose"], self, false, n), n)
                          : "then > " + place_text(link_place(ep["next_win"], self, true, n), n);
    if (!answer_places(ep, self, n).empty()) { t += "  ?"; }
    return t;
}

// =============================================================================================== the saga's check
struct Problem { int ep; std::string what; };
std::vector<Problem> saga_problems(const J &d) {
    std::vector<Problem> out;
    const J &eps = d["episodes"];
    int n = (int)eps.size();
    if (n == 0) { out.push_back({-1, "The saga has no episodes yet."}); return out; }
    std::vector<bool> reached((size_t)n, false);
    std::vector<int> todo = {0};
    reached[0] = true;
    while (!todo.empty()) { // (what the saga can lead to from its first episode: wins, losses, answers)
        int i = todo.back();
        todo.pop_back();
        const J &e = eps[i];
        std::vector<int> to = answer_places(e, i, n);
        to.push_back(link_place(e["next_win"], i, true, n));
        if (e["battle"]["fight"].truthy(true)) { to.push_back(link_place(e["next_lose"], i, false, n)); }
        for (int p : to) {
            if (p >= 0 && p < n && !reached[(size_t)p]) { reached[(size_t)p] = true; todo.push_back(p); }
        }
    }
    for (int i = 0; i < n; i++) {
        const J &e = eps[i];
        std::string at = "Episode " + std::to_string(i + 1) + ": ";
        bool fight = e["battle"]["fight"].truthy(true);
        if (team_of(e, "player").chara.empty() || team_of(e, "enemy").chara.empty()) { out.push_back({i, at + "it needs a fighter on each side."}); }
        int w = link_place(e["next_win"], i, true, n), l = link_place(e["next_lose"], i, false, n);
        if (w >= n) { out.push_back({i, at + (fight ? "\"If you win\"" : "\"Then\"") + " leads to an episode that is not there."}); }
        if (fight && l >= n) { out.push_back({i, at + "\"If you lose\" leads to an episode that is not there."}); }
        for (int p : answer_places(e, i, n)) {
            if (p >= n) { out.push_back({i, at + "an answer to a question leads to an episode that is not there."}); break; }
        }
        if (!reached[(size_t)i]) { out.push_back({i, at + "it is never reached: no win, loss or answer leads to it."}); }
        if (!fight && e["intro"]["lines"].size() == 0) { out.push_back({i, at + "no fight and no lines: it shows nothing."}); }
        const J &evs = e["battle"]["events"];
        int scenes = 0;
        for (size_t k = 0; fight && k < evs.size(); k++) {
            std::string ev = at + "event " + std::to_string(k + 1) + " ";
            if (evs[k]["do"].size() == 0) { out.push_back({i, ev + "does nothing yet."}); }
            if (evs[k]["when"].text() == "after_event") {
                int v = (int)evs[k]["value"].number(1);
                if (v < 1 || v > (int)evs.size() || v == (int)k + 1) { out.push_back({i, ev + "waits for an event that is not there."}); }
            }
            for (size_t a = 0; a < evs[k]["do"].size(); a++) {
                const J &x = evs[k]["do"][a];
                if (x["do"].text() == "scene") {
                    scenes++;
                    if (x["lines"].size() == 0) { out.push_back({i, ev + "has a cutscene with no lines (it is left out)."}); }
                }
            }
        }
        if (scenes > PS_SCENE_MAX) { out.push_back({i, at + "more than " + std::to_string(PS_SCENE_MAX) + " cutscenes in the fight: the ones after are left out."}); }
        if ((int)(evs.size() + e["battle"]["lines"].size()) > 96) { out.push_back({i, at + "more than 96 events and fight lines: the ones after are left out."}); }
    }
    return out;
}

// =============================================================================================== undo
// The saga as it was before each change (touch() counts the changes; once a frame a change is put on the list).
std::vector<J> sUndo, sRedo;
J sSnap;
int sSnapSaga = -1, sSnapGen = -1;
void undo_track(void) {
    Saga *s = saga();
    if (s == NULL || sPlayMode) { sUndo.clear(); sRedo.clear(); sSnapSaga = -1; return; }
    if (sSnapSaga != sSaga) { sSnap = s->doc; sSnapSaga = sSaga; sSnapGen = sDocGen; sUndo.clear(); sRedo.clear(); return; }
    if (sSnapGen != sDocGen) {
        sUndo.push_back(sSnap);
        if (sUndo.size() > 80) { sUndo.erase(sUndo.begin()); }
        sRedo.clear();
        sSnap = s->doc;
        sSnapGen = sDocGen;
    }
}
void undo(bool back) {
    Saga *s = saga();
    std::vector<J> &from = back ? sUndo : sRedo, &to = back ? sRedo : sUndo;
    if (s == NULL || from.empty()) { notice(back ? "Nothing to undo." : "Nothing to redo."); return; }
    to.push_back(s->doc);
    s->doc = from.back();
    from.pop_back();
    sSnap = s->doc;
    sDirty = true;
    sound(2);
    notice(back ? "Undone (R1 redoes it)" : "Redone");
}

// =============================================================================================== copy and paste
J sClip;              // what was copied ...
std::string sClipKind; // ... "line", "lines", "event", "action"
void clip(const std::string &kind, const J &v, const std::string &what) { sClip = v; sClipKind = kind; notice(what + " copied: paste it from another page's menu."); }

// =============================================================================================== templates
int chara_named(const char *name) {
    for (int i = 0; i < kDiscCharas; i++) { if (strcmp(kCharaNames[i], name) == 0) { return i; } }
    return 3;
}
J who(int chara, int health = -1) {
    J m = J::obj();
    m.set("chara", chara_ref(chara));
    if (health > 0) { m.set("health", J::num(health)); }
    return m;
}
J say(const std::string &speaker, const std::string &text, std::initializer_list<std::pair<const char *, J>> more = {}) {
    J l = new_line(speaker, text);
    for (const auto &kv : more) { l.set(kv.first, kv.second); }
    return l;
}
// a line said during the fight (an event's action)
J say_act(const std::string &speaker, const std::string &text) { J a = new_line(speaker, text); a.set("do", J::str("say")); return a; }
J lines(std::initializer_list<J> ls) { J a = J::arr(); for (const J &l : ls) { a.push(l); } return a; }
J scene_with(int distance, J ls) { J s = J::obj(); s.set("distance", J::num(distance)); s.set("lines", ls); return s; }
J fx(std::initializer_list<const char *> names) { J a = J::arr(); for (const char *n : names) { a.push(J::str(n)); } return a; }
J act(const char *kind, const char *side = NULL, int value = -99999) {
    J a = J::obj();
    a.set("do", J::str(kind));
    if (side != NULL) { a.set("side", J::str(side)); }
    if (value != -99999) { a.set("value", J::num(value)); }
    return a;
}
J event(const char *when, int value, std::initializer_list<J> acts) {
    J e = J::obj();
    e.set("when", J::str(when));
    if (value != -99999) { e.set("value", J::num(value)); }
    J d = J::arr();
    for (const J &a : acts) { d.push(a); }
    e.set("do", d);
    return e;
}
const char *const kTemplateNames[6] = {"Blank episode", "Rival duel", "Boss with phases", "Team fight", "Survival",
                                       "Cutscene only"};
const char *const kTemplateTips[6] = {
    "Two fighters, a short opening scene, a fight.",
    "A rival's challenge: an opening exchange, taunts during the fight, a scene for a win and for a loss.",
    "A boss that changes when it is hurt: a cutscene stops the fight, it powers up, the music changes.",
    "Three against three, with lines when someone is knocked out or a partner comes in.",
    "Hold out against a strong opponent until the clock runs out.",
    "No fight: a title card, narration and a conversation.",
};
J template_episode(int kind, int n) {
    J e = new_episode(n);
    J &bt = e.at("battle");
    int goku = 3;
    switch (kind) {
    case 1:
        e.set("title", J::str("A Rival's Challenge"));
        e.set("description", J::str("An old rival wants a rematch."));
        e.set("intro", scene_with(2, lines({say("enemy", "There you are. I've been waiting.", {{"camera", J::str("low")}, {"move", J::str("push")}}),
                                            say("player", "You again? Don't you ever give up?", {{"camera", J::str("shoulder")}}),
                                            say("enemy", "Not until I've beaten you!", {{"camera", J::str("close")}, {"effects", fx({"aura_on", "shake"})}}),
                                            say("player", "Alright. Let's settle this!", {{"camera", J::str("two")}, {"pose", J::str("Battle stance")}})})));
        bt.set("cpu", J::num(2));
        {
            J evs = J::arr();
            evs.push(event("start", -99999, {say_act("enemy", "Show me what you've got!")}));
            evs.push(event("enemy_health", 50, {say_act("enemy", "Not bad... but I'm only getting started!"), act("aura", "enemy", 1)}));
            evs.push(event("player_health", 30, {say_act("enemy", "Is that all? Pathetic!")}));
            bt.set("events", evs);
        }
        e.set("win", scene_with(2, lines({say("enemy", "Grr... This isn't over!"), say("player", "Any time you want a rematch.")})));
        e.set("lose", scene_with(2, lines({say("enemy", "Hah! Just as I thought.", {{"pose", J::str("Arms open")}})})));
        break;
    case 2:
        e.set("title", J::str("The Tyrant"));
        e.set("description", J::str("A boss with more than one face."));
        e.set("enemy", lines({who(chara_named("Frieza Final Form"))}));
        e.set("intro", scene_with(3, lines({say("enemy", "So you are the one who has been causing me trouble.", {{"camera", J::str("low")}}),
                                            say("player", "Your reign ends here!", {{"camera", J::str("close")}, {"effects", fx({"aura_on"})}})})));
        bt.set("cpu", J::num(2));
        bt.set("show_objective", J::boolean(true));
        {
            J evs = J::arr();
            J sc = act("scene");
            sc.set("distance", J::num(2));
            sc.set("lines", lines({say("enemy", "Enough! You will see my true power!", {{"camera", J::str("close")}, {"effects", fx({"aura_on", "big_shake"})}}),
                                   say("player", "His power... it's still rising!", {{"camera", J::str("shoulder")}})}));
            J card = act("card");
            card.set("text", J::str("The tyrant powers up!"));
            card.set("card", J::str("title"));
            evs.push(event("enemy_health", 60, {sc, act("heal", "enemy", 25), act("max_power", "enemy"), act("music", NULL, 6), card}));
            evs.push(event("enemy_health", 25, {say_act("enemy", "No! I will not lose to a monkey like you!"), act("ki", "enemy", 5), act("flash", NULL, 1)}));
            bt.set("events", evs);
        }
        e.set("win", scene_with(2, lines({say("enemy", "Impossible... how...?", {{"effects", fx({"dark"})}})})));
        break;
    case 3:
        e.set("title", J::str("Three Against Three"));
        e.set("description", J::str("A team battle."));
        e.set("player", lines({who(goku), who(chara_named("Piccolo (End)")), who(chara_named("Gohan"))}));
        e.set("enemy", lines({who(chara_named("Android 17")), who(chara_named("Android 18")), who(chara_named("Perfect Cell"))}));
        e.set("intro", scene_with(2, lines({say("enemy", "Three of you? It won't make a difference."), say("player", "We'll see about that!")})));
        {
            J evs = J::arr();
            evs.push(event("player_switch", -99999, {say_act("player2", "I'm in! Leave this to me.")}));
            evs.push(event("player_ko", -99999, {say_act("enemy", "One down. Who's next?")}));
            J c = act("card");
            c.set("text", J::str("One of them is down!"));
            evs.push(event("enemy_ko", -99999, {c}));
            evs.a.back().set("repeat", J::num(5));
            bt.set("events", evs);
        }
        break;
    case 4:
        e.set("title", J::str("Hold the Line"));
        e.set("description", J::str("Last as long as you can."));
        e.set("enemy", lines({who(chara_named("Broly Legendary Super Saiyan"))}));
        e.set("intro", scene_with(3, lines({say("enemy", "Kakarot!", {{"effects", fx({"big_shake", "aura_on"})}}),
                                            say("player", "I can't beat him... but I can hold him off until help comes!")})));
        bt.set("win", J::str("survive"));
        bt.set("survive", J::num(60));
        bt.set("cpu", J::num(3));
        bt.set("enemy_boost", [] { J b = J::obj(); b.set("attack", J::num(2)); return b; }());
        {
            J evs = J::arr();
            J c = act("card");
            c.set("text", J::str("Ten more seconds!"));
            c.set("card", J::str("title"));
            evs.push(event("time_left", 10, {c}));
            evs.push(event("player_health", 30, {say_act("player", "Hang on... just a little longer!")}));
            bt.set("events", evs);
        }
        break;
    case 5:
        e.set("title", J::str("Interlude"));
        e.set("description", J::str("A quiet moment between battles."));
        e.set("intro", scene_with(2, lines({say("narrator", "Some time later...", {{"card", J::str("title")}, {"transition", J::str("fade_in")}}),
                                            say("narrator", "The battle was over, but the peace would not last.", {{"card", J::str("narration")}, {"camera", J::str("wide")}}),
                                            say("player", "Something's coming. I can feel it.", {{"camera", J::str("close")}}),
                                            say("enemy", "Then we had better be ready.", {{"camera", J::str("shoulder")}, {"transition", J::str("fade_out")}})})));
        bt.set("fight", J::boolean(false));
        e.erase("win");
        e.set("lose", J::obj());
        break;
    default: break;
    }
    return e;
}
// the list of templates; picked(kind) with the one chosen
void open_templates(const std::string &title, std::function<void(int)> picked) {
    std::vector<Row> rs;
    for (int i = 0; i < 6; i++) {
        Row r;
        r.label = kTemplateNames[i];
        r.tip = kTemplateTips[i];
        r.ok = [picked, i] { ov_close(); picked(i); };
        rs.push_back(r);
    }
    open_list(title, rs);
}

// =============================================================================================== the What-if generator
// Two characters, and a short saga of what might happen if they met: a meeting with a question, a fight or a truce,
// then a common enemy for both, and an ending.
J what_if(int a, int b) {
    static unsigned seed;
    if (seed == 0) { seed = (unsigned)SDL_GetTicks() | 1u; }
    auto rnd = [](int n) { seed = seed * 1664525u + 1013904223u; return (int)((seed >> 16) % (unsigned)n); };
    static const char *const kVillains[] = {"Perfect Cell", "Frieza Final Form", "Kid Buu", "Cooler Final Form", "Super Janemba",
                                            "Broly Legendary Super Saiyan", "Omega Shenron", "Hildegarn"};
    static const int kStages[] = {0, 1, 4, 9, 13, 15, 19, 23, 24, 26};
    int villain = chara_named(kVillains[rnd(8)]);
    int stage = kStages[rnd(10)], stage2 = kStages[rnd(10)];
    std::string A = short_name(a), B = short_name(b), V = short_name(villain);
    J d = J::obj();
    d.set("format", J::num(1));
    d.set("title", J::str("What if " + A + " met " + B + "?"));
    d.set("author", J::str("What-if generator"));
    d.set("description", J::str(A + " and " + B + " meet where they never should have. A fight, or a truce, and then " + V + "."));
    J eps = J::arr();
    // 1: the meeting, and a question
    J e1 = new_episode(1);
    e1.set("title", J::str("An Unexpected Meeting"));
    e1.set("description", J::str("Two warriors from different stories."));
    e1.set("stage", J::num(stage));
    e1.set("player", lines({who(a)}));
    e1.set("enemy", lines({who(b)}));
    J q = say("player", "So... what now?", {{"camera", J::str("two")}, {"move", J::str("orbit")}});
    J ch = J::arr();
    J c1 = J::obj(); c1.set("text", J::str("Fight " + B)); c1.set("goto", J::num(2)); ch.push(c1);
    J c2 = J::obj(); c2.set("text", J::str("Talk it out")); c2.set("goto", J::num(3)); ch.push(c2);
    q.set("choices", ch);
    e1.set("intro", scene_with(3, lines({say("narrator", "What if...?", {{"card", J::str("title")}, {"transition", J::str("fade_in")}}),
                                         say("narrator", "Somewhere far away, two warriors who should never have met came face to face.", {{"card", J::str("narration")}, {"camera", J::str("wide")}, {"move", J::str("from_wide")}}),
                                         say("enemy", "Who are you? You're not from around here.", {{"camera", J::str("close")}}),
                                         say("player", "I could ask you the same thing.", {{"camera", J::str("shoulder")}}),
                                         say("enemy", "There's one way to find out what you're made of.", {{"camera", J::str("low")}, {"effects", fx({"aura_on"})}}),
                                         q})));
    e1.at("battle").set("fight", J::boolean(false));
    e1.erase("win");
    e1.set("next_win", J::num(2));
    eps.push(e1);
    // 2: the fight
    J e2 = template_episode(1, 2);
    e2.set("title", J::str(A + " vs " + B));
    e2.set("description", J::str("A clash of two worlds."));
    e2.set("stage", J::num(stage));
    e2.set("music", J::num(rnd(20)));
    e2.set("player", lines({who(a)}));
    e2.set("enemy", lines({who(b)}));
    e2.set("intro", scene_with(2, lines({say("enemy", "Don't hold back!", {{"camera", J::str("close")}, {"effects", fx({"aura_on"})}}),
                                         say("player", "I wasn't planning to!", {{"camera", J::str("two")}})})));
    e2.set("win", scene_with(2, lines({say("enemy", "You're strong... Stronger than I thought."), say("player", "You too. But something's wrong. Do you feel that?", {{"effects", fx({"rumble"})}})})));
    e2.set("next_win", J::num(4));
    e2.set("next_lose", J::str("retry"));
    eps.push(e2);
    // 3: the truce
    J e3 = new_episode(3);
    e3.set("title", J::str("An Uneasy Truce"));
    e3.set("stage", J::num(stage));
    e3.set("player", lines({who(a)}));
    e3.set("enemy", lines({who(b)}));
    e3.set("intro", scene_with(2, lines({say("player", "We don't have to fight.", {{"camera", J::str("close")}}),
                                         say("enemy", "...Fine. But something is coming. Can you feel it?", {{"camera", J::str("shoulder")}, {"effects", fx({"rumble"})}}),
                                         say("narrator", "A dark power was rising...", {{"card", J::str("narration")}, {"transition", J::str("fade_out")}})})));
    e3.at("battle").set("fight", J::boolean(false));
    e3.erase("win");
    e3.set("next_win", J::num(4));
    eps.push(e3);
    // 4: the common enemy, both on the player's side
    J e4 = template_episode(2, 4);
    e4.set("title", J::str("A Common Enemy"));
    e4.set("description", J::str(A + " and " + B + " against " + V + "."));
    e4.set("stage", J::num(stage2));
    e4.set("player", lines({who(a), who(b)}));
    e4.set("enemy", lines({who(villain)}));
    e4.set("intro", scene_with(3, lines({say("enemy", "Two insects, working together? How touching.", {{"camera", J::str("low")}, {"move", J::str("push")}}),
                                         say("player", "Ready?", {{"camera", J::str("close")}}),
                                         say("player2", "Ready!", {{"effects", fx({"aura_on"})}})})));
    e4.set("next_win", J::str("end"));
    e4.set("next_lose", J::str("retry"));
    eps.push(e4);
    d.set("episodes", eps);
    J end = J::obj();
    end.set("distance", J::num(2));
    end.set("lines", lines({say("player", "We did it.", {{"camera", J::str("two")}}),
                            say("narrator", "And so two warriors from different worlds parted as friends... until the next time.", {{"card", J::str("narration")}, {"transition", J::str("fade_out")}})}));
    end.set("player", lines({who(a)}));
    end.set("enemy", lines({who(b)}));
    d.set("ending", end);
    return d;
}

// =============================================================================================== sharing a saga
// What is wrong with a saga read from a file or pasted, "" when it can be played (old sagas included)
std::string saga_fault(const J &d) {
    if (!d.isObj()) { return "it is not a saga (no { } around it)"; }
    if (!d["episodes"].isArr()) { return "it has no \"episodes\" list"; }
    for (size_t i = 0; i < d["episodes"].size(); i++) {
        if (!d["episodes"][i].isObj()) { return "episode " + std::to_string(i + 1) + " is not written as { }"; }
    }
    return "";
}
// a new saga file of the player's (named after its title), read in again, the cursor of My Sagas on it: its file name
// ("" when it could not be saved)
std::string add_saga(J d, const std::string &what) {
    if (!d.has("format")) { d.set("format", J::num(1)); }
    Saga s;
    s.doc = d;
    s.file = file_for(d["title"].text("saga"));
    std::string o;
    js_write(d, o);
    o += '\n';
    if (!write_file(sDir + "/" + s.file, o)) { notice("The saga could not be saved in " + sDir, true); return ""; }
    sagas_read();
    for (size_t i = 0; i < sSagas.size(); i++) { if (sSagas[i].file == s.file) { sHomeCur = 1 + (int)i; } }
    notice(what + ": " + s.file);
    return s.file;
}
void open_folder(void) {
    char full[1100], url[1200];
#ifdef _WIN32
    if (_fullpath(full, sDir.c_str(), sizeof(full)) == NULL) { return; }
    for (char *c = full; *c != '\0'; c++) { if (*c == '\\') { *c = '/'; } }
    snprintf(url, sizeof(url), "file:///%s", full);
#else
    if (realpath(sDir.c_str(), full) == NULL) { return; }
    snprintf(url, sizeof(url), "file://%s", full);
#endif
    fprintf(stderr, "bt3: story: opening %s\n", url);
    if (getenv("BT3_STORY_SCRIPT") == NULL) { SDL_OpenURL(url); } // (not in a test: it would open a window)
    notice("The sagas folder: " + std::string(full));
}

// =============================================================================================== My Sagas
std::string best_text(const Saga &s) {
    const J &b = sProgress[saga_key(s).c_str()]["best"];
    std::string best = "";
    for (size_t i = 0; i < b.size(); i++) {
        std::string r = b.a[i]["rank"].text();
        if (!r.empty() && (best.empty() || r < best)) { best = r; } // ("A" < "B" ... ; "S" is the best of all)
        if (r == "S") { best = "S"; break; }
    }
    return best;
}
std::string home_progress(const Saga &s) {
    const J &p = sProgress[saga_key(s).c_str()];
    int n = (int)s.doc["episodes"].size();
    std::string t = p["done"].truthy() ? std::string("Cleared!") : p.has("episode") ? "Episode " + std::to_string((int)p["episode"].number(0) + 1) + " / " + std::to_string(n)
                                                                                    : std::to_string(n) + (n == 1 ? " episode" : " episodes");
    int cleared = (int)p["cleared"].size();
    if (cleared > 0 && !p["done"].truthy()) { t += "  (" + std::to_string(cleared) + " cleared)"; }
    std::string best = best_text(s);
    if (!best.empty()) { t += "  best " + best; }
    return t;
}
void new_saga_named(const std::string &t, J first) {
    J d = J::obj();
    d.set("format", J::num(1));
    d.set("title", J::str(t.empty() ? "My Saga" : t));
    d.set("author", J::str(""));
    d.set("description", J::str(""));
    J eps = J::arr();
    eps.push(first);
    d.set("episodes", eps);
    std::string file = add_saga(d, "A new saga");
    for (size_t i = 0; !file.empty() && i < sSagas.size(); i++) {
        if (!sSagas[i].builtin && sSagas[i].file == file) {
            sSaga = (int)i;
            sPlayMode = false;
            sDirty = false;
            sPages.clear();
            push_page(SC_SAGA);
        }
    }
}
void rows_home(std::vector<Row> &rows) {
    rows.push_back(sec("MY SAGAS"));
    for (size_t i = 0; i < sSagas.size(); i++) {
        const Saga &s = sSagas[i];
        Row r;
        r.label = s.doc["title"].text("Untitled saga");
        r.value = home_progress(s);
        r.sub = s.builtin ? "sample" : s.doc["author"].text();
        const J &e0 = s.doc["episodes"][0];
        r.face = chara_of(e0["player"][0].isObj() ? e0["player"][0]["chara"] : e0["player"][0]);
        r.model = r.face;
        r.tip = s.doc["description"].text().empty() ? "Cross plays it (from where you left it), Square edits it, Triangle has more." : s.doc["description"].text();
        int idx = (int)i;
        r.ok = [idx] { play_saga(idx, false); };
        r.okLabel = "Play";
        r.sq = [idx] { sSaga = idx; sPlayMode = false; sDirty = false; sPages.clear(); push_page(SC_SAGA); sound(1); };
        r.sqLabel = "Edit";
        r.tri = [idx] {
            const Saga &sg = sSagas[(size_t)idx];
            std::vector<Row> rs;
            Row a;
            a.label = "Play from the first episode";
            a.ok = [idx] { ov_close(); play_saga(idx, true); };
            rs.push_back(a);
            Row b;
            b.label = "Play from another episode...";
            b.tip = "Your progress goes on from the episode chosen.";
            b.ok = [idx] {
                ov_close();
                std::vector<Row> es;
                const J &eps = sSagas[(size_t)idx].doc["episodes"];
                for (size_t k = 0; k < eps.size(); k++) {
                    Row x;
                    x.label = std::to_string(k + 1) + ". " + eps[k]["title"].text("Untitled");
                    int ep = (int)k;
                    x.ok = [idx, ep] {
                        ov_close();
                        sSaga = idx;
                        sPlayMode = true;
                        J &p = progress_of(sSagas[(size_t)idx]);
                        p.set("episode", J::num(ep));
                        p.erase("done");
                        progress_save();
                        leave_for(idx, ep, "");
                    };
                    es.push_back(x);
                }
                open_list("PLAY FROM", es);
            };
            rs.push_back(b);
            Row c;
            c.label = sg.builtin ? "Make a copy to edit" : "Duplicate this saga";
            c.ok = [idx] {
                ov_close();
                J d = sSagas[(size_t)idx].doc;
                d.set("title", J::str(d["title"].text("Saga") + " (copy)"));
                add_saga(d, "Copied");
            };
            rs.push_back(c);
            Row sh;
            sh.label = "Copy it to the clipboard (to share)";
            sh.tip = "The saga as text: paste it in a message or a file. \"Paste a saga\" on My Sagas reads it back.";
            sh.ok = [idx] {
                ov_close();
                std::string o;
                js_write(sSagas[(size_t)idx].doc, o);
                SDL_SetClipboardText(o.c_str());
                notice("The saga is on the clipboard (" + std::to_string(o.size()) + " letters).");
            };
            rs.push_back(sh);
            Row rp;
            rp.label = "Reset my progress in it";
            rp.ok = [idx] {
                ov_close();
                confirm("FORGET YOUR PROGRESS AND BEST RANKS?", [idx] {
                    sProgress.erase(saga_key(sSagas[(size_t)idx]).c_str());
                    progress_save();
                    notice("Progress reset.");
                });
            };
            rs.push_back(rp);
            Row del;
            del.label = sg.builtin ? "(the sample cannot be deleted)" : "Delete this saga";
            del.locked = sg.builtin;
            del.ok = [idx] {
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
            rs.push_back(del);
            open_list(sg.doc["title"].text("SAGA"), rs);
        };
        r.triLabel = "More";
        rows.push_back(r);
    }
    rows.push_back(sec("MAKE YOUR OWN"));
    Row n;
    n.label = "New saga";
    n.tip = "Start empty, from an episode template, or let the What-if generator write one for two characters.";
    n.ok = [] {
        std::vector<Row> rs;
        Row a;
        a.label = "An empty saga";
        a.ok = [] { ov_close(); open_keys("NAME YOUR SAGA", "", 40, [](const std::string &t) { new_saga_named(t, new_episode(1)); }); };
        rs.push_back(a);
        Row b;
        b.label = "Start from an episode template...";
        b.ok = [] {
            ov_close();
            open_templates("THE FIRST EPISODE", [](int k) {
                open_keys("NAME YOUR SAGA", "", 40, [k](const std::string &t) { new_saga_named(t, template_episode(k, 1)); });
            });
        };
        rs.push_back(b);
        Row c;
        c.label = "What-if generator...";
        c.tip = "Pick two characters: a short saga of what might happen if they met (a question, a fight or a truce, a common enemy).";
        c.ok = [] {
            ov_close();
            open_grid("WHAT IF... (THE FIRST ONE)", 3, [](int a) {
                open_grid("...MET? (THE SECOND ONE)", chara_named("Vegeta"), [a](int b) { add_saga(what_if(a, b), "A what-if saga"); });
            });
        };
        rs.push_back(c);
        open_list("NEW SAGA", rs);
    };
    rows.push_back(n);
    Row p;
    p.label = "Paste a saga";
    p.tip = "A saga copied as text (from \"Copy it to the clipboard\", or a friend's): it is saved as a new file.";
    p.ok = [] {
        char *c = SDL_GetClipboardText();
        std::string text = c != NULL ? c : "";
        SDL_free(c);
        J d;
        std::string fault = js_read(text, d) ? saga_fault(d) : "it is not a saga's text";
        if (text.empty()) { notice("The clipboard is empty.", true); }
        else if (!fault.empty()) { notice("What is on the clipboard cannot be read: " + fault + ".", true); }
        else { add_saga(d, "Pasted"); }
    };
    rows.push_back(p);
    Row f;
    f.label = "Open the sagas folder";
    f.value = sDir;
    f.tip = "Saga files are shared by copying them into this folder (JSON: docs/port/sagas.md). Square reads it again.";
    f.ok = [] { open_folder(); };
    f.okLabel = "Open";
    f.sq = [] { sagas_read(); notice("Read " + std::to_string(sSagas.size() - 1) + " saga files from " + sDir); };
    f.sqLabel = "Read again";
    rows.push_back(f);
}

// =============================================================================================== the saga page
const char *const kDiffNames[5] = {"Much easier", "Easier", "As set in each fight", "Harder", "Much harder"};
void rows_saga(std::vector<Row> &rows) {
    J &d = doc();
    rows.push_back(sec("THE SAGA"));
    rows.push_back(text_row("Title", d["title"].text(), "THE SAGA'S TITLE", 40, [](const std::string &t) { doc().set("title", J::str(t)); }));
    rows.push_back(text_row("Author", d["author"].text(), "WHO MADE IT", 40, [](const std::string &t) { doc().set("author", J::str(t)); }));
    rows.push_back(text_row("Description", d["description"].text(), "WHAT IT IS ABOUT", 200, [](const std::string &t) { doc().set("description", J::str(t)); }));
    rows.back().tip = "Shown on My Sagas.";
    rows.push_back(sec("SETTINGS"));
    {
        const J &st = d["settings"];
        Row df;
        df.label = "Difficulty";
        df.value = kDiffNames[clampi((int)st["difficulty"].number(0), -2, 2) + 2];
        df.lr = [](int k) { cycle(doc().at("settings"), "difficulty", k, -2, 2, 1, 0); };
        df.tip = "Every opponent of the saga a step weaker or stronger than its fight says.";
        rows.push_back(df);
        Row lv;
        int lives = (int)st["lives"].number(0);
        lv.label = "Tries of an episode";
        lv.value = lives <= 0 ? std::string("As many as you like") : std::to_string(lives);
        lv.lr = [](int k) { cycle(doc().at("settings"), "lives", k, 0, 9, 1, 0); };
        lv.tip = "How many times a lost fight can be tried again before the story is sent back.";
        rows.push_back(lv);
        if (lives > 0) {
            Row ct;
            ct.label = "When the tries run out";
            ct.value = st["continue"].truthy() ? "Try the episode again anyway" : "Back to the saga's start";
            ct.lr = [](int) { J &s = doc().at("settings"); s.set("continue", J::boolean(!s["continue"].truthy())); touch(); };
            ct.tip = "Back to the start: the saga begins again from episode 1.";
            rows.push_back(ct);
        }
    }
    rows.push_back(sec("EPISODES"));
    J &eps = episodes();
    int n = (int)eps.a.size();
    std::vector<Problem> probs = saga_problems(d);
    for (int i = 0; i < n; i++) {
        const J &e = eps.a[(size_t)i];
        Row r;
        Team p = team_of(e, "player"), q = team_of(e, "enemy");
        bool fight = e["battle"]["fight"].truthy(true);
        r.label = std::to_string(i + 1) + ". " + e["title"].text("Untitled");
        r.value = (p.chara.empty() ? std::string("?") : short_name(p.chara[0])) + (fight ? " vs " : " & ") + (q.chara.empty() ? std::string("?") : short_name(q.chara[0]));
        r.sub = flow_text(e, i, n);
        r.face = p.chara.empty() ? -1 : p.chara[0];
        r.model = r.face;
        std::string warn;
        for (const Problem &pr : probs) { if (pr.ep == i) { warn += (warn.empty() ? "" : " ") + pr.what; } }
        if (!warn.empty()) { r.sub = "! " + r.sub; }
        r.tip = !warn.empty() ? warn : e["description"].text().empty() ? "Cross opens it. Win > / lose >: where it leads (? an answer to a question leads on too)." : e["description"].text();
        r.ok = [i] { push_page(SC_EPISODE, i); };
        r.okLabel = "Open";
        r.sq = [i] {
            std::vector<Row> rs;
            Row a;
            a.label = "Play this episode (a test)";
            a.ok = [i] { ov_close(); if (!sDirty || save_saga()) { sPlayMode = false; leave_for(sSaga, i, ""); } };
            rs.push_back(a);
            Row b;
            b.label = "Duplicate it";
            b.ok = [i] { ov_close(); episode_duplicate(i); notice("Episode copied (the links that went after it follow)"); };
            rs.push_back(b);
            Row c;
            c.label = "Put a new episode after it...";
            c.ok = [i] { ov_close(); open_templates("WHICH KIND?", [i](int k) { episode_insert(i + 1, template_episode(k, i + 2)); push_page(SC_EPISODE, i + 1); }); };
            rs.push_back(c);
            Row u;
            u.label = "Move it up";
            u.ok = [i] { ov_close(); episode_swap(i, i - 1); };
            u.locked = i == 0;
            rs.push_back(u);
            Row dn;
            dn.label = "Move it down";
            dn.ok = [i] { ov_close(); episode_swap(i, i + 1); };
            dn.locked = i + 1 >= (int)episodes().a.size();
            rs.push_back(dn);
            open_list("EPISODE " + std::to_string(i + 1), rs);
        };
        r.sqLabel = "More";
        r.tri = [i] {
            confirm("DELETE EPISODE " + std::to_string(i + 1) + "?", [i] {
                int cut = episode_delete(i);
                notice(cut > 0 ? std::to_string(cut) + (cut == 1 ? " link that led to it now ends" : " links that led to it now end") + " the saga. L1 undoes it." : "Deleted. L1 undoes it.");
            });
        };
        r.triLabel = "Delete";
        r.l2 = [i] { if (i > 0) { episode_swap(i, i - 1); page().cur--; sound(0); } };
        r.r2 = [i] { if (i + 1 < (int)episodes().a.size()) { episode_swap(i, i + 1); page().cur++; sound(0); } };
        r.l2Label = "Move";
        rows.push_back(r);
    }
    Row add;
    add.label = "+ Add an episode";
    add.tip = "A blank one, or a template: rival duel, boss with phases, team fight, survival, cutscene only.";
    add.ok = [] {
        open_templates("ADD AN EPISODE", [](int k) {
            int at = (int)episodes().a.size();
            episode_insert(at, template_episode(k, at + 1));
            push_page(SC_EPISODE, at);
        });
    };
    rows.push_back(add);
    {
        Row en;
        en.label = "Ending";
        int nl = (int)d["ending"]["lines"].size();
        en.value = nl == 0 ? std::string("none") : std::to_string(nl) + (nl == 1 ? " line" : " lines");
        en.tip = "A scene played when the saga is cleared, before the credits. Square shows it.";
        en.ok = [] { push_page(SC_SCENE, std::max(0, (int)episodes().a.size() - 1), "ending"); };
        en.okLabel = "Open";
        en.sq = [] { if (doc()["ending"]["lines"].size() > 0 && (!sDirty || save_saga())) { sPlayMode = false; leave_for(sSaga, std::max(0, (int)episodes().a.size() - 1), "ending"); } };
        en.sqLabel = "Preview";
        rows.push_back(en);
    }
    rows.push_back(sec("CHECK AND PLAY"));
    Row ck;
    ck.label = "Check the saga";
    ck.value = probs.empty() ? std::string("All good") : std::to_string(probs.size()) + (probs.size() == 1 ? " thing to look at" : " things to look at");
    ck.tip = "Links that lead nowhere, episodes never reached, events that wait for nothing...";
    ck.ok = [] {
        std::vector<Problem> pr = saga_problems(doc());
        std::vector<Row> rs;
        for (const Problem &x : pr) {
            Row r;
            r.label = x.what;
            r.tip = x.what;
            int ep = x.ep;
            if (ep >= 0) { r.ok = [ep] { ov_close(); push_page(SC_EPISODE, ep); }; r.okLabel = "Open"; }
            rs.push_back(r);
        }
        if (rs.empty()) { Row r; r.label = "Nothing to fix: every episode is reached and every link leads somewhere."; r.locked = true; rs.push_back(r); }
        open_list("WHAT TO LOOK AT", rs);
    };
    rows.push_back(ck);
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

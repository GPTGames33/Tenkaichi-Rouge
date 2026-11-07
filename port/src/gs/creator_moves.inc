// The Custom Character screen (creator_ui.cpp, which includes this file): the MOVES and FORMS tabs, and the LEARN A
// TECHNIQUE grid's techniques side.
//
// MOVES: per technique slot its move, "its effects" (another move's: beam, ball, explosion) and its camera ("its
// camera"; an Ultimate's "camera (in)", "camera (loop)", "camera (finish)"), each from any fighter's technique; square
// on a move: its TECHNIQUE COLOUR (creator_hsl.inc; the spec's attack_tint, triangle there the detailed editor). The spec
// (port/tools/custom_fighter.py, "Mixed moves"): "moves": {"blast2_1": {"chara", "slot", "effects": {...},
// "camera": {...}}, "ultimate": {..., "camera_in", "camera_loop", "camera_finish"}}. The creator says which moves fit
// (/api/techfit for the grid, /api/mixinfo for the tab): a move's effects that cannot drive it leave it its own effects
// in their colours. The grid offers every BT3 and BT4 fighter and, under ALL, the user's own warriors ("Mine: ..."):
// a warrior's technique is the move its spec names, with what it took from other moves.
// FORMS: up to three transformations (Form 1..3: whom it turns into, its buttons, its cost in blast stocks, its kind, its
// burst colour) and the way back: the spec's forms, form_costs, form_kinds, form_bursts, form_default.

// ---- the creator's word on the moves (/api/mixinfo): which effects fit, which moves are rushes
static J sMix;                  // {"slots": {key: {"effects": true / false / null, "rush"}}, "notes"}
static std::string sMixKey, sMixAsked;
static void mix_refresh(void) {
    J body = J::obj();
    body.set("spec", sSpec);
    std::string key = js_text(sSpec["moves"]) + js_text(moveset()) + js_text(sSpec["look"]) + js_text(sSpec["costumes"]);
    if (key == sMixKey || key == sMixAsked || sScreen != SC_EDIT) { return; }
    sMixAsked = key;
    api("/api/mixinfo", &body, [key](int st, const J &r) {
        if (st == 200 && r.isObj()) { sMix = r; sMixKey = key; }
        if (sMixAsked == key) { sMixAsked.clear(); }
    }, 300000);
}

static const char *const kParts[6] = {"move", "effects", "camera", "camera_in", "camera_loop", "camera_finish"};
static const char *part_label(const std::string &part, int k) {
    if (part == "move") { return kSlotLabel[k]; }
    if (part == "effects") { return "its effects"; }
    if (part == "camera") { return "its camera"; }
    if (part == "camera_in") { return "camera (in)"; }
    if (part == "camera_loop") { return "camera (loop)"; }
    return "camera (finish)";
}
static std::vector<int> part_kinds(const std::string &part, int k) {
    if (part == "move") { return k < 2 ? std::vector<int>{0, 1} : k < 4 ? std::vector<int>{2, 3} : std::vector<int>{4}; }
    if (part == "effects" && k < 2) { return std::vector<int>{0, 1}; } // (a Blast 2's or an Ultimate's effects: from any of the three)
    if (part == "camera_in") { return std::vector<int>{4}; }
    return std::vector<int>{2, 3, 4};
}

static void open_technique_colour(int k, const std::string &shown);
static J sPickedFit; // the /api/techfit answer for the technique just chosen in the grid (its "fits", "why", "finish")

// A move entry of the spec for slot k: the one there, or the moveset's own move.
static J move_entry(int k) {
    const J &mv = sSpec["moves"][kSlots[k]];
    if (mv.isObj() && mv.has("chara")) { return mv; }
    J e = J::obj();
    J ms = moveset();
    e.set("chara", ms.isObj() ? ms["chara"] : ms);
    if (ms["costume"].isNum()) { e.set("costume", ms["costume"]); }
    e.set("slot", J::str(kSlots[k]));
    if (mv.isObj()) { for (size_t i = 0; i < mv.k.size(); i++) { if (mv.k[i] != "chara" && mv.k[i] != "slot" && mv.k[i] != "costume") { e.set(mv.k[i].c_str(), mv.a[i]); } } }
    return e;
}
static bool same_move(const J &a, const J &b, int k) {
    return chara_id(a) == chara_id(b) && a["slot"].text(kSlots[k]) == b["slot"].text(kSlots[k]) && (int)a["costume"].number(1) == (int)b["costume"].number(1);
}
// The entry back into the spec: none when it is the moveset's own move with nothing of another's.
static void set_move_entry(int k, const J &e) {
    bool mixed = false;
    for (int p = 1; p < 6; p++) { mixed |= e.has(kParts[p]); }
    J ms = moveset();
    bool own = chara_id(e) == chara_id(ms) && e["slot"].text(kSlots[k]) == kSlots[k] && (int)e["costume"].number(1) == (int)ms["costume"].number(1);
    if (!mixed && own) {
        J *mv = sSpec.find("moves");
        if (mv != NULL) { mv->erase(kSlots[k]); }
    } else {
        sSpec.at("moves").set(kSlots[k], e);
    }
    clean_empty("moves");
}

// The LEARN A TECHNIQUE grid for `part` of slot k.
static void learn(int k, const std::string &part) {
    std::string label = part_label(part, k);
    open_grid("LEARN A TECHNIQUE", [k, part](const J *r) {
        J e = move_entry(k);
        J fit = sPickedFit;
        if (part == "move") {
            J old = e, n = J::obj();
            if (r != NULL) {
                n = *r;
            } else { // the moveset's own move
                J ms = moveset();
                n.set("chara", ms.isObj() ? ms["chara"] : ms);
                if (ms["costume"].isNum()) { n.set("costume", ms["costume"]); }
                n.set("slot", J::str(kSlots[k]));
            }
            bool brings = false; // (a warrior's technique comes with what it took from others; else this slot's stay)
            for (int p = 1; p < 6; p++) { brings |= n.has(kParts[p]); }
            for (int p = 1; p < 6 && !brings; p++) { if (old.has(kParts[p])) { n.set(kParts[p], old[kParts[p]]); } }
            if (old.has("camera_finish") && same_move(old["camera_finish"], old, k)) { n.erase("camera_finish"); } // (its own: goes with it)
            if (k >= 2 && r != NULL && fit["finish"].truthy() && !n.has("camera_finish")) { n.set("camera_finish", *r); } // (a rush: its own finish)
            set_move_entry(k, n);
            toast(r != NULL ? move_name(n, slot_index(n["slot"].text(kSlots[k]))) + "! Good choice!" : std::string("Back to the moveset's own."));
            if (k >= 2 && r != NULL && !fit.has("finish")) { // (chosen before the creator said: asked now)
                J body = J::obj(), mv = *r;
                body.set("spec", sSpec);
                body.set("slot", J::str(kSlots[k]));
                body.set("what", J::str("move"));
                body.set("chara", mv["chara"]);
                api("/api/techfit", &body, [k, mv](int st, const J &res) {
                    const J &cur = sSpec["moves"][kSlots[k]];
                    if (st != 200 || !same_move(cur, mv, k) || cur.has("camera_finish")) { return; }
                    for (size_t i = 0; i < res["techs"].size(); i++) {
                        if (res["techs"][i]["slot"].text() == mv["slot"].text() && res["techs"][i]["finish"].truthy()) {
                            sSpec.at("moves").at(kSlots[k]).set("camera_finish", mv);
                            sMixKey.clear();
                        }
                    }
                }, 120000);
            }
        } else {
            if (r != NULL) {
                e.set(part.c_str(), *r);
                std::string what = move_name(*r, slot_index((*r)["slot"].text(kSlots[k])));
                if (part == "effects" && !fit["fits"].truthy() && fit.has("fits")) {
                    toast(what + " does not fit this technique: it keeps its own effects, in " + what + "'s colours.", true);
                } else if (part == "effects") {
                    toast(what + "'s effects! Fire it in a fight to see it.");
                } else {
                    toast("Filmed like " + what + ".");
                }
            } else {
                e.erase(part.c_str());
                toast("Its own again.");
            }
            set_move_entry(k, e);
        }
        touch();
        sMixKey.clear();
    }, false, part == "move" ? "Moveset's own" : "Its own", false, 0, part_kinds(part, k));
    Overlay &o = sOv.back();
    o.forWhat = std::string("for: ") + label;
    o.fitSlot = kSlots[k];
    o.fitWhat = part;
}

// ---- the grid's techniques side: the fighter under the cursor and its techniques (by name), with the creator's word
// on whether each fits what is chosen; the gold pill says what the choice is for
static float grid_side_width(const Overlay &o) { return o.moveKinds.empty() || o.stage != 0 ? 0.0f : (sBoxB.x - sBoxA.x) * 0.36f; }
static void grid_pill(const Overlay &o) {
    if (o.forWhat.empty()) { return; }
    ImFont *bold = sBold != NULL ? sBold : sFont;
    float size = S(14), w = text_w(bold, size, o.forWhat.c_str()) + S(40), h = S(26);
    ImVec2 b(sBoxB.x - S(18), sBoxA.y + S(14)), a(b.x - w, b.y);
    ImVec2 bb(b.x, b.y + h);
    grad(a, bb, kGold1, kGold2, h * 0.5f);
    sDl->AddRect(a, bb, IM_COL32(255, 243, 192, 255), h * 0.5f, 0, S(1.5f));
    sDl->AddText(bold, size, ImVec2(a.x + S(20), a.y + (h - size) * 0.5f), IM_COL32(74, 26, 0, 255), o.forWhat.c_str());
}
static void grid_fit_ask(Overlay &o, int chara, const std::string &name) {
    if (o.fitWhat.empty() || o.fitAsked == chara) { return; }
    o.fitAsked = chara;
    J body = J::obj();
    body.set("spec", sSpec);
    body.set("slot", J::str(o.fitSlot));
    body.set("what", J::str(o.fitWhat));
    body.set("chara", J::str(name));
    int id = o.id;
    api("/api/techfit", &body, [id, chara](int st, const J &r) {
        for (Overlay &x : sOv) {
            if (x.id == id && st == 200) { x.fit = r["techs"]; x.fitChara = chara; }
        }
    }, 120000);
}
static const J *grid_fit_of(const Overlay &o, int chara, int k) {
    if (o.fitChara != chara) { return NULL; }
    for (size_t i = 0; i < o.fit.size(); i++) { if (o.fit[i]["slot"].text() == kSlots[k]) { return &o.fit[i]; } }
    return NULL;
}
static void grid_side(Overlay &o, const J *c, ImVec2 a, ImVec2 b) {
    sDl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 12), S(10));
    if (c == NULL) { return; }
    int id = (int)(*c)["id"].number(0);
    bool mine = c->has("warrior"); // (a warrior of the user's: its techniques by name, no word on whether they fit)
    if (o.rest > 8 && !mine) { grid_fit_ask(o, id, (*c)["name"].text()); }
    ImFont *bold = sBold != NULL ? sBold : sFont;
    float y = a.y + S(12), w = b.x - a.x;
    std::string name = (*c)["name"].text();
    float ns = S(19);
    while (ns > S(12) && text_w(bold, ns, name.c_str()) > w - S(16)) { ns -= S(1); }
    outlined(bold, ns, ImVec2((a.x + b.x - text_w(bold, ns, name.c_str())) * 0.5f, y), IM_COL32(255, 214, 70, 255), IM_COL32(60, 20, 0, 255), name, S(1.2f));
    y += ns + S(10);
    float fs = S(64);
    face_at(id, ImVec2((a.x + b.x - fs) * 0.5f, y), ImVec2((a.x + b.x + fs) * 0.5f, y + fs), fs * 0.5f);
    sDl->AddCircle(ImVec2((a.x + b.x) * 0.5f, y + fs * 0.5f), fs * 0.5f, IM_COL32(255, 211, 77, 255), 0, S(2));
    y += fs + S(18);
    for (int k : o.moveKinds) {
        std::string m = (*c)["moves"][(size_t)k].text("-");
        const J *f = mine ? NULL : grid_fit_of(o, id, k);
        bool bad = f != NULL && !(*f)["fits"].truthy();
        ImVec2 pa(a.x + S(12), y), pb(b.x - S(12), y + S(30));
        grad(pa, pb, bad ? IM_COL32(150, 130, 190, 255) : IM_COL32(198, 181, 245, 255), bad ? IM_COL32(90, 70, 140, 255) : IM_COL32(125, 100, 200, 255), S(15));
        float ts = S(15);
        std::string t = m;
        while (t.size() > 3 && text_w(bold, ts, t.c_str()) > pb.x - pa.x - S(16)) { t = t.substr(0, t.size() - 4) + "..."; }
        sDl->AddText(bold, ts, ImVec2((pa.x + pb.x - text_w(bold, ts, t.c_str())) * 0.5f, pa.y + S(7)), IM_COL32(43, 22, 96, 255), t.c_str());
        y += S(34);
        if (bad) {
            std::string why = o.fitWhat == "effects" ? "its colours only: too different" : (*f)["why"].text();
            sDl->AddText(sFont, S(12), ImVec2(pa.x + S(6), y - S(2)), IM_COL32(255, 170, 170, 255), why.c_str(), NULL, pb.x - pa.x);
            y += S(16);
        }
    }
    y += S(6);
    if (o.moveKinds.size() == 3) {
        const char *t = "the third is its Ultimate";
        sDl->AddText(sFont, S(13), ImVec2((a.x + b.x - text_w(sFont, S(13), t)) * 0.5f, y), kDim, t);
        y += S(18);
    }
    const char *t = "Cross: choose from its techniques";
    sDl->AddText(sFont, S(13), ImVec2((a.x + b.x - text_w(sFont, S(13), t)) * 0.5f, y), kGold1, t);
}
// ---- the user's own warriors in the technique grid: each a grid item {"id": its look's (for the face), "name",
// "costumes", "moves": its five techniques' names, "game": "MINE", "warrior": its file, "refs": [the move each slot
// is, as the spec names it: {chara, slot} with its effects and cameras]}
static std::vector<J> sWarriorItems;
static std::string sWarriorKey;
static void warriors_refresh(void) {
    std::string key = js_text(sFighters) + sFile;
    if (key == sWarriorKey) { return; }
    sWarriorKey = key;
    sWarriorItems.clear();
    for (size_t i = 0; i < sFighters.size(); i++) {
        const J &f = sFighters[i];
        const J &sp = f["spec"];
        if (!sp.isObj() || f["file"].text() == sFile) { continue; }
        J lk = sp.has("look") ? sp["look"] : sp["costumes"].size() ? sp["costumes"][0] : sp["base"];
        J ms = sp.has("moveset") ? sp["moveset"] : lk;
        int face = chara_id(sp.has("portrait") ? sp["portrait"] : lk);
        J it = J::obj(), moves = J::arr(), refs = J::arr(), cos = J::arr();
        for (int k = 0; k < 5; k++) {
            const J &mv = sp["moves"][kSlots[k]];
            J r = J::obj();
            const J &who = mv.isObj() && mv.has("chara") ? mv : ms;
            r.set("chara", who.isObj() ? who["chara"] : who);
            if (who["costume"].isNum()) { r.set("costume", who["costume"]); }
            r.set("slot", J::str(mv.isObj() && mv.has("slot") ? mv["slot"].text() : std::string(kSlots[k])));
            if (mv.isObj()) { for (int p = 1; p < 6; p++) { if (mv.has(kParts[p])) { r.set(kParts[p], mv[kParts[p]]); } } }
            moves.push(J::str(sp["names"][kSlots[k]].isStr() ? sp["names"][kSlots[k]].s : move_name(r, slot_index(r["slot"].text()))));
            refs.push(r);
        }
        cos.push(J::num(1));
        it.set("id", J::num(face < 0 ? 0 : face));
        it.set("name", J::str("Mine: " + sp["name"].text(f["file"].text().c_str())));
        it.set("costumes", cos);
        it.set("moves", moves);
        it.set("game", J::str("MINE"));
        it.set("warrior", f["file"]);
        it.set("refs", refs);
        sWarriorItems.push_back(it);
    }
}
static void grid_add_warriors(const Overlay &o, std::vector<const J *> &out) {
    if (o.moveKinds.empty() || o.game != 0) { return; }
    warriors_refresh();
    for (const J &w : sWarriorItems) {
        if (!o.search.empty()) {
            std::string n = w["name"].text(), q = o.search;
            for (char &ch : n) { ch = (char)SDL_tolower((unsigned char)ch); }
            for (char &ch : q) { ch = (char)SDL_tolower((unsigned char)ch); }
            if (n.find(q) == std::string::npos) { continue; }
        }
        out.push_back(&w);
    }
}
static const J *grid_warrior(const std::string &file) {
    warriors_refresh();
    for (const J &w : sWarriorItems) { if (w["warrior"].text() == file) { return &w; } }
    return NULL;
}

// The grid's second step: the techniques of the fighter chosen, as rows (with the creator's word on each).
static void grid_tech_rows(Overlay &o, const J *c, std::vector<Row> &rows) {
    rows.push_back(sec(((*c)["name"].text() + ": WHICH TECHNIQUE?").c_str()));
    int id = (int)(*c)["id"].number(0);
    if ((*c).has("warrior")) { // a warrior of the user's: the move its spec names
        for (int k : o.moveKinds) {
            Row r;
            r.label = kSlotLabel[k];
            r.value = (*c)["moves"][(size_t)k].text("-");
            J ref = (*c)["refs"][(size_t)k];
            r.sub = "from " + nm(ref, "?");
            std::string part = o.fitWhat;
            r.ok = [ref, &o, part] {
                J pick = ref;
                if (part != "move") { for (int p = 1; p < 6; p++) { pick.erase(kParts[p]); } } // (its move, not what it took)
                std::function<void(const J *)> fn = o.picked;
                ov_close();
                sPickedFit = J();
                fn(&pick);
            };
            rows.push_back(r);
        }
        return;
    }
    grid_fit_ask(o, id, (*c)["name"].text());
    for (int k : o.moveKinds) {
        Row r;
        r.label = kSlotLabel[k];
        r.value = (*c)["moves"][(size_t)k].text("-");
        const J *f = grid_fit_of(o, id, k);
        J fit = f != NULL ? *f : J();
        if (f != NULL && !(*f)["fits"].truthy()) {
            r.sub = o.fitWhat == "effects" ? "its colours only" : "does not fit";
            r.tip = (*f)["why"].text();
            r.locked = o.fitWhat != "effects"; // (another's effects that do not fit: still its colours)
        } else if (f == NULL && !o.fitWhat.empty() && o.fitWhat != "move") {
            r.sub = "...";
        }
        r.ok = [k, c, &o, fit] {
            J ref = ref_of(*c);
            ref.set("slot", J::str(kSlots[k]));
            std::function<void(const J *)> fn = o.picked;
            ov_close();
            sPickedFit = fit;
            fn(&ref);
            sPickedFit = J();
        };
        rows.push_back(r);
    }
}

// ---- the MOVES tab
static void moves_rows(std::vector<Row> &rows) {
    mix_refresh();
    rows.push_back(sec("FIGHTING"));
    Row r;
    r.label = "Moveset";
    r.value = nm(moveset(), "?");
    r.sub = "everything below starts from it";
    r.face = chara_id(moveset());
    r.tip = "Stance, animations, stats and the techniques you do not change.";
    r.ok = [] {
        open_grid("WHOSE MOVESET?", [](const J *p) {
            if (!p) { return; }
            J m = J::obj();
            m.set("chara", (*p)["chara"]);
            sSpec.set("moveset", m);
            touch();
        });
    };
    rows.push_back(r);
    static const char *const kDonor[2][3] = {{"Melee & rush", "melee", "Combos, rushes and finishers."},
                                             {"Ki blasts", "ki_blasts", "The ki blasts and charged shots."}};
    for (int i = 0; i < 2; i++) {
        Row d;
        d.label = kDonor[i][0];
        d.value = nm(sSpec[kDonor[i][1]], "the moveset's");
        d.face = chara_id(sSpec[kDonor[i][1]]);
        d.tip = kDonor[i][2];
        std::string key = kDonor[i][1], label = kDonor[i][0];
        for (char &c : label) { c = (char)SDL_toupper((unsigned char)c); }
        d.ok = [key, label] {
            open_grid(label + ": FROM WHOM?", [key](const J *p) {
                if (p) { J m = J::obj(); m.set("chara", (*p)["chara"]); sSpec.set(key.c_str(), m); } else { sSpec.erase(key.c_str()); }
                touch();
            }, false, "Moveset's own");
        };
        rows.push_back(d);
    }
    static const char *const kGroups[3] = {"BLAST 1", "BLAST 2", "ULTIMATE"};
    for (int k = 0; k < 5; k++) {
        if (k == 0 || k == 2 || k == 4) { rows.push_back(sec(kGroups[k / 2])); }
        MoveOf m = move_of(k);
        const J &entry = sSpec["moves"][kSlots[k]];
        const J &info = sMix["slots"][kSlots[k]];
        bool renamed = sSpec["names"][kSlots[k]].isStr();
        bool tinted = sSpec["attack_tint"][kSlots[k]].isObj();
        int nfx = fx_changes(kSlots[k]);
        Row s;
        s.label = kSlotLabel[k];
        s.value = m.shown;
        s.sub = renamed ? "(" + m.name + ", " + nm(m.who, "?") + ")" : "from " + nm(m.who, "?");
        if (tinted || nfx) { s.sub = "coloured - " + s.sub; }
        s.face = chara_id(m.who);
        s.tip = "Whose technique should your warrior learn? Square: its colour, triangle: its name.";
        s.ok = [k] { learn(k, "move"); };
        s.sqLabel = "Colour";
        std::string shown = m.shown;
        s.sq = [k, shown] { open_technique_colour(k, shown); };
        s.triLabel = "Rename";
        std::string own = m.name;
        s.tri = [k, own] {
            open_keys("NAME YOUR TECHNIQUE! (empty: " + own + ")", sSpec["names"][kSlots[k]].text(), 40, [k](const std::string &t) {
                if (!t.empty()) { sSpec.at("names").set(kSlots[k], J::str(t)); }
                else { J *n = sSpec.find("names"); if (n) { n->erase(kSlots[k]); } clean_empty("names"); }
                touch();
            });
        };
        rows.push_back(s);
        // its effects and its cameras
        std::vector<std::string> parts = {"effects"};
        if (k >= 2 && k < 4) { parts.push_back("camera"); }
        if (k == 4) { parts.push_back("camera_in"); parts.push_back("camera_loop"); parts.push_back("camera_finish"); }
        for (const std::string &part : parts) {
            Row p;
            p.label = std::string("   ") + part_label(part, k);
            const J &src = entry[part.c_str()];
            bool rush = info["rush"].truthy();
            if (src.isObj()) {
                int ws = slot_index(src["slot"].text(part == "camera_in" ? "ultimate" : kSlots[k]));
                p.value = move_name(src, ws);
                p.sub = "from " + nm(src, "?");
                p.face = chara_id(src);
                if (part == "effects" && info["effects"].t == J::BOOL && !info["effects"].b) {
                    p.sub = "too different: its own, in its colours";
                }
                if (part == "camera_finish" && same_move(src, entry, k)) { p.value = "its own"; p.sub = ""; p.face = -1; }
                if (part == "camera_finish" && !sMix.nul() && !rush) { p.sub = "not a rush: the battle's camera"; }
                p.sqLabel = "Its own";
                p.sq = [k, part] {
                    J e = move_entry(k);
                    e.erase(part.c_str());
                    set_move_entry(k, e);
                    touch();
                    sMixKey.clear();
                    sound(2);
                };
            } else {
                p.value = "its own";
                if (part == "camera_finish" && !sMix.nul() && !rush) { p.value = "the battle's"; p.sub = "only a rush has a finish"; p.locked = true; }
                if (part == "camera" || part == "camera_loop") { p.sub = ""; }
            }
            if (part == "effects") { p.tip = "Mix it with another super attack's effects: its beam, its ball, its explosion."; }
            else if (part == "camera") { p.tip = "Choose how it is filmed: another super attack's camera."; }
            else if (part == "camera_in") { p.tip = "The Ultimate's opening, filmed like another Ultimate's."; }
            else if (part == "camera_loop") { p.tip = "How it is filmed from the start until it fires."; }
            else { p.tip = "A rush's finish, filmed like another rush's finish."; }
            p.ok = [k, part] { learn(k, part); };
            rows.push_back(p);
        }
    }
    rows.push_back(sec("ATTACK COLOURS"));
    Row sum;
    sum.label = "Recoloured";
    for (int k = 0; k < 7; k++) {
        const char *what = k < 5 ? kSlots[k] : k == 5 ? "ki_blasts" : "charge";
        if (fx_changes(what) || (k < 5 && sSpec["attack_tint"][what].isObj())) { sum.value += (sum.value.empty() ? "" : ", ") + (k < 5 ? move_of(k).shown : std::string(fx_label(what))); }
    }
    if (sum.value.empty()) { sum.value = "nothing yet: square on a technique colours it"; }
    sum.locked = true;
    rows.push_back(sum);
    static const char *const kFx[2][3] = {{"ki_blasts", "Ki blasts", "The ki blasts and their charge glow."},
                                          {"charge", "Ki charge", "The glow and sparks when charging ki (the aura flames are the game's own)."}};
    for (int i = 0; i < 2; i++) {
        Row c;
        c.label = kFx[i][1];
        int nfx = fx_changes(kFx[i][0]);
        c.value = nfx ? std::to_string(nfx) + (nfx > 1 ? " colour changes" : " colour change") : "Original colours";
        c.tip = kFx[i][2];
        std::string what = kFx[i][0];
        c.ok = [what] { open_fx(what); };
        if (nfx) {
            c.sqLabel = "Original colours";
            c.sq = [what] { fx_original(what); sound(2); };
        }
        rows.push_back(c);
    }
    if (sMix["notes"].size()) { // what the builder will do differently
        Row n;
        n.label = "Note";
        n.value = sMix["notes"][(size_t)0].text();
        n.tip = n.value;
        n.locked = true;
        rows.push_back(n);
    }
}

// The TECHNIQUE COLOUR panel of slot k: the spec's attack_tint (Hue / Strength / Light), its own colours, or the
// detailed editor (triangle). It starts on the move's colour: its tint, else its main colour (the creator's answer).
static void open_technique_colour(int k, const std::string &shown) {
    std::string what = kSlots[k];
    const J &t = sSpec["attack_tint"][what.c_str()];
    bool own = !t.isObj();
    float h = (float)t["hue"].number(0), s = (float)t["strength"].number(60) / 100.0f, l = (float)t["light"].number(90) / 100.0f;
    open_hsl("TECHNIQUE COLOUR", shown, own, h, s, l, [what](bool isOwn, float hue, float strength, float light) {
        if (isOwn) {
            J *a = sSpec.find("attack_tint");
            if (a != NULL) { a->erase(what.c_str()); }
            clean_empty("attack_tint");
            fx_original(what);
        } else {
            J v = J::obj();
            v.set("hue", J::num((double)lround(hue)));
            v.set("strength", J::num((double)lround(strength * 100)));
            v.set("light", J::num((double)lround(light * 100)));
            sSpec.at("attack_tint").set(what.c_str(), v);
        }
        touch();
    }, [what, shown] { open_fx(what, shown); });
    if (own) { // (the sliders on its own main colour, when the creator has said which)
        Overlay &o = sOv.back();
        o.loading = true;
        J body = J::obj();
        body.set("spec", sSpec);
        body.set("what", J::str(what));
        body.set("plain", J::boolean(true));
        int id = o.id;
        api("/api/fxcolours", &body, [id](int st, const J &r) {
            for (Overlay &x : sOv) {
                if (x.id != id) { continue; }
                x.loading = false;
                if (st == 200 && x.chosen == 0 && r["tint"].size() == 3) {
                    x.hue = (float)r["tint"][(size_t)0].number(0);
                    x.sat = (float)r["tint"][(size_t)1].number(60) / 100.0f;
                    x.val = (float)r["tint"][(size_t)2].number(90) / 100.0f;
                    x.hslStart[0] = x.hue;
                    x.hslStart[1] = x.sat;
                    x.hslStart[2] = x.val;
                }
            }
        }, 300000);
    }
}

// ---- the FORMS tab
static const char *const kFormDirs[3] = {"up", "left", "right"};
static const char *dir_label(const std::string &d) { return d == "up" ? "Up" : d == "left" ? "Left" : d == "right" ? "Right" : "Down"; }
static const char *const kKinds[4][3] = {
    {"power_up", "Power up", "Powers up with the burst, as the game's transformations do."},
    {"quick", "Quick power up", "The burst, without waiting for the new form."},
    {"big", "Big power up", "A long powering up, the ground shaking."},
    {"instant", "Instant change", "Changes at once, without a burst."},
};
static const struct { const char *name; unsigned rgb; } kBursts[] = {
    {"Purple", 0xa040ff}, {"Red", 0xff3030}, {"Orange", 0xff8a20}, {"Gold", 0xffd040}, {"Green", 0x40e060},
    {"Cyan", 0x40e0ff}, {"Blue", 0x3070ff}, {"Pink", 0xff60c0}, {"White", 0xffffff}, {"Black", 0x303030},
};
enum { BURSTS = (int)(sizeof(kBursts) / sizeof(kBursts[0])) };

// The forms of the warrior in the order the tab shows them: the one of R3 alone first, then up, left, right.
static std::vector<std::string> form_dirs(void) {
    std::vector<std::string> out;
    std::string d = sSpec["form_default"].text();
    if (!d.empty() && !sSpec["forms"][d.c_str()].text().empty()) { out.push_back(d); }
    for (const char *x : kFormDirs) { if (x != d && !sSpec["forms"][x].text().empty()) { out.push_back(x); } }
    return out;
}
// The keys of one direction moved to another (the two swap: forms, costs, kinds, bursts, and R3 alone follows).
static void form_swap(const std::string &a, const std::string &b) {
    static const char *const kKeys[4] = {"forms", "form_costs", "form_kinds", "form_bursts"};
    for (const char *key : kKeys) {
        J va = sSpec[key][a.c_str()], vb = sSpec[key][b.c_str()];
        J *o = sSpec.find(key);
        if (o == NULL) { continue; }
        o->erase(a.c_str());
        o->erase(b.c_str());
        if (!vb.nul()) { o->set(a.c_str(), vb); }
        if (!va.nul()) { o->set(b.c_str(), va); }
    }
    std::string d = sSpec["form_default"].text();
    if (d == a) { sSpec.set("form_default", J::str(b)); } else if (d == b) { sSpec.set("form_default", J::str(a)); }
}
static std::string form_target_name(const std::string &t) {
    if (t.compare(0, 5, "disc:") == 0) {
        const J *c = entry_of(J::num(atoi(t.c_str() + 5)));
        return c != NULL ? (*c)["name"].text() : t;
    }
    return fighter_name(t);
}
static std::string burst_name(const J &v) {
    if (!v.isStr()) { return "The game's own"; }
    unsigned rgb = hex_rgb(v.s);
    for (int i = 0; i < BURSTS; i++) { if (kBursts[i].rgb == rgb) { return kBursts[i].name; } }
    return "Your colour " + v.s;
}
static void forms_rows(std::vector<Row> &rows) {
    rows.push_back(sec("TRANSFORMATIONS"));
    std::vector<std::string> dirs = form_dirs();
    std::string def = sSpec["form_default"].text();
    for (int n = 0; n < 3; n++) {
        bool have = n < (int)dirs.size();
        std::string d;
        if (have) { d = dirs[(size_t)n]; }
        else { for (const char *x : kFormDirs) { if (std::find(dirs.begin(), dirs.end(), std::string(x)) == dirs.end()) { d = x; break; } } }
        bool first = n == (int)dirs.size(); // (the first empty one: where a new form goes)
        Row f;
        f.label = "Form " + std::to_string(n + 1);
        std::string t = sSpec["forms"][d.c_str()].text();
        f.value = have ? "Turns into " + form_target_name(t) : "None";
        if (have && t.compare(0, 5, "disc:") == 0) { f.face = atoi(t.c_str() + 5); }
        f.tip = "Transform into another warrior you made, a new form of this one, or any fighter.";
        f.locked = !have && !first;
        f.ok = [d, have] {
            if (!have && sSpec["form_default"].text().empty()) { sSpec.set("form_default", J::str(d)); }
            if (!have) { // (a new form: the usual power up, a blast stock)
                if (!sSpec["form_kinds"].has(d.c_str())) { sSpec.at("form_kinds").set(d.c_str(), J::str("power_up")); }
                if (!sSpec["form_costs"].has(d.c_str())) { sSpec.at("form_costs").set(d.c_str(), J::num(1)); }
            }
            pick_form(d.c_str());
        };
        if (have) {
            f.sqLabel = "Remove";
            f.sq = [d] {
                static const char *const kKeys[4] = {"forms", "form_costs", "form_kinds", "form_bursts"};
                for (const char *key : kKeys) { J *o = sSpec.find(key); if (o) { o->erase(d.c_str()); } clean_empty(key); }
                if (sSpec["form_default"].text() == d) { sSpec.erase("form_default"); }
                touch();
                sound(2);
            };
        }
        rows.push_back(f);
        // its buttons, cost, kind and burst
        Row b;
        b.label = "   buttons";
        b.locked = !have;
        b.value = !have ? "-" : (d == def || (dirs.size() == 1 && def.empty())) ? std::string("R3 (or R3 + ") + dir_label(d) + ")" : std::string("R3 + ") + dir_label(d);
        b.tip = "Left / right: which way of R3 it is. Cross: R3 alone does this one.";
        if (have) {
            b.lrLabel = "Which way";
            b.lr = [d](int k) {
                int i = 0;
                while (i < 3 && d != kFormDirs[i]) { i++; }
                std::string to = kFormDirs[(i + (k > 0 ? 1 : 2)) % 3];
                form_swap(d, to);
                touch();
                sound(0);
            };
            b.ok = [d] { sSpec.set("form_default", J::str(d)); touch(); toast(std::string("R3 alone: this form (and R3 + ") + dir_label(d) + ")."); };
        }
        rows.push_back(b);
        Row c;
        c.label = "   blast stocks";
        c.locked = !have;
        int cost = (int)sSpec["form_costs"][d.c_str()].number(0);
        c.value = have ? std::to_string(cost) : "-";
        c.tip = "How many blast stocks it costs. Left / right.";
        if (have) {
            c.lrLabel = "Cost";
            c.lr = [d](int k) {
                int v = (int)sSpec["form_costs"][d.c_str()].number(0) + k;
                v = v < 0 ? 0 : v > 7 ? 7 : v;
                sSpec.at("form_costs").set(d.c_str(), J::num(v));
                touch();
                sound(0);
            };
        }
        rows.push_back(c);
        Row kd;
        kd.label = "   kind";
        kd.locked = !have;
        std::string kind = sSpec["form_kinds"][d.c_str()].text();
        int ki = -1;
        for (int i = 0; i < 4; i++) { if (kind == kKinds[i][0]) { ki = i; } }
        kd.value = !have ? "-" : ki >= 0 ? kKinds[ki][1] : "the moveset's own";
        kd.tip = ki >= 0 ? kKinds[ki][2] : "How it transforms. Left / right.";
        if (have) {
            kd.lrLabel = "Kind";
            kd.lr = [d, ki](int k) {
                int i = ((ki < 0 ? (k > 0 ? -1 : 0) : ki) + k + 4) % 4;
                sSpec.at("form_kinds").set(d.c_str(), J::str(kKinds[i][0]));
                touch();
                sound(0);
            };
        }
        rows.push_back(kd);
        Row u;
        u.label = "   burst colour";
        u.locked = !have;
        const J &bv = sSpec["form_bursts"][d.c_str()];
        u.value = have ? burst_name(bv) : "-";
        if (have && bv.isStr()) { u.sub = ki == 3 ? "an instant change has no burst" : ""; }
        u.tip = "The colour of the transformation's aura (the burst as it powers up). Left and right! Cross: any colour.";
        if (have) {
            u.lrLabel = "Colour";
            u.lr = [d](int k) {
                const J &v = sSpec["form_bursts"][d.c_str()];
                int at = -1; // -1 the game's own
                if (v.isStr()) { at = BURSTS; for (int i = 0; i < BURSTS; i++) { if (kBursts[i].rgb == hex_rgb(v.s)) { at = i; } } }
                at = at == BURSTS ? (k > 0 ? -1 : BURSTS - 1) : at + k;
                if (at < -1) { at = BURSTS - 1; }
                if (at >= BURSTS) { at = -1; }
                if (at < 0) { J *o = sSpec.find("form_bursts"); if (o) { o->erase(d.c_str()); } clean_empty("form_bursts"); }
                else { sSpec.at("form_bursts").set(d.c_str(), J::str(hex_of(kBursts[at].rgb))); }
                touch();
                sound(0);
            };
            u.ok = [d] {
                const J &v = sSpec["form_bursts"][d.c_str()];
                float h = 280, s = 0.75f, l = 1.0f;
                if (v.isStr()) { rgb_hsv(hex_rgb(v.s), &h, &s, &l); }
                open_hsl("BURST COLOUR", std::string("Form: R3 + ") + dir_label(d), !v.isStr(), h, s, l, [d](bool own, float hue, float strength, float light) {
                    if (own) { J *o = sSpec.find("form_bursts"); if (o) { o->erase(d.c_str()); } clean_empty("form_bursts"); }
                    else { sSpec.at("form_bursts").set(d.c_str(), J::str(hex_of(hsv_rgb(hue, strength, light)))); }
                    touch();
                }, nullptr);
            };
        }
        rows.push_back(u);
    }
    rows.push_back(sec("THE WAY BACK"));
    Row w;
    w.label = "R3 + Down";
    std::string t = sSpec["forms"]["down"].text();
    w.value = t.empty() ? "back to where it came from" : form_target_name(t);
    if (t.compare(0, 5, "disc:") == 0) { w.face = atoi(t.c_str() + 5); }
    w.tip = "Save to make it so. Give that warrior a way back too!";
    w.ok = [] { pick_form("down"); };
    rows.push_back(w);
}

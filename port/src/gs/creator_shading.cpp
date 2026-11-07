// The Custom Character screen's SHADING (PARTS: "Shading..."): the warrior's parts (its model's own: face, chest,
// waist, ..., hair) and for the one chosen its shadow colour (SHADE COLOUR: the Hue / Strength / Light panel of
// creator_hsl.cpp), the shading of another part, the Super Saiyan glow, the outline. The spec's "shading" (see
// port/tools/custom_fighter.py, port/tools/creator/shade.py); the parts come from the creator (/api/shading) and the
// model is built again after each change, as with every other change of the look.
//
// Part of creator_ui.cpp: that file includes this one (with CREATOR_UI_PARTS defined) after creator_hsl.cpp.
// Compiled on its own (the build compiles every .cpp of port/src/gs) it is empty.
#ifdef CREATOR_UI_PARTS

enum { SH_SHADOW, SH_LIKE, SH_GLOW, SH_OUTLINE, SH_ROWS };

static void shading_load(Overlay *o);
static void open_shading(void) {
    Overlay o;
    o.kind = OV_SHADING;
    o.title = "SHADING";
    o.node = -1; // the cursor in the list of parts (0.. its rows)
    ov_push(o);
    shading_load(&sOv.back());
    sound(1);
}
static void shading_load(Overlay *o) {
    o->loading = true;
    J body = J::obj();
    body.set("spec", sSpec);
    body.set("costume", J::num(sShownCostume));
    int depth = (int)sOv.size();
    api("/api/shading", &body, [depth](int st, const J &r) {
        if ((int)sOv.size() < depth || sOv[depth - 1].kind != OV_SHADING) { return; } // (closed meanwhile)
        Overlay &ov = sOv[depth - 1];
        ov.loading = false;
        if (st != 200) { toast(error_of(st, r), true); return; }
        ov.info = r;
        if (ov.cur >= (int)r["parts"].size()) { ov.cur = 0; }
    }, 300000);
}

// (no shading left: costume 1 drops the key; a costume of its own keeps an empty one, which means "the game's")
static void shade_clean(void) {
    if (edit_costume() == 0) { clean_empty("shading"); }
}
// the spec's "shading" of a part
static const J &shade_get(const std::string &part, const char *what) { return edit_view()["shading"][part.c_str()][what]; }
static void shade_set(const std::string &part, const char *what, const J &v) {
    J &all = edit_spec().at("shading");
    J &p = all.at(part.c_str());
    if (v.nul()) { p.erase(what); } else { p.set(what, v); }
    if (p.size() == 0) { all.erase(part.c_str()); }
    shade_clean();
    touch();
}
static std::string shade_label(const Overlay &o, const std::string &key) {
    const J &ps = o.info["parts"];
    for (size_t i = 0; i < ps.size(); i++) {
        if (ps[i]["key"].text() == key) {
            std::string l = ps[i]["label"].text();
            if (key == "hair" && o.info["layer"].truthy()) { l += " (layer)"; }
            return l;
        }
    }
    return key;
}

// the four settings of a part as rows (value, and what left / right / cross / square do)
static void shading_rows(Overlay &o, const J &part, std::vector<Row> &rows) {
    std::string key = part["key"].text();
    const J &ps = o.info["parts"];
    {
        Row r;
        r.label = "Shadow colour";
        const J &c = shade_get(key, "shadow");
        r.value = c.isStr() ? "your colour" : "the game's";
        r.sub = c.isStr() ? c.text() : "";
        r.tip = "Recolour the shadows themselves: any colour you like!";
        std::string label = shade_label(o, key);
        unsigned own = hex_rgb(part["own_shadow"].text("#404040"));
        r.ok = [key, label, own] {
            const J &cur = shade_get(key, "shadow");
            open_hsl("SHADE COLOUR", label, cur.isStr() ? (int)hex_rgb(cur.text()) : -1, own, "Its own colours", [key](int rgb) {
                shade_set(key, "shadow", rgb >= 0 ? J::str(hex_of((unsigned)rgb)) : J());
            });
        };
        r.sq = [key] { shade_set(key, "shadow", J()); sound(2); };
        rows.push_back(r);
    }
    {
        Row r;
        r.label = "Same shading as";
        std::string like = shade_get(key, "like").text();
        r.value = like.empty() ? "its own" : shade_label(o, like);
        r.tip = "Left and right: shaded just like another part. All the way round: its own again.";
        std::vector<std::string> ring = {""}; // its own, then every other part
        for (size_t i = 0; i < ps.size(); i++) { if (ps[i]["key"].text() != key) { ring.push_back(ps[i]["key"].text()); } }
        int at = 0;
        for (size_t i = 0; i < ring.size(); i++) { if (ring[i] == like) { at = (int)i; } }
        r.lr = [key, ring, at](int d) {
            int n = (int)ring.size(), k = ((at + d) % n + n) % n;
            shade_set(key, "like", ring[(size_t)k].empty() ? J() : J::str(ring[(size_t)k]));
            sound(0);
        };
        r.ok = [key, ring, at] {
            int k = (at + 1) % (int)ring.size();
            shade_set(key, "like", ring[(size_t)k].empty() ? J() : J::str(ring[(size_t)k]));
        };
        r.sq = [key] { shade_set(key, "like", J()); sound(2); };
        rows.push_back(r);
    }
    {
        Row r;
        r.label = "Glow";
        const J &g = shade_get(key, "glow");
        int own = (int)part["glow"].number(0);
        r.value = g.nul() ? "its own" : g.truthy() ? "Super Saiyan glow" : "none";
        r.sub = g.nul() ? (own > 0 ? "glows" : "no glow") : "";
        r.tip = "The Super Saiyan glow: on any part, of any warrior.";
        auto step = [key](int d) { // its own -> on -> off -> its own
            const J &cur = shade_get(key, "glow");
            int at = cur.nul() ? 0 : cur.truthy() ? 1 : 2;
            at = ((at + d) % 3 + 3) % 3;
            shade_set(key, "glow", at == 0 ? J() : J::boolean(at == 1));
            sound(0);
        };
        r.lr = step;
        r.ok = [step] { step(1); };
        r.sq = [key] { shade_set(key, "glow", J()); sound(2); };
        rows.push_back(r);
    }
    {
        Row r;
        r.label = "Outline";
        bool none = shade_get(key, "outline").text() == "none";
        r.value = none ? "none" : "with the body";
        r.tip = "The black line round your warrior: drawn round this part with the body, or left off it.";
        auto flip = [key] {
            bool n = shade_get(key, "outline").text() == "none";
            shade_set(key, "outline", n ? J() : J::str("none"));
            sound(0);
        };
        r.lr = [flip](int) { flip(); };
        r.ok = flip;
        r.sq = [key] { shade_set(key, "outline", J()); sound(2); };
        rows.push_back(r);
    }
}

static std::string shading_tip(const Overlay &o) {
    const J &ps = o.info["parts"];
    if (o.node < 0 || o.cur >= (int)ps.size()) { return "Borrow any part's shading. Add the Super Saiyan glow. Recolour the shadows themselves."; }
    std::vector<Row> rows;
    shading_rows(const_cast<Overlay &>(o), ps[(size_t)o.cur], rows);
    return o.node < (int)rows.size() ? rows[(size_t)o.node].tip : "";
}

static void shading_frame(Overlay &o, std::vector<Prompt> &ps) {
    ImFont *bold = sBold != NULL ? sBold : sFont;
    float top0 = sBoxA.y + S(56);
    const J &parts = o.info["parts"];
    int n = (int)parts.size();
    if (o.loading && n == 0) {
        sDl->AddText(sFont, S(18), ImVec2(sBoxA.x + S(24), top0 + S(10)), kInk, "Looking at your warrior's parts...");
    }
    ps.push_back({B_CROSS, "Choose"});
    ps.push_back({B_SQUARE, "As it was"});
    ps.push_back({B_CIRCLE, "Back"});
    if (n == 0) {
        if (sIn.back) { ov_close(); sound(2); }
        return;
    }
    if (o.cur >= n) { o.cur = n - 1; }
    // the parts on the left, the chosen one's settings on the right
    float split = sBoxA.x + (sBoxB.x - sBoxA.x) * 0.36f;
    ImVec2 la(sBoxA.x + S(14), top0), lb(split - S(6), sBoxB.y - S(14));
    sDl->AddRectFilled(la, lb, IM_COL32(0, 0, 0, 60), S(10));
    float rh = S(30), gap = S(3);
    int fits = std::max(1, (int)((lb.y - la.y - S(12)) / (rh + gap)));
    if (o.cur < o.top) { o.top = o.cur; }
    if (o.cur >= o.top + fits) { o.top = o.cur - fits + 1; }
    int clickedPart = -1;
    for (int i = o.top; i < n && i < o.top + fits; i++) {
        float y = la.y + S(6) + (i - o.top) * (rh + gap);
        ImVec2 a(la.x + S(6), y), b(lb.x - S(6), y + rh);
        std::string key = parts[(size_t)i]["key"].text();
        bool sel = i == o.cur;
        if (sel && o.node < 0) { grad(a, b, kGold1, kGold2, S(6)); }
        else if (sel) { sDl->AddRect(a, b, IM_COL32(255, 211, 77, 255), S(6), 0, S(2)); }
        std::string label = shade_label(o, key);
        text(sFont, S(16), ImVec2(a.x + S(10), y + (rh - S(16)) * 0.5f), sel && o.node < 0 ? kDark : kInk, label, b.x - a.x - S(34));
        if (edit_view()["shading"][key.c_str()].size()) { // (changed: a dot, in its shadow colour when it has one)
            const J &c = shade_get(key, "shadow");
            sDl->AddCircleFilled(ImVec2(b.x - S(12), y + rh * 0.5f), S(5), c.isStr() ? col32(hex_rgb(c.text())) : kGold1);
        }
        if (click_in(a, b)) { clickedPart = i; }
    }
    const J &part = parts[(size_t)o.cur];
    std::string key = part["key"].text();
    std::vector<Row> rows;
    shading_rows(o, part, rows);
    // the right: the part's name, its four rows
    ImVec2 ra(split + S(6), top0), rb(sBoxB.x - S(14), sBoxB.y - S(14));
    std::string head = shade_label(o, key);
    float hw = text_w(bold, S(19), head.c_str());
    sDl->AddText(bold, S(19), ImVec2((ra.x + rb.x - hw) * 0.5f, ra.y + S(4)), IM_COL32(255, 210, 63, 255), head.c_str());
    int clickedRow = -1;
    float y = ra.y + S(40), rrh = S(36);
    for (int i = 0; i < (int)rows.size(); i++) {
        ImVec2 a(ra.x, y), b(rb.x, y + rrh);
        bool sel = i == o.node;
        if (sel) { grad(a, b, kGold1, kGold2, rrh * 0.5f); } else { sDl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 14), rrh * 0.5f); }
        ImU32 ink = sel ? kDark : kInk;
        float lw = (rb.x - ra.x) * 0.46f;
        text(sFont, S(16), ImVec2(a.x + S(14), y + (rrh - S(16)) * 0.5f), ink, rows[(size_t)i].label, lw - S(14));
        float vx = a.x + lw;
        if (i == SH_SHADOW && shade_get(key, "shadow").isStr()) { // (the colour itself)
            ImVec2 c(vx + S(10), y + rrh * 0.5f);
            sDl->AddCircleFilled(c, S(9), col32(hex_rgb(shade_get(key, "shadow").text())));
            sDl->AddCircle(c, S(9), sel ? kDark : IM_COL32(255, 255, 255, 200), 0, S(1.5f));
            vx += S(26);
        }
        text(bold, S(16), ImVec2(vx, y + (rrh - S(16)) * 0.5f), sel ? kDark : IM_COL32(255, 255, 255, 255), rows[(size_t)i].value, b.x - vx - S(12));
        if (!rows[(size_t)i].sub.empty() && i != SH_SHADOW) {
            float sw = text_w(sFont, S(12), rows[(size_t)i].sub.c_str());
            sDl->AddText(sFont, S(12), ImVec2(b.x - sw - S(16), y + (rrh - S(12)) * 0.5f), sel ? IM_COL32(107, 58, 0, 255) : kDim, rows[(size_t)i].sub.c_str());
        }
        if (click_in(a, b)) { clickedRow = i; }
        y += rrh + S(6);
    }
    // the input: the list of parts, or (after cross) the chosen part's rows
    if (clickedPart >= 0) { o.cur = clickedPart; o.node = -1; sound(0); return; }
    if (clickedRow >= 0) { o.node = clickedRow; sIn.ok = true; }
    if (o.node < 0) {
        int was = o.cur;
        if (sIn.up) { o.cur = (o.cur + n - 1) % n; }
        if (sIn.down) { o.cur = (o.cur + 1) % n; }
        if (o.cur != was) { sound(0); }
        if (sIn.ok || sIn.right) { o.node = 0; sound(1); return; }
        if (sIn.sq && edit_view()["shading"][key.c_str()].size()) { // the whole part as the game has it
            edit_spec().at("shading").erase(key.c_str());
            shade_clean();
            touch();
            sound(2);
            return;
        }
        if (sIn.back) { ov_close(); sound(2); }
        return;
    }
    if (sIn.back) { o.node = -1; sound(2); return; }
    int was = o.node;
    if (sIn.up) { o.node = (o.node + SH_ROWS - 1) % SH_ROWS; }
    if (sIn.down) { o.node = (o.node + 1) % SH_ROWS; }
    if (o.node != was) { sound(0); return; }
    Row &r = rows[(size_t)o.node];
    if (sIn.ok && r.ok) { std::function<void()> f = r.ok; f(); return; }
    if ((sIn.left || sIn.right) && r.lr) { std::function<void(int)> f = r.lr; f(sIn.right ? 1 : -1); return; }
    if (sIn.sq && r.sq) { std::function<void()> f = r.sq; f(); return; }
}

#endif // CREATOR_UI_PARTS

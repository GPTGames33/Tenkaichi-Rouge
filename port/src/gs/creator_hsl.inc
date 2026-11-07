// The Custom Character screen (creator_ui.cpp, which includes this file): the TECHNIQUE COLOUR panel, a reusable
// Hue / Strength / Light overlay.
//
//   open_tech_hsl(title, name, own, hue, strength, light, changed, more)
//
// hue in degrees, strength and light 0..1; `own`: the thing has its own colours now (no colour of the player's).
// `changed(own, hue, strength, light)` is called at every change (so what it colours follows the sliders), and once
// more with the starting values when the panel is left with Back. `more` (may be empty): triangle, e.g. the detailed
// colour-by-colour editor. Its rows: the colour as it is, Hue, Strength, Light, two rows of preset colours, "Its own
// colours". Cross chooses (a preset, its own colours), START is OK, square its own colours, circle Back.

// (a technique's: the presets are colours an attack looks good in; the second row lighter, darker and greys)
static const unsigned kHslPresets[2][12] = {
    {0xd42020, 0xf06010, 0xf0b020, 0xe8e030, 0x60d030, 0x20b060, 0x20c8c8, 0x2090f0, 0x2040e0, 0x6030e0, 0xa030e8, 0xe030b0},
    {0xff8080, 0xffc080, 0xfff0a0, 0xa0ffa0, 0xa0f0ff, 0xa0b0ff, 0xd8a0ff, 0xffa0e0, 0xffffff, 0x909090, 0x404040, 0x101010},
};
enum { THSL_SWATCH, THSL_HUE, THSL_STRENGTH, THSL_LIGHT, THSL_ROW1, THSL_ROW2, THSL_OWN, THSL_ROWS };
static int sHslDrag = -1; // the slider the mouse holds

static void open_tech_hsl(const std::string &title, const std::string &name, bool own, float hue, float strength, float light,
                     std::function<void(bool, float, float, float)> changed, std::function<void()> more) {
    Overlay o;
    o.kind = OV_TECHHSL;
    o.title = title;
    o.text = name;
    o.hslOwn = o.hslStartOwn = own;
    o.hue = hue;
    o.sat = strength;
    o.val = light;
    o.hslStart[0] = hue;
    o.hslStart[1] = strength;
    o.hslStart[2] = light;
    o.hslChanged = changed;
    o.hslMore = more;
    o.cur = THSL_HUE;
    o.texture = 0; // (the preset under the cursor)
    ov_push(o);
    sHslDrag = -1;
    sound(1);
}

// The colour the sliders make (the "Your colour" swatch).
static unsigned hsl_rgb(const Overlay &o) { return hsv_rgb(o.hue, o.sat, o.val); }

static void tech_hsl_frame(Overlay &o, std::vector<Prompt> &ps) {
    ImGuiIO &io = ImGui::GetIO();
    ImFont *bold = sBold != NULL ? sBold : sFont;
    float x0 = sBoxA.x + S(22), x1 = sBoxB.x - S(22), y = sBoxA.y + S(56);
    bool touched = false, own = false;
    // the move's name
    if (!o.text.empty()) {
        float size = S(19);
        sDl->AddText(bold, size, ImVec2((x0 + x1 - text_w(bold, size, o.text.c_str())) * 0.5f, y), IM_COL32(255, 226, 122, 255), o.text.c_str());
        y += S(32);
    }
    float rh = S(44), labelW = S(120);
    float rowY[THSL_ROWS];
    for (int i = 0; i < THSL_ROWS; i++) { rowY[i] = y + i * rh + (i >= THSL_ROW1 ? S(10) : 0) + (i == THSL_OWN ? S(10) : 0); }
    // the input: rows, sliders, presets
    int was = o.cur;
    if (sIn.up) { o.cur = o.cur > THSL_HUE ? o.cur - 1 : THSL_OWN; }
    if (sIn.down) { o.cur = o.cur < THSL_OWN ? o.cur + 1 : THSL_HUE; }
    if (o.cur != was) { sound(0); }
    float step = sIn.fast ? 0.08f : 0.02f;
    int lr = (sIn.right ? 1 : 0) - (sIn.left ? 1 : 0);
    if (lr != 0) {
        if (o.cur == THSL_HUE) { o.hue = fmodf(o.hue + lr * step * 180.0f + 360.0f, 360.0f); touched = true; }
        else if (o.cur == THSL_STRENGTH) { o.sat += lr * step; touched = true; }
        else if (o.cur == THSL_LIGHT) { o.val += lr * step; touched = true; }
        else if (o.cur == THSL_ROW1 || o.cur == THSL_ROW2) { o.texture = (o.texture + lr + 12) % 12; sound(0); }
    }
    // the swatch: the colour as it is
    {
        float r = S(17);
        ImVec2 c(x0 + r + S(4), rowY[THSL_SWATCH] + rh * 0.5f);
        if (o.hslOwn) {
            for (int k = 0; k < 4; k++) { // (its own: a little wheel of colours)
                sDl->PathArcTo(c, r, k * 1.5708f, (k + 1) * 1.5708f, 8);
                sDl->PathLineTo(c);
                sDl->PathFillConvex(col32(hsv_rgb(k * 90.0f + 20.0f, 0.7f, 0.95f)));
            }
        } else {
            sDl->AddCircleFilled(c, r, col32(hsl_rgb(o)));
        }
        sDl->AddCircle(c, r + S(1), IM_COL32(255, 255, 255, 220), 0, S(2));
        sDl->AddText(sFont, S(18), ImVec2(c.x + r + S(16), c.y - S(10)), kInk, o.hslOwn ? "Its own colours" : "Your colour");
        if (o.loading) { sDl->AddText(sFont, S(14), ImVec2(x1 - S(170), c.y - S(8)), kDim, "reading its colours..."); }
    }
    // the sliders
    static const char *const kLabel[3] = {"Hue", "Strength", "Light"};
    for (int i = THSL_HUE; i <= THSL_LIGHT; i++) {
        float ry = rowY[i];
        ImVec2 ra(x0 - S(8), ry + S(2)), rb(x1 + S(8), ry + rh - S(2));
        bool sel = o.cur == i;
        if (sel) { sDl->AddRectFilled(ra, rb, IM_COL32(255, 211, 77, 60), rh * 0.5f); }
        sDl->AddText(bold, S(17), ImVec2(x0, ry + rh * 0.5f - S(10)), sel ? IM_COL32(255, 226, 122, 255) : kInk, kLabel[i - THSL_HUE]);
        ImVec2 ta(x0 + labelW, ry + rh * 0.5f - S(8)), tb(x1 - S(60), ry + rh * 0.5f + S(8));
        const int N = 24;
        for (int k = 0; k < N; k++) { // the track: what that slider goes through
            float f0 = (float)k / N, f1 = (float)(k + 1) / N;
            unsigned c0, c1;
            if (i == THSL_HUE) { c0 = hsv_rgb(f0 * 360.0f, 1, 1); c1 = hsv_rgb(f1 * 360.0f, 1, 1); }
            else if (i == THSL_STRENGTH) { c0 = hsv_rgb(o.hue, f0, std::max(o.val, 0.35f)); c1 = hsv_rgb(o.hue, f1, std::max(o.val, 0.35f)); }
            else { c0 = hsv_rgb(o.hue, o.sat, f0); c1 = hsv_rgb(o.hue, o.sat, f1); }
            float xa = ta.x + (tb.x - ta.x) * f0, xb = ta.x + (tb.x - ta.x) * f1 + 0.5f;
            sDl->AddRectFilledMultiColor(ImVec2(xa, ta.y), ImVec2(xb, tb.y), col32(c0), col32(c1), col32(c1), col32(c0));
        }
        sDl->AddRect(ta, tb, IM_COL32(255, 255, 255, 120), S(3), 0, S(1.5f));
        float f = i == THSL_HUE ? o.hue / 360.0f : i == THSL_STRENGTH ? o.sat : o.val;
        f = f < 0 ? 0 : f > 1 ? 1 : f;
        ImVec2 k(ta.x + (tb.x - ta.x) * f, (ta.y + tb.y) * 0.5f);
        sDl->AddCircleFilled(k, S(10), i == THSL_HUE ? col32(hsv_rgb(o.hue, 1, 1)) : kGold1);
        sDl->AddCircle(k, S(10), IM_COL32(60, 25, 0, 255), 0, S(2.5f));
        char v[16];
        if (i == THSL_HUE) { snprintf(v, sizeof(v), "%d", (int)lround(o.hue)); } else { snprintf(v, sizeof(v), "%d%%", (int)lround(f * 100)); }
        sDl->AddText(sFont, S(16), ImVec2(tb.x + S(12), ry + rh * 0.5f - S(9)), kDim, v);
        if (sMouse && ImGui::IsMouseClicked(0) && ImGui::IsMouseHoveringRect(ImVec2(ta.x - S(10), ra.y), ImVec2(tb.x + S(10), rb.y), false)) { sHslDrag = i; o.cur = i; }
        if (sHslDrag == i) {
            float m = (io.MousePos.x - ta.x) / (tb.x - ta.x);
            m = m < 0 ? 0 : m > 1 ? 1 : m;
            if (i == THSL_HUE) { o.hue = m * 359.9f; } else if (i == THSL_STRENGTH) { o.sat = m; } else { o.val = m; }
            touched = true;
        }
    }
    if (!ImGui::IsMouseDown(0)) { sHslDrag = -1; }
    // the presets
    int pickRow = -1, pickCol = -1;
    for (int row = 0; row < 2; row++) {
        float ry = rowY[THSL_ROW1 + row];
        float s0 = std::min(S(34), (x1 - x0 - S(11) * 11) / 12.0f);
        for (int c = 0; c < 12; c++) {
            ImVec2 cc(x0 + s0 * 0.5f + c * (s0 + S(11)), ry + rh * 0.5f);
            sDl->AddCircleFilled(cc, s0 * 0.5f, col32(kHslPresets[row][c]));
            bool sel = o.cur == THSL_ROW1 + row && o.texture == c;
            sDl->AddCircle(cc, s0 * 0.5f + S(2), sel ? IM_COL32(255, 226, 122, 255) : IM_COL32(255, 255, 255, 90), 0, sel ? S(4) : S(1.5f));
            if (click_in(ImVec2(cc.x - s0 * 0.5f, cc.y - s0 * 0.5f), ImVec2(cc.x + s0 * 0.5f, cc.y + s0 * 0.5f))) { pickRow = row; pickCol = c; }
        }
    }
    // its own colours
    {
        float ry = rowY[THSL_OWN];
        ImVec2 ra(x0 - S(8), ry + S(2)), rb(x1 + S(8), ry + rh - S(2));
        bool sel = o.cur == THSL_OWN;
        if (sel) { grad(ra, rb, kGold1, kGold2, rh * 0.5f); } else { sDl->AddRectFilled(ra, rb, IM_COL32(255, 255, 255, 16), rh * 0.5f); }
        sDl->AddText(sFont, S(18), ImVec2(x0 + S(6), ry + rh * 0.5f - S(10)), sel ? kDark : kInk, "Its own colours");
        if (click_in(ra, rb)) { o.cur = THSL_OWN; own = true; }
    }
    if (pickRow >= 0) { o.cur = THSL_ROW1 + pickRow; o.texture = pickCol; sIn.ok = true; }
    if (sIn.ok) {
        if (o.cur == THSL_ROW1 || o.cur == THSL_ROW2) {
            rgb_hsv(kHslPresets[o.cur - THSL_ROW1][o.texture], &o.hue, &o.sat, &o.val);
            touched = true;
            sound(1);
        } else if (o.cur == THSL_OWN) {
            own = true;
        }
    }
    if (sIn.sq) { own = true; }
    o.sat = o.sat < 0 ? 0 : o.sat > 1 ? 1 : o.sat;
    o.val = o.val < 0 ? 0 : o.val > 1 ? 1 : o.val;
    if (own) {
        o.hslOwn = true;
        sound(2);
        if (o.hslChanged) { o.hslChanged(true, o.hue, o.sat, o.val); }
    } else if (touched) {
        o.hslOwn = false;
        o.chosen = 1; // (the player has changed it: a late answer of what it was does not move the sliders)
        if (o.hslChanged) { o.hslChanged(false, o.hue, o.sat, o.val); }
    }
    ps.push_back({B_CROSS, "Choose"});
    ps.push_back({B_START, "OK"});
    ps.push_back({B_SQUARE, "Its own"});
    if (o.hslMore) { ps.push_back({B_TRIANGLE, "Detailed"}); }
    ps.push_back({B_CIRCLE, "Back"});
    if (sIn.start) { ov_close(); sound(1); return; }
    if (sIn.tri && o.hslMore) {
        std::function<void()> m = o.hslMore;
        ov_close();
        m();
        return;
    }
    if (sIn.back) { // (as it was)
        std::function<void(bool, float, float, float)> f = o.hslChanged;
        bool startOwn = o.hslStartOwn;
        float h = o.hslStart[0], s = o.hslStart[1], l = o.hslStart[2];
        ov_close();
        sound(2);
        if (f) { f(startOwn, h, s, l); }
    }
}

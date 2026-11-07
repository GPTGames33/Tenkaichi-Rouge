// The Custom Character screen's colour panel: Hue / Strength / Light (a rainbow, a saturation and a brightness bar),
// "Your colour", two rows of ready colours and "Its own colours" - the trailer's SHADE COLOUR screen, made to be used
// for any colour (open_hsl). Every change is handed on at once (the model is built again as it rests); Back hands
// back the colour it opened with.
//
// Part of creator_ui.cpp: that file includes this one (with CREATOR_UI_PARTS defined) after its colour wheel, so
// its lists, overlays, drawing helpers and input are this file's too. Compiled on its own (the build compiles every
// .cpp of port/src/gs) it is empty.
#ifdef CREATOR_UI_PARTS

// rows of the panel: the three bars, the two rows of ready colours, its own colours (when it has one)
enum { HSL_HUE, HSL_SAT, HSL_LIGHT, HSL_ROW1, HSL_ROW2, HSL_OWN, HSL_ROWS };
static const unsigned kHslReady[2][13] = {
    {0xC8102E, 0xE8541C, 0xF39C12, 0xF7C531, 0xD8E021, 0x7AC943, 0x1E9E4A, 0x17A398, 0x22B8D8, 0x2E6FD8, 0x3D3BB8, 0x6A2BB0,
     0xB0279C},
    {0xF29AB8, 0xE8C9A0, 0xB8B8B8, 0x6E6E6E, 0x202020, 0x6B3A1E, 0x7A1020, 0x1F4D2A, 0x15315E, 0x2A1F5C, 0x4B1D5E, 0x2E5E5E,
     0x8C6A3A},
};
struct HslPanel {
    std::function<void(int)> change; // the colour (0xRRGGBB) or -1: its own
    int start = -1;                  // what it was when it opened
    int now = -1;                    // what it is (-1: its own)
    const char *own = NULL;          // the label of "its own colours" (NULL: none)
    std::string heading;             // what is being coloured ("Hair")
    int col = 0;                     // the ready colour the cursor is on
};
static HslPanel sHsl; // (one at a time)

// title: the screen's ("SHADE COLOUR"); heading: what it colours; current: 0xRRGGBB or -1 (its own: the bars start at
// `shown`); own: "Its own colours" or NULL; change: called at every change and with the start again on Back.
static void open_hsl(const std::string &title, const std::string &heading, int current, unsigned shown, const char *own,
                     std::function<void(int)> change) {
    Overlay o;
    o.kind = OV_HSL;
    o.title = title;
    rgb_hsv(current >= 0 ? (unsigned)current : shown, &o.hue, &o.sat, &o.val);
    o.startRgb = current >= 0 ? (unsigned)current : shown;
    o.cur = HSL_HUE;
    ov_push(o);
    sHsl = HslPanel();
    sHsl.change = change;
    sHsl.start = sHsl.now = current;
    sHsl.own = own;
    sHsl.heading = heading;
    sound(1);
}

static void hsl_set(Overlay &o, int rgb) {
    if (rgb >= 0) { rgb_hsv((unsigned)rgb, &o.hue, &o.sat, &o.val); }
    sHsl.now = rgb;
    if (sHsl.change) { sHsl.change(rgb); }
}

// a bar: its track (drawn by `fill`), the knob at f (0..1); the mouse sets it -> true when it was set
static bool hsl_bar(ImVec2 a, ImVec2 b, float f, bool sel, std::function<ImU32(float)> fill, float *out) {
    const int N = 48;
    float w = b.x - a.x;
    for (int i = 0; i < N; i++) {
        float x0 = a.x + w * i / N, x1 = a.x + w * (i + 1) / N;
        ImU32 c0 = fill((float)i / N), c1 = fill((float)(i + 1) / N);
        sDl->AddRectFilledMultiColor(ImVec2(x0, a.y), ImVec2(x1 + 0.5f, b.y), c0, c1, c1, c0);
    }
    sDl->AddRect(a, b, sel ? IM_COL32(255, 226, 122, 255) : IM_COL32(255, 255, 255, 90), S(3), 0, sel ? S(2.5f) : S(1));
    ImVec2 k(a.x + w * (f < 0 ? 0 : f > 1 ? 1 : f), (a.y + b.y) * 0.5f);
    sDl->AddCircleFilled(k, S(9), fill(f < 0 ? 0 : f > 1 ? 1 : f));
    sDl->AddCircle(k, S(9), IM_COL32(20, 8, 40, 255), 0, S(3));
    sDl->AddCircle(k, S(9), IM_COL32(255, 255, 255, 255), 0, S(1.5f));
    ImVec2 ha(a.x - S(8), a.y - S(8)), hb(b.x + S(8), b.y + S(8));
    const ImVec2 &at = ImGui::GetIO().MouseClickedPos[0]; // (dragged from a press on the bar)
    if (sMouse && ImGui::IsMouseDown(0) && at.x >= ha.x && at.x <= hb.x && at.y >= ha.y && at.y <= hb.y) {
        *out = (ImGui::GetIO().MousePos.x - a.x) / w;
        *out = *out < 0 ? 0 : *out > 1 ? 1 : *out;
        return true;
    }
    return false;
}

static void hsl_frame(Overlay &o, std::vector<Prompt> &ps) {
    ImFont *bold = sBold != NULL ? sBold : sFont;
    float x0 = sBoxA.x + S(24), x1 = sBoxB.x - S(24), y = sBoxA.y + S(56);
    int rows = sHsl.own != NULL ? HSL_ROWS : HSL_OWN;
    // the input: up / down the rows, left / right the bars and the ready colours
    int was = o.cur;
    if (sIn.up) { o.cur = (o.cur + rows - 1) % rows; }
    if (sIn.down) { o.cur = (o.cur + 1) % rows; }
    if (o.cur != was) { sound(0); }
    bool moved = false;
    float k = sIn.fast ? 3.0f : 1.0f;
    if ((sIn.left || sIn.right) && o.cur <= HSL_LIGHT) {
        float d = sIn.right ? 1.0f : -1.0f;
        if (o.cur == HSL_HUE) { o.hue = fmodf(o.hue + d * 3.0f * k + 360.0f, 360.0f); }
        if (o.cur == HSL_SAT) { o.sat += d * 0.025f * k; }
        if (o.cur == HSL_LIGHT) { o.val += d * 0.025f * k; }
        moved = true;
    }
    if ((sIn.left || sIn.right) && (o.cur == HSL_ROW1 || o.cur == HSL_ROW2)) {
        sHsl.col = (sHsl.col + (sIn.right ? 1 : 12)) % 13;
        sound(0);
    }
    // the heading, "Your colour"
    float hw = text_w(bold, S(20), sHsl.heading.c_str());
    sDl->AddText(bold, S(20), ImVec2((x0 + x1 - hw) * 0.5f, y), IM_COL32(255, 210, 63, 255), sHsl.heading.c_str());
    y += S(34);
    unsigned now = hsv_rgb(o.hue, o.sat, o.val);
    ImVec2 sc(x0 + S(24), y + S(22));
    sDl->AddCircleFilled(sc, S(22), sHsl.now >= 0 ? col32(now) : IM_COL32(0, 0, 0, 0));
    if (sHsl.now < 0) { // (its own: a split swatch of what the bars show, dimmed)
        sDl->AddCircleFilled(sc, S(22), col32(now, 110));
    }
    sDl->AddCircle(sc, S(22), IM_COL32(255, 255, 255, 230), 0, S(2.5f));
    sDl->AddText(sFont, S(17), ImVec2(sc.x + S(36), sc.y - S(10)), kInk, sHsl.now >= 0 ? "Your colour" : sHsl.own != NULL ? "Its own colours" : "Your colour");
    if (sHsl.now >= 0) { sDl->AddText(sFont, S(14), ImVec2(x1 - text_w(sFont, S(14), hex_of(now).c_str()), sc.y - S(8)), kDim, hex_of(now).c_str()); }
    y += S(58);
    // the bars
    static const char *const kBar[3] = {"Hue", "Strength", "Light"};
    float lw = S(96);
    for (int i = 0; i < 3; i++) {
        bool sel = o.cur == i;
        ImVec2 ra(x0 - S(8), y - S(6)), rb(x1 + S(8), y + S(30));
        if (sel) { sDl->AddRectFilled(ra, rb, IM_COL32(255, 211, 77, 40), S(10)); }
        sDl->AddText(sFont, S(17), ImVec2(x0, y + S(3)), sel ? IM_COL32(255, 226, 122, 255) : kInk, kBar[i]);
        ImVec2 a(x0 + lw, y + S(4)), b(x1, y + S(22));
        float f = i == 0 ? o.hue / 360.0f : i == 1 ? o.sat : o.val, got = f;
        float h = o.hue, s = o.sat;
        bool set = hsl_bar(a, b, f, sel, [i, h, s](float t) {
            return col32(i == 0 ? hsv_rgb(t * 360.0f, 1, 1) : i == 1 ? hsv_rgb(h, t, 1) : hsv_rgb(h, s, t));
        }, &got);
        if (set && fabsf(got - f) > 0.0005f) {
            if (i == 0) { o.hue = got * 359.9f; } else if (i == 1) { o.sat = got; } else { o.val = got; }
            o.cur = i;
            moved = true;
        }
        y += S(42);
    }
    o.sat = o.sat < 0 ? 0 : o.sat > 1 ? 1 : o.sat;
    o.val = o.val < 0 ? 0 : o.val > 1 ? 1 : o.val;
    // the ready colours
    y += S(6);
    int clickedReady = -1;
    float s0 = std::min(S(30), (x1 - x0 - S(12) * 12) / 13.0f), g = (x1 - x0 - s0 * 13) / 12.0f;
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 13; c++) {
            ImVec2 a(x0 + c * (s0 + g) + s0 * 0.5f, y + s0 * 0.5f);
            bool sel = o.cur == HSL_ROW1 + r && c == sHsl.col;
            sDl->AddCircleFilled(a, s0 * 0.5f, col32(kHslReady[r][c]));
            sDl->AddCircle(a, s0 * 0.5f + (sel ? S(3) : 0), sel ? IM_COL32(255, 226, 122, 255) : IM_COL32(255, 255, 255, 70), 0, sel ? S(3) : S(1));
            if (click_in(ImVec2(a.x - s0 * 0.5f, a.y - s0 * 0.5f), ImVec2(a.x + s0 * 0.5f, a.y + s0 * 0.5f))) {
                o.cur = HSL_ROW1 + r;
                sHsl.col = c;
                clickedReady = r * 13 + c;
            }
        }
        y += s0 + S(12);
    }
    bool clickedOwn = false;
    if (sHsl.own != NULL) {
        y += S(4);
        bool sel = o.cur == HSL_OWN;
        ImVec2 ra(x0 - S(8), y - S(6)), rb(x1 + S(8), y + S(28));
        if (sel) { grad(ra, rb, kGold1, kGold2, (rb.y - ra.y) * 0.5f); }
        sDl->AddText(sFont, S(17), ImVec2(x0, y), sel ? kDark : kInk, sHsl.own);
        clickedOwn = click_in(ra, rb);
    }
    // what the buttons do
    bool onReady = o.cur == HSL_ROW1 || o.cur == HSL_ROW2;
    ps.push_back({B_DPAD, "Choose"});
    ps.push_back({B_CROSS, onReady || o.cur == HSL_OWN ? "Choose" : "OK"});
    if (sHsl.own != NULL) { ps.push_back({B_SQUARE, "Its own"}); }
    ps.push_back({B_CIRCLE, "Back"});
    if (sIn.back) { // as it was when the panel opened
        int start = sHsl.start;
        std::function<void(int)> f = sHsl.change;
        ov_close();
        sound(2);
        if (f && sHsl.now != start) { f(start); }
        return;
    }
    if (moved) { hsl_set(o, (int)hsv_rgb(o.hue, o.sat, o.val)); return; }
    if (clickedReady >= 0) { sound(1); hsl_set(o, (int)kHslReady[clickedReady / 13][clickedReady % 13]); return; }
    if (clickedOwn || (sIn.sq && sHsl.own != NULL) || (sIn.ok && o.cur == HSL_OWN)) {
        sound(1);
        hsl_set(o, -1);
        if (sIn.sq) { ov_close(); } // (square: its own, and done)
        return;
    }
    if (sIn.ok && onReady) { sound(1); hsl_set(o, (int)kHslReady[o.cur - HSL_ROW1][sHsl.col]); return; }
    if (sIn.ok || sIn.start) { // OK: kept as it is
        if (sHsl.now >= 0) { add_recent((unsigned)sHsl.now); }
        ov_close();
        sound(1);
        return;
    }
}

#endif // CREATOR_UI_PARTS

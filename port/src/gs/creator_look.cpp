// The Custom Character screen's look ("Z WARRIORS"): see creator_look.h.
#include <math.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "creator_look.h"

namespace zlook {

const ImU32 kGoldHi = IM_COL32(255, 222, 92, 255), kGoldLo = IM_COL32(240, 150, 16, 255), kGoldInk = IM_COL32(74, 26, 0, 255);
const ImU32 kLavHi = IM_COL32(214, 204, 250, 255), kLavLo = IM_COL32(150, 132, 220, 255), kLavInk = IM_COL32(43, 22, 96, 255);
const ImU32 kNavy = IM_COL32(14, 20, 58, 236), kRim = IM_COL32(205, 190, 255, 200);

static void grad(ImDrawList *dl, ImVec2 a, ImVec2 b, ImU32 top, ImU32 bottom, float round, ImDrawFlags fl = 0) {
    int v0 = dl->VtxBuffer.Size;
    dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, (top >> IM_COL32_A_SHIFT) & 0xFF), round, fl);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, a, ImVec2(a.x, b.y), top, bottom);
}
static void hgrad(ImDrawList *dl, ImVec2 a, ImVec2 b, ImU32 left, ImU32 right) {
    int v0 = dl->VtxBuffer.Size;
    dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 255));
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, a, ImVec2(b.x, a.y), left, right);
}

void meander_strip(ImDrawList *dl, ImVec2 a, ImVec2 b) {
    float w = b.x - a.x;
    if (w < 4.0f) { return; }
    float mid = (a.x + b.x) * 0.5f;
    // the gold, lighter down its middle, dark edges
    hgrad(dl, a, ImVec2(mid, b.y), IM_COL32(196, 120, 14, 255), IM_COL32(255, 214, 96, 255));
    hgrad(dl, ImVec2(mid, a.y), b, IM_COL32(255, 214, 96, 255), IM_COL32(196, 120, 14, 255));
    ImU32 ink = IM_COL32(92, 40, 4, 255);
    float edge = w * 0.11f;
    dl->AddRectFilled(a, ImVec2(a.x + edge * 0.6f, b.y), ink);
    dl->AddRectFilled(ImVec2(b.x - edge * 0.6f, a.y), b, ink);
    // the key: down the strip, a line on one side with a square spiral off it each period
    float m = edge * 1.6f, u = (w - m * 2.0f) / 4.0f, t = u * 0.62f;
    if (u <= 0.5f) { return; }
    float x0 = a.x + m;
    float period = u * 5.0f;
    float start = a.y - fmodf(a.y, period);
    dl->PushClipRect(a, b, true);
    for (float y0 = start; y0 < b.y; y0 += period) {
        ImVec2 p[8] = {ImVec2(x0, y0), ImVec2(x0 + u * 4, y0), ImVec2(x0 + u * 4, y0 + u * 3), ImVec2(x0 + u * 1, y0 + u * 3),
                       ImVec2(x0 + u * 1, y0 + u * 1), ImVec2(x0 + u * 3, y0 + u * 1), ImVec2(x0 + u * 3, y0 + u * 2),
                       ImVec2(x0 + u * 2, y0 + u * 2)};
        dl->AddPolyline(p, 8, ink, 0, t);
        dl->AddLine(ImVec2(x0, y0 - t * 0.5f), ImVec2(x0, y0 + period + t * 0.5f), ink, t);
    }
    dl->PopClipRect();
}

void panel(ImDrawList *dl, ImVec2 a, ImVec2 b, float round, float s) {
    dl->AddRectFilled(ImVec2(a.x + 4 * s, a.y + 6 * s), ImVec2(b.x + 4 * s, b.y + 6 * s), IM_COL32(10, 0, 30, 60), round);
    grad(dl, a, b, IM_COL32(88, 62, 168, 214), IM_COL32(54, 32, 120, 224), round);
    // the faint stripes
    dl->PushClipRect(ImVec2(a.x + 2 * s, a.y + 2 * s), ImVec2(b.x - 2 * s, b.y - 2 * s), true);
    float step = 7.0f * s;
    if (step < 3.0f) { step = 3.0f; }
    for (float y = a.y + step * 0.5f; y < b.y; y += step) {
        dl->AddRectFilled(ImVec2(a.x, y), ImVec2(b.x, y + step * 0.42f), IM_COL32(255, 255, 255, 12));
    }
    dl->PopClipRect();
    // a sheen at its top, the rim
    grad(dl, ImVec2(a.x + 2 * s, a.y + 2 * s), ImVec2(b.x - 2 * s, a.y + 40 * s), IM_COL32(255, 255, 255, 34), IM_COL32(255, 255, 255, 0), round, ImDrawFlags_RoundCornersTop);
    dl->AddRect(a, b, IM_COL32(232, 222, 255, 170), round, 0, 2.0f * s);
}

void pill(ImDrawList *dl, ImVec2 a, ImVec2 b, bool gold, float s) {
    float r = (b.y - a.y) * 0.5f;
    dl->AddRectFilled(ImVec2(a.x, a.y + 2 * s), ImVec2(b.x, b.y + 2 * s), IM_COL32(20, 6, 40, 110), r);
    if (gold) { grad(dl, a, b, kGoldHi, kGoldLo, r); } else { grad(dl, a, b, kLavHi, kLavLo, r); }
    grad(dl, ImVec2(a.x + r * 0.5f, a.y + 1.5f * s), ImVec2(b.x - r * 0.5f, a.y + (b.y - a.y) * 0.45f), IM_COL32(255, 255, 255, 90), IM_COL32(255, 255, 255, 0), r * 0.6f);
    dl->AddRect(a, b, gold ? IM_COL32(255, 244, 190, 255) : IM_COL32(244, 238, 255, 255), r, 0, 1.6f * s);
}

void cursor_bar(ImDrawList *dl, ImVec2 a, ImVec2 b, float s, float pulse) {
    float r = (b.y - a.y) * 0.5f;
    dl->AddRectFilled(ImVec2(a.x - 3 * s, a.y - 3 * s), ImVec2(b.x + 3 * s, b.y + 3 * s), IM_COL32(255, 220, 90, (int)(40 + 50 * pulse)), r + 3 * s);
    grad(dl, a, b, IM_COL32(255, 236, 120, 255), IM_COL32(246, 168, 24, 255), r);
    grad(dl, ImVec2(a.x + r * 0.4f, a.y + 1.5f * s), ImVec2(b.x - r * 0.4f, a.y + (b.y - a.y) * 0.45f), IM_COL32(255, 255, 255, 110), IM_COL32(255, 255, 255, 0), r * 0.6f);
    dl->AddRect(a, b, IM_COL32(255, 250, 214, 255), r, 0, 1.6f * s);
}

float title(ImDrawList *dl, ImFont *f, float size, ImVec2 at, const char *t, ImU32 top, ImU32 bottom, ImU32 edge, float edgeW,
            float slant) {
    int v0 = dl->VtxBuffer.Size;
    dl->AddText(f, size, ImVec2(at.x + edgeW * 0.6f, at.y + edgeW * 1.6f), IM_COL32(20, 4, 0, 150), t); // (its shadow)
    for (int i = 0; i < 16; i++) {
        float k = i * 6.2831853f / 16.0f;
        dl->AddText(f, size, ImVec2(at.x + cosf(k) * edgeW, at.y + sinf(k) * edgeW), edge, t);
    }
    int v1 = dl->VtxBuffer.Size;
    dl->AddText(f, size, at, IM_COL32_WHITE, t);
    int v2 = dl->VtxBuffer.Size;
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v1, v2, ImVec2(at.x, at.y + size * 0.22f), ImVec2(at.x, at.y + size * 0.86f), top, bottom);
    float base = at.y + size;
    for (int i = v0; i < v2; i++) {
        ImDrawVert &v = dl->VtxBuffer[i];
        v.pos.x += (base - v.pos.y) * slant;
    }
    return f->CalcTextSizeA(size, FLT_MAX, 0.0f, t).x;
}

void brackets(ImDrawList *dl, ImVec2 a, ImVec2 b, float len, float thick, ImU32 c) {
    ImVec2 p[3];
    p[0] = ImVec2(a.x, a.y + len); p[1] = a; p[2] = ImVec2(a.x + len, a.y);
    dl->AddPolyline(p, 3, c, 0, thick);
    p[0] = ImVec2(b.x - len, a.y); p[1] = ImVec2(b.x, a.y); p[2] = ImVec2(b.x, a.y + len);
    dl->AddPolyline(p, 3, c, 0, thick);
    p[0] = ImVec2(a.x, b.y - len); p[1] = ImVec2(a.x, b.y); p[2] = ImVec2(a.x + len, b.y);
    dl->AddPolyline(p, 3, c, 0, thick);
    p[0] = ImVec2(b.x - len, b.y); p[1] = b; p[2] = ImVec2(b.x, b.y - len);
    dl->AddPolyline(p, 3, c, 0, thick);
}

void face_ring(ImDrawList *dl, ImTextureID t, ImVec2 c, float r, bool sel, ImVec2 uv0, ImVec2 uv1, float s) {
    float ring = (sel ? 3.6f : 2.2f) * s;
    if (sel) { dl->AddCircleFilled(c, r + ring + 5 * s, IM_COL32(255, 226, 110, 90)); }
    dl->AddCircleFilled(ImVec2(c.x, c.y + 2 * s), r + ring, IM_COL32(20, 6, 40, 140));
    dl->AddCircleFilled(c, r + ring, sel ? IM_COL32(255, 236, 140, 255) : IM_COL32(222, 160, 40, 255));
    dl->AddCircleFilled(c, r, IM_COL32(28, 18, 60, 255));
    if (t) { dl->AddImageRounded(ImTextureRef(t), ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), uv0, uv1, IM_COL32_WHITE, r); }
    dl->AddCircle(c, r + ring * 0.5f, sel ? IM_COL32(255, 255, 230, 255) : IM_COL32(120, 64, 6, 255), 0, 1.0f * s);
}

void fold_sign(ImDrawList *dl, ImVec2 c, float r, bool folded, ImU32 ink) {
    dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), ink, r * 0.45f);
    if (folded) { dl->AddLine(ImVec2(c.x, c.y - r), ImVec2(c.x, c.y + r), ink, r * 0.45f); }
}

} // namespace zlook

// The Custom Character screen's look ("Z WARRIORS"): the pieces it is drawn with (creator_look.cpp), used by the
// screen's draw helpers in creator_ui.cpp - the gold Greek-key strips at the sides, the purple list panel with its
// faint stripes, the gold and lavender pills, the big slanted title letters, the tip box's corner brackets, the round
// faces of the grid. Everything is drawn into the list given, at the sizes given (screen pixels).
#pragma once
#include "imgui.h"

namespace zlook {

// colours of the screen
extern const ImU32 kGoldHi, kGoldLo, kGoldInk, kLavHi, kLavLo, kLavInk, kNavy, kRim;

// a vertical gold strip with the Greek key ("meander") running down it, from a to b
void meander_strip(ImDrawList *dl, ImVec2 a, ImVec2 b);
// the list's panel: translucent purple with faint horizontal stripes, a light rim (round: its corners)
void panel(ImDrawList *dl, ImVec2 a, ImVec2 b, float round, float s);
// a rounded pill: gold (the chosen one) or lavender, with its rim
void pill(ImDrawList *dl, ImVec2 a, ImVec2 b, bool gold, float s);
// the bright gold bar of the row the cursor is on
void cursor_bar(ImDrawList *dl, ImVec2 a, ImVec2 b, float s, float pulse);
// big title letters: a gradient from `top` to `bottom`, a thick `edge` outline and a drop shadow, slanted (italic)
// by `slant` (x moved per pixel up); -> its width
float title(ImDrawList *dl, ImFont *f, float size, ImVec2 at, const char *t, ImU32 top, ImU32 bottom, ImU32 edge,
            float edgeW, float slant);
// white corner brackets around a box
void brackets(ImDrawList *dl, ImVec2 a, ImVec2 b, float len, float thick, ImU32 c);
// a round face in a gold ring: the picture's part uv0..uv1 (none: a dark disc); `sel` a bright thick ring
void face_ring(ImDrawList *dl, ImTextureID t, ImVec2 c, float r, bool sel, ImVec2 uv0, ImVec2 uv1, float s);
// "+" or "-" in a small circle (a section's fold sign)
void fold_sign(ImDrawList *dl, ImVec2 c, float r, bool folded, ImU32 ink);

} // namespace zlook

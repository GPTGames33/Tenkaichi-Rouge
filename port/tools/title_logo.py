"""The title screen's logo from a picture on a white canvas (port/assets/title/logo.png).

The white connected to the picture's borders becomes transparent; the anti-aliased ramp of the logo's outer outline
becomes alpha with the outline's own colour (so no white fringe is left), white enclosed by the outline stays; the
blank canvas around it is cropped. Needs Pillow and numpy.

    python port/tools/title_logo.py custom_logo.webp port/assets/title/logo.png
"""
import sys
from collections import deque

import numpy as np
from PIL import Image

REACH = 48.0  # the flood from the borders goes through anything lighter: the whole ramp, not the outline's core


def cut(rgb):
    h, w, _ = rgb.shape
    lum = rgb @ np.array([0.299, 0.587, 0.114], np.float32)
    white = float(np.median(np.concatenate([lum[0], lum[-1], lum[:, 0], lum[:, -1]])))  # the canvas
    light = lum > REACH
    bg = np.zeros((h, w), bool)
    q = deque()
    for y, x in [(y, x) for x in range(w) for y in (0, h - 1)] + [(y, x) for y in range(h) for x in (0, w - 1)]:
        if light[y, x] and not bg[y, x]:
            bg[y, x] = True
            q.append((y, x))
    while q:
        y, x = q.popleft()
        for ny, nx in ((y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)):
            if 0 <= ny < h and 0 <= nx < w and light[ny, nx] and not bg[ny, nx]:
                bg[ny, nx] = True
                q.append((ny, nx))
    # In the flooded part a pixel is the outline (dark) over the canvas: its coverage from its lightness. The darkest
    # flooded pixels' level is taken as the outline's.
    black = float(np.percentile(lum[~bg], 2)) if (~bg).any() else 0.0
    top = white - 4.0  # (the canvas's own noise counts as clear)
    alpha = np.where(bg, np.clip((top - lum) / (top - black), 0.0, 1.0), 1.0)
    out = rgb.copy()
    ramp = bg & (alpha > 0)
    for c in range(3):
        ch = out[..., c]
        un = (ch - (1.0 - alpha) * white) / np.maximum(alpha, 1e-3)  # the canvas's share taken out
        ch[ramp] = np.clip(un[ramp], 0, 255)
        ch[bg & (alpha == 0)] = 0  # clear pixels black: filtering brings in no white
    return out, alpha, white, bg.mean()


def main():
    src, dst = sys.argv[1], sys.argv[2]
    rgb = np.asarray(Image.open(src).convert("RGB")).astype(np.float32)
    out, alpha, white, flooded = cut(rgb)
    ys, xs = np.nonzero(alpha > 0.02)
    m = 4
    h, w = alpha.shape
    x0, x1 = max(xs.min() - m, 0), min(xs.max() + 1 + m, w)
    y0, y1 = max(ys.min() - m, 0), min(ys.max() + 1 + m, h)
    rgba = np.dstack([out, alpha * 255.0])[y0:y1, x0:x1]
    Image.fromarray(np.round(rgba).astype(np.uint8), "RGBA").save(dst, optimize=True)
    print(f"{dst}: {x1 - x0} x {y1 - y0} (from {x0}, {y0}); canvas {white:.0f}, {flooded * 100:.1f}% cleared")


if __name__ == "__main__":
    main()

/*
 * The title screen's logo ("Dragon Ball Z Budokai Tenkaichi 3", over the characters and "Press START button")
 * replaced by another picture, drawn by the game exactly where and when its own logo is (its zoom as it comes in,
 * the fades), at the resolution the picture is rendered at.
 *
 * The game's logo is one 512 x 256 texture of 256 colours (in the title's pack in the first menu archive, drawn at
 * 1:1 from the screen's top left: the logo itself is the part from (105, 25) to (407, 232)). The attract demo that
 * the title leaves for when no button is pressed shows a smaller copy in the top right corner (256 x 128, the logo
 * from (75, 0) to (255, 124)): replaced too. Each is known by its decoded pixels (a hash, the US disc's; kLogos).
 * The replacement is made like a texture pack's file (gs_texpack.c, and sampled by the same rules): the same layout
 * at SCALE times the size, the new picture fitted inside the old logo's rectangle (where the original's alpha is) at
 * its own proportions, in the middle, transparent around it. Nothing on the disc is changed. (The opening movie's
 * logo is part of its video and stays.)
 *
 * The picture: `title/logo.png` next to the game (any size, RGBA), else the one built into the program
 * (port/assets/title/logo.png, made by port/tools/title_logo.py from the user's logo). Setting `title_logo`
 * (bt3_settings.txt, 1 by default): 0 shows the game's own logo. BT3_TITLE_LOGO_LOG=1 prints the hash of every
 * texture of a logo's size, to find the logo of another disc.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "gs_internal.h"
#include "gs_draw.h"
#include "title_logo.h"
#include "title_logo_png.h" /* kTitleLogoPng: port/assets/title/logo.png, written by port/tools/undefined.py */

extern int Port_Setting(const char *name, int def); /* plat_settings.c */

static const struct {
    uint32_t w, h;
    uint64_t hash; /* hash_px of the decoded texture */
} kLogos[] = {
    {512, 256, UINT64_C(0xE251A3DAA9E1EF2A)}, /* the title screen (SLUS-21678, US) */
    {256, 128, UINT64_C(0x3C293010F1820B2C)}, /* the attract demo's corner (US) */
};
#define LOGOS ((int)(sizeof(kLogos) / sizeof(kLogos[0])))

static int sOn = -1;
static int sLoaded;              /* the picture read (or tried) */
static float *sPic;              /* the picture, premultiplied RGBA, 0..1 */
static int sPicW, sPicH;
static struct {
    uint8_t *px;                 /* the replacement made last for kLogos[n] */
    int scale;                   /* for this SCALE */
    uint32_t alpha;
} sOut[LOGOS];

uint32_t TitleLogo_Key(void) {
    if (sOn < 0) {
        sOn = Port_Setting("title_logo", 1) != 0;
    }
    return sOn ? 0x7171E000u ^ (uint32_t)SCALE * 0x9E3779B1u : 0;
}

static uint64_t hash_px(const uint32_t *px, size_t n) {
    uint64_t h = UINT64_C(14695981039346656037);
    size_t i;
    for (i = 0; i < n; i++) {
        h = (h ^ px[i]) * UINT64_C(1099511628211);
    }
    return h;
}

static void load_picture(void) {
    SDL_Surface *s = SDL_LoadPNG("title/logo.png"), *rgba;
    int i;
    sLoaded = 1;
    if (s != NULL) {
        fprintf(stderr, "bt3: title logo: title/logo.png, %d x %d\n", s->w, s->h);
    } else {
        s = SDL_LoadPNG_IO(SDL_IOFromConstMem(kTitleLogoPng, sizeof(kTitleLogoPng)), true);
    }
    if (s == NULL) {
        fprintf(stderr, "bt3: title logo: no picture (%s): the game's own\n", SDL_GetError());
        return;
    }
    rgba = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(s);
    if (rgba == NULL) {
        return;
    }
    sPicW = rgba->w;
    sPicH = rgba->h;
    sPic = malloc((size_t)sPicW * sPicH * 4 * sizeof(float));
    if (sPic != NULL) {
        for (i = 0; i < sPicW * sPicH; i++) {
            const uint8_t *p = (const uint8_t *)rgba->pixels + (size_t)(i / sPicW) * rgba->pitch + (size_t)(i % sPicW) * 4;
            float a = p[3] / 255.0f;
            sPic[i * 4 + 0] = p[0] / 255.0f * a;
            sPic[i * 4 + 1] = p[1] / 255.0f * a;
            sPic[i * 4 + 2] = p[2] / 255.0f * a;
            sPic[i * 4 + 3] = a;
        }
    }
    SDL_DestroySurface(rgba);
}

/* Catmull-Rom, widened by the reduction when the picture is made smaller (no shimmer, edges kept sharp). */
static float cubic(float x) {
    x = fabsf(x);
    return x < 1.0f ? 1.5f * x * x * x - 2.5f * x * x + 1.0f : x < 2.0f ? -0.5f * x * x * x + 2.5f * x * x - 4.0f * x + 2.0f : 0.0f;
}

/* One axis of the resampling: `n` source samples at `stride` floats (4 channels each) into `m`, written at `ostride`. */
static void resample_line(const float *in, int n, int stride, float *out, int m, int ostride) {
    float r = (float)n / (float)m, s = r > 1.0f ? r : 1.0f;
    int j, c;
    for (j = 0; j < m; j++) {
        float centre = (j + 0.5f) * r - 0.5f, sum[4] = {0, 0, 0, 0}, wsum = 0.0f;
        int i0 = (int)floorf(centre - 2.0f * s), i1 = (int)ceilf(centre + 2.0f * s), i;
        for (i = i0; i <= i1; i++) {
            float w = cubic((i - centre) / s);
            int k = i < 0 ? 0 : i >= n ? n - 1 : i;
            if (w == 0.0f) {
                continue;
            }
            for (c = 0; c < 4; c++) {
                sum[c] += in[(size_t)k * stride + c] * w;
            }
            wsum += w;
        }
        for (c = 0; c < 4; c++) {
            out[(size_t)j * ostride + c] = wsum != 0.0f ? sum[c] / wsum : 0.0f;
        }
    }
}

/* The replacement for kLogos[n], whose logo covers `box` (x0, y0, x1, y1, its texels), at scale k. */
static int build(int n, const int box[4], int k, uint32_t amax) {
    int ow = (int)kLogos[n].w * k, oh = (int)kLogos[n].h * k, bw = (box[2] - box[0] + 1) * k, bh = (box[3] - box[1] + 1) * k;
    uint8_t *out;
    double fit = (double)bw / sPicW < (double)bh / sPicH ? (double)bw / sPicW : (double)bh / sPicH;
    int dw = (int)lround(sPicW * fit), dh = (int)lround(sPicH * fit);
    int dx = box[0] * k + (bw - dw) / 2, dy = box[1] * k + (bh - dh) / 2;
    float *mid, *dst;
    int x, y;

    if (dw < 1 || dh < 1) {
        return 0;
    }
    free(sOut[n].px);
    out = sOut[n].px = calloc((size_t)ow * oh, 4); /* transparent black around the picture: filtering brings in no colour */
    mid = malloc((size_t)dw * sPicH * 4 * sizeof(float));
    dst = malloc((size_t)dw * dh * 4 * sizeof(float));
    if (out == NULL || mid == NULL || dst == NULL) {
        free(mid);
        free(dst);
        free(out);
        sOut[n].px = NULL;
        return 0;
    }
    for (y = 0; y < sPicH; y++) {
        resample_line(sPic + (size_t)y * sPicW * 4, sPicW, 4, mid + (size_t)y * dw * 4, dw, 4);
    }
    for (x = 0; x < dw; x++) {
        resample_line(mid + (size_t)x * 4, sPicH, dw * 4, dst + (size_t)x * 4, dh, dw * 4);
    }
    for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
            const float *p = dst + ((size_t)y * dw + x) * 4;
            uint8_t *q = out + ((size_t)(dy + y) * ow + dx + x) * 4;
            float a = p[3] < 0.0f ? 0.0f : p[3] > 1.0f ? 1.0f : p[3];
            int c;
            if (a <= 0.0f) {
                continue;
            }
            for (c = 0; c < 3; c++) {
                float v = p[c] / a;
                q[c] = (uint8_t)lroundf((v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v) * 255.0f);
            }
            q[3] = (uint8_t)lroundf(a * (float)amax); /* the GS's alpha: the original's opaque value */
        }
    }
    free(mid);
    free(dst);
    sOut[n].scale = k;
    sOut[n].alpha = amax;
    return 1;
}

int TitleLogo_Swap(const uint32_t *px, uint32_t tw, uint32_t th, TexPackImage *img, uint32_t *maxAlpha) {
    uint64_t h = 0;
    size_t i, n = (size_t)tw * th;
    int logo, sized = 0, box[4] = {(int)tw, (int)th, -1, -1}, k = SCALE < 1 ? 1 : SCALE > 8 ? 8 : SCALE;
    uint32_t amax = 0;

    if (!TitleLogo_Key() || px == NULL) {
        return 0;
    }
    for (logo = 0; logo < LOGOS; logo++) { /* (most textures have neither size: no hash) */
        if (tw == kLogos[logo].w && th == kLogos[logo].h) {
            if (!sized) {
                sized = 1;
                h = hash_px(px, n);
                if (getenv("BT3_TITLE_LOGO_LOG") != NULL) {
                    fprintf(stderr, "bt3: title logo: a %u x %u texture, hash %016llx\n", (unsigned)tw, (unsigned)th, (unsigned long long)h);
                }
            }
            if (h == kLogos[logo].hash) {
                break;
            }
        }
    }
    if (logo == LOGOS) {
        return 0;
    }
    for (i = 0; i < n; i++) { /* where the logo is in it, and its opaque alpha */
        uint32_t a = px[i] >> 24;
        int x = (int)(i % tw), y = (int)(i / tw);
        amax = a > amax ? a : amax;
        if (a >= 8) {
            box[0] = x < box[0] ? x : box[0];
            box[1] = y < box[1] ? y : box[1];
            box[2] = x > box[2] ? x : box[2];
            box[3] = y > box[3] ? y : box[3];
        }
    }
    if (box[2] < box[0] || amax == 0) {
        return 0;
    }
    if (!sLoaded) {
        load_picture();
    }
    if (sPic == NULL) {
        return 0;
    }
    if ((sOut[logo].px == NULL || sOut[logo].scale != k || sOut[logo].alpha != amax) && !build(logo, box, k, amax)) {
        return 0;
    }
    memset(img, 0, sizeof(*img));
    img->w = tw * (uint32_t)k;
    img->h = th * (uint32_t)k;
    img->format = 0;
    img->levels = 1;
    img->data[0] = sOut[logo].px;
    img->bytes[0] = img->w * img->h * 4;
    *maxAlpha = amax;
    return 1;
}

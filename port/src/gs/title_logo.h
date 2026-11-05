/* The title screen's logo replaced by another picture (title_logo.c). */
#ifndef PORT_GS_TITLE_LOGO_H
#define PORT_GS_TITLE_LOGO_H
#include <stdint.h>
#include "gs_texpack.h"

/* Part of every texture's identity (texture_get's `gen`): 0 with the setting off, else a value that changes with what
   the replacement is made for (the render scale), so that a change takes effect at once. */
uint32_t TitleLogo_Key(void);
/* `px` (tw x th, RGBA with the GS's alpha, as just decoded from a paletted texture): when it is the title's logo, *img
   is its replacement (kept here; valid until the next call) and *maxAlpha the original's largest alpha; returns 1. */
int TitleLogo_Swap(const uint32_t *px, uint32_t tw, uint32_t th, TexPackImage *img, uint32_t *maxAlpha);

#endif

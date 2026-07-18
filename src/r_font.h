#ifndef SG_R_FONT_H
#define SG_R_FONT_H

#include "r_main.h"

r_font_t *r_fontCreate(r_renderer_t *ren, const char *fontName);
void r_fontDestroy(r_font_t *font);
int r_fontStringWidth(r_font_t *font, int height, const char *str);
void r_fontDrawString(r_font_t *font, float x, float y, int align, int height, const col4_t col, const char *str);
int r_fontCursorIndex(r_font_t *font, int height, const char *str, int curX, int curY);
int r_fontFindHeight(r_font_t *font, int height);

#endif

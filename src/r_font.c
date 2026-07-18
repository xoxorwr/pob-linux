#include "r_font.h"
#include "r_texture.h"
#include "console.h"
#include "sys_main.h"

#include <stdio.h>
#include <unistd.h>
#include <math.h>
#include <string.h>

/* ======== Glyph / Font Height ======== */

static const f_glyph_s g_defGlyph = { 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0 };

static const f_glyph_s *FontHeightGlyph(const f_fontHeight_s *fh, int ch)
{
	if (ch < 0 || ch >= fh->numGlyph) return &fh->defGlyph;
	return &fh->glyphs[ch];
}

/* ======== Font Creation ======== */

r_font_t *r_fontCreate(r_renderer_t *ren, const char *fontName)
{
	r_font_t *font = (r_font_t *)calloc(1, sizeof(r_font_t));
	if (!font) return NULL;
	font->renderer = ren;

	char pathBase[512];
	snprintf(pathBase, sizeof(pathBase), "%s/%sFonts/%s", ren->sys->basePath, CFG_DATAPATH, fontName);

	char tgfPath[612];
	snprintf(tgfPath, sizeof(tgfPath), "%s.tgf", pathBase);

	{
		char cwd[512];
		if (getcwd(cwd, sizeof(cwd)))
			fprintf(stderr, "Font CWD: '%s'\n", cwd);
		else
			fprintf(stderr, "Font CWD: getcwd failed\n");
	}
	FILE *f = fopen(tgfPath, "r");
	if (!f) {
		conWarning("font \"%s\" not found", fontName);
		return font;
	}

	font->maxHeight = 0;
	f_fontHeight_s *curFH = NULL;
	char line[512];

	while (fgets(line, sizeof(line), f)) {
		int h, x, y, w, sl, sr;
		if (sscanf(line, "HEIGHT %d;", &h) == 1) {
			if (font->numFontHeight >= 16) continue;
			curFH = (f_fontHeight_s *)calloc(1, sizeof(f_fontHeight_s));
			font->fontHeights[font->numFontHeight++] = curFH;
			curFH->height = h;

			char tgaPath[612];
			snprintf(tgaPath, sizeof(tgaPath), "%s.%d.tga", pathBase, h);
			curFH->tex = r_texCreate(ren, tgaPath, TF_NOMIPMAP);
			curFH->numGlyph = 0;

			if (h > font->maxHeight) font->maxHeight = h;
		}
		else if (curFH && sscanf(line, "GLYPH %d %d %d %d %d;", &x, &y, &w, &sl, &sr) == 5) {
			if (curFH->numGlyph >= 128) continue;
			f_glyph_s *glyph = &curFH->glyphs[curFH->numGlyph++];
			if (curFH->tex->fileWidth > 0) {
				glyph->tcLeft = (float)x / (float)curFH->tex->fileWidth;
				glyph->tcRight = (float)(x + w) / (float)curFH->tex->fileWidth;
				glyph->tcTop = (float)y / (float)curFH->tex->fileHeight;
				glyph->tcBottom = (float)(y + curFH->height) / (float)curFH->tex->fileHeight;
			}
			glyph->width = w;
			glyph->spLeft = sl;
			glyph->spRight = sr;
		}
	}
	fclose(f);

	/* Build height map */
	if (font->maxHeight > 0) {
		font->fontHeightMap = (int *)calloc(font->maxHeight + 1, sizeof(int));
		for (int i = 0; i < font->numFontHeight; i++) {
			int gh = font->fontHeights[i]->height;
			for (int h = gh; h <= font->maxHeight; h++) {
				font->fontHeightMap[h] = i;
			}
			if (i > 0) {
				int belowH = font->fontHeights[i - 1]->height;
				int lim = (gh - belowH - 1) / 2;
				for (int b = 0; b < lim; b++) {
					if (gh - b - 1 >= 0 && gh - b - 1 <= font->maxHeight)
						font->fontHeightMap[gh - b - 1] = i;
				}
			}
		}
	}

	return font;
}

void r_fontDestroy(r_font_t *font)
{
	if (!font) return;
	for (int i = 0; i < font->numFontHeight; i++) {
		if (font->fontHeights[i]) {
			r_texDestroy(font->fontHeights[i]->tex);
			free(font->fontHeights[i]);
		}
	}
	free(font->fontHeightMap);
	free(font);
}

/* ======== Font Height Lookup ======== */

static int FindFontHeightIndex(r_font_t *font, int height)
{
	if (!font->fontHeightMap || font->numFontHeight == 0) return 0;
	if (height >= font->maxHeight) return font->numFontHeight - 1;
	if (height <= 0) return 0;
	return font->fontHeightMap[height];
}

int r_fontFindHeight(r_font_t *font, int height)
{
	int idx = FindFontHeightIndex(font, height);
	return font->fontHeights[idx]->height;
}

/* ======== String Width ======== */

static int GlyphPixelWidth(const f_fontHeight_s *fh, int ch)
{
	const f_glyph_s *g = FontHeightGlyph(fh, ch);
	return g->width + g->spLeft + g->spRight;
}

int r_fontStringWidth(r_font_t *font, int height, const char *str)
{
	if (!str || !*str || !font->fontHeightMap) return 0;

	int fhIdx = FindFontHeightIndex(font, height);
	f_fontHeight_s *fh = font->fontHeights[fhIdx];
	float scale = (float)height / (float)fh->height;
	int maxW = 0;
	int curW = 0;
	const char *p = str;

	while (*p) {
		if (*p == '\n') {
			if (curW > maxW) maxW = curW;
			curW = 0;
			p++;
			continue;
		}
		if (*p == '\t') {
			int spW = GlyphPixelWidth(fh, ' ');
			curW += (int)((float)(spW * 4) * scale);
			p++;
			continue;
		}
		int escLen = IsColorEscape(p);
		if (escLen) {
			p += escLen;
			continue;
		}
		int ch = (unsigned char)*p;
		curW += (int)((float)GlyphPixelWidth(fh, ch) * scale);
		p++;
	}
	if (curW > maxW) maxW = curW;
	return maxW;
}

/* ======== Cursor Index ======== */

int r_fontCursorIndex(r_font_t *font, int height, const char *str, int curX, int curY)
{
	if (!str || !*str || !font->fontHeightMap) return (int)strlen(str);

	int fhIdx = FindFontHeightIndex(font, height);
	f_fontHeight_s *fh = font->fontHeights[fhIdx];
	float scale = (float)height / (float)fh->height;

	int lineY = height;
	int lastIdx = 0;
	const char *lineStart = str;
	const char *p = str;

	while (*p) {
		const char *lineEnd = p;
		while (*lineEnd && *lineEnd != '\n') lineEnd++;

		/* Find cursor position on this line */
		float x = 0.0f;
		const char *lp = lineStart;
		int idx = (int)(lineStart - str);
		while (lp < lineEnd) {
			int escLen = IsColorEscape(lp);
			if (escLen) {
				lp += escLen;
				idx += escLen;
				continue;
			}
			if (*lp == '\t') {
				int spW = GlyphPixelWidth(fh, ' ');
				x += (float)(spW * 4) * scale;
				x = ceilf(x);
				lp++;
				idx++;
				if (curX <= (int)x) {
					lastIdx = idx;
					break;
				}
				continue;
			}
			int ch = (unsigned char)*lp;
			x += (float)GlyphPixelWidth(fh, ch) * scale;
			x = ceilf(x);
			lp++;
			idx++;
			if (curX <= (int)x) {
				lastIdx = idx;
				break;
			}
		}
		if (lp >= lineEnd) lastIdx = (int)(lineEnd - str);

		if (curY <= lineY) break;

		if (*lineEnd == '\n') {
			lineStart = lineEnd + 1;
			p = lineStart;
			lineY += height;
		} else {
			break;
		}
	}
	return lastIdx;
}

/* ======== Drawing ======== */

static void DrawTextLine(r_font_t *font, float x, float y, int align, int height, const col4_t col, const char *str, int len)
{
	r_renderer_t *ren = font->renderer;
	int fhIdx = FindFontHeightIndex(font, height);
	f_fontHeight_s *fh = font->fontHeights[fhIdx];
	float scale = (float)height / (float)fh->height;

	/* Calculate alignment */
	if (align != F_LEFT) {
		int lineWidth = 0;
		const char *p = str;
		int remaining = len;
		while (remaining > 0 && *p && *p != '\n') {
			int escLen = IsColorEscape(p);
			if (escLen) { p += escLen; remaining -= escLen; continue; }
			if (*p == '\t') {
				int spW = GlyphPixelWidth(fh, ' ');
				lineWidth += (int)((float)(spW * 4) * scale);
				p++; remaining--;
				continue;
			}
			lineWidth += (int)((float)GlyphPixelWidth(fh, (unsigned char)*p) * scale);
			p++; remaining--;
		}
		switch (align) {
		case F_CENTRE:
			x = floorf(((float)rVirtualScreenWidth(ren) - (float)lineWidth) / 2.0f + x);
			break;
		case F_RIGHT:
			x = floorf((float)rVirtualScreenWidth(ren) - (float)lineWidth - x);
			break;
		case F_CENTRE_X:
			x = floorf(x - (float)lineWidth / 2.0f);
			break;
		case F_RIGHT_X:
			x = floorf(x - (float)lineWidth);
			break;
		}
	}

	x = roundf(x);
	float drawY = y;
	col4_t curCol;
	memcpy(curCol, col, sizeof(col4_t));

	r_tex_t *curTex = NULL;
	const char *p = str;
	int remaining = len;

	while (remaining > 0 && *p && *p != '\n') {
		int escLen = IsColorEscape(p);
		if (escLen) {
			ReadColorEscape(p, escLen, curCol);
			curCol[3] = 1.0f;
			r_layerCmdColor_t colorCmd;
			colorCmd.cmd = CMD_COLOR;
			memcpy(colorCmd.col, curCol, sizeof(col4_t));
			memcpy(ren->curLayer->cmdBuf + ren->curLayer->cmdCursor, &colorCmd, sizeof(colorCmd));
			ren->curLayer->cmdCursor += sizeof(colorCmd);
			ren->curLayer->numCmd++;
			p += escLen;
			remaining -= escLen;
			continue;
		}

		if (*p == '\t') {
			const f_glyph_s *spGlyph = FontHeightGlyph(fh, ' ');
			int spW = spGlyph->width + spGlyph->spLeft + spGlyph->spRight;
			x += (float)(spW << 2) * scale;
			p++;
			remaining--;
			continue;
		}

		int ch = (unsigned char)*p;
		const f_glyph_s *glyph = FontHeightGlyph(fh, ch);

		if (glyph != &fh->defGlyph && curTex != fh->tex) {
			curTex = fh->tex;
			r_layerCmdBind_t cmd;
			cmd.cmd = CMD_BIND;
			cmd.tex = fh->tex;
			memcpy(ren->curLayer->cmdBuf + ren->curLayer->cmdCursor, &cmd, sizeof(cmd));
			ren->curLayer->cmdCursor += sizeof(cmd);
			ren->curLayer->numCmd++;
		}

		x += (float)glyph->spLeft * scale;
		float gw = (float)glyph->width * scale;

		if (glyph->width > 0) {
			/* Emit quad */
			r_layerCmdQuad_t quad;
			quad.cmd = CMD_QUAD;
			quad.s[0] = glyph->tcLeft;  quad.t[0] = glyph->tcTop;
			quad.x[0] = x;              quad.y[0] = drawY;
			quad.s[1] = glyph->tcRight; quad.t[1] = glyph->tcTop;
			quad.x[1] = x + gw;         quad.y[1] = drawY;
			quad.s[2] = glyph->tcRight; quad.t[2] = glyph->tcBottom;
			quad.x[2] = x + gw;         quad.y[2] = drawY + (float)height;
			quad.s[3] = glyph->tcLeft;  quad.t[3] = glyph->tcBottom;
			quad.x[3] = x;              quad.y[3] = drawY + (float)height;
			quad.stackLayer = 0;
			quad.maskLayer = -1;
			memcpy(ren->curLayer->cmdBuf + ren->curLayer->cmdCursor, &quad, sizeof(quad));
			ren->curLayer->cmdCursor += sizeof(quad);
			ren->curLayer->numCmd++;

			x += gw;
		}

		x += (float)glyph->spRight * scale;
		x = ceilf(x);

		p++;
		remaining--;
	}
}

void r_fontDrawString(r_font_t *font, float x, float y, int align, int height, const col4_t col, const char *str)
{
	if (!str || !*str || !font->fontHeightMap) return;

	r_renderer_t *ren = font->renderer;
	r_layerCmdColor_t colorCmd;
	colorCmd.cmd = CMD_COLOR;
	memcpy(colorCmd.col, col, sizeof(col4_t));
	memcpy(ren->curLayer->cmdBuf + ren->curLayer->cmdCursor, &colorCmd, sizeof(colorCmd));
	ren->curLayer->cmdCursor += sizeof(colorCmd);
	ren->curLayer->numCmd++;

	const char *lineStart = str;
	while (*str) {
		const char *lineEnd = str;
		while (*lineEnd && *lineEnd != '\n') lineEnd++;
		int lineLen = (int)(lineEnd - lineStart);
		DrawTextLine(font, x, y, align, height, col, lineStart, lineLen);
		y += (float)height;
		if (*lineEnd == '\n') {
			str = lineEnd + 1;
			lineStart = str;
		} else {
			break;
		}
	}
}

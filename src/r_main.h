#ifndef SG_R_MAIN_H
#define SG_R_MAIN_H

#include "common.h"
#include "config.h"

#include <glad/gles2.h>

/* ======== Forward Declarations ======== */

typedef struct sys_main_s sys_main_t;
typedef struct r_tex_s r_tex_t;
typedef struct r_shader_s r_shader_t;
typedef struct r_shaderHnd_s r_shaderHnd_t;
typedef struct r_layer_s r_layer_t;
typedef struct r_renderer_s r_renderer_t;
typedef struct r_font_s r_font_t;

/* ======== Enums ======== */

typedef enum {
	F_DPI_AWARE = 0x1
} r_featureFlag_e;

typedef enum {
	F_LEFT = 0,
	F_CENTRE,
	F_RIGHT,
	F_CENTRE_X,
	F_RIGHT_X
} r_fontAlign_e;

typedef enum {
	F_FIXED = 0,
	F_VAR,
	F_VAR_BOLD,
	F_FONTIN_SC,
	F_FONTIN_SC_ITALIC,
	F_FONTIN,
	F_FONTIN_ITALIC,
	F_NUMFONTS
} r_fonts_e;

typedef enum {
	TF_CLAMP    = 0x01,
	TF_NOMIPMAP = 0x02,
	TF_NEAREST  = 0x04,
	TF_ASYNC    = 0x08
} r_texFlag_e;

typedef enum {
	RB_ALPHA = 0,
	RB_PRE_ALPHA,
	RB_ADDITIVE
} r_blendMode_e;

/* ======== Vertex Format ======== */

typedef struct {
	float x, y;
	float u, v;
	float r, g, b, a;
	float viewX, viewY, viewW, viewH;
	float texId, stackIdx, maskIdx;
} Vertex;

/* ======== Viewport ======== */

typedef struct {
	int x;
	int y;
	int width;
	int height;
} r_viewport_s;

/* ======== Command Buffer Types ======== */

typedef enum {
	CMD_VIEWPORT,
	CMD_BLEND,
	CMD_BIND,
	CMD_COLOR,
	CMD_QUAD
} r_layerCmd_e;

typedef struct {
	r_layerCmd_e cmd;
	r_viewport_s viewport;
} r_layerCmdViewport_t;

typedef struct {
	r_layerCmd_e cmd;
	int blendMode;
} r_layerCmdBlend_t;

typedef struct {
	r_layerCmd_e cmd;
	r_tex_t *tex;
} r_layerCmdBind_t;

typedef struct {
	r_layerCmd_e cmd;
	col4_t col;
} r_layerCmdColor_t;

typedef struct {
	r_layerCmd_e cmd;
	float s[4];
	float t[4];
	float x[4];
	float y[4];
	int stackLayer;
	int maskLayer;
} r_layerCmdQuad_t;

/* ======== Shader ======== */

struct r_shader_s {
	r_renderer_t *renderer;
	char name[256];
	dword nameHash;
	int refCount;
	r_tex_t *tex;
};

/* ======== Shader Handle (reference counted) ======== */

struct r_shaderHnd_s {
	r_shader_t *sh;
};

/* ======== Texture ======== */

struct r_tex_s {
	GLuint texId;
	int width;
	int height;
	int fileWidth;
	int fileHeight;
	int format;
	int flags;
	int error;
	char fileName[256];

	/* Async loading state */
	GLenum target;
	size_t stackLayers;
};

/* ======== Layer ======== */

#define LAYER_CMD_BUF_SIZE (1 << 23)

struct r_layer_s {
	r_renderer_t *renderer;
	int layer;
	int subLayer;

	byte cmdBuf[LAYER_CMD_BUF_SIZE];
	size_t cmdCursor;
	size_t numCmd;
};

/* ======== Font Structures ======== */

typedef struct {
	float tcLeft, tcRight, tcTop, tcBottom;
	int width;
	int spLeft, spRight;
} f_glyph_s;

typedef struct {
	r_tex_t *tex;
	int height;
	int numGlyph;
	f_glyph_s glyphs[128];
	f_glyph_s defGlyph;
} f_fontHeight_s;

struct r_font_s {
	r_renderer_t *renderer;
	int numFontHeight;
	f_fontHeight_s *fontHeights[16];
	int *fontHeightMap;
	int maxHeight;
};

/* ======== Renderer ======== */

#define R_MAXSHADERS 65536

struct r_renderer_s {
	sys_main_t *sys;

	/* OpenGL state */
	const char *st_vendor;
	const char *st_renderer;
	const char *st_ver;
	const char *st_ext;

	int texMaxDim;
	int apiDpiAware;
	int dpiScaleOverridePercent;

	/* Shader program */
	GLuint tintedTextureProgram;
	GLint mvpMatrixLoc;
	GLint texLocs[8];
	GLint xyAttr;
	GLint uvAttr;
	GLint tintAttr;
	GLint viewportAttr;
	GLint texIdAttr;

	/* Clear color */
	float clearColor[4];

	/* Layer system */
	int numLayer;
	int layerListSize;
	r_layer_t **layerList;
	r_layer_t *curLayer;

	/* Draw state */
	r_viewport_s curViewport;
	int curBlendMode;
	col4_t drawColor;

	/* Shader list */
	int numShader;
	r_shader_t *shaderList[R_MAXSHADERS];

	/* White image for solid color draws */
	r_shaderHnd_t *whiteImage;

	/* Fonts */
	r_font_t *fonts[F_NUMFONTS];

	/* Virtual screen */
	float dpiScale;
	int virtualWidth;
	int virtualHeight;
};

/* ======== Function Declarations ======== */

/* r_main.c */
void rInit(r_renderer_t *ren, sys_main_t *sys, r_featureFlag_e features);
void rShutdown(r_renderer_t *ren);
void rBeginFrame(r_renderer_t *ren);
void rEndFrame(r_renderer_t *ren);

r_shaderHnd_t *rRegisterShader(r_renderer_t *ren, const char *name, int flags);
r_shaderHnd_t *rRegisterShaderFromImage(r_renderer_t *ren, GLuint texId, int width, int height, int flags);
void rGetShaderImageSize(r_shaderHnd_t *hnd, int *width, int *height);
void rSetShaderLoadingPriority(r_shaderHnd_t *hnd, int pri);
void rPurgeShaders(r_renderer_t *ren);
int rGetAsyncCount(r_renderer_t *ren);

void rSetClearColor(r_renderer_t *ren, const col4_t col);
void rSetDrawLayer(r_renderer_t *ren, int layer, int subLayer);
void rSetDrawSubLayer(r_renderer_t *ren, int subLayer);
int rGetDrawLayer(r_renderer_t *ren);
void rSetViewport(r_renderer_t *ren, int x, int y, int width, int height);
void rSetBlendMode(r_renderer_t *ren, int mode);
void rDrawColor(r_renderer_t *ren, const col4_t col);
void rDrawColorDword(r_renderer_t *ren, dword col);
void rGetDrawColor(r_renderer_t *ren, col4_t color);

void rDrawImage(r_renderer_t *ren, r_shaderHnd_t *hnd, float px, float py, float ex, float ey,
                 float uv0x, float uv0y, float uv2x, float uv2y,
                 int stackLayer, int maskLayer);
void rDrawImageQuad(r_renderer_t *ren, r_shaderHnd_t *hnd,
                     float p0x, float p0y, float p1x, float p1y,
                     float p2x, float p2y, float p3x, float p3y,
                     float uv0x, float uv0y, float uv1x, float uv1y,
                     float uv2x, float uv2y, float uv3x, float uv3y,
                     int stackLayer, int maskLayer);

void rDrawString(r_renderer_t *ren, float x, float y, int align, int height,
                  const col4_t col, int font, const char *str);
int rDrawStringWidth(r_renderer_t *ren, int height, int font, const char *str);
int rDrawStringCursorIndex(r_renderer_t *ren, int height, int font, const char *str, int curX, int curY);

int rVirtualScreenWidth(r_renderer_t *ren);
int rVirtualScreenHeight(r_renderer_t *ren);
float rVirtualScreenScaleFactor(r_renderer_t *ren);

/* r_texture.c */
void r_texInit(r_renderer_t *ren);
r_tex_t *r_texCreate(r_renderer_t *ren, const char *fileName, int flags);
r_tex_t *r_texCreateFromData(r_renderer_t *ren, int width, int height, const byte *pixels, int flags);
void r_texDestroy(r_tex_t *tex);
void r_texBind(r_tex_t *tex);
void r_texGetSize(r_tex_t *tex, int *width, int *height);

/* r_font.c */
r_font_t *r_fontCreate(r_renderer_t *ren, const char *fontName);
void r_fontDestroy(r_font_t *font);
int r_fontStringWidth(r_font_t *font, int height, const char *str);
void r_fontDrawString(r_font_t *font, float x, float y, int align, int height, const col4_t col, const char *str);
int r_fontCursorIndex(r_font_t *font, int height, const char *str, int curX, int curY);
int r_fontFindHeight(r_font_t *font, int height);

#endif

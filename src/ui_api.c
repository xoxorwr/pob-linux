#include "ui_main.h"
#include "ui_subscript.h"
#include "lua_compat.h"
#include "sys_video.h"

#include <math.h>
#include <zlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>

/* ======== UI Pointer from Lua State ======== */

static ui_main_t *GetUIPtr(lua_State *L)
{
	lua_geti(L, LUA_REGISTRYINDEX, UI_REGISTRY_KEY);
	ui_main_t *ui = (ui_main_t *)lua_touserdata(L, -1);
	lua_pop(L, 1);
	return ui;
}

/* ======== Assertion Helpers ======== */

static void uiLAssert(lua_State *L, int cond, const char *fmt, ...)
{
	if (!cond) {
		va_list va;
		va_start(va, fmt);
		lua_pushvfstring(L, fmt, va);
		va_end(va);
		lua_error(L);
	}
}

static int uiIsUserData(lua_State *L, int index, const char *metaName)
{
	if (lua_type(L, index) != LUA_TUSERDATA || lua_getmetatable(L, index) == 0)
		return 0;
	lua_getfield(L, LUA_REGISTRYINDEX, metaName);
	int ret = lua_rawequal(L, -2, -1);
	lua_pop(L, 2);
	return ret;
}

/* ======== Helper: Parse Font Enum ======== */

static int parseFont(lua_State *L, int idx)
{
	static const char *fontMap[] = {
		"FIXED", "VAR", "VAR BOLD", "FONTIN SC",
		"FONTIN SC ITALIC", "FONTIN", "FONTIN ITALIC", NULL
	};
	return luaL_checkoption(L, idx, "FIXED", fontMap);
}

/* ======== Helper: Parse Align Enum ======== */

static int parseAlign(lua_State *L, int idx)
{
	static const char *alignMap[] = {
		"LEFT", "CENTER", "RIGHT", "CENTER_X", "RIGHT_X", NULL
	};
	return luaL_checkoption(L, idx, "LEFT", alignMap);
}

/* ======== Helper: Scaled Height ======== */

static int scaledHeight(lua_State *L, ui_main_t *ui, int idx)
{
	const float dpiScale = rVirtualScreenScaleFactor(ui->renderer);
	const lua_Number logicalHeight = lua_tonumber(L, idx);
	int h = (int)lround(logicalHeight * dpiScale);
	if (h <= 1)
		h = 1;
	else
		h = (h + 1) & ~1;
	return h;
}

/* ======== Callbacks ======== */

static int l_SetCallback(lua_State *L)
{
	int n = lua_gettop(L);
	uiLAssert(L, n >= 1, "Usage: SetCallback(name[, func])");
	uiLAssert(L, lua_isstring(L, 1),
		"SetCallback() argument 1: expected string, got %s", luaL_typename(L, 1));
	lua_pushvalue(L, 1);
	if (n >= 2) {
		uiLAssert(L, lua_isfunction(L, 2) || lua_isnil(L, 2),
			"SetCallback() argument 2: expected function or nil, got %s", luaL_typename(L, 2));
		lua_pushvalue(L, 2);
	} else {
		lua_pushnil(L);
	}
	lua_settable(L, lua_upvalueindex(1));
	return 0;
}

static int l_GetCallback(lua_State *L)
{
	int n = lua_gettop(L);
	uiLAssert(L, n >= 1, "Usage: GetCallback(name)");
	uiLAssert(L, lua_isstring(L, 1),
		"GetCallback() argument 1: expected string, got %s", luaL_typename(L, 1));
	lua_pushvalue(L, 1);
	lua_gettable(L, lua_upvalueindex(1));
	return 1;
}

static int l_SetMainObject(lua_State *L)
{
	int n = lua_gettop(L);
	lua_pushstring(L, "MainObject");
	if (n >= 1) {
		uiLAssert(L, lua_istable(L, 1) || lua_isnil(L, 1),
			"SetMainObject() argument 1: expected table or nil, got %s", luaL_typename(L, 1));
		lua_pushvalue(L, 1);
	} else {
		lua_pushnil(L);
	}
	lua_settable(L, lua_upvalueindex(1));
	return 0;
}

/* ======== Art Handles ======== */

typedef struct {
	int width;
	int height;
	int components;
	byte *data;
} artImage_t;

typedef struct {
	artImage_t img;
} artHandle_t;

static void artImageFree(artImage_t *img)
{
	if (img->data) {
		free(img->data);
		img->data = NULL;
	}
	img->width = 0;
	img->height = 0;
	img->components = 0;
}

static artHandle_t *GetArtHandle(lua_State *L, ui_main_t *ui, const char *method)
{
	uiLAssert(L, uiIsUserData(L, 1, "uiarthandlemeta"),
		"artHandle:%s() must be used on an art handle", method);
	artHandle_t *ah = (artHandle_t *)lua_touserdata(L, 1);
	lua_remove(L, 1);
	return ah;
}

static int l_artHandleGC(lua_State *L)
{
	artHandle_t *ah = GetArtHandle(L, GetUIPtr(L), "__gc");
	artImageFree(&ah->img);
	return 0;
}

static int l_artHandleSize(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	artHandle_t *ah = GetArtHandle(L, ui, "Size");
	lua_pushinteger(L, ah->img.width);
	lua_pushinteger(L, ah->img.height);
	return 2;
}

static int l_NewArtHandle(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	int n = lua_gettop(L);
	uiLAssert(L, n >= 1, "Usage: NewArtHandle(fileName)");
	uiLAssert(L, lua_isstring(L, 1),
		"NewArtHandle() argument 1: expected string, got %s", luaL_typename(L, 1));

	const char *given = lua_tostring(L, 1);
	char filePath[UI_PATH_MAX];
	if (given[0] != '/') {
		snprintf(filePath, sizeof(filePath), "%s/%s", ui->scriptWorkDir, given);
	} else {
		strncpy(filePath, given, sizeof(filePath) - 1);
		filePath[sizeof(filePath) - 1] = '\0';
	}

	/* Try loading as a TGA-like raw image: read header to get dimensions */
	FILE *fp = fopen(filePath, "rb");
	if (!fp)
		return 0;

	/* Read a simple TGA header (18 bytes) to get width/height/bpp */
	byte header[18];
	if (fread(header, 1, 18, fp) != 18) {
		fclose(fp);
		return 0;
	}

	int idLen = header[0];
	int imgType = header[2];
	int w = header[12] | (header[13] << 8);
	int h = header[14] | (header[15] << 8);
	int bpp = header[16];
	int comp;

	/* Only support uncompressed RGB/RGBA */
	if (imgType != 2 && imgType != 3) {
		fclose(fp);
		return 0;
	}

	if (bpp == 8)      comp = 1;
	else if (bpp == 24) comp = 3;
	else if (bpp == 32) comp = 4;
	else {
		fclose(fp);
		return 0;
	}

	if (w <= 0 || h <= 0 || w > 8192 || h > 8192) {
		fclose(fp);
		return 0;
	}

	/* Skip image ID */
	fseek(fp, idLen, SEEK_CUR);

	/* Read pixel data */
	size_t dataSize = (size_t)w * (size_t)h * (size_t)comp;
	byte *pixels = (byte *)malloc(dataSize);
	if (!pixels) {
		fclose(fp);
		return 0;
	}

	if (fread(pixels, 1, dataSize, fp) != dataSize) {
		free(pixels);
		fclose(fp);
		return 0;
	}
	fclose(fp);

	/* TGA is bottom-up, flip it */
	int rowBytes = w * comp;
	byte *rowBuf = (byte *)malloc((size_t)rowBytes);
	for (int y = 0; y < h / 2; y++) {
		byte *top = pixels + y * rowBytes;
		byte *bot = pixels + (h - 1 - y) * rowBytes;
		memcpy(rowBuf, top, (size_t)rowBytes);
		memcpy(top, bot, (size_t)rowBytes);
		memcpy(bot, rowBuf, (size_t)rowBytes);
	}
	free(rowBuf);

	artHandle_t *ah = (artHandle_t *)lua_newuserdata(L, sizeof(artHandle_t));
	ah->img.width = w;
	ah->img.height = h;
	ah->img.components = comp;
	ah->img.data = pixels;
	lua_pushvalue(L, lua_upvalueindex(1));
	lua_setmetatable(L, -2);
	return 1;
}

/* ======== Image Handles ======== */

typedef struct {
	r_shaderHnd_t *hnd;
} imgHandle_t;

static imgHandle_t *GetImgHandle(lua_State *L, ui_main_t *ui, const char *method, int needLoaded)
{
	uiLAssert(L, uiIsUserData(L, 1, "uiimghandlemeta"),
		"imgHandle:%s() must be used on an image handle", method);
	imgHandle_t *ih = (imgHandle_t *)lua_touserdata(L, 1);
	lua_remove(L, 1);
	if (needLoaded)
		uiLAssert(L, ih->hnd != NULL,
			"imgHandle:%s(): image handle has no image loaded", method);
	return ih;
}

static int l_NewImageHandle(lua_State *L)
{
	imgHandle_t *ih = (imgHandle_t *)lua_newuserdata(L, sizeof(imgHandle_t));
	ih->hnd = NULL;
	lua_pushvalue(L, lua_upvalueindex(1));
	lua_setmetatable(L, -2);
	return 1;
}

static int l_imgHandleGC(lua_State *L)
{
	imgHandle_t *ih = GetImgHandle(L, GetUIPtr(L), "__gc", 0);
	if (ih->hnd) {
		free(ih->hnd);
		ih->hnd = NULL;
	}
	return 0;
}

static int l_imgHandleLoad(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	uiLAssert(L, ui->renderer != NULL, "Renderer is not initialised");
	imgHandle_t *ih = GetImgHandle(L, ui, "Load", 0);
	int n = lua_gettop(L);
	uiLAssert(L, n >= 1, "Usage: imgHandle:Load(fileName[, flag1[, flag2...]])");
	uiLAssert(L, lua_isstring(L, 1),
		"imgHandle:Load() argument 1: expected string, got %s", luaL_typename(L, 1));

	const char *given = lua_tostring(L, 1);
	char filePath[UI_PATH_MAX];
	if (given[0] != '/' && ui->scriptWorkDir[0]) {
		snprintf(filePath, sizeof(filePath), "%s/%s", ui->scriptWorkDir, given);
	} else {
		strncpy(filePath, given, sizeof(filePath) - 1);
		filePath[sizeof(filePath) - 1] = '\0';
	}

	int flags = TF_NOMIPMAP;
	for (int f = 2; f <= n; f++) {
		if (!lua_isstring(L, f))
			continue;
		const char *flag = lua_tostring(L, f);
		if (strcmp(flag, "ASYNC") == 0)
			flags |= TF_ASYNC;
		else if (strcmp(flag, "CLAMP") == 0)
			flags |= TF_CLAMP;
		else if (strcmp(flag, "MIPMAP") == 0)
			flags &= ~TF_NOMIPMAP;
		else if (strcmp(flag, "NEAREST") == 0)
			flags |= TF_NEAREST;
		else
			uiLAssert(L, 0, "imgHandle:Load(): unrecognised flag '%s'", flag);
	}

	if (ih->hnd) {
		free(ih->hnd);
		ih->hnd = NULL;
	}
	ih->hnd = rRegisterShader(ui->renderer, filePath, flags);
	return 0;
}

static int l_imgHandleUnload(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	imgHandle_t *ih = GetImgHandle(L, ui, "Unload", 0);
	if (ih->hnd) {
		free(ih->hnd);
		ih->hnd = NULL;
	}
	return 0;
}

static int l_imgHandleIsValid(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	imgHandle_t *ih = GetImgHandle(L, ui, "IsValid", 0);
	lua_pushboolean(L, ih->hnd != NULL);
	return 1;
}

static int l_imgHandleIsLoading(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	imgHandle_t *ih = GetImgHandle(L, ui, "IsLoading", 1);
	int width, height;
	rGetShaderImageSize(ih->hnd, &width, &height);
	lua_pushboolean(L, width == 0);
	return 1;
}

static int l_imgHandleSetLoadingPriority(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	imgHandle_t *ih = GetImgHandle(L, ui, "SetLoadingPriority", 1);
	int n = lua_gettop(L);
	uiLAssert(L, n >= 1, "Usage: imgHandle:SetLoadingPriority(pri)");
	uiLAssert(L, lua_isnumber(L, 1),
		"imgHandle:SetLoadingPriority() argument 1: expected number, got %s", luaL_typename(L, 1));
	rSetShaderLoadingPriority(ih->hnd, (int)lua_tointeger(L, 1));
	return 0;
}

static int l_imgHandleImageSize(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	imgHandle_t *ih = GetImgHandle(L, ui, "ImageSize", 1);
	int width, height;
	rGetShaderImageSize(ih->hnd, &width, &height);
	lua_pushinteger(L, width);
	lua_pushinteger(L, height);
	return 2;
}

static int parseArtFlags(lua_State *L, ui_main_t *ui, int k, int n)
{
	int flags = TF_NOMIPMAP;
	for (int f = k; f <= n; f++) {
		if (!lua_isstring(L, f))
			continue;
		const char *flag = lua_tostring(L, f);
		if (strcmp(flag, "CLAMP") == 0)
			flags |= TF_CLAMP;
		else if (strcmp(flag, "MIPMAP") == 0)
			flags &= ~TF_NOMIPMAP;
		else if (strcmp(flag, "NEAREST") == 0)
			flags |= TF_NEAREST;
		else
			uiLAssert(L, 0, "unrecognised flag '%s'", flag);
	}
	return flags;
}

static int l_imgHandleLoadArtRectangle(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	imgHandle_t *ih = GetImgHandle(L, ui, "LoadArtRectangle", 0);

	int n = lua_gettop(L);
	uiLAssert(L, n >= 5,
		"Usage: imgHandle:LoadArtRectangle(art, x1, y1, x2, y2[, flag1[, flag2...]])");
	uiLAssert(L, lua_isnumber(L, 2), "argument 2: expected number");
	uiLAssert(L, lua_isnumber(L, 3), "argument 3: expected number");
	uiLAssert(L, lua_isnumber(L, 4), "argument 4: expected number");
	uiLAssert(L, lua_isnumber(L, 5), "argument 5: expected number");
	int x1 = (int)lua_tointeger(L, 2);
	int y1 = (int)lua_tointeger(L, 3);
	int x2 = (int)lua_tointeger(L, 4);
	int y2 = (int)lua_tointeger(L, 5);

	artHandle_t *ah = GetArtHandle(L, ui, "LoadArtRectangle");

	if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
	if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }

	uiLAssert(L, x1 >= 0 && x2 <= ah->img.width,
		"X range %d..%d outside 0..%d", x1, x2, ah->img.width);
	uiLAssert(L, y1 >= 0 && y2 <= ah->img.height,
		"Y range %d..%d outside 0..%d", y1, y2, ah->img.height);

	int dstW = x2 - x1;
	int dstH = y2 - y1;
	int comp = ah->img.components;
	int srcStride = ah->img.width * comp;
	int dstStride = dstW * comp;
	byte *dst = (byte *)calloc(1, (size_t)(dstH * dstStride));

	const byte *src = ah->img.data;
	for (int row = 0; row < dstH; row++) {
		memcpy(dst + row * dstStride,
		       src + (y1 + row) * srcStride + x1 * comp,
		       (size_t)dstStride);
	}

	/* Create GL texture and register shader */
	r_tex_t *tex = r_texCreateFromData(ui->renderer, dstW, dstH, dst, TF_CLAMP);
	free(dst);

	if (ih->hnd)
		free(ih->hnd);

	int flags = parseArtFlags(L, ui, 5, n);
	if (tex) {
		ih->hnd = rRegisterShaderFromImage(ui->renderer, tex->texId, dstW, dstH, flags);
	} else {
		ih->hnd = NULL;
	}
	return 0;
}

static int l_imgHandleLoadArtArcBand(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	imgHandle_t *ih = GetImgHandle(L, ui, "LoadArtArcBand", 0);

	int n = lua_gettop(L);
	uiLAssert(L, n >= 5,
		"Usage: imgHandle:LoadArtArcBand(art, xC, yC, rMin, rMax[, flag1[, flag2...]])");
	uiLAssert(L, lua_isnumber(L, 2), "argument 2: expected number");
	uiLAssert(L, lua_isnumber(L, 3), "argument 3: expected number");
	uiLAssert(L, lua_isnumber(L, 4), "argument 4: expected number");
	uiLAssert(L, lua_isnumber(L, 5), "argument 5: expected number");
	int xC = (int)lua_tointeger(L, 2);
	int yC = (int)lua_tointeger(L, 3);
	int rMin = (int)lua_tointeger(L, 4);
	int rMax = (int)lua_tointeger(L, 5);

	if (rMin > rMax) { int t = rMin; rMin = rMax; rMax = t; }

	artHandle_t *ah = GetArtHandle(L, ui, "LoadArtArcBand");

	int srcW = ah->img.width;
	int srcH = ah->img.height;
	int comp = ah->img.components;
	int x1 = xC - rMax;
	int y1 = yC - rMax;

	uiLAssert(L, xC >= 0 && xC <= srcW, "xC %d outside 0..%d", xC, srcW);
	uiLAssert(L, yC >= 0 && yC <= srcH, "yC %d outside 0..%d", yC, srcH);
	uiLAssert(L, x1 >= 0, "x1 %d out of bounds", x1);
	uiLAssert(L, y1 >= 0, "y1 %d out of bounds", y1);

	int dstW = xC - x1;
	int dstH = yC - y1;
	int srcStride = srcW * comp;
	int dstStride = dstW * comp;
	int dstByteCount = dstH * dstStride;
	byte *dst = (byte *)calloc(1, (size_t)dstByteCount);

	const byte *srcData = ah->img.data;

	/* Copy pixels whose center is between the two radii */
	int rMinSq = (rMin * 2) * (rMin * 2);
	int rMaxSq = (rMax * 2) * (rMax * 2);
	int width2 = dstW * 2;
	int height2 = dstH * 2;

	for (int row = 1; row < height2; row += 2) {
		int dy = height2 - row;
		int colLo = -1;
		for (int x = 1; x < width2; x += 2) {
			int dx = width2 - x;
			int rSq = dx * dx + dy * dy;
			if (rSq <= rMaxSq) {
				colLo = x;
				break;
			}
		}
		if (colLo == -1)
			continue;

		int colHi = width2;
		for (int x = colLo; x < width2; x += 2) {
			int dx = width2 - x;
			int rSq = dx * dx + dy * dy;
			if (rSq < rMinSq) {
				colHi = x;
				break;
			}
		}

		int xLo = colLo / 2;
		int xHi = colHi / 2;
		if (xLo != xHi) {
			int row2 = row / 2;
			int spanBytes = (xHi - xLo) * comp;
			int srcRow = y1 + row2;
			int srcCol = x1 + xLo;
			const byte *sp = srcData + srcRow * srcStride + srcCol * comp;
			byte *dp = dst + row2 * dstStride + xLo * comp;
			memcpy(dp, sp, (size_t)spanBytes);
		}
	}

	int flags = parseArtFlags(L, ui, 5, n);

	r_tex_t *tex = r_texCreateFromData(ui->renderer, dstW, dstH, dst, TF_CLAMP);
	free(dst);

	if (ih->hnd)
		free(ih->hnd);

	if (tex) {
		ih->hnd = rRegisterShaderFromImage(ui->renderer, tex->texId, dstW, dstH, flags);
	} else {
		ih->hnd = NULL;
	}
	return 0;
}

/* ======== Rendering ======== */

static int l_RenderInit(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	int n = lua_gettop(L);
	int dpiAware = 0;
	for (int i = 1; i <= n; i++) {
		uiLAssert(L, lua_isstring(L, i),
			"RenderInit() argument %d: expected string, got %s", i, luaL_typename(L, i));
		if (strcmp(lua_tostring(L, i), "DPI_AWARE") == 0)
			dpiAware = 1;
	}
	if (!ui->renderer) {
		ui->renderer = (r_renderer_t *)calloc(1, sizeof(r_renderer_t));
		rInit(ui->renderer, ui->sys, dpiAware ? F_DPI_AWARE : 0);
	}
	return 0;
}

static int l_GetScreenSize(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	uiLAssert(L, ui->renderer != NULL, "Renderer is not initialised");
	lua_pushinteger(L, rVirtualScreenWidth(ui->renderer));
	lua_pushinteger(L, rVirtualScreenHeight(ui->renderer));
	return 2;
}

static int l_GetScreenScale(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	uiLAssert(L, ui->renderer != NULL, "Renderer is not initialised");
	lua_pushnumber(L, rVirtualScreenScaleFactor(ui->renderer));
	return 1;
}

static int l_GetVirtualScreenSize(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	uiLAssert(L, ui->renderer != NULL, "Renderer is not initialised");
	int w = rVirtualScreenWidth(ui->renderer);
	int h = rVirtualScreenHeight(ui->renderer);
	float scale = rVirtualScreenScaleFactor(ui->renderer);
	if (scale != 1.0f) {
		w = (int)((float)w / scale);
		h = (int)((float)h / scale);
	}
	lua_pushinteger(L, w);
	lua_pushinteger(L, h);
	return 2;
}

static int l_SetClearColor(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	uiLAssert(L, ui->renderer != NULL, "Renderer is not initialised");
	int n = lua_gettop(L);
	uiLAssert(L, n >= 3, "Usage: SetClearColor(red, green, blue[, alpha])");
	col4_t color;
	for (int i = 1; i <= 3; i++) {
		uiLAssert(L, lua_isnumber(L, i),
			"SetClearColor() argument %d: expected number, got %s", i, luaL_typename(L, i));
		color[i - 1] = (float)lua_tonumber(L, i);
	}
	if (n >= 4 && !lua_isnil(L, 4)) {
		uiLAssert(L, lua_isnumber(L, 4),
			"SetClearColor() argument 4: expected number or nil, got %s", luaL_typename(L, 4));
		color[3] = (float)lua_tonumber(L, 4);
	} else {
		color[3] = 1.0f;
	}
	rSetClearColor(ui->renderer, color);
	return 0;
}

static int l_SetDrawLayer(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	uiLAssert(L, ui->renderer != NULL, "Renderer is not initialised");
	uiLAssert(L, ui->renderEnable, "SetDrawLayer() called outside of OnFrame");
	int n = lua_gettop(L);
	uiLAssert(L, n >= 1, "Usage: SetDrawLayer({layer|nil}[, subLayer])");
	uiLAssert(L, lua_isnumber(L, 1) || lua_isnil(L, 1),
		"SetDrawLayer() argument 1: expected number or nil, got %s", luaL_typename(L, 1));
	if (n >= 2)
		uiLAssert(L, lua_isnumber(L, 2),
			"SetDrawLayer() argument 2: expected number, got %s", luaL_typename(L, 2));

	if (lua_isnil(L, 1)) {
		uiLAssert(L, n >= 2, "SetDrawLayer(): must provide subLayer if layer is nil");
		rSetDrawSubLayer(ui->renderer, (int)lua_tointeger(L, 2));
	} else if (n >= 2) {
		rSetDrawLayer(ui->renderer, (int)lua_tointeger(L, 1), (int)lua_tointeger(L, 2));
	} else {
		rSetDrawLayer(ui->renderer, (int)lua_tointeger(L, 1), 0);
	}
	return 0;
}

static int l_GetDrawLayer(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	uiLAssert(L, ui->renderer != NULL, "Renderer is not initialised");
	lua_pushinteger(L, rGetDrawLayer(ui->renderer));
	return 1;
}

static int l_SetViewport(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	uiLAssert(L, ui->renderer != NULL, "Renderer is not initialised");
	uiLAssert(L, ui->renderEnable, "SetViewport() called outside of OnFrame");
	int n = lua_gettop(L);
	float dpiScale = rVirtualScreenScaleFactor(ui->renderer);
	if (n) {
		uiLAssert(L, n >= 4, "Usage: SetViewport([x, y, width, height])");
		for (int i = 1; i <= 4; i++)
			uiLAssert(L, lua_isnumber(L, i),
				"SetViewport() argument %d: expected number, got %s", i, luaL_typename(L, i));
		int vpX = (int)lround(lua_tonumber(L, 1) * dpiScale);
		int vpY = (int)lround(lua_tonumber(L, 2) * dpiScale);
		int vpW = (int)ceil(lua_tonumber(L, 3) * dpiScale);
		int vpH = (int)ceil(lua_tonumber(L, 4) * dpiScale);
		rSetViewport(ui->renderer, vpX, vpY, vpW, vpH);
	} else {
		rSetViewport(ui->renderer, 0, 0, 0, 0);
	}
	return 0;
}

static int l_SetBlendMode(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	uiLAssert(L, ui->renderer != NULL, "Renderer is not initialised");
	uiLAssert(L, ui->renderEnable, "SetBlendMode() called outside of OnFrame");
	int n = lua_gettop(L);
	uiLAssert(L, n >= 1, "Usage: SetBlendMode(mode)");
	static const char *modeMap[] = { "ALPHA", "PREALPHA", "ADDITIVE", NULL };
	rSetBlendMode(ui->renderer, luaL_checkoption(L, 1, "ALPHA", modeMap));
	return 0;
}

static int l_SetDrawColor(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	uiLAssert(L, ui->renderer != NULL, "Renderer is not initialised");
	uiLAssert(L, ui->renderEnable, "SetDrawColor() called outside of OnFrame");
	int n = lua_gettop(L);
	uiLAssert(L, n >= 1,
		"Usage: SetDrawColor(red, green, blue[, alpha]) or SetDrawColor(escapeStr)");
	col4_t color;
	if (lua_type(L, 1) == LUA_TSTRING) {
		const char *str = lua_tostring(L, 1);
		int len = IsColorEscape(str);
		uiLAssert(L, len, "SetDrawColor() argument 1: invalid color escape sequence");
		ReadColorEscape(str, len, color);
		color[3] = 1.0f;
	} else {
		uiLAssert(L, n >= 3,
			"Usage: SetDrawColor(red, green, blue[, alpha]) or SetDrawColor(escapeStr)");
		for (int i = 1; i <= 3; i++) {
			int isnum;
			lua_Number val = lua_tonumberx(L, i, &isnum);
			if (!isnum)
				uiLAssert(L, 0, "SetDrawColor() argument %d: expected number, got %s",
					i, luaL_typename(L, i));
			color[i - 1] = (float)val;
		}
		if (n >= 4 && !lua_isnil(L, 4)) {
			int isnum;
			lua_Number val = lua_tonumberx(L, 4, &isnum);
			if (!isnum)
				uiLAssert(L, 0, "SetDrawColor() argument 4: expected number or nil, got %s",
					luaL_typename(L, 4));
			color[3] = (float)val;
		} else {
			color[3] = 1.0f;
		}
	}
	rDrawColor(ui->renderer, color);

	/* Store last applied color */
	col4_t finalColor;
	rGetDrawColor(ui->renderer, finalColor);
	ui->lastColor[0] = finalColor[0];
	ui->lastColor[1] = finalColor[1];
	ui->lastColor[2] = finalColor[2];
	ui->lastColor[3] = finalColor[3];
	return 0;
}

static int l_GetDrawColor(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	uiLAssert(L, ui->renderer != NULL, "Renderer is not initialised");
	lua_pushnumber(L, ui->lastColor[0]);
	lua_pushnumber(L, ui->lastColor[1]);
	lua_pushnumber(L, ui->lastColor[2]);
	lua_pushnumber(L, ui->lastColor[3]);
	return 4;
}

static int l_DrawImage(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	uiLAssert(L, ui->renderer != NULL, "Renderer is not initialised");
	uiLAssert(L, ui->renderEnable, "DrawImage() called outside of OnFrame");
	int n = lua_gettop(L);
	const char *usage = "Usage: DrawImage({imgHandle|nil}, left, top, width, height"
		"[, tcLeft, tcTop, tcRight, tcBottom][, stackIdx[, mask]])";
	uiLAssert(L, n >= 5, "%s", usage);

	if (!lua_isnil(L, 1) && !uiIsUserData(L, 1, "uiimghandlemeta"))
		uiLAssert(L, 0, "DrawImage() argument 1: expected image handle or nil, got %s",
			luaL_typename(L, 1));

	r_shaderHnd_t *hnd = NULL;
	float dpiScale = rVirtualScreenScaleFactor(ui->renderer);
	float px[2][2] = {{0}}; /* [0][0]=left, [0][1]=top, [1][0]=right, [1][1]=bottom */
	float uv[2][2] = {{0,0},{1,1}};
	int stackLayer = 0;
	int hasMask = 0;
	int maskLayer = 0;

	int k = 1;
	/* Image handle */
	if (!lua_isnil(L, k)) {
		imgHandle_t *ih = (imgHandle_t *)lua_touserdata(L, k);
		uiLAssert(L, ih->hnd != NULL, "DrawImage(): image handle has no image loaded");
		hnd = ih->hnd;
	}
	k++;

	/* Position: left, top, width, height */
	for (int i = 0; i < 4; i++) {
		int isNum;
		lua_Number val = lua_tonumberx(L, k + i, &isNum);
		uiLAssert(L, isNum, "DrawImage() argument %d: expected number, got %s",
			k + i, luaL_typename(L, k + i));
		px[i / 2][i % 2] = (float)val * dpiScale;
	}
	k += 4;

	/* Optional UV coordinates */
	if (n >= 9) {
		for (int i = 0; i < 4; i++) {
			int isNum;
			lua_Number val = lua_tonumberx(L, k + i, &isNum);
			uiLAssert(L, isNum, "DrawImage() argument %d: expected number, got %s",
				k + i, luaL_typename(L, k + i));
			uv[i / 2][i % 2] = (float)val;
		}
		k += 4;
	}

	/* Optional stack layer */
	if (k <= n && (n == 6 || n == 7 || n == 10 || n == 11)) {
		int isInt;
		int val = (int)lua_tointegerx(L, k, &isInt);
		uiLAssert(L, isInt, "DrawImage() argument %d: expected integer, got %s",
			k, luaL_typename(L, k));
		uiLAssert(L, val > 0, "DrawImage() argument %d: expected positive integer, got %d", k, val);
		stackLayer = val - 1;
		k++;
	}

	/* Optional mask layer */
	if (k <= n && (n == 7 || n == 11)) {
		if (!lua_isnil(L, k)) {
			int isInt;
			int val = (int)lua_tointegerx(L, k, &isInt);
			uiLAssert(L, isInt, "DrawImage() argument %d: expected integer or nil, got %s",
				k, luaL_typename(L, k));
			uiLAssert(L, val > 0, "DrawImage() argument %d: expected positive integer, got %d", k, val);
			maskLayer = val - 1;
			hasMask = 1;
		}
		k++;
	}

	rDrawImage(ui->renderer, hnd,
		px[0][0], px[0][1], px[1][0], px[1][1],
		uv[0][0], uv[0][1], uv[1][0], uv[1][1],
		stackLayer, hasMask ? maskLayer : 0);
	return 0;
}

static int l_DrawImageQuad(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	uiLAssert(L, ui->renderer != NULL, "Renderer is not initialised");
	uiLAssert(L, ui->renderEnable, "DrawImageQuad() called outside of OnFrame");
	int n = lua_gettop(L);
	const char *usage = "Usage: DrawImageQuad({imgHandle|nil}, x1,y1, x2,y2, x3,y3, x4,y4"
		"[, s1,t1, s2,t2, s3,t3, s4,t4][, stackIdx[, mask]])";
	uiLAssert(L, n >= 9, "%s", usage);

	if (!lua_isnil(L, 1) && !uiIsUserData(L, 1, "uiimghandlemeta"))
		uiLAssert(L, 0, "DrawImageQuad() argument 1: expected image handle or nil, got %s",
			luaL_typename(L, 1));

	r_shaderHnd_t *hnd = NULL;
	float dpiScale = rVirtualScreenScaleFactor(ui->renderer);
	float px[4][2];
	float uv[4][2] = {{0,0},{1,0},{1,1},{0,1}};
	int stackLayer = 0;
	int hasMask = 0;
	int maskLayer = 0;

	int k = 1;
	/* Image handle */
	if (!lua_isnil(L, k)) {
		imgHandle_t *ih = (imgHandle_t *)lua_touserdata(L, k);
		uiLAssert(L, ih->hnd != NULL, "DrawImageQuad(): image handle has no image loaded");
		hnd = ih->hnd;
	}
	k++;

	/* 4 corners: x1,y1, x2,y2, x3,y3, x4,y4 */
	for (int i = 0; i < 8; i++) {
		int isNum;
		lua_Number val = lua_tonumberx(L, k + i, &isNum);
		uiLAssert(L, isNum, "DrawImageQuad() argument %d: expected number, got %s",
			k + i, luaL_typename(L, k + i));
		px[i / 2][i % 2] = (float)val * dpiScale;
	}
	k += 8;

	/* Optional UV coords: s1,t1 .. s4,t4 */
	if (n >= 17) {
		for (int i = 0; i < 8; i++) {
			int isNum;
			lua_Number val = lua_tonumberx(L, k + i, &isNum);
			uiLAssert(L, isNum, "DrawImageQuad() argument %d: expected number, got %s",
				k + i, luaL_typename(L, k + i));
			uv[i / 2][i % 2] = (float)val;
		}
		k += 8;
	}

	/* Optional stack layer */
	if (k <= n && (n == 10 || n == 11 || n == 18 || n == 19)) {
		int isInt;
		int val = (int)lua_tointegerx(L, k, &isInt);
		uiLAssert(L, isInt, "DrawImageQuad() argument %d: expected integer, got %s",
			k, luaL_typename(L, k));
		uiLAssert(L, val > 0, "expected positive integer");
		stackLayer = val - 1;
		k++;
	}

	/* Optional mask layer */
	if (k <= n && (n == 11 || n == 19)) {
		if (!lua_isnil(L, k)) {
			int isInt;
			int val = (int)lua_tointegerx(L, k, &isInt);
			uiLAssert(L, isInt, "expected integer or nil");
			uiLAssert(L, val > 0, "expected positive integer");
			maskLayer = val - 1;
			hasMask = 1;
		}
		k++;
	}

	rDrawImageQuad(ui->renderer, hnd,
		px[0][0], px[0][1], px[1][0], px[1][1],
		px[2][0], px[2][1], px[3][0], px[3][1],
		uv[0][0], uv[0][1], uv[1][0], uv[1][1],
		uv[2][0], uv[2][1], uv[3][0], uv[3][1],
		stackLayer, hasMask ? maskLayer : 0);
	return 0;
}

static int l_DrawString(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	uiLAssert(L, ui->renderer != NULL, "Renderer is not initialised");
	uiLAssert(L, ui->renderEnable, "DrawString() called outside of OnFrame");
	int n = lua_gettop(L);
	uiLAssert(L, n >= 6, "Usage: DrawString(left, top, align, height, font, text)");
	uiLAssert(L, lua_isnumber(L, 1), "DrawString() argument 1: expected number, got %s",
		luaL_typename(L, 1));
	uiLAssert(L, lua_isnumber(L, 2), "DrawString() argument 2: expected number, got %s",
		luaL_typename(L, 2));
	uiLAssert(L, lua_isnumber(L, 4), "DrawString() argument 4: expected number, got %s",
		luaL_typename(L, 4));
	uiLAssert(L, lua_isstring(L, 6), "DrawString() argument 6: expected string, got %s",
		luaL_typename(L, 6));

	float dpiScale = rVirtualScreenScaleFactor(ui->renderer);
	float left = (float)lua_tonumber(L, 1) * dpiScale;
	float top = (float)lua_tonumber(L, 2) * dpiScale;
	int h = scaledHeight(L, ui, 4);
	int align = parseAlign(L, 3);
	int font = parseFont(L, 5);
	const char *text = lua_tostring(L, 6);

	rDrawString(ui->renderer, left, top, align, h, NULL, font, text);

	/* Update last color after DrawString processes color codes */
	col4_t finalColor;
	rGetDrawColor(ui->renderer, finalColor);
	ui->lastColor[0] = finalColor[0];
	ui->lastColor[1] = finalColor[1];
	ui->lastColor[2] = finalColor[2];
	ui->lastColor[3] = finalColor[3];

	return 0;
}

static int l_DrawStringWidth(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	uiLAssert(L, ui->renderer != NULL, "Renderer is not initialised");
	int n = lua_gettop(L);
	uiLAssert(L, n >= 3, "Usage: DrawStringWidth(height, font, text)");
	uiLAssert(L, lua_isnumber(L, 1), "DrawStringWidth() argument 1: expected number, got %s",
		luaL_typename(L, 1));
	uiLAssert(L, lua_isstring(L, 3), "DrawStringWidth() argument 3: expected string, got %s",
		luaL_typename(L, 3));

	float dpiScale = rVirtualScreenScaleFactor(ui->renderer);
	int h = scaledHeight(L, ui, 1);
	int font = parseFont(L, 2);
	const char *text = lua_tostring(L, 3);

	double pw = rDrawStringWidth(ui->renderer, h, font, text);
	lua_pushnumber(L, pw / dpiScale);
	return 1;
}

static int l_DrawStringCursorIndex(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	uiLAssert(L, ui->renderer != NULL, "Renderer is not initialised");
	int n = lua_gettop(L);
	uiLAssert(L, n >= 5, "Usage: DrawStringCursorIndex(height, font, text, cursorX, cursorY)");
	uiLAssert(L, lua_isnumber(L, 1), "argument 1: expected number");
	uiLAssert(L, lua_isstring(L, 3), "argument 3: expected string");
	uiLAssert(L, lua_isnumber(L, 4), "argument 4: expected number");
	uiLAssert(L, lua_isnumber(L, 5), "argument 5: expected number");

	float dpiScale = rVirtualScreenScaleFactor(ui->renderer);
	int h = scaledHeight(L, ui, 1);
	int font = parseFont(L, 2);
	const char *text = lua_tostring(L, 3);
	int cx = (int)lround(lua_tonumber(L, 4) * dpiScale);
	int cy = (int)lround(lua_tonumber(L, 5) * dpiScale);

	lua_pushinteger(L, (lua_Integer)rDrawStringCursorIndex(ui->renderer, h, font, text, cx, cy) + 1);
	return 1;
}

static int l_StripEscapes(lua_State *L)
{
	int n = lua_gettop(L);
	uiLAssert(L, n >= 1, "Usage: StripEscapes(string)");
	uiLAssert(L, lua_isstring(L, 1),
		"StripEscapes() argument 1: expected string, got %s", luaL_typename(L, 1));
	const char *str = lua_tostring(L, 1);
	size_t len = strlen(str);
	char *strip = (char *)malloc(len + 1);
	char *p = strip;
	while (*str) {
		int esclen = IsColorEscape(str);
		if (esclen) {
			str += esclen;
		} else {
			*(p++) = *(str++);
		}
	}
	*p = 0;
	lua_pushstring(L, strip);
	free(strip);
	return 1;
}

static int l_GetAsyncCount(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	uiLAssert(L, ui->renderer != NULL, "Renderer is not initialised");
	lua_pushinteger(L, rGetAsyncCount(ui->renderer));
	return 1;
}

static int l_SetDPIScaleOverridePercent(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	uiLAssert(L, ui->renderer != NULL, "Renderer is not initialised");
	int n = lua_gettop(L);
	uiLAssert(L, n >= 1, "Usage: SetDPIScaleOverridePercent(percent)");
	uiLAssert(L, lua_isnumber(L, 1),
		"SetDPIScaleOverridePercent() argument 1: expected number, got %s", luaL_typename(L, 1));
	ui->renderer->dpiScaleOverridePercent = (int)lua_tointeger(L, 1);
	return 0;
}

static int l_GetDPIScaleOverridePercent(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	uiLAssert(L, ui->renderer != NULL, "Renderer is not initialised");
	lua_pushinteger(L, ui->renderer->dpiScaleOverridePercent);
	return 1;
}

/* ======== Search Handles ======== */

typedef struct {
	DIR          *dir;
	char          basePath[UI_PATH_MAX];
	char          pattern[UI_PATH_MAX];
	int           dirOnly;
	char          foundName[UI_PATH_MAX];
	long          foundSize;
	time_t        foundModified;
	int           foundIsDir;
	int           valid;
} searchHandle_t;

static void searchNext(searchHandle_t *sh)
{
	struct dirent *ent;
	while ((ent = readdir(sh->dir)) != NULL) {
		if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
			continue;

		int isDir = 0;
#ifdef _DIRENT_D_TYPE
		if (ent->d_type == DT_DIR)
			isDir = 1;
#else
		{
			char full[UI_PATH_MAX];
			snprintf(full, sizeof(full), "%s/%s", sh->basePath, ent->d_name);
			struct stat st;
			if (stat(full, &st) == 0 && S_ISDIR(st.st_mode))
				isDir = 1;
		}
#endif

		if (sh->dirOnly != isDir)
			continue;

		/* Match pattern using fnmatch */
		if (sh->pattern[0] && fnmatch(sh->pattern, ent->d_name, 0) != 0)
			continue;

		/* Found a match */
		strncpy(sh->foundName, ent->d_name, UI_PATH_MAX - 1);
		sh->foundName[UI_PATH_MAX - 1] = '\0';
		sh->foundIsDir = isDir;

		/* Get file info */
		char full[UI_PATH_MAX];
		snprintf(full, sizeof(full), "%s/%s", sh->basePath, ent->d_name);
		struct stat st;
		if (stat(full, &st) == 0) {
			sh->foundSize = (long)st.st_size;
			sh->foundModified = st.st_mtime;
		} else {
			sh->foundSize = 0;
			sh->foundModified = 0;
		}
		sh->valid = 1;
		return;
	}
	sh->valid = 0;
}

static searchHandle_t *GetSearchHandle(lua_State *L, ui_main_t *ui, const char *method, int needValid)
{
	uiLAssert(L, uiIsUserData(L, 1, "uisearchhandlemeta"),
		"searchHandle:%s() must be used on a search handle", method);
	searchHandle_t *sh = (searchHandle_t *)lua_touserdata(L, 1);
	lua_remove(L, 1);
	if (needValid)
		uiLAssert(L, sh->valid,
			"searchHandle:%s(): search handle is no longer valid", method);
	return sh;
}

static int l_searchHandleGC(lua_State *L)
{
	searchHandle_t *sh = GetSearchHandle(L, GetUIPtr(L), "__gc", 0);
	if (sh->dir) {
		closedir(sh->dir);
		sh->dir = NULL;
	}
	return 0;
}

static int l_NewFileSearch(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	int n = lua_gettop(L);
	uiLAssert(L, n >= 1, "Usage: NewFileSearch(spec[, findDirectories])");
	uiLAssert(L, lua_isstring(L, 1),
		"NewFileSearch() argument 1: expected string, got %s", luaL_typename(L, 1));

	const char *spec = lua_tostring(L, 1);
	int dirOnly = lua_toboolean(L, 2);
	char dirPath[UI_PATH_MAX];
	const char *slash = strrchr(spec, '/');

	if (slash) {
		size_t dirLen = (size_t)(slash - spec);
		strncpy(dirPath, spec, dirLen);
		dirPath[dirLen] = '\0';
		/* Use spec as pattern */
		const char *pat = slash + 1;
		/* Build absolute dir if relative */
		if (dirPath[0] != '/') {
			char absDir[UI_PATH_MAX];
			snprintf(absDir, sizeof(absDir), "%s/%s", ui->scriptWorkDir, dirPath);
			strncpy(dirPath, absDir, sizeof(dirPath) - 1);
		}
		searchHandle_t *sh = (searchHandle_t *)lua_newuserdata(L, sizeof(searchHandle_t));
		memset(sh, 0, sizeof(*sh));
		strncpy(sh->basePath, dirPath, UI_PATH_MAX - 1);
		strncpy(sh->pattern, pat, UI_PATH_MAX - 1);
		sh->dirOnly = dirOnly;
		sh->dir = opendir(dirPath);
		if (!sh->dir)
			return 0;
		sh->valid = 0;
		searchNext(sh);
		if (!sh->valid) {
			closedir(sh->dir);
			return 0;
		}
		lua_pushvalue(L, lua_upvalueindex(1));
		lua_setmetatable(L, -2);
		return 1;
	} else {
		/* No slash: spec is a pattern in the current dir */
		searchHandle_t *sh = (searchHandle_t *)lua_newuserdata(L, sizeof(searchHandle_t));
		memset(sh, 0, sizeof(*sh));
		strncpy(sh->basePath, ui->scriptWorkDir, UI_PATH_MAX - 1);
		strncpy(sh->pattern, spec, UI_PATH_MAX - 1);
		sh->dirOnly = dirOnly;
		sh->dir = opendir(ui->scriptWorkDir);
		if (!sh->dir)
			return 0;
		sh->valid = 0;
		searchNext(sh);
		if (!sh->valid) {
			closedir(sh->dir);
			return 0;
		}
		lua_pushvalue(L, lua_upvalueindex(1));
		lua_setmetatable(L, -2);
		return 1;
	}
}

static int l_searchHandleNextFile(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	searchHandle_t *sh = GetSearchHandle(L, ui, "NextFile", 1);
	sh->valid = 0;
	searchNext(sh);
	if (!sh->valid) {
		closedir(sh->dir);
		sh->dir = NULL;
		return 0;
	}
	lua_pushboolean(L, 1);
	return 1;
}

static int l_searchHandleGetFileName(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	searchHandle_t *sh = GetSearchHandle(L, ui, "GetFileName", 1);
	lua_pushstring(L, sh->foundName);
	return 1;
}

static int l_searchHandleGetFileSize(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	searchHandle_t *sh = GetSearchHandle(L, ui, "GetFileSize", 1);
	lua_pushinteger(L, (lua_Integer)sh->foundSize);
	return 1;
}

static int l_searchHandleGetFileModifiedTime(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	searchHandle_t *sh = GetSearchHandle(L, ui, "GetFileModifiedTime", 1);
	lua_pushnumber(L, (double)sh->foundModified);
	return 1;
}

/* ======== General Functions ======== */

static int l_GetCloudProvider(lua_State *L)
{
	/* Cloud provider detection not available on Linux */
	(void)L;
	return 0;
}

static int l_SetWindowTitle(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	int n = lua_gettop(L);
	uiLAssert(L, n >= 1, "Usage: SetWindowTitle(title)");
	uiLAssert(L, lua_isstring(L, 1),
		"SetWindowTitle() argument 1: expected string, got %s", luaL_typename(L, 1));
	if (ui->sys->video)
		sysVideoSetTitle(ui->sys->video, lua_tostring(L, 1));
	return 0;
}

static int l_GetCursorPos(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	float dpiScale = ui->renderer ? rVirtualScreenScaleFactor(ui->renderer) : 1.0f;
	lua_pushinteger(L, (lua_Integer)lround((float)ui->cursorX / dpiScale));
	lua_pushinteger(L, (lua_Integer)lround((float)ui->cursorY / dpiScale));
	return 2;
}

static int l_SetCursorPos(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	int n = lua_gettop(L);
	uiLAssert(L, n >= 2, "Usage: SetCursorPos(x, y)");
	uiLAssert(L, lua_isnumber(L, 1),
		"SetCursorPos() argument 1: expected number, got %s", luaL_typename(L, 1));
	uiLAssert(L, lua_isnumber(L, 2),
		"SetCursorPos() argument 2: expected number, got %s", luaL_typename(L, 2));
	if (ui->sys->video)
		sysVideoSetRelativeCursor(ui->sys->video,
			(int)lua_tonumber(L, 1), (int)lua_tonumber(L, 2));
	return 0;
}

static int l_ShowCursor(lua_State *L)
{
	int n = lua_gettop(L);
	uiLAssert(L, n >= 1, "Usage: ShowCursor(doShow)");
	return 0;
}

static int l_IsKeyDown(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	int n = lua_gettop(L);
	uiLAssert(L, n >= 1, "Usage: IsKeyDown(keyName)");
	uiLAssert(L, lua_isstring(L, 1),
		"IsKeyDown() argument 1: expected string, got %s", luaL_typename(L, 1));
	size_t len;
	const char *kname = lua_tolstring(L, 1, &len);
	uiLAssert(L, len >= 1, "IsKeyDown() argument 1: string is empty");
	int key = uiKeyForName(kname);
	uiLAssert(L, key, "IsKeyDown(): unrecognised key name");
	lua_pushboolean(L, sysIsKeyDown(key));
	return 1;
}

static int l_Copy(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	int n = lua_gettop(L);
	uiLAssert(L, n >= 1, "Usage: Copy(string)");
	uiLAssert(L, lua_isstring(L, 1),
		"Copy() argument 1: expected string, got %s", luaL_typename(L, 1));
	sysClipboardCopy(lua_tostring(L, 1));
	return 0;
}

static int l_Paste(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	char *data = sysClipboardPaste();
	if (data) {
		lua_pushstring(L, data);
		FreeString(data);
		return 1;
	}
	return 0;
}

static int l_Deflate(lua_State *L)
{
	int n = lua_gettop(L);
	uiLAssert(L, n >= 1, "Usage: Deflate(string)");
	uiLAssert(L, lua_isstring(L, 1),
		"Deflate() argument 1: expected string, got %s", luaL_typename(L, 1));

	size_t inLen;
	const byte *in = (const byte *)lua_tolstring(L, 1, &inLen);
	size_t maxInLen = (size_t)128 << 20;
	if (inLen > maxInLen) {
		lua_pushnil(L);
		lua_pushstring(L, "Input larger than 128 MiB");
		return 2;
	}

	z_stream z;
	memset(&z, 0, sizeof(z));
	deflateInit(&z, 9);
	uLong outSz = deflateBound(&z, (uLong)inLen);
	size_t maxOutLen = (size_t)128 << 20;
	if (outSz > maxOutLen) outSz = (uLong)maxOutLen;
	byte *out = (byte *)malloc((size_t)outSz);
	z.next_in = (Bytef *)in;
	z.avail_in = (uInt)inLen;
	z.next_out = out;
	z.avail_out = (uInt)outSz;
	int err = deflate(&z, Z_FINISH);
	deflateEnd(&z);
	if (err == Z_STREAM_END) {
		lua_pushlstring(L, (const char *)out, z.total_out);
		free(out);
		return 1;
	}
	free(out);
	lua_pushnil(L);
	lua_pushstring(L, zError(err));
	return 2;
}

static int l_Inflate(lua_State *L)
{
	int n = lua_gettop(L);
	uiLAssert(L, n >= 1, "Usage: Inflate(string)");
	uiLAssert(L, lua_isstring(L, 1),
		"Inflate() argument 1: expected string, got %s", luaL_typename(L, 1));

	size_t inLen;
	const byte *in = (const byte *)lua_tolstring(L, 1, &inLen);
	size_t maxInLen = (size_t)128 << 20;
	if (inLen > maxInLen) {
		lua_pushnil(L);
		lua_pushstring(L, "Input larger than 128 MiB");
		return 2;
	}

	uInt outSz = (uInt)(inLen * 4);
	if (outSz < 256) outSz = 256;
	byte *out = (byte *)malloc((size_t)outSz);

	z_stream z;
	memset(&z, 0, sizeof(z));
	z.next_in = (Bytef *)in;
	z.avail_in = (uInt)inLen;
	z.next_out = out;
	z.avail_out = outSz;
	inflateInit(&z);

	int err;
	while ((err = inflate(&z, Z_NO_FLUSH)) == Z_OK) {
		if (z.avail_out == 0) {
			size_t maxOutLen = (size_t)128 << 20;
			if ((size_t)outSz > maxOutLen) break;
			uInt newSz = outSz * 2;
			out = (byte *)realloc(out, (size_t)newSz);
			z.next_out = out + outSz;
			z.avail_out = outSz;
			outSz = newSz;
		}
	}
	inflateEnd(&z);

	if (err == Z_STREAM_END) {
		lua_pushlstring(L, (const char *)out, z.total_out);
		free(out);
		return 1;
	}
	free(out);
	lua_pushnil(L);
	lua_pushstring(L, zError(err));
	return 2;
}

static int l_GetTime(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	lua_pushinteger(L, sysGetTime());
	return 1;
}

static int l_GetScriptPath(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	lua_pushstring(L, ui->scriptPath);
	return 1;
}

static int l_GetRuntimePath(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	lua_pushstring(L, ui->sys->basePath);
	return 1;
}

static int l_GetUserPath(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	if (ui->sys->userPath[0]) {
		lua_pushstring(L, ui->sys->userPath);
		return 1;
	}
	return 0;
}

static int l_MakeDir(lua_State *L)
{
	int n = lua_gettop(L);
	uiLAssert(L, n >= 1, "Usage: MakeDir(path)");
	uiLAssert(L, lua_isstring(L, 1),
		"MakeDir() argument 1: expected string, got %s", luaL_typename(L, 1));
	const char *path = lua_tostring(L, 1);
	if (mkdir(path, 0755) == 0) {
		lua_pushboolean(L, 1);
		return 1;
	}
	lua_pushnil(L);
	lua_pushstring(L, strerror(errno));
	return 2;
}

static int l_RemoveDir(lua_State *L)
{
	int n = lua_gettop(L);
	uiLAssert(L, n >= 1, "Usage: RemoveDir(path[, recurse])");
	uiLAssert(L, lua_isstring(L, 1),
		"RemoveDir() argument 1: expected string, got %s", luaL_typename(L, 1));
	const char *path = lua_tostring(L, 1);
	int recursive = 0;
	if (n > 1) {
		uiLAssert(L, lua_isboolean(L, 2),
			"RemoveDir() argument 2: expected boolean, got %s", luaL_typename(L, 2));
		recursive = lua_toboolean(L, 2);
	}

	if (recursive) {
		/* Simple recursive remove using system */
		char cmd[UI_PATH_MAX + 32];
		snprintf(cmd, sizeof(cmd), "rm -rf '%s'", path);
		int ret = system(cmd);
		if (ret == 0) {
			lua_pushboolean(L, 1);
			return 1;
		}
	} else {
		if (rmdir(path) == 0) {
			lua_pushboolean(L, 1);
			return 1;
		}
	}
	lua_pushnil(L);
	lua_pushstring(L, strerror(errno));
	return 2;
}

static int l_SetWorkDir(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	int n = lua_gettop(L);
	uiLAssert(L, n >= 1, "Usage: SetWorkDir(path)");
	uiLAssert(L, lua_isstring(L, 1),
		"SetWorkDir() argument 1: expected string, got %s", luaL_typename(L, 1));
	const char *path = lua_tostring(L, 1);
	if (sysSetWorkDir(path) != 0) {
		strncpy(ui->scriptWorkDir, path, UI_PATH_MAX - 1);
		ui->scriptWorkDir[UI_PATH_MAX - 1] = '\0';
	}
	return 0;
}

static int l_GetWorkDir(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	lua_pushstring(L, ui->scriptWorkDir);
	return 1;
}

/* ======== Sub-script Functions ======== */

static int l_LaunchSubScript(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	int n = lua_gettop(L);
	uiLAssert(L, n >= 3, "Usage: LaunchSubScript(scriptText, funcList, subList[, ...])");
	for (int i = 1; i <= 3; i++)
		uiLAssert(L, lua_isstring(L, i),
			"LaunchSubScript() argument %d: expected string, got %s", i, luaL_typename(L, i));
	for (int i = 4; i <= n; i++)
		uiLAssert(L, lua_isnil(L, i) || lua_isboolean(L, i) ||
			lua_isnumber(L, i) || lua_isstring(L, i),
			"LaunchSubScript() argument %d: only nil, boolean, number and string types allowed", i);

	const char *scriptText = lua_tostring(L, 1);
	const char *funcList = lua_tostring(L, 2);
	const char *subList = lua_tostring(L, 3);
	int extraArgc = n - 3;

	int slot = uiSubScriptLaunch(ui, scriptText, funcList, subList, extraArgc);
	if (slot >= 0) {
		lua_pushlightuserdata(L, (void *)(uintptr_t)slot);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

static int l_AbortSubScript(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	int n = lua_gettop(L);
	uiLAssert(L, n >= 1, "Usage: AbortSubScript(ssID)");
	uiLAssert(L, lua_islightuserdata(L, 1),
		"AbortSubScript() argument 1: expected subscript ID, got %s", luaL_typename(L, 1));
	int slot = (int)(uintptr_t)lua_touserdata(L, 1);
	uiLAssert(L, slot >= 0 && slot < UI_MAX_SUBSCRIPTS && ui->subScripts[slot],
		"AbortSubScript() argument 1: invalid subscript ID");
	uiLAssert(L, ui->subScripts[slot]->isRunning,
		"AbortSubScript(): subscript isn't running");
	uiSubScriptAbort(ui, slot);
	return 0;
}

static int l_IsSubScriptRunning(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	int n = lua_gettop(L);
	uiLAssert(L, n >= 1, "Usage: IsSubScriptRunning(ssID)");
	uiLAssert(L, lua_islightuserdata(L, 1),
		"IsSubScriptRunning() argument 1: expected subscript ID, got %s", luaL_typename(L, 1));
	int slot = (int)(uintptr_t)lua_touserdata(L, 1);
	uiLAssert(L, slot >= 0 && slot < UI_MAX_SUBSCRIPTS && ui->subScripts[slot],
		"IsSubScriptRunning() argument 1: invalid subscript ID");
	lua_pushboolean(L, ui->subScripts[slot]->isRunning);
	return 1;
}

/* ======== Module Loading ======== */

static int l_LoadModule(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	int n = lua_gettop(L);
	uiLAssert(L, n >= 1, "Usage: LoadModule(name[, ...])");
	uiLAssert(L, lua_isstring(L, 1),
		"LoadModule() argument 1: expected string, got %s", luaL_typename(L, 1));
	const char *modName = lua_tostring(L, 1);

	char fileName[UI_PATH_MAX];
	strncpy(fileName, modName, sizeof(fileName) - 1);
	fileName[sizeof(fileName) - 1] = '\0';
	if (!strchr(fileName, '.'))
		strcat(fileName, ".lua");

	char fullPath[UI_PATH_MAX];
	snprintf(fullPath, sizeof(fullPath), "%s/%s", ui->scriptPath, fileName);

	sysSetWorkDir(ui->scriptPath);
	int err = luaL_loadfile(L, fullPath);
	sysSetWorkDir(ui->scriptWorkDir);
	if (err) {
		conPrintf("LoadModule: failed to load '%s' (%d): %s\n", fullPath, err, lua_tostring(L, -1));
	}
	uiLAssert(L, err == 0, "LoadModule() error loading '%s' (%d):\n%s",
		fullPath, err, lua_tostring(L, -1));

	lua_replace(L, 1);
	lua_call(L, n - 1, LUA_MULTRET);
	return lua_gettop(L);
}

static int l_PLoadModule(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	int n = lua_gettop(L);
	uiLAssert(L, n >= 1, "Usage: PLoadModule(name[, ...])");
	uiLAssert(L, lua_isstring(L, 1),
		"PLoadModule() argument 1: expected string, got %s", luaL_typename(L, 1));
	const char *modName = lua_tostring(L, 1);

	char fileName[UI_PATH_MAX];
	strncpy(fileName, modName, sizeof(fileName) - 1);
	fileName[sizeof(fileName) - 1] = '\0';
	if (!strchr(fileName, '.'))
		strcat(fileName, ".lua");

	char fullPath[UI_PATH_MAX];
	snprintf(fullPath, sizeof(fullPath), "%s/%s", ui->scriptPath, fileName);

	sysSetWorkDir(ui->scriptPath);
	int err = luaL_loadfile(L, fullPath);
	sysSetWorkDir(ui->scriptWorkDir);

	if (err) {
		conPrintf("PLoadModule: failed to load '%s': %s\n", fullPath, lua_tostring(L, -1));
		return 1;
	}

	lua_replace(L, 1);
	lua_getfield(L, LUA_REGISTRYINDEX, "traceback");
	lua_insert(L, 1);
	err = lua_pcall(L, n - 1, LUA_MULTRET, 1);
	if (err) {
		conPrintf("PLoadModule: error executing '%s': %s\n", fullPath, lua_tostring(L, -1));
		return 1;
	}

	conPrintf("PLoadModule: '%s' loaded successfully (%d return values)\n", fullPath, lua_gettop(L));
	lua_pushnil(L);
	lua_replace(L, 1);
	return lua_gettop(L);
}

static int l_PCall(lua_State *L)
{
	int n = lua_gettop(L);
	uiLAssert(L, n >= 1, "Usage: PCall(func[, ...])");
	uiLAssert(L, lua_isfunction(L, 1),
		"PCall() argument 1: expected function, got %s", luaL_typename(L, 1));

	lua_getfield(L, LUA_REGISTRYINDEX, "traceback");
	lua_insert(L, 1);
	int err = lua_pcall(L, n - 1, LUA_MULTRET, 1);
	if (err) {
		/* Error: error message is on the stack, return it */
		return 1;
	}
	/* Success: return nil followed by function results */
	lua_pushnil(L);
	lua_replace(L, 1);
	return lua_gettop(L);
}

/* ======== Console Functions ======== */

static int l_ConPrintf(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	int n = lua_gettop(L);
	uiLAssert(L, n >= 1, "Usage: ConPrintf(fmt[, ...])");
	uiLAssert(L, lua_isstring(L, 1),
		"ConPrintf() argument 1: expected string, got %s", luaL_typename(L, 1));

	lua_pushvalue(L, lua_upvalueindex(1));
	lua_insert(L, 1);
	lua_call(L, n, 1);
	uiLAssert(L, lua_isstring(L, 1), "ConPrintf() error: string.format returned non-string");
	conPrintf("%s\n", lua_tostring(L, 1));
	return 0;
}

static void printTableIter(lua_State *L, int index, int level, int recurse)
{
	lua_checkstack(L, 5);
	lua_pushnil(L);
	while (lua_next(L, index)) {
		for (int t = 0; t < level; t++) conPrintf("  ");
		/* Print key */
		if (lua_type(L, -2) == LUA_TSTRING) {
			conPrintf("[\"%s\"] = ", lua_tostring(L, -2));
		} else {
			lua_getglobal(L, "tostring");
			lua_pushvalue(L, -3);
			lua_call(L, 1, 1);
			conPrintf("%s = ", lua_tostring(L, -1));
			lua_pop(L, 1);
		}
		/* Print value */
		if (lua_type(L, -1) == LUA_TTABLE) {
			int expand = recurse;
			if (expand) {
				lua_pushvalue(L, -1);
				lua_gettable(L, 3);
				expand = !lua_toboolean(L, -1);
				lua_pop(L, 1);
			}
			if (expand) {
				lua_pushvalue(L, -1);
				lua_pushboolean(L, 1);
				lua_settable(L, 3);
				conPrintf("table: %p {\n", lua_topointer(L, -1));
				printTableIter(L, lua_gettop(L), level + 1, 1);
				for (int t = 0; t < level; t++) conPrintf("  ");
				conPrintf("}\n");
			} else {
				conPrintf("table: %p { ... }\n", lua_topointer(L, -1));
			}
		} else if (lua_type(L, -1) == LUA_TSTRING) {
			conPrintf("\"%s\"\n", lua_tostring(L, -1));
		} else {
			lua_getglobal(L, "tostring");
			lua_pushvalue(L, -2);
			lua_call(L, 1, 1);
			conPrintf("%s\n", lua_tostring(L, -1));
			lua_pop(L, 1);
		}
		lua_pop(L, 1);
	}
}

static int l_ConPrintTable(lua_State *L)
{
	int n = lua_gettop(L);
	uiLAssert(L, n >= 1, "Usage: ConPrintTable(tbl[, noRecurse])");
	uiLAssert(L, lua_istable(L, 1),
		"ConPrintTable() argument 1: expected table, got %s", luaL_typename(L, 1));
	int recurse = !lua_toboolean(L, 2);
	lua_settop(L, 1);
	lua_newtable(L); /* Printed tables list */
	lua_pushvalue(L, 1);
	lua_pushboolean(L, 1);
	lua_settable(L, 2);
	printTableIter(L, 1, 0, recurse);
	return 0;
}

static int l_ConExecute(lua_State *L)
{
	int n = lua_gettop(L);
	uiLAssert(L, n >= 1, "Usage: ConExecute(cmd)");
	uiLAssert(L, lua_isstring(L, 1),
		"ConExecute() argument 1: expected string, got %s", luaL_typename(L, 1));
	conCmd_Buffer(lua_tostring(L, 1));
	conExecCommands();
	return 0;
}

static int l_ConClear(lua_State *L)
{
	(void)L;
	return 0;
}

static int l_print(lua_State *L)
{
	int n = lua_gettop(L);
	lua_getglobal(L, "tostring");
	for (int i = 1; i <= n; i++) {
		lua_pushvalue(L, -1);
		lua_pushvalue(L, i);
		lua_call(L, 1, 1);
		const char *s = lua_tostring(L, -1);
		uiLAssert(L, s != NULL, "print() error: tostring returned non-string");
		if (i > 1) conPrintf(" ");
		conPrintf("%s", s);
		lua_pop(L, 1);
	}
	conPrintf("\n");
	return 0;
}

/* ======== System Functions ======== */

static int l_SpawnProcess(lua_State *L)
{
	int n = lua_gettop(L);
	uiLAssert(L, n >= 1, "Usage: SpawnProcess(cmdName[, args])");
	uiLAssert(L, lua_isstring(L, 1),
		"SpawnProcess() argument 1: expected string, got %s", luaL_typename(L, 1));
	const char *cmd = lua_tostring(L, 1);
	const char *args = (n >= 2 && lua_isstring(L, 2)) ? lua_tostring(L, 2) : "";
	sysSpawnProcess(cmd, args);
	return 0;
}

static int l_OpenURL(lua_State *L)
{
	int n = lua_gettop(L);
	uiLAssert(L, n >= 1, "Usage: OpenURL(url)");
	uiLAssert(L, lua_isstring(L, 1),
		"OpenURL() argument 1: expected string, got %s", luaL_typename(L, 1));
	sysOpenURLErr(lua_tostring(L, 1));
	return 0;
}

static int l_SetProfiling(lua_State *L)
{
	int n = lua_gettop(L);
	uiLAssert(L, n >= 1, "Usage: SetProfiling(isEnabled)");
	(void)lua_toboolean(L, 1);
	return 0;
}

static int l_TakeScreenshot(lua_State *L)
{
	(void)L;
	conCmd_Buffer("screenshot");
	conExecCommands();
	return 0;
}

static int l_Restart(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	(void)L;
	ui->restartFlag = 1;
	return 0;
}

static int l_Exit(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	int n = lua_gettop(L);
	const char *msg = NULL;
	if (n >= 1 && !lua_isnil(L, 1)) {
		uiLAssert(L, lua_isstring(L, 1),
			"Exit() argument 1: expected string or nil, got %s", luaL_typename(L, 1));
		msg = lua_tostring(L, 1);
	}
	sysExit(msg);
	ui->didExit = 1;
	return 0;
}

static int l_SetForeground(lua_State *L)
{
	ui_main_t *ui = GetUIPtr(L);
	(void)L;
	if (ui->sys->video)
		sysVideoSetForeground(ui->sys->video);
	return 0;
}

/* ======== API Registration ======== */

#define ADDFUNC(n) lua_pushcclosure(L, l_##n, 0); lua_setglobal(L, #n);
#define ADDFUNCCL(n, u) lua_pushcclosure(L, l_##n, u); lua_setglobal(L, #n);

int uiInitAPI(lua_State *L)
{
	luaL_openlibs(L);

	/* Add "lua/" subdir to package.path (absolute path based on basePath) */
	{
		lua_getglobal(L, "package");
		lua_getfield(L, -1, "path");
		const char *old_path = lua_tostring(L, -1);
		lua_pop(L, 1);

		/* Get basePath from the ui_main_t stored in registry */
		char new_path[4096];
		lua_rawgeti(L, LUA_REGISTRYINDEX, UI_REGISTRY_KEY);
		ui_main_t *ui = (ui_main_t *)lua_touserdata(L, -1);
		lua_pop(L, 1);

		if (ui && ui->sys && ui->sys->basePath[0]) {
			snprintf(new_path, sizeof(new_path), "%s;%s/lua/?.lua;%s/lua/?/init.lua;lua/?.lua;lua/?/init.lua",
				old_path ? old_path : "", ui->sys->basePath, ui->sys->basePath);
		} else {
			snprintf(new_path, sizeof(new_path), "%s;lua/?.lua;lua/?/init.lua",
				old_path ? old_path : "");
		}
		lua_pushstring(L, new_path);
		lua_setfield(L, -2, "path");
		lua_pop(L, 1);
	}

	/* Callbacks table */
	lua_newtable(L);
	lua_pushvalue(L, -1);
	ADDFUNCCL(SetCallback, 1);
	lua_pushvalue(L, -1);
	ADDFUNCCL(GetCallback, 1);
	lua_pushvalue(L, -1);
	ADDFUNCCL(SetMainObject, 1);
	lua_setfield(L, LUA_REGISTRYINDEX, "uicallbacks");

	/* Image handle metatable */
	lua_newtable(L);
	lua_pushvalue(L, -1);
	ADDFUNCCL(NewImageHandle, 1);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, l_imgHandleGC);
	lua_setfield(L, -2, "__gc");
	lua_pushcfunction(L, l_imgHandleLoad);
	lua_setfield(L, -2, "Load");
	lua_pushcfunction(L, l_imgHandleUnload);
	lua_setfield(L, -2, "Unload");
	lua_pushcfunction(L, l_imgHandleIsValid);
	lua_setfield(L, -2, "IsValid");
	lua_pushcfunction(L, l_imgHandleIsLoading);
	lua_setfield(L, -2, "IsLoading");
	lua_pushcfunction(L, l_imgHandleSetLoadingPriority);
	lua_setfield(L, -2, "SetLoadingPriority");
	lua_pushcfunction(L, l_imgHandleImageSize);
	lua_setfield(L, -2, "ImageSize");
	lua_pushcfunction(L, l_imgHandleLoadArtRectangle);
	lua_setfield(L, -2, "LoadArtRectangle");
	lua_pushcfunction(L, l_imgHandleLoadArtArcBand);
	lua_setfield(L, -2, "LoadArtArcBand");
	lua_setfield(L, LUA_REGISTRYINDEX, "uiimghandlemeta");

	/* Art handle metatable */
	lua_newtable(L);
	lua_pushvalue(L, -1);
	ADDFUNCCL(NewArtHandle, 1);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, l_artHandleGC);
	lua_setfield(L, -2, "__gc");
	lua_pushcfunction(L, l_artHandleSize);
	lua_setfield(L, -2, "Size");
	lua_setfield(L, LUA_REGISTRYINDEX, "uiarthandlemeta");

	/* Rendering */
	ADDFUNC(RenderInit);
	ADDFUNC(GetScreenSize);
	ADDFUNC(GetScreenScale);
	ADDFUNC(GetVirtualScreenSize);
	ADDFUNC(SetClearColor);
	ADDFUNC(SetDrawLayer);
	ADDFUNC(GetDrawLayer);
	ADDFUNC(SetViewport);
	ADDFUNC(SetBlendMode);
	ADDFUNC(SetDrawColor);
	ADDFUNC(GetDrawColor);
	ADDFUNC(SetDPIScaleOverridePercent);
	ADDFUNC(GetDPIScaleOverridePercent);
	ADDFUNC(DrawImage);
	ADDFUNC(DrawImageQuad);
	ADDFUNC(DrawString);
	ADDFUNC(DrawStringWidth);
	ADDFUNC(DrawStringCursorIndex);
	ADDFUNC(StripEscapes);
	ADDFUNC(GetAsyncCount);

	/* Search handle metatable */
	lua_newtable(L);
	lua_pushvalue(L, -1);
	ADDFUNCCL(NewFileSearch, 1);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, l_searchHandleGC);
	lua_setfield(L, -2, "__gc");
	lua_pushcfunction(L, l_searchHandleNextFile);
	lua_setfield(L, -2, "NextFile");
	lua_pushcfunction(L, l_searchHandleGetFileName);
	lua_setfield(L, -2, "GetFileName");
	lua_pushcfunction(L, l_searchHandleGetFileSize);
	lua_setfield(L, -2, "GetFileSize");
	lua_pushcfunction(L, l_searchHandleGetFileModifiedTime);
	lua_setfield(L, -2, "GetFileModifiedTime");
	lua_setfield(L, LUA_REGISTRYINDEX, "uisearchhandlemeta");

	/* General functions */
	ADDFUNC(GetCloudProvider);
	ADDFUNC(SetWindowTitle);
	ADDFUNC(GetCursorPos);
	ADDFUNC(SetCursorPos);
	ADDFUNC(ShowCursor);
	ADDFUNC(IsKeyDown);
	ADDFUNC(Copy);
	ADDFUNC(Paste);
	ADDFUNC(Deflate);
	ADDFUNC(Inflate);
	ADDFUNC(GetTime);
	ADDFUNC(GetScriptPath);
	ADDFUNC(GetRuntimePath);
	ADDFUNC(GetUserPath);
	ADDFUNC(MakeDir);
	ADDFUNC(RemoveDir);
	ADDFUNC(SetWorkDir);
	ADDFUNC(GetWorkDir);
	ADDFUNC(LaunchSubScript);
	ADDFUNC(AbortSubScript);
	ADDFUNC(IsSubScriptRunning);
	ADDFUNC(LoadModule);
	ADDFUNC(PLoadModule);
	ADDFUNC(PCall);

	/* ConPrintf with string.format as upvalue */
	lua_getglobal(L, "string");
	lua_getfield(L, -1, "format");
	ADDFUNCCL(ConPrintf, 1);
	lua_pop(L, 1);

	ADDFUNC(ConPrintTable);
	ADDFUNC(ConExecute);
	ADDFUNC(ConClear);
	ADDFUNC(print);
	ADDFUNC(SpawnProcess);
	ADDFUNC(OpenURL);
	ADDFUNC(SetProfiling);
	ADDFUNC(TakeScreenshot);
	ADDFUNC(Restart);
	ADDFUNC(Exit);
	ADDFUNC(SetForeground);

	/* Override os.exit */
	lua_getglobal(L, "os");
	lua_pushcfunction(L, l_Exit);
	lua_setfield(L, -2, "exit");
	lua_pop(L, 1);

	return 0;
}

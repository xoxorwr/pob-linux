#include "ui_main.h"
#include "ui_subscript.h"
#include "lua_compat.h"
#include "sys_video.h"
#include "sys_opengl.h"

#include <limits.h>
#include <libgen.h>
#include <math.h>
#include <zlib.h>

/* Lua 5.1 compatibility for LuaJIT */

/* ======== Key Name Map ======== */

static struct {
	int key;
	const char *str;
} ui_keyNameMap[] = {
	{ KEY_LMOUSE,     "LEFTBUTTON" },
	{ KEY_MMOUSE,     "MIDDLEBUTTON" },
	{ KEY_RMOUSE,     "RIGHTBUTTON" },
	{ KEY_MOUSE4,     "MOUSE4" },
	{ KEY_MOUSE5,     "MOUSE5" },
	{ KEY_MWHEELUP,   "WHEELUP" },
	{ KEY_MWHEELDOWN, "WHEELDOWN" },
	{ KEY_BACK,       "BACK" },
	{ KEY_TAB,        "TAB" },
	{ KEY_RETURN,     "RETURN" },
	{ KEY_ESCAPE,     "ESCAPE" },
	{ KEY_SHIFT,      "SHIFT" },
	{ KEY_CTRL,       "CTRL" },
	{ KEY_ALT,        "ALT" },
	{ KEY_PAUSE,      "PAUSE" },
	{ KEY_PGUP,       "PAGEUP" },
	{ KEY_PGDN,       "PAGEDOWN" },
	{ KEY_END,        "END" },
	{ KEY_HOME,       "HOME" },
	{ KEY_PRINTSCRN,  "PRINTSCREEN" },
	{ KEY_INSERT,     "INSERT" },
	{ KEY_DELETE,     "DELETE" },
	{ KEY_UP,         "UP" },
	{ KEY_DOWN,       "DOWN" },
	{ KEY_LEFT,       "LEFT" },
	{ KEY_RIGHT,      "RIGHT" },
	{ KEY_F1,         "F1" },
	{ KEY_F2,         "F2" },
	{ KEY_F3,         "F3" },
	{ KEY_F4,         "F4" },
	{ KEY_F5,         "F5" },
	{ KEY_F6,         "F6" },
	{ KEY_F7,         "F7" },
	{ KEY_F8,         "F8" },
	{ KEY_F9,         "F9" },
	{ KEY_F10,        "F10" },
	{ KEY_F11,        "F11" },
	{ KEY_F12,        "F12" },
	{ KEY_F13,        "F13" },
	{ KEY_F14,        "F14" },
	{ KEY_F15,        "F15" },
	{ KEY_NUMLOCK,    "NUMLOCK" },
	{ KEY_SCROLL,     "SCROLLLOCK" },
	{ 0, NULL }
};

const char *uiNameForKey(int key)
{
	for (int i = 0; ui_keyNameMap[i].key; i++) {
		if (ui_keyNameMap[i].key == key)
			return ui_keyNameMap[i].str;
	}
	return "?";
}

int uiKeyForName(const char *keyName)
{
	if (keyName[1]) {
		for (int i = 0; ui_keyNameMap[i].key; i++) {
			if (_stricmp(keyName, ui_keyNameMap[i].str) == 0)
				return ui_keyNameMap[i].key;
		}
	} else {
		if (isalpha((unsigned char)*keyName))
			return tolower((unsigned char)*keyName);
		else if (*keyName == ' ' || isdigit((unsigned char)*keyName))
			return *keyName;
	}
	return 0;
}

/* ======== Lua Helpers ======== */

static int traceback(lua_State *L)
{
	if (!lua_isstring(L, 1))
		return 1;
	lua_getglobal(L, "debug");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		return 1;
	}
	lua_getfield(L, -1, "traceback");
	if (!lua_isfunction(L, -1)) {
		lua_pop(L, 2);
		return 1;
	}
	lua_pushvalue(L, 1);
	lua_pushinteger(L, 2);
	lua_call(L, 2, 1);
	return 1;
}

static int l_panicFunc(lua_State *L)
{
	lua_rawgeti(L, LUA_REGISTRYINDEX, UI_REGISTRY_KEY);
	ui_main_t *ui = (ui_main_t *)lua_touserdata(L, -1);
	lua_pop(L, 1);
	sysError("Unprotected Lua error:\n%s", lua_tostring(L, -1));
	return 0;
}

/* ======== Path Helpers ======== */

static void resolvePath(char *out, size_t outSize, const char *in)
{
	char resolved[PATH_MAX];
	if (realpath(in, resolved)) {
		strncpy(out, resolved, outSize - 1);
		out[outSize - 1] = '\0';
	} else {
		strncpy(out, in, outSize - 1);
		out[outSize - 1] = '\0';
	}
}

static void dirName(char *out, size_t outSize, const char *path)
{
	char tmp[PATH_MAX];
	strncpy(tmp, path, sizeof(tmp) - 1);
	tmp[sizeof(tmp) - 1] = '\0';
	char *d = dirname(tmp);
	strncpy(out, d, outSize - 1);
	out[outSize - 1] = '\0';
}

static void baseName(char *out, size_t outSize, const char *path)
{
	char tmp[PATH_MAX];
	strncpy(tmp, path, sizeof(tmp) - 1);
	tmp[sizeof(tmp) - 1] = '\0';
	char *b = basename(tmp);
	strncpy(out, b, outSize - 1);
	out[outSize - 1] = '\0';
}

/* ======== UI Init/Shutdown ======== */

void uiInit(ui_main_t *ui, sys_main_t *sys, core_main_t *core, int argc, char **argv)
{
	memset(ui, 0, sizeof(*ui));
	ui->sys = sys;
	ui->core = core;
	ui->renderer = NULL;

	/* Resolve script path from argv[0] */
	if (argv[0][0] == '/') {
		resolvePath(ui->scriptName, UI_PATH_MAX, argv[0]);
	} else {
		char full[PATH_MAX];
		snprintf(full, sizeof(full), "%s/%s", sys->basePath, argv[0]);
		resolvePath(ui->scriptName, UI_PATH_MAX, full);
	}

	/* Derive .cfg path */
	strncpy(ui->scriptCfg, ui->scriptName, UI_PATH_MAX - 1);
	ui->scriptCfg[UI_PATH_MAX - 1] = '\0';
	{
		char *dot = strrchr(ui->scriptCfg, '.');
		if (dot) strcpy(dot, ".cfg");
	}

	/* Derive script directory */
	dirName(ui->scriptPath, UI_PATH_MAX, ui->scriptName);
	strncpy(ui->scriptWorkDir, ui->scriptPath, UI_PATH_MAX - 1);
	ui->scriptWorkDir[UI_PATH_MAX - 1] = '\0';

	/* Copy args */
	ui->scriptArgc = argc;
	if (argc > 0) {
		ui->scriptArgv = (char **)calloc((size_t)argc, sizeof(char *));
		for (int i = 0; i < argc; i++) {
			ui->scriptArgv[i] = AllocString(argv[i]);
		}
	}

	/* Load config files */
	conCmd_Buffer("exec SimpleGraphic/SimpleGraphic.cfg");
	conCmd_Buffer("exec SimpleGraphic/SimpleGraphicAuto.cfg");
	conExecCommands();

	/* Initialise script */
	uiScriptInit(ui);
	while (ui->restartFlag && !ui->didExit) {
		uiScriptShutdown(ui);
		uiScriptInit(ui);
	}
}

void uiShutdown(ui_main_t *ui)
{
	uiScriptShutdown(ui);

	if (ui->renderer) {
		rShutdown(ui->renderer);
		free(ui->renderer);
		ui->renderer = NULL;
	}

	/* Save config */
	if (ui->scriptCfg[0]) {
		/* TODO: save config to file */
	}

	/* Free args */
	for (int i = 0; i < ui->scriptArgc; i++) {
		FreeString(ui->scriptArgv[i]);
	}
	free(ui->scriptArgv);
	ui->scriptArgv = NULL;
}

/* ======== Script Init/Shutdown ======== */

void uiScriptInit(ui_main_t *ui)
{
	conPrintf("UI Init\n");
	conPrintf("Script: %s\n", ui->scriptName);
	if (ui->scriptPath[0])
		conPrintf("Script working directory: %s\n", ui->scriptWorkDir);

	if (ui->sys->video)
		sysVideoSetTitle(ui->sys->video, ui->scriptName);

	ui->restartFlag = 0;
	ui->didExit = 0;
	ui->renderEnable = 0;
	ui->inLua = 0;
	ui->hasActiveCoroutine = 0;
	ui->framesSinceWindowHidden = 0;
	/* Initialise Lua */
	conPrintf("Initialising Lua...\n");
	ui->L = luaL_newstate();
	if (!ui->L) sysError("Error: unable to create Lua state.");

	lua_atpanic(ui->L, l_panicFunc);
	lua_pushlightuserdata(ui->L, ui);
	lua_seti(ui->L, LUA_REGISTRYINDEX, UI_REGISTRY_KEY);
	lua_pushcfunction(ui->L, traceback);
	lua_pushvalue(ui->L, -1);
	lua_setfield(ui->L, LUA_REGISTRYINDEX, "traceback");

	lua_pushboolean(ui->L, 1);
	lua_setfield(ui->L, LUA_REGISTRYINDEX, "LUA_NOENV");

	/* Add libraries and APIs */
	lua_gc(ui->L, LUA_GCSTOP, 0);
	lua_pushcfunction(ui->L, uiInitAPI);
	if (lua_pcall(ui->L, 0, 0, 0))
		sysError("Error initialising Lua environment:\n%s\n", lua_tostring(ui->L, -1));
	lua_gc(ui->L, LUA_GCRESTART, -1);

	/* Register lcurl.safe via package.preload */
	{
		int luaopen_lcurl_safe(lua_State *L);
		lua_getglobal(ui->L, "package");
		lua_getfield(ui->L, -1, "preload");
		lua_pushcfunction(ui->L, luaopen_lcurl_safe);
		lua_setfield(ui->L, -2, "lcurl.safe");
		lua_pop(ui->L, 2);
	}

	/* Init subscript system */
	for (int i = 0; i < UI_MAX_SUBSCRIPTS; i++)
		ui->subScripts[i] = NULL;

	/* Load the script file */
	sysSetWorkDir(ui->scriptWorkDir);
	{
		char scriptFile[UI_PATH_MAX];
		baseName(scriptFile, UI_PATH_MAX, ui->scriptName);
		if (luaL_loadfile(ui->L, scriptFile))
			uiDoError(ui, "Error loading", lua_tostring(ui->L, -1));
	}
	sysSetWorkDir(NULL);

	/* Run the script */
	conPrintf("Running script...\n");
	for (int i = 0; i < ui->scriptArgc; i++)
		lua_pushstring(ui->L, ui->scriptArgv[i]);
	lua_createtable(ui->L, ui->scriptArgc - 1, 1);
	for (int i = 0; i < ui->scriptArgc; i++) {
		lua_pushstring(ui->L, ui->scriptArgv[i]);
		lua_rawseti(ui->L, -2, i);
	}
	lua_setglobal(ui->L, "arg");
	uiPCall(ui, ui->scriptArgc, 0);

	if (!ui->didExit && !ui->restartFlag) {
		/* Run initialisation callback */
		int extraArgs = uiPushCallback(ui, "OnInit");
		if (extraArgs >= 0)
			uiPCall(ui, extraArgs, 0);
	}

	if (!ui->didExit && !ui->restartFlag) {
		/* Check for frame callback */
		int extraArgs = uiPushCallback(ui, "OnFrame");
		if (extraArgs >= 0) {
			lua_pop(ui->L, 1 + extraArgs);
		} else {
			conPrintf("\nScript didn't set frame callback, exiting...\n");
			sysExit(NULL);
		}
	}
}

void uiScriptShutdown(ui_main_t *ui)
{
	/* Run exit callback */
	int extraArgs = uiPushCallback(ui, "OnExit");
	if (extraArgs >= 0)
		uiPCall(ui, extraArgs, 0);

	/* Shutdown subscript system */
	uiSubScriptFreeAll(ui);

	/* Shutdown Lua */
	if (ui->L) {
		lua_close(ui->L);
		ui->L = NULL;
	}
}

/* ======== Frame ======== */

void uiFrame(ui_main_t *ui)
{
	int hasSubscript = 0;
	for (int i = 0; i < UI_MAX_SUBSCRIPTS; i++) {
		if (ui->subScripts[i]) {
			hasSubscript = 1;
			break;
		}
	}

	/* Always run 10 frames after finishing the boot process */
	if (!sysVideoIsActive(ui->sys->video) || ui->restartFlag || ui->didExit) {
		ui->framesSinceWindowHidden = 0;
	} else if (ui->framesSinceWindowHidden <= 10) {
		ui->framesSinceWindowHidden++;
	} else if (!sysVideoIsActive(ui->sys->video) &&
	           !sysVideoIsCursorOverWindow(ui->sys->video) &&
	           !ui->hasActiveCoroutine && !hasSubscript) {
		sysSleep(100);
		return;
	}

	if (ui->renderer) {
		rBeginFrame(ui->renderer);
		sysVideoGetRelativeCursor(ui->sys->video, &ui->cursorX, &ui->cursorY);
	}

	ui->renderEnable = 1;

	/* Run subscript system */
	for (int i = 0; i < UI_MAX_SUBSCRIPTS; i++) {
		if (ui->subScripts[i]) {
			uiSubScriptFrame(ui);
			if (!ui->subScripts[i]->isRunning) {
				free(ui->subScripts[i]);
				ui->subScripts[i] = NULL;
			}
		}
	}

	/* Run script frame callback */
	{
		int extraArgs = uiPushCallback(ui, "OnFrame");
		if (extraArgs >= 0)
			uiPCall(ui, extraArgs, 0);
	}

	ui->renderEnable = 0;

	if (ui->renderer) {
		rEndFrame(ui->renderer);
	}

	sysGLSwap(ui->sys->gl);

	if (!sysVideoIsActive(ui->sys->video) && !ui->hasActiveCoroutine && !hasSubscript)
		sysSleep(100);

	while (ui->restartFlag) {
		uiScriptShutdown(ui);
		if (ui->renderer)
			rPurgeShaders(ui->renderer);
		uiScriptInit(ui);
	}
}

/* ======== Input Handling ======== */

void uiKeyEvent(ui_main_t *ui, int key, int type)
{
	switch (type) {
	case KE_CHAR:
		uiCallKeyHandler(ui, "OnChar", key, 0);
		break;
	case KE_KEYDOWN:
	case KE_DBLCLK:
		uiCallKeyHandler(ui, "OnKeyDown", key, type == KE_DBLCLK);
		break;
	case KE_KEYUP:
		uiCallKeyHandler(ui, "OnKeyUp", key, 0);
		break;
	}
}

void uiCallKeyHandler(ui_main_t *ui, const char *hname, int key, int dblclk)
{
	if (!ui->L) return;
	int extraArgs = uiPushCallback(ui, hname);
	if (extraArgs < 0)
		return;
	if (key < 128) {
		lua_pushfstring(ui->L, "%c", key);
	} else {
		lua_pushstring(ui->L, uiNameForKey(key));
	}
	lua_pushboolean(ui->L, dblclk);
	uiPCall(ui, 2 + extraArgs, 0);
}

/* ======== CanExit ======== */

int uiCanExit(ui_main_t *ui)
{
	int ret = 1;
	int extraArgs = uiPushCallback(ui, "CanExit");
	if (extraArgs >= 0) {
		uiPCall(ui, extraArgs, 1);
		ret = !!lua_toboolean(ui->L, -1);
		lua_pop(ui->L, 1);
	}
	return ret;
}

/* ======== Callback Push ======== */

int uiPushCallback(ui_main_t *ui, const char *name)
{
	lua_State *L = ui->L;

	/* 1) Check uicallbacks table */
	lua_getfield(L, LUA_REGISTRYINDEX, "uicallbacks");
	lua_getfield(L, -1, name);
	if (lua_isfunction(L, -1)) {
		lua_remove(L, -2);
		return 0;
	}
	lua_pop(L, 2);

	/* 2) Check MainObject table */
	lua_getfield(L, LUA_REGISTRYINDEX, "uicallbacks");
	lua_getfield(L, -1, "MainObject");
	lua_remove(L, -2);
	if (lua_istable(L, -1)) {
		lua_getfield(L, -1, name);
		if (lua_isfunction(L, -1)) {
			lua_insert(L, -2);
			return 1;
		}
		lua_pop(L, 2);
	} else {
		lua_pop(L, 1);
	}

	/* 3) Fallback: check globals */
	lua_getglobal(L, name);
	if (lua_isfunction(L, -1))
		return 0;

	lua_pop(L, 1);
	return -1;
}

/* ======== Protected Call ======== */

void uiPCall(ui_main_t *ui, int narg, int nret)
{
	lua_State *L = ui->L;

	sysSetWorkDir(ui->scriptWorkDir);
	ui->inLua = 1;
	ui->hasActiveCoroutine = 0;

	lua_getfield(L, LUA_REGISTRYINDEX, "traceback");
	lua_insert(L, -(narg + 2));

	int err = lua_pcall(L, narg, nret, -(narg + 2));

	/* Save error message before clearing stack */
	const char *errMsg = NULL;
	if (err)
		errMsg = lua_tostring(L, -1);

	/* Check for active coroutines */
	lua_getglobal(L, "coroutine");
	if (lua_istable(L, -1)) {
		lua_getfield(L, -1, "list");
		if (lua_istable(L, -1)) {
			lua_pushnil(L);
			while (lua_next(L, -2)) {
				lua_State *co = lua_tothread(L, -2);
				if (co && lua_status(co) == LUA_YIELD)
					ui->hasActiveCoroutine = 1;
				lua_pop(L, 1);
			}
		}
	}
	lua_settop(L, nret > 0 ? -(nret + 1) : 0);

	ui->inLua = 0;
	sysSetWorkDir(NULL);

	if (err && !ui->didExit)
		uiDoError(ui, "Runtime error in", errMsg ? errMsg : "(unknown error)");
}

/* ======== Error Handling ======== */

void uiDoError(ui_main_t *ui, const char *msg, const char *error)
{
	char errText[4096];
	snprintf(errText, sizeof(errText), "--- SCRIPT ERROR ---\n%s '%s':\n%s\n",
		msg, ui->scriptName, error);
	sysExit(errText);
	ui->didExit = 1;
}

#ifndef SG_UI_MAIN_H
#define SG_UI_MAIN_H

#include "common.h"
#include "config.h"
#include "console.h"
#include "r_main.h"
#include "sys_main.h"

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

/* ======== Forward Declarations ======== */

typedef struct ui_main_s ui_main_t;
typedef struct ui_subscript_s ui_subscript_t;

/* ======== Constants ======== */

#define UI_REGISTRY_KEY   42
#define UI_MAX_SUBSCRIPTS 16
#define UI_PATH_MAX       1024

/* ======== UI Main Struct ======== */

struct ui_main_s {
	lua_State      *L;
	sys_main_t     *sys;
	core_main_t    *core;
	r_renderer_t   *renderer;

	char  scriptName[UI_PATH_MAX];
	char  scriptCfg[UI_PATH_MAX];
	char  scriptPath[UI_PATH_MAX];
	char  scriptWorkDir[UI_PATH_MAX];
	int   scriptArgc;
	char **scriptArgv;

	int   callbacksRef;
	int   mainObjectRef;

	int   renderEnable;
	int   inLua;
	int   exitFlag;
	int   restartFlag;
	int   didExit;
	int   hasActiveCoroutine;
	int   framesSinceWindowHidden;

	float lastColor[4];
	int   cursorX;
	int   cursorY;
	int   gcCounter;

	ui_subscript_t *subScripts[UI_MAX_SUBSCRIPTS];
};

/* ======== Function Declarations ======== */

void uiInit(ui_main_t *ui, sys_main_t *sys, core_main_t *core, int argc, char **argv);
void uiShutdown(ui_main_t *ui);
void uiFrame(ui_main_t *ui);
void uiKeyEvent(ui_main_t *ui, int key, int type);
int  uiCanExit(ui_main_t *ui);

void uiScriptInit(ui_main_t *ui);
void uiScriptShutdown(ui_main_t *ui);

int  uiPushCallback(ui_main_t *ui, const char *name);
void uiPCall(ui_main_t *ui, int narg, int nret);
void uiDoError(ui_main_t *ui, const char *msg, const char *error);
void uiCallKeyHandler(ui_main_t *ui, const char *hname, int key, int dblclk);

int  uiInitAPI(lua_State *L);

const char *uiNameForKey(int key);
int  uiKeyForName(const char *keyName);

#endif

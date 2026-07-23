#include "ui_subscript.h"

#include <string.h>

/* ======== Sub-script System ======== */
/*
 * Runs sub-scripts synchronously on the main Lua state via lua_pcall.
 * No threads, no coroutines, no lua_resume — just a simple pcall.
 * Return values are stored in a registry table referenced by resultsRef.
 * Results are delivered to Lua callbacks (OnSubFinished/OnSubError) on
 * the next frame via the msgh=1 pattern (function is its own error handler,
 * matching the Windows approach — no traceback, no lua_call inside error
 * handlers).
 */

int uiSubScriptLaunch(ui_main_t *ui, const char *scriptText, const char *funcList,
                      const char *subList, int extraArgc)
{
	lua_State *L = ui->L;
	int slot = -1;

	for (int i = 0; i < UI_MAX_SUBSCRIPTS; i++) {
		if (!ui->subScripts[i]) {
			slot = i;
			break;
		}
	}
	if (slot < 0)
		return -1;

	if (luaL_loadstring(L, scriptText) != 0) {
		conWarning("LaunchSubScript: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
		return -1;
	}

	/* Push extra args for the pcall:
	 * Stack before: [sText(1), fList(2), sList(3), url(4), ..., varN(3+extraArgc), chunk(top)]
	 * Stack after:  [sText, fList, sList, url, ..., varN, chunk, url_dup, ..., varN_dup]
	 */
	for (int i = 0; i < extraArgc; i++)
		lua_pushvalue(L, 4 + i);

	ui_subscript_t *ss = (ui_subscript_t *)calloc(1, sizeof(ui_subscript_t));
	ss->id = slot;
	ss->isRunning = 1;
	ss->finished = 0;
	ss->resultsRef = LUA_REFNIL;
	ss->nresults = 0;
	ss->errorStr = NULL;
	ss->ui = ui;
	ui->subScripts[slot] = ss;

	int status = lua_pcall(L, extraArgc, LUA_MULTRET, 0);

	if (status == LUA_OK) {
		int top = lua_gettop(L);
		int nres = top - (3 + extraArgc);
		ss->nresults = nres;

		if (nres > 0) {
			lua_createtable(L, nres, 0);
			for (int r = 0; r < nres; r++) {
				lua_pushvalue(L, 4 + extraArgc + r);
				lua_rawseti(L, -2, r + 1);
			}
			ss->resultsRef = luaL_ref(L, LUA_REGISTRYINDEX);
		}
		lua_pop(L, nres); /* pop return values off main stack */
	} else {
		ss->errorStr = AllocString(lua_tostring(L, -1) ? lua_tostring(L, -1) : "(unknown)");
		conWarning("SubScript %d error: %s", slot, ss->errorStr);
		lua_pop(L, 1); /* pop error message */
	}
	ss->finished = 1;

	return slot;
}

void uiSubScriptAbort(ui_main_t *ui, int slot)
{
	if (slot < 0 || slot >= UI_MAX_SUBSCRIPTS) return;
	ui_subscript_t *ss = ui->subScripts[slot];
	if (!ss) return;
	ss->isRunning = 0;
	if (ss->resultsRef != LUA_REFNIL)
		luaL_unref(ui->L, LUA_REGISTRYINDEX, ss->resultsRef);
	FreeString(ss->errorStr);
	free(ss);
	ui->subScripts[slot] = NULL;
}

int uiSubScriptIsRunning(ui_main_t *ui, int slot)
{
	if (slot < 0 || slot >= UI_MAX_SUBSCRIPTS) return 0;
	ui_subscript_t *ss = ui->subScripts[slot];
	return ss && ss->isRunning;
}

void uiSubScriptFrame(ui_main_t *ui)
{
	lua_State *L = ui->L;

	for (int i = 0; i < UI_MAX_SUBSCRIPTS; i++) {
		ui_subscript_t *ss = ui->subScripts[i];
		if (!ss || !ss->isRunning || !ss->finished)
			continue;

		ss->isRunning = 0;

		/* Get MainObject callback */
		lua_getfield(L, LUA_REGISTRYINDEX, "uicallbacks");
		lua_getfield(L, -1, "MainObject");
		lua_remove(L, -2);

		if (lua_istable(L, -1)) {
			const char *cbName = ss->errorStr ? "OnSubError" : "OnSubFinished";
			lua_getfield(L, -1, cbName);

			if (lua_isfunction(L, -1)) {
				/* msgh=1: func at index 1 is its own error handler */
				lua_insert(L, 1);
				/* Stack: [func(1), main_obj(2)] */

				lua_pushlightuserdata(L, (void *)(uintptr_t)i);

				if (ss->errorStr) {
					lua_pushstring(L, ss->errorStr);
					/* [func(1), main_obj(2), id(3), err(4)] */
					lua_pcall(L, 3, 0, 1);
				} else {
					int nres = ss->nresults;
					if (ss->resultsRef != LUA_REFNIL) {
						lua_rawgeti(L, LUA_REGISTRYINDEX, ss->resultsRef);
						/* table is at lua_gettop(L); push its elements */
						int tblIdx = lua_gettop(L);
						for (int r = 0; r < nres; r++)
							lua_rawgeti(L, tblIdx, r + 1);
						lua_remove(L, tblIdx);
					}
					/* [func(1), main_obj(2), id(3), ret1(4), ...] */
					lua_pcall(L, 2 + nres, 0, 1);
				}
			}
			lua_pop(L, 1);
		}
		lua_settop(L, 0);

		if (ss->resultsRef != LUA_REFNIL)
			luaL_unref(ui->L, LUA_REGISTRYINDEX, ss->resultsRef);
		FreeString(ss->errorStr);
		free(ss);
		ui->subScripts[i] = NULL;
	}
}

void uiSubScriptFreeAll(ui_main_t *ui)
{
	for (int i = 0; i < UI_MAX_SUBSCRIPTS; i++) {
		if (ui->subScripts[i]) {
			ui_subscript_t *ss = ui->subScripts[i];
			ss->isRunning = 0;
			if (ss->resultsRef != LUA_REFNIL)
				luaL_unref(ui->L, LUA_REGISTRYINDEX, ss->resultsRef);
			FreeString(ss->errorStr);
			free(ss);
			ui->subScripts[i] = NULL;
		}
	}
}

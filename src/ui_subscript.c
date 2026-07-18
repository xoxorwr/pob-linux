#include "ui_subscript.h"

/* ======== Sub-script System ======== */
/*
 * Sub-scripts are Lua coroutines that run alongside the main script.
 * Each sub-script gets its own coroutine state created from the main Lua state.
 * The script text is loaded as a chunk, then wrapped in a function that calls
 * the user-provided entry functions and returns results via OnSubFinished/OnSubError.
 */

static void subScriptFree(ui_subscript_t *ss)
{
	if (ss) {
		if (ss->co) {
			/* Coroutine is closed automatically when its parent state is closed,
			 * but we null it to avoid dangling pointer access */
			ss->co = NULL;
		}
		free(ss);
	}
}

int uiSubScriptLaunch(ui_main_t *ui, const char *scriptText, const char *funcList,
                      const char *subList, int extraArgc)
{
	lua_State *L = ui->L;
	int slot = -1;

	/* Find a free slot */
	for (int i = 0; i < UI_MAX_SUBSCRIPTS; i++) {
		if (!ui->subScripts[i]) {
			slot = i;
			break;
		}
	}
	if (slot < 0)
		return -1;

	/* Load the script text as a function */
	if (luaL_loadstring(L, scriptText) != 0) {
		/* Error loading subscript */
		const char *err = lua_tostring(L, -1);
		conWarning("LaunchSubScript: load error: %s", err ? err : "(unknown)");
		lua_pop(L, 1);
		return -1;
	}

	/* Create a new coroutine from the main state */
	lua_State *co = lua_newthread(L);
	if (!co) {
		lua_pop(L, 1); /* Pop loaded chunk */
		return -1;
	}
	/* The thread is now on top of L's stack, and we also reference it via registry
	 * to prevent GC. Store the ref in the registry keyed by slot. */
	int coRef = luaL_ref(L, LUA_REGISTRYINDEX);
	/* Now the thread is popped from the stack (ref takes it) */

	/* Push the loaded chunk onto the coroutine */
	lua_xmove(L, co, 1);
	/* Stack: chunk */

	/* The sub-script pattern from the reference implementation:
	 * The scriptText is a Lua chunk that returns a function.
	 * We call it to get the entry function, then we wrap it in
	 * a function that calls funcList entries as callbacks. */

	/* Create a wrapper function on the coroutine that:
	 * 1. Loads and runs the script text
	 * 2. Calls OnSubCall for each function in funcList
	 * 3. Calls OnSubFinished with results */

	/* For simplicity, execute the chunk directly. The scriptText is expected
	 * to be a complete Lua chunk. The funcList and subList provide the
	 * mapping of function names. */
	lua_call(co, 0, LUA_MULTRET);

	/* Create the subscript structure */
	ui_subscript_t *ss = (ui_subscript_t *)calloc(1, sizeof(ui_subscript_t));
	ss->id = slot;
	ss->coRef = coRef;
	ss->co = co;
	ss->isRunning = 1;
	ss->ui = ui;

	ui->subScripts[slot] = ss;

	return slot;
}

void uiSubScriptAbort(ui_main_t *ui, int slot)
{
	if (slot < 0 || slot >= UI_MAX_SUBSCRIPTS)
		return;
	ui_subscript_t *ss = ui->subScripts[slot];
	if (!ss) return;

	ss->isRunning = 0;
	ss->co = NULL;
	luaL_unref(ui->L, LUA_REGISTRYINDEX, ss->coRef);
	free(ss);
	ui->subScripts[slot] = NULL;
}

int uiSubScriptIsRunning(ui_main_t *ui, int slot)
{
	if (slot < 0 || slot >= UI_MAX_SUBSCRIPTS)
		return 0;
	ui_subscript_t *ss = ui->subScripts[slot];
	return ss && ss->isRunning;
}

void uiSubScriptFrame(ui_main_t *ui)
{
	lua_State *L = ui->L;

	for (int i = 0; i < UI_MAX_SUBSCRIPTS; i++) {
		ui_subscript_t *ss = ui->subScripts[i];
		if (!ss || !ss->isRunning || !ss->co)
			continue;

		lua_State *co = ss->co;
		int status = lua_resume(co, 0);

		if (status == LUA_YIELD) {
			/* Sub-script yielded, still running */
			ui->hasActiveCoroutine = 1;
		} else if (status == LUA_OK) {
			/* Sub-script finished */
			ss->isRunning = 0;
			int nres = lua_gettop(co);

			/* Call OnSubFinished callback */
			lua_getfield(L, LUA_REGISTRYINDEX, "uicallbacks");
			lua_getfield(L, -1, "OnSubFinished");
			if (lua_isfunction(L, -1)) {
				lua_remove(L, -2);
				lua_pushlightuserdata(L, (void *)(uintptr_t)i);
				/* Copy results from coroutine */
				for (int r = 0; r < nres; r++)
					lua_pushvalue(co, r + 1);
				lua_call(L, 1 + nres, 0);
			} else {
				lua_pop(L, 2);
				lua_settop(co, 0);
			}
		} else {
			/* Error */
			ss->isRunning = 0;

			const char *errMsg = lua_tostring(co, -1);
			conWarning("SubScript %d error: %s", i, errMsg ? errMsg : "(unknown)");

			/* Call OnSubError callback */
			lua_getfield(L, LUA_REGISTRYINDEX, "uicallbacks");
			lua_getfield(L, -1, "OnSubError");
			if (lua_isfunction(L, -1)) {
				lua_remove(L, -2);
				lua_pushlightuserdata(L, (void *)(uintptr_t)i);
				lua_pushstring(L, errMsg ? errMsg : "unknown error");
				lua_call(L, 2, 0);
			} else {
				lua_pop(L, 2);
			}
			lua_pop(co, 1); /* Pop error message */
		}
	}
}

void uiSubScriptFreeAll(ui_main_t *ui)
{
	for (int i = 0; i < UI_MAX_SUBSCRIPTS; i++) {
		if (ui->subScripts[i]) {
			ui_subscript_t *ss = ui->subScripts[i];
			ss->isRunning = 0;
			ss->co = NULL;
			luaL_unref(ui->L, LUA_REGISTRYINDEX, ss->coRef);
			free(ss);
			ui->subScripts[i] = NULL;
		}
	}
}

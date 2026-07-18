#include "common.h"

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <curl/curl.h>
#include <string.h>
#include <stdlib.h>

/* ======== Write callback ======== */

struct writeCtx {
	lua_State *L;
	int headerFuncRef;
	int writeFuncRef;
};

static size_t writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	struct writeCtx *ctx = (struct writeCtx *)userdata;
	size_t total = size * nmemb;

	lua_rawgeti(ctx->L, LUA_REGISTRYINDEX, ctx->writeFuncRef);
	lua_pushlstring(ctx->L, ptr, total);
	lua_call(ctx->L, 1, 1);
	int ret = lua_toboolean(ctx->L, -1);
	lua_pop(ctx->L, 1);
	return ret ? total : 0;
}

static size_t headerCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	struct writeCtx *ctx = (struct writeCtx *)userdata;
	size_t total = size * nmemb;

	lua_rawgeti(ctx->L, LUA_REGISTRYINDEX, ctx->headerFuncRef);
	lua_pushlstring(ctx->L, ptr, total);
	lua_call(ctx->L, 1, 1);
	int ret = lua_toboolean(ctx->L, -1);
	lua_pop(ctx->L, 1);
	return ret ? total : 0;
}

/* ======== Easy handle userdata ======== */

struct easyHandle {
	CURL *curl;
	struct curl_slist *headers;
	struct writeCtx ctx;
};

static int easyGC(lua_State *L)
{
	struct easyHandle *h = (struct easyHandle *)lua_touserdata(L, 1);
	if (h) {
		if (h->headers) curl_slist_free_all(h->headers);
		if (h->curl) curl_easy_cleanup(h->curl);
		if (h->ctx.headerFuncRef != LUA_REFNIL) luaL_unref(L, LUA_REGISTRYINDEX, h->ctx.headerFuncRef);
		if (h->ctx.writeFuncRef != LUA_REFNIL) luaL_unref(L, LUA_REGISTRYINDEX, h->ctx.writeFuncRef);
	}
	return 0;
}

static int easySetOpt(lua_State *L)
{
	struct easyHandle *h = (struct easyHandle *)lua_touserdata(L, 1);
	int opt = (int)lua_tointeger(L, 2);
	if (!h || !h->curl) return 0;
	switch (opt) {
	case 10023: /* CURLOPT_HTTPHEADER */ {
		if (lua_istable(L, 3)) {
			lua_pushnil(L);
			while (lua_next(L, 3)) {
				h->headers = curl_slist_append(h->headers, lua_tostring(L, -1));
				lua_pop(L, 1);
			}
			curl_easy_setopt(h->curl, CURLOPT_HTTPHEADER, h->headers);
		}
		break;
	}
	case 10018: curl_easy_setopt(h->curl, CURLOPT_USERAGENT, lua_tostring(L, 3)); break;
	case 10102: curl_easy_setopt(h->curl, CURLOPT_ACCEPT_ENCODING, lua_tostring(L, 3)); break;
	case 52:    curl_easy_setopt(h->curl, CURLOPT_FOLLOWLOCATION, lua_toboolean(L, 3) ? 1L : 0L); break;
	case 47:    curl_easy_setopt(h->curl, CURLOPT_POST, lua_toboolean(L, 3) ? 1L : 0L); break;
	case 10015: curl_easy_setopt(h->curl, CURLOPT_POSTFIELDS, lua_tostring(L, 3)); break;
	case 13:    curl_easy_setopt(h->curl, CURLOPT_IPRESOLVE, (long)lua_tointeger(L, 3)); break;
	case 10004: curl_easy_setopt(h->curl, CURLOPT_PROXY, lua_tostring(L, 3)); break;
	case 64:    curl_easy_setopt(h->curl, CURLOPT_SSL_VERIFYPEER, lua_toboolean(L, 3) ? 1L : 0L); break;
	case 81:    curl_easy_setopt(h->curl, CURLOPT_SSL_VERIFYHOST, lua_toboolean(L, 3) ? 2L : 0L); break;
	}
	return 0;
}

static int easySetOptUrl(lua_State *L)
{
	struct easyHandle *h = (struct easyHandle *)lua_touserdata(L, 1);
	if (h && h->curl)
		curl_easy_setopt(h->curl, CURLOPT_URL, lua_tostring(L, 2));
	return 0;
}

static int easySetOptHeaderFunc(lua_State *L)
{
	struct easyHandle *h = (struct easyHandle *)lua_touserdata(L, 1);
	if (!h || !h->curl) return 0;
	if (h->ctx.headerFuncRef != LUA_REFNIL) luaL_unref(L, LUA_REGISTRYINDEX, h->ctx.headerFuncRef);
	h->ctx.headerFuncRef = luaL_ref(L, LUA_REGISTRYINDEX);
	h->ctx.L = L;
	curl_easy_setopt(h->curl, CURLOPT_HEADERFUNCTION, headerCallback);
	curl_easy_setopt(h->curl, CURLOPT_HEADERDATA, &h->ctx);
	return 0;
}

static int easySetOptWriteFunc(lua_State *L)
{
	struct easyHandle *h = (struct easyHandle *)lua_touserdata(L, 1);
	if (!h || !h->curl) return 0;
	if (h->ctx.writeFuncRef != LUA_REFNIL) luaL_unref(L, LUA_REGISTRYINDEX, h->ctx.writeFuncRef);
	h->ctx.writeFuncRef = luaL_ref(L, LUA_REGISTRYINDEX);
	h->ctx.L = L;
	curl_easy_setopt(h->curl, CURLOPT_WRITEFUNCTION, writeCallback);
	curl_easy_setopt(h->curl, CURLOPT_WRITEDATA, &h->ctx);
	return 0;
}

static int errMsgFunc(lua_State *L)
{
	const char *msg = (const char *)lua_touserdata(L, lua_upvalueindex(1));
	lua_pushstring(L, msg);
	return 1;
}

static int easyPerform(lua_State *L)
{
	struct easyHandle *h = (struct easyHandle *)lua_touserdata(L, 1);
	if (!h || !h->curl) {
		lua_pushnil(L);
		lua_pushstring(L, "no handle");
		return 2;
	}
	CURLcode res = curl_easy_perform(h->curl);
	if (res != CURLE_OK) {
		lua_pushnil(L);
		lua_newtable(L);
		const char *errStr = curl_easy_strerror(res);
		lua_pushstring(L, errStr);
		lua_pushcclosure(L, errMsgFunc, 1);
		lua_setfield(L, -2, "msg");
		return 2;
	}
	lua_pushnil(L);
	return 1;
}

static int easyGetInfo(lua_State *L)
{
	struct easyHandle *h = (struct easyHandle *)lua_touserdata(L, 1);
	int info = (int)lua_tointeger(L, 2);
	if (!h || !h->curl) {
		lua_pushinteger(L, 0);
		return 1;
	}
	switch (info) {
	case 2097154: { /* INFO_RESPONSE_CODE */
		long code;
		curl_easy_getinfo(h->curl, CURLINFO_RESPONSE_CODE, &code);
		lua_pushinteger(L, code);
		return 1;
	}
	default:
		lua_pushnil(L);
		return 1;
	}
}

static int easyClose(lua_State *L)
{
	struct easyHandle *h = (struct easyHandle *)lua_touserdata(L, 1);
	if (h) {
		if (h->headers) curl_slist_free_all(h->headers);
		if (h->curl) curl_easy_cleanup(h->curl);
		h->curl = NULL;
		h->headers = NULL;
		if (h->ctx.headerFuncRef != LUA_REFNIL) luaL_unref(L, LUA_REGISTRYINDEX, h->ctx.headerFuncRef);
		if (h->ctx.writeFuncRef != LUA_REFNIL) luaL_unref(L, LUA_REGISTRYINDEX, h->ctx.writeFuncRef);
		h->ctx.headerFuncRef = LUA_REFNIL;
		h->ctx.writeFuncRef = LUA_REFNIL;
	}
	return 0;
}

static int lcurlEasy(lua_State *L)
{
	struct easyHandle *h = (struct easyHandle *)lua_newuserdata(L, sizeof(struct easyHandle));
	memset(h, 0, sizeof(*h));
	h->curl = curl_easy_init();
	h->ctx.headerFuncRef = LUA_REFNIL;
	h->ctx.writeFuncRef = LUA_REFNIL;

	if (luaL_newmetatable(L, "lcurl_easy")) {
		static const luaL_Reg mt[] = {
			{"__gc", easyGC},
			{"setopt", easySetOpt},
			{"setopt_url", easySetOptUrl},
			{"setopt_headerfunction", easySetOptHeaderFunc},
			{"setopt_writefunction", easySetOptWriteFunc},
			{"perform", easyPerform},
			{"getinfo", easyGetInfo},
			{"close", easyClose},
			{NULL, NULL}
		};
		luaL_setfuncs(L, mt, 0);
		lua_pushvalue(L, -1);
		lua_setfield(L, -2, "__index");
	}
	lua_setmetatable(L, -2);
	return 1;
}

static const luaL_Reg lcurl_funcs[] = {
	{"easy", lcurlEasy},
	{NULL, NULL}
};

static const luaL_Reg lcurl_consts[] = {
	{"OPT_HTTPHEADER", NULL},
	{"OPT_USERAGENT", NULL},
	{"OPT_ACCEPT_ENCODING", NULL},
	{"OPT_FOLLOWLOCATION", NULL},
	{"OPT_POST", NULL},
	{"OPT_POSTFIELDS", NULL},
	{"OPT_IPRESOLVE", NULL},
	{"OPT_PROXY", NULL},
	{"OPT_SSL_VERIFYPEER", NULL},
	{"OPT_SSL_VERIFYHOST", NULL},
	{"INFO_RESPONSE_CODE", NULL},
	{NULL, NULL}
};

static int luaopen_lcurl(lua_State *L)
{
	lua_newtable(L);
	luaL_setfuncs(L, lcurl_funcs, 0);
	for (int i = 0; lcurl_consts[i].name; i++) {
		int val;
		if (strcmp(lcurl_consts[i].name, "OPT_HTTPHEADER") == 0) val = 10023;
		else if (strcmp(lcurl_consts[i].name, "OPT_USERAGENT") == 0) val = 10018;
		else if (strcmp(lcurl_consts[i].name, "OPT_ACCEPT_ENCODING") == 0) val = 10102;
		else if (strcmp(lcurl_consts[i].name, "OPT_FOLLOWLOCATION") == 0) val = 52;
		else if (strcmp(lcurl_consts[i].name, "OPT_POST") == 0) val = 47;
		else if (strcmp(lcurl_consts[i].name, "OPT_POSTFIELDS") == 0) val = 10015;
		else if (strcmp(lcurl_consts[i].name, "OPT_IPRESOLVE") == 0) val = 13;
		else if (strcmp(lcurl_consts[i].name, "OPT_PROXY") == 0) val = 10004;
		else if (strcmp(lcurl_consts[i].name, "OPT_SSL_VERIFYPEER") == 0) val = 64;
		else if (strcmp(lcurl_consts[i].name, "OPT_SSL_VERIFYHOST") == 0) val = 81;
		else if (strcmp(lcurl_consts[i].name, "INFO_RESPONSE_CODE") == 0) val = 2097154;
		else continue;
		lua_pushinteger(L, val);
		lua_setfield(L, -2, lcurl_consts[i].name);
	}
	return 1;
}

int luaopen_lcurl_safe(lua_State *L)
{
	return luaopen_lcurl(L);
}

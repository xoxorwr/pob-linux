#ifndef SG_LUA_COMPAT_H
#define SG_LUA_COMPAT_H

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

/* Lua 5.1 (LuaJIT) compatibility macros for Lua 5.3+ APIs used in the codebase */
#ifndef lua_seti
#define lua_seti(L,idx,n)  lua_rawseti(L,idx,n)
#endif

#ifndef lua_geti
#define lua_geti(L,idx,n)  lua_rawgeti(L,idx,n)
#endif

#endif

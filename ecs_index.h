#ifndef LUA_ECS_INDEX_H
#define LUA_ECS_INDEX_H

#include <lua.h>

int ecs_index_cache(lua_State *L);
int ecs_index_access(lua_State *L);
int ecs_index_make(lua_State *L);

#endif

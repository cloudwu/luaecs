#ifndef LUA_ECS_GROUP_H
#define LUA_ECS_GROUP_H

#include <lua.h>

int ecs_group_update(lua_State *L);
int ecs_group_id(lua_State *L);
int ecs_group_fetch(lua_State *L);
int ecs_group_enable(lua_State *L);

#endif

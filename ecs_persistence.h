#ifndef LUA_ECS_PERSISTENCE_H
#define LUA_ECS_PERSISTENCE_H

#include <lua.h>

int ecs_persistence_readcomponent(lua_State *L);
int ecs_persistence_writer(lua_State *L);
int ecs_persistence_reader(lua_State *L);
int ecs_persistence_resetmaxid(lua_State *L);

#endif

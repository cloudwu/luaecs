#ifndef LUA_ECS_TEMPLATE_H
#define LUA_ECS_TEMPLATE_H

#include <lua.h>

int ecs_serialize_object(lua_State *L);
int ecs_serialize_lua(lua_State *L);
int ecs_template_create(lua_State *L);
int ecs_template_extract(lua_State *L);
int ecs_template_instance_component(lua_State *L);

#endif

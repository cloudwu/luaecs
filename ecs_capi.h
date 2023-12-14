#ifndef LUA_ECS_CAPI_H
#define LUA_ECS_CAPI_H

#include <stdint.h>
#include "luaecs.h"

struct entity_world;

void *entity_fetch_(struct entity_world *w, int cid, int index, struct ecs_token *t);
void entity_clear_type_(struct entity_world *w, int cid);
void *entity_component_(struct entity_world *w, struct ecs_token t, int cid);
int entity_component_index_(struct entity_world *w, struct ecs_token t, int cid);
int entity_component_index_hint_(struct entity_world *w, struct ecs_token t, int cid, int hint);
void * entity_component_add_(struct entity_world *w, struct ecs_token t, int cid, const void *buffer);
int entity_new_(struct entity_world *w, int cid, struct ecs_token *t);
void entity_remove_(struct entity_world *w, struct ecs_token t);
void entity_enable_tag_(struct entity_world *w, struct ecs_token t, int tag_id);
void entity_disable_tag_(struct entity_world *w, int tag_id, int index);
int entity_next_tag_(struct entity_world *w, int tag_id, int index, struct ecs_token *t);
int entity_get_lua_(struct entity_world *w, int cid, int index, void *L);
int entity_count_(struct entity_world *w, int cid);
int entity_index_(struct entity_world *w, void *eid);
int entity_propagate_tag_(struct entity_world *w, int cid, int tag_id);

#endif

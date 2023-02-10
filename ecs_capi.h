#ifndef LUA_ECS_CAPI_H
#define LUA_ECS_CAPI_H

#include <stdint.h>

struct entity_world;

void *entity_iter_(struct entity_world *w, int cid, int index);
void entity_clear_type_(struct entity_world *w, int cid);
int entity_sibling_index_(struct entity_world *w, int cid, int index, int silbling_id);
void *entity_add_sibling_(struct entity_world *w, int cid, int index, int silbling_id, const void *buffer);
int entity_new_(struct entity_world *w, int cid, const void *buffer);
void entity_remove_(struct entity_world *w, int cid, int index);
void entity_enable_tag_(struct entity_world *w, int cid, int index, int tag_id);
void entity_disable_tag_(struct entity_world *w, int cid, int index, int tag_id);
int entity_get_lua_(struct entity_world *w, int cid, int index, void *wL, int world_index, void *L);
int entity_count_(struct entity_world *w, int cid);

#endif

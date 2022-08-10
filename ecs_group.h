#ifndef LUA_ECS_GROUP_H
#define LUA_ECS_GROUP_H

#include <stdint.h>
#include <lua.h>

#define ENTITY_GROUP_CACHE_BITS 13
#define ENTITY_GROUP_CACHE_SIZE (1<<13)

struct entity_group;
struct entity_world;

struct entity_group_arena {
	int n;
	int cap;
	int cache[ENTITY_GROUP_CACHE_SIZE];
	struct entity_group **g;
};

void entity_group_deinit_(struct entity_group_arena *);
size_t entity_group_memsize_(struct entity_group_arena *);
void entity_group_enable_(struct entity_world *, int tagid, int n, int groupid[]);
int entity_group_add_(struct entity_group_arena *G, int groupid, uint64_t eid);
void entity_group_id_(struct entity_group_arena *G, int groupid, lua_State *L);

#endif

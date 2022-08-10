#ifndef LUA_ECS_ENTITYID_H
#define LUA_ECS_ENTITYID_H

#include <stdint.h>
#include "ecs_entityindex.h"

#define ENTITY_ID_LOOKUP 8191

struct entity_id {
	uint32_t n;
	uint32_t cap;
	uint64_t last_id;
	uint64_t *id;
	entity_index_t lookup[ENTITY_ID_LOOKUP];
};

int entity_id_alloc(struct entity_id *e, uint64_t *eid);
size_t entity_id_memsize(struct entity_id *e);
void entity_id_deinit(struct entity_id *e);
int entity_id_find(struct entity_id *e, uint64_t eid);
int entity_id_find_last(struct entity_id *e, uint64_t eid);
int entity_id_find_guessrange(struct entity_id *e, uint64_t eid, int begin, int end);

#endif

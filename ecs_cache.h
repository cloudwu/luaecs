#ifndef LUA_ECS_CACHE_H
#define LUA_ECS_CACHE_H

struct entity_world;
struct ecs_cache;

struct ecs_cache * ecs_cache_create(struct entity_world *w, int keys[], int n);
void ecs_cache_release(struct ecs_cache *);
void* ecs_cache_fetch(struct ecs_cache *, int index, int cid);
int ecs_cache_fetch_index(struct ecs_cache *c, int index, int cid);
int ecs_cache_sync(struct ecs_cache *);

#endif

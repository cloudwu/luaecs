#include "ecs_cache.h"
#include "ecs_entityindex.h"
#include "ecs_internal.h"
#include "ecs_capi.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

struct ecs_cache {
	int mainkey;
	int keys[MAX_COMPONENT];
	int keys_n;
	int n;
	int cap;
	struct entity_world *w;
	entity_index_t * index;
};

struct ecs_cache *
ecs_cache_create(struct entity_world *w, int keys[], int n) {
	if (n <= 1)
		return NULL;
	struct ecs_cache * c = (struct ecs_cache *)malloc(sizeof(*c));
	c->mainkey = keys[0];
	c->keys_n = n - 1;
	int i;
	for (i=0;i<MAX_COMPONENT;i++) {
		c->keys[i] = -1;
	}
	for (i=1;i<n;i++) {
		int cid = keys[i];
		assert(cid >= 0 && cid < MAX_COMPONENT);
		c->keys[cid] = i-1;
	}
	c->n = 0;
	c->cap = 0;
	c->w = w;
	c->index = NULL;
	return c;
}

void
ecs_cache_release(struct ecs_cache *c) {
	if (c == NULL)
		return;
	free(c->index);
	free(c);
}

int
ecs_cache_sync(struct ecs_cache *c) {
	struct entity_world *w = c->w;
	struct component_pool *mainkey = &w->c[c->mainkey];
	entity_trim(w, c->mainkey);
	int n = mainkey->n;
	if (n > c->cap) {
		size_t sz = c->keys_n * mainkey->cap * sizeof(entity_index_t);
		free(c->index);
		c->index = (entity_index_t *)malloc(sz);
		c->cap = mainkey->cap;
		c->n = n;
	}
	return n;
}

void*
ecs_cache_fetch(struct ecs_cache *c, int index, int cid) {
	if (index >= c->n)
		return NULL;
	struct component_pool * mp = &c->w->c[c->mainkey];
	if (cid == c->mainkey) {
		return get_ptr(mp, index);
	}
	struct component_pool * cp = &c->w->c[cid];
	int offset = c->keys[cid];
	assert(offset >= 0);
	entity_index_t *hint = c->index + index * c->keys_n + offset;
	uint32_t pos = index_(*hint);
	if (pos < cp->n && ENTITY_INDEX_CMP(mp->id[index], cp->id[pos]) == 0) {
		return get_ptr(cp, pos);
	}
	int id = entity_sibling_index_hint_(c->w, c->mainkey, index, cid, pos) - 1;
	if (id < 0)
		return NULL;
	if (id != pos) {
		*hint = make_index_(id);
	}
	return get_ptr(cp, id);
}

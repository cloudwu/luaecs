#include <assert.h>
#include <string.h>
#include <lua.h>

#include "ecs_capi.h"

#include "ecs_internal.h"

static void
remove_dup(struct component_pool *c, int index) {
	int i;
	unsigned int eid = c->id[index];
	int to = index;
	for (i = index + 1; i < c->n; i++) {
		if (c->id[i] != eid) {
			eid = c->id[i];
			c->id[to] = eid;
			++to;
		}
	}
	c->n = to;
}

void *
entity_iter_(struct entity_world *w, int cid, int index) {
	struct component_pool *c = &w->c[cid];
	assert(index >= 0);
	if (index >= c->n)
		return NULL;
	if (c->stride == STRIDE_TAG) {
		// it's a tag
		unsigned int eid = c->id[index];
		if (index < c->n - 1 && eid == c->id[index + 1]) {
			remove_dup(c, index + 1);
		}
		return DUMMY_PTR;
	}
	return get_ptr(c, index);
}

void
entity_clear_type_(struct entity_world *w, int cid) {
	struct component_pool *c = &w->c[cid];
	c->n = 0;
}

int
entity_sibling_index_(struct entity_world *w, int cid, int index, int silbling_id) {
	struct component_pool *c = &w->c[cid];
	if (index < 0 || index >= c->n)
		return 0;
	unsigned int eid = c->id[index];
	c = &w->c[silbling_id];
	int result_index = ecs_lookup_component_(c, eid, c->last_lookup);
	if (result_index >= 0) {
		c->last_lookup = result_index;
		return result_index + 1;
	}
	return 0;
}

static void *
add_component_(lua_State *L, int world_index, struct entity_world *w, int cid, unsigned int eid, const void *buffer) {
	int index = ecs_add_component_id_(L, world_index, w, cid, eid);
	struct component_pool *pool = &w->c[cid];
	void *ret = get_ptr(pool, index);
	if (buffer) {
		assert(pool->stride >= 0);
		memcpy(ret, buffer, pool->stride);
	}
	return ret;
}

void *
entity_add_sibling_(struct entity_world *w, int cid, int index, int silbling_id, const void *buffer, void *L, int world_index) {
	struct component_pool *c = &w->c[cid];
	assert(index >= 0 && index < c->n);
	unsigned int eid = c->id[index];
	// todo: pcall add_component_
	return add_component_((lua_State *)L, world_index, w, silbling_id, eid, buffer);
}

int
entity_new_(struct entity_world *w, int cid, const void *buffer, void *L, int world_index) {
	unsigned int eid = ++w->max_id;
	assert(eid != 0);
	struct component_pool *c = &w->c[cid];
	assert(c->cap > 0);
	if (buffer == NULL) {
		return ecs_add_component_id_(L, world_index, w, cid, eid);
	} else {
		assert(c->stride >= 0);
		int index = ecs_add_component_id_(L, world_index, w, cid, eid);
		void *ret = get_ptr(c, index);
		memcpy(ret, buffer, c->stride);
		return index;
	}
}

void
entity_remove_(struct entity_world *w, int cid, int index, void *L, int world_index) {
	entity_enable_tag_(w, cid, index, ENTITY_REMOVED, L, world_index);
}

static void
insert_id(lua_State *L, int world_index, struct entity_world *w, int cid, unsigned int eid) {
	struct component_pool *c = &w->c[cid];
	assert(c->stride == STRIDE_TAG);
	int from = 0;
	int to = c->n;
	while (from < to) {
		int mid = (from + to) / 2;
		int aa = c->id[mid];
		if (aa == eid)
			return;
		else if (aa < eid) {
			from = mid + 1;
		} else {
			to = mid;
		}
	}
	// insert eid at [from]
	if (from < c->n - 1) {
		int i;
		// Any dup id ?
		for (i = from; i < c->n - 1; i++) {
			if (c->id[i] == c->id[i + 1]) {
				memmove(c->id + from + 1, c->id + from, sizeof(unsigned int) * (i - from));
				c->id[from] = eid;
				return;
			}
		}
	}
	// 0xffffffff max uint avoid check
	ecs_add_component_id_(L, world_index, w, cid, 0xffffffff);
	memmove(c->id + from + 1, c->id + from, sizeof(unsigned int) * (c->n - from - 1));
	c->id[from] = eid;
}

void
entity_enable_tag_(struct entity_world *w, int cid, int index, int tag_id, void *L, int world_index) {
	struct component_pool *c = &w->c[cid];
	assert(index >= 0 && index < c->n);
	unsigned int eid = c->id[index];
	insert_id((lua_State *)L, world_index, w, tag_id, eid);
}

static inline void
replace_id(struct component_pool *c, int from, int to, unsigned int eid) {
	int i;
	for (i = from; i < to; i++) {
		c->id[i] = eid;
	}
}

void
entity_disable_tag_(struct entity_world *w, int cid, int index, int tag_id) {
	struct component_pool *c = &w->c[cid];
	assert(index >= 0 && index < c->n);
	unsigned int eid = c->id[index];
	if (cid != tag_id) {
		c = &w->c[tag_id];
		index = ecs_lookup_component_(c, eid, c->last_lookup);
		if (index < 0)
			return;
		c->last_lookup = index;
	}
	assert(c->stride == STRIDE_TAG);
	int from, to;
	// find next tag. You may disable subsquent tags in iteration.
	// For example, The sequence is 1 3 5 7 9 . We are now on 5 , and disable 7 .
	// We should change 7 to 9 ( 1 3 5 9 9 ) rather than 7 to 5 ( 1 3 5 5 9 )
	//                   iterator ->   ^                                ^
	for (to = index + 1; to < c->n; to++) {
		if (c->id[to] != eid) {
			for (from = index - 1; from >= 0; from--) {
				if (c->id[from] != eid)
					break;
			}
			replace_id(c, from + 1, to, c->id[to]);
			return;
		}
	}
	for (from = index - 1; from >= 0; from--) {
		if (c->id[from] != eid)
			break;
	}
	c->n = from + 1;
}

int
entity_get_lua_(struct entity_world *w, int cid, int index, void *wL, int world_index, void *L_) {
	lua_State *L = (lua_State *)L_;
	struct component_pool *c = &w->c[cid];
	++index;
	if (c->stride != STRIDE_LUA || index <= 0 || index > c->n) {
		return LUA_TNIL;
	}
	if (lua_getiuservalue(wL, world_index, cid * 2 + 2) != LUA_TTABLE) {
		lua_pop(wL, 1);
		return LUA_TNIL;
	}
	int t = lua_rawgeti(wL, -1, index);
	if (t == LUA_TNIL) {
		lua_pop(wL, 2);
		return LUA_TNIL;
	}
	lua_xmove(wL, L, 1);
	lua_pop(wL, 1);
	return t;
}

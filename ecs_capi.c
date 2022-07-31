#include <assert.h>
#include <string.h>
#include <lua.h>

#include "ecs_capi.h"

#include "ecs_internal.h"

static void
remove_dup(struct component_pool *c, int index) {
	int i;
	entity_index_t eid = c->id[index];
	int to = index;
	for (i = index + 1; i < c->n; i++) {
		if (ENTITY_INDEX_CMP(c->id[i],eid)!=0) {
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
		entity_index_t eid = c->id[index];
		if (index < c->n - 1 && ENTITY_INDEX_CMP(eid , c->id[index + 1])==0) {
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

static inline entity_index_t
get_index_(struct entity_world *w, int cid, int index) {
	if (cid < 0) {
		return make_index_(index);
	} else {
		struct component_pool *c = &w->c[cid];
		assert(index >= 0 && index < c->n);
		return c->id[index];
	}
}

int
entity_sibling_index_(struct entity_world *w, int cid, int index, int silbling_id) {
	entity_index_t eid;
	if (cid < 0) {
		eid =  make_index_(index);
	} else {
		struct component_pool *c = &w->c[cid];
		if (index < 0 || index >= c->n)
			return 0;
		eid = c->id[index];
	}
	struct component_pool *c = &w->c[silbling_id];
	int result_index = ecs_lookup_component_(c, eid, c->last_lookup);
	if (result_index >= 0) {
		c->last_lookup = result_index;
		return result_index + 1;
	}
	return 0;
}

static void *
add_component_(struct entity_world *w, int cid, entity_index_t eid, const void *buffer) {
	int index = ecs_add_component_id_(w, cid, eid);
	struct component_pool *pool = &w->c[cid];
	void *ret = get_ptr(pool, index);
	if (buffer) {
		assert(pool->stride >= 0);
		memcpy(ret, buffer, pool->stride);
	}
	return ret;
}

void *
entity_add_sibling_(struct entity_world *w, int cid, int index, int silbling_id, const void *buffer) {
	entity_index_t eid = get_index_(w, cid, index);
	// todo: pcall add_component_
	return add_component_(w, silbling_id, eid, buffer);
}

int
entity_new_(struct entity_world *w, int cid, const void *buffer) {
	entity_index_t eid = ecs_new_entityid_(w);
	if (INVALID_ENTITY_INDEX(eid)) {
		return -1;
	}
	struct component_pool *c = &w->c[cid];
	assert(c->cap > 0);
	if (buffer == NULL) {
		return ecs_add_component_id_(w, cid, eid);
	} else {
		assert(c->stride >= 0);
		int index = ecs_add_component_id_(w, cid, eid);
		void *ret = get_ptr(c, index);
		memcpy(ret, buffer, c->stride);
		return index;
	}
}

void
entity_remove_(struct entity_world *w, int cid, int index) {
	entity_enable_tag_(w, cid, index, ENTITY_REMOVED);
}

static void
insert_id(struct entity_world *w, int cid, entity_index_t eindex) {
	struct component_pool *c = &w->c[cid];
	assert(c->stride == STRIDE_TAG);
	int from = 0;
	int to = c->n;
	const uint64_t *map = w->eid.id;
	uint64_t eid = map[index_(eindex)];
	while (from < to) {
		int mid = (from + to) / 2;
		entity_index_t aa_index = c->id[mid];
		uint64_t aa = map[index_(aa_index)];
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
			if (ENTITY_INDEX_CMP(c->id[i] , c->id[i + 1])==0) {
				memmove(c->id + from + 1, c->id + from, sizeof(entity_index_t) * (i - from));
				c->id[from] = eindex;
				return;
			}
		}
	}
	ecs_add_component_id_(w, cid, INVALID_ENTITY);
	memmove(c->id + from + 1, c->id + from, sizeof(entity_index_t) * (c->n - from - 1));
	c->id[from] = eindex;
}

void
entity_enable_tag_(struct entity_world *w, int cid, int index, int tag_id) {
	entity_index_t eid = get_index_(w, cid, index);
	insert_id(w, tag_id, eid);
}

static inline void
replace_id(struct component_pool *c, int from, int to, entity_index_t eid) {
	int i;
	for (i = from; i < to; i++) {
		c->id[i] = eid;
	}
}

void
entity_disable_tag_(struct entity_world *w, int cid, int index, int tag_id) {
	struct component_pool *c = &w->c[tag_id];
	entity_index_t eid = get_index_(w, cid, index);
	if (cid != tag_id) {
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
		if (ENTITY_INDEX_CMP(c->id[to] , eid)!=0) {
			for (from = index - 1; from >= 0; from--) {
				if (ENTITY_INDEX_CMP(c->id[from] , eid)!=0)
					break;
			}
			replace_id(c, from + 1, to, c->id[to]);
			return;
		}
	}
	for (from = index - 1; from >= 0; from--) {
		if (ENTITY_INDEX_CMP(c->id[from] , eid)!=0)
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
	if (lua_getiuservalue(wL, world_index, cid) != LUA_TTABLE) {
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

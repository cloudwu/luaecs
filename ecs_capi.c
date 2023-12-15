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
entity_fetch_(struct entity_world *w, int cid, int index, struct ecs_token *output) {
	if (cid < 0) {
		assert(cid == ENTITYID_TAG);
		if (index >= w->eid.n)
			return NULL;
		if (output) {
			output->id = index;
		}
		return (void *)w->eid.id[index];
	}
	struct component_pool *c = &w->c[cid];
	assert(index >= 0);
	if (index >= c->n)
		return NULL;
	if (output) {
		output->id = (int)index_(c->id[index]);
	}
	return get_ptr(c, index);
}

int
entity_next_tag_(struct entity_world *w, int tag_id, int index, struct ecs_token *t) {
	++index;
	if (tag_id < 0) {
		if (index >= w->eid.n)
			return -1;
		t->id = index;
		return index;
	}
	struct component_pool *c = &w->c[tag_id];
	if (index >= c->n)
		return -1;

	int current_id = index_(c->id[index]);

	if (index == 0 || c->stride != STRIDE_TAG) {
		t->id = current_id;
		return index;
	}

	int last_id = t->id;
	if (current_id == last_id) {
		if (index_(c->id[index-1]) == last_id) {
			remove_dup(c, index);
			if (index >= c->n)
				return -1;
			t->id = index_(c->id[index]);
			return index;
		}
	}
	int i;
	if (current_id <= last_id) {
		for (i=index;i<c->n;i++) {
			int id = index_(c->id[i]);
			if (id > last_id) {
				t->id = id;
				return i;
			}
		}
		return -1;
	}
	// current_id > last_id
	int current_pos = index;
	for (i=index-1;i>=0;i--) {
		int id = index_(c->id[i]);
		if (id > last_id) {
			current_id = id;
			current_pos = i;
		} else {
			break;
		}
	}
	t->id = current_id;
	return current_pos;
}

int
entity_count_(struct entity_world *w, int cid) {
	if (cid < 0)
		return w->eid.n;
	struct component_pool *c = &w->c[cid];
	return c->n;
}

void
entity_clear_type_(struct entity_world *w, int cid) {
	struct component_pool *c = &w->c[cid];
	if (c->stride == STRIDE_LUA && c->n > 0) {
		lua_State *L = w->lua.L;
		unsigned int * lua_index = (unsigned int *)c->buffer;
		int next = w->lua.freelist;
		int i;
		for (i=0;i<c->n;i++) {
			lua_pushinteger(L, next);
			next = lua_index[i];
			lua_rawseti(L, 1, next);
		}
		w->lua.freelist = next;
	}
	c->n = 0;
}

int
entity_component_index_(struct entity_world *w, struct ecs_token t, int cid) {
	if (cid < 0) {
		assert(t.id < w->eid.n);
		return t.id;
	}
	entity_index_t eid = make_index_(t.id);
	struct component_pool *c = &w->c[cid];
	int result_index = ecs_lookup_component_(c, eid, c->last_lookup);
	if (result_index >= 0) {
		c->last_lookup = result_index + 1;
		return result_index;
	}
	return -1;
}

void *
entity_component_(struct entity_world *w, struct ecs_token t, int cid) {
	int id = entity_component_index_(w, t, cid);
	if (cid < 0) {
		return (void *)w->eid.id[id];
	}
	if (id >=0) {
		struct component_pool * cp = &w->c[cid];
		return get_ptr(cp, id);
	} else {
		return NULL;
	}
}

int
entity_component_index_hint_(struct entity_world *w, struct ecs_token t, int cid, int hint) {
	entity_index_t eid = make_index_(t.id);
	struct component_pool *c = &w->c[cid];
	int result_index = ecs_lookup_component_(c, eid, hint);
	if (result_index >= 0) {
		return result_index;
	}
	return -1;
}

static void *
add_component_(struct entity_world *w, int cid, entity_index_t eid, const void *buffer) {
	int index = ecs_add_component_id_(w, cid, eid);
	if (index < 0)
		return NULL;
	struct component_pool *pool = &w->c[cid];
	void *ret = get_ptr(pool, index);
	if (buffer) {
		assert(pool->stride >= 0);
		memcpy(ret, buffer, pool->stride);
	}
	return ret;
}

void *
entity_component_add_(struct entity_world *w, struct ecs_token t, int cid, const void *buffer) {
	entity_index_t eid = make_index_(t.id);
	// todo: pcall add_component_
	return add_component_(w, cid, eid, buffer);
}

int
entity_new_(struct entity_world *w, int cid, struct ecs_token *t) {
	entity_index_t eid = ecs_new_entityid_(w);
	if (INVALID_ENTITY_INDEX(eid)) {
		return -1;
	}
	struct component_pool *c = &w->c[cid];
	assert(c->cap > 0);
	int index = ecs_add_component_id_(w, cid, eid);
	if (index >= 0) {
		if (t != NULL) {
			if (cid >= 0) {
				t->id = index_(w->c[cid].id[index]);
			} else {
				t->id = index;
			}
		}
	}
	return index;
}

void
entity_remove_(struct entity_world *w, struct ecs_token t) {
	entity_enable_tag_(w, t, ENTITY_REMOVED);
}

static void
insert_id(struct entity_world *w, int cid, entity_index_t eindex) {
	struct component_pool *c = &w->c[cid];
	assert(c->stride == STRIDE_TAG);
	int from = 0;
	int to = c->n;
	const uint64_t *map = w->eid.id;
	uint64_t eid = map[index_(eindex)];
	// a common use is inserting tag continuously, so the first checkpoint is (to - 1)
	int mid = to - 1;
	while (from < to) {
		entity_index_t aa_index = c->id[mid];
		uint64_t aa = map[index_(aa_index)];
		if (aa == eid)
			return;
		else if (aa < eid) {
			from = mid + 1;
		} else {
			to = mid;
		}
		mid = (from + to) / 2;
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
	ecs_add_component_id_(w, cid, eindex);
}

void
entity_enable_tag_(struct entity_world *w, struct ecs_token t, int tag_id) {
	entity_index_t eid = make_index_(t.id);
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
entity_disable_tag_(struct entity_world *w, int tag_id, int index) {
	struct component_pool *c = &w->c[tag_id];
	assert(index >= 0 && index < c->n);
	entity_index_t eid = c->id[index];
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
entity_get_lua_(struct entity_world *w, int cid, int index, void *L_) {
	lua_State *L = (lua_State *)L_;
	struct component_pool *c = &w->c[cid];
	++index;
	if (c->stride != STRIDE_LUA || index < 0 || index >= c->n) {
		return LUA_TNIL;
	}
	lua_State *tL = w->lua.L;
	unsigned int *lua_index = (unsigned int *)c->buffer;
	int t = lua_rawgeti(tL, 1, lua_index[index]);
	lua_xmove(tL, L, 1);
	return t;
}

int
entity_index_(struct entity_world *w, void *eid_) {
	uint64_t eid = (uint64_t)eid_;
	int index = entity_id_find(&w->eid, eid);
	return index;
}

#define HASHSET 127
#define HASHSET_UNKNOWN 0
#define HASHSET_SET 1
#define HASHSET_UNSET 2

struct eid_cache {
	uint8_t flag[HASHSET];
	entity_index_t id[HASHSET];
};

static inline void
cache_init(struct eid_cache *c) {
	memset(c->flag, HASHSET_UNKNOWN, sizeof(c->flag));
}

static inline int
cache_flag(struct eid_cache *c, int idx) {
	int key = idx % HASHSET;
	uint8_t r = c->flag[key];
	if (r == HASHSET_UNKNOWN)
		return HASHSET_UNKNOWN;
	if (index_(c->id[key]) == idx)
		return r;
	return HASHSET_UNKNOWN;
}

static inline void
cache_set(struct eid_cache *c, int idx, uint8_t s) {
	int key = idx % HASHSET;
	c->flag[key] = s;
	c->id[key] = make_index_(idx);
}

static int
find_first(entity_index_t *id, int n, entity_index_t v) {
	int begin = 0;
	int end = n;
	while (begin < end) {
		int mid = (begin + end) / 2;
		int c = ENTITY_INDEX_CMP(id[mid], v);
		if (c == 0)
			return mid;
		if (c < 0)
			begin = mid + 1;
		else
			end = mid;
	}
	return begin;
}

int
entity_propagate_tag_(struct entity_world *w, int cid, int tag_id) {
	if (cid < 0 || cid >= MAX_COMPONENT) {
		return -1;
	}
	struct component_pool *c = &w->c[cid];
	if (c->cap == 0 || c->stride < sizeof(uint64_t)) {
		// Should be a C Componet with an eid (uint64)
		return -1;
	}
	struct component_pool *tag = &w->c[tag_id];
	if (tag_id < 0 || tag_id >= MAX_COMPONENT || tag->stride != STRIDE_TAG) {
		return -1;
	}
	if (tag->n == 0) {
		// no tags
		return 0;
	}
	ecs_reserve_component_(tag, tag_id, tag->n + c->n);
	entity_index_t *root = &tag->id[c->n];
	int root_n = tag->n;
	memmove(root, &tag->id[0], tag->n * sizeof(tag->id[0]));
	tag->n = 0;
#define APPEND_EID(v) tag->id[tag->n++] = (v)
	int i;
	struct eid_cache cache;
	cache_init(&cache);
	int start = find_first(c->id, c->n, *root);
	char * parent = (char *)c->buffer + start * c->stride;
	for (i=start;i<c->n;i++) {
		if (root_n > 0 && ENTITY_INDEX_CMP(c->id[i], *root) >= 0) {
			APPEND_EID(*root);
			++root;
			--root_n;
		} else {
			uint64_t eid = *(uint64_t *)parent;
			if (eid > 0) {
				int index = entity_id_find(&w->eid, eid);
				if (index >= 0) {
					switch (cache_flag(&cache, index)) {
					case HASHSET_SET:
						APPEND_EID(c->id[i]);
						break;
					case HASHSET_UNKNOWN:
						if (ecs_lookup_component_(tag, make_index_(index), -1) >= 0) {
							APPEND_EID(c->id[i]);
							cache_set(&cache, index, HASHSET_SET);
						} else {
							cache_set(&cache, index, HASHSET_UNSET);
						}
						break;
					case HASHSET_UNSET:
					default:
						break;
					}
				}
			}
		}
		parent += c->stride;
	}
	for (i=0;i<root_n;i++) {
		APPEND_EID(*root);
		++root;
	}
	return 0;
}

#define LUA_LIB

#include <lua.h>
#include <lauxlib.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "luaecs.h"
#include "ecs_group.h"
#include "ecs_internal.h"
#include "ecs_persistence.h"
#include "ecs_template.h"
#include "ecs_capi.h"
#include "ecs_cache.h"

static unsigned int
new_lua_component_id(lua_State *L, struct entity_world *w) {
	unsigned int r = w->lua.freelist;
	if (r != 0) {
		lua_State *tL = w->lua.L;
		if (lua_rawgeti(tL, 1, r) != LUA_TNUMBER) {
			lua_pop(tL, 1);
			return luaL_error(L, "Invalid lua component table[%d]", r);
		}
		unsigned int next = (unsigned int)lua_tointeger(L, -1);
		lua_pop(tL, 1);
		w->lua.freelist = next;
		return r;
	} else {
		unsigned int n = ++w->lua.cap;
		return n;
	}
}

static inline int
mainkey_istag(struct group_iter *iter) {
	int mainkey = iter->k[0].id;
	return (mainkey >= 0 && iter->world->c[mainkey].stride == STRIDE_TAG);
}

static void
remove_lua_component(struct entity_world *w, unsigned int idx) {
	lua_State *L = w->lua.L;
	lua_pushinteger(L, w->lua.freelist);
	lua_rawseti(L, 1, idx);
	w->lua.freelist = idx;
}

static void
set_lua_component(lua_State *L, struct entity_world *w, struct component_pool *c, int index) {
	lua_State *tL = w->lua.L;
	lua_xmove(L, tL, 1);
	unsigned int *lua_index = (unsigned int *)c->buffer;
	lua_rawseti(tL, 1, lua_index[index]);
}

static void
new_lua_component(lua_State *L, struct entity_world *w, struct component_pool *c, int index) {
	unsigned int idx = new_lua_component_id(L, w);
	unsigned int *lua_index = (unsigned int *)c->buffer;
	lua_index[index] = idx;

	lua_State *tL = w->lua.L;
	lua_xmove(L, tL, 1);
	lua_rawseti(tL, 1, idx);
}

static void
get_lua_component(lua_State *L, struct entity_world *w, struct component_pool *c, int index) {
	if (index < 0) {
		lua_pushnil(L);
	} else {
		lua_State *tL = w->lua.L;
		unsigned int *lua_index = (unsigned int *)c->buffer;
		lua_rawgeti(tL, 1, lua_index[index]);
		lua_xmove(tL, L, 1);
	}
}

static void
init_component_pool(struct entity_world *w, int index, int stride, int opt_size) {
	struct component_pool *c = &w->c[index];
	c->cap = opt_size;
	c->n = 0;
	c->stride = stride;
	c->id = NULL;
	c->last_lookup = 0;
	if (stride != STRIDE_TAG) {
		c->buffer = NULL;
	} else {
		c->buffer = DUMMY_PTR;
	}
}

static void
entity_new_type(lua_State *L, int world_index, int cid, int stride, int opt_size) {
	struct entity_world *w = (struct entity_world *)lua_touserdata(L, world_index);
	if (opt_size <= 0) {
		opt_size = DEFAULT_SIZE;
	}
	if (cid < 0 || cid >= MAX_COMPONENT || w->c[cid].cap != 0) {
		luaL_error(L, "Can't new type %d", cid);
	}
	init_component_pool(w, cid, stride, opt_size);
}

static int
lnew_type(lua_State *L) {
	int cid = luaL_checkinteger(L, 2);
	int stride = luaL_checkinteger(L, 3);
	int size = luaL_optinteger(L, 4, 0);
	entity_new_type(L, 1, cid, stride, size);
	return 0;
}

static int
lcount_memory(lua_State *L) {
	struct entity_world *w = getW(L);
	// count eid
	size_t sz = sizeof(*w);
	sz += entity_id_memsize(&w->eid);
	sz += entity_group_memsize_(&w->group);
	int i;
	size_t msz = sz;
	for (i = 0; i < MAX_COMPONENT; i++) {
		struct component_pool *c = &w->c[i];
		if (c->id) {
			sz += c->cap * sizeof(entity_index_t);
			msz += c->n * sizeof(entity_index_t);
		}
		if (c->buffer != DUMMY_PTR) {
			int stride = c->stride;
			if (stride == STRIDE_LUA)
				stride = sizeof(unsigned int);
			sz += c->cap * stride;
			msz += c->n * stride;
		}
	}
	lua_pushinteger(L, sz);
	lua_pushinteger(L, msz);
	return 2;
}

static void
init_buffers(struct component_pool *pool) {
	int cap = pool->cap;
	int stride = pool->stride;
	if (stride <= 0) {
		// only id
		size_t id_sz = sizeof(entity_index_t) * cap;
		pool->id = (entity_index_t *)malloc(id_sz);
		if (stride == STRIDE_LUA) {
			stride = sizeof(unsigned int);
		} else {
			return;
		}
	}
	size_t offset = stride * cap;
	size_t sz = offset + sizeof(entity_index_t) * cap;
	pool->buffer = malloc(sz);
	pool->id = (entity_index_t *)((uint8_t *)pool->buffer + offset);
}

static void
move_buffers(struct component_pool *pool, void *buffer, entity_index_t *id) {
	memcpy(pool->id, id, pool->n * sizeof(entity_index_t));
	int stride = pool->stride;
	if (stride <= 0) {
		if (stride == STRIDE_LUA) {
			stride = sizeof(unsigned int);
		} else {
			free(id);
			return;
		}
	}
	memcpy(pool->buffer, buffer, pool->n * stride);
	free(buffer);
}

static void
free_buffers(struct component_pool *c) {
	int stride = c->stride;
	if (stride <= 0) {
		if (stride == STRIDE_LUA) {
			stride = sizeof(unsigned int);
		} else {
			free(c->id);
			c->id = NULL;
			return;
		}
	}
	free(c->buffer);
	c->buffer = NULL;
	c->id = NULL;
}

void
ecs_reserve_eid_(struct entity_world *w, int n) {
	if (w->eid.cap >= n) {
		return;
	}
	free(w->eid.id);
	w->eid.id = (uint64_t *)malloc(n * sizeof(uint64_t));
	w->eid.n = 0;
	w->eid.cap = n;
}

void
ecs_reserve_component_(struct component_pool *pool, int cid, int cap) {
	if (pool->n == 0) {
		if (cap > pool->cap) {
			free_buffers(pool);
			pool->cap = cap;
		}
		if (pool->id == NULL) {
			init_buffers(pool);
		}
	} else if (cap > pool->cap) {
		pool->cap = cap;
		void *buffer = pool->buffer;
		entity_index_t *id = pool->id;
		init_buffers(pool);
		move_buffers(pool, buffer, id);
	}
}

static void
shrink_component_pool(lua_State *L, struct component_pool *c, int cid) {
	if (c->id == NULL)
		return;
	ecs_reserve_component_(c, cid, c->n);
}

static int
lcollect_memory(lua_State *L) {
	struct entity_world *w = getW(L);
	int i;
	for (i = 0; i < MAX_COMPONENT; i++) {
		shrink_component_pool(L, &w->c[i], i);
	}
	return 0;
}

static inline int
add_component_id_(struct component_pool *pool, int cid, entity_index_t eid) {
	int cap = pool->cap;
	int index = pool->n;
	if (pool->n == 0) {
		if (pool->id == NULL) {
			init_buffers(pool);
		}
	} else if (pool->n >= pool->cap) {
		// expand pool
		pool->cap = cap * 3 / 2 + 1;
		void *buffer = pool->buffer;
		entity_index_t *id = pool->id;
		init_buffers(pool);
		move_buffers(pool, buffer, id);
	}
	int cmp;
	if (index > 0 && (cmp = ENTITY_INDEX_CMP(pool->id[index-1], eid)) >= 0) {
		do {
			if (cmp == 0) {
				if (pool->stride == STRIDE_TAG)
					return index - 1;
				return -1;
			}
			--index;
		} while (index > 0 && (cmp = ENTITY_INDEX_CMP(pool->id[index-1], eid)) >= 0);
		// move [index, pool->n) -> [index+1, pool->n]
		memmove(&pool->id[index+1], &pool->id[index], (pool->n - index) * sizeof(pool->id[0]));
		int stride = pool->stride;
		if (stride == STRIDE_LUA)
			stride = sizeof(unsigned int);
		if (stride > 0) {
			memmove((uint8_t *)pool->buffer + (index+1) * stride,
				(uint8_t *)pool->buffer + index * stride,
				(pool->n - index) * stride);
		}
	}
	pool->id[index] = eid;
	++pool->n;
	return index;
}

int
ecs_add_component_id_(struct entity_world *w, int cid, entity_index_t eid) {
	struct component_pool *pool = &w->c[cid];
	return add_component_id_(pool, cid, eid);
}

entity_index_t
ecs_new_entityid_(struct entity_world *w) {
	struct entity_id *e = &w->eid;
	uint64_t eid;
	int n = entity_id_alloc(e, &eid);
	if (n < 0)
		return INVALID_ENTITY;
	return make_index_(n);
} 

static int
lnew_entity(lua_State *L) {
	struct entity_world *w = getW(L);
	entity_index_t n = ecs_new_entityid_(w);
	if (INVALID_ENTITY_INDEX(n)) {
		return luaL_error(L, "Too many entities");
	}

	int index = index_(n);
	lua_pushinteger(L, w->eid.id[index]);
	lua_pushinteger(L, index);
	return 2;
}

static int
binary_search(entity_index_t *a, int from, int to, uint32_t v) {
	while (from < to) {
		int mid = (from + to) / 2;
		uint32_t aa = index_(a[mid]);
		if (aa == v)
			return mid;
		else if (aa < v) {
			from = mid + 1;
		} else {
			to = mid;
		}
	}
	return -1;
}

#define GUESS_RANGE 64

static inline int
search_after(struct component_pool *pool, uint32_t eid, int from_index) {
	entity_index_t *a = pool->id;
	if (from_index + GUESS_RANGE * 2 >= pool->n) {
		return binary_search(a, from_index + 1, pool->n, eid);
	}
	entity_index_t higher_index = a[from_index + GUESS_RANGE];
	uint32_t higher = index_(higher_index);
	if (eid > higher) {
		return binary_search(a, from_index + GUESS_RANGE + 1, pool->n, eid);
	}
	return binary_search(a, from_index + 1, from_index + GUESS_RANGE + 1, eid);
}

int
ecs_lookup_component_(struct component_pool *pool, entity_index_t eindex, int guess_index) {
	int n = pool->n;
	if (n == 0)
		return -1;
	uint32_t eid = index_(eindex);
	if (guess_index < 0 || guess_index >= pool->n)
		return binary_search(pool->id, 0, pool->n, eid);
	entity_index_t *a = pool->id;
	entity_index_t lower_index = a[guess_index];
	uint32_t lower = index_(lower_index);
	if (eid <= lower) {
		if (eid == lower)
			return guess_index;
		return binary_search(a, 0, guess_index, eid);
	}
	return search_after(pool, eid, guess_index);
}

static inline int
lookup_component_from(struct component_pool *pool, entity_index_t eindex, int from_index) {
	int n = pool->n;
	if (from_index >= n)
		return -1;
	uint32_t eid = index_(eindex);
	entity_index_t *a = pool->id;
	uint32_t from_id = index_(a[from_index]);
	if (eid <= from_id) {
		if (eid == from_id)
			return from_index;
		return -1;
	}
	return search_after(pool, eid, from_index);
}

static inline void
move_tag(struct component_pool *pool, int from, int to, int delta) {
	pool->id[to] = DEC_ENTITY_INDEX(pool->id[from], delta);
}

static inline void
move_item(struct component_pool *pool, int from, int to, int delta) {
	pool->id[to] = DEC_ENTITY_INDEX(pool->id[from], delta);
	if (from != to) {
		int stride = pool->stride;
		memcpy((char *)pool->buffer + to * stride, (char *)pool->buffer + from * stride, stride);
	}
}

static inline void
move_lua(struct component_pool *pool, int from, int to, int delta) {
	pool->id[to] = DEC_ENTITY_INDEX(pool->id[from], delta);
	if (from != to) {
		int stride = sizeof(unsigned int);
		memcpy((char *)pool->buffer + to * stride, (char *)pool->buffer + from * stride, stride);
	}
}

static void
remove_entityid(struct entity_world *w, struct component_pool *removed) {
	int n = removed->n;
	if (n == 0)
		return;
	int i;
	uint32_t last = index_(removed->id[0]);
	uint32_t offset = last;
	uint64_t *eid = w->eid.id;
	for (i=1;i<n;i++) {
		uint32_t next = index_(removed->id[i]);
		uint32_t t = next - last - 1;
		memmove(eid+offset, eid+last+1, t * sizeof(uint64_t));
		offset += t;
		last = next;
	}
	uint32_t t = w->eid.n - last - 1;
	memmove(eid+offset, eid+last+1, t * sizeof(uint64_t));
	w->eid.n -= removed->n;
}

// return the biggset index less than v, or [index] = v:
// c : [ 1 3 5 7 ], v = 4, from = 0, return 2 (1 3)
// c : [ 1 3 5 7 ], v = 3, from = 1, return 1 (1)
static int
less_part(struct component_pool *c, entity_index_t vidx, int from) {
	if (from >= c->n)
		return c->n;
	entity_index_t *id = c->id;
	uint32_t v = index_(vidx);
	if (v <= index_(id[from]))
		return from;
	int begin = from;
	int end = c->n;
	while (begin < end) {
		int mid = (begin + end) / 2;
		uint32_t p = index_(id[mid]);
		if (p < v)
			begin = mid + 1;
		else
			end = mid;
	}
	return begin;
}

static void
remove_all(lua_State *L, struct entity_world *w, struct component_pool *removed, int cid) {
	struct component_pool *pool = &w->c[cid];
	if (pool->n == 0)
		return;
	entity_index_t *removed_id = removed->id;
	if (ENTITY_INDEX_CMP(removed_id[0], pool->id[pool->n-1]) > 0) {
		// No action, because removed_id[0] is bigger than the biggest index in pool
		return;
	}
	if (ENTITY_INDEX_CMP(pool->id[0], removed_id[removed->n-1]) > 0) {
		// No removed components, but the id in pool should -= removed->n
		int n = removed->n;
		int i;
		for (i=0;i<pool->n;i++) {
			pool->id[i] = DEC_ENTITY_INDEX(pool->id[i], n);
		}
		return;
	}
	int removed_n = less_part(removed, pool->id[0], 0);
	int i = 0;
	if (removed_n == 0) {
		// removed[0] is less than pool[0], find the start point of pool
		i = less_part(pool, removed_id[0], 0);
	}
	int index = i;
	int delta = 0;
	unsigned int * lua_index;
	switch (pool->stride) {
	case STRIDE_LUA:
		lua_index = (unsigned int *)pool->buffer;
		while (i < pool->n && removed_n < removed->n) {
			int cmp = ENTITY_INDEX_CMP(pool->id[i], removed_id[removed_n]);
			if (cmp == 0) {
				// pool[i] should be removed
				remove_lua_component(w, lua_index[i]);
				++removed_n;
				++delta;
				++i;
			} else if (cmp < 0) {
				// pool[i] < current removed
				move_lua(pool, i, index, removed_n);
				++index;
				++i;
			} else {
				// pool[i] > current removed, find next removed
				removed_n = less_part(removed, pool->id[i], removed_n);
			}
		}
		for (;i<pool->n;i++) {
			move_lua(pool, i, index, removed_n);
			++index;
		}
		break;
	case STRIDE_TAG: {
		entity_index_t last = INVALID_ENTITY;
		while (i < pool->n && removed_n < removed->n) {
			if (ENTITY_INDEX_CMP(pool->id[i], last) == 0) {
				// remove duplicate
				++i;
			} else {
				int cmp = ENTITY_INDEX_CMP(pool->id[i], removed_id[removed_n]);
				if (cmp == 0) {
					// pool[i] should be removed
					++delta;
					++i;
				} else if (cmp < 0) {
					// pool[i] < current removed
					last = pool->id[i];
					move_tag(pool, i, index, removed_n);
					++index;
					++i;
				} else {
					// pool[i] > current removed, find next removed
					removed_n = less_part(removed, pool->id[i], removed_n);
				}
			}
		}
		for (;i<pool->n;i++) {
			move_tag(pool, i, index, removed_n);
			++index;
		}
		break; }
	default:
		while (i < pool->n && removed_n < removed->n) {
			int cmp = ENTITY_INDEX_CMP(pool->id[i], removed_id[removed_n]);
			if (cmp == 0) {
				// pool[i] should be removed
				++removed_n;
				++delta;
				++i;
			} else if (cmp < 0) {
				// pool[i] < current removed
				move_item(pool, i, index, removed_n);
				++index;
				++i;
			} else {
				// pool[i] > current removed, find next removed
				removed_n = less_part(removed, pool->id[i], removed_n);
			}
		}
		for (;i<pool->n;i++) {
			move_item(pool, i, index, removed_n);
			++index;
		}
		break;
	}
	pool->n -= delta;
}

static int
ladd_component(lua_State *L) {
	struct entity_world *w = getW(L);
	uint32_t n = luaL_checkinteger(L, 2);
	if (n >= MAX_ENTITY)
		return luaL_error(L, "Invalid entity index %u", n); 
	entity_index_t eid = make_index_(n);
	int cid = check_cid(L, w, 3);
	int	index = ecs_add_component_id_(w, cid, eid);
	if (index < 0)
		return luaL_error(L, "Component %d exist", cid);
	struct component_pool *c = &w->c[cid];
	if (c->stride == STRIDE_LUA) {
		lua_pushnil(L);
		new_lua_component(L, w, c, index);
	}
	lua_pushinteger(L, index + 1);
	return 1;
}

static int
lupdate(lua_State *L) {
	struct entity_world *w = getW(L);
	int removed_id = luaL_optinteger(L, 2, ENTITY_REMOVED);
	struct component_pool *removed = &w->c[removed_id];
	int i;
	if (removed->n > 0) {
		// mark removed
		for (i = 0; i < MAX_COMPONENT; i++) {
			if (i != removed_id)
				remove_all(L, w, removed, i);
		}
		remove_entityid(w, removed);
		removed->n = 0;
	}

	return 0;
}

static int
lclear_type(lua_State *L) {
	struct entity_world *w = getW(L);
	int cid = check_cid(L, w, 2);
	entity_clear_type_(w, cid);
	return 0;
}

static int
lcontext(lua_State *L) {
	struct entity_world *w = getW(L);
	struct ecs_context *ctx = (struct ecs_context *)lua_newuserdatauv(L, sizeof(struct ecs_context), 1);
	lua_pushvalue(L, 1);
	lua_setiuservalue(L, -2, 1);
	ctx->world = w;
	static struct ecs_capi c_api = {
		entity_fetch_,
		entity_clear_type_,
		entity_component_,
		entity_component_index_,
		entity_component_add_,
		entity_new_,
		entity_remove_,
		entity_enable_tag_,
		entity_disable_tag_,
		entity_next_tag_,
		entity_get_lua_,
		entity_group_enable_,
		entity_count_,
		entity_index_,
		ecs_cache_create,
		ecs_cache_release,
		ecs_cache_fetch,
		ecs_cache_fetch_index,
		ecs_cache_sync,
	};
	ctx->api = &c_api;
	return 1;
}

static int
lnew_world(lua_State *L) {
	size_t sz = sizeof(struct entity_world);
	struct entity_world *w = (struct entity_world *)lua_newuserdatauv(L, sz, 1);
	memset(w, 0, sz);
	w->lua.L = lua_newthread(L);
	lua_newtable(w->lua.L);	// table for all lua components
	lua_setiuservalue(L, -2, 1);
	// removed set
	int world_index = lua_gettop(L);
	entity_new_type(L, world_index, ENTITY_REMOVED, 0, 0);
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_pushvalue(L, 1);
	lua_setmetatable(L, -2);
	return 1;
}

static int
ldeinit_world(lua_State *L) {
	struct entity_world *w = lua_touserdata(L, 1);
	entity_group_deinit_(&w->group);
	entity_id_deinit(&w->eid);
	int i;
	for (i=0;i<MAX_COMPONENT;i++) {
		struct component_pool *c = &w->c[i];
		if (c->stride != STRIDE_TAG) {
			free(c->buffer);
			c->buffer = NULL;
			c->id = NULL;
		} else {
			free(c->id);
			c->id = NULL;
		}
	}
	return 0;
}

static const int
sizeof_type[TYPE_COUNT] = {
	sizeof(int),
	sizeof(float),
	sizeof(char),
	sizeof(int64_t),
	sizeof(uint32_t),	// DWORD
	sizeof(uint16_t),	// WORD
	sizeof(uint8_t),	// BYTE
	sizeof(double),
	sizeof(void *),	// USERDATA
};

static int
check_type(lua_State *L) {
	int type = lua_tointeger(L, -1);
	if (type < 0 || type >= TYPE_COUNT) {
		luaL_error(L, "Invalid field type(%d)", type);
	}
	lua_pop(L, 1);
	return type;
}

static void
get_name(lua_State *L, char * name, int index) {
	size_t sz;
	const char * s = lua_tolstring(L, index, &sz);
	if (sz >= MAX_COMPONENT_NAME)
		luaL_error(L, "string '%s' is too long", s);
	memcpy(name, s, sz+1);
}

static void
get_field(lua_State *L, int i, struct group_field *f) {
	if (lua_geti(L, -1, 1) != LUA_TNUMBER) {
		luaL_error(L, "Invalid field %d [1] type", i);
	}
	f->type = check_type(L);

	if (lua_geti(L, -1, 2) != LUA_TSTRING) {
		luaL_error(L, "Invalid field %d [2] key", i);
	}
	get_name(L, f->key, -1);
	lua_pop(L, 1);

	if (lua_geti(L, -1, 3) != LUA_TNUMBER) {
		luaL_error(L, "Invalid field %d [3] offset", i);
	}
	f->offset = lua_tointeger(L, -1);
	lua_pop(L, 1);

	lua_pop(L, 1);
}

static void
write_value(lua_State *L, struct group_field *f, char *buffer) {
	int luat = lua_type(L, -1);
	char *ptr = buffer + f->offset;
	switch (f->type) {
	case TYPE_INT:
		if (!lua_isinteger(L, -1))
			luaL_error(L, "Invalid .%s type %s (int)", f->key[0] ? f->key : "*", lua_typename(L, luat));
		*(int *)ptr = lua_tointeger(L, -1);
		break;
	case TYPE_FLOAT:
		if (luat != LUA_TNUMBER)
			luaL_error(L, "Invalid .%s type %s (float)", f->key[0] ? f->key : "*", lua_typename(L, luat));
		*(float *)ptr = lua_tonumber(L, -1);
		break;
	case TYPE_BOOL:
		if (luat != LUA_TBOOLEAN)
			luaL_error(L, "Invalid .%s type %s (bool)", f->key[0] ? f->key : "*", lua_typename(L, luat));
		*(unsigned char *)ptr = lua_toboolean(L, -1);
		break;
	case TYPE_INT64:
		if (!lua_isinteger(L, -1))
			luaL_error(L, "Invalid .%s type %s (int64)", f->key[0] ? f->key : "*", lua_typename(L, luat));
		*(int64_t *)ptr = lua_tointeger(L, -1);
		break;
	case TYPE_DWORD:
		if (!lua_isinteger(L, -1))
			luaL_error(L, "Invalid .%s type %s (uint32)", f->key[0] ? f->key : "*", lua_typename(L, luat));
		else {
			int64_t v = lua_tointeger(L, -1);
			if (v < 0 || v > 0xffffffff) {
				luaL_error(L, "Invalid DWORD %d", (int)v);
			}
			*(uint32_t *)ptr = v;
		}
		break;
	case TYPE_WORD:
		if (!lua_isinteger(L, -1))
			luaL_error(L, "Invalid .%s type %s (uint16)", f->key[0] ? f->key : "*", lua_typename(L, luat));
		else {
			int v = lua_tointeger(L, -1);
			if (v < 0 || v > 0xffff) {
				luaL_error(L, "Invalid WORD %d", v);
			}
			*(uint16_t *)ptr = v;
		}
		break;
	case TYPE_BYTE:
		if (!lua_isinteger(L, -1))
			luaL_error(L, "Invalid .%s type %s (uint8)", f->key[0] ? f->key : "*", lua_typename(L, luat));
		else {
			int v = lua_tointeger(L, -1);
			if (v < 0 || v > 255) {
				luaL_error(L, "Invalid BYTE %d", v);
			}
			*(uint8_t *)ptr = v;
		}
		break;
	case TYPE_DOUBLE:
		if (luat != LUA_TNUMBER)
			luaL_error(L, "Invalid .%s type %s (double)", f->key[0] ? f->key : "*", lua_typename(L, luat));
		*(double *)ptr = lua_tonumber(L, -1);
		break;
	case TYPE_USERDATA:
		if (luat != LUA_TLIGHTUSERDATA)
			luaL_error(L, "Invalid .%s type %s (pointer)", f->key[0] ? f->key : "*", lua_typename(L, luat));
		*(void **)ptr = lua_touserdata(L, -1);
		break;
	}
	lua_pop(L, 1);
}

static void
write_value_check(lua_State *L, struct group_field *f, const char *buffer, const char *name) {
	struct group_field tmp_f = *f;
	tmp_f.offset = 0;
	char tmp[sizeof(void *)];
	write_value(L, &tmp_f, tmp);
	if (memcmp(buffer + f->offset, tmp, sizeof_type[f->type]) != 0) {
		if (f->key[0]) {
			luaL_error(L, "[%s.%s] changes", name, f->key);
		} else {
			luaL_error(L, "[%s] changes", name);
		}
	}
}

static inline void
write_component(lua_State *L, int field_n, struct group_field *f, int index, char *buffer) {
	int i;
	for (i = 0; i < field_n; i++) {
		lua_getfield(L, index, f[i].key);
		write_value(L, &f[i], buffer);
	}
}

static inline void
write_component_check(lua_State *L, int field_n, struct group_field *f, int index, const char *buffer, const char *name) {
	int i;
	for (i = 0; i < field_n; i++) {
		lua_getfield(L, index, f[i].key);
		write_value_check(L, &f[i], buffer, name);
	}
}

static void
read_value(lua_State *L, struct group_field *f, const char *buffer) {
	const char *ptr = buffer + f->offset;
	switch (f->type) {
	case TYPE_INT:
		lua_pushinteger(L, *(const int *)ptr);
		break;
	case TYPE_FLOAT:
		lua_pushnumber(L, *(const float *)ptr);
		break;
	case TYPE_BOOL:
		lua_pushboolean(L, *ptr);
		break;
	case TYPE_INT64:
		lua_pushinteger(L, *(const int64_t *)ptr);
		break;
	case TYPE_DWORD:
		lua_pushinteger(L, *(const uint32_t *)ptr);
		break;
	case TYPE_WORD:
		lua_pushinteger(L, *(const uint16_t *)ptr);
		break;
	case TYPE_BYTE:
		lua_pushinteger(L, *(const uint8_t *)ptr);
		break;
	case TYPE_DOUBLE:
		lua_pushnumber(L, *(const double *)ptr);
		break;
	case TYPE_USERDATA:
		lua_pushlightuserdata(L, *(void **)ptr);
		break;
	default:
		// never here
		luaL_error(L, "Invalid field type %d", f->type);
		break;
	}
}

static void
read_component(lua_State *L, int field_n, struct group_field *f, int index, const char *buffer) {
	int i;
	for (i = 0; i < field_n; i++) {
		read_value(L, &f[i], buffer);
		lua_setfield(L, index, f[i].key);
	}
}

static int
get_len(lua_State *L, int index) {
	lua_len(L, index);
	if (lua_type(L, -1) != LUA_TNUMBER) {
		return luaL_error(L, "Invalid table length");
	}
	int n = lua_tointeger(L, -1);
	if (n < 0) {
		return luaL_error(L, "Invalid table length %d", n);
	}
	lua_pop(L, 1);
	return n;
}

#define COMPONENT_IN 1
#define COMPONENT_OUT 2
#define COMPONENT_OPTIONAL 4
#define COMPONENT_OBJECT 8
#define COMPONENT_EXIST 0x10
#define COMPONENT_ABSENT 0x20
#define COMPONENT_FILTER (COMPONENT_EXIST | COMPONENT_ABSENT)

static inline int
is_temporary(int attrib) {
	if (attrib & COMPONENT_FILTER)
		return 0;
	return (attrib & COMPONENT_IN) == 0 && (attrib & COMPONENT_OUT) == 0;
}

static int
get_write_component(lua_State *L, int lua_index, const char *name, struct group_field *f, struct component_pool *c) {
	switch (lua_getfield(L, lua_index, name)) {
	case LUA_TNIL:
		lua_pop(L, 1);
		// restore cache (metatable can be absent during sync)
		if (lua_getmetatable(L, lua_index)) {
			lua_getfield(L, -1, name);
			lua_setfield(L, lua_index, name);
			lua_pop(L, 1); // pop metatable
		}
		return 0;
	case LUA_TTABLE:
		return 1;
	default:
		if (c->stride == STRIDE_LUA) {
			// lua object
			return 1;
		}
		if (f->key[0] == 0) {
			// value type
			return 1;
		}
		return luaL_error(L, "Invalid iterator type %s for .%s", lua_typename(L, lua_type(L, -1)), name);
	}
}

void
ecs_write_component_object_(lua_State *L, int n, struct group_field *f, void *buffer) {
	if (f->key[0] == 0) {
		write_value(L, f, buffer);
	} else {
		write_component(L, n, f, -1, (char *)buffer);
		lua_pop(L, 1);
	}
}

static void
write_component_object_check(lua_State *L, int n, struct group_field *f, const void *buffer, const char *name) {
	if (f->key[0] == 0) {
		write_value_check(L, f, buffer, name);
	} else {
		write_component_check(L, n, f, -1, (char *)buffer, name);
		lua_pop(L, 1);
	}
}

static int
remove_tag(lua_State *L, int lua_index, const char *name) {
	int r = 0;
	switch (lua_getfield(L, lua_index, name)) {
	case LUA_TNIL:
		r = 1;
		break;
	case LUA_TBOOLEAN:
		r = !lua_toboolean(L, -1);
		break;
	default:
		return luaL_error(L, "Invalid tag type %s", lua_typename(L, lua_type(L, -1)));
	}
	lua_pop(L, 1);
	return r;
}

static int
update_iter(lua_State *L, int lua_index, struct group_iter *iter, int idx, int mainkey, int skip) {
	struct group_field *f = iter->f;
	int disable_mainkey = 0;

	int i;
	for (i = 0; i < skip; i++) {
		f += iter->k[i].field_n;
	}
	struct ecs_token token;
	if (entity_fetch_(iter->world, mainkey, idx, &token) == NULL)
		return luaL_error(L, "Invalid token");

	for (i = skip; i < iter->nkey; i++) {
		struct group_key *k = &iter->k[i];
		if (!(k->attrib & COMPONENT_FILTER)) {
			struct component_pool *c = &iter->world->c[k->id];
			if (c->stride == STRIDE_TAG) {
				// It's a tag
				if ((k->attrib & COMPONENT_OUT)) {
					switch (lua_getfield(L, lua_index, k->name)) {
					case LUA_TNIL:
						break;
					case LUA_TBOOLEAN:
						if (lua_toboolean(L, -1)) {
							entity_enable_tag_(iter->world, token, k->id);
						} else {
							if (k->id == mainkey)
								disable_mainkey = 1;
							else {
								int tag_index = entity_component_index_(iter->world, token, k->id);
								if (tag_index >= 0)
									entity_disable_tag_(iter->world, k->id, tag_index);
							}
						}
						if (!(k->attrib & COMPONENT_IN)) {
							// reset tag
							lua_pushnil(L);
							lua_setfield(L, lua_index, k->name);
						}
						break;
					default:
						luaL_error(L, ".%s is a tag , should be a boolean or nil. It's %s", k->name, lua_typename(L, lua_type(L, -1)));
					}
					lua_pop(L, 1);
				}
			} else if ((k->attrib & COMPONENT_OUT)
				&& get_write_component(L, lua_index, k->name, f, c)) {
				int index = entity_component_index_(iter->world, token, k->id);
				if (index < 0) {
					luaL_error(L, "Can't find component %s of %s", k->name, iter->k[0].name);
				}
				if (c->stride == STRIDE_LUA) {
					set_lua_component(L, iter->world, c, index);
				} else {
					void *buffer = get_ptr(c, index);
					ecs_write_component_object_(L, k->field_n, f, buffer);
				}
			} else if (is_temporary(k->attrib)
				&& get_write_component(L, lua_index, k->name, f, c)) {
				if (c->stride == STRIDE_LUA) {
					entity_index_t eid = make_index_(token.id);
					int index = ecs_add_component_id_(iter->world, k->id, eid);
					if (index < 0) {
						luaL_error(L, "component %d exist", k->id);
					}
					new_lua_component(L, iter->world, c, index);
				} else {
					void *buffer = entity_component_add_(iter->world, token, k->id, NULL);
					ecs_write_component_object_(L, k->field_n, f, buffer);
				}
			}
		}
		f += k->field_n;
	}
	return disable_mainkey;
}

static void
update_last_index(lua_State *L, int lua_index, struct group_iter *iter, int idx) {
	int mainkey = iter->k[0].id;
	if (mainkey < 0) {
		assert(mainkey == ENTITYID_TAG);
		update_iter(L, lua_index, iter, idx, mainkey, 1);
		return;
	}

	int disable_mainkey = 0;
	struct component_pool *c = &iter->world->c[mainkey];
	if (!(iter->k[0].attrib & COMPONENT_FILTER)) {
		if (c->stride == STRIDE_TAG) {
			// The mainkey is a tag, delay disable
			disable_mainkey = ((iter->k[0].attrib & COMPONENT_OUT) && remove_tag(L, lua_index, iter->k[0].name));
		} else if ((iter->k[0].attrib & COMPONENT_OUT)
			&& get_write_component(L, lua_index, iter->k[0].name, iter->f, c)) {
			struct component_pool *c = &iter->world->c[mainkey];
			if (c->n <= idx) {
				luaL_error(L, "Can't find component %s for index %d", iter->k[0].name, idx);
			}
			if (c->stride == STRIDE_LUA) {
				set_lua_component(L, iter->world, c, idx);
			} else {
				void *buffer = get_ptr(c, idx);
				ecs_write_component_object_(L, iter->k[0].field_n, iter->f, buffer);
			}
		}
	}

	update_iter(L, lua_index, iter, idx, mainkey, 1);

	if (disable_mainkey) {
		entity_disable_tag_(iter->world, mainkey, idx);
	}
}

static void
check_update(lua_State *L, int lua_index, struct group_iter *iter, int idx) {
	int mainkey = iter->k[0].id;
	int i;
	struct group_field *f = iter->f;
	struct ecs_token token;
	if (entity_fetch_(iter->world, mainkey, idx, &token) == NULL) {
		luaL_error(L, "mainkey[%d] absent", idx);
	}
	for (i = 0; i < iter->nkey; i++) {
		struct group_key *k = &iter->k[i];
		if (!(k->attrib & COMPONENT_FILTER)) {
			struct component_pool *c = &iter->world->c[k->id];
			if (c->stride > 0 && !(k->attrib & COMPONENT_OUT) && (k->attrib & COMPONENT_IN) && k->id != ENTITYID_TAG) {
				// readonly C component, check it
				if (get_write_component(L, lua_index, k->name, f, c)) {
					void *buffer = entity_component_(iter->world, token, k->id);
					if (buffer) {
						write_component_object_check(L, k->field_n, f, buffer, k->name);
					}
				}
			}
		}
		f += k->field_n;
	}
}

static void
read_component_in_field(lua_State *L, int lua_index, const char *name, int n, struct group_field *f, void *buffer) {
	if (n == 0) {
		// It's tag
		lua_pushboolean(L, buffer ? 1 : 0);
		lua_setfield(L, lua_index, name);
		return;
	}
	if (f->key[0] == 0) {
		// value type
		read_value(L, f, buffer);
		lua_setfield(L, lua_index, name);
		return;
	}
	if (lua_getfield(L, lua_index, name) != LUA_TTABLE) {
		lua_pop(L, 1);
		lua_newtable(L);
		lua_pushvalue(L, -1);
		lua_setfield(L, lua_index, name);
	}
	read_component(L, n, f, lua_gettop(L), buffer);
	lua_pop(L, 1);
}

// -1 : end ; 0 : next ; 1 : succ
static int
query_index(struct group_iter *iter, int skip, int mainkey, int *idx, int index[MAX_COMPONENT], struct ecs_token *token) {
	struct ecs_token tmp;
	if (token) {
		*idx = entity_next_tag_(iter->world, mainkey, *idx, token);
		if (*idx < 0)
			return -1;
	} else {
		++*idx;
		token = &tmp;
		if (entity_fetch_(iter->world, mainkey, *idx, token) == NULL)
			return -1;
	}
	int j;
	for (j = skip; j < iter->nkey; j++) {
		struct group_key *k = &iter->k[j];
		if (k->attrib & COMPONENT_ABSENT) {
			if (entity_component_index_(iter->world, *token, k->id) >= 0) {
				// exist. try next
				return 0;
			}
			index[j] = -1;
		} else if (!is_temporary(k->attrib)) {
			index[j] = entity_component_index_(iter->world, *token, k->id);
			if (index[j] < 0) {
				if (!(k->attrib & COMPONENT_OPTIONAL)) {
					// required. try next
					return 0;
				}
			}
		} else {
			index[j] = -1;
		}
	}
	return 1;
}

static void
read_iter(lua_State *L, int obj_index, struct group_iter *iter, int index[MAX_COMPONENT]) {
	struct group_field *f = iter->f;
	int i;
	for (i = 0; i < iter->nkey; i++) {
		struct group_key *k = &iter->k[i];
		if (k->id == ENTITYID_TAG) {
			int idx = index[i];
			uint64_t eid = iter->world->eid.id[idx];
			lua_pushinteger(L, eid);
			lua_setfield(L, obj_index, "eid");
		} else if (!(k->attrib & COMPONENT_FILTER)) {
			struct component_pool *c = &iter->world->c[k->id];
			if (c->stride == STRIDE_LUA) {
				// lua object component
				get_lua_component(L, iter->world, c, index[i]);
				lua_setfield(L, obj_index, k->name);
			} else if (k->attrib & COMPONENT_IN) {
				if (index[i] >= 0) {
					void *ptr = get_ptr(c, index[i]);
					read_component_in_field(L, obj_index, k->name, k->field_n, f, ptr);
				} else {
					lua_pushnil(L);
					lua_setfield(L, obj_index, k->name);
				}
			} else if (index[i] < 0 && !is_temporary(k->attrib)) {
				lua_pushnil(L);
				lua_setfield(L, obj_index, k->name);
			}
		}
		f += k->field_n;
	}
}

static int
lread(lua_State *L) {
	struct group_iter *iter = (struct group_iter *)lua_touserdata(L, 2);
	luaL_checktype(L, 3, LUA_TTABLE);
	int idx = get_integer(L, 3, 1, "index") - 2;
	int mainkey = get_integer(L, 3, 2, "mainkey");
	int index[MAX_COMPONENT];
	int r = query_index(iter, 0, mainkey, &idx, index, NULL);
	if (r <= 0) {
		return luaL_error(L, "Can't read pattern");
	}

	if (!iter->readonly) {
		return luaL_error(L, "Pattern is not readonly");
	}
	read_iter(L, 3, iter, index);
	return 1;
}

static inline struct group_iter *
submit_index(lua_State *L, int iter_index, int i, int check) {
	if (lua_rawgeti(L, iter_index, 3) != LUA_TUSERDATA) {
		luaL_error(L, "Invalid iterator");
	}
	struct group_iter * update_iter = lua_touserdata(L, -1);
	lua_pop(L, 1);
	if (check) {
		check_update(L, iter_index, update_iter, i);
	}
	if (!update_iter->readonly) {
		update_last_index(L, iter_index, update_iter, i);
	}
	return update_iter;
}

static int
lsubmit(lua_State *L) {
	int iter_index = 2;
	luaL_checktype(L, iter_index, LUA_TTABLE);
	if (lua_rawgeti(L, iter_index, 1) != LUA_TNUMBER) {
		return luaL_error(L, "Invalid group iterator");
	}
	int i = lua_tointeger(L, -1);
	if (i < 1)
		return luaL_error(L, "Invalid iterator index %d", i);
	lua_pop(L, 1);
	submit_index(L, iter_index, i - 1, 0);
	return 0;
}

static inline int
leach_group_(lua_State *L, int check) {
	struct group_iter *iter = lua_touserdata(L, 1);
	if (lua_rawgeti(L, 2, 1) != LUA_TNUMBER) {
		return luaL_error(L, "Invalid group iterator");
	}
	int i = lua_tointeger(L, -1);
	if (i < 0)
		return luaL_error(L, "Invalid iterator index %d", i);
	lua_pop(L, 1);

	if (lua_getiuservalue(L, 1, 1) != LUA_TUSERDATA) {
		return luaL_error(L, "Missing world object for iterator");
	}

	int index[MAX_COMPONENT];
	int mainkey = iter->k[0].id;

	if (i > 0) {
		if (submit_index(L, 2, i-1, check) != iter) {
			// iterator extended, restore it
			lua_pushvalue(L, 1);
			lua_rawseti(L, 2, 3);
		}
	}
	struct ecs_token tmp;
	struct ecs_token *token = NULL;
	int istag = mainkey_istag(iter);
	if (istag) {
		token = &tmp;
		if (i > 0) {
			if (lua_rawgeti(L, 2, 0) != LUA_TNUMBER) {
				return luaL_error(L, "Invalid group iterator, missing token");
			}
			token->id = lua_tointeger(L, -1);
			lua_pop(L, 1);
		}
	}
	int idx = i - 1;
	for (;;) {
		int ret = query_index(iter, 1, mainkey, &idx, index, token);
		if (ret < 0)
			return 0;
		if (ret > 0)
			break;
	}
	index[0] = idx;

	lua_pushinteger(L, idx+1);
	lua_rawseti(L, 2, 1);	// iterator

	if (istag) {
		lua_pushinteger(L, token->id);
		lua_rawseti(L, 2, 0);	// token
	}

	read_iter(L, 2, iter, index);

	lua_settop(L, 2);
	return 1;
}

static int
leach_group_nocheck(lua_State *L) {
	return leach_group_(L, 0);
}

static int
leach_group_check(lua_State *L) {
	return leach_group_(L, 1);
}

static int
lcount(lua_State *L) {
	struct group_iter *iter = lua_touserdata(L, 1);
	int index[MAX_COMPONENT];
	int mainkey = iter->k[0].id;
	int count = 0;
	if (iter->nkey == 1) {
		if (mainkey < 0) {
			lua_pushinteger(L, iter->world->eid.n);
			return 1;
		}
		struct component_pool *c = &iter->world->c[mainkey];
		if (c->stride != STRIDE_TAG) {
			lua_pushinteger(L, c->n);
			return 1;
		}
	}
	struct ecs_token token;
	int idx = -1;
	for (;;) {
		int ret = query_index(iter, 1, mainkey, &idx, index, &token);
		if (ret < 0)
			break;
		if (ret > 0)
			++count;
	}
	lua_pushinteger(L, count);
	return 1;
}

static void
create_key_cache(lua_State *L, struct group_key *k, struct group_field *f) {
	if (k->field_n == 0 // is tag or object?
		|| (k->attrib & COMPONENT_FILTER)) { // existence or ref
		return;
	}
	if (k->field_n == 1 && f[0].key[0] == 0) {
		// value type
		switch (f[0].type) {
		case TYPE_INT:
		case TYPE_INT64:
		case TYPE_DWORD:
		case TYPE_WORD:
		case TYPE_BYTE:
			lua_pushinteger(L, 0);
			break;
		case TYPE_FLOAT:
		case TYPE_DOUBLE:
			lua_pushnumber(L, 0);
			break;
		case TYPE_BOOL:
			lua_pushboolean(L, 0);
			break;
		case TYPE_USERDATA:
			lua_pushlightuserdata(L, NULL);
			break;
		default:
			lua_pushnil(L);
			break;
		}
	} else {
		lua_createtable(L, 0, k->field_n);
	}
	lua_setfield(L, -2, k->name);
}

static inline int
lpairs_group_(lua_State *L, int check) {
	struct group_iter *iter = lua_touserdata(L, 1);
	lua_pushcfunction(L, check ? leach_group_check : leach_group_nocheck);
	lua_pushvalue(L, 1);
	lua_createtable(L, 3, iter->nkey);
	int i;
	int opt = 0;
	struct group_field *f = iter->f;
	for (i = 0; i < iter->nkey; i++) {
		struct group_key *k = &iter->k[i];
		create_key_cache(L, k, f);
		f += k->field_n;
		if (k->attrib & COMPONENT_OPTIONAL)
			++opt;
	}
	if (opt) {
		// create backup table in metatable
		lua_createtable(L, 0, opt);
		for (i = 0; i < iter->nkey; i++) {
			struct group_key *k = &iter->k[i];
			if (k->attrib & COMPONENT_OPTIONAL) {
				lua_getfield(L, -2, k->name);
				lua_setfield(L, -2, k->name);
			}
		}
		lua_setmetatable(L, -2);
	}
	lua_pushinteger(L, 0);
	lua_rawseti(L, -2, 1);
	lua_pushinteger(L, iter->k[0].id); // mainkey
	lua_rawseti(L, -2, 2);
	lua_pushvalue(L, 1);
	lua_rawseti(L, -2, 3);	// pattern
	return 3;
}

static inline int
lpairs_group(lua_State *L) {
	return lpairs_group_(L, 0);
}

static inline int
lpairs_group_check(lua_State *L) {
	return lpairs_group_(L, 1);
}

static int
lfirst(lua_State *L) {
	struct group_iter *iter = lua_touserdata(L, 2);
	int mainkey = iter->k[0].id;
	int index[MAX_COMPONENT];
	int idx = -1;
	int r = query_index(iter, 0, mainkey, &idx, index, NULL);
	if (r <= 0) {
		return 0;
	}
	if (lua_type(L, 3) == LUA_TTABLE) {
		luaL_checktype(L, 3, LUA_TTABLE);
		lua_pushinteger(L, 1);
		lua_rawseti(L, 3, 1);
		lua_pushinteger(L, mainkey);
		lua_rawseti(L, 3, 2);
		lua_pushvalue(L, 2);	// pattern
		lua_rawseti(L, 3, 3);

		read_iter(L, 3, iter, index);
	} else {
		lua_pushboolean(L, 1);
	}
	return 1;
}

static int
check_boolean(lua_State *L, const char *key) {
	int r = 0;
	switch (lua_getfield(L, -1, key)) {
	case LUA_TNIL:
		break;
	case LUA_TBOOLEAN:
		r = lua_toboolean(L, -1);
		break;
	default:
		return luaL_error(L, "Invalid boolean type %s", lua_typename(L, lua_type(L, -1)));
	}
	lua_pop(L, 1);
	return r;
}

static int
is_value(lua_State *L, struct group_field *f) {
	switch (lua_getfield(L, -1, "type")) {
	case LUA_TNIL:
		lua_pop(L, 1);
		return 0;
	case LUA_TNUMBER:
		f->key[0] = 0;
		if (lua_geti(L, -2, 1) != LUA_TTABLE) {
			return luaL_error(L, "Invalid field 0");
		}
		if (lua_geti(L, -1, 3) != LUA_TNUMBER) {
			luaL_error(L, "Invalid field 0 [3] offset");
		}
		f->offset = lua_tointeger(L, -1);
		lua_pop(L, 2);
		f->type = check_type(L);
		return 1;
	default:
		return luaL_error(L, "Invalid value type %s", lua_typename(L, lua_type(L, -1)));
	}
}

static int
get_key(struct entity_world *w, lua_State *L, struct group_key *key, struct group_field *f) {
	if (lua_getfield(L, -1, "id") != LUA_TNUMBER) {
		return luaL_error(L, "Invalid id");
	}
	key->id = lua_tointeger(L, -1);
	lua_pop(L, 1);
	if (key->id != ENTITYID_TAG) {
		if (key->id < 0 || key->id >= MAX_COMPONENT || w->c[key->id].cap == 0) {
			return luaL_error(L, "Invalid id %d", key->id);
		}
	}
	if (lua_getfield(L, -1, "name") != LUA_TSTRING) {
		return luaL_error(L, "Invalid component name");
	}
	get_name(L, key->name, -1);
	lua_pop(L, 1);
	int attrib = 0;
	if (check_boolean(L, "r")) {
		attrib |= COMPONENT_IN;
	}
	if (check_boolean(L, "w")) {
		attrib |= COMPONENT_OUT;
	}
	if (check_boolean(L, "opt")) {
		attrib |= COMPONENT_OPTIONAL;
	}
	if (check_boolean(L, "exist")) {
		attrib |= COMPONENT_EXIST;
	}
	if (check_boolean(L, "absent")) {
		attrib |= COMPONENT_ABSENT;
	}
	key->attrib = attrib;
	if (key->id == ENTITYID_TAG) {
		attrib &= ~COMPONENT_IN;
		attrib &= ~COMPONENT_OPTIONAL;
		attrib &= ~COMPONENT_EXIST;

		if (attrib != 0) {
			return luaL_error(L, "eid should be input");
		}
	}
	if (is_value(L, f)) {
		key->field_n = 1;
		return 1;
	} else {
		int i = 0;
		int ttype;
		while ((ttype = lua_geti(L, -1, i + 1)) != LUA_TNIL) {
			if (ttype != LUA_TTABLE) {
				return luaL_error(L, "Invalid field %d", i + 1);
			}
			get_field(L, i + 1, &f[i]);
			++i;
		}
		key->field_n = i;
		lua_pop(L, 1);
		return i;
	}
}

static int
need_input(struct group_iter *iter, struct group_key *key) {
	int i;
	if (!(key->attrib & COMPONENT_IN))
		return 0;
	if (key->attrib & COMPONENT_OPTIONAL) {
		for (i=0;i<iter->nkey;i++) {
			struct group_key * okey = &iter->k[i];
			if (key->id == okey->id && !(okey->attrib & COMPONENT_FILTER))
				return 0;
		}
		return 1;
	}
	// must input

	for (i=0;i<iter->nkey;i++) {
		struct group_key * okey = &iter->k[i];
		if (key->id == okey->id && !(okey->attrib & COMPONENT_FILTER) && !(okey->attrib & COMPONENT_OPTIONAL))
			return 0;
	}
	return 1;
}

static struct group_iter *
create_group_iter(lua_State *L, int nkey, int field_n) {
	size_t header_size = sizeof(struct group_iter) + sizeof(struct group_key) * (nkey - 1);
	const int align_size = sizeof(void *);
	// align
	header_size = (header_size + align_size - 1) & ~(align_size - 1);
	size_t size = header_size + field_n * sizeof(struct group_field);
	struct group_iter *iter = (struct group_iter *)lua_newuserdatauv(L, size, 1);
	// refer world
	iter->nkey = nkey;
	iter->readonly = 1;
	struct group_field *f = (struct group_field *)((char *)iter + header_size);
	iter->nkey = nkey;
	iter->f = f;
	iter->readonly = 1;
	return iter;
}

static int
generate_diff(lua_State *L, struct group_iter *origin, struct group_iter *ext) {
	int field_n = 0;
	int n = 0;
	int i;
	for (i=0;i<ext->nkey;i++) {
		if (need_input(origin, &ext->k[i])) {
			++n;
			field_n += ext->k[i].field_n;
		}
	}
	if (n == 0) {
		return 0;
	}
	if (origin->world != ext->world)
		luaL_error(L, "Different world");
	struct group_iter *iter = create_group_iter(L, n, field_n);
	iter->world = origin->world;
	struct group_field *f = iter->f;
	struct group_key *key = &iter->k[0];
	int field = 0;
	for (i=0;i<ext->nkey;i++) {
		int fn = ext->k[i].field_n;
		if (need_input(origin, &ext->k[i])) {
			*key = ext->k[i];
			++key;
			memcpy(f, ext->f+field, fn * sizeof(struct group_field));
			f += fn;
		}
		field += fn;
	}
	assert(key == &iter->k[n]);
	assert(f == &iter->f[field_n]);
	return 1;
}

static int
merge_key(struct group_iter *origin, struct group_key *key) {
	struct group_key * okey = NULL;
	int i;
	for (i=0;i<origin->nkey;i++) {
		if (origin->k[i].id == key->id) {
			okey = &origin->k[i];
			break;
		}
	}
	if (okey == NULL) {
		// new key
		return 1;
	}
	if (key->attrib & COMPONENT_IN) {
		if (!(okey->attrib & COMPONENT_IN))
			return 0;
	}
	if (key->attrib & COMPONENT_OUT) {
		if (!(okey->attrib & COMPONENT_OUT))
			return 0;
	}

	// do not need change
	return -1;
}

static int
generate_merge(lua_State *L, struct group_iter *origin, struct group_iter *ext) {
	int field_n = 0;
	int n = 0;
	int i;
	int dirty = 0;
	for (i=0;i<ext->nkey;i++) {
		int m = merge_key(origin, &ext->k[i]);
		if (m >= 0) {
			dirty = 1;
			if (m > 0) {
				++n;
				field_n += ext->k[i].field_n;
			}
		}
	}
	if (!dirty) {
		return 0;
	}
	int origin_field_n = 0;
	for (i=0;i<origin->nkey;i++) {
		origin_field_n += origin->k[i].field_n;
	}
	struct group_iter *iter = create_group_iter(L, n+origin->nkey, field_n + origin_field_n);
	iter->world = origin->world;
	iter->readonly = origin->readonly;
	if (!ext->readonly) {
		iter->readonly = 0;
	}

	for (i=0;i<origin->nkey;i++) {
		iter->k[i] = origin->k[i];
	}
	memcpy(iter->f, origin->f, origin_field_n * sizeof(struct group_field));

	struct group_key *key = &iter->k[origin->nkey];
	struct group_field *f = &iter->f[origin_field_n];

	int field = 0;
	for (i=0;i<ext->nkey;i++) {
		int m = merge_key(origin, &ext->k[i]);
		int fn = ext->k[i].field_n;
		if (m > 0) {
			*key = ext->k[i];
			++key;
			memcpy(f, ext->f+field, fn * sizeof(struct group_field));
			f += fn;
		} else if (m == 0) {
			int j;
			struct group_key *okey = NULL;
			int keyid = ext->k[i].id;
			for (j=0;j<origin->nkey;j++) {
				if (iter->k[j].id == keyid) {
					okey = &iter->k[j];
					break;
				}
			}
			assert(okey);
			if (is_temporary(ext->k[i].attrib) && !is_temporary(okey->attrib))
				luaL_error(L, "Change temporary");
			okey->attrib |= ext->k[i].attrib & (COMPONENT_IN | COMPONENT_OUT);
		}
		field += fn;
	}
	assert(key == &iter->k[n+origin->nkey]);
	assert(f == &iter->f[field_n+origin_field_n]);
	return 1;
}

// merge 2 struct group_iter
// diff : input iter
// merge : for submit
static int
lmergeiter(lua_State *L) {
	struct group_iter * origin = (struct group_iter *)lua_touserdata(L, 1);
	struct group_iter * ext = (struct group_iter *)lua_touserdata(L, 2);
	if (generate_diff(L, origin, ext)) {
		lua_getiuservalue(L, 1, 1);
		lua_setiuservalue(L, -2, 1);
	} else {
		lua_pushnil(L);
	}

	if (generate_merge(L, origin, ext)) {
		lua_getiuservalue(L, 1, 1);
		lua_setiuservalue(L, -2, 1);
	} else {
		lua_pushvalue(L, 1);
	}

	return 2;
}

static int
lgroupiter(lua_State *L) {
	struct entity_world *w = getW(L);
	luaL_checktype(L, 2, LUA_TTABLE);
	int nkey = get_len(L, 2);
	int field_n = 0;
	int i;
	if (nkey == 0) {
		return luaL_error(L, "At least one key");
	}
	if (nkey > MAX_COMPONENT) {
		return luaL_error(L, "Too many keys");
	}
	for (i = 0; i < nkey; i++) {
		if (lua_geti(L, 2, i + 1) != LUA_TTABLE) {
			return luaL_error(L, "index %d is not a table", i);
		}
		int n = get_len(L, -1);
		if (n == 0) {
			struct group_field f;
			if (is_value(L, &f)) {
				n = 1;
			}
		}
		field_n += n;
		lua_pop(L, 1);
	}
	struct group_iter *iter = create_group_iter(L, nkey, field_n);
	lua_pushvalue(L, 1);
	lua_setiuservalue(L, -2, 1);
	iter->world = w;
	struct group_field *f = iter->f;
	for (i = 0; i < nkey; i++) {
		lua_geti(L, 2, i + 1);
		int n = get_key(w, L, &iter->k[i], f);
		struct component_pool *c = &w->c[iter->k[i].id];
		if (c->stride == STRIDE_TAG && is_temporary(iter->k[i].attrib)) {
			return luaL_error(L, "%s is a tag, use %s?out instead", iter->k[i].name, iter->k[i].name);
		}
		f += n;
		lua_pop(L, 1);
		if (c->stride == STRIDE_LUA) {
			if (n != 0)
				return luaL_error(L, ".%s is object component, no fields needed", iter->k[i].name);
			iter->k[i].attrib |= COMPONENT_OBJECT;
		}
		int attrib = iter->k[i].attrib;
		if (!(attrib & COMPONENT_FILTER)) {
			int readonly = (attrib & COMPONENT_IN) && !(attrib & COMPONENT_OUT);
			if (!readonly)
				iter->readonly = 0;
		}
	}
	int mainkey_attrib = iter->k[0].attrib;
	if (mainkey_attrib & COMPONENT_ABSENT) {
		return luaL_error(L, "The main key can't be absent");
	}
	if (luaL_newmetatable(L, "ENTITY_GROUPITER")) {
		lua_pushcfunction(L, lpairs_group);
		lua_setfield(L, -2, "__call");
	}
	lua_setmetatable(L, -2);
	return 1;
}

static int
lcheck_iter(lua_State *L) {
	int enable_check = lua_toboolean(L, 1);
	luaL_newmetatable(L, "ENTITY_GROUPITER");
	lua_pushcfunction(L, enable_check ? lpairs_group_check : lpairs_group);
	lua_setfield(L, -2, "__call");
	return 0;
}

static int
lindex_entity(lua_State *L) {
	struct entity_world *w = getW(L);
	uint64_t eid = (uint64_t)luaL_checkinteger(L, 2);
	int index = entity_id_find(&w->eid, eid);
	if (index < 0)
		return luaL_error(L, "Missing entity %d", eid);
	lua_pushinteger(L, index);
	return 1;
}


static int
lexist(lua_State *L) {
	struct entity_world *w = getW(L);
	uint64_t eid = (uint64_t)luaL_checkinteger(L, 2);
	lua_pushboolean(L, entity_id_find(&w->eid, eid) >= 0);
	return 1;
}

static int
lfetch(lua_State *L) {
	struct entity_world *w = getW(L);
	uint64_t eid = (uint64_t)luaL_checkinteger(L, 2);
	int index = entity_id_find(&w->eid, eid);
	if (index < 0) {
		return 0;
	}
	lua_createtable(L, 3, 0);
	lua_pushinteger(L, index + 1);
	lua_rawseti(L, -2, 1);
	lua_pushinteger(L, ENTITYID_TAG);
	lua_rawseti(L, -2, 2);
	return 1;
}

static int
lremove(lua_State *L) {
	struct entity_world *w = getW(L);
	if (lua_isinteger(L, 2)) {
		// It's eid
		int index = entity_id_find(&w->eid, lua_tointeger(L, 2));
		if (index < 0)
			return luaL_error(L, "No eid %d", lua_tointeger(L, 2));
		struct ecs_token t = { index };
		entity_remove_(w, t);
	} else {
		luaL_checktype(L, 2, LUA_TTABLE);
		int iter = get_integer(L, 2, 1, "index") - 1;
		int mainkey = get_integer(L, 2, 2, "mainkey");
		struct ecs_token t;
		if (entity_fetch_(w, mainkey, iter, &t)) {
			entity_remove_(w, t);
		}
	}
	return 0;
}

void
ecs_read_object_(lua_State *L, struct group_iter *iter, void *buffer) {
	struct group_field *f = iter->f;
	if (f->key[0] == 0) {
		// value type
		read_value(L, f, buffer);
	} else {
		lua_createtable(L, 0, iter->k[0].field_n);
		int lua_index = lua_gettop(L);
		read_component(L, iter->k[0].field_n, f, lua_index, buffer);
	}
}

static int
lobject(lua_State *L) {
	struct group_iter *iter = luaL_checkudata(L, 1, "ENTITY_GROUPITER");
	int index = luaL_checkinteger(L, 3) - 1;
	int cid = iter->k[0].id;
	struct entity_world *w = iter->world;
	if (cid < 0 || cid >= MAX_COMPONENT) {
		return luaL_error(L, "Invalid object %d", cid);
	}
	lua_settop(L, 2);
	struct component_pool *c = &w->c[cid];
	if (c->n <= index) {
		return luaL_error(L, "No object %d", cid);
	}
	if (c->stride == STRIDE_LUA) {
		// lua object
		if (lua_getiuservalue(L, 1, 1) != LUA_TUSERDATA) {
			return luaL_error(L, "No world");
		}
		if (lua_isnil(L, 2)) {
			get_lua_component(L, iter->world, c, index);
		} else {

			lua_settop(L, 2);
			set_lua_component(L, iter->world, c, index);
		}
		return 1;
	} else if (c->stride == 0) {
		if (lua_type(L, 2) != LUA_TBOOLEAN)
			return luaL_error(L, "%s is a tag, need boolean", iter->k[0].name);
		if (!lua_toboolean(L, 2)) {
			entity_disable_tag_(w, cid, index);
		}
		return 1;
	} else if (c->stride < 0) {
		return luaL_error(L, "Invalid object %d", cid);
	}
	void *buffer = get_ptr(c, index);
	if (lua_isnoneornil(L, 2)) {
		ecs_read_object_(L, iter, buffer);
	} else {
		if (lua_type(L, 2) == LUA_TSTRING) {
			size_t sz;
			const char *raw = lua_tolstring(L, 2, &sz);
			if (sz != c->stride) {
				return luaL_error(L, "rawdata need %d bytes, it's %d.", c->stride, (int)sz);
			}
			memcpy(buffer, raw, sz);
		} else {
			// write object
			lua_pushvalue(L, 2);
			ecs_write_component_object_(L, iter->k[0].field_n, iter->f, buffer);
		}
	}
	return 1;
}

static int
ldumpid(lua_State *L) {
	struct entity_world *w = getW(L);
	int cid = luaL_checkinteger(L, 2);
	if (cid == ENTITYID_TAG) {
		int n = w->eid.n;
		lua_createtable(L, n, 0);
		int i;
		for (i = 0; i<n ; i++) {
			lua_pushinteger(L, w->eid.id[i]);
			lua_rawseti(L, -2, i+1);
		}
		return 1;
	}
	check_cid_valid(L, w, cid);
	struct component_pool *c = &w->c[cid];
	lua_createtable(L, c->n, 0);
	int i;
	for (i = 0; i < c->n; i++) {
		entity_index_t index = c->id[i];
		lua_pushinteger(L, ENTITY_EID(w, index));
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

static int
lfilter(lua_State *L) {
	struct entity_world *w = getW(L);
	int tagid = check_cid(L, w, 2);
	entity_clear_type_(w, tagid);
	struct group_iter *iter = luaL_checkudata(L, 3, "ENTITY_GROUPITER");
	int mainkey = iter->k[0].id;
	int i,j;
	struct ecs_token token;
	for (i = -1; (i=entity_next_tag_(w, mainkey, i, &token)) >=0;) {
		for (j = 1; j < iter->nkey; j++) {
			struct group_key *k = &iter->k[j];
			if ((entity_component_index_(w, token, k->id) >= 0) ^ (!(k->attrib & COMPONENT_ABSENT))) {
				break;
			}
		}
		if (j == iter->nkey)
			entity_enable_tag_(w, token, tagid);
	}
	return 0;
}

int
laccess(lua_State *L) {
	struct entity_world *w = getW(L);
	uint64_t eid = (uint64_t)luaL_checkinteger(L, 2);
	int idx = entity_id_find(&w->eid, eid);
	if (idx < 0)
		return luaL_error(L, "eid %d not found", idx);
	struct group_iter *iter = lua_touserdata(L, 3);
	int value_index = 4;
	if (iter->nkey > 1)
		return luaL_error(L, "More than one key in pattern");
	if (iter->world != w)
		return luaL_error(L, "World mismatch");

	int output = (lua_gettop(L) >= value_index);
	struct group_key *k = &iter->k[0];
	struct ecs_token token = { idx };

	struct component_pool *c = &w->c[k->id];
	if (c->stride == STRIDE_TAG) {
		// It is a tag
		if (output) {
			if (lua_toboolean(L, value_index)) {
				entity_enable_tag_(w, token, k->id);
			} else {
				int index = entity_component_index_(w, token, k->id);
				if (index >= 0)
					entity_disable_tag_(w, k->id, index);
			}
			return 0;
		} else {
			lua_pushboolean(L, entity_component_index_(w, token, k->id) >= 0);
			return 1;
		}
	}

	int index = entity_component_index_(w, token, k->id);
	if (index < 0) {
		if (output)
			return luaL_error(L, "No component .%s", k->name);
		else
			return 0;
	}
	if (c->stride == STRIDE_LUA) {
		// It is lua component
		if (output) {
			lua_settop(L, value_index);
			set_lua_component(L, w, c, index);
			return 0;
		} else {
			get_lua_component(L, w, c, index);
			return 1;
		}
	}

	// It is C component
	void *buffer = get_ptr(c, index);
	if (output) {
		ecs_write_component_object_(L, k->field_n, iter->f, buffer);
		return 0;
	} else {
		ecs_read_object_(L, iter, buffer);
		return 1;
	}
}


// 1: world
// 2: groupid
// 3: eid
static int
lgroup_add(lua_State *L) {
	struct entity_world *w = getW(L);
	int groupid = luaL_checkinteger(L, 2);
	uint64_t eid = (uint64_t)luaL_checkinteger(L, 3);
	if (!entity_group_add_(&w->group, groupid, eid)) {
		return luaL_error(L, "group add fail");
	}
	return 0;
}

#define MAXGROUP 1024

// 1: world
// 2: tagid
// 3...: groupids
static int
lgroup_enable(lua_State *L) {
	struct entity_world *w = getW(L);
	int tagid = check_tagid(L, w, 2);
	int top = lua_gettop(L);
	int from = 3;
	int n = top - from + 1;
	if (n > MAXGROUP) {
		return luaL_error(L, "Too many groups (%d > %d)", n, MAXGROUP);
	}

	int groupid[MAXGROUP];
	int i;
	for (i=0;i<n;i++) {
		groupid[i] = luaL_checkinteger(L, from+i);
	}
	entity_group_enable_(w, tagid, n, groupid);
	return 0;
}

static int
lgroup_get(lua_State *L) {
	struct entity_world *w = getW(L);
	int groupid = luaL_checkinteger(L, 2);
	entity_group_id_(&w->group, groupid, L);
	return 1;
}

static int
lmethods(lua_State *L) {
	luaL_Reg m[] = {
		{ "memory", lcount_memory },
		{ "collect", lcollect_memory },
		{ "_newtype", lnew_type },
		{ "_newentity", lnew_entity },
		{ "_indexentity", lindex_entity },
		{ "_addcomponent", ladd_component },
		{ "_update", lupdate },
		{ "_clear", lclear_type },
		{ "context", lcontext },
		{ "_groupiter", lgroupiter },
		{ "_mergeiter", lmergeiter },
		{ "_fetch", lfetch },
		{ "exist", lexist },
		{ "remove", lremove },
		{ "submit", lsubmit },
		{ "_object", lobject },
		{ "_read", lread },
		{ "_first", lfirst },
		{ "_dumpid", ldumpid },
		{ "_count", lcount },
		{ "_filter", lfilter },
		{ "_access", laccess },
		{ "__gc", ldeinit_world },
		{ "group_add", lgroup_add },
		{ "_group_enable", lgroup_enable },
		{ "group_get", lgroup_get },
		{ NULL, NULL },
	};
	luaL_newlib(L, m);

	return 1;
}

LUAMOD_API int
luaopen_ecs_core(lua_State *L) {
	luaL_checkversion(L);
	luaL_Reg l[] = {
		{ "_world", lnew_world },
		{ "_methods", lmethods },
		{ "check_select", lcheck_iter },
		
		/*library extension*/
		{ "_persistence_methods", lpersistence_methods },
		{ "_template_methods", ltemplate_methods },
		{ NULL, NULL },
	};
	luaL_newlib(L, l);
	lua_pushinteger(L, MAX_COMPONENT - 1);
	lua_setfield(L, -2, "_MAXTYPE");
	lua_pushinteger(L, TYPE_INT);
	lua_setfield(L, -2, "_TYPEINT");
	lua_pushinteger(L, TYPE_FLOAT);
	lua_setfield(L, -2, "_TYPEFLOAT");
	lua_pushinteger(L, TYPE_BOOL);
	lua_setfield(L, -2, "_TYPEBOOL");
	lua_pushinteger(L, TYPE_INT64);
	lua_setfield(L, -2, "_TYPEINT64");
	lua_pushinteger(L, TYPE_DWORD);
	lua_setfield(L, -2, "_TYPEDWORD");
	lua_pushinteger(L, TYPE_WORD);
	lua_setfield(L, -2, "_TYPEWORD");
	lua_pushinteger(L, TYPE_BYTE);
	lua_setfield(L, -2, "_TYPEBYTE");
	lua_pushinteger(L, TYPE_DOUBLE);
	lua_setfield(L, -2, "_TYPEDOUBLE");
	lua_pushinteger(L, TYPE_USERDATA);
	lua_setfield(L, -2, "_TYPEUSERDATA");
	lua_pushinteger(L, STRIDE_LUA);
	lua_setfield(L, -2, "_LUAOBJECT");
	lua_pushinteger(L, ENTITY_REMOVED);
	lua_setfield(L, -2, "_REMOVED");
	lua_pushinteger(L, ENTITYID_TAG);
	lua_setfield(L, -2, "_EID");
	lua_pushlightuserdata(L, NULL);
	lua_setfield(L, -2, "NULL");

	return 1;
}

#ifdef TEST_LUAECS

#include "ecs_test.h"

#endif

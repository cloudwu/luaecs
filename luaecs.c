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

static void
init_component_pool(struct entity_world *w, int index, int stride, int opt_size) {
	struct component_pool *c = &w->c[index];
	c->cap = opt_size;
	c->n = 0;
	c->stride = stride;
	c->id = NULL;
	c->last_lookup = 0;
	if (stride > 0) {
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
	if (stride == STRIDE_LUA) {
		if (cid == 0) {
			luaL_error(L, "0 can't be lua type");
		}
		lua_newtable(L);
		lua_setiuservalue(L, world_index, cid);
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
	size_t sz = sizeof(*w) + sizeof(uint64_t) * w->eid.cap;
	int i;
	size_t msz = sz;
	for (i = 0; i < MAX_COMPONENT; i++) {
		struct component_pool *c = &w->c[i];
		if (c->id) {
			sz += c->cap * sizeof(entity_index_t);
			msz += c->n * sizeof(entity_index_t);
		}
		if (c->buffer != DUMMY_PTR) {
			sz += c->cap * c->stride;
			msz += c->n * c->stride;
		}
	}
	lua_pushinteger(L, sz);
	lua_pushinteger(L, msz);
	return 2;
}

static void
init_buffers(struct component_pool *pool) {
	int cap = pool->cap;
	if (pool->stride <= 0) {
		// only id
		size_t id_sz = sizeof(entity_index_t) * cap;
		pool->id = (entity_index_t *)malloc(id_sz);
	} else {
		size_t offset = pool->stride * cap;
		size_t sz = offset + sizeof(entity_index_t) * cap;
		pool->buffer = malloc(sz);
		pool->id = (entity_index_t *)((uint8_t *)pool->buffer + offset);
	}
}

static void
move_buffers(struct component_pool *pool, void *buffer, entity_index_t *id) {
	memcpy(pool->id, id, pool->n * sizeof(entity_index_t));
	if (pool->stride > 0) {
		memcpy(pool->buffer, buffer, pool->n * pool->stride);
		free(buffer);
	} else {
		free(id);
	}
}

static void
shrink_component_pool(lua_State *L, struct component_pool *c, int id) {
	if (c->id == NULL)
		return;
	if (c->n == 0) {
		if (c->stride > 0) {
			free(c->buffer);
			c->buffer = NULL;
			c->id = NULL;
		} else {
			free(c->id);
			c->id = NULL;
		}
		if (c->stride == STRIDE_LUA) {
			lua_pushnil(L);
			lua_setiuservalue(L, 1, id);
		}
	} else if (c->stride > 0 && c->n < c->cap) {
		c->cap = c->n;
		void *buffer = c->buffer;
		entity_index_t *id = c->id;
		move_buffers(c, buffer, id);
	}
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
		pool->cap = cap * 3 / 2;
		void *buffer = pool->buffer;
		entity_index_t *id = pool->id;
		init_buffers(pool);
		move_buffers(pool, buffer, id);
	}
	++pool->n;
	pool->id[index] = eid;
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

	int n = e->n;
	if (n >= MAX_ENTITY) {
		return INVALID_ENTITY;
	}
	e->n++;

	if (n >= e->cap) {
		if (e->id == NULL) {
			e->cap = ENTITY_INIT_SIZE;
			e->id = (uint64_t *)malloc(e->cap * sizeof(uint64_t));
		} else {
		int newcap = e->cap * 3 / 2;
			e->id = (uint64_t *)realloc(e->id, newcap * sizeof(uint64_t));
			e->cap = newcap;
		}
	}
	uint64_t eid = ++e->last_id;
	e->id[n] = eid;
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
	lua_pushinteger(L, index);
	lua_pushinteger(L, w->eid.id[index]);
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
	switch (pool->stride) {
	case STRIDE_LUA:
		if (lua_getiuservalue(L, 1, cid) != LUA_TTABLE) {
			luaL_error(L, "Missing lua object table for type %d", cid);
		}
		while (i < pool->n && removed_n < removed->n) {
			int cmp = ENTITY_INDEX_CMP(pool->id[i], removed_id[removed_n]);
			if (cmp == 0) {
				// pool[i] should be removed
				lua_pushnil(L);
				lua_rawseti(L, -2, ENTITY_EID(w, pool->id[i]));
				++removed_n;
				++delta;
				++i;
			} else if (cmp < 0) {
				// pool[i] < current removed
				move_tag(pool, i, index, removed_n);
				++index;
				++i;
			} else {
				// pool[i] > current removed, find next removed
				removed_n = less_part(removed, pool->id[i], removed_n);
			}
		}
		for (;i<pool->n;i++) {
			move_tag(pool, i, index, removed_n);
			++index;
		}
		lua_pop(L, 1); // pop lua object table
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
	struct component_pool *c = &w->c[cid];
	int index = ecs_lookup_component_(c, eid, c->n - 1);
	if (index < 0)
		index = ecs_add_component_id_(w, cid, eid);
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
add_sibling_index_(lua_State *L, struct entity_world *w, int cid, int index, int slibling_id) {
	struct component_pool *c = &w->c[cid];
	assert(index >= 0 && index < c->n);
	entity_index_t eid = c->id[index];
	// todo: pcall add_component_
	int ret = ecs_add_component_id_(w, slibling_id, eid);
	return ret;
}

static int
lcontext(lua_State *L) {
	struct entity_world *w = getW(L);
	luaL_checktype(L, 2, LUA_TTABLE);
	lua_len(L, 2);
	int n = lua_tointeger(L, -1);
	lua_pop(L, 1);
	if (n <= 0) {
		return luaL_error(L, "Invalid length %d of table", n);
	}
	size_t sz = sizeof(struct ecs_context) + sizeof(int) * n;
	struct ecs_context *ctx = (struct ecs_context *)lua_newuserdatauv(L, sz, 1);
	ctx->L = (void *)lua_newthread(L);
	lua_pushvalue(L, 1);
	lua_xmove(L, ctx->L, 1); // put world in the index 1 of newthread
	lua_setiuservalue(L, -2, 1);
	ctx->max_id = n;
	ctx->world = w;
	static struct ecs_capi c_api = {
		entity_iter_,
		entity_clear_type_,
		entity_sibling_index_,
		entity_add_sibling_,
		entity_new_,
		entity_remove_,
		entity_enable_tag_,
		entity_disable_tag_,
		entity_get_lua_,
	};
	ctx->api = &c_api;
	ctx->cid[0] = ENTITY_REMOVED;
	int i;
	for (i = 1; i <= n; i++) {
		if (lua_geti(L, 2, i) != LUA_TNUMBER) {
			return luaL_error(L, "Invalid id at index %d", i);
		}
		ctx->cid[i] = lua_tointeger(L, -1);
		lua_pop(L, 1);
		int cid = ctx->cid[i];
		if (cid < 0 || cid >= MAX_COMPONENT)
			return luaL_error(L, "Invalid id (%d) at index %d", cid, i);
	}
	return 1;
}

static int
lnew_world(lua_State *L) {
	size_t sz = sizeof(struct entity_world);
	struct entity_world *w = (struct entity_world *)lua_newuserdatauv(L, sz, MAX_COMPONENT-1);	// REMOVED 0 don't need uservalue
	memset(w, 0, sz);
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
	free(w->eid.id);
	w->eid.id = NULL;
	int i;
	for (i=0;i<MAX_COMPONENT;i++) {
		struct component_pool *c = &w->c[i];
		if (c->stride > 0) {
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
get_field(lua_State *L, int i, struct group_field *f) {
	if (lua_geti(L, -1, 1) != LUA_TNUMBER) {
		luaL_error(L, "Invalid field %d [1] type", i);
	}
	f->type = check_type(L);

	if (lua_geti(L, -1, 2) != LUA_TSTRING) {
		luaL_error(L, "Invalid field %d [2] key", i);
	}
	f->key = lua_tostring(L, -1);
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
			luaL_error(L, "Invalid .%s type %s (int)", f->key ? f->key : "*", lua_typename(L, luat));
		*(int *)ptr = lua_tointeger(L, -1);
		break;
	case TYPE_FLOAT:
		if (luat != LUA_TNUMBER)
			luaL_error(L, "Invalid .%s type %s (float)", f->key ? f->key : "*", lua_typename(L, luat));
		*(float *)ptr = lua_tonumber(L, -1);
		break;
	case TYPE_BOOL:
		if (luat != LUA_TBOOLEAN)
			luaL_error(L, "Invalid .%s type %s (bool)", f->key ? f->key : "*", lua_typename(L, luat));
		*(unsigned char *)ptr = lua_toboolean(L, -1);
		break;
	case TYPE_INT64:
		if (!lua_isinteger(L, -1))
			luaL_error(L, "Invalid .%s type %s (int64)", f->key ? f->key : "*", lua_typename(L, luat));
		*(int64_t *)ptr = lua_tointeger(L, -1);
		break;
	case TYPE_DWORD:
		if (!lua_isinteger(L, -1))
			luaL_error(L, "Invalid .%s type %s (uint32)", f->key ? f->key : "*", lua_typename(L, luat));
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
			luaL_error(L, "Invalid .%s type %s (uint16)", f->key ? f->key : "*", lua_typename(L, luat));
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
			luaL_error(L, "Invalid .%s type %s (uint8)", f->key ? f->key : "*", lua_typename(L, luat));
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
			luaL_error(L, "Invalid .%s type %s (double)", f->key ? f->key : "*", lua_typename(L, luat));
		*(double *)ptr = lua_tonumber(L, -1);
		break;
	case TYPE_USERDATA:
		if (luat != LUA_TLIGHTUSERDATA)
			luaL_error(L, "Invalid .%s type %s (pointer)", f->key ? f->key : "*", lua_typename(L, luat));
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
		if (f->key) {
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
		if (f->key == NULL) {
			// value type
			return 1;
		}
		return luaL_error(L, "Invalid iterator type %s", lua_typename(L, lua_type(L, -1)));
	}
}

void
ecs_write_component_object_(lua_State *L, int n, struct group_field *f, void *buffer) {
	if (f->key == NULL) {
		write_value(L, f, buffer);
	} else {
		write_component(L, n, f, -1, (char *)buffer);
		lua_pop(L, 1);
	}
}

static void
write_component_object_check(lua_State *L, int n, struct group_field *f, const void *buffer, const char *name) {
	if (f->key == NULL) {
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
update_iter(lua_State *L, int world_index, int lua_index, struct group_iter *iter, int idx, int mainkey, int skip) {
	struct group_field *f = iter->f;
	int disable_mainkey = 0;

	int i;
	for (i = 0; i < skip; i++) {
		f += iter->k[i].field_n;
	}
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
							entity_enable_tag_(iter->world, mainkey, idx, k->id);
						} else {
							if (k->id == mainkey)
								disable_mainkey = 1;
							else
								entity_disable_tag_(iter->world, mainkey, idx, k->id);
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
				int index = entity_sibling_index_(iter->world, mainkey, idx, k->id);
				if (index == 0) {
					luaL_error(L, "Can't find sibling %s of %s", k->name, iter->k[0].name);
				}
				if (c->stride == STRIDE_LUA) {
					if (lua_getiuservalue(L, world_index, k->id) != LUA_TTABLE) {
						luaL_error(L, "Missing lua table for %d", k->id);
					}
					lua_insert(L, -2);
					lua_rawseti(L, -2, ecs_get_eid(iter->world, k->id, index-1));
				} else {
					void *buffer = get_ptr(c, index - 1);
					ecs_write_component_object_(L, k->field_n, f, buffer);
				}
			} else if (is_temporary(k->attrib)
				&& get_write_component(L, lua_index, k->name, f, c)) {
				if (c->stride == STRIDE_LUA) {
					int index = add_sibling_index_(L, iter->world, mainkey, idx, k->id);
					if (lua_getiuservalue(L, world_index, k->id) != LUA_TTABLE) {
						luaL_error(L, "Missing lua table for %d", k->id);
					}
					lua_insert(L, -2);
					lua_rawseti(L, -2, ecs_get_eid(iter->world, k->id, index-1));
				} else {
					void *buffer = entity_add_sibling_(iter->world, mainkey, idx, k->id, NULL);
					ecs_write_component_object_(L, k->field_n, f, buffer);
				}
			}
		}
		f += k->field_n;
	}
	return disable_mainkey;
}

static void
update_last_index(lua_State *L, int world_index, int lua_index, struct group_iter *iter, int idx) {
	int mainkey = iter->k[0].id;
	struct component_pool *c = &iter->world->c[mainkey];
	int disable_mainkey = 0;
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
				assert(mainkey > 0);
				if (lua_getiuservalue(L, world_index, mainkey) != LUA_TTABLE) {
					luaL_error(L, "Missing lua table for %d", mainkey);
				}
				lua_insert(L, -2);
				lua_rawseti(L, -2, ecs_get_eid(iter->world, mainkey, idx));
			} else {
				void *buffer = get_ptr(c, idx);
				ecs_write_component_object_(L, iter->k[0].field_n, iter->f, buffer);
			}
		}
	}

	update_iter(L, world_index, lua_index, iter, idx, mainkey, 1);

	if (disable_mainkey) {
		entity_disable_tag_(iter->world, mainkey, idx, mainkey);
	}
}

static void
check_update(lua_State *L, int world_index, int lua_index, struct group_iter *iter, int idx) {
	int mainkey = iter->k[0].id;
	int i;
	struct group_field *f = iter->f;
	for (i = 0; i < iter->nkey; i++) {
		struct group_key *k = &iter->k[i];
		if (!(k->attrib & COMPONENT_FILTER)) {
			struct component_pool *c = &iter->world->c[k->id];
			if (c->stride > 0 && !(k->attrib & COMPONENT_OUT) && (k->attrib & COMPONENT_IN)) {
				// readonly C component, check it
				if (get_write_component(L, lua_index, k->name, f, c)) {
					int index = entity_sibling_index_(iter->world, mainkey, idx, k->id);
					if (index > 0) {
						void *buffer = get_ptr(c, index - 1);
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
	if (f->key == NULL) {
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
query_index(struct group_iter *iter, int skip, int mainkey, int idx, unsigned int index[MAX_COMPONENT]) {
	if (entity_iter_(iter->world, mainkey, idx) == NULL) {
		return -1;
	}
	int j;
	for (j = skip; j < iter->nkey; j++) {
		struct group_key *k = &iter->k[j];
		if (k->attrib & COMPONENT_ABSENT) {
			if (entity_sibling_index_(iter->world, mainkey, idx, k->id)) {
				// exist. try next
				return 0;
			}
			index[j] = 0;
		} else if (!is_temporary(k->attrib)) {
			if (k->id == ENTITYID_TAG) {
				uint32_t x = index_(iter->world->c[mainkey].id[idx]);
				index[j] = x;
			} else {
				index[j] = entity_sibling_index_(iter->world, mainkey, idx, k->id);
				if (index[j] == 0) {
					if (!(k->attrib & COMPONENT_OPTIONAL)) {
						// required. try next
						return 0;
					}
				}
			}
		} else {
			index[j] = 0;
		}
	}
	return 1;
}

static void
check_index(lua_State *L, struct group_iter *iter, int mainkey, int idx) {
	int i;
	for (i = 0; i < iter->nkey; i++) {
		struct group_key *k = &iter->k[i];
		if (k->attrib & COMPONENT_ABSENT) {
			if (entity_sibling_index_(iter->world, mainkey, idx, k->id)) {
				luaL_error(L, ".%s should be absent", k->name);
			}
		} else if (!is_temporary(k->attrib)) {
			if (entity_sibling_index_(iter->world, mainkey, idx, k->id) == 0) {
				if (!(k->attrib & COMPONENT_OPTIONAL)) {
					luaL_error(L, ".%s not found", k->name);
				}
			}
		}
	}
}

static void
read_iter(lua_State *L, int world_index, int obj_index, struct group_iter *iter, unsigned int index[MAX_COMPONENT]) {
	struct group_field *f = iter->f;
	int i;
	for (i = 0; i < iter->nkey; i++) {
		struct group_key *k = &iter->k[i];
		if (k->id == ENTITYID_TAG) {
			lua_pushinteger(L, iter->world->eid.id[index[i]]);
			lua_setfield(L, obj_index, "_eid");
		} else if (!(k->attrib & COMPONENT_FILTER)) {
			struct component_pool *c = &iter->world->c[k->id];
			if (c->stride == STRIDE_LUA) {
				// lua object component
				if (index[i]) {
					if (lua_getiuservalue(L, world_index, k->id) != LUA_TTABLE) {
						luaL_error(L, "Missing lua table for %d", k->id);
					}

					lua_rawgeti(L, -1, ecs_get_eid(iter->world, k->id, index[i]-1));
					lua_setfield(L, obj_index, k->name);
					lua_pop(L, 1);
				} else {
					lua_pushnil(L);
					lua_setfield(L, obj_index, k->name);
				}
			} else if (k->attrib & COMPONENT_IN) {
				if (index[i]) {
					void *ptr = get_ptr(c, index[i] - 1);
					read_component_in_field(L, obj_index, k->name, k->field_n, f, ptr);
				} else {
					lua_pushnil(L);
					lua_setfield(L, obj_index, k->name);
				}
			} else if (index[i] == 0 && !is_temporary(k->attrib)) {
				lua_pushnil(L);
				lua_setfield(L, obj_index, k->name);
			}
		}
		f += k->field_n;
	}
}

static int
lsync(lua_State *L) {
	struct group_iter *iter = luaL_checkudata(L, 2, "ENTITY_GROUPITER");
	luaL_checktype(L, 3, LUA_TTABLE);
	int idx = get_integer(L, 3, 1, "index") - 1;
	int mainkey = get_integer(L, 3, 2, "mainkey");
	unsigned int index[MAX_COMPONENT];
	int r = query_index(iter, 0, mainkey, idx, index);
	if (r <= 0) {
		if (r < 0) {
			return luaL_error(L, "Invalid iterator of mainkey (%d)", mainkey);
		} else {
			check_index(L, iter, mainkey, idx); // raise error
			return 0;
		}
	}

	if (!iter->readonly) {
		if (update_iter(L, 1, 3, iter, idx, mainkey, 0)) {
			entity_disable_tag_(iter->world, mainkey, idx, mainkey);
		}
	}
	read_iter(L, 1, 3, iter, index);
	return 0;
}

static int
lreadid(lua_State *L) {
	struct group_iter *iter = luaL_checkudata(L, 2, "ENTITY_GROUPITER");
	luaL_checktype(L, 3, LUA_TTABLE);
	int idx = get_integer(L, 3, 1, "index") - 1;
	int mainkey = get_integer(L, 3, 2, "mainkey");
	if (entity_iter_(iter->world, mainkey, idx) == NULL) {
		return 0;
	}
	struct entity_world *w = getW(L);
	entity_index_t index = w->c[mainkey].id[idx];
	lua_pushinteger(L, (lua_Integer)ENTITY_EID(w, index));
	return 1;
}

static int
lread(lua_State *L) {
	struct group_iter *iter = luaL_checkudata(L, 2, "ENTITY_GROUPITER");
	luaL_checktype(L, 3, LUA_TTABLE);
	int idx = get_integer(L, 3, 1, "index") - 1;
	int mainkey = get_integer(L, 3, 2, "mainkey");
	unsigned int index[MAX_COMPONENT];
	int r = query_index(iter, 0, mainkey, idx, index);
	if (r <= 0) {
		return 0;
	}

	if (!iter->readonly) {
		return luaL_error(L, "Pattern is not readonly");
	}
	read_iter(L, 1, 3, iter, index);
	return 1;
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

	int world_index = lua_gettop(L);

	unsigned int index[MAX_COMPONENT];
	int mainkey = iter->k[0].id;

	if (i > 0) {
		if (check) {
			check_update(L, world_index, 2, iter, i - 1);
		}
		if (!iter->readonly) {
			update_last_index(L, world_index, 2, iter, i - 1);
		}
	}
	for (;;) {
		int idx = i++;
		index[0] = idx + 1;
		int ret = query_index(iter, 1, mainkey, idx, index);
		if (ret < 0)
			return 0;
		if (ret > 0)
			break;
	}

	lua_pushinteger(L, i);
	lua_rawseti(L, 2, 1);

	read_iter(L, world_index, 2, iter, index);

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
	unsigned int index[MAX_COMPONENT];
	int mainkey = iter->k[0].id;
	int count = 0;
	int i;
	for (i = 0;; ++i) {
		int ret = query_index(iter, 1, mainkey, i, index);
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
	if (k->field_n == 1 && f[0].key == NULL) {
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
	lua_createtable(L, 2, iter->nkey);
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
		f->key = NULL;
		f->offset = 0;
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
	key->name = lua_tostring(L, -1);
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
		if (attrib != COMPONENT_IN) {
			return luaL_error(L, "_id must be in");
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
	size_t header_size = sizeof(struct group_iter) + sizeof(struct group_key) * (nkey - 1);
	const int align_size = sizeof(void *);
	// align
	header_size = (header_size + align_size - 1) & ~(align_size - 1);
	size_t size = header_size + field_n * sizeof(struct group_field);
	struct group_iter *iter = (struct group_iter *)lua_newuserdatauv(L, size, 1);
	// refer world
	lua_pushvalue(L, 1);
	lua_setiuservalue(L, -2, 1);
	iter->nkey = nkey;
	iter->world = w;
	iter->readonly = 1;
	struct group_field *f = (struct group_field *)((char *)iter + header_size);
	iter->f = f;
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
find_eid_(struct entity_world *w, uint64_t eid, int begin, int end) {
	const uint64_t *id = w->eid.id;
	while (begin < end) {
		int mid = (begin + end) / 2;
		if (eid == id[mid]) {
			return mid;
		}
		if (eid < id[mid])
			end = mid;
		else
			begin = mid+1;
	}
	int p = begin > w->eid.n / 2 ? begin : begin + 1;
	return -p;
}

static int
find_eid(struct entity_world *w, uint64_t eid) {
	unsigned h = (unsigned)(2654435761 * (uint32_t)eid) % ENTITY_ID_LOOKUP;
	entity_index_t p = w->eid.lookup[h];
	int index = index_(p);
	int begin = 0;
	int end;
	if (index >= w->eid.n) {
		end = w->eid.n;
	} else {
		uint64_t v = w->eid.id[index];
		if (v == eid) {
			return index;
		}
		if (v > eid) {
			end = index;
		} else {
			begin = index + 1;
			end = w->eid.n;
		}
	}
	index = find_eid_(w, eid, begin, end);
	if (index < 0) {
		w->eid.lookup[h] = make_index_(-index);
		return -1;
	} else {
		w->eid.lookup[h] = make_index_(index);
		return index;
	}
}

static int
lexist(lua_State *L) {
	struct entity_world *w = getW(L);
	uint64_t eid = (uint64_t)luaL_checkinteger(L, 2);
	lua_pushboolean(L, find_eid(w, eid) >= 0);
	return 1;
}

static int
lremove(lua_State *L) {
	struct entity_world *w = getW(L);
	if (lua_isinteger(L, 2)) {
		// It's eid
		int index = find_eid(w, lua_tointeger(L, 2));
		if (index < 0)
			return luaL_error(L, "No eid %x", lua_tointeger(L, 2));
		entity_remove_(w, ENTITYID_TAG, index);
	} else {
		luaL_checktype(L, 2, LUA_TTABLE);
		int iter = get_integer(L, 2, 1, "index") - 1;
		int mainkey = get_integer(L, 2, 2, "mainkey");
		entity_remove_(w, mainkey, iter);
	}
	return 0;
}

void
ecs_read_object_(lua_State *L, struct group_iter *iter, void *buffer) {
	struct group_field *f = iter->f;
	if (f->key == NULL) {
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
		if (lua_getiuservalue(L, -1, cid) != LUA_TTABLE) {
			return luaL_error(L, "Missing lua table for %d", cid);
		}
		if (lua_isnil(L, 2)) {
			lua_rawgeti(L, -1, ecs_get_eid(w, cid, index));
		} else {
			lua_pushvalue(L, 2);
			lua_rawseti(L, -2, ecs_get_eid(w, cid, index));
			lua_settop(L, 2);
		}
		return 1;
	} else if (c->stride == 0) {
		if (lua_type(L, 2) != LUA_TBOOLEAN)
			return luaL_error(L, "%s is a tag, need boolean", iter->k[0].name);
		if (!lua_toboolean(L, 2)) {
			entity_disable_tag_(w, cid, index, cid);
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
	int cid = check_cid(L, w, 2);
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
	for (i = 0; entity_iter_(w, mainkey, i); i++) {
		for (j = 1; j < iter->nkey; j++) {
			struct group_key *k = &iter->k[j];
			if ((entity_sibling_index_(w, mainkey, i, k->id) != 0) ^ (!(k->attrib & COMPONENT_ABSENT))) {
				break;
			}
		}
		if (j == iter->nkey)
			entity_enable_tag_(w, mainkey, i, tagid);
	}
	return 0;
}

int
laccess(lua_State *L) {
	struct entity_world *w = getW(L);
	uint64_t eid = (uint64_t)luaL_checkinteger(L, 2);
	int idx = find_eid(w, eid);
	if (idx < 0)
		return luaL_error(L, "eid %x not found", idx);
	struct group_iter *iter = lua_touserdata(L, 3);
	int value_index = 4;
	if (iter->nkey > 1)
		return luaL_error(L, "More than one key in pattern");
	if (iter->world != w)
		return luaL_error(L, "World mismatch");

	int output = (lua_gettop(L) >= value_index);
	int mainkey = ENTITYID_TAG;
	struct group_key *k = &iter->k[0];

	struct component_pool *c = &w->c[k->id];
	if (c->stride == STRIDE_TAG) {
		// It is a tag
		if (output) {
			if (lua_toboolean(L, value_index)) {
				entity_enable_tag_(w, mainkey, idx, k->id);
			} else {
				entity_disable_tag_(w, mainkey, idx, k->id);
			}
			return 0;
		} else {
			lua_pushboolean(L, entity_sibling_index_(w, mainkey, idx, k->id) != 0);
			return 1;
		}
	}

	unsigned int index = entity_sibling_index_(w, mainkey, idx, k->id);
	if (index == 0) {
		if (output)
			return luaL_error(L, "No component .%s", k->name);
		else
			return 0;
	}
	if (c->stride == STRIDE_LUA) {
		// It is lua component
		lua_settop(L, value_index);
		int world_index = 1;
		if (lua_getiuservalue(L, world_index, k->id) != LUA_TTABLE) {
			luaL_error(L, "Missing lua table for .%s", k->name);
		}
		if (output) {
			lua_pushvalue(L, value_index);
			lua_rawseti(L, -2, index);
			return 0;
		} else {
			lua_rawgeti(L, -1, index);
			return 1;
		}
	}

	// It is C component
	void *buffer = get_ptr(c, index - 1);
	if (output) {
		ecs_write_component_object_(L, k->field_n, iter->f, buffer);
		return 0;
	} else {
		ecs_read_object_(L, iter, buffer);
		return 1;
	}
}


static int
lmethods(lua_State *L) {
	luaL_Reg m[] = {
		{ "memory", lcount_memory },
		{ "collect", lcollect_memory },
		{ "_newtype", lnew_type },
		{ "_newentity", lnew_entity },
		{ "_addcomponent", ladd_component },
		{ "update", lupdate },
		{ "_clear", lclear_type },
		{ "_context", lcontext },
		{ "_groupiter", lgroupiter },
		{ "exist", lexist },
		{ "remove", lremove },
		{ "_object", lobject },
		{ "_sync", lsync },
		{ "_read", lread },
		{ "_dumpid", ldumpid },
		{ "_readid", lreadid },
		{ "_count", lcount },
		{ "_filter", lfilter },
		{ "_access", laccess },
		{ "__gc", ldeinit_world },
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
		{ "_group_methods", lgroup_methods },
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

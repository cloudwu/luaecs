#define LUA_LIB

#include <lua.h>
#include <lauxlib.h>

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "luaecs.h"

#define MAX_COMPONENT 256
#define ENTITY_REMOVED 0
#define DEFAULT_SIZE 128
#define DUMMY_PTR (void *)(uintptr_t)(~0)

struct component_pool {
	int cap;
	int n;
	int count;
	int stride;
	int last_lookup;
	unsigned int *id;
	void *buffer;
};

struct entity_world {
	unsigned int max_id;
	unsigned int wrap_begin;
	int wrap;
	struct component_pool c[MAX_COMPONENT];
};

static void
init_component_pool(struct entity_world *w, int index, int stride, int opt_size) {
	struct component_pool *c = &w->c[index];
	c->cap = opt_size;
	c->n = 0;
	c->count = 0;
	c->stride = stride;
	c->id = NULL;
	if (stride > 0) {
		c->buffer = NULL;
	} else {
		c->buffer = DUMMY_PTR;
	}
}

static void
entity_new_type(lua_State *L, struct entity_world *w, int cid, int stride, int opt_size) {
	if (opt_size <= 0) {
		opt_size = DEFAULT_SIZE;
	}
	if (cid < 0 || cid >=MAX_COMPONENT || w->c[cid].cap != 0) {
		luaL_error(L, "Can't new type %d", cid);
	}
	init_component_pool(w, cid, stride, opt_size);
}

static inline struct entity_world *
getW(lua_State *L) {
	return (struct entity_world *)luaL_checkudata(L, 1, "ENTITY_WORLD");
}

static int
lnew_type(lua_State *L) {
	struct entity_world *w = getW(L);
	int cid = luaL_checkinteger(L, 2);
	int stride = luaL_checkinteger(L, 3);
	int size = luaL_optinteger(L, 4, 0);
	entity_new_type(L, w, cid, stride, size);
	return 0;
}

static int
lcount_memory(lua_State *L) {
	struct entity_world *w = getW(L);
	size_t sz = sizeof(*w);
	int i;
	size_t msz = sz;
	for (i=0;i<MAX_COMPONENT;i++) {
		struct component_pool *c = &w->c[i];
		if (c->id) {
			sz += c->cap * sizeof(unsigned int);
		}
		if (c->buffer) {
			sz += c->cap * c->stride;
		}

		msz += c->n * (sizeof(unsigned int) + c->stride);
	}
	lua_pushinteger(L, sz);
	lua_pushinteger(L, msz);
	return 2;
}

static void
shrink_component_pool(lua_State *L, struct component_pool *c, int id) {
	if (c->id == NULL)
		return;
	if (c->n == 0) {
		c->id = NULL;
		if (c->stride > 0)
			c->buffer = NULL;
		lua_pushnil(L);
		lua_setiuservalue(L, 1, id * 2 + 1);
		lua_pushnil(L);
		lua_setiuservalue(L, 1, id * 2 + 2);
	} if (c->n < c->cap) {
		c->cap = c->n;
		c->id = (unsigned int *)lua_newuserdatauv(L, c->n * sizeof(unsigned int), 0);
		lua_setiuservalue(L, 1, id * 2 + 1);
		if (c->stride > 0) {
			c->buffer = lua_newuserdatauv(L, c->n * c->stride, 0);
			lua_setiuservalue(L, 1, id * 2 + 2);
		}
	}
}

static int
lcollect_memory(lua_State *L) {
	struct entity_world *w = getW(L);
	int i;
	for (i=0;i<MAX_COMPONENT;i++) {
		shrink_component_pool(L, &w->c[i], i);
	}
	return 0;
}

static void
add_component_(lua_State *L, struct entity_world *w, int cid, unsigned int eid, const void *buffer) {
	struct component_pool *pool = &w->c[cid];
	int cap = pool->cap;
	int index = pool->n;
	int stride = pool->stride;
	if (pool->n == 0) {
		if (pool->id == NULL) {
			pool->id = (unsigned int *)lua_newuserdatauv(L, cap * sizeof(unsigned int), 0);
			lua_setiuservalue(L, 1, cid * 2 + 1);
		}
		if (pool->buffer == NULL) {
			pool->buffer = lua_newuserdatauv(L, cap *stride, 0);
			lua_setiuservalue(L, 1, cid * 2 + 2);
		}
	} else {
		if (pool->n >= pool->cap) {
			// expand pool
			int newcap = cap * 3 / 2;
			unsigned int *newid = (unsigned int *)lua_newuserdatauv(L, newcap * sizeof(unsigned int), 0);
			lua_setiuservalue(L, 1, cid * 2 + 1);
			memcpy(newid, pool->id,  cap * sizeof(unsigned int));
			pool->id = newid;
			if (stride > 0) {
				void *newbuffer = lua_newuserdatauv(L, newcap * stride, 0);
				lua_setiuservalue(L, 1, cid * 2 + 2);
				memcpy(newbuffer, pool->buffer, cap * stride);
				pool->buffer = newbuffer;
			}
			pool->cap = newcap;
		}
	}
	++pool->n;
	pool->id[index] = eid;
	memcpy((char *)pool->buffer + index * stride, buffer, stride);
}

static inline int
check_cid(lua_State *L, struct entity_world *w, int index) {
	int cid = luaL_checkinteger(L, index);
	struct component_pool *c = &w->c[cid];
	if (cid < 0 || 	cid >=MAX_COMPONENT || c->cap == 0) {
		luaL_error(L, "Invalid type %d", cid);
	}
	return cid;
}

static int
ladd_component(lua_State *L) {
	struct entity_world *w = getW(L);
	unsigned int eid = luaL_checkinteger(L, 2);
	int cid = check_cid(L, w, 3);
	size_t sz;
	const char *buffer = lua_tolstring(L, 4, &sz);
	int stride = w->c[cid].stride;
	if ((buffer == NULL && stride > 0) || (sz != stride)) {
		return luaL_error(L, "Invalid data (size=%d/%d) for type %d", (int)sz, stride, cid);
	}
	add_component_(L, w, cid, eid, buffer);
	return 0;
}

static int
lnew_entity(lua_State *L) {
	struct entity_world *w = getW(L);
	unsigned int eid = ++w->max_id;
	if (eid == 0) {
		assert(w->wrap == 0);
		w->wrap = 1;
	}
	lua_pushinteger(L, eid);
	return 1;
}


static void
remove_entity_(struct entity_world *w, int cid, int index, void *L) {
	struct component_pool *c = &w->c[cid];
	if (index < 0 || index >= c->count)
		luaL_error((lua_State *)L, "Invalid index %d/%d", index, c->count);
	unsigned int eid = c->id[index];
	add_component_((lua_State *)L, w, ENTITY_REMOVED, eid, NULL);
}

static int
lremove_entity(lua_State *L) {
	struct entity_world *w = getW(L);
	int cid = check_cid(L, w, 2);
	int index = luaL_checkinteger(L, 3);
	remove_entity_(w, cid, index, (void *)L);
	return 0;
}

static int
id_comp(const void *a , const void *b) {
    const unsigned int ai = *( const unsigned int* )a;
    const unsigned int bi = *( const unsigned int* )b;

    if( ai < bi )
        return -1;
    else if( ai > bi )
        return 1;
    else
        return 0;
}

static int
lsort_removed(lua_State *L) {
	struct entity_world *w = getW(L);
	struct component_pool *pool = &w->c[ENTITY_REMOVED];
	if (pool->n == 0)
		return 0;
	qsort(pool->id, pool->n, sizeof(unsigned int), id_comp);
	pool->count = pool->n;
	lua_pushinteger(L, pool->n);
	return 1;
}

static int
binary_search(unsigned int *a, int from, int to, unsigned int v) {
	while(from < to) {
		int mid = (from + to)/2;
		int aa = a[mid];
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
lookup_component(struct component_pool *pool, unsigned int eid, int guess_index) {
	int n = pool->count;
	if (n == 0)
		return -1;
	if (guess_index + GUESS_RANGE >= n)
		return binary_search(pool->id, 0, pool->count, eid);
	unsigned int *a = pool->id;
	int higher = a[guess_index + GUESS_RANGE];
	if (eid > higher) {
		return binary_search(a, guess_index + GUESS_RANGE + 1, pool->count, eid);
	}
	int lower = a[guess_index];
	if (eid < lower) {
		return binary_search(a, 0, guess_index, eid);
	}
	return binary_search(a, guess_index, guess_index + GUESS_RANGE, eid);
}

struct rearrange_context {
	struct entity_world *w;
	unsigned int ptr[MAX_COMPONENT-1];
};

static int
find_min(struct rearrange_context *ctx) {
	unsigned int m = ~0;
	int i;
	int r = -1;
	struct entity_world *w = ctx->w;
	for (i=1;i<MAX_COMPONENT;i++) {
		int index = ctx->ptr[i-1];
		if (index < w->c[i].count) {
			if (w->c[i].id[index] <= m) {
				m = w->c[i].id[index];
				r = i;
			}
		}
	}
	return r;
}

static void
rearrange(struct entity_world *w) {
	struct rearrange_context ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.w = w;
	int cid;
	unsigned int new_id = 1;
	unsigned int last_id = 0;
	while ((cid = find_min(&ctx)) >= 0) {
		int index = ctx.ptr[cid-1];
		unsigned int current_id = w->c[cid].id[index];
//		printf("arrange %d -> %d\n", new_id, w->c[cid].id[index]);
		w->c[cid].id[index] = new_id;
		if (current_id != last_id) {
			++new_id;
			last_id = current_id;
		}
		++ctx.ptr[cid-1];
	}
	int i,j;
	for (i=1;i<MAX_COMPONENT;i++) {
		struct component_pool *pool = &w->c[i];
		for (j=pool->count;j<pool->n;j++) {
//			printf("arrange new %d -> %d\n", pool->id[j], new_id + pool->id[j] - w->wrap_begin -1);
			pool->id[j] = new_id + pool->id[j] - w->wrap_begin -1;
		}
	}
	w->max_id = new_id + w->max_id - w->wrap_begin - 1;
}

static inline void
move_item(struct component_pool *pool, int from, int to) {
	if (from != to) {
		pool->id[to] = pool->id[from];
		memcpy((char *)pool->buffer + to * pool->stride, (char *)pool->buffer + from * pool->stride, pool->stride);
	}
}

static void
remove_all(struct component_pool *pool, struct component_pool *removed) {
	int index = 0;
	int i;
	unsigned int *id = removed->id;
	unsigned int last_id = 0;
	int count = 0;
	for (i=0;i<removed->n;i++) {
		if (id[i] != last_id) {
			int r = lookup_component(pool, id[i], index);
			if (r >= 0) {
				index = r;
				assert(pool->id[r] == id[i]);
				pool->id[r] = 0;
				++count;
			}
		}
	}
	if (count > 0) {
		index = 0;
		for (i=0;i<pool->n;i++) {
			if (pool->id[i] != 0) {
				move_item(pool, i, index);
				++index;
			}
		}
		pool->n -= count;
		pool->count -= count;
	}
}

static int
lupdate(lua_State *L) {
	struct entity_world *w = getW(L);
	struct component_pool *removed = &w->c[ENTITY_REMOVED];
	int i;
	if (removed->n > 0) {
		// mark removed
		assert(ENTITY_REMOVED == 0);
		for (i=1;i<MAX_COMPONENT;i++) {
			struct component_pool *pool = &w->c[i];
			if (pool->n > 0)
				remove_all(pool, removed);
		}
		removed->n = 0;
		removed->count = 0;
	}

	if (w->wrap) {
		rearrange(w);
		w->wrap = 0;
	}
	w->wrap_begin = w->max_id;
	// add componets
	for (i=1;i<MAX_COMPONENT;i++) {
		struct component_pool *c = &w->c[i];
		c->count = c->n;
	}
	return 0;
}

static void *
entity_iter_(struct entity_world *w, int cid, int index) {
	struct component_pool *c = &w->c[cid];
	assert(index >= 0);
	if (index >= c->count)
		return NULL;
	return (char *)c->buffer + c->stride * index;
}

static void
entity_clear_type_(struct entity_world *w, int cid) {
	struct component_pool *c = &w->c[cid];
	c->n = 0;
	c->count = 0;
}

static int
lclear_type(lua_State *L) {
	struct entity_world *w = getW(L);
	int cid = check_cid(L,w, 2);
	entity_clear_type_(w, cid);
	return 0;
}

static void *
entity_sibling_(struct entity_world *w, int cid, int index, int slibling_id) {
	struct component_pool *c = &w->c[cid];
	if (index < 0 || index >= c->count)
		return NULL;
	unsigned int eid = c->id[index];
	c = &w->c[slibling_id];
	int result_index = lookup_component(c, eid, c->last_lookup);
	if (result_index >= 0) {
		c->last_lookup = result_index;
		return (char *)c->buffer + c->stride * result_index;
	}
	return NULL;
}

static void
entity_add_sibling_(struct entity_world *w, int cid, int index, int slibling_id, const void *buffer, void *L) {
	struct component_pool *c = &w->c[cid];
	assert(index >=0 && index < c->count);
	unsigned int eid = c->id[index];
	// todo: pcall add_component_
	add_component_((lua_State *)L, w, slibling_id, eid, buffer);
	c->count = c->n;
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
	lua_setiuservalue(L, -2, 1);
	ctx->max_id = n;
	ctx->world = w;
	static struct ecs_capi c_api = {
		entity_iter_,
		entity_clear_type_,
		entity_sibling_,
		entity_add_sibling_,
	};
	ctx->api = &c_api;
	ctx->cid[0] = ENTITY_REMOVED;
	int i;
	for (i=1;i<=n;i++) {
		if (lua_geti(L, 2, i) != LUA_TNUMBER) {
			return luaL_error(L, "Invalid id at index %d", i);
		}
		ctx->cid[i] = lua_tointeger(L, -1);
		lua_pop(L, 1);
		int cid = ctx->cid[i];
		if (cid == ENTITY_REMOVED || cid < 0 || cid >= MAX_COMPONENT)
			return luaL_error(L, "Invalid id (%d) at index %d", cid, i);
	}
	return 1;
}

static int
lnew_world(lua_State *L) {
	size_t sz = sizeof(struct entity_world);
	struct entity_world *w = (struct entity_world *)lua_newuserdatauv(L, sz, MAX_COMPONENT * 2);
	memset(w, 0, sz);
	// removed set
	entity_new_type(L, w, ENTITY_REMOVED, 0, 0);
	luaL_getmetatable(L, "ENTITY_WORLD");
	lua_setmetatable(L, -2);
	return 1;
}

#define TYPE_INT 0
#define TYPE_FLOAT 1
#define TYPE_BOOL 2

struct field {
	const char *key;
	int offset;
	int type;
};

struct simple_iter {
	int id;
	int field_n;
	struct entity_world *world;
	struct field f[1];
};

static void
get_field(lua_State *L, int index, int i, struct field *f) {
	if (lua_geti(L, index, i) != LUA_TTABLE) {
		luaL_error(L, "Invalid field %d", i);
	}

	if (lua_geti(L, -1, 1) != LUA_TNUMBER) {
		luaL_error(L, "Invalid field %d [1] type", i);
	}
	f->type = lua_tointeger(L, -1);
	if (f->type != TYPE_INT &&
		f->type != TYPE_FLOAT &&
		f->type != TYPE_BOOL) {
		luaL_error(L, "Invalid field %d [1] type(%d)", i, f->type);
	}
	lua_pop(L, 1);

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
write_component(lua_State *L, struct simple_iter *iter, int index, char *buffer) {
	int i;
	for (i=0; i < iter->field_n; i++) {
		int luat = lua_getfield(L, index, iter->f[i].key);
		char *ptr = buffer + iter->f[i].offset;
		switch (iter->f[i].type) {
		case TYPE_INT:
			if (!lua_isinteger(L, -1))
				luaL_error(L, "Invalid .%s type %s (int)", iter->f[i].key, lua_typename(L, luat));
			*(int *)ptr = lua_tointeger(L, -1);
			break;
		case TYPE_FLOAT:
			if (luat != LUA_TNUMBER)
				luaL_error(L, "Invalid .%s type %s (float)", iter->f[i].key, lua_typename(L, luat));
			*(float *)ptr = lua_tonumber(L, -1);
			break;
		case TYPE_BOOL:
			if (luat != LUA_TBOOLEAN)
				luaL_error(L, "Invalid .%s type %s (bool)", iter->f[i].key, lua_typename(L, luat));
			*(unsigned char *)ptr = lua_toboolean(L, -1);
		}
		lua_pop(L, 1);
	}
}

static void
read_component(lua_State *L, struct simple_iter *iter, int index, const char * buffer) {
	int i;
	for (i=0; i < iter->field_n; i++) {
		const char * ptr = buffer + iter->f[i].offset;
		switch (iter->f[i].type) {
		case TYPE_INT:
			lua_pushinteger(L, *(const int *)ptr);
			break;
		case TYPE_FLOAT:
			lua_pushnumber(L, *(const float *)ptr);
			break;
		case TYPE_BOOL:
			lua_pushboolean(L, *ptr);
			break;
		default:
			// never here
			lua_pushnil(L);
			break;
		}
		lua_setfield(L, index, iter->f[i].key);
	}
}

static int
leach_simple(lua_State *L) {
	struct simple_iter *iter = lua_touserdata(L, 1); 
	if (lua_rawgeti(L, 2, 1) != LUA_TNUMBER) {
		return luaL_error(L, "Invalid simple iterator");
	}
	int i = lua_tointeger(L, -1);
	lua_pop(L, 1);
	if (i > 0) {
		void * write_buffer = entity_iter_(iter->world, iter->id, i-1);
		if (write_buffer == NULL)
			return luaL_error(L, "Can't write to index %d", i);
		write_component(L, iter, 2, (char *)write_buffer);
	}
	void * read_buffer = entity_iter_(iter->world, iter->id, i++);
	if (read_buffer == NULL) {
		return 0;
	}
	lua_pushinteger(L, i);
	lua_rawseti(L, 2, 1);
	read_component(L, iter, 2, (const char *)read_buffer);
	lua_settop(L, 2);
	return 1;
}

static int
lpairs_simple(lua_State *L) {
	struct simple_iter *iter = lua_touserdata(L, 1); 
	lua_pushcfunction(L, leach_simple);
	lua_pushvalue(L, 1);
	lua_createtable(L, 1, iter->field_n);
	lua_pushinteger(L, 0);
	lua_rawseti(L, -2, 1);
	return 3;		
}

static int
lsimpleiter(lua_State *L) {
	struct entity_world *w = getW(L);
	luaL_checktype(L, 2, LUA_TTABLE);
	lua_len(L, 2);
	if (lua_type(L, -1) != LUA_TNUMBER) {
		return luaL_error(L, "Invalid fields");
	}
	int n = lua_tointeger(L, -1);
	if (n <= 0) {
		return luaL_error(L, "Invalid fields number %d", n);
	}
	lua_pop(L, 1);
	size_t sz = sizeof(struct simple_iter) + (n-1) * sizeof(struct field);
	struct simple_iter * iter = (struct simple_iter *)lua_newuserdatauv(L, sz, 1);
	lua_pushvalue(L, 1);
	lua_setiuservalue(L, -2, 1);
	iter->world = w;
	iter->field_n = n;
	if (lua_getfield(L, 2, "id") != LUA_TNUMBER) {
		return luaL_error(L, "Invalid id");
	}
	iter->id = lua_tointeger(L, -1);
	lua_pop(L, 1);
	if (iter->id < 0 || iter->id >= MAX_COMPONENT || iter->id == ENTITY_REMOVED || w->c[iter->id].cap == 0) {
		return luaL_error(L, "Invalid id %d", iter->id);
	}
	int i;
	for (i=0;i<n;i++) {
		get_field(L, 2, i+1, &iter->f[i]);
	}
	if (luaL_newmetatable(L, "ENTITY_SIMPLEITER")) {
		lua_pushcfunction(L, lpairs_simple);
		lua_setfield(L, -2, "__call");
	}
	lua_setmetatable(L, -2);
	return 1;
}

LUAMOD_API int
luaopen_ecs_core(lua_State *L) {
	luaL_checkversion(L);
	luaL_Reg l[] = {
		{ "world", lnew_world },
		{ "_MAXTYPE", NULL },
		{ "_METHODS", NULL },
		{ "_TYPEINT", NULL },
		{ "_TYPEFLOAT", NULL },
		{ "_TYPEBOOL", NULL },
		{ NULL, NULL },
	};
	luaL_newlib(L,l);
	lua_pushinteger(L, MAX_COMPONENT-1);
	lua_setfield(L, -2, "_MAXTYPE");
	if (luaL_newmetatable(L, "ENTITY_WORLD")) {
		luaL_Reg l[] = {
			{ "__index", NULL },
			{ "memory", lcount_memory },
			{ "collect", lcollect_memory },
			{ "_newtype",lnew_type },
			{ "_newentity", lnew_entity },
			{ "_addcomponent", ladd_component },
			{ "remove", lremove_entity },
			{ "sort_removed", lsort_removed },
			{ "update", lupdate },
			{ "clear", lclear_type },
			{ "_context", lcontext },
			{ "_simpleiter", lsimpleiter },
			{ NULL, NULL },
		};
		luaL_setfuncs(L,l,0);
		lua_pushvalue(L, -1);
		lua_setfield(L, -2, "__index");
	} else {
		return luaL_error(L, "ENTITY_WORLD exist");
	}
	lua_setfield(L, -2, "_METHODS");
	lua_pushinteger(L, TYPE_INT);
	lua_setfield(L, -2, "_TYPEINT");
	lua_pushinteger(L, TYPE_FLOAT);
	lua_setfield(L, -2, "_TYPEFLOAT");
	lua_pushinteger(L, TYPE_BOOL);
	lua_setfield(L, -2, "_TYPEBOOL");
	return 1;
}

#ifdef TEST_LUAECS

#include <stdio.h>

#define COMPONENT_VECTOR2 1
#define TAG_MARK 2
#define COMPONENT_ID 3

struct vector2 {
	float x;
	float y;
};

struct id {
	int v;
};

static int
ltest(lua_State *L) {
	struct ecs_context *ctx = lua_touserdata(L, 1);
	struct vector2 *v;
	int i;
	for (i=0;(v=(struct vector2 *)entity_iter(ctx, COMPONENT_VECTOR2, i));i++) {
		printf("vector2 %d: x=%f y=%f\n", i, v->x, v->y);
		struct id * id = (struct id *)entity_sibling(ctx, COMPONENT_VECTOR2, i, COMPONENT_ID);
		if (id) {
			printf("\tid = %d\n", id->v);
		}
		void * mark = entity_sibling(ctx, COMPONENT_VECTOR2, i, TAG_MARK);
		if (mark) {
			printf("\tMARK\n");
		}
	}

	return 0;
}

static int
lsum(lua_State *L) {
	struct ecs_context *ctx = lua_touserdata(L, 1);
	struct vector2 *v;
	int i;
	float s = 0;
	for (i=0;(v=(struct vector2 *)entity_iter(ctx, COMPONENT_VECTOR2, i));i++) {
		s += v->x + v->y;
	}
	lua_pushnumber(L, s);
	return 1;
}


LUAMOD_API int
luaopen_ecs_ctest(lua_State *L) {
	luaL_checkversion(L);
	luaL_Reg l[] = {
		{ "test", ltest },
		{ "sum", lsum },
		{ NULL, NULL },
	};
	luaL_newlib(L, l);
	return 1;
}

#endif

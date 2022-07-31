#include <lua.h>
#include <lauxlib.h>

#include <stdint.h>

#include "ecs_index.h"
#include "ecs_internal.h"
#include "ecs_capi.h"

struct index_cache {
	struct entity_world *world;
	int type;
	int id;
	int size;
	int64_t key[1];
};

static inline unsigned int *
index_value(struct index_cache *c) {
	return (unsigned int *)(&c->key[c->size]);
}

// https://lemire.me/blog/2018/08/15/fast-strongly-universal-64-bit-hashing-everywhere/
static inline unsigned int
hash64(int64_t x) {
	static int64_t a = 6525041574978960637ull;
	static int64_t b = 4440846264005121539ull;
	static int64_t c = 3850123055445736813ull;
	int low = (unsigned int)x;
	int high = (unsigned int)(x >> 32);
	return (unsigned int)((a * low + b * high + c) >> 32);
}

#define INVALID_INDEX (~0)
#define WORLD_INDEX 1
#define OBJECT_INDEX 2
#define MAX_INDEX 2

static unsigned int
find_key_from(struct index_cache *c, int64_t key_, int n) {
	int i;
	void *buffer_ = c->world->c[c->id].buffer;
	int len = c->world->c[c->id].n - 1;
	if (n > len) {
		n = len;
	}
	switch (c->type) {
	case TYPE_INT: {
		int *buffer = (int *)buffer_;
		int key = (int)key_;
		for (i = n; i >= 0; i--) {
			if (buffer[i] == key)
				return i;
		}
		for (i = len; i > n; i--) {
			if (buffer[i] == key)
				return i;
		}
		break;
	}
	case TYPE_DWORD: {
		unsigned int *buffer = (unsigned int *)buffer_;
		int key = (unsigned int)key_;
		for (i = n; i >= 0; i--) {
			if (buffer[i] == key)
				return i;
		}
		for (i = len; i > n; i--) {
			if (buffer[i] == key)
				return i;
		}
		break;
	}
	case TYPE_INT64: {
		int64_t *buffer = (int64_t *)buffer_;
		int64_t key = (int64_t)key_;
		for (i = n; i >= 0; i--) {
			if (buffer[i] == key)
				return i;
		}
		for (i = len; i > n; i--) {
			if (buffer[i] == key)
				return i;
		}
		break;
	}
	}
	return INVALID_INDEX;
}

static inline unsigned int
find_key(struct index_cache *c, int64_t key) {
	return find_key_from(c, key, c->world->c[c->id].n - 1);
}

static inline unsigned int
check_key(struct index_cache *c, unsigned int index, int64_t key) {
	return find_key_from(c, key, index);
}

static void
make_iterator(lua_State *L, struct index_cache *c, int slot, unsigned int index) {
	lua_createtable(L, 2, 0);
	lua_pushinteger(L, index + 1);
	lua_rawseti(L, -2, 1);
	lua_pushinteger(L, c->id);
	lua_rawseti(L, -2, 2);
	// stack : OBJECT_INDEX iterator
	lua_pushvalue(L, -1);
	lua_rawseti(L, -3, slot + 1);
}

int
ecs_index_cache(lua_State *L) {
	struct index_cache *c = (struct index_cache *)lua_touserdata(L, 1);
	lua_Integer key = luaL_checkinteger(L, 2);
	unsigned int slot = hash64(key) % c->size;
	if (c->key[slot] == key) {
		unsigned int index = index_value(c)[slot];
		if (index != INVALID_INDEX) {
			// get iterator
			lua_getiuservalue(L, 1, OBJECT_INDEX);
			if (lua_rawgeti(L, -1, slot + 1) != LUA_TTABLE) {
				lua_pop(L, 1);
				make_iterator(L, c, slot, index);
			}
			unsigned int real_index = check_key(c, index, key);
			if (real_index != index) {
				index_value(c)[slot] = real_index;
				if (real_index == INVALID_INDEX) {
					// clear iterator
					lua_pushnil(L);
					lua_rawseti(L, -2, 1);
					lua_pushnil(L);
					lua_rawseti(L, -2, 2);

					// remove iterator in OBJECT_INDEX
					lua_pushnil(L);
					lua_rawseti(L, -3, slot + 1);
					return 0;
				}
				// fix iterator
				lua_pushinteger(L, real_index + 1);
				lua_rawseti(L, -2, 1);
			}
			return 1;
		}
	}
	unsigned int index = find_key(c, key);
	if (index != INVALID_INDEX) {
		// UV(OBJECT_INDEX)[slot+1] = iterator { index+1, c->id }
		lua_getiuservalue(L, 1, OBJECT_INDEX);
		make_iterator(L, c, slot, index);
		c->key[slot] = key;
		index_value(c)[slot] = index;
		return 1;
	}
	return 0;
}

int
ecs_index_access(lua_State *L) {
	struct index_cache *cache = (struct index_cache *)lua_touserdata(L, 1);
	lua_Integer key = luaL_checkinteger(L, 2);
	struct group_iter *iter = lua_touserdata(L, 3);
	int value_index = 4;
	unsigned int slot = hash64(key) % cache->size;
	unsigned int cached_index = INVALID_INDEX;
	unsigned int idx;
	if (cache->key[slot] == key) {
		cached_index = index_value(cache)[slot];
		idx = check_key(cache, cached_index, key);
	} else {
		idx = find_key(cache, key);
	}
	if (idx == INVALID_INDEX)
		return luaL_error(L, "Invalid key %p", (void *)key);
	if (idx != cached_index) {
		if (index_value(cache)[slot] != INVALID_INDEX) {
			lua_getiuservalue(L, 1, OBJECT_INDEX);
			if (lua_rawgeti(L, -1, slot + 1) == LUA_TTABLE) {
				lua_pushinteger(L, idx + 1);
				lua_rawseti(L, -2, 1);
			}
			lua_pop(L, 2);
		}
		cache->key[slot] = key;
		index_value(cache)[slot] = idx;
	}

	if (iter->nkey > 1)
		return luaL_error(L, "More than one key in pattern");
	if (iter->world != cache->world)
		return luaL_error(L, "World mismatch");

	int output = (lua_gettop(L) >= value_index);
	int mainkey = cache->id;
	struct group_key *k = &iter->k[0];

	struct component_pool *c = &cache->world->c[k->id];
	if (c->stride == STRIDE_TAG) {
		// It is a tag
		if (output) {
			if (lua_toboolean(L, value_index)) {
				lua_settop(L, value_index);
				lua_getiuservalue(L, 1, WORLD_INDEX);
				int world_index = value_index + 1;
				entity_enable_tag_(cache->world, mainkey, idx, k->id, L, world_index);
			} else {
				entity_disable_tag_(cache->world, mainkey, idx, k->id);
			}
			return 0;
		} else {
			lua_pushboolean(L, entity_sibling_index_(cache->world, mainkey, idx, k->id) != 0);
			return 1;
		}
	}

	unsigned int index = entity_sibling_index_(cache->world, mainkey, idx, k->id);
	if (index == 0) {
		if (output)
			return luaL_error(L, "No component .%s", k->name);
		else
			return 0;
	}
	if (c->stride == STRIDE_LUA) {
		// It is lua component
		lua_settop(L, value_index);
		if (lua_getiuservalue(L, 1, WORLD_INDEX) != LUA_TUSERDATA) {
			luaL_error(L, "Invalid index cache");
		}
		int world_index = value_index + 1;
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

int
ecs_index_make(lua_State *L) {
	struct entity_world *w = getW(L);
	int id = check_cid(L, w, 2);
	int type = luaL_checkinteger(L, 3);
	int size = luaL_checkinteger(L, 4);
	if (size < 1)
		return luaL_error(L, "Invalid index size %d", size);
	luaL_checktype(L, 5, LUA_TTABLE);
	switch (type) {
	case TYPE_INT:
	case TYPE_DWORD:
	case TYPE_INT64:
		break;
	default:
		return luaL_error(L, "Invalid index component type");
	}
	struct index_cache *index = (struct index_cache *)lua_newuserdatauv(L,
		sizeof(struct index_cache) + (size - 1) * sizeof(index->key[0]) + size * sizeof(unsigned int), MAX_INDEX);
	index->world = w;
	index->id = id;
	index->type = type;
	index->size = size;

	int i;
	unsigned int *value = index_value(index);
	for (i = 0; i < size; i++) {
		value[i] = INVALID_INDEX;
	}

	lua_pushvalue(L, 1);
	lua_setiuservalue(L, -2, WORLD_INDEX);
	lua_createtable(L, size, 0);
	lua_setiuservalue(L, -2, OBJECT_INDEX);
	lua_pushvalue(L, 5);
	lua_setmetatable(L, -2);
	return 1;
}

int
lindex_methods(lua_State *L) {
	luaL_Reg m[] = {
		{ "_cache_index", ecs_index_cache },
		{ "_access_index", ecs_index_access },
		{ "_make_index", ecs_index_make },
		{ NULL, NULL },
	};
	luaL_newlib(L, m);

	return 1;
}
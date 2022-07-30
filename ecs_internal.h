#ifndef LUA_ECS_INTERNAL_H
#define LUA_ECS_INTERNAL_H

#include <lua.h>
#include <lauxlib.h>

typedef unsigned int entity_index_t;
#define MAX_ENTITYID ((entity_index_t)0xffffffff)
#define REARRANGE_THRESHOLD ((entity_index_t)0x80000000)

#define MAX_COMPONENT 256
#define ENTITY_REMOVED 0
#define DEFAULT_SIZE 128
#define STRIDE_TAG 0
#define STRIDE_LUA -1
#define DUMMY_PTR (void *)(~(uintptr_t)0)

#define TYPE_INT 0
#define TYPE_FLOAT 1
#define TYPE_BOOL 2
#define TYPE_INT64 3
#define TYPE_DWORD 4
#define TYPE_WORD 5
#define TYPE_BYTE 6
#define TYPE_DOUBLE 7
#define TYPE_USERDATA 8
#define TYPE_COUNT 9

typedef unsigned short component_id_t;

struct component_pool {
	int cap;
	int n;
	int stride; // -1 means lua object
	int last_lookup;
	entity_index_t *id;
	void *buffer;
};

struct entity_id {
	uint32_t n;
	uint32_t cap;
	uint64_t last_id;
	uint64_t *id;
};

struct entity_world {
	struct entity_id eid;
	entity_index_t max_id;
	struct component_pool c[MAX_COMPONENT];
};

struct group_field {
	const char *key;
	int offset;
	int type;
};

struct group_key {
	const char *name;
	int id;
	int field_n;
	int attrib;
};

struct group_iter {
	struct entity_world *world;
	struct group_field *f;
	int nkey;
	int readonly;
	struct group_key k[1];
};

static inline struct entity_world *
getW(lua_State *L) {
	return (struct entity_world *)lua_touserdata(L, 1);
}

static inline void
check_cid_valid(lua_State *L, struct entity_world *w, int cid) {
	if (cid < 0 || cid >= MAX_COMPONENT || w->c[cid].cap == 0) {
		luaL_error(L, "Invalid type %d", cid);
	}
}

static inline int
check_cid(lua_State *L, struct entity_world *w, int index) {
	int cid = luaL_checkinteger(L, index);
	check_cid_valid(L, w, cid);
	return cid;
}

static inline void *
get_ptr(struct component_pool *c, int index) {
	if (c->stride > 0)
		return (void *)((char *)c->buffer + c->stride * index);
	else
		return DUMMY_PTR;
}

static inline int
get_integer(lua_State *L, int index, int i, const char *key) {
	if (lua_rawgeti(L, index, i) != LUA_TNUMBER) {
		return luaL_error(L, "Can't find %s in iterator", key);
	}
	int r = lua_tointeger(L, -1);
	lua_pop(L, 1);
	if (r < 0)
		return luaL_error(L, "Invalid %s (%d)", key, r);
	return r;
}

int ecs_add_component_id_(lua_State *L, int world_index, struct entity_world *w, int cid, entity_index_t eid);
int ecs_add_component_id_nocheck_(lua_State *L, int world_index, struct entity_world *w, int cid, entity_index_t eid);
void ecs_write_component_object_(lua_State *L, int n, struct group_field *f, void *buffer);
void ecs_read_object_(lua_State *L, struct group_iter *iter, void *buffer);
int ecs_lookup_component_(struct component_pool *pool, entity_index_t eid, int guess_index);

#endif

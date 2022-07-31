#ifndef LUA_ECS_INTERNAL_H
#define LUA_ECS_INTERNAL_H

#include <lua.h>
#include <lauxlib.h>
#include <string.h>

typedef struct { uint8_t idx[3]; } entity_index_t;

static const entity_index_t INVALID_ENTITY = { { 0xff, 0xff, 0xff } };

#define MAX_ENTITY 0xffffff 
#define ENTITY_INIT_SIZE 4096
#define ENTITY_ID_LOOKUP 8191

#define MAX_COMPONENT 256
#define ENTITY_REMOVED 0
#define ENTITYID_TAG -1
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
	entity_index_t lookup[ENTITY_ID_LOOKUP];
};

struct entity_world {
	struct entity_id eid;
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

static inline uint32_t
index_(entity_index_t x) {
	return (uint32_t) x.idx[0] << 16 | (uint32_t) x.idx[1] << 8 | x.idx[2]; 
}

static inline entity_index_t
make_index_(uint32_t v) {
	entity_index_t r = {{ (v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff }};
	return r;
}

static inline int
INVALID_ENTITY_INDEX(entity_index_t e) {
	return index_(e) == MAX_ENTITY;
}

static inline int
ENTITY_INDEX_CMP(entity_index_t a, entity_index_t b) {
	return memcmp(&a, &b, sizeof(a));
} 

static inline entity_index_t
DEC_ENTITY_INDEX(entity_index_t e, int delta) {
	return make_index_(index_(e) - delta);
}

static inline uint64_t
ENTITY_EID(struct entity_world *w, entity_index_t e) {
	return w->eid.id[index_(e)];
}

static inline uint64_t
ecs_get_eid(struct entity_world *w, int cid, int index) {
	struct component_pool *c = &w->c[cid];
	return w->eid.id[index_(c->id[index])];
}

int ecs_add_component_id_(struct entity_world *w, int cid, entity_index_t eindex);
void ecs_write_component_object_(lua_State *L, int n, struct group_field *f, void *buffer);
void ecs_read_object_(lua_State *L, struct group_iter *iter, void *buffer);
int ecs_lookup_component_(struct component_pool *pool, entity_index_t eindex, int guess_index);
entity_index_t ecs_new_entityid_(struct entity_world *w); 

#endif

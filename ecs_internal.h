#ifndef LUA_ECS_INTERNAL_H
#define LUA_ECS_INTERNAL_H

#include <lua.h>
#include <lauxlib.h>
#include <string.h>
#include <stdint.h>

#include "ecs_group.h"
#include "ecs_entityindex.h"
#include "ecs_entityid.h"

#define MAX_COMPONENT_NAME 32
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

struct component_lua {
	lua_State *L;	// for lua object table
	unsigned int freelist;
	unsigned int cap;
};

struct entity_world {
	struct component_lua lua;
	struct entity_id eid;
	struct entity_group_arena group;
	struct component_pool c[MAX_COMPONENT];
};

struct group_field {
	char key[MAX_COMPONENT_NAME];
	int offset;
	int type;
};

struct group_key {
	char name[MAX_COMPONENT_NAME];
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

static inline int
check_tagid(lua_State *L, struct entity_world *w, int index) {
	int cid = luaL_checkinteger(L, index);
	check_cid_valid(L, w, cid);
	if (w->c[cid].stride != 0)
		luaL_error(L, "Invalid tag %d", cid);
	return cid;
}

static inline struct group_iter *
check_groupiter(lua_State *L, int index) {
	struct group_iter *iter = lua_touserdata(L, index);
	if (iter == NULL)
		luaL_error(L, "Need Userdata Iterator");
	return iter;
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
	return r;
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
void ecs_reserve_component_(struct component_pool *pool, int cid, int cap);
void ecs_reserve_eid_(struct entity_world *w, int n);

#endif

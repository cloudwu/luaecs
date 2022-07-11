#include <lua.h>
#include <lauxlib.h>

#include <stdint.h>
#include <assert.h>

#include "ecs_internal.h"
#include "ecs_capi.h"

struct group {
	uint64_t uid;
	uint64_t lastid;
	uint32_t group;
	uint32_t next;
};

static inline int
find_groupid(struct component_pool *g, int index, int groupid) {
	if (index >= g->n) {
		index = g->n - 1;
	}
	if (index < 0)
		return -1;
	struct group *group = (struct group *)g->buffer;
	while (group[index].group != groupid) {
		--index;
		if (index < 0)
			return -1;
	}
	return index;
}

static inline int
read_group(lua_State *L, struct component_pool *g, int index, int groupid) {
	if (lua_rawgeti(L, index, groupid) == LUA_TNIL) {
		lua_pop(L, 1);
		return -1;
	}
	if (lua_isinteger(L, -1)) {
		int r = lua_tointeger(L, -1);
		lua_pop(L, 1);
		int realid = find_groupid(g, r, groupid);
		if (realid != r) {
			r = realid;
			lua_pushinteger(L, r);
			lua_rawseti(L, index, groupid);
		}
		return r;
	}
	return luaL_error(L, "Invalid group");
}

static inline void
write_group(lua_State *L, int index, int groupid, int n) {
	lua_pushinteger(L, n);
	lua_rawseti(L, index, groupid);
}

// 1: world
// 2: group table
// 3: groupid component id
// 4: groupstruct component id
// 5: uint64 uid
int
ecs_group_update(lua_State *L) {
	struct entity_world *w = getW(L);
	int gid = check_cid(L, w, 3);

	struct component_pool *c = &w->c[gid];
	if (c->n == 0) {
		lua_settop(L, 5); // returns uid
		return 1; // no new group id
	}
	if (c->stride != sizeof(uint32_t)) {
		return luaL_error(L, "Invalid group id componet");
	}
	uint32_t *group = (uint32_t *)c->buffer;

	luaL_checktype(L, 2, LUA_TTABLE);
	int sid = check_cid(L, w, 4);
	uint64_t uid = (uint64_t)luaL_checkinteger(L, 5);

	struct component_pool *g = &w->c[sid];
	if (g->stride != sizeof(struct group))
		return luaL_error(L, "Invalid group struct");

	int i;
	for (i = 0; i < c->n; i++) {
		// insert group
		int index = read_group(L, g, 2, group[i]);
		//		printf("Group %d group = %d index = %d\n", i, group[i], index);
		int n = ecs_add_component_id_(L, 1, w, sid, c->id[i]);
		struct group *gs = (struct group *)get_ptr(g, n);
		gs->uid = ++uid;
		gs->group = group[i];
		if (index < 0) {
			gs->lastid = 0;
			gs->next = 0;
		} else {
			struct group *last = (struct group *)get_ptr(g, index);
			gs->lastid = last->uid;
			gs->next = n - index;
			assert(gs->next > 0);
		}
		write_group(L, 2, group[i], n);
	}

	// clear groupid component
	c->n = 0;

	lua_pushinteger(L, uid + c->n);

	return 1;
}

// 1: world
// 2: iterator
// 2: groupstruct component id
int
ecs_group_id(lua_State *L) {
	struct entity_world *w = getW(L);
	luaL_checktype(L, 2, LUA_TTABLE);
	int idx = get_integer(L, 2, 1, "index") - 1;
	int mainkey = get_integer(L, 2, 2, "mainkey");
	int cid = luaL_checkinteger(L, 3);
	int index = entity_sibling_index_(w, mainkey, idx, cid);
	if (index <= 0) {
		return luaL_error(L, "Invalid iterator");
	}
	struct component_pool *c = &w->c[cid];
	struct group *g = (struct group *)get_ptr(c, index - 1);
	lua_pushinteger(L, g->group);
	lua_pushinteger(L, g->uid);
	return 2;
}

static inline int
next_groupid(struct group *group, int index) {
	if (group[index].next == 0)
		return -1;
	int nextindex = index - group[index].next;
	if (nextindex < 0)
		nextindex = 0;
	uint64_t lastid = group[index].lastid;
	uint32_t groupid = group[index].group;
	struct group *n = &group[nextindex];
	if (lastid == n->uid)
		return nextindex;
	// case 1: lastid is removed (entity with the same group is removed)
	// case 2: lastid is exist, but position is changed (entity between lastid and current id is removed)
	int i;
	if (lastid > n->uid) {
		int possible_index = -1;
		// lookup forward for lastid
		for (i = nextindex + 1; i < index; i++) {
			if (group[i].uid == lastid) {
				// case 2, found lastid
				group[index].next = index - i;
				return i;
			}
			if (group[i].group == groupid) {
				possible_index = i;
			}
			if (group[i].uid > lastid && possible_index >= 0)
				break;
		}
		if (possible_index >= 0) {
			// case 1: fix the linklist
			group[index].lastid = group[possible_index].uid;
			group[index].next = index - possible_index;
			return possible_index;
		}
	}
	// lookup backward
	int possible_index = -1;
	for (i = nextindex; i >= 0; i--) {
		if (group[i].uid == lastid) {
			group[index].next = index - i;
			return i;
		}
		if (group[i].group == groupid) {
			possible_index = i;
		}
		if (group[i].uid < lastid && possible_index >= 0) {
			break;
		}
	}
	if (possible_index >= 0) {
		group[index].lastid = group[possible_index].uid;
		group[index].next = index - possible_index;
		return possible_index;
	} else {
		group[index].next = 0;
	}
	return -1;
}

// debug use
// 1: world
// 2: group table
// 3: groupstruct component id
// 4: group id
// 5: check
int
ecs_group_fetch(lua_State *L) {
	struct entity_world *w = getW(L);
	luaL_checktype(L, 2, LUA_TTABLE);
	int sid = check_cid(L, w, 3);
	struct component_pool *g = &w->c[sid];
	struct group *group = (struct group *)g->buffer;
	int groupid = luaL_checkinteger(L, 4);
	lua_newtable(L);
	int n = 1;
	if (lua_toboolean(L, 5)) {
		int i;
		for (i = 0; i < g->n; i++) {
			if (group[i].group == groupid) {
				lua_pushinteger(L, group[i].uid);
				lua_rawseti(L, -2, n++);
			}
		}
	} else {
		int index = read_group(L, g, 2, groupid);
		while (index >= 0) {
			lua_pushinteger(L, group[index].uid);
			lua_rawseti(L, -2, n++);
			index = next_groupid(group, index);
		}
	}
	return 1;
}

#define MAX_GROUPS 256

struct group_enable {
	int groupid;
	int index;
};

static void
group_enable_insert(struct group_enable *array, int n, int groupid, int index) {
	int i;
	for (i = n - 1; i >= 0; i--) {
		if (index > array[i].index) {
			array[i + 1].groupid = groupid;
			array[i + 1].index = index;
			return;
		}
		array[i + 1] = array[i];
	}
	array[0].groupid = groupid;
	array[0].index = index;
}

// 1: world
// 2: group table
// 3: groupstruct component id
// 4: tags id
// 5-: group id
int
ecs_group_enable(lua_State *L) {
	const int groupid_index = 5;
	struct group_enable tmp[MAX_GROUPS];
	struct group_enable *array = tmp;
	int groupn = lua_gettop(L) - (groupid_index - 1);
	struct entity_world *w = getW(L);
	luaL_checktype(L, 2, LUA_TTABLE);
	int sid = check_cid(L, w, 3);
	struct component_pool *g = &w->c[sid];
	struct group *group = (struct group *)g->buffer;
	if (groupn > MAX_GROUPS) {
		array = (struct group_enable *)lua_newuserdatauv(L, groupn * sizeof(struct group_enable), 0);
	}
	int i;
	int n = 0;
	for (i = 0; i < groupn; i++) {
		int groupid = luaL_checkinteger(L, groupid_index + i);
		int index = read_group(L, g, 2, groupid);
		if (index >= 0) {
			group_enable_insert(array, n, groupid, index);
			++n;
		}
	}
	int tagid = check_cid(L, w, 4);
	struct component_pool *tag = &w->c[tagid];
	if (tag->stride != STRIDE_TAG) {
		return luaL_error(L, "Invalid tag");
	}
	tag->n = 0;
	while (n > 0) {
		int index = array[n - 1].index;
		ecs_add_component_id_nocheck_(L, 1, w, tagid, g->id[index]);
		index = next_groupid(group, index);
		if (index >= 0) {
			group_enable_insert(array, n - 1, array[n - 1].groupid, index);
		} else {
			--n;
		}
	}
	n = tag->n;
	int last = n - 1;
	for (i = 0; i < n / 2; i++) {
		unsigned int tmp = tag->id[i];
		tag->id[i] = tag->id[last];
		tag->id[last] = tmp;
		--last;
	}
	return 0;
}

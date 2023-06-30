#ifdef TEST_LUAECS

#include <stdio.h>
#include "luaecs.h"

#define COMPONENT_VECTOR2 MAKE_COMPONENT_ID(1)
#define TAG_MARK MAKE_COMPONENT_ID(2)
#define COMPONENT_ID MAKE_COMPONENT_ID(3)
#define SINGLETON_STRING MAKE_COMPONENT_ID(4)
#define COMPONENT_LUA MAKE_COMPONENT_ID(5)

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
	struct ecs_token token;
	for (i = 0; (v = (struct vector2 *)entity_iter(ctx, COMPONENT_VECTOR2, i, &token)); i++) {
		printf("vector2 %d: x=%f y=%f\n", i, v->x, v->y);
		struct id *id = (struct id *)entity_component(ctx, token, COMPONENT_ID, NULL);
		if (id) {
			printf("\tid = %d\n", id->v);
		}
		void *mark = entity_component(ctx, token, TAG_MARK, NULL);
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
	for (i = 0; (v = (struct vector2 *)entity_iter(ctx, COMPONENT_VECTOR2, i, NULL)); i++) {
		s += v->x + v->y;
	}
	lua_pushnumber(L, s);
	return 1;
}

struct userdata_t {
	unsigned char a;
	void *b;
};

static int
ltestuserdata(lua_State *L) {
	struct ecs_context *ctx = lua_touserdata(L, 1);
	struct userdata_t *ud = entity_iter(ctx, 1, 0, NULL);
	ud->a = 1 - ud->a;
	ud->b = ctx;
	return 0;
}

static int
lgetlua(lua_State *L) {
	struct ecs_context *ctx = lua_touserdata(L, 1);
	int index = luaL_checkinteger(L, 2);
	int t = entity_get_lua(ctx, COMPONENT_LUA, index-1, L);
	if (t) {
		return 1;
	}
	return 0;
}

static int
lsiblinglua(lua_State *L) {
	struct ecs_context *ctx = lua_touserdata(L, 1);
	int index = luaL_checkinteger(L, 2);
	struct ecs_token token;
	if (entity_iter(ctx, TAG_MARK, index, &token) == NULL)
		return luaL_error(L, "Invalid token");
	int t = entity_component_lua(ctx, token, COMPONENT_LUA, L);
	if (t) {
		return 1;
	}
	return 0;
}

static int
lcache(lua_State *L) {
	struct ecs_context *ctx = lua_touserdata(L, 1);
	int index[3] = {
		TAG_MARK,
		COMPONENT_ID,
		COMPONENT_EID,
	};

	struct ecs_cache *c = entity_cache_create(ctx, index, 3);
	
	int n = entity_cache_sync(ctx, c);
	int i;
	lua_createtable(L, n, 0);
	for (i=0;i<n;i++) {
		int * v = (int *)entity_cache_fetch(ctx, c, i, COMPONENT_ID);
		if (v) {
			lua_pushinteger(L, *v);
		} else {
			lua_pushboolean(L, 0);
		}
		lua_rawseti(L, -2, i+1);
	}
	lua_createtable(L, n, 0);
	for (i=0;i<n;i++) {
		int64_t v = (int64_t)entity_cache_fetch(ctx, c, i, COMPONENT_EID);
		lua_pushinteger(L, v);
		lua_rawseti(L, -2, i+1);
	}

	entity_cache_release(ctx, c);

	return 2;
}

LUAMOD_API int
luaopen_ecs_ctest(lua_State *L) {
	luaL_checkversion(L);
	luaL_Reg l[] = {
		{ "test", ltest },
		{ "sum", lsum },
		{ "testuserdata", ltestuserdata },
		{ "getlua", lgetlua },
		{ "siblinglua", lsiblinglua },
		{ "cache", lcache },
		{ NULL, NULL },
	};
	luaL_newlib(L, l);
	return 1;
}

#endif
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
	for (i = 0; (v = (struct vector2 *)entity_iter(ctx, COMPONENT_VECTOR2, i)); i++) {
		printf("vector2 %d: x=%f y=%f\n", i, v->x, v->y);
		struct id *id = (struct id *)entity_sibling(ctx, COMPONENT_VECTOR2, i, COMPONENT_ID);
		if (id) {
			printf("\tid = %d\n", id->v);
		}
		void *mark = entity_sibling(ctx, COMPONENT_VECTOR2, i, TAG_MARK);
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
	for (i = 0; (v = (struct vector2 *)entity_iter(ctx, COMPONENT_VECTOR2, i)); i++) {
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
	struct userdata_t *ud = entity_iter(ctx, 1, 0);
	ud->a = 1 - ud->a;
	ud->b = ctx;
	return 0;
}

static int
lgetlua(lua_State *L) {
	struct ecs_context *ctx = lua_touserdata(L, 1);
	int index = luaL_checkinteger(L, 2);
	int t = entity_get_lua(ctx, COMPONENT_LUA, index, L);
	if (t) {
		return 1;
	}
	return 0;
}

static int
lsiblinglua(lua_State *L) {
	struct ecs_context *ctx = lua_touserdata(L, 1);
	int index = luaL_checkinteger(L, 2);
	int t = entity_sibling_lua(ctx, TAG_MARK, index, COMPONENT_LUA, L);
	if (t) {
		return 1;
	}
	return 0;
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
		{ NULL, NULL },
	};
	luaL_newlib(L, l);
	return 1;
}

#endif
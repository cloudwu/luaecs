#include <lua.h>
#include <lauxlib.h>

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "ecs_internal.h"
#include "ecs_template.h"

// varint :
//	[0,0x7f]  one byte
//	[0x80, 0x3fff] two bytes : v >> 7,  0x80 | (v & 0x7f)
//	[0x4000, 0x1fffff] three bytes
static inline void
varint_encode(luaL_Buffer *b, size_t v) {
	while (v > 0x7f) {
		luaL_addchar(b, (char)((v & 0x7f) | 0x80));
		v >>= 7;
	}
	luaL_addchar(b, (char)v);
}

static size_t
varint_decode(lua_State *L, uint8_t *buffer, size_t sz, size_t *r) {
	*r = 0;
	int shift = 0;
	size_t rsize = 0;
	while (sz > rsize) {
		int s = buffer[rsize] & 0x7f;
		*r |= (size_t)s << shift;
		if (s == buffer[rsize]) {
			return rsize + 1;
		}
		++rsize;
		shift += 7;
	}
	return luaL_error(L, "Invalid varint");
}

int
ecs_serialize_object(lua_State *L) {
	struct group_iter *iter = check_groupiter(L, 1);
	int cid = iter->k[0].id;
	struct entity_world *w = iter->world;
	if (cid < 0 || cid >= MAX_COMPONENT) {
		return luaL_error(L, "Invalid object %d", cid);
	}
	lua_settop(L, 2);
	struct component_pool *c = &w->c[cid];
	if (c->stride <= 0) {
		if (c->stride == STRIDE_LUA)
			return luaL_error(L, "Can't serialize lua object");
		return luaL_error(L, "Can't serialize tag");
	}

	luaL_Buffer b;
	luaL_buffinit(L, &b);
	varint_encode(&b, c->stride);

	void *buffer = luaL_prepbuffsize(&b, c->stride);
	lua_pushvalue(L, 2);
	ecs_write_component_object_(L, iter->k[0].field_n, iter->f, buffer);
	luaL_addsize(&b, c->stride);
	luaL_pushresult(&b);
	return 1;
}

int
ecs_template_create(lua_State *L) {
	struct entity_world *w = getW(L);
	luaL_checktype(L, 2, LUA_TTABLE);
	int i = 1;
	luaL_Buffer b;
	luaL_buffinit(L, &b);
	for (;;) {
		if (lua_geti(L, 2, i++) == LUA_TNIL) {
			lua_pop(L, 1);
			break;
		}
		int cid = check_cid(L, w, -1);
		lua_pop(L, 1);

		varint_encode(&b, cid);
		switch (lua_geti(L, 2, i++)) {
		case LUA_TSTRING:
			luaL_addvalue(&b);
			break;
		case LUA_TUSERDATA: {
			size_t sz = lua_rawlen(L, -1);
			void *buf = lua_touserdata(L, -1);
			luaL_addlstring(&b, (const char *)buf, sz);
			lua_pop(L, 1);
			break;
		}
		default:
			return luaL_error(L, "Invalid valid with cid = %d", (int)cid);
		}
	}
	luaL_pushresult(&b);
	return 1;
}

// 1 : world
// 2 : component id
// 3 : index
// 4 : value
int
ecs_template_instance_component(lua_State *L) {
	struct entity_world *w = getW(L);
	int cid = check_cid(L, w, 2);
	int index = luaL_checkinteger(L, 3) - 1;
	struct component_pool *c = &w->c[cid];
	if (c->stride == STRIDE_LUA) {
		lua_State *tL = w->lua.L;
		lua_pushvalue(L, 4);
		lua_xmove(L, tL, 1);
		unsigned int *lua_index = (unsigned int *)c->buffer;
		lua_rawseti(tL, 1, lua_index[index]);
	} else {
		size_t sz;
		void *s;
		switch (lua_type(L, 4)) {
		case LUA_TSTRING:
			s = (void *)lua_tolstring(L, 4, &sz);
			break;
		case LUA_TLIGHTUSERDATA:
			s = lua_touserdata(L, 4);
			sz = luaL_checkinteger(L, 5);
			break;
		default:
			lua_pushboolean(L, 1);
			return 1;
		}
		if (sz != c->stride) {
			return luaL_error(L, "Invalid unmarshal result");
		}
		memcpy(get_ptr(c, index), s, sz);
	}
	return 0;
}

// 1 : string data
// 2 : int offset
int
ecs_template_extract(lua_State *L) {
	size_t sz;
	const char *buffer = luaL_checklstring(L, 1, &sz);
	int offset = luaL_optinteger(L, 2, 0);
	if (offset >= sz) {
		if (offset > sz)
			return luaL_error(L, "Invalid offset %d", offset);
		return 0;
	}
	sz -= offset;
	buffer += offset;
	size_t cid;
	size_t r = varint_decode(L, (uint8_t *)buffer, sz, &cid);
	lua_pushinteger(L, cid);
	sz -= r;
	buffer += r;
	size_t slen;
	size_t r2 = varint_decode(L, (uint8_t *)buffer, sz, &slen);
	if (slen == 0 && r2 == 2) { // magic number 0x80 0x00 , string
		r2 += varint_decode(L, (uint8_t *)buffer + 2, sz - 2, &slen);
		sz -= r2;
		buffer += r2;
		if (slen > sz) {
			return luaL_error(L, "Invalid template");
		}
		// return id, offset, string
		lua_pushinteger(L, offset + r + r2 + slen);
		lua_pushlstring(L, buffer, slen);
		return 3;
	} else {
		// return id, offset, lightuserdata, sz
		sz -= r2;
		buffer += r2;
		lua_pushinteger(L, offset + r + r2 + slen);
		lua_pushlightuserdata(L, (void *)buffer);
		lua_pushinteger(L, slen);
		return 4;
	}
}

int
ecs_serialize_lua(lua_State *L) {
	luaL_Buffer b;
	if (lua_type(L, 1) == LUA_TSTRING) {
		size_t sz;
		const char *s = lua_tolstring(L, 1, &sz);
		luaL_buffinitsize(L, &b, sz + 8);
		luaL_addchar(&b, 0x80);
		luaL_addchar(&b, 0); // 0x80 0x00 : magic number, it's string
		varint_encode(&b, sz);
		luaL_addlstring(&b, s, sz);
	} else {
		// Support seri function in ltask : https://github.com/cloudwu/ltask/blob/master/src/lua-seri.c
		luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
		const char *buf = (const char *)lua_touserdata(L, 1);
		size_t sz = luaL_checkinteger(L, 2);
		luaL_buffinitsize(L, &b, sz + 8);
		varint_encode(&b, sz);
		luaL_addlstring(&b, buf, sz);
		free((void *)buf); // lightuserdata, free
	}
	luaL_pushresult(&b);
	return 1;
}


int
ltemplate_methods(lua_State *L) {
	luaL_Reg m[] = {
		{ "_serialize", ecs_serialize_object },
		{ "_serialize_lua", ecs_serialize_lua },
		{ "_template_extract", ecs_template_extract },
		{ "_template_create", ecs_template_create },
		{ "_template_instance_component", ecs_template_instance_component },
		{ NULL, NULL },
	};
	luaL_newlib(L, m);

	return 1;
}
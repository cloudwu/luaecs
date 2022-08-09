#include <lua.h>
#include <lauxlib.h>
#include <stdio.h>

#include "ecs_internal.h"
#include "ecs_persistence.h"

struct file_reader {
	FILE *f;
};

static entity_index_t
read_id(lua_State *L, FILE *f, entity_index_t *id, int n) {
	size_t r = fread(id, sizeof(entity_index_t), n, f);
	if (r != n)
		luaL_error(L, "Read id error");
	int i;
	uint32_t last_id = 0;
	for (i = 0; i < n; i++) {
		last_id += index_(id[i]); 
		id[i] = make_index_(last_id);
	}
	return make_index_(last_id);
}

static void
read_data(lua_State *L, FILE *f, void *buffer, int stride, int n) {
	size_t r = fread(buffer, stride, n, f);
	if (r != n)
		luaL_error(L, "Read data error");
}

static entity_index_t
read_section(lua_State *L, struct file_reader *reader, struct component_pool *c, size_t offset, int stride, int n) {
	if (reader->f == NULL)
		luaL_error(L, "Invalid reader");
	if (fseek(reader->f, offset, SEEK_SET) != 0) {
		luaL_error(L, "Reader seek error");
	}
	entity_index_t maxid;
	if (stride > 0) {
		maxid = read_id(L, reader->f, c->id, n);
		read_data(L, reader->f, c->buffer, stride, n);
	} else {
		maxid = read_id(L, reader->f, c->id, n);
	}
	return maxid;
}

static void
read_section_eid(lua_State *L, struct file_reader *reader, uint64_t *eid, size_t offset, int n) {
	if (reader->f == NULL)
		luaL_error(L, "Invalid reader");
	if (fseek(reader->f, offset, SEEK_SET) != 0) {
		luaL_error(L, "Reader seek error");
	}
	size_t r = fread(eid, sizeof(uint64_t), n, reader->f);
	if (r != n)
		luaL_error(L, "Read eid error");
	int i;
	uint64_t last_id = (uint64_t)-1;
	for (i = 0; i < n; i++) {
		last_id += eid[i] + 1;
		eid[i] = last_id;
	}
}

static int
ecs_persistence_generate_eid(lua_State *L) {
	struct entity_world *w = getW(L);
	int i;
	int maxid = -1;
	for (i=0;i<MAX_COMPONENT;i++) {
		struct component_pool *c = &w->c[i];
		if (c->n > 0) {
			int m = index_(c->id[c->n-1]);
			if (m > maxid)
				maxid = m;
		}
	}
	if (maxid < 0) {
		return 0;
	}
	maxid++;
	ecs_reserve_eid_(w, maxid);
	w->eid.last_id = w->eid.n = maxid;
	for (i=0;i<maxid;i++) {
		w->eid.id[i] = (uint64_t)i+1;
	}
	return 0;
}

int
ecs_persistence_readcomponent(lua_State *L) {
	struct entity_world *w = getW(L);
	struct file_reader *reader = luaL_checkudata(L, 2, "LUAECS_READER");
	int cid = luaL_checkinteger(L, 3);
	size_t offset = luaL_checkinteger(L, 4);
	int stride = luaL_optinteger(L, 5, -1);
	int n = luaL_checkinteger(L, 6);

	if (cid == ENTITYID_TAG) {
		if (stride != -1)
			return luaL_error(L, "Invalid eid");
		ecs_reserve_eid_(w, n);
		read_section_eid(L, reader, w->eid.id, offset, n);
		w->eid.n = n;
		w->eid.last_id = (n > 0) ? w->eid.id[n-1] : 0;
		lua_pushinteger(L, n);
		return 1;
	} else {
		check_cid_valid(L, w, cid);
		struct component_pool *c = &w->c[cid];
		if (c->n != 0) {
			return luaL_error(L, "Component %d exists", cid);
		}
		if (c->stride != stride) {
			return luaL_error(L, "Invalid component %d (%d != %d)", cid, c->stride, stride);
		}
		ecs_reserve_component_(c, cid, n);
		entity_index_t maxid = read_section(L, reader, c, offset, stride, n);
		c->n = n;
		lua_pushinteger(L, index_(maxid));
		return 1;
	}
}

struct file_section {
	size_t offset;
	int stride;
	int n;
};

struct file_writer {
	FILE *f;
	int n;
	struct file_section c[MAX_COMPONENT];
};

static size_t
get_length(struct file_section *s) {
	if (s->stride < 0)
		return s->offset + s->n * sizeof(uint64_t);
	else
		return s->offset + s->n * (sizeof(entity_index_t) + s->stride);
}

static uint32_t
write_id_(lua_State *L, struct file_writer *w, entity_index_t *id, int n, uint32_t last_id) {
	entity_index_t buffer[1024];
	int i;
	for (i = 0; i < n; i++) {
		uint32_t t = index_(id[i]);
		uint32_t diff = t - last_id;
		last_id = t;
		buffer[i] = make_index_(diff);
	}
	size_t r = fwrite(buffer, sizeof(entity_index_t), n, w->f);
	if (r != n) {
		luaL_error(L, "Can't write section id");
	}
	return last_id;
}

static void
write_id(lua_State *L, struct file_writer *w, struct component_pool *c) {
	int i;
	uint32_t last_id = 0;
	for (i = 0; i < c->n; i += 1024) {
		int n = c->n - i;
		if (n > 1024)
			n = 1024;
		last_id = write_id_(L, w, c->id + i, n, last_id);
	}
}

static void
write_data(lua_State *L, struct file_writer *w, struct component_pool *c) {
	size_t s = fwrite(c->buffer, c->stride, c->n, w->f);
	if (s != c->n) {
		luaL_error(L, "Can't write section data %d:%d", c->n, c->stride);
	}
}

static uint64_t
write_eid_(lua_State *L, struct file_writer *w, const uint64_t *id, int n, uint64_t last_id) {
	uint64_t buffer[1024];
	int i;
	for (i = 0; i < n; i++) {
		uint64_t diff = id[i] - last_id - 1;
		last_id = id[i];
		buffer[i] = diff;
	}
	size_t r = fwrite(buffer, sizeof(uint64_t), n, w->f);
	if (r != n) {
		luaL_error(L, "Can't write section id");
	}
	return last_id;
}

static void
write_eid(lua_State *L, struct file_writer *w, const struct entity_id *eid) {
	int i;
	uint64_t last_id = (uint64_t)-1;
	for (i = 0; i < eid->n; i += 1024) {
		int n = eid->n - i;
		if (n > 1024)
			n = 1024;
		last_id = write_eid_(L, w, eid->id + i, n, last_id);
	}
}

static int
lwrite_section(lua_State *L) {
	struct file_writer *w = (struct file_writer *)luaL_checkudata(L, 1, "LUAECS_WRITER");
	struct entity_world *world = (struct entity_world *)lua_touserdata(L, 2);
	if (w->f == NULL)
		return luaL_error(L, "Invalid writer");
	if (world == NULL)
		return luaL_error(L, "Invalid world");
	if (w->n >= MAX_COMPONENT)
		return luaL_error(L, "Too many sections");

	struct file_section *s = &w->c[w->n];
	if (w->n == 0) {
		s->offset = 0;
	} else {
		s->offset = get_length(&w->c[w->n - 1]);
	}

	int cid = luaL_checkinteger(L, 3);
	if (cid == ENTITYID_TAG) {
		s->stride = -1;	// It's eid
		s->n = world->eid.n;
		++w->n;
		write_eid(L, w, &world->eid);
	} else {
		check_cid_valid(L, world, cid);
		struct component_pool *c = &world->c[cid];
		if (c->stride < 0) {
			return luaL_error(L, "The component is not writable");
		}
		s->stride = c->stride;
		s->n = c->n;
		++w->n;
		if (s->stride > 0) {
			write_id(L, w, c);
			write_data(L, w, c);
		} else {
			write_id(L, w, c);
		}
	}
	return 0;
}

static int
lrawclose_writer(lua_State *L) {
	struct file_writer *w = (struct file_writer *)lua_touserdata(L, 1);
	if (w->f) {
		fclose(w->f);
		w->f = NULL;
	}
	return 0;
}

static int
lclose_writer(lua_State *L) {
	struct file_writer *w = (struct file_writer *)luaL_checkudata(L, 1, "LUAECS_WRITER");
	if (w->f == NULL)
		return luaL_error(L, "Invalid writer");
	lrawclose_writer(L);
	lua_createtable(L, w->n, 0);
	int i;
	for (i = 0; i < w->n; i++) {
		lua_createtable(L, 0, 3);
		struct file_section *s = &w->c[i];
		lua_pushinteger(L, s->offset);
		lua_setfield(L, -2, "offset");
		if (s->stride >= 0) {
			lua_pushinteger(L, s->stride);
			lua_setfield(L, -2, "stride");
		}
		lua_pushinteger(L, s->n);
		lua_setfield(L, -2, "n");
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

static FILE *
fileopen(lua_State *L, int idx, const char *mode) {
	if (lua_type(L, idx) == LUA_TSTRING) {
		const char *filename = lua_tostring(L, idx);
		FILE *f = fopen(filename, mode);
		if (f == NULL) {
			luaL_error(L, "Can't open %s (%s)", filename, mode);
		}
		return f;
	}
	int fd = luaL_checkinteger(L, idx);
	FILE *f = fdopen(fd, mode);
	if (f == NULL)
		luaL_error(L, "Can't open %d (%s)", fd, mode);
	return f;
}

int
ecs_persistence_writer(lua_State *L) {
	struct file_writer *w = (struct file_writer *)lua_newuserdatauv(L, sizeof(*w), 0);
	w->f = NULL;
	w->n = 0;
	w->f = fileopen(L, 1, "wb");
	if (luaL_newmetatable(L, "LUAECS_WRITER")) {
		luaL_Reg l[] = {
			{ "write", lwrite_section },
			{ "close", lclose_writer },
			{ "__gc", lrawclose_writer },
			{ "__index", NULL },
			{ NULL, NULL },
		};
		luaL_setfuncs(L, l, 0);
		lua_pushvalue(L, -1);
		lua_setfield(L, -2, "__index");
	}
	lua_setmetatable(L, -2);
	return 1;
}

static int
lclose_reader(lua_State *L) {
	struct file_reader *r = (struct file_reader *)lua_touserdata(L, 1);
	if (r != NULL && r->f != NULL) {
		fclose(r->f);
		r->f = NULL;
	}
	return 0;
}

int
ecs_persistence_reader(lua_State *L) {
	struct file_reader *r = (struct file_reader *)lua_newuserdatauv(L, sizeof(*r), 0);
	r->f = fileopen(L, 1, "rb");
	if (luaL_newmetatable(L, "LUAECS_READER")) {
		luaL_Reg l[] = {
			{ "close", lclose_reader },
			{ "__gc", lclose_reader },
			{ "__index", NULL },
			{ NULL, NULL },
		};
		luaL_setfuncs(L, l, 0);
		lua_pushvalue(L, -1);
		lua_setfield(L, -2, "__index");
	}
	lua_setmetatable(L, -2);
	return 1;
}

int
lpersistence_methods(lua_State *L) {
	luaL_Reg m[] = {
		{ "_readcomponent", ecs_persistence_readcomponent },
		{ "generate_eid", ecs_persistence_generate_eid },
		{ "writer", ecs_persistence_writer },
		{ "reader", ecs_persistence_reader },		
		{ NULL, NULL },
	};
	luaL_newlib(L, m);

	return 1;
}
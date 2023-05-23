#ifndef lua_ecs_cdata_h
#define lua_ecs_cdata_h

#include <assert.h>
#include <stddef.h>

typedef unsigned int cid_t;

#define MAKE_COMPONENT_ID(id) (cid_t)(0x80000000 | (id))
#define COMPONENT_EID 0xffffffff

struct entity_world;
struct ecs_cache;

struct ecs_capi {
	void *(*iter)(struct entity_world *w, int cid, int index);
	void (*clear_type)(struct entity_world *w, int cid);
	int (*sibling_id)(struct entity_world *w, int cid, int index, int slibling_id);
	int (*sibling_id_hint)(struct entity_world *w, int cid, int index, int slibling_id, int hint);
	void *(*add_sibling)(struct entity_world *w, int cid, int index, int slibling_id, const void *buffer);
	int (*new_entity)(struct entity_world *w, int cid, const void *buffer);
	void (*remove)(struct entity_world *w, int cid, int index);
	void (*enable_tag)(struct entity_world *w, int cid, int index, int tag_id);
	void (*disable_tag)(struct entity_world *w, int cid, int index, int tag_id);
	int (*get_lua)(struct entity_world *w, int cid, int index, void *wL, int world_index, void *L);
	void (*group_enable)(struct entity_world *w, int tagid, int n, int groupid[]);
	int (*count)(struct entity_world *w, int cid);
	int (*index)(struct entity_world *w, void *eid);
	struct ecs_cache * (*cache_create)(struct entity_world *w, int keys[], int n);
	void (*cache_release)(struct ecs_cache *);
	void* (*cache_fetch)(struct ecs_cache *, int index, int cid);
	int (*cache_sync)(struct ecs_cache *);
};

struct ecs_context {
	struct ecs_capi *api;
	struct entity_world *world;
	void *L; // for memory allocator
	int max_id;
	int cid[1];
};

static inline int
real_id_(struct ecs_context *ctx, cid_t cid) {
	if (cid & 0x80000000) {
		if (cid == COMPONENT_EID)
			return -1;
		int index = cid & 0x7fffffff;
		assert(index >= 0 && index <= ctx->max_id);
		return ctx->cid[index];
	} else {
		return (int)cid;
	}
}

static inline void *
entity_iter(struct ecs_context *ctx, cid_t cid, int index) {
	int id = real_id_(ctx, cid);
	return ctx->api->iter(ctx->world, id, index);
}

static inline void
entity_clear_type(struct ecs_context *ctx, cid_t cid) {
	int id = real_id_(ctx, cid);
	ctx->api->clear_type(ctx->world, id);
}

static inline int
entity_sibling_id(struct ecs_context *ctx, cid_t cid, int index, cid_t sibling_id) {
	int mid = real_id_(ctx, cid);
	int sid = real_id_(ctx, sibling_id);
	return ctx->api->sibling_id(ctx->world, mid, index, sid);
}

static inline int
entity_sibling_id_hint(struct ecs_context *ctx, cid_t cid, int index, cid_t sibling_id, int hint) {
	int mid = real_id_(ctx, cid);
	int sid = real_id_(ctx, sibling_id);
	return ctx->api->sibling_id_hint(ctx->world, mid, index, sid, hint);
}

static inline void *
entity_sibling(struct ecs_context *ctx, cid_t cid, int index, cid_t sibling_id) {
	int mid = real_id_(ctx, cid);
	int sid = real_id_(ctx, sibling_id);
	int id = ctx->api->sibling_id(ctx->world, mid, index, sid);
	if (id == 0) {
		return NULL;
	} else {
		return ctx->api->iter(ctx->world, sid, id - 1);
	}
}

static inline void *
entity_add_sibling(struct ecs_context *ctx, cid_t cid, int index, cid_t sibling_id, const void *buffer) {
	int mid = real_id_(ctx, cid);
	int sid = real_id_(ctx, sibling_id);
	return ctx->api->add_sibling(ctx->world, mid, index, sid, buffer);
}

static inline int
entity_new(struct ecs_context *ctx, cid_t cid, const void *buffer) {
	int mid = real_id_(ctx, cid);
	return ctx->api->new_entity(ctx->world, mid, buffer);
}

static inline void
entity_remove(struct ecs_context *ctx, cid_t cid, int index) {
	int mid = real_id_(ctx, cid);
	ctx->api->remove(ctx->world, mid, index);
}

static inline void
entity_enable_tag(struct ecs_context *ctx, cid_t cid, int index, cid_t tag_id) {
	int mid = real_id_(ctx, cid);
	int tid = real_id_(ctx, tag_id);
	ctx->api->enable_tag(ctx->world, mid, index, tid);
}

static inline void
entity_disable_tag(struct ecs_context *ctx, cid_t cid, int index, cid_t tag_id) {
	int mid = real_id_(ctx, cid);
	int tid = real_id_(ctx, tag_id);
	ctx->api->disable_tag(ctx->world, mid, index, tid);
}

static inline int
entity_get_lua(struct ecs_context *ctx, cid_t cid, int index, void *L) {
	int mid = real_id_(ctx, cid);
	assert(index > 0);
	return ctx->api->get_lua(ctx->world, mid, index - 1, ctx->L, 1, L);
}

static inline int
entity_sibling_lua(struct ecs_context *ctx, cid_t cid, int index, cid_t sibling_id, void *L) {
	int mid = real_id_(ctx, cid);
	int sid = real_id_(ctx, sibling_id);
	int id = ctx->api->sibling_id(ctx->world, mid, index, sid);
	if (id == 0) {
		return 0;
	} else {
		return ctx->api->get_lua(ctx->world, sid, id - 1, ctx->L, 1, L);
	}
}

static inline void
entity_group_enable(struct ecs_context *ctx, int tagid, int n, int groupid[]) {
	int id = real_id_(ctx, tagid);
	return ctx->api->group_enable(ctx->world, id, n, groupid);
}

static inline int
entity_count(struct ecs_context *ctx, int cid) {
	int id = real_id_(ctx, cid);
	return ctx->api->count(ctx->world, id);
}

static inline int
entity_index(struct ecs_context *ctx, void *eid) {
	return ctx->api->index(ctx->world, eid);
}


static inline struct ecs_cache *
entity_cache_create(struct ecs_context *ctx, int keys[], int n) {
	int i;
	for (i=0;i<n;i++) {
		keys[i] = real_id_(ctx, keys[i]);
	}
	return ctx->api->cache_create(ctx->world, keys, n);
}

static inline void
entity_cache_release(struct ecs_context *ctx, struct ecs_cache *c) {
	ctx->api->cache_release(c);
}

static inline void *
entity_cache_fetch(struct ecs_context *ctx, struct ecs_cache *c, int index, int cid) {
	int id = real_id_(ctx, cid);
	return ctx->api->cache_fetch(c, index, id);
}

static inline int
entity_cache_sync(struct ecs_context *ctx, struct ecs_cache *c) {
	return ctx->api->cache_sync(c);
}

#endif

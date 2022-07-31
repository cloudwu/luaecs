#ifndef lua_ecs_cdata_h
#define lua_ecs_cdata_h

#include <assert.h>
#include <stddef.h>

typedef unsigned int cid_t;

#define MAKE_COMPONENT_ID(id) (cid_t)(0x80000000 | (id))

struct entity_world;

struct ecs_capi {
	void *(*iter)(struct entity_world *w, int cid, int index);
	void (*clear_type)(struct entity_world *w, int cid);
	int (*sibling_id)(struct entity_world *w, int cid, int index, int slibling_id);
	void *(*add_sibling)(struct entity_world *w, int cid, int index, int slibling_id, const void *buffer);
	int (*new_entity)(struct entity_world *w, int cid, const void *buffer);
	void (*remove)(struct entity_world *w, int cid, int index);
	void (*enable_tag)(struct entity_world *w, int cid, int index, int tag_id);
	void (*disable_tag)(struct entity_world *w, int cid, int index, int tag_id);
	int (*get_lua)(struct entity_world *w, int cid, int index, void *wL, int world_index, void *L);
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

#endif

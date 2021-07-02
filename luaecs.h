#ifndef lua_ecs_cdata_h
#define lua_ecs_cdata_h

#include <assert.h>

struct entity_world;

struct ecs_capi {
	void * (*iter)(struct entity_world *w, int cid, int index);
	void (*clear_type)(struct entity_world *w, int cid);
	void * (*sibling)(struct entity_world *w, int cid, int index, int slibling_id);
	void (*add_sibling)(struct entity_world *w, int cid, int index, int slibling_id, const void *buffer, void *L);
	void (*remove)(struct entity_world *w, int cid, int index, void *L);
};

struct ecs_context {
	struct ecs_capi *api;
	struct entity_world *world;
	void *L;	// for memory allocator
	int max_id;
	int cid[1];
};

static inline void
check_id_(struct ecs_context *ctx, int cid) {
	assert(cid >= 0 && cid <= ctx->max_id);
}

static inline void *
entity_iter(struct ecs_context *ctx, int cid, int index) {
	check_id_(ctx, cid);
	return ctx->api->iter(ctx->world, ctx->cid[cid], index);
}

static inline void
entity_clear_type(struct ecs_context *ctx, int cid) {
	check_id_(ctx, cid);
	ctx->api->clear_type(ctx->world, ctx->cid[cid]);
}

static inline void *
entity_sibling(struct ecs_context *ctx, int cid, int index, int slibling_id) {
	check_id_(ctx, cid);
	check_id_(ctx, slibling_id);
	return ctx->api->sibling(ctx->world,  ctx->cid[cid], index, ctx->cid[slibling_id]);
}

static inline void
entity_add_sibling(struct ecs_context *ctx, int cid, int index, int slibling_id, const void *buffer) {
	check_id_(ctx, cid);
	check_id_(ctx, slibling_id);
	ctx->api->add_sibling(ctx->world, ctx->cid[cid], index, ctx->cid[slibling_id], buffer, ctx->L);
}

static inline void
entity_remove(struct ecs_context *ctx, int cid, int index) {
	check_id_(ctx, cid);
	ctx->api->remove(ctx->world, ctx->cid[cid], index, ctx->L);
}

#endif

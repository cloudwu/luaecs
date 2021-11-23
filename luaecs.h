#ifndef lua_ecs_cdata_h
#define lua_ecs_cdata_h

#include <assert.h>

struct entity_world;

struct ecs_capi {
	void * (*iter)(struct entity_world *w, int cid, int index);
	void (*clear_type)(struct entity_world *w, int cid);
	int (*sibling_id)(struct entity_world *w, int cid, int index, int slibling_id);
	void* (*add_sibling)(struct entity_world *w, int cid, int index, int slibling_id, const void *buffer, void *L, int world_index);
	int (*new_entity)(struct entity_world *w, int cid, const void *buffer, void *L, int world_index);
	void (*remove)(struct entity_world *w, int cid, int index, void *L, int world_index);
	void (*enable_tag)(struct entity_world *w, int cid, int index, int tag_id, void *L, int world_index);
	void (*disable_tag)(struct entity_world *w, int cid, int index, int tag_id);
	void * (*iter_lua)(struct entity_world *w, int cid, int index, void *L, int world_index);
	int (*assign_lua)(struct entity_world *w, int cid, int index, void *L, int world_index);
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

static inline void *
entity_ref_object(struct ecs_context *ctx, int cid, int index) {
	if (index <= 0)
		return NULL;
	check_id_(ctx, cid);
	return ctx->api->iter_lua(ctx->world, ctx->cid[cid], index - 1, ctx->L, 1);
}

static inline void
entity_clear_type(struct ecs_context *ctx, int cid) {
	check_id_(ctx, cid);
	ctx->api->clear_type(ctx->world, ctx->cid[cid]);
}

static inline int
entity_sibling_id(struct ecs_context *ctx, int cid, int index, int sibling_id) {
	check_id_(ctx, cid);
	check_id_(ctx, sibling_id);
	return ctx->api->sibling_id(ctx->world,  ctx->cid[cid], index, ctx->cid[sibling_id]);
}

static inline void *
entity_sibling(struct ecs_context *ctx, int cid, int index, int sibling_id) {
	check_id_(ctx, cid);
	check_id_(ctx, sibling_id);
	int id = ctx->api->sibling_id(ctx->world,  ctx->cid[cid], index, ctx->cid[sibling_id]);
	if (id == 0) {
		return NULL;
	} else {
		return ctx->api->iter(ctx->world, ctx->cid[sibling_id], id-1);
	}
}

static inline void *
entity_add_sibling(struct ecs_context *ctx, int cid, int index, int sibling_id, const void *buffer) {
	check_id_(ctx, cid);
	check_id_(ctx, sibling_id);
	return ctx->api->add_sibling(ctx->world, ctx->cid[cid], index, ctx->cid[sibling_id], buffer, ctx->L, 1);
}

static inline int
entity_new(struct ecs_context *ctx, int cid, const void *buffer) {
	check_id_(ctx, cid);
	return ctx->api->new_entity(ctx->world, ctx->cid[cid], buffer, ctx->L, 1);
}

static inline void
entity_remove(struct ecs_context *ctx, int cid, int index) {
	check_id_(ctx, cid);
	ctx->api->remove(ctx->world, ctx->cid[cid], index, ctx->L, 1);
}

static inline void
entity_enable_tag(struct ecs_context *ctx, int cid, int index, int tag_id) {
	check_id_(ctx, cid);
	check_id_(ctx, tag_id);
	ctx->api->enable_tag(ctx->world, ctx->cid[cid], index, ctx->cid[tag_id], ctx->L, 1);
}

static inline void
entity_disable_tag(struct ecs_context *ctx, int cid, int index, int tag_id) {
	check_id_(ctx, cid);
	check_id_(ctx, tag_id);
	ctx->api->disable_tag(ctx->world, ctx->cid[cid], index, ctx->cid[tag_id]);
}

static inline int
entity_assign_lua(struct ecs_context *ctx, int cid, int index) {
	check_id_(ctx, cid);
	assert(index > 0);
	return ctx->api->assign_lua(ctx->world, ctx->cid[cid], index-1, ctx->L, 1);
}

#endif

#ifndef lua_ecs_cdata_h
#define lua_ecs_cdata_h

#include <assert.h>
#include <stddef.h>

#define COMPONENT_EID -1

struct entity_world;
struct ecs_cache;
struct ecs_token { int id; };

struct ecs_capi {
	void *(*fetch)(struct entity_world *w, int cid, int index, struct ecs_token *t);
	void (*clear_type)(struct entity_world *w, int cid);
	void *(*component)(struct entity_world *w, struct ecs_token t, int cid);
	int (*component_index)(struct entity_world *w, struct ecs_token t, int cid);
	void *(*component_add)(struct entity_world *w, struct ecs_token t, int cid, const void *buffer);
	void *(*component_temp)(struct entity_world *w, int tag, int cid);
	int (*new_entity)(struct entity_world *w, int cid, struct ecs_token *t);
	void (*remove)(struct entity_world *w, struct ecs_token t);
	void (*enable_tag)(struct entity_world *w, struct ecs_token t, int tag_id);
	void (*disable_tag)(struct entity_world *w, int tag_id, int index);
	int (*next_tag_)(struct entity_world *w, int tag_id, int index, struct ecs_token *t);
	int (*get_lua)(struct entity_world *w, int cid, int index, void *L);
	void (*group_enable)(struct entity_world *w, int tagid, int n, int groupid[]);
	int (*count)(struct entity_world *w, int cid);
	int (*index)(struct entity_world *w, void *eid);
	int (*propagate_tag)(struct entity_world *w, int cid, int tag_id);
	struct ecs_cache * (*cache_create)(struct entity_world *w, int keys[], int n);
	void (*cache_release)(struct ecs_cache *);
	void* (*cache_fetch)(struct ecs_cache *, int index, int cid);
	int (*cache_fetch_index)(struct ecs_cache *, int index, int cid);
	int (*cache_sync)(struct ecs_cache *);
};

struct ecs_context {
	struct ecs_capi *api;
	struct entity_world *world;
};

static inline void *
entity_fetch(struct ecs_context *ctx, int id, int index, struct ecs_token *t) {
	return ctx->api->fetch(ctx->world, id, index, t);
}

static inline void
entity_clear_type(struct ecs_context *ctx, int id) {
	ctx->api->clear_type(ctx->world, id);
}

static inline void *
entity_component(struct ecs_context *ctx, struct ecs_token t, int cid) {
	return ctx->api->component(ctx->world, t, cid);
}

static inline int
entity_component_index(struct ecs_context *ctx, struct ecs_token t, int cid) {
	return ctx->api->component_index(ctx->world, t, cid);
}

static inline void *
entity_component_add(struct ecs_context *ctx, struct ecs_token t, int id, const void *buffer) {
	return ctx->api->component_add(ctx->world, t, id, buffer);
}

static inline void *
entity_component_temp(struct ecs_context *ctx, int tag, int cid) {
	return ctx->api->component_temp(ctx->world, tag, cid);
}

static inline int
entity_new(struct ecs_context *ctx, int mid, struct ecs_token *t) {
	return ctx->api->new_entity(ctx->world, mid, t);
}

static inline void
entity_remove(struct ecs_context *ctx, struct ecs_token t) {
	ctx->api->remove(ctx->world, t);
}

static inline void
entity_enable_tag(struct ecs_context *ctx, struct ecs_token t, int tid) {
	ctx->api->enable_tag(ctx->world, t, tid);
}

static inline void
entity_disable_tag(struct ecs_context *ctx, int tid, int index) {
	ctx->api->disable_tag(ctx->world, tid, index);
}

static inline int
entity_next(struct ecs_context *ctx, int tid, int index, struct ecs_token *t) {
	return ctx->api->next_tag_(ctx->world, tid, index, t);
}

static inline int
entity_get_lua(struct ecs_context *ctx, int mid, int index, void *L) {
	assert(index >= 0);
	return ctx->api->get_lua(ctx->world, mid, index, L);
}

static inline int
entity_component_lua(struct ecs_context *ctx, struct ecs_token t, int cid, void *L) {
	int id = ctx->api->component_index(ctx->world, t, cid);
	if (id >= 0) {
		return ctx->api->get_lua(ctx->world, cid, id, L);
	} else {
		return 0;
	}
}

static inline void
entity_group_enable(struct ecs_context *ctx, int id, int n, int groupid[]) {
	return ctx->api->group_enable(ctx->world, id, n, groupid);
}

static inline int
entity_count(struct ecs_context *ctx, int id) {
	return ctx->api->count(ctx->world, id);
}

static inline int
entity_index(struct ecs_context *ctx, void *eid, struct ecs_token *t) {
	int id = ctx->api->index(ctx->world, eid);
	if (t)
		t->id = id;
	return id;
}

static inline int
entity_propagate_tag(struct ecs_context *ctx, int cid, int tag_id) {
	return ctx->api->propagate_tag(ctx->world, cid, tag_id);
}

static inline struct ecs_cache *
entity_cache_create(struct ecs_context *ctx, int keys[], int n) {
	return ctx->api->cache_create(ctx->world, keys, n);
}

static inline void
entity_cache_release(struct ecs_context *ctx, struct ecs_cache *c) {
	ctx->api->cache_release(c);
}

static inline void *
entity_cache_fetch(struct ecs_context *ctx, struct ecs_cache *c, int index, int id) {
	return ctx->api->cache_fetch(c, index, id);
}

static inline int
entity_cache_fetch_index(struct ecs_context *ctx, struct ecs_cache *c, int index, int id) {
	return ctx->api->cache_fetch_index(c, index, id);
}

static inline int
entity_cache_sync(struct ecs_context *ctx, struct ecs_cache *c) {
	return ctx->api->cache_sync(c);
}

#endif

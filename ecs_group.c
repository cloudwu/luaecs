#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <lua.h>
#include <lauxlib.h>

#include "ecs_internal.h"
#include "ecs_capi.h"
#include "ecs_group.h"
#include "ecs_entityindex.h"

#define DEFAULT_GROUP_SIZE 1024
#define GROUP_COMBINE 1024

struct entity_iterator {
	int last_pos;
	int decode_pos;
	int encode_pos;
	int n;
	uint64_t last;
	uint64_t eid;
};

struct entity_group {
	int n;
	int cap;
	int groupid;
	uint64_t last;
	uint8_t *s;
};

void
entity_group_deinit_(struct entity_group_arena *G) {
	int i;
	for (i=0;i<G->n;i++) {
		free(G->g[i]);
	}
	free(G->g);
}

size_t
entity_group_memsize_(struct entity_group_arena *G) {
	size_t sz = G->cap * sizeof(struct entity_group *);
	int i;
	for (i=0;i<G->n;i++) {
		sz += sizeof(struct entity_group) +  G->g[i]->cap;
	}
	return sz;
}

static inline void
add_byte(struct entity_group *g, uint8_t b) {
	if (g->n >= g->cap) {
		if (g->s == NULL) {
			g->cap = DEFAULT_GROUP_SIZE;
			g->s = (uint8_t *)malloc(DEFAULT_GROUP_SIZE);
		} else {
			int newcap = g->cap * 3 / 2 + 1;
			g->s = (uint8_t *)realloc(g->s, newcap);
			g->cap = newcap;
		}
	}
	g->s[g->n++] = b;
}

static void
add_eid(struct entity_group *g, uint64_t eid) {
	uint64_t eid_diff = eid - g->last - 1;
	if (eid_diff < 128) {
		add_byte(g, eid_diff);
	} else {
		do {
			add_byte(g, (eid_diff & 0x7f) | 0x80);
			eid_diff >>= 7;
		} while (eid_diff >= 128);
		add_byte(g, eid_diff);
	}
	g->last = eid;
}

static inline void
foreach_begin(struct entity_group *g, struct entity_iterator *iter) {
	memset(iter, 0, sizeof(*iter));
	iter->n = g->n;
}

static inline void
decode_eid(struct entity_group *g, struct entity_iterator *iter) {
	uint8_t *s = g->s;
	int i = iter->decode_pos++;
	if (s[i] < 128) {
		iter->eid += s[i] + 1;
		return;
	}
	int shift = 7;
	uint64_t diff = s[i] & 0x7f;
	for (;;) {
		++i;
		assert(i < iter->n);
		if (s[i] < 128) {
			diff |= s[i] << shift;
			iter->eid += diff + 1;
			iter->decode_pos = i + 1;
			return;
		} else {
			diff |= (s[i] & 0x7f) << shift;
		}
		shift += 7;
	}
}

static int
foreach_end(struct entity_group *g, struct entity_iterator *iter) {
	if (iter->decode_pos >= iter->n)
		return 0;
	iter->last_pos = iter->decode_pos;
	iter->last = iter->eid;
	decode_eid(g, iter);
	return 1;
}

static int
insert_group(struct entity_group_arena *G, int groupid, int begin, int end) {
	while (begin < end) {
		int mid = (begin + end) / 2;
		int v = G->g[mid]->groupid;
		if (v == groupid)
			return mid;
		if (v < groupid)
			begin = mid + 1;
		else
			end = mid;
	}
	// insert at begin
	if (G->n >= G->cap) {
		if (G->g == NULL) {
			G->cap = DEFAULT_GROUP_SIZE;
			G->g = (struct entity_group **)malloc(G->cap * sizeof(struct entity_group *));
		} else {
			G->cap = G->cap * 3 / 2 + 1;
			struct entity_group ** g = (struct entity_group **)malloc(G->cap * sizeof(struct entity_group *));
			memcpy(g, G->g, begin * sizeof(struct entity_group *));
			memcpy(g+begin+1, G->g + begin, (G->n - begin) * sizeof(struct entity_group *));
			free(G->g);
			G->g = g;
		}
	} else {
		memmove(G->g+begin+1, G->g+begin, (G->n - begin) * sizeof(struct entity_group *));
	}
	++G->n;
	struct entity_group *group = (struct entity_group *)malloc(sizeof(struct entity_group));
	memset(group, 0, sizeof(*group));
	group->groupid = groupid;

	G->g[begin] = group;

	return begin;
}

static struct entity_group *
find_group(struct entity_group_arena *G, int groupid) {
	int h = (uint32_t)(2654435769 * (uint32_t)groupid) >> (32 - ENTITY_GROUP_CACHE_BITS);
	int index;
	if (h >= G->n) {
		index = insert_group(G, groupid, 0, G->n);
	} else {
		int pos = G->cache[h];
		struct entity_group *g = G->g[pos];
		if (g->groupid == groupid) {
			return g;
		} else if (g->groupid < groupid) {
			index = insert_group(G, groupid, pos+1, G->n);
		} else {
			index = insert_group(G, groupid, 0, pos);
		}
	}

	// cache miss
	G->cache[h] = index;
	return G->g[index];
}

int
entity_group_add_(struct entity_group_arena *G, int groupid, uint64_t eid) {
	struct entity_group *g = find_group(G, groupid);
	if (eid <= g->last) {
		return 0;
	} else {
		add_eid(g, eid);
		return 1;
	}
}

struct tag_index_context {
	struct entity_group *group[GROUP_COMBINE];
	struct entity_iterator iter[GROUP_COMBINE];
	uint64_t lastid;
	int n;
	int pos;
	int index[GROUP_COMBINE];
};

static int
tag_index(struct entity_world *w, struct tag_index_context *ctx) {
	int i;
	uint64_t min_id = ctx->iter[ctx->index[0]].eid;
	int j = 0;
	for (i=1;i<ctx->n;i++) {
		uint64_t eid = ctx->iter[ctx->index[i]].eid;
		if (eid < min_id) {
			min_id = eid;
			j = i;
		}
	}
	int ii = ctx->index[j];
	struct entity_iterator * iter = &ctx->iter[ii];
	struct entity_group *group = ctx->group[ii];
	uint64_t diff = min_id - ctx->lastid + 1;
	int index = entity_id_find_guessrange(&w->eid, min_id, ctx->pos, ctx->pos + diff);
	int need_encode = iter->encode_pos != iter->last_pos;
	if (index >= 0) {
		if (need_encode) {
			// previous eid removed, encode current eid
			add_eid(group, min_id);
			iter->encode_pos = group->n;
		} else {
			iter->encode_pos = ctx->iter[ii].decode_pos;
		}
		ctx->lastid = min_id;
		ctx->pos = index + 1;
	} else if (!need_encode) {
		group->n = ctx->iter[ii].last_pos;
		group->last = ctx->iter[ii].last;
	}
	if (!foreach_end(group, iter)) {
		// This group is end, remove j from index
		--ctx->n;
		memmove(ctx->index+j, ctx->index+j+1, (ctx->n - j) * sizeof(int));
	}
	return index;
}

static inline void
dump_(struct entity_group_arena *G) {
	int i;
	for (i=0;i<G->n;i++) {
		struct entity_group *g = G->g[i];
		printf("Group %d:\n", g->groupid);
		struct entity_iterator iter;
		for (foreach_begin(g, &iter); foreach_end(g, &iter);) {
			printf("\t%llu\n", iter.eid);
		}
	}
}

static void
enable_(struct entity_world *w, int tagid, int n, int groupid[GROUP_COMBINE]) {
	struct tag_index_context ctx;
	ctx.n = 0;
	ctx.pos = 0;
	ctx.lastid = 0;
	int i;
	// find groups are not empty
	for (i=0;i<n;i++) {
		ctx.group[i] = find_group(&w->group, groupid[i]);
		foreach_begin(ctx.group[i], &ctx.iter[i]);
		if (foreach_end(ctx.group[i], &ctx.iter[i])) {
			ctx.index[ctx.n++] = i;
		}
	}
	while (ctx.n > 0) {
		// find minimal index
		int index = tag_index(w, &ctx);
		if (index >= 0) {
			ecs_add_component_id_(w, tagid, make_index_(index));
		}
	}
}

void
entity_group_enable_(struct entity_world *w, int tagid, int n, int groupid[]) {
	entity_clear_type_(w, tagid);
	int *p = groupid;
	while (n > GROUP_COMBINE) {
		enable_(w, tagid, GROUP_COMBINE, p);
		p += GROUP_COMBINE;
		n -= GROUP_COMBINE;
	}
	if (n > 0)
		enable_(w, tagid, n, p);
}

void
entity_group_id_(struct entity_group_arena *G, int groupid, lua_State *L) {
	struct entity_group	*g = find_group(G, groupid);
	lua_createtable(L, g->n, 0);
	struct entity_iterator iter;
	int i = 0;
	for (foreach_begin(g, &iter); foreach_end(g, &iter);) {
		lua_pushinteger(L, iter.eid);
		lua_rawseti(L, -2, ++i);
	}
	assert(iter.eid == g->last);
}


#ifdef TEST_GROUP_CODEC

static void
test_add_item() {
	struct entity_group g;
	memset(&g, 0, sizeof(g));
	uint64_t eid = 1;
	int i;
	for (i=0;i<10;i++) {
		add_eid(&g, eid);
		eid += i*2;
	}

	eid = 1;
	struct entity_iterator iter;
	i = 0;
	for (foreach_begin(&g, &iter); foreach_end(&g, &iter);) {
		assert(iter.eid == eid);
		eid += i*2;
		++i;
	}
}

static void
test_group_add() {
	struct entity_group_arena g;
	memset(&g, 0, sizeof(g));
	int groupid = 10000;
	int i;
	for (i=0;i<100000;i++) {
		entity_group_add(&g, groupid, i);
		groupid -= 3;
	}

	groupid = 10000;
	for (i=0;i<100000;i++) {
		struct entity_group * group = find_group(&g, groupid);
		struct entity_iterator iter;
		foreach_begin(group, &iter);
		foreach_end(group, &iter);
		assert(iter.eid == i);
		groupid -= 3;
	}
}

int
main() {
	test_add_item();
	test_group_add();
	return 0;
}

#endif

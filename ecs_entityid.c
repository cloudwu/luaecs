#include "ecs_entityid.h"
#include "ecs_entityindex.h"

#include <stdlib.h>

#define ENTITY_INIT_SIZE 4096

void
entity_id_deinit(struct entity_id *e) {
	free(e->id);
	e->id = NULL;
}

static int
find_eid_(struct entity_id *e, uint64_t eid, int begin, int end) {
	const uint64_t *id = e->id;
	while (begin < end) {
		int mid = (begin + end) / 2;
		if (eid == id[mid]) {
			return mid;
		}
		if (eid < id[mid])
			end = mid;
		else
			begin = mid+1;
	}
	int p = begin > e->n / 2 ? begin : begin + 1;
	return -p;
}

int
entity_id_find_guessrange(struct entity_id *e, uint64_t eid, int begin, int end) {
	if (end >= e->n) {
		return find_eid_(e, eid, begin, e->n);
	} else {
		uint64_t end_id = e->id[end];
		if (eid == end_id)
			return end;
		if (eid > end_id)
			return find_eid_(e, eid, end+1, e->n);
		return find_eid_(e, eid, begin, end);
	}
}

int
entity_id_find(struct entity_id *e, uint64_t eid) {
	unsigned h = (unsigned)(2654435761 * (uint32_t)eid) % ENTITY_ID_LOOKUP;
	entity_index_t p = e->lookup[h];
	int index = index_(p);
	int begin = 0;
	int end;
	if (index >= e->n) {
		end = e->n;
	} else {
		uint64_t v = e->id[index];
		if (v == eid) {
			return index;
		}
		if (v > eid) {
			end = index;
		} else {
			begin = index + 1;
			end = e->n;
		}
	}
	index = find_eid_(e, eid, begin, end);
	if (index < 0) {
		e->lookup[h] = make_index_(-index);
		return -1;
	} else {
		e->lookup[h] = make_index_(index);
		return index;
	}
}

int
entity_id_find_last(struct entity_id *e, uint64_t eid) {
	if (e->n == 0)
		return -1;
	int n = e->n - 1;
	if (e->id[n] == eid)
		return n;
	if (e->id[n] < eid)
		return -1;
	return find_eid_(e, eid, 0, n);
}

size_t
entity_id_memsize(struct entity_id *e) {
	return sizeof(uint64_t) * e->cap;
}

int
entity_id_alloc(struct entity_id *e, uint64_t *eid) {
	int n = e->n;
	if (n >= MAX_ENTITY) {
		return -1;
	}
	e->n++;

	if (n >= e->cap) {
		if (e->id == NULL) {
			e->cap = ENTITY_INIT_SIZE;
			e->id = (uint64_t *)malloc(e->cap * sizeof(uint64_t));
		} else {
		int newcap = e->cap * 3 / 2 + 1;
			e->id = (uint64_t *)realloc(e->id, newcap * sizeof(uint64_t));
			e->cap = newcap;
		}
	}
	*eid = ++e->last_id;
	e->id[n] = *eid;

	return n;
}

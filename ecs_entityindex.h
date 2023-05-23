#ifndef ecs_entity_index_h
#define ecs_entity_index_h

#include <string.h>
#include <stdint.h>

#define MAX_ENTITY 0xffffff 

typedef struct { uint8_t idx[3]; } entity_index_t;

static const entity_index_t INVALID_ENTITY = { { 0xff, 0xff, 0xff } };

static inline uint32_t
index_(entity_index_t x) {
	return (uint32_t) x.idx[0] << 16 | (uint32_t) x.idx[1] << 8 | x.idx[2]; 
}

static inline entity_index_t
make_index_(uint32_t v) {
	entity_index_t r = {{ (v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff }};
	return r;
}

static inline int
INVALID_ENTITY_INDEX(entity_index_t e) {
	return index_(e) == MAX_ENTITY;
}

static inline int
ENTITY_INDEX_CMP(entity_index_t a, entity_index_t b) {
	return memcmp(&a, &b, sizeof(a));
}

static inline entity_index_t
DEC_ENTITY_INDEX(entity_index_t e, int delta) {
	return make_index_(index_(e) - delta);
}

#endif

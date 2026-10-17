#ifndef MAP_H
#define MAP_H

#include <stddef.h>

// auto grow on put
struct HashMap;

// returns malloced object (move), must be freed with hashmap_free
// value_deallocator - is function to free values in hashmap_free, if NULL - no free value preformed
struct HashMap *hashmap_new(void (*value_deallocator)(void*));

// returns amount of elements, not the capacity
size_t hashmap_size(struct HashMap *hm);

// key copied and owned, value owned, returns old value (move)
void *hashmap_put(struct HashMap *hm, const char *key, void *val);

// key borrowed, return value (lent mut, don't free)
void *hashmap_get(struct HashMap *hm, const char *key);

// key borrowed, return value (moved, must be freed)
void *hashmap_remove(struct HashMap *hm, const char *key);

// frees memory of nodes and array, frees keys, frees values
void hashmap_free(struct HashMap *hm);

// for tests only

bool hashmap_has_collisions(struct HashMap *hm);
bool hashmap_all_nulls(struct HashMap *hm);

#endif

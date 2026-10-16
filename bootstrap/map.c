#include "map.h"
#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <assert.h>

#define GROW_FACTOR 0.75

// default capacity can be only the power of 2
// because of index function
#define DEFAULT_CAPACITY 16

typedef struct HMNode {
  char *key;
  void *val;
  struct HMNode *next;
} HMNode;

typedef struct HashMap {
  size_t cap;
  size_t size;
  void (*vfree)(void*);
  HMNode **nodes;
} HashMap;

#define index(n, h) (n - 1) & h

static int hash(const char* s) {
  if (s == NULL || *s == '\0') {
    return 0;
  } else if (s[1] == '\0') {
    return *s & 0xff;
  } else {
    int result = 0;
    int i = 0;
    while (s[i] != '\0') {
      result = 31 * result + s[i];
      i += 1;
    }
    return result ^ (result >> 16);
  }
}

static void grow(HashMap *hm) {
  HMNode **old_nodes = hm->nodes;
  size_t old_cap = hm->cap;
  hm->cap *= 2;
  hm->nodes = malloc(hm->cap * sizeof (*hm->nodes));
  memset(hm->nodes, 0, hm->cap * sizeof(*hm->nodes));
  hm->size = 0;
  for (size_t i = 0; i < old_cap; i++) {
    HMNode *n = old_nodes[i];
    while (n != NULL) {
      hashmap_put(hm, n->key, n->val);
      HMNode *next = n->next;
      free(n->key);
      free(n);
      n = next;
    }
  }
  free(old_nodes);
}

static void do_nothing(void*) {}

struct HashMap *hashmap_new(void (*value_deallocator)(void*)) {
  HashMap *hm = malloc(sizeof(HashMap));
  hm->cap = DEFAULT_CAPACITY;
  hm->size = 0;
  hm->vfree = do_nothing;
  if (value_deallocator != NULL)
    hm->vfree = value_deallocator;
  hm->nodes = malloc(hm->cap * sizeof (*hm->nodes));
  memset(hm->nodes, 0, hm->cap * sizeof(*hm->nodes));
  return hm;
}

size_t hashmap_size(struct HashMap *hm) {
  assert(hm != NULL);
  return hm->size;
}

void *hashmap_put(HashMap *hm, const char *key, void *val) {
  assert(hm != NULL && key != NULL);
  if (hm->size > hm->cap * GROW_FACTOR) {
    grow(hm);
  }

  int h = hash(key);
  size_t i = index(hm->cap, h);
  HMNode *n = hm->nodes[i];

  if (n == NULL) {
    n = malloc(sizeof(HMNode));
    n->key = strdup(key);
    n->val = val;
    n->next = NULL;
    hm->nodes[i] = n;
    hm->size += 1;
    return NULL;
  }

  bool found = strcmp(n->key, key) == 0;
  while (n->next != NULL && !found) {
    n = n->next;
    found = strcmp(n->key, key) == 0;
  }

  if (found) {
    void *old_val = n->val;
    n->val = val;
    return old_val;
  } else {
    HMNode *new_val = malloc(sizeof(HMNode));
    new_val->key = strdup(key);
    new_val->val = val;
    new_val->next = n->next;
    n->next = new_val;
    hm->size += 1;
    return NULL;
  }
}

void *hashmap_get(HashMap *hm, const char *key) {
  assert(hm != NULL);
  int h = hash(key);
  size_t i = index(hm->cap, h);
  HMNode *n = hm->nodes[i];
  if (n == NULL) {
    return NULL;
  }

  bool found = strcmp(n->key, key) == 0;
  while (n->next != NULL && !found) {
    n = n->next;
    found = strcmp(n->key, key) == 0;
  }

  if (!found) return NULL;

  return n->val;
}

void *hashmap_remove(struct HashMap *hm, const char *key) {
  assert(hm != NULL);
  int h = hash(key);
  size_t i = index(hm->cap, h);
  HMNode *n = hm->nodes[i];
  if (n == NULL) {
    return NULL;
  }

  bool found = strcmp(n->key, key) == 0;
  if (found) {
    HMNode *next = n->next;
    void *val = n->val;
    free(n->key);
    free(n);
    hm->nodes[i] = next;
    hm->size -= 1;
    return val;
  }

  while (n->next != NULL && !found) {
    n = n->next;
    found = strcmp(n->key, key) == 0;
  }

  if (!found) return NULL;

  HMNode *next = n->next->next;
  void *val = n->next->val;
  free(n->next->key);
  free(n->next);
  n->next = next;
  hm->size -= 1;
  return val;
}

bool hashmap_contains(HashMap *, const char *) {
  assert(false);
}

void hashmap_free(HashMap *hm) {
  assert(hm != NULL);
  for (size_t i = 0; i < hm->cap; i++) {
    HMNode *n = hm->nodes[i];
    while (n != NULL) {
      HMNode *next = n->next;
      free(n->key);
      hm->vfree(n->val);
      free(n);
      n = next;
    }
  }
  free(hm->nodes);
  free(hm);
}

bool hashmap_has_collisions(struct HashMap *hm) {
  assert(hm != NULL);
  for (size_t i = 0; i < hm->cap; i++) {
    struct HMNode *n = hm->nodes[i];
    if (n != NULL && n->next != NULL) return true;
  }
  return false;
}

bool hashmap_all_nulls(struct HashMap *hm) {
  assert(hm != NULL);
  for (size_t i = 0; i < hm->cap; i++) {
    if (hm->nodes[i] != NULL) return false;
  }
  return true;
}

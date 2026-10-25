#ifndef HMAP_H
#define HMAP_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

  static const size_t HASH_MAP_DEFAULT_CAPACITY = 8;
  static const float HASH_MAP_GROW_FACTOR = 0.75f;

#define DEFINE_HMAP(Type, Name)                                      \
                                                                     \
  typedef struct Name##Node {                                       \
    char *key;                                                      \
    Type value;                                                     \
    struct Name##Node *next;                                        \
  } Name##Node;                                                     \
                                                                     \
  typedef struct Name {                                             \
    Name##Node **nodes;                                             \
    size_t size;                                                    \
    size_t cap;                                                     \
    void (*vdealloc)(void*);                                        \
  } Name;                                                           \
                                                                     \
  static inline size_t Name##_index(size_t n, uint64_t h) {         \
    return (n - 1) & h;                                             \
  }                                                                 \
                                                                     \
  static inline uint64_t Name##_hash(const char* s) {               \
    if (s == NULL || *s == '\0') {                                  \
      return 0;                                                     \
    }                                                                 \
    uint64_t result = 1469598103934665603ULL;                       \
    int i = 0;                                                      \
    while (s[i] != '\0') {                                          \
      result ^= (uint64_t)(unsigned char)s[i];                     \
      result *= 1099511628211ULL;                                   \
      i += 1;                                                       \
    }                                                               \
    return result;                                                  \
  }                                                                 \
                                                                     \
  static inline void Name##_grow(Name *hm) {                               \
    if (hm == NULL || hm->cap == 0) return;                         \
    Name##Node **old_nodes = hm->nodes;                             \
    size_t old_cap = hm->cap;                                       \
    size_t new_cap = hm->cap * 2;                                   \
    hm->nodes = (Name##Node **)malloc(new_cap * sizeof(Name##Node*)); \
    assert(hm->nodes != NULL);                                      \
    memset(hm->nodes, 0, new_cap * sizeof(Name##Node*));            \
    hm->cap = new_cap;                                              \
    size_t old_size = hm->size;                                     \
    hm->size = 0;                                                   \
    (void)old_size;                                                 \
    void (*saved_dealloc) = hm->vdealloc;                            \
    for (size_t i = 0; i < old_cap; i++) {                         \
      Name##Node *n = old_nodes[i];                                 \
      while (n != NULL) {                                           \
        Name##Node *next = n->next;                                 \
        uint64_t h = Name##_hash(n->key);                           \
        size_t idx = Name##_index(hm->cap, h);                     \
        Name##Node *head = hm->nodes[idx];                          \
        n->next = NULL;                                             \
        if (head == NULL) {                                         \
          hm->nodes[idx] = n;                                       \
        } else {                                                    \
          while (head->next != NULL) head = head->next;             \
          head->next = n;                                           \
        }                                                           \
        hm->size += 1;                                              \
        n = next;                                                   \
      }                                                             \
    }                                                               \
    free(old_nodes);                                                \
    hm->vdealloc = saved_dealloc;                                   \
  }                                                                 \
                                                                    \
  static inline Name *Name##_new(void *value_deallocator) {         \
    Name *hm = (Name *)malloc(sizeof(Name));                        \
    assert(hm != NULL);                                             \
    hm->cap = HASH_MAP_DEFAULT_CAPACITY;                              \
    hm->size = 0;                                                   \
    hm->vdealloc = value_deallocator;                               \
    hm->nodes = (Name##Node **)malloc(hm->cap * sizeof(Name##Node*)); \
    assert(hm->nodes != NULL);                                      \
    memset(hm->nodes, 0, hm->cap * sizeof(Name##Node*));            \
    return hm;                                                      \
  }                                                                 \
                                                                    \
  static inline size_t Name##_size(Name *hm) {                      \
    assert(hm != NULL);                                             \
    return hm->size;                                                \
  }                                                                 \
                                                                    \
  static inline Type Name##_put(Name *hm, const char *key, Type val) {\
    assert(hm != NULL && key != NULL);                              \
    if (hm->cap > 0 && hm->size + 1 > (size_t)(hm->cap * HASH_MAP_GROW_FACTOR)) { \
      Name##_grow(hm);                                              \
    }                                                               \
                                                                    \
    uint64_t h = Name##_hash(key);                                  \
    size_t i = Name##_index(hm->cap, h);                            \
    Name##Node *n = hm->nodes[i];                                   \
                                                                    \
    if (n == NULL) {                                                \
      n = (Name##Node *)malloc(sizeof(Name##Node));                 \
      assert(n != NULL);                                            \
      n->key = strdup(key);                                         \
      n->value = val;                                               \
      n->next = NULL;                                               \
      hm->nodes[i] = n;                                             \
      hm->size += 1;                                                \
      return (Type)0;                                               \
    }                                                               \
                                                                    \
    Name##Node *cur = n;                                            \
    while (cur != NULL) {                                           \
      if (strcmp(cur->key, key) == 0) {                             \
        Type old_val = cur->value;                                  \
        cur->value = val;                                           \
        return old_val;                                             \
      }                                                             \
      cur = cur->next;                                              \
    }                                                               \
                                                                    \
    Name##Node *new_node = (Name##Node *)malloc(sizeof(Name##Node));\
    assert(new_node != NULL);                                       \
    new_node->key = strdup(key);                                    \
    new_node->value = val;                                          \
    new_node->next = n->next;                                       \
    n->next = new_node;                                             \
    hm->size += 1;                                                  \
    return (Type)0;                                                 \
  }                                                                 \
                                                                    \
  static inline Type Name##_get(Name *hm, const char *key) {        \
    assert(hm != NULL);                                             \
    if (key == NULL) return (Type)0;                                \
    uint64_t h = Name##_hash(key);                                  \
    size_t i = Name##_index(hm->cap, h);                            \
    Name##Node *n = hm->nodes[i];                                   \
    while (n != NULL) {                                             \
      if (strcmp(n->key, key) == 0) {                               \
        return n->value;                                            \
      }                                                             \
      n = n->next;                                                  \
    }                                                               \
    return (Type)0;                                                 \
  }                                                                 \
                                                                    \
  static inline Type Name##_remove(Name *hm, const char *key) {     \
    assert(hm != NULL);                                             \
    if (key == NULL) return (Type)0;                                \
    uint64_t h = Name##_hash(key);                                  \
    size_t i = Name##_index(hm->cap, h);                            \
    Name##Node *n = hm->nodes[i];                                   \
    if (n == NULL) {                                                \
      return (Type)0;                                               \
    }                                                               \
                                                                    \
    if (strcmp(n->key, key) == 0) {                                 \
      Name##Node *next = n->next;                                   \
      Type val = n->value;                                          \
      free(n->key);                                                 \
      free(n);                                                      \
      hm->nodes[i] = next;                                          \
      hm->size -= 1;                                                \
      return val;                                                   \
    }                                                               \
                                                                    \
    Name##Node *prev = n;                                           \
    n = n->next;                                                    \
    while (n != NULL) {                                             \
      if (strcmp(n->key, key) == 0) {                               \
        prev->next = n->next;                                       \
        Type val = n->value;                                        \
        free(n->key);                                               \
        free(n);                                                    \
        hm->size -= 1;                                              \
        return val;                                                 \
      }                                                             \
      prev = n;                                                     \
      n = n->next;                                                  \
    }                                                               \
    return (Type)0;                                                 \
  }                                                                 \
                                                                    \
  static inline void Name##_free(Name *hm) {                        \
    if (hm == NULL) return;                                         \
    for (size_t i = 0; i < hm->cap; i++) {                          \
      Name##Node *n = hm->nodes[i];                                 \
      while (n != NULL) {                                           \
        Name##Node *next = n->next;                                 \
        free(n->key);                                               \
        if (hm->vdealloc != NULL) {                                 \
          hm->vdealloc((void*)(uint64_t)n->value);                  \
        }                                                           \
        free(n);                                                    \
        n = next;                                                   \
      }                                                             \
    }                                                               \
    free(hm->nodes);                                                \
    free(hm);                                                       \
  }                                                                 \
                                                                    \
  static __attribute__((unused)) bool Name##_has_collisions(Name *hm) {                     \
    if (hm == NULL) return false;                                   \
    for (size_t i = 0; i < hm->cap; i++) {                          \
      Name##Node *n = hm->nodes[i];                                 \
      if (n != NULL && n->next != NULL) return true;                \
    }                                                               \
    return false;                                                   \
  }                                                                 \
                                                                    \
  static __attribute__((unused)) bool Name##_all_nulls(Name *hm) {  \
    if (hm == NULL) return true;                                    \
    for (size_t i = 0; i < hm->cap; i++) {                          \
      if (hm->nodes[i] != NULL) return false;                       \
    }                                                               \
    return true;                                                    \
  }

#endif

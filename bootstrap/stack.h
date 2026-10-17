#ifndef STACK_H
#define STACK_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define DEFINE_STACK(Type, deallocator, Name)                       \
  typedef struct {                                                  \
    Type *data;                                                     \
    size_t len;                                                     \
    size_t cap;                                                     \
  } Name;                                                           \
                                                                    \
  static inline Name * Name##_new() {                               \
    Name *s = malloc(sizeof(Name));                                 \
    s->data = NULL;                                                 \
    s->len = 0;                                                     \
    s->cap = 0;                                                     \
    return s;                                                       \
  }                                                                 \
                                                                    \
  static inline void Name##_free(Name *s) {                         \
    if (s == NULL) return;                                          \
    if(s->data == NULL) { free(s); return; }                        \
    for (size_t i = 0; i < s->len; i++) {                           \
      deallocator(s->data[i]);                                      \
    }                                                               \
    free(s->data);                                                  \
    s->data = NULL;                                                 \
    s->len = 0;                                                     \
    s->cap = 0;                                                     \
    free(s);                                                        \
  }                                                                 \
                                                                    \
  static inline bool Name##_push(Name *s, Type value) {             \
    if (s->len == s->cap) {                                         \
      size_t max_cap = SIZE_MAX / sizeof(Type);                     \
      if (s->cap == max_cap) return false;                          \
                                                                    \
      size_t new_cap = s->cap ? s->cap * 2 : 8;                     \
      if (new_cap < s->cap || new_cap > max_cap)                    \
          new_cap = max_cap;                                        \
                                                                    \
      Type *new_data = realloc(s->data, new_cap * sizeof(Type));    \
      if (!new_data) return false;                                  \
                                                                    \
      s->data = new_data;                                           \
      s->cap = new_cap;                                             \
    }                                                               \
                                                                    \
    s->data[s->len++] = value;                                      \
    return true;                                                    \
  }                                                                 \
                                                                    \
  static inline Type Name##_top(Name *s) {                          \
    if (s != NULL && s->data != NULL && s->len > 0)                 \
      return s->data[s->len - 1];                                   \
    return NULL;                                                    \
  }                                                                 \
                                                                    \
  static inline bool Name##_pop(Name *s, Type *out) {               \
    if (s->len == 0) return false;                                  \
    *out = s->data[--s->len];                                       \
    return true;                                                    \
  }


#endif


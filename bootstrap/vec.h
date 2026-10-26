#ifndef VEC_H_
#define VEC_H_

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define DEFINE_VEC(Type, Name) \
typedef struct Name {\
  void (*vdealoc)(Type);\
  size_t len;\
  size_t cap;\
  Type  *values;\
} Name;\
\
static Name *Name##_new(size_t capacity, void (*value_deallocator)(void*)) { \
  Name *vec = malloc(sizeof(Name));\
  if (vec == NULL) return NULL;\
  vec->len = 0;\
  if (capacity > 0) vec->cap = capacity;\
  else              vec->cap = 8;\
  vec->values = malloc(vec->cap * sizeof(Type));\
  if (vec->values == NULL) {\
    free(vec);\
    return NULL;\
  }\
  vec->vdealoc = (void(*)(Type))(void*)value_deallocator;\
  return vec;\
}\
\
static __attribute__((unused)) inline void Name##_reserve(Name *vec, size_t capacity) {\
  assert(vec != NULL);\
  if (vec->cap < capacity) {\
    size_t new_cap = vec->cap * 2;\
    if (new_cap < capacity) new_cap = capacity;\
    Type *tmp = realloc(vec->values, new_cap * sizeof(Type));\
    assert(tmp != NULL);\
    vec->values = tmp;\
    vec->cap = new_cap;\
  }\
}\
\
static __attribute__((unused)) inline size_t Name##_len(Name* vec) {\
  assert(vec != NULL);\
  return vec->len;\
}\
\
static __attribute__((unused)) inline size_t Name##_cap(Name* vec) {\
  assert(vec != NULL);\
  return vec->cap;\
}\
\
static __attribute__((unused)) inline void Name##_append(Name *vec, Type v) {\
  assert(vec != NULL);\
  if (vec->len >= vec->cap) {\
    size_t new_cap = vec->cap * 2;\
    if (new_cap == 0) new_cap = 8;\
    Type *tmp = realloc(vec->values, new_cap * sizeof(Type));\
    assert(tmp != NULL);\
    vec->values = tmp;\
    vec->cap = new_cap;\
  }\
  vec->values[vec->len++] = v;\
}\
\
static __attribute__((unused)) inline Type Name##_get(Name* vec, size_t i) {\
  assert(vec != NULL);\
  if (i < vec->len) return vec->values[i];\
  abort();\
}\
\
static __attribute__((unused)) inline Type *Name##_get_ptr(Name* vec, size_t i) {\
  assert(vec != NULL);\
  if (i < vec->len) return &vec->values[i];\
  abort();\
}\
\
static __attribute__((unused)) inline void Name##_set(Name* vec, size_t i, Type v) {\
  assert(vec != NULL);\
  if (i >= vec->len) abort();\
  vec->values[i] = v;\
}\
\
static void Name##_free(Name *vec) {\
  if (vec == NULL) return;\
  if (vec->vdealoc != NULL) {\
    for (size_t i = 0; i < vec->len; i++) {\
      if ((void*)(uintptr_t)vec->values[i] != NULL) {\
        vec->vdealoc(vec->values[i]);\
      }\
    }\
  }\
  free(vec->values);\
  free(vec);\
}

#endif

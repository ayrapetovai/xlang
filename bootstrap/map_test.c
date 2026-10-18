#include "map.h"
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

void test_basic_happy_path() {
  struct HashMap *hm = hashmap_new(NULL);
  assert(hashmap_size(hm) == 0);
  hashmap_put(hm, "abc", "123");
  assert(hashmap_size(hm) == 1);
  hashmap_put(hm, "def", "456");
  assert(hashmap_size(hm) == 2);

  assert(strcmp(hashmap_get(hm, "abc"), "123") == 0);
  assert(strcmp(hashmap_get(hm, "def"), "456") == 0);
  assert(hashmap_size(hm) == 2);

  assert(hashmap_get(hm, "1k2j31k") == NULL);
  assert(hashmap_size(hm) == 2);

  hashmap_remove(hm, "abc"); // no free neaded, str in pool
  assert(hashmap_size(hm) == 1);
  assert(hashmap_get(hm, "abc") == NULL);
  assert(hashmap_size(hm) == 1);

  hashmap_put(hm, "ghi", "789");
  assert(hashmap_size(hm) == 2);
  assert(strcmp(hashmap_get(hm, "ghi"), "789") == 0);

  hashmap_free(hm);
}

void test_put_grow() {
  size_t max_size = 5000;
  struct HashMap *hm = hashmap_new(free);
  assert(hashmap_size(hm) == 0);

  for (size_t i = 0; i < max_size; i++) {
    char key[10] = {};
    sprintf(key, "%ld", i);
    void *old = hashmap_put(hm, key, strdup(key));
    assert(old == NULL);
    assert(hashmap_size(hm) == i + 1);
  }

  for (size_t i = 0; i < max_size; i++) {
    char key[10] = {};
    sprintf(key, "%ld", i);
    assert(strcmp(hashmap_get(hm, key), key) == 0);
  }
  hashmap_free(hm);
}

void test_put_same() {
  struct HashMap *hm = hashmap_new(NULL);
  hashmap_put(hm, "abc", "123");
  assert(hashmap_size(hm) == 1);
  assert(strcmp(hashmap_get(hm, "abc"), "123") == 0);

  char *old = hashmap_put(hm, "abc", "456");
  assert(strcmp(old, "123") == 0);
  assert(hashmap_size(hm) == 1);
  assert(strcmp(hashmap_get(hm, "abc"), "456") == 0);

  hashmap_free(hm);
}

void test_remove() {
  size_t max_size = 1024;
  struct HashMap *hm = hashmap_new(NULL);
  for (size_t i = 0; i < max_size; i++) {
    char key[10] = {};
    sprintf(key, "%ld", i);
    hashmap_put(hm, key, strdup(key));
  }
  assert(hashmap_has_collisions(hm));
  for (size_t i = 0; i < max_size; i++) {
    char key[10] = {};
    sprintf(key, "%ld", i);
    char *removed = hashmap_remove(hm, key);
    assert(hashmap_size(hm) == max_size - (i + 1));
    assert(strcmp(removed, key) == 0);
    free(removed);
  }
  assert(hashmap_size(hm) == 0);
  assert(hashmap_all_nulls(hm));
  hashmap_free(hm);
}

void test_get_absent() {
  struct HashMap *hm = hashmap_new(NULL);
  void *got = hashmap_get(hm, "abc");
  assert(got == NULL);
  got = hashmap_get(hm, "");
  assert(got == NULL);
  got = hashmap_get(hm, NULL);
  assert(got == NULL);
  hashmap_free(hm);
}

void test_put_null_or_empty() {
  struct HashMap *hm = hashmap_new(NULL);
  void* old = hashmap_put(hm, "", "abc");
  assert(old == NULL);
  assert(strcmp(hashmap_get(hm, ""), "abc") == 0);
  // old = hashmap_put(hm, NULL, "def"); // asserts key is not NULL
  hashmap_free(hm);
}

int main() {
  test_basic_happy_path();
  test_put_grow();
  test_put_same();
  test_remove();
  test_get_absent();
  test_put_null_or_empty();
}

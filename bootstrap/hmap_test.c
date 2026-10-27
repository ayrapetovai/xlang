#include "hmap.h"
#include <stddef.h>
#include <stdio.h>

DEFINE_HMAP(char*, StrToStrMap);

void test_basic_happy_path() {
  struct StrToStrMap *hm = StrToStrMap_new(NULL);
  assert(StrToStrMap_size(hm) == 0);
  StrToStrMap_put(hm, "abc", "123");
  assert(StrToStrMap_size(hm) == 1);
  StrToStrMap_put(hm, "def", "456");
  assert(StrToStrMap_size(hm) == 2);

  assert(strcmp(StrToStrMap_get(hm, "abc"), "123") == 0);
  assert(strcmp(StrToStrMap_get(hm, "def"), "456") == 0);
  assert(StrToStrMap_size(hm) == 2);

  assert(StrToStrMap_get(hm, "ubsend in map") == NULL);
  assert(StrToStrMap_size(hm) == 2);

  StrToStrMap_remove(hm, "abc"); // no free neaded, str in pool
  assert(StrToStrMap_size(hm) == 1);
  assert(StrToStrMap_get(hm, "abc") == NULL);
  assert(StrToStrMap_size(hm) == 1);

  StrToStrMap_put(hm, "ghi", "789");
  assert(StrToStrMap_size(hm) == 2);
  assert(strcmp(StrToStrMap_get(hm, "ghi"), "789") == 0);

  StrToStrMap_free(hm);
}

void test_put_grow() {
  size_t max_size = 5000;
  struct StrToStrMap *hm = StrToStrMap_new(free);
  assert(StrToStrMap_size(hm) == 0);

  for (size_t i = 0; i < max_size; i++) {
    char key[10] = {};
    sprintf(key, "%ld", i);
    void *old = StrToStrMap_put(hm, key, strdup(key));
    assert(old == NULL);
    assert(StrToStrMap_size(hm) == i + 1);
  }

  for (size_t i = 0; i < max_size; i++) {
    char key[10] = {};
    sprintf(key, "%ld", i);
    assert(strcmp(StrToStrMap_get(hm, key), key) == 0);
  }
  StrToStrMap_free(hm);
}

DEFINE_HMAP(int, StrToIntMap);

void test_put_same() {
  struct StrToIntMap *hm = StrToIntMap_new(NULL);
  StrToIntMap_put(hm, "abc", 123);
  assert(StrToIntMap_size(hm) == 1);
  assert(StrToIntMap_get(hm, "abc") == 123);

  int old = StrToIntMap_put(hm, "abc", 456);
  assert(old == 123);
  assert(StrToIntMap_size(hm) == 1);
  assert(StrToIntMap_get(hm, "abc") ==  456);

  StrToIntMap_put(hm, "foo", 1);
  StrToIntMap_remove(hm, "foo");

  StrToIntMap_free(hm);
}

void test_remove() {
  size_t max_size = 1024;
  struct StrToStrMap *hm = StrToStrMap_new(NULL);
  for (size_t i = 0; i < max_size; i++) {
    char key[10] = {};
    sprintf(key, "%ld", i);
    StrToStrMap_put(hm, key, strdup(key));
  }
  assert(StrToStrMap_has_collisions(hm));
  for (size_t i = 0; i < max_size; i++) {
    char key[10] = {};
    sprintf(key, "%ld", i);
    char *removed = StrToStrMap_remove(hm, key);
    assert(StrToStrMap_size(hm) == max_size - (i + 1));
    assert(strcmp(removed, key) == 0);
    free(removed);
  }
  assert(StrToStrMap_size(hm) == 0);
  assert(StrToStrMap_all_nulls(hm));
  StrToStrMap_free(hm);
}

void test_get_absent() {
  struct StrToStrMap *hm = StrToStrMap_new(NULL);
  char *got = StrToStrMap_get(hm, "abc");
  assert(got == NULL);
  got = StrToStrMap_get(hm, "");
  assert(got == NULL);
  got = StrToStrMap_get(hm, NULL);
  assert(got == NULL);
  StrToStrMap_free(hm);
}

void test_put_null_or_empty() {
  struct StrToStrMap *hm = StrToStrMap_new(NULL);
  char* old = StrToStrMap_put(hm, "", "abc");
  assert(old == NULL);
  assert(strcmp(StrToStrMap_get(hm, ""), "abc") == 0);
  // old = StrToStrMap_put(hm, NULL, "def"); // asserts key is not NULL
  StrToStrMap_free(hm);
}

const int test_compute_if_absetn_var1 = 42;
const int test_compute_if_absetn_var2 = 66;
int create_int1(const char*) { return test_compute_if_absetn_var1; }
int create_int2(const char*) { return test_compute_if_absetn_var1; }
void test_compute_if_absent() {
  StrToIntMap *m = StrToIntMap_new(NULL);

  int created_value = StrToIntMap_compute_if_absent(m, "abc", create_int1);
  assert(created_value == test_compute_if_absetn_var1);

  int old_value = StrToIntMap_compute_if_absent(m, "abc", create_int2);
  assert(old_value == test_compute_if_absetn_var1);

  int new_value = StrToIntMap_compute_if_absent(m, "d", create_int2);
  assert(new_value == test_compute_if_absetn_var2);

  StrToIntMap_free(m);
}

int main() {
  test_basic_happy_path();
  test_put_grow();
  test_put_same();
  test_remove();
  test_get_absent();
  test_put_null_or_empty();
}

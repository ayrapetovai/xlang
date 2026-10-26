#include "vec.h"
#include <stddef.h>

DEFINE_VEC(int, IntVec);

void test_append_elements() {
  IntVec *v = IntVec_new(0, NULL);
  size_t max_numbers = 1000;
  for (size_t i = 0; i < max_numbers; i++)
    IntVec_append(v, i * 2);
  for (size_t i = 0; i < max_numbers; i++)
    assert(IntVec_get(v, i) == (int)i * 2);
  IntVec_free(v);
}

typedef struct { } TestStruct;
DEFINE_VEC(TestStruct*, PtrVec);

size_t test_free_deallocation_counter = 0;
void test_free_counting_deallocator(void *v) { free(v); test_free_deallocation_counter += 1; }
void test_free_elements() {
  test_free_deallocation_counter = 0;
  PtrVec *v = PtrVec_new(10, test_free_counting_deallocator);
  PtrVec_append(v, malloc(sizeof(TestStruct)));
  PtrVec_append(v, malloc(sizeof(TestStruct)));
  PtrVec_append(v, malloc(sizeof(TestStruct)));
  PtrVec_free(v);
  assert(test_free_deallocation_counter == 3);
}

int main() {
  test_append_elements();
  test_free_elements();
}

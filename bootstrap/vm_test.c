#include "vm.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

void print_cstring(VMEngine *vm) {
  static const char *s = "abc";
  vm_add_command(vm, (VMCommand){BC_PUSH,  1, {(uint64_t)(uintptr_t)s}});
  vm_add_command(vm, (VMCommand){BC_ECHOS, 0, {}});
}

void print_sum_of_two_integers(VMEngine *vm) {
  // echo (!51 + 5)
  vm_add_command(vm, (VMCommand){BC_PUSH,  1, {51}});
  vm_add_command(vm, (VMCommand){BC_NOT,   0, {0}});
  vm_add_command(vm, (VMCommand){BC_DUP,   0, {}});
  vm_add_command(vm, (VMCommand){BC_ECHOI, 0, {}});
  vm_add_command(vm, (VMCommand){BC_PUSH,  1, {5}});
  vm_add_command(vm, (VMCommand){BC_ADD,   0, {}});
  vm_add_command(vm, (VMCommand){BC_ECHOI, 0, {}});
}

void print_string(VMEngine *vm) {
  size_t hw = vm_add_string(vm, "Hello, World!", 13);
  vm_add_command(vm, (VMCommand){BC_CONSTA, 1, {hw}});
  vm_add_command(vm, (VMCommand){BC_ARRBEG, 0, {}});
  vm_add_command(vm, (VMCommand){BC_ECHOS,  0, {}});
}

int main() {
  VMEngine *vm = vm_new(1024, 1024, 1024);
  if (vm == NULL) return 1;

  print_cstring(vm);
  print_sum_of_two_integers(vm);
  print_string(vm);

  vm_add_command(vm, (VMCommand){BC_HALT, 0, {}});
  VMState s = vm_execute(vm, 100);

  int rc = 0;
  if (s == VM_STATE_ERROR) {
    printf("execution error: %s\n", vm->error);
    rc = 2;
  }

  vm_free(vm);
  return rc;
}

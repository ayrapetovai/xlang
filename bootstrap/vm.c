#include "vm.h"
#include <inttypes.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static inline VMStackSlot pop_slot(VMEngine *vm) {
  if (vm->stopped) return 0;
  if (vm->sp == 0) {
    vm->stopped = true;
    snprintf(vm->error, sizeof(vm->error), "stack pointer underflow");
    return 0;
  }
  vm->sp -= 1;
  return *(VMStackSlot*)(vm->stack + vm->sp);
}

static inline void push_slot(VMEngine *vm, VMStackSlot x) {
  if (vm->stopped) return;
  if (vm->sp >= vm->stack_len) {
    vm->stopped = true;
    snprintf(vm->error, sizeof(vm->error), "stack pointer overflow");
    return;
  }

  *(VMStackSlot*)(vm->stack + vm->sp) = x;
  vm->sp += 1;
}

static void exec_push(VMEngine *vm) {
  if (vm->ip + sizeof(VMStackSlot) > vm->code_len) {
    vm->stopped = true;
    snprintf(vm->error, sizeof(vm->error), "instruction pointer overflow");
    return;
  }
  uint64_t operand;
  memcpy(&operand, vm->code + vm->ip, sizeof operand);
  push_slot(vm, operand);
  if (vm->stopped) return;
  vm->ip += sizeof(operand);
}

static void exec_not(VMEngine *vm) {
  VMStackSlot op = pop_slot(vm);
  push_slot(vm, !op);
}

static void exec_add(VMEngine *vm) {
  int64_t op1 = pop_slot(vm);
  int64_t op2 = pop_slot(vm);
  push_slot(vm, op1 + op2);
}

static void exec_sub(VMEngine *vm) {
  int64_t op1 = pop_slot(vm);
  int64_t op2 = pop_slot(vm);
  push_slot(vm, op2 - op1);
}

static void exec_mul(VMEngine *vm) {
  int64_t op1 = pop_slot(vm);
  int64_t op2 = pop_slot(vm);
  push_slot(vm, op1 * op2);
}

static void exec_div(VMEngine *vm) {
  int64_t op1 = pop_slot(vm);
  if (vm->stopped) return;
  if (op1 == 0) {
    size_t where = vm->sp;
    push_slot(vm, op1);
    vm->stopped = true;
    snprintf(vm->error, sizeof(vm->error), "division by zero, operand at stack offset %zu is 0", where);
    return;
  }
  int64_t op2 = pop_slot(vm);
  if (vm->stopped) return;
  push_slot(vm, op2 / op1);
}

static void exec_dup(VMEngine *vm) {
  VMStackSlot op = pop_slot(vm);
  push_slot(vm, op);
  push_slot(vm, op);
}

static void exec_const(VMEngine *vm) {
  if (vm->ip + sizeof(uint64_t) > vm->code_len) {
    vm->stopped = true;
    snprintf(vm->error, sizeof(vm->error), "instruction pointer overflow");
    return;
  }
  uint64_t offset;
  memcpy(&offset, vm->code + vm->ip, sizeof offset);
  if (offset + sizeof(uint64_t) > vm->pool_len) {
    vm->stopped = true;
    snprintf(vm->error, sizeof(vm->error), "constant pool offset overflow");
    return;
  }
  uint64_t value;
  memcpy(&value, vm->pool + offset, sizeof value);
  push_slot(vm, value);
  if (vm->stopped) return;
  vm->ip += sizeof(uint64_t);
}

static void exec_consta(VMEngine *vm) {
  if (vm->ip + sizeof(size_t) > vm->code_len) {
    vm->stopped = true;
    snprintf(vm->error, sizeof(vm->error), "instruction pointer overflow");
    return;
  }
  size_t offset;
  memcpy(&offset, vm->code + vm->ip, sizeof offset);
  if (offset + sizeof(size_t) > vm->pool_len) {
    vm->stopped = true;
    snprintf(vm->error, sizeof(vm->error), "constant pool offset overflow");
    return;
  }
  push_slot(vm, (uintptr_t)&vm->pool[offset]);
  if (vm->stopped) return;
  vm->ip += sizeof(size_t);
}

static void exec_arrbeg(VMEngine *vm) {
  uint64_t ref = pop_slot(vm);
  if (vm->stopped) return;
  VMArray *array = (VMArray*)(uintptr_t)ref;
  if (array == NULL) {
    vm->stopped = true;
    snprintf(vm->error, sizeof(vm->error), "array begin: NULL array reference");
    return;
  }
  push_slot(vm, (uint64_t)(uintptr_t)((uint8_t*)array + offsetof(VMArray, data)));
}

static void exec_halt(VMEngine *vm) {
  vm->stopped = true;
}

static void exec_echoi(VMEngine *vm) {
  int64_t op = pop_slot(vm);
  if (vm->stopped) return;
  printf("%"PRId64"\n", op);
}

static void exec_echos(VMEngine *vm) {
  uint64_t str_addr = pop_slot(vm);
  if (vm->stopped) return;
  char *s = (char*)(uintptr_t)str_addr;
  if (s == NULL) {
    vm->stopped = true;
    snprintf(vm->error, sizeof(vm->error), "echo error: NULL-address access");
    return;
  }
  printf("%s\n", s);
}

typedef void(*Executor)(VMEngine*);
static const Executor command_excutors[BC_END_OF_ENUM] = {
  // TODO assert all cellas are filled
  [BC_PUSH]   = exec_push,
  [BC_NOT]    = exec_not,
  [BC_ADD]    = exec_add,
  [BC_SUB]    = exec_sub,
  [BC_MUL]    = exec_mul,
  [BC_DIV]    = exec_div,
  [BC_DUP]    = exec_dup,
  [BC_CONST]  = exec_const,
  [BC_CONSTA] = exec_consta,
  [BC_ARRBEG] = exec_arrbeg,
  [BC_HALT]   = exec_halt,
  [BC_ECHOI]  = exec_echoi,
  [BC_ECHOS]  = exec_echos,
};

VMEngine *vm_new(size_t pool_len, size_t code_len, size_t stack_len) {
  const size_t align = _Alignof(uint64_t);
  const size_t pool_bytes  = pool_len  * sizeof(uint8_t);
  const size_t code_bytes  = code_len  * sizeof(uint8_t);
  const size_t stack_bytes = stack_len * sizeof(uint64_t);

  VMEngine *vm = malloc(sizeof(VMEngine) + pool_bytes + code_bytes
                        + align + stack_bytes);
  if (vm == NULL) {
    return NULL;
  }
  memset(vm, 0, sizeof(VMEngine));

  vm->pool_len  = pool_len;
  vm->code_len  = code_len;
  vm->stack_len = stack_len;

  uint8_t *p = (uint8_t*)vm + sizeof(VMEngine);
  vm->pool = p; p += pool_bytes;
  vm->code = p; p += code_bytes;
  uintptr_t stack_addr = ((uintptr_t)p + (align - 1)) & ~(uintptr_t)(align - 1);
  vm->stack = (VMStackSlot*)stack_addr;

  vm->sp = 0;
  vm->sfp = 0;
  vm->ip = 0;
  vm->add_code_idx = 0;

  memset(vm->code, 0, vm->code_len);

  return vm;
}

void vm_add_command(VMEngine *vm, VMCommand cmd) {
  assert(vm != NULL);
  if (vm->add_code_idx + 1 + cmd.op_len * sizeof(uint64_t) > vm->code_len) {
    vm->stopped = true;
    snprintf(vm->error, sizeof(vm->error), "failed to add command: code overflow");
    return;
  }
  vm->code[vm->add_code_idx++] = cmd.kind;
  for (size_t i = 0; i < cmd.op_len; i++) {
    memcpy(vm->code + vm->add_code_idx, &cmd.ops[i], sizeof cmd.ops[i]);
    vm->add_code_idx += sizeof(cmd.ops[0]);
  }
}

size_t vm_add_string(VMEngine *vm, const char *s, size_t len) {
  assert(vm != NULL);
  if (s == NULL) {
    vm->stopped = true;
    snprintf(vm->error, sizeof(vm->error), "attempt to add NULL string");
    return (size_t)-1;
  }
  const size_t align = _Alignof(VMArray);
  size_t off = (vm->add_pool_idx + align - 1) & ~(align - 1);
  size_t body_bytes = len + 1;                          // include the NUL
  if (off + sizeof(VMArray) + body_bytes > vm->pool_len) {   // pool_len, not code_len
    vm->stopped = true;
    snprintf(vm->error, sizeof(vm->error), "constant pool overflow");
    return (size_t)-1;
  }
  VMArray *target = (VMArray*)(uintptr_t)(vm->pool + off);
  target->length = len;
  memcpy(target->data, s, len);
  target->data[len] = '\0';
  vm->add_pool_idx = off + sizeof(VMArray) + len + 1;
  return off;
}

VMState vm_execute(VMEngine *vm, size_t steps) {
  assert(vm != NULL);
  size_t step = 0;
  while (step < steps && !vm->stopped) {
    const size_t ip = vm->ip;
    if (ip >= vm->code_len) {
      snprintf(vm->error, sizeof(vm->error), "instruction pointer overflow");
      return VM_STATE_ERROR;
    }
    BCCommandKind command_kind = (BCCommandKind) vm->code[ip];
    vm->ip += sizeof(BCCommandKind);
    if (command_kind >= BC_END_OF_ENUM) {
      snprintf(vm->error, sizeof(vm->error), "unknown command %d", command_kind);
      return VM_STATE_ERROR;
    }
    Executor e = command_excutors[command_kind];
    if (e == NULL) {
      snprintf(vm->error, sizeof(vm->error), "no executor for command %d", command_kind);
      return VM_STATE_ERROR;
    }
    step += 1;
    e(vm);
  }
  if (strlen(vm->error) > 0) return VM_STATE_ERROR;
  if (vm->stopped) return VM_STATE_STOPPED;
  return VM_STATE_RUNNING;   // ran out of steps, still runnable
}

void vm_free(VMEngine *vm) {
  assert(vm != NULL);
  free(vm);
}


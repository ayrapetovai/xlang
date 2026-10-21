#ifndef EXEC_H
#define EXEC_H

#include "walker.h"
#include "map.h"
#include "stack.h"
#include <stddef.h>

DEFINE_STACK(struct HashMap*, hashmap_free, EMScope);

typedef uint64_t EMStackSlot;

// typedef struct {
//   EMStackSlot *slots;
// } EMStackFrame;

typedef struct {
  WalkerContextHeader header;
  EMStackSlot  *stack_end;   // end of stack address space, stack pinter cannot not be greater than this, stack < stack_end
  EMStackSlot  *stack;       // operand stack address space, grows up
  EMStackSlot  *sp;          // stack pointer, defines the free space to push next operand, grows up
  EMStackSlot  *fp;          // frame pointer, defines where the currnet function's slots begin
  EMScope      *scope;       // name storage
  bool          corrupted;   // the machine can be no longer used
  bool          if_taken;    // flag tha is set by the if_then branch to let if_else to know that it must be skipped
} ExecMachine;

ExecMachine *exec_new(size_t max_stack_size);

void exec_free(ExecMachine *em);

#endif

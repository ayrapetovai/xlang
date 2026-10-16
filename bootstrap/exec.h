#ifndef EXEC_H
#define EXEC_H

#include "ast.h"
#include "map.h"
#include "stack.h"
#include <inttypes.h>
#include <setjmp.h>
#include <stddef.h>

DEFINE_STACK(struct HashMap*, hashmap_free, EMScope);

typedef uint64_t EMStackSlot;

// typedef struct {
//   EMStackSlot *slots;
// } EMStackFrame;

typedef enum {
  EMS_SUCCESS,
  EMS_ERROR,
} EMState;

typedef struct {
  EMStackSlot  *stack_end; // end of stack address space, stack pinter cannot not be greater than this
  EMStackSlot  *stack;     // operand stack address space
  EMStackSlot  *sp;        // stack pointer, defines the free space to push next operand
  EMStackSlot  *fp;        // frame pointer, defines where the currnet function's slots begin
  EMScope      *scope;     // name storage
  size_t        task_slots;// the AST depth
  jmp_buf       catch;     // return address for runtime error cases
  char error[1024];        // memory for error messages
} ExecMachine;

ExecMachine *exec_new(size_t stack_slots, size_t task_slots);

EMState exec_run(ExecMachine *em, const ASTNode* root);

void exec_free(ExecMachine *em);

#endif

#ifndef EXEC_H
#define EXEC_H

#include "ast.h"
#include "map.h"
#include "stack.h"
#include <setjmp.h>
#include <stddef.h>

DEFINE_STACK(struct HashMap*, hashmap_free, EMScope);

typedef uint64_t EMStackSlot;

// typedef struct {
//   EMStackSlot *slots;
// } EMStackFrame;

typedef enum : char {
  EMS_SUCCESS,
  EMS_ERROR,
} EMState;

typedef enum : char {
  EMR_EVAL,
  EMR_REFERENCE,
} EMReduceType;

typedef struct {
  const ASTNode *node;
  EMReduceType   reduce_type;
  size_t         phase;      // the step of execution
} EMTask;

typedef struct {
  EMStackSlot  *stack_end;   // end of stack address space, stack pinter cannot not be greater than this, stack < stack_end
  EMStackSlot  *stack;       // operand stack address space, grows up
  EMStackSlot  *sp;          // stack pointer, defines the free space to push next operand, grows up
  EMStackSlot  *fp;          // frame pointer, defines where the currnet function's slots begin
  EMScope      *scope;       // name storage
  EMTask       *tasks;       // task stack, holds AST nodes to define the processing order, grows up
  size_t        task_offset; // task stack offset, defined the next free space to push a task, grows up
  size_t        task_slots;  // must be greater than or equal to the AST depth
  jmp_buf       catch;       // return address for runtime error cases, longjmp to this catch invalidates the execution
  bool          corrupted;   // the machine can be no longer used
  char error[1024];          // memory for error messages
} ExecMachine;

ExecMachine *exec_new(size_t max_stack_size);

EMState exec_run(ExecMachine *em, const ASTNode* root);

void exec_free(ExecMachine *em);

#endif

#ifndef WALKER_H
#define WALKER_H

#include "ast.h"
#include <setjmp.h>

typedef enum : char {
  EMS_SUCCESS,
  EMS_ERROR,
} EMState;

typedef void (*NodeApplyer)(void*ctx, const ASTNode *n);
typedef void (*Applyer)(void*ctx);
typedef bool (*CondApplyer)(void *ctx);   // reads the decision apply_if left behind

typedef struct {
  NodeApplyer value_of;
  NodeApplyer apply_identifier_ref;
  NodeApplyer apply_identifier;
  NodeApplyer apply_uexpr;
  NodeApplyer apply_expr;
  NodeApplyer apply_if_cond;
  CondApplyer apply_if_then_descision;
  CondApplyer apply_if_else_descision;
  NodeApplyer apply_if_post;
  NodeApplyer apply_ifexpr_post;
  NodeApplyer apply_define;
  Applyer apply_assign;
  Applyer apply_echo;
  Applyer scope_push_new;
  NodeApplyer scope_pop;

  Applyer apply_enter;
  Applyer apply_leave;
} WOperations;

// This context header must be the first field in any context (print, check, exec)
typedef struct {
  WOperations ops; // operations to be performed under the nodes of the AST
  jmp_buf catch;   // return address for runtime error cases, longjmp to this catch invalidates the execution
  char error[1024];     // memory for error messages, occured in context evaluation
} WalkerContextHeader;

typedef enum : char {
  EMR_EVAL,
  EMR_REFERENCE,
} EMReduceType;

typedef struct {
  const ASTNode *node;
  EMReduceType   reduce_type;
  size_t         phase;      // the step of execution
} EMTask;

typedef struct Walker {
  WalkerContextHeader *ctx;
  EMTask       *tasks;       // task stack, holds AST nodes to define the processing order, grows up
  size_t        task_offset; // task stack offset, defined the next free space to push a task, grows up
  size_t        task_slots;  // must be greater than or equal to the AST depth
  jmp_buf       catch;       // return address for runtime error cases, longjmp to this catch invalidates the execution
  bool          corrupted;   // the machine can be no longer used
  char error[1024];          // memory for error messages
} Walker;

// ctx implementing struct must have WOperations as a first field
struct Walker *walker_new(WalkerContextHeader *ctx, size_t max_stack_size);

EMState walker_run(struct Walker *walker, const ASTNode *root);

void walker_free(struct Walker *walker);

#endif


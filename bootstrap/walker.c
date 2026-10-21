
#include "ast.h"
#include "walker.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Walker *walker_new(WalkerContextHeader *ctx, size_t max_stack_size) {
  assert(max_stack_size > 0);
  struct Walker *walker = malloc(sizeof(Walker));
  assert(walker != NULL);
  if (walker == NULL) return NULL;
  memset(walker->error, '\0', sizeof(walker->error));
  walker->ctx = ctx;

  walker->task_offset = 0;
  walker->task_slots = max_stack_size;
  walker->tasks = malloc(walker->task_slots * sizeof(EMTask));         // work stack; size = AST depth/node count
  if (walker->tasks == NULL) {
    walker->corrupted = true;
    snprintf(walker->error, sizeof(walker->error), "failed to allocate memory for task stack, %zu elements", max_stack_size);
    return walker;
  }
  walker->corrupted = false;
  return walker;
}

static inline void push_task(Walker *walker, const ASTNode *n, EMReduceType r) {
  if (walker->task_offset >= walker->task_slots) {
    snprintf(walker->error, sizeof(walker->error), "task stack overflow");
    longjmp(walker->catch, 1);
  }
  walker->ctx->ops.apply_enter(walker->ctx);
  walker->tasks[walker->task_offset++] = (EMTask){ n, r, 0 };
}

static inline EMTask pop_task(Walker *walker) {
  if (walker->task_offset == 0) {
    snprintf(walker->error, sizeof(walker->error), "task stack underflow");
    longjmp(walker->catch, 1);
  }
  walker->ctx->ops.apply_leave(walker->ctx);
  return walker->tasks[--walker->task_offset];
}

static inline EMTask *top_task(Walker *walker) {
  if (walker->task_offset == 0) {
    snprintf(walker->error, sizeof(walker->error), "fetch top fialed: task stack underflow");
    longjmp(walker->catch, 1);
  }
  return &walker->tasks[walker->task_offset - 1];
}

EMState walker_run(Walker *walker, const ASTNode *root) {
  if (walker->corrupted) return EMS_ERROR;

  if (setjmp(walker->catch) != 0) {
    walker->corrupted = true;
    return EMS_ERROR;
  }

  if (setjmp(walker->ctx->catch) != 0) {
    walker->corrupted = true;
    snprintf(walker->error, sizeof(walker->error), "%s", walker->ctx->error);
    return EMS_ERROR;
  }

  const void *const *ops = (const void *const *)&walker->ctx->ops;
  for (size_t i = 0; i < sizeof(WOperations) / sizeof(void *); i++) {
    if (ops[i] == NULL) {
      walker->corrupted = true;
      snprintf(walker->error, sizeof(walker->error), "operation table is not initialized, field %zu", i);
      return EMS_ERROR;
    }
  }

  push_task(walker, root, EMR_EVAL);

  #define PHASE1(t, p1)             { p1; pop_task(walker); } break;
  #define PHASE2(t, p1, p2)         if (t->phase == 0) { t->phase = 1; p1; } else { p2; pop_task(walker); } break;
  #define PHASE3(t, p1, p2, p3)     if (t->phase == 0) { t->phase = 1; p1; } else if (t->phase == 1) {t->phase = 2; p2; } else { p3; pop_task(walker); } break;
  #define PHASE4(t, p1, p2, p3, p4) if (t->phase == 0) { t->phase = 1; p1; } else if (t->phase == 1) {t->phase = 2; p2; } else if (t->phase == 2) { t->phase = 3; p3; } else { p4; pop_task(walker); } break;

  while (walker->task_offset > 0) {
    EMTask *t = top_task(walker);
    const ASTNode *n = t->node;

    if (n == NULL) {
      pop_task(walker);
      continue;
    }

    if (t->reduce_type == EMR_REFERENCE) {
      switch (n->kind) {
        case NODE_ID:
          PHASE1(t, walker->ctx->ops.apply_identifier_ref(walker->ctx, n));
        default:
          snprintf(walker->error, sizeof(walker->error), "expression is not assignable (not an lvalue)");
          longjmp(walker->catch, 1);
      }
    } else if (t->reduce_type == EMR_EVAL) { // if eval
      switch (n->kind) {
        case NODE_VALUE:
          PHASE1(t, walker->ctx->ops.value_of(walker->ctx, n));
        case NODE_EXPR:
          PHASE3(t,
            push_task(walker, n->info.expr.op1, EMR_EVAL),
            push_task(walker, n->info.expr.op2, EMR_EVAL),
            walker->ctx->ops.apply_expr(walker->ctx, n)
          );
        case NODE_UEXPR:
          PHASE2(t,
            push_task(walker, n->info.uexpr.op, EMR_EVAL),
            walker->ctx->ops.apply_uexpr(walker->ctx, n)
          );
        case NODE_STMT:
          switch (n->info.stmt.stmt_tok) {
            case TOK_IF:
              PHASE4(t,
                push_task(walker, n->info.stmt.iff.cond, EMR_EVAL),
                walker->ctx->ops.apply_if_cond(walker->ctx, n),
                { bool do_then = walker->ctx->ops.apply_if_then_descision(walker->ctx);
                  bool do_else = walker->ctx->ops.apply_if_else_descision(walker->ctx);
                  if (do_else) push_task(walker, n->info.stmt.iff.else_arm, EMR_EVAL);
                  if (do_then) push_task(walker, n->info.stmt.iff.then_arm, EMR_EVAL);},
                walker->ctx->ops.apply_if_post(walker->ctx, n)
              );
            case TOK_ECHO:
              PHASE2(t,
                push_task(walker, n->info.stmt.echo.expr, EMR_EVAL),
                walker->ctx->ops.apply_echo(walker->ctx)
            );
            case TOK_DEFINE:
              PHASE2(t,
                push_task(walker, n->info.stmt.define.expr, EMR_EVAL),
                walker->ctx->ops.apply_define(walker->ctx, n)
              );
            case TOK_ASSIGN:
              PHASE3(t,
                push_task(walker, n->info.stmt.assign.expr, EMR_EVAL),
                push_task(walker, n->info.stmt.assign.lexpr, EMR_REFERENCE),
                walker->ctx->ops.apply_assign(walker->ctx);
              );
            default:
              snprintf(walker->error, sizeof(walker->error), "failed to create a task: statement unrecognized %d", n->info.stmt.stmt_tok);
              longjmp(walker->catch, 1);
          }
          break;
        case NODE_BLOCK:
          if (t->phase == 0) walker->ctx->ops.scope_push_new(walker->ctx);
          if (t->phase < n->info.block.len) {
            size_t i = t->phase; t->phase = i + 1;
            push_task(walker, n->info.block.stmts[i], EMR_EVAL);
          } else { walker->ctx->ops.scope_pop(walker->ctx, n); pop_task(walker); }
          break;
        case NODE_ID:
          PHASE1(t, walker->ctx->ops.apply_identifier(walker->ctx, n));
        default:
          snprintf(walker->error, sizeof(walker->error), "failed to create a task: node kind unrecognized %d", n->kind);
          longjmp(walker->catch, 1);
      }
    } else {
      snprintf(walker->error, sizeof(walker->error), "unknown walk state");
      longjmp(walker->catch, 1);
    }
  }
  #undef PHASE1
  #undef PHASE2
  #undef PHASE3
  return EMS_SUCCESS;
}

void walker_free(Walker *walker) {
  if (walker == NULL) return;
  if (walker->tasks != NULL) free(walker->tasks);
  free(walker);
}

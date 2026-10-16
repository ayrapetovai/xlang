#include "exec.h"
#include "ast.h"
#include "tokens.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ExecMachine *exec_new(size_t stack_slots) {
  assert(stack_slots > 0);
  ExecMachine *em = malloc(sizeof(ExecMachine) + stack_slots * sizeof(EMStackSlot));
  assert(em != NULL);
  if (em == NULL) return NULL;
  em->stack = (EMStackSlot*)(em + 1);
  em->stack_end = em->stack + stack_slots;
  em->sp = em->stack;
  em->fp = NULL;
  memset(em->error, '\0', sizeof(em->error));
  return em;
}

static inline EMStackSlot exec_pop(ExecMachine *em) {
  // TODO check bounds
  return *--em->sp;
}

static inline void exec_push(ExecMachine *em, EMStackSlot value) {
  // TODO check bounds
  *em->sp++ = value;
}

static void apply_expr(ExecMachine *em, const ASTNode *n) {
  int64_t b = (int64_t)exec_pop(em);
  int64_t a = (int64_t)exec_pop(em);
  int64_t r;
  switch (n->info.expr.op_tok) {
    case TOK_MINUS: r = a - b; break;
    case TOK_PLUS:  r = a + b; break;
    case TOK_STAR:  r = a * b; break;
    case TOK_SLASH:
      if (b == 0) {
        snprintf(em->error, sizeof(em->error), "division by zero");
        // TODO stop execution
        // em->stopped = true;
        return;
      }
      r = a / b; break;
    default: assert(false);
  }
  exec_push(em, r);
}

static void apply_uexpr(ExecMachine *em, const ASTNode *n) {
  int64_t v = (int64_t)exec_pop(em);
  switch (n->info.uexpr.op_tok) {
    case TOK_MINUS: exec_push(em, -v); break;
    default: assert(false);
  }
}

static void value_of(ExecMachine *em, const ASTNode *n) {
  switch (n->info.val.kind) {
    case VK_INTEGER: exec_push(em, (int64_t)atoi(n->info.val.value));       break;
    case VK_BOOL:    exec_push(em, strcmp(n->info.val.value, "true") == 0); break;
    default:
      snprintf(em->error, sizeof(em->error), "unsupported value kind %d", n->info.val.kind);
      // TODO stop execution
      // em->stopped = true;
  }
}

EMState exec_run(ExecMachine *em, const ASTNode *root) {
  typedef struct { const ASTNode *node; bool reduce; } Task;
  Task *tasks = malloc(1024 * sizeof(Task));         // work stack; size = AST depth/node count

  if (tasks == NULL) {
    snprintf(em->error, sizeof(em->error), "failed to allocate memory for task stack");
    return EMS_ERROR;
  }

  size_t ttop = 0;                         // em->stack / em->sp is the value stack
  #define PUSH_TASK(t) (tasks[ttop++] = (t))
  #define EVAL(n)   (Task){ (n), false }   // expand: schedule children
  #define REDUCE(n) (Task){ (n), true  }   // children done: combine them

  PUSH_TASK(EVAL(root));

  while (ttop > 0) {
    Task t = tasks[--ttop];
    const ASTNode *n = t.node;

    if (t.reduce) {                                   // children's values are on the value stack
      switch (n->kind) {
        case NODE_EXPR:  apply_expr(em, n);  break;   // pop 2, push 1
        case NODE_UEXPR: apply_uexpr(em, n); break;   // pop 1, push 1
        case NODE_STMT:
          // TODO factor out to the separate function apply_stmt
          switch (n->info.stmt.stmt_tok) {
            case TOK_IF:
              int64_t c = (int64_t)exec_pop(em);
              // TODO refactor: move working with task stack to the eval-branch
              PUSH_TASK(EVAL(c ? n->info.stmt.iff.then_arm
                              : n->info.stmt.iff.else_arm));
              break;
            case TOK_ECHO:
              printf("%d\n", (int)(int64_t)exec_pop(em));
              break;
            default:
              assert(false);
          }
          break;
        case NODE_BLOCK: break;                       // nothing to reduce
        default: assert(false);
      }
    } else { // if eval
      if (n == NULL) continue;
      switch (n->kind) {
        case NODE_VALUE: value_of(em, n); break;
        case NODE_EXPR:
          PUSH_TASK(REDUCE(n));
          PUSH_TASK(EVAL(n->info.expr.op2));
          PUSH_TASK(EVAL(n->info.expr.op1));
          break;
        case NODE_UEXPR:
          PUSH_TASK(REDUCE(n));
          PUSH_TASK(EVAL(n->info.uexpr.op));
          break;
        case NODE_STMT:
          PUSH_TASK(REDUCE(n));
          switch (n->info.stmt.stmt_tok) {
            case TOK_IF:
              PUSH_TASK(EVAL(n->info.stmt.iff.cond));  break;
            case TOK_ECHO:
              PUSH_TASK(EVAL(n->info.stmt.echo.expr)); break;
            default: assert(false);
          }
          break;
        case NODE_BLOCK:
          for (size_t i = n->info.block.len; i-- > 0;)
            PUSH_TASK(EVAL(n->info.block.stmts[i]));
          break;
        default:
          assert(false);
      }
    }
  }
  free(tasks);
  return EMS_SUCCESS;
}

void exec_free(ExecMachine *em) {
  assert(em != NULL);
  free(em);
}

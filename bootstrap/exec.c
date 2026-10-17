#include "exec.h"
#include "ast.h"
#include "map.h"
#include "tokens.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

// all functions with longjmp to ExecMachine::catch can be called only
// in stack of exec_run(ExecMachine*, const ASTNode*), becase
// they transfere exection to this function

typedef struct {
  // type: variable/function/structure/interface/array
  // mutability: const/variable
  // ownership: OWNED|BORROW|SHARED
  // constitution: SCALAR|PTR|STRUCT
  // size: 1|2|4|8
  // location: stack/heap/pool
  // lifecycle bits: copyable + disposable
  // value:
  // ```c
  // union {
  //   VMStackSlot *s;
  //   ASTNode *n;
  //   void* p;
  // };
  // ```

  EMStackSlot value;
} NameDescriptor;

ExecMachine *exec_new(size_t stack_slots, size_t task_slots) {
  assert(stack_slots > 0);
  assert(task_slots > 0);
  ExecMachine *em = malloc(sizeof(ExecMachine) + stack_slots * sizeof(EMStackSlot));
  assert(em != NULL);
  if (em == NULL) return NULL;
  em->stack = (EMStackSlot*)(em + 1);
  em->stack_end = em->stack + stack_slots;
  em->sp = em->stack;
  em->fp = NULL;
  em->scope = EMScope_new();
  if (em->scope == NULL) {
    free(em);
    return NULL;
  }
  em->ttop = 0;
  em->task_slots = task_slots;
  em->tasks = malloc(em->task_slots * sizeof(EMTask));         // work stack; size = AST depth/node count
  if (em->tasks == NULL) {
    EMScope_free(em->scope);
    free(em);
    return NULL;
  }
  memset(em->error, '\0', sizeof(em->error));
  return em;
}

static inline void push_task(ExecMachine *em, const ASTNode *n, EMReduceType r) {
    if (em->ttop >= em->task_slots) {
      snprintf(em->error, sizeof(em->error), "task stack overflow");
      longjmp(em->catch, 1);
    }
    em->tasks[em->ttop++] = (EMTask){ n, r };
}

static inline EMTask pop_task(ExecMachine *em) {
    if (em->ttop == 0){
      snprintf(em->error, sizeof(em->error), "task stack underflow");
      longjmp(em->catch, 1);
    }
    return em->tasks[--em->ttop];
}

static inline void scope_push_new(ExecMachine *em) {
  if (!EMScope_push(em->scope, hashmap_new(free))) {
    snprintf(em->error, sizeof(em->error), "failed to create scope");
    longjmp(em->catch, 1);
  }
}

static inline void scope_pop(ExecMachine *em) {
  struct HashMap *hm = NULL;
  EMScope_pop(em->scope, &hm);
  if (hm != NULL) hashmap_free(hm);
}

static inline void scope_add_descriptor(ExecMachine *em, char *name, NameDescriptor *dsc) {
  hashmap_put(EMScope_top(em->scope), name, dsc);
}

static inline NameDescriptor *scope_get_descriptor(ExecMachine *em, char *name) {
  for (size_t i = em->scope->len; i-- > 0;) {
    struct HashMap *hm = em->scope->data[i];
    NameDescriptor *dsc = hashmap_get(hm, name);
    if (dsc != NULL) {
      return dsc;
    }
  }
  return NULL;
}

static inline EMStackSlot exec_pop(ExecMachine *em) {
  if (em->sp <= em->stack) {
    snprintf(em->error, sizeof(em->error), "stack underflow");
    longjmp(em->catch, 1);
  }
  return *--em->sp;
}

static inline void exec_push(ExecMachine *em, EMStackSlot value) {
  if (em->sp >= em->stack_end) {
    snprintf(em->error, sizeof(em->error), "stack overflow");
    longjmp(em->catch, 1);
  }
  *em->sp++ = value;
}

static void apply_identifier_ref(ExecMachine *em, const ASTNode *n) {
  NameDescriptor* dsc = scope_get_descriptor(em, n->info.id.value);
  if (dsc == NULL) {
    snprintf(em->error, sizeof(em->error), "reference fetch failed: undefined name %s", n->info.id.value);
    longjmp(em->catch, 1);
  }
  exec_push(em, (uintptr_t)&dsc->value);
}

static void apply_identifier(ExecMachine *em, const ASTNode *n) {
  NameDescriptor* dsc = scope_get_descriptor(em, n->info.id.value);
  if (dsc == NULL) {
    snprintf(em->error, sizeof(em->error), "value fetch failed: undefined name %s", n->info.id.value);
    longjmp(em->catch, 1);
  }
  exec_push(em, dsc->value);
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
        longjmp(em->catch, 1);
      }
      r = a / b; break;
    default:
      snprintf(em->error, sizeof(em->error), "unknown expression kind %d", n->info.expr.op_tok);
      longjmp(em->catch, 1);
  }
  exec_push(em, r);
}

static void apply_uexpr(ExecMachine *em, const ASTNode *n) {
  int64_t v = (int64_t)exec_pop(em);
  switch (n->info.uexpr.op_tok) {
    case TOK_MINUS: exec_push(em, -v); break;
    default:
      snprintf(em->error, sizeof(em->error), "unknown unary expression kind %d", n->info.uexpr.op_tok);
      longjmp(em->catch, 1);
  }
}

static void apply_if(ExecMachine *em, const ASTNode *n) {
  int64_t c = (int64_t)exec_pop(em);
  push_task(em, (c ? n->info.stmt.iff.then_arm
                  : n->info.stmt.iff.else_arm), EMR_EVAL);
}

static void value_of(ExecMachine *em, const ASTNode *n) {
  switch (n->info.val.kind) {
    case VK_INTEGER: exec_push(em, (int64_t)atoi(n->info.val.value));       break;
    case VK_BOOL:    exec_push(em, strcmp(n->info.val.value, "true") == 0); break;
    default:
      snprintf(em->error, sizeof(em->error), "unsupported value kind %d", n->info.val.kind);
      longjmp(em->catch, 1);
  }
}

static void apply_define(ExecMachine *em, const ASTNode *n) {
  // TODO refactor get name from define node
  char *name = n->info.stmt.define.id->info.id.value;
  NameDescriptor *dsc = malloc(sizeof(NameDescriptor));
  if (dsc == NULL) {
    snprintf(em->error, sizeof(em->error), "name dclaration failed to allocate memory");
    longjmp(em->catch, 1);
  }
  dsc->value = exec_pop(em);
  scope_add_descriptor(em, name, dsc);
}

static void apply_assign(ExecMachine *em) {
  EMStackSlot *addr  = (EMStackSlot*)(uintptr_t)exec_pop(em);
  if (addr == NULL) {
    snprintf(em->error, sizeof(em->error), "NULL pointer access");
    longjmp(em->catch, 1);
  }
  *addr = exec_pop(em);
}

EMState exec_run(ExecMachine *em, const ASTNode *root) {
  if (setjmp(em->catch) != 0) {
    em->sp = em->stack;
    // TODO exec_clear_scopes(em); // with proper disposing variables
    return EMS_ERROR;
  }

  push_task(em, root, EMR_EVAL);

  while (em->ttop > 0) {
    EMTask t = pop_task(em);
    const ASTNode *n = t.node;

    if (t.reduce_type == EMR_VALUE) {                                   // children's values are on the value stack
      switch (n->kind) {
        case NODE_EXPR:  apply_expr(em, n);  break;   // pop 2, push 1
        case NODE_UEXPR: apply_uexpr(em, n); break;   // pop 1, push 1
        case NODE_STMT:
          // TODO factor out to the separate function apply_stmt
          switch (n->info.stmt.stmt_tok) {
            case TOK_IF: apply_if(em, n); break;
            case TOK_ECHO:
              printf("%d\n", (int)(int64_t)exec_pop(em));
              break;
            case TOK_DEFINE: apply_define(em, n); break;
            case TOK_ASSIGN: apply_assign(em);    break;
            default:
              snprintf(em->error, sizeof(em->error), "statment unrecognized %d", n->info.stmt.stmt_tok);
              longjmp(em->catch, 1);
          }
          break;
        case NODE_BLOCK:                       // nothing to reduce
          scope_pop(em);
          break;
        case NODE_ID: apply_identifier(em, n); break;
        default:
          snprintf(em->error, sizeof(em->error), "node kind unrecognized %d", n->kind);
          longjmp(em->catch, 1);
      }
    } else if (t.reduce_type == EMR_REFERENCE) {
      switch (n->kind) {
        case NODE_ID: apply_identifier_ref(em, n); break;
        default:
          snprintf(em->error, sizeof(em->error), "expression is not assignable (not lvalue)");
          longjmp(em->catch, 1);
      }
    } else { // if eval
      if (n == NULL) continue;
      switch (n->kind) {
        case NODE_VALUE: value_of(em, n); break;
        case NODE_EXPR:
          push_task(em, n, EMR_VALUE);
          push_task(em, n->info.expr.op2, EMR_EVAL);
          push_task(em, n->info.expr.op1, EMR_EVAL);
          break;
        case NODE_UEXPR:
          push_task(em, n, EMR_VALUE);
          push_task(em, n->info.uexpr.op, EMR_EVAL);
          break;
        case NODE_STMT:
          push_task(em, n, EMR_VALUE);
          switch (n->info.stmt.stmt_tok) {
            case TOK_IF:
              push_task(em, n->info.stmt.iff.cond, EMR_EVAL);  break;
            case TOK_ECHO:
              push_task(em, n->info.stmt.echo.expr, EMR_EVAL); break;
            case TOK_DEFINE:
              push_task(em, n->info.stmt.define.expr, EMR_EVAL); break;
            case TOK_ASSIGN:
              push_task(em, n->info.stmt.assign.lexpr, EMR_REFERENCE);
              push_task(em, n->info.stmt.assign.expr, EMR_EVAL);
              break;
            default:
              snprintf(em->error, sizeof(em->error), "failed to create a task: statement unrecognized %d", n->info.stmt.stmt_tok);
              longjmp(em->catch, 1);
          }
          break;
        case NODE_BLOCK:
          scope_push_new(em);
          push_task(em, n, EMR_VALUE);
          for (size_t i = n->info.block.len; i-- > 0;)
            push_task(em, n->info.block.stmts[i], EMR_EVAL);
          break;
        case NODE_ID:
          push_task(em, n, EMR_VALUE);
          break;
        default:
          snprintf(em->error, sizeof(em->error), "failed to create a task: node kind unrecognized %d", n->kind);
          longjmp(em->catch, 1);
      }
    }
  }
  return EMS_SUCCESS;
}

void exec_free(ExecMachine *em) {
  assert(em != NULL);
  free(em->tasks);
  EMScope_free(em->scope);
  free(em);
}

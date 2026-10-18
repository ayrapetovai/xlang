#include "exec.h"
#include "ast.h"
#include "map.h"
#include "tokens.h"
#include <assert.h>
#include <errno.h>
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
  // TODO allocate memory for stack separately. Must stack be growable?
  ExecMachine *em = malloc(sizeof(ExecMachine) + stack_slots * sizeof(EMStackSlot));
  assert(em != NULL);
  if (em == NULL) return NULL;
  memset(em->error, '\0', sizeof(em->error));
  em->stack = (EMStackSlot*)(em + 1);
  em->stack_end = em->stack + stack_slots;
  em->sp = em->stack;
  em->fp = NULL;

  em->tasks = NULL;

  em->scope = EMScope_new();
  if (em->scope == NULL) {
    em->corrupted = true;
    snprintf(em->error, sizeof(em->error), "failed to allocate memory for scope");
    return em;
  }
  em->to = 0;
  em->task_slots = task_slots;
  em->tasks = malloc(em->task_slots * sizeof(EMTask));         // work stack; size = AST depth/node count
  if (em->tasks == NULL) {
    em->corrupted = true;
    snprintf(em->error, sizeof(em->error), "failed to allocate memory for task stack, %ld elements", task_slots);
    return em;
  }
  em->corrupted = false;
  return em;
}

static inline void push_task(ExecMachine *em, const ASTNode *n, EMReduceType r) {
    if (em->to >= em->task_slots) {
      snprintf(em->error, sizeof(em->error), "task stack overflow");
      longjmp(em->catch, 1);
    }
    em->tasks[em->to++] = (EMTask){ n, r, 0 };
}

static inline EMTask pop_task(ExecMachine *em) {
    if (em->to == 0){
      snprintf(em->error, sizeof(em->error), "task stack underflow");
      longjmp(em->catch, 1);
    }
    return em->tasks[--em->to];
}

static inline EMTask *top_task(ExecMachine *em) {
    if (em->to == 0) {
      snprintf(em->error, sizeof(em->error), "fetch top fialed: task stack underflow");
      longjmp(em->catch, 1);
    }
    return &em->tasks[em->to - 1];
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
  void *prev = hashmap_put(EMScope_top(em->scope), name, dsc);
  if (prev != NULL) {
    free(prev);
    snprintf(em->error, sizeof(em->error), "name redefinition, %s", name);
    longjmp(em->catch, 1);
  }
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
    case VK_INTEGER:
      errno = 0;
      int64_t v = strtoll(n->info.val.value, NULL, 10);
      if (errno != 0) {
        snprintf(em->error, sizeof(em->error), "failed to convert string to integer: %s", strerror(errno));
        longjmp(em->catch, 1);
      }
      exec_push(em, v);
      break;
    case VK_BOOL: exec_push(em, strcmp(n->info.val.value, "true") == 0); break;
    default:
      snprintf(em->error, sizeof(em->error), "unsupported value kind %d", n->info.val.kind);
      longjmp(em->catch, 1);
  }
}

static void apply_echo(ExecMachine *em) {
  printf("%"PRId64"\n", (int64_t)exec_pop(em));
}

static void apply_define(ExecMachine *em, const ASTNode *n) {
  // TODO refactor get name from define node
  char *name = n->info.stmt.define.id->info.id.value;
  NameDescriptor *dsc = malloc(sizeof(NameDescriptor));
  if (dsc == NULL) {
    snprintf(em->error, sizeof(em->error), "name declaration failed to allocate memory");
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
  if (em->corrupted) return EMS_ERROR;

  if (setjmp(em->catch) != 0) {
    em->corrupted = true;
    return EMS_ERROR;
  }

  push_task(em, root, EMR_EVAL);

  #define PHASE1(t, p1) { p1; pop_task(em); } break;
  #define PHASE2(t, p1, p2) if (t->phase == 0) { t->phase = 1; p1; } else { p2; pop_task(em); } break;
  #define PHASE3(t, p1, p2, p3) if (t->phase == 0) { t->phase = 1; p1; } else if (t->phase == 1) {t->phase = 2; p2; } else { p3; pop_task(em); } break;

  while (em->to > 0) {
    EMTask *t = top_task(em);
    const ASTNode *n = t->node;

    if (n == NULL) {
      pop_task(em);
      continue;
    }

    if (t->reduce_type == EMR_REFERENCE) {
      switch (n->kind) {
        case NODE_ID: apply_identifier_ref(em, n); pop_task(em); break;
        default:
          snprintf(em->error, sizeof(em->error), "expression is not assignable (not lvalue)");
          longjmp(em->catch, 1);
      }
    } else { // if eval
      switch (n->kind) {
        case NODE_VALUE:
          PHASE1(t, value_of(em, n));
        case NODE_EXPR:
          PHASE3(t,
            push_task(em, n->info.expr.op1, EMR_EVAL),
            push_task(em, n->info.expr.op2, EMR_EVAL),
            apply_expr(em, n)
          );
        case NODE_UEXPR:
          PHASE2(t,
            push_task(em, n->info.uexpr.op, EMR_EVAL),
            apply_uexpr(em, n)
          );
        case NODE_STMT:
          switch (n->info.stmt.stmt_tok) {
            case TOK_IF:
              PHASE3(t,
                push_task(em, n->info.stmt.iff.cond, EMR_EVAL),
                apply_if(em, n),
                {} // no operation, pop task only
              );
            case TOK_ECHO:
              PHASE2(t,
                push_task(em, n->info.stmt.echo.expr, EMR_EVAL),
                apply_echo(em)
            );
            case TOK_DEFINE:
              PHASE2(t,
                push_task(em, n->info.stmt.define.expr, EMR_EVAL),
                apply_define(em, n)
              );
            case TOK_ASSIGN:
              PHASE3(t,
                push_task(em, n->info.stmt.assign.expr, EMR_EVAL),
                push_task(em, n->info.stmt.assign.lexpr, EMR_REFERENCE),
                apply_assign(em)
              );
            default:
              snprintf(em->error, sizeof(em->error), "failed to create a task: statement unrecognized %d", n->info.stmt.stmt_tok);
              longjmp(em->catch, 1);
          }
          break;
        case NODE_BLOCK:
          if (t->phase == 0) scope_push_new(em);
          if (t->phase < n->info.block.len) {
            size_t i = t->phase; t->phase = i + 1;
            push_task(em, n->info.block.stmts[i], EMR_EVAL);
          } else { scope_pop(em); pop_task(em); }
          break;
        case NODE_ID:
          PHASE1(t, apply_identifier(em, n));
        default:
          snprintf(em->error, sizeof(em->error), "failed to create a task: node kind unrecognized %d", n->kind);
          longjmp(em->catch, 1);
      }
      #undef PHASE1
      #undef PHASE2
      #undef PHASE3
    }
  }
  return EMS_SUCCESS;
}

void exec_free(ExecMachine *em) {
  if (em == NULL) return;
  if (em->tasks != NULL) free(em->tasks);
  if (em->scope != NULL) EMScope_free(em->scope);
  free(em);
}

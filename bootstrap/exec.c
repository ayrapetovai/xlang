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

// all functions with longjmp to catch_executin_exception can be called only
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
  em->task_slots = task_slots;
  if (em->scope == NULL) return NULL;
  memset(em->error, '\0', sizeof(em->error));
  return em;
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

static void apply_identifier(ExecMachine *em, const ASTNode *n) {
  NameDescriptor* dsc = scope_get_descriptor(em, n->info.id.value);
  if (dsc == NULL) {
    snprintf(em->error, sizeof(em->error), "undefined name %s", n->info.id.value);
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
  dsc->value = exec_pop(em);
  scope_add_descriptor(em, name, dsc);
}

static void apply_assign(ExecMachine *em) {
  (void)em;
}

EMState exec_run(ExecMachine *em, const ASTNode *root) {
  typedef struct { const ASTNode *node; bool reduce; } Task;
  Task *tasks = malloc(em->task_slots * sizeof(Task));         // work stack; size = AST depth/node count

  if (tasks == NULL) {
    snprintf(em->error, sizeof(em->error), "failed to allocate memory for task stack");
    return EMS_ERROR;
  }

  int jmpres = setjmp(em->catch);
  if (jmpres != 0) {
    em->sp = em->stack;
    // TODO exec_clear_scopes(em); // with proper disposing variables
    free(tasks);
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
            case TOK_DEFINE:
              PUSH_TASK(EVAL(n->info.stmt.define.expr)); break;
            case TOK_ASSIGN:
              PUSH_TASK(EVAL(n->info.stmt.assign.lexpr));
              PUSH_TASK(EVAL(n->info.stmt.assign.expr));
              break;
            default:
              snprintf(em->error, sizeof(em->error), "failed to create a task: statement unrecognized %d", n->info.stmt.stmt_tok);
              longjmp(em->catch, 1);
          }
          break;
        case NODE_BLOCK:
          scope_push_new(em);
          PUSH_TASK(REDUCE(n));
          for (size_t i = n->info.block.len; i-- > 0;)
            PUSH_TASK(EVAL(n->info.block.stmts[i]));
          break;
        case NODE_ID:
          PUSH_TASK(REDUCE(n));
          break;
        default:
          snprintf(em->error, sizeof(em->error), "failed to create a task: node kind unrecognized %d", n->kind);
          longjmp(em->catch, 1);
      }
    }
  }
  free(tasks);
  return EMS_SUCCESS;

  #undef PUSH_TASK
  #undef EVAL
  #undef REDUCE
}

void exec_free(ExecMachine *em) {
  assert(em != NULL);
  EMScope_free(em->scope);
  free(em);
}

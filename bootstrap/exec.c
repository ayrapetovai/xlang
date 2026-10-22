#include "exec.h"
#include "ast.h"
#include "map.h"
#include "tokens.h"
#include "walker.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

// all functions with longjmp to ExecMachine.header.catch can be called only
// in stack of function walker_run, becase they transfere exection to the function

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

static inline void scope_push_new(ExecMachine *em) {
  if (!EMScope_push(em->scope, hashmap_new(free))) {
    snprintf(em->header.error, sizeof(em->header.error), "failed to create scope");
    longjmp(em->header.catch, 1);
  }
}

static inline void scope_pop(ExecMachine *em, const ASTNode *) {
  struct HashMap *hm = NULL;
  EMScope_pop(em->scope, &hm);
  if (hm != NULL) hashmap_free(hm);
}

static inline void scope_add_descriptor(ExecMachine *em, char *name, NameDescriptor *dsc) {
  void *prev = hashmap_put(EMScope_top(em->scope), name, dsc);
  if (prev != NULL) {
    free(prev);
    snprintf(em->header.error, sizeof(em->header.error), "name redefinition, %s", name);
    longjmp(em->header.catch, 1);
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
    snprintf(em->header.error, sizeof(em->header.error), "stack underflow");
    longjmp(em->header.catch, 1);
  }
  return *--em->sp;
}

static inline void exec_push(ExecMachine *em, EMStackSlot value) {
  if (em->sp >= em->stack_end) {
    snprintf(em->header.error, sizeof(em->header.error), "stack overflow");
    longjmp(em->header.catch, 1);
  }
  *em->sp++ = value;
}

static void apply_identifier_ref(ExecMachine *em, const ASTNode *n) {
  NameDescriptor* dsc = scope_get_descriptor(em, n->info.id.value);
  if (dsc == NULL) {
    snprintf(em->header.error, sizeof(em->header.error), "reference fetch failed: undefined name %s", n->info.id.value);
    longjmp(em->header.catch, 1);
  }
  exec_push(em, (uintptr_t)&dsc->value);
}

static void apply_identifier(ExecMachine *em, const ASTNode *n) {
  NameDescriptor* dsc = scope_get_descriptor(em, n->info.id.value);
  if (dsc == NULL) {
    snprintf(em->header.error, sizeof(em->header.error), "value fetch failed: undefined name %s", n->info.id.value);
    longjmp(em->header.catch, 1);
  }
  exec_push(em, dsc->value);
}

static void apply_expr(ExecMachine *em, const ASTNode *n) {
  int64_t b = (int64_t)exec_pop(em);
  int64_t a = (int64_t)exec_pop(em);
  int64_t r;
  switch (n->info.expr.op_tok) {
    case TOK_MINUS: r = (int64_t)((uint64_t)a - (uint64_t)b); break;
    case TOK_PLUS:  r = (int64_t)((uint64_t)a + (uint64_t)b); break;
    case TOK_STAR:  r = (int64_t)((uint64_t)a * (uint64_t)b); break;
    case TOK_SLASH:
      if (b == 0) {
        snprintf(em->header.error, sizeof(em->header.error), "division by zero");
        longjmp(em->header.catch, 1);
      }
      if (b == -1) r = (int64_t)(0 - (uint64_t)a);
      else         r = a / b;
      break;
    default:
      snprintf(em->header.error, sizeof(em->header.error), "unknown expression kind %d", n->info.expr.op_tok);
      longjmp(em->header.catch, 1);
  }
  exec_push(em, r);
}

static void apply_uexpr(ExecMachine *em, const ASTNode *n) {
  int64_t v = (int64_t)exec_pop(em);
  switch (n->info.uexpr.op_tok) {
    // negate unsigned so -INT64_MIN wraps to INT64_MIN instead of being UB
    case TOK_MINUS: exec_push(em, (int64_t)(0 - (uint64_t)v)); break;
    default:
      snprintf(em->header.error, sizeof(em->header.error), "unknown unary expression kind %d", n->info.uexpr.op_tok);
      longjmp(em->header.catch, 1);
  }
}

static void apply_if_cond(ExecMachine *em, const ASTNode *) {
  EMStackSlot c = exec_pop(em);
  exec_push(em, c != 0);
}

static bool apply_if_then_descision(ExecMachine *em) {
  em->if_taken = false;
  uint64_t at_the_top = exec_pop(em);
  if (at_the_top == 0) {
    exec_push(em, at_the_top);
    return false;
  }
  em->if_taken = true;
  return true;
}

static bool apply_if_else_descision(ExecMachine *em) {
  if (em->if_taken) return false;   // then-arm chosen, decision already consumed
  return exec_pop(em) == 0;
}

static void value_of(ExecMachine *em, const ASTNode *n) {
  switch (n->info.val.kind) {
    case VK_INTEGER:
      errno = 0;
      int64_t v = strtoll(n->info.val.value, NULL, 10);
      if (errno != 0) {
        snprintf(em->header.error, sizeof(em->header.error), "failed to convert string to integer: %s", strerror(errno));
        longjmp(em->header.catch, 1);
      }
      exec_push(em, v);
      break;
    case VK_BOOL: exec_push(em, strcmp(n->info.val.value, "true") == 0); break;
    default:
      snprintf(em->header.error, sizeof(em->header.error), "unsupported value kind %d", n->info.val.kind);
      longjmp(em->header.catch, 1);
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
    snprintf(em->header.error, sizeof(em->header.error), "name declaration failed to allocate memory");
    longjmp(em->header.catch, 1);
  }
  dsc->value = exec_pop(em);
  scope_add_descriptor(em, name, dsc);
}

static void apply_assign(ExecMachine *em) {
  EMStackSlot *addr  = (EMStackSlot*)(uintptr_t)exec_pop(em);
  if (addr == NULL) {
    snprintf(em->header.error, sizeof(em->header.error), "NULL pointer access");
    longjmp(em->header.catch, 1);
  }
  *addr = exec_pop(em);
}

static void do_nothing_node(ExecMachine*, const ASTNode*) {}

ExecMachine *exec_new(size_t max_stack_size) {
  assert(max_stack_size > 0);
  ExecMachine *em = malloc(sizeof(ExecMachine) + max_stack_size * sizeof(EMStackSlot));
  assert(em != NULL);
  if (em == NULL) return NULL;
  memset(em, 0, sizeof(ExecMachine));
  memset(&em->header, 0, sizeof(em->header));
  em->stack = (EMStackSlot*)(em + 1);
  em->stack_end = em->stack + max_stack_size;
  em->sp = em->stack;
  em->fp = NULL;

  em->scope = EMScope_new();
  if (em->scope == NULL) {
    em->corrupted = true;
    snprintf(em->header.error, sizeof(em->header.error), "failed to allocate memory for scope");
    return em;
  }

  em->corrupted = false;

  em->header.ops.apply_assign = (Applyer)apply_assign;
  em->header.ops.apply_define = (NodeApplyer)apply_define;
  em->header.ops.apply_echo = (Applyer)apply_echo;
  em->header.ops.apply_expr = (NodeApplyer)apply_expr;
  em->header.ops.apply_uexpr = (NodeApplyer)apply_uexpr;
  em->header.ops.apply_identifier = (NodeApplyer)apply_identifier;
  em->header.ops.apply_identifier_ref = (NodeApplyer)apply_identifier_ref;
  em->header.ops.value_of = (NodeApplyer)value_of;

  em->header.ops.apply_if_cond = (NodeApplyer)apply_if_cond;
  em->header.ops.apply_if_then_descision = (CondApplyer)apply_if_then_descision;
  em->header.ops.apply_if_else_descision = (CondApplyer)apply_if_else_descision;

  // executor does not need to do anything after calculating if-statement
  em->header.ops.apply_if_post = (NodeApplyer)do_nothing_node;
  em->header.ops.apply_ifexpr_post = (NodeApplyer)do_nothing_node;

  em->header.ops.scope_push_new = (Applyer)scope_push_new;
  em->header.ops.scope_pop = (NodeApplyer)scope_pop;

  em->header.ops.apply_enter = (NodeApplyer)do_nothing_node;
  em->header.ops.apply_leave = (NodeApplyer)do_nothing_node;

  memset(em->header.error, '\0', sizeof(em->header.error));

  return em;
}

void exec_free(ExecMachine *em) {
  if (em == NULL) return;
  if (em->scope != NULL) EMScope_free(em->scope);
  free(em);
}

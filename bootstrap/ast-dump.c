#include "ast-dump.h"
#include "ast.h"
#include "walker.h"

#include <unistd.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static PrintNode *printnode_new(size_t i, char *t, struct PrintNode *n) {
  PrintNode *pn = malloc(sizeof(PrintNode));
  pn->text = t;
  pn->indent = i;
  pn->next = n;
  return pn;
}

static void printnode_append(PrintNode *n1, PrintNode *n2) {
  if (n1 == NULL || n2 == NULL) return;
  PrintNode *it = n1;
  while(it->next != NULL) {
    it = it->next;
  }
  it->next = n2;
}

#define RED_TEXT(s) "\033[31m" s "\033[0m"
#define GREEN_TEXT(s) "\033[32m" s "\033[0m"

typedef enum {
  RED,
  GREEN,
} Color;

static inline const char* colorise(ASTDump *dmp, const char *prefix, const char *s, Color color) {
  static char buf[1024] = {};
  char *print_to = buf;
  size_t prefix_len = 0;
  if (prefix != NULL) {
    prefix_len = snprintf(buf, sizeof(buf), "%s", prefix);
    print_to = buf + prefix_len;
  }
  if (dmp->colored) {
    switch (color) {
      case RED:
        snprintf(print_to, sizeof(buf) - prefix_len, RED_TEXT("%s"), s);
        break;
      case GREEN:
        snprintf(print_to, sizeof(buf) - prefix_len, GREEN_TEXT("%s"), s);
        break;
      default:
        exit(55);
    }
  } else {
    snprintf(print_to, sizeof(buf) - prefix_len, "%s", s);
    return buf;
  }
  return buf;
}

static inline void stack_push(ASTDump *dmp, PrintNode *s) {
  if (dmp->sp >= dmp->stack_len) {
    size_t nlen = dmp->stack_len * 2;
    PrintNode **nstack = realloc(dmp->stack, nlen * sizeof(*nstack));
    if (nstack == NULL) {
      snprintf(dmp->header.error, sizeof(dmp->header.error), "print stack realloc failed");
      longjmp(dmp->header.catch, 1);
    }
    dmp->stack = nstack;
    dmp->stack_len = nlen;
  }
  dmp->stack[dmp->sp++] = s;
}

static inline PrintNode *stack_pop(ASTDump *dmp) {
  if (dmp->sp == 0) {
    snprintf(dmp->header.error, sizeof(dmp->header.error), "print stack underflow");
    longjmp(dmp->header.catch, 1);
  }
  return dmp->stack[--dmp->sp];
}

static inline void scope_push_new(ASTDump *) {
}

static inline void scope_pop(ASTDump *dmp, const ASTNode *block) {
  PrintNode *block_pn = printnode_new(dmp->depth, strdup("block"), NULL);
  PrintNode *addant = block_pn;
  PrintNode *adde;

  size_t from;
  if (dmp->sp > block->info.block.len) {
    from = dmp->sp - block->info.block.len;
  } else {
    from = 0;
  }

  for (size_t i = from; i < dmp->sp; i++) {
    adde = dmp->stack[i];
    printnode_append(addant, adde);
    addant = adde;
  }
  dmp->sp = from;
  stack_push(dmp, block_pn);
}

static void apply_identifier_ref(ASTDump *dmp, const ASTNode *n) {
  switch (n->kind) {
    case NODE_ID:
      stack_push(dmp, printnode_new(dmp->depth, strdup(colorise(dmp, "id ", n->info.id.value, RED)), NULL));
      break;
    default:
      snprintf(dmp->header.error, sizeof(dmp->header.error), "unknown reference expression kind %d", n->info.expr.op_tok);
      longjmp(dmp->header.catch, 1);
  }
}

static void apply_identifier(ASTDump *dmp, const ASTNode *n) {
  stack_push(dmp, printnode_new(dmp->depth, strdup(colorise(dmp, "id ", n->info.id.value, RED)), NULL));
}

static void apply_expr(ASTDump *dmp, const ASTNode *n) {
  char buf[20] = {};
  switch (n->info.expr.op_tok) {
    case TOK_MINUS: sprintf(buf, "%s", colorise(dmp, "expr ", "-", RED)); break;
    case TOK_PLUS:  sprintf(buf, "%s", colorise(dmp, "expr ", "+", RED)); break;
    case TOK_STAR:  sprintf(buf, "%s", colorise(dmp, "expr ", "*", RED)); break;
    case TOK_SLASH: sprintf(buf, "%s", colorise(dmp, "expr ", "/", RED)); break;
    default:
      snprintf(dmp->header.error, sizeof(dmp->header.error), "unknown expression kind %d", n->info.expr.op_tok);
      longjmp(dmp->header.catch, 1);
  }
  PrintNode *r = stack_pop(dmp);
  PrintNode *l = stack_pop(dmp);
  printnode_append(l, r);
  stack_push(dmp, printnode_new(dmp->depth, strdup(buf), l));
}

static void apply_uexpr(ASTDump *dmp, const ASTNode *n) {
  switch (n->info.uexpr.op_tok) {
    case TOK_MINUS:
      stack_push(dmp, printnode_new(dmp->depth, strdup(colorise(dmp, "uexpr ", "-", RED)), stack_pop(dmp)));
      break;
    default:
      snprintf(dmp->header.error, sizeof(dmp->header.error), "unknown unary expression kind %d", n->info.uexpr.op_tok);
      longjmp(dmp->header.catch, 1);
  }
}

static void apply_if_cond(ASTDump *, const ASTNode *) {
}

static bool apply_if_then_descision(ASTDump *) {
  return true;
}

static bool apply_if_else_descision(ASTDump *) {
  return true;
}

static void printnode_indent_add(PrintNode *pn, size_t by) {
  for (PrintNode *it = pn; it != NULL; it = it->next) it->indent += by;
}

static inline void apply_if_post_universal(ASTDump *dmp, bool has_else, const char *node_type, Color c) {
  PrintNode *else_chain = NULL;
  PrintNode *then_chain;
  PrintNode *cond_chain;
  if (has_else) {
    else_chain = stack_pop(dmp);
    then_chain = stack_pop(dmp);
  } else {
    then_chain = stack_pop(dmp);
  }
  cond_chain = stack_pop(dmp);

  const size_t label_level = dmp->depth + 1;
  printnode_indent_add(cond_chain, 1);
  printnode_indent_add(else_chain, 1);

  PrintNode *cond_pn = printnode_new(label_level, strdup("cond"), cond_chain);
  PrintNode *then_pn = printnode_new(label_level, strdup("then"), then_chain);
  PrintNode *else_pn = NULL;
  if (has_else) {
    else_pn = printnode_new(label_level, strdup("else"), else_chain);
  }

  PrintNode *if_pn = printnode_new(dmp->depth, strdup(colorise(dmp, node_type, "if", c)), cond_pn);
  printnode_append(cond_pn, then_pn);
  printnode_append(then_pn, else_pn);
  stack_push(dmp, if_pn);
}

static void apply_if_post(ASTDump *dmp, const ASTNode *n) {
  bool has_else = n->info.stmt.iff.else_arm != NULL;
  apply_if_post_universal(dmp, has_else, "stmt ", GREEN);
}

static void apply_ifexpr_post(ASTDump *dmp, const ASTNode *n) {
  bool has_else = n->info.ifexpr.else_arm != NULL;
  apply_if_post_universal(dmp, has_else, "expr ", RED);
}

static void value_of(ASTDump *dmp, const ASTNode *n) {
  switch (n->info.val.kind) {
    case VK_INTEGER:
    case VK_BOOL:
      char *at = strdup(colorise(dmp, "val ", n->info.val.value, RED));
      stack_push(dmp, printnode_new(dmp->depth, at, NULL));
      break;
    default:
      snprintf(dmp->header.error, sizeof(dmp->header.error), "unsupported value kind %d", n->info.val.kind);
      longjmp(dmp->header.catch, 1);
  }
}

static void apply_echo(ASTDump *dmp) {
  stack_push(dmp, printnode_new(dmp->depth, strdup(colorise(dmp, "stmt ", "echo", GREEN)), stack_pop(dmp)));
}

static void apply_define(ASTDump *dmp, const ASTNode *n) {
  PrintNode *expr_chain = stack_pop(dmp);
  PrintNode *id_pn = printnode_new(dmp->depth + 1,
      strdup(colorise(dmp, "id ", n->info.stmt.define.id->info.id.value, RED)),
      expr_chain);
  stack_push(dmp, printnode_new(dmp->depth,
      strdup(colorise(dmp, "stmt ", ":=", GREEN)), id_pn));
}

static void apply_assign(ASTDump *dmp) {
  PrintNode *lexpr_chain = stack_pop(dmp);
  PrintNode *expr_chain = stack_pop(dmp);
  printnode_append(lexpr_chain, expr_chain);
  stack_push(dmp, printnode_new(dmp->depth,
      strdup(colorise(dmp, "stmt ", "=", GREEN)), lexpr_chain));
}

static void apply_enter(ASTDump *dmp, ASTNode*) {
  dmp->depth += 1;
}

static void apply_leave(ASTDump *dmp, ASTNode*) {
  dmp->depth -= 1;
}

ASTDump *astdump_new(size_t max_stack_size) {
  ASTDump *dmp = malloc(sizeof(ASTDump));
  assert(dmp != NULL);

  dmp->stack_len = max_stack_size;
  dmp->stack = malloc(max_stack_size * sizeof(*dmp->stack));
  assert(dmp->stack != NULL);
  dmp->sp = 0;

  dmp->colored = isatty(STDOUT_FILENO) && getenv("NO_COLOR") == NULL;
  dmp->depth = 0;

  memset(&dmp->header, 0, sizeof(dmp->header));

  dmp->header.ops.apply_assign = (Applyer)apply_assign;
  dmp->header.ops.apply_define = (NodeApplyer)apply_define;
  dmp->header.ops.apply_echo = (Applyer)apply_echo;
  dmp->header.ops.apply_expr = (NodeApplyer)apply_expr;
  dmp->header.ops.apply_uexpr = (NodeApplyer)apply_uexpr;
  dmp->header.ops.apply_identifier = (NodeApplyer)apply_identifier;
  dmp->header.ops.apply_identifier_ref = (NodeApplyer)apply_identifier_ref;
  dmp->header.ops.value_of = (NodeApplyer)value_of;

  dmp->header.ops.apply_if_cond = (NodeApplyer)apply_if_cond;
  dmp->header.ops.apply_if_then_descision = (CondApplyer)apply_if_then_descision;
  dmp->header.ops.apply_if_else_descision = (CondApplyer)apply_if_else_descision;
  dmp->header.ops.apply_if_post = (NodeApplyer)apply_if_post;
  dmp->header.ops.apply_ifexpr_post = (NodeApplyer)apply_ifexpr_post;

  dmp->header.ops.scope_push_new = (Applyer)scope_push_new;
  dmp->header.ops.scope_pop = (NodeApplyer)scope_pop;

  dmp->header.ops.apply_enter = (NodeApplyer)apply_enter;
  dmp->header.ops.apply_leave = (NodeApplyer)apply_leave;

  memset(dmp->header.error, '\0', sizeof(dmp->header.error));

  return dmp;
}

void astdump_print(ASTDump *dmp) {
  if (dmp->sp == 0) return; // TODO error, stack is empty
  PrintNode *it = NULL;
  for (size_t i = 0; i < dmp->sp; i++) {
    it = dmp->stack[i];
    while (it != NULL) {
      for (size_t i = 1; i < it->indent; i++)
        printf("  ");
      printf("%s\n", it->text);
      PrintNode *free_me = it;
      it = it->next;
      free(free_me->text);
      free(free_me);
    }
  }
  dmp->sp = 0;
}

void astdump_free(ASTDump *dmp) {
  assert(dmp != NULL);
  for (size_t i = 0; i < dmp->sp; i++) {
    PrintNode *pn = dmp->stack[i];
    if (pn != NULL) {
      while (pn != NULL && pn->next != NULL) {
        free(pn->text);
        PrintNode *next = pn->next;
        free(pn);
        pn = next;
      }
      free(pn->text);
      free(pn);
    }
  }
  free(dmp->stack);
  free(dmp);
}

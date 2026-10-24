#include "ast-dump.h"
#include "ast.h"
#include "walker.h"

#include <stddef.h>
#include <unistd.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline PrintChain chain_empty(void) {
  return (PrintChain){NULL, NULL};
}

static PrintChain chain_single(size_t i, const char *t) {
  PrintNode *pn = malloc(sizeof(PrintNode));
  assert(pn != NULL);
  pn->text = strdup(t);
  pn->indent = i;
  pn->next = NULL;
  return (PrintChain){pn, pn};
}

// Prepend a label node to c. The tail of the result is the tail of c.
static PrintChain chain_wrap(size_t i, const char *t, PrintChain c) {
  assert(c.head != NULL || c.tail == NULL);
  assert(c.tail == NULL || c.tail->next == NULL);
  PrintNode *pn = malloc(sizeof(PrintNode));
  assert(pn != NULL);
  pn->text = strdup(t);
  pn->indent = i;
  pn->next = c.head;
  return (PrintChain){pn, c.head == NULL ? pn : c.tail};
}

static void chain_append(PrintChain *dst, PrintChain src) {
  if (src.head == NULL) return;
  assert(src.tail != NULL);
  assert(src.tail->next == NULL);
  if (dst->head == NULL) {
    *dst = src;
    return;
  }
  assert(dst->tail != NULL);
  assert(dst->tail->next == NULL);
  dst->tail->next = src.head;
  dst->tail = src.tail;
}

#define RED_TEXT(s)   "\033[31m" s "\033[0m"
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
        abort();
    }
  } else {
    snprintf(print_to, sizeof(buf) - prefix_len, "%s", s);
    return buf;
  }
  return buf;
}

static inline void stack_push(ASTDump *dmp, PrintChain c) {
  if (dmp->sp >= dmp->stack_len) {
    size_t nlen = dmp->stack_len * 2;
    PrintChain *nstack = realloc(dmp->stack, nlen * sizeof(*nstack));
    if (nstack == NULL) {
      snprintf(dmp->header.error, sizeof(dmp->header.error), "print stack realloc failed");
      longjmp(dmp->header.catch, 1);
    }
    dmp->stack = nstack;
    dmp->stack_len = nlen;
  }
  dmp->stack[dmp->sp++] = c;
}

static inline PrintChain stack_pop(ASTDump *dmp) {
  if (dmp->sp == 0) {
    snprintf(dmp->header.error, sizeof(dmp->header.error), "print stack underflow");
    longjmp(dmp->header.catch, 1);
  }
  return dmp->stack[--dmp->sp];
}

static inline void scope_push_new(ASTDump *) {
}

static inline void scope_pop(ASTDump *dmp, const ASTNode *block) {
  PrintChain block_chain = chain_single(dmp->depth, "block");

  size_t from;
  if (dmp->sp > block->info.block.len) {
    from = dmp->sp - block->info.block.len;
  } else {
    from = 0;
  }

  for (size_t i = from; i < dmp->sp; i++)
    chain_append(&block_chain, dmp->stack[i]);

  dmp->sp = from;
  stack_push(dmp, block_chain);
}

static void apply_identifier_ref(ASTDump *dmp, const ASTNode *n) {
  switch (n->kind) {
    case NODE_ID:
      stack_push(dmp, chain_single(dmp->depth, colorise(dmp, "id ", n->info.id.value, RED)));
      break;
    default:
      snprintf(dmp->header.error, sizeof(dmp->header.error), "unknown reference expression kind %d", n->info.expr.op_tok);
      longjmp(dmp->header.catch, 1);
  }
}

static void apply_identifier(ASTDump *dmp, const ASTNode *n) {
  stack_push(dmp, chain_single(dmp->depth, colorise(dmp, "id ", n->info.id.value, RED)));
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
  PrintChain r = stack_pop(dmp);
  PrintChain l = stack_pop(dmp);
  chain_append(&l, r);
  stack_push(dmp, chain_wrap(dmp->depth, buf, l));
}

static void apply_uexpr(ASTDump *dmp, const ASTNode *n) {
  switch (n->info.uexpr.op_tok) {
    case TOK_MINUS: {
      PrintChain inner = stack_pop(dmp);
      stack_push(dmp, chain_wrap(dmp->depth, colorise(dmp, "uexpr ", "-", RED), inner));
      break;
    }
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
  PrintChain else_chain = chain_empty();
  PrintChain then_chain;
  PrintChain cond_chain;
  if (has_else) {
    else_chain = stack_pop(dmp);
    then_chain = stack_pop(dmp);
  } else {
    then_chain = stack_pop(dmp);
  }
  cond_chain = stack_pop(dmp);

  printnode_indent_add(cond_chain.head, 1);
  if (else_chain.head)
    printnode_indent_add(else_chain.head, 1);

  const size_t label_level = dmp->depth + 1;
  PrintChain chain = chain_wrap(label_level, "cond", cond_chain);
  chain_append(&chain, chain_wrap(label_level, "then", then_chain));
  if (has_else)
    chain_append(&chain, chain_wrap(label_level, "else", else_chain));
  PrintChain if_chain = chain_wrap(dmp->depth, colorise(dmp, node_type, "if", c), chain);
  stack_push(dmp, if_chain);
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
    case VK_BOOL: {
      stack_push(dmp, chain_single(dmp->depth, colorise(dmp, "val ", n->info.val.value, RED)));
      break;
    }
    default:
      snprintf(dmp->header.error, sizeof(dmp->header.error), "unsupported value kind %d", n->info.val.kind);
      longjmp(dmp->header.catch, 1);
  }
}

static void apply_echo(ASTDump *dmp) {
  PrintChain inner = stack_pop(dmp);
  stack_push(dmp, chain_wrap(dmp->depth, colorise(dmp, "stmt ", "echo", GREEN), inner));
}

static void apply_define(ASTDump *dmp, const ASTNode *n) {
  PrintChain expr = stack_pop(dmp);
  PrintChain id_pn = chain_wrap(dmp->depth + 1,
      colorise(dmp, "id ", n->info.stmt.define.id->info.id.value, RED), expr);
  stack_push(dmp, chain_wrap(dmp->depth,
      colorise(dmp, "stmt ", ":=", GREEN), id_pn));
}

static void apply_assign(ASTDump *dmp, const ASTNode*) {
  PrintChain lexpr = stack_pop(dmp);
  PrintChain expr = stack_pop(dmp);
  chain_append(&lexpr, expr);
  stack_push(dmp, chain_wrap(dmp->depth,
      colorise(dmp, "stmt ", "=", GREEN), lexpr));
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

  dmp->header.ops.apply_assign = (NodeApplyer)apply_assign;
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
  if (dmp->sp == 0) {
    snprintf(dmp->header.error, sizeof(dmp->header.error), "dump stack is empty");
    return;
  }

  for (size_t i = 0; i < dmp->sp; i++) {
    PrintNode *it = dmp->stack[i].head;
    while (it != NULL) {
      for (size_t j = 1; j < it->indent; j++)
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
    PrintNode *pn = dmp->stack[i].head;
    while (pn != NULL) {
      PrintNode *next = pn->next;
      free(pn->text);
      free(pn);
      pn = next;
    }
  }
  free(dmp->stack);
  free(dmp);
}

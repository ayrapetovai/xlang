#include "ast.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

ASTNode *node_new(NodeKind kind) {
  ASTNode *n = malloc(sizeof *n);
  n->kind  = kind;
  return n;
}

static void print_indents(int count) {
  for (int i = 0; i < count; i++)
    printf("  ");
}

void ast_dump(const ASTNode *n, int depth) {
  if (n == NULL) return;

  print_indents(depth);

  switch (n->kind) {
    case NODE_BLOCK:
      printf("block\n");
      for (size_t i = 0; i < n->info.block.len; i++) {
        ast_dump(n->info.block.stmts[i], depth + 1);
      }
    break;
    case NODE_STMT:
      printf("stmt %d\n", n->info.stmt.stmt_tok);
      ast_dump(n->info.stmt.expr, depth + 1);
    break;
    case NODE_EXPR:
      printf("expr %d\n", n->info.expr.op_tok);
      ast_dump(n->info.expr.op1, depth + 1);
      ast_dump(n->info.expr.op2, depth + 1);
    break;
    case NODE_VALUE:
      printf("val %s\n", n->info.val.value);
    break;
    default:
  }
}

void node_free(ASTNode *n) {
  if (n == NULL) return;

  switch (n->kind) {
    case NODE_BLOCK:
      for (size_t i = 0; i < n->info.block.len; i++) {
        node_free(n->info.block.stmts[i]);
      }
      free(n->info.block.stmts);
    break;
    case NODE_STMT:
      node_free(n->info.stmt.expr);
    break;
    case NODE_EXPR:
      node_free(n->info.expr.op1);
      node_free(n->info.expr.op2);
    break;
    case NODE_VALUE:
      free((void*) n->info.val.value);
    break;
    default:
  }
  free(n);
}

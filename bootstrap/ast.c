#include "ast.h"
#include "tokens.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define RED_TEXT(s) "\033[31m" s "\033[0m"
#define GREEN_TEXT(s) "\033[32m" s "\033[0m"

ASTNode *node_new(NodeKind kind) {
  ASTNode *n = malloc(sizeof *n);
  assert(n != NULL);
  n->kind = kind;
  return n;
}

static void print_indents(int count) {
  for (int i = 0; i < count; i++)
    printf("  ");
}

static void inner_ast_dump(const ASTNode *n, bool colored, int depth) {
  if (n == NULL) return;

  print_indents(depth);

  switch (n->kind) {
    case NODE_BLOCK:
      printf("block\n");
      for (size_t i = 0; i < n->info.block.len; i++) {
        inner_ast_dump(n->info.block.stmts[i], colored, depth + 1);
      }
    break;
    case NODE_STMT:
      const char* s1;
      if (colored) s1 = "stmt " GREEN_TEXT("%s\n");
      else s1 = "stms %s\n";
      printf(s1, token_kind_to_string(n->info.stmt.stmt_tok));
      inner_ast_dump(n->info.stmt.expr, colored, depth + 1);
    break;
    case NODE_EXPR:
      const char* s2;
      if (colored) s2 = "expr " RED_TEXT("%s\n");
      else s2 = "expr %s\n";
      printf(s2, token_kind_to_string(n->info.expr.op_tok));
      inner_ast_dump(n->info.expr.op1, colored, depth + 1);
      inner_ast_dump(n->info.expr.op2, colored, depth + 1);
    break;
    case NODE_UEXPR:
      const char* s3;
      if (colored) s3 = "uexpr " RED_TEXT("%s\n");
      else s3 = "uexpr %s\n";
      printf(s3, token_kind_to_string(n->info.uexpr.op_tok));
      inner_ast_dump(n->info.uexpr.op, colored, depth + 1);
    break;
    case NODE_VALUE:
      const char* s4;
      if (colored) s4 = "val " RED_TEXT("%s\n");
      else s4 = "val %s\n";
      printf(s4, n->info.val.value);
    break;
    default:
    assert(false);
  }
}

void ast_dump(const ASTNode *n) {
  if (isatty(STDOUT_FILENO)) {
    inner_ast_dump(n, true, 0);
  } else {
    inner_ast_dump(n, false, 0);
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
    case NODE_UEXPR:
      node_free(n->info.uexpr.op);
    break;
    case NODE_VALUE:
      free((void*) n->info.val.value);
    break;
    default:
    assert(false);
  }
  free(n);
}

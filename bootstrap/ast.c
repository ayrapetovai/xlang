#include "ast.h"
#include "tokens.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

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

  const char* s;
  switch (n->kind) {
    case NODE_BLOCK:
      printf("block\n");
      for (size_t i = 0; i < n->info.block.len; i++) {
        inner_ast_dump(n->info.block.stmts[i], colored, depth + 1);
      }
    break;
    case NODE_STMT:
      if (colored) s = "stmt " GREEN_TEXT("%s\n");
      else s = "stms %s\n";
      printf(s, token_kind_to_string(n->info.stmt.stmt_tok));
      switch (n->info.stmt.stmt_tok) {
        case TOK_IF:
          print_indents(depth + 1);
          printf("cond\n");
          inner_ast_dump(n->info.stmt.iff.cond, colored, depth + 2);
          print_indents(depth + 1);
          printf("then\n");
          inner_ast_dump(n->info.stmt.iff.then_arm, colored, depth + 2);
          if (n->info.stmt.iff.else_arm != NULL) {
            print_indents(depth + 1);
            printf("else\n");
            inner_ast_dump(n->info.stmt.iff.else_arm, colored, depth + 2);
          }
        break;
        case TOK_ECHO: inner_ast_dump(n->info.stmt.echo.expr, colored, depth + 1); break;
        default: assert(false);
      }
    break;
    case NODE_EXPR:
      if (colored) s = "expr " RED_TEXT("%s\n");
      else s = "expr %s\n";
      printf(s, token_kind_to_string(n->info.expr.op_tok));
      inner_ast_dump(n->info.expr.op1, colored, depth + 1);
      inner_ast_dump(n->info.expr.op2, colored, depth + 1);
    break;
    case NODE_UEXPR:
      const char* s;
      if (colored) s = "uexpr " RED_TEXT("%s\n");
      else s = "uexpr %s\n";
      printf(s, token_kind_to_string(n->info.uexpr.op_tok));
      inner_ast_dump(n->info.uexpr.op, colored, depth + 1);
    break;
    case NODE_VALUE:
      if (colored) s = "val " RED_TEXT("%s\n");
      else s = "val %s\n";
      printf(s, n->info.val.value);
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
      switch (n->info.stmt.stmt_tok) {
        case TOK_IF:
          node_free(n->info.stmt.iff.cond);
          node_free(n->info.stmt.iff.else_arm);
          node_free(n->info.stmt.iff.then_arm);
        break;
        case TOK_ECHO: node_free(n->info.stmt.echo.expr); break;
        default: assert(false);
      }
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

ASTNode *node_value_new(ValueKind kind, const char *v) {
  ASTNode *n = node_new(NODE_VALUE);
  n->info.val = (InfoValue) {
    .kind = kind,
    .value = strdup(v),
  };
  return n;
}

ASTNode *node_expr_new(TokenKind kind, ASTNode *l, ASTNode *r) {
  ASTNode *n = node_new(NODE_EXPR);
  n->info.expr = (InfoExpr) {
    .op_tok = kind,
    .op1 = l,
    .op2 = r,
  };
  l->parent = n;
  r->parent = n;
  return n;
}

ASTNode *node_uexpr_new(TokenKind kind, ASTNode *expr) {
  ASTNode *n = node_new(NODE_UEXPR);
  n->info.uexpr = (InfoUExpr) {
    .op_tok = kind,
    .op = expr,
  };
  expr->parent = n;
  return n;
}

ASTNode *node_if_new(ASTNode *cond, ASTNode *then_arm, ASTNode *else_arm) {
  ASTNode *n = node_new(NODE_STMT);
  n->info.stmt.stmt_tok = TOK_IF;
  n->info.stmt.iff.cond = cond;
  n->info.stmt.iff.then_arm = then_arm;
  n->info.stmt.iff.else_arm = else_arm;
  return n;
}

ASTNode *node_stmt_echo_new(TokenKind kind, ASTNode *expr) {
  ASTNode *n = node_new(NODE_STMT);
  n->info.stmt = (InfoStmt) {
    .stmt_tok = kind,
    .echo.expr = expr,
  };
  expr->parent = n;
  return n;
}

ASTNode *node_block_new() {
  ASTNode *block = node_new(NODE_BLOCK);
  block->info.block = (InfoBlock) {
    .len = 0,
    .cap = 2,
    .stmts = malloc(2 * sizeof(ASTNode*)),
  };
  assert(block->info.block.stmts != NULL);
  memset(block->info.block.stmts, 0, 2);
  return block;
}

static void ensure_capacity(ASTNode *block, size_t expected_capacity, bool shift_right) {
  InfoBlock *info_block = &block->info.block;
  if (info_block->cap < expected_capacity) {
    size_t new_cap = info_block->cap * 2;
    ASTNode **new_stmts = malloc(new_cap * sizeof(ASTNode*));
    assert(new_stmts != NULL);
    memcpy(new_stmts + (shift_right?1:0), info_block->stmts, info_block->len * sizeof(ASTNode*));
    free(info_block->stmts);
    info_block->stmts = new_stmts;
    info_block->cap = new_cap;
  } else if (shift_right) {
    for (int i = info_block->len; 0 < i; i--)
      info_block->stmts[i] = info_block->stmts[i-1];
    info_block->stmts[0] = NULL;
  }
}

void node_block_append(ASTNode *block, ASTNode *stmt) {
  InfoBlock *info_block = &block->info.block;
  ensure_capacity(block, info_block->len + 1, 0);
  info_block->stmts[info_block->len] = stmt;
  info_block->len += 1;
  stmt->parent = block;
}

void node_block_prepend(ASTNode *block, ASTNode *stmt) {
  InfoBlock *info_block = &block->info.block;
  ensure_capacity(block, info_block->len + 1, 1);
  info_block->stmts[0] = stmt;
  info_block->len += 1;
  stmt->parent = block;
}


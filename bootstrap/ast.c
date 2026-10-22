#include "ast.h"
#include "tokens.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define MAX2(a, b) (((a) > (b)) ? (a) : (b))
#define MAX3(a, b, c) MAX2(MAX2(a,b),c)

ASTNode *node_new(NodeKind kind) {
  ASTNode *n = malloc(sizeof *n);
  assert(n != NULL);
  n->kind = kind;
  n->height = SIZE_MAX; // must be overwritten
  return n;
}

ASTNode *node_id_new(const char* value) {
  ASTNode *n = node_new(NODE_ID);
  n->info.id.value = strdup(value);
  n->height = 1;
  return n;
}

ASTNode *node_value_new(ValueKind kind, const char *v) {
  ASTNode *n = node_new(NODE_VALUE);
  n->info.val = (InfoValue) {
    .kind = kind,
    .value = strdup(v),
  };
  n->height = 1;
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
  n->height = 1 + MAX2(l->height, r->height);
  return n;
}

ASTNode *node_uexpr_new(TokenKind kind, ASTNode *expr) {
  ASTNode *n = node_new(NODE_UEXPR);
  n->info.uexpr = (InfoUExpr) {
    .op_tok = kind,
    .op = expr,
  };
  expr->parent = n;
  n->height = 1 + expr->height;
  return n;
}
void *node_abstract_if_new(ASTNode *cond, ASTNode *then_arm, ASTNode *else_arm) {
  ASTNode **n = malloc(3 * sizeof(ASTNode*));
  n[0] = cond;
  n[1] = then_arm;
  n[2] = else_arm;
  return n;
}

ASTNode *node_if_new(ASTNode *cond, ASTNode *then_arm, ASTNode *else_arm) {
  ASTNode *n = node_new(NODE_STMT);
  n->info.stmt.stmt_tok = TOK_IF;
  n->info.stmt.iff.cond = cond;
  n->info.stmt.iff.then_arm = then_arm;
  n->info.stmt.iff.else_arm = else_arm;
  n->height = 1 + MAX3(cond->height, then_arm->height, (else_arm != NULL ? else_arm->height : 0));
  return n;
}

ASTNode *node_ifexpr_new(ASTNode *cond, ASTNode *then_arm, ASTNode *else_arm) {
  ASTNode *n = node_new(NODE_IFEXPR);
  n->info.ifexpr.cond = cond;
  n->info.ifexpr.then_arm = then_arm;
  n->info.ifexpr.else_arm = else_arm;
  n->height = 1 + MAX3(cond->height, then_arm->height, (else_arm != NULL ? else_arm->height : 0));
  return n;
}

ASTNode *node_stmt_echo_new(TokenKind kind, ASTNode *expr) {
  ASTNode *n = node_new(NODE_STMT);
  n->info.stmt = (InfoStmt) {
    .stmt_tok = kind,
    .echo.expr = expr,
  };
  expr->parent = n;
  n->height = 1 + expr->height;
  return n;
}

ASTNode *node_definition_new(ASTNode *id_node, ASTNode *expr) {
  ASTNode *n = node_new(NODE_STMT);
  n->info.stmt = (InfoStmt) {
    .stmt_tok = TOK_DEFINE,
    .define.id = id_node,
    .define.type_expr = NULL,
    .define.expr = expr,
  };
  expr->parent = n;
  id_node->parent = n;
  n->height = 1 + MAX2(id_node->height, expr->height);
  return n;
}

ASTNode *node_assignment_new(ASTNode *lexpr, ASTNode *expr) {
  ASTNode *n = node_new(NODE_STMT);
  n->info.stmt = (InfoStmt) {
    .stmt_tok = TOK_ASSIGN,
    .assign.lexpr = lexpr,
    .assign.expr = expr,
  };
  lexpr->parent = n;
  expr->parent = n;
  n->height = 1 + MAX2(lexpr->height, expr->height);
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
  memset(block->info.block.stmts, 0, 2 * sizeof(ASTNode*));
  block->height = 1;
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
  block->height = MAX2(block->height, stmt->height + 1);
}

void node_block_prepend(ASTNode *block, ASTNode *stmt) {
  InfoBlock *info_block = &block->info.block;
  ensure_capacity(block, info_block->len + 1, 1);
  info_block->stmts[0] = stmt;
  info_block->len += 1;
  stmt->parent = block;
  block->height = MAX2(block->height, stmt->height + 1);
}


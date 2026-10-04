#ifndef AST_H
#define AST_H

#include "tokens.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

typedef enum NodeKind {
  NODE_EXPR,
  NODE_UEXPR,
  NODE_VALUE,
  NODE_ID,
  NODE_STMT,
  NODE_BLOCK,
} NodeKind;

typedef enum ValueKind {
  VK_INTEGER,
  VK_FLOAT,
  VK_STRING,
  VK_CHAR,
  VK_BOOL,
} ValueKind;

typedef struct InfoFunDef {

} InfoFunDef;

typedef struct InfoExpr {
  TokenKind op_tok;     // +, -, /, *, == ...
  struct ASTNode *op1;  // expr or value
  struct ASTNode *op2;  // expr or value
} InfoExpr;

typedef struct InfoUExpr {
  TokenKind op_tok;    // -x, *p, &p, ++i, i++
  struct ASTNode *op;  // expr or value
} InfoUExpr;

typedef struct InfoValue {
  ValueKind kind;     // integer, string, float
  const char* value;  // "123", "abc", "3.14"
} InfoValue;

typedef struct InfoStmt {
  TokenKind stmt_tok; // :=, if, echo, loop, match, select, func
  union {
    struct { struct ASTNode *expr; } echo;               // `echo` expr
    struct { struct ASTNode *cond, *then, *else_; } if_; // `if` expr block(*) `else` block(*); `if` expr `then` block(1) `else` block(1)
    struct {
      const char* name;      // function name
      size_t param_len;
      struct ASTNode **params;
      struct ASTNode **body; // block
    } funcDef;
  };
} InfoStmt;

typedef struct InfoBlock {
  size_t len;
  size_t cap;
  struct ASTNode **stmts;
} InfoBlock;

typedef struct ASTNode {
  struct ASTNode *parent;
  NodeKind kind;
  union {
    struct InfoExpr expr;
    struct InfoUExpr uexpr;
    struct InfoStmt stmt;
    struct InfoValue val;
    struct InfoBlock block;
    // ...
  } info;
} ASTNode;

typedef struct ASTNodeList {
  size_t len;
  size_t cap;
  struct ASTNode **nodes;
} ASTNodeList;

ASTNode *node_new(NodeKind kind);
ASTNode *node_value_new(ValueKind kind, const char *str_reprentation);
ASTNode *node_expr_new(TokenKind kind, ASTNode *left_expr, ASTNode *right_expr);
ASTNode *node_uexpr_new(TokenKind kind, ASTNode *expr);
ASTNode *node_stmt_echo_new(TokenKind kind, ASTNode *expr);
ASTNode *node_block_new();
void node_block_append(ASTNode *block, ASTNode *stmt);
void node_block_prepend(ASTNode *block, ASTNode *stmt);
void node_free(ASTNode *);
void ast_dump(const ASTNode *);


#endif


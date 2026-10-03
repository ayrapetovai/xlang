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
  const char* name;
  size_t param_len;
  struct ASTNode **params;
  struct ASTNode **body;
} InfoFunDef;

typedef struct InfoExpr {
  TokenKind op_tok; // +, -, /, *, == ...
  struct ASTNode *op1;
  struct ASTNode *op2;
} InfoExpr;

typedef struct InfoUExpr {
  TokenKind op_tok; // -x, *p, &p, ++i, i++
  struct ASTNode *op;
} InfoUExpr;

typedef struct InfoValue {
  ValueKind kind;
  const char* value;
} InfoValue;

typedef struct InfoStmt {
  TokenKind stmt_tok; // :=, if, echo, loop, match, select
  struct ASTNode *expr;
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
    struct InfoFunDef funDef;
    struct InfoBlock block;
    // ...
  } info;
} ASTNode;

typedef struct ASTNodeList {
  size_t len;
  size_t cap;
  struct ASTNode **nodes;
} ASTNodeList;

ASTNode *node_new(NodeKind);
ASTNode *node_block_new();
void node_block_append(ASTNode *block, ASTNode *stmt);
void node_block_prepend(ASTNode *block, ASTNode *stmt);
void node_free(ASTNode *);
void ast_dump(const ASTNode *);


#endif


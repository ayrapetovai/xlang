#ifndef AST_H
#define AST_H

#include "tokens.h"
#include <stdbool.h>
#include <inttypes.h>
#include <stdlib.h>

typedef enum {
  NODE_EXPR,
  NODE_UEXPR,
  NODE_IFEXPR,
  NODE_VALUE,
  NODE_ID,
  NODE_STMT,
  NODE_BLOCK,
} NodeKind;

typedef enum {
  VK_INTEGER,
  VK_FLOAT,
  VK_STRING,
  VK_CHAR,
  VK_BOOL,
} ValueKind;

typedef struct {

} InfoFunDef;

typedef struct {
  TokenKind op_tok;     // +, -, /, *, ==, if ...
  struct ASTNode *op1;  // expr or value
  struct ASTNode *op2;  // expr or value
} InfoExpr;

typedef struct {
  TokenKind op_tok;    // -x, *p, &p, ++i, i++
  struct ASTNode *op;  // expr or value
} InfoUExpr;

typedef struct {
  ValueKind kind;     // integer, string, float
  const char* value;  // "123", "abc", "3.14"
} InfoValue;

typedef struct {
  struct ASTNode *cond, *then_arm, *else_arm;
} InfoStmtIf;

typedef struct InfoStmt {
  TokenKind stmt_tok; // :=, =, if, echo, loop, match, select, func
  union {
    struct {
      struct ASTNode *id;         // variable (or function) name
      struct ASTNode *type_expr;  // type descripting expression
      struct ASTNode *expr;       // initializing expression
    } define;                     // x := expr (type_expr == NULL) or x type := expr (type_expr != NULL)
    struct {
      struct ASTNode *lexpr;      // lvalue expression (writeable memory location)
      struct ASTNode *expr;
    } assign;                     // x = expr
    struct { struct ASTNode *expr; } echo;                      // `echo` expr
    struct { struct ASTNode *cond, *then_arm, *else_arm; } iff; // `if` expr block(*) `else` block(*); `if` expr `then` block(1) `else` block(1)
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

typedef struct InfoId {
  char *value;
} InfoId;

typedef struct InfoIfExpr {
  struct ASTNode *cond, *then_arm, *else_arm;
} InfoIfExpr;

typedef struct ASTNode {
  struct ASTNode *parent;
  NodeKind kind;
  size_t height; // how many children in depth does this node holds, trivial are 1
  size_t line;
  size_t col;
  union {
    InfoId id;
    InfoIfExpr ifexpr;
    InfoExpr expr;
    InfoUExpr uexpr;
    InfoStmt stmt;
    InfoValue val;
    InfoBlock block;
    // ...
  } info;
} ASTNode;

ASTNode *node_new(NodeKind kind, size_t line, size_t col);
ASTNode *node_id_new(const char* value, size_t line, size_t col);
ASTNode *node_value_new(ValueKind kind, const char *str_reprentation, size_t line, size_t col);
ASTNode *node_expr_new(TokenKind kind, ASTNode *left_expr, ASTNode *right_expr, size_t line, size_t col);
ASTNode *node_uexpr_new(TokenKind kind, ASTNode *expr, size_t line, size_t col);
void    *node_abstract_if_new(ASTNode *cond, ASTNode *then_arm, ASTNode *else_arm);
ASTNode *node_if_new(ASTNode *cond, ASTNode *then_arm, ASTNode *else_arm, size_t line, size_t col);
ASTNode *node_ifexpr_new(ASTNode *cond, ASTNode *then_arm, ASTNode *else_arm, size_t line, size_t col);
ASTNode *node_stmt_echo_new(TokenKind kind, ASTNode *expr, size_t line, size_t col);
ASTNode *node_definition_new(ASTNode *id_node, ASTNode *expr, size_t line, size_t col);
ASTNode *node_assignment_new(ASTNode *lexpr, ASTNode *expr, size_t line, size_t col);
ASTNode *node_block_new();
void node_block_append(ASTNode *block, ASTNode *stmt);
void node_block_prepend(ASTNode *block, ASTNode *stmt);

#endif


#include "exec.h"
#include "ast.h"
#include "tokens.h"
#include <assert.h>
#include <stdio.h>

int exec(const ASTNode* n) {
  if (n == NULL) return 0;

  switch (n->kind) {
    case NODE_BLOCK:
      if (n->info.block.len <= 0) assert(false);
      for (size_t i = 0; i < n->info.block.len; i++) {
        exec(n->info.block.stmts[i]);
      }
    break;
    case NODE_STMT:
      switch (n->info.stmt.stmt_tok) {
        case TOK_ECHO:
          printf("%d\n", exec(n->info.stmt.echo.expr));
          break;
        default: assert(false);
      }
    break;
    case NODE_EXPR:
      TokenKind op = n->info.expr.op_tok;
      switch (op) {
        case TOK_MINUS: return exec(n->info.expr.op1) - exec(n->info.expr.op2);
        case TOK_PLUS:  return exec(n->info.expr.op1) + exec(n->info.expr.op2);
        case TOK_STAR:  return exec(n->info.expr.op1) * exec(n->info.expr.op2);
        case TOK_SLASH: return exec(n->info.expr.op1) / exec(n->info.expr.op2);
        default:
          assert(false);
      }
    break;
    case NODE_UEXPR:
      if (n->info.uexpr.op_tok == TOK_MINUS)
        return - exec(n->info.uexpr.op);
      else
        assert(false);
    break;
    case NODE_VALUE:
      if (n->info.val.kind == VK_INTEGER)
        return atoi(n->info.val.value);
      else
        assert(false);
    break;
    default:
      assert(false);
  }
  return 0;
}

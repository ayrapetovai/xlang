#include "exec.h"
#include "ast.h"
#include "tokens.h"
#include <assert.h>
#include <stdio.h>

int exec(const ASTNode* n) {
  if (n == NULL) return 0;

  switch (n->kind) {
    case NODE_BLOCK:
      for (size_t i = 0; i < n->info.block.len; i++) {
        exec(n->info.block.stmts[i]);
      }
    break;
    case NODE_STMT:
      if (n->info.stmt.stmt_tok == TOK_ECHO) {
        int res = exec(n->info.stmt.expr);
        printf("%d\n", res);
      } else {
        assert(false);
      }
    break;
    case NODE_EXPR:
      TokenKind op = n->info.expr.op_tok;
      switch (op) {
        case TOK_MINUS: {
          if (n->info.expr.op2 == NULL) {
            return - exec(n->info.expr.op1);
          }
          return exec(n->info.expr.op1) - exec(n->info.expr.op2);
        }
        case TOK_PLUS: return exec(n->info.expr.op1) + exec(n->info.expr.op2);
        case TOK_STAR: return exec(n->info.expr.op1) * exec(n->info.expr.op2);
        case TOK_SLASH: return exec(n->info.expr.op1) / exec(n->info.expr.op2);
        default:
          assert(false);
      }
    break;
    case NODE_VALUE:
      if (n->info.val.kind == VK_INTEGER) {
        return atoi(n->info.val.value);
      } else {
        assert(false);
      }
    break;
    default:
      assert(false);
  }
  return 0;
}

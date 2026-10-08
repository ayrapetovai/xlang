#include "exec.h"
#include "ast.h"
#include "tokens.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int exec(const ASTNode* n) {
  if (n == NULL) return 0;

  switch (n->kind) {
    case NODE_BLOCK:
      for (size_t i = 0; i < n->info.block.len; i++) {
        exec(n->info.block.stmts[i]);
      }
    break;
    case NODE_STMT:
      switch (n->info.stmt.stmt_tok) {
        case TOK_IF:
          if (exec(n->info.stmt.iff.cond))
            return exec(n->info.stmt.iff.then_arm);
          else
            return exec(n->info.stmt.iff.else_arm);
        case TOK_ECHO:
          printf("%d\n", exec(n->info.stmt.echo.expr));
          return 0;
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
        case TOK_SLASH:
          int op2 = exec(n->info.expr.op2);
          assert(op2 != 0);
          return exec(n->info.expr.op1) / op2;
          break;
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
      switch (n->info.val.kind) {
        case VK_INTEGER: return atoi(n->info.val.value);
        case VK_BOOL: return strcmp(n->info.val.value, "true") == 0;
        default:
          assert(false);
      }
    break;
    default:
      assert(false);
  }
  return 0;
}

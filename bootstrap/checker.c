#include "checker.h"
#include "ast.h"
#include "walker.h"
#include "typetab.h"
#include "tokens.h"
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#define FAIL_IF_TMPL(cond, tmpl, ...) do { if (cond) { snprintf(c->header.error, sizeof(c->header.error), tmpl, __VA_ARGS__); longjmp(c->header.catch, 1); } } while(false)
#define FAIL_IF_MSG(cond, msg)        do { if (cond) { snprintf(c->header.error, sizeof(c->header.error), msg);               longjmp(c->header.catch, 1); } } while(false)

static inline void scope_push(Checker *c) {
  if (c->scope_len >= c->scope_cap) {
    size_t ncap = c->scope_cap ? c->scope_cap * 2 : 16;
    TypeScope **ns = realloc(c->scopes, ncap * sizeof(*ns));
    assert(ns);
    c->scopes = ns;
    c->scope_cap = ncap;
  }
  c->scopes[c->scope_len++] = TypeScope_new(NULL);
}

static inline void scope_pop(Checker *c) {
  if (c->scope_len == 0) return;
  TypeScope *hm = c->scopes[--c->scope_len];
  if (hm) TypeScope_free(hm);
}

static inline void scope_put(Checker *c, const char *name, TypeId tid) {
  if (c->scope_len == 0) scope_push(c);
  TypeScope_put(c->scopes[c->scope_len - 1], name, tid);
}

static TypeId scope_get(Checker *c, const char *name) {
  for (int i = (int)c->scope_len - 1; i >= 0; i--) {
    TypeId v = TypeScope_get(c->scopes[i], name);
    if (v) return v;
  }
  TypeId v = TypeScope_get(c->globals, name);
  if (v) return v;
  return TYPE_UNKNOWN_ID;
}

static void apply_scope_push(Checker *c) { scope_push(c); }
static void apply_scope_pop(Checker *c, const ASTNode *n) { (void)n; scope_pop(c); }
static void do_nothing_node(Checker *c, const ASTNode *n) { (void)c; (void)n; }
static bool return_true(Checker *c) { (void)c; return true; }

static void value_of(Checker *c, const ASTNode *n) {
  ASTNode *an = (ASTNode *)n;
  if (an && an->kind == NODE_VALUE) {
    switch (an->info.val.kind) {
      case VK_INTEGER: an->type = typetab_find_by_name(c->type_table, "int"); break;
      case VK_BOOL:    an->type = typetab_find_by_name(c->type_table, "bool"); break;
      case VK_STRING:  an->type = typetab_find_by_name(c->type_table, "string"); break;
      case VK_FLOAT:   an->type = typetab_find_by_name(c->type_table, "float"); break;
      case VK_CHAR:    an->type = typetab_find_by_name(c->type_table, "char"); break;
      default:
        FAIL_IF_TMPL(true, "unknown value kind %d", an->info.val.kind);
    }
  }
}

static void apply_identifier(Checker *c, const ASTNode *n) {
  ASTNode *an = (ASTNode *)n;
  FAIL_IF_MSG(an == NULL, "indetifier node is NULL");
  if (an->kind == NODE_ID) an->type = scope_get(c, an->info.id.value);
  else FAIL_IF_TMPL(true, "name %s is undefined", n->info.id.value);
}

static void apply_identifier_ref(Checker *c, const ASTNode *n) {
  ASTNode *an = (ASTNode *)n;
  FAIL_IF_MSG(an == NULL, "indetifier ref node is NULL");
  if (an->kind == NODE_ID) an->type = scope_get(c, an->info.id.value);
  else FAIL_IF_TMPL(true, "name %s is undefined", n->info.id.value);
}

static void apply_expr(Checker *c, const ASTNode *n) {
  ASTNode *an = (ASTNode *)n;
  FAIL_IF_MSG(an == NULL, "expr node is NULL");
  if (an->kind == NODE_EXPR) {
    char *operator_name = NULL;
    switch (an->info.expr.op_tok) {
      case TOK_PLUS:  operator_name = "operator+";  break;
      case TOK_MINUS: operator_name = "operator-";  break;
      case TOK_STAR:  operator_name = "operator*";  break;
      case TOK_SLASH: operator_name = "operator/";  break;
      case TOK_EQ:    operator_name = "operator=="; break;
      case TOK_NE:    operator_name = "operator!="; break;
      case TOK_LT:    operator_name = "operator<";  break;
      case TOK_GT:    operator_name = "operator>";  break;
      case TOK_AND:   operator_name = "operator&&"; break;
      case TOK_OR:    operator_name = "operator||"; break;
      default:
        FAIL_IF_TMPL(true, "unknown binary operator %s", token_kind_to_string(an->info.expr.op_tok));
    }

    TypeId operator_type_id = functab_find(c->func_table, operator_name);
    FAIL_IF_TMPL(operator_type_id == TYPE_UNKNOWN_ID,        "%s is not registered", operator_name);

    Type *operator_type = typetab_get(c->type_table, operator_type_id);
    FAIL_IF_TMPL(operator_type == NULL,                      "%s is not found", operator_name);
    FAIL_IF_TMPL(operator_type->base != TB_FUNC,             "%s is not a function, %d", operator_name, operator_type->base);
    FAIL_IF_TMPL(operator_type->props.func.param_count != 2, "%s takes %d parameters, given 2", operator_name, operator_type->props.func.param_count);

    TypeId left_param_typeid = operator_type->props.func.params[0].type;
    TypeId right_param_typeid = operator_type->props.func.params[1].type;

    // TODO: convergence check, ownership check, lifetime check

    if (left_param_typeid != n->info.expr.op1->type) {
      char op_sig[128] = {};
      typetab_render_type_buf(c->type_table, operator_type_id, op_sig, 128);
      char op_type[128] = {};
      typetab_render_type_buf(c->type_table, left_param_typeid, op_type, 128);
      char arg_type[128] = {};
      typetab_render_type_buf(c->type_table, n->info.expr.op1->type, arg_type, 128);
      FAIL_IF_TMPL(true,  "1 parameter of %s %s has type %s, incompatible with argument type %s", operator_name, op_sig, op_type, arg_type);
    }

    if (right_param_typeid != n->info.expr.op2->type) {
      char op_sig[128] = {};
      typetab_render_type_buf(c->type_table, operator_type_id, op_sig, 128);
      char op_type[128] = {};
      typetab_render_type_buf(c->type_table, right_param_typeid, op_type, 128);
      char arg_type[128] = {};
      typetab_render_type_buf(c->type_table, n->info.expr.op2->type, arg_type, 128);
      FAIL_IF_TMPL(true, "2 parameter of %s %s has type %s, incompatible with argument type %s", operator_name, op_sig, op_type, arg_type);
    }

    an->type = operator_type->props.func.return_type;
  } else
    FAIL_IF_TMPL(an == NULL, "wrong ast node type %d", an->kind);
}

static void apply_uexpr(Checker *c, const ASTNode *n) {
  (void)c;
  ASTNode *an = (ASTNode *)n;
  if (an && an->kind == NODE_UEXPR) {
    an->type = 2;
  } else if (an) {
    an->type = TYPE_UNKNOWN_ID;
  }
}

static void apply_if_cond(Checker *c, const ASTNode *n) {
  (void)c;
  ASTNode *an = (ASTNode *)n;
  if (an) an->type = 5;
}

static void apply_if_post(Checker *c, const ASTNode *n) {
  (void)c;
  ASTNode *an = (ASTNode *)n;
  if (an) an->type = TYPE_NONE_ID;
}

static void apply_ifexpr_post(Checker *c, const ASTNode *n) {
  (void)c;
  ASTNode *an = (ASTNode *)n;
  if (!an) return;
  if (an->info.ifexpr.then_arm && an->info.ifexpr.else_arm) {
    if (an->info.ifexpr.else_arm->type == TYPE_UNKNOWN_ID && an->info.ifexpr.then_arm->type != TYPE_UNKNOWN_ID)
      an->type = an->info.ifexpr.then_arm->type;
    else if (an->info.ifexpr.then_arm->type == TYPE_UNKNOWN_ID && an->info.ifexpr.else_arm->type != TYPE_UNKNOWN_ID)
      an->type = an->info.ifexpr.else_arm->type;
    else if (an->info.ifexpr.else_arm->type == an->info.ifexpr.then_arm->type)
      an->type = an->info.ifexpr.then_arm->type;
    else if (an->info.ifexpr.then_arm->type != TYPE_UNKNOWN_ID)
      an->type = an->info.ifexpr.then_arm->type;
    else if (an->info.ifexpr.else_arm->type != TYPE_UNKNOWN_ID)
      an->type = an->info.ifexpr.else_arm->type;
    else
      an->type = TYPE_UNKNOWN_ID;
  } else if (an->info.ifexpr.then_arm) {
    an->type = an->info.ifexpr.then_arm->type;
  } else {
    an->type = TYPE_NONE_ID;
  }
}

static void apply_define(Checker *c, const ASTNode *n) {
  FAIL_IF_MSG(n == NULL, "failed to define, node is NULL");
  ASTNode *an = (ASTNode *)n;
  if (an->kind == NODE_STMT && an->info.stmt.stmt_tok == TOK_DEFINE) {
    TypeId tid = TYPE_UNKNOWN_ID;
    if (an->info.stmt.define.expr)
      tid = ((ASTNode *)an->info.stmt.define.expr)->type;

    FAIL_IF_MSG(tid == TYPE_UNKNOWN_ID, "define failed, undefined type of initializing expression");

    if (an->info.stmt.define.id && an->info.stmt.define.id->kind == NODE_ID) {
      scope_put(c, ((ASTNode *)an->info.stmt.define.id)->info.id.value, tid);
      ((ASTNode *)an->info.stmt.define.id)->type = tid;
    }
    an->type = TYPE_NONE_ID;
  } else
    FAIL_IF_MSG(true, "failed to define, wrong node types");
}

static void apply_assign(Checker *c, const ASTNode *n) {
  ASTNode *an = (ASTNode *)n;
  if (an) an->type = typetab_find_by_name(c->type_table, "void");
}

static void apply_echo(Checker *c) {
  (void)c;
}

Checker *checker_new(void) {
  Checker *c = calloc(1, sizeof(Checker));
  assert(c);
  memset(&c->header, 0, sizeof(c->header));
  c->type_table = typetab_new();
  c->func_table = functab_new(c->type_table);
  c->globals = TypeScope_new(NULL);
  c->scopes = calloc(16, sizeof(struct HashMap*));
  c->scope_cap = 16;
  c->scope_len = 0;
  strcpy(c->current_namespace, "main");
  
  c->header.ops.apply_assign = (NodeApplyer)apply_assign;
  c->header.ops.apply_define = (NodeApplyer)apply_define;
  c->header.ops.apply_echo = (Applyer)apply_echo;
  c->header.ops.apply_expr = (NodeApplyer)apply_expr;
  c->header.ops.apply_uexpr = (NodeApplyer)apply_uexpr;
  c->header.ops.apply_identifier = (NodeApplyer)apply_identifier;
  c->header.ops.apply_identifier_ref = (NodeApplyer)apply_identifier_ref;
  c->header.ops.value_of = (NodeApplyer)value_of;
  c->header.ops.apply_if_cond = (NodeApplyer)apply_if_cond;
  c->header.ops.apply_if_then_descision = (CondApplyer)return_true;
  c->header.ops.apply_if_else_descision = (CondApplyer)return_true;
  c->header.ops.apply_if_post = (NodeApplyer)apply_if_post;
  c->header.ops.apply_ifexpr_post = (NodeApplyer)apply_ifexpr_post;
  c->header.ops.scope_push_new = (Applyer)apply_scope_push;
  c->header.ops.scope_pop = (NodeApplyer)apply_scope_pop;
  c->header.ops.apply_enter = (NodeApplyer)do_nothing_node;
  c->header.ops.apply_leave = (NodeApplyer)do_nothing_node;
  
  memset(c->header.error, '\0', sizeof(c->header.error));
  return c;
}

void checker_free(Checker *c) {
  if (c == NULL) return;
  typetab_free(c->type_table);
  functab_free(c->func_table);
  if (c->globals) TypeScope_free(c->globals);
  for (size_t i = 0; i < c->scope_len; i++) {
    if (c->scopes[i]) TypeScope_free(c->scopes[i]);
  }
  free(c->scopes);
  free(c);
}

TypeTable *checker_get_type_table(Checker *c) {
  return c ? c->type_table : NULL;
}

FuncTable *checker_get_func_table(Checker *c) {
  return c ? c->func_table : NULL;
}

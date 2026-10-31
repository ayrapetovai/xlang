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

static void check_ownership_transfer(Checker *c, TypeId from, TypeId to_what) {
  // TODO: can_giveup_ownership(), can_take_ownership()
  Type *from_type = env_get_type_by_id(c->env, from);
  FAIL_IF_TMPL(from_type == NULL, "unknown type_id %d, type is NULL", from);
  Type *to_what_type = env_get_type_by_id(c->env, to_what);
  FAIL_IF_TMPL(to_what_type == NULL, "unknown type_id %d, type is NULL", to_what);

  // FIXME: this is not a real check, just a stub
  FAIL_IF_TMPL(from_type->flags.is_primitive == false,
               "cannot transfer ownership of non-primitive value of type %s", env_render_type1(c->env, from));
  FAIL_IF_TMPL(to_what_type->flags.is_primitive == false && to_what_type->flags.is_owning == false,
               "cannot transfer ownership to non-owning type %s", env_render_type1(c->env, to_what));
}

static void apply_scope_push(Checker *c) { scope_push(c); }
static void apply_scope_pop(Checker *c, const ASTNode *n) { (void)n; scope_pop(c); }
static void do_nothing_node(Checker *c, const ASTNode *n) { (void)c; (void)n; }
static bool return_true(Checker *c) { (void)c; return true; }

static void value_of(Checker *c, const ASTNode *n) {
  ASTNode *an = (ASTNode *)n;
  if (an && an->kind == NODE_VALUE) {
    switch (an->info.val.kind) {
      case VK_INTEGER: an->type = env_find_typeid_by_name(c->env, "int"); break;
      case VK_BOOL:    an->type = env_find_typeid_by_name(c->env, "bool"); break;
      case VK_STRING:  an->type = env_find_typeid_by_name(c->env, "string"); break;
      case VK_FLOAT:   an->type = env_find_typeid_by_name(c->env, "float"); break;
      case VK_CHAR:    an->type = env_find_typeid_by_name(c->env, "char"); break;
      default:
        FAIL_IF_TMPL(true, "value: unknown ast node kind %d", an->info.val.kind);
    }
  }
}

static void apply_identifier(Checker *c, const ASTNode *n) {
  ASTNode *an = (ASTNode *)n;
  TypeId identifier_typeid = TYPE_UNKNOWN_ID;
  FAIL_IF_MSG(an == NULL, "idetifier node is NULL");
  if (an->kind == NODE_ID) identifier_typeid = scope_get(c, an->info.id.value);
  else FAIL_IF_TMPL(true, "name %s is undefined", n->info.id.value);
  FAIL_IF_TMPL(identifier_typeid == TYPE_UNKNOWN_ID, "identifier %s is not defined", n->info.id.value);
  an->type = identifier_typeid;
}

static void apply_identifier_ref(Checker *c, const ASTNode *n) {
  ASTNode *an = (ASTNode *)n;
  TypeId identifier_typeid = TYPE_UNKNOWN_ID;
  FAIL_IF_MSG(an == NULL, "idetifier ref node is NULL");
  if (an->kind == NODE_ID) identifier_typeid = scope_get(c, an->info.id.value);
  else FAIL_IF_TMPL(true, "name %s is undefined", n->info.id.value);
  FAIL_IF_TMPL(identifier_typeid == TYPE_UNKNOWN_ID, "identifier %s is not defined", n->info.id.value);
  an->type = identifier_typeid;
}

static void apply_expr(Checker *c, const ASTNode *n) {
  ASTNode *an = (ASTNode *)n;
  FAIL_IF_MSG(an == NULL, "expr: node is NULL");
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
        FAIL_IF_TMPL(true, "expr: unknown binary operator %s", token_kind_to_string(an->info.expr.op_tok));
    }

    TypeId operator_type_id = env_find_func_type_name_params(c->env, operator_name, n->info.expr.op1->type, n->info.expr.op2->type);
    FAIL_IF_TMPL(operator_type_id == TYPE_NONE_ID, "expr: %s is not defined for types %s and %s", operator_name,
                  env_render_type1(c->env, n->info.expr.op1->type),
                  env_render_type2(c->env, n->info.expr.op2->type));

    Type *operator_type = env_get_type_by_id(c->env, operator_type_id);
    FAIL_IF_TMPL(operator_type == NULL,                      "expr: %s is not found", operator_name);
    FAIL_IF_TMPL(operator_type->base != TB_FUNC,             "expr: %s is not a function, %d", operator_name, operator_type->base);
    FAIL_IF_TMPL(operator_type->props.func.param_count != 2, "expr: %s takes %d parameters, given 2", operator_name, operator_type->props.func.param_count);

    TypeId operator_op1_typeid = operator_type->props.func.params[0].type;
    TypeId operator_op2_typeid = operator_type->props.func.params[1].type;

    // TODO: lifetime check

    // convergence check
    // if (operator_op1_typeid != n->info.expr.op1->type) {
    //   TypeId converter = env_find_func_type_name_ret_params(c->env, "from", operator_op1_typeid, n->info.expr.op1->type);
    //   FAIL_IF_TMPL(converter == TYPE_NONE_ID, "no converter found from type %s to type %s",
    //                env_render_type1(c->env, n->info.expr.op1->type),
    //                env_render_type2(c->env, operator_op1_typeid));
    // }

    // ownership check
    check_ownership_transfer(c, n->info.expr.op1->type, operator_op1_typeid);
    check_ownership_transfer(c, n->info.expr.op2->type, operator_op2_typeid);

    // FAIL_IF_TMPL(operator_op1_typeid != n->info.expr.op1->type, "1 parameter of %s %s has type %s, incompatible with argument type %s", operator_name,
    //               env_render_type1(c->env, operator_type_id),
    //               env_render_type2(c->env, operator_op1_typeid),
    //               env_render_type3(c->env, n->info.expr.op1->type));
    //
    // FAIL_IF_TMPL(operator_op2_typeid != n->info.expr.op2->type, "2 parameter of %s %s has type %s, incompatible with argument type %s", operator_name,
    //               env_render_type1(c->env, operator_type_id),
    //               env_render_type2(c->env, operator_op2_typeid),
    //               env_render_type3(c->env,  n->info.expr.op2->type));

    an->type = operator_type->props.func.return_type;
  } else
    FAIL_IF_TMPL(true, "expr: wrong ast node type %d", an->kind);
}

static void apply_uexpr(Checker *c, const ASTNode *n) {
  ASTNode *an = (ASTNode *)n;
  FAIL_IF_MSG(an == NULL, "uexpr: node is NULL");
  if (an->kind == NODE_UEXPR) {
    const char *operator_name;
    switch (n->info.uexpr.op_tok) {
      case TOK_MINUS: operator_name = "operator-"; break;
      case TOK_BANG:  operator_name = "operator!"; break;
      default:
        FAIL_IF_TMPL(true, "uexpr: unknown operator %s", token_kind_to_string(an->info.uexpr.op_tok));
    }
    TypeId op_typeid = an->info.uexpr.op->type;
    TypeId operator_typeid = env_find_func_type_name_params(c->env, operator_name, op_typeid);
    FAIL_IF_TMPL(operator_typeid == TYPE_NONE_ID, "uexpr: %s is not defined for type %s", operator_name, env_render_type1(c->env, op_typeid));
    Type *operator_type = env_get_type_by_id(c->env, operator_typeid);
    FAIL_IF_MSG(operator_type == NULL, "uexpr: got wrong type id");
    FAIL_IF_TMPL(operator_type->props.func.param_count != 1, "uexpr: invalid amount of paramters %d", operator_type->props.func.param_count );
    check_ownership_transfer(c, op_typeid, operator_type->props.func.params[0].type);

    an->type = operator_type->props.func.return_type;
  } else
    FAIL_IF_TMPL(true, "uexpr: wrong ast node kind %d", an->kind);
}

static void apply_if_cond(Checker *c, const ASTNode *n) {
  assert(n != NULL);
  TypeId cond_typeid = TYPE_UNKNOWN_ID;
  if (n->kind == NODE_STMT) {
    FAIL_IF_TMPL(n->info.stmt.stmt_tok != TOK_IF, "if stmt cond: wrong ast node token %s", token_kind_to_string(n->info.stmt.stmt_tok));
    cond_typeid = n->info.stmt.iff.cond->type;
  } else if (n->kind == NODE_IFEXPR) {
    cond_typeid = n->info.ifexpr.cond->type;
  } else
    FAIL_IF_TMPL(true, "if cond: wrong ast node kind %d", n->kind);

  TypeId bool_typeid = env_find_typeid_by_name(c->env, "bool");
  FAIL_IF_MSG(cond_typeid != bool_typeid, "'if' condition is not a bool expression");
}

static void apply_if_post(Checker *c, const ASTNode *n) {
  ASTNode *an = (ASTNode *)n;
  assert(an != NULL);
  an->type = env_find_typeid_by_name(c->env, "void");
}

static void apply_ifexpr_post(Checker *c, const ASTNode *n) {
  ASTNode *an = (ASTNode *)n;
  if (!an) return;
  ASTNode *t = an->info.ifexpr.then_arm;
  ASTNode *e = an->info.ifexpr.else_arm;
  FAIL_IF_MSG(t == NULL || e == NULL, "'if' expression requires both branches");
  FAIL_IF_TMPL(t->type != e->type, "'if' expression branches must provide the same type, 'then' branch is %s, 'else' branch is %s",
               env_render_type1(c->env, t->type), env_render_type2(c->env, e->type));
  an->type = t->type;
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
  if (an) an->type = env_find_typeid_by_name(c->env, "void");
}

static void apply_echo(Checker *c) {
  (void)c;
}

Checker *checker_new(void) {
  Checker *c = calloc(1, sizeof(Checker));
  assert(c);
  memset(&c->header, 0, sizeof(c->header));
  c->env = env_new();
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
  env_free(c->env);
  if (c->globals) TypeScope_free(c->globals);
  for (size_t i = 0; i < c->scope_len; i++) {
    if (c->scopes[i]) TypeScope_free(c->scopes[i]);
  }
  free(c->scopes);
  free(c);
}

Environment *checker_get_env(Checker *c) {
  return c ? c->env : NULL;
}


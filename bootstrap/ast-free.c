#include "ast-free.h"

#include <string.h>
#include <assert.h>

static void free_the_block(ASTFree*, ASTNode* n) {
  if (n != NULL) free(n->info.block.stmts);   // entries are freed by apply_leave
}

static void free_the_define(ASTFree*, ASTNode* n) {
  assert(n != NULL && n->kind == NODE_STMT);
  assert(n->info.stmt.define.type_expr == NULL); // typed defines do not parse yet
  ASTNode *id = n->info.stmt.define.id;
  if (id != NULL) {
    assert(id->kind == NODE_ID);
    free(id->info.id.value);
    free(id);
  }
}

static void free_the_identifier(ASTFree*, ASTNode* n) {
  assert(n->kind == NODE_ID);
  if (n != NULL) free(n->info.id.value);
}

static void free_the_value(ASTFree*, ASTNode* n) {
  assert(n->kind == NODE_VALUE);
  if (n != NULL) free((void*)n->info.val.value);
}

static void free_the_node(ASTFree*, ASTNode* n) {
  if (n != NULL) free(n);
}

static void do_nothing_node(ASTFree*, const ASTNode*) {}
static void do_nothing(ASTFree*) {}
static bool return_true(ASTFree*) { return true; }

ASTFree *astfree_new() {
  ASTFree *fr = malloc(sizeof(ASTFree));
  assert(fr != NULL);

  memset(&fr->header, 0, sizeof(fr->header));

  fr->header.ops.apply_assign = (Applyer)do_nothing;
  fr->header.ops.apply_define = (NodeApplyer)free_the_define;
  fr->header.ops.apply_echo = (Applyer)do_nothing;
  fr->header.ops.apply_expr = (NodeApplyer)do_nothing_node;
  fr->header.ops.apply_uexpr = (NodeApplyer)do_nothing_node;
  fr->header.ops.apply_identifier = (NodeApplyer)free_the_identifier;
  fr->header.ops.apply_identifier_ref = (NodeApplyer)free_the_identifier;
  fr->header.ops.value_of = (NodeApplyer)free_the_value;

  fr->header.ops.apply_if_cond = (NodeApplyer)do_nothing_node;
  fr->header.ops.apply_if_then_descision = (CondApplyer)return_true;
  fr->header.ops.apply_if_else_descision = (CondApplyer)return_true;
  fr->header.ops.apply_if_post = (NodeApplyer)do_nothing_node;
  fr->header.ops.apply_ifexpr_post = (NodeApplyer)do_nothing_node;

  fr->header.ops.scope_push_new = (Applyer)do_nothing;
  fr->header.ops.scope_pop = (NodeApplyer)free_the_block;

  fr->header.ops.apply_enter = (NodeApplyer)do_nothing_node;
  fr->header.ops.apply_leave = (NodeApplyer)free_the_node;

  memset(fr->header.error, '\0', sizeof(fr->header.error));

  return fr;
}

void astfree_free(ASTFree *fr) {
  if (fr != NULL) free(fr);
}

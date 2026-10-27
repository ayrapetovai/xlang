#ifndef CHECKER_H
#define CHECKER_H

#include "walker.h"
#include "typetab.h"
#include "hmap.h"

DEFINE_HMAP(TypeId, TypeScope)

typedef struct {
  WalkerContextHeader header;
  Environment *env;
  struct TypeScope *globals;      // name -> TypeId
  struct TypeScope **scopes;      // stack of scopes
  size_t scope_cap;
  size_t scope_len;
  char current_namespace[512];
} Checker;

Checker *checker_new(void);
Environment *checker_get_env(Checker *c);
void checker_free(Checker *c);

#endif

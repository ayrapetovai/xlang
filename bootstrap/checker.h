#ifndef CHECKER_H
#define CHECKER_H

#include "walker.h"
#include "typetab.h"
#include "map.h"

typedef struct {
  WalkerContextHeader header;
  TypeTable *type_table;
  FuncTable *func_table;
  struct HashMap *globals;      // name -> TypeId
  struct HashMap **scopes;      // stack of scopes
  size_t scope_cap;
  size_t scope_len;
  char current_namespace[512];
} Checker;

Checker *checker_new(void);
void checker_free(Checker *c);
TypeTable *checker_get_type_table(Checker *c);
FuncTable *checker_get_func_table(Checker *c);

#endif

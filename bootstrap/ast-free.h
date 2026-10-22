#ifndef AST_FREE_H
#define AST_FREE_H

#include "walker.h"

typedef struct {
  WalkerContextHeader header;
} ASTFree;

ASTFree *astfree_new();

void astfree_free(ASTFree*);

#endif


#ifndef AST_DUMP_H
#define AST_DUMP_H

#include "walker.h"
#include <stddef.h>

typedef struct PrintNode PrintNode;

typedef struct PrintNode {
  char *text;
  size_t indent;
  PrintNode *next;
} PrintNode;

typedef struct {
  WalkerContextHeader header;
  size_t  stack_len;
  PrintNode  **stack;
  size_t  sp;
  bool    colored;
  size_t  depth;
} ASTDump;

ASTDump *astdump_new(size_t max_stack_size);

void astdump_print(ASTDump *dmp);

void astdump_free(ASTDump *dmp);

#endif


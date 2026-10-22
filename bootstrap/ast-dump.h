#ifndef AST_DUMP_H
#define AST_DUMP_H

#include "walker.h"
#include <stddef.h>

typedef struct PrintNode PrintNode;

// This is one string (wihtout indention symbols) representation.
// The next field is a next string to be printed.
// This is basically a linked list.
// During the traversing process we appending new elements to the list.
// Add Head and "tail" pointer, such that we will be able to append to the tail
// in no time.
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


#ifndef READER_H
#define READER_H

#include <stdbool.h>
#include <stdio.h>

typedef struct Reader {
  FILE *file;
  char buf[16];
  char unget_buf[4];
  size_t pos;
  size_t available;
  char error[128];
} Reader;

Reader *new_reader(char *);

bool reader_getch(struct Reader *, char *);

void reader_ungetch(struct Reader *, char);

void reader_close(struct Reader *);

#endif

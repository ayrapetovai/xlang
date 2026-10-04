#ifndef READER_H
#define READER_H

#include <stdbool.h>
#include <stdio.h>

#define READER_BUFFER_SIZE 1024
#define READER_UNGET_BUF_SIZE 4

typedef struct Reader {
  FILE *file;
  char buf[READER_BUFFER_SIZE];
  char unget_buf[READER_UNGET_BUF_SIZE];
  size_t buf_base; // file offset of buf[0]
  size_t pos;
  size_t available;
  char error[128];
} Reader;

Reader *new_reader(char *);

bool reader_getch(struct Reader *, char *);

void reader_ungetch(struct Reader *, char);

// logical file offset of the next character to be read
size_t reader_tell(const struct Reader *);

// Rewind to a logical offset. fread overwrites buf, so restoring pos/available
// alone cannot undo a refill that happened after the checkpoint was taken.
bool reader_rewind(struct Reader *, size_t);

void reader_close(struct Reader *);

#endif

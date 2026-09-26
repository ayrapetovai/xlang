#include "reader.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Reader* new_reader(char* filename) {
  struct Reader *reader = malloc(sizeof(Reader));
  if (reader == NULL) return NULL;
  FILE* f = fopen(filename, "ra");
  if (f == NULL) {
    perror(reader->error);
    return NULL;
  }

  reader->file = f;
  reader->available = 0;
  reader->pos = 0;
  memset(reader->error, 0, sizeof reader->error);
  return reader;
}

bool reader_getch(struct Reader* reader, char *c) {
  if (reader == NULL || c == NULL) return -1;

  if (reader->pos < reader->available) {
    *c = reader->buf[reader->pos++];
  } else {
    size_t read = fread(reader->buf, 1, sizeof reader->buf, reader->file);
    if (ferror(reader->file)) {
      strcpy(reader->error, strerror(errno));
      return false;
    } else if (read == 0 && feof(reader->file)) {
      return false;
    }
    reader->available = read;
    reader->pos = 1;
    *c = reader->buf[0];
  }
  return true;
}

void reader_close(struct Reader* reader) {
  if (reader == NULL || reader->file == NULL) return;
  fclose(reader->file);
  free(reader);
}

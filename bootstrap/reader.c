#include "reader.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Reader* new_reader(char* filename) {
  struct Reader *reader = malloc(sizeof(Reader));
  if (reader == NULL) return NULL;
  reader->file = NULL;
  reader->available = 0;
  reader->pos = 0;

  memset(reader->error, 0, sizeof reader->error);
  memset(reader->unget_buf, 0, sizeof reader->unget_buf);

  FILE* f = fopen(filename, "ra"); // read only as text
  if (f == NULL) {
    sprintf(reader->error, strerror(errno));
    return reader;
  }

  reader->file = f;
  return reader;
}

bool reader_getch(struct Reader* reader, char *c) {
  if (reader == NULL) return false;

  // check if the last operation was unget,
  // then ouer get must return the last ungetted char
  int i = 0;
  while (reader->unget_buf[i] != '\0' && i < (int) sizeof reader->unget_buf) i++;
  i -= 1;
  if (i >= 0) {
    *c = reader->unget_buf[i];
    reader->unget_buf[i] = '\0'; // remove ungetted char, becasue we get it again
    return true;
  }

  if (reader->pos < reader->available) { // if we have something to read - we read it
    *c = reader->buf[reader->pos++];
  } else {
    // if we have nothing to read from buffer we will read text from file to buffer
    // text fills buffer from the beginning
    size_t read = fread(reader->buf, 1, sizeof reader->buf, reader->file);
    if (ferror(reader->file)) {
      strcpy(reader->error, strerror(errno));
      return false;
    } else if (read == 0 && feof(reader->file)) {
      return false;
    }
    reader->available = read;
    reader->pos = 1; // we could make it 0, but anyway it must be ++
    *c = reader->buf[0];
  }
  return true;
}

void reader_ungetch(struct Reader* reader, char c) {
  // the \0 is an anchor of emtpy place
  if (c == '\0') return;
  size_t i = 0;
  while (reader->unget_buf[i] != 0 && i < sizeof reader->unget_buf) i++;
  if (i == sizeof reader->unget_buf) return; // unget buffer is overfload
  // write the ungetted char right after the last ungetted char
  reader->unget_buf[i] = c;
}

void reader_close(struct Reader* reader) {
  if (reader == NULL || reader->file == NULL) return;
  fclose(reader->file);
  free(reader);
}

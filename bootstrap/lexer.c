#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "tokens.h"
#include "lexer.h"
#include "reader.h"

struct Lexer* new_lexer(struct Reader* reader) {
  if (reader == NULL)
    return NULL;
  struct Lexer *lexer = malloc(sizeof(Lexer));
  if (lexer == NULL)
    return NULL;
  lexer->reader = reader;
  memset(lexer->error, 0, sizeof lexer->error);
  return lexer;
}

enum LexState next_token(struct Lexer* lexer, struct Token* tok) {
  if (lexer == NULL || tok == NULL) return LEX_PRG_ERROR;

  char c;
  bool read = reader_getch(lexer->reader, &c);
  if (!read) {
    if (lexer->reader->error[0] != 0) {
      strcpy(lexer->error, lexer->reader->error);
      return LEX_ERROR;
    }
    return LEX_EOF;
  }

  printf("0x%x ", c);
  if (c != '\n') {
    putchar(c);
  } else {
    printf("\\n");
  }
  putchar('\n');
  return LEX_OK;
}

void close_lexer(struct Lexer* lexer) {
  if (lexer == NULL) return;
  reader_close(lexer->reader);
  free(lexer);
}


#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

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
  lexer->line = 1;
  lexer->col = 1;
  memset(lexer->error, 0, sizeof lexer->error);
  return lexer;
}

bool strcmplen(const char* s, const char* t, size_t *size) {
  const char *t_start = t;
  while (*s != '\0' && *t != '\0' && *s == *t) {
    s++;
    t++;
  }
  bool are_equal = *s == '\0' && *t == '\0';
  while (*t++ != '\0');
  *size = t - t_start;
  return are_equal;
}

enum LexState next_token(struct Lexer* lexer, struct Token* tok) {
  if (lexer == NULL || tok == NULL) return LEX_PRG_ERROR;

  // bootstrap compiler cannot parse names longer than 64 chars
  char buf[64] = {0};
  size_t buf_idx = 0;

  tok->line = lexer->line;
  tok->col = lexer->col;

  while (true) {
    char c;
    bool read = reader_getch(lexer->reader, &c);
    if (!read && buf_idx == 0) {
      if (lexer->reader->error[0] != 0) {
        strcpy(lexer->error, lexer->reader->error);
        return LEX_ERROR;
      }
      return LEX_EOF;
    }
    lexer->col += 1;

    if (buf_idx >= sizeof buf) {
      sprintf(lexer->error, "too long identifier");
      return LEX_ERROR;
    }

    if (!isalnum(c)) {
      if (buf_idx > 0) {
        lexer->col -= 1;
        reader_ungetch(lexer->reader, c);
        tok->kind = TOK_ID;
        strcpy(tok->value, buf);
      } else {
        if (c == '\n') {
          lexer->line += 1;
          lexer->col = 1;
          tok->kind = TOK_NL;
        } else if (c == ' ') {
          tok->kind = TOK_SPACE;
        } else {
          tok->kind = TOK_UNDEF;
          sprintf(tok->value, "'%c'", c);
        }
      }
      return LEX_OK;
    }

    buf[buf_idx] = c;

    int found_word_idx = -1;
    bool matching_is_finished = false;
    for (size_t i = 0; i < TOKEN_KEYWORDS_COUNT; i++) {
      size_t keyword_len;
      matching_is_finished = strcmplen(buf, TOKEN_KEYWORDS[i].letters, &keyword_len);
      if (matching_is_finished) found_word_idx = i;
      if (matching_is_finished || buf_idx > keyword_len) {
        break;
      }
    }

    buf_idx += 1;

    if (matching_is_finished) {
      if (found_word_idx < 0) {
        // word is not found in keywrds, it is an identifier
        tok->kind = TOK_ID;
        strcpy(tok->value, buf);
      } else {
        tok->kind = TOKEN_KEYWORDS[found_word_idx].kind;
      }
      break;
    }
  }
  return LEX_OK;
}

void close_lexer(struct Lexer* lexer) {
  if (lexer == NULL) return;
  reader_close(lexer->reader);
  free(lexer);
}

void print_char(char c) {
  printf("0x%x ", c);
  if (c != '\n') {
    putchar(c);
  } else {
    printf("\\n");
  }
  putchar('\n');
}


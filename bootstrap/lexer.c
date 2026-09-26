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
  memset(lexer->error, 0, sizeof lexer->error);
  return lexer;
}

enum LexState next_token(struct Lexer* lexer, struct Token* tok) {
  if (lexer == NULL || tok == NULL) return LEX_PRG_ERROR;

  // bootstrap compiler cannot parse names longer than 64 chars
  char buf[64] = {0};
  size_t buf_idx = 0;

  while (true) {
    char c;
    bool read = reader_getch(lexer->reader, &c);
    if (!read) {
      if (lexer->reader->error[0] != 0) {
        strcpy(lexer->error, lexer->reader->error);
        return LEX_ERROR;
      }
      return LEX_EOF;
    }

    if (buf_idx >= sizeof buf) {
      sprintf(lexer->error, "too long identifier");
      return LEX_ERROR;
    }

    if (!isalnum(c)) {
      if (buf_idx > 0) {
        tok->kind = TOK_ID;
        strcpy(tok->value, buf);
      } else {
        tok->kind = TOK_SEP;
        if (c == '\n') {
          strcpy(tok->value, "'\\n'");
        } else if (c == ' ') {
          strcpy(tok->value, "' '");
        } else {
          sprintf(tok->value, "'%c'", c);
        }
      }
      return LEX_OK;
    }

    buf[buf_idx] = c;

    int found_word_idx = -1;
    bool matching_is_finished = false;
    for (size_t i = 0; i < TOKEN_KEYWORDS_COUNT; i++) {
      char keyword_char = TOKEN_KEYWORDS[i].letters[buf_idx];
      char keyword_next_char = TOKEN_KEYWORDS[i].letters[buf_idx + 1];
      bool keyword_is_over = (keyword_next_char == '\0');
      bool letters_are_equal = (c == keyword_char);
      if (keyword_is_over) {
        if (letters_are_equal) { // word is found in keywords
          matching_is_finished = true;
          found_word_idx = i;
        }
        break;
      }
      if (letters_are_equal) {
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


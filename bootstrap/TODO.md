- lex floats: create token TOK_FLOAT_L.
- add TOK_EOF, let the lexer return it.
- Remove pool from parser.
- Parser's error messages are bad.
- Lexer searches for reserved word the inefficient way.
- parser: skip multi line comments.


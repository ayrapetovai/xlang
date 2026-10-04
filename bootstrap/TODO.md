- add TOK_EOF, let the lexer return it.
- Remove pool from parser.
- Lexer searches for reserved word the inefficient way.
- parser: reader rewinding must not reread file.
- lexer: create TOK_BOOL_L for boolean literals, `true` and `false` must retain reserved,
  and ASTNode must not carry string representation of the TOK_BOOL_L?

type token =
  | INPUT
  | PRINT
  | ASSIGN
  | PLUS
  | MINUS
  | MULTIPLY
  | DIVIDE
  | ABS
  | IF
  | THEN
  | ELSE
  | FOR
  | WHILE
  | INV_BOOL
  | INV_SCALA
  | INV_VEC
  | INV_MAT
  | DOTPROD_VEC
  | MAG_VEC
  | MAG_SCALA
  | ANGLE_VEC
  | ISZERO_VEC
  | ISZERO_SCALA
  | TRANSPOSE_MAT
  | MULT_MAT
  | DET_MAT
  | INVERSE_MAT
  | TYPE_BOOL
  | TYPE_INT
  | TYPE_FLOAT
  | INT_TYPE_VECTOR
  | FLOAT_TYPE_VECTOR
  | INT_TYPE_MATRIX
  | FLOAT_TYPE_MATRIX
  | EQ
  | NEQ
  | GT
  | LT
  | GE
  | LE
  | AND
  | OR
  | NOT
  | MOD
  | ADD_VECTOR
  | SCALE_VECTOR
  | DOT_VECTOR
  | ANGLE
  | MAGNITUDE
  | DIMENSION
  | ADD_MATRIX
  | SCALE_MATRIX
  | MULTIPLY_MATRIX
  | TRANSPOSE
  | DETERMINANT
  | LPAREN
  | RPAREN
  | LBRACKET
  | RBRACKET
  | LBRACE
  | RBRACE
  | COMMA
  | SEMICOLON
  | STRING_LITERAL of (string)
  | IDENTIFIER of (string)
  | FLOAT_VAL of (float)
  | INT_VAL of (int)
  | TRUE_VAL
  | FALSE_VAL
  | END_OF_FILE

val program :
  (Lexing.lexbuf  -> token) -> Lexing.lexbuf -> Ast.program

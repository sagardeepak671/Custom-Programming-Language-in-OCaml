(* lexer.mll *)
{open Parser  
exception Lexing_error of string}
rule token = parse
  | [' ' '\t' '\r' '\n']+       { token lexbuf }   
  | "/*"                       { comment lexbuf }
  | "//"                       { single_comment lexbuf }
  (* Keywords and Operators *)
  | "Input"                    { INPUT }
  | "Print"                    { PRINT }
  | ":="                       { ASSIGN }
  | "if"                       { IF }
  | "then"                     { THEN }
  | "else"                     { ELSE }
  | "for"                      { FOR }
  | "while"                    { WHILE }
  | "abs"                      { ABS }
  | "+"                        { PLUS }
  | "intvector"                { INT_TYPE_VECTOR }  
  | "floatvector"              { FLOAT_TYPE_VECTOR }
  | "intmatrix"                { INT_TYPE_MATRIX }
  | "floatmatrix"              { FLOAT_TYPE_MATRIX }
  | ".txt"                     { TXT_FILE }
  | "-"                        { MINUS }
  | "*"                        { MULTIPLY }
  | "/"                        { DIVIDE }
  | "("                        { LPAREN }
  | ")"                        { RPAREN }
  | "["                        { LBRACKET }
  | "]"                        { RBRACKET }
  | "{"                        { LBRACE }
  | "}"                        { RBRACE }
  | ";"                        { SEMICOLON }
  | ","                        { COMMA }
  | "=="                       { EQ }
  | "!="                       { NEQ }
  | "<="                       { LE }
  | "<"                        { LT }
  | ">="                       { GE }
  | ">"                        { GT }
  | "&&"                       { AND }
  | "||"                       { OR }
  | "!"                        { NOT }
  | "%"                        { MOD } 
  
  | "true"                     { TRUE_VAL }
  | "false"                    { FALSE_VAL }
  | "inv_bool"                 { INV_BOOL }
  | "inv_scala"                { INV_SCALA }
  | "inv_vec"                  { INV_VEC }
  | "inv_mat"                  { INV_MAT }
  | "dotprod_vec"              { DOTPROD_VEC }
  | "mag_vec"                  { MAG_VEC }
  | "mag_scala"                { MAG_SCALA }
  | "angle_vec"                { ANGLE_VEC }
  | "is_zero"                  { ISZERO } 
  | "mult_mat"                 { MULT_MAT } 
  | "inverse_mat"              { INVERSE_MAT } 
  | "transpose"                { TRANSPOSE }
  | "determinant"              { DETERMINANT }
  | "add_vector"               { ADD_VECTOR }
  | "scale_vector"             { SCALE_VECTOR }
  | "add_matrix"               { ADD_MATRIX }
  | "scale_matrix"             { SCALE_MATRIX }
  | "multiply_matrix"          { MULTIPLY_MATRIX }
  (* Type tokens *)
  | "bool"                     { TYPE_BOOL }
  | "int"                      { TYPE_INT }
  | "float"                    { TYPE_FLOAT }
  (* Numeric literals: floats and integers *)
  | ['0'-'9']+ '.' ['0'-'9']* (['e''E'] (['+' '-']? ['0'-'9']+))? as fnum
      { FLOAT_VAL (float_of_string fnum) }
  | ['0'-'9']+ as inum
      { INT_VAL (int_of_string inum) }
  (* String literals *)
  | '"' ([^ '"'])* '"' as s
      { let len = String.length s in STRING_LITERAL (String.sub s 1 (len - 2)) }
  (* Identifiers *)
  | ['a'-'z' 'A'-'Z' '_' '\''] 
      (['a'-'z' 'A'-'Z' '0'-'9' '_' '\''])* as id
      { IDENTIFIER id }
  | eof                        { END_OF_FILE }
  | _                          { raise (Lexing_error ("Unexpected character: " ^ Lexing.lexeme lexbuf)) }
and comment = parse
  | "*/" { token lexbuf }
  | _    { comment lexbuf }
and single_comment = parse
  | '\n' { token lexbuf }
  | _    { single_comment lexbuf }
// (* parser.mly *)
%{
open Ast 
%} 
%token INPUT TXT_FILE PRINT ASSIGN PLUS MINUS MULTIPLY DIVIDE ABS 
%token IF THEN ELSE FOR WHILE 
%token INV_BOOL INV_SCALA INV_VEC INV_MAT
%token DOTPROD_VEC MAG_VEC MAG_SCALA ANGLE_VEC ISZERO
%token  MULT_MAT  INVERSE_MAT
%token TYPE_BOOL TYPE_INT TYPE_FLOAT INT_TYPE_VECTOR FLOAT_TYPE_VECTOR INT_TYPE_MATRIX FLOAT_TYPE_MATRIX
%token EQ NEQ GT LT GE LE
%token AND OR NOT
%token MOD 
%token ADD_VECTOR SCALE_VECTOR 
%token ADD_MATRIX SCALE_MATRIX MULTIPLY_MATRIX TRANSPOSE DETERMINANT
%token LPAREN RPAREN LBRACKET RBRACKET LBRACE RBRACE COMMA SEMICOLON
%token <string> STRING_LITERAL
%token <string> IDENTIFIER
%token <float> FLOAT_VAL
%token <int> INT_VAL
%token TRUE_VAL FALSE_VAL
%token END_OF_FILE
 
%nonassoc LOWER_THAN_ELSE
%nonassoc ELSE 
  
%left OR
%left AND
%left EQ NEQ
%left LT GT LE GE
%left MOD  
%left ASSIGN
%left PLUS MINUS ADD_VECTOR ADD_MATRIX
%left MULTIPLY DIVIDE SCALE_VECTOR SCALE_MATRIX 
%left MULTIPLY_MATRIX
%nonassoc UMINUS    
%nonassoc NOT ABS
%nonassoc DOTPROD_VEC MAG_VEC MAG_SCALA ANGLE_VEC ISZERO
%nonassoc  MULT_MAT  INVERSE_MAT
%nonassoc INV_BOOL INV_SCALA INV_VEC INV_MAT
%nonassoc TRANSPOSE DETERMINANT 

%start program
%type <Ast.program> program

%%

program:
  stmt_list END_OF_FILE { Seq $1 }

stmt_list:
    stmt SEMICOLON stmt_list { $1 :: $3 }
  | stmt SEMICOLON            { [$1] }
  | stmt                      { [$1] }

stmt:
  | IDENTIFIER ASSIGN expr                { Assign ($1,$3,None)}
  | var_type IDENTIFIER ASSIGN expr       { Assign ($2, $4, Some $1) }
  | print_stmt                            { $1 } 
  | IF LPAREN expr RPAREN THEN stmt ELSE stmt { If ( $3, $6, $8) }
  | IF LPAREN expr RPAREN THEN stmt %prec LOWER_THAN_ELSE { If ( $3, $6, Seq []) }
  | FOR LPAREN IDENTIFIER ASSIGN expr COMMA expr COMMA expr RPAREN stmt { For ($3,  $5,  $7, $9, $11) }
  | WHILE LPAREN expr RPAREN stmt         { While ( $3, $5) }
  | LBRACE stmt_list RBRACE               { Seq $2 }
  | expr                                  { ExprStmt ( $1) }
  ;
var_type:
  | TYPE_BOOL { Bool }
  | TYPE_INT { Scalar_Int }
  | TYPE_FLOAT { Scalar_Float }
  | INT_TYPE_VECTOR INT_VAL { Vector_Int($2) }
  | FLOAT_TYPE_VECTOR INT_VAL { Vector_Float($2) }
  | INT_TYPE_MATRIX INT_VAL COMMA INT_VAL { Matrix_Int($2,$4) }
  | FLOAT_TYPE_MATRIX INT_VAL COMMA INT_VAL { Matrix_Float($2,$4)}
print_stmt:
    PRINT LPAREN expr RPAREN      { PrintStmt ($3) } 
expr:
  | matrix_expr      { $1 }
  | vector_expr      { $1 } 
  | simple_expr      { $1 }
  | arith_expr       { $1 }
  | comp_expr        { $1 }
  | logic_expr       { $1 }
  | func_expr        { $1 }
  | input_expr       { $1 } 
  | access_vector_expr { $1 }
  | access_matrix_expr { $1 }
  ;
input_expr:
  INPUT LPAREN IDENTIFIER TXT_FILE RPAREN { InputExpr (Some $3) }  
  | INPUT LPAREN RPAREN                     { InputExpr None }
vector_expr:
LPAREN INT_VAL LBRACKET int_vector_elements RBRACKET RPAREN { VectorDecl ($2, ConstIntV $4) }
| LPAREN INT_VAL LBRACKET float_vector_elements RBRACKET RPAREN { VectorDecl ($2, ConstV $4) }
; 
matrix_expr:
LPAREN INT_VAL COMMA INT_VAL LBRACKET int_matrix_content RBRACKET RPAREN { MatrixDecl ($2, $4, ConstIntM $6) }
| LPAREN INT_VAL COMMA INT_VAL LBRACKET float_matrix_content RBRACKET RPAREN { MatrixDecl ($2, $4, ConstM $6) }
;
int_vector_literal:
  LBRACKET int_vector_elements RBRACKET { $2 } 
  ;
float_vector_literal:
  LBRACKET float_vector_elements RBRACKET { $2 } 
  ;
int_vector_elements:
  | INT_VAL COMMA int_vector_elements { $1 :: $3 }
  | MINUS INT_VAL COMMA int_vector_elements { (-$2) :: $4 }
  | INT_VAL                          { [$1] }
  | MINUS INT_VAL                     { [-$2] }
  ;
float_vector_elements:
  | MINUS FLOAT_VAL COMMA float_vector_elements { (-1.0 *. $2) :: $4 }
  | FLOAT_VAL COMMA float_vector_elements    { $1 :: $3 } 
  // | MINUS INT_VAL COMMA float_vector_elements { (-1.0 *. float_of_int $2) :: $4 }
  | MINUS FLOAT_VAL                        { [(-1.0 *. $2)] }
  | FLOAT_VAL                               { [$1] }
  // | INT_VAL                                 { [float_of_int $1] }
  // | MINUS INT_VAL                        { [(-1.0 *. float_of_int $2)] }
  ;
int_matrix_literal:
    LBRACKET int_matrix_content RBRACKET { $2 }
  ;
float_matrix_literal:
    LBRACKET float_matrix_content RBRACKET { $2 }
  ;
int_matrix_content:
    LBRACKET int_vector_elements RBRACKET { [$2] }
  | LBRACKET int_vector_elements RBRACKET COMMA int_matrix_content { $2 :: $5 }
  ;
float_matrix_content:
    LBRACKET float_vector_elements RBRACKET { [$2] }
  | LBRACKET float_vector_elements RBRACKET COMMA float_matrix_content { $2 :: $5 }
  ;
simple_expr:
    TRUE_VAL                   { T }
  | FALSE_VAL                  { F }
  | FLOAT_VAL                  { ConstS $1 }
  | IDENTIFIER                 { Var $1 } 
  | INT_VAL                    { ConstI ($1) }
  | int_vector_literal         { ConstIntV $1 }   
  | float_vector_literal       { ConstV $1 }      
  | int_matrix_literal         { ConstIntM $1 }   
  | float_matrix_literal       { ConstM $1 }      
  | LPAREN expr RPAREN         { $2 }
  ;


arith_expr:
    expr PLUS expr           { Add ($1, $3) }
  | expr MINUS expr          { Add ($1, Inv $3) }
  | expr MULTIPLY expr       { ScalProd ($1, $3) }
  | expr DIVIDE expr         { DotProd ($1,$3) }
  | MINUS expr %prec UMINUS  { Inv $2 }
  | expr MOD expr          { Mod ($1, $3) } 
  ;
comp_expr:
    expr EQ expr             { Eq ($1, $3) }
  | expr NEQ expr            { Neq ($1, $3) }
  | expr LT expr             { Lt ($1, $3) }
  | expr GT expr             { Gt ($1, $3) }
  | expr LE expr             { Le ($1, $3) }
  | expr GE expr             { Ge ($1, $3) }
  ;
logic_expr:
    expr AND expr            { ScalProd ($1, $3) }
  | expr OR expr             { Add ($1, $3) }
  | NOT expr                 { Inv $2 }
  ;
func_expr:
    ABS LPAREN expr RPAREN                       { Mag $3 }
  | INV_BOOL LPAREN expr RPAREN                  { Inv $3 }
  | INV_SCALA LPAREN expr RPAREN                 { Inv $3 }
  | INV_VEC LPAREN expr RPAREN                   { Inv $3 }
  | INV_MAT LPAREN expr RPAREN                   { Inv $3 }
  | DOTPROD_VEC LPAREN expr COMMA expr RPAREN    { DotProd ($3, $5) }
  | MAG_VEC LPAREN expr RPAREN                   { Mag $3 }
  | MAG_SCALA LPAREN expr RPAREN                 { Mag $3 }
  | ANGLE_VEC LPAREN expr COMMA expr RPAREN      { Angle ($3, $5) }
  | ISZERO LPAREN expr RPAREN                    { IsZero $3 } 
  | TRANSPOSE LPAREN expr RPAREN                 { Transpose $3 }
  | MULT_MAT LPAREN expr COMMA expr RPAREN       { Mult ($3, $5) }
  | DETERMINANT LPAREN expr RPAREN               { Det $3 }
  | INVERSE_MAT LPAREN expr RPAREN               { Inverse $3 }  
  | expr ADD_VECTOR expr                         { Add ($1, $3) }
  | expr ADD_MATRIX expr                         { Add ($1, $3) }
  | expr SCALE_VECTOR expr                       { ScalProd ($1, $3) }
  | expr SCALE_MATRIX expr                       { ScalProd ($1, $3) }
  | expr MULTIPLY_MATRIX expr                    { Mult ($1, $3) }
  ;
access_vector_expr:
    IDENTIFIER LBRACKET expr RBRACKET { AccessV ($1, $3) }
access_matrix_expr:
    IDENTIFIER LBRACKET expr COMMA expr RBRACKET { AccessM ($1, $3, $5) } 
  ;
%%

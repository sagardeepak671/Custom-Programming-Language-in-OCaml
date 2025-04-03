(* ast.ml *)
exception TypeError of string

type types = 
| Bool 
| Scalar_Int 
| Scalar_Float 
| Vector_Int of int 
| Vector_Float of int 
| Matrix_Int of int * int 
| Matrix_Float of int * int

(**Just_for_Printing*)
let string_of_type t = 
  match t with
  | Bool -> "Bool"
  | Scalar_Int -> "Scalar_Int"
  | Scalar_Float -> "Scalar_Float"
  | Vector_Int n -> "Vector_Int(" ^ string_of_int n ^ ")"
  | Vector_Float n -> "Vector_Float(" ^ string_of_int n ^ ")"
  | Matrix_Int (r, c) -> "Matrix_Int(" ^ string_of_int r ^ ", " ^ string_of_int c ^ ")"
  | Matrix_Float (r, c) -> "Matrix_Float(" ^ string_of_int r ^ ", " ^ string_of_int c ^ ")"
  

type expr =  
  | T                         (* Boolean true *)
  | F                         (* Boolean false *)
  | ConstS of float           (* Float scalar constant *)
  | ConstI of int             (* Integer scalar constant *)
  | ConstV of float list      (* Float vector constant *)
  | ConstIntV of int list     (* Integer vector constant *)
  | ConstM of float list list (* Float matrix constant *)
  | ConstIntM of int list list(* Integer matrix constant *)
  | Add of expr * expr        (* Addition *)
  | Mod of expr * expr        (* Modulus *)
  | ScalProd of expr * expr   (* Scalar product *)
  | DotProd of expr * expr    (* Dot product *)
  | Mag of expr               (* Magnitude *)
  | Angle of expr * expr      (* Angle *)
  | IsZero of expr            (* IsZero check *) 
  | Var of string             (* Variable *)
  | Inv of expr             (* Negation/inversion *)
  | Transpose of expr         (* Transpose *)
  | Mult of expr * expr       (* Matrix multiplication *)
  | VectorDecl of  int * expr (* Vector statement *)
  | MatrixDecl of  int * int * expr (* Matrix statement *)
  | Det of expr               (* Determinant *)
  | InputExpr of string option (* Input expression *)
  | Inverse of expr           (* Matrix inverse *)
  | Eq of expr * expr         (* Equality comparison *)
  | Neq of expr * expr        (* Not equal comparison *)
  | Lt of expr * expr         (* Less than comparison *)
  | Gt of expr * expr         (* Greater than comparison *)
  | Le of expr * expr         (* Less than or equal comparison *)
  | Ge of expr * expr         (* Greater than or equal comparison *)
  | AccessV of string * expr  (* Accessing a vector element *)
  | AccessM of string * expr *expr (* Accessing a matrix element *)

type stmt = 
    Assign of string * expr * (types option)         (* x := expr *)
  | Seq of stmt list                 (* sequence of statements *)
  | If of expr * stmt * stmt         (* if (cond) then stmt else stmt *)
  | While of expr * stmt             (* while (cond) stmt *)
  | For of string * expr * expr * expr * stmt(* for (var from expr to expr) stmt *)
  | PrintStmt of expr                (* Print statement *) 
  | ExprStmt of expr                 (* Expression statement *)

type program = stmt
(* printing the AST*)
let rec string_of_expr e =
  match e with
  | T -> "T"
  | F -> "F"
  | ConstS f -> "ConstS(" ^ string_of_float f ^ ")"
  | ConstI i -> "ConstI(" ^ string_of_int i ^ ")"
  | ConstV v -> "ConstV([" ^ (String.concat "; " (List.map string_of_float v)) ^ "])"
  | ConstIntV v -> "ConstIntV([" ^ (String.concat "; " (List.map string_of_int v)) ^ "])"
  | ConstM m -> "ConstM([" ^ (String.concat "; " (List.map (fun v -> "[" ^ (String.concat "; " (List.map string_of_float v)) ^ "]") m)) ^ "])"
  | ConstIntM m -> "ConstIntM([" ^ (String.concat "; " (List.map (fun v -> "[" ^ (String.concat "; " (List.map string_of_int v)) ^ "]") m)) ^ "])"
  | Add(e1,e2) -> "Add(" ^ string_of_expr e1 ^ ", " ^ string_of_expr e2 ^ ")"
  | Inv e -> "Inv(" ^ string_of_expr e ^ ")"
  | Mod(e1,e2) -> "Mod(" ^ string_of_expr e1 ^ ", " ^ string_of_expr e2 ^ ")"
  | ScalProd(e1,e2) -> "ScalProd(" ^ string_of_expr e1 ^ ", " ^ string_of_expr e2 ^ ")"
  | DotProd(e1,e2) -> "DotProd(" ^ string_of_expr e1 ^ ", " ^ string_of_expr e2 ^ ")"
  | Mag e -> "Mag(" ^ string_of_expr e ^ ")"
  | Angle(e1,e2) -> "Angle(" ^ string_of_expr e1 ^ ", " ^ string_of_expr e2 ^ ")"
  | IsZero e -> "IsZero(" ^ string_of_expr e ^ ")"
  | Var s -> "Var(" ^ s ^ ")"
  | Transpose e -> "Transpose(" ^ string_of_expr e ^ ")"
  | Mult(e1,e2) -> "Mult(" ^ string_of_expr e1 ^ ", " ^ string_of_expr e2 ^ ")"
  | Det e -> "Det(" ^ string_of_expr e ^ ")"
  | VectorDecl(size, e) -> "VectorDecl(" ^ string_of_int size ^ ", " ^ string_of_expr e ^ ")"
  | MatrixDecl(rows, cols, e) -> "MatrixDecl(" ^ string_of_int rows ^ ", " ^ string_of_int cols ^ ", " ^ string_of_expr e ^ ")"
  | Inverse e -> "Inverse(" ^ string_of_expr e ^ ")"
  | InputExpr(Some s_val) -> "InputExpr(" ^ s_val ^ ")"
  | InputExpr None -> "InputExpr()"
  | Eq(e1,e2) -> "Eq(" ^ string_of_expr e1 ^ ", " ^ string_of_expr e2 ^ ")"
  | Neq(e1,e2) -> "Neq(" ^ string_of_expr e1 ^ ", " ^ string_of_expr e2 ^ ")"
  | Lt(e1,e2) -> "Lt(" ^ string_of_expr e1 ^ ", " ^ string_of_expr e2 ^ ")"
  | Gt(e1,e2) -> "Gt(" ^ string_of_expr e1 ^ ", " ^ string_of_expr e2 ^ ")"
  | Le(e1,e2) -> "Le(" ^ string_of_expr e1 ^ ", " ^ string_of_expr e2 ^ ")"
  | Ge(e1,e2) -> "Ge(" ^ string_of_expr e1 ^ ", " ^ string_of_expr e2 ^ ")"
  | AccessV(s, e) -> "AccessV(" ^ s ^ ", " ^ string_of_expr e ^ ")"
  | AccessM(s, e1, e2) -> "AccessM(" ^ s ^ ", " ^ string_of_expr e1 ^ ", " ^ string_of_expr e2 ^ ")"

let rec string_of_stmt ?(indent=0) s =
  let spaces = String.make (indent * 2) ' ' in
  match s with
  | Assign(v, e, t) ->spaces ^ "Assign(" ^ v ^ ", " ^ string_of_expr e ^ ", " ^
      (match t with Some t -> string_of_type t | None -> "None") ^ ")"
  (* | Assign(v, e, None) ->spaces ^ "Assign(" ^ v ^ ", " ^ string_of_expr e ^ ", None)" *)
  | Seq stmts ->spaces ^ "Seq([\n" ^
      (String.concat ";\n" (List.map (string_of_stmt ~indent:(indent+1)) stmts)) ^
      "\n" ^ spaces ^ "])"
  | If(e, s1, s2) ->spaces ^ "If(" ^ string_of_expr e ^ ",\n" ^
      string_of_stmt ~indent:(indent+1) s1 ^ ",\n" ^
      string_of_stmt ~indent:(indent+1) s2 ^ "\n" ^
      spaces ^ ")"
  | While(e, s) ->spaces ^ "While(" ^ string_of_expr e ^ ",\n" ^
      string_of_stmt ~indent:(indent+1) s ^ "\n" ^
      spaces ^ ")"
  | For(v, e1, e2, e3, s) ->spaces ^ "For(" ^ v ^ ", " ^
      string_of_expr e1 ^ ", " ^ string_of_expr e2 ^ ", " ^
      string_of_expr e3 ^ ",\n" ^
      string_of_stmt ~indent:(indent+1) s ^ "\n" ^
      spaces ^ ")"
  | PrintStmt e ->spaces ^ "PrintStmt(" ^ string_of_expr e ^ ")"
  | ExprStmt e ->spaces ^ "ExprStmt(" ^ string_of_expr e ^ ")"

let string_of_ast p = string_of_stmt p
let print_AST ast =
  Printf.printf "<--Parsed AST-->✅🥳\n%s\n" (string_of_ast ast)
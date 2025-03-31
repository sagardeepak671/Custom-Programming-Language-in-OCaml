(* type_checker.ml *)
open Ast

module TypeChecker = struct
  type environment = {
    symbol_table : (string, types) Hashtbl.t;
    parent : environment option;
  }

  exception Type_error of string
  exception Undefined_variable of string
  exception Redeclaration_error of string

  let string_of_type = function
    | Bool -> "bool"
    | Scalar_Int -> "int"
    | Scalar_Float -> "float"
    | Vector_Int d -> "vector<int>[" ^ string_of_int d ^ "]"
    | Vector_Float d -> "vector<float>[" ^ string_of_int d ^ "]"
    | Matrix_Int (r,c) -> "matrix<int>[" ^ string_of_int r ^ "," ^ string_of_int c ^ "]"
    | Matrix_Float (r,c) -> "matrix<float>[" ^ string_of_int r ^ "," ^ string_of_int c ^ "]"
  
    let create_env parent = 
    { symbol_table = Hashtbl.create 100; parent }

    let rec lookup_var env var =
      if Hashtbl.mem env.symbol_table var then
        Hashtbl.find env.symbol_table var
      else match env.parent with
      | Some parent_env -> lookup_var parent_env var
      | None -> raise (Undefined_variable ("Undefined variable: " ^ var))

    let rec check_get_var_type env var =
      match Hashtbl.find_opt env.symbol_table var with
      | Some typ -> Some typ
      | None -> (
          match env.parent with
          | Some parent_env -> check_get_var_type parent_env var
          | None -> None
        )
    
    let add_var env var typ =
      match check_get_var_type env var with
      | Some existing_type when existing_type <> typ ->
          raise (Redeclaration_error (
            Printf.sprintf "Variable '%s' redeclared with type %s (original: %s)"
              var 
              (string_of_type typ) (string_of_type existing_type)
          ))
      | _ -> Hashtbl.add env.symbol_table var typ

  let get_vector_dim = function
    | Vector_Int d | Vector_Float d -> d
    | _ -> raise (Type_error "Not a vector type")

  let get_matrix_dims = function
    | Matrix_Int (r,c) | Matrix_Float (r,c) -> (r,c)
    | _ -> raise (Type_error "Not a matrix type")

  let rec type_of_expr env = function
    | T | F -> Bool
    | ConstS _ -> Scalar_Float
    | ConstI _ -> Scalar_Int
    | ConstIntV v -> Vector_Int (List.length v)
    | ConstV v -> Vector_Float (List.length v)
    | ConstIntM m ->
        let rows = List.length m in
        let cols = match m with h::_ -> List.length h | [] -> 0 in
        Matrix_Int (rows, cols)
    | ConstM m ->
        let rows = List.length m in
        let cols = match m with h::_ -> List.length h | [] -> 0 in
        Matrix_Float (rows, cols)
    | Var var -> lookup_var env var
    | Add(e1, e2) ->
        let t1 = type_of_expr env e1 in
        let t2 = type_of_expr env e2 in
        if t1 = t2 then t1
        else raise (Type_error ("Cannot add " ^ string_of_type t1 ^ " and " ^ string_of_type t2^"😭"))
    | Mod(e1,e2)->
        let t1 = type_of_expr env e1 in
        let t2 = type_of_expr env e2 in
        if t1 = Scalar_Int && t2 = Scalar_Int then Scalar_Int
        else raise (Type_error ("Cannot mod " ^ string_of_type t1 ^ " and " ^ string_of_type t2^"😭"))
    | ScalProd(e1, e2) ->
        let t1 = type_of_expr env e1 in
        let t2 = type_of_expr env e2 in
        (match (t1, t2) with
        |(Scalar_Int , Scalar_Int ) -> Scalar_Int
        |(Scalar_Float , Scalar_Float ) -> Scalar_Float
        | (Bool, Bool) -> Bool
         | (Scalar_Int, Vector_Int d) | (Vector_Int d, Scalar_Int) -> Vector_Int d
         | (Scalar_Float, Vector_Float d) | (Vector_Float d, Scalar_Float) -> Vector_Float d
         | (Scalar_Int, Matrix_Int (r,c)) | (Matrix_Int (r,c), Scalar_Int) -> Matrix_Int (r,c)
         | (Scalar_Float, Matrix_Float (r,c)) | (Matrix_Float (r,c), Scalar_Float) -> Matrix_Float (r,c)
         | _ -> raise (Type_error ("Invalid scalar multiplication: incompatible types " ^ string_of_type t1 ^ " and " ^ string_of_type t2 ^ "😭")) )
    | DotProd(e1, e2) ->
        let t1 = type_of_expr env e1 in
        let t2 = type_of_expr env e2 in
        (match (t1, t2) with
         | (Vector_Int d1, Vector_Int d2) when d1 = d2 -> Scalar_Int
         | (Vector_Float d1, Vector_Float d2) when d1 = d2 -> Scalar_Float
         | _ -> raise (Type_error ("Invalid dot product: incompatible types " ^ string_of_type t1 ^ " and " ^ string_of_type t2 ^ "😭")) )
    | Mag e ->
        let t1 = type_of_expr env e in
        (match t1 with
         | Vector_Int _ | Vector_Float _ |Scalar_Float-> Scalar_Float
         | Scalar_Int -> Scalar_Int 
         | Bool -> Bool
         | _ -> raise (Type_error ("Invalid magnitude: incompatible type " ^ string_of_type t1 ^ "😭")))
    | IsZero e ->
        let _ = type_of_expr env e in
        Bool
    | Angle (e1, e2) ->
      let t1 = type_of_expr env e1 in
      let t2 = type_of_expr env e2 in
      (match (t1, t2) with
      | (Vector_Int d1, Vector_Int d2) when d1 = d2 -> Scalar_Float
      | (Vector_Float d1, Vector_Float d2) when d1 = d2 -> Scalar_Float
      | _ -> raise (Type_error ("Invalid angle dimensions: cannot compute angle between " ^ string_of_type t1 ^ " and " ^ string_of_type t2 ^ "😭")))
    
    | Inv e -> type_of_expr env e 
    
    | Eq(e1, e2) | Neq(e1, e2) | Lt(e1, e2) | Gt(e1, e2) | Le(e1, e2) | Ge(e1, e2) ->
        let t1 = type_of_expr env e1 in
        let t2 = type_of_expr env e2 in
        if t1 = t2 then Bool
        else raise (Type_error ("Cannot compare " ^ string_of_type t1 ^ " and " ^ string_of_type t2 ^ "😭"))
    | Transpose e ->
      let t = type_of_expr env e in
      (match t with
      | Matrix_Int (r, c) -> Matrix_Int (c, r)
      | Matrix_Float (r, c) -> Matrix_Float (c, r)
      | _ -> raise (Type_error ("Cannot transpose type " ^ string_of_type t ^ "😭")))
    | Mult(e1, e2) ->
        let t1 = type_of_expr env e1 in
        let t2 = type_of_expr env e2 in
        (match (t1, t2) with
         | (Matrix_Int (_,c1), Matrix_Int (r2,_)) when c1 = r2 -> Matrix_Int (r2, c1)
         | (Matrix_Float (_,c1), Matrix_Float (r2,_)) when c1 = r2 -> Matrix_Float (r2, c1)
         | _ -> raise (Type_error ("Invaild Matrix Muliplication: incompatible types " ^ string_of_type t1 ^ " and " ^ string_of_type t2 ^ "😭")))
    | Det e ->
      let t = type_of_expr env e in
      (match t with
       | Matrix_Int (r,c) when r = c -> Scalar_Int
       | Matrix_Float (r,c) when r = c -> Scalar_Float
       | _ -> raise (Type_error ("Invalid Determinant: incompatible type " ^ string_of_type t ^ "😭")))
    | Inverse e ->
      let t = type_of_expr env e in
      (match t with
       | Matrix_Int (r, c) when r = c -> Matrix_Int (r, c)
       | Matrix_Float (r, c) when r = c -> Matrix_Float (r, c)
       | _ -> raise (Type_error ("Invalid Inverse: incompatible type " ^ string_of_type t ^ "😭")))
 

  let rec typecheck_stmt env = function
    | Assign(var, expr) ->
        let expr_type = type_of_expr env expr in
        add_var env var expr_type; 
        env

    | Seq stmts ->
        let block_env = create_env (Some env) in
        let _ = List.fold_left typecheck_stmt block_env stmts in
        env
    | If(cond, then_branch, else_branch) ->
        if type_of_expr env cond <> Bool then
          raise (Type_error "If condition must be boolean");
        let then_env = create_env (Some env) in
        let else_env = create_env (Some env) in
        ignore (typecheck_stmt then_env then_branch);
        ignore (typecheck_stmt else_env else_branch);
        env

    | While(cond, body) ->
        if type_of_expr env cond <> Bool then
          raise (Type_error "While condition must be boolean");
        let loop_env = create_env (Some env) in
        ignore (typecheck_stmt loop_env body);
        env

    | For(var, start, stop, step, body) ->
      (* 1. Check start/step in PARENT scope *)
        if type_of_expr env start <> Scalar_Int then
          raise (Type_error "For loop start must be integer");
        if type_of_expr env step <> Scalar_Int then
          raise (Type_error "For loop step must be integer");
            (* 2. Create NEW scope for loop *)
        let loop_env = create_env (Some env) in
            (* 3. Declare loop variable in NEW scope *)
        add_var loop_env var Scalar_Int;
            (* 4. Check condition/body in LOOP SCOPE *)
        if type_of_expr loop_env stop <> Bool then
          raise (Type_error "For loop condition must be boolean");
        ignore (typecheck_stmt loop_env body); 
        env

    | VectorDecl(var, dim, init) ->
        let init_type = type_of_expr env init in
        (match init_type with
         | Vector_Int d when d = dim -> add_var env var (Vector_Int dim)
         | Vector_Float d when d = dim -> add_var env var (Vector_Float dim)
         | _ -> raise (Type_error "Vector dimension mismatch"));
        env

    | MatrixDecl(var, rows, cols, init) ->
        let init_type = type_of_expr env init in
        (match init_type with
         | Matrix_Int (r,c) when r = rows && c = cols -> add_var env var (Matrix_Int (rows,cols))
         | Matrix_Float (r,c) when r = rows && c = cols -> add_var env var (Matrix_Float (rows,cols))
         | _ -> raise (Type_error "Matrix dimension mismatch"));
        env
    | PrintStmt e | ExprStmt e ->
      let _ = type_of_expr env e in
      env
    | _ -> failwith "Unimplemented statement type"

  
    let typecheck_program prog =
      try
        let global_env = create_env None in
        let _ = match prog with
          | Seq stmts -> List.fold_left typecheck_stmt global_env stmts
          | _ -> raise(Type_error "Program must be a sequence of statements")
        in
        (true, "Type checking successful", global_env)
      with
      | Type_error msg -> (false, "Type error: " ^ msg, create_env None)
      | Undefined_variable name -> (false, "Undefined variable: " ^ name, create_env None)
      | Redeclaration_error msg -> (false, "Redeclaration error: " ^ msg, create_env None)
    

  let print_TYPE_CHECKED_ENV env =
    Printf.printf "\n<--Type checking successful-->✅🥳\n";
    let rec print_help env indent =
      Hashtbl.iter (fun name typ ->  (* Parameter names are name/typ *)
          Printf.printf "%s%s : %s\n" indent name (string_of_type typ)
        ) env.symbol_table;
      match env.parent with
      | Some p -> print_help p (indent ^ "  ")
      | None -> ()
    in
    print_help env ""   
end
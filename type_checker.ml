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
      | Some _ -> () (* Variable already declared with the same type *)
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
    | ConstV v -> 
      if v = [] then raise (Type_error "Empty vector")
      else Vector_Float (List.length v)
    | ConstIntV v -> 
      if v = [] then raise (Type_error "Empty vector")
      else Vector_Int (List.length v)
    | ConstM m ->
        let rows = List.length m in
        let cols = match m with h::_ -> List.length h | [] -> 0 in
        Matrix_Float (rows, cols)
    | ConstIntM m ->
        let rows = List.length m in
        let cols = match m with h::_ -> List.length h | [] -> 0 in
        Matrix_Int (rows, cols)
    
    | Var v -> lookup_var env v
    | VectorDecl(size, v) -> 
      let t = type_of_expr env v in
      (match t with
      | Vector_Int d->
          if d = size then Vector_Int d
          else raise (Type_error "Vector size does not match the declared size")
      | Vector_Float d->
          if d = size then Vector_Float d
          else raise (Type_error "Vector size does not match the declared size")
      | _ -> raise (Type_error "Invalid vector declaration: size must be a positive integer and match the vector type"))

    | MatrixDecl(rows, cols, m) ->  
      let t = type_of_expr env m in
      (match t with
      | Matrix_Int (r, c)->
          if r = rows && c = cols then Matrix_Int (r, c)
          else raise (Type_error "Matrix dimensions do not match the declared dimensions")
      | Matrix_Float (r, c) ->
          if r = rows && c = cols then Matrix_Float (r, c)
          else raise (Type_error "Matrix dimensions do not match the declared dimensions")
      | _ -> raise (Type_error "Invalid matrix declaration: dimensions must be positive integers and match the matrix type"))
 
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
         | (Scalar_Int,Scalar_Int) -> Scalar_Int
         | (Scalar_Float,Scalar_Float) -> Scalar_Float
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
       | Matrix_Int (r, c) when r = c -> Matrix_Float (r, c)
       | Matrix_Float (r, c) when r = c -> Matrix_Float (r, c)
       | _ -> raise (Type_error ("Invalid Inverse: incompatible type " ^ string_of_type t ^ "😭")))
    | AccessV(_, i) -> 
      (match type_of_expr env i with 
      | Scalar_Int -> Scalar_Float
      | _ -> raise (Type_error "Access index must be an integer"))
    | AccessM(_, i,j) ->
      let t1 = type_of_expr env i in
      if t1 <> Scalar_Int then raise (Type_error "Row index must be an integer")
      else 
        let t2 = type_of_expr env j in
        if t2 <> Scalar_Int then raise (Type_error "Column index must be an integer")
        else Scalar_Float 
    | _ -> raise (Type_error "should not reach here its input")

  let rec typecheck_stmt env = function
  | Assign(var, expr, Some typ) ->
    let var_type = check_get_var_type env var in
    (match var_type with
     | Some t -> raise (Redeclaration_error ("Variable " ^ var ^ " already declared with type " ^ string_of_type t))
     | None -> ());
    add_var env var typ;
    (match expr with
     | InputExpr _ -> ()
     | _ ->
       let expr_type = type_of_expr env expr in
       if expr_type <> typ then
         raise (Type_error ("Type mismatch: expected " ^ string_of_type typ ^ " but got " ^ string_of_type expr_type ^ " for variable " ^ var)));
    env
  | Assign(var, expr, None) ->
    let var_type = check_get_var_type env var in
    (match var_type with
    | Some t -> ()
    | None -> raise (Undefined_variable ("Undefined variable: " ^ var)));
    
    (match expr with
    | InputExpr _ -> raise (Type_error "Input expression cannot be assigned without a type")
    | _ ->
        let expr_type = type_of_expr env expr in
        add_var env var expr_type);
    env
    | Seq stmts ->
        let block_env = create_env (Some env) in
        let _ = List.fold_left typecheck_stmt block_env stmts in
        env
    | If(cond, tblock, fblock) ->
        if type_of_expr env cond <> Bool then
          raise (Type_error "If condition must be boolean");
        let then_env = create_env (Some env) in
        let else_env = create_env (Some env) in
        ignore (typecheck_stmt then_env tblock);
        ignore (typecheck_stmt else_env fblock);
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

    | PrintStmt e | ExprStmt e ->
      let _ = type_of_expr env e in
      env 
  
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
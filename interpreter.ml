(* evaluator.ml *) 
open Ast
open Parsing
open Lexing
open Lexer
open Parser
exception RuntimeError of string   
exception InputError of string

(**Type_Of_Values*)
type value =
  | BoolVal of bool
  | IntVal of int
  | FloatVal of float
  | IntVector of int list
  | FloatVector of float list
  | IntMatrix of int list list
  | FloatMatrix of float list list
(**For_Printing*)
let rec string_of_value = function
  | BoolVal b -> "Bool(" ^ string_of_bool b ^ ")" 
  | IntVal i -> "Int(" ^ string_of_int i ^ ")"
  | FloatVal f -> "Float(" ^ string_of_float f ^ ")"
  | IntVector lst -> "IntVector[" ^ String.concat ", " (List.map string_of_int lst) ^ "]"
  | FloatVector lst -> "FloatVector[" ^ String.concat ", " (List.map string_of_float lst) ^ "]"
  | IntMatrix m ->"IntMatrix[\n" ^ String.concat "\n" (List.map (fun r -> "  [" ^ String.concat ", " (List.map string_of_int r) ^ "]") m) ^ "\n]"
  | FloatMatrix m ->"FloatMatrix[\n" ^ String.concat "\n"  (List.map (fun r -> "  [" ^ String.concat ", " (List.map string_of_float r) ^ "]") m) ^ "\n]"


  
(**Enviournment*)
type environment = {
  parent: environment option;
  symbols: (string, value) Hashtbl.t;}
let create_env parent = { parent; symbols = Hashtbl.create 16 }
let rec lookup_env env id =
  match Hashtbl.find_opt env.symbols id with
  | Some v -> v
  | None -> match env.parent with
    | Some parent -> lookup_env parent id
    | None -> raise (RuntimeError ("Undefined variable: " ^ id))
    
(* let debug_env env = 
  (match env.parent with
   | Some env -> 
       Printf.printf "\n==PARENT==:\n";
       Hashtbl.iter (fun k v -> 
           Printf.printf "- %s : %s\n" k (string_of_value v)  (* Changed from Evaluator *)
         ) env.symbols
   | None -> Printf.printf "\n==NO_PARENT==\n");
  Printf.printf "\n==CHILD==:\n";
  Hashtbl.iter (fun k v -> 
      Printf.printf "- %s : %s\n" k (string_of_value v)  (* Changed from Evaluator *)
    ) env.symbols 
;; *)

let global_env = create_env None;;

let rec check_and_update_env env var_name new_value =
  match env with
  | Some env ->
    (*if present in table and of same type as new_value*)
      if Hashtbl.mem env.symbols var_name then (
        let current_value = Hashtbl.find env.symbols var_name in
        match current_value, new_value with
        | IntVal _, IntVal _
        | FloatVal _, FloatVal _
        | IntVector _, IntVector _
        | FloatVector _, FloatVector _
        | IntMatrix _, IntMatrix _
        | FloatMatrix _, FloatMatrix _
        | BoolVal _, BoolVal _ -> Hashtbl.replace env.symbols var_name new_value; true
        | _ -> raise (RuntimeError ( "Redeclaration error/Assigning diff type: Variable '" ^ var_name ^ "' redeclared with type"^string_of_value current_value^" (original : "^ string_of_value new_value^") 😭"))
      ) else
        check_and_update_env env.parent var_name new_value
  | None -> false (* Variable not found in hierarchy *)
  
let update_env env id value =
  let temp = check_and_update_env (Some(env)) id value in
  (match temp with 
   |true -> ()
   |false ->Hashtbl.add env.symbols id value)
(**Helper_Functions*)
let matrix_dims_equal m1 m2 =
  let rows1 = List.length m1 in
  let rows2 = List.length m2 in
  rows1 = rows2 && 
  (let cols1 = List.length (List.hd m1) in
    let cols2 = List.length (List.hd m2) in
    cols1 = cols2)

let add_values v1 v2 =
  match (v1, v2) with
  | IntVal a, IntVal b -> IntVal (a + b)
  | FloatVal a, FloatVal b -> FloatVal (a +. b)
  | IntVector a, IntVector b when List.length a = List.length b -> IntVector (List.map2 (+) a b)
  | FloatVector a, FloatVector b when List.length a = List.length b -> FloatVector (List.map2 (+.) a b)
  | IntMatrix a, IntMatrix b when matrix_dims_equal a b ->IntMatrix (List.map2 (List.map2 (+)) a b)
  | FloatMatrix a, FloatMatrix b when matrix_dims_equal a b ->FloatMatrix (List.map2 (List.map2 (+.)) a b)
  | BoolVal a, BoolVal b -> BoolVal (a || b) (* Assuming addition for booleans is logical OR *)
  | _ -> raise (RuntimeError ("Invalid addition operands: cannot add " ^ string_of_value v1 ^ " and " ^ string_of_value v2 ^ "😭"))
      
let inv_value = function
  | IntVal i -> IntVal (-i)
  | FloatVal f -> FloatVal (-.f)
  | IntVector v -> IntVector (List.map (( * ) (-1)) v)
  | FloatVector v -> FloatVector (List.map (( *. ) (-1.0)) v)
  | IntMatrix m -> IntMatrix (List.map (List.map (( * ) (-1))) m)
  | FloatMatrix m -> FloatMatrix (List.map (List.map (( *. ) (-1.0))) m)
  | BoolVal b -> BoolVal (not b)      
      
let scal_prod v1 v2 =
  match (v1, v2) with
  | IntVal a, IntVal b -> IntVal (a * b)
  | FloatVal a, FloatVal b -> FloatVal (a *. b)
  | IntVal s, IntVector v -> IntVector (List.map (( * ) s) v)
  | FloatVal s, FloatVector v -> FloatVector (List.map (( *. ) s) v)
  | IntVal s, IntMatrix m -> IntMatrix (List.map (List.map (( * ) s)) m)
  | FloatVal s, FloatMatrix m -> FloatMatrix (List.map (List.map (( *. ) s)) m)
  | IntVector v, IntVal s -> IntVector (List.map (( * ) s) v)
  | FloatVector v, FloatVal s -> FloatVector (List.map (( *. ) s) v)
  | IntMatrix m, IntVal s -> IntMatrix (List.map (List.map (( * ) s)) m)
  | FloatMatrix m, FloatVal s -> FloatMatrix (List.map (List.map (( *. ) s)) m)
  | BoolVal b, BoolVal b2 -> BoolVal (b && b2)
  | _ -> raise (RuntimeError ("Invalid scalar product operands: cannot compute scalar product of " ^ string_of_value v1 ^ " and " ^ string_of_value v2 ^ " 😭"))
  
let dot_prod v1 v2 = match (v1, v2) with
  | IntVector v1, IntVector v2 when List.length v1 = List.length v2 ->IntVal (List.fold_left2 (fun acc a b -> acc + a * b) 0 v1 v2)
  | FloatVector v1, FloatVector v2 when List.length v1 = List.length v2 ->FloatVal (List.fold_left2 (fun acc a b -> acc +. a *. b) 0.0 v1 v2)
  | IntVal v1, IntVal v2 when v2 <> 0 -> IntVal (v1 / v2)
  | IntVal _, IntVal 0 -> raise (RuntimeError "Division by zero is not allowed for integers 😭")
  | FloatVal v1, FloatVal v2 when abs_float v2 > 1e-6 -> FloatVal (v1 /. v2)
  | FloatVal _, FloatVal v2 when abs_float v2 <= 1e-6 -> raise (RuntimeError "Division by zero is not allowed for floats 😭")
  | _ -> raise (RuntimeError ("Invalid dot product operands: cannot compute dot product of " ^ string_of_value v1 ^ " and " ^ string_of_value v2 ^ " 😭"))
    
let magnitude v = 
  match v with 
  | IntVector v -> FloatVal (sqrt (float_of_int (List.fold_left (fun acc x -> acc + x * x) 0 v)))
  | FloatVector v -> FloatVal (sqrt (List.fold_left (fun acc x -> acc +. x *. x) 0.0 v))
  | IntVal i -> IntVal (abs i)
  | FloatVal f -> FloatVal (abs_float f)
  | BoolVal b -> BoolVal true (* Assuming magnitude of boolean is true *)
  | _ -> raise (RuntimeError ("Cannot find magnitude of " ^ string_of_value v^" 😭"))
  
let rec transpose matrix =
  match matrix with
  | [] | [] :: _ -> [] (* returning the empty if input is non regular matrix *)
  | _ ->
      let heads = List.map List.hd matrix in
      let tails = List.map List.tl matrix in
      heads :: transpose tails 

let transpose_value = function
  | IntMatrix m -> IntMatrix (transpose m)
  | FloatMatrix m -> FloatMatrix (transpose m)
  | value -> raise (RuntimeError ("Transpose requires a matrix: cannot transpose " ^ string_of_value value ^ " 😭"))
    

let rec matrix_mult v1 v2 =
  match (v1, v2) with
  | IntMatrix m1, IntMatrix m2 ->
      let m2t = transpose m2 in
      IntMatrix (List.map (fun row ->
          List.map (fun col ->
              List.fold_left2 (fun acc a b -> acc + a * b) 0 row col
            ) m2t
        ) m1)
  | FloatMatrix m1, FloatMatrix m2 ->
      let m2t = transpose m2 in
      FloatMatrix (List.map (fun row ->
          List.map (fun col ->
              List.fold_left2 (fun acc a b -> acc +. a *. b) 0.0 row col
            ) m2t
        ) m1)
  | _ -> raise (RuntimeError ("Matrix multiplication requires two matrices: cannot multiply " ^ string_of_value v1 ^ " and " ^ string_of_value v2 ^ " 😭"))
                
module MatrixOps = struct
  (* get the minor *)
  let minor m row col =
    List.mapi (fun i r ->
        if i = row then []
        else
          List.mapi (fun j x -> if j = col then None else Some x) r
          |> List.filter_map Fun.id
      ) m
    |> List.filter (fun r -> r <> [])
  
  (* Determinant for integer matrices *)
  let rec det_int = function
    | [[x]] -> x
    | m ->
        List.mapi (fun i x ->
            x * det_int (minor m 0 i) * (if i mod 2 = 0 then 1 else -1)
          ) (List.hd m)
        |> List.fold_left (+) 0
  
  (* Determinant for float matrices *)
  let rec det_float = function
    | [[x]] -> x
    | m ->
        List.mapi (fun i x ->
            x *. det_float (minor m 0 i) *. (if i mod 2 = 0 then 1.0 else -1.0)
          ) (List.hd m)
        |> List.fold_left (+.) 0.0
end
let determinant = function
  | IntMatrix m -> IntVal (MatrixOps.det_int m)
  | FloatMatrix m -> FloatVal (MatrixOps.det_float m)
  | _ -> raise (RuntimeError "Determinant requires a matrix")


let angle v1 v2 =
  let dot = match dot_prod v1 v2 with
    | IntVal d -> float_of_int d
    | FloatVal d -> d
    | _ -> raise (RuntimeError "Dot product must return a numeric value") in
  let abs1 = match magnitude v1 with
    | FloatVal m -> m
    | _ -> raise (RuntimeError "Magnitude must return a float value") in
  let abs2 = match magnitude v2 with
    | FloatVal m -> m
    | _ -> raise (RuntimeError "Magnitude must return a float value") in
  if abs_float abs1 < 1e-6 || abs_float abs2 < 1e-6 then
    raise (RuntimeError ("Cannot compute angle with zero magnitude: " ^ string_of_value v1 ^ " and " ^ string_of_value v2 ^ " 😭"))
  else
    FloatVal (acos (dot /. (abs1 *. abs2)))
  
let is_zero = function
  | IntVal i -> BoolVal (i = 0)
  | FloatVal f -> BoolVal (abs_float f < 1e-6)
  | IntVector v -> BoolVal (List.for_all ((=) 0) v)
  | FloatVector v -> BoolVal (List.for_all (fun x -> abs_float x < 1e-6) v)
  | IntMatrix m -> BoolVal (List.for_all (List.for_all ((=) 0)) m)
  | FloatMatrix m -> BoolVal (List.for_all (List.for_all (fun x -> abs_float x < 1e-6)) m)
  | BoolVal b -> BoolVal (not b)

let eq v1 v2 = match (v1, v2) with
  | (BoolVal b1, BoolVal b2) -> BoolVal (b1 = b2)
  | (IntVal i1, IntVal i2) -> BoolVal (i1 = i2)
  | (FloatVal f1, FloatVal f2) -> BoolVal (abs_float (f1 -. f2) < 1e-6)
  | (IntVector v1, IntVector v2) -> BoolVal (v1 = v2)
  | (FloatVector v1, FloatVector v2) -> BoolVal (List.for_all2 (fun a b -> abs_float (a -. b) < 1e-6) v1 v2)
  | (IntMatrix m1, IntMatrix m2) -> BoolVal (m1 = m2)
  | (FloatMatrix m1, FloatMatrix m2) -> BoolVal (List.for_all2 (fun row1 row2 -> 
                    List.for_all2 (fun a b -> abs_float (a -. b) < 1e-6) row1 row2)  m1 m2)
  | _ -> raise (RuntimeError ("Cannot compare different types for equality "^ string_of_value v1 ^ " and " ^ string_of_value v2 ^ " 😭"))
  
let neq v1 v2 = match (v1, v2) with
  | (BoolVal b1, BoolVal b2) -> BoolVal (b1 <> b2)
  | (IntVal i1, IntVal i2) -> BoolVal (i1 <> i2)
  | (FloatVal f1, FloatVal f2) -> BoolVal (abs_float (f1 -. f2) >= 1e-6)
  | (IntVector v1, IntVector v2) -> BoolVal (v1 <> v2)
  | (FloatVector v1, FloatVector v2) -> BoolVal (not (List.for_all2 (fun a b -> abs_float (a -. b) < 1e-6) v1 v2))
  | (IntMatrix m1, IntMatrix m2) -> BoolVal (m1 <> m2)
  | (FloatMatrix m1, FloatMatrix m2) -> BoolVal (not (List.for_all2 (fun row1 row2 -> 
                         List.for_all2 (fun a b -> abs_float (a -. b) < 1e-6) row1 row2) m1 m2))
  | _ -> raise (RuntimeError ("Cannot compare different types for inequality "^ string_of_value v1 ^ " and " ^ string_of_value v2 ^ " 😭"))
  
let lt v1 v2 =match (v1, v2) with
  | (IntVal i1, IntVal i2) -> BoolVal (i1 < i2)
  | (FloatVal f1, FloatVal f2) -> BoolVal (f2 -. f1 > 1e-6)
  | _ -> raise (RuntimeError ("Cannot compare less than for "^ string_of_value v1 ^ " and " ^ string_of_value v2 ^ " 😭"))

let gt v1 v2 = match (v1, v2) with
  | (IntVal i1, IntVal i2) -> BoolVal (i1 > i2)
  | (FloatVal f1, FloatVal f2) -> BoolVal (f1 -. f2 > 1e-6)
  | _ -> raise (RuntimeError ("Cannot compare greater than for "^ string_of_value v1 ^ " and " ^ string_of_value v2 ^ " 😭"))
  
let le v1 v2 = match (v1, v2) with
  | (IntVal i1, IntVal i2) -> BoolVal (i1 <= i2)
  | (FloatVal f1, FloatVal f2) -> BoolVal (f1-. f2 < 1e-6)
  | _ -> raise (RuntimeError ("Cannot compare less than equal to for "^ string_of_value v1 ^ " and " ^ string_of_value v2 ^ " 😭"))
 
let ge v1 v2 = match (v1, v2) with
  | (IntVal i1, IntVal i2) -> BoolVal (i1 >= i2)
  | (FloatVal f1, FloatVal f2) -> BoolVal (f2 -. f1 < 1e-6)
  | _ -> raise (RuntimeError ("Cannot compare greater than equal to for "^ string_of_value v1 ^ " and " ^ string_of_value v2 ^ " 😭"))
  
let eval_mod e1 e2 = match (e1,e2) with
  | IntVal i1, IntVal i2 -> IntVal (i1 mod i2)  | _ -> raise (RuntimeError ("Modulus requires integer values: cannot compute modulus of " ^ string_of_value e1 ^ " and " ^ string_of_value e2 ^ " 😭"))

(* finding the inverse *)
let matrix_inverse_gauss (m: float list list) =
  let n = List.length m in
  (* Augment m with the identity matrix *)
  let aug = List.mapi (fun i row ->
      row @ (List.init n (fun j -> if i = j then 1.0 else 0.0))
    ) m in
  let aug_arr = Array.of_list (List.map Array.of_list aug) in
  for i = 0 to n - 1 do
    let pivot = ref i in      (* geting pivot row *)
    for j = i + 1 to n - 1 do
      if abs_float aug_arr.(j).(i) > abs_float aug_arr.(!pivot).(i) then pivot := j
    done;
    if abs_float aug_arr.(!pivot).(i) < 1e-12 then failwith "Matrix is singular";
    
    let temp = aug_arr.(i) in (* Swap pivot and curr row *)
    aug_arr.(i) <- aug_arr.(!pivot);
    aug_arr.(!pivot) <- temp;
    
    let pivot_val = aug_arr.(i).(i) in  (* working in pivot row *)
    for j = i to 2 * n - 1 do
      aug_arr.(i).(j) <- aug_arr.(i).(j) /. pivot_val
    done;
    
    for k = 0 to n - 1 do (* remove the current row *)
      if k <> i then
        let factor = aug_arr.(k).(i) in
        for j = i to 2 * n - 1 do
          aug_arr.(k).(j) <- aug_arr.(k).(j) -. factor *. aug_arr.(i).(j)
        done
    done
  done;
  (* returning the inverse *)
  Array.to_list (Array.map (fun row -> Array.to_list (Array.sub row n n)) aug_arr)

(**For_Input_Parsing*)
let get_single_input_expression env = 
  match Hashtbl.find_opt env.symbols "INPuTT" with
  | Some value -> value
  | None -> raise (RuntimeError "No input value found in the global environment")


let rec eval_expr env = function
  | T -> BoolVal true | F -> BoolVal false
  | ConstS f -> FloatVal f | ConstI i -> IntVal i
  | ConstV v -> FloatVector v | ConstIntV v -> IntVector v
  | ConstM m -> FloatMatrix m | ConstIntM m -> IntMatrix m
  | Var id -> lookup_env env id
  | Add (e1, e2) -> add_values (eval_expr env e1) (eval_expr env e2)
  | Mod (e1, e2) -> eval_mod (eval_expr env e1) (eval_expr env e2)
  | Inv e -> inv_value (eval_expr env e)
  | ScalProd (e1, e2) -> scal_prod (eval_expr env e1) (eval_expr env e2)
  | DotProd (e1, e2) -> dot_prod (eval_expr env e1) (eval_expr env e2)
  | Mag e -> magnitude (eval_expr env e)
  | Angle (e1, e2) -> angle (eval_expr env e1) (eval_expr env e2)
  | IsZero e -> is_zero (eval_expr env e)
  | Transpose e ->let value = eval_expr env e in transpose_value value
  | Mult (e1, e2) -> matrix_mult (eval_expr env e1) (eval_expr env e2)
  | Det e -> determinant (eval_expr env e)
  | Inverse e ->
      let value = eval_expr env e in
      (match value with  
      | FloatMatrix m when List.length m = List.length (List.hd m) -> FloatMatrix (matrix_inverse_gauss m)
      | _ -> raise (RuntimeError ("Inverse requires a matrix: cannot invert " ^ string_of_value value ^ " 😭")))
  | Eq (e1, e2) -> eq (eval_expr env e1) (eval_expr env e2)
  | Neq (e1, e2) -> neq (eval_expr env e1) (eval_expr env e2)
  | Lt (e1, e2) -> lt (eval_expr env e1) (eval_expr env e2)
  | Gt (e1, e2) -> gt (eval_expr env e1) (eval_expr env e2)
  | Le (e1, e2) -> le (eval_expr env e1) (eval_expr env e2)
  | Ge (e1, e2) -> ge (eval_expr env e1) (eval_expr env e2)
  | VectorDecl (size, e) ->let value = eval_expr env e in
      let vector = (match value with
          | IntVector v when List.length v = size -> value
          | FloatVector v when List.length v = size -> value
          | _ -> raise (RuntimeError ("Vector declaration size mismatch: expected size " ^ string_of_int size ^ ", but got " ^ string_of_value value ^ " 😭"))) in
      vector
  |MatrixDecl (rows, cols, e) ->
      let value = eval_expr env e in
      let matrix = (match value with
          | IntMatrix m when List.length m = rows && List.for_all (fun r -> List.length r = cols) m -> value
          | FloatMatrix m when List.length m = rows && List.for_all (fun r -> List.length r = cols) m -> value
          | _ -> raise (RuntimeError "Matrix declaration size mismatch")) in
      matrix
  |AccessV(var,e)->
      let value = lookup_env env var in
      let index = eval_expr env e in
      (match value, index with 
       | FloatVector v, IntVal i when i >= 0 && i < List.length v -> FloatVal (List.nth v i)  
       | _ -> raise (RuntimeError ("Invalid access to variable " ^ var ^ " with index " ^ string_of_value index ^ " 😭")))
  |AccessM(var,e1,e2)->
      let value = lookup_env env var in
      let row_index = eval_expr env e1 in
      let col_index = eval_expr env e2 in
      (match value, row_index, col_index with 
       | FloatMatrix m, IntVal i, IntVal j when i >= 0 && i < List.length m && j >= 0 && j < List.length (List.hd m) -> FloatVal (List.nth (List.nth m i) j)  
       | _ -> raise (RuntimeError ("Invalid access to variable " ^ var ^ " with indices " ^ string_of_value row_index ^ " and " ^ string_of_value col_index ^ " 😭")))
  | _ -> raise(RuntimeError ("ahaha😭"))

let parse_input lexbuf =
  try
    let ast = Parser.program Lexer.token lexbuf in
    ast
  with
  | Lexer.Lexing_error msg ->
      Printf.eprintf "Lexing error for input: %s\n" msg;
      exit 1
  | Parse_error ->  (* Corrected exception name from Parse_error to Parser.Error *)
      let pos = Lexing.lexeme_start_p lexbuf in
      Printf.eprintf "Input Parse error at line %d, column %d\n"
        pos.pos_lnum (pos.pos_cnum - pos.pos_bol);
      exit 1
  
and eval_bool env e =
  match eval_expr env e with
  | BoolVal b -> b
  | _ -> raise (RuntimeError "Condition must evaluate to boolean") 
            
let rec execute_stmt env = function
  | Assign (id, expr, Some t) -> 
      let value = 
        match expr with
        | InputExpr None ->  
            print_string "Enter input: ";
            let line =  read_line () in
            let lexbuf = Lexing.from_string line in
            let input_ast = parse_input lexbuf in
            let input_env = match input_ast with
              | Seq stmts -> List.fold_left execute_stmt global_env stmts
              | _ -> raise (RuntimeError "Input must be a Seq according to AST") in
            let input_val = get_single_input_expression input_env in
            input_val
        | InputExpr (Some filename) ->
            let file_path = filename ^ ".txt" in
            if Sys.file_exists file_path then
              let lines = ref [] in
              let ic = open_in file_path in
              try
                while true do
                  lines := input_line ic :: !lines
                done;
                IntVal 0
              with End_of_file ->close_in ic;
                let content = String.concat "\n" (List.rev !lines) in
                let lexbuf = Lexing.from_string content in
                let input_ast = parse_input lexbuf in
                let input_env = 
                  match input_ast with
                  | Seq stmts -> List.fold_left execute_stmt global_env stmts
                  | _ -> raise (RuntimeError "Input file must contain a Seq according to AST")in
                let input_val = get_single_input_expression input_env in
                input_val
              else raise (RuntimeError ("File not found: " ^ file_path))
        | _ -> eval_expr env expr in
  (* Type checking logic for the value and the type 't' *)
      (match value with
       | IntVal _ when t = Scalar_Int -> ()
       | FloatVal _ when t = Scalar_Float -> ()
       | IntVector v when t = Vector_Int (List.length v) -> ()
       | FloatVector v when t = Vector_Float (List.length v) -> ()
       | IntMatrix m when t = Matrix_Int ((List.length m), (List.length (List.hd m))) -> ()
       | FloatMatrix m when t = Matrix_Float ((List.length m), (List.length (List.hd m))) -> ()
       | BoolVal _ when t = Bool -> ()
       | _ -> raise (RuntimeError ("Type mismatch: " ^ string_of_value value ^ "This not matched with declared type 😭")) );
      
       let temp = check_and_update_env (Some(env)) id value in
      (match temp with 
       | true -> raise (RuntimeError ("Redeclaration error: Variable '" ^ id ^ "' redeclared with type " ^ string_of_value value ^ " 😭"))
       | false -> ()); 
      update_env env id value;  
      env   
  | Assign (id, expr, None) ->
      let value = eval_expr env expr in
      let temp = check_and_update_env (Some(env)) id value in
      (match temp with 
       | true -> ()
       | false -> raise (RuntimeError ("Redeclaration error: Variable '" ^ id ^ "' redeclared with type " ^ string_of_value value ^ " 😭")));
      update_env env id value; (* Update the environment with the new value *)
      env
  | Seq stmts ->  (* Create a new environment for the sequence of statements *)
      let new_env = create_env (Some env) in 
      List.fold_left execute_stmt new_env stmts |> ignore; 
      env
  | If (cond, then_b, else_b) ->
      let branch_env = create_env (Some env) in 
      if eval_bool env cond 
      then execute_stmt branch_env then_b |> ignore
      else execute_stmt branch_env else_b |> ignore;
      env
  | While (cond, body) ->
      let loop_env = create_env (Some env) in 
      while eval_bool loop_env cond do 
        execute_stmt loop_env body |> ignore; 
      done; 
      env
  | For (var, start_expr, cond_expr, step_val, body) ->
      (* Evaluate initial value *)
      let start_val = eval_expr env start_expr in 
      (* Validate types *)
      let check_int = function
        | IntVal _ -> true
        | _ -> false
      in
      if not (check_int start_val) then raise (RuntimeError "For loop start must be integer");
      
      (* Validate step value *)
      let step_int = match step_val with
        | ConstI s -> s  (* Extract integer step value directly *)
        | _ -> raise (RuntimeError "For loop step must be a scalar integer")
      in
      (* Create loop environment *)
      let loop_env = 
        let new_env = create_env (Some env) in
        update_env new_env var start_val;
        new_env
      in 
      (* Loop execution *)
      let rec loop current = 
        let cond_result = eval_expr loop_env cond_expr in
        match cond_result with
        | BoolVal true -> 
            execute_stmt loop_env body |> ignore; 
            let next_current = match current with
              | IntVal c -> IntVal (c + step_int)
              | _ -> raise (RuntimeError "Loop variable must be integer")
            in
            update_env loop_env var next_current;
  
            loop next_current (*loop*)
        | BoolVal false -> ()
        | _ -> raise (RuntimeError "For loop condition must be boolean")
      in
      loop start_val;
      env
  | PrintStmt e ->
      let value = eval_expr env e in
      print_endline (string_of_value value);
      env
  | ExprStmt e -> 
      let value = eval_expr env e in
      if Hashtbl.mem global_env.symbols "INPuTT" then Hashtbl.replace global_env.symbols "INPuTT" value
      else Hashtbl.add global_env.symbols "INPuTT" value;
      env

let execute_program program = match program with
  | Seq stmts -> List.fold_left execute_stmt global_env stmts
  | _ -> raise (RuntimeError "Program must be a sequence of statements")

let print_INTERPRETED_ENV env = 
  Hashtbl.remove global_env.symbols "INPuTT";
  Printf.printf "\n<---Final Interpreted Values--> ✅🥳\n";
  Hashtbl.iter (fun k v -> Printf.printf "- %s : %s\n" k (string_of_value v)  ) env.symbols
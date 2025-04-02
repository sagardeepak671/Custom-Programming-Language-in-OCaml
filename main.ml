(* main.ml *)
open Lexer
open Parser
open Lexing
open Ast
open Parsing
open Type_checker.TypeChecker 
open Interpreter 

let parse_input lexbuf =
  try
    let ast = Parser.program Lexer.token lexbuf in
    ast
  with
  | Lexer.Lexing_error msg ->
      Printf.eprintf "Lexing error: %s\n" msg;
      exit 1
  | Parse_error ->
      let pos = lexeme_start_p lexbuf in
      Printf.eprintf "Parse error at line %d, column %d\n"
        pos.pos_lnum (pos.pos_cnum - pos.pos_bol);
      exit 1
 
  (**AST TREE*)
let () =
  print_endline "Enter DSL code (press Ctrl+D to finish):\n\n";
  let lexbuf = Lexing.from_channel stdin in
  let ast = parse_input lexbuf in
  print_AST ast;

  
  (**TYPE CHECK*)
  let success, msg, type_checked_env = typecheck_program ast in
  if success then (
    print_TYPE_CHECKED_ENV type_checked_env;
  ) else
    Printf.printf "\n%s\n" msg;

  Printf.printf "\n<---STD OUT--> ✅🥳\n";

  (**INTERPRET*)
  try
    let final_interpreted_env = Interpreter.execute_program ast in 
    print_INTERPRETED_ENV final_interpreted_env;
  with
  | Interpreter.RuntimeError msg ->
      Printf.eprintf "Runtime error: %s\n" msg;
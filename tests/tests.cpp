// Unit tests for the tokenizer and parser. No framework: each check prints its name on
// failure and the process exits non-zero if anything failed.
#include "mydsl.hpp"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <random>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace {
int failures = 0, passed = 0;

void check(bool ok, const std::string& name, const std::string& detail = "") {
    if (ok) { ++passed; return; }
    ++failures;
    std::cerr << "FAIL: " << name;
    if (!detail.empty()) std::cerr << "\n      " << detail;
    std::cerr << '\n';
}

std::string dump(const std::string& source) {
    std::ostringstream out;
    mydsl::print_ast(mydsl::parse(source), out);
    return out.str();
}

// Parses `source` and compares the printed AST with `expected` (one line per statement).
void expect_ast(const std::string& source, const std::string& expected) {
    try {
        const auto actual = dump(source);
        check(actual == expected, "AST of: " + source, "expected:\n" + expected + "actual:\n" + actual);
    } catch (const std::exception& e) {
        check(false, "AST of: " + source, std::string("unexpected exception: ") + e.what());
    }
}

// Parses `source` and expects a Parse error at line/column whose message contains `fragment`.
void expect_error(const std::string& source, std::size_t line, std::size_t column,
                  const std::string& fragment, const std::string& stage = "Parse") {
    try {
        dump(source);
        check(false, "error for: " + source, "parsed successfully but an error was expected");
    } catch (const mydsl::Error& e) {
        const std::string message = e.what();
        const std::string prefix = stage + " error at line " + std::to_string(line) +
                                   ", column " + std::to_string(column) + ": ";
        const bool located = message.compare(0, prefix.size(), prefix) == 0;
        const bool explained = message.find(fragment) != std::string::npos;
        check(located && explained, "error for: " + source,
              "expected '" + prefix + "...' containing '" + fragment + "'\n      got      '" + message + "'");
    } catch (const std::exception& e) {
        check(false, "error for: " + source, std::string("wrong exception type: ") + e.what());
    }
}

void lexer_tests() {
    auto t = mydsl::lex("// comment\nint a:=1;");
    check(t[0].text == "int" && t[0].location.line == 2, "lexer skips comments and tracks lines");
    auto numbers = mydsl::lex("1 2.5 3e2");
    check(numbers[0].kind == mydsl::TokenKind::Integer && numbers[1].kind == mydsl::TokenKind::Float &&
          numbers[2].kind == mydsl::TokenKind::Float, "lexer classifies numbers");
    check(mydsl::lex("").at(0).kind == mydsl::TokenKind::End, "lexer yields End for empty input");
    try { mydsl::lex("@"); check(false, "lexer rejects '@'"); } catch (const mydsl::Error&) { ++passed; }
    try { mydsl::lex("/* unfinished"); check(false, "lexer rejects open comment"); } catch (const mydsl::Error&) { ++passed; }
}

void statement_tests() {
    expect_ast("", "");
    expect_ast("  // just a comment\n", "");
    expect_ast("int a := 1;", "(declare int a Int(1))\n");
    expect_ast("float f := 2.;", "(declare float f Float(2.0))\n");
    expect_ast("float f := .5e1;", "(declare float f Float(5.0))\n");
    expect_ast("bool b := true;", "(declare bool b Bool(true))\n");
    expect_ast("intvector 3 v := [1,2,3];", "(declare intvector 3 v (vector[3] Int(1) Int(2) Int(3)))\n");
    expect_ast("floatmatrix 2,1 m := [[1.],[2.]];", "(declare floatmatrix 2,1 m (matrix[2,1] Float(1.0) Float(2.0)))\n");
    expect_ast("intmatrix 1,2 m := [[1,2]];", "(declare intmatrix 1,2 m (matrix[1,2] Int(1) Int(2)))\n");
    expect_ast("a := b;", "(assign a (variable b))\n");
    expect_ast("v[i] := 1;", "(assign_index v (variable i) Int(1))\n");
    expect_ast("m[i, j+1] := 0.;", "(assign_index m (variable i) (binary + (variable j) Int(1)) Float(0.0))\n");
    expect_ast("Print(a);", "(print (variable a))\n");
    expect_ast("Print(Input());", "(print (call Input))\n");
    expect_ast("{};", "(block)\n");
    expect_ast("{ int a := 1; { a := 2; }; };",
               "(block\n  (declare int a Int(1))\n  (block\n    (assign a Int(2))))\n");
    expect_ast("if (a) then { Print(1); };",
               "(if (variable a)\n  (block\n    (print Int(1))))\n");
    expect_ast("if (a < b) then { Print(1); } else { Print(2); };",
               "(if (binary < (variable a) (variable b))\n  (block\n    (print Int(1)))\n  (block\n    (print Int(2))))\n");
    expect_ast("if (a) then {} else { if (b) then {}; };",
               "(if (variable a)\n  (block)\n  (block\n    (if (variable b)\n      (block))))\n");
    expect_ast("while (i < 10) { i := i + 1; };",
               "(while (binary < (variable i) Int(10))\n  (block\n    (assign i (binary + (variable i) Int(1)))))\n");
    expect_ast("for (i := 0, i < n, 1) { Print(i); };",
               "(for i Int(0) (binary < (variable i) (variable n)) Int(1)\n  (block\n    (print (variable i))))\n");
    expect_ast("for (i := n - 1, i >= 0, -1) {};",
               "(for i (binary - (variable n) Int(1)) (binary >= (variable i) Int(0)) (unary - Int(1))\n  (block))\n");
    expect_ast("int a := 1;\nint b := 2;\nPrint(a + b);",
               "(declare int a Int(1))\n(declare int b Int(2))\n(print (binary + (variable a) (variable b)))\n");
    // Statements and blocks are always terminated by ';', whitespace and comments are free.
    expect_ast("if(a)then{b:=1;}else{b:=2;};", "(if (variable a)\n  (block\n    (assign b Int(1)))\n  (block\n    (assign b Int(2))))\n");
    expect_ast("x := 1; /* multi\nline */ y := 2; // trailing", "(assign x Int(1))\n(assign y Int(2))\n");
    // Identifiers may contain apostrophes and digits; 'Input' is an ordinary function name.
    expect_ast("x' := x1;", "(assign x' (variable x1))\n");
    expect_ast("Input := 1;", "(assign Input Int(1))\n");
}

void function_syntax_tests() {
    // function <return type> <name>(<type> <param>, ...) { body };  return <expr>;
    expect_ast("function int square(int x) { return x * x; };",
               "(function int square (parameter int x)\n  (block\n    (return (binary * (variable x) (variable x)))))\n");
    expect_ast("function bool always() { return true; };", "(function bool always\n  (block\n    (return Bool(true))))\n");
    expect_ast("function float f(floatvector 3 v, intmatrix 2,2 m, bool b) { return 1.5; };",
               "(function float f (parameter floatvector 3 v) (parameter intmatrix 2,2 m) (parameter bool b)\n  (block\n    (return Float(1.5))))\n");
    expect_ast("function intvector 2 pair(int a, int b) { return [a, b]; };",
               "(function intvector 2 pair (parameter int a) (parameter int b)\n  (block\n    (return (vector[2] (variable a) (variable b)))))\n");
    expect_ast("function int f(int n) { if (n < 1) then { return 0; } else { return n + f(n - 1); }; };",
               "(function int f (parameter int n)\n  (block\n    (if (binary < (variable n) Int(1))\n      (block\n        (return Int(0)))\n"
               "      (block\n        (return (binary + (variable n) (call f (binary - (variable n) Int(1)))))))))\n");
    expect_ast("function int f(int x) { int y := x; y := y + 1; Print(y); return y; };",
               "(function int f (parameter int x)\n  (block\n    (declare int y (variable x))\n    (assign y (binary + (variable y) Int(1)))\n"
               "    (print (variable y))\n    (return (variable y))))\n");
    expect_ast("function int f() { while (true) { return 1; }; return 2; };",
               "(function int f\n  (block\n    (while Bool(true)\n      (block\n        (return Int(1))))\n    (return Int(2))))\n");
    // Calls are ordinary expressions: arguments, nesting, indexing of the result.
    expect_ast("int a := square(4);", "(declare int a (call square Int(4)))\n");
    expect_ast("Print(add(square(2), 3)[0]);", "(print (index (call add (call square Int(2)) Int(3)) Int(0)))\n");
    expect_ast("function int f(int x) { return x; };\nint a := f(2);\nPrint(f(a) + f(f(1)));",
               "(function int f (parameter int x)\n  (block\n    (return (variable x))))\n(declare int a (call f Int(2)))\n"
               "(print (binary + (call f (variable a)) (call f (call f Int(1)))))\n");
    expect_ast("function int f() { return Input(); };", "(function int f\n  (block\n    (return (call Input))))\n");
    // Parameters carry their type, name and span.
    {
        auto program = mydsl::parse("function float f(int x, floatvector 3 v) { return 1.0; };");
        const auto& f = program.at(0);
        check(f->op == "function" && f->name == "f" && f->declaration && f->declaration->str() == "float" && f->params.size() == 2, "function node fields");
        check(f->params.at(0).name == "x" && f->params.at(0).type.str() == "int" && f->params.at(0).location.column == 18 &&
              f->params.at(0).span.begin.column == 18 && f->params.at(0).span.end.column == 23, "parameter 1 fields and span");
        check(f->params.at(1).name == "v" && f->params.at(1).type.str() == "floatvector 3" && f->params.at(1).span.begin.column == 25 &&
              f->params.at(1).span.end.column == 40, "parameter 2 fields and span");
        check(f->body.size() == 1 && f->body.at(0)->op == "block" && f->body.at(0)->body.at(0)->op == "return", "function body is a block with the return");
        check(f->span.begin.column == 1 && f->span.end.column == 58, "function span runs through ';'");
        check(f->body.at(0)->body.at(0)->span.begin.column == 44 && f->body.at(0)->body.at(0)->span.end.column == 55, "return span runs through ';'");
    }
    // Function and parameter names follow the identifier rules; 'function'/'return' are keywords.
    expect_ast("function int f'(int x1) { return x1; };", "(function int f' (parameter int x1)\n  (block\n    (return (variable x1))))\n");
    expect_error("int function := 1;", 1, 5, "Cannot use keyword 'function' as a variable name");
    expect_error("return := 1;", 1, 1, "'return' can only be used inside a function body");
    expect_error("x := return;", 1, 6, "Expected an expression but found keyword 'return'");
    expect_error("x := function;", 1, 6, "Expected an expression but found keyword 'function'");
    // Invalid declarations.
    expect_error("function square(int x) { return x; };", 1, 10, "Expected a return type after 'function' but found 'square'; write: function <type> square(...) { ... };");
    expect_error("function void f() { return 1; };", 1, 10, "Functions must return a value; there is no 'void' return type");
    expect_error("function 3 f() { return 1; };", 1, 10, "Expected a return type after 'function' (int, float, bool, string, intvector N, floatvector N, intmatrix R,C or floatmatrix R,C) but found '3'");
    expect_error("function int (int x) { return x; };", 1, 14, "Expected a function name after the return type 'int' but found '('");
    expect_error("function int if(int x) { return x; };", 1, 14, "Cannot use keyword 'if' as a function name");
    expect_error("function int Print() { return 1; };", 1, 14, "Cannot use keyword 'Print' as a function name");
    expect_error("function intvector v() { return [1]; };", 1, 20, "Expected a positive integer as the vector size after 'intvector' but found 'v'");
    expect_error("function int f := 1;", 1, 16, "Expected '(' after function name 'f' but found ':='; a function needs a parameter list, e.g. function int f() { ... };");
    expect_error("function int f { return 1; };", 1, 16, "Expected '(' after function name 'f' to start the parameter list but found '{'");
    expect_error("function int f(x) { return x; };", 1, 16, "Parameter 'x' of function 'f' needs a type, e.g. int x");
    expect_error("function int f(int x, y) { return x; };", 1, 23, "Parameter 'y' of function 'f' needs a type, e.g. int y");
    expect_error("function int f(int) { return 1; };", 1, 19, "Expected a parameter name after parameter type 'int' of function 'f' but found ')'");
    expect_error("function int f(int for) { return 1; };", 1, 20, "Cannot use keyword 'for' as a parameter name");
    expect_error("function int f(1) { return 1; };", 1, 16, "Expected a parameter type (int, float, bool, string, intvector N, floatvector N, intmatrix R,C or floatmatrix R,C) in the parameter list of 'f' but found '1'");
    expect_error("function int f(int x, ) { return x; };", 1, 23, "Expected a parameter after ',' in the parameter list of 'f' but found ')'");
    expect_error("function int f(int x int y) { return x; };", 1, 22, "Expected ')' to close the parameter list of 'f' opened at line 1, column 15 but found 'int'");
    expect_error("function int f(int x := 1) { return x; };", 1, 22, "Parameters cannot have default values (found ':=' after parameter 'x')");
    expect_error("function int f(int x);", 1, 22, "Expected '{' to start the body of function 'f' but found ';' (a function must have a body)");
    expect_error("function int f(int x) return x;", 1, 23, "Expected '{' to start the body of function 'f' but found 'return'");
    expect_error("function int f(int x) { return x; }", 1, 36, "Expected ';' to end the definition of function 'f' but found end of input");
    expect_error("function int f(int x) { return x; }\nPrint(f(1));", 2, 1, "Expected ';' to end the definition of function 'f' but found 'Print'");
    expect_error("function int f(int x) {\n  return x;\n", 3, 1, "Expected '}' to close the block opened at line 1, column 23 but found end of input");
    expect_error("int f(int x) { return x; };", 1, 6, "Expected ':=' after variable name 'f' but found '('; to define a function write: function int f(...) { ... };");
    expect_error("floatvector 2 f(int x) { return [1., 2.]; };", 1, 16, "to define a function write: function floatvector 2 f(...) { ... };");
    // Functions only at the top level; return only inside functions.
    expect_error("{ function int f() { return 1; }; };", 1, 3, "A function can only be defined at the top level of the program, not inside a block or loop");
    expect_error("while (true) { function int f() { return 1; }; };", 1, 16, "A function can only be defined at the top level");
    expect_error("if (true) then { function int f() { return 1; }; };", 1, 18, "A function can only be defined at the top level");
    expect_error("function int f() { function int g() { return 1; }; return 1; };", 1, 20, "Functions cannot be nested; define this function at the top level of the program");
    expect_error("return 1;", 1, 1, "'return' can only be used inside a function body");
    expect_error("{ return 1; };", 1, 3, "'return' can only be used inside a function body");
    expect_error("for (i := 0, i < 3, 1) { return i; };", 1, 26, "'return' can only be used inside a function body");
    expect_error("function int f() { return 1; };\nreturn 2;", 2, 1, "'return' can only be used inside a function body");
    expect_error("function int f() { return; };", 1, 26, "Expected a value after 'return' (every function returns a value of its declared type)");
    expect_error("function int f() { return 1 };", 1, 29, "Expected ';' to end the return statement but found '}'");
    expect_error("function int f() { return 1; 2; };", 1, 30, "Expected a statement (declaration, assignment, Print, error, block, if, while, for or return) but found '2'");
    // A call is still not a statement.
    expect_error("function int f() { return 1; }; f();", 1, 34, "A call to 'f' cannot be used as a statement");
}

void expression_tests() {
    // Precedence: * / % over + -, over comparisons, over == !=, over &&, over ||.
    expect_ast("x := 1 + 2 * 3;", "(assign x (binary + Int(1) (binary * Int(2) Int(3))))\n");
    expect_ast("x := 1 * 2 + 3;", "(assign x (binary + (binary * Int(1) Int(2)) Int(3)))\n");
    expect_ast("x := (1 + 2) * 3;", "(assign x (binary * (binary + Int(1) Int(2)) Int(3)))\n");
    expect_ast("x := 1+2*3-4/5%6;",
               "(assign x (binary - (binary + Int(1) (binary * Int(2) Int(3))) (binary % (binary / Int(4) Int(5)) Int(6))))\n");
    expect_ast("x := a < b + 1;", "(assign x (binary < (variable a) (binary + (variable b) Int(1))))\n");
    expect_ast("x := a + 1 == b;", "(assign x (binary == (binary + (variable a) Int(1)) (variable b)))\n");
    expect_ast("x := a < b == c < d;",
               "(assign x (binary == (binary < (variable a) (variable b)) (binary < (variable c) (variable d))))\n");
    expect_ast("x := a == b && c != d;",
               "(assign x (binary && (binary == (variable a) (variable b)) (binary != (variable c) (variable d))))\n");
    expect_ast("x := a || b && c;", "(assign x (binary || (variable a) (binary && (variable b) (variable c))))\n");
    expect_ast("x := a && b || c;", "(assign x (binary || (binary && (variable a) (variable b)) (variable c)))\n");
    expect_ast("x := (a && b) || true && a;",
               "(assign x (binary || (binary && (variable a) (variable b)) (binary && Bool(true) (variable a))))\n");
    expect_ast("x := !a && b;", "(assign x (binary && (unary ! (variable a)) (variable b)))\n");
    expect_ast("x := !(a && b);", "(assign x (unary ! (binary && (variable a) (variable b))))\n");
    // Left associativity.
    expect_ast("x := a - b - c;", "(assign x (binary - (binary - (variable a) (variable b)) (variable c)))\n");
    expect_ast("x := a / b / c;", "(assign x (binary / (binary / (variable a) (variable b)) (variable c)))\n");
    expect_ast("x := a - (b - c);", "(assign x (binary - (variable a) (binary - (variable b) (variable c))))\n");
    expect_ast("x := a <= b <= c;", "(assign x (binary <= (binary <= (variable a) (variable b)) (variable c)))\n");
    // Unary minus and not bind tighter than binary operators and can be chained.
    expect_ast("x := -a * b;", "(assign x (binary * (unary - (variable a)) (variable b)))\n");
    expect_ast("x := a + 2 - -2;", "(assign x (binary - (binary + (variable a) Int(2)) (unary - Int(2))))\n");
    expect_ast("x := a+2--2;", "(assign x (binary - (binary + (variable a) Int(2)) (unary - Int(2))))\n");
    expect_ast("x := - - -a;", "(assign x (unary - (unary - (unary - (variable a)))))\n");
    expect_ast("x := -!a;", "(assign x (unary - (unary ! (variable a))))\n");
    expect_ast("x := -a[0];", "(assign x (unary - (index (variable a) Int(0))))\n");
    expect_ast("x := -f(1);", "(assign x (unary - (call f Int(1))))\n");
    // Parentheses.
    expect_ast("x := ((1));", "(assign x Int(1))\n");
    expect_ast("x := (a + b) * (c - d);",
               "(assign x (binary * (binary + (variable a) (variable b)) (binary - (variable c) (variable d))))\n");
    expect_ast("x := ((a + b) * c) / ((d));",
               "(assign x (binary / (binary * (binary + (variable a) (variable b)) (variable c)) (variable d)))\n");
    // Calls.
    expect_ast("x := f();", "(assign x (call f))\n");
    expect_ast("x := abs(a);", "(assign x (call abs (variable a)))\n");
    expect_ast("x := dotprod_vec(a, b);", "(assign x (call dotprod_vec (variable a) (variable b)))\n");
    expect_ast("x := mult_mat(inverse_mat(a), b);",
               "(assign x (call mult_mat (call inverse_mat (variable a)) (variable b)))\n");
    expect_ast("x := Input(\"ip.txt\");", "(assign x (call Input (string \"ip.txt\")))\n");
    expect_ast("x := f(1 + 2, -3, g(4));",
               "(assign x (call f (binary + Int(1) Int(2)) (unary - Int(3)) (call g Int(4))))\n");
    // Indexing.
    expect_ast("x := v[i];", "(assign x (index (variable v) (variable i)))\n");
    expect_ast("x := v[i + 1];", "(assign x (index (variable v) (binary + (variable i) Int(1))))\n");
    expect_ast("x := m[0, 1];", "(assign x (index (variable m) Int(0) Int(1)))\n");
    expect_ast("x := m[i][j];", "(assign x (index (index (variable m) (variable i)) (variable j)))\n");
    expect_ast("x := f(v)[0];", "(assign x (index (call f (variable v)) Int(0)))\n");
    expect_ast("x := (v)[0];", "(assign x (index (variable v) Int(0)))\n");
    expect_ast("x := v[m[0,0]] * 2;",
               "(assign x (binary * (index (variable v) (index (variable m) Int(0) Int(0))) Int(2)))\n");
    // Vector and matrix literals, with and without explicit sizes.
    expect_ast("x := [1];", "(assign x (vector[1] Int(1)))\n");
    expect_ast("x := [1, -2, a + b];", "(assign x (vector[3] Int(1) (unary - Int(2)) (binary + (variable a) (variable b))))\n");
    expect_ast("x := (3 [1,-2,-3]);", "(assign x (vector[3] Int(1) (unary - Int(2)) (unary - Int(3))))\n");
    expect_ast("x := (2 [1.0,2.0]);", "(assign x (vector[2] Float(1.0) Float(2.0)))\n");
    expect_ast("x := [[1,2],[3,4]];", "(assign x (matrix[2,2] Int(1) Int(2) Int(3) Int(4)))\n");
    expect_ast("x := [[1,2,3]];", "(assign x (matrix[1,3] Int(1) Int(2) Int(3)))\n");
    expect_ast("x := [[1],[2],[3]];", "(assign x (matrix[3,1] Int(1) Int(2) Int(3)))\n");
    expect_ast("x := (2,2[[1.,2.],[3.,4.]]);", "(assign x (matrix[2,2] Float(1.0) Float(2.0) Float(3.0) Float(4.0)))\n");
    expect_ast("x := ( 2 , 2 [ [ 1 , 2 ] , [ 3 , 4 ] ] );", "(assign x (matrix[2,2] Int(1) Int(2) Int(3) Int(4)))\n");
    expect_ast("x := [[a, f(b)], [-c, d[0]]];",
               "(assign x (matrix[2,2] (variable a) (call f (variable b)) (unary - (variable c)) (index (variable d) Int(0))))\n");
    expect_ast("x := [v, (2 [1,2])];", "(assign x (vector[2] (variable v) (vector[2] Int(1) Int(2))))\n");
    expect_ast("x := [1,2] + [3,4] * 2;",
               "(assign x (binary + (vector[2] Int(1) Int(2)) (binary * (vector[2] Int(3) Int(4)) Int(2))))\n");
    expect_ast("x := (3) * 2;", "(assign x (binary * Int(3) Int(2)))\n");
    expect_ast("x := (3)[0];", "(assign x (index Int(3) Int(0)))\n");
    // Nested mixture.
    expect_ast("x := -(a[i] * (b + c[i, j])) / f(g(h), [1, 2]) % 2 >= 0 && !done || flag;",
               "(assign x (binary || (binary && (binary >= (binary % (binary / (unary - (binary * (index (variable a) (variable i)) "
               "(binary + (variable b) (index (variable c) (variable i) (variable j))))) (call f (call g (variable h)) "
               "(vector[2] Int(1) Int(2)))) Int(2)) Int(0)) (unary ! (variable done))) (variable flag)))\n");
    // Literal edge cases.
    expect_ast("x := 9223372036854775807;", "(assign x Int(9223372036854775807))\n");
    expect_ast("x := 0;", "(assign x Int(0))\n");
    expect_ast("x := 007;", "(assign x Int(7))\n");
    expect_ast("x := 1e3;", "(assign x Float(1000.0))\n");
    expect_ast("x := 1.5E-3;", "(assign x Float(0.0015))\n");
    expect_ast("x := \"a \\\"quoted\\\" \\\\ string\";", "(assign x (string \"a \\\"quoted\\\" \\\\ string\"))\n");
}

void location_tests() {
    // Node locations: statement start, operator token, '[' of an index, name of a call.
    auto program = mydsl::parse("int a := 1;\n  b := a + f(a)[0];");
    check(program.size() == 2, "two statements parsed");
    const auto& assign = program.at(1);
    check(assign->location.line == 2 && assign->location.column == 3, "statement location");
    const auto& plus = assign->exprs.at(0);
    check(plus->op == "binary" && plus->location.line == 2 && plus->location.column == 10, "binary operator location");
    const auto& index = plus->args.at(1);
    check(index->op == "index" && index->location.column == 16, "index location is the '['");
    const auto& call = index->args.at(0);
    check(call->op == "call" && call->name == "f" && call->location.column == 12, "call location is the name");
    check(plus->args.at(0)->op == "variable" && plus->args.at(0)->location.column == 8, "variable location");
    // Declaration shape.
    const auto& decl = program.at(0);
    check(decl->op == "declare" && decl->declaration && decl->declaration->base == mydsl::Base::Int &&
          decl->declaration->rank == 0 && decl->name == "a" && decl->exprs.size() == 1, "declaration node");
    auto matrix = mydsl::parse("floatmatrix 2,3 m := [[1.,2.,3.],[4.,5.,6.]];").at(0);
    check(matrix->declaration->base == mydsl::Base::Float && matrix->declaration->rank == 2 &&
          matrix->declaration->rows == 2 && matrix->declaration->cols == 3, "matrix declaration type");
    const auto& literal = matrix->exprs.at(0);
    check(literal->op == "matrix" && literal->annotation && literal->annotation->rank == 2 &&
          literal->annotation->rows == 2 && literal->annotation->cols == 3 && literal->args.size() == 6,
          "matrix literal shape");
    auto vec = mydsl::parse("intvector 2 v := (2 [1,2]);").at(0);
    check(vec->declaration->rank == 1 && vec->declaration->rows == 2 && vec->exprs.at(0)->annotation->rank == 1 &&
          vec->exprs.at(0)->annotation->rows == 2, "vector declaration and literal shape");
    auto lit = mydsl::parse("x := 2.5;").at(0)->exprs.at(0);
    check(lit->op == "literal" && lit->literal.type.base == mydsl::Base::Float && lit->literal.floats.at(0) == 2.5,
          "float literal value");
}

void error_tests() {
    // Missing pieces.
    expect_error("int a := 1", 1, 11, "Expected ';' to end the declaration of 'a' but found end of input");
    expect_error("int a := ;", 1, 10, "Expected an expression but found ';'");
    expect_error("int a := 1 +;", 1, 13, "Expected an expression but found ';'");
    expect_error("int a := 1 *", 1, 13, "Expected an expression but found end of input");
    expect_error("int a;", 1, 6, "Expected ':=' after variable name 'a'");
    expect_error("int := 1;", 1, 5, "Expected a variable name after type 'int'");
    expect_error("x := ;", 1, 6, "Expected an expression");
    expect_error("x := 1 == ;", 1, 11, "Expected an expression but found ';'");
    expect_error("x := !;", 1, 7, "Expected an expression but found ';'");
    expect_error("x := a &&", 1, 10, "Expected an expression but found end of input");
    expect_error("Print();", 1, 7, "Print needs exactly one argument");
    expect_error("Print(1, 2);", 1, 8, "Print takes exactly one argument");
    expect_error("Print 1;", 1, 7, "Expected '(' after 'Print' but found '1'");
    // Brackets and parentheses.
    expect_error("x := (1;", 1, 8, "Expected ')' to close the '(' opened at line 1, column 6 but found ';'");
    expect_error("x := (1 + 2;", 1, 12, "Expected ')' to close the '(' opened at line 1, column 6");
    expect_error("x := f(1;", 1, 9, "Expected ')' to close the argument list of 'f' opened at line 1, column 7");
    expect_error("x := f(1,);", 1, 10, "Expected an expression but found ')'");
    expect_error("x := a[1;", 1, 9, "Expected ']' to close the index opened at line 1, column 7");
    expect_error("x := a[];", 1, 8, "Expected an expression but found ']'");
    expect_error("x := v[1,2,3];", 1, 11, "Indexing takes at most two indices");
    expect_error("v[1,2,3] := 0;", 1, 6, "Indexing takes at most two indices");
    expect_error("x := [1,2;", 1, 10, "Expected ']' to close the vector literal opened at line 1, column 6");
    expect_error("x := [1,2,];", 1, 11, "Expected an expression but found ']'");
    expect_error("x := 1 + 2);", 1, 11, "Expected ';' to end the assignment to 'x' but found ')'");
    expect_error("Print((1+2);", 1, 12, "Expected ')' to close the Print argument opened at line 1, column 6 but found ';'");
    expect_error("{ int a := 1;", 1, 14, "Expected '}' to close the block opened at line 1, column 1 but found end of input");
    expect_error("if (a) then {\n  Print(a);\n", 3, 1, "Expected '}' to close the block opened at line 1, column 13");
    expect_error("if (a) then { Print(a); }", 1, 26, "Expected ';' to end the if statement but found end of input");
    expect_error("while (x) { }", 1, 14, "Expected ';' to end the while loop");
    expect_error("{ x := 1; }", 1, 12, "Expected ';' to end the block");
    expect_error("};", 1, 1, "Unexpected '}' without a matching '{'");
    expect_error("x := 1 ]", 1, 8, "Expected ';' to end the assignment to 'x' but found ']'");
    // Control flow shapes.
    expect_error("if (a) { };", 1, 8, "Expected 'then' after the if condition but found '{'");
    expect_error("if a then { };", 1, 4, "Expected '(' after 'if' but found 'a'");
    expect_error("if (a then {};", 1, 7, "Expected ')' to close the if condition opened at line 1, column 4 but found 'then'");
    expect_error("if (a) then Print(a);", 1, 13, "Expected '{' after 'then'");
    expect_error("if (a) then {} else Print(a);", 1, 21, "Expected '{' after 'else'");
    expect_error("if (a) then {} else if (b) then {};", 1, 21, "'else if' is not supported");
    expect_error("if (a) then {} {};", 1, 16, "Expected ';' to end the if statement but found '{'");
    expect_error("else { };", 1, 1, "Unexpected 'else' without a matching 'if'");
    expect_error("then { };", 1, 1, "Unexpected 'then'");
    expect_error("while x < 3 { };", 1, 7, "Expected '(' after 'while' but found 'x'");
    expect_error("while (x) x := 1;", 1, 11, "Expected '{' after the while condition");
    expect_error("for (i := 0; i < 3; 1) { };", 1, 12, "Expected ',' after the initial value of 'i'");
    expect_error("for (i := 0, i < 3) { };", 1, 19, "Expected ',' after the loop condition");
    expect_error("for (i := 0, i < 3, 1, 2) { };", 1, 22, "Expected ')' to close the for loop header opened at line 1, column 5");
    expect_error("for (int i := 0, i < 3, 1) { };", 1, 6, "The for loop variable must not have a type");
    expect_error("for (i = 0, i < 3, 1) { };", 1, 8, "Expected ':=' after the loop variable 'i'");
    expect_error("for i := 0, i < 3, 1 { };", 1, 5, "Expected '(' after 'for'");
    expect_error("for (i := 0, i < 3, 1) Print(i);", 1, 24, "Expected '{' after the for loop header");
    // Wrong or unexpected tokens.
    expect_error("a = 1;", 1, 3, "Expected ':=' after 'a' but found '='; MyDSL uses ':=' for assignment");
    expect_error("int a = 1;", 1, 7, "Expected ':=' after variable name 'a' but found '='");
    expect_error("x := 1 2;", 1, 8, "Expected ';' to end the assignment to 'x' but found '2'");
    expect_error("x := a b;", 1, 8, "Expected ';' to end the assignment to 'x' but found 'b'");
    expect_error("x := a.b;", 1, 7, "but found '.'");
    expect_error("x := 3 $ 4;", 1, 8, "Unexpected character '$'", "Lexing");
    expect_error("int a := \"open", 1, 10, "Unterminated string", "Lexing");
    expect_error("3;", 1, 1, "Expected a statement (declaration, assignment, Print, error, block, if, while, for, function or import) but found '3'");
    expect_error("{ 3; };", 1, 3, "Expected a statement (declaration, assignment, Print, error, block, if, while or for) but found '3'");
    expect_error("(x) := 1;", 1, 1, "Expected a statement");
    expect_error("int a := 1; ;", 1, 13, "Expected a statement");
    expect_error("abs(a);", 1, 4, "A call to 'abs' cannot be used as a statement");
    expect_error("x := Print(1);", 1, 6, "'Print' is a statement and cannot be used inside an expression");
    expect_error("x := if;", 1, 6, "Expected an expression but found keyword 'if'");
    expect_error("x := int;", 1, 6, "Expected an expression but found keyword 'int'");
    expect_error("x := \"s\" \"t\";", 1, 10, "but found string literal \"t\"");
    expect_error("int if := 3;", 1, 5, "Cannot use keyword 'if' as a variable name");
    expect_error("int true := 3;", 1, 5, "Cannot use keyword 'true' as a variable name");
    expect_error("for (while := 0, 1, 1) {};", 1, 6, "Cannot use keyword 'while' as a variable name");
    expect_error("x := a + -+3;", 1, 11, "Unary '+' is not supported");
    expect_error("x := +3;", 1, 6, "Unary '+' is not supported");
    expect_error("x := 1 + * 2;", 1, 10, "Expected an expression but found '*'");
    expect_error("x := a[1] 2;", 1, 11, "Expected ';' to end the assignment to 'x' but found '2'");
    expect_error("boolvector 2 b := [true, false];", 1, 1, "Unknown type 'boolvector' in declaration");
    expect_error("Int a := 1;", 1, 1, "Unknown type 'Int' in declaration");
    expect_error("integer a := 1;", 1, 1, "Unknown type 'integer'");
    expect_error("x y := 1;", 1, 1, "Unknown type 'x'");
    // Types.
    expect_error("intvector v := [1];", 1, 11, "Expected a positive integer as the vector size after 'intvector' but found 'v'");
    expect_error("intvector 0 v := [1];", 1, 11, "Expected a positive integer as the vector size after 'intvector' but found '0'");
    expect_error("intvector -1 v := [1];", 1, 11, "Expected a positive integer as the vector size");
    expect_error("intvector 2.0 v := [1];", 1, 11, "Expected a positive integer as the vector size");
    expect_error("floatmatrix 2 m := [[1.]];", 1, 15, "Expected ',' between the row and column counts of 'floatmatrix' but found 'm'");
    expect_error("floatmatrix 2, m := [[1.]];", 1, 16, "Expected a positive integer as the column count after 'floatmatrix'");
    // Vector and matrix literal shapes.
    expect_error("x := [];", 1, 7, "Vector literal must contain at least one element");
    expect_error("x := [[]];", 1, 8, "Matrix row 1 must contain at least one element");
    expect_error("x := [[1,2],[3]];", 1, 13, "Matrix row 2 has 1 element(s) but row 1 has 2");
    expect_error("x := [[1],[2,3]];", 1, 11, "Matrix row 2 has 2 element(s) but row 1 has 1");
    expect_error("x := [[1,2],3];", 1, 13, "Expected '[' to start matrix row 2 but found '3'");
    expect_error("x := [[1,2],[3,4];", 1, 18, "Expected ']' to close the matrix literal opened at line 1, column 6");
    expect_error("x := [[1,2;", 1, 11, "Expected ']' to close matrix row 1 opened at line 1, column 7");
    expect_error("x := (3 [1,2]);", 1, 7, "Vector literal declares size 3 but has 2 element(s)");
    expect_error("x := (2 [1,2,3]);", 1, 7, "Vector literal declares size 2 but has 3 element(s)");
    expect_error("x := (2,2 [[1,2],[3,4],[5,6]]);", 1, 7, "Matrix literal declares size 2,2 but has 3 row(s) of 2 element(s)");
    expect_error("x := (2,3 [[1,2],[3,4]]);", 1, 7, "Matrix literal declares size 2,3 but has 2 row(s) of 2 element(s)");
    expect_error("x := (2,2 [1,2,3,4]);", 1, 11, "Size 2,2 describes a matrix but a vector literal follows");
    expect_error("x := (2 [[1],[2]]);", 1, 9, "Size 2 describes a vector but a matrix literal follows");
    expect_error("x := (2 [1,2];", 1, 14, "Expected ')' to close the sized literal opened at line 1, column 6 but found ';'");
    expect_error("x := (0 [1]);", 1, 7, "Expected a positive integer as the literal size but found '0'");
    // Literal ranges.
    expect_error("x := 99999999999999999999;", 1, 6, "Integer literal '99999999999999999999' is out of range");
    expect_error("x := 9223372036854775808;", 1, 6, "is out of range");
    expect_error("x := 1e999;", 1, 6, "Float literal '1e999' is out of range");
    expect_ast("x := 1e-999;", "(assign x Float(0.0))\n");
    expect_ast("x := 1e308;", "(assign x Float(1e+308))\n");
    expect_error("x := 1e;", 1, 6, "Invalid exponent", "Lexing");
    // Error positions on later lines and after comments.
    expect_error("int a := 1;\nint b := 2;\n  c := ;", 3, 8, "Expected an expression");
    expect_error("// comment\n/* block\ncomment */ int a :=\n\n   * 1;", 5, 4, "Expected an expression but found '*'");
    expect_error("if (a) then {\n  if (b) then {\n    x := 1\n  };\n};", 4, 3, "Expected ';' to end the assignment to 'x' but found '}'");
    // Only the first error is reported.
    expect_error("x := ;\ny := ;", 1, 6, "Expected an expression");
}

void nesting_tests() {
    const std::string deep_parens = "x := " + std::string(100000, '(') + "1" + std::string(100000, ')') + ";";
    expect_error(deep_parens, 1, 261, "nesting is too deep");
    std::string deep_blocks;
    for (int i = 0; i < 100000; ++i) deep_blocks += '{';
    for (int i = 0; i < 100000; ++i) deep_blocks += "};";
    expect_error(deep_blocks, 1, 257, "nesting is too deep");
    expect_error("x := " + std::string(100000, '-') + "1;", 1, 260, "nesting is too deep");
    std::string chain = "x := 1";
    for (int i = 0; i < 100000; ++i) chain += "+1";
    expect_error(chain, 1, 515, "nesting is too deep");
    std::string indexes = "x := a";
    for (int i = 0; i < 100000; ++i) indexes += "[0]";
    expect_error(indexes, 1, 767, "nesting is too deep");
    // Wide inputs are fine: long programs, many elements, many arguments, moderately deep trees.
    std::string many_statements;
    for (int i = 0; i < 20000; ++i) many_statements += "x := 1;";
    check(mydsl::parse(many_statements).size() == 20000, "20000 statements");
    std::string wide = "x := [1";
    for (int i = 0; i < 20000; ++i) wide += ",1";
    check(mydsl::parse(wide + "];").at(0)->exprs.at(0)->args.size() == 20001, "20001-element vector");
    check(!mydsl::parse("x := " + std::string(200, '(') + "1" + std::string(200, ')') + ";").empty(), "200 nested parentheses");
    std::string sum = "x := 1";
    for (int i = 0; i < 200; ++i) sum += "+1";
    check(!mydsl::parse(sum + ";").empty(), "200-term sum");
}

void example_tests() {
    // Every example program in inputs/ must parse (run from the project directory).
    const char* files[] = {"Check_Sorted", "arithmetic_op", "binary_search", "check_even", "eigen_values",
                           "error_input1", "error_input2", "factorial", "fibonacci", "float_input", "for_loop", "functions",
                           "gaussian_elimination", "gcd", "int_vector_test", "logical_tests", "matrix_indexing",
                           "matrix_test", "prime", "scoping_test", "search", "string_functions", "string_input", "strings", "t", "test", "vector_functions", "while_loop"};
    for (const char* name : files) {
        const std::string path = std::string("inputs/") + name + ".txt";
        std::ifstream file(path);
        if (!file) { check(false, "open " + path); continue; }
        std::string text{std::istreambuf_iterator<char>(file), {}};
        try { check(!mydsl::parse(text).empty(), "parse " + path); }
        catch (const std::exception& e) { check(false, "parse " + path, e.what()); }
    }
}

// Strict JSON syntax check (RFC 8259 grammar, no extensions) so that the browser view's
// contract can be verified without a JSON library.
struct JsonChecker {
    const std::string& text;
    std::size_t i = 0;
    bool ok = true;
    void skip() { while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i; }
    bool eat(char c) { skip(); if (i < text.size() && text[i] == c) { ++i; return true; } return false; }
    bool literal(const char* word) {
        const std::size_t n = std::strlen(word);
        if (text.compare(i, n, word) != 0) return false;
        i += n; return true;
    }
    bool string() {
        if (!eat('"')) return false;
        while (i < text.size() && text[i] != '"') {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            if (c < 0x20) return false; // control characters must be escaped
            if (c == '\\') {
                ++i;
                if (i >= text.size()) return false;
                if (text[i] == 'u') {
                    for (int k = 1; k <= 4; ++k)
                        if (i + k >= text.size() || !std::isxdigit(static_cast<unsigned char>(text[i + k]))) return false;
                    i += 4;
                } else if (std::string("\"\\/bfnrt").find(text[i]) == std::string::npos) return false;
            }
            ++i;
        }
        return eat('"');
    }
    bool number() {
        const std::size_t start = i;
        if (i < text.size() && text[i] == '-') ++i;
        if (i >= text.size() || !std::isdigit(static_cast<unsigned char>(text[i]))) return false;
        if (text[i] == '0') ++i; else while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
        if (i < text.size() && text[i] == '.') { ++i; if (!std::isdigit(static_cast<unsigned char>(text[i]))) return false;
            while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) ++i; }
        if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) { ++i; if (text[i] == '+' || text[i] == '-') ++i;
            if (!std::isdigit(static_cast<unsigned char>(text[i]))) return false;
            while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) ++i; }
        return i > start;
    }
    bool value() {
        skip();
        if (i >= text.size()) return false;
        switch (text[i]) {
        case '{': ++i; if (eat('}')) return true;
            do { if (!string() || !eat(':') || !value()) return false; } while (eat(','));
            return eat('}');
        case '[': ++i; if (eat(']')) return true;
            do { if (!value()) return false; } while (eat(','));
            return eat(']');
        case '"': return string();
        case 't': return literal("true");
        case 'f': return literal("false");
        case 'n': return literal("null");
        default: return number();
        }
    }
    bool document() { if (!value()) return false; skip(); return i == text.size(); }
};

bool valid_json(const std::string& text) { JsonChecker checker{text}; return checker.document(); }

void json_tests() {
    // The validator itself.
    check(valid_json("{\"a\":[1,-2.5e3,\"x\\n\\u00e9\",true,null,{}]}"), "json validator accepts valid json");
    check(!valid_json("{\"a\":1,}") && !valid_json("[1 2]") && !valid_json("\"\n\"") && !valid_json("{a:1}") && !valid_json("01"),
          "json validator rejects invalid json");

    const std::string ok = mydsl::debug_json("int a := 1 + b[2];\nPrint(a);");
    check(valid_json(ok), "debug_json is valid json", ok);
    check(ok.find("\"error\":{\"stage\":\"Type\",\"line\":1,\"column\":14,\"message\":\"Undeclared variable 'b'\"}") != std::string::npos,
          "syntactically valid program with an undeclared variable: type error, ast kept", ok);
    check(valid_json(mydsl::debug_json("int a := 1;")) && mydsl::debug_json("int a := 1;").find("\"error\":null") != std::string::npos, "valid program: error is null");
    check(ok.find("{\"kind\":\"word\",\"text\":\"int\",\"begin\":{\"line\":1,\"column\":1},\"end\":{\"line\":1,\"column\":4}}") != std::string::npos,
          "token json with begin/end", ok);
    check(ok.find("{\"kind\":\"end\",\"text\":\"<eof>\",\"begin\":{\"line\":2,\"column\":10}") != std::string::npos, "end token json", ok);
    check(ok.find("\"node\":\"statement\",\"op\":\"declare\",\"name\":\"a\",\"declaration\":\"int\",\"location\":{\"line\":1,\"column\":1},"
                  "\"begin\":{\"line\":1,\"column\":1},\"end\":{\"line\":1,\"column\":19},\"exprs\":[") != std::string::npos,
          "statement json with span through ';'", ok);
    check(ok.find("{\"node\":\"expression\",\"op\":\"binary\",\"name\":\"+\",\"location\":{\"line\":1,\"column\":12},"
                  "\"begin\":{\"line\":1,\"column\":10},\"end\":{\"line\":1,\"column\":18},\"args\":[") != std::string::npos,
          "binary json: location is the operator, span covers both operands", ok);
    check(ok.find("{\"node\":\"expression\",\"op\":\"literal\",\"value\":\"Int(1)\",") != std::string::npos, "literal json has value", ok);
    check(ok.find("\"op\":\"index\",\"location\":{\"line\":1,\"column\":15},\"begin\":{\"line\":1,\"column\":14},\"end\":{\"line\":1,\"column\":18}") != std::string::npos,
          "index json", ok);
    check(ok.find("\"op\":\"print\",\"location\":{\"line\":2,\"column\":1},\"begin\":{\"line\":2,\"column\":1},\"end\":{\"line\":2,\"column\":10}") != std::string::npos,
          "second statement json", ok);

    const std::string shapes = mydsl::debug_json("x := (2,2 [[1,2],[3,4]]);");
    check(valid_json(shapes) && shapes.find("\"op\":\"matrix\",\"shape\":{\"rank\":2,\"rows\":2,\"cols\":2},\"location\":{\"line\":1,\"column\":11},"
                                            "\"begin\":{\"line\":1,\"column\":6},\"end\":{\"line\":1,\"column\":25}") != std::string::npos,
          "matrix literal json: shape and span including the sizing parentheses", shapes);

    const std::string bad = mydsl::debug_json("int a := 1;\nint b := (a +\n  2;");
    check(valid_json(bad), "parse error json is valid", bad);
    check(bad.find("\"ast\":null") != std::string::npos, "parse error: ast is null");
    check(bad.find("\"error\":{\"stage\":\"Parse\",\"line\":3,\"column\":4,\"message\":\"Expected ')' to close the '(' opened at line 2, column 10 but found ';'\"}") != std::string::npos,
          "parse error json", bad);
    check(bad.find("\"tokens\":[{\"kind\":\"word\"") != std::string::npos, "parse error: tokens are still reported");

    const std::string lexbad = mydsl::debug_json("x := 3 $ 4;");
    check(valid_json(lexbad) && lexbad.find("\"tokens\":[],\"ast\":null,\"error\":{\"stage\":\"Lexing\",\"line\":1,\"column\":8,") != std::string::npos,
          "lexing error json", lexbad);

    const std::string typed = mydsl::debug_json("int a := 1 + 2;");
    check(valid_json(typed) && typed.find("\"op\":\"binary\",\"name\":\"+\",\"type\":\"int\",") != std::string::npos &&
          typed.find("\"op\":\"literal\",\"type\":\"int\",\"value\":\"Int(1)\"") != std::string::npos, "expression types in json", typed);
    const std::string typebad = mydsl::debug_json("int a := 1;\na := 1.5;");
    check(valid_json(typebad) && typebad.find("\"ast\":[{\"node\":\"statement\",\"op\":\"declare\"") != std::string::npos &&
          typebad.find("\"type\":") == std::string::npos &&
          typebad.find("\"error\":{\"stage\":\"Type\",\"line\":2,\"column\":1,\"message\":\"Cannot assign a value of type float to 'a' of type int") != std::string::npos,
          "type error json keeps the ast and omits types", typebad);
    const std::string empty = mydsl::debug_json("");
    check(valid_json(empty) && empty.find("\"ast\":[],\"error\":null") != std::string::npos, "empty program json", empty);
    const std::string function = mydsl::debug_json("function int sq(int x, float y) { return x; };\nPrint(sq(2, 1.0));");
    check(valid_json(function) && function.find("\"error\":null") != std::string::npos, "function json is valid and typed", function);
    check(function.find("{\"node\":\"statement\",\"op\":\"function\",\"name\":\"sq\",\"declaration\":\"int\",\"location\":{\"line\":1,\"column\":1},"
                        "\"begin\":{\"line\":1,\"column\":1},\"end\":{\"line\":1,\"column\":47},\"params\":["
                        "{\"node\":\"parameter\",\"op\":\"parameter\",\"name\":\"x\",\"declaration\":\"int\",\"location\":{\"line\":1,\"column\":17},\"begin\":{\"line\":1,\"column\":17},\"end\":{\"line\":1,\"column\":22}},"
                        "{\"node\":\"parameter\",\"op\":\"parameter\",\"name\":\"y\",\"declaration\":\"float\",\"location\":{\"line\":1,\"column\":24},\"begin\":{\"line\":1,\"column\":24},\"end\":{\"line\":1,\"column\":31}}],"
                        "\"exprs\":[],\"body\":[{\"node\":\"statement\",\"op\":\"block\"") != std::string::npos, "function node json with params", function);
    check(function.find("{\"node\":\"statement\",\"op\":\"return\",\"location\":{\"line\":1,\"column\":35},\"begin\":{\"line\":1,\"column\":35},\"end\":{\"line\":1,\"column\":44},\"exprs\":[{\"node\":\"expression\",\"op\":\"variable\",\"name\":\"x\",\"type\":\"int\"") != std::string::npos, "return node json", function);
    check(function.find("{\"node\":\"expression\",\"op\":\"call\",\"name\":\"sq\",\"type\":\"int\",") != std::string::npos, "user call json has the return type", function);
    check(mydsl::debug_json("int a := 1;").find("\"params\"") == std::string::npos, "params only appear on function nodes");
    const std::string missing = mydsl::debug_json("function int f(int x) { Print(x); };");
    check(valid_json(missing) && missing.find("\"error\":{\"stage\":\"Type\",\"line\":1,\"column\":1,\"message\":\"Function 'f' must return int on every path") != std::string::npos &&
          missing.find("\"op\":\"function\"") != std::string::npos, "missing return: type error json keeps the function node", missing);
    const std::string dup = mydsl::debug_json("function int f(int x, int x) { return x; };");
    check(dup.find("\"error\":{\"stage\":\"Type\",\"line\":1,\"column\":23,\"message\":\"Function 'f' declares parameter 'x' twice") != std::string::npos, "duplicate parameter error points at the parameter", dup);
    const std::string stray = mydsl::debug_json("return 1;");
    check(stray.find("\"ast\":null,\"error\":{\"stage\":\"Parse\",\"line\":1,\"column\":1,\"message\":\"'return' can only be used inside a function body\"}") != std::string::npos, "stray return parse error json", stray);

    // "functions": the callable functions with their signatures, for the editor's completion -
    // the program's own, and those of imported modules with their file (in dependency order).
    check(function.find("\"functions\":[{\"name\":\"sq\",\"params\":[{\"name\":\"x\",\"type\":\"int\"},{\"name\":\"y\",\"type\":\"float\"}],\"returns\":\"int\",\"line\":1,\"column\":1}]}") != std::string::npos,
          "functions list of the main program", function);
    check(mydsl::debug_json("int a := 1;").find("\"functions\":[]}") != std::string::npos && bad.find("\"functions\":[]}") != std::string::npos,
          "no functions without definitions or without an AST");
    check(missing.find("\"functions\":[{\"name\":\"f\",\"params\":[{\"name\":\"x\",\"type\":\"int\"}],\"returns\":\"int\",\"line\":1,\"column\":1}]}") != std::string::npos,
          "the functions of a program with a type error are still listed", missing);
    const std::string imported = mydsl::debug_json("import \"tests/modules/lib/strings.mydsl\";\nfunction int own() { return 1; };\nPrint(greet(\"x\"));");
    check(valid_json(imported) && imported.find("\"error\":null") != std::string::npos, "the import test program type-checks", imported);
    const auto greet = imported.find("{\"name\":\"greet\",\"params\":[{\"name\":\"name\",\"type\":\"string\"}],\"returns\":\"string\",\"line\":");
    const auto own = imported.find("{\"name\":\"own\",\"params\":[],\"returns\":\"int\",\"line\":2,\"column\":1}]}");
    check(greet != std::string::npos && imported.find("\"file\":\"tests/modules/lib/strings.mydsl\"}", greet) != std::string::npos && own != std::string::npos && greet < own,
          "imported functions come with their file, before the program's own", imported);
    check(mydsl::debug_json("import \"tests/modules/lib/strings.mydsl\";\nint a := 1.5;").find("{\"name\":\"greet\"") != std::string::npos,
          "imported functions are listed although the main program has a type error");
    const std::string unresolved = mydsl::debug_json("import \"nowhere.mydsl\";\nfunction int own() { return 1; };");
    check(unresolved.find("\"stage\":\"Import\"") != std::string::npos && unresolved.find("\"functions\":[{\"name\":\"own\"") != std::string::npos,
          "a failed import still lists the program's own functions", unresolved);

    // language_json: the parser's keywords and the checker's builtins, each documented.
    const std::string language = mydsl::language_json();
    check(valid_json(language) && language.find("{\"keywords\":[") == 0 && language.find("],\"builtins\":[") != std::string::npos, "language_json is valid json", language);
    for (const std::string& keyword : mydsl::keywords())
        check(language.find("{\"name\":\"" + keyword + "\",\"kind\":\"") != std::string::npos && language.find("{\"name\":\"" + keyword + "\",\"kind\":\"") < language.find("\"builtins\""),
              "keyword '" + keyword + "' is listed");
    for (const std::string& builtin : mydsl::builtin_functions())
        check(language.find("{\"name\":\"" + builtin + "\",\"signature\":\"" + builtin + "(") != std::string::npos, "builtin '" + builtin + "' is listed with a signature");
    check(language.find("\"description\":\"\"") == std::string::npos && language.find("\"detail\":\"\"") == std::string::npos, "every entry has a detail and a description", language);
    check(language.find("{\"name\":\"while\",\"kind\":\"statement\",\"detail\":\"while (condition) { ... };\",\"description\":\"Loop while the bool condition holds; the statement ends with ';'.\"}") != std::string::npos, "while entry", language);
    check(language.find("{\"name\":\"intvector\",\"kind\":\"type\",") != std::string::npos && language.find("{\"name\":\"true\",\"kind\":\"constant\",") != std::string::npos, "kinds of types and constants");
    check(language.find("{\"name\":\"substring\",\"signature\":\"substring(s, start, count)\",\"detail\":\"string, int, int -> string\",") != std::string::npos, "substring entry", language);
    check(mydsl::keywords().size() == 20 && mydsl::builtin_functions().size() == 15 && mydsl::keywords().count("Print") && mydsl::builtin_functions()[4] == "Input", "vocabulary sizes");
    check(mydsl::keywords().count("abs") == 0 && std::find(mydsl::builtin_functions().begin(), mydsl::builtin_functions().end(), "Print") == mydsl::builtin_functions().end(), "keywords and builtins are disjoint");

    // The file overlay: texts that stand in for files on disk while loading imports (the
    // browser's unsaved tabs). Keyed by canonical path; a file must still exist on disk.
    {
        namespace fs = std::filesystem;
        const std::string key = fs::canonical("tests/modules/lib/strings.mydsl").generic_string();
        const std::string program = "import \"tests/modules/lib/strings.mydsl\";\nPrint(greet(\"x\"));";
        mydsl::FileOverlay overlay{{key, "function string greet(string who) { return \"hi \" + who + \"!\"; };\n"}};
        const std::string edited = mydsl::run_json(program, "", {}, "", overlay);
        check(edited.find("\"output\":\"String(\\\"hi x!\\\")\\n\",\"error\":null") != std::string::npos, "run_json uses the overlay text of an imported file", edited);
        check(mydsl::run_json(program, "", {}, "", {}).find("\"output\":\"String(\\\"hi x!") == std::string::npos, "without the overlay the file on disk is used");
        const std::string broken = mydsl::debug_json(program, "", {{key, "function string greet(string who) { return who }\n"}});
        check(broken.find("\"error\":{\"stage\":\"Parse\",\"line\":1,\"column\":48,\"message\":\"Expected ';' to end the return statement but found '}'\",\"file\":\"tests/modules/lib/strings.mydsl\",\"imports\":[{\"file\":\"\",\"line\":1,\"column\":1}]}") != std::string::npos,
              "a syntax error in the overlay text is reported in that file with the import chain", broken);
        const std::string typed = mydsl::debug_json(program, "", {{key, "function int greet(string who) { return 1; };\n"}});
        check(typed.find("\"functions\":[{\"name\":\"greet\",\"params\":[{\"name\":\"who\",\"type\":\"string\"}],\"returns\":\"int\",\"line\":1,\"column\":1,\"file\":\"tests/modules/lib/strings.mydsl\"}]") != std::string::npos,
              "the functions list reflects the overlay text", typed);
        check(mydsl::debug_json("import \"tests/modules/lib/nowhere.mydsl\";", "", {{fs::absolute("tests/modules/lib/nowhere.mydsl").lexically_normal().generic_string(), "x"}}).find("\"stage\":\"Import\"") != std::string::npos,
              "an overlay entry cannot stand in for a file that does not exist on disk");
        const mydsl::Modules modules = mydsl::load_modules(program, "", overlay);
        check(modules.modules.size() == 2 && modules.modules[0].source.find("hi ") != std::string::npos && modules.modules[0].display == "tests/modules/lib/strings.mydsl", "load_modules keeps the overlay text as the module's source");
    }

    // The project file API (src/project.cpp): listing, reading, writing, and every refusal.
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / ("mydsl-project-test-" + std::to_string(::getpid()));
        fs::create_directories(dir / "lib");
        fs::create_directories(dir / ".git");
        fs::create_directories(dir / "empty");
        auto write = [](const fs::path& p, const std::string& s) { std::ofstream f(p); f << s; };
        write(dir / "main.mydsl", "Print(1);\n");
        write(dir / "lib" / "a.mydsl", "function int f() { return 1; };\n");
        write(dir / "data.txt", "42\n");
        write(dir / "notes.md", "# no\n");
        write(dir / ".git" / "x.mydsl", "Print(0);\n");
        const fs::path outside = fs::temp_directory_path() / ("mydsl-outside-" + std::to_string(::getpid()) + ".mydsl");
        write(outside, "Print(9);\n");
        std::error_code ignored;
        fs::create_symlink(outside, dir / "lib" / "link.mydsl", ignored);
        fs::create_directory_symlink(fs::temp_directory_path(), dir / "tmplink", ignored);
        const std::string root = dir.string();
        const std::string tree = mydsl::project_tree_json(root);
        check(valid_json(tree) && tree.find("\"root\":\"" + fs::canonical(dir).generic_string() + "\"") != std::string::npos, "tree names the canonical root", tree);
        check(tree.find("\"entries\":[{\"path\":\"data.txt\",\"dir\":false,\"writable\":true},{\"path\":\"lib\",\"dir\":true},{\"path\":\"lib/a.mydsl\",\"dir\":false,\"writable\":true},{\"path\":\"main.mydsl\",\"dir\":false,\"writable\":true}]") != std::string::npos,
              "tree lists .mydsl/.txt files and their directories only: no hidden dir, no .md, no empty dir, no symlinks", tree);
        check(mydsl::project_file_json(root, "lib/a.mydsl") == "{\"path\":\"lib/a.mydsl\",\"content\":\"function int f() { return 1; };\\n\",\"writable\":true}\n", "reading a file", mydsl::project_file_json(root, "lib/a.mydsl"));
        check(mydsl::project_path(root, "lib/a.mydsl", true) == (fs::canonical(dir) / "lib" / "a.mydsl").string(), "project_path resolves inside the root");
        auto refused = [&](const std::string& relative, bool must_exist, int status, const std::string& text) {
            try { mydsl::project_path(root, relative, must_exist); check(false, "refused: " + relative); }
            catch (const mydsl::ProjectError& e) { check(e.status == status && e.message.find(text) != std::string::npos, "refused '" + relative + "' with " + std::to_string(status) + " " + text, std::to_string(e.status) + " " + e.message); }
        };
        refused("", true, 400, "Missing file path");
        refused("/etc/passwd", true, 400, "not absolute");
        refused("../" + outside.filename().string(), true, 400, "'..'");
        refused("lib/../../x.mydsl", true, 400, "'..'");
        refused("lib//a.mydsl", true, 400, "empty");
        refused("./main.mydsl", true, 400, "'.'");
        refused("lib\\a.mydsl", true, 400, "invalid character");
        refused("a\nb.mydsl", true, 400, "invalid character");
        refused("lib/link.mydsl", true, 403, "leaves the project directory");
        refused("tmplink/" + outside.filename().string(), true, 403, "leaves the project directory");
        refused("notes.md", true, 403, "Only .mydsl and .txt");
        refused(".git/x.mydsl", true, 403, "Hidden entries");
        refused("build/x.mydsl", false, 403, "Hidden entries");
        refused("nope.mydsl", true, 404, "No such file");
        refused("lib", true, 403, "Only .mydsl");
        refused("nodir/new.mydsl", false, 404, "No such directory");
        refused("lib/new.py", false, 403, "Only .mydsl");
        check(mydsl::project_path(root, "lib/new.mydsl", false) == (fs::canonical(dir) / "lib" / "new.mydsl").string(), "a new file in an existing directory is allowed");
        // Writing: atomic replace keeps the mode; a new file; refusals for links and read-only files.
        fs::permissions(dir / "main.mydsl", fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read | fs::perms::others_read | fs::perms::others_exec);
        mydsl::write_project_file(root, "main.mydsl", "Print(2);\n");
        check(mydsl::project_file_json(root, "main.mydsl").find("\"content\":\"Print(2);\\n\"") != std::string::npos, "write replaces the content");
        check((fs::status(dir / "main.mydsl").permissions() & fs::perms::others_exec) != fs::perms::none, "the file's mode is kept");
        check(!fs::exists(dir / "main.mydsl.mydsl-saving"), "no temporary file left");
        mydsl::write_project_file(root, "lib/new.mydsl", "");
        check(fs::is_regular_file(dir / "lib" / "new.mydsl") && mydsl::project_tree_json(root).find("lib/new.mydsl") != std::string::npos, "a new empty file can be created and is listed");
        try { mydsl::write_project_file(root, "lib/link.mydsl", "x"); check(false, "writing through a link"); }
        catch (const mydsl::ProjectError& e) { check(e.status == 403, "writing through a link outside is refused: " + e.message); }
        {
            std::ifstream f(outside); std::string s{std::istreambuf_iterator<char>(f), {}};
            check(s == "Print(9);\n", "the file outside is untouched");
        }
        if (::geteuid() != 0) { // root may write anywhere
            fs::permissions(dir / "data.txt", fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read);
            try { mydsl::write_project_file(root, "data.txt", "x"); check(false, "writing a read-only file"); }
            catch (const mydsl::ProjectError& e) { check(e.status == 403 && e.message.find("read-only") != std::string::npos, "a read-only file is refused with the reason: " + e.message); }
            check(mydsl::project_tree_json(root).find("{\"path\":\"data.txt\",\"dir\":false,\"writable\":false}") != std::string::npos, "the tree reports it as not writable");
            check(mydsl::project_file_json(root, "data.txt").find("\"writable\":false") != std::string::npos, "reading reports it as not writable");
        }
        try { mydsl::write_project_file(root, "big.mydsl", std::string((1u << 20) + 1, 'x')); check(false, "a file over 1 MiB"); }
        catch (const mydsl::ProjectError& e) { check(e.status == 413, "a file over 1 MiB is refused"); }
        check(!fs::exists(dir / "big.mydsl"), "nothing written for the refused file");
        fs::permissions(dir / "data.txt", fs::perms::owner_all, ignored);
        fs::remove_all(dir, ignored);
        fs::remove(outside, ignored);
    }

    // Escaping: quotes, backslashes, tabs, newlines and control characters in string literals.
    const std::string escaped = mydsl::debug_json("x := \"q\\\"b\\\\s\tt\x01" "c\";");
    check(valid_json(escaped), "escaped json is valid", escaped);
    check(escaped.find("\"text\":\"q\\\"b\\\\s\\tt\\u0001c\"") != std::string::npos, "string token escaped", escaped);
    check(escaped.find("\"op\":\"string\",\"name\":\"q\\\"b\\\\s\\tt\\u0001c\"") != std::string::npos, "string node escaped", escaped);
    const std::string message = mydsl::debug_json("x := \"unterminated\n");
    check(valid_json(message) && message.find("\"stage\":\"Lexing\"") != std::string::npos, "error messages are escaped", message);
    check(valid_json(mydsl::debug_json("x := 1 +")) && valid_json(mydsl::debug_json("x := \"\\\"\";")), "more escaping cases");

    // Every example program produces valid JSON without an error.
    const char* files[] = {"Check_Sorted", "binary_search", "eigen_values", "int_vector_test", "matrix_test", "scoping_test"};
    for (const char* name : files) {
        std::ifstream file(std::string("inputs/") + name + ".txt");
        std::string text{std::istreambuf_iterator<char>(file), {}};
        const std::string json = mydsl::debug_json(text);
        check(valid_json(json) && json.find("\"error\":null") != std::string::npos, std::string("debug_json of inputs/") + name + ".txt");
    }
}

void span_tests() {
    auto at = [](std::size_t line, std::size_t column) { return mydsl::Location{line, column}; };
    auto same = [](mydsl::Location a, mydsl::Location b) { return a.line == b.line && a.column == b.column; };
    auto spans = [&](const mydsl::Span& s, mydsl::Location b, mydsl::Location e) { return same(s.begin, b) && same(s.end, e); };
    auto tokens = mydsl::lex("int a := \"s\\\"t\";\n  b := 2.5e1;");
    check(spans({tokens[0].location, tokens[0].end}, at(1, 1), at(1, 4)), "token end: word");
    check(spans({tokens[3].location, tokens[3].end}, at(1, 10), at(1, 16)), "token end: string includes quotes and escapes");
    check(spans({tokens[7].location, tokens[7].end}, at(2, 8), at(2, 13)), "token end: float on line 2");
    check(spans({tokens.back().location, tokens.back().end}, at(2, 14), at(2, 14)), "token end: <eof> is empty");

    auto program = mydsl::parse("int a := (1 + 2) * -b[i, j];\nif (a) then {\n  Print(f(a));\n} else {\n};");
    const auto& decl = program.at(0);
    check(spans(decl->span, at(1, 1), at(1, 29)), "declaration span runs through ';'");
    const auto& times = decl->exprs.at(0);
    check(times->op == "binary" && spans(times->span, at(1, 10), at(1, 28)), "binary span: left begin to right end");
    check(spans(times->args.at(0)->span, at(1, 10), at(1, 17)), "parenthesised expression span includes the parentheses");
    check(spans(times->args.at(0)->args.at(0)->span, at(1, 11), at(1, 12)), "literal span");
    const auto& neg = times->args.at(1);
    check(neg->op == "unary" && spans(neg->span, at(1, 20), at(1, 28)), "unary span: operator to operand end");
    check(spans(neg->args.at(0)->span, at(1, 21), at(1, 28)), "index span: target begin to ']'");
    const auto& cond = program.at(1);
    check(spans(cond->span, at(2, 1), at(5, 3)), "if statement span runs to its ';' on line 5");
    check(spans(cond->exprs.at(0)->span, at(2, 5), at(2, 6)), "condition span excludes the if parentheses");
    check(spans(cond->body.at(0)->span, at(2, 13), at(4, 2)), "then-block span is '{' to '}'");
    check(spans(cond->body.at(1)->span, at(4, 8), at(5, 2)), "else-block span");
    const auto& print = cond->body.at(0)->body.at(0);
    check(spans(print->span, at(3, 3), at(3, 15)), "nested print span");
    check(spans(print->exprs.at(0)->span, at(3, 9), at(3, 13)), "call span includes argument list");
    auto block = mydsl::parse("{ x := 1; };").at(0);
    check(spans(block->span, at(1, 1), at(1, 13)), "block statement span includes the trailing ';'");
    auto sized = mydsl::parse("x := (3 [1,2,3]);").at(0)->exprs.at(0);
    check(spans(sized->span, at(1, 6), at(1, 17)), "sized literal span includes the parentheses");
    auto call = mydsl::parse("x := f(1)[0];").at(0)->exprs.at(0);
    check(spans(call->span, at(1, 6), at(1, 13)) && spans(call->args.at(0)->span, at(1, 6), at(1, 10)), "index of call spans");
    try { mydsl::parse("x := (1;"); check(false, "structured error fields"); }
    catch (const mydsl::Error& e) {
        check(e.stage == "Parse" && e.location.line == 1 && e.location.column == 8 &&
              e.message == "Expected ')' to close the '(' opened at line 1, column 6 but found ';'", "structured error fields");
    }
}

// Parses and type-checks `source`; returns "name: type" lines of the top-level variables.
std::string types_of(const std::string& source) {
    auto program = mydsl::parse(source);
    std::string out;
    for (const auto& [name, type] : mydsl::typecheck(program)) out += name + ": " + type.str() + "\n";
    return out;
}

void expect_types(const std::string& source, const std::string& expected) {
    try {
        const auto actual = types_of(source);
        check(actual == expected, "types of: " + source, "expected:\n" + expected + "actual:\n" + actual);
    } catch (const std::exception& e) {
        check(false, "types of: " + source, std::string("unexpected exception: ") + e.what());
    }
}

void expect_type_error(const std::string& source, std::size_t line, std::size_t column, const std::string& fragment) {
    try {
        types_of(source);
        check(false, "type error for: " + source, "accepted but an error was expected");
    } catch (const mydsl::Error& e) {
        const std::string prefix = "Type error at line " + std::to_string(line) + ", column " + std::to_string(column) + ": ";
        const std::string message = e.what();
        check(message.compare(0, prefix.size(), prefix) == 0 && message.find(fragment) != std::string::npos,
              "type error for: " + source, "expected '" + prefix + "...' containing '" + fragment + "'\n      got      '" + message + "'");
    } catch (const std::exception& e) {
        check(false, "type error for: " + source, std::string("wrong exception type: ") + e.what());
    }
}

// Type of the expression of the n-th top-level statement after type checking.
std::string expr_type(const std::string& source, std::size_t statement = 0, std::size_t expr = 0) {
    auto program = mydsl::parse(source);
    mydsl::typecheck(program);
    return program.at(statement)->exprs.at(expr)->type.str();
}

void type_tests() {
    // Scalars, literals and operators.
    expect_types("", "");
    expect_types("int a := 1; float b := 2.5; bool c := true;", "a: int\nb: float\nc: bool\n");
    expect_types("int a := 1 + 2 * 3 - 4 / 5 % 6; float b := 1.5 * 2. / .5 - -1.;", "a: int\nb: float\n");
    expect_types("int a := 1; a := -a; a := - - -a; a := a + 2 - -2;", "a: int\n");
    expect_types("bool a := 1 < 2; bool b := 1.5 >= 2.5; bool c := a == b; bool d := a != b && !a || b;", "a: bool\nb: bool\nc: bool\nd: bool\n");
    expect_types("bool a := true; bool b := a + false; bool c := a * b;", "a: bool\nb: bool\nc: bool\n"); // + is or, * is and
    expect_types("bool b := 1 == 1 && 2.0 != 3.0;", "b: bool\n");
    expect_types("int a := to_int(2.7); float b := to_float(3); float c := abs(-1.5); int d := abs(-2);", "a: int\nb: float\nc: float\nd: int\n");
    check(expr_type("int a := 1; bool b := a + 1 < 2 * a;", 1) == "bool", "inferred type of a comparison");
    check(expr_type("Print(1.5 * 2.0);") == "float", "inferred type of a float product");
    check(expr_type("Print(-[1, 2]);") == "intvector 2", "inferred type of a negated vector");
    // Mixed int/float and bool arithmetic are errors.
    expect_type_error("int a := 1; float b := 1.0 * a;", 1, 28, "Operator '*' cannot be applied to float and int (MyDSL does not convert between int and float implicitly; use to_float(...) or to_int(...))");
    expect_type_error("int a := 1 + 1.0;", 1, 12, "Operator '+' cannot be applied to int and float");
    expect_type_error("int a := 1; a := a + true;", 1, 20, "Operator '+' cannot be applied to int and bool");
    expect_type_error("bool b := true - false;", 1, 16, "Operator '-' cannot be applied to bool operands (only + as 'or' and * as 'and' are defined)");
    expect_type_error("bool b := true / false;", 1, 16, "Operator '/' cannot be applied to bool operands");
    expect_type_error("int a := 5 % 2.0;", 1, 12, "Operator '%' requires int operands but got int and float");
    expect_type_error("float a := 5.0 % 2.0;", 1, 16, "Operator '%' requires int operands but got float and float");
    expect_type_error("int a := -true;", 1, 10, "Operator '-' requires an int, float, vector or matrix operand but got bool");
    expect_type_error("bool a := !1;", 1, 11, "Operator '!' requires a bool operand but got int");
    expect_type_error("bool a := 1 < 2.0;", 1, 13, "Operator '<' requires operands of the same type but got int and float");
    expect_type_error("bool a := true < false;", 1, 16, "Operator '<' requires int or float operands but got bool and bool");
    expect_type_error("bool a := 1 == 1.0;", 1, 13, "Operator '==' requires operands of the same type but got int and float");
    expect_type_error("bool a := 1 == true;", 1, 13, "Operator '==' requires operands of the same type but got int and bool");
    expect_type_error("bool a := 1 && true;", 1, 13, "Operator '&&' requires bool operands but got int and bool");
    expect_type_error("bool a := true || 0;", 1, 16, "Operator '||' requires bool operands but got bool and int");
    expect_type_error("int a := \"text\";", 1, 1, "Cannot initialise 'a' of type int with a value of type string (MyDSL does not convert between string and other types)");
    // Declarations and assignments.
    expect_type_error("int a := true;", 1, 1, "Cannot initialise 'a' of type int with a value of type bool");
    expect_type_error("int a := 1.0;", 1, 1, "Cannot initialise 'a' of type int with a value of type float (MyDSL does not convert");
    expect_type_error("float a := 1;", 1, 1, "Cannot initialise 'a' of type float with a value of type int");
    expect_type_error("int a := 1;\na := 2.5;", 2, 1, "Cannot assign a value of type float to 'a' of type int (declared at line 1, column 1)");
    expect_type_error("bool b := true; b := 1;", 1, 17, "Cannot assign a value of type int to 'b' of type bool");
    expect_type_error("x := 1;", 1, 1, "Undeclared variable 'x'");
    expect_type_error("int a := b;", 1, 10, "Undeclared variable 'b'");
    expect_type_error("Print(y);", 1, 7, "Undeclared variable 'y'");
    expect_type_error("int a := 1; int a := 2;", 1, 13, "Variable 'a' is already declared in this scope (as int at line 1, column 1)");
    expect_type_error("int a := 1; float a := 2.0;", 1, 13, "Variable 'a' is already declared in this scope");
    expect_type_error("int a := a;", 1, 10, "Undeclared variable 'a'"); // not in scope inside its own initializer
    // Scoping: blocks nest, inner declarations vanish, shadowing in inner scopes is allowed.
    expect_types("int a := 1; { int b := 2; a := b; }; a := 3;", "a: int\n");
    expect_types("int a := 1; { bool a := 2.0 > 1.0; }; a := 3;", "a: int\n"); // shadowing with another type
    expect_types("int a := 1; { { { a := 2; }; }; };", "a: int\n");
    expect_type_error("{ int b := 2; }; b := 3;", 1, 18, "Undeclared variable 'b'");
    expect_type_error("{ int b := 2; }; { Print(b); };", 1, 26, "Undeclared variable 'b'");
    expect_type_error("int a := 1; { int b := 1; int b := 2; };", 1, 27, "Variable 'b' is already declared in this scope (as int at line 1, column 15)");
    expect_type_error("if (true) then { int t := 1; } else { Print(t); };", 1, 45, "Undeclared variable 't'");
    expect_type_error("while (true) { int w := 1; }; Print(w);", 1, 37, "Undeclared variable 'w'");
    // Conditions.
    expect_type_error("if (1) then {};", 1, 5, "The condition of 'if' must be bool but is int");
    expect_type_error("int a := 1; if (a) then {};", 1, 17, "The condition of 'if' must be bool but is int");
    expect_type_error("while (1.0) {};", 1, 8, "The condition of 'while' must be bool but is float");
    expect_type_error("intvector 2 v := [1,2]; while (v) {};", 1, 32, "The condition of 'while' must be bool but is intvector 2");
    // For loops: the loop variable is a fresh int, visible only in the header and body.
    expect_types("for (i := 0, i < 3, 1) { Print(i); };", "");
    expect_types("int s := 0; for (i := 0, i < 3, 1) { s := s + i; };", "s: int\n");
    expect_types("int i := 7; for (i := 0, i < 3, 1) { i := i + 1; }; i := i + 1;", "i: int\n"); // shadows the outer i
    expect_types("float i := 7.0; for (i := 0, i < 3, 1) { Print(i); }; i := 1.5;", "i: float\n");
    expect_types("for (i := 0, i < 2, 1) { for (j := 0, j < 2, 1) { Print(i * j); }; };", "");
    expect_types("for (i := 0, i < 2, 1) { int i := 5; Print(i); };", ""); // body is an inner scope
    expect_type_error("for (i := 0, i < 3, 1) {}; Print(i);", 1, 34, "Undeclared variable 'i'");
    expect_type_error("for (i := 0.0, i < 3.0, 1.0) {};", 1, 11, "The for loop variable 'i' must start with an int value but got float");
    expect_type_error("for (i := 0, i, 1) {};", 1, 14, "The condition of 'for' must be bool but is int");
    expect_type_error("for (i := 0, i < 3, 0.5) {};", 1, 21, "The step of the for loop must be int but got float");
    expect_type_error("for (i := 0, i < 3, 1) { i := 1.5; };", 1, 26, "Cannot assign a value of type float to 'i' of type int (declared at line 1, column 1)");
    expect_type_error("for (i := 0, j < 3, 1) {};", 1, 14, "Undeclared variable 'j'");
    // Vectors and matrices: literals, element-wise operators, scaling, equality.
    expect_types("intvector 3 v := [1, -2, 3]; floatvector 2 w := (2 [1.0, 2.0]);", "v: intvector 3\nw: floatvector 2\n");
    expect_types("intmatrix 2,2 m := [[1,2],[3,4]]; floatmatrix 2,3 n := (2,3 [[1.,2.,3.],[4.,5.,6.]]);", "m: intmatrix 2,2\nn: floatmatrix 2,3\n");
    expect_types("intvector 2 a := [1,2]; intvector 2 b := a + a - a; intvector 2 c := a * 2; intvector 2 d := 3 * a; intvector 2 e := a / 2; intvector 2 f := -a;",
                 "a: intvector 2\nb: intvector 2\nc: intvector 2\nd: intvector 2\ne: intvector 2\nf: intvector 2\n");
    expect_types("floatmatrix 2,2 a := [[1.,2.],[3.,4.]]; floatmatrix 2,2 b := a + a; floatmatrix 2,2 c := -a * 2.0; floatmatrix 2,2 d := a / 2.0;",
                 "a: floatmatrix 2,2\nb: floatmatrix 2,2\nc: floatmatrix 2,2\nd: floatmatrix 2,2\n");
    expect_types("intvector 2 a := [1,2]; bool e := a == a; bool n := a != [2,1]; intmatrix 1,2 m := [[1,2]]; bool q := m == m;", "a: intvector 2\ne: bool\nm: intmatrix 1,2\nn: bool\nq: bool\n");
    expect_types("floatvector 2 a := [1.,2.]; floatvector 2 b := to_float([1,2]); intvector 2 c := to_int(a); intmatrix 1,1 d := to_int([[1.5]]);", "a: floatvector 2\nb: floatvector 2\nc: intvector 2\nd: intmatrix 1,1\n");
    expect_types("intvector 2 v := [1,2]; int x := v[0]; intmatrix 2,2 m := [[1,2],[3,4]]; int y := m[1,1]; int z := m[v[0], 0];", "m: intmatrix 2,2\nv: intvector 2\nx: int\ny: int\nz: int\n");
    expect_types("floatvector 2 v := [1.,2.]; v[0] := 3.5; floatmatrix 2,2 m := [[1.,2.],[3.,4.]]; m[1,0] := v[1];", "m: floatmatrix 2,2\nv: floatvector 2\n");
    expect_types("intvector 2 v := [1,2]; int i := 5; int x := v[i - 4];", "i: int\nv: intvector 2\nx: int\n"); // non-literal index: checked at run time
    check(expr_type("intvector 2 a := [1,2]; Print(a * 2 + [3,4]);", 1) == "intvector 2", "inferred vector expression type");
    check(expr_type("Print([[1,2],[3,4]][0,1]);") == "int", "indexing a literal");
    expect_type_error("floatvector 2 v := [1, 2];", 1, 1, "Cannot initialise 'v' of type floatvector 2 with a value of type intvector 2");
    expect_type_error("intvector 3 v := [1, 2];", 1, 1, "Cannot initialise 'v' of type intvector 3 with a value of type intvector 2");
    expect_type_error("intmatrix 2,2 m := [[1,2,3],[4,5,6]];", 1, 1, "Cannot initialise 'm' of type intmatrix 2,2 with a value of type intmatrix 2,3");
    expect_type_error("intvector 2 v := [1, 2.0];", 1, 22, "Vector elements must all have the same type: element 2 is float but element 1 is int");
    expect_type_error("intmatrix 1,2 m := [[1.0, 2]];", 1, 27, "Matrix elements must all have the same type: element 2 is int but element 1 is float");
    expect_type_error("intvector 2 v := [true, false];", 1, 19, "Vector elements must be int or float but element 1 is bool");
    expect_type_error("intvector 2 v := [1, [2]];", 1, 22, "Vector elements must be int or float but element 2 is intvector 1");
    expect_type_error("intvector 2 a := [1,2]; intvector 3 b := [1,2,3]; intvector 2 c := a + b;", 1, 70, "Operator '+' requires operands of the same shape but got intvector 2 and intvector 3");
    expect_type_error("intvector 2 a := [1,2]; intvector 2 c := a * a;", 1, 44, "Operator '*' is not defined between two vectors; use dotprod_vec(a, b)");
    expect_type_error("intmatrix 1,1 a := [[1]]; intmatrix 1,1 c := a * a;", 1, 48, "Operator '*' is not defined between two matrices; use mult_mat(a, b)");
    expect_type_error("intvector 2 a := [1,2]; intvector 2 c := a / a;", 1, 44, "Operator '/' is not defined between two vectors");
    expect_type_error("intvector 2 a := [1,2]; intvector 2 c := 2 / a;", 1, 44, "Operator '/' cannot combine a scalar and a vector (int and intvector 2)");
    expect_type_error("intvector 2 a := [1,2]; intvector 2 c := a + 1;", 1, 44, "Operator '+' cannot combine a scalar and a vector");
    expect_type_error("intvector 2 a := [1,2]; intvector 2 c := a * 2.0;", 1, 44, "Operator '*' cannot be applied to intvector 2 and float (MyDSL does not convert");
    expect_type_error("intvector 2 a := [1,2]; floatvector 2 b := [1.,2.]; bool e := a == b;", 1, 65, "Operator '==' requires operands of the same type but got intvector 2 and floatvector 2");
    expect_type_error("intvector 2 a := [1,2]; bool e := a < a;", 1, 37, "Operator '<' requires int or float operands but got intvector 2 and intvector 2");
    expect_type_error("intvector 2 a := [1,2]; bool e := !a;", 1, 35, "Operator '!' requires a bool operand but got intvector 2");
    // Indexing.
    expect_type_error("int a := 1; int b := a[0];", 1, 23, "Cannot index a value of type int; only vectors and matrices can be indexed");
    expect_type_error("intvector 2 v := [1,2]; int x := v[0, 1];", 1, 35, "A vector takes one index (v[i]) but intvector 2 was given two");
    expect_type_error("intmatrix 2,2 m := [[1,2],[3,4]]; int x := m[0];", 1, 45, "A matrix takes two indices (m[row, column]) but intmatrix 2,2 was given one");
    expect_type_error("intvector 2 v := [1,2]; int x := v[1.0];", 1, 36, "Index must be int but is float");
    expect_type_error("intvector 2 v := [1,2]; int x := v[true];", 1, 36, "Index must be int but is bool");
    expect_type_error("intvector 2 v := [1,2]; int x := v[2];", 1, 36, "Index 2 is out of range for intvector 2 (valid indices are 0..1)");
    expect_type_error("intvector 2 v := [1,2]; int x := v[-1];", 1, 36, "Index -1 is out of range for intvector 2");
    expect_type_error("intmatrix 2,3 m := [[1,2,3],[4,5,6]]; int x := m[2, 0];", 1, 50, "Row index 2 is out of range for intmatrix 2,3 (valid indices are 0..1)");
    expect_type_error("intmatrix 2,3 m := [[1,2,3],[4,5,6]]; int x := m[0, 3];", 1, 53, "Column index 3 is out of range for intmatrix 2,3 (valid indices are 0..2)");
    expect_type_error("intvector 2 v := [1,2]; float x := v[0];", 1, 25, "Cannot initialise 'x' of type float with a value of type int");
    expect_type_error("intvector 2 v := [1,2]; v[0] := 1.5;", 1, 25, "Cannot assign a value of type float to an element of 'v' (intvector 2, elements are int)");
    expect_type_error("intvector 2 v := [1,2]; v[0, 0] := 1;", 1, 25, "'v' is an intvector 2 and takes one index (v[i] := ...)");
    expect_type_error("intmatrix 2,2 m := [[1,2],[3,4]]; m[0] := 1;", 1, 35, "'m' is an intmatrix 2,2 and takes two indices (m[row, column] := ...)");
    expect_type_error("intmatrix 2,2 m := [[1,2],[3,4]]; m[0, 2] := 1;", 1, 40, "Column index 2 is out of range for intmatrix 2,2");
    expect_type_error("int a := 1; a[0] := 1;", 1, 13, "Cannot index 'a' of type int; only vector and matrix elements can be assigned");
    expect_type_error("v[0] := 1;", 1, 1, "Undeclared variable 'v'");
    // Builtin functions and matrix multiplication.
    expect_types("intmatrix 2,3 a := [[1,2,3],[4,5,6]]; intmatrix 3,1 b := [[1],[2],[3]]; intmatrix 2,1 c := mult_mat(a, b); intmatrix 3,2 t := transpose(a); intmatrix 3,3 s := mult_mat(t, a);",
                 "a: intmatrix 2,3\nb: intmatrix 3,1\nc: intmatrix 2,1\ns: intmatrix 3,3\nt: intmatrix 3,2\n");
    expect_types("floatmatrix 2,2 a := [[1.,2.],[3.,4.]]; floatmatrix 2,2 i := inverse_mat(a); float d := determinant(a); floatmatrix 2,2 p := mult_mat(mult_mat(a, i), transpose(a));",
                 "a: floatmatrix 2,2\nd: float\ni: floatmatrix 2,2\np: floatmatrix 2,2\n");
    expect_types("intmatrix 2,2 a := [[1,2],[3,4]]; int d := determinant(a); floatmatrix 2,2 i := inverse_mat(to_float(a));", "a: intmatrix 2,2\nd: int\ni: floatmatrix 2,2\n");
    expect_types("intvector 3 a := [1,2,3]; intvector 3 b := inv_vec(a); int d := dotprod_vec(a, b); float n := abs(a); float g := angle_vec(a, b); bool z := is_zero(a);",
                 "a: intvector 3\nb: intvector 3\nd: int\ng: float\nn: float\nz: bool\n");
    expect_types("floatvector 2 a := [1.,2.]; float d := dotprod_vec(a, a); float n := abs(a); intmatrix 1,1 m := [[0]]; bool z := is_zero(m);", "a: floatvector 2\nd: float\nm: intmatrix 1,1\nn: float\nz: bool\n");
    check(expr_type("intmatrix 2,3 a := [[1,2,3],[4,5,6]]; Print(mult_mat(a, transpose(a)));", 1) == "intmatrix 2,2", "mult_mat result type");
    check(expr_type("intmatrix 2,3 a := [[1,2,3],[4,5,6]]; Print(mult_mat(transpose(a), a));", 1) == "intmatrix 3,3", "mult_mat result type (transposed first)");
    expect_type_error("intmatrix 2,3 a := [[1,2,3],[4,5,6]]; intmatrix 2,2 c := mult_mat(a, a);", 1, 58, "mult_mat cannot multiply intmatrix 2,3 by intmatrix 2,3: the column count of the first (3) must equal the row count of the second (2)");
    expect_type_error("intmatrix 2,2 a := [[1,2],[3,4]]; floatmatrix 2,2 b := [[1.,2.],[3.,4.]]; intmatrix 2,2 c := mult_mat(a, b);", 1, 94, "mult_mat expects matrices of the same element type but got intmatrix 2,2 and floatmatrix 2,2");
    expect_type_error("intmatrix 2,2 a := [[1,2],[3,4]]; intmatrix 2,2 c := mult_mat(a, [1,2]);", 1, 54, "mult_mat expects two matrices but got intmatrix 2,2 and intvector 2");
    expect_type_error("intmatrix 2,2 a := [[1,2],[3,4]]; intmatrix 2,2 c := mult_mat(a);", 1, 54, "Function 'mult_mat' expects 2 arguments but got 1");
    expect_type_error("intmatrix 2,3 a := [[1,2,3],[4,5,6]]; intmatrix 2,3 t := transpose(a);", 1, 39, "Cannot initialise 't' of type intmatrix 2,3 with a value of type intmatrix 3,2");
    expect_type_error("intvector 2 v := [1,2]; intvector 2 t := transpose(v);", 1, 42, "transpose expects a matrix but got intvector 2");
    expect_type_error("intmatrix 2,3 a := [[1,2,3],[4,5,6]]; int d := determinant(a);", 1, 48, "determinant expects a square matrix but got intmatrix 2,3");
    expect_type_error("float d := determinant(1.5);", 1, 12, "determinant expects a matrix but got float");
    expect_type_error("intmatrix 2,2 a := [[1,2],[3,4]]; intmatrix 2,2 i := inverse_mat(a);", 1, 54, "inverse_mat expects a floatmatrix but got intmatrix 2,2; convert with to_float(...)");
    expect_type_error("floatmatrix 2,3 a := [[1.,2.,3.],[4.,5.,6.]]; floatmatrix 2,3 i := inverse_mat(a);", 1, 68, "inverse_mat expects a square matrix but got floatmatrix 2,3");
    expect_type_error("intvector 2 a := [1,2]; intvector 3 b := [1,2,3]; int d := dotprod_vec(a, b);", 1, 60, "dotprod_vec expects two vectors of the same type and size but got intvector 2 and intvector 3");
    expect_type_error("intvector 2 a := [1,2]; floatvector 2 b := [1.,2.]; float g := angle_vec(a, b);", 1, 64, "angle_vec expects two vectors of the same type and size but got intvector 2 and floatvector 2");
    expect_type_error("int d := dotprod_vec(1, 2);", 1, 10, "dotprod_vec expects two vectors but got int and int");
    expect_type_error("intmatrix 1,1 m := [[1]]; intmatrix 1,1 i := inv_vec(m);", 1, 46, "inv_vec expects a vector but got intmatrix 1,1");
    expect_type_error("bool z := is_zero(0);", 1, 11, "is_zero expects a vector or matrix but got int");
    expect_type_error("intmatrix 1,1 m := [[1]]; int a := abs(m);", 1, 36, "abs expects an int, float or vector but got intmatrix 1,1");
    expect_type_error("int a := abs(true);", 1, 10, "abs expects an int, float or vector but got bool");
    expect_type_error("int a := abs(1, 2);", 1, 10, "Function 'abs' expects 1 argument but got 2");
    expect_type_error("int a := abs();", 1, 10, "Function 'abs' expects 1 argument but got 0");
    expect_type_error("int a := to_int(true);", 1, 10, "to_int expects an int, float, vector or matrix but got bool");
    expect_type_error("int a := foo(1);", 1, 10, "Unknown function 'foo' (available: abs, angle_vec, determinant, dotprod_vec, Input, inv_vec, inverse_mat, is_zero, length, mult_mat, substring, to_float, to_int, to_string, transpose)");
    expect_type_error("Print(print(1));", 1, 7, "Unknown function 'print'");
    // Input() takes the type of its target.
    expect_types("int a := Input(); float b := Input(\"ip.txt\"); floatvector 3 v := Input(); bool c := Input(); floatmatrix 2,2 m := Input();",
                 "a: int\nb: float\nc: bool\nm: floatmatrix 2,2\nv: floatvector 3\n");
    expect_types("int a := 1; a := Input(); intvector 2 v := [1,2]; v[1] := Input();", "a: int\nv: intvector 2\n");
    check(expr_type("floatvector 3 v := Input();") == "floatvector 3", "Input() gets the declared type");
    check(expr_type("intvector 2 v := [1,2]; v[1] := Input();", 1, 1) == "int", "Input() in an element assignment gets the element type");
    expect_type_error("int a := Input() + 1;", 1, 10, "Input() can only be used as the whole initializer of a declaration, the whole right-hand side of an assignment or the whole value of a return statement");
    expect_type_error("Print(Input());", 1, 7, "Input() can only be used as the whole initializer");
    expect_type_error("int a := abs(Input());", 1, 14, "Input() can only be used as the whole initializer");
    expect_type_error("int a := Input(1);", 1, 16, "The argument of Input must be a file name string literal, e.g. Input(\"data.txt\")");
    expect_type_error("int a := Input(\"a\", \"b\");", 1, 10, "Input expects no argument or one file name but got 2 arguments");
    expect_type_error("for (i := Input(), i < 3, 1) {};", 1, 11, "Input() can only be used as the whole initializer");
    // Print accepts any type; statements after an error are not reached (first error wins).
    expect_types("Print(1); Print(2.5); Print(true); Print([1,2]); Print([[1.0]]); Print(1 < 2);", "");
    expect_type_error("int a := 1;\nPrint(a + 1.0);\nint b := true;", 2, 9, "Operator '+' cannot be applied to int and float");
    // Every example program type-checks except the intentional static failure.
    const char* files[] = {"Check_Sorted", "arithmetic_op", "binary_search", "check_even", "eigen_values", "error_input1", "factorial",
                           "fibonacci", "float_input", "for_loop", "functions", "gaussian_elimination", "gcd", "int_vector_test", "logical_tests",
                           "matrix_indexing", "matrix_test", "prime", "scoping_test", "search", "string_functions", "string_input", "strings", "t", "test",
                           "vector_functions", "while_loop"};
    for (const char* name : files) {
        std::ifstream file(std::string("inputs/") + name + ".txt");
        std::string text{std::istreambuf_iterator<char>(file), {}};
        try { types_of(text); ++passed; }
        catch (const std::exception& e) { check(false, std::string("typecheck inputs/") + name + ".txt", e.what()); }
    }
    {
        std::ifstream file("inputs/error_input2.txt");
        std::string text{std::istreambuf_iterator<char>(file), {}};
        expect_type_error(text, 3, 1, "Cannot initialise 'a' of type int with a value of type bool");
    }
    expect_types("floatmatrix 2,2 a := [[3.,1.],[1.,2.]];\nfloatmatrix 2,1 b := [[9.],[8.]];\nfloatmatrix 2,1 x := mult_mat(inverse_mat(a), b);\nPrint(mult_mat(a, x) == b);",
                 "a: floatmatrix 2,2\nb: floatmatrix 2,1\nx: floatmatrix 2,1\n");
}

void function_type_tests() {
    const std::string square = "function int square(int x) { return x * x; };";
    const std::string hint = " (MyDSL does not convert between int and float implicitly; use to_float(...) or to_int(...))";
    // Valid definitions and calls: the call has the declared return type, functions are not variables.
    expect_types(square, "");
    expect_types(square + " int a := square(4); int b := square(square(a)) + square(1);", "a: int\nb: int\n");
    check(expr_type(square + " Print(square(3));", 1) == "int", "call of a user function has the return type");
    check(expr_type(square + " Print(square(3) + 1);", 1) == "int", "call result in an expression");
    check(expr_type("function floatvector 2 f(float x) { return [x, x]; }; Print(f(1.5));", 1) == "floatvector 2", "vector return type");
    check(expr_type("function floatvector 2 f(float x) { return [x, x]; }; Print(f(1.5)[1]);", 1) == "float", "indexing a call result");
    check(expr_type("function intmatrix 2,2 m(int d) { return [[d, 0], [0, d]]; }; Print(mult_mat(m(1), transpose(m(2))));", 1) == "intmatrix 2,2", "matrix return type through builtins");
    check(expr_type("function bool b() { return true; }; Print(!b() && b());", 1) == "bool", "bool return type");
    expect_types("function float f(int a, float b, bool c, intvector 2 v, floatmatrix 1,2 m) { return b; }; float r := f(1, 2.0, true, [1, 2], [[1., 2.]]);", "r: float\n");
    expect_types("function int f() { return 1; }; int a := f();", "a: int\n");
    // Order does not matter: a call may precede the definition; recursion and mutual recursion.
    expect_types("int a := square(2); " + square, "a: int\n");
    expect_types("function int fact(int n) { if (n <= 1) then { return 1; } else { return n * fact(n - 1); }; }; int f := fact(5);", "f: int\n");
    expect_types("function bool even(int n) { if (n == 0) then { return true; } else { return odd(n - 1); }; };\n"
                 "function bool odd(int n) { if (n == 0) then { return false; } else { return even(n - 1); }; };\nbool e := even(10);", "e: bool\n");
    // Bodies: parameters are variables of the body scope, locals, nested blocks, loops, Print, Input().
    expect_types("function int f(int x) { x := x + 1; int y := x * 2; { int z := y; y := z; }; return y; };", "");
    expect_types("function int f(int x) { { int x := 2; Print(x); }; return x; };", ""); // an inner block may shadow a parameter
    expect_types("function int sum(intvector 3 v) { int s := 0; for (i := 0, i < 3, 1) { s := s + v[i]; }; return s; };", "");
    expect_types("function int f(int x) { while (x > 0) { x := x - 1; }; Print(x); return x; };", "");
    expect_types("function int f() { return Input(); }; function float g() { return Input(\"ip.txt\"); };", "");
    {
        auto program = mydsl::parse("function floatvector 3 f() { return Input(); };");
        mydsl::typecheck(program);
        check(program.at(0)->body.at(0)->body.at(0)->exprs.at(0)->type.str() == "floatvector 3", "return Input() takes the return type");
    }
    // Every path must return: if/else in both branches, nested blocks; loops do not count.
    expect_types("function int f(int x) { if (x > 0) then { return 1; } else { return -1; }; };", "");
    expect_types("function int f(int x) { { { return 1; }; }; };", "");
    expect_types("function int f(int x) { if (x > 0) then { return 1; }; return 0; };", "");
    expect_types("function int f(int x) { if (x > 0) then { if (x > 5) then { return 2; } else { return 1; }; } else { return 0; }; };", "");
    expect_types("function int f(int x) { while (true) { return 1; }; return 0; };", "");
    // Functions and variables are separate namespaces (as 'Input := 1;' already was).
    expect_types(square + " int square := 3; Print(square(square));", "square: int\n");

    // Wrong number of arguments.
    expect_type_error(square + " int a := square();", 1, 56, "Function 'square' expects 1 argument (int x) but got 0 (declared at line 1, column 1)");
    expect_type_error(square + " int a := square(1, 2);", 1, 56, "Function 'square' expects 1 argument (int x) but got 2 (declared at line 1, column 1)");
    expect_type_error("function int f() { return 1; }; Print(f(1));", 1, 39, "Function 'f' expects 0 arguments but got 1 (declared at line 1, column 1)");
    expect_type_error("function int add(int a, int b) { return a + b; };\nPrint(add(1));", 2, 7, "Function 'add' expects 2 arguments (int a, int b) but got 1 (declared at line 1, column 1)");
    expect_type_error("function int f(int n) { return f(); };", 1, 32, "Function 'f' expects 1 argument (int n) but got 0");
    // Argument type mismatches are reported at the argument.
    expect_type_error(square + " Print(square(2.0));", 1, 60, "Argument 1 of 'square' must be int (parameter 'x') but got float" + hint);
    expect_type_error(square + " Print(square(true));", 1, 60, "Argument 1 of 'square' must be int (parameter 'x') but got bool");
    expect_type_error(square + " Print(square([1]));", 1, 60, "Argument 1 of 'square' must be int (parameter 'x') but got intvector 1");
    expect_type_error("function int add(int a, float b) { return a; }; Print(add(1, 2));", 1, 62, "Argument 2 of 'add' must be float (parameter 'b') but got int" + hint);
    expect_type_error("function float f(floatvector 3 v) { return v[0]; }; Print(f([1., 2.]));", 1, 61, "Argument 1 of 'f' must be floatvector 3 (parameter 'v') but got floatvector 2");
    expect_type_error("function int f(intmatrix 2,2 m) { return m[0, 0]; }; Print(f([[1, 2, 3], [4, 5, 6]]));", 1, 62, "Argument 1 of 'f' must be intmatrix 2,2 (parameter 'm') but got intmatrix 2,3");
    expect_type_error(square + " Print(square(square(1.5)));", 1, 67, "Argument 1 of 'square' must be int (parameter 'x') but got float");
    expect_type_error(square + " Print(square(Input()));", 1, 60, "Input() can only be used as the whole initializer");
    expect_type_error(square + " Print(square(undefined_name));", 1, 60, "Undeclared variable 'undefined_name'");
    // The call result must fit where it is used.
    expect_type_error(square + " float a := square(2);", 1, 47, "Cannot initialise 'a' of type float with a value of type int" + hint);
    expect_type_error("function bool b() { return true; }; Print(b() + 1);", 1, 47, "Operator '+' cannot be applied to bool and int");
    expect_type_error("function float f() { return 1.0; }; if (f()) then {};", 1, 41, "The condition of 'if' must be bool but is float");
    expect_type_error("function int f() { return 1; }; Print(f()[0]);", 1, 42, "Cannot index a value of type int");
    // Return type mismatches.
    expect_type_error("function int f(int x) { return 1.5; };", 1, 25, "Function 'f' must return int but this return statement has a value of type float" + hint);
    expect_type_error("function int f(int x) { return x > 0; };", 1, 25, "Function 'f' must return int but this return statement has a value of type bool");
    expect_type_error("function floatvector 3 f() { return [1., 2.]; };", 1, 30, "Function 'f' must return floatvector 3 but this return statement has a value of type floatvector 2");
    expect_type_error("function int f(int x) {\n  if (x > 0) then { return x; } else { return [x]; };\n};", 2, 40, "Function 'f' must return int but this return statement has a value of type intvector 1");
    expect_type_error("function int f(int x) { return y; };", 1, 32, "Undeclared variable 'y'");
    // Missing returns.
    expect_type_error("function int f(int x) { Print(x); };", 1, 1, "Function 'f' must return int on every path, but the end of its body can be reached without a return statement (an if needs a return in both branches; loops do not count because they may run zero times)");
    expect_type_error("function int f() { };", 1, 1, "Function 'f' must return int on every path, but the end of its body can be reached without a return statement (the body is empty)");
    expect_type_error("function int f(int x) { if (x > 0) then { return 1; }; };", 1, 1, "must return int on every path");
    expect_type_error("function int f(int x) { if (x > 0) then { return 1; } else { Print(x); }; };", 1, 1, "must return int on every path");
    expect_type_error("function int f(int x) { if (x > 0) then { Print(x); } else { return 1; }; };", 1, 1, "must return int on every path");
    expect_type_error("function int f(int x) { while (x > 0) { return 1; }; };", 1, 1, "must return int on every path");
    expect_type_error("function int f(int x) { for (i := 0, i < x, 1) { return i; }; };", 1, 1, "must return int on every path");
    expect_type_error("function int f(int x) { int y := x; };", 1, 1, "must return int on every path");
    expect_type_error("function int ok() { return 1; };\nfunction float bad(float x) {\n  x := x * 2.0;\n};", 2, 1, "Function 'bad' must return float on every path");
    // Duplicate and invalid definitions.
    expect_type_error(square + "\n" + square, 2, 1, "Function 'square' is already defined (at line 1, column 1)");
    expect_type_error(square + " function float square(float x) { return x; };", 1, 47, "Function 'square' is already defined (at line 1, column 1)"); // no overloading
    expect_type_error("function int abs(int x) { return x; };", 1, 1, "Cannot define function 'abs': 'abs' is a built-in function; choose another name");
    expect_type_error("function int Input() { return 1; };", 1, 1, "Cannot define function 'Input': 'Input' is a built-in function");
    expect_type_error("function intmatrix 2,2 mult_mat(intmatrix 2,2 a) { return a; };", 1, 1, "Cannot define function 'mult_mat'");
    expect_type_error("function int f(int x, float y, int x) { return x; };", 1, 32, "Function 'f' declares parameter 'x' twice (first at line 1, column 16); parameter names must be unique");
    expect_type_error("function int f(int x) { int x := 2; return x; };", 1, 25, "Variable 'x' is already declared in this scope (as parameter int at line 1, column 16); parameters are variables of the function body and cannot be redeclared there");
    expect_type_error("function int f(int x) { int y := 1; int y := 2; return x; };", 1, 37, "Variable 'y' is already declared in this scope (as int at line 1, column 25)");
    expect_types("function int f(int x) { for (i := 0, i < 1, 1) {}; int i := 1; return x; };", ""); // the loop variable is local to the loop
    // Declaration errors are found before body errors (pass 1), body errors in source order.
    expect_type_error("function int f() { return true; };\nfunction int f() { return 1; };", 2, 1, "Function 'f' is already defined");
    expect_type_error("function int f() { return true; };\nfunction int g() { return 1.0; };", 1, 20, "Function 'f' must return int");
    // Scoping: a body sees only parameters and locals, never top-level variables or later globals.
    expect_type_error("int g := 1; function int f(int x) { return x + g; };", 1, 48, "Undeclared variable 'g' (inside function 'f' only its parameters and local variables are visible, not top-level variables; pass the value as a parameter)");
    expect_type_error("function int f(int x) { return x + g; }; int g := 1;", 1, 36, "Undeclared variable 'g' (inside function 'f' only its parameters");
    expect_type_error("function int f(int x) { g := 1; return x; }; int g := 1;", 1, 25, "Undeclared variable 'g' (inside function 'f'");
    expect_type_error("function int f(int x) { return x; }; Print(x);", 1, 44, "Undeclared variable 'x'"); // parameters are local to the body
    expect_type_error("function int f(int x) { int y := 1; return x; }; Print(y);", 1, 56, "Undeclared variable 'y'");
    expect_type_error("function int f(int x) { return x; }; function int g() { return x; };", 1, 64, "Undeclared variable 'x' (inside function 'g'");
    // Functions are not variables and variables are not functions.
    expect_type_error(square + " Print(square);", 1, 53, "Undeclared variable 'square' ('square' is a function; call it with arguments: square(...))");
    expect_type_error(square + " square := 1;", 1, 47, "Undeclared variable 'square' ('square' is a function; call it with arguments: square(...))");
    expect_type_error(square + " int a := square + 1;", 1, 56, "'square' is a function; call it with arguments");
    expect_type_error("int v := 1; Print(v(1));", 1, 19, "Unknown function 'v': 'v' is a variable of type int (declared at line 1, column 1), not a function");
    expect_type_error("function int f(int x) { return x(1); };", 1, 32, "Unknown function 'x': 'x' is a variable of type int (declared at line 1, column 16), not a function");
    expect_type_error(square + " Print(cube(2));", 1, 53, "Unknown function 'cube' (available: abs, angle_vec, determinant, dotprod_vec, Input, inv_vec, inverse_mat, is_zero, length, mult_mat, substring, to_float, to_int, to_string, transpose; defined in this program: square)");
    expect_type_error("function int f() { return g(); };", 1, 27, "Unknown function 'g' (available: abs, angle_vec, determinant, dotprod_vec, Input, inv_vec, inverse_mat, is_zero, length, mult_mat, substring, to_float, to_int, to_string, transpose; defined in this program: f)");
    // Errors inside a body use the usual rules and positions.
    expect_type_error("function int f(int x) {\n  float y := x;\n  return x;\n};", 2, 3, "Cannot initialise 'y' of type float with a value of type int");
    expect_type_error("function int f(int x) {\n  if (x) then { return 1; } else { return 0; };\n};", 2, 7, "The condition of 'if' must be bool but is int");
    expect_type_error("function int f(intvector 2 v) { return v[2]; };", 1, 42, "Index 2 is out of range for intvector 2");
    expect_type_error("function int f(int x) { return x; };\nint a := true;", 2, 1, "Cannot initialise 'a' of type int with a value of type bool");
    // The fixture files.
    for (const char* name : {"function_arg_count", "function_arg_type", "function_duplicate", "function_missing_return", "function_return_type", "function_scope"}) {
        std::ifstream file(std::string("tests/type_errors/") + name + ".txt");
        std::string text{std::istreambuf_iterator<char>(file), {}};
        try { types_of(text); check(false, std::string("tests/type_errors/") + name + ".txt should fail"); }
        catch (const mydsl::Error& e) { check(e.stage == "Type", std::string("tests/type_errors/") + name + ".txt is a type error", e.what()); }
    }
}

// Parses, type-checks and runs `source`; returns the program's output.
std::string output_of(const std::string& source, const mydsl::Limits& limits = {}, const std::string& input = "") {
    auto program = mydsl::parse(source);
    mydsl::typecheck(program);
    std::istringstream in(input);
    std::ostringstream out;
    mydsl::interpret(program, in, out, limits);
    return out.str();
}

void expect_output(const std::string& source, const std::string& expected, const std::string& input = "") {
    try {
        const auto actual = output_of(source, {}, input);
        check(actual == expected, "output of: " + source, "expected:\n" + expected + "actual:\n" + actual);
    } catch (const std::exception& e) {
        check(false, "output of: " + source, std::string("unexpected exception: ") + e.what());
    }
}

// Expects a Runtime error at line/column containing `fragment`; `before` is the output that must
// have been produced before the failure.
void expect_runtime_error(const std::string& source, std::size_t line, std::size_t column, const std::string& fragment,
                          const std::string& before = "", const mydsl::Limits& limits = {}, const std::string& input = "") {
    std::ostringstream out;
    try {
        auto program = mydsl::parse(source);
        mydsl::typecheck(program);
        std::istringstream in(input);
        mydsl::interpret(program, in, out, limits);
        check(false, "runtime error for: " + source, "ran to completion but an error was expected");
    } catch (const mydsl::Error& e) {
        const std::string prefix = "Runtime error at line " + std::to_string(line) + ", column " + std::to_string(column) + ": ";
        const std::string message = e.what();
        check(message.compare(0, prefix.size(), prefix) == 0 && message.find(fragment) != std::string::npos,
              "runtime error for: " + source, "expected '" + prefix + "...' containing '" + fragment + "'\n      got      '" + message + "'");
        check(out.str() == before, "output before the runtime error of: " + source, "expected '" + before + "' got '" + out.str() + "'");
    } catch (const std::exception& e) {
        check(false, "runtime error for: " + source, std::string("wrong exception type: ") + e.what());
    }
}

void interpreter_tests() {
    // Literals, Print formats, arithmetic and precedence.
    expect_output("", "");
    expect_output("Print(42); Print(-7); Print(2.5); Print(3.); Print(1e3); Print(true); Print(false);", "Int(42)\nInt(-7)\nFloat(2.5)\nFloat(3.0)\nFloat(1000.0)\nBool(true)\nBool(false)\n");
    expect_output("Print(1 + 2 * 3); Print((1 + 2) * 3); Print(10 - 4 - 3); Print(2 * 3 % 4); Print(7 / 2); Print(-7 / 2); Print(7 % 3); Print(-7 % 3); Print(7 % -3);",
                  "Int(7)\nInt(9)\nInt(3)\nInt(2)\nInt(3)\nInt(-3)\nInt(1)\nInt(-1)\nInt(1)\n");
    expect_output("Print(1.5 + 2.25); Print(1.0 / 4.0); Print(2.0 * -3.5); Print(10.0 - 0.5 * 4.0); Print(1.0 / 3.0);", "Float(3.75)\nFloat(0.25)\nFloat(-7.0)\nFloat(8.0)\nFloat(0.333333333333333)\n");
    expect_output("Print(- - -2); Print(-(1 + 2) * 3); Print(2 - -2);", "Int(-2)\nInt(-9)\nInt(4)\n");
    expect_output("Print(9223372036854775807); Print(-9223372036854775807 - 1); Print(9223372036854775807 / 2);", "Int(9223372036854775807)\nInt(-9223372036854775808)\nInt(4611686018427387903)\n");
    // Comparisons and booleans.
    expect_output("Print(1 < 2); Print(2 < 1); Print(1 <= 1); Print(1 >= 2); Print(3 > 2); Print(1 == 1); Print(1 != 1); Print(1.5 < 1.6); Print(2.0 == 2.0); Print(true == false); Print(true != false);",
                  "Bool(true)\nBool(false)\nBool(true)\nBool(false)\nBool(true)\nBool(true)\nBool(false)\nBool(true)\nBool(true)\nBool(false)\nBool(true)\n");
    expect_output("Print(!true); Print(!!true); Print(true && false); Print(true || false); Print(false || false && true); Print((true || false) && false);",
                  "Bool(false)\nBool(true)\nBool(false)\nBool(true)\nBool(false)\nBool(false)\n");
    expect_output("Print(true + false); Print(false + false); Print(true * true); Print(true * false);", "Bool(true)\nBool(false)\nBool(true)\nBool(false)\n"); // + is or, * is and
    // Short-circuit evaluation: the right operand is not evaluated (and cannot fail) when unneeded.
    expect_output("int z := 0; Print(false && 1 / z == 0); Print(true || 1 / z == 0); Print(z > 0 && 10 / z > 1); Print(z == 0 || 10 / z > 1);",
                  "Bool(false)\nBool(true)\nBool(false)\nBool(true)\n");
    expect_runtime_error("int z := 0; Print(true && 1 / z == 0);", 1, 29, "Division by zero: 1 / 0");
    expect_runtime_error("int z := 0; Print(false || 1 / z == 0);", 1, 30, "Division by zero: 1 / 0");
    expect_runtime_error("int z := 0; Print(false + (1 / z == 0));", 1, 30, "Division by zero"); // + on bools does not short-circuit
    // Builtins.
    expect_output("Print(abs(-3)); Print(abs(3)); Print(abs(-2.5)); Print(abs(0)); Print(to_float(3)); Print(to_int(3.99)); Print(to_int(-3.99)); Print(to_float(to_int(2.5)) * 2.0); Print(to_int(1e18));",
                  "Int(3)\nInt(3)\nFloat(2.5)\nInt(0)\nFloat(3.0)\nInt(3)\nInt(-3)\nFloat(4.0)\nInt(1000000000000000000)\n");
    expect_runtime_error("Print(abs(-9223372036854775807 - 1));", 1, 7, "Integer overflow: abs(-9223372036854775808) does not fit in a 64-bit int");
    expect_runtime_error("Print(to_int(1e19));", 1, 7, "to_int(1e+19) does not fit in a 64-bit int");
    expect_runtime_error("Print(to_int(-1e19));", 1, 7, "does not fit in a 64-bit int");
    // Variables and assignments.
    expect_output("int a := 1; a := a + 1; a := a * 10; Print(a); float f := 0.5; f := f + f; Print(f); bool b := false; b := !b; Print(b);", "Int(20)\nFloat(1.0)\nBool(true)\n");
    expect_output("int a := 5; int b := a; a := 6; Print(a); Print(b);", "Int(6)\nInt(5)\n"); // values are copied
    // Division by zero and overflow.
    expect_runtime_error("int a := 1 / 0;", 1, 12, "Division by zero: 1 / 0");
    expect_runtime_error("int z := 0; Print(7 % z);", 1, 21, "Modulo by zero: 7 % 0");
    expect_runtime_error("float f := 1.0 / 0.0;", 1, 16, "Division by zero: Float(1.0) / Float(0.0)");
    expect_runtime_error("float z := 0.0; Print(-2.5 / z);", 1, 28, "Division by zero: Float(-2.5) / Float(0.0)");
    expect_runtime_error("Print(1); Print(2 / (1 - 1)); Print(3);", 1, 19, "Division by zero: 2 / 0", "Int(1)\n");
    expect_runtime_error("int a := 9223372036854775807; a := a + 1;", 1, 38, "Integer overflow: 9223372036854775807 + 1 does not fit in a 64-bit int (range -9223372036854775808 .. 9223372036854775807)");
    expect_runtime_error("int a := -9223372036854775807 - 1; a := a - 1;", 1, 43, "Integer overflow: -9223372036854775808 - 1");
    expect_runtime_error("int a := 4294967296; a := a * a;", 1, 29, "Integer overflow: 4294967296 * 4294967296");
    expect_runtime_error("int a := -9223372036854775807 - 1; a := -a;", 1, 41, "Integer overflow: 0 - -9223372036854775808");
    expect_runtime_error("int a := -9223372036854775807 - 1; a := a / -1;", 1, 43, "Integer overflow: -9223372036854775808 / -1");
    expect_output("int a := -9223372036854775807 - 1; Print(a % -1);", "Int(0)\n");
    expect_output("int big := 3037000499; Print(big * big);", "Int(9223372030926249001)\n"); // just below the limit
    expect_runtime_error("float f := 1e308; Print(f * 10.0);", 1, 27, "Float overflow: Float(1e+308) * Float(10.0) is not a finite number");
    expect_runtime_error("float f := -1e308; Print(f - 1e308);", 1, 28, "Float overflow: Float(-1e+308) - Float(1e+308) is not a finite number");
    expect_output("float f := 1e308; Print(f / 10.0); Print(1e-300 / 1e5);", "Float(1e+307)\nFloat(1e-305)\n");
    // Blocks and scoping: declarations are local, assignments reach outer scopes, shadowing.
    expect_output("int a := 1; { int b := 2; a := a + b; Print(a); }; Print(a);", "Int(3)\nInt(3)\n");
    expect_output("int a := 1; { int a := 2; Print(a); { int a := 3; Print(a); }; Print(a); }; Print(a);", "Int(2)\nInt(3)\nInt(2)\nInt(1)\n");
    expect_output("int a := 1; { a := 2; { a := 3; { int a := 4; a := 5; }; }; }; Print(a);", "Int(3)\n");
    expect_output("int a := 1; { bool a := true; Print(a); }; Print(a);", "Bool(true)\nInt(1)\n");
    expect_output("int a := 1; if (true) then { int a := 10; a := a + 1; Print(a); } else { Print(0); }; Print(a);", "Int(11)\nInt(1)\n");
    expect_output("int x := 0; { int x := 1; { x := 2; }; Print(x); }; Print(x);", "Int(2)\nInt(0)\n");
    // A block that runs repeatedly gets fresh variables each time.
    expect_output("int i := 0; while (i < 3) { int y := i * 10; y := y + 1; Print(y); i := i + 1; };", "Int(1)\nInt(11)\nInt(21)\n");
    // Conditions.
    expect_output("if (1 < 2) then { Print(1); };", "Int(1)\n");
    expect_output("if (1 > 2) then { Print(1); };", "");
    expect_output("if (1 > 2) then { Print(1); } else { Print(2); };", "Int(2)\n");
    expect_output("int n := 7; if (n % 2 == 0) then { Print(true); } else { if (n > 5) then { Print(n); } else { Print(false); }; };", "Int(7)\n");
    // While loops.
    expect_output("int i := 0; int s := 0; while (i < 5) { s := s + i; i := i + 1; }; Print(s); Print(i);", "Int(10)\nInt(5)\n");
    expect_output("while (false) { Print(1); }; Print(2);", "Int(2)\n");
    expect_output("int i := 10; while (i > 0 && i % 7 != 0) { i := i - 1; }; Print(i);", "Int(7)\n");
    expect_output("int i := 0; int j := 0; int c := 0; while (i < 3) { j := 0; while (j < 2) { c := c + 1; j := j + 1; }; i := i + 1; }; Print(c);", "Int(6)\n");
    // For loops: i := a; while c { body; i := i + s }.
    expect_output("for (i := 0, i < 3, 1) { Print(i); };", "Int(0)\nInt(1)\nInt(2)\n");
    expect_output("for (i := 3, i > 0, -1) { Print(i); };", "Int(3)\nInt(2)\nInt(1)\n");
    expect_output("for (i := 0, i < 10, 3) { Print(i); };", "Int(0)\nInt(3)\nInt(6)\nInt(9)\n");
    expect_output("for (i := 5, i < 3, 1) { Print(i); }; Print(0);", "Int(0)\n");
    expect_output("int s := 0; for (i := 1, i <= 4, 1) { for (j := 1, j <= i, 1) { s := s + j; }; }; Print(s);", "Int(20)\n");
    expect_output("for (i := 0, i < 2, 1) { for (i := 10, i < 12, 1) { Print(i); }; Print(i); };", "Int(10)\nInt(11)\nInt(0)\nInt(10)\nInt(11)\nInt(1)\n"); // inner loop shadows
    expect_output("int i := 100; for (i := 0, i < 2, 1) { Print(i); }; Print(i);", "Int(0)\nInt(1)\nInt(100)\n"); // the outer i is untouched
    // Changing the counter in the body changes the iteration: the step is added to the current value.
    expect_output("for (i := 0, i < 10, 1) { Print(i); i := i + 4; };", "Int(0)\nInt(5)\n");
    expect_output("for (i := 0, i < 10, 1) { Print(i); if (i == 2) then { i := 100; }; }; Print(7);", "Int(0)\nInt(1)\nInt(2)\nInt(7)\n"); // the Check_Sorted.txt "break" idiom
    expect_output("for (i := 0, i < 4, 1) { Print(i); i := i - 1; i := i + 1; };", "Int(0)\nInt(1)\nInt(2)\nInt(3)\n");
    // The step expression is re-evaluated each iteration; the condition too.
    expect_output("int step := 1; for (i := 0, i < 20, step) { Print(i); step := step * 2; };", "Int(0)\nInt(2)\nInt(6)\nInt(14)\n"); // step read after the body
    expect_output("int limit := 2; for (i := 0, i < limit, 1) { Print(i); limit := 4; };", "Int(0)\nInt(1)\nInt(2)\nInt(3)\n");
    expect_runtime_error("for (i := 9223372036854775807, i > 0, 1) { Print(i); };", 1, 1, "Integer overflow: 9223372036854775807 + 1", "Int(9223372036854775807)\n");
    expect_runtime_error("for (i := 0, i < 3, 1) { Print(i / i); };", 1, 34, "Division by zero: 0 / 0");
    // Variables declared in the loop body are fresh each iteration; the counter persists.
    expect_output("for (i := 0, i < 2, 1) { int t := i; t := t + 1; Print(t); };", "Int(1)\nInt(2)\n");
    // Limits (used by the browser view): time and output size.
    expect_runtime_error("int i := 0; while (true) { i := i + 1; };", 1, 13, "Execution stopped after 100 ms (the time limit was exceeded; is there an infinite loop?)", "", mydsl::Limits{100.0, 0});
    expect_runtime_error("for (i := 0, true, 0) { };", 1, 1, "Execution stopped after 50 ms", "", mydsl::Limits{50.0, 0});
    expect_runtime_error("int i := 0;\nwhile (i < 10) {\n  while (true) { i := i + 1; };\n};", 3, 3, "Execution stopped after 50 ms", "", mydsl::Limits{50.0, 0}); // reported at the innermost loop
    expect_runtime_error("int i := 0; while (true) { Print(i); i := i + 1; };", 1, 28, "Output limit of 40 bytes exceeded", "Int(0)\nInt(1)\nInt(2)\nInt(3)\nInt(4)\n", mydsl::Limits{0, 40});
    check(output_of("int i := 0; while (i < 100000) { i := i + 1; }; Print(i);", mydsl::Limits{5000.0, 1000}) == "Int(100000)\n", "limits do not interfere with a finishing program");
    // Final values of the top-level variables are returned.
    {
        auto program = mydsl::parse("int a := 2; float b := 1.5; bool c := a > 1; { int hidden := 9; }; for (i := 0, i < 3, 1) { a := a * 2; };");
        mydsl::typecheck(program);
        std::istringstream in; std::ostringstream out;
        const auto globals = mydsl::interpret(program, in, out);
        check(globals.size() == 3 && globals.at("a").ints.at(0) == 16 && globals.at("b").floats.at(0) == 1.5 && globals.at("c").truth(), "interpret returns the final top-level values");
        check(globals.at("a").type.str() == "int" && globals.at("b").type.str() == "float" && globals.at("c").type.str() == "bool", "returned values carry their types");
    }
    // Example programs.
    struct Example { const char* name; const char* output; };
    const Example examples[] = {
        {"factorial", "Int(120)\n"},
        {"prime", "Bool(true)\n"},
        {"arithmetic_op", "Int(6)\nInt(3)\nInt(8)\nInt(23)\nInt(-1)\n"},
        {"logical_tests", "Bool(true)\nBool(true)\nBool(false)\nBool(true)\n"},
        {"t", "Int(2)\nInt(5)\nInt(10)\nInt(10)\n"},
        {"for_loop", "Int(1)\nInt(2)\nInt(3)\nInt(4)\nInt(5)\n"},
        {"scoping_test", ""},
        {"while_loop", "Int(3)\nInt(4)\nInt(4)\nInt(4)\nInt(5)\nInt(4)\nInt(6)\nInt(4)\nInt(7)\nInt(4)\nInt(8)\nInt(4)\nInt(9)\nInt(4)\nInt(10)\nInt(4)\nInt(11)\nInt(4)\n"},
    };
    for (const auto& example : examples) {
        std::ifstream file(std::string("inputs/") + example.name + ".txt");
        std::string text{std::istreambuf_iterator<char>(file), {}};
        try { const auto out = output_of(text); check(out == example.output, std::string("run inputs/") + example.name + ".txt", "got:\n" + out); }
        catch (const std::exception& e) { check(false, std::string("run inputs/") + example.name + ".txt", e.what()); }
    }
    {
        std::ifstream file("inputs/error_input1.txt");
        std::string text{std::istreambuf_iterator<char>(file), {}};
        expect_runtime_error(text, 2, 20, "Division by zero: Float(-23.0) / Float(0.0)");
    }
    // Run JSON (what the browser's Run button receives).
    const std::string ran = mydsl::run_json("Print(1 + 1);\nPrint(2 / 0);", "", {});
    check(valid_json(ran) && ran.find("{\"output\":\"Int(2)\\n\",\"error\":{\"stage\":\"Runtime\",\"line\":2,\"column\":9,\"message\":\"Division by zero: 2 / 0\"},\"variables\":[],\"millis\":") == 0, "run_json with a runtime error", ran);
    const std::string fine = mydsl::run_json("Print(true);", "", {});
    check(valid_json(fine) && fine.find("{\"output\":\"Bool(true)\\n\",\"error\":null,\"variables\":[],\"millis\":") == 0, "run_json success", fine);
    check(valid_json(mydsl::run_json("int a := ;", "", {})) && mydsl::run_json("int a := ;", "", {}).find("\"stage\":\"Parse\"") != std::string::npos, "run_json parse error");
    check(mydsl::run_json("int a := 1.5;", "", {}).find("\"stage\":\"Type\"") != std::string::npos, "run_json type error");
    const std::string with_input = mydsl::run_json("floatvector 2 v := Input(); int n := Input(); Print(v * to_float(n));", "[1.5, -2.] 2", {});
    check(valid_json(with_input) && with_input.find("{\"output\":\"FloatVector[3.0, -4.0]\\n\",\"error\":null") == 0, "run_json passes the input to Input()", with_input);
    const std::string looped = mydsl::run_json("while (true) {};", "", mydsl::Limits{50.0, 0});
    check(valid_json(looped) && looped.find("Execution stopped after 50 ms") != std::string::npos, "run_json enforces the time limit", looped);
}

// Expected values below were computed independently (NumPy / exact fractions); see the comments.
void vector_matrix_tests() {
    // Literals, printing, element-wise arithmetic, scaling, negation, equality.
    expect_output("intvector 3 v := [1, -2, 3]; Print(v); floatvector 2 w := (2 [1.5, -0.25]); Print(w);", "IntVector[1, -2, 3]\nFloatVector[1.5, -0.25]\n");
    expect_output("intmatrix 2,3 m := [[1,2,3],[4,5,6]]; Print(m);", "IntMatrix[\n  [1, 2, 3]\n  [4, 5, 6]\n]\n");
    expect_output("intvector 3 a := [1,2,3]; intvector 3 b := [10,20,30]; Print(a + b); Print(b - a); Print(a * 2); Print(3 * a); Print(b / 3); Print(-a); Print(a + b * 2 - a);",
                  "IntVector[11, 22, 33]\nIntVector[9, 18, 27]\nIntVector[2, 4, 6]\nIntVector[3, 6, 9]\nIntVector[3, 6, 10]\nIntVector[-1, -2, -3]\nIntVector[20, 40, 60]\n");
    expect_output("floatmatrix 2,2 f := [[1.25,-0.5],[2.0,3.0]]; floatmatrix 2,2 g := [[0.5,1.5],[-2.0,0.25]]; Print(f + g); Print(f * 2.5); Print(f / 4.0); Print(-g);",
                  "FloatMatrix[\n  [1.75, 1.0]\n  [0.0, 3.25]\n]\nFloatMatrix[\n  [3.125, -1.25]\n  [5.0, 7.5]\n]\nFloatMatrix[\n  [0.3125, -0.125]\n  [0.5, 0.75]\n]\nFloatMatrix[\n  [-0.5, -1.5]\n  [2.0, -0.25]\n]\n"); // NumPy: f+g, f*2.5, f/4
    expect_output("intvector 2 a := [1,2]; Print(a == [1,2]); Print(a == [2,1]); Print(a != [2,1]); intmatrix 1,2 m := [[1,2]]; Print(m == [[1,2]]); Print([1.5] == [1.5]);",
                  "Bool(true)\nBool(false)\nBool(true)\nBool(true)\nBool(true)\n");
    expect_output("int x := 2; intvector 2 v := [x, x * x]; floatmatrix 1,2 m := [[to_float(x), 0.5]]; Print(v); Print(m);", "IntVector[2, 4]\nFloatMatrix[\n  [2.0, 0.5]\n]\n");
    expect_output("intvector 2 a := [1,2]; intvector 2 b := a; b[0] := 9; Print(a); Print(b);", "IntVector[1, 2]\nIntVector[9, 2]\n"); // values are copied
    // Indexing and element assignment (with computed indices).
    expect_output("intvector 3 v := [10,20,30]; int i := 2; Print(v[0]); Print(v[i]); Print(v[i - 1] + v[0]);", "Int(10)\nInt(30)\nInt(30)\n");
    expect_output("intmatrix 2,3 m := [[1,2,3],[4,5,6]]; int r := 1; Print(m[0,0]); Print(m[r, 2]); Print(m[r - 1, r + 1]);", "Int(1)\nInt(6)\nInt(3)\n");
    expect_output("floatvector 2 v := [1.,2.]; v[1] := v[0] * 10.0; Print(v); floatmatrix 2,2 m := [[0.,0.],[0.,0.]]; for (i := 0, i < 2, 1) { m[i, i] := 1.0; }; Print(m);",
                  "FloatVector[1.0, 10.0]\nFloatMatrix[\n  [1.0, 0.0]\n  [0.0, 1.0]\n]\n");
    expect_output("intvector 4 v := [0,0,0,0]; for (i := 0, i < 4, 1) { v[i] := i * i; }; Print(v); int s := 0; for (i := 0, i < 4, 1) { s := s + v[i]; }; Print(s);", "IntVector[0, 1, 4, 9]\nInt(14)\n");
    expect_runtime_error("intvector 3 v := [1,2,3]; int i := 3; Print(v[i]);", 1, 47, "Index 3 is out of range for intvector 3 (valid indices are 0..2)");
    expect_runtime_error("intvector 3 v := [1,2,3]; int i := 0; Print(v[i - 1]);", 1, 49, "Index -1 is out of range for intvector 3 (valid indices are 0..2)");
    expect_runtime_error("intmatrix 2,3 m := [[1,2,3],[4,5,6]]; int i := 2; Print(m[i, 0]);", 1, 59, "Row index 2 is out of range for intmatrix 2,3 (valid indices are 0..1)");
    expect_runtime_error("intmatrix 2,3 m := [[1,2,3],[4,5,6]]; int j := 3; Print(m[1, j]);", 1, 62, "Column index 3 is out of range for intmatrix 2,3 (valid indices are 0..2)");
    expect_runtime_error("intvector 3 v := [1,2,3]; int i := 5; v[i] := 0;", 1, 41, "Index 5 is out of range for intvector 3");
    expect_runtime_error("floatmatrix 1,1 m := [[1.]]; int i := 1; m[0, i] := 0.;", 1, 47, "Column index 1 is out of range for floatmatrix 1,1 (valid indices are 0..0)");
    expect_runtime_error("intvector 2 v := [1,2]; Print(1); int i := 2; Print(v[i]);", 1, 55, "Index 2 is out of range", "Int(1)\n");
    // Element-wise errors name the element.
    expect_runtime_error("intvector 2 a := [9223372036854775807, 1]; Print(a + a);", 1, 52, "Integer overflow: 9223372036854775807 + 9223372036854775807 does not fit in a 64-bit int (range -9223372036854775808 .. 9223372036854775807) (element 0)");
    expect_runtime_error("intmatrix 1,2 a := [[1, -9223372036854775807 - 1]]; Print(-a);", 1, 59, "Integer overflow: 0 - -9223372036854775808 does not fit in a 64-bit int (range -9223372036854775808 .. 9223372036854775807) (element [0,1])");
    expect_runtime_error("intvector 2 a := [1, 2]; int z := 0; Print(a / z);", 1, 46, "Division by zero: 1 / 0 (element 0)");
    expect_runtime_error("floatvector 2 a := [1., 2.]; Print(a / 0.0);", 1, 38, "Division by zero: Float(1.0) / Float(0.0) (element 0)");
    expect_runtime_error("floatvector 2 a := [1., 1e308]; Print(a * 10.0);", 1, 41, "Float overflow: Float(1e+308) * Float(10.0) (element 1) is not a finite number");
    // Vector builtins. |[1,2,3]| = sqrt(14) = 3.74165738677394; angles: pi/2, pi/4, pi, acos(32/sqrt(14*77)) = 0.225726128552734 (NumPy).
    expect_output("intvector 3 a := [1,2,3]; intvector 3 b := [4,5,6]; Print(dotprod_vec(a, b)); Print(abs(a)); Print(abs([3,4])); Print(abs([-3.0,4.0])); Print(inv_vec(a)); Print(is_zero(a)); Print(is_zero(a - a)); Print(is_zero([[0, 0]]));",
                  "Int(32)\nFloat(3.74165738677394)\nFloat(5.0)\nFloat(5.0)\nIntVector[3, 2, 1]\nBool(false)\nBool(true)\nBool(true)\n");
    expect_output("floatvector 2 a := [1.5, 2.5]; floatvector 2 b := [2.0, 4.0]; Print(dotprod_vec(a, b)); Print(dotprod_vec(a, a)); Print(inv_vec([1.5, 2.5, -1.0]));", "Float(13.0)\nFloat(8.5)\nFloatVector[-1.0, 2.5, 1.5]\n");
    expect_output("Print(angle_vec([1.,0.], [0.,1.])); Print(angle_vec([1,1], [1,0])); Print(angle_vec([3,4], [-4,3])); Print(angle_vec([1,2,3], [4,5,6])); Print(angle_vec([2.,2.], [1.,1.])); Print(angle_vec([1,0], [-5,0]));",
                  "Float(1.5707963267949)\nFloat(0.785398163397448)\nFloat(1.5707963267949)\nFloat(0.225726128552734)\nFloat(0.0)\nFloat(3.14159265358979)\n");
    expect_runtime_error("intvector 2 a := [0,0]; intvector 2 b := [1,0]; Print(angle_vec(a, b));", 1, 55, "angle_vec: the angle with a zero vector is undefined (IntVector[0, 0] has length 0)");
    expect_runtime_error("Print(angle_vec([1.,2.], [0.,0.]));", 1, 7, "angle_vec: the angle with a zero vector is undefined (FloatVector[0.0, 0.0] has length 0)");
    expect_runtime_error("intvector 2 a := [4611686018427387904, 1]; Print(dotprod_vec(a, a));", 1, 50, "Integer overflow: 4611686018427387904 * 4611686018427387904 does not fit in a 64-bit int (range -9223372036854775808 .. 9223372036854775807) in dotprod_vec");
    expect_runtime_error("intvector 2 a := [9223372036854775807, 9223372036854775807]; Print(dotprod_vec(a, [1, 1]));", 1, 68, "Integer overflow: 9223372036854775807 + 9223372036854775807 does not fit in a 64-bit int (range -9223372036854775808 .. 9223372036854775807) in dotprod_vec");
    expect_runtime_error("floatvector 2 a := [1e200, 1e200]; Print(dotprod_vec(a, a));", 1, 42, "Numerical error: the dot product is not a finite number");
    // Transpose and matrix products, including rectangular shapes (NumPy: a@b, b@a, outer and inner products, fa@fb, fb@fa).
    expect_output("intmatrix 2,3 a := [[1,2,3],[4,5,6]]; intmatrix 3,2 b := [[7,8],[9,10],[11,12]]; Print(transpose(a)); Print(mult_mat(a, b)); Print(mult_mat(b, a));",
                  "IntMatrix[\n  [1, 4]\n  [2, 5]\n  [3, 6]\n]\nIntMatrix[\n  [58, 64]\n  [139, 154]\n]\nIntMatrix[\n  [39, 54, 69]\n  [49, 68, 87]\n  [59, 82, 105]\n]\n");
    expect_output("intmatrix 3,1 c := [[1],[2],[3]]; intmatrix 1,3 r := [[4,5,6]]; Print(mult_mat(c, r)); Print(mult_mat(r, c)); Print(transpose(c)); Print(transpose(transpose(r)) == r);",
                  "IntMatrix[\n  [4, 5, 6]\n  [8, 10, 12]\n  [12, 15, 18]\n]\nIntMatrix[\n  [32]\n]\nIntMatrix[\n  [1, 2, 3]\n]\nBool(true)\n");
    expect_output("floatmatrix 2,3 a := [[1.5,-2.0,0.5],[0.0,3.0,-1.0]]; floatmatrix 3,2 b := [[2.0,1.0],[0.5,-1.0],[4.0,0.0]]; Print(mult_mat(a, b)); Print(mult_mat(b, a));",
                  "FloatMatrix[\n  [4.0, 3.5]\n  [-2.5, -3.0]\n]\nFloatMatrix[\n  [3.0, -1.0, 0.0]\n  [0.75, -4.0, 1.25]\n  [6.0, -8.0, 2.0]\n]\n");
    expect_output("floatmatrix 2,2 f := [[1.25,-0.5],[2.0,3.0]]; floatmatrix 2,2 g := [[0.5,1.5],[-2.0,0.25]]; Print(mult_mat(f, g)); intmatrix 2,2 i := [[1,0],[0,1]]; Print(mult_mat(i, [[7,8],[9,10]]) == [[7,8],[9,10]]);",
                  "FloatMatrix[\n  [1.625, 1.75]\n  [-5.0, 3.75]\n]\nBool(true)\n");
    expect_runtime_error("intmatrix 1,1 a := [[4294967296]]; Print(mult_mat(a, a));", 1, 42, "Integer overflow: 4294967296 * 4294967296 does not fit in a 64-bit int (range -9223372036854775808 .. 9223372036854775807) in mult_mat");
    expect_runtime_error("floatmatrix 1,1 a := [[1e200]]; Print(mult_mat(a, a));", 1, 39, "Numerical error: element [0,0] of the matrix product is not a finite number");
    // Determinants: exact for int matrices (Bareiss), LU for float matrices.
    // [[2,-3,1],[2,0,-1],[1,4,5]] = 49; the 4x4 is exactly -84 (NumPy's float det gives -83.99999999999999).
    expect_output("Print(determinant([[5]])); Print(determinant([[1,2],[3,4]])); Print(determinant([[2,-3,1],[2,0,-1],[1,4,5]])); Print(determinant([[3,1,-2,4],[0,2,5,-1],[1,-3,0,2],[2,0,1,1]]));",
                  "Int(5)\nInt(-2)\nInt(49)\nInt(-84)\n");
    expect_output("Print(determinant([[1,2,3],[4,5,6],[7,8,9]])); Print(determinant([[0,1],[1,0]])); Print(determinant([[0,0],[0,0]])); Print(determinant([[0,2,0],[1,0,0],[0,0,3]]));", "Int(0)\nInt(-1)\nInt(0)\nInt(-6)\n");
    expect_output("Print(determinant([[4.,7.],[2.,6.]])); Print(determinant([[1.5,2.25],[-3.0,4.0]])); Print(determinant([[0.,1.,2.],[1.,0.,3.],[4.,-3.,8.]])); Print(determinant([[2.,0.,0.],[0.,4.,0.],[0.,0.,8.]]));",
                  "Float(10.0)\nFloat(12.75)\nFloat(-2.0)\nFloat(64.0)\n"); // 51/4 = 12.75 exactly
    expect_output("Print(determinant([[1.,2.],[2.,4.]])); Print(determinant([[0.,0.],[0.,0.]])); Print(determinant([[3.5]]));", "Float(0.0)\nFloat(0.0)\nFloat(3.5)\n");
    expect_runtime_error("intmatrix 2,2 a := [[3037000500, 1],[1, 3037000500]]; Print(determinant(a));", 1, 61, "Integer overflow: 3037000500 * 3037000500 does not fit in a 64-bit int (range -9223372036854775808 .. 9223372036854775807) while computing the determinant");
    expect_runtime_error("Print(determinant([[1e200, 0.], [0., 1e200]]));", 1, 7, "Numerical error: the determinant is not a finite number");
    // Inverses: [[4,7],[2,6]]^-1 = [[0.6,-0.7],[-0.2,0.4]]; [[1.5,2.25],[-3,4]]^-1 = [[16/51,-3/17],[4/17,2/17]] (exact fractions);
    // the 3x3 is a textbook example ([[-4.5,7,-1.5],[-2,4,-1],[1.5,-2,0.5]]).
    expect_output("Print(inverse_mat([[4.,7.],[2.,6.]])); Print(inverse_mat([[2.,0.,0.],[0.,4.,0.],[0.,0.,8.]])); Print(inverse_mat([[2.5]]));",
                  "FloatMatrix[\n  [0.6, -0.7]\n  [-0.2, 0.4]\n]\nFloatMatrix[\n  [0.5, 0.0, 0.0]\n  [0.0, 0.25, 0.0]\n  [0.0, 0.0, 0.125]\n]\nFloatMatrix[\n  [0.4]\n]\n");
    expect_output("Print(inverse_mat([[1.5,2.25],[-3.0,4.0]]));", "FloatMatrix[\n  [0.313725490196078, -0.176470588235294]\n  [0.235294117647059, 0.117647058823529]\n]\n");
    expect_output("floatmatrix 3,3 a := [[0.,1.,2.],[1.,0.,3.],[4.,-3.,8.]]; Print(inverse_mat(a)); Print(mult_mat(a, inverse_mat(a)) == [[1.,0.,0.],[0.,1.,0.],[0.,0.,1.]]);",
                  "FloatMatrix[\n  [-4.5, 7.0, -1.5]\n  [-2.0, 4.0, -1.0]\n  [1.5, -2.0, 0.5]\n]\nBool(true)\n"); // needs a row swap (a[0,0] = 0)
    expect_output("Print(inverse_mat([[1.,2.],[3.,4.]]));", "FloatMatrix[\n  [-2.0, 1.0]\n  [1.5, -0.5]\n]\n"); // printed with 15 significant digits
    expect_output("floatmatrix 2,2 a := [[3.,1.],[1.,2.]]; floatmatrix 2,1 b := [[9.],[8.]]; Print(mult_mat(inverse_mat(a), b));", "FloatMatrix[\n  [2.0]\n  [3.0]\n]\n"); // gaussian_elimination.txt
    expect_runtime_error("Print(inverse_mat([[1.,2.],[2.,4.]]));", 1, 7, "inverse_mat: the matrix is singular (its determinant is 0), so it has no inverse");
    expect_runtime_error("Print(inverse_mat([[0.,0.],[0.,0.]]));", 1, 7, "inverse_mat: the matrix is singular");
    expect_runtime_error("Print(inverse_mat([[0.]]));", 1, 7, "inverse_mat: the matrix is singular");
    expect_runtime_error("Print(inverse_mat([[1.,2.,3.],[4.,5.,6.],[7.,8.,9.]]));", 1, 7, "inverse_mat: the matrix is numerically singular (pivot "); // rounding leaves a ~1e-16 pivot
    expect_runtime_error("Print(inverse_mat([[1e-320, 0.],[0., 1e-320]]));", 1, 7, "Numerical error: an element of the inverse is not a finite number"); // 1/1e-320 overflows
    // Floating-point behaviour that users should expect (see README): rounding is exact to ~16 digits, Print shows 15.
    expect_output("Print(0.1 + 0.2); Print(0.1 + 0.2 == 0.3); Print(abs(0.1 + 0.2 - 0.3) < 1e-12); Print(1.0 / 3.0); Print(determinant([[1.,2.,3.],[4.,5.,6.],[7.,8.,9.]]) == 0.0);",
                  "Float(0.3)\nBool(false)\nBool(true)\nFloat(0.333333333333333)\nBool(false)\n");
    // Conversions and mixed-shape helpers.
    expect_output("intvector 2 v := [1, -2]; Print(to_float(v)); Print(to_int([1.9, -1.9])); Print(to_float([[1, 2]]) / 4.0); Print(to_int(to_float(v)) == v);",
                  "FloatVector[1.0, -2.0]\nIntVector[1, -1]\nFloatMatrix[\n  [0.25, 0.5]\n]\nBool(true)\n");
    expect_runtime_error("Print(to_int([1e19, 0.]));", 1, 7, "to_int(1e+19) does not fit in a 64-bit int");
    // The vector/matrix example programs.
    struct Example { const char* name; const char* output; };
    const Example examples[] = {
        {"matrix_test", ""}, {"test", ""},
        {"gaussian_elimination", "FloatMatrix[\n  [2.0]\n  [3.0]\n]\nBool(true)\n"},
        {"eigen_values", "Float(3.0)\nFloat(1.0)\n"},
        {"matrix_indexing", "FloatMatrix[\n  [3.0, 1.0]\n  [1.0, 1.0]\n]\nFloat(3.0)\n"},
        {"int_vector_test", "IntVector[1, -2, -3]\n"},
    };
    for (const auto& example : examples) {
        std::ifstream file(std::string("inputs/") + example.name + ".txt");
        std::string text{std::istreambuf_iterator<char>(file), {}};
        try { const auto out = output_of(text); check(out == example.output, std::string("run inputs/") + example.name + ".txt", "got:\n" + out); }
        catch (const std::exception& e) { check(false, std::string("run inputs/") + example.name + ".txt", e.what()); }
    }
    {
        auto program = mydsl::parse("intmatrix 2,2 m := [[1,2],[3,4]]; m[0,1] := 9; floatvector 2 v := to_float([1, 2]) * 0.5;");
        mydsl::typecheck(program);
        std::istringstream in; std::ostringstream out;
        const auto globals = mydsl::interpret(program, in, out);
        check(globals.at("m").type.str() == "intmatrix 2,2" && globals.at("m").ints == std::vector<std::int64_t>{1, 9, 3, 4} &&
              globals.at("v").floats == std::vector<double>{0.5, 1.0}, "interpret returns vector/matrix values row-major");
    }
}

void input_tests() {
    // Scalars from standard input, in sequence; whitespace and comments are free; floats may omit the fraction.
    expect_output("int a := Input(); Print(a);", "Int(42)\n", "42");
    expect_output("int a := Input(); int b := Input(); Print(a - b);", "Int(-3)\n", "  7\n10  ");
    expect_output("float a := Input(); float b := Input(); float c := Input(); Print(a); Print(b); Print(c);", "Float(2.5)\nFloat(4.0)\nFloat(-25.0)\n", "2.5 4 -2.5e1");
    expect_output("bool a := Input(); bool b := Input(); Print(a); Print(b);", "Bool(true)\nBool(false)\n", "true false");
    expect_output("int a := Input(); Print(a);", "Int(-8)\n", "// a comment\n-8 /* trailing */");
    expect_output("int a := 1; a := Input(); Print(a);", "Int(5)\n", "5");
    expect_output("intvector 2 v := [0, 0]; v[1] := Input(); Print(v);", "IntVector[0, 3]\n", "3");
    expect_output("int n := Input(); int s := 0; for (i := 0, i < n, 1) { int x := Input(); s := s + x; }; Print(s);", "Int(60)\n", "3 10 20 30");
    // Vectors and matrices in literal syntax; the size must match the declared type exactly.
    expect_output("floatvector 3 v := Input(); Print(v);", "FloatVector[1.0, 2.5, -3.0]\n", "[1, 2.5, -3.]");
    expect_output("intvector 2 v := Input(); int k := Input(); Print(v * k);", "IntVector[6, -9]\n", "[2, -3]\n3");
    expect_output("intmatrix 2,3 m := Input(); Print(m); Print(m[1, 2]);", "IntMatrix[\n  [1, 2, 3]\n  [4, 5, 6]\n]\nInt(6)\n", "[[1,2,3],\n [4,5,6]]");
    expect_output("floatmatrix 2,2 a := Input(); floatmatrix 2,1 b := Input(); Print(mult_mat(inverse_mat(a), b));", "FloatMatrix[\n  [2.0]\n  [3.0]\n]\n", "[[3.,1.],[1.,2.]] [[9.],[8.]]");
    // Wrong type, wrong dimensions, malformed data, unexpected end of input: each names the data position.
    expect_runtime_error("int a := Input();", 1, 10, "Invalid data in standard input at line 1, column 1: expected an int (a float was given) but found '4.5'", "", {}, "4.5");
    expect_runtime_error("int a := Input();", 1, 10, "Invalid data in standard input at line 1, column 1: expected an int but found 'abc'", "", {}, "abc");
    expect_runtime_error("int a := Input();", 1, 10, "Invalid data in standard input at line 1, column 1: expected an int that fits in 64 bits but found '99999999999999999999'", "", {}, "99999999999999999999");
    expect_runtime_error("float a := Input();", 1, 12, "expected a number but found 'true'", "", {}, "true");
    expect_runtime_error("bool a := Input();", 1, 11, "expected true or false but found '1'", "", {}, "1");
    expect_runtime_error("bool a := Input();", 1, 11, "expected true or false but found 'true'", "", {}, "-true");
    expect_runtime_error("int a := Input();", 1, 10, "Unexpected end of standard input: expected an int", "", {}, "");
    expect_runtime_error("int a := Input(); Print(a); int b := Input();", 1, 38, "Unexpected end of standard input: expected an int", "Int(1)\n", {}, "1");
    expect_runtime_error("int a := Input();", 1, 10, "Invalid data in standard input at line 2, column 3: Unexpected character '@'", "", {}, "1\n  @");
    expect_runtime_error("intvector 3 v := Input();", 1, 18, "Invalid data in standard input at line 1, column 6: expected ',' (the vector needs 3 elements) but found ']'", "", {}, "[1, 2]");
    expect_runtime_error("intvector 3 v := Input();", 1, 18, "Invalid data in standard input at line 1, column 9: expected ']' after 3 elements but found ','", "", {}, "[1, 2, 3, 4]");
    expect_runtime_error("intvector 3 v := Input();", 1, 18, "expected a value of type intvector 3 starting with '[' but found '1'", "", {}, "1 2 3");
    expect_runtime_error("intvector 3 v := Input();", 1, 18, "Invalid data in standard input at line 1, column 5: expected an int (a float was given) but found '2.5'", "", {}, "[1, 2.5, 3]");
    expect_runtime_error("intvector 2 v := Input();", 1, 18, "Unexpected end of standard input: expected ']' after 2 elements", "", {}, "[1, 2");
    expect_runtime_error("floatmatrix 2,2 m := Input();", 1, 22, "expected ',' (each row needs 2 elements) but found ']'", "", {}, "[[1,2],[3]]");
    expect_runtime_error("floatmatrix 2,2 m := Input();", 1, 22, "expected ']' after 2 rows but found ','", "", {}, "[[1,2],[3,4],[5,6]]");
    expect_runtime_error("floatmatrix 2,2 m := Input();", 1, 22, "expected '[' starting row 1 but found '1'", "", {}, "[1,2,3,4]");
    expect_runtime_error("floatmatrix 2,2 m := Input();", 1, 22, "expected ',' (the matrix needs 2 rows) but found ']'", "", {}, "[[1,2]]");
    expect_runtime_error("floatvector 2 v := Input();", 1, 20, "expected a finite number", "", {}, "[1e999, 2]");
    // Data files: opened once per run relative to the working directory and read sequentially.
    expect_output("float a := Input(\"ip.txt\"); Print(a);", "Float(23.5)\n");
    expect_output("floatvector 6 v := Input(\"tests/data/binary_search.txt\"); float k := Input(\"tests/data/binary_search.txt\"); Print(v[3] == k); Print(v);",
                  "Bool(true)\nFloatVector[1.0, 2.0, 3.0, 4.0, 5.0, 6.0]\n");
    expect_output("int n := Input(\"tests/data/int.txt\"); float f := Input(\"tests/data/float.txt\"); int m := Input(); Print(to_float(n + m) * f);", "Float(43.5)\n", "2");
    expect_runtime_error("int a := Input(\"does/not/exist.txt\");", 1, 10, "Cannot open input file 'does/not/exist.txt' (paths are relative to the working directory)");
    expect_runtime_error("float a := Input(\"ip.txt\"); float b := Input(\"ip.txt\");", 1, 40, "Unexpected end of file 'ip.txt': expected a number");
    expect_runtime_error("int a := Input(\"ip.txt\");", 1, 10, "Invalid data in file 'ip.txt' at line 1, column 1: expected an int (a float was given) but found '23.5'");
    expect_runtime_error("float a := Input(\"tests/data/binary_search.txt\");", 1, 12, "Invalid data in file 'tests/data/binary_search.txt' at line 1, column 1: expected a number but found '['");
    // The example programs with their data files.
    struct Example { const char* program; const char* data; const char* output; };
    const Example examples[] = {
        {"binary_search", "tests/data/binary_search.txt", "Int(3)\n"},
        {"Check_Sorted", "tests/data/sorted.txt", "FloatVector[1.0, 2.0, 3.0, 4.0, 5.0]\nBool(true)\n"},
        {"check_even", "tests/data/int.txt", "Bool(true)\n"},
        {"float_input", "tests/data/float.txt", "Float(23.5)\nFloat(7.25)\n"},
        {"functions", "/dev/null", "Int(49)\nInt(120)\nFloat(3.0)\nBool(true)\n"},
        {"fibonacci", "/dev/null", "Int(0)\nInt(1)\nInt(1)\nInt(2)\nInt(3)\nInt(5)\nInt(8)\nInt(13)\nInt(21)\nInt(34)\nInt(55)\nBool(true)\nInt(2880067194370816120)\n"},
        {"gcd", "/dev/null", "Int(6)\nBool(true)\nInt(12)\nBool(true)\nBool(false)\n"},
        {"search", "/dev/null", "Int(2)\nInt(-1)\nBool(true)\nBool(false)\nInt(0)\nInt(5)\nInt(10)\n"},
        {"string_functions", "/dev/null", "Int(5)\nString(\"eve\")\nBool(true)\nString(\"desserts\")\nBool(true)\nBool(false)\nString(\"    42\")\nString(\"12 squared is 144\")\nString(\"2.5 true\")\n"},
        {"string_input", "tests/data/names.txt", "String(\"Hello, Ada Lovelace and Alan Turing!\")\nInt(12)\nString(\"Alan\")\nBool(true)\nInt(3)\n"},
        {"strings", "/dev/null", "String(\"Hello MyDSL!\")\nString(\"Hello, MyDSL\")\nString(\"MyDSL\")\nString(\"ababab\")\nBool(true)\nString(\"quote \\\" backslash \\\\ tab \\t newline \\n\")\nBool(true)\nBool(true)\n"},
        {"vector_functions", "/dev/null", "FloatVector[0.6, 0.0, 0.8]\nFloatVector[3.0, 0.0, 4.0]\nFloat(1.0)\nFloatMatrix[\n  [89.0, 55.0]\n  [55.0, 34.0]\n]\nFloat(11.0)\nBool(true)\n"},
    };
    for (const auto& example : examples) {
        std::ifstream file(std::string("inputs/") + example.program + ".txt"), data(example.data);
        std::string text{std::istreambuf_iterator<char>(file), {}}, input{std::istreambuf_iterator<char>(data), {}};
        try { const auto out = output_of(text, {}, input); check(out == example.output, std::string("run inputs/") + example.program + ".txt", "got:\n" + out); }
        catch (const std::exception& e) { check(false, std::string("run inputs/") + example.program + ".txt", e.what()); }
    }
    expect_output("floatvector 5 vec := Input(); bool sorted := true; for (i := 0, i < 4, 1) { if (vec[i] > vec[i + 1]) then { sorted := false; i := 5; }; }; Print(sorted);", "Bool(false)\n", "[1., 3., 2., 4., 5.]");
}

void string_tests() {
    // ----- lexer: literals, escapes, errors -----
    {
        auto t = mydsl::lex("\"\" \"plain\" \"a\\\"b\" \"back\\\\slash\" \"tab\\tnl\\ncr\\r\"");
        check(t[0].kind == mydsl::TokenKind::String && t[0].text.empty() && t[0].location.column == 1 && t[0].end.column == 3, "empty string token");
        check(t[1].text == "plain" && t[2].text == "a\"b" && t[3].text == "back\\slash" && t[4].text == "tab\tnl\ncr\r", "string escapes are decoded");
        check(t[4].location.column == 33 && t[4].end.column == 48, "string token span covers quotes and escapes");
        check(t.size() == 6 && t[5].kind == mydsl::TokenKind::End, "all string tokens lexed");
    }
    expect_error("x := \"bad \\q\";", 1, 11, "Unknown escape sequence '\\q' in string literal (supported: \\\" \\\\ \\n \\t \\r)", "Lexing");
    expect_error("x := \"no end", 1, 6, "Unterminated string literal (missing the closing '\"'; a string cannot span lines, use \\n for a line break)", "Lexing");
    expect_error("x := \"two\nlines\";", 1, 6, "Unterminated string literal", "Lexing");
    expect_error("x := \"ends with backslash\\", 1, 6, "Unterminated string literal (missing the closing '\"')", "Lexing");
    expect_error("x := \"a\\\nb\";", 1, 6, "Unterminated string literal", "Lexing"); // backslash before a line break
    expect_error("x := \"tab\\t\" + \"\\u0041\";", 1, 17, "Unknown escape sequence '\\u'", "Lexing");
    // Non-ASCII text is fine inside literals and comments; outside, the whole character is named
    // (never a partial UTF-8 sequence, which would make the JSON error message invalid text).
    check(mydsl::lex("\"caf\xc3\xa9\" // \xe2\x9c\x93")[0].text == "caf\xc3\xa9", "UTF-8 passes through string literals and comments");
    expect_error("x := caf\xc3\xa9;", 1, 9, "Unexpected character '\xc3\xa9' (non-ASCII text is only allowed inside string literals and comments)", "Lexing");
    expect_error("x := \xe2\x9c\x93;", 1, 6, "Unexpected character '\xe2\x9c\x93' (non-ASCII", "Lexing");
    expect_error("x := \xc3;", 1, 6, "Unexpected character byte 0xC3 (non-ASCII", "Lexing");
    expect_error("x := \xff;", 1, 6, "Unexpected character byte 0xFF (non-ASCII", "Lexing");
    expect_error("x := \"open; Print(\"caf\xc3\xa9\");", 1, 23, "Unexpected character '\xc3\xa9'", "Lexing"); // a mis-quoted literal flips the quoting
    check(valid_json(mydsl::debug_json("x := caf\xc3\xa9;")) && mydsl::debug_json("x := caf\xc3\xa9;").find("'caf\xc3\xa9'") == std::string::npos &&
          mydsl::debug_json("x := caf\xc3\xa9;").find("Unexpected character '\xc3\xa9'") != std::string::npos, "non-ASCII lexing error json is valid UTF-8");
    // ----- parser: string type and literals everywhere an expression is allowed -----
    expect_ast("string s := \"hi\";", "(declare string s (string \"hi\"))\n");
    expect_ast("string s := \"\";", "(declare string s (string \"\"))\n");
    expect_ast("s := \"a\\\"b\\\\c\\n\";", "(assign s (string \"a\\\"b\\\\c\\n\"))\n"); // printed with the same escapes
    expect_ast("Print(\"a\" + s + \"b\");", "(print (binary + (binary + (string \"a\") (variable s)) (string \"b\")))\n");
    expect_ast("bool b := s == \"x\" && s != \"\";", "(declare bool b (binary && (binary == (variable s) (string \"x\")) (binary != (variable s) (string \"\"))))\n");
    expect_ast("function string f(string a, string b) { return a + b; };",
               "(function string f (parameter string a) (parameter string b)\n  (block\n    (return (binary + (variable a) (variable b)))))\n");
    expect_ast("x := f(\"a\", 1)[0];", "(assign x (index (call f (string \"a\") Int(1)) Int(0)))\n");
    expect_error("string := \"a\";", 1, 8, "Expected a variable name after type 'string' but found ':='");
    expect_error("int string := 1;", 1, 5, "Cannot use keyword 'string' as a variable name");
    expect_error("function int f(string) { return 1; };", 1, 22, "Expected a parameter name after parameter type 'string' of function 'f' but found ')'");
    expect_error("string 3 s := \"a\";", 1, 8, "Expected a variable name after type 'string' but found '3'");
    expect_error("stringvector 2 v := [\"a\"];", 1, 1, "Unknown type 'stringvector' in declaration (supported types: int, float, bool, string, intvector N, floatvector N, intmatrix R,C or floatmatrix R,C)");
    expect_error("x := \"a\" \"b\";", 1, 10, "Expected ';' to end the assignment to 'x' but found string literal \"b\"");
    expect_error("x := \"a\" +;", 1, 11, "Expected an expression but found ';'");
    {
        auto program = mydsl::parse("Print(\"a\\tb\");");
        const auto& lit = program.at(0)->exprs.at(0);
        check(lit->op == "string" && lit->name == "a\tb" && lit->span.begin.column == 7 && lit->span.end.column == 13, "string literal node holds the decoded text and the source span");
    }
    // ----- type checker: valid uses -----
    const std::string hint = " (MyDSL does not convert between string and other types)";
    expect_types("string s := \"a\"; string e := \"\"; s := e; s := s + s + \"!\" + e;", "e: string\ns: string\n");
    expect_types("string s := \"a\"; bool x := s == \"a\"; bool y := s != \"\" || \"\" == \"\"; Print(s); Print(s + s); Print(s == s);", "s: string\nx: bool\ny: bool\n");
    expect_types("function string greet(string who) { return \"hi \" + who; }; string g := greet(greet(\"x\"));", "g: string\n");
    expect_types("function bool same(string a, string b) { return a == b; }; bool t := same(\"a\", \"\");", "t: bool\n");
    expect_types("function string pick(bool b, string x, string y) { if (b) then { return x; } else { return y; }; }; Print(pick(true, \"\", \"b\"));", "");
    expect_types("string s := \"a\"; { string s := \"inner\"; Print(s); }; if (s == \"a\") then { s := s + \"b\"; };", "s: string\n");
    expect_types("int a := Input(\"data.txt\");", "a: int\n"); // the file name is still a string literal
    check(expr_type("Print(\"a\" + \"b\");") == "string", "concatenation has type string");
    check(expr_type("Print(\"a\" == \"b\");") == "bool", "string equality has type bool");
    check(expr_type("Print(\"\");") == "string", "empty string literal has type string");
    {
        auto program = mydsl::parse("int a := Input(\"ip.txt\");");
        mydsl::typecheck(program);
        check(program.at(0)->exprs.at(0)->args.at(0)->type.str() == "string", "Input's file name argument is typed as string");
    }
    // ----- type checker: invalid uses, each with a clear message -----
    expect_type_error("string s := 1;", 1, 1, "Cannot initialise 's' of type string with a value of type int" + hint);
    expect_type_error("string s := 1.5;", 1, 1, "Cannot initialise 's' of type string with a value of type float" + hint);
    expect_type_error("string s := true;", 1, 1, "Cannot initialise 's' of type string with a value of type bool" + hint);
    expect_type_error("string s := [1, 2];", 1, 1, "Cannot initialise 's' of type string with a value of type intvector 2" + hint);
    expect_type_error("int a := \"1\";", 1, 1, "Cannot initialise 'a' of type int with a value of type string" + hint);
    expect_type_error("bool b := \"true\";", 1, 1, "Cannot initialise 'b' of type bool with a value of type string" + hint);
    expect_type_error("string s := \"a\"; s := 1;", 1, 18, "Cannot assign a value of type int to 's' of type string (declared at line 1, column 1)" + hint);
    expect_type_error("int a := 1; a := \"x\";", 1, 13, "Cannot assign a value of type string to 'a' of type int (declared at line 1, column 1)" + hint);
    expect_type_error("string s := \"a\" + 1;", 1, 17, "Operator '+' cannot be applied to string and int" + hint);
    expect_type_error("string s := 1 + \"a\";", 1, 15, "Operator '+' cannot be applied to int and string" + hint);
    expect_type_error("string s := \"a\" + 1.5;", 1, 17, "Operator '+' cannot be applied to string and float" + hint);
    expect_type_error("string s := \"a\" + true;", 1, 17, "Operator '+' cannot be applied to string and bool" + hint);
    expect_type_error("Print(\"a\" + [1]);", 1, 11, "Operator '+' cannot be applied to string and intvector 1" + hint);
    expect_type_error("string s := \"a\" - \"b\";", 1, 17, "Operator '-' cannot be applied to strings (only + for concatenation and == / != are defined)");
    expect_type_error("string s := \"a\" * \"b\";", 1, 17, "Operator '*' cannot be applied to strings (only + for concatenation and == / != are defined)");
    expect_type_error("string s := \"a\" / \"b\";", 1, 17, "Operator '/' cannot be applied to strings");
    expect_type_error("int n := \"a\" % \"b\";", 1, 14, "Operator '%' requires int operands but got string and string");
    expect_type_error("bool b := \"a\" < \"b\";", 1, 15, "Operator '<' requires int or float operands but got string and string");
    expect_type_error("bool b := \"a\" >= 1;", 1, 15, "Operator '>=' requires operands of the same type but got string and int" + hint);
    expect_type_error("bool b := \"a\" == 1;", 1, 15, "Operator '==' requires operands of the same type but got string and int" + hint);
    expect_type_error("bool b := \"1\" != 1.0;", 1, 15, "Operator '!=' requires operands of the same type but got string and float" + hint);
    expect_type_error("bool b := \"true\" == true;", 1, 18, "Operator '==' requires operands of the same type but got string and bool" + hint);
    expect_type_error("bool b := \"a\" && true;", 1, 15, "Operator '&&' requires bool operands but got string and bool");
    expect_type_error("bool b := true || \"a\";", 1, 16, "Operator '||' requires bool operands but got bool and string");
    expect_type_error("bool b := !\"a\";", 1, 11, "Operator '!' requires a bool operand but got string");
    expect_type_error("string s := -\"a\";", 1, 13, "Operator '-' requires an int, float, vector or matrix operand but got string");
    expect_type_error("if (\"yes\") then {};", 1, 5, "The condition of 'if' must be bool but is string");
    expect_type_error("while (\"\") {};", 1, 8, "The condition of 'while' must be bool but is string");
    expect_type_error("for (i := \"0\", i < 1, 1) {};", 1, 11, "The for loop variable 'i' must start with an int value but got string");
    expect_type_error("for (i := 0, \"go\", 1) {};", 1, 14, "The condition of 'for' must be bool but is string");
    expect_type_error("string s := \"abc\"; Print(s[0]);", 1, 27, "Cannot index a value of type string; only vectors and matrices can be indexed");
    expect_type_error("string s := \"abc\"; s[0] := \"x\";", 1, 20, "Cannot index 's' of type string; only vector and matrix elements can be assigned");
    expect_type_error("intvector 2 v := [1, 2]; Print(v[\"0\"]);", 1, 34, "Index must be int but is string");
    expect_type_error("Print([\"a\", \"b\"]);", 1, 8, "Vector elements must be int or float but element 1 is string");
    expect_type_error("Print([[1], [\"a\"]]);", 1, 14, "Matrix elements must be int or float but element 2 is string");
    expect_type_error("Print([1, \"a\"]);", 1, 11, "Vector elements must be int or float but element 2 is string");
    expect_types("string s := Input(); s := Input(\"ip.txt\"); function string f() { return Input(); };", "s: string\n");
    expect_type_error("int a := Input(\"a\" + \"b\");", 1, 20, "The argument of Input must be a file name string literal");
    expect_type_error("string f := \"ip.txt\"; float a := Input(f);", 1, 40, "The argument of Input must be a file name string literal");
    expect_type_error("Print(abs(\"a\"));", 1, 7, "abs expects an int, float or vector but got string");
    expect_type_error("Print(to_int(\"1\"));", 1, 7, "to_int expects an int, float, vector or matrix but got string");
    expect_type_error("Print(to_float(\"1.5\"));", 1, 7, "to_float expects an int, float, vector or matrix but got string");
    expect_type_error("Print(is_zero(\"\"));", 1, 7, "is_zero expects a vector or matrix but got string");
    expect_type_error("Print(dotprod_vec(\"a\", \"b\"));", 1, 7, "dotprod_vec expects two vectors but got string and string");
    expect_type_error("Print(mult_mat(\"a\", [[1]]));", 1, 7, "mult_mat expects two matrices but got string and intmatrix 1,1");
    expect_type_error("function int f(string s) { return s; };", 1, 28, "Function 'f' must return int but this return statement has a value of type string" + hint);
    expect_type_error("function string f(int n) { return n; };", 1, 28, "Function 'f' must return string but this return statement has a value of type int" + hint);
    expect_type_error("function int f(string s) { return 1; }; Print(f(1));", 1, 49, "Argument 1 of 'f' must be string (parameter 's') but got int" + hint);
    expect_type_error("function int f(int n) { return n; }; Print(f(\"1\"));", 1, 46, "Argument 1 of 'f' must be int (parameter 'n') but got string" + hint);
    expect_type_error("string s := \"a\"; string s := \"b\";", 1, 18, "Variable 's' is already declared in this scope (as string at line 1, column 1)");
    expect_type_error("Print(s + \"a\");", 1, 7, "Undeclared variable 's'");
    // ----- interpreter: values, concatenation, comparison, printing, edge cases -----
    expect_output("Print(\"hello\");", "String(\"hello\")\n");
    expect_output("Print(\"\");", "String(\"\")\n");
    expect_output("string s := \"hello\"; string t := s + \", \" + \"world\"; Print(t); Print(s);", "String(\"hello, world\")\nString(\"hello\")\n");
    expect_output("Print(\"\" + \"\"); Print(\"\" + \"a\" + \"\"); Print(\"\" == \"\"); Print(\"\" != \"\"); Print(\"\" == \" \");", "String(\"\")\nString(\"a\")\nBool(true)\nBool(false)\nBool(false)\n");
    expect_output("Print(\"a\" == \"a\"); Print(\"a\" == \"A\"); Print(\"a\" != \"b\"); Print(\"ab\" == \"a\" + \"b\"); Print(\"a \" == \"a\");", "Bool(true)\nBool(false)\nBool(true)\nBool(true)\nBool(false)\n");
    expect_output("Print(\"tab\\there\"); Print(\"q\\\"q\"); Print(\"b\\\\s\"); Print(\"l1\\nl2\"); Print(\"cr\\r\");",
                  "String(\"tab\\there\")\nString(\"q\\\"q\")\nString(\"b\\\\s\")\nString(\"l1\\nl2\")\nString(\"cr\\r\")\n"); // one line per value: escapes are printed back
    expect_output("Print(\"a\\nb\" == \"a\" + \"\\n\" + \"b\"); Print(\"\\\\\" == \"\\\\\"); Print(\"\\\"\" + \"\\\"\");", "Bool(true)\nBool(true)\nString(\"\\\"\\\"\")\n");
    expect_output("Print(\"caf\xc3\xa9 \xe2\x9c\x93\");", "String(\"caf\xc3\xa9 \xe2\x9c\x93\")\n"); // bytes pass through unchanged
    // Values are copied; strings in blocks, loops, conditions and functions.
    expect_output("string s := \"a\"; string t := s; t := t + \"b\"; Print(s); Print(t);", "String(\"a\")\nString(\"ab\")\n");
    expect_output("string s := \"\"; for (i := 0, i < 3, 1) { s := s + \"ab\"; }; Print(s);", "String(\"ababab\")\n");
    expect_output("string s := \"x\"; { string s := \"inner\"; Print(s); }; Print(s); if (s == \"x\") then { s := s + \"!\"; }; Print(s);", "String(\"inner\")\nString(\"x\")\nString(\"x!\")\n");
    expect_output("string s := \"go\"; int n := 0; while (s != \"gogogo\") { s := s + \"go\"; n := n + 1; }; Print(n);", "Int(2)\n");
    expect_output("function string greet(string who) { return \"Hello, \" + who + \"!\"; }; Print(greet(\"MyDSL\")); Print(greet(\"\"));", "String(\"Hello, MyDSL!\")\nString(\"Hello, !\")\n");
    expect_output("function string repeat(string s, int n) { if (n <= 0) then { return \"\"; }; return s + repeat(s, n - 1); }; Print(repeat(\"ab\", 3)); Print(repeat(\"x\", 0));", "String(\"ababab\")\nString(\"\")\n");
    expect_output("function string bump(string s) { s := s + \"!\"; return s; }; string a := \"a\"; Print(bump(a)); Print(a);", "String(\"a!\")\nString(\"a\")\n"); // by value
    expect_output("function bool is(string a, string b) { return a == b; }; Print(is(\"x\", \"x\")); Print(is(\"x\", \"y\"));", "Bool(true)\nBool(false)\n");
    expect_output("function string label(int n) { if (n < 0) then { return \"negative\"; }; if (n == 0) then { return \"zero\"; }; return \"positive\"; }; Print(label(-1)); Print(label(0)); Print(label(5));",
                  "String(\"negative\")\nString(\"zero\")\nString(\"positive\")\n");
    // Short-circuit still applies with string comparisons.
    expect_output("int z := 0; Print(\"a\" == \"a\" || 1 / z == 0); Print(\"a\" == \"b\" && 1 / z == 0);", "Bool(true)\nBool(false)\n");
    expect_runtime_error("int z := 0; Print(\"a\" == \"b\" || 1 / z == 0);", 1, 35, "Division by zero: 1 / 0");
    // The string length limit stops exponential growth with a clear error.
    expect_runtime_error("string s := \"x\"; for (i := 0, i < 25, 1) { s := s + s; }; Print(s);", 1, 51, "String length limit of 1048576 characters exceeded (the result of '+' would have 2097152 characters)");
    expect_output("string s := \"x\"; for (i := 0, i < 20, 1) { s := s + s; }; Print(s == s + \"\");", "Bool(true)\n"); // exactly at the limit is fine
    expect_runtime_error("function string grow(string s, int n) { if (n == 0) then { return s; }; return grow(s + s, n - 1); }; Print(grow(\"ab\", 30));", 1, 87, "String length limit of 1048576 characters exceeded (the result of '+' would have 2097152 characters) (in function 'grow' called from line 1, column 80 [19 times]; in function 'grow' called from line 1, column 109)");
    // The output limit counts the printed characters of strings as well.
    expect_runtime_error("string s := \"0123456789\"; Print(s); Print(s);", 1, 37, "Output limit of 30 bytes exceeded", "String(\"0123456789\")\n", {0, 30});
    // Value::str and quote_string.
    check(mydsl::Value::string("").str() == "String(\"\")" && mydsl::Value::string("a\"\\\n\t\r").str() == "String(\"a\\\"\\\\\\n\\t\\r\")", "Value::str for strings");
    check(mydsl::quote_string("plain") == "\"plain\"" && mydsl::quote_string("") == "\"\"", "quote_string");
    check(mydsl::Value::string("x").type.str() == "string" && mydsl::Value::string("x").type.rank == 0, "string values are scalars of type string");
    // --check lists string variables and signatures.
    {
        std::istringstream in; std::ostringstream out;
        mydsl::run("string s := \"a\"; function string f(string x) { return x; };", in, out, false, true);
        check(out.str() == "s: string\nf: function(string x) -> string\nType check passed: 2 top-level statement(s), 1 top-level variable(s), 1 function(s)\n", "--check with strings", out.str());
    }
    // --tokens and --ast print string text with escapes, on one line.
    {
        std::ostringstream tokens;
        mydsl::print_tokens(mydsl::lex("\"a\\nb\""), tokens);
        check(tokens.str() == "1:1 string \"a\\nb\"\n1:7 end \"<eof>\"\n", "print_tokens escapes string text", tokens.str());
        check(dump("Print(\"a\\nb\");") == "(print (string \"a\\nb\"))\n", "print_ast escapes string text");
    }
    // JSON: string tokens and nodes carry the decoded text (escaped for JSON) and the type.
    const std::string json = mydsl::debug_json("string s := \"a\\\"b\\n\";\nPrint(s + \"\");");
    check(valid_json(json) && json.find("\"error\":null") != std::string::npos, "string program json is valid and typed", json);
    check(json.find("{\"kind\":\"string\",\"text\":\"a\\\"b\\n\",\"begin\":{\"line\":1,\"column\":13},\"end\":{\"line\":1,\"column\":21}}") != std::string::npos, "string token json", json);
    check(json.find("{\"node\":\"expression\",\"op\":\"string\",\"name\":\"a\\\"b\\n\",\"type\":\"string\",") != std::string::npos, "string literal node json has the type", json);
    check(json.find("\"op\":\"binary\",\"name\":\"+\",\"type\":\"string\",") != std::string::npos, "concatenation node json has type string", json);
    check(json.find("\"op\":\"string\",\"name\":\"\",\"type\":\"string\",") != std::string::npos, "empty string node json", json);
    check(json.find("\"declaration\":\"string\"") != std::string::npos, "declaration json shows the string type", json);
    const std::string input_json = mydsl::debug_json("int a := Input(\"ip.txt\");");
    check(input_json.find("\"op\":\"string\",\"name\":\"ip.txt\",\"type\":\"string\",") != std::string::npos, "Input file name node json is typed as string", input_json);
    const std::string run = mydsl::run_json("string s := \"a\\\"b\"; Print(s + \"\\n\");", "", {});
    check(valid_json(run) && run.find("{\"output\":\"String(\\\"a\\\\\\\"b\\\\n\\\")\\n\",\"error\":null") != std::string::npos, "run_json output escapes the printed string", run);
    const std::string bad_json = mydsl::debug_json("string s := \"a\" + 1;");
    check(bad_json.find("\"error\":{\"stage\":\"Type\",\"line\":1,\"column\":17,\"message\":\"Operator '+' cannot be applied to string and int") != std::string::npos, "string type error json", bad_json);
    const std::string lex_json = mydsl::debug_json("string s := \"a\\q\";");
    check(lex_json.find("\"tokens\":[],\"ast\":null,\"error\":{\"stage\":\"Lexing\",\"line\":1,\"column\":15,") != std::string::npos, "escape error json", lex_json);
    // Fixture files.
    for (const char* name : {"string_plus_int", "string_comparison", "string_condition"}) {
        std::ifstream file(std::string("tests/type_errors/") + name + ".txt");
        std::string text{std::istreambuf_iterator<char>(file), {}};
        try { types_of(text); check(false, std::string("tests/type_errors/") + name + ".txt should fail"); }
        catch (const mydsl::Error& e) { check(e.stage == "Type", std::string("tests/type_errors/") + name + ".txt is a type error", e.what()); }
    }
}

void string_builtin_tests() {
    const std::string hint = " (MyDSL does not convert between string and other types)";
    // ----- type rules -----
    expect_types("string s := \"abc\"; int n := length(s); string t := substring(s, 1, 2); string u := to_string(n) + to_string(1.5) + to_string(true) + to_string(s);",
                 "n: int\ns: string\nt: string\nu: string\n");
    check(expr_type("Print(length(\"\"));") == "int" && expr_type("Print(substring(\"abc\", 0, 1));") == "string" && expr_type("Print(to_string(1));") == "string", "builtin result types");
    expect_types("int n := length(to_string(length(\"x\")));", "n: int\n");
    expect_types("function string head(string s) { if (length(s) == 0) then { return \"\"; }; return substring(s, 0, 1); }; Print(head(\"\"));", "");
    expect_type_error("int n := length(1);", 1, 10, "length expects a string but got int");
    expect_type_error("int n := length([1, 2]);", 1, 10, "length expects a string but got intvector 2");
    expect_type_error("int n := length(true);", 1, 10, "length expects a string but got bool");
    expect_type_error("int n := length();", 1, 10, "Function 'length' expects 1 argument but got 0");
    expect_type_error("int n := length(\"a\", \"b\");", 1, 10, "Function 'length' expects 1 argument but got 2");
    expect_type_error("string n := length(\"a\");", 1, 1, "Cannot initialise 'n' of type string with a value of type int" + hint);
    expect_type_error("string s := to_string([1, 2]);", 1, 13, "to_string expects an int, float, bool or string but got intvector 2 (only scalars can be converted)");
    expect_type_error("string s := to_string([[1.0]]);", 1, 13, "to_string expects an int, float, bool or string but got floatmatrix 1,1");
    expect_type_error("string s := to_string();", 1, 13, "Function 'to_string' expects 1 argument but got 0");
    expect_type_error("int n := to_string(1);", 1, 1, "Cannot initialise 'n' of type int with a value of type string" + hint);
    expect_type_error("int n := 1 + to_string(1);", 1, 12, "Operator '+' cannot be applied to int and string" + hint);
    expect_type_error("string s := substring(\"abc\", 1);", 1, 13, "Function 'substring' expects 3 arguments but got 2");
    expect_type_error("string s := substring(\"abc\", 1, 2, 3);", 1, 13, "Function 'substring' expects 3 arguments but got 4");
    expect_type_error("string s := substring(1, 2, 3);", 1, 13, "substring expects a string as its first argument but got int");
    expect_type_error("string s := substring([1], 0, 1);", 1, 13, "substring expects a string as its first argument but got intvector 1");
    expect_type_error("string s := substring(\"abc\", 1.0, 2);", 1, 30, "The start index of substring must be int but is float (MyDSL does not convert between int and float implicitly; use to_float(...) or to_int(...))");
    expect_type_error("string s := substring(\"abc\", \"1\", 2);", 1, 30, "The start index of substring must be int but is string" + hint);
    expect_type_error("string s := substring(\"abc\", 1, true);", 1, 33, "The count of substring must be int but is bool");
    expect_type_error("string s := substring(\"abc\", -1, 2);", 1, 30, "The start index of substring must not be negative (got -1)");
    expect_type_error("string s := substring(\"abc\", 0, -2);", 1, 33, "The count of substring must not be negative (got -2)");
    expect_type_error("string s := substring(\"abc\", - -1, -(1));", 1, 36, "The count of substring must not be negative (got -1)"); // constant folding of unary minus
    expect_type_error("int n := substring(\"abc\", 0, 1);", 1, 1, "Cannot initialise 'n' of type int with a value of type string" + hint);
    expect_type_error("function int length(string s) { return 1; };", 1, 1, "Cannot define function 'length': 'length' is a built-in function");
    expect_type_error("function string substring(string s) { return s; };", 1, 1, "Cannot define function 'substring': 'substring' is a built-in function");
    expect_type_error("function string to_string(int n) { return \"\"; };", 1, 1, "Cannot define function 'to_string': 'to_string' is a built-in function");
    expect_type_error("Print(length(Input()));", 1, 14, "Input() can only be used as the whole initializer");
    // ----- length: counts bytes, empty string is 0 -----
    expect_output("Print(length(\"\")); Print(length(\"abc\")); Print(length(\"a\\nb\")); Print(length(\"\\\"\")); Print(length(\"a\" + \"bc\")); Print(length(\"  \"));", "Int(0)\nInt(3)\nInt(3)\nInt(1)\nInt(3)\nInt(2)\n");
    expect_output("Print(length(\"caf\xc3\xa9\"));", "Int(5)\n"); // bytes, not code points
    expect_output("string s := \"\"; for (i := 0, i < 4, 1) { s := s + \"ab\"; }; Print(length(s)); Print(length(s) == 8);", "Int(8)\nBool(true)\n");
    // ----- substring: boundaries -----
    expect_output("string s := \"hello\"; Print(substring(s, 0, 5)); Print(substring(s, 1, 3)); Print(substring(s, 4, 1)); Print(substring(s, 0, 1));", "String(\"hello\")\nString(\"ell\")\nString(\"o\")\nString(\"h\")\n");
    expect_output("string s := \"hello\"; Print(substring(s, 5, 0)); Print(substring(s, 0, 0)); Print(substring(s, 2, 0)); Print(substring(\"\", 0, 0));", "String(\"\")\nString(\"\")\nString(\"\")\nString(\"\")\n");
    expect_output("string s := \"hello\"; Print(substring(s, 0, length(s))); Print(substring(s, length(s), 0)); Print(substring(s, 1, length(s) - 1));", "String(\"hello\")\nString(\"\")\nString(\"ello\")\n");
    expect_output("Print(substring(\"a\\nb\", 1, 1) == \"\\n\"); Print(substring(\"x\\\"y\", 1, 1));", "Bool(true)\nString(\"\\\"\")\n");
    expect_output("string s := \"hello\"; Print(substring(substring(s, 1, 4), 1, 2)); Print(substring(s, 0, 2) + substring(s, 2, 3) == s);", "String(\"ll\")\nBool(true)\n");
    // Invalid ranges are runtime errors naming the range; the string is unchanged.
    expect_runtime_error("string s := \"hello\"; int i := 6; Print(substring(s, i, 0));", 1, 40, "substring: start 6 is out of range for a string of length 5 (valid starts are 0..5)");
    expect_runtime_error("string s := \"hello\"; int i := -1; Print(substring(s, i, 0));", 1, 41, "substring: start -1 is out of range for a string of length 5 (valid starts are 0..5)");
    expect_runtime_error("string s := \"hello\"; int n := -1; Print(substring(s, 0, n));", 1, 41, "substring: count -1 must not be negative");
    expect_runtime_error("string s := \"hello\"; int n := 4; Print(substring(s, 2, n));", 1, 40, "substring: start 2 + count 4 exceeds the length 5 of the string (at most 3 characters are available from 2)");
    expect_runtime_error("string s := \"hello\"; Print(substring(s, 0, 6));", 1, 28, "substring: start 0 + count 6 exceeds the length 5 of the string (at most 5 characters are available from 0)");
    expect_runtime_error("string s := \"hello\"; Print(substring(s, 5, 1));", 1, 28, "substring: start 5 + count 1 exceeds the length 5 of the string (at most 0 characters are available from 5)");
    expect_runtime_error("Print(substring(\"\", 1, 0));", 1, 7, "substring: start 1 is out of range for a string of length 0 (valid starts are 0..0)");
    expect_runtime_error("int i := 1; Print(substring(\"\", 0, i));", 1, 19, "substring: start 0 + count 1 exceeds the length 0 of the string");
    expect_runtime_error("function string tail(string s) { return substring(s, 1, length(s)); }; Print(tail(\"ab\"));", 1, 41, "substring: start 1 + count 2 exceeds the length 2 of the string (at most 1 characters are available from 1) (in function 'tail' called from line 1, column 78)");
    // ----- to_string: the literal text of a scalar (what Print shows inside the parentheses) -----
    expect_output("Print(to_string(42)); Print(to_string(-7)); Print(to_string(0)); Print(to_string(9223372036854775807)); Print(to_string(-9223372036854775807 - 1));",
                  "String(\"42\")\nString(\"-7\")\nString(\"0\")\nString(\"9223372036854775807\")\nString(\"-9223372036854775808\")\n");
    expect_output("Print(to_string(2.5)); Print(to_string(3.0)); Print(to_string(-0.5)); Print(to_string(1e20)); Print(to_string(1e-7)); Print(to_string(0.1 + 0.2)); Print(to_string(1.0 / 3.0));",
                  "String(\"2.5\")\nString(\"3.0\")\nString(\"-0.5\")\nString(\"1e+20\")\nString(\"1e-07\")\nString(\"0.3\")\nString(\"0.333333333333333\")\n");
    expect_output("Print(to_string(true)); Print(to_string(false)); Print(to_string(1 < 2)); Print(to_string(\"\")); Print(to_string(\"a\\nb\"));", "String(\"true\")\nString(\"false\")\nString(\"true\")\nString(\"\")\nString(\"a\\nb\")\n");
    expect_output("int n := 7; float f := 2.0; Print(\"n=\" + to_string(n) + \", f=\" + to_string(f) + \", ok=\" + to_string(n > 5));", "String(\"n=7, f=2.0, ok=true\")\n");
    expect_output("Print(length(to_string(12345))); Print(substring(to_string(3.25), 0, 1) == \"3\"); Print(to_string(length(\"abcd\")) == \"4\");", "Int(5)\nBool(true)\nBool(true)\n");
    expect_output("function string repeat(string s, int n) { if (n == 0) then { return \"\"; }; return s + repeat(s, n - 1); }; Print(length(repeat(\"ab\", 100)));", "Int(200)\n");
    // to_string and Print agree, and the text reads back as a literal of the same type.
    expect_output("float f := 1.0 / 3.0; Print(f); Print(to_string(f)); int big := 3037000499 * 3037000499; Print(to_string(big) == \"9223372030926249001\");", "Float(0.333333333333333)\nString(\"0.333333333333333\")\nBool(true)\n");
    expect_output("float f := Input(); Print(to_string(f));", "String(\"2.5\")\n", "2.5");
    // ----- Input() of strings: a double-quoted literal, like every other value -----
    expect_output("string s := Input(); Print(s);", "String(\"hello world\")\n", "\"hello world\"");
    expect_output("string s := Input(); Print(s); Print(length(s));", "String(\"\")\nInt(0)\n", "\"\"");
    expect_output("string s := Input(); Print(s);", "String(\"two\\nlines \\\"quoted\\\" back\\\\slash\\ttab\")\n", "\"two\\nlines \\\"quoted\\\" back\\\\slash\\ttab\""); // source escapes
    expect_output("string s := Input(); Print(s);", "String(\"  padded  \")\n", "\"  padded  \""); // spaces inside the quotes are kept
    expect_output("string a := Input(); string b := Input(); Print(a); Print(b);", "String(\"a\")\nString(\"b\")\n", "\"a\" \"b\""); // several values per line
    expect_output("string a := Input(); string b := Input(); Print(a + b);", "String(\"ab\")\n", "\n\n  \"a\"\n\n\"b\"\n\n"); // empty lines are skipped
    expect_output("string a := Input(); string b := Input(); Print(a + b);", "String(\"ab\")\n", "\"a\" // note\n/* block */ \"b\""); // comments are skipped
    expect_output("string a := Input(); Print(a);", "String(\"// not a comment\")\n", "\"// not a comment\"");
    expect_output("string s := Input(); Print(s); Print(length(s));", "String(\"caf\xc3\xa9\")\nInt(5)\n", "\"caf\xc3\xa9\"");
    // Mixed types: each Input() takes the next value, which must have the target's type.
    expect_output("int n := Input(); string s := Input(); float f := Input(); bool b := Input(); intvector 2 v := Input(); string t := Input(); Print(to_string(n) + s + to_string(f) + to_string(b)); Print(v); Print(t);",
                  "String(\"1x2.5true\")\nIntVector[3, 4]\nString(\"y\")\n", "1 \"x\" 2.5 true [3, 4] \"y\"");
    expect_output("string s := Input(); int n := Input(); Print(substring(s, 0, n));", "String(\"hel\")\n", "\"hello\" 3");
    expect_output("function string next() { string s := Input(); return s; }; Print(next() + next());", "String(\"ab\")\n", "\"a\" \"b\"");
    expect_output("function string read() { return Input(); }; string s := read(); Print(length(s));", "Int(3)\n", "\"abc\"");
    expect_output("string s := \"\"; s := Input(); Print(s);", "String(\"later\")\n", "\"later\"");
    // Errors: bare text, numbers, wrong type, malformed literal, end of input, missing file.
    expect_runtime_error("string s := Input();", 1, 13, "Invalid data in standard input at line 1, column 1: expected a string in double quotes (e.g. \"text\"), not bare text but found 'hello'", "", {}, "hello world");
    expect_runtime_error("string s := Input();", 1, 13, "expected a string in double quotes (e.g. \"text\"), not bare text but found '42'", "", {}, "42");
    expect_runtime_error("string s := Input();", 1, 13, "expected a string in double quotes (e.g. \"text\"), not bare text but found '2.5'", "", {}, "2.5");
    expect_runtime_error("string s := Input();", 1, 13, "expected a string in double quotes (e.g. \"text\"), not bare text but found 'true'", "", {}, "true");
    expect_runtime_error("string s := Input();", 1, 13, "expected a string in double quotes (e.g. \"text\") but found '['", "", {}, "[1, 2]");
    expect_runtime_error("string s := Input();", 1, 13, "Invalid data in standard input at line 1, column 1: Unterminated string literal", "", {}, "\"open");
    expect_runtime_error("string s := Input();", 1, 13, "Invalid data in standard input at line 1, column 6: Unknown escape sequence '\\q'", "", {}, "\"bad \\q\"");
    expect_runtime_error("string s := Input();", 1, 13, "Unexpected end of standard input: expected a string in double quotes (e.g. \"text\")", "", {}, "");
    expect_runtime_error("string s := Input();", 1, 13, "Unexpected end of standard input: expected a string", "", {}, "   \n// only a comment\n");
    expect_runtime_error("string a := Input(); Print(a); string b := Input();", 1, 44, "Unexpected end of standard input: expected a string", "String(\"a\")\n", {}, "\"a\"");
    expect_runtime_error("int n := Input();", 1, 10, "Invalid data in standard input at line 1, column 1: expected an int but found the string \"42\"", "", {}, "\"42\"");
    expect_runtime_error("float f := Input();", 1, 12, "expected a number but found the string \"2.5\"", "", {}, "\"2.5\"");
    expect_runtime_error("bool b := Input();", 1, 11, "expected true or false but found the string \"true\"", "", {}, "\"true\"");
    expect_runtime_error("intvector 2 v := Input();", 1, 18, "expected a value of type intvector 2 starting with '[' but found the string \"x\"", "", {}, "\"x\"");
    expect_runtime_error("intvector 2 v := Input();", 1, 18, "Invalid data in standard input at line 1, column 2: expected an int but found the string \"1\"", "", {}, "[\"1\", 2]");
    expect_runtime_error("string s := Input(\"does/not/exist.txt\");", 1, 13, "Cannot open input file 'does/not/exist.txt' (paths are relative to the working directory)");
    expect_runtime_error("string s := Input(\"ip.txt\");", 1, 13, "Invalid data in file 'ip.txt' at line 1, column 1: expected a string in double quotes (e.g. \"text\"), not bare text but found '23.5'");
    // Files: read sequentially, strings and numbers mixed, EOF reported with the file name.
    expect_output("string a := Input(\"tests/data/names.txt\"); string b := Input(\"tests/data/names.txt\"); string e := Input(\"tests/data/names.txt\"); int n := Input(\"tests/data/names.txt\"); Print(a); Print(b); Print(length(e)); Print(n);",
                  "String(\"Ada Lovelace\")\nString(\"Alan Turing\")\nInt(0)\nInt(3)\n");
    expect_runtime_error("string a := Input(\"tests/data/names.txt\"); string b := Input(\"tests/data/names.txt\"); string c := Input(\"tests/data/names.txt\"); int n := Input(\"tests/data/names.txt\"); string d := Input(\"tests/data/names.txt\");", 1, 182,
                         "Unexpected end of file 'tests/data/names.txt': expected a string in double quotes (e.g. \"text\")");
    expect_output("string a := Input(\"tests/data/names.txt\"); string s := Input(); Print(substring(a, 0, 3) + s);", "String(\"Ada!\")\n", "\"!\""); // a file and stdin side by side
    // JSON / run_json with string input through the browser's path.
    const std::string run = mydsl::run_json("string s := Input(); Print(length(s)); Print(s);", "\"a b\"", {});
    check(valid_json(run) && run.find("{\"output\":\"Int(3)\\nString(\\\"a b\\\")\\n\",\"error\":null") != std::string::npos, "run_json reads a string from the input text", run);
    const std::string bad = mydsl::run_json("string s := Input();", "plain", {});
    check(bad.find("\"message\":\"Invalid data in standard input at line 1, column 1: expected a string in double quotes (e.g. \\\"text\\\"), not bare text but found 'plain'\"") != std::string::npos, "run_json string input error", bad);
    const std::string typed = mydsl::debug_json("string s := Input(); Print(substring(to_string(length(s)), 0, 1));");
    check(valid_json(typed) && typed.find("\"op\":\"call\",\"name\":\"substring\",\"type\":\"string\"") != std::string::npos &&
          typed.find("\"op\":\"call\",\"name\":\"to_string\",\"type\":\"string\"") != std::string::npos &&
          typed.find("\"op\":\"call\",\"name\":\"length\",\"type\":\"int\"") != std::string::npos &&
          typed.find("\"op\":\"call\",\"name\":\"Input\",\"type\":\"string\"") != std::string::npos, "builtin call types in json", typed);
}

void inspector_tests() {
    // run_json reports the top-level variables exactly as the interpreter holds them at the end.
    const std::string scalars = mydsl::run_json("int a := 1; a := a + 41; float f := 2.5; bool ok := a == 42; string s := \"x\"; s := s + \"y\";", "", {});
    check(valid_json(scalars) && scalars.find("\"variables\":[{\"name\":\"a\",\"type\":\"int\",\"value\":\"Int(42)\"},{\"name\":\"f\",\"type\":\"float\",\"value\":\"Float(2.5)\"},"
                                             "{\"name\":\"ok\",\"type\":\"bool\",\"value\":\"Bool(true)\"},{\"name\":\"s\",\"type\":\"string\",\"value\":\"String(\\\"xy\\\")\"}],\"millis\":") != std::string::npos,
          "run_json variables: scalars after updates", scalars);
    const std::string tensors = mydsl::run_json("intvector 2 v := [1, 2]; v[0] := 9; floatmatrix 2,2 m := [[1., 2.], [3., 4.]]; m := transpose(m); m[0, 1] := 7.5;", "", {});
    check(tensors.find("\"variables\":[{\"name\":\"m\",\"type\":\"floatmatrix 2,2\",\"value\":\"FloatMatrix[\\n  [1.0, 7.5]\\n  [2.0, 4.0]\\n]\"},{\"name\":\"v\",\"type\":\"intvector 2\",\"value\":\"IntVector[9, 2]\"}]") != std::string::npos,
          "run_json variables: vectors and matrices after element updates", tensors);
    check(mydsl::run_json("Print(1);", "", {}).find("\"variables\":[],\"millis\"") != std::string::npos, "run_json: no variables");
    check(mydsl::run_json("int a := ;", "", {}).find("\"variables\":[]") != std::string::npos && mydsl::run_json("int a := true;", "", {}).find("\"variables\":[]") != std::string::npos,
          "run_json: static errors leave no variables");
    // Blocks, loops and functions: only top-level variables, with the values after all calls.
    const std::string scoped = mydsl::run_json("int a := 0; { int inner := 5; a := inner; }; for (i := 0, i < 3, 1) { a := a + i; };\n"
                                               "function int twice(int n) { int local := n * 2; return local; }; int b := twice(a); string s := Input();", "\"in\"", {});
    check(scoped.find("\"variables\":[{\"name\":\"a\",\"type\":\"int\",\"value\":\"Int(8)\"},{\"name\":\"b\",\"type\":\"int\",\"value\":\"Int(16)\"},{\"name\":\"s\",\"type\":\"string\",\"value\":\"String(\\\"in\\\")\"}]") != std::string::npos,
          "run_json variables: globals only (no block locals, loop variables, parameters or function locals)", scoped);
    const std::string recursion = mydsl::run_json("function int fib(int n) { if (n < 2) then { return n; }; return fib(n - 1) + fib(n - 2); }; int f := fib(15); int g := f - fib(14);", "", {});
    check(recursion.find("\"variables\":[{\"name\":\"f\",\"type\":\"int\",\"value\":\"Int(610)\"},{\"name\":\"g\",\"type\":\"int\",\"value\":\"Int(233)\"}]") != std::string::npos, "run_json variables after recursion", recursion);
    // A runtime error: the values at the moment of the error (a failing declaration never binds).
    const std::string failed = mydsl::run_json("int done := 0;\nfunction int inner(int x) {\n  return 10 / x;\n};\nfunction int outer(int x) {\n  return inner(x - 1);\n};\ndone := outer(2);\nint r := outer(1);\ndone := 99;", "", {});
    check(failed.find("\"error\":{\"stage\":\"Runtime\",\"line\":3,\"column\":13,\"message\":\"Division by zero: 10 / 0\",\"trace\":[{\"function\":\"inner\",\"line\":6,\"column\":10,\"repeats\":1},"
                      "{\"function\":\"outer\",\"line\":9,\"column\":10,\"repeats\":1}],\"omitted\":0},\"variables\":[{\"name\":\"done\",\"type\":\"int\",\"value\":\"Int(10)\"}]") != std::string::npos,
          "run_json: structured trace (innermost first) and variables at the time of the error", failed);
    const std::string deep = mydsl::run_json("function int d(int n) { return d(n + 1); }; int a := 5; Print(d(0));", "", {});
    check(deep.find("\"trace\":[{\"function\":\"d\",\"line\":1,\"column\":32,\"repeats\":999},{\"function\":\"d\",\"line\":1,\"column\":63,\"repeats\":1}],\"omitted\":0},\"variables\":[{\"name\":\"a\",\"type\":\"int\",\"value\":\"Int(5)\"}]") != std::string::npos,
          "run_json: collapsed recursive frames", deep);
    const std::string mutual = mydsl::run_json("function int a(int n) { if (n == 0) then { return 1 / n; }; return b(n - 1); };\nfunction int b(int n) { return a(n); };\nPrint(a(3));", "", {});
    check(mutual.find("\"trace\":[{\"function\":\"a\",\"line\":2,\"column\":32,\"repeats\":1},{\"function\":\"b\",\"line\":1,\"column\":68,\"repeats\":1},{\"function\":\"a\",\"line\":2,\"column\":32,\"repeats\":1},"
                      "{\"function\":\"b\",\"line\":1,\"column\":68,\"repeats\":1},{\"function\":\"a\",\"line\":2,\"column\":32,\"repeats\":1}],\"omitted\":2}") != std::string::npos,
          "run_json: at most five frames, the rest counted as omitted", mutual);
    check(mydsl::run_json("Print(1 / 0);", "", {}).find("\"message\":\"Division by zero: 1 / 0\"},\"variables\":[]") != std::string::npos, "run_json: no trace field outside functions");
    // The Error object carries the same trace; what() renders it as before (CLI output unchanged).
    try { output_of("function int inner(int x) { return 10 / x; };\nfunction int outer(int x) { return inner(x - 1); };\nPrint(outer(1));"); check(false, "trace fields"); }
    catch (const mydsl::Error& e) {
        check(e.message == "Division by zero: 10 / 0" && e.trace.size() == 2 && e.trace[0].function == "inner" && e.trace[0].call_site.line == 2 && e.trace[0].call_site.column == 36 &&
              e.trace[1].function == "outer" && e.trace[1].call_site.line == 3 && e.trace[1].call_site.column == 7 && e.omitted_frames == 0, "Error::trace fields");
        check(std::string(e.what()) == "Runtime error at line 1, column 39: Division by zero: 10 / 0 (in function 'inner' called from line 2, column 36; in function 'outer' called from line 3, column 7)", "Error::what renders the trace", e.what());
    }
    try { output_of("Print(1 / 0);"); check(false, "no trace"); }
    catch (const mydsl::Error& e) { check(e.trace.empty() && e.omitted_frames == 0 && e.message == "Division by zero: 1 / 0", "no trace outside functions"); }
    {
        mydsl::CallFrame f; f.function = "f"; f.call_site = {3, 4}; f.repeats = 2;
        mydsl::CallFrame g; g.function = "g"; g.call_site = {1, 2}; g.file = "lib/a.mydsl";
        const std::vector<mydsl::CallFrame> single{f}, nested{g, f};
        check(mydsl::render_trace({}, 0).empty() && mydsl::render_trace(single, 5) == " (in function 'f' called from line 3, column 4 [2 times]; ... 5 more call(s))" &&
              mydsl::render_trace(nested, 0) == " (in function 'g' called from line 1, column 2 of 'lib/a.mydsl'; in function 'f' called from line 3, column 4 [2 times])", "render_trace");
    }
    // interpret() hands back the state through the optional pointer, also when it throws.
    {
        auto program = mydsl::parse("int a := 1; intvector 2 v := [1, 2]; a := 2; v[1] := 5; int z := 1 / 0; a := 3;");
        mydsl::typecheck(program);
        std::istringstream in; std::ostringstream out;
        std::map<std::string, mydsl::Value> state;
        try { mydsl::interpret(program, in, out, {}, &state); check(false, "state on failure"); }
        catch (const mydsl::Error&) {
            check(state.size() == 2 && state.at("a").str() == "Int(2)" && state.at("v").str() == "IntVector[1, 5]", "interpret() state at the moment of the error");
        }
        auto ok = mydsl::parse("string s := \"a\"; s := s + \"b\";");
        mydsl::typecheck(ok);
        const auto returned = mydsl::interpret(ok, in, out, {}, &state);
        check(returned.size() == 1 && state.size() == 1 && state.at("s").str() == "String(\"ab\")" && returned.at("s").str() == state.at("s").str(), "interpret() state on success equals the result");
    }
}

void function_runtime_tests() {
    const std::string square = "function int square(int x) { return x * x; };\n";
    const std::string fib = "function int fib(int n) { if (n < 2) then { return n; }; return fib(n - 1) + fib(n - 2); };\n";
    const std::string fact = "function int fact(int n) { if (n <= 1) then { return 1; } else { return n * fact(n - 1); }; };\n";
    // ----- normal calls: arguments, return values, results used in expressions -----
    expect_output(square + "Print(square(4));", "Int(16)\n");
    expect_output(square + "int a := square(3); Print(a + square(a)); Print(square(square(2)) == 16);", "Int(90)\nBool(true)\n");
    expect_output("function int answer() { return 42; }; Print(answer());", "Int(42)\n");
    expect_output("function int add(int a, int b) { return a + b; }; Print(add(1, 2)); Print(add(add(1, 2), add(3, 4)));", "Int(3)\nInt(10)\n");
    expect_output("function float half(float x) { return x / 2.0; }; Print(half(5.0)); Print(half(half(1.0)));", "Float(2.5)\nFloat(0.25)\n");
    expect_output("function bool both(bool a, bool b) { return a && b; }; Print(both(true, false)); Print(both(true, true));", "Bool(false)\nBool(true)\n");
    expect_output("function float mixed(int i, float f, bool b) { if (b) then { return to_float(i) + f; }; return f; }; Print(mixed(2, 0.5, true)); Print(mixed(2, 0.5, false));", "Float(2.5)\nFloat(0.5)\n");
    // Arguments are evaluated left to right in the caller's scope, before the body runs.
    expect_output("function int first(int a, int b) { return a; }; function int next() { int v := Input(); return v; }; Print(first(next(), next())); Print(next());", "Int(1)\nInt(3)\n", "1 2 3");
    // Vectors and matrices as parameters and results; builtins on call results.
    expect_output("function intvector 3 rev(intvector 3 v) { return inv_vec(v); }; Print(rev([1, 2, 3])); Print(rev(rev([1, 2, 3]))[0]);", "IntVector[3, 2, 1]\nInt(1)\n");
    expect_output("function float sum(floatvector 3 v) { float s := 0.0; for (i := 0, i < 3, 1) { s := s + v[i]; }; return s; }; Print(sum([1.5, 2.5, 3.0]));", "Float(7.0)\n");
    expect_output("function floatmatrix 2,2 id() { return [[1., 0.], [0., 1.]]; }; floatmatrix 2,2 m := [[1., 2.], [3., 4.]]; Print(mult_mat(m, id()) == m); Print(determinant(id()));", "Bool(true)\nFloat(1.0)\n");
    expect_output("function intmatrix 2,2 sq(intmatrix 2,2 m) { return mult_mat(m, m); }; Print(sq([[1, 1], [1, 0]])); Print(transpose(sq(sq([[0, 1], [1, 1]]))));",
                  "IntMatrix[\n  [2, 1]\n  [1, 1]\n]\nIntMatrix[\n  [2, 3]\n  [3, 5]\n]\n");
    expect_output("function bool zero(intvector 2 v) { return is_zero(v); }; Print(zero([0, 0])); Print(zero([0, 1]));", "Bool(true)\nBool(false)\n");
    // Calls anywhere an expression is allowed: conditions, indices, loop headers, Print, Input targets.
    expect_output(square + "if (square(3) > 8) then { Print(1); } else { Print(0); }; intvector 3 v := [7, 8, 9]; Print(v[square(1)]); for (i := square(1), i < square(2), square(1)) { Print(i); };", "Int(1)\nInt(8)\nInt(1)\nInt(2)\nInt(3)\n");
    expect_output(square + "int i := 0; while (square(i) < 10) { i := i + 1; }; Print(i);", "Int(4)\n");
    expect_output("function int twice() { int a := Input(); return 2 * a; }; Print(twice()); Print(twice());", "Int(6)\nInt(10)\n", "3 5");
    expect_output("function float r() { return Input(\"ip.txt\"); }; Print(r());", "Float(23.5)\n");
    // Definitions may follow their use and do nothing when reached.
    expect_output("Print(square(5));\n" + square, "Int(25)\n");
    expect_output("Print(1); " + square + "Print(2);", "Int(1)\nInt(2)\n");
    // ----- scoping: by-value arguments, locals, no access to globals, parameters as locals -----
    expect_output("int x := 1; function int bump(int x) { x := x + 10; return x; }; Print(bump(x)); Print(x);", "Int(11)\nInt(1)\n");
    expect_output("intvector 2 v := [1, 2]; function int poke(intvector 2 v) { v[0] := 99; return v[0]; }; Print(poke(v)); Print(v);", "Int(99)\nIntVector[1, 2]\n");
    expect_output("int g := 5; function int f(int g) { return g * 2; }; Print(f(1)); Print(g);", "Int(2)\nInt(5)\n"); // the parameter shadows nothing: globals are invisible anyway
    expect_output("function int f(int x) { int y := x + 1; { int y := 100; x := y; }; return x + y; }; Print(f(1));", "Int(102)\n");
    expect_output("function int counter() { int c := 0; c := c + 1; return c; }; Print(counter()); Print(counter()); Print(counter());", "Int(1)\nInt(1)\nInt(1)\n"); // locals are fresh every call
    expect_output("function int f(int n) { int local := n * 10; return local; }; Print(f(1)); Print(f(2)); int local := 7; Print(local);", "Int(10)\nInt(20)\nInt(7)\n");
    expect_output("int a := 1; function int f() { int a := 2; return a; }; Print(f()); Print(a); a := f() + a; Print(a);", "Int(2)\nInt(1)\nInt(3)\n");
    expect_output("function int outer(int x) { return inner(x) + inner(x); }; function int inner(int x) { int x2 := x * x; return x2; }; Print(outer(3));", "Int(18)\n");
    // A function's loop variable and blocks do not leak into the caller's scope.
    expect_output("int i := 100; function int loop() { int s := 0; for (i := 0, i < 3, 1) { s := s + i; }; return s; }; Print(loop()); Print(i);", "Int(3)\nInt(100)\n");
    // ----- early returns: from nested ifs, blocks, while and for loops; later code does not run -----
    expect_output("function int sign(int x) { if (x < 0) then { return -1; }; if (x > 0) then { return 1; }; return 0; }; Print(sign(-5)); Print(sign(7)); Print(sign(0));", "Int(-1)\nInt(1)\nInt(0)\n");
    expect_output("function int f(int x) { { { if (x > 0) then { return 1; }; }; Print(x); }; return 0; }; Print(f(1)); Print(f(-1));", "Int(1)\nInt(-1)\nInt(0)\n");
    expect_output("function int find(intvector 5 v, int k) { for (i := 0, i < 5, 1) { if (v[i] == k) then { return i; }; }; return -1; }; Print(find([4, 8, 15, 16, 23], 15)); Print(find([4, 8, 15, 16, 23], 42));", "Int(2)\nInt(-1)\n");
    expect_output("function int count(int n) { int c := 0; while (true) { c := c + 1; if (c >= n) then { return c; }; }; return 0; }; Print(count(7));", "Int(7)\n");
    expect_output("function int f() { for (i := 0, i < 10, 1) { Print(i); if (i == 1) then { return i * 100; }; }; return -1; }; Print(f());", "Int(0)\nInt(1)\nInt(100)\n");
    expect_output("function int nested() { int n := 0; for (i := 0, i < 3, 1) { for (j := 0, j < 3, 1) { n := n + 1; if (j == 1) then { return n; }; }; }; return -1; }; Print(nested());", "Int(2)\n");
    expect_output("function int f() { while (true) { while (true) { return 1; }; }; return 2; }; Print(f());", "Int(1)\n");
    expect_output("function int f(int x) { if (x > 0) then { return 1; } else { return -1; }; }; Print(f(3)); Print(f(-3));", "Int(1)\nInt(-1)\n");
    expect_output("function int f() { return 1; Print(999); }; Print(f());", "Int(1)\n"); // nothing after a return runs
    expect_output("function int f() { { return 1; }; Print(999); return 2; }; Print(f());", "Int(1)\n");
    // The return value is computed once, at the return statement.
    expect_output("function int f() { int a := Input(); return a * a; }; Print(f() + f());", "Int(13)\n", "2 3");
    // ----- recursion -----
    expect_output(fact + "Print(fact(0)); Print(fact(1)); Print(fact(5)); Print(fact(20));", "Int(1)\nInt(1)\nInt(120)\nInt(2432902008176640000)\n");
    expect_output(fib + "Print(fib(0)); Print(fib(1)); Print(fib(10)); Print(fib(20));", "Int(0)\nInt(1)\nInt(55)\nInt(6765)\n");
    expect_output("function int gcd(int a, int b) { if (b == 0) then { return a; }; return gcd(b, a % b); }; Print(gcd(48, 18)); Print(gcd(17, 5));", "Int(6)\nInt(1)\n");
    expect_output("function int sum(int n) { if (n == 0) then { return 0; }; return n + sum(n - 1); }; Print(sum(100)); Print(sum(999));", "Int(5050)\nInt(499500)\n"); // 999 levels: just inside the limit
    expect_output("function bool even(int n) { if (n == 0) then { return true; }; return odd(n - 1); }; function bool odd(int n) { if (n == 0) then { return false; }; return even(n - 1); }; Print(even(10)); Print(odd(10)); Print(even(7));",
                  "Bool(true)\nBool(false)\nBool(false)\n");
    expect_output("function int ack(int m, int n) { if (m == 0) then { return n + 1; }; if (n == 0) then { return ack(m - 1, 1); }; return ack(m - 1, ack(m, n - 1)); }; Print(ack(2, 3));", "Int(9)\n");
    expect_output("function floatmatrix 2,2 pow(floatmatrix 2,2 m, int n) { if (n == 0) then { return [[1., 0.], [0., 1.]]; }; return mult_mat(m, pow(m, n - 1)); }; Print(pow([[1., 1.], [1., 0.]], 10));",
                  "FloatMatrix[\n  [89.0, 55.0]\n  [55.0, 34.0]\n]\n");
    // Each recursive call has its own locals and parameters.
    expect_output("function int f(int n) { int local := n; if (n > 0) then { int ignored := f(n - 1); }; return local; }; Print(f(5));", "Int(5)\n");
    // ----- runtime failures inside functions: location inside the body plus the call trace -----
    expect_runtime_error("function float half(float x, float d) {\n  return x / d;\n};\nPrint(half(1.0, 2.0));\nPrint(half(1.0, 0.0));", 2, 12,
                         "Division by zero: Float(1.0) / Float(0.0) (in function 'half' called from line 5, column 7)", "Float(0.5)\n");
    expect_runtime_error("function int inner(int x) { return 10 / x; };\nfunction int middle(int x) { return inner(x - 1); };\nfunction int outer(int x) { return middle(x); };\nPrint(outer(1));", 1, 39,
                         "Division by zero: 10 / 0 (in function 'inner' called from line 2, column 37; in function 'middle' called from line 3, column 36; in function 'outer' called from line 4, column 7)");
    expect_runtime_error("function int at(intvector 3 v, int i) { return v[i]; }; Print(at([1, 2, 3], 3));", 1, 50, "Index 3 is out of range for intvector 3 (valid indices are 0..2) (in function 'at' called from line 1, column 63)");
    expect_runtime_error("function int dbl(int x) { return x * 2; }; int big := 4611686018427387904; Print(dbl(big));", 1, 36, "Integer overflow: 4611686018427387904 * 2 does not fit in a 64-bit int (range -9223372036854775808 .. 9223372036854775807) (in function 'dbl' called from line 1, column 82)");
    expect_runtime_error("function int f(int x) { Print(x); return x / (x - 2); }; Print(f(1)); Print(f(2));", 1, 44, "Division by zero: 2 / 0 (in function 'f' called from line 1, column 77)", "Int(1)\nInt(-1)\nInt(2)\n");
    expect_runtime_error("function int f() { return Input(); }; Print(f());", 1, 27, "Unexpected end of standard input: expected an int (in function 'f' called from line 1, column 45)");
    expect_runtime_error("function float a(float x) { return abs(inverse_mat([[x, 2.], [2., 4.]])[0, 0]); }; Print(a(1.0));", 1, 40, "inverse_mat: the matrix is singular (its determinant is 0), so it has no inverse (in function 'a' called from line 1, column 90)");
    // Errors while evaluating the arguments happen in the caller (no trace).
    expect_runtime_error(square + "Print(square(1 / 0));", 2, 16, "Division by zero: 1 / 0");
    {   // The trace is exact: stage, location and message fields.
        try { output_of("function int f(int x) {\n  return x / 0;\n};\nPrint(f(1));"); check(false, "traced error fields"); }
        catch (const mydsl::Error& e) {
            check(e.stage == "Runtime" && e.location.line == 2 && e.location.column == 12 && e.message == "Division by zero: 1 / 0" && e.trace.size() == 1 && e.trace[0].function == "f" && e.trace[0].call_site.line == 4 && e.trace[0].call_site.column == 7 &&
                  std::string(e.what()) == "Runtime error at line 2, column 12: Division by zero: 1 / 0 (in function 'f' called from line 4, column 7)", "traced error fields", e.what());
        }
    }
    // Recursion traces collapse repeated frames and cut off after a few entries.
    expect_runtime_error("function int a(int n) { if (n == 0) then { return 1 / n; }; return b(n - 1); };\nfunction int b(int n) { return a(n); };\nPrint(a(3));", 1, 53,
                         "Division by zero: 1 / 0 (in function 'a' called from line 2, column 32; in function 'b' called from line 1, column 68; in function 'a' called from line 2, column 32; in function 'b' called from line 1, column 68; in function 'a' called from line 2, column 32; ... 2 more call(s))");
    expect_runtime_error("function int down(int n) {\n  if (n == 0) then { return 1 / n; };\n  return down(n - 1);\n};\nPrint(down(50));", 2, 31,
                         "Division by zero: 1 / 0 (in function 'down' called from line 3, column 10 [50 times]; in function 'down' called from line 5, column 7)");
    // ----- the recursion limit -----
    expect_runtime_error("function int down(int n) {\n  return down(n + 1);\n};\nPrint(down(0));", 2, 10,
                         "Call depth limit of 1000 exceeded while calling 'down' (is a recursive function missing its base case?) (in function 'down' called from line 2, column 10 [999 times]; in function 'down' called from line 4, column 7)");
    expect_runtime_error("function int sum(int n) { if (n == 0) then { return 0; }; return n + sum(n - 1); }; Print(sum(1000));", 1, 70, "Call depth limit of 1000 exceeded while calling 'sum'"); // one level too many
    expect_runtime_error("function bool even(int n) { if (n == 0) then { return true; }; return odd(n + 1); }; function bool odd(int n) { return even(n); }; Print(1); Print(even(1));", 1, 120,
                         "Call depth limit of 1000 exceeded while calling 'even' (is a recursive function missing its base case?) (in function 'odd' called from line 1, column 71; in function 'even' called from line 1, column 120; in function 'odd' called from line 1, column 71; in function 'even' called from line 1, column 120; in function 'odd' called from line 1, column 71; ... 995 more call(s))", "Int(1)\n");
    // After the failure nothing else runs, and the interpreter can be reused for another program.
    expect_output("function int d(int n) { return d(n + 1); }; Print(7);", "Int(7)\n");
    // Deeply nested statements inside deep recursion hit the nesting guard instead of the C++ stack.
    {
        std::string nested = "function int f(int n) { if (n == 0) then { return 0; }; " + std::string(200, '{') + " return f(n - 1) + 1; ";
        for (int i = 0; i < 200; ++i) nested += "}; ";
        expect_runtime_error(nested + "}; Print(f(999));", 1, 161, "Evaluation nesting limit of 20000 exceeded (statements and expressions are nested too deeply, probably through recursion) (in function 'f' called from line 1, column 265 [98 times]; in function 'f' called from line 1, column 888)");
    }
    // ----- limits inside functions: time and output -----
    expect_runtime_error("function int spin() { while (true) {}; return 1; }; Print(spin());", 1, 23, "Execution stopped after 50 ms (the time limit was exceeded; is there an infinite loop?) (in function 'spin' called from line 1, column 59)", "", {50.0, 0});
    expect_runtime_error("function int loud(int n) { Print(n); return n; }; int i := 0; while (true) { i := loud(i + 1); };", 1, 28, "Output limit of 30 bytes exceeded (in function 'loud' called from line 1, column 83)", "Int(1)\nInt(2)\nInt(3)\nInt(4)\n", {0, 30});
    // ----- the interpret() interface returns the top-level variables only -----
    {
        auto program = mydsl::parse("function int f(int p) { int local := p; return local; }; int a := f(1); int b := f(a + 1);");
        mydsl::typecheck(program);
        std::istringstream in; std::ostringstream out;
        const auto globals = mydsl::interpret(program, in, out);
        check(globals.size() == 2 && globals.at("a").str() == "Int(1)" && globals.at("b").str() == "Int(2)", "interpret() result holds the globals, not parameters or locals");
    }
}

// ----- modules: import syntax, the loader (tests/modules/ fixtures), errors with file and chain -----

std::string read_file(const std::string& path) {
    std::ifstream file(path);
    return std::string{std::istreambuf_iterator<char>(file), {}};
}

// Loads tests/modules/<name>.mydsl (or a file given with its path) through the loader.
mydsl::Modules load_fixture(const std::string& path) { return mydsl::load_modules(read_file(path), path); }

// Expects load_modules to fail with exactly `expected` as what() and the given structured fields.
void expect_load_error(const std::string& path, const std::string& stage, const std::string& file, std::size_t line, std::size_t column,
                       const std::string& expected, std::size_t chain = 0) {
    try {
        load_fixture(path);
        check(false, "load error for " + path, "loaded but an error was expected");
    } catch (const mydsl::Error& e) {
        check(e.what() == expected, "load error for " + path, "expected '" + expected + "'\n      got      '" + e.what() + "'");
        check(e.stage == stage && e.file == file && e.location.line == line && e.location.column == column && e.imports.size() == chain,
              "load error fields for " + path, "stage " + e.stage + ", file '" + e.file + "', line " + std::to_string(e.location.line) +
              ", column " + std::to_string(e.location.column) + ", " + std::to_string(e.imports.size()) + " import site(s)");
    } catch (const std::exception& e) {
        check(false, "load error for " + path, std::string("wrong exception type: ") + e.what());
    }
}

void module_syntax_tests() {
    // import "file";  is a top-level statement with the file name as a string literal.
    expect_ast("import \"math_utils.mydsl\";", "(import \"math_utils.mydsl\")\n");
    expect_ast("import \"lib/a.mydsl\"; import \"../b.mydsl\";\nPrint(f(1));", "(import \"lib/a.mydsl\")\n(import \"../b.mydsl\")\n(print (call f Int(1)))\n");
    expect_ast("import \"with \\\"quote\\\".mydsl\";", "(import \"with \\\"quote\\\".mydsl\")\n"); // the name is the unescaped text, printed re-escaped
    expect_ast("Print(1);\nimport \"late.mydsl\";\nfunction int f() { return 1; };", "(print Int(1))\n(import \"late.mydsl\")\n(function int f\n  (block\n    (return Int(1))))\n"); // anywhere at the top level, like functions
    {
        auto program = mydsl::parse("  import \"lib/m.mydsl\"  ;\n");
        const auto& s = program.at(0);
        check(s->op == "import" && s->name == "lib/m.mydsl" && s->declaration == std::nullopt && s->exprs.empty() && s->body.empty() && s->params.empty(), "import node fields");
        check(s->location.line == 1 && s->location.column == 3 && s->span.begin.column == 3 && s->span.end.line == 1 && s->span.end.column == 26, "import location and span through ';'");
    }
    check(mydsl::lex("import \"a\";").at(0).kind == mydsl::TokenKind::Word && mydsl::lex("import \"a\";").at(1).kind == mydsl::TokenKind::String, "import tokens: word + string literal");
    // Malformed imports are parse errors of the program that contains them.
    expect_error("import math_utils;", 1, 8, "Expected the file name as a string literal after 'import' (e.g. import \"math_utils.mydsl\";) but found 'math_utils'");
    expect_error("import;", 1, 7, "Expected the file name as a string literal after 'import' (e.g. import \"math_utils.mydsl\";) but found ';'");
    expect_error("import", 1, 7, "Expected the file name as a string literal after 'import' (e.g. import \"math_utils.mydsl\";) but found end of input");
    expect_error("import (\"a.mydsl\");", 1, 8, "Expected the file name as a string literal after 'import'");
    expect_error("import \"\";", 1, 8, "The file name of an import must not be empty (e.g. import \"math_utils.mydsl\";)");
    expect_error("import \"a.mydsl\"", 1, 17, "Expected ';' to end the import of \"a.mydsl\" but found end of input");
    expect_error("import \"a.mydsl\" \"b.mydsl\";", 1, 18, "Expected ';' to end the import of \"a.mydsl\" but found string literal \"b.mydsl\"");
    expect_error("import \"a.mydsl\", \"b.mydsl\";", 1, 17, "Expected ';' to end the import of \"a.mydsl\" but found ','");
    expect_error("import \"a.mydsl\";;", 1, 18, "Expected a statement (declaration, assignment, Print, error, block, if, while, for, function or import) but found ';'");
    expect_error("import \"unterminated;", 1, 8, "Unterminated string literal", "Lexing");
    expect_error("{ import \"a.mydsl\"; };", 1, 3, "An import can only appear at the top level of the program, not inside a block or loop");
    expect_error("while (true) { import \"a.mydsl\"; };", 1, 16, "An import can only appear at the top level of the program, not inside a block or loop");
    expect_error("if (true) then { import \"a.mydsl\"; };", 1, 18, "An import can only appear at the top level of the program, not inside a block or loop");
    expect_error("function int f() { import \"a.mydsl\"; return 1; };", 1, 20, "An import can only appear at the top level of the program, not inside a function body");
    // 'import' is a keyword.
    expect_error("int import := 1;", 1, 5, "Cannot use keyword 'import' as a variable name");
    expect_error("import := 1;", 1, 8, "Expected the file name as a string literal after 'import'");
    expect_error("x := import;", 1, 6, "Expected an expression but found keyword 'import'");
    expect_error("function int import() { return 1; };", 1, 14, "Cannot use keyword 'import' as a function name");
    expect_error("function int f(int import) { return 1; };", 1, 20, "Cannot use keyword 'import' as a parameter name");
    expect_ast("imports := 1; import' := 2;", "(assign imports Int(1))\n(assign import' Int(2))\n"); // only the exact word is reserved
}

void module_loader_tests() {
    // ----- normal imports: one level -----
    {
        const auto modules = load_fixture("tests/modules/basic.mydsl");
        check(modules.modules.size() == 3, "basic: three modules", std::to_string(modules.modules.size()));
        const auto& vectors = modules.modules.at(0);
        const auto& math = modules.modules.at(1);
        const auto& main = modules.modules.at(2);
        check(vectors.display == "tests/modules/lib/vectors.mydsl" && math.display == "tests/modules/lib/math_utils.mydsl" && main.display == "tests/modules/basic.mydsl",
              "basic: dependency order (imported modules first, main last)", vectors.display + ", " + math.display + ", " + main.display);
        check(&modules.main() == &main && main.main && !vectors.main && !math.main, "basic: main() is the last module and the only one flagged main");
        check(vectors.imports.empty() && vectors.functions() == 2 && vectors.statements() == 0, "basic: leaf module has no imports and two functions");
        check(math.imports.size() == 1 && math.imports[0].path == "vectors.mydsl" && math.imports[0].module == 0 &&
              math.imports[0].location.line == 2 && math.imports[0].location.column == 1, "basic: math_utils imports vectors (resolved relative to lib/)");
        check(main.imports.size() == 1 && main.imports[0].path == "lib/math_utils.mydsl" && main.imports[0].module == 1 && main.functions() == 0 && main.statements() == 1,
              "basic: main imports math_utils; its Print is an 'other' statement");
        check(main.program.size() == 2 && main.program[0]->op == "import" && main.program[1]->op == "print", "basic: the main AST keeps the import statement");
        check(math.program.at(1)->op == "function" && math.program.at(1)->name == "square" && math.program.at(2)->name == "mean", "basic: the imported AST was produced by the same parser");
        check(!vectors.path.empty() && vectors.path[0] == '/' && vectors.path.find("/tests/modules/lib/vectors.mydsl") != std::string::npos, "basic: Module::path is absolute", vectors.path);
        check(vectors.source == read_file("tests/modules/lib/vectors.mydsl") && main.source == read_file("tests/modules/basic.mydsl"), "basic: Module::source holds the file text");
    }
    // ----- nested imports: three levels -----
    {
        const auto modules = load_fixture("tests/modules/nested.mydsl");
        std::string order;
        for (const auto& m : modules.modules) order += (order.empty() ? "" : " < ") + m.display;
        check(order == "tests/modules/lib/vectors.mydsl < tests/modules/lib/math_utils.mydsl < tests/modules/lib/stats.mydsl < tests/modules/nested.mydsl",
              "nested: every module after the modules it imports", order);
        check(modules.modules.at(2).imports.at(0).module == 1 && modules.modules.at(1).imports.at(0).module == 0 && modules.main().imports.at(0).module == 2,
              "nested: import indices follow the chain");
    }
    // ----- diamond: a module reached twice is loaded once; importing it from two modules is no duplicate -----
    {
        const auto modules = load_fixture("tests/modules/diamond.mydsl");
        check(modules.modules.size() == 5, "diamond: vectors is loaded once", std::to_string(modules.modules.size()));
        std::size_t vector_modules = 0;
        for (const auto& m : modules.modules) vector_modules += m.display == "tests/modules/lib/vectors.mydsl";
        check(vector_modules == 1, "diamond: exactly one copy of vectors");
        const auto& main = modules.main();
        check(main.imports.size() == 3 && main.imports[1].path == "lib/vectors.mydsl" && main.imports[1].module == 0 &&
              modules.modules.at(1).imports.at(0).module == 0, "diamond: both importers refer to the same loaded module");
        check(main.functions() == 1 && main.statements() == 2, "diamond: main's own function and statements are counted");
    }
    // ----- relative resolution: "../lib/x" and "./x" relative to the importing file, not the working directory -----
    {
        const auto modules = load_fixture("tests/modules/app/main.mydsl");
        check(modules.modules.size() == 4 && modules.modules.at(2).display == "tests/modules/app/helpers.mydsl" &&
              modules.modules.at(2).imports.at(0).path == "../lib/vectors.mydsl" && modules.modules.at(2).imports.at(0).module == 0 &&
              modules.main().imports.at(1).path == "./helpers.mydsl" && modules.main().imports.at(1).module == 2,
              "relative: '../lib/...' and './...' resolve against the importing file's directory");
        const auto outer = load_fixture("tests/modules/relative.mydsl");
        check(outer.modules.size() == 3 && outer.modules.at(1).display == "tests/modules/app/helpers.mydsl" && outer.main().display == "tests/modules/relative.mydsl",
              "relative: the same chain one level deeper");
    }
    // ----- a module with top-level statements is loaded; what to do with them is the next step's decision -----
    {
        const auto modules = load_fixture("tests/modules/statements.mydsl");
        check(modules.modules.at(0).functions() == 1 && modules.modules.at(0).statements() == 2, "statements in an imported module are counted");
    }
    // ----- the main program from standard input (no path): imports are relative to the working directory -----
    {
        const auto modules = mydsl::load_modules("import \"tests/modules/lib/strings.mydsl\"; Print(greet(\"x\"));");
        check(modules.modules.size() == 2 && modules.main().path.empty() && modules.main().display.empty() && modules.main().main &&
              modules.modules.at(0).display == "tests/modules/lib/strings.mydsl", "stdin program: imports resolve relative to the working directory");
    }
    // ----- single-file programs: one module, nothing loaded -----
    {
        const auto modules = mydsl::load_modules("int a := 1; Print(a);");
        check(modules.modules.size() == 1 && modules.main().program.size() == 2 && modules.main().imports.empty() && modules.main().statements() == 2, "a program without imports is a single module");
        const auto named = mydsl::load_modules(read_file("inputs/functions.txt"), "inputs/functions.txt");
        check(named.modules.size() == 1 && named.main().display == "inputs/functions.txt" && named.main().functions() == 4, "a single-file program keeps its path");
        try { mydsl::load_modules("int := ;", "tests/modules/basic.mydsl"); check(false, "main parse error"); }
        catch (const mydsl::Error& e) { check(std::string(e.what()) == "Parse error at line 1, column 5: Expected a variable name after type 'int' but found ':='" && e.file.empty() && e.imports.empty(), "the main program's own parse error is reported unchanged (no file name)", e.what()); }
    }
    // ----- invalid cases: exact messages (file, position, chain) -----
    expect_load_error("tests/modules/err_missing.mydsl", "Import", "", 1, 1,
                      "Import error at line 1, column 1: Cannot find imported file \"lib/nope.mydsl\" (resolved to 'tests/modules/lib/nope.mydsl' relative to the directory of 'tests/modules/err_missing.mydsl')");
    expect_load_error("tests/modules/err_missing_nested.mydsl", "Import", "tests/modules/lib/imports_missing.mydsl", 2, 1,
                      "Import error in 'tests/modules/lib/imports_missing.mydsl' at line 2, column 1: Cannot find imported file \"missing.mydsl\" (resolved to 'tests/modules/lib/missing.mydsl' relative to the directory of 'tests/modules/lib/imports_missing.mydsl') (imported from the main program at line 1, column 1)", 1);
    expect_load_error("tests/modules/err_duplicate.mydsl", "Import", "", 3, 1,
                      "Import error at line 3, column 1: Duplicate import of \"lib/vectors.mydsl\": it was already imported at line 1, column 1; remove one of the two imports");
    expect_load_error("tests/modules/err_duplicate_spelling.mydsl", "Import", "", 2, 1,
                      "Import error at line 2, column 1: Duplicate import of \"./lib/../lib/vectors.mydsl\": the same file 'tests/modules/lib/vectors.mydsl' was already imported at line 1, column 1 as \"lib/vectors.mydsl\"; remove one of the two imports");
    expect_load_error("tests/modules/err_self.mydsl", "Import", "", 2, 1, "Import error at line 2, column 1: Circular import: the main program imports itself");
    expect_load_error("tests/modules/err_cycle.mydsl", "Import", "tests/modules/cycle/y.mydsl", 1, 1,
                      "Import error in 'tests/modules/cycle/y.mydsl' at line 1, column 1: Circular import: 'tests/modules/cycle/x.mydsl' -> 'tests/modules/cycle/y.mydsl' -> 'tests/modules/cycle/x.mydsl' (a module cannot import itself, directly or through other modules) (imported from 'tests/modules/cycle/x.mydsl' at line 1, column 1; imported from the main program at line 1, column 1)", 2);
    expect_load_error("tests/modules/err_cycle_long.mydsl", "Import", "tests/modules/cycle/c.mydsl", 2, 1,
                      "Import error in 'tests/modules/cycle/c.mydsl' at line 2, column 1: Circular import: 'tests/modules/cycle/a.mydsl' -> 'tests/modules/cycle/b.mydsl' -> 'tests/modules/cycle/c.mydsl' -> 'tests/modules/cycle/a.mydsl' (a module cannot import itself, directly or through other modules) (imported from 'tests/modules/cycle/b.mydsl' at line 1, column 1; imported from 'tests/modules/cycle/a.mydsl' at line 1, column 1; imported from the main program at line 1, column 1)", 3);
    expect_load_error("tests/modules/err_syntax.mydsl", "Parse", "tests/modules/lib/broken_syntax.mydsl", 3, 1,
                      "Parse error in 'tests/modules/lib/broken_syntax.mydsl' at line 3, column 1: Expected ';' to end the return statement but found '}' (imported from the main program at line 1, column 1)", 1);
    expect_load_error("tests/modules/err_syntax_nested.mydsl", "Parse", "tests/modules/lib/broken_syntax.mydsl", 3, 1,
                      "Parse error in 'tests/modules/lib/broken_syntax.mydsl' at line 3, column 1: Expected ';' to end the return statement but found '}' (imported from 'tests/modules/lib/imports_broken.mydsl' at line 3, column 1; imported from the main program at line 2, column 1)", 2);
    expect_load_error("tests/modules/err_lexing.mydsl", "Lexing", "tests/modules/lib/broken_lexing.mydsl", 2, 14,
                      "Lexing error in 'tests/modules/lib/broken_lexing.mydsl' at line 2, column 14: Unexpected character '$' (imported from the main program at line 1, column 1)", 1);
    expect_load_error("tests/modules/err_directory.mydsl", "Import", "", 1, 1, "Import error at line 1, column 1: Cannot import \"lib\": 'tests/modules/lib' is a directory, not a MyDSL source file");
    expect_load_error("tests/modules/err_empty_name.mydsl", "Parse", "", 1, 8, "Parse error at line 1, column 8: The file name of an import must not be empty (e.g. import \"math_utils.mydsl\";)");
    // The structured chain: innermost first, "" for the main program.
    try { load_fixture("tests/modules/err_syntax_nested.mydsl"); }
    catch (const mydsl::Error& e) {
        check(e.imports.size() == 2 && e.imports[0].file == "tests/modules/lib/imports_broken.mydsl" && e.imports[0].location.line == 3 && e.imports[0].location.column == 1 &&
              e.imports[1].file.empty() && e.imports[1].location.line == 2 && e.imports[1].location.column == 1 && e.message == "Expected ';' to end the return statement but found '}'" && e.trace.empty(),
              "Error::imports lists the import sites innermost first; Error::message stays the bare message");
    }
    // A missing file named from standard input says so.
    try { mydsl::load_modules("Print(1);\nimport \"nowhere/x.mydsl\";"); check(false, "stdin missing import"); }
    catch (const mydsl::Error& e) {
        check(std::string(e.what()) == "Import error at line 2, column 1: Cannot find imported file \"nowhere/x.mydsl\" (resolved to 'nowhere/x.mydsl' relative to the working directory, because the main program was read from standard input)", "stdin missing import message", e.what());
    }
    // Error::in_file and the rendered chain (capped at five sites; the vector keeps all).
    {
        const mydsl::Error base("Parse", {4, 2}, "Expected ';'");
        std::vector<mydsl::ImportSite> chain;
        for (std::size_t i = 1; i <= 7; ++i) chain.push_back({"m" + std::to_string(i) + ".mydsl", {i, 1}});
        chain.push_back({"", {9, 3}});
        const mydsl::Error located = base.in_file("lib/x.mydsl", chain);
        check(std::string(located.what()) == "Parse error in 'lib/x.mydsl' at line 4, column 2: Expected ';' (imported from 'm1.mydsl' at line 1, column 1; imported from 'm2.mydsl' at line 2, column 1; "
                                             "imported from 'm3.mydsl' at line 3, column 1; imported from 'm4.mydsl' at line 4, column 1; imported from 'm5.mydsl' at line 5, column 1; ... 3 more import(s))",
              "in_file renders the file and a capped chain", located.what());
        check(located.imports.size() == 8 && located.file == "lib/x.mydsl" && located.stage == "Parse" && located.message == "Expected ';'" && located.location.line == 4, "in_file keeps every field");
        check(mydsl::render_imports({}).empty() && mydsl::render_imports({{"", {1, 1}}}) == " (imported from the main program at line 1, column 1)", "render_imports");
        check(std::string(base.what()) == "Parse error at line 4, column 2: Expected ';'" && base.file.empty(), "an error without a file renders as before");
    }
    // ----- the single-Program API does not load imports and says so instead of ignoring them -----
    expect_type_error("import \"tests/modules/lib/strings.mydsl\";\nPrint(greet(\"x\"));", 1, 1,
                      "import \"tests/modules/lib/strings.mydsl\" was not resolved by the module loader (typecheck(Program) and interpret(Program) are for single-file programs; load the program with load_modules)");
    {
        auto program = mydsl::parse("import \"x.mydsl\";");
        std::istringstream in; std::ostringstream out;
        try { mydsl::interpret(program, in, out); check(false, "interpreter rejects an unresolved import"); }
        catch (const mydsl::Error& e) { check(std::string(e.what()) == "Runtime error at line 1, column 1: import \"x.mydsl\" was not resolved by the module loader (interpret(Program) is for single-file programs; load the program with load_modules)", "interpreter rejects an unresolved import", e.what()); }
    }
    // ----- the pipeline: run() loads the imports, type-checks and runs them -----
    {
        std::istringstream in; std::ostringstream out;
        try { mydsl::run(read_file("tests/modules/err_syntax.mydsl"), in, out, false, true, "tests/modules/err_syntax.mydsl"); check(false, "run reports loader errors"); }
        catch (const mydsl::Error& e) { check(e.stage == "Parse" && e.file == "tests/modules/lib/broken_syntax.mydsl", "run() reports the error of an imported file", e.what()); }
        mydsl::run(read_file("tests/modules/basic.mydsl"), in, out, false, false, "tests/modules/basic.mydsl");
        check(out.str() == "Int(9)\n", "run() executes a program that calls an imported function", out.str());
        std::ostringstream checked;
        mydsl::run(read_file("tests/modules/basic.mydsl"), in, checked, false, true, "tests/modules/basic.mydsl");
        check(checked.str() == "norm: function(floatvector 3 v) -> float (imported from 'tests/modules/lib/vectors.mydsl')\n"
                               "scale: function(floatvector 3 v, float factor) -> floatvector 3 (imported from 'tests/modules/lib/vectors.mydsl')\n"
                               "square: function(int x) -> int (imported from 'tests/modules/lib/math_utils.mydsl')\n"
                               "mean: function(floatvector 3 v) -> float (imported from 'tests/modules/lib/math_utils.mydsl')\n"
                               "Type check passed: 2 top-level statement(s), 0 top-level variable(s), 4 imported function(s) from 2 module(s)\n",
              "--check lists the imported functions with their modules", checked.str());
    }
    // ----- JSON: file and import chain; the AST of the main program is kept -----
    {
        const std::string json = mydsl::debug_json(read_file("tests/modules/err_syntax_nested.mydsl"), "tests/modules/err_syntax_nested.mydsl");
        check(valid_json(json) && json.find("\"error\":{\"stage\":\"Parse\",\"line\":3,\"column\":1,\"message\":\"Expected ';' to end the return statement but found '}'\",\"file\":\"tests/modules/lib/broken_syntax.mydsl\","
                                            "\"imports\":[{\"file\":\"tests/modules/lib/imports_broken.mydsl\",\"line\":3,\"column\":1},{\"file\":\"\",\"line\":2,\"column\":1}]}") != std::string::npos,
              "debug_json error with file and import chain", json);
        check(json.find("\"ast\":[{\"node\":\"statement\",\"op\":\"print\"") != std::string::npos && json.find("\"op\":\"import\",\"name\":\"lib/imports_broken.mydsl\"") != std::string::npos && json.find("\"type\":") == std::string::npos,
              "debug_json keeps the (untyped) main AST after an import error", json);
        const std::string missing = mydsl::debug_json("import \"nowhere.mydsl\";");
        check(valid_json(missing) && missing.find("\"error\":{\"stage\":\"Import\",\"line\":1,\"column\":1,\"message\":\"Cannot find imported file \\\"nowhere.mydsl\\\" (resolved to 'nowhere.mydsl' relative to the working directory, because the main program was read from standard input)\"}") != std::string::npos,
              "debug_json import error without a file (main program) has no file/imports fields", missing);
        const std::string ran = mydsl::run_json(read_file("tests/modules/err_missing_nested.mydsl"), "", {}, "tests/modules/err_missing_nested.mydsl");
        check(valid_json(ran) && ran.find("\"error\":{\"stage\":\"Import\",\"line\":2,\"column\":1,\"message\":\"Cannot find imported file \\\"missing.mydsl\\\" (resolved to 'tests/modules/lib/missing.mydsl' relative to the directory of 'tests/modules/lib/imports_missing.mydsl')\",\"file\":\"tests/modules/lib/imports_missing.mydsl\",\"imports\":[{\"file\":\"\",\"line\":1,\"column\":1}]},\"variables\":[]") != std::string::npos,
              "run_json import error with file and chain", ran);
        const std::string ok = mydsl::run_json(read_file("tests/modules/basic.mydsl"), "", {}, "tests/modules/basic.mydsl");
        check(valid_json(ok) && ok.find("{\"output\":\"Int(9)\\n\",\"error\":null,\"variables\":[]") != std::string::npos, "run_json runs imported functions", ok);
        const std::string typed = mydsl::debug_json(read_file("tests/modules/basic.mydsl"), "tests/modules/basic.mydsl");
        check(valid_json(typed) && typed.find("\"error\":null") != std::string::npos && typed.find("\"op\":\"call\",\"name\":\"square\",\"type\":\"int\"") != std::string::npos,
              "debug_json annotates the main AST with the types of imported calls", typed);
    }
    // ----- files reached through a symbolic link to a directory (macOS: $TMPDIR is under /var -> /private/var) -----
    // Messages and Module::display name the path as reached; Module::path (identity) resolves the link.
    {
        namespace fs = std::filesystem;
        std::error_code failed;
        const fs::path root = fs::temp_directory_path(failed) / ("mydsl-modules-test-" + std::to_string(static_cast<long long>(::getpid())));
        fs::remove_all(root, failed);
        fs::create_directories(root / "real" / "pkg", failed);
        fs::create_directory_symlink(root / "real", root / "via", failed);
        if (!failed) {
            auto write = [&](const fs::path& path, const std::string& text) { std::ofstream out(path); out << text; };
            write(root / "real" / "pkg" / "strings.mydsl", read_file("tests/modules/lib/strings.mydsl"));
            write(root / "real" / "pkg" / "main.mydsl", "import \"strings.mydsl\";\nimport \"./sub/../strings.mydsl\";\n");
            const std::string via = (root / "via" / "pkg").generic_string(), real = (root / "real" / "pkg").generic_string();
            const std::string main_via = via + "/main.mydsl";
            try { load_fixture(main_via); check(false, "duplicate through a linked directory"); }
            catch (const mydsl::Error& e) {
                check(std::string(e.what()) == "Import error at line 2, column 1: Duplicate import of \"./sub/../strings.mydsl\": the same file '" + via +
                                               "/strings.mydsl' was already imported at line 1, column 1 as \"strings.mydsl\"; remove one of the two imports",
                      "a duplicate names the file by the path it was reached through, not the resolved one", e.what());
            }
            write(root / "real" / "pkg" / "main.mydsl", "import \"strings.mydsl\";\nimport \"" + real + "/strings.mydsl\";\n");
            try { load_fixture(main_via); check(false, "link and real directory are the same module"); }
            catch (const mydsl::Error& e) { check(e.message.rfind("Duplicate import of \"" + real + "/strings.mydsl\": the same file '" + via + "/strings.mydsl' was already imported", 0) == 0, "the file through the link and through the real directory is one module", e.what()); }
            write(root / "real" / "pkg" / "main.mydsl", "import \"strings.mydsl\";\n");
            const auto modules = load_fixture(main_via);
            check(modules.modules.size() == 2 && modules.modules.at(0).display == via + "/strings.mydsl" && modules.main().display == main_via,
                  "Module::display keeps the link in the path", modules.modules.at(0).display);
            const std::string resolved = fs::weakly_canonical(root / "real" / "pkg" / "strings.mydsl", failed).generic_string();
            check(modules.modules.at(0).path == resolved && modules.modules.at(0).path.find("/via/") == std::string::npos, "Module::path is the canonical path (link resolved)", modules.modules.at(0).path);
            const auto through_real = load_fixture(real + "/main.mydsl");
            check(through_real.modules.at(0).path == modules.modules.at(0).path && through_real.modules.at(0).display == real + "/strings.mydsl", "the same module reached directly: same identity, its own display");
            write(root / "real" / "pkg" / "main.mydsl", "import \"missing.mydsl\";\n");
            try { load_fixture(main_via); check(false, "missing through a link"); }
            catch (const mydsl::Error& e) { check(std::string(e.what()) == "Import error at line 1, column 1: Cannot find imported file \"missing.mydsl\" (resolved to '" + via + "/missing.mydsl' relative to the directory of '" + main_via + "')", "a missing file is reported with the path as reached", e.what()); }
        } else {
            check(true, "symbolic links not available here: linked-directory checks skipped");
        }
        fs::remove_all(root, failed);
    }
    // ----- print_modules -----
    {
        std::ostringstream out;
        mydsl::print_modules(load_fixture("tests/modules/basic.mydsl"), out);
        check(out.str() == read_file("tests/modules/basic.modules"), "print_modules matches the hand-written listing", out.str());
        std::ostringstream single;
        mydsl::print_modules(mydsl::load_modules("function int f() { return 1; }; Print(f());"), single);
        check(single.str() == "<stdin> (main program)\n  functions: f\n  other top-level statements: 1\n1 module (the program has no imports)\n", "print_modules for a single stdin program", single.str());
    }
}

// ----- modules: type checking and execution across files -----

// Loads `source` as a main program located at `path` (so that relative imports resolve), then
// type-checks and runs it; returns the output.
std::string module_output(const std::string& source, const std::string& path = "tests/modules/unit.mydsl", const std::string& input = "") {
    const auto modules = mydsl::load_modules(source, path);
    mydsl::typecheck(modules);
    std::istringstream in(input);
    std::ostringstream out;
    mydsl::interpret(modules, in, out);
    return out.str();
}

void expect_module_output(const std::string& source, const std::string& expected, const std::string& input = "") {
    try {
        const auto actual = module_output(source, "tests/modules/unit.mydsl", input);
        check(actual == expected, "module output of: " + source, "expected:\n" + expected + "actual:\n" + actual);
    } catch (const std::exception& e) {
        check(false, "module output of: " + source, std::string("unexpected exception: ") + e.what());
    }
}

// Expects the pipeline to fail with exactly `expected` as what(), `stage`, `file` and the given
// output produced before a runtime failure.
void expect_module_error(const std::string& source, const std::string& stage, const std::string& file, const std::string& expected,
                         const std::string& before = "", const std::string& path = "tests/modules/unit.mydsl") {
    std::ostringstream out;
    try {
        const auto modules = mydsl::load_modules(source, path);
        mydsl::typecheck(modules);
        std::istringstream in;
        mydsl::interpret(modules, in, out);
        check(false, "module error for: " + source, "ran to completion but an error was expected");
    } catch (const mydsl::Error& e) {
        check(e.what() == expected, "module error for: " + source, "expected '" + expected + "'\n      got      '" + e.what() + "'");
        check(e.stage == stage && e.file == file, "module error fields for: " + source, "stage " + e.stage + ", file '" + e.file + "'");
        check(out.str() == before, "output before the module error of: " + source, "expected '" + before + "' got '" + out.str() + "'");
    } catch (const std::exception& e) {
        check(false, "module error for: " + source, std::string("wrong exception type: ") + e.what());
    }
}

void module_semantics_tests() {
    // ----- successful calls across files -----
    expect_module_output("import \"lib/math_utils.mydsl\";\nPrint(square(3));\nPrint(mean([1.0, 2.0, 6.0]));", "Int(9)\nFloat(3.0)\n");
    expect_module_output("Print(square(3));\nimport \"lib/math_utils.mydsl\";", "Int(9)\n"); // an import may follow its use, like a definition
    expect_module_output("import \"lib/stats.mydsl\";\nPrint(variance([1.0, 2.0, 3.0]));", "Float(0.666666666666667)\n"); // stats -> math_utils (mean) -> vectors
    expect_module_output("import \"lib/stats.mydsl\";\nPrint(square(4));\nPrint(norm([3.0, 4.0, 0.0]));", "Int(16)\nFloat(5.0)\n"); // transitively visible
    expect_module_output("import \"lib/stats.mydsl\";\nimport \"lib/vectors.mydsl\";\nPrint(norm(scale([1.0, 0.0, 0.0], 2.0)) + variance([0.0, 0.0, 0.0]));", "Float(2.0)\n"); // diamond
    expect_module_output("import \"lib/recursion.mydsl\";\nPrint(factorial(10));\nPrint(is_odd(7));\nPrint(sum_of_squares(4));", "Int(3628800)\nBool(true)\nInt(30)\n");
    expect_module_output("import \"lib/math_utils.mydsl\";\nfunction int twice_square(int x) { return 2 * square(x); };\nPrint(twice_square(3));", "Int(18)\n"); // own function calls an imported one
    expect_module_output("import \"lib/math_utils.mydsl\";\nint x := 2;\nx := square(square(x));\nPrint(x);\nintvector 2 v := [square(1), square(2)];\nPrint(v);", "Int(16)\nIntVector[1, 4]\n");
    expect_module_output("import \"lib/readers.mydsl\";\nPrint(square_of_next());\nint n := Input();\nPrint(square(n));", "Int(25)\nInt(36)\n", "5 6"); // Input() inside an imported body
    expect_module_output("import \"lib/strings.mydsl\";\nstring s := greet(\"a\") + greet(\"b\");\nPrint(s);", "String(\"Hello, a!Hello, b!\")\n");
    expect_module_output("import \"app/helpers.mydsl\";\nPrint(unit_norm([0.0, 3.0, 4.0]));", "Float(1.0)\n"); // "../lib" from a subdirectory
    // Arguments are passed by value across files too: the caller's vector is untouched.
    expect_module_output("import \"lib/vectors.mydsl\";\nfloatvector 3 v := [1.0, 2.0, 3.0];\nfloatvector 3 w := scale(v, 10.0);\nPrint(v);\nPrint(w);", "FloatVector[1.0, 2.0, 3.0]\nFloatVector[10.0, 20.0, 30.0]\n");
    // The interpreter's result and the inspector state only hold the main program's variables.
    {
        const auto modules = mydsl::load_modules("import \"lib/math_utils.mydsl\";\nint a := square(5);", "tests/modules/unit.mydsl");
        mydsl::typecheck(modules);
        std::istringstream in; std::ostringstream out;
        std::map<std::string, mydsl::Value> state;
        const auto globals = mydsl::interpret(modules, in, out, {}, &state);
        check(globals.size() == 1 && globals.at("a").str() == "Int(25)" && state.size() == 1, "interpret(Modules) returns the main program's globals");
        const auto types = mydsl::typecheck(modules);
        check(types.size() == 1 && types.at("a").str() == "int", "typecheck(Modules) returns the main program's variable types");
        // Every expression of every module is annotated.
        const auto& square = modules.modules.at(1).program.at(1);
        check(square->name == "square" && square->body.at(0)->body.at(0)->exprs.at(0)->type.str() == "int", "imported bodies are annotated with types");
    }
    // ----- invalid cases: duplicates, visibility, arguments, unknown names, library contents, scoping -----
    expect_module_error("import \"lib/math_utils.mydsl\";\nfunction int square(int x) { return x; };", "Type", "",
                        "Type error at line 2, column 1: Function 'square' is already defined (at line 3, column 1 of 'tests/modules/lib/math_utils.mydsl'); function names are shared by a program and every module it imports, directly or indirectly");
    expect_module_error("import \"lib/math_utils.mydsl\";\nimport \"lib/also_square.mydsl\";", "Type", "tests/modules/lib/also_square.mydsl",
                        "Type error in 'tests/modules/lib/also_square.mydsl' at line 1, column 1: Function 'square' is already defined (at line 3, column 1 of 'tests/modules/lib/math_utils.mydsl'); function names are shared by a program and every module it imports, directly or indirectly (imported from the main program at line 2, column 1)");
    expect_module_error("function int square(int x) { return x; };\nfunction int square(int y) { return y; };", "Type", "",
                        "Type error at line 2, column 1: Function 'square' is already defined (at line 1, column 1)"); // same file: wording unchanged
    expect_module_error("import \"lib/calls_main.mydsl\";\nfunction int helper(int x) { return x; };\nPrint(uses_helper(1));", "Type", "tests/modules/lib/calls_main.mydsl",
                        "Type error in 'tests/modules/lib/calls_main.mydsl' at line 3, column 12: Unknown function 'helper': 'helper' is defined in the main program, which 'tests/modules/lib/calls_main.mydsl' does not import (a function is visible where its file is imported, directly or through other imports) (imported from the main program at line 1, column 1)");
    expect_module_error("import \"lib/math_utils.mydsl\";\nPrint(square());", "Type", "",
                        "Type error at line 2, column 7: Function 'square' expects 1 argument (int x) but got 0 (declared at line 3, column 1 of 'tests/modules/lib/math_utils.mydsl')");
    expect_module_error("import \"lib/math_utils.mydsl\";\nPrint(mean([1, 2, 3]));", "Type", "",
                        "Type error at line 2, column 12: Argument 1 of 'mean' must be floatvector 3 (parameter 'v') but got intvector 3 (MyDSL does not convert between int and float implicitly; use to_float(...) or to_int(...))");
    expect_module_error("import \"lib/math_utils.mydsl\";\nint s := square(2) + mean([1.0, 2.0, 3.0]);", "Type", "",
                        "Type error at line 2, column 20: Operator '+' cannot be applied to int and float (MyDSL does not convert between int and float implicitly; use to_float(...) or to_int(...))");
    expect_module_error("import \"lib/math_utils.mydsl\";\nPrint(cube(2));", "Type", "",
                        "Type error at line 2, column 7: Unknown function 'cube' (available: abs, angle_vec, determinant, dotprod_vec, Input, inv_vec, inverse_mat, is_zero, length, mult_mat, substring, to_float, to_int, to_string, transpose; imported: mean, norm, scale, square)");
    expect_module_error("import \"lib/math_utils.mydsl\";\nfunction int own() { return 1; };\nPrint(cube(2));", "Type", "",
                        "Type error at line 3, column 7: Unknown function 'cube' (available: abs, angle_vec, determinant, dotprod_vec, Input, inv_vec, inverse_mat, is_zero, length, mult_mat, substring, to_float, to_int, to_string, transpose; defined in this program: own; imported: mean, norm, scale, square)");
    expect_module_error("import \"lib/math_utils.mydsl\";\nPrint(square);", "Type", "",
                        "Type error at line 2, column 7: Undeclared variable 'square' ('square' is a function; call it with arguments: square(...))");
    expect_module_error("import \"lib/config.mydsl\";\nPrint(the_answer());", "Type", "tests/modules/lib/config.mydsl",
                        "Type error in 'tests/modules/lib/config.mydsl' at line 3, column 1: An imported module may only contain function definitions and imports, but this is a declaration (the top-level statements of an imported module are never executed; move it into a function or into the main program) (imported from the main program at line 1, column 1)");
    expect_module_error("import \"lib/bad_body.mydsl\";", "Type", "tests/modules/lib/bad_body.mydsl",
                        "Type error in 'tests/modules/lib/bad_body.mydsl' at line 2, column 14: Operator '*' cannot be applied to int and float (MyDSL does not convert between int and float implicitly; use to_float(...) or to_int(...)) (imported from the main program at line 1, column 1)");
    expect_module_error("import \"lib/uses_global.mydsl\";\nint limit := 3;\nPrint(clamp(5));", "Type", "tests/modules/lib/uses_global.mydsl",
                        "Type error in 'tests/modules/lib/uses_global.mydsl' at line 2, column 13: Undeclared variable 'limit' (inside function 'clamp' only its parameters and local variables are visible, not top-level variables; pass the value as a parameter) (imported from the main program at line 1, column 1)");
    // The main program's first error is still reported in source order, before later function bodies.
    expect_module_error("import \"lib/math_utils.mydsl\";\nint a := true;\nfunction int f() { return 1.5; };", "Type", "",
                        "Type error at line 2, column 1: Cannot initialise 'a' of type int with a value of type bool");
    // Errors inside imported modules come before the main program's own, in dependency order.
    expect_module_error("int a := true;\nimport \"lib/bad_body.mydsl\";", "Type", "tests/modules/lib/bad_body.mydsl",
                        "Type error in 'tests/modules/lib/bad_body.mydsl' at line 2, column 14: Operator '*' cannot be applied to int and float (MyDSL does not convert between int and float implicitly; use to_float(...) or to_int(...)) (imported from the main program at line 2, column 1)");
    // ----- runtime failures inside imported functions: file, trace with files, import chain, output kept -----
    expect_module_error("import \"lib/failing.mydsl\";\nPrint(ratio(6, 3));\nPrint(ratio(1, 0));", "Runtime", "tests/modules/lib/failing.mydsl",
                        "Runtime error in 'tests/modules/lib/failing.mydsl' at line 4, column 14: Division by zero: 1 / 0 (in function 'ratio' called from line 3, column 7) (imported from the main program at line 1, column 1)", "Int(2)\n");
    expect_module_error("import \"lib/failing.mydsl\";\nPrint(scaled_ratio(1, 0));", "Runtime", "tests/modules/lib/failing.mydsl",
                        "Runtime error in 'tests/modules/lib/failing.mydsl' at line 4, column 14: Division by zero: 1 / 0 (in function 'ratio' called from line 7, column 19 of 'tests/modules/lib/failing.mydsl'; in function 'scaled_ratio' called from line 2, column 7) (imported from the main program at line 1, column 1)");
    expect_module_error("import \"lib/failing.mydsl\";\nfunction int wrap(int b) { return scaled_ratio(8, b); };\nPrint(wrap(2));\nPrint(wrap(0));", "Runtime", "tests/modules/lib/failing.mydsl",
                        "Runtime error in 'tests/modules/lib/failing.mydsl' at line 4, column 14: Division by zero: 8 / 0 (in function 'ratio' called from line 7, column 19 of 'tests/modules/lib/failing.mydsl'; in function 'scaled_ratio' called from line 2, column 35; in function 'wrap' called from line 4, column 7) (imported from the main program at line 1, column 1)", "Int(16)\n");
    expect_module_error("import \"lib/failing.mydsl\";\nPrint(countdown(1));", "Runtime", "tests/modules/lib/failing.mydsl",
                        "Runtime error in 'tests/modules/lib/failing.mydsl' at line 10, column 12: Call depth limit of 1000 exceeded while calling 'countdown' (is a recursive function missing its base case?) (in function 'countdown' called from line 10, column 12 of 'tests/modules/lib/failing.mydsl' [999 times]; in function 'countdown' called from line 2, column 7) (imported from the main program at line 1, column 1)");
    // An error in the main program while an imported function's argument is evaluated is the main program's.
    expect_module_error("import \"lib/math_utils.mydsl\";\nPrint(square(1 / 0));", "Runtime", "",
                        "Runtime error at line 2, column 16: Division by zero: 1 / 0");
    // An error inside a main-program function called from the main program: unchanged wording.
    expect_module_error("import \"lib/math_utils.mydsl\";\nfunction int bad(int x) { return square(x) / 0; };\nPrint(bad(1));", "Runtime", "",
                        "Runtime error at line 2, column 44: Division by zero: 1 / 0 (in function 'bad' called from line 3, column 7)");
    // Structured fields of a runtime error inside an imported function.
    try { module_output("import \"lib/failing.mydsl\";\nPrint(scaled_ratio(1, 0));"); check(false, "structured runtime error"); }
    catch (const mydsl::Error& e) {
        check(e.trace.size() == 2 && e.trace[0].function == "ratio" && e.trace[0].file == "tests/modules/lib/failing.mydsl" && e.trace[0].call_site.line == 7 &&
              e.trace[1].function == "scaled_ratio" && e.trace[1].file.empty() && e.trace[1].call_site.line == 2 && e.imports.size() == 1 && e.imports[0].file.empty() &&
              e.message == "Division by zero: 1 / 0" && e.location.line == 4 && e.location.column == 14, "Error::trace frames carry the file of their call site; Error::imports the chain");
    }
    // Time and output limits apply inside imported functions and are reported there.
    try {
        const auto modules = mydsl::load_modules("import \"lib/failing.mydsl\";\nPrint(countdown(0));", "tests/modules/unit.mydsl");
        mydsl::typecheck(modules);
        std::istringstream in; std::ostringstream out;
        mydsl::interpret(modules, in, out, {0, 1});
        check(false, "output limit inside imported code");
    } catch (const mydsl::Error& e) { check(e.stage == "Runtime", "limits inside imported code are runtime errors", e.what()); }
    // ----- a program read from standard input (no path) resolves imports against the working directory -----
    {
        const auto modules = mydsl::load_modules("import \"tests/modules/lib/strings.mydsl\";\nPrint(greet(\"stdin\"));");
        mydsl::typecheck(modules);
        std::istringstream in; std::ostringstream out;
        mydsl::interpret(modules, in, out);
        check(out.str() == "String(\"Hello, stdin!\")\n", "stdin program calls an imported function", out.str());
    }
}

// ----- the standard library (stdlib/): numeric accuracy at full precision, references for ints and strings -----

// Runs a main program that imports `module` and declares `declarations` (text), and returns
// the final top-level variables at full precision (Print would round to 15 digits).
std::map<std::string, mydsl::Value> stdlib_run(const std::string& module, const std::string& declarations, const std::string& input = "") {
    const auto modules = mydsl::load_modules("import \"stdlib/" + module + ".mydsl\";\n" + declarations); // no path: relative to the project directory
    mydsl::typecheck(modules);
    std::istringstream in(input);
    std::ostringstream out;
    return mydsl::interpret(modules, in, out);
}

double ulps_apart(double a, double b) {
    if (a == b) return 0;
    const double ulp = std::nextafter(std::fabs(b), INFINITY) - std::fabs(b);
    return std::fabs(a - b) / ulp;
}

void stdlib_tests() {
    std::mt19937_64 random(2024);
    auto uniform = [&](double low, double high) { return std::uniform_real_distribution<double>(low, high)(random); };
    auto literal = [](double x) { char text[64]; std::snprintf(text, sizeof text, "%.17g", x); std::string s = text; if (s.find_first_of(".e") == std::string::npos) s += ".0"; return s; };
    // One run per function: r0, r1, ... hold the results for every argument.
    auto evaluate = [&](const std::string& module, const std::string& type, const std::string& function, const std::vector<std::string>& arguments) {
        std::string program;
        for (std::size_t i = 0; i < arguments.size(); ++i) program += type + " r" + std::to_string(i) + " := " + function + "(" + arguments[i] + ");\n";
        const auto values = stdlib_run(module, program);
        std::vector<mydsl::Value> results;
        for (std::size_t i = 0; i < arguments.size(); ++i) results.push_back(values.at("r" + std::to_string(i)));
        return results;
    };
    // ----- float functions against <cmath> -----
    {
        std::vector<double> xs = {0.0, 1.0, 2.0, 0.5, 0.25, 1e-300, 1e300, 1e308, 5e-324, 9.0, 1e-5, 12345.678};
        for (int i = 0; i < 300; ++i) xs.push_back(uniform(0.001, 1000.0));
        for (int i = 0; i < 100; ++i) xs.push_back(std::exp(uniform(-690.0, 690.0)));
        std::vector<std::string> args; for (double x : xs) args.push_back(literal(x));
        double worst = 0;
        auto results = evaluate("fmath", "float", "sqrt_f", args);
        for (std::size_t i = 0; i < xs.size(); ++i) worst = std::max(worst, ulps_apart(results[i].floats[0], std::sqrt(xs[i])));
        check(worst <= 1.0, "sqrt_f is within 1 ulp of std::sqrt over " + std::to_string(xs.size()) + " arguments", "worst: " + std::to_string(worst) + " ulps");
        worst = 0;
        results = evaluate("fmath", "float", "cbrt_f", args);
        for (std::size_t i = 0; i < xs.size(); ++i) worst = std::max(worst, std::fabs(results[i].floats[0] - std::cbrt(xs[i])) / std::cbrt(xs[i]));
        check(worst <= 2e-14, "cbrt_f within 2e-14 relative of std::cbrt", "worst: " + std::to_string(worst));
        worst = 0;
        std::vector<double> positive; std::vector<std::string> positive_args;
        for (double x : xs) if (x > 0) { positive.push_back(x); positive_args.push_back(literal(x)); }
        results = evaluate("fmath", "float", "ln_f", positive_args);
        for (std::size_t i = 0; i < positive.size(); ++i) worst = std::max(worst, std::fabs(results[i].floats[0] - std::log(positive[i])) / std::max(1.0, std::fabs(std::log(positive[i]))));
        check(worst <= 1e-14, "ln_f within 1e-14 of std::log (relative to max(1, |ln x|))", "worst: " + std::to_string(worst));
    }
    {
        std::vector<double> xs = {0.0, 1.0, -1.0, 0.5, 709.0, -708.0, 100.0, -100.0, 2.5, 1e-10, -1e-10, 0.6931471805599453};
        for (int i = 0; i < 300; ++i) xs.push_back(uniform(-700.0, 700.0));
        for (int i = 0; i < 100; ++i) xs.push_back(uniform(-2.0, 2.0));
        std::vector<std::string> args; for (double x : xs) args.push_back(literal(x));
        auto results = evaluate("fmath", "float", "exp_f", args);
        double worst = 0;
        for (std::size_t i = 0; i < xs.size(); ++i) worst = std::max(worst, std::fabs(results[i].floats[0] - std::exp(xs[i])) / std::exp(xs[i]));
        check(worst <= 1e-14, "exp_f within 1e-14 relative of std::exp over " + std::to_string(xs.size()) + " arguments", "worst: " + std::to_string(worst));
    }
    {
        std::vector<double> xs = {0.0, M_PI, -M_PI, M_PI / 2, -M_PI / 2, M_PI / 6, M_PI / 4, 3 * M_PI, 1e6, -1e6, 100.0, -0.001};
        for (int i = 0; i < 300; ++i) xs.push_back(uniform(-50.0, 50.0));
        for (int i = 0; i < 100; ++i) xs.push_back(uniform(-1e6, 1e6));
        std::vector<std::string> args; for (double x : xs) args.push_back(literal(x));
        auto sines = evaluate("fmath", "float", "sin_f", args), cosines = evaluate("fmath", "float", "cos_f", args);
        double worst = 0;
        for (std::size_t i = 0; i < xs.size(); ++i) worst = std::max({worst, std::fabs(sines[i].floats[0] - std::sin(xs[i])), std::fabs(cosines[i].floats[0] - std::cos(xs[i]))});
        check(worst <= 2e-15, "sin_f and cos_f within 2e-15 absolute of <cmath> up to |x| = 1e6", "worst: " + std::to_string(worst));
        auto tangents = evaluate("fmath", "float", "tan_f", {"0.0", "0.5", "-1.0", "1.5", "0.7853981633974483"});
        check(std::fabs(tangents[1].floats[0] - std::tan(0.5)) < 1e-14 && std::fabs(tangents[3].floats[0] - std::tan(1.5)) < 1e-12 && std::fabs(tangents[4].floats[0] - 1.0) < 1e-15, "tan_f");
    }
    {
        std::vector<std::pair<double, double>> pairs = {{2.0, 10.0}, {2.0, 0.5}, {10.0, -3.0}, {0.0, 2.0}, {0.0, 0.0}, {9.0, 0.5}, {1.0, 1e6}, {0.5, -1000.0}};
        for (int i = 0; i < 200; ++i) pairs.push_back({uniform(0.1, 50.0), uniform(-10.0, 10.0)});
        std::vector<std::string> args; for (const auto& [b, e] : pairs) args.push_back(literal(b) + ", " + literal(e));
        auto results = evaluate("fmath", "float", "pow_f", args);
        double worst = 0;
        for (std::size_t i = 0; i < pairs.size(); ++i) { // exp(y ln x): the error grows with |y ln x|
            const double expected = std::pow(pairs[i].first, pairs[i].second);
            const double magnitude = pairs[i].first > 0 ? std::fabs(pairs[i].second * std::log(pairs[i].first)) : 0.0;
            worst = std::max(worst, std::fabs(results[i].floats[0] - expected) / std::max(expected, 1e-300) / (1e-15 * (4.0 + magnitude)));
        }
        check(worst <= 1.0, "pow_f within 1e-15 * (4 + |exponent * ln base|) relative of std::pow", "worst: " + std::to_string(worst) + " of the bound");
        std::vector<std::pair<double, double>> sides = {{3.0, 4.0}, {0.0, 0.0}, {1e200, 1e200}, {-1e200, 1e-200}, {1e-300, 1e-300}, {5e-324, 0.0}, {1e308, 0.0}};
        for (int i = 0; i < 100; ++i) sides.push_back({uniform(-1e100, 1e100), uniform(-1e100, 1e100)});
        args.clear(); for (const auto& [a, b] : sides) args.push_back(literal(a) + ", " + literal(b));
        results = evaluate("fmath", "float", "hypot_f", args);
        worst = 0;
        for (std::size_t i = 0; i < sides.size(); ++i) worst = std::max(worst, std::fabs(results[i].floats[0] - std::hypot(sides[i].first, sides[i].second)) / std::max(std::hypot(sides[i].first, sides[i].second), 5e-324));
        check(worst <= 1e-14, "hypot_f within 1e-14 relative of std::hypot, including huge and tiny arguments", "worst: " + std::to_string(worst));
        std::vector<std::pair<double, int>> powers = {{2.0, 10}, {2.0, -3}, {-2.0, 3}, {0.5, 10}, {2.0, -1000}, {1.0000001, 1000000}, {3.0, 0}, {0.0, 0}, {10.0, 300}, {10.0, -300}, {0.1, -300}};
        args.clear(); for (const auto& [b, e] : powers) args.push_back(literal(b) + ", " + std::to_string(e));
        results = evaluate("fmath", "float", "power_f", args);
        worst = 0;
        for (std::size_t i = 0; i < powers.size(); ++i) { // repeated squaring: about |n| ulps
            const double expected = std::pow(powers[i].first, powers[i].second);
            worst = std::max(worst, std::fabs(results[i].floats[0] - expected) / std::max(std::fabs(expected), 5e-324) / (2.3e-16 * (4.0 + std::fabs(powers[i].second))));
        }
        check(worst <= 1.0, "power_f within (4 + |exponent|) * 2.3e-16 relative of std::pow (2^-1000, 10^300, 0.1^-300, 1.0000001^1000000)", "worst: " + std::to_string(worst) + " of the bound");
    }
    {
        std::vector<double> xs = {0.0, 0.5, -0.5, 1.5, -1.5, 2.5, -2.5, 2.4999, 1e15, -1e15, 123.456, -123.456, 9.2e18, -9.2e18};
        for (int i = 0; i < 100; ++i) xs.push_back(uniform(-1e6, 1e6));
        std::vector<std::string> args; for (double x : xs) args.push_back(literal(x));
        auto floors = evaluate("fmath", "float", "floor_f", args), ceils = evaluate("fmath", "float", "ceil_f", args), rounds = evaluate("fmath", "float", "round_f", args), fracts = evaluate("fmath", "float", "fract_f", args);
        bool ok = true;
        for (std::size_t i = 0; i < xs.size(); ++i)
            ok = ok && floors[i].floats[0] == std::floor(xs[i]) && ceils[i].floats[0] == std::ceil(xs[i]) && rounds[i].floats[0] == std::round(xs[i]) && fracts[i].floats[0] == xs[i] - std::floor(xs[i]);
        check(ok, "floor_f, ceil_f, round_f (halves away from zero) and fract_f agree with <cmath>");
        const auto values = stdlib_run("fmath", "float a := pi(); float b := euler(); float c := degrees_f(1.0); float d := radians_f(90.0); float e := lerp_f(2.0, 4.0, 0.75); "
                                                "float f := clamp_f(5.0, -1.0, 1.0); float g := min_f(2.0, -3.0); float h := max_f(2.0, -3.0); float i := sign_f(-0.5); bool j := approx_equal_f(1.0, 1.0 + 1e-13, 1e-12); "
                                                "bool k := is_integer_f(4.0); bool l := is_integer_f(4.5); float m := log2_f(1024.0); float n := log10_f(0.001); float o := square_f(1.5); float p := cube_f(-2.0);");
        check(values.at("a").floats[0] == M_PI && values.at("b").floats[0] == M_E && std::fabs(values.at("c").floats[0] - 180.0 / M_PI) < 1e-13 && std::fabs(values.at("d").floats[0] - M_PI / 2) < 1e-16 &&
              values.at("e").floats[0] == 3.5 && values.at("f").floats[0] == 1.0 && values.at("g").floats[0] == -3.0 && values.at("h").floats[0] == 2.0 && values.at("i").floats[0] == -1.0 &&
              values.at("j").truth() && values.at("k").truth() && !values.at("l").truth() && std::fabs(values.at("m").floats[0] - 10.0) < 1e-14 && std::fabs(values.at("n").floats[0] + 3.0) < 1e-14 &&
              values.at("o").floats[0] == 2.25 && values.at("p").floats[0] == -8.0, "the simple float helpers");
    }
    // ----- int functions against reference implementations -----
    {
        std::vector<long long> ns = {0, 1, -1, 2, -2, 7, -7, 10, 100, 999, 1000, 1 << 20, -(1 << 20), 1000000007, 9223372036854775807LL, -9223372036854775807LL, 3037000499LL, 4611686018427387904LL};
        for (int i = 0; i < 200; ++i) ns.push_back(static_cast<long long>(std::uniform_int_distribution<long long>(-1000000, 1000000)(random)));
        std::vector<std::string> args; for (long long n : ns) args.push_back(std::to_string(n));
        auto signs = evaluate("math", "int", "sign", args), evens = evaluate("math", "bool", "is_even", args), counts = evaluate("math", "int", "digit_count", args), sums = evaluate("math", "int", "digit_sum", args);
        bool ok = true;
        for (std::size_t i = 0; i < ns.size(); ++i) {
            const std::string digits = std::to_string(ns[i] < 0 ? -(ns[i] + 1) : ns[i]); // |n| without overflowing for the minimum
            long long digit_sum = 0; for (char c : std::to_string(ns[i])) if (c >= '0' && c <= '9') digit_sum += c - '0';
            ok = ok && signs[i].ints[0] == (ns[i] > 0) - (ns[i] < 0) && evens[i].truth() == (ns[i] % 2 == 0) && counts[i].ints[0] == static_cast<long long>(digits.size()) && sums[i].ints[0] == digit_sum;
        }
        check(ok, "sign, is_even, digit_count and digit_sum over " + std::to_string(ns.size()) + " ints");
        std::vector<std::string> positive; std::vector<long long> ps;
        for (long long n : ns) if (n >= 0) { ps.push_back(n); positive.push_back(std::to_string(n)); }
        auto roots = evaluate("math", "int", "isqrt", positive);
        ok = true;
        for (std::size_t i = 0; i < ps.size(); ++i) { // (r + 1)^2 may exceed the int range: compare through a division
            const long long r = roots[i].ints[0];
            ok = ok && r >= 0 && r <= 3037000499LL && r * r <= ps[i] && (r + 1 > ps[i] / (r + 1) || (r + 1) * (r + 1) > ps[i]);
        }
        check(ok, "isqrt(n) is the largest s with s*s <= n, up to the maximum int");
        std::vector<std::string> primes; std::vector<long long> pn;
        for (long long n = -5; n <= 2000; ++n) { pn.push_back(n); primes.push_back(std::to_string(n)); }
        pn.push_back(1000000007); primes.push_back("1000000007"); pn.push_back(1000000007LL * 3); primes.push_back("3000000021");
        auto flags = evaluate("math", "bool", "is_prime", primes);
        ok = true;
        for (std::size_t i = 0; i < pn.size(); ++i) {
            bool prime = pn[i] >= 2; for (long long d = 2; d * d <= pn[i] && prime; ++d) if (pn[i] % d == 0) prime = false;
            ok = ok && flags[i].truth() == prime;
        }
        check(ok, "is_prime from -5 to 2000 and two large cases");
    }
    {
        std::vector<std::pair<long long, long long>> pairs = {{0, 0}, {0, 5}, {5, 0}, {-7, 2}, {7, -2}, {-7, -2}, {-8, 2}, {12, 18}, {-12, 18}, {9223372036854775807LL, -1}, {1, 9223372036854775807LL}, {6, 4}, {-6, -4}};
        for (int i = 0; i < 200; ++i) pairs.push_back({std::uniform_int_distribution<long long>(-1000000000, 1000000000)(random), std::uniform_int_distribution<long long>(-1000000000, 1000000000)(random)});
        std::vector<std::string> args; for (const auto& [a, b] : pairs) args.push_back(std::to_string(a) + ", " + std::to_string(b));
        auto mins = evaluate("math", "int", "min", args), maxs = evaluate("math", "int", "max", args), gcds = evaluate("math", "int", "gcd", args);
        bool ok = true;
        for (std::size_t i = 0; i < pairs.size(); ++i) {
            long long x = std::llabs(pairs[i].first), y = std::llabs(pairs[i].second);
            while (y) { const long long r = x % y; x = y; y = r; }
            ok = ok && mins[i].ints[0] == std::min(pairs[i].first, pairs[i].second) && maxs[i].ints[0] == std::max(pairs[i].first, pairs[i].second) && gcds[i].ints[0] == x;
        }
        check(ok, "min, max and gcd over " + std::to_string(pairs.size()) + " pairs");
        std::vector<std::string> nonzero; std::vector<std::pair<long long, long long>> nz;
        for (const auto& p : pairs) if (p.second != 0 && p.first != 9223372036854775807LL && p.second != 9223372036854775807LL) { nz.push_back(p); nonzero.push_back(std::to_string(p.first) + ", " + std::to_string(p.second)); }
        auto quotients = evaluate("math", "int", "div_floor", nonzero), remainders = evaluate("math", "int", "mod_floor", nonzero), lcms = evaluate("math", "int", "lcm", nonzero);
        ok = true;
        for (std::size_t i = 0; i < nz.size(); ++i) {
            const auto [a, b] = nz[i];
            long long q = a / b; if (a % b != 0 && ((a < 0) != (b < 0))) --q;                 // floor division
            long long x = std::llabs(a), y = std::llabs(b); while (y) { const long long r = x % y; x = y; y = r; }
            const long long lcm = a == 0 ? 0 : std::llabs(a / x * b);
            ok = ok && quotients[i].ints[0] == q && remainders[i].ints[0] == a - q * b && (remainders[i].ints[0] == 0 || (remainders[i].ints[0] < 0) == (b < 0)) && lcms[i].ints[0] == lcm;
        }
        check(ok, "div_floor, mod_floor (sign of the divisor) and lcm");
        std::string program;
        for (int n = 0; n <= 20; ++n) program += "int f" + std::to_string(n) + " := factorial(" + std::to_string(n) + ");\n";
        for (int n = 0; n <= 92; ++n) program += "int b" + std::to_string(n) + " := fibonacci(" + std::to_string(n) + ");\n";
        for (int n = 0; n <= 20; ++n) for (int k = 0; k <= n; ++k) program += "int c" + std::to_string(n) + "_" + std::to_string(k) + " := binomial(" + std::to_string(n) + ", " + std::to_string(k) + ");\n";
        for (int e = 0; e <= 62; ++e) program += "int p" + std::to_string(e) + " := power(2, " + std::to_string(e) + ");\n";
        program += "int q := power(-3, 5); int r := power(7, 0); int s := power(0, 0); int t := power(0, 9); int u := power(-2, 63); int v := power(10, 18);\n";
        const auto values = stdlib_run("math", program);
        ok = true;
        long long f = 1; for (int n = 0; n <= 20; ++n) { if (n) f *= n; ok = ok && values.at("f" + std::to_string(n)).ints[0] == f; }
        unsigned long long a = 0, b = 1; // unsigned: the pair reaches fib(93), which wraps instead of overflowing (only fib(0..92) is compared)
        for (int n = 0; n <= 92; ++n) { ok = ok && values.at("b" + std::to_string(n)).ints[0] == static_cast<long long>(a); const unsigned long long next = a + b; a = b; b = next; }
        for (int n = 0; n <= 20; ++n) { long long c = 1; for (int k = 0; k <= n; ++k) { ok = ok && values.at("c" + std::to_string(n) + "_" + std::to_string(k)).ints[0] == c; c = c * (n - k) / (k + 1); } }
        for (int e = 0; e <= 62; ++e) ok = ok && values.at("p" + std::to_string(e)).ints[0] == (1LL << e);
        ok = ok && values.at("q").ints[0] == -243 && values.at("r").ints[0] == 1 && values.at("s").ints[0] == 1 && values.at("t").ints[0] == 0 && values.at("u").ints[0] == (-9223372036854775807LL - 1) && values.at("v").ints[0] == 1000000000000000000LL;
        check(ok, "factorial 0..20, fibonacci 0..92, binomial up to n = 20 and power (exact, up to the limits of int)");
    }
    // ----- string functions against std::string -----
    {
        const std::vector<std::string> samples = {"", "a", "hello world", "  padded\t\n", "aaa", "abcabc", "Hello, MyDSL!", "-42", "42", "-", "12a", "x y z", "AbC xYz 123", "0", "-0", "--1", "1-", "9223372036854775807", "-9223372036854775808"};
        const std::vector<std::string> parts = {"", "a", "l", "lo", "abc", "z", "world"};
        std::string program;
        std::size_t n = 0;
        struct Expect { std::string name; mydsl::Value value; };
        std::vector<Expect> expected;
        auto add = [&](const std::string& type, const std::string& expression, mydsl::Value value) {
            const std::string name = "v" + std::to_string(n++);
            program += type + " " + name + " := " + expression + ";\n";
            expected.push_back({name, std::move(value)});
        };
        auto strip = [](std::string s, bool left, bool right) {
            const std::string ws = " \t\n\r";
            if (left) s.erase(0, s.find_first_not_of(ws) == std::string::npos ? s.size() : s.find_first_not_of(ws));
            if (right) { const auto last = s.find_last_not_of(ws); s.erase(last == std::string::npos ? 0 : last + 1); }
            return s;
        };
        auto is_int = [](const std::string& s) { std::size_t i = s.size() && s[0] == '-'; if (i >= s.size()) return false; for (; i < s.size(); ++i) if (s[i] < '0' || s[i] > '9') return false; return true; };
        for (const std::string& s : samples) {
            const std::string q = mydsl::quote_string(s);
            add("string", "reverse(" + q + ")", mydsl::Value::string(std::string(s.rbegin(), s.rend())));
            add("string", "trim(" + q + ")", mydsl::Value::string(strip(s, true, true)));
            add("string", "trim_left(" + q + ")", mydsl::Value::string(strip(s, true, false)));
            add("string", "trim_right(" + q + ")", mydsl::Value::string(strip(s, false, true)));
            std::string upper = s, lower = s;
            for (char& c : upper) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
            for (char& c : lower) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            add("string", "to_upper(" + q + ")", mydsl::Value::string(upper));
            add("string", "to_lower(" + q + ")", mydsl::Value::string(lower));
            add("bool", "is_integer_text(" + q + ")", mydsl::Value::boolean(is_int(s)));
            if (is_int(s)) add("int", "parse_int(" + q + ")", mydsl::Value::integer(std::stoll(s)));
            for (const std::string& part : parts) {
                const std::string p = mydsl::quote_string(part);
                const auto first = s.find(part), last = s.rfind(part);
                add("int", "index_of(" + q + ", " + p + ")", mydsl::Value::integer(first == std::string::npos ? -1 : static_cast<long long>(first)));
                add("int", "last_index_of(" + q + ", " + p + ")", mydsl::Value::integer(last == std::string::npos ? -1 : static_cast<long long>(last)));
                add("bool", "contains(" + q + ", " + p + ")", mydsl::Value::boolean(first != std::string::npos));
                add("bool", "starts_with(" + q + ", " + p + ")", mydsl::Value::boolean(s.compare(0, part.size(), part) == 0 && part.size() <= s.size()));
                add("bool", "ends_with(" + q + ", " + p + ")", mydsl::Value::boolean(part.size() <= s.size() && s.compare(s.size() - part.size(), part.size(), part) == 0));
                if (!part.empty()) {
                    long long count = 0; std::string replaced;
                    for (std::size_t i = 0; i < s.size();) {
                        if (s.compare(i, part.size(), part) == 0) { ++count; replaced += "_"; i += part.size(); } else { replaced += s[i]; ++i; }
                    }
                    add("int", "count_of(" + q + ", " + p + ")", mydsl::Value::integer(count));
                    add("string", "replace_all(" + q + ", " + p + ", \"_\")", mydsl::Value::string(replaced));
                }
            }
            for (int k : {0, 1, 3}) { std::string r; for (int i = 0; i < k; ++i) r += s; add("string", "repeat(" + q + ", " + std::to_string(k) + ")", mydsl::Value::string(r)); }
            for (std::size_t w : {std::size_t{0}, std::size_t{5}, std::size_t{12}}) {
                add("string", "pad_left(" + q + ", " + std::to_string(w) + ", \"*\")", mydsl::Value::string(s.size() >= w ? s : std::string(w - s.size(), '*') + s));
                add("string", "pad_right(" + q + ", " + std::to_string(w) + ", \".\")", mydsl::Value::string(s.size() >= w ? s : s + std::string(w - s.size(), '.')));
            }
        }
        for (const std::string c : {"a", "Z", "5", " ", "\t", "ab", "", "_", "\n"}) {
            const std::string q = mydsl::quote_string(c);
            add("bool", "is_digit(" + q + ")", mydsl::Value::boolean(c.size() == 1 && c[0] >= '0' && c[0] <= '9'));
            add("bool", "is_letter(" + q + ")", mydsl::Value::boolean(c.size() == 1 && std::isalpha(static_cast<unsigned char>(c[0]))));
            add("bool", "is_whitespace(" + q + ")", mydsl::Value::boolean(c == " " || c == "\t" || c == "\n" || c == "\r"));
        }
        add("string", "char_at(\"hello\", 1)", mydsl::Value::string("e"));
        const auto values = stdlib_run("strings", program);
        std::size_t mismatches = 0; std::string first;
        for (const auto& e : expected) if (values.at(e.name).str() != e.value.str()) { if (!mismatches++) first = e.name + ": got " + values.at(e.name).str() + ", expected " + e.value.str(); }
        check(mismatches == 0, "string functions agree with std::string over " + std::to_string(expected.size()) + " cases", first);
    }
    // ----- invalid arguments stop the program with a message naming the function, the rule and the value -----
    {
        auto fails_in = [&](const std::string& module, const std::string& call, const std::string& function, const std::string& message) {
            try { stdlib_run(module, "Print(" + call + ");"); check(false, "stdlib rejects " + call); }
            catch (const mydsl::Error& e) {
                const bool named = !e.trace.empty() && e.trace.back().function == function && e.trace.back().file.empty() && e.trace.back().call_site.line == 2 && e.trace.back().call_site.column == 7;
                const bool located = e.file == "stdlib/" + module + ".mydsl" && e.imports.size() == 1 && e.imports[0].file.empty();
                check(e.stage == "Runtime" && named && located && e.message == message, "stdlib rejects " + call + " inside '" + function + "'", e.what());
            }
        };
        fails_in("math", "factorial(-1)", "factorial", "factorial: the argument must not be negative, but n is -1");
        fails_in("math", "factorial(21)", "factorial", "factorial: 21! does not fit in a 64-bit int (the largest is 20!)");
        fails_in("math", "power(2, -1)", "power", "power: the exponent must not be negative, but it is -1 (for negative exponents use power_f from fmath.mydsl)");
        fails_in("math", "isqrt(-1)", "isqrt", "isqrt: the argument must not be negative, but n is -1");
        fails_in("math", "binomial(3, 4)", "binomial", "binomial: needs 0 <= k <= n, but n is 3 and k is 4");
        fails_in("math", "fibonacci(93)", "fibonacci", "fibonacci: fibonacci(93) does not fit in a 64-bit int (the largest index is 92)");
        fails_in("math", "fibonacci(-1)", "fibonacci", "fibonacci: the index must not be negative, but n is -1");
        fails_in("math", "clamp(0, 1, 0)", "clamp", "clamp: low must not exceed high, but low is 1 and high is 0");
        fails_in("math", "power(3, 40)", "power", "Integer overflow: 6561 * 1853020188851841 does not fit in a 64-bit int (range -9223372036854775808 .. 9223372036854775807)");
        fails_in("math", "mod_floor(1, 0)", "mod_floor", "mod_floor: the divisor must not be 0 (dividing 1)");
        fails_in("fmath", "sqrt_f(-1e-300)", "sqrt_f", "sqrt_f: the argument must not be negative, but x is -1e-300");
        fails_in("fmath", "ln_f(-1.0)", "ln_f", "ln_f: the argument must be positive, but x is -1.0");
        fails_in("fmath", "ln_f(0.0)", "ln_f", "ln_f: the argument must be positive, but x is 0.0");
        fails_in("fmath", "pow_f(0.0, -1.0)", "pow_f", "pow_f: 0.0 cannot be raised to the negative exponent -1.0");
        fails_in("fmath", "pow_f(-2.0, 0.5)", "pow_f", "pow_f: the base must not be negative, but it is -2.0 (for an integer exponent use power_f)");
        fails_in("fmath", "clamp_f(0.0, 1.0, 0.0)", "clamp_f", "clamp_f: low must not exceed high, but low is 1.0 and high is 0.0");
        fails_in("fmath", "exp_f(1000.0)", "exp_f", "exp_f: exp(1000.0) does not fit in a float (the argument must be at most 709)");
        fails_in("fmath", "power_f(0.0, -1)", "power_f", "Division by zero: Float(1.0) / Float(0.0)");
        fails_in("fmath", "sin_f(1e19)", "sin_f", "reduce_angle_f: the angle 1e+19 is too large to reduce accurately (sin_f, cos_f and tan_f accept angles up to 1e9 in magnitude)");
        fails_in("fmath", "cos_f(-2e9)", "cos_f", "reduce_angle_f: the angle -2000000000.0 is too large to reduce accurately (sin_f, cos_f and tan_f accept angles up to 1e9 in magnitude)");
        fails_in("strings", "repeat(\"ab\", -2)", "repeat", "repeat: the count must not be negative, but it is -2");
        fails_in("strings", "count_of(\"abc\", \"\")", "count_of", "count_of: the part to count must not be empty");
        fails_in("strings", "replace_all(\"abc\", \"\", \"x\")", "replace_all", "replace_all: the text to replace must not be empty");
        fails_in("strings", "pad_right(\"a\", 3, \"\")", "pad_right", "pad_right: the fill must be exactly one character, but it is \"\"");
        fails_in("strings", "pad_left(\"a\", 3, \"ab\")", "pad_left", "pad_left: the fill must be exactly one character, but it is \"ab\"");
        fails_in("strings", "parse_int(\"\")", "parse_int", "parse_int: \"\" is not the text of an integer (expected an optional '-' followed by digits only)");
        fails_in("strings", "parse_int(\"1 2\")", "parse_int", "parse_int: \"1 2\" is not the text of an integer (expected an optional '-' followed by digits only)");
        fails_in("strings", "parse_int(\"say \\\"no\\\"\")", "parse_int", "parse_int: \"say \\\"no\\\"\" is not the text of an integer (expected an optional '-' followed by digits only)");
        fails_in("strings", "parse_int(\"9223372036854775808\")", "parse_int", "Integer overflow: 9223372036854775800 + 8 does not fit in a 64-bit int (range -9223372036854775808 .. 9223372036854775807)");
        fails_in("strings", "char_at(\"\", 0)", "char_at", "char_at: the string is empty, it has no position 0");
        fails_in("strings", "char_at(\"abc\", -1)", "char_at", "char_at: position -1 is outside \"abc\" (valid positions are 0..2)");
        const auto quoted = stdlib_run("strings", "string a := quote(\"\"); string b := quote(\"plain\"); string c := quote(\"q\\\"b\\\\s\\tt\\nn\\rr\"); string d := escape_char(\"\\n\"); string e := escape_char(\"x\");");
        check(quoted.at("a").text == "\"\"" && quoted.at("b").text == "\"plain\"" && quoted.at("c").text == mydsl::quote_string("q\"b\\s\tt\nn\rr") && quoted.at("d").text == "\\n" && quoted.at("e").text == "x",
              "quote writes a string as a MyDSL literal, like Print; escape_char handles one character", quoted.at("c").text);
        fails_in("math", "div_floor(5, 0)", "div_floor", "div_floor: the divisor must not be 0 (dividing 5)");
        fails_in("math", "mod_floor(-5, 0)", "mod_floor", "mod_floor: the divisor must not be 0 (dividing -5)");
        fails_in("strings", "char_at(\"\", 2)", "char_at", "char_at: the string is empty, it has no position 2");
        fails_in("math", "binomial(67, 33)", "binomial", "Integer overflow: 212336130412243110 * 67 does not fit in a 64-bit int (range -9223372036854775808 .. 9223372036854775807)");
    }
    // ----- review fixes: the edge cases that used to fail or give a wrong value -----
    {
        const auto values = stdlib_run("fmath", "float a := floor_f(1e19); float b := ceil_f(-1e300); float c := round_f(9007199254740993.0); float d := fract_f(1e19); bool e := is_integer_f(1e300); "
                                                "float f := round_f(0.49999999999999994); float g := round_f(-0.49999999999999994); float h := round_f(0.5); float i := round_f(1.4999999999999998); float j := round_f(-1.5); "
                                                "float k := floor_f(4503599627370495.5); float l := ceil_f(4503599627370495.5); float m := sin_f(1e9); bool n := has_no_fraction_f(4503599627370496.0); bool o := has_no_fraction_f(4503599627370495.5);");
        check(values.at("a").floats[0] == 1e19 && values.at("b").floats[0] == -1e300 && values.at("c").floats[0] == 9007199254740993.0 && values.at("d").floats[0] == 0.0 && values.at("e").truth(),
              "rounding functions accept values of 2^52 and beyond (they are integers already)");
        check(values.at("f").floats[0] == 0.0 && values.at("g").floats[0] == 0.0 && values.at("h").floats[0] == 1.0 && values.at("i").floats[0] == 1.0 && values.at("j").floats[0] == -2.0,
              "round_f(0.49999999999999994) is 0.0 (not floor(x + 0.5)); halves still round away from zero");
        check(values.at("k").floats[0] == 4503599627370495.0 && values.at("l").floats[0] == 4503599627370496.0 && std::fabs(values.at("m").floats[0] - std::sin(1e9)) < 1e-6 && values.at("n").truth() && !values.at("o").truth(),
              "the largest floats with a fraction are still rounded; sin_f at the 1e9 bound is still within 1e-6");
        const auto ints = stdlib_run("math", "int smallest := -9223372036854775807 - 1; int a := gcd(smallest, 6); int b := gcd(6, smallest); int c := gcd(smallest, 1); int d := gcd(-12, -18); "
                                             "int e := binomial(62, 31); int f := binomial(66, 33); int g := binomial(60, 30); int h := binomial(64, 1); int i := lcm(-4, 6);");
        check(ints.at("a").ints[0] == 2 && ints.at("b").ints[0] == 2 && ints.at("c").ints[0] == 1 && ints.at("d").ints[0] == 6, "gcd accepts the smallest int (no abs of it)");
        check(ints.at("e").ints[0] == 465428353255261088LL && ints.at("f").ints[0] == 7219428434016265740LL && ints.at("g").ints[0] == 118264581564861424LL && ints.at("h").ints[0] == 64 && ints.at("i").ints[0] == 12,
              "binomial no longer overflows in intermediate products: every result that fits is computed");
        try { stdlib_run("math", "int smallest := -9223372036854775807 - 1; int g := gcd(smallest, smallest);"); check(false, "gcd(smallest, smallest)"); }
        catch (const mydsl::Error& e) { check(e.message.rfind("Integer overflow: abs(-9223372036854775808)", 0) == 0 && !e.trace.empty() && e.trace[0].function == "gcd", "gcd(smallest, smallest) = 2^63 does not fit: an overflow inside gcd", e.what()); }
    }
    // ----- errors raised across three modules: program -> helper module -> standard library -----
    {
        const auto modules = mydsl::load_modules(read_file("tests/stdlib/err_trace_across_modules.mydsl"), "tests/stdlib/err_trace_across_modules.mydsl");
        mydsl::typecheck(modules);
        std::istringstream in; std::ostringstream out;
        try { mydsl::interpret(modules, in, out); check(false, "three-level error"); }
        catch (const mydsl::Error& e) {
            check(out.str() == "Int(25)\n" && e.stage == "Runtime" && e.file == "stdlib/math.mydsl" && e.location.line == 85 && e.location.column == 23 &&
                  e.message == "factorial: the argument must not be negative, but n is -2", "the error is located in the library file with the library's message", e.what());
            check(e.trace.size() == 3 && e.trace[0].function == "factorial" && e.trace[0].file == "tests/stdlib/helpers/checked_math.mydsl" && e.trace[0].call_site.line == 3 && e.trace[0].call_site.column == 48 &&
                  e.trace[1].function == "checked_factorial" && e.trace[1].file.empty() && e.trace[1].call_site.line == 2 && e.trace[1].call_site.column == 37 &&
                  e.trace[2].function == "report" && e.trace[2].file.empty() && e.trace[2].call_site.line == 4 && e.trace[2].call_site.column == 7,
                  "each trace frame names the file of its call site (innermost first)");
            check(e.imports.size() == 2 && e.imports[0].file == "tests/stdlib/helpers/checked_math.mydsl" && e.imports[0].location.line == 2 && e.imports[1].file.empty() && e.imports[1].location.line == 1,
                  "the import chain tells how the library file was reached");
            check(std::string(e.what()) == read_file("tests/stdlib/err_trace_across_modules.err").substr(0, read_file("tests/stdlib/err_trace_across_modules.err").size() - 1), "the rendered message equals the hand-checked .err file", e.what());
        }
    }
}

// ----- the error statement: error("message"); -----
void error_statement_tests() {
    // Syntax: a statement like Print, with one string-typed argument.
    expect_ast("error(\"stop\");", "(error (string \"stop\"))\n");
    expect_ast("error(\"n is \" + to_string(n));", "(error (binary + (string \"n is \") (call to_string (variable n))))\n");
    expect_ast("function int f(int n) { if (n < 0) then { error(\"negative\"); }; return n; };",
               "(function int f (parameter int n)\n  (block\n    (if (binary < (variable n) Int(0))\n      (block\n        (error (string \"negative\"))))\n    (return (variable n))))\n");
    {
        const auto s = mydsl::parse("  error(\"m\");").at(0);
        check(s->op == "error" && s->exprs.size() == 1 && s->exprs[0]->op == "string" && s->location.column == 3 && s->span.end.column == 14, "error statement node, location and span through ';'");
    }
    expect_error("error;", 1, 6, "Expected '(' after 'error' but found ';'");
    expect_error("error();", 1, 7, "error needs a message, e.g. error(\"the count must not be negative\");");
    expect_error("error(\"a\", \"b\");", 1, 10, "error takes exactly one argument, the message (use + and to_string to build it)");
    expect_error("error(\"a\"", 1, 10, "Expected ')' to close the error message opened at line 1, column 6 but found end of input");
    expect_error("error(\"a\")", 1, 11, "Expected ';' to end the error statement but found end of input");
    expect_error("int x := error(\"a\");", 1, 10, "'error' is a statement that stops the program and cannot be used inside an expression; write it on its own: error(\"message\");");
    expect_error("Print(error(\"a\"));", 1, 7, "'error' is a statement that stops the program and cannot be used inside an expression");
    expect_error("int error := 1;", 1, 5, "Cannot use keyword 'error' as a variable name");
    expect_error("function int error() { return 1; };", 1, 14, "Cannot use keyword 'error' as a function name");
    expect_ast("errors := 1;", "(assign errors Int(1))\n"); // only the exact word is reserved
    // Types: the message must be a string; the statement itself has no value.
    expect_types("error(\"x\");", "");
    expect_types("string s := \"a\"; error(s + to_string(1) + to_string(2.5) + to_string(true));", "s: string\n");
    expect_type_error("error(1);", 1, 7, "The message of error must be a string but is int (build it with to_string(...) and +)");
    expect_type_error("error(true);", 1, 7, "The message of error must be a string but is bool");
    expect_type_error("error([1, 2]);", 1, 7, "The message of error must be a string but is intvector 2");
    expect_type_error("error(\"n = \" + 3);", 1, 14, "Operator '+' cannot be applied to string and int");
    expect_type_error("error(Input());", 1, 7, "Input() can only be used as the whole initializer of a declaration");
    expect_type_error("error(undefined);", 1, 7, "Undeclared variable 'undefined'");
    // An error statement ends a path, so a branch that only errors needs no return.
    expect_types("function int f(int n) { if (n > 0) then { return n; } else { error(\"no\"); }; };", "");
    expect_types("function int g() { error(\"never returns\"); };", "");
    expect_types("function int h(int n) { { error(\"in a block\"); }; };", "");
    expect_type_error("function int f(int n) { if (n > 0) then { error(\"no\"); }; };", 1, 1, "Function 'f' must return int on every path");
    expect_type_error("function int f(int n) { while (true) { error(\"loop\"); }; };", 1, 1, "Function 'f' must return int on every path");
    // Execution: a Runtime error with the message, located at the statement, with the trace.
    expect_runtime_error("Print(1);\nerror(\"stopped\");\nPrint(2);", 2, 1, "stopped", "Int(1)\n");
    expect_runtime_error("int n := 7;\nerror(\"n is \" + to_string(n));", 2, 1, "n is 7");
    expect_runtime_error("error(\"\");", 1, 1, "");
    expect_runtime_error("error(\"quotes \\\" and a\\nnewline\");", 1, 1, "quotes \" and a\nnewline");
    expect_runtime_error("function int f(int n) {\n  if (n < 0) then {\n    error(\"f: negative: \" + to_string(n));\n  };\n  return n;\n};\nPrint(f(1));\nPrint(f(-2));", 3, 5,
                         "f: negative: -2 (in function 'f' called from line 8, column 7)", "Int(1)\n");
    expect_runtime_error("function int inner(int n) { error(\"deep \" + to_string(n)); };\nfunction int outer(int n) { return inner(n + 1); };\nPrint(outer(1));", 1, 29,
                         "deep 2 (in function 'inner' called from line 2, column 36; in function 'outer' called from line 3, column 7)");
    expect_runtime_error("for (i := 0, i < 5, 1) { Print(i); if (i == 2) then { error(\"at \" + to_string(i)); }; };", 1, 55, "at 2", "Int(0)\nInt(1)\nInt(2)\n");
    // The message is evaluated first: an error inside it wins, and a runtime error in it is reported there.
    expect_runtime_error("error(to_string(1 / 0));", 1, 19, "Division by zero: 1 / 0");
    {
        try { output_of("error(\"structured\");"); check(false, "error statement throws"); }
        catch (const mydsl::Error& e) { check(e.stage == "Runtime" && e.message == "structured" && e.location.line == 1 && e.trace.empty() && e.file.empty(), "Error fields of an error statement"); }
        const std::string json = mydsl::run_json("Print(1);\nerror(\"json\");", "", {});
        check(valid_json(json) && json.find("{\"output\":\"Int(1)\\n\",\"error\":{\"stage\":\"Runtime\",\"line\":2,\"column\":1,\"message\":\"json\"},\"variables\":[]") != std::string::npos, "run_json of an error statement", json);
        const std::string ast = mydsl::debug_json("error(\"x\");");
        check(valid_json(ast) && ast.find("{\"node\":\"statement\",\"op\":\"error\",\"location\":{\"line\":1,\"column\":1},\"begin\":{\"line\":1,\"column\":1},\"end\":{\"line\":1,\"column\":12},\"exprs\":[{\"node\":\"expression\",\"op\":\"string\",\"name\":\"x\",\"type\":\"string\"") != std::string::npos, "debug_json of an error statement", ast);
    }
    // Inside an imported module: file, chain and the trace to the program's call.
    expect_module_error("import \"lib/raising.mydsl\";\nPrint(checked(2));\nPrint(checked(-1));", "Runtime", "tests/modules/lib/raising.mydsl",
                        "Runtime error in 'tests/modules/lib/raising.mydsl' at line 3, column 9: checked: the value must not be negative, but it is -1 (in function 'checked' called from line 3, column 7) (imported from the main program at line 1, column 1)", "Int(2)\n");
    expect_module_error("import \"lib/raising.mydsl\";\nfunction int twice_checked(int n) { return 2 * checked(n); };\nPrint(twice_checked(-5));", "Runtime", "tests/modules/lib/raising.mydsl",
                        "Runtime error in 'tests/modules/lib/raising.mydsl' at line 3, column 9: checked: the value must not be negative, but it is -5 (in function 'checked' called from line 2, column 48; in function 'twice_checked' called from line 3, column 7) (imported from the main program at line 1, column 1)");
    // An imported module may not have a top-level error statement (it is a statement like any other).
    expect_module_error("import \"lib/top_level_error.mydsl\";", "Type", "tests/modules/lib/top_level_error.mydsl",
                        "Type error in 'tests/modules/lib/top_level_error.mydsl' at line 1, column 1: An imported module may only contain function definitions and imports, but this is an error statement (the top-level statements of an imported module are never executed; move it into a function or into the main program) (imported from the main program at line 1, column 1)");
}

void pipeline_tests() {
    std::istringstream in; std::ostringstream out;
    std::ostringstream checked;
    mydsl::run("int a := 1; float b := 2.0; Print(a);", in, checked, false, true);
    check(checked.str() == "a: int\nb: float\nType check passed: 3 top-level statement(s), 2 top-level variable(s)\n", "--check output", checked.str());
    try { mydsl::run("int := ;", in, out, false, true); check(false, "--check reports syntax errors"); }
    catch (const mydsl::Error& e) { check(std::string(e.what()).rfind("Parse error", 0) == 0, "--check syntax error"); }
    try { mydsl::run("int a := true;", in, out, false, true); check(false, "--check reports type errors"); }
    catch (const mydsl::Error& e) { check(std::string(e.what()).rfind("Type error at line 1, column 1:", 0) == 0, "--check type error"); }
    std::ostringstream ran;
    mydsl::run("int a := 6; Print(a * 7);", in, ran);
    check(ran.str() == "Int(42)\n", "run executes the program", ran.str());
    try { mydsl::run("Print(1/0);", in, out); check(false, "run reports runtime errors"); }
    catch (const mydsl::Error& e) { check(std::string(e.what()).rfind("Runtime error at line 1, column 8: Division by zero", 0) == 0, "run runtime error", e.what()); }
    try { mydsl::run("Print(999); int a := true;", in, out); check(false, "no execution before type checking"); }
    catch (const mydsl::Error& e) { check(std::string(e.what()).rfind("Type error", 0) == 0 && out.str().empty(), "static errors prevent execution (nothing printed)"); }
    std::ostringstream quiet;
    mydsl::run("Print(1); Print(2);", in, quiet, true);
    check(quiet.str().empty(), "quiet mode discards the program output");
    try { mydsl::run("int := ;", in, quiet, true); check(false, "quiet mode still reports errors"); } catch (const mydsl::Error&) { ++passed; }
    std::ostringstream functions;
    mydsl::run("function float half(float x) { return x / 2.0; };\nfunction bool ok() { return true; };\nint a := 1;", in, functions, false, true);
    check(functions.str() == "a: int\nhalf: function(float x) -> float\nok: function() -> bool\nType check passed: 3 top-level statement(s), 1 top-level variable(s), 2 function(s)\n",
          "--check lists function signatures", functions.str());
    std::ostringstream tokens;
    mydsl::print_tokens(mydsl::lex("int a := 1;"), tokens);
    check(tokens.str() == "1:1 word \"int\"\n1:5 word \"a\"\n1:7 symbol \":=\"\n1:10 integer \"1\"\n1:11 symbol \";\"\n1:12 end \"<eof>\"\n", "print_tokens", tokens.str());
}
} // namespace

int main() {
    try {
        lexer_tests();
        statement_tests();
        expression_tests();
        location_tests();
        error_tests();
        nesting_tests();
        example_tests();
        json_tests();
        span_tests();
        type_tests();
        function_syntax_tests();
        function_type_tests();
        string_tests();
        string_builtin_tests();
        inspector_tests();
        interpreter_tests();
        function_runtime_tests();
        vector_matrix_tests();
        input_tests();
        module_syntax_tests();
        module_loader_tests();
        module_semantics_tests();
        stdlib_tests();
        error_statement_tests();
        pipeline_tests();
    } catch (const std::exception& e) {
        std::cerr << "unexpected exception: " << e.what() << '\n';
        return 1;
    }
    if (failures) {
        std::cerr << failures << " of " << (passed + failures) << " checks failed\n";
        return 1;
    }
    std::cout << passed << " lexer/parser/type-checker/interpreter checks passed\n";
    return 0;
}

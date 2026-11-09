# MyDSL

A small programming language implemented in C++17 with scalar values, strings, vectors,
matrices, control flow, runtime input and user-defined functions. See `REVIEW.md` for the review of the
core pipeline: what was checked, the results and the known limitations.

## Current status

The tokenizer works and reports token kinds and source locations. The parser is complete: it
turns a program into the AST described in `include/mydsl.hpp` (expressions with precedence,
declarations, assignments, blocks, if/else, while, for, vector and matrix literals, calls and
indexing) and reports the first syntax error with its line and column. The static type checker
is complete: it verifies declarations, assignments, scoping, operators, builtin function
arguments, indexing and vector/matrix dimensions and annotates every expression with its type
(see "Type rules"). The interpreter executes the whole language: scalars, vectors and matrices
with all operators and builtins (indexing, element assignment, dot product, length, angle,
transpose, matrix product, determinant, inverse), checked integer arithmetic, explicit runtime
errors and `Input()` from standard input or data files (see "Execution rules"). User-defined
functions (`function int square(int x) { return x * x; };`) are parsed, fully type-checked -
signatures, argument counts and types, return types, missing returns, duplicate definitions -
and executed: calls are ordinary expressions, arguments are passed by value, each call has its
own scope, functions call each other and recurse (with a call depth limit), and a runtime error
inside a body reports the position in the body plus the chain of calls (see "Functions").
Strings (`string s := "Hello, " + name;`) are a scalar type with literals, concatenation,
equality and printing, and never convert to or from other types (see "Strings"). A
browser debugging view (`mydsl --serve`) shows source, tokens, AST, inferred types, program
input and output side by side using the C++ implementation throughout. Programs can be split
into modules (see "Modules"): `import "lib/math_utils.mydsl";` loads a file of function
definitions (located relative to the importing file, tokenized and parsed with the same
pipeline, loaded once however many files import it) and makes its functions - and those of the
files it imports - callable; every module is type-checked, and errors inside imported code name
the file, the call trace and the chain of imports. `mydsl --modules` lists the loaded modules
in dependency order. A standard library written in MyDSL itself (`stdlib/`: int and float
arithmetic, text; see "Standard library") shows the module system in use.

The intended pipeline is:

```text
source → tokens → AST → module loading (imports) → type checking → execution
```

## Build and run

Requires a C++17 compiler and Make. Run from the project directory:

```sh
make -j2
./mydsl inputs/factorial.txt
make test
```

`./mydsl program.txt` parses, type-checks and runs the program: `Print` output goes to stdout,
and a lexing, parse, type or runtime error goes to stderr with its line and column and exit
status 1. A program with a static error never starts executing. Other modes:

| Flag | Effect |
| --- | --- |
| `--quiet` | Run, but discard the program's output (only errors are reported). |
| `--tokens` | Print the tokens (`line:column kind text`) instead of running. |
| `--ast` | Print the AST instead of running (exit 1 on a syntax error). |
| `--modules` | Load the program and, transitively, the files it imports; print the modules in dependency order with their imports and functions (exit 1 on a missing file, duplicate or circular import, or a syntax error in any file). |
| `--check` | Parse, load imports and type-check only; print the top-level variables with their types and the function signatures (imported functions with their module). |
| `--json` | Tokens, AST, inferred types and any lexer/parser/type error as JSON (always exit 0). |
| `--run-json` | Run and print output, error (with the call trace), the final top-level variables and timing as JSON (what the browser's Run button uses). |
| `--language-json` | Print the keywords, type names and builtin functions with a short description of each as JSON (what the browser editor's completion uses; takes no source file). |
| `--serve [PORT]` | Start the browser debugging view (default port 8000, `0` picks a free port). |
| `--input FILE` | Read the program's `Input()` values from FILE instead of standard input. |

```sh
./mydsl inputs/factorial.txt            # Int(120)
./mydsl --input tests/data/binary_search.txt inputs/binary_search.txt   # Int(3)
echo '[1., 3., 2.] 3' | ./mydsl program.txt                             # Input() values from a pipe
./mydsl inputs/error_input1.txt         # Runtime error at line 2, column 20: Division by zero: Float(-23.0) / Float(0.0)
./mydsl --ast inputs/factorial.txt      # print the AST of a file
echo 'x := 1 + 2 * 3;' | ./mydsl --ast -  # or of stdin
./mydsl --check inputs/error_input2.txt # Type error at line 3, column 1: Cannot initialise 'a' of type int with a value of type bool
./mydsl --check inputs/functions.txt    # a: int, square: function(int x) -> int, ... (functions are listed after the variables)
./mydsl inputs/fibonacci.txt            # recursive functions: Int(0) Int(1) Int(1) Int(2) ... Int(2880067194370816120)
./mydsl --modules tests/modules/nested.mydsl   # the files a program imports, in dependency order (see "Modules")
./mydsl stdlib/examples/geometry.mydsl  # the standard library: sqrt_f, sin_f, pow_f, ... written in MyDSL (see "Standard library")
make serve                              # browser view on http://127.0.0.1:8000/ (PORT=9000 to change)
make sanitize   # Run all checks with address/undefined-behaviour sanitizers
make clean      # Remove generated files
```

`make test` runs four layers of checks:

- `tests/tests.cpp`: unit tests for tokenization, parsing (including the malformed programs in
  `tests/syntax_errors/`), type checking (`tests/type_errors/`), user-defined functions (syntax,
  signatures, calls, returns, scoping and every diagnostic; execution of calls, by-value
  arguments, locals, early returns, recursion, runtime errors with call traces, the recursion
  and nesting limits), execution (outputs, scoping, loops, runtime errors, limits,
  vector/matrix results checked against values computed with NumPy or exact fractions,
  runtime input from strings and `tests/data/`) and the JSON interface.
- `tests/cli.sh`: command-line modes, exit statuses, the HTTP server and, when Node is
  installed, the browser page's script (`tests/web.test.js`) against real `--json` output and -
  when Playwright is installed as well - the source editor in a real browser
  (`tests/editor.test.js`: highlighting, indentation, bracket matching, undo/redo, shortcuts,
  completion, the page's markers and clicks through the editor, and - with a server started in
  a throwaway project directory - the file panel: opening, editing, saving, tabs, unsaved
  marks and warnings, reloads, conflicts, read-only files, the path checks of the file API,
  and debugging across files: a runtime error two imports deep whose message and trace frames
  open the right files at the right positions, a syntax error typed into an unsaved library
  that the program's parse reports, a run that uses an unsaved library, single-file programs).
- `tests/e2e.sh`: end-to-end programs in `tests/e2e/` with hand-written expected output and
  error files (precedence, scoping, loops, vectors, matrices, runtime input, functions,
  recursion, early returns, every error stage);
  each is run through the command line and through the browser endpoints, which must return
  identical JSON.
- `tests/modules.sh`: multi-file programs in `tests/modules/` (calls across files, nested and
  diamond imports, transitive visibility, recursion and `Input()` inside imported code, paths
  relative to the importing file, and every loader, type and runtime diagnostic) with
  hand-written expected output, `--modules` listings and error lines; plus resolution from
  other working directories, symbolic links, absolute paths, the nesting limit and the browser
  endpoints against the command line (see "Modules").
- `tests/stdlib.sh`: the standard library's examples and `tests/stdlib/` (edge cases and every
  invalid-argument and overflow path) against hand-written outputs and error lines, each module
  on its own, name checks against builtins and keywords, the examples from other directories
  and the library from standard input; `tests/tests.cpp` (`stdlib_tests`) compares the numeric
  functions with the C library at full precision and the int/string functions with reference
  implementations.
- Every example in `inputs/` is parsed, type-checked and run with its data file.

`make sanitize` repeats all of this with AddressSanitizer and UndefinedBehaviorSanitizer. The
programs in `inputs/` are examples, not a complete language specification or test suite.

## Type rules

The checker (`src/type_checker.cpp`) runs after parsing and stops at the first problem with a
`Type error at line L, column C: ...` message. Where the examples left room for interpretation the
following rules were chosen; each is a few lines to change.

Types are `int`, `float`, `bool`, `string`, `intvector N`, `floatvector N`, `intmatrix R,C` and
`floatmatrix R,C`. **Types must match exactly** - there is no implicit conversion between `int`
and `float` (`1.0 * a` with `a` an int is an error, as in `arithmetic_op.txt`) and none at all
to or from `string`; `to_float(x)` and `to_int(x)` convert scalars, vectors and matrices
explicitly. Vector and matrix sizes are part of the type, so `intvector 3` and `intvector 2` are
different types.

| Construct | Rule |
| --- | --- |
| `int a := e;` / `a := e;` | `e` must have exactly the declared type. A variable must be declared before use and may not be declared twice in the same scope (`int a := 1; int a := 2;` is an error); an inner block may shadow an outer variable. The variable is not in scope inside its own initializer. |
| `{ ... }`, `if`/`while`/`for` bodies | Each block opens a new scope; its declarations disappear at `}`. |
| `for (i := e0, cond, step)` | Declares a fresh `int` loop variable `i`, visible in the header and body only (an outer `i` is shadowed, not modified). `e0` and `step` must be `int`, `cond` must be `bool`. |
| `if (c)`, `while (c)` | `c` must be `bool`. |
| `+ - * /` | Two `int` or two `float` operands give that type. Two `bool` operands: `+` is "or" and `*` is "and" (`b + true` in `logical_tests.txt`); `-` and `/` are errors. Two `string` operands: `+` concatenates; `- * /` are errors. Two vectors or two matrices of the same type: `+` and `-` element-wise; `*` and `/` are errors (use `dotprod_vec`/`mult_mat`). Scalar `*` vector/matrix, vector/matrix `*` scalar and vector/matrix `/` scalar scale element-wise when the element type matches. |
| `%` | `int % int` only. |
| `< > <= >=` | Two `int` or two `float` operands, result `bool`. |
| `== !=` | Two operands of exactly the same type (including vectors and matrices), result `bool`. |
| `&& \|\| !` | `bool` only. |
| unary `-` | `int`, `float`, vector or matrix. |
| `[e1, e2, ...]`, `[[...], ...]` | All elements must be `int` or all `float`; `bool`, `string` or nested vectors are errors. The size comes from the literal (`(3 [1,2,3])` must agree, checked by the parser). |
| `v[i]`, `m[i, j]` | A vector takes exactly one index, a matrix exactly two; indices must be `int`; the result is the element type. A literal index (such as `v[3]` or `m[0, -1]`) is bounds-checked statically; other indices are checked at run time. |
| `v[i] := e`, `m[i, j] := e` | As above; `e` must have the element type. |
| `Print(e)` | Any type. |
| `Input()` / `Input("file")` | Takes the type of the variable it is stored in (any type, including `string`), so it may only be the whole initializer of a declaration, the whole right-hand side of an assignment or the whole value of a `return` (`int a := Input() + 1;` is an error). The optional argument must be a string literal. |
| `abs(x)` | `int -> int`, `float -> float`, vector -> `float` (its length). |
| `to_int(x)`, `to_float(x)` | Same shape with the element type changed. |
| `determinant(m)` | Square matrix -> its element type (`int` for `intmatrix`, `float` for `floatmatrix`). |
| `inverse_mat(m)` | Square `floatmatrix N,N` -> same type (`intmatrix` must be converted with `to_float` first). |
| `transpose(m)` | `matrix R,C -> matrix C,R`. |
| `mult_mat(a, b)` | `matrix R,K` times `matrix K,C` of the same element type -> `matrix R,C`; a mismatch of `K` is reported with both sizes. |
| `inv_vec(v)` | Vector -> same type. |
| `dotprod_vec(a, b)` | Two vectors of the same type and size -> element type. |
| `angle_vec(a, b)` | Two vectors of the same type and size -> `float`. |
| `is_zero(x)` | Vector or matrix -> `bool`. |
| `length(s)` | `string -> int`: the number of characters (bytes). |
| `substring(s, start, count)` | `string, int, int -> string`: `count` characters from the 0-based `start`. A literal negative `start` or `count` is a type error; other invalid ranges are runtime errors. |
| `to_string(x)` | `int`, `float`, `bool` or `string` -> `string`: the value's literal text (`"42"`, `"2.5"`, `"3.0"`, `"true"`); vectors and matrices are errors. |

Unknown functions and wrong argument counts are errors. The checker stores the type of every
expression in `Expr::type` (shown in the browser view and `--json`) and returns the types of the
top-level variables, which `--check` prints.

## Strings

`string` is a scalar type alongside `int`, `float` and `bool`:

```text
string name := "MyDSL";
string line := "Hello, " + name + "!";          // + concatenates two strings
Print(line);                                    // String("Hello, MyDSL!")
Print(name == "MyDSL");                         // Bool(true)
Print("tab\t quote\" backslash\\ newline\n");   // String("tab\t quote\" backslash\\ newline\n")
function string twice(string s) { return s + s; };
```

- **Literals** are double-quoted and stay on one line. The escapes are `\"`, `\\`, `\n`, `\t`
  and `\r`; any other backslash sequence is a lexing error (`Unknown escape sequence '\q' ...`),
  as is a missing closing quote (`Unterminated string literal ...`, reported at the opening
  quote). `""` is the empty string. Other bytes (UTF-8 text, raw tabs) pass through unchanged.
- **Operations**: `+` between two strings (concatenation), `==` and `!=` between two strings
  (exact, case-sensitive comparison of the characters), `Print`, `Input()`, declarations,
  assignments, function parameters and return values. Strings are copied on assignment and
  passed by value.
- **Builtins** (`inputs/string_functions.txt`):
  - `length(s)` is the number of characters; `length("")` is 0. Characters are bytes, so
    `length("café")` is 5.
  - `substring(s, start, count)` returns `count` characters starting at the 0-based `start`.
    The range must lie inside the string: `0 <= start <= length(s)`, `count >= 0` and
    `start + count <= length(s)`; `substring(s, length(s), 0)` and any `count` of 0 give `""`.
    Anything else is a runtime error that names the range (`substring: start 2 + count 4
    exceeds the length 5 of the string (at most 3 characters are available from 2)`), and a
    negative literal argument is already a type error. There is no clamping, in line with the
    strict index rules for vectors.
  - `to_string(x)` converts an `int`, `float`, `bool` or `string` to its literal text - exactly
    the text `Print` shows inside the parentheses (`42`, `-7`, `2.5`, `3.0`, `1e+20`, `true`),
    so the result reads back as a value of the original type through `Input()`. Vectors and
    matrices are rejected (`to_string expects an int, float, bool or string but got
    intvector 2`). There is still no parsing in the other direction.
- **Everything else is a type error** with a message naming the types: arithmetic or ordering
  on strings (`Operator '-' cannot be applied to strings (only + for concatenation and == / !=
  are defined)`, `Operator '<' requires int or float operands but got string and string`),
  mixing with other types (`Operator '+' cannot be applied to string and int (MyDSL does not
  convert between string and other types)`, `Cannot initialise 'n' of type int with a value of
  type string ...`), strings as conditions, indices, vector elements, builtin arguments, `!`/`-`
  operands. There is no string indexing (use `substring(s, i, 1)`), ordering or parsing of
  numbers from strings.
- **Printing** writes `String("...")` with the literal escapes applied, so the output stays one
  line per value and can be pasted back as a literal; `--tokens`, `--ast` and the browser view
  show string tokens and nodes the same way (the token holds the decoded text).
- **Limit**: a concatenation result may not exceed 1,048,576 characters (`kMaxStringLength` in
  `src/interpreter.cpp`), so `s := s + s` in a loop ends with `String length limit of 1048576
  characters exceeded ...` instead of exhausting memory. `inputs/strings.txt` is a worked example.

## Functions

User-defined functions follow the shape of the other compound statements - a keyword, a
parenthesised header, a block and the terminating `;` - with the type written before the name
as in declarations:

```text
function int square(int x) {
    return x * x;
};
function float mean(floatvector 3 v) {
    float sum := 0.0;
    for (i := 0, i < 3, 1) { sum := sum + v[i]; };
    return sum / 3.0;
};
int a := square(4) + square(square(2));
Print(mean([1.0, 2.0, 6.0]));
```

Calls are ordinary expressions (`square(4)`, or `pair(1, 2)[0]` for a vector-valued function),
so they can be nested, indexed and passed to builtins; a call on its own is still not a
statement. The rules, all checked statically (`src/type_checker.cpp`):

| Rule | Error example |
| --- | --- |
| A function is defined at the top level only, with a return type, a name and a typed parameter list (`function int f(int x, floatvector 3 v) {...};`); there is no `void`. `function`/`return` are keywords. | `function square(int x)` -> *Expected a return type after 'function'*; `function int f(x)` -> *Parameter 'x' of function 'f' needs a type*; a definition inside a block or another function -> *A function can only be defined at the top level* |
| All definitions are collected before anything is checked, so a function may be called before its definition and may be recursive (also mutually). Each name is defined once and may not be a builtin; there is no overloading. | *Function 'f' is already defined (at line 1, column 1)*; *Cannot define function 'abs': 'abs' is a built-in function* |
| Parameter names must be unique. They are the variables of the body's outermost scope: the body may assign to them (arguments are passed by value, like every assignment) and inner blocks may shadow them, but the body may not redeclare them. | *Function 'f' declares parameter 'x' twice*; *Variable 'x' is already declared in this scope (as parameter int at ...)* |
| A body sees only its parameters and its own locals - never top-level variables (pass them as parameters). Functions and variables are separate namespaces, as they were for `Input`. | *Undeclared variable 'g' (inside function 'f' only its parameters and local variables are visible ...)*; `Print(f)` -> *'f' is a function; call it with arguments: f(...)*; `v(1)` with a variable `v` -> *'v' is a variable of type int, not a function* |
| A call passes exactly the declared number of arguments, each with exactly the parameter's type (sizes included, no int/float conversion), and has the declared return type. | *Function 'add' expects 2 arguments (int a, int b) but got 1*; *Argument 2 of 'add' must be float (parameter 'b') but got int*, reported at the argument |
| `return e;` is only valid inside a function body and `e` must have exactly the return type (`return Input();` reads a value of that type). | *'return' can only be used inside a function body* (parse error); *Function 'f' must return int but this return statement has a value of type float* |
| Every path through the body must end in a `return` or an `error(...)`: a block returns if one of its statements does, an `if` only when both branches do, and loops never count (they may run zero times). | *Function 'f' must return int on every path, but the end of its body can be reached without a return statement* |
| `error(message);` stops the program with `message` (a `string`; build it with `+` and `to_string`). It is a statement like `Print`, allowed anywhere, and counts as the end of a path. | `error(42);` -> *The message of error must be a string but is int (build it with to_string(...) and +)*; `x := error("m")` -> *'error' is a statement that stops the program and cannot be used inside an expression* (parse error) |

Unknown function names list the builtins and the functions defined in the program. `--check`
prints each signature as `square: function(int x) -> int` after the variables, `--ast` prints
`(function int square (parameter int x) (block ...))` and `(return e)`, and the JSON/browser
view shows a `function` node with clickable `parameter` children and the `return` statements.

**Execution.** The interpreter registers every definition before the program starts (so a call
may precede its definition); reaching a definition does nothing. A call:

1. evaluates the arguments left to right in the caller's scope (an error there is the caller's,
   without a call trace);
2. binds them *by value* to the parameters in a fresh scope that has no parent - exactly the
   checker's view: the body sees its parameters, its own locals and nothing else, and changing
   a parameter or a vector element inside the body never affects the caller;
3. runs the body; the first `return` reached ends the call immediately, also from inside nested
   blocks, `if`s, `while` and `for` loops (the loop variable and locals vanish with the call);
4. yields the returned value to the calling expression. Locals are fresh for every call, so
   recursive calls (including mutual recursion) each have their own variables.

`Print` and `Input()` work inside bodies as anywhere else. A runtime error inside a body is
reported at its position in the body, followed by the chain of active calls, innermost first:
`Runtime error at line 3, column 14: Division by zero: Float(1.0) / Float(0.0) (in function
'ratio' called from line 6, column 12; in function 'scaled' called from line 9, column 7)`.
Consecutive identical frames are collapsed (`... called from line 3, column 10 [999 times]`) and
at most five entries are listed (`; ... 995 more call(s)`). The trace is also available
structurally (`Error::trace`, the `trace` array of `--run-json`; the browser shows it as
clickable lines). Output printed before the error is kept, as for every runtime error.

**Recursion limit.** At most 1000 calls may be active at once (`kMaxCallDepth` in
`src/interpreter.cpp`, like Python's default); the 1001st fails with `Call depth limit of 1000
exceeded while calling 'f' (is a recursive function missing its base case?)` plus the trace.
This is a language rule, not a crash guard: the interpreter runs on a thread with a 128 MB stack
and additionally bounds the nesting of statements and expressions being evaluated (20000
activations, `kMaxNesting`), so even contrived programs - hundreds of nested blocks inside deep
recursion - end with a clear runtime error rather than a stack overflow, in every build
configuration including the sanitizers. The browser's 2 s / 256 KB limits apply inside
functions too and are reported with the trace. `inputs/functions.txt`, `fibonacci.txt`,
`gcd.txt`, `search.txt` and `vector_functions.txt` are worked examples.

## Modules

A program can be split into several files: `import "file";` makes the functions defined in
another MyDSL file - and in the files that one imports - callable as if they were defined in the
program. Imported modules are libraries: they contain function definitions and imports only,
nothing in them is executed on its own, and the usual rules apply unchanged to every body
wherever it is defined (parameters and locals only, by-value arguments, recursion, return paths).
Single-file programs are unaffected: a program without imports is parsed, checked and run
exactly as before and never touches the file system.

```text
// lib/vectors.mydsl                         // lib/math_utils.mydsl
function float norm(floatvector 3 v) {       import "vectors.mydsl";
    return abs(v);                           function int square(int x) {
};                                               return x * x;
                                             };
// main.mydsl                                function float mean(floatvector 3 v) {
import "lib/math_utils.mydsl";                   return (v[0] + v[1] + v[2]) / 3.0;
Print(square(3));                            };
Print(norm([3.0, 0.0, 4.0]));                // norm is visible in main through math_utils
```

```sh
$ ./mydsl main.mydsl
Int(9)
Float(5.0)
$ ./mydsl --modules main.mydsl
lib/vectors.mydsl
  functions: norm
lib/math_utils.mydsl
  imports: "vectors.mydsl" at line 1, column 1 -> lib/vectors.mydsl
  functions: square, mean
main.mydsl (main program)
  imports: "lib/math_utils.mydsl" at line 1, column 1 -> lib/math_utils.mydsl
  functions: (none)
  other top-level statements: 2
3 modules in dependency order (each module after the modules it imports, the main program last)
```

The rules (`src/parser.cpp` for the syntax, `src/modules.cpp` for the loader,
`src/type_checker.cpp` and `src/interpreter.cpp` for the semantics):

| Rule | Error example |
| --- | --- |
| **Syntax.** `import "file";` - the keyword `import`, the file name as a string literal (file names contain `/` and `.`, and `Input("file")` already names files this way) and the terminating `;` like every statement. `import` is a keyword and cannot be used as a name. An import may appear anywhere at the top level, like a function definition (all imports are resolved before anything is checked), but not inside a block, loop or function body. The file name is taken literally: no extension is added (`.mydsl` is a convention, the examples in `inputs/` use `.txt`) and there is no search path. | `import math_utils;` -> *Expected the file name as a string literal after 'import' (e.g. import "math_utils.mydsl";) but found 'math_utils'*; `import "";` -> *The file name of an import must not be empty*; inside a block -> *An import can only appear at the top level of the program, not inside a block or loop*; `int import := 1;` -> *Cannot use keyword 'import' as a variable name* |
| **Resolution.** A relative file name is resolved against the directory of the file that contains the import - not the working directory - so `lib/math_utils.mydsl` can import `"vectors.mydsl"` next to it and `app/main.mydsl` can import `"../lib/vectors.mydsl"`, and the program gives the same result from any working directory. Absolute names are used as they are. A program read from standard input (`./mydsl -`) or typed into the browser page has no directory, so its imports are resolved against the working directory (as `Input("file")` is). | *Cannot find imported file "lib/nope.mydsl" (resolved to 'tests/modules/lib/nope.mydsl' relative to the directory of 'tests/modules/err_missing.mydsl')*; *Cannot import "lib": 'tests/modules/lib' is a directory, not a MyDSL source file*; *Cannot import "/dev/null": '/dev/null' is not a regular file*; *Cannot read imported file "a.mydsl" ('lib/a.mydsl': Permission denied)* |
| **Loading.** Every imported file goes through the same tokenizer and parser as the main program (`--ast` of the file shows what was loaded). A lexing or parse error inside an imported file is reported with the stage, the file, the position in that file and the chain of imports that led there, innermost first. Errors in the main program itself keep their usual form without a file name, so "no file name" always means "the file you ran". | *Parse error in 'tests/modules/lib/broken_syntax.mydsl' at line 3, column 1: Expected ';' to end the return statement but found '}' (imported from 'tests/modules/lib/imports_broken.mydsl' at line 3, column 1; imported from the main program at line 2, column 1)* |
| **Identity.** A module is identified by its canonical path (symbolic links, `.` and `..` resolved), so the same file named in two ways is the same module. A file is loaded and parsed once however many modules import it: when `main` imports `stats` and `vectors`, and `stats` imports `math_utils`, which imports `vectors` (a "diamond"), `vectors` is one module that both importers refer to - that is not a duplicate. Importing the same file twice from one module is an error. Messages and `--modules` name a file by the path it was *reached* through (the importing file's directory joined with the import, `.` and `..` removed, symbolic links kept; relative to the working directory when the file is inside it), never by the resolved path - on macOS, for example, every temporary directory lies under the link `/var -> /private/var`, and a file you named as `/var/...` must not be reported as `/private/var/...`. | *Duplicate import of "lib/vectors.mydsl": it was already imported at line 1, column 1; remove one of the two imports*; *Duplicate import of "./lib/../lib/vectors.mydsl": the same file 'tests/modules/lib/vectors.mydsl' was already imported at line 1, column 1 as "lib/vectors.mydsl"; ...* |
| **Cycles.** Modules are loaded depth first; an import of a module that is still being loaded closes a cycle and is reported with the whole cycle at the import statement that closes it. A module importing itself is the shortest cycle. Cycles are rejected rather than tolerated because they leave no dependency order (and usually indicate that the shared functions belong in a third module); mutual recursion between two functions therefore has to live in one module. At most 100 levels of nested imports are accepted, so a runaway chain ends with a message instead of a stack overflow. | *Circular import: 'tests/modules/cycle/a.mydsl' -> 'tests/modules/cycle/b.mydsl' -> 'tests/modules/cycle/c.mydsl' -> 'tests/modules/cycle/a.mydsl' (a module cannot import itself, directly or through other modules) (imported from 'tests/modules/cycle/b.mydsl' at line 1, column 1; ...)*; *Circular import: the main program imports itself*; *Import nesting is too deep: the limit is 100 levels of imports* |
| **Library contents.** An imported module may contain only function definitions and imports. Any other top-level statement (a declaration, `Print`, a loop, ...) is a type error in that file: a library has no state to initialise and nothing of it runs on its own, so such a statement could only be a mistake. Only the main program's top-level statements execute. | *Type error in 'tests/modules/lib/config.mydsl' at line 3, column 1: An imported module may only contain function definitions and imports, but this is a declaration (the top-level statements of an imported module are never executed; move it into a function or into the main program) (imported from the main program at line 1, column 1)* |
| **Visibility.** A module sees its own functions and those of every module it imports, directly or through further imports: importing `stats` makes `variance` callable and also `mean` and `norm`, which `stats` needs. A library never sees the functions of a file that imports it (or of a sibling it does not import), so it keeps working wherever it is imported. Within what a file can see there is one namespace - no qualification, no renaming, no overloading - so a name defined in two visible files is an error naming both, reported at the later definition in dependency order. Functions and variables stay separate namespaces; bodies see only their parameters and locals, never the importing program's variables. | *Function 'square' is already defined (at line 3, column 1 of 'tests/modules/lib/math_utils.mydsl'); function names are shared by a program and every module it imports, directly or indirectly*; *Unknown function 'helper': 'helper' is defined in the main program, which 'tests/modules/lib/calls_main.mydsl' does not import (a function is visible where its file is imported, directly or through other imports)*; *Undeclared variable 'limit' (inside function 'clamp' only its parameters and local variables are visible ...)* |
| **Calls.** A call of an imported function is checked like any other: exact argument count and types, the declared return type; `declared at` names the defining file when it is not the current one. Unknown names list the builtins, the current file's functions and the visible imported ones. | *Function 'square' expects 1 argument (int x) but got 2 (declared at line 3, column 1 of 'tests/modules/lib/math_utils.mydsl')*; *Unknown function 'cube' (available: abs, ...; imported: mean, norm, scale, square)* |
| **Errors inside imported code.** All modules are checked - imported ones first, in dependency order, then the main program in source order - and every expression of every module is annotated. A type error inside an imported function names the file and the import chain. At run time a call into an imported function switches the current file: a runtime error there is reported with that file, the call trace names the file of every call site that is not in the main program, and the import chain follows; output printed before the error is kept, as always. | *Type error in 'tests/modules/lib/bad_body.mydsl' at line 2, column 14: Operator '\*' cannot be applied to int and float ... (imported from the main program at line 1, column 1)*; *Runtime error in 'tests/modules/lib/failing.mydsl' at line 4, column 14: Division by zero: 1 / 0 (in function 'ratio' called from line 7, column 19 of 'tests/modules/lib/failing.mydsl'; in function 'scaled_ratio' called from line 3, column 7) (imported from the main program at line 1, column 1)* |

Interfaces: `load_modules` (`include/mydsl.hpp`) returns the modules in dependency order -
every module after the modules it imports, the main program last - each with its canonical
path, the path shown in messages, its source text, its AST, its imports resolved to module
indices and the chain of imports it was first reached through; `typecheck(const Modules&)` and
`interpret(const Modules&, ...)` take that structure (the `Program` overloads wrap a single-file
program and refuse unresolved imports). `mydsl --modules FILE` prints the listing above;
`--check` lists the imported functions after the program's own, each with its module
(`square: function(int x) -> int (imported from 'lib/math_utils.mydsl')`), and the plain run,
`--check`, `--json` and `--run-json` all load the imports the same way, so they report the
same errors (`tests/modules.sh` checks that). `--ast` and `--tokens` stay single-file views of
the given file. In the JSON interface an error inside an imported file carries `"file"` and
the `"imports"` chain, and each trace frame whose call site lies in an imported file carries
`"file"` (`src/debug.cpp`); the browser page shows the message with the file and chain, lists
the trace with file names, and marks the innermost call site (or, for static errors, the
`import` statement) of the editor's program, since the offending line is in another file (see
"Browser view"). Rendered messages list at most five import sites (then `... N more
import(s)`) and five trace frames; `Error::imports` keeps every site. `tests/modules/` holds
worked examples: `basic`, `nested` (three levels), `diamond`, `transitive`, `recursion`,
`mixed` (own and imported functions), `input` (`Input()` inside an imported body), `app/main`
(`../lib/` and `./` paths) and one `err_*` program per diagnostic, each with its hand-written
`.out`, `.modules` or `.err` file.

## Standard library

`stdlib/` is a small library of reusable functions written in MyDSL and loaded through
`import`, not new C++ builtins - so everything it does is expressible in the language, and you
can read or adapt any function. `stdlib/README.md` lists every function with its rules.

```text
import "stdlib/math.mydsl";       // int: square, cube, min, max, clamp, sign, power, gcd, lcm, factorial,
                                  //      fibonacci, isqrt, div_floor, mod_floor, is_prime, binomial, digit_count, ...
import "stdlib/fmath.mydsl";      // float: sqrt_f, cbrt_f, hypot_f, exp_f, ln_f, log2_f, log10_f, pow_f, power_f,
                                  //      sin_f, cos_f, tan_f, floor_f, ceil_f, round_f, clamp_f, lerp_f, pi(), ...
import "stdlib/strings.mydsl";    // text: trim, reverse, starts_with, index_of, contains, count_of, replace_all,
                                  //      pad_left, pad_right, repeat, to_upper, to_lower, is_digit, parse_int, ...
import "stdlib/all.mydsl";        // all of the above
```

```sh
./mydsl stdlib/examples/number_facts.mydsl    # Int(144) Int(1099511627776) ... gcd, factorial, is_prime, ...
./mydsl stdlib/examples/geometry.mydsl        # Float(12.5663706143592) Float(5.0) Float(1.41421356237309) ...
./mydsl stdlib/examples/text.mydsl            # String("Hello, MyDSL world!") String("HELLO, MYDSL WORLD!") ...
printf '7 2.5' | ./mydsl stdlib/examples/report.mydsl
./mydsl stdlib/examples/validation.mydsl      # Int(42) Int(100), then: Runtime error at line 11, column 9: percent: the value must be between 0 and 100, but it is 150 (...)
./mydsl --input stdlib/examples/layered/main.in stdlib/examples/layered/main.mydsl   # a library of your own on top of stdlib
printf 'import "stdlib/fmath.mydsl"; Print(sin_f(pi() / 6.0));' | ./mydsl -     # Float(0.5); the same text works in the browser page
```

Design points, all consequences of the language rules:

- **Names.** There is no overloading and one function namespace along the import graph, so the
  float versions carry an `_f` suffix (`min`/`min_f`, `square`/`square_f`) and no library name
  repeats a builtin (`abs`, `length`, `substring`, `to_string`, ...) or a keyword
  (`tests/stdlib.sh` checks this). A program that defines a function with a library name gets
  the usual duplicate-definition error naming `stdlib/...`.
- **Invalid arguments.** A function given an argument outside its domain stops the program
  with `error(...)` and a message that names the function, the rule and the value -
  `factorial: the argument must not be negative, but n is -1`, `sqrt_f: the argument must not
  be negative, but x is -4.0`, `clamp: low must not exceed high, but low is 5 and high is 2`,
  `parse_int: "12a" is not the text of an integer (expected an optional '-' followed by digits
  only)` - reported with the library file, the call trace back to your call and the chain of
  imports. Results that do not fit (`factorial(21)`, `exp_f(710.0)`) are rejected the same
  way; overflow in the middle of a computation (`power(3, 40)`) is reported by the language.
  The messages keep their meaning across modules: a library called through a module of your
  own reports the library file, every call site with its file, and the import chain.
  `stdlib/examples/validation.mydsl` shows `error` in a function of your own, and
  `stdlib/examples/layered/` a library of your own on top of the standard library that
  validates before calling (`is_valid_celsius`) and rejects with `error` otherwise.
- **Numerics in MyDSL.** `sqrt_f` (Newton's method, within 1 ulp), `exp_f`, `ln_f`, `sin_f`,
  `cos_f` (series after range reduction, about 1e-14 relative / 1e-15 absolute up to |x| = 1e6)
  and `pow_f` are computed in the language, since it has no such builtins; `tests/tests.cpp`
  compares them with `<cmath>` over hundreds of arguments at full precision.
- **Libraries only define functions.** The modules contain function definitions and imports,
  as the module rules require; constants are functions (`pi()`, `euler()`), the three modules
  are independent, and `all.mydsl` simply imports them.

## Execution rules

The interpreter (`src/interpreter.cpp`) only runs programs that passed the type checker, and
stops at the first runtime error with `Runtime error at line L, column C: ...`. Output produced
before the error is kept (the CLI has already printed it; the browser shows it above the error).

- **error(message).** Stops the program at once with a runtime error whose text is the
  message: `Runtime error at line 3, column 9: percent out of range: 101 (in function
  'checked_percent' called from line 12, column 7)`. It is reported exactly like the
  language's own runtime errors - with the position of the `error` statement, the call trace,
  and, inside an imported module, the file and the chain of imports - so the standard library
  uses it to reject arguments outside a function's domain with an explanation (see "Standard
  library"). Output printed before it is kept; the message is evaluated first, so an error
  while building it is reported instead.
- **Values.** `int` is a 64-bit integer, `float` a double, `bool` a boolean, `string` a byte
  string. `Print` writes one value per line in the `Value::str()` format: `Int(120)`,
  `Float(2.5)`, `Bool(true)`, `String("a\nb")` (see "Strings").
- **Integer arithmetic is checked.** `+ - * /`, unary `-`, `abs` and the for-loop step raise
  `Integer overflow: 9223372036854775807 + 1 does not fit in a 64-bit int` instead of wrapping.
  `/` truncates towards zero and `%` takes the sign of the dividend (`-7 / 2 = -3`,
  `-7 % 3 = -1`), as in C++. Division or modulo by zero is a runtime error.
- **Float arithmetic** follows IEEE 754 except that division by zero (`-23.0 / 0.0`, see
  `error_input1.txt`) and any non-finite result (`1e308 * 10.0`) are runtime errors, so `inf` and
  `nan` never appear. `to_int` truncates towards zero and fails if the value does not fit.
- **Short-circuit evaluation.** `a && b` does not evaluate `b` when `a` is false; `a || b` does
  not evaluate `b` when `a` is true (so `z == 0 || 10 / z > 1` is safe). Every other operator,
  including `+`/`*` on bools, evaluates both operands.
- **Scoping follows the type checker.** Each execution of a block (including if/while/for
  bodies) creates a fresh scope: variables declared inside are new every iteration and vanish at
  the closing `}`; assignments update the nearest enclosing declaration. Shadowed outer variables
  are untouched.
- **For loops.** `for (i := a, c, s) { body }` runs `i := a; while (c) { body; i := i + s; }` in
  a scope of its own: `i` is an ordinary `int` variable of that scope (an outer `i` is shadowed
  and unchanged afterwards). Because the step is added to the *current* value, assigning to `i`
  in the body changes the iteration - `Check_Sorted.txt` and `for_loop.txt` rely on `i := 5` /
  `i := 11` to break out of a loop. `c` and `s` are re-evaluated every iteration.
- **Vectors and matrices** are copied on assignment (`b := a; b[0] := 9;` leaves `a` alone).
  `v[i]`, `m[i, j]` and element assignments are bounds-checked at run time
  (`Index 3 is out of range for intvector 3 (valid indices are 0..2)`). Element-wise `+ - * /`
  and `-` apply the scalar rules per element and name the element in errors
  (`Integer overflow: ... (element [0,1])`). `==` compares all elements exactly.
- **Vector builtins.** `abs(v)` is the Euclidean length, `dotprod_vec` the dot product (exact
  with overflow checks for ints), `inv_vec(v)` returns the elements in reverse order (unary `-`
  already negates), `is_zero` tests all elements. `angle_vec(a, b)` is the angle in radians
  computed with Kahan's formula `2·atan2(|û−v̂|, |û+v̂|)` on the unit vectors, which is exact for
  parallel and antiparallel vectors (`acos(dot/(|a||b|))` would give 2e-8 instead of 0); a zero
  vector has no direction and is a runtime error.
- **Matrix builtins.** `transpose` and `mult_mat` follow the checked dimensions (int products
  are overflow-checked). `determinant` of an `intmatrix` is exact (Bareiss fraction-free
  elimination; `-84` where floating point gives `-83.99999999999999`), of a `floatmatrix` it uses
  LU decomposition with partial pivoting. `inverse_mat` uses Gauss-Jordan elimination with
  partial pivoting and raises `the matrix is singular` when a pivot is exactly 0 and
  `numerically singular` when a pivot is smaller than `n · ε · max|a_ij|` (e.g. the classic
  `[[1,2,3],[4,5,6],[7,8,9]]`, whose float determinant comes out as `6.7e-16`, not 0). Any
  non-finite intermediate or result is a `Numerical error`.
- **Runtime input.** `Input()` reads the next value from standard input and `Input("file")`
  from the file (relative to the working directory; each file is opened once per run and read
  sequentially, so one file can hold several values). Values use the literal syntax of the
  language, separated by whitespace, with comments allowed: `42`, `-2.5`, `true`, `"text"`,
  `[1., 2., 3.]`, `[[1, 2], [3, 4]]`. The value must have exactly the target's type and size -
  an `int` may not be written as `4.5`, a `floatvector 3` needs exactly three elements - except
  that a float may be written without a fraction (`4`). A missing file, a wrong token, a
  wrong size or running out of data is a runtime error naming the data position:
  `Invalid data in standard input at line 1, column 6: expected ',' (the vector needs 3
  elements) but found ']'`, `Unexpected end of file 'ip.txt': expected a number`. Standard
  input is read completely when the first `Input()` runs (pipe or redirect it, or use
  `--input FILE`).
- **String input** follows the same model (`inputs/string_input.txt`, `tests/data/names.txt`):
  a string value is a double-quoted literal with the source escapes, so it may contain spaces
  (`"Ada Lovelace"`) and line breaks as `\n`, and `""` is the empty string. Quoting is what
  makes the rules simple and unambiguous: a string may share a line with other values (`"hi" 3
  [1, 2]`), blank lines, indentation and comments between values are skipped, and every
  `Input()` takes the next value whatever its type. Bare text is rejected with a hint
  (`expected a string in double quotes (e.g. "text"), not bare text but found 'Ada'`), a
  string where a number is expected says `expected an int but found the string "42"`, a
  malformed literal in the data reports the lexing error at its data position, and running
  out of values is `Unexpected end of standard input: expected a string in double quotes`.
  Reading a whole raw line is not supported.
- **Limits.** `interpret` accepts optional limits on wall-clock time and output size; the browser
  runs with 2 s / 256 KB so an infinite loop ends with `Execution stopped after 2000 ms`
  reported at the innermost loop. The CLI runs without limits.
- **User-defined functions** are called by value in an isolated scope, return from anywhere in
  the body, may recurse up to 1000 active calls, and report errors with a call trace (see
  "Functions").

## Floating-point results and how to verify them

`float` is an IEEE 754 double: about 16 significant decimal digits, and most decimal fractions
are not representable exactly. Consequences you will see:

- `Print(0.1 + 0.2)` shows `Float(0.3)` because `Print` rounds to 15 significant digits, but
  `0.1 + 0.2 == 0.3` is `Bool(false)`. Compare floats with a tolerance
  (`abs(x - y) < 1e-12`) rather than `==`, unless the values are exact (small integers, halves,
  quarters, ... and results computed from them).
- Results of elimination algorithms (`determinant`, `inverse_mat`) carry rounding errors of the
  order of `n · ε · |entries|` (ε ≈ 2.2e-16): the float determinant of a singular matrix may be
  `6.66e-16`, and `mult_mat(a, inverse_mat(a))` may differ from the identity in the last digits.
  Int matrices avoid this: their determinant is exact, and integer overflow is reported rather
  than rounded.
- Ill-conditioned matrices (nearly dependent rows) lose digits in proportion to their condition
  number; `inverse_mat` refuses only the numerically singular ones and otherwise returns the
  best double-precision answer, which may be far from the exact rational inverse.

To verify a result independently, compute it with exact arithmetic or NumPy and compare at the
printed precision, e.g. in Python: `from fractions import Fraction` for exact rational
determinants/inverses, or `numpy.linalg.det`, `numpy.linalg.inv`, `a @ b`, `numpy.degrees(...)`
for angles. `tests/tests.cpp` (`vector_matrix_tests`) does exactly this: every expected value was
computed with NumPy or `Fraction` and is compared with the interpreter's 15-digit output.

## Browser view

`mydsl --serve [PORT]` (or `make serve`) starts a small HTTP server on `127.0.0.1` and serves a
single page, `web/index.html`, with the source editor in the centre, the program input, output
and variables below it and a sidebar with the token list and the AST. The editor is CodeMirror 6
with a MyDSL mode (see "The editor" below), bundled into `web/vendor/mydsl-editor.js` and served
by the binary itself, so the page works offline. There is no JavaScript
implementation of the language: on every edit the
page POSTs the source to `/api/parse`, and the C++ `lex()`/`parse()`/`typecheck()` run inside the
`mydsl` process and answer with JSON (`src/debug.cpp`; identical to `mydsl --json`). Pressing
*Run* POSTs to `/api/run`, which executes the program with the C++ interpreter (identical to
`mydsl --run-json`). Nothing is ever executed while typing. The server (`src/server.cpp`) has no
dependencies beyond POSIX sockets, handles one request at a time and only listens on localhost.

```sh
make -j2
./mydsl --serve            # "MyDSL debug page: http://127.0.0.1:8000/  (press Ctrl+C to stop)"
./mydsl --serve 0          # let the OS pick a free port (printed on stderr)
```

Open the printed address in a browser. The page is laid out like a code editor: a title bar
with the actions (*Parse*, *▶ Run*, *Save*, *Load example…*, the *View* toggles), the *Files*
panel on the left, tabs over the editor, the Input / Output / Variables panel below it, the
Tokens and AST panels on the right, and a status bar at the bottom (parse verdict, file
status, cursor position, theme). What you should see:

- **Files** (left): the project - the `.mydsl` and `.txt` files below the directory the server
  was started in (its absolute path is shown in the panel header). Click a file to open it in a
  tab; *Folder* narrows the tree to a sub-folder (switching closes the open files, after a
  warning when one has unsaved changes); ↻ re-reads the disk. See "Files, tabs and saving".
- **Tabs** (above the editor): *Scratch* is the browser-only buffer of before (kept in local
  storage, never written to disk; *Load example…* loads into it); every opened file has a tab
  with its own undo history and cursor. ● marks unsaved changes (also in the tree, the header
  and the window title), 🔒 a read-only file, × closes a tab (asking first when it is unsaved).
  *Save* / `Ctrl+S` writes the active file; the header says `saved HH:MM:SS` or why it failed.
- **Status bar** (bottom): the parser's verdict on the active tab (`ok · 42 tokens · 5
  statements · 12 ms` or `parse error`, updated as you type), the file status (`saved 12:03:44`,
  `read-only`, or why a save failed), the cursor position, the indentation, and the **Theme**
  selector.
- **Themes**: *Auto (system)* follows the operating system's light/dark setting (MyDSL Light /
  Dark Modern); or pick one of MyDSL Light, Light+ (VS Code), GitHub Light, Solarized Light,
  Dark+ (VS Code), Dark Modern (VS Code), Monokai, Solarized Dark, Dracula, One Dark Pro,
  GitHub Dark, Nord, Dark High Contrast. A theme is one palette for everything - panels, tabs,
  tables, the token badges and AST colours, the editor and its syntax colours, marks and
  completion list - defined in `web/editor/src/themes.js` (a compact form per theme; the page
  applies it as CSS variables, the editor as a CodeMirror theme). The choice is remembered.
- **Layout**: the editor takes most of the window. The *View* buttons in the header show or
  hide the *Files* panel, the *Output* panel (input, output, variables), the *Tokens* list and the *AST* (each
  panel also has a *hide* button; Tokens is hidden by default, and *▶ Run* always shows the
  Output panel). The bars between the panels can be dragged to resize them - editor/output,
  editor/sidebar, tokens/AST, output/variables - and double-clicking a bar restores its default
  size. Which panels are shown and their sizes are kept in the browser's local storage.
- **Source** (centre): the code editor, pre-filled with the factorial example. Typing re-parses
  after a short pause; `Ctrl+Enter` or the *Parse* button parses immediately. *Load example…*
  offers the programs in `inputs/` (read from the working directory); loading one can be undone.
- **Input** (below the editor): text that serves as standard input for `Input()` - values in
  literal syntax separated by whitespace, e.g. `[1., 2., 3., 4., 5.]  4  "a string"`. `Input("file")` reads
  files relative to the directory the server was started in. The box is kept in local storage.
- **Output** (below the Input box): *▶ Run* (or `Ctrl+Shift+Enter`) executes the current source and
  shows the `Print` output, then `finished in N ms` or the runtime/type/parse error in red (click
  it to jump to the location, which is also outlined in red in the source). A runtime error
  inside a function is followed by its call trace, one orange line per active call, innermost
  first: `↳ in function 'inner' called from line 6, column 10`, then the call of `inner` in
  `outer`, and so on (direct recursion is collapsed to `[999 times]`, more than five levels end
  with `… N more call(s)`). Every call site is outlined in orange in the source, and clicking a
  line jumps to it, so the path from the top-level statement into the failing body can be
  followed step by step. Editing the source afterwards dims the output and notes that it is from
  an earlier version. Runs are cut off after 2 seconds or 256 KB of output, so an infinite loop
  cannot freeze the page or the server; the server handles one request at a time, so parsing
  pauses for at most that long.
- **Variables** (right of the Output): the variable inspector - one row per top-level variable with
  its name, declared type and final value, exactly as the C++ interpreter holds them when the
  run ends (`IntVector[10, 20, 30]`, `String("n=3")`, matrices on several lines). The page only
  displays what `/api/run` returns; nothing is computed in JavaScript. Variables whose value
  differs from a literal initializer are marked ✎, and clicking a row highlights the
  declaration in the source. After a runtime error the table shows the values *at the moment of
  the error* (a declaration whose initializer failed is absent) and says so; block locals, loop
  variables, parameters and function locals are never listed, only top-level variables. The
  table is dimmed together with the output once the source changes.
- **Tokens** (sidebar, top): one row per token with its kind (`word`, `integer`, `float`, `string`,
  `symbol`, `end`), text and `line:column` begin/end. Click a row to highlight the token in the
  source.
- **AST** (sidebar, bottom): an expandable tree, one `▸` node per statement/expression with its `op`, name
  or value, literal shapes, its source span and - when the program type-checks - the inferred
  type of every expression in blue (`: floatmatrix 2,2`). A function definition shows its
  return type, name and parameter list, with one `parameter` child per parameter and the body. Click a node to highlight its whole
  span in the source (`▸`, or double-click, toggles children; *expand all* / *collapse all* at the
  top). Moving the cursor in the source marks the token and the innermost node under it in yellow
  and expands the tree to reveal it.
- **Errors**: a red banner across the top with the stage (`Lexing`, `Parse`, `Import` or
  `Type`), line, column and message; the offending position is outlined in red in the source
  and the token is marked in the list. Click the banner to jump to the position. After a syntax
  error the AST pane says so instead of showing a stale tree; after a type or import error the
  tree is still shown and the node the error was reported on is outlined in red. An error
  located in an imported file (`import "..."` in the editor is resolved relative to the
  server's working directory) names that file and the chain of imports in the banner, and the
  marker sits on the `import` statement of the editor's program that led there; a runtime
  error inside an imported function lists its trace with the file of each call site and marks
  the innermost call site in the editor. If the server is stopped the banner says that instead.

The page keeps the current source, the Input box and the layout in the browser's local storage,
so reloading does not lose them.

### Files, tabs and saving

The files come from the `mydsl` server, not from the browser: `GET /api/files` lists the
project, `GET /api/file?path=…` reads a file and `PUT /api/file?path=…` writes one
(`src/project.cpp`). **The project is the directory the server was started in** - start it
where your programs are (`cd ~/mydsl-programs && /path/to/mydsl --serve`) - and nothing
outside it is reachable:

- Paths are relative to that directory and must be plain: no absolute paths, no `..` or `.`
  components, no empty components, backslashes or control characters (HTTP 400). After
  resolving symbolic links the file must still lie inside the directory (403), so a link to
  `/etc` or to your home directory is refused.
- Only `.mydsl` and `.txt` files are listed, opened or saved (403 otherwise); hidden entries
  (`.git`, `.env`, …), `node_modules` and `build` are neither listed nor reachable by path;
  links to directories are not followed when listing; files are at most 1 MiB (413).
- Saving writes a temporary file next to the original and renames it over it, keeping the
  file's mode, so a crash half-way never leaves a truncated file; symbolic links are never
  overwritten; a file that is not writable (or a directory that is not) is refused with
  *Permission denied*, and the tree and the tab show such files as read-only (🔒, the editor
  will not accept edits, *Save* is disabled with the reason). Nothing is deleted or renamed;
  a new file can only be created by a `PUT` into an existing directory (the page has no "new
  file" yet - create it with your editor or shell and press ↻).
- The server binds to 127.0.0.1 only, every request must carry a `Host` header naming the
  loopback address (DNS rebinding from another site is refused with 403), saving requires the
  header `X-Requested-With: mydsl`, and no route sends CORS headers, so a web page from another
  origin can neither read your files nor save through your browser.

Why not the browser's own folder picker? The File System Access API (`showDirectoryPicker`)
exists only in Chrome and Edge - Firefox and Safari would get a read-only folder upload and
"save" as a download - and, more importantly, files opened that way live in the browser, so
the C++ side could not resolve `import "lib/util.mydsl"` between them. With the server's
directory as the project, every browser works the same, the imports of a file resolve
relative to it (`/api/parse?path=…` and `/api/run?path=…` name the active file, exactly like
running it from the command line), and the access rules above are enforced by the code you
build yourself. The price is that the project is fixed when the server starts; to work on a
folder elsewhere, start another server there (any port: `mydsl --serve 0`).

Unsaved work: a tab is dirty when its text differs from what was last read from or written to
disk. Switching tabs keeps every tab's text, cursor and undo history. Closing a dirty tab or
switching the folder asks for confirmation; leaving or reloading the page with unsaved changes
triggers the browser's "leave site?" dialog. The session - open tabs, the unsaved texts, the
active tab, the folder - is kept in local storage, so after a reload (or a browser crash) the
tabs and their unsaved edits come back; a file that changed on disk behind an unsaved tab is
flagged in red (*changed on disk since your unsaved edits; saving overwrites it*).

**Parse and Run always take the active tab as it is in the editor** - never a saved copy and
never another tab - as the file it belongs to (`?path=`), so its imports resolve relative to
it. The unsaved texts of the other open files travel with every request (the `#files` prefix
of the body, `FileOverlay` in `load_modules`): an imported library that you edited but have
not saved is used in its edited form, and the run status says *with the unsaved text of
lib/util.mydsl*. A result that arrives after you switched tabs is discarded (parse) or shown
dimmed as belonging to the other tab (run; the Output header names the file that was run).

**Debugging across files.** An error located in an imported project file - a syntax or type
error in a library, a runtime error inside one of its functions - shows the file and the
import chain in the banner / the output as before, and **clicking it opens that file in a tab
at the line and column** (the banner's tooltip says so; files outside the project, shown with
absolute paths, cannot be opened and the click falls back to the import or call site in the
program). The call trace of a runtime error works the same: every frame whose file is in the
project is clickable and opens the file at the call site; frames in the program return to the
program's tab. Marks follow the files: the tab that was run shows the innermost call site in
the program (red) and the program's own frames (orange); the tab of an imported file shows the
failing position and the frames that lie in that file; a tab edited after the run shows no
marks of it any more (its positions may have moved). The Tokens and AST panels always show
the active tab's own parse - a library opened in a tab is parsed and type-checked on its own -
and the Variables panel shows the last run's top-level variables.

### A complete session

1. Start the server where your programs are: `cd ~/mydsl-programs && /path/to/mydsl --serve`
   (or `make serve` in this repository to browse `inputs/`, `stdlib/` and `tests/`). Open
   http://127.0.0.1:8000/.
2. In *Files*, pick a *Folder* if the tree is large, click `main.mydsl` - it opens in a tab, the
   status bar says `ok · …` or names the first error, the AST shows the typed tree.
3. Type. Names, keywords and the functions of imported modules complete as you type (`Enter`
   or `Tab` accepts, templates for `if`/`while`/`for`/`function`); brackets close and match;
   Enter indents. An incomplete statement is reported in the banner while you type and the
   completion keeps working from the last good parse.
4. Open the library the program imports in a second tab, edit it - the ● marks the unsaved
   tab; Parse and Run of the program already use the edited text ("with the unsaved text of
   …" in the run status). `Ctrl+S` writes it; a failed write says why.
5. `▶ Run` (or `Ctrl+Shift+Enter`): output, runtime errors with their trace, the variables.
   Click an error or a trace frame to land on the line, also when it lies in another file.
6. Switch themes in the status bar, drag the bars between panels, hide what you do not need
   (*View*). Everything - layout, theme, open tabs, unsaved texts - is back after a reload.

### The editor

The source pane is [CodeMirror 6](https://codemirror.net) with a MyDSL language mode written
for this project (`web/editor/src/mydsl-language.js`, a stream tokenizer that mirrors
`src/lexer.cpp`: `//` and `/* */` comments, words, integers and floats including `.5` and
`2e3`, strings with the `\" \\ \n \t \r` escapes, the operators and brackets; the keyword and
builtin lists of `src/parser.cpp` / `src/type_checker.cpp`). It provides:

- **Syntax highlighting**: keywords (`if`, `while`, `function`, `Print`, …) purple, type names
  (`int`, `floatvector`, …) blue, builtins (`abs`, `length`, `Input`, …) and calls teal, numbers
  and `true`/`false` amber, strings green, comments grey italic; text the C++ lexer would reject
  (an unterminated string, an unknown escape, a stray character) is underlined in red.
- **Line numbers**, the current line highlighted, matching brackets highlighted when the cursor
  touches one (an unmatched bracket in red), and `( [ { "` auto-closed as you type (typing the
  closing one steps over the inserted one).
- **Automatic indentation**: Enter indents the new line one level (4 spaces) per open bracket
  and typing a closing `) ] }` outdents it; `Tab` / `Shift+Tab` indent and outdent the selected
  lines (press `Escape` and then `Tab` to move the focus out of the editor).
- **Completion**: typing a name opens a list of suggestions (`Ctrl+Space` opens it anywhere);
  `↑`/`↓` choose, `Enter` or `Tab` accept, `Escape` closes. The list holds the keywords, type
  names and `true`/`false`, the builtin functions, every function the program can call - its
  own and those of the modules it imports, directly or indirectly - and the variables, loop
  variables and parameters declared in the program. Each entry shows its parameters or type
  next to the name (`scale (int value, int factor) → int`, `total int`, `substring (s, start,
  count)`) and, beside the list, a description: the statement form or type rule and a sentence
  about it, where a function is defined or which module it comes from. Accepting a function
  inserts the call with one placeholder per parameter (`Tab` moves to the next one), a
  statement keyword inserts its template (`while (condition) {` … `};`, likewise `if`, `for`,
  `function`, `import`, `Print`, `error`). Nothing is suggested inside strings and comments,
  the word being typed is not offered for itself, and code that does not parse yet still
  completes: the names come from the words the C++ lexer produced plus the declarations of the
  last parse that succeeded. All of this is language information from the C++ side, not a
  second parser in JavaScript: the keyword and builtin lists with their descriptions are
  `mydsl --language-json` (`GET /api/language`; the lists are the parser's `keywords()` and the
  checker's `builtin_functions()`, tested to stay in step), the callable functions are the
  `functions` array of `/api/parse` (`--json`), the declarations are read off the AST and the
  words off the token list of the same response.
- **Keyboard**: `Ctrl+Z` / `Ctrl+Shift+Z` (or `Ctrl+Y`) undo and redo, `Ctrl+F` find and replace,
  `Ctrl+D` select the next occurrence, `Ctrl+/` toggle a `//` comment, `Alt+↑`/`Alt+↓` move the
  line, `Shift+Alt+↓` copy it, `Ctrl+Space` completion, `Ctrl+S` save, `Ctrl+Enter` parse,
  `Ctrl+Shift+Enter` run (the `⌨ shortcuts` label in the pane header lists them). **On macOS**
  the editor follows the platform: `Cmd+Z`, `Cmd+Shift+Z` (there is no `Cmd+Y`), `Cmd+F`,
  `Cmd+/`, `Cmd+D`; the page's own shortcuts - save, parse, run - accept both `Cmd` and `Ctrl`,
  and work with the focus in the Input box as well.

The editor only edits, highlights and draws marks; the page talks to it through the small
interface of `web/editor/src/main.js` (`getValue`, `setValue`, `getCursor`, `setCursor`,
`setMarks`, `scrollTo`, …), and every token, node, type and error still comes from the C++
side. `web/vendor/mydsl-editor.js` is the bundle (CodeMirror packages at pinned versions plus
the two source files, one ASCII-only minified file); the makefile embeds it into the binary
next to `index.html` and the server answers `GET /vendor/mydsl-editor.js` with it, so nothing
is fetched from the internet. The bundle is committed: building `mydsl` never needs Node. To
change the mode or the editor set-up, edit `web/editor/src/` and run `make editor` (or
`cd web/editor && npm install && npm run build`; needs Node and npm, versions are pinned in
`web/editor/package.json` / `package-lock.json`), then rebuild the binary.

`tests/editor.test.js` drives the editor in a headless browser (needs Node and Playwright:
`npm install -g playwright && npx playwright install chromium`; `tests/cli.sh` runs it while its
server is up and skips it otherwise): highlighting classes per token kind, line numbers,
indentation on Enter and on closing brackets, bracket auto-closing and matching, undo/redo and
the other shortcuts, the completion (which kinds of symbols appear and with which details,
arrow keys, Enter/Tab/Escape, the info box, the templates and placeholders, incomplete code,
no suggestions in strings and comments, and that typing whole statements is unaffected), and
the page's behaviour through the editor - error banner and marks, click-to-jump,
token/node/variable clicks marking the text, cursor tracking, Run, stale output, persistence
across a reload. `tests/web.test.js` replaces the editor by a stub with the same interface, so
the page logic stays testable in plain Node; it checks the symbol table the page builds for
the completion (functions with signatures and modules, variables with types, parameters, loop
variables, keywords and builtins from `--language-json`, the fallback while the text does not
parse).
Routes: `GET /` (page), `POST /api/parse` (body = program text, response = JSON), `POST /api/run`
(body = `<source byte count>\n` + source + standard-input text, response =
`{"output", "error", "variables", "millis"}` where `error` carries `trace` / `omitted` for
runtime errors inside functions, `file` / `imports` for errors inside imported files, and
`variables` lists `{"name", "type", "value"}` of the top-level variables), `GET /api/examples`,
`GET /api/language` (the completion vocabulary, identical to `--language-json`),
`GET /api/files`, `GET /api/file?path=`, `PUT /api/file?path=` (see "Files, tabs and
saving"); `?path=FILE` on `/api/parse` and `/api/run` names the project file being edited so
that its imports resolve relative to it, and their bodies may start with
`#files N\n` + N × `<path bytes> <content bytes>\n<path><content>` - unsaved texts of other
project files that imports then use instead of the disk versions. A parse
response also carries `functions`: every function the program can call with its parameters,
return type, position and - for imported ones - file, in dependency order. The JSON layout is
documented at the top of `src/debug.cpp`; every node carries `location` (the point used for
error messages), `begin` and `end` (the span it covers).

### Testing the page

```sh
make test                                   # everything below that the machine can run
make sanitize                               # the same under AddressSanitizer/UBSan
node tests/web.test.js ./mydsl              # the page's script in Node (no browser needed)
npm install -g playwright && npx playwright install chromium   # once, for the browser checks
node tests/editor.test.js http://127.0.0.1:8000 ./mydsl        # with `./mydsl --serve` running:
                                            # editor, completion, themes, files (own temp project)
```

`make test` runs `tests/cli.sh`, which starts the server on a free port, fetches the page,
posts valid and invalid programs, checks the JSON responses and the HTTP status codes, the
file API and its refusals, then runs the page's own script in Node against real `--json` output
(`tests/web.test.js`: clicking tokens/nodes highlights the expected text, the cursor finds the
innermost node, errors are shown and cleared, Run output and runtime errors are rendered and
marked stale after edits, the completion symbols, the tab model, the themes) and - when Node
can `require('playwright')`, globally installed is fine - `tests/editor.test.js` in headless
Chromium (the editor, completion, themes, the file panel in a throwaway project, debugging
across files). The browser script presses the platform's keys: `Cmd` on macOS, `Ctrl`
elsewhere. `curl` works too:

```sh
curl -s -X POST --data-binary 'Print(1 + ;' http://127.0.0.1:8000/api/parse
# {"tokens":[...],"ast":null,"error":{"stage":"Parse","line":1,"column":11,"message":"Expected an expression but found ';'"}}
printf '27\nPrint(6 * 7); Print(1 / 0);' | curl -s -X POST --data-binary @- http://127.0.0.1:8000/api/run
# {"output":"Int(42)\n","error":{"stage":"Runtime","line":1,"column":23,"message":"Division by zero: 1 / 0"},"variables":[],"millis":0}
printf '33\nint a := Input(); Print(a * a);  7' | curl -s -X POST --data-binary @- http://127.0.0.1:8000/api/run
# {"output":"Int(49)\n","error":null,"variables":[{"name":"a","type":"int","value":"Int(7)"}],"millis":0}
#                                                   (the first line is the source byte count; the rest is input)
```

## Inspecting the AST

`--ast` prints one S-expression per top-level statement; nested statements are indented.

```text
$ ./mydsl --ast inputs/factorial.txt
(declare int a Int(1))
(declare int i Int(1))
(declare int num Int(5))
(while (binary <= (variable i) (variable num))
  (block
    (assign a (binary * (variable a) (variable i)))
    (assign i (binary + (variable i) Int(1)))))
(print (variable a))
```

Expression nodes: `Int(1)`, `Float(2.0)`, `Bool(true)`, `(string "ip.txt")`, `(variable x)`,
`(unary - e)`, `(binary + a b)`, `(call name args...)`, `(index target i [j])`,
`(vector[N] elements...)` and `(matrix[R,C] elements-in-row-major-order...)`.
Statement nodes: `(declare type name init)`, `(assign name value)`,
`(assign_index name i [j] value)`, `(print e)`, `(error e)`, `(block ...)`, `(if cond then-block [else-block])`,
`(while cond block)`, `(for name init cond step block)`,
`(function return-type name (parameter type name)... block)`, `(return e)` and
`(import "file")`. The exact field layout of each node is documented in `include/mydsl.hpp`.

A syntax error aborts parsing, prints nothing on stdout and exits with status 1:

```text
$ ./mydsl --ast tests/syntax_errors/unclosed_block.txt
Parse error at line 6, column 1: Expected '}' to close the block opened at line 3, column 15 but found end of input
```

## Grammar

Whitespace is insignificant; `//` and `/* */` comments are skipped. Every statement, including
those that end in `}`, is terminated by `;`.

```text
program     := (statement | function | import)*
import      := 'import' STRING ';'                                  top level only (see "Modules")
function    := 'function' type name '(' [parameter (',' parameter)*] ')' block ';'
parameter   := type name
statement   := type name ':=' expression ';'                       declaration (initializer required)
             | name ':=' expression ';'                            assignment
             | name '[' expression [',' expression] ']' ':=' expression ';'   element assignment
             | 'Print' '(' expression ')' ';'
             | 'error' '(' expression ')' ';'                         stops the program with the string message
             | '{' statement* '}' ';'
             | 'if' '(' expression ')' 'then' block ['else' block] ';'
             | 'while' '(' expression ')' block ';'
             | 'for' '(' name ':=' expression ',' expression ',' expression ')' block ';'
             | 'return' expression ';'                              only inside a function body
block       := '{' statement* '}'
type        := 'int' | 'float' | 'bool' | 'string'
             | ('intvector' | 'floatvector') INTEGER
             | ('intmatrix' | 'floatmatrix') INTEGER ',' INTEGER

expression  := or
or          := and ('||' and)*
and         := equality ('&&' equality)*
equality    := comparison (('==' | '!=') comparison)*
comparison  := additive (('<' | '>' | '<=' | '>=') additive)*
additive    := term (('+' | '-') term)*
term        := unary (('*' | '/' | '%') unary)*
unary       := ('-' | '!') unary | postfix
postfix     := primary ('[' expression [',' expression] ']')*
primary     := INTEGER | FLOAT | STRING | 'true' | 'false'
             | name | name '(' [expression (',' expression)*] ')'
             | '(' expression ')'
             | vector | matrix
             | '(' INTEGER vector ')'                              sized vector, e.g. (3 [1,2,3])
             | '(' INTEGER ',' INTEGER matrix ')'                  sized matrix, e.g. (2,2 [[1,2],[3,4]])
vector      := '[' expression (',' expression)* ']'
matrix      := '[' vector (',' vector)* ']'                        all rows must have the same length
```

Binary operators are listed from lowest to highest precedence and are all left-associative;
unary `-`/`!` bind tighter than any binary operator, and indexing binds tighter still
(`-a[0]` is `-(a[0])`). Unary `+` is not allowed. The keywords `int float bool intvector
floatvector intmatrix floatmatrix if then else while for Print true false function return
string import error` cannot be used as names; `Input` is an ordinary function name. `STRING` is a
double-quoted literal on one line with the escapes `\" \\ \n \t \r`. Literal shapes are checked while parsing: literals
must be non-empty, matrix rows must be rectangular, and the explicit size of a sized literal must
match its contents. Integer literals must fit in 64 bits. Nesting (brackets, blocks and chained
operators) is limited to 256 levels so that later recursive stages cannot overflow the stack.
The parser also rejects a `function` or `import` anywhere but at the top level and a `return`
outside a function body. Everything else (undeclared names, operand types, dimension agreement,
unknown functions, argument counts, signatures and return paths) is left to the type checker;
the files named by imports are located and parsed by the module loader (see "Modules").

## TODO

- [x] Complete the parser: expressions, precedence, declarations, assignments, blocks, control
  flow, calls and indexing. Document the grammar and add source-positioned errors.
- [x] Add a local browser debugging view for source, tokens and the parsed AST, with expandable
  nodes and source locations. Use the C++ parser output directly.
- [x] Implement static type checking: scalar types, scope rules, assignment compatibility,
  vector/matrix dimensions, builtin signatures and indexing.
- [x] Implement scalar execution and control flow: arithmetic, booleans, variables, Print,
  blocks and loops. Handle overflow, division by zero and short-circuit evaluation.
- [x] Implement vector/matrix operations and runtime input, including bounds checks,
  determinant, inverse, singular matrices, input validation and file errors.
- [x] Verify the complete pipeline with independent regression tests, malformed programs,
  boundary cases and end-to-end examples. Test browser integration and run sanitizers.
- [x] Update CLI help and language documentation as features become available.
- [x] User-defined functions, part 1: syntax (`function`/`return`), AST nodes, type checking
  of signatures, calls, returns and scoping, browser view, documentation and tests.
- [x] User-defined functions, part 2: execution - by-value arguments in an isolated scope,
  early returns from nested blocks and loops, recursion with a call depth limit, runtime
  errors with call traces, stack safety in every build, examples and tests.
- [x] Strings: `string` type, literals with escapes, concatenation, equality, printing, strict
  no-conversion rules, browser representation, tests.
- [x] String builtins `length`, `substring`, `to_string` and `Input()` of quoted strings from
  standard input and files, with documented rules for spaces, lines and mixed types.
- [x] Browser: variable inspector fed by the interpreter's final state (also at the moment of
  a runtime error), call traces as clickable lines with call sites marked in the source.
- [x] Modules, part 1: `import "file";` syntax, the module loader (paths relative to the
  importing file, one module per file, dependency order), diagnostics for missing files,
  duplicate and circular imports and syntax errors in imported files naming the file and the
  import chain, `--modules`, browser and JSON support, tests in `tests/modules/`.
- [x] Modules, part 2: type checking and execution of imported functions - functions of all
  modules collected in dependency order, visibility through the import graph, duplicate names
  reported with both files, libraries restricted to definitions and imports, file names in
  type errors and runtime call traces, browser trace lines with files, tests.
- [x] Standard library in MyDSL (`stdlib/`): int, float and string modules with examples,
  an API reference, edge-case and invalid-input tests and full-precision numeric checks.
- [x] `error(message)` statement; the standard library explains rejected arguments with it;
  layered and validation examples; browser and CLI tests for raised errors.

Keep precedence, examples and diagnostics consistent with the documented rules. Static errors
must prevent execution, and unfinished features must report errors rather than silently succeed.

## Limitations

- **Standard input is shared.** When the program itself comes from standard input
  (`./mydsl -` or no file argument), `Input()` finds it already consumed; give data with
  `--input FILE` in that case. Standard input is read to the end when the first `Input()` runs,
  so interactive line-by-line input is not supported.
- **Numbers.** `int` is a checked 64-bit integer (overflow is an error, not wrap-around);
  `float` is a double with the rounding behaviour described under "Floating-point results".
  Integer literals larger than 64 bits are rejected by the parser.
- **Type system.** There is no implicit int/float conversion, no conversion to or from
  `string`, no bool or string vectors, no `else if`, and `Input()` is typed by its target only.
- **Strings** support literals, `+`, `==`/`!=`, `Print`, `Input()` (quoted values only),
  `length`, `substring` and `to_string`: no indexing syntax, ordering, parsing of numbers from
  text, raw-line input or Unicode awareness (`length` and `substring` count bytes); a string
  value is at most 1,048,576 characters; columns in error messages count bytes. `inv_vec` reverses a vector (unary `-` negates);
  change one line in `src/interpreter.cpp` if a different meaning was intended.
- **Functions** are top-level only, always return a value (no `void`), cannot be overloaded or
  nested, see no top-level variables, cannot be passed around as values, and may have at most
  1000 active calls (no tail-call elimination: a loop is the way to iterate 10^6 times).
- **Modules** share one function namespace along the import graph: there is no qualification
  (`math_utils.square`), renaming, selective import or re-export control - `import` always
  brings a whole file and everything it imports - and no search path or default extension
  (the file name is literal). Imported modules hold functions only (no shared variables or
  constants), cycles between modules are errors, and the browser page and `./mydsl -` resolve
  imports against the working directory because their program has no file. A call site inside
  an imported file cannot be highlighted in the browser editor (it shows only the main
  program), so such trace lines are listed but not clickable.
- **Identifiers** may contain `'` anywhere (the tokenizer treats it like a letter), so `a'b`
  and `'x` are names.
- **Columns count bytes.** Source positions and browser highlights count bytes/characters of
  the source; a non-ASCII character inside a comment or string shifts the browser highlight on
  that line by the difference between its byte and character lengths. Tabs count as one column.
- **Browser view.** The server is a single-threaded development tool bound to `127.0.0.1`; a
  running program (at most 2 s / 256 KB) blocks parsing requests meanwhile, and
  `Input("file")` reads files relative to the server's working directory. The first error only
  is reported by every stage; there is no error recovery to list several errors at once. The
  variable inspector shows top-level variables only and only after a run (no stepping,
  breakpoints or values inside function calls), and very long values are shown in full.
- **Portability.** The server uses POSIX sockets and the interpreter runs on a POSIX thread
  with a large stack (Linux/macOS); the rest of the tool is portable C++17. Checked arithmetic
  uses the GCC/Clang `__builtin_*_overflow` intrinsics and the interpreter's hot path uses
  `__attribute__((noinline))` to keep its frames small.

## Project layout

- `src/lexer.cpp`: tokenizer.
- `src/parser.cpp`: recursive-descent parser producing the AST; first syntax error is reported with line and column.
- `src/modules.cpp`: module loader for `import` (file resolution, dependency order, duplicate and circular import detection; rules above); the checker and interpreter take its result.
- `src/type_checker.cpp`: static type checker; `src/interpreter.cpp`: interpreter and runtime input (rules above).
- `src/ast.cpp`, `include/mydsl.hpp`: AST and value structures, printers and pipeline interfaces.
- `src/debug.cpp`: JSON views for the browser (`--json`/`/api/parse`, `--run-json`/`/api/run`).
- `web/editor/`: the editor's source (`src/main.js` set-up and page interface,
  `src/mydsl-language.js` the MyDSL mode) and its build (`npm run build` → `web/vendor/mydsl-editor.js`).
- `src/project.cpp`: the project file API of the browser page (listing, reading and atomic
  writing of the files below the server's directory, with the path checks).
- `src/server.cpp`, `web/index.html`: local HTTP server and the browser debugging page (the page is
  embedded into the binary by the makefile).
- `include/environment.hpp`: scoped-environment helper.
- `src/main.cpp`: command-line entry point.
- `stdlib/`: the standard library written in MyDSL (`math.mydsl`, `fmath.mydsl`, `strings.mydsl`,
  `all.mydsl`), its examples (`stdlib/examples/`, including the layered `examples/layered/`) and API reference (`stdlib/README.md`).
- `inputs/`, `tests/data/`, `ip.txt`: example programs (`functions.txt`, `fibonacci.txt`,
  `gcd.txt`, `search.txt` and `vector_functions.txt` show user-defined functions,
  `strings.txt`, `string_functions.txt` and `string_input.txt` strings) and input fixtures
  (`tests/data/names.txt` holds string values).
- `tests/`: unit tests (`tests.cpp`), command-line and server checks (`cli.sh`), the browser page's
  script checks (`web.test.js`), end-to-end programs with expected results (`e2e.sh`, `e2e/`),
  multi-file programs with expected module listings and errors (`modules.sh`, `modules/`), the
  standard library's checks (`stdlib.sh`, `stdlib/`) and malformed programs (`syntax_errors/`,
  `type_errors/`).
- `REVIEW.md`: final review notes (checks performed, results, limitations).
- `makefile`, `.github/workflows/ci.yml`: build and test automation (`make serve` starts the browser view).

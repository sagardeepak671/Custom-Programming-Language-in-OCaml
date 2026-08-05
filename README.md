# MyDSL — A Statically Typed Matrix & Vector Language, in OCaml

A complete implementation of a small programming language built for linear algebra: **lexer →
parser → AST → static type checker → tree-walking interpreter**, all in ~1,300 lines of OCaml.

The language is *dimensionally typed*: a `floatmatrix 2,3` and a `floatmatrix 3,2` are different
types, and the type checker rejects a mismatched multiplication **before** the program runs. It has
integers, floats, booleans, vectors and matrices of both numeric kinds, control flow, scoping, and
built-ins for determinant, inverse, transpose, dot product, magnitude and angle.

> Built for **COL226 – Programming Languages** at IIT Delhi.

---

## Table of Contents

- [Why This Is Interesting](#why-this-is-interesting)
- [Pipeline](#pipeline)
- [Environment Setup](#environment-setup)
- [Build](#build)
- [Running Programs](#running-programs)
- [Language Tour](#language-tour)
- [Type System](#type-system)
- [Built-in Operations](#built-in-operations)
- [Error Handling](#error-handling)
- [Sample Programs](#sample-programs)
- [Project Structure](#project-structure)
- [Contributing](#contributing)

---

## Why This Is Interesting

Most toy interpreters stop at "parse and evaluate". This one carries **dimensions in the type**:

```
floatmatrix 2,2 a := (2,2[[1.,2.],[3.,4.]]);
floatmatrix 2,3 b := ...;
floatmatrix 2,2 c := mult_mat(a, b);   -- rejected at type-check time, not at runtime
```

The type checker maintains a scoped environment and propagates `Matrix_Float(r, c)` /
`Vector_Int(n)` shapes through every operation, so shape errors, redeclarations, out-of-scope uses
and type mismatches are all caught statically.

---

## Pipeline

```
  source (stdin)
       │
       ▼
  lexer.mll   ──ocamllex──▶  token stream
       │
       ▼
  parser.mly  ──ocamlyacc─▶  AST  (ast.ml)
       │
       ├──▶  type_checker.ml   static types + scoping   ──▶  typed environment
       │
       ▼
  interpreter.ml   tree-walking evaluation             ──▶  final environment + output
```

Each stage prints its result, so a single run shows you the AST, the type environment, the
program's own output, and the final variable bindings — useful for teaching and for debugging the
compiler itself.

---

## Environment Setup

| Requirement | Notes |
|---|---|
| OCaml ≥ 4.08 | `ocamlc` compiler |
| `ocamllex` | Ships with OCaml |
| `ocamlyacc` | Ships with OCaml |
| GNU Make | Any recent version |

No opam packages are needed — only the OCaml standard library.

<details open>
<summary><b>Install OCaml via opam (recommended, all platforms)</b></summary>

```bash
# Linux / macOS
bash -c "sh <(curl -fsSL https://opam.ocaml.org/install.sh)"
opam init -y
eval $(opam env)
opam switch create 4.14.1
eval $(opam env)
```
</details>

<details>
<summary><b>Linux (Debian / Ubuntu) — system packages</b></summary>

```bash
sudo apt update
sudo apt install -y ocaml ocaml-nox make
```
</details>

<details>
<summary><b>macOS</b></summary>

```bash
brew install ocaml opam
opam init -y && eval $(opam env)
```
</details>

<details>
<summary><b>Windows</b></summary>

Use **WSL2** and follow the Linux instructions.
</details>

Verify:

```bash
ocamlc -version && ocamllex -version && ocamlyacc -version
```

---

## Build

```bash
git clone https://github.com/<your-username>/Designing-a-programming-Language_in_OCaml.git
cd Designing-a-programming-Language_in_OCaml

make            # produces ./mydsl
```

| Target | Effect |
|---|---|
| `make` | Build the `mydsl` executable |
| `make run` | Build and run `inputs/t.txt` |
| `make clean` | Remove `*.cmo`, `*.cmi`, generated `lexer.ml`/`parser.ml`/`parser.mli`, and `mydsl` |

Build order is `ast → parser → lexer → type_checker → interpreter → main`; `ocamlyacc` and
`ocamllex` are invoked automatically.

---

## Running Programs

The interpreter reads from **stdin**:

```bash
./mydsl < inputs/matrix_test.txt
./mydsl < inputs/factorial.txt
./mydsl < inputs/gaussian_elimination.txt
```

Or type a program interactively and finish with `Ctrl+D`:

```bash
./mydsl
Enter DSL code (press Ctrl+D to finish):

int x := 5;
Print(x);
^D
```

A run prints four sections in order:

1. the **AST** as an s-expression-style tree
2. the **type-checked environment** (each variable with its inferred type), or the first type error
3. `<---STD OUT-->` followed by whatever the program's `Print` statements emit
4. the **final interpreted environment** (each variable with its runtime value)

---

## Language Tour

### Declarations and assignment

```
int    a  := 5;
float  x  := 3.14;
bool   ok := true;

intvector   3 v := (3[1,2,3]);
floatvector 3 w := (3[1.,2.,3.]);
intmatrix   2,2 m := (2,2[[1,2],[3,4]]);
floatmatrix 2,2 n := (2,2[[1.,2.],[3.,4.]]);

a := a + 1;              -- reassignment, no type annotation
```

### Input

```
floatvector 5 vec := Input();                 -- read from stdin
floatmatrix 2,2 m := Input("matrix.txt");     -- read from a file
```

### Control flow

```
if (a < b) then {
    Print(a);
} else {
    Print(b);
};

while (i < 10) {
    i := i + 1;
};

for (i := 0, i < 4, 1) {
    Print(v[i]);
};
```

> Every statement — including the closing brace of an `if`/`while`/`for` — is terminated by `;`.

### Indexing

```
Print(v[0]);        -- vector element
Print(m[1][0]);     -- matrix element
```

### Comments

```
// single-line comment
```

---

## Type System

| Type | Declaration keyword | Internal representation |
|---|---|---|
| Boolean | `bool` | `Bool` |
| Integer scalar | `int` | `Scalar_Int` |
| Float scalar | `float` | `Scalar_Float` |
| Integer vector | `intvector n` | `Vector_Int of int` |
| Float vector | `floatvector n` | `Vector_Float of int` |
| Integer matrix | `intmatrix r,c` | `Matrix_Int of int * int` |
| Float matrix | `floatmatrix r,c` | `Matrix_Float of int * int` |

Dimensions are part of the type. The checker enforces:

- **Shape agreement** — `a + b` requires identical dimensions; `mult_mat(a, b)` requires
  `cols(a) = rows(b)` and produces `rows(a) × cols(b)`
- **No implicit numeric coercion** — `int` and `float` do not mix
- **Declaration before use**, with block scoping
- **No redeclaration** in the same scope, and no redeclaration at a different type
- **Index bounds** where statically known

---

## Built-in Operations

| Category | Operations |
|---|---|
| Arithmetic | `+` `-` `*` `/` `abs` `mod` |
| Comparison | `=` `!=` `<` `>` `<=` `>=` |
| Logical | `&&` `\|\|` `not`, `inv_bool` |
| Vector | `add_vector`, `scale_vector`, `dotprod_vec`, `mag_vec`, `angle_vec`, `inv_vec`, `is_zero` |
| Matrix | `add_matrix`, `scale_matrix`, `multiply_matrix` / `mult_mat`, `transpose`, `determinant`, `inverse_mat`, `inv_mat` |
| Scalar | `mag_scala`, `inv_scala` |
| I/O | `Input()`, `Input("file")`, `Print(e)` |

---

## Error Handling

Errors are reported per stage, with the stage named, so you always know whether a program failed to
lex, parse, type-check, or run.

| Stage | Example message |
|---|---|
| Lexing | `Lexing error: <detail>` |
| Parsing | `Parse error at line 4, column 12` |
| Type checking | reported inline in place of the type environment (shape mismatch, redeclaration, out-of-scope variable, type mismatch) |
| Runtime | `Runtime error: <detail>` — e.g. division by zero, singular matrix inverse, index out of bounds |

`inputs/error_input1.txt` and `inputs/error_input2.txt` collect these cases, with the triggering
lines commented so you can uncomment them one at a time.

---

## Sample Programs

`inputs/` doubles as the test suite.

| File | Demonstrates |
|---|---|
| `arithmetic_op.txt` | Scalar arithmetic, integer vs. float division |
| `logical_tests.txt` | Boolean operators and comparisons |
| `int_vector_test.txt` | Integer vector construction and operations |
| `matrix_test.txt` | Add, negate, multiply, inverse, determinant, transpose |
| `matrix_indexing.txt` | Element access on vectors and matrices |
| `for_loop.txt`, `while_loop.txt` | Loop constructs |
| `factorial.txt`, `prime.txt`, `check_even.txt` | Classic control-flow programs |
| `binary_search.txt`, `Check_Sorted.txt` | Array algorithms over vectors |
| `gaussian_elimination.txt` | Matrix inverse applied to solving a linear system |
| `eigen_values.txt` | Numerical linear algebra in the DSL |
| `scoping_test.txt` | Block scoping and shadowing rules |
| `float_input.txt` | Reading input at runtime |
| `error_input1.txt`, `error_input2.txt` | Every error class, with cases commented out |

Run them all:

```bash
for f in inputs/*.txt; do echo "== $f"; ./mydsl < "$f"; done
```

---

## Project Structure

```
.
├── lexer.mll         #  88 lines — ocamllex token rules, comments, literals
├── parser.mly        # 196 lines — ocamlyacc grammar, precedence, AST construction
├── ast.ml            # 131 lines — type and expression/statement ADTs, pretty printers
├── type_checker.ml   # 278 lines — scoped type environment, dimension propagation
├── interpreter.ml    # 531 lines — evaluation, linear-algebra kernels, runtime errors
├── main.ml           #  46 lines — driver: parse, print AST, type check, interpret
├── makefile
└── inputs/           # 20 sample programs
```

**Reading order:** `ast.ml` (the data model) → `parser.mly` (the grammar) → `type_checker.ml` →
`interpreter.ml`.

---

## Contributing

Contributions are welcome. Good first issues:

- Accept a filename argument instead of requiring stdin redirection
- Add a `--quiet` flag to suppress the AST and environment dumps
- Add user-defined functions (currently the language has none)
- Replace `ocamlyacc` with Menhir for better error messages
- Add a `tests/` runner comparing output against golden files

```bash
git checkout -b feat/your-change
make clean && make
for f in inputs/*.txt; do ./mydsl < "$f" > /dev/null || echo "FAILED: $f"; done
git commit -m "feat: describe your change"
```

Please keep the implementation dependency-free (standard library only) and update `inputs/` with a
program exercising any new construct.

---

## License

Released for educational use.

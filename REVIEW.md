# Final review of the MyDSL pipeline

Scope: tokenizer, parser, type checker, interpreter, command line and browser debugging view as
of this review. Everything below was run from a clean tree (`make clean`) on Linux with GCC 13;
the CI workflow runs the same targets on Ubuntu and macOS and with Clang sanitizers.

## How to reproduce

```sh
make clean && make -j2      # must print no warnings
make test                   # unit tests, command-line/server checks, page script, end-to-end
make sanitize               # the same under AddressSanitizer + UndefinedBehaviorSanitizer
./mydsl --serve             # browser view; compare with ./mydsl --run-json / --json
```

## What was checked and the results

| Area | Check | Result |
| --- | --- | --- |
| Build | `make clean && make` with `-Wall -Wextra -Wpedantic` | clean, 0 warnings (one pre-existing formatting warning in `lexer.cpp` fixed; no behaviour change) |
| Tokenizer | comment handling, numbers, strings, CR/LF and tab input, invalid characters, end positions (`tests.cpp` lexer/span tests; `--tokens` on every example) | passed |
| Parser | every statement/expression form, precedence and associativity (table-driven cases), literal shapes, 100+ malformed inputs with exact line/column, 100k-deep nesting under ASan, all `inputs/*.txt` and `tests/syntax_errors/*` | passed |
| Type checker | valid programs (resulting variable and expression types), ~130 invalid programs with exact locations, scoping/shadowing/for-variable rules, all builtin signatures, `mult_mat` dimensions, `tests/type_errors/*`, every example (only `error_input2.txt` rejected, at 3:1) | passed |
| Interpreter | scalar arithmetic and printing, C++ `/` and `%` semantics, every overflow path, division by zero, short-circuit, scoping, while/for (counter modification, re-evaluated step), limits, vector/matrix operators and builtins with expected values from NumPy / exact fractions (rectangular products, determinants incl. exact `-84` where floating point gives `-83.99999999999999`, inverses, angles), invalid indices, singular and numerically singular matrices, zero-vector angle, runtime input of every type from strings and files with all error kinds | passed (736 unit checks) |
| Command line | default run mode, exit statuses, nothing printed before a static error, `--quiet`, `--tokens`, `--ast`, `--check`, `--json`, `--run-json`, `--input`, `--help` wording against README (`tests/cli.sh`) | passed |
| Browser view | HTTP server routes, 400/404/405 handling, garbage requests, 1 MB bodies, time and output limits (infinite loop returns in 2 s, server stays responsive); the page's own JavaScript executed in Node against real C++ output: token/node click → highlighted range, cursor → innermost node, banners and markers for lexing/parse/type/runtime errors, inferred types, Run output, stale marker, Input box framing (`tests/web.test.js`, 84 checks) | passed |
| CLI = browser | `tests/e2e.sh`: 23 programs run through the command line and through `/api/run` and `/api/parse`; the JSON must be byte-identical to `--run-json` / `--json` (timing removed) and carry the same error stage/line/column as the hand-written `.err` file | passed, identical |
| End-to-end | `tests/e2e/`: precedence, scoping, loops, vectors, matrices, floats, short-circuit, runtime input from stdin and files, and errors at every stage (lexing, parse, type, undeclared, dimensions, division by zero, index, singular matrix, overflow, zero-vector angle, input EOF/type/size/missing file) with exact expected stdout/stderr written by hand | passed; mutation check confirmed the suite fails on a changed value or column |
| Robustness | 600 random programs (250 random token soups, 350 generated mostly-valid programs with random input data) through `--run-json`, `--json` and `--check` under ASan/UBSan: always exit 0 for the JSON modes, valid JSON, no sanitizer reports, `--check` status consistent with the reported stage | passed (stages seen: 197 parse, 52 lexing, 288 type, 13 runtime errors, 50 clean runs) |
| Sanitizers | `make sanitize` (ASan + UBSan) over all of the above including the HTTP server | passed, no reports |
| Documentation | README flag table vs `--help`, type and execution rules vs tests, browser description vs page, TODO list | updated where stale (`--run-json` in usage line, `--serve` description, intro sentence, test description, TODO, new Limitations section) |

Static errors never execute anything: `tests/cli.sh` and the end-to-end `err_parse`, `err_type`,
`err_undeclared` and `err_dimensions` cases require empty stdout; runtime errors keep the output
produced before them (`err_runtime_*` cases).

## Bugs found during the review

- None in the pipeline's behaviour. The only code change to the working implementation is a
  formatting fix in `src/lexer.cpp` to silence `-Wmisleading-indentation`; the `--help` text was
  brought in line with the README (missing `--run-json`, outdated `--serve` description).
- Earlier review rounds (recorded here for completeness) fixed: stack exhaustion on deeply
  nested input (parser depth limit), the `acos`-based angle formula returning `2e-8` for
  parallel vectors (replaced by Kahan's formula), `Float(inf.0)` printing (non-finite results
  are now runtime errors), an uncaught `std::out_of_range` for out-of-range float input data,
  and a stale runtime-error marker in the browser after a later successful run.

## Known limitations

See README "Limitations": shared standard input when the program is read from stdin, no
interactive input, no implicit int/float conversion, no bool vectors / `else if` / user
functions, `inv_vec` = reverse, identifiers may contain `'`, byte-based columns with non-ASCII
text, single-threaded localhost-only development server with 2 s / 256 KB run limits, first
error only (no error recovery), POSIX sockets and GCC/Clang overflow intrinsics.

Not verified here: the page layout in a real browser (this review environment has no browser;
the page logic was verified in Node and the endpoints with curl), and Windows.

## Later iterations (functions, strings, input, browser inspector)

The review above covers the core pipeline. The features added afterwards were verified the same
way at every step - `make test` (unit tests, `tests/cli.sh`, `tests/web.test.js`, `tests/e2e.sh`)
and `make sanitize` (ASan + UBSan), plus random-program fuzzing under the sanitizers:

| Iteration | Added | Checks |
| --- | --- | --- |
| User-defined functions (syntax, types) | `function`/`return`, signatures, calls, return paths, scoping, diagnostics | 170 unit checks, 9 e2e programs, browser nodes for functions/parameters |
| Function execution | by-value calls in isolated scopes, early returns, recursion, call traces, depth/nesting limits, interpreter on a large-stack thread | 75 unit checks, stack-safety measurements in every build configuration, 600 random recursive programs under the sanitizers |
| Strings | `string` type, literals with escapes, `+`, `==`/`!=`, `Print`, strict no-conversion rules | 135 unit checks, 500 random string programs; fixed a pre-existing lexer message that could split a UTF-8 character |
| String builtins and input | `length`, `substring`, `to_string`, `Input()` of quoted strings from stdin and files | 126 unit checks, 6 e2e programs, 1000 random input/builtin runs |
| Browser inspector | `variables` and structured `trace` in `--run-json`/`/api/run`, variable table and clickable call traces in the page | 22 browser-script checks, 16 unit checks, CLI = browser JSON identity for 47 e2e programs |
| Modules, part 1 | `import "file";` syntax, module loader (`src/modules.cpp`: paths relative to the importing file, canonical-path identity, dependency order, duplicate/circular/missing/unreadable/directory diagnostics, syntax errors in imported files with file and import chain, nesting limit), `--modules`, `Error::file`/`imports` in messages and JSON, browser banner and marker; checker and interpreter reject `import` explicitly until part 2 | 98 unit checks, `tests/modules.sh` with 23 programs and hand-written listings/errors (also from other working directories, through symbolic links and absolute paths, a 99/100-deep chain), 14 browser-script checks, 2 e2e programs, sanitizers. Fixed after a macOS run: files were named by their canonical path, so anything under `$TMPDIR` (`/var -> /private/var`) was reported as `/private/var/...`; messages now use the path as reached, identity still the canonical path (regression checks run the script with `TMPDIR` through a symbolic link) |

| Modules, part 2 | `typecheck(Modules)` / `interpret(Modules)`: functions of all modules collected in dependency order, visibility through the import graph (a library never sees its importer), duplicates reported with both files, libraries restricted to definitions and imports, type errors inside imported bodies with file and import chain, runtime errors with file, per-frame call-site files and chain, `--check` listing imported functions, browser trace lines with files and editor markers on the main program's call site; the `Program` overloads refuse unresolved imports instead of ignoring them | 81 unit checks (calls across files, transitive visibility, diamonds, recursion, `Input()` in imported bodies, by-value arguments, every diagnostic with exact text, error order), 15 more `tests/modules.sh` programs with hand-written outputs and errors, browser endpoints = CLI for multi-file programs, 9 browser-script checks, sanitizers |

| Standard library | `stdlib/` in MyDSL: `math` (20 int functions), `fmath` (30 float functions incl. sqrt/cbrt/exp/ln/sin/cos/pow computed in the language), `strings` (23), `all`, shared `core` for domain errors; examples with expected output; API reference | `tests/stdlib.sh` (25 programs: examples, edge cases, every invalid-argument/overflow path with exact error lines; standalone modules; no builtin/keyword name reuse; every function documented; other working directories; stdin/browser path), 40 unit checks comparing `sqrt_f` (1 ulp), `exp_f`, `ln_f`, `cbrt_f`, `hypot_f`, `pow_f`, `power_f`, `sin_f`, `cos_f`, rounding with `<cmath>` at full precision over ~1500 arguments and the int/string functions with reference implementations over ~1700 cases; 3457 further comparisons with Python during development; sanitizers. Found and fixed while testing: a spurious overflow in `hypot_f` (now scaled) and `cbrt_f` (better start), an off-by-one overflow in `fibonacci(92)`, precision loss of `sin_f`/`cos_f` for large arguments (two-part 2π reduction) |

| `error(message)` | statement like `Print` (keyword, string argument, counts as the end of a path for the return analysis), raised as a Runtime error with the statement's position, trace, file and import chain; the standard library rejects arguments with messages naming the function, rule and value (`core.mydsl` removed, `quote` added); layered and validation examples | 64 unit checks (syntax, types, return paths, execution, traces, JSON, inside imported modules, every library message), 4 e2e programs (CLI = browser JSON), 7 browser-script checks, `tests/stdlib.sh` messages through the CLI and `--run-json`. Fixed a signed-overflow UB report of the sanitizers in the unit tests' own Fibonacci reference (the interpreter was clean) |

| Library review | Read through `stdlib/` for edge cases and readability. Fixed: `floor_f`/`ceil_f` (and `round_f`, `fract_f`, `is_integer_f`) failed for \|x\| >= 2^63 although such floats are integers; `round_f(0.49999999999999994)` gave 1.0 (`floor(x + 0.5)`); `binomial` overflowed in intermediate products for results that fit (from n = 61); `gcd` rejected the smallest int (abs overflow) although the result fits; `div_floor`/`mod_floor` by 0 and `char_at` on "" had unhelpful messages; `sin_f`/`cos_f`/`tan_f` now reject angles beyond 1e9 instead of returning garbage once the rounding limit was gone; `quote` rewritten with `escape_char` instead of a five-level if/else ladder | 15 unit checks for the fixed cases, edge-case programs extended, a three-module program (`tests/stdlib/err_trace_across_modules`) checking every trace frame's file through the API, the CLI and `--run-json` |

Final state: 1575 unit checks, 174 browser-script checks, the CLI/server suite, 53 end-to-end
programs, 38 module programs and 28 standard-library programs pass in the optimized build and
under the sanitizers. Known limitations are listed in README "Limitations".

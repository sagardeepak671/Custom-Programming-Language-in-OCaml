#!/bin/sh
# Standard library checks (README "Standard library", stdlib/README.md). Run from the project
# directory: sh tests/stdlib.sh ./mydsl
#
# Every example stdlib/examples/NAME.mydsl and every test program tests/stdlib/NAME.mydsl is
# run with NAME.in as standard input (if present) and must print exactly NAME.out; if NAME.err
# exists the run must end with exactly that error line and exit status 1 (invalid arguments
# stop the program with a runtime error whose call trace names the library function; type
# errors at the call site name the library file). The expected files were written by hand or
# checked against Python's math module and string methods (tests/tests.cpp repeats the numeric
# comparisons against the C library at full precision).
#
# Then: every module loads and type-checks on its own; no library function is named like a
# builtin or a keyword; every function name is unique across the library; the examples work
# from another working directory; and a program typed into the browser (standard input, paths
# relative to the working directory) can use the library.
set -eu
exe=${1:-./mydsl}
case $exe in /*) exe_abs=$exe ;; *) exe_abs=$PWD/$exe ;; esac
failures=0
cases=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

report() { echo "FAIL: $1" >&2; failures=$((failures + 1)); }

for prog in stdlib/examples/*.mydsl stdlib/examples/layered/main.mydsl tests/stdlib/*.mydsl; do
  name=${prog%.mydsl}
  cases=$((cases + 1))
  input=/dev/null
  [ -f "$name.in" ] && input="$name.in"
  set +e
  "$exe" --input "$input" "$prog" > "$tmp/out" 2> "$tmp/err"
  status=$?
  set -e
  expected_out="$name.out"
  [ -f "$expected_out" ] || expected_out=/dev/null
  cmp -s "$tmp/out" "$expected_out" || { report "$prog: stdout differs from $name.out"; diff "$expected_out" "$tmp/out" >&2 || true; }
  if [ -f "$name.err" ]; then
    [ "$status" -eq 1 ] || report "$prog: exit status $status, expected 1"
    cmp -s "$tmp/err" "$name.err" || { report "$prog: stderr differs from $name.err"; diff "$name.err" "$tmp/err" >&2 || true; }
  else
    [ "$status" -eq 0 ] || report "$prog: exit status $status, expected 0: $(cat "$tmp/err")"
    [ ! -s "$tmp/err" ] || report "$prog: unexpected stderr: $(cat "$tmp/err")"
  fi
done

# Every module is a valid program on its own (definitions only: nothing runs, nothing is printed).
for module in stdlib/*.mydsl stdlib/examples/layered/temperature.mydsl tests/stdlib/helpers/checked_math.mydsl; do
  "$exe" "$module" > "$tmp/out" 2> "$tmp/err" || report "$module: does not load on its own: $(cat "$tmp/err")"
  [ ! -s "$tmp/out" ] || report "$module: a library module must not print anything"
done
"$exe" --modules stdlib/all.mydsl > "$tmp/modules"
[ "$(tail -1 "$tmp/modules")" = '4 modules in dependency order (each module after the modules it imports, the main program last)' ] || report "all.mydsl module listing: $(cat "$tmp/modules")"
"$exe" --check stdlib/all.mydsl | grep -q '^Type check passed: 3 top-level statement(s), 0 top-level variable(s), [0-9]* imported function(s) from 3 module(s)$' || report '--check of all.mydsl'
# The layered example reaches fmath.mydsl directly and through temperature.mydsl: loaded once.
"$exe" --modules stdlib/examples/layered/main.mydsl > "$tmp/modules"
[ "$(grep -c '^stdlib/fmath.mydsl$' "$tmp/modules")" = 1 ] && [ "$(tail -1 "$tmp/modules")" = '4 modules in dependency order (each module after the modules it imports, the main program last)' ] || report "layered example module listing: $(cat "$tmp/modules")"
# Errors raised with error(...) inside the library: file, message, trace to the program's call, chain.
printf 'import "stdlib/math.mydsl";\nPrint(factorial(-1));' | "$exe" - 2>&1 | grep -q "^Runtime error in 'stdlib/math.mydsl' at line 85, column 23: factorial: the argument must not be negative, but n is -1 (in function 'factorial' called from line 2, column 7) (imported from the main program at line 1, column 1)$" || report 'factorial(-1) message'
printf 'import "stdlib/fmath.mydsl";\nPrint(sqrt_f(-2.5));' | "$exe" - 2>&1 | grep -q "^Runtime error in 'stdlib/fmath.mydsl' at line 108, column 25: sqrt_f: the argument must not be negative, but x is -2.5 (in function 'sqrt_f' called from line 2, column 7)" || report 'sqrt_f(-2.5) message'
printf 'import "stdlib/strings.mydsl";\nPrint(parse_int(" 42"));' | "$exe" - 2>&1 | grep -q "parse_int: \" 42\" is not the text of an integer (expected an optional '-' followed by digits only) (in function 'parse_int' called from line 2, column 7)" || report 'parse_int(" 42") message'
printf 'import "stdlib/strings.mydsl";\nPrint(parse_int("a\\"b"));' | "$exe" - 2>&1 | grep -q 'parse_int: "a\\"b" is not the text of an integer' || report 'parse_int quotes the text as a literal'
printf 'import "stdlib/fmath.mydsl";\nPrint(clamp_f(1.0, 2.0, 1.0));' | "$exe" - 2>&1 | grep -q "clamp_f: low must not exceed high, but low is 2.0 and high is 1.0" || report 'clamp_f message'

# Function names: not a builtin, not a keyword, unique across the library, all documented.
names=$(sed -n 's/^function [a-z]* *[0-9,]* *\([A-Za-z_][A-Za-z0-9_]*\)(.*/\1/p' stdlib/*.mydsl | sort)
builtins='abs angle_vec determinant dotprod_vec Input inv_vec inverse_mat is_zero length mult_mat substring to_float to_int to_string transpose'
keywords='int float bool intvector floatvector intmatrix floatmatrix if then else while for Print true false function return string import'
for name in $names; do
  for taken in $builtins $keywords; do
    [ "$name" != "$taken" ] || report "library function '$name' is named like a builtin or keyword"
  done
  grep -q "\`$name(" stdlib/README.md || report "library function '$name' is not documented in stdlib/README.md"
done
[ "$(printf '%s\n' $names | sort | uniq -d | wc -l)" -eq 0 ] || report "duplicate function names in the library: $(printf '%s\n' $names | sort | uniq -d | tr '\n' ' ')"
[ "$(printf '%s\n' $names | wc -l)" -ge 70 ] || report "unexpectedly few library functions: $(printf '%s\n' $names | wc -l)"
# A program that defines a name the library also defines gets the usual duplicate error (tests/stdlib/err_name_clash).

# The examples resolve "../math.mydsl" relative to themselves: they run from any directory.
(cd stdlib/examples && "$exe_abs" number_facts.mydsl) | diff - stdlib/examples/number_facts.out > /dev/null || report 'example from its own directory'
project=$PWD
(cd /tmp && "$exe_abs" "$project/stdlib/examples/text.mydsl") | diff - stdlib/examples/text.out > /dev/null || report 'example from an unrelated directory'

# From the browser page or standard input the import path is relative to the working directory.
[ "$(printf 'import "stdlib/all.mydsl";\nPrint(to_upper(repeat("ab", 2)) + to_string(power(2, 10)) + to_string(round_f(sqrt_f(2.0) * 100.0)));' | "$exe" -)" = 'String("ABAB1024141.0")' ] || report 'library through standard input'
printf 'import "stdlib/math.mydsl";\nPrint(factorial(-1));' | "$exe" --run-json - | grep -q '"error":{"stage":"Runtime","line":85,"column":23,"message":"factorial: the argument must not be negative, but n is -1","file":"stdlib/math.mydsl","imports":\[{"file":"","line":1,"column":1}\],"trace":\[{"function":"factorial","line":2,"column":7,"repeats":1}\],"omitted":0}' || report "--run-json of an invalid argument: $(printf 'import "stdlib/math.mydsl";\nPrint(factorial(-1));' | "$exe" --run-json -)"

# Three levels (program -> tests/stdlib/helpers -> stdlib): every trace frame names its file, also in the JSON for the browser.
"$exe" --run-json tests/stdlib/err_trace_across_modules.mydsl | grep -q '"file":"stdlib/math.mydsl","imports":\[{"file":"tests/stdlib/helpers/checked_math.mydsl","line":2,"column":1},{"file":"","line":1,"column":1}\],"trace":\[{"function":"factorial","line":3,"column":48,"repeats":1,"file":"tests/stdlib/helpers/checked_math.mydsl"},{"function":"checked_factorial","line":2,"column":37,"repeats":1},{"function":"report","line":4,"column":7,"repeats":1}\],"omitted":0}' || report "--run-json trace across three modules: $("$exe" --run-json tests/stdlib/err_trace_across_modules.mydsl)"

if [ "$failures" -ne 0 ]; then
  echo "$failures standard-library failure(s)" >&2
  exit 1
fi
echo "Standard library checks passed: $cases programs, $(printf '%s\n' $names | wc -l | tr -d ' ') library functions"

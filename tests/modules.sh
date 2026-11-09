#!/bin/sh
# Module system checks (README "Modules"). Run from the project directory: sh tests/modules.sh ./mydsl
#
# Every entry point tests/modules/NAME.mydsl (and app/main.mydsl) is run through the command
# line with NAME.in as standard input (if present). NAME.modules, if present, is the exact
# expected output of `--modules` (the loaded modules in dependency order). NAME.out is the exact
# expected stdout (empty if absent). If NAME.err exists the run must exit with status 1 and
# print exactly that line on stderr: a loader error (missing file, duplicate or circular
# import, syntax error in an imported file), a syntax error in the main program, a type error
# (in the main program or inside an imported module, with the file and import chain) or a
# runtime error inside an imported function (with the file, the call trace naming the file of
# each call site, and the import chain); otherwise it must exit 0 with no stderr. The expected
# files were written by hand from the documented rules.
#
# Then: `--modules` and `--check` must agree with the run on every static error; `--json` and
# `--run-json` go through the loader too; import paths resolve relative to the importing file
# (the same program from another working directory shows the same modules with paths relative
# to that directory); files reached through symbolic links are named as reached; a symbolic
# link to an imported file is the same module; single-file programs load as one module.
set -eu
exe=${1:-./mydsl}
case $exe in /*) exe_abs=$exe ;; *) exe_abs=$PWD/$exe ;; esac # for checks that run from another directory
failures=0
cases=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"; [ -z "${server:-}" ] || kill "$server" 2>/dev/null' EXIT

report() { echo "FAIL: $1" >&2; failures=$((failures + 1)); }

for prog in tests/modules/*.mydsl tests/modules/app/main.mydsl; do
  name=${prog%.mydsl}
  cases=$((cases + 1))
  if [ -f "$name.modules" ]; then
    set +e
    "$exe" --modules "$prog" > "$tmp/modules" 2> "$tmp/modules_err"
    status=$?
    set -e
    [ "$status" -eq 0 ] || report "$prog: --modules exit status $status, expected 0: $(cat "$tmp/modules_err")"
    cmp -s "$tmp/modules" "$name.modules" || { report "$prog: --modules output differs from $name.modules"; diff "$name.modules" "$tmp/modules" >&2 || true; }
  fi
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
    stage=$(sed 's/ error .*//' "$name.err")
    case $stage in
      Runtime) # static checks pass; nothing is printed by --check before it
        "$exe" --check "$prog" > /dev/null 2> "$tmp/err" || report "$prog: --check must accept a program that fails at run time"
        "$exe" --modules "$prog" > /dev/null 2>&1 || report "$prog: --modules must load a program that fails at run time" ;;
      Type) # the loader is fine, the checker reports the same error
        "$exe" --modules "$prog" > /dev/null 2>&1 || report "$prog: --modules must load a program with a type error"
        "$exe" --check "$prog" > "$tmp/out" 2> "$tmp/err" || true
        [ ! -s "$tmp/out" ] || report "$prog: --check printed declarations despite the type error"
        cmp -s "$tmp/err" "$name.err" || report "$prog: --check reports a different error than a run" ;;
      *) # loader or syntax error: reported identically by --modules and --check
        "$exe" --modules "$prog" > "$tmp/out" 2> "$tmp/err" || true
        [ ! -s "$tmp/out" ] || report "$prog: --modules printed modules despite the error"
        cmp -s "$tmp/err" "$name.err" || report "$prog: --modules reports a different error than a run"
        "$exe" --check "$prog" > "$tmp/out" 2> "$tmp/err" || true
        cmp -s "$tmp/err" "$name.err" || report "$prog: --check reports a different error than a run" ;;
    esac
  else
    [ "$status" -eq 0 ] || report "$prog: exit status $status, expected 0: $(cat "$tmp/err")"
    [ ! -s "$tmp/err" ] || report "$prog: unexpected stderr: $(cat "$tmp/err")"
    "$exe" --quiet --input "$input" "$prog" > "$tmp/out" 2>&1 || report "$prog: --quiet failed"
    [ ! -s "$tmp/out" ] || report "$prog: --quiet printed output"
  fi
done

# --ast prints the import statements of the file (and does not load the imported files).
[ "$("$exe" --ast tests/modules/basic.mydsl | head -1)" = '(import "lib/math_utils.mydsl")' ] || report '--ast of an import statement'
"$exe" --ast tests/modules/err_missing.mydsl > /dev/null || report '--ast must not fail on a missing import (it only parses the given file)'
"$exe" --tokens tests/modules/basic.mydsl | grep -q '^1:8 string "lib/math_utils.mydsl"$' || report '--tokens of an import statement'

# The JSON modes carry the file and the import chain of an error inside an imported file, and
# keep the AST of the main program.
"$exe" --json tests/modules/err_syntax_nested.mydsl > "$tmp/json"
grep -q '"error":{"stage":"Parse","line":3,"column":1,"message":"Expected '"'"';'"'"' to end the return statement but found '"'"'}'"'"'","file":"tests/modules/lib/broken_syntax.mydsl","imports":\[{"file":"tests/modules/lib/imports_broken.mydsl","line":3,"column":1},{"file":"","line":2,"column":1}\]}' "$tmp/json" || report "--json error with file and import chain: $(cat "$tmp/json")"
grep -q '"ast":\[{"node":"statement","op":"print"' "$tmp/json" || report '--json keeps the AST of the main program after an error in an imported file'
grep -q '{"node":"statement","op":"import","name":"lib/imports_broken.mydsl","location":{"line":2,"column":1},"begin":{"line":2,"column":1},"end":{"line":2,"column":35},"exprs":\[\],"body":\[\]}' "$tmp/json" || report '--json import node'
"$exe" --run-json tests/modules/err_missing.mydsl | grep -q '^{"output":"","error":{"stage":"Import","line":1,"column":1,"message":"Cannot find imported file \\"lib/nope.mydsl\\" (resolved to '"'"'tests/modules/lib/nope.mydsl'"'"' relative to the directory of '"'"'tests/modules/err_missing.mydsl'"'"')"},"variables":\[\]' || report '--run-json import error'
"$exe" --run-json tests/modules/basic.mydsl | grep -q '^{"output":"Int(9)\\n","error":null,"variables":\[\]' || report '--run-json runs imported functions'
"$exe" --run-json tests/modules/err_runtime_nested.mydsl | grep -q '^{"output":"Int(9)\\n","error":{"stage":"Runtime","line":4,"column":14,"message":"Division by zero: 1 / 0","file":"tests/modules/lib/failing.mydsl","imports":\[{"file":"","line":1,"column":1}\],"trace":\[{"function":"ratio","line":7,"column":19,"repeats":1,"file":"tests/modules/lib/failing.mydsl"},{"function":"scaled_ratio","line":3,"column":7,"repeats":1}\],"omitted":0},"variables":\[\]' || report "--run-json runtime error inside an imported function: $("$exe" --run-json tests/modules/err_runtime_nested.mydsl)"
"$exe" --json tests/modules/basic.mydsl | grep -q '"op":"call","name":"square","type":"int"' || report '--json annotates calls of imported functions with their return type'
"$exe" --json tests/modules/err_type_in_import.mydsl | grep -q '"error":{"stage":"Type","line":2,"column":14,"message":"Operator .* cannot be applied to int and float.*","file":"tests/modules/lib/bad_body.mydsl","imports":\[{"file":"","line":1,"column":1}\]}' || report '--json type error inside an imported module'
# --check lists imported functions after the program's own, with their module.
"$exe" --check tests/modules/mixed.mydsl > "$tmp/check"
grep -q '^base: int$' "$tmp/check" && grep -q '^shifted_square: function(int x) -> int$' "$tmp/check" && grep -q "^square: function(int x) -> int (imported from 'tests/modules/lib/math_utils.mydsl')$" "$tmp/check" &&
  grep -q '^Type check passed: 9 top-level statement(s), 1 top-level variable(s), 3 function(s), 4 imported function(s) from 2 module(s)$' "$tmp/check" || report "--check listing with imported functions: $(cat "$tmp/check")"
"$exe" --check inputs/functions.txt | grep -q '^Type check passed: 9 top-level statement(s), 1 top-level variable(s), 4 function(s)$' || report '--check of a single-file program is unchanged'

# Imports resolve relative to the importing file, not the working directory: the same program
# from inside tests/modules lists the same modules, shown relative to that directory.
sed 's#tests/modules/##g' tests/modules/app/main.modules > "$tmp/expected"
(cd tests/modules && "$exe_abs" --modules app/main.mydsl) > "$tmp/actual" 2>&1 || report 'app/main.mydsl from tests/modules'
cmp -s "$tmp/actual" "$tmp/expected" || { report 'module paths from another working directory'; diff "$tmp/expected" "$tmp/actual" >&2 || true; }
(cd tests/modules/app && "$exe_abs" --modules main.mydsl) > "$tmp/actual" 2>&1 || report 'app/main.mydsl from its own directory'
# From app/ the lib files are outside the working directory and are shown with absolute paths.
grep -q '^main.mydsl (main program)$' "$tmp/actual" || report 'main program path relative to its own directory'
grep -q '^helpers.mydsl$' "$tmp/actual" || report 'helper path relative to the working directory'
grep -q "^/.*/tests/modules/lib/vectors.mydsl$" "$tmp/actual" || report 'files outside the working directory are shown with their absolute path'
# A program read from standard input resolves its imports against the working directory.
(cd tests/modules && "$exe_abs" --modules - < basic.mydsl) | grep -q '^<stdin> (main program)$' || report 'stdin program with imports'
printf 'import "lib/math_utils.mydsl";' | "$exe" --modules - 2>&1 | grep -q "^Import error at line 1, column 1: Cannot find imported file \"lib/math_utils.mydsl\" (resolved to 'lib/math_utils.mydsl' relative to the working directory, because the main program was read from standard input)$" || report 'stdin import resolution message'
printf 'import "tests/modules/lib/strings.mydsl";' | "$exe" --modules - | grep -q '^2 modules in dependency order' || report 'stdin import relative to the working directory'

# The same file through a symbolic link or an absolute path is the same module (duplicate).
mkdir -p "$tmp/link"
cp tests/modules/lib/strings.mydsl "$tmp/link/strings.mydsl"
if ln -s strings.mydsl "$tmp/link/alias.mydsl" 2>/dev/null; then
  printf 'import "strings.mydsl";\nimport "alias.mydsl";\n' > "$tmp/link/main.mydsl"
  "$exe" --modules "$tmp/link/main.mydsl" 2>&1 | grep -q "^Import error at line 2, column 1: Duplicate import of \"alias.mydsl\": the same file '$tmp/link/strings.mydsl' was already imported at line 1, column 1 as \"strings.mydsl\"; remove one of the two imports$" || report 'symbolic link is the same module'
fi
printf 'import "strings.mydsl";\nimport "%s/link/strings.mydsl";\n' "$tmp" > "$tmp/link/main2.mydsl"
"$exe" --modules "$tmp/link/main2.mydsl" 2>&1 | grep -q '^Import error at line 2, column 1: Duplicate import of ".*": the same file' || report 'absolute path is the same module'
printf 'import "%s/link/strings.mydsl";\n' "$tmp" > "$tmp/link/main3.mydsl"
"$exe" --modules "$tmp/link/main3.mydsl" | grep -q '^2 modules in dependency order' || report 'absolute import path'
printf 'import "%s/link/nope.mydsl";\n' "$tmp" > "$tmp/link/main4.mydsl"
"$exe" --modules "$tmp/link/main4.mydsl" 2>&1 | grep -q "^Import error at line 1, column 1: Cannot find imported file \"$tmp/link/nope.mydsl\"$" || report 'absolute missing import'

# Files reached through a symbolic link to a directory (on macOS every temporary directory is:
# $TMPDIR lies under /var -> /private/var) are named in messages by the path they were reached
# through, never by the resolved one - while identity still resolves the link, so the same
# file through the link and through the real directory is one module.
mkdir -p "$tmp/real/pkg"
if ln -s real "$tmp/via" 2>/dev/null; then
  cp tests/modules/lib/strings.mydsl "$tmp/real/pkg/strings.mydsl"
  printf 'import "strings.mydsl";\nimport "../pkg/nope.mydsl";\n' > "$tmp/real/pkg/main.mydsl"
  "$exe" --modules "$tmp/via/pkg/main.mydsl" 2>&1 | grep -q "^Import error at line 2, column 1: Cannot find imported file \"../pkg/nope.mydsl\" (resolved to '$tmp/via/pkg/nope.mydsl' relative to the directory of '$tmp/via/pkg/main.mydsl')$" || report 'paths through a directory symlink are shown as reached (missing file)'
  printf 'import "strings.mydsl";\nimport "%s/real/pkg/strings.mydsl";\n' "$tmp" > "$tmp/real/pkg/main.mydsl"
  "$exe" --modules "$tmp/via/pkg/main.mydsl" 2>&1 | grep -q "^Import error at line 2, column 1: Duplicate import of \"$tmp/real/pkg/strings.mydsl\": the same file '$tmp/via/pkg/strings.mydsl' was already imported at line 1, column 1 as \"strings.mydsl\"; remove one of the two imports$" || report 'the same file through the link and the real directory is one module, named as first reached'
  printf 'import "strings.mydsl";\nimport "broken.mydsl";\n' > "$tmp/real/pkg/main.mydsl"
  cp tests/modules/lib/broken_syntax.mydsl "$tmp/real/pkg/broken.mydsl"
  "$exe" "$tmp/via/pkg/main.mydsl" 2>&1 | grep -q "^Parse error in '$tmp/via/pkg/broken.mydsl' at line 3, column 1: Expected ';' to end the return statement but found '}' (imported from the main program at line 2, column 1)$" || report 'an error inside an imported file names the file as reached'
  "$exe" --modules "$tmp/via/pkg/main.mydsl" 2>&1 | grep -q "^Import error in '$tmp/via/pkg/broken.mydsl'" && report 'the loader must not invent a different stage'
  printf 'import "strings.mydsl";\n' > "$tmp/real/pkg/main.mydsl"
  "$exe" --modules "$tmp/via/pkg/main.mydsl" | grep -q "^$tmp/via/pkg/strings.mydsl$" || report '--modules lists files as reached through the link'
  # From inside the linked directory the same files are shown relative to it.
  (cd "$tmp/via/pkg" && "$exe_abs" --modules main.mydsl) | grep -q '^strings.mydsl$' || report 'relative display from inside a linked directory'
fi

# Only regular files can be imported (no devices, pipes or sockets).
if [ -c /dev/null ]; then
  printf 'import "/dev/null";' | "$exe" --modules - 2>&1 | grep -q "^Import error at line 1, column 1: Cannot import \"/dev/null\": '/dev/null' is not a regular file$" || report 'device files are rejected'
fi

# Deep chains: 99 nested imports load, 100 are rejected with a capped chain in the message.
mkdir -p "$tmp/deep"
i=1
while [ "$i" -le 120 ]; do printf 'import "d%d.mydsl";\n' "$((i + 1))" > "$tmp/deep/d$i.mydsl"; i=$((i + 1)); done
printf 'function int last() { return 1; };\n' > "$tmp/deep/d99.mydsl"
printf 'import "deep/d1.mydsl";\n' > "$tmp/chain.mydsl"
"$exe" --modules "$tmp/chain.mydsl" | grep -q '^100 modules in dependency order' || report '99 nested imports load'
printf 'import "d100.mydsl";\n' > "$tmp/deep/d99.mydsl"
"$exe" --modules "$tmp/chain.mydsl" 2>&1 | grep -q "^Import error in '$tmp/deep/d99.mydsl' at line 1, column 1: Import nesting is too deep: the limit is 100 levels of imports (is there a very long chain of modules?) (imported from '$tmp/deep/d98.mydsl' at line 1, column 1; .*; imported from '$tmp/deep/d94.mydsl' at line 1, column 1; ... 94 more import(s))$" || report 'import nesting limit'

# The browser endpoints resolve imports against the server's working directory and must answer
# exactly like the command line run from that directory (the e2e suite does this for tests/e2e).
if command -v curl > /dev/null 2>&1; then
  "$exe" --serve 0 2> "$tmp/server.log" &
  server=$!
  port=''
  for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    port=$(sed -n 's/.*http:\/\/127\.0\.0\.1:\([0-9]*\)\/.*/\1/p' "$tmp/server.log" 2>/dev/null || true)
    [ -n "$port" ] && break
    sleep 0.25
  done
  if [ -n "$port" ]; then
    for prog in tests/modules/err_runtime_nested.mydsl tests/modules/err_type_in_import.mydsl tests/modules/err_syntax_nested.mydsl; do
      # Written so that the editor's text resolves the same files: the import paths start at the project directory.
      sed 's#import "lib/#import "tests/modules/lib/#' "$prog" > "$tmp/editor.mydsl"
      { printf '%s\n' "$(wc -c < "$tmp/editor.mydsl" | tr -d ' ')"; cat "$tmp/editor.mydsl"; } > "$tmp/body"
      curl -sf -X POST --data-binary @"$tmp/body" "http://127.0.0.1:$port/api/run" | sed 's/,"millis":[0-9]*}$/}/' > "$tmp/server_run"
      "$exe" --run-json - < "$tmp/editor.mydsl" | sed 's/,"millis":[0-9]*}$/}/' > "$tmp/cli_run"
      cmp -s "$tmp/server_run" "$tmp/cli_run" || { report "$prog: /api/run differs from --run-json"; diff "$tmp/cli_run" "$tmp/server_run" >&2 || true; }
      curl -sf -X POST --data-binary @"$tmp/editor.mydsl" "http://127.0.0.1:$port/api/parse" > "$tmp/server_parse"
      "$exe" --json - < "$tmp/editor.mydsl" > "$tmp/cli_parse"
      cmp -s "$tmp/server_parse" "$tmp/cli_parse" || report "$prog: /api/parse differs from --json"
    done
    printf 'import "tests/modules/lib/math_utils.mydsl";Print(square(12));' > "$tmp/editor.mydsl"
    { printf '%s\n' "$(wc -c < "$tmp/editor.mydsl" | tr -d ' ')"; cat "$tmp/editor.mydsl"; } > "$tmp/body"
    curl -sf -X POST --data-binary @"$tmp/body" "http://127.0.0.1:$port/api/run" | grep -q '^{"output":"Int(144)\\n","error":null,"variables":\[\]' || report '/api/run executes an imported function'
  else
    report 'server did not start'
  fi
  kill "$server" 2>/dev/null || true
  wait "$server" 2>/dev/null || true
  server=''
else
  echo 'curl not found: skipping the browser endpoint checks' >&2
fi

# Single-file programs are unchanged: one module, no file system access, same results.
"$exe" --modules inputs/factorial.txt | grep -q '^1 module (the program has no imports)$' || report 'single-file program has one module'
[ "$("$exe" --modules inputs/functions.txt | sed -n 2p)" = '  functions: square, factorial, mean, is_even' ] || report 'functions of a single-file program'
[ "$("$exe" inputs/factorial.txt)" = 'Int(120)' ] || report 'single-file program still runs'
"$exe" --help | grep -q -- '--modules' || report '--help mentions --modules'

if [ "$failures" -ne 0 ]; then
  echo "$failures module-system failure(s)" >&2
  exit 1
fi
echo "Module system checks passed: $cases programs"

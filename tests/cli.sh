#!/bin/sh
# Command-line checks. Run from the project directory: sh tests/cli.sh ./mydsl
set -eu
exe=${1:-./mydsl}

# Default mode parses, type-checks and runs the program.
[ "$("$exe" inputs/factorial.txt)" = 'Int(120)' ]
[ "$("$exe" < inputs/prime.txt)" = 'Bool(true)' ]
[ "$("$exe" inputs/arithmetic_op.txt | tr '\n' ' ')" = 'Int(6) Int(3) Int(8) Int(23) Int(-1) ' ]
[ "$(printf 'for (i := 0, i < 3, 1) { Print(i * i); };' | "$exe" - | tr '\n' ' ')" = 'Int(0) Int(1) Int(4) ' ]
# Static errors prevent execution: nothing is printed before the type error.
if "$exe" inputs/error_input2.txt > /tmp/mydsl_run_out 2> /tmp/mydsl_run_err; then echo 'error_input2.txt should fail' >&2; exit 1; fi
[ ! -s /tmp/mydsl_run_out ]
grep -q '^Type error at line 3, column 1:' /tmp/mydsl_run_err
# Runtime errors exit with status 1 and name the location.
if "$exe" inputs/error_input1.txt > /dev/null 2> /tmp/mydsl_run_err; then echo 'error_input1.txt should fail' >&2; exit 1; fi
grep -q '^Runtime error at line 2, column 20: Division by zero' /tmp/mydsl_run_err
printf 'int a := 9223372036854775807;\na := a + 1;' | "$exe" - 2>&1 | grep -q '^Runtime error at line 2, column 8: Integer overflow'
# Vector/matrix programs and runtime input (--input DATA_FILE or stdin).
[ "$("$exe" inputs/gaussian_elimination.txt | tr '\n' ' ')" = 'FloatMatrix[   [2.0]   [3.0] ] Bool(true) ' ]
[ "$("$exe" inputs/eigen_values.txt | tr '\n' ' ')" = 'Float(3.0) Float(1.0) ' ]
"$exe" inputs/matrix_test.txt && "$exe" inputs/test.txt
[ "$("$exe" --input tests/data/binary_search.txt inputs/binary_search.txt)" = 'Int(3)' ]
[ "$("$exe" --input tests/data/sorted.txt inputs/Check_Sorted.txt | tail -1)" = 'Bool(true)' ]
[ "$("$exe" --input tests/data/int.txt inputs/check_even.txt)" = 'Bool(true)' ]
[ "$("$exe" --input tests/data/float.txt inputs/float_input.txt | tr '\n' ' ')" = 'Float(23.5) Float(7.25) ' ]
[ "$(printf 'int a := Input(); Print(a * 2);' > /tmp/mydsl_prog.txt; printf '21' | "$exe" /tmp/mydsl_prog.txt)" = 'Int(42)' ]
if "$exe" --input tests/data/float.txt inputs/check_even.txt > /tmp/mydsl_run_out 2> /tmp/mydsl_run_err; then echo 'check_even.txt with float data should fail' >&2; exit 1; fi
[ "$(cat /tmp/mydsl_run_out)" = 'Bool(true)' ]
grep -q "^Runtime error at line 10, column 9: Invalid data in standard input at line 1, column 1: expected an int (a float was given) but found '7.25'" /tmp/mydsl_run_err
printf 'int a := Input("missing.txt");' | "$exe" - 2>&1 | grep -q "^Runtime error at line 1, column 10: Cannot open input file 'missing.txt'"
printf 'intvector 3 v := Input();' > /tmp/mydsl_prog.txt; printf '[1, 2]' | "$exe" /tmp/mydsl_prog.txt 2>&1 | grep -q "expected ',' (the vector needs 3 elements) but found ']'"
printf 'Print(inverse_mat([[1., 2.], [2., 4.]]));' | "$exe" - 2>&1 | grep -q 'inverse_mat: the matrix is singular'
printf 'intvector 2 v := [1, 2]; int i := 2; Print(v[i]);' | "$exe" - 2>&1 | grep -q '^Runtime error at line 1, column 46: Index 2 is out of range for intvector 2 (valid indices are 0..1)'
rm -f /tmp/mydsl_run_out /tmp/mydsl_run_err /tmp/mydsl_prog.txt
# --quiet runs but discards the output; --tokens prints tokens only.
[ -z "$("$exe" --quiet inputs/factorial.txt)" ]
"$exe" --tokens inputs/factorial.txt | grep -q '^4:1 word "while"$'
"$exe" --tokens inputs/factorial.txt | grep -q '^9:1 end "<eof>"$'
for f in inputs/*.txt; do "$exe" --tokens "$f" > /dev/null; done

# --ast parses and prints the tree for every example program.
for f in inputs/*.txt; do "$exe" --ast "$f" > /dev/null; done
"$exe" --ast inputs/factorial.txt | grep -q '^(while (binary <= (variable i) (variable num))$'
"$exe" --ast inputs/functions.txt | grep -q '^(function float mean (parameter floatvector 3 v)$'
"$exe" --ast inputs/functions.txt | grep -q '^    (return (binary / (variable sum) Float(3.0)))))$'
"$exe" --ast inputs/functions.txt | grep -q '^(declare int a (call square Int(7)))$'
"$exe" --ast inputs/eigen_values.txt | grep -q '(declare floatmatrix 2,2 a (matrix\[2,2\] Float(2.0) Float(1.0) Float(1.0) Float(2.0)))'
printf 'int a := 1 + 2 * 3;' | "$exe" --ast - | grep -q '^(declare int a (binary + Int(1) (binary \* Int(2) Int(3))))$'

# --ast reports syntax errors with a location, exits with status 1 and prints no tree.
for f in tests/syntax_errors/*.txt; do
  if "$exe" --ast "$f" > /tmp/mydsl_ast_out 2> /tmp/mydsl_ast_err; then
    echo "Expected a parse error for $f" >&2; exit 1
  fi
  [ ! -s /tmp/mydsl_ast_out ] || { echo "Unexpected AST output for $f" >&2; exit 1; }
  grep -q '^\(Parse\|Lexing\) error at line [0-9]*, column [0-9]*: ' /tmp/mydsl_ast_err || { cat /tmp/mydsl_ast_err >&2; exit 1; }
done
printf 'if (a) then {\n  x := 1\n};' | "$exe" --ast - 2>&1 | grep -q "^Parse error at line 3, column 1: Expected ';' to end the assignment to 'x' but found '}'"
rm -f /tmp/mydsl_ast_out /tmp/mydsl_ast_err

# --json always exits 0 and reports errors structurally.
printf 'int a := 1;' | "$exe" --json - | grep -q '"error":null'
printf 'Print(6 * 7);' | "$exe" --run-json - | grep -q '^{"output":"Int(42)\\n","error":null,"variables":\[\],"millis":'
"$exe" --run-json --input tests/data/int.txt inputs/check_even.txt | grep -q '^{"output":"Bool(true)\\n","error":null'
printf 'int a := ;' | "$exe" --json - | grep -q '"error":{"stage":"Parse","line":1,"column":10,'
"$exe" --json inputs/functions.txt | grep -q '"op":"function","name":"square","declaration":"int",.*"params":\[{"node":"parameter","op":"parameter","name":"x","declaration":"int",'
"$exe" --json inputs/functions.txt | grep -q '"op":"call","name":"factorial","type":"int"'
"$exe" --json inputs/functions.txt | grep -q '"error":null'
printf 'int a := @;' | "$exe" --json - | grep -q '"tokens":\[\],"ast":null,"error":{"stage":"Lexing"'
"$exe" --json inputs/eigen_values.txt | grep -q '"op":"matrix","type":"floatmatrix 2,2","shape":{"rank":2,"rows":2,"cols":2}'
"$exe" --json inputs/functions.txt | grep -q '"functions":\[{"name":"square","params":\[{"name":"x","type":"int"}\],"returns":"int","line":'
"$exe" --json tests/modules/nested.mydsl | grep -q '"file":"tests/modules/lib/vectors.mydsl"}'
# --language-json: the vocabulary for the editor's completion (no source file).
"$exe" --language-json | grep -q '^{"keywords":\[{"name":"Print","kind":"statement","detail":"Print(value);"'
"$exe" --language-json | grep -q '{"name":"transpose","signature":"transpose(m)","detail":"matrix R,C -> matrix C,R","description":"The transpose of a matrix."}\]}$'
status=0; "$exe" --language-json inputs/factorial.txt > /dev/null 2>&1 || status=$?   # a source file is a usage error
[ "$status" = 2 ]

# --serve: the browser view is served by the binary itself and parses via the C++ parser.
if command -v curl > /dev/null 2>&1; then
  log=$(mktemp)
  "$exe" --serve 0 2> "$log" &
  server=$!
  trap 'kill $server 2>/dev/null; rm -f "$log"' EXIT
  port=''
  for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    port=$(sed -n 's/.*http:\/\/127\.0\.0\.1:\([0-9]*\)\/.*/\1/p' "$log" 2>/dev/null || true) # the log may not exist yet
    [ -n "$port" ] && break
    sleep 0.25
  done
  [ -n "$port" ] || { echo 'Server did not start:' >&2; cat "$log" >&2; exit 1; }
  url="http://127.0.0.1:$port"
  curl -sf "$url/" | grep -q '<title>MyDSL debug view</title>'
  curl -sf "$url/" | grep -q 'id="ast"'
  curl -sf "$url/" | grep -q '<script src="vendor/mydsl-editor.js"></script>'
  curl -sf -D - -o /dev/null "$url/vendor/mydsl-editor.js" | grep -q 'Content-Type: application/javascript'
  curl -sf "$url/vendor/mydsl-editor.js" | head -c 200 | grep -q 'mydsl-editor: CodeMirror 6'
  [ "$(curl -sf "$url/vendor/mydsl-editor.js" | wc -c | tr -d ' ')" = "$(wc -c < web/vendor/mydsl-editor.js | tr -d ' ')" ]   # the embedded copy is complete
  curl -sf -X POST --data-binary 'Print(1 + 2);' "$url/api/parse" | grep -q '"op":"binary","name":"+"'
  curl -sf -X POST --data-binary 'Print(1 + ;' "$url/api/parse" | grep -q '"error":{"stage":"Parse","line":1,"column":11,'
  curl -sf -X POST --data-binary @inputs/binary_search.txt "$url/api/parse" | grep -q '"error":null'
  curl -sf -X POST --data-binary 'int a := 1.5;' "$url/api/parse" | grep -q '"error":{"stage":"Type","line":1,"column":1,'
  # /api/run body: "<source byte count>\n" + source + standard input.
  framed() { printf '%s\n%s%s' "$(printf '%s' "$1" | wc -c | tr -d ' ')" "$1" "$2"; }
  framed "$(cat inputs/factorial.txt)" '' | curl -sf -X POST --data-binary @- "$url/api/run" | grep -q '^{"output":"Int(120)\\n","error":null,"variables":\[{"name":"a","type":"int","value":"Int(120)"},{"name":"i","type":"int","value":"Int(6)"},{"name":"num","type":"int","value":"Int(5)"}\],"millis":'
  framed 'Print(1); Print(1 / 0);' '' | curl -sf -X POST --data-binary @- "$url/api/run" | grep -q '^{"output":"Int(1)\\n","error":{"stage":"Runtime","line":1,"column":19,"message":"Division by zero: 1 / 0"}'
  framed 'int a := true;' '' | curl -sf -X POST --data-binary @- "$url/api/run" | grep -q '"error":{"stage":"Type"'
  framed "$(cat inputs/fibonacci.txt)" '' | curl -sf -X POST --data-binary @- "$url/api/run" | grep -q '^{"output":"Int(0)\\nInt(1)\\nInt(1)\\nInt(2)\\nInt(3)\\nInt(5)\\nInt(8)\\nInt(13)\\nInt(21)\\nInt(34)\\nInt(55)\\nBool(true)\\nInt(2880067194370816120)\\n","error":null'
  framed 'function int f(int n) { return f(n + 1); }; Print(f(0));' '' | curl -sf -X POST --data-binary @- "$url/api/run" | grep -q '"error":{"stage":"Runtime","line":1,"column":32,"message":"Call depth limit of 1000 exceeded'
  framed 'function int spin(int n) { while (true) { n := n + 1; }; return n; }; Print(spin(0));' '' | curl -sf --max-time 10 -X POST --data-binary @- "$url/api/run" | grep -q '"message":"Execution stopped after 2000 ms (the time limit was exceeded; is there an infinite loop?)","trace":\[{"function":"spin","line":1,"column":77,"repeats":1}\],"omitted":0}' 
  framed 'floatvector 2 v := Input(); float k := Input(); Print(v * k);' '[1.5, -2.] 2' | curl -sf -X POST --data-binary @- "$url/api/run" | grep -q '^{"output":"FloatVector\[3.0, -4.0\]\\n","error":null'
  framed 'int a := Input();' '' | curl -sf -X POST --data-binary @- "$url/api/run" | grep -q 'Unexpected end of standard input'
  framed 'string s := Input(); int n := Input(); Print(substring(s, n, 2) + to_string(length(s)));' '"hello world" 6' | curl -sf -X POST --data-binary @- "$url/api/run" | grep -q '^{"output":"String(\\"wo11\\")\\n","error":null'
  framed 'string s := Input();' 'bare' | curl -sf -X POST --data-binary @- "$url/api/run" | grep -q "expected a string in double quotes (e.g. \\\\\"text\\\\\"), not bare text but found 'bare'"
  framed "$(cat inputs/binary_search.txt)" "$(cat tests/data/binary_search.txt)" | curl -sf -X POST --data-binary @- "$url/api/run" | grep -q '^{"output":"Int(3)\\n","error":null'
  [ "$(printf 'not framed' | curl -s -o /dev/null -w '%{http_code}' -X POST --data-binary @- "$url/api/run")" = 400 ]
  # An infinite loop must come back (the server enforces a time limit) and must not wedge the server.
  framed 'while (true) {};' '' | curl -sf --max-time 10 -X POST --data-binary @- "$url/api/run" | grep -q 'Execution stopped after 2000 ms'
  framed 'int i := 0; while (true) { Print(i); i := i + 1; };' '' | curl -sf --max-time 10 -X POST --data-binary @- "$url/api/run" | grep -q 'Output limit of 262144 bytes exceeded'
  framed 'Print(2);' '' | curl -sf -X POST --data-binary @- "$url/api/run" | grep -q '"output":"Int(2)\\n"'
  [ "$(curl -s -o /dev/null -w '%{http_code}' "$url/api/run")" = 405 ]
  curl -sf "$url/api/examples" | grep -q '"name":"factorial.txt"'
  [ "$(curl -sf "$url/api/language")" = "$("$exe" --language-json)" ]
  # The project file API: the files below the working directory, nothing else.
  curl -sf "$url/api/files" | grep -q '"root":"'"$(pwd -P)"'","truncated":false,"entries":\[{"path":"inputs","dir":true},{"path":"inputs/Check_Sorted.txt","dir":false,"writable":'
  curl -sf "$url/api/files" | grep -q '{"path":"stdlib/examples/layered/main.mydsl","dir":false'
  curl -sf "$url/api/files" | grep -qv '"path":"makefile"\|"path":".git\|"path":"build\|node_modules\|web/editor'
  curl -sf "$url/api/file?path=inputs%2Ffactorial.txt" | grep -q '^{"path":"inputs/factorial.txt","content":"'
  [ "$(curl -s -o /dev/null -w '%{http_code}' "$url/api/file?path=../etc/passwd")" = 400 ]
  [ "$(curl -s -o /dev/null -w '%{http_code}' "$url/api/file?path=%2Fetc%2Fpasswd")" = 400 ]
  [ "$(curl -s -o /dev/null -w '%{http_code}' "$url/api/file?path=makefile")" = 403 ]
  [ "$(curl -s -o /dev/null -w '%{http_code}' "$url/api/file?path=.gitignore")" = 403 ]
  [ "$(curl -s -o /dev/null -w '%{http_code}' "$url/api/file?path=nope.mydsl")" = 404 ]
  [ "$(curl -s -o /dev/null -w '%{http_code}' "$url/api/file")" = 400 ]
  [ "$(curl -s -o /dev/null -w '%{http_code}' -X PUT --data-binary 'x' "$url/api/file?path=inputs/zz_cli_test.txt")" = 403 ]          # no X-Requested-With header
  [ "$(curl -s -o /dev/null -w '%{http_code}' -X PUT -H 'X-Requested-With: mydsl' --data-binary 'x' "$url/api/file?path=../zz.mydsl")" = 400 ]
  [ "$(curl -s -o /dev/null -w '%{http_code}' -X PUT -H 'X-Requested-With: mydsl' --data-binary 'x' "$url/api/file?path=zz_nodir/a.mydsl")" = 404 ]
  [ ! -e inputs/zz_cli_test.txt ]
  curl -sf -X PUT -H 'X-Requested-With: mydsl' --data-binary 'Print(7);' "$url/api/file?path=inputs/zz_cli_test.txt" | grep -q '^{"ok":true}'
  [ "$(cat inputs/zz_cli_test.txt)" = 'Print(7);' ]
  curl -sf -X PUT -H 'X-Requested-With: mydsl' --data-binary 'Print(8);' "$url/api/file?path=inputs/zz_cli_test.txt" > /dev/null
  [ "$(cat inputs/zz_cli_test.txt)" = 'Print(8);' ] && [ ! -e inputs/zz_cli_test.txt.mydsl-saving ]
  rm -f inputs/zz_cli_test.txt
  [ "$(curl -s -o /dev/null -w '%{http_code}' -X DELETE "$url/api/file?path=inputs/factorial.txt")" = 405 ]
  [ "$(curl -s -o /dev/null -w '%{http_code}' -H 'Host: evil.example' "$url/api/files")" = 403 ]
  [ "$(curl -s -o /dev/null -w '%{http_code}' -H 'Host: localhost:1' "$url/api/language")" = 200 ]
  # ?path= makes parse/run resolve imports relative to that file.
  curl -sf -X POST --data-binary 'import "temperature.mydsl"; Print(to_fahrenheit(100.0));' "$url/api/parse?path=stdlib%2Fexamples%2Flayered%2Fmain.mydsl" | grep -q '"error":null'
  curl -sf -X POST --data-binary 'import "temperature.mydsl"; Print(to_fahrenheit(100.0));' "$url/api/parse" | grep -q '"stage":"Import"'
  framed 'import "temperature.mydsl"; Print(to_fahrenheit(100.0));' '' | curl -sf -X POST --data-binary @- "$url/api/run?path=stdlib/examples/layered/main.mydsl" | grep -q '^{"output":"Float(212.0)\\n","error":null'
  [ "$(curl -s -o /dev/null -w '%{http_code}' -X POST --data-binary 'Print(1);' "$url/api/parse?path=../x.mydsl")" = 400 ]
  # The "#files" prefix: unsaved texts of other files stand in for the disk versions.
  overlay='function float to_fahrenheit(float c) { return 0.0; };'
  printf '#files 1\n%s %s\n%s%s%s' "$(printf '%s' 'stdlib/examples/layered/temperature.mydsl' | wc -c | tr -d ' ')" "$(printf '%s' "$overlay" | wc -c | tr -d ' ')" 'stdlib/examples/layered/temperature.mydsl' "$overlay" 'import "temperature.mydsl"; Print(to_fahrenheit(100.0));' \
    | curl -sf -X POST --data-binary @- "$url/api/parse?path=stdlib/examples/layered/main.mydsl" | grep -q '"functions":\[{"name":"to_fahrenheit","params":\[{"name":"c","type":"float"}\],"returns":"float","line":1,"column":1,"file":"stdlib/examples/layered/temperature.mydsl"}\]'
  printf '#files 1\n%s %s\n%s%s%s' "$(printf '%s' 'stdlib/examples/layered/temperature.mydsl' | wc -c | tr -d ' ')" "$(printf '%s' "$overlay" | wc -c | tr -d ' ')" 'stdlib/examples/layered/temperature.mydsl' "$overlay" "$(framed 'import "temperature.mydsl"; Print(to_fahrenheit(100.0));' '')" \
    | curl -sf -X POST --data-binary @- "$url/api/run?path=stdlib/examples/layered/main.mydsl" | grep -q '^{"output":"Float(0.0)\\n","error":null'
  [ "$(printf '#files 1\n9 1\n../a.mydslxPrint(1);' | curl -s -o /dev/null -w '%{http_code}' -X POST --data-binary @- "$url/api/parse")" = 400 ]
  [ "$(printf '#files 1\n8 1\nmakefilexPrint(1);' | curl -s -o /dev/null -w '%{http_code}' -X POST --data-binary @- "$url/api/parse")" = 403 ]
  [ "$(printf '#files 2\n7 1\na.mydslb' | curl -s -o /dev/null -w '%{http_code}' -X POST --data-binary @- "$url/api/parse")" = 400 ]   # second entry missing
  [ "$(printf '#files x\nPrint(1);' | curl -s -o /dev/null -w '%{http_code}' -X POST --data-binary @- "$url/api/parse")" = 400 ]
  printf '#files 0\nPrint(1 + 2);' | curl -sf -X POST --data-binary @- "$url/api/parse" | grep -q '"op":"binary","name":"+"'
  [ "$(curl -s -o /dev/null -w '%{http_code}' -X POST --data-binary 'Print(1);' "$url/api/parse?path=missing.mydsl")" = 404 ]
  [ "$(curl -s -o /dev/null -w '%{http_code}' -X POST "$url/api/language")" = 405 ]
  curl -sf -X POST --data-binary 'import "stdlib/math.mydsl"; Print(factorial(3));' "$url/api/parse" | grep -q '"functions":\[{"name":"square","params":\[{"name":"x","type":"int"}\],"returns":"int","line":8,"column":1,"file":"stdlib/math.mydsl"}'
  [ "$(curl -s -o /dev/null -w '%{http_code}' "$url/missing")" = 404 ]
  [ "$(curl -s -o /dev/null -w '%{http_code}' "$url/api/parse")" = 405 ]
  curl -sf "$url/" > /dev/null   # still alive after the error responses
  # The editor in a real browser (tests/editor.test.js; skipped unless Node and Playwright are installed).
  if command -v node > /dev/null 2>&1; then
    node tests/editor.test.js "$url" "$exe"
  fi
  kill $server
  wait $server 2>/dev/null || true
  trap - EXIT
  rm -f "$log"
else
  echo 'curl not found: skipping the --serve checks' >&2
fi

# The page's JavaScript, run in Node against real --json output (tests/web.test.js).
if command -v node > /dev/null 2>&1; then
  node tests/web.test.js "$exe"
else
  echo 'node not found: skipping the browser-view script checks' >&2
fi

# --check type-checks: every example passes except the intentional static failure.
for f in inputs/*.txt; do
  case "$f" in inputs/error_input2.txt) continue;; esac
  "$exe" --check "$f" > /dev/null
done
"$exe" --check inputs/factorial.txt | grep -q '^num: int$'
"$exe" --check inputs/gaussian_elimination.txt | grep -q '^x: floatmatrix 2,1$'
"$exe" --check inputs/factorial.txt | grep -q '^Type check passed: 5 top-level statement(s), 3 top-level variable(s)$'
"$exe" --check inputs/functions.txt | grep -q '^mean: function(floatvector 3 v) -> float$'
"$exe" --check inputs/functions.txt | grep -q '^Type check passed: 9 top-level statement(s), 1 top-level variable(s), 4 function(s)$'
if "$exe" --check inputs/error_input2.txt > /dev/null 2>&1; then
  echo 'error_input2.txt unexpectedly passed the type checker' >&2; exit 1
fi
"$exe" --check inputs/error_input2.txt 2>&1 | grep -q "^Type error at line 3, column 1: Cannot initialise 'a' of type int with a value of type bool"
for f in tests/type_errors/*.txt; do
  if "$exe" --check "$f" > /tmp/mydsl_check_out 2> /tmp/mydsl_check_err; then
    echo "Expected a type error for $f" >&2; exit 1
  fi
  [ ! -s /tmp/mydsl_check_out ] || { echo "Unexpected output for $f" >&2; exit 1; }
  grep -q '^Type error at line [0-9]*, column [0-9]*: ' /tmp/mydsl_check_err || { cat /tmp/mydsl_check_err >&2; exit 1; }
done
rm -f /tmp/mydsl_check_out /tmp/mydsl_check_err
printf 'int a := 1;\nfloat b := a;' | "$exe" --check - 2>&1 | grep -q "^Type error at line 2, column 1: Cannot initialise 'b' of type float with a value of type int"
# User-defined functions: static checks, execution (calls, recursion, early returns) and runtime errors with a call trace.
printf 'function int f(int x) { return x; };\nPrint(f(1, 2));' | "$exe" --check - 2>&1 | grep -q "^Type error at line 2, column 7: Function 'f' expects 1 argument (int x) but got 2"
printf 'function int f(int x) { Print(x); };' | "$exe" --check - 2>&1 | grep -q "^Type error at line 1, column 1: Function 'f' must return int on every path"
printf 'function int f() { return 1; };\nfunction int f() { return 2; };' | "$exe" --check - 2>&1 | grep -q "^Type error at line 2, column 1: Function 'f' is already defined (at line 1, column 1)"
printf 'return 1;' | "$exe" --ast - 2>&1 | grep -q "^Parse error at line 1, column 1: 'return' can only be used inside a function body"
[ "$("$exe" inputs/functions.txt | tr '\n' ' ')" = 'Int(49) Int(120) Float(3.0) Bool(true) ' ]
[ "$("$exe" inputs/fibonacci.txt | tail -2 | tr '\n' ' ')" = 'Bool(true) Int(2880067194370816120) ' ]
[ "$("$exe" inputs/gcd.txt | tr '\n' ' ')" = 'Int(6) Bool(true) Int(12) Bool(true) Bool(false) ' ]
[ "$("$exe" inputs/search.txt | tr '\n' ' ')" = 'Int(2) Int(-1) Bool(true) Bool(false) Int(0) Int(5) Int(10) ' ]
[ "$("$exe" inputs/vector_functions.txt | head -1)" = 'FloatVector[0.6, 0.0, 0.8]' ]
[ "$(printf 'function int f(int x) { return x; };\nPrint(6 * 7);' | "$exe" -)" = 'Int(42)' ]
# Strings: literals, escapes, concatenation and comparison through every mode.
[ "$("$exe" inputs/strings.txt | head -1)" = 'String("Hello MyDSL!")' ]
[ "$("$exe" inputs/strings.txt | sed -n 6p)" = 'String("quote \" backslash \\ tab \t newline \n")' ]
[ "$(printf 'string s := "a" + "b"; Print(s == "ab"); Print(s);' | "$exe" - | tr '\n' ' ')" = 'Bool(true) String("ab") ' ]
[ "$(printf 'Print("");' | "$exe" -)" = 'String("")' ]
printf 'string s := "x";' | "$exe" --tokens - | grep -q '^1:13 string "x"$'
printf 'Print("a\\nb");' | "$exe" --tokens - | grep -q '^1:7 string "a\\nb"$'
printf 'string s := "a\\tb";' | "$exe" --ast - | grep -q '^(declare string s (string "a\\tb"))$'
printf 'string s := "a"; Print(s + "b");' | "$exe" --check - | grep -q '^s: string$'
printf 'string s := "a"; Print(s + "b");' | "$exe" --json - | grep -q '"op":"binary","name":"+","type":"string"'
printf 'Print("");' | "$exe" --json - | grep -q '"op":"string","name":"","type":"string"'
printf 'Print("a\\"b");' | "$exe" --run-json - | grep -q '^{"output":"String(\\"a\\\\\\"b\\")\\n","error":null'
printf 'string s := "a" + 1;' | "$exe" --check - 2>&1 | grep -q "^Type error at line 1, column 17: Operator '+' cannot be applied to string and int (MyDSL does not convert between string and other types)"
printf 'int n := "1";' | "$exe" --check - 2>&1 | grep -q "^Type error at line 1, column 1: Cannot initialise 'n' of type int with a value of type string"
printf 'Print("bad \\q");' | "$exe" --tokens - 2>&1 | grep -q "^Lexing error at line 1, column 12: Unknown escape sequence '\\\\q' in string literal"
printf 'Print("open);' | "$exe" - 2>&1 | grep -q "^Lexing error at line 1, column 7: Unterminated string literal"
# String builtins and string input (stdin, --input FILE and Input("file")).
[ "$("$exe" inputs/string_functions.txt | tr '\n' ' ')" = 'Int(5) String("eve") Bool(true) String("desserts") Bool(true) Bool(false) String("    42") String("12 squared is 144") String("2.5 true") ' ]
[ "$("$exe" --input tests/data/names.txt inputs/string_input.txt | tr '\n' ' ')" = 'String("Hello, Ada Lovelace and Alan Turing!") Int(12) String("Alan") Bool(true) Int(3) ' ]
printf 'string s := Input(); int n := Input(); Print(substring(s, 0, n) + to_string(length(s)));' > /tmp/mydsl_prog.txt
[ "$(printf '"hello world" 5' | "$exe" /tmp/mydsl_prog.txt)" = 'String("hello11")' ]
[ "$(printf 'string s := Input("tests/data/names.txt"); Print(length(s));' | "$exe" -)" = 'Int(12)' ]
if printf 'hello' | "$exe" /tmp/mydsl_prog.txt > /tmp/mydsl_run_out 2> /tmp/mydsl_run_err; then echo 'bare text should not be accepted as a string value' >&2; exit 1; fi
[ ! -s /tmp/mydsl_run_out ]
grep -q "^Runtime error at line 1, column 13: Invalid data in standard input at line 1, column 1: expected a string in double quotes (e.g. \"text\"), not bare text but found 'hello'" /tmp/mydsl_run_err
printf '' | "$exe" /tmp/mydsl_prog.txt 2>&1 | grep -q '^Runtime error at line 1, column 13: Unexpected end of standard input: expected a string in double quotes'
printf 'string s := Input("missing.txt");' | "$exe" - 2>&1 | grep -q "^Runtime error at line 1, column 13: Cannot open input file 'missing.txt'"
printf 'Print(substring("abc", 2, 2));' | "$exe" - 2>&1 | grep -q '^Runtime error at line 1, column 7: substring: start 2 + count 2 exceeds the length 3 of the string'
printf 'Print(length(1));' | "$exe" --check - 2>&1 | grep -q '^Type error at line 1, column 7: length expects a string but got int'
printf 'Print(to_string([1]));' | "$exe" --check - 2>&1 | grep -q '^Type error at line 1, column 7: to_string expects an int, float, bool or string but got intvector 1'
printf 'string s := Input(); Print(length(s));' | "$exe" --json - | grep -q '"op":"call","name":"length","type":"int"'
printf '"a b"' > /tmp/mydsl_data.txt
"$exe" --run-json --input /tmp/mydsl_data.txt /tmp/mydsl_prog.txt 2>/dev/null | grep -q 'Unexpected end of standard input: expected an int'
printf 'string s := Input(); Print(s);' > /tmp/mydsl_prog.txt
"$exe" --run-json --input /tmp/mydsl_data.txt /tmp/mydsl_prog.txt | grep -q '^{"output":"String(\\"a b\\")\\n","error":null'
rm -f /tmp/mydsl_prog.txt /tmp/mydsl_data.txt /tmp/mydsl_run_out /tmp/mydsl_run_err
[ "$(printf 'function int fact(int n) { if (n <= 1) then { return 1; }; return n * fact(n - 1); }; Print(fact(5));' | "$exe" -)" = 'Int(120)' ]
if printf 'function int half(int x) {\n  return 10 / x;\n};\nPrint(half(5));\nPrint(half(0));' | "$exe" - > /tmp/mydsl_fn_out 2> /tmp/mydsl_fn_err; then echo 'division by zero inside a function should fail' >&2; exit 1; fi
[ "$(cat /tmp/mydsl_fn_out)" = 'Int(2)' ]
grep -q "^Runtime error at line 2, column 13: Division by zero: 10 / 0 (in function 'half' called from line 5, column 7)$" /tmp/mydsl_fn_err
printf 'function int f(int n) { return f(n + 1); };\nPrint(f(0));' | "$exe" - 2>&1 | grep -q "^Runtime error at line 1, column 32: Call depth limit of 1000 exceeded while calling 'f'"
printf 'function int f(int n) { return f(n + 1); }; Print(f(0));' | "$exe" --run-json - | grep -q '^{"output":"","error":{"stage":"Runtime","line":1,"column":32,"message":"Call depth limit of 1000 exceeded'
# --run-json carries the structured call trace and the variable inspector data (identical through /api/run, see e2e.sh).
printf 'function int f(int n) { return f(n + 1); }; int a := 5; Print(f(0));' | "$exe" --run-json - | grep -q '"trace":\[{"function":"f","line":1,"column":32,"repeats":999},{"function":"f","line":1,"column":63,"repeats":1}\],"omitted":0},"variables":\[{"name":"a","type":"int","value":"Int(5)"}\]'
printf 'int a := 1; a := a * 7; string s := "x" + "y"; intvector 2 v := [1, 2]; v[1] := 8;' | "$exe" --run-json - | grep -q '"variables":\[{"name":"a","type":"int","value":"Int(7)"},{"name":"s","type":"string","value":"String(\\"xy\\")"},{"name":"v","type":"intvector 2","value":"IntVector\[1, 8\]"}\]'
printf 'int a := 1; int b := 1 / 0; a := 2;' | "$exe" --run-json - | grep -q '"variables":\[{"name":"a","type":"int","value":"Int(1)"}\]'
rm -f /tmp/mydsl_fn_out /tmp/mydsl_fn_err
printf 'int a := 1 + 2;' | "$exe" --json - | grep -q '"op":"binary","name":"+","type":"int"'
printf 'int a := true;' | "$exe" --json - | grep -q '"error":{"stage":"Type","line":1,"column":1,'
"$exe" --help | grep -q -- '--ast'
"$exe" --help | grep -q -- '--serve'
"$exe" --help | grep -q -- '--tokens'
echo 'Parser, type checker, interpreter and browser-view CLI checks passed'

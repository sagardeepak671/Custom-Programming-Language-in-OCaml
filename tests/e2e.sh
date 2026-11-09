#!/bin/sh
# End-to-end checks. Run from the project directory: sh tests/e2e.sh ./mydsl
#
# Every tests/e2e/NAME.mydsl is run through the command line with NAME.in as standard input
# (if present) and must produce exactly NAME.out on stdout and, if NAME.err exists, exactly that
# error line on stderr with exit status 1 (otherwise exit status 0 and no stderr). The expected
# files were written by hand, independently of the implementation.
#
# The same programs are then sent to the browser endpoints of `mydsl --serve`: /api/run must
# return the same JSON as `mydsl --run-json` (timing removed) and /api/parse the same JSON as
# `mydsl --json`, so the browser view and the command line cannot disagree.
set -eu
exe=${1:-./mydsl}
failures=0
cases=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"; [ -z "${server:-}" ] || kill "$server" 2>/dev/null' EXIT

report() { echo "FAIL: $1" >&2; failures=$((failures + 1)); }

for prog in tests/e2e/*.mydsl; do
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
  if ! cmp -s "$tmp/out" "$expected_out"; then
    report "$prog: stdout differs from $name.out"
    diff "$expected_out" "$tmp/out" >&2 || true
  fi
  if [ -f "$name.err" ]; then
    [ "$status" -eq 1 ] || report "$prog: exit status $status, expected 1"
    cmp -s "$tmp/err" "$name.err" || { report "$prog: stderr differs from $name.err"; diff "$name.err" "$tmp/err" >&2 || true; }
  else
    [ "$status" -eq 0 ] || report "$prog: exit status $status, expected 0: $(cat "$tmp/err")"
    [ ! -s "$tmp/err" ] || report "$prog: unexpected stderr: $(cat "$tmp/err")"
  fi
done

# Browser endpoints against the command line.
if command -v curl > /dev/null 2>&1; then
  "$exe" --serve 0 2> "$tmp/server.log" &
  server=$!
  port=''
  for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    port=$(sed -n 's/.*http:\/\/127\.0\.0\.1:\([0-9]*\)\/.*/\1/p' "$tmp/server.log" 2>/dev/null || true) # the log may not exist yet
    [ -n "$port" ] && break
    sleep 0.25
  done
  [ -n "$port" ] || { echo 'Server did not start' >&2; exit 1; }
  for prog in tests/e2e/*.mydsl; do
    name=${prog%.mydsl}
    input=/dev/null
    [ -f "$name.in" ] && input="$name.in"
    # /api/run body: "<source byte count>\n" + source + standard input.
    { printf '%s\n' "$(wc -c < "$prog" | tr -d ' ')"; cat "$prog" "$input"; } > "$tmp/body"
    curl -sf -X POST --data-binary @"$tmp/body" "http://127.0.0.1:$port/api/run" | sed 's/,"millis":[0-9]*}$/}/' > "$tmp/server_run"
    "$exe" --run-json --input "$input" "$prog" | sed 's/,"millis":[0-9]*}$/}/' > "$tmp/cli_run"
    cmp -s "$tmp/server_run" "$tmp/cli_run" || { report "$prog: /api/run differs from --run-json"; diff "$tmp/cli_run" "$tmp/server_run" >&2 || true; }
    curl -sf -X POST --data-binary @"$prog" "http://127.0.0.1:$port/api/parse" > "$tmp/server_parse"
    "$exe" --json "$prog" > "$tmp/cli_parse"
    cmp -s "$tmp/server_parse" "$tmp/cli_parse" || report "$prog: /api/parse differs from --json"
    # The JSON must carry the same output/error as the command line did.
    if [ -f "$name.err" ]; then
      stage=$(sed 's/ error at line \([0-9]*\), column \([0-9]*\): .*/ \1 \2/' "$name.err")
      set -- $stage
      grep -q "\"error\":{\"stage\":\"$1\",\"line\":$2,\"column\":$3," "$tmp/server_run" || report "$prog: /api/run error stage/location differs from $name.err"
    else
      grep -q '"error":null' "$tmp/server_run" || report "$prog: /api/run reported an error"
    fi
  done
  kill "$server"
  wait "$server" 2>/dev/null || true
  server=''
else
  echo 'curl not found: skipping the browser endpoint comparison' >&2
fi

if [ "$failures" -ne 0 ]; then
  echo "$failures end-to-end failure(s) in $cases programs" >&2
  exit 1
fi
echo "End-to-end checks passed: $cases programs, command line and browser endpoints agree"

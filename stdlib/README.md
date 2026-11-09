# MyDSL standard library

Reusable functions written in MyDSL itself, loaded with the module system (README "Modules").
Nothing here is built into the interpreter: every function is ordinary MyDSL code that you can
read, copy or change.

```text
import "stdlib/math.mydsl";       // int arithmetic: square, power, gcd, factorial, is_prime, ...
import "stdlib/fmath.mydsl";      // float arithmetic: sqrt_f, exp_f, ln_f, sin_f, floor_f, pi, ...
import "stdlib/strings.mydsl";    // text: trim, index_of, replace_all, pad_left, parse_int, ...
import "stdlib/all.mydsl";        // all three
```

The path in the import is relative to the file that contains it (or to the working directory
for a program typed into the browser page or piped into `./mydsl -`): from the project
directory a program in `my/program.mydsl` writes `import "../stdlib/math.mydsl";`, and the
examples in `examples/` write `import "../math.mydsl";`. The three modules are independent;
importing `all.mydsl` or any combination is fine.

```sh
./mydsl stdlib/examples/number_facts.mydsl      # int helpers
./mydsl stdlib/examples/geometry.mydsl          # float helpers
./mydsl stdlib/examples/text.mydsl              # string helpers
printf '7 2.5' | ./mydsl stdlib/examples/report.mydsl   # all of them, with Input()
./mydsl --modules stdlib/all.mydsl              # the modules and every function they define
./mydsl --check stdlib/examples/geometry.mydsl  # the signatures of the imported functions
```

## Conventions

- **Names.** MyDSL has no overloading and one function namespace per program, so float
  versions carry the suffix `_f` (`min` and `min_f`, `sqrt_f`), and no library name repeats a
  builtin (`abs`, `length`, `substring`, `to_string`, ...) or a keyword. A program that
  defines a function with a library name gets the usual *Function 'min' is already defined (at
  line 16, column 1 of 'stdlib/math.mydsl')* error - rename yours or import less.
- **Invalid arguments.** A function given an argument outside its domain (`factorial(-1)`,
  `sqrt_f(-4.0)`, `parse_int("12a")`, `clamp(1, 5, 2)`) stops the program with
  `error(...)`: the message names the function, the rule and the offending value, and the
  error is reported with the library file, the call trace back to your call and the chain of
  imports, e.g.
  `Runtime error in 'stdlib/math.mydsl' at line 83, column 23: factorial: the argument must
  not be negative, but n is -1 (in function 'factorial' called from line 3, column 7)
  (imported from the main program at line 1, column 1)`. Results that do not fit are rejected
  the same way (`factorial: 21! does not fit in a 64-bit int (the largest is 20!)`,
  `exp_f: exp(710.0) does not fit in a float (the argument must be at most 709)`); overflow in
  the middle of a computation (`power(3, 40)`, `parse_int("9223372036854775808")`) is reported
  by the language itself. Check first if you would rather not stop: `is_integer_text(s)`
  before `parse_int(s)`, `x >= 0.0` before `sqrt_f(x)`, `n <= 20` before `factorial(n)`.
- **Accuracy.** `sqrt_f` is within 1 ulp of the correctly rounded result; `exp_f`, `ln_f`,
  `pow_f`, `cbrt_f` and `hypot_f` within about 1e-14 relative; `sin_f`/`cos_f` within about
  1e-15 absolute for |x| up to 1e6 (the error grows with |x|; angles beyond 1e9 are rejected). Printing rounds to 15 significant digits, so
  `Print(sqrt_f(2.0))` shows `Float(1.41421356237309)` while `Print(pow_f(2.0, 0.5))` may show
  a last digit off; compare floats with `approx_equal_f`, not `==`.
- **Cost.** Everything runs in the tree-walking interpreter: `is_prime` tries divisors up to
  the square root (fine up to about 1e12, hopeless near 1e18), string functions copy
  character by character, `repeat` builds its result one piece at a time.

## math.mydsl (int)

| Function | Result |
| --- | --- |
| `square(x)`, `cube(x)` | x², x³ |
| `min(a, b)`, `max(a, b)` | the smaller / larger of two ints |
| `clamp(x, low, high)` | x limited to low..high (low <= high) |
| `sign(x)` | -1, 0 or 1 |
| `is_even(x)`, `is_odd(x)` | parity (also for negative x) |
| `power(base, exponent)` | base^exponent for exponent >= 0 (`power(0, 0)` is 1); overflow is an error |
| `gcd(a, b)`, `lcm(a, b)` | greatest common divisor / least common multiple, never negative; `gcd(0, 0)` and `lcm(0, n)` are 0 (a result of 2^63, such as `gcd(-9223372036854775808, 0)`, does not fit and is an overflow error) |
| `factorial(n)` | n! for 0 <= n <= 20 |
| `fibonacci(n)` | the n-th Fibonacci number (0, 1, 1, 2, ...) for 0 <= n <= 92 |
| `isqrt(n)` | the largest s with s·s <= n, for n >= 0 |
| `div_floor(a, b)`, `mod_floor(a, b)` | division rounding towards minus infinity and its remainder (Python's `//` and `%`), unlike `/` and `%` which truncate: `div_floor(-7, 2)` is -4, `mod_floor(-7, 2)` is 1; a divisor of 0 is rejected |
| `is_prime(n)` | primality by trial division (n < 2 is not prime) |
| `binomial(n, k)` | n choose k for 0 <= k <= n, exact for every result that fits in an int (up to `binomial(66, 33)`) |
| `digit_count(n)`, `digit_sum(n)` | decimal digits of \|n\| (0 has one digit) and their sum |

## fmath.mydsl (float)

| Function | Result |
| --- | --- |
| `pi()`, `euler()` | π and e (MyDSL has no constants, so they are functions) |
| `square_f(x)`, `cube_f(x)` | x², x³ |
| `min_f(a, b)`, `max_f(a, b)`, `clamp_f(x, low, high)`, `sign_f(x)` | as the int versions |
| `floor_f(x)`, `ceil_f(x)`, `round_f(x)`, `fract_f(x)`, `is_integer_f(x)` | rounding (`round_f` rounds halves away from zero: `round_f(2.5)` is 3.0, `round_f(-2.5)` is -3.0); `fract_f(x)` is x - floor_f(x) in [0, 1); `has_no_fraction_f(x)` tells whether \|x\| >= 2^52, where every float is an integer already |
| `lerp_f(a, b, t)` | a + (b - a)·t |
| `approx_equal_f(a, b, tolerance)` | \|a - b\| <= tolerance |
| `sqrt_f(x)` | square root of x >= 0 (Newton's method, 1 ulp) |
| `cbrt_f(x)` | cube root, any sign |
| `hypot_f(a, b)` | sqrt(a² + b²) without overflow for large a, b |
| `power_f(base, exponent)` | base raised to an int exponent of any sign by repeated squaring: exact when the result is (`power_f(2.0, -3)`), otherwise off by about \|exponent\| × 1e-16 relative; `power_f(0.0, -1)` divides by zero |
| `exp_f(x)` | eˣ for x <= 709; 0.0 below about -708 |
| `ln_f(x)`, `log2_f(x)`, `log10_f(x)` | logarithms of x > 0 |
| `pow_f(base, exponent)` | base^exponent as exp(exponent · ln base), off by about \|exponent · ln base\| × 1e-16 relative; base > 0, or base == 0.0 with exponent >= 0.0 (negative bases: use `power_f`) |
| `sin_f(x)`, `cos_f(x)`, `tan_f(x)` | trigonometry in radians for \|x\| up to 1e9 (accurate to about 1e-15 up to 1e6, to about 1e-7 at 1e9; larger angles are rejected, they cannot be reduced meaningfully); `reduce_angle_f(x)` is the reduction to [-π, π] they use |
| `degrees_f(radians)`, `radians_f(degrees)` | angle conversion |

## strings.mydsl

Positions and lengths count bytes (like `length` and `substring`); letters are ASCII.

| Function | Result |
| --- | --- |
| `char_at(s, i)` | the one-character string at position i (0 <= i < length(s)) |
| `quote(s)` | s written as a MyDSL string literal with `\" \\ \n \t \r` escapes, as `Print` shows it: for messages (`escape_char(c)` escapes one character) |
| `repeat(s, n)` | s written n >= 0 times |
| `reverse(s)` | the characters in reverse order |
| `starts_with(s, prefix)`, `ends_with(s, suffix)`, `contains(s, part)` | tests (the empty string is a prefix, suffix and part of every string) |
| `index_of(s, part)`, `last_index_of(s, part)` | first / last position of part, or -1 |
| `count_of(s, part)` | non-overlapping occurrences of a non-empty part |
| `replace_all(s, from, to)` | every occurrence of the non-empty `from` replaced, left to right |
| `pad_left(s, width, fill)`, `pad_right(s, width, fill)` | s extended to width characters with the one-character `fill` (never shortened) |
| `trim(s)`, `trim_left(s)`, `trim_right(s)` | without spaces, tabs, newlines and carriage returns at the ends |
| `is_whitespace(c)`, `is_digit(c)`, `is_letter(c)` | tests of a one-character string (false for any other length) |
| `to_upper(s)`, `to_lower(s)` | ASCII letters converted, everything else kept |
| `is_integer_text(s)` | whether s is an optional `-` followed by at least one digit and nothing else |
| `parse_int(s)` | the int written in s (`is_integer_text(s)` must hold; 64-bit overflow is an error) |

## Writing a library of your own

`examples/layered/temperature.mydsl` is the pattern: a file of function definitions that
imports what it needs relative to itself (`import "../../fmath.mydsl";`), validates its
arguments with `error(...)` and offers a predicate (`is_valid_celsius`) so that callers can
check before calling. `examples/layered/main.mydsl` imports it next to the standard library;
a module reached twice (here `fmath.mydsl`) is loaded once (`./mydsl --modules
stdlib/examples/layered/main.mydsl`). `examples/validation.mydsl` shows `error` in a program.

## Tests

`sh tests/stdlib.sh ./mydsl` (part of `make test`) runs the examples (including the layered
one and the failing `validation.mydsl`) and `tests/stdlib/` (edge cases and every
invalid-argument and overflow path, with hand-written expected output and error lines), checks
the error messages through the command line and `--run-json` (what the browser uses), that
every module loads on its own, that no function is named like a builtin or keyword and that
every function is listed here. `tests/tests.cpp`
(`stdlib_tests`) compares the numeric functions with the C library at full precision over
thousands of arguments, and the int and string functions with reference implementations.

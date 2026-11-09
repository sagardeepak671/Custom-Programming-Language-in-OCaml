// Tree-walking interpreter for type-checked programs. The rules are summarised in README.md
// ("Execution rules"); in short:
//
//   * int is 64-bit and checked: overflow in + - * / unary - abs, the for-loop step and every
//     element-wise / dot-product / matrix-product / determinant step on ints is a runtime error,
//     as is division or modulo by zero (also for floats). Division truncates towards zero and %
//     takes the sign of the dividend, as in C++. A float operation whose result is not finite
//     is a "Float overflow" / "Numerical error" runtime error, so inf and nan never appear.
//   * && and || short-circuit; every other operator evaluates both operands.
//   * Scoping mirrors the type checker: a block/if/while/for body is a fresh scope each time it
//     runs, declarations go to the innermost scope, assignments update the nearest declaration.
//   * for (i := a, c, s): i is a real variable of the loop scope; after each iteration
//     i := i + s with the current i (so the body can change it), and s is re-evaluated each time.
//   * Vectors and matrices are row-major arrays; indices are bounds-checked at run time.
//     abs(v) is the Euclidean length, angle_vec the angle in radians (undefined for a zero
//     vector), inv_vec reverses the order of the elements, determinant uses exact (Bareiss)
//     arithmetic for int matrices and LU with partial pivoting for float matrices, inverse_mat
//     uses Gauss-Jordan elimination with partial pivoting and reports singular matrices.
//   * Input() reads the next value in literal syntax (4, 2.5, true, "text", [1., 2.],
//     [[1, 2], [3, 4]]) from standard input, Input("file") from the file (opened once per run
//     and read sequentially, relative to the working directory). The value must match the
//     target type and size exactly, except that a float may be written without a fraction. A
//     string value is a double-quoted literal with the source escapes, so it may contain
//     spaces and sit on a line with other values; whitespace, line breaks and comments between
//     values are skipped.
//   * Strings: a string literal evaluates to its (unescaped) text, + concatenates two strings
//     (the result may not exceed kMaxStringLength characters, so s := s + s cannot exhaust
//     memory), == and != compare the characters exactly. Print writes String("...") with the
//     literal escapes, so the output stays one line per value. length(s) counts bytes,
//     substring(s, start, count) takes `count` bytes from 0-based `start` and fails when the
//     range leaves the string, to_string(x) gives a scalar's literal text.
//   * Print writes Value::str() and a newline to the output stream. error(message) stops the
//     program with a runtime error carrying the message text.
//   * User-defined functions: definitions are collected before execution starts (so a call may
//     precede its definition). A call evaluates the arguments left to right in the caller's
//     scope, binds them by value to the parameters in a fresh environment with no parent (the
//     body sees its parameters and locals only, never top-level variables - as in the type
//     checker), runs the body and yields the value of the first 'return' reached, unwinding any
//     enclosing blocks and loops. Calls nest and recurse; kMaxCallDepth active calls is the
//     limit ("Call depth limit ... exceeded"), and a runtime error inside a body carries a
//     short trace of the active calls ("in function 'f' called from line L, column C").
//   * Modules (README "Modules"): the functions of every loaded module are registered before
//     execution; only the main program's top-level statements run. A call into an imported
//     function switches the "current module": a runtime error there is reported with that
//     file (and the chain of imports that led to it), and every trace frame names the file of
//     its call site (the main program's frames have no file, as before).
#include "mydsl.hpp"
#include "environment.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>

#include <pthread.h>

namespace mydsl {
namespace {
using Clock = std::chrono::steady_clock;

[[noreturn]] void fail(Location at, const std::string& message) { throw Error("Runtime", at, message); }

// Limit on simultaneously active user-defined function calls (the user-facing recursion limit,
// like Python's): deep enough for ordinary recursion and reported with a clear message.
constexpr std::size_t kMaxCallDepth = 1000;
// Safety net for the C++ stack, counting nested execute()/evaluate() activations: every MyDSL
// call level costs a handful, every nested block/loop/expression one more. The interpreter runs
// on a thread with a kStackBytes stack (see interpret()); one activation costs at most ~2 KB
// even under the sanitizers, so 20000 of them need < 40 MB. Only contrived programs (hundreds
// of nested blocks inside deep recursion) reach it; plain recursion hits kMaxCallDepth first.
constexpr std::size_t kMaxNesting = 20000;
constexpr std::size_t kStackBytes = 128u << 20; // reserved lazily; only the pages used are committed
// How many call levels a runtime error inside a function names.
constexpr std::size_t kTraceFrames = 5;
// Longest string value (characters) that + may produce.
constexpr std::size_t kMaxStringLength = 1u << 20;

// The recursive path through the interpreter is evaluate() -> call()/invoke() -> execute() ->
// block() -> ... -> evaluate(). Those dispatchers hold no values themselves; the work (and the
// Value locals) lives in per-node helpers that must NOT be inlined into them, otherwise the
// compiler merges all helper locals into one large dispatcher frame and deep MyDSL recursion
// needs many times more C++ stack (measured: ~10x under the sanitizers).
#define MYDSL_NOINLINE __attribute__((noinline))

// A runtime error that already carries its call trace (so outer calls do not add another).
struct TracedError : Error { using Error::Error; };

std::string show_int(std::int64_t n) { return std::to_string(n); }

std::string show_float(double x) {
    std::ostringstream out;
    out.precision(15);
    out << x;
    return out.str();
}

// ----- checked integer arithmetic -----------------------------------------------------

std::int64_t checked(char op, std::int64_t a, std::int64_t b, Location at, const std::string& context = "") {
    std::int64_t result = 0;
    bool overflow = false;
    switch (op) {
    case '+': overflow = __builtin_add_overflow(a, b, &result); break;
    case '-': overflow = __builtin_sub_overflow(a, b, &result); break;
    case '*': overflow = __builtin_mul_overflow(a, b, &result); break;
    case '/':
        if (b == 0) fail(at, "Division by zero: " + show_int(a) + " / 0" + context);
        if (a == std::numeric_limits<std::int64_t>::min() && b == -1) overflow = true;
        else result = a / b;
        break;
    case '%':
        if (b == 0) fail(at, "Modulo by zero: " + show_int(a) + " % 0" + context);
        if (a == std::numeric_limits<std::int64_t>::min() && b == -1) result = 0;
        else result = a % b;
        break;
    }
    if (overflow)
        fail(at, "Integer overflow: " + show_int(a) + " " + op + " " + show_int(b) + " does not fit in a 64-bit int (range " +
                     show_int(std::numeric_limits<std::int64_t>::min()) + " .. " + show_int(std::numeric_limits<std::int64_t>::max()) + ")" + context);
    return result;
}

double finite(double x, Location at, const std::string& what) {
    if (!std::isfinite(x)) fail(at, "Numerical error: " + what + " is not a finite number");
    return x;
}

// ----- runtime input ------------------------------------------------------------------

// Reads values in literal syntax from one source (standard input or a data file). The whole
// source is read and tokenized with the language lexer on first use, then consumed in order.
class InputSource {
    std::string origin_;      // for messages: "standard input" or "file 'x'"
    std::istream* stream_;    // standard input, or null for a file
    std::string path_;
    bool loaded_ = false;
    std::vector<Token> tokens_;
    std::size_t position_ = 0;

    void load(Location at) {
        if (loaded_) return;
        loaded_ = true;
        std::string text;
        if (stream_) {
            text.assign(std::istreambuf_iterator<char>(*stream_), {});
        } else {
            std::ifstream file(path_);
            if (!file) fail(at, "Cannot open input file '" + path_ + "' (paths are relative to the working directory)");
            text.assign(std::istreambuf_iterator<char>(file), {});
        }
        try {
            tokens_ = lex(text);
        } catch (const Error& e) {
            fail(at, "Invalid data in " + origin_ + " at line " + std::to_string(e.location.line) + ", column " +
                         std::to_string(e.location.column) + ": " + e.message);
        }
    }

    const Token& current() const { return tokens_[std::min(position_, tokens_.size() - 1)]; }

    [[noreturn]] void bad(Location at, const std::string& expected) {
        const Token& t = current();
        if (t.kind == TokenKind::End)
            fail(at, "Unexpected end of " + origin_ + ": expected " + expected);
        const std::string found = t.kind == TokenKind::String ? "the string " + quote_string(t.text) : "'" + t.text + "'";
        fail(at, "Invalid data in " + origin_ + " at line " + std::to_string(t.location.line) + ", column " +
                     std::to_string(t.location.column) + ": expected " + expected + " but found " + found);
    }

    bool accept(const std::string& symbol) {
        if (current().kind == TokenKind::Symbol && current().text == symbol) { ++position_; return true; }
        return false;
    }

    void element(Base base, Value& into, std::size_t index, Location at) {
        const bool negative = accept("-");
        const Token& t = current();
        if (base == Base::Bool) {
            if (negative || t.kind != TokenKind::Word || (t.text != "true" && t.text != "false")) bad(at, "true or false");
            into.ints[index] = t.text == "true";
        } else if (base == Base::Int) {
            if (t.kind != TokenKind::Integer) bad(at, std::string("an int") + (t.kind == TokenKind::Float ? " (a float was given)" : ""));
            try { into.ints[index] = std::stoll(t.text); } catch (const std::out_of_range&) { bad(at, "an int that fits in 64 bits"); }
            if (negative) into.ints[index] = -into.ints[index];
        } else {
            if (t.kind != TokenKind::Integer && t.kind != TokenKind::Float) bad(at, "a number");
            const double x = std::strtod(t.text.c_str(), nullptr); // overflow yields inf, caught below
            if (!std::isfinite(x)) bad(at, "a finite number");
            into.floats[index] = negative ? -x : x;
        }
        ++position_;
    }

public:
    InputSource(std::istream& stream) : origin_("standard input"), stream_(&stream) {}
    explicit InputSource(std::string path) : origin_("file '" + path + "'"), stream_(nullptr), path_(std::move(path)) {}

    Value read(const Type& type, Location at) {
        load(at);
        Value value = Value::zeros(type);
        const std::string expected = "a value of type " + type.str();
        if (type.base == Base::String) {
            const Token& t = current();
            if (t.kind != TokenKind::String)
                bad(at, std::string("a string in double quotes (e.g. \"text\")") +
                            (t.kind == TokenKind::Word || t.kind == TokenKind::Integer || t.kind == TokenKind::Float ? ", not bare text" : ""));
            value.text = t.text;
            ++position_;
            return value;
        }
        if (type.rank == 0) {
            element(type.base, value, 0, at);
            return value;
        }
        if (!accept("[")) bad(at, expected + " starting with '['");
        if (type.rank == 1) {
            for (std::size_t i = 0; i < type.rows; ++i) {
                if (i > 0 && !accept(",")) bad(at, "',' (the vector needs " + std::to_string(type.rows) + " elements)");
                element(type.base, value, i, at);
            }
            if (!accept("]")) bad(at, "']' after " + std::to_string(type.rows) + " elements");
            return value;
        }
        for (std::size_t r = 0; r < type.rows; ++r) {
            if (r > 0 && !accept(",")) bad(at, "',' (the matrix needs " + std::to_string(type.rows) + " rows)");
            if (!accept("[")) bad(at, "'[' starting row " + std::to_string(r + 1));
            for (std::size_t c = 0; c < type.cols; ++c) {
                if (c > 0 && !accept(",")) bad(at, "',' (each row needs " + std::to_string(type.cols) + " elements)");
                element(type.base, value, r * type.cols + c, at);
            }
            if (!accept("]")) bad(at, "']' after " + std::to_string(type.cols) + " elements of row " + std::to_string(r + 1));
        }
        if (!accept("]")) bad(at, "']' after " + std::to_string(type.rows) + " rows");
        return value;
    }
};

// ----- the interpreter ----------------------------------------------------------------

class Interpreter {
    std::ostream& output_;
    Limits limits_;
    Clock::time_point started_ = Clock::now();
    std::size_t written_ = 0;
    unsigned tick_counter_ = 0;
    std::optional<Location> loop_; // innermost loop being executed, reported by the time limit
    InputSource stdin_;
    std::map<std::string, std::unique_ptr<InputSource>> files_;
    Environment<Value> globals_;
    Environment<Value>* scope_ = &globals_;
    const Modules& modules_;
    struct Function { Statement definition; std::size_t module; };
    std::map<std::string, Function> functions_; // top-level "function" statements of every module, by name
    std::size_t module_;                        // the module whose code is executing (initially the main program)
    // Set by a return statement; blocks and loops stop as soon as it is set, and the active
    // call takes the value and clears it.
    std::optional<Value> returning_;
    struct Frame { const Stmt* function; Location call_site; std::size_t caller; }; // `caller`: module of the call site
    std::vector<Frame> calls_; // active user-defined function calls, outermost first

    // Error::file for code of module `m` ("" for the main program).
    std::string file_of(std::size_t m) const { const Module& module = modules_.modules.at(m); return module.main ? "" : module.display; }
    std::size_t nesting_ = 0;   // nested execute()/evaluate() activations (C++ stack guard)

    // Block scope: a new environment whose parent is the current scope. An `isolated` scope
    // has no parent (function bodies), so top-level variables are invisible in it.
    struct Scope {
        Interpreter& interpreter;
        Environment<Value>* previous;
        std::unique_ptr<Environment<Value>> env;
        explicit Scope(Interpreter& i, bool isolated = false)
            : interpreter(i), previous(i.scope_), env(std::make_unique<Environment<Value>>(isolated ? nullptr : i.scope_)) { i.scope_ = env.get(); }
        ~Scope() { interpreter.scope_ = previous; }
    };

    struct Nesting {
        Interpreter& interpreter;
        Nesting(Interpreter& i, Location at) : interpreter(i) { if (++i.nesting_ > kMaxNesting) nesting_exceeded(at); }
        ~Nesting() { --interpreter.nesting_; }
    };

    [[noreturn]] MYDSL_NOINLINE static void nesting_exceeded(Location at) {
        fail(at, "Evaluation nesting limit of " + std::to_string(kMaxNesting) +
                     " exceeded (statements and expressions are nested too deeply, probably through recursion)");
    }

    // Called once per statement and loop iteration: enforces the time limit cheaply.
    void tick(Location at) {
        if (limits_.max_millis <= 0 || (++tick_counter_ & 255) != 0) return;
        check_time(at);
    }

    MYDSL_NOINLINE void check_time(Location at) const {
        const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - started_).count();
        if (elapsed > limits_.max_millis)
            fail(loop_.value_or(at), "Execution stopped after " + std::to_string(static_cast<long long>(limits_.max_millis)) +
                                         " ms (the time limit was exceeded; is there an infinite loop?)");
    }

    // ----- value helpers ----------------------------------------------------------------

    static bool is_int(const Value& v) { return v.type.base == Base::Int; }
    static bool is_float(const Value& v) { return v.type.base == Base::Float; }
    static std::int64_t as_int(const Value& v) { return v.ints.at(0); }
    static double as_float(const Value& v) { return v.floats.at(0); }
    static std::size_t count(const Value& v) { return v.type.size(); }
    static std::string show(const Value& v) { return v.str(); }

    static std::string element_name(const Type& t, std::size_t i) {
        if (t.rank == 2) return "element [" + std::to_string(i / t.cols) + "," + std::to_string(i % t.cols) + "]";
        return "element " + std::to_string(i);
    }

    // Element-wise int or float operation over equally shaped operands (or a scalar and a tensor).
    Value elementwise(char op, const Value& a, const Value& b, Location at) {
        const Value& shaped = a.type.rank ? a : b;
        Value result = Value::zeros(shaped.type);
        const std::size_t n = count(shaped);
        auto pick = [&](const Value& v, std::size_t i) { return v.type.rank ? i : 0; };
        for (std::size_t i = 0; i < n; ++i) {
            const std::string where = shaped.type.rank ? " (" + element_name(shaped.type, i) + ")" : "";
            if (is_int(shaped)) {
                result.ints[i] = checked(op, a.ints[pick(a, i)], b.ints[pick(b, i)], at, where);
            } else {
                const double x = a.floats[pick(a, i)], y = b.floats[pick(b, i)];
                const std::string text = Value::real(x).str() + " " + op + " " + Value::real(y).str();
                if (op == '/' && y == 0.0) fail(at, "Division by zero: " + text + where);
                const double r = op == '+' ? x + y : op == '-' ? x - y : op == '*' ? x * y : x / y;
                if (!std::isfinite(r)) fail(at, "Float overflow: " + text + where + " is not a finite number");
                result.floats[i] = r;
            }
        }
        return result;
    }

    static bool equal(const Value& a, const Value& b) {
        if (a.type.base == Base::String) return a.text == b.text;
        return is_float(a) ? a.floats == b.floats : a.ints == b.ints;
    }

    // substring(s, start, count): `count` bytes from the 0-based `start`; the range must lie
    // inside the string (start == length with count 0 gives "").
    static Value substring(const Value& s, const Value& start, const Value& count, Location at) {
        const std::int64_t length = static_cast<std::int64_t>(s.text.size()), from = as_int(start), n = as_int(count);
        if (from < 0 || from > length)
            fail(at, "substring: start " + show_int(from) + " is out of range for a string of length " + show_int(length) + " (valid starts are 0.." + show_int(length) + ")");
        if (n < 0) fail(at, "substring: count " + show_int(n) + " must not be negative");
        if (n > length - from)
            fail(at, "substring: start " + show_int(from) + " + count " + show_int(n) + " exceeds the length " + show_int(length) + " of the string (at most " +
                         show_int(length - from) + " characters are available from " + show_int(from) + ")");
        return Value::string(s.text.substr(static_cast<std::size_t>(from), static_cast<std::size_t>(n)));
    }

    static Value concatenate(const Value& a, const Value& b, Location at) {
        if (a.text.size() + b.text.size() > kMaxStringLength)
            fail(at, "String length limit of " + std::to_string(kMaxStringLength) + " characters exceeded (the result of '+' would have " +
                         std::to_string(a.text.size() + b.text.size()) + " characters)");
        return Value::string(a.text + b.text);
    }

    // ----- expressions ----------------------------------------------------------------

    // Dispatcher only: the recursive path (evaluate -> call/invoke -> execute -> evaluate ...)
    // keeps its frames small so that deep recursion in MyDSL programs stays within the C++
    // stack (see kMaxCallDepth / kMaxNesting); each node kind has its own function.
    Value evaluate(const Expression& e) {
        Nesting nesting(*this, e->location);
        const std::string& op = e->op;
        if (op == "literal") return e->literal;
        if (op == "string") return Value::string(e->name);
        if (op == "variable") return variable(e);
        if (op == "unary") return unary(e);
        if (op == "binary") return binary(e);
        if (op == "call") return call(e);
        if (op == "index") return index(e);
        if (op == "vector" || op == "matrix") return tensor(e);
        unknown(e->location, "expression", op);
    }

    [[noreturn]] MYDSL_NOINLINE static void unknown(Location at, const char* what, const std::string& op) {
        fail(at, std::string("Internal error: unknown ") + what + " '" + op + "'");
    }

    MYDSL_NOINLINE Value variable(const Expression& e) {
        if (Value* value = scope_->find(e->name)) return *value;
        fail(e->location, "Internal error: variable '" + e->name + "' has no value"); // the checker prevents this
    }

    MYDSL_NOINLINE Value tensor(const Expression& e) {
        Value result = Value::zeros(e->type);
        for (std::size_t i = 0; i < e->args.size(); ++i) {
            const Value element = evaluate(e->args[i]);
            if (is_int(result)) result.ints[i] = as_int(element); else result.floats[i] = as_float(element);
        }
        return result;
    }

    MYDSL_NOINLINE Value unary(const Expression& e) {
        const Value v = evaluate(e->args.at(0));
        if (e->name == "!") return Value::boolean(!v.truth());
        Value zero = Value::zeros(v.type.rank ? Type{v.type.base, 0, 1, 1} : v.type);
        return elementwise('-', zero, v, e->location);
    }

    MYDSL_NOINLINE Value binary(const Expression& e) {
        const std::string& op = e->name;
        const Value left = evaluate(e->args.at(0));
        if (op == "&&") return left.truth() ? Value::boolean(evaluate(e->args.at(1)).truth()) : Value::boolean(false);
        if (op == "||") return left.truth() ? Value::boolean(true) : Value::boolean(evaluate(e->args.at(1)).truth());
        const Value right = evaluate(e->args.at(1));
        if (op == "==") return Value::boolean(equal(left, right));
        if (op == "!=") return Value::boolean(!equal(left, right));
        if (left.type.base == Base::String) return concatenate(left, right, e->location); // the checker only allows +
        if (left.type.base == Base::Bool) {
            // The checker only allows + (or) and * (and) on two bools here.
            const bool a = left.truth(), b = right.truth();
            return Value::boolean(op == "+" ? (a || b) : (a && b));
        }
        if (op == "<" || op == ">" || op == "<=" || op == ">=") {
            const double a = left.number(0), b = right.number(0);
            if (is_int(left)) {
                const std::int64_t x = as_int(left), y = as_int(right);
                return Value::boolean(op == "<" ? x < y : op == ">" ? x > y : op == "<=" ? x <= y : x >= y);
            }
            return Value::boolean(op == "<" ? a < b : op == ">" ? a > b : op == "<=" ? a <= b : a >= b);
        }
        return elementwise(op[0], left, right, e->location);
    }

    std::size_t checked_index(const Value& target, const Expression& index_expr, std::size_t which, std::size_t limit) {
        const std::int64_t i = as_int(evaluate(index_expr));
        if (i < 0 || static_cast<std::size_t>(i) >= limit) {
            const char* kind = target.type.rank == 2 ? (which == 0 ? "Row index " : "Column index ") : "Index ";
            fail(index_expr->location, kind + show_int(i) + " is out of range for " + target.type.str() +
                                           " (valid indices are 0.." + std::to_string(limit - 1) + ")");
        }
        return static_cast<std::size_t>(i);
    }

    // Flat position of v[i] or m[i, j]; `indices` are the index expressions.
    template <class It> std::size_t position(const Value& target, It first, It last) {
        const std::size_t i = checked_index(target, *first, 0, target.type.rows);
        if (target.type.rank == 1) return i;
        const std::size_t j = checked_index(target, *(first + 1), 1, target.type.cols);
        (void)last;
        return i * target.type.cols + j;
    }

    MYDSL_NOINLINE Value index(const Expression& e) {
        const Value target = evaluate(e->args.at(0));
        const std::size_t at = position(target, e->args.begin() + 1, e->args.end());
        return is_int(target) ? Value::integer(target.ints[at]) : Value::real(target.floats[at]);
    }

    // ----- builtin functions ----------------------------------------------------------

    static double length(const Value& v) {
        double sum = 0;
        for (std::size_t i = 0; i < count(v); ++i) sum += v.number(i) * v.number(i);
        return std::sqrt(sum);
    }

    Value call(const Expression& e) {
        if (e->name == "Input") return input(e);
        if (auto defined = functions_.find(e->name); defined != functions_.end()) return invoke(e, defined->second.definition, defined->second.module);
        return apply_builtin(e, arguments(e));
    }

    // The argument values of a call, evaluated left to right in the current scope. Kept apart
    // from the consumers so that their (large) frames are not live while arguments that
    // contain further calls are evaluated.
    MYDSL_NOINLINE std::vector<Value> arguments(const Expression& e) {
        std::vector<Value> values;
        values.reserve(e->args.size());
        for (const auto& arg : e->args) values.push_back(evaluate(arg));
        return values;
    }

    MYDSL_NOINLINE Value apply_builtin(const Expression& e, const std::vector<Value>& args) {
        const std::string& f = e->name;
        const Location at = e->location;
        const Value& a = args.at(0);
        if (f == "abs") {
            if (a.type.rank == 1) return Value::real(finite(length(a), at, "the length of the vector"));
            if (is_float(a)) return Value::real(std::fabs(as_float(a)));
            const std::int64_t n = as_int(a);
            if (n == std::numeric_limits<std::int64_t>::min())
                fail(at, "Integer overflow: abs(" + show_int(n) + ") does not fit in a 64-bit int");
            return Value::integer(n < 0 ? -n : n);
        }
        if (f == "to_float") {
            if (is_float(a)) return a;
            Value result = Value::zeros(e->type);
            for (std::size_t i = 0; i < count(a); ++i) result.floats[i] = static_cast<double>(a.ints[i]);
            return result;
        }
        if (f == "to_int") {
            if (is_int(a)) return a;
            Value result = Value::zeros(e->type);
            for (std::size_t i = 0; i < count(a); ++i) {
                const double x = std::trunc(a.floats[i]);
                if (std::isnan(x) || x < -9223372036854775808.0 || x >= 9223372036854775808.0)
                    fail(at, "to_int(" + show_float(a.floats[i]) + ") does not fit in a 64-bit int");
                result.ints[i] = static_cast<std::int64_t>(x);
            }
            return result;
        }
        if (f == "inv_vec") {
            Value result = a;
            if (is_int(a)) std::reverse(result.ints.begin(), result.ints.end());
            else std::reverse(result.floats.begin(), result.floats.end());
            return result;
        }
        if (f == "is_zero") {
            for (std::size_t i = 0; i < count(a); ++i) if (a.number(i) != 0.0) return Value::boolean(false);
            return Value::boolean(true);
        }
        if (f == "transpose") {
            Value result = Value::zeros(e->type);
            for (std::size_t r = 0; r < a.type.rows; ++r)
                for (std::size_t c = 0; c < a.type.cols; ++c) {
                    if (is_int(a)) result.ints[c * a.type.rows + r] = a.ints[r * a.type.cols + c];
                    else result.floats[c * a.type.rows + r] = a.floats[r * a.type.cols + c];
                }
            return result;
        }
        if (f == "length") return Value::integer(static_cast<std::int64_t>(a.text.size()));
        if (f == "to_string") return Value::string(scalar_text(a));
        if (f == "substring") return substring(a, args.at(1), args.at(2), at);
        if (f == "determinant") return is_int(a) ? Value::integer(int_determinant(a, at)) : Value::real(float_determinant(a, at));
        if (f == "inverse_mat") return inverse(a, at);
        // Two-argument functions.
        const Value& b = args.at(1);
        if (f == "dotprod_vec") return dot(a, b, at);
        if (f == "angle_vec") {
            // Kahan's formula 2*atan2(|u - v|, |u + v|) on the unit vectors u, v: unlike
            // acos(dot / (|a||b|)) it is accurate for (nearly) parallel vectors, where rounding
            // in the cosine would otherwise give angles like 2e-8 instead of 0.
            const double la = finite(length(a), at, "the length of the first vector"), lb = finite(length(b), at, "the length of the second vector");
            if (la == 0.0 || lb == 0.0)
                fail(at, "angle_vec: the angle with a zero vector is undefined (" + (la == 0.0 ? show(a) : show(b)) + " has length 0)");
            double diff = 0, sum = 0;
            for (std::size_t i = 0; i < count(a); ++i) {
                const double u = a.number(i) / la, v = b.number(i) / lb;
                diff += (u - v) * (u - v);
                sum += (u + v) * (u + v);
            }
            return Value::real(2.0 * std::atan2(std::sqrt(diff), std::sqrt(sum)));
        }
        if (f == "mult_mat") return multiply(a, b, at);
        fail(at, "Internal error: unknown function '" + f + "'");
    }

    Value dot(const Value& a, const Value& b, Location at) {
        if (is_int(a)) {
            std::int64_t sum = 0;
            for (std::size_t i = 0; i < count(a); ++i)
                sum = checked('+', sum, checked('*', a.ints[i], b.ints[i], at, " in dotprod_vec"), at, " in dotprod_vec");
            return Value::integer(sum);
        }
        double sum = 0;
        for (std::size_t i = 0; i < count(a); ++i) sum += a.floats[i] * b.floats[i];
        return Value::real(finite(sum, at, "the dot product"));
    }

    Value multiply(const Value& a, const Value& b, Location at) {
        const std::size_t n = a.type.rows, k = a.type.cols, m = b.type.cols;
        Value result = Value::zeros(Type{a.type.base, 2, n, m});
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t j = 0; j < m; ++j) {
                if (is_int(a)) {
                    std::int64_t sum = 0;
                    for (std::size_t t = 0; t < k; ++t)
                        sum = checked('+', sum, checked('*', a.ints[i * k + t], b.ints[t * m + j], at, " in mult_mat"), at, " in mult_mat");
                    result.ints[i * m + j] = sum;
                } else {
                    double sum = 0;
                    for (std::size_t t = 0; t < k; ++t) sum += a.floats[i * k + t] * b.floats[t * m + j];
                    result.floats[i * m + j] = finite(sum, at, "element [" + std::to_string(i) + "," + std::to_string(j) + "] of the matrix product");
                }
            }
        return result;
    }

    // Exact integer determinant (Bareiss fraction-free elimination; every division is exact).
    std::int64_t int_determinant(const Value& a, Location at) {
        const std::size_t n = a.type.rows;
        std::vector<std::int64_t> m = a.ints;
        std::int64_t sign = 1, previous = 1;
        const std::string context = " while computing the determinant";
        for (std::size_t k = 0; k + 1 < n; ++k) {
            if (m[k * n + k] == 0) {
                std::size_t swap = k + 1;
                while (swap < n && m[swap * n + k] == 0) ++swap;
                if (swap == n) return 0;
                for (std::size_t j = 0; j < n; ++j) std::swap(m[k * n + j], m[swap * n + j]);
                sign = -sign;
            }
            for (std::size_t i = k + 1; i < n; ++i)
                for (std::size_t j = k + 1; j < n; ++j) {
                    const std::int64_t product = checked('*', m[i * n + j], m[k * n + k], at, context);
                    const std::int64_t cross = checked('*', m[i * n + k], m[k * n + j], at, context);
                    m[i * n + j] = checked('-', product, cross, at, context) / previous;
                }
            previous = m[k * n + k];
        }
        return checked('*', sign, m[(n - 1) * n + (n - 1)], at, context);
    }

    // LU decomposition with partial pivoting; the determinant is the signed product of pivots.
    double float_determinant(const Value& a, Location at) {
        const std::size_t n = a.type.rows;
        std::vector<double> m = a.floats;
        double det = 1;
        for (std::size_t k = 0; k < n; ++k) {
            std::size_t pivot = k;
            for (std::size_t i = k + 1; i < n; ++i) if (std::fabs(m[i * n + k]) > std::fabs(m[pivot * n + k])) pivot = i;
            if (m[pivot * n + k] == 0.0) return 0.0;
            if (pivot != k) { for (std::size_t j = 0; j < n; ++j) std::swap(m[k * n + j], m[pivot * n + j]); det = -det; }
            det *= m[k * n + k];
            for (std::size_t i = k + 1; i < n; ++i) {
                const double factor = m[i * n + k] / m[k * n + k];
                for (std::size_t j = k; j < n; ++j) m[i * n + j] -= factor * m[k * n + j];
            }
        }
        return finite(det, at, "the determinant");
    }

    // Gauss-Jordan elimination with partial pivoting on [A | I].
    Value inverse(const Value& a, Location at) {
        const std::size_t n = a.type.rows;
        std::vector<double> m = a.floats;
        Value result = Value::zeros(a.type);
        for (std::size_t i = 0; i < n; ++i) result.floats[i * n + i] = 1.0;
        double scale = 0;
        for (double x : m) scale = std::max(scale, std::fabs(x));
        const double tolerance = (scale == 0 ? 1.0 : scale) * n * std::numeric_limits<double>::epsilon();
        for (std::size_t k = 0; k < n; ++k) {
            std::size_t pivot = k;
            for (std::size_t i = k + 1; i < n; ++i) if (std::fabs(m[i * n + k]) > std::fabs(m[pivot * n + k])) pivot = i;
            const double p = m[pivot * n + k];
            if (p == 0.0) fail(at, "inverse_mat: the matrix is singular (its determinant is 0), so it has no inverse");
            if (std::fabs(p) < tolerance)
                fail(at, "inverse_mat: the matrix is numerically singular (pivot " + show_float(p) +
                             " is negligible next to entries of size " + show_float(scale) + "), so no reliable inverse exists");
            if (pivot != k)
                for (std::size_t j = 0; j < n; ++j) { std::swap(m[k * n + j], m[pivot * n + j]); std::swap(result.floats[k * n + j], result.floats[pivot * n + j]); }
            for (std::size_t j = 0; j < n; ++j) { m[k * n + j] /= p; result.floats[k * n + j] /= p; }
            for (std::size_t i = 0; i < n; ++i) {
                if (i == k) continue;
                const double factor = m[i * n + k];
                if (factor == 0.0) continue;
                for (std::size_t j = 0; j < n; ++j) { m[i * n + j] -= factor * m[k * n + j]; result.floats[i * n + j] -= factor * result.floats[k * n + j]; }
            }
        }
        for (double x : result.floats) finite(x, at, "an element of the inverse");
        return result;
    }

    MYDSL_NOINLINE Value input(const Expression& e) {
        if (e->args.empty()) return stdin_.read(e->type, e->location);
        const std::string& path = e->args.at(0)->name;
        auto& source = files_[path];
        if (!source) source = std::make_unique<InputSource>(path);
        return source->read(e->type, e->location);
    }

    // ----- user-defined functions -----------------------------------------------------

    static std::string where(Location p) {
        return "line " + std::to_string(p.line) + ", column " + std::to_string(p.column);
    }

    // The innermost kTraceFrames active calls, innermost first, with runs of identical frames
    // (direct recursion) collapsed into one entry with a repeat count; `omitted` receives the
    // number of deeper frames that are not listed.
    std::vector<CallFrame> trace(std::size_t& omitted) const {
        std::vector<CallFrame> frames;
        std::size_t counted = 0;
        for (auto it = calls_.rbegin(); it != calls_.rend() && frames.size() < kTraceFrames;) {
            std::size_t repeats = 1;
            while (it + static_cast<std::ptrdiff_t>(repeats) != calls_.rend() && (it + static_cast<std::ptrdiff_t>(repeats))->function == it->function &&
                   (it + static_cast<std::ptrdiff_t>(repeats))->call_site.line == it->call_site.line && (it + static_cast<std::ptrdiff_t>(repeats))->call_site.column == it->call_site.column &&
                   (it + static_cast<std::ptrdiff_t>(repeats))->caller == it->caller)
                ++repeats;
            frames.push_back({it->function->name, it->call_site, repeats, file_of(it->caller)});
            counted += repeats;
            it += static_cast<std::ptrdiff_t>(repeats);
        }
        omitted = calls_.size() - counted;
        return frames;
    }

    // A call of a user-defined function. Arguments are evaluated in the caller's scope, left to
    // right, before the new frame is entered; the body runs as code of the function's module.
    MYDSL_NOINLINE Value invoke(const Expression& e, const Statement& function, std::size_t module) {
        std::vector<Value> values = arguments(e);
        if (calls_.size() >= kMaxCallDepth) depth_exceeded(e, function); // the enclosing call appends the trace
        calls_.push_back({function.get(), e->location, module_});
        const auto outer_loop = loop_;
        const std::size_t caller = module_;
        loop_.reset(); // a time limit inside the body is reported at the body's own loops
        module_ = module;
        try {
            Value result = run_body(function, std::move(values));
            calls_.pop_back();
            loop_ = outer_loop;
            module_ = caller;
            return result;
        } catch (const TracedError&) {
            throw; // already carries the trace of the innermost failure
        } catch (const Error& error) {
            rethrow_with_trace(error); // the error lies in the body: module_ is still the callee's module
        }
    }

    // Binds the arguments in a fresh scope without a parent (top-level variables are invisible,
    // as in the type checker; parameters and body share the scope) and runs the body up to the
    // first return statement reached.
    MYDSL_NOINLINE Value run_body(const Statement& function, std::vector<Value> values) {
        Scope frame(*this, /*isolated=*/true);
        for (std::size_t i = 0; i < function->params.size(); ++i)
            scope_->symbols[function->params[i].name] = std::move(values[i]);
        for (const auto& child : function->body.at(0)->body) {
            execute(child);
            if (returning_) break;
        }
        if (!returning_) no_return(function); // the type checker guarantees a return on every path
        Value result = std::move(*returning_);
        returning_.reset();
        return result;
    }

    [[noreturn]] MYDSL_NOINLINE static void depth_exceeded(const Expression& e, const Statement& function) {
        fail(e->location, "Call depth limit of " + std::to_string(kMaxCallDepth) + " exceeded while calling '" + function->name +
                              "' (is a recursive function missing its base case?)");
    }

    // Reaching an import statement is a no-op, unless the loader never resolved it (a Program
    // run through interpret(Program) although it has imports).
    MYDSL_NOINLINE void unresolved_import(const Statement& s) const {
        std::size_t imports = 0;
        for (const auto& statement : modules_.main().program) imports += statement->op == "import";
        if (imports != modules_.main().imports.size())
            fail(s->location, "import \"" + s->name + "\" was not resolved by the module loader (interpret(Program) is for single-file programs; load the program with load_modules)");
    }

    [[noreturn]] MYDSL_NOINLINE static void no_return(const Statement& function) {
        fail(function->location, "Internal error: function '" + function->name + "' ended without a return value");
    }

    // Adds the call trace and, for an error inside an imported function, its file and the
    // chain of imports that led to the file.
    [[noreturn]] MYDSL_NOINLINE void rethrow_with_trace(const Error& error) const {
        std::size_t omitted = 0;
        std::vector<CallFrame> frames = trace(omitted);
        throw TracedError(error.stage, error.location, error.message, file_of(module_), modules_.modules.at(module_).chain, std::move(frames), omitted);
    }

    // ----- statements -----------------------------------------------------------------

    MYDSL_NOINLINE void print(const Value& v, Location at) {
        const std::string text = v.str() + "\n";
        written_ += text.size();
        if (limits_.max_output > 0 && written_ > limits_.max_output)
            fail(at, "Output limit of " + std::to_string(limits_.max_output) + " bytes exceeded");
        output_ << text;
    }

    MYDSL_NOINLINE void block(const Statement& s) {
        Scope scope(*this);
        for (const auto& child : s->body) {
            execute(child);
            if (returning_) return; // a return statement unwinds the enclosing blocks
        }
    }

    // Dispatcher only (see evaluate()).
    void execute(const Statement& s) {
        Nesting nesting(*this, s->location);
        tick(s->location);
        const std::string& op = s->op;
        if (op == "declare") declare(s);
        else if (op == "assign") assign(s);
        else if (op == "assign_index") assign_index(s);
        else if (op == "print") print(s);
        else if (op == "error") raise_error(s);
        else if (op == "function") {} // registered by run() before execution; nothing happens when the definition is reached
        else if (op == "import") unresolved_import(s); // resolved by the loader; the module's functions were registered by run()
        else if (op == "return") return_value(s);
        else if (op == "block") block(s);
        else if (op == "if") branch(s);
        else if (op == "while") while_loop(s);
        else if (op == "for") for_loop(s);
        else unknown(s->location, "statement", op);
    }

    MYDSL_NOINLINE void declare(const Statement& s) {
        Value value = evaluate(s->exprs.at(0));
        scope_->symbols[s->name] = std::move(value);
    }

    MYDSL_NOINLINE void assign(const Statement& s) {
        Value value = evaluate(s->exprs.at(0));
        *scope_->find(s->name) = std::move(value); // the checker guarantees the variable exists
    }

    MYDSL_NOINLINE void assign_index(const Statement& s) {
        Value& target = *scope_->find(s->name);
        const std::size_t at = position(target, s->exprs.begin(), s->exprs.end() - 1);
        const Value value = evaluate(s->exprs.back());
        if (is_int(target)) target.ints[at] = as_int(value); else target.floats[at] = as_float(value);
    }

    MYDSL_NOINLINE void print(const Statement& s) { print(evaluate(s->exprs.at(0)), s->location); }

    // error(message): a runtime error raised by the program itself, reported like any other
    // (with the file, the call trace and the import chain when inside an imported function).
    [[noreturn]] MYDSL_NOINLINE void raise_error(const Statement& s) { fail(s->location, evaluate(s->exprs.at(0)).text); }

    MYDSL_NOINLINE void return_value(const Statement& s) {
        if (calls_.empty()) fail(s->location, "Internal error: 'return' executed outside a function call"); // the parser prevents this
        returning_ = evaluate(s->exprs.at(0));
    }

    MYDSL_NOINLINE void branch(const Statement& s) {
        if (evaluate(s->exprs.at(0)).truth()) block(s->body.at(0));
        else if (s->body.size() > 1) block(s->body.at(1));
    }

    MYDSL_NOINLINE void while_loop(const Statement& s) {
        const auto outer_loop = loop_;
        loop_ = s->location;
        while (evaluate(s->exprs.at(0)).truth()) {
            tick(s->location);
            block(s->body.at(0));
            if (returning_) break;
        }
        loop_ = outer_loop;
    }

    MYDSL_NOINLINE void for_loop(const Statement& s) {
        const auto outer_loop = loop_;
        loop_ = s->location;
        Scope loop(*this);
        scope_->symbols[s->name] = evaluate(s->exprs.at(0));
        while (evaluate(s->exprs.at(1)).truth()) {
            tick(s->location);
            block(s->body.at(0));
            if (returning_) break;
            advance_counter(s);
        }
        loop_ = outer_loop;
    }

    MYDSL_NOINLINE void advance_counter(const Statement& s) {
        const std::int64_t step = as_int(evaluate(s->exprs.at(2)));
        Value& counter = *scope_->find(s->name);
        counter = Value::integer(checked('+', as_int(counter), step, s->location));
    }

public:
    Interpreter(const Modules& modules, std::istream& input, std::ostream& output, const Limits& limits)
        : output_(output), limits_(limits), stdin_(input), modules_(modules), module_(modules.modules.size() - 1) {}

    // Registers the functions of every module (the type checker guarantees unique names among
    // the modules a program can see), then runs the main program's statements.
    std::map<std::string, Value> run() {
        for (std::size_t m = 0; m < modules_.modules.size(); ++m)
            for (const auto& s : modules_.modules[m].program)
                if (s->op == "function") functions_[s->name] = {s, m};
        for (const auto& s : modules_.main().program) execute(s);
        return globals();
    }

    // The top-level variables with their current values (also meaningful after an error).
    std::map<std::string, Value> globals() const {
        std::map<std::string, Value> values;
        for (const auto& [name, value] : globals_.symbols) values[name] = value;
        return values;
    }
};
} // namespace

namespace {
// The interpreter runs on its own thread purely to get a stack of a known, generous size
// (kStackBytes): the main thread's stack (8 MB on Linux/macOS, and frames are several times
// larger under the sanitizers or at -O0) could otherwise overflow on deep MyDSL recursion
// before kMaxCallDepth / kMaxNesting are reached. The caller blocks until the run finishes, so
// nothing is concurrent; errors are carried back and rethrown in the calling thread.
struct Job {
    const Modules* modules;
    std::istream* input;
    std::ostream* output;
    const Limits* limits;
    std::map<std::string, Value>* state;
    std::map<std::string, Value> result;
    std::exception_ptr error;
    void run() {
        Interpreter interpreter(*modules, *input, *output, *limits);
        try { result = interpreter.run(); }
        catch (...) { error = std::current_exception(); }
        if (state) *state = interpreter.globals(); // on failure: the values at the moment of the error
    }
};

void* run_job(void* raw) {
    static_cast<Job*>(raw)->run();
    return nullptr;
}
} // namespace

std::map<std::string, Value> interpret(const Modules& modules, std::istream& input, std::ostream& output,
                                       const Limits& limits, std::map<std::string, Value>* state) {
    Job job{&modules, &input, &output, &limits, state, {}, nullptr};
    pthread_attr_t attributes;
    pthread_t thread;
    if (pthread_attr_init(&attributes) == 0) {
        const bool started = pthread_attr_setstacksize(&attributes, kStackBytes) == 0 &&
                             pthread_create(&thread, &attributes, run_job, &job) == 0;
        pthread_attr_destroy(&attributes);
        if (started) pthread_join(thread, nullptr);
        else job.run(); // no thread available: run on the caller's stack (limits still apply)
    } else {
        job.run();
    }
    if (job.error) std::rethrow_exception(job.error);
    return std::move(job.result);
}

std::map<std::string, Value> interpret(const Program& program, std::istream& input, std::ostream& output,
                                       const Limits& limits, std::map<std::string, Value>* state) {
    return interpret(single_module(program), input, output, limits, state);
}
} // namespace mydsl

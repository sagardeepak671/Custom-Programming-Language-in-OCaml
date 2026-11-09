#pragma once
#include <cstdint>
#include <istream>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace mydsl {
struct Location { std::size_t line = 1, column = 1; };
// Source range covered by a token or node; `end` is exclusive (just after the last character).
struct Span { Location begin, end; };
// One level of the call trace of a runtime error: `function` was entered from `call_site`,
// which lies in the imported file `file` ("" = the main program); `repeats` > 1 collapses a
// run of identical frames (direct recursion).
struct CallFrame { std::string function; Location call_site; std::size_t repeats = 1; std::string file; };
// " (in function 'g' called from line 3, column 5 of 'lib/a.mydsl' [2 times]; in function 'f'
// called from ...; ... 4 more call(s))", or "" for an empty trace.
std::string render_trace(const std::vector<CallFrame>& trace, std::size_t omitted_frames);
// One step of the chain of imports that led to a file (README "Modules"): the import statement
// at `location` in `file` ("" = the main program) imported the next file of the chain.
struct ImportSite { std::string file; Location location; };
// " (imported from 'lib/a.mydsl' at line 1, column 1; imported from the main program at line
// 2, column 1)" (at most five sites, then "; ... N more import(s)"), or "" for an empty chain.
std::string render_imports(const std::vector<ImportSite>& imports);
struct Error : std::runtime_error {
    std::string stage;
    Location location;
    std::string message;                    // without the trace and the import chain
    std::string file;                       // the imported file the error is located in; "" when it is in the main program
    std::vector<ImportSite> imports;        // how `file` was reached, innermost first (empty for the main program)
    std::vector<CallFrame> trace;           // active calls, innermost first (runtime errors inside functions)
    std::size_t omitted_frames = 0;         // deeper frames not listed in `trace`
    // what() is "<stage> error[ in '<file>'] at line L, column C: <message><rendered trace><rendered imports>".
    Error(const std::string& stage, Location location, const std::string& message);
    Error(const std::string& stage, Location location, const std::string& message,
          std::vector<CallFrame> trace, std::size_t omitted_frames);
    Error(const std::string& stage, Location location, const std::string& message, const std::string& file,
          std::vector<ImportSite> imports, std::vector<CallFrame> trace, std::size_t omitted_frames);
    // The same error located in the imported file `file`, reached through `imports`.
    Error in_file(const std::string& file, std::vector<ImportSite> imports) const;
};
// Element/base types. Vectors and matrices (rank 1 and 2) exist for Int and Float only;
// Bool and String are always scalars (rank 0).
enum class Base { Bool, Int, Float, String };
struct Type {
    Base base = Base::Int;
    int rank = 0;
    std::size_t rows = 1, cols = 1;
    bool operator==(const Type& other) const;
    bool operator!=(const Type& other) const { return !(*this == other); }
    bool numeric() const { return base == Base::Int || base == Base::Float; }
    std::size_t size() const { return rows * cols; }
    std::string str() const;
};
// A runtime value: `ints` holds int and bool elements, `floats` float elements (row-major for
// matrices), `text` the characters of a string.
struct Value {
    Type type;
    std::vector<std::int64_t> ints;
    std::vector<double> floats;
    std::string text;
    static Value integer(std::int64_t n);
    static Value real(double n);
    static Value boolean(bool b);
    static Value string(std::string text);
    static Value zeros(Type type);
    double number(std::size_t i) const;
    bool truth() const { return ints.at(0) != 0; }
    // Print format: Int(1), Float(2.5), Bool(true), String("a\"b"), IntVector[1, 2], ...
    std::string str() const;
};
// `text` written as a MyDSL string literal: double-quoted, with \" \\ \n \t \r escapes.
std::string quote_string(const std::string& text);
// The literal text of a scalar value, as Print shows it inside the parentheses and as to_string
// returns it: "42", "-7", "2.5", "3.0", "1e+20", "true", or the characters of a string.
std::string scalar_text(const Value& value);
enum class TokenKind { Word, Integer, Float, String, Symbol, End };
struct Token { TokenKind kind; std::string text; Location location; Location end; };
std::vector<Token> lex(const std::string& source);
// One "line:column kind text" line per token (the --tokens command-line mode).
void print_tokens(const std::vector<Token>& tokens, std::ostream& output);
// The fixed vocabulary of the language, as the parser and the type checker know it (the
// browser editor's completion is built from these lists, see language_json): every reserved
// word - type names, statement keywords, true/false - and the names of the builtin functions.
const std::set<std::string>& keywords();
const std::vector<std::string>& builtin_functions();
struct Expr;
using Expression = std::shared_ptr<Expr>;
// Expression nodes produced by the parser. `op` selects the node kind:
//   "literal"   literal holds a Bool/Int/Float scalar.
//   "string"    name holds the (unescaped) text of a string literal: "hello", "", "a\nb".
//   "variable"  name holds the identifier.
//   "unary"     name is "-" or "!", args = {operand}.
//   "binary"    name is one of + - * / % < > <= >= == != && ||, args = {left, right}.
//   "call"      name is the function name, args are the arguments (may be empty).
//   "index"     args = {target, index} or {target, row, column}.
//   "vector"    args are the elements; annotation = {rank 1, rows N}.
//   "matrix"    args are the elements in row-major order; annotation = {rank 2, rows R, cols C}.
// For vector/matrix literals the parser fills in the shape (rank/rows/cols) of `annotation`;
// the element base type is not known until type checking, so `annotation->base` is unset.
// `location` is the operator token for unary/binary nodes, the '[' for index nodes and the
// first token of the expression otherwise; `span` covers the whole expression text (including
// surrounding parentheses). `type` is reserved for the type checker.
struct Expr {
    std::string op;
    Location location;
    Span span;
    std::string name;
    Value literal;
    std::vector<Expression> args;
    std::optional<Type> annotation;
    Type type;
};
struct Stmt;
using Statement = std::shared_ptr<Stmt>;
// A typed parameter of a user-defined function (`int x`, `floatvector 3 v`). `location` is the
// type's first token; `span` covers the type and the name.
struct Parameter {
    Type type;
    std::string name;
    Location location;
    Span span;
};
// Statement nodes produced by the parser. `op` selects the node kind:
//   "declare"      declaration = declared type, name = variable, exprs = {initializer}.
//   "assign"       name = variable, exprs = {value}.
//   "assign_index" name = variable, exprs = {index, value} or {row, column, value}.
//   "print"        exprs = {value}.
//   "error"        exprs = {message}: stops the program with the message (a string).
//   "block"        body = statements of the block.
//   "if"           exprs = {condition}, body = {then-block} or {then-block, else-block}.
//   "while"        exprs = {condition}, body = {block}.
//   "for"          name = loop variable, exprs = {initial, condition, step}, body = {block}.
//   "function"     declaration = return type, name = function name, params = parameters,
//                  body = {block}. Only produced at the top level of a program.
//   "return"       exprs = {value}. Only produced inside a function body.
//   "import"       name = the file name written in the statement (the unescaped text of the
//                  string literal, e.g. "lib/math_utils.mydsl"). Only produced at the top level
//                  of a program; resolved by load_modules (see "Modules" below).
// `location` is the first token of the statement; `span` runs to the end of the statement
// (including the terminating ';' where there is one).
struct Stmt {
    std::string op, name;
    Location location;
    Span span;
    std::optional<Type> declaration;
    std::vector<Parameter> params;
    std::vector<Expression> exprs;
    std::vector<Statement> body;
};
using Program = std::vector<Statement>;
// Parses the full source text. Throws mydsl::Error with stage "Parse" (or "Lexing") and the
// line/column of the offending token on the first syntax error.
Program parse(const std::string& source);
void print_ast(const Program& program, std::ostream& out);
// ----- Modules (README "Modules") ---------------------------------------------------------
// An `import "file";` statement as resolved by the loader: `path` is the text written in the
// statement, `location` the statement's position in the importing module and `module` the index
// in Modules::modules of the module it refers to.
struct Import { std::string path; Location location; std::size_t module = 0; };
// One source file of a multi-file program.
struct Module {
    std::string path;              // normalised absolute path identifying the module ("" when the main program came from standard input)
    std::string display;           // the path as shown in messages: relative to the working directory when the file is inside it ("" for standard input)
    std::string source;            // the text of the file
    Program program;               // its AST: function definitions, import statements and other top-level statements
    std::vector<Import> imports;   // its import statements in source order, resolved
    std::vector<ImportSite> chain; // the import statements through which the module was first reached, innermost first (empty for the main program)
    bool main = false;             // true for the program that was run (the last module)
    std::size_t functions() const; // number of top-level function definitions
    std::size_t statements() const; // number of other top-level statements (neither function nor import)
};
struct Modules {
    // In dependency order: every module comes after the modules it imports (directly or
    // indirectly), so the main program is last. A module reached through several import paths
    // (a "diamond") appears once.
    std::vector<Module> modules;
    const Module& main() const { return modules.back(); }
};
// Parses `main_source` (read from the file `main_path`, or from standard input / the browser
// when `main_path` is empty) and loads, transitively, every file it imports with the same
// lexer and parser: an import path is resolved relative to the directory of the importing file
// (relative to the working directory for standard input), a file is loaded once however many
// modules import it, and the result lists the modules in dependency order. Throws mydsl::Error:
// the main program's own "Lexing"/"Parse" error unchanged; with stage "Import" for an empty
// path, a missing or unreadable file, a directory, the same file imported twice by one module,
// a circular import (a module that imports itself directly or through other modules) or too
// deep nesting; and the "Lexing"/"Parse" error of an imported file with Error::file and
// Error::imports set so that the message names the file and the chain of imports. A program
// without imports never touches the file system.
// `overlay` holds texts that replace files on disk while loading: canonical absolute path ->
// content (the browser sends the unsaved texts of its open tabs, so a program sees the
// editor's version of a library that has not been saved yet). A file must still exist on
// disk to be imported; only its content is taken from the overlay.
using FileOverlay = std::map<std::string, std::string>;
Modules load_modules(const std::string& main_source, const std::string& main_path = "", const FileOverlay& overlay = {});
// Lists the modules in dependency order with their imports, functions and statement counts
// (the --modules command-line mode).
void print_modules(const Modules& modules, std::ostream& out);
// Execution limits for the interpreter; 0 means unlimited. Exceeding a limit raises a
// Runtime error ("Execution stopped after N ms" / "Output limit of N bytes exceeded").
struct Limits { double max_millis = 0; std::size_t max_output = 0; };
// Debugging support for the browser view (see README "Browser view"): tokenizes and parses
// `source`, loads its imports (relative to `source_path`, or to the working directory when it
// is empty) and returns a JSON document {"tokens": [...], "ast": [...] | null, "error": {...} | null}.
// Never throws on malformed programs; lexer/parser/import errors are reported in "error".
std::string debug_json(const std::string& source, const std::string& source_path = "", const FileOverlay& overlay = {});
// The project files of the browser page (src/project.cpp): the MyDSL (.mydsl, .txt) files
// below `root` (the server's working directory when empty) and nothing outside it. `relative`
// is a plain path relative to the root ("lib/a.mydsl"); anything else - absolute paths, "..",
// symbolic links leading outside, other extensions, files over 1 MiB - is refused with a
// ProjectError{http status, message}. project_path validates and returns the absolute path
// (the file must exist when `must_exist`, else only its directory). project_tree_json lists
// every file and the directories containing them; project_file_json reads one file; and
// write_project_file saves one atomically (temporary file + rename, mode kept).
struct ProjectError { int status; std::string message; };
std::string project_path(const std::string& root, const std::string& relative, bool must_exist);
std::string project_tree_json(const std::string& root = "");
std::string project_file_json(const std::string& root, const std::string& relative);
void write_project_file(const std::string& root, const std::string& relative, const std::string& content);
// The language's vocabulary with a short description of each entry, for the editor's
// completion (the --language-json command-line mode and GET /api/language): {"keywords": [...],
// "builtins": [...]}; layout in src/debug.cpp.
std::string language_json();
// Parses, loads imports, type-checks and executes `source` under `limits`, with `input` as the
// program's standard input; returns a JSON document {"output": "...", "error": {...} | null,
// "variables": [{"name", "type", "value"}, ...], "millis": N} (layout in src/debug.cpp).
// Never throws on bad programs.
std::string run_json(const std::string& source, const std::string& input, const Limits& limits,
                     const std::string& source_path = "", const FileOverlay& overlay = {});
// Serves the debugging page on 127.0.0.1:port (0 picks a free port) until the process is
// terminated. Logs the chosen address to `log`. Returns a non-zero exit status on failure.
int serve_debug_page(int port, std::ostream& log);
// Static type checker (rules in README "Type rules", "Functions" and "Modules"). Fills
// Expr::type for every expression of every module and returns the types of the main program's
// top-level variables (function signatures stay in the AST: see Stmt "function"). The functions
// of every module are collected first; a module sees its own functions and those of the modules
// it imports, directly or indirectly; an imported module may contain only function definitions
// and imports. Throws mydsl::Error with stage "Type" and the location of the offending
// statement/expression on the first error - with Error::file and Error::imports set when it
// lies in an imported module.
std::map<std::string, Type> typecheck(const Modules& modules);
// The same for a single-file program (a Modules with one module).
std::map<std::string, Type> typecheck(const Program& program);
// Executes a type-checked program (see README "Execution rules", "Functions" and "Modules"):
// the functions of every module are registered, then the top-level statements of the main
// program run (those of imported modules never do). Print writes to `output`; `input` is the
// program's standard input for Input(). Returns the final values of the top-level variables.
// Throws mydsl::Error with stage "Runtime" and a source location on division by zero, integer
// overflow, exceeded limits (time, output, call depth) and so on; errors inside user-defined
// functions carry the call trace (Error::trace, each frame with the file of its call site) and,
// inside imported functions, Error::file and Error::imports. If `state` is given it receives
// the top-level variables as well - also when the run fails, in which case it holds their
// values at the moment of the error (the browser's variable inspector). Runs on a helper
// thread with a large stack (the caller blocks until it finishes).
std::map<std::string, Value> interpret(const Modules& modules, std::istream& input,
                                       std::ostream& output, const Limits& limits = {},
                                       std::map<std::string, Value>* state = nullptr);
// The same for a single-file program (a Modules with one module).
std::map<std::string, Value> interpret(const Program& program, std::istream& input,
                                       std::ostream& output, const Limits& limits = {},
                                       std::map<std::string, Value>* state = nullptr);
// A Modules holding `program` as its only (main) module, for the single-file overloads.
Modules single_module(const Program& program);
// Full pipeline: parse, load imports (relative to `source_path`; see load_modules), type-check,
// execute. With `check_only` the program is not executed and the top-level variable types are
// printed instead; with `quiet` the program's output is discarded (errors are still thrown).
void run(const std::string& source, std::istream& input, std::ostream& output,
         bool quiet = false, bool check_only = false, const std::string& source_path = "");
} // namespace mydsl

// JSON view of the tokenizer, parser, type checker and interpreter output, used by the browser
// debugging page (web/index.html) and by `mydsl --json`. debug_json format:
//
// {
//   "tokens": [{"kind": "word", "text": "int", "begin": {"line":1,"column":1},
//               "end": {"line":1,"column":4}}, ...],        // [] after a lexing error
//   "ast": [statement, ...] | null,                         // null after a lexing/parse error
//   "error": null | {"stage": "Parse", "line": 3, "column": 5, "message": "...",
//                    "file": "lib/a.mydsl",                    // only for an error inside an imported file
//                    "imports": [{"file": "", "line": 1, "column": 1}, ...]},  // the chain of imports that
//                                                             // led there, innermost first ("" = the main
//                                                             // program); stages "Type" and "Import" keep the AST
//   "functions": [{"name": "square", "params": [{"name": "x", "type": "int"}], "returns": "int",
//                  "line": 3, "column": 1, "file": "lib/math_utils.mydsl"}, ...]
// }                                                           // every function the main program can
//                                                             // call: those of the modules it imports
//                                                             // (directly or indirectly; "file" names
//                                                             // the module) in dependency order, then its
//                                                             // own; [] without an AST
// run_json format:
// {
//   "output": "Int(1)\n...",                                  // everything Print wrote
//   "error": null | {"stage": "Runtime", "line": 2, "column": 12, "message": "Division by zero: 10 / 0",
//                    "trace": [{"function": "half", "line": 5, "column": 7, "repeats": 1,
//                               "file": "lib/a.mydsl"}, ...],  // trace: active calls, innermost first,
//                    "omitted": 0,                             // "file" only for call sites in imported
//                    "file": ..., "imports": [...]},           // files; omitted: deeper calls not listed;
//                                                              // file/imports as in debug_json
//   "variables": [{"name": "a", "type": "int", "value": "Int(1)"}, ...],  // top-level variables
//                                                              // (their values at the error, if any)
//   "millis": 12
// }
// language_json format (the vocabulary for the editor's completion; the lists come from the
// parser and the type checker, the descriptions from the table in this file):
// {
//   "keywords": [{"name": "while", "kind": "statement" | "type" | "constant",
//                 "detail": "while (condition) { ... };", "description": "..."}, ...],
//   "builtins": [{"name": "abs", "signature": "abs(x)", "detail": "int -> int, ...", "description": "..."}, ...]
// }
// statement:  {"node": "statement", "op": "declare", "name": "a", "declaration": "int",
//              "location": {...}, "begin": {...}, "end": {...},
//              "params": [parameter, ...] (function definitions only: "declaration" is the
//              return type), "exprs": [expression, ...], "body": [statement, ...]}
// parameter:  {"node": "parameter", "op": "parameter", "name": "x", "declaration": "int",
//              "location": {...}, "begin": {...}, "end": {...}}
// expression: {"node": "expression", "op": "binary", "name": "+", "location": {...},
//              "begin": {...}, "end": {...}, "args": [expression, ...],
//              "value": "Int(1)" (literals only), "shape": {"rank":1,"rows":3,"cols":1} (literals),
//              "type": "int" (only when the whole program type-checks)}
#include "mydsl.hpp"
#include <chrono>
#include <sstream>

namespace mydsl {
namespace {
void write_string(std::ostream& out, const std::string& text) {
    out << '"';
    for (const unsigned char c : text) {
        switch (c) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 0x20 || c == 0x7f) {
                const char* hex = "0123456789abcdef";
                out << "\\u00" << hex[c >> 4] << hex[c & 0xf];
            } else out << c;
        }
    }
    out << '"';
}

void write_location(std::ostream& out, Location p) {
    out << "{\"line\":" << p.line << ",\"column\":" << p.column << '}';
}

void write_span(std::ostream& out, const Span& span) {
    out << "\"begin\":"; write_location(out, span.begin);
    out << ",\"end\":"; write_location(out, span.end);
}

const char* kind_name(TokenKind kind) {
    switch (kind) {
    case TokenKind::Word: return "word";
    case TokenKind::Integer: return "integer";
    case TokenKind::Float: return "float";
    case TokenKind::String: return "string";
    case TokenKind::Symbol: return "symbol";
    case TokenKind::End: return "end";
    }
    return "unknown";
}

void write_tokens(std::ostream& out, const std::vector<Token>& tokens) {
    out << '[';
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (i) out << ',';
        out << "{\"kind\":\"" << kind_name(tokens[i].kind) << "\",\"text\":";
        write_string(out, tokens[i].text);
        out << ','; write_span(out, {tokens[i].location, tokens[i].end});
        out << '}';
    }
    out << ']';
}

bool typed = false; // set while writing a program whose types were all resolved

void write_expr(std::ostream& out, const Expression& e) {
    out << "{\"node\":\"expression\",\"op\":"; write_string(out, e->op);
    if (!e->name.empty() || e->op == "string") { out << ",\"name\":"; write_string(out, e->name); } // "" is a valid string literal
    if (typed) { out << ",\"type\":"; write_string(out, e->type.str()); }
    if (e->op == "literal") { out << ",\"value\":"; write_string(out, e->literal.str()); }
    if (e->annotation)
        out << ",\"shape\":{\"rank\":" << e->annotation->rank << ",\"rows\":" << e->annotation->rows
            << ",\"cols\":" << e->annotation->cols << '}';
    out << ",\"location\":"; write_location(out, e->location);
    out << ','; write_span(out, e->span);
    out << ",\"args\":[";
    for (std::size_t i = 0; i < e->args.size(); ++i) { if (i) out << ','; write_expr(out, e->args[i]); }
    out << "]}";
}

void write_stmt(std::ostream& out, const Statement& s) {
    out << "{\"node\":\"statement\",\"op\":"; write_string(out, s->op);
    if (!s->name.empty()) { out << ",\"name\":"; write_string(out, s->name); }
    if (s->declaration) { out << ",\"declaration\":"; write_string(out, s->declaration->str()); }
    out << ",\"location\":"; write_location(out, s->location);
    out << ','; write_span(out, s->span);
    if (s->op == "function") {
        out << ",\"params\":[";
        for (std::size_t i = 0; i < s->params.size(); ++i) {
            const Parameter& p = s->params[i];
            out << (i ? "," : "") << "{\"node\":\"parameter\",\"op\":\"parameter\",\"name\":"; write_string(out, p.name);
            out << ",\"declaration\":"; write_string(out, p.type.str());
            out << ",\"location\":"; write_location(out, p.location);
            out << ','; write_span(out, p.span);
            out << '}';
        }
        out << ']';
    }
    out << ",\"exprs\":[";
    for (std::size_t i = 0; i < s->exprs.size(); ++i) { if (i) out << ','; write_expr(out, s->exprs[i]); }
    out << "],\"body\":[";
    for (std::size_t i = 0; i < s->body.size(); ++i) { if (i) out << ','; write_stmt(out, s->body[i]); }
    out << "]}";
}

void write_error(std::ostream& out, const Error& error) {
    out << "{\"stage\":"; write_string(out, error.stage);
    out << ",\"line\":" << error.location.line << ",\"column\":" << error.location.column;
    out << ",\"message\":"; write_string(out, error.message);
    if (!error.file.empty()) { out << ",\"file\":"; write_string(out, error.file); }
    if (!error.imports.empty()) {
        out << ",\"imports\":[";
        for (std::size_t i = 0; i < error.imports.size(); ++i) {
            const ImportSite& site = error.imports[i];
            out << (i ? "," : "") << "{\"file\":"; write_string(out, site.file);
            out << ",\"line\":" << site.location.line << ",\"column\":" << site.location.column << '}';
        }
        out << ']';
    }
    if (!error.trace.empty() || error.omitted_frames) {
        out << ",\"trace\":[";
        for (std::size_t i = 0; i < error.trace.size(); ++i) {
            const CallFrame& frame = error.trace[i];
            out << (i ? "," : "") << "{\"function\":"; write_string(out, frame.function);
            out << ",\"line\":" << frame.call_site.line << ",\"column\":" << frame.call_site.column << ",\"repeats\":" << frame.repeats;
            if (!frame.file.empty()) { out << ",\"file\":"; write_string(out, frame.file); }
            out << '}';
        }
        out << "],\"omitted\":" << error.omitted_frames;
    }
    out << '}';
}
} // namespace

std::string run_json(const std::string& source, const std::string& input, const Limits& limits, const std::string& source_path, const FileOverlay& overlay) {
    std::ostringstream out, program_output;
    std::istringstream no_input(input);
    std::optional<Error> error;
    std::map<std::string, Value> variables;
    const auto started = std::chrono::steady_clock::now();
    try {
        const Modules modules = load_modules(source, source_path, overlay);
        typecheck(modules);
        interpret(modules, no_input, program_output, limits, &variables);
    } catch (const Error& e) {
        error = e;
    }
    const auto millis = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    out << "{\"output\":"; write_string(out, program_output.str());
    out << ",\"error\":";
    if (error) write_error(out, *error); else out << "null";
    out << ",\"variables\":[";
    bool first = true;
    for (const auto& [name, value] : variables) {
        out << (first ? "" : ",") << "{\"name\":"; write_string(out, name);
        out << ",\"type\":"; write_string(out, value.type.str());
        out << ",\"value\":"; write_string(out, value.str());
        out << '}';
        first = false;
    }
    out << "],\"millis\":" << static_cast<long long>(millis) << "}\n";
    return out.str();
}

bool has_imports(const Program& program) {
    for (const auto& s : program) if (s->op == "import") return true;
    return false;
}

// The function definitions of `program` (top-level "function" statements) as JSON objects,
// appended to `out`; `file` names the module for imported functions ("" = the main program).
void write_functions(std::ostream& out, const Program& program, const std::string& file, bool& first) {
    for (const auto& s : program) {
        if (s->op != "function") continue;
        out << (first ? "" : ",") << "{\"name\":"; write_string(out, s->name);
        out << ",\"params\":[";
        for (std::size_t i = 0; i < s->params.size(); ++i) {
            out << (i ? "," : "") << "{\"name\":"; write_string(out, s->params[i].name);
            out << ",\"type\":"; write_string(out, s->params[i].type.str());
            out << '}';
        }
        out << "],\"returns\":"; write_string(out, s->declaration ? s->declaration->str() : "");
        out << ",\"line\":" << s->location.line << ",\"column\":" << s->location.column;
        if (!file.empty()) { out << ",\"file\":"; write_string(out, file); }
        out << '}';
        first = false;
    }
}

// Documentation for language_json: what the editor shows next to a completion. Every keyword
// and builtin has an entry (tests/tests.cpp checks that the lists stay in step).
struct KeywordDoc { const char* name; const char* kind; const char* detail; const char* description; };
const KeywordDoc kKeywordDocs[] = {
    {"int", "type", "int name := value;", "64-bit integer; arithmetic is overflow-checked."},
    {"float", "type", "float name := value;", "Double-precision floating-point number; literals need a '.' or an exponent (2.0, 1e3)."},
    {"bool", "type", "bool name := value;", "true or false; combine with && || ! (also + for or, * for and)."},
    {"string", "type", "string name := \"text\";", "Text in double quotes with \\\" \\\\ \\n \\t \\r escapes; + concatenates, == compares; never converted to or from numbers."},
    {"intvector", "type", "intvector N name := [1, 2, 3];", "Vector of N ints; the size is part of the type. v[i] indexes from 0."},
    {"floatvector", "type", "floatvector N name := [1.0, 2.0];", "Vector of N floats; the size is part of the type. v[i] indexes from 0."},
    {"intmatrix", "type", "intmatrix R,C name := [[1, 2], [3, 4]];", "R-by-C matrix of ints; m[i, j] indexes from 0."},
    {"floatmatrix", "type", "floatmatrix R,C name := [[1.0, 2.0], [3.0, 4.0]];", "R-by-C matrix of floats; m[i, j] indexes from 0."},
    {"if", "statement", "if (condition) then { ... } else { ... };", "Conditional; the condition must be bool, 'else' is optional, the statement ends with ';'."},
    {"then", "statement", "if (condition) then { ... };", "Introduces the block of an if statement."},
    {"else", "statement", "if (condition) then { ... } else { ... };", "The block of an if statement that runs when the condition is false."},
    {"while", "statement", "while (condition) { ... };", "Loop while the bool condition holds; the statement ends with ';'."},
    {"for", "statement", "for (i := start, condition, step) { ... };", "Counting loop: declares the int variable i, runs while the condition holds, adds step after each pass."},
    {"function", "statement", "function type name(type param, ...) { ... return value; };", "Defines a function (top level only); every path of the body must end in return or error(...)."},
    {"return", "statement", "return value;", "Leaves the function with the value (inside a function body only)."},
    {"import", "statement", "import \"path/to/file.mydsl\";", "Loads the function definitions of another file (path relative to this file) and makes them callable."},
    {"error", "statement", "error(\"message\");", "Stops the program with a runtime error carrying the string message."},
    {"Print", "statement", "Print(value);", "Writes the value to the output, e.g. Int(5), Float(2.5), String(\"x\"), IntVector[1, 2]."},
    {"true", "constant", "true", "The bool value true."},
    {"false", "constant", "false", "The bool value false."},
};
struct BuiltinDoc { const char* name; const char* signature; const char* detail; const char* description; };
const BuiltinDoc kBuiltinDocs[] = {
    {"abs", "abs(x)", "int -> int, float -> float, vector -> float", "Absolute value of a number, or the length (Euclidean norm) of a vector."},
    {"angle_vec", "angle_vec(a, b)", "vector, vector -> float", "Angle in radians between two vectors of the same type and size."},
    {"determinant", "determinant(m)", "intmatrix N,N -> int, floatmatrix N,N -> float", "Determinant of a square matrix."},
    {"dotprod_vec", "dotprod_vec(a, b)", "vector, vector -> element type", "Dot product of two vectors of the same type and size."},
    {"Input", "Input() or Input(\"file\")", "-> the declared type", "Reads the next value from standard input (or the file) in literal syntax; only as the whole initializer, right-hand side or return value."},
    {"inv_vec", "inv_vec(v)", "vector -> same type", "The elements of a vector in reverse order."},
    {"inverse_mat", "inverse_mat(m)", "floatmatrix N,N -> floatmatrix N,N", "Inverse of a square float matrix (convert an intmatrix with to_float first); singular matrices are runtime errors."},
    {"is_zero", "is_zero(x)", "vector or matrix -> bool", "True when every element is zero."},
    {"length", "length(s)", "string -> int", "The number of characters (bytes) of a string."},
    {"mult_mat", "mult_mat(a, b)", "matrix R,K, matrix K,C -> matrix R,C", "Matrix product of two matrices with the same element type."},
    {"substring", "substring(s, start, count)", "string, int, int -> string", "count characters of s starting at the 0-based index start; the range must lie inside the string."},
    {"to_float", "to_float(x)", "int -> float, intvector -> floatvector, intmatrix -> floatmatrix", "Converts ints to floats, element-wise for vectors and matrices."},
    {"to_int", "to_int(x)", "float -> int, floatvector -> intvector, floatmatrix -> intmatrix", "Converts floats to ints (truncating towards zero), element-wise for vectors and matrices."},
    {"to_string", "to_string(x)", "int, float, bool or string -> string", "The literal text of a scalar value (\"42\", \"2.5\", \"true\")."},
    {"transpose", "transpose(m)", "matrix R,C -> matrix C,R", "The transpose of a matrix."},
};

std::string debug_json(const std::string& source, const std::string& source_path, const FileOverlay& overlay) {
    std::ostringstream out;
    std::vector<Token> tokens;
    std::optional<Error> error;
    std::optional<Program> program;
    std::optional<Modules> modules; // set once the imports loaded (even if the types then fail)
    try {
        tokens = lex(source);
        program = parse(source);
        // The AST of the main program is shown even when one of its imports fails to load; the
        // checker annotates the main AST held by `modules` (a copy of the shared nodes).
        if (has_imports(*program)) {
            modules = load_modules(source, source_path, overlay);
            program = modules->main().program;
            typecheck(*modules);
        } else {
            typecheck(*program);
        }
        typed = true;
    } catch (const Error& e) {
        error = e;
        typed = false;
    }
    out << "{\"tokens\":"; write_tokens(out, tokens);
    out << ",\"ast\":";
    if (!program) out << "null";
    else {
        out << '[';
        for (std::size_t i = 0; i < program->size(); ++i) { if (i) out << ','; write_stmt(out, (*program)[i]); }
        out << ']';
    }
    typed = false;
    out << ",\"error\":";
    if (error) write_error(out, *error); else out << "null";
    // The callable functions: those of the imported modules (every module in `modules` is
    // reachable from the main program, hence visible to it), then the main program's own.
    out << ",\"functions\":[";
    bool first = true;
    if (modules) {
        for (const Module& module : modules->modules)
            if (!module.main) write_functions(out, module.program, module.display, first);
    }
    if (program) write_functions(out, *program, "", first);
    out << "]}\n";
    return out.str();
}

std::string language_json() {
    std::ostringstream out;
    out << "{\"keywords\":[";
    bool first = true;
    for (const std::string& name : keywords()) { // the parser's list, documented here
        const KeywordDoc* doc = nullptr;
        for (const KeywordDoc& d : kKeywordDocs) if (name == d.name) doc = &d;
        out << (first ? "" : ",") << "{\"name\":"; write_string(out, name);
        out << ",\"kind\":"; write_string(out, doc ? doc->kind : "statement");
        out << ",\"detail\":"; write_string(out, doc ? doc->detail : "");
        out << ",\"description\":"; write_string(out, doc ? doc->description : "");
        out << '}';
        first = false;
    }
    out << "],\"builtins\":[";
    first = true;
    for (const std::string& name : builtin_functions()) { // the type checker's list
        const BuiltinDoc* doc = nullptr;
        for (const BuiltinDoc& d : kBuiltinDocs) if (name == d.name) doc = &d;
        out << (first ? "" : ",") << "{\"name\":"; write_string(out, name);
        out << ",\"signature\":"; write_string(out, doc ? doc->signature : name + "(...)");
        out << ",\"detail\":"; write_string(out, doc ? doc->detail : "");
        out << ",\"description\":"; write_string(out, doc ? doc->description : "");
        out << '}';
        first = false;
    }
    out << "]}\n";
    return out.str();
}
} // namespace mydsl

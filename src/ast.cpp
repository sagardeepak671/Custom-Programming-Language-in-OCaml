#include "mydsl.hpp"
#include <iomanip>
#include <sstream>

namespace mydsl {
std::string render_trace(const std::vector<CallFrame>& trace, std::size_t omitted_frames) {
    std::string text;
    for (const auto& frame : trace)
        text += (text.empty() ? "" : "; ") + std::string("in function '") + frame.function + "' called from line " +
                std::to_string(frame.call_site.line) + ", column " + std::to_string(frame.call_site.column) +
                (frame.file.empty() ? "" : " of '" + frame.file + "'") +
                (frame.repeats > 1 ? " [" + std::to_string(frame.repeats) + " times]" : "");
    if (omitted_frames) text += "; ... " + std::to_string(omitted_frames) + " more call(s)";
    return text.empty() ? "" : " (" + text + ")";
}
// At most kShownImports sites are written out (the innermost ones); Error::imports keeps them all.
constexpr std::size_t kShownImports = 5;
std::string render_imports(const std::vector<ImportSite>& imports) {
    std::string text;
    for (std::size_t i = 0; i < imports.size() && i < kShownImports; ++i)
        text += (text.empty() ? "" : "; ") + std::string("imported from ") + (imports[i].file.empty() ? "the main program" : "'" + imports[i].file + "'") +
                " at line " + std::to_string(imports[i].location.line) + ", column " + std::to_string(imports[i].location.column);
    if (imports.size() > kShownImports) text += "; ... " + std::to_string(imports.size() - kShownImports) + " more import(s)";
    return text.empty() ? "" : " (" + text + ")";
}
Error::Error(const std::string& stage, Location p, const std::string& message)
    : Error(stage, p, message, "", {}, {}, 0) {}
Error::Error(const std::string& stage, Location p, const std::string& message, std::vector<CallFrame> frames, std::size_t omitted)
    : Error(stage, p, message, "", {}, std::move(frames), omitted) {}
Error::Error(const std::string& stage, Location p, const std::string& message, const std::string& file, std::vector<ImportSite> sites,
             std::vector<CallFrame> frames, std::size_t omitted)
    : std::runtime_error(stage + " error" + (file.empty() ? "" : " in '" + file + "'") + " at line " + std::to_string(p.line) + ", column " +
                         std::to_string(p.column) + ": " + message + render_trace(frames, omitted) + render_imports(sites)),
      stage(stage), location(p), message(message), file(file), imports(std::move(sites)), trace(std::move(frames)), omitted_frames(omitted) {}
Error Error::in_file(const std::string& in, std::vector<ImportSite> sites) const {
    return Error(stage, location, message, in, std::move(sites), trace, omitted_frames);
}
bool Type::operator==(const Type& b) const {
    return base == b.base && rank == b.rank && rows == b.rows && cols == b.cols;
}
std::string Type::str() const {
    std::string s = base == Base::Bool ? "bool" : base == Base::Int ? "int" : base == Base::Float ? "float" : "string";
    if (rank == 1) s += "vector " + std::to_string(rows);
    if (rank == 2) s += "matrix " + std::to_string(rows) + "," + std::to_string(cols);
    return s;
}
Value Value::zeros(Type t) {
    Value v; v.type = t;
    if (t.base == Base::Float) v.floats.resize(t.size());
    else if (t.base != Base::String) v.ints.resize(t.size());
    return v;
}
Value Value::integer(std::int64_t n) { auto v = zeros({Base::Int}); v.ints[0] = n; return v; }
Value Value::real(double n) { auto v = zeros({Base::Float}); v.floats[0] = n; return v; }
Value Value::boolean(bool b) { auto v = zeros({Base::Bool}); v.ints[0] = b; return v; }
Value Value::string(std::string text) { auto v = zeros({Base::String}); v.text = std::move(text); return v; }
std::string quote_string(const std::string& text) {
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        case '\r': out += "\\r"; break;
        default: out += c;
        }
    }
    return out + "\"";
}
double Value::number(std::size_t i) const {
    return type.base == Base::Float ? floats.at(i) : static_cast<double>(ints.at(i));
}
// Text of element `i`: ints as decimal, floats with 15 significant digits and always a '.' or
// an exponent ("3.0", "0.25", "1e+20"), so that the text reads back as a literal of that type.
static std::string element_text(const Value& v, std::size_t i) {
    if (v.type.base == Base::Int) return std::to_string(v.ints.at(i));
    std::ostringstream s;
    s << std::setprecision(15) << v.floats.at(i);
    std::string text = s.str();
    if (text.find_first_of(".eE") == std::string::npos) text += ".0";
    return text;
}
std::string scalar_text(const Value& value) {
    if (value.type.base == Base::Bool) return value.truth() ? "true" : "false";
    if (value.type.base == Base::String) return value.text;
    return element_text(value, 0);
}
std::string Value::str() const {
    if (type.base == Base::Bool) return truth() ? "Bool(true)" : "Bool(false)";
    if (type.base == Base::String) return "String(" + quote_string(text) + ")";
    std::ostringstream out;
    out << (type.base == Base::Int ? "Int" : "Float");
    auto cell = [&](std::size_t i) { out << element_text(*this, i); };
    if (!type.rank) { out << '('; cell(0); out << ')'; }
    else if (type.rank == 1) {
        out << "Vector[";
        for (std::size_t i = 0; i < type.rows; ++i) { if (i) out << ", "; cell(i); }
        out << ']';
    } else {
        out << "Matrix[\n";
        for (std::size_t r = 0; r < type.rows; ++r) {
            out << "  [";
            for (std::size_t c = 0; c < type.cols; ++c) {
                if (c) out << ", ";
                cell(r * type.cols + c);
            }
            out << "]\n";
        }
        out << ']';
    }
    return out.str();
}
// S-expression dump, one statement per top-level line:
//   (declare int a (binary + Int(1) (variable b)))
//   (vector[3] Int(1) Int(2) Int(3))      (matrix[2,2] Float(1.0) ...)   (string "ip.txt")
static void dump_expr(const Expression& e, std::ostream& out) {
    if (e->op == "literal") { out << e->literal.str(); return; }
    out << '(' << e->op;
    if ((e->op == "vector" || e->op == "matrix") && e->annotation) {
        out << '[' << e->annotation->rows;
        if (e->op == "matrix") out << ',' << e->annotation->cols;
        out << ']';
    }
    if (e->op == "string") out << ' ' << quote_string(e->name);
    else if (!e->name.empty()) out << ' ' << e->name;
    for (const auto& a : e->args) { out << ' '; dump_expr(a, out); }
    out << ')';
}
//   (function int square (parameter int x)
//     (block
//       (return (binary * (variable x) (variable x)))))
//   (import "lib/math_utils.mydsl")
static void dump_stmt(const Statement& s, std::ostream& out, int depth) {
    out << std::string(static_cast<std::size_t>(depth), ' ') << '(' << s->op;
    if (s->declaration) out << ' ' << s->declaration->str();
    if (s->op == "import") out << ' ' << quote_string(s->name);
    else if (!s->name.empty()) out << ' ' << s->name;
    for (const auto& p : s->params) out << " (parameter " << p.type.str() << ' ' << p.name << ')';
    for (const auto& e : s->exprs) { out << ' '; dump_expr(e, out); }
    for (const auto& child : s->body) { out << '\n'; dump_stmt(child, out, depth + 2); }
    out << ')';
}
void print_ast(const Program& program, std::ostream& out) {
    for (const auto& s : program) { dump_stmt(s, out, 0); out << '\n'; }
}
void run(const std::string& source, std::istream& input, std::ostream& output,
         bool quiet, bool check_only, const std::string& source_path) {
    // Parses the main program and loads its imports (a program without imports is parsed
    // exactly as before and never touches the file system).
    const Modules modules = load_modules(source, source_path);
    const Program& program = modules.main().program;
    const auto globals = typecheck(modules);
    if (check_only) {
        for (const auto& [name, type] : globals) output << name << ": " << type.str() << '\n';
        auto signature = [&](const Statement& s) {
            output << s->name << ": function(";
            for (std::size_t i = 0; i < s->params.size(); ++i)
                output << (i ? ", " : "") << s->params[i].type.str() << ' ' << s->params[i].name;
            output << ") -> " << s->declaration->str();
        };
        std::size_t functions = 0, imported = 0;
        for (const auto& s : program) {
            if (s->op != "function") continue;
            ++functions;
            signature(s);
            output << '\n';
        }
        // Imported functions follow, in dependency order, each with the module that defines it.
        for (const auto& m : modules.modules) {
            if (m.main) continue;
            for (const auto& s : m.program) {
                if (s->op != "function") continue;
                ++imported;
                signature(s);
                output << " (imported from '" << m.display << "')\n";
            }
        }
        output << "Type check passed: " << program.size() << " top-level statement(s), "
               << globals.size() << " top-level variable(s)";
        if (functions) output << ", " << functions << " function(s)";
        if (modules.modules.size() > 1)
            output << ", " << imported << " imported function(s) from " << modules.modules.size() - 1 << " module(s)";
        output << '\n';
        return;
    }
    if (quiet) {
        std::ostringstream discard;
        (void)interpret(modules, input, discard);
    } else {
        (void)interpret(modules, input, output);
    }
}
void print_tokens(const std::vector<Token>& tokens, std::ostream& output) {
    for (const auto& token : tokens) {
        const char* kind = token.kind == TokenKind::Word ? "word" :
            token.kind == TokenKind::Integer ? "integer" :
            token.kind == TokenKind::Float ? "float" :
            token.kind == TokenKind::String ? "string" :
            token.kind == TokenKind::End ? "end" : "symbol";
        output << token.location.line << ':' << token.location.column << ' '
               << kind << ' ' << quote_string(token.text) << '\n';
    }
}
} // namespace mydsl

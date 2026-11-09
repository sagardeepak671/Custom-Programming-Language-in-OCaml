// Static type checker. Walks the AST produced by the parser, fills in Expr::type for every
// expression and throws mydsl::Error (stage "Type") with a source location on the first
// problem. The rules are summarised in README.md ("Type rules"); in short:
//
//   * Scalars are int, float, bool and string; vectors/matrices carry their element base and
//     size. Types must match exactly: there is no implicit conversion between int and float
//     (to_int/to_float convert explicitly) and none at all to or from string.
//   * Strings: literals ("..." with \" \\ \n \t \r escapes), + between two strings
//     (concatenation), == and != between two strings, Print, Input(), declarations,
//     assignments, parameters and return values, and the builtins length(s) -> int,
//     substring(s, start, count) -> string and to_string(scalar) -> string. Everything else
//     (ordering, arithmetic, indexing, vector elements, conditions) is an error.
//   * Arithmetic + - * / on two operands of the same numeric type; % on ints only;
//     + and * on two bools are "or" and "and"; + - between equal-shaped vectors/matrices;
//     scalar * tensor, tensor * scalar and tensor / scalar scale element-wise.
//   * Comparisons need two equal numeric scalars; == and != need two equal types of any shape;
//     && || ! need bool; unary - needs a numeric scalar, vector or matrix.
//   * Vector/matrix literals need elements of one numeric type; a[i] and m[i,j] need int
//     indices (literal indices are bounds-checked statically).
//   * Blocks, if/while/for bodies open a new scope. A declaration may shadow a variable of an
//     outer scope but not repeat one of the same scope. The for loop declares its own int
//     variable, visible in the header and body only.
//   * Input() has the type of the variable it is assigned to, so it may only appear as the
//     whole initializer of a declaration, the whole right-hand side of an assignment or the
//     whole value of a return statement.
//   * User-defined functions (README "Functions"): all top-level definitions are collected
//     first, so a function may be called before its definition and may be recursive. A name
//     may be defined once and may not be a builtin. Parameters are the outermost variables of
//     the body (they cannot be redeclared there); the body sees only its parameters and its
//     own locals, never top-level variables. A call needs exactly the declared number of
//     arguments with exactly the parameter types and has the declared return type. Every
//     return value must have the return type, and the end of the body must be unreachable
//     (every path ends in a return; loops never count as returning).
//   * Modules (README "Modules"): the functions of every loaded module are collected first, in
//     dependency order. A module sees its own functions and those of the modules it imports,
//     directly or through further imports - never those of a module that imports it - so one
//     name defined in two files that are both visible is a duplicate, reported with both
//     files. An imported module may contain only function definitions and imports; only the
//     main program has other top-level statements. Bodies are checked with the same scoping
//     rules whatever file they are in, and an error inside an imported module is reported with
//     that file and the chain of imports that led to it.
#include "mydsl.hpp"
#include "environment.hpp"
#include <memory>

namespace mydsl {
namespace {
struct Binding { Type type; Location declared; bool parameter = false; };
// Signature of a user-defined function, collected before the bodies are checked; `module` is
// the index (in Modules::modules) of the file that defines it.
struct Signature { Type returns; std::vector<Parameter> params; Location declared; std::size_t module = 0; };

Type scalar(Base base) { Type t; t.base = base; t.rank = 0; t.rows = 1; t.cols = 1; return t; }
Type vector_of(Base base, std::size_t n) { Type t; t.base = base; t.rank = 1; t.rows = n; t.cols = 1; return t; }
Type matrix_of(Base base, std::size_t r, std::size_t c) { Type t; t.base = base; t.rank = 2; t.rows = r; t.cols = c; return t; }
const Type kInt = scalar(Base::Int), kFloat = scalar(Base::Float), kBool = scalar(Base::Bool), kString = scalar(Base::String);

std::string where(Location p) {
    return "line " + std::to_string(p.line) + ", column " + std::to_string(p.column);
}
bool is_numeric_scalar(const Type& t) { return t.rank == 0 && t.numeric(); }
const char* article(const Type& t) { return t.base == Base::Int ? "an " : "a "; }

class Checker {
    const Modules& modules_;
    Environment<Binding> globals_;
    Environment<Binding>* scope_ = &globals_;
    std::map<std::string, Signature> functions_;
    const Signature* function_ = nullptr; // signature of the function whose body is being checked
    std::string function_name_;
    std::size_t module_ = 0;              // the module whose code is being checked
    bool unresolved_imports_ = false;     // the module has import statements the loader did not resolve
    std::vector<std::vector<bool>> reach_; // reach_[m][n]: module n is m itself or imported by m, directly or indirectly

    std::string name_of(std::size_t module) const {
        const Module& m = modules_.modules.at(module);
        return m.main ? "the main program" : "'" + m.display + "'";
    }

    // "line L, column C", plus " of '<file>'" when `module` is not the module being checked.
    std::string located(Location p, std::size_t module) const {
        return where(p) + (module == module_ ? "" : " of " + name_of(module));
    }

    // Runs `body` as the code of module `m`; an error inside an imported module is relayed with
    // the module's file and import chain.
    template <class F> void in_module(std::size_t m, F body) {
        module_ = m;
        const Module& module = modules_.modules.at(m);
        std::size_t imports = 0;
        for (const auto& s : module.program) imports += s->op == "import";
        unresolved_imports_ = imports != module.imports.size();
        try {
            body();
        } catch (const Error& error) {
            if (module.main || !error.file.empty()) throw;
            throw error.in_file(module.display, module.chain);
        }
    }

    // RAII block scope: pushes a new environment for its lifetime. An `isolated` scope has no
    // parent, so nothing declared outside it (in particular no top-level variable) is visible.
    struct Scope {
        Checker& checker;
        Environment<Binding>* previous;
        std::unique_ptr<Environment<Binding>> env;
        explicit Scope(Checker& c, bool isolated = false)
            : checker(c), previous(c.scope_), env(std::make_unique<Environment<Binding>>(isolated ? nullptr : c.scope_)) { c.scope_ = env.get(); }
        ~Scope() { checker.scope_ = previous; }
    };

    [[noreturn]] static void fail(Location at, const std::string& message) { throw Error("Type", at, message); }

    // Extra hint appended when an int and a float meet, or a string meets another type.
    static std::string conversion_hint(const Type& a, const Type& b) {
        const bool mixed = (a.base == Base::Int && b.base == Base::Float) || (a.base == Base::Float && b.base == Base::Int);
        if (mixed) return " (MyDSL does not convert between int and float implicitly; use to_float(...) or to_int(...))";
        if ((a.base == Base::String) != (b.base == Base::String)) return " (MyDSL does not convert between string and other types)";
        return "";
    }

    // ----- declarations and lookup ----------------------------------------------------

    void declare(const std::string& name, const Type& type, Location at, bool parameter = false) {
        auto existing = scope_->symbols.find(name);
        if (existing != scope_->symbols.end())
            fail(at, "Variable '" + name + "' is already declared in this scope (as " + (existing->second.parameter ? "parameter " : "") +
                         existing->second.type.str() + " at " + where(existing->second.declared) + ")" +
                         (existing->second.parameter ? "; parameters are variables of the function body and cannot be redeclared there" : ""));
        scope_->symbols[name] = Binding{type, at, parameter};
    }

    const Binding& lookup(const std::string& name, Location at) {
        if (const Binding* binding = scope_->find(name)) return *binding;
        if (functions_.count(name))
            fail(at, "Undeclared variable '" + name + "' ('" + name + "' is a function; call it with arguments: " + name + "(...))");
        if (function_)
            fail(at, "Undeclared variable '" + name + "' (inside function '" + function_name_ +
                         "' only its parameters and local variables are visible, not top-level variables; pass the value as a parameter)");
        fail(at, "Undeclared variable '" + name + "'");
    }

    // ----- expressions ----------------------------------------------------------------

    Type check(const Expression& e) {
        e->type = infer(e);
        return e->type;
    }

    Type infer(const Expression& e) {
        const std::string& op = e->op;
        if (op == "literal") return e->literal.type;
        if (op == "string") return kString;
        if (op == "variable") return lookup(e->name, e->location).type;
        if (op == "unary") return unary(e);
        if (op == "binary") return binary(e);
        if (op == "call") return call(e);
        if (op == "index") return index(e);
        if (op == "vector" || op == "matrix") return literal_tensor(e);
        fail(e->location, "Unknown expression kind '" + op + "'");
    }

    Type unary(const Expression& e) {
        const Type operand = check(e->args.at(0));
        if (e->name == "!") {
            if (operand == kBool) return kBool;
            fail(e->location, "Operator '!' requires a bool operand but got " + operand.str());
        }
        if (operand.numeric()) return operand; // -int, -float, -vector, -matrix
        fail(e->location, "Operator '-' requires an int, float, vector or matrix operand but got " + operand.str());
    }

    Type binary(const Expression& e) {
        const std::string& op = e->name;
        const Type left = check(e->args.at(0)), right = check(e->args.at(1));
        const std::string pair = left.str() + " and " + right.str();
        if (op == "&&" || op == "||") {
            if (left == kBool && right == kBool) return kBool;
            fail(e->location, "Operator '" + op + "' requires bool operands but got " + pair);
        }
        if (op == "==" || op == "!=") {
            if (left == right) return kBool;
            fail(e->location, "Operator '" + op + "' requires operands of the same type but got " + pair + conversion_hint(left, right));
        }
        if (op == "<" || op == ">" || op == "<=" || op == ">=") {
            if (left == right && is_numeric_scalar(left)) return kBool;
            if (left == right) fail(e->location, "Operator '" + op + "' requires int or float operands but got " + pair);
            fail(e->location, "Operator '" + op + "' requires operands of the same type but got " + pair + conversion_hint(left, right));
        }
        if (op == "%") {
            if (left == kInt && right == kInt) return kInt;
            fail(e->location, "Operator '%' requires int operands but got " + pair);
        }
        // + - * /
        if (left == right) {
            if (is_numeric_scalar(left)) return left;
            if (left == kString) {
                if (op == "+") return kString; // concatenation
                fail(e->location, "Operator '" + op + "' cannot be applied to strings (only + for concatenation and == / != are defined)");
            }
            if (left == kBool) {
                if (op == "+" || op == "*") return kBool; // Boolean algebra: + is "or", * is "and"
                fail(e->location, "Operator '" + op + "' cannot be applied to bool operands (only + as 'or' and * as 'and' are defined)");
            }
            if (op == "+" || op == "-") return left; // element-wise on equal shapes
            if (op == "*")
                fail(e->location, left.rank == 1 ? "Operator '*' is not defined between two vectors; use dotprod_vec(a, b) for the dot product or a scalar to scale"
                                                 : "Operator '*' is not defined between two matrices; use mult_mat(a, b) for the matrix product or a scalar to scale");
            fail(e->location, "Operator '/' is not defined between two " + std::string(left.rank == 1 ? "vectors" : "matrices") + "; only division by a scalar is supported");
        }
        if (left.base == right.base && left.numeric()) {
            if (op == "*" && (left.rank == 0 || right.rank == 0)) return left.rank == 0 ? right : left;
            if (op == "/" && right.rank == 0 && left.rank > 0) return left;
            if (left.rank > 0 && right.rank > 0)
                fail(e->location, "Operator '" + op + "' requires operands of the same shape but got " + pair);
            fail(e->location, "Operator '" + op + "' cannot combine a scalar and a " + (left.rank + right.rank == 1 ? "vector" : "matrix") +
                                  " (" + pair + "); only scalar * tensor, tensor * scalar and tensor / scalar are defined");
        }
        fail(e->location, "Operator '" + op + "' cannot be applied to " + pair + conversion_hint(left, right));
    }

    Type index(const Expression& e) {
        const Type target = check(e->args.at(0));
        const std::size_t count = e->args.size() - 1;
        if (target.rank == 0) fail(e->location, "Cannot index a value of type " + target.str() + "; only vectors and matrices can be indexed");
        if (target.rank == 1 && count != 1) fail(e->location, "A vector takes one index (v[i]) but " + target.str() + " was given two");
        if (target.rank == 2 && count != 2) fail(e->location, "A matrix takes two indices (m[row, column]) but " + target.str() + " was given one");
        check_indices(e->args.begin() + 1, e->args.end(), target);
        return scalar(target.base);
    }

    // Indices must be int; literal indices must be within the static bounds.
    template <class It> void check_indices(It first, It last, const Type& target) {
        std::size_t which = 0;
        for (It it = first; it != last; ++it, ++which) {
            const Type t = check(*it);
            if (t != kInt) fail((*it)->location, "Index must be int but is " + t.str());
            if (auto value = constant_int(*it)) {
                const std::size_t limit = (target.rank == 1 || which == 0) ? target.rows : target.cols;
                if (*value < 0 || static_cast<std::size_t>(*value) >= limit)
                    fail((*it)->location, (target.rank == 2 ? (which == 0 ? "Row index " : "Column index ") : "Index ") + std::to_string(*value) +
                                              " is out of range for " + target.str() + " (valid indices are 0.." + std::to_string(limit - 1) + ")");
            }
        }
    }

    static std::optional<std::int64_t> constant_int(const Expression& e) {
        if (e->op == "literal" && e->literal.type == kInt) return e->literal.ints.at(0);
        if (e->op == "unary" && e->name == "-") if (auto inner = constant_int(e->args.at(0))) return -*inner;
        return std::nullopt;
    }

    Type literal_tensor(const Expression& e) {
        const bool matrix = e->op == "matrix";
        std::optional<Type> element;
        for (std::size_t i = 0; i < e->args.size(); ++i) {
            const Type t = check(e->args[i]);
            if (!is_numeric_scalar(t))
                fail(e->args[i]->location, std::string(matrix ? "Matrix" : "Vector") + " elements must be int or float but element " +
                                               std::to_string(i + 1) + " is " + t.str());
            if (!element) element = t;
            else if (*element != t)
                fail(e->args[i]->location, std::string(matrix ? "Matrix" : "Vector") + " elements must all have the same type: element " +
                                               std::to_string(i + 1) + " is " + t.str() + " but element 1 is " + element->str() + conversion_hint(*element, t));
        }
        const Type& shape = *e->annotation;
        return matrix ? matrix_of(element->base, shape.rows, shape.cols) : vector_of(element->base, shape.rows);
    }

    // ----- builtin functions ----------------------------------------------------------

    static const std::vector<std::string>& builtins() { return builtin_functions(); }

    void arity(const Expression& e, std::size_t n) {
        if (e->args.size() != n)
            fail(e->location, "Function '" + e->name + "' expects " + std::to_string(n) + " argument" + (n == 1 ? "" : "s") +
                                  " but got " + std::to_string(e->args.size()));
    }

    static std::string parameter_list(const Signature& signature) {
        std::string text;
        for (const auto& p : signature.params) text += (text.empty() ? "" : ", ") + p.type.str() + " " + p.name;
        return text;
    }

    // Call of a user-defined function: exact argument count and exact parameter types.
    Type user_call(const Expression& e, const Signature& signature) {
        const std::string& f = e->name;
        if (!reach_.at(module_).at(signature.module))
            fail(e->location, "Unknown function '" + f + "': '" + f + "' is defined in " + name_of(signature.module) + ", which " +
                                  (module_ == modules_.modules.size() - 1 ? "this program" : name_of(module_)) +
                                  " does not import (a function is visible where its file is imported, directly or through other imports)");
        const std::size_t n = signature.params.size();
        if (e->args.size() != n)
            fail(e->location, "Function '" + f + "' expects " + std::to_string(n) + " argument" + (n == 1 ? "" : "s") +
                                  (n ? " (" + parameter_list(signature) + ")" : "") + " but got " + std::to_string(e->args.size()) +
                                  " (declared at " + located(signature.declared, signature.module) + ")");
        for (std::size_t i = 0; i < n; ++i) {
            const Parameter& p = signature.params[i];
            const Type t = check(e->args[i]);
            if (t != p.type)
                fail(e->args[i]->location, "Argument " + std::to_string(i + 1) + " of '" + f + "' must be " + p.type.str() + " (parameter '" + p.name +
                                               "') but got " + t.str() + conversion_hint(p.type, t));
        }
        return signature.returns;
    }

    Type call(const Expression& e) {
        const std::string& f = e->name;
        if (f == "Input")
            fail(e->location, "Input() can only be used as the whole initializer of a declaration, the whole right-hand side of an "
                              "assignment or the whole value of a return statement, because its type is taken from the variable it is stored in");
        if (auto defined = functions_.find(f); defined != functions_.end()) return user_call(e, defined->second);
        if (f == "abs") {
            arity(e, 1);
            const Type t = check(e->args[0]);
            if (is_numeric_scalar(t)) return t;
            if (t.rank == 1) return kFloat; // vector magnitude
            fail(e->location, "abs expects an int, float or vector but got " + t.str());
        }
        if (f == "to_float" || f == "to_int") {
            arity(e, 1);
            Type t = check(e->args[0]);
            if (!t.numeric()) fail(e->location, f + " expects an int, float, vector or matrix but got " + t.str());
            t.base = f == "to_float" ? Base::Float : Base::Int;
            return t;
        }
        if (f == "length") {
            arity(e, 1);
            const Type t = check(e->args[0]);
            if (t != kString) fail(e->location, "length expects a string but got " + t.str());
            return kInt;
        }
        if (f == "to_string") {
            arity(e, 1);
            const Type t = check(e->args[0]);
            if (t.rank != 0) fail(e->location, "to_string expects an int, float, bool or string but got " + t.str() + " (only scalars can be converted)");
            return kString;
        }
        if (f == "substring") {
            arity(e, 3);
            const Type t = check(e->args[0]);
            if (t != kString) fail(e->location, "substring expects a string as its first argument but got " + t.str());
            const char* names[] = {"start index", "count"};
            for (std::size_t i = 1; i <= 2; ++i) {
                const Type n = check(e->args[i]);
                if (n != kInt) fail(e->args[i]->location, std::string("The ") + names[i - 1] + " of substring must be int but is " + n.str() + conversion_hint(kInt, n));
                if (auto value = constant_int(e->args[i]); value && *value < 0)
                    fail(e->args[i]->location, std::string("The ") + names[i - 1] + " of substring must not be negative (got " + std::to_string(*value) + ")");
            }
            return kString;
        }
        if (f == "determinant" || f == "inverse_mat") {
            arity(e, 1);
            const Type t = check(e->args[0]);
            if (t.rank != 2) fail(e->location, f + " expects a matrix but got " + t.str());
            if (t.rows != t.cols) fail(e->location, f + " expects a square matrix but got " + t.str());
            if (f == "inverse_mat" && t.base != Base::Float)
                fail(e->location, "inverse_mat expects a floatmatrix but got " + t.str() + "; convert with to_float(...)");
            return f == "determinant" ? scalar(t.base) : t;
        }
        if (f == "transpose") {
            arity(e, 1);
            const Type t = check(e->args[0]);
            if (t.rank != 2) fail(e->location, "transpose expects a matrix but got " + t.str());
            return matrix_of(t.base, t.cols, t.rows);
        }
        if (f == "mult_mat") {
            arity(e, 2);
            const Type a = check(e->args[0]), b = check(e->args[1]);
            if (a.rank != 2 || b.rank != 2) fail(e->location, "mult_mat expects two matrices but got " + a.str() + " and " + b.str());
            if (a.base != b.base) fail(e->location, "mult_mat expects matrices of the same element type but got " + a.str() + " and " + b.str() + conversion_hint(a, b));
            if (a.cols != b.rows)
                fail(e->location, "mult_mat cannot multiply " + a.str() + " by " + b.str() + ": the column count of the first (" +
                                      std::to_string(a.cols) + ") must equal the row count of the second (" + std::to_string(b.rows) + ")");
            return matrix_of(a.base, a.rows, b.cols);
        }
        if (f == "inv_vec" || f == "is_zero") {
            arity(e, 1);
            const Type t = check(e->args[0]);
            if (f == "inv_vec" && t.rank != 1) fail(e->location, "inv_vec expects a vector but got " + t.str());
            if (f == "is_zero" && t.rank == 0) fail(e->location, "is_zero expects a vector or matrix but got " + t.str());
            return f == "inv_vec" ? t : kBool;
        }
        if (f == "dotprod_vec" || f == "angle_vec") {
            arity(e, 2);
            const Type a = check(e->args[0]), b = check(e->args[1]);
            if (a.rank != 1 || b.rank != 1) fail(e->location, f + " expects two vectors but got " + a.str() + " and " + b.str());
            if (a != b) fail(e->location, f + " expects two vectors of the same type and size but got " + a.str() + " and " + b.str() + conversion_hint(a, b));
            return f == "dotprod_vec" ? scalar(a.base) : kFloat;
        }
        if (const Binding* variable = scope_->find(f))
            fail(e->location, "Unknown function '" + f + "': '" + f + "' is a variable of type " + variable->type.str() + " (declared at " +
                                  where(variable->declared) + "), not a function");
        std::string known;
        for (const auto& name : builtins()) known += (known.empty() ? "" : ", ") + name;
        std::string defined, imported;
        for (const auto& [name, signature] : functions_) {
            if (!reach_.at(module_).at(signature.module)) continue;
            std::string& list = signature.module == module_ ? defined : imported;
            list += (list.empty() ? "" : ", ") + name;
        }
        fail(e->location, "Unknown function '" + f + "' (available: " + known + (defined.empty() ? "" : "; defined in this " + std::string(modules_.modules.at(module_).main ? "program" : "file") + ": " + defined) +
                              (imported.empty() ? "" : "; imported: " + imported) + ")");
    }

    // Initializer / assigned value: Input() takes the target's type, anything else is inferred.
    Type check_value(const Expression& e, const Type& target) {
        if (e->op == "call" && e->name == "Input") {
            if (e->args.size() > 1) fail(e->location, "Input expects no argument or one file name but got " + std::to_string(e->args.size()) + " arguments");
            if (e->args.size() == 1) {
                if (e->args[0]->op != "string") fail(e->args[0]->location, "The argument of Input must be a file name string literal, e.g. Input(\"data.txt\")");
                check(e->args[0]);
            }
            e->type = target;
            return target;
        }
        return check(e);
    }

    Type condition(const Expression& e, const char* what) {
        const Type t = check(e);
        if (t != kBool) fail(e->location, std::string("The condition of '") + what + "' must be bool but is " + t.str());
        return t;
    }

    // ----- statements -----------------------------------------------------------------

    void block(const Statement& s) {
        Scope scope(*this);
        for (const auto& child : s->body) statement(child);
    }

    void statement(const Statement& s) {
        const std::string& op = s->op;
        if (op == "declare") {
            const Type& declared = *s->declaration;
            const Type value = check_value(s->exprs.at(0), declared);
            if (value != declared)
                fail(s->location, "Cannot initialise '" + s->name + "' of type " + declared.str() + " with a value of type " + value.str() + conversion_hint(declared, value));
            declare(s->name, declared, s->location);
        } else if (op == "assign") {
            const Binding& variable = lookup(s->name, s->location);
            const Type value = check_value(s->exprs.at(0), variable.type);
            if (value != variable.type)
                fail(s->location, "Cannot assign a value of type " + value.str() + " to '" + s->name + "' of type " + variable.type.str() +
                                      " (declared at " + where(variable.declared) + ")" + conversion_hint(variable.type, value));
        } else if (op == "assign_index") {
            const Binding& variable = lookup(s->name, s->location);
            const Type& target = variable.type;
            const std::size_t count = s->exprs.size() - 1;
            if (target.rank == 0) fail(s->location, "Cannot index '" + s->name + "' of type " + target.str() + "; only vector and matrix elements can be assigned");
            if (target.rank == 1 && count != 1) fail(s->location, "'" + s->name + "' is " + article(target) + target.str() + " and takes one index (" + s->name + "[i] := ...)");
            if (target.rank == 2 && count != 2) fail(s->location, "'" + s->name + "' is " + article(target) + target.str() + " and takes two indices (" + s->name + "[row, column] := ...)");
            check_indices(s->exprs.begin(), s->exprs.end() - 1, target);
            const Type element = scalar(target.base);
            const Type value = check_value(s->exprs.back(), element);
            if (value != element)
                fail(s->location, "Cannot assign a value of type " + value.str() + " to an element of '" + s->name + "' (" + target.str() + ", elements are " + element.str() + ")" + conversion_hint(element, value));
        } else if (op == "print") {
            check(s->exprs.at(0));
        } else if (op == "error") {
            const Type message = check(s->exprs.at(0));
            if (message != kString)
                fail(s->exprs.at(0)->location, "The message of error must be a string but is " + message.str() + " (build it with to_string(...) and +)");
        } else if (op == "block") {
            block(s);
        } else if (op == "if") {
            condition(s->exprs.at(0), "if");
            for (const auto& branch : s->body) block(branch);
        } else if (op == "while") {
            condition(s->exprs.at(0), "while");
            block(s->body.at(0));
        } else if (op == "return") {
            if (!function_) fail(s->location, "'return' can only be used inside a function body");
            const Type& expected = function_->returns;
            const Type value = check_value(s->exprs.at(0), expected);
            if (value != expected)
                fail(s->location, "Function '" + function_name_ + "' must return " + expected.str() + " but this return statement has a value of type " +
                                      value.str() + conversion_hint(expected, value));
        } else if (op == "function") {
            fail(s->location, "A function can only be defined at the top level of the program");
        } else if (op == "import") {
            // Resolved by the loader; the functions it brings in were collected by run(). A
            // Program checked without the loader (typecheck(Program)) has unresolved imports.
            if (unresolved_imports_)
                fail(s->location, "import \"" + s->name + "\" was not resolved by the module loader (typecheck(Program) and interpret(Program) are for "
                                  "single-file programs; load the program with load_modules)");
        } else if (op == "for") {
            Scope loop(*this); // the loop variable lives here: header and body only
            const Type initial = check(s->exprs.at(0));
            if (initial != kInt) fail(s->exprs.at(0)->location, "The for loop variable '" + s->name + "' must start with an int value but got " + initial.str());
            declare(s->name, kInt, s->location);
            condition(s->exprs.at(1), "for");
            const Type step = check(s->exprs.at(2));
            if (step != kInt) fail(s->exprs.at(2)->location, "The step of the for loop must be int but got " + step.str());
            block(s->body.at(0));
        } else {
            fail(s->location, "Unknown statement kind '" + op + "'");
        }
    }

    // ----- user-defined functions -----------------------------------------------------

    // Pass 1: validates the declaration and records the signature (with its module).
    void declare_function(const Statement& s) {
        const std::string& name = s->name;
        for (const auto& builtin : builtins())
            if (builtin == name)
                fail(s->location, "Cannot define function '" + name + "': '" + name + "' is a built-in function; choose another name");
        if (auto existing = functions_.find(name); existing != functions_.end())
            fail(s->location, "Function '" + name + "' is already defined (at " + located(existing->second.declared, existing->second.module) + ")" +
                                  (existing->second.module == module_ ? "" : "; function names are shared by a program and every module it imports, directly or indirectly"));
        for (std::size_t i = 0; i < s->params.size(); ++i)
            for (std::size_t j = 0; j < i; ++j)
                if (s->params[i].name == s->params[j].name)
                    fail(s->params[i].location, "Function '" + name + "' declares parameter '" + s->params[i].name + "' twice (first at " +
                                                    where(s->params[j].location) + "); parameter names must be unique");
        functions_[name] = Signature{*s->declaration, s->params, s->location, module_};
    }

    // Statements allowed at the top level of an imported module: definitions and imports only.
    static void library_statement(const Statement& s) {
        if (s->op == "function" || s->op == "import") return;
        const std::string what = s->op == "declare" ? "a declaration" : s->op == "assign" || s->op == "assign_index" ? "an assignment" :
                                 s->op == "print" ? "a Print statement" : s->op == "error" ? "an error statement" : s->op == "block" ? "a block" : s->op == "if" ? "an if statement" :
                                 s->op == "while" ? "a while loop" : s->op == "for" ? "a for loop" : "a statement";
        fail(s->location, "An imported module may only contain function definitions and imports, but this is " + what +
                              " (the top-level statements of an imported module are never executed; move it into a function or into the main program)");
    }

    // True when every path through `s` ends in a return statement or an error statement (which
    // never returns). Loops are never assumed to run, and an if without else may skip its body.
    static bool always_returns(const Statement& s) {
        if (s->op == "return" || s->op == "error") return true;
        if (s->op == "block") {
            for (const auto& child : s->body) if (always_returns(child)) return true;
            return false;
        }
        if (s->op == "if") return s->body.size() == 2 && always_returns(s->body[0]) && always_returns(s->body[1]);
        return false;
    }

    // Pass 2: checks the body in a scope that sees only the parameters and its own locals.
    void check_function(const Statement& s) {
        const Signature& signature = functions_.at(s->name);
        Scope scope(*this, /*isolated=*/true);
        for (const auto& p : signature.params) declare(p.name, p.type, p.location, /*parameter=*/true);
        function_ = &signature;
        function_name_ = s->name;
        for (const auto& child : s->body.at(0)->body) statement(child); // same scope as the parameters
        function_ = nullptr;
        function_name_.clear();
        if (!always_returns(s->body.at(0)))
            fail(s->location, "Function '" + s->name + "' must return " + signature.returns.str() +
                                  " on every path, but the end of its body can be reached without a return statement" +
                                  (s->body.at(0)->body.empty() ? " (the body is empty)"
                                                               : " (an if needs a return in both branches; loops do not count because they may run zero times)"));
    }

public:
    explicit Checker(const Modules& modules) : modules_(modules) {}

    std::map<std::string, Type> run() {
        const auto& modules = modules_.modules;
        const std::size_t n = modules.size();
        // Visibility: a module reaches itself and everything its imports reach. The modules
        // come in dependency order, so every import refers to an earlier, finished row.
        reach_.assign(n, std::vector<bool>(n, false));
        for (std::size_t m = 0; m < n; ++m) {
            reach_[m][m] = true;
            for (const auto& import : modules[m].imports)
                for (std::size_t k = 0; k < n; ++k) reach_[m][k] = reach_[m][k] || reach_[import.module][k];
        }
        // Imported modules are libraries: definitions and imports only. Unresolved imports
        // (a Program checked without the loader) are reported before anything else.
        for (std::size_t m = 0; m < n; ++m)
            in_module(m, [&] {
                for (const auto& s : modules[m].program) {
                    if (s->op == "import") statement(s);
                    else if (m + 1 < n) library_statement(s);
                }
            });
        // All signatures first (so calls may precede definitions and cross files), then the bodies.
        for (std::size_t m = 0; m < n; ++m)
            in_module(m, [&] { for (const auto& s : modules[m].program) if (s->op == "function") declare_function(s); });
        for (std::size_t m = 0; m + 1 < n; ++m)
            in_module(m, [&] { for (const auto& s : modules[m].program) if (s->op == "function") check_function(s); });
        // The main program in source order (as for a single file: its first error is reported).
        in_module(n - 1, [&] { for (const auto& s : modules[n - 1].program) { if (s->op == "function") check_function(s); else statement(s); } });
        std::map<std::string, Type> globals;
        for (const auto& [name, binding] : globals_.symbols) globals[name] = binding.type;
        return globals;
    }
};
} // namespace

const std::vector<std::string>& builtin_functions() {
    static const std::vector<std::string> names = {"abs", "angle_vec", "determinant", "dotprod_vec", "Input", "inv_vec", "inverse_mat",
                                                   "is_zero", "length", "mult_mat", "substring", "to_float", "to_int", "to_string", "transpose"};
    return names;
}

std::map<std::string, Type> typecheck(const Modules& modules) {
    return Checker(modules).run();
}

std::map<std::string, Type> typecheck(const Program& program) {
    return typecheck(single_module(program));
}
} // namespace mydsl

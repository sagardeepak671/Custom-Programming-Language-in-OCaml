#include "mydsl.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <set>

namespace mydsl {
namespace {
// Recursive-descent parser. See README.md ("Grammar") for the rules implemented here and
// include/mydsl.hpp for the shape of the nodes that are produced.
//
// Errors: the first syntax error aborts parsing with a mydsl::Error that carries the
// line/column of the offending token and says what was expected and what was found instead.

// Binary operators from lowest to highest precedence. All levels are left-associative.
const std::array<std::set<std::string>, 6> kBinaryLevels = {{
    {"||"},
    {"&&"},
    {"==", "!="},
    {"<", ">", "<=", ">="},
    {"+", "-"},
    {"*", "/", "%"},
}};

const std::set<std::string> kKeywords = {
    "int", "float", "bool", "intvector", "floatvector", "intmatrix", "floatmatrix",
    "if", "then", "else", "while", "for", "Print", "true", "false", "function", "return", "string", "import", "error"};

const char* const kTypeList = "int, float, bool, string, intvector N, floatvector N, intmatrix R,C or floatmatrix R,C";

// Upper bound on the depth of the AST. Every nested parenthesis, bracket, block and every
// chained operator ("- - - x", "a+b+c+...", "v[0][1]...") counts as one level, so the
// recursive consumers of the tree (printer, checker, interpreter, destructors) cannot
// exhaust the stack on pathological input such as 100k '(' characters.
constexpr int kMaxDepth = 256;

class Parser {
    std::vector<Token> tokens_;
    std::size_t position_ = 0;
    int depth_ = 0;
    // > 0 while parsing a function body: 'return' is only a statement there. Function
    // definitions themselves are only accepted at the top level (see parse_program).
    int function_depth_ = 0;
    int block_depth_ = 0; // > 0 inside '{ }' (used to word the "expected a statement" message)

    // RAII depth accounting: enter() claims one level; all claimed levels are released when
    // the owning parse function returns or throws.
    struct Depth {
        Parser& parser;
        int held = 0;
        explicit Depth(Parser& p) : parser(p) {}
        Depth(Parser& p, Location where) : parser(p) { enter(where); }
        Depth(const Depth&) = delete;
        Depth& operator=(const Depth&) = delete;
        ~Depth() { parser.depth_ -= held; }
        void enter(Location where) {
            ++held;
            if (++parser.depth_ > kMaxDepth)
                parser.fail(where, "Expression or block nesting is too deep (the limit is " +
                                       std::to_string(kMaxDepth) +
                                       " levels, counting nested brackets, blocks and chained operators)");
        }
    };

    // ----- token navigation -------------------------------------------------------------

    const Token& current() const { return tokens_[position_]; }

    const Token& peek(std::size_t n) const {
        return tokens_[std::min(position_ + n, tokens_.size() - 1)];
    }

    // Returns the current token and moves on; never moves past the End token.
    const Token& advance() {
        const Token& token = tokens_[position_];
        if (token.kind != TokenKind::End) ++position_;
        return token;
    }

    static bool is(const Token& token, const std::string& text) {
        return (token.kind == TokenKind::Symbol || token.kind == TokenKind::Word) &&
               token.text == text;
    }

    bool at(const std::string& text) const { return is(current(), text); }

    bool accept(const std::string& text) {
        if (!at(text)) return false;
        ++position_;
        return true;
    }

    // Consumes `text` or fails with "Expected '<text>' <context> but found <token>".
    const Token& expect(const std::string& text, const std::string& context) {
        if (!at(text)) fail(current().location, "Expected '" + text + "' " + context + unexpected());
        return advance();
    }

    [[noreturn]] void fail(Location where, const std::string& message) const {
        throw Error("Parse", where, message);
    }

    static std::string describe(const Token& token) {
        switch (token.kind) {
        case TokenKind::End: return "end of input";
        case TokenKind::String: return "string literal \"" + token.text + "\"";
        default: return "'" + token.text + "'";
        }
    }

    std::string unexpected() const { return " but found " + describe(current()); }

    static std::string where(Location p) {
        return "line " + std::to_string(p.line) + ", column " + std::to_string(p.column);
    }

    static bool is_keyword(const Token& token) {
        return token.kind == TokenKind::Word && kKeywords.count(token.text) != 0;
    }

    static bool is_type_keyword(const Token& token) {
        static const std::set<std::string> types = {
            "int", "float", "bool", "string", "intvector", "floatvector", "intmatrix", "floatmatrix"};
        return token.kind == TokenKind::Word && types.count(token.text) != 0;
    }

    static bool is_identifier(const Token& token) {
        return token.kind == TokenKind::Word && !is_keyword(token);
    }

    // Identifier in a position that introduces a name (declaration, assignment, for-variable,
    // function or parameter name); `what` names the kind of thing in messages.
    std::string expect_identifier(const std::string& context, const std::string& what = "variable") {
        const Token& token = current();
        if (is_keyword(token))
            fail(token.location, "Cannot use keyword '" + token.text + "' as a " + what + " name");
        if (token.kind != TokenKind::Word)
            fail(token.location, "Expected a " + what + " name " + context + unexpected());
        return advance().text;
    }

    // ----- literal helpers --------------------------------------------------------------

    std::int64_t integer_value(const Token& token) const {
        try {
            std::size_t used = 0;
            const auto value = std::stoll(token.text, &used);
            if (used != token.text.size()) throw std::invalid_argument(token.text);
            return value;
        } catch (const std::out_of_range&) {
            fail(token.location, "Integer literal '" + token.text + "' is out of range (maximum is " +
                                     std::to_string(std::numeric_limits<std::int64_t>::max()) + ")");
        } catch (const std::invalid_argument&) {
            fail(token.location, "Invalid integer literal '" + token.text + "'");
        }
    }

    double float_value(const Token& token) const {
        errno = 0;
        char* end = nullptr;
        const double value = std::strtod(token.text.c_str(), &end);
        if (end != token.text.c_str() + token.text.size())
            fail(token.location, "Invalid float literal '" + token.text + "'");
        // Overflow is an error; underflow (e.g. 1e-999) simply yields zero or a denormal.
        if (errno == ERANGE && std::fabs(value) == HUGE_VAL)
            fail(token.location, "Float literal '" + token.text + "' is out of range");
        return value;
    }

    // A dimension in a type ("intvector 3") or a sized literal ("(3 [..])"): positive integer.
    std::size_t expect_dimension(const std::string& context) {
        const Token& token = current();
        if (token.kind != TokenKind::Integer)
            fail(token.location, "Expected a positive integer " + context + unexpected());
        const auto value = integer_value(token);
        if (value <= 0)
            fail(token.location, "Expected a positive integer " + context + " but found '" + token.text + "'");
        advance();
        return static_cast<std::size_t>(value);
    }

    // End position of the most recently consumed token.
    Location last_end() const { return position_ ? tokens_[position_ - 1].end : current().location; }

    // Sets the node's span to [begin, end of the last consumed token).
    template <class Node> Node finish(Node node, Location begin) {
        node->span = {begin, last_end()};
        return node;
    }

    static Expression make_expr(const std::string& op, Location location, std::string name = {}) {
        auto e = std::make_shared<Expr>();
        e->op = op;
        e->location = location;
        e->name = std::move(name);
        return e;
    }

    static Statement make_stmt(const std::string& op, Location location, std::string name = {}) {
        auto s = std::make_shared<Stmt>();
        s->op = op;
        s->location = location;
        s->name = std::move(name);
        return s;
    }

    // ----- expressions ------------------------------------------------------------------

    Expression parse_expression() {
        Depth depth(*this, current().location);
        return parse_binary(0);
    }

    // Precedence climbing over kBinaryLevels; level == size() hands over to unary.
    // Operators of one level are left-associative: a-b-c -> (binary - (binary - a b) c).
    Expression parse_binary(std::size_t level) {
        if (level == kBinaryLevels.size()) return parse_unary();
        Depth depth(*this);
        auto left = parse_binary(level + 1);
        while (current().kind == TokenKind::Symbol && kBinaryLevels[level].count(current().text)) {
            const Token op = advance();
            depth.enter(op.location);
            auto right = parse_binary(level + 1);
            auto node = make_expr("binary", op.location, op.text);
            node->span = {left->span.begin, right->span.end};
            node->args = {std::move(left), std::move(right)};
            left = std::move(node);
        }
        return left;
    }

    // Prefix operators are collected iteratively and then applied from the innermost
    // outwards: "- ! x" -> (unary - (unary ! x)).
    Expression parse_unary() {
        Depth depth(*this);
        std::vector<Token> prefixes;
        while (at("-") || at("!")) {
            depth.enter(current().location);
            prefixes.push_back(advance());
        }
        auto operand = parse_postfix();
        for (auto it = prefixes.rbegin(); it != prefixes.rend(); ++it) {
            auto node = make_expr("unary", it->location, it->text);
            node->span = {it->location, operand->span.end};
            node->args = {std::move(operand)};
            operand = std::move(node);
        }
        return operand;
    }

    // primary followed by any number of index suffixes: a[i], m[i,j], f(x)[0], a[0][1].
    Expression parse_postfix() {
        Depth depth(*this);
        auto target = parse_primary();
        while (at("[")) {
            const Token open = advance();
            depth.enter(open.location);
            auto node = make_expr("index", open.location);
            node->args.push_back(std::move(target));
            node->args.push_back(parse_expression());
            if (accept(",")) node->args.push_back(parse_expression());
            if (at(","))
                fail(current().location,
                     "Indexing takes at most two indices (vector[i] or matrix[i,j])");
            expect("]", "to close the index opened at " + where(open.location));
            node->span = {node->args.front()->span.begin, last_end()};
            target = std::move(node);
        }
        return target;
    }

    Expression parse_primary() {
        const Token& token = current();
        switch (token.kind) {
        case TokenKind::Integer: {
            auto node = make_expr("literal", token.location);
            node->literal = Value::integer(integer_value(token));
            advance();
            return finish(node, token.location);
        }
        case TokenKind::Float: {
            auto node = make_expr("literal", token.location);
            node->literal = Value::real(float_value(token));
            advance();
            return finish(node, token.location);
        }
        case TokenKind::String: {
            auto node = make_expr("string", token.location, token.text);
            advance();
            return finish(node, token.location);
        }
        case TokenKind::Word: {
            if (token.text == "true" || token.text == "false") {
                auto node = make_expr("literal", token.location);
                node->literal = Value::boolean(token.text == "true");
                advance();
                return finish(node, token.location);
            }
            if (token.text == "Print")
                fail(token.location, "'Print' is a statement and cannot be used inside an expression");
            if (token.text == "error")
                fail(token.location, "'error' is a statement that stops the program and cannot be used inside an expression; write it on its own: error(\"message\");");
            if (is_keyword(token))
                fail(token.location, "Expected an expression but found keyword '" + token.text + "'");
            const Token name = advance();
            if (at("(")) return parse_call(name);
            return finish(make_expr("variable", name.location, name.text), name.location);
        }
        case TokenKind::Symbol:
            if (token.text == "(") {
                if (starts_sized_literal()) return parse_sized_literal();
                const Token open = advance();
                auto inner = parse_expression();
                expect(")", "to close the '(' opened at " + where(open.location));
                return finish(inner, open.location); // the span includes the parentheses
            }
            if (token.text == "[") return parse_bracket_literal();
            if (token.text == "+")
                fail(token.location, "Unary '+' is not supported; remove the '+' or write the operand directly");
            break;
        case TokenKind::End:
            break;
        }
        fail(token.location, "Expected an expression" + unexpected());
    }

    Expression parse_call(const Token& name) {
        const Token open = expect("(", "after function name '" + name.text + "'");
        auto node = make_expr("call", name.location, name.text);
        if (!at(")")) {
            do {
                node->args.push_back(parse_expression());
            } while (accept(","));
        }
        expect(")", "to close the argument list of '" + name.text + "' opened at " + where(open.location));
        return finish(node, name.location);
    }

    // "(" N "[" ...   or   "(" R "," C "[" ...
    bool starts_sized_literal() const {
        if (!at("(") || peek(1).kind != TokenKind::Integer) return false;
        if (is(peek(2), "[")) return true;
        return is(peek(2), ",") && peek(3).kind == TokenKind::Integer && is(peek(4), "[");
    }

    // (3 [1,2,3])  or  (2,2 [[1,2],[3,4]]): a literal with an explicit size that must match.
    Expression parse_sized_literal() {
        const Token open = advance(); // '('
        const Token size_token = current();
        const std::size_t rows = expect_dimension("as the literal size");
        std::optional<std::size_t> cols;
        if (accept(",")) cols = expect_dimension("as the column count of the literal");
        auto literal = parse_bracket_literal();
        const auto& shape = *literal->annotation;
        const std::string declared = std::to_string(rows) + (cols ? "," + std::to_string(*cols) : "");
        if (cols) {
            if (literal->op != "matrix")
                fail(literal->location, "Size " + declared + " describes a matrix but a vector literal follows; "
                                        "matrix rows must be bracketed, e.g. [[1,2],[3,4]]");
            if (shape.rows != rows || shape.cols != *cols)
                fail(size_token.location, "Matrix literal declares size " + declared + " but has " +
                                              std::to_string(shape.rows) + " row(s) of " +
                                              std::to_string(shape.cols) + " element(s)");
        } else {
            if (literal->op != "vector")
                fail(literal->location, "Size " + declared + " describes a vector but a matrix literal follows; "
                                        "use (rows,cols [[...]]) for matrices");
            if (shape.rows != rows)
                fail(size_token.location, "Vector literal declares size " + declared + " but has " +
                                              std::to_string(shape.rows) + " element(s)");
        }
        expect(")", "to close the sized literal opened at " + where(open.location));
        return finish(literal, open.location);
    }

    // [e1, e2, ...] -> vector; [[e11, e12], [e21, e22]] -> matrix (rectangular, flattened).
    Expression parse_bracket_literal() {
        Depth depth(*this, current().location);
        const Token open = expect("[", "to start a vector or matrix literal");
        if (at("]"))
            fail(current().location, "Vector literal must contain at least one element");
        if (!at("[")) {
            auto node = make_expr("vector", open.location);
            do {
                node->args.push_back(parse_expression());
            } while (accept(","));
            expect("]", "to close the vector literal opened at " + where(open.location));
            node->annotation = Type{Base::Int, 1, node->args.size(), 1};
            return finish(node, open.location);
        }
        auto node = make_expr("matrix", open.location);
        std::size_t rows = 0, cols = 0;
        do {
            const Token row_open = expect("[", "to start matrix row " + std::to_string(rows + 1));
            if (at("]"))
                fail(current().location, "Matrix row " + std::to_string(rows + 1) + " must contain at least one element");
            std::size_t count = 0;
            do {
                node->args.push_back(parse_expression());
                ++count;
            } while (accept(","));
            expect("]", "to close matrix row " + std::to_string(rows + 1) + " opened at " + where(row_open.location));
            if (rows == 0) cols = count;
            else if (count != cols)
                fail(row_open.location, "Matrix row " + std::to_string(rows + 1) + " has " + std::to_string(count) +
                                            " element(s) but row 1 has " + std::to_string(cols) +
                                            "; all rows must have the same length");
            ++rows;
        } while (accept(","));
        expect("]", "to close the matrix literal opened at " + where(open.location));
        node->annotation = Type{Base::Int, 2, rows, cols};
        return finish(node, open.location);
    }

    // ----- statements -------------------------------------------------------------------

    std::optional<Type> parse_type() {
        const Token& token = current();
        if (token.kind != TokenKind::Word) return std::nullopt;
        Type type;
        const std::string& word = token.text;
        if (word == "int" || word == "float" || word == "bool" || word == "string") {
            type.base = word == "int" ? Base::Int : word == "float" ? Base::Float : word == "bool" ? Base::Bool : Base::String;
            advance();
            return type;
        }
        if (word == "intvector" || word == "floatvector") {
            type.base = word == "intvector" ? Base::Int : Base::Float;
            type.rank = 1;
            advance();
            type.rows = expect_dimension("as the vector size after '" + word + "'");
            return type;
        }
        if (word == "intmatrix" || word == "floatmatrix") {
            type.base = word == "intmatrix" ? Base::Int : Base::Float;
            type.rank = 2;
            advance();
            type.rows = expect_dimension("as the row count after '" + word + "'");
            expect(",", "between the row and column counts of '" + word + "'");
            type.cols = expect_dimension("as the column count after '" + word + "'");
            return type;
        }
        return std::nullopt;
    }

    void expect_terminator(const std::string& what) {
        expect(";", "to end the " + what);
    }

    Statement parse_statement() {
        Depth depth(*this, current().location);
        const Token& token = current();
        if (token.kind == TokenKind::End)
            fail(token.location, "Expected a statement but found end of input");
        if (at("{")) {
            auto block = parse_block();
            expect_terminator("block");
            return finish(block, block->location); // as a statement, the span includes the ';'
        }
        if (at("if")) return parse_if();
        if (at("while")) return parse_while();
        if (at("for")) return parse_for();
        if (at("Print")) return parse_print();
        if (at("error")) return parse_error();
        if (at("return")) return parse_return();
        if (at("function"))
            fail(token.location, function_depth_ > 0
                                     ? "Functions cannot be nested; define this function at the top level of the program"
                                     : "A function can only be defined at the top level of the program, not inside a block or loop");
        if (at("import"))
            fail(token.location, std::string("An import can only appear at the top level of the program, not inside a ") +
                                     (function_depth_ > 0 ? "function body" : "block or loop"));
        if (at("else"))
            fail(token.location, "Unexpected 'else' without a matching 'if'");
        if (at("then"))
            fail(token.location, "Unexpected 'then' without a matching 'if'");
        if (at("}"))
            fail(token.location, "Unexpected '}' without a matching '{'");
        if (is_identifier(token)) return parse_assignment();
        if (auto type = parse_type()) return parse_declaration(token.location, *type);
        fail(token.location, std::string("Expected a statement (declaration, assignment, Print, error, block, if, while") +
                                 (function_depth_ > 0 ? ", for or return)" : block_depth_ > 0 ? " or for)" : ", for, function or import)") + unexpected());
    }

    Statement parse_declaration(Location start, const Type& type) {
        auto stmt = make_stmt("declare", start);
        stmt->declaration = type;
        stmt->name = expect_identifier("after type '" + type.str() + "'");
        if (at("="))
            fail(current().location, "Expected ':=' after variable name '" + stmt->name + "' but found '='; MyDSL uses ':=' for initialisation");
        if (at("("))
            fail(current().location, "Expected ':=' after variable name '" + stmt->name + "' but found '('; to define a function write: function " +
                                         type.str() + " " + stmt->name + "(...) { ... };");
        expect(":=", "after variable name '" + stmt->name + "' (declarations need an initial value)");
        stmt->exprs.push_back(parse_expression());
        expect_terminator("declaration of '" + stmt->name + "'");
        return finish(stmt, start);
    }

    // name := value;   name[i] := value;   name[i,j] := value;
    Statement parse_assignment() {
        const Token name = advance();
        auto stmt = make_stmt("assign", name.location, name.text);
        if (at("[")) {
            stmt->op = "assign_index";
            const Token open = advance();
            stmt->exprs.push_back(parse_expression());
            if (accept(",")) stmt->exprs.push_back(parse_expression());
            if (at(","))
                fail(current().location, "Indexing takes at most two indices (vector[i] or matrix[i,j])");
            expect("]", "to close the index opened at " + where(open.location));
        }
        if (at("="))
            fail(current().location, "Expected ':=' after '" + name.text + "' but found '='; MyDSL uses ':=' for assignment");
        if (at("("))
            fail(current().location, "A call to '" + name.text + "' cannot be used as a statement; assign its result to a variable or pass it to Print");
        if (stmt->op == "assign" && (is_identifier(current()) || current().kind == TokenKind::Integer))
            fail(name.location, "Unknown type '" + name.text + "' in declaration (supported types: " + kTypeList + ")");
        expect(":=", "after '" + name.text + "' to form an assignment");
        stmt->exprs.push_back(parse_expression());
        expect_terminator("assignment to '" + name.text + "'");
        return finish(stmt, name.location);
    }

    Statement parse_print() {
        const Token keyword = advance();
        auto stmt = make_stmt("print", keyword.location);
        const Token open = expect("(", "after 'Print'");
        if (at(")")) fail(current().location, "Print needs exactly one argument");
        stmt->exprs.push_back(parse_expression());
        if (at(",")) fail(current().location, "Print takes exactly one argument");
        expect(")", "to close the Print argument opened at " + where(open.location));
        expect_terminator("Print statement");
        return finish(stmt, keyword.location);
    }

    // error(message);   stops the program with the message (README "Execution rules")
    Statement parse_error() {
        const Token keyword = advance();
        auto stmt = make_stmt("error", keyword.location);
        const Token open = expect("(", "after 'error'");
        if (at(")")) fail(current().location, "error needs a message, e.g. error(\"the count must not be negative\");");
        stmt->exprs.push_back(parse_expression());
        if (at(",")) fail(current().location, "error takes exactly one argument, the message (use + and to_string to build it)");
        expect(")", "to close the error message opened at " + where(open.location));
        expect_terminator("error statement");
        return finish(stmt, keyword.location);
    }

    Statement parse_block() {
        const Token open = expect("{", "to start a block");
        auto block = make_stmt("block", open.location);
        ++block_depth_;
        while (!at("}")) {
            if (current().kind == TokenKind::End)
                fail(current().location, "Expected '}' to close the block opened at " + where(open.location) + " but found end of input");
            block->body.push_back(parse_statement());
        }
        --block_depth_;
        advance(); // '}'
        return finish(block, open.location);
    }

    Expression parse_parenthesised_condition(const std::string& keyword) {
        const Token open = expect("(", "after '" + keyword + "'");
        auto condition = parse_expression();
        expect(")", "to close the " + keyword + " condition opened at " + where(open.location));
        return condition;
    }

    Statement parse_if() {
        const Token keyword = advance();
        auto stmt = make_stmt("if", keyword.location);
        stmt->exprs.push_back(parse_parenthesised_condition("if"));
        expect("then", "after the if condition");
        if (!at("{")) fail(current().location, "Expected '{' after 'then' (the if body must be a block)" + unexpected());
        stmt->body.push_back(parse_block());
        if (accept("else")) {
            if (at("if"))
                fail(current().location, "'else if' is not supported; write the nested if inside an else block: else { if (...) then {...}; }");
            if (!at("{")) fail(current().location, "Expected '{' after 'else' (the else body must be a block)" + unexpected());
            stmt->body.push_back(parse_block());
        }
        expect_terminator("if statement");
        return finish(stmt, keyword.location);
    }

    Statement parse_while() {
        const Token keyword = advance();
        auto stmt = make_stmt("while", keyword.location);
        stmt->exprs.push_back(parse_parenthesised_condition("while"));
        if (!at("{")) fail(current().location, "Expected '{' after the while condition (the loop body must be a block)" + unexpected());
        stmt->body.push_back(parse_block());
        expect_terminator("while loop");
        return finish(stmt, keyword.location);
    }

    // for (name := initial, condition, step) { body };
    Statement parse_for() {
        const Token keyword = advance();
        auto stmt = make_stmt("for", keyword.location);
        const Token open = expect("(", "after 'for'");
        if (is_type_keyword(current()))
            fail(current().location, "The for loop variable must not have a type; write for (i := 0, i < n, 1)");
        stmt->name = expect_identifier("as the loop variable after 'for ('");
        expect(":=", "after the loop variable '" + stmt->name + "'");
        stmt->exprs.push_back(parse_expression());
        expect(",", "after the initial value of '" + stmt->name + "' (for loop header is: variable := initial, condition, step)");
        stmt->exprs.push_back(parse_expression());
        expect(",", "after the loop condition (for loop header is: variable := initial, condition, step)");
        stmt->exprs.push_back(parse_expression());
        expect(")", "to close the for loop header opened at " + where(open.location));
        if (!at("{")) fail(current().location, "Expected '{' after the for loop header (the loop body must be a block)" + unexpected());
        stmt->body.push_back(parse_block());
        expect_terminator("for loop");
        return finish(stmt, keyword.location);
    }

    // return value;   (only inside a function body)
    Statement parse_return() {
        const Token keyword = advance();
        if (function_depth_ == 0)
            fail(keyword.location, "'return' can only be used inside a function body");
        auto stmt = make_stmt("return", keyword.location);
        if (at(";"))
            fail(current().location, "Expected a value after 'return' (every function returns a value of its declared type)");
        stmt->exprs.push_back(parse_expression());
        expect_terminator("return statement");
        return finish(stmt, keyword.location);
    }

    // function type name(type param, ...) { body };   (top level only)
    Statement parse_function() {
        Depth depth(*this, current().location);
        const Token keyword = advance();
        auto stmt = make_stmt("function", keyword.location);
        if (at("void"))
            fail(current().location, "Functions must return a value; there is no 'void' return type (supported types: " +
                                         std::string(kTypeList) + ")");
        if (is_identifier(current()) && is(peek(1), "("))
            fail(current().location, "Expected a return type after 'function' but found '" + current().text +
                                         "'; write: function <type> " + current().text + "(...) { ... }; (supported types: " + kTypeList + ")");
        auto type = parse_type();
        if (!type)
            fail(current().location, "Expected a return type after 'function' (" + std::string(kTypeList) + ")" + unexpected());
        stmt->declaration = *type;
        stmt->name = expect_identifier("after the return type '" + type->str() + "'", "function");
        if (at(":="))
            fail(current().location, "Expected '(' after function name '" + stmt->name + "' but found ':='; a function needs a parameter list, e.g. function " +
                                         type->str() + " " + stmt->name + "() { ... };");
        const Token open = expect("(", "after function name '" + stmt->name + "' to start the parameter list");
        if (!at(")")) {
            do {
                if (at(")"))
                    fail(current().location, "Expected a parameter after ',' in the parameter list of '" + stmt->name + "' but found ')'");
                Parameter parameter;
                parameter.location = current().location;
                if (is_identifier(current()) && (is(peek(1), ",") || is(peek(1), ")")))
                    fail(current().location, "Parameter '" + current().text + "' of function '" + stmt->name + "' needs a type, e.g. int " +
                                                 current().text + " (supported types: " + kTypeList + ")");
                auto parameter_type = parse_type();
                if (!parameter_type)
                    fail(current().location, "Expected a parameter type (" + std::string(kTypeList) + ") in the parameter list of '" +
                                                 stmt->name + "'" + unexpected());
                parameter.type = *parameter_type;
                parameter.name = expect_identifier("after parameter type '" + parameter_type->str() + "' of function '" + stmt->name + "'", "parameter");
                if (at(":=") || at("="))
                    fail(current().location, "Parameters cannot have default values (found '" + current().text + "' after parameter '" + parameter.name + "')");
                parameter.span = {parameter.location, last_end()};
                stmt->params.push_back(std::move(parameter));
            } while (accept(","));
        }
        expect(")", "to close the parameter list of '" + stmt->name + "' opened at " + where(open.location));
        if (at(";"))
            fail(current().location, "Expected '{' to start the body of function '" + stmt->name + "' but found ';' (a function must have a body)");
        if (!at("{")) fail(current().location, "Expected '{' to start the body of function '" + stmt->name + "'" + unexpected());
        ++function_depth_;
        stmt->body.push_back(parse_block());
        --function_depth_;
        expect_terminator("definition of function '" + stmt->name + "'");
        return finish(stmt, keyword.location);
    }

    // import "file";   (top level only; the file is resolved and loaded by load_modules)
    Statement parse_import() {
        const Token keyword = advance();
        auto stmt = make_stmt("import", keyword.location);
        const Token& token = current();
        if (token.kind != TokenKind::String)
            fail(token.location, "Expected the file name as a string literal after 'import' (e.g. import \"math_utils.mydsl\";)" + unexpected());
        if (token.text.empty())
            fail(token.location, "The file name of an import must not be empty (e.g. import \"math_utils.mydsl\";)");
        stmt->name = advance().text;
        expect_terminator("import of \"" + stmt->name + "\"");
        return finish(stmt, keyword.location);
    }

public:
    explicit Parser(const std::string& source) : tokens_(lex(source)) {}

    Program parse_program() {
        Program program;
        while (current().kind != TokenKind::End)
            program.push_back(at("function") ? parse_function() : at("import") ? parse_import() : parse_statement());
        return program;
    }
};
} // namespace

const std::set<std::string>& keywords() { return kKeywords; }

Program parse(const std::string& source) {
    return Parser(source).parse_program();
}
} // namespace mydsl

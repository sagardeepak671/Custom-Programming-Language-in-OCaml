#include "mydsl.hpp"
#include <cctype>
#include <cstdio>

namespace mydsl {
std::vector<Token> lex(const std::string& source) {
    std::vector<Token> tokens;
    std::size_t i = 0;
    Location p;
    auto peek = [&](std::size_t n = 0) { return i + n < source.size() ? source[i+n] : '\0'; };
    auto advance = [&]() { char c = source[i++]; if (c == '\n') { ++p.line; p.column = 1; }
                          else ++p.column;
                          return c; };
    auto digit = [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; };
    auto alpha = [](char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '\''; };
    while (i < source.size()) {
        if (std::isspace(static_cast<unsigned char>(peek()))) { advance(); continue; }
        const auto start = p;
        if (peek() == '/' && peek(1) == '/') {
            while (i < source.size() && peek() != '\n') advance();
            continue;
        }
        if (peek() == '/' && peek(1) == '*') {
            advance(); advance();
            while (i < source.size() && !(peek() == '*' && peek(1) == '/')) advance();
            if (i == source.size()) throw Error("Lexing", start, "Unterminated block comment");
            advance(); advance(); continue;
        }
        std::string text;
        if (alpha(peek())) {
            while (alpha(peek()) || digit(peek())) text += advance();
            tokens.push_back({TokenKind::Word, text, start, p}); continue;
        }
        if (digit(peek()) || (peek() == '.' && digit(peek(1)))) {
            bool real = false;
            while (digit(peek())) text += advance();
            if (peek() == '.') { real = true; text += advance(); while (digit(peek())) text += advance(); }
            if (peek() == 'e' || peek() == 'E') {
                real = true; text += advance();
                if (peek() == '+' || peek() == '-') text += advance();
                if (!digit(peek())) throw Error("Lexing", start, "Invalid exponent");
                while (digit(peek())) text += advance();
            }
            tokens.push_back({real ? TokenKind::Float : TokenKind::Integer, text, start, p}); continue;
        }
        // String literal: "..." on one line; escapes \" \\ \n \t \r (the token text holds the
        // unescaped characters).
        if (peek() == '"') {
            advance();
            while (i < source.size() && peek() != '"' && peek() != '\n') {
                const Location escape = p;
                char c = advance();
                if (c == '\\') {
                    if (i == source.size() || peek() == '\n')
                        throw Error("Lexing", start, "Unterminated string literal (missing the closing '\"')");
                    c = advance();
                    switch (c) {
                    case '"': case '\\': break;
                    case 'n': c = '\n'; break;
                    case 't': c = '\t'; break;
                    case 'r': c = '\r'; break;
                    default:
                        throw Error("Lexing", escape, std::string("Unknown escape sequence '\\") + c +
                                                          "' in string literal (supported: \\\" \\\\ \\n \\t \\r)");
                    }
                }
                text += c;
            }
            if (i == source.size() || peek() == '\n')
                throw Error("Lexing", start, "Unterminated string literal (missing the closing '\"'; a string cannot span lines, use \\n for a line break)");
            advance(); tokens.push_back({TokenKind::String, text, start, p}); continue;
        }
        std::string pair{peek(), peek(1)};
        if (pair == ":=" || pair == "==" || pair == "!=" || pair == "<=" || pair == ">=" || pair == "&&" || pair == "||") {
            advance(); advance(); text = pair;
        } else if (std::string("+-*/%()[]{},;<>!=.").find(peek()) != std::string::npos) text += advance();
        else if (static_cast<unsigned char>(peek()) >= 0x80) {
            // A non-ASCII character: report the whole UTF-8 sequence, never a partial one, so
            // the message itself stays valid text (the browser reads it as JSON).
            const unsigned char lead = static_cast<unsigned char>(peek());
            const std::size_t length = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
            std::string character;
            for (std::size_t k = 0; k < length && i + k < source.size(); ++k) {
                if (k > 0 && (static_cast<unsigned char>(source[i + k]) & 0xC0) != 0x80) break;
                character += source[i + k];
            }
            const bool valid = length > 1 && character.size() == length;
            std::string shown;
            if (valid) shown = "'" + character + "'";
            else { char hex[16]; std::snprintf(hex, sizeof hex, "byte 0x%02X", lead); shown = hex; }
            throw Error("Lexing", p, "Unexpected character " + shown + " (non-ASCII text is only allowed inside string literals and comments)");
        }
        else throw Error("Lexing", p, "Unexpected character '" + std::string(1, peek()) + "'");
        tokens.push_back({TokenKind::Symbol, text, start, p});
    }
    tokens.push_back({TokenKind::End, "<eof>", p, p});
    return tokens;
}
} // namespace mydsl

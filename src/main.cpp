#include "mydsl.hpp"
#include <fstream>
#include <iostream>
#include <iterator>

int main(int argc, char** argv) {
    bool quiet = false, check = false, ast = false, json = false, run_json = false, serve = false, tokens = false, modules = false, language = false;
    int port = 8000;
    std::string filename, input_filename;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: mydsl [--quiet] [--tokens] [--ast] [--modules] [--check] [--json] [--run-json] [--input DATA_FILE] [SOURCE_FILE|-]\n"
                         "       mydsl --serve [PORT]\n"
                         "Parses, type-checks and runs the program (source defaults to stdin); Print output goes to\n"
                         "stdout, errors to stderr with exit status 1. Input() reads from stdin or --input DATA_FILE.\n"
                         "--quiet  run but discard the program's output (only errors are reported).\n"
                         "--tokens print the tokens instead of running.\n"
                         "--ast    parse the source and print the AST (exit status 1 on a syntax error).\n"
                         "--modules  load the program and the files it imports (import \"file\"; resolved relative to\n"
                         "         the importing file) and list the modules in dependency order with their imports\n"
                         "         and functions (exit status 1 on a missing file, duplicate or circular import,\n"
                         "         or a syntax error in any file).\n"
                         "--json   print tokens, AST, types and any lexer/parser/type error as JSON (always exits 0).\n"
                         "--run-json  run and print output, error and timing as JSON (what the browser's Run uses).\n"
                         "--language-json  print the keywords and builtin functions with descriptions as JSON (what\n"
                         "         the browser editor's completion uses; no source file).\n"
                         "--serve  serve the browser debugging page on http://127.0.0.1:PORT/ (default 8000,\n"
                         "         0 picks a free port): tokens, AST and types on every edit, execution on Run.\n"
                         "--check  parse and type-check; print the top-level variable types and the function\n"
                         "         signatures (exit 1 on an error).\n"
                         "--input  read the program's Input() values from DATA_FILE instead of stdin.\n";
            return 0;
        }
        if (arg == "--quiet" || arg == "-q") quiet = true;
        else if (arg == "--tokens") tokens = true;
        else if (arg == "--ast") ast = true;
        else if (arg == "--modules") modules = true;
        else if (arg == "--json") json = true;
        else if (arg == "--run-json") run_json = true;
        else if (arg == "--language-json") language = true;
        else if (arg == "--serve") {
            serve = true;
            if (i + 1 < argc && std::string(argv[i + 1]).find_first_not_of("0123456789") == std::string::npos)
                port = std::stoi(argv[++i]);
        }
        else if (arg == "--check") check = true;
        else if (arg == "--input" && i + 1 < argc) input_filename = argv[++i];
        else if ((arg.size() > 1 && arg[0] == '-') || !filename.empty()) {
            std::cerr << "Usage error: unexpected argument " << arg << '\n'; return 2;
        } else filename = arg;
    }
    if (serve) {
        if (!filename.empty()) { std::cerr << "Usage error: --serve does not take a source file\n"; return 2; }
        return mydsl::serve_debug_page(port, std::cerr);
    }
    if (language) {
        if (!filename.empty()) { std::cerr << "Usage error: --language-json does not take a source file\n"; return 2; }
        std::cout << mydsl::language_json();
        return 0;
    }
    try {
        std::ifstream source_file, input_file;
        std::istream* source = &std::cin;
        if (!filename.empty() && filename != "-") {
            source_file.open(filename);
            if (!source_file) throw std::runtime_error("I/O error: cannot open source file: " + filename);
            source = &source_file;
        }
        std::istream* input = &std::cin;
        if (!input_filename.empty()) {
            input_file.open(input_filename);
            if (!input_file) throw std::runtime_error("I/O error: cannot open runtime input file: " + input_filename);
            input = &input_file;
        }
        std::string text{std::istreambuf_iterator<char>(*source), {}};
        const std::string source_path = filename == "-" ? "" : filename; // imports resolve relative to it
        if (tokens) { mydsl::print_tokens(mydsl::lex(text), std::cout); return 0; }
        if (json) { std::cout << mydsl::debug_json(text, source_path); return 0; }
        if (run_json) {
            std::string data;
            if (!input_filename.empty()) data.assign(std::istreambuf_iterator<char>(*input), {});
            std::cout << mydsl::run_json(text, data, {}, source_path);
            return 0;
        }
        if (ast) { mydsl::print_ast(mydsl::parse(text), std::cout); return 0; }
        if (modules) { mydsl::print_modules(mydsl::load_modules(text, source_path), std::cout); return 0; }
        mydsl::run(text, *input, std::cout, quiet, check, source_path);
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

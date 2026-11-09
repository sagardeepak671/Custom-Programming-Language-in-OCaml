// Module loader: turns a main program and its `import "file";` statements into the list of
// modules of a multi-file program (README "Modules"). The rules:
//
//   * The main program is parsed first, exactly as a single-file program; its own lexing or
//     parse error is reported unchanged. A program without imports never touches the file
//     system.
//   * Every import statement names a file. A relative name is resolved against the directory
//     of the file that contains the import (not the working directory), so a module can be
//     imported from anywhere; the main program read from standard input (or the browser page)
//     resolves its imports against the working directory. Absolute names are used as they are.
//     The name is taken literally: no extension is added and no search path is consulted.
//   * Imported files go through the same lexer and parser. A lexing or parse error in an
//     imported file is relayed with the file's path and the chain of imports that led to it
//     (innermost first), e.g. "Parse error in 'lib/a.mydsl' at line 2, column 3: ... (imported
//     from the main program at line 1, column 1)".
//   * A module is identified by its canonical path (symbolic links and ".." resolved), so the
//     same file written in two ways is the same module. A module is loaded once: when several
//     modules import it (a "diamond") later imports refer to the loaded copy. Importing the
//     same file twice from one module is reported as a duplicate import.
//   * The modules are loaded depth first; a module that is still being loaded and is imported
//     again closes a cycle (a module importing itself is the shortest one), which is reported
//     with the whole cycle. The nesting of imports is limited (kMaxImportDepth) so that a long
//     chain of distinct files cannot exhaust the stack.
//   * The result lists the modules in dependency order (post-order of the depth-first walk):
//     each module after every module it imports, the main program last.
//
// Nothing here type-checks or executes anything: the loader produces the modules and their
// ASTs; typecheck(const Modules&) and interpret(const Modules&) consume them.
#include "mydsl.hpp"
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>

namespace mydsl {
namespace fs = std::filesystem;
namespace {
// Longest chain main -> a -> b -> ... of nested imports that is accepted.
constexpr std::size_t kMaxImportDepth = 100;

std::string where(Location p) {
    return "line " + std::to_string(p.line) + ", column " + std::to_string(p.column);
}

// The working directory as the C++ library reports it (what fs::absolute prepends to relative
// paths, so relative and absolute spellings of one file compare equal lexically).
fs::path working_directory() {
    std::error_code failed;
    const fs::path here = fs::current_path(failed);
    return failed ? fs::path(".") : here.lexically_normal();
}

// How a path is shown in messages: relative to the working directory when the file is inside
// it, otherwise absolute; always with '/' separators. `logical` is the absolute, lexically
// normalised path the file was reached through (symbolic links not resolved).
std::string display_path(const fs::path& logical) {
    const fs::path relative = logical.lexically_relative(working_directory());
    if (relative.empty() || relative.native().compare(0, 2, "..") == 0) return logical.generic_string();
    return relative.generic_string();
}

// The absolute path of `path` as written: "." and ".." removed lexically, symbolic links kept.
fs::path logical_path(const fs::path& path) {
    std::error_code failed;
    const fs::path absolute = fs::absolute(path, failed);
    return (failed ? path : absolute).lexically_normal();
}

// Absolute path with symbolic links and "." / ".." resolved as the operating system would.
fs::path canonical_path(const fs::path& path) {
    std::error_code failed;
    const fs::path absolute = fs::absolute(path, failed);
    if (failed) return path.lexically_normal();
    const fs::path resolved = fs::weakly_canonical(absolute, failed);
    return failed ? absolute.lexically_normal() : resolved;
}

class Loader {
    const FileOverlay& overlay_;
    Modules result_;
    std::map<std::string, std::size_t> loaded_; // canonical path -> index in result_.modules (finished modules only)
    // Modules being loaded, outermost first: the main program and the chain of imports that
    // led to the current file. `imported_at` is the import statement in the previous frame;
    // `logical` is the module's absolute path as reached (see logical_path; empty for standard
    // input), against which its relative imports are resolved.
    struct Frame { const Module* module; Location imported_at; fs::path logical; };
    std::vector<Frame> stack_;

    static std::string name_of(const Module& m) { return m.display.empty() ? "the main program" : "'" + m.display + "'"; }

    // Error::file for an error located in `m`: the main program is never named.
    static std::string file_of(const Module& m) { return m.main ? "" : m.display; }

    // The import chain leading to the module on top of the stack, innermost first. With
    // `from` the chain starts at the import statement `from` in that module (for errors
    // located in the file it imports).
    std::vector<ImportSite> chain(const Location* from = nullptr) const {
        std::vector<ImportSite> sites;
        if (from) sites.push_back({file_of(*stack_.back().module), *from});
        for (std::size_t i = stack_.size(); i-- > 1;)
            sites.push_back({file_of(*stack_[i - 1].module), stack_[i].imported_at});
        return sites;
    }

    // An Import error at `at` in the module being loaded (top of the stack).
    [[noreturn]] void fail(Location at, const std::string& message) const {
        throw Error("Import", at, message).in_file(file_of(*stack_.back().module), chain());
    }

    std::string read_file(const fs::path& path, const std::string& spec, const std::string& shown, Location at) const {
        if (auto unsaved = overlay_.find(canonical_path(path).generic_string()); unsaved != overlay_.end()) return unsaved->second;
        errno = 0;
        std::ifstream file(path, std::ios::binary);
        if (!file) fail(at, "Cannot read imported file \"" + spec + "\" ('" + shown + "': " + (errno ? std::strerror(errno) : "unknown error") + ")");
        return std::string{std::istreambuf_iterator<char>(file), {}};
    }

    // Resolves and loads the imports of `module` (whose program is already parsed), then
    // appends it to the result. `module` is the current top of the stack.
    void load_imports(Module& module) {
        const fs::path& logical = stack_.back().logical;
        const fs::path base = logical.empty() ? working_directory() : logical.parent_path();
        struct Earlier { std::string spec, shown; Location at; };
        std::map<std::string, Earlier> seen; // canonical path -> first import of that file in this module
        for (const auto& statement : module.program) {
            if (statement->op != "import") continue;
            const std::string& spec = statement->name;
            const Location at = statement->location;
            if (spec.empty()) fail(at, "The file name of an import must not be empty");
            const fs::path target = fs::path(spec).is_absolute() ? fs::path(spec) : base / fs::path(spec);
            const fs::path canonical = canonical_path(target); // identity: what the operating system opens
            const std::string key = canonical.generic_string();
            const fs::path reached = logical_path(target);      // naming: the path as written
            const std::string shown = display_path(reached);
            // How a relative name was resolved, for the "cannot find" message.
            const std::string resolution = fs::path(spec).is_absolute() ? "" :
                module.path.empty() ? " (resolved to '" + shown + "' relative to the working directory, because the main program was read from standard input)"
                                    : " (resolved to '" + shown + "' relative to the directory of " + name_of(module) + ")";
            std::error_code ignored;
            if (!fs::exists(canonical, ignored))
                fail(at, "Cannot find imported file \"" + spec + "\"" + resolution);
            if (fs::is_directory(canonical, ignored))
                fail(at, "Cannot import \"" + spec + "\": '" + shown + "' is a directory, not a MyDSL source file");
            if (!fs::is_regular_file(canonical, ignored))
                fail(at, "Cannot import \"" + spec + "\": '" + shown + "' is not a regular file");
            if (auto earlier = seen.find(key); earlier != seen.end())
                fail(at, "Duplicate import of \"" + spec + "\": " + (earlier->second.spec == spec ? "it was" : "the same file '" + earlier->second.shown + "' was") +
                             " already imported at " + where(earlier->second.at) +
                             (earlier->second.spec == spec ? "" : " as \"" + earlier->second.spec + "\"") + "; remove one of the two imports");
            seen[key] = {spec, shown, at};
            // A module still being loaded: the import closes a cycle.
            for (std::size_t i = 0; i < stack_.size(); ++i) {
                if (stack_[i].module->path != key) continue;
                if (i + 1 == stack_.size())
                    fail(at, "Circular import: " + std::string(module.main ? "the main program" : "'" + module.display + "'") + " imports itself");
                std::string cycle;
                for (std::size_t k = i; k < stack_.size(); ++k) cycle += (k == i ? "" : " -> ") + name_of(*stack_[k].module);
                cycle += " -> " + name_of(*stack_[i].module);
                fail(at, "Circular import: " + cycle + " (a module cannot import itself, directly or through other modules)");
            }
            if (auto done = loaded_.find(key); done != loaded_.end()) { // already loaded through another module
                module.imports.push_back({spec, at, done->second});
                continue;
            }
            if (stack_.size() >= kMaxImportDepth)
                fail(at, "Import nesting is too deep: the limit is " + std::to_string(kMaxImportDepth) + " levels of imports (is there a very long chain of modules?)");
            Module child;
            child.path = key;
            child.display = shown;
            child.chain = chain(&at);
            child.source = read_file(target, spec, shown, at);
            try {
                child.program = parse(child.source);
            } catch (const Error& error) {
                throw error.in_file(child.display, chain(&at)); // the file's own lexing/parse error, located in that file
            }
            stack_.push_back({&child, at, reached});
            load_imports(child);
            stack_.pop_back();
            module.imports.push_back({spec, at, result_.modules.size() - 1});
        }
        loaded_[module.path] = result_.modules.size();
        result_.modules.push_back(std::move(module));
    }

public:
    explicit Loader(const FileOverlay& overlay) : overlay_(overlay) {}
    Modules load(const std::string& source, const std::string& path) {
        Module main;
        main.main = true;
        fs::path logical;
        if (!path.empty()) {
            main.path = canonical_path(path).generic_string();
            logical = logical_path(path);
            main.display = display_path(logical);
        }
        main.source = source;
        main.program = parse(source); // a lexing/parse error of the main program is reported as for a single-file program
        stack_.push_back({&main, {}, logical});
        load_imports(main);
        stack_.pop_back();
        return std::move(result_);
    }
};
} // namespace

std::size_t Module::functions() const {
    std::size_t n = 0;
    for (const auto& s : program) n += s->op == "function";
    return n;
}

std::size_t Module::statements() const {
    std::size_t n = 0;
    for (const auto& s : program) n += s->op != "function" && s->op != "import";
    return n;
}

Modules load_modules(const std::string& main_source, const std::string& main_path, const FileOverlay& overlay) {
    return Loader(overlay).load(main_source, main_path);
}

Modules single_module(const Program& program) {
    Modules modules;
    Module main;
    main.main = true;
    main.program = program;
    modules.modules.push_back(std::move(main));
    return modules;
}

// main.mydsl imports lib/math_utils.mydsl, which imports lib/vectors.mydsl:
//   lib/vectors.mydsl
//     functions: norm, scale
//   lib/math_utils.mydsl
//     imports: "vectors.mydsl" at line 1, column 1 -> lib/vectors.mydsl
//     functions: square, mean
//   main.mydsl (main program)
//     imports: "lib/math_utils.mydsl" at line 1, column 1 -> lib/math_utils.mydsl
//     functions: (none)
//     other top-level statements: 2
//   3 modules in dependency order (each module after the modules it imports, the main program last)
void print_modules(const Modules& modules, std::ostream& out) {
    for (const auto& m : modules.modules) {
        out << (m.display.empty() ? "<stdin>" : m.display) << (m.main ? " (main program)" : "") << '\n';
        if (!m.imports.empty()) {
            out << "  imports:";
            for (std::size_t i = 0; i < m.imports.size(); ++i) {
                const Import& import = m.imports[i];
                const Module& target = modules.modules.at(import.module);
                out << (i ? "\n           " : " ") << quote_string(import.path) << " at " << where(import.location) << " -> " << target.display;
            }
            out << '\n';
        }
        out << "  functions:";
        bool any = false;
        for (const auto& s : m.program)
            if (s->op == "function") { out << (any ? ", " : " ") << s->name; any = true; }
        out << (any ? "" : " (none)") << '\n';
        if (const std::size_t other = m.statements()) out << "  other top-level statements: " << other << '\n';
    }
    const std::size_t n = modules.modules.size();
    out << n << (n == 1 ? " module" : " modules");
    if (n == 1) out << " (the program has no imports)\n";
    else out << " in dependency order (each module after the modules it imports, the main program last)\n";
}
} // namespace mydsl

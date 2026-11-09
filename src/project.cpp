// The project files of the browser page (`mydsl --serve`): listing, reading and writing the
// MyDSL files below the directory the server was started in, and nothing outside it.
//
// Safety rules (every route of the file API goes through project_path):
//   * A request names a file by a path relative to the root. It must be plain: no leading '/',
//     no "." or ".." components, no empty component, no backslash, no control character.
//   * The result is canonicalised (symbolic links resolved) and must still lie inside the
//     canonical root, so a link pointing elsewhere cannot be used to leave the root.
//   * Only regular files with the extensions in kExtensions are listed, read or written;
//     hidden entries (".git", ".env"), "node_modules" and "build" are neither listed nor
//     reachable by path, and symbolic links to directories are not followed when listing.
//   * A file is at most kMaxFileSize bytes. Writing goes to a temporary file in the same
//     directory that is then renamed over the original (permissions kept), so a failure
//     halfway never leaves a truncated file; a symbolic link is not overwritten.
//   * Nothing is deleted or renamed; a new file may only be created in an existing directory.
// Errors are ProjectError{http status, message} so the server can answer precisely.
#include "mydsl.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <unistd.h>

namespace mydsl {
namespace fs = std::filesystem;
namespace {
const char* const kExtensions[] = {".mydsl", ".txt"};
constexpr std::size_t kMaxFileSize = 1u << 20;   // 1 MiB
constexpr std::size_t kMaxEntries = 5000;
constexpr int kMaxDepth = 12;

bool allowed_extension(const fs::path& path) {
    const std::string extension = path.extension().string();
    return std::find(std::begin(kExtensions), std::end(kExtensions), extension) != std::end(kExtensions);
}

bool hidden(const std::string& name) {
    return name.empty() || name[0] == '.' || name == "node_modules" || name == "build";
}

void json_string(std::ostream& out, const std::string& text) {
    out << '"';
    for (const unsigned char c : text) {
        if (c == '"') out << "\\\"";
        else if (c == '\\') out << "\\\\";
        else if (c == '\n') out << "\\n";
        else if (c == '\r') out << "\\r";
        else if (c == '\t') out << "\\t";
        else if (c < 0x20) out << "\\u00" << "0123456789abcdef"[c >> 4] << "0123456789abcdef"[c & 0xf];
        else out << c;
    }
    out << '"';
}

fs::path canonical_root(const std::string& root) {
    std::error_code failed;
    const fs::path canonical = fs::canonical(root.empty() ? fs::current_path() : fs::path(root), failed);
    if (failed) throw ProjectError{500, "The project directory cannot be resolved: " + failed.message()};
    return canonical;
}

bool inside(const fs::path& path, const fs::path& root) {
    const std::string p = path.generic_string(), r = root.generic_string();
    return p == r || (p.size() > r.size() && p.compare(0, r.size(), r) == 0 && (r == "/" || p[r.size()] == '/'));
}

bool writable(const fs::path& path) { return ::access(path.c_str(), W_OK) == 0; }

void list(const fs::path& directory, const fs::path& root, int depth, std::vector<std::pair<std::string, bool>>& entries, bool& truncated) {
    if (depth > kMaxDepth) return;
    std::error_code failed;
    std::vector<fs::directory_entry> children;
    for (fs::directory_iterator it(directory, fs::directory_options::skip_permission_denied, failed), end; !failed && it != end; it.increment(failed))
        children.push_back(*it);
    std::sort(children.begin(), children.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) { return a.path().filename() < b.path().filename(); });
    for (const fs::directory_entry& child : children) {
        const std::string name = child.path().filename().string();
        if (hidden(name)) continue;
        if (entries.size() >= kMaxEntries) { truncated = true; return; }
        std::error_code ignored;
        const fs::file_status status = child.symlink_status(ignored); // never follow links when listing
        if (fs::is_directory(status)) {
            const std::size_t before = entries.size();
            entries.emplace_back(child.path().lexically_relative(root).generic_string(), true);
            list(child.path(), root, depth + 1, entries, truncated);
            if (entries.size() == before + 1) entries.pop_back(); // nothing of interest inside
        } else if (fs::is_regular_file(status) && allowed_extension(child.path())) {
            entries.emplace_back(child.path().lexically_relative(root).generic_string(), false);
        }
    }
}
} // namespace

std::string project_path(const std::string& root, const std::string& relative, bool must_exist) {
    if (relative.empty()) throw ProjectError{400, "Missing file path"};
    if (relative[0] == '/') throw ProjectError{400, "The path must be relative to the project directory, not absolute"};
    if (relative.size() > 1024) throw ProjectError{400, "The path is too long"};
    for (const unsigned char c : relative)
        if (c < 0x20 || c == 0x7f || c == '\\') throw ProjectError{400, "The path contains an invalid character"};
    std::size_t start = 0;
    while (start <= relative.size()) {
        const std::size_t slash = relative.find('/', start);
        const std::string component = relative.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (component.empty() || component == "." || component == "..")
            throw ProjectError{400, "The path must not contain empty, '.' or '..' components"};
        if (hidden(component)) throw ProjectError{403, "Hidden entries, node_modules and build are not part of the project: " + relative};
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    const fs::path root_path = canonical_root(root);
    const fs::path logical = root_path / relative;
    std::error_code failed;
    fs::path resolved = fs::canonical(logical, failed);
    if (failed) {
        if (must_exist) throw ProjectError{404, "No such file in the project: " + relative};
        // A file that does not exist yet: its directory must exist inside the root.
        const fs::path parent = fs::canonical(logical.parent_path(), failed);
        if (failed) throw ProjectError{404, "No such directory in the project: " + logical.parent_path().lexically_relative(root_path).generic_string()};
        resolved = parent / logical.filename();
    }
    if (!inside(resolved, root_path)) throw ProjectError{403, "The path leaves the project directory: " + relative};
    if (!allowed_extension(resolved)) throw ProjectError{403, "Only .mydsl and .txt files can be opened or saved: " + relative};
    if (must_exist && !fs::is_regular_file(resolved, failed)) throw ProjectError{404, "Not a regular file: " + relative};
    return resolved.string();
}

std::string project_tree_json(const std::string& root) {
    const fs::path root_path = canonical_root(root);
    std::vector<std::pair<std::string, bool>> entries;
    bool truncated = false;
    list(root_path, root_path, 0, entries, truncated);
    std::ostringstream out;
    out << "{\"root\":"; json_string(out, root_path.generic_string());
    out << ",\"truncated\":" << (truncated ? "true" : "false") << ",\"entries\":[";
    bool first = true;
    for (const auto& [path, directory] : entries) {
        out << (first ? "" : ",") << "{\"path\":"; json_string(out, path);
        out << ",\"dir\":" << (directory ? "true" : "false");
        if (!directory) out << ",\"writable\":" << (writable(root_path / path) ? "true" : "false");
        out << '}';
        first = false;
    }
    out << "]}\n";
    return out.str();
}

std::string project_file_json(const std::string& root, const std::string& relative) {
    const fs::path path = project_path(root, relative, true);
    std::error_code failed;
    const auto size = fs::file_size(path, failed);
    if (failed) throw ProjectError{500, "Cannot read " + relative + ": " + failed.message()};
    if (size > kMaxFileSize) throw ProjectError{413, "The file is larger than 1 MiB: " + relative};
    std::ifstream file(path, std::ios::binary);
    if (!file) throw ProjectError{errno == EACCES ? 403 : 500, "Cannot read " + relative + ": " + std::strerror(errno)};
    const std::string content{std::istreambuf_iterator<char>(file), {}};
    std::ostringstream out;
    out << "{\"path\":"; json_string(out, relative);
    out << ",\"content\":"; json_string(out, content);
    out << ",\"writable\":" << (writable(path) ? "true" : "false") << "}\n";
    return out.str();
}

void write_project_file(const std::string& root, const std::string& relative, const std::string& content) {
    if (content.size() > kMaxFileSize) throw ProjectError{413, "The file is larger than 1 MiB: " + relative};
    const fs::path path = project_path(root, relative, false);
    std::error_code failed;
    const fs::file_status status = fs::symlink_status(path, failed);
    const bool exists = !failed && fs::exists(status);
    if (exists && fs::is_symlink(status)) throw ProjectError{403, "Refusing to overwrite a symbolic link: " + relative};
    if (exists && !fs::is_regular_file(status)) throw ProjectError{403, "Not a regular file: " + relative};
    if (exists && !writable(path)) throw ProjectError{403, "Cannot save " + relative + ": the file is read-only (Permission denied)"};
    if (!writable(path.parent_path())) throw ProjectError{403, "Cannot save " + relative + ": the directory is read-only (Permission denied)"};
    const fs::path temporary = path.parent_path() / (path.filename().string() + ".mydsl-saving");
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) throw ProjectError{errno == EACCES ? 403 : 500, "Cannot save " + relative + ": " + std::strerror(errno)};
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        file.flush();
        if (!file) { std::error_code ignored; fs::remove(temporary, ignored); throw ProjectError{500, "Cannot save " + relative + ": " + std::strerror(errno)}; }
    }
    if (exists) fs::permissions(temporary, status.permissions(), failed); // keep the file's mode
    fs::rename(temporary, path, failed);
    if (failed) { std::error_code ignored; fs::remove(temporary, ignored); throw ProjectError{500, "Cannot save " + relative + ": " + failed.message()}; }
}
} // namespace mydsl

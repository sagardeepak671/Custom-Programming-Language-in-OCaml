// Minimal local HTTP server for the browser debugging page (`mydsl --serve`).
//
// Routes:
//   GET  /               the page from web/index.html (embedded into the binary at build time)
//   GET  /vendor/mydsl-editor.js   the editor (CodeMirror + the MyDSL mode) from web/vendor, also embedded
//   POST /api/parse[?path=FILE]   request body = program text; response = debug_json(body)
//   POST /api/run[?path=FILE]     request body = "<source byte count>\n" + source + standard input text;
//                        response = run_json(source, input, kRunLimits)
//                        `path` (a project file, see below) makes imports resolve relative to
//                        that file and names it in messages, as running the file would.
//                        Both bodies may start with the unsaved texts of other open files,
//                        which imports then use instead of the files on disk (FileOverlay):
//                          "#files <count>\n" then per file "<path bytes> <content bytes>\n<path><content>"
//                        followed by the body described above. Paths are project files.
//   GET  /api/examples   {"examples": [{"name": "...", "source": "..."}]} from ./inputs/*.txt
//   GET  /api/language   language_json(): keywords and builtins with descriptions (for the editor's completion)
//   GET  /api/files      project_tree_json(): the .mydsl/.txt files below the working directory
//   GET  /api/file?path=FILE      project_file_json(): {"path", "content", "writable"}
//   PUT  /api/file?path=FILE      write_project_file(body); needs the header "X-Requested-With: mydsl";
//                        response {"ok": true} or an error status with {"error": "..."}
// The project is the directory the server was started in; src/project.cpp refuses every path
// that leads outside it (the page cannot reach other files on the machine). Every request
// must carry a Host header naming the loopback address (against DNS rebinding), the file API
// never answers cross-origin requests (no CORS headers, so browsers block other sites).
//
// Deliberately small: one request at a time, bound to 127.0.0.1 only, POSIX sockets, no
// dependencies. It is a development tool, not a production web server.
#include "mydsl.hpp"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace mydsl {
extern const char* const kDebugPage;    // generated from web/index.html by the makefile
extern const char* const kEditorScript; // generated from web/vendor/mydsl-editor.js by the makefile

namespace {
constexpr std::size_t kMaxBody = 16u << 20; // 16 MiB is far more than any program text
// Programs run inside the (single-threaded) server, so runaway programs are cut off: the page
// reports the limit as a runtime error and the server is free again after at most this long.
const Limits kRunLimits{2000.0, 256u << 10};

struct Request { std::string method, path, body, host, requested_with; bool ok = false; };

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

bool read_exact(int fd, std::string& buffer, std::size_t wanted) {
    while (buffer.size() < wanted) {
        char chunk[8192];
        const auto n = ::read(fd, chunk, sizeof chunk);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        buffer.append(chunk, static_cast<std::size_t>(n));
    }
    return true;
}

Request read_request(int fd) {
    Request request;
    std::string data;
    std::size_t header_end = std::string::npos;
    while (header_end == std::string::npos) {
        if (data.size() > 64 * 1024) return request; // absurd header block
        if (!read_exact(fd, data, data.size() + 1)) return request;
        header_end = data.find("\r\n\r\n");
    }
    std::istringstream head(data.substr(0, header_end));
    std::string version;
    head >> request.method >> request.path >> version;
    if (request.method.empty() || request.path.empty()) return request;
    std::size_t length = 0;
    std::string line;
    std::getline(head, line); // rest of the request line
    while (std::getline(head, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string name = lower(line.substr(0, colon));
        std::string value = line.substr(colon + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        if (name == "content-length") {
            try { length = std::stoul(value); } catch (...) { return request; }
        } else if (name == "host") request.host = value;
        else if (name == "x-requested-with") request.requested_with = value;
    }
    if (length > kMaxBody) return request;
    const std::size_t body_start = header_end + 4;
    if (!read_exact(fd, data, body_start + length)) return request;
    request.body = data.substr(body_start, length);
    request.ok = true;
    return request;
}

void respond(int fd, int status, const char* reason, const char* type, const std::string& body) {
    std::ostringstream out;
    out << "HTTP/1.1 " << status << ' ' << reason << "\r\n"
        << "Content-Type: " << type << "; charset=utf-8\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Cache-Control: no-store\r\nConnection: close\r\n\r\n" << body;
    const std::string text = out.str();
    std::size_t sent = 0;
    while (sent < text.size()) {
        const auto n = ::write(fd, text.data() + sent, text.size() - sent);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return;
        sent += static_cast<std::size_t>(n);
    }
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

// The value of `name` in the query string of `target` ("/api/file?path=a%2Fb.mydsl" -> "a/b.mydsl"),
// percent-decoded; "" when absent.
std::string query_parameter(const std::string& target, const std::string& name) {
    const auto question = target.find('?');
    if (question == std::string::npos) return "";
    std::string query = target.substr(question + 1);
    while (!query.empty()) {
        const auto amp = query.find('&');
        const std::string pair = query.substr(0, amp);
        query = amp == std::string::npos ? "" : query.substr(amp + 1);
        const auto eq = pair.find('=');
        if (pair.substr(0, eq) != name) continue;
        const std::string raw = eq == std::string::npos ? "" : pair.substr(eq + 1);
        std::string decoded;
        for (std::size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] == '+') decoded += ' ';
            else if (raw[i] == '%' && i + 2 < raw.size() && std::isxdigit(static_cast<unsigned char>(raw[i + 1])) && std::isxdigit(static_cast<unsigned char>(raw[i + 2]))) {
                decoded += static_cast<char>(std::stoi(raw.substr(i + 1, 2), nullptr, 16));
                i += 2;
            } else decoded += raw[i];
        }
        return decoded;
    }
    return "";
}

// The Host header must name this machine's loopback address: a web page elsewhere that makes
// the browser resolve its own name to 127.0.0.1 (DNS rebinding) is refused.
bool loopback_host(std::string host) {
    host = lower(host);
    if (!host.empty() && host[0] == '[') { const auto close = host.find(']'); host = close == std::string::npos ? host : host.substr(0, close + 1); }
    else host = host.substr(0, host.find(':'));
    return host == "127.0.0.1" || host == "localhost" || host == "[::1]";
}

// Splits the optional "#files" prefix off a parse/run body (see the routes above) into the
// overlay; returns the rest of the body. Throws ProjectError for malformed or refused entries.
std::string take_overlay(const std::string& body, FileOverlay& overlay) {
    const std::string magic = "#files ";
    if (body.compare(0, magic.size(), magic) != 0) return body;
    std::size_t pos = body.find('\n');
    if (pos == std::string::npos) throw ProjectError{400, "Malformed #files header"};
    std::size_t count = 0;
    try { count = std::stoul(body.substr(magic.size(), pos - magic.size())); } catch (...) { throw ProjectError{400, "Malformed #files header"}; }
    if (count > 64) throw ProjectError{400, "Too many unsaved files in the request (at most 64)"};
    ++pos;
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t newline = body.find('\n', pos);
        if (newline == std::string::npos) throw ProjectError{400, "Malformed #files entry"};
        std::size_t path_size = 0, content_size = 0;
        try {
            const std::string sizes = body.substr(pos, newline - pos);
            const std::size_t space = sizes.find(' ');
            if (space == std::string::npos) throw 0;
            path_size = std::stoul(sizes.substr(0, space));
            content_size = std::stoul(sizes.substr(space + 1));
        } catch (...) { throw ProjectError{400, "Malformed #files entry"}; }
        pos = newline + 1;
        if (path_size > 1024 || content_size > (1u << 20) || pos + path_size + content_size > body.size()) throw ProjectError{400, "Malformed #files entry (sizes)"};
        const std::string relative = body.substr(pos, path_size);
        const std::string canonical = project_path("", relative, false); // a project file, or refused
        overlay[std::filesystem::path(canonical).generic_string()] = body.substr(pos + path_size, content_size);
        pos += path_size + content_size;
    }
    return body.substr(pos);
}

std::string error_json(const std::string& message) {
    std::ostringstream out;
    out << "{\"error\":"; json_string(out, message); out << "}\n";
    return out.str();
}

// The example programs in ./inputs (relative to the working directory), sorted by name.
std::string examples_json() {
    namespace fs = std::filesystem;
    std::vector<fs::path> files;
    std::error_code ignored;
    for (const auto& entry : fs::directory_iterator("inputs", ignored))
        if (entry.is_regular_file() && entry.path().extension() == ".txt") files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    std::ostringstream out;
    out << "{\"examples\":[";
    bool first = true;
    for (const auto& path : files) {
        std::ifstream file(path);
        if (!file) continue;
        std::string text{std::istreambuf_iterator<char>(file), {}};
        if (!first) out << ',';
        first = false;
        out << "{\"name\":"; json_string(out, path.filename().string());
        out << ",\"source\":"; json_string(out, text);
        out << '}';
    }
    out << "]}\n";
    return out.str();
}

void handle(int fd) {
    const Request request = read_request(fd);
    if (!request.ok) { respond(fd, 400, "Bad Request", "text/plain", "Bad request\n"); return; }
    if (!loopback_host(request.host)) { respond(fd, 403, "Forbidden", "text/plain", "This server only answers requests addressed to 127.0.0.1 or localhost\n"); return; }
    const std::string path = request.path.substr(0, request.path.find('?'));
    // The project file named by ?path=, validated by src/project.cpp; "" when absent. For
    // parse/run also the unsaved texts of other files (the "#files" prefix of the body).
    std::string file, body = request.body;
    FileOverlay overlay;
    try {
        const std::string relative = query_parameter(request.path, "path");
        if (!relative.empty() || path == "/api/file") { project_path("", relative, request.method != "PUT"); file = relative; }
        if (path == "/api/parse" || path == "/api/run") body = take_overlay(request.body, overlay);
    } catch (const ProjectError& e) {
        respond(fd, e.status, e.status == 404 ? "Not Found" : e.status == 403 ? "Forbidden" : "Bad Request", "application/json", error_json(e.message));
        return;
    }
    if (request.method == "GET" && (path == "/" || path == "/index.html"))
        respond(fd, 200, "OK", "text/html", kDebugPage);
    else if (request.method == "GET" && path == "/vendor/mydsl-editor.js")
        respond(fd, 200, "OK", "application/javascript", kEditorScript);
    else if (request.method == "POST" && path == "/api/parse")
        respond(fd, 200, "OK", "application/json", debug_json(body, file, overlay));
    else if (request.method == "POST" && path == "/api/run") {
        const auto newline = body.find('\n');
        std::size_t length = std::string::npos;
        if (newline != std::string::npos) { try { length = std::stoul(body.substr(0, newline)); } catch (...) {} }
        if (length == std::string::npos || newline + 1 + length > body.size()) {
            respond(fd, 400, "Bad Request", "text/plain", "Expected \"<source byte count>\\n<source><input>\"\n");
            return;
        }
        const std::string source = body.substr(newline + 1, length);
        const std::string input = body.substr(newline + 1 + length);
        respond(fd, 200, "OK", "application/json", run_json(source, input, kRunLimits, file, overlay));
    }
    else if (path == "/api/files" || path == "/api/file") {
        try {
            if (request.method == "GET" && path == "/api/files") respond(fd, 200, "OK", "application/json", project_tree_json());
            else if (request.method == "GET") respond(fd, 200, "OK", "application/json", project_file_json("", file));
            else if (request.method == "PUT" && path == "/api/file") {
                if (request.requested_with != "mydsl") { respond(fd, 403, "Forbidden", "application/json", error_json("Saving needs the header X-Requested-With: mydsl (only the page itself sends it)")); return; }
                write_project_file("", file, request.body);
                respond(fd, 200, "OK", "application/json", "{\"ok\":true}\n");
            }
            else respond(fd, 405, "Method Not Allowed", "text/plain", "Method not allowed\n");
        } catch (const ProjectError& e) {
            respond(fd, e.status, e.status == 404 ? "Not Found" : e.status == 403 ? "Forbidden" : e.status == 413 ? "Payload Too Large" : e.status == 400 ? "Bad Request" : "Internal Server Error",
                    "application/json", error_json(e.message));
        }
    }
    else if (request.method == "GET" && path == "/api/examples")
        respond(fd, 200, "OK", "application/json", examples_json());
    else if (request.method == "GET" && path == "/api/language")
        respond(fd, 200, "OK", "application/json", language_json());
    else if (path == "/api/parse" || path == "/api/run" || path == "/api/examples" || path == "/api/language")
        respond(fd, 405, "Method Not Allowed", "text/plain", "Method not allowed\n");
    else
        respond(fd, 404, "Not Found", "text/plain", "Not found\n");
}
} // namespace

int serve_debug_page(int port, std::ostream& log) {
    if (port < 0 || port > 65535) { log << "Invalid port " << port << '\n'; return 2; }
    std::signal(SIGPIPE, SIG_IGN); // a browser closing a connection early must not kill us
    const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) { log << "socket: " << std::strerror(errno) << '\n'; return 1; }
    int yes = 1;
    ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<uint16_t>(port));
    if (::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof address) < 0 ||
        ::listen(listener, 16) < 0) {
        log << "Cannot listen on 127.0.0.1:" << port << ": " << std::strerror(errno) << '\n';
        ::close(listener);
        return 1;
    }
    socklen_t size = sizeof address;
    ::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size);
    log << "MyDSL debug page: http://127.0.0.1:" << ntohs(address.sin_port)
        << "/  (press Ctrl+C to stop)" << std::endl;
    for (;;) {
        const int client = ::accept(listener, nullptr, nullptr);
        if (client < 0) {
            if (errno == EINTR) continue;
            log << "accept: " << std::strerror(errno) << '\n';
            ::close(listener);
            return 1;
        }
        handle(client);
        ::close(client);
    }
}
} // namespace mydsl

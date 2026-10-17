#include "HttpServer.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <csignal>
#endif

namespace opera {

namespace {

#ifdef _WIN32
using SocketHandle = SOCKET;
const SocketHandle kInvalidSocket = INVALID_SOCKET;
void closeSocket(SocketHandle s) { closesocket(s); }
#else
using SocketHandle = int;
const SocketHandle kInvalidSocket = -1;
void closeSocket(SocketHandle s) { close(s); }
#endif

constexpr std::size_t kMaxHeaderBytes = 16 * 1024;
constexpr std::size_t kMaxBodyBytes = 64 * 1024;
constexpr int kTimeoutSeconds = 5;

void setTimeouts(SocketHandle s) {
#ifdef _WIN32
    const DWORD ms = kTimeoutSeconds * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms), sizeof ms);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&ms), sizeof ms);
#else
    timeval tv{};
    tv.tv_sec = kTimeoutSeconds;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
#endif
}

bool sendAll(SocketHandle s, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const int n = static_cast<int>(
            send(s, data.data() + sent, static_cast<int>(data.size() - sent), 0));
        if (n <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

const char* reasonPhrase(int status) {
    switch (status) {
        case 200: return "OK";
        case 204: return "No Content";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 429: return "Too Many Requests";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 413: return "Payload Too Large";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        default:  return "Unknown";
    }
}

std::string toLower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string contentTypeFor(const std::string& path) {
    const auto dot = path.rfind('.');
    const std::string ext = dot == std::string::npos ? "" : toLower(path.substr(dot));
    if (ext == ".html") return "text/html; charset=utf-8";
    if (ext == ".css")  return "text/css; charset=utf-8";
    if (ext == ".js")   return "text/javascript; charset=utf-8";
    if (ext == ".svg")  return "image/svg+xml";
    if (ext == ".png")  return "image/png";
    if (ext == ".ico")  return "image/x-icon";
    if (ext == ".txt")  return "text/plain; charset=utf-8";   // captcha.txt
    return "";  // anything else is not served
}

HttpResponse textResponse(int status, const std::string& text) {
    return HttpResponse{status, "text/plain; charset=utf-8", text + "\n"};
}

void writeResponse(SocketHandle s, const HttpResponse& res, bool headOnly,
                   const std::string& corsOrigin) {
    std::ostringstream out;
    out << "HTTP/1.1 " << res.status << ' ' << reasonPhrase(res.status) << "\r\n"
        << "Content-Type: " << res.contentType << "\r\n"
        << "Content-Length: " << res.body.size() << "\r\n"
        << "Cache-Control: no-store\r\n"
        << "X-Content-Type-Options: nosniff\r\n";
    if (!corsOrigin.empty()) {
        out << "Access-Control-Allow-Origin: " << corsOrigin << "\r\n"
            << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
            << "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
            << "Access-Control-Max-Age: 600\r\n"
            << "Vary: Origin\r\n";
    }
    out << "Connection: close\r\n\r\n";
    if (!headOnly) {
        out << res.body;
    }
    sendAll(s, out.str());
}

// Reads one request. Returns false if the client sent nothing usable;
// `error` is set (and should be sent back) when the request was malformed.
bool readRequest(SocketHandle s, HttpRequest& req, HttpResponse& error) {
    std::string data;
    char buf[4096];
    std::size_t headerEnd;
    while ((headerEnd = data.find("\r\n\r\n")) == std::string::npos) {
        if (data.size() > kMaxHeaderBytes) {
            error = textResponse(431, "Request headers are too large.");
            return false;
        }
        const int n = static_cast<int>(recv(s, buf, sizeof buf, 0));
        if (n <= 0) {
            return false;  // closed or timed out before a full request arrived
        }
        data.append(buf, static_cast<std::size_t>(n));
    }

    std::istringstream head(data.substr(0, headerEnd));
    std::string requestLine;
    std::getline(head, requestLine);
    std::istringstream rl(requestLine);
    std::string target;
    std::string version;
    if (!(rl >> req.method >> target >> version) || target.empty() || target[0] != '/') {
        error = textResponse(400, "Malformed request line.");
        return false;
    }
    req.path = target.substr(0, target.find('?'));

    std::size_t contentLength = 0;
    std::string line;
    while (std::getline(head, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string name = toLower(line.substr(0, colon));
        std::string value = line.substr(colon + 1);
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.erase(0, 1);
        while (!value.empty() && (value.back() == '\r' || value.back() == ' ')) value.pop_back();
        if (name == "authorization") req.authorization = value;
        if (name == "origin") req.origin = value;
        if (name == "content-length") {
            char* end = nullptr;
            const unsigned long v = std::strtoul(line.c_str() + colon + 1, &end, 10);
            if (end == line.c_str() + colon + 1) {
                error = textResponse(400, "Bad Content-Length.");
                return false;
            }
            contentLength = static_cast<std::size_t>(v);
        }
    }
    if (contentLength > kMaxBodyBytes) {
        error = textResponse(413, "Request body is too large.");
        return false;
    }

    req.body = data.substr(headerEnd + 4);
    while (req.body.size() < contentLength) {
        const int n = static_cast<int>(recv(s, buf, sizeof buf, 0));
        if (n <= 0) {
            error = textResponse(400, "Request body ended early.");
            return false;
        }
        req.body.append(buf, static_cast<std::size_t>(n));
    }
    req.body.resize(contentLength);
    return true;
}

}  // namespace

bool isLocalOrigin(const std::string& origin) {
    for (const char* host : {"http://localhost", "http://127.0.0.1"}) {
        const std::string h(host);
        if (origin.compare(0, h.size(), h) != 0) continue;
        const std::string rest = origin.substr(h.size());
        if (rest.empty()) return true;
        if (rest[0] != ':' || rest.size() == 1 || rest.size() > 6) continue;
        bool digits = true;
        for (std::size_t i = 1; i < rest.size(); ++i) {
            if (rest[i] < '0' || rest[i] > '9') digits = false;
        }
        if (digits) return true;
    }
    return false;
}

HttpServer::HttpServer(unsigned short port, std::string webRoot, Handler apiHandler)
    : port_(port), webRoot_(std::move(webRoot)), api_(std::move(apiHandler)) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        throw std::runtime_error("Winsock could not be started");
    }
#endif
}

HttpServer::~HttpServer() {
#ifdef _WIN32
    WSACleanup();
#endif
}

HttpResponse HttpServer::serveStatic(const HttpRequest& req) const {
    if (req.method != "GET" && req.method != "HEAD") {
        return textResponse(405, "Only GET is allowed for files.");
    }
    std::string path = req.path == "/" ? "/opera.html" : req.path;
    // No URL decoding and no way out of the web root: refuse anything odd.
    if (path.find("..") != std::string::npos || path.find('\\') != std::string::npos ||
        path.find('%') != std::string::npos || path.find(':') != std::string::npos ||
        path.find("//") != std::string::npos) {
        return textResponse(404, "Not found.");
    }
    const std::string type = contentTypeFor(path);
    if (type.empty()) {
        return textResponse(404, "Not found.");
    }
    std::ifstream file(webRoot_ + path, std::ios::binary);
    if (!file) {
        return textResponse(404, "Not found: " + path);
    }
    std::ostringstream content;
    content << file.rdbuf();
    return HttpResponse{200, type, content.str()};
}

void HttpServer::run() {
#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);  // a browser closing early must not kill the server
#endif
    const SocketHandle listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener == kInvalidSocket) {
        throw std::runtime_error("could not create a socket");
    }
#ifndef _WIN32
    const int yes = 1;  // allow an immediate restart on the same port
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // this computer only, not the network
    if (bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 ||
        listen(listener, 16) != 0) {
        closeSocket(listener);
        throw std::runtime_error("port " + std::to_string(port_) +
                                 " is already in use (another OPERA running?) or not allowed");
    }

    while (true) {
        const SocketHandle client = accept(listener, nullptr, nullptr);
        if (client == kInvalidSocket) {
            continue;
        }
        setTimeouts(client);

        HttpRequest req;
        HttpResponse res;
        if (readRequest(client, req, res)) {
            const bool isApi = req.path.compare(0, 5, "/api/") == 0;
            const std::string cors = isApi && isLocalOrigin(req.origin) ? req.origin : "";
            if (isApi && req.method == "OPTIONS") {
                // CORS preflight from a page on another local port (e.g. Live Server).
                res = HttpResponse{cors.empty() ? 403 : 204, "text/plain; charset=utf-8", ""};
            } else {
                res = isApi ? api_(req) : serveStatic(req);
            }
            if (isApi) {
                std::cout << req.method << ' ' << req.path << " -> " << res.status << std::endl;
            }
            writeResponse(client, res, req.method == "HEAD", cors);
        } else if (res.status != 200) {
            writeResponse(client, res, false, "");
        }
        closeSocket(client);
    }
}

}  // namespace opera

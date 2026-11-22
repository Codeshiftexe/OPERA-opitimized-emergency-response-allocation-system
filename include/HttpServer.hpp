#pragma once

// A small, single-threaded HTTP/1.1 server using plain sockets
// (POSIX on Linux/macOS, Winsock on Windows).
//
//   /api/...    handed to the API handler
//   anything    served as a static file from the web root (opera.html for "/")
//
// CORS: pages opened from another local server (e.g. VS Code Live Server on
// http://127.0.0.1:5500) may call the API too. Only http://localhost:<port>
// and http://127.0.0.1:<port> origins are allowed; every other origin gets no
// CORS headers, so its browser blocks the reply.
//
// Single-threaded on purpose: requests are handled one at a time, so the
// engine is never touched by two requests at once and no locks are needed.
// It also avoids std::thread, which older MinGW builds do not support.
// The cost: one slow client delays the others, which is why every socket
// gets a short receive timeout.

#include <cstddef>
#include <functional>
#include <string>

namespace opera {

struct HttpRequest {
    std::string method;
    std::string path;           // without the query string
    std::string body;
    std::string authorization;  // value of the Authorization header, or ""
    std::string origin;         // value of the Origin header, or ""
    std::string remote;         // the client's IP address, from the connection itself
};

struct HttpResponse {
    int status = 200;
    std::string contentType = "application/json";
    std::string body;
};

// True for http://localhost[:port] and http://127.0.0.1[:port].
bool isLocalOrigin(const std::string& origin);

// Largest request body the server accepts.
constexpr std::size_t kMaxRequestBodyBytes = 64 * 1024;

// Parses the request line and headers (everything before the blank line) into
// `req` and the declared body length. False with `error` set for a malformed
// request, a body over kMaxRequestBodyBytes, a Transfer-Encoding header, or two
// different Content-Length values (request smuggling defence). In HttpParse.cpp.
bool parseRequestHead(const std::string& headBlock, HttpRequest& req, std::size_t& contentLength,
                      HttpResponse& error);

class HttpServer {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    // listenOnNetwork false: 127.0.0.1 only (this computer). true: every
    // network interface, so a crew phone on the same Wi-Fi can connect.
    HttpServer(unsigned short port, std::string webRoot, Handler apiHandler,
               bool listenOnNetwork = false);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    // Binds (127.0.0.1, or all interfaces if listenOnNetwork), then serves forever.
    // Throws std::runtime_error if the port cannot be opened.
    void run();

    // Called about once a second while the server runs, also when no browser
    // sends anything (auto-dispatch hands over arrivals and dispatches the next case).
    void setTick(std::function<void()> tick) { tick_ = std::move(tick); }

private:
    HttpResponse serveStatic(const HttpRequest& req) const;

    unsigned short port_;
    std::string webRoot_;
    Handler api_;
    bool listenOnNetwork_;
    std::function<void()> tick_;
};

}  // namespace opera

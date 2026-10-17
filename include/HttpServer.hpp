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

#include <functional>
#include <string>

namespace opera {

struct HttpRequest {
    std::string method;
    std::string path;           // without the query string
    std::string body;
    std::string authorization;  // value of the Authorization header, or ""
    std::string origin;         // value of the Origin header, or ""
};

struct HttpResponse {
    int status = 200;
    std::string contentType = "application/json";
    std::string body;
};

// True for http://localhost[:port] and http://127.0.0.1[:port].
bool isLocalOrigin(const std::string& origin);

class HttpServer {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    HttpServer(unsigned short port, std::string webRoot, Handler apiHandler);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    // Binds to 127.0.0.1 only (this machine), then serves forever.
    // Throws std::runtime_error if the port cannot be opened.
    void run();

private:
    HttpResponse serveStatic(const HttpRequest& req) const;

    unsigned short port_;
    std::string webRoot_;
    Handler api_;
};

}  // namespace opera

// Parsing of an HTTP request's head (request line and headers), kept apart
// from the socket code in HttpServer.cpp so the tests can check it directly,
// without opening a network connection.
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <string>

#include "HttpServer.hpp"

namespace opera {

namespace {

std::string toLower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

HttpResponse textResponse(int status, const std::string& text) {
    return HttpResponse{status, "text/plain; charset=utf-8", text + "\n"};
}

}  // namespace

bool parseRequestHead(const std::string& headBlock, HttpRequest& req, std::size_t& contentLength,
                      HttpResponse& error) {
    std::istringstream head(headBlock);
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

    contentLength = 0;
    bool haveLength = false;
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
        // Request smuggling defence: OPERA reads a body only by Content-Length.
        // A request that also declares Transfer-Encoding (or two different
        // lengths) could be read differently by a proxy in front, so refuse it.
        if (name == "transfer-encoding") {
            error = textResponse(501, "Transfer-Encoding is not supported; send Content-Length.");
            return false;
        }
        if (name == "content-length") {
            const bool digits = !value.empty() && value.size() <= 10 &&
                                value.find_first_not_of("0123456789") == std::string::npos;
            if (!digits) {
                error = textResponse(400, "Bad Content-Length.");
                return false;
            }
            const std::size_t v = static_cast<std::size_t>(std::strtoul(value.c_str(), nullptr, 10));
            if (haveLength && v != contentLength) {
                error = textResponse(400, "Conflicting Content-Length headers.");
                return false;
            }
            contentLength = v;
            haveLength = true;
        }
    }
    if (contentLength > kMaxRequestBodyBytes) {
        error = textResponse(413, "Request body is too large.");
        return false;
    }

    return true;
}

}  // namespace opera

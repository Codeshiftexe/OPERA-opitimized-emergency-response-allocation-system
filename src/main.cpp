// OPERA server: loads the city, owns the one DispatchCoordinator, and serves
// both the web interface and the JSON API on http://localhost:<port>.
//
//   opera_server [--port 8080] [--data <dir>] [--web <dir>]
//
// --data  folder with city_map.txt, hospitals.txt, ambulances.txt, passwords.txt
// --web   folder with opera.html, opera.css, opera.js
// Defaults point at data/ and frontend/ in the source tree (set by CMake),
// so running the built program from anywhere just works.

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <utility>

#include "DispatchCoordinator.hpp"
#include "CityLoader.hpp"
#include "Api.hpp"
#include "Auth.hpp"
#include "HttpServer.hpp"

#ifndef OPERA_DEFAULT_DATA_DIR
#define OPERA_DEFAULT_DATA_DIR "data"
#endif
#ifndef OPERA_DEFAULT_WEB_DIR
#define OPERA_DEFAULT_WEB_DIR "frontend"
#endif

namespace {

struct Options {
    int port = 8080;
    std::string dataDir = OPERA_DEFAULT_DATA_DIR;
    std::string webDir = OPERA_DEFAULT_WEB_DIR;
};

bool parseArgs(int argc, char** argv, Options& opt) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (i + 1 >= argc) {
            std::cerr << "Missing value after " << arg << '\n';
            return false;
        }
        const std::string value = argv[++i];
        if (arg == "--port") {
            opt.port = std::atoi(value.c_str());
            if (opt.port < 1 || opt.port > 65535) {
                std::cerr << "--port must be from 1 to 65535\n";
                return false;
            }
        } else if (arg == "--data") {
            opt.dataDir = value;
        } else if (arg == "--web") {
            opt.webDir = value;
        } else {
            std::cerr << "Unknown option " << arg << '\n';
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parseArgs(argc, argv, opt)) {
        std::cerr << "Usage: opera_server [--port 8080] [--data <dir>] [--web <dir>]\n";
        return 2;
    }

    // 1. Loader: text files -> Graph + Registry (+ map layout for drawing).
    opera::CityData city = [&] {
        try {
            return opera::loadCity(opt.dataDir + "/city_map.txt", opt.dataDir + "/hospitals.txt",
                                   opt.dataDir + "/ambulances.txt");
        } catch (const opera::LoadError& e) {
            std::cerr << "Could not load the city data.\n  " << e.what() << '\n';
            std::exit(1);
        }
    }();
    std::cout << "Loaded " << city.registry.roads().size() << " nodes, "
              << city.registry.hospitals().size() << " hospitals, "
              << city.registry.ambulances().size() << " ambulances from " << opt.dataDir
              << '\n';

    // Accounts for sign-in. passwords.txt sits in data/, which is never served.
    opera::Auth auth;
    try {
        auth.loadUsersFile(opt.dataDir + "/passwords.txt");
    } catch (const opera::LoadError& e) {
        std::cerr << "Could not load the accounts.\n  " << e.what() << '\n';
        return 1;
    }
    std::cout << "Loaded " << auth.userCount() << " accounts from passwords.txt\n";

    // 2. The single DispatchCoordinator every request works on.
    opera::DispatchCoordinator coordinator(std::move(city.registry));

    // 3. API layer borrows the coordinator; the server only moves bytes.
    opera::Api api(coordinator, std::move(city.layout), auth);
    opera::HttpServer server(
        static_cast<unsigned short>(opt.port), opt.webDir,
        [&api](const opera::HttpRequest& req) {
            const opera::ApiResponse r = api.handle(req.method, req.path, req.body, req.authorization);
            return opera::HttpResponse{r.status, "application/json; charset=utf-8", r.body};
        });

    std::cout << "Serving the web interface from " << opt.webDir << '\n'
              << "OPERA is running: open http://localhost:" << opt.port
              << "  (Ctrl+C to stop)\n"
              << "A page opened with VS Code Live Server also works while this runs." << std::endl;
    try {
        server.run();
    } catch (const std::exception& e) {
        std::cerr << "Server stopped: " << e.what() << '\n';
        return 1;
    }
}

#pragma once

// The HTTP API, independent of sockets: (method, path, body, auth header) in,
// (status, JSON) out. HttpServer moves bytes; this class decides what each
// route means. Every route can therefore be unit-tested without a port.
//
// Open routes (no sign-in needed):
//   GET  /api/ping                   {"ok":true,"opera":true}  - lets the page find the server
//   POST /api/login                  {"name":"admin","password":"..."}  -> token + user + state
//
// Routes that need "Authorization: Bearer <token>":
//   GET  /api/session                who is signed in + state (used after a page reload)
//   POST /api/logout                 {}
//   GET  /api/state
//   POST /api/emergencies            {"type":"Cardiac|Trauma|Transfer","node":3}
//   POST /api/dispatch               {}
//   POST /api/undo                   {}
//   POST /api/emergencies/complete   {"id":4}
//   POST /api/hospitals/discharge    {"id":2}   (only beds of patients who have arrived)
//   POST /api/ambulances/status      {"id":5,"online":false}   ADMIN ONLY
//   POST /api/clock/advance          {"minutes":5}
//
// Reply: {"ok":bool, "message":string, "result":object|null,
//         "user":{"name","role"}|null, "state":{...}|null}   (+ "token" on login)
// "state" is null whenever the caller is not signed in.
// Status: 200 done, 400 bad request, 401 not signed in / wrong password,
// 403 signed in but not allowed (operator), 404 unknown route, 405 wrong method,
// 409 valid request the engine refused, 429 locked after too many failed sign-ins,
// 500 engine invariant broke.

#include <string>
#include <vector>

#include "Auth.hpp"
#include "CityLoader.hpp"
#include "DispatchCoordinator.hpp"
#include "StateJson.hpp"

namespace opera {

struct ApiResponse {
    int status = 200;
    std::string body;  // always JSON
};

class Api {
public:
    // The coordinator and the account store are owned by main(); Api borrows them.
    Api(DispatchCoordinator& coordinator, MapLayout layout, Auth& auth);

    // `authorization` is the raw Authorization header ("Bearer <token>"), or "".
    ApiResponse handle(const std::string& method, const std::string& path,
                       const std::string& body, const std::string& authorization = "");

    const std::vector<LogEntry>& log() const { return log_; }

private:
    struct Outcome {
        int status = 200;
        bool ok = true;
        std::string message;
        bool hasResult = false;
        DispatchResult result;
    };

    ApiResponse login(const JsonObject& in);

    Outcome registerEmergency(const JsonObject& in);
    Outcome dispatchNext();
    Outcome undo();
    Outcome complete(const JsonObject& in);
    Outcome discharge(const JsonObject& in);
    Outcome setAmbulanceStatus(const JsonObject& in);
    Outcome advanceClock(const JsonObject& in);

    // user == nullptr: not signed in, so no state is sent.
    ApiResponse reply(const Outcome& o, const User* user, const std::string& token = "") const;
    void record(const std::string& text);  // logged under actor_
    std::string nodeName(int id) const;

    DispatchCoordinator& coord_;
    MapLayout layout_;
    Auth& auth_;
    std::vector<LogEntry> log_;  // oldest first, capped
    std::string actor_;          // name of the user making the current request
};

}  // namespace opera

#pragma once

// The HTTP API, independent of sockets: (method, path, body, auth header) in,
// (status, JSON) out. HttpServer moves bytes; this class decides what each
// route means. Every route can therefore be unit-tested without a port.
//
// Open routes (no sign-in needed):
//   GET  /api/ping                   {"ok":true,"opera":true}  - lets the page find the server
//   GET  /api/captcha                -> {"ok":true,"captchaId":"...","image":"data:image/bmp;base64,..."}
//   POST /api/login                  {"name":"admin","password":"...","captchaId":"...","captcha":"AB3XY7"}
//                                    -> token + user + state. The captcha is checked first and
//                                    is single use (see Captcha.hpp).
//
// Routes that need "Authorization: Bearer <token>":
//   GET  /api/session                who is signed in + state (used after a page reload)
//   POST /api/logout                 {}
//   GET  /api/state
//   POST /api/emergencies            {"type":"Cardiac|Trauma|Transfer","node":3}
//   POST /api/dispatch               {}
//   POST /api/dispatch/batch         {}   most urgent cases together, ambulances chosen by backtracking
//   POST /api/undo                   {}
//   POST /api/emergencies/complete   {"id":4}
//   POST /api/hospitals/discharge    {"id":2}   (only beds of patients who have arrived)
//   POST /api/ambulances/status      {"id":5,"online":false}   ADMIN ONLY
//   POST /api/clock/advance          {"minutes":5}
//   POST /api/autodispatch           {"enabled":true}   any signed-in dispatcher (see AutoDispatcher.hpp)
//   POST /api/tracking/gps           {"emergency":4,"lat":30.32,"lon":78.04,"accuracy":8,"speed":11.2}
//                                    crew device's GPS fix (crew.html sends it every ~2 s)
//   POST /api/tracking/report        {"emergency":4,"node":7}   crew: "we reached node 7" (manual fallback)
//   POST /api/tracking/mode          {"mode":"gps"|"simulated"} ADMIN ONLY (default gps = real positions only)
//   POST /api/tracking/speed         {"msPerMinute":1000}       simulated mode only: demo speed
//
// Reply: {"ok":bool, "message":string, "result":object|null,
//         "user":{"name","role"}|null, "state":{...}|null,
//         "autoDispatch":bool,
//         "tracking":{"mode","geo","secondsPerMinute","trips":[live positions]}|null}   (+ "token" on login)
// "state" is null whenever the caller is not signed in.
// Status: 200 done, 400 bad request, 401 not signed in / wrong password,
// 403 signed in but not allowed (operator), 404 unknown route, 405 wrong method,
// 409 valid request the engine refused, 429 locked after too many failed sign-ins,
// 500 engine invariant broke.

#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include "Auth.hpp"
#include "AutoDispatcher.hpp"
#include "Captcha.hpp"
#include "LiveTracker.hpp"
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
    // `remote` is the caller's IP address from the connection ("" in tests).
    ApiResponse handle(const std::string& method, const std::string& path,
                       const std::string& body, const std::string& authorization = "",
                       const std::string& remote = "");

    // The server always requires the captcha. Tests of other features may
    // switch it off; test_security checks that it is enforced.
    void setCaptchaRequired(bool on) { captchaRequired_ = on; }
    Captcha& captcha() { return captcha_; }

    const std::vector<LogEntry>& log() const { return log_; }
    bool autoDispatchOn() const { return auto_.enabled(); }

    // Once a second (from the server): auto hand-over on arrival, then auto-dispatch.
    void tick();

private:
    struct Outcome {
        // A constructor, not default member initialisers, so every handler can
        // keep writing {status, ok, message, hasResult, result} unchanged now
        // that the batch fields have been added after them.
        Outcome(int status_ = 200, bool ok_ = true, std::string message_ = "",
                bool hasResult_ = false, DispatchResult result_ = DispatchResult())
            : status(status_), ok(ok_), message(std::move(message_)), hasResult(hasResult_),
              result(std::move(result_)) {}

        int status;
        bool ok;
        std::string message;
        bool hasResult;
        DispatchResult result;
        bool hasBatch = false;      // the reply carries a whole batch, not one dispatch
        BatchResult batch;
    };

    ApiResponse login(const JsonObject& in, const std::string& remote);

    Outcome registerEmergency(const JsonObject& in);
    Outcome dispatchNext();
    Outcome dispatchBatch();        // POST /api/dispatch/batch (see BatchPlanner.hpp)
    Outcome undo();
    Outcome complete(const JsonObject& in);
    Outcome discharge(const JsonObject& in);
    Outcome setAmbulanceStatus(const JsonObject& in);
    Outcome advanceClock(const JsonObject& in);
    Outcome setAutoDispatch(const JsonObject& in);  // POST /api/autodispatch {"enabled": bool}
    Outcome trackingReport(const JsonObject& in);   // POST /api/tracking/report {"emergency", "node"}
    Outcome trackingGps(const JsonObject& in);      // POST /api/tracking/gps {"emergency","lat","lon",...}
    Outcome trackingMode(const JsonObject& in);     // POST /api/tracking/mode {"mode"}, admin only
    Outcome trackingSpeed(const JsonObject& in);    // POST /api/tracking/speed {"msPerMinute"}
    double now() const;                              // seconds since the Api started (steady clock)

    // After a change that can free a resource or add a case, let the
    // AutoDispatcher dispatch whatever it can (only while it is switched on).
    void runAutoDispatch(Outcome& o);

    // user == nullptr: not signed in, so no state is sent.
    // Not const: it first brings the live tracker up to date with the coordinator.
    ApiResponse reply(const Outcome& o, const User* user, const std::string& token = "");
    void record(const std::string& text);  // logged under actor_
    std::string nodeName(int id) const;

    DispatchCoordinator& coord_;
    MapLayout layout_;
    Auth& auth_;
    std::vector<LogEntry> log_;  // oldest first, capped
    std::string actor_;          // name of the user making the current request
    AutoDispatcher auto_;        // optional automatic dispatching (off at start)
    Captcha captcha_;            // server-side captcha for /api/login
    bool captchaRequired_ = true;
    LiveTracker live_;           // real-time position of every ambulance on a call
    std::chrono::steady_clock::time_point started_;
};

}  // namespace opera

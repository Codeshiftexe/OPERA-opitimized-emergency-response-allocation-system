// Tests for the HTTP API layer, driven directly through Api::handle (no sockets).
// Each test checks three things: the HTTP status, the reply text, and the
// engine state behind it, so a route cannot "succeed" without the engine changing.

#include <sstream>
#include <string>

#include "Api.hpp"
#include "test_util.hpp"

using namespace opera;

namespace {

// Same city as test_dispatch: A-B-C-D-F chain with a shortcut A-E-D; Island is cut off.
// Hospitals: 1 at D (2 beds), 2 at F (1 bed). Ambulances: 10 at A, 11 at C.
enum Node { A, B, C, D, E, F, Island };

// Accounts: boss (admin, password pw-admin) and op (operator, password pw-oper).
Auth makeAuth() {
    std::istringstream in(
        "USER boss | admin | s1 | d534bf292ac365a54fc4c70a6048c9835e90f0845c7819ed19f3d34d36d46ab0\n"
        "USER op | operator | s2 | c759c858afb12c7b927596b80c328e34a6e07ab67d22f17f24a55ae2c5c8af5f\n");
    Auth a;
    a.loadUsers(in, "test-passwords");
    return a;
}

// Pulls "token":"..." out of a login reply.
std::string tokenIn(const std::string& body) {
    const std::string key = "\"token\":\"";
    const auto p = body.find(key);
    if (p == std::string::npos) return "";
    return body.substr(p + key.size(), 64);
}

struct World {
    DispatchCoordinator coord;
    Auth auth;
    Api api;
    std::string token;  // signed in as boss (admin) unless a test changes it
    World() : coord(makeRegistry()), auth(makeAuth()), api(coord, makeLayout(), auth) {
        token = tokenIn(api.handle("POST", "/api/login", R"({"name":"boss","password":"pw-admin"})").body);
    }

    static Registry makeRegistry() {
        Graph g;
        for (const char* name : {"A", "B", "C", "D", "E", "F", "Island"}) g.addNode(name);
        g.addEdge(A, B, 4);
        g.addEdge(B, C, 3);
        g.addEdge(C, D, 5);
        g.addEdge(A, E, 2);
        g.addEdge(E, D, 9);
        g.addEdge(D, F, 1);
        Registry r(g);
        r.addHospital(Hospital(1, D, 2));
        r.addHospital(Hospital(2, F, 1));
        r.addAmbulance(Ambulance(10, A));
        r.addAmbulance(Ambulance(11, C));
        return r;
    }
    static MapLayout makeLayout() {
        MapLayout m;
        m.nodePositions.assign(7, Point{0, 0});
        m.hospitalNames = {{1, "D General"}, {2, "F Clinic"}};
        return m;
    }

    ApiResponse get(const std::string& path) { return api.handle("GET", path, "", "Bearer " + token); }
    ApiResponse post(const std::string& path, const std::string& body = "") {
        return api.handle("POST", path, body, "Bearer " + token);
    }
    const Emergency& em(int id) { return *coord.findEmergency(id); }
};

bool has(const ApiResponse& r, const std::string& part) {
    return r.body.find(part) != std::string::npos;
}

int count(const std::string& s, const std::string& part) {
    int n = 0;
    for (auto p = s.find(part); p != std::string::npos; p = s.find(part, p + 1)) ++n;
    return n;
}

void testStateAndRouting() {
    World w;
    ApiResponse r = w.get("/api/state");
    CHECK_EQ(r.status, 200);
    CHECK(has(r, R"("ok":true)"));
    CHECK(has(r, R"("canUndo":false)"));
    CHECK(has(r, R"({"name":"Cardiac","basePriority":100,"targetMinutes":8})"));
    CHECK(has(r, R"("name":"D General","totalBeds":2,"availableBeds":2)"));
    CHECK_EQ(count(r.body, R"("oneWay":false)"), 6);  // 6 two-way roads, each sent once
    CHECK_EQ(count(r.body, R"("oneWay":true)"), 0);

    CHECK_EQ(w.post("/api/state").status, 405);
    CHECK_EQ(w.get("/api/nope").status, 404);
    CHECK_EQ(w.get("/api/dispatch").status, 405);
}

void testRegisterValidation() {
    World w;
    ApiResponse r = w.post("/api/emergencies", R"({"type":"Cardiac","node":1})");
    CHECK_EQ(r.status, 200);
    CHECK(has(r, "Registered #1 Cardiac at B."));
    CHECK(w.em(1).status() == EmergencyStatus::Pending);
    CHECK(has(r, R"("queue":[1])"));

    CHECK_EQ(w.post("/api/emergencies", R"({"type":"Flu","node":1})").status, 400);
    CHECK_EQ(w.post("/api/emergencies", R"({"type":"Cardiac","node":99})").status, 400);
    CHECK_EQ(w.post("/api/emergencies", R"({"type":"Cardiac"})").status, 400);
    CHECK_EQ(w.post("/api/emergencies", R"({"type":"Cardiac","node":1.5})").status, 400);
    CHECK_EQ(w.post("/api/emergencies", R"({"type":)").status, 400);
    CHECK_EQ(w.coord.emergencies().size(), std::size_t(1));  // none of the bad ones got in
}

void testDispatchUndoComplete() {
    World w;
    w.post("/api/emergencies", R"({"type":"Trauma","node":1})");

    ApiResponse r = w.post("/api/dispatch");
    CHECK_EQ(r.status, 200);
    CHECK(has(r, R"("result":{"status":"Dispatched","emergency":1,"ambulance":11,"hospital":1)"));
    CHECK(has(r, R"("canUndo":true)"));
    // Recomputed routes in the state match the dispatch: C->B, then B->C->D.
    CHECK(has(r, R"("routeToScene":[2,1],"routeToHospital":[1,2,3],"minutesToScene":3,"minutesToHospital":8)"));
    CHECK(w.em(1).status() == EmergencyStatus::Assigned);
    CHECK_EQ(w.coord.registry().hospital(1)->availableBeds(), 1);

    r = w.post("/api/undo");
    CHECK_EQ(r.status, 200);
    CHECK(has(r, "Undid the dispatch of #1"));
    CHECK(has(r, R"("canUndo":false)"));
    CHECK(w.em(1).status() == EmergencyStatus::Pending);
    CHECK(w.coord.registry().ambulance(11)->isAvailable());
    CHECK_EQ(w.coord.registry().hospital(1)->availableBeds(), 2);
    CHECK_EQ(w.post("/api/undo").status, 409);

    w.post("/api/dispatch");
    r = w.post("/api/emergencies/complete", R"({"id":1})");
    CHECK_EQ(r.status, 200);
    CHECK(w.em(1).status() == EmergencyStatus::Resolved);
    CHECK_EQ(w.coord.registry().ambulance(11)->nodeId(), int(D));  // moved to the hospital
    CHECK(w.coord.registry().ambulance(11)->isAvailable());
    CHECK(has(r, R"("canUndo":false)"));                  // completed work cannot be undone
    CHECK_EQ(w.post("/api/undo").status, 409);
    CHECK_EQ(w.post("/api/emergencies/complete", R"({"id":1})").status, 409);
    CHECK_EQ(w.post("/api/emergencies/complete", R"({"id":99})").status, 400);
}

void testDischarge() {
    World w;
    w.post("/api/emergencies", R"({"type":"Cardiac","node":3})");
    w.post("/api/dispatch");
    w.post("/api/emergencies/complete", R"({"id":1})");
    CHECK_EQ(w.coord.registry().hospital(1)->availableBeds(), 1);  // bed still taken

    ApiResponse r = w.post("/api/hospitals/discharge", R"({"id":1})");
    CHECK_EQ(r.status, 200);
    CHECK(has(r, "2 of 2 beds free"));
    CHECK_EQ(w.post("/api/hospitals/discharge", R"({"id":1})").status, 409);
    CHECK_EQ(w.post("/api/hospitals/discharge", R"({"id":42})").status, 400);
}

// Regression: discharging a bed that is reserved for a patient still in the
// ambulance used to make the next undo throw "state mismatch" (HTTP 500).
void testDischargeOfReservedBedIsRefused() {
    World w;
    w.post("/api/emergencies", R"({"type":"Cardiac","node":3})");
    w.post("/api/dispatch");                                       // bed at hospital 1 reserved
    ApiResponse r = w.post("/api/hospitals/discharge", R"({"id":1})");
    CHECK_EQ(r.status, 409);
    CHECK(has(r, "reserved for patients still on the way"));
    CHECK_EQ(w.coord.registry().hospital(1)->availableBeds(), 1); // nothing freed

    r = w.post("/api/undo");                                       // must still work
    CHECK_EQ(r.status, 200);
    CHECK(w.em(1).status() == EmergencyStatus::Pending);
    CHECK_EQ(w.coord.registry().hospital(1)->availableBeds(), 2);

    // A bed with an admitted patient next to an inbound one: exactly one discharge allowed.
    w.post("/api/dispatch");
    w.post("/api/emergencies/complete", R"({"id":1})");           // #1 admitted at hospital 1
    w.post("/api/emergencies", R"({"type":"Cardiac","node":3})");
    w.post("/api/dispatch");                                       // #2 inbound to hospital 1
    CHECK_EQ(w.coord.registry().hospital(1)->availableBeds(), 0);
    CHECK_EQ(w.post("/api/hospitals/discharge", R"({"id":1})").status, 200);
    CHECK_EQ(w.post("/api/hospitals/discharge", R"({"id":1})").status, 409);
    CHECK_EQ(w.post("/api/undo").status, 200);                    // undo of #2 still consistent
}

void testAmbulanceStatus() {
    World w;
    CHECK_EQ(w.post("/api/ambulances/status", R"({"id":10,"online":false})").status, 200);
    CHECK(w.coord.registry().ambulance(10)->status() == AmbulanceStatus::Offline);
    CHECK_EQ(w.post("/api/ambulances/status", R"({"id":10,"online":false})").status, 409);
    CHECK_EQ(w.post("/api/ambulances/status", R"({"id":10,"online":true})").status, 200);
    CHECK(w.coord.registry().ambulance(10)->isAvailable());

    w.post("/api/emergencies", R"({"type":"Cardiac","node":2})");  // at C: ambulance 11 goes
    w.post("/api/dispatch");
    ApiResponse r = w.post("/api/ambulances/status", R"({"id":11,"online":false})");
    CHECK_EQ(r.status, 409);
    CHECK(has(r, "on call #1"));
    CHECK(w.coord.registry().ambulance(11)->status() == AmbulanceStatus::Busy);

    CHECK_EQ(w.post("/api/ambulances/status", R"({"id":77,"online":true})").status, 400);
    CHECK_EQ(w.post("/api/ambulances/status", R"({"id":10})").status, 400);
}

void testClockAndAging() {
    World w;
    w.post("/api/emergencies", R"({"type":"Transfer","node":1})");
    ApiResponse r = w.post("/api/clock/advance", R"({"minutes":10})");
    CHECK_EQ(r.status, 200);
    CHECK_EQ(w.coord.now(), 10);
    // Transfer: base 20 + 0.5 * 10 minutes waiting = 25
    CHECK(has(r, R"("waitingMinutes":10,"basePriority":20,"priority":25)"));

    CHECK_EQ(w.post("/api/clock/advance", R"({"minutes":0})").status, 400);
    CHECK_EQ(w.post("/api/clock/advance", R"({"minutes":"5"})").status, 400);
    CHECK_EQ(w.post("/api/clock/advance", R"({"minutes":100000})").status, 400);
    CHECK_EQ(w.coord.now(), 10);
}

void testRefusedDispatches() {
    World w;
    ApiResponse r = w.post("/api/dispatch");
    CHECK_EQ(r.status, 409);
    CHECK(has(r, "No emergency is waiting."));

    w.post("/api/emergencies", R"({"type":"Cardiac","node":6})");  // Island
    r = w.post("/api/dispatch");
    CHECK_EQ(r.status, 409);
    CHECK(has(r, R"("status":"No ambulance available")"));
    CHECK(has(r, "stays at the front of the queue"));
    CHECK(has(r, R"("queue":[1])"));
}

void testLogNewestFirst() {
    World w;
    w.post("/api/emergencies", R"({"type":"Cardiac","node":1})");
    w.post("/api/clock/advance", R"({"minutes":3})");
    const ApiResponse r = w.get("/api/state");
    CHECK(has(r, R"("log":[{"minute":3,"user":"boss","text":"Clock advanced by 3 min to T+3."},{"minute":0,"user":"boss","text":"Registered #1)"));
    CHECK_EQ(w.api.log().size(), std::size_t(3));  // sign-in + 2 actions; GET /api/state is not logged
}

void testSignInRules() {
    World w;
    CHECK_EQ(w.token.size(), std::size_t(64));

    // Open routes.
    ApiResponse r = w.api.handle("GET", "/api/ping", "");
    CHECK_EQ(r.status, 200);
    CHECK(has(r, R"("opera":true)"));

    // Without a token: 401 and no city data at all.
    for (const char* path : {"/api/state", "/api/session"}) {
        r = w.api.handle("GET", path, "");
        CHECK_EQ(r.status, 401);
        CHECK(has(r, R"("user":null,"state":null)"));
    }
    r = w.api.handle("POST", "/api/dispatch", "{}", "Bearer not-a-real-token");
    CHECK_EQ(r.status, 401);
    CHECK_EQ(w.api.handle("POST", "/api/emergencies", R"({"type":"Cardiac","node":1})", "").status, 401);
    CHECK_EQ(w.coord.emergencies().size(), std::size_t(0));  // nothing slipped through

    // Login replies.
    r = w.api.handle("POST", "/api/login", R"({"name":"boss","password":"nope"})");
    CHECK_EQ(r.status, 401);
    CHECK(has(r, "The name or password is incorrect."));
    CHECK(!has(r, "\"token\""));
    CHECK_EQ(w.api.handle("POST", "/api/login", R"({"name":"boss"})").status, 400);
    CHECK_EQ(w.api.handle("GET", "/api/login", "").status, 405);
    r = w.api.handle("POST", "/api/login", R"({"name":"op","password":"pw-oper"})");
    CHECK_EQ(r.status, 200);
    CHECK(has(r, R"("user":{"name":"op","role":"operator"},"state":{)"));

    // Session lookup after a page reload.
    r = w.get("/api/session");
    CHECK_EQ(r.status, 200);
    CHECK(has(r, R"("user":{"name":"boss","role":"admin"})"));

    // Lockout surfaces as 429.
    for (int i = 0; i < Auth::kMaxFails; ++i) w.api.handle("POST", "/api/login", R"({"name":"op","password":"x"})");
    CHECK_EQ(w.api.handle("POST", "/api/login", R"({"name":"op","password":"pw-oper"})").status, 429);

    // Logout ends the session.
    CHECK_EQ(w.post("/api/logout").status, 200);
    CHECK_EQ(w.get("/api/state").status, 401);
}

void testOperatorCannotChangeAmbulances() {
    World w;
    const std::string op = tokenIn(w.api.handle("POST", "/api/login", R"({"name":"op","password":"pw-oper"})").body);
    ApiResponse r = w.api.handle("POST", "/api/ambulances/status", R"({"id":10,"online":false})", "Bearer " + op);
    CHECK_EQ(r.status, 403);
    CHECK(has(r, "Only an admin"));
    CHECK(w.coord.registry().ambulance(10)->isAvailable());
    // Operators can still do everything else.
    CHECK_EQ(w.api.handle("POST", "/api/emergencies", R"({"type":"Cardiac","node":1})", "Bearer " + op).status, 200);
    CHECK_EQ(w.api.handle("POST", "/api/dispatch", "{}", "Bearer " + op).status, 200);
    CHECK(has(w.get("/api/state"), R"("user":"op","text":"Registered #1)"));
}

}  // namespace

int main() {
    testStateAndRouting();
    testRegisterValidation();
    testDispatchUndoComplete();
    testDischarge();
    testDischargeOfReservedBedIsRefused();
    testAmbulanceStatus();
    testClockAndAging();
    testRefusedDispatches();
    testLogNewestFirst();
    testSignInRules();
    testOperatorCannotChangeAmbulances();
    return report("test_api");
}

// Tests for the auto-dispatch route of the API (POST /api/autodispatch), driven
// through Api::handle without sockets, like test_api.cpp.
//
// Build and run (Windows: add .exe and run with .\test_api_autodispatch.exe):
//     g++ -std=c++17 -Iinclude tests/test_api_autodispatch.cpp src/Api.cpp src/StateJson.cpp src/Json.cpp src/Auth.cpp src/Sha256.cpp src/AutoDispatcher.cpp src/DispatchCoordinator.cpp src/Registry.cpp src/TriageQueue.cpp src/Emergency.cpp src/Hospital.cpp src/Ambulance.cpp src/Graph.cpp src/RoutingEngine.cpp src/CityLoader.cpp -o test_api_autodispatch
//     ./test_api_autodispatch

#include <chrono>
#include <sstream>
#include <string>

#include "Api.hpp"
#include "test_util.hpp"

using namespace opera;

namespace {

enum Node { A, B, C, D, E, F };

// Accounts: boss (admin, password pw-admin) and op (operator, password pw-oper).
Auth makeAuth() {
    std::istringstream in(
        "USER boss | admin | s1 | d534bf292ac365a54fc4c70a6048c9835e90f0845c7819ed19f3d34d36d46ab0\n"
        "USER op | operator | s2 | c759c858afb12c7b927596b80c328e34a6e07ab67d22f17f24a55ae2c5c8af5f\n");
    Auth a;
    a.loadUsers(in, "test-passwords");
    return a;
}

std::string tokenIn(const std::string& body) {
    const std::string key = "\"token\":\"";
    const auto p = body.find(key);
    return p == std::string::npos ? "" : body.substr(p + key.size(), 64);
}

struct World {
    DispatchCoordinator coord;
    Auth auth;
    Api api;
    std::string admin, oper;
    World() : coord(makeRegistry()), auth(makeAuth()), api(coord, makeLayout(), auth) {
        admin = tokenIn(api.handle("POST", "/api/login", R"({"name":"boss","password":"pw-admin"})").body);
        oper = tokenIn(api.handle("POST", "/api/login", R"({"name":"op","password":"pw-oper"})").body);
    }
    static Registry makeRegistry() {
        Graph g;
        for (const char* name : {"A", "B", "C", "D", "E", "F"}) g.addNode(name);
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
        m.nodePositions.assign(6, Point{0, 0});
        m.hospitalNames = {{1, "D General"}, {2, "F Clinic"}};
        return m;
    }
    ApiResponse post(const std::string& token, const std::string& path, const std::string& body = "{}") {
        return api.handle("POST", path, body, "Bearer " + token);
    }
};

bool has(const ApiResponse& r, const std::string& part) { return r.body.find(part) != std::string::npos; }

// The "message" field of a reply (the activity log in "state" may mention older events).
std::string messageOf(const ApiResponse& r) {
    const std::string key = "\"message\":\"";
    const auto p = r.body.find(key);
    if (p == std::string::npos) return "";
    const auto end = r.body.find("\",\"", p + key.size());
    return r.body.substr(p + key.size(), end == std::string::npos ? std::string::npos : end - p - key.size());
}

void testOffByDefaultAndReported() {
    World w;
    const ApiResponse r = w.post(w.admin, "/api/emergencies", R"({"type":"Cardiac","node":1})");
    CHECK_EQ(r.status, 200);
    CHECK(has(r, "\"autoDispatch\":false"));
    CHECK(w.coord.findEmergency(1)->status() == EmergencyStatus::Pending);   // not dispatched
    CHECK(!w.api.autoDispatchOn());
}

void testAnyDispatcherCanSwitch() {
    World w;
    const ApiResponse r = w.post(w.oper, "/api/autodispatch", R"({"enabled":true})");
    CHECK_EQ(r.status, 200);
    CHECK(w.api.autoDispatchOn());
    CHECK_EQ(w.post(w.admin, "/api/autodispatch", R"({"enabled":"yes"})").status, 400);
}

// The scenario auto-dispatch exists for: more emergencies than ambulances, and
// nobody clicking. When an ambulance reaches its hospital it hands the patient
// over by itself, and the freed ambulance takes the next waiting case.
void testArrivalFreesAmbulanceForNextCase() {
    World w;
    w.post(w.admin, "/api/tracking/mode", R"({"mode":"simulated"})");
    w.post(w.admin, "/api/tracking/speed", R"({"msPerMinute":50})");   // 1 road minute = 0.05 s
    w.post(w.admin, "/api/autodispatch", R"({"enabled":true})");
    w.post(w.admin, "/api/emergencies", R"({"type":"Cardiac","node":1})");
    w.post(w.admin, "/api/emergencies", R"({"type":"Cardiac","node":4})");
    w.post(w.admin, "/api/emergencies", R"({"type":"Transfer","node":5})");
    CHECK(w.coord.findEmergency(3)->status() == EmergencyStatus::Pending);   // only two ambulances
    w.api.tick();                                                            // nothing has arrived yet
    CHECK(w.coord.findEmergency(3)->status() == EmergencyStatus::Pending);
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(1300);
    while (std::chrono::steady_clock::now() < until) { /* let the trips arrive (no threads: old MinGW) */ }
    w.api.tick();                                                            // what the server does every second
    CHECK(w.coord.findEmergency(1)->status() == EmergencyStatus::Resolved);  // handed over on arrival
    CHECK(w.coord.findEmergency(3)->status() != EmergencyStatus::Pending);   // dispatched with no click
}

// With auto-dispatch off, arrivals still wait for the dispatcher to confirm.
void testTickDoesNothingWhenOff() {
    World w;
    w.post(w.admin, "/api/tracking/mode", R"({"mode":"simulated"})");
    w.post(w.admin, "/api/tracking/speed", R"({"msPerMinute":50})");
    w.post(w.admin, "/api/emergencies", R"({"type":"Cardiac","node":1})");
    w.post(w.admin, "/api/dispatch");
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(1300);
    while (std::chrono::steady_clock::now() < until) { }
    w.api.tick();
    CHECK(w.coord.findEmergency(1)->status() == EmergencyStatus::Assigned);
}

void testSwitchingOnServesBacklog() {
    World w;
    w.post(w.admin, "/api/emergencies", R"({"type":"Transfer","node":4})");
    w.post(w.admin, "/api/emergencies", R"({"type":"Cardiac","node":1})");
    const ApiResponse r = w.post(w.admin, "/api/autodispatch", R"({"enabled":true})");
    CHECK_EQ(r.status, 200);
    CHECK(has(r, "\"autoDispatch\":true"));
    CHECK(has(r, "Auto-dispatch: Ambulance"));
    CHECK(w.coord.findEmergency(1)->status() == EmergencyStatus::Assigned);
    CHECK(w.coord.findEmergency(2)->status() == EmergencyStatus::Assigned);
}

void testNewEmergencyIsDispatchedAutomatically() {
    World w;
    w.post(w.admin, "/api/autodispatch", R"({"enabled":true})");
    const ApiResponse r = w.post(w.oper, "/api/emergencies", R"({"type":"Trauma","node":1})");
    CHECK_EQ(r.status, 200);
    CHECK(has(r, "Registered #1"));
    CHECK(has(r, "Auto-dispatch:"));
    CHECK(w.coord.findEmergency(1)->status() == EmergencyStatus::Assigned);
}

void testWaitingCaseServedWhenAmbulanceFrees() {
    World w;
    w.post(w.admin, "/api/autodispatch", R"({"enabled":true})");
    w.post(w.admin, "/api/emergencies", R"({"type":"Cardiac","node":1})");
    w.post(w.admin, "/api/emergencies", R"({"type":"Cardiac","node":4})");
    w.post(w.admin, "/api/emergencies", R"({"type":"Transfer","node":5})");
    CHECK(w.coord.findEmergency(3)->status() == EmergencyStatus::Pending);   // both ambulances busy
    const ApiResponse r = w.post(w.admin, "/api/emergencies/complete", R"({"id":1})");
    CHECK_EQ(r.status, 200);
    CHECK(w.coord.findEmergency(3)->status() == EmergencyStatus::Assigned);  // served automatically
}

void testUndoIsNotRedispatched() {
    World w;
    w.post(w.admin, "/api/autodispatch", R"({"enabled":true})");
    w.post(w.admin, "/api/emergencies", R"({"type":"Cardiac","node":1})");
    CHECK(w.coord.findEmergency(1)->status() == EmergencyStatus::Assigned);
    const ApiResponse r = w.post(w.admin, "/api/undo");
    CHECK_EQ(r.status, 200);
    CHECK(w.coord.findEmergency(1)->status() == EmergencyStatus::Pending);   // stays undone
    CHECK(messageOf(r).find("Auto-dispatch:") == std::string::npos);
}

}  // namespace

int main() {
    testOffByDefaultAndReported();
    testAnyDispatcherCanSwitch();
    testArrivalFreesAmbulanceForNextCase();
    testTickDoesNothingWhenOff();
    testSwitchingOnServesBacklog();
    testNewEmergencyIsDispatchedAutomatically();
    testWaitingCaseServedWhenAmbulanceFrees();
    testUndoIsNotRedispatched();
    return report("test_api_autodispatch");
}

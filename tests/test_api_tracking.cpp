// Tests for live tracking in the API (POST /api/tracking/report and the "tracking"
// part of every reply), driven through Api::handle without sockets.
//
// Build and run (Windows: add .exe and run with .\test_api_tracking.exe):
//     g++ -std=c++17 -Iinclude tests/test_api_tracking.cpp src/Api.cpp src/StateJson.cpp src/Json.cpp src/Auth.cpp src/Sha256.cpp src/AutoDispatcher.cpp src/LiveTracker.cpp src/DispatchCoordinator.cpp src/Registry.cpp src/TriageQueue.cpp src/Emergency.cpp src/Hospital.cpp src/Ambulance.cpp src/Graph.cpp src/RoutingEngine.cpp src/CityLoader.cpp -o test_api_tracking
//     ./test_api_tracking

#include <cstdio>
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
        // real coordinates: every node on a line going east from (30, 78), 1 km per road minute
        const GeoPoint o{30.0, 78.0, true};
        for (double km : {0.0, 4.0, 7.0, 12.0, 2.0, 13.0}) m.nodeGeo.push_back(fromLocal(o, km * 1000.0, 0.0));
        m.hospitalNames = {{1, "D General"}, {2, "F Clinic"}};
        return m;
    }
    ApiResponse post(const std::string& token, const std::string& path, const std::string& body = "{}") {
        return api.handle("POST", path, body, "Bearer " + token);
    }
};

bool has(const ApiResponse& r, const std::string& part) { return r.body.find(part) != std::string::npos; }


void testRepliesCarryLiveTrips() {
    World w;
    w.post(w.admin, "/api/tracking/mode", R"({"mode":"gps"})");   // these checks are about GPS-only mode
    w.post(w.admin, "/api/emergencies", R"({"type":"Cardiac","node":1})");
    const ApiResponse r = w.post(w.admin, "/api/dispatch");
    CHECK_EQ(r.status, 200);
    CHECK(has(r, "\"tracking\":{\"mode\":"));
    CHECK(has(r, "\"emergency\":1"));
    CHECK(has(r, "\"mode\":\"gps\""));
    CHECK(has(r, "\"source\":\"waiting for gps\""));   // real mode: nothing moves before a fix
    const ApiResponse none = w.api.handle("POST", "/api/emergencies", "{}", "");
    CHECK(has(none, "\"tracking\":null"));        // not signed in: no positions
}

void testCrewReport() {
    World w;
    w.post(w.admin, "/api/tracking/mode", R"({"mode":"gps"})");   // these checks are about GPS-only mode
    w.post(w.admin, "/api/emergencies", R"({"type":"Cardiac","node":1})");   // patient at B
    w.post(w.admin, "/api/dispatch");                                          // ambulance 11 from C
    ApiResponse r = w.post(w.oper, "/api/tracking/report", R"({"emergency":1,"node":1})");
    CHECK_EQ(r.status, 200);
    CHECK(has(r, "reports reaching B"));
    CHECK(has(r, "\"source\":\"junction report\""));
    r = w.post(w.oper, "/api/tracking/report", R"({"emergency":1,"node":5})");   // F is not on the route
    CHECK_EQ(r.status, 409);
    r = w.post(w.oper, "/api/tracking/report", R"({"emergency":9,"node":1})");
    CHECK_EQ(r.status, 409);
    r = w.post(w.oper, "/api/tracking/report", R"({"emergency":"x"})");
    CHECK_EQ(r.status, 400);
}

void testDeliveredTripStopsBeingTracked() {
    World w;
    w.post(w.admin, "/api/emergencies", R"({"type":"Cardiac","node":1})");
    w.post(w.admin, "/api/dispatch");
    const ApiResponse r = w.post(w.admin, "/api/emergencies/complete", R"({"id":1})");
    CHECK_EQ(r.status, 200);
    CHECK(has(r, "\"trips\":[]"));
}

void testSpeed() {
    World w;
    w.post(w.admin, "/api/tracking/mode", R"({"mode":"gps"})");   // these checks are about GPS-only mode
    CHECK_EQ(w.post(w.oper, "/api/tracking/speed", R"({"msPerMinute":2000})").status, 200);
    CHECK(has(w.post(w.oper, "/api/tracking/speed", R"({"msPerMinute":500})"), "\"secondsPerMinute\":0.5"));
    CHECK_EQ(w.post(w.oper, "/api/tracking/speed", R"({"msPerMinute":0})").status, 400);
}


std::string gpsBody(int emergency, double km, double northMetres = 0.0, double accuracy = 8.0) {
    const GeoPoint p = fromLocal(GeoPoint{30.0, 78.0, true}, km * 1000.0, northMetres);
    char buf[200];
    std::snprintf(buf, sizeof buf, R"({"emergency":%d,"lat":%.8f,"lon":%.8f,"accuracy":%.1f,"speed":10})",
                  emergency, p.lat, p.lon, accuracy);
    return buf;
}

void testGps() {
    World w;
    w.post(w.admin, "/api/tracking/mode", R"({"mode":"gps"})");   // these checks are about GPS-only mode
    w.post(w.admin, "/api/emergencies", R"({"type":"Cardiac","node":1})");   // patient at B
    w.post(w.admin, "/api/dispatch");                                          // ambulance 11 from C (7 km) to B (4 km)
    ApiResponse r = w.post(w.oper, "/api/tracking/gps", gpsBody(1, 5.5));     // halfway C -> B
    CHECK_EQ(r.status, 200);
    CHECK(has(r, "placed on the route of #1"));
    CHECK(has(r, "\"source\":\"gps\""));
    CHECK(has(r, "\"gpsFixes\":1"));
    CHECK(has(r, "\"offRoute\":false"));
    CHECK(has(r, "\"progress\":1.5"));                                         // 1.5 of 3 road min
    CHECK(has(r, "\"lat\":"));
    r = w.post(w.oper, "/api/tracking/gps", gpsBody(1, 5.0, 900.0));          // far off the road
    CHECK_EQ(r.status, 200);
    CHECK(has(r, "from the planned route"));
    CHECK(has(r, "\"offRoute\":true"));
    CHECK(has(r, "\"progress\":1.5"));                                         // not moved
    CHECK_EQ(w.post(w.oper, "/api/tracking/gps", gpsBody(1, 5.0, 0.0, 999.0)).status, 409);  // inaccurate
    CHECK_EQ(w.post(w.oper, "/api/tracking/gps", gpsBody(7, 5.0)).status, 409);              // no such trip
    CHECK_EQ(w.post(w.oper, "/api/tracking/gps", R"({"emergency":1,"lat":"x","lon":1})").status, 400);
    CHECK_EQ(w.post(w.oper, "/api/tracking/gps", R"({"emergency":1,"lat":30})").status, 400);
    // the activity log gets the start of tracking, not every fix
    const ApiResponse s = w.api.handle("GET", "/api/state", "", "Bearer " + w.oper);
    CHECK(has(s, "Live GPS tracking started for #1"));
}

void testMode() {
    World w;
    CHECK_EQ(w.post(w.oper, "/api/tracking/mode", R"({"mode":"simulated"})").status, 403);   // admin only
    CHECK_EQ(w.post(w.admin, "/api/tracking/mode", R"({"mode":"fast"})").status, 400);
    ApiResponse r = w.post(w.admin, "/api/tracking/mode", R"({"mode":"simulated"})");
    CHECK_EQ(r.status, 200);
    CHECK(has(r, "\"mode\":\"simulated\""));
    w.post(w.admin, "/api/emergencies", R"({"type":"Cardiac","node":1})");
    r = w.post(w.admin, "/api/dispatch");
    CHECK(has(r, "\"source\":\"simulated\""));
    r = w.post(w.admin, "/api/tracking/mode", R"({"mode":"gps"})");
    CHECK(has(r, "\"mode\":\"gps\""));
}

}  // namespace

// The default: real-time tracking. The trip moves at real road speed from the moment
// it is dispatched (1 road minute = 60 s), with no phone and no button press.
void testRealTimeIsDefault() {
    World w;
    w.post(w.admin, "/api/emergencies", R"({"type":"Cardiac","node":1})");
    ApiResponse r = w.post(w.admin, "/api/dispatch");
    CHECK(has(r, "\"mode\":\"realtime\""));
    CHECK(has(r, "\"secondsPerMinute\":60"));
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    while (std::chrono::steady_clock::now() < until) { /* let real time pass (no threads: old MinGW) */ }
    r = w.api.handle("GET", "/api/session", "", "Bearer " + w.admin);
    CHECK(has(r, "\"source\":\"real-time estimate\""));
    CHECK(!has(r, "\"progress\":0,"));                 // it has started moving by itself
    // a real fix from the crew takes over, then real-time movement continues from it
    r = w.post(w.oper, "/api/tracking/report", R"({"emergency":1,"node":1})");
    CHECK_EQ(r.status, 200);
    CHECK_EQ(w.post(w.admin, "/api/tracking/mode", R"({"mode":"nonsense"})").status, 400);
}

int main() {
    testRepliesCarryLiveTrips();
    testCrewReport();
    testDeliveredTripStopsBeingTracked();
    testSpeed();
    testGps();
    testMode();
    testRealTimeIsDefault();
    return report("test_api_tracking");
}

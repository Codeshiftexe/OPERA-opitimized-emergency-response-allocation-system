// Tests for LiveTracker (real-time positions). Time is passed in, so no waiting.
// Dead-reckoning tests use TrackingMode::Simulated; GPS tests the default (Gps) mode.
//
// Build and run (Windows: add .exe and run with .\test_live_tracker.exe):
//     g++ -std=c++17 -Iinclude tests/test_live_tracker.cpp src/LiveTracker.cpp src/DispatchCoordinator.cpp src/Registry.cpp src/TriageQueue.cpp src/Emergency.cpp src/Hospital.cpp src/Ambulance.cpp src/Graph.cpp src/RoutingEngine.cpp -o test_live_tracker
//     ./test_live_tracker

#include <string>
#include <vector>

#include "LiveTracker.hpp"
#include "test_util.hpp"

using namespace opera;

// A-B-C-D (4, 3, 5 min) and D-F (1 min).
enum Node { A, B, C, D, F };

static Graph roads() {
    Graph g;
    for (const char* n : {"A", "B", "C", "D", "F"}) g.addNode(n);
    g.addEdge(A, B, 4);
    g.addEdge(B, C, 3);
    g.addEdge(C, D, 5);
    g.addEdge(D, F, 1);
    return g;
}

// Ambulance at A, patient at C, hospital at F: A-B-C (7 min), then C-D-F (6 min).
static DispatchResult trip(int emergency = 1, int ambulance = 10) {
    DispatchResult d;
    d.status = DispatchStatus::Dispatched;
    d.emergencyId = emergency;
    d.ambulanceId = ambulance;
    d.hospitalId = 2;
    d.routeToScene = {A, B, C};
    d.routeToHospital = {C, D, F};
    d.minutesToScene = 7;
    d.minutesToHospital = 6;
    return d;
}

static void testStartsAndMovesInRealTime() {
    const Graph g = roads();
    LiveTracker t(1.0, TrackingMode::Simulated);   // 1 s per road minute
    t.sync({trip()}, g, 100.0);
    CHECK(t.isTracking(1));
    LivePosition p = t.position(1, 100.0);
    CHECK_NEAR(p.progress, 0.0, 1e-9);
    CHECK_NEAR(p.toSceneMinutes, 7.0, 1e-9);
    CHECK_NEAR(p.totalMinutes, 13.0, 1e-9);
    CHECK_EQ(p.phase, 0);
    p = t.position(1, 105.0);                 // 5 s later: 5 road minutes
    CHECK_NEAR(p.progress, 5.0, 1e-9);
    CHECK_EQ(p.phase, 0);
    p = t.position(1, 109.0);
    CHECK_EQ(p.phase, 1);                     // past the patient
    p = t.position(1, 500.0);
    CHECK_NEAR(p.progress, 13.0, 1e-9);       // never beyond the hospital
    CHECK_EQ(p.phase, 2);
    CHECK(!p.reported);
}

static void testCrewReportCorrectsPosition() {
    const Graph g = roads();
    LiveTracker t(1.0, TrackingMode::Simulated);
    t.sync({trip()}, g, 0.0);
    std::string err;
    // traffic: after 6 s the crew is only at B (4 min along), not at 6 min
    CHECK(t.reportNode(1, B, 6.0, err));
    LivePosition p = t.position(1, 6.0);
    CHECK_NEAR(p.progress, 4.0, 1e-9);
    CHECK(p.reported);
    CHECK_EQ(p.reports, 1);
    p = t.position(1, 8.0);                   // dead reckoning from the report
    CHECK_NEAR(p.progress, 6.0, 1e-9);
    CHECK_NEAR(p.secondsSinceFix, 2.0, 1e-9);
    CHECK(!t.reportNode(1, 99, 8.0, err));    // not on the route
    CHECK(err.find("not on the route") != std::string::npos);
    CHECK(!t.reportNode(7, B, 8.0, err));     // not tracked
}

static void testRevisitedNodeUsesNearestVisit() {
    // route A-B-C then back C-B-A... (patient at C, hospital at A)
    Graph g = roads();
    DispatchResult d = trip();
    d.routeToHospital = {C, B, A};
    LiveTracker t(1.0, TrackingMode::Simulated);
    t.sync({d}, g, 0.0);
    std::string err;
    CHECK(t.reportNode(1, B, 10.0, err));     // at 10 min, B is the visit on the way back (cum 10)
    CHECK_NEAR(t.position(1, 10.0).progress, 10.0, 1e-9);
    CHECK(t.reportNode(1, B, 10.0, err));
}

static void testSyncDropsFinishedTrips() {
    const Graph g = roads();
    LiveTracker t(1.0, TrackingMode::Simulated);
    t.sync({trip(1, 10), trip(2, 11)}, g, 0.0);
    CHECK_EQ(t.size(), 2u);
    t.sync({trip(2, 11)}, g, 5.0);           // #1 delivered or undone
    CHECK(!t.isTracking(1));
    CHECK_NEAR(t.position(2, 5.0).progress, 5.0, 1e-9);   // #2 keeps its progress
    t.sync({trip(2, 12)}, g, 6.0);           // undone and dispatched again with another ambulance
    CHECK_NEAR(t.position(2, 6.0).progress, 0.0, 1e-9);
    CHECK_EQ(t.position(2, 6.0).ambulanceId, 12);
    CHECK_EQ(t.all(6.0).size(), 1u);
}

static void testSpeedChangeKeepsProgress() {
    const Graph g = roads();
    LiveTracker t(1.0, TrackingMode::Simulated);
    t.sync({trip()}, g, 0.0);
    CHECK(t.setSecondsPerRoadMinute(2.0, 4.0));   // 4 min covered so far
    CHECK_NEAR(t.position(1, 4.0).progress, 4.0, 1e-9);
    CHECK_NEAR(t.position(1, 8.0).progress, 6.0, 1e-9);   // now 2 s per minute
    CHECK(!t.setSecondsPerRoadMinute(0.0, 8.0));
    CHECK_NEAR(t.secondsPerRoadMinute(), 2.0, 1e-9);
}


// ---- real GPS ------------------------------------------------------------
// The test route A-B-C-D-F laid out on a straight line going east, 1 km per
// road minute, so expected progress is easy to compute.
static const GeoPoint kOrigin{30.0, 78.0, true};

static std::vector<GeoPoint> lineGeo() {
    // A at 0 km, B at 4 km, C at 7 km, D at 12 km, F at 13 km (east of kOrigin)
    std::vector<GeoPoint> g;
    for (double km : {0.0, 4.0, 7.0, 12.0, 13.0}) g.push_back(fromLocal(kOrigin, km * 1000.0, 0.0));
    return g;
}

// A fix `km` east of A and `northMetres` off the road.
static GpsFix fixAt(double km, double northMetres = 0.0, double accuracy = 10.0) {
    const GeoPoint p = fromLocal(kOrigin, km * 1000.0, northMetres);
    GpsFix f;
    f.lat = p.lat;
    f.lon = p.lon;
    f.accuracyMetres = accuracy;
    f.speedMps = 12.0;
    return f;
}

static void testGpsModeNeverMovesByItself() {
    const Graph g = roads();
    LiveTracker t;                             // default: real GPS
    CHECK(t.mode() == TrackingMode::Gps);
    t.setNodeGeo(lineGeo());
    t.sync({trip()}, g, 0.0);
    LivePosition p = t.position(1, 600.0);    // 10 minutes later, no fix
    CHECK_NEAR(p.progress, 0.0, 1e-9);
    CHECK(p.source == FixSource::None);
    CHECK_EQ(sourceLabel(p), std::string("waiting for gps"));
    CHECK_NEAR(p.secondsSinceFix, 600.0, 1e-9);
}

static void testGpsFixPlacesAmbulanceOnRoute() {
    const Graph g = roads();
    LiveTracker t;
    t.setNodeGeo(lineGeo());
    t.sync({trip()}, g, 0.0);
    std::string err;
    CHECK(t.reportGps(1, fixAt(2.5, 30.0), 60.0, err));     // 2.5 km along, 30 m beside the road
    LivePosition p = t.position(1, 60.0);
    CHECK_NEAR(p.progress, 2.5, 0.01);
    CHECK_EQ(p.phase, 0);
    CHECK(p.hasGps);
    CHECK(!p.offRoute);
    CHECK_NEAR(p.offRouteMetres, 30.0, 0.5);
    CHECK_EQ(sourceLabel(p), std::string("gps"));
    CHECK_NEAR(t.position(1, 90.0).progress, 2.5, 0.01);    // stays put until the next fix
    CHECK_NEAR(t.position(1, 90.0).secondsSinceFix, 30.0, 1e-9);

    CHECK(t.reportGps(1, fixAt(8.0), 240.0, err));          // past the patient (C at 7 km)
    p = t.position(1, 240.0);
    CHECK_NEAR(p.progress, 8.0, 0.01);
    CHECK_EQ(p.phase, 1);
    CHECK_EQ(p.gpsFixes, 2);

    CHECK(t.reportGps(1, fixAt(12.97, 15.0), 480.0, err));  // 30 m short of the hospital: arrived
    CHECK_EQ(t.position(1, 480.0).phase, 2);
    CHECK_NEAR(t.position(1, 480.0).progress, 13.0, 1e-9);
}

static void testGpsOffRouteDoesNotMove() {
    const Graph g = roads();
    LiveTracker t;
    t.setNodeGeo(lineGeo());
    t.sync({trip()}, g, 0.0);
    std::string err;
    CHECK(t.reportGps(1, fixAt(3.0), 60.0, err));
    CHECK(t.reportGps(1, fixAt(4.0, 800.0), 120.0, err));   // 800 m off the road: kept, not used
    LivePosition p = t.position(1, 120.0);
    CHECK(p.offRoute);
    CHECK_NEAR(p.offRouteMetres, 800.0, 1.0);
    CHECK_NEAR(p.progress, 3.0, 0.01);
    // a 4-minute detour off the road, then back on the route 4 km further on
    CHECK(t.reportGps(1, fixAt(5.0, 900.0), 300.0, err));
    CHECK(t.reportGps(1, fixAt(7.5), 360.0, err));
    CHECK_NEAR(t.position(1, 360.0).progress, 7.5, 0.01);
    CHECK(!t.position(1, 360.0).offRoute);
}

static void testGpsRefusesBadFixes() {
    const Graph g = roads();
    LiveTracker t;
    t.sync({trip()}, g, 0.0);
    std::string err;
    CHECK(!t.reportGps(1, fixAt(1.0), 10.0, err));          // no coordinates for the route yet
    CHECK(err.find("GEO") != std::string::npos);
    t.setNodeGeo(lineGeo());
    t.sync({trip(2, 11)}, g, 0.0);                          // new trips get the coordinates
    CHECK(!t.reportGps(2, fixAt(1.0, 0.0, 900.0), 10.0, err));   // far too inaccurate
    CHECK(err.find("inaccurate") != std::string::npos);
    GpsFix wild = fixAt(1.0);
    wild.lat = 123.0;
    CHECK(!t.reportGps(2, wild, 10.0, err));
    CHECK(!t.reportGps(9, fixAt(1.0), 10.0, err));          // not tracked
    CHECK(t.reportGps(2, fixAt(1.0), 10.0, err));
}

static void testGpsOutAndBackPicksTheRightVisit() {
    // patient at C, hospital back at A: A-B-C (7 min) then C-B-A (7 min).
    // Near A the fix fits both the start (0 min) and the end (14 min).
    const Graph g = roads();
    DispatchResult d = trip();
    d.routeToHospital = {C, B, A};
    LiveTracker t;
    t.setNodeGeo(lineGeo());
    t.sync({d}, g, 0.0);
    std::string err;
    CHECK(t.reportGps(1, fixAt(0.5), 20.0, err));           // just left A: the outward visit
    CHECK_NEAR(t.position(1, 20.0).progress, 0.5, 0.01);
    CHECK(t.reportGps(1, fixAt(6.0), 400.0, err));          // outward, before C
    CHECK_NEAR(t.position(1, 400.0).progress, 6.0, 0.01);
    CHECK(t.reportGps(1, fixAt(5.0), 600.0, err));          // coming back: 9 min along
    CHECK_NEAR(t.position(1, 600.0).progress, 9.0, 0.01);
    CHECK(t.reportGps(1, fixAt(0.2), 900.0, err));          // nearly back at A
    CHECK_NEAR(t.position(1, 900.0).progress, 13.8, 0.01);
}

static void testModeSwitchKeepsPosition() {
    const Graph g = roads();
    LiveTracker t(1.0, TrackingMode::Simulated);
    t.setNodeGeo(lineGeo());
    t.sync({trip()}, g, 0.0);
    CHECK_EQ(sourceLabel(t.position(1, 3.0)), std::string("simulated"));
    t.setMode(TrackingMode::Gps, 3.0);                      // simulated 3 min so far
    CHECK_NEAR(t.position(1, 50.0).progress, 3.0, 1e-9);    // then frozen: no fix
    std::string err;
    t.setMode(TrackingMode::Simulated, 50.0);
    CHECK(t.reportGps(1, fixAt(5.0), 60.0, err));           // in demo mode a fix still corrects it
    CHECK_NEAR(t.position(1, 60.0).progress, 5.0, 0.01);
    CHECK_EQ(sourceLabel(t.position(1, 60.0)), std::string("gps"));
    CHECK_EQ(sourceLabel(t.position(1, 62.0)), std::string("simulated"));
    TrackingMode m;
    CHECK(parseTrackingMode("gps", m) && m == TrackingMode::Gps);
    CHECK(!parseTrackingMode("GPS!", m));
}

// RealTime: moves by itself from dispatch at real road speed, and a crew fix takes over.
static void testRealTimeModeMovesAtRealSpeed() {
    const Graph g = roads();
    LiveTracker t(1.0, TrackingMode::RealTime);   // the demo speed (1 s) must NOT be used
    t.sync({trip()}, g, 0.0);
    CHECK_NEAR(t.position(1, 60.0).progress, 1.0, 1e-9);      // 60 real seconds = 1 road minute
    CHECK_NEAR(t.position(1, 420.0).progress, 7.0, 1e-9);     // 7 real minutes: at the patient
    CHECK_EQ(t.position(1, 421.0).phase, 1);
    CHECK(t.position(1, 30.0).estimated);
    CHECK(t.position(1, 30.0).realTime);
    std::string err;
    CHECK(t.reportNode(1, B, 100.0, err));        // the crew is only at B (4 min) after 100 s
    CHECK_NEAR(t.position(1, 100.0).progress, 4.0, 1e-9);
    CHECK_NEAR(t.position(1, 160.0).progress, 5.0, 1e-9);     // and moves on from there in real time
    CHECK_NEAR(t.position(1, 5000.0).progress, 13.0, 1e-9);   // never past the hospital
    CHECK_EQ(t.position(1, 5000.0).phase, 2);
}

int main() {
    testStartsAndMovesInRealTime();
    testCrewReportCorrectsPosition();
    testRevisitedNodeUsesNearestVisit();
    testSyncDropsFinishedTrips();
    testSpeedChangeKeepsProgress();
    testGpsModeNeverMovesByItself();
    testGpsFixPlacesAmbulanceOnRoute();
    testGpsOffRouteDoesNotMove();
    testGpsRefusesBadFixes();
    testGpsOutAndBackPicksTheRightVisit();
    testModeSwitchKeepsPosition();
    testRealTimeModeMovesAtRealSpeed();
    return report("test_live_tracker");
}

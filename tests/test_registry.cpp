// Tests for Registry.
//
// Build and run :
//     g++ -std=c++17 -Iinclude tests/test_registry.cpp src/Registry.cpp src/Hospital.cpp src/Ambulance.cpp src/Graph.cpp src/RoutingEngine.cpp -o test_registry
//     ./test_registry

#include <vector>

#include "Registry.hpp"
#include "test_util.hpp"

using namespace opera;

//graph of the city for testing.  A-B-C-D-F is a chain, with a shortcut A-E-D.  Island is disconnected.
//weights are travel time in minutes.  All roads are two-way except the one-way road from Island to D.

enum Node { A, B, C, D, E, F, Island };

static Graph cityRoads() {
    Graph g;
    for (const char* name : {"A", "B", "C", "D", "E", "F", "Island"}) {
        g.addNode(name);
    }
    g.addEdge(A, B, 4, true);
    g.addEdge(B, C, 3, true);
    g.addEdge(C, D, 5, true);
    g.addEdge(A, E, 2, true);
    g.addEdge(E, D, 9, true);
    g.addEdge(D, F, 1, true);
    return g;
}

static void testAddValidation() {
    Registry r(cityRoads());
    CHECK(r.addHospital(Hospital(1, D, 2)));
    CHECK(!r.addHospital(Hospital(1, F, 2)));  // duplicate id
    CHECK(!r.addHospital(Hospital(2, 99, 2))); // node not on map
    CHECK(!r.addHospital(Hospital(-1, D, 2))); // negative id
    CHECK(r.addAmbulance(Ambulance(10, A)));
    CHECK(!r.addAmbulance(Ambulance(10, C)));
    CHECK(!r.addAmbulance(Ambulance(11, -5)));
    CHECK(!r.addAmbulance(Ambulance(-2, A)));
    CHECK_EQ(r.hospitals().size(), 1u);
    CHECK_EQ(r.ambulances().size(), 1u);
}

static void testLookupById() {
    Registry r(cityRoads());
    r.addHospital(Hospital(1, D, 2));
    r.addAmbulance(Ambulance(10, A));

    CHECK(r.hospital(1) != nullptr);
    CHECK_EQ(r.hospital(1)->nodeId(), static_cast<int>(D));
    CHECK(r.hospital(2) == nullptr);
    CHECK(r.ambulance(10) != nullptr);
    CHECK(r.ambulance(11) == nullptr);

    const Registry& readOnly = r;
    CHECK(readOnly.hospital(1) == r.hospital(1));
    CHECK(readOnly.ambulance(10) == r.ambulance(10));
}

// Changes made through a looked-up pointer must stick in the registry.
static void testChangesThroughPointerPersist() {
    Registry r(cityRoads());
    r.addHospital(Hospital(1, D, 2));
    r.addAmbulance(Ambulance(10, A));

    CHECK(r.hospital(1)->reserveBed());
    CHECK_EQ(r.hospital(1)->availableBeds(), 1);
    CHECK(r.ambulance(10)->assignTo(5));
    CHECK(r.ambulance(10)->status() == AmbulanceStatus::Busy);
}

static void testListsSortedById() {
    Registry r(cityRoads());
    r.addAmbulance(Ambulance(12, A));
    r.addAmbulance(Ambulance(3, B));
    r.addAmbulance(Ambulance(7, C));
    r.addHospital(Hospital(9, D, 1));
    r.addHospital(Hospital(4, F, 1));

    const std::vector<const Ambulance*> ambs = r.ambulances();
    CHECK_EQ(ambs.size(), 3u);
    CHECK_EQ(ambs[0]->id(), 3);
    CHECK_EQ(ambs[1]->id(), 7);
    CHECK_EQ(ambs[2]->id(), 12);

    const std::vector<const Hospital*> hosps = r.hospitals();
    CHECK_EQ(hosps[0]->id(), 4);
    CHECK_EQ(hosps[1]->id(), 9);
}

// Scene at B: ambulance 11 at C is 3 min away, 10 at A is 4 min.
static void testNearestAmbulance() {
    Registry r(cityRoads());
    r.addAmbulance(Ambulance(10, A));
    r.addAmbulance(Ambulance(11, C));

    const NearestResult n = r.nearestAvailableAmbulance(B);
    CHECK(n.found());
    CHECK_EQ(n.id, 11);
    CHECK_NEAR(n.minutes, 3.0, 1e-9);
    CHECK(n.route == std::vector<int>({C, B}));
}

static void testAmbulanceAlreadyAtScene() {
    Registry r(cityRoads());
    r.addAmbulance(Ambulance(10, B));
    const NearestResult n = r.nearestAvailableAmbulance(B);
    CHECK_EQ(n.id, 10);
    CHECK_NEAR(n.minutes, 0.0, 1e-9);
    CHECK(n.route == std::vector<int>({B}));
}

static void testBusyOfflineUnreachableSkipped() {
    Registry r(cityRoads());
    r.addAmbulance(Ambulance(10, C));        // nearest, but will be busy
    r.addAmbulance(Ambulance(11, B));        // at the scene, but offline
    r.addAmbulance(Ambulance(12, Island));   // available, but no road
    r.addAmbulance(Ambulance(13, E));        // the only valid choice
    r.ambulance(10)->assignTo(1);
    r.ambulance(11)->goOffline();

    const NearestResult n = r.nearestAvailableAmbulance(B);
    CHECK_EQ(n.id, 13);
    CHECK_NEAR(n.minutes, 6.0, 1e-9);        // E -> A -> B
    CHECK(n.route == std::vector<int>({E, A, B}));
}

static void testNoAmbulanceFound() {
    Registry empty(cityRoads());
    CHECK(!empty.nearestAvailableAmbulance(B).found());

    Registry r(cityRoads());
    r.addAmbulance(Ambulance(10, A));
    CHECK(!r.nearestAvailableAmbulance(Island).found());
    CHECK(!r.nearestAvailableAmbulance(99).found());
    CHECK(!r.nearestAvailableAmbulance(-1).found());

    const NearestResult none = r.nearestAvailableAmbulance(Island);
    CHECK_EQ(none.id, -1);
    CHECK(none.route.empty());
}

// Same distance: the lower id wins, whatever order they were added in.
static void testAmbulanceTieGoesToLowerId() {
    Registry r(cityRoads());
    r.addAmbulance(Ambulance(31, C));
    r.addAmbulance(Ambulance(30, C));
    CHECK_EQ(r.nearestAvailableAmbulance(B).id, 30);
}

// One-way roads: X -> S takes 10, S -> X takes 1. Measuring from the scene
// would wrongly choose the ambulance at X.
static void testOneWayRoads() {
    Graph g;
    const int s = g.addNode("S");
    const int x = g.addNode("X");
    const int y = g.addNode("Y");
    g.addEdge(x, s, 10, false);
    g.addEdge(s, x, 1, false);
    g.addEdge(y, s, 5, true);

    Registry r(g);
    r.addAmbulance(Ambulance(1, x));
    r.addAmbulance(Ambulance(2, y));
    const NearestResult n = r.nearestAvailableAmbulance(s);
    CHECK_EQ(n.id, 2);
    CHECK_NEAR(n.minutes, 5.0, 1e-9);
}

// From B: hospital 1 at D is 8 min, hospital 2 at F is 9 min.
static void testNearestHospital() {
    Registry r(cityRoads());
    r.addHospital(Hospital(1, D, 2));
    r.addHospital(Hospital(2, F, 2));

    const NearestResult n = r.nearestHospitalWithBed(B);
    CHECK_EQ(n.id, 1);
    CHECK_NEAR(n.minutes, 8.0, 1e-9);
    CHECK(n.route == std::vector<int>({B, C, D}));
}

static void testFullHospitalSkipped() {
    Registry r(cityRoads());
    r.addHospital(Hospital(1, D, 1));
    r.addHospital(Hospital(2, F, 1));
    r.addHospital(Hospital(3, Island, 5));
    r.hospital(1)->reserveBed();

    const NearestResult n = r.nearestHospitalWithBed(B);
    CHECK_EQ(n.id, 2);
    CHECK_NEAR(n.minutes, 9.0, 1e-9);
    CHECK(n.route == std::vector<int>({B, C, D, F}));

    r.hospital(2)->reserveBed();
    CHECK(!r.nearestHospitalWithBed(B).found());   // only the island has beds
    CHECK(!r.nearestHospitalWithBed(99).found());
}

static void testHospitalTieGoesToLowerId() {
    Registry r(cityRoads());
    r.addHospital(Hospital(8, D, 1));
    r.addHospital(Hospital(5, D, 1));
    CHECK_EQ(r.nearestHospitalWithBed(B).id, 5);
}

int main() {
    testAddValidation();
    testLookupById();
    testChangesThroughPointerPersist();
    testListsSortedById();
    testNearestAmbulance();
    testAmbulanceAlreadyAtScene();
    testBusyOfflineUnreachableSkipped();
    testNoAmbulanceFound();
    testAmbulanceTieGoesToLowerId();
    testOneWayRoads();
    testNearestHospital();
    testFullHospitalSkipped();
    testHospitalTieGoesToLowerId();
    return report("test_registry");
}
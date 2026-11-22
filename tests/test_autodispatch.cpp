// Tests for AutoDispatcher.
//
// Build and run (Windows: add .exe and run with .\test_autodispatch.exe):
//     g++ -std=c++17 -Iinclude tests/test_autodispatch.cpp src/AutoDispatcher.cpp src/DispatchCoordinator.cpp src/Registry.cpp src/TriageQueue.cpp src/Emergency.cpp src/Hospital.cpp src/Ambulance.cpp src/Graph.cpp src/RoutingEngine.cpp -o test_autodispatch
//     ./test_autodispatch

#include <vector>

#include "AutoDispatcher.hpp"
#include "DispatchCoordinator.hpp"
#include "test_util.hpp"

using namespace opera;

// Same small city as test_dispatch: A-B-C-D, A-E-D, D-F.
enum Node { A, B, C, D, E, F };

static Graph cityRoads() {
    Graph g;
    for (const char* name : {"A", "B", "C", "D", "E", "F"}) {
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

// Two ambulances (10 at A, 11 at C), hospitals at D and F.
static DispatchCoordinator makeCity(int bedsAtD = 2, int bedsAtF = 1) {
    Registry city(cityRoads());
    city.addHospital(Hospital(1, D, bedsAtD));
    city.addHospital(Hospital(2, F, bedsAtF));
    city.addAmbulance(Ambulance(10, A));
    city.addAmbulance(Ambulance(11, C));
    return DispatchCoordinator(city);
}

static void testOffByDefault() {
    DispatchCoordinator dc = makeCity();
    AutoDispatcher ad(dc);
    CHECK(!ad.enabled());
    dc.registerEmergency(EmergencyType::Cardiac, B);
    CHECK(ad.run().empty());                 // switched off: nothing happens
    CHECK_EQ(dc.queue().size(), 1u);
    CHECK_EQ(ad.totalDispatched(), 0);
}

static void testDispatchesEverythingPossible() {
    DispatchCoordinator dc = makeCity();
    AutoDispatcher ad(dc);
    ad.setEnabled(true);
    dc.registerEmergency(EmergencyType::Transfer, E);
    dc.registerEmergency(EmergencyType::Cardiac, B);
    const std::vector<DispatchResult> done = ad.run();
    CHECK_EQ(done.size(), 2u);
    CHECK_EQ(done[0].emergencyId, 2);         // Cardiac first: triage order is respected
    CHECK_EQ(done[1].emergencyId, 1);
    CHECK_EQ(dc.queue().size(), 0u);
    CHECK(ad.lastStop() == DispatchStatus::NoPendingEmergency);
    CHECK_EQ(ad.totalDispatched(), 2);
}

static void testStopsWhenNoAmbulance() {
    DispatchCoordinator dc = makeCity();
    AutoDispatcher ad(dc);
    ad.setEnabled(true);
    dc.registerEmergency(EmergencyType::Cardiac, B);
    dc.registerEmergency(EmergencyType::Trauma, D);
    dc.registerEmergency(EmergencyType::Transfer, F);
    const std::vector<DispatchResult> done = ad.run();
    CHECK_EQ(done.size(), 2u);                // only two ambulances
    CHECK(ad.lastStop() == DispatchStatus::NoAmbulanceAvailable);
    CHECK_EQ(dc.queue().size(), 1u);          // the Transfer waits at the front
    CHECK(dc.queue().peek() != nullptr && dc.queue().peek()->id() == 3);

    // a delivery frees an ambulance: the next run serves the waiting case
    CHECK(dc.completeDispatch(done[0].emergencyId));
    const std::vector<DispatchResult> next = ad.run();
    CHECK_EQ(next.size(), 1u);
    CHECK_EQ(next[0].emergencyId, 3);
    CHECK_EQ(dc.queue().size(), 0u);
}

static void testStopsWhenNoBed() {
    DispatchCoordinator dc = makeCity(1, 0);  // one bed in the whole city
    AutoDispatcher ad(dc);
    ad.setEnabled(true);
    dc.registerEmergency(EmergencyType::Cardiac, B);
    dc.registerEmergency(EmergencyType::Cardiac, E);
    const std::vector<DispatchResult> done = ad.run();
    CHECK_EQ(done.size(), 1u);
    CHECK(ad.lastStop() == DispatchStatus::NoHospitalAvailable);
    CHECK_EQ(dc.queue().size(), 1u);
}

static void testNeverDoubleBooks() {
    DispatchCoordinator dc = makeCity(5, 5);
    AutoDispatcher ad(dc);
    ad.setEnabled(true);
    for (int i = 0; i < 6; ++i) dc.registerEmergency(EmergencyType::Trauma, B);
    std::vector<DispatchResult> done = ad.run();
    CHECK_EQ(done.size(), 2u);
    CHECK(done[0].ambulanceId != done[1].ambulanceId);
    CHECK(ad.run().empty());                   // nothing changed, nothing more to do
    CHECK_EQ(dc.queue().size(), 4u);
}

static void testSwitchOffStops() {
    DispatchCoordinator dc = makeCity();
    AutoDispatcher ad(dc);
    ad.setEnabled(true);
    ad.setEnabled(false);
    dc.registerEmergency(EmergencyType::Cardiac, B);
    CHECK(ad.run().empty());
    CHECK_EQ(dc.queue().size(), 1u);
}

int main() {
    testOffByDefault();
    testDispatchesEverythingPossible();
    testStopsWhenNoAmbulance();
    testStopsWhenNoBed();
    testNeverDoubleBooks();
    testSwitchOffStops();
    return report("test_autodispatch");
}

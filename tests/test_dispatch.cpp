// Tests for DispatchCoordinator.
//
// Build and run (Windows: add .exe and run with .\test_dispatch.exe):
//     g++ -std=c++17 -Iinclude tests/test_dispatch.cpp src/DispatchCoordinator.cpp src/Registry.cpp src/TriageQueue.cpp src/Emergency.cpp src/Hospital.cpp src/Ambulance.cpp src/Graph.cpp src/RoutingEngine.cpp -o test_dispatch
//     ./test_dispatch

#include <vector>

#include "DispatchCoordinator.hpp"
#include "test_util.hpp"

using namespace opera;

//graph for testing: A-B-C-D, A-E-D, D-F, Island
// A-B-C-D is 4+3+5=12, A-E-D is 2+9=11, D-F is 1, Island is unreachable
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

static DispatchCoordinator makeCity(int bedsAtD = 2, int bedsAtF = 1) {
    Registry city(cityRoads());
    city.addHospital(Hospital(1, D, bedsAtD));
    city.addHospital(Hospital(2, F, bedsAtF));
    city.addAmbulance(Ambulance(10, A));
    city.addAmbulance(Ambulance(11, C));
    return DispatchCoordinator(city);
}

static bool sameRoute(const std::vector<int>& got, const std::vector<int>& want) {
    return got == want;
}

static void testRegisterEmergency() {
    DispatchCoordinator dc = makeCity();
    CHECK_EQ(dc.registerEmergency(EmergencyType::Trauma, 99), -1);
    CHECK_EQ(dc.registerEmergency(EmergencyType::Trauma, -1), -1);
    CHECK_EQ(dc.queue().size(), 0u);

    const int first = dc.registerEmergency(EmergencyType::Trauma, B);
    const int second = dc.registerEmergency(EmergencyType::Cardiac, E);
    CHECK_EQ(first, 1);
    CHECK_EQ(second, 2);
    CHECK_EQ(dc.queue().size(), 2u);
    CHECK(dc.findEmergency(first) != nullptr);
    CHECK(dc.findEmergency(first)->status() == EmergencyStatus::Pending);
    CHECK(dc.findEmergency(99) == nullptr);
}

static void testEmptyQueue() {
    DispatchCoordinator dc = makeCity();
    const DispatchResult r = dc.dispatchNext();
    CHECK(r.status == DispatchStatus::NoPendingEmergency);
    CHECK_EQ(r.emergencyId, -1);
    CHECK(!dc.undoLastDispatch());
}

// Emergency at B: ambulance 11 (C, 3 min) beats 10 (A, 4 min).
// From B, hospital 1 at D is 8 min; hospital 2 at F is 9 min.
static void testBasicDispatch() {
    DispatchCoordinator dc = makeCity();
    const int id = dc.registerEmergency(EmergencyType::Trauma, B);
    const DispatchResult r = dc.dispatchNext();

    CHECK(r.status == DispatchStatus::Dispatched);
    CHECK_EQ(r.emergencyId, id);
    CHECK_EQ(r.ambulanceId, 11);
    CHECK_EQ(r.hospitalId, 1);
    CHECK_NEAR(r.minutesToScene, 3.0, 1e-9);
    CHECK_NEAR(r.minutesToHospital, 8.0, 1e-9);
    CHECK(sameRoute(r.routeToScene, {C, B}));
    CHECK(sameRoute(r.routeToHospital, {B, C, D}));

    const Emergency* e = dc.findEmergency(id);
    CHECK(e->status() == EmergencyStatus::Assigned);
    CHECK_EQ(e->assignedAmbulance(), 11);
    CHECK_EQ(e->assignedHospital(), 1);
    CHECK(dc.registry().ambulance(11)->status() == AmbulanceStatus::Busy);
    CHECK_EQ(dc.registry().ambulance(11)->assignedEmergency(), id);
    CHECK(dc.registry().ambulance(10)->status() == AmbulanceStatus::Available);
    CHECK_EQ(dc.registry().hospital(1)->availableBeds(), 1);
    CHECK(dc.queue().empty());
}

static void testHighestPriorityServedFirst() {
    DispatchCoordinator dc = makeCity();
    const int transfer = dc.registerEmergency(EmergencyType::Transfer, B);
    const int cardiac = dc.registerEmergency(EmergencyType::Cardiac, E);
    CHECK_EQ(dc.dispatchNext().emergencyId, cardiac);
    CHECK_EQ(dc.dispatchNext().emergencyId, transfer);
}

// After 200 minutes the transfer scores 20 + 100 = 120, above a fresh cardiac.
static void testAgedCaseServedFirst() {
    DispatchCoordinator dc = makeCity();
    const int transfer = dc.registerEmergency(EmergencyType::Transfer, B);
    CHECK(dc.advanceTime(200));
    const int cardiac = dc.registerEmergency(EmergencyType::Cardiac, B);
    CHECK_EQ(dc.dispatchNext().emergencyId, transfer);
    CHECK_EQ(dc.dispatchNext().emergencyId, cardiac);
}

// Cardiac target is 8 minutes: 0 waited + 3 travel meets it, 6 waited + 3 does not.
static void testMeetsTarget() {
    DispatchCoordinator dc = makeCity();
    dc.registerEmergency(EmergencyType::Cardiac, B);
    CHECK(dc.dispatchNext().meetsTarget);

    DispatchCoordinator late = makeCity();
    late.registerEmergency(EmergencyType::Cardiac, B);
    late.advanceTime(6);
    CHECK(!late.dispatchNext().meetsTarget);
}

static void testNoAmbulanceChangesNothing() {
    Registry city(cityRoads());
    city.addHospital(Hospital(1, D, 5));
    city.addAmbulance(Ambulance(10, A));
    DispatchCoordinator dc(city);
    dc.registerEmergency(EmergencyType::Trauma, B);
    const int waiting = dc.registerEmergency(EmergencyType::Trauma, B);
    CHECK(dc.dispatchNext().status == DispatchStatus::Dispatched);

    const DispatchResult r = dc.dispatchNext();
    CHECK(r.status == DispatchStatus::NoAmbulanceAvailable);
    CHECK_EQ(r.emergencyId, waiting);
    CHECK_EQ(r.ambulanceId, -1);
    CHECK(r.routeToScene.empty());
    CHECK(dc.queue().contains(waiting));
    CHECK(dc.findEmergency(waiting)->status() == EmergencyStatus::Pending);
    CHECK_EQ(dc.registry().hospital(1)->availableBeds(), 4);
}

// Hospital 1 is full, so hospital 2 at F is used even though it is farther.
// Then both are full: the ambulance must stay free and no bed may change.
static void testFullHospitalsFallbackThenRefuse() {
    DispatchCoordinator dc = makeCity(/*bedsAtD=*/0, /*bedsAtF=*/1);
    dc.registerEmergency(EmergencyType::Trauma, B);
    const DispatchResult first = dc.dispatchNext();
    CHECK(first.status == DispatchStatus::Dispatched);
    CHECK_EQ(first.hospitalId, 2);
    CHECK(sameRoute(first.routeToHospital, {B, C, D, F}));
    CHECK_NEAR(first.minutesToHospital, 9.0, 1e-9);

    const int stuck = dc.registerEmergency(EmergencyType::Trauma, B);
    const DispatchResult second = dc.dispatchNext();
    CHECK(second.status == DispatchStatus::NoHospitalAvailable);
    CHECK_EQ(second.emergencyId, stuck);
    CHECK(dc.registry().ambulance(10)->status() == AmbulanceStatus::Available);
    CHECK_EQ(dc.registry().hospital(1)->availableBeds(), 0);
    CHECK_EQ(dc.registry().hospital(2)->availableBeds(), 0);
    CHECK(dc.queue().contains(stuck));
}

static void testOfflineAndUnreachableAmbulancesSkipped() {
    Registry city(cityRoads());
    city.addHospital(Hospital(1, D, 5));
    city.addAmbulance(Ambulance(10, C));
    city.addAmbulance(Ambulance(11, Island));
    city.addAmbulance(Ambulance(12, A));
    DispatchCoordinator dc(city);
    CHECK(dc.setAmbulanceOnline(10, false));
    CHECK(!dc.setAmbulanceOnline(99, false));

    dc.registerEmergency(EmergencyType::Trauma, B);
    const DispatchResult r = dc.dispatchNext();
    CHECK_EQ(r.ambulanceId, 12);

    dc.registerEmergency(EmergencyType::Trauma, B);
    CHECK(dc.dispatchNext().status == DispatchStatus::NoAmbulanceAvailable);
    CHECK(!dc.setAmbulanceOnline(12, false));      // busy: cannot go offline
    CHECK(dc.setAmbulanceOnline(10, true));
    CHECK_EQ(dc.dispatchNext().ambulanceId, 10);
}

static void testUndoRestoresEverything() {
    DispatchCoordinator dc = makeCity();
    const int id = dc.registerEmergency(EmergencyType::Trauma, B);
    const DispatchResult before = dc.dispatchNext();

    CHECK(dc.undoLastDispatch());
    const Emergency* e = dc.findEmergency(id);
    CHECK(e->status() == EmergencyStatus::Pending);
    CHECK_EQ(e->assignedAmbulance(), -1);
    CHECK_EQ(e->assignedHospital(), -1);
    CHECK(dc.registry().ambulance(11)->status() == AmbulanceStatus::Available);
    CHECK_EQ(dc.registry().ambulance(11)->assignedEmergency(), -1);
    CHECK_EQ(dc.registry().hospital(1)->availableBeds(), 2);
    CHECK(dc.queue().contains(id));

    const DispatchResult again = dc.dispatchNext();
    CHECK_EQ(again.ambulanceId, before.ambulanceId);
    CHECK_EQ(again.hospitalId, before.hospitalId);
}

static void testUndoReportsWhichEmergency() {
    DispatchCoordinator dc = makeCity();
    const int first = dc.registerEmergency(EmergencyType::Cardiac, B);
    const int second = dc.registerEmergency(EmergencyType::Trauma, B);
    dc.dispatchNext();
    dc.dispatchNext();

    int undone = 0;
    CHECK(dc.undoLastDispatch(undone));
    CHECK_EQ(undone, second);
    CHECK(dc.undoLastDispatch(undone));
    CHECK_EQ(undone, first);
    CHECK(!dc.undoLastDispatch(undone));
    CHECK_EQ(undone, -1);
}

static void testActiveDispatchesTracked() {
    DispatchCoordinator dc = makeCity();
    CHECK(dc.activeDispatches().empty());

    const int first = dc.registerEmergency(EmergencyType::Cardiac, B);
    const int second = dc.registerEmergency(EmergencyType::Trauma, E);
    const DispatchResult r1 = dc.dispatchNext();
    const DispatchResult r2 = dc.dispatchNext();

    std::vector<DispatchResult> active = dc.activeDispatches();
    CHECK_EQ(active.size(), 2u);
    CHECK_EQ(active[0].emergencyId, first);
    CHECK_EQ(active[1].emergencyId, second);
    CHECK(sameRoute(active[0].routeToScene, r1.routeToScene));
    CHECK(sameRoute(active[1].routeToHospital, r2.routeToHospital));
    CHECK_EQ(active[1].ambulanceId, r2.ambulanceId);

    dc.undoLastDispatch();
    active = dc.activeDispatches();
    CHECK_EQ(active.size(), 1u);
    CHECK_EQ(active[0].emergencyId, first);

    dc.completeDispatch(first);
    CHECK(dc.activeDispatches().empty());

    dc.dispatchNext();
    active = dc.activeDispatches();
    CHECK_EQ(active.size(), 1u);
    CHECK_EQ(active[0].emergencyId, second);
}

static void testFailedDispatchNotActive() {
    DispatchCoordinator dc = makeCity(/*bedsAtD=*/0, /*bedsAtF=*/0);
    dc.registerEmergency(EmergencyType::Trauma, B);
    CHECK(dc.dispatchNext().status == DispatchStatus::NoHospitalAvailable);
    CHECK(dc.activeDispatches().empty());
}

static void testUndoIsLastInFirstOut() {
    DispatchCoordinator dc = makeCity();
    const int first = dc.registerEmergency(EmergencyType::Cardiac, B);
    const int second = dc.registerEmergency(EmergencyType::Trauma, B);
    dc.dispatchNext();
    dc.dispatchNext();

    CHECK(dc.undoLastDispatch());
    CHECK(dc.findEmergency(second)->status() == EmergencyStatus::Pending);
    CHECK(dc.findEmergency(first)->status() == EmergencyStatus::Assigned);
    CHECK(dc.undoLastDispatch());
    CHECK(dc.findEmergency(first)->status() == EmergencyStatus::Pending);
    CHECK(!dc.undoLastDispatch());
    CHECK_EQ(dc.queue().size(), 2u);
}

static void testCompleteDispatch() {
    DispatchCoordinator dc = makeCity();
    const int id = dc.registerEmergency(EmergencyType::Trauma, B);
    dc.dispatchNext();

    CHECK(dc.completeDispatch(id));
    CHECK(dc.findEmergency(id)->status() == EmergencyStatus::Resolved);
    CHECK(dc.registry().ambulance(11)->status() == AmbulanceStatus::Available);
    CHECK_EQ(dc.registry().ambulance(11)->nodeId(), static_cast<int>(D));
    CHECK_EQ(dc.registry().hospital(1)->availableBeds(), 1);   // patient admitted

    CHECK(!dc.completeDispatch(id));
    CHECK(!dc.completeDispatch(99));
}

// The newest dispatch was completed, so undo skips it and reverses the older one.
static void testUndoSkipsCompletedDispatches() {
    DispatchCoordinator dc = makeCity();
    const int first = dc.registerEmergency(EmergencyType::Cardiac, B);
    const int second = dc.registerEmergency(EmergencyType::Trauma, B);
    dc.dispatchNext();
    dc.dispatchNext();
    dc.completeDispatch(second);

    CHECK(dc.undoLastDispatch());
    CHECK(dc.findEmergency(first)->status() == EmergencyStatus::Pending);
    CHECK(dc.findEmergency(second)->status() == EmergencyStatus::Resolved);
    CHECK(!dc.undoLastDispatch());
}

static void testDischargeAndClock() {
    DispatchCoordinator dc = makeCity();
    CHECK(!dc.dischargePatient(1));        // no one admitted yet
    CHECK(!dc.dischargePatient(99));
    dc.registerEmergency(EmergencyType::Trauma, B);
    dc.dispatchNext();
    CHECK(dc.dischargePatient(1));
    CHECK_EQ(dc.registry().hospital(1)->availableBeds(), 2);

    CHECK_EQ(dc.now(), 0);
    CHECK(!dc.advanceTime(0));
    CHECK(!dc.advanceTime(-5));
    CHECK(dc.advanceTime(15));
    CHECK_EQ(dc.now(), 15);
}

int main() {
    testRegisterEmergency();
    testEmptyQueue();
    testBasicDispatch();
    testHighestPriorityServedFirst();
    testAgedCaseServedFirst();
    testMeetsTarget();
    testNoAmbulanceChangesNothing();
    testFullHospitalsFallbackThenRefuse();
    testOfflineAndUnreachableAmbulancesSkipped();
    testUndoRestoresEverything();
    testUndoReportsWhichEmergency();
    testActiveDispatchesTracked();
    testFailedDispatchNotActive();
    testUndoIsLastInFirstOut();
    testCompleteDispatch();
    testUndoSkipsCompletedDispatches();
    testDischargeAndClock();
    return report("test_dispatch");
}
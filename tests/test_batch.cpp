// Tests for BatchPlanner (backtracking) and DispatchCoordinator::dispatchBatch.
//
// Build and run:
//     g++ -std=c++17 -Iinclude tests/test_batch.cpp src/BatchPlanner.cpp src/DispatchCoordinator.cpp src/Registry.cpp src/TriageQueue.cpp src/Emergency.cpp src/Hospital.cpp src/Ambulance.cpp src/Graph.cpp src/RoutingEngine.cpp -o test_batch
//     ./test_batch

#include <memory>
#include <vector>

#include "BatchPlanner.hpp"
#include "DispatchCoordinator.hpp"
#include "RoutingEngine.hpp"
#include "test_util.hpp"

using namespace opera;

// The greedy trap. Two-way roads, minutes:
//
//     Cardiac --2-- X --4-- Trauma        ambulance 1 at X, ambulance 2 at Y
//        |                    |
//        3                   20           hospital 1 at H
//        |                    |
//        Y                    H --1-- Cardiac
//
// From X: Cardiac 2, Trauma 4.   From Y: Cardiac 3, Trauma 9 (Y-Cardiac-X-Trauma).
// One at a time, Cardiac takes X (nearest), leaving Trauma with Y: 2 + 9 = 11 min.
// Together: Cardiac <- Y, Trauma <- X: 3 + 4 = 7 min.
enum Node { Cardiac, Trauma, X, Y, H, Island };

static Graph trapRoads() {
    Graph g;
    for (const char* n : {"Cardiac", "Trauma", "X", "Y", "H", "Island"}) g.addNode(n);
    g.addEdge(Cardiac, X, 2);
    g.addEdge(X, Trauma, 4);
    g.addEdge(Cardiac, Y, 3);
    g.addEdge(Trauma, H, 20);
    g.addEdge(H, Cardiac, 1);
    return g;
}

static Registry trapCity(int beds = 5) {
    Registry r(trapRoads());
    r.addHospital(Hospital(1, H, beds));
    r.addAmbulance(Ambulance(1, X));
    r.addAmbulance(Ambulance(2, Y));
    return r;
}

static void testPlannerBeatsGreedy() {
    const Registry city = trapCity();
    auto c = makeEmergency(EmergencyType::Cardiac, 1, Cardiac, 0);
    auto t = makeEmergency(EmergencyType::Trauma, 2, Trauma, 0);
    const BatchPlan p = BatchPlanner::plan(city, {c.get(), t.get()}, 0);

    CHECK_EQ(p.assignments.size(), 2u);
    if (p.assignments.size() != 2u) return;
    CHECK_EQ(p.assignments[0].emergencyId, 1);
    CHECK_EQ(p.assignments[0].ambulanceId, 2);              // Cardiac <- Y
    CHECK_EQ(p.assignments[1].ambulanceId, 1);              // Trauma  <- X
    CHECK_NEAR(p.assignments[0].minutes, 3.0, 1e-9);
    CHECK_NEAR(p.assignments[1].minutes, 4.0, 1e-9);
    CHECK(p.assignments[0].route == std::vector<int>({Y, Cardiac}));
    CHECK_NEAR(p.cost, 100 * 3.0 + 80 * 4.0, 1e-9);          // 620
    CHECK_NEAR(p.greedyCost, 100 * 2.0 + 80 * 9.0, 1e-9);    // 920
    CHECK_EQ(p.greedyAssigned, 2);
}

static void testBatchCommitsThePlan() {
    DispatchCoordinator dc(trapCity());
    const int c = dc.registerEmergency(EmergencyType::Cardiac, Cardiac);
    const int t = dc.registerEmergency(EmergencyType::Trauma, Trauma);
    const BatchResult b = dc.dispatchBatch();

    CHECK_EQ(b.results.size(), 2u);
    CHECK(b.results[0].status == DispatchStatus::Dispatched);
    CHECK(b.results[1].status == DispatchStatus::Dispatched);
    CHECK_EQ(b.results[0].emergencyId, c);
    CHECK_EQ(b.results[0].ambulanceId, 2);
    CHECK_EQ(b.results[1].ambulanceId, 1);
    CHECK(b.results[0].routeToHospital == std::vector<int>({Cardiac, H}));
    CHECK(b.cost < b.greedyCost);
    CHECK(b.explored > 0);

    CHECK(dc.findEmergency(c)->status() == EmergencyStatus::Assigned);
    CHECK(dc.findEmergency(t)->status() == EmergencyStatus::Assigned);
    CHECK(dc.registry().ambulance(1)->status() == AmbulanceStatus::Busy);
    CHECK(dc.registry().ambulance(2)->status() == AmbulanceStatus::Busy);
    CHECK_EQ(dc.registry().hospital(1)->availableBeds(), 3);
    CHECK(dc.queue().empty());
    CHECK_EQ(dc.activeDispatches().size(), 2u);
}

// One ambulance, two cases it can reach. The Transfer is nearer, but the
// Cardiac must be served: urgency comes before travel time.
static void testUrgentCaseWinsAScarceAmbulance() {
    Graph g;
    for (const char* n : {"Amb", "Near", "Far"}) g.addNode(n);
    g.addEdge(0, 1, 1);
    g.addEdge(0, 2, 30);
    Registry city(g);
    city.addAmbulance(Ambulance(1, 0));

    auto cardiac = makeEmergency(EmergencyType::Cardiac, 1, 2, 0);    // far
    auto transfer = makeEmergency(EmergencyType::Transfer, 2, 1, 0);  // near
    const BatchPlan p = BatchPlanner::plan(city, {cardiac.get(), transfer.get()}, 0);
    CHECK_EQ(p.assignments.size(), 1u);
    if (p.assignments.size() != 1u) return;
    CHECK_EQ(p.assignments[0].emergencyId, 1);
}

// Dispatching one at a time, an unreachable top case blocks everyone behind it.
// A batch serves the others and leaves only that case waiting.
static void testUnreachableCaseDoesNotBlockOthers() {
    DispatchCoordinator dc(trapCity());
    const int stuck = dc.registerEmergency(EmergencyType::Cardiac, Island);
    const int ok = dc.registerEmergency(EmergencyType::Trauma, Trauma);

    CHECK(dc.dispatchNext().status == DispatchStatus::NoAmbulanceAvailable);   // blocked

    const BatchResult b = dc.dispatchBatch();
    CHECK_EQ(b.results.size(), 2u);
    CHECK(b.results[0].status == DispatchStatus::NoAmbulanceAvailable);
    CHECK_EQ(b.results[0].emergencyId, stuck);
    CHECK(b.results[1].status == DispatchStatus::Dispatched);
    CHECK_EQ(b.results[1].emergencyId, ok);
    CHECK(dc.findEmergency(stuck)->status() == EmergencyStatus::Pending);
    CHECK(dc.queue().contains(stuck));
    CHECK_EQ(dc.queue().size(), 1u);
}

// One bed left: the more urgent case gets it; the other keeps its place in the
// queue, and the ambulance it would have used stays free.
static void testLastBedGoesToMostUrgent() {
    DispatchCoordinator dc(trapCity(/*beds=*/1));
    const int trauma = dc.registerEmergency(EmergencyType::Trauma, Trauma);
    const int cardiac = dc.registerEmergency(EmergencyType::Cardiac, Cardiac);
    const BatchResult b = dc.dispatchBatch();

    CHECK_EQ(b.results[0].emergencyId, cardiac);           // most urgent first
    CHECK(b.results[0].status == DispatchStatus::Dispatched);
    CHECK_EQ(b.results[1].emergencyId, trauma);
    CHECK(b.results[1].status == DispatchStatus::NoHospitalAvailable);
    CHECK(dc.findEmergency(trauma)->status() == EmergencyStatus::Pending);
    CHECK(dc.queue().contains(trauma));
    CHECK_EQ(dc.registry().hospital(1)->availableBeds(), 0);
    int busy = 0;
    for (const Ambulance* a : dc.registry().ambulances()) {
        if (a->status() == AmbulanceStatus::Busy) ++busy;
    }
    CHECK_EQ(busy, 1);
}

static void testUndoAfterBatch() {
    DispatchCoordinator dc(trapCity());
    const int c = dc.registerEmergency(EmergencyType::Cardiac, Cardiac);
    const int t = dc.registerEmergency(EmergencyType::Trauma, Trauma);
    dc.dispatchBatch();

    int undone = 0;
    CHECK(dc.undoLastDispatch(undone));
    CHECK_EQ(undone, t);                                   // last committed comes back first
    CHECK(dc.undoLastDispatch(undone));
    CHECK_EQ(undone, c);
    CHECK(!dc.undoLastDispatch());
    CHECK_EQ(dc.registry().hospital(1)->availableBeds(), 5);
    CHECK(dc.registry().ambulance(1)->isAvailable());
    CHECK(dc.registry().ambulance(2)->isAvailable());
    CHECK_EQ(dc.queue().size(), 2u);
}

static void testNothingToDo() {
    DispatchCoordinator empty(trapCity());
    BatchResult b = empty.dispatchBatch();
    CHECK_EQ(b.results.size(), 1u);
    CHECK(b.results[0].status == DispatchStatus::NoPendingEmergency);

    Registry noFree(trapRoads());
    noFree.addHospital(Hospital(1, H, 5));
    noFree.addAmbulance(Ambulance(1, X));
    noFree.ambulance(1)->goOffline();
    DispatchCoordinator dc(noFree);
    const int id = dc.registerEmergency(EmergencyType::Trauma, Trauma);
    b = dc.dispatchBatch();
    CHECK(b.results[0].status == DispatchStatus::NoAmbulanceAvailable);
    CHECK_EQ(b.results[0].emergencyId, id);
    CHECK(dc.queue().contains(id));
}

// Never more cases than free ambulances, and never more than kMaxCases.
static void testBatchSizeLimits() {
    DispatchCoordinator dc(trapCity());
    dc.registerEmergency(EmergencyType::Cardiac, Cardiac);
    dc.registerEmergency(EmergencyType::Trauma, Trauma);
    const int third = dc.registerEmergency(EmergencyType::Transfer, Trauma);
    const BatchResult b = dc.dispatchBatch();
    CHECK_EQ(b.results.size(), 2u);                        // only 2 free ambulances
    CHECK(dc.queue().contains(third));

    Graph g;
    g.addNode("Hub");
    Registry big(g);
    big.addHospital(Hospital(1, 0, 20));
    for (int i = 1; i <= 8; ++i) big.addAmbulance(Ambulance(i, 0));
    DispatchCoordinator many(big);
    for (int i = 0; i < 8; ++i) many.registerEmergency(EmergencyType::Trauma, 0);
    CHECK_EQ(many.dispatchBatch().results.size(), static_cast<std::size_t>(BatchPlanner::kMaxCases));
    CHECK_EQ(many.queue().size(), 2u);
}

// ---- Brute force: every possible way to assign, no pruning ----------------

struct Brute {
    std::vector<std::vector<double>> cost;
    std::vector<double> unserved;
    std::vector<bool> used;
    double best = INF;
    void go(std::size_t c, double total) {
        if (c == cost.size()) { if (total < best) best = total; return; }
        for (std::size_t a = 0; a < used.size(); ++a) {
            if (used[a] || cost[c][a] == INF) continue;
            used[a] = true;
            go(c + 1, total + cost[c][a]);
            used[a] = false;
        }
        go(c + 1, total + unserved[c]);
    }
};

static void testMatchesBruteForceOnRandomCities() {
    unsigned seed = 2026;
    auto next = [&seed]() { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7fff; };
    const EmergencyType types[] = {EmergencyType::Cardiac, EmergencyType::Trauma,
                                   EmergencyType::Transfer};
    int mismatches = 0, badPlans = 0, worseThanGreedy = 0, trials = 0;

    for (int trial = 0; trial < 300; ++trial) {
        Graph g;
        const int n = 3 + static_cast<int>(next() % 8);
        for (int i = 0; i < n; ++i) g.addNode("n" + std::to_string(i));
        const int roads = static_cast<int>(next() % (2 * n));
        for (int k = 0; k < roads; ++k) {
            g.addEdge(static_cast<int>(next() % n), static_cast<int>(next() % n),
                      static_cast<double>(1 + next() % 15), next() % 3 != 0);
        }
        Registry city(g);
        const int ambulances = 1 + static_cast<int>(next() % 6);
        for (int a = 1; a <= ambulances; ++a) city.addAmbulance(Ambulance(a, static_cast<int>(next() % n)));

        std::vector<std::unique_ptr<Emergency>> owned;
        std::vector<const Emergency*> cases;
        const int k = 1 + static_cast<int>(next() % 6);
        for (int i = 0; i < k; ++i) {
            owned.push_back(makeEmergency(types[next() % 3], i + 1, static_cast<int>(next() % n),
                                          static_cast<int>(next() % 50)));
            cases.push_back(owned.back().get());
        }
        const int now = 60;
        const BatchPlan p = BatchPlanner::plan(city, cases, now);

        // The brute force sees exactly the same costs.
        Brute b;
        std::vector<const Ambulance*> free = city.ambulances();
        b.cost.assign(cases.size(), std::vector<double>(free.size(), INF));
        b.used.assign(free.size(), false);
        for (std::size_t c = 0; c < cases.size(); ++c) {
            const double w = cases[c]->priorityAt(now);
            b.unserved.push_back(w * BatchPlanner::kUnservedMinutes);
            for (std::size_t a = 0; a < free.size(); ++a) {
                const ShortestPaths sp = dijkstra(g, free[a]->nodeId());
                if (sp.reachable(cases[c]->nodeId())) b.cost[c][a] = w * sp.distanceTo(cases[c]->nodeId());
            }
        }
        b.go(0, 0.0);

        // The planner's answer, in the same terms.
        double planTotal = 0.0;
        std::vector<bool> served(cases.size(), false), ambUsed(ambulances + 1, false);
        for (const BatchAssignment& a : p.assignments) {
            const std::size_t c = static_cast<std::size_t>(a.emergencyId - 1);
            planTotal += cases[c]->priorityAt(now) * a.minutes;
            if (served[c] || ambUsed[a.ambulanceId]) ++badPlans;   // used twice
            served[c] = true;
            ambUsed[a.ambulanceId] = true;
        }
        double greedyTotal = p.greedyCost;
        int unservedByGreedy = static_cast<int>(cases.size()) - p.greedyAssigned;
        for (std::size_t c = 0; c < cases.size(); ++c) {
            if (!served[c]) planTotal += cases[c]->priorityAt(now) * BatchPlanner::kUnservedMinutes;
        }
        if (std::fabs(planTotal - b.best) > 1e-6 * (1 + b.best)) ++mismatches;
        if (unservedByGreedy == 0 && p.cost > greedyTotal + 1e-9 &&
            p.assignments.size() == cases.size()) ++worseThanGreedy;
        ++trials;
    }
    CHECK_EQ(trials, 300);
    CHECK_EQ(mismatches, 0);
    CHECK_EQ(badPlans, 0);
    CHECK_EQ(worseThanGreedy, 0);
}

int main() {
    testPlannerBeatsGreedy();
    testBatchCommitsThePlan();
    testUrgentCaseWinsAScarceAmbulance();
    testUnreachableCaseDoesNotBlockOthers();
    testLastBedGoesToMostUrgent();
    testUndoAfterBatch();
    testNothingToDo();
    testBatchSizeLimits();
    testMatchesBruteForceOnRandomCities();
    return report("test_batch");
}

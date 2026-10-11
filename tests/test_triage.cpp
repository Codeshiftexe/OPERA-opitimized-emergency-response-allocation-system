// Tests for TriageQueue.
//
// Build and run:
//     g++ -std=c++17 -Iinclude tests/test_triage.cpp src/TriageQueue.cpp src/Emergency.cpp -o test_triage
//     ./test_triage

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "TriageQueue.hpp"
#include "test_util.hpp"

using namespace opera;

using Owned = std::vector<std::unique_ptr<Emergency>>;

static Emergency* add(Owned& store, EmergencyType type, int id, int reportedAt) {
    store.push_back(makeEmergency(type, id, 0, reportedAt));
    return store.back().get();
}

static void testEmptyQueue() {
    TriageQueue q;
    CHECK(q.empty());
    CHECK_EQ(q.size(), 0u);
    CHECK(q.peek() == nullptr);
    CHECK(q.pop() == nullptr);
    CHECK(q.snapshot().empty());
}

static void testRejectsBadPushes() {
    Owned store;
    TriageQueue q;

    CHECK(!q.push(nullptr));

    Emergency* assigned = add(store, EmergencyType::Cardiac, 1, 0);
    assigned->assign(3, 4);
    CHECK(!q.push(assigned));

    Emergency* e = add(store, EmergencyType::Trauma, 2, 0);
    CHECK(q.push(e));
    CHECK(!q.push(e));
    CHECK_EQ(q.size(), 1u);
}

static void testSameTimeOrderedByType() {
    Owned store;
    TriageQueue q;
    q.push(add(store, EmergencyType::Transfer, 1, 0));
    q.push(add(store, EmergencyType::Trauma, 2, 0));
    q.push(add(store, EmergencyType::Cardiac, 3, 0));

    CHECK_EQ(q.pop()->typeName(), std::string("Cardiac"));
    CHECK_EQ(q.pop()->typeName(), std::string("Trauma"));
    CHECK_EQ(q.pop()->typeName(), std::string("Transfer"));
    CHECK(q.empty());
}

// The heap must not depend on the order calls arrived in.
static void testInsertionOrderIrrelevant() {
    std::vector<EmergencyType> types = {EmergencyType::Cardiac, EmergencyType::Trauma,
                                        EmergencyType::Transfer};
    std::sort(types.begin(), types.end());
    int permutations = 0;
    do {
        Owned store;
        TriageQueue q;
        int id = 1;
        for (EmergencyType t : types) {
            q.push(add(store, t, id++, 0));
        }
        CHECK_EQ(q.pop()->basePriority(), 100);
        CHECK_EQ(q.pop()->basePriority(), 80);
        CHECK_EQ(q.pop()->basePriority(), 20);
        ++permutations;
    } while (std::next_permutation(types.begin(), types.end()));
    CHECK_EQ(permutations, 6);
}

static void testAgingCanOvertake() {
    // At minute 100: transfer 20 + 50 = 70, fresh cardiac 100. Cardiac first.
    {
        Owned store;
        TriageQueue q;
        q.push(add(store, EmergencyType::Transfer, 1, 0));
        q.push(add(store, EmergencyType::Cardiac, 2, 100));
        CHECK_EQ(q.pop()->id(), 2);
    }
    // At minute 200: transfer 20 + 100 = 120, fresh cardiac 100. Transfer first.
    {
        Owned store;
        TriageQueue q;
        q.push(add(store, EmergencyType::Transfer, 1, 0));
        q.push(add(store, EmergencyType::Cardiac, 2, 200));
        CHECK_EQ(q.pop()->id(), 1);
    }
}

// At minute 160 both score exactly 100. The one waiting longer goes first.
static void testExactTieGoesToEarlierCall() {
    Owned store;
    TriageQueue q;
    q.push(add(store, EmergencyType::Cardiac, 1, 160));
    q.push(add(store, EmergencyType::Transfer, 2, 0));
    CHECK_EQ(q.pop()->id(), 2);
    CHECK_EQ(q.pop()->id(), 1);
}

static void testSameTypeSameTimeByLowerId() {
    Owned store;
    TriageQueue q;
    q.push(add(store, EmergencyType::Trauma, 9, 5));
    q.push(add(store, EmergencyType::Trauma, 4, 5));
    q.push(add(store, EmergencyType::Trauma, 7, 5));
    CHECK_EQ(q.pop()->id(), 4);
    CHECK_EQ(q.pop()->id(), 7);
    CHECK_EQ(q.pop()->id(), 9);
}

static void testPeekDoesNotRemove() {
    Owned store;
    TriageQueue q;
    q.push(add(store, EmergencyType::Trauma, 1, 0));
    q.push(add(store, EmergencyType::Cardiac, 2, 0));

    Emergency* seen = q.peek();
    CHECK_EQ(seen->id(), 2);
    CHECK_EQ(q.size(), 2u);
    CHECK(q.pop() == seen);
    CHECK_EQ(q.size(), 1u);
}

// No free ambulance: the coordinator pops, cannot serve, and pushes it back.
static void testContainsAndRequeue() {
    Owned store;
    TriageQueue q;
    Emergency* e = add(store, EmergencyType::Cardiac, 5, 0);

    CHECK(!q.contains(5));
    q.push(e);
    CHECK(q.contains(5));
    q.pop();
    CHECK(!q.contains(5));
    CHECK(q.push(e));
    CHECK(q.contains(5));
}

static void testSnapshotKeepsQueueIntact() {
    Owned store;
    TriageQueue q;
    q.push(add(store, EmergencyType::Transfer, 1, 0));
    q.push(add(store, EmergencyType::Cardiac, 2, 10));
    q.push(add(store, EmergencyType::Trauma, 3, 5));

    std::vector<const Emergency*> order = q.snapshot();
    CHECK_EQ(order.size(), 3u);
    CHECK_EQ(q.size(), 3u);
    for (const Emergency* expected : order) {
        CHECK(q.pop() == expected);
    }
}

static Emergency* bruteForceBest(const std::vector<Emergency*>& pool, int lateTime) {
    Emergency* best = nullptr;
    for (Emergency* e : pool) {
        if (best == nullptr) { best = e; continue; }
        const double pe = e->priorityAt(lateTime);
        const double pb = best->priorityAt(lateTime);
        if (pe > pb ||
            (pe == pb && e->reportedAt() < best->reportedAt()) ||
            (pe == pb && e->reportedAt() == best->reportedAt() && e->id() < best->id())) {
            best = e;
        }
    }
    return best;
}

static void testInterleavedAgainstBruteForce() {
    Owned store;
    TriageQueue q;
    std::vector<Emergency*> pool;
    unsigned seed = 12345;
    auto next = [&seed]() { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7fff; };

    const EmergencyType types[] = {EmergencyType::Cardiac, EmergencyType::Trauma,
                                   EmergencyType::Transfer};
    const int lateTime = 100000;
    int clock = 0, nextId = 1, mismatches = 0, pops = 0;

    for (int step = 0; step < 400; ++step) {
        clock += static_cast<int>(next() % 7);
        if (next() % 3 != 0 || pool.empty()) {
            Emergency* e = add(store, types[next() % 3], nextId++, clock);
            q.push(e);
            pool.push_back(e);
        } else {
            Emergency* expected = bruteForceBest(pool, lateTime);
            Emergency* got = q.pop();
            if (got != expected) ++mismatches;
            pool.erase(std::find(pool.begin(), pool.end(), expected));
            ++pops;
        }
    }
    CHECK(pops > 50);
    CHECK_EQ(mismatches, 0);
    CHECK_EQ(q.size(), pool.size());
}

// Pop order must match sorting by live priority at any time after the last call.
static void testOrderHoldsAtEveryLaterTime() {
    Owned store;
    TriageQueue q;
    const EmergencyType types[] = {EmergencyType::Transfer, EmergencyType::Cardiac,
                                   EmergencyType::Trauma};
    for (int i = 0; i < 30; ++i) {
        q.push(add(store, types[i % 3], i + 1, (i * 37) % 180));
    }
    std::vector<const Emergency*> order = q.snapshot();

    for (int now : {180, 250, 1000}) {
        bool nonIncreasing = true;
        for (std::size_t i = 1; i < order.size(); ++i) {
            if (order[i]->priorityAt(now) > order[i - 1]->priorityAt(now)) {
                nonIncreasing = false;
            }
        }
        CHECK(nonIncreasing);
    }
}

int main() {
    testEmptyQueue();
    testRejectsBadPushes();
    testSameTimeOrderedByType();
    testInsertionOrderIrrelevant();
    testAgingCanOvertake();
    testExactTieGoesToEarlierCall();
    testSameTypeSameTimeByLowerId();
    testPeekDoesNotRemove();
    testContainsAndRequeue();
    testSnapshotKeepsQueueIntact();
    testInterleavedAgainstBruteForce();
    testOrderHoldsAtEveryLaterTime();
    return report("test_triage");
}
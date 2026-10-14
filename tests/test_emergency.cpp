#include "Emergency.hpp"
#include "test_util.hpp"

#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

using namespace opera;

static void testClassDesign() {
    CHECK(std::is_abstract<Emergency>::value);
    CHECK(std::has_virtual_destructor<Emergency>::value);
    CHECK(!std::is_copy_constructible<Emergency>::value);
    CHECK(!std::is_copy_assignable<Emergency>::value);
    CHECK((std::is_base_of<Emergency, CardiacEmergency>::value));
    CHECK((std::is_base_of<Emergency, TraumaEmergency>::value));
    CHECK((std::is_base_of<Emergency, TransferRequest>::value));
}

static void testFactoryBuildsCorrectType() {
    auto c = makeEmergency(EmergencyType::Cardiac, 1, 0, 0);
    auto t = makeEmergency(EmergencyType::Trauma, 2, 0, 0);
    auto r = makeEmergency(EmergencyType::Transfer, 3, 0, 0);

    CHECK(dynamic_cast<CardiacEmergency*>(c.get()) != nullptr);
    CHECK(dynamic_cast<TraumaEmergency*>(t.get()) != nullptr);
    CHECK(dynamic_cast<TransferRequest*>(r.get()) != nullptr);
}

// Same call through a base pointer, different answer per type.
static void testPolymorphism() {
    std::vector<std::unique_ptr<Emergency>> calls;
    calls.push_back(makeEmergency(EmergencyType::Cardiac, 1, 0, 0));
    calls.push_back(makeEmergency(EmergencyType::Trauma, 2, 0, 0));
    calls.push_back(makeEmergency(EmergencyType::Transfer, 3, 0, 0));

    const std::string names[]   = {"Cardiac", "Trauma", "Transfer"};
    const int         base[]    = {100, 80, 20};
    const int         targets[] = {8, 15, 60};

    for (std::size_t i = 0; i < calls.size(); ++i) {
        CHECK(calls[i]->typeName() == names[i]);
        CHECK_EQ(calls[i]->basePriority(), base[i]);
        CHECK_EQ(calls[i]->targetResponseMinutes(), targets[i]);
    }
}

static void testStartsPending() {
    auto e = makeEmergency(EmergencyType::Trauma, 7, 3, 10);
    CHECK_EQ(e->id(), 7);
    CHECK_EQ(e->nodeId(), 3);
    CHECK_EQ(e->reportedAt(), 10);
    CHECK(e->status() == EmergencyStatus::Pending);
    CHECK_EQ(e->assignedAmbulance(), -1);
    CHECK_EQ(e->assignedHospital(), -1);
}

static void testPriorityAging() {
    auto e = makeEmergency(EmergencyType::Trauma, 1, 0, 10);
    CHECK_NEAR(e->priorityAt(10), 80.0, 1e-9);
    CHECK_NEAR(e->priorityAt(30), 90.0, 1e-9);
    CHECK_EQ(e->waitingMinutes(30), 20);
}

// Without aging a transfer could wait forever behind cardiac calls.
// Compares a long-waiting transfer against a cardiac call reported at that moment.
static void testAgingPreventsStarvation() {
    auto transfer = makeEmergency(EmergencyType::Transfer, 1, 0, 0);
    auto freshCardiac = [](int now) {
        return makeEmergency(EmergencyType::Cardiac, 2, 0, now)->priorityAt(now);
    };

    CHECK(transfer->priorityAt(0) < freshCardiac(0));              // 20 < 100
    CHECK_NEAR(transfer->priorityAt(160), freshCardiac(160), 1e-9); // 100 == 100
    CHECK(transfer->priorityAt(170) > freshCardiac(170));          // 105 > 100
}

static void testMeetsTarget() {
    auto cardiac = makeEmergency(EmergencyType::Cardiac, 1, 0, 0);

    CHECK(cardiac->meetsTarget(3, 5.0));     // 3 + 5 = 8, exactly on target
    CHECK(!cardiac->meetsTarget(3, 6.0));    // 9 > 8

    // Queue time counts, not only travel.
    CHECK(!cardiac->meetsTarget(4, 5.0));

    // Same numbers, different type, different answer.
    auto trauma = makeEmergency(EmergencyType::Trauma, 2, 0, 0);
    CHECK(trauma->meetsTarget(3, 6.0));

    const double unreachable = std::numeric_limits<double>::infinity();
    CHECK(!cardiac->meetsTarget(0, unreachable));
    CHECK(!makeEmergency(EmergencyType::Transfer, 3, 0, 0)->meetsTarget(0, unreachable));
}

static void testAssignAndResolve() {
    auto e = makeEmergency(EmergencyType::Cardiac, 1, 0, 0);

    CHECK(!e->resolve());                    // nothing to resolve yet
    CHECK(!e->assign(-1, 2));
    CHECK(!e->assign(4, -1));
    CHECK(e->status() == EmergencyStatus::Pending);

    CHECK(e->assign(4, 2));
    CHECK(e->status() == EmergencyStatus::Assigned);
    CHECK_EQ(e->assignedAmbulance(), 4);
    CHECK_EQ(e->assignedHospital(), 2);

    CHECK(!e->assign(5, 1));                 // no reassignment
    CHECK_EQ(e->assignedAmbulance(), 4);
    CHECK_EQ(e->assignedHospital(), 2);

    CHECK(e->resolve());
    CHECK(e->status() == EmergencyStatus::Resolved);
    CHECK(!e->resolve());
    CHECK(!e->assign(6, 3));
    CHECK_EQ(e->assignedAmbulance(), 4);     // kept for the record
}

static bool invariantHolds(const Emergency& e) {
    const bool pending  = e.status() == EmergencyStatus::Pending;
    const bool noneSet  = e.assignedAmbulance() == -1 && e.assignedHospital() == -1;
    const bool bothSet  = e.assignedAmbulance() >= 0 && e.assignedHospital() >= 0;
    return pending ? noneSet : bothSet;
}

static void testInvariantAfterEveryOperation() {
    auto e = makeEmergency(EmergencyType::Trauma, 1, 0, 0);
    CHECK(invariantHolds(*e));

    e->resolve();       CHECK(invariantHolds(*e));
    e->assign(-1, 3);   CHECK(invariantHolds(*e));
    e->assign(2, -5);   CHECK(invariantHolds(*e));
    e->assign(2, 3);    CHECK(invariantHolds(*e));
    e->assign(9, 9);    CHECK(invariantHolds(*e));
    e->resolve();       CHECK(invariantHolds(*e));
    e->resolve();       CHECK(invariantHolds(*e));
}

static void testStatusToString() {
    CHECK(std::string(toString(EmergencyStatus::Pending)) == "Pending");
    CHECK(std::string(toString(EmergencyStatus::Assigned)) == "Assigned");
    CHECK(std::string(toString(EmergencyStatus::Resolved)) == "Resolved");
}

static void testUnassign() {
    auto e = makeEmergency(EmergencyType::Trauma, 1, 0, 0);

    CHECK(!e->unassign());                       // Pending: nothing to undo
    CHECK(e->status() == EmergencyStatus::Pending);

    CHECK(e->assign(4, 2));
    CHECK(e->unassign());                        // Assigned -> Pending
    CHECK(e->status() == EmergencyStatus::Pending);
    CHECK_EQ(e->assignedAmbulance(), -1);
    CHECK_EQ(e->assignedHospital(), -1);

    CHECK(e->assign(5, 3));                      // can be dispatched again
    CHECK_EQ(e->assignedAmbulance(), 5);

    CHECK(e->resolve());
    CHECK(!e->unassign());                       // Resolved is final
    CHECK(e->status() == EmergencyStatus::Resolved);
    CHECK_EQ(e->assignedAmbulance(), 5);         // refused call changed nothing
}

int main() {
    testClassDesign();
    testFactoryBuildsCorrectType();
    testPolymorphism();
    testStartsPending();
    testPriorityAging();
    testAgingPreventsStarvation();
    testMeetsTarget();
    testAssignAndResolve();
    testInvariantAfterEveryOperation();
    testStatusToString();
    testUnassign();
    return report("test_emergency");
}
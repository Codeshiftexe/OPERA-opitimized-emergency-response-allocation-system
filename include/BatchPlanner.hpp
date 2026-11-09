#pragma once

// Plans which free ambulance goes to which waiting emergency when several
// emergencies are dispatched together, using backtracking.
//
// Dispatching one at a time is greedy: the most urgent case takes its nearest
// ambulance even if that leaves the next case with a far one. Looking at the
// whole batch can do much better:
//
//     Ambulance A: 2 min to Cardiac, 4 min to Trauma
//     Ambulance B: 3 min to Cardiac, 20 min to Trauma
//     one at a time:  Cardiac <- A, Trauma <- B   (2 + 20 = 22 min)
//     whole batch:    Cardiac <- B, Trauma <- A   (3 +  4 =  7 min)
//
// The planner only decides; it changes nothing. DispatchCoordinator commits.

#include <vector>

#include "Emergency.hpp"
#include "Registry.hpp"

namespace opera {

struct BatchAssignment {
    int emergencyId = -1;
    int ambulanceId = -1;
    double minutes = 0.0;        // ambulance to scene
    std::vector<int> route;      // ambulance node ... scene node
};

struct BatchPlan {
    std::vector<BatchAssignment> assignments;   // in the order the cases were given
    double cost = 0.0;           // sum of priority x minutes over the assigned cases
    double greedyCost = 0.0;     // the same measure for one-at-a-time dispatch
    int greedyAssigned = 0;      // how many cases one-at-a-time dispatch would serve
    long long explored = 0;      // search calls made: shows how much pruning saved
};

class BatchPlanner {
public:
    // Up to this many cases are planned together; 6! = 720 orders at most.
    static constexpr int kMaxCases = 6;
    static constexpr double kUnservedMinutes = 1e6;

    // `cases` must be Pending, most urgent first. Each case is weighted by its
    // priority at `now`. Leaving a case unserved costs its priority times
    // kUnservedMinutes, far more than any real trip, so the planner serves the
    // most urgent cases it can and only then minimises travel time. A case no
    // free ambulance can reach is left out.
    static BatchPlan plan(const Registry& registry, const std::vector<const Emergency*>& cases,
                          int now);
};

}  // namespace opera

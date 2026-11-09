#include "BatchPlanner.hpp"

#include <algorithm>
#include <cstddef>

#include "RoutingEngine.hpp"

namespace opera {

namespace {

// Everything the recursion needs, so search() takes only two arguments.
struct Search {
    std::vector<std::vector<double>> cost;      // cost[case][ambulance], INF if unreachable
    std::vector<double> unserved;               // cost of leaving each case for later
    std::vector<std::vector<int>> tryOrder;     // per case: reachable ambulances, cheapest first
    std::vector<bool> used;                     // ambulance already taken in this branch
    std::vector<int> current;                   // current[case] = ambulance index, or -1
    std::vector<int> best;
    double bestCost = INF;
    long long explored = 0;

    void search(std::size_t c, double total) {
        ++explored;
        // Prune: costs never go down, so this branch cannot beat the best plan.
        if (total >= bestCost) return;
        if (c == cost.size()) {
            bestCost = total;
            best = current;
            return;
        }
        for (const int a : tryOrder[c]) {
            if (used[a]) continue;
            used[a] = true;                     // choose
            current[c] = a;
            search(c + 1, total + cost[c][a]);
            used[a] = false;                    // undo the choice: backtrack
            current[c] = -1;
        }
        search(c + 1, total + unserved[c]);     // or leave this case for later
    }
};

}  // namespace

BatchPlan BatchPlanner::plan(const Registry& registry, const std::vector<const Emergency*>& cases,
                             int now) {
    BatchPlan result;
    std::vector<const Ambulance*> free;
    for (const Ambulance* a : registry.ambulances()) {
        if (a->status() == AmbulanceStatus::Available) free.push_back(a);
    }
    const std::size_t n = std::min(cases.size(), static_cast<std::size_t>(kMaxCases));
    if (n == 0 || free.empty()) return result;

    // One Dijkstra run per free ambulance gives its time to every scene.
    std::vector<ShortestPaths> fromAmbulance;
    fromAmbulance.reserve(free.size());
    for (const Ambulance* a : free) fromAmbulance.push_back(dijkstra(registry.roads(), a->nodeId()));

    Search s;
    s.cost.assign(n, std::vector<double>(free.size(), INF));
    s.unserved.resize(n);
    s.tryOrder.resize(n);
    for (std::size_t c = 0; c < n; ++c) {
        const double weight = cases[c]->priorityAt(now);
        s.unserved[c] = weight * kUnservedMinutes;
        for (std::size_t a = 0; a < free.size(); ++a) {
            if (fromAmbulance[a].reachable(cases[c]->nodeId())) {
                s.cost[c][a] = weight * fromAmbulance[a].distanceTo(cases[c]->nodeId());
                s.tryOrder[c].push_back(static_cast<int>(a));
            }
        }
        // Cheapest first, so a good plan is found early and pruning cuts more.
        std::sort(s.tryOrder[c].begin(), s.tryOrder[c].end(), [&](int x, int y) {
            if (s.cost[c][x] != s.cost[c][y]) return s.cost[c][x] < s.cost[c][y];
            return free[x]->id() < free[y]->id();
        });
    }

    // The greedy answer, for comparison: each case in turn takes its cheapest free ambulance.
    std::vector<bool> taken(free.size(), false);
    for (std::size_t c = 0; c < n; ++c) {
        for (const int a : s.tryOrder[c]) {
            if (taken[a]) continue;
            taken[a] = true;
            result.greedyCost += s.cost[c][a];
            ++result.greedyAssigned;
            break;
        }
    }

    s.used.assign(free.size(), false);
    s.current.assign(n, -1);
    s.search(0, 0.0);

    result.explored = s.explored;
    for (std::size_t c = 0; c < n; ++c) {
        const int a = s.best[c];
        if (a < 0) continue;
        result.cost += s.cost[c][a];
        BatchAssignment b;
        b.emergencyId = cases[c]->id();
        b.ambulanceId = free[a]->id();
        b.minutes = fromAmbulance[a].distanceTo(cases[c]->nodeId());
        b.route = fromAmbulance[a].pathTo(cases[c]->nodeId());
        result.assignments.push_back(b);
    }
    return result;
}

}  // namespace opera

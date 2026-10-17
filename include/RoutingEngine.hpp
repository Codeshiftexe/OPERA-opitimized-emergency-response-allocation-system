#pragma once


#include "Graph.hpp"

#include <limits>
#include <vector>

namespace opera {
//infinity, used to mark unreachable nodes.

constexpr double INF = std::numeric_limits<double>::infinity();

// The result of one Dijkstra run from a single source location.
// Holds two arrays, both indexed by node id:
//   dist_[v]    fastest travel time from the source to v (INF if unreachable)
//   parent_[v]  the node just before v on that fastest route (-1 if none)
// The parent array helps reconstruct the actual route, not just the travel time.

class ShortestPaths {
public:
    ShortestPaths(std::vector<double> dist, std::vector<int> parent, int source);

    int source() const { return source_; }

    // Fastest travel time from the source to target. INF if unreachable.
    // Throws std::out_of_range for an invalid id.
    double distanceTo(int target) const;

    bool reachable(int target) const;

    // The actual route: node ids from source to target, both included.
    //   pathTo(source) -> { source }   (one node: you are already there)
    //   unreachable    -> { }          (empty: there is no route)
    // Callers must handle the empty case - never assume a route exists.
    std::vector<int> pathTo(int target) const;

private:
    std::vector<double> dist_;
    std::vector<int> parent_;
    int source_;
};

ShortestPaths dijkstra(const Graph& g, int source);

}  // namespace opera
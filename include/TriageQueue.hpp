#pragma once

#include <cstddef>
#include <unordered_set>
#include <vector>

#include "Emergency.hpp"
#include "MinHeap.hpp"

namespace opera {

// Orders pending emergencies so the most urgent one is always served next.

class TriageQueue {
public:
    // Rejects nullptr, emergencies that are not Pending, and duplicates.
    bool push(Emergency* e);

    // Both return nullptr when empty.
    Emergency* peek() const;
    Emergency* pop();

    bool contains(int emergencyId) const;
    std::size_t size() const { return heap_.size(); }
    bool empty() const { return heap_.empty(); }

    // Every queued emergency in serve order, without changing the queue.
    std::vector<const Emergency*> snapshot() const;

private:
    struct ServesFirst {
        bool operator()(const Emergency* a, const Emergency* b) const;
    };

    MinHeap<Emergency*, ServesFirst> heap_;
    std::unordered_set<int> queuedIds_;
};

} 
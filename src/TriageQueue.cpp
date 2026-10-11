#include "TriageQueue.hpp"

#include <algorithm>

namespace opera {

bool TriageQueue::ServesFirst::operator()(const Emergency* a,const Emergency* b) const {
    // Compare at the later report time, so neither has a negative wait.
    const int t = std::max(a->reportedAt(), b->reportedAt());
    const double pa = a->priorityAt(t);
    const double pb = b->priorityAt(t);
    if (pa != pb) {
        return pa > pb;
    }
    if (a->reportedAt() != b->reportedAt()) {
        return a->reportedAt() < b->reportedAt();
    }
    return a->id() < b->id();
}

bool TriageQueue::push(Emergency* e) {
    if (e == nullptr) {
        return false;
    }
    if (e->status() != EmergencyStatus::Pending) {
        return false;
    }
    if (queuedIds_.count(e->id()) != 0) {
        return false;
    }
    heap_.push(e);
    queuedIds_.insert(e->id());
    return true;
}

Emergency* TriageQueue::peek() const {
    if (heap_.empty()) {
        return nullptr;
    }
    return heap_.top();
}

Emergency* TriageQueue::pop() {
    if (heap_.empty()) {
        return nullptr;
    }
    Emergency* e = heap_.pop();
    queuedIds_.erase(e->id());
    return e;
}

bool TriageQueue::contains(int emergencyId) const {
    return queuedIds_.count(emergencyId) != 0;
}

std::vector<const Emergency*> TriageQueue::snapshot() const {
    MinHeap<Emergency*, ServesFirst> copy = heap_;
    std::vector<const Emergency*> order;
    order.reserve(copy.size());
    while (!copy.empty()) {
        order.push_back(copy.pop());
    }
    return order;
}

}
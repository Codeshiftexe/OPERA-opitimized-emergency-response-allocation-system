#pragma once

// AutoDispatcher - optional automatic dispatching.
//
// When it is switched on, waiting emergencies are dispatched without anyone
// pressing "Dispatch next": run() keeps calling DispatchCoordinator::dispatchNext()
// while an emergency is waiting and an ambulance and a hospital bed are free.
//
// It adds no new decision rules. Every dispatch goes through dispatchNext(), so
// auto-dispatch follows exactly the same rules as a manual click:
//   - the triage order (priority with aging, then report time, then id),
//   - the nearest available ambulance and the nearest hospital with a bed (Dijkstra),
//   - check-first-then-commit, so nothing is ever double-booked.
//
// run() stops at the first refusal. The queue is served strictly in triage order,
// so if the most urgent case cannot be served (no ambulance or no bed), no
// lower-priority case is allowed to jump ahead of it.
//
// The Api calls run() after events that can make a dispatch possible:
// a new emergency, a delivered patient (ambulance free again), a discharged
// patient (bed free again), an ambulance brought back online, the clock moving,
// and auto-dispatch being switched on. It deliberately does NOT run after an
// undo, otherwise the undone case would be dispatched again immediately.

#include <vector>

#include "DispatchCoordinator.hpp"

namespace opera {

class AutoDispatcher {
public:
    // maxPerRun is only a safety limit; each successful dispatch removes one
    // emergency from the queue, so a run always ends on its own.
    explicit AutoDispatcher(DispatchCoordinator& coordinator, int maxPerRun = 256);

    bool enabled() const { return enabled_; }
    void setEnabled(bool on) { enabled_ = on; }

    // Dispatch everything that can be dispatched right now, most urgent first.
    // Returns the successful dispatches in the order they were made.
    // Does nothing (returns an empty list) while switched off.
    std::vector<DispatchResult> run();

    // Why the most recent run() stopped: NoPendingEmergency when the queue is
    // empty, NoAmbulanceAvailable / NoHospitalAvailable when the front case had
    // to wait.
    DispatchStatus lastStop() const { return lastStop_; }

    // Total number of automatic dispatches made since the program started.
    int totalDispatched() const { return total_; }

private:
    DispatchCoordinator& coord_;  // borrowed, owned by main()
    int maxPerRun_;
    bool enabled_ = false;
    DispatchStatus lastStop_ = DispatchStatus::NoPendingEmergency;
    int total_ = 0;
};

}  // namespace opera

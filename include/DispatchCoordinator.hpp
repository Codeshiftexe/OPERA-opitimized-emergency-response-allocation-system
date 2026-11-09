#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "BatchPlanner.hpp"
#include "Emergency.hpp"
#include "Registry.hpp"
#include "Stack.hpp"
#include "TriageQueue.hpp"

namespace opera {

enum class DispatchStatus {
    Dispatched,
    NoPendingEmergency,
    NoAmbulanceAvailable,
    NoHospitalAvailable
};

const char* toString(DispatchStatus s);

struct DispatchResult {
    DispatchStatus status = DispatchStatus::NoPendingEmergency;
    int emergencyId = -1;
    int ambulanceId = -1;
    int hospitalId = -1;
    std::vector<int> routeToScene;      
    std::vector<int> routeToHospital;   
    double minutesToScene = 0.0;
    double minutesToHospital = 0.0;
    bool meetsTarget = false;
};

// The outcome of dispatching several emergencies together.
struct BatchResult {
    std::vector<DispatchResult> results;   // one per case considered, most urgent first
    double cost = 0.0;                     // priority x minutes over the cases served
    double greedyCost = 0.0;               // the same, had they been dispatched one at a time
    int greedyAssigned = 0;
    long long explored = 0;                // backtracking search calls
};

class DispatchCoordinator {
public:
    explicit DispatchCoordinator(Registry registry);

    // Reported at the current time. Returns the new id, or -1 for a node
    // not on the map.
    int registerEmergency(EmergencyType type, int nodeId);

    // Serves the most urgent pending emergency. If it cannot be served,
    // nothing changes and the emergency stays queued.
    DispatchResult dispatchNext();

    // Serves the most urgent waiting emergencies together (as many as there are
    // free ambulances, at most BatchPlanner::kMaxCases), choosing which ambulance
    // goes where by backtracking. Hospitals are then given out most urgent first.
    // Cases that cannot be served stay queued, and nothing about them changes.
    BatchResult dispatchBatch();

    // Reverses the most recent dispatch that has not been completed yet.
    bool undoLastDispatch();
    bool undoLastDispatch(int& undoneEmergencyId);

    // Patient delivered: the emergency is resolved, the ambulance becomes
    // available at the hospital, and the bed stays occupied.
    bool completeDispatch(int emergencyId);

    // Patient leaves hospital: frees one bed.
    bool dischargePatient(int hospitalId);

    bool setAmbulanceOnline(int ambulanceId, bool online);

    bool advanceTime(int minutes);
    int now() const { return clock_; }

    const Registry& registry() const { return registry_; }
    const TriageQueue& queue() const { return queue_; }

    const Emergency* findEmergency(int id) const;
    std::vector<const Emergency*> emergencies() const;   // sorted by id
    std::vector<DispatchResult> activeDispatches() const;

private:
    struct DispatchRecord {
        int emergencyId;
        int ambulanceId;
        int hospitalId;
    };

    Emergency* emergencyById(int id);

    // Assigns the ambulance and bed, records the dispatch for undo, and builds
    // the result. Callers check everything first and remove e from the queue.
    DispatchResult commit(Emergency* e, const NearestResult& amb, const NearestResult& hosp);

    Registry registry_;
    std::unordered_map<int, std::unique_ptr<Emergency>> emergencies_;
    TriageQueue queue_;
    std::unordered_map<int, DispatchResult> active_;
    Stack<DispatchRecord> history_;
    int clock_ = 0;
    int nextEmergencyId_ = 1;
};

}  // namespace opera
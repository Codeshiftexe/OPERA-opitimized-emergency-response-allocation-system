#include "DispatchCoordinator.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace opera {

const char* toString(DispatchStatus s) {
    switch (s) {
        case DispatchStatus::Dispatched:           return "Dispatched";
        case DispatchStatus::NoPendingEmergency:   return "No pending emergency";
        case DispatchStatus::NoAmbulanceAvailable: return "No ambulance available";
        case DispatchStatus::NoHospitalAvailable:  return "No hospital available";
    }
    return "Unknown";
}

DispatchCoordinator::DispatchCoordinator(Registry registry)
    : registry_(std::move(registry)) {}

int DispatchCoordinator::registerEmergency(EmergencyType type, int nodeId) {
    if (!registry_.roads().hasNode(nodeId)) {
        return -1;
    }
    const int id = nextEmergencyId_++;
    std::unique_ptr<Emergency> e = makeEmergency(type, id, nodeId, clock_);
    Emergency* raw = e.get();
    emergencies_.emplace(id, std::move(e));
    queue_.push(raw);
    return id;
}

DispatchResult DispatchCoordinator::dispatchNext() {
    DispatchResult result;
    Emergency* e = queue_.peek();
    if (e == nullptr) {
        result.status = DispatchStatus::NoPendingEmergency;
        return result;
    }
    result.emergencyId = e->id();

    const NearestResult amb = registry_.nearestAvailableAmbulance(e->nodeId());
    if (!amb.found()) {
        result.status = DispatchStatus::NoAmbulanceAvailable;
        return result;
    }
    const NearestResult hosp = registry_.nearestHospitalWithBed(e->nodeId());
    if (!hosp.found()) {
        result.status = DispatchStatus::NoHospitalAvailable;
        return result;
    }

    
    Ambulance* ambulance = registry_.ambulance(amb.id);
    Hospital* hospital = registry_.hospital(hosp.id);
    queue_.pop();
    if (!ambulance->assignTo(e->id()) || !hospital->reserveBed() ||
        !e->assign(amb.id, hosp.id)) {
        throw std::logic_error("DispatchCoordinator::dispatchNext - checked resource refused");
    }
    history_.push(DispatchRecord{e->id(), amb.id, hosp.id});

    result.status = DispatchStatus::Dispatched;
    result.ambulanceId = amb.id;
    result.hospitalId = hosp.id;
    result.routeToScene = amb.route;
    result.routeToHospital = hosp.route;
    result.minutesToScene = amb.minutes;
    result.minutesToHospital = hosp.minutes;
    result.meetsTarget = e->meetsTarget(clock_, amb.minutes);
    return result;
}

bool DispatchCoordinator::undoLastDispatch() {
    while (!history_.empty()) {
        const DispatchRecord r = history_.pop();
        Emergency* e = emergencyById(r.emergencyId);
        // A completed dispatch can no longer be undone; discard its record
        // and look at the one below it.
        if (e == nullptr || e->status() != EmergencyStatus::Assigned) {
            continue;
        }
        Ambulance* a = registry_.ambulance(r.ambulanceId);
        Hospital* h = registry_.hospital(r.hospitalId);
        if (a == nullptr || h == nullptr || !e->unassign() || !a->release() ||
            !h->releaseBed() || !queue_.push(e)) {
            throw std::logic_error("DispatchCoordinator::undoLastDispatch - state mismatch");
        }
        return true;
    }
    return false;
}

bool DispatchCoordinator::completeDispatch(int emergencyId) {
    Emergency* e = emergencyById(emergencyId);
    if (e == nullptr || e->status() != EmergencyStatus::Assigned) {
        return false;
    }
    Ambulance* a = registry_.ambulance(e->assignedAmbulance());
    Hospital* h = registry_.hospital(e->assignedHospital());
    if (a == nullptr || h == nullptr || !e->resolve() || !a->release()) {
        throw std::logic_error("DispatchCoordinator::completeDispatch - state mismatch");
    }
    a->moveTo(h->nodeId());
    return true;
}

bool DispatchCoordinator::dischargePatient(int hospitalId) {
    Hospital* h = registry_.hospital(hospitalId);
    return h != nullptr && h->releaseBed();
}

bool DispatchCoordinator::setAmbulanceOnline(int ambulanceId, bool online) {
    Ambulance* a = registry_.ambulance(ambulanceId);
    if (a == nullptr) {
        return false;
    }
    return online ? a->goOnline() : a->goOffline();
}

bool DispatchCoordinator::advanceTime(int minutes) {
    if (minutes <= 0) {
        return false;
    }
    clock_ += minutes;
    return true;
}

const Emergency* DispatchCoordinator::findEmergency(int id) const {
    auto it = emergencies_.find(id);
    return it == emergencies_.end() ? nullptr : it->second.get();
}

Emergency* DispatchCoordinator::emergencyById(int id) {
    auto it = emergencies_.find(id);
    return it == emergencies_.end() ? nullptr : it->second.get();
}

std::vector<const Emergency*> DispatchCoordinator::emergencies() const {
    std::vector<const Emergency*> out;
    out.reserve(emergencies_.size());
    for (const auto& entry : emergencies_) {
        out.push_back(entry.second.get());
    }
    std::sort(out.begin(), out.end(),
              [](const Emergency* a, const Emergency* b) { return a->id() < b->id(); });
    return out;
}

} 
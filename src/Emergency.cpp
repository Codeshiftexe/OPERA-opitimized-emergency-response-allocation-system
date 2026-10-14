#include "Emergency.hpp"

namespace opera {

// No default case: -Wall then warns if a new status is added but not handled here.
const char* toString(EmergencyStatus s) {
    switch (s) {
        case EmergencyStatus::Pending:  return "Pending";
        case EmergencyStatus::Assigned: return "Assigned";
        case EmergencyStatus::Resolved: return "Resolved";
    }
    return "Unknown";
}

Emergency::Emergency(int id, int nodeId, int reportedAt)
    : id_(id),
      nodeId_(nodeId),
      reportedAt_(reportedAt),
      status_(EmergencyStatus::Pending),
      assignedAmbulance_(-1),
      assignedHospital_(-1) {}

double Emergency::priorityAt(int now) const {
    return basePriority() + kAgingPerMinute * waitingMinutes(now);
}

bool Emergency::meetsTarget(int now, double travelMinutes) const {
    return waitingMinutes(now) + travelMinutes <= targetResponseMinutes();
}

bool Emergency::assign(int ambulanceId, int hospitalId) {
    if (ambulanceId < 0 || hospitalId < 0) {
        return false;
    }
    if (status_ != EmergencyStatus::Pending) {
        return false;
    }
    status_            = EmergencyStatus::Assigned;
    assignedAmbulance_ = ambulanceId;
    assignedHospital_  = hospitalId;
    return true;
}

bool Emergency::resolve() {
    if (status_ != EmergencyStatus::Assigned) {
        return false;
    }
    status_ = EmergencyStatus::Resolved;
    return true;
}

bool Emergency::unassign() {
    if (status_ != EmergencyStatus::Assigned) {
        return false;
    }
    status_            = EmergencyStatus::Pending;
    assignedAmbulance_ = -1;
    assignedHospital_  = -1;
    return true;
}

std::unique_ptr<Emergency> makeEmergency(EmergencyType type, int id, int nodeId,
                                         int reportedAt) {
    switch (type) {
        case EmergencyType::Cardiac:
            return std::make_unique<CardiacEmergency>(id, nodeId, reportedAt);
        case EmergencyType::Trauma:
            return std::make_unique<TraumaEmergency>(id, nodeId, reportedAt);
        case EmergencyType::Transfer:
            return std::make_unique<TransferRequest>(id, nodeId, reportedAt);
    }
    return nullptr;
}

}  // namespace opera
#pragma once

#include <memory>
#include <string>

namespace opera {

enum class EmergencyStatus { Pending, Assigned, Resolved };

const char* toString(EmergencyStatus s);

class Emergency {
public:
    // Time is in simulation minutes, not wall-clock time, so tests are repeatable.
    Emergency(int id, int nodeId, int reportedAt);

    // Must be virtual: emergencies are deleted through Emergency pointers.
    virtual ~Emergency() = default;

    // Copying would slice a derived object down to its base part.
    Emergency(const Emergency&)            = delete;
    Emergency& operator=(const Emergency&) = delete;

    virtual std::string typeName() const              = 0;
    virtual int         basePriority() const          = 0;
    virtual int         targetResponseMinutes() const = 0;

    // Base priority plus a waiting bonus, so low-priority calls cannot starve.
    double priorityAt(int now) const;

    int  waitingMinutes(int now) const { return now - reportedAt_; }
    bool meetsTarget(int now, double travelMinutes) const;

    int             id() const { return id_; }
    int             nodeId() const { return nodeId_; }
    int             reportedAt() const { return reportedAt_; }
    EmergencyStatus status() const { return status_; }
    int             assignedAmbulance() const { return assignedAmbulance_; }
    int             assignedHospital() const { return assignedHospital_; }

    bool assign(int ambulanceId, int hospitalId);
    bool resolve();
    bool unassign(); 
    static constexpr double kAgingPerMinute = 0.5;

private:
    int             id_;
    int             nodeId_;
    int             reportedAt_;
    EmergencyStatus status_;
    int             assignedAmbulance_;
    int             assignedHospital_;
    // Invariant: Pending <=> both assigned ids are -1.
};

// Priorities and target times are project assumptions, not clinical standards.

class CardiacEmergency final : public Emergency {
public:
    using Emergency::Emergency;
    std::string typeName() const override { return "Cardiac"; }
    int         basePriority() const override { return 100; }
    int         targetResponseMinutes() const override { return 8; }
};

class TraumaEmergency final : public Emergency {
public:
    using Emergency::Emergency;
    std::string typeName() const override { return "Trauma"; }
    int         basePriority() const override { return 80; }
    int         targetResponseMinutes() const override { return 15; }
};

class TransferRequest final : public Emergency {
public:
    using Emergency::Emergency;
    std::string typeName() const override { return "Transfer"; }
    int         basePriority() const override { return 20; }
    int         targetResponseMinutes() const override { return 60; }
};

enum class EmergencyType { Cardiac, Trauma, Transfer };

std::unique_ptr<Emergency> makeEmergency(EmergencyType type, int id, int nodeId,
                                         int reportedAt);

}  // namespace opera
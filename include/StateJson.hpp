#pragma once

// Serialisation layer: turns engine objects into JSON for the browser.
// Nothing in the engine (domain, dispatch, routing) knows that JSON exists.

#include <string>
#include <vector>

#include "DispatchCoordinator.hpp"
#include "CityLoader.hpp"
#include "Json.hpp"

namespace opera {

struct LogEntry {
    int minute;        // simulation clock when it happened
    std::string user;  // who did it ("" if no one was signed in)
    std::string text;
};

// The whole state as one JSON object. The web page redraws entirely from it.
// `log` is written newest first.
void writeState(JsonWriter& w, const DispatchCoordinator& coordinator, const MapLayout& layout,
                const std::vector<LogEntry>& log);

void writeDispatchResult(JsonWriter& w, const DispatchResult& r);

// A dispatch can be undone while at least one emergency is Assigned.
// Why this is exact: dispatchNext pushes a record for every assignment;
// undoLastDispatch skips only records whose emergency is no longer Assigned.
// So the history holds an undoable record exactly when an Assigned emergency exists.
bool canUndo(const DispatchCoordinator& coordinator);

// "Cardiac" / "Trauma" / "Transfer" <-> EmergencyType.
bool parseEmergencyType(const std::string& name, EmergencyType& out);

std::string hospitalName(const MapLayout& layout, int hospitalId);

}  // namespace opera

#include "Registry.hpp"

#include <algorithm>
#include <utility>

#include "RoutingEngine.hpp"

namespace opera {

Registry::Registry(Graph roads) : roads_(std::move(roads)) {}

bool Registry::addHospital(const Hospital& h) {
    if (h.id() < 0 || !roads_.hasNode(h.nodeId()) || hospitals_.count(h.id()) != 0) {
        return false;
    }
    hospitals_.emplace(h.id(), h);
    return true;
}

bool Registry::addAmbulance(const Ambulance& a) {
    if (a.id() < 0 || !roads_.hasNode(a.nodeId()) || ambulances_.count(a.id()) != 0) {
        return false;
    }
    ambulances_.emplace(a.id(), a);
    return true;
}

Hospital* Registry::hospital(int id) {
    auto it = hospitals_.find(id);
    return it == hospitals_.end() ? nullptr : &it->second;
}

const Hospital* Registry::hospital(int id) const {
    auto it = hospitals_.find(id);
    return it == hospitals_.end() ? nullptr : &it->second;
}

Ambulance* Registry::ambulance(int id) {
    auto it = ambulances_.find(id);
    return it == ambulances_.end() ? nullptr : &it->second;
}

const Ambulance* Registry::ambulance(int id) const {
    auto it = ambulances_.find(id);
    return it == ambulances_.end() ? nullptr : &it->second;
}

std::vector<const Hospital*> Registry::hospitals() const {
    std::vector<const Hospital*> out;
    out.reserve(hospitals_.size());
    for (const auto& entry : hospitals_) {
        out.push_back(&entry.second);
    }
    std::sort(out.begin(), out.end(),
              [](const Hospital* a, const Hospital* b) { return a->id() < b->id(); });
    return out;
}

std::vector<const Ambulance*> Registry::ambulances() const {
    std::vector<const Ambulance*> out;
    out.reserve(ambulances_.size());
    for (const auto& entry : ambulances_) {
        out.push_back(&entry.second);
    }
    std::sort(out.begin(), out.end(),
              [](const Ambulance* a, const Ambulance* b) { return a->id() < b->id(); });
    return out;
}

NearestResult Registry::nearestAvailableAmbulance(int sceneNode) const {
    NearestResult best;
    if (!roads_.hasNode(sceneNode)) {
        return best;
    }

    for (const auto& entry : ambulances_) {
        const Ambulance& amb = entry.second;
        if (amb.status() != AmbulanceStatus::Available) {
            continue;
        }
        const ShortestPaths paths = dijkstra(roads_, amb.nodeId());
        if (!paths.reachable(sceneNode)) {
            continue;
        }
        const double minutes = paths.distanceTo(sceneNode);
        if (!best.found() || minutes < best.minutes ||
            (minutes == best.minutes && amb.id() < best.id)) {
            best.id = amb.id();
            best.minutes = minutes;
            best.route = paths.pathTo(sceneNode);
        }
    }
    return best;
}

NearestResult Registry::nearestHospitalWithBed(int fromNode) const {
    NearestResult best;
    if (!roads_.hasNode(fromNode)) {
        return best;
    }
    const ShortestPaths paths = dijkstra(roads_, fromNode);
    for (const auto& entry : hospitals_) {
        const Hospital& hosp = entry.second;
        if (!hosp.hasFreeBed() || !paths.reachable(hosp.nodeId())) {
            continue;
        }
        const double minutes = paths.distanceTo(hosp.nodeId());
        if (!best.found() || minutes < best.minutes ||
            (minutes == best.minutes && hosp.id() < best.id)) {
            best.id = hosp.id();
            best.minutes = minutes;
            best.route = paths.pathTo(hosp.nodeId());
        }
    }
    return best;
}

} 
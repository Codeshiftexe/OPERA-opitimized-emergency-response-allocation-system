#pragma once

#include <unordered_map>
#include <vector>

#include "Ambulance.hpp"
#include "Graph.hpp"
#include "Hospital.hpp"

namespace opera {

// The answer to a "nearest" query. id is -1 when nothing qualifies.
struct NearestResult {
    int id = -1;
    double minutes = 0.0;
    std::vector<int> route;
    bool found() const { return id != -1; }
};

class Registry {
public:
    explicit Registry(Graph roads);

    // Refused for a negative or duplicate id, or a node not on the map.
    bool addHospital(const Hospital& h);
    bool addAmbulance(const Ambulance& a);

    const Graph& roads() const { return roads_; }

    // nullptr if no such id.
    Hospital* hospital(int id);
    const Hospital* hospital(int id) const;
    Ambulance* ambulance(int id);
    const Ambulance* ambulance(int id) const;

    std::vector<const Hospital*> hospitals() const;
    std::vector<const Ambulance*> ambulances() const;

    NearestResult nearestAvailableAmbulance(int sceneNode) const;

    NearestResult nearestHospitalWithBed(int fromNode) const;

private:
    Graph roads_;
    std::unordered_map<int, Hospital> hospitals_;
    std::unordered_map<int, Ambulance> ambulances_;
};

}  
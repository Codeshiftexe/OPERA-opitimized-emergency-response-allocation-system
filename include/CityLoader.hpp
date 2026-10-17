#pragma once

// Reads the city data files and builds a validated Registry.
//
// File formats (lines starting with # are comments, blank lines are ignored):
//
//   city_map.txt     NODE <id> <x> <y> <name...>
//                    EDGE <node id> <node id> <minutes>
//       Node ids must be 0, 1, 2, ... in the order they appear, so the id in
//       the file is exactly the id the Graph hands out. Every EDGE is a
//       two-way road. x and y are map coordinates for the web interface only.
//
//   hospitals.txt    HOSPITAL <id> | <node id> | <name> | <total beds> [| <beds free>]
//       beds free defaults to total beds.
//
//   ambulances.txt   AMBULANCE <id> | <node id> [| available | offline]
//       status defaults to available. "busy" cannot be loaded, because a busy
//       ambulance needs an emergency, and emergencies only exist at runtime.
//
// Any problem stops the load with a LoadError whose message is
// "<file>:<line>: <what is wrong>", so the person editing the file can find it.

#include <istream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "Registry.hpp"
#include "Graph.hpp"

namespace opera {

// Screen position of a node. Display data only: the routing engine never reads it.
struct Point {
    double x = 0.0;
    double y = 0.0;
};

// Everything the map needs that the engine does not model: node coordinates
// and hospital display names. Kept beside the Registry, never inside it, so
// the domain classes stay unaware of how they are drawn.
struct MapLayout {
    std::vector<Point> nodePositions;                    // index = graph node id
    std::unordered_map<int, std::string> hospitalNames;  // hospital id -> name
};

struct CityData {
    Registry registry;
    MapLayout layout;
};

class LoadError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Reads all three files. Throws LoadError on the first problem found,
// including a file that cannot be opened.
CityData loadCity(const std::string& mapPath, const std::string& hospitalsPath,
                  const std::string& ambulancesPath);

// Stream versions, used by loadCity and by the tests. `source` is the name
// printed in error messages.
Graph loadGraph(std::istream& in, const std::string& source, MapLayout& layout);
void loadHospitals(std::istream& in, const std::string& source, Registry& registry,
                   MapLayout& layout);
void loadAmbulances(std::istream& in, const std::string& source, Registry& registry);

}  // namespace opera

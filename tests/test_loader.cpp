// Tests for the city loader: valid files build the right Graph and Registry,
// and every kind of bad line is refused with "<file>:<line>: ..." in the message.

#include <sstream>
#include <string>

#include "CityLoader.hpp"
#include "test_util.hpp"

// Set by CMake; when building by hand, run the test from the repository folder.
#ifndef OPERA_DATA_DIR
#define OPERA_DATA_DIR "data"
#endif

using namespace opera;

namespace {

const char* kMap =
    "# comment line\n"
    "NODE 0 10 20 North Gate\n"
    "\n"
    "NODE 1 30 40 Market\r\n"           // Windows line ending
    "node 2 50 60 Lake View\n"          // keywords are case-insensitive
    "EDGE 0 1 4\n"
    "EDGE 1 2 2.5\n";

Graph graphFrom(const std::string& text, MapLayout& layout) {
    std::istringstream in(text);
    return loadGraph(in, "map.txt", layout);
}

// Runs `load` and returns the LoadError message, or "" if nothing was thrown.
template <typename F>
std::string errorOf(F load) {
    try {
        load();
    } catch (const LoadError& e) {
        return e.what();
    }
    return "";
}

bool contains(const std::string& s, const std::string& part) {
    return s.find(part) != std::string::npos;
}

Registry smallCity(MapLayout& layout) { return Registry(graphFrom(kMap, layout)); }

void testValidMap() {
    MapLayout layout;
    const Graph g = graphFrom(kMap, layout);
    CHECK_EQ(g.size(), 3);
    CHECK_EQ(g.nodeName(0), std::string("North Gate"));
    CHECK_EQ(g.nodeName(2), std::string("Lake View"));
    CHECK_EQ(g.edgeCount(), 4);  // two two-way roads
    CHECK_EQ(layout.nodePositions.size(), std::size_t(3));
    CHECK_NEAR(layout.nodePositions[1].x, 30.0, 1e-9);
    CHECK_NEAR(layout.nodePositions[1].y, 40.0, 1e-9);
    CHECK_NEAR(g.neighbours(1)[1].weight, 2.5, 1e-9);
}

void testMapErrors() {
    MapLayout layout;
    auto bad = [&](const std::string& text) { return errorOf([&] { graphFrom(text, layout); }); };

    std::string e = bad("NODE 1 0 0 A\n");
    CHECK(contains(e, "map.txt:1:"));
    CHECK(contains(e, "expected 0"));

    e = bad("NODE 0 0 0 A\nNODE 1 5 5 A\n");
    CHECK(contains(e, "map.txt:2:"));
    CHECK(contains(e, "already exists"));

    e = bad("NODE 0 0 0 A\nNODE 1 5 5 B\nEDGE 0 7 3\n");
    CHECK(contains(e, "map.txt:3:"));
    CHECK(contains(e, "not defined"));

    e = bad("NODE 0 0 0 A\nNODE 1 5 5 B\nEDGE 0 1 -2\n");
    CHECK(contains(e, "map.txt:3:"));

    e = bad("NODE 0 0 0 A\nNODE 1 5 5 B\nEDGE 0 1 3 x2.5\n");
    CHECK(contains(e, "congestion"));

    e = bad("NODE 0 0 0 A\nEDGE 0 0 3\n");
    CHECK(contains(e, "different nodes"));

    e = bad("NODE 0 zero 0 A\n");
    CHECK(contains(e, "numbers"));

    e = bad("NODE 0 0 0 A\nROAD 0 1 3\n");
    CHECK(contains(e, "unknown record 'ROAD'"));

    e = bad("# nothing here\n");
    CHECK(contains(e, "no NODE records"));

    // Edges may come before the nodes they use.
    CHECK_EQ(bad("EDGE 0 1 3\nNODE 0 0 0 A\nNODE 1 5 5 B\n"), std::string(""));
}

void testHospitals() {
    MapLayout layout;
    Registry reg = smallCity(layout);
    std::istringstream in(
        "HOSPITAL 7 | 2 | Lake Clinic | 5 | 2\n"
        "HOSPITAL 8 | 0 | Gate Hospital | 3\n");
    loadHospitals(in, "h.txt", reg, layout);

    const Hospital* lake = reg.hospital(7);
    CHECK(lake != nullptr);
    CHECK_EQ(lake->nodeId(), 2);
    CHECK_EQ(lake->totalBeds(), 5);
    CHECK_EQ(lake->availableBeds(), 2);   // 3 beds taken at load
    CHECK_EQ(reg.hospital(8)->availableBeds(), 3);  // free defaults to total
    CHECK_EQ(layout.hospitalNames[7], std::string("Lake Clinic"));
}

void testHospitalErrors() {
    auto bad = [](const std::string& text) {
        MapLayout layout;
        Registry reg = smallCity(layout);
        std::istringstream in(text);
        return errorOf([&] { loadHospitals(in, "h.txt", reg, layout); });
    };
    CHECK(contains(bad("HOSPITAL 1 | 9 | X | 5\n"), "not on the map"));
    CHECK(contains(bad("HOSPITAL 1 | 0 | X | 5 | 6\n"), "beds free"));
    CHECK(contains(bad("HOSPITAL 1 | 0 | X | -1\n"), "total beds"));
    CHECK(contains(bad("HOSPITAL 1 | 0 |  | 5\n"), "name is empty"));
    CHECK(contains(bad("HOSPITAL 1 | 0 | X\n"), "HOSPITAL needs"));
    const std::string dup = bad("HOSPITAL 1 | 0 | X | 5\nHOSPITAL 1 | 1 | Y | 5\n");
    CHECK(contains(dup, "h.txt:2:"));
    CHECK(contains(dup, "used twice"));
}

void testAmbulances() {
    MapLayout layout;
    Registry reg = smallCity(layout);
    std::istringstream in(
        "AMBULANCE 1 | 0\n"
        "AMBULANCE 2 | 1 | offline\n"
        "AMBULANCE 3 | 2 | available\n");
    loadAmbulances(in, "a.txt", reg);
    CHECK_EQ(reg.ambulances().size(), std::size_t(3));
    CHECK(reg.ambulance(1)->isAvailable());
    CHECK(reg.ambulance(2)->status() == AmbulanceStatus::Offline);
    CHECK_EQ(reg.ambulance(3)->nodeId(), 2);
}

void testAmbulanceErrors() {
    auto bad = [](const std::string& text) {
        MapLayout layout;
        Registry reg = smallCity(layout);
        std::istringstream in(text);
        return errorOf([&] { loadAmbulances(in, "a.txt", reg); });
    };
    CHECK(contains(bad("AMBULANCE 1 | 0 | busy\n"), "status must be"));
    CHECK(contains(bad("AMBULANCE 1 | 5\n"), "not on the map"));
    CHECK(contains(bad("AMBULANCE x | 0\n"), "whole number"));
    CHECK(contains(bad("AMBULANCE 1 | 0\nAMBULANCE 1 | 1\n"), "a.txt:2:"));
}

void testMissingFile() {
    const std::string e = errorOf([] { loadCity("no/such/map.txt", "h", "a"); });
    CHECK(contains(e, "cannot open"));
}

void testShippedData() {
    // The real files the server starts with must always load.
    const std::string dir = OPERA_DATA_DIR;
    std::string error;
    try {
        CityData city = loadCity(dir + "/city_map.txt", dir + "/hospitals.txt",
                                 dir + "/ambulances.txt");
        CHECK_EQ(city.registry.roads().size(), 12);
        CHECK_EQ(city.registry.hospitals().size(), std::size_t(6));
        CHECK_EQ(city.registry.ambulances().size(), std::size_t(8));
        CHECK_EQ(city.layout.nodePositions.size(), std::size_t(12));
    } catch (const LoadError& e) {
        error = e.what();
    }
    CHECK_EQ(error, std::string(""));
}

}  // namespace

int main() {
    testValidMap();
    testMapErrors();
    testHospitals();
    testHospitalErrors();
    testAmbulances();
    testAmbulanceErrors();
    testMissingFile();
    testShippedData();
    return report("test_loader");
}

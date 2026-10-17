#include "CityLoader.hpp"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

namespace opera {

namespace {

[[noreturn]] void fail(const std::string& source, int line, const std::string& message) {
    throw LoadError(source + ":" + std::to_string(line) + ": " + message);
}

std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    const auto first = s.find_first_not_of(ws);
    if (first == std::string::npos) {
        return "";
    }
    const auto last = s.find_last_not_of(ws);
    return s.substr(first, last - first + 1);
}

std::vector<std::string> splitWhitespace(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    std::string token;
    while (in >> token) {
        out.push_back(token);
    }
    return out;
}

std::vector<std::string> splitBars(const std::string& s) {
    std::vector<std::string> out;
    std::string::size_type start = 0;
    while (true) {
        const auto bar = s.find('|', start);
        out.push_back(trim(s.substr(start, bar - start)));
        if (bar == std::string::npos) {
            return out;
        }
        start = bar + 1;
    }
}

// Whole-string integer parse. "12" ok; "12abc", "1.5", "" are rejected.
bool parseInt(const std::string& s, int& out) {
    if (s.empty()) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (errno != 0 || *end != '\0' || v < std::numeric_limits<int>::min() ||
        v > std::numeric_limits<int>::max()) {
        return false;
    }
    out = static_cast<int>(v);
    return true;
}

bool parseDouble(const std::string& s, double& out) {
    if (s.empty()) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const double v = std::strtod(s.c_str(), &end);
    if (errno != 0 || *end != '\0' || !std::isfinite(v)) {
        return false;
    }
    out = v;
    return true;
}

// One meaningful line: its number, its first word (upper-cased), and the rest.
struct Record {
    int line = 0;
    std::string keyword;
    std::string rest;
};

// Reads every non-blank, non-comment line. Handles Windows line endings.
std::vector<Record> readRecords(std::istream& in) {
    std::vector<Record> records;
    std::string raw;
    int lineNo = 0;
    while (std::getline(in, raw)) {
        ++lineNo;
        const std::string line = trim(raw);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const auto space = line.find_first_of(" \t");
        Record r;
        r.line = lineNo;
        r.keyword = line.substr(0, space);
        for (char& c : r.keyword) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        r.rest = space == std::string::npos ? "" : trim(line.substr(space));
        records.push_back(std::move(r));
    }
    return records;
}

std::ifstream openOrFail(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw LoadError(path + ": cannot open the file");
    }
    return in;
}

}  // namespace

Graph loadGraph(std::istream& in, const std::string& source, MapLayout& layout) {
    struct PendingEdge {
        int line;
        int a;
        int b;
        double minutes;
    };

    Graph graph;
    std::vector<PendingEdge> edges;
    layout.nodePositions.clear();

    for (const Record& r : readRecords(in)) {
        const std::vector<std::string> f = splitWhitespace(r.rest);

        if (r.keyword == "NODE") {
            if (f.size() < 4) {
                fail(source, r.line, "NODE needs: id x y name");
            }
            int id = 0;
            double x = 0.0;
            double y = 0.0;
            if (!parseInt(f[0], id)) {
                fail(source, r.line, "node id '" + f[0] + "' is not a whole number");
            }
            if (id != graph.size()) {
                fail(source, r.line, "node ids must be 0, 1, 2, ... in order; expected " +
                                         std::to_string(graph.size()) + " but found " +
                                         std::to_string(id));
            }
            if (!parseDouble(f[1], x) || !parseDouble(f[2], y)) {
                fail(source, r.line, "x and y must be numbers");
            }
            std::string name = f[3];
            for (std::size_t i = 4; i < f.size(); ++i) {
                name += " " + f[i];
            }
            // Graph::addNode silently returns the existing id for a repeated
            // name, which would break "file id == graph id". Refuse it here.
            if (graph.nodeId(name) != -1) {
                fail(source, r.line, "a node called '" + name + "' already exists");
            }
            graph.addNode(name);
            layout.nodePositions.push_back(Point{x, y});
        } else if (r.keyword == "EDGE") {
            if (f.size() == 4 && !f[3].empty() && (f[3][0] == 'x' || f[3][0] == 'X')) {
                fail(source, r.line,
                     "congestion multipliers like '" + f[3] +
                         "' are not supported: the engine uses static road weights");
            }
            if (f.size() != 3) {
                fail(source, r.line, "EDGE needs exactly: nodeA nodeB minutes");
            }
            PendingEdge e{r.line, 0, 0, 0.0};
            if (!parseInt(f[0], e.a) || !parseInt(f[1], e.b)) {
                fail(source, r.line, "EDGE node ids must be whole numbers");
            }
            if (!parseDouble(f[2], e.minutes) || e.minutes < 0.0) {
                fail(source, r.line, "minutes must be a number that is 0 or more");
            }
            if (e.a == e.b) {
                fail(source, r.line, "a road must join two different nodes");
            }
            edges.push_back(e);
        } else {
            fail(source, r.line, "unknown record '" + r.keyword + "' (expected NODE or EDGE)");
        }
    }

    if (graph.size() == 0) {
        throw LoadError(source + ": no NODE records found");
    }
    // Edges are checked after all nodes are read, so the file may list them in any order.
    for (const PendingEdge& e : edges) {
        if (!graph.hasNode(e.a) || !graph.hasNode(e.b)) {
            fail(source, e.line, "EDGE refers to a node that is not defined");
        }
        graph.addEdge(e.a, e.b, e.minutes, true);
    }
    return graph;
}

void loadHospitals(std::istream& in, const std::string& source, Registry& registry,
                   MapLayout& layout) {
    for (const Record& r : readRecords(in)) {
        if (r.keyword != "HOSPITAL") {
            fail(source, r.line, "unknown record '" + r.keyword + "' (expected HOSPITAL)");
        }
        const std::vector<std::string> f = splitBars(r.rest);
        if (f.size() != 4 && f.size() != 5) {
            fail(source, r.line, "HOSPITAL needs: id | node | name | total beds [| beds free]");
        }
        int id = 0;
        int node = 0;
        int total = 0;
        if (!parseInt(f[0], id) || id < 0) {
            fail(source, r.line, "hospital id must be a whole number, 0 or more");
        }
        if (!parseInt(f[1], node) || !registry.roads().hasNode(node)) {
            fail(source, r.line, "node '" + f[1] + "' is not on the map");
        }
        if (f[2].empty()) {
            fail(source, r.line, "hospital name is empty");
        }
        if (!parseInt(f[3], total) || total < 0) {
            fail(source, r.line, "total beds must be a whole number, 0 or more");
        }
        int freeBeds = total;
        if (f.size() == 5 && (!parseInt(f[4], freeBeds) || freeBeds < 0 || freeBeds > total)) {
            fail(source, r.line, "beds free must be a whole number from 0 to total beds");
        }
        if (registry.hospital(id) != nullptr) {
            fail(source, r.line, "hospital id " + std::to_string(id) + " is used twice");
        }

        registry.addHospital(Hospital(id, node, total));
        // Hospital starts with every bed free; occupy the ones the file says are taken.
        Hospital* h = registry.hospital(id);
        for (int taken = total - freeBeds; taken > 0; --taken) {
            h->reserveBed();
        }
        layout.hospitalNames[id] = f[2];
    }
}

void loadAmbulances(std::istream& in, const std::string& source, Registry& registry) {
    for (const Record& r : readRecords(in)) {
        if (r.keyword != "AMBULANCE") {
            fail(source, r.line, "unknown record '" + r.keyword + "' (expected AMBULANCE)");
        }
        const std::vector<std::string> f = splitBars(r.rest);
        if (f.size() != 2 && f.size() != 3) {
            fail(source, r.line, "AMBULANCE needs: id | node [| available or offline]");
        }
        int id = 0;
        int node = 0;
        if (!parseInt(f[0], id) || id < 0) {
            fail(source, r.line, "ambulance id must be a whole number, 0 or more");
        }
        if (!parseInt(f[1], node) || !registry.roads().hasNode(node)) {
            fail(source, r.line, "node '" + f[1] + "' is not on the map");
        }
        const std::string status = f.size() == 3 ? f[2] : "available";
        if (status != "available" && status != "offline") {
            fail(source, r.line, "status must be 'available' or 'offline', not '" + status + "'");
        }
        if (registry.ambulance(id) != nullptr) {
            fail(source, r.line, "ambulance id " + std::to_string(id) + " is used twice");
        }

        registry.addAmbulance(Ambulance(id, node));
        if (status == "offline") {
            registry.ambulance(id)->goOffline();
        }
    }
}

CityData loadCity(const std::string& mapPath, const std::string& hospitalsPath,
                  const std::string& ambulancesPath) {
    MapLayout layout;
    std::ifstream mapIn = openOrFail(mapPath);
    Registry registry(loadGraph(mapIn, mapPath, layout));

    std::ifstream hospIn = openOrFail(hospitalsPath);
    loadHospitals(hospIn, hospitalsPath, registry, layout);

    std::ifstream ambIn = openOrFail(ambulancesPath);
    loadAmbulances(ambIn, ambulancesPath, registry);

    return CityData{std::move(registry), std::move(layout)};
}

}  // namespace opera

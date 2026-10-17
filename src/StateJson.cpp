#include "StateJson.hpp"

#include <cctype>

#include "RoutingEngine.hpp"

namespace opera {

namespace {

std::string lower(const char* s) {
    std::string out(s);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

void writeIntArray(JsonWriter& w, const std::vector<int>& v) {
    w.beginArray();
    for (const int x : v) {
        w.value(x);
    }
    w.endArray();
}

void writeIdOrNull(JsonWriter& w, int id) {
    if (id < 0) {
        w.null();
    } else {
        w.value(id);
    }
}

bool hasEdge(const Graph& g, int from, int to, double weight) {
    for (const Edge& e : g.neighbours(from)) {
        if (e.to == to && e.weight == weight) {
            return true;
        }
    }
    return false;
}

void writeEmergencyTypes(JsonWriter& w) {
    // Read the numbers from the engine's own classes so the page can never
    // show a priority or target that differs from what the engine uses.
    w.key("emergencyTypes").beginArray();
    for (const EmergencyType t : {EmergencyType::Cardiac, EmergencyType::Trauma,
                                  EmergencyType::Transfer}) {
        const auto e = makeEmergency(t, 0, 0, 0);
        w.beginObject()
            .key("name").value(e->typeName())
            .key("basePriority").value(e->basePriority())
            .key("targetMinutes").value(e->targetResponseMinutes())
            .endObject();
    }
    w.endArray();
    w.key("agingPerMinute").value(Emergency::kAgingPerMinute);
}

void writeMap(JsonWriter& w, const Graph& roads, const MapLayout& layout) {
    w.key("nodes").beginArray();
    for (int id = 0; id < roads.size(); ++id) {
        const Point p = id < static_cast<int>(layout.nodePositions.size())
                            ? layout.nodePositions[id]
                            : Point{};
        w.beginObject()
            .key("id").value(id)
            .key("name").value(roads.nodeName(id))
            .key("x").value(p.x)
            .key("y").value(p.y)
            .endObject();
    }
    w.endArray();

    // The Graph stores a two-way road as two directed edges. Send it once.
    w.key("roads").beginArray();
    for (int u = 0; u < roads.size(); ++u) {
        for (const Edge& e : roads.neighbours(u)) {
            const bool twoWay = hasEdge(roads, e.to, u, e.weight);
            if (twoWay && u > e.to) {
                continue;  // already sent from the other end
            }
            w.beginObject()
                .key("a").value(u)
                .key("b").value(e.to)
                .key("minutes").value(e.weight)
                .key("oneWay").value(!twoWay)
                .endObject();
        }
    }
    w.endArray();
}

void writeEmergency(JsonWriter& w, const Emergency& e, const DispatchCoordinator& c) {
    const int now = c.now();
    w.beginObject()
        .key("id").value(e.id())
        .key("type").value(e.typeName())
        .key("node").value(e.nodeId())
        .key("status").value(lower(toString(e.status())))
        .key("reportedAt").value(e.reportedAt())
        .key("waitingMinutes").value(e.waitingMinutes(now))
        .key("basePriority").value(e.basePriority())
        .key("priority").value(e.priorityAt(now))
        .key("targetMinutes").value(e.targetResponseMinutes());
    w.key("ambulance");
    writeIdOrNull(w, e.assignedAmbulance());
    w.key("hospital");
    writeIdOrNull(w, e.assignedHospital());

    // Routes are recomputed rather than stored. This gives the same answer the
    // dispatch got, because road weights are static and a Busy ambulance does
    // not move until completeDispatch. If dynamic weights are ever added, the
    // DispatchResult must be stored instead.
    std::vector<int> toScene;
    std::vector<int> toHospital;
    double minutesToScene = 0.0;
    double minutesToHospital = 0.0;
    if (e.status() == EmergencyStatus::Assigned) {
        const Registry& reg = c.registry();
        const Ambulance* a = reg.ambulance(e.assignedAmbulance());
        const Hospital* h = reg.hospital(e.assignedHospital());
        if (a != nullptr && h != nullptr) {
            const ShortestPaths fromAmb = dijkstra(reg.roads(), a->nodeId());
            const ShortestPaths fromScene = dijkstra(reg.roads(), e.nodeId());
            toScene = fromAmb.pathTo(e.nodeId());
            toHospital = fromScene.pathTo(h->nodeId());
            minutesToScene = fromAmb.distanceTo(e.nodeId());
            minutesToHospital = fromScene.distanceTo(h->nodeId());
        }
    }
    w.key("routeToScene");
    writeIntArray(w, toScene);
    w.key("routeToHospital");
    writeIntArray(w, toHospital);
    w.key("minutesToScene").value(minutesToScene);
    w.key("minutesToHospital").value(minutesToHospital);
    w.endObject();
}

}  // namespace

bool canUndo(const DispatchCoordinator& coordinator) {
    for (const Emergency* e : coordinator.emergencies()) {
        if (e->status() == EmergencyStatus::Assigned) {
            return true;
        }
    }
    return false;
}

bool parseEmergencyType(const std::string& name, EmergencyType& out) {
    if (name == "Cardiac")  { out = EmergencyType::Cardiac;  return true; }
    if (name == "Trauma")   { out = EmergencyType::Trauma;   return true; }
    if (name == "Transfer") { out = EmergencyType::Transfer; return true; }
    return false;
}

std::string hospitalName(const MapLayout& layout, int hospitalId) {
    const auto it = layout.hospitalNames.find(hospitalId);
    return it != layout.hospitalNames.end() ? it->second
                                            : "Hospital " + std::to_string(hospitalId);
}

void writeState(JsonWriter& w, const DispatchCoordinator& c, const MapLayout& layout,
                const std::vector<LogEntry>& log) {
    const Registry& reg = c.registry();

    w.beginObject();
    w.key("clock").value(c.now());
    w.key("canUndo").value(canUndo(c));
    writeEmergencyTypes(w);
    writeMap(w, reg.roads(), layout);

    w.key("hospitals").beginArray();
    for (const Hospital* h : reg.hospitals()) {
        w.beginObject()
            .key("id").value(h->id())
            .key("node").value(h->nodeId())
            .key("name").value(hospitalName(layout, h->id()))
            .key("totalBeds").value(h->totalBeds())
            .key("availableBeds").value(h->availableBeds())
            .endObject();
    }
    w.endArray();

    w.key("ambulances").beginArray();
    for (const Ambulance* a : reg.ambulances()) {
        w.beginObject()
            .key("id").value(a->id())
            .key("node").value(a->nodeId())
            .key("status").value(lower(toString(a->status())));
        w.key("emergency");
        writeIdOrNull(w, a->assignedEmergency());
        w.endObject();
    }
    w.endArray();

    w.key("emergencies").beginArray();
    for (const Emergency* e : c.emergencies()) {
        writeEmergency(w, *e, c);
    }
    w.endArray();

    // Serve order straight from the TriageQueue (heap order, aging included).
    w.key("queue").beginArray();
    for (const Emergency* e : c.queue().snapshot()) {
        w.value(e->id());
    }
    w.endArray();

    w.key("log").beginArray();
    for (auto it = log.rbegin(); it != log.rend(); ++it) {
        w.beginObject().key("minute").value(it->minute).key("user").value(it->user).key("text").value(it->text).endObject();
    }
    w.endArray();

    w.endObject();
}

void writeDispatchResult(JsonWriter& w, const DispatchResult& r) {
    w.beginObject()
        .key("status").value(toString(r.status));
    w.key("emergency");
    writeIdOrNull(w, r.emergencyId);
    w.key("ambulance");
    writeIdOrNull(w, r.ambulanceId);
    w.key("hospital");
    writeIdOrNull(w, r.hospitalId);
    w.key("routeToScene");
    writeIntArray(w, r.routeToScene);
    w.key("routeToHospital");
    writeIntArray(w, r.routeToHospital);
    w.key("minutesToScene").value(r.minutesToScene)
        .key("minutesToHospital").value(r.minutesToHospital)
        .key("meetsTarget").value(r.meetsTarget)
        .endObject();
}

}  // namespace opera

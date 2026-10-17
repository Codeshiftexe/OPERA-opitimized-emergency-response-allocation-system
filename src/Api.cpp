#include "Api.hpp"

#include <cctype>
#include <cstdio>
#include <exception>
#include <unordered_map>
#include <utility>

namespace opera {

namespace {

constexpr std::size_t kMaxLogEntries = 100;
constexpr int kMaxClockStep = 24 * 60;  // one request may move the clock at most a day

std::string ambulanceLabel(int id) { return "Ambulance " + std::to_string(id); }

}  // namespace

Api::Api(DispatchCoordinator& coordinator, MapLayout layout, Auth& auth)
    : coord_(coordinator), layout_(std::move(layout)), auth_(auth) {}

std::string Api::nodeName(int id) const {
    const Graph& g = coord_.registry().roads();
    return g.hasNode(id) ? g.nodeName(id) : "node " + std::to_string(id);
}

void Api::record(const std::string& text) {
    log_.push_back(LogEntry{coord_.now(), actor_, text});
    if (log_.size() > kMaxLogEntries) {
        log_.erase(log_.begin());
    }
}

namespace {

// "Bearer abc123" -> "abc123"; anything else -> "".
std::string bearerToken(const std::string& header) {
    const std::string prefix = "bearer ";
    if (header.size() <= prefix.size()) return "";
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(header[i])) != prefix[i]) return "";
    }
    std::string token = header.substr(prefix.size());
    while (!token.empty() && (token.back() == ' ' || token.back() == '\r')) token.pop_back();
    return token;
}

}  // namespace

ApiResponse Api::handle(const std::string& method, const std::string& path,
                        const std::string& body, const std::string& authorization) {
    using Handler = Outcome (Api::*)(const JsonObject&);
    static const std::unordered_map<std::string, Handler> postRoutes = {
        {"/api/emergencies", &Api::registerEmergency},
        {"/api/emergencies/complete", &Api::complete},
        {"/api/hospitals/discharge", &Api::discharge},
        {"/api/ambulances/status", &Api::setAmbulanceStatus},
        {"/api/clock/advance", &Api::advanceClock},
    };

    try {
        // ---- open routes -------------------------------------------------
        if (path == "/api/ping") {
            if (method != "GET") return reply({405, false, "Use GET for /api/ping.", false, {}}, nullptr);
            return ApiResponse{200, R"({"ok":true,"opera":true,"message":"OPERA server"})"};
        }

        JsonObject in;
        std::string error;
        if (method == "POST" && !body.empty() && !JsonObject::parse(body, in, error)) {
            return reply({400, false, "The request body is not valid JSON: " + error, false, {}}, nullptr);
        }

        if (path == "/api/login") {
            if (method != "POST") return reply({405, false, "Use POST for /api/login.", false, {}}, nullptr);
            return login(in);
        }

        // ---- everything else needs a signed-in user ------------------------
        const std::string token = bearerToken(authorization);
        const User* user = auth_.find(token);
        if (user == nullptr) {
            return reply({401, false, "Please sign in.", false, {}}, nullptr);
        }
        actor_ = user->name;

        if (path == "/api/logout") {
            if (method != "POST") return reply({405, false, "Use POST for /api/logout.", false, {}}, nullptr);
            record("Signed out.");
            auth_.logout(token);  // `user` points into the session table: do not use it after this
            return reply({200, true, "Signed out.", false, {}}, nullptr);
        }
        if (path == "/api/state" || path == "/api/session") {
            if (method != "GET") return reply({405, false, "Use GET for " + path + ".", false, {}}, user);
            return reply({200, true, "", false, {}}, user);
        }

        const bool isDispatch = path == "/api/dispatch";
        const bool isUndo = path == "/api/undo";
        const auto route = postRoutes.find(path);
        if (!isDispatch && !isUndo && route == postRoutes.end()) {
            return reply({404, false, "Unknown route " + path + ".", false, {}}, user);
        }
        if (method != "POST") {
            return reply({405, false, "Use POST for " + path + ".", false, {}}, user);
        }
        if (path == "/api/ambulances/status" && user->role != "admin") {
            return reply({403, false, "Only an admin can take ambulances offline or bring them online.",
                          false, {}}, user);
        }

        if (isDispatch) return reply(dispatchNext(), user);
        if (isUndo) return reply(undo(), user);
        return reply((this->*(route->second))(in), user);
    } catch (const std::exception& ex) {
        // The coordinator throws std::logic_error only if its own invariants
        // break. That is a bug, not a user mistake: report it, keep serving.
        return reply({500, false, std::string("Internal engine error: ") + ex.what(), false, {}}, nullptr);
    }
}

ApiResponse Api::login(const JsonObject& in) {
    std::string name;
    std::string password;
    if (!in.getString("name", name) || !in.getString("password", password) || name.empty()) {
        return reply({400, false, "Enter your name and password.", false, {}}, nullptr);
    }
    std::string token;
    User user;
    int secondsLeft = 0;
    switch (auth_.login(name, password, token, user, secondsLeft)) {
        case Auth::LoginResult::LockedOut:
            return reply({429, false, "Too many failed attempts. Try again in " +
                                          std::to_string(secondsLeft) + " s.", false, {}}, nullptr);
        case Auth::LoginResult::BadCredentials:
            return reply({401, false, "The name or password is incorrect.", false, {}}, nullptr);
        case Auth::LoginResult::Ok:
            break;
    }
    actor_ = user.name;
    record("Signed in as " + user.role + ".");
    return reply({200, true, "Signed in as " + user.name + ".", false, {}}, auth_.find(token), token);
}

Api::Outcome Api::registerEmergency(const JsonObject& in) {
    std::string typeText;
    int node = 0;
    EmergencyType type{};
    if (!in.getString("type", typeText) || !parseEmergencyType(typeText, type)) {
        return {400, false, "type must be \"Cardiac\", \"Trauma\" or \"Transfer\".", false, {}};
    }
    if (!in.getInt("node", node)) {
        return {400, false, "node must be a whole number.", false, {}};
    }
    const int id = coord_.registerEmergency(type, node);
    if (id < 0) {
        return {400, false, "Node " + std::to_string(node) + " is not on the map.", false, {}};
    }
    const std::string msg = "Registered #" + std::to_string(id) + " " + typeText + " at " +
                            nodeName(node) + ".";
    record(msg);
    return {200, true, msg, false, {}};
}

Api::Outcome Api::dispatchNext() {
    const DispatchResult r = coord_.dispatchNext();
    Outcome o{409, false, "", true, r};
    const std::string em = "#" + std::to_string(r.emergencyId);
    const Emergency* e = coord_.findEmergency(r.emergencyId);
    const std::string where = e != nullptr ? nodeName(e->nodeId()) : "";

    switch (r.status) {
        case DispatchStatus::Dispatched: {
            const int target = e != nullptr ? e->targetResponseMinutes() : 0;
            o.status = 200;
            o.ok = true;
            char minutes[64];
            std::snprintf(minutes, sizeof minutes, "%.1f min to the scene, then %.1f min to ",
                          r.minutesToScene, r.minutesToHospital);
            o.message = ambulanceLabel(r.ambulanceId) + " dispatched to " + em + " at " + where +
                        ": " + minutes + hospitalName(layout_, r.hospitalId) + ". Target " +
                        std::to_string(target) + " min " + (r.meetsTarget ? "met." : "missed.");
            break;
        }
        case DispatchStatus::NoPendingEmergency:
            o.message = "No emergency is waiting.";
            break;
        case DispatchStatus::NoAmbulanceAvailable:
            o.message = em + " cannot be dispatched: no available ambulance can reach " + where +
                        ". It stays at the front of the queue.";
            break;
        case DispatchStatus::NoHospitalAvailable:
            o.message = em + " cannot be dispatched: no hospital with a free bed can be reached from " +
                        where + ". It stays at the front of the queue.";
            break;
    }
    record(o.message);
    return o;
}

Api::Outcome Api::undo() {
    // undoLastDispatch only says yes/no, so note who is Assigned before and
    // see who went back to Pending afterwards.
    struct Assignment { int ambulance; int hospital; };
    std::unordered_map<int, Assignment> before;
    for (const Emergency* e : coord_.emergencies()) {
        if (e->status() == EmergencyStatus::Assigned) {
            before[e->id()] = {e->assignedAmbulance(), e->assignedHospital()};
        }
    }
    if (!coord_.undoLastDispatch()) {
        return {409, false, "Nothing to undo: no dispatch is still in progress.", false, {}};
    }
    std::string msg = "Undid the last dispatch.";
    for (const auto& entry : before) {
        const Emergency* e = coord_.findEmergency(entry.first);
        if (e != nullptr && e->status() == EmergencyStatus::Pending) {
            msg = "Undid the dispatch of #" + std::to_string(e->id()) + ": " +
                  ambulanceLabel(entry.second.ambulance) + " and a bed at " +
                  hospitalName(layout_, entry.second.hospital) +
                  " are free again, and #" + std::to_string(e->id()) + " is back in the queue.";
            break;
        }
    }
    record(msg);
    return {200, true, msg, false, {}};
}

Api::Outcome Api::complete(const JsonObject& in) {
    int id = 0;
    if (!in.getInt("id", id)) return {400, false, "id must be a whole number.", false, {}};
    const Emergency* e = coord_.findEmergency(id);
    if (e == nullptr) return {400, false, "There is no emergency #" + std::to_string(id) + ".", false, {}};

    const int amb = e->assignedAmbulance();
    const int hosp = e->assignedHospital();
    if (!coord_.completeDispatch(id)) {
        return {409, false, "#" + std::to_string(id) + " is not assigned to an ambulance (it is " +
                                toString(e->status()) + ").", false, {}};
    }
    const std::string msg = "#" + std::to_string(id) + " delivered to " +
                            hospitalName(layout_, hosp) + " by " + ambulanceLabel(amb) +
                            ". The ambulance is available again at the hospital; the bed stays "
                            "occupied until the patient is discharged.";
    record(msg);
    return {200, true, msg, false, {}};
}

Api::Outcome Api::discharge(const JsonObject& in) {
    int id = 0;
    if (!in.getInt("id", id)) return {400, false, "id must be a whole number.", false, {}};
    const Hospital* h = coord_.registry().hospital(id);
    if (h == nullptr) return {400, false, "There is no hospital " + std::to_string(id) + ".", false, {}};

    const std::string name = hospitalName(layout_, id);

    // Hospital counts a bed reserved for a patient still in an ambulance the
    // same as a bed with an admitted patient. If we let dischargePatient free
    // a reserved bed, a later undo of that dispatch cannot release it and
    // DispatchCoordinator::undoLastDispatch throws "state mismatch".
    // So only beds of patients who have actually arrived may be freed.
    int inbound = 0;
    for (const Emergency* e : coord_.emergencies()) {
        if (e->status() == EmergencyStatus::Assigned && e->assignedHospital() == id) {
            ++inbound;
        }
    }
    const int admitted = (h->totalBeds() - h->availableBeds()) - inbound;
    if (admitted <= 0) {
        return {409, false, name + " has no admitted patient to discharge" +
                                (inbound > 0 ? std::string(" (its occupied beds are reserved for "
                                                           "patients still on the way)")
                                             : std::string("")) + ".", false, {}};
    }
    if (!coord_.dischargePatient(id)) {
        return {409, false, name + " has no occupied beds to free.", false, {}};
    }
    const std::string msg = "One patient discharged from " + name + ": " +
                            std::to_string(h->availableBeds()) + " of " +
                            std::to_string(h->totalBeds()) + " beds free.";
    record(msg);
    return {200, true, msg, false, {}};
}

Api::Outcome Api::setAmbulanceStatus(const JsonObject& in) {
    int id = 0;
    bool online = false;
    if (!in.getInt("id", id)) return {400, false, "id must be a whole number.", false, {}};
    if (!in.getBool("online", online)) return {400, false, "online must be true or false.", false, {}};
    const Ambulance* a = coord_.registry().ambulance(id);
    if (a == nullptr) return {400, false, "There is no ambulance " + std::to_string(id) + ".", false, {}};

    const std::string label = ambulanceLabel(id);
    if (!coord_.setAmbulanceOnline(id, online)) {
        std::string why;
        if (a->status() == AmbulanceStatus::Busy) {
            why = label + " is on call #" + std::to_string(a->assignedEmergency()) +
                  " and cannot go offline until the patient is delivered.";
        } else {
            why = label + " is already " + (online ? "online." : "offline.");
        }
        return {409, false, why, false, {}};
    }
    const std::string msg = label + (online ? " is back online at " : " taken offline at ") +
                            nodeName(a->nodeId()) + ".";
    record(msg);
    return {200, true, msg, false, {}};
}

Api::Outcome Api::advanceClock(const JsonObject& in) {
    int minutes = 0;
    if (!in.getInt("minutes", minutes) || minutes < 1 || minutes > kMaxClockStep) {
        return {400, false, "minutes must be a whole number from 1 to " +
                                std::to_string(kMaxClockStep) + ".", false, {}};
    }
    coord_.advanceTime(minutes);
    const std::string msg = "Clock advanced by " + std::to_string(minutes) + " min to T+" +
                            std::to_string(coord_.now()) + ".";
    record(msg);
    return {200, true, msg, false, {}};
}

ApiResponse Api::reply(const Outcome& o, const User* user, const std::string& token) const {
    JsonWriter w;
    w.beginObject().key("ok").value(o.ok).key("message").value(o.message).key("result");
    if (o.hasResult) {
        writeDispatchResult(w, o.result);
    } else {
        w.null();
    }
    if (!token.empty()) {
        w.key("token").value(token);
    }
    w.key("user");
    if (user != nullptr) {
        w.beginObject().key("name").value(user->name).key("role").value(user->role).endObject();
    } else {
        w.null();
    }
    w.key("state");
    if (user != nullptr) {
        writeState(w, coord_, layout_, log_);
    } else {
        w.null();  // never show the city to someone who is not signed in
    }
    w.endObject();
    return ApiResponse{o.status, w.str()};
}

}  // namespace opera

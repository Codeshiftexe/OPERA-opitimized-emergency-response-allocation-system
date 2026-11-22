#include "LiveTracker.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace opera {

namespace {

// Travel time of the road a -> b as stored in the graph (cheapest if several).
double roadMinutes(const Graph& g, int a, int b) {
    double best = std::numeric_limits<double>::infinity();
    if (g.hasNode(a)) {
        for (const Edge& e : g.neighbours(a)) {
            if (e.to == b) best = std::min(best, e.weight);
        }
    }
    return std::isinf(best) ? 0.0 : best;
}

// Map matching weights (see LiveTracker.hpp).
constexpr double kPenaltyMetresPerMinute = 150.0;  // per road minute of an implausible jump
constexpr double kMaxSpeedFactor = 3.0;            // an ambulance may beat the road time 3x
constexpr double kForwardSlackMinutes = 1.0;
constexpr double kNearestMetresPerMinute = 2.0;    // tie-break: prefer the visit nearest to the last position

}  // namespace

const char* toString(TrackingMode mode) {
    return mode == TrackingMode::Gps ? "gps" : mode == TrackingMode::RealTime ? "realtime" : "simulated";
}

bool parseTrackingMode(const std::string& text, TrackingMode& out) {
    if (text == "gps") { out = TrackingMode::Gps; return true; }
    if (text == "simulated") { out = TrackingMode::Simulated; return true; }
    if (text == "realtime") { out = TrackingMode::RealTime; return true; }
    return false;
}

std::string sourceLabel(const LivePosition& p) {
    if (p.estimated) return p.realTime ? "real-time estimate" : "simulated";
    if (p.source == FixSource::Gps) return "gps";
    if (p.source == FixSource::Junction) return "junction report";
    return "waiting for gps";
}

LiveTracker::LiveTracker(double secondsPerRoadMinute, TrackingMode mode)
    : secondsPerMinute_(secondsPerRoadMinute > 0.0 ? secondsPerRoadMinute : 1.0), mode_(mode) {}

void LiveTracker::setNodeGeo(std::vector<GeoPoint> nodeGeo) {
    nodeGeo_ = std::move(nodeGeo);
    origin_ = GeoPoint{};
    for (const GeoPoint& g : nodeGeo_) {
        if (g.known) { origin_ = g; break; }
    }
}

bool LiveTracker::hasGeo() const { return origin_.known; }

LiveTracker::Trip LiveTracker::makeTrip(const DispatchResult& d, const Graph& roads, double now) const {
    Trip t;
    t.emergencyId = d.emergencyId;
    t.ambulanceId = d.ambulanceId;
    t.chain = d.routeToScene;
    if (t.chain.empty()) t.chain = d.routeToHospital.empty() ? std::vector<int>{} : std::vector<int>{d.routeToHospital.front()};
    // the hospital leg starts at the scene, which already ends the first leg
    for (std::size_t i = 1; i < d.routeToHospital.size(); ++i) t.chain.push_back(d.routeToHospital[i]);
    t.cum.assign(t.chain.size(), 0.0);
    for (std::size_t i = 1; i < t.chain.size(); ++i) {
        t.cum[i] = t.cum[i - 1] + roadMinutes(roads, t.chain[i - 1], t.chain[i]);
    }
    t.total = t.cum.empty() ? 0.0 : t.cum.back();
    const std::size_t sceneIndex = d.routeToScene.empty() ? 0 : d.routeToScene.size() - 1;
    t.toScene = sceneIndex < t.cum.size() ? t.cum[sceneIndex] : 0.0;
    t.anchorTime = now;
    t.fixTime = now;
    t.matchTime = now;

    t.geoReady = origin_.known && !t.chain.empty();
    for (int node : t.chain) {
        const bool ok = node >= 0 && static_cast<std::size_t>(node) < nodeGeo_.size() &&
                        nodeGeo_[static_cast<std::size_t>(node)].known;
        if (!ok) { t.geoReady = false; break; }
        const GeoPoint& g = nodeGeo_[static_cast<std::size_t>(node)];
        t.pts.push_back(toLocal(origin_, g.lat, g.lon));
    }
    if (!t.geoReady) t.pts.clear();
    return t;
}

void LiveTracker::sync(const std::vector<DispatchResult>& active, const Graph& roads, double now) {
    std::unordered_set<int> live;
    for (const DispatchResult& d : active) {
        live.insert(d.emergencyId);
        auto it = trips_.find(d.emergencyId);
        // a new dispatch, or the same emergency dispatched again after an undo
        if (it == trips_.end() || it->second.ambulanceId != d.ambulanceId) {
            trips_[d.emergencyId] = makeTrip(d, roads, now);
        }
    }
    for (auto it = trips_.begin(); it != trips_.end();) {
        if (live.count(it->first) == 0) it = trips_.erase(it);  // delivered or undone
        else ++it;
    }
}

double LiveTracker::progressAt(const Trip& t, double now) const {
    if (mode_ == TrackingMode::Gps) return std::min(t.total, t.anchorProgress);  // only fixes move it
    const double perMinute = mode_ == TrackingMode::RealTime ? kRealSecondsPerRoadMinute : secondsPerMinute_;
    const double moved = std::max(0.0, now - t.anchorTime) / perMinute;
    return std::min(t.total, t.anchorProgress + moved);
}

LivePosition LiveTracker::describe(const Trip& t, double now) const {
    LivePosition p;
    p.emergencyId = t.emergencyId;
    p.ambulanceId = t.ambulanceId;
    p.progress = progressAt(t, now);
    p.toSceneMinutes = t.toScene;
    p.totalMinutes = t.total;
    p.phase = p.progress >= t.total ? 2 : (p.progress < t.toScene ? 0 : 1);
    p.source = t.source;
    p.reported = t.source != FixSource::None;
    p.reports = t.reports;
    // Simulated / RealTime: estimated unless it sits exactly on a fix
    p.realTime = mode_ == TrackingMode::RealTime;
    p.estimated = mode_ != TrackingMode::Gps &&
                  (t.source == FixSource::None || p.progress > t.anchorProgress + 1e-9);
    p.secondsSinceFix = std::max(0.0, now - t.fixTime);
    p.hasGps = t.gpsFixes > 0;
    p.gpsFixes = t.gpsFixes;
    if (p.hasGps) {
        p.lat = t.lastGps.lat;
        p.lon = t.lastGps.lon;
        p.accuracyMetres = t.lastGps.accuracyMetres;
        p.speedMps = t.lastGps.speedMps;
        p.offRouteMetres = t.offRouteMetres;
        p.offRoute = t.offRoute;
    }
    return p;
}

LivePosition LiveTracker::position(int emergencyId, double now) const {
    const auto it = trips_.find(emergencyId);
    return it == trips_.end() ? LivePosition{} : describe(it->second, now);
}

std::vector<LivePosition> LiveTracker::all(double now) const {
    std::vector<LivePosition> out;
    out.reserve(trips_.size());
    for (const auto& kv : trips_) out.push_back(describe(kv.second, now));
    std::sort(out.begin(), out.end(),
              [](const LivePosition& a, const LivePosition& b) { return a.emergencyId < b.emergencyId; });
    return out;
}

bool LiveTracker::reportNode(int emergencyId, int nodeId, double now, std::string& error) {
    const auto it = trips_.find(emergencyId);
    if (it == trips_.end()) {
        error = "#" + std::to_string(emergencyId) + " is not on its way (no live trip).";
        return false;
    }
    Trip& t = it->second;
    // a route may pass a junction twice: take the visit closest to where we think the ambulance is
    const double current = progressAt(t, now);
    int best = -1;
    for (std::size_t i = 0; i < t.chain.size(); ++i) {
        if (t.chain[i] != nodeId) continue;
        if (best < 0 || std::fabs(t.cum[i] - current) < std::fabs(t.cum[static_cast<std::size_t>(best)] - current)) {
            best = static_cast<int>(i);
        }
    }
    if (best < 0) {
        error = "Node " + std::to_string(nodeId) + " is not on the route of #" + std::to_string(emergencyId) + ".";
        return false;
    }
    t.anchorProgress = t.cum[static_cast<std::size_t>(best)];
    t.anchorTime = now;
    t.fixTime = now;
    t.matchTime = now;
    t.source = FixSource::Junction;
    ++t.reports;
    return true;
}

bool LiveTracker::reportGps(int emergencyId, const GpsFix& fix, double now, std::string& error) {
    const auto it = trips_.find(emergencyId);
    if (it == trips_.end()) {
        error = "#" + std::to_string(emergencyId) + " is not on its way (no live trip).";
        return false;
    }
    if (!validLatLon(fix.lat, fix.lon)) {
        error = "lat must be -90..90 and lon -180..180.";
        return false;
    }
    if (!(fix.accuracyMetres >= 0.0) || fix.accuracyMetres > kMaxAccuracyMetres) {
        error = "GPS fix too inaccurate to place on a road (accuracy must be 0.." +
                std::to_string(static_cast<int>(kMaxAccuracyMetres)) + " m). Move outdoors and wait for a better fix.";
        return false;
    }
    Trip& t = it->second;
    if (!t.geoReady) {
        error = "The route of #" + std::to_string(emergencyId) +
                " has junctions without real coordinates. Add GEO lines (or a GEOREF line) to data/city_map.txt.";
        return false;
    }

    const LocalPoint q = toLocal(origin_, fix.lat, fix.lon);
    const double current = progressAt(t, now);
    const double allowedForward =
        std::max(0.0, now - t.matchTime) / 60.0 * kMaxSpeedFactor + kForwardSlackMinutes;

    double bestScore = std::numeric_limits<double>::infinity();
    double bestDistance = 0.0;
    double bestProgress = current;
    auto consider = [&](double distance, double progress) {
        const double back = std::max(0.0, current - progress);
        const double ahead = std::max(0.0, progress - current - allowedForward);
        const double score = distance + kPenaltyMetresPerMinute * (back + ahead) +
                             kNearestMetresPerMinute * std::fabs(progress - current);
        if (score < bestScore) {
            bestScore = score;
            bestDistance = distance;
            bestProgress = progress;
        }
    };
    if (t.pts.size() == 1) {
        consider(distanceMetres(q, t.pts[0]), 0.0);
    }
    for (std::size_t i = 0; i + 1 < t.pts.size(); ++i) {
        const LocalPoint& a = t.pts[i];
        const LocalPoint& b = t.pts[i + 1];
        const double dx = b.east - a.east, dy = b.north - a.north;
        const double len2 = dx * dx + dy * dy;
        double f = len2 > 0.0 ? ((q.east - a.east) * dx + (q.north - a.north) * dy) / len2 : 0.0;
        f = std::max(0.0, std::min(1.0, f));
        const LocalPoint proj{a.east + f * dx, a.north + f * dy};
        const double len = std::sqrt(len2);
        if (f * len <= kJunctionSnapMetres) f = 0.0;                 // at junction i
        else if ((1.0 - f) * len <= kJunctionSnapMetres) f = 1.0;    // at junction i + 1
        consider(distanceMetres(q, proj), t.cum[i] + f * (t.cum[i + 1] - t.cum[i]));
    }

    t.lastGps = fix;
    ++t.gpsFixes;
    t.offRouteMetres = bestDistance;
    // copy first: std::max takes references, and C++14 then needs an out-of-class
    // definition of the constant (an "undefined reference" link error without it)
    const double offRouteLimit = kOffRouteMetres;
    t.offRoute = bestDistance > std::max(offRouteLimit, 2.0 * fix.accuracyMetres);
    t.fixTime = now;
    t.source = FixSource::Gps;
    if (!t.offRoute) {
        t.anchorProgress = std::min(t.total, std::max(0.0, bestProgress));
        t.matchTime = now;
    } else {
        t.anchorProgress = current;   // keep the last on-route position
    }
    t.anchorTime = now;
    return true;
}

void LiveTracker::setMode(TrackingMode mode, double now) {
    for (auto& kv : trips_) {   // keep every trip where it is now
        kv.second.anchorProgress = progressAt(kv.second, now);
        kv.second.anchorTime = now;
    }
    mode_ = mode;
}

bool LiveTracker::setSecondsPerRoadMinute(double seconds, double now) {
    if (!(seconds >= 0.05 && seconds <= 600.0)) return false;
    for (auto& kv : trips_) {   // keep every trip where it is now
        kv.second.anchorProgress = progressAt(kv.second, now);
        kv.second.anchorTime = now;
    }
    secondsPerMinute_ = seconds;
    return true;
}

}  // namespace opera

#pragma once

// LiveTracker - real-time position of every ambulance on a call.
//
// The SERVER keeps one live position per active dispatch, and every screen
// (dashboard, tracking page, 3D map) shows that same position.
//
// Where the position comes from (TrackingMode):
//
//   RealTime       (default) The trip starts moving the moment it is dispatched,
//                  at REAL road speed: 1 road minute takes 1 real minute, so a
//                  19-minute route takes 19 minutes. This is how dispatch centres
//                  show a vehicle with no GPS yet: an estimate from the route and
//                  the time since dispatch. When a GPS fix or a junction report
//                  arrives, the position jumps to that real point and the
//                  estimate continues from there. No phone is needed to see the
//                  ambulance move; a phone makes the position exact.
//
//   Gps            GPS ONLY. The position moves only when the ambulance
//                  says where it is:
//                    - a GPS fix from the crew's phone (POST /api/tracking/gps,
//                      sent automatically by crew.html), or
//                    - a manual "we reached junction X" (POST /api/tracking/report).
//                  Between fixes the position stays where it was last seen;
//                  nothing is invented. Until the first fix the ambulance is
//                  shown at its start and the trip says "waiting for GPS".
//
//   Simulated      Demo mode for when no crew device is connected: the
//                  position moves forward along the route at
//                  secondsPerRoadMinute (dead reckoning). Fixes still correct it.
//                  Every reply says which mode is on, so a simulation is never
//                  shown as if it were real.
//
// Map matching. A GPS fix is latitude/longitude; the route is a chain of junctions
// (the engine's Dijkstra paths ambulance -> patient -> hospital). Each junction
// on the route needs real coordinates (GEO / GEOREF lines in city_map.txt). The
// fix is projected onto the nearest road segment of the route, and that point's
// distance along the route, in road minutes, becomes the live progress. So the
// ETA is always "road minutes still ahead from where the ambulance really is".
//
//   - A fix that would jump backwards, or further forward than the ambulance can
//     have driven since the last fix, is scored down. This keeps routes that use
//     the same road twice (out and back) from snapping to the wrong visit.
//   - A fix farther than max(kOffRouteMetres, 2 x its accuracy) from the route is
//     kept as "off route by N m" but does NOT move the progress.
//   - A fix less accurate than kMaxAccuracyMetres is refused.
//   - A fix within kJunctionSnapMetres of a junction (along the road) counts as
//     being at it, so "arrived at the patient / hospital" is reached in practice.
//
// The position is kept as "road minutes covered" along the route. Time is passed
// in as seconds (any steady clock), so the class is testable without waiting.
//
// sync() is called with the coordinator's list of active dispatches whenever a
// reply is built: new dispatches (manual or automatic) start being tracked,
// finished or undone ones are dropped. No other code has to remember to call it.

#include <string>
#include <unordered_map>
#include <vector>

#include "DispatchCoordinator.hpp"
#include "Geo.hpp"
#include "Graph.hpp"

namespace opera {

enum class TrackingMode { Gps, Simulated, RealTime };

// Where the current position of a trip came from.
enum class FixSource { None, Junction, Gps };

struct GpsFix {
    double lat = 0.0;
    double lon = 0.0;
    double accuracyMetres = 0.0;  // radius reported by the device (0 = unknown)
    double speedMps = -1.0;       // metres per second, -1 = unknown
};

struct LivePosition {
    int emergencyId = -1;
    int ambulanceId = -1;
    double progress = 0.0;        // road minutes covered so far
    double toSceneMinutes = 0.0;  // where the patient is picked up
    double totalMinutes = 0.0;    // ambulance -> patient -> hospital
    int phase = 0;                // 0 to patient, 1 to hospital, 2 arrived at hospital
    bool reported = false;        // the last fix came from the crew (junction or GPS)
    int reports = 0;              // junction reports so far
    double secondsSinceFix = 0.0; // since the last fix (or since the dispatch if none yet)
    FixSource source = FixSource::None;
    bool estimated = false;       // true: dead reckoning moved it (Simulated or RealTime mode)
    bool realTime = false;        // the estimate runs at real road speed (RealTime mode)
    // last GPS fix (hasGps false: none yet)
    bool hasGps = false;
    int gpsFixes = 0;
    double lat = 0.0, lon = 0.0, accuracyMetres = 0.0, speedMps = -1.0;
    double offRouteMetres = 0.0;  // distance of the last GPS fix from the route
    bool offRoute = false;        // last GPS fix too far from the route to be used
};

class LiveTracker {
public:
    static constexpr double kOffRouteMetres = 120.0;
    static constexpr double kMaxAccuracyMetres = 250.0;
    static constexpr double kJunctionSnapMetres = 40.0;  // this close to a junction = at it

    // RealTime mode always moves at this speed: 1 road minute = 60 real seconds.
    static constexpr double kRealSecondsPerRoadMinute = 60.0;

    explicit LiveTracker(double secondsPerRoadMinute = 1.0, TrackingMode mode = TrackingMode::Gps);

    // Real-world coordinates of the graph's nodes (index = node id). Trips whose
    // route has a node without coordinates cannot take GPS fixes.
    void setNodeGeo(std::vector<GeoPoint> nodeGeo);
    // True if at least one node has coordinates.
    bool hasGeo() const;

    // Start tracking new active dispatches, stop tracking the others.
    void sync(const std::vector<DispatchResult>& active, const Graph& roads, double now);

    bool isTracking(int emergencyId) const { return trips_.count(emergencyId) != 0; }
    std::size_t size() const { return trips_.size(); }

    // Live position at time `now`; emergencyId -1 if it is not being tracked.
    LivePosition position(int emergencyId, double now) const;
    // All tracked trips, ordered by emergency id.
    std::vector<LivePosition> all(double now) const;

    // The crew reached `nodeId`. It must be on the trip's route. Returns false
    // (and explains why in `error`) otherwise; then nothing changes.
    bool reportNode(int emergencyId, int nodeId, double now, std::string& error);

    // A GPS fix from the crew's device. Returns false (with `error`) if the trip
    // is not tracked, its route has no coordinates, or the fix is unusable; then
    // nothing changes. An off-route fix returns true but leaves the progress alone
    // (see LivePosition::offRoute).
    bool reportGps(int emergencyId, const GpsFix& fix, double now, std::string& error);

    TrackingMode mode() const { return mode_; }
    // Switching keeps every trip where it is now.
    void setMode(TrackingMode mode, double now);

    // Real seconds per road minute (Simulated mode only).
    // Changing it keeps every trip's current progress.
    bool setSecondsPerRoadMinute(double seconds, double now);
    double secondsPerRoadMinute() const { return secondsPerMinute_; }

private:
    struct Trip {
        int emergencyId = -1;
        int ambulanceId = -1;
        std::vector<int> chain;       // nodes: ambulance -> patient -> hospital
        std::vector<double> cum;      // road minutes from the start to chain[i]
        std::vector<LocalPoint> pts;  // chain[i] in local metres (if geoReady)
        bool geoReady = false;        // every node of the chain has coordinates
        double toScene = 0.0;
        double total = 0.0;
        double anchorProgress = 0.0;  // progress at anchorTime
        double anchorTime = 0.0;      // dead reckoning starts here (Simulated mode)
        double fixTime = 0.0;         // last fix, or the dispatch
        double matchTime = 0.0;       // last fix that moved the progress (not off route), or the dispatch
        FixSource source = FixSource::None;
        int reports = 0;
        int gpsFixes = 0;
        GpsFix lastGps;
        double offRouteMetres = 0.0;
        bool offRoute = false;
    };

    double progressAt(const Trip& t, double now) const;
    LivePosition describe(const Trip& t, double now) const;
    Trip makeTrip(const DispatchResult& d, const Graph& roads, double now) const;

    std::unordered_map<int, Trip> trips_;   // by emergency id
    std::vector<GeoPoint> nodeGeo_;
    GeoPoint origin_;                       // reference point for local metres
    double secondsPerMinute_;
    TrackingMode mode_;
};

const char* toString(TrackingMode mode);
bool parseTrackingMode(const std::string& text, TrackingMode& out);
// "gps", "junction report", "waiting for gps" or "simulated"
std::string sourceLabel(const LivePosition& p);

}  // namespace opera

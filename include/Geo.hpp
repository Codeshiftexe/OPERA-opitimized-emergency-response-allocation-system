#pragma once

// Geo helpers for real GPS tracking.
//
// A GPS fix is latitude/longitude. The road graph is junctions joined by roads.
// To put a fix on a route we turn both into flat local metres (east, north)
// around one reference point. Over a city (a few tens of km) the error of this
// equirectangular projection is far below GPS accuracy, so no map library is needed.

#include <cmath>

namespace opera {

struct GeoPoint {
    double lat = 0.0;
    double lon = 0.0;
    bool known = false;   // false: this junction has no real-world coordinates
};

struct LocalPoint {
    double east = 0.0;    // metres
    double north = 0.0;   // metres
};

constexpr double kEarthRadiusMetres = 6371000.0;
constexpr double kPi = 3.14159265358979323846;

inline double degToRad(double d) { return d * kPi / 180.0; }

inline bool validLatLon(double lat, double lon) {
    return std::isfinite(lat) && std::isfinite(lon) && lat >= -90.0 && lat <= 90.0 &&
           lon >= -180.0 && lon <= 180.0;
}

// p in metres east/north of `origin`.
inline LocalPoint toLocal(const GeoPoint& origin, double lat, double lon) {
    const double k = degToRad(1.0) * kEarthRadiusMetres;  // metres per degree of latitude
    return LocalPoint{(lon - origin.lon) * k * std::cos(degToRad(origin.lat)),
                      (lat - origin.lat) * k};
}

// The point `east`/`north` metres away from `origin`.
inline GeoPoint fromLocal(const GeoPoint& origin, double east, double north) {
    const double k = degToRad(1.0) * kEarthRadiusMetres;
    GeoPoint g;
    g.lat = origin.lat + north / k;
    g.lon = origin.lon + east / (k * std::cos(degToRad(origin.lat)));
    g.known = true;
    return g;
}

inline double distanceMetres(const LocalPoint& a, const LocalPoint& b) {
    return std::hypot(a.east - b.east, a.north - b.north);
}

}  // namespace opera

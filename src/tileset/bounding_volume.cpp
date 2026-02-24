#include "bounding_volume.h"

#include <cmath>
#include <algorithm>

namespace tileset {

// Coordinate conversion helpers
static constexpr double PI = 3.14159265358979323846;
static constexpr double DEG_TO_RAD = PI / 180.0;

static double degreesToRadians(double deg) {
    return deg * DEG_TO_RAD;
}

// Box implementation
Box Box::fromCenterAndHalfLengths(double cx, double cy, double cz,
                                   double hx, double hy, double hz) {
    Box box;
    box.values = {
        cx, cy, cz,           // center
        hx, 0.0, 0.0,         // x axis (half-length vector)
        0.0, hy, 0.0,         // y axis (half-length vector)
        0.0, 0.0, hz          // z axis (half-length vector)
    };
    return box;
}

double Box::diagonal() const {
    // Calculate the full diagonal of the box
    double hx = std::sqrt(values[3]*values[3] + values[4]*values[4] + values[5]*values[5]);
    double hy = std::sqrt(values[6]*values[6] + values[7]*values[7] + values[8]*values[8]);
    double hz = std::sqrt(values[9]*values[9] + values[10]*values[10] + values[11]*values[11]);
    return 2.0 * std::sqrt(hx*hx + hy*hy + hz*hz);
}

Box Box::extended(double ratio) const {
    Box result = *this;
    double scale = 1.0 + ratio;
    // Scale the half-length vectors
    for (int i = 3; i < 12; ++i) {
        result.values[i] *= scale;
    }
    return result;
}

// Region implementation
Region Region::fromDegrees(double west_deg, double south_deg, double east_deg, double north_deg,
                           double min_h, double max_h) {
    return Region(
        degreesToRadians(west_deg),
        degreesToRadians(south_deg),
        degreesToRadians(east_deg),
        degreesToRadians(north_deg),
        min_h, max_h
    );
}

double Region::diagonal(double latitude) const {
    // Approximate meters per degree
    double lat_rad = latitude != 0.0 ? latitude : (north + south) * 0.5;
    double meters_per_lon = 111320.0 * std::cos(lat_rad);
    double meters_per_lat = 110540.0;

    double dx = (east - west) * meters_per_lon;
    double dy = (north - south) * meters_per_lat;
    double dz = max_height - min_height;

    return std::sqrt(dx*dx + dy*dy + dz*dz);
}

// Sphere implementation
Sphere Sphere::fromCenterAndRadius(double cx, double cy, double cz, double radius) {
    Sphere s;
    s.values = {cx, cy, cz, radius};
    return s;
}

// Helper to get center from any bounding volume type
namespace {
    struct CenterVisitor {
        std::array<double, 3> operator()(const Box& b) const {
            return {b.values[0], b.values[1], b.values[2]};
        }
        std::array<double, 3> operator()(const Region& r) const {
            double lon = (r.west + r.east) * 0.5;
            double lat = (r.south + r.north) * 0.5;
            double height = (r.min_height + r.max_height) * 0.5;
            return {lon, lat, height};
        }
        std::array<double, 3> operator()(const Sphere& s) const {
            return {s.values[0], s.values[1], s.values[2]};
        }
    };
}

// General functions
double computeDiagonal(const BoundingVolume& bv, double reference_latitude) {
    return std::visit([&](const auto& v) -> double {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, Box>) {
            return v.diagonal();
        } else if constexpr (std::is_same_v<T, Region>) {
            return v.diagonal(reference_latitude);
        } else if constexpr (std::is_same_v<T, Sphere>) {
            return 2.0 * v.radius();
        }
        return 0.0;
    }, bv);
}

std::vector<double> toJsonArray(const BoundingVolume& bv) {
    return std::visit([](const auto& v) -> std::vector<double> {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, Box>) {
            return std::vector<double>(v.values.begin(), v.values.end());
        } else if constexpr (std::is_same_v<T, Region>) {
            return {v.west, v.south, v.east, v.north, v.min_height, v.max_height};
        } else if constexpr (std::is_same_v<T, Sphere>) {
            return std::vector<double>(v.values.begin(), v.values.end());
        }
        return {};
    }, bv);
}

Box createBoxFromMinMax(double min_x, double min_y, double min_z,
                        double max_x, double max_y, double max_z) {
    double cx = (min_x + max_x) * 0.5;
    double cy = (min_y + max_y) * 0.5;
    double cz = (min_z + max_z) * 0.5;
    double hx = (max_x - min_x) * 0.5;
    double hy = (max_y - min_y) * 0.5;
    double hz = (max_z - min_z) * 0.5;
    return Box::fromCenterAndHalfLengths(cx, cy, cz, hx, hy, hz);
}

Region createRegionFromDegrees(double min_lon, double min_lat, double max_lon, double max_lat,
                               double min_height, double max_height) {
    return Region::fromDegrees(min_lon, min_lat, max_lon, max_lat, min_height, max_height);
}

std::optional<BoundingVolume> mergeBoundingVolumes(const BoundingVolume& a, const BoundingVolume& b) {
    // Both must be the same type
    if (a.index() != b.index()) {
        return std::nullopt;
    }

    // Use std::get to access the specific type since we know they match
    if (std::holds_alternative<Box>(a)) {
        const Box& v1 = std::get<Box>(a);
        const Box& v2 = std::get<Box>(b);

        // For boxes, we compute the union of the two boxes
        // This is an approximation - we create a new AABB that contains both
        auto c1 = v1.center();
        auto x1 = v1.xAxis();
        auto y1 = v1.yAxis();
        auto z1 = v1.zAxis();

        auto c2 = v2.center();
        auto x2 = v2.xAxis();
        auto y2 = v2.yAxis();
        auto z2 = v2.zAxis();

        // Compute corners of both boxes and find min/max
        double min_x = std::min(c1[0] - std::abs(x1[0]) - std::abs(y1[0]) - std::abs(z1[0]),
                                c2[0] - std::abs(x2[0]) - std::abs(y2[0]) - std::abs(z2[0]));
        double max_x = std::max(c1[0] + std::abs(x1[0]) + std::abs(y1[0]) + std::abs(z1[0]),
                                c2[0] + std::abs(x2[0]) + std::abs(y2[0]) + std::abs(z2[0]));
        double min_y = std::min(c1[1] - std::abs(x1[1]) - std::abs(y1[1]) - std::abs(z1[1]),
                                c2[1] - std::abs(x2[1]) - std::abs(y2[1]) - std::abs(z2[1]));
        double max_y = std::max(c1[1] + std::abs(x1[1]) + std::abs(y1[1]) + std::abs(z1[1]),
                                c2[1] + std::abs(x2[1]) + std::abs(y2[1]) + std::abs(z2[1]));
        double min_z = std::min(c1[2] - std::abs(x1[2]) - std::abs(y1[2]) - std::abs(z1[2]),
                                c2[2] - std::abs(x2[2]) - std::abs(y2[2]) - std::abs(z2[2]));
        double max_z = std::max(c1[2] + std::abs(x1[2]) + std::abs(y1[2]) + std::abs(z1[2]),
                                c2[2] + std::abs(x2[2]) + std::abs(y2[2]) + std::abs(z2[2]));

        return createBoxFromMinMax(min_x, min_y, min_z, max_x, max_y, max_z);
    }
    else if (std::holds_alternative<Region>(a)) {
        const Region& v1 = std::get<Region>(a);
        const Region& v2 = std::get<Region>(b);

        Region r;
        r.west = std::min(v1.west, v2.west);
        r.south = std::min(v1.south, v2.south);
        r.east = std::max(v1.east, v2.east);
        r.north = std::max(v1.north, v2.north);
        r.min_height = std::min(v1.min_height, v2.min_height);
        r.max_height = std::max(v1.max_height, v2.max_height);
        return r;
    }
    else if (std::holds_alternative<Sphere>(a)) {
        const Sphere& v1 = std::get<Sphere>(a);
        const Sphere& v2 = std::get<Sphere>(b);

        // For spheres, we create a new sphere that contains both
        auto c1 = v1.center();
        auto c2 = v2.center();
        double dx = c2[0] - c1[0];
        double dy = c2[1] - c1[1];
        double dz = c2[2] - c1[2];
        double dist = std::sqrt(dx*dx + dy*dy + dz*dz);

        if (dist + v2.radius() <= v1.radius()) {
            return v1; // v1 contains v2
        }
        if (dist + v1.radius() <= v2.radius()) {
            return v2; // v2 contains v1
        }

        // New sphere contains both
        double new_radius = (dist + v1.radius() + v2.radius()) * 0.5;
        double ratio = (new_radius - v1.radius()) / dist;
        double cx = c1[0] + dx * ratio;
        double cy = c1[1] + dy * ratio;
        double cz = c1[2] + dz * ratio;
        return Sphere::fromCenterAndRadius(cx, cy, cz, new_radius);
    }

    return std::nullopt;
}

} // namespace tileset

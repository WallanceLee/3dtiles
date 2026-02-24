#pragma once

#include <array>
#include <optional>
#include <variant>
#include <vector>

namespace tileset {

// 3D Tiles bounding volume types
// Reference: https://github.com/CesiumGS/3d-tiles/tree/main/specification#bounding-volumes

// Box: center(3) + x_axis(3) + y_axis(3) + z_axis(3) = 12 numbers
// The x/y/z axes are half-length vectors
struct Box {
    std::array<double, 12> values{};

    Box() = default;
    explicit Box(const std::array<double, 12>& v) : values(v) {}

    // Convenience constructor from center and half-lengths (axis-aligned)
    static Box fromCenterAndHalfLengths(double cx, double cy, double cz,
                                        double hx, double hy, double hz);

    // Get center point
    std::array<double, 3> center() const { return {values[0], values[1], values[2]}; }

    // Get half-length vectors
    std::array<double, 3> xAxis() const { return {values[3], values[4], values[5]}; }
    std::array<double, 3> yAxis() const { return {values[6], values[7], values[8]}; }
    std::array<double, 3> zAxis() const { return {values[9], values[10], values[11]}; }

    // Calculate diagonal length (approximate size of the box)
    double diagonal() const;

    // Extend box by a ratio (inflate)
    Box extended(double ratio) const;
};

// Region: west, south, east, north, min_height, max_height (radians for angles)
struct Region {
    double west = 0.0;      // radians
    double south = 0.0;     // radians
    double east = 0.0;      // radians
    double north = 0.0;     // radians
    double min_height = 0.0; // meters
    double max_height = 0.0; // meters

    Region() = default;
    Region(double w, double s, double e, double n, double min_h, double max_h)
        : west(w), south(s), east(e), north(n), min_height(min_h), max_height(max_h) {}

    // Create from degrees
    static Region fromDegrees(double west_deg, double south_deg, double east_deg, double north_deg,
                              double min_h, double max_h);

    // Calculate approximate diagonal in meters
    double diagonal(double latitude) const;
};

// Sphere: center(3) + radius = 4 numbers
struct Sphere {
    std::array<double, 4> values{};

    Sphere() = default;
    explicit Sphere(const std::array<double, 4>& v) : values(v) {}

    static Sphere fromCenterAndRadius(double cx, double cy, double cz, double radius);

    std::array<double, 3> center() const { return {values[0], values[1], values[2]}; }
    double radius() const { return values[3]; }
};

// Bounding volume variant type
using BoundingVolume = std::variant<Box, Region, Sphere>;

// Calculate diagonal length for any bounding volume type
double computeDiagonal(const BoundingVolume& bv, double reference_latitude = 0.0);

// Convert bounding volume to JSON array
std::vector<double> toJsonArray(const BoundingVolume& bv);

// Factory functions from various inputs

// Create box from min/max corners (axis-aligned)
Box createBoxFromMinMax(double min_x, double min_y, double min_z,
                        double max_x, double max_y, double max_z);

// Create region from tile bounds in degrees
Region createRegionFromDegrees(double min_lon, double min_lat, double max_lon, double max_lat,
                               double min_height, double max_height);

// Merge two bounding volumes (only for same type)
std::optional<BoundingVolume> mergeBoundingVolumes(const BoundingVolume& a, const BoundingVolume& b);

} // namespace tileset

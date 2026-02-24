#pragma once

#include <array>
#include <cmath>
#include <numbers>

namespace tileset {

// Coordinate conversion utilities
// Reference: https://github.com/CesiumGS/3d-tiles/tree/main/specification#coordinate-reference-system-crs

// Constants
constexpr double PI = std::numbers::pi;
constexpr double DEG_TO_RAD = PI / 180.0;
constexpr double RAD_TO_DEG = 180.0 / PI;

// WGS84 ellipsoid parameters
constexpr double WGS84_A = 6378137.0;                    // Semi-major axis (meters)
constexpr double WGS84_B = 6356752.3142451793;          // Semi-minor axis (meters)
constexpr double WGS84_E2 = 0.00669437999013;           // First eccentricity squared

// Coordinate conversions
inline double degreesToRadians(double degrees) {
    return degrees * DEG_TO_RAD;
}

inline double radiansToDegrees(double radians) {
    return radians * RAD_TO_DEG;
}

// Convert latitude difference to meters
// 1 degree latitude ≈ 111.32 km at equator
double latitudeToMeters(double delta_degrees);

// Convert longitude difference to meters at a given latitude
double longitudeToMeters(double delta_degrees, double latitude_deg);

// Convert meters to latitude difference
double metersToLatitude(double meters);

// Convert meters to longitude difference at a given latitude
double metersToLongitude(double meters, double latitude_deg);

// ENU (East-North-Up) to ECEF (Earth-Centered-Earth-Fixed) transformation matrix
// This is a 4x4 column-major matrix represented as a flat array of 16 doubles
using TransformMatrix = std::array<double, 16>;

// Calculate ENU to ECEF transform matrix at a given geodetic position
// lon_deg, lat_deg: longitude and latitude in degrees
// height: ellipsoidal height in meters
TransformMatrix calcEnuToEcefMatrix(double lon_deg, double lat_deg, double height);

// Calculate ECEF to ENU transform matrix (inverse of ENU to ECEF)
TransformMatrix calcEcefToEnuMatrix(double lon_deg, double lat_deg, double height);

// Apply translation to a transform matrix
// The translation is applied in the local ENU frame
TransformMatrix applyEnuTranslation(const TransformMatrix& base, double tx, double ty, double tz);

// Extract translation component from transform matrix
std::array<double, 3> getTranslation(const TransformMatrix& matrix);

// Convert flat array to nested array for JSON output
std::array<std::array<double, 4>, 4> toNestedArray(const TransformMatrix& m);

// Matrix multiplication
TransformMatrix multiplyMatrices(const TransformMatrix& a, const TransformMatrix& b);

// Identity matrix
TransformMatrix identityMatrix();

} // namespace tileset

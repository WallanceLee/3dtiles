#include "transform.h"

#include <cmath>

namespace tileset {

// Approximate conversion factors
// 1 degree latitude ≈ 111.32 km (varies slightly with latitude)
// 1 degree longitude ≈ 111.32 km * cos(latitude)
static constexpr double METERS_PER_DEGREE_LAT = 111320.0;

double latitudeToMeters(double delta_degrees) {
    return delta_degrees * METERS_PER_DEGREE_LAT;
}

double longitudeToMeters(double delta_degrees, double latitude_deg) {
    double lat_rad = degreesToRadians(latitude_deg);
    return delta_degrees * METERS_PER_DEGREE_LAT * std::cos(lat_rad);
}

double metersToLatitude(double meters) {
    return meters / METERS_PER_DEGREE_LAT;
}

double metersToLongitude(double meters, double latitude_deg) {
    double lat_rad = degreesToRadians(latitude_deg);
    return meters / (METERS_PER_DEGREE_LAT * std::cos(lat_rad));
}

TransformMatrix calcEnuToEcefMatrix(double lon_deg, double lat_deg, double height) {
    double lon = degreesToRadians(lon_deg);
    double lat = degreesToRadians(lat_deg);

    double sinLon = std::sin(lon);
    double cosLon = std::cos(lon);
    double sinLat = std::sin(lat);
    double cosLat = std::cos(lat);

    // Calculate ECEF position of the origin
    // Using WGS84 ellipsoid
    double N = WGS84_A / std::sqrt(1.0 - WGS84_E2 * sinLat * sinLat);
    double x0 = (N + height) * cosLat * cosLon;
    double y0 = (N + height) * cosLat * sinLon;
    double z0 = (N * (1.0 - WGS84_E2) + height) * sinLat;

    // ENU to ECEF rotation matrix (column-major for OpenGL/GLM compatibility)
    // East axis (points in direction of increasing longitude)
    // North axis (points in direction of increasing latitude)
    // Up axis (points away from Earth's center)
    TransformMatrix matrix = {
        -sinLon,         cosLon,          0.0,    0.0,  // Column 0 (East direction in ECEF)
        -sinLat * cosLon, -sinLat * sinLon, cosLat, 0.0,  // Column 1 (North direction in ECEF)
        cosLat * cosLon,  cosLat * sinLon,  sinLat, 0.0,  // Column 2 (Up direction in ECEF)
        x0,              y0,               z0,     1.0   // Column 3 (Translation)
    };

    return matrix;
}

TransformMatrix calcEcefToEnuMatrix(double lon_deg, double lat_deg, double height) {
    // ECEF to ENU is the transpose of ENU to ECEF (for rotation part)
    // plus translation adjustment
    double lon = degreesToRadians(lon_deg);
    double lat = degreesToRadians(lat_deg);

    double sinLon = std::sin(lon);
    double cosLon = std::cos(lon);
    double sinLat = std::sin(lat);
    double cosLat = std::cos(lat);

    // Calculate ECEF position of the origin
    double N = WGS84_A / std::sqrt(1.0 - WGS84_E2 * sinLat * sinLat);
    double x0 = (N + height) * cosLat * cosLon;
    double y0 = (N + height) * cosLat * sinLon;
    double z0 = (N * (1.0 - WGS84_E2) + height) * sinLat;

    // ECEF to ENU transformation
    TransformMatrix matrix = {
        -sinLon, -sinLat * cosLon, cosLat * cosLon, 0.0,
        cosLon,  -sinLat * sinLon, cosLat * sinLon, 0.0,
        0.0,     cosLat,           sinLat,          0.0,
        0.0,     0.0,              0.0,             1.0
    };

    // Apply translation: subtract origin in ECEF, then rotate
    // This is simplified - full implementation would properly handle the translation
    matrix[12] = -(matrix[0] * x0 + matrix[4] * y0 + matrix[8] * z0);
    matrix[13] = -(matrix[1] * x0 + matrix[5] * y0 + matrix[9] * z0);
    matrix[14] = -(matrix[2] * x0 + matrix[6] * y0 + matrix[10] * z0);

    return matrix;
}

TransformMatrix applyEnuTranslation(const TransformMatrix& base, double tx, double ty, double tz) {
    TransformMatrix result = base;

    // Apply translation in ENU frame to the ECEF transformation
    // The translation (tx, ty, tz) in ENU needs to be converted to ECEF
    // and added to the translation component of the matrix

    // ENU axes in ECEF are the first three columns of the matrix
    double ecef_tx = tx * base[0] + ty * base[4] + tz * base[8];
    double ecef_ty = tx * base[1] + ty * base[5] + tz * base[9];
    double ecef_tz = tx * base[2] + ty * base[6] + tz * base[10];

    result[12] += ecef_tx;
    result[13] += ecef_ty;
    result[14] += ecef_tz;

    return result;
}

std::array<double, 3> getTranslation(const TransformMatrix& matrix) {
    return {matrix[12], matrix[13], matrix[14]};
}

std::array<std::array<double, 4>, 4> toNestedArray(const TransformMatrix& m) {
    return {{
        {m[0], m[1], m[2], m[3]},
        {m[4], m[5], m[6], m[7]},
        {m[8], m[9], m[10], m[11]},
        {m[12], m[13], m[14], m[15]}
    }};
}

TransformMatrix multiplyMatrices(const TransformMatrix& a, const TransformMatrix& b) {
    TransformMatrix result{};

    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            double sum = 0.0;
            for (int k = 0; k < 4; ++k) {
                sum += a[k * 4 + row] * b[col * 4 + k];
            }
            result[col * 4 + row] = sum;
        }
    }

    return result;
}

TransformMatrix identityMatrix() {
    return {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    };
}

} // namespace tileset

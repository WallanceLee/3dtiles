#include "geometric_error.h"

#include <algorithm>
#include <cmath>

namespace tileset {

double computeGeometricErrorFromDiagonal(double diagonal, double scale) {
    if (diagonal <= 0.0) {
        return 0.0;
    }
    return scale * diagonal;
}

double computeGeometricError(const BoundingVolume& bv, double scale) {
    double diagonal = computeDiagonal(bv);
    return computeGeometricErrorFromDiagonal(diagonal, scale);
}

double computeGeometricErrorFromBox(double width, double height, double depth, double scale) {
    double diagonal = std::sqrt(width * width + height * height + depth * depth);
    return computeGeometricErrorFromDiagonal(diagonal, scale);
}

double computeParentGeometricError(const std::vector<double>& childErrors, double multiplier) {
    if (childErrors.empty()) {
        return 0.0;
    }
    double maxChildError = *std::max_element(childErrors.begin(), childErrors.end());
    return maxChildError * multiplier;
}

double computeGeometricErrorFromSpans(double span_x, double span_y, double span_z, double scale) {
    double max_span = std::max({span_x, span_y, span_z});
    if (max_span <= 0.0) {
        return 0.0;
    }
    // Original formula from shp23dtile: max_span / 20.0
    // This is equivalent to scale * diagonal where scale = 1/20 for a cube
    return max_span / 20.0;
}

std::vector<double> computeLODGeometricErrors(const std::vector<LODLevel>& levels, double base_diagonal) {
    std::vector<double> errors;
    errors.reserve(levels.size());
    
    double base_error = computeGeometricErrorFromDiagonal(base_diagonal);
    
    for (const auto& level : levels) {
        // Coarser LOD (smaller ratio) gets larger geometric error
        // This ensures that when the camera is far, the coarser model is used
        double ratio = std::max(0.01, std::min(1.0, level.target_ratio));
        double error = base_error * std::max(1.0, 1.0 / std::sqrt(ratio));
        errors.push_back(error);
    }
    
    return errors;
}

} // namespace tileset

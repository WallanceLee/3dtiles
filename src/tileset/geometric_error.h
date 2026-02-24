#pragma once

#include "bounding_volume.h"

namespace tileset {

// Geometric error calculation strategies
// Reference: https://github.com/CesiumGS/3d-tiles/tree/main/specification#geometric-error

// Default scale factor for geometric error calculation
constexpr double DEFAULT_GEOMETRIC_ERROR_SCALE = 0.5;

// Calculate geometric error from bounding volume diagonal
// The error is typically a fraction of the diagonal length
double computeGeometricErrorFromDiagonal(double diagonal, double scale = DEFAULT_GEOMETRIC_ERROR_SCALE);

// Calculate geometric error directly from a bounding volume
double computeGeometricError(const BoundingVolume& bv, double scale = DEFAULT_GEOMETRIC_ERROR_SCALE);

// Calculate geometric error from box dimensions
double computeGeometricErrorFromBox(double width, double height, double depth, double scale = DEFAULT_GEOMETRIC_ERROR_SCALE);

// Calculate geometric error for a tile based on its children's errors
// Parent error should be larger than children's errors
double computeParentGeometricError(const std::vector<double>& childErrors, double multiplier = 2.0);

// Compute geometric error from spans (used in shp23dtile)
double computeGeometricErrorFromSpans(double span_x, double span_y, double span_z, double scale = DEFAULT_GEOMETRIC_ERROR_SCALE);

// LOD (Level of Detail) geometric error calculation
// For hierarchical LOD, coarser levels have larger geometric errors
struct LODLevel {
    double target_ratio;  // Mesh simplification ratio (1.0 = full detail, 0.5 = half detail)
    double base_error;    // Base geometric error for this level
};

// Calculate geometric errors for LOD levels
// Returns a vector of geometric errors, one per LOD level
std::vector<double> computeLODGeometricErrors(const std::vector<LODLevel>& levels, double base_diagonal);

} // namespace tileset

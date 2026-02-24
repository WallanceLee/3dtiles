#include "tileset_types.h"

#include "geometric_error.h"

#include <algorithm>
#include <cmath>

namespace tileset {

void Tile::updateGeometricErrorFromChildren(double multiplier) {
    if (children.empty()) {
        return;
    }

    std::vector<double> childErrors;
    childErrors.reserve(children.size());
    for (const auto& child : children) {
        childErrors.push_back(child.geometricError);
    }

    geometricError = computeParentGeometricError(childErrors, multiplier);
}

bool Tile::computeBoundingVolumeFromChildren() {
    if (children.empty()) {
        return false;
    }

    BoundingVolume merged = children[0].boundingVolume;
    for (size_t i = 1; i < children.size(); ++i) {
        auto result = mergeBoundingVolumes(merged, children[i].boundingVolume);
        if (!result) {
            return false; // Cannot merge different types
        }
        merged = *result;
    }

    boundingVolume = merged;
    return true;
}

void Tileset::updateGeometricError() {
    if (geometricError <= 0.0) {
        geometricError = computeGeometricError(root.boundingVolume);
    }
}

// TileBuilder implementations

Tile TileBuilder::createBoxTile(double cx, double cy, double cz,
                                 double hx, double hy, double hz,
                                 double geometricError) {
    Box box = Box::fromCenterAndHalfLengths(cx, cy, cz, hx, hy, hz);
    Tile tile(box);
    tile.geometricError = geometricError;
    return tile;
}

Tile TileBuilder::createRegionTile(double west, double south, double east, double north,
                                    double min_height, double max_height,
                                    double geometricError) {
    Region region = Region::fromDegrees(west, south, east, north, min_height, max_height);
    Tile tile(region);
    tile.geometricError = geometricError;
    return tile;
}

Tile TileBuilder::createQuadtreeTile(int level, int x, int y, int maxLevel,
                                      const BoundingVolume& rootBv,
                                      double baseGeometricError) {
    // This is a simplified implementation
    // In practice, you'd compute the actual bounds for each tile

    Tile tile;
    tile.geometricError = baseGeometricError / std::pow(2.0, level);

    // For now, just use the root bounding volume
    // A full implementation would subdivide the bounds
    tile.boundingVolume = rootBv;

    // Create children if not at max level
    if (level < maxLevel) {
        for (int i = 0; i < 4; ++i) {
            int childX = x * 2 + (i % 2);
            int childY = y * 2 + (i / 2);
            Tile child = createQuadtreeTile(level + 1, childX, childY, maxLevel,
                                            rootBv, baseGeometricError);
            tile.addChild(std::move(child));
        }
    }

    return tile;
}

} // namespace tileset

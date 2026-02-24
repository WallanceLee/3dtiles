#pragma once

// Main header for the tileset module
// Includes all components for 3D Tiles tileset.json generation

#include "bounding_volume.h"
#include "geometric_error.h"
#include "tileset_types.h"
#include "tileset_writer.h"
#include "transform.h"

/**
 * @brief Tileset Module for 3D Tiles
 *
 * This module provides a unified interface for generating 3D Tiles tileset.json files.
 * It consolidates the previously scattered tileset generation logic from:
 * - shp23dtile (Shapefile to 3D Tiles)
 * - osgb23dtile (OSGB to 3D Tiles)
 * - fbx23dtile (FBX to 3D Tiles)
 *
 * Example usage:
 * @code
 * using namespace tileset;
 *
 * // Create a simple tileset
 * Box box = Box::fromCenterAndHalfLengths(0, 0, 50, 100, 100, 50);
 * Tile tile(box);
 * tile.geometricError = computeGeometricError(box);
 * tile.setContent("model.b3dm");
 *
 * Tileset tileset(tile);
 * tileset.setVersion("1.0");
 * tileset.setGltfUpAxis("Z");
 *
 * // Write to file
 * TilesetWriter writer;
 * writer.writeToFile(tileset, "tileset.json");
 * @endcode
 *
 * @see https://github.com/CesiumGS/3d-tiles/tree/main/specification
 */

namespace tileset {

// Version of the tileset module
constexpr const char* TILESET_MODULE_VERSION = "1.0.0";

} // namespace tileset

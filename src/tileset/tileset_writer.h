#pragma once

#include "tileset_types.h"

#include <nlohmann/json.hpp>
#include <string>

namespace tileset {

// JSON writer for 3D Tiles tileset.json
// Uses nlohmann/json for JSON generation
class TilesetWriter {
public:
    // Configuration options
    struct Options {
        int indent = 2;                    // JSON indentation (0 for compact)
        bool writeContentBoundingVolume = false;  // Write content.boundingVolume if available
        bool skipEmptyChildren = true;     // Skip "children": [] for leaf nodes
    };

    TilesetWriter();
    explicit TilesetWriter(const Options& opts);

    // Write a complete tileset to a JSON string
    std::string write(const Tileset& tileset) const;

    // Write a tileset to a file
    bool writeToFile(const Tileset& tileset, const std::string& filepath) const;

    // Write just a tile (for nested tilesets)
    std::string writeTile(const Tile& tile) const;

    // Convert to nlohmann::json object (for further manipulation)
    nlohmann::json toJson(const Tileset& tileset) const;
    nlohmann::json tileToJson(const Tile& tile) const;

    // Set options
    void setOptions(const Options& opts) { options_ = opts; }
    const Options& getOptions() const { return options_; }

private:
    Options options_;

    // JSON conversion helpers
    nlohmann::json assetToJson(const Asset& asset) const;
    nlohmann::json boundingVolumeToJson(const BoundingVolume& bv) const;
    nlohmann::json contentToJson(const Content& content) const;
    nlohmann::json transformToJson(const TransformMatrix& matrix) const;
};

// Convenience functions for writing tilesets

// Write a simple tileset with a single tile
std::string writeSimpleTileset(const BoundingVolume& bv,
                                double geometricError,
                                const std::string& contentUri,
                                const std::string& version = "1.0");

// Write a tileset with transform (for ENU to ECEF conversion)
std::string writeTilesetWithTransform(const BoundingVolume& bv,
                                       double geometricError,
                                       const std::string& contentUri,
                                       const TransformMatrix& transform,
                                       const std::string& version = "1.0");

// Write a hierarchical tileset from a tree structure
// The tree is defined by parent-child relationships in the tiles
std::string writeHierarchicalTileset(const Tile& rootTile,
                                      double rootGeometricError,
                                      const std::string& version = "1.0");

} // namespace tileset

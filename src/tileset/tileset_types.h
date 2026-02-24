#pragma once

#include "bounding_volume.h"
#include "transform.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tileset {

// Forward declarations
class Tile;

// Content object representing a tile's content
// https://github.com/CesiumGS/3d-tiles/tree/main/specification#content
struct Content {
    std::string uri;
    std::optional<BoundingVolume> boundingVolume; // Optional content-specific bounding volume

    Content() = default;
    explicit Content(const std::string& u) : uri(u) {}
    Content(const std::string& u, const BoundingVolume& bv) : uri(u), boundingVolume(bv) {}
};

// Tile object in 3D Tiles
// https://github.com/CesiumGS/3d-tiles/tree/main/specification#tile
class Tile {
public:
    // Required properties
    BoundingVolume boundingVolume;
    double geometricError = 0.0;

    // Optional properties
    std::optional<Content> content;
    std::vector<Tile> children;
    std::optional<TransformMatrix> transform;
    std::string refine = "REPLACE"; // "ADD" or "REPLACE"
    std::optional<BoundingVolume> viewerRequestVolume;

    // Metadata extensions (for 3D Tiles 1.1)
    // std::optional<Metadata> metadata;

    // Constructors
    Tile() = default;
    explicit Tile(const BoundingVolume& bv) : boundingVolume(bv) {}
    Tile(const BoundingVolume& bv, double ge) : boundingVolume(bv), geometricError(ge) {}

    // Convenience methods
    void addChild(const Tile& child) { children.push_back(child); }
    void addChild(Tile&& child) { children.push_back(std::move(child)); }

    void setContent(const std::string& uri) { content = Content(uri); }
    void setContent(const std::string& uri, const BoundingVolume& bv) { content = Content(uri, bv); }

    void setTransform(const TransformMatrix& matrix) { transform = matrix; }

    bool isLeaf() const { return children.empty(); }

    // Update geometric error based on children's errors
    void updateGeometricErrorFromChildren(double multiplier = 2.0);

    // Compute the bounding volume that contains all children
    // Returns true if successful
    bool computeBoundingVolumeFromChildren();
};

// Asset object
// https://github.com/CesiumGS/3d-tiles/tree/main/specification#asset
struct Asset {
    std::string version = "1.0";
    std::optional<std::string> tilesetVersion;
    std::optional<std::string> gltfUpAxis = "Z";

    Asset() = default;
    explicit Asset(const std::string& v) : version(v) {}
};

// Tileset root object
// https://github.com/CesiumGS/3d-tiles/tree/main/specification#tileset
class Tileset {
public:
    Asset asset;
    Tile root;
    double geometricError = 0.0;

    // Extensions (optional)
    // std::vector<Extension> extensionsUsed;
    // std::vector<Extension> extensionsRequired;

    // Constructors
    Tileset() = default;
    explicit Tileset(const Tile& r) : root(r) {}
    Tileset(const Tile& r, double ge) : root(r), geometricError(ge) {}

    // Convenience method to set asset version
    void setVersion(const std::string& version) { asset.version = version; }
    void setGltfUpAxis(const std::string& axis) { asset.gltfUpAxis = axis; }

    // Calculate root geometric error from root tile if not set
    void updateGeometricError();
};

// Helper struct for building tile hierarchies
struct TileBuilder {
    // Create a simple tile with box bounding volume
    static Tile createBoxTile(double cx, double cy, double cz,
                               double hx, double hy, double hz,
                               double geometricError);

    // Create a tile with region bounding volume
    static Tile createRegionTile(double west, double south, double east, double north,
                                  double min_height, double max_height,
                                  double geometricError);

    // Create a tile hierarchy from a quadtree structure
    // level: current level in the tree
    // x, y: tile coordinates
    // maxLevel: maximum depth
    static Tile createQuadtreeTile(int level, int x, int y, int maxLevel,
                                    const BoundingVolume& rootBv,
                                    double baseGeometricError);
};

} // namespace tileset

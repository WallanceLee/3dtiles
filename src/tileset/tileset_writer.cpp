#include "tileset_writer.h"
#include "geometric_error.h"

#include <fstream>
#include <iostream>

namespace tileset {

TilesetWriter::TilesetWriter() = default;

TilesetWriter::TilesetWriter(const Options& opts) : options_(opts) {}

std::string TilesetWriter::write(const Tileset& tileset) const {
    nlohmann::json j = toJson(tileset);
    if (options_.indent > 0) {
        return j.dump(options_.indent);
    }
    return j.dump();
}

bool TilesetWriter::writeToFile(const Tileset& tileset, const std::string& filepath) const {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        return false;
    }
    file << write(tileset);
    return file.good();
}

std::string TilesetWriter::writeTile(const Tile& tile) const {
    nlohmann::json j = tileToJson(tile);
    if (options_.indent > 0) {
        return j.dump(options_.indent);
    }
    return j.dump();
}

nlohmann::json TilesetWriter::toJson(const Tileset& tileset) const {
    nlohmann::json j;

    // Asset
    j["asset"] = assetToJson(tileset.asset);

    // Geometric error
    j["geometricError"] = tileset.geometricError > 0.0 ? tileset.geometricError
                                                         : computeGeometricError(tileset.root.boundingVolume);

    // Root tile
    j["root"] = tileToJson(tileset.root);

    return j;
}

nlohmann::json TilesetWriter::tileToJson(const Tile& tile) const {
    nlohmann::json j;

    // Bounding volume (required)
    j["boundingVolume"] = boundingVolumeToJson(tile.boundingVolume);

    // Geometric error (required)
    j["geometricError"] = tile.geometricError;

    // Refine mode
    if (!tile.refine.empty()) {
        j["refine"] = tile.refine;
    }

    // Transform (optional)
    if (tile.transform) {
        j["transform"] = transformToJson(*tile.transform);
    }

    // Content (optional)
    if (tile.content) {
        j["content"] = contentToJson(*tile.content);
    }

    // Viewer request volume (optional)
    if (tile.viewerRequestVolume) {
        j["viewerRequestVolume"] = boundingVolumeToJson(*tile.viewerRequestVolume);
    }

    // Children (optional)
    if (!tile.children.empty() || !options_.skipEmptyChildren) {
        j["children"] = nlohmann::json::array();
        for (const auto& child : tile.children) {
            j["children"].push_back(tileToJson(child));
        }
    }

    return j;
}

nlohmann::json TilesetWriter::assetToJson(const Asset& asset) const {
    nlohmann::json j;
    j["version"] = asset.version;
    if (asset.tilesetVersion) {
        j["tilesetVersion"] = *asset.tilesetVersion;
    }
    if (asset.gltfUpAxis) {
        j["gltfUpAxis"] = *asset.gltfUpAxis;
    }
    return j;
}

nlohmann::json TilesetWriter::boundingVolumeToJson(const BoundingVolume& bv) const {
    nlohmann::json j;

    std::visit([&j](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, Box>) {
            j["box"] = std::vector<double>(v.values.begin(), v.values.end());
        } else if constexpr (std::is_same_v<T, Region>) {
            j["region"] = {v.west, v.south, v.east, v.north, v.min_height, v.max_height};
        } else if constexpr (std::is_same_v<T, Sphere>) {
            j["sphere"] = std::vector<double>(v.values.begin(), v.values.end());
        }
    }, bv);

    return j;
}

nlohmann::json TilesetWriter::contentToJson(const Content& content) const {
    nlohmann::json j;
    j["uri"] = content.uri;

    if (options_.writeContentBoundingVolume && content.boundingVolume) {
        j["boundingVolume"] = boundingVolumeToJson(*content.boundingVolume);
    }

    return j;
}

nlohmann::json TilesetWriter::transformToJson(const TransformMatrix& matrix) const {
    // 3D Tiles expects column-major order (same as GLM)
    return std::vector<double>(matrix.begin(), matrix.end());
}

// Convenience functions

std::string writeSimpleTileset(const BoundingVolume& bv,
                                double geometricError,
                                const std::string& contentUri,
                                const std::string& version) {
    Tileset tileset;
    tileset.asset.version = version;
    tileset.asset.gltfUpAxis = "Z";
    tileset.geometricError = geometricError;

    Tile root(bv, geometricError);
    root.setContent(contentUri);
    tileset.root = std::move(root);

    TilesetWriter writer;
    return writer.write(tileset);
}

std::string writeTilesetWithTransform(const BoundingVolume& bv,
                                       double geometricError,
                                       const std::string& contentUri,
                                       const TransformMatrix& transform,
                                       const std::string& version) {
    Tileset tileset;
    tileset.asset.version = version;
    tileset.asset.gltfUpAxis = "Z";
    tileset.geometricError = geometricError;

    Tile root(bv, geometricError);
    root.setContent(contentUri);
    root.setTransform(transform);
    tileset.root = std::move(root);

    TilesetWriter writer;
    return writer.write(tileset);
}

std::string writeHierarchicalTileset(const Tile& rootTile,
                                      double rootGeometricError,
                                      const std::string& version) {
    Tileset tileset;
    tileset.asset.version = version;
    tileset.asset.gltfUpAxis = "Z";
    tileset.geometricError = rootGeometricError;
    tileset.root = rootTile;

    TilesetWriter writer;
    return writer.write(tileset);
}

} // namespace tileset

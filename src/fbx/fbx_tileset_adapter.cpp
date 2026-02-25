/**
 * @file fbx/fbx_tileset_adapter.cpp
 * @brief FBX Tileset适配器实现
 */

#include "fbx_tileset_adapter.h"
#include "../extern.h"
#include "../tileset/tileset_writer.h"
#include <cmath>

namespace fbx {

FBXTilesetAdapter::FBXTilesetAdapter(const FBXTilesetAdapterConfig& config)
    : config_(config) {}

bool FBXTilesetAdapter::buildAndWriteTileset(
    const FBXTileMetaMap& allMetas,
    const std::string& outputPath) {

    if (allMetas.empty()) {
        LOG_E("FBXTilesetAdapter: No tile metadata available");
        return false;
    }

    // 查找根节点
    auto rootMeta = findRootNode(allMetas);
    if (!rootMeta) {
        LOG_E("FBXTilesetAdapter: No root node found");
        return false;
    }

    // 为所有叶子节点生成子tileset（如果启用LOD）
    if (config_.enableLOD) {
        for (const auto& [key, meta] : allMetas) {
            if (meta->isLeaf && meta->hasGeometry) {
                if (!generateLeafTileset(meta, outputPath)) {
                    LOG_E("FBXTilesetAdapter: Failed to generate leaf tileset for node %lu", key);
                    return false;
                }
            }
        }
    }

    // 构建根tileset
    tileset::Tileset tileset;
    tileset.setVersion("1.0");
    tileset.setGltfUpAxis("Z");

    // 构建根节点
    tileset::Tile rootTile = buildTileRecursive(rootMeta, allMetas);

    // 添加根节点变换矩阵（ENU到ECEF）
    tileset::TransformMatrix transform = createRootTransform();
    rootTile.setTransform(transform);

    // 设置根几何误差
    double rootGe = rootMeta->geometricError > 0.0
                        ? rootMeta->geometricError
                        : computeGeometricError(rootMeta->bbox) * 2.0;
    tileset.geometricError = rootGe;
    tileset.root = std::move(rootTile);

    // 写入文件
    std::filesystem::path tilesetPath = std::filesystem::path(outputPath) / "tileset.json";
    tileset::TilesetWriter writer;

    if (!writer.writeToFile(tileset, tilesetPath.string())) {
        LOG_E("FBXTilesetAdapter: Failed to write tileset to %s", tilesetPath.string().c_str());
        return false;
    }

    return true;
}

bool FBXTilesetAdapter::generateLeafTileset(
    const FBXTileMetaPtr& meta,
    const std::string& outputPath) {

    if (!meta || meta->lodFiles.empty()) {
        return true; // 没有LOD文件，不需要生成子tileset
    }

    // 创建叶子节点的tileset
    tileset::Tileset leafTileset;
    leafTileset.setVersion("1.0");
    leafTileset.setGltfUpAxis("Z");

    // 计算几何误差
    double geometricError = meta->geometricError > 0.0
                                ? meta->geometricError
                                : computeGeometricError(meta->bbox);

    // 创建根tile（对应LOD0）
    tileset::Tile rootTile;
    rootTile.geometricError = geometricError;
    rootTile.refine = "REPLACE";

    // 设置包围盒
    rootTile.boundingVolume = convertBoundingBox(meta->bbox);

    // 创建LOD层级结构：LOD0 -> LOD1 -> LOD2
    // 在子tileset中，content URI应该是相对于子tileset所在目录的路径
    std::vector<std::pair<std::string, double>> lodLevels;
    for (size_t i = 0; i < meta->lodFiles.size(); ++i) {
        double ge = geometricError * (1.0 - i * 0.25); // LOD误差递减
        // 提取文件名（去掉目录路径）
        std::string filename = std::filesystem::path(meta->lodFiles[i]).filename().string();
        lodLevels.emplace_back(filename, ge);
    }

    // 构建层级结构
    tileset::Tile* currentParent = &rootTile;
    for (size_t i = 0; i < lodLevels.size(); ++i) {
        const auto& [content, geError] = lodLevels[i];

        tileset::Tile lodTile;
        lodTile.boundingVolume = rootTile.boundingVolume;
        lodTile.geometricError = geError;
        lodTile.refine = "REPLACE";
        lodTile.setContent(content);

        // 如果不是最后一个LOD，需要继续添加子节点
        if (i < lodLevels.size() - 1) {
            currentParent->addChild(std::move(lodTile));
            currentParent = &currentParent->children.back();
        } else {
            // 最后一个LOD，直接添加为叶子节点
            currentParent->addChild(std::move(lodTile));
        }
    }

    leafTileset.root = std::move(rootTile);
    leafTileset.updateGeometricError();

    // 写入文件
    std::filesystem::path tilesetDir = std::filesystem::path(outputPath) / meta->getTileDirectory();
    std::filesystem::create_directories(tilesetDir);
    std::filesystem::path tilesetPath = tilesetDir / "tileset.json";

    tileset::TilesetWriter writer;
    return writer.writeToFile(leafTileset, tilesetPath.string());
}

tileset::Box FBXTilesetAdapter::convertBoundingBox(const osg::BoundingBoxd& bbox) const {
    // 计算中心点
    double centerX = (bbox.xMin() + bbox.xMax()) * 0.5;
    double centerY = (bbox.yMin() + bbox.yMax()) * 0.5;
    double centerZ = (bbox.zMin() + bbox.zMax()) * 0.5;

    // 计算半轴长度
    double halfX = (bbox.xMax() - bbox.xMin()) * 0.5 * config_.boundingVolumeScale;
    double halfY = (bbox.yMax() - bbox.yMin()) * 0.5 * config_.boundingVolumeScale;
    double halfZ = (bbox.zMax() - bbox.zMin()) * 0.5 * config_.boundingVolumeScale;

    // 创建Box（中心点 + 半轴长度）
    return tileset::Box::fromCenterAndHalfLengths(centerX, centerY, centerZ, halfX, halfY, halfZ);
}

double FBXTilesetAdapter::computeGeometricError(const osg::BoundingBoxd& bbox) const {
    // 基于包围盒对角线计算几何误差
    double dx = bbox.xMax() - bbox.xMin();
    double dy = bbox.yMax() - bbox.yMin();
    double dz = bbox.zMax() - bbox.zMin();

    double diagonal = std::sqrt(dx * dx + dy * dy + dz * dz);
    return diagonal / 20.0 * config_.geometricErrorScale;
}

tileset::TransformMatrix FBXTilesetAdapter::createRootTransform() const {
    // 使用CoordinateTransformer计算ENU->ECEF变换矩阵
    glm::dmat4 enuToEcef = coords::CoordinateTransformer::CalcEnuToEcefMatrix(
        config_.centerLongitude, config_.centerLatitude, config_.centerHeight);

    // 转换为tileset::TransformMatrix (std::array<double, 16>)
    tileset::TransformMatrix matrix;
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            matrix[c * 4 + r] = enuToEcef[c][r];
        }
    }
    return matrix;
}

FBXTileMetaPtr FBXTilesetAdapter::findRootNode(const FBXTileMetaMap& allMetas) const {
    for (const auto& [key, meta] : allMetas) {
        if (meta->isRoot()) {
            return meta;
        }
    }
    // 如果没有找到根节点，返回第一个节点
    if (!allMetas.empty()) {
        return allMetas.begin()->second;
    }
    return nullptr;
}

tileset::Tile FBXTilesetAdapter::buildTileRecursive(
    const FBXTileMetaPtr& meta,
    const FBXTileMetaMap& allMetas) const {

    tileset::Tile tile;

    // 设置包围体
    tile.boundingVolume = convertBoundingBox(meta->bbox);

    // 设置几何误差
    tile.geometricError = meta->geometricError > 0.0
                              ? meta->geometricError
                              : computeGeometricError(meta->bbox);

    // 设置细化策略
    tile.refine = "REPLACE";

    // 设置内容
    if (meta->content.hasContent) {
        tile.setContent(meta->content.uri);
    }

    // 递归构建子节点
    if (!meta->isLeaf && !meta->childrenKeys.empty()) {
        for (uint64_t childKey : meta->childrenKeys) {
            auto it = allMetas.find(childKey);
            if (it != allMetas.end()) {
                tileset::Tile childTile = buildTileRecursive(it->second, allMetas);
                tile.addChild(std::move(childTile));
            }
        }
    }

    return tile;
}

} // namespace fbx

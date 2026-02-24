#pragma once

/**
 * @file shapefile_tileset_adapter.h
 * @brief Shapefile 业务层到 Tileset 标准层的适配器
 *
 * 该模块负责将 Shapefile 特有的业务数据结构 (shapefile::TileMeta)
 * 转换为标准的 3D Tiles 数据结构 (tileset::Tile)。
 *
 * 主要职责：
 * 1. 坐标系统转换：WGS84 (度) → ENU (米)
 * 2. 包围体转换：TileBBox → tileset::Box
 * 3. 层次结构构建：四叉树 → Tile 嵌套结构
 * 4. Transform 矩阵生成 (根节点)
 */

#include "shapefile_tile.h"
#include "../tileset/tileset_types.h"
#include "../tileset/bounding_volume.h"
#include "../tileset/transform.h"
#include "../coordinate_transformer.h"

#include <extern.h>  // for degree2rad, longti_to_meter, lati_to_meter

#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

namespace shapefile {

/**
 * @brief 适配器配置选项
 */
struct AdapterConfig {
    // 包围盒扩展系数 (防止浮点误差导致的裁剪)
    double boundingVolumeScaleFactor = 2.0;

    // 几何误差缩放系数
    double geometricErrorScale = 0.5;

    // 是否对根节点应用 ENU->ECEF 变换
    bool applyRootTransform = true;

    // 最小层级 (该层级及以下的瓦片为根节点)
    int minZRoot = 0;
};

/**
 * @brief Shapefile 到 Tileset 的适配器类
 */
class ShapefileTilesetAdapter {
public:
    /**
     * @brief 构造函数
     *
     * @param globalCenterLon 全局中心经度 (度)
     * @param globalCenterLat 全局中心纬度 (度)
     * @param config 适配器配置
     */
    ShapefileTilesetAdapter(double globalCenterLon,
                            double globalCenterLat,
                            const AdapterConfig& config = {});

    /**
     * @brief 将单个 TileMeta 转换为 tileset::Tile
     *
     * @param meta Shapefile 瓦片元数据
     * @return 标准 3D Tiles 瓦片对象
     */
    tileset::Tile convertTile(const TileMeta& meta) const;

    /**
     * @brief 构建完整的 Tileset
     *
     * 递归地将整个四叉树结构转换为 3D Tiles 层次结构
     *
     * @param rootMeta 根节点元数据
     * @param allMetas 所有节点的元数据映射表
     * @return 完整的 Tileset 对象
     */
    tileset::Tileset buildTileset(
        const TileMeta& rootMeta,
        const std::unordered_map<uint64_t, TileMeta>& allMetas) const;

    /**
     * @brief 计算几何误差
     *
     * 基于包围盒的对角线长度计算几何误差
     *
     * @param bbox Shapefile 包围盒
     * @return 几何误差值
     */
    double computeGeometricError(const TileBBox& bbox) const;

    /**
     * @brief 将 TileBBox 转换为 tileset::Box (ENU 坐标系)
     *
     * @param bbox Shapefile 包围盒 (WGS84 度)
     * @return 3D Tiles Box 包围体 (ENU 米)
     */
    tileset::Box convertBoundingBox(const TileBBox& bbox) const;

    /**
     * @brief 生成根节点的 ENU->ECEF 变换矩阵
     *
     * @param centerLon 中心经度 (度)
     * @param centerLat 中心纬度 (度)
     * @param minHeight 最小高度 (米)
     * @return 4x4 变换矩阵
     */
    static tileset::TransformMatrix createRootTransform(double centerLon,
                                                         double centerLat,
                                                         double minHeight);

private:
    double globalCenterLon_;  // 全局中心经度 (度)
    double globalCenterLat_;  // 全局中心纬度 (度)
    AdapterConfig config_;

    // 递归构建子节点
    void buildChildren(tileset::Tile& parentTile,
                       const TileMeta& parentMeta,
                       const std::unordered_map<uint64_t, TileMeta>& allMetas) const;

    // 计算 ENU 坐标偏移
    void computeEnuOffset(double lon, double lat,
                          double& offsetX, double& offsetY) const;
};

// ============================================================================
// 内联实现
// ============================================================================

inline ShapefileTilesetAdapter::ShapefileTilesetAdapter(double globalCenterLon,
                                                         double globalCenterLat,
                                                         const AdapterConfig& config)
    : globalCenterLon_(globalCenterLon)
    , globalCenterLat_(globalCenterLat)
    , config_(config) {}

inline void ShapefileTilesetAdapter::computeEnuOffset(double lon, double lat,
                                                       double& offsetX, double& offsetY) const {
    // 计算相对于全局中心的 ENU 偏移 (米)
    offsetX = longti_to_meter(degree2rad(lon - globalCenterLon_),
                              degree2rad(globalCenterLat_));
    offsetY = lati_to_meter(degree2rad(lat - globalCenterLat_));
}

inline tileset::Box ShapefileTilesetAdapter::convertBoundingBox(const TileBBox& bbox) const {
    // 1. 计算中心点经纬度
    double centerLon = bbox.centerLon();
    double centerLat = bbox.centerLat();

    // 2. 计算经纬度跨度 (弧度)
    double lonRadSpan = degree2rad(bbox.widthDeg());
    double latRadSpan = degree2rad(bbox.heightDeg());

    // 3. 转换为米 (ENU 坐标系)
    double halfW = longti_to_meter(lonRadSpan * 0.5, degree2rad(centerLat)) *
                   1.05 * config_.boundingVolumeScaleFactor;
    double halfH = lati_to_meter(latRadSpan * 0.5) *
                   1.05 * config_.boundingVolumeScaleFactor;
    double halfZ = (bbox.maxHeight - bbox.minHeight) * 0.5 * config_.boundingVolumeScaleFactor;

    // 4. 计算相对于全局中心的 ENU 偏移
    double offsetX, offsetY;
    computeEnuOffset(centerLon, centerLat, offsetX, offsetY);

    // 5. 创建 Box (中心点 + 半轴长度)
    return tileset::Box::fromCenterAndHalfLengths(
        offsetX, offsetY, halfZ, halfW, halfH, halfZ);
}

inline double ShapefileTilesetAdapter::computeGeometricError(const TileBBox& bbox) const {
    // 基于包围盒对角线计算几何误差
    double spanX = bbox.widthDeg();
    double spanY = bbox.heightDeg();
    double spanZ = bbox.maxHeight - bbox.minHeight;

    // 将经纬度跨度转换为米 (近似)
    double centerLat = bbox.centerLat();
    double meterX = longti_to_meter(degree2rad(spanX), degree2rad(centerLat));
    double meterY = lati_to_meter(degree2rad(spanY));

    double maxSpan = std::max({meterX, meterY, spanZ});
    if (maxSpan <= 0.0) {
        return 0.0;
    }
    return maxSpan / 20.0 * config_.geometricErrorScale;
}

inline tileset::TransformMatrix ShapefileTilesetAdapter::createRootTransform(
    double centerLon, double centerLat, double minHeight) {
    // 使用 CoordinateTransformer 计算 ENU->ECEF 变换矩阵
    glm::dmat4 enuToEcef = coords::CoordinateTransformer::CalcEnuToEcefMatrix(
        centerLon, centerLat, minHeight);

    // 转换为 tileset::TransformMatrix (std::array<double, 16>)
    tileset::TransformMatrix matrix;
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            matrix[c * 4 + r] = enuToEcef[c][r];
        }
    }
    return matrix;
}

inline tileset::Tile ShapefileTilesetAdapter::convertTile(const TileMeta& meta) const {
    tileset::Tile tile;

    // 1. 设置包围体 (ENU 坐标系)
    tile.boundingVolume = convertBoundingBox(meta.bbox);

    // 2. 设置几何误差
    tile.geometricError = meta.geometric_error > 0.0
                              ? meta.geometric_error
                              : computeGeometricError(meta.bbox);

    // 3. 设置细化策略
    tile.refine = "REPLACE";

    // 如果是根节点，添加 ENU->ECEF 变换矩阵
    if (config_.applyRootTransform && meta.z <= config_.minZRoot) {
        double centerLon = meta.bbox.centerLon();
        double centerLat = meta.bbox.centerLat();
        double minHeight = meta.bbox.minHeight;

        tileset::TransformMatrix transform = createRootTransform(
            centerLon, centerLat, minHeight);
        tile.setTransform(transform);
    }

    // 5. 如果是叶子节点，设置内容 URI
    if (meta.is_leaf && !meta.tileset_rel.empty()) {
        tile.setContent(meta.tileset_rel);
    }

    return tile;
}

inline void ShapefileTilesetAdapter::buildChildren(
    tileset::Tile& parentTile,
    const TileMeta& parentMeta,
    const std::unordered_map<uint64_t, TileMeta>& allMetas) const {

    for (uint64_t childKey : parentMeta.children_keys) {
        auto it = allMetas.find(childKey);
        if (it == allMetas.end()) {
            continue;
        }

        const TileMeta& childMeta = it->second;
        tileset::Tile childTile = convertTile(childMeta);

        // 递归构建子节点的子节点
        if (!childMeta.children_keys.empty()) {
            buildChildren(childTile, childMeta, allMetas);
        }

        parentTile.addChild(std::move(childTile));
    }
}

inline tileset::Tileset ShapefileTilesetAdapter::buildTileset(
    const TileMeta& rootMeta,
    const std::unordered_map<uint64_t, TileMeta>& allMetas) const {

    // 1. 转换根节点
    tileset::Tile rootTile = convertTile(rootMeta);

    // 2. 递归构建子节点
    if (!rootMeta.children_keys.empty()) {
        buildChildren(rootTile, rootMeta, allMetas);
    }

    // 3. 创建 Tileset
    tileset::Tileset tileset(rootTile);
    tileset.setVersion("1.0");
    tileset.setGltfUpAxis("Z");

    // 4. 设置根节点的几何误差
    double rootGe = rootMeta.geometric_error > 0.0
                        ? rootMeta.geometric_error
                        : computeGeometricError(rootMeta.bbox) * 2.0;
    tileset.geometricError = rootGe;

    return tileset;
}

} // namespace shapefile

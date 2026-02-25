#pragma once

/**
 * @file common/tileset_builder.h
 * @brief 通用Tileset构建器
 *
 * 提供统一的Tileset构建功能，支持四叉树（Shapefile）和八叉树（FBX）结构
 */

#include "tile_meta.h"
#include "../tileset/tileset_types.h"
#include "../tileset/bounding_volume.h"
#include "../tileset/transform.h"
#include <unordered_map>
#include <functional>

namespace common {

/**
 * @brief Tileset构建器配置
 */
struct TilesetBuilderConfig {
    // 版本信息
    std::string version = "1.0";
    std::string gltfUpAxis = "Z";

    // 几何误差配置
    double rootGeometricErrorMultiplier = 2.0;
    double childGeometricErrorMultiplier = 0.5;

    // 包围盒扩展系数
    double boundingVolumeScale = 1.0;

    // 是否启用LOD
    bool enableLOD = false;

    // LOD级别数量
    int lodLevelCount = 1;

    // 细化策略 ("ADD" 或 "REPLACE")
    std::string refine = "REPLACE";
};

/**
 * @brief 包围盒转换函数类型
 */
using BoundingBoxConverter = std::function<tileset::Box(const BoundingBox&)>;

/**
 * @brief 几何误差计算函数类型
 */
using GeometricErrorCalculator = std::function<double(const BoundingBox&)>;

/**
 * @brief 通用Tileset构建器
 *
 * 将通用的TileMeta结构转换为标准的tileset::Tileset
 */
class TilesetBuilder {
public:
    /**
     * @brief 构造函数
     * @param config 构建器配置
     */
    explicit TilesetBuilder(const TilesetBuilderConfig& config = {});

    /**
     * @brief 构建完整的Tileset
     *
     * @param rootMeta 根节点元数据
     * @param allMetas 所有节点的元数据映射表
     * @param bboxConverter 包围盒转换函数
     * @param geCalculator 几何误差计算函数
     * @return 完整的Tileset对象
     */
    tileset::Tileset buildTileset(
        const TileMetaPtr& rootMeta,
        const TileMetaMap& allMetas,
        const BoundingBoxConverter& bboxConverter,
        const GeometricErrorCalculator& geCalculator) const;

    /**
     * @brief 构建单个Tile
     *
     * @param meta 瓦片元数据
     * @param allMetas 所有节点的元数据映射表
     * @param bboxConverter 包围盒转换函数
     * @param geCalculator 几何误差计算函数
     * @return Tile对象
     */
    tileset::Tile buildTile(
        const TileMetaPtr& meta,
        const TileMetaMap& allMetas,
        const BoundingBoxConverter& bboxConverter,
        const GeometricErrorCalculator& geCalculator) const;

    /**
     * @brief 创建根节点变换矩阵（ENU到ECEF）
     *
     * @param centerLon 中心经度（度）
     * @param centerLat 中心纬度（度）
     * @param centerHeight 中心高度（米）
     * @return 变换矩阵
     */
    static tileset::TransformMatrix createRootTransform(
        double centerLon, double centerLat, double centerHeight = 0.0);

protected:
    TilesetBuilderConfig config_;

private:
    // 递归构建子节点
    void buildChildren(
        tileset::Tile& parentTile,
        const TileMetaPtr& parentMeta,
        const TileMetaMap& allMetas,
        const BoundingBoxConverter& bboxConverter,
        const GeometricErrorCalculator& geCalculator) const;

    // 创建Content对象
    tileset::Content createContent(const TileMetaPtr& meta) const;
};

/**
 * @brief 四叉树Tileset构建器（Shapefile专用）
 */
class QuadtreeTilesetBuilder : public TilesetBuilder {
public:
    /**
     * @brief 构造函数
     * @param globalCenterLon 全局中心经度
     * @param globalCenterLat 全局中心纬度
     * @param config 构建器配置
     */
    QuadtreeTilesetBuilder(double globalCenterLon,
                           double globalCenterLat,
                           const TilesetBuilderConfig& config = {});

    /**
     * @brief 构建Tileset（使用默认的包围盒转换和几何误差计算）
     *
     * @param rootMeta 根节点元数据
     * @param allMetas 所有节点的元数据映射表
     * @return 完整的Tileset对象
     */
    tileset::Tileset buildTileset(
        const TileMetaPtr& rootMeta,
        const TileMetaMap& allMetas) const;

    /**
     * @brief 将经纬度包围盒转换为ENU坐标系的Box
     *
     * @param bbox 包围盒（WGS84度）
     * @return tileset::Box（ENU米）
     */
    tileset::Box convertBoundingBox(const BoundingBox& bbox) const;

    /**
     * @brief 计算几何误差（基于包围盒对角线）
     *
     * @param bbox 包围盒
     * @return 几何误差值
     */
    double computeGeometricError(const BoundingBox& bbox) const;

protected:
    double globalCenterLon_;
    double globalCenterLat_;

private:
    // 计算ENU坐标偏移
    void computeEnuOffset(double lon, double lat,
                          double& offsetX, double& offsetY) const;
};

/**
 * @brief 八叉树Tileset构建器（FBX专用）
 */
class OctreeTilesetBuilder : public TilesetBuilder {
public:
    /**
     * @brief 构造函数
     * @param config 构建器配置
     */
    explicit OctreeTilesetBuilder(const TilesetBuilderConfig& config = {});

    /**
     * @brief 构建Tileset（使用默认的包围盒转换和几何误差计算）
     *
     * @param rootMeta 根节点元数据
     * @param allMetas 所有节点的元数据映射表
     * @return 完整的Tileset对象
     */
    tileset::Tileset buildTileset(
        const TileMetaPtr& rootMeta,
        const TileMetaMap& allMetas) const;

    /**
     * @brief 将本地坐标包围盒转换为Box
     *
     * @param bbox 包围盒（本地坐标）
     * @return tileset::Box
     */
    tileset::Box convertBoundingBox(const BoundingBox& bbox) const;

    /**
     * @brief 计算几何误差
     *
     * @param bbox 包围盒
     * @return 几何误差值
     */
    double computeGeometricError(const BoundingBox& bbox) const;
};

} // namespace common

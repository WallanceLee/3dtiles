#pragma once

/**
 * @file fbx/fbx_tileset_adapter.h
 * @brief FBX Tileset适配器
 *
 * 整合TilesetBuilder生成最终的tileset.json
 * 复用common::TilesetBuilder，与shapefile保持一致
 */

#include "fbx_tile_meta.h"
#include "fbx_tile_meta_converter.h"
#include "../common/tileset_builder.h"
#include "../tileset/tileset_types.h"
#include "../tileset/bounding_volume.h"
#include "../coordinate_transformer.h"
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include <filesystem>

namespace fbx {

/**
 * @brief FBX Tileset适配器配置
 */
struct FBXTilesetAdapterConfig {
    double centerLongitude = 0.0;     // 中心经度（度）
    double centerLatitude = 0.0;      // 中心纬度（度）
    double centerHeight = 0.0;        // 中心高度（米）
    double boundingVolumeScale = 1.0; // 包围盒扩展系数
    double geometricErrorScale = 0.5; // 几何误差缩放系数
    bool enableLOD = false;           // 是否启用LOD
    int lodLevelCount = 3;            // LOD级别数量

    /**
     * @brief 转换为TilesetBuilderConfig
     */
    common::TilesetBuilderConfig toBuilderConfig() const {
        common::TilesetBuilderConfig config;
        config.boundingVolumeScale = boundingVolumeScale;
        config.childGeometricErrorMultiplier = geometricErrorScale;
        config.enableLOD = enableLOD;
        config.lodLevelCount = lodLevelCount;
        config.refine = "REPLACE";
        return config;
    }
};

/**
 * @brief FBX Tileset适配器
 *
 * 整合TilesetBuilder生成最终的tileset.json
 */
class FBXTilesetAdapter {
public:
    explicit FBXTilesetAdapter(const FBXTilesetAdapterConfig& config);

    /**
     * @brief 构建并写入Tileset
     *
     * @param allMetas 所有节点的元数据映射表
     * @param outputPath 输出路径
     * @return 是否成功
     */
    bool buildAndWriteTileset(
        const FBXTileMetaMap& allMetas,
        const std::string& outputPath);

    /**
     * @brief 为叶子节点生成子tileset（包含LOD层级）
     *
     * @param meta 叶子节点元数据
     * @param outputPath 输出根目录
     * @return 是否成功
     */
    bool generateLeafTileset(
        const FBXTileMetaPtr& meta,
        const std::string& outputPath);

private:
    FBXTilesetAdapterConfig config_;

    /**
     * @brief 将FBX包围盒转换为tileset::Box
     */
    tileset::Box convertBoundingBox(const osg::BoundingBoxd& bbox) const;

    /**
     * @brief 计算几何误差
     */
    double computeGeometricError(const osg::BoundingBoxd& bbox) const;

    /**
     * @brief 创建根节点变换矩阵（ENU到ECEF）
     */
    tileset::TransformMatrix createRootTransform() const;

    /**
     * @brief 查找根节点
     */
    FBXTileMetaPtr findRootNode(const FBXTileMetaMap& allMetas) const;

    /**
     * @brief 递归构建Tile层次结构
     */
    tileset::Tile buildTileRecursive(
        const FBXTileMetaPtr& meta,
        const FBXTileMetaMap& allMetas) const;
};

} // namespace fbx

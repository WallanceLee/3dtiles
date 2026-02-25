#pragma once

/**
 * @file fbx/fbx_tile_meta_converter.h
 * @brief FBX瓦片元数据转换器
 *
 * 将OctreeStrategy节点转换为TileMeta结构，复用common::TilesetBuilder
 */

#include "fbx_tile_meta.h"
#include "fbx_spatial_item_adapter.h"
#include "../spatial/strategy/octree_strategy.h"
#include "../b3dm/b3dm_generator.h"
#include <memory>
#include <unordered_map>

namespace fbx {

/**
 * @brief FBX瓦片元数据转换器
 *
 * 将OctreeStrategy节点转换为TileMeta结构，支持LOD生成
 */
class FBXTileMetaConverter {
public:
    /**
     * @brief 转换配置
     */
    struct Config {
        std::string outputPath;           // 输出根目录
        bool enableLOD = false;           // 是否启用LOD
        std::vector<LODLevelSettings> lodLevels; // LOD配置
        b3dm::B3DMGenerator* generator = nullptr; // B3DM生成器
    };

    /**
     * @brief 转换八叉树为TileMeta映射表
     * @param strategy 八叉树策略
     * @param config 转换配置
     * @return 根节点元数据 + 所有节点映射表
     */
    static std::pair<FBXTileMetaPtr, FBXTileMetaMap> convert(
        const spatial::strategy::OctreeStrategy& strategy,
        const Config& config);

private:
    static FBXTileMetaPtr convertNodeRecursive(
        const spatial::strategy::OctreeNode* node,
        FBXTileMetaMap& allMetas,
        const Config& config,
        int& nodeIdCounter);

    /**
     * @brief 为节点生成B3DM文件和LOD
     * @param meta 瓦片元数据
     * @param node 八叉树节点
     * @param config 转换配置
     */
    static void generateB3DMForNode(
        FBXTileMetaPtr& meta,
        const spatial::strategy::OctreeNode* node,
        const Config& config);

    /**
     * @brief 计算几何误差
     * @param bbox 包围盒
     * @return 几何误差值
     */
    static double computeGeometricError(const osg::BoundingBoxd& bbox);
};

} // namespace fbx

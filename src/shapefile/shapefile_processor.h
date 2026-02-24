#pragma once

/**
 * @file shapefile_processor.h
 * @brief Shapefile 处理器 - 阶段4完整实现
 *
 * 整合所有新框架组件：
 * - ShapefileDataPool (数据加载)
 * - QuadtreeStrategy (空间索引)
 * - B3DMContentGenerator (B3DM生成)
 * - ShapefileTilesetAdapter (Tileset生成)
 */

#include "shapefile_data_pool.h"
#include "../spatial/strategy/quadtree_strategy.h"
#include "../spatial/core/slicing_strategy.h"
#include "shapefile_tile.h"
#include "../mesh_processor.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <optional>

namespace shapefile {

// 前向声明
class B3DMContentGenerator;
class ShapefileTilesetAdapter;
struct AdapterConfig;

/**
 * @brief Shapefile 处理器配置
 */
struct ShapefileProcessorConfig {
    // 输入输出
    std::string inputPath;
    std::string outputPath;

    // 高度字段
    std::string heightField;

    // 地理中心
    double centerLongitude = 0.0;
    double centerLatitude = 0.0;

    // 是否启用 LOD
    bool enableLOD = false;

    // 简化参数
    bool enableSimplification = false;
    SimplificationParams simplifyParams;

    // Draco 压缩参数
    bool enableDraco = false;
    DracoCompressionParams dracoParams;

    // 四叉树配置
    spatial::strategy::QuadtreeConfig quadtreeConfig;

    // Tileset 适配器配置
    double boundingVolumeScaleFactor = 2.0;
    double geometricErrorScale = 0.5;
    bool applyRootTransform = true;
};

/**
 * @brief 处理结果
 */
struct ProcessingResult {
    bool success = false;
    size_t featureCount = 0;
    size_t nodeCount = 0;
    size_t b3dmCount = 0;
    std::string tilesetPath;
    std::string errorMessage;
};

/**
 * @brief Shapefile 处理器
 *
 * 阶段4完整实现：完全使用新框架
 */
class ShapefileProcessor {
public:
    explicit ShapefileProcessor(const ShapefileProcessorConfig& config);
    ~ShapefileProcessor();

    /**
     * @brief 处理 Shapefile 生成 3D Tiles
     * @return 处理结果
     */
    ProcessingResult process();

private:
    ShapefileProcessorConfig config_;

    // 数据池
    std::unique_ptr<ShapefileDataPool> dataPool_;

    // 四叉树索引
    std::unique_ptr<spatial::core::SpatialIndex> quadtreeIndex_;

    // B3DM 生成器 (前向声明，避免头文件依赖)
    class B3DMGeneratorImpl;
    std::unique_ptr<B3DMGeneratorImpl> b3dmGenerator_;

    // 节点映射表（用于 Tileset 生成）
    std::unordered_map<uint64_t, TileMeta> nodeMap_;

    // ===== 处理步骤 =====

    // 1. 加载数据
    bool loadData();

    // 2. 构建四叉树索引
    bool buildSpatialIndex();

    // 3. 生成 B3DM 文件（遍历四叉树叶子节点）
    bool generateB3DMFiles();

    // 4. 生成 Tileset
    bool generateTileset();

    // ===== 辅助函数 =====

    // 从空间索引节点生成 B3DM
    std::string generateB3DMForNode(
        const spatial::core::SpatialIndexNode* node,
        const std::string& outputDir
    );

    // 构建节点映射表（用于 Tileset 生成）
    void buildNodeMap();

    // 递归收集叶子节点
    void collectLeafNodes(
        const spatial::core::SpatialIndexNode* node,
        std::vector<const spatial::core::SpatialIndexNode*>& leaves
    );

    // 递归构建节点映射表
    void buildNodeMapRecursive(
        const spatial::core::SpatialIndexNode* node,
        std::unordered_map<uint64_t, TileMeta>& nodes
    );

    // 从子节点更新父节点的高度范围
    void updateParentHeightsFromChildren();

    // 转换包围盒
    static TileBBox convertBounds(const spatial::core::SpatialBounds<double, 2>& bounds);

    // 计算几何误差
    static double calculateGeometricError(const TileBBox& bbox);

    // 为叶子节点生成 LOD tileset
    bool generateLeafTileset(const TileMeta& meta);
};

} // namespace shapefile

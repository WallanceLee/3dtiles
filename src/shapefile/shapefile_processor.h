#pragma once

/**
 * @file shapefile_processor.h
 * @brief Shapefile 处理器 - 步骤3修改：支持外部 TilesetBuilder
 *
 * 修改内容：
 * - 步骤1：添加 SetDataSource 方法，允许外部注入数据源
 * - 步骤2：添加 SetSpatialIndex 方法，允许外部注入空间索引
 * - 步骤3：添加 SetTilesetBuilder 方法，允许外部注入 TilesetBuilder
 * - 保持原有接口不变，确保向后兼容
 */

#include "shapefile_data_pool.h"
#include "../spatial/strategy/quadtree_strategy.h"
#include "../spatial/core/slicing_strategy.h"
#include "shapefile_tile.h"
#include "../common/mesh_processor.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <optional>

// 前向声明 - 避免循环依赖
namespace pipeline {
class DataSource;
class ISpatialIndex;
class ITilesetBuilder;
}

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
 * 步骤3修改：支持外部数据源、空间索引和 TilesetBuilder
 */
class ShapefileProcessor {
public:
    explicit ShapefileProcessor(const ShapefileProcessorConfig& config);
    ~ShapefileProcessor();

    /**
     * @brief 设置外部数据源（步骤1新增）
     * @param dataSource 外部数据源，如果为 nullptr 则内部加载
     *
     * 注意：外部数据源的生命周期必须超过 ShapefileProcessor 的生命周期
     */
    void SetDataSource(pipeline::DataSource* dataSource);

    /**
     * @brief 设置外部空间索引（步骤2新增）
     * @param spatialIndex 外部空间索引，如果为 nullptr 则内部构建
     *
     * 注意：外部空间索引的生命周期必须超过 ShapefileProcessor 的生命周期
     */
    void SetSpatialIndex(pipeline::ISpatialIndex* spatialIndex);

    /**
     * @brief 设置外部 TilesetBuilder（步骤3新增）
     * @param tilesetBuilder 外部 TilesetBuilder，如果为 nullptr 则内部创建
     *
     * 注意：外部 TilesetBuilder 的生命周期必须超过 ShapefileProcessor 的生命周期
     */
    void SetTilesetBuilder(pipeline::ITilesetBuilder* tilesetBuilder);

    /**
     * @brief 处理 Shapefile 生成 3D Tiles
     * @return 处理结果
     */
    ProcessingResult process();

private:
    ShapefileProcessorConfig config_;

    // 数据池（内部加载时使用 - 向后兼容）
    std::unique_ptr<ShapefileDataPool> dataPool_;

    // 数据源（内部创建时使用）
    std::unique_ptr<pipeline::DataSource> dataSource_;

    // 外部数据源（步骤1新增）
    pipeline::DataSource* externalDataSource_ = nullptr;

    // 四叉树索引（内部构建时使用 - 向后兼容）
    std::unique_ptr<spatial::core::SpatialIndex> quadtreeIndex_;

    // 空间索引（内部创建时使用）
    std::unique_ptr<pipeline::ISpatialIndex> spatialIndex_;

    // 外部空间索引（步骤2新增）
    pipeline::ISpatialIndex* externalSpatialIndex_ = nullptr;

    // TilesetBuilder（内部创建时使用）
    std::unique_ptr<pipeline::ITilesetBuilder> tilesetBuilder_;

    // 外部 TilesetBuilder（步骤3新增）
    pipeline::ITilesetBuilder* externalTilesetBuilder_ = nullptr;

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

    // 获取当前使用的数据源（步骤1新增）
    [[nodiscard]] const ShapefileDataPool* GetCurrentDataPool() const;

    // 获取当前使用的数据源（新接口，步骤1补充）
    [[nodiscard]] pipeline::DataSource* GetCurrentDataSource();

    // 获取当前使用的空间索引根节点（步骤2新增）
    [[nodiscard]] const spatial::core::SpatialIndexNode* GetCurrentRootNode() const;

    // 获取当前使用的空间索引（新接口，步骤2补充）
    [[nodiscard]] pipeline::ISpatialIndex* GetCurrentSpatialIndex();

    // 获取当前使用的 TilesetBuilder（步骤3新增）
    [[nodiscard]] pipeline::ITilesetBuilder* GetCurrentTilesetBuilder();
};

} // namespace shapefile

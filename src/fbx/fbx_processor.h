#pragma once

/**
 * @file FBXPipeline.h
 * @brief FBX Pipeline - 步骤3修改：支持外部 TilesetBuilder
 *
 * 修改内容：
 * - 步骤1：添加 SetDataSource 方法，允许外部注入数据源
 * - 步骤2：添加 SetSpatialIndex 方法，允许外部注入空间索引
 * - 步骤3：添加 SetTilesetBuilder 方法，允许外部注入 TilesetBuilder
 * - 保持原有接口不变，确保向后兼容
 */

#include "fbx/core/fbx.h"
#include "fbx/fbx_spatial_item_adapter.h"
#include "fbx/fbx_tile_meta.h"
#include "spatial/strategy/octree_strategy.h"
#include "b3dm/b3dm_generator.h"
#include "pipeline/data_source.h"
#include "pipeline/spatial_index.h"
#include "pipeline/tileset_builder.h"
#include <string>
#include <vector>
#include <memory>
#include <osg/Matrixd>
#include <osg/BoundingBox>
#include <osg/Geometry>

// Forward declarations
namespace tinygltf {
    class Model;
}

struct PipelineSettings {
    std::string inputPath;
    std::string outputPath;
    int maxDepth = 5;
    int maxItemsPerTile = 1000;

    // Optimization flags
    bool enableSimplify = false;
    bool enableDraco = false;
    bool enableTextureCompress = false; // KTX2
    bool enableLOD = false; // Enable Hierarchical LOD generation
    bool enableUnlit = false; // Enable KHR_materials_unlit
    std::vector<float> lodRatios = {1.0f, 0.5f, 0.25f}; // Default LOD ratios (Fine to Coarse)

    // Geolocation (Origin)
    double longitude = 0.0;
    double latitude = 0.0;
    double height = 0.0;

    // Geometric error scale (multiplier applied to boundingVolume diagonal)
    double geScale = 0.5; // Adjusted for better LOD switching with SSE=16
};

class FBXPipeline {
public:
    explicit FBXPipeline(const PipelineSettings& settings);
    ~FBXPipeline() = default;

    /**
     * @brief 设置外部数据源（步骤1新增）
     * @param dataSource 外部数据源，如果为 nullptr 则内部加载
     *
     * 注意：外部数据源的生命周期必须超过 FBXPipeline 的生命周期
     */
    void SetDataSource(pipeline::DataSource* dataSource);

    /**
     * @brief 设置外部空间索引（步骤2新增）
     * @param spatialIndex 外部空间索引，如果为 nullptr 则内部构建
     *
     * 注意：外部空间索引的生命周期必须超过 FBXPipeline 的生命周期
     */
    void SetSpatialIndex(pipeline::ISpatialIndex* spatialIndex);

    /**
     * @brief 设置外部 TilesetBuilder（步骤3新增）
     * @param tilesetBuilder 外部 TilesetBuilder，如果为 nullptr 则内部创建
     *
     * 注意：外部 TilesetBuilder 的生命周期必须超过 FBXPipeline 的生命周期
     */
    void SetTilesetBuilder(pipeline::ITilesetBuilder* tilesetBuilder);

    void run();

private:
    PipelineSettings settings;
    std::unique_ptr<FBXLoader> loader_;

    // 数据源（内部创建时使用）
    std::unique_ptr<pipeline::DataSource> dataSource_;

    // 外部数据源（步骤1新增）
    pipeline::DataSource* externalDataSource_ = nullptr;

    // 空间索引（内部创建时使用）
    std::unique_ptr<pipeline::ISpatialIndex> spatialIndex_;

    // 外部空间索引（步骤2新增）
    pipeline::ISpatialIndex* externalSpatialIndex_ = nullptr;

    // TilesetBuilder（内部创建时使用）
    std::unique_ptr<pipeline::ITilesetBuilder> tilesetBuilder_;

    // 外部 TilesetBuilder（步骤3新增）
    pipeline::ITilesetBuilder* externalTilesetBuilder_ = nullptr;

    // 阶段1：空间对象适配器列表
    fbx::FBXSpatialItemList spatialItems_;

    // Process OctreeNode directly to generate tile metadata
    fbx::FBXTileMetaPtr processOctreeNode(
        const spatial::strategy::OctreeNode* node,
        const std::string& parentPath,
        const std::string& treePath,
        b3dm::B3DMGenerator& generator,
        const std::vector<LODLevelSettings>& lodLevels,
        fbx::FBXTileMetaMap& allMetas,
        int& nodeIdCounter
    );

    // 获取当前使用的空间项列表（步骤1新增）
    [[nodiscard]] const fbx::FBXSpatialItemList& GetCurrentSpatialItems() const {
        return spatialItems_;
    }

    // 加载数据（步骤1新增：支持外部数据源）
    bool loadData();

    // 获取当前使用的数据源（新接口，步骤1补充）
    [[nodiscard]] pipeline::DataSource* GetCurrentDataSource();

    // 获取当前使用的空间索引根节点（步骤2新增）
    [[nodiscard]] const spatial::strategy::OctreeNode* GetCurrentRootNode() const;

    // 获取当前使用的空间索引（新接口，步骤2补充）
    [[nodiscard]] pipeline::ISpatialIndex* GetCurrentSpatialIndex();

    // 获取当前使用的 TilesetBuilder（步骤3新增）
    [[nodiscard]] pipeline::ITilesetBuilder* GetCurrentTilesetBuilder();
};

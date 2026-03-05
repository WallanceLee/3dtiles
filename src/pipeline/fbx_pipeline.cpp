#include "fbx_pipeline.h"
#include "adapters/fbx/fbx_data_source.h"
#include "adapters/spatial/octree_index.h"
#include "adapters/tileset/fbx_tileset_builder.h"
#include "data_source.h"
#include "spatial_index.h"
#include "tileset_builder.h"
#include "fbx/fbx_processor.h"
#include <iostream>
#include <filesystem>

namespace pipeline {

// ============================================================
// FBXComponentFactory 实现
// ============================================================

std::unique_ptr<DataSource> FBXComponentFactory::CreateDataSource() {
    return DataSourceFactory::Instance().Create("fbx");
}

std::unique_ptr<ISpatialIndex> FBXComponentFactory::CreateSpatialIndex() {
    return SpatialIndexFactory::Instance().Create("octree");
}

std::unique_ptr<ITilesetBuilder> FBXComponentFactory::CreateTilesetBuilder() {
    return TilesetBuilderFactory::Instance().Create("fbx");
}

// ============================================================
// FBXPipeline 实现
// ============================================================

ConversionResult FBXPipeline::Convert(const ConversionParams& params) {
    ConversionResult result;
    result.success = false;

    ReportStart();

    // 复用现有 FBXPipeline 实现（完全复用）
    // 使用新的参数结构：options 和 spatial_config
    PipelineSettings settings;
    settings.inputPath = params.input_path;
    settings.outputPath = params.output_path;
    settings.maxDepth = params.spatial_config.max_depth;
    settings.maxItemsPerTile = static_cast<int>(params.spatial_config.max_items_per_node);
    settings.enableSimplify = params.options.enable_simplify;
    settings.enableDraco = params.options.enable_draco;
    settings.enableTextureCompress = params.options.enable_texture_compress;
    settings.enableLOD = params.options.enable_lod;
    settings.enableUnlit = params.options.enable_unlit;

    // 从 specific 参数获取 FBX 特定配置
    if (const auto* fbx_params = params.GetSpecific<FBXParams>()) {
        settings.longitude = fbx_params->longitude;
        settings.latitude = fbx_params->latitude;
        settings.height = fbx_params->height;
    } else {
        // 向后兼容：使用废弃的字段
        settings.longitude = params.longitude;
        settings.latitude = params.latitude;
        settings.height = params.height;
    }
    settings.geScale = 0.5;

    // 创建并运行管道
    ::FBXPipeline pipeline(settings);

    // 注入抽象接口组件（如果已设置）
    if (auto* dataSource = GetCurrentDataSource()) {
        pipeline.SetDataSource(dataSource);
    }

    if (auto* spatialIndex = GetCurrentSpatialIndex()) {
        pipeline.SetSpatialIndex(spatialIndex);
    }

    if (auto* tilesetBuilder = GetCurrentTilesetBuilder()) {
        pipeline.SetTilesetBuilder(tilesetBuilder);
    }

    ReportProcessing();

    pipeline.run();

    ReportCompletion();

    // 检查输出
    std::filesystem::path tilesetPath = std::filesystem::path(params.output_path) / "tileset.json";
    if (std::filesystem::exists(tilesetPath)) {
        result.success = true;
        result.tileset_path = tilesetPath.string();

        // 统计 B3DM 文件
        int b3dmCount = 0;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(params.output_path)) {
            if (entry.path().extension() == ".b3dm") {
                b3dmCount++;
            }
        }
        result.b3dm_count = b3dmCount;

        std::cout << "[FBXPipeline] Success: " << result.b3dm_count << " B3DM files generated" << std::endl;
    } else {
        result.error_message = "tileset.json not generated";
        std::cerr << "[FBXPipeline] Failed: " << result.error_message << std::endl;
    }

    return result;
}

} // namespace pipeline

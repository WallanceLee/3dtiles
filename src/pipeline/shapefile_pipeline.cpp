#include "shapefile_pipeline.h"
#include "adapters/shapefile/shapefile_data_source.h"
#include "adapters/spatial/quadtree_index.h"
#include "adapters/tileset/shapefile_tileset_builder.h"
#include "../shapefile/shapefile_processor.h"
#include "../shape.h"
#include <ogrsf_frmts.h>
#include <iostream>
#include <filesystem>

namespace pipeline {

ShapefilePipeline::ShapefilePipeline() = default;

void ShapefilePipeline::SetDataSource(std::unique_ptr<DataSource> dataSource) {
    externalDataSource_ = dataSource.get();
    dataSource_ = std::move(dataSource);
}

void ShapefilePipeline::SetSpatialIndex(std::unique_ptr<ISpatialIndex> spatialIndex) {
    externalSpatialIndex_ = spatialIndex.get();
    spatialIndex_ = std::move(spatialIndex);
}

void ShapefilePipeline::SetTilesetBuilder(std::unique_ptr<ITilesetBuilder> tilesetBuilder) {
    externalTilesetBuilder_ = tilesetBuilder.get();
    tilesetBuilder_ = std::move(tilesetBuilder);
}

void ShapefilePipeline::SetProgressCallback(ProgressCallback callback) {
    progressCallback_ = std::move(callback);
}

DataSource* ShapefilePipeline::GetCurrentDataSource() {
    if (externalDataSource_) {
        return externalDataSource_;
    }
    if (!dataSource_) {
        dataSource_ = DataSourceFactory::Instance().Create("shapefile");
    }
    return dataSource_.get();
}

ISpatialIndex* ShapefilePipeline::GetCurrentSpatialIndex() {
    if (externalSpatialIndex_) {
        return externalSpatialIndex_;
    }
    if (!spatialIndex_) {
        spatialIndex_ = SpatialIndexFactory::Instance().Create("quadtree");
    }
    return spatialIndex_.get();
}

ITilesetBuilder* ShapefilePipeline::GetCurrentTilesetBuilder() {
    if (externalTilesetBuilder_) {
        return externalTilesetBuilder_;
    }
    if (!tilesetBuilder_) {
        tilesetBuilder_ = TilesetBuilderFactory::Instance().Create("shapefile");
    }
    return tilesetBuilder_.get();
}

void ShapefilePipeline::ReportProgress(const std::string& stage, float progress) {
    if (progressCallback_) {
        progressCallback_(stage, progress);
    }
}

ConversionResult ShapefilePipeline::Convert(const ConversionParams& params) {
    ConversionResult result;
    result.success = false;

    ReportProgress("initialization", 0.0f);

    // 复用 ShapefileProcessor 处理数据（完全复用现有实现）
    // 使用新的参数结构：options 和 spatial_config
    shapefile::ShapefileProcessorConfig config;
    config.inputPath = params.input_path;
    config.outputPath = params.output_path;

    // 从 specific 参数获取 Shapefile 特定配置
    if (const auto* shp_params = params.GetSpecific<ShapefileParams>()) {
        config.heightField = shp_params->height_field;
        // 注意：ShapefileProcessorConfig 没有 layerId 字段，使用默认值 0
        (void)shp_params->layer_id;  // 避免未使用警告
    } else {
        // 向后兼容：使用废弃的字段
        config.heightField = params.height_field;
    }

    // 使用通用选项
    config.enableLOD = params.options.enable_lod;
    config.enableSimplification = params.options.enable_simplify;
    config.enableDraco = params.options.enable_draco;

    // 设置空间索引配置
    config.quadtreeConfig.maxDepth = static_cast<size_t>(params.spatial_config.max_depth);
    config.quadtreeConfig.maxItemsPerNode = params.spatial_config.max_items_per_node;
    config.quadtreeConfig.minBoundsSize = params.spatial_config.min_bounds_size;

    // 创建处理器
    shapefile::ShapefileProcessor processor(config);

    // 注入抽象接口组件（如果已设置）
    auto* dataSource = GetCurrentDataSource();
    if (dataSource) {
        processor.SetDataSource(dataSource);
    }

    auto* spatialIndex = GetCurrentSpatialIndex();
    if (spatialIndex) {
        processor.SetSpatialIndex(spatialIndex);
    }

    auto* tilesetBuilder = GetCurrentTilesetBuilder();
    if (tilesetBuilder) {
        processor.SetTilesetBuilder(tilesetBuilder);
    }

    ReportProgress("processing", 0.5f);

    // 执行处理
    auto processResult = processor.process();

    ReportProgress("completion", 1.0f);

    if (processResult.success) {
        result.success = true;
        result.node_count = static_cast<int>(processResult.nodeCount);
        result.b3dm_count = static_cast<int>(processResult.b3dmCount);
        result.tileset_path = processResult.tilesetPath;

        std::cout << "[ShapefilePipeline] Success: " << result.node_count
                  << " nodes, " << result.b3dm_count << " B3DM files" << std::endl;
    } else {
        result.error_message = processResult.errorMessage;
        std::cerr << "[ShapefilePipeline] Failed: " << result.error_message << std::endl;
    }

    return result;
}

} // namespace pipeline

// C API 实现 - 从 shp23dtile.cpp 迁移过来
extern "C" bool shp23dtile(const ShapeConversionParams* params)
{
    if (!params || !params->input_path || !params->output_path) {
        return false;
    }

    // 构建 ConversionParams
    pipeline::ConversionParams pipelineParams;
    pipelineParams.input_path = params->input_path;
    pipelineParams.output_path = params->output_path;
    pipelineParams.source_type = "shapefile";

    // 创建 Shapefile 特定参数
    auto shpSpecific = std::make_unique<pipeline::ShapefileParams>();
    shpSpecific->height_field = params->height_field ? params->height_field : "";
    shpSpecific->layer_id = params->layer_id;
    pipelineParams.specific = std::move(shpSpecific);

    // 设置通用选项
    pipelineParams.options.enable_lod = params->enable_lod;
    pipelineParams.options.enable_simplify = params->simplify_params.enable_simplification;
    pipelineParams.options.enable_draco = params->draco_compression_params.enable_compression;

    // 计算中心点
    GDALAllRegister();
    GDALDataset* poDS = (GDALDataset*)GDALOpenEx(
        params->input_path, GDAL_OF_VECTOR, NULL, NULL, NULL);
    if (poDS) {
        OGRLayer* poLayer = poDS->GetLayer(params->layer_id);
        if (poLayer) {
            OGREnvelope envelope;
            if (poLayer->GetExtent(&envelope, true) == OGRERR_NONE) {
                pipelineParams.longitude = (envelope.MinX + envelope.MaxX) * 0.5;
                pipelineParams.latitude = (envelope.MinY + envelope.MaxY) * 0.5;
            }
        }
        GDALClose(poDS);
    }

    // 创建管道并执行转换
    auto pipeline = pipeline::PipelineFactory::Instance().Create("shapefile");
    if (!pipeline) {
        return false;
    }

    auto result = pipeline->Convert(pipelineParams);
    return result.success;
}

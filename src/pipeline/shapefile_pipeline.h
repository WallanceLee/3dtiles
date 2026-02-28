#pragma once

/**
 * @file shapefile_pipeline.h
 * @brief Shapefile 转换管道 - 步骤4
 *
 * 使用步骤1-3的抽象接口实现 Shapefile 转换
 */

#include "conversion_pipeline.h"
#include <memory>

namespace pipeline {

// Shapefile 转换管道实现
class ShapefilePipeline : public IConversionPipeline {
public:
    ShapefilePipeline();
    ~ShapefilePipeline() override = default;

    // 禁止拷贝，允许移动
    ShapefilePipeline(const ShapefilePipeline&) = delete;
    ShapefilePipeline& operator=(const ShapefilePipeline&) = delete;
    ShapefilePipeline(ShapefilePipeline&&) = default;
    ShapefilePipeline& operator=(ShapefilePipeline&&) = default;

    // 设置数据源
    void SetDataSource(std::unique_ptr<DataSource> dataSource) override;

    // 设置空间索引
    void SetSpatialIndex(std::unique_ptr<ISpatialIndex> spatialIndex) override;

    // 设置 TilesetBuilder
    void SetTilesetBuilder(std::unique_ptr<ITilesetBuilder> tilesetBuilder) override;

    // 设置进度回调
    void SetProgressCallback(ProgressCallback callback) override;

    // 执行转换
    ConversionResult Convert(const ConversionParams& params) override;

private:
    // 内部组件
    std::unique_ptr<DataSource> dataSource_;
    std::unique_ptr<ISpatialIndex> spatialIndex_;
    std::unique_ptr<ITilesetBuilder> tilesetBuilder_;

    // 外部注入的组件
    DataSource* externalDataSource_ = nullptr;
    ISpatialIndex* externalSpatialIndex_ = nullptr;
    ITilesetBuilder* externalTilesetBuilder_ = nullptr;

    // 进度回调
    ProgressCallback progressCallback_;

    // 获取当前数据源
    [[nodiscard]] DataSource* GetCurrentDataSource();

    // 获取当前空间索引
    [[nodiscard]] ISpatialIndex* GetCurrentSpatialIndex();

    // 获取当前 TilesetBuilder
    [[nodiscard]] ITilesetBuilder* GetCurrentTilesetBuilder();

    // 报告进度
    void ReportProgress(const std::string& stage, float progress);
};

} // namespace pipeline

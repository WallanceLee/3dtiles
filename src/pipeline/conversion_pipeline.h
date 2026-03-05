#pragma once

/**
 * @file conversion_pipeline.h
 * @brief 统一转换管道接口 - 阶段 1 重构
 *
 * 整合步骤1-3的抽象接口，提供统一的转换管道
 *
 * 注意：此文件现在使用 conversion_params.h 中的新参数体系
 * 旧的 PipelineFactory 定义已移至 pipeline_factory.h
 */

#include "data_source.h"
#include "spatial_index.h"
#include "tileset_builder.h"
#include "conversion_params.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace pipeline {

// ============================================
// 转换管道接口
// ============================================

/**
 * @brief 统一转换管道接口
 *
 * 所有数据源转换管道都应实现此接口
 */
class IConversionPipeline {
public:
    virtual ~IConversionPipeline() = default;

    // 禁止拷贝，允许移动
    IConversionPipeline(const IConversionPipeline&) = delete;
    IConversionPipeline& operator=(const IConversionPipeline&) = delete;
    IConversionPipeline(IConversionPipeline&&) = default;
    IConversionPipeline& operator=(IConversionPipeline&&) = default;

    // 设置数据源（可选，如果不设置则内部创建）
    virtual void SetDataSource(std::unique_ptr<DataSource> dataSource) = 0;

    // 设置空间索引（可选，如果不设置则内部创建）
    virtual void SetSpatialIndex(std::unique_ptr<ISpatialIndex> spatialIndex) = 0;

    // 设置 TilesetBuilder（可选，如果不设置则内部创建）
    virtual void SetTilesetBuilder(std::unique_ptr<ITilesetBuilder> tilesetBuilder) = 0;

    // 设置进度回调
    virtual void SetProgressCallback(ProgressCallback callback) = 0;

    // 执行转换
    virtual ConversionResult Convert(const ConversionParams& params) = 0;

protected:
    IConversionPipeline() = default;
};

// 注意：为了保持与 pipeline_factory.h 的一致性，使用 shared_ptr
using ConversionPipelinePtr = std::shared_ptr<IConversionPipeline>;
using PipelineCreator = std::function<ConversionPipelinePtr()>;

// 为了保持向后兼容，保留 unique_ptr 版本
using UniquePipelinePtr = std::unique_ptr<IConversionPipeline>;

} // namespace pipeline

// ============================================
// C API 接口
// ============================================

extern "C" {
    /**
     * @brief 执行转换（使用新管道）
     * @param params 转换参数
     * @return 是否成功
     */
    bool convert_with_pipeline(const pipeline::ConversionParams* params);
}

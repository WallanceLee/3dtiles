#pragma once

/**
 * @file fbx_pipeline.h
 * @brief FBX 转换管道
 *
 * 使用 BasePipeline 模板基类消除重复代码
 */

#include "base_pipeline.h"
#include <memory>

namespace pipeline {

// 前向声明
class DataSource;
class ISpatialIndex;
class ITilesetBuilder;

/**
 * @brief FBX 管道组件工厂
 */
struct FBXComponentFactory : public PipelineComponentFactory {
    std::unique_ptr<DataSource> CreateDataSource() override;
    std::unique_ptr<ISpatialIndex> CreateSpatialIndex() override;
    std::unique_ptr<ITilesetBuilder> CreateTilesetBuilder() override;
};

/**
 * @brief FBX 转换管道实现
 *
 * 继承自 BasePipeline，只需实现 Convert() 方法
 */
class FBXPipeline : public BasePipeline<FBXPipeline, FBXComponentFactory> {
public:
    FBXPipeline() = default;
    ~FBXPipeline() override = default;

    // 执行转换
    ConversionResult Convert(const ConversionParams& params) override;
};

} // namespace pipeline

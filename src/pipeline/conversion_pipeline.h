#pragma once

/**
 * @file conversion_pipeline.h
 * @brief 统一转换管道接口 - 步骤4
 *
 * 整合步骤1-3的抽象接口，提供统一的转换管道
 */

#include "data_source.h"
#include "spatial_index.h"
#include "tileset_builder.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace pipeline {

// 转换参数
struct ConversionParams {
    std::string input_path;
    std::string output_path;
    std::string source_type;  // "shapefile" or "fbx"

    // Shapefile specific
    std::string height_field;
    int layer_id = 0;

    // FBX specific
    double longitude = 0.0;
    double latitude = 0.0;
    double height = 0.0;

    // Common options
    bool enable_lod = false;
    bool enable_draco = false;
    bool enable_texture_compress = false;
    bool enable_meshopt = false;
    bool enable_simplify = false;
    bool enable_unlit = false;

    // 空间索引配置
    int max_depth = 10;
    size_t max_items_per_node = 1000;
    double min_bounds_size = 0.01;
};

// 转换结果
struct ConversionResult {
    bool success = false;
    std::string error_message;
    int node_count = 0;
    int b3dm_count = 0;
    std::string tileset_path;
};

// 管道阶段回调
using ProgressCallback = std::function<void(const std::string& stage, float progress)>;

// 统一转换管道接口
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

using ConversionPipelinePtr = std::unique_ptr<IConversionPipeline>;
using PipelineCreator = std::function<ConversionPipelinePtr()>;

// 管道工厂 - 单例注册模式
class PipelineFactory {
public:
    [[nodiscard]] static auto Instance() noexcept -> PipelineFactory&;

    void Register(const std::string& type, PipelineCreator creator);
    [[nodiscard]] auto Create(const std::string& type) const -> ConversionPipelinePtr;
    [[nodiscard]] auto IsRegistered(const std::string& type) const noexcept -> bool;

private:
    PipelineFactory() = default;
    ~PipelineFactory() = default;

    std::unordered_map<std::string, PipelineCreator> creators_;
};

// 管道注册辅助宏
#define REGISTER_PIPELINE(TYPE, CLASS)                                         \
    namespace {                                                                \
        [[maybe_unused]] const bool _##CLASS##_registered = []() -> bool {     \
            ::pipeline::PipelineFactory::Instance().Register(                  \
                TYPE, []() -> ::pipeline::ConversionPipelinePtr {              \
                    return std::make_unique<CLASS>();                        \
                });                                                          \
            return true;                                                     \
        }();                                                                 \
    }

} // namespace pipeline

// C API 接口
extern "C" {
    // 执行转换（使用新管道）
    bool convert_with_pipeline(const pipeline::ConversionParams* params);
}

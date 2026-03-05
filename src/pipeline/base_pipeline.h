#pragma once

/**
 * @file pipeline/base_pipeline.h
 * @brief 管道模板基类
 *
 * 消除 ShapefilePipeline 和 FBXPipeline 的重复代码
 * 使用 CRTP 模式实现编译时多态
 */

#include "conversion_pipeline.h"
#include "data_source.h"
#include "spatial_index.h"
#include "tileset_builder.h"
#include <memory>
#include <string>

namespace pipeline {

/**
 * @brief 管道组件工厂接口
 *
 * 用于创建默认的数据源、空间索引和 tileset 构建器
 */
struct PipelineComponentFactory {
    virtual ~PipelineComponentFactory() = default;
    virtual std::unique_ptr<DataSource> CreateDataSource() = 0;
    virtual std::unique_ptr<ISpatialIndex> CreateSpatialIndex() = 0;
    virtual std::unique_ptr<ITilesetBuilder> CreateTilesetBuilder() = 0;
};

/**
 * @brief 管道模板基类
 *
 * @tparam Derived 派生类类型（CRTP 模式）
 * @tparam FactoryType 组件工厂类型
 *
 * 使用示例:
 * @code
 * class ShapefilePipeline : public BasePipeline<ShapefilePipeline, ShapefileComponentFactory> {
 * public:
 *     ConversionResult Convert(const ConversionParams& params) override {
 *         // 只需实现转换逻辑
 *     }
 * };
 * @endcode
 */
template<typename Derived, typename FactoryType>
class BasePipeline : public IConversionPipeline {
public:
    BasePipeline() = default;
    ~BasePipeline() override = default;

    // 禁止拷贝，允许移动
    BasePipeline(const BasePipeline&) = delete;
    BasePipeline& operator=(const BasePipeline&) = delete;
    BasePipeline(BasePipeline&&) = default;
    BasePipeline& operator=(BasePipeline&&) = default;

    // ============================================================
    // IConversionPipeline 接口实现
    // ============================================================

    void SetDataSource(std::unique_ptr<DataSource> dataSource) override {
        externalDataSource_ = dataSource.get();
        dataSource_ = std::move(dataSource);
    }

    void SetSpatialIndex(std::unique_ptr<ISpatialIndex> spatialIndex) override {
        externalSpatialIndex_ = spatialIndex.get();
        spatialIndex_ = std::move(spatialIndex);
    }

    void SetTilesetBuilder(std::unique_ptr<ITilesetBuilder> tilesetBuilder) override {
        externalTilesetBuilder_ = tilesetBuilder.get();
        tilesetBuilder_ = std::move(tilesetBuilder);
    }

    void SetProgressCallback(ProgressCallback callback) override {
        progressCallback_ = std::move(callback);
    }

    // ============================================================
    // 派生类可使用的辅助方法
    // ============================================================

protected:
    /**
     * @brief 获取当前数据源
     *
     * 优先返回外部注入的数据源，如果没有则创建默认数据源
     */
    [[nodiscard]] DataSource* GetCurrentDataSource() {
        if (externalDataSource_) {
            return externalDataSource_;
        }
        if (!dataSource_) {
            FactoryType factory;
            dataSource_ = factory.CreateDataSource();
        }
        return dataSource_.get();
    }

    /**
     * @brief 获取当前空间索引
     *
     * 优先返回外部注入的空间索引，如果没有则创建默认空间索引
     */
    [[nodiscard]] ISpatialIndex* GetCurrentSpatialIndex() {
        if (externalSpatialIndex_) {
            return externalSpatialIndex_;
        }
        if (!spatialIndex_) {
            FactoryType factory;
            spatialIndex_ = factory.CreateSpatialIndex();
        }
        return spatialIndex_.get();
    }

    /**
     * @brief 获取当前 tileset 构建器
     *
     * 优先返回外部注入的构建器，如果没有则创建默认构建器
     */
    [[nodiscard]] ITilesetBuilder* GetCurrentTilesetBuilder() {
        if (externalTilesetBuilder_) {
            return externalTilesetBuilder_;
        }
        if (!tilesetBuilder_) {
            FactoryType factory;
            tilesetBuilder_ = factory.CreateTilesetBuilder();
        }
        return tilesetBuilder_.get();
    }

    /**
     * @brief 报告进度
     * @param stage 当前阶段名称
     * @param progress 进度值 [0.0, 1.0]
     */
    void ReportProgress(const std::string& stage, float progress) {
        if (progressCallback_) {
            progressCallback_(stage, progress);
        }
    }

    /**
     * @brief 报告处理开始
     */
    void ReportStart() {
        ReportProgress("initialization", 0.0f);
    }

    /**
     * @brief 报告处理进行中
     */
    void ReportProcessing() {
        ReportProgress("processing", 0.5f);
    }

    /**
     * @brief 报告处理完成
     */
    void ReportCompletion() {
        ReportProgress("completion", 1.0f);
    }

    // ============================================================
    // 成员变量
    // ============================================================

    // 内部拥有的组件
    std::unique_ptr<DataSource> dataSource_;
    std::unique_ptr<ISpatialIndex> spatialIndex_;
    std::unique_ptr<ITilesetBuilder> tilesetBuilder_;

    // 外部注入的组件（不拥有所有权）
    DataSource* externalDataSource_ = nullptr;
    ISpatialIndex* externalSpatialIndex_ = nullptr;
    ITilesetBuilder* externalTilesetBuilder_ = nullptr;

    // 进度回调
    ProgressCallback progressCallback_;
};

/**
 * @brief 简化的管道基类
 *
 * 适用于不需要自定义组件工厂的场景
 * 派生类需要实现静态方法 CreateDataSource(), CreateSpatialIndex(), CreateTilesetBuilder()
 *
 * 使用示例:
 * @code
 * class MyPipeline : public SimplePipeline<MyPipeline> {
 * public:
 *     static std::unique_ptr<DataSource> CreateDataSource() {
 *         return std::make_unique<MyDataSource>();
 *     }
 *     static std::unique_ptr<ISpatialIndex> CreateSpatialIndex() {
 *         return std::make_unique<OctreeIndex>();
 *     }
 *     static std::unique_ptr<ITilesetBuilder> CreateTilesetBuilder() {
 *         return std::make_unique<MyTilesetBuilder>();
 *     }
 *
 *     ConversionResult Convert(const ConversionParams& params) override {
 *         // 实现转换逻辑
 *     }
 * };
 * @endcode
 */
template<typename Derived>
class SimplePipeline : public IConversionPipeline {
public:
    SimplePipeline() = default;
    ~SimplePipeline() override = default;

    SimplePipeline(const SimplePipeline&) = delete;
    SimplePipeline& operator=(const SimplePipeline&) = delete;
    SimplePipeline(SimplePipeline&&) = default;
    SimplePipeline& operator=(SimplePipeline&&) = default;

    void SetDataSource(std::unique_ptr<DataSource> dataSource) override {
        externalDataSource_ = dataSource.get();
        dataSource_ = std::move(dataSource);
    }

    void SetSpatialIndex(std::unique_ptr<ISpatialIndex> spatialIndex) override {
        externalSpatialIndex_ = spatialIndex.get();
        spatialIndex_ = std::move(spatialIndex);
    }

    void SetTilesetBuilder(std::unique_ptr<ITilesetBuilder> tilesetBuilder) override {
        externalTilesetBuilder_ = tilesetBuilder.get();
        tilesetBuilder_ = std::move(tilesetBuilder);
    }

    void SetProgressCallback(ProgressCallback callback) override {
        progressCallback_ = std::move(callback);
    }

protected:
    [[nodiscard]] DataSource* GetCurrentDataSource() {
        if (externalDataSource_) {
            return externalDataSource_;
        }
        if (!dataSource_) {
            dataSource_ = Derived::CreateDataSource();
        }
        return dataSource_.get();
    }

    [[nodiscard]] ISpatialIndex* GetCurrentSpatialIndex() {
        if (externalSpatialIndex_) {
            return externalSpatialIndex_;
        }
        if (!spatialIndex_) {
            spatialIndex_ = Derived::CreateSpatialIndex();
        }
        return spatialIndex_.get();
    }

    [[nodiscard]] ITilesetBuilder* GetCurrentTilesetBuilder() {
        if (externalTilesetBuilder_) {
            return externalTilesetBuilder_;
        }
        if (!tilesetBuilder_) {
            tilesetBuilder_ = Derived::CreateTilesetBuilder();
        }
        return tilesetBuilder_.get();
    }

    void ReportProgress(const std::string& stage, float progress) {
        if (progressCallback_) {
            progressCallback_(stage, progress);
        }
    }

    void ReportStart() { ReportProgress("initialization", 0.0f); }
    void ReportProcessing() { ReportProgress("processing", 0.5f); }
    void ReportCompletion() { ReportProgress("completion", 1.0f); }

    std::unique_ptr<DataSource> dataSource_;
    std::unique_ptr<ISpatialIndex> spatialIndex_;
    std::unique_ptr<ITilesetBuilder> tilesetBuilder_;
    DataSource* externalDataSource_ = nullptr;
    ISpatialIndex* externalSpatialIndex_ = nullptr;
    ITilesetBuilder* externalTilesetBuilder_ = nullptr;
    ProgressCallback progressCallback_;
};

} // namespace pipeline

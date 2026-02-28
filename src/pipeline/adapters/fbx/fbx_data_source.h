#pragma once

/**
 * @file fbx_data_source.h
 * @brief FBX 数据源适配器
 *
 * 步骤1：将 FBXLoader 适配为 pipeline::DataSource 接口
 */

#include "pipeline/data_source.h"
#include "fbx/core/fbx.h"
#include "fbx/fbx_spatial_item_adapter.h"
#include <memory>
#include <vector>

namespace pipeline::adapters::fbx {

// FBX 空间项适配器 - 实现 ISpatialItem 接口
class FBXSpatialItemImpl : public ISpatialItem {
public:
    explicit FBXSpatialItemImpl(::fbx::FBXSpatialItemPtr item)
        : item_(std::move(item)) {}

    [[nodiscard]] auto GetId() const -> uint64_t override {
        return static_cast<uint64_t>(item_->getId());
    }

    [[nodiscard]] auto GetBounds() const
        -> std::tuple<double, double, double, double, double, double> override {
        auto bounds = item_->getBounds();
        auto min = bounds.min();
        auto max = bounds.max();
        return {
            min[0], min[1], min[2],
            max[0], max[1], max[2]
        };
    }

    [[nodiscard]] auto GetGeometry() const
        -> osg::ref_ptr<osg::Geometry> override {
        const auto* geom = item_->getGeometry();
        return const_cast<osg::Geometry*>(geom);
    }

    [[nodiscard]] auto GetProperties() const
        -> std::unordered_map<std::string, std::string> override {
        std::unordered_map<std::string, std::string> props;
        props["node_name"] = item_->getNodeName();
        props["transform_index"] = std::to_string(item_->getTransformIndex());
        return props;
    }

    // 获取原始适配器
    [[nodiscard]] auto GetOriginalAdapter() const
        -> const ::fbx::FBXSpatialItemAdapter* {
        return item_.get();
    }

    [[nodiscard]] auto GetOriginalAdapter() -> ::fbx::FBXSpatialItemAdapter* {
        return item_.get();
    }

private:
    ::fbx::FBXSpatialItemPtr item_;
};

// FBX 数据源适配器
class FBXDataSource : public DataSource {
public:
    FBXDataSource() = default;
    ~FBXDataSource() override = default;

    // 禁止拷贝，允许移动
    FBXDataSource(const FBXDataSource&) = delete;
    FBXDataSource& operator=(const FBXDataSource&) = delete;
    FBXDataSource(FBXDataSource&&) = default;
    FBXDataSource& operator=(FBXDataSource&&) = default;

    // 加载数据
    [[nodiscard]] auto Load(const DataSourceConfig& config) -> bool override {
        config_ = config;

        loader_ = std::make_unique<::FBXLoader>(config.input_path.string());
        loader_->load();

        if (!loader_->getRoot()) {
            return false;
        }

        // 创建空间项适配器
        spatialItems_ = ::fbx::createSpatialItems(loader_.get());

        // 转换为统一接口
        pipelineItems_.clear();
        pipelineItems_.reserve(spatialItems_.size());
        for (const auto& item : spatialItems_) {
            pipelineItems_.push_back(std::make_shared<FBXSpatialItemImpl>(item));
        }

        // 计算世界包围盒
        CalculateWorldBounds();

        return true;
    }

    // 获取空间项列表
    [[nodiscard]] auto GetSpatialItems() const -> SpatialItemList override {
        return pipelineItems_;
    }

    // 获取世界包围盒
    [[nodiscard]] auto GetWorldBounds() const
        -> std::tuple<double, double, double, double, double, double> override {
        return {
            worldMinX_, worldMinY_, worldMinZ_,
            worldMaxX_, worldMaxY_, worldMaxZ_
        };
    }

    // 获取地理参考
    [[nodiscard]] auto GetGeoReference() const
        -> std::tuple<double, double, double> override {
        return {
            config_.center_longitude,
            config_.center_latitude,
            config_.center_height
        };
    }

    // 获取数据项数量
    [[nodiscard]] auto GetItemCount() const noexcept -> std::size_t override {
        return pipelineItems_.size();
    }

    // 是否已加载
    [[nodiscard]] auto IsLoaded() const noexcept -> bool override {
        return loader_ != nullptr && loader_->getRoot() != nullptr;
    }

    // 获取原始加载器（供其他组件使用）
    [[nodiscard]] auto GetLoader() const -> ::FBXLoader* {
        return loader_.get();
    }

    // 获取原始空间项列表
    [[nodiscard]] auto GetFBXSpatialItems() const
        -> const ::fbx::FBXSpatialItemList& {
        return spatialItems_;
    }

private:
    void CalculateWorldBounds() {
        if (pipelineItems_.empty()) {
            worldMinX_ = worldMinY_ = worldMinZ_ = 0.0;
            worldMaxX_ = worldMaxY_ = worldMaxZ_ = 0.0;
            return;
        }

        worldMinX_ = std::numeric_limits<double>::max();
        worldMinY_ = std::numeric_limits<double>::max();
        worldMinZ_ = std::numeric_limits<double>::max();
        worldMaxX_ = std::numeric_limits<double>::lowest();
        worldMaxY_ = std::numeric_limits<double>::lowest();
        worldMaxZ_ = std::numeric_limits<double>::lowest();

        for (const auto& item : pipelineItems_) {
            auto [minx, miny, minz, maxx, maxy, maxz] = item->GetBounds();
            worldMinX_ = std::min(worldMinX_, minx);
            worldMinY_ = std::min(worldMinY_, miny);
            worldMinZ_ = std::min(worldMinZ_, minz);
            worldMaxX_ = std::max(worldMaxX_, maxx);
            worldMaxY_ = std::max(worldMaxY_, maxy);
            worldMaxZ_ = std::max(worldMaxZ_, maxz);
        }
    }

    DataSourceConfig config_;
    std::unique_ptr<::FBXLoader> loader_;
    ::fbx::FBXSpatialItemList spatialItems_;
    SpatialItemList pipelineItems_;
    double worldMinX_ = 0.0, worldMinY_ = 0.0, worldMinZ_ = 0.0;
    double worldMaxX_ = 0.0, worldMaxY_ = 0.0, worldMaxZ_ = 0.0;
};

// 注册 FBX 数据源
REGISTER_DATA_SOURCE("fbx", FBXDataSource);

} // namespace pipeline::adapters::fbx

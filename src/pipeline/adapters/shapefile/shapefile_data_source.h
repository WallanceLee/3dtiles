#pragma once

/**
 * @file shapefile_data_source.h
 * @brief Shapefile 数据源适配器
 *
 * 步骤1：将 ShapefileDataPool 适配为 pipeline::DataSource 接口
 */

#include "pipeline/data_source.h"
#include "shapefile/shapefile_data_pool.h"
#include "shapefile/shapefile_spatial_item_adapter.h"
#include <memory>
#include <vector>

namespace pipeline::adapters::shapefile {

// Shapefile 空间项适配器 - 实现 ISpatialItem 接口
class ShapefileSpatialItemImpl : public ISpatialItem {
public:
    explicit ShapefileSpatialItemImpl(::shapefile::ShapefileDataPool::ItemPtr item)
        : item_(std::move(item)) {}

    [[nodiscard]] auto GetId() const -> uint64_t override {
        return static_cast<uint64_t>(item_->featureId);
    }

    [[nodiscard]] auto GetBounds() const
        -> std::tuple<double, double, double, double, double, double> override {
        return {
            item_->bounds.minx,
            item_->bounds.miny,
            item_->bounds.minHeight,
            item_->bounds.maxx,
            item_->bounds.maxy,
            item_->bounds.maxHeight
        };
    }

    [[nodiscard]] auto GetGeometry() const
        -> osg::ref_ptr<osg::Geometry> override {
        if (!item_->geometries.empty()) {
            return item_->geometries[0];
        }
        return nullptr;
    }

    [[nodiscard]] auto GetProperties() const
        -> std::unordered_map<std::string, std::string> override {
        std::unordered_map<std::string, std::string> props;
        for (const auto& [key, value] : item_->properties) {
            props[key] = value.dump();
        }
        return props;
    }

    // 获取原始数据项
    [[nodiscard]] auto GetOriginalItem() const
        -> const ::shapefile::ShapefileSpatialItem* {
        return item_.get();
    }

private:
    ::shapefile::ShapefileDataPool::ItemPtr item_;
};

// Shapefile 数据源适配器
class ShapefileDataSource : public DataSource {
public:
    ShapefileDataSource() = default;
    ~ShapefileDataSource() override = default;

    // 禁止拷贝，允许移动
    ShapefileDataSource(const ShapefileDataSource&) = delete;
    ShapefileDataSource& operator=(const ShapefileDataSource&) = delete;
    ShapefileDataSource(ShapefileDataSource&&) = default;
    ShapefileDataSource& operator=(ShapefileDataSource&&) = default;

    // 加载数据
    [[nodiscard]] auto Load(const DataSourceConfig& config) -> bool override {
        config_ = config;

        dataPool_ = std::make_unique<::shapefile::ShapefileDataPool>();

        if (!dataPool_->loadFromShapefileWithGeometry(
                config.input_path.string(),
                config.height_field,
                config.center_longitude,
                config.center_latitude)) {
            return false;
        }

        // 构建空间项列表
        spatialItems_.clear();
        const auto& items = dataPool_->getAllItems();
        spatialItems_.reserve(items.size());

        for (const auto& item : items) {
            spatialItems_.push_back(
                std::make_shared<ShapefileSpatialItemImpl>(item));
        }

        // 计算世界包围盒
        CalculateWorldBounds();

        return true;
    }

    // 获取空间项列表
    [[nodiscard]] auto GetSpatialItems() const -> SpatialItemList override {
        return spatialItems_;
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
        return spatialItems_.size();
    }

    // 是否已加载
    [[nodiscard]] auto IsLoaded() const noexcept -> bool override {
        return dataPool_ != nullptr && !spatialItems_.empty();
    }

    // 获取原始数据池（供其他组件使用）
    [[nodiscard]] auto GetDataPool() const
        -> const ::shapefile::ShapefileDataPool* {
        return dataPool_.get();
    }

private:
    void CalculateWorldBounds() {
        if (spatialItems_.empty()) {
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

        for (const auto& item : spatialItems_) {
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
    std::unique_ptr<::shapefile::ShapefileDataPool> dataPool_;
    SpatialItemList spatialItems_;
    double worldMinX_ = 0.0, worldMinY_ = 0.0, worldMinZ_ = 0.0;
    double worldMaxX_ = 0.0, worldMaxY_ = 0.0, worldMaxZ_ = 0.0;
};

// 注册 Shapefile 数据源
REGISTER_DATA_SOURCE("shapefile", ShapefileDataSource);

} // namespace pipeline::adapters::shapefile

#pragma once

/**
 * @file shapefile_spatial_item_adapter.h
 * @brief Shapefile 空间项适配器
 *
 * 阶段2迁移组件：将 ShapefileSpatialItem 适配为空间索引接口
 */

#include "shapefile_data_pool.h"
#include "../spatial/core/spatial_item.h"
#include "../spatial/core/spatial_bounds.h"

namespace shapefile {

/**
 * @brief Shapefile 空间项适配器
 *
 * 将 ShapefileSpatialItem 包装为空间索引可用的 SpatialItem 接口
 */
class ShapefileSpatialItemAdapter : public spatial::core::SpatialItem {
public:
    // 从 shared_ptr 构造
    explicit ShapefileSpatialItemAdapter(const ShapefileDataPool::ItemPtr& item)
        : item_(item) {}

    // 从原始指针构造（用于B3DM生成器）
    explicit ShapefileSpatialItemAdapter(const ShapefileSpatialItem* item)
        : item_(item, [](const ShapefileSpatialItem*) {}) {}

    spatial::core::SpatialBounds<double, 3> getBounds() const override {
        const auto& b = item_->bounds;
        return spatial::core::SpatialBounds<double, 3>(
            std::array<double, 3>{b.minx, b.miny, b.minHeight},
            std::array<double, 3>{b.maxx, b.maxy, b.maxHeight}
        );
    }

    size_t getId() const override {
        return static_cast<size_t>(item_->featureId);
    }

    std::array<double, 3> getCenter() const override {
        return {
            (item_->bounds.minx + item_->bounds.maxx) * 0.5,
            (item_->bounds.miny + item_->bounds.maxy) * 0.5,
            (item_->bounds.minHeight + item_->bounds.maxHeight) * 0.5
        };
    }

    // 获取原始数据项
    const ShapefileSpatialItem* getItem() const { return item_.get(); }
    const ShapefileDataPool::ItemPtr& getItemPtr() const { return item_; }

    int getFeatureId() const { return item_->featureId; }
    const TileBBox& getBounds2D() const { return item_->bounds; }
    const std::map<std::string, nlohmann::json>& getProperties() const { return item_->properties; }

private:
    ShapefileDataPool::ItemPtr item_;
};

} // namespace shapefile

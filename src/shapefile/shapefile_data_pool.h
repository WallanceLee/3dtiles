#pragma once

/**
 * @file shapefile_data_pool.h
 * @brief Shapefile 数据池管理器
 *
 * 阶段1迁移组件：新的数据加载层
 * 使用 shared_ptr 管理 ShapefileSpatialItem，避免数据拷贝
 */

#include "shapefile_tile.h"
#include <memory>
#include <vector>
#include <map>
#include <string>
#include <osg/Geometry>
#include <nlohmann/json.hpp>

namespace shapefile {

/**
 * @brief Shapefile 空间数据项
 *
 * 包含 Shapefile 中一个要素的完整数据
 * 禁止拷贝，只允许通过 shared_ptr 管理
 */
struct ShapefileSpatialItem {
    int featureId = 0;                          // 要素ID
    TileBBox bounds;                            // WGS84 包围盒
    std::vector<osg::ref_ptr<osg::Geometry>> geometries;  // OSG 几何体
    std::map<std::string, nlohmann::json> properties;     // 属性

    // 默认构造函数
    ShapefileSpatialItem() = default;

    // 禁止拷贝
    ShapefileSpatialItem(const ShapefileSpatialItem&) = delete;
    ShapefileSpatialItem& operator=(const ShapefileSpatialItem&) = delete;

    // 允许移动
    ShapefileSpatialItem(ShapefileSpatialItem&&) = default;
    ShapefileSpatialItem& operator=(ShapefileSpatialItem&&) = default;
};

/**
 * @brief Shapefile 数据池
 *
 * 管理从 Shapefile 加载的所有空间数据
 * 使用 shared_ptr 避免数据拷贝，确保数据零拷贝传递
 */
class ShapefileDataPool {
public:
    using ItemPtr = std::shared_ptr<const ShapefileSpatialItem>;

    ShapefileDataPool() = default;
    ~ShapefileDataPool() = default;

    // 禁止拷贝
    ShapefileDataPool(const ShapefileDataPool&) = delete;
    ShapefileDataPool& operator=(const ShapefileDataPool&) = delete;

    // 允许移动
    ShapefileDataPool(ShapefileDataPool&&) = default;
    ShapefileDataPool& operator=(ShapefileDataPool&&) = default;

    /**
     * @brief 从 Shapefile 加载数据（包含几何数据）
     * @param filename Shapefile 路径
     * @param heightField 高度字段名
     * @param centerLon 中心经度（用于ENU坐标转换）
     * @param centerLat 中心纬度（用于ENU坐标转换）
     * @return 是否成功
     */
    bool loadFromShapefileWithGeometry(const std::string& filename, const std::string& heightField,
                                       double centerLon, double centerLat);

    /**
     * @brief 获取数据项数量
     */
    size_t size() const { return items_.size(); }

    /**
     * @brief 获取指定索引的数据项
     * @param index 索引
     * @return 数据项指针
     */
    const ItemPtr& getItem(size_t index) const {
        static const ItemPtr nullPtr;
        if (index >= items_.size()) return nullPtr;
        return items_[index];
    }

    /**
     * @brief 获取所有数据项
     */
    const std::vector<ItemPtr>& getAllItems() const { return items_; }

    /**
     * @brief 计算世界包围盒
     * @return 包含所有数据的包围盒
     */
    TileBBox computeWorldBounds() const;

    /**
     * @brief 清空数据
     */
    void clear() { items_.clear(); }

private:
    std::vector<ItemPtr> items_;  // 数据项列表
};

} // namespace shapefile

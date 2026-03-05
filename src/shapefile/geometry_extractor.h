#pragma once

/**
 * @file shapefile/geometry_extractor.h
 * @brief Shapefile几何体提取器
 *
 * 使用 GeometryExtractorBase 简化实现
 */

#include "../common/geometry_extractor_base.h"
#include "shapefile_spatial_item_adapter.h"
#include <osg/Geometry>

namespace shapefile {

// 前向声明
struct ShapefileSpatialItem;

/**
 * @brief Shapefile几何体提取器
 *
 * 继承 GeometryExtractorBase，只需实现数据提取逻辑
 */
class GeometryExtractor : public common::GeometryExtractorBase<
    GeometryExtractor,                  // 派生类
    ShapefileSpatialItemAdapter,        // 适配器类型
    const ShapefileSpatialItem*> {      // 数据类型
public:
    // ============================================================
    // GeometryExtractorBase 要求实现的纯虚函数
    // ============================================================

    /**
     * @brief 从数据提取几何体
     */
    std::vector<osg::ref_ptr<osg::Geometry>> extractImpl(
        const ShapefileSpatialItem* item);

    /**
     * @brief 获取对象ID
     */
    std::string getIdImpl(const ShapefileSpatialItem* item, size_t id);

    /**
     * @brief 获取对象属性
     */
    std::map<std::string, nlohmann::json> getAttributesImpl(
        const ShapefileSpatialItem* item);

    /**
     * @brief 获取材质信息
     *
     * Shapefile通常不包含材质信息，返回默认材质
     */
    std::shared_ptr<common::MaterialInfo> getMaterialImpl(
        const ShapefileSpatialItem* item);

    // ============================================================
    // GeometryExtractorBase 要求实现的辅助方法（必须是 public）
    // ============================================================

    /**
     * @brief 从适配器获取数据
     *
     * ShapefileSpatialItemAdapter 使用 getItem() 而不是 getData()
     */
    const ShapefileSpatialItem* getData(
        const ShapefileSpatialItemAdapter* adapter) {
        return adapter->getItem();
    }

    /**
     * @brief 获取适配器中的ID
     */
    size_t getItemId(const ShapefileSpatialItemAdapter* adapter) {
        return adapter->getId();
    }
};

} // namespace shapefile

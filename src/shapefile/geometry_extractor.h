#pragma once

/**
 * @file shapefile/geometry_extractor.h
 * @brief Shapefile几何体提取器
 *
 * 实现common::IGeometryExtractor接口，供B3DM生成器使用
 */

#include "../common/geometry_extractor.h"
#include "shapefile_spatial_item_adapter.h"
#include <osg/Geometry>

namespace shapefile {

/**
 * @brief Shapefile几何体提取器
 */
class GeometryExtractor : public common::IGeometryExtractor {
public:
    /**
     * @brief 从空间对象提取几何体
     */
    std::vector<osg::ref_ptr<osg::Geometry>> extract(
        const spatial::core::SpatialItem* item) override;

    /**
     * @brief 获取对象的唯一标识
     */
    std::string getId(const spatial::core::SpatialItem* item) override;

    /**
     * @brief 获取对象的属性
     */
    std::map<std::string, nlohmann::json> getAttributes(
        const spatial::core::SpatialItem* item) override;
};

} // namespace shapefile

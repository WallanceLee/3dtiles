#pragma once

/**
 * @file common/geometry_extractor.h
 * @brief 几何体提取器接口
 *
 * 该接口抽象不同数据源(FBX/Shapefile)的几何体提取逻辑，
 * 供B3DM生成器统一使用。
 */

#include "../spatial/core/spatial_item.h"
#include <osg/Geometry>
#include <vector>
#include <string>
#include <map>
#include <nlohmann/json.hpp>

namespace common {

/**
 * @brief 几何体提取器接口
 *
 * 不同数据源(FBX/Shapefile)实现此接口以提供几何体
 */
class IGeometryExtractor {
public:
    virtual ~IGeometryExtractor() = default;

    /**
     * @brief 从空间对象提取几何体
     * @param item 空间对象
     * @return 几何体列表
     */
    virtual std::vector<osg::ref_ptr<osg::Geometry>> extract(
        const spatial::core::SpatialItem* item) = 0;

    /**
     * @brief 获取对象的唯一标识（用于BatchID）
     */
    virtual std::string getId(const spatial::core::SpatialItem* item) = 0;

    /**
     * @brief 获取对象的属性（用于BatchTable）
     */
    virtual std::map<std::string, nlohmann::json> getAttributes(
        const spatial::core::SpatialItem* item) = 0;
};

} // namespace common

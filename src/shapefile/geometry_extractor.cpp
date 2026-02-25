#include "geometry_extractor.h"
#include "shapefile_spatial_item_adapter.h"

namespace shapefile {

std::vector<osg::ref_ptr<osg::Geometry>> GeometryExtractor::extract(
    const spatial::core::SpatialItem* item) {

    std::vector<osg::ref_ptr<osg::Geometry>> result;

    // 尝试转换为ShapefileSpatialItemAdapter
    const auto* adapter = dynamic_cast<const ShapefileSpatialItemAdapter*>(item);
    if (!adapter) {
        return result;
    }

    // 获取原始ShapefileSpatialItem
    const ShapefileSpatialItem* shapefileItem = adapter->getItem();
    if (!shapefileItem) {
        return result;
    }

    // 返回几何体列表
    return shapefileItem->geometries;
}

std::string GeometryExtractor::getId(const spatial::core::SpatialItem* item) {
    const auto* adapter = dynamic_cast<const ShapefileSpatialItemAdapter*>(item);
    if (!adapter) {
        return "";
    }

    // 使用featureId作为ID
    return std::to_string(adapter->getFeatureId());
}

std::map<std::string, nlohmann::json> GeometryExtractor::getAttributes(
    const spatial::core::SpatialItem* item) {

    std::map<std::string, nlohmann::json> result;

    const auto* adapter = dynamic_cast<const ShapefileSpatialItemAdapter*>(item);
    if (!adapter) {
        return result;
    }

    const ShapefileSpatialItem* shapefileItem = adapter->getItem();
    if (!shapefileItem) {
        return result;
    }

    // 复制属性
    return shapefileItem->properties;
}

} // namespace shapefile

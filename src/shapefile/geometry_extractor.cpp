#include "geometry_extractor.h"
#include "shapefile_data_pool.h"  // for ShapefileSpatialItem

namespace shapefile {

// ============================================================
// 数据提取实现
// ============================================================

std::vector<osg::ref_ptr<osg::Geometry>> GeometryExtractor::extractImpl(
    const ShapefileSpatialItem* item) {

    if (!item) {
        return {};
    }

    // 返回几何体列表
    return item->geometries;
}

std::string GeometryExtractor::getIdImpl(const ShapefileSpatialItem* item, size_t id) {
    (void)item;  // Shapefile 使用 featureId 作为 ID
    return std::to_string(id);
}

std::map<std::string, nlohmann::json> GeometryExtractor::getAttributesImpl(
    const ShapefileSpatialItem* item) {

    if (!item) {
        return {};
    }

    // 复制属性
    return item->properties;
}

std::shared_ptr<common::MaterialInfo> GeometryExtractor::getMaterialImpl(
    const ShapefileSpatialItem* item) {
    (void)item;  // Shapefile 不包含材质信息
    // 返回默认材质
    return std::make_shared<common::MaterialInfo>();
}

} // namespace shapefile

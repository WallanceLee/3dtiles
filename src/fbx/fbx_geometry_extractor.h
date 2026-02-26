#pragma once

/**
 * @file fbx/fbx_geometry_extractor.h
 * @brief FBX几何体提取器
 *
 * 实现IGeometryExtractor接口，供B3DMGenerator使用
 */

#include "../common/geometry_extractor.h"
#include "fbx_spatial_item_adapter.h"
#include <osg/Geometry>

namespace fbx {

/**
 * @brief FBX几何体提取器
 *
 * 从FBXSpatialItemAdapter提取几何体信息
 */
class FBXGeometryExtractor : public common::IGeometryExtractor {
public:
    FBXGeometryExtractor() = default;
    ~FBXGeometryExtractor() override = default;

    /**
     * @brief 从空间对象提取几何体
     * @param item 空间对象（必须是FBXSpatialItemAdapter）
     * @return 几何体列表
     */
    std::vector<osg::ref_ptr<osg::Geometry>> extract(
        const spatial::core::SpatialItem* item) override;

    /**
     * @brief 获取对象的唯一标识（用于BatchID）
     */
    std::string getId(const spatial::core::SpatialItem* item) override;

    /**
     * @brief 获取对象的属性（用于BatchTable）
     */
    std::map<std::string, nlohmann::json> getAttributes(
        const spatial::core::SpatialItem* item) override;

    /**
     * @brief 获取对象的材质信息
     *
     * 从FBX空间对象提取完整的材质信息，包括：
     * - PBR参数（baseColor, roughness, metallic等）
     * - 纹理对象（从StateSet提取）
     * - 纹理变换（从MaterialExtensionData提取）
     * - Specular-Glossiness参数（传统FBX材质）
     *
     * @param item FBX空间对象
     * @return 材质信息，如果没有材质返回默认材质
     */
    std::shared_ptr<common::MaterialInfo> getMaterial(
        const spatial::core::SpatialItem* item) override;
};

} // namespace fbx

#pragma once

/**
 * @file fbx/fbx_spatial_item_adapter.h
 * @brief FBX 空间项适配器
 *
 * 阶段1迁移组件：将 FBX 的 MeshInstanceInfo 适配为空间索引接口
 */

#include "../spatial/core/spatial_item.h"
#include "../spatial/core/spatial_bounds.h"
#include "fbx/core/fbx.h"
#include <osg/BoundingBox>
#include <osg/Matrixd>
#include <memory>
#include <optional>

namespace fbx {

/**
 * @brief FBX 空间项适配器
 *
 * 将 FBX 的 MeshInstanceInfo 包装为空间索引可用的 SpatialItem 接口
 */
class FBXSpatialItemAdapter : public spatial::core::SpatialItem {
public:
    /**
     * @brief 构造函数
     * @param meshInfo Mesh 实例信息
     * @param transformIndex 变换矩阵索引
     */
    FBXSpatialItemAdapter(MeshInstanceInfo* meshInfo, int transformIndex);

    // SpatialItem 接口实现
    spatial::core::SpatialBounds<double, 3> getBounds() const override;
    size_t getId() const override;
    std::array<double, 3> getCenter() const override;

    // FBX 特有接口
    MeshInstanceInfo* getMeshInfo() const { return meshInfo_; }
    int getTransformIndex() const { return transformIndex_; }
    osg::Matrixd getTransform() const;
    std::string getNodeName() const;
    const osg::Geometry* getGeometry() const;

    /**
     * @brief 获取材质扩展数据
     * @return 材质扩展数据指针，如果没有返回nullptr
     */
    const MaterialExtensionData* getMaterialExtensionData() const { return materialExtData_; }

    /**
     * @brief 设置材质扩展数据
     * @param data 材质扩展数据指针（由外部管理生命周期）
     */
    void setMaterialExtensionData(const MaterialExtensionData* data) { materialExtData_ = data; }

private:
    MeshInstanceInfo* meshInfo_;
    int transformIndex_;
    mutable std::optional<osg::BoundingBox> worldBoundsCache_;
    const MaterialExtensionData* materialExtData_ = nullptr;  // 材质扩展数据（不拥有所有权）

    void computeWorldBounds() const;
};

using FBXSpatialItemPtr = std::shared_ptr<FBXSpatialItemAdapter>;
using FBXSpatialItemList = std::vector<FBXSpatialItemPtr>;

/**
 * @brief 从 FBXLoader 创建所有空间对象适配器
 * @param loader FBX 加载器
 * @return 空间对象适配器列表
 */
FBXSpatialItemList createSpatialItems(FBXLoader* loader);

} // namespace fbx

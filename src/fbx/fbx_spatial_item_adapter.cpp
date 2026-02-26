#include "fbx_spatial_item_adapter.h"
#include "../extern.h"
#include <osg/Geometry>
#include <osg/ComputeBoundsVisitor>

namespace fbx {

FBXSpatialItemAdapter::FBXSpatialItemAdapter(MeshInstanceInfo* meshInfo, int transformIndex)
    : meshInfo_(meshInfo)
    , transformIndex_(transformIndex) {
}

spatial::core::SpatialBounds<double, 3> FBXSpatialItemAdapter::getBounds() const {
    if (!worldBoundsCache_.has_value()) {
        computeWorldBounds();
    }

    const osg::BoundingBox& bbox = worldBoundsCache_.value();
    return spatial::core::SpatialBounds<double, 3>(
        std::array<double, 3>{bbox.xMin(), bbox.yMin(), bbox.zMin()},
        std::array<double, 3>{bbox.xMax(), bbox.yMax(), bbox.zMax()}
    );
}

size_t FBXSpatialItemAdapter::getId() const {
    // 使用 meshInfo 指针和 transformIndex 组合生成唯一 ID
    size_t meshId = reinterpret_cast<size_t>(meshInfo_);
    return (meshId << 16) | static_cast<size_t>(transformIndex_);
}

std::array<double, 3> FBXSpatialItemAdapter::getCenter() const {
    if (!worldBoundsCache_.has_value()) {
        computeWorldBounds();
    }

    const osg::BoundingBox& bbox = worldBoundsCache_.value();
    return {
        (bbox.xMin() + bbox.xMax()) * 0.5,
        (bbox.yMin() + bbox.yMax()) * 0.5,
        (bbox.zMin() + bbox.zMax()) * 0.5
    };
}

osg::Matrixd FBXSpatialItemAdapter::getTransform() const {
    if (meshInfo_ && transformIndex_ >= 0 && transformIndex_ < static_cast<int>(meshInfo_->transforms.size())) {
        return meshInfo_->transforms[transformIndex_];
    }
    return osg::Matrixd::identity();
}

std::string FBXSpatialItemAdapter::getNodeName() const {
    if (meshInfo_ && transformIndex_ >= 0 && transformIndex_ < static_cast<int>(meshInfo_->nodeNames.size())) {
        return meshInfo_->nodeNames[transformIndex_];
    }
    return "";
}

const osg::Geometry* FBXSpatialItemAdapter::getGeometry() const {
    if (meshInfo_) {
        return meshInfo_->geometry.get();
    }
    return nullptr;
}

void FBXSpatialItemAdapter::computeWorldBounds() const {
    worldBoundsCache_ = osg::BoundingBox();

    if (!meshInfo_ || !meshInfo_->geometry) {
        return;
    }

    // 获取局部包围盒
    osg::BoundingBox localBounds;
    const osg::Geometry* geom = meshInfo_->geometry.get();

    if (geom->getVertexArray()) {
        osg::ComputeBoundsVisitor cbv;
        const_cast<osg::Geometry*>(geom)->accept(cbv);
        localBounds = cbv.getBoundingBox();
    }

    // 应用世界变换
    osg::Matrixd transform = getTransform();

    // 变换包围盒的8个角点
    osg::BoundingBox worldBounds;
    for (int i = 0; i < 8; ++i) {
        osg::Vec3d corner(
            (i & 1) ? localBounds.xMax() : localBounds.xMin(),
            (i & 2) ? localBounds.yMax() : localBounds.yMin(),
            (i & 4) ? localBounds.zMax() : localBounds.zMin()
        );
        worldBounds.expandBy(corner * transform);
    }

    worldBoundsCache_ = worldBounds;
}

FBXSpatialItemList createSpatialItems(FBXLoader* loader) {
    FBXSpatialItemList items;

    if (!loader) {
        return items;
    }

    // 遍历 meshPool，为每个 transform 创建适配器
    for (auto& pair : loader->meshPool) {
        MeshInstanceInfo& meshInfo = pair.second;

        // 获取该mesh的材质扩展数据（如果有）
        const MaterialExtensionData* matExtData = nullptr;
        if (meshInfo.geometry && meshInfo.geometry->getStateSet()) {
            const osg::StateSet* stateSet = meshInfo.geometry->getStateSet();
            auto it = loader->stateSetExtensionCache.find(stateSet);
            if (it != loader->stateSetExtensionCache.end()) {
                matExtData = &(it->second);
            }
        }

        // 为每个变换实例创建适配器
        for (int i = 0; i < static_cast<int>(meshInfo.transforms.size()); ++i) {
            auto adapter = std::make_shared<FBXSpatialItemAdapter>(&meshInfo, i);
            // 设置材质扩展数据（所有实例共享相同的材质）
            if (matExtData) {
                adapter->setMaterialExtensionData(matExtData);
            }
            items.push_back(adapter);
        }
    }

    LOG_I("Created %zu spatial items from FBX loader", items.size());
    return items;
}

} // namespace fbx

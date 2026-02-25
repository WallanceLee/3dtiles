#include "fbx_octree_adapter.h"
#include "fbx_spatial_item_adapter.h"
#include "../fbx.h"
#include <osg/Geometry>

namespace fbx {

LegacyOctreeNode* FBXOctreeAdapter::convertFromIndex(
    const spatial::strategy::OctreeIndex& index,
    const FBXSpatialItemList& spatialItems) {
    
    const auto* root = index.getRoot();
    if (!root) {
        return nullptr;
    }
    
    return convertNode(static_cast<const spatial::strategy::OctreeNode*>(root), spatialItems);
}

LegacyOctreeNode* FBXOctreeAdapter::convertNode(
    const spatial::strategy::OctreeNode* node,
    const FBXSpatialItemList& spatialItems) {
    
    if (!node) {
        return nullptr;
    }
    
    auto* legacyNode = new LegacyOctreeNode();
    
    // 转换包围盒
    auto bounds = node->getBounds3D();
    auto min = bounds.min();
    auto max = bounds.max();
    legacyNode->bbox = osg::BoundingBox(
        min[0], min[1], min[2],
        max[0], max[1], max[2]
    );
    
    // 转换深度
    legacyNode->depth = static_cast<int>(node->getDepth());
    
    // 转换内容
    auto items = node->getItems();
    for (const auto& itemRef : items) {
        auto ref = findInstanceRef(itemRef.get(), spatialItems);
        if (ref.meshInfo) {
            legacyNode->content.push_back(ref);
        }
    }
    
    // 递归转换子节点
    auto children = node->getChildren();
    for (const auto* child : children) {
        auto* legacyChild = convertNode(
            static_cast<const spatial::strategy::OctreeNode*>(child), 
            spatialItems
        );
        if (legacyChild) {
            legacyNode->children.push_back(legacyChild);
        }
    }
    
    return legacyNode;
}

InstanceRef FBXOctreeAdapter::findInstanceRef(
    const spatial::core::SpatialItem* item,
    const FBXSpatialItemList& spatialItems) {
    
    InstanceRef ref;
    ref.meshInfo = nullptr;
    ref.transformIndex = -1;
    
    // 查找匹配的spatial item
    for (const auto& spatialItem : spatialItems) {
        if (spatialItem.get() == item) {
            ref.meshInfo = spatialItem->getMeshInfo();
            ref.transformIndex = spatialItem->getTransformIndex();
            return ref;
        }
    }
    
    return ref;
}

bool FBXOctreeAdapter::verifyConversion(
    const LegacyOctreeNode* legacyRoot,
    const spatial::strategy::OctreeIndex& index) {
    
    if (!legacyRoot) {
        return index.getRoot() == nullptr;
    }
    
    const auto* root = index.getRoot();
    if (!root) {
        return false;
    }
    
    // 验证根节点
    auto newBounds = root->getBounds();
    auto legacyMin = legacyRoot->bbox._min;
    auto legacyMax = legacyRoot->bbox._max;
    
    // 简单的包围盒验证
    bool boundsMatch = 
        std::abs(newBounds.min()[0] - legacyMin.x()) < 0.001 &&
        std::abs(newBounds.min()[1] - legacyMin.y()) < 0.001 &&
        std::abs(newBounds.min()[2] - legacyMin.z()) < 0.001 &&
        std::abs(newBounds.max()[0] - legacyMax.x()) < 0.001 &&
        std::abs(newBounds.max()[1] - legacyMax.y()) < 0.001 &&
        std::abs(newBounds.max()[2] - legacyMax.z()) < 0.001;
    
    if (!boundsMatch) {
        return false;
    }
    
    // 验证项目数量
    size_t newItemCount = index.getItemCount();
    
    // 计算legacy树中的项目数量
    std::function<size_t(const LegacyOctreeNode*)> countItems = 
        [&countItems](const LegacyOctreeNode* node) -> size_t {
        if (!node) return 0;
        size_t count = node->content.size();
        for (const auto* child : node->children) {
            count += countItems(child);
        }
        return count;
    };
    
    size_t legacyItemCount = countItems(legacyRoot);
    
    return newItemCount == legacyItemCount;
}

} // namespace fbx

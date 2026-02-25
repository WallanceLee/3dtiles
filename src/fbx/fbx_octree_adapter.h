#pragma once

#include "../spatial/strategy/octree_strategy.h"
#include "fbx_spatial_item_adapter.h"
#include "../fbx.h"
#include <osg/BoundingBox>
#include <vector>
#include <memory>

namespace fbx {

/**
 * @brief 传统八叉树节点结构（与FBXPipeline::OctreeNode兼容）
 *
 * 注意：这个结构体定义在FBXPipeline中是私有的，这里重新定义用于适配器
 */
struct LegacyOctreeNode {
    osg::BoundingBox bbox;
    std::vector<InstanceRef> content;
    std::vector<LegacyOctreeNode*> children;
    int depth = 0;

    bool isLeaf() const { return children.empty(); }
    ~LegacyOctreeNode() {
        for (auto c : children) delete c;
    }
};

/**
 * @brief FBX八叉树适配器
 *
 * 阶段2适配器：将新的OctreeStrategy节点转换为LegacyOctreeNode格式
 * 使后续的processNode可以继续使用老的数据结构
 */
class FBXOctreeAdapter {
public:
    /**
     * @brief 将OctreeIndex转换为传统OctreeNode
     * @param index 八叉树索引
     * @param spatialItems 空间对象列表（用于查找原始InstanceRef）
     * @return 传统OctreeNode树根节点
     */
    static LegacyOctreeNode* convertFromIndex(
        const spatial::strategy::OctreeIndex& index,
        const FBXSpatialItemList& spatialItems);

    /**
     * @brief 验证转换结果
     * @param legacyRoot 传统八叉树根节点
     * @param index 八叉树索引
     * @return 验证是否通过
     */
    static bool verifyConversion(
        const LegacyOctreeNode* legacyRoot,
        const spatial::strategy::OctreeIndex& index);

private:
    static LegacyOctreeNode* convertNode(
        const spatial::strategy::OctreeNode* node,
        const FBXSpatialItemList& spatialItems);

    static InstanceRef findInstanceRef(
        const spatial::core::SpatialItem* item,
        const FBXSpatialItemList& spatialItems);
};

} // namespace fbx

#pragma once

#include "../core/slicing_strategy.h"
#include <array>
#include <vector>
#include <memory>

namespace spatial::strategy {

/**
 * @brief 八叉树切片配置
 */
struct OctreeConfig : public core::SlicingConfig {
    // 八叉树特定配置
    size_t minItemsPerNode = 500;
};

/**
 * @brief 八叉树节点
 * 
 * 使用 std::unique_ptr 管理子节点，自动内存管理
 */
class OctreeNode : public core::SpatialIndexNode {
public:
    OctreeNode() = default;
    OctreeNode(const core::SpatialBounds<double, 3>& bounds, int depth)
        : bounds_(bounds), depth_(depth) {}

    void setBounds(const core::SpatialBounds<double, 3>& bounds) { bounds_ = bounds; }
    void setDepth(int depth) { depth_ = depth; }
    void addItem(const core::SpatialItemRef& item) { items_.push_back(item); }

    const core::SpatialBounds<double, 3>& getBounds3D() const { return bounds_; }
    OctreeNode* getParent() const { return parent_; }
    OctreeNode* getChild(int index) const { return children_[index].get(); }

    void setChild(int index, std::unique_ptr<OctreeNode> child) {
        if (child) {
            child->parent_ = this;
        }
        children_[index] = std::move(child);
    }

    core::SpatialBounds<double, 3> getBounds() const override {
        return bounds_;
    }

    size_t getDepth() const override { return depth_; }

    core::SpatialItemRefList getItems() const override {
        return items_;
    }

    bool isLeaf() const override {
        return !children_[0];
    }

    std::vector<const core::SpatialIndexNode*> getChildren() const override {
        std::vector<const core::SpatialIndexNode*> result;
        for (const auto& child : children_) {
            if (child) {
                result.push_back(child.get());
            }
        }
        return result;
    }

    size_t getItemCount() const override { return items_.size(); }

    int getChildIndex(const std::array<double, 3>& point) const {
        auto center = bounds_.center();
        int index = 0;
        if (point[0] >= center[0]) index += 1;
        if (point[1] >= center[1]) index += 2;
        if (point[2] >= center[2]) index += 4;
        return index;
    }

    void split() {
        auto center = bounds_.center();
        auto min = bounds_.min();
        auto max = bounds_.max();

        // 8个子节点：按x, y, z顺序
        // index = (x >= cx) + 2*(y >= cy) + 4*(z >= cz)
        for (int i = 0; i < 8; ++i) {
            bool xHigh = (i & 1) != 0;
            bool yHigh = (i & 2) != 0;
            bool zHigh = (i & 4) != 0;

            std::array<double, 3> childMin{
                xHigh ? center[0] : min[0],
                yHigh ? center[1] : min[1],
                zHigh ? center[2] : min[2]
            };
            std::array<double, 3> childMax{
                xHigh ? max[0] : center[0],
                yHigh ? max[1] : center[1],
                zHigh ? max[2] : center[2]
            };

            auto child = std::make_unique<OctreeNode>(
                core::SpatialBounds<double, 3>(childMin, childMax),
                depth_ + 1
            );
            child->parent_ = this;
            children_[i] = std::move(child);
        }
    }

    void collectAllItems(core::SpatialItemRefList& result) const {
        result.insert(result.end(), items_.begin(), items_.end());
        for (const auto& child : children_) {
            if (child) {
                child->collectAllItems(result);
            }
        }
    }

    // 清除所有子节点（unique_ptr 自动释放内存）
    void clearChildren() {
        for (auto& child : children_) {
            child.reset();
        }
    }

    ~OctreeNode() = default;

private:
    core::SpatialBounds<double, 3> bounds_;
    int depth_ = 0;
    OctreeNode* parent_ = nullptr;
    std::array<std::unique_ptr<OctreeNode>, 8> children_;
    core::SpatialItemRefList items_;
};

/**
 * @brief 八叉树索引
 */
class OctreeIndex : public core::SpatialIndex {
public:
    OctreeIndex(std::unique_ptr<OctreeNode> root, const core::SpatialBounds<double, 3>& worldBounds)
        : root_(std::move(root)), worldBounds_(worldBounds) {}

    const core::SpatialIndexNode* getRootNode() const override {
        return root_.get();
    }

    core::SpatialItemRefList getAllItems() const override {
        core::SpatialItemRefList result;
        if (root_) {
            root_->collectAllItems(result);
        }
        return result;
    }

    core::SpatialItemRefList query(const core::SpatialBounds<double, 3>& bounds) const override {
        core::SpatialItemRefList result;
        if (!root_) return result;

        queryRecursive(root_.get(), bounds, result);
        return result;
    }

    size_t getNodeCount() const override { return nodeCount_; }
    size_t getItemCount() const override { return getAllItems().size(); }

    void setNodeCount(size_t count) { nodeCount_ = count; }
    OctreeNode* getRoot() const { return root_.get(); }

    ~OctreeIndex() = default;

private:
    void queryRecursive(OctreeNode* node, 
                       const core::SpatialBounds<double, 3>& queryBounds,
                       core::SpatialItemRefList& result) const {
        if (!node) return;

        if (!node->getBounds3D().intersects(queryBounds)) {
            return;
        }

        result.insert(result.end(), node->getItems().begin(), node->getItems().end());

        for (int i = 0; i < 8; ++i) {
            queryRecursive(node->getChild(i), queryBounds, result);
        }
    }

    std::unique_ptr<OctreeNode> root_;
    core::SpatialBounds<double, 3> worldBounds_;
    size_t nodeCount_ = 0;
};

/**
 * @brief 八叉树切片策略
 */
class OctreeStrategy : public core::SlicingStrategy {
public:
    OctreeStrategy() = default;

    size_t getDimension() const override { return 3; }
    size_t getChildCount() const override { return 8; }
    const char* getName() const override { return "Octree"; }

    std::unique_ptr<core::SpatialIndex> buildIndex(
        const core::SpatialItemList& items,
        const core::SpatialBounds<double, 3>& worldBounds,
        const core::SlicingConfig& config) override
    {
        const auto& octreeConfig = dynamic_cast<const OctreeConfig&>(config);

        auto root = std::make_unique<OctreeNode>(worldBounds, 0);

        for (const auto& item : items) {
            auto bounds = item->getBounds();
            insertItem(root.get(), item, bounds, 0, octreeConfig);
        }

        size_t nodeCount = 0;
        countNodes(root.get(), nodeCount);

        auto index = std::make_unique<OctreeIndex>(std::move(root), worldBounds);
        index->setNodeCount(nodeCount);
        return index;
    }

private:
    void insertItem(OctreeNode* node,
                   const core::SpatialItemPtr& item,
                   const core::SpatialBounds<double, 3>& itemBounds,
                   int depth,
                   const OctreeConfig& config)
    {
        if (!node->getBounds3D().intersects(itemBounds)) {
            return;
        }

        if (node->isLeaf()) {
            node->addItem(core::SpatialItemRef(item));

            if (depth < static_cast<int>(config.maxDepth) &&
                node->getItemCount() > config.maxItemsPerNode) {
                redistributeItems(node, config);
            }
            return;
        }

        for (int i = 0; i < 8; ++i) {
            auto* child = node->getChild(i);
            if (child && child->getBounds3D().intersects(itemBounds)) {
                insertItem(child, item, itemBounds, depth + 1, config);
            }
        }
    }

    void redistributeItems(OctreeNode* node, const OctreeConfig& config) {
        auto items = node->getItems();
        node->clearChildren();
        node->split();

        for (const auto& itemRef : items) {
            auto bounds = itemRef->getBounds();
            auto tempPtr = std::shared_ptr<core::SpatialItem>(const_cast<core::SpatialItem*>(itemRef.get()), [](core::SpatialItem*){});
            insertItem(node, tempPtr, bounds, node->getDepth(), config);
        }
    }

    void countNodes(OctreeNode* node, size_t& count) const {
        if (!node) return;
        count++;
        for (int i = 0; i < 8; ++i) {
            countNodes(node->getChild(i), count);
        }
    }
};

} // namespace spatial::strategy

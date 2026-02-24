#pragma once

#include "../core/slicing_strategy.h"
#include <array>
#include <unordered_map>
#include <queue>

namespace spatial::strategy {

/**
 * @brief 四叉树切片配置
 */
struct QuadtreeConfig : public core::SlicingConfig {
    // 四叉树特定配置
    size_t minItemsPerNode = 100;
    double metricThreshold = 0.01;
};

/**
 * @brief 四叉树节点坐标
 */
struct QuadtreeCoord {
    int z = 0;
    int x = 0;
    int y = 0;

    QuadtreeCoord() = default;
    QuadtreeCoord(int z_, int x_, int y_) : z(z_), x(x_), y(y_) {}

    uint64_t encode() const {
        return (static_cast<uint64_t>(z) << 48) |
               (static_cast<uint64_t>(x) << 24) |
               static_cast<uint64_t>(y);
    }

    static QuadtreeCoord decode(uint64_t key) {
        return QuadtreeCoord(
            static_cast<int>((key >> 48) & 0xFFFF),
            static_cast<int>((key >> 24) & 0xFFFFFF),
            static_cast<int>(key & 0xFFFFFF)
        );
    }

    bool operator==(const QuadtreeCoord& other) const {
        return z == other.z && x == other.x && y == other.y;
    }
};

/**
 * @brief 四叉树节点
 */
class QuadtreeNode : public core::SpatialIndexNode {
public:
    QuadtreeNode() = default;
    QuadtreeNode(const core::SpatialBounds<double, 2>& bounds, int depth)
        : bounds2d_(bounds), depth_(depth) {}

    void setBounds(const core::SpatialBounds<double, 2>& bounds) { bounds2d_ = bounds; }
    void setDepth(int depth) { depth_ = depth; }
    void setCoord(const QuadtreeCoord& coord) { coord_ = coord; }
    void addItem(const core::SpatialItemRef& item) { items_.push_back(item); }
    void clearItems() { items_.clear(); }
    void setParent(QuadtreeNode* parent) { parent_ = parent; }

    const core::SpatialBounds<double, 2>& getBounds2D() const { return bounds2d_; }
    const QuadtreeCoord& getCoord() const { return coord_; }
    QuadtreeNode* getParent() const { return parent_; }
    const std::array<QuadtreeNode*, 4>& getChildrenArray() const { return children_; }
    QuadtreeNode* getChild(int index) const { return children_[index]; }

    void setChild(int index, QuadtreeNode* child) {
        children_[index] = child;
    }

    core::SpatialBounds<double, 3> getBounds() const override {
        return core::SpatialBounds<double, 3>(
            bounds2d_.min()[0], bounds2d_.min()[1], 0.0,
            bounds2d_.max()[0], bounds2d_.max()[1], 0.0
        );
    }

    size_t getDepth() const override { return depth_; }

    core::SpatialItemRefList getItems() const override {
        return items_;
    }

    bool isLeaf() const override {
        return children_[0] == nullptr;
    }

    std::vector<const core::SpatialIndexNode*> getChildren() const override {
        std::vector<const core::SpatialIndexNode*> result;
        for (const auto& child : children_) {
            if (child != nullptr) {
                result.push_back(child);
            }
        }
        return result;
    }

    size_t getItemCount() const override { return items_.size(); }

    int getChildIndex(double x, double y) const {
        auto center = bounds2d_.center();
        int index = 0;
        if (x >= center[0]) index += 1;
        if (y >= center[1]) index += 2;
        return index;
    }

    void split() {
        auto center = bounds2d_.center();
        auto min = bounds2d_.min();
        auto max = bounds2d_.max();

        children_[0] = new QuadtreeNode(
            core::SpatialBounds<double, 2>(
                std::array<double, 2>{min[0], min[1]},
                std::array<double, 2>{center[0], center[1]}
            ),
            depth_ + 1
        );
        children_[1] = new QuadtreeNode(
            core::SpatialBounds<double, 2>(
                std::array<double, 2>{center[0], min[1]},
                std::array<double, 2>{max[0], center[1]}
            ),
            depth_ + 1
        );
        children_[2] = new QuadtreeNode(
            core::SpatialBounds<double, 2>(
                std::array<double, 2>{min[0], center[1]},
                std::array<double, 2>{center[0], max[1]}
            ),
            depth_ + 1
        );
        children_[3] = new QuadtreeNode(
            core::SpatialBounds<double, 2>(
                std::array<double, 2>{center[0], center[1]},
                std::array<double, 2>{max[0], max[1]}
            ),
            depth_ + 1
        );

        for (int i = 0; i < 4; ++i) {
            children_[i]->setParent(this);
            QuadtreeCoord childCoord = coord_;
            childCoord.z = depth_ + 1;
            childCoord.x = coord_.x * 2 + (i % 2);
            childCoord.y = coord_.y * 2 + (i / 2);
            children_[i]->setCoord(childCoord);
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

    void collectLeaves(std::vector<QuadtreeNode*>& result) const {
        if (isLeaf()) {
            result.push_back(const_cast<QuadtreeNode*>(this));
        } else {
            for (const auto& child : children_) {
                if (child) {
                    child->collectLeaves(result);
                }
            }
        }
    }

    void deleteChildren() {
        for (int i = 0; i < 4; ++i) {
            if (children_[i]) {
                children_[i]->deleteChildren();
                delete children_[i];
                children_[i] = nullptr;
            }
        }
    }

    ~QuadtreeNode() {
        deleteChildren();
    }

private:
    core::SpatialBounds<double, 2> bounds2d_;
    int depth_ = 0;
    QuadtreeCoord coord_;
    QuadtreeNode* parent_ = nullptr;
    std::array<QuadtreeNode*, 4> children_{nullptr, nullptr, nullptr, nullptr};
    core::SpatialItemRefList items_;
};

/**
 * @brief 四叉树索引
 */
class QuadtreeIndex : public core::SpatialIndex {
public:
    QuadtreeIndex(QuadtreeNode* root, const core::SpatialBounds<double, 2>& worldBounds)
        : root_(root), worldBounds_(worldBounds) {}

    const core::SpatialIndexNode* getRootNode() const override {
        return root_;
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

        core::SpatialBounds<double, 2> bounds2d(
            std::array<double, 2>{bounds.min()[0], bounds.min()[1]},
            std::array<double, 2>{bounds.max()[0], bounds.max()[1]}
        );

        queryRecursive(root_, bounds2d, result);
        return result;
    }

    size_t getNodeCount() const override { return nodeCount_; }
    size_t getItemCount() const override { return getAllItems().size(); }

    void setNodeCount(size_t count) { nodeCount_ = count; }
    QuadtreeNode* getRoot() const { return root_; }

    // 存储原始 SpatialItemPtr 以保持对象生命周期
    void setItemStorage(core::SpatialItemList&& items) { itemStorage_ = std::move(items); }

    ~QuadtreeIndex() {
        if (root_) {
            root_->deleteChildren();
            delete root_;
        }
    }

private:
    void queryRecursive(QuadtreeNode* node,
                       const core::SpatialBounds<double, 2>& queryBounds,
                       core::SpatialItemRefList& result) const {
        if (!node) return;

        if (!node->getBounds2D().intersects(queryBounds)) {
            return;
        }

        result.insert(result.end(), node->getItems().begin(), node->getItems().end());

        for (int i = 0; i < 4; ++i) {
            queryRecursive(node->getChild(i), queryBounds, result);
        }
    }

    QuadtreeNode* root_ = nullptr;
    core::SpatialBounds<double, 2> worldBounds_;
    size_t nodeCount_ = 0;
    core::SpatialItemList itemStorage_;  // 存储原始 SpatialItemPtr 以保持对象生命周期
};

/**
 * @brief 四叉树切片策略
 */
class QuadtreeStrategy : public core::SlicingStrategy {
public:
    QuadtreeStrategy() = default;

    size_t getDimension() const override { return 2; }
    size_t getChildCount() const override { return 4; }
    const char* getName() const override { return "Quadtree"; }

    std::unique_ptr<core::SpatialIndex> buildIndex(
        const core::SpatialItemList& items,
        const core::SpatialBounds<double, 3>& worldBounds,
        const core::SlicingConfig& config) override
    {
        const auto& quadConfig = dynamic_cast<const QuadtreeConfig&>(config);

        core::SpatialBounds<double, 2> worldBounds2d(
            std::array<double, 2>{worldBounds.min()[0], worldBounds.min()[1]},
            std::array<double, 2>{worldBounds.max()[0], worldBounds.max()[1]}
        );

        auto root = std::make_unique<QuadtreeNode>(worldBounds2d, 0);
        root->setCoord(QuadtreeCoord(0, 0, 0));

        for (const auto& item : items) {
            auto bounds = item->getBounds();
            core::SpatialBounds<double, 2> itemBounds2d(
                std::array<double, 2>{bounds.min()[0], bounds.min()[1]},
                std::array<double, 2>{bounds.max()[0], bounds.max()[1]}
            );
            insertItem(root.get(), item, itemBounds2d, 0, quadConfig);
        }

        size_t nodeCount = 0;
        countNodes(root.get(), nodeCount);

        auto index = std::make_unique<QuadtreeIndex>(root.release(), worldBounds2d);
        index->setNodeCount(nodeCount);
        // 存储原始 items 以保持对象生命周期
        index->setItemStorage(const_cast<core::SpatialItemList&&>(items));
        return index;
    }

private:
    void insertItem(QuadtreeNode* node,
                   const core::SpatialItemPtr& item,
                   const core::SpatialBounds<double, 2>& itemBounds,
                   int depth,
                   const QuadtreeConfig& config)
    {
        if (!node->getBounds2D().intersects(itemBounds)) {
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

        for (int i = 0; i < 4; ++i) {
            auto* child = node->getChild(i);
            if (child && child->getBounds2D().intersects(itemBounds)) {
                insertItem(child, item, itemBounds, depth + 1, config);
            }
        }
    }

    void redistributeItems(QuadtreeNode* node, const QuadtreeConfig& config) {
        auto items = node->getItems();
        node->clearItems();  // 清空当前节点的 items
        node->split();       // 分割节点

        // 将 items 重新插入到子节点
        for (const auto& itemRef : items) {
            auto bounds = itemRef->getBounds();
            core::SpatialBounds<double, 2> itemBounds2d(
                std::array<double, 2>{bounds.min()[0], bounds.min()[1]},
                std::array<double, 2>{bounds.max()[0], bounds.max()[1]}
            );
            // 直接使用 itemRef，不需要创建临时 shared_ptr
            // 因为 insertItemToChildren 只使用 bounds 和 itemRef
            insertItemToChildren(node, itemRef, itemBounds2d, node->getDepth() + 1, config);
        }
    }

    // 辅助函数：将 item 插入到子节点（与阶段2原始实现一致：只添加到一个子节点）
    void insertItemToChildren(QuadtreeNode* node,
                             const core::SpatialItemRef& itemRef,
                             const core::SpatialBounds<double, 2>& itemBounds,
                             int depth,
                             const QuadtreeConfig& config)
    {
        for (int i = 0; i < 4; ++i) {
            auto* child = node->getChild(i);
            if (child && child->getBounds2D().intersects(itemBounds)) {
                insertItem(child, itemRef, itemBounds, depth, config);
                // 与阶段2原始实现一致：只添加到一个子节点后就返回
                if (std::find(node->getItems().begin(), node->getItems().end(), itemRef) == node->getItems().end()) {
                    return;
                }
            }
        }
    }

    // 重载 insertItem，接受 SpatialItemRef
    void insertItem(QuadtreeNode* node,
                   const core::SpatialItemRef& itemRef,
                   const core::SpatialBounds<double, 2>& itemBounds,
                   int depth,
                   const QuadtreeConfig& config)
    {
        if (!node->getBounds2D().intersects(itemBounds)) {
            return;
        }

        if (node->isLeaf()) {
            node->addItem(itemRef);

            if (depth < static_cast<int>(config.maxDepth) &&
                node->getItemCount() > config.maxItemsPerNode) {
                redistributeItems(node, config);
            }
            return;
        }

        for (int i = 0; i < 4; ++i) {
            auto* child = node->getChild(i);
            if (child && child->getBounds2D().intersects(itemBounds)) {
                insertItem(child, itemRef, itemBounds, depth + 1, config);
            }
        }
    }

    void countNodes(QuadtreeNode* node, size_t& count) const {
        if (!node) return;
        count++;
        for (int i = 0; i < 4; ++i) {
            countNodes(node->getChild(i), count);
        }
    }
};

} // namespace spatial::strategy

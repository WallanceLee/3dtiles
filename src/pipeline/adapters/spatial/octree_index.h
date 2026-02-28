#pragma once

/**
 * @file octree_index.h
 * @brief Octree 空间索引适配器
 *
 * 步骤2：将 spatial::strategy::OctreeStrategy 适配为 pipeline::ISpatialIndex 接口
 */

#include "pipeline/spatial_index.h"
#include "spatial/strategy/octree_strategy.h"
#include "spatial/core/slicing_strategy.h"
#include <memory>
#include <vector>

namespace pipeline::adapters::spatial {

// Octree 节点适配器
class OctreeNodeAdapter : public ISpatialIndexNode {
public:
    explicit OctreeNodeAdapter(const ::spatial::strategy::OctreeNode* node)
        : node_(node) {}

    [[nodiscard]] auto GetId() const -> uint64_t override {
        if (!node_) return 0;
        // 使用深度和位置计算ID
        // 简化：使用深度和包围盒中心
        auto bounds = node_->getBounds3D();
        auto center = bounds.center();
        return (static_cast<uint64_t>(node_->getDepth()) << 48) |
               (static_cast<uint64_t>(center[0] * 1000) << 24) |
               static_cast<uint64_t>(center[1] * 1000);
    }

    [[nodiscard]] auto GetDepth() const -> int override {
        if (!node_) return 0;
        return static_cast<int>(node_->getDepth());
    }

    [[nodiscard]] auto GetBounds() const
        -> std::tuple<double, double, double, double, double, double> override {
        if (!node_) return {0, 0, 0, 0, 0, 0};

        auto bounds = node_->getBounds3D();
        auto min = bounds.min();
        auto max = bounds.max();
        return {min[0], min[1], min[2], max[0], max[1], max[2]};
    }

    [[nodiscard]] auto GetItems() const -> SpatialItemList override {
        if (!node_) return {};

        SpatialItemList items;
        auto spatialItems = node_->getItems();

        for (const auto& itemRef : spatialItems) {
            // 类型转换逻辑...
        }

        return items;
    }

    [[nodiscard]] auto GetChildren() const
        -> std::vector<const ISpatialIndexNode*> override {
        if (!node_) return {};

        std::vector<const ISpatialIndexNode*> children;
        auto spatialChildren = node_->getChildren();

        for (const auto* child : spatialChildren) {
            if (child) {
                // 类型转换逻辑...
            }
        }

        return children;
    }

    [[nodiscard]] auto IsLeaf() const -> bool override {
        if (!node_) return true;
        return node_->isLeaf();
    }

    [[nodiscard]] auto GetItemCount() const -> std::size_t override {
        if (!node_) return 0;
        return node_->getItemCount();
    }

    // 获取原始节点
    [[nodiscard]] auto GetRawNode() const -> const ::spatial::strategy::OctreeNode* {
        return node_;
    }

private:
    const ::spatial::strategy::OctreeNode* node_;
};

// Octree 空间索引适配器
class OctreeIndexAdapter : public ISpatialIndex {
public:
    OctreeIndexAdapter() = default;
    ~OctreeIndexAdapter() override = default;

    // 禁止拷贝，允许移动
    OctreeIndexAdapter(const OctreeIndexAdapter&) = delete;
    OctreeIndexAdapter& operator=(const OctreeIndexAdapter&) = delete;
    OctreeIndexAdapter(OctreeIndexAdapter&&) = default;
    OctreeIndexAdapter& operator=(OctreeIndexAdapter&&) = default;

    // 构建索引
    [[nodiscard]] auto Build(const DataSource* data_source,
                              const SpatialIndexConfig& config) -> bool override {
        if (!data_source || !data_source->IsLoaded()) {
            return false;
        }

        config_ = config;

        // 获取世界包围盒
        auto [minX, minY, minZ, maxX, maxY, maxZ] = data_source->GetWorldBounds();

        // 创建八叉树配置
        ::spatial::strategy::OctreeConfig octConfig;
        octConfig.maxDepth = static_cast<size_t>(config.max_depth);
        octConfig.maxItemsPerNode = config.max_items_per_node;
        octConfig.minBoundsSize = config.min_bounds_size;

        // 转换为空间项列表
        ::spatial::core::SpatialItemList spatialItems;
        auto items = data_source->GetSpatialItems();

        // 构建八叉树索引
        ::spatial::strategy::OctreeStrategy strategy;
        auto bounds3d = ::spatial::core::SpatialBounds<double, 3>(
            std::array<double, 3>{minX, minY, minZ},
            std::array<double, 3>{maxX, maxY, maxZ}
        );

        index_ = strategy.buildIndex(spatialItems, bounds3d, octConfig);

        return index_ != nullptr;
    }

    // 获取根节点
    [[nodiscard]] auto GetRootNode() const -> const ISpatialIndexNode* override {
        if (!index_) return nullptr;

        auto* rawRoot = index_->getRootNode();
        if (!rawRoot) return nullptr;

        // 动态转换为 OctreeNode
        auto* octRoot = dynamic_cast<const ::spatial::strategy::OctreeNode*>(rawRoot);
        if (!octRoot) return nullptr;

        // 返回适配器
        rootAdapter_ = std::make_unique<OctreeNodeAdapter>(octRoot);
        return rootAdapter_.get();
    }

    // 获取节点数量
    [[nodiscard]] auto GetNodeCount() const -> std::size_t override {
        if (!index_) return 0;
        return index_->getNodeCount();
    }

    // 获取对象数量
    [[nodiscard]] auto GetItemCount() const -> std::size_t override {
        if (!index_) return 0;
        return index_->getItemCount();
    }

    // 查询指定范围内的对象
    [[nodiscard]] auto Query(double minX, double minY, double minZ,
                              double maxX, double maxY, double maxZ) const
        -> SpatialItemList override {
        if (!index_) return {};

        auto bounds = ::spatial::core::SpatialBounds<double, 3>(
            std::array<double, 3>{minX, minY, minZ},
            std::array<double, 3>{maxX, maxY, maxZ}
        );

        auto results = index_->query(bounds);

        // 转换结果类型
        SpatialItemList items;

        return items;
    }

    // 获取原始索引
    [[nodiscard]] auto GetRawIndex() const
        -> const ::spatial::core::SpatialIndex* {
        return index_.get();
    }

    [[nodiscard]] auto GetRawIndex()
        -> ::spatial::core::SpatialIndex* {
        return index_.get();
    }

private:
    SpatialIndexConfig config_;
    std::unique_ptr<::spatial::core::SpatialIndex> index_;
    mutable std::unique_ptr<OctreeNodeAdapter> rootAdapter_;
};

// 注册 Octree 空间索引
REGISTER_SPATIAL_INDEX("octree", OctreeIndexAdapter);

} // namespace pipeline::adapters::spatial

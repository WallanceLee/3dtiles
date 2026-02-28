#pragma once

/**
 * @file spatial_index.h
 * @brief 空间索引抽象接口
 *
 * 步骤2：定义统一的空间索引接口，用于替换直接使用 Quadtree/Octree
 */

#include "data_source.h"
#include <memory>
#include <vector>
#include <string>
#include <functional>

namespace pipeline {

// 空间索引节点接口
class ISpatialIndexNode {
public:
    virtual ~ISpatialIndexNode() = default;

    // 禁止拷贝，允许移动
    ISpatialIndexNode(const ISpatialIndexNode&) = delete;
    ISpatialIndexNode& operator=(const ISpatialIndexNode&) = delete;
    ISpatialIndexNode(ISpatialIndexNode&&) = default;
    ISpatialIndexNode& operator=(ISpatialIndexNode&&) = default;

    // 获取节点唯一ID
    [[nodiscard]] virtual auto GetId() const -> uint64_t = 0;

    // 获取节点深度
    [[nodiscard]] virtual auto GetDepth() const -> int = 0;

    // 获取包围盒
    [[nodiscard]] virtual auto GetBounds() const
        -> std::tuple<double, double, double, double, double, double> = 0;

    // 获取节点中的空间项
    [[nodiscard]] virtual auto GetItems() const -> SpatialItemList = 0;

    // 获取子节点
    [[nodiscard]] virtual auto GetChildren() const
        -> std::vector<const ISpatialIndexNode*> = 0;

    // 是否为叶子节点
    [[nodiscard]] virtual auto IsLeaf() const -> bool = 0;

    // 获取对象数量
    [[nodiscard]] virtual auto GetItemCount() const -> std::size_t = 0;

protected:
    ISpatialIndexNode() = default;
};

// 空间索引配置
struct SpatialIndexConfig {
    // 最大深度
    int max_depth = 10;

    // 每个节点最大对象数
    std::size_t max_items_per_node = 1000;

    // 最小包围盒尺寸
    double min_bounds_size = 0.01;

    // 地理中心（用于坐标转换）
    double center_longitude = 0.0;
    double center_latitude = 0.0;
    double center_height = 0.0;
};

// 空间索引接口
class ISpatialIndex {
public:
    virtual ~ISpatialIndex() = default;

    // 禁止拷贝，允许移动
    ISpatialIndex(const ISpatialIndex&) = delete;
    ISpatialIndex& operator=(const ISpatialIndex&) = delete;
    ISpatialIndex(ISpatialIndex&&) = default;
    ISpatialIndex& operator=(ISpatialIndex&&) = default;

    // 构建索引
    [[nodiscard]] virtual auto Build(const DataSource* data_source,
                                      const SpatialIndexConfig& config) -> bool = 0;

    // 获取根节点
    [[nodiscard]] virtual auto GetRootNode() const -> const ISpatialIndexNode* = 0;

    // 获取节点数量
    [[nodiscard]] virtual auto GetNodeCount() const -> std::size_t = 0;

    // 获取对象数量
    [[nodiscard]] virtual auto GetItemCount() const -> std::size_t = 0;

    // 查询指定范围内的对象
    [[nodiscard]] virtual auto Query(double minX, double minY, double minZ,
                                      double maxX, double maxY, double maxZ) const
        -> SpatialItemList = 0;

protected:
    ISpatialIndex() = default;
};

using SpatialIndexPtr = std::unique_ptr<ISpatialIndex>;
using SpatialIndexCreator = std::function<SpatialIndexPtr()>;

// 空间索引工厂 - 单例注册模式
class SpatialIndexFactory {
public:
    [[nodiscard]] static auto Instance() noexcept -> SpatialIndexFactory&;

    void Register(const std::string& type, SpatialIndexCreator creator);
    [[nodiscard]] auto Create(const std::string& type) const -> SpatialIndexPtr;
    [[nodiscard]] auto IsRegistered(const std::string& type) const noexcept -> bool;

private:
    SpatialIndexFactory() = default;
    ~SpatialIndexFactory() = default;

    std::unordered_map<std::string, SpatialIndexCreator> creators_;
};

// 空间索引注册辅助宏
#define REGISTER_SPATIAL_INDEX(TYPE, CLASS)                                  \
    namespace {                                                              \
        [[maybe_unused]] const bool _##CLASS##_registered = []() -> bool {   \
            ::pipeline::SpatialIndexFactory::Instance().Register(            \
                TYPE, []() -> ::pipeline::SpatialIndexPtr {                  \
                    return std::make_unique<CLASS>();                        \
                });                                                          \
            return true;                                                     \
        }();                                                                 \
    }

} // namespace pipeline

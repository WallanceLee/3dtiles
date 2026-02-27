#pragma once

#include "spatial_bounds.h"
#include "spatial_item.h"
#include <vector>
#include <memory>
#include <functional>
#include <string>

namespace spatial::core {

/**
 * @brief 切片策略配置基类
 */
struct SlicingConfig {
    virtual ~SlicingConfig() = default;

    // 最大深度
    size_t maxDepth = 10;

    // 每个节点最大对象数
    size_t maxItemsPerNode = 1000;

    // 最小包围盒尺寸 (低于此尺寸不再分割)
    double minBoundsSize = 0.01;
};

/**
 * @brief 空间索引节点接口
 */
class SpatialIndexNode {
public:
    virtual ~SpatialIndexNode() = default;

    /**
     * @brief 获取节点包围盒
     */
    virtual SpatialBounds<double, 3> getBounds() const = 0;

    /**
     * @brief 获取节点深度
     */
    virtual size_t getDepth() const = 0;

    /**
     * @brief 获取节点中的对象
     */
    virtual SpatialItemRefList getItems() const = 0;

    /**
     * @brief 检查是否为叶子节点
     */
    virtual bool isLeaf() const = 0;

    /**
     * @brief 获取子节点
     */
    virtual std::vector<const SpatialIndexNode*> getChildren() const = 0;

    /**
     * @brief 获取对象数量
     */
    virtual size_t getItemCount() const = 0;
};

/**
 * @brief 空间索引接口
 */
class SpatialIndex {
public:
    virtual ~SpatialIndex() = default;

    /**
     * @brief 获取根节点
     */
    virtual const SpatialIndexNode* getRootNode() const = 0;

    /**
     * @brief 获取所有对象
     */
    virtual SpatialItemRefList getAllItems() const = 0;

    /**
     * @brief 在指定范围内查询对象
     */
    virtual SpatialItemRefList query(const SpatialBounds<double, 3>& bounds) const = 0;

    /**
     * @brief 获取节点数量
     */
    virtual size_t getNodeCount() const = 0;

    /**
     * @brief 获取对象数量
     */
    virtual size_t getItemCount() const = 0;
};

/**
 * @brief 切片策略接口
 *
 * 定义了空间切片的统一接口
 * 四叉树和八叉树都实现此接口
 */
class SlicingStrategy {
public:
    virtual ~SlicingStrategy() = default;

    /**
     * @brief 获取策略维度 (2或3)
     */
    virtual size_t getDimension() const = 0;

    /**
     * @brief 获取子节点数量 (4或8)
     */
    virtual size_t getChildCount() const = 0;

    /**
     * @brief 构建空间索引
     * @param items 要索引的空间对象
     * @param worldBounds 世界包围盒
     * @param config 切片配置
     * @return 构建好的空间索引
     */
    virtual std::unique_ptr<SpatialIndex> buildIndex(
        const SpatialItemList& items,
        const SpatialBounds<double, 3>& worldBounds,
        const SlicingConfig& config
    ) = 0;

    /**
     * @brief 获取策略名称
     */
    virtual const char* getName() const = 0;
};

/**
 * @brief 切片策略工厂
 */
class SlicingStrategyFactory {
public:
    using StrategyCreator = std::function<std::unique_ptr<SlicingStrategy>()>;

    /**
     * @brief 注册策略
     */
    static void registerStrategy(const std::string& name, StrategyCreator creator);

    /**
     * @brief 创建策略
     */
    static std::unique_ptr<SlicingStrategy> create(const std::string& name);

    /**
     * @brief 获取所有可用策略名称
     */
    static std::vector<std::string> getAvailableStrategies();
};

/**
 * @brief 策略注册宏
 */
#define REGISTER_SLICING_STRATEGY(StrategyClass) \
    namespace { \
        struct StrategyClass##Registrar { \
            StrategyClass##Registrar() { \
                SlicingStrategyFactory::registerStrategy( \
                    StrategyClass().getName(), \
                    []() -> std::unique_ptr<SlicingStrategy> { \
                        return std::make_unique<StrategyClass>(); \
                    } \
                ); \
            } \
        } StrategyClass##instance; \
    }

} // namespace spatial::core

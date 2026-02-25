# 空间切片索引抽象技术方案

## 1. 概述

### 1.1 目标
抽象四叉树(Quadtree)和八叉树(Octree)的核心数据结构，提供统一的空间切片接口，使shp23dtile和FBXPipeline能够使用一致的策略进行空间划分和3D Tiles生成。

### 1.2 现状分析

#### 1.2.1 shp23dtile - 四叉树实现
- **文件**: `src/shp23dtile.cpp`
- **特点**:
  - 2D空间划分（XY平面）
  - 基于WGS84经纬度坐标
  - 每个节点包含4个子节点 (subnode[4])
  - 使用z/x/y编码标识瓦片位置
  - 支持metric阈值控制最小划分粒度
  - 业务数据结构：`shapefile::TileMeta`, `shapefile::TileBBox`, `shapefile::QuadtreeCoord`

#### 1.2.2 FBXPipeline - 八叉树实现
- **文件**: `src/FBXPipeline.h`, `src/FBXPipeline.cpp`
- **特点**:
  - 3D空间划分（XYZ空间）
  - 基于ENU局部坐标系（米）
  - 每个节点包含8个子节点
  - 使用深度优先递归构建
  - 支持maxDepth和maxItemsPerTile控制
  - 内嵌结构体：`OctreeNode`

### 1.3 设计原则
1. **策略模式**: 四叉树和八叉树作为两种切片策略实现统一接口
2. **类型安全**: 使用模板支持不同类型的空间对象
3. **可扩展性**: 易于添加新的切片策略（如KD树、R树等）
4. **零开销抽象**: 编译期多态，无运行时开销

---

## 2. 核心接口设计

### 2.1 命名空间规划
```cpp
namespace spatial {
    // 核心抽象接口
    namespace core {
        struct BoundingBox;           // 通用包围盒
        struct SpatialIndex;          // 空间索引基类
        struct SlicingStrategy;       // 切片策略接口
    }

    // 策略实现
    namespace strategy {
        struct QuadtreeStrategy;      // 四叉树策略
        struct OctreeStrategy;        // 八叉树策略
    }

    // 构建器
    namespace builder {
        struct TilesetBuilder;        // Tileset构建器
    }
}
```

### 2.2 核心数据结构

#### 2.2.1 通用包围盒 (SpatialBounds)
```cpp
namespace spatial::core {

/**
 * @brief 通用空间包围盒
 *
 * 支持2D和3D空间，使用模板参数区分
 */
template<typename T = double, size_t Dim = 3>
struct SpatialBounds {
    static_assert(Dim == 2 || Dim == 3, "Only 2D or 3D supported");

    std::array<T, Dim> min;
    std::array<T, Dim> max;

    // 2D构造器
    SpatialBounds(T minX, T minY, T maxX, T maxY)
        requires (Dim == 2);

    // 3D构造器
    SpatialBounds(T minX, T minY, T minZ, T maxX, T maxY, T maxZ)
        requires (Dim == 3);

    // 核心操作
    bool contains(const SpatialBounds& other) const;
    bool intersects(const SpatialBounds& other) const;
    SpatialBounds merge(const SpatialBounds& other) const;

    std::array<T, Dim> center() const;
    std::array<T, Dim> size() const;
    T volume() const;  // 2D返回面积，3D返回体积

    // 分割为子包围盒
    std::vector<SpatialBounds> split(size_t childCount) const;
};

// 类型别名
using Bounds2D = SpatialBounds<double, 2>;
using Bounds3D = SpatialBounds<double, 3>;

} // namespace spatial::core
```

#### 2.2.2 空间对象接口 (SpatialItem)
```cpp
namespace spatial::core {

/**
 * @brief 空间对象概念 (C++20 Concept)
 *
 * 任何可以被放入空间索引的对象必须满足此概念
 */
template<typename T, size_t Dim = 3>
concept SpatialItem = requires(T item) {
    { item.getBounds() } -> std::convertible_to<SpatialBounds<double, Dim>>;
    { item.getCenter() } -> std::convertible_to<std::array<double, Dim>>;
};

/**
 * @brief 空间对象包装器
 *
 * 用于将现有数据结构适配到空间索引系统
 */
template<typename T, size_t Dim = 3>
struct SpatialItemWrapper {
    T* item;
    std::function<SpatialBounds<double, Dim>(const T&)> boundsGetter;

    SpatialBounds<double, Dim> getBounds() const {
        return boundsGetter(*item);
    }

    std::array<double, Dim> getCenter() const {
        auto bounds = getBounds();
        return bounds.center();
    }
};

} // namespace spatial::core
```

### 2.3 切片策略接口

#### 2.3.1 切片配置 (SlicingConfig)
```cpp
namespace spatial::core {

/**
 * @brief 切片策略配置基类
 */
struct SlicingConfig {
    virtual ~SlicingConfig() = default;

    // 通用配置
    size_t maxDepth = 10;              // 最大深度
    size_t maxItemsPerNode = 1000;     // 每个节点最大对象数
    double minSize = 0.01;             // 最小划分尺寸（防止无限细分）
};

/**
 * @brief 四叉树专用配置
 */
struct QuadtreeConfig : SlicingConfig {
    // 四叉树特有配置
    bool useZOrderCurve = true;        // 是否使用Z序曲线编码
    bool strictContainment = false;    // 是否严格包含（对象跨边界时的处理）
};

/**
 * @brief 八叉树专用配置
 */
struct OctreeConfig : SlicingConfig {
    // 八叉树特有配置
    bool looseOctree = false;          // 是否使用松散八叉树
    float loosenessFactor = 1.0f;      // 松散系数
};

} // namespace spatial::core
```

#### 2.3.2 切片策略接口 (ISlicingStrategy)
```cpp
namespace spatial::core {

/**
 * @brief 切片策略接口
 *
 * 定义空间切片的通用操作
 */
template<typename ItemType, size_t Dim = 3>
class ISlicingStrategy {
public:
    using BoundsType = SpatialBounds<double, Dim>;
    using ItemRef = std::reference_wrapper<ItemType>;

    virtual ~ISlicingStrategy() = default;

    // 构建空间索引
    virtual void build(std::vector<ItemType>& items, const BoundsType& worldBounds) = 0;

    // 查询指定包围盒内的对象
    virtual std::vector<ItemRef> query(const BoundsType& bounds) const = 0;

    // 查询指定点的最近对象
    virtual std::vector<ItemRef> queryNearest(const std::array<double, Dim>& point,
                                               size_t maxResults) const = 0;

    // 获取所有叶子节点
    virtual std::vector<const void*> getLeafNodes() const = 0;

    // 获取节点深度
    virtual size_t getMaxDepth() const = 0;

    // 获取节点包围盒
    virtual BoundsType getNodeBounds(const void* node) const = 0;

    // 获取节点内对象
    virtual std::vector<ItemRef> getNodeItems(const void* node) const = 0;

    // 获取子节点
    virtual std::vector<const void*> getChildNodes(const void* node) const = 0;

    // 判断是否为叶子节点
    virtual bool isLeafNode(const void* node) const = 0;

    // 获取根节点
    virtual const void* getRootNode() const = 0;
};

} // namespace spatial::core
```

---

## 3. 四叉树实现方案

### 3.1 四叉树节点结构
```cpp
namespace spatial::strategy {

/**
 * @brief 四叉树节点
 */
template<typename ItemType>
struct QuadtreeNode {
    using BoundsType = core::SpatialBounds<double, 2>;
    using ItemRef = std::reference_wrapper<ItemType>;

    // 空间信息
    BoundsType bounds;
    size_t depth = 0;

    // 四叉树坐标 (z/x/y)
    uint32_t z = 0;
    uint32_t x = 0;
    uint32_t y = 0;

    // 内容
    std::vector<ItemRef> items;

    // 子节点 (4个或0个)
    std::array<std::unique_ptr<QuadtreeNode>, 4> children;

    // 状态
    bool isLeaf() const { return !children[0]; }

    // 编码为64位整数
    uint64_t encode() const {
        return (static_cast<uint64_t>(z) << 42) |
               (static_cast<uint64_t>(x) << 21) |
               static_cast<uint64_t>(y);
    }

    // 从编码解码
    static std::tuple<uint32_t, uint32_t, uint32_t> decode(uint64_t key) {
        return {
            static_cast<uint32_t>((key >> 42) & 0x1FFFFF),
            static_cast<uint32_t>((key >> 21) & 0x1FFFFF),
            static_cast<uint32_t>(key & 0x1FFFFF)
        };
    }
};

} // namespace spatial::strategy
```

### 3.2 四叉树策略实现
```cpp
namespace spatial::strategy {

/**
 * @brief 四叉树切片策略
 */
template<typename ItemType>
class QuadtreeStrategy : public core::ISlicingStrategy<ItemType, 2> {
public:
    using BaseType = core::ISlicingStrategy<ItemType, 2>;
    using BoundsType = typename BaseType::BoundsType;
    using ItemRef = typename BaseType::ItemRef;
    using NodeType = QuadtreeNode<ItemType>;
    using ConfigType = core::QuadtreeConfig;

    explicit QuadtreeStrategy(const ConfigType& config = {});

    // ISlicingStrategy 实现
    void build(std::vector<ItemType>& items, const BoundsType& worldBounds) override;
    std::vector<ItemRef> query(const BoundsType& bounds) const override;
    std::vector<ItemRef> queryNearest(const std::array<double, 2>& point,
                                       size_t maxResults) const override;
    std::vector<const void*> getLeafNodes() const override;
    size_t getMaxDepth() const override { return maxDepth_; }
    BoundsType getNodeBounds(const void* node) const override;
    std::vector<ItemRef> getNodeItems(const void* node) const override;
    std::vector<const void*> getChildNodes(const void* node) const override;
    bool isLeafNode(const void* node) const override;
    const void* getRootNode() const override { return root_.get(); }

    // 四叉树特有接口
    const NodeType* getNodeByCoord(uint32_t z, uint32_t x, uint32_t y) const;
    std::vector<const NodeType*> getNodesAtLevel(uint32_t level) const;

private:
    void splitNode(NodeType* node);
    void distributeItems(NodeType* node, std::vector<ItemRef>& items);
    void collectLeafNodes(const NodeType* node, std::vector<const void*>& leaves) const;
    void queryRecursive(const NodeType* node, const BoundsType& bounds,
                        std::vector<ItemRef>& results) const;

    std::unique_ptr<NodeType> root_;
    ConfigType config_;
    size_t maxDepth_ = 0;
};

} // namespace spatial::strategy
```

### 3.3 四叉树构建算法
```cpp
template<typename ItemType>
void QuadtreeStrategy<ItemType>::build(std::vector<ItemType>& items,
                                        const BoundsType& worldBounds) {
    // 1. 创建根节点
    root_ = std::make_unique<NodeType>();
    root_->bounds = worldBounds;
    root_->depth = 0;
    root_->z = 0;
    root_->x = 0;
    root_->y = 0;

    // 2. 将所有对象加入根节点
    std::vector<ItemRef> allItems;
    allItems.reserve(items.size());
    for (auto& item : items) {
        allItems.push_back(std::ref(item));
    }

    // 3. 递归构建
    buildRecursive(root_.get(), allItems);
}

template<typename ItemType>
void QuadtreeStrategy<ItemType>::buildRecursive(NodeType* node,
                                                 std::vector<ItemRef>& items) {
    // 1. 过滤不在节点范围内的对象
    std::vector<ItemRef> containedItems;
    for (auto& item : items) {
        if (node->bounds.intersects(item.get().getBounds())) {
            containedItems.push_back(item);
        }
    }

    // 2. 检查终止条件
    if (node->depth >= config_.maxDepth ||
        containedItems.size() <= config_.maxItemsPerNode ||
        node->bounds.size()[0] < config_.minSize) {
        node->items = std::move(containedItems);
        maxDepth_ = std::max(maxDepth_, node->depth);
        return;
    }

    // 3. 分割节点
    splitNode(node);

    // 4. 分发对象到子节点
    distributeItems(node, containedItems);

    // 5. 递归构建子节点
    for (auto& child : node->children) {
        if (!child->items.empty()) {
            std::vector<ItemRef> childItems = std::move(child->items);
            buildRecursive(child.get(), childItems);
        }
    }
}

template<typename ItemType>
void QuadtreeStrategy<ItemType>::splitNode(NodeType* node) {
    double cx = (node->bounds.min[0] + node->bounds.max[0]) * 0.5;
    double cy = (node->bounds.min[1] + node->bounds.max[1]) * 0.5;

    double minX = node->bounds.min[0];
    double minY = node->bounds.min[1];
    double maxX = node->bounds.max[0];
    double maxY = node->bounds.max[1];

    // 创建4个子节点 (Z序: SW, SE, NW, NE)
    // 0: SW (minX, minY) -> (cx, cy)
    node->children[0] = std::make_unique<NodeType>();
    node->children[0]->bounds = BoundsType(minX, minY, cx, cy);
    node->children[0]->depth = node->depth + 1;
    node->children[0]->z = node->z + 1;
    node->children[0]->x = node->x * 2;
    node->children[0]->y = node->y * 2;

    // 1: SE (cx, minY) -> (maxX, cy)
    node->children[1] = std::make_unique<NodeType>();
    node->children[1]->bounds = BoundsType(cx, minY, maxX, cy);
    node->children[1]->depth = node->depth + 1;
    node->children[1]->z = node->z + 1;
    node->children[1]->x = node->x * 2 + 1;
    node->children[1]->y = node->y * 2;

    // 2: NW (minX, cy) -> (cx, maxY)
    node->children[2] = std::make_unique<NodeType>();
    node->children[2]->bounds = BoundsType(minX, cy, cx, maxY);
    node->children[2]->depth = node->depth + 1;
    node->children[2]->z = node->z + 1;
    node->children[2]->x = node->x * 2;
    node->children[2]->y = node->y * 2 + 1;

    // 3: NE (cx, cy) -> (maxX, maxY)
    node->children[3] = std::make_unique<NodeType>();
    node->children[3]->bounds = BoundsType(cx, cy, maxX, maxY);
    node->children[3]->depth = node->depth + 1;
    node->children[3]->z = node->z + 1;
    node->children[3]->x = node->x * 2 + 1;
    node->children[3]->y = node->y * 2 + 1;
}
```

---

## 4. 八叉树实现方案

### 4.1 八叉树节点结构
```cpp
namespace spatial::strategy {

/**
 * @brief 八叉树节点
 */
template<typename ItemType>
struct OctreeNode {
    using BoundsType = core::SpatialBounds<double, 3>;
    using ItemRef = std::reference_wrapper<ItemType>;

    // 空间信息
    BoundsType bounds;
    size_t depth = 0;

    // 内容
    std::vector<ItemRef> items;

    // 子节点 (8个或0个)
    // 索引: 0=左下后, 1=右下后, 2=左上前, 3=右上前,
    //       4=左下前, 5=右下前, 6=左上后, 7=右上后
    std::array<std::unique_ptr<OctreeNode>, 8> children;

    // 状态
    bool isLeaf() const { return !children[0]; }
};

} // namespace spatial::strategy
```

### 4.2 八叉树策略实现
```cpp
namespace spatial::strategy {

/**
 * @brief 八叉树切片策略
 */
template<typename ItemType>
class OctreeStrategy : public core::ISlicingStrategy<ItemType, 3> {
public:
    using BaseType = core::ISlicingStrategy<ItemType, 3>;
    using BoundsType = typename BaseType::BoundsType;
    using ItemRef = typename BaseType::ItemRef;
    using NodeType = OctreeNode<ItemType>;
    using ConfigType = core::OctreeConfig;

    explicit OctreeStrategy(const ConfigType& config = {});

    // ISlicingStrategy 实现
    void build(std::vector<ItemType>& items, const BoundsType& worldBounds) override;
    std::vector<ItemRef> query(const BoundsType& bounds) const override;
    std::vector<ItemRef> queryNearest(const std::array<double, 3>& point,
                                       size_t maxResults) const override;
    std::vector<const void*> getLeafNodes() const override;
    size_t getMaxDepth() const override { return maxDepth_; }
    BoundsType getNodeBounds(const void* node) const override;
    std::vector<ItemRef> getNodeItems(const void* node) const override;
    std::vector<const void*> getChildNodes(const void* node) const override;
    bool isLeafNode(const void* node) const override;
    const void* getRootNode() const override { return root_.get(); }

private:
    void buildRecursive(NodeType* node, std::vector<ItemRef>& items);
    void splitNode(NodeType* node);
    void distributeItems(NodeType* node, std::vector<ItemRef>& items);
    void collectLeafNodes(const NodeType* node, std::vector<const void*>& leaves) const;
    void queryRecursive(const NodeType* node, const BoundsType& bounds,
                        std::vector<ItemRef>& results) const;

    std::unique_ptr<NodeType> root_;
    ConfigType config_;
    size_t maxDepth_ = 0;
};

} // namespace spatial::strategy
```

### 4.3 八叉树构建算法
```cpp
template<typename ItemType>
void OctreeStrategy<ItemType>::splitNode(NodeType* node) {
    double cx = (node->bounds.min[0] + node->bounds.max[0]) * 0.5;
    double cy = (node->bounds.min[1] + node->bounds.max[1]) * 0.5;
    double cz = (node->bounds.min[2] + node->bounds.max[2]) * 0.5;

    double minX = node->bounds.min[0];
    double minY = node->bounds.min[1];
    double minZ = node->bounds.min[2];
    double maxX = node->bounds.max[0];
    double maxY = node->bounds.max[1];
    double maxZ = node->bounds.max[2];

    // 创建8个子节点
    // 0: 左下后 (minX, minY, minZ) -> (cx, cy, cz)
    node->children[0] = std::make_unique<NodeType>();
    node->children[0]->bounds = BoundsType(minX, minY, minZ, cx, cy, cz);
    node->children[0]->depth = node->depth + 1;

    // 1: 右下后 (cx, minY, minZ) -> (maxX, cy, cz)
    node->children[1] = std::make_unique<NodeType>();
    node->children[1]->bounds = BoundsType(cx, minY, minZ, maxX, cy, cz);
    node->children[1]->depth = node->depth + 1;

    // 2: 左上前 (minX, cy, minZ) -> (cx, maxY, cz)
    node->children[2] = std::make_unique<NodeType>();
    node->children[2]->bounds = BoundsType(minX, cy, minZ, cx, maxY, cz);
    node->children[2]->depth = node->depth + 1;

    // 3: 右上前 (cx, cy, minZ) -> (maxX, maxY, cz)
    node->children[3] = std::make_unique<NodeType>();
    node->children[3]->bounds = BoundsType(cx, cy, minZ, maxX, maxY, cz);
    node->children[3]->depth = node->depth + 1;

    // 4: 左下前 (minX, minY, cz) -> (cx, cy, maxZ)
    node->children[4] = std::make_unique<NodeType>();
    node->children[4]->bounds = BoundsType(minX, minY, cz, cx, cy, maxZ);
    node->children[4]->depth = node->depth + 1;

    // 5: 右下前 (cx, minY, cz) -> (maxX, cy, maxZ)
    node->children[5] = std::make_unique<NodeType>();
    node->children[5]->bounds = BoundsType(cx, minY, cz, maxX, cy, maxZ);
    node->children[5]->depth = node->depth + 1;

    // 6: 左上后 (minX, cy, cz) -> (cx, maxY, maxZ)
    node->children[6] = std::make_unique<NodeType>();
    node->children[6]->bounds = BoundsType(minX, cy, cz, cx, maxY, maxZ);
    node->children[6]->depth = node->depth + 1;

    // 7: 右上后 (cx, cy, cz) -> (maxX, maxY, maxZ)
    node->children[7] = std::make_unique<NodeType>();
    node->children[7]->bounds = BoundsType(cx, cy, cz, maxX, maxY, maxZ);
    node->children[7]->depth = node->depth + 1;
}
```

---

## 5. Tileset构建器

### 5.1 构建器接口
```cpp
namespace spatial::builder {

/**
 * @brief Tileset构建配置
 */
struct TilesetBuildConfig {
    // 输出配置
    std::string outputPath;
    std::string contentPrefix = "tile";

    // 几何误差配置
    double geometricErrorScale = 0.5;
    double baseGeometricError = 1000.0;

    // 坐标系统配置
    double centerLongitude = 0.0;
    double centerLatitude = 0.0;
    double centerHeight = 0.0;

    // 内容生成回调
    using ContentGenerator = std::function<std::string(
        const void* node,
        const std::vector<std::reference_wrapper<void>>& items,
        const std::string& path
    )>;
    ContentGenerator contentGenerator;
};

/**
 * @brief Tileset构建器
 *
 * 将空间索引转换为3D Tiles Tileset
 */
class TilesetBuilder {
public:
    /**
     * @brief 从四叉树构建Tileset
     */
    template<typename ItemType>
    static tileset::Tileset buildFromQuadtree(
        const strategy::QuadtreeStrategy<ItemType>& strategy,
        const TilesetBuildConfig& config
    );

    /**
     * @brief 从八叉树构建Tileset
     */
    template<typename ItemType>
    static tileset::Tileset buildFromOctree(
        const strategy::OctreeStrategy<ItemType>& strategy,
        const TilesetBuildConfig& config
    );

private:
    template<typename StrategyType, typename ItemType>
    static tileset::Tileset buildInternal(
        const StrategyType& strategy,
        const TilesetBuildConfig& config
    );

    template<typename StrategyType, typename ItemType>
    static tileset::Tile buildTileRecursive(
        const StrategyType& strategy,
        const void* node,
        const std::string& path,
        const TilesetBuildConfig& config
    );
};

} // namespace spatial::builder
```

---

## 6. 文件结构规划

```
src/spatial/
├── core/
│   ├── spatial_bounds.h          # 通用包围盒
│   ├── spatial_bounds.cpp
│   ├── spatial_item.h            # 空间对象接口
│   ├── slicing_strategy.h        # 切片策略接口
│   └── slicing_config.h          # 切片配置
├── strategy/
│   ├── quadtree_node.h           # 四叉树节点
│   ├── quadtree_strategy.h       # 四叉树策略
│   ├── quadtree_strategy.cpp
│   ├── octree_node.h             # 八叉树节点
│   ├── octree_strategy.h         # 八叉树策略
│   └── octree_strategy.cpp
├── builder/
│   ├── tileset_builder.h         # Tileset构建器
│   └── tileset_builder.cpp
└── utils/
    ├── coordinate_utils.h        # 坐标转换工具
    └── encoding_utils.h          # 编码工具 (Z序等)
```

---

## 7. 关键技术点

### 7.1 对象分发策略
1. **中心点法**: 使用对象中心点决定归属（简单，无重复）
2. **完全包含法**: 对象必须完全包含在子节点内（严格，可能保留在父节点）
3. **交集法**: 对象与多个子节点相交时复制到所有相交节点（查询精确，内存开销大）

### 7.2 几何误差计算
```cpp
double computeGeometricError(const SpatialBounds& bounds) {
    auto size = bounds.size();
    double diagonal = std::sqrt(
        size[0] * size[0] +
        size[1] * size[1] +
        (size.size() > 2 ? size[2] * size[2] : 0)
    );
    return diagonal * config.geometricErrorScale;
}
```

### 7.3 坐标系统处理
- **四叉树**: WGS84 (度) -> ENU (米) -> ECEF
- **八叉树**: ENU (米) -> ECEF

---

## 8. 性能考虑

1. **内存布局**: 使用SOA (Structure of Arrays) 优化缓存友好性
2. **并行构建**: 使用OpenMP或TBB并行化节点分割
3. **延迟加载**: 支持按需加载深层节点
4. **空间压缩**: 使用Z序曲线优化空间局部性

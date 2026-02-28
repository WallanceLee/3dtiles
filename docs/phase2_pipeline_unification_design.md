# 第二阶段：统一管道架构重构方案

> **规范**: C++20, Google C++ Style Guide, C++ Core Guidelines
> **核心原则**: RAII, 零开销抽象, const 正确性, 智能指针优先
> **重构策略**: 渐进式接口抽象（Strangler Fig Pattern）

## 1. 目标

在第一阶段基础上，进一步完善抽象层设计，实现 Shapefile 和 FBX 处理流程的完全一致性。

## 2. 当前问题分析

### 2.1 现有实现的问题

```
┌─────────────────────────────────────────────────────────────┐
│                     当前架构 (第一阶段)                      │
├─────────────────────────────────────────────────────────────┤
│  ShapefilePipeline          FBXPipeline                     │
│       │                          │                          │
│       ▼                          ▼                          │
│  ShapefileProcessor         FBXPipeline::run()              │
│  (四叉树内部实现)            (八叉树内部实现)                │
│       │                          │                          │
│       ▼                          ▼                          │
│   B3DM生成                   B3DM生成                       │
│  (内部实现不同)              (内部实现不同)                  │
└─────────────────────────────────────────────────────────────┘

问题：
1. 空间索引策略内聚在具体处理器中，无法复用
2. B3DM 生成逻辑分散，缺乏统一接口
3. 数据处理流程不一致，维护成本高
```

### 2.2 目标架构

```
┌─────────────────────────────────────────────────────────────┐
│                     目标架构 (第二阶段)                      │
├─────────────────────────────────────────────────────────────┤
│                    UnifiedPipeline                          │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐         │
│  │ LoadData()  │→ │BuildSpatial │→ │GenerateB3DM │→ ...     │
│  └─────────────┘  │   Index()   │  │   Files()   │         │
│                   └─────────────┘  └─────────────┘         │
├─────────────────────────────────────────────────────────────┤
│  DataSource (抽象)                                          │
│  ├─ ShapefileDataSource ──→ ShapefileDataPool              │
│  └─ FBXDataSource ────────→ FBXLoader                      │
├─────────────────────────────────────────────────────────────┤
│  SpatialIndex (抽象)                                        │
│  ├─ QuadtreeIndex ────────→ QuadtreeStrategy               │
│  └─ OctreeIndex ──────────→ OctreeStrategy                 │
├─────────────────────────────────────────────────────────────┤
│  IB3DMGenerator (抽象)                                      │
│  ├─ ShapefileB3DMGenerator ─→ B3DMContentGenerator         │
│  └─ FBXB3DMGenerator ───────→ B3DMGenerator                │
├─────────────────────────────────────────────────────────────┤
│  ITilesetBuilder (抽象)                                     │
│  └─ StandardTilesetBuilder ─→ common::TilesetBuilder       │
└─────────────────────────────────────────────────────────────┘
```

## 3. 核心设计

### 3.1 统一处理流程

```cpp
// 模板方法模式 - C++20 概念约束
class ConversionPipeline {
 public:
    virtual ~ConversionPipeline() = default;

    // 禁止拷贝，允许移动
    ConversionPipeline(const ConversionPipeline&) = delete;
    ConversionPipeline& operator=(const ConversionPipeline&) = delete;
    ConversionPipeline(ConversionPipeline&&) = default;
    ConversionPipeline& operator=(ConversionPipeline&&) = default;

    // 模板方法 - 统一处理流程
    [[nodiscard]] auto Convert(const ConversionParams& params) -> ConversionResult {
        // 1. 加载数据
        auto data_source = LoadData(params);
        if (!data_source) {
            return ConversionResult{.success = false,
                                   .error_message = "Failed to load data"};
        }

        // 2. 构建空间索引
        auto spatial_index = BuildSpatialIndex(data_source.get(), params);
        if (!spatial_index) {
            return ConversionResult{.success = false,
                                   .error_message = "Failed to build spatial index"};
        }

        // 3. 生成 B3DM 文件
        auto b3dm_files = GenerateB3DMFiles(data_source.get(),
                                           spatial_index.get(), params);
        if (b3dm_files.empty()) {
            return ConversionResult{.success = false,
                                   .error_message = "No B3DM files generated"};
        }

        // 4. 生成 Tileset
        auto tileset_path = GenerateTileset(spatial_index.get(),
                                           b3dm_files, params);
        if (tileset_path.empty()) {
            return ConversionResult{.success = false,
                                   .error_message = "Failed to generate tileset"};
        }

        return ConversionResult{
            .success = true,
            .node_count = static_cast<int>(spatial_index->GetNodeCount()),
            .b3dm_count = static_cast<int>(b3dm_files.size()),
            .tileset_path = std::move(tileset_path)
        };
    }

 protected:
    ConversionPipeline() = default;

    // 纯虚函数 - 子类实现
    [[nodiscard]] virtual auto LoadData(const ConversionParams& params)
        -> std::unique_ptr<DataSource> = 0;

    [[nodiscard]] virtual auto BuildSpatialIndex(DataSource* source,
                                                  const ConversionParams& params)
        -> std::unique_ptr<SpatialIndex> = 0;

    [[nodiscard]] virtual auto GenerateB3DMFiles(
        DataSource* source,
        SpatialIndex* index,
        const ConversionParams& params) -> std::vector<B3DMFile> = 0;

    [[nodiscard]] virtual auto GenerateTileset(
        SpatialIndex* index,
        const std::vector<B3DMFile>& files,
        const ConversionParams& params) -> std::filesystem::path = 0;
};
```

### 3.2 DataSource 抽象接口

```cpp
// pipeline/data_source.h
#pragma once

#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <unordered_map>
#include <filesystem>

namespace pipeline {

// 前向声明
class ISpatialItem;
using SpatialItemPtr = std::shared_ptr<ISpatialItem>;
using SpatialItemList = std::vector<SpatialItemPtr>;

// 数据源配置 - 使用聚合初始化
struct DataSourceConfig {
    std::filesystem::path input_path;
    std::filesystem::path output_path;

    // 地理参考
    double center_longitude = 0.0;
    double center_latitude = 0.0;
    double center_height = 0.0;

    // Shapefile 特定
    std::string height_field;

    // 处理选项
    bool enable_simplification = false;
    bool enable_draco = false;
    bool enable_lod = false;
};

// 空间项接口 - 统一表示几何对象
class ISpatialItem {
 public:
    virtual ~ISpatialItem() = default;

    // 禁止拷贝，允许移动
    ISpatialItem(const ISpatialItem&) = delete;
    ISpatialItem& operator=(const ISpatialItem&) = delete;
    ISpatialItem(ISpatialItem&&) = default;
    ISpatialItem& operator=(ISpatialItem&&) = default;

    // 获取唯一ID
    [[nodiscard]] virtual auto GetId() const -> uint64_t = 0;

    // 获取包围盒 (WGS84 经纬度/高度)
    [[nodiscard]] virtual auto GetBounds() const
        -> std::tuple<double, double, double, double, double, double> = 0;

    // 获取几何数据 (OSG 格式)
    [[nodiscard]] virtual auto GetGeometry() const
        -> osg::ref_ptr<osg::Geometry> = 0;

    // 获取属性数据
    [[nodiscard]] virtual auto GetProperties() const
        -> std::unordered_map<std::string, std::string> = 0;

 protected:
    ISpatialItem() = default;
};

// 数据源接口
class DataSource {
 public:
    virtual ~DataSource() = default;

    // 禁止拷贝，允许移动
    DataSource(const DataSource&) = delete;
    DataSource& operator=(const DataSource&) = delete;
    DataSource(DataSource&&) = default;
    DataSource& operator=(DataSource&&) = default;

    // 加载数据
    [[nodiscard]] virtual auto Load(const DataSourceConfig& config) -> bool = 0;

    // 获取空间项列表
    [[nodiscard]] virtual auto GetSpatialItems() const -> SpatialItemList = 0;

    // 获取世界包围盒
    [[nodiscard]] virtual auto GetWorldBounds() const
        -> std::tuple<double, double, double, double, double, double> = 0;

    // 获取地理参考
    [[nodiscard]] virtual auto GetGeoReference() const
        -> std::tuple<double, double, double> = 0;

    // 获取数据项数量
    [[nodiscard]] virtual auto GetItemCount() const noexcept -> std::size_t = 0;

    // 是否已加载
    [[nodiscard]] virtual auto IsLoaded() const noexcept -> bool = 0;

 protected:
    DataSource() = default;
};

using DataSourcePtr = std::unique_ptr<DataSource>;
using DataSourceCreator = std::function<DataSourcePtr()>;

// 数据源工厂 - 单例注册模式
class DataSourceFactory {
 public:
    [[nodiscard]] static auto Instance() noexcept -> DataSourceFactory&;

    void Register(std::string_view type, DataSourceCreator creator);
    [[nodiscard]] auto Create(std::string_view type) const -> DataSourcePtr;
    [[nodiscard]] auto IsRegistered(std::string_view type) const noexcept -> bool;

 private:
    DataSourceFactory() = default;
    ~DataSourceFactory() = default;

    std::unordered_map<std::string, DataSourceCreator> creators_;
};

// 数据源注册辅助宏
#define REGISTER_DATA_SOURCE(TYPE, CLASS)                                    \
    namespace {                                                              \
        [[maybe_unused]] const bool _##CLASS##_registered = []() -> bool {   \
            ::pipeline::DataSourceFactory::Instance().Register(              \
                TYPE, []() -> ::pipeline::DataSourcePtr {                    \
                    return std::make_unique<CLASS>();                        \
                });                                                          \
            return true;                                                     \
        }();                                                                 \
    }

} // namespace pipeline
```

### 3.3 SpatialIndexStrategy 抽象

```cpp
// pipeline/spatial_index_strategy.h
#pragma once

#include <functional>
#include <memory>
#include <vector>
#include <cstdint>

namespace pipeline {

// 前向声明
class ISpatialItem;
using SpatialItemPtr = std::shared_ptr<ISpatialItem>;
using SpatialItemList = std::vector<SpatialItemPtr>;

// 空间索引节点接口
class ISpatialIndexNode {
 public:
    virtual ~ISpatialIndexNode() = default;

    // 禁止拷贝，允许移动
    ISpatialIndexNode(const ISpatialIndexNode&) = delete;
    ISpatialIndexNode& operator=(const ISpatialIndexNode&) = delete;
    ISpatialIndexNode(ISpatialIndexNode&&) = default;
    ISpatialIndexNode& operator=(ISpatialIndexNode&&) = default;

    // 获取节点ID
    [[nodiscard]] virtual auto GetId() const -> uint64_t = 0;

    // 获取深度
    [[nodiscard]] virtual auto GetDepth() const -> int = 0;

    // 获取包围盒
    [[nodiscard]] virtual auto GetBounds() const
        -> std::tuple<double, double, double, double, double, double> = 0;

    // 是否是叶子节点
    [[nodiscard]] virtual auto IsLeaf() const -> bool = 0;

    // 获取子节点
    [[nodiscard]] virtual auto GetChildren() const
        -> std::vector<const ISpatialIndexNode*> = 0;

    // 获取包含的空间项ID
    [[nodiscard]] virtual auto GetItemIds() const -> std::vector<uint64_t> = 0;

 protected:
    ISpatialIndexNode() = default;
};

// 空间索引配置
struct SpatialIndexConfig {
    int max_depth = 5;
    std::size_t max_items_per_node = 1000;
};

// 空间索引接口
class SpatialIndex {
 public:
    virtual ~SpatialIndex() = default;

    // 禁止拷贝，允许移动
    SpatialIndex(const SpatialIndex&) = delete;
    SpatialIndex& operator=(const SpatialIndex&) = delete;
    SpatialIndex(SpatialIndex&&) = default;
    SpatialIndex& operator=(SpatialIndex&&) = default;

    // 构建索引
    [[nodiscard]] virtual auto Build(const SpatialItemList& items,
                                    const SpatialIndexConfig& config) -> bool = 0;

    // 获取根节点
    [[nodiscard]] virtual auto GetRoot() const -> const ISpatialIndexNode* = 0;

    // 遍历所有叶子节点
    virtual auto ForEachLeaf(
        const std::function<void(const ISpatialIndexNode*)>& callback) const -> void = 0;

    // 获取节点数量
    [[nodiscard]] virtual auto GetNodeCount() const -> std::size_t = 0;

    // 获取叶子节点数量
    [[nodiscard]] virtual auto GetLeafCount() const -> std::size_t = 0;

 protected:
    SpatialIndex() = default;
};

using SpatialIndexPtr = std::unique_ptr<SpatialIndex>;
using SpatialIndexCreator = std::function<SpatialIndexPtr()>;

// 空间索引策略类型
enum class SpatialStrategyType : std::uint8_t {
    kQuadtree = 0,
    kOctree = 1
};

// 空间索引工厂
class SpatialIndexFactory {
 public:
    [[nodiscard]] static auto Instance() noexcept -> SpatialIndexFactory&;

    void Register(SpatialStrategyType type, SpatialIndexCreator creator);
    [[nodiscard]] auto Create(SpatialStrategyType type) const -> SpatialIndexPtr;

 private:
    SpatialIndexFactory() = default;
    ~SpatialIndexFactory() = default;

    std::unordered_map<SpatialStrategyType, SpatialIndexCreator> creators_;
};

#define REGISTER_SPATIAL_STRATEGY(TYPE, CLASS)                               \
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
```

### 3.4 B3DMGenerator 统一接口

```cpp
// pipeline/b3dm_generator.h
#pragma once

#include <memory>
#include <vector>
#include <string>
#include <filesystem>

namespace pipeline {

// 前向声明
class ISpatialIndexNode;
class ISpatialItem;
using SpatialItemPtr = std::shared_ptr<ISpatialItem>;
using SpatialItemList = std::vector<SpatialItemPtr>;

// B3DM 生成配置 - 使用聚合初始化
struct B3DMGenerationConfig {
    std::filesystem::path output_directory;

    // 地理参考
    double center_longitude = 0.0;
    double center_latitude = 0.0;
    double center_height = 0.0;

    // 压缩选项
    bool enable_draco = false;
    bool enable_texture_compress = false;
    bool enable_meshopt = false;

    // LOD 选项
    bool enable_lod = false;
    std::vector<float> lod_ratios = {1.0f, 0.5f, 0.25f};
};

// B3DM 文件信息
struct B3DMFile {
    std::filesystem::path filepath;
    std::filesystem::path relative_path;
    uint64_t node_id = 0;
    int lod_level = 0;
    std::size_t file_size = 0;
};

// B3DM 生成器接口
class IB3DMGenerator {
 public:
    virtual ~IB3DMGenerator() = default;

    // 禁止拷贝，允许移动
    IB3DMGenerator(const IB3DMGenerator&) = delete;
    IB3DMGenerator& operator=(const IB3DMGenerator&) = delete;
    IB3DMGenerator(IB3DMGenerator&&) = default;
    IB3DMGenerator& operator=(IB3DMGenerator&&) = default;

    // 初始化
    [[nodiscard]] virtual auto Initialize(const B3DMGenerationConfig& config) -> bool = 0;

    // 为单个节点生成 B3DM
    [[nodiscard]] virtual auto GenerateForNode(
        const ISpatialIndexNode* node,
        const SpatialItemList& items,
        std::string_view filename) -> B3DMFile = 0;

    // 批量生成（支持 LOD）
    [[nodiscard]] virtual auto GenerateWithLOD(
        const ISpatialIndexNode* node,
        const SpatialItemList& items,
        std::string_view base_filename) -> std::vector<B3DMFile> = 0;

 protected:
    IB3DMGenerator() = default;
};

using B3DMGeneratorPtr = std::unique_ptr<IB3DMGenerator>;

} // namespace pipeline
```

### 3.5 TilesetBuilder 统一接口

```cpp
// pipeline/tileset_builder.h
#pragma once

#include "spatial_index_strategy.h"
#include "b3dm_generator.h"
#include <nlohmann/json.hpp>

namespace pipeline {

// Tileset 节点元数据
struct TileNodeMetadata {
    uint64_t node_id = 0;
    uint64_t parent_id = 0;
    int depth = 0;

    // 包围盒
    double bbox_min[3] = {0, 0, 0};
    double bbox_max[3] = {0, 0, 0};

    // 几何误差
    double geometric_error = 0.0;

    // 内容
    std::vector<std::string> content_uris;
    bool has_content = false;

    // 子节点
    std::vector<uint64_t> children_ids;
    bool is_leaf = false;
};

using TileNodeMetadataMap = std::unordered_map<uint64_t, TileNodeMetadata>;

// Tileset 配置
struct TilesetBuilderConfig {
    std::filesystem::path output_path;

    // 地理参考
    double center_longitude = 0.0;
    double center_latitude = 0.0;
    double center_height = 0.0;

    // 几何误差计算
    double geometric_error_scale = 0.5;
    double geometric_error_base = 1.0;

    // 包围盒扩展
    double bounding_volume_scale = 1.0;

    // LOD
    bool enable_lod = false;
    int lod_level_count = 3;
};

// Tileset 构建器接口
class ITilesetBuilder {
 public:
    virtual ~ITilesetBuilder() = default;

    // 初始化
    virtual bool Initialize(const TilesetBuilderConfig& config) = 0;

    // 添加节点元数据
    virtual void AddNodeMetadata(const TileNodeMetadata& metadata) = 0;

    // 构建并写入 Tileset
    virtual bool BuildAndWrite(
        const SpatialIndex* index,
        const std::vector<B3DMFile>& b3dm_files
    ) = 0;

    // 获取生成的 tileset.json 路径
    virtual std::filesystem::path GetTilesetPath() const = 0;
};

using TilesetBuilderPtr = std::unique_ptr<ITilesetBuilder>;

} // namespace pipeline
```

### 3.6 统一管道实现

```cpp
// pipeline/unified_pipeline.h
#pragma once

#include "conversion_pipeline.h"
#include "spatial_index_strategy.h"

namespace pipeline {

// 统一管道配置
struct UnifiedPipelineConfig {
    std::string source_type;  // "shapefile" or "fbx"
    DataSourceConfig data_source_config;
    SpatialIndexConfig spatial_index_config;
    B3DMGenerationConfig b3dm_generator_config;
    TilesetBuilderConfig tileset_builder_config;
};

// 统一管道 - 使用策略模式处理不同数据源
class UnifiedPipeline : public ConversionPipeline {
 public:
    explicit UnifiedPipeline(const UnifiedPipelineConfig& config);
    ~UnifiedPipeline() override;

    // 禁止拷贝，允许移动
    UnifiedPipeline(const UnifiedPipeline&) = delete;
    UnifiedPipeline& operator=(const UnifiedPipeline&) = delete;
    UnifiedPipeline(UnifiedPipeline&&) = default;
    UnifiedPipeline& operator=(UnifiedPipeline&&) = default;

    // 执行完整转换流程
    ConversionResult Execute();

    // 分步执行（用于调试）
    bool LoadData();
    bool BuildSpatialIndex();
    bool GenerateB3DMFiles();
    bool BuildTileset();

 protected:
    [[nodiscard]] auto LoadData(const ConversionParams& params)
        -> std::unique_ptr<DataSource> override;

    [[nodiscard]] auto BuildSpatialIndex(DataSource* source,
                                          const ConversionParams& params)
        -> std::unique_ptr<SpatialIndex> override;

    [[nodiscard]] auto GenerateB3DMFiles(DataSource* source,
                                          SpatialIndex* index,
                                          const ConversionParams& params)
        -> std::vector<B3DMFile> override;

    [[nodiscard]] auto GenerateTileset(SpatialIndex* index,
                                        const std::vector<B3DMFile>& files,
                                        const ConversionParams& params)
        -> std::filesystem::path override;

 private:
    UnifiedPipelineConfig config_;

    // 组件
    DataSourcePtr dataSource_;
    SpatialIndexPtr spatialIndex_;
    B3DMGeneratorPtr b3dmGenerator_;
    TilesetBuilderPtr tilesetBuilder_;

    // 执行状态
    std::vector<ISpatialItem*> spatialItems_;
    std::vector<B3DMFile> b3dmFiles_;

    // 工厂方法
    DataSourcePtr CreateDataSource();
    SpatialIndexPtr CreateSpatialIndex();
    B3DMGeneratorPtr CreateB3DMGenerator();
    TilesetBuilderPtr CreateTilesetBuilder();
};

} // namespace pipeline
```

## 4. 渐进式重构步骤

采用 **Strangler Fig Pattern（绞杀者模式）**：
1. 每次只抽象一个接口
2. 新接口包装旧实现（适配器模式）
3. 验证通过后再抽象下一个接口
4. 最终达到统一架构

### 步骤 1：抽象 DataSource 接口

#### 4.1.1 Shapefile 数据源实现（包装现有代码）

```cpp
// pipeline/adapters/shapefile/shapefile_data_source.h
#pragma once

#include "../../data_source.h"
#include "../../../shapefile/shapefile_data_pool.h"

namespace pipeline {
namespace adapters {

// Shapefile 空间项适配器
class ShapefileSpatialItemAdapter : public ISpatialItem {
public:
    explicit ShapefileSpatialItemAdapter(shapefile::ShapefileDataPool::ItemPtr item)
        : item_(item) {}

    uint64_t GetId() const override {
        return static_cast<uint64_t>(item_->featureId);
    }

    void GetBounds(double& minX, double& minY, double& minZ,
                  double& maxX, double& maxY, double& maxZ) const override {
        minX = item_->bounds.minx;
        minY = item_->bounds.miny;
        minZ = item_->bounds.minHeight;
        maxX = item_->bounds.maxx;
        maxY = item_->bounds.maxy;
        maxZ = item_->bounds.maxHeight;
    }

    osg::ref_ptr<osg::Geometry> GetGeometry() const override {
        if (item_->geometries.empty()) return nullptr;
        if (item_->geometries.size() == 1) return item_->geometries[0];
        // 多个几何体合并逻辑...
        return osg::ref_ptr<osg::Geometry>();
    }

    std::map<std::string, std::string> GetProperties() const override {
        std::map<std::string, std::string> props;
        for (const auto& [key, value] : item_->properties) {
            props[key] = value.dump();
        }
        return props;
    }

    const shapefile::ShapefileSpatialItem* GetOriginalItem() const {
        return item_.get();
    }

private:
    shapefile::ShapefileDataPool::ItemPtr item_;
};

// Shapefile 数据源
class ShapefileDataSource : public DataSource {
public:
    ShapefileDataSource() = default;

    bool Load(const DataSourceConfig& config) override {
        dataPool_ = std::make_unique<shapefile::ShapefileDataPool>();

        // 完全复用现有的加载逻辑
        bool result = dataPool_->loadFromShapefileWithGeometry(
            config.input_path.string(),
            config.height_field,
            config.center_longitude,
            config.center_latitude
        );

        if (result) {
            // 重新计算中心点（复用现有逻辑）
            auto worldBounds = dataPool_->computeWorldBounds();
            double centerLon = (worldBounds.minx + worldBounds.maxx) * 0.5;
            double centerLat = (worldBounds.miny + worldBounds.maxy) * 0.5;

            if (std::abs(centerLon - config.center_longitude) > 1.0 ||
                std::abs(centerLat - config.center_latitude) > 1.0) {
                dataPool_->clear();
                result = dataPool_->loadFromShapefileWithGeometry(
                    config.input_path.string(),
                    config.height_field,
                    centerLon,
                    centerLat
                );
            }
        }

        // 转换为适配器列表
        if (result) {
            items_.clear();
            for (const auto& item : dataPool_->getAllItems()) {
                items_.push_back(std::make_shared<ShapefileSpatialItemAdapter>(item));
            }
        }

        return result;
    }

    SpatialItemList GetSpatialItems() const override { return items_; }

    void GetWorldBounds(double& minX, double& minY, double& minZ,
                       double& maxX, double& maxY, double& maxZ) const override {
        auto bounds = dataPool_->computeWorldBounds();
        minX = bounds.minx; minY = bounds.miny; minZ = bounds.minHeight;
        maxX = bounds.maxx; maxY = bounds.maxy; maxZ = bounds.maxHeight;
    }

    size_t GetItemCount() const override { return items_.size(); }
    bool IsLoaded() const override { return dataPool_ && !items_.empty(); }

    shapefile::ShapefileDataPool* GetOriginalPool() const {
        return dataPool_.get();
    }

private:
    std::unique_ptr<shapefile::ShapefileDataPool> dataPool_;
    SpatialItemList items_;
};

} // namespace adapters
} // namespace pipeline
```

#### 4.1.2 FBX 数据源实现（包装现有代码）

```cpp
// pipeline/adapters/fbx/fbx_data_source.h
#pragma once

#include "../../data_source.h"
#include "../../../fbx.h"
#include "../../../fbx/fbx_spatial_item_adapter.h"

namespace pipeline {
namespace adapters {

// FBX 空间项适配器
class FBXSpatialItemAdapter : public ISpatialItem {
public:
    explicit FBXSpatialItemAdapter(fbx::FBXSpatialItemPtr item)
        : item_(item) {}

    uint64_t GetId() const override {
        return reinterpret_cast<uint64_t>(item_->getMeshInfo());
    }

    void GetBounds(double& minX, double& minY, double& minZ,
                  double& maxX, double& maxY, double& maxZ) const override {
        auto bounds = item_->getBounds();
        auto min = bounds.min();
        auto max = bounds.max();
        minX = min[0]; minY = min[1]; minZ = min[2];
        maxX = max[0]; maxY = max[1]; maxZ = max[2];
    }

    osg::ref_ptr<osg::Geometry> GetGeometry() const override {
        return osg::ref_ptr<osg::Geometry>(
            const_cast<osg::Geometry*>(item_->getGeometry())
        );
    }

    std::map<std::string, std::string> GetProperties() const override {
        std::map<std::string, std::string> props;
        props["nodeName"] = item_->getNodeName();
        return props;
    }

    fbx::FBXSpatialItemPtr GetOriginalItem() const { return item_; }

private:
    fbx::FBXSpatialItemPtr item_;
};

// FBX 数据源
class FBXDataSource : public DataSource {
public:
    FBXDataSource() = default;

    bool Load(const DataSourceConfig& config) override {
        loader_ = std::make_unique<FBXLoader>(config.input_path.string());

        if (!loader_->load()) {
            return false;
        }

        auto fbxItems = fbx::createSpatialItems(loader_.get());

        items_.clear();
        for (const auto& item : fbxItems) {
            items_.push_back(std::make_shared<FBXSpatialItemAdapter>(item));
        }

        return !items_.empty();
    }

    SpatialItemList GetSpatialItems() const override { return items_; }

    void GetWorldBounds(double& minX, double& minY, double& minZ,
                       double& maxX, double& maxY, double& maxZ) const override {
        if (items_.empty()) return;
        minX = minY = minZ = std::numeric_limits<double>::max();
        maxX = maxY = maxZ = std::numeric_limits<double>::lowest();

        for (const auto& item : items_) {
            double ixmin, iymin, izmin, ixmax, iymax, izmax;
            item->GetBounds(ixmin, iymin, izmin, ixmax, iymax, izmax);
            minX = std::min(minX, ixmin);
            minY = std::min(minY, iymin);
            minZ = std::min(minZ, izmin);
            maxX = std::max(maxX, ixmax);
            maxY = std::max(maxY, iymax);
            maxZ = std::max(maxZ, izmax);
        }
    }

    size_t GetItemCount() const override { return items_.size(); }
    bool IsLoaded() const override { return loader_ && !items_.empty(); }

    FBXLoader* GetOriginalLoader() const { return loader_.get(); }

private:
    std::unique_ptr<FBXLoader> loader_;
    SpatialItemList items_;
};

} // namespace adapters
} // namespace pipeline
```

#### 4.1.3 修改 ShapefileProcessor 使用新接口

```cpp
// shapefile/shapefile_processor.h（步骤1后）
class ShapefileProcessor {
public:
    // 新增：使用外部数据源
    void SetDataSource(std::shared_ptr<pipeline::DataSource> dataSource) {
        externalDataSource_ = dataSource;
    }

private:
    std::shared_ptr<pipeline::DataSource> externalDataSource_;
    std::unique_ptr<ShapefileDataPool> internalDataPool_;
};
```

#### 4.1.4 验证步骤 1

```cpp
TEST(Step1_DataSource, ShapefileProcessorWithNewDataSource) {
    shapefile::ShapefileProcessorConfig config;
    config.inputPath = "tests/data/sample.shp";
    config.outputPath = "tests/output/step1_shapefile";
    config.heightField = "height";

    shapefile::ShapefileProcessor processor(config);

    // 使用新的数据源
    auto dataSource = std::make_unique<pipeline::adapters::ShapefileDataSource>();
    processor.SetDataSource(dataSource);

    auto result = processor.process();
    EXPECT_TRUE(result.success);

    // 对比基准数据
    CompareWithBenchmark("tests/output/step1_shapefile", "tests/reference/shapefile");
}
```

### 步骤 2：抽象 SpatialIndex 接口

#### 4.2.1 四叉树实现（包装现有代码）

```cpp
// pipeline/adapters/spatial/quadtree_index.h
#pragma once

#include "../../spatial_index_strategy.h"
#include "../../../spatial/strategy/quadtree_strategy.h"

namespace pipeline {
namespace adapters {

class QuadtreeNodeAdapter : public ISpatialIndexNode {
public:
    explicit QuadtreeNodeAdapter(const spatial::strategy::QuadtreeNode* node, uint64_t id)
        : node_(node), id_(id) {}

    uint64_t GetId() const override { return id_; }
    int GetDepth() const override { return static_cast<int>(node_->getDepth()); }

    void GetBounds(double& minX, double& minY, double& minZ,
                  double& maxX, double& maxY, double& maxZ) const override {
        auto bounds = node_->getBounds();
        auto min = bounds.min();
        auto max = bounds.max();
        minX = min[0]; minY = min[1]; minZ = min[2];
        maxX = max[0]; maxY = max[1]; maxZ = max[2];
    }

    bool IsLeaf() const override { return node_->isLeaf(); }
    std::vector<const ISpatialIndexNode*> GetChildren() const override;
    std::vector<uint64_t> GetItemIds() const override;
    size_t GetItemCount() const override { return node_->getItemCount(); }

    const spatial::strategy::QuadtreeNode* GetOriginalNode() const { return node_; }

private:
    const spatial::strategy::QuadtreeNode* node_;
    uint64_t id_;
};

class QuadtreeIndex : public SpatialIndex {
public:
    QuadtreeIndex() = default;

    bool Build(const SpatialItemList& items, const SpatialIndexConfig& config) override {
        // 转换为现有接口需要的格式
        spatial::core::SpatialItemList spatialItems;
        for (const auto& item : items) {
            // 类型转换...
        }

        spatial::strategy::QuadtreeStrategy strategy;
        spatial::strategy::QuadtreeConfig qtConfig;
        qtConfig.maxDepth = config.max_depth;
        qtConfig.maxItemsPerNode = config.max_items_per_node;

        index_ = strategy.buildIndex(spatialItems, worldBounds, qtConfig);
        BuildNodeMap();

        return index_ != nullptr;
    }

    const ISpatialIndexNode* GetRoot() const override {
        if (!index_) return nullptr;
        return GetNodeAdapter(index_->getRootNode());
    }

    void ForEachLeaf(const std::function<void(const ISpatialIndexNode*)>& callback) const override;
    size_t GetNodeCount() const override;
    size_t GetLeafCount() const override;

private:
    std::unique_ptr<spatial::strategy::QuadtreeIndex> index_;
    std::unordered_map<const spatial::strategy::QuadtreeNode*, std::unique_ptr<QuadtreeNodeAdapter>> nodeAdapters_;
    uint64_t nextNodeId_ = 0;

    void BuildNodeMap();
    const QuadtreeNodeAdapter* GetNodeAdapter(const spatial::strategy::QuadtreeNode* node) const;
};

} // namespace adapters
} // namespace pipeline
```

### 步骤 3：抽象 B3DMGenerator 接口

#### 4.3.1 现有实现差异分析

| 组件 | Shapefile | FBX | 统一策略 |
|------|-----------|-----|---------|
| 入口类 | `B3DMContentGenerator` | `B3DMGenerator` | 统一为 `IB3DMGenerator` |
| 输入类型 | `vector<const ShapefileSpatialItem*>` | `SpatialItemRefList` | 统一为 `ISpatialItem` 列表 |
| LOD 生成 | `generateLODFiles()` | `generateLODFiles()` | 统一接口 |
| 几何提取 | `GeometryExtractor` | `FBXGeometryExtractor` | 通过 `ISpatialItem::GetGeometry()` |
| 坐标转换 | ENU 转换在数据加载时完成 | ENU 转换在 B3DM 生成时完成 | 统一在 DataSource 层完成 |

#### 4.3.2 Shapefile B3DM 生成器实现

```cpp
// pipeline/adapters/shapefile/shapefile_b3dm_generator.h
#pragma once

#include "../../b3dm_generator.h"
#include "../../../shapefile/b3dm_content_generator.h"

namespace pipeline {
namespace adapters {

class ShapefileB3DMGenerator : public IB3DMGenerator {
public:
    ShapefileB3DMGenerator() = default;

    bool Initialize(const B3DMGenerationConfig& config) override {
        config_ = config;
        generator_ = std::make_unique<shapefile::B3DMContentGenerator>(
            config.center_longitude,
            config.center_latitude
        );
        return generator_ != nullptr;
    }

    B3DMFile GenerateForNode(
        const ISpatialIndexNode* node,
        const SpatialItemList& items,
        std::string_view filename) override {
        // 转换 ISpatialItem 为 ShapefileSpatialItem
        std::vector<const shapefile::ShapefileSpatialItem*> shapefileItems;
        for (const auto& item : items) {
            auto* adapter = dynamic_cast<ShapefileSpatialItemAdapter*>(item.get());
            if (adapter) {
                shapefileItems.push_back(adapter->GetOriginalItem());
            }
        }

        // 调用现有的 B3DMContentGenerator
        auto content = generator_->generate(shapefileItems, true, false);

        // 写入文件并返回 B3DMFile
        B3DMFile result;
        // ... 文件写入逻辑
        return result;
    }

    std::vector<B3DMFile> GenerateWithLOD(
        const ISpatialIndexNode* node,
        const SpatialItemList& items,
        std::string_view base_filename) override {
        std::vector<B3DMFile> results;

        // 转换 ISpatialItem
        std::vector<const shapefile::ShapefileSpatialItem*> shapefileItems;
        for (const auto& item : items) {
            auto* adapter = dynamic_cast<ShapefileSpatialItemAdapter*>(item.get());
            if (adapter) {
                shapefileItems.push_back(adapter->GetOriginalItem());
            }
        }

        // 构建 LOD 配置
        std::vector<LODLevelSettings> lodLevels;
        if (config_.enable_lod) {
            lodLevels = BuildLODLevels();
        } else {
            LODLevelSettings level;
            level.target_ratio = 1.0f;
            level.enable_simplification = config_.enable_simplification;
            level.enable_draco = config_.enable_draco;
            lodLevels.push_back(level);
        }

        // 调用现有的 generateLODFiles
        auto lodFiles = generator_->generateLODFiles(
            shapefileItems,
            config_.output_directory.string(),
            lodLevels
        );

        // 转换结果
        for (const auto& file : lodFiles) {
            B3DMFile info;
            info.filepath = config_.output_directory / file.filename;
            info.relative_path = file.relativePath;
            info.node_id = node->GetId();
            info.lod_level = file.level;
            info.geometric_error = file.geometricError;
            if (std::filesystem::exists(info.filepath)) {
                info.file_size = std::filesystem::file_size(info.filepath);
            }
            results.push_back(info);
        }

        return results;
    }

private:
    B3DMGenerationConfig config_;
    std::unique_ptr<shapefile::B3DMContentGenerator> generator_;

    std::vector<LODLevelSettings> BuildLODLevels() {
        SimplificationParams simParams;
        simParams.enable_simplification = config_.enable_simplification;
        DracoCompressionParams dracoParams;
        dracoParams.enable_compression = config_.enable_draco;

        return build_lod_levels(
            config_.lod_ratios,
            0.0001f,
            simParams,
            dracoParams,
            false
        );
    }
};

} // namespace adapters
} // namespace pipeline
```

#### 4.3.3 FBX B3DM 生成器实现

```cpp
// pipeline/adapters/fbx/fbx_b3dm_generator.h
#pragma once

#include "../../b3dm_generator.h"
#include "../../../b3dm/b3dm_generator.h"
#include "../../../fbx/fbx_geometry_extractor.h"

namespace pipeline {
namespace adapters {

class FBXB3DMGenerator : public IB3DMGenerator {
public:
    FBXB3DMGenerator() = default;

    bool Initialize(const B3DMGenerationConfig& config) override {
        config_ = config;

        b3dm::B3DMGeneratorConfig b3dmConfig;
        b3dmConfig.centerLongitude = config.center_longitude;
        b3dmConfig.centerLatitude = config.center_latitude;
        b3dmConfig.centerHeight = config.center_height;
        b3dmConfig.enableSimplification = config.enable_simplification;
        b3dmConfig.enableDraco = config.enable_draco;
        b3dmConfig.enableTextureCompress = config.enable_texture_compress;

        geometryExtractor_ = std::make_unique<fbx::FBXGeometryExtractor>();
        b3dmConfig.geometryExtractor = geometryExtractor_.get();

        generator_ = std::make_unique<b3dm::B3DMGenerator>(b3dmConfig);
        return generator_ != nullptr;
    }

    B3DMFile GenerateForNode(
        const ISpatialIndexNode* node,
        const SpatialItemList& items,
        std::string_view filename) override {
        // 转换 ISpatialItem 为 SpatialItemRefList
        spatial::core::SpatialItemRefList spatialItems;
        for (const auto& item : items) {
            auto* adapter = dynamic_cast<FBXSpatialItemAdapter*>(item.get());
            if (adapter) {
                spatialItems.push_back(adapter->GetOriginalItem());
            }
        }

        LODLevelSettings lodSettings;
        lodSettings.target_ratio = 1.0f;
        auto content = generator_->generate(spatialItems, lodSettings);

        B3DMFile result;
        // ... 文件写入逻辑
        return result;
    }

    std::vector<B3DMFile> GenerateWithLOD(
        const ISpatialIndexNode* node,
        const SpatialItemList& items,
        std::string_view base_filename) override {
        std::vector<B3DMFile> results;

        // 转换 ISpatialItem
        spatial::core::SpatialItemRefList spatialItems;
        for (const auto& item : items) {
            auto* adapter = dynamic_cast<FBXSpatialItemAdapter*>(item.get());
            if (adapter) {
                spatialItems.push_back(adapter->GetOriginalItem());
            }
        }

        // 构建 LOD 配置
        auto lodLevels = BuildLODLevels();

        std::string tileDir = config_.output_directory.string() + "/" + std::string(base_filename);
        std::filesystem::create_directories(tileDir);

        auto lodFiles = generator_->generateLODFiles(
            spatialItems,
            tileDir,
            std::string(base_filename),
            lodLevels
        );

        // 转换结果
        for (const auto& file : lodFiles) {
            B3DMFile info;
            info.filepath = tileDir + "/" + file.filename;
            info.relative_path = std::string(base_filename) + "/" + file.filename;
            info.node_id = node->GetId();
            info.lod_level = file.level;
            info.geometric_error = file.geometricError;
            if (std::filesystem::exists(info.filepath)) {
                info.file_size = std::filesystem::file_size(info.filepath);
            }
            results.push_back(info);
        }

        return results;
    }

private:
    B3DMGenerationConfig config_;
    std::unique_ptr<b3dm::B3DMGenerator> generator_;
    std::unique_ptr<fbx::FBXGeometryExtractor> geometryExtractor_;

    std::vector<LODLevelSettings> BuildLODLevels() {
        if (config_.enable_lod) {
            SimplificationParams simTemplate;
            simTemplate.enable_simplification = config_.enable_simplification;
            DracoCompressionParams dracoTemplate;
            dracoTemplate.enable_compression = config_.enable_draco;

            return build_lod_levels(
                config_.lod_ratios,
                0.0001f,
                simTemplate,
                dracoTemplate,
                false
            );
        } else {
            LODLevelSettings lod0;
            lod0.target_ratio = 1.0f;
            lod0.enable_simplification = false;
            lod0.enable_draco = config_.enable_draco;
            return {lod0};
        }
    }
};

} // namespace adapters
} // namespace pipeline
```

### 步骤 4：抽象 TilesetBuilder 接口

#### 4.4.1 通用 Tileset 构建器实现

```cpp
// pipeline/adapters/common/tileset_builder_impl.h
#pragma once

#include "../../tileset_builder.h"
#include "../../../common/tileset_builder.h"
#include "../../../tileset/tileset_writer.h"

namespace pipeline {
namespace adapters {

class StandardTilesetBuilder : public ITilesetBuilder {
public:
    StandardTilesetBuilder() = default;

    bool Initialize(const TilesetBuilderConfig& config) override {
        config_ = config;

        common::TilesetBuilderConfig builderConfig;
        builderConfig.boundingVolumeScale = config.bounding_volume_scale;
        builderConfig.childGeometricErrorMultiplier = config.geometric_error_scale;
        builderConfig.enableLOD = config.enable_lod;
        builderConfig.lodLevelCount = config.lod_level_count;
        builderConfig.refine = "REPLACE";

        builder_ = std::make_unique<common::TilesetBuilder>(builderConfig);
        return builder_ != nullptr;
    }

    void AddNodeMetadata(const TileNodeMetadata& metadata) override {
        metadataMap_[metadata.node_id] = metadata;
    }

    bool BuildAndWrite(
        const SpatialIndex* index,
        const std::vector<B3DMFile>& b3dm_files
    ) override {
        auto rootNode = index->GetRoot();
        if (!rootNode) return false;

        auto rootTile = BuildTileRecursive(rootNode, b3dm_files);

        tileset::Tileset tileset;
        tileset.setVersion("1.0");
        tileset.setRoot(rootTile);

        std::filesystem::path tilesetPath = config_.output_path / "tileset.json";
        tileset::TilesetWriter writer;
        return writer.write(tileset, tilesetPath.string());
    }

    std::filesystem::path GetTilesetPath() const override {
        return config_.output_path / "tileset.json";
    }

private:
    TilesetBuilderConfig config_;
    std::unique_ptr<common::TilesetBuilder> builder_;
    TileNodeMetadataMap metadataMap_;

    tileset::Tile BuildTileRecursive(
        const ISpatialIndexNode* node,
        const std::vector<B3DMFile>& b3dm_files
    ) {
        tileset::Tile tile;

        auto it = metadataMap_.find(node->GetId());
        if (it == metadataMap_.end()) {
            return tile;
        }

        const auto& meta = it->second;

        // 设置包围盒
        tileset::Box box;
        box.centerX = (meta.bbox_min[0] + meta.bbox_max[0]) * 0.5;
        box.centerY = (meta.bbox_min[1] + meta.bbox_max[1]) * 0.5;
        box.centerZ = (meta.bbox_min[2] + meta.bbox_max[2]) * 0.5;
        box.halfLengthX = (meta.bbox_max[0] - meta.bbox_min[0]) * 0.5;
        box.halfLengthY = (meta.bbox_max[1] - meta.bbox_min[1]) * 0.5;
        box.halfLengthZ = (meta.bbox_max[2] - meta.bbox_min[2]) * 0.5;
        tile.setBoundingVolume(box);

        tile.setGeometricError(meta.geometric_error);

        if (meta.has_content && !meta.content_uris.empty()) {
            tileset::Content content;
            content.uri = meta.content_uris[0];
            tile.setContent(content);
        }

        for (auto childId : meta.children_ids) {
            auto childNode = FindNodeById(node, childId);
            if (childNode) {
                tile.addChild(BuildTileRecursive(childNode, b3dm_files));
            }
        }

        return tile;
    }

    const ISpatialIndexNode* FindNodeById(const ISpatialIndexNode* root, uint64_t id) {
        std::queue<const ISpatialIndexNode*> queue;
        queue.push(root);

        while (!queue.empty()) {
            auto* node = queue.front();
            queue.pop();

            if (node->GetId() == id) {
                return node;
            }

            for (auto* child : node->GetChildren()) {
                queue.push(child);
            }
        }

        return nullptr;
    }
};

} // namespace adapters
} // namespace pipeline
```

### 步骤 5：最终统一

#### 4.5.1 统一 Pipeline 实现

```cpp
// pipeline/unified_pipeline.cpp
#include "unified_pipeline.h"
#include "adapters/shapefile/shapefile_data_source.h"
#include "adapters/fbx/fbx_data_source.h"
#include "adapters/spatial/quadtree_index.h"
#include "adapters/spatial/octree_index.h"
#include "adapters/shapefile/shapefile_b3dm_generator.h"
#include "adapters/fbx/fbx_b3dm_generator.h"
#include "adapters/common/tileset_builder_impl.h"

namespace pipeline {

ConversionResult UnifiedPipeline::Execute() {
    ConversionResult result;
    result.success = false;

    if (!LoadData()) {
        result.error_message = "Failed to load data";
        return result;
    }

    if (!BuildSpatialIndex()) {
        result.error_message = "Failed to build spatial index";
        return result;
    }

    if (!GenerateB3DMFiles()) {
        result.error_message = "Failed to generate B3DM files";
        return result;
    }

    if (!BuildTileset()) {
        result.error_message = "Failed to build tileset";
        return result;
    }

    result.success = true;
    result.node_count = static_cast<int>(spatialIndex_->GetNodeCount());
    result.b3dm_count = static_cast<int>(b3dmFiles_.size());
    result.tileset_path = tilesetBuilder_->GetTilesetPath();

    return result;
}

bool UnifiedPipeline::LoadData() {
    dataSource_ = CreateDataSource();
    if (!dataSource_) return false;

    if (!dataSource_->Load(config_.data_source_config)) {
        return false;
    }

    auto items = dataSource_->GetSpatialItems();
    spatialItems_.clear();
    for (auto& item : items) {
        spatialItems_.push_back(item.get());
    }

    return true;
}

bool UnifiedPipeline::BuildSpatialIndex() {
    spatialIndex_ = CreateSpatialIndex();
    if (!spatialIndex_) return false;

    auto items = dataSource_->GetSpatialItems();
    return spatialIndex_->Build(items, config_.spatial_index_config);
}

bool UnifiedPipeline::GenerateB3DMFiles() {
    b3dmGenerator_ = CreateB3DMGenerator();
    if (!b3dmGenerator_) return false;

    if (!b3dmGenerator_->Initialize(config_.b3dm_generator_config)) {
        return false;
    }

    spatialIndex_->ForEachLeaf([this](const ISpatialIndexNode* node) {
        auto nodeItems = GetItemsForNode(node);
        std::string filename = "content_" + std::to_string(node->GetId());

        auto files = b3dmGenerator_->GenerateWithLOD(node, nodeItems, filename);
        b3dmFiles_.insert(b3dmFiles_.end(), files.begin(), files.end());
    });

    return !b3dmFiles_.empty();
}

bool UnifiedPipeline::BuildTileset() {
    tilesetBuilder_ = CreateTilesetBuilder();
    if (!tilesetBuilder_) return false;

    if (!tilesetBuilder_->Initialize(config_.tileset_builder_config)) {
        return false;
    }

    spatialIndex_->ForEachLeaf([this](const ISpatialIndexNode* node) {
        TileNodeMetadata meta;
        meta.node_id = node->GetId();
        meta.depth = node->GetDepth();
        node->GetBounds(
            meta.bbox_min[0], meta.bbox_min[1], meta.bbox_min[2],
            meta.bbox_max[0], meta.bbox_max[1], meta.bbox_max[2]
        );

        for (const auto& file : b3dmFiles_) {
            if (file.node_id == node->GetId()) {
                meta.content_uris.push_back(file.relative_path.string());
                meta.has_content = true;
            }
        }

        meta.is_leaf = node->IsLeaf();

        double dx = meta.bbox_max[0] - meta.bbox_min[0];
        double dy = meta.bbox_max[1] - meta.bbox_min[1];
        double dz = meta.bbox_max[2] - meta.bbox_min[2];
        meta.geometric_error = std::sqrt(dx*dx + dy*dy + dz*dz) / 20.0 *
                              config_.tileset_builder_config.geometric_error_scale;

        tilesetBuilder_->AddNodeMetadata(meta);
    });

    return tilesetBuilder_->BuildAndWrite(spatialIndex_.get(), b3dmFiles_);
}

DataSourcePtr UnifiedPipeline::CreateDataSource() {
    if (config_.source_type == "shapefile") {
        return std::make_unique<adapters::ShapefileDataSource>();
    } else if (config_.source_type == "fbx") {
        return std::make_unique<adapters::FBXDataSource>();
    }
    return nullptr;
}

SpatialIndexPtr UnifiedPipeline::CreateSpatialIndex() {
    if (config_.source_type == "shapefile") {
        return std::make_unique<adapters::QuadtreeIndex>();
    } else if (config_.source_type == "fbx") {
        return std::make_unique<adapters::OctreeIndex>();
    }
    return nullptr;
}

B3DMGeneratorPtr UnifiedPipeline::CreateB3DMGenerator() {
    if (config_.source_type == "shapefile") {
        return std::make_unique<adapters::ShapefileB3DMGenerator>();
    } else if (config_.source_type == "fbx") {
        return std::make_unique<adapters::FBXB3DMGenerator>();
    }
    return nullptr;
}

TilesetBuilderPtr UnifiedPipeline::CreateTilesetBuilder() {
    return std::make_unique<adapters::StandardTilesetBuilder>();
}

} // namespace pipeline
```

#### 4.5.2 最终验证

```cpp
// tests/test_step5_unified_pipeline.cpp
TEST(Step5_UnifiedPipeline, ShapefileConversion) {
    // 1. 使用旧管道生成基准
    shapefile::ShapefileProcessor oldProcessor(config);
    auto oldResult = oldProcessor.process();

    // 2. 使用统一管道生成
    pipeline::UnifiedPipelineConfig config;
    config.source_type = "shapefile";
    // ... 配置其他参数

    pipeline::UnifiedPipeline newPipeline(config);
    auto newResult = newPipeline.Execute();

    // 3. 对比结果
    EXPECT_TRUE(oldResult.success);
    EXPECT_TRUE(newResult.success);
    EXPECT_EQ(oldResult.nodeCount, newResult.node_count);
    EXPECT_EQ(oldResult.b3dmCount, newResult.b3dm_count);

    // 4. 对比输出文件
    CompareDirectories(
        "tests/output/old_shapefile",
        "tests/output/new_shapefile"
    );
}

TEST(Step5_UnifiedPipeline, FBXConversion) {
    // 类似 Shapefile 的测试...
}

TEST(Step5_UnifiedPipeline, ConsistencyWithReference) {
    pipeline::UnifiedPipelineConfig config;
    config.source_type = "shapefile";
    // ...

    pipeline::UnifiedPipeline pipeline(config);
    auto result = pipeline.Execute();

    EXPECT_TRUE(result.success);

    CompareWithBenchmark(
        result.tileset_path.parent_path(),
        "tests/reference/shapefile"
    );
}
```

## 5. 关键原则

1. **每次只改一个接口** - 降低风险，便于定位问题
2. **新接口包装旧实现** - 业务逻辑完全复用
3. **每次修改后验证** - 与基准数据对比
4. **保持向后兼容** - 旧代码可以继续使用
5. **渐进式替换** - 最终达到统一架构

## 6. 迁移完成标准

- [ ] Shapefile 转换结果与基准数据 100% 一致
- [ ] FBX 转换结果与基准数据 100% 一致
- [ ] 所有单元测试通过
- [ ] 性能不低于旧实现
- [ ] 代码覆盖率 > 80%
- [ ] 文档完整

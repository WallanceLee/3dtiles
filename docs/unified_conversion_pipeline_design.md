# 统一数据转换管道设计方案

## 关键约束（必须遵守）

### 1. Rust 调用 C++ 的接口保持不变

**Shapefile 转换接口：**
```rust
// Rust 侧（src/shape.rs）
#[repr(C)]
struct ShapeConversionParams {
    input_path: *const c_char,
    output_path: *const c_char,
    height_field: *const c_char,
    layer_id: i32,
    enable_lod: bool,
    draco_compression_params: DracoCompressionParams,
    simplify_params: SimplificationParams,
}

extern "C" {
    fn shp23dtile(params: *const ShapeConversionParams) -> bool;
}
```

```cpp
// C++ 侧（src/shp23dtile.cpp）
extern "C" bool shp23dtile(const ShapeConversionParams* params);
```

**FBX 转换接口：**
```rust
// Rust 侧（src/fbx.rs）
extern "C" {
    fn fbx23dtile(
        in_path: *const u8,
        out_path: *const u8,
        box_ptr: *mut f64,
        len: *mut i32,
        max_lvl: i32,
        enable_texture_compress: bool,
        enable_meshopt: bool,
        enable_draco: bool,
        enable_unlit: bool,
        longitude: f64,
        latitude: f64,
        height: f64,
        enable_lod: bool,
    ) -> *mut libc::c_void;
}
```

```cpp
// C++ 侧（src/fbx.cpp 或 src/FBXPipeline.cpp）
extern "C" void* fbx23dtile(
    const char* in_path,
    const char* out_path,
    double* box_ptr,
    int* len,
    int max_lvl,
    bool enable_texture_compress,
    bool enable_meshopt,
    bool enable_draco,
    bool enable_unlit,
    double longitude,
    double latitude,
    double height,
    bool enable_lod
);
```

### 2. 使用命令行选项切换新旧流水线

不修改 CMakeLists.txt，通过命令行参数 `--use-new-pipeline` 控制：

```bash
# 使用旧流水线（默认）
./target/debug/_3dtile -f shape -i input.shp -o output

# 使用新流水线
./target/debug/_3dtile -f shape -i input.shp -o output --use-new-pipeline
```

**实现机制：**
- Rust `main.rs` 解析 `--use-new-pipeline` 参数
- 通过 FFI 设置 C++ 全局标志 `g_use_new_pipeline`
- C++ 侧 `shp23dtile()` 和 `fbx23dtile()` 根据标志决定走旧逻辑还是新管道
- 新管道代码放在 `src/pipeline/` 目录，与旧代码共存
- CMakeLists.txt 使用 `file(GLOB_RECURSE)` 自动收集所有文件，无需修改

### 3. 禁止重写业务逻辑，优先复用现有代码

**核心原则：**
- **禁止重新实现**任何业务逻辑（坐标变换、包围盒计算、geometricError 计算、LOD 生成等）
- **优先复用**现有代码，通过提取、包装、适配等方式使用
- **必须保持**与重构前完全一致的计算结果（顶点坐标、法线、包围盒、geometricError 等）

**复用方式：**
1. **直接类复用** - 直接使用现有类（如 `ShapefileDataPool`、`FBXLoader`）
2. **函数提取** - 将静态逻辑提取为公共工具函数
3. **适配器包装** - 用适配器模式包装现有实现，统一接口

**验证要求：**
- 每个重构阶段必须能与基准数据对比验证
- 顶点坐标误差必须 < 1e-6
- 包围盒、geometricError 必须完全一致
- B3DM 文件内容必须一致（或差异可解释）

### 4. 业务逻辑确保和重构前完全一致

**一致性检查清单：**

| 检查项 | 验证方法 | 通过标准 |
|--------|----------|----------|
| 顶点坐标 | 对比 B3DM 顶点数据 | 浮点误差 < 1e-6 |
| 法线向量 | 对比 B3DM 法线数据 | 向量值完全一致 |
| 包围盒 | 对比 tileset.json box | 数值完全一致 |
| geometricError | 对比 tileset.json | 数值完全一致 |
| 节点结构 | 对比 tileset.json 层级 | 节点数量和关系一致 |
| B3DM 文件 | 对比文件哈希 | 内容一致 |
| 纹理坐标 | 对比 B3DM UV 数据 | 数值完全一致 |
| 属性数据 | 对比 attributes.db | 数据完全一致 |

**禁止修改的代码：**
- 坐标变换矩阵（Y-up 到 Z-up 转换）
- 包围盒计算逻辑
- geometricError 计算公式
- LOD 简化算法
- Draco 压缩参数
- 八叉树/四叉树分割逻辑

---

## 1. 设计概述

### 1.1 目标

- 统一 Shapefile 和 FBX 的转换流程，提高代码可扩展性
- 支持未来轻松添加新数据类型（GeoJSON、CityGML、OSGB 等）
- 遵循 C++20 标准、Google C++ 规范和 C++ Core Guidelines
- 保持 Rust 与 C++ 的 FFI 接口清晰简洁
- 将 Rust main.rs 中的业务逻辑迁移到专门的模块

### 1.2 核心设计原则

1. **单一职责原则 (SRP)**：每个类只负责一个功能
2. **开闭原则 (OCP)**：对扩展开放，对修改关闭
3. **依赖倒置原则 (DIP)**：依赖抽象接口，而非具体实现
4. **组合优于继承**：通过组合实现功能复用
5. **显式优于隐式**：地理参考配置必须显式指定

---

## 2. 架构设计

### 2.1 整体架构图

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              Rust 应用层                                     │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │  main.rs (简洁入口)                                                  │   │
│  │  └── 仅负责参数解析和模块调用                                        │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                              │                                              │
│                              ▼                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │  converter/ 模块 (Rust业务逻辑)                                      │   │
│  │  ├── shapefile_converter.rs                                         │   │
│  │  ├── fbx_converter.rs                                               │   │
│  │  └── mod.rs                                                         │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                              │                                              │
│                              ▼ FFI                                          │
└─────────────────────────────────────────────────────────────────────────────┘
                              │
┌─────────────────────────────────────────────────────────────────────────────┐
│                           C++ 统一管道层 (Unified Pipeline)                  │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                    ConversionPipeline (模板方法模式)                 │   │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐            │   │
│  │  │  Load    │→ │  Index   │→ │ Generate │→ │  Output  │            │   │
│  │  │  Data    │  │  Space   │  │  B3DM    │  │ Tileset  │            │   │
│  │  └──────────┘  └──────────┘  └──────────┘  └──────────┘            │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────────┘
                              │
                    ┌─────────┴─────────┐
                    ▼                   ▼
┌────────────────────────┐  ┌────────────────────────┐
│    DataSource          │  │   SpatialIndexStrategy │
│    (抽象接口)          │  │   (抽象接口)           │
├────────────────────────┤  ├────────────────────────┤
│ + Load()               │  │ + BuildIndex()         │
│ + GetSpatialItems()    │  │ + Query()              │
│ + GetWorldBounds()     │  │ + GetRootNode()        │
│ + CreateGeometryExtractor()│                        │
└────────────────────────┘  └────────────────────────┘
       ▲      ▲                      ▲
       │      │                      │
┌──────┘      └──────┐      ┌───────┴───────┐
│                    │      │               │
┌────────────────┐  ┌────────────────┐  ┌──────────────────┐  ┌──────────────────┐
│ ShapefileSource│  │   FBXSource    │  │ShapefilePipeline │  │  FbxPipeline     │
│ (无地理参考)   │  │ (需地理参考)   │  │ (使用四叉树)     │  │ (使用八叉树)     │
└────────────────┘  └────────────────┘  └──────────────────┘  └──────────────────┘
```

### 2.2 关键区别：地理参考处理

| 数据源 | 地理参考 | 处理方式 |
|--------|----------|----------|
| **Shapefile** | 从文件解析 | 自动从 .shp 文件读取坐标系信息，转换为 WGS84 |
| **FBX** | 必须显式提供 | 通过命令行参数 `--lon`, `--lat`, `--alt` 指定 FBX 原点在地理空间中的位置 |

---

## 3. 核心接口定义 (C++20)

### 3.1 数据源接口

```cpp
// pipeline/data_source.h
#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "common/geometry_extractor.h"
#include "spatial/core/spatial_bounds.h"
#include "spatial/core/spatial_item.h"

namespace pipeline {

// 数据源类型枚举
enum class DataSourceType : std::uint8_t {
  kShapefile = 0,
  kFbx = 1,
  kOsgb = 2,
  kGeoJson = 3,
  kCityGml = 4,
  kUnknown = 255
};

// 地理参考信息
struct GeoReference {
  // 对于 FBX：显式指定的原点坐标
  // 对于 Shapefile：从文件解析的中心点
  double center_longitude = 0.0;  // 度
  double center_latitude = 0.0;   // 度
  double center_height = 0.0;     // 米（椭球高）

  // 坐标系 EPSG 代码
  int epsg_code = 4326;  // 默认 WGS84

  [[nodiscard]] bool IsValid() const noexcept {
    return center_longitude >= -180.0 && center_longitude <= 180.0 &&
           center_latitude >= -90.0 && center_latitude <= 90.0;
  }
};

// 数据源配置基类
struct DataSourceConfig {
  virtual ~DataSourceConfig() = default;

  std::filesystem::path input_path;
  std::filesystem::path output_path;

  // 地理参考（FBX 必须显式提供，Shapefile 自动解析）
  GeoReference geo_reference;

  // 处理选项
  bool enable_simplification = false;
  SimplificationParams simplification_params;

  bool enable_draco = false;
  DracoCompressionParams draco_params;

  bool enable_lod = false;
  std::vector<LODLevelSettings> lod_levels;

  // 验证配置
  [[nodiscard]] virtual bool Validate() const { return true; }
};

// 数据源接口 - 纯抽象类
class DataSource {
 public:
  virtual ~DataSource() = default;

  // 禁止拷贝，允许移动
  DataSource(const DataSource&) = delete;
  DataSource& operator=(const DataSource&) = delete;
  DataSource(DataSource&&) = default;
  DataSource& operator=(DataSource&&) = default;

  [[nodiscard]] virtual DataSourceType GetType() const noexcept = 0;

  // 加载数据 - 纯虚函数
  [[nodiscard]] virtual bool Load(const DataSourceConfig& config) = 0;

  // 获取地理参考信息（加载后可用）
  [[nodiscard]] virtual const GeoReference& GetGeoReference() const noexcept = 0;

  // 获取空间对象列表
  [[nodiscard]] virtual spatial::core::SpatialItemList GetSpatialItems() const = 0;

  // 获取世界包围盒
  [[nodiscard]] virtual spatial::core::SpatialBounds<double, 3> GetWorldBounds() const = 0;

  // 创建几何提取器
  [[nodiscard]] virtual std::unique_ptr<common::IGeometryExtractor>
  CreateGeometryExtractor() const = 0;

  [[nodiscard]] virtual std::size_t GetItemCount() const noexcept = 0;

  // 检查是否已加载
  [[nodiscard]] virtual bool IsLoaded() const noexcept = 0;

 protected:
  DataSource() = default;
};

using DataSourcePtr = std::unique_ptr<DataSource>;

// 数据源创建函数类型
using DataSourceCreator = std::function<DataSourcePtr()>;

// 数据源工厂 - 单例模式
class DataSourceFactory {
 public:
  [[nodiscard]] static DataSourceFactory& Instance() noexcept;

  void Register(DataSourceType type, DataSourceCreator creator);
  [[nodiscard]] DataSourcePtr Create(DataSourceType type) const;
  [[nodiscard]] bool IsRegistered(DataSourceType type) const noexcept;

 private:
  DataSourceFactory() = default;
  ~DataSourceFactory() = default;

  std::unordered_map<DataSourceType, DataSourceCreator> creators_;
};

// 数据源注册辅助宏
#define REGISTER_DATA_SOURCE(TYPE, CLASS)                                    \
  namespace {                                                                \
  [[maybe_unused]] const bool _##CLASS##_registered = []() {                 \
    ::pipeline::DataSourceFactory::Instance().Register(                      \
        TYPE, []() -> ::pipeline::DataSourcePtr {                            \
          return std::make_unique<CLASS>();                                  \
        });                                                                  \
    return true;                                                             \
  }();                                                                       \
  }

}  // namespace pipeline
```

### 3.2 统一管道接口

```cpp
// pipeline/conversion_pipeline.h
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "b3dm/b3dm_generator.h"
#include "common/tile_meta.h"
#include "common/tileset_builder.h"
#include "data_source.h"
#include "spatial/core/slicing_strategy.h"

namespace pipeline {

// 转换结果
struct ConversionResult {
  bool success = false;
  std::filesystem::path tileset_path;
  std::size_t item_count = 0;
  std::size_t node_count = 0;
  std::size_t b3dm_count = 0;
  std::string error_message;

  // C++20 显式 bool 转换
  [[nodiscard]] explicit operator bool() const noexcept { return success; }
};

// 管道配置
struct PipelineConfig {
  // 数据源配置（必须由调用方提供所有权）
  std::unique_ptr<DataSourceConfig> source_config;
  DataSourceType source_type;

  // 空间索引配置
  std::unique_ptr<spatial::core::SlicingConfig> slicing_config;

  // B3DM 生成配置
  std::unique_ptr<b3dm::B3DMGeneratorConfig> b3dm_config;

  // Tileset 构建配置
  std::unique_ptr<common::TilesetBuilderConfig> tileset_config;

  // 验证配置完整性
  [[nodiscard]] bool Validate() const noexcept;
};

// 统一转换管道 - 模板方法模式
class ConversionPipeline {
 public:
  explicit ConversionPipeline(PipelineConfig config);
  virtual ~ConversionPipeline() = default;

  // 禁止拷贝，允许移动
  ConversionPipeline(const ConversionPipeline&) = delete;
  ConversionPipeline& operator=(const ConversionPipeline&) = delete;
  ConversionPipeline(ConversionPipeline&&) = default;
  ConversionPipeline& operator=(ConversionPipeline&&) = default;

  // 执行完整的转换流程 - 模板方法
  [[nodiscard]] ConversionResult Execute();

 protected:
  // 模板方法步骤 - 可被子类覆盖
  [[nodiscard]] virtual bool LoadData();
  [[nodiscard]] virtual bool BuildSpatialIndex();
  [[nodiscard]] virtual bool GenerateB3DMFiles();
  [[nodiscard]] virtual bool GenerateTileset();

  // 钩子方法 - 必须由子类实现
  [[nodiscard]] virtual std::unique_ptr<spatial::core::SlicingStrategy>
  CreateSlicingStrategy() = 0;

  [[nodiscard]] virtual std::unique_ptr<common::TileMeta>
  CreateTileMetaFromNode(const spatial::core::SpatialIndexNode& node) = 0;

  [[nodiscard]] virtual common::BoundingBoxConverter
  GetBoundingBoxConverter() const = 0;

  [[nodiscard]] virtual common::GeometricErrorCalculator
  GetGeometricErrorCalculator() const = 0;

  // 访问器
  [[nodiscard]] const PipelineConfig& GetConfig() const noexcept { return config_; }
  [[nodiscard]] DataSource& GetDataSource() noexcept { return *data_source_; }

 private:
  PipelineConfig config_;

  // 执行状态
  DataSourcePtr data_source_;
  std::unique_ptr<spatial::core::SpatialIndex> spatial_index_;
  std::unique_ptr<b3dm::B3DMGenerator> b3dm_generator_;
  common::TileMetaMap tile_meta_map_;
  common::TileMetaPtr root_meta_;

  // 节点 ID 计数器
  int node_id_counter_ = 0;

  // 递归处理节点
  void ProcessNodeRecursive(const spatial::core::SpatialIndexNode& node,
                           std::optional<std::size_t> parent_id);
};

// Shapefile 专用管道 - 使用四叉树策略
class ShapefilePipeline : public ConversionPipeline {
 public:
  using ConversionPipeline::ConversionPipeline;

 protected:
  [[nodiscard]] std::unique_ptr<spatial::core::SlicingStrategy>
  CreateSlicingStrategy() override;
};

// FBX 专用管道 - 使用八叉树策略
class FbxPipeline : public ConversionPipeline {
 public:
  using ConversionPipeline::ConversionPipeline;

 protected:
  [[nodiscard]] std::unique_ptr<spatial::core::SlicingStrategy>
  CreateSlicingStrategy() override;
};

// 工厂函数
[[nodiscard]] std::unique_ptr<ConversionPipeline> CreatePipeline(
    DataSourceType type, PipelineConfig config);

}  // namespace pipeline
```

### 3.3 Shapefile 专用配置和数据源

```cpp
// pipeline/shapefile_source.h
#pragma once

#include "data_source.h"

namespace pipeline {

// Shapefile 专用配置
struct ShapefileSourceConfig : public DataSourceConfig {
  std::string height_field;  // 高度字段名
  int layer_id = 0;          // 图层 ID

  [[nodiscard]] bool Validate() const override {
    if (!DataSourceConfig::Validate()) return false;
    // Shapefile 不需要显式地理参考，从文件自动解析
    return true;
  }
};

// Shapefile 数据源实现
class ShapefileSource : public DataSource {
 public:
  ShapefileSource() = default;
  ~ShapefileSource() override = default;

  [[nodiscard]] DataSourceType GetType() const noexcept override {
    return DataSourceType::kShapefile;
  }

  [[nodiscard]] bool Load(const DataSourceConfig& config) override;
  [[nodiscard]] const GeoReference& GetGeoReference() const noexcept override;
  [[nodiscard]] spatial::core::SpatialItemList GetSpatialItems() const override;
  [[nodiscard]] spatial::core::SpatialBounds<double, 3> GetWorldBounds() const override;
  [[nodiscard]] std::unique_ptr<common::IGeometryExtractor>
  CreateGeometryExtractor() const override;
  [[nodiscard]] std::size_t GetItemCount() const noexcept override;
  [[nodiscard]] bool IsLoaded() const noexcept override;

 private:
  class Impl;  // PIMPL 模式隐藏实现细节
  std::unique_ptr<Impl> impl_;
};

REGISTER_DATA_SOURCE(DataSourceType::kShapefile, ShapefileSource)

}  // namespace pipeline
```

### 3.4 FBX 专用配置和数据源

```cpp
// pipeline/fbx_source.h
#pragma once

#include "data_source.h"

namespace pipeline {

// FBX 专用配置
struct FbxSourceConfig : public DataSourceConfig {
  bool load_textures = true;
  bool convert_to_y_up = true;

  [[nodiscard]] bool Validate() const override {
    if (!DataSourceConfig::Validate()) return false;
    // FBX 必须提供有效的地理参考
    if (!geo_reference.IsValid()) {
      return false;
    }
    return true;
  }
};

// FBX 数据源实现
class FbxSource : public DataSource {
 public:
  FbxSource() = default;
  ~FbxSource() override = default;

  [[nodiscard]] DataSourceType GetType() const noexcept override {
    return DataSourceType::kFbx;
  }

  [[nodiscard]] bool Load(const DataSourceConfig& config) override;
  [[nodiscard]] const GeoReference& GetGeoReference() const noexcept override;
  [[nodiscard]] spatial::core::SpatialItemList GetSpatialItems() const override;
  [[nodiscard]] spatial::core::SpatialBounds<double, 3> GetWorldBounds() const override;
  [[nodiscard]] std::unique_ptr<common::IGeometryExtractor>
  CreateGeometryExtractor() const override;
  [[nodiscard]] std::size_t GetItemCount() const noexcept override;
  [[nodiscard]] bool IsLoaded() const noexcept override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

REGISTER_DATA_SOURCE(DataSourceType::kFbx, FbxSource)

}  // namespace pipeline
```

---

## 4. Rust FFI 接口设计

### 4.1 Rust 侧模块结构

```
src/
├── main.rs              # 简洁入口，仅参数解析
├── lib.rs               # 库入口（如果需要）
├── common.rs            # 通用工具函数
├── error.rs             # 错误处理
├── fun_c.rs             # C 函数导出（供 C++ 调用）
├── ffi/                 # FFI 接口模块
│   ├── mod.rs           # FFI 模块入口
│   ├── c_api.rs         # C API 定义
│   └── types.rs         # FFI 类型定义
└── converter/           # 转换器业务逻辑
    ├── mod.rs           # 模块入口
    ├── shapefile.rs     # Shapefile 转换逻辑
    ├── fbx.rs           # FBX 转换逻辑
    └── osgb.rs          # OSGB 转换逻辑
```

### 4.2 FFI 类型定义

```rust
// src/ffi/types.rs
use libc::{c_char, c_double, c_int, c_void};

/// 数据源类型
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CDataSourceType {
    Shapefile = 0,
    Fbx = 1,
    Osgb = 2,
}

/// 地理参考信息
#[repr(C)]
pub struct CGeoReference {
    pub center_longitude: c_double,
    pub center_latitude: c_double,
    pub center_height: c_double,
    pub epsg_code: c_int,
}

impl Default for CGeoReference {
    fn default() -> Self {
        Self {
            center_longitude: 0.0,
            center_latitude: 0.0,
            center_height: 0.0,
            epsg_code: 4326,
        }
    }
}

/// Draco 压缩参数
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct CDracoCompressionParams {
    pub position_quantization_bits: c_int,
    pub normal_quantization_bits: c_int,
    pub tex_coord_quantization_bits: c_int,
    pub generic_quantization_bits: c_int,
    pub enable_compression: bool,
}

impl Default for CDracoCompressionParams {
    fn default() -> Self {
        Self {
            position_quantization_bits: 11,
            normal_quantization_bits: 10,
            tex_coord_quantization_bits: 12,
            generic_quantization_bits: 8,
            enable_compression: false,
        }
    }
}

/// 简化参数
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct CSimplificationParams {
    pub target_error: c_float,
    pub target_ratio: c_float,
    pub enable_simplification: bool,
    pub preserve_texture_coords: bool,
    pub preserve_normals: bool,
}

impl Default for CSimplificationParams {
    fn default() -> Self {
        Self {
            target_error: 0.01,
            target_ratio: 0.5,
            enable_simplification: false,
            preserve_texture_coords: true,
            preserve_normals: true,
        }
    }
}

/// LOD 级别设置
#[repr(C)]
pub struct CLodLevelSettings {
    pub level: c_int,
    pub geometric_error: c_double,
    pub simplification_ratio: c_float,
}

/// 转换参数
#[repr(C)]
pub struct CConversionParams {
    pub input_path: *const c_char,
    pub output_path: *const c_char,
    pub source_type: CDataSourceType,
    pub geo_reference: CGeoReference,
    pub enable_lod: bool,
    pub enable_simplification: bool,
    pub enable_draco: bool,
    pub enable_texture_compress: bool,
    pub enable_unlit: bool,
    pub draco_params: CDracoCompressionParams,
    pub simplify_params: CSimplificationParams,
    pub max_depth: c_int,
    pub max_items_per_node: c_int,
    pub height_field: *const c_char,  // Shapefile 专用
    pub layer_id: c_int,              // Shapefile 专用
}

/// 转换结果
#[repr(C)]
pub struct CConversionResult {
    pub success: bool,
    pub item_count: usize,
    pub node_count: usize,
    pub b3dm_count: usize,
    pub error_message: *mut c_char,
}
```

### 4.3 Rust 转换器模块

```rust
// src/converter/mod.rs
use std::path::Path;

pub mod shapefile;
pub mod fbx;
pub mod osgb;

#[derive(Debug, Clone)]
pub enum DataSourceType {
    Shapefile,
    Fbx,
    Osgb,
}

#[derive(Debug, Clone)]
pub struct GeoReference {
    pub center_longitude: f64,
    pub center_latitude: f64,
    pub center_height: f64,
    pub epsg_code: i32,
}

impl Default for GeoReference {
    fn default() -> Self {
        Self {
            center_longitude: 0.0,
            center_latitude: 0.0,
            center_height: 0.0,
            epsg_code: 4326,
        }
    }
}

#[derive(Debug, Clone)]
pub struct ConversionParams {
    pub input_path: String,
    pub output_path: String,
    pub source_type: DataSourceType,
    pub geo_reference: GeoReference,
    pub enable_lod: bool,
    pub enable_simplification: bool,
    pub enable_draco: bool,
    pub enable_texture_compress: bool,
    pub enable_unlit: bool,
    pub draco_params: crate::ffi::types::CDracoCompressionParams,
    pub simplify_params: crate::ffi::types::CSimplificationParams,
    pub max_depth: i32,
    pub max_items_per_node: i32,
    pub height_field: Option<String>,
    pub layer_id: i32,
}

impl ConversionParams {
    pub fn to_c_params(&self) -> crate::ffi::types::CConversionParams {
        use std::ffi::CString;
        use crate::ffi::types::*;

        CConversionParams {
            input_path: CString::new(self.input_path.clone()).unwrap().into_raw(),
            output_path: CString::new(self.output_path.clone()).unwrap().into_raw(),
            source_type: match self.source_type {
                DataSourceType::Shapefile => CDataSourceType::Shapefile,
                DataSourceType::Fbx => CDataSourceType::Fbx,
                DataSourceType::Osgb => CDataSourceType::Osgb,
            },
            geo_reference: CGeoReference {
                center_longitude: self.geo_reference.center_longitude,
                center_latitude: self.geo_reference.center_latitude,
                center_height: self.geo_reference.center_height,
                epsg_code: self.geo_reference.epsg_code,
            },
            enable_lod: self.enable_lod,
            enable_simplification: self.enable_simplification,
            enable_draco: self.enable_draco,
            enable_texture_compress: self.enable_texture_compress,
            enable_unlit: self.enable_unlit,
            draco_params: self.draco_params,
            simplify_params: self.simplify_params,
            max_depth: self.max_depth,
            max_items_per_node: self.max_items_per_node,
            height_field: self.height_field.as_ref()
                .map(|s| CString::new(s.clone()).unwrap().into_raw())
                .unwrap_or(std::ptr::null()),
            layer_id: self.layer_id,
        }
    }
}

pub trait Converter {
    fn convert(input: &Path, output: &Path, params: &ConversionParams) -> Result<(), Box<dyn std::error::Error>>;
}
```

### 4.4 Shapefile 转换器

```rust
// src/converter/shapefile.rs
use std::path::Path;
use crate::converter::{ConversionParams, Converter};
use crate::ffi::types::*;

pub struct ShapefileConverter;

impl Converter for ShapefileConverter {
    fn convert(
        input: &Path,
        output: &Path,
        params: &ConversionParams,
    ) -> Result<(), Box<dyn std::error::Error>> {
        let mut c_params = params.to_c_params();

        // Shapefile 不需要显式地理参考，从文件自动解析
        // 但如果有提供，也可以使用

        let result = unsafe { convert_with_pipeline(&c_params) };

        // 释放分配的字符串内存
        unsafe {
            if !c_params.input_path.is_null() {
                let _ = std::ffi::CString::from_raw(c_params.input_path as *mut _);
            }
            if !c_params.output_path.is_null() {
                let _ = std::ffi::CString::from_raw(c_params.output_path as *mut _);
            }
            if !c_params.height_field.is_null() {
                let _ = std::ffi::CString::from_raw(c_params.height_field as *mut _);
            }
        }

        if result.success {
            log::info!("Shapefile conversion successful: {} items, {} B3DM files",
                      result.item_count, result.b3dm_count);
            Ok(())
        } else {
            let error = unsafe {
                if !result.error_message.is_null() {
                    let msg = std::ffi::CStr::from_ptr(result.error_message)
                        .to_string_lossy()
                        .into_owned();
                    libc::free(result.error_message as *mut _);
                    msg
                } else {
                    "Unknown error".to_string()
                }
            };
            Err(error.into())
        }
    }
}

// 对外暴露的便捷函数
pub fn convert(
    input: &str,
    output: &str,
    params: &ConversionParams,
) -> Result<(), Box<dyn std::error::Error>> {
    ShapefileConverter::convert(
        Path::new(input),
        Path::new(output),
        params,
    )
}

extern "C" {
    fn convert_with_pipeline(params: *const CConversionParams) -> CConversionResult;
}
```

### 4.5 FBX 转换器

```rust
// src/converter/fbx.rs
use std::path::Path;
use crate::converter::{ConversionParams, Converter, DataSourceType, GeoReference};
use crate::ffi::types::*;

pub struct FbxConverter;

impl Converter for FbxConverter {
    fn convert(
        input: &Path,
        output: &Path,
        params: &ConversionParams,
    ) -> Result<(), Box<dyn std::error::Error>> {
        // FBX 必须提供有效的地理参考
        if !params.geo_reference.is_valid() {
            return Err("FBX conversion requires valid geo-reference (lon, lat, alt)".into());
        }

        let mut c_params = params.to_c_params();

        let result = unsafe { convert_with_pipeline(&c_params) };

        // 释放内存
        unsafe {
            if !c_params.input_path.is_null() {
                let _ = std::ffi::CString::from_raw(c_params.input_path as *mut _);
            }
            if !c_params.output_path.is_null() {
                let _ = std::ffi::CString::from_raw(c_params.output_path as *mut _);
            }
        }

        if result.success {
            log::info!("FBX conversion successful: {} items, {} B3DM files",
                      result.item_count, result.b3dm_count);
            Ok(())
        } else {
            let error = unsafe {
                if !result.error_message.is_null() {
                    let msg = std::ffi::CStr::from_ptr(result.error_message)
                        .to_string_lossy()
                        .into_owned();
                    libc::free(result.error_message as *mut _);
                    msg
                } else {
                    "Unknown error".to_string()
                }
            };
            Err(error.into())
        }
    }
}

impl GeoReference {
    fn is_valid(&self) -> bool {
        self.center_longitude >= -180.0 && self.center_longitude <= 180.0 &&
        self.center_latitude >= -90.0 && self.center_latitude <= 90.0
    }
}

pub fn convert(
    input: &str,
    output: &str,
    params: &ConversionParams,
) -> Result<(), Box<dyn std::error::Error>> {
    FbxConverter::convert(
        Path::new(input),
        Path::new(output),
        params,
    )
}

extern "C" {
    fn convert_with_pipeline(params: *const CConversionParams) -> CConversionResult;
}
```

### 4.6 简化后的 main.rs

```rust
// src/main.rs
use clap::{Arg, ArgAction, Command};
use log::LevelFilter;
use std::io::Write;
use chrono::prelude::*;

// 迁移后的模块
mod common;
mod converter;
mod error;
mod ffi;
mod fun_c;
mod utils;

use converter::{ConversionParams, DataSourceType, GeoReference};

fn main() {
    setup_environment();
    init_logger();

    let matches = build_cli().get_matches();

    let input = matches.get_one::<String>("input").expect("input is required");
    let output = matches.get_one::<String>("output").expect("output is required");
    let format = matches.get_one::<String>("format").expect("format is required");

    // 构建转换参数
    let params = build_conversion_params(&matches);

    // 执行转换
    match format.as_str() {
        "shape" => converter::shapefile::convert(input, output, params),
        "fbx" => converter::fbx::convert(input, output, params),
        "osgb" => converter::osgb::convert(input, output, params),
        _ => {
            log::error!("Unsupported format: {}", format);
            std::process::exit(1);
        }
    }
}

fn build_cli() -> Command {
    Command::new("3dtiles")
        .version("1.0")
        .about("Unified 3D Tiles conversion tool")
        .arg(Arg::new("input")
            .short('i')
            .long("input")
            .required(true)
            .help("Input file path"))
        .arg(Arg::new("output")
            .short('o')
            .long("output")
            .required(true)
            .help("Output directory path"))
        .arg(Arg::new("format")
            .short('f')
            .long("format")
            .required(true)
            .value_parser(["shape", "fbx", "osgb"])
            .help("Input format"))
        // FBX 地理参考参数（FBX 必需）
        .arg(Arg::new("longitude")
            .long("lon")
            .help("Longitude for FBX origin (required for FBX)")
            .num_args(1))
        .arg(Arg::new("latitude")
            .long("lat")
            .help("Latitude for FBX origin (required for FBX)")
            .num_args(1))
        .arg(Arg::new("altitude")
            .long("alt")
            .help("Altitude for FBX origin")
            .num_args(1))
        // Shapefile 专用参数
        .arg(Arg::new("height-field")
            .long("height")
            .help("Height field name for Shapefile")
            .num_args(1))
        // 通用功能开关
        .arg(Arg::new("enable-lod")
            .long("enable-lod")
            .action(ArgAction::SetTrue)
            .help("Enable LOD generation"))
        .arg(Arg::new("enable-draco")
            .long("enable-draco")
            .action(ArgAction::SetTrue)
            .help("Enable Draco compression"))
        .arg(Arg::new("enable-simplify")
            .long("enable-simplify")
            .action(ArgAction::SetTrue)
            .help("Enable mesh simplification"))
        .arg(Arg::new("enable-texture-compress")
            .long("enable-texture-compress")
            .action(ArgAction::SetTrue)
            .help("Enable texture compression (KTX2)"))
        .arg(Arg::new("use-new-pipeline")
            .long("use-new-pipeline")
            .action(ArgAction::SetTrue)
            .help("Use new unified pipeline (experimental)"))
}

fn build_conversion_params(matches: &clap::ArgMatches) -> ConversionParams {
    ConversionParams {
        input_path: matches.get_one::<String>("input").unwrap().clone(),
        output_path: matches.get_one::<String>("output").unwrap().clone(),
        source_type: parse_source_type(matches.get_one::<String>("format").unwrap()),
        geo_reference: GeoReference {
            center_longitude: matches.get_one::<String>("longitude")
                .and_then(|s| s.parse().ok())
                .unwrap_or(0.0),
            center_latitude: matches.get_one::<String>("latitude")
                .and_then(|s| s.parse().ok())
                .unwrap_or(0.0),
            center_height: matches.get_one::<String>("altitude")
                .and_then(|s| s.parse().ok())
                .unwrap_or(0.0),
            epsg_code: 4326,
        },
        enable_lod: matches.get_flag("enable-lod"),
        enable_simplification: matches.get_flag("enable-simplify"),
        enable_draco: matches.get_flag("enable-draco"),
        enable_texture_compress: matches.get_flag("enable-texture-compress"),
        enable_unlit: false,
        draco_params: Default::default(),
        simplify_params: Default::default(),
        max_depth: 10,
        max_items_per_node: 1000,
        height_field: matches.get_one::<String>("height-field").cloned(),
        layer_id: 0,
    }
}

fn parse_source_type(format: &str) -> DataSourceType {
    match format {
        "shape" => DataSourceType::Shapefile,
        "fbx" => DataSourceType::Fbx,
        "osgb" => DataSourceType::Osgb,
        _ => panic!("Unsupported format: {}", format),
    }
}

fn setup_environment() {
    // 环境设置代码...
}

fn init_logger() {
    env_logger::Builder::from_default_env()
        .format(|buf, record| {
            let dt = Local::now();
            writeln!(
                buf,
                "{}: {} - {}",
                record.level(),
                dt.format("%Y-%m-%d %H:%M:%S"),
                record.args()
            )
        })
        .filter(None, LevelFilter::Info)
        .init();
}
```

---

## 5. C++ FFI 实现

### 5.1 保持旧接口不变，内部转发到新管道

```cpp
// src/shp23dtile.cpp - 修改现有文件，添加新管道转发
#include "pipeline/conversion_pipeline.h"

// 全局标志，由 Rust 通过 FFI 设置
extern "C" bool g_use_new_pipeline = false;

// 设置全局标志（供 Rust 调用）
extern "C" void set_use_new_pipeline(bool use_new) {
    g_use_new_pipeline = use_new;
}

// 原有接口保持不变
extern "C" bool shp23dtile(const ShapeConversionParams* params) {
    if (g_use_new_pipeline) {
        // 转发到新管道
        return shp23dtile_new_pipeline(params);
    }
    // 原有旧实现
    return shp23dtile_legacy(params);
}

// 旧实现（原有代码移动到这里）
bool shp23dtile_legacy(const ShapeConversionParams* params) {
    // ... 原有实现不变
}

// 新管道包装器
bool shp23dtile_new_pipeline(const ShapeConversionParams* params) {
    // 将旧参数转换为新管道参数
    CConversionParams c_params{};
    c_params.input_path = params->input_path;
    c_params.output_path = params->output_path;
    c_params.source_type = kShapefile;
    c_params.height_field = params->height_field;
    c_params.layer_id = params->layer_id;
    c_params.enable_lod = params->enable_lod;
    // ... 其他参数转换

    auto result = convert_with_pipeline(&c_params);
    return result.success;
}
```

```cpp
// src/fbx.cpp - 修改现有文件，添加新管道转发
#include "pipeline/conversion_pipeline.h"

// 原有接口保持不变
extern "C" void* fbx23dtile(
    const char* in_path,
    const char* out_path,
    double* box_ptr,
    int* len,
    int max_lvl,
    bool enable_texture_compress,
    bool enable_meshopt,
    bool enable_draco,
    bool enable_unlit,
    double longitude,
    double latitude,
    double height,
    bool enable_lod
) {
    if (g_use_new_pipeline) {
        // 转发到新管道
        return fbx23dtile_new_pipeline(...);
    }
    // 原有旧实现
    return fbx23dtile_legacy(...);
}
```

### 5.2 新管道内部实现

```cpp
// src/pipeline/ffi_bridge.cpp
#include "ffi_bridge.h"

#include <cstring>
#include <string>

#include "conversion_pipeline.h"
#include "fbx_source.h"
#include "shapefile_source.h"

extern "C" {

// C 结构体定义（新管道内部使用）
enum CDataSourceType : uint8_t {
  kShapefile = 0,
  kFbx = 1,
  kOsgb = 2,
};

struct CGeoReference {
  double center_longitude;
  double center_latitude;
  double center_height;
  int epsg_code;
};

struct CDracoCompressionParams {
  int position_quantization_bits;
  int normal_quantization_bits;
  int tex_coord_quantization_bits;
  int generic_quantization_bits;
  bool enable_compression;
};

struct CSimplificationParams {
  float target_error;
  float target_ratio;
  bool enable_simplification;
  bool preserve_texture_coords;
  bool preserve_normals;
};

struct CConversionParams {
  const char* input_path;
  const char* output_path;
  CDataSourceType source_type;
  CGeoReference geo_reference;
  bool enable_lod;
  bool enable_simplification;
  bool enable_draco;
  bool enable_texture_compress;
  bool enable_unlit;
  CDracoCompressionParams draco_params;
  CSimplificationParams simplify_params;
  int max_depth;
  int max_items_per_node;
  const char* height_field;
  int layer_id;
};

struct CConversionResult {
  bool success;
  size_t item_count;
  size_t node_count;
  size_t b3dm_count;
  char* error_message;
};

// 新管道统一转换入口（内部使用）
CConversionResult convert_with_pipeline(const CConversionParams* c_params) {
  CConversionResult result{};

  if (c_params == nullptr) {
    result.success = false;
    const char* msg = "Null params pointer";
    result.error_message = new char[strlen(msg) + 1];
    strcpy(result.error_message, msg);
    return result;
  }

  try {
    // 构建管道配置
    auto config = BuildPipelineConfig(*c_params);

    // 创建并执行管道
    auto pipeline = pipeline::CreatePipeline(
        static_cast<pipeline::DataSourceType>(c_params->source_type),
        std::move(config));

    if (!pipeline) {
      result.success = false;
      const char* msg = "Failed to create pipeline";
      result.error_message = new char[strlen(msg) + 1];
      strcpy(result.error_message, msg);
      return result;
    }

    auto conversion_result = pipeline->Execute();

    result.success = conversion_result.success;
    result.item_count = conversion_result.item_count;
    result.node_count = conversion_result.node_count;
    result.b3dm_count = conversion_result.b3dm_count;

    if (!conversion_result.success) {
      result.error_message = new char[conversion_result.error_message.size() + 1];
      strcpy(result.error_message, conversion_result.error_message.c_str());
    }

  } catch (const std::exception& e) {
    result.success = false;
    result.error_message = new char[strlen(e.what()) + 1];
    strcpy(result.error_message, e.what());
  }

  return result;
}

// 释放结果内存
void free_conversion_result(CConversionResult* result) {
  if (result && result->error_message) {
    delete[] result->error_message;
    result->error_message = nullptr;
  }
}

}  // extern "C"

namespace pipeline {

namespace {

PipelineConfig BuildPipelineConfig(const CConversionParams& c_params) {
  PipelineConfig config;

  // 根据类型构建数据源配置
  switch (c_params.source_type) {
    case kShapefile: {
      auto source_config = std::make_unique<ShapefileSourceConfig>();
      source_config->input_path = c_params.input_path;
      source_config->output_path = c_params.output_path;
      if (c_params.height_field) {
        source_config->height_field = c_params.height_field;
      }
      source_config->layer_id = c_params.layer_id;
      source_config->enable_lod = c_params.enable_lod;
      source_config->enable_simplification = c_params.enable_simplification;
      source_config->enable_draco = c_params.enable_draco;
      // ... 其他参数
      config.source_config = std::move(source_config);
      config.source_type = DataSourceType::kShapefile;
      break;
    }
    case kFbx: {
      auto source_config = std::make_unique<FbxSourceConfig>();
      source_config->input_path = c_params.input_path;
      source_config->output_path = c_params.output_path;
      source_config->geo_reference.center_longitude = c_params.geo_reference.center_longitude;
      source_config->geo_reference.center_latitude = c_params.geo_reference.center_latitude;
      source_config->geo_reference.center_height = c_params.geo_reference.center_height;
      source_config->geo_reference.epsg_code = c_params.geo_reference.epsg_code;
      source_config->enable_lod = c_params.enable_lod;
      source_config->enable_simplification = c_params.enable_simplification;
      source_config->enable_draco = c_params.enable_draco;
      // ... 其他参数
      config.source_config = std::move(source_config);
      config.source_type = DataSourceType::kFbx;
      break;
    }
    default:
      throw std::invalid_argument("Unknown source type");
  }

  // 空间索引配置
  auto slicing_config = std::make_unique<spatial::core::SlicingConfig>();
  slicing_config->max_depth = c_params.max_depth;
  slicing_config->max_items_per_node = c_params.max_items_per_node;
  config.slicing_config = std::move(slicing_config);

  // B3DM 配置
  auto b3dm_config = std::make_unique<b3dm::B3DMGeneratorConfig>();
  b3dm_config->enable_simplification = c_params.enable_simplification;
  b3dm_config->enable_draco = c_params.enable_draco;
  // ... 其他参数
  config.b3dm_config = std::move(b3dm_config);

  // Tileset 配置
  config.tileset_config = std::make_unique<common::TilesetBuilderConfig>();

  return config;
}

}  // namespace
}  // namespace pipeline
```

---

## 6. 代码复用与一致性保证策略

### 6.1 核心原则：**复用现有代码，而非重写**

为确保新接口实现与旧逻辑完全一致，所有关键算法必须**直接复用**现有代码实现，而非重新编写。

### 6.2 必须复用的关键代码模块

| 功能模块 | 现有代码位置 | 复用方式 | 一致性风险 |
|----------|--------------|----------|------------|
| **Shapefile 中心点计算** | `shapefile_processor.cpp:85-95` | 提取为公共函数 | ⚠️ 高 |
| **FBX Y-up 到 Z-up 变换** | `fbx_geometry_extractor.cpp:50-70` | 直接复用矩阵定义 | ⚠️ 高 |
| **geometricError 计算** | `FBXPipeline.cpp:225-228` | 提取为公共函数 | ⚠️ 高 |
| **boundingVolume 转换** | `FBXPipeline.cpp:220-230` | 提取为公共函数 | ⚠️ 高 |
| **Shapefile 数据加载** | `ShapefileDataPool::loadFromShapefileWithGeometry()` | 直接调用 | ⚠️ 中 |
| **FBX 数据加载** | `FBXLoader` 类 | 包装为适配器 | ⚠️ 中 |
| **B3DM 生成** | `B3DMContentGenerator` 类 | 直接调用 | ⚠️ 中 |
| **四叉树分割** | `QuadtreeStrategy` 类 | 直接复用 | ✅ 低 |
| **八叉树分割** | `OctreeStrategy` 类 | 直接复用 | ✅ 低 |

### 6.3 代码复用模式

#### 模式 1：直接调用现有类（推荐）

```cpp
// adapters/shapefile/shapefile_source.cpp
// 直接复用 ShapefileDataPool，不重新实现加载逻辑
bool ShapefileSource::Load(const DataSourceConfig& config) {
    auto& shpConfig = static_cast<const ShapefileSourceConfig&>(config);

    // 复用现有实现
    dataPool_ = std::make_unique<ShapefileDataPool>();
    bool result = dataPool_->loadFromShapefileWithGeometry(
        shpConfig.input_path.string(),
        shpConfig.height_field,
        shpConfig.geo_reference.center_longitude,
        shpConfig.geo_reference.center_latitude
    );

    // 复用中心点重新计算逻辑（shapefile_processor.cpp:85-95）
    if (result) {
        auto worldBounds = dataPool_->computeWorldBounds();
        double centerLon = (worldBounds.minx + worldBounds.maxx) * 0.5;
        double centerLat = (worldBounds.miny + worldBounds.maxy) * 0.5;

        if (std::abs(centerLon - shpConfig.geo_reference.center_longitude) > 1.0 ||
            std::abs(centerLat - shpConfig.geo_reference.center_latitude) > 1.0) {
            // 使用正确的中心点重新加载数据
            dataPool_->clear();
            result = dataPool_->loadFromShapefileWithGeometry(
                shpConfig.input_path.string(),
                shpConfig.height_field,
                centerLon, centerLat
            );
        }
    }

    return result;
}
```

#### 模式 2：提取公共函数

```cpp
// common/geometric_error.h
#pragma once
#include <osg/BoundingBox>

namespace common {

// 提取自 FBXPipeline.cpp:225-228
// 必须与旧实现保持完全一致
inline double CalculateGeometricError(
    const osg::BoundingBoxd& bbox,
    double scale = 1.0) {
    double dx = bbox.xMax() - bbox.xMin();
    double dy = bbox.yMax() - bbox.yMin();
    double dz = bbox.zMax() - bbox.zMin();
    return std::sqrt(dx*dx + dy*dy + dz*dz) / 20.0 * scale;
}

// 提取自 fbx_geometry_extractor.cpp:50-70
// Y-up 到 Z-up 的转换矩阵
inline osg::Matrixd GetYupToZupMatrix() {
    // 注意：OSG 使用列主序矩阵
    return osg::Matrixd(
        1,  0,  0, 0,   // 第一列
        0,  0,  1, 0,   // 第二列
        0, -1,  0, 0,   // 第三列
        0,  0,  0, 1    // 第四列
    );
}

} // namespace common
```

#### 模式 3：适配器包装

```cpp
// adapters/fbx/fbx_geometry_extractor.cpp
// 复用 FBXLoader 的实现，包装为 IGeometryExtractor 接口
std::vector<osg::ref_ptr<osg::Geometry>>
FbxGeometryExtractor::extract(const spatial::core::SpatialItem* item) {
    const auto* fbxItem = dynamic_cast<const FbxSpatialItemAdapter*>(item);
    if (!fbxItem) return {};

    // 获取几何体（复用现有 FBXLoader 的数据）
    const osg::Geometry* geom = fbxItem->getGeometry();
    if (!geom) return {};

    // 克隆几何体
    osg::ref_ptr<osg::Geometry> clonedGeom =
        static_cast<osg::Geometry*>(geom->clone(osg::CopyOp::DEEP_COPY_ALL));

    // 应用世界变换
    osg::Matrixd transform = fbxItem->getTransform();

    // 复用 Y-up 到 Z-up 的转换矩阵（必须与旧代码完全一致）
    osg::Matrixd finalTransform = transform * common::GetYupToZupMatrix();

    // 变换顶点（复用现有逻辑）
    if (auto* vertices = dynamic_cast<osg::Vec3Array*>(clonedGeom->getVertexArray())) {
        for (auto& vertex : *vertices) {
            vertex = vertex * finalTransform;  // OSG 是行向量右乘矩阵
        }
        vertices->dirty();
    }

    // 变换法线（复用现有逻辑）
    osg::Matrixd normalMatrix = osg::Matrixd::inverse(finalTransform);
    normalMatrix.transpose3x3(normalMatrix);

    if (auto* normals = dynamic_cast<osg::Vec3Array*>(clonedGeom->getNormalArray())) {
        for (auto& normal : *normals) {
            normal = osg::Matrixd::transform3x3(normal, normalMatrix);
            normal.normalize();
        }
        normals->dirty();
    }

    return {clonedGeom};
}
```

### 6.4 一致性验证检查点

每个实施步骤必须验证以下检查点：

| 检查点 | 验证方法 | 通过标准 |
|--------|----------|----------|
| **顶点坐标** | 对比 B3DM 中的顶点数据 | 坐标值完全一致（浮点误差 < 1e-6） |
| **法线向量** | 对比 B3DM 中的法线数据 | 向量值完全一致 |
| **包围盒** | 对比 tileset.json 中的 box | 数值完全一致 |
| **geometricError** | 对比 tileset.json | 数值完全一致 |
| **节点结构** | 对比 tileset.json 层级 | 节点数量和关系一致 |
| **B3DM 文件** | 对比文件哈希 | 文件内容一致（或差异可解释） |

### 6.5 各阶段对比验证流程

```
阶段 1: 保存基准数据
    │
    ├── 使用旧管道生成 Shapefile 基准输出 → tests/reference/shapefile/
    └── 使用旧管道生成 FBX 基准输出 → tests/reference/fbx/

阶段 2: Shapefile 新管道
    │
    ├── 实现 Shapefile 新管道
    ├── 编译（新旧代码共存）
    ├── 使用 --use-new-pipeline 生成输出
    └── 与 tests/reference/shapefile/ 对比验证

阶段 3: FBX 新管道
    │
    ├── 实现 FBX 新管道
    ├── 编译（新旧代码共存）
    ├── 使用 --use-new-pipeline 生成 FBX 输出
    ├── 与 tests/reference/fbx/ 对比验证
    ├── 验证 Shapefile 新管道仍可用
    └── 验证旧管道仍可用（不添加 --use-new-pipeline）

阶段 4: 默认切换到新管道
    │
    ├── 修改 main.rs 默认启用新管道
    ├── 验证新管道为默认
    └── 保留 --use-legacy-pipeline 回退选项

阶段 5: 清理旧代码
    │
    └── 删除旧逻辑，只保留新管道转发
```

---

## 7. 详细实施方案

### 实施原则

每个实施步骤完成后，必须能够：
1. **编译通过** - 项目可正常构建
2. **Shapefile 转换** - 执行 `3dtiles -i test.shp -o out -f shape` 成功
3. **FBX 转换** - 执行 `3dtiles -i test.fbx -o out -f fbx --lon 116 --lat 39` 成功
4. **输出一致** - 生成的 tileset.json 和 B3DM 文件与重构前一致

**关键要求**：
- 所有算法逻辑必须复用现有代码，禁止重新实现关键计算逻辑
- 直接在 `src/` 目录中修改，不创建 `src_new/`
- **不修改 CMakeLists.txt**，通过运行时命令行参数切换新旧实现

每个阶段完成后由人工确认，再继续下一阶段。

---

### 阶段 1：建立双轨运行框架（保持旧代码可用）

#### 7.1.1 目标
建立新架构框架，同时保持旧代码完全可用，确保随时可以回退。

#### 7.1.2 实施步骤

**步骤 1.1: 保存旧代码生成的参考输出（基准数据）**
```bash
# 使用旧代码（默认）编译
cargo build -vv

# 生成 Shapefile 参考输出
rm -rf tests/e2e/3dtiles-viewer/public/output
./target/debug/_3dtile -f shape -i data/SHP/bj_building/bj_building.shp \
    -o tests/e2e/3dtiles-viewer/public/output \
    --height height --enable-lod --enable-simplify

# 保存参考数据
mkdir -p tests/reference/shapefile
cp -r tests/e2e/3dtiles-viewer/public/output/* tests/reference/shapefile/

# 生成 FBX 参考输出
rm -rf tests/e2e/3dtiles-viewer/public/output
./target/debug/_3dtile -f fbx -i data/FBX/TZCS_0829.FBX \
    -o tests/e2e/3dtiles-viewer/public/output \
    --lon 118 --lat 32 --alt 20 \
    --enable-draco --enable-texture-compress --enable-lod

# 保存参考数据
mkdir -p tests/reference/fbx
cp -r tests/e2e/3dtiles-viewer/public/output/* tests/reference/fbx/
```

**步骤 1.2: 创建新架构目录（与旧代码共存）**
```bash
# 在 src/ 目录中创建新架构子目录
mkdir -p src/core/spatial
mkdir -p src/core/geometry
mkdir -p src/core/output/b3dm
mkdir -p src/core/output/gltf
mkdir -p src/core/output/tileset
mkdir -p src/adapters/shapefile
mkdir -p src/adapters/fbx
mkdir -p src/pipeline
mkdir -p src/ffi

# 创建测试目录
mkdir -p tests/fixtures/shapefile
mkdir -p tests/fixtures/fbx
mkdir -p tests/reference
```

**步骤 1.3: 实现核心接口（空实现）**
- `src/pipeline/data_source.h` - DataSource 接口定义
- `src/pipeline/conversion_pipeline.h` - ConversionPipeline 基类
- `src/ffi/ffi_bridge.h` - C API 定义

所有方法先做空实现或返回 `false`，确保能编译通过。

**步骤 1.4: 修改现有入口文件支持运行时切换**

修改 `src/shp23dtile.cpp` 和 `src/fbx.cpp`，添加运行时切换逻辑：

```cpp
// src/shp23dtile.cpp - 修改现有文件
#include "pipeline/conversion_pipeline.h"

// 全局标志，由 Rust 通过 FFI 设置
extern "C" bool g_use_new_pipeline = false;

// 设置全局标志（供 Rust 调用）
extern "C" void set_use_new_pipeline(bool use_new) {
    g_use_new_pipeline = use_new;
}

// 原有接口保持不变
extern "C" bool shp23dtile(const ShapeConversionParams* params) {
    if (g_use_new_pipeline) {
        // 转发到新管道
        return shp23dtile_new_pipeline(params);
    }
    // 原有旧实现
    return shp23dtile_legacy(params);
}

// 旧实现（原有代码移动到这里或保持原位）
bool shp23dtile_legacy(const ShapeConversionParams* params) {
    // ... 原有实现不变
}

// 新管道包装器
bool shp23dtile_new_pipeline(const ShapeConversionParams* params) {
    // 阶段 1: 空实现，返回 false
    // 阶段 2: 实现 Shapefile 新管道逻辑
    return false;
}
```

**步骤 1.4: 创建新架构目录（与旧代码共存）**
```bash
# 在 src/ 目录中创建新架构子目录
mkdir -p src/core/spatial
mkdir -p src/core/geometry
mkdir -p src/core/output/b3dm
mkdir -p src/core/output/gltf
mkdir -p src/core/output/tileset
mkdir -p src/adapters/shapefile
mkdir -p src/adapters/fbx
mkdir -p src/pipeline
mkdir -p src/ffi

# 创建测试目录
mkdir -p tests/fixtures/shapefile
mkdir -p tests/fixtures/fbx
mkdir -p tests/reference
```

**步骤 1.5: 实现核心接口（空实现）**
- `src/pipeline/data_source.h` - DataSource 接口定义
- `src/pipeline/conversion_pipeline.h` - ConversionPipeline 基类
- `src/ffi/ffi_bridge.h` - C API 定义

所有方法先做空实现或返回 `false`，确保能编译通过。

#### 7.1.3 阶段 1 完成标准
- [ ] 旧代码完全未改动，可正常编译运行
- [ ] **基准数据已保存到 `tests/reference/`**（Shapefile 和 FBX 各一份）
- [ ] 新代码框架目录结构已创建
- [ ] 新代码框架可编译（空实现，返回失败）

**阶段 1 交付物：**
```
tests/reference/
├── shapefile/           # Shapefile 基准输出
│   ├── tileset.json
│   ├── data/
│   │   ├── 0_0_0.b3dm
│   │   └── ...
│   └── ...
└── fbx/                 # FBX 基准输出
    ├── tileset.json
    ├── data/
    │   ├── 0_0_0.b3dm
    │   └── ...
    └── ...
```

**验证步骤：**
```bash
# 1. 编译项目
cargo build -vv

# 2. 生成 Shapefile 基准数据
rm -rf tests/e2e/3dtiles-viewer/public/output
./target/debug/_3dtile -f shape -i data/SHP/bj_building/bj_building.shp \
    -o tests/e2e/3dtiles-viewer/public/output \
    --height height --enable-lod --enable-simplify
mkdir -p tests/reference/shapefile
cp -r tests/e2e/3dtiles-viewer/public/output/* tests/reference/shapefile/

# 3. 生成 FBX 基准数据
rm -rf tests/e2e/3dtiles-viewer/public/output
./target/debug/_3dtile -f fbx -i data/FBX/TZCS_0829.FBX \
    -o tests/e2e/3dtiles-viewer/public/output \
    --lon 118 --lat 32 --alt 20 \
    --enable-draco --enable-texture-compress --enable-lod
mkdir -p tests/reference/fbx
cp -r tests/e2e/3dtiles-viewer/public/output/* tests/reference/fbx/

# 4. 验证基准数据完整
ls -la tests/reference/shapefile/tileset.json
ls -la tests/reference/fbx/tileset.json
```

**说明：** 阶段 1 只保存基准数据，新管道为空实现，无法执行完整转换。阶段 2 开始实现 Shapefile 新管道，阶段 3 实现 FBX 新管道。

---

### 阶段 2：实现 Shapefile 新管道（保持旧代码可用）

#### 7.2.1 目标
在 `src/` 中实现 Shapefile 的新管道，复用现有代码逻辑，确保输出一致。

#### 7.2.2 实施步骤

**步骤 2.1: 创建公共工具函数（提取自旧代码）**
- 文件: `src/common/shapefile_utils.h/cpp`
- **复用** `shapefile/shapefile_processor.cpp:85-95` 的中心点重新计算逻辑
- **复用** `shapefile/ShapefileDataPool::loadFromShapefileWithGeometry()` 数据加载逻辑
- **禁止重新实现**，只允许提取和包装

```cpp
// common/shapefile_utils.h
#pragma once
#include "shapefile/shapefile_data_pool.h"

namespace common {

// 提取自 shapefile_processor.cpp:85-95
// 必须与旧实现保持完全一致
inline bool ReloadDataWithCorrectCenter(
    ShapefileDataPool* dataPool,
    const std::string& inputPath,
    const std::string& heightField,
    double expectedCenterLon,
    double expectedCenterLat) {

    auto worldBounds = dataPool->computeWorldBounds();
    double centerLon = (worldBounds.minx + worldBounds.maxx) * 0.5;
    double centerLat = (worldBounds.miny + worldBounds.maxy) * 0.5;

    if (std::abs(centerLon - expectedCenterLon) > 1.0 ||
        std::abs(centerLat - expectedCenterLat) > 1.0) {
        dataPool->clear();
        return dataPool->loadFromShapefileWithGeometry(
            inputPath, heightField, centerLon, centerLat);
    }
    return true;
}

} // namespace common
```

**步骤 2.2: 实现 ShapefileSource（复用现有类）**
- 文件: `src/adapters/shapefile/shapefile_source.h/cpp`
- **复用** `ShapefileDataPool` 类进行数据加载
- **复用** `ShapefileSpatialItemAdapter` 进行空间项适配
- `Load()` 方法调用 `ShapefileDataPool::loadFromShapefileWithGeometry()`
- 使用步骤 2.1 的公共函数处理中心点重新计算

```cpp
// adapters/shapefile/shapefile_source.cpp
bool ShapefileSource::Load(const DataSourceConfig& config) {
    auto& shpConfig = static_cast<const ShapefileSourceConfig&>(config);

    // 复用现有 ShapefileDataPool
    dataPool_ = std::make_unique<ShapefileDataPool>();
    bool result = dataPool_->loadFromShapefileWithGeometry(
        shpConfig.input_path.string(),
        shpConfig.height_field,
        shpConfig.geo_reference.center_longitude,
        shpConfig.geo_reference.center_latitude
    );

    if (result) {
        // 复用中心点重新计算逻辑
        result = common::ReloadDataWithCorrectCenter(
            dataPool_.get(),
            shpConfig.input_path.string(),
            shpConfig.height_field,
            shpConfig.geo_reference.center_longitude,
            shpConfig.geo_reference.center_latitude
        );
    }

    return result;
}
```

**步骤 2.3: 实现几何提取器（复用现有实现）**
- 文件: `src/adapters/shapefile/shapefile_geometry_extractor.cpp`
- **复用** `GeometryExtractor` 或 `B3DMContentGenerator` 的几何处理逻辑
- **禁止重新实现** OGRGeometry 到 OSG 几何体的转换

**步骤 2.4: 复用四叉树空间索引**
- 文件: 无需新文件
- **直接复用** `src/spatial/strategy/quadtree_strategy.h/cpp`
- 确保分割参数与旧代码一致

**步骤 2.5: 实现 ShapefilePipeline（复用 B3DM 生成器）**
- 文件: `src/pipeline/shapefile_pipeline.cpp`
- **复用** `B3DMContentGenerator` 生成 B3DM 文件
- **复用** `ShapefileTilesetAdapter` 生成 tileset
- 实现 `CreateSlicingStrategy()` 返回现有的 `QuadtreeStrategy`

```cpp
// pipeline/shapefile_pipeline.cpp
std::unique_ptr<spatial::core::SlicingStrategy>
ShapefilePipeline::CreateSlicingStrategy() {
    // 复用现有的 QuadtreeStrategy
    return std::make_unique<spatial::strategy::QuadtreeStrategy>();
}
```

**步骤 2.6: 编译并验证新管道（与基准数据对比）**

```bash
# 编译项目（新旧代码共存）
cargo build -vv

# 生成 Shapefile 输出（新管道）
rm -rf tests/e2e/3dtiles-viewer/public/output
./target/debug/_3dtile -f shape -i data/SHP/bj_building/bj_building.shp \
    -o tests/e2e/3dtiles-viewer/public/output \
    --height height --enable-lod --enable-simplify --use-new-pipeline

# 对比验证检查点：
# 1. 顶点坐标一致性（误差 < 1e-6）
# 2. 包围盒数值一致性
# 3. geometricError 一致性
# 4. 节点结构一致性
# 5. B3DM 文件哈希一致性
```

**自动化对比脚本（建议添加到 tests/compare.sh）：**
```bash
#!/bin/bash
# tests/compare.sh - 对比新输出与基准数据

set -e

REFERENCE_DIR="tests/reference/shapefile"
OUTPUT_DIR="tests/e2e/3dtiles-viewer/public/output"

echo "=== 对比 tileset.json ==="
# 对比关键字段：geometricError、boundingVolume
diff <(jq '.root.geometricError' $REFERENCE_DIR/tileset.json) \
     <(jq '.root.geometricError' $OUTPUT_DIR/tileset.json) \
     || { echo "❌ geometricError 不匹配"; exit 1; }

echo "✅ geometricError 匹配"

# 对比 B3DM 文件数量
ref_count=$(find $REFERENCE_DIR -name "*.b3dm" | wc -l)
out_count=$(find $OUTPUT_DIR -name "*.b3dm" | wc -l)
if [ "$ref_count" -eq "$out_count" ]; then
    echo "✅ B3DM 文件数量匹配: $ref_count"
else
    echo "❌ B3DM 文件数量不匹配: 基准=$ref_count, 输出=$out_count"
    exit 1
fi

# 对比每个 B3DM 文件大小（允许 1% 误差）
for ref_file in $REFERENCE_DIR/data/*.b3dm; do
    filename=$(basename $ref_file)
    out_file="$OUTPUT_DIR/data/$filename"
    if [ ! -f "$out_file" ]; then
        echo "❌ 缺少文件: $filename"
        exit 1
    fi

    ref_size=$(stat -f%z "$ref_file" 2>/dev/null || stat -c%s "$ref_file")
    out_size=$(stat -f%z "$out_file" 2>/dev/null || stat -c%s "$out_file")

    diff_percent=$(echo "scale=2; abs($ref_size - $out_size) * 100 / $ref_size" | bc)
    if (( $(echo "$diff_percent > 1" | bc -l) )); then
        echo "❌ 文件大小差异过大: $filename (差异 ${diff_percent}%)"
        exit 1
    fi
done
echo "✅ 所有 B3DM 文件大小匹配"

echo ""
echo "=== 阶段 2 验证通过 ==="
```

#### 7.2.3 阶段 2 完成标准
- [ ] Shapefile 新管道可执行**完整转换流程**
- [ ] **输出可与基准数据对比验证**
- [ ] `cargo build -vv` 编译成功（新旧代码共存）
- [ ] **顶点坐标与基准数据一致**（误差 < 1e-6）
- [ ] **包围盒数值与基准数据一致**
- [ ] **geometricError 与基准数据一致**
- [ ] **B3DM 文件内容与基准数据一致**
- [ ] 旧管道仍可用（不添加 `--use-new-pipeline` 参数）

**阶段 2 交付物：**
- 可工作的 Shapefile 新管道实现
- 通过对比验证的输出结果
- 阶段 2 验证报告（包含对比数据）

**验证命令：**
```bash
# 1. 编译
cargo build -vv

# 2. 使用新管道生成输出
rm -rf tests/e2e/3dtiles-viewer/public/output
./target/debug/_3dtile -f shape -i data/SHP/bj_building/bj_building.shp \
    -o tests/e2e/3dtiles-viewer/public/output \
    --height height --enable-lod --enable-simplify --use-new-pipeline

# 3. 与基准数据对比
./tests/compare.sh

# 4. 验证旧管道仍可用
rm -rf tests/e2e/3dtiles-viewer/public/output
./target/debug/_3dtile -f shape -i data/SHP/bj_building/bj_building.shp \
    -o tests/e2e/3dtiles-viewer/public/output \
    --height height --enable-lod --enable-simplify
# 应使用旧管道成功执行
```

---

### 阶段 3：实现 FBX 新管道（保持旧代码可用）

#### 7.3.1 目标
在 `src/` 中实现 FBX 的新管道，复用现有代码逻辑，确保输出一致。

#### 7.3.2 实施步骤

**步骤 3.1: 创建公共工具函数（提取自旧代码）**
- 文件: `src/common/fbx_utils.h/cpp`
- **复用** `fbx/fbx_geometry_extractor.cpp:50-70` 的 Y-up 到 Z-up 变换矩阵
- **复用** `FBXPipeline.cpp:225-228` 的 geometricError 计算逻辑
- **复用** `FBXPipeline.cpp:220-230` 的 boundingVolume 转换逻辑
- **禁止重新实现**，只允许提取和包装

```cpp
// common/fbx_utils.h
#pragma once
#include <osg/BoundingBox>
#include <osg/Matrix>

namespace common {

// 提取自 fbx_geometry_extractor.cpp:50-70
// Y-up 到 Z-up 的转换矩阵
// 注意：OSG 使用列主序矩阵，必须与旧代码完全一致
inline osg::Matrixd GetYupToZupMatrix() {
    return osg::Matrixd(
        1,  0,  0, 0,   // 第一列: x' = 1*x + 0*y + 0*z
        0,  0,  1, 0,   // 第二列: y' = 0*x + 0*y + 1*z = z
        0, -1,  0, 0,   // 第三列: z' = 0*x - 1*y + 0*z = -y
        0,  0,  0, 1    // 第四列
    );
}

// 提取自 FBXPipeline.cpp:225-228
// geometricError 计算公式
inline double CalculateGeometricError(
    const osg::BoundingBoxd& bbox,
    double scale = 1.0) {
    double dx = bbox.xMax() - bbox.xMin();
    double dy = bbox.yMax() - bbox.yMin();
    double dz = bbox.zMax() - bbox.zMin();
    return std::sqrt(dx*dx + dy*dy + dz*dz) / 20.0 * scale;
}

// 提取自 FBXPipeline.cpp:220-230
// boundingVolume 转换（Y-up 到 Z-up）
inline osg::BoundingBoxd ConvertBoundsYupToZup(
    const std::array<double, 3>& min,
    const std::array<double, 3>& max) {
    // 转换: (x, y, z) -> (x, -z, y)
    osg::BoundingBoxd bbox;
    bbox.expandBy(osg::Vec3d(min[0], -max[2], min[1]));
    bbox.expandBy(osg::Vec3d(max[0], -min[2], max[1]));
    return bbox;
}

} // namespace common
```

**步骤 3.2: 实现 FbxSource（复用 FBXLoader）**
- 文件: `src/adapters/fbx/fbx_source.h/cpp`
- **复用** `FBXLoader` 类进行数据加载
- **复用** `FbxSpatialItemAdapter` 进行空间项适配
- `Load()` 方法调用 `FBXLoader::load()`

```cpp
// adapters/fbx/fbx_source.cpp
bool FbxSource::Load(const DataSourceConfig& config) {
    auto& fbxConfig = static_cast<const FbxSourceConfig&>(config);

    // 复用现有 FBXLoader
    loader_ = std::make_unique<FBXLoader>(fbxConfig.input_path.string());
    if (!loader_->load()) {
        return false;
    }

    // 复用 FbxSpatialItemAdapter 创建空间项
    spatialItems_ = fbx::createSpatialItems(loader_.get());

    return !spatialItems_.empty();
}
```

**步骤 3.3: 实现 FBX 几何提取器（复用变换逻辑）**
- 文件: `src/adapters/fbx/fbx_geometry_extractor.cpp`
- **复用** `common::GetYupToZupMatrix()` 进行坐标变换
- **复用** `common::CalculateGeometricError()` 计算误差
- **复用** `common::ConvertBoundsYupToZup()` 转换包围盒
- **禁止重新实现** 坐标变换逻辑

```cpp
// adapters/fbx/fbx_geometry_extractor.cpp
std::vector<osg::ref_ptr<osg::Geometry>>
FbxGeometryExtractor::extract(const spatial::core::SpatialItem* item) {
    const auto* fbxItem = dynamic_cast<const FbxSpatialItemAdapter*>(item);
    if (!fbxItem) return {};

    const osg::Geometry* geom = fbxItem->getGeometry();
    if (!geom) return {};

    // 克隆几何体
    osg::ref_ptr<osg::Geometry> clonedGeom =
        static_cast<osg::Geometry*>(geom->clone(osg::CopyOp::DEEP_COPY_ALL));

    // 复用 Y-up 到 Z-up 的转换矩阵（必须与旧代码完全一致）
    osg::Matrixd transform = fbxItem->getTransform();
    osg::Matrixd finalTransform = transform * common::GetYupToZupMatrix();

    // 变换顶点
    if (auto* vertices = dynamic_cast<osg::Vec3Array*>(clonedGeom->getVertexArray())) {
        for (auto& vertex : *vertices) {
            vertex = vertex * finalTransform;  // OSG 是行向量右乘矩阵
        }
        vertices->dirty();
    }

    // 变换法线
    osg::Matrixd normalMatrix = osg::Matrixd::inverse(finalTransform);
    normalMatrix.transpose3x3(normalMatrix);

    if (auto* normals = dynamic_cast<osg::Vec3Array*>(clonedGeom->getNormalArray())) {
        for (auto& normal : *normals) {
            normal = osg::Matrixd::transform3x3(normal, normalMatrix);
            normal.normalize();
        }
        normals->dirty();
    }

    return {clonedGeom};
}
```

**步骤 3.4: 复用八叉树空间索引**
- 文件: 无需新文件
- **直接复用** `src/spatial/strategy/octree_strategy.h/cpp`
- 确保分割参数与旧代码一致

**步骤 3.5: 实现 FbxPipeline（复用 B3DM 生成器）**
- 文件: `src/pipeline/fbx_pipeline.cpp`
- **复用** `B3DMGenerator` 生成 B3DM 文件
- **复用** `FbxTilesetAdapter` 生成 tileset
- **复用** `common::CalculateGeometricError()` 计算 geometricError
- **复用** `common::ConvertBoundsYupToZup()` 转换包围盒
- 实现 `CreateSlicingStrategy()` 返回现有的 `OctreeStrategy`

```cpp
// pipeline/fbx_pipeline.cpp
std::unique_ptr<spatial::core::SlicingStrategy>
FbxPipeline::CreateSlicingStrategy() {
    // 复用现有的 OctreeStrategy
    return std::make_unique<spatial::strategy::OctreeStrategy>();
}

common::GeometricErrorCalculator
FbxPipeline::GetGeometricErrorCalculator() const {
    // 复用提取的公共函数
    return [](const auto& bbox) {
        return common::CalculateGeometricError(bbox);
    };
}
```

**步骤 3.6: 编译并验证新管道（与基准数据对比）**

```bash
# 编译项目（新旧代码共存）
cargo build -vv

# 生成 FBX 输出（新管道）
rm -rf tests/e2e/3dtiles-viewer/public/output
./target/debug/_3dtile -f fbx -i data/FBX/TZCS_0829.FBX \
    -o tests/e2e/3dtiles-viewer/public/output \
    --lon 118 --lat 32 --alt 20 \
    --enable-draco --enable-texture-compress --enable-lod --use-new-pipeline

# 对比验证检查点：
# 1. 顶点坐标一致性（变换后，误差 < 1e-6）
# 2. 法线向量一致性
# 3. 包围盒数值一致性
# 4. geometricError 一致性
# 5. 节点结构一致性
# 6. B3DM 文件哈希一致性
```

**FBX 对比脚本（建议添加到 tests/compare_fbx.sh）：**
```bash
#!/bin/bash
# tests/compare_fbx.sh - 对比 FBX 新输出与基准数据

set -e

REFERENCE_DIR="tests/reference/fbx"
OUTPUT_DIR="tests/e2e/3dtiles-viewer/public/output"

echo "=== 对比 FBX 转换结果 ==="

# 对比 tileset.json 关键字段
diff <(jq '.root.geometricError' $REFERENCE_DIR/tileset.json) \
     <(jq '.root.geometricError' $OUTPUT_DIR/tileset.json) \
     || { echo "❌ geometricError 不匹配"; exit 1; }
echo "✅ geometricError 匹配"

# 对比 B3DM 文件数量
ref_count=$(find $REFERENCE_DIR -name "*.b3dm" | wc -l)
out_count=$(find $OUTPUT_DIR -name "*.b3dm" | wc -l)
if [ "$ref_count" -eq "$out_count" ]; then
    echo "✅ B3DM 文件数量匹配: $ref_count"
else
    echo "❌ B3DM 文件数量不匹配: 基准=$ref_count, 输出=$out_count"
    exit 1
fi

# FBX 特有的对比：检查法线向量（如果工具可用）
# 这里可以添加 b3dm 解析工具来对比顶点/法线数据

echo ""
echo "=== 阶段 3 验证通过 ==="
```

#### 7.3.3 阶段 3 完成标准
- [ ] FBX 新管道可执行**完整转换流程**
- [ ] **输出可与基准数据对比验证**
- [ ] `cargo build -vv` 编译成功（新旧代码共存）
- [ ] **顶点坐标与基准数据一致**（误差 < 1e-6）
- [ ] **法线向量与基准数据一致**
- [ ] **包围盒数值与基准数据一致**
- [ ] **geometricError 与基准数据一致**
- [ ] **B3DM 文件内容与基准数据一致**
- [ ] 地理参考参数正确处理
- [ ] 旧管道仍可用（不添加 `--use-new-pipeline` 参数）

**阶段 3 交付物：**
- 可工作的 FBX 新管道实现
- 通过对比验证的输出结果
- 阶段 3 验证报告（包含对比数据）
- Shapefile 和 FBX 双管道都可用

**验证命令：**
```bash
# 1. 编译
cargo build -vv

# 2. 使用新管道生成 FBX 输出
rm -rf tests/e2e/3dtiles-viewer/public/output
./target/debug/_3dtile -f fbx -i data/FBX/TZCS_0829.FBX \
    -o tests/e2e/3dtiles-viewer/public/output \
    --lon 118 --lat 32 --alt 20 \
    --enable-draco --enable-texture-compress --enable-lod --use-new-pipeline

# 3. 与基准数据对比
./tests/compare_fbx.sh

# 4. 验证 Shapefile 新管道仍可用
rm -rf tests/e2e/3dtiles-viewer/public/output
./target/debug/_3dtile -f shape -i data/SHP/bj_building/bj_building.shp \
    -o tests/e2e/3dtiles-viewer/public/output \
    --height height --enable-lod --enable-simplify --use-new-pipeline
./tests/compare.sh

# 5. 验证旧管道仍可用（Shapefile）
rm -rf tests/e2e/3dtiles-viewer/public/output
./target/debug/_3dtile -f shape -i data/SHP/bj_building/bj_building.shp \
    -o tests/e2e/3dtiles-viewer/public/output \
    --height height --enable-lod --enable-simplify

# 6. 验证旧管道仍可用（FBX）
rm -rf tests/e2e/3dtiles-viewer/public/output
./target/debug/_3dtile -f fbx -i data/FBX/TZCS_0829.FBX \
    -o tests/e2e/3dtiles-viewer/public/output \
    --lon 118 --lat 32 --alt 20 \
    --enable-draco --enable-texture-compress --enable-lod
```

---

### 阶段 4：全面验证和切换

#### 7.4.1 目标
全面验证新管道，将默认实现切换到新管道。

#### 7.4.2 实施步骤

**步骤 4.1: 扩展数据集验证**
- 使用多个不同规模的数据集测试
- 验证各种参数组合

**步骤 4.2: 性能对比**
- 对比新旧管道的执行时间
- 确保新管道性能不低于旧管道的 90%

**步骤 4.3: 切换默认实现**
- 修改 `main.rs`：默认设置 `use_new_pipeline = true`
- 旧代码保留在 `src/` 中，与新代码共存
- 通过 `--use-new-pipeline` 参数控制（默认启用）

```rust
// main.rs 修改
let use_new_pipeline = !matches.get_flag("use-legacy-pipeline");  // 默认启用新管道

// 设置 C++ 全局标志
unsafe {
    set_use_new_pipeline(use_new_pipeline);
}
```

**命令行使用：**
```bash
# 默认使用新管道
./target/debug/_3dtile -f shape -i input.shp -o output

# 显式使用旧管道
./target/debug/_3dtile -f shape -i input.shp -o output --use-legacy-pipeline
```

**步骤 4.4: 添加命令行参数回退机制**
- 保留旧代码作为回退选项
- 通过 `--use-legacy-pipeline` 命令行参数切换回旧实现

```bash
# 默认使用新管道
./target/debug/_3dtile -f shape -i input.shp -o output

# 显式使用旧管道
./target/debug/_3dtile -f shape -i input.shp -o output --use-legacy-pipeline
```

#### 7.4.3 阶段 4 完成标准
- [ ] 多个数据集验证通过（人工确认）
- [ ] 性能达到要求
- [ ] 默认实现已切换到新管道（`main.rs` 默认启用）
- [ ] `--use-legacy-pipeline` 回退机制工作正常
- [ ] 旧代码保留在 `src/` 中与新代码共存

**验证命令：**
```bash
# 编译
cargo build -vv

# 默认使用新管道
rm -rf tests/e2e/3dtiles-viewer/public/output
./target/debug/_3dtile -f shape -i data/SHP/bj_building/bj_building.shp \
    -o tests/e2e/3dtiles-viewer/public/output \
    --height height --enable-lod --enable-simplify
# 人工确认输出正常

# 回退到旧管道验证
rm -rf tests/e2e/3dtiles-viewer/public/output
./target/debug/_3dtile -f shape -i data/SHP/bj_building/bj_building.shp \
    -o tests/e2e/3dtiles-viewer/public/output \
    --height height --enable-lod --enable-simplify --use-legacy-pipeline
# 确认旧实现仍然可用
```

---

### 阶段 5：清理和优化

#### 7.5.1 目标
删除旧代码，优化新实现，完善文档。

#### 7.5.2 实施步骤

**步骤 5.1: 删除旧代码**
- 删除旧实现文件中的旧逻辑：
  - `src/shp23dtile.cpp` 中的 `shp23dtile_legacy()` 函数
  - `src/fbx.cpp` 中的 `fbx23dtile_legacy()` 函数
  - 保留文件，但只保留新管道转发逻辑
- 清理 `main.rs` 中的 `--use-legacy-pipeline` 参数（不再支持旧管道）
- CMakeLists.txt 保持不变（始终使用 GLOB 收集所有文件）

```cpp
// src/shp23dtile.cpp 最终状态 - 只保留新管道
extern "C" bool shp23dtile(const ShapeConversionParams* params) {
    // 直接转发到新管道（不再支持旧管道）
    return shp23dtile_new_pipeline(params);
}
```

```rust
// main.rs 最终状态 - 移除 --use-legacy-pipeline 参数
// 不再添加 use-legacy-pipeline 参数，始终使用新管道
```

**步骤 5.2: 代码优化**
- 性能分析（Profiling）
- 内存优化
- 并发优化（如适用）

**步骤 5.3: 更新文档**
- 更新 README.md
- 更新架构文档

#### 7.5.3 阶段 5 完成标准
- [ ] 旧代码已完全删除
- [ ] 项目可正常编译运行
- [ ] 所有转换功能正常工作（人工确认）
- [ ] 性能达到或超过旧实现
- [ ] 文档已更新

---

## 8. 目录结构重构方案

### 8.1 新目录结构

```
src/
├── core/                          # 核心框架层（稳定、通用）
│   ├── spatial/                   # 空间索引核心
│   │   ├── spatial_item.h
│   │   ├── spatial_bounds.h
│   │   └── slicing_strategy.h
│   ├── geometry/                  # 几何处理核心
│   │   ├── geometry_extractor.h
│   │   └── mesh_processor.h
│   └── output/                    # 输出格式核心
│       ├── b3dm/
│       ├── gltf/
│       └── tileset/
│
├── adapters/                      # 数据源适配器层
│   ├── shapefile/                 # Shapefile 适配器
│   │   ├── shapefile_source.h/cpp        # 实现 DataSource 接口
│   │   ├── shapefile_geometry_extractor.h/cpp
│   │   ├── shapefile_spatial_item_adapter.h/cpp
│   │   └── shapefile_tile_meta.h
│   │
│   ├── fbx/                       # FBX 适配器
│   │   ├── fbx_source.h/cpp              # 实现 DataSource 接口
│   │   ├── fbx_geometry_extractor.h/cpp
│   │   ├── fbx_spatial_item_adapter.h/cpp
│   │   └── fbx_tile_meta.h
│   │
│   └── osgb/                      # OSGB 适配器（未来）
│
├── pipeline/                      # 统一管道层（新架构核心）
│   ├── data_source.h              # DataSource 接口
│   ├── data_source_factory.h/cpp
│   ├── conversion_pipeline.h/cpp  # 模板方法模式
│   ├── shapefile_pipeline.h/cpp   # Shapefile 专用管道
│   ├── fbx_pipeline.h/cpp         # FBX 专用管道
│   └── ffi_bridge.h/cpp           # Rust FFI 桥接
│
├── shapefile/                     # 现有 Shapefile 实现（复用）
│   ├── shapefile_processor.h/cpp
│   ├── shapefile_data_pool.h/cpp
│   ├── b3dm_content_generator.h/cpp
│   └── ...
│
├── fbx/                           # 现有 FBX 实现（复用）
│   ├── fbx_loader.h/cpp
│   ├── fbx_tileset_adapter.h/cpp
│   ├── fbx_geometry_extractor.h/cpp
│   └── ...
│
├── spatial/                       # 现有空间索引实现（复用）
│   └── strategy/
│       ├── quadtree_strategy.h/cpp
│       └── octree_strategy.h/cpp
│
├── b3dm/                          # 现有 B3DM 实现（复用）
│   ├── b3dm_generator.h/cpp
│   └── b3dm_writer.h/cpp
│
├── common/                        # 现有公共代码（复用）
│   └── ...
│
└── utils/                         # 工具类
    ├── extern.h/cpp
    ├── attribute_storage.h/cpp
    ├── GeoidHeight.h/cpp
    └── dxt_img.h/cpp

# Rust 代码（项目根目录）
# src/
# ├── main.rs                      # 简洁入口
# ├── lib.rs
# ├── common.rs
# ├── error.rs
# ├── fun_c.rs
# ├── utils.rs
# ├── shape.rs                     # Shapefile 转换
# ├── fbx.rs                       # FBX 转换
# └── osgb.rs                      # OSGB 转换

# 旧实现文件（迁移期间保留，与新代码共存）
# 这些文件在 src/ 根目录，迁移完成后只保留转发逻辑：
# - shp23dtile.cpp（Shapefile 入口，内部转发到新管道）
# - fbx.cpp（FBX 入口，内部转发到新管道）
# - FBXPipeline.cpp（FBX 管道实现）
```

### 8.2 分层职责

| 目录 | 职责 | 依赖方向 |
|------|------|----------|
| `core/` | 通用核心，不依赖任何数据源 | 无 |
| `adapters/` | 数据源适配，实现 `DataSource` 接口 | core → adapters |
| `pipeline/` | 流程编排，组合 core 和 adapters | core/adapters → pipeline |
| `shapefile/` | 现有 Shapefile 实现（复用） | adapters → shapefile |
| `fbx/` | 现有 FBX 实现（复用） | adapters → fbx |
| `spatial/` | 现有空间索引实现（复用） | core → spatial |
| `b3dm/` | 现有 B3DM 实现（复用） | core/output → b3dm |
| `common/` | 现有公共代码（复用） | 无 |

### 8.3 代码迁移映射

| 旧文件 | 处理方式 | 说明 |
|--------|----------|------|
| `src/shp23dtile.cpp` | **修改转发** | 保留文件，内部添加新旧管道切换逻辑，最终只保留新管道转发 |
| `src/fbx.cpp` | **修改转发** | 保留文件，内部添加新旧管道切换逻辑，最终只保留新管道转发 |
| `src/FBXPipeline.cpp` | **保留复用** | FBX 管道实现，被新管道适配器调用 |
| `src/shapefile/*` | **直接复用** | 现有实现保持不变，新适配器直接调用 |
| `src/fbx/*` | **直接复用** | 现有实现保持不变，新适配器直接调用 |
| `src/spatial/*` | **直接复用** | 现有实现保持不变 |
| `src/b3dm/*` | **直接复用** | 现有实现保持不变 |
| `src/common/*` | **直接复用** | 现有实现保持不变，提取关键函数到此目录 |
| `src/gltf/`, `src/tileset/` | **直接复用** | 现有实现保持不变 |

### 8.4 依赖关系

```
src/main.rs (Rust)
    │
    ▼ FFI 调用
src/shp23dtile.cpp 或 src/fbx.cpp (C++)
    │
    ▼ 内部转发
pipeline/conversion_pipeline.h
    │
    ├──► adapters/shapefile/shapefile_source.h ──► core/spatial/
    │
    └──► adapters/fbx/fbx_source.h ──────────────► core/spatial/
                           │
                           ▼
                    core/output/b3dm/
                    core/output/gltf/
                    core/output/tileset/
```

---

## 9. 关键设计决策说明

### 9.1 管道命名策略

采用**数据源导向**的命名方式，而非切片策略导向：

| 管道类 | 使用的空间索引 | 说明 |
|--------|---------------|------|
| `ShapefilePipeline` | QuadtreeStrategy | Shapefile 目前使用四叉树，但未来可能支持其他策略 |
| `FbxPipeline` | OctreeStrategy | FBX 目前使用八叉树，但未来可能支持其他策略 |

这种命名方式的优势：
1. **语义清晰** - 管道为特定数据源服务，而非特定算法
2. **易于扩展** - 如果 Shapefile 未来需要支持八叉树，只需修改 `ShapefilePipeline` 内部实现
3. **隐藏实现细节** - 调用方不需要知道内部使用什么空间索引策略

### 9.2 地理参考处理

- **Shapefile**：从 .shp 文件自动解析坐标系，计算中心点，转换为 WGS84
- **FBX**：必须通过 `--lon`, `--lat`, `--alt` 参数显式指定，无默认值

### 9.3 错误处理

- C++ 侧使用异常处理内部错误，FFI 边界转换为错误码/消息
- Rust 侧使用 `Result` 类型传递错误

### 9.4 内存管理

- C++ 侧使用智能指针管理资源
- FFI 边界明确所有权：Rust 分配的参数，C++ 执行，Rust 释放结果

### 9.5 向后兼容

- 不保留旧 API 兼容层，直接替换
- 新的统一 API 通过 `convert_with_pipeline` 暴露
- 旧代码移动到 `legacy/` 目录，迁移完成后直接删除

---

## 10. 代码规范检查清单

- [ ] 使用 C++20 特性（概念、约束、范围等）
- [ ] 遵循 Google C++ 命名规范
- [ ] 遵循 C++ Core Guidelines
- [ ] 所有公共接口使用 `[[nodiscard]]`
- [ ] 使用智能指针管理资源
- [ ] 使用 `explicit` 防止隐式转换
- [ ] 使用 `noexcept` 标记不抛出异常的函数
- [ ] 使用 PIMPL 模式隐藏实现细节
- [ ] Rust 代码使用 `unsafe` 块并标注安全前提

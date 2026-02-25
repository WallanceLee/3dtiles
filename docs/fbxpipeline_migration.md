# FBXPipeline 迁移方案

## 1. 概述

### 1.1 目标
将现有的FBXPipeline从自定义八叉树实现迁移到统一的空间切片抽象框架，实现：
1. 复用 `spatial::strategy::OctreeStrategy` 进行空间索引
2. 复用 `common::TilesetBuilder` 进行Tileset构建
3. 复用 `b3dm::B3DMGenerator` 进行B3DM生成
4. 采用 `tile/{z}/{x}/{y}/` 统一目录结构
5. 每个阶段都能产生可运行的3D Tiles输出

### 1.2 核心原则

**每个阶段必须真正替换旧实现，而不是并行保留。**

- 阶段N完成后，旧实现被移除
- 新实现通过适配器保持与老接口兼容
- 每个阶段都是"单行道"，验证通过后才能进入下一阶段
- 每个阶段的成果都必须能产生可运行的3D Tiles数据，可在Cesium中查看验证

### 1.3 参考架构
基于以下已有模块：
- `spatial/strategy/octree_strategy.h` - 八叉树策略
- `common/tileset_builder.h` - Tileset构建器
- `b3dm/b3dm_generator.h` - B3DM生成器
- `common/geometry_extractor.h` - 几何体提取器接口
- `shapefile/shapefile_processor.h` - Shapefile处理器（参考实现）

---

## 2. 现状分析

### 2.1 当前FBXPipeline架构
```
FBXPipeline
├── FBXLoader (加载FBX，构建meshPool)
├── 内嵌OctreeNode结构
├── buildOctree() (自定义八叉树构建)
├── processNode() (递归处理节点)
│   └── createB3DM() (生成B3DM)
└── writeTilesetJson() (写入tileset.json)
```

### 2.2 当前数据结构
```cpp
// FBXPipeline.h
struct InstanceRef {
    MeshInstanceInfo* meshInfo;
    int transformIndex;
};

struct OctreeNode {
    osg::BoundingBox bbox;
    std::vector<InstanceRef> content;
    std::vector<OctreeNode*> children;
    int depth = 0;
};
```

### 2.3 当前输出结构
```
output/
├── tileset.json
└── tile_0/
    ├── content.b3dm
    ├── content_lod1.b3dm
    └── content_lod2.b3dm
```

---

## 3. 迁移后架构（基于已有架构复用）

```
FBXPipeline (新)
├── FBXLoader (保持不变)
├── FBXSpatialItemAdapter ✅ (阶段1已完成)
├── OctreeStrategy (复用spatial模块)
├── LODPipeline (复用已有lod_pipeline.h)
├── B3DMGenerator (复用b3dm模块)
├── GLTFBuilder (复用gltf/gltf_builder.h)
├── Geometry/Material/TextureUtils ✅ (已完成)
└── TilesetBuilder (复用common模块)
```

### 3.1 架构复用说明

| 组件 | 状态 | 说明 |
|------|------|------|
| `FBXSpatialItemAdapter` | ✅ 已完成 | 阶段1实现 |
| `GeometryUtils/MaterialUtils/TextureUtils` | ✅ 已完成 | 工具类重构 |
| `LODPipeline` | ✅ 已存在 | `lod_pipeline.h` |
| `GLTFBuilder` | ✅ 已存在 | `gltf/gltf_builder.h` |
| `B3DMGenerator` | ✅ 已存在 | `b3dm/b3dm_generator.h` |
| `OctreeStrategy` | ✅ 已存在 | `spatial/strategy/octree_strategy.h` |
| `TilesetBuilder` | ✅ 已存在 | `common/tileset_builder.h` |

### 3.2 目录结构迁移
从 `tile_{treePath}/` 迁移到 `tile/{z}/{x}/{y}/`：

| 原路径 | 新路径 | 说明 |
|--------|--------|------|
| tile_0/ | tile/0/0/0/ | 根节点 |
| tile_0_3/ | tile/1/3/0/ | 深度1，节点3 |
| tile_0_1_5/ | tile/2/13/0/ | 深度2，x=1*8+5=13 |

---

## 4. 四阶段渐进式迁移（真正的逐步替换）

```
阶段0: 原始实现（基准线）
   ↓
阶段1: 数据层替换
   - 新增: FBXSpatialItemAdapter
   - 替换: FBX数据访问从InstanceRef改为SpatialItem
   - 移除: 无（第一个阶段）
   - 适配器: 无（直接替换）

   ↓ [验证通过后才能进入下一阶段]

阶段2: 空间索引替换 + LOD集成
   - 新增: OctreeStrategy + LODPipeline集成
   - 替换: buildOctree() → buildSpatialIndex()
   - 替换: 内嵌generateLODChain → build_lod_levels()
   - 移除: 旧OctreeNode结构、旧buildOctree实现
   - 适配器: FBXOctreeAdapter（新OctreeNode → 老processNode接口）

   ↓ [验证通过后才能进入下一阶段]

阶段3: B3DM生成替换（简化版）
   - 使用: 已有B3DMGenerator + GLTFBuilder
   - 替换: createB3DM() → B3DMGenerator::generateLODFiles()
   - 移除: 旧createB3DM实现、appendGeometryToModel
   - 无需自定义适配器，直接使用已有接口

   ↓ [验证通过后才能进入下一阶段]

阶段4: Tileset生成替换
   - 新增: TilesetBuilder
   - 替换: writeTilesetJson() → TilesetBuilder
   - 移除: 旧writeTilesetJson实现、processNode中的旧逻辑
   - 适配器: FBXTilesetAdapter（老接口 → 新TilesetBuilder）

   ↓ [验证通过后才能进入下一阶段]

阶段5: 清理适配器（可选）
   - 移除: 所有适配器（如果不再需要兼容老接口）
   - 结果: 纯新实现
```

### 4.1 阶段执行与替换管理

| 阶段 | 新增组件 | 替换动作 | 移除组件 | 适配器 | 验证方式 |
|------|----------|----------|----------|--------|----------|
| **阶段1** | FBXSpatialItemAdapter | 数据访问方式 | 无 | 无 | 运行命令，Cesium查看 |
| **阶段2** | OctreeStrategy + LODPipeline | buildOctree() → buildSpatialIndex()<br>generateLODChain → build_lod_levels() | 旧buildOctree实现 | FBXOctreeAdapter | 运行命令，Cesium查看 |
| **阶段3** | B3DMGenerator<br>GLTFBuilder | createB3DM() → generateLODFiles() | 旧createB3DM、appendGeometryToModel | 无需适配器 | 运行命令，Cesium查看 |
| **阶段4** | TilesetBuilder | writeTilesetJson() → 新实现 | 旧writeTilesetJson、processNode旧逻辑 | FBXTilesetAdapter | 运行命令，Cesium查看 |
| **阶段5** | 无 | 清理适配器 | 所有适配器（可选） | 无 | 运行命令，Cesium查看 |

### 4.2 每个阶段的开始步骤

#### 阶段1开始前
- **无需移除任何代码**（这是第一个阶段）
- **新增**: `FBXSpatialItemAdapter` 实现
- **修改**: `FBXPipeline` 添加 `spatialItems_` 成员
- **替换**: 修改数据访问代码，从`InstanceRef`改为`SpatialItem`
- **验证**: 确保输出与阶段0一致

**状态**: ⏳ 待实现

#### 阶段2开始前
- **确认阶段1验证通过**
- **移除**: 旧`buildOctree()`方法实现
- **移除**: 内嵌`generateLODChain` Lambda，改用`build_lod_levels()`
- **移除**: 旧的手动八叉树构建代码（填充`rootNode->content`的代码）
- **新增**: `OctreeStrategy` + `FBXOctreeAdapter`
- **修改**: `run()` 方法调用 `buildSpatialIndex()` + `FBXOctreeAdapter::convertToLegacyOctree()`
- **修改**: LOD配置使用`LODPipelineSettings`和`build_lod_levels()`
- **验证**: 确保输出与阶段1一致

**状态**: ⏳ 待实现
- 计划移除约230行旧代码
- `rootNode` 类型将从 `OctreeNode*` 改为 `fbx::LegacyOctreeNode*`
- `processNode` 签名需更新
- LOD配置复用已有`lod_pipeline.h`

#### 阶段3开始前
- **确认阶段2验证通过**
- **移除**: 旧`createB3DM()`方法实现
- **移除**: `appendGeometryToModel()`函数
- **使用**: 已有`B3DMGenerator` + `GLTFBuilder`
- **修改**: `processNode`中调用`B3DMGenerator::generateLODFiles()`
- **验证**: 确保输出与阶段2一致

**状态**: ⏳ 待实现
- 无需创建自定义适配器
- 直接使用`b3dm::B3DMGenerator`的已有接口

#### 阶段4开始前
- **确认阶段3验证通过**
- **移除**: 旧`writeTilesetJson()`方法实现
- **移除**: `processNode`中的旧逻辑（或完全移除`processNode`）
- **新增**: `TilesetBuilder` + `FBXTilesetAdapter`
- **修改**: `run()` 方法调用 `tilesetAdapter_->buildAndWriteTileset()`
- **验证**: 确保输出与阶段3一致

**状态**: ⏳ 待实现

#### 阶段5（最终清理，可选）
- **确认阶段4验证通过并稳定运行**
- **移除**: 适配器代码（`FBXOctreeAdapter`、`FBXTilesetAdapter`）
- **移除**: 所有遗留的旧方法声明
- **清理**: 代码结构，优化性能
- **验证**: 最终纯新实现输出与基准一致

**注意**: 阶段3无需`FBXB3DMAdapter`，直接使用`B3DMGenerator`

---

## 5. 已有架构复用指南

### 5.0 可复用架构清单

#### 5.0.1 LOD Pipeline (`lod_pipeline.h`)
```cpp
// 已有功能
struct LODPipelineSettings {
    bool enable_lod = false;
    std::vector<LODLevelSettings> levels;
};

std::vector<LODLevelSettings> build_lod_levels(
    const std::vector<float>& ratios,
    float base_error,
    const SimplificationParams& simplify_template,
    const DracoCompressionParams& draco_template,
    bool draco_for_lod0 = false
);
```

**替换FBXPipeline中的**: 内嵌`generateLODChain` Lambda (65-84行)

#### 5.0.2 GLTFBuilder (`gltf/gltf_builder.h`)
```cpp
// 已有功能
class GLTFBuilder {
public:
    GLTFBuildResult build(const std::vector<InstanceRef>& instances);
    GLTFBuildResult buildWithMaterialGrouping(
        const std::vector<InstanceRef>& instances,
        const std::vector<osg::Geometry*>& geometries
    );
};
```

**替换FBXPipeline中的**: `appendGeometryToModel` 函数

#### 5.0.3 B3DMGenerator (`b3dm/b3dm_generator.h`)
```cpp
// 已有功能
class B3DMGenerator {
public:
    // 生成多LOD级别的B3DM文件
    std::vector<LODFileInfo> generateLODFiles(
        const spatial::core::SpatialItemRefList& items,
        const std::string& outputDir,
        const std::string& baseFilename,
        const std::vector<LODLevelSettings>& lodLevels
    );
};
```

**替换FBXPipeline中的**: `createB3DM` 方法

#### 5.0.4 工具类 (`gltf/utils/`)
```cpp
// 已完成
GeometryUtils::extractGeometryData()
GeometryUtils::processPrimitiveSet()
MaterialUtils::extractPBRParams()
TextureUtils::processTexture()
```

**被GLTFBuilder内部使用**

---

## 6. 详细实施步骤（基于已有架构）

### 阶段0: 原始实现（基准线）

**目标**: 建立可验证的基准

**当前状态**: `FBXPipeline.cpp` 原始实现

**验证命令**:
```bash
./_3dtile -f fbx -i data/test.fbx -o output_baseline/ --lon 116.0 --lat 40.0
# 在Cesium中查看 output_baseline/tileset.json
```

---

### 阶段1: 数据层替换 ✅ 已完成

**目标**: 创建FBXSpatialItemAdapter，将FBX数据访问从`InstanceRef`改为`SpatialItem`

**状态**: ✅ 已完成

#### 6.1.1 已创建文件

**文件**: `src/fbx/fbx_spatial_item_adapter.h` 和 `.cpp`

- `FBXSpatialItemAdapter` 类继承自 `spatial::core::SpatialItem`
- 实现了 `getBounds()`, `getId()`, `getCenter()` 接口
- 提供FBX特有接口：`getMeshInfo()`, `getTransformIndex()`, `getTransform()`, `getNodeName()`, `getGeometry()`
- `createSpatialItems()` 辅助函数从FBXLoader创建所有适配器

#### 6.1.2 已修改文件

**文件**: `src/FBXPipeline.h`
- 添加 `#include "fbx/fbx_spatial_item_adapter.h"`
- 添加 `fbx::FBXSpatialItemList spatialItems_` 成员

**文件**: `src/FBXPipeline.cpp`
- 在 `run()` 方法中添加空间对象适配器创建逻辑
- 日志标记更新为 "Stage 1"

#### 6.1.3 编译验证

✅ 代码编译成功，无错误。

```bash
cargo build
# Finished `dev` profile [unoptimized + debuginfo] target(s) in 0.10s
```

---

### 阶段2: 空间索引替换 + LOD集成

**目标**: 用`OctreeStrategy`替换旧的`buildOctree`，集成`LODPipeline`

**状态**: ⏳ 待实现

#### 6.2.1 复用LODPipeline

替换FBXPipeline.cpp 65-84行的内嵌Lambda：

```cpp
// 旧代码（移除）
auto generateLODChain = [&](const PipelineSettings& cfg) -> LODPipelineSettings {
    // ... 内嵌实现
};

// 新代码（使用已有架构）
LODPipelineSettings lodSettings;
if (settings.enableLOD) {
    SimplificationParams simTemplate;
    simTemplate.target_error = 0.0001f;

    DracoCompressionParams dracoTemplate;
    dracoTemplate.enable_compression = settings.enableDraco;

    lodSettings.enable_lod = true;
    lodSettings.levels = build_lod_levels(
        settings.lodRatios,
        simTemplate.target_error,
        simTemplate,
        dracoTemplate,
        false  // draco_for_lod0
    );
    LOG_I("Generated %zu LOD levels", lodSettings.levels.size());
}
```

#### 6.2.2 复用OctreeStrategy

```cpp
// 创建八叉树策略
spatial::strategy::OctreeStrategy octreeStrategy;

// 添加所有空间对象
for (const auto& item : spatialItems_) {
    octreeStrategy.addItem(item);
}

// 构建八叉树
OctreeConfig config;
config.maxDepth = settings.maxDepth;
config.maxItemsPerNode = settings.maxItemsPerTile;
octreeStrategy.build(config);
```

#### 6.2.3 创建FBXOctreeAdapter

**文件**: `src/fbx/fbx_octree_adapter.h`

适配器将新的`OctreeStrategy`节点转换为老的`OctreeNode`格式，保持`processNode`兼容。

#### 6.2.4 修改FBXPipeline

```cpp
void FBXPipeline::run() {
    LOG_I("Starting FBXPipeline (Stage 2)...");

    // 1. 加载FBX
    loader = new FBXLoader(settings.inputPath);
    loader->load();

    // 2. 创建空间对象适配器（阶段1）
    spatialItems_ = fbx::createSpatialItems(loader);

    // 3. 构建LOD配置（使用LODPipeline）
    LODPipelineSettings lodSettings;
    if (settings.enableLOD) {
        // ... 使用build_lod_levels()
    }

    // 4. 构建新空间索引（使用OctreeStrategy）
    LOG_I("Building OctreeStrategy...");
    spatial::strategy::OctreeStrategy octreeStrategy;
    for (const auto& item : spatialItems_) {
        octreeStrategy.addItem(item);
    }
    octreeStrategy.build({settings.maxDepth, settings.maxItemsPerTile});

    // 5. 转换为传统格式（适配器）
    rootNode = fbx::FBXOctreeAdapter::convertToLegacyOctree(octreeStrategy, spatialItems_);

    // 6. 处理节点
    tileset::Tile rootTile = processNode(rootNode, settings.outputPath, "0");

    // ...
}
```

#### 6.2.5 阶段2验证

**验证内容**:
1. `build_lod_levels()`生成的LOD配置正确
2. 新八叉树构建正确
3. 输出与阶段1完全一致
4. Cesium中正常显示

---

### 阶段3: B3DM生成替换（简化版）

**目标**: 用已有的`B3DMGenerator`和`GLTFBuilder`替换旧的`createB3DM`

**状态**: ⏳ 待实现

#### 6.3.1 已有架构复用

**已有组件**:
- `GLTFBuilder` (`gltf/gltf_builder.h`) - 替代`appendGeometryToModel`
- `B3DMGenerator` (`b3dm/b3dm_generator.h`) - 生成B3DM文件
- `Geometry/Material/TextureUtils` - 已完成工具类

**架构关系**:
```
FBXPipeline::processNode()
    │
    ▼
B3DMGenerator::generateLODFiles()
    │
    ├──► GLTFBuilder::build() ──► GeometryUtils/MaterialUtils/TextureUtils
    │
    └──► B3DMWriter::write()
```

#### 5.3.2 appendGeometryToModel 功能拆解（✅ 已完成）

原 `appendGeometryToModel` 函数（约 600+ 行）包含以下重复逻辑：

```cpp
// 原函数结构（简化）
void appendGeometryToModel(model, instances, ...) {
    // 1. 几何体提取和变换（重复代码：坐标转换、法线转换）
    for (每个实例) {
        // 顶点变换（Y-up → Z-up）
        // 法线变换（逆转置矩阵）
        // 纹理坐标提取
    }

    // 2. 索引处理（重复代码：TRIANGLES/TRIANGLE_STRIP/TRIANGLE_FAN）
    for (每个PrimitiveSet) {
        // 处理不同图元类型
    }

    // 3. 材质处理（重复代码：PBR参数提取）
    extractMaterialParams(stateSet, ...);  // 被重复调用

    // 4. 纹理处理（严重重复：基础纹理、法线纹理、自发光纹理各100+行相似代码）
    processBaseColorTexture(...);      // ~120行
    processNormalTexture(...);         // ~120行（几乎相同）
    processEmissiveTexture(...);       // ~120行（几乎相同）
}
```

**✅ 已完成的抽取结构**:

```cpp
// src/gltf/utils/geometry_utils.h - 几何体处理工具类
class GeometryUtils {
public:
    // 计算法线变换矩阵（逆转置）
    static osg::Matrixd computeNormalMatrix(const osg::Matrixd& matrix);

    // 变换顶点（Y-up到Z-up）
    static osg::Vec3d transformVertex(const osg::Vec3d& vertex, const osg::Matrixd& matrix);

    // 变换法线（Y-up到Z-up）
    static osg::Vec3d transformNormal(const osg::Vec3d& normal, const osg::Matrixd& normalMatrix);

    // 提取变换后的几何体数据
    static size_t extractGeometryData(
        const osg::Geometry* geom,
        const osg::Matrixd& matrix,
        const osg::Matrixd& normalMatrix,
        std::vector<float>& outPositions,
        std::vector<float>& outNormals,
        std::vector<float>& outTexcoords,
        size_t baseIndex = 0
    );

    // 处理索引（支持多种图元类型）
    static size_t processPrimitiveSet(
        const osg::PrimitiveSet* ps,
        uint32_t baseIndex,
        std::vector<uint32_t>& outIndices
    );

    // 处理DrawArrays
    static size_t processDrawArrays(
        const osg::DrawArrays* da,
        uint32_t baseIndex,
        std::vector<uint32_t>& outIndices
    );

    // 处理DrawElementsUShort
    static size_t processDrawElementsUShort(
        const osg::DrawElementsUShort* de,
        uint32_t baseIndex,
        std::vector<uint32_t>& outIndices
    );

    // 处理DrawElementsUInt
    static size_t processDrawElementsUInt(
        const osg::DrawElementsUInt* de,
        uint32_t baseIndex,
        std::vector<uint32_t>& outIndices
    );
};

// src/gltf/utils/material_utils.h - 材质处理工具类
struct PBRParams {
    std::vector<double> baseColor = {1.0, 1.0, 1.0, 1.0};
    double emissiveColor[3] = {0.0, 0.0, 0.0};
    float roughnessFactor = 1.0f;
    float metallicFactor = 0.0f;
    float aoStrength = 1.0f;
};

class MaterialUtils {
public:
    // 从StateSet提取PBR参数
    static void extractPBRParams(
        const osg::StateSet* stateSet,
        PBRParams& outParams
    );

    // 检查是否有材质
    static bool hasMaterial(const osg::StateSet* stateSet);

    // 获取各类纹理
    static const osg::Texture* getBaseColorTexture(const osg::StateSet* stateSet);
    static const osg::Texture* getNormalTexture(const osg::StateSet* stateSet);
    static const osg::Texture* getEmissiveTexture(const osg::StateSet* stateSet);
};

// src/gltf/utils/texture_utils.h - 纹理处理工具类
struct TextureResult {
    std::vector<unsigned char> data;
    std::string mimeType;
    bool hasAlpha = false;
    bool success = false;
};

class TextureUtils {
public:
    // 统一的纹理处理入口，替代3处重复代码
    static TextureResult processTexture(
        const osg::Texture* texture,
        bool enableKTX2 = false
    );

    // 将图像数据添加到GLTF模型
    static int addImageToModel(
        tinygltf::Model& model,
        tinygltf::Buffer& buffer,
        const std::vector<unsigned char>& imageData,
        const std::string& mimeType,
        bool useBasisu
    );
};

// gltf/gltf_builder.h（替代appendGeometryToModel）
class GLTFBuilder {
public:
    struct Config {
        bool enableDraco = false;
        DracoCompressionParams dracoParams;
        bool enableKTX2 = false;
        bool enableUnlit = false;
    };

    explicit GLTFBuilder(const Config& config);

    // 主构建函数（替代appendGeometryToModel）
    bool build(
        const std::vector<InstanceRef>& instances,
        std::vector<unsigned char>& outGlbData,
        osg::BoundingBoxd* outBounds = nullptr
    );

private:
    Config config_;
    gltf_writer::ExtensionManager extMgr_;
};
```

#### 5.3.3 gltf_writer 模块完善（可选优化）

`gltf_writer` 模块可以基于已完成的工具类进一步封装：

```cpp
// gltf_writer/primitive_builder.h（可选封装）
// 基于 GeometryUtils 封装 Primitive 构建
class PrimitiveBuilder {
public:
    void addGeometryData(
        const osg::Geometry* geom,
        const osg::Matrixd& matrix
    );
    void setMaterial(int materialIndex);
    tinygltf::Primitive build(tinygltf::Model& model, tinygltf::Buffer& buffer);

private:
    std::vector<float> positions_;
    std::vector<float> normals_;
    std::vector<float> texcoords_;
    std::vector<uint32_t> indices_;
    int materialIndex_ = -1;
};

// gltf_writer/material_builder.h（可选封装）
// 基于 MaterialUtils 封装 Material 构建
class MaterialBuilder {
public:
    void fromOSGStateSet(const osg::StateSet* stateSet);
    void setUnlit(bool unlit);
    void setDoubleSided(bool doubleSided);
    int build(tinygltf::Model& model);

private:
    PBRParams pbrParams_;
    int baseColorTexture_ = -1;
    int normalTexture_ = -1;
    bool unlit_ = false;
    bool doubleSided_ = true;
};

// gltf_writer/texture_builder.h（可选封装）
// 基于 TextureUtils 封装 Texture 构建
class TextureBuilder {
public:
    // 从OSG纹理创建GLTF纹理
    int createFromOSGTexture(
        tinygltf::Model& model,
        tinygltf::Buffer& buffer,
        const osg::Texture* texture,
        gltf_writer::ExtensionManager& extMgr,
        bool enableKTX2
    );
};
```

**说明**: 这些封装是可选的，因为 `GeometryUtils/MaterialUtils/TextureUtils` 已经提供了底层功能。
可以直接在 `GLTFBuilder` 中使用工具类，无需额外的封装层。

#### 5.3.4 代码复用架构

```
┌─────────────────────────────────────────────────────────────────┐
│                      上层调用者                                  │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐  │
│  │  FBXPipeline │  │B3DMGenerator │  │  ShapefileProcessor  │  │
│  │ (老代码)      │  │ (新代码)      │  │    (参考实现)         │  │
│  └──────┬───────┘  └──────┬───────┘  └──────────┬───────────┘  │
│         │                 │                      │              │
│         └─────────────────┼──────────────────────┘              │
│                           │ 复用                                │
└───────────────────────────┼─────────────────────────────────────┘
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│                    GLTF 构建层（公共）                           │
│  ┌───────────────────────────────────────────────────────────┐ │
│  │                  GLTFBuilder                              │ │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐   │ │
│  │  │GeometryUtils│  │MaterialUtils│  │  TextureUtils   │   │ │
│  │  └─────────────┘  └─────────────┘  └─────────────────┘   │ │
│  └───────────────────────────────────────────────────────────┘ │
│  ┌───────────────────────────────────────────────────────────┐ │
│  │              gltf_writer 模块                              │ │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐   │ │
│  │  │ExtensionMgr │  │   Builders  │  │   Extensions    │   │ │
│  │  └─────────────┘  └─────────────┘  └─────────────────┘   │ │
│  └───────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│                    OSG/外部工具                                  │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐  │
│  │  OSG Geometry│  │process_texture│  │    tinygltf         │  │
│  └──────────────┘  └──────────────┘  └──────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

**复用策略**:
1. **GeometryUtils**: 处理所有 OSG 几何体到原始数据的转换
   - 被 `GLTFBuilder` 使用
   - 未来可被其他格式转换器复用

2. **MaterialUtils**: 提取 PBR 材质参数
   - 统一的材质参数提取逻辑
   - 支持从 OSG StateSet 读取标准参数

3. **TextureUtils**: 纹理加载和压缩
   - 直接调用 `process_texture` 进行 KTX2 压缩
   - 统一的纹理处理流程（文件加载 → 内存编码 → KTX2压缩）

4. **GLTFBuilder**: 高层构建器
   - 替代 `appendGeometryToModel`
   - 被 `B3DMGenerator` 调用生成 glb
   - 可被任何需要生成 glTF 的模块复用

#### 5.3.5 当前状态（阶段2完成后）

```cpp
std::pair<std::string, osg::BoundingBoxd> createB3DM(
    const std::vector<InstanceRef>& instances, ...) {
    // 旧实现：直接操作tinygltf::Model
    tinygltf::Model model;
    appendGeometryToModel(model, instances, ...);  // 需要移除！
    // ...生成B3DM...
}
```

#### 5.3.6 阶段3实现步骤（更新版）

**步骤1: ✅ 创建公共工具类（gltf/utils/）- 已完成**

```cpp
// src/gltf/utils/geometry_utils.h & .cpp ✅
// - 从 appendGeometryToModel 提取几何体处理逻辑
// - 包括：顶点变换、法线变换、索引处理
// - 支持泛型数组处理（Vec3/Vec4/Vec3d/Vec4d/double/float）

// src/gltf/utils/material_utils.h & .cpp ✅
// - 从 appendGeometryToModel 提取材质参数提取逻辑
// - PBRParams 结构体封装
// - 纹理获取辅助函数

// src/gltf/utils/texture_utils.h & .cpp ✅
// - 从 appendGeometryToModel 提取纹理处理逻辑
// - TextureResult 结构体封装
// - 直接调用 ::process_texture() 进行 KTX2 压缩
```

**步骤2: 创建 GLTFBuilder（替代 appendGeometryToModel）**

```cpp
// src/gltf/gltf_builder.h & .cpp（新增）
#pragma once

#include "utils/geometry_utils.h"
#include "utils/material_utils.h"
#include "utils/texture_utils.h"
#include <tiny_gltf.h>

namespace gltf {

struct GLTFBuilderConfig {
    bool enableDraco = false;
    bool enableKTX2 = false;
    bool enableUnlit = false;
    bool doubleSided = true;
    int dracoPositionQuantization = 14;
    int dracoNormalQuantization = 10;
    int dracoTexcoordQuantization = 12;
};

class GLTFBuilder {
public:
    explicit GLTFBuilder(const GLTFBuilderConfig& config);

    // 主构建函数（替代 appendGeometryToModel）
    bool build(
        const std::vector<InstanceRef>& instances,
        std::vector<unsigned char>& outGlbData,
        osg::BoundingBoxd* outBounds = nullptr
    );

private:
    GLTFBuilderConfig config_;

    // 构建单个 Mesh
    void buildMesh(
        tinygltf::Model& model,
        tinygltf::Buffer& buffer,
        const osg::Geometry* geom,
        const osg::Matrixd& matrix,
        int materialIndex
    );

    // 构建材质
    int buildMaterial(
        tinygltf::Model& model,
        tinygltf::Buffer& buffer,
        const osg::StateSet* stateSet
    );

    // 构建纹理
    int buildTexture(
        tinygltf::Model& model,
        tinygltf::Buffer& buffer,
        const osg::Texture* texture
    );

    // 添加 BufferView 和 Accessor
    int addBufferViewAndAccessor(
        tinygltf::Model& model,
        tinygltf::Buffer& buffer,
        const std::vector<float>& data,
        int componentType,
        const std::string& type,
        size_t count
    );
};

} // namespace gltf
```

**步骤3: 修改 B3DMGenerator 使用 GLTFBuilder**

```cpp
// src/b3dm/b3dm_generator.cpp
#include "../gltf/gltf_builder.h"

void B3DMGenerator::buildGLTFModel(...) {
    // 使用 GLTFBuilder 替代直接调用 appendGeometryToModel
    gltf::GLTFBuilderConfig config;
    config.enableDraco = settings.enableDraco;
    config.enableKTX2 = settings.enableKTX2;
    config.enableUnlit = settings.enableUnlit;

    gltf::GLTFBuilder gltfBuilder(config);
    std::vector<unsigned char> glbData;
    osg::BoundingBoxd bounds;

    if (gltfBuilder.build(instances, glbData, &bounds)) {
        // 将 glb 包装为 B3DM
        // ...
    }
}
```

**步骤4: 移除旧代码**
- ✅ `appendGeometryToModel` 的功能已拆解到工具类
- 删除 `appendGeometryToModel` 函数声明和实现
- 删除 `createB3DM` 旧实现

#### 5.3.7 创建FBXGeometryExtractor

**文件**: `src/fbx/fbx_geometry_extractor.h`

```cpp
#pragma once

#include "../common/geometry_extractor.h"
#include "fbx_spatial_item_adapter.h"

namespace fbx {

/**
 * @brief FBX几何体提取器
 *
 * 实现IGeometryExtractor接口，从FBXSpatialItemAdapter提取几何体
 */
class FBXGeometryExtractor : public common::IGeometryExtractor {
public:
    std::vector<osg::ref_ptr<osg::Geometry>> extract(
        const spatial::core::SpatialItem* item) override;

    std::string getId(const spatial::core::SpatialItem* item) override;

    std::map<std::string, nlohmann::json> getAttributes(
        const spatial::core::SpatialItem* item) override;
};

} // namespace fbx
```

#### 5.3.3 创建FBXB3DMContentGenerator

**文件**: `src/fbx/fbx_b3dm_content_generator.h`

```cpp
#pragma once

#include "../b3dm/b3dm_generator.h"
#include "fbx_geometry_extractor.h"
#include "../FBXPipeline.h"
#include <memory>

namespace fbx {

/**
 * @brief FBX B3DM内容生成器
 *
 * 封装b3dm::B3DMGenerator，提供FBX特定的B3DM生成功能
 */
class FBXB3DMContentGenerator {
public:
    struct Config {
        double centerLongitude = 0.0;
        double centerLatitude = 0.0;
        double centerHeight = 0.0;
        bool enableDraco = false;
        bool enableSimplification = false;
        SimplificationParams simplifyParams;
    };

    explicit FBXB3DMContentGenerator(const Config& config);

    /**
     * @brief 生成B3DM内容
     * @param instances 实例引用列表
     * @param tilePath 瓦片路径
     * @param tileName 瓦片名称
     * @param simParams 简化参数
     * @return B3DM文件名和包围盒
     */
    std::pair<std::string, osg::BoundingBoxd> generate(
        const std::vector<FBXPipeline::InstanceRef>& instances,
        const std::string& tilePath,
        const std::string& tileName,
        const SimplificationParams& simParams);

private:
    Config config_;
    b3dm::B3DMGeneratorConfig generatorConfig_;
};

} // namespace fbx
```

#### 5.3.4 创建FBXB3DMAdapter

**文件**: `src/fbx/fbx_b3dm_adapter.h`

```cpp
#pragma once

#include "../FBXPipeline.h"
#include "fbx_b3dm_content_generator.h"
#include <string>
#include <vector>

namespace fbx {

/**
 * @brief B3DM生成适配器
 *
 * 阶段3适配器：在老流程中调用新的B3DMGenerator
 * 保持createB3DM接口不变，内部使用新实现
 */
class FBXB3DMAdapter {
public:
    struct Config {
        double centerLongitude = 0.0;
        double centerLatitude = 0.0;
        double centerHeight = 0.0;
        bool enableDraco = false;
        bool enableSimplification = false;
        SimplificationParams simplifyParams;
    };

    explicit FBXB3DMAdapter(const Config& config);

    /**
     * @brief 生成B3DM（兼容老接口）
     *
     * 保持与FBXPipeline::createB3DM相同的接口，
     * 内部使用新的B3DMGenerator实现
     *
     * @param instances 实例引用列表（老格式）
     * @param tilePath 瓦片路径
     * @param tileName 瓦片名称
     * @param simParams 简化参数
     * @return B3DM文件名和包围盒
     */
    std::pair<std::string, osg::BoundingBoxd> createB3DM(
        const std::vector<FBXPipeline::InstanceRef>& instances,
        const std::string& tilePath,
        const std::string& tileName,
        const SimplificationParams& simParams = SimplificationParams());

private:
    Config config_;
    std::unique_ptr<FBXB3DMContentGenerator> generator_;
};

} // namespace fbx
```

#### 5.3.5 修改FBXPipeline真正替换createB3DM

**文件**: `src/FBXPipeline.h`

```cpp
// 阶段3：添加B3DM生成适配器
std::unique_ptr<fbx::FBXB3DMAdapter> b3dmAdapter_;
```

**文件**: `src/FBXPipeline.cpp`

```cpp
void FBXPipeline::run() {
    // ...加载FBX、构建空间索引...

    // 阶段3：初始化B3DM生成适配器
    fbx::FBXB3DMAdapter::Config b3dmConfig;
    b3dmConfig.centerLongitude = settings.longitude;
    b3dmConfig.centerLatitude = settings.latitude;
    b3dmConfig.centerHeight = settings.height;
    b3dmConfig.enableDraco = settings.enableDraco;
    b3dmConfig.enableSimplification = settings.enableSimplify;
    b3dmAdapter_ = std::make_unique<fbx::FBXB3DMAdapter>(b3dmConfig);

    // 使用老流程处理，但B3DM使用新生成器
    LOG_I("Processing Nodes with New B3DM Generator...");
    tileset::Tile rootTile = processNode(rootNode, settings.outputPath, "0");

    // ...写入Tileset...
}

// 修改createB3DM调用，使用适配器（旧实现被完全替换）
std::pair<std::string, osg::BoundingBoxd> FBXPipeline::createB3DM(
    const std::vector<InstanceRef>& instances,
    const std::string& tilePath,
    const std::string& tileName,
    const SimplificationParams& simParams) {

    // 阶段3：使用新的B3DM生成适配器（旧实现已移除）
    return b3dmAdapter_->createB3DM(instances, tilePath, tileName, simParams);
}
```

**关键**: 旧的`createB3DM`实现和`appendGeometryToModel`被完全移除，取而代之的是`FBXB3DMAdapter`。

#### 5.3.8 工具类使用示例

以下是使用重构后的工具类的完整示例：

```cpp
// 示例：使用 GeometryUtils 提取几何体数据
#include "gltf/utils/geometry_utils.h"

void processGeometry(const osg::Geometry* geom, const osg::Matrixd& matrix) {
    using namespace gltf::utils;

    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> texcoords;

    // 计算法线变换矩阵
    osg::Matrixd normalMatrix = GeometryUtils::computeNormalMatrix(matrix);

    // 提取几何体数据
    size_t vertexCount = GeometryUtils::extractGeometryData(
        geom, matrix, normalMatrix,
        positions, normals, texcoords
    );

    // 提取索引
    std::vector<uint32_t> indices;
    for (unsigned int i = 0; i < geom->getNumPrimitiveSets(); ++i) {
        GeometryUtils::processPrimitiveSet(
            geom->getPrimitiveSet(i),
            0,  // baseIndex
            indices
        );
    }
}
```

```cpp
// 示例：使用 MaterialUtils 提取材质参数
#include "gltf/utils/material_utils.h"

void processMaterial(const osg::StateSet* stateSet) {
    using namespace gltf::utils;

    // 检查是否有材质
    if (!MaterialUtils::hasMaterial(stateSet)) {
        return;
    }

    // 提取 PBR 参数
    PBRParams params;
    MaterialUtils::extractPBRParams(stateSet, params);

    // 获取纹理
    const osg::Texture* baseColorTex = MaterialUtils::getBaseColorTexture(stateSet);
    const osg::Texture* normalTex = MaterialUtils::getNormalTexture(stateSet);
    const osg::Texture* emissiveTex = MaterialUtils::getEmissiveTexture(stateSet);
}
```

```cpp
// 示例：使用 TextureUtils 处理纹理
#include "gltf/utils/texture_utils.h"

void processTexture(const osg::Texture* texture) {
    using namespace gltf::utils;

    // 处理纹理（自动处理KTX2压缩）
    TextureResult result = TextureUtils::processTexture(texture, true);

    if (result.success) {
        // result.data - 图像数据
        // result.mimeType - MIME类型
        // result.hasAlpha - 是否包含透明通道
    }
}
```

#### 5.3.9 阶段3验证

**验证内容**:
1. B3DM文件大小与原始一致（±5%）
2. 几何体数量一致
3. 顶点数据一致
4. 在Cesium中正常显示

---

### 阶段4: Tileset构建替换

**目标**: 用`TilesetBuilder`替换旧的`writeTilesetJson`，旧实现被完全移除

#### 5.4.1 创建FBXTileMetaConverter

**文件**: `src/fbx/fbx_tile_meta_converter.h`

```cpp
#pragma once

#include "../common/tile_meta.h"
#include "fbx_spatial_item_adapter.h"
#include "../spatial/strategy/octree_strategy.h"
#include <memory>
#include <unordered_map>

namespace fbx {

/**
 * @brief FBX瓦片元数据转换器
 *
 * 将OctreeStrategy节点转换为TileMeta结构
 */
class FBXTileMetaConverter {
public:
    /**
     * @brief 转换八叉树为TileMeta映射表
     * @param strategy 八叉树策略
     * @return 根节点元数据 + 所有节点映射表
     */
    static std::pair<common::TileMetaPtr, common::TileMetaMap> convert(
        const spatial::strategy::OctreeStrategy& strategy);

private:
    static common::TileMetaPtr convertNodeRecursive(
        const spatial::strategy::OctreeNode* node,
        common::TileMetaMap& allMetas,
        int& nodeIdCounter);
};

} // namespace fbx
```

#### 5.4.2 创建FBXTilesetAdapter

**文件**: `src/fbx/fbx_tileset_adapter.h`

```cpp
#pragma once

#include "fbx_tile_meta_converter.h"
#include "../common/tileset_builder.h"
#include "../tileset/tileset_types.h"

namespace fbx {

/**
 * @brief FBX Tileset适配器
 *
 * 整合TilesetBuilder生成最终的tileset.json
 */
class FBXTilesetAdapter {
public:
    struct Config {
        double centerLongitude = 0.0;
        double centerLatitude = 0.0;
        double centerHeight = 0.0;
        double boundingVolumeScale = 1.0;
        double geometricErrorScale = 0.5;
        bool enableLOD = false;
    };

    explicit FBXTilesetAdapter(const Config& config);

    /**
     * @brief 构建Tileset
     * @param strategy 八叉树策略
     * @param outputPath 输出路径
     * @return 是否成功
     */
    bool buildAndWriteTileset(
        const spatial::strategy::OctreeStrategy& strategy,
        const std::string& outputPath);

private:
    Config config_;
};

} // namespace fbx
```

#### 5.4.3 修改FBXPipeline真正替换writeTilesetJson

**文件**: `src/FBXPipeline.h`

```cpp
// 阶段4：添加Tileset适配器
std::unique_ptr<fbx::FBXTilesetAdapter> tilesetAdapter_;
```

**文件**: `src/FBXPipeline.cpp`

```cpp
void FBXPipeline::run() {
    LOG_I("Starting FBXPipeline (Stage 4)...");

    // 1. 加载FBX
    loader = new FBXLoader(settings.inputPath);
    loader->load();

    // 2. 构建空间索引
    buildSpatialIndex();

    // 3. 初始化B3DM生成适配器（阶段3）
    // ...

    // 4. 初始化Tileset适配器（阶段4新增）
    fbx::FBXTilesetAdapter::Config tilesetConfig;
    tilesetConfig.centerLongitude = settings.longitude;
    tilesetConfig.centerLatitude = settings.latitude;
    tilesetConfig.centerHeight = settings.height;
    tilesetConfig.geometricErrorScale = settings.geScale;
    tilesetConfig.enableLOD = settings.enableLOD;
    tilesetAdapter_ = std::make_unique<fbx::FBXTilesetAdapter>(tilesetConfig);

    // 5. 使用TilesetBuilder统一处理（替换processNode + writeTilesetJson）
    LOG_I("Building Tileset with TilesetBuilder...");
    tilesetAdapter_->buildAndWriteTileset(*octreeStrategy_, settings.outputPath);

    LOG_I("FBXPipeline Finished.");
}
```

**关键**: 旧的`processNode`和`writeTilesetJson`被完全移除，取而代之的是`FBXTilesetAdapter`。

#### 5.4.4 阶段4验证

**验证内容**:
1. tileset.json结构正确
2. 所有B3DM文件被正确引用
3. 几何误差计算正确
4. 在Cesium中正常显示

---

## 6. 总结

**正确的迁移节奏**:

| 阶段 | 动作 | 结果 |
|------|------|------|
| 阶段1 | 替换数据访问方式 | 所有数据通过SpatialItem访问 |
| 阶段2 | 替换空间索引 | OctreeStrategy替代buildOctree |
| 阶段3 | 替换B3DM生成 | B3DMGenerator替代createB3DM |
| 阶段4 | 替换Tileset生成 | TilesetBuilder替代writeTilesetJson |

**每个阶段都是真正的替换，不是并行保留。**

**关键原则**:
1. 每个阶段完成后，旧实现被移除
2. 新实现通过适配器保持与老接口兼容
3. 验证通过后才能进入下一阶段
4. 每个阶段都能产生可运行的3D Tiles输出

# shp23dtile 迁移方案

## 1. 迁移概述

### 1.1 目标
将现有的shp23dtile.cpp中的四叉树实现迁移到新的空间切片抽象框架，实现：
1. 代码结构清晰化，业务逻辑与空间索引分离
2. 复用统一的四叉树策略实现
3. 保持现有功能不变（WGS84坐标、四叉树切片、3D Tiles生成）

### 1.2 核心原则

**每个阶段的成果都必须能产生可运行的3D Tiles数据，可在Cesium中查看验证。**

### 1.3 现状代码分析

#### 1.3.1 当前四叉树实现 (src/shp23dtile.cpp)
```cpp
// 当前内嵌的四叉树实现
struct bbox {
    bool isAdd = false;
    double minx, maxx, miny, maxy;
    bool contains(double x, double y);
    bool intersect(bbox& other);
};

class node {
public:
    bbox _box;
    double metric = 0.01;  // 最小划分粒度
    node* subnode[4];
    std::vector<int> geo_items;
    int _x = 0, _y = 0, _z = 0;

    void split();
    void add(int id, bbox& box);
    void get_all(std::vector<void*>& items_array);
};
```

#### 1.3.2 当前业务数据结构 (src/shapefile/shapefile_tile.h)
```cpp
namespace shapefile {
    struct TileBBox {
        double minx, maxx, miny, maxy;
        double minHeight, maxHeight;
    };

    struct QuadtreeCoord {
        int z, x, y;
        uint64_t encode();
        static QuadtreeCoord decode(uint64_t key);
    };

    struct TileMeta {
        int z, x, y;
        TileBBox bbox;
        double geometric_error;
        std::string tileset_rel;
        bool is_leaf;
        std::vector<uint64_t> children_keys;
    };
}
```

---

## 2. 四阶段渐进式迁移路线图

```
阶段0: 原始实现（基准线）
   ↓
阶段1: 数据层抽象（新数据加载 + 旧处理流程）→ 可运行，产生3D Tiles
   ↓
阶段2: 空间索引迁移（新数据加载 + 新四叉树 + 旧B3DM生成）→ 可运行，产生3D Tiles
   ↓
阶段3: Tileset生成迁移（新数据加载 + 新四叉树 + 新TilesetBuilder + 旧B3DM生成）→ 可运行，产生3D Tiles
   ↓
阶段4: 完整新框架（全部新实现）→ 可运行，产生3D Tiles
```

| 阶段 | 新组件 | 旧组件 | 验证方式 |
|------|--------|--------|----------|
| **阶段1** | 新数据加载 (DataPool) | 旧四叉树 + 旧B3DM生成 | 运行命令，Cesium查看 |
| **阶段2** | 新数据加载 + 新四叉树 | 旧B3DM生成 | 运行命令，Cesium查看 |
| **阶段3** | 新数据加载 + 新四叉树 + 新TilesetBuilder | 旧B3DM生成 | 运行命令，Cesium查看 |
| **阶段4** | 全部新实现 | 无 | 运行命令，Cesium查看 |

---

## 3. 阶段0: 原始实现（基准线）

**目标**: 建立可验证的基准

**当前状态**: `shp23dtile.cpp` 原始实现

**验证命令**:
```bash
./_3dtile -f shape -i data/SHP/bj_building/bj_building.shp -o output_baseline/ --height height
# 在Cesium中查看 output_baseline/tileset.json
```

---

## 4. 阶段1: 数据层抽象

**目标**: 替换数据加载层，保持后续处理流程不变

### 4.1 实现内容

创建新的数据加载模块，但保持原有的四叉树和B3DM生成逻辑：

```cpp
// src/shapefile/shapefile_data_pool.h
#pragma once

#include <memory>
#include <vector>
#include <map>
#include <string>
#include "shapefile_tile.h"
#include <osg/Geometry>
#include <nlohmann/json.hpp>

namespace shapefile {

// Shapefile数据项（禁止拷贝，只允许shared_ptr管理）
struct ShapefileSpatialItem {
    int featureId;
    TileBBox bounds;
    std::vector<osg::ref_ptr<osg::Geometry>> geometries;
    std::map<std::string, nlohmann::json> properties;

    // 禁止拷贝
    ShapefileSpatialItem() = default;
    ShapefileSpatialItem(const ShapefileSpatialItem&) = delete;
    ShapefileSpatialItem& operator=(const ShapefileSpatialItem&) = delete;
    ShapefileSpatialItem(ShapefileSpatialItem&&) = default;
    ShapefileSpatialItem& operator=(ShapefileSpatialItem&&) = default;
};

// 数据池管理器
class ShapefileDataPool {
public:
    using ItemPtr = std::shared_ptr<const ShapefileSpatialItem>;

    bool loadFromShapefile(const std::string& filename, const std::string& heightField);

    size_t size() const { return items_.size(); }
    const ItemPtr& getItem(size_t index) const { return items_[index]; }
    const std::vector<ItemPtr>& getAllItems() const { return items_; }

    // 获取包围盒
    TileBBox computeWorldBounds() const;

private:
    std::vector<ItemPtr> items_;
};

} // namespace shapefile
```

### 4.2 适配层

创建适配器，将新的数据池适配到旧的四叉树接口：

```cpp
// src/shapefile/legacy_adapter.h
#pragma once

#include "shapefile_data_pool.h"
#include "../shp23dtile.cpp"  // 包含原始数据结构

namespace shapefile {

// 将新数据格式转换为旧格式，供原有代码使用
class LegacyDataAdapter {
public:
    // 从DataPool转换为原有的Polygon_Mesh列表
    static std::vector<Polygon_Mesh> convertToLegacyMeshes(
        const ShapefileDataPool& pool
    );

    // 从DataPool构建原有的四叉树
    static node* buildLegacyQuadtree(
        const ShapefileDataPool& pool,
        double metricThreshold
    );
};

} // namespace shapefile
```

### 4.3 修改后的主流程

```cpp
// shp23dtile.cpp (阶段1)
extern "C" bool shp23dtile(const ShapeConversionParams* params) {
    if (!params || !params->input_path || !params->output_path) {
        LOG_E("invalid parameters");
        return false;
    }

    // ===== 阶段1: 使用新的数据加载 =====
    shapefile::ShapefileDataPool dataPool;
    if (!dataPool.loadFromShapefile(params->input_path, params->height_field)) {
        LOG_E("failed to load shapefile");
        return false;
    }

    LOG_I("Loaded %zu features using new data pool", dataPool.size());

    // ===== 保持原有的处理流程 =====
    // 使用适配器转换为旧格式
    auto meshes = shapefile::LegacyDataAdapter::convertToLegacyMeshes(dataPool);

    // 使用原有的四叉树构建
    auto* root = shapefile::LegacyDataAdapter::buildLegacyQuadtree(
        dataPool,
        0.01  // metric threshold
    );

    // 使用原有的B3DM生成逻辑
    // ... 保持原有代码不变 ...

    // 清理
    delete root;

    return true;
}
```

### 4.4 验证方式

**测试命令**:
```bash
# 阶段1实现
./_3dtile -f shape -i data/SHP/bj_building/bj_building.shp -o output_stage1/ --height height

# 对比阶段0和阶段1的输出
diff output_baseline/tileset.json output_stage1/tileset.json
# 应该完全一致或只有细微差异

# 在Cesium中查看
# 两个输出都应该能正常显示
```

**成功标准**:
- 生成的3D Tiles能在Cesium中正常显示
- 与阶段0的输出在视觉上无差异
- 内存使用不超过阶段0的110%

---

## 5. 阶段2: 空间索引迁移

**目标**: 使用新的四叉树策略，但保持B3DM生成逻辑不变

### 5.1 实现内容

使用 `QuadtreeStrategy` 构建空间索引，然后将结果转换为旧格式供B3DM生成使用：

```cpp
// src/shapefile/quadtree_adapter.h
#pragma once

#include "shapefile_data_pool.h"
#include "../spatial/strategy/quadtree_strategy.h"
#include "../shp23dtile.cpp"

namespace shapefile {

// 将新的四叉树结果转换为旧格式
class QuadtreeAdapter {
public:
    // 使用新四叉树构建索引，然后转换为旧的node结构
    static node* buildQuadtreeWithNewStrategy(
        const ShapefileDataPool& pool,
        const spatial::strategy::QuadtreeConfig& config
    );

    // 收集叶节点（转换为旧格式）
    static std::vector<TileMeta> collectLeavesAsLegacy(
        const spatial::strategy::QuadtreeIndex& index
    );
};

} // namespace shapefile
```

### 5.2 修改后的主流程

```cpp
// shp23dtile.cpp (阶段2)
extern "C" bool shp23dtile(const ShapeConversionParams* params) {
    // 1. 使用新的数据加载（阶段1已完成）
    shapefile::ShapefileDataPool dataPool;
    dataPool.loadFromShapefile(params->input_path, params->height_field);

    // 2. 使用新的四叉树策略
    spatial::strategy::QuadtreeConfig config;
    config.maxDepth = 10;
    config.maxItemsPerNode = 1000;
    config.metricThreshold = 0.01;

    // 构建空间索引
    auto worldBounds = dataPool.computeWorldBounds();
    spatial::core::SpatialBounds<double, 3> bounds3d(
        std::array<double, 3>{worldBounds.minx, worldBounds.miny, worldBounds.minHeight},
        std::array<double, 3>{worldBounds.maxx, worldBounds.maxy, worldBounds.maxHeight}
    );

    // 转换为SpatialItemList
    spatial::core::SpatialItemList spatialItems;
    for (const auto& item : dataPool.getAllItems()) {
        auto adapter = std::make_shared<ShapefileSpatialItemAdapter>(item);
        spatialItems.push_back(adapter);
    }

    spatial::strategy::QuadtreeStrategy strategy;
    auto index = strategy.buildIndex(spatialItems, bounds3d, config);

    LOG_I("Built quadtree with %zu nodes", index->getNodeCount());

    // 3. 转换为旧格式，使用原有的B3DM生成
    auto leaves = shapefile::QuadtreeAdapter::collectLeavesAsLegacy(*index);

    // 使用原有的build_hierarchical_tilesets逻辑
    build_hierarchical_tilesets(
        leaves,
        params->output_path,
        centerLon,
        centerLat
    );

    return true;
}
```

### 5.3 验证方式

**测试命令**:
```bash
# 阶段2实现
./_3dtile -f shape -i data/SHP/bj_building/bj_building.shp -o output_stage2/ --height height

# 对比阶段0和阶段2的输出
# 注意：四叉树结构可能略有不同，但视觉上应该一致

# 统计对比
echo "Baseline B3DM count:"
find output_baseline -name "*.b3dm" | wc -l

echo "Stage2 B3DM count:"
find output_stage2 -name "*.b3dm" | wc -l

# 在Cesium中查看
```

**成功标准**:
- 生成的3D Tiles能在Cesium中正常显示
- B3DM文件数量与阶段0相差不超过10%
- 视觉质量与阶段0一致

---

## 6. 阶段3: Tileset生成迁移 ✅ 已完成

**目标**: 使用新的TilesetBuilder生成tileset.json，但B3DM生成保持不变

**状态**: ✅ 已完成（实现方式与方案略有差异，但功能等价）

### 6.1 实际实现（与方案差异说明）

由于B3DM生成逻辑与GDAL数据访问紧密耦合，严格按方案实现`HybridProcessor`需要大量重构。实际采用以下等效实现：

**实际架构**:
```
主流程 (shp23dtile.cpp)
├── 阶段1: ShapefileDataPool (新数据加载)
├── 阶段2: QuadtreeStrategy (新四叉树)
├── 阶段2/3: 原有B3DM生成循环 (旧逻辑)
└── 阶段3: ShapefileTilesetAdapter (新Tileset生成)
    └── 替代方案中的 TilesetBuilder
    └── 功能等价，但更适合Shapefile业务
```

**关键组件**:
- `ShapefileTilesetAdapter`: 承担TilesetBuilder的角色，将Shapefile业务数据结构转换为标准3D Tiles
- `build_hierarchical_tilesets`: 主流程函数，协调B3DM生成和Tileset生成

### 6.2 代码实现

```cpp
// shp23dtile.cpp (阶段3实际实现)
extern "C" bool shp23dtile(const ShapeConversionParams* params) {
    // 1. 阶段1: 新的数据加载
    shapefile::ShapefileDataPool dataPool;
    dataPool.loadFromShapefile(params->input_path, params->height_field);

    // 2. 阶段2: 新的四叉树策略
    auto qtIndex = shapefile::QuadtreeAdapter::buildIndex(dataPool, qtConfig);

    // 3. 阶段2/3: 使用新的四叉树索引，但保持B3DM生成逻辑不变
    node* legacyRoot = convertToLegacyNode(*qtIndex);
    // ... 原有B3DM生成循环 ...

    // 4. 阶段3: 使用 ShapefileTilesetAdapter 生成 tileset
    build_hierarchical_tilesets(leaf_tiles, dest, g_shp_center_lon, g_shp_center_lat);

    // 5. 阶段3验证: 生成 tileset_stage3.json 用于对比
    // ... ShapefileTilesetAdapter 生成 tileset_stage3.json ...

    return true;
}
```

### 6.3 实现与方案对比

| 方案要求 | 实际实现 | 等价性 |
|---------|---------|--------|
| `HybridProcessor` 类 | 主流程直接实现 | 功能等价，未封装为类 |
| `TilesetBuilder` | `ShapefileTilesetAdapter` | 功能等价，接口更适合业务 |
| 旧B3DM生成 | 原有循环 | 完全一致 |
| 输出验证 | tileset.json = tileset_stage3.json | 验证通过 |

### 6.4 验证方式

**测试命令**:
```bash
# 阶段3实现
./_3dtile -f shape -i data/SHP/bj_building/bj_building.shp -o output_stage3/ --height height

# 验证tileset.json结构
python3 -c "import json; t=json.load(open('output_stage3/tileset.json')); print('Root geometricError:', t['root']['geometricError'])"

# 在Cesium中查看
```

**成功标准**:
- tileset.json符合3D Tiles规范
- Cesium能正确加载和显示
- 包围盒和几何误差计算正确

---

## 7. 阶段4: 完整新框架 ✅ 已完成

**目标**: 全部使用新框架实现

**状态**: ✅ 已完成

### 7.1 实际实现

使用 `ShapefileProcessor` 完全替换原有的处理逻辑：

```cpp
// shp23dtile.cpp (阶段4 - 最终)
extern "C" bool shp23dtile(const ShapeConversionParams* params) {
    // 配置 ShapefileProcessor
    shapefile::ShapefileProcessorConfig processorConfig;
    processorConfig.inputPath = filename;
    processorConfig.outputPath = dest;
    processorConfig.heightField = height_field;
    processorConfig.centerLongitude = g_shp_center_lon;
    processorConfig.centerLatitude = g_shp_center_lat;
    processorConfig.enableLOD = params->enable_lod;
    processorConfig.enableSimplification = simplify_params.enable_simplification;
    processorConfig.simplifyParams = simplify_params;
    processorConfig.enableDraco = draco_params.enable_compression;
    processorConfig.dracoParams = draco_params;

    // 配置四叉树
    processorConfig.quadtreeConfig.maxDepth = 10;
    processorConfig.quadtreeConfig.maxItemsPerNode = 1000;
    processorConfig.quadtreeConfig.metricThreshold = 0.01;

    // 配置 Tileset 适配器
    processorConfig.boundingVolumeScaleFactor = 2.0;
    processorConfig.geometricErrorScale = 0.5;
    processorConfig.applyRootTransform = true;

    // 创建并运行处理器
    shapefile::ShapefileProcessor processor(processorConfig);
    auto result = processor.process();

    return result.success;
}
```

### 7.2 关键组件

- `ShapefileProcessor`: 完整处理器，整合所有新组件
- `ShapefileDataPool`: 数据加载（阶段1）
- `QuadtreeStrategy`: 空间索引（阶段2）
- `ShapefileTilesetAdapter`: Tileset 生成（阶段3）
- `B3DMContentGenerator`: B3DM 内容生成

### 7.3 验证结果

**输出对比（阶段3 vs 阶段4）**:
```bash
$ diff tileset_stage3.json tileset_stage4.json
# 仅几何误差有微小差异（浮点精度，< 0.001%）
# 包围盒、transform、路径完全一致
```

**成功标准**:
- ✅ 生成的3D Tiles能在Cesium中正常显示
- ✅ tileset.json符合3D Tiles规范
- ✅ B3DM文件正常生成
- ✅ 与阶段3输出基本一致（仅几何误差有微小浮点精度差异）

---

## 8. 迁移完成总结

### 8.1 架构演进

```
阶段0: 原始实现（单一文件，内嵌四叉树）
   ↓
阶段1: 数据层抽象（ShapefileDataPool）
   ↓
阶段2: 空间索引迁移（QuadtreeStrategy）
   ↓
阶段3: Tileset生成迁移（ShapefileTilesetAdapter）
   ↓
阶段4: 完整新框架（ShapefileProcessor）✅ 当前状态
```

### 8.2 最终架构

```
shp23dtile.cpp (主入口)
    └── ShapefileProcessor (完整处理器)
        ├── ShapefileDataPool (数据加载)
        ├── QuadtreeStrategy (空间索引)
        ├── ShapefileTilesetAdapter (Tileset生成)
        └── B3DMContentGenerator (B3DM生成)
```

### 8.3 文件清单

**核心组件**:
- `src/shapefile/shapefile_processor.h/cpp` - 完整处理器
- `src/shapefile/shapefile_data_pool.h/cpp` - 数据加载
- `src/shapefile/shapefile_spatial_item_adapter.h` - 空间项适配器
- `src/shapefile/quadtree_adapter.h/cpp` - 四叉树适配器
- `src/shapefile/shapefile_tileset_adapter.h/cpp` - Tileset适配器
- `src/shapefile/b3dm_content_generator.h` - B3DM生成接口

**空间索引框架**:
- `src/spatial/core/spatial_item.h` - 空间项接口
- `src/spatial/core/spatial_bounds.h` - 空间边界
- `src/spatial/core/slicing_strategy.h` - 切片策略接口
- `src/spatial/strategy/quadtree_strategy.h` - 四叉树策略
- `src/spatial/builder/tileset_builder.h` - Tileset构建器
- `src/spatial/builder/tiling_path_generator.h` - 路径生成

### 8.4 主流程简化

迁移后的主流程已大幅简化：

```cpp
// 迁移前: ~1500行，内嵌四叉树实现
// 迁移后: ~100行，使用ShapefileProcessor

extern "C" bool shp23dtile(const ShapeConversionParams* params) {
    shapefile::ShapefileProcessorConfig config;
    // ... 配置参数 ...
    shapefile::ShapefileProcessor processor(config);
    return processor.process().success;
}
```

---

## 9. 附录：历史实现

### 9.1 阶段4之前的实现（已归档）

以下实现已在最终版本中移除，仅作历史参考：

```cpp
// shp23dtile.cpp (阶段4 - 最终)
extern "C" bool shp23dtile(const ShapeConversionParams* params) {
    // 1. 配置
    shapefile::ShapefileProcessorConfig config;
    config.inputPath = params->input_path;
    config.outputPath = params->output_path;
    config.heightField = params->height_field;
    config.centerLongitude = centerLon;
    config.centerLatitude = centerLat;
    config.enableLOD = params->enable_lod;

    // 2. 使用完整的ShapefileProcessor
    shapefile::ShapefileProcessor processor(config);
    return processor.process(params->input_path);
}
```

### 7.2 验证方式

**测试命令**:
```bash
# 阶段4实现
./_3dtile -f shape -i data/SHP/bj_building/bj_building.shp -o output_stage4/ --height height

# 完整对比
echo "=== Comparison ==="
echo "Baseline:"
find output_baseline -name "*.b3dm" | wc -l
ls -lh output_baseline/tileset.json

echo "Stage4:"
find output_stage4 -name "*.b3dm" | wc -l
ls -lh output_stage4/tileset.json

# 在Cesium中对比查看
```

---

## 8. 关键设计要点

### 8.1 内存管理原则
1. **零拷贝原则**：数据一旦加载，全程使用指针/引用传递，禁止拷贝大型对象
2. **统一所有权**：使用 `shared_ptr` 管理 `ShapefileSpatialItem` 生命周期
3. **禁止拷贝**：`ShapefileSpatialItem` 应该 `delete` 拷贝构造函数

### 8.2 适配器模式

每个阶段都通过适配器连接新旧组件：

```
阶段1: DataPool → [适配器] → 旧四叉树 → 旧B3DM生成
阶段2: DataPool → 新四叉树 → [适配器] → 旧B3DM生成
阶段3: DataPool → 新四叉树 → 新TilesetBuilder → [适配器] → 旧B3DM生成
阶段4: DataPool → 新四叉树 → 新TilesetBuilder → 新B3DM生成
```

### 8.3 阶段间兼容性接口

```cpp
// 数据层接口
class IShapefileDataProvider {
public:
    virtual ~IShapefileDataProvider() = default;
    virtual void load(const std::string& filename) = 0;
    virtual size_t getItemCount() const = 0;
    virtual const ShapefileSpatialItem* getItem(size_t index) const = 0;
};

// 配置开关
struct MigrationConfig {
    bool useNewDataPool = false;      // 阶段一开关
    bool useNewQuadtree = false;      // 阶段二开关
    bool useNewTilesetBuilder = false; // 阶段三开关
};
```

---

## 9. 实用验证脚本

```bash
#!/bin/bash
# validate_migration.sh

set -e

DATASET="data/SHP/bj_building/bj_building.shp"
HEIGHT_FIELD="height"

echo "=== Migration Validation ==="

# 阶段0: 基准
if [ -d "output_baseline" ]; then
    echo "✓ Baseline exists"
else
    echo "Creating baseline..."
    git stash  # 保存当前修改
    cargo build --release
    ./target/release/_3dtile -f shape -i "$DATASET" -o output_baseline/ --height "$HEIGHT_FIELD"
    git stash pop  # 恢复修改
fi

# 当前阶段
echo "Testing current stage..."
cargo build --release
rm -rf output_current
./target/release/_3dtile -f shape -i "$DATASET" -o output_current/ --height "$HEIGHT_FIELD"

# 对比
echo ""
echo "=== Comparison ==="
echo "B3DM files:"
echo "  Baseline: $(find output_baseline -name '*.b3dm' | wc -l)"
echo "  Current:  $(find output_current -name '*.b3dm' | wc -l)"

echo ""
echo "Tileset size:"
echo "  Baseline: $(ls -lh output_baseline/tileset.json | awk '{print $5}')"
echo "  Current:  $(ls -lh output_current/tileset.json | awk '{print $5}')"

echo ""
echo "=== Next Steps ==="
echo "1. Start HTTP server: cd tests/e2e/3dtiles-viewer && npm start"
echo "2. Open browser and compare:"
echo "   - Baseline: http://localhost:3000/?tileset=output_baseline/tileset.json"
echo "   - Current:  http://localhost:3000/?tileset=output_current/tileset.json"
echo "3. Visually verify they look identical"
```

---

## 10. 回滚策略

### 10.1 Git分支策略
```
main
├── migration/stage1-data-pool
├── migration/stage2-quadtree
├── migration/stage3-tileset
└── migration/stage4-complete
```

### 10.2 配置宏切换
```cpp
// config.h
#define MIGRATION_STAGE 1  // 修改这个宏切换到不同阶段
```

### 10.3 自动回滚触发条件
- 阶段验证失败时自动回滚到上一阶段
- 内存使用增加超过10%
- 性能下降超过20%
- Cesium无法正确加载

---

## 11. 总结

关键要点：
1. **每个阶段都可运行**：每阶段都能产生可查看的3D Tiles
2. **渐进式替换**：从数据层开始，逐步替换到完整框架
3. **适配器连接**：通过适配器连接新旧组件，确保兼容性
4. **实用验证**：通过Cesium查看验证，而非仅单元测试
5. **简单回滚**：Git分支或配置宏即可回滚

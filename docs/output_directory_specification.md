# 3D Tiles 输出目录规范

## 1. 概述

本文档定义了四叉树(Quadtree)和八叉树(Octree)切片策略生成B3DM和tileset.json的统一目录规范，采用 **shp23dtile.cpp 的 `tile/{z}/{x}/{y}/` 目录结构** 作为标准，确保两种策略的输出结构一致、可预测、易于管理。

## 2. 现状分析

### 2.1 shp23dtile 当前目录结构 (采用为标准)
```
output/
├── tileset.json                    # 根tileset
└── tile/                           # 瓦片内容目录
    ├── {z}/                        # 层级目录
    │   ├── {x}/                    # X坐标目录
    │   │   ├── {y}/                # Y坐标目录
    │   │   │   ├── tileset.json    # 子tileset
    │   │   │   └── content.b3dm    # B3DM内容
```

**特点**:
- 使用 `tile/{z}/{x}/{y}/` 层级结构
- 每个目录包含 `tileset.json` 和 `content.b3dm`
- 基于四叉树坐标 (z/x/y)
- 支持嵌套tileset

### 2.2 FBXPipeline 当前目录结构 (需迁移)
```
output/
├── tileset.json                    # 根tileset
└── tile_0/                         # 根节点内容
    ├── content.b3dm                # B3DM内容
    ├── content_lod1.b3dm           # LOD1
    └── content_lod2.b3dm           # LOD2
```

**特点**:
- 使用 `tile_{treePath}` 扁平结构
- 使用 `treePath` (如 "0_1_2") 标识节点
- 支持LOD文件命名

### 2.3 问题
1. **目录结构不一致** - shp23dtile使用z/x/y，FBXPipeline使用treePath
2. **文件命名不统一** - content.b3dm vs content_lod{n}.b3dm
3. **层级组织混乱** - 没有统一的层级深度控制

## 3. 统一目录规范 (基于shp23dtile)

### 3.1 规范设计原则

1. **采用shp23dtile结构** - 使用 `tile/{z}/{x}/{y}/` 作为标准
2. **兼容四叉树和八叉树** - 通过坐标映射统一处理
3. **层级可预测** - 目录深度与树深度对应
4. **命名一致性** - 统一的文件命名约定
5. **扩展性** - 支持LOD、多内容等扩展

### 3.2 统一目录结构

```
output/
├── tileset.json                            # 根tileset (必需)
└── tile/                                   # 瓦片内容根目录
    ├── {z}/                                # 层级目录 (z=深度)
    │   ├── {x}/                            # X坐标目录
    │   │   ├── {y}/                        # Y坐标目录
    │   │   │   ├── tileset.json            # 子tileset
    │   │   │   ├── content.b3dm            # LOD0 (近距离，最高精度)
    │   │   │   ├── content_lod1.b3dm       # LOD1 (中距离，中等精度)
    │   │   │   └── content_lod2.b3dm       # LOD2 (远距离，最低精度)
```

**LOD说明**:
- `content.b3dm` = LOD0 (最高精度，近距离显示)
- `content_lod1.b3dm` = LOD1 (中等精度，中距离显示)
- `content_lod2.b3dm` = LOD2 (最低精度，远距离显示)
- LOD级别通过文件名后缀区分，z坐标保持空间层级含义

### 3.3 路径编码规则

#### 3.3.1 四叉树映射 (直接使用)
- **z** = 四叉树层级
- **x** = X坐标
- **y** = Y坐标

| 四叉树坐标 | 目录路径 | 说明 |
|-----------|---------|------|
| z=0, x=0, y=0 | tile/0/0/0 | 根节点 |
| z=5, x=16, y=8 | tile/5/16/8 | 第5层节点 |

#### 3.3.2 八叉树映射 (转换为z/x/y)
- **z** = 八叉树深度
- **x** = 节点索引 (深度1: 0-7, 深度2+: parentIndex*8 + nodeIndex)
- **y** = 子层级 (深度-2，用于区分不同层级的子节点)

| 八叉树深度 | 节点路径 | 目录路径 | 说明 |
|-----------|---------|---------|------|
| 0 | - | tile/0/0/0 | 根节点 |
| 1 | 3 | tile/1/3/0 | 深度1，节点3 |
| 2 | 1→5 | tile/2/13/0 | x=1*8+5=13, y=0 |
| 3 | 1→5→2 | tile/3/106/1 | x=13*8+2=106, y=1 |

**映射公式**:
```cpp
// 八叉树 (depth, nodeIndex, parentIndices) -> (z, x, y)
std::tuple<int, int, int> octreeToZXY(int depth, int nodeIndex,
                                       const std::vector<int>& parentIndices) {
    int z = depth;

    if (depth == 0) {
        return {0, 0, 0};
    }

    if (depth == 1) {
        return {1, nodeIndex, 0};
    }

    // 深度>=2
    int parentIndex = parentIndices.empty() ? 0 : parentIndices.back();
    int x = parentIndex * 8 + nodeIndex;
    int y = depth - 2;

    return {z, x, y};
}
```

#### 3.3.3 内容文件命名与LOD规范

**LOD文件命名规则**:
| 文件名 | LOD级别 | 适用场景 | geometricError |
|--------|---------|----------|----------------|
| `content.b3dm` | LOD0 | 近距离，最高精度 | 最小 |
| `content_lod1.b3dm` | LOD1 | 中距离，中等精度 | 中等 |
| `content_lod2.b3dm` | LOD2 | 远距离，最低精度 | 最大 |

**注意**: LOD编号与精度一致，编号越小精度越高（与3D Tiles的geometricError递减一致）

**类型变体**: `content_{type}.b3dm` (type=point, line, polygon等)

### 3.4 LOD与tileset.json结构

#### 3.4.1 嵌套tileset结构（3D Tiles 1.0兼容）
每个节点目录包含独立的tileset.json，通过`children`引用子节点：

```
tile/0/0/0/tileset.json (根节点)
├── content.b3dm (LOD0)
├── content_lod1.b3dm (LOD1)
├── content_lod2.b3dm (LOD2)
└── 引用: tile/1/0/0/tileset.json (子节点)
```

#### 3.4.2 tileset.json示例
```json
{
  "asset": { "version": "1.0" },
  "root": {
    "boundingVolume": { "region": [...] },
    "geometricError": 10,
    "content": { "uri": "content.b3dm" },
    "children": [
      {
        "boundingVolume": { "region": [...] },
        "geometricError": 50,
        "content": { "uri": "content_lod1.b3dm" }
      },
      {
        "boundingVolume": { "region": [...] },
        "geometricError": 100,
        "content": { "uri": "content_lod2.b3dm" }
      },
      {
        "boundingVolume": { "region": [...] },
        "geometricError": 200,
        "content": { "uri": "../1/0/0/tileset.json" }
      }
    ]
  }
}
```

#### 3.4.3 geometricError计算
- LOD0 (content.b3dm): `geometricError = baseError / 4` (最小)
- LOD1 (content_lod1.b3dm): `geometricError = baseError / 2` (中等)
- LOD2 (content_lod2.b3dm): `geometricError = baseError` (最大)
- 子节点: `geometricError = baseError * 2` (延迟加载)

## 4. 实现方案

### 4.1 目录规范实现位置

**建议放在**: `spatial/builder/shp23dtile_path_generator.h`

原因:
1. 属于builder模块的功能
2. 与TilesetBuilder紧密配合
3. 采用shp23dtile标准命名
4. 可被所有切片策略复用

### 4.2 路径生成器接口

```cpp
#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <optional>
#include <tuple>

namespace spatial::builder {

/**
 * @brief shp23dtile风格路径配置
 */
struct Shp23dtilePathConfig {
    // 根输出目录
    std::string outputRoot;

    // 瓦片子目录名
    std::string tilesDir = "tile";

    // 内容文件名
    std::string contentFilename = "content.b3dm";

    // tileset文件名
    std::string tilesetFilename = "tileset.json";

    // 最小层级 (该层级及以下的tileset放在根目录)
    int minZForRoot = 0;
};

/**
 * @brief shp23dtile风格路径生成器
 *
 * 统一生成 tile/{z}/{x}/{y}/ 风格的路径
 * 兼容四叉树 (z/x/y) 和八叉树 (depth/nodeIndex)
 */
class Shp23dtilePathGenerator {
public:
    explicit Shp23dtilePathGenerator(const Shp23dtilePathConfig& config);

    // ==================== 四叉树路径生成 ====================

    /**
     * @brief 获取四叉树节点目录
     * @param z 四叉树层级
     * @param x X坐标
     * @param y Y坐标
     * @return 节点目录绝对路径
     */
    std::filesystem::path getQuadtreeNodePath(int z, int x, int y) const;

    /**
     * @brief 获取四叉树tileset路径
     */
    std::filesystem::path getQuadtreeTilesetPath(int z, int x, int y) const;

    /**
     * @brief 获取四叉树内容路径
     */
    std::filesystem::path getQuadtreeContentPath(int z, int x, int y,
                                                   int lodLevel = -1) const;

    // ==================== 八叉树路径生成 ====================

    /**
     * @brief 获取八叉树节点目录
     * @param depth 八叉树深度
     * @param nodeIndex 节点索引 (0-7)
     * @param parentIndices 父节点索引路径 (用于深度>1)
     */
    std::filesystem::path getOctreeNodePath(
        int depth,
        int nodeIndex,
        const std::vector<int>& parentIndices = {}
    ) const;

    /**
     * @brief 获取八叉树tileset路径
     */
    std::filesystem::path getOctreeTilesetPath(
        int depth,
        int nodeIndex,
        const std::vector<int>& parentIndices = {}
    ) const;

    /**
     * @brief 获取八叉树内容路径
     */
    std::filesystem::path getOctreeContentPath(
        int depth,
        int nodeIndex,
        const std::vector<int>& parentIndices = {},
        int lodLevel = -1
    ) const;

    // ==================== 通用方法 ====================

    /**
     * @brief 获取根tileset路径
     */
    std::filesystem::path getRootTilesetPath() const;

    /**
     * @brief 获取相对于根tileset的URI
     */
    std::string getRelativeUri(const std::filesystem::path& absolutePath) const;

    /**
     * @brief 创建目录结构
     */
    bool createDirectory(const std::filesystem::path& path) const;

    /**
     * @brief 解析路径为四叉树坐标
     * @return std::optional<std::tuple<int, int, int>> (z, x, y)
     */
    std::optional<std::tuple<int, int, int>> parseQuadtreePath(
        const std::filesystem::path& path
    ) const;

    /**
     * @brief 解析路径为八叉树坐标
     * @return std::optional<std::pair<int, std::vector<int>>> (depth, indices)
     */
    std::optional<std::pair<int, std::vector<int>>> parseOctreePath(
        const std::filesystem::path& path
    ) const;

private:
    Shp23dtilePathConfig config_;
    std::filesystem::path tilesRoot_;

    // 将八叉树节点索引转换为x/y坐标
    std::pair<int, int> octreeIndexToXY(int depth, int nodeIndex,
                                         const std::vector<int>& parentIndices) const;

    // 从x/y坐标解析八叉树节点索引
    std::pair<int, std::vector<int>> xyToOctreeIndex(int z, int x, int y) const;
};

} // namespace spatial::builder
```

### 4.3 实现代码

```cpp
// spatial/builder/shp23dtile_path_generator.cpp

#include "shp23dtile_path_generator.h"
#include <sstream>
#include <algorithm>

namespace spatial::builder {

Shp23dtilePathGenerator::Shp23dtilePathGenerator(const Shp23dtilePathConfig& config)
    : config_(config)
    , tilesRoot_(std::filesystem::path(config.outputRoot) / config.tilesDir)
{}

// ==================== 四叉树路径生成 ====================

std::filesystem::path Shp23dtilePathGenerator::getQuadtreeNodePath(int z, int x, int y) const {
    return tilesRoot_ / std::to_string(z) / std::to_string(x) / std::to_string(y);
}

std::filesystem::path Shp23dtilePathGenerator::getQuadtreeTilesetPath(int z, int x, int y) const {
    if (z <= config_.minZForRoot) {
        return std::filesystem::path(config_.outputRoot) / config_.tilesetFilename;
    }
    return getQuadtreeNodePath(z, x, y) / config_.tilesetFilename;
}

std::filesystem::path Shp23dtilePathGenerator::getQuadtreeContentPath(int z, int x, int y,
                                                                       int lodLevel) const {
    std::string filename = config_.contentFilename;
    if (lodLevel >= 0) {
        size_t dotPos = filename.find_last_of('.');
        if (dotPos != std::string::npos) {
            filename = filename.substr(0, dotPos) + "_lod" + std::to_string(lodLevel) +
                       filename.substr(dotPos);
        }
    }
    return getQuadtreeNodePath(z, x, y) / filename;
}

// ==================== 八叉树路径生成 ====================

std::pair<int, int> Shp23dtilePathGenerator::octreeIndexToXY(
    int depth, int nodeIndex, const std::vector<int>& parentIndices
) const {
    if (depth == 0) {
        return {0, 0};
    }

    if (depth == 1) {
        return {nodeIndex, 0};
    }

    // 深度>=2: x = parentIndex * 8 + nodeIndex, y = depth - 2
    int parentIndex = parentIndices.empty() ? 0 : parentIndices.back();
    int x = parentIndex * 8 + nodeIndex;
    int y = depth - 2;

    return {x, y};
}

std::filesystem::path Shp23dtilePathGenerator::getOctreeNodePath(
    int depth, int nodeIndex, const std::vector<int>& parentIndices
) const {
    auto [x, y] = octreeIndexToXY(depth, nodeIndex, parentIndices);
    return getQuadtreeNodePath(depth, x, y);
}

std::filesystem::path Shp23dtilePathGenerator::getOctreeTilesetPath(
    int depth, int nodeIndex, const std::vector<int>& parentIndices
) const {
    auto [x, y] = octreeIndexToXY(depth, nodeIndex, parentIndices);
    return getQuadtreeTilesetPath(depth, x, y);
}

std::filesystem::path Shp23dtilePathGenerator::getOctreeContentPath(
    int depth, int nodeIndex, const std::vector<int>& parentIndices, int lodLevel
) const {
    auto [x, y] = octreeIndexToXY(depth, nodeIndex, parentIndices);
    return getQuadtreeContentPath(depth, x, y, lodLevel);
}

// ==================== 通用方法 ====================

std::filesystem::path Shp23dtilePathGenerator::getRootTilesetPath() const {
    return std::filesystem::path(config_.outputRoot) / config_.tilesetFilename;
}

std::string Shp23dtilePathGenerator::getRelativeUri(
    const std::filesystem::path& absolutePath
) const {
    std::filesystem::path rootDir = std::filesystem::path(config_.outputRoot);
    return std::filesystem::relative(absolutePath, rootDir).generic_string();
}

bool Shp23dtilePathGenerator::createDirectory(const std::filesystem::path& path) const {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return !ec;
}

std::optional<std::tuple<int, int, int>> Shp23dtilePathGenerator::parseQuadtreePath(
    const std::filesystem::path& path
) const {
    std::filesystem::path relPath = std::filesystem::relative(path, tilesRoot_);

    std::vector<std::string> parts;
    for (const auto& part : relPath) {
        parts.push_back(part.string());
    }

    if (parts.size() >= 3) {
        try {
            int z = std::stoi(parts[0]);
            int x = std::stoi(parts[1]);
            int y = std::stoi(parts[2]);
            return std::make_tuple(z, x, y);
        } catch (...) {
            return std::nullopt;
        }
    }

    return std::nullopt;
}

std::optional<std::pair<int, std::vector<int>>> Shp23dtilePathGenerator::parseOctreePath(
    const std::filesystem::path& path
) const {
    auto quadCoord = parseQuadtreePath(path);
    if (!quadCoord) {
        return std::nullopt;
    }

    auto [z, x, y] = *quadCoord;
    return xyToOctreeIndex(z, x, y);
}

std::pair<int, std::vector<int>> Shp23dtilePathGenerator::xyToOctreeIndex(
    int z, int x, int y
) const {
    std::vector<int> indices;

    if (z == 0) {
        return {0, indices};
    }

    if (z == 1) {
        indices.push_back(x);
        return {1, indices};
    }

    // 深度>=2: 反向解析
    int depth = y + 2;
    int remainingX = x;

    for (int d = depth; d > 0; --d) {
        int nodeIndex = remainingX % 8;
        indices.push_back(nodeIndex);
        remainingX /= 8;
    }

    std::reverse(indices.begin(), indices.end());
    return {depth, indices};
}

} // namespace spatial::builder
```

## 5. TilesetBuilder集成

### 5.1 更新TilesetBuildConfig

```cpp
struct TilesetBuildConfig {
    // ... 原有配置 ...

    // 输出路径配置
    Shp23dtilePathConfig pathConfig;

    // 或者使用默认配置
    std::optional<Shp23dtilePathConfig> customPathConfig;
};
```

### 5.2 更新TilesetBuilder构建流程

```cpp
template<typename StrategyType>
tileset::Tileset TilesetBuilder::build(
    const StrategyType& strategy,
    const TilesetBuildConfig& config
) {
    // 创建shp23dtile风格路径生成器
    Shp23dtilePathConfig pathConfig = config.customPathConfig.value_or(
        Shp23dtilePathConfig{config.outputPath}
    );
    Shp23dtilePathGenerator pathGen(pathConfig);

    // 构建根Tile
    tileset::Tile rootTile = buildTileRecursive(
        strategy,
        strategy.getRootNode(),
        pathGen,
        {},  // 空路径表示根
        true, // isRoot
        config
    );

    // 创建Tileset
    tileset::Tileset tileset(rootTile);
    tileset.setVersion("1.0");
    tileset.setGltfUpAxis("Z");
    tileset.updateGeometricError();

    return tileset;
}

template<typename StrategyType>
tileset::Tile TilesetBuilder::buildTileRecursive(
    const StrategyType& strategy,
    const void* node,
    Shp23dtilePathGenerator& pathGen,
    const std::vector<int>& nodePath,
    bool isRoot,
    const TilesetBuildConfig& config
) {
    // 获取节点信息
    auto bounds = strategy.getNodeBounds(node);
    auto items = strategy.getNodeItems(node);
    bool isLeaf = strategy.isLeafNode(node);
    int depth = nodePath.size();

    // 获取节点坐标
    int z, x, y;
    std::filesystem::path nodeDir;

    if constexpr (StrategyType::Dimension == 2) {
        // 四叉树: 直接获取z/x/y
        auto* quadNode = static_cast<const QuadtreeNode*>(node);
        z = quadNode->z;
        x = quadNode->x;
        y = quadNode->y;
        nodeDir = pathGen.getQuadtreeNodePath(z, x, y);
    } else {
        // 八叉树: 使用深度和节点索引
        int nodeIndex = isRoot ? 0 : nodePath.back();
        std::vector<int> parentIndices(nodePath.begin(),
                                       nodePath.begin() + std::max(0, depth - 1));
        nodeDir = pathGen.getOctreeNodePath(depth, nodeIndex, parentIndices);

        auto [zx, xx, yx] = pathGen.octreeIndexToXY(depth, nodeIndex, parentIndices);
        z = zx; x = xx; y = yx;
    }

    // 创建目录
    pathGen.createDirectory(nodeDir);

    // 创建Tile
    tileset::Tile tile;
    tile.boundingVolume = bounds.toBoundingVolume();
    tile.geometricError = calculateGeometricError(bounds, depth);

    // 生成内容
    if (shouldGenerateContent(items, isLeaf, config)) {
        std::filesystem::path contentPath = pathGen.getQuadtreeContentPath(z, x, y);
        std::string contentUri = config.contentGenerator(
            node, items, contentPath.string()
        );

        if (!contentUri.empty()) {
            std::string relativeUri = pathGen.getRelativeUri(contentPath);
            tile.setContent(relativeUri);
        }
    }

    // 递归构建子节点
    if (!isLeaf) {
        auto childNodes = strategy.getChildNodes(node);
        for (size_t i = 0; i < childNodes.size(); ++i) {
            std::vector<int> childPath = nodePath;
            childPath.push_back(static_cast<int>(i));

            tileset::Tile childTile = buildTileRecursive(
                strategy, childNodes[i], pathGen, childPath, false, config
            );
            tile.addChild(std::move(childTile));
        }
    }

    return tile;
}
```

## 6. 向后兼容

### 6.1 兼容shp23dtile旧路径

shp23dtile的 `tile/{z}/{x}/{y}/` 结构与新的统一规范完全一致，无需特殊处理。

### 6.2 兼容FBXPipeline旧路径

```cpp
// 提供兼容模式配置 (迁移期间使用)
Shp23dtilePathConfig getLegacyFBXConfig(const std::string& outputRoot) {
    Shp23dtilePathConfig config;
    config.outputRoot = outputRoot;
    config.tilesDir = "tile";  // 使用tile目录
    config.contentFilename = "content.b3dm";
    return config;
}
```

## 7. 示例输出

### 7.1 四叉树输出

```
/output/shapefile/
├── tileset.json
└── tile/
    ├── 0/
    │   └── 0/
    │       └── 0/
    │           ├── tileset.json
    │           └── content.b3dm
    ├── 5/
    │   ├── 16/
    │   │   ├── 8/
    │   │   │   ├── tileset.json
    │   │   │   └── content.b3dm
    │   │   └── 9/
    │   │       ├── tileset.json
    │   │       └── content.b3dm
    │   └── 17/
    └── 6/
```

### 7.2 八叉树输出

```
/output/fbx/
├── tileset.json
└── tile/
    ├── 0/
    │   └── 0/
    │       └── 0/
    │           ├── tileset.json
    │           ├── content.b3dm
    │           ├── content_lod1.b3dm
    │           └── content_lod2.b3dm
    ├── 1/
    │   ├── 0/
    │   │   └── 0/
    │   ├── 1/
    │   │   └── 0/
    │   ├── 2/
    │   ├── 3/
    │   ├── 4/
    │   ├── 5/
    │   ├── 6/
    │   └── 7/
    └── 2/
        ├── 0/
        │   ├── 0/
        │   ├── 1/
        │   ├── 2/
        │   └── 3/
        ├── 1/
        │   └── ...
        └── ...
```

## 8. 总结

统一目录规范的关键点:

1. **采用shp23dtile结构** - 使用 `tile/{z}/{x}/{y}/` 作为标准
2. **四叉树直接使用** - z/x/y 对应四叉树坐标
3. **八叉树映射** - 深度→z, 节点索引→x, 子层级→y
4. **文件命名统一** - `content.b3dm`, `content_lod{n}.b3dm`
5. **向后兼容** - 与现有shp23dtile输出完全兼容

该规范作为 `spatial/builder` 模块的一部分实现，与 `TilesetBuilder` 紧密集成。

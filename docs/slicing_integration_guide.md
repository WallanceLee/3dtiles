# 空间切片与Tileset/B3DM模块对接指南

## 1. 架构概览

### 1.1 数据流向
```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           空间切片与输出流程                                  │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐                  │
│  │  空间对象     │───▶│  切片策略     │───▶│  空间索引     │                  │
│  │  (Items)     │    │ (Strategy)   │    │  (Tree)      │                  │
│  └──────────────┘    └──────────────┘    └──────┬───────┘                  │
│                                                  │                          │
│                                                  ▼                          │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                      TilesetBuilder (对接层)                         │   │
│  │  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐          │   │
│  │  │  遍历节点     │───▶│  生成B3DM    │───▶│  构建Tile    │          │   │
│  │  │ (Traverse)   │    │ (Content)    │    │ (Tileset)    │          │   │
│  │  └──────────────┘    └──────┬───────┘    └──────┬───────┘          │   │
│  │                             │                   │                  │   │
│  │                             ▼                   ▼                  │   │
│  │  ┌─────────────────────────────────────────────────────────────┐  │   │
│  │  │                      输出模块                                 │  │   │
│  │  │  ┌──────────────┐              ┌──────────────┐              │  │   │
│  │  │  │  B3DM Writer │              │ TilesetWriter│              │  │   │
│  │  │  │  (b3dm)      │              │  (tileset)   │              │  │   │
│  │  │  └──────────────┘              └──────────────┘              │  │   │
│  │  └─────────────────────────────────────────────────────────────┘  │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 1.2 核心对接点

| 对接点 | 输入 | 输出 | 模块 |
|-------|------|------|------|
| 空间索引 → TilesetBuilder | 树节点 + 空间对象 | Tile层次结构 | spatial/builder |
| TilesetBuilder → ContentGenerator | 节点对象 | B3DM文件路径 | 业务层实现 |
| ContentGenerator → B3DM Writer | GLB数据 + Batch数据 | B3DM文件 | b3dm |
| TilesetBuilder → TilesetWriter | Tileset对象 | tileset.json | tileset |

---

## 2. 与Tileset模块的对接

### 2.1 对接流程

```cpp
// 1. 从空间索引构建Tileset
template<typename StrategyType, typename ItemType>
tileset::Tileset TilesetBuilder::build(
    const StrategyType& strategy,
    const TilesetBuildConfig& config
) {
    // 2. 递归构建Tile层次结构
    tileset::Tile rootTile = buildTileRecursive(
        strategy,
        strategy.getRootNode(),
        config.outputPath,
        "0",
        config
    );

    // 3. 创建Tileset
    tileset::Tileset tileset(rootTile);
    tileset.setVersion("1.0");
    tileset.setGltfUpAxis("Z");

    // 4. 计算根几何误差
    tileset.updateGeometricError();

    return tileset;
}
```

### 2.2 Tile构建递归函数

```cpp
template<typename StrategyType, typename ItemType>
tileset::Tile TilesetBuilder::buildTileRecursive(
    const StrategyType& strategy,
    const void* node,
    const std::string& parentPath,
    const std::string& treePath,
    const TilesetBuildConfig& config
) {
    // 1. 获取节点信息
    auto bounds = strategy.getNodeBounds(node);
    auto items = strategy.getNodeItems(node);
    bool isLeaf = strategy.isLeafNode(node);

    // 2. 创建Tile的包围体 (ENU坐标)
    tileset::Box boundingVolume = createBoundingVolume(bounds, config);

    // 3. 计算几何误差
    double geometricError = computeGeometricError(bounds, config.geometricErrorScale);

    // 4. 创建Tile
    tileset::Tile tile(boundingVolume, geometricError);

    // 5. 生成内容 (如果是叶子节点或达到输出条件)
    if (shouldGenerateContent(node, items, config)) {
        std::string contentPath = generateContentPath(parentPath, treePath);

        // 调用内容生成器 (生成B3DM)
        std::string contentUri = config.contentGenerator(
            node, items, contentPath
        );

        if (!contentUri.empty()) {
            tile.setContent(contentUri);
        }
    }

    // 6. 递归构建子节点
    if (!isLeaf) {
        auto childNodes = strategy.getChildNodes(node);
        int childIndex = 0;
        for (const void* childNode : childNodes) {
            std::string childTreePath = treePath + "_" + std::to_string(childIndex);
            tileset::Tile childTile = buildTileRecursive(
                strategy, childNode, parentPath, childTreePath, config
            );
            tile.addChild(std::move(childTile));
            childIndex++;
        }
    }

    return tile;
}
```

### 2.3 包围体创建

```cpp
// 四叉树 (2D) → Box (ENU 3D)
tileset::Box createBoundingVolume2D(
    const spatial::core::SpatialBounds<double, 2>& bounds2D,
    const TilesetBuildConfig& config
) {
    // 2D bounds (WGS84度) → ENU米
    double centerLon = (bounds2D.min[0] + bounds2D.max[0]) * 0.5;
    double centerLat = (bounds2D.min[1] + bounds2D.max[1]) * 0.5;

    // 转换为ENU坐标 (相对于全局中心)
    double offsetX = longti_to_meter(
        degree2rad(centerLon - config.centerLongitude),
        degree2rad(config.centerLatitude)
    );
    double offsetY = lati_to_meter(
        degree2rad(centerLat - config.centerLatitude)
    );

    // 计算半轴长度
    double halfWidth = longti_to_meter(
        degree2rad(bounds2D.max[0] - centerLon),
        degree2rad(centerLat)
    ) * config.boundingVolumeScaleFactor;

    double halfHeight = lati_to_meter(
        degree2rad(bounds2D.max[1] - centerLat)
    ) * config.boundingVolumeScaleFactor;

    // 高度范围 (shp23dtile特有)
    double halfZ = (config.maxHeight - config.minHeight) * 0.5 *
                   config.boundingVolumeScaleFactor;

    return tileset::Box::fromCenterAndHalfLengths(
        offsetX, offsetY, halfZ,  // 中心点
        halfWidth, halfHeight, halfZ  // 半轴
    );
}

// 八叉树 (3D) → Box (ENU 3D)
tileset::Box createBoundingVolume3D(
    const spatial::core::SpatialBounds<double, 3>& bounds3D,
    const TilesetBuildConfig& config
) {
    // 已经是ENU坐标，直接转换
    double cx = (bounds3D.min[0] + bounds3D.max[0]) * 0.5;
    double cy = (bounds3D.min[1] + bounds3D.max[1]) * 0.5;
    double cz = (bounds3D.min[2] + bounds3D.max[2]) * 0.5;

    double hx = (bounds3D.max[0] - bounds3D.min[0]) * 0.5 *
                config.boundingVolumeScaleFactor;
    double hy = (bounds3D.max[1] - bounds3D.min[1]) * 0.5 *
                config.boundingVolumeScaleFactor;
    double hz = (bounds3D.max[2] - bounds3D.min[2]) * 0.5 *
                config.boundingVolumeScaleFactor;

    return tileset::Box::fromCenterAndHalfLengths(cx, cy, cz, hx, hy, hz);
}
```

### 2.4 根节点Transform (ENU → ECEF)

```cpp
// 创建根节点Transform矩阵 (仅根节点需要)
std::optional<tileset::TransformMatrix> createRootTransform(
    const TilesetBuildConfig& config
) {
    if (!config.applyRootTransform) {
        return std::nullopt;
    }

    // 使用CoordinateTransformer计算ENU→ECEF矩阵
    glm::dmat4 enuToEcef = coords::CoordinateTransformer::CalcEnuToEcefMatrix(
        config.centerLongitude,
        config.centerLatitude,
        config.centerHeight
    );

    // 转换为tileset::TransformMatrix
    tileset::TransformMatrix matrix;
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            matrix.values[c * 4 + r] = enuToEcef[c][r];
        }
    }

    return matrix;
}

// 在构建根Tile时应用
if (isRootNode) {
    auto transform = createRootTransform(config);
    if (transform) {
        tile.setTransform(*transform);
    }
}
```

---

## 3. 与B3DM模块的对接

### 3.1 内容生成器接口

```cpp
/**
 * @brief 内容生成器配置
 */
struct ContentGeneratorConfig {
    // 输出路径
    std::string outputRoot;
    std::string contentPrefix = "tile";

    // 地理坐标 (用于坐标转换)
    double centerLongitude = 0.0;
    double centerLatitude = 0.0;
    double centerHeight = 0.0;

    // 优化选项
    bool enableDraco = false;
    bool enableLOD = false;

    // BatchTable配置
    bool includeBatchTable = true;
    std::vector<std::string> batchAttributes;  // 要包含的属性名
};

/**
 * @brief 内容生成器接口
 *
 * 业务层需要实现此接口来生成B3DM内容
 */
class IContentGenerator {
public:
    virtual ~IContentGenerator() = default;

    /**
     * @brief 生成节点内容
     *
     * @param node 空间索引节点 (void* 避免模板暴露)
     * @param items 节点内的空间对象
     * @param outputPath 输出路径
     * @return 生成的内容URI (相对于tileset.json的路径)
     */
    virtual std::string generate(
        const void* node,
        const std::vector<std::reference_wrapper<void>>& items,
        const std::string& outputPath
    ) = 0;
};
```

### 3.2 B3DM生成流程

```cpp
/**
 * @brief Shapefile B3DM生成器实现
 */
class ShapefileContentGenerator : public IContentGenerator {
public:
    ShapefileContentGenerator(const ContentGeneratorConfig& config)
        : config_(config) {}

    std::string generate(
        const void* node,
        const std::vector<std::reference_wrapper<void>>& items,
        const std::string& outputPath
    ) override {
        if (items.empty()) {
            return "";
        }

        // 1. 转换空间对象为几何数据
        std::vector<osg::ref_ptr<osg::Geometry>> geometries;
        std::vector<std::map<std::string, nlohmann::json>> properties;

        for (auto& itemRef : items) {
            auto* item = static_cast<ShapefileSpatialItem*>(itemRef.get());
            geometries.insert(geometries.end(),
                            item->geometries.begin(),
                            item->geometries.end());
            properties.push_back(item->properties);
        }

        // 2. 合并几何体
        osg::ref_ptr<osg::Geometry> mergedGeom = mergeGeometries(geometries);

        // 3. 创建GLTF模型
        tinygltf::Model gltfModel = createGLTFModel(mergedGeom, properties);

        // 4. 序列化为GLB
        std::string glbBuffer = serializeToGLB(gltfModel);

        // 5. 创建Batch数据
        b3dm::BatchData batchData;
        for (size_t i = 0; i < items.size(); ++i) {
            batchData.batchIds.push_back(static_cast<int>(i));
            // 添加其他属性...
        }

        // 6. 包装为B3DM
        b3dm::Options b3dmOptions;
        std::string b3dmBuffer = b3dm::wrapGlbToB3dm(glbBuffer, batchData, b3dmOptions);

        // 7. 写入文件
        std::string fileName = "content.b3dm";
        std::string fullPath = outputPath + "/" + fileName;
        b3dm::writeB3dmToFile(fullPath, b3dmBuffer);

        // 8. 返回相对URI
        return fileName;
    }

private:
    ContentGeneratorConfig config_;
};
```

### 3.3 FBX B3DM生成器 (带LOD)

```cpp
/**
 * @brief FBX B3DM生成器实现 (支持LOD)
 */
class FBXContentGenerator : public IContentGenerator {
public:
    FBXContentGenerator(const ContentGeneratorConfig& config, FBXLoader* loader)
        : config_(config), loader_(loader) {}

    std::string generate(
        const void* node,
        const std::vector<std::reference_wrapper<void>>& items,
        const std::string& outputPath
    ) override {
        if (items.empty()) {
            return "";
        }

        // 1. 转换空间对象
        std::vector<FBXSpatialItem*> fbxItems;
        for (auto& itemRef : items) {
            fbxItems.push_back(static_cast<FBXSpatialItem*>(itemRef.get()));
        }

        // 2. 生成LOD链 (如果启用)
        if (config_.enableLOD) {
            return generateWithLOD(fbxItems, outputPath);
        } else {
            return generateSingle(fbxItems, outputPath);
        }
    }

private:
    std::string generateSingle(
        const std::vector<FBXSpatialItem*>& items,
        const std::string& outputPath
    ) {
        // 1. 合并几何体
        auto mergedGeom = mergeFBXGeometries(items);

        // 2. 应用简化 (如果启用)
        if (config_.enableSimplify) {
            simplifyGeometry(mergedGeom);
        }

        // 3. 创建GLTF
        tinygltf::Model gltfModel = createGLTFModel(mergedGeom, items);

        // 4. 应用Draco压缩 (如果启用)
        if (config_.enableDraco) {
            applyDracoCompression(gltfModel);
        }

        // 5. 序列化并包装为B3DM
        std::string glbBuffer = serializeToGLB(gltfModel);
        b3dm::BatchData batchData = createBatchData(items);
        std::string b3dmBuffer = b3dm::wrapGlbToB3dm(glbBuffer, batchData, {});

        // 6. 写入文件
        std::string filePath = outputPath + "/content.b3dm";
        b3dm::writeB3dmToFile(filePath, b3dmBuffer);

        return "content.b3dm";
    }

    std::string generateWithLOD(
        const std::vector<FBXSpatialItem*>& items,
        const std::string& outputPath
    ) {
        // LOD比例: 1.0, 0.5, 0.25
        std::vector<float> lodRatios = {1.0f, 0.5f, 0.25f};
        std::vector<std::string> contentUris;

        for (size_t lodLevel = 0; lodLevel < lodRatios.size(); ++lodLevel) {
            float ratio = lodRatios[lodLevel];

            // 1. 合并几何体
            auto mergedGeom = mergeFBXGeometries(items);

            // 2. 应用简化
            SimplificationParams simParams;
            simParams.target_ratio = ratio;
            simplifyGeometry(mergedGeom, simParams);

            // 3. 创建GLTF
            tinygltf::Model gltfModel = createGLTFModel(mergedGeom, items);

            // 4. 应用Draco (LOD0可选)
            if (config_.enableDraco && (lodLevel > 0 || config_.dracoForLOD0)) {
                applyDracoCompression(gltfModel);
            }

            // 5. 生成B3DM
            std::string glbBuffer = serializeToGLB(gltfModel);
            b3dm::BatchData batchData = createBatchData(items);
            std::string b3dmBuffer = b3dm::wrapGlbToB3dm(glbBuffer, batchData, {});

            // 6. 写入文件 (content_lod0.b3dm, content_lod1.b3dm, ...)
            std::string fileName = "content_lod" + std::to_string(lodLevel) + ".b3dm";
            std::string filePath = outputPath + "/" + fileName;
            b3dm::writeB3dmToFile(filePath, b3dmBuffer);

            contentUris.push_back(fileName);
        }

        // 返回多个URI (使用逗号分隔或返回主URI)
        return contentUris[0];  // 主内容
    }

    ContentGeneratorConfig config_;
    FBXLoader* loader_;
};
```

---

## 4. 完整对接示例

### 4.1 shp23dtile完整流程

```cpp
// shp23dtile.cpp (迁移后)
void processShapefile(const std::string& inputPath,
                      const std::string& outputPath,
                      double centerLon, double centerLat) {

    // 1. 读取Shapefile并创建空间对象
    std::vector<ShapefileSpatialItem> items = readShapefile(inputPath);

    // 2. 计算世界包围盒
    auto worldBounds = computeWorldBounds(items);

    // 3. 配置并构建四叉树
    spatial::strategy::QuadtreeConfig treeConfig;
    treeConfig.maxDepth = 10;
    treeConfig.maxItemsPerNode = 1000;

    spatial::strategy::QuadtreeStrategy<ShapefileSpatialItem> strategy(treeConfig);
    strategy.build(items, worldBounds);

    // 4. 配置内容生成器
    ContentGeneratorConfig contentConfig;
    contentConfig.outputRoot = outputPath;
    contentConfig.centerLongitude = centerLon;
    contentConfig.centerLatitude = centerLat;
    contentConfig.includeBatchTable = true;

    auto contentGenerator = std::make_shared<ShapefileContentGenerator>(contentConfig);

    // 5. 配置Tileset构建器
    spatial::builder::TilesetBuildConfig tilesetConfig;
    tilesetConfig.outputPath = outputPath;
    tilesetConfig.centerLongitude = centerLon;
    tilesetConfig.centerLatitude = centerLat;
    tilesetConfig.applyRootTransform = true;
    tilesetConfig.contentGenerator = contentGenerator;

    // 6. 构建Tileset
    tileset::Tileset tileset = spatial::builder::TilesetBuilder::build(
        strategy, tilesetConfig
    );

    // 7. 写入tileset.json
    tileset::TilesetWriter writer;
    writer.writeToFile(tileset, outputPath + "/tileset.json");
}
```

### 4.2 FBXPipeline完整流程

```cpp
// FBXPipeline.cpp (迁移后)
void FBXPipeline::run() {
    // 1. 加载FBX
    FBXLoader loader(settings.inputPath);
    loader.load();

    // 2. 创建空间对象
    std::vector<FBXSpatialItem> items = createSpatialItems(loader);

    // 3. 计算世界包围盒
    auto worldBounds = computeWorldBounds(items);

    // 4. 配置并构建八叉树
    spatial::strategy::OctreeConfig treeConfig;
    treeConfig.maxDepth = settings.maxDepth;
    treeConfig.maxItemsPerNode = settings.maxItemsPerTile;

    spatial::strategy::OctreeStrategy<FBXSpatialItem> strategy(treeConfig);
    strategy.build(items, worldBounds);

    // 5. 配置内容生成器 (支持LOD)
    ContentGeneratorConfig contentConfig;
    contentConfig.outputRoot = settings.outputPath;
    contentConfig.centerLongitude = settings.longitude;
    contentConfig.centerLatitude = settings.latitude;
    contentConfig.centerHeight = settings.height;
    contentConfig.enableDraco = settings.enableDraco;
    contentConfig.enableLOD = settings.enableLOD;

    auto contentGenerator = std::make_shared<FBXContentGenerator>(
        contentConfig, &loader
    );

    // 6. 配置Tileset构建器
    spatial::builder::TilesetBuildConfig tilesetConfig;
    tilesetConfig.outputPath = settings.outputPath;
    tilesetConfig.centerLongitude = settings.longitude;
    tilesetConfig.centerLatitude = settings.latitude;
    tilesetConfig.centerHeight = settings.height;
    tilesetConfig.applyRootTransform = true;
    tilesetConfig.contentGenerator = contentGenerator;

    // 7. 构建Tileset
    tileset::Tileset tileset = spatial::builder::TilesetBuilder::build(
        strategy, tilesetConfig
    );

    // 8. 写入tileset.json
    tileset::TilesetWriter writer;
    writer.writeToFile(tileset, settings.outputPath + "/tileset.json");
}
```

---

## 5. 关键对接代码实现

### 5.1 TilesetBuilder完整实现

```cpp
// spatial/builder/tileset_builder.h
#pragma once

#include "../core/slicing_strategy.h"
#include "../../tileset/tileset_types.h"
#include "../../tileset/tileset_writer.h"
#include "../../tileset/bounding_volume.h"
#include <functional>
#include <memory>

namespace spatial::builder {

/**
 * @brief 内容生成器回调类型
 */
using ContentGenerator = std::function<std::string(
    const void* node,                                    // 空间索引节点
    const std::vector<std::reference_wrapper<void>>& items,  // 空间对象
    const std::string& outputPath                        // 输出路径
)>;

/**
 * @brief Tileset构建配置
 */
struct TilesetBuildConfig {
    // 输出配置
    std::string outputPath;
    std::string contentPrefix = "tile";

    // 地理坐标配置
    double centerLongitude = 0.0;
    double centerLatitude = 0.0;
    double centerHeight = 0.0;

    // 包围体配置
    double boundingVolumeScaleFactor = 2.0;
    double geometricErrorScale = 0.5;
    double minHeight = 0.0;      // shp23dtile特有
    double maxHeight = 100.0;    // shp23dtile特有

    // Transform配置
    bool applyRootTransform = true;

    // 内容生成器
    ContentGenerator contentGenerator;

    // 内容生成条件
    size_t minItemsForContent = 1;   // 最小对象数才生成内容
    size_t maxDepthForContent = 100; // 最大深度才生成内容
};

/**
 * @brief Tileset构建器
 */
class TilesetBuilder {
public:
    /**
     * @brief 从空间策略构建Tileset
     */
    template<typename StrategyType>
    static tileset::Tileset build(
        const StrategyType& strategy,
        const TilesetBuildConfig& config
    ) {
        // 构建根Tile
        tileset::Tile rootTile = buildTileRecursive(
            strategy,
            strategy.getRootNode(),
            config.outputPath,
            "0",
            true,  // isRoot
            config
        );

        // 创建Tileset
        tileset::Tileset tileset(rootTile);
        tileset.setVersion("1.0");
        tileset.setGltfUpAxis("Z");
        tileset.updateGeometricError();

        return tileset;
    }

private:
    template<typename StrategyType>
    static tileset::Tile buildTileRecursive(
        const StrategyType& strategy,
        const void* node,
        const std::string& parentPath,
        const std::string& treePath,
        bool isRoot,
        const TilesetBuildConfig& config
    ) {
        using ItemType = typename StrategyType::ItemType;

        // 获取节点信息
        auto bounds = strategy.getNodeBounds(node);
        auto items = strategy.getNodeItems(node);
        bool isLeaf = strategy.isLeafNode(node);

        // 创建包围体
        tileset::BoundingVolume boundingVolume = createBoundingVolume(
            bounds, config, StrategyType::Dimension
        );

        // 计算几何误差
        double geometricError = computeGeometricError(bounds, config.geometricErrorScale);

        // 创建Tile
        tileset::Tile tile(boundingVolume, geometricError);

        // 应用根Transform
        if (isRoot && config.applyRootTransform) {
            auto transform = createRootTransform(config);
            if (transform) {
                tile.setTransform(*transform);
            }
        }

        // 生成内容
        if (shouldGenerateContent(items, isLeaf, config)) {
            std::string nodePath = parentPath + "/" + config.contentPrefix + "_" + treePath;
            std::filesystem::create_directories(nodePath);

            // 转换items为void*
            std::vector<std::reference_wrapper<void>> voidItems;
            for (auto& item : items) {
                voidItems.push_back(std::ref(static_cast<void&>(item.get())));
            }

            std::string contentUri = config.contentGenerator(node, voidItems, nodePath);
            if (!contentUri.empty()) {
                tile.setContent(contentUri);
            }
        }

        // 递归构建子节点
        if (!isLeaf) {
            auto childNodes = strategy.getChildNodes(node);
            int childIndex = 0;
            for (const void* childNode : childNodes) {
                std::string childTreePath = treePath + "_" + std::to_string(childIndex);
                tileset::Tile childTile = buildTileRecursive(
                    strategy, childNode, parentPath, childTreePath,
                    false, config
                );
                tile.addChild(std::move(childTile));
                childIndex++;
            }
        }

        return tile;
    }

    // 辅助函数...
    static tileset::BoundingVolume createBoundingVolume(
        const auto& bounds,
        const TilesetBuildConfig& config,
        size_t dimension
    );

    static double computeGeometricError(const auto& bounds, double scale);

    static std::optional<tileset::TransformMatrix> createRootTransform(
        const TilesetBuildConfig& config
    );

    static bool shouldGenerateContent(
        const auto& items,
        bool isLeaf,
        const TilesetBuildConfig& config
    ) {
        if (items.empty()) return false;
        if (isLeaf) return true;
        return items.size() >= config.minItemsForContent;
    }
};

} // namespace spatial::builder
```

---

## 6. 输出文件结构

### 6.1 shp23dtile输出结构
```
output/
├── tileset.json              # 根tileset
└── tile/
    ├── 5/
    │   ├── 16/
    │   │   ├── 8/
    │   │   │   └── content.b3dm
    │   │   └── 9/
    │   │       └── content.b3dm
    │   └── 17/
    │       └── ...
    └── 6/
        └── ...
```

### 6.2 FBXPipeline输出结构 (带LOD)
```
output/
├── tileset.json              # 根tileset
└── tile/
    ├── 0_0/
    │   ├── content_lod0.b3dm  # 精细
    │   ├── content_lod1.b3dm  # 中等
    │   └── content_lod2.b3dm  # 粗糙
    ├── 0_1/
    │   └── ...
    └── ...
```

---

## 7. 注意事项

### 7.1 坐标系统
- **shp23dtile**: WGS84(度) → ENU(米) → ECEF (通过Transform)
- **FBXPipeline**: ENU(米) → ECEF (通过Transform)

### 7.2 内存管理
- 空间对象通过`reference_wrapper`传递，避免拷贝
- B3DM生成时合并几何体，减少DrawCall

### 7.3 错误处理
- 空节点跳过内容生成
- 文件写入失败记录日志但不中断流程
- 几何误差为0时设置默认值

### 7.4 性能优化
- 使用线程池并行生成B3DM
- 预分配GLTF buffer大小
- 批量写入文件

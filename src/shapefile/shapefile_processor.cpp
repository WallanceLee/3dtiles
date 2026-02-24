#include "shapefile_processor.h"
#include "shapefile_spatial_item_adapter.h"
#include "b3dm_content_generator.h"
#include "shapefile_tileset_adapter.h"
#include "../tileset/tileset_writer.h"
#include "../lod_pipeline.h"
#include "../extern.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <limits>

namespace shapefile {

// B3DM生成器实现包装类
class ShapefileProcessor::B3DMGeneratorImpl {
public:
    explicit B3DMGeneratorImpl(double centerLon, double centerLat)
        : generator_(centerLon, centerLat) {}

    std::string generate(
        const std::vector<const ShapefileSpatialItem*>& items,
        bool withHeight,
        bool enableSimplify,
        const std::optional<SimplificationParams>& simplifyParams,
        bool enableDraco,
        const std::optional<DracoCompressionParams>& dracoParams) {
        return generator_.generate(items, withHeight, enableSimplify, simplifyParams, enableDraco, dracoParams);
    }

private:
    B3DMContentGenerator generator_;
};

ShapefileProcessor::ShapefileProcessor(const ShapefileProcessorConfig& config)
    : config_(config) {
    // 初始化 B3DM 生成器
    b3dmGenerator_ = std::make_unique<B3DMGeneratorImpl>(
        config.centerLongitude,
        config.centerLatitude
    );
}

ShapefileProcessor::~ShapefileProcessor() = default;

ProcessingResult ShapefileProcessor::process() {
    ProcessingResult result;

    LOG_I("Stage4: Starting Shapefile processing...");

    // 步骤1: 加载数据
    if (!loadData()) {
        result.errorMessage = "Failed to load data";
        return result;
    }
    result.featureCount = dataPool_->size();
    LOG_I("Stage4: Loaded %zu features", result.featureCount);

    // 步骤2: 构建空间索引
    if (!buildSpatialIndex()) {
        result.errorMessage = "Failed to build spatial index";
        return result;
    }
    result.nodeCount = quadtreeIndex_->getNodeCount();
    LOG_I("Stage4: Built quadtree with %zu nodes", result.nodeCount);

    // 步骤3: 生成 B3DM 文件
    if (!generateB3DMFiles()) {
        result.errorMessage = "Failed to generate B3DM files";
        return result;
    }
    LOG_I("Stage4: Generated B3DM files");

    // 步骤4: 生成 Tileset
    if (!generateTileset()) {
        result.errorMessage = "Failed to generate tileset";
        return result;
    }
    LOG_I("Stage4: Generated tileset");

    result.success = true;
    result.tilesetPath = (std::filesystem::path(config_.outputPath) / "tileset.json").string();
    return result;
}

bool ShapefileProcessor::loadData() {
    dataPool_ = std::make_unique<ShapefileDataPool>();
    // 使用带几何数据加载的方法，传入中心点坐标用于ENU转换
    // 注意：这里传入的中心点可能是投影坐标，需要在加载后重新计算
    bool result = dataPool_->loadFromShapefileWithGeometry(
        config_.inputPath, config_.heightField,
        config_.centerLongitude, config_.centerLatitude
    );

    if (result) {
        // 从转换后的数据重新计算中心点（WGS84）
        auto worldBounds = dataPool_->computeWorldBounds();
        double centerLon = (worldBounds.minx + worldBounds.maxx) * 0.5;
        double centerLat = (worldBounds.miny + worldBounds.maxy) * 0.5;

        // 如果中心点变化较大，重新加载几何数据
        if (std::abs(centerLon - config_.centerLongitude) > 1.0 ||
            std::abs(centerLat - config_.centerLatitude) > 1.0) {
            LOG_I("Stage4: Recalculating geometry with corrected center: lon=%.6f, lat=%.6f",
                  centerLon, centerLat);

            // 使用正确的中心点重新加载数据
            dataPool_->clear();
            result = dataPool_->loadFromShapefileWithGeometry(
                config_.inputPath, config_.heightField,
                centerLon, centerLat
            );

            // 更新配置中的中心点
            config_.centerLongitude = centerLon;
            config_.centerLatitude = centerLat;

            // 重新初始化 B3DM 生成器
            b3dmGenerator_ = std::make_unique<B3DMGeneratorImpl>(centerLon, centerLat);
        }
    }

    return result;
}

bool ShapefileProcessor::buildSpatialIndex() {
    // 计算世界包围盒
    auto worldBounds = dataPool_->computeWorldBounds();
    spatial::core::SpatialBounds<double, 3> bounds3d(
        std::array<double, 3>{worldBounds.minx, worldBounds.miny, worldBounds.minHeight},
        std::array<double, 3>{worldBounds.maxx, worldBounds.maxy, worldBounds.maxHeight}
    );

    // 转换为空间项列表
    spatial::core::SpatialItemList spatialItems;
    for (const auto& item : dataPool_->getAllItems()) {
        auto adapter = std::make_shared<ShapefileSpatialItemAdapter>(item);
        spatialItems.push_back(adapter);
    }

    // 使用四叉树策略构建索引
    spatial::strategy::QuadtreeStrategy strategy;
    quadtreeIndex_ = strategy.buildIndex(spatialItems, bounds3d, config_.quadtreeConfig);

    return quadtreeIndex_ != nullptr;
}

bool ShapefileProcessor::generateB3DMFiles() {
    // 收集所有叶子节点
    std::vector<const spatial::core::SpatialIndexNode*> leaves;
    collectLeafNodes(quadtreeIndex_->getRootNode(), leaves);

    if (leaves.empty()) {
        LOG_E("Stage4: No leaf nodes found");
        return false;
    }

    LOG_I("Stage4: Processing %zu leaf nodes", leaves.size());

    // 为每个叶子节点生成 B3DM
    for (const auto* leaf : leaves) {
        std::string b3dmPath = generateB3DMForNode(leaf, config_.outputPath);
        if (b3dmPath.empty()) {
            LOG_W("Stage4: Failed to generate B3DM for leaf node");
        }
    }

    return true;
}

std::string ShapefileProcessor::generateB3DMForNode(
    const spatial::core::SpatialIndexNode* node,
    const std::string& outputDir) {

    if (!node) {
        return "";
    }

    // 获取节点中的所有要素
    auto items = node->getItems();
    if (items.empty()) {
        return "";
    }

    // 转换为 ShapefileSpatialItem 指针列表
    std::vector<const ShapefileSpatialItem*> shapefileItems;
    shapefileItems.reserve(items.size());

    for (const auto& itemRef : items) {
        auto* adapter = dynamic_cast<const ShapefileSpatialItemAdapter*>(itemRef.get());
        if (adapter) {
            shapefileItems.push_back(adapter->getItem());
        }
    }

    if (shapefileItems.empty()) {
        return "";
    }

    // 获取节点坐标（优先使用四叉树坐标）
    int z, x, y;
    auto* qtNode = dynamic_cast<const spatial::strategy::QuadtreeNode*>(node);
    if (qtNode) {
        auto coord = qtNode->getCoord();
        z = coord.z;
        x = coord.x;
        y = coord.y;
    } else {
        // 回退：使用深度和包围盒中心计算
        auto bounds = node->getBounds();
        z = static_cast<int>(node->getDepth());
        double centerX = (bounds.min()[0] + bounds.max()[0]) * 0.5;
        double centerY = (bounds.min()[1] + bounds.max()[1]) * 0.5;
        x = static_cast<int>(centerX * 1000) % 1000;
        y = static_cast<int>(centerY * 1000) % 1000;
    }

    // 构建输出路径
    std::filesystem::path b3dmDir = std::filesystem::path(outputDir) /
                                    "tile" /
                                    std::to_string(z) /
                                    std::to_string(x) /
                                    std::to_string(y);
    std::filesystem::create_directories(b3dmDir);

    // 配置 LOD 级别
    std::vector<LODLevelSettings> lodLevels;

    if (config_.enableLOD) {
        // 使用默认 LOD 配置: [1.0, 0.5, 0.25]
        std::vector<float> ratios = {1.0f, 0.5f, 0.25f};
        float base_error = 0.01f;
        bool draco_for_lod0 = false;

        lodLevels = build_lod_levels(
            ratios,
            base_error,
            config_.simplifyParams,
            config_.dracoParams,
            draco_for_lod0
        );
    } else {
        // 只生成 LOD0
        LODLevelSettings level;
        level.target_ratio = 1.0f;
        level.target_error = config_.simplifyParams.target_error;
        level.enable_simplification = config_.enableSimplification;
        level.enable_draco = config_.enableDraco;
        level.simplify = config_.simplifyParams;
        level.draco = config_.dracoParams;
        lodLevels.push_back(level);
    }

    // 生成每个 LOD 级别的 B3DM
    std::vector<std::string> generatedFiles;

    for (size_t i = 0; i < lodLevels.size(); ++i) {
        const auto& level = lodLevels[i];

        // 构建文件名
        std::string filename = "content_lod" + std::to_string(i) + ".b3dm";
        std::filesystem::path b3dmPath = b3dmDir / filename;

        // 配置简化参数
        std::optional<SimplificationParams> simplifyOpt = std::nullopt;
        if (level.enable_simplification) {
            simplifyOpt = level.simplify;
            simplifyOpt->target_ratio = level.target_ratio;
            simplifyOpt->target_error = level.target_error;
        }

        // 配置 Draco 参数
        std::optional<DracoCompressionParams> dracoOpt = std::nullopt;
        if (level.enable_draco) {
            dracoOpt = level.draco;
            dracoOpt->enable_compression = true;
        }

        // 生成 B3DM
        std::string b3dmData = b3dmGenerator_->generate(
            shapefileItems,
            true,  // withHeight
            level.enable_simplification,
            simplifyOpt,
            level.enable_draco,
            dracoOpt
        );

        if (b3dmData.empty()) {
            LOG_E("Stage4: Failed to generate B3DM data for LOD %zu", i);
            continue;
        }

        // 写入文件
        std::ofstream file(b3dmPath, std::ios::binary);
        if (!file) {
            LOG_E("Stage4: Failed to open B3DM file for writing: %s", b3dmPath.string().c_str());
            continue;
        }

        file.write(b3dmData.data(), b3dmData.size());
        file.close();

        generatedFiles.push_back(filename);
        LOG_I("Stage4: Generated %s (ratio: %.2f)", filename.c_str(), level.target_ratio);
    }

    if (generatedFiles.empty()) {
        LOG_E("Stage4: No B3DM files were generated");
        return "";
    }

    // 返回第一个文件的相对路径（用于兼容）
    std::filesystem::path relPath = std::filesystem::path("tile") /
                                    std::to_string(z) /
                                    std::to_string(x) /
                                    std::to_string(y) /
                                    generatedFiles[0];

    return relPath.generic_string();
}

void ShapefileProcessor::collectLeafNodes(
    const spatial::core::SpatialIndexNode* node,
    std::vector<const spatial::core::SpatialIndexNode*>& leaves) {

    if (!node) return;

    if (node->isLeaf()) {
        if (node->getItemCount() > 0) {
            leaves.push_back(node);
        }
        return;
    }

    // 递归收集子节点
    auto children = node->getChildren();
    for (const auto* child : children) {
        if (child) {
            collectLeafNodes(child, leaves);
        }
    }
}

bool ShapefileProcessor::generateTileset() {
    // 构建节点映射表
    buildNodeMap();

    if (nodeMap_.empty()) {
        LOG_E("Stage4: Node map is empty");
        return false;
    }

    // 找到根节点
    uint64_t rootKey = 0;
    const TileMeta* rootMeta = nullptr;

    for (const auto& [key, meta] : nodeMap_) {
        if (meta.z == 0 || (rootMeta == nullptr)) {
            rootKey = key;
            rootMeta = &meta;
        }
    }

    if (!rootMeta) {
        LOG_E("Stage4: No root node found");
        return false;
    }

    // 为 LOD 场景的叶子节点生成子 tileset
    if (config_.enableLOD) {
        for (const auto& [key, meta] : nodeMap_) {
            if (meta.is_leaf) {
                if (!generateLeafTileset(meta)) {
                    LOG_E("Stage4: Failed to generate tileset for leaf node %d/%d/%d",
                          meta.z, meta.x, meta.y);
                    return false;
                }
            }
        }
    }

    // 创建适配器配置
    AdapterConfig adapterConfig;
    adapterConfig.boundingVolumeScaleFactor = config_.boundingVolumeScaleFactor;
    adapterConfig.geometricErrorScale = config_.geometricErrorScale;
    adapterConfig.applyRootTransform = config_.applyRootTransform;
    adapterConfig.minZRoot = rootMeta->z;
    adapterConfig.enableLOD = config_.enableLOD;
    adapterConfig.lodLevelCount = config_.enableLOD ? 3 : 1;
    adapterConfig.lodErrorRatios = {1.0, 0.5, 0.25};

    // 创建适配器
    ShapefileTilesetAdapter adapter(config_.centerLongitude, config_.centerLatitude, adapterConfig);

    // 构建 Tileset
    tileset::Tileset tileset = adapter.buildTileset(*rootMeta, nodeMap_);
    tileset.setVersion("1.0");
    tileset.setGltfUpAxis("Z");

    // 写入文件
    std::filesystem::path tilesetPath = std::filesystem::path(config_.outputPath) / "tileset.json";
    tileset::TilesetWriter writer;

    return writer.writeToFile(tileset, tilesetPath.string());
}

bool ShapefileProcessor::generateLeafTileset(const TileMeta& meta) {
    using namespace tileset;

    // 创建叶子节点的 tileset（包含 LOD 层级结构）
    Tileset leafTileset;
    leafTileset.setVersion("1.0");
    leafTileset.setGltfUpAxis("Z");

    // 计算几何误差（基于包围盒）
    double geometricError = calculateGeometricError(meta.bbox);

    // 创建根 tile（对应 LOD0）
    Tile rootTile;
    rootTile.geometricError = geometricError;
    rootTile.refine = "REPLACE";

    // 设置包围盒（ENU 坐标系，相对于节点中心）
    double centerX = (meta.bbox.minx + meta.bbox.maxx) / 2.0;
    double centerY = (meta.bbox.miny + meta.bbox.maxy) / 2.0;
    double centerZ = (meta.bbox.minHeight + meta.bbox.maxHeight) / 2.0;

    // 将 WGS84 包围盒转换为 ENU 米
    double spanX = (meta.bbox.maxx - meta.bbox.minx) * M_PI / 180.0 * 6378137.0 * std::cos(centerY * M_PI / 180.0);
    double spanY = (meta.bbox.maxy - meta.bbox.miny) * M_PI / 180.0 * 6378137.0;
    double spanZ = meta.bbox.maxHeight - meta.bbox.minHeight;

    // 创建 Box 包围体
    std::array<double, 12> boxValues = {
        0.0, 0.0, centerZ,           // center
        spanX / 2.0, 0.0, 0.0,       // x half-axis
        0.0, spanY / 2.0, 0.0,       // y half-axis
        0.0, 0.0, spanZ / 2.0        // z half-axis
    };
    tileset::Box box(boxValues);
    rootTile.boundingVolume = box;

    // 创建 LOD 层级结构：LOD0 -> LOD1 -> LOD2
    std::vector<std::pair<std::string, double>> lodLevels = {
        {"content_lod0.b3dm", geometricError * 1.0},
        {"content_lod1.b3dm", geometricError * 0.5},
        {"content_lod2.b3dm", geometricError * 0.25}
    };

    // 构建层级结构
    Tile* currentParent = &rootTile;
    for (size_t i = 0; i < lodLevels.size(); ++i) {
        const auto& [content, geError] = lodLevels[i];

        Tile lodTile;
        lodTile.boundingVolume = box;
        lodTile.geometricError = geError;
        lodTile.refine = "REPLACE";
        lodTile.setContent(content);

        // 如果不是最后一个 LOD，需要继续添加子节点
        if (i < lodLevels.size() - 1) {
            currentParent->addChild(std::move(lodTile));
            currentParent = &currentParent->children.back();
        } else {
            // 最后一个 LOD，直接添加为叶子节点
            currentParent->addChild(std::move(lodTile));
        }
    }

    leafTileset.root = std::move(rootTile);
    leafTileset.updateGeometricError();

    // 写入文件
    std::filesystem::path tilesetDir = std::filesystem::path(config_.outputPath) /
                                       "tile" /
                                       std::to_string(meta.z) /
                                       std::to_string(meta.x) /
                                       std::to_string(meta.y);
    std::filesystem::path tilesetPath = tilesetDir / "tileset.json";

    TilesetWriter writer;
    return writer.writeToFile(leafTileset, tilesetPath.string());
}

void ShapefileProcessor::buildNodeMap() {
    nodeMap_.clear();

    // 从空间索引根节点开始递归构建
    if (quadtreeIndex_ && quadtreeIndex_->getRootNode()) {
        buildNodeMapRecursive(quadtreeIndex_->getRootNode(), nodeMap_);
    }

    // 从子节点更新父节点的高度范围（后序遍历）
    updateParentHeightsFromChildren();
}

void ShapefileProcessor::updateParentHeightsFromChildren() {
    LOG_I("Stage4: Updating parent heights from children, node count = %zu", nodeMap_.size());

    // 收集所有键值并按层级排序（从叶子到根）
    std::vector<uint64_t> sortedKeys;
    for (auto& [key, meta] : nodeMap_) {
        sortedKeys.push_back(key);
    }
    std::sort(sortedKeys.begin(), sortedKeys.end(), [this](const auto& a, const auto& b) {
        return nodeMap_[a].z > nodeMap_[b].z;  // 从大到小排序
    });

    // 从叶子节点开始，向上更新父节点的高度
    for (uint64_t childKey : sortedKeys) {
        auto& childMeta = nodeMap_[childKey];

        // 如果子节点使用了默认高度（0-100），跳过
        if (childMeta.bbox.minHeight == 0.0 && childMeta.bbox.maxHeight == 100.0) {
            continue;
        }

        // 找到所有包含此子节点的父节点并更新
        for (auto& [parentKey, parentMeta] : nodeMap_) {
            // 检查 parentMeta 是否包含 childKey 作为子节点
            auto it = std::find(parentMeta.children_keys.begin(), parentMeta.children_keys.end(), childKey);
            if (it != parentMeta.children_keys.end()) {
                // 如果父节点使用的是默认高度，直接替换
                if (parentMeta.bbox.minHeight == 0.0 && parentMeta.bbox.maxHeight == 100.0) {
                    parentMeta.bbox.minHeight = childMeta.bbox.minHeight;
                    parentMeta.bbox.maxHeight = childMeta.bbox.maxHeight;
                } else {
                    // 否则取最小值和最大值
                    parentMeta.bbox.minHeight = std::min(parentMeta.bbox.minHeight, childMeta.bbox.minHeight);
                    parentMeta.bbox.maxHeight = std::max(parentMeta.bbox.maxHeight, childMeta.bbox.maxHeight);
                }
            }
        }
    }
}

void ShapefileProcessor::buildNodeMapRecursive(
    const spatial::core::SpatialIndexNode* node,
    std::unordered_map<uint64_t, TileMeta>& nodes) {

    if (!node) return;

    // 尝试转换为 QuadtreeNode 以获取坐标
    auto* qtNode = dynamic_cast<const spatial::strategy::QuadtreeNode*>(node);

    // 创建 TileMeta
    TileMeta meta;

    if (qtNode) {
        // 使用四叉树坐标
        auto coord = qtNode->getCoord();
        meta.z = coord.z;
        meta.x = coord.x;
        meta.y = coord.y;
    } else {
        // 回退：使用深度和包围盒中心计算
        meta.z = static_cast<int>(node->getDepth());
        auto bounds = node->getBounds();
        double centerX = (bounds.min()[0] + bounds.max()[0]) * 0.5;
        double centerY = (bounds.min()[1] + bounds.max()[1]) * 0.5;
        meta.x = static_cast<int>(centerX * 1000) % 1000;
        meta.y = static_cast<int>(centerY * 1000) % 1000;
    }

    // 设置是否为叶子节点
    meta.is_leaf = node->isLeaf();

    // 设置 content URI
    // 对于叶子节点：
    //   - 如果启用了 LOD，指向 tileset.json（子 tileset 管理多个 LOD 级别）
    //   - 如果没有启用 LOD，直接指向 content_lod0.b3dm
    // 对于非叶子节点，指向 tileset.json
    if (node->isLeaf() && !config_.enableLOD) {
        // 非 LOD 场景：叶子节点直接指向 B3DM 文件
        meta.tileset_rel = (std::filesystem::path("tile") /
                           std::to_string(meta.z) /
                           std::to_string(meta.x) /
                           std::to_string(meta.y) /
                           "content_lod0.b3dm").generic_string();
    } else {
        // LOD 场景的叶子节点，或非叶子节点：指向 tileset.json
        meta.tileset_rel = (std::filesystem::path("tile") /
                           std::to_string(meta.z) /
                           std::to_string(meta.x) /
                           std::to_string(meta.y) /
                           "tileset.json").generic_string();
    }

    // 编码键值
    uint64_t key = QuadtreeCoord(meta.z, meta.x, meta.y).encode();

    // 先递归处理子节点（后序遍历）
    if (!node->isLeaf()) {
        auto children = node->getChildren();
        for (const auto* child : children) {
            if (child) {
                // 为子节点获取 key
                uint64_t childKey;
                auto* childQtNode = dynamic_cast<const spatial::strategy::QuadtreeNode*>(child);
                if (childQtNode) {
                    auto childCoord = childQtNode->getCoord();
                    childKey = QuadtreeCoord(childCoord.z, childCoord.x, childCoord.y).encode();
                } else {
                    TileMeta childMeta;
                    childMeta.z = static_cast<int>(child->getDepth());
                    auto childBounds = child->getBounds();
                    double childCenterX = (childBounds.min()[0] + childBounds.max()[0]) * 0.5;
                    double childCenterY = (childBounds.min()[1] + childBounds.max()[1]) * 0.5;
                    childMeta.x = static_cast<int>(childCenterX * 1000) % 1000;
                    childMeta.y = static_cast<int>(childCenterY * 1000) % 1000;
                    childKey = QuadtreeCoord(childMeta.z, childMeta.x, childMeta.y).encode();
                }
                meta.children_keys.push_back(childKey);

                buildNodeMapRecursive(child, nodes);
            }
        }
    }

    // 计算包围盒（从子节点合并或从要素计算）
    if (node->isLeaf()) {
        // 叶子节点：从要素的实际包围盒合并
        bool firstItem = true;
        double minHeight = std::numeric_limits<double>::max();
        double maxHeight = std::numeric_limits<double>::lowest();

        auto items = node->getItems();
        for (const auto& itemRef : items) {
            auto* adapter = dynamic_cast<const ShapefileSpatialItemAdapter*>(itemRef.get());
            if (adapter) {
                const auto* item = adapter->getItem();
                if (item) {
                    if (firstItem) {
                        meta.bbox = item->bounds;
                        firstItem = false;
                    } else {
                        meta.bbox = mergeBBox(meta.bbox, item->bounds);
                    }
                    minHeight = std::min(minHeight, item->bounds.minHeight);
                    maxHeight = std::max(maxHeight, item->bounds.maxHeight);
                }
            }
        }

        // 如果没有要素，使用四叉树的空间范围作为回退
        if (firstItem) {
            auto bounds = node->getBounds();
            meta.bbox.minx = bounds.min()[0];
            meta.bbox.maxx = bounds.max()[0];
            meta.bbox.miny = bounds.min()[1];
            meta.bbox.maxy = bounds.max()[1];
            minHeight = 0.0;
            maxHeight = 100.0;
        }

        meta.bbox.minHeight = minHeight;
        meta.bbox.maxHeight = maxHeight;
    } else {
        // 非叶子节点：从子节点合并包围盒
        bool firstChild = true;
        for (uint64_t childKey : meta.children_keys) {
            auto it = nodes.find(childKey);
            if (it != nodes.end()) {
                const TileMeta& childMeta = it->second;
                if (firstChild) {
                    meta.bbox = childMeta.bbox;
                    firstChild = false;
                } else {
                    meta.bbox = mergeBBox(meta.bbox, childMeta.bbox);
                }
            }
        }
    }

    // 计算几何误差
    if (node->isLeaf()) {
        // 叶子节点：基于包围盒计算
        meta.geometric_error = calculateGeometricError(meta.bbox);
    } else {
        // 父节点：从子节点的最大几何误差计算
        double maxChildGE = 0.0;
        for (uint64_t childKey : meta.children_keys) {
            auto it = nodes.find(childKey);
            if (it != nodes.end()) {
                maxChildGE = std::max(maxChildGE, it->second.geometric_error);
            }
        }
        meta.geometric_error = maxChildGE * 2.0;
    }

    nodes[key] = std::move(meta);
}

TileBBox ShapefileProcessor::convertBounds(const spatial::core::SpatialBounds<double, 2>& bounds) {
    TileBBox bbox;
    bbox.minx = bounds.min()[0];
    bbox.maxx = bounds.max()[0];
    bbox.miny = bounds.min()[1];
    bbox.maxy = bounds.max()[1];
    bbox.minHeight = 0.0;
    bbox.maxHeight = 100.0;  // 默认高度，实际应从数据中获取
    return bbox;
}

double ShapefileProcessor::calculateGeometricError(const TileBBox& bbox) {
    // 使用与阶段3 (shp23dtile.cpp) 相同的计算方式
    double spanX = bbox.maxx - bbox.minx;
    double spanY = bbox.maxy - bbox.miny;
    double spanZ = bbox.maxHeight - bbox.minHeight;

    // 将经纬度跨度转换为米 (近似)
    // 使用与阶段3相同的公式：乘以 1.05 膨胀系数
    double centerLat = (bbox.miny + bbox.maxy) / 2.0;
    const double pi = std::acos(-1);
    double meterX = (spanX * pi / 180.0) * 1.05 * 6378137.0 * std::cos(centerLat * pi / 180.0);
    double meterY = (spanY * pi / 180.0) * 1.05 * 6378137.0;

    double maxSpan = std::max({meterX, meterY, spanZ});
    if (maxSpan <= 0.0) {
        return 0.0;
    }
    return maxSpan / 20.0;
}

} // namespace shapefile

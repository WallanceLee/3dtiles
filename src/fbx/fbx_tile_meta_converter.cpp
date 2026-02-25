/**
 * @file fbx/fbx_tile_meta_converter.cpp
 * @brief FBX瓦片元数据转换器实现
 */

#include "fbx_tile_meta_converter.h"
#include "../extern.h"
#include <filesystem>

namespace fbx {

// 辅助函数：将SpatialBounds转换为osg::BoundingBoxd
static osg::BoundingBoxd spatialBoundsToOSG(const spatial::core::SpatialBounds<double, 3>& bounds) {
    auto min = bounds.min();
    auto max = bounds.max();
    return osg::BoundingBoxd(min[0], min[1], min[2], max[0], max[1], max[2]);
}

// 辅助函数：计算节点在层级中的位置索引
static void computeNodePosition(const spatial::strategy::OctreeNode* node, int& x, int& y) {
    // 对于八叉树，我们使用简化的位置编码
    // 基于节点在父节点中的索引计算位置
    x = 0;
    y = 0;
    auto* parent = node->getParent();
    if (parent) {
        for (int i = 0; i < 8; ++i) {
            if (parent->getChild(i) == node) {
                x = i % 2;
                y = (i / 2) % 2;
                break;
            }
        }
    }
}

std::pair<FBXTileMetaPtr, FBXTileMetaMap> FBXTileMetaConverter::convert(
    const spatial::strategy::OctreeStrategy& strategy,
    const Config& config) {

    FBXTileMetaMap allMetas;
    int nodeIdCounter = 0;

    // OctreeStrategy没有getRootNode方法，需要通过其他方式获取根节点
    // 这里我们需要修改策略来暴露根节点，或者使用不同的方法
    // 暂时返回空，需要在FBXPipeline中直接处理

    return {nullptr, allMetas};
}

FBXTileMetaPtr FBXTileMetaConverter::convertNodeRecursive(
    const spatial::strategy::OctreeNode* node,
    FBXTileMetaMap& allMetas,
    const Config& config,
    int& nodeIdCounter) {

    if (!node) {
        return nullptr;
    }

    // 计算节点位置
    int x, y;
    computeNodePosition(node, x, y);

    // 创建TileCoord
    common::TileCoord coord(static_cast<int>(node->getDepth()), x, y);
    auto meta = std::make_shared<FBXTileMeta>(coord);

    // 设置包围盒
    auto bounds = spatialBoundsToOSG(node->getBounds3D());
    meta->setBoundingBox(bounds);

    // 计算几何误差
    meta->geometricError = computeGeometricError(bounds);

    // 判断是否为叶子节点
    meta->isLeaf = node->isLeaf();

    // 如果有内容，生成B3DM
    auto items = node->getItems();
    if (!items.empty()) {
        meta->hasGeometry = true;
        if (config.generator) {
            generateB3DMForNode(meta, node, config);
        }
    }

    // 递归处理子节点
    auto children = node->getChildren();
    for (const auto* child : children) {
        if (child) {
            auto childMeta = convertNodeRecursive(
                static_cast<const spatial::strategy::OctreeNode*>(child),
                allMetas, config, nodeIdCounter);
            if (childMeta) {
                meta->childrenKeys.push_back(childMeta->key());
            }
        }
    }

    // 保存到映射表
    allMetas[meta->key()] = meta;

    return meta;
}

void FBXTileMetaConverter::generateB3DMForNode(
    FBXTileMetaPtr& meta,
    const spatial::strategy::OctreeNode* node,
    const Config& config) {

    auto items = node->getItems();
    if (!config.generator || items.empty()) {
        return;
    }

    // 创建瓦片目录
    std::string tileDir = config.outputPath + "/" + meta->getTileDirectory();
    std::filesystem::create_directories(tileDir);

    // 转换空间项引用
    spatial::core::SpatialItemRefList spatialItems;
    for (const auto& item : items) {
        spatialItems.push_back(item);
    }

    if (spatialItems.empty()) {
        return;
    }

    // 生成LOD文件
    std::string tileName = "tile_" + std::to_string(node->getDepth()) + "_" +
                           std::to_string(meta->coord.x) + "_" + std::to_string(meta->coord.y);

    auto lodFiles = config.generator->generateLODFiles(
        spatialItems,
        tileDir,
        tileName,
        config.lodLevels
    );

    // 保存LOD文件路径
    for (const auto& file : lodFiles) {
        meta->lodFiles.push_back(meta->getTileDirectory() + "/" + file.filename);
    }

    // 设置内容URI（第一个LOD文件或子tileset）
    if (config.enableLOD && !meta->lodFiles.empty()) {
        // 启用LOD时，内容指向子tileset
        meta->content.uri = meta->getTilesetPath();
        meta->content.hasContent = true;
    } else if (!meta->lodFiles.empty()) {
        // 不启用LOD时，直接指向B3DM文件
        meta->content.uri = meta->lodFiles[0];
        meta->content.hasContent = true;
    }
}

double FBXTileMetaConverter::computeGeometricError(const osg::BoundingBoxd& bbox) {
    // 基于包围盒对角线计算几何误差
    double dx = bbox.xMax() - bbox.xMin();
    double dy = bbox.yMax() - bbox.yMin();
    double dz = bbox.zMax() - bbox.zMin();

    double diagonal = std::sqrt(dx * dx + dy * dy + dz * dz);
    return diagonal / 20.0;  // 与shapefile使用相同的比例
}

} // namespace fbx

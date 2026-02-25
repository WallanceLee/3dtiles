/**
 * @file common/tileset_builder.cpp
 * @brief 通用Tileset构建器实现
 */

#include "tileset_builder.h"
#include <extern.h>  // for degree2rad, longti_to_meter, lati_to_meter
#include <algorithm>
#include <cmath>

namespace common {

// ============================================================================
// TilesetBuilder 基类实现
// ============================================================================

TilesetBuilder::TilesetBuilder(const TilesetBuilderConfig& config)
    : config_(config) {}

tileset::Tileset TilesetBuilder::buildTileset(
    const TileMetaPtr& rootMeta,
    const TileMetaMap& allMetas,
    const BoundingBoxConverter& bboxConverter,
    const GeometricErrorCalculator& geCalculator) const {

    tileset::Tileset tileset;
    tileset.asset.version = config_.version;
    tileset.asset.gltfUpAxis = config_.gltfUpAxis;

    // 构建根节点
    tileset.root = buildTile(rootMeta, allMetas, bboxConverter, geCalculator);

    // 设置根几何误差
    tileset.geometricError = tileset.root.geometricError * config_.rootGeometricErrorMultiplier;

    return tileset;
}

tileset::Tile TilesetBuilder::buildTile(
    const TileMetaPtr& meta,
    const TileMetaMap& allMetas,
    const BoundingBoxConverter& bboxConverter,
    const GeometricErrorCalculator& geCalculator) const {

    tileset::Tile tile;

    // 设置包围体
    tile.boundingVolume = bboxConverter(meta->bbox);

    // 设置几何误差
    tile.geometricError = geCalculator(meta->bbox);
    if (meta->geometricError > 0.0) {
        tile.geometricError = meta->geometricError;
    }

    // 设置细化策略
    tile.refine = config_.refine;

    // 设置内容
    if (meta->content.hasContent) {
        tile.content = createContent(meta);
    }

    // 递归构建子节点
    if (!meta->isLeaf && !meta->childrenKeys.empty()) {
        buildChildren(tile, meta, allMetas, bboxConverter, geCalculator);
    }

    return tile;
}

void TilesetBuilder::buildChildren(
    tileset::Tile& parentTile,
    const TileMetaPtr& parentMeta,
    const TileMetaMap& allMetas,
    const BoundingBoxConverter& bboxConverter,
    const GeometricErrorCalculator& geCalculator) const {

    for (uint64_t childKey : parentMeta->childrenKeys) {
        auto it = allMetas.find(childKey);
        if (it != allMetas.end()) {
            tileset::Tile childTile = buildTile(
                it->second, allMetas, bboxConverter, geCalculator);
            parentTile.addChild(std::move(childTile));
        }
    }
}

tileset::Content TilesetBuilder::createContent(const TileMetaPtr& meta) const {
    tileset::Content content;
    content.uri = meta->content.uri;
    return content;
}

tileset::TransformMatrix TilesetBuilder::createRootTransform(
    double centerLon, double centerLat, double centerHeight) {

    // ENU到ECEF的变换矩阵
    // 基于WGS84坐标系
    double lon = degree2rad(centerLon);
    double lat = degree2rad(centerLat);

    double cosLon = std::cos(lon);
    double sinLon = std::sin(lon);
    double cosLat = std::cos(lat);
    double sinLat = std::sin(lat);

    // 构建旋转矩阵（ENU到ECEF）
    tileset::TransformMatrix matrix;
    // 第一列: -sin(lon), cos(lon), 0
    matrix[0] = -sinLon;
    matrix[1] = cosLon;
    matrix[2] = 0.0;
    matrix[3] = 0.0;

    // 第二列: -sin(lat)*cos(lon), -sin(lat)*sin(lon), cos(lat)
    matrix[4] = -sinLat * cosLon;
    matrix[5] = -sinLat * sinLon;
    matrix[6] = cosLat;
    matrix[7] = 0.0;

    // 第三列: cos(lat)*cos(lon), cos(lat)*sin(lon), sin(lat)
    matrix[8] = cosLat * cosLon;
    matrix[9] = cosLat * sinLon;
    matrix[10] = sinLat;
    matrix[11] = 0.0;

    // 第四列: 平移（ECEF坐标）
    // 简化为ENU原点在ECEF中的位置
    matrix[12] = 0.0;
    matrix[13] = 0.0;
    matrix[14] = centerHeight;
    matrix[15] = 1.0;

    return matrix;
}

// ============================================================================
// QuadtreeTilesetBuilder 四叉树构建器实现
// ============================================================================

QuadtreeTilesetBuilder::QuadtreeTilesetBuilder(double globalCenterLon,
                                               double globalCenterLat,
                                               const TilesetBuilderConfig& config)
    : TilesetBuilder(config)
    , globalCenterLon_(globalCenterLon)
    , globalCenterLat_(globalCenterLat) {}

tileset::Tileset QuadtreeTilesetBuilder::buildTileset(
    const TileMetaPtr& rootMeta,
    const TileMetaMap& allMetas) const {

    // 创建转换函数
    auto bboxConverter = [this](const BoundingBox& bbox) {
        return this->convertBoundingBox(bbox);
    };

    auto geCalculator = [this](const BoundingBox& bbox) {
        return this->computeGeometricError(bbox);
    };

    return TilesetBuilder::buildTileset(rootMeta, allMetas, bboxConverter, geCalculator);
}

tileset::Box QuadtreeTilesetBuilder::convertBoundingBox(const BoundingBox& bbox) const {
    // 假设输入bbox是WGS84经纬度（度）
    // 需要转换为ENU坐标系（米）

    // 计算中心点经纬度
    double centerLon = bbox.centerX();
    double centerLat = bbox.centerY();

    // 计算经纬度跨度（弧度）
    double lonRadSpan = degree2rad(bbox.width());
    double latRadSpan = degree2rad(bbox.height());

    // 转换为米（ENU坐标系）
    double halfW = longti_to_meter(lonRadSpan * 0.5, degree2rad(centerLat)) *
                   config_.boundingVolumeScale;
    double halfH = lati_to_meter(latRadSpan * 0.5) *
                   config_.boundingVolumeScale;
    double halfZ = bbox.depth() * 0.5 * config_.boundingVolumeScale;

    // 计算相对于全局中心的ENU偏移
    double offsetX, offsetY;
    computeEnuOffset(centerLon, centerLat, offsetX, offsetY);

    // 计算Z轴中心点
    double centerZ = bbox.centerZ();

    // 创建Box（中心点 + 半轴长度）
    return tileset::Box::fromCenterAndHalfLengths(
        offsetX, offsetY, centerZ, halfW, halfH, halfZ);
}

double QuadtreeTilesetBuilder::computeGeometricError(const BoundingBox& bbox) const {
    // 基于包围盒对角线计算几何误差
    // 将经纬度跨度转换为米（近似）
    double centerLat = bbox.centerY();
    double meterX = longti_to_meter(degree2rad(bbox.width()), degree2rad(centerLat));
    double meterY = lati_to_meter(degree2rad(bbox.height()));
    double meterZ = bbox.depth();

    double maxSpan = std::max({meterX, meterY, meterZ});
    if (maxSpan <= 0.0) {
        return 1.0;  // 最小几何误差
    }

    return maxSpan * config_.childGeometricErrorMultiplier;
}

void QuadtreeTilesetBuilder::computeEnuOffset(double lon, double lat,
                                              double& offsetX, double& offsetY) const {
    // 计算相对于全局中心的ENU偏移（米）
    offsetX = longti_to_meter(degree2rad(lon - globalCenterLon_), degree2rad(globalCenterLat_));
    offsetY = lati_to_meter(degree2rad(lat - globalCenterLat_));
}

// ============================================================================
// OctreeTilesetBuilder 八叉树构建器实现
// ============================================================================

OctreeTilesetBuilder::OctreeTilesetBuilder(const TilesetBuilderConfig& config)
    : TilesetBuilder(config) {}

tileset::Tileset OctreeTilesetBuilder::buildTileset(
    const TileMetaPtr& rootMeta,
    const TileMetaMap& allMetas) const {

    // 创建转换函数
    auto bboxConverter = [this](const BoundingBox& bbox) {
        return this->convertBoundingBox(bbox);
    };

    auto geCalculator = [this](const BoundingBox& bbox) {
        return this->computeGeometricError(bbox);
    };

    return TilesetBuilder::buildTileset(rootMeta, allMetas, bboxConverter, geCalculator);
}

tileset::Box OctreeTilesetBuilder::convertBoundingBox(const BoundingBox& bbox) const {
    // FBX使用本地坐标系，直接转换为Box
    double centerX = bbox.centerX();
    double centerY = bbox.centerY();
    double centerZ = bbox.centerZ();

    double halfW = bbox.width() * 0.5 * config_.boundingVolumeScale;
    double halfH = bbox.height() * 0.5 * config_.boundingVolumeScale;
    double halfD = bbox.depth() * 0.5 * config_.boundingVolumeScale;

    return tileset::Box::fromCenterAndHalfLengths(
        centerX, centerY, centerZ, halfW, halfH, halfD);
}

double OctreeTilesetBuilder::computeGeometricError(const BoundingBox& bbox) const {
    // 基于包围盒最大边长计算几何误差
    double maxSpan = std::max({bbox.width(), bbox.height(), bbox.depth()});
    if (maxSpan <= 0.0) {
        return 1.0;  // 最小几何误差
    }

    return maxSpan * config_.childGeometricErrorMultiplier;
}

} // namespace common

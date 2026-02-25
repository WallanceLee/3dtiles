#pragma once

/**
 * @file shapefile/shapefile_tile_meta.h
 * @brief Shapefile瓦片元数据
 *
 * 继承自common::TileMeta，添加Shapefile特有的属性
 */

#include "../common/tile_meta.h"
#include "shapefile_tile.h"  // For TileBBox, QuadtreeCoord

namespace shapefile {

/**
 * @brief Shapefile瓦片元数据
 *
 * 继承通用TileMeta，添加WGS84经纬度信息
 */
class ShapefileTileMeta : public common::TileMeta {
public:
    // WGS84经纬度包围盒（度）
    TileBBox wgs84BBox;

    // 原始平面路径（向后兼容）
    std::string origTilesetRel;

    // 子节点最大几何误差（聚合时使用）
    double maxChildGe = 0.0;

    ShapefileTileMeta() = default;

    /**
     * @brief 从QuadtreeCoord构造
     */
    explicit ShapefileTileMeta(const QuadtreeCoord& coord) {
        this->coord = common::TileCoord(coord.z, coord.x, coord.y);
    }

    /**
     * @brief 从TileMeta构造（用于向上转型）
     */
    explicit ShapefileTileMeta(const common::TileMeta& base)
        : common::TileMeta(base) {}

    /**
     * @brief 获取四叉树坐标
     */
    QuadtreeCoord getQuadtreeCoord() const {
        return QuadtreeCoord(coord.z, coord.x, coord.y);
    }

    /**
     * @brief 从TileBBox设置包围盒
     */
    void setFromTileBBox(const TileBBox& tbbox) {
        wgs84BBox = tbbox;
        // 同时设置基类的bbox（使用度作为单位，后续会转换）
        bbox = common::BoundingBox(
            tbbox.minx, tbbox.maxx,
            tbbox.miny, tbbox.maxy,
            tbbox.minHeight, tbbox.maxHeight
        );
    }

    /**
     * @brief 转换为TileBBox
     */
    TileBBox toTileBBox() const {
        return wgs84BBox;
    }
};

/**
 * @brief Shapefile瓦片元数据指针类型
 */
using ShapefileTileMetaPtr = std::shared_ptr<ShapefileTileMeta>;

/**
 * @brief 从通用TileMeta转换为ShapefileTileMeta
 */
inline ShapefileTileMetaPtr toShapefileTileMeta(const common::TileMetaPtr& meta) {
    return std::dynamic_pointer_cast<ShapefileTileMeta>(meta);
}

} // namespace shapefile

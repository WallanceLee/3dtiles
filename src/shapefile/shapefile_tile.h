#pragma once

/**
 * @file shapefile_tile.h
 * @brief Shapefile 业务逻辑层数据结构
 *
 * 该模块包含 Shapefile 特有的业务数据结构，与 3D Tiles 标准无关。
 * 主要责任：
 * 1. 管理 Shapefile 源数据的包围盒 (WGS84 经纬度)
 * 2. 管理四叉树空间索引 (z/x/y)
 * 3. 管理构建过程中的临时状态
 *
 * 坐标系统：
 * - 所有几何坐标使用 WGS84 经纬度 (度)
 * - 高度使用米
 */

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "../common/tile_path_utils.h"

namespace shapefile {

/**
 * @brief Shapefile 瓦片包围盒 (WGS84 坐标系)
 *
 * 使用度作为经纬度单位，这是 Shapefile 源数据的原始坐标系。
 * 注意：这不是 3D Tiles 标准的包围体，需要转换后才能使用。
 */
struct TileBBox {
    double minx = 0.0;      // 最小经度 (度)
    double maxx = 0.0;      // 最大经度 (度)
    double miny = 0.0;      // 最小纬度 (度)
    double maxy = 0.0;      // 最大纬度 (度)
    double minHeight = 0.0; // 最小高度 (米)
    double maxHeight = 0.0; // 最大高度 (米)

    // 默认构造函数
    TileBBox() = default;

    // 从角点构造
    TileBBox(double minx_deg, double maxx_deg, double miny_deg, double maxy_deg,
             double min_h, double max_h)
        : minx(minx_deg), maxx(maxx_deg), miny(miny_deg), maxy(maxy_deg),
          minHeight(min_h), maxHeight(max_h) {}

    // 获取中心点经度
    double centerLon() const { return (minx + maxx) * 0.5; }

    // 获取中心点纬度
    double centerLat() const { return (miny + maxy) * 0.5; }

    // 获取宽度 (度)
    double widthDeg() const { return maxx - minx; }

    // 获取高度 (度)
    double heightDeg() const { return maxy - miny; }

    // 判断是否有效
    bool isValid() const {
        return minx < maxx && miny < maxy && minHeight <= maxHeight;
    }
};

/**
 * @brief 四叉树坐标
 *
 * 用于标识瓦片在四叉树中的位置
 */
struct QuadtreeCoord {
    int z = 0;  // 层级
    int x = 0;  // X 坐标
    int y = 0;  // Y 坐标

    QuadtreeCoord() = default;
    QuadtreeCoord(int level, int x_coord, int y_coord)
        : z(level), x(x_coord), y(y_coord) {}

    // 编码为 64 位整数 (用于哈希表键值)
    uint64_t encode() const {
        return (static_cast<uint64_t>(z) << 42) |
               (static_cast<uint64_t>(x) << 21) |
               static_cast<uint64_t>(y);
    }

    // 从编码解码
    static QuadtreeCoord decode(uint64_t key) {
        QuadtreeCoord coord;
        coord.z = static_cast<int>((key >> 42) & 0x1FFFFF);
        coord.x = static_cast<int>((key >> 21) & 0x1FFFFF);
        coord.y = static_cast<int>(key & 0x1FFFFF);
        return coord;
    }

    // 获取父节点坐标
    QuadtreeCoord parent() const {
        if (z <= 0) return *this;
        return QuadtreeCoord(z - 1, x / 2, y / 2);
    }

    // 判断两个坐标是否相等
    bool operator==(const QuadtreeCoord& other) const {
        return z == other.z && x == other.x && y == other.y;
    }
};

/**
 * @brief Shapefile 瓦片元数据
 *
 * 包含瓦片的所有业务信息，用于构建 3D Tiles 层次结构。
 * 注意：这是构建时的临时数据结构，最终会转换为 tileset::Tile
 */
struct TileMeta {
    // 四叉树坐标 (保持与旧代码兼容的直接访问)
    int z = 0;  // 层级
    int x = 0;  // X 坐标
    int y = 0;  // Y 坐标

    TileBBox bbox;                 // 包围盒 (WGS84 度)
    double geometric_error = 0.0;  // 几何误差
    std::string tileset_rel;       // tileset.json 相对输出根目录的路径
    std::string orig_tileset_rel;  // 原始平面路径 (tile/z/x/y.json)
    bool is_leaf = false;          // 是否为叶子节点
    std::vector<uint64_t> children_keys;  // 子节点编码键值
    double max_child_ge = 0.0;     // 子节点最大几何误差 (聚合时使用)

    // 获取编码键值
    uint64_t key() const { return QuadtreeCoord(z, x, y).encode(); }
};

/**
 * @brief 合并两个包围盒
 */
inline TileBBox mergeBBox(const TileBBox& a, const TileBBox& b) {
    TileBBox r;
    r.minx = std::min(a.minx, b.minx);
    r.maxx = std::max(a.maxx, b.maxx);
    r.miny = std::min(a.miny, b.miny);
    r.maxy = std::max(a.maxy, b.maxy);
    r.minHeight = std::min(a.minHeight, b.minHeight);
    r.maxHeight = std::max(a.maxHeight, b.maxHeight);
    return r;
}

/**
 * @brief 生成瓦片路径
 *
 * 根据四叉树坐标生成 tileset.json 的相对路径
 * 使用公共模块的TilePathUtils实现统一路径格式
 *
 * @param coord 四叉树坐标
 * @param min_z 最小层级 (该层级及以下的瓦片放在根目录)
 * @return 相对路径，如 "tileset.json" 或 "tile/5/3/2/tileset.json"
 */
inline std::string tilesetPathForNode(const QuadtreeCoord& coord, int min_z) {
    // 使用公共模块的TilePathUtils
    common::TileCoord tileCoord(coord.z, coord.x, coord.y);
    std::string path = common::TilePathUtils::getTilesetPath(tileCoord);

    // 如果是最小层级，返回根目录的tileset.json
    if (coord.z <= min_z) {
        return "tileset.json";
    }

    return path;
}

} // namespace shapefile

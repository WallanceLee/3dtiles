#pragma once

/**
 * @file common/tile_meta.h
 * @brief 通用瓦片元数据基类
 *
 * 为不同数据源（Shapefile/FBX）提供统一的瓦片元数据接口
 */

#include "tile_path_utils.h"
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace common {

/**
 * @brief 通用包围盒结构
 *
 * 使用双精度浮点数表示3D空间中的包围盒
 */
struct BoundingBox {
    double minX = 0.0;
    double maxX = 0.0;
    double minY = 0.0;
    double maxY = 0.0;
    double minZ = 0.0;
    double maxZ = 0.0;

    BoundingBox() = default;
    BoundingBox(double minx, double maxx, double miny, double maxy,
                double minz, double maxz)
        : minX(minx), maxX(maxx), minY(miny), maxY(maxy),
          minZ(minz), maxZ(maxz) {}

    bool isValid() const {
        return minX < maxX && minY < maxY && minZ <= maxZ;
    }

    double centerX() const { return (minX + maxX) * 0.5; }
    double centerY() const { return (minY + maxY) * 0.5; }
    double centerZ() const { return (minZ + maxZ) * 0.5; }
    double width() const { return maxX - minX; }
    double height() const { return maxY - minY; }
    double depth() const { return maxZ - minZ; }
};

/**
 * @brief 瓦片内容信息
 */
struct TileContent {
    std::string uri;              // 内容文件URI（相对路径）
    std::string b3dmPath;         // B3DM文件路径
    bool hasContent = false;      // 是否有内容

    TileContent() = default;
    explicit TileContent(const std::string& u) : uri(u), hasContent(true) {}
};

/**
 * @brief 通用瓦片元数据基类
 *
 * 所有数据源的瓦片元数据都应继承此类
 */
class TileMeta {
public:
    TileCoord coord;              // 瓦片坐标
    BoundingBox bbox;             // 包围盒
    double geometricError = 0.0;  // 几何误差
    TileContent content;          // 内容信息
    bool isLeaf = false;          // 是否为叶子节点
    std::vector<uint64_t> childrenKeys;  // 子节点键值

    TileMeta() = default;
    explicit TileMeta(const TileCoord& c) : coord(c) {}

    virtual ~TileMeta() = default;

    // 获取唯一键值
    uint64_t key() const { return coord.encode(); }

    // 获取父节点键值
    uint64_t parentKey() const {
        if (coord.z <= 0) return 0;
        return TileCoord(coord.z - 1, coord.x / 2, coord.y / 2).encode();
    }

    // 判断是否为根节点
    bool isRoot() const { return coord.z == 0; }

    // 获取层级
    int getLevel() const { return coord.z; }

    // 获取瓦片路径（用于tileset.json中的uri）
    virtual std::string getTilesetPath() const {
        return TilePathUtils::getTilesetPath(coord);
    }

    // 获取内容路径
    virtual std::string getContentPath(int lodLevel = 0) const {
        return TilePathUtils::getContentPath(coord, lodLevel);
    }
};

/**
 * @brief 瓦片元数据指针类型
 */
using TileMetaPtr = std::shared_ptr<TileMeta>;

/**
 * @brief 瓦片元数据映射表
 */
using TileMetaMap = std::unordered_map<uint64_t, TileMetaPtr>;

} // namespace common

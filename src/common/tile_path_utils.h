#pragma once

/**
 * @file common/tile_path_utils.h
 * @brief 瓦片路径工具
 *
 * 统一输出目录结构 tile/{z}/{x}/{y}/
 */

#include <string>
#include <vector>
#include <cstdint>

namespace common {

/**
 * @brief 瓦片坐标结构
 */
struct TileCoord {
    int z = 0;  // 层级
    int x = 0;  // X坐标
    int y = 0;  // Y坐标

    TileCoord() = default;
    TileCoord(int level, int x_coord, int y_coord)
        : z(level), x(x_coord), y(y_coord) {}

    /**
     * @brief 编码为64位整数（用于哈希表键值）
     */
    uint64_t encode() const {
        return (static_cast<uint64_t>(z) << 42) |
               (static_cast<uint64_t>(x) << 21) |
               static_cast<uint64_t>(y);
    }

    /**
     * @brief 从编码解码
     */
    static TileCoord decode(uint64_t key) {
        TileCoord coord;
        coord.z = static_cast<int>((key >> 42) & 0x1FFFFF);
        coord.x = static_cast<int>((key >> 21) & 0x1FFFFF);
        coord.y = static_cast<int>(key & 0x1FFFFF);
        return coord;
    }

    bool operator==(const TileCoord& other) const {
        return z == other.z && x == other.x && y == other.y;
    }

    bool operator!=(const TileCoord& other) const {
        return !(*this == other);
    }
};

/**
 * @brief 八叉树坐标（FBX使用）
 */
struct OctreeCoord {
    int depth = 0;           // 深度
    int index = 0;           // 当前节点索引 (0-7)
    std::vector<int> path;   // 从根到当前节点的路径

    OctreeCoord() = default;
    OctreeCoord(int d, int idx, std::vector<int> p)
        : depth(d), index(idx), path(std::move(p)) {}

    /**
     * @brief 编码为64位整数（用于哈希表键值）
     */
    uint64_t encode() const {
        uint64_t key = 0;
        for (int p : path) {
            key = (key << 3) | (p & 0x7);
        }
        key = (key << 3) | (index & 0x7);
        key = (key << 8) | (depth & 0xFF);
        return key;
    }

    /**
     * @brief 转换为标准瓦片坐标
     */
    TileCoord toTileCoord() const;
};

/**
 * @brief 路径生成工具
 */
class TilePathUtils {
public:
    /**
     * @brief 获取tileset.json路径
     */
    static std::string getTilesetPath(const TileCoord& coord);

    /**
     * @brief 获取B3DM内容路径
     */
    static std::string getContentPath(const TileCoord& coord, int lodLevel = 0);

    /**
     * @brief 获取瓦片目录路径
     */
    static std::string getTileDirectory(const TileCoord& coord);

    /**
     * @brief 创建瓦片目录
     */
    static bool createTileDirectory(const std::string& outputRoot, const TileCoord& coord);

    /**
     * @brief 从八叉树坐标获取路径
     */
    static std::string getTilesetPath(const OctreeCoord& coord);
    static std::string getContentPath(const OctreeCoord& coord, int lodLevel = 0);
};

} // namespace common

#include "tile_path_utils.h"
#include <filesystem>

namespace common {

TileCoord OctreeCoord::toTileCoord() const {
    // 八叉树映射到瓦片坐标
    // x = 路径编码, y = 深度偏移
    int x = 0;
    for (int node : path) {
        x = x * 8 + node;
    }
    x = x * 8 + index;
    int y = depth > 1 ? depth - 2 : 0;
    return TileCoord(depth, x, y);
}

std::string TilePathUtils::getTilesetPath(const TileCoord& coord) {
    if (coord.z == 0) {
        return "tileset.json";
    }
    std::filesystem::path p = "tile";
    p /= std::to_string(coord.z);
    p /= std::to_string(coord.x);
    p /= std::to_string(coord.y);
    p /= "tileset.json";
    return p.generic_string();
}

std::string TilePathUtils::getContentPath(const TileCoord& coord, int lodLevel) {
    std::filesystem::path p = "tile";
    p /= std::to_string(coord.z);
    p /= std::to_string(coord.x);
    p /= std::to_string(coord.y);

    if (lodLevel == 0) {
        p /= "content.b3dm";
    } else {
        p /= "content_lod" + std::to_string(lodLevel) + ".b3dm";
    }
    return p.generic_string();
}

std::string TilePathUtils::getTileDirectory(const TileCoord& coord) {
    std::filesystem::path p = "tile";
    p /= std::to_string(coord.z);
    p /= std::to_string(coord.x);
    p /= std::to_string(coord.y);
    return p.generic_string();
}

bool TilePathUtils::createTileDirectory(const std::string& outputRoot, const TileCoord& coord) {
    std::filesystem::path p = outputRoot;
    p /= getTileDirectory(coord);

    try {
        std::filesystem::create_directories(p);
        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

std::string TilePathUtils::getTilesetPath(const OctreeCoord& coord) {
    return getTilesetPath(coord.toTileCoord());
}

std::string TilePathUtils::getContentPath(const OctreeCoord& coord, int lodLevel) {
    return getContentPath(coord.toTileCoord(), lodLevel);
}

} // namespace pipeline

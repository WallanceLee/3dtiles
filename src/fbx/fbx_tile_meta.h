#pragma once

/**
 * @file fbx/fbx_tile_meta.h
 * @brief FBX瓦片元数据
 *
 * 继承common::TileMeta，添加FBX特有的属性
 */

#include "../common/tile_meta.h"
#include <osg/BoundingBox>
#include <string>

namespace fbx {

/**
 * @brief FBX瓦片元数据
 *
 * 扩展通用TileMeta，添加FBX特有的包围盒和路径信息
 */
class FBXTileMeta : public common::TileMeta {
public:
    // FBX特有的包围盒（ENU坐标系，米）
    osg::BoundingBoxd bbox;

    // B3DM文件路径（相对于输出根目录）
    std::string b3dmPath;

    // 是否有实际几何内容
    bool hasGeometry = false;

    // LOD文件列表（如果启用了LOD）
    std::vector<std::string> lodFiles;

    FBXTileMeta() = default;
    explicit FBXTileMeta(const common::TileCoord& c) : common::TileMeta(c) {}

    /**
     * @brief 从osg::BoundingBoxd设置包围盒
     */
    void setBoundingBox(const osg::BoundingBoxd& box) {
        bbox = box;
        // 同时更新基类的BoundingBox (common::BoundingBox使用minX/maxX等命名)
        this->common::TileMeta::bbox.minX = box.xMin();
        this->common::TileMeta::bbox.maxX = box.xMax();
        this->common::TileMeta::bbox.minY = box.yMin();
        this->common::TileMeta::bbox.maxY = box.yMax();
        this->common::TileMeta::bbox.minZ = box.zMin();
        this->common::TileMeta::bbox.maxZ = box.zMax();
    }

    /**
     * @brief 获取瓦片目录路径（用于创建目录）
     */
    std::string getTileDirectory() const {
        return "tile/" + std::to_string(coord.z) + "/" +
               std::to_string(coord.x) + "/" + std::to_string(coord.y);
    }

    /**
     * @brief 获取子tileset的相对路径
     */
    std::string getTilesetPath() const override {
        return getTileDirectory() + "/tileset.json";
    }

    /**
     * @brief 获取B3DM文件的相对路径
     */
    std::string getB3DMPath(int lodLevel = 0) const {
        if (lodLevel >= 0 && lodLevel < static_cast<int>(lodFiles.size())) {
            return lodFiles[lodLevel];
        }
        return getTileDirectory() + "/content_lod" + std::to_string(lodLevel) + ".b3dm";
    }
};

using FBXTileMetaPtr = std::shared_ptr<FBXTileMeta>;
using FBXTileMetaMap = std::unordered_map<uint64_t, FBXTileMetaPtr>;

} // namespace fbx

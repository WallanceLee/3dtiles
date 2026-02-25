#pragma once

/**
 * @file shapefile/b3dm_content_generator.h
 * @brief Shapefile B3DM内容生成器
 *
 * 基于b3dm::B3DMGenerator的Shapefile专用包装器
 * 提供与旧代码兼容的API
 */

#include "shapefile_data_pool.h"
#include "geometry_extractor.h"
#include "../b3dm/b3dm_generator.h"
#include <osg/Geometry>
#include <vector>
#include <string>
#include <optional>
#include <memory>

namespace shapefile {

// 前向声明
class ShapefileSpatialItemAdapter;

/**
 * @brief B3DM内容生成器
 *
 * 基于pipeline::B3DMGenerator的Shapefile专用包装器
 */
class B3DMContentGenerator {
public:
    /**
     * @brief 构造内容生成器
     *
     * @param centerLon 全局中心经度（用于ENU坐标转换）
     * @param centerLat 全局中心纬度
     */
    B3DMContentGenerator(double centerLon, double centerLat);

    /**
     * @brief 生成B3DM内容
     *
     * @param items 空间对象指针列表（避免拷贝）
     * @param withHeight 是否包含高度属性
     * @param enableSimplify 是否启用简化
     * @param simplifyParams 简化参数
     * @param enableDraco 是否启用Draco压缩
     * @param dracoParams Draco参数
     * @return 生成的B3DM二进制数据，失败返回空字符串
     */
    std::string generate(
        const std::vector<const ShapefileSpatialItem*>& items,
        bool withHeight = true,
        bool enableSimplify = false,
        const std::optional<SimplificationParams>& simplifyParams = std::nullopt,
        bool enableDraco = false,
        const std::optional<DracoCompressionParams>& dracoParams = std::nullopt
    );

    /**
     * @brief 生成多LOD级别的B3DM文件
     *
     * @param items 空间对象指针列表
     * @param outputDir 输出目录
     * @param lodLevels LOD级别配置列表
     * @return 生成的文件信息列表
     */
    std::vector<b3dm::LODFileInfo> generateLODFiles(
        const std::vector<const ShapefileSpatialItem*>& items,
        const std::string& outputDir,
        const std::vector<LODLevelSettings>& lodLevels
    );

    /**
     * @brief 生成B3DM并写入文件
     *
     * @param items 空间对象指针列表
     * @param outputPath 输出文件路径
     * @return 是否成功
     */
    bool generateToFile(
        const std::vector<const ShapefileSpatialItem*>& items,
        const std::string& outputPath,
        bool withHeight = true
    );

private:
    double centerLon_;
    double centerLat_;

    // 几何体提取器
    GeometryExtractor geometryExtractor_;

    // 将ShapefileSpatialItem列表转换为适配器列表
    std::vector<std::shared_ptr<ShapefileSpatialItemAdapter>> convertToSpatialItems(
        const std::vector<const ShapefileSpatialItem*>& items
    );
};

} // namespace shapefile

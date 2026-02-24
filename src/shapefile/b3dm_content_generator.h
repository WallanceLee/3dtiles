#pragma once

#include "shapefile_data_pool.h"
#include "../b3dm/b3dm_writer.h"
#include "../mesh_processor.h"
#include <osg/Geometry>
#include <vector>
#include <string>
#include <optional>

namespace shapefile {

/**
 * @brief B3DM内容生成器
 *
 * 将Shapefile空间对象列表生成B3DM文件
 * 对应原代码中的 make_b3dm() 函数的新框架适配版本
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
     * @param outputPath 输出文件完整路径
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

    // 从ShapefileSpatialItem提取几何体
    std::vector<osg::ref_ptr<osg::Geometry>> extractGeometries(
        const std::vector<const ShapefileSpatialItem*>& items
    );

    // 构建BatchData
    b3dm::BatchData buildBatchData(
        const std::vector<const ShapefileSpatialItem*>& items,
        bool withHeight
    );
};

} // namespace shapefile

#pragma once

/**
 * @file osg/utils/geometry_utils.h
 * @brief OSG几何体工具类
 *
 * 从appendGeometryToModel提取的几何体处理逻辑
 * 包括：顶点变换、法线变换、索引处理
 */

#include <osg/Geometry>
#include <osg/Array>
#include <osg/PrimitiveSet>
#include <osg/BoundingBox>
#include <vector>
#include <cstdint>

namespace osg {
namespace utils {

/**
 * @brief 几何体工具类
 *
 * 处理OSG几何体到GLTF原始数据的转换
 */
class GeometryUtils {
public:
    /**
     * @brief 提取变换后的几何体数据
     *
     * 从OSG几何体提取顶点、法线、纹理坐标，并应用世界变换
     * 同时进行Y-up到Z-up的坐标转换
     *
     * @param geom OSG几何体
     * @param matrix 世界变换矩阵
     * @param normalMatrix 法线变换矩阵（逆转置）
     * @param outPositions 输出顶点位置（已变换）
     * @param outNormals 输出法线（已变换）
     * @param outTexcoords 输出纹理坐标
     * @param baseIndex 基础索引偏移
     * @return 提取的顶点数量
     */
    static size_t extractGeometryData(
        const osg::Geometry* geom,
        const osg::Matrixd& matrix,
        const osg::Matrixd& normalMatrix,
        std::vector<float>& outPositions,
        std::vector<float>& outNormals,
        std::vector<float>& outTexcoords,
        size_t baseIndex = 0
    );

    /**
     * @brief 处理索引数据
     *
     * 支持TRIANGLES、TRIANGLE_STRIP、TRIANGLE_FAN等多种图元类型
     *
     * @param ps OSG图元集
     * @param baseIndex 基础索引偏移
     * @param outIndices 输出索引
     * @return 处理的三角形数量
     */
    static size_t processPrimitiveSet(
        const osg::PrimitiveSet* ps,
        uint32_t baseIndex,
        std::vector<uint32_t>& outIndices
    );

    /**
     * @brief 计算法线变换矩阵（逆转置）
     *
     * 从世界矩阵计算法线变换矩阵，保持法线正交性
     *
     * @param matrix 世界变换矩阵
     * @return 法线变换矩阵
     */
    static osg::Matrixd computeNormalMatrix(const osg::Matrixd& matrix);

    /**
     * @brief 变换顶点（Y-up到Z-up）
     *
     * 应用世界变换并进行坐标系转换：
     * x' = x
     * y' = -z
     * z' = y
     *
     * @param vertex 原始顶点
     * @param matrix 世界变换矩阵
     * @return 变换后的顶点
     */
    static osg::Vec3d transformVertex(const osg::Vec3d& vertex, const osg::Matrixd& matrix);

    /**
     * @brief 变换法线（Y-up到Z-up）
     *
     * 应用法线变换矩阵并进行坐标系转换
     *
     * @param normal 原始法线
     * @param normalMatrix 法线变换矩阵
     * @return 变换后的法线
     */
    static osg::Vec3d transformNormal(const osg::Vec3d& normal, const osg::Matrixd& normalMatrix);

    /**
     * @brief 计算几何体包围盒（世界空间）
     *
     * @param geom OSG几何体
     * @param matrix 世界变换矩阵
     * @return 世界空间的包围盒
     */
    static osg::BoundingBoxd computeWorldBounds(
        const osg::Geometry* geom,
        const osg::Matrixd& matrix
    );
};

} // namespace utils
} // namespace osg

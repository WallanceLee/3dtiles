#pragma once

/**
 * @file geometry_converter.h
 * @brief OGR几何体到OSG几何体的转换器
 *
 * 将OGR Polygon转换为OSG Geometry，用于B3DM生成
 */

#include "shapefile_tile.h"
#include <osg/Geometry>
#include <ogr_geometry.h>

namespace shapefile {

/**
 * @brief 多边形网格数据结构
 *
 * 与原shp23dtile.cpp中的Polygon_Mesh对应
 */
struct PolygonMesh {
    std::string meshName;
    std::vector<std::array<float, 3>> vertices;
    std::vector<std::array<int, 3>> indices;
    std::vector<std::array<float, 3>> normals;

    bool isEmpty() const { return vertices.empty(); }
};

/**
 * @brief 几何体转换器
 *
 * 将OGR几何体转换为OSG Geometry
 */
class GeometryConverter {
public:
    /**
     * @brief 构造函数
     * @param centerLon 中心经度（用于本地坐标投影）
     * @param centerLat 中心纬度（用于本地坐标投影）
     */
    GeometryConverter(double centerLon, double centerLat);

    /**
     * @brief 将OGR Polygon转换为PolygonMesh
     * @param polygon OGR多边形
     * @param height 建筑高度
     * @return 转换后的网格
     */
    PolygonMesh convertPolygon(OGRPolygon* polygon, double height);

    /**
     * @brief 将PolygonMesh转换为OSG Geometry
     * @param mesh 多边形网格
     * @return OSG几何体
     */
    osg::ref_ptr<osg::Geometry> meshToGeometry(const PolygonMesh& mesh);

    /**
     * @brief 直接转换OGR Polygon为OSG Geometry
     * @param polygon OGR多边形
     * @param height 建筑高度
     * @return OSG几何体
     */
    osg::ref_ptr<osg::Geometry> convertToGeometry(OGRPolygon* polygon, double height);

private:
    double centerLon_;
    double centerLat_;

    // 坐标转换辅助函数
    void transformToWGS84(double& x, double& y, double& z);
    std::pair<double, double> projectToLocalMeters(double lon, double lat);
    void calcNormal(int baseCount, int pointNum, PolygonMesh& mesh);
};

} // namespace shapefile

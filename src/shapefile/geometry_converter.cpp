#include "geometry_converter.h"
#include "../coords/coordinate_transformer.h"
#include "../extern.h"

#include <numbers>
#include <osg/Array>
#include <osg/PrimitiveSet>
#include <osgUtil/DelaunayTriangulator>
#include <osgUtil/Tessellator>
#include <mapbox/earcut.hpp>

namespace shapefile {

GeometryConverter::GeometryConverter(double centerLon, double centerLat)
    : centerLon_(centerLon), centerLat_(centerLat) {}

void GeometryConverter::transformToWGS84(double& x, double& y, double& z) {
    // 如果坐标已经是WGS84，这里不需要转换
    // 如果需要从其他坐标系转换，在这里实现
    // 目前假设输入已经是WGS84经纬度
}

std::pair<double, double> GeometryConverter::projectToLocalMeters(double lon, double lat) {
    // 使用与原有代码相同的投影逻辑
    // 将WGS84经纬度转换为以centerLon_/centerLat_为中心的本地米坐标
    double x = (lon - centerLon_) * 111320.0 * std::cos(centerLat_ * std::numbers::pi / 180.0);
    double y = (lat - centerLat_) * 111320.0;
    return {x, y};
}

void GeometryConverter::calcNormal(int baseCount, int pointNum, PolygonMesh& mesh) {
    // 计算法线，与原有逻辑一致
    for (int i = 0; i < pointNum; i += 2) {
        float x1 = mesh.vertices[baseCount + 2 * i][0];
        float y1 = mesh.vertices[baseCount + 2 * i][1];
        float z1 = mesh.vertices[baseCount + 2 * i][2];
        float x2 = mesh.vertices[baseCount + 2 * (i + 1)][0];
        float y2 = mesh.vertices[baseCount + 2 * (i + 1)][1];
        float z2 = mesh.vertices[baseCount + 2 * (i + 1)][2];

        // 计算法线（简化版本）
        float nx = y2 - y1;
        float ny = -(x2 - x1);
        float nz = 0.0f;

        // 归一化
        float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len > 0) {
            nx /= len;
            ny /= len;
            nz /= len;
        }

        mesh.normals.push_back({nx, ny, nz});
        mesh.normals.push_back({nx, ny, nz});
    }
}

PolygonMesh GeometryConverter::convertPolygon(OGRPolygon* polygon, double height) {
    PolygonMesh mesh;

    if (!polygon) {
        return mesh;
    }

    OGRLinearRing* exteriorRing = polygon->getExteriorRing();
    if (!exteriorRing) {
        return mesh;
    }

    int pointNum = exteriorRing->getNumPoints();
    if (pointNum < 4) {  // 需要至少4个点（包括重复的起点/终点）
        return mesh;
    }

    // 处理外环 - 生成侧面墙体
    int vertexCount = 0;
    for (int i = 0; i < pointNum; i++) {
        OGRPoint pt;
        exteriorRing->getPoint(i, &pt);

        double x = pt.getX();
        double y = pt.getY();
        double bottom = pt.getZ();

        transformToWGS84(x, y, bottom);
        auto [localX, localY] = projectToLocalMeters(x, y);

        // 底部顶点
        mesh.vertices.push_back({static_cast<float>(localX), static_cast<float>(localY), static_cast<float>(bottom)});
        // 顶部顶点
        mesh.vertices.push_back({static_cast<float>(localX), static_cast<float>(localY), static_cast<float>(height)});

        // 重复顶点用于生成三角形
        if (i != 0 && i != pointNum - 1) {
            mesh.vertices.push_back({static_cast<float>(localX), static_cast<float>(localY), static_cast<float>(bottom)});
            mesh.vertices.push_back({static_cast<float>(localX), static_cast<float>(localY), static_cast<float>(height)});
        }
    }

    // 生成侧面索引
    int vertexNum = mesh.vertices.size() / 2;
    for (int i = 0; i < vertexNum; i += 2) {
        if (i != vertexNum - 1) {
            // 第一个三角形
            mesh.indices.push_back({2 * i, 2 * i + 1, 2 * (i + 1) + 1});
            // 第二个三角形
            mesh.indices.push_back({2 * (i + 1), 2 * i, 2 * (i + 1) + 1});
        }
    }

    calcNormal(0, vertexNum, mesh);
    vertexCount += 2 * vertexNum;

    // 处理内环（孔洞）
    int interiorCount = polygon->getNumInteriorRings();
    for (int j = 0; j < interiorCount; j++) {
        OGRLinearRing* interiorRing = polygon->getInteriorRing(j);
        if (!interiorRing) continue;

        int interiorPointNum = interiorRing->getNumPoints();
        if (interiorPointNum < 4) continue;

        int interiorVertexStart = mesh.vertices.size();

        for (int i = 0; i < interiorPointNum; i++) {
            OGRPoint pt;
            interiorRing->getPoint(i, &pt);

            double x = pt.getX();
            double y = pt.getY();
            double bottom = pt.getZ();

            transformToWGS84(x, y, bottom);
            auto [localX, localY] = projectToLocalMeters(x, y);

            mesh.vertices.push_back({static_cast<float>(localX), static_cast<float>(localY), static_cast<float>(bottom)});
            mesh.vertices.push_back({static_cast<float>(localX), static_cast<float>(localY), static_cast<float>(height)});

            if (i != 0 && i != interiorPointNum - 1) {
                mesh.vertices.push_back({static_cast<float>(localX), static_cast<float>(localY), static_cast<float>(bottom)});
                mesh.vertices.push_back({static_cast<float>(localX), static_cast<float>(localY), static_cast<float>(height)});
            }
        }

        int interiorVertexNum = (mesh.vertices.size() - interiorVertexStart) / 2;
        for (int i = 0; i < interiorVertexNum; i += 2) {
            if (i != interiorVertexNum - 1) {
                int base = interiorVertexStart / 2;
                mesh.indices.push_back({base + 2 * i, base + 2 * i + 1, base + 2 * (i + 1)});
                mesh.indices.push_back({base + 2 * (i + 1), base + 2 * i, base + 2 * (i + 1) + 1});
            }
        }

        calcNormal(interiorVertexStart / 2, interiorPointNum, mesh);
        vertexCount = mesh.vertices.size();
    }

    // 生成顶面和底面（使用earcut三角化）
    {
        using Point = std::array<double, 2>;
        std::vector<std::vector<Point>> polygonPoints;

        // 外环
        std::vector<Point> exteriorPoints;
        for (int i = 0; i < pointNum; i++) {
            OGRPoint pt;
            exteriorRing->getPoint(i, &pt);

            double x = pt.getX();
            double y = pt.getY();
            double bottom = pt.getZ();

            transformToWGS84(x, y, bottom);
            auto [localX, localY] = projectToLocalMeters(x, y);

            exteriorPoints.push_back({localX, localY});

            // 添加顶面和底面顶点
            mesh.vertices.push_back({static_cast<float>(localX), static_cast<float>(localY), static_cast<float>(bottom)});
            mesh.vertices.push_back({static_cast<float>(localX), static_cast<float>(localY), static_cast<float>(height)});
            mesh.normals.push_back({0, 0, -1});  // 底面法线
            mesh.normals.push_back({0, 0, 1});   // 顶面法线
        }
        polygonPoints.push_back(exteriorPoints);

        // 内环
        for (int j = 0; j < interiorCount; j++) {
            OGRLinearRing* interiorRing = polygon->getInteriorRing(j);
            if (!interiorRing) continue;

            int interiorPointNum = interiorRing->getNumPoints();
            std::vector<Point> interiorPoints;

            for (int i = 0; i < interiorPointNum; i++) {
                OGRPoint pt;
                interiorRing->getPoint(i, &pt);

                double x = pt.getX();
                double y = pt.getY();
                double bottom = pt.getZ();

                transformToWGS84(x, y, bottom);
                auto [localX, localY] = projectToLocalMeters(x, y);

                interiorPoints.push_back({localX, localY});
            }
            polygonPoints.push_back(interiorPoints);
        }

        // 使用earcut进行三角化
        std::vector<uint32_t> triIndices = mapbox::earcut<uint32_t>(polygonPoints);

        // 底面索引（逆时针）
        int baseVertex = vertexCount;
        for (size_t i = 0; i < triIndices.size(); i += 3) {
            mesh.indices.push_back({
                baseVertex + 2 * static_cast<int>(triIndices[i]),
                baseVertex + 2 * static_cast<int>(triIndices[i + 2]),
                baseVertex + 2 * static_cast<int>(triIndices[i + 1])
            });
        }

        // 顶面索引（顺时针）
        for (size_t i = 0; i < triIndices.size(); i += 3) {
            mesh.indices.push_back({
                baseVertex + 2 * static_cast<int>(triIndices[i]) + 1,
                baseVertex + 2 * static_cast<int>(triIndices[i + 1]) + 1,
                baseVertex + 2 * static_cast<int>(triIndices[i + 2]) + 1
            });
        }
    }

    return mesh;
}

osg::ref_ptr<osg::Geometry> GeometryConverter::meshToGeometry(const PolygonMesh& mesh) {
    if (mesh.isEmpty()) {
        return nullptr;
    }

    osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry();

    // 创建顶点数组
    osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array();
    vertices->reserve(mesh.vertices.size());
    for (const auto& v : mesh.vertices) {
        vertices->push_back(osg::Vec3(v[0], v[1], v[2]));
    }
    geometry->setVertexArray(vertices);

    // 创建法线数组
    if (!mesh.normals.empty()) {
        osg::ref_ptr<osg::Vec3Array> normals = new osg::Vec3Array();
        normals->reserve(mesh.normals.size());
        for (const auto& n : mesh.normals) {
            normals->push_back(osg::Vec3(n[0], n[1], n[2]));
        }
        geometry->setNormalArray(normals, osg::Array::BIND_PER_VERTEX);
    }

    // 创建索引数组
    osg::ref_ptr<osg::DrawElementsUInt> indices = new osg::DrawElementsUInt(osg::PrimitiveSet::TRIANGLES);
    indices->reserve(mesh.indices.size() * 3);
    for (const auto& tri : mesh.indices) {
        indices->push_back(tri[0]);
        indices->push_back(tri[1]);
        indices->push_back(tri[2]);
    }
    geometry->addPrimitiveSet(indices);

    return geometry;
}

osg::ref_ptr<osg::Geometry> GeometryConverter::convertToGeometry(OGRPolygon* polygon, double height) {
    PolygonMesh mesh = convertPolygon(polygon, height);
    return meshToGeometry(mesh);
}

} // namespace shapefile

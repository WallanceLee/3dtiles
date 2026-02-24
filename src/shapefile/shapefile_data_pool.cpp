#include "shapefile_data_pool.h"
#include "../extern.h"
#include <ogrsf_frmts.h>
#include <osg/Geometry>
#include <algorithm>
#include <limits>
#include <mapbox/earcut.hpp>

namespace shapefile {

TileBBox ShapefileDataPool::computeWorldBounds() const {
    if (items_.empty()) {
        return TileBBox();
    }

    TileBBox worldBounds = items_[0]->bounds;

    for (size_t i = 1; i < items_.size(); ++i) {
        const auto& bounds = items_[i]->bounds;
        worldBounds.minx = std::min(worldBounds.minx, bounds.minx);
        worldBounds.maxx = std::max(worldBounds.maxx, bounds.maxx);
        worldBounds.miny = std::min(worldBounds.miny, bounds.miny);
        worldBounds.maxy = std::max(worldBounds.maxy, bounds.maxy);
        worldBounds.minHeight = std::min(worldBounds.minHeight, bounds.minHeight);
        worldBounds.maxHeight = std::max(worldBounds.maxHeight, bounds.maxHeight);
    }

    return worldBounds;
}

// 辅助函数：将经纬度转换为ENU坐标（米）
static std::pair<double, double> lonlat_to_enu_meters(double lon, double lat, double centerLon, double centerLat) {
    const double pi = std::acos(-1.0);
    const double earthRadius = 6378137.0; // WGS84 赤道半径（米）

    // 经度方向：每度距离随纬度变化
    double cosLat = std::cos(centerLat * pi / 180.0);
    double meterPerDegreeLon = (pi / 180.0) * earthRadius * cosLat;
    double meterPerDegreeLat = (pi / 180.0) * earthRadius;

    // 计算相对于中心点的偏移（米）
    double x = (lon - centerLon) * meterPerDegreeLon;
    double y = (lat - centerLat) * meterPerDegreeLat;

    return {x, y};
}

// 辅助函数：计算法向量（与原始实现完全一致）
// 原始实现：每个点计算一个法线，然后给4个顶点使用
// 法线基于水平向量的垂直方向：(-y, x, 0)
static void calc_normal(int baseCnt, int ptNum, osg::Vec3Array* vertices, osg::Vec3Array* normals) {
    // 注意：原始实现中 baseCnt 是顶点索引，但在调用时传入的是 vertex_index / 2
    // 这里我们假设 baseCnt 是起始顶点索引（不是 /2 后的值）
    int vertexIdx = baseCnt;

    for (int i = 0; i < ptNum; i += 2) {
        // 计算水平向量：从当前点指向下一个点
        if (vertexIdx + 4 < vertices->size() && i + 1 < ptNum) {
            // 获取当前点和下一个点的XY坐标
            float x0 = (*vertices)[vertexIdx].x();
            float y0 = (*vertices)[vertexIdx].y();
            float x1 = (*vertices)[vertexIdx + 4].x();  // 下一个点的底部顶点
            float y1 = (*vertices)[vertexIdx + 4].y();

            // 水平向量
            float dx = x1 - x0;
            float dy = y1 - y0;

            // 垂直向量 (-dy, dx, 0)
            float nx = -dy;
            float ny = dx;
            float nz = 0.0f;

            // 归一化
            float len = std::sqrt(nx * nx + ny * ny);
            if (len > 0) {
                nx /= len;
                ny /= len;
            }

            // 为4个顶点添加相同的法线（2个底部 + 2个顶部）
            (*normals)[vertexIdx] = osg::Vec3(nx, ny, nz);
            (*normals)[vertexIdx + 1] = osg::Vec3(nx, ny, nz);
            (*normals)[vertexIdx + 2] = osg::Vec3(nx, ny, nz);
            (*normals)[vertexIdx + 3] = osg::Vec3(nx, ny, nz);
        }

        vertexIdx += 4;  // 移动到下一个点（4个顶点）
    }
}

// 辅助函数：从 OGRPolygon 创建 OSG 几何体（与原始实现一致）
static osg::ref_ptr<osg::Geometry> create_geometry_from_polygon(OGRPolygon* polygon, double height,
                                                                double centerLon, double centerLat,
                                                                bool xySwapped = false) {
    if (!polygon) return nullptr;

    OGRLinearRing* exteriorRing = polygon->getExteriorRing();
    int ptNum = exteriorRing->getNumPoints();
    if (ptNum < 4) {
        return nullptr;
    }

    osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
    osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
    osg::ref_ptr<osg::Vec3Array> normals = new osg::Vec3Array;
    osg::ref_ptr<osg::DrawElementsUInt> indices = new osg::DrawElementsUInt(osg::PrimitiveSet::TRIANGLES);

    int pt_count = 0;

    // ========== 1. 创建外环侧面 ==========
    for (int i = 0; i < ptNum; i++) {
        OGRPoint pt;
        exteriorRing->getPoint(i, &pt);
        double x = pt.getX();
        double y = pt.getY();
        double bottom = pt.getZ();

        // 如果坐标转换后 X/Y 交换，需要交换回来
        double lon = xySwapped ? y : x;
        double lat = xySwapped ? x : y;

        auto [point_x, point_y] = lonlat_to_enu_meters(lon, lat, centerLon, centerLat);

        // 每个点创建2个顶点（底部和顶部）
        vertices->push_back(osg::Vec3(point_x, point_y, bottom));
        vertices->push_back(osg::Vec3(point_x, point_y, height));
        normals->push_back(osg::Vec3(0, 0, 0)); // 占位，稍后计算
        normals->push_back(osg::Vec3(0, 0, 0));

        // 中间点重复添加（与原始实现一致）
        if (i != 0 && i != ptNum - 1) {
            vertices->push_back(osg::Vec3(point_x, point_y, bottom));
            vertices->push_back(osg::Vec3(point_x, point_y, height));
            normals->push_back(osg::Vec3(0, 0, 0));
            normals->push_back(osg::Vec3(0, 0, 0));
        }
    }

    // 创建侧面索引
    int vertex_num = vertices->size() / 2;
    for (int i = 0; i < vertex_num; i += 2) {
        if (i != vertex_num - 1) {
            indices->push_back(2 * i);
            indices->push_back(2 * i + 1);
            indices->push_back(2 * (i + 1) + 1);
            indices->push_back(2 * (i + 1));
            indices->push_back(2 * i);
            indices->push_back(2 * (i + 1) + 1);
        }
    }

    // 计算侧面法向量
    calc_normal(0, vertex_num, vertices.get(), normals.get());
    pt_count += 2 * vertex_num;

    // ========== 2. 创建内环侧面（洞） ==========
    int inner_count = polygon->getNumInteriorRings();
    for (int j = 0; j < inner_count; j++) {
        OGRLinearRing* interiorRing = polygon->getInteriorRing(j);
        int innerPtNum = interiorRing->getNumPoints();
        if (innerPtNum < 4) continue;

        int innerBase = vertices->size();

        for (int i = 0; i < innerPtNum; i++) {
            OGRPoint pt;
            interiorRing->getPoint(i, &pt);
            double x = pt.getX();
            double y = pt.getY();
            double bottom = pt.getZ();

            double lon = xySwapped ? y : x;
            double lat = xySwapped ? x : y;

            auto [point_x, point_y] = lonlat_to_enu_meters(lon, lat, centerLon, centerLat);

            vertices->push_back(osg::Vec3(point_x, point_y, bottom));
            vertices->push_back(osg::Vec3(point_x, point_y, height));
            normals->push_back(osg::Vec3(0, 0, 0));
            normals->push_back(osg::Vec3(0, 0, 0));

            if (i != 0 && i != innerPtNum - 1) {
                vertices->push_back(osg::Vec3(point_x, point_y, bottom));
                vertices->push_back(osg::Vec3(point_x, point_y, height));
                normals->push_back(osg::Vec3(0, 0, 0));
                normals->push_back(osg::Vec3(0, 0, 0));
            }
        }

        int innerVertexNum = (vertices->size() - innerBase) / 2;
        for (int i = 0; i < innerVertexNum; i += 2) {
            if (i != innerVertexNum - 1) {
                indices->push_back(innerBase + 2 * i);
                indices->push_back(innerBase + 2 * i + 1);
                indices->push_back(innerBase + 2 * (i + 1) + 1);
                indices->push_back(innerBase + 2 * (i + 1));
                indices->push_back(innerBase + 2 * i);
                indices->push_back(innerBase + 2 * (i + 1) + 1);
            }
        }

        calc_normal(innerBase / 2, innerVertexNum, vertices.get(), normals.get());
        pt_count = vertices->size();
    }

    // ========== 3. 创建顶面和底面（使用 earcut 三角剖分） ==========
    {
        using Point = std::array<double, 2>;
        std::vector<std::vector<Point>> earcutPolygon;

        // 底面和顶面的顶点起始索引
        int roofBaseIdx = vertices->size();

        // 外环顶点（用于 earcut）
        std::vector<Point> exteriorPoints;
        for (int i = 0; i < ptNum; i++) {
            OGRPoint pt;
            exteriorRing->getPoint(i, &pt);
            double x = pt.getX();
            double y = pt.getY();
            double bottom = pt.getZ();

            double lon = xySwapped ? y : x;
            double lat = xySwapped ? x : y;

            auto [point_x, point_y] = lonlat_to_enu_meters(lon, lat, centerLon, centerLat);
            exteriorPoints.push_back({point_x, point_y});

            // 添加底面顶点
            vertices->push_back(osg::Vec3(point_x, point_y, bottom));
            normals->push_back(osg::Vec3(0, 0, -1));

            // 添加顶面顶点
            vertices->push_back(osg::Vec3(point_x, point_y, height));
            normals->push_back(osg::Vec3(0, 0, 1));
        }
        earcutPolygon.push_back(exteriorPoints);

        // 内环（洞）
        for (int j = 0; j < inner_count; j++) {
            OGRLinearRing* interiorRing = polygon->getInteriorRing(j);
            int innerPtNum = interiorRing->getNumPoints();
            if (innerPtNum < 4) continue;

            std::vector<Point> interiorPoints;
            for (int i = 0; i < innerPtNum; i++) {
                OGRPoint pt;
                interiorRing->getPoint(i, &pt);
                double x = pt.getX();
                double y = pt.getY();
                double bottom = pt.getZ();

                double lon = xySwapped ? y : x;
                double lat = xySwapped ? x : y;

                auto [point_x, point_y] = lonlat_to_enu_meters(lon, lat, centerLon, centerLat);
                interiorPoints.push_back({point_x, point_y});

                // 添加底面顶点
                vertices->push_back(osg::Vec3(point_x, point_y, bottom));
                normals->push_back(osg::Vec3(0, 0, -1));

                // 添加顶面顶点
                vertices->push_back(osg::Vec3(point_x, point_y, height));
                normals->push_back(osg::Vec3(0, 0, 1));
            }
            earcutPolygon.push_back(interiorPoints);
        }

        // 使用 earcut 进行三角剖分
        std::vector<int> earcutIndices = mapbox::earcut<int>(earcutPolygon);

        // 创建底面索引（反向顺序）
        for (int idx = 0; idx < earcutIndices.size(); idx += 3) {
            indices->push_back(roofBaseIdx + 2 * earcutIndices[idx]);
            indices->push_back(roofBaseIdx + 2 * earcutIndices[idx + 2]);
            indices->push_back(roofBaseIdx + 2 * earcutIndices[idx + 1]);
        }

        // 创建顶面索引
        for (int idx = 0; idx < earcutIndices.size(); idx += 3) {
            indices->push_back(roofBaseIdx + 2 * earcutIndices[idx] + 1);
            indices->push_back(roofBaseIdx + 2 * earcutIndices[idx + 1] + 1);
            indices->push_back(roofBaseIdx + 2 * earcutIndices[idx + 2] + 1);
        }
    }

    geometry->setVertexArray(vertices);
    geometry->setNormalArray(normals, osg::Array::BIND_PER_VERTEX);
    geometry->addPrimitiveSet(indices);

    return geometry;
}

bool ShapefileDataPool::loadFromShapefileWithGeometry(const std::string& filename, const std::string& heightField,
                                                      double centerLon, double centerLat) {
    items_.clear();

    // 注册所有 GDAL 驱动
    static bool gdal_initialized = false;
    if (!gdal_initialized) {
        GDALAllRegister();
        gdal_initialized = true;
    }

    // 打开 Shapefile
    GDALDataset* poDS = static_cast<GDALDataset*>(
        GDALOpenEx(filename.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr)
    );

    if (!poDS) {
        LOG_E("Failed to open shapefile: %s", filename.c_str());
        return false;
    }

    OGRLayer* poLayer = poDS->GetLayer(0);
    if (!poLayer) {
        LOG_E("No layer found in shapefile");
        GDALClose(poDS);
        return false;
    }

    // 获取图层的空间参考系统
    const OGRSpatialReference* poSrcSRS = poLayer->GetSpatialRef();
    OGRCoordinateTransformation* poCT = nullptr;

    if (poSrcSRS) {
        char* pszWKT = nullptr;
        poSrcSRS->exportToWkt(&pszWKT);
        LOG_I("Source CRS: %s", pszWKT ? pszWKT : "Unknown");
        CPLFree(pszWKT);

        // 检查是否是地理坐标系（WGS84）
        if (!poSrcSRS->IsGeographic()) {
            // 需要转换为 WGS84
            OGRSpatialReference oDstSRS;
            oDstSRS.SetWellKnownGeogCS("WGS84");

            poCT = OGRCreateCoordinateTransformation(poSrcSRS, &oDstSRS);
            if (poCT) {
                LOG_I("Created coordinate transformation to WGS84");
            } else {
                LOG_W("Failed to create coordinate transformation, using original coordinates");
            }
        }
    } else {
        LOG_W("No spatial reference found in shapefile, assuming WGS84");
    }

    poLayer->ResetReading();
    OGRFeature* poFeature = nullptr;
    int featureId = 0;

    while ((poFeature = poLayer->GetNextFeature()) != nullptr) {
        OGRGeometry* poGeometry = poFeature->GetGeometryRef();
        if (!poGeometry) {
            OGRFeature::DestroyFeature(poFeature);
            continue;
        }

        // 创建新的数据项
        auto item = std::make_shared<ShapefileSpatialItem>();
        item->featureId = featureId++;

        // 获取所有属性
        for (int i = 0; i < poFeature->GetFieldCount(); ++i) {
            const OGRFieldDefn* poFieldDefn = poFeature->GetFieldDefnRef(i);
            const char* fieldName = poFieldDefn->GetNameRef();

            switch (poFieldDefn->GetType()) {
                case OFTInteger:
                    item->properties[fieldName] = poFeature->GetFieldAsInteger(i);
                    break;
                case OFTReal:
                    item->properties[fieldName] = poFeature->GetFieldAsDouble(i);
                    break;
                case OFTString:
                    item->properties[fieldName] = poFeature->GetFieldAsString(i);
                    break;
                default:
                    item->properties[fieldName] = poFeature->GetFieldAsString(i);
                    break;
            }
        }

        // 获取高度（不区分大小写查找）
        double height = 0.0;
        if (!heightField.empty()) {
            // 转换为小写进行查找
            std::string lowerHeightField = heightField;
            std::transform(lowerHeightField.begin(), lowerHeightField.end(), lowerHeightField.begin(), ::tolower);

            for (const auto& kv : item->properties) {
                std::string lowerKey = kv.first;
                std::transform(lowerKey.begin(), lowerKey.end(), lowerKey.begin(), ::tolower);
                if (lowerKey == lowerHeightField) {
                    if (kv.second.is_number()) {
                        height = kv.second.get<double>();
                    } else if (kv.second.is_string()) {
                        try {
                            height = std::stod(kv.second.get<std::string>());
                        } catch (...) {
                            height = 0.0;
                        }
                    }
                    break;
                }
            }
        }

        // 如果需要，进行坐标转换
        if (poCT) {
            poGeometry->transform(poCT);
        }

        // 计算包围盒
        OGREnvelope envelope;
        poGeometry->getEnvelope(&envelope);

        // 注意：GDAL 坐标转换后，envelope.MinX/MaxX 是纬度，envelope.MinY/MaxY 是经度
        // 但 TileBBox 期望 minx/maxx 是经度，miny/maxy 是纬度
        // 所以需要交换 X 和 Y
        double minLon, maxLon, minLat, maxLat;
        if (poCT) {
            // 坐标转换后，X=纬度，Y=经度
            minLon = envelope.MinY;
            maxLon = envelope.MaxY;
            minLat = envelope.MinX;
            maxLat = envelope.MaxX;
        } else {
            // 没有坐标转换，X=经度，Y=纬度
            minLon = envelope.MinX;
            maxLon = envelope.MaxX;
            minLat = envelope.MinY;
            maxLat = envelope.MaxY;
        }

        item->bounds = TileBBox(
            minLon, maxLon,
            minLat, maxLat,
            0.0, height
        );

        // 转换几何数据
        bool xySwapped = (poCT != nullptr);  // 如果进行了坐标转换，X/Y 需要交换
        OGRwkbGeometryType geomType = wkbFlatten(poGeometry->getGeometryType());
        if (geomType == wkbPolygon) {
            OGRPolygon* polygon = static_cast<OGRPolygon*>(poGeometry);
            auto geometry = create_geometry_from_polygon(polygon, height, centerLon, centerLat, xySwapped);
            if (geometry.valid()) {
                item->geometries.push_back(geometry);
            }
        } else if (geomType == wkbMultiPolygon) {
            OGRMultiPolygon* multiPoly = static_cast<OGRMultiPolygon*>(poGeometry);
            int numGeoms = multiPoly->getNumGeometries();
            for (int i = 0; i < numGeoms; i++) {
                OGRPolygon* polygon = static_cast<OGRPolygon*>(multiPoly->getGeometryRef(i));
                auto geometry = create_geometry_from_polygon(polygon, height, centerLon, centerLat, xySwapped);
                if (geometry.valid()) {
                    item->geometries.push_back(geometry);
                }
            }
        }

        // 只添加有有效几何数据的项
        if (!item->geometries.empty()) {
            items_.push_back(std::move(item));
        }

        OGRFeature::DestroyFeature(poFeature);

        // 每1000个要素输出一次日志
        if (featureId % 1000 == 0) {
            LOG_I("Loaded %d features with geometry...", featureId);
        }
    }

    GDALClose(poDS);

    // 释放坐标转换对象
    if (poCT) {
        OGRCoordinateTransformation::DestroyCT(poCT);
    }

    LOG_I("Successfully loaded %zu features with geometry from shapefile", items_.size());
    return !items_.empty();
}

} // namespace shapefile

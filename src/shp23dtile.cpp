#include <tiny_gltf.h>
#include <nlohmann/json.hpp>
#include "extern.h"

#include "mesh_processor.h"
#include "attribute_storage.h"
#include "coordinate_transformer.h"
#include "lod_pipeline.h"
#include "shape.h"

// Tileset 模块
#include "tileset/tileset_types.h"
#include "tileset/tileset_writer.h"
#include "tileset/bounding_volume.h"
#include "tileset/geometric_error.h"

// Shapefile 业务层
#include "shapefile/shapefile_tile.h"
#include "shapefile/shapefile_tileset_adapter.h"

// 阶段4迁移：完整新框架
#include "shapefile/shapefile_processor.h"

/* vcpkg path */
#include <ogrsf_frmts.h>

#include <optional>
#include <fstream>
#include <osg/Material>
#include <osg/PagedLOD>
#include <osgDB/ReadFile>
#include <osgDB/ConvertUTF>
#include <osgUtil/Optimizer>
#include <osgUtil/SmoothingVisitor>

#include <osg/Geometry>
#include <osg/Geode>
#include <osgUtil/DelaunayTriangulator>
#include <osgUtil/Tessellator>
#include <osgUtil/Optimizer>
#include <osgUtil/SmoothingVisitor>

#include <string>
#include <vector>
#include <array>
#include <filesystem>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <set>
#include <limits>
#include <algorithm>
#include <cmath>
#include <numeric>

using namespace std;

// 全局变量
static double g_shp_center_lon = 0.0;
static double g_shp_center_lat = 0.0;
static OGRCoordinateTransformation* g_shp_coord_transform = nullptr;

/**
 * @brief Shapefile 转 3D Tiles - 阶段4完整实现
 *
 * 简化版本：只保留阶段4（ShapefileProcessor）实现
 * 使用新框架组件完成所有处理
 */
extern "C" bool
shp23dtile(const ShapeConversionParams* params)
{
    if (!params || !params->input_path || !params->output_path) {
        LOG_E("make shp23dtile failed: invalid parameters");
        return false;
    }

    const char* filename = params->input_path;
    const char* dest = params->output_path;
    std::string height_field = "";
    if (params->height_field) {
        height_field = params->height_field;
    }

    // 注册 GDAL 驱动
    GDALAllRegister();

    // 确保输出目录存在
    std::error_code mkdir_ec;
    std::filesystem::create_directories(std::filesystem::path(dest), mkdir_ec);

    // 打开 Shapefile 获取图层信息（用于属性存储）
    GDALDataset* poDS = (GDALDataset*)GDALOpenEx(
        filename, GDAL_OF_VECTOR,
        NULL, NULL, NULL);
    if (poDS == NULL)
    {
        LOG_E("open shapefile [%s] failed", filename);
        return false;
    }

    OGRLayer* poLayer = poDS->GetLayer(params->layer_id);
    if (!poLayer) {
        GDALClose(poDS);
        LOG_E("open layer [%s]:[%d] failed", filename, params->layer_id);
        return false;
    }

    // 存储属性到 SQLite 数据库
    {
        const std::string sqlite_path = (std::filesystem::path(dest) / "attributes.db").string();
        AttributeStorage attr_storage(sqlite_path);

        if (attr_storage.isOpen()) {
            if (attr_storage.createTable(poLayer->GetLayerDefn())) {
                attr_storage.insertFeaturesInBatches(poLayer, 1000);
            }
        }
    }

    // 检查几何类型
    OGRwkbGeometryType geomType = poLayer->GetGeomType();
    if (geomType != wkbPolygon && geomType != wkbMultiPolygon &&
        geomType != wkbPolygon25D && geomType != wkbMultiPolygon25D)
    {
        GDALClose(poDS);
        LOG_E("only support polygon now");
        return false;
    }

    // 计算中心点（用于坐标转换）
    OGREnvelope envelope;
    if (poLayer->GetExtent(&envelope, true) == OGRERR_NONE) {
        g_shp_center_lon = (envelope.MinX + envelope.MaxX) * 0.5;
        g_shp_center_lat = (envelope.MinY + envelope.MaxY) * 0.5;
        LOG_I("Center: lon=%.6f, lat=%.6f", g_shp_center_lon, g_shp_center_lat);
    }

    GDALClose(poDS);

    // ========== 阶段4：使用 ShapefileProcessor 完整新框架 ==========
    LOG_I("Stage4: Building tileset using ShapefileProcessor (full new framework)...");

    // 配置 ShapefileProcessor
    shapefile::ShapefileProcessorConfig processorConfig;
    processorConfig.inputPath = filename;
    processorConfig.outputPath = dest;
    processorConfig.heightField = height_field;
    processorConfig.centerLongitude = g_shp_center_lon;
    processorConfig.centerLatitude = g_shp_center_lat;
    processorConfig.enableLOD = params->enable_lod;
    processorConfig.enableSimplification = params->simplify_params.enable_simplification;
    processorConfig.simplifyParams = params->simplify_params;
    processorConfig.enableDraco = params->draco_compression_params.enable_compression;
    processorConfig.dracoParams = params->draco_compression_params;

    // 配置四叉树
    processorConfig.quadtreeConfig.maxDepth = 10;
    processorConfig.quadtreeConfig.maxItemsPerNode = 1000;
    processorConfig.quadtreeConfig.metricThreshold = 0.01;

    // 配置 Tileset 适配器
    processorConfig.boundingVolumeScaleFactor = 2.0;
    processorConfig.geometricErrorScale = 0.5;
    processorConfig.applyRootTransform = true;

    // 创建并运行处理器
    shapefile::ShapefileProcessor processor(processorConfig);
    auto result = processor.process();

    if (result.success) {
        LOG_I("Stage4: Successfully generated tileset at %s", result.tilesetPath.c_str());
        LOG_I("Stage4: Processed %zu features into %zu nodes",
              result.featureCount, result.nodeCount);
        return true;
    } else {
        LOG_E("Stage4: Failed to generate tileset: %s", result.errorMessage.c_str());
        return false;
    }
}

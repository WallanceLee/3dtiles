#include "fbx/fbx_processor.h"
#include "utils/log.h"
#include "utils/file_utils.h"
#include "./coords/coordinate_transformer.h"
#include "pipeline/conversion_pipeline.h"
#include "pipeline/fbx_pipeline.h"
#include "pipeline/adapters/fbx/fbx_data_source.h"
#include <osg/MatrixTransform>
#include <osg/Geode>
#include <osg/Material>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <algorithm>

// Use existing tinygltf if possible, or include it
#include <osgDB/ReaderWriter>
#include <osgDB/Registry>
#include <tiny_gltf.h>
#include <nlohmann/json.hpp>
#include "common/mesh_processor.h"
#include <osg/Texture>
#include <osg/Image>
#include "lod_pipeline.h"
#include <osg/GL>
#include <cmath>

// Stage 2: New architecture integration
#include "spatial/strategy/octree_strategy.h"

// Stage 3: B3DMGenerator integration
#include "fbx/fbx_geometry_extractor.h"

// Stage 4: TilesetBuilder integration
#include "fbx/fbx_tileset_adapter.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

// Helper to check point in box
bool isPointInBox(const osg::Vec3d& p, const osg::BoundingBox& b) {
    return p.x() >= b.xMin() && p.x() <= b.xMax() &&
           p.y() >= b.yMin() && p.y() <= b.yMax() &&
           p.z() >= b.zMin() && p.z() <= b.zMax();
}

FBXPipeline::FBXPipeline(const PipelineSettings& s) : settings(s) {
}

void FBXPipeline::SetDataSource(pipeline::DataSource* dataSource) {
    externalDataSource_ = dataSource;
}

void FBXPipeline::SetSpatialIndex(pipeline::ISpatialIndex* spatialIndex) {
    externalSpatialIndex_ = spatialIndex;
}

void FBXPipeline::SetTilesetBuilder(pipeline::ITilesetBuilder* tilesetBuilder) {
    externalTilesetBuilder_ = tilesetBuilder;
}

pipeline::ITilesetBuilder* FBXPipeline::GetCurrentTilesetBuilder() {
    if (externalTilesetBuilder_) {
        return externalTilesetBuilder_;
    }
    // 如果内部 TilesetBuilder 未创建，则创建它
    if (!tilesetBuilder_) {
        // 使用工厂创建 FBX TilesetBuilder
        tilesetBuilder_ = pipeline::TilesetBuilderFactory::Instance().Create("fbx");
        if (tilesetBuilder_) {
            pipeline::TilesetBuilderConfig tbConfig;
            tbConfig.center_longitude = settings.longitude;
            tbConfig.center_latitude = settings.latitude;
            tbConfig.center_height = settings.height;
            tbConfig.bounding_volume_scale = 1.0;
            tbConfig.child_geometric_error_multiplier = settings.geScale;
            tbConfig.enable_lod = settings.enableLOD;
            tilesetBuilder_->Initialize(tbConfig);
        }
    }
    return tilesetBuilder_.get();
}

pipeline::DataSource* FBXPipeline::GetCurrentDataSource() {
    if (externalDataSource_) {
        return externalDataSource_;
    }
    // 如果内部数据源未创建，则使用工厂创建
    if (!dataSource_) {
        dataSource_ = pipeline::DataSourceFactory::Instance().Create("fbx");
        if (dataSource_) {
            LOG_I("FBXPipeline: Created FBXDataSource using factory");
        }
    }
    return dataSource_.get();
}

pipeline::ISpatialIndex* FBXPipeline::GetCurrentSpatialIndex() {
    if (externalSpatialIndex_) {
        return externalSpatialIndex_;
    }
    // 如果内部空间索引未创建，则使用工厂创建
    if (!spatialIndex_) {
        spatialIndex_ = pipeline::SpatialIndexFactory::Instance().Create("octree");
        if (spatialIndex_) {
            LOG_I("FBXPipeline: Created OctreeIndex using factory");
        }
    }
    return spatialIndex_.get();
}

const spatial::strategy::OctreeNode* FBXPipeline::GetCurrentRootNode() const {
    if (externalSpatialIndex_) {
        // 从外部空间索引获取根节点
        auto* rawNode = externalSpatialIndex_->GetRootNode();
        if (rawNode) {
            // 需要适配器转换
            // 暂时返回 nullptr，实际使用时需要正确处理
            return nullptr;
        }
    }
    if (spatialIndex_) {
        // 从内部空间索引获取根节点
        auto* rawNode = spatialIndex_->GetRootNode();
        if (rawNode) {
            // 需要适配器转换
            return nullptr;
        }
    }
    // 原有逻辑返回内部构建的根节点
    return nullptr;
}

bool FBXPipeline::loadData() {
    // 步骤1修改：如果提供了外部数据源，直接使用其空间项
    if (externalDataSource_) {
        LOG_I("FBXPipeline: Using external data source");

        // 检查外部数据源是否已加载
        if (!externalDataSource_->IsLoaded()) {
            pipeline::DataSourceConfig dsConfig;
            dsConfig.input_path = settings.inputPath;
            dsConfig.output_path = settings.outputPath;
            dsConfig.center_longitude = settings.longitude;
            dsConfig.center_latitude = settings.latitude;
            dsConfig.center_height = settings.height;

            if (!externalDataSource_->Load(dsConfig)) {
                LOG_E("FBXPipeline: Failed to load external data source");
                return false;
            }
        }

        // 从外部数据源获取空间项
        auto* fbxSource = dynamic_cast<pipeline::adapters::fbx::FBXDataSource*>(externalDataSource_);
        if (fbxSource) {
            spatialItems_ = fbxSource->GetFBXSpatialItems();
            // 注意：不要获取外部数据源的 loader，避免双重释放
            // 我们自己加载 loader
        }

        LOG_I("FBXPipeline: External data source loaded with %zu items",
              spatialItems_.size());
        // 继续执行下面的 loader 加载逻辑
    }

    // 原有逻辑：内部加载数据（无论是否使用外部数据源，都需要加载 loader）
    loader_ = std::make_unique<FBXLoader>(settings.inputPath);
    loader_->load();
    LOG_I("FBX Loaded. Mesh Pool Size: %zu", loader_->meshPool.size());

    // 阶段1：创建空间对象适配器
    LOG_I("Creating Spatial Items...");
    spatialItems_ = fbx::createSpatialItems(loader_.get());
    LOG_I("Created %zu spatial items.", spatialItems_.size());

    return !spatialItems_.empty();
}

void FBXPipeline::run() {
    LOG_I("Starting FBXPipeline (Stage 1)...");

    // 步骤1修改：使用 loadData 方法加载数据
    if (!loadData()) {
        LOG_E("FBXPipeline: Failed to load data");
        return;
    }

    // Lambda to generate LOD settings chain
    auto generateLODChain = [&](const PipelineSettings& cfg) -> LODPipelineSettings {
        LODPipelineSettings lodOps;
        lodOps.enable_lod = cfg.enableLOD;

        SimplificationParams simTemplate;
        simTemplate.enable_simplification = true;
        simTemplate.target_error = 0.0001f; // Base error

        DracoCompressionParams dracoTemplate;
        dracoTemplate.enable_compression = cfg.enableDraco;

        // Use build_lod_levels from lod_pipeline.h
        // Ratios are expected to be e.g. [1.0, 0.5, 0.25]
        lodOps.levels = build_lod_levels(
            cfg.lodRatios,
            simTemplate.target_error,
            simTemplate,
            dracoTemplate,
            false // draco_for_lod0
        );

        return lodOps;
    };

    // Simplification Step (Only if LOD is NOT enabled, otherwise we do it per-level or later)
    if (settings.enableSimplify && !settings.enableLOD) {
        LOG_I("Simplifying meshes (Global)...");
        SimplificationParams simParams;
        simParams.enable_simplification = true;
        simParams.target_ratio = 0.5f; // Default ratio
        simParams.target_error = 0.0001f; // Base error

        for (auto& pair : loader_->meshPool) {
            if (pair.second.geometry) {
                simplify_mesh_geometry(pair.second.geometry.get(), simParams);
            }
        }
    } else if (settings.enableLOD) {
        // If LOD is enabled, we prepare the settings
        LODPipelineSettings lodSettings = generateLODChain(settings);
        LOG_I("LOD Enabled. Generated %zu LOD levels configuration.", lodSettings.levels.size());
    }

    LOG_I("Building Octree (Stage 2 - Using OctreeStrategy)...");

    // Stage 2: Use OctreeStrategy to build spatial index
    spatial::strategy::OctreeStrategy octreeStrategy;
    spatial::strategy::OctreeConfig octreeConfig;
    octreeConfig.maxDepth = settings.maxDepth;
    octreeConfig.maxItemsPerNode = settings.maxItemsPerTile;

    // Convert spatialItems_ to SpatialItemList for the strategy
    spatial::core::SpatialItemList spatialItemList;
    for (const auto& item : spatialItems_) {
        spatialItemList.push_back(item);
    }

    // Calculate world bounds from spatial items
    spatial::core::SpatialBounds<double, 3> worldBounds;
    if (!spatialItems_.empty()) {
        auto firstBounds = spatialItems_[0]->getBounds();
        std::array<double, 3> min = firstBounds.min();
        std::array<double, 3> max = firstBounds.max();

        for (const auto& item : spatialItems_) {
            auto bounds = item->getBounds();
            for (int i = 0; i < 3; ++i) {
                min[i] = std::min(min[i], bounds.min()[i]);
                max[i] = std::max(max[i], bounds.max()[i]);
            }
        }
        worldBounds = spatial::core::SpatialBounds<double, 3>(min, max);
    }

    // Build the octree index
    auto spatialIndex = octreeStrategy.buildIndex(spatialItemList, worldBounds, octreeConfig);
    auto* octreeIndex = static_cast<spatial::strategy::OctreeIndex*>(spatialIndex.get());

    LOG_I("Octree built with %zu nodes and %zu items",
          octreeIndex->getNodeCount(), octreeIndex->getItemCount());

    // Stage 3: Setup B3DMGenerator
    LOG_I("Setting up B3DMGenerator (Stage 3)...");
    b3dm::B3DMGeneratorConfig b3dmConfig;
    b3dmConfig.centerLongitude = settings.longitude;
    b3dmConfig.centerLatitude = settings.latitude;
    b3dmConfig.centerHeight = settings.height;
    b3dmConfig.enableSimplification = settings.enableSimplify;
    b3dmConfig.enableDraco = settings.enableDraco;
    b3dmConfig.enableTextureCompress = settings.enableTextureCompress;

    // Create geometry extractor
    auto geometryExtractor = std::make_unique<fbx::FBXGeometryExtractor>();
    b3dmConfig.geometryExtractor = geometryExtractor.get();

    b3dm::B3DMGenerator b3dmGenerator(b3dmConfig);

    // Build LOD levels configuration
    std::vector<LODLevelSettings> lodLevels;
    if (settings.enableLOD) {
        SimplificationParams simTemplate;
        simTemplate.enable_simplification = true;
        simTemplate.target_error = 0.0001f;

        DracoCompressionParams dracoTemplate;
        dracoTemplate.enable_compression = settings.enableDraco;

        lodLevels = build_lod_levels(
            settings.lodRatios,
            simTemplate.target_error,
            simTemplate,
            dracoTemplate,
            false
        );
        LOG_I("Stage 3: Generated %zu LOD levels", lodLevels.size());
    } else {
        // Single LOD level (LOD0)
        LODLevelSettings lod0;
        lod0.target_ratio = 1.0f;
        lod0.enable_simplification = false;
        lod0.enable_draco = settings.enableDraco;
        lodLevels.push_back(lod0);
    }

    // Stage 4: Process nodes and build tileset using FBXTilesetAdapter
    LOG_I("Processing Nodes and Building Tileset (Stage 4)...");

    // Collect all node metadata directly from OctreeIndex
    fbx::FBXTileMetaMap allMetas;
    int nodeIdCounter = 0;
    auto* octreeRoot = static_cast<const spatial::strategy::OctreeNode*>(octreeIndex->getRootNode());
    auto rootMeta = processOctreeNode(octreeRoot, settings.outputPath, "0", b3dmGenerator, lodLevels, allMetas, nodeIdCounter);

    if (!rootMeta || allMetas.empty()) {
        LOG_E("Stage 4: Failed to process nodes");
        return;
    }

    // Create adapter and build tileset
    fbx::FBXTilesetAdapterConfig adapterConfig;
    adapterConfig.centerLongitude = settings.longitude;
    adapterConfig.centerLatitude = settings.latitude;
    adapterConfig.centerHeight = settings.height;
    adapterConfig.boundingVolumeScale = 1.0;
    adapterConfig.geometricErrorScale = settings.geScale;
    adapterConfig.enableLOD = settings.enableLOD;
    adapterConfig.lodLevelCount = static_cast<int>(lodLevels.size());

    fbx::FBXTilesetAdapter tilesetAdapter(adapterConfig);

    // Build and write tileset (completely replaces writeTilesetJson)
    if (!tilesetAdapter.buildAndWriteTileset(allMetas, settings.outputPath)) {
        LOG_E("Stage 4: Failed to build and write tileset");
        return;
    }

    LOG_I("FBXPipeline Finished.");
}

// Process OctreeNode directly to generate tile metadata
fbx::FBXTileMetaPtr FBXPipeline::processOctreeNode(
    const spatial::strategy::OctreeNode* node,
    const std::string& parentPath,
    const std::string& treePath,
    b3dm::B3DMGenerator& generator,
    const std::vector<LODLevelSettings>& lodLevels,
    fbx::FBXTileMetaMap& allMetas,
    int& nodeIdCounter) {

    // Create TileCoord (using depth and position)
    int x = 0, y = 0, z = 0;
    if (!treePath.empty()) {
        std::stringstream ss(treePath);
        std::string token;
        int depth = 0;
        while (std::getline(ss, token, '_')) {
            int index = std::stoi(token);
            x = index % 2;
            y = (index / 2) % 2;
            z = index / 4;
            depth++;
        }
    }

    common::TileCoord coord(static_cast<int>(node->getDepth()), x, y);
    auto meta = std::make_shared<fbx::FBXTileMeta>(coord);
    meta->isLeaf = node->isLeaf();

    // Calculate bounding box (Y-up to Z-up conversion)
    auto bounds = node->getBounds3D();
    osg::BoundingBoxd bbox;
    bbox.expandBy(osg::Vec3d(bounds.min()[0], -bounds.max()[2], bounds.min()[1]));
    bbox.expandBy(osg::Vec3d(bounds.max()[0], -bounds.min()[2], bounds.max()[1]));
    meta->setBoundingBox(bbox);

    // Calculate geometric error
    double dx = bbox.xMax() - bbox.xMin();
    double dy = bbox.yMax() - bbox.yMin();
    double dz = bbox.zMax() - bbox.zMin();
    meta->geometricError = std::sqrt(dx*dx + dy*dy + dz*dz) / 20.0 * settings.geScale;

    // Generate B3DM if node has content
    auto items = node->getItems();
    if (!items.empty()) {
        meta->hasGeometry = true;

        // Create tile directory
        std::string tileDir = settings.outputPath + "/" + meta->getTileDirectory();
        std::filesystem::create_directories(tileDir);

        // Convert SpatialItemRefList to the format expected by B3DMGenerator
        spatial::core::SpatialItemRefList spatialItems;
        for (const auto& item : items) {
            spatialItems.push_back(item);
        }

        if (!spatialItems.empty() && !lodLevels.empty()) {
            // Generate B3DM files
            std::string tileName = "tile_" + treePath;
            auto lodFiles = generator.generateLODFiles(spatialItems, tileDir, tileName, lodLevels);

            // Save LOD file paths
            for (const auto& file : lodFiles) {
                meta->lodFiles.push_back(meta->getTileDirectory() + "/" + file.filename);
            }

            // Set content URI
            if (settings.enableLOD && !meta->lodFiles.empty()) {
                meta->content.uri = meta->getTilesetPath();
                meta->content.hasContent = true;
            } else if (!meta->lodFiles.empty()) {
                meta->content.uri = meta->lodFiles[0];
                meta->content.hasContent = true;
            }
        }
    }

    // Save to map
    allMetas[meta->key()] = meta;

    // Recursively process children
    auto children = node->getChildren();
    for (size_t i = 0; i < children.size(); ++i) {
        std::string childTreePath = treePath + "_" + std::to_string(i);
        auto* childNode = static_cast<const spatial::strategy::OctreeNode*>(children[i]);
        auto childMeta = processOctreeNode(
            childNode,
            parentPath,
            childTreePath,
            generator,
            lodLevels,
            allMetas,
            nodeIdCounter
        );
        if (childMeta) {
            meta->childrenKeys.push_back(childMeta->key());
        }
    }

    return meta;
}

// C-API Implementation
extern "C" void* fbx23dtile(
    const char* in_path,
    const char* out_path,
    double* box_ptr,
    int* len,
    int max_lvl,
    bool enable_texture_compress,
    bool enable_meshopt,
    bool enable_draco,
    bool enable_unlit,
    double longitude,
    double latitude,
    double height,
    bool enable_lod
) {
    std::cout << "[fbx23dtile] Using unified pipeline" << std::endl;

    // 构建 ConversionParams
    pipeline::ConversionParams params;
    params.input_path = in_path;
    params.output_path = out_path;
    params.source_type = "fbx";
    params.longitude = longitude;
    params.latitude = latitude;
    params.height = height;
    params.enable_lod = enable_lod;
    params.enable_simplify = enable_meshopt;
    params.enable_draco = enable_draco;
    params.enable_texture_compress = enable_texture_compress;
    params.enable_unlit = enable_unlit;

    // 创建管道并执行转换
    auto pipeline = pipeline::PipelineFactory::Instance().Create("fbx");
    if (!pipeline) {
        std::cerr << "[fbx23dtile] Failed to create pipeline" << std::endl;
        return nullptr;
    }

    auto result = pipeline->Convert(params);
    if (!result.success) {
        std::cerr << "[fbx23dtile] Conversion failed" << std::endl;
        return nullptr;
    }

    // 读取 tileset.json 返回给 Rust
    fs::path tilesetPath = fs::path(out_path) / "tileset.json";
    if (!fs::exists(tilesetPath)) {
        std::cerr << "[fbx23dtile] tileset.json not found" << std::endl;
        return nullptr;
    }

    std::ifstream t(tilesetPath);
    std::stringstream buffer;
    buffer << t.rdbuf();
    std::string jsonStr = buffer.str();

    // Parse json to get bounding box
    try {
        json root = json::parse(jsonStr);
        auto& box = root["root"]["boundingVolume"]["box"];
        if (box.is_array() && box.size() == 12) {
            double cx = box[0];
            double cy = box[1];
            double cz = box[2];
            double hx = box[3];
            double hy = box[7];
            double hz = box[11];

            double max[3] = {cx + hx, cy + hy, cz + hz};
            double min[3] = {cx - hx, cy - hy, cz - hz};

            memcpy(box_ptr, max, 3 * sizeof(double));
            memcpy(box_ptr + 3, min, 3 * sizeof(double));
        }
    } catch (const std::exception& e) {
        std::cerr << "[fbx23dtile] Failed to parse tileset.json: " << e.what() << std::endl;
    }

    void* str = malloc(jsonStr.length() + 1);
    if (str) {
        memcpy(str, jsonStr.c_str(), jsonStr.length());
        ((char*)str)[jsonStr.length()] = '\0';
        *len = (int)jsonStr.length();
    }

    return str;
}

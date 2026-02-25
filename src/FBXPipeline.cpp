#include "FBXPipeline.h"
#include "extern.h"
#include "./coords/coordinate_transformer.h"
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

void FBXPipeline::run() {
    LOG_I("Starting FBXPipeline (Stage 1)...");

    loader_ = std::make_unique<FBXLoader>(settings.inputPath);
    loader_->load();
    LOG_I("FBX Loaded. Mesh Pool Size: %zu", loader_->meshPool.size());

    // 阶段1：创建空间对象适配器
    LOG_I("Creating Spatial Items...");
    spatialItems_ = fbx::createSpatialItems(loader_.get());
    LOG_I("Created %zu spatial items.", spatialItems_.size());

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
    std::string input(in_path);
    std::string output(out_path);

    PipelineSettings settings;
    settings.inputPath = input;
    settings.outputPath = output;
    settings.maxDepth = max_lvl > 0 ? max_lvl : 5;
    settings.enableTextureCompress = enable_texture_compress;
    settings.enableDraco = enable_draco;
    settings.enableSimplify = enable_meshopt;
    settings.enableLOD = enable_lod;
    settings.enableUnlit = enable_unlit;
    settings.longitude = longitude;
    settings.latitude = latitude;
    settings.height = height;

    FBXPipeline pipeline(settings);
    pipeline.run();

    fs::path tilesetPath = fs::path(output) / "tileset.json";
    if (!fs::exists(tilesetPath)) {
        LOG_E("Failed to generate tileset.json at %s", tilesetPath.string().c_str());
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
        LOG_E("Failed to parse tileset.json: %s", e.what());
    }

    void* str = malloc(jsonStr.length() + 1);
    if (str) {
        memcpy(str, jsonStr.c_str(), jsonStr.length());
        ((char*)str)[jsonStr.length()] = '\0';
        *len = (int)jsonStr.length();
    }

    return str;
}

#pragma once

#include "fbx.h"
#include "fbx/fbx_spatial_item_adapter.h"
#include "fbx/fbx_tile_meta.h"
#include "spatial/strategy/octree_strategy.h"
#include "b3dm/b3dm_generator.h"
#include <string>
#include <vector>
#include <osg/Matrixd>
#include <osg/BoundingBox>
#include <osg/Geometry>

// Forward declarations
namespace tinygltf {
    class Model;
}

struct PipelineSettings {
    std::string inputPath;
    std::string outputPath;
    int maxDepth = 5;
    int maxItemsPerTile = 1000;

    // Optimization flags
    bool enableSimplify = false;
    bool enableDraco = false;
    bool enableTextureCompress = false; // KTX2
    bool enableLOD = false; // Enable Hierarchical LOD generation
    bool enableUnlit = false; // Enable KHR_materials_unlit
    std::vector<float> lodRatios = {1.0f, 0.5f, 0.25f}; // Default LOD ratios (Fine to Coarse)

    // Geolocation (Origin)
    double longitude = 0.0;
    double latitude = 0.0;
    double height = 0.0;

    // Geometric error scale (multiplier applied to boundingVolume diagonal)
    double geScale = 0.5; // Adjusted for better LOD switching with SSE=16
};

class FBXPipeline {
public:
    FBXPipeline(const PipelineSettings& settings);
    ~FBXPipeline();

    void run();

private:
    PipelineSettings settings;
    FBXLoader* loader = nullptr;

    // 阶段1：空间对象适配器列表
    fbx::FBXSpatialItemList spatialItems_;

    // Process OctreeNode directly to generate tile metadata
    fbx::FBXTileMetaPtr processOctreeNode(
        const spatial::strategy::OctreeNode* node,
        const std::string& parentPath,
        const std::string& treePath,
        b3dm::B3DMGenerator& generator,
        const std::vector<LODLevelSettings>& lodLevels,
        fbx::FBXTileMetaMap& allMetas,
        int& nodeIdCounter
    );
};

#pragma once

/**
 * @file b3dm/b3dm_generator.h
 * @brief 通用B3DM内容生成器
 *
 * 该模块提供统一的B3DM生成功能，支持：
 * - 几何体合并与转换
 * - LOD级别生成
 * - Draco压缩
 * - 纹理压缩(KTX2)
 * - 批量属性处理
 *
 * 被FBXPipeline和ShapefileProcessor复用
 */

#include "../common/geometry_extractor.h"
#include "b3dm_writer.h"
#include "../common/mesh_processor.h"
#include "../lod_pipeline.h"
#include <osg/Geometry>
#include <vector>
#include <string>
#include <optional>
#include <filesystem>
#include <unordered_map>

// Forward declaration for tinygltf
namespace tinygltf {
    class Model;
    class Buffer;
}

namespace b3dm {

/**
 * @brief B3DM生成配置
 */
struct B3DMGeneratorConfig {
    // 坐标转换参数
    double centerLongitude = 0.0;
    double centerLatitude = 0.0;
    double centerHeight = 0.0;

    // 几何简化
    bool enableSimplification = false;
    SimplificationParams simplifyParams;

    // Draco压缩
    bool enableDraco = false;
    DracoCompressionParams dracoParams;

    // 纹理压缩
    bool enableTextureCompress = false;

    // 几何体提取器（由调用方提供）
    common::IGeometryExtractor* geometryExtractor = nullptr;
};

/**
 * @brief LOD文件信息
 */
struct LODFileInfo {
    int level;              // LOD级别
    std::string filename;   // 文件名
    std::string relativePath;  // 相对路径
    double geometricError;  // 几何误差
};

/**
 * @brief 通用B3DM内容生成器
 */
class B3DMGenerator {
public:
    explicit B3DMGenerator(const B3DMGeneratorConfig& config);

    /**
     * @brief 生成单LOD级别的B3DM
     *
     * @param items 空间对象列表
     * @param lodSettings LOD级别设置
     * @return B3DM二进制数据，失败返回空字符串
     */
    std::string generate(
        const spatial::core::SpatialItemRefList& items,
        const LODLevelSettings& lodSettings
    );

    /**
     * @brief 生成多LOD级别的B3DM文件
     *
     * @param items 空间对象列表
     * @param outputDir 输出目录
     * @param baseFilename 基础文件名（不含扩展名）
     * @param lodLevels LOD级别配置列表
     * @return 生成的文件信息列表
     */
    std::vector<LODFileInfo> generateLODFiles(
        const spatial::core::SpatialItemRefList& items,
        const std::string& outputDir,
        const std::string& baseFilename,
        const std::vector<LODLevelSettings>& lodLevels
    );

    /**
     * @brief 生成多LOD级别的B3DM文件（带坐标路径）
     *
     * @param items 空间对象列表
     * @param outputRoot 输出根目录
     * @param tilePath 瓦片路径（如 "tile/5/3/2"）
     * @param lodLevels LOD级别配置列表
     * @return 生成的文件信息列表
     */
    std::vector<LODFileInfo> generateLODFilesWithPath(
        const spatial::core::SpatialItemRefList& items,
        const std::string& outputRoot,
        const std::string& tilePath,
        const std::vector<LODLevelSettings>& lodLevels
    );

private:
    B3DMGeneratorConfig config_;

    // 纹理类型枚举（用于材质处理）
    enum class TextureType {
        BASE_COLOR,
        NORMAL,
        EMISSIVE,
        METALLIC_ROUGHNESS,
        OCCLUSION
    };

    // 材质缓存（用于去重）
    std::unordered_map<std::string, int> materialCache_;

    // 按材质分组的几何体信息
    struct MaterialGroup {
        std::shared_ptr<common::MaterialInfo> materialInfo;
        osg::ref_ptr<osg::Geometry> geometry;
    };

    // 从空间对象提取并按材质分组合并几何体
    std::vector<MaterialGroup> extractAndMergeGeometriesByMaterial(
        const spatial::core::SpatialItemRefList& items
    );

    // 从空间对象提取并合并几何体（已废弃，使用 extractAndMergeGeometriesByMaterial）
    osg::ref_ptr<osg::Geometry> extractAndMergeGeometries(
        const spatial::core::SpatialItemRefList& items
    );

    // 应用几何简化
    void applySimplification(
        osg::Geometry* geometry,
        const SimplificationParams& params
    );

    // 构建GLTF模型（支持材质）
    void buildGLTFModel(
        osg::Geometry* mergedGeom,
        const spatial::core::SpatialItemRefList& items,
        bool enableDraco,
        const DracoCompressionParams& dracoParams,
        std::vector<unsigned char>& glbData,
        const std::vector<std::shared_ptr<common::MaterialInfo>>& materials
    );

    // 构建GLTF模型（支持多材质）
    void buildGLTFModelMultiMaterial(
        const std::vector<MaterialGroup>& materialGroups,
        const spatial::core::SpatialItemRefList& items,
        bool enableDraco,
        const DracoCompressionParams& dracoParams,
        std::vector<unsigned char>& glbData
    );

    // 构建材质（新增）
    int buildMaterial(
        const std::shared_ptr<common::MaterialInfo>& matInfo,
        tinygltf::Model& model,
        tinygltf::Buffer& buffer
    );

    // 处理并添加纹理（新增）
    void processAndAddTexture(
        osg::Texture* texture,
        const common::TextureTransformInfo& transform,
        TextureType type,
        tinygltf::Model& model,
        tinygltf::Buffer& buffer,
        int& textureIndexOut,
        bool& hasAlphaOut
    );

    // 计算材质哈希键（用于去重）
    std::string computeMaterialKey(
        const std::shared_ptr<common::MaterialInfo>& matInfo
    );

    // 构建BatchData
    BatchData buildBatchData(
        const spatial::core::SpatialItemRefList& items
    );

    // 生成文件名
    std::string generateFilename(int lodLevel) const;
};

} // namespace b3dm

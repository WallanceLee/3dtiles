#pragma once

/**
 * @file gltf/gltf_builder.h
 * @brief GLTF构建器
 *
 * 替代appendGeometryToModel的核心类
 * 协调GeometryUtils、MaterialUtils、TextureUtils和gltf_writer模块
 */

#include "../osg/utils/geometry_utils.h"
#include "../osg/utils/material_utils.h"
#include "../osg/utils/texture_utils.h"
#include "primitive_builder.h"
#include "material_builder.h"
#include "extension_manager.h"
#include "../mesh_processor.h"
#include <osg/Geometry>
#include <osg/Matrix>
#include <tiny_gltf.h>
#include <vector>
#include <string>

namespace gltf {

/**
 * @brief GLTF构建器配置
 */
struct GLTFBuilderConfig {
    // Draco压缩
    bool enableDraco = false;
    struct DracoCompressionParams {
        int positionQuantizationBits = 14;
        int normalQuantizationBits = 10;
        int texcoordQuantizationBits = 12;
        int compressionLevel = 7;
    } dracoParams;

    // KTX2纹理压缩
    bool enableKTX2 = false;

    // Unlit材质
    bool enableUnlit = false;

    // 双面渲染
    bool doubleSided = true;
};

/**
 * @brief 实例引用（与FBXPipeline::InstanceRef兼容）
 */
struct InstanceRef {
    void* meshInfo;           // MeshInstanceInfo指针
    int transformIndex;       // 变换索引
    osg::Matrixd matrix;      // 世界变换矩阵
    int originalBatchId;      // 原始批次ID
};

/**
 * @brief GLTF构建结果
 */
struct GLTFBuildResult {
    bool success = false;
    std::vector<unsigned char> glbData;
    osg::BoundingBoxd bounds;
    size_t vertexCount = 0;
    size_t triangleCount = 0;
};

/**
 * @brief GLTF构建器
 *
 * 替代appendGeometryToModel，提供清晰的职责分离：
 * - GeometryUtils: 几何体处理
 * - MaterialUtils: 材质参数提取
 * - TextureUtils: 纹理处理
 * - gltf_writer: GLTF格式构建
 */
class GLTFBuilder {
public:
    explicit GLTFBuilder(const GLTFBuilderConfig& config);

    /**
     * @brief 构建GLTF模型
     *
     * 主构建函数，替代appendGeometryToModel
     *
     * @param instances 实例引用列表
     * @return 构建结果
     */
    GLTFBuildResult build(const std::vector<InstanceRef>& instances);

    /**
     * @brief 构建GLTF模型（带材质分组）
     *
     * 根据StateSet分组处理，保持材质一致性
     *
     * @param instances 实例引用列表
     * @param geometries 对应的几何体列表
     * @return 构建结果
     */
    GLTFBuildResult buildWithMaterialGrouping(
        const std::vector<InstanceRef>& instances,
        const std::vector<osg::Geometry*>& geometries
    );

private:
    GLTFBuilderConfig config_;
    ExtensionManager extMgr_;

    // 构建步骤
    bool buildGeometries(
        const std::vector<InstanceRef>& instances,
        const std::vector<osg::Geometry*>& geometries,
        std::vector<float>& positions,
        std::vector<float>& normals,
        std::vector<float>& texcoords,
        std::vector<uint32_t>& indices,
        std::vector<uint32_t>& batchIds,
        osg::BoundingBoxd& bounds
    );

    bool buildMaterials(
        const std::vector<osg::Geometry*>& geometries,
        tinygltf::Model& model,
        tinygltf::Buffer& buffer,
        std::vector<int>& materialIndices
    );

    int buildMaterialFromGeometry(
        osg::Geometry* geom,
        tinygltf::Model& model,
        tinygltf::Buffer& buffer
    );

    bool serializeToGLB(
        tinygltf::Model& model,
        std::vector<unsigned char>& outGlbData
    );

    // 应用扩展
    void applyExtensions(tinygltf::Model& model);
};

} // namespace gltf

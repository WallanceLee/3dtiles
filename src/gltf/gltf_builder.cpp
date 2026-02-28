#include "gltf_builder.h"
#include "../utils/log.h"

#include <osg/Geometry>
#include <osg/StateSet>
#include <osgDB/Registry>

namespace gltf {

GLTFBuilder::GLTFBuilder(const GLTFBuilderConfig& config)
    : config_(config) {
}

GLTFBuildResult GLTFBuilder::build(const std::vector<InstanceRef>& instances) {
    GLTFBuildResult result;

    if (instances.empty()) {
        return result;
    }

    // 创建模型
    tinygltf::Model model;
    tinygltf::Buffer buffer;
    buffer.data.reserve(1024 * 1024);  // 预分配1MB

    // 提取几何体数据
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> texcoords;
    std::vector<uint32_t> indices;
    std::vector<uint32_t> batchIds;
    osg::BoundingBoxd bounds;

    // 这里简化处理，实际需要从instances提取geometry
    // 暂时返回失败，需要外部提供geometry
    LOG_W("GLTFBuilder::build requires geometry data from instances");

    result.success = false;
    return result;
}

GLTFBuildResult GLTFBuilder::buildWithMaterialGrouping(
    const std::vector<InstanceRef>& instances,
    const std::vector<osg::Geometry*>& geometries) {

    GLTFBuildResult result;

    if (instances.empty() || geometries.empty()) {
        return result;
    }

    // 创建模型和缓冲区
    tinygltf::Model model;
    tinygltf::Buffer buffer;
    buffer.data.reserve(1024 * 1024);

    // 构建几何体数据
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> texcoords;
    std::vector<uint32_t> indices;
    std::vector<uint32_t> batchIds;
    osg::BoundingBoxd bounds;

    if (!buildGeometries(instances, geometries, positions, normals, texcoords, indices, batchIds, bounds)) {
        LOG_E("Failed to build geometries");
        return result;
    }

    // 构建材质
    std::vector<int> materialIndices;
    if (!buildMaterials(geometries, model, buffer, materialIndices)) {
        LOG_W("Failed to build materials, using default");
    }

    // 创建Primitive
    PrimitiveBuilder primBuilder;
    primBuilder.addVertices(positions);
    primBuilder.addNormals(normals);
    primBuilder.addTexcoords(texcoords);
    primBuilder.addIndices(indices);

    if (!materialIndices.empty() && materialIndices[0] >= 0) {
        primBuilder.setMaterial(materialIndices[0]);
    }

    tinygltf::Primitive primitive = primBuilder.build(model, buffer);

    // 创建Mesh
    tinygltf::Mesh mesh;
    mesh.primitives.push_back(primitive);
    model.meshes.push_back(mesh);

    // 创建Node
    tinygltf::Node node;
    node.mesh = 0;
    model.nodes.push_back(node);

    // 创建Scene
    tinygltf::Scene scene;
    scene.nodes.push_back(0);
    model.scenes.push_back(scene);
    model.defaultScene = 0;

    // 添加缓冲区
    model.buffers.push_back(buffer);

    // 应用扩展
    applyExtensions(model);

    // 序列化为GLB
    if (!serializeToGLB(model, result.glbData)) {
        LOG_E("Failed to serialize GLTF to GLB");
        return result;
    }

    result.success = true;
    result.bounds = bounds;
    result.vertexCount = positions.size() / 3;
    result.triangleCount = indices.size() / 3;

    return result;
}

bool GLTFBuilder::buildGeometries(
    const std::vector<InstanceRef>& instances,
    const std::vector<osg::Geometry*>& geometries,
    std::vector<float>& positions,
    std::vector<float>& normals,
    std::vector<float>& texcoords,
    std::vector<uint32_t>& indices,
    std::vector<uint32_t>& batchIds,
    osg::BoundingBoxd& bounds) {

    // 预分配空间
    size_t estimatedVertices = 0;
    size_t estimatedIndices = 0;
    for (size_t i = 0; i < geometries.size() && i < instances.size(); ++i) {
        if (geometries[i]) {
            estimatedVertices += geometries[i]->getVertexArray() ? geometries[i]->getVertexArray()->getNumElements() : 0;
            for (unsigned int j = 0; j < geometries[i]->getNumPrimitiveSets(); ++j) {
                estimatedIndices += geometries[i]->getPrimitiveSet(j)->getNumIndices();
            }
        }
    }

    positions.reserve(estimatedVertices * 3);
    normals.reserve(estimatedVertices * 3);
    texcoords.reserve(estimatedVertices * 2);
    indices.reserve(estimatedIndices);
    batchIds.reserve(estimatedVertices);

    // 处理每个实例
    uint32_t baseIndex = 0;
    for (size_t i = 0; i < instances.size() && i < geometries.size(); ++i) {
        const auto& inst = instances[i];
        osg::Geometry* geom = geometries[i];

        if (!geom) continue;

        // 计算法线变换矩阵
        osg::Matrixd normalMatrix = osg::utils::GeometryUtils::computeNormalMatrix(inst.matrix);

        // 提取几何体数据
        size_t vertexCount = osg::utils::GeometryUtils::extractGeometryData(
            geom,
            inst.matrix,
            normalMatrix,
            positions,
            normals,
            texcoords,
            baseIndex
        );

        // 处理索引
        for (unsigned int j = 0; j < geom->getNumPrimitiveSets(); ++j) {
            osg::PrimitiveSet* ps = geom->getPrimitiveSet(j);
            osg::utils::GeometryUtils::processPrimitiveSet(ps, baseIndex, indices);
        }

        // 添加batch ID（用于B3DM）
        for (size_t v = 0; v < vertexCount; ++v) {
            batchIds.push_back(static_cast<uint32_t>(inst.originalBatchId));
        }

        // 更新包围盒
        if (geom->getBound().valid()) {
            bounds.expandBy(geom->getBound());
        }

        baseIndex += static_cast<uint32_t>(vertexCount);
    }

    return !positions.empty() && !indices.empty();
}

bool GLTFBuilder::buildMaterials(
    const std::vector<osg::Geometry*>& geometries,
    tinygltf::Model& model,
    tinygltf::Buffer& buffer,
    std::vector<int>& materialIndices) {

    materialIndices.clear();
    materialIndices.reserve(geometries.size());

    for (osg::Geometry* geom : geometries) {
        int matIdx = buildMaterialFromGeometry(geom, model, buffer);
        materialIndices.push_back(matIdx);
    }

    return true;
}

int GLTFBuilder::buildMaterialFromGeometry(
    osg::Geometry* geom,
    tinygltf::Model& model,
    tinygltf::Buffer& buffer) {

    const osg::StateSet* stateSet = geom ? geom->getStateSet() : nullptr;

    // 提取PBR参数
    osg::utils::PBRParams pbrParams;
    osg::utils::MaterialUtils::extractPBRParams(stateSet, pbrParams);

    // 创建材质构建器
    MaterialBuilder matBuilder;
    matBuilder.setBaseColor(pbrParams.baseColor);
    matBuilder.setPBRParams(pbrParams.roughnessFactor, pbrParams.metallicFactor);
    matBuilder.setEmissiveColor({pbrParams.emissiveColor[0], pbrParams.emissiveColor[1], pbrParams.emissiveColor[2]});
    matBuilder.setDoubleSided(config_.doubleSided);
    matBuilder.setUnlit(config_.enableUnlit);

    // 处理纹理
    const osg::Texture* baseColorTex = osg::utils::MaterialUtils::getBaseColorTexture(stateSet);
    if (baseColorTex) {
        auto texResult = osg::utils::TextureUtils::processTexture(baseColorTex, config_.enableKTX2);
        if (texResult.success) {
            bool useBasisu = (texResult.mimeType == "image/ktx2");
            if (useBasisu) {
                extMgr_.useAndRequire("KHR_texture_basisu");
            }
            int texIdx = osg::utils::TextureUtils::addImageToModel(
                model, buffer, texResult.data, texResult.mimeType, useBasisu);
            matBuilder.setBaseColorTexture(texIdx);

            if (texResult.hasAlpha) {
                matBuilder.setAlphaMode("BLEND");
            }
        }
    }

    const osg::Texture* normalTex = osg::utils::MaterialUtils::getNormalTexture(stateSet);
    if (normalTex) {
        auto texResult = osg::utils::TextureUtils::processTexture(normalTex, config_.enableKTX2);
        if (texResult.success) {
            bool useBasisu = (texResult.mimeType == "image/ktx2");
            if (useBasisu) {
                extMgr_.useAndRequire("KHR_texture_basisu");
            }
            int texIdx = osg::utils::TextureUtils::addImageToModel(
                model, buffer, texResult.data, texResult.mimeType, useBasisu);
            matBuilder.setNormalTexture(texIdx);
        }
    }

    // 构建材质
    return matBuilder.build(model, extMgr_);
}

bool GLTFBuilder::serializeToGLB(
    tinygltf::Model& model,
    std::vector<unsigned char>& outGlbData) {

    tinygltf::TinyGLTF gltf;
    std::ostringstream ss;
    if (!gltf.WriteGltfSceneToStream(&model, ss, false, true)) {
        return false;
    }

    std::string glbStr = ss.str();
    outGlbData.assign(glbStr.begin(), glbStr.end());
    return !outGlbData.empty();
}

void GLTFBuilder::applyExtensions(tinygltf::Model& model) {
    extMgr_.apply(model);
}

} // namespace gltf

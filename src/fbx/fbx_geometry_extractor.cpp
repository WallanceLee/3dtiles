#include "fbx_geometry_extractor.h"
#include "fbx/core/fbx.h"  // for MeshInstanceInfo, MaterialExtensionData
#include "../extern.h"
#include "../osg/utils/material_utils.h"

namespace fbx {

// ============================================================
// GeometryExtractorBase 要求实现的辅助方法
// ============================================================

FBXExtractContext FBXGeometryExtractor::getData(
    const FBXSpatialItemAdapter* adapter) {

    FBXExtractContext ctx;
    if (!adapter) {
        return ctx;
    }

    ctx.meshInfo = adapter->getMeshInfo();
    ctx.transformIndex = adapter->getTransformIndex();
    ctx.transform = adapter->getTransform();
    ctx.nodeName = adapter->getNodeName();
    ctx.geometry = adapter->getGeometry();
    ctx.materialExtData = adapter->getMaterialExtensionData();

    return ctx;
}

size_t FBXGeometryExtractor::getItemId(const FBXSpatialItemAdapter* adapter) {
    // FBX 使用 transformIndex 作为 ID
    return static_cast<size_t>(adapter->getTransformIndex());
}

// ============================================================
// 辅助函数
// ============================================================

void FBXGeometryExtractor::copyTextureTransform(const ::TextureTransformData& src,
                                                 common::TextureTransformInfo& dst) {
    if (src.has_transform) {
        dst.hasTransform = true;
        dst.offset[0] = src.offset[0];
        dst.offset[1] = src.offset[1];
        dst.scale[0] = src.scale[0];
        dst.scale[1] = src.scale[1];
        dst.rotation = src.rotation;
        dst.texCoord = src.tex_coord;
    }
}

// ============================================================
// 数据提取实现
// ============================================================

std::vector<osg::ref_ptr<osg::Geometry>> FBXGeometryExtractor::extractImpl(
    FBXExtractContext& ctx) {

    std::vector<osg::ref_ptr<osg::Geometry>> result;

    if (!ctx.geometry) {
        LOG_W("FBXGeometryExtractor::extractImpl: no geometry found for %s", ctx.nodeName.c_str());
        return result;
    }

    // 检查原始几何体的顶点数组
    const osg::Array* vertexArray = ctx.geometry->getVertexArray();
    if (!vertexArray || vertexArray->getNumElements() == 0) {
        return result;
    }

    // 克隆几何体
    osg::ref_ptr<osg::Geometry> clonedGeom = static_cast<osg::Geometry*>(
        ctx.geometry->clone(osg::CopyOp::DEEP_COPY_ALL)
    );

    // 应用世界变换到顶点
    osg::Matrixd transform = ctx.transform;

    // 创建Y-up到Z-up的坐标转换矩阵
    // FBX是Y-up，3D Tiles是Z-up
    // 转换: (x, y, z) -> (x, -z, y)
    osg::Matrixd yupToZup(
        1,  0,  0, 0,   // 第一列: x' = 1*x + 0*y + 0*z
        0,  0,  1, 0,   // 第二列: y' = 0*x + 0*y + 1*z = z
        0, -1,  0, 0,   // 第三列: z' = 0*x - 1*y + 0*z = -y
        0,  0,  0, 1
    );

    // 组合变换: 先应用世界变换，再Y-up到Z-up
    osg::Matrixd finalTransform = transform * yupToZup;

    // 处理不同类型的顶点数组 (Vec3Array 或 Vec3dArray)
    osg::Vec3Array* vertices = dynamic_cast<osg::Vec3Array*>(clonedGeom->getVertexArray());
    osg::Vec3dArray* verticesd = dynamic_cast<osg::Vec3dArray*>(clonedGeom->getVertexArray());

    if (vertices) {
        // 单精度顶点数组
        for (auto& vertex : *vertices) {
            vertex = vertex * finalTransform;
        }
        vertices->dirty();
    } else if (verticesd) {
        // 双精度顶点数组
        for (auto& vertex : *verticesd) {
            vertex = vertex * finalTransform;
        }
        verticesd->dirty();
    } else {
        return result;
    }

    // 变换法线 (使用finalTransform)
    osg::Matrixd normalMatrix = osg::Matrixd::inverse(finalTransform);
    normalMatrix.transpose3x3(normalMatrix);

    osg::Vec3Array* normals = dynamic_cast<osg::Vec3Array*>(clonedGeom->getNormalArray());
    if (normals) {
        for (auto& normal : *normals) {
            normal = osg::Matrixd::transform3x3(normal, normalMatrix);
            normal.normalize();
        }
        normals->dirty();
    }

    result.push_back(clonedGeom);

    return result;
}

std::string FBXGeometryExtractor::getIdImpl(FBXExtractContext& ctx, size_t id) {
    (void)id;  // FBX 使用 nodeName 作为 ID
    return ctx.nodeName;
}

std::map<std::string, nlohmann::json> FBXGeometryExtractor::getAttributesImpl(
    FBXExtractContext& ctx) {

    std::map<std::string, nlohmann::json> attrs;

    // 添加节点名称
    attrs["name"] = ctx.nodeName;

    // 可以添加更多属性...

    return attrs;
}

std::shared_ptr<common::MaterialInfo> FBXGeometryExtractor::getMaterialImpl(
    FBXExtractContext& ctx) {

    if (!ctx.geometry) {
        return std::make_shared<common::MaterialInfo>();
    }

    const osg::StateSet* stateSet = ctx.geometry->getStateSet();
    if (!stateSet) {
        // 没有StateSet表示使用默认材质
        return std::make_shared<common::MaterialInfo>();
    }

    auto materialInfo = std::make_shared<common::MaterialInfo>();

    // ===== 提取基础PBR参数 =====
    osg::utils::PBRParams pbrParams;
    osg::utils::MaterialUtils::extractPBRParams(stateSet, pbrParams);

    materialInfo->baseColor = pbrParams.baseColor;
    materialInfo->roughnessFactor = pbrParams.roughnessFactor;
    materialInfo->metallicFactor = pbrParams.metallicFactor;
    materialInfo->emissiveColor = {
        pbrParams.emissiveColor[0],
        pbrParams.emissiveColor[1],
        pbrParams.emissiveColor[2]
    };
    materialInfo->aoStrength = pbrParams.aoStrength;

    // ===== 提取纹理对象 =====
    materialInfo->baseColorTexture =
        const_cast<osg::Texture*>(osg::utils::MaterialUtils::getBaseColorTexture(stateSet));
    materialInfo->normalTexture =
        const_cast<osg::Texture*>(osg::utils::MaterialUtils::getNormalTexture(stateSet));
    materialInfo->emissiveTexture =
        const_cast<osg::Texture*>(osg::utils::MaterialUtils::getEmissiveTexture(stateSet));

    // 金属度/粗糙度和遮挡纹理需要从其他纹理单元获取
    if (stateSet->getTextureAttributeList().size() > 2) {
        materialInfo->metallicRoughnessTexture =
            const_cast<osg::Texture*>(dynamic_cast<const osg::Texture*>(
                stateSet->getTextureAttribute(2, osg::StateAttribute::TEXTURE)));
    }
    if (stateSet->getTextureAttributeList().size() > 3) {
        materialInfo->occlusionTexture =
            const_cast<osg::Texture*>(dynamic_cast<const osg::Texture*>(
                stateSet->getTextureAttribute(3, osg::StateAttribute::TEXTURE)));
    }

    // ===== 提取纹理变换和Specular-Glossiness数据 =====
    if (ctx.materialExtData) {
        // 复制纹理变换
        copyTextureTransform(ctx.materialExtData->base_color_transform, materialInfo->baseColorTransform);
        copyTextureTransform(ctx.materialExtData->normal_transform, materialInfo->normalTransform);
        copyTextureTransform(ctx.materialExtData->metallic_roughness_transform, materialInfo->metallicRoughnessTransform);
        copyTextureTransform(ctx.materialExtData->occlusion_transform, materialInfo->occlusionTransform);
        copyTextureTransform(ctx.materialExtData->emissive_transform, materialInfo->emissiveTransform);

        // 复制Specular-Glossiness数据
        if (ctx.materialExtData->specular_glossiness.use_specular_glossiness) {
            materialInfo->useSpecularGlossiness = true;
            materialInfo->diffuseFactor = {
                ctx.materialExtData->specular_glossiness.diffuse_factor[0],
                ctx.materialExtData->specular_glossiness.diffuse_factor[1],
                ctx.materialExtData->specular_glossiness.diffuse_factor[2],
                ctx.materialExtData->specular_glossiness.diffuse_factor[3]
            };
            materialInfo->specularFactor = {
                ctx.materialExtData->specular_glossiness.specular_factor[0],
                ctx.materialExtData->specular_glossiness.specular_factor[1],
                ctx.materialExtData->specular_glossiness.specular_factor[2]
            };
            materialInfo->glossinessFactor = ctx.materialExtData->specular_glossiness.glossiness_factor;
        }
    }

    return materialInfo;
}

} // namespace fbx

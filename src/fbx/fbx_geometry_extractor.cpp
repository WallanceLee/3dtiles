#include "fbx_geometry_extractor.h"
#include "fbx_spatial_item_adapter.h"
#include "../extern.h"
#include "../osg/utils/material_utils.h"

namespace fbx {

namespace {
    /**
     * @brief 复制纹理变换数据
     */
    void copyTextureTransform(const ::TextureTransformData& src,
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
}

std::vector<osg::ref_ptr<osg::Geometry>> FBXGeometryExtractor::extract(
    const spatial::core::SpatialItem* item) {

    std::vector<osg::ref_ptr<osg::Geometry>> result;

    // 尝试转换为FBXSpatialItemAdapter
    const auto* fbxItem = dynamic_cast<const FBXSpatialItemAdapter*>(item);
    if (!fbxItem) {
        LOG_W("FBXGeometryExtractor: item is not FBXSpatialItemAdapter");
        return result;
    }

    // 获取几何体
    const osg::Geometry* geom = fbxItem->getGeometry();
    if (!geom) {
        LOG_W("FBXGeometryExtractor: no geometry found for %s", fbxItem->getNodeName().c_str());
        return result;
    }

    // 检查原始几何体的顶点数组
    const osg::Array* vertexArray = geom->getVertexArray();
    if (!vertexArray || vertexArray->getNumElements() == 0) {
        return result;
    }

    // 克隆几何体
    osg::ref_ptr<osg::Geometry> clonedGeom = static_cast<osg::Geometry*>(
        geom->clone(osg::CopyOp::DEEP_COPY_ALL)
    );

    // 应用世界变换到顶点
    osg::Matrixd transform = fbxItem->getTransform();

    // 创建Y-up到Z-up的坐标转换矩阵
    // FBX是Y-up，3D Tiles是Z-up
    // 转换: (x, y, z) -> (x, -z, y)
    // 注意: OSG使用行主序矩阵，但构造时是列主序
    osg::Matrixd yupToZup(
        1,  0,  0, 0,   // 第一列: x' = 1*x + 0*y + 0*z
        0,  0,  1, 0,   // 第二列: y' = 0*x + 0*y + 1*z = z
        0, -1,  0, 0,   // 第三列: z' = 0*x - 1*y + 0*z = -y
        0,  0,  0, 1
    );

    // 组合变换: 先应用世界变换，再Y-up到Z-up
    // OSG是右乘: v' = v * transform * yupToZup
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

std::string FBXGeometryExtractor::getId(const spatial::core::SpatialItem* item) {
    const auto* fbxItem = dynamic_cast<const FBXSpatialItemAdapter*>(item);
    if (!fbxItem) {
        return "";
    }

    return fbxItem->getNodeName();
}

std::map<std::string, nlohmann::json> FBXGeometryExtractor::getAttributes(
    const spatial::core::SpatialItem* item) {

    std::map<std::string, nlohmann::json> attrs;

    const auto* fbxItem = dynamic_cast<const FBXSpatialItemAdapter*>(item);
    if (!fbxItem) {
        return attrs;
    }

    // 添加节点名称
    attrs["name"] = fbxItem->getNodeName();

    // 可以添加更多属性...

    return attrs;
}

std::shared_ptr<common::MaterialInfo> FBXGeometryExtractor::getMaterial(
    const spatial::core::SpatialItem* item) {

    const auto* fbxItem = dynamic_cast<const FBXSpatialItemAdapter*>(item);
    if (!fbxItem) {
        LOG_W("FBXGeometryExtractor::getMaterial: item is not FBXSpatialItemAdapter");
        return std::make_shared<common::MaterialInfo>();
    }

    // 获取几何体的StateSet
    const osg::Geometry* geom = fbxItem->getGeometry();
    if (!geom) {
        return std::make_shared<common::MaterialInfo>();
    }

    const osg::StateSet* stateSet = geom->getStateSet();
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
    // 根据FBXLoader的实现，它们可能在特定的纹理单元
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
    const MaterialExtensionData* extData = fbxItem->getMaterialExtensionData();
    if (extData) {
        // 复制纹理变换
        copyTextureTransform(extData->base_color_transform, materialInfo->baseColorTransform);
        copyTextureTransform(extData->normal_transform, materialInfo->normalTransform);
        copyTextureTransform(extData->metallic_roughness_transform, materialInfo->metallicRoughnessTransform);
        copyTextureTransform(extData->occlusion_transform, materialInfo->occlusionTransform);
        copyTextureTransform(extData->emissive_transform, materialInfo->emissiveTransform);

        // 复制Specular-Glossiness数据
        if (extData->specular_glossiness.use_specular_glossiness) {
            materialInfo->useSpecularGlossiness = true;
            materialInfo->diffuseFactor = {
                extData->specular_glossiness.diffuse_factor[0],
                extData->specular_glossiness.diffuse_factor[1],
                extData->specular_glossiness.diffuse_factor[2],
                extData->specular_glossiness.diffuse_factor[3]
            };
            materialInfo->specularFactor = {
                extData->specular_glossiness.specular_factor[0],
                extData->specular_glossiness.specular_factor[1],
                extData->specular_glossiness.specular_factor[2]
            };
            materialInfo->glossinessFactor = extData->specular_glossiness.glossiness_factor;
        }
    }

    return materialInfo;
}

} // namespace fbx

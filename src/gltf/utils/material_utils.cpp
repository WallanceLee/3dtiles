#include "material_utils.h"
#include <osg/Texture>

namespace gltf {
namespace utils {

void MaterialUtils::extractPBRParams(
    const osg::StateSet* stateSet,
    PBRParams& outParams) {

    // 重置为默认值
    outParams.baseColor = {1.0, 1.0, 1.0, 1.0};
    outParams.emissiveColor[0] = 0.0;
    outParams.emissiveColor[1] = 0.0;
    outParams.emissiveColor[2] = 0.0;
    outParams.roughnessFactor = 1.0f;
    outParams.metallicFactor = 0.0f;
    outParams.aoStrength = 1.0f;

    if (!stateSet) {
        return;
    }

    // 从Material提取颜色
    const osg::Material* material = dynamic_cast<const osg::Material*>(
        stateSet->getAttribute(osg::StateAttribute::MATERIAL));
    if (material) {
        extractColorsFromMaterial(material, outParams);
    }

    // 从Uniform提取PBR参数
    extractUniformsFromStateSet(stateSet, outParams);
}

bool MaterialUtils::hasMaterial(const osg::StateSet* stateSet) {
    if (!stateSet) {
        return false;
    }

    // 检查是否有Material属性
    const osg::Material* material = dynamic_cast<const osg::Material*>(
        stateSet->getAttribute(osg::StateAttribute::MATERIAL));
    if (material) {
        return true;
    }

    // 检查是否有纹理
    for (unsigned int i = 0; i < 8; ++i) {
        const osg::Texture* texture = dynamic_cast<const osg::Texture*>(
            stateSet->getTextureAttribute(i, osg::StateAttribute::TEXTURE));
        if (texture) {
            return true;
        }
    }

    return false;
}

const osg::Texture* MaterialUtils::getBaseColorTexture(const osg::StateSet* stateSet) {
    if (!stateSet) {
        return nullptr;
    }

    // 纹理单元0：基础颜色纹理
    return dynamic_cast<const osg::Texture*>(
        stateSet->getTextureAttribute(0, osg::StateAttribute::TEXTURE));
}

const osg::Texture* MaterialUtils::getNormalTexture(const osg::StateSet* stateSet) {
    if (!stateSet) {
        return nullptr;
    }

    // 纹理单元1：法线纹理
    return dynamic_cast<const osg::Texture*>(
        stateSet->getTextureAttribute(1, osg::StateAttribute::TEXTURE));
}

const osg::Texture* MaterialUtils::getEmissiveTexture(const osg::StateSet* stateSet) {
    if (!stateSet) {
        return nullptr;
    }

    // 纹理单元4：自发光纹理
    return dynamic_cast<const osg::Texture*>(
        stateSet->getTextureAttribute(4, osg::StateAttribute::TEXTURE));
}

void MaterialUtils::extractColorsFromMaterial(
    const osg::Material* material,
    PBRParams& params) {

    if (!material) {
        return;
    }

    // 提取diffuse颜色作为基础颜色
    osg::Vec4 diffuse = material->getDiffuse(osg::Material::FRONT);
    params.baseColor = {
        static_cast<double>(diffuse.r()),
        static_cast<double>(diffuse.g()),
        static_cast<double>(diffuse.b()),
        static_cast<double>(diffuse.a())
    };

    // 提取emission颜色作为自发光颜色
    osg::Vec4 emission = material->getEmission(osg::Material::FRONT);
    params.emissiveColor[0] = emission.r();
    params.emissiveColor[1] = emission.g();
    params.emissiveColor[2] = emission.b();
}

void MaterialUtils::extractUniformsFromStateSet(
    const osg::StateSet* stateSet,
    PBRParams& params) {

    if (!stateSet) {
        return;
    }

    // 提取粗糙度
    const osg::Uniform* roughnessUniform = stateSet->getUniform("roughnessFactor");
    if (roughnessUniform) {
        roughnessUniform->get(params.roughnessFactor);
    }

    // 提取金属度
    const osg::Uniform* metallicUniform = stateSet->getUniform("metallicFactor");
    if (metallicUniform) {
        metallicUniform->get(params.metallicFactor);
    }

    // 提取AO强度
    const osg::Uniform* aoUniform = stateSet->getUniform("aoStrength");
    if (aoUniform) {
        aoUniform->get(params.aoStrength);
    }
}

} // namespace utils
} // namespace gltf

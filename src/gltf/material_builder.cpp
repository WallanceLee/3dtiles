#include "material_builder.h"

namespace gltf {

MaterialBuilder::MaterialBuilder()
    : baseColor_({1.0, 1.0, 1.0, 1.0})
    , roughnessFactor_(1.0f)
    , metallicFactor_(0.0f)
    , baseColorTexture_(-1)
    , normalTexture_(-1)
    , emissiveTexture_(-1)
    , emissiveColor_({0.0, 0.0, 0.0})
    , unlit_(false)
    , doubleSided_(true)
    , alphaMode_("OPAQUE") {
}

void MaterialBuilder::setBaseColor(const std::vector<double>& color) {
    baseColor_ = color;
    if (baseColor_.size() < 4) {
        baseColor_.resize(4, 1.0);
    }
}

void MaterialBuilder::setBaseColorTexture(int textureIndex) {
    baseColorTexture_ = textureIndex;
}

void MaterialBuilder::setNormalTexture(int textureIndex) {
    normalTexture_ = textureIndex;
}

void MaterialBuilder::setEmissiveTexture(int textureIndex) {
    emissiveTexture_ = textureIndex;
}

void MaterialBuilder::setPBRParams(float roughness, float metallic) {
    roughnessFactor_ = roughness;
    metallicFactor_ = metallic;
}

void MaterialBuilder::setEmissiveColor(const std::vector<double>& color) {
    emissiveColor_ = color;
    if (emissiveColor_.size() < 3) {
        emissiveColor_.resize(3, 0.0);
    }
}

void MaterialBuilder::setUnlit(bool unlit) {
    unlit_ = unlit;
}

void MaterialBuilder::setDoubleSided(bool doubleSided) {
    doubleSided_ = doubleSided;
}

void MaterialBuilder::setAlphaMode(const std::string& alphaMode) {
    alphaMode_ = alphaMode;
}

void MaterialBuilder::clear() {
    baseColor_ = {1.0, 1.0, 1.0, 1.0};
    roughnessFactor_ = 1.0f;
    metallicFactor_ = 0.0f;
    baseColorTexture_ = -1;
    normalTexture_ = -1;
    emissiveTexture_ = -1;
    emissiveColor_ = {0.0, 0.0, 0.0};
    unlit_ = false;
    doubleSided_ = true;
    alphaMode_ = "OPAQUE";
}

int MaterialBuilder::build(tinygltf::Model& model, ExtensionManager& extMgr) {
    tinygltf::Material material;
    material.name = "Material";
    material.doubleSided = doubleSided_;
    material.alphaMode = alphaMode_;

    // 设置PBR参数
    material.pbrMetallicRoughness.baseColorFactor = baseColor_;
    material.pbrMetallicRoughness.roughnessFactor = roughnessFactor_;
    material.pbrMetallicRoughness.metallicFactor = metallicFactor_;

    // 设置基础颜色纹理
    if (baseColorTexture_ >= 0) {
        material.pbrMetallicRoughness.baseColorTexture.index = baseColorTexture_;
    }

    // 设置法线纹理
    if (normalTexture_ >= 0) {
        material.normalTexture.index = normalTexture_;
    }

    // 设置自发光
    if (emissiveTexture_ >= 0) {
        material.emissiveTexture.index = emissiveTexture_;
    }
    if (emissiveColor_[0] > 0.0 || emissiveColor_[1] > 0.0 || emissiveColor_[2] > 0.0) {
        material.emissiveFactor = emissiveColor_;
    }

    // 设置Unlit扩展
    if (unlit_) {
        material.extensions["KHR_materials_unlit"] = tinygltf::Value(tinygltf::Value::Object());
        extMgr.use("KHR_materials_unlit");
    }

    int index = static_cast<int>(model.materials.size());
    model.materials.push_back(material);
    return index;
}

} // namespace gltf

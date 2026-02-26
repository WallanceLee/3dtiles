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

void MaterialBuilder::setMetallicRoughnessTexture(int textureIndex) {
    metallicRoughnessTexture_ = textureIndex;
}

void MaterialBuilder::setOcclusionTexture(int textureIndex) {
    occlusionTexture_ = textureIndex;
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

void MaterialBuilder::setAlphaCutoff(float alphaCutoff) {
    alphaCutoff_ = alphaCutoff;
}

void MaterialBuilder::setBaseColorTextureTransform(const extensions::TextureTransform& transform) {
    baseColorTransform_ = transform;
}

void MaterialBuilder::setNormalTextureTransform(const extensions::TextureTransform& transform) {
    normalTransform_ = transform;
}

void MaterialBuilder::setEmissiveTextureTransform(const extensions::TextureTransform& transform) {
    emissiveTransform_ = transform;
}

void MaterialBuilder::setMetallicRoughnessTextureTransform(const extensions::TextureTransform& transform) {
    metallicRoughnessTransform_ = transform;
}

void MaterialBuilder::setOcclusionTextureTransform(const extensions::TextureTransform& transform) {
    occlusionTransform_ = transform;
}

void MaterialBuilder::setOcclusionStrength(float strength) {
    occlusionStrength_ = strength;
}

void MaterialBuilder::setSpecularGlossiness(const extensions::SpecularGlossiness& sg) {
    specularGlossiness_ = sg;
}

void MaterialBuilder::clear() {
    baseColor_ = {1.0, 1.0, 1.0, 1.0};
    roughnessFactor_ = 1.0f;
    metallicFactor_ = 0.0f;
    baseColorTexture_ = -1;
    normalTexture_ = -1;
    emissiveTexture_ = -1;
    metallicRoughnessTexture_ = -1;
    occlusionTexture_ = -1;
    baseColorTransform_.reset();
    normalTransform_.reset();
    emissiveTransform_.reset();
    metallicRoughnessTransform_.reset();
    occlusionTransform_.reset();
    emissiveColor_ = {0.0, 0.0, 0.0};
    unlit_ = false;
    doubleSided_ = true;
    alphaMode_ = "OPAQUE";
    alphaCutoff_ = 0.5f;
    occlusionStrength_ = 1.0f;
    specularGlossiness_.reset();
}

int MaterialBuilder::build(tinygltf::Model& model, ExtensionManager& extMgr) {
    tinygltf::Material material;
    material.name = "Material";
    material.doubleSided = doubleSided_;
    material.alphaMode = alphaMode_;
    material.alphaCutoff = alphaCutoff_;

    // 设置PBR参数
    material.pbrMetallicRoughness.baseColorFactor = baseColor_;
    material.pbrMetallicRoughness.roughnessFactor = roughnessFactor_;
    material.pbrMetallicRoughness.metallicFactor = metallicFactor_;

    // 设置基础颜色纹理
    if (baseColorTexture_ >= 0) {
        material.pbrMetallicRoughness.baseColorTexture.index = baseColorTexture_;
        // 应用纹理变换
        if (baseColorTransform_) {
            extensions::applyTextureTransform(material, "baseColorTexture", *baseColorTransform_, extMgr);
        }
    }

    // 设置金属度粗糙度纹理
    if (metallicRoughnessTexture_ >= 0) {
        material.pbrMetallicRoughness.metallicRoughnessTexture.index = metallicRoughnessTexture_;
        if (metallicRoughnessTransform_) {
            extensions::applyTextureTransform(material, "metallicRoughnessTexture", *metallicRoughnessTransform_, extMgr);
        }
    }

    // 设置法线纹理
    if (normalTexture_ >= 0) {
        material.normalTexture.index = normalTexture_;
        if (normalTransform_) {
            extensions::applyTextureTransform(material, "normalTexture", *normalTransform_, extMgr);
        }
    }

    // 设置环境光遮蔽纹理
    if (occlusionTexture_ >= 0) {
        material.occlusionTexture.index = occlusionTexture_;
        material.occlusionTexture.strength = occlusionStrength_;
        if (occlusionTransform_) {
            extensions::applyTextureTransform(material, "occlusionTexture", *occlusionTransform_, extMgr);
        }
    }

    // 设置自发光
    if (emissiveTexture_ >= 0) {
        material.emissiveTexture.index = emissiveTexture_;
        if (emissiveTransform_) {
            extensions::applyTextureTransform(material, "emissiveTexture", *emissiveTransform_, extMgr);
        }
    }
    if (emissiveColor_[0] > 0.0 || emissiveColor_[1] > 0.0 || emissiveColor_[2] > 0.0) {
        material.emissiveFactor = emissiveColor_;
    }

    // 设置Unlit扩展
    if (unlit_) {
        material.extensions["KHR_materials_unlit"] = tinygltf::Value(tinygltf::Value::Object());
        extMgr.use("KHR_materials_unlit");
    }

    // 应用Specular-Glossiness扩展
    if (specularGlossiness_) {
        extensions::applySpecularGlossiness(material, *specularGlossiness_, extMgr);
    }

    int index = static_cast<int>(model.materials.size());
    model.materials.push_back(material);
    return index;
}

} // namespace gltf

#pragma once

#include <array>
#include <tiny_gltf.h>
#include "../extension_manager.h"

namespace gltf_writer::extensions {

struct TextureTransform {
    std::array<float, 2> offset = {0.0f, 0.0f};
    float rotation = 0.0f;
    std::array<float, 2> scale = {1.0f, 1.0f};
    int tex_coord = 0;

    static TextureTransform Identity() { return {}; }
    
    static TextureTransform WithOffset(float u, float v) {
        TextureTransform t;
        t.offset = {u, v};
        return t;
    }
    
    static TextureTransform WithScale(float u, float v) {
        TextureTransform t;
        t.scale = {u, v};
        return t;
    }
    
    static TextureTransform WithRotation(float radians) {
        TextureTransform t;
        t.rotation = radians;
        return t;
    }

    static TextureTransform WithOffsetAndScale(float offset_u, float offset_v,
                                                float scale_u, float scale_v) {
        TextureTransform t;
        t.offset = {offset_u, offset_v};
        t.scale = {scale_u, scale_v};
        return t;
    }
};

inline void applyTextureTransform(tinygltf::Material& material,
                                  const std::string& texture_key,
                                  const TextureTransform& transform,
                                  ExtensionManager& ext_mgr) {
    tinygltf::Value::Object ext_obj;
    ext_obj["offset"] = tinygltf::Value(tinygltf::Value::Array{
        tinygltf::Value(static_cast<double>(transform.offset[0])),
        tinygltf::Value(static_cast<double>(transform.offset[1]))
    });
    ext_obj["rotation"] = tinygltf::Value(static_cast<double>(transform.rotation));
    ext_obj["scale"] = tinygltf::Value(tinygltf::Value::Array{
        tinygltf::Value(static_cast<double>(transform.scale[0])),
        tinygltf::Value(static_cast<double>(transform.scale[1]))
    });
    if (transform.tex_coord != 0) {
        ext_obj["texCoord"] = tinygltf::Value(transform.tex_coord);
    }

    if (texture_key == "baseColorTexture") {
        material.pbrMetallicRoughness.baseColorTexture.extensions["KHR_texture_transform"] = 
            tinygltf::Value(ext_obj);
    } else if (texture_key == "normalTexture") {
        material.normalTexture.extensions["KHR_texture_transform"] = tinygltf::Value(ext_obj);
    } else if (texture_key == "emissiveTexture") {
        material.emissiveTexture.extensions["KHR_texture_transform"] = tinygltf::Value(ext_obj);
    } else if (texture_key == "occlusionTexture") {
        material.occlusionTexture.extensions["KHR_texture_transform"] = tinygltf::Value(ext_obj);
    } else if (texture_key == "metallicRoughnessTexture") {
        material.pbrMetallicRoughness.metallicRoughnessTexture.extensions["KHR_texture_transform"] = 
            tinygltf::Value(ext_obj);
    }

    ext_mgr.use("KHR_texture_transform");
}

}

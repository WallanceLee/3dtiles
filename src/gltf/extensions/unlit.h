#pragma once

#include <tiny_gltf.h>
#include "../extension_manager.h"

namespace gltf::extensions {

inline void applyUnlit(tinygltf::Material& material, ExtensionManager& ext_mgr) {
    material.extensions["KHR_materials_unlit"] = tinygltf::Value(tinygltf::Value::Object());
    ext_mgr.use("KHR_materials_unlit");
}

inline bool hasUnlit(const tinygltf::Material& material) {
    return material.extensions.count("KHR_materials_unlit") > 0;
}

} // namespace gltf::extensions

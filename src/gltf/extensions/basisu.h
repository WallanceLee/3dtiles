#pragma once

#include <tiny_gltf.h>
#include "../extension_manager.h"

namespace gltf::extensions {

inline void applyBasisu(tinygltf::Texture& texture, int source_index, ExtensionManager& ext_mgr) {
    tinygltf::Value::Object ext_obj;
    ext_obj["source"] = tinygltf::Value(source_index);
    texture.extensions["KHR_texture_basisu"] = tinygltf::Value(ext_obj);
    texture.source = -1;
    ext_mgr.useAndRequire("KHR_texture_basisu");
}

inline bool hasBasisu(const tinygltf::Texture& texture) {
    return texture.extensions.count("KHR_texture_basisu") > 0;
}

inline int getBasisuSource(const tinygltf::Texture& texture) {
    auto it = texture.extensions.find("KHR_texture_basisu");
    if (it != texture.extensions.end()) {
        const auto& ext = it->second;
        if (ext.IsObject()) {
            auto src_it = ext.Get<tinygltf::Value::Object>().find("source");
            if (src_it != ext.Get<tinygltf::Value::Object>().end() && src_it->second.IsInt()) {
                return src_it->second.Get<int>();
            }
        }
    }
    return -1;
}

} // namespace gltf::extensions

#pragma once

#include <unordered_map>
#include <tiny_gltf.h>
#include "../extension_manager.h"

namespace gltf::extensions {

struct DracoCompression {
    int buffer_view = -1;
    std::unordered_map<std::string, int> attributes;

    static DracoCompression FromBufferView(int bv, 
                                           const std::unordered_map<std::string, int>& attrs) {
        DracoCompression dc;
        dc.buffer_view = bv;
        dc.attributes = attrs;
        return dc;
    }

    DracoCompression& addAttribute(const std::string& name, int id) {
        attributes[name] = id;
        return *this;
    }
};

inline void applyDracoCompression(tinygltf::Primitive& primitive,
                                  const DracoCompression& draco,
                                  ExtensionManager& ext_mgr) {
    tinygltf::Value::Object ext_obj;
    ext_obj["bufferView"] = tinygltf::Value(draco.buffer_view);

    tinygltf::Value::Object attrs_obj;
    for (const auto& [name, id] : draco.attributes) {
        attrs_obj[name] = tinygltf::Value(id);
    }
    ext_obj["attributes"] = tinygltf::Value(attrs_obj);

    primitive.extensions["KHR_draco_mesh_compression"] = tinygltf::Value(ext_obj);
    ext_mgr.useAndRequire("KHR_draco_mesh_compression");
}

inline bool hasDracoCompression(const tinygltf::Primitive& primitive) {
    return primitive.extensions.count("KHR_draco_mesh_compression") > 0;
}

} // namespace gltf::extensions

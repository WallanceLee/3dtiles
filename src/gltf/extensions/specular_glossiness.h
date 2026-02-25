#pragma once

#include <array>
#include <tiny_gltf.h>
#include "../extension_manager.h"

namespace gltf::extensions {

struct SpecularGlossiness {
    std::array<double, 4> diffuse_factor = {1.0, 1.0, 1.0, 1.0};
    std::array<double, 3> specular_factor = {1.0, 1.0, 1.0};
    double glossiness_factor = 1.0;

    int diffuse_texture = -1;
    int specular_glossiness_texture = -1;

    static SpecularGlossiness Default() { return {}; }
    
    static SpecularGlossiness FromDiffuse(const std::array<double, 4>& color) {
        SpecularGlossiness sg;
        sg.diffuse_factor = color;
        return sg;
    }
    
    static SpecularGlossiness FromDiffuse(double r, double g, double b, double a = 1.0) {
        SpecularGlossiness sg;
        sg.diffuse_factor = {r, g, b, a};
        return sg;
    }
    
    static SpecularGlossiness FromSpecular(const std::array<double, 3>& specular, 
                                           double glossiness) {
        SpecularGlossiness sg;
        sg.specular_factor = specular;
        sg.glossiness_factor = glossiness;
        return sg;
    }

    static SpecularGlossiness FromSpecular(double sr, double sg, double sb,
                                           double glossiness) {
        SpecularGlossiness spec;
        spec.specular_factor = {sr, sg, sb};
        spec.glossiness_factor = glossiness;
        return spec;
    }

    static SpecularGlossiness Full(double dr, double dg, double db, double da,
                                   double sr, double sg, double sb,
                                   double glossiness) {
        SpecularGlossiness spec;
        spec.diffuse_factor = {dr, dg, db, da};
        spec.specular_factor = {sr, sg, sb};
        spec.glossiness_factor = glossiness;
        return spec;
    }
};

inline void applySpecularGlossiness(tinygltf::Material& material,
                                    const SpecularGlossiness& sg,
                                    ExtensionManager& ext_mgr) {
    tinygltf::Value::Object ext_obj;
    
    ext_obj["diffuseFactor"] = tinygltf::Value(tinygltf::Value::Array{
        tinygltf::Value(sg.diffuse_factor[0]),
        tinygltf::Value(sg.diffuse_factor[1]),
        tinygltf::Value(sg.diffuse_factor[2]),
        tinygltf::Value(sg.diffuse_factor[3])
    });
    
    ext_obj["specularFactor"] = tinygltf::Value(tinygltf::Value::Array{
        tinygltf::Value(sg.specular_factor[0]),
        tinygltf::Value(sg.specular_factor[1]),
        tinygltf::Value(sg.specular_factor[2])
    });
    
    ext_obj["glossinessFactor"] = tinygltf::Value(sg.glossiness_factor);

    if (sg.diffuse_texture >= 0) {
        tinygltf::Value::Object tex_info;
        tex_info["index"] = tinygltf::Value(sg.diffuse_texture);
        ext_obj["diffuseTexture"] = tinygltf::Value(tex_info);
    }

    if (sg.specular_glossiness_texture >= 0) {
        tinygltf::Value::Object tex_info;
        tex_info["index"] = tinygltf::Value(sg.specular_glossiness_texture);
        ext_obj["specularGlossinessTexture"] = tinygltf::Value(tex_info);
    }

    material.extensions["KHR_materials_pbrSpecularGlossiness"] = tinygltf::Value(ext_obj);
    ext_mgr.use("KHR_materials_pbrSpecularGlossiness");
}

} // namespace gltf::extensions

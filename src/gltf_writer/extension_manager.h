#pragma once

#include <set>
#include <string>
#include <vector>
#include <algorithm>
#include <tiny_gltf.h>

namespace gltf_writer {

class ExtensionManager {
public:
    ExtensionManager() = default;

    void use(const std::string& name) { used_.insert(name); }
    void require(const std::string& name) { required_.insert(name); }

    void useAndRequire(const std::string& name) {
        use(name);
        require(name);
    }

    void apply(tinygltf::Model& model) const {
        for (const auto& ext : used_) {
            if (std::find(model.extensionsUsed.begin(), 
                         model.extensionsUsed.end(), ext) == model.extensionsUsed.end()) {
                model.extensionsUsed.push_back(ext);
            }
        }
        for (const auto& ext : required_) {
            if (std::find(model.extensionsRequired.begin(),
                         model.extensionsRequired.end(), ext) == model.extensionsRequired.end()) {
                model.extensionsRequired.push_back(ext);
            }
        }
    }

    bool isUsed(const std::string& name) const { return used_.count(name) > 0; }
    bool isRequired(const std::string& name) const { return required_.count(name) > 0; }

    const std::set<std::string>& used() const { return used_; }
    const std::set<std::string>& required() const { return required_; }

    void clear() {
        used_.clear();
        required_.clear();
    }

private:
    std::set<std::string> used_;
    std::set<std::string> required_;
};

}

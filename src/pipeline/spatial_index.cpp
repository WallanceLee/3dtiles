#include "spatial_index.h"
#include <stdexcept>

namespace pipeline {

auto SpatialIndexFactory::Instance() noexcept -> SpatialIndexFactory& {
    static SpatialIndexFactory instance;
    return instance;
}

void SpatialIndexFactory::Register(const std::string& type, SpatialIndexCreator creator) {
    creators_[type] = std::move(creator);
}

auto SpatialIndexFactory::Create(const std::string& type) const -> SpatialIndexPtr {
    auto it = creators_.find(type);
    if (it != creators_.end()) {
        return it->second();
    }
    return nullptr;
}

auto SpatialIndexFactory::IsRegistered(const std::string& type) const noexcept -> bool {
    return creators_.find(type) != creators_.end();
}

} // namespace pipeline

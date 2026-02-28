#include "tileset_builder.h"
#include <stdexcept>

namespace pipeline {

auto TilesetBuilderFactory::Instance() noexcept -> TilesetBuilderFactory& {
    static TilesetBuilderFactory instance;
    return instance;
}

void TilesetBuilderFactory::Register(const std::string& type, TilesetBuilderCreator creator) {
    creators_[type] = std::move(creator);
}

auto TilesetBuilderFactory::Create(const std::string& type) const -> TilesetBuilderPtr {
    auto it = creators_.find(type);
    if (it != creators_.end()) {
        return it->second();
    }
    return nullptr;
}

auto TilesetBuilderFactory::IsRegistered(const std::string& type) const noexcept -> bool {
    return creators_.find(type) != creators_.end();
}

} // namespace pipeline

#include "data_source.h"
#include <stdexcept>

namespace pipeline {

auto DataSourceFactory::Instance() noexcept -> DataSourceFactory& {
    static DataSourceFactory instance;
    return instance;
}

void DataSourceFactory::Register(const std::string& type, DataSourceCreator creator) {
    creators_[type] = std::move(creator);
}

auto DataSourceFactory::Create(const std::string& type) const -> DataSourcePtr {
    auto it = creators_.find(type);
    if (it != creators_.end()) {
        return it->second();
    }
    return nullptr;
}

auto DataSourceFactory::IsRegistered(const std::string& type) const noexcept -> bool {
    return creators_.find(type) != creators_.end();
}

} // namespace pipeline

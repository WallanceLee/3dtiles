#include "pipeline_factory.h"
#include <algorithm>

namespace pipeline {

// ============================================
// 工厂单例实现
// ============================================

PipelineFactoryV2& PipelineFactoryV2::Instance() noexcept {
    static PipelineFactoryV2 instance;
    return instance;
}

// ============================================
// 注册和创建
// ============================================

void PipelineFactoryV2::Register(PipelineCreatorPtr creator) {
    if (!creator) {
        return;
    }
    const auto& metadata = creator->GetMetadata();
    creators_[metadata.type_name] = std::move(creator);
}

ConversionPipelinePtr PipelineFactoryV2::Create(const std::string& type) const {
    auto it = creators_.find(type);
    if (it != creators_.end()) {
        return it->second->Create();
    }
    return nullptr;
}

// ============================================
// 元数据查询
// ============================================

const PipelineMetadata* PipelineFactoryV2::GetMetadata(const std::string& type) const {
    auto it = creators_.find(type);
    if (it != creators_.end()) {
        return &it->second->GetMetadata();
    }
    return nullptr;
}

std::vector<std::string> PipelineFactoryV2::ListRegisteredTypes() const {
    std::vector<std::string> types;
    types.reserve(creators_.size());
    for (const auto& [type, _] : creators_) {
        types.push_back(type);
    }
    return types;
}

std::string PipelineFactoryV2::FindPipelineForExtension(const std::string& ext) const {
    for (const auto& [type, creator] : creators_) {
        if (creator->GetMetadata().SupportsExtension(ext)) {
            return type;
        }
    }
    return "";
}

// ============================================
// 参数创建
// ============================================

std::unique_ptr<DataSourceSpecificParams>
PipelineFactoryV2::CreateDefaultParams(const std::string& type) const {
    auto it = creators_.find(type);
    if (it != creators_.end()) {
        return it->second->CreateDefaultParams();
    }
    return nullptr;
}

// ============================================
// 状态查询
// ============================================

bool PipelineFactoryV2::IsRegistered(const std::string& type) const {
    return creators_.find(type) != creators_.end();
}

void PipelineFactoryV2::Unregister(const std::string& type) {
    creators_.erase(type);
}

void PipelineFactoryV2::Clear() {
    creators_.clear();
}

} // namespace pipeline

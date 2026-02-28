#include "conversion_pipeline.h"
#include <iostream>
#include <stdexcept>

namespace pipeline {

auto PipelineFactory::Instance() noexcept -> PipelineFactory& {
    static PipelineFactory instance;
    return instance;
}

void PipelineFactory::Register(const std::string& type, PipelineCreator creator) {
    creators_[type] = std::move(creator);
}

auto PipelineFactory::Create(const std::string& type) const -> ConversionPipelinePtr {
    auto it = creators_.find(type);
    if (it != creators_.end()) {
        return it->second();
    }
    return nullptr;
}

auto PipelineFactory::IsRegistered(const std::string& type) const noexcept -> bool {
    return creators_.find(type) != creators_.end();
}

} // namespace pipeline

// C API 实现
extern "C" {

bool convert_with_pipeline(const pipeline::ConversionParams* params) {
    if (!params) {
        std::cerr << "[convert_with_pipeline] params is null" << std::endl;
        return false;
    }

    // 使用工厂创建管道
    auto pipeline = pipeline::PipelineFactory::Instance().Create(params->source_type);
    if (!pipeline) {
        std::cerr << "[convert_with_pipeline] Failed to create pipeline for type: "
                  << params->source_type << std::endl;
        return false;
    }

    // 执行转换
    auto result = pipeline->Convert(*params);
    return result.success;
}

} // extern "C"

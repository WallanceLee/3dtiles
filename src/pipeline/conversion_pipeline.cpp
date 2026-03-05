#include "conversion_pipeline.h"
#include "pipeline_factory.h"
#include <iostream>
#include <stdexcept>

namespace pipeline {
// 旧工厂实现已移除，使用 PipelineFactoryV2
} // namespace pipeline

// ============================================
// C API 实现
// ============================================

extern "C" {

bool convert_with_pipeline(const pipeline::ConversionParams* params) {
    if (!params) {
        std::cerr << "[convert_with_pipeline] params is null" << std::endl;
        return false;
    }

    // 验证参数
    std::string error_msg;
    if (!params->Validate(error_msg)) {
        std::cerr << "[convert_with_pipeline] Validation failed: " << error_msg << std::endl;
        return false;
    }

    // 检查是否需要迁移旧参数
    if (params->NeedsMigration()) {
        std::cerr << "[convert_with_pipeline] Warning: Using deprecated params format, "
                  << "please migrate to new format" << std::endl;
        // 注意：这里不能直接修改 const 参数，实际使用时应先复制
    }

    // 使用 PipelineFactoryV2 创建管道
    auto& factory = pipeline::PipelineFactoryV2::Instance();
    auto pipeline = factory.Create(params->source_type);

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

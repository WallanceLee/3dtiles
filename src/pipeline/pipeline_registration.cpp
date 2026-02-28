#include "conversion_pipeline.h"
#include "shapefile_pipeline.h"
#include "fbx_pipeline.h"
#include <iostream>

namespace pipeline {

// 注册管道到工厂
// 使用静态初始化确保在 main 之前注册

struct PipelineRegistrar {
    PipelineRegistrar() {
        // 注册 Shapefile 管道
        PipelineFactory::Instance().Register("shapefile", []() -> ConversionPipelinePtr {
            return std::make_unique<ShapefilePipeline>();
        });

        // 注册 FBX 管道
        PipelineFactory::Instance().Register("fbx", []() -> ConversionPipelinePtr {
            return std::make_unique<FBXPipeline>();
        });

        std::cout << "[PipelineRegistrar] Registered pipelines: shapefile, fbx" << std::endl;
    }
};

// 静态实例，确保在程序启动时注册
static PipelineRegistrar registrar;

} // namespace pipeline

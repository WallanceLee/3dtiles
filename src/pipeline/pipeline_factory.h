#pragma once

/**
 * @file pipeline_factory.h
 * @brief 增强的管道工厂 - 阶段 1 重构
 *
 * 支持动态管道注册和元数据查询
 * 实现插件式扩展机制
 *
 * 注意：此文件替代 conversion_pipeline.h 中的旧 PipelineFactory
 */

#include "conversion_params.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>

namespace pipeline {

// ============================================
// 前向声明和类型定义
// ============================================

class IConversionPipeline;

// 使用 shared_ptr 避免需要完整类型声明
// 在 C++20 中，unique_ptr 需要完整类型，但 shared_ptr 不需要
using ConversionPipelinePtr = std::shared_ptr<IConversionPipeline>;

// ============================================
// 管道元数据
// ============================================

/**
 * @brief 管道元数据结构
 *
 * 描述管道的基本信息和能力
 */
struct PipelineMetadata {
    std::string type_name;                     ///< 类型标识（如 "shapefile", "fbx"）
    std::string display_name;                  ///< 显示名称（如 "Shapefile Converter"）
    std::string description;                   ///< 描述信息
    std::vector<std::string> supported_extensions;  ///< 支持的文件扩展名（如 ".shp", ".fbx"）
    std::string version = "1.0";              ///< 版本号

    /**
     * @brief 检查是否支持指定扩展名
     */
    [[nodiscard]] bool SupportsExtension(const std::string& ext) const {
        for (const auto& supported : supported_extensions) {
            if (supported == ext) {
                return true;
            }
        }
        return false;
    }
};

// ============================================
// 管道创建器接口
// ============================================

/**
 * @brief 管道创建器接口
 *
 * 封装管道创建逻辑和元数据
 */
class IPipelineCreator {
public:
    virtual ~IPipelineCreator() = default;

    /**
     * @brief 创建管道实例
     */
    [[nodiscard]] virtual ConversionPipelinePtr Create() const = 0;

    /**
     * @brief 获取管道元数据
     */
    [[nodiscard]] virtual const PipelineMetadata& GetMetadata() const = 0;

    /**
     * @brief 创建默认参数
     */
    [[nodiscard]] virtual std::unique_ptr<DataSourceSpecificParams>
        CreateDefaultParams() const = 0;

protected:
    IPipelineCreator() = default;
    IPipelineCreator(const IPipelineCreator&) = default;
    IPipelineCreator& operator=(const IPipelineCreator&) = default;
};

using PipelineCreatorPtr = std::unique_ptr<IPipelineCreator>;

// ============================================
// 模板化的创建器实现
// ============================================

/**
 * @brief 模板化的管道创建器实现
 * @tparam PipelineType 管道类类型
 * @tparam ParamsType 参数类类型
 */
template<typename PipelineType, typename ParamsType>
class PipelineCreatorImpl : public IPipelineCreator {
public:
    explicit PipelineCreatorImpl(PipelineMetadata metadata)
        : metadata_(std::move(metadata)) {}

    [[nodiscard]] ConversionPipelinePtr Create() const override {
        return std::make_unique<PipelineType>();
    }

    [[nodiscard]] const PipelineMetadata& GetMetadata() const override {
        return metadata_;
    }

    [[nodiscard]] std::unique_ptr<DataSourceSpecificParams>
        CreateDefaultParams() const override {
        return std::make_unique<ParamsType>();
    }

private:
    PipelineMetadata metadata_;
};

// ============================================
// 增强的工厂类
// ============================================

/**
 * @brief 增强的管道工厂
 *
 * 支持动态注册、元数据查询和扩展名映射
 *
 * 注意：此类替代 conversion_pipeline.h 中的 OldPipelineFactory
 */
class PipelineFactoryV2 {
public:
    /**
     * @brief 获取工厂单例实例
     */
    [[nodiscard]] static PipelineFactoryV2& Instance() noexcept;

    /**
     * @brief 注册管道
     * @param creator 管道创建器
     */
    void Register(PipelineCreatorPtr creator);

    /**
     * @brief 创建管道实例
     * @param type 管道类型名
     * @return 管道实例，未找到时返回 nullptr
     */
    [[nodiscard]] ConversionPipelinePtr Create(const std::string& type) const;

    /**
     * @brief 获取管道元数据
     * @param type 管道类型名
     * @return 元数据指针，未找到时返回 nullptr
     */
    [[nodiscard]] const PipelineMetadata* GetMetadata(const std::string& type) const;

    /**
     * @brief 列出所有已注册管道类型
     */
    [[nodiscard]] std::vector<std::string> ListRegisteredTypes() const;

    /**
     * @brief 根据文件扩展名查找管道
     * @param ext 扩展名（如 ".shp", ".fbx"）
     * @return 管道类型名，未找到时返回空字符串
     */
    [[nodiscard]] std::string FindPipelineForExtension(const std::string& ext) const;

    /**
     * @brief 创建默认参数
     * @param type 管道类型名
     * @return 默认参数实例，未找到时返回 nullptr
     */
    [[nodiscard]] std::unique_ptr<DataSourceSpecificParams>
        CreateDefaultParams(const std::string& type) const;

    /**
     * @brief 检查是否已注册
     */
    [[nodiscard]] bool IsRegistered(const std::string& type) const;

    /**
     * @brief 注销管道（主要用于测试）
     */
    void Unregister(const std::string& type);

    /**
     * @brief 清空所有注册（主要用于测试）
     */
    void Clear();

private:
    PipelineFactoryV2() = default;
    ~PipelineFactoryV2() = default;
    PipelineFactoryV2(const PipelineFactoryV2&) = delete;
    PipelineFactoryV2& operator=(const PipelineFactoryV2&) = delete;

    std::unordered_map<std::string, PipelineCreatorPtr> creators_;
};

// ============================================
// 自动注册宏
// ============================================

/**
 * @brief 管道自动注册宏
 *
 * 在管道实现文件中使用此宏自动注册到工厂
 *
 * @param PipelineClass 管道类名（如 ShapefilePipeline）
 * @param ParamsClass 参数类名（如 ShapefileParams）
 * @param TypeName 类型标识（如 "shapefile"）
 * @param DisplayName 显示名称（如 "Shapefile Converter"）
 * @param ... 支持的扩展名列表（如 ".shp", ".shx", ".dbf"）
 *
 * 示例：
 * @code
 * REGISTER_PIPELINE(ShapefilePipeline, ShapefileParams, "shapefile",
 *                   "Shapefile Converter", ".shp", ".shx", ".dbf")
 * @endcode
 */
#define REGISTER_PIPELINE(PipelineClass, ParamsClass, TypeName, DisplayName, ...) \
    namespace {                                                                   \
        struct PipelineClass##_Registrar {                                        \
            PipelineClass##_Registrar() {                                         \
                ::pipeline::PipelineMetadata metadata;                            \
                metadata.type_name = TypeName;                                    \
                metadata.display_name = DisplayName;                              \
                metadata.supported_extensions = {__VA_ARGS__};                    \
                auto creator = std::make_unique<                                  \
                    ::pipeline::PipelineCreatorImpl<PipelineClass, ParamsClass>>( \
                    metadata);                                                    \
                ::pipeline::PipelineFactoryV2::Instance().Register(std::move(creator)); \
            }                                                                     \
        };                                                                        \
        static PipelineClass##_Registrar PipelineClass##_instance;                \
    }

} // namespace pipeline

#pragma once

/**
 * @file core/processing_params.h
 * @brief 统一处理参数定义 - 阶段 1 重构
 *
 * 集中定义所有处理参数，消除重复定义
 * 替代以下文件中的重复定义：
 *   - lod_pipeline.h
 *   - common/mesh_processor.h
 *   - gltf/gltf_builder.h
 */

#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>

namespace core {

// ============================================
// 网格简化参数
// ============================================

/**
 * @brief 网格简化参数
 *
 * 用于 meshoptimizer 等简化库的配置
 */
struct SimplificationParams {
    float target_error = 0.01f;           ///< 目标误差 (0.01 = 1%)
    float target_ratio = 0.5f;            ///< 目标三角形保留比例 (0.5 = 50%)
    bool enable_simplification = false;   ///< 是否启用简化
    bool preserve_texture_coords = true;  ///< 是否保留纹理坐标
    bool preserve_normals = true;         ///< 是否保留法线
    bool preserve_bounds = false;         ///< 是否保留边界（防止边界收缩）

    /**
     * @brief 验证参数有效性
     */
    [[nodiscard]] bool IsValid() const {
        return target_error >= 0.0f && target_error <= 1.0f &&
               target_ratio > 0.0f && target_ratio <= 1.0f;
    }

    /**
     * @brief 创建高质量预设
     */
    static SimplificationParams HighQuality() {
        return {0.005f, 0.8f, true, true, true, true};
    }

    /**
     * @brief 创建平衡预设
     */
    static SimplificationParams Balanced() {
        return {0.01f, 0.5f, true, true, true, false};
    }

    /**
     * @brief 创建高性能预设（激进简化）
     */
    static SimplificationParams Aggressive() {
        return {0.05f, 0.2f, false, false, false, false};
    }
};

// ============================================
// Draco 压缩参数
// ============================================

/**
 * @brief Draco 压缩参数
 *
 * 用于 Draco 几何压缩库的配置
 */
struct DracoCompressionParams {
    int position_quantization_bits = 11;   ///< 位置量化位数 (10-16)
    int normal_quantization_bits = 10;     ///< 法线量化位数 (8-16)
    int tex_coord_quantization_bits = 12;  ///< 纹理坐标量化位数 (8-16)
    int generic_quantization_bits = 8;     ///< 其他属性量化位数 (8-16)
    int compression_level = 7;             ///< 压缩级别 (0-10, 10=最大压缩)
    bool enable_compression = false;       ///< 是否启用压缩

    /**
     * @brief 验证参数有效性
     */
    [[nodiscard]] bool IsValid() const {
        return position_quantization_bits >= 1 && position_quantization_bits <= 30 &&
               normal_quantization_bits >= 1 && normal_quantization_bits <= 30 &&
               tex_coord_quantization_bits >= 1 && tex_coord_quantization_bits <= 30 &&
               generic_quantization_bits >= 1 && generic_quantization_bits <= 30 &&
               compression_level >= 0 && compression_level <= 10;
    }

    /**
     * @brief 创建高质量预设（高精度）
     */
    static DracoCompressionParams HighQuality() {
        return {14, 12, 14, 10, 7, true};
    }

    /**
     * @brief 创建平衡预设
     */
    static DracoCompressionParams Balanced() {
        return {11, 10, 12, 8, 7, true};
    }

    /**
     * @brief 创建低带宽预设（高压缩）
     */
    static DracoCompressionParams LowBandwidth() {
        return {10, 8, 10, 8, 10, true};
    }

    /**
     * @brief 创建最小预设（用于调试）
     */
    static DracoCompressionParams Minimal() {
        return {8, 8, 8, 8, 0, true};
    }
};

// ============================================
// LOD 级别设置
// ============================================

/**
 * @brief 单个 LOD 级别的设置
 */
struct LODLevelSettings {
    float target_ratio = 1.0f;              ///< 目标三角形比例
    float target_error = 0.01f;             ///< 目标误差
    bool enable_simplification = false;     ///< 是否启用简化
    bool enable_draco = false;              ///< 是否启用 Draco 压缩

    SimplificationParams simplify;          ///< 简化参数
    DracoCompressionParams draco;           ///< Draco 参数

    /**
     * @brief 验证设置有效性
     */
    [[nodiscard]] bool IsValid() const {
        return target_ratio > 0.0f && target_ratio <= 1.0f &&
               target_error >= 0.0f &&
               simplify.IsValid() &&
               draco.IsValid();
    }
};

// ============================================
// LOD 管道设置
// ============================================

/**
 * @brief LOD 管道完整设置
 */
struct LODPipelineSettings {
    bool enable_lod = false;                ///< 主开关
    std::vector<LODLevelSettings> levels;   ///< LOD 级别列表（从精细到粗糙）

    /**
     * @brief 验证设置有效性
     */
    [[nodiscard]] bool IsValid() const {
        if (!enable_lod) {
            return true;
        }
        for (const auto& level : levels) {
            if (!level.IsValid()) {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief 获取 LOD 级别数量
     */
    [[nodiscard]] size_t GetLevelCount() const {
        return enable_lod ? levels.size() : 1;
    }

    /**
     * @brief 从预设比例生成级别
     * @param ratios 目标比例列表（如 {1.0, 0.5, 0.25}）
     * @param base_error 基础误差
     * @param simplify_template 简化参数模板
     * @param draco_template Draco 参数模板
     * @param draco_for_lod0 LOD0 是否启用 Draco
     * @return 生成的 LOD 设置
     */
    static LODPipelineSettings FromRatios(
        const std::vector<float>& ratios,
        float base_error,
        const SimplificationParams& simplify_template,
        const DracoCompressionParams& draco_template,
        bool draco_for_lod0 = false);

    /**
     * @brief 创建默认 LOD 设置（3 级）
     */
    static LODPipelineSettings DefaultThreeLevel(
        const SimplificationParams& simplify_template = {},
        const DracoCompressionParams& draco_template = {});
};

// ============================================
// 纹理处理参数
// ============================================

/**
 * @brief 纹理处理参数
 */
struct TextureProcessingParams {
    enum class Format {
        Original,    ///< 保持原始格式
        KTX2,        ///< KTX2 + Basis Universal
        WebP,        ///< WebP 压缩
        PNG,         ///< PNG（无损）
        JPEG         ///< JPEG（有损）
    };

    enum class Quality {
        Low,         ///< 低质量（高压缩）
        Medium,      ///< 中等质量
        High,        ///< 高质量
        Lossless     ///< 无损
    };

    Format format = Format::KTX2;           ///< 输出格式
    Quality quality = Quality::Medium;      ///< 质量级别
    int quality_value = 85;                 ///< 具体质量值 (0-100)
    int max_size = 2048;                    ///< 最大纹理尺寸
    bool generate_mipmaps = true;           ///< 是否生成 mipmap
    bool flip_y = false;                    ///< 是否 Y 轴翻转
    bool premultiply_alpha = false;         ///< 是否预乘 alpha

    /**
     * @brief 验证参数有效性
     */
    [[nodiscard]] bool IsValid() const {
        return quality_value >= 0 && quality_value <= 100 &&
               max_size > 0;
    }

    /**
     * @brief 获取实际质量值
     */
    [[nodiscard]] int GetQualityValue() const {
        if (quality_value >= 0) {
            return quality_value;
        }
        switch (quality) {
            case Quality::Low: return 50;
            case Quality::Medium: return 85;
            case Quality::High: return 95;
            case Quality::Lossless: return 100;
        }
        return 85;
    }
};

// ============================================
// B3DM 生成参数
// ============================================

/**
 * @brief B3DM 生成参数
 */
struct B3DMGenerationParams {
    bool embed_textures = true;             ///< 是否嵌入纹理
    bool embed_gltf = true;                 ///< 是否嵌入 glTF（而非外部引用）
    bool include_batch_table = true;        ///< 是否包含批次表
    bool include_feature_table = true;      ///< 是否包含要素表
    std::string batch_table_json;           ///< 自定义批次表 JSON

    /**
     * @brief 验证参数有效性
     */
    [[nodiscard]] bool IsValid() const {
        return true;  // 当前所有组合都有效
    }
};

// ============================================
// 顶点数据格式
// ============================================

/**
 * @brief 顶点数据结构
 *
 * 用于网格处理的标准顶点格式
 */
struct VertexData {
    float x = 0.0f, y = 0.0f, z = 0.0f;           ///< 位置
    float nx = 0.0f, ny = 0.0f, nz = 0.0f;        ///< 法线
    float u = 0.0f, v = 0.0f;                     ///< 纹理坐标
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f; ///< 颜色

    VertexData() = default;
    VertexData(float px, float py, float pz) : x(px), y(py), z(pz) {}
    VertexData(float px, float py, float pz, float nx_, float ny_, float nz_)
        : x(px), y(py), z(pz), nx(nx_), ny(ny_), nz(nz_) {}
};

} // namespace core

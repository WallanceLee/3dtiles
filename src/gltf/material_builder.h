#pragma once

/**
 * @file gltf/material_builder.h
 * @brief GLTF Material构建器
 *
 * 封装Material构建逻辑，简化GLTF材质创建
 */

#include "types.h"
#include "extension_manager.h"
#include "extensions/texture_transform.h"
#include "extensions/specular_glossiness.h"
#include <tiny_gltf.h>
#include <vector>
#include <optional>

namespace gltf {

/**
 * @brief 材质构建器
 *
 * 简化GLTF Material的创建过程
 */
class MaterialBuilder {
public:
    MaterialBuilder();

    /**
     * @brief 设置基础颜色
     * @param color 基础颜色 [r,g,b,a]
     */
    void setBaseColor(const std::vector<double>& color);

    /**
     * @brief 设置基础颜色纹理
     * @param textureIndex 纹理索引
     */
    void setBaseColorTexture(int textureIndex);

    /**
     * @brief 设置法线纹理
     * @param textureIndex 纹理索引
     */
    void setNormalTexture(int textureIndex);

    /**
     * @brief 设置自发光纹理
     * @param textureIndex 纹理索引
     */
    void setEmissiveTexture(int textureIndex);

    /**
     * @brief 设置金属度粗糙度纹理
     * @param textureIndex 纹理索引
     */
    void setMetallicRoughnessTexture(int textureIndex);

    /**
     * @brief 设置环境光遮蔽纹理
     * @param textureIndex 纹理索引
     */
    void setOcclusionTexture(int textureIndex);

    /**
     * @brief 设置PBR参数
     * @param roughness 粗糙度 [0,1]
     * @param metallic 金属度 [0,1]
     */
    void setPBRParams(float roughness, float metallic);

    /**
     * @brief 设置自发光颜色
     * @param color 自发光颜色 [r,g,b]
     */
    void setEmissiveColor(const std::vector<double>& color);

    /**
     * @brief 设置Unlit扩展
     * @param unlit 是否启用Unlit
     */
    void setUnlit(bool unlit);

    /**
     * @brief 设置双面渲染
     * @param doubleSided 是否双面渲染
     */
    void setDoubleSided(bool doubleSided);

    /**
     * @brief 设置Alpha模式
     * @param alphaMode Alpha模式 (OPAQUE, MASK, BLEND)
     */
    void setAlphaMode(const std::string& alphaMode);

    /**
     * @brief 设置Alpha裁剪值
     * @param alphaCutoff Alpha裁剪值
     */
    void setAlphaCutoff(float alphaCutoff);

    /**
     * @brief 设置基础颜色纹理变换
     * @param transform 纹理变换
     */
    void setBaseColorTextureTransform(const extensions::TextureTransform& transform);

    /**
     * @brief 设置法线纹理变换
     * @param transform 纹理变换
     */
    void setNormalTextureTransform(const extensions::TextureTransform& transform);

    /**
     * @brief 设置自发光纹理变换
     * @param transform 纹理变换
     */
    void setEmissiveTextureTransform(const extensions::TextureTransform& transform);

    /**
     * @brief 设置金属度粗糙度纹理变换
     * @param transform 纹理变换
     */
    void setMetallicRoughnessTextureTransform(const extensions::TextureTransform& transform);

    /**
     * @brief 设置环境光遮蔽纹理变换
     * @param transform 纹理变换
     */
    void setOcclusionTextureTransform(const extensions::TextureTransform& transform);

    /**
     * @brief 设置环境光遮蔽强度
     * @param strength 强度值 [0,1]
     */
    void setOcclusionStrength(float strength);

    /**
     * @brief 设置Specular-Glossiness参数
     * @param sg Specular-Glossiness参数
     */
    void setSpecularGlossiness(const extensions::SpecularGlossiness& sg);

    /**
     * @brief 构建Material
     * @param model GLTF模型
     * @param extMgr 扩展管理器（用于记录使用的扩展）
     * @return 材质索引
     */
    int build(tinygltf::Model& model, ExtensionManager& extMgr);

    /**
     * @brief 清空数据
     */
    void clear();

private:
    // PBR参数
    std::vector<double> baseColor_ = {1.0, 1.0, 1.0, 1.0};
    float roughnessFactor_ = 1.0f;
    float metallicFactor_ = 0.0f;

    // 纹理索引
    int baseColorTexture_ = -1;
    int normalTexture_ = -1;
    int emissiveTexture_ = -1;
    int metallicRoughnessTexture_ = -1;
    int occlusionTexture_ = -1;

    // 纹理变换
    std::optional<extensions::TextureTransform> baseColorTransform_;
    std::optional<extensions::TextureTransform> normalTransform_;
    std::optional<extensions::TextureTransform> emissiveTransform_;
    std::optional<extensions::TextureTransform> metallicRoughnessTransform_;
    std::optional<extensions::TextureTransform> occlusionTransform_;

    // 自发光颜色
    std::vector<double> emissiveColor_ = {0.0, 0.0, 0.0};

    // 配置
    bool unlit_ = false;
    bool doubleSided_ = true;
    std::string alphaMode_ = "OPAQUE";
    float alphaCutoff_ = 0.5f;
    float occlusionStrength_ = 1.0f;

    // Specular-Glossiness
    std::optional<extensions::SpecularGlossiness> specularGlossiness_;
};

} // namespace gltf

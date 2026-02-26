#pragma once

/**
 * @file common/geometry_extractor.h
 * @brief 几何体提取器接口
 *
 * 该接口抽象不同数据源(FBX/Shapefile)的几何体和材质提取逻辑，
 * 供B3DM生成器统一使用。
 */

#include "../spatial/core/spatial_item.h"
#include <osg/Geometry>
#include <osg/Texture>
#include <vector>
#include <string>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>

namespace common {

/**
 * @brief 纹理变换信息
 *
 * 对应GLTF KHR_texture_transform扩展的参数
 * 用于描述纹理坐标的变换（偏移、缩放、旋转）
 */
struct TextureTransformInfo {
    float offset[2] = {0.0f, 0.0f};      // UV偏移 [u, v]
    float scale[2] = {1.0f, 1.0f};       // UV缩放 [u, v]
    float rotation = 0.0f;                // 旋转角度（弧度）
    int texCoord = 0;                     // 纹理坐标集索引
    bool hasTransform = false;            // 标记是否有变换

    /**
     * @brief 创建默认变换（无变换）
     */
    static TextureTransformInfo Identity() {
        return {};
    }

    /**
     * @brief 创建带偏移的变换
     */
    static TextureTransformInfo WithOffset(float u, float v) {
        TextureTransformInfo t;
        t.offset[0] = u;
        t.offset[1] = v;
        t.hasTransform = true;
        return t;
    }

    /**
     * @brief 创建带缩放的变换
     */
    static TextureTransformInfo WithScale(float u, float v) {
        TextureTransformInfo t;
        t.scale[0] = u;
        t.scale[1] = v;
        t.hasTransform = true;
        return t;
    }
};

/**
 * @brief 完整的材质信息
 *
 * 包含PBR材质的所有参数，支持：
 * - Metallic-Roughness工作流
 * - Specular-Glossiness工作流（传统FBX材质）
 * - 纹理变换
 * - 各种GLTF材质扩展
 */
struct MaterialInfo {
    // ==================== 基础PBR参数 ====================

    /**
     * @brief 基础颜色
     * 线性颜色空间，RGBA格式，默认: [1.0, 1.0, 1.0, 1.0]
     */
    std::vector<double> baseColor = {1.0, 1.0, 1.0, 1.0};

    /**
     * @brief 粗糙度 [0.0, 1.0]，默认: 1.0
     */
    float roughnessFactor = 1.0f;

    /**
     * @brief 金属度 [0.0, 1.0]，默认: 0.0
     */
    float metallicFactor = 0.0f;

    /**
     * @brief 自发光颜色 [r,g,b]，默认: [0.0, 0.0, 0.0]
     */
    std::vector<double> emissiveColor = {0.0, 0.0, 0.0};

    /**
     * @brief 遮挡强度 [0.0, 1.0]，默认: 1.0
     */
    float aoStrength = 1.0f;

    // ==================== 纹理对象 ====================

    osg::ref_ptr<osg::Texture> baseColorTexture;           // 基础颜色纹理（纹理单元0）
    osg::ref_ptr<osg::Texture> normalTexture;              // 法线纹理（纹理单元1）
    osg::ref_ptr<osg::Texture> metallicRoughnessTexture;   // 金属度/粗糙度纹理（纹理单元2）
    osg::ref_ptr<osg::Texture> occlusionTexture;           // 遮挡纹理（纹理单元3）
    osg::ref_ptr<osg::Texture> emissiveTexture;            // 自发光纹理（纹理单元4）

    // ==================== 纹理变换 ====================

    TextureTransformInfo baseColorTransform;
    TextureTransformInfo normalTransform;
    TextureTransformInfo metallicRoughnessTransform;
    TextureTransformInfo occlusionTransform;
    TextureTransformInfo emissiveTransform;

    // ==================== Specular-Glossiness ====================

    bool useSpecularGlossiness = false;                    // 是否使用Specular-Glossiness
    std::vector<double> diffuseFactor = {1.0, 1.0, 1.0, 1.0};  // 漫反射因子
    std::vector<double> specularFactor = {1.0, 1.0, 1.0};      // 高光因子
    double glossinessFactor = 1.0;                            // 光泽度
    osg::ref_ptr<osg::Texture> specularGlossinessTexture;     // Specular-Glossiness纹理
    osg::ref_ptr<osg::Texture> diffuseTexture;                // 漫反射纹理

    // ==================== 其他属性 ====================

    bool doubleSided = true;                               // 双面渲染
    std::string alphaMode = "OPAQUE";                      // Alpha模式
    float alphaCutoff = 0.5f;                              // Alpha裁剪值

    // ==================== 辅助方法 ====================

    /**
     * @brief 检查是否有任何纹理
     */
    bool hasAnyTexture() const {
        return baseColorTexture || normalTexture || metallicRoughnessTexture ||
               occlusionTexture || emissiveTexture || specularGlossinessTexture ||
               diffuseTexture;
    }

    /**
     * @brief 检查是否有纹理变换
     */
    bool hasAnyTextureTransform() const {
        return baseColorTransform.hasTransform || normalTransform.hasTransform ||
               metallicRoughnessTransform.hasTransform || occlusionTransform.hasTransform ||
               emissiveTransform.hasTransform;
    }
};

/**
 * @brief 几何体提取器接口
 *
 * 不同数据源(FBX/Shapefile)实现此接口以提供几何体和材质
 */
class IGeometryExtractor {
public:
    virtual ~IGeometryExtractor() = default;

    /**
     * @brief 从空间对象提取几何体
     * @param item 空间对象
     * @return 几何体列表
     */
    virtual std::vector<osg::ref_ptr<osg::Geometry>> extract(
        const spatial::core::SpatialItem* item) = 0;

    /**
     * @brief 获取对象的唯一标识（用于BatchID）
     */
    virtual std::string getId(const spatial::core::SpatialItem* item) = 0;

    /**
     * @brief 获取对象的属性（用于BatchTable）
     */
    virtual std::map<std::string, nlohmann::json> getAttributes(
        const spatial::core::SpatialItem* item) = 0;

    /**
     * @brief 获取对象的材质信息
     *
     * 提取空间对象的完整材质信息，包括：
     * - PBR参数
     * - 纹理对象
     * - 纹理变换
     * - 扩展数据
     *
     * @param item 空间对象
     * @return 材质信息，如果没有材质返回nullptr或默认材质
     */
    virtual std::shared_ptr<MaterialInfo> getMaterial(
        const spatial::core::SpatialItem* item) = 0;
};

} // namespace common

#pragma once

/**
 * @file osg/utils/material_utils.h
 * @brief OSG材质工具类
 *
 * 从appendGeometryToModel提取的材质处理逻辑
 * 包括：PBR参数提取、材质创建
 */

#include <osg/StateSet>
#include <osg/Material>
#include <osg/Uniform>
#include <vector>

namespace osg {
namespace utils {

/**
 * @brief 材质配置
 */
struct MaterialConfig {
    bool enableUnlit = false;  // 启用KHR_materials_unlit
    bool doubleSided = true;   // 双面渲染
};

/**
 * @brief PBR材质参数
 */
struct PBRParams {
    std::vector<double> baseColor = {1.0, 1.0, 1.0, 1.0};  // 基础颜色 [r,g,b,a]
    double emissiveColor[3] = {0.0, 0.0, 0.0};             // 自发光颜色 [r,g,b]
    float roughnessFactor = 1.0f;                          // 粗糙度
    float metallicFactor = 0.0f;                           // 金属度
    float aoStrength = 1.0f;                               // AO强度
};

/**
 * @brief 材质工具类
 *
 * 处理OSG材质到GLTF材质的转换
 */
class MaterialUtils {
public:
    /**
     * @brief 从StateSet提取PBR参数
     *
     * 从OSG StateSet中提取PBR材质参数：
     * - 基础颜色（从osg::Material的diffuse）
     * - 自发光颜色（从osg::Material的emission）
     * - 粗糙度、金属度（从Uniform）
     *
     * @param stateSet OSG状态集
     * @param outParams 输出的PBR参数
     */
    static void extractPBRParams(
        const osg::StateSet* stateSet,
        PBRParams& outParams
    );

    /**
     * @brief 检查是否有材质
     *
     * @param stateSet OSG状态集
     * @return true 如果有Material或纹理
     */
    static bool hasMaterial(const osg::StateSet* stateSet);

    /**
     * @brief 获取基础颜色纹理
     *
     * @param stateSet OSG状态集
     * @return 纹理对象，如果没有则返回nullptr
     */
    static const osg::Texture* getBaseColorTexture(const osg::StateSet* stateSet);

    /**
     * @brief 获取法线纹理
     *
     * @param stateSet OSG状态集
     * @return 纹理对象，如果没有则返回nullptr
     */
    static const osg::Texture* getNormalTexture(const osg::StateSet* stateSet);

    /**
     * @brief 获取自发光纹理
     *
     * @param stateSet OSG状态集
     * @return 纹理对象，如果没有则返回nullptr
     */
    static const osg::Texture* getEmissiveTexture(const osg::StateSet* stateSet);

private:
    /**
     * @brief 从Material提取颜色
     */
    static void extractColorsFromMaterial(
        const osg::Material* material,
        PBRParams& params
    );

    /**
     * @brief 从StateSet提取Uniform参数
     */
    static void extractUniformsFromStateSet(
        const osg::StateSet* stateSet,
        PBRParams& params
    );
};

} // namespace utils
} // namespace osg

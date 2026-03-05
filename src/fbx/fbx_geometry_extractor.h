#pragma once

/**
 * @file fbx/fbx_geometry_extractor.h
 * @brief FBX几何体提取器
 *
 * 使用 GeometryExtractorBase 简化实现
 */

#include "../common/geometry_extractor_base.h"
#include "fbx_spatial_item_adapter.h"
#include <osg/Geometry>

// 前向声明（这些结构体定义在全局命名空间）
struct MeshInstanceInfo;
struct MaterialExtensionData;

namespace fbx {

/**
 * @brief FBX提取数据上下文
 *
 * 封装 FBX 提取所需的所有数据
 */
struct FBXExtractContext {
    ::MeshInstanceInfo* meshInfo = nullptr;  // 非 const，因为原始代码使用非 const 指针
    int transformIndex = 0;
    osg::Matrixd transform;
    std::string nodeName;
    const osg::Geometry* geometry = nullptr;
    const ::MaterialExtensionData* materialExtData = nullptr;
};

/**
 * @brief FBX几何体提取器
 *
 * 继承 GeometryExtractorBase，只需实现数据提取逻辑
 */
class FBXGeometryExtractor : public common::GeometryExtractorBase<
    FBXGeometryExtractor,       // 派生类
    FBXSpatialItemAdapter,      // 适配器类型
    FBXExtractContext> {        // 数据类型
public:
    FBXGeometryExtractor() = default;
    ~FBXGeometryExtractor() override = default;

    // ============================================================
    // GeometryExtractorBase 要求实现的纯虚函数
    // ============================================================

    /**
     * @brief 从上下文提取几何体
     */
    std::vector<osg::ref_ptr<osg::Geometry>> extractImpl(
        FBXExtractContext& ctx);  // 非 const，因为需要修改

    /**
     * @brief 获取对象ID
     */
    std::string getIdImpl(FBXExtractContext& ctx, size_t id);

    /**
     * @brief 获取对象属性
     */
    std::map<std::string, nlohmann::json> getAttributesImpl(
        FBXExtractContext& ctx);

    /**
     * @brief 获取材质信息
     */
    std::shared_ptr<common::MaterialInfo> getMaterialImpl(
        FBXExtractContext& ctx);

    // ============================================================
    // GeometryExtractorBase 要求实现的辅助方法（必须是 public）
    // ============================================================

    /**
     * @brief 从适配器构建提取上下文
     */
    FBXExtractContext getData(const FBXSpatialItemAdapter* adapter);

    /**
     * @brief 获取对象ID（使用 nodeName）
     */
    size_t getItemId(const FBXSpatialItemAdapter* adapter);

private:
    // 辅助函数：复制纹理变换
    void copyTextureTransform(const ::TextureTransformData& src,
                              common::TextureTransformInfo& dst);
};

} // namespace fbx

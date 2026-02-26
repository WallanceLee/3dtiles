# FBX材质移植实施步骤

本文档提供详细的实施步骤，指导开发者逐步完成FBX材质功能从master分支到refactor-master分支的移植。

## 前置条件

- 熟悉C++和OSG/GLTF基础
- 了解FBX材质系统
- 已阅读`fbx_material_migration.md`

## 步骤1：扩展IGeometryExtractor接口

### 1.1 修改文件
**文件路径：** `src/common/geometry_extractor.h`

### 1.2 添加TextureTransformInfo结构
```cpp
namespace common {

/**
 * @brief 纹理变换信息
 * 
 * 对应GLTF的KHR_texture_transform扩展
 */
struct TextureTransformInfo {
    float offset[2] = {0.0f, 0.0f};      // UV偏移
    float scale[2] = {1.0f, 1.0f};       // UV缩放
    float rotation = 0.0f;                // 旋转（弧度）
    int texCoord = 0;                     // 纹理坐标集
    bool hasTransform = false;            // 是否有变换
};
```

### 1.3 添加MaterialInfo结构
```cpp
/**
 * @brief 材质信息
 * 
 * 包含完整的PBR材质参数和纹理信息
 */
struct MaterialInfo {
    // ===== 基础PBR参数 =====
    std::vector<double> baseColor = {1.0, 1.0, 1.0, 1.0};  // 基础颜色 [r,g,b,a]
    float roughnessFactor = 1.0f;                          // 粗糙度 [0,1]
    float metallicFactor = 0.0f;                           // 金属度 [0,1]
    std::vector<double> emissiveColor = {0.0, 0.0, 0.0};   // 自发光颜色 [r,g,b]
    float aoStrength = 1.0f;                               // 遮挡强度
    
    // ===== 纹理对象 =====
    osg::ref_ptr<osg::Texture> baseColorTexture;           // 基础颜色纹理（纹理单元0）
    osg::ref_ptr<osg::Texture> normalTexture;              // 法线纹理（纹理单元1）
    osg::ref_ptr<osg::Texture> metallicRoughnessTexture;   // 金属度/粗糙度纹理（纹理单元2）
    osg::ref_ptr<osg::Texture> occlusionTexture;           // 遮挡纹理（纹理单元3）
    osg::ref_ptr<osg::Texture> emissiveTexture;            // 自发光纹理（纹理单元4）
    
    // ===== 纹理变换 =====
    TextureTransformInfo baseColorTransform;
    TextureTransformInfo normalTransform;
    TextureTransformInfo metallicRoughnessTransform;
    TextureTransformInfo occlusionTransform;
    TextureTransformInfo emissiveTransform;
    
    // ===== Specular-Glossiness（传统材质） =====
    bool useSpecularGlossiness = false;                    // 是否使用Specular-Glossiness
    std::vector<double> diffuseFactor = {1.0, 1.0, 1.0, 1.0};  // 漫反射因子
    std::vector<double> specularFactor = {1.0, 1.0, 1.0};      // 高光因子
    double glossinessFactor = 1.0;                            // 光泽度
    osg::ref_ptr<osg::Texture> specularGlossinessTexture;     // Specular-Glossiness纹理
    osg::ref_ptr<osg::Texture> diffuseTexture;                // 漫反射纹理
    
    // ===== 其他属性 =====
    bool doubleSided = true;                               // 双面渲染
    std::string alphaMode = "OPAQUE";                      // Alpha模式：OPAQUE/MASK/BLEND
    float alphaCutoff = 0.5f;                              // Alpha裁剪值（MASK模式）
};
```

### 1.4 扩展IGeometryExtractor接口
```cpp
class IGeometryExtractor {
public:
    virtual ~IGeometryExtractor() = default;

    /**
     * @brief 从空间对象提取几何体
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
     * @brief 获取对象的材质信息（新增）
     * 
     * @param item 空间对象
     * @return 材质信息，如果没有材质返回nullptr
     */
    virtual std::shared_ptr<MaterialInfo> getMaterial(
        const spatial::core::SpatialItem* item) = 0;
};

} // namespace common
```

### 1.5 验证步骤
- [ ] 编译通过，无语法错误
- [ ] 其他提取器（如Shapefile）需要实现虚函数或保持默认行为

---

## 步骤2：实现FBXGeometryExtractor::getMaterial

### 2.1 修改文件
**文件路径：** `src/fbx/fbx_geometry_extractor.h` 和 `src/fbx/fbx_geometry_extractor.cpp`

### 2.2 添加头文件包含
```cpp
#include "../common/geometry_extractor.h"
#include "../osg/utils/material_utils.h"
#include "fbx_spatial_item_adapter.h"
```

### 2.3 在类定义中添加方法声明
```cpp
class FBXGeometryExtractor : public common::IGeometryExtractor {
public:
    // ... 现有方法 ...
    
    std::shared_ptr<common::MaterialInfo> getMaterial(
        const spatial::core::SpatialItem* item) override;
};
```

### 2.4 实现getMaterial方法
```cpp
std::shared_ptr<common::MaterialInfo> FBXGeometryExtractor::getMaterial(
    const spatial::core::SpatialItem* item) {

    const auto* fbxItem = dynamic_cast<const FBXSpatialItemAdapter*>(item);
    if (!fbxItem) {
        LOG_W("FBXGeometryExtractor::getMaterial: item is not FBXSpatialItemAdapter");
        return nullptr;
    }

    // 获取几何体的StateSet
    const osg::Geometry* geom = fbxItem->getGeometry();
    if (!geom) {
        return nullptr;
    }

    const osg::StateSet* stateSet = geom->getStateSet();
    if (!stateSet) {
        // 没有StateSet表示使用默认材质
        return std::make_shared<common::MaterialInfo>();
    }

    auto materialInfo = std::make_shared<common::MaterialInfo>();

    // ===== 步骤2.4.1：提取基础PBR参数 =====
    osg::utils::PBRParams pbrParams;
    osg::utils::MaterialUtils::extractPBRParams(stateSet, pbrParams);

    materialInfo->baseColor = pbrParams.baseColor;
    materialInfo->roughnessFactor = pbrParams.roughnessFactor;
    materialInfo->metallicFactor = pbrParams.metallicFactor;
    materialInfo->emissiveColor = {
        pbrParams.emissiveColor[0],
        pbrParams.emissiveColor[1],
        pbrParams.emissiveColor[2]
    };
    materialInfo->aoStrength = pbrParams.aoStrength;

    // ===== 步骤2.4.2：提取纹理对象 =====
    materialInfo->baseColorTexture = 
        const_cast<osg::Texture*>(osg::utils::MaterialUtils::getBaseColorTexture(stateSet));
    materialInfo->normalTexture = 
        const_cast<osg::Texture*>(osg::utils::MaterialUtils::getNormalTexture(stateSet));
    materialInfo->emissiveTexture = 
        const_cast<osg::Texture*>(osg::utils::MaterialUtils::getEmissiveTexture(stateSet));

    // 金属度/粗糙度和遮挡纹理需要从其他纹理单元获取
    // 根据FBXLoader的实现，它们可能在特定的纹理单元
    if (stateSet->getTextureAttributeList().size() > 2) {
        materialInfo->metallicRoughnessTexture = 
            const_cast<osg::Texture*>(dynamic_cast<const osg::Texture*>(
                stateSet->getTextureAttribute(2, osg::StateAttribute::TEXTURE)));
    }
    if (stateSet->getTextureAttributeList().size() > 3) {
        materialInfo->occlusionTexture = 
            const_cast<osg::Texture*>(dynamic_cast<const osg::Texture*>(
                stateSet->getTextureAttribute(3, osg::StateAttribute::TEXTURE)));
    }

    // ===== 步骤2.4.3：提取FBX特有的扩展数据 =====
    // 需要从FBXSpatialItemAdapter获取MaterialExtensionData
    // 这需要在FBXSpatialItemAdapter中添加相应的方法
    const MaterialExtensionData* extData = fbxItem->getMaterialExtensionData();
    if (extData) {
        // 复制纹理变换
        copyTextureTransform(extData->base_color_transform, materialInfo->baseColorTransform);
        copyTextureTransform(extData->normal_transform, materialInfo->normalTransform);
        copyTextureTransform(extData->metallic_roughness_transform, materialInfo->metallicRoughnessTransform);
        copyTextureTransform(extData->occlusion_transform, materialInfo->occlusionTransform);
        copyTextureTransform(extData->emissive_transform, materialInfo->emissiveTransform);

        // 复制Specular-Glossiness数据
        if (extData->specular_glossiness.use_specular_glossiness) {
            materialInfo->useSpecularGlossiness = true;
            materialInfo->diffuseFactor = {
                extData->specular_glossiness.diffuse_factor[0],
                extData->specular_glossiness.diffuse_factor[1],
                extData->specular_glossiness.diffuse_factor[2],
                extData->specular_glossiness.diffuse_factor[3]
            };
            materialInfo->specularFactor = {
                extData->specular_glossiness.specular_factor[0],
                extData->specular_glossiness.specular_factor[1],
                extData->specular_glossiness.specular_factor[2]
            };
            materialInfo->glossinessFactor = extData->specular_glossiness.glossiness_factor;
        }
    }

    return materialInfo;
}
```

### 2.5 添加辅助函数
```cpp
namespace {
    void copyTextureTransform(const ::TextureTransformData& src, 
                              common::TextureTransformInfo& dst) {
        if (src.has_transform) {
            dst.hasTransform = true;
            dst.offset[0] = src.offset[0];
            dst.offset[1] = src.offset[1];
            dst.scale[0] = src.scale[0];
            dst.scale[1] = src.scale[1];
            dst.rotation = src.rotation;
            dst.texCoord = src.tex_coord;
        }
    }
}
```

### 2.6 验证步骤
- [ ] 编译通过
- [ ] 单元测试：验证能从FBX正确提取材质信息

---

## 步骤3：扩展FBXSpatialItemAdapter

### 3.1 修改文件
**文件路径：** `src/fbx/fbx_spatial_item_adapter.h` 和 `src/fbx/fbx_spatial_item_adapter.cpp`

### 3.2 添加方法声明
```cpp
class FBXSpatialItemAdapter : public spatial::core::SpatialItem {
public:
    // ... 现有方法 ...

    /**
     * @brief 获取材质扩展数据
     * @return 材质扩展数据指针，如果没有返回nullptr
     */
    const MaterialExtensionData* getMaterialExtensionData() const;

    /**
     * @brief 设置材质扩展数据
     * @param data 材质扩展数据指针（由外部管理生命周期）
     */
    void setMaterialExtensionData(const MaterialExtensionData* data);

private:
    // ... 现有成员 ...
    const MaterialExtensionData* materialExtData_ = nullptr;
};
```

### 3.3 实现方法
```cpp
const MaterialExtensionData* FBXSpatialItemAdapter::getMaterialExtensionData() const {
    return materialExtData_;
}

void FBXSpatialItemAdapter::setMaterialExtensionData(const MaterialExtensionData* data) {
    materialExtData_ = data;
}
```

---

## 步骤4：修改B3DMGenerator支持材质

### 4.1 分析现有代码
**文件路径：** `src/b3dm/b3dm_generator.cpp`

现有`buildGLTFModel`函数只处理几何体，不处理材质。

### 4.2 修改buildGLTFModel函数签名
```cpp
void B3DMGenerator::buildGLTFModel(
    osg::Geometry* mergedGeom,
    const spatial::core::SpatialItemRefList& items,
    bool enableDraco,
    const DracoCompressionParams& dracoParams,
    std::vector<unsigned char>& glbData,
    const std::vector<std::shared_ptr<common::MaterialInfo>>& materials);  // 新增参数
```

### 4.3 在函数内添加材质构建逻辑
```cpp
void B3DMGenerator::buildGLTFModel(
    osg::Geometry* mergedGeom,
    const spatial::core::SpatialItemRefList& items,
    bool enableDraco,
    const DracoCompressionParams& dracoParams,
    std::vector<unsigned char>& glbData,
    const std::vector<std::shared_ptr<common::MaterialInfo>>& materials) {

    // ... 现有几何体提取代码 ...

    // ===== 新增：构建材质 =====
    std::vector<int> materialIndices;
    if (!materials.empty()) {
        // 去重：相同的材质只创建一次
        std::map<std::string, int> materialCache;
        
        for (const auto& matInfo : materials) {
            std::string matKey = computeMaterialKey(matInfo);
            auto it = materialCache.find(matKey);
            if (it != materialCache.end()) {
                materialIndices.push_back(it->second);
            } else {
                int matIdx = buildMaterial(matInfo, model, buffer);
                materialCache[matKey] = matIdx;
                materialIndices.push_back(matIdx);
            }
        }
    }

    // ... 创建primitive时关联材质 ...
    tinygltf::Primitive primitive;
    // ... 设置attributes ...
    
    // 关联材质
    if (!materialIndices.empty() && materialIndices[0] >= 0) {
        primitive.material = materialIndices[0];
    }
    
    // ... 其余代码 ...
}
```

### 4.4 实现buildMaterial辅助函数
```cpp
int B3DMGenerator::buildMaterial(
    const std::shared_ptr<common::MaterialInfo>& matInfo,
    tinygltf::Model& model,
    tinygltf::Buffer& buffer) {

    if (!matInfo) {
        return -1;
    }

    gltf::MaterialBuilder matBuilder;
    gltf::ExtensionManager extMgr;

    // 设置基础PBR参数
    matBuilder.setBaseColor(matInfo->baseColor);
    matBuilder.setPBRParams(matInfo->roughnessFactor, matInfo->metallicFactor);
    matBuilder.setEmissiveColor(matInfo->emissiveColor);
    matBuilder.setDoubleSided(matInfo->doubleSided);
    matBuilder.setAlphaMode(matInfo->alphaMode);

    // 处理基础颜色纹理
    if (matInfo->baseColorTexture) {
        processAndAddTexture(matInfo->baseColorTexture, 
                            matInfo->baseColorTransform,
                            matBuilder, model, buffer, extMgr,
                            TextureType::BASE_COLOR);
    }

    // 处理法线纹理
    if (matInfo->normalTexture) {
        processAndAddTexture(matInfo->normalTexture,
                            matInfo->normalTransform,
                            matBuilder, model, buffer, extMgr,
                            TextureType::NORMAL);
    }

    // 处理自发光纹理
    if (matInfo->emissiveTexture) {
        processAndAddTexture(matInfo->emissiveTexture,
                            matInfo->emissiveTransform,
                            matBuilder, model, buffer, extMgr,
                            TextureType::EMISSIVE);
    }

    // 处理Specular-Glossiness
    if (matInfo->useSpecularGlossiness) {
        gltf::extensions::SpecularGlossiness sg;
        sg.diffuse_factor = {
            matInfo->diffuseFactor[0],
            matInfo->diffuseFactor[1],
            matInfo->diffuseFactor[2],
            matInfo->diffuseFactor[3]
        };
        sg.specular_factor = {
            matInfo->specularFactor[0],
            matInfo->specularFactor[1],
            matInfo->specularFactor[2]
        };
        sg.glossiness_factor = matInfo->glossinessFactor;
        
        matBuilder.setSpecularGlossiness(sg);
    }

    // 构建材质
    return matBuilder.build(model, extMgr);
}
```

### 4.5 实现纹理处理辅助函数
```cpp
void B3DMGenerator::processAndAddTexture(
    osg::Texture* texture,
    const common::TextureTransformInfo& transform,
    gltf::MaterialBuilder& matBuilder,
    tinygltf::Model& model,
    tinygltf::Buffer& buffer,
    gltf::ExtensionManager& extMgr,
    TextureType type) {

    // 处理纹理
    auto result = osg::utils::TextureUtils::processTexture(
        texture, config_.enableTextureCompress);
    
    if (!result.success) {
        LOG_W("Failed to process texture");
        return;
    }

    // 添加到模型
    bool useBasisu = (result.mimeType == "image/ktx2");
    if (useBasisu) {
        extMgr.useAndRequire("KHR_texture_basisu");
    }

    int texIdx = osg::utils::TextureUtils::addImageToModel(
        model, buffer, result.data, result.mimeType, useBasisu);

    // 设置到材质构建器
    switch (type) {
        case TextureType::BASE_COLOR:
            matBuilder.setBaseColorTexture(texIdx);
            if (transform.hasTransform) {
                matBuilder.setBaseColorTextureTransform(
                    convertToGLTFTransform(transform));
            }
            if (result.hasAlpha) {
                matBuilder.setAlphaMode("BLEND");
            }
            break;
        case TextureType::NORMAL:
            matBuilder.setNormalTexture(texIdx);
            if (transform.hasTransform) {
                matBuilder.setNormalTextureTransform(
                    convertToGLTFTransform(transform));
            }
            break;
        case TextureType::EMISSIVE:
            matBuilder.setEmissiveTexture(texIdx);
            if (transform.hasTransform) {
                matBuilder.setEmissiveTextureTransform(
                    convertToGLTFTransform(transform));
            }
            break;
    }
}
```

---

## 步骤5：扩展MaterialBuilder

### 5.1 修改文件
**文件路径：** `src/gltf/material_builder.h`

### 5.2 添加头文件包含
```cpp
#include "extensions/texture_transform.h"
#include "extensions/specular_glossiness.h"
#include <optional>
```

### 5.3 添加新方法声明
```cpp
class MaterialBuilder {
public:
    // ... 现有方法 ...

    // 纹理变换设置
    void setBaseColorTextureTransform(const gltf::extensions::TextureTransform& transform);
    void setNormalTextureTransform(const gltf::extensions::TextureTransform& transform);
    void setEmissiveTextureTransform(const gltf::extensions::TextureTransform& transform);
    void setMetallicRoughnessTextureTransform(const gltf::extensions::TextureTransform& transform);
    void setOcclusionTextureTransform(const gltf::extensions::TextureTransform& transform);

    // Specular-Glossiness设置
    void setSpecularGlossiness(const gltf::extensions::SpecularGlossiness& sg);

private:
    // ... 现有成员 ...
    std::optional<gltf::extensions::TextureTransform> baseColorTransform_;
    std::optional<gltf::extensions::TextureTransform> normalTransform_;
    std::optional<gltf::extensions::TextureTransform> emissiveTransform_;
    std::optional<gltf::extensions::TextureTransform> metallicRoughnessTransform_;
    std::optional<gltf::extensions::TextureTransform> occlusionTransform_;
    std::optional<gltf::extensions::SpecularGlossiness> specularGlossiness_;
};
```

### 5.4 实现新方法
**文件路径：** `src/gltf/material_builder.cpp`

```cpp
void MaterialBuilder::setBaseColorTextureTransform(
    const gltf::extensions::TextureTransform& transform) {
    baseColorTransform_ = transform;
}

void MaterialBuilder::setNormalTextureTransform(
    const gltf::extensions::TextureTransform& transform) {
    normalTransform_ = transform;
}

void MaterialBuilder::setEmissiveTextureTransform(
    const gltf::extensions::TextureTransform& transform) {
    emissiveTransform_ = transform;
}

void MaterialBuilder::setSpecularGlossiness(
    const gltf::extensions::SpecularGlossiness& sg) {
    specularGlossiness_ = sg;
}
```

### 5.5 修改build()方法
```cpp
int MaterialBuilder::build(tinygltf::Model& model, ExtensionManager& extMgr) {
    tinygltf::Material material;
    material.name = "Material";
    material.doubleSided = doubleSided_;
    material.alphaMode = alphaMode_;

    // 设置PBR参数
    material.pbrMetallicRoughness.baseColorFactor = baseColor_;
    material.pbrMetallicRoughness.roughnessFactor = roughnessFactor_;
    material.pbrMetallicRoughness.metallicFactor = metallicFactor_;

    // 设置基础颜色纹理
    if (baseColorTexture_ >= 0) {
        material.pbrMetallicRoughness.baseColorTexture.index = baseColorTexture_;
        
        // 应用纹理变换
        if (baseColorTransform_) {
            gltf::extensions::applyTextureTransform(
                material, "baseColorTexture", *baseColorTransform_, extMgr);
        }
    }

    // 设置法线纹理
    if (normalTexture_ >= 0) {
        material.normalTexture.index = normalTexture_;
        
        if (normalTransform_) {
            gltf::extensions::applyTextureTransform(
                material, "normalTexture", *normalTransform_, extMgr);
        }
    }

    // 设置自发光
    if (emissiveTexture_ >= 0) {
        material.emissiveTexture.index = emissiveTexture_;
        
        if (emissiveTransform_) {
            gltf::extensions::applyTextureTransform(
                material, "emissiveTexture", *emissiveTransform_, extMgr);
        }
    }
    
    if (emissiveColor_[0] > 0.0 || emissiveColor_[1] > 0.0 || emissiveColor_[2] > 0.0) {
        material.emissiveFactor = emissiveColor_;
    }

    // 设置Unlit扩展
    if (unlit_) {
        material.extensions["KHR_materials_unlit"] = tinygltf::Value(tinygltf::Value::Object());
        extMgr.use("KHR_materials_unlit");
    }

    // 应用Specular-Glossiness扩展
    if (specularGlossiness_) {
        gltf::extensions::applySpecularGlossiness(
            material, *specularGlossiness_, extMgr);
    }

    int index = static_cast<int>(model.materials.size());
    model.materials.push_back(material);
    return index;
}
```

---

## 步骤6：修改generate函数

### 6.1 收集材质信息
**文件路径：** `src/b3dm/b3dm_generator.cpp`

```cpp
std::string B3DMGenerator::generate(
    const spatial::core::SpatialItemRefList& items,
    const LODLevelSettings& lodSettings) {

    if (items.empty()) {
        return std::string();
    }

    // 提取并合并几何体
    osg::ref_ptr<osg::Geometry> mergedGeom = extractAndMergeGeometries(items);
    if (!mergedGeom.valid()) {
        LOG_E("Failed to extract and merge geometries");
        return std::string();
    }

    // 应用简化
    if (lodSettings.enable_simplification) {
        applySimplification(mergedGeom.get(), lodSettings.simplify);
    }

    // ===== 新增：收集材质信息 =====
    std::vector<std::shared_ptr<common::MaterialInfo>> materials;
    for (const auto& item : items) {
        auto matInfo = config_.geometryExtractor->getMaterial(item.get());
        materials.push_back(matInfo);
    }

    // 构建GLTF模型（传入材质信息）
    std::vector<unsigned char> glbData;
    buildGLTFModel(
        mergedGeom.get(),
        items,
        lodSettings.enable_draco,
        lodSettings.draco,
        glbData,
        materials  // 新增参数
    );

    // ... 其余代码 ...
}
```

---

## 验证清单

### 编译验证
- [ ] 所有修改的文件编译通过
- [ ] 无警告（或警告在可接受范围内）
- [ ] 链接成功

### 功能验证
- [ ] 基础颜色正确显示
- [ ] 金属度/粗糙度参数生效
- [ ] 自发光效果正确
- [ ] 基础颜色纹理加载
- [ ] 法线贴图效果
- [ ] 自发光纹理
- [ ] KTX2纹理压缩（如果启用）
- [ ] Alpha透明度检测
- [ ] 纹理变换（偏移、缩放、旋转）
- [ ] Specular-Glossiness材质

### 回归测试
- [ ] OSGB流程不受影响
- [ ] Shapefile流程不受影响
- [ ] 无材质模型正常显示

---

## 常见问题排查

### 问题1：纹理不显示
**排查步骤：**
1. 检查`getMaterial`是否正确返回材质信息
2. 检查纹理对象是否有效
3. 检查`processTexture`是否成功
4. 检查GLTF中是否正确引用纹理索引

### 问题2：材质颜色不正确
**排查步骤：**
1. 检查`extractPBRParams`提取的参数
2. 检查`MaterialBuilder`是否正确设置参数
3. 检查GLTF中baseColorFactor的值

### 问题3：纹理变换不生效
**排查步骤：**
1. 检查FBX中是否有纹理变换数据
2. 检查`TextureTransformInfo`是否正确复制
3. 检查`applyTextureTransform`是否被调用
4. 检查GLTF扩展是否正确写入

---

## 性能优化建议

1. **材质缓存**：相同材质只创建一次，使用哈希表缓存
2. **纹理缓存**：相同纹理只处理一次，避免重复编码
3. **延迟加载**：大纹理可以考虑延迟加载或流式加载

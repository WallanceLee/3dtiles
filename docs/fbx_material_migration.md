# FBX材质功能移植方案

## 概述

本文档详细描述如何将`master`分支中的FBX材质处理功能移植到`refactor-master`分支。

## 现状分析

### master分支支持的材质功能

1. **基础PBR材质**
   - 基础颜色（BaseColor）
   - 金属度（Metallic）
   - 粗糙度（Roughness）
   - 自发光（Emissive）

2. **纹理支持**
   - 基础颜色纹理
   - 法线贴图（Normal Map）
   - 自发光纹理
   - 金属度/粗糙度纹理
   - 遮挡纹理（AO）

3. **高级特性**
   - KTX2/Basis Universal纹理压缩
   - Alpha透明度检测
   - Unlit材质扩展
   - 双面渲染

### refactor-master分支现状

**已有的基础设施：**
- ✅ `gltf::MaterialBuilder` - GLTF材质构建器
- ✅ `gltf::GLTFBuilder` - GLTF模型构建器
- ✅ `osg::utils::MaterialUtils` - OSG材质提取工具
- ✅ `osg::utils::TextureUtils` - 纹理处理工具
- ✅ `gltf::extensions::TextureTransform` - 纹理变换扩展
- ✅ `gltf::extensions::SpecularGlossiness` - 高光-光泽度扩展

**缺失的部分：**
- ❌ `B3DMGenerator`没有调用材质构建逻辑
- ❌ `FBXGeometryExtractor`没有传递材质信息
- ❌ 材质扩展数据没有传递到GLTF构建器

## 移植方案

### 阶段1：扩展IGeometryExtractor接口

**文件：** `src/common/geometry_extractor.h`

```cpp
// 新增：纹理变换信息
struct TextureTransformInfo {
    float offset[2] = {0.0f, 0.0f};
    float scale[2] = {1.0f, 1.0f};
    float rotation = 0.0f;
    bool hasTransform = false;
};

// 新增：材质信息结构
struct MaterialInfo {
    // 基础PBR参数
    std::vector<double> baseColor = {1.0, 1.0, 1.0, 1.0};
    float roughnessFactor = 1.0f;
    float metallicFactor = 0.0f;
    std::vector<double> emissiveColor = {0.0, 0.0, 0.0};

    // OSG纹理对象
    osg::ref_ptr<osg::Texture> baseColorTexture;
    osg::ref_ptr<osg::Texture> normalTexture;
    osg::ref_ptr<osg::Texture> emissiveTexture;
    osg::ref_ptr<osg::Texture> metallicRoughnessTexture;
    osg::ref_ptr<osg::Texture> occlusionTexture;

    // 纹理变换
    TextureTransformInfo baseColorTransform;
    TextureTransformInfo normalTransform;
    TextureTransformInfo emissiveTransform;
    TextureTransformInfo metallicRoughnessTransform;
    TextureTransformInfo occlusionTransform;

    // Specular-Glossiness支持
    bool useSpecularGlossiness = false;
    std::vector<double> specularFactor = {1.0, 1.0, 1.0};
    double glossinessFactor = 1.0;
    osg::ref_ptr<osg::Texture> specularGlossinessTexture;

    // 其他属性
    bool doubleSided = true;
    std::string alphaMode = "OPAQUE";
};

class IGeometryExtractor {
public:
    virtual ~IGeometryExtractor() = default;

    // 现有方法...
    virtual std::vector<osg::ref_ptr<osg::Geometry>> extract(
        const spatial::core::SpatialItem* item) = 0;
    virtual std::string getId(const spatial::core::SpatialItem* item) = 0;
    virtual std::map<std::string, nlohmann::json> getAttributes(
        const spatial::core::SpatialItem* item) = 0;

    // 新增：获取材质信息
    virtual std::shared_ptr<MaterialInfo> getMaterial(
        const spatial::core::SpatialItem* item) = 0;
};
```

### 阶段2：FBXGeometryExtractor实现材质提取

**文件：** `src/fbx/fbx_geometry_extractor.cpp`

实现步骤：

1. **从StateSet提取基础PBR参数**
   ```cpp
   osg::utils::PBRParams pbrParams;
   osg::utils::MaterialUtils::extractPBRParams(stateSet, pbrParams);
   materialInfo->baseColor = pbrParams.baseColor;
   materialInfo->roughnessFactor = pbrParams.roughnessFactor;
   materialInfo->metallicFactor = pbrParams.metallicFactor;
   ```

2. **提取纹理对象**
   ```cpp
   materialInfo->baseColorTexture =
       const_cast<osg::Texture*>(osg::utils::MaterialUtils::getBaseColorTexture(stateSet));
   materialInfo->normalTexture =
       const_cast<osg::Texture*>(osg::utils::MaterialUtils::getNormalTexture(stateSet));
   materialInfo->emissiveTexture =
       const_cast<osg::Texture*>(osg::utils::MaterialUtils::getEmissiveTexture(stateSet));
   ```

3. **从FBX扩展数据提取高级特性**
   - 需要从`FBXLoader`获取`MaterialExtensionData`
   - 复制纹理变换参数
   - 复制Specular-Glossiness参数

### 阶段3：重构B3DMGenerator支持材质

**文件：** `src/b3dm/b3dm_generator.cpp`

核心修改点：

1. **修改`buildGLTFModel`函数**
   - 收集所有items的材质信息
   - 为每个唯一材质创建GLTF材质
   - 在创建primitive时关联材质索引

2. **新增`buildMaterial`辅助函数**
   ```cpp
   int B3DMGenerator::buildMaterial(
       const std::shared_ptr<common::MaterialInfo>& matInfo,
       tinygltf::Model& model,
       tinygltf::Buffer& buffer);
   ```

3. **纹理处理流程**
   ```cpp
   // 1. 处理纹理
   auto result = osg::utils::TextureUtils::processTexture(
       matInfo->baseColorTexture.get(), config_.enableTextureCompress);

   // 2. 添加到模型
   int texIdx = osg::utils::TextureUtils::addImageToModel(
       model, buffer, result.data, result.mimeType, useBasisu);

   // 3. 设置到材质
   matBuilder.setBaseColorTexture(texIdx);
   ```

### 阶段4：扩展MaterialBuilder

**文件：** `src/gltf/material_builder.h` 和 `src/gltf/material_builder.cpp`

新增功能：

1. **纹理变换支持**
   ```cpp
   void setBaseColorTextureTransform(const TextureTransformInfo& transform);
   void setNormalTextureTransform(const TextureTransformInfo& transform);
   void setEmissiveTextureTransform(const TextureTransformInfo& transform);
   ```

2. **Specular-Glossiness支持**
   ```cpp
   void setSpecularGlossiness(const SpecularGlossinessInfo& sg);
   ```

3. **在build()中应用扩展**
   ```cpp
   // 应用纹理变换
   if (baseColorTransform_.hasTransform) {
       gltf::extensions::applyTextureTransform(
           material, "baseColorTexture",
           convertToGLTFTransform(*baseColorTransform_), extMgr);
   }

   // 应用Specular-Glossiness
   if (useSpecularGlossiness_) {
       gltf::extensions::applySpecularGlossiness(
           material, specularGlossiness_, extMgr);
   }
   ```

## 关键代码复用

refactor-master已存在的可复用代码：

| 组件 | 文件路径 | 复用方式 |
|------|----------|----------|
| 纹理处理 | `osg::utils::TextureUtils::processTexture` | 完全复用 |
| 材质构建 | `gltf::MaterialBuilder` | 扩展纹理变换支持 |
| 扩展应用 | `gltf::extensions::applyTextureTransform` | 完全复用 |
| 图像添加 | `osg::utils::TextureUtils::addImageToModel` | 完全复用 |
| PBR参数提取 | `osg::utils::MaterialUtils::extractPBRParams` | 完全复用 |

## 实施优先级

| 优先级 | 任务 | 工作量 | 依赖 |
|--------|------|--------|------|
| P0 | 扩展`IGeometryExtractor`接口 | 小 | 无 |
| P0 | 实现`FBXGeometryExtractor::getMaterial` | 中 | P0 |
| P0 | `B3DMGenerator`材质集成 | 大 | P0, P1 |
| P1 | `MaterialBuilder`纹理变换扩展 | 中 | 无 |
| P1 | `MaterialBuilder`Specular-Glossiness | 中 | 无 |
| P2 | 多材质分组优化 | 大 | P0 |

## 测试验证点

1. **基础材质**
   - [ ] 基础颜色正确显示
   - [ ] 金属度/粗糙度参数正确
   - [ ] 自发光效果正确

2. **纹理支持**
   - [ ] 基础颜色纹理加载
   - [ ] 法线贴图效果
   - [ ] 自发光纹理

3. **高级特性**
   - [ ] KTX2纹理压缩
   - [ ] Alpha透明度
   - [ ] 纹理变换（偏移、缩放、旋转）
   - [ ] Specular-Glossiness材质

4. **兼容性**
   - [ ] 与现有OSGB流程不冲突
   - [ ] 与Shapefile流程不冲突

## 风险与注意事项

1. **性能考虑**
   - 纹理处理是CPU密集型操作，需要考虑缓存机制
   - 相同纹理应该只处理一次

2. **内存管理**
   - OSG纹理对象的引用计数需要正确处理
   - 大纹理可能导致内存峰值

3. **错误处理**
   - 纹理加载失败需要优雅降级
   - 不支持的纹理格式需要处理

## 附录：参考代码

### master分支材质处理关键代码位置

- `src/FBXPipeline.cpp:1144-1780` - 完整材质转换逻辑
- `src/FBXPipeline.cpp:408` - 材质分组逻辑
- `src/FBXPipeline.cpp:424` - StateSet到材质映射

### refactor-master相关代码位置

- `src/gltf/material_builder.h` - 材质构建器接口
- `src/gltf/material_builder.cpp` - 材质构建实现
- `src/osg/utils/material_utils.h` - 材质工具类
- `src/osg/utils/texture_utils.h` - 纹理工具类
- `src/gltf/extensions/texture_transform.h` - 纹理变换扩展
- `src/gltf/extensions/specular_glossiness.h` - 高光-光泽度扩展

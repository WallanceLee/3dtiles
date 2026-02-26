# FBX材质移植接口设计文档

本文档详细描述FBX材质移植过程中涉及的接口设计，包括数据结构、类接口和模块间的交互关系。

## 1. 数据结构设计

### 1.1 TextureTransformInfo

```cpp
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

} // namespace common
```

### 1.2 MaterialInfo

```cpp
namespace common {

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
     * 
     * 线性颜色空间，RGBA格式
     * 默认: [1.0, 1.0, 1.0, 1.0]（白色不透明）
     */
    std::vector<double> baseColor = {1.0, 1.0, 1.0, 1.0};
    
    /**
     * @brief 粗糙度
     * 
     * 范围: [0.0, 1.0]
     * 0.0 = 完全光滑（镜面反射）
     * 1.0 = 完全粗糙（漫反射）
     * 默认: 1.0
     */
    float roughnessFactor = 1.0f;
    
    /**
     * @brief 金属度
     * 
     * 范围: [0.0, 1.0]
     * 0.0 = 非金属（介电质）
     * 1.0 = 金属
     * 默认: 0.0
     */
    float metallicFactor = 0.0f;
    
    /**
     * @brief 自发光颜色
     * 
     * 线性颜色空间，RGB格式
     * 默认: [0.0, 0.0, 0.0]（无自发光）
     */
    std::vector<double> emissiveColor = {0.0, 0.0, 0.0};
    
    /**
     * @brief 遮挡强度
     * 
     * 范围: [0.0, 1.0]
     * 仅当使用遮挡纹理时有效
     * 默认: 1.0
     */
    float aoStrength = 1.0f;
    
    // ==================== 纹理对象 ====================
    
    /**
     * @brief 基础颜色纹理（纹理单元0）
     * 
     * 与baseColor相乘
     * 支持RGB或RGBA格式
     */
    osg::ref_ptr<osg::Texture> baseColorTexture;
    
    /**
     * @brief 法线纹理（纹理单元1）
     * 
     * 切线空间法线贴图
     * 通常使用RGB格式存储XYZ
     */
    osg::ref_ptr<osg::Texture> normalTexture;
    
    /**
     * @brief 金属度/粗糙度纹理（纹理单元2）
     * 
     * 使用GB通道：
     * - G通道: 粗糙度
     * - B通道: 金属度
     */
    osg::ref_ptr<osg::Texture> metallicRoughnessTexture;
    
    /**
     * @brief 遮挡纹理（纹理单元3）
     * 
     * 通常使用R通道存储AO值
     */
    osg::ref_ptr<osg::Texture> occlusionTexture;
    
    /**
     * @brief 自发光纹理（纹理单元4）
     * 
     * 与emissiveColor相乘
     */
    osg::ref_ptr<osg::Texture> emissiveTexture;
    
    // ==================== 纹理变换 ====================
    
    TextureTransformInfo baseColorTransform;           // 基础颜色纹理变换
    TextureTransformInfo normalTransform;              // 法线纹理变换
    TextureTransformInfo metallicRoughnessTransform;   // 金属度/粗糙度纹理变换
    TextureTransformInfo occlusionTransform;           // 遮挡纹理变换
    TextureTransformInfo emissiveTransform;            // 自发光纹理变换
    
    // ==================== Specular-Glossiness ====================
    
    /**
     * @brief 是否使用Specular-Glossiness工作流
     * 
     * 用于支持传统FBX材质
     * 启用时使用KHR_materials_pbrSpecularGlossiness扩展
     */
    bool useSpecularGlossiness = false;
    
    /**
     * @brief 漫反射因子（Specular-Glossiness）
     * 
     * 对应Metallic-Roughness的baseColor
     * 默认: [1.0, 1.0, 1.0, 1.0]
     */
    std::vector<double> diffuseFactor = {1.0, 1.0, 1.0, 1.0};
    
    /**
     * @brief 高光因子（Specular-Glossiness）
     * 
     * 范围: [0.0, 1.0] per channel
     * 默认: [1.0, 1.0, 1.0]
     */
    std::vector<double> specularFactor = {1.0, 1.0, 1.0};
    
    /**
     * @brief 光泽度（Specular-Glossiness）
     * 
     * 范围: [0.0, 1.0]
     * 0.0 = 粗糙
     * 1.0 = 光滑
     * 默认: 1.0
     */
    double glossinessFactor = 1.0;
    
    /**
     * @brief Specular-Glossiness纹理
     * 
     * RGB通道存储高光颜色，A通道存储光泽度
     */
    osg::ref_ptr<osg::Texture> specularGlossinessTexture;
    
    /**
     * @brief 漫反射纹理（Specular-Glossiness）
     */
    osg::ref_ptr<osg::Texture> diffuseTexture;
    
    // ==================== 其他属性 ====================
    
    /**
     * @brief 双面渲染
     * 
     * true = 渲染正面和背面
     * false = 只渲染正面（背面剔除）
     * 默认: true
     */
    bool doubleSided = true;
    
    /**
     * @brief Alpha模式
     * 
     * - "OPAQUE": 不透明（忽略Alpha）
     * - "MASK": Alpha裁剪（使用alphaCutoff）
     * - "BLEND": Alpha混合（半透明）
     * 默认: "OPAQUE"
     */
    std::string alphaMode = "OPAQUE";
    
    /**
     * @brief Alpha裁剪值
     * 
     * 仅当alphaMode为"MASK"时有效
     * 范围: [0.0, 1.0]
     * 默认: 0.5
     */
    float alphaCutoff = 0.5f;
    
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

} // namespace common
```

## 2. 接口设计

### 2.1 IGeometryExtractor（扩展）

```cpp
namespace common {

/**
 * @brief 几何体提取器接口
 * 
 * 抽象不同数据源(FBX/Shapefile/OSGB)的几何体和材质提取逻辑
 */
class IGeometryExtractor {
public:
    virtual ~IGeometryExtractor() = default;

    /**
     * @brief 从空间对象提取几何体
     * 
     * @param item 空间对象
     * @return 几何体列表（可能包含多个子几何体）
     */
    virtual std::vector<osg::ref_ptr<osg::Geometry>> extract(
        const spatial::core::SpatialItem* item) = 0;

    /**
     * @brief 获取对象的唯一标识
     * 
     * 用于生成BatchID，在3D Tiles中标识单个要素
     * 
     * @param item 空间对象
     * @return 唯一标识字符串
     */
    virtual std::string getId(const spatial::core::SpatialItem* item) = 0;

    /**
     * @brief 获取对象的属性
     * 
     * 用于生成BatchTable，存储要素的属性数据
     * 
     * @param item 空间对象
     * @return 属性键值对
     */
    virtual std::map<std::string, nlohmann::json> getAttributes(
        const spatial::core::SpatialItem* item) = 0;

    /**
     * @brief 获取对象的材质信息（新增）
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
```

### 2.2 FBXGeometryExtractor

```cpp
namespace fbx {

/**
 * @brief FBX几何体提取器
 * 
 * 从FBXSpatialItemAdapter提取几何体和材质信息
 */
class FBXGeometryExtractor : public common::IGeometryExtractor {
public:
    FBXGeometryExtractor() = default;
    ~FBXGeometryExtractor() override = default;

    // 禁用拷贝
    FBXGeometryExtractor(const FBXGeometryExtractor&) = delete;
    FBXGeometryExtractor& operator=(const FBXGeometryExtractor&) = delete;

    /**
     * @brief 提取几何体
     * 
     * 从FBX空间对象提取OSG几何体，应用坐标变换（Y-up到Z-up）
     */
    std::vector<osg::ref_ptr<osg::Geometry>> extract(
        const spatial::core::SpatialItem* item) override;

    /**
     * @brief 获取节点名称作为ID
     */
    std::string getId(const spatial::core::SpatialItem* item) override;

    /**
     * @brief 获取节点属性
     */
    std::map<std::string, nlohmann::json> getAttributes(
        const spatial::core::SpatialItem* item) override;

    /**
     * @brief 获取材质信息（新增）
     * 
     * 提取流程：
     * 1. 获取几何体的StateSet
     * 2. 使用MaterialUtils提取PBR参数
     * 3. 提取纹理对象（从各个纹理单元）
     * 4. 从FBX扩展数据提取纹理变换和Specular-Glossiness参数
     * 
     * @param item FBX空间对象
     * @return 完整的材质信息
     */
    std::shared_ptr<common::MaterialInfo> getMaterial(
        const spatial::core::SpatialItem* item) override;

private:
    /**
     * @brief 复制纹理变换数据
     */
    void copyTextureTransform(const ::TextureTransformData& src,
                              common::TextureTransformInfo& dst);
};

} // namespace fbx
```

### 2.3 MaterialBuilder（扩展）

```cpp
namespace gltf {

/**
 * @brief GLTF材质构建器
 * 
 * 简化GLTF Material的创建，支持各种扩展
 */
class MaterialBuilder {
public:
    MaterialBuilder();
    ~MaterialBuilder() = default;

    // ==================== 基础参数设置 ====================

    void setBaseColor(const std::vector<double>& color);
    void setPBRParams(float roughness, float metallic);
    void setEmissiveColor(const std::vector<double>& color);
    void setDoubleSided(bool doubleSided);
    void setAlphaMode(const std::string& alphaMode);
    void setAlphaCutoff(float cutoff);
    void setUnlit(bool unlit);

    // ==================== 纹理设置 ====================

    void setBaseColorTexture(int textureIndex);
    void setNormalTexture(int textureIndex);
    void setEmissiveTexture(int textureIndex);
    void setMetallicRoughnessTexture(int textureIndex);
    void setOcclusionTexture(int textureIndex);

    // ==================== 纹理变换（新增） ====================

    /**
     * @brief 设置基础颜色纹理变换
     * @param transform 纹理变换参数
     */
    void setBaseColorTextureTransform(const extensions::TextureTransform& transform);

    /**
     * @brief 设置法线纹理变换
     * @param transform 纹理变换参数
     */
    void setNormalTextureTransform(const extensions::TextureTransform& transform);

    /**
     * @brief 设置自发光纹理变换
     * @param transform 纹理变换参数
     */
    void setEmissiveTextureTransform(const extensions::TextureTransform& transform);

    /**
     * @brief 设置金属度/粗糙度纹理变换
     * @param transform 纹理变换参数
     */
    void setMetallicRoughnessTextureTransform(const extensions::TextureTransform& transform);

    /**
     * @brief 设置遮挡纹理变换
     * @param transform 纹理变换参数
     */
    void setOcclusionTextureTransform(const extensions::TextureTransform& transform);

    // ==================== Specular-Glossiness（新增） ====================

    /**
     * @brief 设置Specular-Glossiness参数
     * @param sg Specular-Glossiness数据
     */
    void setSpecularGlossiness(const extensions::SpecularGlossiness& sg);

    // ==================== 构建 ====================

    /**
     * @brief 构建GLTF材质
     * 
     * 构建流程：
     * 1. 创建tinygltf::Material
     * 2. 设置PBR参数
     * 3. 设置纹理引用
     * 4. 应用纹理变换扩展（如果有）
     * 5. 应用Specular-Glossiness扩展（如果有）
     * 6. 应用Unlit扩展（如果有）
     * 7. 添加到模型并返回索引
     * 
     * @param model GLTF模型
     * @param extMgr 扩展管理器（记录使用的扩展）
     * @return 材质索引
     */
    int build(tinygltf::Model& model, ExtensionManager& extMgr);

    /**
     * @brief 清空所有设置
     */
    void clear();

private:
    // 基础参数
    std::vector<double> baseColor_;
    float roughnessFactor_;
    float metallicFactor_;
    std::vector<double> emissiveColor_;
    bool doubleSided_;
    std::string alphaMode_;
    float alphaCutoff_;
    bool unlit_;

    // 纹理索引
    int baseColorTexture_;
    int normalTexture_;
    int emissiveTexture_;
    int metallicRoughnessTexture_;
    int occlusionTexture_;

    // 纹理变换（新增）
    std::optional<extensions::TextureTransform> baseColorTransform_;
    std::optional<extensions::TextureTransform> normalTransform_;
    std::optional<extensions::TextureTransform> emissiveTransform_;
    std::optional<extensions::TextureTransform> metallicRoughnessTransform_;
    std::optional<extensions::TextureTransform> occlusionTransform_;

    // Specular-Glossiness（新增）
    std::optional<extensions::SpecularGlossiness> specularGlossiness_;
};

} // namespace gltf
```

### 2.4 B3DMGenerator（扩展）

```cpp
namespace b3dm {

/**
 * @brief B3DM内容生成器
 * 
 * 生成支持材质的B3DM数据
 */
class B3DMGenerator {
public:
    explicit B3DMGenerator(const B3DMGeneratorConfig& config);
    ~B3DMGenerator() = default;

    /**
     * @brief 生成单LOD级别的B3DM
     */
    std::string generate(
        const spatial::core::SpatialItemRefList& items,
        const LODLevelSettings& lodSettings);

    /**
     * @brief 生成多LOD级别的B3DM文件
     */
    std::vector<LODFileInfo> generateLODFiles(
        const spatial::core::SpatialItemRefList& items,
        const std::string& outputDir,
        const std::string& baseFilename,
        const std::vector<LODLevelSettings>& lodLevels);

private:
    // 纹理类型枚举（新增）
    enum class TextureType {
        BASE_COLOR,
        NORMAL,
        EMISSIVE,
        METALLIC_ROUGHNESS,
        OCCLUSION
    };

    /**
     * @brief 提取并合并几何体（现有）
     */
    osg::ref_ptr<osg::Geometry> extractAndMergeGeometries(
        const spatial::core::SpatialItemRefList& items);

    /**
     * @brief 应用几何简化（现有）
     */
    void applySimplification(osg::Geometry* geometry,
                             const SimplificationParams& params);

    /**
     * @brief 构建Batch数据（现有）
     */
    BatchData buildBatchData(const spatial::core::SpatialItemRefList& items);

    /**
     * @brief 构建GLTF模型（扩展）
     * 
     * @param mergedGeom 合并后的几何体
     * @param items 空间对象列表
     * @param enableDraco 是否启用Draco压缩
     * @param dracoParams Draco参数
     * @param glbData 输出的GLB数据
     * @param materials 材质信息列表（新增）
     */
    void buildGLTFModel(
        osg::Geometry* mergedGeom,
        const spatial::core::SpatialItemRefList& items,
        bool enableDraco,
        const DracoCompressionParams& dracoParams,
        std::vector<unsigned char>& glbData,
        const std::vector<std::shared_ptr<common::MaterialInfo>>& materials);

    /**
     * @brief 构建材质（新增）
     * 
     * 将MaterialInfo转换为GLTF材质
     * 
     * @param matInfo 材质信息
     * @param model GLTF模型
     * @param buffer GLTF缓冲区
     * @return 材质索引，失败返回-1
     */
    int buildMaterial(
        const std::shared_ptr<common::MaterialInfo>& matInfo,
        tinygltf::Model& model,
        tinygltf::Buffer& buffer);

    /**
     * @brief 处理并添加纹理（新增）
     * 
     * 统一的纹理处理流程：
     * 1. 处理纹理（可能包括KTX2压缩）
     * 2. 添加到GLTF模型
     * 3. 设置到材质构建器
     * 4. 应用纹理变换（如果有）
     * 
     * @param texture OSG纹理对象
     * @param transform 纹理变换
     * @param matBuilder 材质构建器
     * @param model GLTF模型
     * @param buffer GLTF缓冲区
     * @param extMgr 扩展管理器
     * @param type 纹理类型
     */
    void processAndAddTexture(
        osg::Texture* texture,
        const common::TextureTransformInfo& transform,
        gltf::MaterialBuilder& matBuilder,
        tinygltf::Model& model,
        tinygltf::Buffer& buffer,
        gltf::ExtensionManager& extMgr,
        TextureType type);

    /**
     * @brief 计算材质哈希键（新增）
     * 
     * 用于材质去重，相同材质只创建一次
     */
    std::string computeMaterialKey(
        const std::shared_ptr<common::MaterialInfo>& matInfo);

    /**
     * @brief 转换纹理变换格式（新增）
     */
    gltf::extensions::TextureTransform convertToGLTFTransform(
        const common::TextureTransformInfo& info);

    B3DMGeneratorConfig config_;
};

} // namespace b3dm
```

## 3. 模块交互关系

```
┌─────────────────────────────────────────────────────────────────┐
│                        B3DMGenerator                            │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  generate()                                             │   │
│  │  1. 提取几何体 (extractAndMergeGeometries)              │   │
│  │  2. 收集材质信息 (getMaterial)                          │   │
│  │  3. 构建GLTF (buildGLTFModel)                           │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    IGeometryExtractor                           │
│              (FBXGeometryExtractor实现)                          │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  getMaterial()                                          │   │
│  │  1. 获取StateSet                                        │   │
│  │  2. 提取PBR参数 (MaterialUtils)                         │   │
│  │  3. 提取纹理                                            │   │
│  │  4. 提取扩展数据 (TextureTransform, SpecularGlossiness) │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                     MaterialBuilder                             │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  build()                                                │   │
│  │  1. 设置PBR参数                                         │   │
│  │  2. 设置纹理                                            │   │
│  │  3. 应用纹理变换扩展                                    │   │
│  │  4. 应用SpecularGlossiness扩展                          │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                     TextureUtils                                │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  processTexture()                                       │   │
│  │  1. 检查Alpha通道                                       │   │
│  │  2. KTX2压缩（可选）                                    │   │
│  │  3. 编码为PNG/JPEG                                      │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

## 4. 数据流向

### 4.1 材质提取流程

```
FBX文件
    │
    ▼
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│  ufbx_material │──▶│  osg::StateSet │──▶│ MaterialInfo │
└─────────────┘     └─────────────┘     └─────────────┘
      │                    │                    │
      │                    │                    │
      ▼                    ▼                    ▼
   PBR参数              Uniforms           完整材质信息
   (base_color)        (roughnessFactor)   + 纹理对象
   (diffuse_color)     (metallicFactor)    + 纹理变换
   (specular_color)    (textures)          + SpecularGlossiness
```

### 4.2 材质构建流程

```
MaterialInfo
    │
    ▼
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│  processTexture │──▶│  addImageToModel │──▶│  textureIndex  │
└─────────────┘     └─────────────┘     └─────────────┘
                                              │
                                              ▼
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│  MaterialBuilder │──▶│  build()       │──▶│  tinygltf::Material │
└─────────────┘     └─────────────┘     └─────────────┘
       │                                        │
       │                                        ▼
       │                                 GLTF JSON + 二进制纹理
       │
       ▼
设置参数：
- baseColor
- roughnessFactor
- metallicFactor
- 纹理索引
- 纹理变换扩展
- SpecularGlossiness扩展
```

## 5. 扩展点设计

### 5.1 支持新的纹理类型

如需支持新的纹理类型（如Clearcoat），需要：

1. 在`MaterialInfo`中添加纹理对象和变换
2. 在`MaterialBuilder`中添加设置方法
3. 在`B3DMGenerator::processAndAddTexture`中添加处理逻辑

### 5.2 支持新的材质扩展

如需支持新的GLTF材质扩展，需要：

1. 在`gltf/extensions/`下创建新的扩展头文件
2. 实现`applyXXX`函数
3. 在`MaterialBuilder::build`中调用

### 5.3 支持新的数据源

如需支持新的数据源（如glTF直接输入），需要：

1. 实现`IGeometryExtractor`接口
2. 实现`getMaterial`方法返回`MaterialInfo`
3. 其余流程自动复用

## 6. 错误处理策略

### 6.1 材质提取失败

```cpp
std::shared_ptr<common::MaterialInfo> getMaterial(...) {
    if (!stateSet) {
        // 返回默认材质，不中断流程
        return std::make_shared<common::MaterialInfo>();
    }
    // ...
}
```

### 6.2 纹理处理失败

```cpp
void processAndAddTexture(...) {
    auto result = osg::utils::TextureUtils::processTexture(...);
    if (!result.success) {
        LOG_W("Failed to process texture, using material without texture");
        return;  // 不使用纹理，继续流程
    }
    // ...
}
```

### 6.3 材质构建失败

```cpp
int buildMaterial(...) {
    if (!matInfo) {
        return -1;  // 返回-1表示使用默认材质
    }
    // ...
}
```

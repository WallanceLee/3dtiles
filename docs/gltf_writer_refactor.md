# glTF Writer 重构方案

## 1. 边界定义

### 1.1 模块边界

| 模块 | 职责 | 不包含 |
|------|------|--------|
| **gltf_writer** | glTF 2.0 模型构建、Extension 管理 | B3DM 封装、Tileset JSON 生成 |
| **b3dm_writer** | B3DM 格式封装（FeatureTable、BatchTable） | glTF 内容生成 |
| **tileset_writer** | Tileset JSON 生成、空间索引结构 | 具体瓦片内容 |

### 1.2 glTF 坐标系约定

glTF 2.0 规范定义：
- **上轴**: Y-Up（Y 轴向上）
- **手性**: 右手坐标系
- **前向**: -Z 方向

**坐标轴转换责任划分**：

| 数据源 | 源坐标系 | 转换责任 |
|--------|----------|----------|
| FBX | Y-Up（右手） | 无需转换 |
| OSGB | Z-Up（右手） | Pipeline 层负责 Z-Up → Y-Up |
| Shapefile | 投影坐标 | Pipeline 层 + coords 模块负责 |

**gltf_writer 不负责坐标轴转换**，所有输入数据应已转换为 Y-Up 坐标系。

### 1.3 与现有模块的关系

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              Pipeline Layer                                  │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐             │
│  │ ShapefilePipeline│  │  OsgbPipeline   │  │   FBXPipeline   │             │
│  │  - 坐标转换      │  │  - Z-Up→Y-Up   │  │  - 材质转换     │             │
│  │  - 属性处理      │  │  - 纹理处理     │  │  - 纹理变换     │             │
│  └────────┬────────┘  └────────┬────────┘  └────────┬────────┘             │
│           │                    │                    │                       │
│           └────────────────────┼────────────────────┘                       │
│                                ▼                                            │
├─────────────────────────────────────────────────────────────────────────────┤
│                           核心模块层                                         │
│                                                                             │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐             │
│  │ coords          │  │ gltf_writer     │  │ mesh_processor  │             │
│  │ CoordinateSystem│  │ GltfBuilder     │  │ simplify_mesh   │             │
│  │ CoordinateTrans-│  │ ExtensionMgr    │  │ compress_mesh   │             │
│  │ former          │  │                 │  │ process_texture │             │
│  └─────────────────┘  └─────────────────┘  └─────────────────┘             │
│                                                                             │
│  ┌─────────────────┐  ┌─────────────────┐                                  │
│  │ b3dm_writer     │  │ tileset_writer  │                                  │
│  │ B3dmWriter      │  │ TilesetWriter   │                                  │
│  │ (独立模块)      │  │ (独立模块)      │                                  │
│  └─────────────────┘  └─────────────────┘                                  │
└─────────────────────────────────────────────────────────────────────────────┘
```

## 2. 当前问题分析

### 2.1 现有代码中的 glTF 底层操作

通过分析 `FBXPipeline.cpp`、`osgb23dtile.cpp`、`shp23dtile.cpp`，识别出以下重复的底层操作：

#### 2.1.1 Buffer 管理

```cpp
// 三个 Pipeline 都有类似代码
tinygltf::Buffer buffer;
buffer.data.resize(offset + size);
memcpy(buffer.data.data() + offset, data, size);
alignment_buffer(buffer.data);  // 4 字节对齐
model.buffers.push_back(std::move(buffer));
```

#### 2.1.2 BufferView 管理

```cpp
// 每次创建 BufferView 都需要手动设置属性
tinygltf::BufferView bv;
bv.buffer = 0;
bv.byteOffset = offset;
bv.byteLength = length;
bv.target = TINYGLTF_TARGET_ARRAY_BUFFER;  // 或 ELEMENT_ARRAY_BUFFER
model.bufferViews.push_back(bv);
```

#### 2.1.3 Accessor 管理

```cpp
// 需要手动计算 min/max
tinygltf::Accessor acc;
acc.bufferView = bvIdx;
acc.byteOffset = 0;
acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
acc.count = count;
acc.type = TINYGLTF_TYPE_VEC3;

// 手动计算包围盒
std::vector<double> box_max = {-1e38, -1e38, -1e38};
std::vector<double> box_min = {1e38, 1e38, 1e38};
for (int i = 0; i < count; ++i) {
    SET_MAX(box_max[0], positions[i*3]);
    SET_MIN(box_min[0], positions[i*3]);
    // ...
}
acc.minValues = box_min;
acc.maxValues = box_max;
model.accessors.push_back(acc);
```

#### 2.1.4 索引类型选择

```cpp
// osgb23dtile.cpp 中的实现
int pick_index_component_type(uint32_t max_index) {
    if (max_index <= std::numeric_limits<uint8_t>::max()) {
        return TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
    }
    if (max_index <= std::numeric_limits<uint16_t>::max()) {
        return TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT;
    }
    return TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
}
```

#### 2.1.5 材质创建

```cpp
// shp23dtile.cpp
tinygltf::Material make_color_material(double r, double g, double b) {
    tinygltf::Material material;
    material.name = "...";
    material.pbrMetallicRoughness.baseColorFactor = {r, g, b, 1};
    material.pbrMetallicRoughness.roughnessFactor = 0.7;
    material.pbrMetallicRoughness.metallicFactor = 0.3;
    return material;
}

// osgb23dtile.cpp - 略有不同的默认值
tinygltf::Material make_color_material_osgb(double r, double g, double b) {
    tinygltf::Material material;
    material.pbrMetallicRoughness.metallicFactor = 0.0;
    material.pbrMetallicRoughness.roughnessFactor = 1.0;
    return material;
}
```

#### 2.1.6 Extension 管理

```cpp
// 每个 Pipeline 独立处理 extension 注册
if (std::find(model.extensionsUsed.begin(), model.extensionsUsed.end(),
    "KHR_draco_mesh_compression") == model.extensionsUsed.end()) {
    model.extensionsUsed.push_back("KHR_draco_mesh_compression");
    model.extensionsRequired.push_back("KHR_draco_mesh_compression");
}

// 应用 extension 到不同对象
material.extensions["KHR_materials_unlit"] = tinygltf::Value(...);
primitive.extensions["KHR_draco_mesh_compression"] = tinygltf::Value(...);
texture.extensions["KHR_texture_basisu"] = tinygltf::Value(...);
```

#### 2.1.7 纹理/图像管理

```cpp
// 图像嵌入
tinygltf::Image gltfImg;
gltfImg.mimeType = "image/png";  // 或 "image/jpeg", "image/ktx2"
gltfImg.bufferView = bvImgIdx;
model.images.push_back(gltfImg);

// 纹理创建
tinygltf::Texture gltfTex;
if (enable_texture_compress) {
    tinygltf::Value::Object basisu_ext;
    basisu_ext["source"] = tinygltf::Value(imgIdx);
    gltfTex.extensions["KHR_texture_basisu"] = tinygltf::Value(basisu_ext);
} else {
    gltfTex.source = imgIdx;
}
model.textures.push_back(gltfTex);
```

#### 2.1.8 场景结构

```cpp
// Node/Scene 组装
tinygltf::Node node;
node.mesh = meshIdx;
model.nodes.push_back(node);

tinygltf::Scene scene;
scene.nodes.push_back(nodeIdx);
model.scenes.push_back(scene);
model.defaultScene = 0;
```

### 2.2 问题总结

| 问题类型 | 具体表现 | 影响 |
|----------|----------|------|
| **代码重复** | Buffer/Accessor 创建逻辑在三个 Pipeline 中重复 | 维护成本高 |
| **默认值不一致** | `make_color_material` 在不同文件中有不同默认值 | 行为不可预测 |
| **手动计算** | 包围盒、索引类型需要手动计算 | 易出错 |
| **Extension 分散** | Extension 注册逻辑分散在各处 | 难以扩展 |
| **类型不安全** | 直接使用 tinygltf 原始类型 | 无编译期检查 |

### 2.3 已实现的 Extension

| Extension | 使用位置 | 当前状态 |
|-----------|----------|----------|
| `KHR_draco_mesh_compression` | Primitive | 已实现 |
| `KHR_materials_unlit` | Material | 已实现 |
| `KHR_texture_basisu` | Texture | 已实现 |

### 2.4 计划新增的 Extension

| Extension | 使用位置 | 用途 | 优先级 |
|-----------|----------|------|--------|
| `KHR_texture_transform` | TextureInfo | FBX 纹理 UV 变换 | **P0** |
| `KHR_materials_pbrSpecularGlossiness` | Material | FBX PBR 高光光泽度工作流 | **P0** |

## 3. 设计目标

1. **业务层抽象**：封装 Buffer/Accessor/包围盒等底层概念
2. **Extension 统一管理**：集中的 extension 注册和应用机制
3. **类型安全**：为每个 extension 定义强类型数据结构
4. **默认值统一**：提供一致的默认材质、采样器配置
5. **最小影响范围**：新增功能不影响现有功能，保证可回归测试
6. **与现有架构兼容**：与 `coords` 命名空间设计风格一致

## 4. 底层概念抽象

### 4.1 类型定义（types.h）

```cpp
#pragma once
#include <cstdint>
#include <array>
#include <vector>
#include <string>
#include <optional>

namespace gltf_writer {

enum class ComponentType : int {
    Byte = 5120,
    UnsignedByte = 5121,
    Short = 5122,
    UnsignedShort = 5123,
    UnsignedInt = 5125,
    Float = 5126
};

enum class AccessorType : int {
    Scalar = TINYGLTF_TYPE_SCALAR,
    Vec2 = TINYGLTF_TYPE_VEC2,
    Vec3 = TINYGLTF_TYPE_VEC3,
    Vec4 = TINYGLTF_TYPE_VEC4,
    Mat2 = TINYGLTF_TYPE_MAT2,
    Mat3 = TINYGLTF_TYPE_MAT3,
    Mat4 = TINYGLTF_TYPE_MAT4
};

enum class PrimitiveMode : int {
    Points = 0,
    Lines = 1,
    LineLoop = 2,
    LineStrip = 3,
    Triangles = 4,
    TriangleStrip = 5,
    TriangleFan = 6
};

enum class BufferViewTarget : int {
    None = 0,
    ArrayBuffer = 34962,
    ElementArrayBuffer = 34963
};

enum class TextureFilter : int {
    Nearest = 9728,
    Linear = 9729,
    NearestMipmapNearest = 9984,
    LinearMipmapNearest = 9985,
    NearestMipmapLinear = 9986,
    LinearMipmapLinear = 9987
};

enum class TextureWrap : int {
    ClampToEdge = 33071,
    MirroredRepeat = 33648,
    Repeat = 10497
};

inline int toTinyGltf(ComponentType t) { return static_cast<int>(t); }
inline int toTinyGltf(AccessorType t) { return static_cast<int>(t); }
inline int toTinyGltf(PrimitiveMode m) { return static_cast<int>(m); }
inline int toTinyGltf(BufferViewTarget t) { return static_cast<int>(t); }

}
```

### 4.2 包围盒（BoundingBox）

```cpp
#pragma once
#include <array>
#include <vector>
#include <span>
#include <limits>
#include <algorithm>

namespace gltf_writer {

template<typename T, size_t N = 3>
struct BoundingBox {
    std::array<T, N> min;
    std::array<T, N> max;

    BoundingBox() {
        min.fill(std::numeric_limits<T>::max());
        max.fill(std::numeric_limits<T>::lowest());
    }

    explicit BoundingBox(std::span<const T> data, size_t stride = N) {
        min.fill(std::numeric_limits<T>::max());
        max.fill(std::numeric_limits<T>::lowest());
        const size_t count = data.size() / stride;
        for (size_t i = 0; i < count; ++i) {
            for (size_t j = 0; j < N && j < stride; ++j) {
                T v = data[i * stride + j];
                min[j] = std::min(min[j], v);
                max[j] = std::max(max[j], v);
            }
        }
    }

    void expand(const T* point) {
        for (size_t i = 0; i < N; ++i) {
            min[i] = std::min(min[i], point[i]);
            max[i] = std::max(max[i], point[i]);
        }
    }

    void expand(const BoundingBox<T, N>& other) {
        for (size_t i = 0; i < N; ++i) {
            min[i] = std::min(min[i], other.min[i]);
            max[i] = std::max(max[i], other.max[i]);
        }
    }

    std::array<T, N> center() const {
        std::array<T, N> c;
        for (size_t i = 0; i < N; ++i) {
            c[i] = (min[i] + max[i]) / T(2);
        }
        return c;
    }

    std::array<T, N> size() const {
        std::array<T, N> s;
        for (size_t i = 0; i < N; ++i) {
            s[i] = max[i] - min[i];
        }
        return s;
    }

    std::vector<double> minAsDouble() const {
        return std::vector<double>(min.begin(), min.end());
    }

    std::vector<double> maxAsDouble() const {
        return std::vector<double>(max.begin(), max.end());
    }

    bool isValid() const {
        for (size_t i = 0; i < N; ++i) {
            if (min[i] > max[i]) return false;
        }
        return true;
    }

    static BoundingBox<T, N> Invalid() {
        return {};
    }

    static BoundingBox<T, N> FromPoint(const std::array<T, N>& point) {
        BoundingBox<T, N> box;
        box.min = point;
        box.max = point;
        return box;
    }
};

using BoundingBox2F = BoundingBox<float, 2>;
using BoundingBox3F = BoundingBox<float, 3>;
using BoundingBox2D = BoundingBox<double, 2>;
using BoundingBox3D = BoundingBox<double, 3>;

}
```

### 4.3 BufferBuilder（Buffer/BufferView 管理）

```cpp
#pragma once
#include <vector>
#include <span>
#include <cstddef>
#include <cstring>
#include "types.h"

namespace gltf_writer {

class BufferBuilder {
public:
    BufferBuilder() = default;

    size_t append(std::span<const std::byte> data) {
        size_t offset = buffer_data_.size();
        buffer_data_.insert(buffer_data_.end(), data.begin(), data.end());
        return offset;
    }

    size_t append(const void* data, size_t size) {
        return append(std::span<const std::byte>(
            static_cast<const std::byte*>(data), size));
    }

    template<typename T>
    size_t append(std::span<const T> data) {
        return append(std::as_bytes(data));
    }

    void align(size_t alignment = 4) {
        size_t padding = (alignment - (buffer_data_.size() % alignment)) % alignment;
        if (padding > 0) {
            buffer_data_.resize(buffer_data_.size() + padding);
        }
    }

    int createBufferView(size_t byte_offset, size_t byte_length,
                         BufferViewTarget target = BufferViewTarget::None) {
        tinygltf::BufferView bv;
        bv.buffer = 0;
        bv.byteOffset = static_cast<int>(byte_offset);
        bv.byteLength = static_cast<int>(byte_length);
        bv.target = toTinyGltf(target);
        int index = static_cast<int>(buffer_views_.size());
        buffer_views_.push_back(bv);
        return index;
    }

    template<typename T>
    int addBufferView(std::span<const T> data, BufferViewTarget target = BufferViewTarget::None) {
        align();
        size_t offset = append(data);
        return createBufferView(offset, data.size_bytes(), target);
    }

    const std::vector<std::byte>& data() const { return buffer_data_; }
    size_t size() const { return buffer_data_.size(); }
    const std::vector<tinygltf::BufferView>& bufferViews() const { return buffer_views_; }

    tinygltf::Buffer toTinyGltf() const {
        tinygltf::Buffer buf;
        buf.data.resize(buffer_data_.size());
        std::memcpy(buf.data.data(), buffer_data_.data(), buffer_data_.size());
        return buf;
    }

    void clear() {
        buffer_data_.clear();
        buffer_views_.clear();
    }

private:
    std::vector<std::byte> buffer_data_;
    std::vector<tinygltf::BufferView> buffer_views_;
};

}
```

### 4.4 AccessorBuilder（Accessor 管理）

```cpp
#pragma once
#include <vector>
#include <span>
#include <optional>
#include <limits>
#include <algorithm>
#include "types.h"
#include "bounding_box.h"
#include "buffer_builder.h"

namespace gltf_writer {

class AccessorBuilder {
public:
    AccessorBuilder() = default;

    template<typename T>
    int addVertexAttribute(BufferBuilder& buffer,
                           std::span<const T> data,
                           AccessorType type,
                           ComponentType component_type,
                           bool compute_bounds = true) {
        int bv_idx = buffer.addBufferView(data, BufferViewTarget::ArrayBuffer);

        tinygltf::Accessor acc;
        acc.bufferView = bv_idx;
        acc.byteOffset = 0;
        acc.componentType = toTinyGltf(component_type);
        acc.count = static_cast<int>(data.size() / componentCount(type));
        acc.type = toTinyGltf(type);

        if (compute_bounds) {
            size_t stride = componentCount(type);
            if constexpr (std::is_same_v<T, float>) {
                if (type == AccessorType::Vec3) {
                    BoundingBox3F bbox(data, stride);
                    acc.minValues = bbox.minAsDouble();
                    acc.maxValues = bbox.maxAsDouble();
                } else if (type == AccessorType::Vec2) {
                    BoundingBox2F bbox(data, stride);
                    acc.minValues = bbox.minAsDouble();
                    acc.maxValues = bbox.maxAsDouble();
                }
            }
        }

        int idx = static_cast<int>(accessors_.size());
        accessors_.push_back(acc);
        return idx;
    }

    int addIndices(BufferBuilder& buffer, std::span<const uint32_t> indices) {
        uint32_t max_idx = 0;
        for (auto idx : indices) {
            max_idx = std::max(max_idx, idx);
        }

        ComponentType comp_type = pickIndexComponentType(max_idx);
        int bv_idx = -1;

        if (comp_type == ComponentType::UnsignedByte) {
            std::vector<uint8_t> indices8(indices.begin(), indices.end());
            bv_idx = buffer.addBufferView(std::span<const uint8_t>(indices8),
                                          BufferViewTarget::ElementArrayBuffer);
        } else if (comp_type == ComponentType::UnsignedShort) {
            std::vector<uint16_t> indices16(indices.begin(), indices.end());
            bv_idx = buffer.addBufferView(std::span<const uint16_t>(indices16),
                                          BufferViewTarget::ElementArrayBuffer);
        } else {
            bv_idx = buffer.addBufferView(indices, BufferViewTarget::ElementArrayBuffer);
        }

        tinygltf::Accessor acc;
        acc.bufferView = bv_idx;
        acc.byteOffset = 0;
        acc.componentType = toTinyGltf(comp_type);
        acc.count = static_cast<int>(indices.size());
        acc.type = toTinyGltf(AccessorType::Scalar);
        acc.minValues = {0.0};
        acc.maxValues = {static_cast<double>(max_idx)};

        int idx = static_cast<int>(accessors_.size());
        accessors_.push_back(acc);
        return idx;
    }

    int addIndices16(BufferBuilder& buffer, std::span<const uint16_t> indices) {
        uint16_t max_idx = 0;
        for (auto idx : indices) {
            max_idx = std::max(max_idx, idx);
        }

        int bv_idx = buffer.addBufferView(indices, BufferViewTarget::ElementArrayBuffer);

        tinygltf::Accessor acc;
        acc.bufferView = bv_idx;
        acc.byteOffset = 0;
        acc.componentType = toTinyGltf(ComponentType::UnsignedShort);
        acc.count = static_cast<int>(indices.size());
        acc.type = toTinyGltf(AccessorType::Scalar);
        acc.minValues = {0.0};
        acc.maxValues = {static_cast<double>(max_idx)};

        int idx = static_cast<int>(accessors_.size());
        accessors_.push_back(acc);
        return idx;
    }

    template<typename T>
    int addScalarAttribute(BufferBuilder& buffer,
                           std::span<const T> data,
                           ComponentType component_type) {
        int bv_idx = buffer.addBufferView(data, BufferViewTarget::ArrayBuffer);

        T max_val = std::numeric_limits<T>::lowest();
        T min_val = std::numeric_limits<T>::max();
        for (auto v : data) {
            max_val = std::max(max_val, v);
            min_val = std::min(min_val, v);
        }

        tinygltf::Accessor acc;
        acc.bufferView = bv_idx;
        acc.byteOffset = 0;
        acc.componentType = toTinyGltf(component_type);
        acc.count = static_cast<int>(data.size());
        acc.type = toTinyGltf(AccessorType::Scalar);
        acc.minValues = {static_cast<double>(min_val)};
        acc.maxValues = {static_cast<double>(max_val)};

        int idx = static_cast<int>(accessors_.size());
        accessors_.push_back(acc);
        return idx;
    }

    int addPlaceholder(AccessorType type, ComponentType component_type,
                       size_t count,
                       std::optional<std::vector<double>> min = std::nullopt,
                       std::optional<std::vector<double>> max = std::nullopt) {
        tinygltf::Accessor acc;
        acc.bufferView = -1;
        acc.byteOffset = 0;
        acc.componentType = toTinyGltf(component_type);
        acc.count = static_cast<int>(count);
        acc.type = toTinyGltf(type);
        if (min) acc.minValues = *min;
        if (max) acc.maxValues = *max;

        int idx = static_cast<int>(accessors_.size());
        accessors_.push_back(acc);
        return idx;
    }

    const std::vector<tinygltf::Accessor>& accessors() const { return accessors_; }
    std::vector<tinygltf::Accessor>& accessors() { return accessors_; }

    void clear() { accessors_.clear(); }

private:
    std::vector<tinygltf::Accessor> accessors_;

    static size_t componentCount(AccessorType type) {
        switch (type) {
            case AccessorType::Scalar: return 1;
            case AccessorType::Vec2: return 2;
            case AccessorType::Vec3: return 3;
            case AccessorType::Vec4: return 4;
            case AccessorType::Mat2: return 4;
            case AccessorType::Mat3: return 9;
            case AccessorType::Mat4: return 16;
            default: return 1;
        }
    }

    static ComponentType pickIndexComponentType(uint32_t max_index) {
        if (max_index <= std::numeric_limits<uint8_t>::max()) {
            return ComponentType::UnsignedByte;
        }
        if (max_index <= std::numeric_limits<uint16_t>::max()) {
            return ComponentType::UnsignedShort;
        }
        return ComponentType::UnsignedInt;
    }
};

}
```

### 4.5 MaterialBuilder（材质管理）

```cpp
#pragma once
#include <array>
#include <string>
#include <optional>
#include "types.h"

namespace gltf_writer {

struct PbrMetallicRoughness {
    std::array<double, 4> base_color_factor = {1.0, 1.0, 1.0, 1.0};
    double metallic_factor = 0.0;
    double roughness_factor = 1.0;
    int base_color_texture = -1;
    int metallic_roughness_texture = -1;

    static PbrMetallicRoughness Default() { return {}; }

    static PbrMetallicRoughness Color(double r, double g, double b, double a = 1.0) {
        PbrMetallicRoughness pbr;
        pbr.base_color_factor = {r, g, b, a};
        return pbr;
    }
};

struct MaterialParams {
    std::string name;
    PbrMetallicRoughness pbr;
    std::array<double, 3> emissive_factor = {0.0, 0.0, 0.0};
    int emissive_texture = -1;
    int normal_texture = -1;
    int occlusion_texture = -1;
    double alpha_cutoff = 0.5;
    std::string alpha_mode = "OPAQUE";
    bool double_sided = true;

    static MaterialParams Default() { return {}; }

    static MaterialParams Unlit(const std::array<double, 4>& color) {
        MaterialParams m;
        m.pbr.base_color_factor = color;
        return m;
    }
};

class MaterialBuilder {
public:
    MaterialBuilder() = default;

    int addMaterial(const MaterialParams& params) {
        tinygltf::Material mat;
        mat.name = params.name;
        mat.pbrMetallicRoughness.baseColorFactor = params.pbr.base_color_factor;
        mat.pbrMetallicRoughness.metallicFactor = params.pbr.metallic_factor;
        mat.pbrMetallicRoughness.roughnessFactor = params.pbr.roughness_factor;

        if (params.pbr.base_color_texture >= 0) {
            mat.pbrMetallicRoughness.baseColorTexture.index = params.pbr.base_color_texture;
        }
        if (params.pbr.metallic_roughness_texture >= 0) {
            mat.pbrMetallicRoughness.metallicRoughnessTexture.index = params.pbr.metallic_roughness_texture;
        }

        mat.emissiveFactor = params.emissive_factor;
        if (params.emissive_texture >= 0) {
            mat.emissiveTexture.index = params.emissive_texture;
        }
        if (params.normal_texture >= 0) {
            mat.normalTexture.index = params.normal_texture;
        }
        if (params.occlusion_texture >= 0) {
            mat.occlusionTexture.index = params.occlusion_texture;
        }

        mat.alphaCutoff = params.alpha_cutoff;
        mat.alphaMode = params.alpha_mode;
        mat.doubleSided = params.double_sided;

        int idx = static_cast<int>(materials_.size());
        materials_.push_back(mat);
        return idx;
    }

    int addDefaultMaterial() {
        return addMaterial(MaterialParams::Default());
    }

    int addColorMaterial(double r, double g, double b) {
        MaterialParams params;
        params.pbr = PbrMetallicRoughness::Color(r, g, b);
        params.pbr.roughness_factor = 0.7;
        params.pbr.metallic_factor = 0.3;
        return addMaterial(params);
    }

    tinygltf::Material& get(int index) { return materials_.at(index); }
    const tinygltf::Material& get(int index) const { return materials_.at(index); }

    const std::vector<tinygltf::Material>& materials() const { return materials_; }
    std::vector<tinygltf::Material>& materials() { return materials_; }

private:
    std::vector<tinygltf::Material> materials_;
};

}
```

### 4.6 TextureBuilder（纹理/图像/采样器管理）

```cpp
#pragma once
#include <vector>
#include <span>
#include <string>
#include "types.h"

namespace gltf_writer {

struct SamplerParams {
    int mag_filter = static_cast<int>(TextureFilter::Linear);
    int min_filter = static_cast<int>(TextureFilter::LinearMipmapLinear);
    int wrap_s = static_cast<int>(TextureWrap::Repeat);
    int wrap_t = static_cast<int>(TextureWrap::Repeat);

    static SamplerParams Default() { return {}; }

    static SamplerParams Clamp() {
        SamplerParams s;
        s.wrap_s = static_cast<int>(TextureWrap::ClampToEdge);
        s.wrap_t = static_cast<int>(TextureWrap::ClampToEdge);
        return s;
    }
};

struct ImageParams {
    std::string mime_type = "image/png";
    std::string name;
    std::string uri;
    int buffer_view = -1;
};

struct TextureParams {
    int sampler = -1;
    int source = -1;
    std::string name;
};

class TextureBuilder {
public:
    TextureBuilder() = default;

    int addSampler(const SamplerParams& params) {
        tinygltf::Sampler sampler;
        sampler.magFilter = params.mag_filter;
        sampler.minFilter = params.min_filter;
        sampler.wrapS = params.wrap_s;
        sampler.wrapT = params.wrap_t;

        int idx = static_cast<int>(samplers_.size());
        samplers_.push_back(sampler);
        return idx;
    }

    int addDefaultSampler() {
        return addSampler(SamplerParams::Default());
    }

    int addImage(const ImageParams& params) {
        tinygltf::Image img;
        img.mimeType = params.mime_type;
        img.name = params.name;
        img.uri = params.uri;
        img.bufferView = params.buffer_view;

        int idx = static_cast<int>(images_.size());
        images_.push_back(img);
        return idx;
    }

    int addEmbeddedImage(std::span<const std::byte> data,
                         int buffer_view,
                         std::string_view mime_type = "image/png") {
        ImageParams params;
        params.mime_type = std::string(mime_type);
        params.buffer_view = buffer_view;
        return addImage(params);
    }

    int addTexture(const TextureParams& params) {
        tinygltf::Texture tex;
        tex.sampler = params.sampler;
        tex.source = params.source;
        tex.name = params.name;

        int idx = static_cast<int>(textures_.size());
        textures_.push_back(tex);
        return idx;
    }

    int addSimpleTexture(int image_index, int sampler_index = -1) {
        TextureParams params;
        params.source = image_index;
        params.sampler = sampler_index;
        return addTexture(params);
    }

    const std::vector<tinygltf::Sampler>& samplers() const { return samplers_; }
    const std::vector<tinygltf::Image>& images() const { return images_; }
    const std::vector<tinygltf::Texture>& textures() const { return textures_; }

    std::vector<tinygltf::Sampler>& samplers() { return samplers_; }
    std::vector<tinygltf::Image>& images() { return images_; }
    std::vector<tinygltf::Texture>& textures() { return textures_; }

private:
    std::vector<tinygltf::Sampler> samplers_;
    std::vector<tinygltf::Image> images_;
    std::vector<tinygltf::Texture> textures_;
};

}
```

### 4.7 MeshBuilder（Mesh/Primitive 管理）

```cpp
#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include "types.h"

namespace gltf_writer {

struct PrimitiveParams {
    std::unordered_map<std::string, int> attributes;
    int indices = -1;
    int material = -1;
    PrimitiveMode mode = PrimitiveMode::Triangles;
};

class MeshBuilder {
public:
    MeshBuilder() = default;

    int addMesh(std::string_view name = "") {
        tinygltf::Mesh mesh;
        mesh.name = std::string(name);

        int idx = static_cast<int>(meshes_.size());
        meshes_.push_back(mesh);
        return idx;
    }

    int addPrimitive(int mesh_index, const PrimitiveParams& params) {
        tinygltf::Primitive prim;
        for (const auto& [name, accessor] : params.attributes) {
            prim.attributes[name] = accessor;
        }
        prim.indices = params.indices;
        prim.material = params.material;
        prim.mode = toTinyGltf(params.mode);

        meshes_.at(mesh_index).primitives.push_back(prim);
        return static_cast<int>(meshes_[mesh_index].primitives.size() - 1);
    }

    int addTriangleMesh(std::string_view name,
                        int position_accessor,
                        int normal_accessor = -1,
                        int texcoord_accessor = -1,
                        int indices_accessor = -1,
                        int material_index = -1) {
        int mesh_idx = addMesh(name);

        PrimitiveParams prim;
        prim.attributes["POSITION"] = position_accessor;
        if (normal_accessor >= 0) {
            prim.attributes["NORMAL"] = normal_accessor;
        }
        if (texcoord_accessor >= 0) {
            prim.attributes["TEXCOORD_0"] = texcoord_accessor;
        }
        prim.indices = indices_accessor;
        prim.material = material_index;

        addPrimitive(mesh_idx, prim);
        return mesh_idx;
    }

    tinygltf::Mesh& get(int index) { return meshes_.at(index); }
    const tinygltf::Mesh& get(int index) const { return meshes_.at(index); }

    const std::vector<tinygltf::Mesh>& meshes() const { return meshes_; }
    std::vector<tinygltf::Mesh>& meshes() { return meshes_; }

private:
    std::vector<tinygltf::Mesh> meshes_;
};

}
```

### 4.8 SceneBuilder（Node/Scene 管理）

```cpp
#pragma once
#include <vector>
#include <string>
#include <optional>
#include <span>

namespace gltf_writer {

struct NodeParams {
    std::string name;
    int mesh = -1;
    int skin = -1;
    std::array<double, 16> matrix = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::vector<int> children;
    std::optional<std::array<double, 3>> translation;
    std::optional<std::array<double, 4>> rotation;
    std::optional<std::array<double, 3>> scale;
};

class SceneBuilder {
public:
    SceneBuilder() = default;

    int addNode(const NodeParams& params) {
        tinygltf::Node node;
        node.name = params.name;
        node.mesh = params.mesh;
        node.skin = params.skin;
        node.matrix.assign(params.matrix.begin(), params.matrix.end());
        node.children = params.children;

        if (params.translation) {
            node.translation = {(*params.translation)[0], (*params.translation)[1], (*params.translation)[2]};
        }
        if (params.rotation) {
            node.rotation = {(*params.rotation)[0], (*params.rotation)[1], (*params.rotation)[2], (*params.rotation)[3]};
        }
        if (params.scale) {
            node.scale = {(*params.scale)[0], (*params.scale)[1], (*params.scale)[2]};
        }

        int idx = static_cast<int>(nodes_.size());
        nodes_.push_back(node);
        return idx;
    }

    int addMeshNode(int mesh_index, std::string_view name = "") {
        NodeParams params;
        params.name = std::string(name);
        params.mesh = mesh_index;
        return addNode(params);
    }

    int createScene(std::span<const int> node_indices) {
        tinygltf::Scene scene;
        for (int idx : node_indices) {
            scene.nodes.push_back(idx);
        }

        int scene_idx = static_cast<int>(scenes_.size());
        scenes_.push_back(scene);
        return scene_idx;
    }

    void setDefaultScene(int scene_index) {
        default_scene_ = scene_index;
    }

    int createDefaultScene(std::span<const int> node_indices) {
        int scene_idx = createScene(node_indices);
        setDefaultScene(scene_idx);
        return scene_idx;
    }

    const std::vector<tinygltf::Node>& nodes() const { return nodes_; }
    const std::vector<tinygltf::Scene>& scenes() const { return scenes_; }
    std::optional<int> defaultScene() const { return default_scene_; }

    std::vector<tinygltf::Node>& nodes() { return nodes_; }
    std::vector<tinygltf::Scene>& scenes() { return scenes_; }

private:
    std::vector<tinygltf::Node> nodes_;
    std::vector<tinygltf::Scene> scenes_;
    std::optional<int> default_scene_;
};

}
```

## 5. Extension 管理

### 5.1 ExtensionManager

```cpp
#pragma once
#include <set>
#include <string>
#include <vector>

namespace gltf_writer {

class ExtensionManager {
public:
    ExtensionManager() = default;

    void use(const std::string& name) { used_.insert(name); }
    void require(const std::string& name) { required_.insert(name); }

    void useAndRequire(const std::string& name) {
        use(name);
        require(name);
    }

    void apply(tinygltf::Model& model) const {
        for (const auto& ext : used_) {
            if (std::find(model.extensionsUsed.begin(),
                         model.extensionsUsed.end(), ext) == model.extensionsUsed.end()) {
                model.extensionsUsed.push_back(ext);
            }
        }
        for (const auto& ext : required_) {
            if (std::find(model.extensionsRequired.begin(),
                         model.extensionsRequired.end(), ext) == model.extensionsRequired.end()) {
                model.extensionsRequired.push_back(ext);
            }
        }
    }

    bool isUsed(const std::string& name) const { return used_.count(name) > 0; }
    bool isRequired(const std::string& name) const { return required_.count(name) > 0; }

    const std::set<std::string>& used() const { return used_; }
    const std::set<std::string>& required() const { return required_; }

    void clear() {
        used_.clear();
        required_.clear();
    }

private:
    std::set<std::string> used_;
    std::set<std::string> required_;
};

}
```

### 5.2 Extension 数据结构

#### KHR_texture_transform

```cpp
#pragma once
#include <array>
#include "extension_manager.h"

namespace gltf_writer::extensions {

struct TextureTransform {
    std::array<float, 2> offset = {0.0f, 0.0f};
    float rotation = 0.0f;
    std::array<float, 2> scale = {1.0f, 1.0f};
    int tex_coord = 0;

    static TextureTransform Identity() { return {}; }

    static TextureTransform WithOffset(float u, float v) {
        TextureTransform t;
        t.offset = {u, v};
        return t;
    }

    static TextureTransform WithScale(float u, float v) {
        TextureTransform t;
        t.scale = {u, v};
        return t;
    }

    static TextureTransform WithRotation(float radians) {
        TextureTransform t;
        t.rotation = radians;
        return t;
    }
};

inline void applyTextureTransform(tinygltf::Material& material,
                                  const std::string& texture_key,
                                  const TextureTransform& transform,
                                  ExtensionManager& ext_mgr) {
    tinygltf::Value::Object ext_obj;
    ext_obj["offset"] = tinygltf::Value(tinygltf::Value::Array{
        tinygltf::Value(static_cast<double>(transform.offset[0])),
        tinygltf::Value(static_cast<double>(transform.offset[1]))
    });
    ext_obj["rotation"] = tinygltf::Value(static_cast<double>(transform.rotation));
    ext_obj["scale"] = tinygltf::Value(tinygltf::Value::Array{
        tinygltf::Value(static_cast<double>(transform.scale[0])),
        tinygltf::Value(static_cast<double>(transform.scale[1]))
    });
    if (transform.tex_coord != 0) {
        ext_obj["texCoord"] = tinygltf::Value(transform.tex_coord);
    }

    tinygltf::Value::Object texture_info;
    texture_info["extensions"] = tinygltf::Value(tinygltf::Value::Object{
        {"KHR_texture_transform", tinygltf::Value(ext_obj)}
    });

    if (texture_key == "baseColorTexture") {
        material.pbrMetallicRoughness.baseColorTexture.extensions["KHR_texture_transform"] =
            tinygltf::Value(ext_obj);
    } else if (texture_key == "normalTexture") {
        material.normalTexture.extensions["KHR_texture_transform"] = tinygltf::Value(ext_obj);
    } else if (texture_key == "emissiveTexture") {
        material.emissiveTexture.extensions["KHR_texture_transform"] = tinygltf::Value(ext_obj);
    }

    ext_mgr.use("KHR_texture_transform");
}

}
```

#### KHR_materials_pbrSpecularGlossiness

```cpp
#pragma once
#include <array>
#include "extension_manager.h"

namespace gltf_writer::extensions {

struct SpecularGlossiness {
    std::array<double, 4> diffuse_factor = {1.0, 1.0, 1.0, 1.0};
    std::array<double, 3> specular_factor = {1.0, 1.0, 1.0};
    double glossiness_factor = 1.0;

    int diffuse_texture = -1;
    int specular_glossiness_texture = -1;

    static SpecularGlossiness Default() { return {}; }

    static SpecularGlossiness FromDiffuse(const std::array<double, 4>& color) {
        SpecularGlossiness sg;
        sg.diffuse_factor = color;
        return sg;
    }

    static SpecularGlossiness FromSpecular(const std::array<double, 3>& specular,
                                           double glossiness) {
        SpecularGlossiness sg;
        sg.specular_factor = specular;
        sg.glossiness_factor = glossiness;
        return sg;
    }
};

inline void applySpecularGlossiness(tinygltf::Material& material,
                                    const SpecularGlossiness& sg,
                                    ExtensionManager& ext_mgr) {
    tinygltf::Value::Object ext_obj;

    ext_obj["diffuseFactor"] = tinygltf::Value(tinygltf::Value::Array{
        tinygltf::Value(sg.diffuse_factor[0]),
        tinygltf::Value(sg.diffuse_factor[1]),
        tinygltf::Value(sg.diffuse_factor[2]),
        tinygltf::Value(sg.diffuse_factor[3])
    });

    ext_obj["specularFactor"] = tinygltf::Value(tinygltf::Value::Array{
        tinygltf::Value(sg.specular_factor[0]),
        tinygltf::Value(sg.specular_factor[1]),
        tinygltf::Value(sg.specular_factor[2])
    });

    ext_obj["glossinessFactor"] = tinygltf::Value(sg.glossiness_factor);

    if (sg.diffuse_texture >= 0) {
        tinygltf::Value::Object tex_info;
        tex_info["index"] = tinygltf::Value(sg.diffuse_texture);
        ext_obj["diffuseTexture"] = tinygltf::Value(tex_info);
    }

    if (sg.specular_glossiness_texture >= 0) {
        tinygltf::Value::Object tex_info;
        tex_info["index"] = tinygltf::Value(sg.specular_glossiness_texture);
        ext_obj["specularGlossinessTexture"] = tinygltf::Value(tex_info);
    }

    material.extensions["KHR_materials_pbrSpecularGlossiness"] = tinygltf::Value(ext_obj);
    ext_mgr.use("KHR_materials_pbrSpecularGlossiness");
}

}
```

#### KHR_materials_unlit（现有扩展封装）

```cpp
#pragma once
#include "extension_manager.h"

namespace gltf_writer::extensions {

inline void applyUnlit(tinygltf::Material& material, ExtensionManager& ext_mgr) {
    material.extensions["KHR_materials_unlit"] = tinygltf::Value(tinygltf::Value::Object());
    ext_mgr.use("KHR_materials_unlit");
}

}
```

#### KHR_draco_mesh_compression（现有扩展封装）

```cpp
#pragma once
#include <unordered_map>
#include "extension_manager.h"

namespace gltf_writer::extensions {

struct DracoCompression {
    int buffer_view = -1;
    std::unordered_map<std::string, int> attributes;

    static DracoCompression FromBufferView(int bv,
                                           const std::unordered_map<std::string, int>& attrs) {
        DracoCompression dc;
        dc.buffer_view = bv;
        dc.attributes = attrs;
        return dc;
    }
};

inline void applyDracoCompression(tinygltf::Primitive& primitive,
                                  const DracoCompression& draco,
                                  ExtensionManager& ext_mgr) {
    tinygltf::Value::Object ext_obj;
    ext_obj["bufferView"] = tinygltf::Value(draco.buffer_view);

    tinygltf::Value::Object attrs_obj;
    for (const auto& [name, id] : draco.attributes) {
        attrs_obj[name] = tinygltf::Value(id);
    }
    ext_obj["attributes"] = tinygltf::Value(attrs_obj);

    primitive.extensions["KHR_draco_mesh_compression"] = tinygltf::Value(ext_obj);
    ext_mgr.useAndRequire("KHR_draco_mesh_compression");
}

}
```

#### KHR_texture_basisu（现有扩展封装）

```cpp
#pragma once
#include "extension_manager.h"

namespace gltf_writer::extensions {

inline void applyBasisu(tinygltf::Texture& texture, int source_index, ExtensionManager& ext_mgr) {
    tinygltf::Value::Object ext_obj;
    ext_obj["source"] = tinygltf::Value(source_index);
    texture.extensions["KHR_texture_basisu"] = tinygltf::Value(ext_obj);
    texture.source = -1;
    ext_mgr.useAndRequire("KHR_texture_basisu");
}

}
```

## 6. GltfBuilder 设计

### 6.1 配置结构

```cpp
#pragma once
#include <string>
#include <optional>
#include "mesh_processor.h"

namespace gltf_writer {

struct GltfConfig {
    bool enable_draco = false;
    bool enable_unlit = false;
    bool enable_texture_compress = false;
    std::optional<DracoCompressionParams> draco_params;

    static GltfConfig Default() { return {}; }

    static GltfConfig WithDraco(const DracoCompressionParams& params = {}) {
        GltfConfig c;
        c.enable_draco = true;
        c.draco_params = params;
        return c;
    }

    static GltfConfig WithUnlit() {
        GltfConfig c;
        c.enable_unlit = true;
        return c;
    }

    static GltfConfig WithTextureCompress() {
        GltfConfig c;
        c.enable_texture_compress = true;
        return c;
    }
};

}
```

### 6.2 GltfBuilder 类

```cpp
#pragma once
#include <string>
#include <span>
#include "types.h"
#include "bounding_box.h"
#include "buffer_builder.h"
#include "accessor_builder.h"
#include "material_builder.h"
#include "texture_builder.h"
#include "mesh_builder.h"
#include "scene_builder.h"
#include "extension_manager.h"
#include "gltf_config.h"

namespace gltf_writer {

class GltfBuilder {
public:
    explicit GltfBuilder(const GltfConfig& config = GltfConfig::Default())
        : config_(config) {
        model_.asset.version = "2.0";
        model_.asset.generator = "gltf_writer";
    }

    GltfBuilder(const GltfBuilder&) = delete;
    GltfBuilder& operator=(const GltfBuilder&) = delete;
    GltfBuilder(GltfBuilder&&) noexcept = default;
    GltfBuilder& operator=(GltfBuilder&&) noexcept = default;

    BufferBuilder& buffer() { return buffer_; }
    AccessorBuilder& accessor() { return accessor_; }
    MaterialBuilder& material() { return material_; }
    TextureBuilder& texture() { return texture_; }
    MeshBuilder& mesh() { return mesh_; }
    SceneBuilder& scene() { return scene_; }
    ExtensionManager& extensions() { return extensions_; }

    const BufferBuilder& buffer() const { return buffer_; }
    const AccessorBuilder& accessor() const { return accessor_; }
    const MaterialBuilder& material() const { return material_; }
    const TextureBuilder& texture() const { return texture_; }
    const MeshBuilder& mesh() const { return mesh_; }
    const SceneBuilder& scene() const { return scene_; }
    const ExtensionManager& extensions() const { return extensions_; }

    std::string toGLB() const {
        tinygltf::Model model = finalizeModel();

        tinygltf::TinyGLTF gltf;
        std::ostringstream ss;
        gltf.WriteGltfSceneToStream(&model, ss, false, true);
        return ss.str();
    }

    tinygltf::Model toModel() const {
        return finalizeModel();
    }

    tinygltf::Model& model() { return model_; }
    const tinygltf::Model& model() const { return model_; }

    const GltfConfig& config() const { return config_; }

private:
    tinygltf::Model model_;
    BufferBuilder buffer_;
    AccessorBuilder accessor_;
    MaterialBuilder material_;
    TextureBuilder texture_;
    MeshBuilder mesh_;
    SceneBuilder scene_;
    ExtensionManager extensions_;
    GltfConfig config_;

    tinygltf::Model finalizeModel() const {
        tinygltf::Model model = model_;

        model.buffers.push_back(buffer_.toTinyGltf());

        const auto& bvs = buffer_.bufferViews();
        model.bufferViews.insert(model.bufferViews.end(), bvs.begin(), bvs.end());

        const auto& accs = accessor_.accessors();
        model.accessors.insert(model.accessors.end(), accs.begin(), accs.end());

        const auto& mats = material_.materials();
        model.materials.insert(model.materials.end(), mats.begin(), mats.end());

        const auto& samplers = texture_.samplers();
        model.samplers.insert(model.samplers.end(), samplers.begin(), samplers.end());

        const auto& images = texture_.images();
        model.images.insert(model.images.end(), images.begin(), images.end());

        const auto& textures = texture_.textures();
        model.textures.insert(model.textures.end(), textures.begin(), textures.end());

        const auto& meshes = mesh_.meshes();
        model.meshes.insert(model.meshes.end(), meshes.begin(), meshes.end());

        const auto& nodes = scene_.nodes();
        model.nodes.insert(model.nodes.end(), nodes.begin(), nodes.end());

        const auto& scenes = scene_.scenes();
        model.scenes.insert(model.scenes.end(), scenes.begin(), scenes.end());

        if (scene_.defaultScene()) {
            model.defaultScene = *scene_.defaultScene();
        }

        extensions_.apply(model);

        return model;
    }
};

}
```

## 7. 实现优先级

### 7.1 优先级原则

1. **最小影响范围**：优先实现新增功能，不修改现有逻辑
2. **可独立测试**：每个功能模块可独立验证
3. **渐进式迁移**：先在 FBX Pipeline 试点，验证后再推广

### 7.2 Phase 1: FBX 新增 Extension 支持（P0）

**目标**：为 FBX Pipeline 添加 `KHR_texture_transform` 和 `KHR_materials_pbrSpecularGlossiness` 支持

**影响范围**：仅影响 FBX Pipeline，不影响其他 Pipeline

| 任务 | 风险 |
|------|------|
| 实现 `ExtensionManager` | 低 |
| 实现 `KHR_texture_transform` 数据结构 | 低 |
| 实现 `KHR_materials_pbrSpecularGlossiness` 数据结构 | 低 |
| 在 `FBXPipeline` 中集成新 Extension | 中 |
| 单元测试 | 低 |

**验证方式**：
- 使用 FBX 测试文件生成 3D Tiles
- 使用 glTF Validator 验证输出合规性
- 使用 CesiumJS 验证渲染效果

### 7.3 Phase 2: 底层抽象层（P1）

**目标**：实现 `BufferBuilder`、`AccessorBuilder`、`BoundingBox` 等底层抽象

**影响范围**：新增模块，不修改现有代码

| 任务 | 风险 |
|------|------|
| 实现 `types.h` 类型定义 | 低 |
| 实现 `BoundingBox` 模板类 | 低 |
| 实现 `BufferBuilder` | 低 |
| 实现 `AccessorBuilder` | 低 |
| 实现 `MaterialBuilder` | 低 |
| 实现 `TextureBuilder` | 低 |
| 实现 `MeshBuilder` | 低 |
| 实现 `SceneBuilder` | 低 |
| 单元测试 | 低 |

**验证方式**：
- 单元测试验证边界计算正确性
- 对比新旧实现输出一致性

### 7.4 Phase 3: GltfBuilder 集成（P2）

**目标**：实现 `GltfBuilder`，整合底层抽象和 Extension 管理

**影响范围**：新增模块，在 FBX Pipeline 中试点使用

| 任务 | 风险 |
|------|------|
| 实现 `GltfBuilder` 基础功能 | 中 |
| 迁移 FBX Pipeline 使用 `GltfBuilder` | 中 |
| 回归测试 | 中 |

**验证方式**：
- 对比迁移前后输出文件二进制一致性
- 完整回归测试所有 FBX 测试用例

### 7.5 Phase 4: 其他 Pipeline 迁移（P3）

**目标**：将 `osgb23dtile.cpp` 和 `shp23dtile.cpp` 迁移到新架构

**影响范围**：修改现有 Pipeline 代码

| 任务 | 风险 |
|------|------|
| 迁移 `osgb23dtile.cpp` | 中 |
| 迁移 `shp23dtile.cpp` | 中 |
| 回归测试 | 中 |

## 8. 文件组织

```
src/
├── gltf_writer/
│   ├── types.h                    // 枚举、基础类型定义
│   ├── bounding_box.h             // 包围盒模板
│   ├── buffer_builder.h           // Buffer/BufferView 管理
│   ├── accessor_builder.h         // Accessor 管理
│   ├── material_builder.h         // Material 管理
│   ├── texture_builder.h          // Texture/Image/Sampler 管理
│   ├── mesh_builder.h             // Mesh/Primitive 管理
│   ├── scene_builder.h            // Node/Scene 管理
│   ├── extension_manager.h        // Extension 注册管理
│   ├── gltf_config.h              // 配置结构
│   ├── gltf_builder.h             // GltfBuilder 主类
│   └── extensions/
│       ├── texture_transform.h    // KHR_texture_transform
│       ├── specular_glossiness.h  // KHR_materials_pbrSpecularGlossiness
│       ├── unlit.h                // KHR_materials_unlit
│       ├── draco.h                // KHR_draco_mesh_compression
│       └── basisu.h               // KHR_texture_basisu
├── coordinate_system.h            // 现有：坐标系定义
├── coordinate_transformer.h       // 现有：坐标转换
├── mesh_processor.h               // 现有：网格处理
└── ...
```

## 9. 使用示例

### 9.1 基础使用

```cpp
#include "gltf_writer/gltf_builder.h"
#include "gltf_writer/extensions/texture_transform.h"
#include "gltf_writer/extensions/specular_glossiness.h"

void buildGltfMesh() {
    gltf_writer::GltfConfig config = gltf_writer::GltfConfig::Default();
    gltf_writer::GltfBuilder builder(config);

    auto& buffer = builder.buffer();
    auto& accessor = builder.accessor();
    auto& material = builder.material();
    auto& mesh = builder.mesh();
    auto& scene = builder.scene();

    std::vector<float> positions = { /* ... */ };
    std::vector<float> normals = { /* ... */ };
    std::vector<uint32_t> indices = { /* ... */ };

    int pos_acc = accessor.addVertexAttribute(buffer,
        std::span<const float>(positions),
        gltf_writer::AccessorType::Vec3,
        gltf_writer::ComponentType::Float);

    int norm_acc = accessor.addVertexAttribute(buffer,
        std::span<const float>(normals),
        gltf_writer::AccessorType::Vec3,
        gltf_writer::ComponentType::Float);

    int idx_acc = accessor.addIndices(buffer,
        std::span<const uint32_t>(indices));

    int mat_idx = material.addDefaultMaterial();

    int mesh_idx = mesh.addTriangleMesh("mesh", pos_acc, norm_acc, -1, idx_acc, mat_idx);

    int node_idx = scene.addMeshNode(mesh_idx, "node");
    scene.createDefaultScene({node_idx});

    std::string glb = builder.toGLB();
}
```

### 9.2 FBX Pipeline 使用新 Extension

```cpp
#include "gltf_writer/gltf_builder.h"
#include "gltf_writer/extensions/texture_transform.h"
#include "gltf_writer/extensions/specular_glossiness.h"

void FBXPipeline::processMaterial(const FBXMaterial& fbx_mat) {
    gltf_writer::MaterialParams params;
    params.name = fbx_mat.name;
    params.pbr.base_color_factor = {fbx_mat.diffuse.r, fbx_mat.diffuse.g,
                                    fbx_mat.diffuse.b, fbx_mat.diffuse.a};

    int mat_idx = builder_.material().addMaterial(params);
    auto& mat = builder_.material().get(mat_idx);

    if (fbx_mat.use_specular_glossiness) {
        gltf_writer::extensions::SpecularGlossiness sg;
        sg.diffuse_factor = {fbx_mat.diffuse.r, fbx_mat.diffuse.g,
                            fbx_mat.diffuse.b, fbx_mat.diffuse.a};
        sg.specular_factor = {fbx_mat.specular.r, fbx_mat.specular.g,
                             fbx_mat.specular.b};
        sg.glossiness_factor = fbx_mat.glossiness;

        gltf_writer::extensions::applySpecularGlossiness(
            mat, sg, builder_.extensions());
    }

    if (fbx_mat.has_texture_transform) {
        gltf_writer::extensions::TextureTransform transform;
        transform.offset = {fbx_mat.texture_offset_u, fbx_mat.texture_offset_v};
        transform.scale = {fbx_mat.texture_scale_u, fbx_mat.texture_scale_v};
        transform.rotation = fbx_mat.texture_rotation;

        gltf_writer::extensions::applyTextureTransform(
            mat, "baseColorTexture", transform, builder_.extensions());
    }

    if (config_.enable_unlit) {
        gltf_writer::extensions::applyUnlit(mat, builder_.extensions());
    }
}
```

### 9.3 Draco 压缩集成

```cpp
void buildDracoMesh() {
    gltf_writer::GltfConfig config = gltf_writer::GltfConfig::WithDraco();
    gltf_writer::GltfBuilder builder(config);

    auto& buffer = builder.buffer();
    auto& accessor = builder.accessor();

    std::vector<float> positions = { /* ... */ };
    std::vector<uint32_t> indices = { /* ... */ };

    BoundingBox3F bbox(positions, 3);

    int pos_acc = accessor.addPlaceholder(
        gltf_writer::AccessorType::Vec3,
        gltf_writer::ComponentType::Float,
        positions.size() / 3,
        bbox.minAsDouble(),
        bbox.maxAsDouble());

    int idx_acc = accessor.addPlaceholder(
        gltf_writer::AccessorType::Scalar,
        gltf_writer::ComponentType::UnsignedInt,
        indices.size());

    std::vector<unsigned char> draco_data;
    int draco_pos_att, draco_idx_att;
    compress_mesh_geometry(..., draco_data, ..., &draco_pos_att, ..., &draco_idx_att);

    int draco_bv = buffer.addBufferView(
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(draco_data.data()),
            draco_data.size()),
        gltf_writer::BufferViewTarget::None);

    auto& prim = builder.mesh().get(mesh_idx).primitives[0];
    gltf_writer::extensions::DracoCompression draco;
    draco.buffer_view = draco_bv;
    draco.attributes = {{"POSITION", draco_pos_att}, {"indices", draco_idx_att}};

    gltf_writer::extensions::applyDracoCompression(prim, draco, builder.extensions());
}
```

## 10. 参考

- glTF 2.0 Specification: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html
- KHR_texture_transform: https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_texture_transform
- KHR_materials_pbrSpecularGlossiness: https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_pbrSpecularGlossiness
- KHR_draco_mesh_compression: https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_draco_mesh_compression
- KHR_materials_unlit: https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_unlit
- KHR_texture_basisu: https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_texture_basisu

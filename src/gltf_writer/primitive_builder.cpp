#include "primitive_builder.h"
#include <algorithm>
#include <cstring>

namespace gltf_writer {

PrimitiveBuilder::PrimitiveBuilder()
    : materialIndex_(-1)
    , mode_(PrimitiveMode::Triangles)
    , vertexCount_(0) {
}

void PrimitiveBuilder::addVertices(const std::vector<float>& positions) {
    positions_.insert(positions_.end(), positions.begin(), positions.end());
    // 更新顶点数量（每个顶点3个float）
    vertexCount_ = positions_.size() / 3;
}

void PrimitiveBuilder::addNormals(const std::vector<float>& normals) {
    normals_.insert(normals_.end(), normals.begin(), normals.end());
}

void PrimitiveBuilder::addTexcoords(const std::vector<float>& texcoords) {
    texcoords_.insert(texcoords_.end(), texcoords.begin(), texcoords.end());
}

void PrimitiveBuilder::addIndices(const std::vector<uint32_t>& indices) {
    indices_.insert(indices_.end(), indices.begin(), indices.end());
}

void PrimitiveBuilder::setMaterial(int materialIndex) {
    materialIndex_ = materialIndex;
}

void PrimitiveBuilder::setMode(PrimitiveMode mode) {
    mode_ = mode;
}

void PrimitiveBuilder::clear() {
    positions_.clear();
    normals_.clear();
    texcoords_.clear();
    indices_.clear();
    materialIndex_ = -1;
    mode_ = PrimitiveMode::Triangles;
    vertexCount_ = 0;
}

tinygltf::Primitive PrimitiveBuilder::build(tinygltf::Model& model, tinygltf::Buffer& buffer) {
    tinygltf::Primitive primitive;
    primitive.mode = toTinyGltf(mode_);

    if (materialIndex_ >= 0) {
        primitive.material = materialIndex_;
    }

    // 计算数据大小
    size_t positionsSize = positions_.size() * sizeof(float);
    size_t normalsSize = normals_.size() * sizeof(float);
    size_t texcoordsSize = texcoords_.size() * sizeof(float);
    size_t indicesSize = indices_.size() * sizeof(uint32_t);

    // 对齐当前缓冲区
    alignBuffer(buffer.data);
    size_t bufferStart = buffer.data.size();

    // 写入位置数据
    size_t positionsOffset = buffer.data.size();
    buffer.data.resize(positionsOffset + positionsSize);
    memcpy(buffer.data.data() + positionsOffset, positions_.data(), positionsSize);

    // 写入法线数据
    size_t normalsOffset = buffer.data.size();
    if (normalsSize > 0) {
        buffer.data.resize(normalsOffset + normalsSize);
        memcpy(buffer.data.data() + normalsOffset, normals_.data(), normalsSize);
    }

    // 写入纹理坐标数据
    size_t texcoordsOffset = buffer.data.size();
    if (texcoordsSize > 0) {
        buffer.data.resize(texcoordsOffset + texcoordsSize);
        memcpy(buffer.data.data() + texcoordsOffset, texcoords_.data(), texcoordsSize);
    }

    // 写入索引数据
    size_t indicesOffset = buffer.data.size();
    if (indicesSize > 0) {
        buffer.data.resize(indicesOffset + indicesSize);
        memcpy(buffer.data.data() + indicesOffset, indices_.data(), indicesSize);
    }

    // 创建BufferViews和Accessors
    // 位置
    int posBvIndex = createBufferView(model, positionsOffset, positionsSize, BufferViewTarget::ArrayBuffer);

    // 计算min/max
    std::vector<double> minPos(3, std::numeric_limits<double>::max());
    std::vector<double> maxPos(3, std::numeric_limits<double>::lowest());
    for (size_t i = 0; i < positions_.size(); i += 3) {
        for (int j = 0; j < 3; ++j) {
            minPos[j] = std::min(minPos[j], static_cast<double>(positions_[i + j]));
            maxPos[j] = std::max(maxPos[j], static_cast<double>(positions_[i + j]));
        }
    }

    int posAccIndex = createAccessor(
        model, posBvIndex, 0, ComponentType::Float, vertexCount_, AccessorType::Vec3, minPos, maxPos);
    primitive.attributes["POSITION"] = posAccIndex;

    // 法线
    if (normalsSize > 0) {
        int normBvIndex = createBufferView(model, normalsOffset, normalsSize, BufferViewTarget::ArrayBuffer);
        int normAccIndex = createAccessor(
            model, normBvIndex, 0, ComponentType::Float, vertexCount_, AccessorType::Vec3);
        primitive.attributes["NORMAL"] = normAccIndex;
    }

    // 纹理坐标
    if (texcoordsSize > 0) {
        int texBvIndex = createBufferView(model, texcoordsOffset, texcoordsSize, BufferViewTarget::ArrayBuffer);
        int texAccIndex = createAccessor(
            model, texBvIndex, 0, ComponentType::Float, vertexCount_, AccessorType::Vec2);
        primitive.attributes["TEXCOORD_0"] = texAccIndex;
    }

    // 索引
    if (indicesSize > 0) {
        int idxBvIndex = createBufferView(model, indicesOffset, indicesSize, BufferViewTarget::ElementArrayBuffer);
        int idxAccIndex = createAccessor(
            model, idxBvIndex, 0, ComponentType::UnsignedInt, indices_.size(), AccessorType::Scalar);
        primitive.indices = idxAccIndex;
    }

    return primitive;
}

void PrimitiveBuilder::alignBuffer(std::vector<unsigned char>& buffer) {
    while (buffer.size() % 4 != 0) {
        buffer.push_back(0);
    }
}

int PrimitiveBuilder::createBufferView(
    tinygltf::Model& model,
    size_t byteOffset,
    size_t byteLength,
    BufferViewTarget target) {

    tinygltf::BufferView bv;
    bv.buffer = 0;
    bv.byteOffset = static_cast<int>(byteOffset);
    bv.byteLength = static_cast<int>(byteLength);
    bv.target = toTinyGltf(target);

    int index = static_cast<int>(model.bufferViews.size());
    model.bufferViews.push_back(bv);
    return index;
}

int PrimitiveBuilder::createAccessor(
    tinygltf::Model& model,
    int bufferViewIndex,
    size_t byteOffset,
    ComponentType componentType,
    size_t count,
    AccessorType type,
    const std::vector<double>& minValues,
    const std::vector<double>& maxValues) {

    tinygltf::Accessor acc;
    acc.bufferView = bufferViewIndex;
    acc.byteOffset = static_cast<int>(byteOffset);
    acc.componentType = toTinyGltf(componentType);
    acc.count = static_cast<int>(count);
    switch (type) {
        case AccessorType::Scalar: acc.type = TINYGLTF_TYPE_SCALAR; break;
        case AccessorType::Vec2: acc.type = TINYGLTF_TYPE_VEC2; break;
        case AccessorType::Vec3: acc.type = TINYGLTF_TYPE_VEC3; break;
        case AccessorType::Vec4: acc.type = TINYGLTF_TYPE_VEC4; break;
        case AccessorType::Mat2: acc.type = TINYGLTF_TYPE_MAT2; break;
        case AccessorType::Mat3: acc.type = TINYGLTF_TYPE_MAT3; break;
        case AccessorType::Mat4: acc.type = TINYGLTF_TYPE_MAT4; break;
        default: acc.type = TINYGLTF_TYPE_SCALAR; break;
    }

    if (!minValues.empty()) {
        acc.minValues = minValues;
    }
    if (!maxValues.empty()) {
        acc.maxValues = maxValues;
    }

    int index = static_cast<int>(model.accessors.size());
    model.accessors.push_back(acc);
    return index;
}

} // namespace gltf_writer

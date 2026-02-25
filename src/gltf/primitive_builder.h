#pragma once

/**
 * @file gltf/primitive_builder.h
 * @brief GLTF Primitive构建器
 *
 * 封装Primitive构建逻辑，简化GLTF网格创建
 */

#include "types.h"
#include <tiny_gltf.h>
#include <vector>
#include <cstdint>

namespace gltf {

/**
 * @brief Primitive构建器
 *
 * 简化GLTF Primitive的创建过程
 */
class PrimitiveBuilder {
public:
    PrimitiveBuilder();

    /**
     * @brief 添加顶点位置
     * @param positions 顶点位置数组 [x,y,z, x,y,z, ...]
     */
    void addVertices(const std::vector<float>& positions);

    /**
     * @brief 添加法线
     * @param normals 法线数组 [x,y,z, x,y,z, ...]
     */
    void addNormals(const std::vector<float>& normals);

    /**
     * @brief 添加纹理坐标
     * @param texcoords 纹理坐标数组 [u,v, u,v, ...]
     */
    void addTexcoords(const std::vector<float>& texcoords);

    /**
     * @brief 添加索引
     * @param indices 索引数组
     */
    void addIndices(const std::vector<uint32_t>& indices);

    /**
     * @brief 设置材质索引
     * @param materialIndex 材质索引
     */
    void setMaterial(int materialIndex);

    /**
     * @brief 设置图元模式
     * @param mode 图元模式（默认TRIANGLES）
     */
    void setMode(PrimitiveMode mode);

    /**
     * @brief 构建Primitive
     *
     * 将数据写入模型和缓冲区，返回Primitive
     *
     * @param model GLTF模型
     * @param buffer GLTF缓冲区
     * @return 构建的Primitive
     */
    tinygltf::Primitive build(tinygltf::Model& model, tinygltf::Buffer& buffer);

    /**
     * @brief 获取顶点数量
     * @return 顶点数量
     */
    size_t getVertexCount() const { return vertexCount_; }

    /**
     * @brief 获取索引数量
     * @return 索引数量
     */
    size_t getIndexCount() const { return indices_.size(); }

    /**
     * @brief 清空数据
     */
    void clear();

private:
    // 输入数据
    std::vector<float> positions_;
    std::vector<float> normals_;
    std::vector<float> texcoords_;
    std::vector<uint32_t> indices_;

    // 配置
    int materialIndex_ = -1;
    PrimitiveMode mode_ = PrimitiveMode::Triangles;
    size_t vertexCount_ = 0;

    // 辅助函数：对齐缓冲区到4字节
    void alignBuffer(std::vector<unsigned char>& buffer);

    // 创建BufferView
    int createBufferView(
        tinygltf::Model& model,
        size_t byteOffset,
        size_t byteLength,
        BufferViewTarget target
    );

    // 创建Accessor
    int createAccessor(
        tinygltf::Model& model,
        int bufferViewIndex,
        size_t byteOffset,
        ComponentType componentType,
        size_t count,
        AccessorType type,
        const std::vector<double>& minValues = {},
        const std::vector<double>& maxValues = {}
    );
};

} // namespace gltf

#pragma once

/**
 * @file gltf/utils/texture_utils.h
 * @brief GLTF纹理工具类
 *
 * 从appendGeometryToModel提取的纹理处理逻辑
 * 包括：纹理加载、KTX2压缩、添加到GLTF模型
 */

#include <osg/Texture>
#include <osg/Image>
#include <tiny_gltf.h>
#include <vector>
#include <string>

namespace gltf {
namespace utils {

/**
 * @brief 纹理处理结果
 */
struct TextureResult {
    std::vector<unsigned char> data;  // 图像数据
    std::string mimeType;             // MIME类型
    bool hasAlpha = false;            // 是否包含透明通道
    bool success = false;             // 是否成功
};

/**
 * @brief 纹理工具类
 *
 * 处理OSG纹理到GLTF纹理的转换
 * 统一处理基础纹理、法线纹理、自发光纹理
 */
class TextureUtils {
public:
    /**
     * @brief 处理纹理
     *
     * 统一的纹理处理入口，替代appendGeometryToModel中的3处重复代码
     * 处理流程：
     * 1. 检查透明通道
     * 2. 尝试KTX2压缩（如果启用）
     * 3. 从文件加载
     * 4. 从内存编码
     *
     * @param texture OSG纹理对象
     * @param enableKTX2 是否启用KTX2压缩
     * @return 处理结果
     */
    static TextureResult processTexture(
        const osg::Texture* texture,
        bool enableKTX2 = false
    );

    /**
     * @brief 将图像数据添加到GLTF模型
     *
     * @param model GLTF模型
     * @param buffer GLTF缓冲区
     * @param imageData 图像数据
     * @param mimeType MIME类型
     * @param useBasisu 是否使用Basisu扩展（KTX2）
     * @return 纹理索引
     */
    static int addImageToModel(
        tinygltf::Model& model,
        tinygltf::Buffer& buffer,
        const std::vector<unsigned char>& imageData,
        const std::string& mimeType,
        bool useBasisu
    );

    /**
     * @brief 检查图像是否有透明通道
     *
     * @param image OSG图像
     * @return 是否包含透明像素
     */
    static bool hasAlphaTransparency(const osg::Image* image);

private:
    // 尝试KTX2压缩
    static bool tryKTX2Compression(
        const osg::Texture* texture,
        std::vector<unsigned char>& outData,
        std::string& outMimeType
    );

    // 从文件加载图像
    static bool loadFromFile(
        const std::string& filePath,
        std::vector<unsigned char>& outData,
        std::string& outMimeType
    );

    // 从内存编码图像
    static bool encodeFromMemory(
        const osg::Image* image,
        std::vector<unsigned char>& outData,
        std::string& outMimeType
    );

    // 获取文件扩展名对应的MIME类型
    static std::string getMimeTypeFromExtension(const std::string& ext);
};

} // namespace utils
} // namespace gltf

#include "texture_utils.h"
#include "../../utils/log.h"
#include "../../common/mesh_processor.h"

#include <osgDB/Registry>
#include <osgDB/ReaderWriter>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cstring>

namespace osg {
namespace utils {

TextureResult TextureUtils::processTexture(
    const osg::Texture* texture,
    bool enableKTX2) {

    TextureResult result;

    if (!texture || texture->getNumImages() == 0) {
        return result;
    }

    const osg::Image* image = texture->getImage(0);
    if (!image) {
        return result;
    }

    // 检查透明通道
    result.hasAlpha = hasAlphaTransparency(image);

    std::string imgPath = image->getFileName();

    // 1. 尝试KTX2压缩
    if (enableKTX2) {
        if (tryKTX2Compression(texture, result.data, result.mimeType)) {
            result.success = true;
            return result;
        }
    }

    // 2. 从文件加载
    if (!imgPath.empty() && std::filesystem::exists(imgPath)) {
        if (loadFromFile(imgPath, result.data, result.mimeType)) {
            result.success = true;
            return result;
        }
    }

    // 3. 从内存编码
    if (image->data() != nullptr) {
        if (encodeFromMemory(image, result.data, result.mimeType)) {
            result.success = true;
            return result;
        }
    }

    return result;
}

int TextureUtils::addImageToModel(
    tinygltf::Model& model,
    tinygltf::Buffer& buffer,
    const std::vector<unsigned char>& imageData,
    const std::string& mimeType,
    bool useBasisu) {

    // 确保4字节对齐
    size_t currentSize = buffer.data.size();
    size_t padding = (4 - (currentSize % 4)) % 4;
    if (padding > 0) {
        buffer.data.resize(currentSize + padding, 0);
    }

    size_t imgOffset = buffer.data.size();
    size_t imgLen = imageData.size();
    buffer.data.resize(imgOffset + imgLen);
    memcpy(buffer.data.data() + imgOffset, imageData.data(), imgLen);

    // 创建BufferView
    tinygltf::BufferView bvImg;
    bvImg.buffer = 0;
    bvImg.byteOffset = static_cast<int>(imgOffset);
    bvImg.byteLength = static_cast<int>(imgLen);
    int bvImgIdx = static_cast<int>(model.bufferViews.size());
    model.bufferViews.push_back(bvImg);

    // 创建Image
    tinygltf::Image gltfImg;
    gltfImg.mimeType = mimeType;
    gltfImg.bufferView = bvImgIdx;
    int imgIdx = static_cast<int>(model.images.size());
    model.images.push_back(gltfImg);

    // 创建Texture
    tinygltf::Texture gltfTex;
    if (useBasisu) {
        tinygltf::Value::Object ktxExt;
        ktxExt["source"] = tinygltf::Value(imgIdx);
        gltfTex.extensions["KHR_texture_basisu"] = tinygltf::Value(ktxExt);
    } else {
        gltfTex.source = imgIdx;
    }
    int texIdx = static_cast<int>(model.textures.size());
    model.textures.push_back(gltfTex);

    // 确保结束对齐
    size_t endSize = buffer.data.size();
    size_t endPadding = (4 - (endSize % 4)) % 4;
    if (endPadding > 0) {
        buffer.data.resize(endSize + endPadding, 0);
    }

    return texIdx;
}

bool TextureUtils::hasAlphaTransparency(const osg::Image* image) {
    if (!image || !image->data()) {
        return false;
    }

    GLenum pf = image->getPixelFormat();
    GLenum dt = image->getDataType();
    int w = image->s();
    int h = image->t();

    // 确定通道数
    int channels = 0;
    if (pf == GL_LUMINANCE) channels = 1;
    else if (pf == GL_LUMINANCE_ALPHA) channels = 2;
    else if (pf == GL_RGB) channels = 3;
    else if (pf == GL_RGBA) channels = 4;

    // 检查透明像素
    if ((channels == 2 || channels == 4) &&
        dt == GL_UNSIGNED_BYTE &&
        w > 0 && h > 0) {

        const unsigned char* p = image->data();
        int alphaIndex = (channels == 2) ? 1 : 3;
        int total = w * h;

        for (int i = 0; i < total; ++i) {
            if (p[i * channels + alphaIndex] < 255) {
                return true;
            }
        }
    }

    return false;
}

bool TextureUtils::tryKTX2Compression(
    const osg::Texture* texture,
    std::vector<unsigned char>& outData,
    std::string& outMimeType) {

    std::vector<unsigned char> compressedData;
    std::string compressedMime;

    // 调用全局的process_texture函数
    if (process_texture(const_cast<osg::Texture*>(texture),
                        compressedData, compressedMime, true)) {
        if (compressedMime == "image/ktx2") {
            outData = compressedData;
            outMimeType = compressedMime;
            return true;
        }
    }

    return false;
}

bool TextureUtils::loadFromFile(
    const std::string& filePath,
    std::vector<unsigned char>& outData,
    std::string& outMimeType) {

    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }

    size_t size = file.tellg();
    outData.resize(size);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(outData.data()), size);

    // 根据扩展名确定MIME类型
    std::string ext = std::filesystem::path(filePath).extension().string();
    outMimeType = getMimeTypeFromExtension(ext);

    return true;
}

bool TextureUtils::encodeFromMemory(
    const osg::Image* image,
    std::vector<unsigned char>& outData,
    std::string& outMimeType) {

    std::string imgPath = image->getFileName();
    std::string ext = "png";

    // 尝试从文件名获取扩展名
    if (!imgPath.empty()) {
        std::string e = std::filesystem::path(imgPath).extension().string();
        if (!e.empty() && e.size() > 1) {
            e = e.substr(1);
            std::transform(e.begin(), e.end(), e.begin(), ::tolower);
            if (e == "jpg" || e == "jpeg") {
                ext = e;
            }
        }
    }

    // 尝试使用对应格式的Writer
    osgDB::ReaderWriter* rw = osgDB::Registry::instance()->getReaderWriterForExtension(ext);
    if (rw) {
        std::stringstream ss;
        osgDB::ReaderWriter::WriteResult wr = rw->writeImage(*image, ss);
        if (wr.success()) {
            std::string s = ss.str();
            outData.assign(s.begin(), s.end());
            outMimeType = getMimeTypeFromExtension(ext);
            return true;
        }
    }

    // 如果失败，尝试PNG
    if (ext != "png") {
        rw = osgDB::Registry::instance()->getReaderWriterForExtension("png");
        if (rw) {
            std::stringstream ss;
            osgDB::ReaderWriter::WriteResult wr = rw->writeImage(*image, ss);
            if (wr.success()) {
                std::string s = ss.str();
                outData.assign(s.begin(), s.end());
                outMimeType = "image/png";
                return true;
            }
        }
    }

    return false;
}

std::string TextureUtils::getMimeTypeFromExtension(const std::string& ext) {
    std::string lowerExt = ext;
    std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(), ::tolower);

    if (lowerExt == ".jpg" || lowerExt == ".jpeg") {
        return "image/jpeg";
    } else if (lowerExt == ".png") {
        return "image/png";
    } else if (lowerExt == ".ktx2") {
        return "image/ktx2";
    }

    return "image/png";  // 默认
}

} // namespace utils
} // namespace osg

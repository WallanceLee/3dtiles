#include "b3dm_generator.h"
#include "../extern.h"
#include "../gltf_writer/extension_manager.h"

#include <tiny_gltf.h>
#include <osg/Geometry>
#include <osg/Array>
#include <osg/PrimitiveSet>
#include <osgUtil/Optimizer>

#include <sstream>
#include <fstream>
#include <algorithm>
#include <set>

namespace b3dm {

// 辅助宏
#define SET_MIN(x,v) do{ if (x > v) x = v; }while (0);
#define SET_MAX(x,v) do{ if (x < v) x = v; }while (0);

template<class T>
void put_val(std::vector<unsigned char>& buf, T val) {
    buf.insert(buf.end(), (unsigned char*)&val, (unsigned char*)&val + sizeof(T));
}

void alignment_buffer(std::vector<unsigned char>& buf) {
    while (buf.size() % 4 != 0) {
        buf.push_back(0x00);
    }
}

tinygltf::BufferView create_buffer_view(int target, int byteOffset, int byteLength) {
    tinygltf::BufferView bfv;
    bfv.buffer = 0;
    bfv.target = target;
    bfv.byteOffset = byteOffset;
    bfv.byteLength = byteLength;
    return bfv;
}

B3DMGenerator::B3DMGenerator(const B3DMGeneratorConfig& config)
    : config_(config) {}

osg::ref_ptr<osg::Geometry> B3DMGenerator::extractAndMergeGeometries(
    const spatial::core::SpatialItemRefList& items) {

    if (!config_.geometryExtractor) {
        LOG_E("Geometry extractor not set");
        return nullptr;
    }

    // 提取所有几何体
    std::vector<osg::ref_ptr<osg::Geometry>> allGeoms;
    for (const auto& itemRef : items) {
        auto geoms = config_.geometryExtractor->extract(itemRef.get());
        allGeoms.insert(allGeoms.end(), geoms.begin(), geoms.end());
    }

    if (allGeoms.empty()) {
        return nullptr;
    }

    // 合并几何体
    osg::ref_ptr<osg::Geometry> mergedGeom = new osg::Geometry;
    osg::ref_ptr<osg::Vec3Array> mergedVertices = new osg::Vec3Array();
    osg::ref_ptr<osg::Vec3Array> mergedNormals = new osg::Vec3Array();
    osg::ref_ptr<osg::Vec2Array> mergedTexCoords = new osg::Vec2Array();
    osg::ref_ptr<osg::DrawElementsUInt> mergedIndices = new osg::DrawElementsUInt(osg::PrimitiveSet::TRIANGLES);

    for (size_t i = 0; i < allGeoms.size(); ++i) {
        if (!allGeoms[i].valid()) continue;

        osg::Vec3Array* vArr = dynamic_cast<osg::Vec3Array*>(allGeoms[i]->getVertexArray());
        if (!vArr || vArr->empty()) continue;

        osg::Vec3Array* nArr = dynamic_cast<osg::Vec3Array*>(allGeoms[i]->getNormalArray());
        osg::Vec2Array* tArr = dynamic_cast<osg::Vec2Array*>(allGeoms[i]->getTexCoordArray(0));

        const size_t base = mergedVertices->size();
        mergedVertices->insert(mergedVertices->end(), vArr->begin(), vArr->end());

        if (nArr && nArr->size() == vArr->size()) {
            mergedNormals->insert(mergedNormals->end(), nArr->begin(), nArr->end());
        } else {
            mergedNormals->insert(mergedNormals->end(), vArr->size(), osg::Vec3(0.0f, 0.0f, 1.0f));
        }

        if (tArr && tArr->size() == vArr->size()) {
            mergedTexCoords->insert(mergedTexCoords->end(), tArr->begin(), tArr->end());
        } else {
            mergedTexCoords->insert(mergedTexCoords->end(), vArr->size(), osg::Vec2(0.0f, 0.0f));
        }

        if (allGeoms[i]->getNumPrimitiveSets() > 0) {
            osg::PrimitiveSet* ps = allGeoms[i]->getPrimitiveSet(0);
            const auto idxCnt = ps->getNumIndices();
            for (unsigned int k = 0; k < idxCnt; ++k) {
                mergedIndices->push_back(static_cast<unsigned int>(base + ps->index(k)));
            }
        }
    }

    if (mergedVertices->empty() || mergedIndices->empty()) {
        return nullptr;
    }

    mergedGeom->setVertexArray(mergedVertices.get());
    mergedGeom->setNormalArray(mergedNormals.get());
    mergedGeom->setTexCoordArray(0, mergedTexCoords.get());
    mergedGeom->addPrimitiveSet(mergedIndices.get());

    return mergedGeom;
}

void B3DMGenerator::applySimplification(
    osg::Geometry* geometry,
    const SimplificationParams& params) {

    if (!geometry || !params.enable_simplification) {
        return;
    }

    simplify_mesh_geometry(geometry, params);
}

BatchData B3DMGenerator::buildBatchData(
    const spatial::core::SpatialItemRefList& items) {

    BatchData batchData;

    if (!config_.geometryExtractor) {
        return batchData;
    }

    // 收集所有属性键
    std::set<std::string> attributeKeys;
    for (const auto& itemRef : items) {
        auto attrs = config_.geometryExtractor->getAttributes(itemRef.get());
        for (const auto& kv : attrs) {
            attributeKeys.insert(kv.first);
        }
    }

    // 构建每个属性的数组
    for (const auto& key : attributeKeys) {
        std::vector<nlohmann::json> values;
        values.reserve(items.size());

        for (const auto& itemRef : items) {
            auto attrs = config_.geometryExtractor->getAttributes(itemRef.get());
            auto it = attrs.find(key);
            if (it != attrs.end()) {
                values.push_back(it->second);
            } else {
                values.push_back(nullptr);
            }
        }
        batchData.attributes[key] = std::move(values);
    }

    // 设置batchIds和names
    for (size_t i = 0; i < items.size(); ++i) {
        batchData.batchIds.push_back(static_cast<int>(i));
        batchData.names.push_back(
            config_.geometryExtractor->getId(items[i].get())
        );
    }

    return batchData;
}

void B3DMGenerator::buildGLTFModel(
    osg::Geometry* mergedGeom,
    const spatial::core::SpatialItemRefList& items,
    bool enableDraco,
    const DracoCompressionParams& dracoParams,
    std::vector<unsigned char>& glbData) {

    tinygltf::Model model;
    tinygltf::Buffer buffer;
    tinygltf::Scene scene;

    // 提取几何体数据
    osg::Vec3Array* vertices = dynamic_cast<osg::Vec3Array*>(mergedGeom->getVertexArray());
    osg::Vec3Array* normals = dynamic_cast<osg::Vec3Array*>(mergedGeom->getNormalArray());
    osg::Vec2Array* texcoords = dynamic_cast<osg::Vec2Array*>(mergedGeom->getTexCoordArray(0));
    osg::DrawElementsUInt* indices = dynamic_cast<osg::DrawElementsUInt*>(
        mergedGeom->getPrimitiveSet(0)
    );

    if (!vertices || !indices) {
        return;
    }

    // Draco压缩
    const bool dracoRequested = enableDraco && dracoParams.enable_compression;
    std::vector<unsigned char> dracoData;
    size_t dracoSize = 0;
    int dracoPosAtt = -1, dracoNormAtt = -1, dracoTexAtt = -1;

    if (dracoRequested) {
        DracoCompressionParams params = dracoParams;
        params.enable_compression = true;

        bool compressSuccess = compress_mesh_geometry(
            mergedGeom, params, dracoData, dracoSize,
            &dracoPosAtt, &dracoNormAtt, &dracoTexAtt,
            nullptr, nullptr
        );

        if (!compressSuccess) {
            LOG_E("Draco compression failed");
        }
    }

    // 构建Accessors和BufferViews
    int indexAccessorIndex = -1;
    int vertexAccessorIndex = -1;
    int normalAccessorIndex = -1;
    int texcoordAccessorIndex = -1;

    // 索引accessor
    {
        indexAccessorIndex = static_cast<int>(model.accessors.size());
        tinygltf::Accessor acc;
        acc.byteOffset = 0;
        acc.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
        acc.count = static_cast<int>(indices->size());
        acc.type = TINYGLTF_TYPE_SCALAR;
        acc.maxValues = {static_cast<double>(vertices->size() - 1)};
        acc.minValues = {0.0};

        if (!dracoRequested) {
            int byteOffset = static_cast<int>(buffer.data.size());
            for (unsigned int idx : *indices) {
                put_val(buffer.data, idx);
            }
            acc.bufferView = static_cast<int>(model.bufferViews.size());
            alignment_buffer(buffer.data);
            model.bufferViews.push_back(create_buffer_view(
                TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER, byteOffset,
                static_cast<int>(buffer.data.size()) - byteOffset));
        } else {
            acc.bufferView = -1;
        }
        model.accessors.push_back(acc);
    }

    // 顶点accessor
    {
        vertexAccessorIndex = static_cast<int>(model.accessors.size());
        std::vector<double> boxMax = {-1e38, -1e38, -1e38};
        std::vector<double> boxMin = {1e38, 1e38, 1e38};

        for (const auto& v : *vertices) {
            SET_MAX(boxMax[0], v.x());
            SET_MAX(boxMax[1], v.y());
            SET_MAX(boxMax[2], v.z());
            SET_MIN(boxMin[0], v.x());
            SET_MIN(boxMin[1], v.y());
            SET_MIN(boxMin[2], v.z());
        }

        tinygltf::Accessor acc;
        acc.byteOffset = 0;
        acc.count = static_cast<int>(vertices->size());
        acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
        acc.type = TINYGLTF_TYPE_VEC3;
        acc.maxValues = boxMax;
        acc.minValues = boxMin;

        if (!dracoRequested) {
            int byteOffset = static_cast<int>(buffer.data.size());
            for (const auto& v : *vertices) {
                put_val(buffer.data, v.x());
                put_val(buffer.data, v.y());
                put_val(buffer.data, v.z());
            }
            acc.bufferView = static_cast<int>(model.bufferViews.size());
            alignment_buffer(buffer.data);
            model.bufferViews.push_back(create_buffer_view(
                TINYGLTF_TARGET_ARRAY_BUFFER, byteOffset,
                static_cast<int>(buffer.data.size()) - byteOffset));
        } else {
            acc.bufferView = -1;
        }
        model.accessors.push_back(acc);
    }

    // 法线accessor
    if (normals && !normals->empty()) {
        normalAccessorIndex = static_cast<int>(model.accessors.size());
        tinygltf::Accessor acc;
        acc.byteOffset = 0;
        acc.count = static_cast<int>(normals->size());
        acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
        acc.type = TINYGLTF_TYPE_VEC3;

        if (!dracoRequested) {
            int byteOffset = static_cast<int>(buffer.data.size());
            for (const auto& n : *normals) {
                put_val(buffer.data, n.x());
                put_val(buffer.data, n.y());
                put_val(buffer.data, n.z());
            }
            acc.bufferView = static_cast<int>(model.bufferViews.size());
            alignment_buffer(buffer.data);
            model.bufferViews.push_back(create_buffer_view(
                TINYGLTF_TARGET_ARRAY_BUFFER, byteOffset,
                static_cast<int>(buffer.data.size()) - byteOffset));
        } else {
            acc.bufferView = -1;
        }
        model.accessors.push_back(acc);
    }

    // 纹理坐标accessor
    if (texcoords && !texcoords->empty()) {
        texcoordAccessorIndex = static_cast<int>(model.accessors.size());
        tinygltf::Accessor acc;
        acc.byteOffset = 0;
        acc.count = static_cast<int>(texcoords->size());
        acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
        acc.type = TINYGLTF_TYPE_VEC2;

        if (!dracoRequested) {
            int byteOffset = static_cast<int>(buffer.data.size());
            for (const auto& t : *texcoords) {
                put_val(buffer.data, t.x());
                put_val(buffer.data, t.y());
            }
            acc.bufferView = static_cast<int>(model.bufferViews.size());
            alignment_buffer(buffer.data);
            model.bufferViews.push_back(create_buffer_view(
                TINYGLTF_TARGET_ARRAY_BUFFER, byteOffset,
                static_cast<int>(buffer.data.size()) - byteOffset));
        } else {
            acc.bufferView = -1;
        }
        model.accessors.push_back(acc);
    }

    // 处理Draco数据
    if (dracoRequested && !dracoData.empty()) {
        int dracoBufferView = static_cast<int>(model.bufferViews.size());
        int byteOffset = static_cast<int>(buffer.data.size());
        buffer.data.insert(buffer.data.end(), dracoData.begin(), dracoData.end());

        tinygltf::BufferView bfv;
        bfv.buffer = 0;
        bfv.byteOffset = byteOffset;
        bfv.byteLength = static_cast<int>(dracoSize);
        model.bufferViews.push_back(bfv);

        // 添加Draco扩展
        tinygltf::ExtensionMap dracoExt;
        dracoExt["bufferView"] = tinygltf::Value(dracoBufferView);
        dracoExt["attributes"] = tinygltf::Value(tinygltf::Value::Object{
            {"POSITION", tinygltf::Value(dracoPosAtt)},
            {"NORMAL", tinygltf::Value(dracoNormAtt)},
            {"TEXCOORD_0", tinygltf::Value(dracoTexAtt)}
        });

        tinygltf::ExtensionMap extMap;
        extMap["KHR_draco_mesh_compression"] = tinygltf::Value(dracoExt);

        // 创建mesh和primitive
        tinygltf::Mesh mesh;
        tinygltf::Primitive primitive;
        primitive.indices = indexAccessorIndex;
        primitive.attributes["POSITION"] = vertexAccessorIndex;
        if (normalAccessorIndex >= 0) {
            primitive.attributes["NORMAL"] = normalAccessorIndex;
        }
        if (texcoordAccessorIndex >= 0) {
            primitive.attributes["TEXCOORD_0"] = texcoordAccessorIndex;
        }
        primitive.extensions = extMap;
        primitive.mode = TINYGLTF_MODE_TRIANGLES;
        mesh.primitives.push_back(primitive);
        model.meshes.push_back(mesh);

        model.extensionsRequired.push_back("KHR_draco_mesh_compression");
        model.extensionsUsed.push_back("KHR_draco_mesh_compression");
    } else {
        // 创建普通mesh
        tinygltf::Mesh mesh;
        tinygltf::Primitive primitive;
        primitive.indices = indexAccessorIndex;
        primitive.attributes["POSITION"] = vertexAccessorIndex;
        if (normalAccessorIndex >= 0) {
            primitive.attributes["NORMAL"] = normalAccessorIndex;
        }
        if (texcoordAccessorIndex >= 0) {
            primitive.attributes["TEXCOORD_0"] = texcoordAccessorIndex;
        }
        primitive.mode = TINYGLTF_MODE_TRIANGLES;
        mesh.primitives.push_back(primitive);
        model.meshes.push_back(mesh);
    }

    // 创建node
    tinygltf::Node node;
    node.mesh = 0;
    model.nodes.push_back(node);
    scene.nodes.push_back(0);

    // 设置场景和buffer
    model.scenes.push_back(scene);
    model.defaultScene = 0;
    model.buffers.push_back(buffer);

    // 序列化为GLB
    tinygltf::TinyGLTF gltf;
    std::ostringstream ss;
    gltf.WriteGltfSceneToStream(&model, ss, false, true);
    std::string glbStr = ss.str();
    glbData.assign(glbStr.begin(), glbStr.end());
}

std::string B3DMGenerator::generateFilename(int lodLevel) const {
    if (lodLevel == 0) {
        return "content.b3dm";
    }
    return "content_lod" + std::to_string(lodLevel) + ".b3dm";
}

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

    // 构建GLTF模型
    std::vector<unsigned char> glbData;
    buildGLTFModel(
        mergedGeom.get(),
        items,
        lodSettings.enable_draco,
        lodSettings.draco,
        glbData
    );

    if (glbData.empty()) {
        LOG_E("Failed to build GLTF model");
        return std::string();
    }

    // 构建BatchData并包装为B3DM
    BatchData batchData = buildBatchData(items);
    Options opts;
    opts.alignTo8Bytes = true;

    std::string glbStr(glbData.begin(), glbData.end());
    return wrapGlbToB3dm(glbStr, batchData, opts);
}

std::vector<LODFileInfo> B3DMGenerator::generateLODFiles(
    const spatial::core::SpatialItemRefList& items,
    const std::string& outputDir,
    const std::string& baseFilename,
    const std::vector<LODLevelSettings>& lodLevels) {

    std::vector<LODFileInfo> result;

    if (items.empty() || lodLevels.empty()) {
        return result;
    }

    // 创建输出目录
    std::filesystem::create_directories(outputDir);

    // 生成每个LOD级别
    for (size_t i = 0; i < lodLevels.size(); ++i) {
        const auto& level = lodLevels[i];

        // 生成B3DM数据
        std::string b3dmData = generate(items, level);
        if (b3dmData.empty()) {
            LOG_W("Failed to generate B3DM for LOD %zu", i);
            continue;
        }

        // 构建文件名
        std::string filename = generateFilename(static_cast<int>(i));
        std::string filepath = outputDir + "/" + filename;

        // 写入文件
        std::ofstream file(filepath, std::ios::binary);
        if (!file) {
            LOG_E("Failed to open file for writing: %s", filepath.c_str());
            continue;
        }
        file.write(b3dmData.data(), b3dmData.size());
        file.close();

        // 计算几何误差
        double geometricError = config_.simplifyParams.target_error;
        if (i > 0 && !lodLevels.empty()) {
            geometricError = lodLevels[0].target_error * (1.0 + i * 0.5);
        }

        LODFileInfo info;
        info.level = static_cast<int>(i);
        info.filename = filename;
        info.relativePath = filename;
        info.geometricError = geometricError;
        result.push_back(info);

        LOG_I("Generated %s (LOD %zu, ratio: %.2f)",
              filename.c_str(), i, level.target_ratio);
    }

    return result;
}

std::vector<LODFileInfo> B3DMGenerator::generateLODFilesWithPath(
    const spatial::core::SpatialItemRefList& items,
    const std::string& outputRoot,
    const std::string& tilePath,
    const std::vector<LODLevelSettings>& lodLevels) {

    std::string fullPath = outputRoot + "/" + tilePath;
    std::string baseName = std::filesystem::path(tilePath).filename().string();

    auto files = generateLODFiles(items, fullPath, baseName, lodLevels);

    // 更新相对路径
    for (auto& file : files) {
        file.relativePath = tilePath + "/" + file.filename;
    }

    return files;
}

} // namespace b3dm

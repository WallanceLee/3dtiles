#include "b3dm_content_generator.h"
#include "../b3dm/b3dm_writer.h"
#include "../mesh_processor.h"
#include "../extern.h"

#include <tiny_gltf.h>
#include <osg/Geometry>
#include <osg/Array>
#include <osg/PrimitiveSet>
#include <osgUtil/Optimizer>

#include <vector>
#include <string>
#include <array>
#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

namespace shapefile {

// 辅助宏
#define SET_MIN(x,v) do{ if (x > v) x = v; }while (0);
#define SET_MAX(x,v) do{ if (x < v) x = v; }while (0);

template<class T>
void put_val(std::vector<unsigned char>& buf, T val) {
    buf.insert(buf.end(), (unsigned char*)&val, (unsigned char*)&val + sizeof(T));
}

template<class T>
void put_val(std::string& buf, T val) {
    buf.append((unsigned char*)&val, (unsigned char*)&val + sizeof(T));
}

template<class T>
void alignment_buffer(std::vector<T>& buf) {
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

B3DMContentGenerator::B3DMContentGenerator(double centerLon, double centerLat)
    : centerLon_(centerLon), centerLat_(centerLat) {}

std::vector<osg::ref_ptr<osg::Geometry>> B3DMContentGenerator::extractGeometries(
    const std::vector<const ShapefileSpatialItem*>& items) {
    std::vector<osg::ref_ptr<osg::Geometry>> geometries;
    geometries.reserve(items.size());

    for (const auto* item : items) {
        if (!item) continue;
        for (const auto& geom : item->geometries) {
            if (geom.valid()) {
                geometries.push_back(geom);
            }
        }
    }

    return geometries;
}

b3dm::BatchData B3DMContentGenerator::buildBatchData(
    const std::vector<const ShapefileSpatialItem*>& items,
    bool withHeight) {
    b3dm::BatchData batchData;
    batchData.batchIds.reserve(items.size());
    batchData.names.reserve(items.size());

    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        if (!items[i]) continue;
        batchData.batchIds.push_back(i);
        // 使用featureId作为名称，或者从properties中获取名称字段
        auto it = items[i]->properties.find("name");
        if (it != items[i]->properties.end() && it->second.is_string()) {
            batchData.names.push_back(it->second.get<std::string>());
        } else {
            batchData.names.push_back("feature_" + std::to_string(items[i]->featureId));
        }
    }

    // 收集所有属性键
    std::set<std::string> attributeKeys;
    for (const auto* item : items) {
        if (!item) continue;
        for (const auto& kv : item->properties) {
            attributeKeys.insert(kv.first);
        }
    }

    // 构建每个属性的数组
    for (const auto& key : attributeKeys) {
        std::vector<nlohmann::json> values(items.size(), nullptr);
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            if (!items[i]) continue;
            auto it = items[i]->properties.find(key);
            if (it != items[i]->properties.end()) {
                values[i] = it->second;
            }
        }
        batchData.attributes[key] = std::move(values);
    }

    // 添加高度属性
    if (withHeight) {
        std::vector<nlohmann::json> heights;
        heights.reserve(items.size());
        for (const auto* item : items) {
            if (!item) {
                heights.push_back(0.0);
                continue;
            }
            auto it = item->properties.find("height");
            if (it != item->properties.end()) {
                heights.push_back(it->second);
            } else {
                heights.push_back(0.0);
            }
        }
        batchData.attributes["height"] = std::move(heights);
    }

    return batchData;
}

std::string B3DMContentGenerator::generate(
    const std::vector<const ShapefileSpatialItem*>& items,
    bool withHeight,
    bool enableSimplify,
    const std::optional<SimplificationParams>& simplifyParams,
    bool enableDraco,
    const std::optional<DracoCompressionParams>& dracoParams) {

    if (items.empty()) {
        return std::string();
    }

    // 提取所有几何体
    std::vector<osg::ref_ptr<osg::Geometry>> osgGeoms = extractGeometries(items);
    if (osgGeoms.empty()) {
        LOG_E("No valid geometries found");
        return std::string();
    }

    // 简化处理
    if (enableSimplify && simplifyParams.has_value()) {
        for (auto& geom : osgGeoms) {
            if (geom.valid() && geom->getNumPrimitiveSets() > 0) {
                simplify_mesh_geometry(geom.get(), simplifyParams.value());
            }
        }
    }

    // 合并所有几何体
    osg::ref_ptr<osg::Geometry> mergedGeom = new osg::Geometry;
    osg::ref_ptr<osg::Vec3Array> mergedVertices = new osg::Vec3Array();
    osg::ref_ptr<osg::Vec3Array> mergedNormals = new osg::Vec3Array();
    osg::ref_ptr<osg::DrawElementsUInt> mergedIndices = new osg::DrawElementsUInt(osg::PrimitiveSet::TRIANGLES);
    std::vector<uint32_t> mergedBatchIds;

    for (size_t i = 0; i < osgGeoms.size(); ++i) {
        if (!osgGeoms[i].valid()) continue;

        osg::Vec3Array* vArr = dynamic_cast<osg::Vec3Array*>(osgGeoms[i]->getVertexArray());
        if (!vArr || vArr->empty()) continue;

        osg::Vec3Array* nArr = dynamic_cast<osg::Vec3Array*>(osgGeoms[i]->getNormalArray());

        const size_t base = mergedVertices->size();
        mergedVertices->insert(mergedVertices->end(), vArr->begin(), vArr->end());

        if (nArr && nArr->size() == vArr->size()) {
            mergedNormals->insert(mergedNormals->end(), nArr->begin(), nArr->end());
        } else {
            mergedNormals->insert(mergedNormals->end(), vArr->size(), osg::Vec3(0.0f, 0.0f, 1.0f));
        }

        mergedBatchIds.insert(mergedBatchIds.end(), vArr->size(), static_cast<uint32_t>(i));

        if (osgGeoms[i]->getNumPrimitiveSets() > 0) {
            osg::PrimitiveSet* ps = osgGeoms[i]->getPrimitiveSet(0);
            const auto idxCnt = ps->getNumIndices();
            for (unsigned int k = 0; k < idxCnt; ++k) {
                mergedIndices->push_back(static_cast<unsigned int>(base + ps->index(k)));
            }
        }
    }

    if (mergedVertices->empty() || mergedIndices->empty()) {
        LOG_E("Merged geometry is empty");
        return std::string();
    }

    mergedGeom->setVertexArray(mergedVertices.get());
    mergedGeom->setNormalArray(mergedNormals.get());
    mergedGeom->addPrimitiveSet(mergedIndices.get());

    // Draco压缩
    const bool dracoRequested = enableDraco && dracoParams.has_value() && dracoParams->enable_compression;
    std::vector<unsigned char> dracoData;
    size_t dracoSize = 0;
    int dracoPosAtt = -1, dracoNormAtt = -1, dracoTexAtt = -1, dracoBatchidAtt = -1;

    if (dracoRequested) {
        DracoCompressionParams params = dracoParams.value();
        params.enable_compression = true;

        std::vector<float> batchIdsF;
        batchIdsF.reserve(mergedBatchIds.size());
        for (auto id : mergedBatchIds) batchIdsF.push_back(static_cast<float>(id));

        bool compressSuccess = compress_mesh_geometry(
            mergedGeom.get(), params, dracoData, dracoSize,
            &dracoPosAtt, &dracoNormAtt, &dracoTexAtt, &dracoBatchidAtt, &batchIdsF);

        if (!compressSuccess) {
            LOG_E("Draco compression failed");
            return std::string();
        }
    }

    // 构建GLTF模型
    tinygltf::TinyGLTF gltf;
    tinygltf::Model model;
    tinygltf::Buffer buffer;
    tinygltf::Scene scene;

    int indexAccessorIndex = -1;
    int vertexAccessorIndex = -1;
    int normalAccessorIndex = -1;
    int batchidAccessorIndex = -1;

    // 索引accessor
    {
        osg::PrimitiveSet* ps = mergedGeom->getPrimitiveSet(0);
        int idxSize = ps->getNumIndices();
        uint32_t maxIdx = 0;

        for (int m = 0; m < idxSize; m++) {
            uint32_t idx = static_cast<uint32_t>(ps->index(m));
            SET_MAX(maxIdx, idx);
        }

        indexAccessorIndex = static_cast<int>(model.accessors.size());
        tinygltf::Accessor acc;
        acc.byteOffset = 0;
        acc.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
        acc.count = idxSize;
        acc.type = TINYGLTF_TYPE_SCALAR;
        acc.maxValues = {static_cast<double>(maxIdx)};
        acc.minValues = {0.0};

        if (!dracoRequested) {
            int byteOffset = static_cast<int>(buffer.data.size());
            for (int m = 0; m < idxSize; m++) {
                uint32_t idx = static_cast<uint32_t>(ps->index(m));
                put_val(buffer.data, idx);
            }
            acc.bufferView = static_cast<int>(model.bufferViews.size());
            alignment_buffer(buffer.data);
            tinygltf::BufferView bfv = create_buffer_view(
                TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER, byteOffset,
                static_cast<int>(buffer.data.size()) - byteOffset);
            model.bufferViews.push_back(bfv);
        } else {
            acc.bufferView = -1;
        }
        model.accessors.push_back(acc);
    }

    // 顶点accessor
    {
        osg::Vec3Array* v3f = mergedVertices.get();
        int vecSize = v3f->size();
        std::vector<double> boxMax = {-1e38, -1e38, -1e38};
        std::vector<double> boxMin = {1e38, 1e38, 1e38};

        for (int vidx = 0; vidx < vecSize; vidx++) {
            osg::Vec3f point = v3f->at(vidx);
            std::vector<float> vertex = {point.x(), point.y(), point.z()};
            for (int i = 0; i < 3; i++) {
                SET_MAX(boxMax[i], vertex[i]);
                SET_MIN(boxMin[i], vertex[i]);
            }
        }

        vertexAccessorIndex = static_cast<int>(model.accessors.size());
        tinygltf::Accessor acc;
        acc.byteOffset = 0;
        acc.count = vecSize;
        acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
        acc.type = TINYGLTF_TYPE_VEC3;
        acc.maxValues = boxMax;
        acc.minValues = boxMin;

        if (!dracoRequested) {
            int byteOffset = static_cast<int>(buffer.data.size());
            for (int vidx = 0; vidx < vecSize; vidx++) {
                osg::Vec3f point = v3f->at(vidx);
                std::vector<float> vertex = {point.x(), point.y(), point.z()};
                for (int i = 0; i < 3; i++) {
                    put_val(buffer.data, vertex[i]);
                }
            }
            acc.bufferView = static_cast<int>(model.bufferViews.size());
            alignment_buffer(buffer.data);
            tinygltf::BufferView bfv = create_buffer_view(
                TINYGLTF_TARGET_ARRAY_BUFFER, byteOffset,
                static_cast<int>(buffer.data.size()) - byteOffset);
            model.bufferViews.push_back(bfv);
        } else {
            acc.bufferView = -1;
        }
        model.accessors.push_back(acc);
    }

    // 法线accessor
    {
        osg::Vec3Array* v3f = mergedNormals.get();
        int normalSize = v3f->size();
        if (normalSize > 0) {
            std::vector<double> boxMax = {-1e38, -1e38, -1e38};
            std::vector<double> boxMin = {1e38, 1e38, 1e38};

            for (int vidx = 0; vidx < normalSize; vidx++) {
                osg::Vec3f point = v3f->at(vidx);
                std::vector<float> vertex = {point.x(), point.y(), point.z()};
                for (int i = 0; i < 3; i++) {
                    SET_MAX(boxMax[i], vertex[i]);
                    SET_MIN(boxMin[i], vertex[i]);
                }
            }

            normalAccessorIndex = static_cast<int>(model.accessors.size());
            tinygltf::Accessor acc;
            acc.byteOffset = 0;
            acc.count = normalSize;
            acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
            acc.type = TINYGLTF_TYPE_VEC3;
            acc.maxValues = boxMax;
            acc.minValues = boxMin;

            if (!dracoRequested) {
                int byteOffset = static_cast<int>(buffer.data.size());
                for (int vidx = 0; vidx < normalSize; vidx++) {
                    osg::Vec3f point = v3f->at(vidx);
                    std::vector<float> vertex = {point.x(), point.y(), point.z()};
                    for (int i = 0; i < 3; i++) {
                        put_val(buffer.data, vertex[i]);
                    }
                }
                acc.bufferView = static_cast<int>(model.bufferViews.size());
                alignment_buffer(buffer.data);
                tinygltf::BufferView bfv = create_buffer_view(
                    TINYGLTF_TARGET_ARRAY_BUFFER, byteOffset,
                    static_cast<int>(buffer.data.size()) - byteOffset);
                model.bufferViews.push_back(bfv);
            } else {
                acc.bufferView = -1;
            }
            model.accessors.push_back(acc);
        }
    }

    // BatchId accessor
    if (!mergedBatchIds.empty()) {
        batchidAccessorIndex = static_cast<int>(model.accessors.size());
        uint32_t maxBatchId = *std::max_element(mergedBatchIds.begin(), mergedBatchIds.end());

        tinygltf::Accessor acc;
        acc.byteOffset = 0;
        acc.count = static_cast<int>(mergedBatchIds.size());
        // Use UNSIGNED_SHORT for batch IDs as per glTF spec for joints/weights
        acc.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT;
        acc.type = TINYGLTF_TYPE_SCALAR;
        acc.maxValues = {static_cast<double>(maxBatchId)};
        acc.minValues = {0.0};

        if (!dracoRequested) {
            int byteOffset = static_cast<int>(buffer.data.size());
            for (auto id : mergedBatchIds) {
                uint16_t sid = static_cast<uint16_t>(id);
                put_val(buffer.data, sid);
            }
            acc.bufferView = static_cast<int>(model.bufferViews.size());
            alignment_buffer(buffer.data);
            tinygltf::BufferView bfv = create_buffer_view(
                TINYGLTF_TARGET_ARRAY_BUFFER, byteOffset,
                static_cast<int>(buffer.data.size()) - byteOffset);
            model.bufferViews.push_back(bfv);
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
            {"_BATCHID", tinygltf::Value(dracoBatchidAtt)}
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
        if (batchidAccessorIndex >= 0) {
            primitive.attributes["_BATCHID"] = batchidAccessorIndex;
        }
        primitive.extensions = extMap;
        primitive.mode = TINYGLTF_MODE_TRIANGLES;
        mesh.primitives.push_back(primitive);
        model.meshes.push_back(mesh);

        // 添加扩展声明
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
        if (batchidAccessorIndex >= 0) {
            primitive.attributes["_BATCHID"] = batchidAccessorIndex;
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

    // 生成GLB
    std::ostringstream ss;
    bool res = gltf.WriteGltfSceneToStream(&model, ss, false, true);
    if (!res) {
        LOG_E("Failed to write GLB buffer");
        return std::string();
    }
    std::string glbBuffer = ss.str();

    // 构建BatchData并包装为B3DM
    b3dm::BatchData batchData = buildBatchData(items, withHeight);
    b3dm::Options opts;
    opts.alignTo8Bytes = true;

    return b3dm::wrapGlbToB3dm(glbBuffer, batchData, opts);
}

bool B3DMContentGenerator::generateToFile(
    const std::vector<const ShapefileSpatialItem*>& items,
    const std::string& outputPath,
    bool withHeight) {

    std::string b3dmData = generate(items, withHeight);
    if (b3dmData.empty()) {
        return false;
    }

    return b3dm::writeB3dmToFile(outputPath, b3dmData);
}

} // namespace shapefile

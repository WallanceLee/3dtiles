#include "b3dm_generator.h"
#include "../extern.h"
#include "../gltf/extension_manager.h"
#include "../gltf/material_builder.h"

#include <tiny_gltf.h>
#include <osg/Geometry>
#include <osg/Array>
#include <osg/PrimitiveSet>
#include <osgUtil/Optimizer>

#include <sstream>
#include <fstream>
#include <set>

// For image encoding
#include "stb_image_write.h"

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
        LOG_W("No geometries extracted from items");
        return nullptr;
    }

    // 合并几何体
    osg::ref_ptr<osg::Geometry> mergedGeom = new osg::Geometry;
    osg::ref_ptr<osg::Vec3Array> mergedVertices = new osg::Vec3Array();
    osg::ref_ptr<osg::Vec3Array> mergedNormals = new osg::Vec3Array();
    osg::ref_ptr<osg::Vec2Array> mergedTexCoords = new osg::Vec2Array();
    osg::ref_ptr<osg::DrawElementsUInt> mergedIndices = new osg::DrawElementsUInt(osg::PrimitiveSet::TRIANGLES);

    for (size_t i = 0; i < allGeoms.size(); ++i) {
        if (!allGeoms[i].valid()) {
            continue;
        }

        // 尝试获取顶点数组 (支持 Vec3Array 和 Vec3dArray)
        osg::Vec3Array* vArr = dynamic_cast<osg::Vec3Array*>(allGeoms[i]->getVertexArray());
        osg::Vec3dArray* vArrd = dynamic_cast<osg::Vec3dArray*>(allGeoms[i]->getVertexArray());

        size_t vertexCount = 0;
        if (vArr && !vArr->empty()) {
            vertexCount = vArr->size();
        } else if (vArrd && !vArrd->empty()) {
            vertexCount = vArrd->size();
        } else {
            continue;
        }

        osg::Vec3Array* nArr = dynamic_cast<osg::Vec3Array*>(allGeoms[i]->getNormalArray());
        osg::Vec2Array* tArr = dynamic_cast<osg::Vec2Array*>(allGeoms[i]->getTexCoordArray(0));

        const size_t base = mergedVertices->size();

        // 添加顶点 (转换 Vec3dArray 到 Vec3Array)
        if (vArr) {
            mergedVertices->insert(mergedVertices->end(), vArr->begin(), vArr->end());
        } else if (vArrd) {
            for (const auto& v : *vArrd) {
                mergedVertices->push_back(osg::Vec3(v.x(), v.y(), v.z()));
            }
        }

        if (nArr && nArr->size() == vertexCount) {
            mergedNormals->insert(mergedNormals->end(), nArr->begin(), nArr->end());
        } else {
            mergedNormals->insert(mergedNormals->end(), vertexCount, osg::Vec3(0.0f, 0.0f, 1.0f));
        }

        if (tArr && tArr->size() == vertexCount) {
            mergedTexCoords->insert(mergedTexCoords->end(), tArr->begin(), tArr->end());
        } else {
            mergedTexCoords->insert(mergedTexCoords->end(), vertexCount, osg::Vec2(0.0f, 0.0f));
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

    // 复制第一个有效几何体的StateSet（材质信息）
    for (const auto& geom : allGeoms) {
        if (geom.valid() && geom->getStateSet()) {
            mergedGeom->setStateSet(const_cast<osg::StateSet*>(geom->getStateSet()));
            break;
        }
    }

    return mergedGeom;
}

std::vector<B3DMGenerator::MaterialGroup> B3DMGenerator::extractAndMergeGeometriesByMaterial(
    const spatial::core::SpatialItemRefList& items) {

    std::vector<MaterialGroup> result;

    if (!config_.geometryExtractor) {
        LOG_E("Geometry extractor not set");
        return result;
    }

    // 按材质分组收集几何体
    std::map<std::string, std::vector<osg::ref_ptr<osg::Geometry>>> materialGroups;
    std::map<std::string, std::shared_ptr<common::MaterialInfo>> materialInfoMap;

    for (const auto& itemRef : items) {
        auto geoms = config_.geometryExtractor->extract(itemRef.get());
        auto matInfo = config_.geometryExtractor->getMaterial(itemRef.get());

        // 计算材质键值用于分组
        std::string matKey = computeMaterialKey(matInfo);

        if (materialInfoMap.find(matKey) == materialInfoMap.end()) {
            materialInfoMap[matKey] = matInfo ? matInfo : std::make_shared<common::MaterialInfo>();
        }

        for (auto& geom : geoms) {
            if (geom.valid()) {
                materialGroups[matKey].push_back(geom);
            }
        }
    }

    // 对每个材质组合并几何体
    for (auto& pair : materialGroups) {
        const std::string& matKey = pair.first;
        auto& geoms = pair.second;

        if (geoms.empty()) {
            continue;
        }

        // 合并几何体
        osg::ref_ptr<osg::Geometry> mergedGeom = new osg::Geometry;
        osg::ref_ptr<osg::Vec3Array> mergedVertices = new osg::Vec3Array();
        osg::ref_ptr<osg::Vec3Array> mergedNormals = new osg::Vec3Array();
        osg::ref_ptr<osg::Vec2Array> mergedTexCoords = new osg::Vec2Array();
        osg::ref_ptr<osg::DrawElementsUInt> mergedIndices = new osg::DrawElementsUInt(osg::PrimitiveSet::TRIANGLES);

        for (size_t i = 0; i < geoms.size(); ++i) {
            if (!geoms[i].valid()) {
                continue;
            }

            // 尝试获取顶点数组 (支持 Vec3Array 和 Vec3dArray)
            osg::Vec3Array* vArr = dynamic_cast<osg::Vec3Array*>(geoms[i]->getVertexArray());
            osg::Vec3dArray* vArrd = dynamic_cast<osg::Vec3dArray*>(geoms[i]->getVertexArray());

            size_t vertexCount = 0;
            if (vArr && !vArr->empty()) {
                vertexCount = vArr->size();
            } else if (vArrd && !vArrd->empty()) {
                vertexCount = vArrd->size();
            } else {
                continue;
            }

            osg::Vec3Array* nArr = dynamic_cast<osg::Vec3Array*>(geoms[i]->getNormalArray());
            osg::Vec2Array* tArr = dynamic_cast<osg::Vec2Array*>(geoms[i]->getTexCoordArray(0));

            const size_t base = mergedVertices->size();

            // 添加顶点 (转换 Vec3dArray 到 Vec3Array)
            if (vArr) {
                mergedVertices->insert(mergedVertices->end(), vArr->begin(), vArr->end());
            } else if (vArrd) {
                for (const auto& v : *vArrd) {
                    mergedVertices->push_back(osg::Vec3(v.x(), v.y(), v.z()));
                }
            }

            if (nArr && nArr->size() == vertexCount) {
                mergedNormals->insert(mergedNormals->end(), nArr->begin(), nArr->end());
            } else {
                mergedNormals->insert(mergedNormals->end(), vertexCount, osg::Vec3(0.0f, 0.0f, 1.0f));
            }

            if (tArr && tArr->size() == vertexCount) {
                mergedTexCoords->insert(mergedTexCoords->end(), tArr->begin(), tArr->end());
            } else {
                mergedTexCoords->insert(mergedTexCoords->end(), vertexCount, osg::Vec2(0.0f, 0.0f));
            }

            if (geoms[i]->getNumPrimitiveSets() > 0) {
                osg::PrimitiveSet* ps = geoms[i]->getPrimitiveSet(0);
                const auto idxCnt = ps->getNumIndices();
                for (unsigned int k = 0; k < idxCnt; ++k) {
                    mergedIndices->push_back(static_cast<unsigned int>(base + ps->index(k)));
                }
            }
        }

        if (mergedVertices->empty() || mergedIndices->empty()) {
            continue;
        }

        mergedGeom->setVertexArray(mergedVertices.get());
        mergedGeom->setNormalArray(mergedNormals.get());
        mergedGeom->setTexCoordArray(0, mergedTexCoords.get());
        mergedGeom->addPrimitiveSet(mergedIndices.get());

        // 复制第一个有效几何体的StateSet（材质信息）
        for (const auto& geom : geoms) {
            if (geom.valid() && geom->getStateSet()) {
                mergedGeom->setStateSet(const_cast<osg::StateSet*>(geom->getStateSet()));
                break;
            }
        }

        // 添加到结果
        MaterialGroup group;
        group.materialInfo = materialInfoMap[matKey];
        group.geometry = mergedGeom;
        result.push_back(group);
    }

    return result;
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
    std::vector<unsigned char>& glbData,
    const std::vector<std::shared_ptr<common::MaterialInfo>>& materials) {

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

    // ===== 构建材质 =====
    int materialIndex = -1;
    if (!materials.empty() && materials[0]) {
        // 使用第一个对象的材质（简化处理，实际应该根据合并后的几何体材质情况处理）
        // 只要有材质信息就创建，不局限于有纹理的情况
        materialIndex = buildMaterial(materials[0], model, buffer);
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
        if (materialIndex >= 0) {
            primitive.material = materialIndex;
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
        if (materialIndex >= 0) {
            primitive.material = materialIndex;
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

void B3DMGenerator::buildGLTFModelMultiMaterial(
    const std::vector<MaterialGroup>& materialGroups,
    const spatial::core::SpatialItemRefList& items,
    bool enableDraco,
    const DracoCompressionParams& dracoParams,
    std::vector<unsigned char>& glbData) {

    tinygltf::Model model;
    tinygltf::Buffer buffer;
    tinygltf::Scene scene;

    // 创建mesh，包含多个primitive（每个材质一个）
    tinygltf::Mesh mesh;

    // 处理每个材质组
    for (const auto& group : materialGroups) {
        if (!group.geometry.valid()) {
            continue;
        }

        osg::Geometry* geom = group.geometry.get();
        osg::Vec3Array* vertices = dynamic_cast<osg::Vec3Array*>(geom->getVertexArray());
        osg::Vec3Array* normals = dynamic_cast<osg::Vec3Array*>(geom->getNormalArray());
        osg::Vec2Array* texcoords = dynamic_cast<osg::Vec2Array*>(geom->getTexCoordArray(0));
        osg::DrawElementsUInt* indices = dynamic_cast<osg::DrawElementsUInt*>(
            geom->getPrimitiveSet(0)
        );

        if (!vertices || !indices) {
            continue;
        }

        // 记录当前的accessor索引
        int indexAccessorIndex = static_cast<int>(model.accessors.size());
        int vertexAccessorIndex = static_cast<int>(model.accessors.size() + 1);
        int normalAccessorIndex = normals && !normals->empty() ? static_cast<int>(model.accessors.size() + 2) : -1;
        int texcoordAccessorIndex = texcoords && !texcoords->empty() ? static_cast<int>(model.accessors.size() + 3) : -1;

        // 索引accessor
        {
            tinygltf::Accessor acc;
            acc.byteOffset = 0;
            acc.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
            acc.count = static_cast<int>(indices->size());
            acc.type = TINYGLTF_TYPE_SCALAR;
            acc.maxValues = {static_cast<double>(vertices->size() - 1)};
            acc.minValues = {0.0};

            int byteOffset = static_cast<int>(buffer.data.size());
            for (unsigned int idx : *indices) {
                put_val(buffer.data, idx);
            }
            acc.bufferView = static_cast<int>(model.bufferViews.size());
            alignment_buffer(buffer.data);
            model.bufferViews.push_back(create_buffer_view(
                TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER, byteOffset,
                static_cast<int>(buffer.data.size()) - byteOffset));
            model.accessors.push_back(acc);
        }

        // 顶点accessor
        {
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
            model.accessors.push_back(acc);
        }

        // 法线accessor
        if (normals && !normals->empty()) {
            tinygltf::Accessor acc;
            acc.byteOffset = 0;
            acc.count = static_cast<int>(normals->size());
            acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
            acc.type = TINYGLTF_TYPE_VEC3;

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
            model.accessors.push_back(acc);
        }

        // 纹理坐标accessor
        if (texcoords && !texcoords->empty()) {
            tinygltf::Accessor acc;
            acc.byteOffset = 0;
            acc.count = static_cast<int>(texcoords->size());
            acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
            acc.type = TINYGLTF_TYPE_VEC2;

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
            model.accessors.push_back(acc);
        }

        // 构建材质
        int materialIndex = -1;
        if (group.materialInfo) {
            materialIndex = buildMaterial(group.materialInfo, model, buffer);
        }

        // 创建primitive
        tinygltf::Primitive primitive;
        primitive.indices = indexAccessorIndex;
        primitive.attributes["POSITION"] = vertexAccessorIndex;
        if (normalAccessorIndex >= 0) {
            primitive.attributes["NORMAL"] = normalAccessorIndex;
        }
        if (texcoordAccessorIndex >= 0) {
            primitive.attributes["TEXCOORD_0"] = texcoordAccessorIndex;
        }
        if (materialIndex >= 0) {
            primitive.material = materialIndex;
        }
        primitive.mode = TINYGLTF_MODE_TRIANGLES;
        mesh.primitives.push_back(primitive);
    }

    if (mesh.primitives.empty()) {
        LOG_E("No valid primitives created");
        return;
    }

    model.meshes.push_back(mesh);

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
    return "content_lod" + std::to_string(lodLevel) + ".b3dm";
}

std::string B3DMGenerator::generate(
    const spatial::core::SpatialItemRefList& items,
    const LODLevelSettings& lodSettings) {

    if (items.empty()) {
        return std::string();
    }

    // 清空材质缓存（每个B3DM独立）
    materialCache_.clear();

    // 按材质分组合并几何体
    std::vector<MaterialGroup> materialGroups = extractAndMergeGeometriesByMaterial(items);
    if (materialGroups.empty()) {
        LOG_E("Failed to extract and merge geometries");
        return std::string();
    }

    // 应用简化到每个材质组的几何体
    if (lodSettings.enable_simplification) {
        for (auto& group : materialGroups) {
            if (group.geometry.valid()) {
                applySimplification(group.geometry.get(), lodSettings.simplify);
            }
        }
    }

    // 构建GLTF模型（支持多材质）
    std::vector<unsigned char> glbData;
    buildGLTFModelMultiMaterial(
        materialGroups,
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

// ===== 材质相关辅助函数实现 =====

std::string B3DMGenerator::computeMaterialKey(
    const std::shared_ptr<common::MaterialInfo>& matInfo) {
    if (!matInfo) {
        return "default";
    }

    std::ostringstream oss;
    // 基础颜色
    for (const auto& c : matInfo->baseColor) {
        oss << c << "_";
    }
    // 粗糙度和金属度
    oss << matInfo->roughnessFactor << "_" << matInfo->metallicFactor << "_";
    // 纹理指针（作为唯一标识）
    oss << matInfo->baseColorTexture.get() << "_"
        << matInfo->normalTexture.get() << "_"
        << matInfo->metallicRoughnessTexture.get() << "_"
        << matInfo->emissiveTexture.get();

    return oss.str();
}

// Helper function to write buffer data for stb_image_write
static void write_buffer(void* context, void* data, int len) {
    std::vector<unsigned char>* buf = static_cast<std::vector<unsigned char>*>(context);
    buf->insert(buf->end(), static_cast<unsigned char*>(data), static_cast<unsigned char*>(data) + len);
}

void B3DMGenerator::processAndAddTexture(
    osg::Texture* texture,
    const common::TextureTransformInfo& transform,
    TextureType type,
    tinygltf::Model& model,
    tinygltf::Buffer& buffer,
    int& textureIndexOut,
    bool& hasAlphaOut) {

    textureIndexOut = -1;
    hasAlphaOut = false;

    if (!texture) {
        return;
    }

    // 获取纹理图像
    osg::Image* image = texture->getImage(0);
    if (!image) {
        LOG_W("Texture has no image");
        return;
    }

    int width = image->s();
    int height = image->t();
    GLenum pixelFormat = image->getPixelFormat();
    const unsigned char* sourceData = image->data();
    unsigned int rowStep = image->getRowStepInBytes();
    unsigned int rowSize = image->getRowSizeInBytes();
    bool hasRowPadding = (rowStep != rowSize);

    // 检查是否有Alpha通道
    hasAlphaOut = (pixelFormat == GL_RGBA || pixelFormat == GL_BGRA);

    std::vector<unsigned char> encodedData;
    std::string mimeType;
    bool encodeSuccess = false;

    // 根据是否有Alpha通道选择编码格式
    if (hasAlphaOut) {
        // 有Alpha通道，使用PNG编码
        std::vector<unsigned char> rgbaData;
        rgbaData.resize(width * height * 4);

        if (pixelFormat == GL_RGBA) {
            if (hasRowPadding) {
                for (int row = 0; row < height; row++) {
                    memcpy(&rgbaData[row * width * 4],
                           &sourceData[row * rowStep],
                           width * 4);
                }
            } else {
                memcpy(rgbaData.data(), sourceData, width * height * 4);
            }
        } else if (pixelFormat == GL_BGRA) {
            // Convert BGRA to RGBA
            for (int row = 0; row < height; row++) {
                for (int col = 0; col < width; col++) {
                    int srcIdx = row * rowStep + col * 4;
                    int dstIdx = (row * width + col) * 4;
                    rgbaData[dstIdx + 0] = sourceData[srcIdx + 2]; // R
                    rgbaData[dstIdx + 1] = sourceData[srcIdx + 1]; // G
                    rgbaData[dstIdx + 2] = sourceData[srcIdx + 0]; // B
                    rgbaData[dstIdx + 3] = sourceData[srcIdx + 3]; // A
                }
            }
        }

        // 使用stb_image_write编码为PNG
        encodeSuccess = stbi_write_png_to_func(write_buffer, &encodedData,
                                                width, height, 4, rgbaData.data(), width * 4) != 0;
        mimeType = "image/png";
    } else {
        // 无Alpha通道，使用JPEG编码（更小的文件大小）
        std::vector<unsigned char> rgbData;
        rgbData.resize(width * height * 3);

        if (pixelFormat == GL_RGB) {
            if (hasRowPadding) {
                for (int row = 0; row < height; row++) {
                    memcpy(&rgbData[row * width * 3],
                           &sourceData[row * rowStep],
                           width * 3);
                }
            } else {
                memcpy(rgbData.data(), sourceData, width * height * 3);
            }
        } else if (pixelFormat == GL_RGBA || pixelFormat == GL_BGRA) {
            // Extract RGB from RGBA/BGRA
            int rOffset = (pixelFormat == GL_BGRA) ? 2 : 0;
            int bOffset = (pixelFormat == GL_BGRA) ? 0 : 2;
            for (int row = 0; row < height; row++) {
                for (int col = 0; col < width; col++) {
                    int srcIdx = row * rowStep + col * 4;
                    int dstIdx = (row * width + col) * 3;
                    rgbData[dstIdx + 0] = sourceData[srcIdx + rOffset];
                    rgbData[dstIdx + 1] = sourceData[srcIdx + 1];
                    rgbData[dstIdx + 2] = sourceData[srcIdx + bOffset];
                }
            }
        } else if (pixelFormat == GL_BGR) {
            // Convert BGR to RGB
            for (int row = 0; row < height; row++) {
                for (int col = 0; col < width; col++) {
                    int srcIdx = row * rowStep + col * 3;
                    int dstIdx = (row * width + col) * 3;
                    rgbData[dstIdx + 0] = sourceData[srcIdx + 2];
                    rgbData[dstIdx + 1] = sourceData[srcIdx + 1];
                    rgbData[dstIdx + 2] = sourceData[srcIdx + 0];
                }
            }
        }

        // 使用stb_image_write编码为JPEG，质量90
        encodeSuccess = stbi_write_jpg_to_func(write_buffer, &encodedData,
                                                width, height, 3, rgbData.data(), 90) != 0;
        mimeType = "image/jpeg";
    }

    if (!encodeSuccess || encodedData.empty()) {
        LOG_W("Failed to encode texture image");
        return;
    }

    // 创建buffer view
    int bufferViewIndex = static_cast<int>(model.bufferViews.size());
    int byteOffset = static_cast<int>(buffer.data.size());

    buffer.data.insert(buffer.data.end(), encodedData.begin(), encodedData.end());
    alignment_buffer(buffer.data);

    tinygltf::BufferView bfv;
    bfv.buffer = 0;
    bfv.byteOffset = byteOffset;
    bfv.byteLength = static_cast<int>(encodedData.size());
    model.bufferViews.push_back(bfv);

    // 创建image
    int imageIndex = static_cast<int>(model.images.size());
    tinygltf::Image img;
    img.bufferView = bufferViewIndex;
    img.mimeType = mimeType;
    model.images.push_back(img);

    // 创建sampler
    int samplerIndex = static_cast<int>(model.samplers.size());
    tinygltf::Sampler sampler;
    sampler.wrapS = TINYGLTF_TEXTURE_WRAP_REPEAT;
    sampler.wrapT = TINYGLTF_TEXTURE_WRAP_REPEAT;
    sampler.minFilter = TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR;
    sampler.magFilter = TINYGLTF_TEXTURE_FILTER_LINEAR;
    model.samplers.push_back(sampler);

    // 创建texture
    textureIndexOut = static_cast<int>(model.textures.size());
    tinygltf::Texture tex;
    tex.sampler = samplerIndex;
    tex.source = imageIndex;
    model.textures.push_back(tex);
}

int B3DMGenerator::buildMaterial(
    const std::shared_ptr<common::MaterialInfo>& matInfo,
    tinygltf::Model& model,
    tinygltf::Buffer& buffer) {

    if (!matInfo) {
        return -1;
    }

    // 计算材质键值，用于去重
    std::string materialKey = computeMaterialKey(matInfo);

    // 检查是否已有相同材质
    auto it = materialCache_.find(materialKey);
    if (it != materialCache_.end()) {
        return it->second;
    }

    // 使用MaterialBuilder构建材质
    gltf::MaterialBuilder builder;
    gltf::ExtensionManager extMgr;

    // 处理基础颜色纹理
    int baseColorTexIdx = -1;
    bool hasAlpha = false;
    if (matInfo->baseColorTexture) {
        processAndAddTexture(
            matInfo->baseColorTexture.get(),
            matInfo->baseColorTransform,
            TextureType::BASE_COLOR,
            model, buffer, baseColorTexIdx, hasAlpha
        );
    }

    // 设置基础PBR参数
    // 如果有纹理，baseColor应该设为白色（纹理提供颜色，baseColorFactor作为tint）
    if (baseColorTexIdx >= 0) {
        builder.setBaseColor({1.0, 1.0, 1.0, 1.0});  // 白色，不染色
    } else {
        builder.setBaseColor(matInfo->baseColor);  // 使用材质颜色
    }
    builder.setPBRParams(matInfo->roughnessFactor, matInfo->metallicFactor);
    builder.setDoubleSided(matInfo->doubleSided);
    builder.setAlphaMode(matInfo->alphaMode);
    builder.setAlphaCutoff(matInfo->alphaCutoff);

    // 设置基础颜色纹理
    if (baseColorTexIdx >= 0) {
        builder.setBaseColorTexture(baseColorTexIdx);
        // 应用纹理变换
        if (matInfo->baseColorTransform.hasTransform) {
            gltf::extensions::TextureTransform transform;
            transform.offset = {matInfo->baseColorTransform.offset[0], matInfo->baseColorTransform.offset[1]};
            transform.scale = {matInfo->baseColorTransform.scale[0], matInfo->baseColorTransform.scale[1]};
            transform.rotation = matInfo->baseColorTransform.rotation;
            transform.tex_coord = matInfo->baseColorTransform.texCoord;
            builder.setBaseColorTextureTransform(transform);
        }
        // 如果有Alpha通道，设置Alpha模式为BLEND
        if (hasAlpha) {
            builder.setAlphaMode("BLEND");
        }
    }

    // 处理金属度粗糙度纹理
    int metallicRoughnessTexIdx = -1;
    bool mrHasAlpha = false;
    if (matInfo->metallicRoughnessTexture) {
        processAndAddTexture(
            matInfo->metallicRoughnessTexture.get(),
            matInfo->metallicRoughnessTransform,
            TextureType::METALLIC_ROUGHNESS,
            model, buffer, metallicRoughnessTexIdx, mrHasAlpha
        );
        if (metallicRoughnessTexIdx >= 0) {
            builder.setMetallicRoughnessTexture(metallicRoughnessTexIdx);
            if (matInfo->metallicRoughnessTransform.hasTransform) {
                gltf::extensions::TextureTransform transform;
                transform.offset = {matInfo->metallicRoughnessTransform.offset[0], matInfo->metallicRoughnessTransform.offset[1]};
                transform.scale = {matInfo->metallicRoughnessTransform.scale[0], matInfo->metallicRoughnessTransform.scale[1]};
                transform.rotation = matInfo->metallicRoughnessTransform.rotation;
                transform.tex_coord = matInfo->metallicRoughnessTransform.texCoord;
                builder.setMetallicRoughnessTextureTransform(transform);
            }
        }
    }

    // 处理法线纹理
    int normalTexIdx = -1;
    bool normalHasAlpha = false;
    if (matInfo->normalTexture) {
        processAndAddTexture(
            matInfo->normalTexture.get(),
            matInfo->normalTransform,
            TextureType::NORMAL,
            model, buffer, normalTexIdx, normalHasAlpha
        );
        if (normalTexIdx >= 0) {
            builder.setNormalTexture(normalTexIdx);
            if (matInfo->normalTransform.hasTransform) {
                gltf::extensions::TextureTransform transform;
                transform.offset = {matInfo->normalTransform.offset[0], matInfo->normalTransform.offset[1]};
                transform.scale = {matInfo->normalTransform.scale[0], matInfo->normalTransform.scale[1]};
                transform.rotation = matInfo->normalTransform.rotation;
                transform.tex_coord = matInfo->normalTransform.texCoord;
                builder.setNormalTextureTransform(transform);
            }
        }
    }

    // 处理环境光遮蔽纹理
    int occlusionTexIdx = -1;
    bool occlusionHasAlpha = false;
    if (matInfo->occlusionTexture) {
        processAndAddTexture(
            matInfo->occlusionTexture.get(),
            matInfo->occlusionTransform,
            TextureType::OCCLUSION,
            model, buffer, occlusionTexIdx, occlusionHasAlpha
        );
        if (occlusionTexIdx >= 0) {
            builder.setOcclusionTexture(occlusionTexIdx);
            builder.setOcclusionStrength(matInfo->aoStrength);
            if (matInfo->occlusionTransform.hasTransform) {
                gltf::extensions::TextureTransform transform;
                transform.offset = {matInfo->occlusionTransform.offset[0], matInfo->occlusionTransform.offset[1]};
                transform.scale = {matInfo->occlusionTransform.scale[0], matInfo->occlusionTransform.scale[1]};
                transform.rotation = matInfo->occlusionTransform.rotation;
                transform.tex_coord = matInfo->occlusionTransform.texCoord;
                builder.setOcclusionTextureTransform(transform);
            }
        }
    }

    // 处理自发光纹理和颜色
    int emissiveTexIdx = -1;
    bool emissiveHasAlpha = false;
    if (matInfo->emissiveTexture) {
        processAndAddTexture(
            matInfo->emissiveTexture.get(),
            matInfo->emissiveTransform,
            TextureType::EMISSIVE,
            model, buffer, emissiveTexIdx, emissiveHasAlpha
        );
        if (emissiveTexIdx >= 0) {
            builder.setEmissiveTexture(emissiveTexIdx);
            if (matInfo->emissiveTransform.hasTransform) {
                gltf::extensions::TextureTransform transform;
                transform.offset = {matInfo->emissiveTransform.offset[0], matInfo->emissiveTransform.offset[1]};
                transform.scale = {matInfo->emissiveTransform.scale[0], matInfo->emissiveTransform.scale[1]};
                transform.rotation = matInfo->emissiveTransform.rotation;
                transform.tex_coord = matInfo->emissiveTransform.texCoord;
                builder.setEmissiveTextureTransform(transform);
            }
        }
    }
    if (!matInfo->emissiveColor.empty()) {
        builder.setEmissiveColor(matInfo->emissiveColor);
    }

    // 处理Specular-Glossiness
    if (matInfo->useSpecularGlossiness) {
        gltf::extensions::SpecularGlossiness sg;
        sg.diffuse_factor = {matInfo->diffuseFactor[0], matInfo->diffuseFactor[1],
                             matInfo->diffuseFactor[2], matInfo->diffuseFactor[3]};
        sg.specular_factor = {matInfo->specularFactor[0], matInfo->specularFactor[1],
                              matInfo->specularFactor[2]};
        sg.glossiness_factor = matInfo->glossinessFactor;
        builder.setSpecularGlossiness(sg);
    }

    // 构建材质
    int materialIndex = builder.build(model, extMgr);

    // 缓存材质索引
    materialCache_[materialKey] = materialIndex;

    return materialIndex;
}

} // namespace b3dm

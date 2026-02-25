#include "fbx_geometry_extractor.h"
#include "fbx_spatial_item_adapter.h"
#include "../extern.h"

namespace fbx {

std::vector<osg::ref_ptr<osg::Geometry>> FBXGeometryExtractor::extract(
    const spatial::core::SpatialItem* item) {

    std::vector<osg::ref_ptr<osg::Geometry>> result;

    // 尝试转换为FBXSpatialItemAdapter
    const auto* fbxItem = dynamic_cast<const FBXSpatialItemAdapter*>(item);
    if (!fbxItem) {
        LOG_W("FBXGeometryExtractor: item is not FBXSpatialItemAdapter");
        return result;
    }

    // 获取几何体
    const osg::Geometry* geom = fbxItem->getGeometry();
    if (!geom) {
        LOG_W("FBXGeometryExtractor: no geometry found for %s", fbxItem->getNodeName().c_str());
        return result;
    }

    // 检查原始几何体的顶点数组
    const osg::Array* vertexArray = geom->getVertexArray();
    if (!vertexArray || vertexArray->getNumElements() == 0) {
        return result;
    }

    // 克隆几何体
    osg::ref_ptr<osg::Geometry> clonedGeom = static_cast<osg::Geometry*>(
        geom->clone(osg::CopyOp::DEEP_COPY_ALL)
    );

    // 应用世界变换到顶点
    osg::Matrixd transform = fbxItem->getTransform();

    // 创建Y-up到Z-up的坐标转换矩阵
    // FBX是Y-up，3D Tiles是Z-up
    // 转换: (x, y, z) -> (x, -z, y)
    // 注意: OSG使用行主序矩阵，但构造时是列主序
    osg::Matrixd yupToZup(
        1,  0,  0, 0,   // 第一列: x' = 1*x + 0*y + 0*z
        0,  0,  1, 0,   // 第二列: y' = 0*x + 0*y + 1*z = z
        0, -1,  0, 0,   // 第三列: z' = 0*x - 1*y + 0*z = -y
        0,  0,  0, 1
    );

    // 组合变换: 先应用世界变换，再Y-up到Z-up
    // OSG是右乘: v' = v * transform * yupToZup
    osg::Matrixd finalTransform = transform * yupToZup;

    // 处理不同类型的顶点数组 (Vec3Array 或 Vec3dArray)
    osg::Vec3Array* vertices = dynamic_cast<osg::Vec3Array*>(clonedGeom->getVertexArray());
    osg::Vec3dArray* verticesd = dynamic_cast<osg::Vec3dArray*>(clonedGeom->getVertexArray());

    if (vertices) {
        // 单精度顶点数组
        for (auto& vertex : *vertices) {
            vertex = vertex * finalTransform;
        }
        vertices->dirty();
    } else if (verticesd) {
        // 双精度顶点数组
        for (auto& vertex : *verticesd) {
            vertex = vertex * finalTransform;
        }
        verticesd->dirty();
    } else {
        return result;
    }

    // 变换法线 (使用finalTransform)
    osg::Matrixd normalMatrix = osg::Matrixd::inverse(finalTransform);
    normalMatrix.transpose3x3(normalMatrix);

    osg::Vec3Array* normals = dynamic_cast<osg::Vec3Array*>(clonedGeom->getNormalArray());
    if (normals) {
        for (auto& normal : *normals) {
            normal = osg::Matrixd::transform3x3(normal, normalMatrix);
            normal.normalize();
        }
        normals->dirty();
    }

    result.push_back(clonedGeom);

    return result;
}

std::string FBXGeometryExtractor::getId(const spatial::core::SpatialItem* item) {
    const auto* fbxItem = dynamic_cast<const FBXSpatialItemAdapter*>(item);
    if (!fbxItem) {
        return "";
    }

    return fbxItem->getNodeName();
}

std::map<std::string, nlohmann::json> FBXGeometryExtractor::getAttributes(
    const spatial::core::SpatialItem* item) {

    std::map<std::string, nlohmann::json> attrs;

    const auto* fbxItem = dynamic_cast<const FBXSpatialItemAdapter*>(item);
    if (!fbxItem) {
        return attrs;
    }

    // 添加节点名称
    attrs["name"] = fbxItem->getNodeName();

    // 可以添加更多属性...

    return attrs;
}

} // namespace fbx

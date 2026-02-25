#include "geometry_utils.h"
#include "../../extern.h"

#include <osg/Vec2>
#include <osg/Vec3>
#include <osg/Vec4>
#include <osg/Array>
#include <algorithm>

namespace gltf {
namespace utils {

osg::Matrixd GeometryUtils::computeNormalMatrix(const osg::Matrixd& matrix) {
    osg::Matrixd normalMatrix = matrix;
    normalMatrix.setTrans(0.0, 0.0, 0.0);
    normalMatrix.invert(normalMatrix);
    normalMatrix.transpose(normalMatrix);
    return normalMatrix;
}

osg::Vec3d GeometryUtils::transformVertex(const osg::Vec3d& vertex, const osg::Matrixd& matrix) {
    osg::Vec3d p = vertex * matrix;
    // Y-up to Z-up: x' = x, y' = -z, z' = y
    return osg::Vec3d(p.x(), -p.z(), p.y());
}

osg::Vec3d GeometryUtils::transformNormal(const osg::Vec3d& normal, const osg::Matrixd& normalMatrix) {
    osg::Vec3d nm = osg::Matrix::transform3x3(normal, normalMatrix);
    nm.normalize();
    // Y-up to Z-up: x' = x, y' = -z, z' = y
    return osg::Vec3d(nm.x(), -nm.z(), nm.y());
}

size_t GeometryUtils::extractGeometryData(
    const osg::Geometry* geom,
    const osg::Matrixd& matrix,
    const osg::Matrixd& normalMatrix,
    std::vector<float>& outPositions,
    std::vector<float>& outNormals,
    std::vector<float>& outTexcoords,
    size_t baseIndex) {

    if (!geom) return 0;

    const osg::Array* va = geom->getVertexArray();
    if (!va || va->getNumElements() == 0) return 0;

    const osg::Array* na = geom->getNormalArray();
    const osg::Array* ta = geom->getTexCoordArray(0);

    size_t vertexCount = va->getNumElements();
    size_t startIdx = outPositions.size() / 3;

    // 预分配空间
    outPositions.reserve(outPositions.size() + vertexCount * 3);
    outNormals.reserve(outNormals.size() + vertexCount * 3);
    outTexcoords.reserve(outTexcoords.size() + vertexCount * 2);

    // 根据数组类型提取数据
    const osg::Vec3Array* v3 = dynamic_cast<const osg::Vec3Array*>(va);
    const osg::Vec4Array* v4 = dynamic_cast<const osg::Vec4Array*>(va);
    const osg::Vec3dArray* v3d = dynamic_cast<const osg::Vec3dArray*>(va);
    const osg::Vec4dArray* v4d = dynamic_cast<const osg::Vec4dArray*>(va);

    const osg::Vec3Array* n = dynamic_cast<const osg::Vec3Array*>(na);
    const osg::Vec3dArray* n3d = dynamic_cast<const osg::Vec3dArray*>(na);

    const osg::Vec2Array* t = dynamic_cast<const osg::Vec2Array*>(ta);
    const osg::Vec2dArray* t2d = dynamic_cast<const osg::Vec2dArray*>(ta);

    // 检查是否需要使用泛型数组处理
    bool useGenericVertexArray = (!v3 || v3->empty()) && (!v4 || v4->empty()) &&
                                  (!v3d || v3d->empty()) && (!v4d || v4d->empty());

    for (size_t i = 0; i < vertexCount; ++i) {
        // 提取顶点
        osg::Vec3d p;
        if (v3 && i < v3->size()) {
            p = (*v3)[i];
        } else if (v4 && i < v4->size()) {
            osg::Vec4 vf = (*v4)[i];
            p = osg::Vec3d(vf.x(), vf.y(), vf.z());
        } else if (v3d && i < v3d->size()) {
            p = (*v3d)[i];
        } else if (v4d && i < v4d->size()) {
            osg::Vec4d vd = (*v4d)[i];
            p = osg::Vec3d(vd.x(), vd.y(), vd.z());
        } else if (useGenericVertexArray) {
            // 泛型数组处理
            GLenum dt = va->getDataType();
            unsigned int cnt = va->getNumElements();
            unsigned int totalBytes = va->getTotalDataSize();
            if (dt == GL_FLOAT || dt == GL_DOUBLE) {
                unsigned int comps = (dt == GL_FLOAT) ?
                    totalBytes / (cnt * sizeof(float)) :
                    totalBytes / (cnt * sizeof(double));
                if (comps >= 3) {
                    if (dt == GL_FLOAT) {
                        const float* ptr = static_cast<const float*>(va->getDataPointer());
                        p = osg::Vec3d((double)ptr[i*comps+0], (double)ptr[i*comps+1], (double)ptr[i*comps+2]);
                    } else {
                        const double* ptr = static_cast<const double*>(va->getDataPointer());
                        p = osg::Vec3d(ptr[i*comps+0], ptr[i*comps+1], ptr[i*comps+2]);
                    }
                } else {
                    continue;
                }
            } else {
                continue;
            }
        } else {
            continue;
        }

        // 变换顶点
        osg::Vec3d tp = transformVertex(p, matrix);
        outPositions.push_back(static_cast<float>(tp.x()));
        outPositions.push_back(static_cast<float>(tp.y()));
        outPositions.push_back(static_cast<float>(tp.z()));

        // 提取并变换法线
        osg::Vec3d nm(0.0, 0.0, 1.0);
        if (n && i < n->size()) {
            nm = (*n)[i];
        } else if (n3d && i < n3d->size()) {
            nm = (*n3d)[i];
        } else if (na && i < na->getNumElements()) {
            // 泛型法线数组处理
            GLenum ndt = na->getDataType();
            unsigned int ncnt = na->getNumElements();
            unsigned int nbytes = na->getTotalDataSize();
            unsigned int ncomps = (ndt == GL_FLOAT) ?
                nbytes / (ncnt * sizeof(float)) :
                (ndt == GL_DOUBLE) ? nbytes / (ncnt * sizeof(double)) : 0;
            if (ncomps >= 3) {
                if (ndt == GL_FLOAT) {
                    const float* nptr = static_cast<const float*>(na->getDataPointer());
                    nm = osg::Vec3d((double)nptr[i*ncomps+0], (double)nptr[i*ncomps+1], (double)nptr[i*ncomps+2]);
                } else if (ndt == GL_DOUBLE) {
                    const double* nptr = static_cast<const double*>(na->getDataPointer());
                    nm = osg::Vec3d(nptr[i*ncomps+0], nptr[i*ncomps+1], nptr[i*ncomps+2]);
                }
            }
        }
        osg::Vec3d tnm = transformNormal(nm, normalMatrix);
        outNormals.push_back(static_cast<float>(tnm.x()));
        outNormals.push_back(static_cast<float>(tnm.y()));
        outNormals.push_back(static_cast<float>(tnm.z()));

        // 提取纹理坐标
        float u = 0.0f, v = 0.0f;
        if (t && i < t->size()) {
            u = (*t)[i].x();
            v = (*t)[i].y();
        } else if (t2d && i < t2d->size()) {
            u = static_cast<float>((*t2d)[i].x());
            v = static_cast<float>((*t2d)[i].y());
        } else if (ta && i < ta->getNumElements()) {
            // 处理泛型数组
            GLenum tdt = ta->getDataType();
            unsigned int tcnt = ta->getNumElements();
            unsigned int tbytes = ta->getTotalDataSize();
            unsigned int tcomps = (tdt == GL_FLOAT) ?
                tbytes / (tcnt * sizeof(float)) :
                (tdt == GL_DOUBLE) ? tbytes / (tcnt * sizeof(double)) : 0;

            if (tcomps >= 2) {
                if (tdt == GL_FLOAT) {
                    const float* tptr = static_cast<const float*>(ta->getDataPointer());
                    u = tptr[i * tcomps + 0];
                    v = tptr[i * tcomps + 1];
                } else if (tdt == GL_DOUBLE) {
                    const double* tptr = static_cast<const double*>(ta->getDataPointer());
                    u = static_cast<float>(tptr[i * tcomps + 0]);
                    v = static_cast<float>(tptr[i * tcomps + 1]);
                }
            }
        }
        outTexcoords.push_back(u);
        outTexcoords.push_back(v);
    }

    return outPositions.size() / 3 - startIdx;
}

size_t GeometryUtils::processPrimitiveSet(
    const osg::PrimitiveSet* ps,
    uint32_t baseIndex,
    std::vector<uint32_t>& outIndices) {

    if (!ps) return 0;

    osg::PrimitiveSet::Mode mode = static_cast<osg::PrimitiveSet::Mode>(ps->getMode());

    // 只处理三角形相关图元
    if (mode != osg::PrimitiveSet::TRIANGLES &&
        mode != osg::PrimitiveSet::TRIANGLE_STRIP &&
        mode != osg::PrimitiveSet::TRIANGLE_FAN) {
        return 0;
    }

    const osg::DrawArrays* da = dynamic_cast<const osg::DrawArrays*>(ps);
    const osg::DrawElementsUShort* deus = dynamic_cast<const osg::DrawElementsUShort*>(ps);
    const osg::DrawElementsUInt* deui = dynamic_cast<const osg::DrawElementsUInt*>(ps);

    if (da) {
        return processDrawArrays(da, baseIndex, outIndices);
    } else if (deus) {
        return processDrawElementsUShort(deus, baseIndex, outIndices);
    } else if (deui) {
        return processDrawElementsUInt(deui, baseIndex, outIndices);
    }

    return 0;
}

size_t GeometryUtils::processDrawArrays(
    const osg::DrawArrays* da,
    uint32_t baseIndex,
    std::vector<uint32_t>& outIndices) {

    unsigned int first = da->getFirst();
    unsigned int count = da->getCount();
    osg::PrimitiveSet::Mode mode = static_cast<osg::PrimitiveSet::Mode>(da->getMode());
    size_t triangleCount = 0;

    if (mode == osg::PrimitiveSet::TRIANGLES) {
        for (unsigned int idx = 0; idx + 2 < count; idx += 3) {
            outIndices.push_back(baseIndex + first + idx);
            outIndices.push_back(baseIndex + first + idx + 1);
            outIndices.push_back(baseIndex + first + idx + 2);
            triangleCount++;
        }
    } else if (mode == osg::PrimitiveSet::TRIANGLE_STRIP) {
        for (unsigned int i = 0; i + 2 < count; ++i) {
            unsigned int a = baseIndex + first + i;
            unsigned int b = baseIndex + first + i + 1;
            unsigned int c = baseIndex + first + i + 2;
            if ((i & 1) == 0) {
                outIndices.push_back(a);
                outIndices.push_back(b);
                outIndices.push_back(c);
            } else {
                outIndices.push_back(b);
                outIndices.push_back(a);
                outIndices.push_back(c);
            }
            triangleCount++;
        }
    } else if (mode == osg::PrimitiveSet::TRIANGLE_FAN) {
        unsigned int center = baseIndex + first;
        for (unsigned int i = 1; i + 1 < count; ++i) {
            outIndices.push_back(center);
            outIndices.push_back(baseIndex + first + i);
            outIndices.push_back(baseIndex + first + i + 1);
            triangleCount++;
        }
    }

    return triangleCount;
}

size_t GeometryUtils::processDrawElementsUShort(
    const osg::DrawElementsUShort* deus,
    uint32_t baseIndex,
    std::vector<uint32_t>& outIndices) {

    size_t count = deus->size();
    osg::PrimitiveSet::Mode mode = static_cast<osg::PrimitiveSet::Mode>(deus->getMode());
    size_t triangleCount = 0;

    if (mode == osg::PrimitiveSet::TRIANGLES) {
        for (size_t idx = 0; idx < count; ++idx) {
            outIndices.push_back(baseIndex + (*deus)[idx]);
        }
        triangleCount = count / 3;
    } else if (mode == osg::PrimitiveSet::TRIANGLE_STRIP && count >= 3) {
        for (size_t i = 0; i + 2 < count; ++i) {
            unsigned int a = baseIndex + (*deus)[i];
            unsigned int b = baseIndex + (*deus)[i + 1];
            unsigned int c = baseIndex + (*deus)[i + 2];
            if ((i & 1) == 0) {
                outIndices.push_back(a);
                outIndices.push_back(b);
                outIndices.push_back(c);
            } else {
                outIndices.push_back(b);
                outIndices.push_back(a);
                outIndices.push_back(c);
            }
            triangleCount++;
        }
    } else if (mode == osg::PrimitiveSet::TRIANGLE_FAN && count >= 3) {
        unsigned int center = baseIndex + (*deus)[0];
        for (size_t i = 1; i + 1 < count; ++i) {
            outIndices.push_back(center);
            outIndices.push_back(baseIndex + (*deus)[i]);
            outIndices.push_back(baseIndex + (*deus)[i + 1]);
            triangleCount++;
        }
    }

    return triangleCount;
}

size_t GeometryUtils::processDrawElementsUInt(
    const osg::DrawElementsUInt* deui,
    uint32_t baseIndex,
    std::vector<uint32_t>& outIndices) {

    size_t count = deui->size();
    osg::PrimitiveSet::Mode mode = static_cast<osg::PrimitiveSet::Mode>(deui->getMode());
    size_t triangleCount = 0;

    if (mode == osg::PrimitiveSet::TRIANGLES) {
        for (size_t idx = 0; idx < count; ++idx) {
            outIndices.push_back(baseIndex + (*deui)[idx]);
        }
        triangleCount = count / 3;
    } else if (mode == osg::PrimitiveSet::TRIANGLE_STRIP && count >= 3) {
        for (size_t i = 0; i + 2 < count; ++i) {
            unsigned int a = baseIndex + (*deui)[i];
            unsigned int b = baseIndex + (*deui)[i + 1];
            unsigned int c = baseIndex + (*deui)[i + 2];
            if ((i & 1) == 0) {
                outIndices.push_back(a);
                outIndices.push_back(b);
                outIndices.push_back(c);
            } else {
                outIndices.push_back(b);
                outIndices.push_back(a);
                outIndices.push_back(c);
            }
            triangleCount++;
        }
    } else if (mode == osg::PrimitiveSet::TRIANGLE_FAN && count >= 3) {
        unsigned int center = baseIndex + (*deui)[0];
        for (size_t i = 1; i + 1 < count; ++i) {
            outIndices.push_back(center);
            outIndices.push_back(baseIndex + (*deui)[i]);
            outIndices.push_back(baseIndex + (*deui)[i + 1]);
            triangleCount++;
        }
    }

    return triangleCount;
}

} // namespace utils
} // namespace gltf

#pragma once

#include "coordinate_system.h"
#include <ogr_spatialref.h>
#include <memory>
#include <vector>
#include <glm/glm.hpp>

namespace coords {

// 转换模式
enum class TransformMode {
    None,               // 无地理转换（OSGB→GLTF场景）
    WithGeoReference    // 带地理参考转换（3D Tiles场景）
};

// 坐标转换器
// 负责将源坐标系转换到目标坐标系(ENU局部坐标)
// 实例类型，每个实例维护独立的转换状态，线程安全
class CoordinateTransformer {
public:
    // 模式1：无地理转换（纯格式转换，如OSGB→GLTF）
    // 仅支持轴方向转换
    explicit CoordinateTransformer(const CoordinateSystem& cs);

    // 模式2：带地理参考转换（3D Tiles场景）
    // 支持完整的坐标转换链
    CoordinateTransformer(const CoordinateSystem& cs, const GeoReference& geo_ref);

    ~CoordinateTransformer();

    // 禁止拷贝，允许移动
    CoordinateTransformer(const CoordinateTransformer&) = delete;
    CoordinateTransformer& operator=(const CoordinateTransformer&) = delete;
    CoordinateTransformer(CoordinateTransformer&&) noexcept;
    CoordinateTransformer& operator=(CoordinateTransformer&&) noexcept;

    // ----- 模式查询 -----

    // 获取转换模式
    TransformMode GetMode() const { return mode_; }

    // 是否有地理参考
    bool HasGeoReference() const { return mode_ == TransformMode::WithGeoReference; }

    // ----- 坐标转换（仅HasGeoReference时有效）-----

    // 转换到WGS84地理坐标(经度, 纬度, 高度)
    glm::dvec3 ToWGS84(const glm::dvec3& point) const;

    // 转换到ECEF地心地固坐标
    glm::dvec3 ToECEF(const glm::dvec3& point) const;

    // 转换到ENU局部坐标(东, 北, 天)
    // 这是3D Tiles使用的坐标系统
    glm::dvec3 ToLocalENU(const glm::dvec3& point) const;

    // 批量转换
    void TransformToWGS84(std::vector<glm::dvec3>& points) const;
    void TransformToLocalENU(std::vector<glm::dvec3>& points) const;

    // ----- 轴方向转换（所有模式都可用）-----

    // 转换轴方向
    // 例如：Z-Up → Y-Up (OSGB → glTF)
    glm::dvec3 ConvertUpAxis(const glm::dvec3& point,
                             UpAxis target_axis = UpAxis::Y_UP) const;

    // ----- 矩阵访问 -----

    // 获取ENU到ECEF的转换矩阵
    const glm::dmat4& GetEnuToEcefMatrix() const { return enu_to_ecef_; }

    // 获取ECEF到ENU的转换矩阵
    const glm::dmat4& GetEcefToEnuMatrix() const { return ecef_to_enu_; }

    // ----- 原点信息 -----

    // 获取地理原点经度(度)
    double GeoOriginLon() const { return geo_origin_lon_; }

    // 获取地理原点纬度(度)
    double GeoOriginLat() const { return geo_origin_lat_; }

    // 获取地理原点高度(米, 椭球高)
    double GeoOriginHeight() const { return geo_origin_height_; }

    // ----- 静态工具方法 -----

    // 计算ENU到ECEF的转换矩阵
    // 基于WGS84椭球参数
    static glm::dmat4 CalcEnuToEcefMatrix(double lon_deg, double lat_deg, double height);

    // 将地理坐标(经度, 纬度, 高度)转换为ECEF坐标
    // 基于WGS84椭球参数
    static glm::dvec3 CartographicToEcef(double lon_deg, double lat_deg, double height);

    // 获取轴方向转换矩阵
    static glm::dmat4 GetAxisTransformMatrix(UpAxis from, UpAxis to);

private:
    // 使用地理参考初始化
    void InitializeWithGeoRef(const GeoReference& geo_ref);

    // 创建OGR坐标转换器
    void CreateOGRTransform();

    CoordinateSystem source_cs_;            // 源坐标系
    TransformMode mode_ = TransformMode::None;  // 转换模式

    // 地理原点(WGS84)
    double geo_origin_lon_ = 0.0;           // 经度(度)
    double geo_origin_lat_ = 0.0;           // 纬度(度)
    double geo_origin_height_ = 0.0;        // 高度(米, 椭球高)

    // 转换矩阵
    glm::dmat4 enu_to_ecef_{1.0};           // ENU → ECEF
    glm::dmat4 ecef_to_enu_{1.0};           // ECEF → ENU
    glm::dmat4 axis_transform_{1.0};        // 轴方向转换

    // OGR坐标转换器(用于EPSG/WKT类型)
    struct OGRCTDeleter {
        void operator()(OGRCoordinateTransformation* pCT) const {
            if (pCT != nullptr) {
                OGRCoordinateTransformation::DestroyCT(pCT);
            }
        }
    };
    std::unique_ptr<OGRCoordinateTransformation, OGRCTDeleter> ogr_transform_;
};

} // namespace coords

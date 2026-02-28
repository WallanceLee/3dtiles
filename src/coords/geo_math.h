#pragma once

/**
 * @file geo_math.h
 * @brief 地理数学工具函数
 *
 * 提供常用的地理坐标转换数学函数
 * 替代 extern.h 中的全局函数
 */

#include <cmath>

namespace coords {

// 数学常量
constexpr double PI = 3.14159265358979323846;
constexpr double DEG_TO_RAD = PI / 180.0;
constexpr double RAD_TO_DEG = 180.0 / PI;

// WGS84 椭球参数
constexpr double WGS84_A = 6378137.0;                    // 长半轴 (米)
constexpr double WGS84_F = 1.0 / 298.257223563;          // 扁率
constexpr double WGS84_E2 = 2 * WGS84_F - WGS84_F * WGS84_F;  // 第一偏心率平方

/**
 * @brief 角度转弧度
 */
inline double degree2rad(double degrees) {
    return degrees * DEG_TO_RAD;
}

/**
 * @brief 弧度转角度
 */
inline double rad2degree(double radians) {
    return radians * RAD_TO_DEG;
}

/**
 * @brief 纬度差转米（近似）
 * @param diff 纬度差（弧度）
 * @return 对应的距离（米）
 */
inline double lati_to_meter(double diff) {
    return diff * WGS84_A;
}

/**
 * @brief 米转纬度差（近似）
 * @param meters 距离（米）
 * @return 对应的纬度差（弧度）
 */
inline double meter_to_lati(double meters) {
    return meters / WGS84_A;
}

/**
 * @brief 经度差转米（近似，与纬度相关）
 * @param diff 经度差（弧度）
 * @param lati 纬度（弧度）
 * @return 对应的距离（米）
 */
inline double longti_to_meter(double diff, double lati) {
    return diff * WGS84_A * std::cos(lati);
}

/**
 * @brief 米转经度差（近似，与纬度相关）
 * @param meters 距离（米）
 * @param lati 纬度（弧度）
 * @return 对应的经度差（弧度）
 */
inline double meter_to_longti(double meters, double lati) {
    return meters / (WGS84_A * std::cos(lati));
}

/**
 * @brief 计算 WGS84 椭球上的子午线曲率半径
 * @param lat 纬度（弧度）
 * @return 曲率半径（米）
 */
inline double meridian_radius(double lat) {
    double sin_lat = std::sin(lat);
    return WGS84_A * (1 - WGS84_E2) /
           std::pow(1 - WGS84_E2 * sin_lat * sin_lat, 1.5);
}

/**
 * @brief 计算 WGS84 椭球上的卯酉圈曲率半径
 * @param lat 纬度（弧度）
 * @return 曲率半径（米）
 */
inline double prime_vertical_radius(double lat) {
    double sin_lat = std::sin(lat);
    return WGS84_A / std::sqrt(1 - WGS84_E2 * sin_lat * sin_lat);
}

} // namespace coords

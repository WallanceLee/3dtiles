#pragma once

/**
 * @file common/geometry_extractor_base.h
 * @brief 几何提取器模板基类
 *
 * 消除不同数据源 GeometryExtractor 实现中的重复代码
 * 使用 CRTP 模式实现编译时多态
 */

#include "geometry_extractor.h"
#include "../spatial/core/spatial_item.h"
#include <osg/Geometry>
#include <vector>
#include <memory>
#include <string>
#include <map>
#include <nlohmann/json.hpp>

namespace common {

/**
 * @brief 几何提取器模板基类
 *
 * @tparam Derived 派生类类型（CRTP 模式）
 * @tparam AdapterType 空间对象适配器类型
 * @tparam DataType 原始数据类型（适配器内部包装的数据）
 *
 * 使用示例:
 * @code
 * class FBXGeometryExtractor : public GeometryExtractorBase<
 *     FBXGeometryExtractor,
 *     FBXSpatialItemAdapter,
 *     FBXNodeData> {
 * public:
 *     std::vector<osg::ref_ptr<osg::Geometry>> extractImpl(const FBXNodeData& data) override {
 *         // 实现几何提取逻辑
 *     }
 *     // ... 其他虚函数实现
 * };
 * @endcode
 */
template<typename Derived, typename AdapterType, typename DataType>
class GeometryExtractorBase : public IGeometryExtractor {
public:
    ~GeometryExtractorBase() override = default;

    /**
     * @brief 提取几何体
     *
     * 模板方法：处理类型转换，调用派生类的 extractImpl
     */
    std::vector<osg::ref_ptr<osg::Geometry>> extract(
        const spatial::core::SpatialItem* item) override {

        const auto* adapter = dynamic_cast<const AdapterType*>(item);
        if (!adapter) {
            return onInvalidItem(item, "extract");
        }

        // 调用派生类的 getData 方法获取数据
        auto data = static_cast<Derived*>(this)->getData(adapter);
        return static_cast<Derived*>(this)->extractImpl(data);
    }

    /**
     * @brief 获取对象ID
     *
     * 模板方法：处理类型转换，调用派生类的 getIdImpl
     */
    std::string getId(const spatial::core::SpatialItem* item) override {
        const auto* adapter = dynamic_cast<const AdapterType*>(item);
        if (!adapter) {
            return onInvalidItemForId(item);
        }

        auto data = static_cast<Derived*>(this)->getData(adapter);
        return static_cast<Derived*>(this)->getIdImpl(data, static_cast<Derived*>(this)->getItemId(adapter));
    }

    /**
     * @brief 获取对象属性
     *
     * 模板方法：处理类型转换，调用派生类的 getAttributesImpl
     */
    std::map<std::string, nlohmann::json> getAttributes(
        const spatial::core::SpatialItem* item) override {

        const auto* adapter = dynamic_cast<const AdapterType*>(item);
        if (!adapter) {
            return onInvalidItemForAttributes(item);
        }

        auto data = static_cast<Derived*>(this)->getData(adapter);
        return static_cast<Derived*>(this)->getAttributesImpl(data);
    }

    /**
     * @brief 获取材质信息
     *
     * 模板方法：处理类型转换，调用派生类的 getMaterialImpl
     */
    std::shared_ptr<MaterialInfo> getMaterial(
        const spatial::core::SpatialItem* item) override {

        const auto* adapter = dynamic_cast<const AdapterType*>(item);
        if (!adapter) {
            return onInvalidItemForMaterial(item);
        }

        auto data = static_cast<Derived*>(this)->getData(adapter);
        return static_cast<Derived*>(this)->getMaterialImpl(data);
    }

protected:
    /**
     * @brief 获取适配器中的ID
     *
     * 默认实现调用 adapter->getId()，派生类可以重写
     */
    size_t getItemId(const AdapterType* adapter) {
        return adapter->getId();
    }

    /**
     * @brief 处理无效的空间对象（类型转换失败）
     *
     * 派生类可以重写此方法以自定义错误处理
     */
    virtual std::vector<osg::ref_ptr<osg::Geometry>> onInvalidItem(
        const spatial::core::SpatialItem* item,
        const std::string& operation) {
        (void)item;
        (void)operation;
        return {};
    }

    /**
     * @brief 处理无效的空间对象（getId 操作）
     */
    virtual std::string onInvalidItemForId(const spatial::core::SpatialItem* item) {
        (void)item;
        return "";
    }

    /**
     * @brief 处理无效的空间对象（getAttributes 操作）
     */
    virtual std::map<std::string, nlohmann::json> onInvalidItemForAttributes(
        const spatial::core::SpatialItem* item) {
        (void)item;
        return {};
    }

    /**
     * @brief 处理无效的空间对象（getMaterial 操作）
     */
    virtual std::shared_ptr<MaterialInfo> onInvalidItemForMaterial(
        const spatial::core::SpatialItem* item) {
        (void)item;
        return std::make_shared<MaterialInfo>();
    }
};

/**
 * @brief 简化版几何提取器基类
 *
 * 适用于 Adapter 直接提供数据访问的场景
 *
 * @tparam Derived 派生类类型
 * @tparam AdapterType 适配器类型（需要有 getData(), getId() 方法）
 */
template<typename Derived, typename AdapterType>
class SimpleGeometryExtractor : public IGeometryExtractor {
public:
    ~SimpleGeometryExtractor() override = default;

    std::vector<osg::ref_ptr<osg::Geometry>> extract(
        const spatial::core::SpatialItem* item) override {

        const auto* adapter = dynamic_cast<const AdapterType*>(item);
        if (!adapter) {
            return {};
        }

        return static_cast<Derived*>(this)->extractFromAdapter(adapter);
    }

    std::string getId(const spatial::core::SpatialItem* item) override {
        const auto* adapter = dynamic_cast<const AdapterType*>(item);
        if (!adapter) {
            return "";
        }

        return static_cast<Derived*>(this)->getIdFromAdapter(adapter);
    }

    std::map<std::string, nlohmann::json> getAttributes(
        const spatial::core::SpatialItem* item) override {

        const auto* adapter = dynamic_cast<const AdapterType*>(item);
        if (!adapter) {
            return {};
        }

        return static_cast<Derived*>(this)->getAttributesFromAdapter(adapter);
    }

    std::shared_ptr<MaterialInfo> getMaterial(
        const spatial::core::SpatialItem* item) override {

        const auto* adapter = dynamic_cast<const AdapterType*>(item);
        if (!adapter) {
            return std::make_shared<MaterialInfo>();
        }

        return static_cast<Derived*>(this)->getMaterialFromAdapter(adapter);
    }
};

/**
 * @brief 默认材质提取器
 *
 * 适用于不包含材质信息的数据源（如 Shapefile）
 */
class DefaultMaterialExtractor {
public:
    std::shared_ptr<MaterialInfo> getMaterial(
        const spatial::core::SpatialItem* item) {
        (void)item;
        return std::make_shared<MaterialInfo>();
    }
};

} // namespace common

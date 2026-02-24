#pragma once

#include "spatial_bounds.h"
#include <memory>
#include <vector>

namespace spatial::core {

/**
 * @brief 空间对象接口
 *
 * 定义了可以被空间索引管理的基本接口
 * 业务数据需要继承此接口或创建适配器
 */
class SpatialItem {
public:
    virtual ~SpatialItem() = default;

    /**
     * @brief 获取空间包围盒
     */
    virtual SpatialBounds<double, 3> getBounds() const = 0;

    /**
     * @brief 获取对象的唯一标识
     */
    virtual size_t getId() const = 0;

    /**
     * @brief 获取对象的几何中心点
     */
    virtual std::array<double, 3> getCenter() const {
        auto bounds = getBounds();
        return bounds.center();
    }

    /**
     * @brief 检查对象是否与包围盒相交
     */
    virtual bool intersects(const SpatialBounds<double, 3>& bounds) const {
        return getBounds().intersects(bounds);
    }

    /**
     * @brief 检查对象是否包含在包围盒内
     */
    virtual bool isContainedBy(const SpatialBounds<double, 3>& bounds) const {
        return bounds.contains(getBounds());
    }
};

/**
 * @brief 空间对象智能指针类型
 */
using SpatialItemPtr = std::shared_ptr<SpatialItem>;

/**
 * @brief 空间对象列表
 */
using SpatialItemList = std::vector<SpatialItemPtr>;

/**
 * @brief 空间对象引用 (轻量级，不拥有所有权)
 */
class SpatialItemRef {
public:
    SpatialItemRef() = default;
    explicit SpatialItemRef(const SpatialItem* item) : item_(item) {}
    explicit SpatialItemRef(const SpatialItemPtr& ptr) : item_(ptr.get()) {}

    const SpatialItem* get() const { return item_; }
    const SpatialItem* operator->() const { return item_; }
    const SpatialItem& operator*() const { return *item_; }

    bool isValid() const { return item_ != nullptr; }
    explicit operator bool() const { return isValid(); }

    bool operator==(const SpatialItemRef& other) const { return item_ == other.item_; }
    bool operator!=(const SpatialItemRef& other) const { return !(*this == other); }

private:
    const SpatialItem* item_ = nullptr;
};

/**
 * @brief 空间对象引用列表
 */
using SpatialItemRefList = std::vector<SpatialItemRef>;

/**
 * @brief 空间对象适配器基类
 *
 * 用于将业务数据包装为空间对象
 * @tparam T 业务数据类型
 */
template<typename T>
class SpatialItemAdapter : public SpatialItem {
public:
    explicit SpatialItemAdapter(const T& data) : data_(data) {}
    explicit SpatialItemAdapter(T&& data) : data_(std::move(data)) {}

    const T& getData() const { return data_; }
    T& getData() { return data_; }

protected:
    T data_;
};

/**
 * @brief 带ID的空间对象适配器
 */
template<typename T>
class SpatialItemAdapterWithId : public SpatialItemAdapter<T> {
public:
    SpatialItemAdapterWithId(size_t id, const T& data) 
        : SpatialItemAdapter<T>(data), id_(id) {}
    SpatialItemAdapterWithId(size_t id, T&& data) 
        : SpatialItemAdapter<T>(std::move(data)), id_(id) {}

    size_t getId() const override { return id_; }

private:
    size_t id_;
};

} // namespace spatial::core

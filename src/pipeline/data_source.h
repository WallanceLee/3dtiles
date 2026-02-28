#pragma once

#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <unordered_map>
#include <filesystem>
#include <osg/Geometry>

namespace pipeline {

// 前向声明
class ISpatialItem;
using SpatialItemPtr = std::shared_ptr<ISpatialItem>;
using SpatialItemList = std::vector<SpatialItemPtr>;

// 数据源配置 - 使用聚合初始化
struct DataSourceConfig {
    std::filesystem::path input_path;
    std::filesystem::path output_path;

    // 地理参考
    double center_longitude = 0.0;
    double center_latitude = 0.0;
    double center_height = 0.0;

    // Shapefile 特定
    std::string height_field;

    // 处理选项
    bool enable_simplification = false;
    bool enable_draco = false;
    bool enable_lod = false;
};

// 空间项接口 - 统一表示几何对象
class ISpatialItem {
public:
    virtual ~ISpatialItem() = default;

    // 禁止拷贝，允许移动
    ISpatialItem(const ISpatialItem&) = delete;
    ISpatialItem& operator=(const ISpatialItem&) = delete;
    ISpatialItem(ISpatialItem&&) = default;
    ISpatialItem& operator=(ISpatialItem&&) = default;

    // 获取唯一ID
    [[nodiscard]] virtual auto GetId() const -> uint64_t = 0;

    // 获取包围盒 (WGS84 经纬度/高度)
    [[nodiscard]] virtual auto GetBounds() const
        -> std::tuple<double, double, double, double, double, double> = 0;

    // 获取几何数据 (OSG 格式)
    [[nodiscard]] virtual auto GetGeometry() const
        -> osg::ref_ptr<osg::Geometry> = 0;

    // 获取属性数据
    [[nodiscard]] virtual auto GetProperties() const
        -> std::unordered_map<std::string, std::string> = 0;

protected:
    ISpatialItem() = default;
};

// 数据源接口
class DataSource {
public:
    virtual ~DataSource() = default;

    // 禁止拷贝，允许移动
    DataSource(const DataSource&) = delete;
    DataSource& operator=(const DataSource&) = delete;
    DataSource(DataSource&&) = default;
    DataSource& operator=(DataSource&&) = default;

    // 加载数据
    [[nodiscard]] virtual auto Load(const DataSourceConfig& config) -> bool = 0;

    // 获取空间项列表
    [[nodiscard]] virtual auto GetSpatialItems() const -> SpatialItemList = 0;

    // 获取世界包围盒
    [[nodiscard]] virtual auto GetWorldBounds() const
        -> std::tuple<double, double, double, double, double, double> = 0;

    // 获取地理参考
    [[nodiscard]] virtual auto GetGeoReference() const
        -> std::tuple<double, double, double> = 0;

    // 获取数据项数量
    [[nodiscard]] virtual auto GetItemCount() const noexcept -> std::size_t = 0;

    // 是否已加载
    [[nodiscard]] virtual auto IsLoaded() const noexcept -> bool = 0;

protected:
    DataSource() = default;
};

using DataSourcePtr = std::unique_ptr<DataSource>;
using DataSourceCreator = std::function<DataSourcePtr()>;

// 数据源工厂 - 单例注册模式
class DataSourceFactory {
public:
    [[nodiscard]] static auto Instance() noexcept -> DataSourceFactory&;

    void Register(const std::string& type, DataSourceCreator creator);
    [[nodiscard]] auto Create(const std::string& type) const -> DataSourcePtr;
    [[nodiscard]] auto IsRegistered(const std::string& type) const noexcept -> bool;

private:
    DataSourceFactory() = default;
    ~DataSourceFactory() = default;

    std::unordered_map<std::string, DataSourceCreator> creators_;
};

// 数据源注册辅助宏
#define REGISTER_DATA_SOURCE(TYPE, CLASS)                                    \
    namespace {                                                              \
        [[maybe_unused]] const bool _##CLASS##_registered = []() -> bool {   \
            ::pipeline::DataSourceFactory::Instance().Register(              \
                TYPE, []() -> ::pipeline::DataSourcePtr {                    \
                    return std::make_unique<CLASS>();                        \
                });                                                          \
            return true;                                                     \
        }();                                                                 \
    }

} // namespace pipeline

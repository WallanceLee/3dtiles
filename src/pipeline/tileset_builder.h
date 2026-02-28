#pragma once

/**
 * @file tileset_builder.h
 * @brief TilesetBuilder 抽象接口
 *
 * 步骤3：定义统一的 Tileset 构建接口，用于替换直接使用 ShapefileTilesetAdapter/FBXTilesetAdapter
 */

#include "spatial_index.h"
#include "../tileset/tileset_types.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace pipeline {

// 瓦片元数据接口
class ITileMeta {
public:
    virtual ~ITileMeta() = default;

    // 禁止拷贝，允许移动
    ITileMeta(const ITileMeta&) = delete;
    ITileMeta& operator=(const ITileMeta&) = delete;
    ITileMeta(ITileMeta&&) = default;
    ITileMeta& operator=(ITileMeta&&) = default;

    // 获取节点唯一ID
    [[nodiscard]] virtual auto GetId() const -> uint64_t = 0;

    // 获取父节点ID (0表示根节点)
    [[nodiscard]] virtual auto GetParentId() const -> uint64_t = 0;

    // 获取子节点ID列表
    [[nodiscard]] virtual auto GetChildIds() const -> std::vector<uint64_t> = 0;

    // 获取包围盒
    [[nodiscard]] virtual auto GetBounds() const
        -> std::tuple<double, double, double, double, double, double> = 0;

    // 获取几何误差
    [[nodiscard]] virtual auto GetGeometricError() const -> double = 0;

    // 获取内容URI
    [[nodiscard]] virtual auto GetContentUri() const -> std::string = 0;

    // 是否有内容
    [[nodiscard]] virtual auto HasContent() const -> bool = 0;

    // 获取深度/层级
    [[nodiscard]] virtual auto GetDepth() const -> int = 0;

protected:
    ITileMeta() = default;
};

using TileMetaPtr = std::shared_ptr<ITileMeta>;
using TileMetaMap = std::unordered_map<uint64_t, TileMetaPtr>;

// Tileset 构建配置
struct TilesetBuilderConfig {
    // 版本信息
    std::string version = "1.0";
    std::string gltf_up_axis = "Z";

    // 几何误差配置
    double root_geometric_error_multiplier = 2.0;
    double child_geometric_error_multiplier = 0.5;

    // 包围盒扩展系数
    double bounding_volume_scale = 1.0;

    // 是否启用LOD
    bool enable_lod = false;

    // LOD级别数量
    int lod_level_count = 1;

    // 细化策略 ("ADD" 或 "REPLACE")
    std::string refine = "REPLACE";

    // 地理中心
    double center_longitude = 0.0;
    double center_latitude = 0.0;
    double center_height = 0.0;
};

// Tileset 构建器接口
class ITilesetBuilder {
public:
    virtual ~ITilesetBuilder() = default;

    // 禁止拷贝，允许移动
    ITilesetBuilder(const ITilesetBuilder&) = delete;
    ITilesetBuilder& operator=(const ITilesetBuilder&) = delete;
    ITilesetBuilder(ITilesetBuilder&&) = default;
    ITilesetBuilder& operator=(ITilesetBuilder&&) = default;

    // 初始化构建器
    virtual void Initialize(const TilesetBuilderConfig& config) = 0;

    // 添加瓦片元数据
    virtual void AddTileMeta(const TileMetaPtr& meta) = 0;

    // 构建 Tileset
    [[nodiscard]] virtual auto BuildTileset() -> tileset::Tileset = 0;

    // 构建并写入文件
    [[nodiscard]] virtual auto BuildAndWrite(const std::string& output_path) -> bool = 0;

    // 清空所有元数据
    virtual void Clear() = 0;

protected:
    ITilesetBuilder() = default;
};

using TilesetBuilderPtr = std::unique_ptr<ITilesetBuilder>;
using TilesetBuilderCreator = std::function<TilesetBuilderPtr()>;

// TilesetBuilder 工厂 - 单例注册模式
class TilesetBuilderFactory {
public:
    [[nodiscard]] static auto Instance() noexcept -> TilesetBuilderFactory&;

    void Register(const std::string& type, TilesetBuilderCreator creator);
    [[nodiscard]] auto Create(const std::string& type) const -> TilesetBuilderPtr;
    [[nodiscard]] auto IsRegistered(const std::string& type) const noexcept -> bool;

private:
    TilesetBuilderFactory() = default;
    ~TilesetBuilderFactory() = default;

    std::unordered_map<std::string, TilesetBuilderCreator> creators_;
};

// TilesetBuilder 注册辅助宏
#define REGISTER_TILESET_BUILDER(TYPE, CLASS)                                  \
    namespace {                                                                \
        [[maybe_unused]] const bool _##CLASS##_registered = []() -> bool {     \
            ::pipeline::TilesetBuilderFactory::Instance().Register(            \
                TYPE, []() -> ::pipeline::TilesetBuilderPtr {                  \
                    return std::make_unique<CLASS>();                        \
                });                                                          \
            return true;                                                     \
        }();                                                                 \
    }

} // namespace pipeline

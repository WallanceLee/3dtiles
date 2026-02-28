#pragma once

/**
 * @file fbx_tileset_builder.h
 * @brief FBX TilesetBuilder 适配器
 *
 * 步骤3：将 fbx::FBXTilesetAdapter 适配为 pipeline::ITilesetBuilder 接口
 */

#include "pipeline/tileset_builder.h"
#include "fbx/fbx_tileset_adapter.h"
#include "fbx/fbx_tile_meta.h"
#include "tileset/tileset_types.h"
#include "tileset/tileset_writer.h"
#include <memory>
#include <unordered_map>

namespace pipeline::adapters::tileset {

// FBX 瓦片元数据适配器
class FBXTileMetaAdapter : public ITileMeta {
public:
    explicit FBXTileMetaAdapter(const ::fbx::FBXTileMeta& meta)
        : meta_(meta) {}

    [[nodiscard]] auto GetId() const -> uint64_t override {
        return meta_.key();
    }

    [[nodiscard]] auto GetParentId() const -> uint64_t override {
        return meta_.parentKey();
    }

    [[nodiscard]] auto GetChildIds() const -> std::vector<uint64_t> override {
        std::vector<uint64_t> ids;
        for (const auto& key : meta_.childrenKeys) {
            ids.push_back(key);
        }
        return ids;
    }

    [[nodiscard]] auto GetBounds() const
        -> std::tuple<double, double, double, double, double, double> override {
        return {
            meta_.bbox.xMin(), meta_.bbox.yMin(), meta_.bbox.zMin(),
            meta_.bbox.xMax(), meta_.bbox.yMax(), meta_.bbox.zMax()
        };
    }

    [[nodiscard]] auto GetGeometricError() const -> double override {
        return meta_.geometricError;
    }

    [[nodiscard]] auto GetContentUri() const -> std::string override {
        return meta_.b3dmPath;
    }

    [[nodiscard]] auto HasContent() const -> bool override {
        return meta_.hasGeometry;
    }

    [[nodiscard]] auto GetDepth() const -> int override {
        return meta_.coord.z;
    }

    // 获取原始元数据
    [[nodiscard]] auto GetRawMeta() const -> const ::fbx::FBXTileMeta& {
        return meta_;
    }

private:
    ::fbx::FBXTileMeta meta_;
};

// FBX TilesetBuilder 适配器
class FBXTilesetBuilderAdapter : public ITilesetBuilder {
public:
    FBXTilesetBuilderAdapter() = default;
    ~FBXTilesetBuilderAdapter() override = default;

    // 禁止拷贝，允许移动
    FBXTilesetBuilderAdapter(const FBXTilesetBuilderAdapter&) = delete;
    FBXTilesetBuilderAdapter& operator=(const FBXTilesetBuilderAdapter&) = delete;
    FBXTilesetBuilderAdapter(FBXTilesetBuilderAdapter&&) = default;
    FBXTilesetBuilderAdapter& operator=(FBXTilesetBuilderAdapter&&) = default;

    // 初始化构建器
    void Initialize(const TilesetBuilderConfig& config) override {
        config_ = config;

        // 创建 FBXTilesetAdapter
        ::fbx::FBXTilesetAdapterConfig adapterConfig;
        adapterConfig.centerLongitude = config.center_longitude;
        adapterConfig.centerLatitude = config.center_latitude;
        adapterConfig.centerHeight = config.center_height;
        adapterConfig.boundingVolumeScale = config.bounding_volume_scale;
        adapterConfig.geometricErrorScale = config.child_geometric_error_multiplier;
        adapterConfig.enableLOD = config.enable_lod;
        adapterConfig.lodLevelCount = config.lod_level_count;

        adapter_ = std::make_unique<::fbx::FBXTilesetAdapter>(adapterConfig);
    }

    // 添加瓦片元数据
    void AddTileMeta(const TileMetaPtr& meta) override {
        auto* fbxMeta = dynamic_cast<FBXTileMetaAdapter*>(meta.get());
        if (fbxMeta) {
            // 转换为 FBX TileMeta
            ::fbx::FBXTileMeta fbxMetaData = fbxMeta->GetRawMeta();
            metas_[fbxMetaData.key()] = std::make_shared<::fbx::FBXTileMeta>(fbxMetaData);
        }
    }

    // 添加 FBX 专用元数据
    void AddFBXTileMeta(const ::fbx::FBXTileMetaPtr& meta) {
        metas_[meta->key()] = meta;
    }

    // 构建 Tileset
    [[nodiscard]] auto BuildTileset() -> ::tileset::Tileset override {
        if (!adapter_ || metas_.empty()) {
            return ::tileset::Tileset();
        }

        // 查找根节点
        uint64_t rootId = 0;
        for (const auto& [id, meta] : metas_) {
            if (meta->parentKey() == 0) {
                rootId = id;
                break;
            }
        }

        if (rootId == 0) {
            return ::tileset::Tileset();
        }

        // 构建 Tileset
        // 注意：FBXTilesetAdapter 的 buildAndWriteTileset 方法需要输出路径
        // 这里返回空 tileset，实际构建应该使用 BuildAndWrite
        return ::tileset::Tileset();
    }

    // 构建并写入文件
    [[nodiscard]] auto BuildAndWrite(const std::string& output_path) -> bool override {
        if (!adapter_ || metas_.empty()) {
            return false;
        }

        return adapter_->buildAndWriteTileset(metas_, output_path);
    }

    // 清空所有元数据
    void Clear() override {
        metas_.clear();
    }

    // 获取原始适配器
    [[nodiscard]] auto GetRawAdapter() const -> ::fbx::FBXTilesetAdapter* {
        return adapter_.get();
    }

    // 获取元数据映射表
    [[nodiscard]] auto GetMetaMap() const -> const ::fbx::FBXTileMetaMap& {
        return metas_;
    }

private:
    TilesetBuilderConfig config_;
    std::unique_ptr<::fbx::FBXTilesetAdapter> adapter_;
    ::fbx::FBXTileMetaMap metas_;
};

// 注册 FBX TilesetBuilder
REGISTER_TILESET_BUILDER("fbx", FBXTilesetBuilderAdapter);

} // namespace pipeline::adapters::tileset

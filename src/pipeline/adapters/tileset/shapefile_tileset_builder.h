#pragma once

/**
 * @file shapefile_tileset_builder.h
 * @brief Shapefile TilesetBuilder 适配器
 *
 * 步骤3：将 shapefile::ShapefileTilesetAdapter 适配为 pipeline::ITilesetBuilder 接口
 */

#include "pipeline/tileset_builder.h"
#include "shapefile/shapefile_tileset_adapter.h"
#include "shapefile/shapefile_tile_meta.h"
#include "tileset/tileset_types.h"
#include "tileset/tileset_writer.h"
#include <memory>
#include <unordered_map>
#include <fstream>

namespace pipeline::adapters::tileset {

// Shapefile 瓦片元数据适配器
class ShapefileTileMetaAdapter : public ITileMeta {
public:
    explicit ShapefileTileMetaAdapter(const ::shapefile::ShapefileTileMeta& meta)
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
            meta_.bbox.minX, meta_.bbox.minY, meta_.bbox.minZ,
            meta_.bbox.maxX, meta_.bbox.maxY, meta_.bbox.maxZ
        };
    }

    [[nodiscard]] auto GetGeometricError() const -> double override {
        return meta_.geometricError;
    }

    [[nodiscard]] auto GetContentUri() const -> std::string override {
        return meta_.content.uri;
    }

    [[nodiscard]] auto HasContent() const -> bool override {
        return meta_.content.hasContent;
    }

    [[nodiscard]] auto GetDepth() const -> int override {
        return meta_.coord.z;
    }

    // 获取原始元数据
    [[nodiscard]] auto GetRawMeta() const -> const ::shapefile::ShapefileTileMeta& {
        return meta_;
    }

private:
    ::shapefile::ShapefileTileMeta meta_;
};

// Shapefile TilesetBuilder 适配器
class ShapefileTilesetBuilderAdapter : public ITilesetBuilder {
public:
    ShapefileTilesetBuilderAdapter() = default;
    ~ShapefileTilesetBuilderAdapter() override = default;

    // 禁止拷贝，允许移动
    ShapefileTilesetBuilderAdapter(const ShapefileTilesetBuilderAdapter&) = delete;
    ShapefileTilesetBuilderAdapter& operator=(const ShapefileTilesetBuilderAdapter&) = delete;
    ShapefileTilesetBuilderAdapter(ShapefileTilesetBuilderAdapter&&) = default;
    ShapefileTilesetBuilderAdapter& operator=(ShapefileTilesetBuilderAdapter&&) = default;

    // 初始化构建器
    void Initialize(const TilesetBuilderConfig& config) override {
        config_ = config;

        // 创建 ShapefileTilesetAdapter
        ::shapefile::AdapterConfig adapterConfig;
        adapterConfig.boundingVolumeScaleFactor = config.bounding_volume_scale;
        adapterConfig.geometricErrorScale = config.child_geometric_error_multiplier;
        adapterConfig.applyRootTransform = true;
        adapterConfig.enableLOD = config.enable_lod;
        adapterConfig.lodLevelCount = config.lod_level_count;

        adapter_ = std::make_unique<::shapefile::ShapefileTilesetAdapter>(
            config.center_longitude,
            config.center_latitude,
            adapterConfig
        );
    }

    // 添加瓦片元数据
    void AddTileMeta(const TileMetaPtr& meta) override {
        auto* shapefileMeta = dynamic_cast<ShapefileTileMetaAdapter*>(meta.get());
        if (shapefileMeta) {
            // 转换为 Shapefile TileMeta
            ::shapefile::ShapefileTileMeta sfMeta = shapefileMeta->GetRawMeta();
            metas_[sfMeta.key()] = sfMeta;
        }
    }

    // 添加 Shapefile 专用元数据
    void AddShapefileTileMeta(const ::shapefile::ShapefileTileMeta& meta) {
        metas_[meta.key()] = meta;
    }

    // 构建 Tileset
    [[nodiscard]] auto BuildTileset() -> ::tileset::Tileset override {
        if (!adapter_ || metas_.empty()) {
            return ::tileset::Tileset();
        }

        // 查找根节点
        uint64_t rootId = 0;
        for (const auto& [id, meta] : metas_) {
            if (meta.parentKey() == 0) {
                rootId = id;
                break;
            }
        }

        if (rootId == 0) {
            return ::tileset::Tileset();
        }

        // 构建 Tileset - 使用通用 TileMetaMap
        common::TileMetaMap metaMap;
        for (const auto& [id, meta] : metas_) {
            metaMap[id] = std::make_shared<::shapefile::ShapefileTileMeta>(meta);
        }
        auto rootMeta = metaMap[rootId];

        return adapter_->buildTileset(rootMeta, metaMap);
    }

    // 构建并写入文件
    [[nodiscard]] auto BuildAndWrite(const std::string& output_path) -> bool override {
        auto tileset = BuildTileset();
        // 检查根节点是否有效
        if (!tileset.root.content.has_value() && tileset.root.children.empty()) {
            return false;
        }

        // 使用 TilesetWriter 序列化并写入
        ::tileset::TilesetWriter writer;
        return writer.writeToFile(tileset, output_path);
    }

    // 清空所有元数据
    void Clear() override {
        metas_.clear();
    }

    // 获取原始适配器
    [[nodiscard]] auto GetRawAdapter() const -> ::shapefile::ShapefileTilesetAdapter* {
        return adapter_.get();
    }

private:
    TilesetBuilderConfig config_;
    std::unique_ptr<::shapefile::ShapefileTilesetAdapter> adapter_;
    std::unordered_map<uint64_t, ::shapefile::ShapefileTileMeta> metas_;
    uint64_t rootId_ = 0;
};

// 注册 Shapefile TilesetBuilder
REGISTER_TILESET_BUILDER("shapefile", ShapefileTilesetBuilderAdapter);

} // namespace pipeline::adapters::tileset

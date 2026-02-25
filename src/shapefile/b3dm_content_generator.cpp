#include "b3dm_content_generator.h"
#include "shapefile_spatial_item_adapter.h"
#include "../b3dm/b3dm_writer.h"

#include <filesystem>
#include <fstream>

namespace shapefile {

B3DMContentGenerator::B3DMContentGenerator(double centerLon, double centerLat)
    : centerLon_(centerLon), centerLat_(centerLat) {}

std::vector<std::shared_ptr<ShapefileSpatialItemAdapter>> B3DMContentGenerator::convertToSpatialItems(
    const std::vector<const ShapefileSpatialItem*>& items) {

    std::vector<std::shared_ptr<ShapefileSpatialItemAdapter>> result;
    result.reserve(items.size());

    for (const auto* item : items) {
        if (!item) continue;

        // 创建适配器包装器
        auto adapter = std::make_shared<ShapefileSpatialItemAdapter>(item);
        result.push_back(adapter);
    }

    return result;
}

std::string B3DMContentGenerator::generate(
    const std::vector<const ShapefileSpatialItem*>& items,
    bool withHeight,
    bool enableSimplify,
    const std::optional<SimplificationParams>& simplifyParams,
    bool enableDraco,
    const std::optional<DracoCompressionParams>& dracoParams) {

    if (items.empty()) {
        return std::string();
    }

    // 转换为适配器列表
    auto adapters = convertToSpatialItems(items);
    if (adapters.empty()) {
        return std::string();
    }

    // 转换为SpatialItemRefList（使用原始指针）
    spatial::core::SpatialItemRefList spatialItems;
    spatialItems.reserve(adapters.size());
    for (const auto& adapter : adapters) {
        spatialItems.emplace_back(adapter.get());
    }

    // 配置B3DM生成器
    b3dm::B3DMGeneratorConfig config;
    config.centerLongitude = centerLon_;
    config.centerLatitude = centerLat_;
    config.centerHeight = 0.0;
    config.enableSimplification = enableSimplify;
    config.enableDraco = enableDraco;
    config.geometryExtractor = &geometryExtractor_;

    if (simplifyParams.has_value()) {
        config.simplifyParams = simplifyParams.value();
    }

    if (dracoParams.has_value()) {
        config.dracoParams = dracoParams.value();
    }

    // 创建B3DM生成器
    b3dm::B3DMGenerator generator(config);

    // 构建LOD级别设置
    LODLevelSettings lodSettings;
    lodSettings.enable_simplification = enableSimplify;
    lodSettings.enable_draco = enableDraco;

    if (simplifyParams.has_value()) {
        lodSettings.simplify = simplifyParams.value();
    }

    if (dracoParams.has_value()) {
        lodSettings.draco = dracoParams.value();
    }

    // 生成B3DM
    return generator.generate(spatialItems, lodSettings);
}

std::vector<b3dm::LODFileInfo> B3DMContentGenerator::generateLODFiles(
    const std::vector<const ShapefileSpatialItem*>& items,
    const std::string& outputDir,
    const std::vector<LODLevelSettings>& lodLevels) {

    std::vector<b3dm::LODFileInfo> result;

    if (items.empty() || lodLevels.empty()) {
        return result;
    }

    // 转换为适配器列表
    auto adapters = convertToSpatialItems(items);
    if (adapters.empty()) {
        return result;
    }

    // 转换为SpatialItemRefList
    spatial::core::SpatialItemRefList spatialItems;
    spatialItems.reserve(adapters.size());
    for (const auto& adapter : adapters) {
        spatialItems.emplace_back(adapter.get());
    }

    // 配置B3DM生成器
    b3dm::B3DMGeneratorConfig config;
    config.centerLongitude = centerLon_;
    config.centerLatitude = centerLat_;
    config.centerHeight = 0.0;
    config.geometryExtractor = &geometryExtractor_;

    // 从LOD级别设置中提取简化参数（使用第一个级别的设置）
    if (!lodLevels.empty()) {
        config.enableSimplification = lodLevels[0].enable_simplification;
        config.simplifyParams = lodLevels[0].simplify;
        config.enableDraco = lodLevels[0].enable_draco;
        config.dracoParams = lodLevels[0].draco;
    }

    // 创建B3DM生成器
    b3dm::B3DMGenerator generator(config);

    // 提取基础文件名
    std::string baseName = std::filesystem::path(outputDir).filename().string();

    // 生成LOD文件
    return generator.generateLODFiles(spatialItems, outputDir, baseName, lodLevels);
}

bool B3DMContentGenerator::generateToFile(
    const std::vector<const ShapefileSpatialItem*>& items,
    const std::string& outputPath,
    bool withHeight) {

    std::string b3dmData = generate(items, withHeight);
    if (b3dmData.empty()) {
        return false;
    }

    // 创建输出目录
    std::filesystem::path p(outputPath);
    std::filesystem::create_directories(p.parent_path());

    // 写入文件
    std::ofstream file(outputPath, std::ios::binary);
    if (!file) {
        return false;
    }

    file.write(b3dmData.data(), b3dmData.size());
    return file.good();
}

} // namespace shapefile

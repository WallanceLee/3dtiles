#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace b3dm {

// B3DM 文件魔数和版本
constexpr uint32_t B3DM_MAGIC = 0x6D643362;  // 'b3dm'
constexpr uint32_t B3DM_VERSION = 1;
constexpr size_t B3DM_HEADER_SIZE = 28;

// B3DM Header 结构（与 3D Tiles 规范一致）
#pragma pack(push, 1)
struct Header {
    uint32_t magic;                          // 'b3dm' = 0x6D643362
    uint32_t version;                        // 1
    uint32_t byteLength;                     // 整个文件的字节长度
    uint32_t featureTableJSONByteLength;     // Feature Table JSON 长度
    uint32_t featureTableBinaryByteLength;   // Feature Table Binary 长度（通常为 0）
    uint32_t batchTableJSONByteLength;       // Batch Table JSON 长度
    uint32_t batchTableBinaryByteLength;     // Batch Table Binary 长度（通常为 0）
};
#pragma pack(pop)

// Batch 数据定义
struct BatchData {
    std::vector<int> batchIds;                    // 必需：batch ID 数组
    std::vector<std::string> names;               // 可选：名称数组
    std::map<std::string, std::vector<nlohmann::json>> attributes; // 动态属性

    // 便捷方法：检查是否为空
    bool empty() const { return batchIds.empty(); }

    // 便捷方法：获取 batch 数量
    size_t size() const { return batchIds.size(); }
};

// B3DM 构建选项
struct Options {
    bool alignTo8Bytes = true;                    // 是否使用 8 字节对齐（推荐，符合规范）
    bool allowEmptyBatch = false;                 // 是否允许 BATCH_LENGTH=0
    nlohmann::json extraFeatureTable;             // 额外的 Feature Table 字段（如 RTC_CENTER）
    nlohmann::json extraBatchTable;               // 额外的 Batch Table 字段
};

// ============================================
// 核心函数：GLB Buffer → B3DM Buffer
// ============================================

/**
 * @brief 将 GLB buffer 包装为 B3DM 格式
 *
 * @param glbBuffer 输入的 GLB 二进制数据
 * @param batchData Batch 数据（batchIds, names, attributes）
 * @param options 构建选项
 * @return std::string B3DM 格式的二进制数据
 */
std::string wrapGlbToB3dm(
    const std::string& glbBuffer,
    const BatchData& batchData,
    const Options& options = {}
);

/**
 * @brief 将 GLB buffer 包装为 B3DM 格式（简化版，自动构造 BatchData）
 *
 * @param glbBuffer 输入的 GLB 二进制数据
 * @param batchLength BATCH_LENGTH 值
 * @param options 构建选项
 * @return std::string B3DM 格式的二进制数据
 */
std::string wrapGlbToB3dmSimple(
    const std::string& glbBuffer,
    size_t batchLength,
    const Options& options = {}
);

// ============================================
// 文件操作函数
// ============================================

/**
 * @brief 将 B3DM 数据写入文件
 *
 * @param filePath 输出文件路径
 * @param b3dmData B3DM 二进制数据
 * @return true 写入成功
 * @return false 写入失败
 */
bool writeB3dmToFile(
    const std::string& filePath,
    const std::string& b3dmData
);

/**
 * @brief 直接将 GLB 转换为 B3DM 并写入文件
 *
 * @param filePath 输出文件路径
 * @param glbBuffer 输入的 GLB 二进制数据
 * @param batchData Batch 数据
 * @param options 构建选项
 * @return true 写入成功
 * @return false 写入失败
 */
bool writeGlbAsB3dm(
    const std::string& filePath,
    const std::string& glbBuffer,
    const BatchData& batchData,
    const Options& options = {}
);

// ============================================
// 工具函数
// ============================================

/**
 * @brief 计算 B3DM 总大小（用于预分配内存）
 *
 * @param glbSize GLB 数据大小
 * @param featureTableSize Feature Table JSON 大小
 * @param batchTableSize Batch Table JSON 大小
 * @param alignTo8Bytes 是否使用 8 字节对齐
 * @return size_t B3DM 总大小
 */
size_t calculateB3dmSize(
    size_t glbSize,
    size_t featureTableSize,
    size_t batchTableSize,
    bool alignTo8Bytes = true
);

/**
 * @brief 填充字符串到指定对齐边界
 *
 * @param str 要填充的字符串（会被修改）
 * @param alignment 对齐字节数（通常为 4 或 8）
 */
void padString(std::string& str, size_t alignment);

/**
 * @brief 验证 B3DM Header 的有效性
 *
 * @param header Header 结构体
 * @return true 有效
 * @return false 无效
 */
bool validateHeader(const Header& header);

} // namespace b3dm

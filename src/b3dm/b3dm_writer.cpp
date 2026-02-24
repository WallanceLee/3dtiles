#include "b3dm_writer.h"

#include <fstream>
#include <cstring>
#include <algorithm>

namespace b3dm {

void padString(std::string& str, size_t alignment) {
    if (alignment == 0) return;
    size_t remainder = str.size() % alignment;
    if (remainder != 0) {
        str.append(alignment - remainder, ' ');
    }
}

size_t calculateB3dmSize(
    size_t glbSize,
    size_t featureTableSize,
    size_t batchTableSize,
    bool alignTo8Bytes
) {
    size_t alignment = alignTo8Bytes ? 8 : 4;

    // 计算对齐后的尺寸
    size_t alignedFeatureTableSize = featureTableSize;
    size_t remainder = alignedFeatureTableSize % alignment;
    if (remainder != 0) {
        alignedFeatureTableSize += alignment - remainder;
    }

    size_t alignedBatchTableSize = batchTableSize;
    remainder = alignedBatchTableSize % alignment;
    if (remainder != 0) {
        alignedBatchTableSize += alignment - remainder;
    }

    return B3DM_HEADER_SIZE + alignedFeatureTableSize + alignedBatchTableSize + glbSize;
}

bool validateHeader(const Header& header) {
    if (header.magic != B3DM_MAGIC) {
        return false;
    }
    if (header.version != B3DM_VERSION) {
        return false;
    }
    if (header.byteLength < B3DM_HEADER_SIZE) {
        return false;
    }
    return true;
}

std::string wrapGlbToB3dm(
    const std::string& glbBuffer,
    const BatchData& batchData,
    const Options& options
) {
    using nlohmann::json;

    // 1. 构造 Feature Table
    json featureTable;
    size_t batchLength = batchData.empty() ? 0 : batchData.size();

    // 如果 batch 为空且不允许空 batch，则返回空字符串表示错误
    if (batchLength == 0 && !options.allowEmptyBatch) {
        return "";
    }

    featureTable["BATCH_LENGTH"] = batchLength;

    // 合并额外的 Feature Table 字段
    if (!options.extraFeatureTable.empty()) {
        for (auto& [key, value] : options.extraFeatureTable.items()) {
            featureTable[key] = value;
        }
    }

    std::string featureTableStr = featureTable.dump();

    // 2. 构造 Batch Table
    json batchTable;
    if (batchLength > 0) {
        batchTable["batchId"] = batchData.batchIds;

        if (!batchData.names.empty()) {
            batchTable["name"] = batchData.names;
        }

        // 添加动态属性
        for (const auto& [key, values] : batchData.attributes) {
            batchTable[key] = values;
        }
    }

    // 合并额外的 Batch Table 字段
    if (!options.extraBatchTable.empty()) {
        for (auto& [key, value] : options.extraBatchTable.items()) {
            batchTable[key] = value;
        }
    }

    std::string batchTableStr = batchTable.empty() ? "" : batchTable.dump();

    // 3. 对齐处理
    size_t alignment = options.alignTo8Bytes ? 8 : 4;
    padString(featureTableStr, alignment);
    if (!batchTableStr.empty()) {
        padString(batchTableStr, alignment);
    }

    // 4. 构造 Header
    Header header;
    header.magic = B3DM_MAGIC;
    header.version = B3DM_VERSION;
    header.featureTableJSONByteLength = static_cast<uint32_t>(featureTableStr.size());
    header.featureTableBinaryByteLength = 0;
    header.batchTableJSONByteLength = static_cast<uint32_t>(batchTableStr.size());
    header.batchTableBinaryByteLength = 0;
    header.byteLength = static_cast<uint32_t>(B3DM_HEADER_SIZE +
                                               featureTableStr.size() +
                                               batchTableStr.size() +
                                               glbBuffer.size());

    // 5. 组装 Buffer
    std::string result;
    result.reserve(header.byteLength);

    // 写入 Header
    result.append(reinterpret_cast<const char*>(&header), sizeof(Header));

    // 写入 Feature Table JSON
    result.append(featureTableStr);

    // 写入 Batch Table JSON（如果有）
    if (!batchTableStr.empty()) {
        result.append(batchTableStr);
    }

    // 写入 GLB 数据
    result.append(glbBuffer);

    return result;
}

std::string wrapGlbToB3dmSimple(
    const std::string& glbBuffer,
    size_t batchLength,
    const Options& options
) {
    BatchData batchData;

    if (batchLength > 0) {
        batchData.batchIds.reserve(batchLength);
        batchData.names.reserve(batchLength);

        for (size_t i = 0; i < batchLength; ++i) {
            batchData.batchIds.push_back(static_cast<int>(i));
            batchData.names.push_back("mesh_" + std::to_string(i));
        }
    }

    return wrapGlbToB3dm(glbBuffer, batchData, options);
}

bool writeB3dmToFile(
    const std::string& filePath,
    const std::string& b3dmData
) {
    if (b3dmData.empty()) {
        return false;
    }

    std::ofstream outfile(filePath, std::ios::binary);
    if (!outfile) {
        return false;
    }

    outfile.write(b3dmData.data(), static_cast<std::streamsize>(b3dmData.size()));
    bool success = outfile.good();
    outfile.close();

    return success;
}

bool writeGlbAsB3dm(
    const std::string& filePath,
    const std::string& glbBuffer,
    const BatchData& batchData,
    const Options& options
) {
    std::string b3dmData = wrapGlbToB3dm(glbBuffer, batchData, options);
    if (b3dmData.empty()) {
        return false;
    }

    return writeB3dmToFile(filePath, b3dmData);
}

} // namespace b3dm

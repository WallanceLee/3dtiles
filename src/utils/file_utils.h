#pragma once

/**
 * @file file_utils.h
 * @brief 文件操作工具头文件
 *
 * 提供文件系统相关工具函数，替代 extern.h 中的文件操作功能
 */

#include <string>

namespace utils {

// 创建目录（递归）
// 返回 true 表示成功或目录已存在
bool mkdirs(const char* path);

// 写入文件
// 返回 true 表示成功
bool write_file(const char* filename, const char* buf, unsigned long buf_len);

// C++ 字符串重载
inline bool write_file(const std::string& filename, const std::string& content) {
    return write_file(filename.c_str(), content.c_str(), content.size());
}

} // namespace utils

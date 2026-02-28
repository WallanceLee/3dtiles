/**
 * @file file_utils.cpp
 * @brief 文件操作工具实现
 */

#include "file_utils.h"

// 使用 Rust 实现的 FFI 函数
extern "C" bool mkdirs(const char* path);
extern "C" bool write_file(const char* filename, const char* buf, unsigned long buf_len);

namespace utils {

bool mkdirs(const char* path) {
    return ::mkdirs(path);
}

bool write_file(const char* filename, const char* buf, unsigned long buf_len) {
    return ::write_file(filename, buf, buf_len);
}

} // namespace utils

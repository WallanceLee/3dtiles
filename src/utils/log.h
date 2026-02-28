#pragma once

/**
 * @file log.h
 * @brief 日志工具头文件
 *
 * 提供统一的日志宏定义，替代 extern.h 中的日志功能
 * 基于 spdlog 实现
 */

#include <cstdarg>
#include <cstdio>
#include <fmt/printf.h>
#include <spdlog/spdlog.h>

namespace utils {

// 内部实现函数
inline void log_printf_impl(spdlog::level::level_enum lvl, const char* format, ...) {
    char buf[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    spdlog::log(lvl, "{}", buf);
}

} // namespace utils

// 日志宏定义
#define LOG_D(format, ...) \
    do { \
        utils::log_printf_impl(spdlog::level::debug, format __VA_OPT__(,) __VA_ARGS__); \
    } while (0)

#define LOG_I(format, ...) \
    do { \
        utils::log_printf_impl(spdlog::level::info, format __VA_OPT__(,) __VA_ARGS__); \
    } while (0)

#define LOG_W(format, ...) \
    do { \
        utils::log_printf_impl(spdlog::level::warn, format __VA_OPT__(,) __VA_ARGS__); \
    } while (0)

#define LOG_E(format, ...) \
    do { \
        utils::log_printf_impl(spdlog::level::err, format __VA_OPT__(,) __VA_ARGS__); \
    } while (0)

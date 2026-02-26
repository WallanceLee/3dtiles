//! 通用工具函数
//!
//! 提供路径处理、字符串转换等辅助函数

#![allow(dead_code)]

use crate::error::{TileError, TileResult};
use std::ffi::CString;
use std::path::{Path, PathBuf};

/// 获取可执行文件所在目录
pub fn get_exe_dir() -> TileResult<PathBuf> {
    std::env::current_exe()
        .map_err(TileError::Io)?
        .parent()
        .map(|p| p.to_path_buf())
        .ok_or_else(|| TileError::InvalidPath {
            path: "Could not get executable directory".to_string(),
        })
}

/// 获取GDAL数据路径
pub fn get_gdal_data_path() -> TileResult<String> {
    let exe_dir = get_exe_dir()?;
    let gdal_path = exe_dir.join("gdal");
    path_to_string(&gdal_path)
}

/// 获取PROJ数据路径
pub fn get_proj_data_path() -> TileResult<String> {
    let exe_dir = get_exe_dir()?;
    let proj_path = exe_dir.join("proj");
    path_to_string(&proj_path)
}

/// 将Path转换为字符串
pub fn path_to_string(path: &Path) -> TileResult<String> {
    path.to_str()
        .map(|s| s.to_string())
        .ok_or_else(|| TileError::InvalidUtf8 {
            path: path.to_path_buf(),
        })
}

/// 将字符串转换为CString（检查null字节）
pub fn string_to_cstring(s: impl Into<String>) -> TileResult<CString> {
    let s = s.into();
    CString::new(s.clone()).map_err(|_| TileError::InvalidPath { path: s })
}

/// 解析f64的辅助函数
pub fn parse_f64(s: &str) -> TileResult<f64> {
    s.parse::<f64>()
        .map_err(|_| TileError::InvalidNumber { value: s.to_string() })
}

/// 解析i32的辅助函数
pub fn parse_i32(s: &str) -> TileResult<i32> {
    s.parse::<i32>()
        .map_err(|_| TileError::InvalidNumber { value: s.to_string() })
}

/// 安全地解析多个f64值
pub fn parse_f64_vec<'a>(items: impl Iterator<Item = &'a str>) -> TileResult<Vec<f64>> {
    items.map(parse_f64).collect()
}

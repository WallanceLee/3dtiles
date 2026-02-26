//! 统一错误类型定义
//!
//! 提供整个项目使用的错误类型和Result别名

#![allow(dead_code)]

use std::path::PathBuf;
use thiserror::Error;

/// 项目统一错误类型
#[derive(Error, Debug)]
pub enum TileError {
    /// IO错误
    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),

    /// 无效的路径
    #[error("Invalid path: {path}")]
    InvalidPath { path: String },

    /// 路径包含无效的UTF-8
    #[error("Path contains invalid UTF-8: {}", .path.display())]
    InvalidUtf8 { path: PathBuf },

    /// FFI调用失败
    #[error("FFI call failed: {func} - {reason}")]
    FfiError { func: &'static str, reason: String },

    /// 格式转换失败
    #[error("Conversion failed for format: {format}")]
    ConversionFailed { format: String },

    /// 缺少必需的参数
    #[error("Required argument missing: {arg}")]
    MissingArgument { arg: &'static str },

    /// 无效的坐标值
    #[error("Invalid coordinate value: {value}")]
    InvalidCoordinate { value: String },

    /// 大地水准面初始化失败
    #[error("Geoid initialization failed: {model}")]
    GeoidInitFailed { model: String },

    /// JSON解析错误
    #[error("JSON parse error: {0}")]
    JsonParse(#[from] serde_json::Error),

    /// 无效的数字格式
    #[error("Invalid number format: {value}")]
    InvalidNumber { value: String },

    /// 文件不存在
    #[error("File not found: {}", .path.display())]
    FileNotFound { path: PathBuf },

    /// 目录不存在
    #[error("Directory not found: {}", .path.display())]
    DirectoryNotFound { path: PathBuf },
}

/// 项目统一Result类型
pub type TileResult<T> = Result<T, TileError>;

/// 将Option转换为TileResult的辅助函数
pub trait OptionExt<T> {
    fn ok_or_missing(self, arg: &'static str) -> TileResult<T>;
    fn ok_or_invalid_path(self, path: impl Into<String>) -> TileResult<T>;
    fn ok_or_file_not_found(self, path: PathBuf) -> TileResult<T>;
}

impl<T> OptionExt<T> for Option<T> {
    fn ok_or_missing(self, arg: &'static str) -> TileResult<T> {
        self.ok_or(TileError::MissingArgument { arg })
    }

    fn ok_or_invalid_path(self, path: impl Into<String>) -> TileResult<T> {
        self.ok_or(TileError::InvalidPath { path: path.into() })
    }

    fn ok_or_file_not_found(self, path: PathBuf) -> TileResult<T> {
        self.ok_or(TileError::FileNotFound { path })
    }
}

/// 解析f64的辅助函数，返回TileResult
pub fn parse_f64(s: &str) -> TileResult<f64> {
    s.parse::<f64>()
        .map_err(|_| TileError::InvalidNumber { value: s.to_string() })
}

/// 解析i32的辅助函数，返回TileResult
pub fn parse_i32(s: &str) -> TileResult<i32> {
    s.parse::<i32>()
        .map_err(|_| TileError::InvalidNumber { value: s.to_string() })
}

/// 将Path转换为字符串的辅助函数
pub fn path_to_string(path: &std::path::Path) -> TileResult<String> {
    path.to_str()
        .map(|s| s.to_string())
        .ok_or_else(|| TileError::InvalidUtf8 {
            path: path.to_path_buf(),
        })
}

/// 获取可执行文件所在目录
pub fn get_exe_dir() -> TileResult<PathBuf> {
    std::env::current_exe()
        .map_err(TileError::Io)?
        .parent()
        .map(|p| p.to_path_buf())
        .ok_or(TileError::InvalidPath {
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

//! 构建配置定义

use std::path::PathBuf;

/// 构建错误类型
#[derive(Debug)]
pub enum BuildError {
    EnvVarMissing(String),
    VcpkgRootMissing,
    Io(std::io::Error),
}

impl std::fmt::Display for BuildError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            BuildError::EnvVarMissing(var) => write!(f, "环境变量未设置: {}", var),
            BuildError::VcpkgRootMissing => write!(f, "VCPKG_ROOT未设置"),
            BuildError::Io(e) => write!(f, "IO错误: {}", e),
        }
    }
}

impl std::error::Error for BuildError {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            BuildError::Io(e) => Some(e),
            _ => None,
        }
    }
}

impl From<std::io::Error> for BuildError {
    fn from(e: std::io::Error) -> Self {
        BuildError::Io(e)
    }
}

/// 支持的目标平台
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Platform {
    WindowsMsvc,
    LinuxGnu,
    MacOsArm64,
    MacOsX86_64,
}

impl Platform {
    /// 从TARGET环境变量解析平台
    pub fn from_target(target: &str) -> Option<Self> {
        match target {
            "x86_64-pc-windows-msvc" => Some(Self::WindowsMsvc),
            "x86_64-unknown-linux-gnu" => Some(Self::LinuxGnu),
            "aarch64-apple-darwin" => Some(Self::MacOsArm64),
            "x86_64-apple-darwin" => Some(Self::MacOsX86_64),
            _ => None,
        }
    }

    /// 获取vcpkg triplet名称
    pub fn vcpkg_triplet(&self) -> &'static str {
        match self {
            Self::WindowsMsvc => "x64-windows",
            Self::LinuxGnu => "x64-linux",
            Self::MacOsArm64 => "arm64-osx",
            Self::MacOsX86_64 => "x64-osx",
        }
    }

    /// 是否为macOS平台
    pub fn is_macos(&self) -> bool {
        matches!(self, Self::MacOsArm64 | Self::MacOsX86_64)
    }
}

/// 通用构建配置
#[derive(Debug, Clone)]
pub struct BuildConfig {
    pub platform: Platform,
    pub vcpkg_root: PathBuf,
    pub source_dir: PathBuf,
    pub out_dir: PathBuf,
    pub target_dir: PathBuf,
    pub enable_strict: bool,
    pub vcpkg_has_been_installed: bool,
}

impl BuildConfig {
    /// 从环境变量创建配置
    pub fn from_env() -> Result<Self, BuildError> {
        let target = std::env::var("TARGET")
            .map_err(|_| BuildError::EnvVarMissing("TARGET".into()))?;

        let platform = Platform::from_target(&target)
            .ok_or_else(|| BuildError::EnvVarMissing(format!("未知目标平台: {}", target)))?;

        let vcpkg_root = std::env::var("VCPKG_ROOT")
            .map_err(|_| BuildError::VcpkgRootMissing)?;

        let source_dir = std::env::var("CARGO_MANIFEST_DIR")
            .map_err(|_| BuildError::EnvVarMissing("CARGO_MANIFEST_DIR".into()))?
            .into();

        let out_dir = std::env::var("OUT_DIR")
            .map_err(|_| BuildError::EnvVarMissing("OUT_DIR".into()))?
            .into();

        let profile = std::env::var("PROFILE").unwrap_or_else(|_| "release".into());
        let target_dir = std::env::var("CARGO_TARGET_DIR")
            .map(PathBuf::from)
            .unwrap_or_else(|_| PathBuf::from("target"))
            .join(&profile);

        let enable_strict = std::env::var("ENABLE_STRICT_CHECKS")
            .map(|v| v == "1")
            .unwrap_or(false);

        let vcpkg_has_been_installed = std::env::var("VCPKG_HAS_BEEN_INSTALLED")
            .map(|v| v == "1")
            .unwrap_or(false);

        Ok(Self {
            platform,
            vcpkg_root: vcpkg_root.into(),
            source_dir,
            out_dir,
            target_dir,
            enable_strict,
            vcpkg_has_been_installed,
        })
    }

    /// 获取vcpkg安装目录
    pub fn vcpkg_installed_dir(&self) -> PathBuf {
        self.out_dir
            .join("build")
            .join("vcpkg_installed")
            .join(self.platform.vcpkg_triplet())
    }

    /// 获取vcpkg lib目录
    pub fn vcpkg_lib_dir(&self) -> PathBuf {
        self.vcpkg_installed_dir().join("lib")
    }

    /// 获取vcpkg share目录
    pub fn vcpkg_share_dir(&self) -> PathBuf {
        self.vcpkg_installed_dir().join("share")
    }
}

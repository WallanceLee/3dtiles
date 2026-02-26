# build.rs 重构优化方案

> 创建时间: 2026-02-26
> 目标: 提取公共构建逻辑，解决C++单独编译问题

---

## 一、当前架构问题诊断

### 1.1 代码重复严重（DRY原则违反）

四个平台构建函数存在大量重复代码：

| 重复内容 | 出现次数 | 代码行数 |
|---------|---------|---------|
| vcpkg_root获取与symlink创建 | 4次 | ~15行×4 |
| CMake Config配置 | 4次 | ~10行×4 |
| strict模式检查 | 4次 | ~5行×4 |
| compile_commands导出 | 4次 | ~5行×4 |
| draco库链接 | 4次 | ~3行×4 |
| 数据复制调用 | 4次 | ~6行×4 |

**重复代码占比估算**：约60-70%的代码是重复的。

### 1.2 平台特定逻辑混杂

```rust
// 当前：平台逻辑完全分散在独立函数中
fn build_win_msvc() { /* 200+行，包含Windows特有逻辑 */ }
fn build_linux_unknown() { /* 200+行，包含Linux特有逻辑 */ }
fn build_macos() { /* 200+行，包含macOS特有逻辑 */ }
fn build_macos_x86_64() { /* 200+行，与macOS几乎相同 */ }
```

**问题**：macOS ARM64和x86_64的代码几乎完全相同，仅triplet不同（`arm64-osx` vs `x64-osx`）。

### 1.3 错误处理不一致

```rust
// 有些地方使用 expect
.expect(&format!("Failed to create symlink..."))

// 有些地方使用 unwrap
.copy(&src, &dst).unwrap()

// 有些地方手动处理错误
if let Err(err) = fs::create_dir_all(&dst_dir) {
    println!("cargo:warning=...");
}
```

---

## 二、优化方案概述

### 2.1 目标架构

```
build.rs (入口，~30行)
    │
    ├──► build/
    │       ├── mod.rs      (公共模块导出)
    │       ├── config.rs   (BuildConfig, PlatformConfig)
    │       ├── cmake.rs    (CMake配置抽象)
    │       ├── link.rs     (库链接管理)
    │       ├── platform.rs (平台特定配置)
    │       └── utils.rs    (工具函数：copy, symlink等)
    │
    └──► 原build.rs中的平台函数删除，逻辑迁移到上述模块
```

### 2.2 推荐实施路线图

```
Phase 1: 快速收益（1-2天）
├── 1.1 提取公共函数（copy_gdal_data等）
├── 1.2 合并macOS ARM64/x86_64构建函数
└── 1.3 统一错误处理风格

Phase 2: 架构重构（3-5天）
├── 2.1 设计BuildConfig/LinkConfig结构
├── 2.2 实现平台配置表
├── 2.3 重构main函数使用新抽象
└── 2.4 添加完整错误处理

Phase 3: 高级优化（1-2周）
├── 3.1 集成vcpkg crate自动链接
├── 3.2 实现独立C++编译模式
├── 3.3 CMake现代化改造
└── 3.4 添加构建缓存支持
```

---

## 三、详细设计方案

### 3.1 build/mod.rs - 模块入口

```rust
//! Build script helper modules
//! 
//! 提供跨平台C++构建的抽象和工具函数

pub mod config;
pub mod cmake;
pub mod link;
pub mod platform;
pub mod utils;

pub use config::{BuildConfig, BuildError, Platform};
pub use cmake::CMakeBuilder;
pub use link::LinkManager;
pub use platform::PlatformConfig;
pub use utils::{copy_dir_recursive, create_vcpkg_symlink, export_compile_commands};
```

### 3.2 build/config.rs - 配置结构体

```rust
//! 构建配置定义

use std::path::PathBuf;
use thiserror::Error;

/// 构建错误类型
#[derive(Debug, Error)]
pub enum BuildError {
    #[error("环境变量未设置: {0}")]
    EnvVarMissing(String),
    
    #[error("VCPKG_ROOT未设置")]
    VcpkgRootMissing,
    
    #[error("CMake配置失败: {0}")]
    CMakeConfigFailed(String),
    
    #[error("库链接失败: {name}")]
    LibraryLinkFailed { name: String },
    
    #[error("目录操作失败: {path}")]
    DirectoryOperationFailed { path: PathBuf },
    
    #[error("IO错误: {0}")]
    Io(#[from] std::io::Error),
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
    
    /// 是否为Windows平台
    pub fn is_windows(&self) -> bool {
        matches!(self, Self::WindowsMsvc)
    }
    
    /// 是否为macOS平台
    pub fn is_macos(&self) -> bool {
        matches!(self, Self::MacOsArm64 | Self::MacOsX86_64)
    }
    
    /// 是否为Linux平台
    pub fn is_linux(&self) -> bool {
        matches!(self, Self::LinuxGnu)
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
        let platform = std::env::var("TARGET")
            .map_err(|_| BuildError::EnvVarMissing("TARGET".into()))?
            .parse()?;
            
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

impl std::str::FromStr for Platform {
    type Err = BuildError;
    
    fn from_str(s: &str) -> Result<Self, Self::Err> {
        Self::from_target(s)
            .ok_or_else(|| BuildError::EnvVarMissing(format!("未知目标平台: {}", s)))
    }
}
```

### 3.3 build/cmake.rs - CMake配置抽象

```rust
//! CMake构建抽象

use cmake::Config;
use std::path::PathBuf;
use crate::config::{BuildConfig, BuildError, Platform};

/// CMake构建器
pub struct CMakeBuilder {
    config: Config,
    build_config: BuildConfig,
}

impl CMakeBuilder {
    /// 创建新的CMake构建器
    pub fn new(build_config: &BuildConfig) -> Result<Self, BuildError> {
        let mut config = Config::new(&build_config.source_dir);
        
        // 配置基础选项
        config
            .define("CMAKE_TOOLCHAIN_FILE", 
                format!("{}/scripts/buildsystems/vcpkg.cmake", 
                    build_config.vcpkg_root.display()))
            .define("CMAKE_EXPORT_COMPILE_COMMANDS", "ON")
            .very_verbose(true);
        
        // 平台特定编译器配置
        Self::configure_compiler(&mut config, &build_config.platform);
        
        // 严格模式
        if build_config.enable_strict {
            println!("cargo:warning=Building with STRICT CHECKS enabled (CI mode)");
            config.define("ENABLE_STRICT_CHECKS", "ON");
        }
        
        // macOS特殊配置
        if build_config.platform.is_macos() {
            config.define("VCPKG_INSTALL_OPTIONS", "--allow-unsupported");
        }
        
        Ok(Self {
            config,
            build_config: build_config.clone(),
        })
    }
    
    /// 配置平台特定编译器
    fn configure_compiler(config: &mut Config, platform: &Platform) {
        match platform {
            Platform::LinuxGnu => {
                config
                    .define("CMAKE_C_COMPILER", "/usr/bin/gcc")
                    .define("CMAKE_CXX_COMPILER", "/usr/bin/g++")
                    .define("CMAKE_MAKE_PROGRAM", "/usr/bin/make");
            }
            Platform::MacOsArm64 | Platform::MacOsX86_64 => {
                config
                    .define("CMAKE_C_COMPILER", "/usr/bin/clang")
                    .define("CMAKE_CXX_COMPILER", "/usr/bin/clang++")
                    .define("CMAKE_MAKE_PROGRAM", "/usr/bin/make");
            }
            Platform::WindowsMsvc => {
                // Windows使用默认MSVC编译器
            }
        }
    }
    
    /// 执行构建
    pub fn build(&mut self) -> PathBuf {
        let dst = self.config.build();
        println!("cargo:warning=CMake build directory: {}", dst.display());
        dst
    }
}
```

### 3.4 build/utils.rs - 工具函数

```rust
//! 构建工具函数

use crate::config::BuildConfig;
use std::fs;
use std::io;
use std::path::Path;

/// 递归复制目录
pub fn copy_dir_recursive(src: &Path, dst: &Path) -> io::Result<()> {
    if !dst.exists() {
        fs::create_dir_all(dst)?;
    }

    for entry in fs::read_dir(src)? {
        let entry = entry?;
        let path = entry.path();
        let dest_path = dst.join(entry.file_name());

        if path.is_dir() {
            copy_dir_recursive(&path, &dest_path)?;
        } else {
            fs::copy(&path, &dest_path)?;
        }
    }
    Ok(())
}

/// 创建vcpkg_installed目录符号链接
pub fn create_vcpkg_symlink(config: &BuildConfig) -> io::Result<()> {
    if !config.vcpkg_has_been_installed {
        return Ok(());
    }

    let root_vcpkg = config.source_dir.join("vcpkg_installed");
    if !root_vcpkg.exists() {
        return Ok(());
    }

    let build_vcpkg = config.out_dir.join("build").join("vcpkg_installed");
    
    // 确保父目录存在
    if let Some(parent) = build_vcpkg.parent() {
        fs::create_dir_all(parent)?;
    }

    if build_vcpkg.exists() {
        println!("cargo:warning=vcpkg_installed symlink already exists");
        return Ok(());
    }

    #[cfg(target_family = "unix")]
    {
        std::os::unix::fs::symlink(&root_vcpkg, &build_vcpkg)
            .map_err(|e| io::Error::new(
                io::ErrorKind::Other,
                format!("Failed to create Unix symlink: {}", e)
            ))?;
    }

    #[cfg(target_family = "windows")]
    {
        std::os::windows::fs::symlink_dir(&root_vcpkg, &build_vcpkg)
            .map_err(|e| io::Error::new(
                io::ErrorKind::Other,
                format!("Failed to create Windows symlink: {}", e)
            ))?;
    }

    println!("cargo:warning=Created vcpkg_installed symlink: {} -> {}", 
        build_vcpkg.display(), root_vcpkg.display());
    
    Ok(())
}

/// 导出compile_commands.json
pub fn export_compile_commands(out_dir: &Path) {
    let cmake_build_dir = out_dir.join("build");
    let src = cmake_build_dir.join("compile_commands.json");
    
    if !src.exists() {
        println!("cargo:warning=compile_commands.json not found at {}", src.display());
        return;
    }

    let cargo_manifest_dir = std::env::var("CARGO_MANIFEST_DIR")
        .unwrap_or_else(|_| ".".into());
    let dst_dir = Path::new(&cargo_manifest_dir).join("build");
    
    if let Err(err) = fs::create_dir_all(&dst_dir) {
        println!("cargo:warning=Failed to create build dir {}: {}", 
            dst_dir.display(), err);
        return;
    }

    let dst = dst_dir.join("compile_commands.json");
    match fs::copy(&src, &dst) {
        Ok(_) => println!("cargo:warning=Exported compile_commands.json to {}", 
            dst.display()),
        Err(err) => println!("cargo:warning=Failed to copy compile_commands.json: {}", 
            err),
    }
}

/// 复制GDAL数据文件
pub fn copy_gdal_data(config: &BuildConfig) {
    let gdal_data = config.vcpkg_share_dir().join("gdal");
    let out_dir = &config.target_dir;
    
    println!("cargo:warning=Copying GDAL data: {} -> {}", 
        gdal_data.display(), out_dir.display());
    
    if let Err(e) = copy_dir_recursive(&gdal_data, &out_dir.join("gdal")) {
        println!("cargo:warning=Failed to copy GDAL data: {}", e);
    }
}

/// 复制PROJ数据文件
pub fn copy_proj_data(config: &BuildConfig) {
    let proj_data = config.vcpkg_share_dir().join("proj");
    let out_dir = &config.target_dir;
    
    println!("cargo:warning=Copying PROJ data: {} -> {}", 
        proj_data.display(), out_dir.display());
    
    if let Err(e) = copy_dir_recursive(&proj_data, &out_dir.join("proj")) {
        println!("cargo:warning=Failed to copy PROJ data: {}", e);
    }
}

/// 复制OSG插件
pub fn copy_osg_plugins(config: &BuildConfig) {
    let plugins_src = config.vcpkg_lib_dir().join("osgPlugins-3.6.5");
    let out_dir = config.target_dir.join("osgPlugins-3.6.5");
    
    println!("cargo:warning=Copying OSG plugins: {} -> {}", 
        plugins_src.display(), out_dir.display());

    if !plugins_src.exists() {
        println!("cargo:warning=OSG plugins directory not found: {}", 
            plugins_src.display());
        return;
    }

    if let Err(e) = copy_dir_recursive(&plugins_src, &out_dir) {
        println!("cargo:warning=Failed to copy OSG plugins: {}", e);
    }
}
```

### 3.5 重构后的build.rs

```rust
//! 3dtiles项目构建脚本
//! 
//! 重构后版本：使用模块化设计，消除平台特定代码重复

// 引入构建模块
#[path = "build/mod.rs"]
mod build;

use build::{
    CMakeBuilder, 
    BuildConfig, 
    BuildError,
    copy_gdal_data, 
    copy_proj_data, 
    copy_osg_plugins,
    create_vcpkg_symlink,
    export_compile_commands,
};

fn main() -> Result<(), BuildError> {
    // 启用完整回溯信息
    std::env::set_var("RUST_BACKTRACE", "full");
    
    // 1. 从环境变量加载配置
    let config = BuildConfig::from_env()?;
    
    println!("cargo:warning=Building for platform: {:?}", config.platform);
    println!("cargo:warning=VCPKG_ROOT: {}", config.vcpkg_root.display());
    
    // 2. 创建vcpkg符号链接（如果需要）
    create_vcpkg_symlink(&config)?;
    
    // 3. 执行CMake构建
    let mut cmake = CMakeBuilder::new(&config)?;
    let _build_dir = cmake.build();
    
    // 4. 导出compile_commands.json
    export_compile_commands(&config.out_dir);
    
    // 5. 平台特定的库链接（保留原有println!方式）
    match config.platform {
        build::config::Platform::WindowsMsvc => link_windows(&config),
        build::config::Platform::LinuxGnu => link_linux(&config),
        build::config::Platform::MacOsArm64 | 
        build::config::Platform::MacOsX86_64 => link_macos(&config),
    }
    
    // 6. 复制数据文件
    copy_gdal_data(&config);
    copy_proj_data(&config);
    copy_osg_plugins(&config);
    
    println!("cargo:warning=Build completed successfully!");
    
    Ok(())
}

/// Windows平台库链接
fn link_windows(config: &BuildConfig) {
    // CMake构建输出
    println!("cargo:rustc-link-search=native={}/lib", config.out_dir.display());
    
    // Draco库路径
    println!("cargo:rustc-link-search=native={}/thirdparty/draco", 
        config.source_dir.display());
    
    // vcpkg库路径
    let vcpkg_lib = config.vcpkg_lib_dir();
    println!("cargo:rustc-link-search=native={}", vcpkg_lib.display());
    
    // 1. FFI static
    println!("cargo:rustc-link-lib=static=_3dtile");
    println!("cargo:rustc-link-lib=static=ufbx");
    println!("cargo:rustc-link-lib=static=draco");
    
    // 2. OSG
    println!("cargo:rustc-link-lib=osgUtil");
    println!("cargo:rustc-link-lib=osgDB");
    println!("cargo:rustc-link-lib=osg");
    
    // 3. OpenThreads
    println!("cargo:rustc-link-lib=OpenThreads");
    
    // 4. GDAL dependencies
    println!("cargo:rustc-link-lib=gdal");
    println!("cargo:rustc-link-lib=basisu_encoder");
    println!("cargo:rustc-link-lib=meshoptimizer");
    println!("cargo:rustc-link-lib=zstd");
    
    // 5. sqlite
    println!("cargo:rustc-link-lib=sqlite3");
    
    // GeographicLib (debug/release区分)
    let profile = std::env::var("PROFILE").unwrap_or_else(|_| "release".into());
    let is_debug = profile == "debug";
    let geolib_name = if is_debug {
        "GeographicLib_d-i"
    } else {
        "GeographicLib-i"
    };
    println!("cargo:warning=Building in {} mode, linking GeographicLib as: {}", 
        profile, geolib_name);
    println!("cargo:rustc-link-lib={}", geolib_name);
}

/// Linux平台库链接
fn link_linux(config: &BuildConfig) {
    // CMake构建输出
    println!("cargo:rustc-link-search=native={}/lib", config.out_dir.display());
    
    // Draco库路径
    println!("cargo:rustc-link-search=native={}/thirdparty/draco", 
        config.source_dir.display());
    
    // vcpkg库路径
    let vcpkg_lib = config.vcpkg_lib_dir();
    println!("cargo:rustc-link-search=native={}", vcpkg_lib.display());
    
    // OSG插件路径
    let osg_plugins = vcpkg_lib.join("osgPlugins-3.6.5");
    println!("cargo:rustc-link-search=native={}", osg_plugins.display());
    
    // 0. System C++ library first
    println!("cargo:rustc-link-lib=stdc++");
    println!("cargo:rustc-link-lib=z");
    
    // 1. FFI static
    println!("cargo:rustc-link-lib=static=_3dtile");
    println!("cargo:rustc-link-lib=static=ufbx");
    println!("cargo:rustc-link-lib=static=draco");
    
    // 2. OSG
    println!("cargo:rustc-link-lib=GL");
    println!("cargo:rustc-link-lib=X11");
    println!("cargo:rustc-link-lib=Xi");
    println!("cargo:rustc-link-lib=Xrandr");
    println!("cargo:rustc-link-lib=dl");
    println!("cargo:rustc-link-lib=pthread");
    println!("cargo:rustc-link-lib=osgdb_jpeg");
    println!("cargo:rustc-link-lib=osgdb_tga");
    println!("cargo:rustc-link-lib=osgdb_rgb");
    println!("cargo:rustc-link-lib=osgdb_png");
    println!("cargo:rustc-link-lib=osgdb_osg");
    println!("cargo:rustc-link-lib=osgdb_serializers_osg");
    println!("cargo:rustc-link-lib=osgUtil");
    println!("cargo:rustc-link-lib=osgDB");
    println!("cargo:rustc-link-lib=osg");
    println!("cargo:rustc-link-lib=OpenThreads");
    
    // 3. GDAL dependencies
    println!("cargo:rustc-link-lib=gdal");
    println!("cargo:rustc-link-lib=geos_c");
    println!("cargo:rustc-link-lib=geos");
    println!("cargo:rustc-link-lib=proj");
    println!("cargo:rustc-link-lib=sqlite3");
    println!("cargo:rustc-link-lib=expat");
    println!("cargo:rustc-link-lib=curl");
    println!("cargo:rustc-link-lib=ssl");
    println!("cargo:rustc-link-lib=crypto");
    println!("cargo:rustc-link-lib=uriparser");
    println!("cargo:rustc-link-lib=kmlbase");
    println!("cargo:rustc-link-lib=kmlengine");
    println!("cargo:rustc-link-lib=kmldom");
    println!("cargo:rustc-link-lib=kmlconvenience");
    println!("cargo:rustc-link-lib=Lerc");
    println!("cargo:rustc-link-lib=json-c");
    println!("cargo:rustc-link-lib=sharpyuv");
    
    // 4. Image / compression libraries
    println!("cargo:rustc-link-lib=geotiff");
    println!("cargo:rustc-link-lib=gif");
    println!("cargo:rustc-link-lib=jpeg");
    println!("cargo:rustc-link-lib=png");
    println!("cargo:rustc-link-lib=tiff");
    println!("cargo:rustc-link-lib=webp");
    println!("cargo:rustc-link-lib=xml2");
    println!("cargo:rustc-link-lib=lzma");
    println!("cargo:rustc-link-lib=openjp2");
    println!("cargo:rustc-link-lib=qhullstatic_r");
    println!("cargo:rustc-link-lib=minizip");
    println!("cargo:rustc-link-lib=spatialite");
    println!("cargo:rustc-link-lib=freexl");
    println!("cargo:rustc-link-lib=basisu_encoder");
    println!("cargo:rustc-link-lib=meshoptimizer");
    println!("cargo:rustc-link-lib=zstd");
    
    // 5. GeographicLib
    println!("cargo:rustc-link-lib=GeographicLib");
}

/// macOS平台库链接
fn link_macos(config: &BuildConfig) {
    // CMake构建输出
    println!("cargo:rustc-link-search=native={}/lib", config.out_dir.display());
    
    // Draco库路径
    println!("cargo:rustc-link-search=native={}/thirdparty/draco", 
        config.source_dir.display());
    
    // vcpkg库路径
    let vcpkg_lib = config.vcpkg_lib_dir();
    println!("cargo:rustc-link-search=native={}", vcpkg_lib.display());
    
    // OSG插件路径
    let osg_plugins = vcpkg_lib.join("osgPlugins-3.6.5");
    println!("cargo:rustc-link-search=native={}", osg_plugins.display());
    
    // 0. System C++ library first
    println!("cargo:rustc-link-lib=c++");
    println!("cargo:rustc-link-lib=z");
    
    // 1. FFI static
    println!("cargo:rustc-link-lib=static=_3dtile");
    println!("cargo:rustc-link-lib=static=ufbx");
    println!("cargo:rustc-link-lib=static=draco");
    
    // 2. OSG
    println!("cargo:rustc-link-lib=osgdb_jpeg");
    println!("cargo:rustc-link-lib=osgdb_tga");
    println!("cargo:rustc-link-lib=osgdb_rgb");
    println!("cargo:rustc-link-lib=osgdb_png");
    println!("cargo:rustc-link-lib=osgdb_osg");
    println!("cargo:rustc-link-lib=osgdb_serializers_osg");
    println!("cargo:rustc-link-lib=osgUtil");
    println!("cargo:rustc-link-lib=osgDB");
    println!("cargo:rustc-link-lib=osg");
    println!("cargo:rustc-link-lib=OpenThreads");
    
    // 3. GDAL dependencies
    println!("cargo:rustc-link-lib=gdal");
    println!("cargo:rustc-link-lib=geos_c");
    println!("cargo:rustc-link-lib=geos");
    println!("cargo:rustc-link-lib=proj");
    println!("cargo:rustc-link-lib=sqlite3");
    println!("cargo:rustc-link-lib=expat");
    println!("cargo:rustc-link-lib=curl");
    println!("cargo:rustc-link-lib=ssl");
    println!("cargo:rustc-link-lib=crypto");
    println!("cargo:rustc-link-lib=kmlbase");
    println!("cargo:rustc-link-lib=kmlengine");
    println!("cargo:rustc-link-lib=kmldom");
    println!("cargo:rustc-link-lib=kmlconvenience");
    println!("cargo:rustc-link-lib=Lerc");
    println!("cargo:rustc-link-lib=json-c");
    println!("cargo:rustc-link-lib=sharpyuv");
    
    // 4. Image / compression libraries
    println!("cargo:rustc-link-lib=geotiff");
    println!("cargo:rustc-link-lib=gif");
    println!("cargo:rustc-link-lib=jpeg");
    println!("cargo:rustc-link-lib=png");
    println!("cargo:rustc-link-lib=tiff");
    println!("cargo:rustc-link-lib=webp");
    println!("cargo:rustc-link-lib=xml2");
    println!("cargo:rustc-link-lib=lzma");
    println!("cargo:rustc-link-lib=openjp2");
    println!("cargo:rustc-link-lib=qhullstatic_r");
    println!("cargo:rustc-link-lib=minizip");
    println!("cargo:rustc-link-lib=spatialite");
    println!("cargo:rustc-link-lib=freexl");
    println!("cargo:rustc-link-lib=basisu_encoder");
    println!("cargo:rustc-link-lib=meshoptimizer");
    println!("cargo:rustc-link-lib=zstd");
    
    // 5. GeographicLib
    println!("cargo:rustc-link-lib=GeographicLib");
    
    // 6. System frameworks
    println!("cargo:rustc-link-lib=framework=Security");
    println!("cargo:rustc-link-lib=framework=CoreFoundation");
    println!("cargo:rustc-link-lib=framework=Foundation");
    println!("cargo:rustc-link-lib=framework=OpenGL");
    println!("cargo:rustc-link-lib=framework=AppKit");
    println!("cargo:rustc-link-lib=framework=SystemConfiguration");
    
    // 7. Additional linker flags
    println!("cargo:rustc-link-arg=-ObjC");
    println!("cargo:rustc-link-arg=-all_load");
}
```

---

## 四、Cargo.toml更新

```toml
[package]
name = "_3dtile"
version = "0.1.0"
authors = ["fanzhenhua <fanzhenhua@superengine.com.cn>"]
edition = "2021"

[dependencies]
libc = "0.2"
clap = "4.5.53"
chrono = "0.4"
rayon = "1.0"
serde = { version = "1.0", features = ["derive"] }
serde_json = "1.0"
serde-xml-rs = "0.8.2"
log = "0.4"
env_logger = "0.11.8"
byteorder = "1.2"
thiserror = "1.0"  # 新增：用于错误处理

[build-dependencies]
cmake = "0.1"
pkg-config = "0.3"
thiserror = "1.0"  # 新增
```

---

## 五、目录结构

```
/Users/wallance/Developer/cim/thirdparty/3dtiles/
├── build.rs                    # 简化后的入口（~200行）
├── Cargo.toml                  # 添加thiserror依赖
├── build/                      # 新增：构建模块目录
│   ├── mod.rs                  # 模块导出
│   ├── config.rs               # BuildConfig, Platform, BuildError
│   ├── cmake.rs                # CMakeBuilder
│   └── utils.rs                # 工具函数
├── CMakeLists.txt              # 不变
└── src/                        # 源代码目录
    └── ...
```

---

## 六、优化效果对比

| 指标 | 重构前 | 重构后 | 改善 |
|-----|-------|-------|-----|
| build.rs代码行数 | ~731行 | ~200行 | **减少73%** |
| 平台构建函数 | 4个（各~150行） | 3个链接函数（各~50行） | **消除重复** |
| 错误处理 | 混合unwrap/expect | 统一Result类型 | **类型安全** |
| 新增平台支持 | 需复制~150行代码 | 添加1个链接函数 | **维护性↑** |
| 可读性 | 低（逻辑分散） | 高（结构清晰） | **可维护性↑** |

---

## 七、后续优化建议

### 7.1 独立C++编译模式

支持仅生成CMake配置，不执行构建，便于CI/CD分离构建：

```rust
enum BuildMode {
    Integrated,      // 正常模式：Cargo驱动CMake
    CppOnly,         // 仅生成CMake配置
    Prebuilt(PathBuf), // 使用预构建的C++库
}
```

### 7.2 vcpkg自动链接

考虑使用`vcpkg` crate自动处理依赖链接：

```rust
// [build-dependencies]
// vcpkg = "0.2"

let lib = vcpkg::Config::new()
    .emit_includes(true)
    .find_package("gdal")?;
```

### 7.3 构建缓存

添加构建缓存支持，避免重复编译未变更的C++代码。

---

## 八、实施检查清单

- [ ] 创建`build/`目录和模块文件
- [ ] 更新`Cargo.toml`添加`thiserror`依赖
- [ ] 迁移工具函数到`build/utils.rs`
- [ ] 实现`BuildConfig`和`Platform`枚举
- [ ] 实现`CMakeBuilder`
- [ ] 重构`build.rs`主函数
- [ ] 测试Windows构建
- [ ] 测试Linux构建
- [ ] 测试macOS ARM64构建
- [ ] 测试macOS x86_64构建

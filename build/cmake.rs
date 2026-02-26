//! CMake构建抽象

use crate::build::config::{BuildConfig, BuildError, Platform};
use cmake::Config;
use std::path::PathBuf;

/// CMake构建器
pub struct CMakeBuilder {
    config: Config,
}

impl CMakeBuilder {
    /// 创建新的CMake构建器
    pub fn new(build_config: &BuildConfig) -> Result<Self, BuildError> {
        let mut config = Config::new(&build_config.source_dir);

        // 配置基础选项
        config
            .define(
                "CMAKE_TOOLCHAIN_FILE",
                format!(
                    "{}/scripts/buildsystems/vcpkg.cmake",
                    build_config.vcpkg_root.display()
                ),
            )
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

        Ok(Self { config })
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
        println!("cmake dst = {}", dst.display());
        dst
    }
}

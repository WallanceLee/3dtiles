extern crate cmake;

use std::path::Path;

// 引入构建模块
#[path = "build/mod.rs"]
mod build;

use build::{
    copy_gdal_data, copy_osg_plugins, copy_proj_data, create_vcpkg_symlink,
    export_compile_commands, link_linux, link_macos, link_windows, BuildConfig, BuildError,
    CMakeBuilder,
};

fn main() -> Result<(), BuildError> {
    // 启用完整回溯信息
    std::env::set_var("RUST_BACKTRACE", "full");

    // 1. 从环境变量加载配置
    let config = BuildConfig::from_env()?;

    println!(
        "cargo:warning=Building for platform: {:?}",
        config.platform
    );
    println!("cargo:warning=VCPKG_ROOT: {}", config.vcpkg_root.display());

    // 2. 创建vcpkg符号链接（如果需要）
    create_vcpkg_symlink(&config)?;

    // 3. 执行CMake构建
    let mut cmake = CMakeBuilder::new(&config)?;
    let _build_dir = cmake.build();

    // 4. 导出compile_commands.json
    export_compile_commands(Path::new(&config.out_dir));

    // 5. 平台特定的库链接
    match config.platform {
        build::config::Platform::WindowsMsvc => link_windows(&config),
        build::config::Platform::LinuxGnu => link_linux(&config),
        build::config::Platform::MacOsArm64 | build::config::Platform::MacOsX86_64 => {
            link_macos(&config)
        }
    }

    // 6. 复制数据文件
    copy_gdal_data(&config);
    copy_proj_data(&config);
    copy_osg_plugins(&config);

    println!("cargo:warning=Build completed successfully!");

    Ok(())
}

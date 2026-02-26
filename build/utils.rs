//! 构建工具函数

use crate::build::config::BuildConfig;
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
        println!(
            "cargo:warning=vcpkg_installed symlink already exists, no need to create again.: {:?}",
            build_vcpkg
        );
        return Ok(());
    }

    #[cfg(target_family = "unix")]
    {
        std::os::unix::fs::symlink(&root_vcpkg, &build_vcpkg).map_err(|e| {
            io::Error::other(format!(
                "Failed to create symlink for Unix-like os from {} -> {}: {}",
                root_vcpkg.display(),
                build_vcpkg.display(),
                e
            ))
        })?;
    }

    #[cfg(target_family = "windows")]
    {
        std::os::windows::fs::symlink_dir(&root_vcpkg, &build_vcpkg).map_err(|e| {
            io::Error::other(format!(
                "Failed to create symlink for windows os from {} -> {}: {}",
                root_vcpkg.display(),
                build_vcpkg.display(),
                e
            ))
        })?;
    }

    println!(
        "cargo:warning=Created vcpkg_installed symlink: {} -> {}",
        build_vcpkg.display(),
        root_vcpkg.display()
    );

    Ok(())
}

/// 导出compile_commands.json
pub fn export_compile_commands(out_dir: &Path) {
    let cmake_build_dir = out_dir.join("build");
    let src = cmake_build_dir.join("compile_commands.json");

    if !src.exists() {
        println!(
            "cargo:warning=compile_commands.json not found at {}",
            src.display()
        );
        return;
    }

    let cargo_manifest_dir = std::env::var("CARGO_MANIFEST_DIR").unwrap_or_else(|_| ".".into());
    let dst_dir = Path::new(&cargo_manifest_dir).join("build");

    if let Err(err) = fs::create_dir_all(&dst_dir) {
        println!(
            "cargo:warning=failed to create build dir {}: {}",
            dst_dir.display(),
            err
        );
        return;
    }

    let dst = dst_dir.join("compile_commands.json");
    match fs::copy(&src, &dst) {
        Ok(_) => println!(
            "cargo:warning=exported compile_commands.json to {}",
            dst.display()
        ),
        Err(err) => println!(
            "cargo:warning=failed to copy compile_commands.json to {}: {}",
            dst.display(),
            err
        ),
    }
}

/// 复制GDAL数据文件
pub fn copy_gdal_data(config: &BuildConfig) {
    let gdal_data = config.vcpkg_share_dir().join("gdal");
    let out_dir = &config.target_dir;

    println!(
        "gdal_data -> {}, out_dir -> {}",
        gdal_data.display(),
        out_dir.display()
    );

    if let Err(e) = copy_dir_recursive(&gdal_data, &out_dir.join("gdal")) {
        println!("cargo:warning=Failed to copy GDAL data: {}", e);
    }
}

/// 复制PROJ数据文件
pub fn copy_proj_data(config: &BuildConfig) {
    let proj_data = config.vcpkg_share_dir().join("proj");
    let out_dir = &config.target_dir;

    println!(
        "proj_data -> {}, out_dir -> {}",
        proj_data.display(),
        out_dir.display()
    );

    if let Err(e) = copy_dir_recursive(&proj_data, &out_dir.join("proj")) {
        println!("cargo:warning=Failed to copy PROJ data: {}", e);
    }
}

/// 复制OSG插件
pub fn copy_osg_plugins(config: &BuildConfig) {
    let plugins_src = config.vcpkg_lib_dir().join("osgPlugins-3.6.5");
    let out_dir = config.target_dir.join("osgPlugins-3.6.5");

    println!(
        "osg_plugins -> {}, out_dir -> {}",
        plugins_src.display(),
        out_dir.display()
    );

    if !plugins_src.exists() {
        println!(
            "cargo:warning=OSG plugins directory not found: {}",
            plugins_src.display()
        );
        return;
    }

    if let Err(e) = copy_dir_recursive(&plugins_src, &out_dir) {
        println!("cargo:warning=Failed to copy OSG plugins: {}", e);
    }
}

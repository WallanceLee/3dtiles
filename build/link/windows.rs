//! Windows平台库链接配置

use crate::build::config::BuildConfig;
use std::env;

/// Windows平台库链接
pub fn link_libraries(config: &BuildConfig) {
    // CMake构建输出
    println!("cargo:rustc-link-search=native={}/lib", config.out_dir.display());

    // Draco库路径
    println!(
        "cargo:rustc-link-search=native={}/thirdparty/draco",
        config.source_dir.display()
    );

    // vcpkg库路径
    let vcpkg_installed_dir = config.vcpkg_installed_dir();
    let vcpkg_installed_lib_dir = vcpkg_installed_dir.join("lib");
    println!("cargo:rustc-link-search=native={}", vcpkg_installed_lib_dir.display());

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
    let profile = env::var("PROFILE").unwrap_or_else(|_| "release".into());
    let is_debug = profile == "debug";
    let geolib_name = if is_debug {
        "GeographicLib_d-i"
    } else {
        "GeographicLib-i"
    };
    println!(
        "cargo:warning=Building in {} mode, linking GeographicLib as: {}",
        profile, geolib_name
    );
    println!("cargo:rustc-link-lib={}", geolib_name);
}

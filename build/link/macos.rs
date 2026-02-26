//! macOS平台库链接配置

use crate::build::config::BuildConfig;

/// macOS平台库链接
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

    // OSG插件路径
    let osg_plugins_lib = vcpkg_installed_lib_dir.join("osgPlugins-3.6.5");
    println!("cargo:rustc-link-search=native={}", osg_plugins_lib.display());

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

//! Build script helper modules
//!
//! 提供跨平台C++构建的抽象和工具函数

pub mod cmake;
pub mod config;
pub mod link;
pub mod utils;

pub use cmake::CMakeBuilder;
pub use config::{BuildConfig, BuildError};
pub use link::{link_linux, link_macos, link_windows};
pub use utils::{
    copy_gdal_data, copy_osg_plugins, copy_proj_data, create_vcpkg_symlink, export_compile_commands,
};

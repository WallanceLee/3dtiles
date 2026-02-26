//! 平台特定的库链接配置

pub mod linux;
pub mod macos;
pub mod windows;

pub use linux::link_libraries as link_linux;
pub use macos::link_libraries as link_macos;
pub use windows::link_libraries as link_windows;

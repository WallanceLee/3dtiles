//! 统一数据转换器接口
//!
//! 提供 DataConverter trait 统一不同数据源的转换逻辑
//! 支持 Shapefile、FBX、OSGB 等格式

use std::error::Error;
use std::path::Path;

/// 转换配置选项
#[derive(Debug, Clone, Default)]
#[allow(dead_code)]
pub struct ConversionConfig {
    /// 最大层级深度
    pub max_level: Option<i32>,
    /// 经度（原点）
    pub longitude: f64,
    /// 纬度（原点）
    pub latitude: f64,
    /// 高度（原点）- 用于FBX等绝对高度场景
    pub height: f64,
    /// 高度偏移 - 用于OSGB等相对高度偏移场景
    pub height_offset: Option<f64>,
    /// 是否启用 Draco 压缩
    pub enable_draco: bool,
    /// 是否启用网格简化
    pub enable_simplify: bool,
    /// 是否启用纹理压缩
    pub enable_texture_compress: bool,
    /// 是否启用 LOD
    pub enable_lod: bool,
    /// 是否启用 unlit 材质
    pub enable_unlit: bool,
    /// Shapefile 高度字段
    pub height_field: Option<String>,
}

#[allow(dead_code)]
impl ConversionConfig {
    /// 创建默认配置
    pub fn new() -> Self {
        Self::default()
    }

    /// 设置最大层级
    pub fn with_max_level(mut self, level: i32) -> Self {
        self.max_level = Some(level);
        self
    }

    /// 设置原点坐标（FBX等绝对高度场景）
    pub fn with_origin(mut self, lon: f64, lat: f64, height: f64) -> Self {
        self.longitude = lon;
        self.latitude = lat;
        self.height = height;
        self
    }

    /// 设置原点坐标和高度偏移（OSGB等相对偏移场景）
    pub fn with_origin_offset(mut self, lon: f64, lat: f64, offset: f64) -> Self {
        self.longitude = lon;
        self.latitude = lat;
        self.height_offset = Some(offset);
        self
    }

    /// 启用所有优化选项
    pub fn with_all_optimizations(mut self) -> Self {
        self.enable_draco = true;
        self.enable_simplify = true;
        self.enable_texture_compress = true;
        self.enable_lod = true;
        self
    }
}

/// 数据转换器 trait
///
/// 所有数据源转换器都应实现此 trait
#[allow(dead_code)]
pub trait DataConverter {
    /// 执行转换
    ///
    /// # Arguments
    /// * `input` - 输入文件或目录路径
    /// * `output` - 输出目录路径
    /// * `config` - 转换配置
    ///
    /// # Returns
    /// * `Ok(())` - 转换成功
    /// * `Err(Box<dyn Error>)` - 转换失败
    fn convert(
        &self,
        input: &str,
        output: &str,
        config: &ConversionConfig,
    ) -> Result<(), Box<dyn Error>>;

    /// 获取支持的文件扩展名列表
    fn supported_extensions(&self) -> Vec<&str>;

    /// 检查是否支持指定扩展名
    fn supports_extension(&self, ext: &str) -> bool {
        let ext = if ext.starts_with('.') {
            ext.to_lowercase()
        } else {
            format!(".{}", ext.to_lowercase())
        };
        self.supported_extensions()
            .iter()
            .any(|&e| e.to_lowercase() == ext)
    }

    /// 获取格式名称
    fn format_name(&self) -> &str;

    /// 根据文件路径自动检测是否支持
    fn can_convert(&self, path: &str) -> bool {
        Path::new(path)
            .extension()
            .and_then(|e| e.to_str())
            .map(|ext| self.supports_extension(ext))
            .unwrap_or(false)
    }
}

/// Shapefile 转换器
#[allow(dead_code)]
pub struct ShapefileConverter;

#[allow(dead_code)]
impl ShapefileConverter {
    pub fn new() -> Self {
        Self
    }
}

impl Default for ShapefileConverter {
    fn default() -> Self {
        Self::new()
    }
}

impl DataConverter for ShapefileConverter {
    fn convert(
        &self,
        input: &str,
        output: &str,
        config: &ConversionConfig,
    ) -> Result<(), Box<dyn Error>> {
        let success = crate::shape::shape_batch_convert(
            input,
            output,
            config.height_field.as_deref().unwrap_or(""),
            config.enable_lod,
            config.enable_simplify,
            config.enable_draco,
        );
        if success {
            Ok(())
        } else {
            Err("Shapefile conversion failed".into())
        }
    }

    fn supported_extensions(&self) -> Vec<&str> {
        vec![".shp", ".shx", ".dbf"]
    }

    fn format_name(&self) -> &str {
        "shapefile"
    }
}

/// FBX 转换器
#[allow(dead_code)]
pub struct FBXConverter;

#[allow(dead_code)]
impl FBXConverter {
    pub fn new() -> Self {
        Self
    }
}

impl Default for FBXConverter {
    fn default() -> Self {
        Self::new()
    }
}

impl DataConverter for FBXConverter {
    fn convert(
        &self,
        input: &str,
        output: &str,
        config: &ConversionConfig,
    ) -> Result<(), Box<dyn Error>> {
        crate::fbx::convert_fbx(
            input,
            output,
            config.max_level,
            config.enable_texture_compress,
            config.enable_simplify,
            config.enable_draco,
            config.enable_unlit,
            config.longitude,
            config.latitude,
            config.height,
            config.enable_lod,
        )
    }

    fn supported_extensions(&self) -> Vec<&str> {
        vec![".fbx", ".obj"]
    }

    fn format_name(&self) -> &str {
        "fbx"
    }
}

/// OSGB 转换器
#[allow(dead_code)]
pub struct OSGBConverter;

#[allow(dead_code)]
impl OSGBConverter {
    pub fn new() -> Self {
        Self
    }
}

impl Default for OSGBConverter {
    fn default() -> Self {
        Self::new()
    }
}

impl DataConverter for OSGBConverter {
    fn convert(
        &self,
        input: &str,
        output: &str,
        config: &ConversionConfig,
    ) -> Result<(), Box<dyn Error>> {
        use std::path::Path;
        crate::osgb::osgb_batch_convert(
            Path::new(input),
            Path::new(output),
            config.max_level,
            config.longitude,
            config.latitude,
            config.height_offset, // 使用height_offset作为region_offset
            None, // enu_offset
            None, // origin_height
            config.enable_texture_compress,
            config.enable_simplify,
            config.enable_draco,
            true, // enable_unlit (OSGB 默认启用)
        )
    }

    fn supported_extensions(&self) -> Vec<&str> {
        vec![".osgb"]
    }

    fn format_name(&self) -> &str {
        "osgb"
    }
}

/// 转换器工厂
///
/// 根据格式名称或文件扩展名创建对应的转换器
#[allow(dead_code)]
pub struct ConverterFactory;

#[allow(dead_code)]
impl ConverterFactory {
    /// 根据格式名称创建转换器
    pub fn create(format: &str) -> Option<Box<dyn DataConverter>> {
        match format.to_lowercase().as_str() {
            "shapefile" | "shape" | "shp" => Some(Box::new(ShapefileConverter::new())),
            "fbx" => Some(Box::new(FBXConverter::new())),
            "osgb" => Some(Box::new(OSGBConverter::new())),
            _ => None,
        }
    }

    /// 根据文件扩展名创建转换器
    pub fn create_from_extension(ext: &str) -> Option<Box<dyn DataConverter>> {
        let ext = ext.to_lowercase();
        let ext = if ext.starts_with('.') {
            ext
        } else {
            format!(".{}", ext)
        };

        match ext.as_str() {
            ".shp" => Some(Box::new(ShapefileConverter::new())),
            ".fbx" | ".obj" => Some(Box::new(FBXConverter::new())),
            ".osgb" => Some(Box::new(OSGBConverter::new())),
            _ => None,
        }
    }

    /// 根据文件路径自动检测并创建转换器
    pub fn create_from_path(path: &str) -> Option<Box<dyn DataConverter>> {
        Path::new(path)
            .extension()
            .and_then(|e| e.to_str())
            .and_then(|ext| Self::create_from_extension(ext))
    }

    /// 列出所有支持的格式
    pub fn list_supported_formats() -> Vec<&'static str> {
        vec!["shapefile", "fbx", "osgb"]
    }

    /// 检查是否支持指定格式
    pub fn is_supported(format: &str) -> bool {
        Self::create(format).is_some()
    }
}

/// 便捷的转换函数
///
/// 自动检测输入格式并执行转换
#[allow(dead_code)]
pub fn convert_auto(
    input: &str,
    output: &str,
    config: &ConversionConfig,
) -> Result<(), Box<dyn Error>> {
    let converter = ConverterFactory::create_from_path(input)
        .ok_or_else(|| format!("Unsupported file format: {}", input))?;

    log::info!(
        "Auto-detected format: {}, starting conversion...",
        converter.format_name()
    );

    converter.convert(input, output, config)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_shapefile_converter() {
        let converter = ShapefileConverter::new();
        assert_eq!(converter.format_name(), "shapefile");
        assert!(converter.supports_extension("shp"));
        assert!(converter.supports_extension(".shp"));
        assert!(!converter.supports_extension("fbx"));
    }

    #[test]
    fn test_fbx_converter() {
        let converter = FBXConverter::new();
        assert_eq!(converter.format_name(), "fbx");
        assert!(converter.supports_extension("fbx"));
        assert!(converter.supports_extension("obj"));
    }

    #[test]
    fn test_factory_create() {
        assert!(ConverterFactory::create("shapefile").is_some());
        assert!(ConverterFactory::create("fbx").is_some());
        assert!(ConverterFactory::create("unknown").is_none());
    }

    #[test]
    fn test_factory_from_extension() {
        assert!(ConverterFactory::create_from_extension("shp").is_some());
        assert!(ConverterFactory::create_from_extension(".fbx").is_some());
        assert!(ConverterFactory::create_from_extension("xyz").is_none());
    }

    #[test]
    fn test_factory_from_path() {
        assert!(ConverterFactory::create_from_path("/path/to/data.shp").is_some());
        assert!(ConverterFactory::create_from_path("/path/to/model.fbx").is_some());
        assert!(ConverterFactory::create_from_path("/path/to/model.unknown").is_none());
    }
}

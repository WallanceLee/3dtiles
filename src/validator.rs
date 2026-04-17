//! 坐标参数验证模块
//!
//! 提供经纬度和高度参数的验证功能
//! 支持 WGS84 坐标系的范围验证

#![allow(dead_code)]

use std::fmt;

/// 验证错误类型
#[derive(Debug, Clone, PartialEq)]
pub enum ValidationError {
    /// 经度超出范围
    InvalidLongitude { value: f64, range: (f64, f64) },
    /// 纬度超出范围
    InvalidLatitude { value: f64, range: (f64, f64) },
    /// 高度超出合理范围
    InvalidHeight { value: f64, reason: String },
    /// 高度偏移超出合理范围
    InvalidHeightOffset { value: f64, reason: String },
    /// 缺少必需参数
    MissingParameter { name: String },
    /// 参数组合无效
    InvalidCombination { message: String },
}

impl fmt::Display for ValidationError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            ValidationError::InvalidLongitude { value, range } => {
                write!(f, "经度 {} 超出有效范围 [{}, {}]", value, range.0, range.1)
            }
            ValidationError::InvalidLatitude { value, range } => {
                write!(f, "纬度 {} 超出有效范围 [{}, {}]", value, range.0, range.1)
            }
            ValidationError::InvalidHeight { value, reason } => {
                write!(f, "高度 {} 无效: {}", value, reason)
            }
            ValidationError::InvalidHeightOffset { value, reason } => {
                write!(f, "高度偏移 {} 无效: {}", value, reason)
            }
            ValidationError::MissingParameter { name } => {
                write!(f, "缺少必需参数: {}", name)
            }
            ValidationError::InvalidCombination { message } => {
                write!(f, "参数组合无效: {}", message)
            }
        }
    }
}

impl std::error::Error for ValidationError {}

/// 坐标验证结果
pub type ValidationResult<T> = Result<T, ValidationError>;

/// WGS84 坐标验证器
pub struct CoordinateValidator;

impl CoordinateValidator {
    /// 创建新的验证器
    pub fn new() -> Self {
        Self
    }

    /// 验证经度值
    ///
    /// # Arguments
    /// * `longitude` - 经度值（度）
    ///
    /// # Returns
    /// * `Ok(())` - 验证通过
    /// * `Err(ValidationError)` - 验证失败
    ///
    /// # Examples
    /// ```
    /// use crate::validator::CoordinateValidator;
    ///
    /// let validator = CoordinateValidator::new();
    /// assert!(validator.validate_longitude(113.5).is_ok());
    /// assert!(validator.validate_longitude(-181.0).is_err());
    /// ```
    pub fn validate_longitude(&self, longitude: f64) -> ValidationResult<()> {
        const MIN_LON: f64 = -180.0;
        const MAX_LON: f64 = 180.0;

        if longitude.is_nan() {
            return Err(ValidationError::InvalidLongitude {
                value: longitude,
                range: (MIN_LON, MAX_LON),
            });
        }

        if longitude < MIN_LON || longitude > MAX_LON {
            return Err(ValidationError::InvalidLongitude {
                value: longitude,
                range: (MIN_LON, MAX_LON),
            });
        }

        Ok(())
    }

    /// 验证纬度值
    ///
    /// # Arguments
    /// * `latitude` - 纬度值（度）
    ///
    /// # Returns
    /// * `Ok(())` - 验证通过
    /// * `Err(ValidationError)` - 验证失败
    pub fn validate_latitude(&self, latitude: f64) -> ValidationResult<()> {
        const MIN_LAT: f64 = -90.0;
        const MAX_LAT: f64 = 90.0;

        if latitude.is_nan() {
            return Err(ValidationError::InvalidLatitude {
                value: latitude,
                range: (MIN_LAT, MAX_LAT),
            });
        }

        if latitude < MIN_LAT || latitude > MAX_LAT {
            return Err(ValidationError::InvalidLatitude {
                value: latitude,
                range: (MIN_LAT, MAX_LAT),
            });
        }

        Ok(())
    }

    /// 验证高度值
    ///
    /// # Arguments
    /// * `height` - 高度值（米）
    ///
    /// # Returns
    /// * `Ok(())` - 验证通过
    /// * `Err(ValidationError)` - 验证失败
    pub fn validate_height(&self, height: f64) -> ValidationResult<()> {
        // 地球表面最低点和最高点的大致范围
        const MIN_HEIGHT: f64 = -12_000.0; // 马里亚纳海沟深度约 -11,000 米
        const MAX_HEIGHT: f64 = 9_000.0;   // 珠穆朗玛峰高度约 8,848 米

        if height.is_nan() || height.is_infinite() {
            return Err(ValidationError::InvalidHeight {
                value: height,
                reason: "高度值必须是有限数值".to_string(),
            });
        }

        if height < MIN_HEIGHT || height > MAX_HEIGHT {
            return Err(ValidationError::InvalidHeight {
                value: height,
                reason: format!("高度应在 [{}, {}] 米范围内", MIN_HEIGHT, MAX_HEIGHT),
            });
        }

        Ok(())
    }

    /// 验证高度偏移值
    ///
    /// # Arguments
    /// * `offset` - 高度偏移值（米）
    ///
    /// # Returns
    /// * `Ok(())` - 验证通过
    /// * `Err(ValidationError)` - 验证失败
    pub fn validate_height_offset(&self, offset: f64) -> ValidationResult<()> {
        // 高度偏移的合理范围（±10公里）
        const MAX_OFFSET: f64 = 10_000.0;

        if offset.is_nan() || offset.is_infinite() {
            return Err(ValidationError::InvalidHeightOffset {
                value: offset,
                reason: "偏移值必须是有限数值".to_string(),
            });
        }

        if offset.abs() > MAX_OFFSET {
            return Err(ValidationError::InvalidHeightOffset {
                value: offset,
                reason: format!("偏移值绝对值不应超过 {} 米", MAX_OFFSET),
            });
        }

        Ok(())
    }

    /// 验证完整的坐标点
    ///
    /// # Arguments
    /// * `longitude` - 经度（度）
    /// * `latitude` - 纬度（度）
    /// * `height` - 高度（米，可选）
    ///
    /// # Returns
    /// * `Ok(())` - 验证通过
    /// * `Err(ValidationError)` - 验证失败
    pub fn validate_coordinate(
        &self,
        longitude: f64,
        latitude: f64,
        height: Option<f64>,
    ) -> ValidationResult<()> {
        self.validate_longitude(longitude)?;
        self.validate_latitude(latitude)?;

        if let Some(h) = height {
            self.validate_height(h)?;
        }

        Ok(())
    }

    /// 验证 OSGB 转换参数
    ///
    /// # Arguments
    /// * `longitude` - 经度（可选）
    /// * `latitude` - 纬度（可选）
    /// * `height_offset` - 高度偏移（可选）
    ///
    /// # Returns
    /// * `Ok(())` - 验证通过
    /// * `Err(ValidationError)` - 验证失败
    pub fn validate_osgb_params(
        &self,
        longitude: Option<f64>,
        latitude: Option<f64>,
        height_offset: Option<f64>,
    ) -> ValidationResult<()> {
        // OSGB 转换中，经纬度是可选的（可以从 metadata.xml 读取）
        if let Some(lon) = longitude {
            self.validate_longitude(lon)?;
        }

        if let Some(lat) = latitude {
            self.validate_latitude(lat)?;
        }

        // 如果提供了经纬度，必须同时提供
        if longitude.is_some() != latitude.is_some() {
            return Err(ValidationError::InvalidCombination {
                message: "经度和纬度必须同时提供或同时省略".to_string(),
            });
        }

        if let Some(offset) = height_offset {
            self.validate_height_offset(offset)?;
        }

        Ok(())
    }

    /// 验证 FBX 转换参数
    ///
    /// # Arguments
    /// * `longitude` - 经度（可选）
    /// * `latitude` - 纬度（可选）
    /// * `height` - 高度（可选）
    ///
    /// # Returns
    /// * `Ok(())` - 验证通过
    /// * `Err(ValidationError)` - 验证失败
    pub fn validate_fbx_params(
        &self,
        longitude: Option<f64>,
        latitude: Option<f64>,
        height: Option<f64>,
    ) -> ValidationResult<()> {
        // FBX 转换中，所有坐标参数都是可选的（默认为 0）
        if let Some(lon) = longitude {
            self.validate_longitude(lon)?;
        }

        if let Some(lat) = latitude {
            self.validate_latitude(lat)?;
        }

        // 如果提供了经纬度，建议同时提供
        if longitude.is_some() != latitude.is_some() {
            return Err(ValidationError::InvalidCombination {
                message: "经度和纬度建议同时提供".to_string(),
            });
        }

        if let Some(h) = height {
            self.validate_height(h)?;
        }

        Ok(())
    }

    /// 格式化验证错误信息
    ///
    /// # Arguments
    /// * `error` - 验证错误
    ///
    /// # Returns
    /// 格式化的错误信息字符串
    pub fn format_error(&self, error: &ValidationError) -> String {
        format!("[坐标验证错误] {}", error)
    }
}

impl Default for CoordinateValidator {
    fn default() -> Self {
        Self::new()
    }
}

/// 便捷函数：验证经度
pub fn validate_longitude(longitude: f64) -> ValidationResult<()> {
    CoordinateValidator::new().validate_longitude(longitude)
}

/// 便捷函数：验证纬度
pub fn validate_latitude(latitude: f64) -> ValidationResult<()> {
    CoordinateValidator::new().validate_latitude(latitude)
}

/// 便捷函数：验证高度
pub fn validate_height(height: f64) -> ValidationResult<()> {
    CoordinateValidator::new().validate_height(height)
}

/// 便捷函数：验证高度偏移
pub fn validate_height_offset(offset: f64) -> ValidationResult<()> {
    CoordinateValidator::new().validate_height_offset(offset)
}

/// 便捷函数：验证完整坐标
pub fn validate_coordinate(
    longitude: f64,
    latitude: f64,
    height: Option<f64>,
) -> ValidationResult<()> {
    CoordinateValidator::new().validate_coordinate(longitude, latitude, height)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_validate_longitude_valid() {
        let validator = CoordinateValidator::new();
        assert!(validator.validate_longitude(0.0).is_ok());
        assert!(validator.validate_longitude(180.0).is_ok());
        assert!(validator.validate_longitude(-180.0).is_ok());
        assert!(validator.validate_longitude(113.5).is_ok());
    }

    #[test]
    fn test_validate_longitude_invalid() {
        let validator = CoordinateValidator::new();
        assert!(validator.validate_longitude(181.0).is_err());
        assert!(validator.validate_longitude(-181.0).is_err());
        assert!(validator.validate_longitude(f64::NAN).is_err());
    }

    #[test]
    fn test_validate_latitude_valid() {
        let validator = CoordinateValidator::new();
        assert!(validator.validate_latitude(0.0).is_ok());
        assert!(validator.validate_latitude(90.0).is_ok());
        assert!(validator.validate_latitude(-90.0).is_ok());
        assert!(validator.validate_latitude(32.8).is_ok());
    }

    #[test]
    fn test_validate_latitude_invalid() {
        let validator = CoordinateValidator::new();
        assert!(validator.validate_latitude(91.0).is_err());
        assert!(validator.validate_latitude(-91.0).is_err());
        assert!(validator.validate_latitude(f64::NAN).is_err());
    }

    #[test]
    fn test_validate_height_valid() {
        let validator = CoordinateValidator::new();
        assert!(validator.validate_height(0.0).is_ok());
        assert!(validator.validate_height(8848.0).is_ok());
        assert!(validator.validate_height(-11000.0).is_ok());
    }

    #[test]
    fn test_validate_height_invalid() {
        let validator = CoordinateValidator::new();
        assert!(validator.validate_height(10000.0).is_err());
        assert!(validator.validate_height(-15000.0).is_err());
        assert!(validator.validate_height(f64::NAN).is_err());
        assert!(validator.validate_height(f64::INFINITY).is_err());
    }

    #[test]
    fn test_validate_height_offset_valid() {
        let validator = CoordinateValidator::new();
        assert!(validator.validate_height_offset(0.0).is_ok());
        assert!(validator.validate_height_offset(-308.0).is_ok());
        assert!(validator.validate_height_offset(5000.0).is_ok());
    }

    #[test]
    fn test_validate_height_offset_invalid() {
        let validator = CoordinateValidator::new();
        assert!(validator.validate_height_offset(15000.0).is_err());
        assert!(validator.validate_height_offset(-15000.0).is_err());
        assert!(validator.validate_height_offset(f64::NAN).is_err());
    }

    #[test]
    fn test_validate_osgb_params() {
        let validator = CoordinateValidator::new();
        // 所有参数为 None 时应该通过
        assert!(validator.validate_osgb_params(None, None, None).is_ok());
        // 同时提供经纬度
        assert!(validator.validate_osgb_params(Some(113.5), Some(32.8), None).is_ok());
        // 只提供其中一个应该失败
        assert!(validator.validate_osgb_params(Some(113.5), None, None).is_err());
        assert!(validator.validate_osgb_params(None, Some(32.8), None).is_err());
        // 提供高度偏移
        assert!(validator.validate_osgb_params(None, None, Some(-308.0)).is_ok());
    }

    #[test]
    fn test_validate_fbx_params() {
        let validator = CoordinateValidator::new();
        // 所有参数为 None 时应该通过
        assert!(validator.validate_fbx_params(None, None, None).is_ok());
        // 同时提供经纬度
        assert!(validator.validate_fbx_params(Some(113.5), Some(32.8), Some(50.0)).is_ok());
    }

    #[test]
    fn test_error_display() {
        let error = ValidationError::InvalidLongitude {
            value: 200.0,
            range: (-180.0, 180.0),
        };
        let msg = format!("{}", error);
        assert!(msg.contains("200"));
        assert!(msg.contains("经度"));
    }
}

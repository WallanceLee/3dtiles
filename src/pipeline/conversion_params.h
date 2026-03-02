#pragma once

/**
 * @file conversion_params.h
 * @brief 转换参数体系 - 阶段 1 重构
 *
 * 解决新增数据类型时的开闭原则违反问题
 * 将类型特定参数从 ConversionParams 中分离
 */

#include "spatial_index.h"  // 包含 SpatialIndexConfig
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace pipeline {

// ============================================
// 前向声明
// ============================================

struct ShapefileParams;
struct FBXParams;

// ============================================
// 基础参数接口
// ============================================

/**
 * @brief 数据源特定参数的抽象基类
 *
 * 所有数据类型特定的参数类都应继承此类
 * 通过多态机制实现类型擦除，避免 ConversionParams 的修改
 */
class DataSourceSpecificParams {
public:
    virtual ~DataSourceSpecificParams() = default;

    /**
     * @brief 获取参数类型标识
     * @return 类型名字符串（如 "shapefile", "fbx"）
     */
    [[nodiscard]] virtual const char* GetType() const = 0;

    /**
     * @brief 验证参数有效性
     * @param error_msg 输出错误信息
     * @return 验证是否通过
     */
    [[nodiscard]] virtual bool Validate(std::string& error_msg) const = 0;

    /**
     * @brief 克隆接口（支持深拷贝）
     * @return 新的参数实例
     */
    [[nodiscard]] virtual std::unique_ptr<DataSourceSpecificParams> Clone() const = 0;

protected:
    // 只允许通过 Clone 进行拷贝
    DataSourceSpecificParams() = default;
    DataSourceSpecificParams(const DataSourceSpecificParams&) = default;
    DataSourceSpecificParams& operator=(const DataSourceSpecificParams&) = default;
    DataSourceSpecificParams(DataSourceSpecificParams&&) = default;
    DataSourceSpecificParams& operator=(DataSourceSpecificParams&&) = default;
};

// ============================================
// 模板基类（简化子类实现）
// ============================================

/**
 * @brief 数据源特定参数的模板基类
 * @tparam Derived 派生类类型（CRTP 模式）
 *
 * 使用 CRTP 模式自动实现 GetType 和 Clone 方法
 */
template<typename Derived>
class DataSourceSpecificParamsBase : public DataSourceSpecificParams {
public:
    [[nodiscard]] const char* GetType() const override {
        return Derived::TypeName();
    }

    [[nodiscard]] std::unique_ptr<DataSourceSpecificParams> Clone() const override {
        return std::make_unique<Derived>(static_cast<const Derived&>(*this));
    }

    // 允许默认拷贝和移动（派生类通常是 POD 类型）
    DataSourceSpecificParamsBase(const DataSourceSpecificParamsBase&) = default;
    DataSourceSpecificParamsBase& operator=(const DataSourceSpecificParamsBase&) = default;
    DataSourceSpecificParamsBase(DataSourceSpecificParamsBase&&) = default;
    DataSourceSpecificParamsBase& operator=(DataSourceSpecificParamsBase&&) = default;

protected:
    DataSourceSpecificParamsBase() = default;
};

// ============================================
// Shapefile 特定参数
// ============================================

/**
 * @brief Shapefile 数据源的特定参数
 */
struct ShapefileParams : public DataSourceSpecificParamsBase<ShapefileParams> {
    /**
     * @brief 获取类型名称
     * @return "shapefile"
     */
    static constexpr const char* TypeName() { return "shapefile"; }

    // Shapefile 特定参数
    std::string height_field;   ///< 高度字段名
    int layer_id = 0;           ///< 图层索引

    // 默认构造函数
    ShapefileParams() = default;

    // 默认拷贝和移动
    ShapefileParams(const ShapefileParams&) = default;
    ShapefileParams& operator=(const ShapefileParams&) = default;
    ShapefileParams(ShapefileParams&&) = default;
    ShapefileParams& operator=(ShapefileParams&&) = default;

    /**
     * @brief 验证参数有效性
     */
    [[nodiscard]] bool Validate(std::string& error_msg) const override {
        if (layer_id < 0) {
            error_msg = "layer_id must be non-negative";
            return false;
        }
        return true;
    }
};

// ============================================
// FBX 特定参数
// ============================================

/**
 * @brief FBX 数据源的特定参数
 */
struct FBXParams : public DataSourceSpecificParamsBase<FBXParams> {
    /**
     * @brief 获取类型名称
     * @return "fbx"
     */
    static constexpr const char* TypeName() { return "fbx"; }

    // FBX 特定参数（地理参考）
    double longitude = 0.0;   ///< 中心经度（度）
    double latitude = 0.0;    ///< 中心纬度（度）
    double height = 0.0;      ///< 中心高度（米）

    // 默认构造函数
    FBXParams() = default;

    // 默认拷贝和移动
    FBXParams(const FBXParams&) = default;
    FBXParams& operator=(const FBXParams&) = default;
    FBXParams(FBXParams&&) = default;
    FBXParams& operator=(FBXParams&&) = default;

    /**
     * @brief 验证参数有效性
     */
    [[nodiscard]] bool Validate(std::string& error_msg) const override {
        if (longitude < -180.0 || longitude > 180.0) {
            error_msg = "longitude out of range [-180, 180]";
            return false;
        }
        if (latitude < -90.0 || latitude > 90.0) {
            error_msg = "latitude out of range [-90, 90]";
            return false;
        }
        return true;
    }
};

// ============================================
// 未来新增类型示例: OBJ
// ============================================

/**
 * @brief OBJ 数据源的特定参数（示例）
 *
 * 新增数据类型时，只需创建类似的新结构体，
 * 无需修改任何现有代码
 */
struct OBJParams : public DataSourceSpecificParamsBase<OBJParams> {
    static constexpr const char* TypeName() { return "obj"; }

    // OBJ 特定参数
    bool flip_uv_v = false;           ///< 是否翻转 UV V 坐标
    bool generate_normals = false;    ///< 是否自动生成法线
    std::string mtl_path;             ///< 材质文件路径（可选）

    // 默认构造函数
    OBJParams() = default;

    // 默认拷贝和移动
    OBJParams(const OBJParams&) = default;
    OBJParams& operator=(const OBJParams&) = default;
    OBJParams(OBJParams&&) = default;
    OBJParams& operator=(OBJParams&&) = default;

    [[nodiscard]] bool Validate(std::string& error_msg) const override {
        // OBJ 参数验证逻辑
        (void)error_msg;
        return true;
    }
};

// ============================================
// 通用处理选项
// ============================================

/**
 * @brief 通用处理选项
 *
 * 适用于所有数据类型的处理选项
 */
struct ProcessingOptions {
    bool enable_lod = false;              ///< 是否启用 LOD
    bool enable_draco = false;            ///< 是否启用 Draco 压缩
    bool enable_texture_compress = false; ///< 是否启用纹理压缩
    bool enable_meshopt = false;          ///< 是否启用 meshoptimizer
    bool enable_simplify = false;         ///< 是否启用网格简化
    bool enable_unlit = false;            ///< 是否使用 unlit 材质

    /**
     * @brief 验证选项有效性
     */
    [[nodiscard]] bool IsValid() const {
        // 当前所有组合都有效
        return true;
    }
};

// ============================================
// 通用转换参数（清理后）
// ============================================

/**
 * @brief 统一的转换参数
 *
 * 重构后的 ConversionParams，将类型特定参数分离到 specific 字段
 * 新增数据类型时无需修改此类
 */
struct ConversionParams {
    // 基础路径
    std::string input_path;   ///< 输入文件路径
    std::string output_path;  ///< 输出目录路径
    std::string source_type;  ///< 数据源类型（用于工厂查找）

    // 类型特定参数（使用类型擦除）
    std::unique_ptr<DataSourceSpecificParams> specific;

    // 通用处理选项
    ProcessingOptions options;

    // 空间索引配置
    SpatialIndexConfig spatial_config;

    // ========== Deprecated 兼容层 ==========
    // 以下字段为向后兼容保留，将在未来版本中移除
#pragma deprecated("Use specific<ShapefileParams>() instead")
    std::string height_field;   ///< 【已废弃】请使用 specific<ShapefileParams>()->height_field

#pragma deprecated("Use specific<ShapefileParams>() instead")
    int layer_id = 0;           ///< 【已废弃】请使用 specific<ShapefileParams>()->layer_id

#pragma deprecated("Use specific<FBXParams>() instead")
    double longitude = 0.0;     ///< 【已废弃】请使用 specific<FBXParams>()->longitude

#pragma deprecated("Use specific<FBXParams>() instead")
    double latitude = 0.0;      ///< 【已废弃】请使用 specific<FBXParams>()->latitude

#pragma deprecated("Use specific<FBXParams>() instead")
    double height = 0.0;        ///< 【已废弃】请使用 specific<FBXParams>()->height

    // ======================================

    // 默认构造函数
    ConversionParams() = default;

    // 移动构造函数
    ConversionParams(ConversionParams&&) = default;

    // 移动赋值运算符（显式删除，因为引用成员无法移动赋值）
    ConversionParams& operator=(ConversionParams&&) = delete;

    // 拷贝构造函数（深拷贝 specific）
    ConversionParams(const ConversionParams& other)
        : input_path(other.input_path)
        , output_path(other.output_path)
        , source_type(other.source_type)
        , specific(other.specific ? other.specific->Clone() : nullptr)
        , options(other.options)
        , spatial_config(other.spatial_config)
        , height_field(other.height_field)
        , layer_id(other.layer_id)
        , longitude(other.longitude)
        , latitude(other.latitude)
        , height(other.height) {}

    // 拷贝赋值运算符
    ConversionParams& operator=(const ConversionParams& other) {
        if (this != &other) {
            input_path = other.input_path;
            output_path = other.output_path;
            source_type = other.source_type;
            specific = other.specific ? other.specific->Clone() : nullptr;
            options = other.options;
            spatial_config = other.spatial_config;
            height_field = other.height_field;
            layer_id = other.layer_id;
            longitude = other.longitude;
            latitude = other.latitude;
            height = other.height;
        }
        return *this;
    }

    // ============================================
    // 便利方法
    // ============================================

    /**
     * @brief 获取类型特定参数
     * @tparam T 参数类型（如 ShapefileParams, FBXParams）
     * @return 类型特定参数的指针，类型不匹配时返回 nullptr
     */
    template<typename T>
    [[nodiscard]] const T* GetSpecific() const {
        if (specific && specific->GetType() == T::TypeName()) {
            return static_cast<const T*>(specific.get());
        }
        return nullptr;
    }

    // ========== 向后兼容的访问函数 ==========

    [[deprecated("Use options.enable_lod instead")]]
    bool& EnableLOD() { return options.enable_lod; }
    [[deprecated("Use options.enable_lod instead")]]
    const bool& EnableLOD() const { return options.enable_lod; }

    [[deprecated("Use options.enable_draco instead")]]
    bool& EnableDraco() { return options.enable_draco; }
    [[deprecated("Use options.enable_draco instead")]]
    const bool& EnableDraco() const { return options.enable_draco; }

    [[deprecated("Use options.enable_texture_compress instead")]]
    bool& EnableTextureCompress() { return options.enable_texture_compress; }
    [[deprecated("Use options.enable_texture_compress instead")]]
    const bool& EnableTextureCompress() const { return options.enable_texture_compress; }

    [[deprecated("Use options.enable_meshopt instead")]]
    bool& EnableMeshOpt() { return options.enable_meshopt; }
    [[deprecated("Use options.enable_meshopt instead")]]
    const bool& EnableMeshOpt() const { return options.enable_meshopt; }

    [[deprecated("Use options.enable_simplify instead")]]
    bool& EnableSimplify() { return options.enable_simplify; }
    [[deprecated("Use options.enable_simplify instead")]]
    const bool& EnableSimplify() const { return options.enable_simplify; }

    [[deprecated("Use options.enable_unlit instead")]]
    bool& EnableUnlit() { return options.enable_unlit; }
    [[deprecated("Use options.enable_unlit instead")]]
    const bool& EnableUnlit() const { return options.enable_unlit; }

    [[deprecated("Use spatial_config.max_depth instead")]]
    int& MaxDepth() { return spatial_config.max_depth; }
    [[deprecated("Use spatial_config.max_depth instead")]]
    const int& MaxDepth() const { return spatial_config.max_depth; }

    [[deprecated("Use spatial_config.max_items_per_node instead")]]
    size_t& MaxItemsPerNode() { return spatial_config.max_items_per_node; }
    [[deprecated("Use spatial_config.max_items_per_node instead")]]
    const size_t& MaxItemsPerNode() const { return spatial_config.max_items_per_node; }

    [[deprecated("Use spatial_config.min_bounds_size instead")]]
    double& MinBoundsSize() { return spatial_config.min_bounds_size; }
    [[deprecated("Use spatial_config.min_bounds_size instead")]]
    const double& MinBoundsSize() const { return spatial_config.min_bounds_size; }

    // ============================================

    /**
     * @brief 验证所有参数
     * @param error_msg 输出错误信息
     * @return 验证是否通过
     */
    [[nodiscard]] bool Validate(std::string& error_msg) const {
        if (input_path.empty()) {
            error_msg = "input_path is required";
            return false;
        }
        if (output_path.empty()) {
            error_msg = "output_path is required";
            return false;
        }
        if (source_type.empty()) {
            error_msg = "source_type is required";
            return false;
        }
        // 验证空间索引配置
        if (spatial_config.max_depth <= 0) {
            error_msg = "max_depth must be positive";
            return false;
        }
        if (spatial_config.max_items_per_node == 0) {
            error_msg = "max_items_per_node must be non-zero";
            return false;
        }
        if (spatial_config.min_bounds_size <= 0.0) {
            error_msg = "min_bounds_size must be positive";
            return false;
        }
        if (!options.IsValid()) {
            error_msg = "invalid processing options";
            return false;
        }
        if (specific && !specific->Validate(error_msg)) {
            return false;
        }
        return true;
    }

    /**
     * @brief 从旧版参数迁移
     *
     * 将废弃字段的值迁移到新的 specific 字段
     * 用于向后兼容
     */
    void MigrateFromLegacy() {
        if (specific) {
            // 已经使用新格式，无需迁移
            return;
        }

        // 根据 source_type 和旧字段创建 specific
        if (source_type == "shapefile" && !height_field.empty()) {
            auto params = std::make_unique<ShapefileParams>();
            params->height_field = height_field;
            params->layer_id = layer_id;
            specific = std::move(params);
        } else if (source_type == "fbx") {
            auto params = std::make_unique<FBXParams>();
            params->longitude = longitude;
            params->latitude = latitude;
            params->height = height;
            specific = std::move(params);
        }
    }

    /**
     * @brief 检查是否需要迁移
     */
    [[nodiscard]] bool NeedsMigration() const {
        return !specific && (
            !height_field.empty() ||
            layer_id != 0 ||
            longitude != 0.0 ||
            latitude != 0.0 ||
            height != 0.0
        );
    }
};

// ============================================
// 转换结果
// ============================================

/**
 * @brief 转换结果
 */
struct ConversionResult {
    bool success = false;           ///< 是否成功
    std::string error_message;      ///< 错误信息（失败时）
    int node_count = 0;             ///< 生成的节点数
    int b3dm_count = 0;             ///< 生成的 B3DM 文件数
    std::string tileset_path;       ///< tileset.json 路径

    /**
     * @brief 创建成功结果
     */
    static ConversionResult Success(int nodes, int b3dms, std::string path) {
        return {true, "", nodes, b3dms, std::move(path)};
    }

    /**
     * @brief 创建失败结果
     */
    static ConversionResult Failure(std::string msg) {
        return {false, std::move(msg), 0, 0, ""};
    }
};

// ============================================
// 进度回调
// ============================================

/**
 * @brief 进度回调函数类型
 */
using ProgressCallback = std::function<void(const std::string& stage, float progress)>;

} // namespace pipeline

#pragma once

#include <array>
#include <cmath>
#include <type_traits>
#include <optional>
#include <algorithm>
#include <vector>

namespace spatial::core {

/**
 * @brief 通用空间包围盒
 *
 * 支持2D和3D空间，使用模板参数区分
 *
 * @tparam T 标量类型 (float, double, int等)
 * @tparam Dim 维度 (2或3)
 */
template<typename T = double, size_t Dim = 3>
struct SpatialBounds {
    static_assert(Dim == 2 || Dim == 3, "SpatialBounds only supports 2D or 3D");
    static_assert(std::is_arithmetic_v<T>, "SpatialBounds requires arithmetic type");

    using ValueType = T;
    using VectorType = std::array<T, Dim>;
    static constexpr size_t Dimension = Dim;

    // 默认构造函数创建空/无效的包围盒
    SpatialBounds() = default;

    // 从最小/最大角点构造
    SpatialBounds(const VectorType& min_corner, const VectorType& max_corner)
        : min_(min_corner), max_(max_corner) {}

    // 2D构造器
    SpatialBounds(T minX, T minY, T maxX, T maxY)
        requires (Dim == 2)
        : min_{minX, minY}, max_{maxX, maxY} {}

    // 3D构造器
    SpatialBounds(T minX, T minY, T minZ, T maxX, T maxY, T maxZ)
        requires (Dim == 3)
        : min_{minX, minY, minZ}, max_{maxX, maxY, maxZ} {}

    // 工厂方法：从中心点和半尺寸创建
    static SpatialBounds fromCenterAndHalfExtents(const VectorType& center,
                                                   const VectorType& half_extents) {
        VectorType min_corner, max_corner;
        for (size_t i = 0; i < Dim; ++i) {
            min_corner[i] = center[i] - half_extents[i];
            max_corner[i] = center[i] + half_extents[i];
        }
        return SpatialBounds(min_corner, max_corner);
    }

    // 工厂方法：创建空包围盒
    static SpatialBounds empty() {
        return SpatialBounds();
    }

    // 工厂方法：从单点创建
    static SpatialBounds fromPoint(const VectorType& point) {
        return SpatialBounds(point, point);
    }

    // 访问器
    const VectorType& min() const { return min_; }
    const VectorType& max() const { return max_; }
    VectorType& min() { return min_; }
    VectorType& max() { return max_; }

    // 获取中心点
    VectorType center() const {
        VectorType c;
        for (size_t i = 0; i < Dim; ++i) {
            c[i] = (min_[i] + max_[i]) * T(0.5);
        }
        return c;
    }

    // 获取尺寸（每个维度的完整大小）
    VectorType extents() const {
        VectorType e;
        for (size_t i = 0; i < Dim; ++i) {
            e[i] = max_[i] - min_[i];
        }
        return e;
    }

    // 获取半尺寸
    VectorType halfExtents() const {
        VectorType he;
        for (size_t i = 0; i < Dim; ++i) {
            he[i] = (max_[i] - min_[i]) * T(0.5);
        }
        return he;
    }

    // 检查包围盒是否有效（min <= max 在所有维度）
    bool isValid() const {
        for (size_t i = 0; i < Dim; ++i) {
            if (min_[i] > max_[i]) return false;
        }
        return true;
    }

    // 检查包围盒是否为空（无体积/面积）
    bool isEmpty() const {
        for (size_t i = 0; i < Dim; ++i) {
            if (min_[i] >= max_[i]) return true;
        }
        return false;
    }

    // 计算体积（3D）或面积（2D）
    T volume() const {
        if (!isValid()) return T(0);
        T vol = T(1);
        for (size_t i = 0; i < Dim; ++i) {
            vol *= (max_[i] - min_[i]);
        }
        return vol;
    }

    // 计算对角线长度
    T diagonal() const {
        T sum = T(0);
        for (size_t i = 0; i < Dim; ++i) {
            T diff = max_[i] - min_[i];
            sum += diff * diff;
        }
        return std::sqrt(sum);
    }

    // 扩展包围盒以包含一个点
    void expand(const VectorType& point) {
        for (size_t i = 0; i < Dim; ++i) {
            min_[i] = std::min(min_[i], point[i]);
            max_[i] = std::max(max_[i], point[i]);
        }
    }

    // 扩展包围盒以包含另一个包围盒
    void expand(const SpatialBounds& other) {
        for (size_t i = 0; i < Dim; ++i) {
            min_[i] = std::min(min_[i], other.min_[i]);
            max_[i] = std::max(max_[i], other.max_[i]);
        }
    }

    // 按比率在所有方向膨胀
    SpatialBounds inflated(T ratio) const {
        VectorType c = center();
        VectorType half_extents = halfExtents();
        for (size_t i = 0; i < Dim; ++i) {
            half_extents[i] *= (T(1) + ratio);
        }
        return fromCenterAndHalfExtents(c, half_extents);
    }

    // 按绝对值在每个方向扩展
    SpatialBounds padded(T amount) const {
        VectorType new_min = min_;
        VectorType new_max = max_;
        for (size_t i = 0; i < Dim; ++i) {
            new_min[i] -= amount;
            new_max[i] += amount;
        }
        return SpatialBounds(new_min, new_max);
    }

    // 检查是否包含一个点
    bool contains(const VectorType& point) const {
        for (size_t i = 0; i < Dim; ++i) {
            if (point[i] < min_[i] || point[i] > max_[i]) return false;
        }
        return true;
    }

    // 检查是否包含另一个包围盒
    bool contains(const SpatialBounds& other) const {
        for (size_t i = 0; i < Dim; ++i) {
            if (other.min_[i] < min_[i] || other.max_[i] > max_[i]) return false;
        }
        return true;
    }

    // 检查是否与另一个包围盒相交
    bool intersects(const SpatialBounds& other) const {
        for (size_t i = 0; i < Dim; ++i) {
            if (other.max_[i] < min_[i] || other.min_[i] > max_[i]) return false;
        }
        return true;
    }

    // 获取两个包围盒的交集
    std::optional<SpatialBounds> intersection(const SpatialBounds& other) const {
        VectorType new_min{};
        VectorType new_max{};
        for (size_t i = 0; i < Dim; ++i) {
            new_min[i] = std::max(min_[i], other.min_[i]);
            new_max[i] = std::min(max_[i], other.max_[i]);
            if (new_min[i] > new_max[i]) return std::nullopt;
        }
        return SpatialBounds(new_min, new_max);
    }

    // 分割为子包围盒
    std::vector<SpatialBounds> split(size_t childCount) const {
        std::vector<SpatialBounds> children;

        if (Dim == 2 && childCount == 4) {
            // 四叉树分割
            VectorType c = center();
            children.reserve(4);
            children.emplace_back(VectorType{min_[0], min_[1]}, VectorType{c[0], c[1]});
            children.emplace_back(VectorType{c[0], min_[1]}, VectorType{max_[0], c[1]});
            children.emplace_back(VectorType{min_[0], c[1]}, VectorType{c[0], max_[1]});
            children.emplace_back(VectorType{c[0], c[1]}, VectorType{max_[0], max_[1]});
        } else if (Dim == 3 && childCount == 8) {
            // 八叉树分割
            VectorType c = center();
            children.reserve(8);
            for (int i = 0; i < 2; ++i) {
                for (int j = 0; j < 2; ++j) {
                    for (int k = 0; k < 2; ++k) {
                        VectorType child_min{
                            i == 0 ? min_[0] : c[0],
                            j == 0 ? min_[1] : c[1],
                            k == 0 ? min_[2] : c[2]
                        };
                        VectorType child_max{
                            i == 0 ? c[0] : max_[0],
                            j == 0 ? c[1] : max_[1],
                            k == 0 ? c[2] : max_[2]
                        };
                        children.emplace_back(child_min, child_max);
                    }
                }
            }
        }

        return children;
    }

    // 相等运算符
    bool operator==(const SpatialBounds& other) const {
        return min_ == other.min_ && max_ == other.max_;
    }
    bool operator!=(const SpatialBounds& other) const {
        return !(*this == other);
    }

private:
    VectorType min_{};
    VectorType max_{};
};

// 常用类型的别名
template<typename T> using Bounds2D = SpatialBounds<T, 2>;
template<typename T> using Bounds3D = SpatialBounds<T, 3>;

using Bounds2Df = Bounds2D<float>;
using Bounds2Dd = Bounds2D<double>;
using Bounds2Di = Bounds2D<int>;

using Bounds3Df = Bounds3D<float>;
using Bounds3Dd = Bounds3D<double>;
using Bounds3Di = Bounds3D<int>;

// 合并多个包围盒
template<typename T, size_t Dim>
SpatialBounds<T, Dim> mergeBounds(const SpatialBounds<T, Dim>& a,
                                   const SpatialBounds<T, Dim>& b) {
    SpatialBounds<T, Dim> result = a;
    result.expand(b);
    return result;
}

} // namespace spatial::core

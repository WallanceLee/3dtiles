#pragma once

#include <cstdint>

namespace gltf_writer {

enum class ComponentType : int {
    Byte = 5120,
    UnsignedByte = 5121,
    Short = 5122,
    UnsignedShort = 5123,
    UnsignedInt = 5125,
    Float = 5126
};

enum class AccessorType : int {
    Scalar = 1,
    Vec2 = 2,
    Vec3 = 3,
    Vec4 = 4,
    Mat2 = 16 | 2,
    Mat3 = 36 | 3,
    Mat4 = 64 | 4
};

enum class PrimitiveMode : int {
    Points = 0,
    Lines = 1,
    LineLoop = 2,
    LineStrip = 3,
    Triangles = 4,
    TriangleStrip = 5,
    TriangleFan = 6
};

enum class BufferViewTarget : int {
    None = 0,
    ArrayBuffer = 34962,
    ElementArrayBuffer = 34963
};

enum class TextureFilter : int {
    Nearest = 9728,
    Linear = 9729,
    NearestMipmapNearest = 9984,
    LinearMipmapNearest = 9985,
    NearestMipmapLinear = 9986,
    LinearMipmapLinear = 9987
};

enum class TextureWrap : int {
    ClampToEdge = 33071,
    MirroredRepeat = 33648,
    Repeat = 10497
};

inline int toTinyGltf(ComponentType t) { return static_cast<int>(t); }
inline int toTinyGltf(AccessorType t) { return static_cast<int>(t); }
inline int toTinyGltf(PrimitiveMode m) { return static_cast<int>(m); }
inline int toTinyGltf(BufferViewTarget t) { return static_cast<int>(t); }

}

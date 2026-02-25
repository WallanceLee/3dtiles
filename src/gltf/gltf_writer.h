#pragma once

/**
 * @file gltf/gltf_writer.h
 * @brief GLTF Writer 统一头文件
 *
 * 包含所有GLTF相关组件：
 * - 扩展管理器
 * - 扩展（Draco, Basisu, Unlit, TextureTransform, SpecularGlossiness）
 * - 构建器（PrimitiveBuilder, MaterialBuilder）
 * - GLTFBuilder（高级API）
 */

#include "extension_manager.h"
#include "extensions/texture_transform.h"
#include "extensions/specular_glossiness.h"
#include "extensions/unlit.h"
#include "extensions/draco.h"
#include "extensions/basisu.h"
#include "primitive_builder.h"
#include "material_builder.h"
#include "gltf_builder.h"

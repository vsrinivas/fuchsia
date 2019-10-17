// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_VK_TARGET_REQUIREMENTS_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_VK_TARGET_REQUIREMENTS_H_

//
//
//

#include <stdint.h>

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////
//
// QUEUEING DISCIPLINES
//

typedef enum spn_vk_target_queueing_e
{
  SPN_VK_TARGET_QUEUEING_SIMPLE,

  SPN_VK_TARGET_QUEUEING_COUNT,

} spn_vk_target_queueing_e;

///////////////////////
//
// EXTENSIONS
//
// Every extension is represented by a bit.
//
// Maintain a *tight* set of extensions used by the targets -- weed out
// unused extensions as necessary.
//

#define SPN_VK_TARGET_EXTENSIONS()                                                                 \
  SPN_VK_TARGET_EXTENSION(AMD_device_coherent_memory)                                              \
  SPN_VK_TARGET_EXTENSION(AMD_gcn_shader)                                                          \
  SPN_VK_TARGET_EXTENSION(AMD_gpu_shader_half_float)                                               \
  SPN_VK_TARGET_EXTENSION(AMD_shader_info)                                                         \
  SPN_VK_TARGET_EXTENSION(EXT_debug_marker)                                                        \
  SPN_VK_TARGET_EXTENSION(EXT_debug_report)                                                        \
  SPN_VK_TARGET_EXTENSION(EXT_debug_utils)                                                         \
  SPN_VK_TARGET_EXTENSION(EXT_descriptor_indexing)                                                 \
  SPN_VK_TARGET_EXTENSION(EXT_hdr_metadata)                                                        \
  SPN_VK_TARGET_EXTENSION(EXT_scalar_block_layout)                                                 \
  SPN_VK_TARGET_EXTENSION(EXT_subgroup_size_control)                                               \
  SPN_VK_TARGET_EXTENSION(KHR_incremental_present)                                                 \
  SPN_VK_TARGET_EXTENSION(KHR_maintenance1)                                                        \
  SPN_VK_TARGET_EXTENSION(KHR_maintenance2)                                                        \
  SPN_VK_TARGET_EXTENSION(KHR_maintenance3)                                                        \
  SPN_VK_TARGET_EXTENSION(KHR_pipeline_executable_properties)                                      \
  SPN_VK_TARGET_EXTENSION(KHR_relaxed_block_layout)                                                \
  SPN_VK_TARGET_EXTENSION(KHR_shader_clock)                                                        \
  SPN_VK_TARGET_EXTENSION(KHR_shader_float16_int8)                                                 \
  SPN_VK_TARGET_EXTENSION(KHR_shader_float_controls)                                               \
  SPN_VK_TARGET_EXTENSION(KHR_shader_subgroup_extended_types)                                      \
  SPN_VK_TARGET_EXTENSION(KHR_timeline_semaphore)                                                  \
  SPN_VK_TARGET_EXTENSION(NV_shader_subgroup_partitioned)

//
//
//

#define SPN_VK_TARGET_EXTENSION_ENUM(ext_) SPN_VK_TARGET_EXTENSION_##ext_

typedef enum spn_vk_target_extensions_e
{
#undef SPN_VK_TARGET_EXTENSION
#define SPN_VK_TARGET_EXTENSION(ext_) SPN_VK_TARGET_EXTENSION_ENUM(ext_),

  SPN_VK_TARGET_EXTENSIONS()

    SPN_VK_TARGET_EXTENSION_ENUM(COUNT)

} spn_vk_target_extensions_e;

//
//
//

union spn_vk_target_extensions
{
  struct
  {
#undef SPN_VK_TARGET_EXTENSION
#define SPN_VK_TARGET_EXTENSION(ext_) uint32_t ext_ : 1;

    SPN_VK_TARGET_EXTENSIONS()

  } named;

  uint32_t bitmap[(SPN_VK_TARGET_EXTENSION_ENUM(COUNT) + 31) / 32];
};

///////////////////////
//
// FEATURES
//
// Spinel doesn't require any features right now but use of .shaderInt64
// may be a future optimization.  Target devices that support this
// feature are currently enabling it.
//

#define SPN_VK_TARGET_FEATURES() SPN_VK_TARGET_FEATURE(shaderInt64)

//
//
//

#define SPN_VK_TARGET_FEATURE_ENUM(feature_) SPN_VK_TARGET_FEATURE_##feature_

typedef enum spn_vk_target_features_e
{
#undef SPN_VK_TARGET_FEATURE
#define SPN_VK_TARGET_FEATURE(feature_) SPN_VK_TARGET_FEATURE_ENUM(feature_),

  SPN_VK_TARGET_FEATURES()

    SPN_VK_TARGET_FEATURE_ENUM(COUNT)

} spn_vk_target_features_e;

//
//
//

union spn_vk_target_features
{
  struct
  {
#undef SPN_VK_TARGET_FEATURE
#define SPN_VK_TARGET_FEATURE(feature_) uint32_t feature_ : 1;

    SPN_VK_TARGET_FEATURES()

  } named;

  uint32_t bitmap[(SPN_VK_TARGET_FEATURE_ENUM(COUNT) + 31) / 32];
};

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_VK_TARGET_REQUIREMENTS_H_

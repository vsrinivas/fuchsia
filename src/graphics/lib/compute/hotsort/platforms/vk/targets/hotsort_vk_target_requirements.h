// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_HOTSORT_PLATFORMS_VK_TARGETS_HOTSORT_VK_TARGET_REQUIREMENTS_H_
#define SRC_GRAPHICS_LIB_COMPUTE_HOTSORT_PLATFORMS_VK_TARGETS_HOTSORT_VK_TARGET_REQUIREMENTS_H_

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
// EXTENSIONS
//

//
// Try to maintain a *tight* set of extensions used by the targets
//

#define HOTSORT_VK_TARGET_EXTENSIONS()                                                             \
  HOTSORT_VK_TARGET_EXTENSION(AMD_shader_info)                                                     \
  HOTSORT_VK_TARGET_EXTENSION(EXT_subgroup_size_control)                                           \
  HOTSORT_VK_TARGET_EXTENSION(KHR_maintenance1)                                                    \
  HOTSORT_VK_TARGET_EXTENSION(KHR_maintenance2)                                                    \
  HOTSORT_VK_TARGET_EXTENSION(KHR_maintenance3)                                                    \
  HOTSORT_VK_TARGET_EXTENSION(KHR_pipeline_executable_properties)                                  \
  HOTSORT_VK_TARGET_EXTENSION(KHR_shader_subgroup_extended_types)

//
//
//

#define HOTSORT_VK_TARGET_EXTENSION_ENUM(ext_) HOTSORT_VK_TARGET_EXTENSION_##ext_

typedef enum hotsort_vk_target_extensions_e
{
#undef HOTSORT_VK_TARGET_EXTENSION
#define HOTSORT_VK_TARGET_EXTENSION(ext_) HOTSORT_VK_TARGET_EXTENSION_ENUM(ext_),

  HOTSORT_VK_TARGET_EXTENSIONS()

    HOTSORT_VK_TARGET_EXTENSION_ENUM(COUNT)

} hotsort_vk_target_extensions_e;

//
//
//

union hotsort_vk_target_extensions
{
  struct
  {
#undef HOTSORT_VK_TARGET_EXTENSION
#define HOTSORT_VK_TARGET_EXTENSION(ext_) uint32_t ext_ : 1;

    HOTSORT_VK_TARGET_EXTENSIONS()

  } named;

  uint32_t bitmap[(HOTSORT_VK_TARGET_EXTENSION_ENUM(COUNT) + 31) / 32];
};

///////////////////////
//
// FEATURES
//

//
// HotSort may depend on .shaderInt64
//

#define HOTSORT_VK_TARGET_FEATURES() HOTSORT_VK_TARGET_FEATURE(shaderInt64)

//
//
//

#define HOTSORT_VK_TARGET_FEATURE_ENUM(feature_) HOTSORT_VK_TARGET_FEATURE_##feature_

typedef enum hotsort_vk_target_features_e
{
#undef HOTSORT_VK_TARGET_FEATURE
#define HOTSORT_VK_TARGET_FEATURE(feature_) HOTSORT_VK_TARGET_FEATURE_ENUM(feature_),

  HOTSORT_VK_TARGET_FEATURES()

    HOTSORT_VK_TARGET_FEATURE_ENUM(COUNT)

} hotsort_vk_target_features_e;

//
//
//

union hotsort_vk_target_features
{
  struct
  {
#undef HOTSORT_VK_TARGET_FEATURE
#define HOTSORT_VK_TARGET_FEATURE(feature_) uint32_t feature_ : 1;

    HOTSORT_VK_TARGET_FEATURES()

  } named;

  uint32_t bitmap[(HOTSORT_VK_TARGET_FEATURE_ENUM(COUNT) + 31) / 32];
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

#endif  // SRC_GRAPHICS_LIB_COMPUTE_HOTSORT_PLATFORMS_VK_TARGETS_HOTSORT_VK_TARGET_REQUIREMENTS_H_

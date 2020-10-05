// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_HOTSORT_PLATFORMS_VK_TARGETS_HOTSORT_VK_TARGET_REQUIREMENTS_H_
#define SRC_GRAPHICS_LIB_COMPUTE_HOTSORT_PLATFORMS_VK_TARGETS_HOTSORT_VK_TARGET_REQUIREMENTS_H_

//
//
//

#include <stdbool.h>
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
  HOTSORT_VK_TARGET_EXTENSION(KHR_pipeline_executable_properties)                                  \
  HOTSORT_VK_TARGET_EXTENSION(KHR_shader_subgroup_extended_types)

//
//
//

union hotsort_vk_target_extensions
{
#undef HOTSORT_VK_TARGET_EXTENSION
#define HOTSORT_VK_TARGET_EXTENSION(ext_) bool ext_ : 1;

  struct
  {
    HOTSORT_VK_TARGET_EXTENSIONS()
  } named;

#undef HOTSORT_VK_TARGET_EXTENSION
#define HOTSORT_VK_TARGET_EXTENSION(ext_) +1

  uint32_t bitmap[(31 HOTSORT_VK_TARGET_EXTENSIONS()) / 32];
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

union hotsort_vk_target_features
{
#undef HOTSORT_VK_TARGET_FEATURE
#define HOTSORT_VK_TARGET_FEATURE(feature_) bool feature_ : 1;

  struct
  {
    HOTSORT_VK_TARGET_FEATURES()
  } named;

#undef HOTSORT_VK_TARGET_FEATURE
#define HOTSORT_VK_TARGET_FEATURE(feature_) +1

  uint32_t bitmap[(31 HOTSORT_VK_TARGET_FEATURES()) / 32];
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

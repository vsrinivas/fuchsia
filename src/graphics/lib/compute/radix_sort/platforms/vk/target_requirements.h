// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_RADIX_SORT_PLATFORMS_VK_TARGET_REQUIREMENTS_H_
#define SRC_GRAPHICS_LIB_COMPUTE_RADIX_SORT_PLATFORMS_VK_TARGET_REQUIREMENTS_H_

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
// Every extension is represented by a bit.
//
// Maintain a *tight* set of extensions used by the targets -- weed out
// unused extensions as necessary.
//
#define RS_VK_TARGET_EXTENSIONS()                                                                  \
  RS_VK_TARGET_EXTENSION(EXT_subgroup_size_control)                                                \
  RS_VK_TARGET_EXTENSION(KHR_pipeline_executable_properties)                                       \
  RS_VK_TARGET_EXTENSION(NV_shader_subgroup_partitioned)

//
//
//

union radix_sort_vk_target_extensions
{
#undef RS_VK_TARGET_EXTENSION
#define RS_VK_TARGET_EXTENSION(ext_) bool ext_ : 1;

  struct
  {
    RS_VK_TARGET_EXTENSIONS()
  } named;

#undef RS_VK_TARGET_EXTENSION
#define RS_VK_TARGET_EXTENSION(ext_) +1

  uint32_t bitmap[(31 RS_VK_TARGET_EXTENSIONS()) / 32];
};

///////////////////////
//
// FEATURES
//
// A Radix Sort target may depend on the Vulkan physical device features listed
// below.
//
// See Section `38.1 Feature Requirements` to understand how enabling certain
// extensions guarantees support of some related features.
//
#define RS_VK_TARGET_FEATURES_VK10()                                                               \
  RS_VK_TARGET_FEATURE_VK10(shaderInt64)                                                           \
  RS_VK_TARGET_FEATURE_VK10(shaderInt16)

#define RS_VK_TARGET_FEATURES_VK11()  // No VK 1.1 features for now

#define RS_VK_TARGET_FEATURES_VK12()                                                               \
  RS_VK_TARGET_FEATURE_VK12(shaderSubgroupExtendedTypes)                                           \
  RS_VK_TARGET_FEATURE_VK12(bufferDeviceAddress)                                                   \
  RS_VK_TARGET_FEATURE_VK12(vulkanMemoryModel)                                                     \
  RS_VK_TARGET_FEATURE_VK12(vulkanMemoryModelDeviceScope)

//
//
//
union radix_sort_vk_target_features
{
#undef RS_VK_TARGET_FEATURE_VK10
#undef RS_VK_TARGET_FEATURE_VK11
#undef RS_VK_TARGET_FEATURE_VK12
#define RS_VK_TARGET_FEATURE_VK10(feature_) bool feature_ : 1;
#define RS_VK_TARGET_FEATURE_VK11(feature_) bool feature_ : 1;
#define RS_VK_TARGET_FEATURE_VK12(feature_) bool feature_ : 1;

  struct
  {
    RS_VK_TARGET_FEATURES_VK10()
    RS_VK_TARGET_FEATURES_VK11()
    RS_VK_TARGET_FEATURES_VK12()
  } named;

#undef RS_VK_TARGET_FEATURE_VK10
#undef RS_VK_TARGET_FEATURE_VK11
#undef RS_VK_TARGET_FEATURE_VK12
#define RS_VK_TARGET_FEATURE_VK10(feature_) +1
#define RS_VK_TARGET_FEATURE_VK11(feature_) +1
#define RS_VK_TARGET_FEATURE_VK12(feature_) +1

  uint32_t bitmap[(31 /* round up */              //
                   RS_VK_TARGET_FEATURES_VK10()   //
                   RS_VK_TARGET_FEATURES_VK11()   //
                   RS_VK_TARGET_FEATURES_VK12())  //
                  / 32];
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

#endif  // SRC_GRAPHICS_LIB_COMPUTE_RADIX_SORT_PLATFORMS_VK_TARGET_REQUIREMENTS_H_

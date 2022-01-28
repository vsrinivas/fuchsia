// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TARGET_REQUIREMENTS_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TARGET_REQUIREMENTS_H_

//
//
//

#include <stdbool.h>
#include <stdint.h>

//
// If you want to see what's happening here with the macro expansions,
// run vk_target_requirements.h through the preprocessor:
//
// clang -I $VULKAN_SDK/include  -E  vk_target_requirements.h | clang-format
// cl    -I %VULKAN_SDK%\include -EP vk_target_requirements.h | clang-format
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

#define SPN_TARGET_EXTENSIONS()                                                                    \
  SPN_TARGET_EXTENSION(EXT_subgroup_size_control)                                                  \
  SPN_TARGET_EXTENSION(KHR_incremental_present)                                                    \
  SPN_TARGET_EXTENSION(KHR_pipeline_executable_properties)                                         \
  SPN_TARGET_EXTENSION(KHR_shader_non_semantic_info)                                               \
  SPN_TARGET_EXTENSION(NV_shader_subgroup_partitioned)

//
//
//

union spinel_target_extensions
{
#undef SPN_TARGET_EXTENSION
#define SPN_TARGET_EXTENSION(ext_) bool ext_ : 1;

  struct
  {
    SPN_TARGET_EXTENSIONS()
  } named;

#undef SPN_TARGET_EXTENSION
#define SPN_TARGET_EXTENSION(ext_) +1

  uint32_t bitmap[(31 SPN_TARGET_EXTENSIONS()) / 32];
};

///////////////////////
//
// FEATURES
//
// A Spinel target may depend on the Vulkan physical device features listed
// below.
//
// See Section `38.1 Feature Requirements` to understand how enabling certain
// extensions guarantees support of some related features.
//

#define SPN_TARGET_FEATURES_VK10()                                                                 \
  SPN_TARGET_FEATURE_VK10(shaderInt16)                                                             \
  SPN_TARGET_FEATURE_VK10(shaderInt64)

#define SPN_TARGET_FEATURES_VK11()                                                                 \
  SPN_TARGET_FEATURE_VK11(storageBuffer16BitAccess)                                                \
  SPN_TARGET_FEATURE_VK11(uniformAndStorageBuffer16BitAccess)                                      \
  SPN_TARGET_FEATURE_VK11(storagePushConstant16)                                                   \
  SPN_TARGET_FEATURE_VK11(samplerYcbcrConversion)

#define SPN_TARGET_FEATURES_VK12()                                                                 \
  SPN_TARGET_FEATURE_VK12(storageBuffer8BitAccess)                                                 \
  SPN_TARGET_FEATURE_VK12(uniformAndStorageBuffer8BitAccess)                                       \
  SPN_TARGET_FEATURE_VK12(storagePushConstant8)                                                    \
  SPN_TARGET_FEATURE_VK12(shaderBufferInt64Atomics)                                                \
  SPN_TARGET_FEATURE_VK12(shaderSharedInt64Atomics)                                                \
  SPN_TARGET_FEATURE_VK12(shaderFloat16)                                                           \
  SPN_TARGET_FEATURE_VK12(shaderInt8)                                                              \
  SPN_TARGET_FEATURE_VK12(scalarBlockLayout)                                                       \
  SPN_TARGET_FEATURE_VK12(shaderSubgroupExtendedTypes)                                             \
  SPN_TARGET_FEATURE_VK12(hostQueryReset)                                                          \
  SPN_TARGET_FEATURE_VK12(timelineSemaphore)                                                       \
  SPN_TARGET_FEATURE_VK12(bufferDeviceAddress)                                                     \
  SPN_TARGET_FEATURE_VK12(subgroupBroadcastDynamicId)                                              \
  SPN_TARGET_FEATURE_VK12(vulkanMemoryModel)                                                       \
  SPN_TARGET_FEATURE_VK12(vulkanMemoryModelDeviceScope)

//
//
//

union spinel_target_features
{
#undef SPN_TARGET_FEATURE_VK10
#undef SPN_TARGET_FEATURE_VK11
#undef SPN_TARGET_FEATURE_VK12
#define SPN_TARGET_FEATURE_VK10(feature_) bool feature_ : 1;
#define SPN_TARGET_FEATURE_VK11(feature_) bool feature_ : 1;
#define SPN_TARGET_FEATURE_VK12(feature_) bool feature_ : 1;

  struct
  {
    SPN_TARGET_FEATURES_VK10()
    SPN_TARGET_FEATURES_VK11()
    SPN_TARGET_FEATURES_VK12()
  } named;

#undef SPN_TARGET_FEATURE_VK10
#undef SPN_TARGET_FEATURE_VK11
#undef SPN_TARGET_FEATURE_VK12
#define SPN_TARGET_FEATURE_VK10(feature_) +1
#define SPN_TARGET_FEATURE_VK11(feature_) +1
#define SPN_TARGET_FEATURE_VK12(feature_) +1

  uint32_t bitmap[(31 /* round up */            //
                   SPN_TARGET_FEATURES_VK10()   //
                   SPN_TARGET_FEATURES_VK11()   //
                   SPN_TARGET_FEATURES_VK12())  //
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

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TARGET_REQUIREMENTS_H_

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_VK_TARGET_REQUIREMENTS_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_VK_TARGET_REQUIREMENTS_H_

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
  SPN_VK_TARGET_EXTENSION(KHR_pipeline_executable_properties)                                      \
  SPN_VK_TARGET_EXTENSION(KHR_shader_clock)                                                        \
  SPN_VK_TARGET_EXTENSION(KHR_shader_float16_int8)                                                 \
  SPN_VK_TARGET_EXTENSION(KHR_shader_float_controls)                                               \
  SPN_VK_TARGET_EXTENSION(KHR_shader_subgroup_extended_types)                                      \
  SPN_VK_TARGET_EXTENSION(KHR_timeline_semaphore)                                                  \
  SPN_VK_TARGET_EXTENSION(NV_shader_subgroup_partitioned)

//
//
//

union spn_vk_target_extensions
{
#undef SPN_VK_TARGET_EXTENSION
#define SPN_VK_TARGET_EXTENSION(ext_) bool ext_ : 1;

  struct
  {
    SPN_VK_TARGET_EXTENSIONS()
  } named;

#undef SPN_VK_TARGET_EXTENSION
#define SPN_VK_TARGET_EXTENSION(ext_) +1

  uint32_t bitmap[(31 SPN_VK_TARGET_EXTENSIONS()) / 32];
};

///////////////////////
//
// FEATURES
//
// A Spinel target may depend on the following Vulkan
// VkPhysicalDeviceFeatures:
//

// clang-format off
#define SPN_VK_TARGET_FEATURES()                \
  SPN_VK_TARGET_FEATURE(shaderInt64)
// clang-format on

//
//
//

union spn_vk_target_features
{
#undef SPN_VK_TARGET_FEATURE
#define SPN_VK_TARGET_FEATURE(feature_) bool feature_ : 1;

  struct
  {
    SPN_VK_TARGET_FEATURES()
  } named;

#undef SPN_VK_TARGET_FEATURE
#define SPN_VK_TARGET_FEATURE(feature_) +1

  uint32_t bitmap[(31 SPN_VK_TARGET_FEATURES()) / 32];
};

///////////////////////
//
// FEATURES2 STRUCTURES
//
// A Spinel target may depend on Vulkan 1.1+ feature structures.
//
// The following VkPhysicalDevice feature structures should appear in
// the VkPhysicalDeviceFeatures2.pNext list:
//
//   * HostQueryResetFeaturesEXT
//   * PipelineExecutablePropertiesFeaturesKHR
//   * ScalarBlockLayoutFeaturesEXT
//   * ShaderFloat16Int8FeaturesKHR
//   * SubgroupSizeControlFeaturesEXT
//
// The following VkPhysicalDevice feature structures will likely be
// added once Fuchsia's Vulkan SDK is updated:
//
//   * BufferDeviceAddressFeaturesKHR
//   * TimelineSemaphoreFeaturesKHR
//   * ShaderIntegerFunctions2FeaturesINTEL
//   * ShaderSubgroupExtendedTypesFeaturesKHR
//
// NOTE(allanmac): Each named feature structure occupies at least one
// byte and the entire structure is unioned with a 32-bit dword array.
//

// clang-format off
#define SPN_VK_TARGET_FEATURE_STRUCTURES()                                                                              \
  SPN_VK_TARGET_FEATURE_STRUCTURE(HostQueryResetFeaturesEXT,                                                            \
                                  VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES_EXT,                      \
                                  hostQueryReset)                                                                       \
  SPN_VK_TARGET_FEATURE_STRUCTURE(PipelineExecutablePropertiesFeaturesKHR,                                              \
                                  VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR,        \
                                  pipelineExecutableInfo)                                                               \
  SPN_VK_TARGET_FEATURE_STRUCTURE(ScalarBlockLayoutFeaturesEXT,                                                         \
                                  VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES_EXT,                   \
                                  scalarBlockLayout)                                                                    \
  SPN_VK_TARGET_FEATURE_STRUCTURE(ShaderFloat16Int8FeaturesKHR,                                                         \
                                  VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES_KHR,                   \
                                  shaderFloat16,                                                                        \
                                  shaderInt8)                                                                           \
  SPN_VK_TARGET_FEATURE_STRUCTURE(SubgroupSizeControlFeaturesEXT,                                                       \
                                  VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES_EXT,                 \
                                  subgroupSizeControl,                                                                  \
                                  computeFullSubgroups)
// clang-format on

//
// Note: current macros support up to 5 VKBool32 fields
//

#define SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBERS_N(fn1_, fn2_, fn3_, fn4_, fn5_, fnN_, ...) fnN_

#define SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBERS(feature_, ...)                                     \
  SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBERS_N(__VA_ARGS__,                                           \
                                            SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBERS5,              \
                                            SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBERS4,              \
                                            SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBERS3,              \
                                            SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBERS2,              \
                                            SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBERS1)              \
  (feature_, __VA_ARGS__)

//
//
//

#undef SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBERS1
#undef SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBERS2
#undef SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBERS3
#undef SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBERS4
#undef SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBERS5

#define SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBERS1(feature_, b1_)                                    \
  SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBER_X(feature_, b1_)

#define SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBERS2(feature_, b1_, b2_)                               \
  SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBERS1(feature_, b1_)                                          \
  SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBER_X(feature_, b2_)

#define SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBERS3(feature_, b1_, b2_, b3_)                          \
  SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBERS2(feature_, b1_, b2_)                                     \
  SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBER_X(feature_, b3_)

#define SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBERS4(feature_, b1_, b2_, b3_, b4_)                     \
  SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBERS3(feature_, b1_, b2_, b3_)                                \
  SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBER_X(feature_, b4_)

#define SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBERS5(feature_, b1_, b2_, b3_, b4_, b5_)                \
  SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBERS4(feature_, b1_, b2_, b3_, b4_)                           \
  SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBER_X(feature_, b5_)

//
//
//

union spn_vk_target_feature_structures
{
#undef SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBER_X
#define SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBER_X(feature_, bX_) bool bX_ : 1;

#undef SPN_VK_TARGET_FEATURE_STRUCTURE
#define SPN_VK_TARGET_FEATURE_STRUCTURE(feature_, stype_, ...)                                     \
  struct                                                                                           \
  {                                                                                                \
    SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBERS(feature_, __VA_ARGS__)                                 \
  } feature_;

  struct
  {
    SPN_VK_TARGET_FEATURE_STRUCTURES()
  } named;

#undef SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBER_X
#define SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBER_X(feature_, bX_) +1

#undef SPN_VK_TARGET_FEATURE_STRUCTURE
#define SPN_VK_TARGET_FEATURE_STRUCTURE(feature_, stype_, ...)                                     \
  +((7 SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBERS(feature_, __VA_ARGS__)) / 8)

  uint32_t bitmap[(3 SPN_VK_TARGET_FEATURE_STRUCTURES()) / 4];
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

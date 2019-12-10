// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_INCLUDE_SPINEL_SPINEL_VK_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_INCLUDE_SPINEL_SPINEL_VK_H_

//
//
//

#include <vulkan/vulkan_core.h>

#include "spinel.h"
#include "spinel_vk_types.h"

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
// A Spinel context must be created with a VkDevice that is intialized
// with all of the target's required queues, extensions and features.
//
// This function yields the queue creation info structures, extensions,
// initialized feature flags and initialized feature structs required by
// a Spinel target.
//
// If either the .qcis or .ext_names member is NULL, the respective
// count member will be initialized.
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
// SPN_ERROR_PARTIAL_TARGET_REQUIREMENTS will be returned if:
//
//   * The .qcis or .ext_names member is NULL
//   * The .qci_count or .ext_name_count member is too small
//   * The .pdf2 member is NULL
//   * The .pdf2->pNext list doesn't contain an expected feature struct
//
// Otherwise, SPN_SUCCESS will be returned.
//

struct spn_vk_target;

spn_result_t
spn_vk_target_get_requirements(struct spn_vk_target const * const        target,
                               struct spn_vk_target_requirements * const requirements);

//
// VULKAN CONTEXT CREATION
//

spn_result_t
spn_vk_context_create(struct spn_vk_environment * const               environment,
                      struct spn_vk_context_create_info const * const create_info,
                      spn_context_t * const                           context);

//
// VULKAN CONTEXT SCHEDULING
//

spn_result_t
spn_vk_context_wait(spn_context_t   context,
                    uint32_t const  imports_count,
                    VkFence * const imports,
                    bool const      wait_all,
                    uint64_t const  timeout_ns);

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_INCLUDE_SPINEL_SPINEL_VK_H_

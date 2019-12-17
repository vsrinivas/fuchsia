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
//
//

struct spn_vk_target;

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
// The following VkPhysicalDevice feature structures may be required and
// should appear in the VkPhysicalDeviceFeatures2.pNext list:
//
//   * HostQueryResetFeaturesEXT
//   * PipelineExecutablePropertiesFeaturesKHR
//   * ScalarBlockLayoutFeaturesEXT
//   * ShaderFloat16Int8FeaturesKHR
//   * SubgroupSizeControlFeaturesEXT
//
// Feature structures that aren't required by a target are ignored.
//
// The following VkPhysicalDevice feature structures will likely be
// added once Fuchsia's Vulkan SDK is updated:
//
//   * BufferDeviceAddressFeaturesKHR
//   * TimelineSemaphoreFeaturesKHR
//   * ShaderIntegerFunctions2FeaturesINTEL
//   * ShaderSubgroupExtendedTypesFeaturesKHR
//
// SPN_ERROR_PARTIAL_TARGET_REQUIREMENTS will be returned if:
//
//   * The .qcis field is NULL
//   * The .ext_names field is NULL and .ext_name_count > 0
//   * The .qci_count or .ext_name_count member is too small
//   * The .pdf2 member is NULL
//   * The .pdf2->pNext list doesn't contain an expected feature struct
//
// Otherwise, SPN_SUCCESS will be returned.
//

spn_result_t
spn_vk_target_get_requirements(struct spn_vk_target const * const        target,
                               struct spn_vk_target_requirements * const requirements);

//
// This convenience function will initialize a block of memory to form a
// chain of initialized feature structures.
//
// The chain will include all feature structures required by the target.
// It may include feature structures that aren't required by the target
// but is limited to the structures documented above.
//
// Each structure in the chain will have its |sType| and |pNext| fields
// initialized.  Remaining fields are zeroed.
//
// The feature structures' fields must then be initialized with the
// spn_vk_target_get_requirements() function.
//
// If the |structures| argument is NULL then the |structures_size|
// argument will be initialized with the required storage size.
//
// Notes:
//
//   * The initialized structures_size will always be non-zero.
//
//   * The structures pointer can be cast to a VkBaseOutStructure
//     pointer and the NULL-terminated chain can be walked -- possibly
//     for merging with other feature structures.
//
//   * If structures_size is larger than necessary, the trailing bytes
//     won't be zero initialized.
//
//   * Note that a physical device feature structure can only appear
//     once in a VkDeviceCreateInfo.pNext chain so merging may be
//     required before device creation.
//
// Usage:
//
//   Allocate the feature structures in two steps:
//
//     | ... spn_vk_target_get_feature_structures(target,&structures_size,NULL);       // returns error
//     |
//     | alignas(VkBaseOutStructure) uint8_t structures[structures_size];              // or malloc()
//     |
//     | ... spn_vk_target_get_feature_structures(target,&structures_size,structures); // returns success
//
// SPN_ERROR_PARTIAL_TARGET_REQUIREMENTS will be returned if:
//
//   * The target is NULL
//   * The structures_size argument is NULL
//   * The structures argument is NULL
//   * The structures_size argument is too small
//
// Otherwise, SPN_SUCCESS will be returned.
//

spn_result_t
spn_vk_target_get_feature_structures(struct spn_vk_target const * const target,
                                     size_t * const                     structures_size,
                                     void *                             structures);

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

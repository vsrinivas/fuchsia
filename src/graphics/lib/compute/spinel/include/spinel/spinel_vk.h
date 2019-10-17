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
// TARGET REQUIREMENTS: VULKAN
//

struct spn_vk_target_requirements
{
  uint32_t                   qci_count;
  VkDeviceQueueCreateInfo *  qcis;
  uint32_t                   ext_name_count;
  char const **              ext_names;
  VkPhysicalDeviceFeatures * pdf;
};

//
// Yields the queues, extensions and features required by a Spinel
// target.
//
// If either .qcis or .ext_names are NULL then only the respective
// counts will be initialized.
//
// It is an error to provide a count that is too small.
//

spn_result_t
spn_vk_target_get_requirements(struct spn_vk_target const * const        target,
                               struct spn_vk_target_requirements * const requirements);

//
// CONTEXT CREATION: VULKAN
//

struct spn_vk_environment
{
  VkDevice                         d;
  VkAllocationCallbacks const *    ac;
  VkPipelineCache                  pc;
  VkPhysicalDevice                 pd;
  VkPhysicalDeviceMemoryProperties pdmp;
  uint32_t                         qfi;  // queue family index
};

struct spn_vk_context_create_info
{
  //
  // NOTE(allanmac): This interface is in flux.
  //
  // When Spinel constructs a target for a particular device, it also
  // generates a custom HotSort target.  These will always be bundled
  // together.
  //
  struct spn_vk_target const *     spinel;
  struct hotsort_vk_target const * hotsort;
  uint64_t                         block_pool_size;
  uint32_t                         handle_count;
};

spn_result_t
spn_vk_context_create(struct spn_vk_environment * const               environment,
                      struct spn_vk_context_create_info const * const create_info,
                      spn_context_t * const                           context);

//
// CONTEXT SCHEDULING
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

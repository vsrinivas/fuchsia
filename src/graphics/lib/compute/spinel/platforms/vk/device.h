// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

//
//
//

#include <vulkan/vulkan_core.h>

#include "spinel_types.h"

#include "allocator_host.h"
#include "allocator_device.h"

#include "fence_pool.h"

//
//
//

struct spn_target_image;

//
// Vulkan primitive objects for Spinel
//

struct spn_device_vk
{
  VkAllocationCallbacks    const * ac;
  VkPhysicalDevice                 pd;
  uint32_t                         qfi; // queue family index
  VkPhysicalDeviceMemoryProperties pdmp;
  VkDevice                         d;
  VkPipelineCache                  pc;
};

//
//
//

struct spn_device
{
  struct spn_context                   * context;

  struct spn_device_vk                 * vk;

  struct spn_target                    * target;

  struct {
    struct {
      struct spn_allocator_host_perm     perm;
      struct spn_allocator_host_temp     temp;
    } host;
    struct {
      struct {
        struct spn_allocator_device_perm local;
        struct spn_allocator_device_perm copyback; // hrN     -- copy-back to host
        struct spn_allocator_device_perm coherent; // hw1:drN -- target-specific
      } perm;
      struct {
        struct spn_allocator_device_temp local;
      } temp;
    } device;
  } allocator;

  struct spn_queue_pool                * queue_pool;
  struct spn_cb_pool                   * cb_pool;
  struct spn_fence_pool                * fence_pool;
  struct spn_handle_pool               * handle_pool;

  struct spn_block_pool                * block_pool;

  //
  //
  //
#if 0
  struct spn_scheduler         * scheduler;

  struct spn_grid_deps         * deps;

  struct hs_cl      const      * hs;     // opaque hotsort
#endif
};


//
// FIXME -- Spinel target needs to be able to vend what extensions it
// requires from a target device
//

//
// Creation and disposal intitializes context and may rely on other
// context resources like the scheduler
//

//
// this is exposed here temporarily
//

spn_result
spn_context_create_vk(struct spn_context *          * const context_p,
                      struct spn_device_vk          * const device_vk,
                      struct spn_target_image const * const target_image,
                      uint64_t                        const block_pool_size,
                      uint32_t                        const handle_count);

//
// Disable device because of a fatal error
//

void
spn_device_lost(struct spn_device * const device);

//
//
//

uint64_t
spn_device_wait_nsecs(struct spn_device * const device);

//
// does this need to be here?  just grab config
//

uint32_t
spn_device_block_pool_get_mask(struct spn_device * const device);

//
// submit          - simplest submit
// submit_signal_1 - submit and signal one semaphore
// submit_wait_1   - submit and wait on one semaphore
//

void
spn_device_cb_submit(struct spn_device    * const device,
                     VkCommandBuffer        const cb,
                     spn_fence_complete_pfn const pfn,
                     void                 * const pfn_payload,
                     size_t                 const pfn_payload_size);

void
spn_device_cb_submit_signal_1(struct spn_device    * const device,
                              VkCommandBuffer        const cb,
                              spn_fence_complete_pfn const pfn,
                              void                 * const pfn_payload,
                              size_t                 const pfn_payload_size,
                              VkSemaphore            const signal);

void
spn_device_cb_submit_wait_1(struct spn_device      * const device,
                            VkCommandBuffer          const cb,
                            spn_fence_complete_pfn   const pfn,
                            void                   * const pfn_payload,
                            size_t                   const pfn_payload_size,
                            VkSemaphore              const wait,
                            VkPipelineStageFlags     const wait_stages);
void
spn_device_cb_submit_wait_1_signal_1(struct spn_device      * const device,
                                     VkCommandBuffer          const cb,
                                     spn_fence_complete_pfn   const pfn,
                                     void                   * const pfn_payload,
                                     size_t                   const pfn_payload_size,
                                     VkSemaphore              const wait,
                                     VkPipelineStageFlags     const wait_stages,
                                     VkSemaphore              const signal);

//
//
//

void
spn_device_release_paths(struct spn_device * const device,
                         spn_path_t  const * const paths,
                         uint32_t            const size,
                         uint32_t            const span,
                         uint32_t            const head);

void
spn_device_release_rasters(struct spn_device  * const device,
                           spn_raster_t const * const rasters,
                           uint32_t             const size,
                           uint32_t             const span,
                           uint32_t             const head);

//
// yield : if there are unsignaled fences, test if at least one fence is signaled
// wait  : if there are unsignaled fences, wait for at least one fence to signal
// drain : wait for all unsignaled fences -- unknown if we need this
//

spn_result
spn_device_yield(struct spn_device * const device);

spn_result
spn_device_wait(struct spn_device * const device);

spn_result
spn_device_drain(struct spn_device * const device);

//
//
//



















#if 0

//
// HOST HANDLE RETAIN/RELEASE/FLUSH
//

spn_result
spn_device_path_host_retain(struct spn_device * const platform,
                            spn_path_t  const *       paths,
                            uint32_t                  count);

spn_result
spn_device_raster_host_retain(struct spn_device  * const platform,
                              spn_raster_t const *       rasters,
                              uint32_t                   count);


spn_result
spn_device_path_host_release(struct spn_device * const platform,
                             spn_path_t  const *       paths,
                             uint32_t                  count);

spn_result
spn_device_raster_host_release(struct spn_device  * const platform,
                               spn_raster_t const *       rasters,
                               uint32_t                   count);


spn_result
spn_device_path_host_flush(struct spn_device * const platform,
                           spn_path_t  const *       paths,
                           uint32_t                  count);

spn_result
spn_device_raster_host_flush(struct spn_device  * const platform,
                             spn_raster_t const *       rasters,
                             uint32_t                   count);

//
// DEVICE/PIPELINE HANDLE ACQUIRE/RETAIN/RELEASE
//
// The retain operations pre-validate handles
//

spn_handle_t
spn_device_handle_device_acquire(struct spn_device * const platform);

spn_result
spn_device_handle_device_validate_retain(struct spn_device        * const platform,
                                         spn_typed_handle_type_e    const handle_type,
                                         spn_typed_handle_t const *       typed_handles,
                                         uint32_t                         count);

void
spn_device_handle_device_retain(struct spn_device  * const platform,
                                spn_handle_t const *       handles,
                                uint32_t                   count);

void
spn_device_path_device_release(struct spn_device  * const platform,
                               spn_handle_t const *       handles,
                               uint32_t                   count);

void
spn_device_raster_device_release(struct spn_device  * const platform,
                                 spn_handle_t const *       handles,
                                 uint32_t                   count);

//
// We only use in-order command queues in the pipeline
//

cl_command_queue
spn_device_acquire_cq_in_order(struct spn_device * const platform);

void
spn_device_release_cq_in_order(struct spn_device * const platform,
                               cl_command_queue          cq);

//
// DEVICE MEMORY ALLOCATION
//

cl_mem
spn_device_device_perm_alloc(struct spn_device * const platform,
                             cl_mem_flags        const flags,
                             size_t              const size);

void
spn_device_device_perm_free(struct spn_device * const platform,
                            cl_mem              const mem);

cl_mem
spn_device_device_temp_alloc(struct spn_device * const platform,
                             cl_mem_flags        const flags,
                             size_t              const size,
                             spn_subbuf_id_t   * const subbuf_id,
                             size_t            * const subbuf_size);

void
spn_device_device_temp_free(struct spn_device * const platform,
                            cl_mem              const mem,
                            spn_subbuf_id_t     const subbuf_id);

//
//
//

#endif

//
//
//

#if 1

void
spn_device_debug(struct spn_context * const context);

#endif

//
//
//

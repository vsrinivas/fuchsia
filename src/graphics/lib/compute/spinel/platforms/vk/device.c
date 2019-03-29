// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include <stdlib.h>

#include "context.h"
#include "device.h"
#include "target.h"

#include "queue_pool.h"
#include "cb_pool.h"
#include "fence_pool.h"
#include "handle_pool.h"
#include "block_pool.h"

#include "path_builder_impl.h"
#include "raster_builder_impl.h"
//#include "composition_impl.h"
#include "styling_impl.h"

#include "common/vk/vk_assert.h"

//
//
//

static
spn_result
spn_device_create(struct spn_context            * const context,
                  struct spn_device_vk          * const device_vk,
                  struct spn_target_image const * const target_image,
                  uint64_t                        const block_pool_size,
                  uint32_t                        const handle_count)
{
  struct spn_device * device = malloc(sizeof(*device));

  context->device = device;
  device->context = context;

  device->vk      = device_vk;
  device->target  = spn_target_create(device_vk,target_image);

  struct spn_target_config const * const config = spn_target_get_config(device->target);

  spn_allocator_host_perm_create(&device->allocator.host.perm,
                                 config->allocator.host.perm.alignment);

  spn_allocator_host_temp_create(&device->allocator.host.temp,
                                 &device->allocator.host.perm,
                                 config->allocator.host.temp.subbufs,
                                 config->allocator.host.temp.size,
                                 config->allocator.host.temp.alignment);

  spn_allocator_device_perm_create(&device->allocator.device.perm.local,
                                   device_vk,

                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,

                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT  |
                                   VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | // vkCmdDispatchIndirect()
                                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT    | // <-- notice SRC bit
                                   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                   0,
                                   NULL);

  spn_allocator_device_perm_create(&device->allocator.device.perm.copyback,
                                   device_vk,

                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_CACHED_BIT,

                                   VK_BUFFER_USAGE_TRANSFER_DST_BIT,

                                   0,
                                   NULL);

  spn_allocator_device_perm_create(&device->allocator.device.perm.coherent,
                                   device_vk,

                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT  |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, // FIXME -- target configurable

                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT  |
                                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT,

                                   0,
                                   NULL);

  spn_allocator_device_temp_create(&device->allocator.device.temp.local,
                                   &device->allocator.host.perm,
                                   &device->allocator.device.perm.local,
                                   device_vk,
                                   128,
                                   32 * 1024 * 1024);


  spn_device_queue_pool_create(device,1); // FIXME - verify performance - this is a simplistic pool

  spn_device_cb_pool_create(device);      // FIXME - verify performance - this is a simplistic pool

  spn_device_fence_pool_create(device,config->fence_pool.size);

  spn_device_handle_pool_create(device,handle_count);

  spn_device_block_pool_create(device,block_pool_size,handle_count);

  return SPN_SUCCESS;
}

//
//
//

static
spn_result
spn_device_dispose(struct spn_device * const device)
{
  //
  // FIXME -- do we want to use spn_device_lost() ?
  //

  // drain all in-flight completions
  spn_device_drain(device);

  // shut down each major module in reverse order
  spn_device_block_pool_dispose(device);
  spn_device_handle_pool_dispose(device);
  spn_device_fence_pool_dispose(device);
  spn_device_cb_pool_dispose(device);
  spn_device_queue_pool_dispose(device);

  spn_allocator_device_temp_dispose(&device->allocator.device.temp.local,
                                    device->vk);

  spn_allocator_device_perm_dispose(&device->allocator.device.perm.coherent,
                                    device->vk);
  spn_allocator_device_perm_dispose(&device->allocator.device.perm.copyback,
                                    device->vk);
  spn_allocator_device_perm_dispose(&device->allocator.device.perm.local,
                                    device->vk);

  spn_allocator_host_temp_dispose(&device->allocator.host.temp);
  spn_allocator_host_perm_dispose(&device->allocator.host.perm);

  spn_target_dispose(device->target,device->vk);

  free(device->context);
  free(device);

  return SPN_SUCCESS;
}

//
//
//

uint64_t
spn_device_wait_nsecs(struct spn_device * const device)
{
  //
  // FIXME -- make this part of config
  //
  return 1000UL * 1000UL * 250UL; // 250 msecs.
}

//
// submit the command buffer
//

void
spn_device_cb_submit(struct spn_device    * const device,
                     VkCommandBuffer        const cb,
                     spn_fence_complete_pfn const pfn,
                     void                 * const pfn_payload,
                     size_t                 const pfn_payload_size)
{
  vk(EndCommandBuffer(cb));

  VkQueue queue = spn_device_queue_pool_acquire(device);

  VkFence fence = spn_device_fence_pool_acquire(device,
                                                queue,
                                                cb,
                                                pfn,
                                                pfn_payload,
                                                pfn_payload_size);

  struct VkSubmitInfo const si = {
    .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .pNext                = NULL,
    .waitSemaphoreCount   = 0,
    .pWaitSemaphores      = NULL,
    .pWaitDstStageMask    = NULL,
    .commandBufferCount   = 1,
    .pCommandBuffers      = &cb,
    .signalSemaphoreCount = 0,
    .pSignalSemaphores    = NULL
  };

  vk(QueueSubmit(queue,1,&si,fence));
}

//
// submit the command buffer and signal one semaphore
//

void
spn_device_cb_submit_signal_1(struct spn_device    * const device,
                              VkCommandBuffer        const cb,
                              spn_fence_complete_pfn const pfn,
                              void                 * const pfn_payload,
                              size_t                 const pfn_payload_size,
                              VkSemaphore            const signal)
{
  vk(EndCommandBuffer(cb));

  VkQueue queue = spn_device_queue_pool_acquire(device);

  VkFence fence = spn_device_fence_pool_acquire(device,
                                                queue,
                                                cb,
                                                pfn,
                                                pfn_payload,
                                                pfn_payload_size);

  struct VkSubmitInfo const si = {
    .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .pNext                = NULL,
    .waitSemaphoreCount   = 0,
    .pWaitSemaphores      = NULL,
    .pWaitDstStageMask    = NULL,
    .commandBufferCount   = 1,
    .pCommandBuffers      = &cb,
    .signalSemaphoreCount = 1,
    .pSignalSemaphores    = &signal
  };

  vk(QueueSubmit(queue,1,&si,fence));
}

//
// submit the command buffer and wait on one semaphore
//

void
spn_device_cb_submit_wait_1(struct spn_device      * const device,
                            VkCommandBuffer          const cb,
                            spn_fence_complete_pfn   const pfn,
                            void                   * const pfn_payload,
                            size_t                   const pfn_payload_size,
                            VkSemaphore              const wait,
                            VkPipelineStageFlags     const wait_stages)
{
  vk(EndCommandBuffer(cb));

  VkQueue queue = spn_device_queue_pool_acquire(device);

  VkFence fence = spn_device_fence_pool_acquire(device,
                                                queue,
                                                cb,
                                                pfn,
                                                pfn_payload,
                                                pfn_payload_size);

  struct VkSubmitInfo const si = {
    .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .pNext                = NULL,
    .waitSemaphoreCount   = 1,
    .pWaitSemaphores      = &wait,
    .pWaitDstStageMask    = &wait_stages,
    .commandBufferCount   = 1,
    .pCommandBuffers      = &cb,
    .signalSemaphoreCount = 0,
    .pSignalSemaphores    = NULL
  };

  vk(QueueSubmit(queue,1,&si,fence));
}

//
// submit the command buffer and wait on one semaphore and then signal
// one semaphore
//

void
spn_device_cb_submit_wait_1_signal_1(struct spn_device      * const device,
                                     VkCommandBuffer          const cb,
                                     spn_fence_complete_pfn   const pfn,
                                     void                   * const pfn_payload,
                                     size_t                   const pfn_payload_size,
                                     VkSemaphore              const wait,
                                     VkPipelineStageFlags     const wait_stages,
                                     VkSemaphore              const signal)
{
  vk(EndCommandBuffer(cb));

  VkQueue queue = spn_device_queue_pool_acquire(device);

  VkFence fence = spn_device_fence_pool_acquire(device,
                                                queue,
                                                cb,
                                                pfn,
                                                pfn_payload,
                                                pfn_payload_size);

  struct VkSubmitInfo const si = {
    .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .pNext                = NULL,
    .waitSemaphoreCount   = 1,
    .pWaitSemaphores      = &wait,
    .pWaitDstStageMask    = &wait_stages,
    .commandBufferCount   = 1,
    .pCommandBuffers      = &cb,
    .signalSemaphoreCount = 1,
    .pSignalSemaphores    = &signal
  };

  vk(QueueSubmit(queue,1,&si,fence));
}

//
//
//

void
spn_device_release_paths(struct spn_device * const device,
                         spn_path_t  const * const paths,
                         uint32_t            const size,
                         uint32_t            const span,
                         uint32_t            const head)
{
  uint32_t const count_lo = MIN_MACRO(uint32_t,head + span,size) - head;

  spn_device_handle_pool_release_d_paths(device,
                                         paths + head,
                                         count_lo);
  if (span > count_lo)
    {
      uint32_t const count_hi = span - count_lo;

      spn_device_handle_pool_release_d_paths(device,
                                             paths,
                                             count_hi);
    }
}

//
//
//

void
spn_device_release_rasters(struct spn_device  * const device,
                           spn_raster_t const * const rasters,
                           uint32_t             const size,
                           uint32_t             const span,
                           uint32_t             const head)
{
  uint32_t const count_lo = MIN_MACRO(uint32_t,head + span,size) - head;

  spn_device_handle_pool_release_d_rasters(device,
                                           rasters + head,
                                           count_lo);
  if (span > count_lo)
    {
      uint32_t const count_hi = span - count_lo;

      spn_device_handle_pool_release_d_rasters(device,
                                               rasters,
                                               count_hi);
    }
}


//
//
//

void
spn_device_lost(struct spn_device * const device)
{
  exit(-1);
}

//
//
//

spn_result
spn_context_create_vk(struct spn_context *          * const context_p,
                      struct spn_device_vk          * const device_vk,
                      struct spn_target_image const * const target_image,
                      uint64_t                        const block_pool_size,
                      uint32_t                        const handle_count)
{
  struct spn_context * context = malloc(sizeof(*context));

  *context_p                 = context;

  context->dispose           = spn_device_dispose;
  //context->reset           = spn_device_reset;
  context->yield             = spn_device_yield;
  context->wait              = spn_device_wait;

  context->path_builder      = spn_path_builder_impl_create;
  context->path_retain       = spn_device_handle_pool_validate_retain_h_paths;
  context->path_release      = spn_device_handle_pool_validate_release_h_paths;

  context->raster_builder    = spn_raster_builder_impl_create;
  context->raster_retain     = spn_device_handle_pool_validate_retain_h_rasters;
  context->raster_release    = spn_device_handle_pool_validate_release_h_rasters;

  //context->composition     = spn_composition_create;
  context->styling           = spn_styling_impl_create;
  //context->render          = spn_render;

  spn_result err = spn_device_create(context,
                                  device_vk,
                                  target_image,
                                  block_pool_size,
                                  handle_count);

  return err;
}

//
//
//

#if 0

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

//
//
//

#include "context.h"
#include "block.h"
#include "grid.h"
#include "common/cl/assert_cl.h"
#include "config_cl.h"
#include "runtime_cl_12.h"
#include "export_cl_12.h"

//
//
//

static
void
spn_block_pool_create(struct spn_runtime * const runtime, cl_command_queue cq)
{
  // save size
  runtime->block_pool.size = &runtime->config->block_pool;

  // create block extent
  spn_extent_pdrw_alloc(runtime,
                        &runtime->block_pool.blocks,
                        runtime->block_pool.size->pool_size *
                        runtime->config->block.bytes);

  // allocate block pool ids
  spn_extent_pdrw_alloc(runtime,
                        &runtime->block_pool.ids,
                        runtime->block_pool.size->ring_pow2 * sizeof(spn_uint));

  // allocate block pool atomics
  spn_extent_phr_pdrw_alloc(runtime,
                            &runtime->block_pool.atomics,
                            sizeof(union spn_block_pool_atomic));

  // acquire pool id and atomic initialization kernels
  cl_kernel k0 = spn_device_acquire_kernel(runtime->device,SPN_DEVICE_KERNEL_ID_BLOCK_POOL_INIT_IDS);
  cl_kernel k1 = spn_device_acquire_kernel(runtime->device,SPN_DEVICE_KERNEL_ID_BLOCK_POOL_INIT_ATOMICS);

  // init ids
  cl(SetKernelArg(k0,0,sizeof(runtime->block_pool.ids.drw),&runtime->block_pool.ids.drw));
  cl(SetKernelArg(k0,1,SPN_CL_ARG(runtime->block_pool.size->pool_size)));

  // the kernel grid is shaped by the target device -- always 2 for atomics
  spn_device_enqueue_kernel(runtime->device,SPN_DEVICE_KERNEL_ID_BLOCK_POOL_INIT_IDS,
                            cq,k0,runtime->block_pool.size->pool_size,
                            0,NULL,NULL);

  // init atomics
  cl(SetKernelArg(k1,0,sizeof(runtime->block_pool.atomics.drw),&runtime->block_pool.atomics.drw));
  cl(SetKernelArg(k1,1,SPN_CL_ARG(runtime->block_pool.size->pool_size)));

  // the kernel grid is shaped by the target device
  spn_device_enqueue_kernel(runtime->device,SPN_DEVICE_KERNEL_ID_BLOCK_POOL_INIT_ATOMICS,
                            cq,k1,2,
                            0,NULL,NULL);

  // kickstart kernel execution
  cl(Flush(cq));

  // release kernels
  cl(ReleaseKernel(k0));
  cl(ReleaseKernel(k1));
}

static
void
spn_block_pool_dispose(struct spn_runtime * const runtime)
{
  spn_extent_phr_pdrw_free(runtime,&runtime->block_pool.atomics);
  spn_extent_pdrw_free    (runtime,&runtime->block_pool.ids);
  spn_extent_pdrw_free    (runtime,&runtime->block_pool.blocks);
}

//
//
//

static
bool
spn_device_yield(struct spn_runtime * const runtime)
{
  return spn_scheduler_yield(runtime->scheduler);
}

static
void
spn_device_wait(struct spn_runtime * const runtime)
{
  spn_scheduler_wait(runtime->scheduler);
}

//
//
//

spn_result
spn_device_cl_12_create(struct spn_context * const context,
                         cl_context                 context_cl,
                         cl_device_id               device_id_cl)
{
  // allocate the runtime
  struct spn_runtime * const runtime = malloc(sizeof(*runtime));

  // save off CL objects
  runtime->cl.context   = context_cl;
  runtime->cl.device_id = device_id_cl;

  // query device alignment
  cl_uint align_bits;

  cl(GetDeviceInfo(device_id_cl,
                   CL_DEVICE_MEM_BASE_ADDR_ALIGN,
                   sizeof(align_bits),
                   &align_bits,
                   NULL));

  runtime->cl.align_bytes = align_bits / 8;

  // create device
  spn_device_create(runtime);

  // create the host and device allocators
  spn_allocator_host_create(runtime);
  spn_allocator_device_create(runtime);

  // how many slots in the scheduler?
  runtime->scheduler = spn_scheduler_create(runtime,runtime->config->scheduler.size);

  // allocate deps structure
  runtime->deps      = spn_grid_deps_create(runtime,
                                            runtime->scheduler,
                                            runtime->config->block_pool.pool_size);

  // initialize cq pool
  spn_cq_pool_create(runtime,
                     &runtime->cq_pool,
                     runtime->config->cq_pool.cq_props,
                     runtime->config->cq_pool.size);

  // acquire in-order cq
  cl_command_queue cq = spn_device_acquire_cq_in_order(runtime);

  // initialize block pool
  spn_block_pool_create(runtime,cq);

  // intialize handle pool
  spn_handle_pool_create(runtime,
                         &runtime->handle_pool,
                         runtime->config->handle_pool.size,
                         runtime->config->handle_pool.width,
                         runtime->config->handle_pool.recs);

  //
  // initialize pfns
  //
  // FIXME -- at this point we will have identified which device we've
  // targeted and will load a DLL (or select from a built-in library)
  // that contains all the pfns.
  //
  context->runtime        = runtime;

  context->yield          = spn_device_yield;
  context->wait           = spn_device_wait;

  context->path_builder   = spn_path_builder_cl_12_create;
  context->path_retain    = spn_device_path_host_retain;
  context->path_release   = spn_device_path_host_release;
  context->path_flush     = spn_device_path_host_flush;

  context->raster_builder = spn_raster_builder_cl_12_create;
  context->raster_retain  = spn_device_raster_host_retain;
  context->raster_release = spn_device_raster_host_release;
  context->raster_flush   = spn_device_raster_host_flush;

  context->composition    = spn_composition_cl_12_create;
  context->styling        = spn_styling_cl_12_create;

  context->surface        = spn_surface_cl_12_create;

  // block on pool creation
  cl(Finish(cq));

  // dispose of in-order cq
  spn_device_release_cq_in_order(runtime,cq);

  return SPN_SUCCESS;
};

//
//
//

spn_result
spn_device_cl_12_dispose(struct spn_context * const context)
{
  //
  // FIXME -- incomplete
  //
  fprintf(stderr,"%s incomplete!\n",__func__);

  struct spn_runtime * runtime = context->runtime;

  spn_allocator_device_dispose(runtime);
  spn_allocator_host_dispose(runtime);

  spn_scheduler_dispose(context->runtime,context->runtime->scheduler);

  spn_grid_deps_dispose(context->runtime->deps);

  spn_cq_pool_dispose(runtime,&runtime->cq_pool);

  spn_block_pool_dispose(context->runtime);

  // spn_handle_pool_dispose(context->runtime);

  return SPN_SUCCESS;
}

//
// REPORT BLOCK POOL ALLOCATION
//

void
spn_device_cl_12_debug(struct spn_context * const context)
{
  struct spn_runtime * const runtime = context->runtime;

  // acquire out-of-order cq
  cl_command_queue cq = spn_device_acquire_cq_in_order(runtime);

  // copy atomics to host
  spn_extent_phr_pdrw_read(&runtime->block_pool.atomics,cq,NULL);

  // block until complete
  cl(Finish(cq));

  // dispose of out-of-order cq
  spn_device_release_cq_in_order(runtime,cq);

  union spn_block_pool_atomic const * const bp_atomic = runtime->block_pool.atomics.hr;

  spn_uint const available = bp_atomic->writes - bp_atomic->reads;
  spn_uint const inuse     = runtime->config->block_pool.pool_size - available;

  fprintf(stderr,
          "writes/reads/avail/alloc: %9u / %9u / %9u = %6.2f MB / %9u = %6.2f MB\n",
          bp_atomic->writes,
          bp_atomic->reads,
          available,
          (available * runtime->config->block.bytes) / (1024.0*1024.0),
          inuse,
          (inuse     * runtime->config->block.bytes) / (1024.0*1024.0));
}

//
//
//

#endif

//
//
//

#if 0
spn_result
spn_device_raster_host_flush(struct spn_device  * const device,
                             spn_raster_t const *       rasters,
                             uint32_t                   count)
{
  spn_grid_deps_force(device->deps,rasters,count);

  return SPN_SUCCESS;
}

spn_result
spn_device_path_host_flush(struct spn_device * const device,
                           spn_path_t  const *       paths,
                           uint32_t                  count)
{
  spn_grid_deps_force(device->deps,paths,count);

  return SPN_SUCCESS;
}
#endif

//
//
//

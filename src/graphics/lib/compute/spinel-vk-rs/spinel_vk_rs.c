// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// Load Fuchsia Vulkan functions first
//
#if defined(__Fuchsia__)
#ifndef VK_USE_PLATFORM_FUCHSIA
#define VK_USE_PLATFORM_FUCHSIA
#endif
#else
#error "Unsupported WSI platform"
#endif

#include <vulkan/vulkan.h>

//
//
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/macros.h"
#include "common/vk/assert.h"
#include "common/vk/debug_utils.h"
#include "common/vk/pipeline_cache.h"
#include "spinel/platforms/vk/ext/find_target/find_target.h"
#include "spinel/platforms/vk/spinel_vk.h"
#include "spinel/spinel_assert.h"
#include "spinel_vk_rs.h"

//
// Define a platform-specific pipeline cache
//
#define SPN_PLATFORM_PIPELINE_CACHE_STRING "/cache/.spinel_vk_cache"

//
// What are the max number of queues?
//
// FIXME(allanmac): There should be no limits.
//
#define SPN_VK_Q_COMPUTE_MAX_QUEUES UINT32_MAX
#define SPN_VK_Q_PRESENT_MAX_QUEUES 1

//
//
//
struct spinel_vk_rs
{
  struct
  {
    VkPhysicalDevice              pd;
    VkDevice                      d;
    VkAllocationCallbacks const * ac;

    struct
    {
      struct
      {
        uint32_t                index;
        VkQueueFamilyProperties props;
      } compute;

      struct
      {
        uint32_t                index;
        VkQueueFamilyProperties props;

        struct
        {
          uint32_t count;
          uint32_t next;
          VkQueue  queues[SPN_VK_Q_PRESENT_MAX_QUEUES];
        } pool;
      } present;
    } q;
  } vk;

  struct
  {
    uint32_t          count;
    uint32_t          next;
    VkCommandPool *   pools;
    VkCommandBuffer * buffers;
    VkSemaphore *     timelines;
    uint64_t *        values;
    bool              is_valid;
  } cmd;

  struct
  {
    spinel_context_t        context;
    spinel_context_limits_t limits;
    spinel_swapchain_t      swapchain;
    VkExtent2D              extent;
    uint32_t                image_count;

    struct
    {
      struct
      {
        spinel_vk_swapchain_submit_ext_compute_acquire_t acquire;
        spinel_vk_swapchain_submit_ext_compute_fill_t    fill;
        spinel_vk_swapchain_submit_ext_compute_render_t  render;
        spinel_vk_swapchain_submit_ext_compute_release_t release;
      } compute;
      struct
      {
        spinel_vk_swapchain_submit_ext_graphics_signal_t signal;
        spinel_vk_swapchain_submit_ext_graphics_store_t  store;
        spinel_vk_swapchain_submit_ext_graphics_wait_t   wait;
      } graphics;
    } ext;
  } spinel;
};

//
// NOTE(allanmac): Validation layers either correctly or incorrectly identifying
// that the presentation queue submissions are hanging on to the command buffers
// a little longer than expected.
//
// The "+2" appears to resolve this when I expected a "+1" to be all that was
// required given the self-clocking behavior of the render loop.
//
// The assumption was that every swapchain image could be "in flight" and its
// associated command buffer in the post-submission "pending" state.  Adding one
// more command buffer enabled recording while the pending command buffers are
// in flight.
//
// Acquiring a fenced presentable doesn't impact this observation.
//
static void
spinel_vk_rs_cmd_create(struct spinel_vk_rs * rs, uint32_t image_count)
{
  uint32_t const count = image_count + 2;

  rs->cmd.count     = count;
  rs->cmd.next      = 0;
  rs->cmd.pools     = MALLOC_MACRO(count * sizeof(*rs->cmd.pools));
  rs->cmd.buffers   = MALLOC_MACRO(count * sizeof(*rs->cmd.buffers));
  rs->cmd.timelines = MALLOC_MACRO(count * sizeof(*rs->cmd.timelines));
  rs->cmd.values    = CALLOC_MACRO(count, sizeof(*rs->cmd.values));

  VkCommandPoolCreateInfo const cpci = {

    .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .pNext            = NULL,
    .flags            = 0,
    .queueFamilyIndex = rs->vk.q.present.index,
  };

  VkCommandBufferAllocateInfo cbai = {

    .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .pNext              = NULL,
    .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = 1,
    /* .commandPool     =  */  // updated on each iteration
  };

  VkSemaphoreTypeCreateInfo const stci = {

    .sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR,
    .pNext         = NULL,
    .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
    .initialValue  = 0UL
  };

  VkSemaphoreCreateInfo const sci = {

    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    .pNext = &stci,
    .flags = 0
  };

  for (uint32_t ii = 0; ii < count; ii++)
    {
      vk(CreateCommandPool(rs->vk.d, &cpci, rs->vk.ac, rs->cmd.pools + ii));

      cbai.commandPool = rs->cmd.pools[ii];

      vk(AllocateCommandBuffers(rs->vk.d, &cbai, rs->cmd.buffers + ii));

      vk(CreateSemaphore(rs->vk.d, &sci, rs->vk.ac, rs->cmd.timelines + ii));
    }

  rs->cmd.is_valid = true;
}

//
//
//
static void
spinel_vk_rs_cmd_destroy(struct spinel_vk_rs * rs)
{
  // VkCommand*
  for (uint32_t ii = 0; ii < rs->cmd.count; ii++)
    {
      vkDestroySemaphore(rs->vk.d, rs->cmd.timelines[ii], rs->vk.ac);
      vkFreeCommandBuffers(rs->vk.d, rs->cmd.pools[ii], 1, rs->cmd.buffers + ii);
      vkDestroyCommandPool(rs->vk.d, rs->cmd.pools[ii], rs->vk.ac);
    }

  free(rs->cmd.values);
  free(rs->cmd.timelines);
  free(rs->cmd.buffers);
  free(rs->cmd.pools);

  rs->cmd.is_valid = false;
}

//
//
//
static void
spinel_vk_rs_cmd_regen(struct spinel_vk_rs * rs, uint32_t image_count)
{
  if (rs->cmd.is_valid)
    {
      spinel_vk_rs_cmd_destroy(rs);
    }

  spinel_vk_rs_cmd_create(rs, image_count);
}

//
//
//
static void
spinel_vk_rs_q_create(struct spinel_vk_rs * rs)
{
  rs->vk.q.present.pool.count = MIN_MACRO(uint32_t,  //
                                          SPN_VK_Q_PRESENT_MAX_QUEUES,
                                          rs->vk.q.present.props.queueCount);
  rs->vk.q.present.pool.next  = 0;

  for (uint32_t ii = 0; ii < rs->vk.q.present.pool.count; ii++)
    {
      vkGetDeviceQueue(rs->vk.d, rs->vk.q.present.index, ii, rs->vk.q.present.pool.queues + ii);
    }
}

//
//
//
static VkQueue
spinel_vk_rs_q_next(struct spinel_vk_rs * rs)
{
  return rs->vk.q.present.pool.queues[rs->vk.q.present.pool.next++ %  //
                                      rs->vk.q.present.pool.count];
}

//
// This is very simple and is only possible because Spinel and the surface
// module will meter access to images.
//
static void
spinel_vk_rs_cb_next(struct spinel_vk_rs * rs,
                     VkCommandBuffer *     cb,
                     VkSemaphore *         timeline,
                     uint64_t *            value)
{
  uint32_t const next = rs->cmd.next++ % rs->cmd.count;

  VkSemaphoreWaitInfo const swi = {
    .sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
    .pNext          = NULL,
    .flags          = 0,
    .semaphoreCount = 1,
    .pSemaphores    = rs->cmd.timelines + next,
    .pValues        = rs->cmd.values + next,
  };

  vk(WaitSemaphores(rs->vk.d, &swi, UINT64_MAX));

  vk(ResetCommandPool(rs->vk.d, rs->cmd.pools[next], 0));

  *cb       = rs->cmd.buffers[next];
  *timeline = rs->cmd.timelines[next];
  *value    = ++rs->cmd.values[next];
}

//
//
//
VkResult
spinel_vk_rs_instance_create(spinel_vk_rs_instance_create_info_t const * instance_create_info,
                             VkInstance *                                instance)
{
  // define Vulkan 1.2 app
  //
  VkApplicationInfo const app_info = {

    .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .pNext              = NULL,
    .pApplicationName   = "Carnelian",
    .applicationVersion = 0,
    .pEngineName        = "Spinel/VK",
    .engineVersion      = 0,
    .apiVersion         = VK_API_VERSION_1_2
  };

  //
  // create a Vulkan instance
  //
  char const * const instance_layers[] = {
    //
    // additional layers here...
    //
    "VK_LAYER_KHRONOS_validation"  // keep this layer name last
  };

  char const * const instance_extensions[] = {
    //
    // additional extensions here...
    //
    VK_EXT_DEBUG_UTILS_EXTENSION_NAME,  // keep this instance name last
  };

  uint32_t const instance_layer_count = ARRAY_LENGTH_MACRO(instance_layers) -  //
                                        (instance_create_info->is_validation ? 0 : 1);

  uint32_t const instance_extension_count = ARRAY_LENGTH_MACRO(instance_extensions) -  //
                                            (instance_create_info->is_debug_info ? 0 : 1);

  VkInstanceCreateInfo const ici = {

    .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pNext                   = NULL,
    .flags                   = 0,
    .pApplicationInfo        = &app_info,
    .enabledLayerCount       = instance_layer_count,
    .ppEnabledLayerNames     = instance_layers,
    .enabledExtensionCount   = instance_extension_count,
    .ppEnabledExtensionNames = instance_extensions
  };

  VkResult const result = vkCreateInstance(&ici, NULL, instance);

  //
  // initialize debug util pfns
  //
  if (result == VK_SUCCESS)
    {
      if (instance_create_info->is_debug_info)
        {
          vk_debug_utils_init(*instance);
        }
    }

  return result;
}

//
//
//
spinel_vk_rs_t *
spinel_vk_rs_create(spinel_vk_rs_create_info_t const * create_info)
{
  //
  // Typical defaults:
  //
  //   uint32_t vendor_id               = 0;
  //   uint32_t device_id               = 0;
  //   uint32_t qfis[2]                 = { 0, 0 };
  //   uint64_t context_block_pool_size = 256UL << 20;  // 256 MB
  //   uint32_t context_handle_count    = 1 << 18;      // 256K handles
  //
  spinel_vk_rs_t * rs = CALLOC_MACRO(1, sizeof(*rs));

  //
  // acquire all physical devices
  //
  uint32_t pd_count;

  vk(EnumeratePhysicalDevices(create_info->instance, &pd_count, NULL));

  if (pd_count == 0)
    {
      fprintf(stderr, "No device found\n");

      free(rs);

      return NULL;
    }

  VkPhysicalDevice * pds = MALLOC_MACRO(pd_count * sizeof(*pds));

  vk(EnumeratePhysicalDevices(create_info->instance, &pd_count, pds));

  //
  // default to selecting the first id
  //
  VkPhysicalDeviceProperties pdp;

  vkGetPhysicalDeviceProperties(pds[0], &pdp);

  //
  // default vendor/device is the first physical device
  //
  uint32_t const vendor_id = (create_info->vendor_id == 0) ? pdp.vendorID : create_info->vendor_id;
  uint32_t const device_id = (create_info->device_id == 0) ? pdp.deviceID : create_info->device_id;

  //
  // list all devices
  //
  for (uint32_t ii = 0; ii < pd_count; ii++)
    {
      VkPhysicalDeviceProperties pdp_tmp;

      vkGetPhysicalDeviceProperties(pds[ii], &pdp_tmp);

      bool const is_match = (pdp_tmp.vendorID == vendor_id) &&  //
                            (pdp_tmp.deviceID == device_id);

      if (is_match)
        {
          pdp       = pdp_tmp;
          rs->vk.pd = pds[ii];
        }

#ifndef NDEBUG
      fprintf(stdout,
              "%c %8X : %X : %s\n",
              is_match ? '*' : ' ',
              pdp_tmp.vendorID,
              pdp_tmp.deviceID,
              pdp_tmp.deviceName);
#endif
    }

  if (rs->vk.pd == VK_NULL_HANDLE)
    {
      fprintf(stderr, "Error -- device %X : %X not found.\n", vendor_id, device_id);

      free(pds);
      free(rs);

      return NULL;
    }

  //
  // free physical devices
  //
  free(pds);

  //
  // find Spinel target
  //
  spinel_vk_target_t * target = spinel_vk_find_target(vendor_id, device_id);

  if (target == NULL)
    {
      fprintf(stderr, "Error: No target for %X:%X\n", vendor_id, device_id);

      free(rs);

      return NULL;
    }

  //
  // get queue properties
  //
  uint32_t qfp_count;

  vkGetPhysicalDeviceQueueFamilyProperties(rs->vk.pd, &qfp_count, NULL);

  VkQueueFamilyProperties qfp[qfp_count];

  vkGetPhysicalDeviceQueueFamilyProperties(rs->vk.pd, &qfp_count, qfp);

  //
  // make sure qfis[2] are in range
  //
  if ((create_info->qfis[0] >= qfp_count) || (create_info->qfis[1] >= qfp_count))
    {
      fprintf(stderr,
              "Error -- queue indices out of range: %u:%u >= [0-%u]:[0-%u].\n",
              create_info->qfis[0],
              create_info->qfis[1],
              qfp_count - 1,
              qfp_count - 1);

      free(rs);

      return NULL;
    }

  //
  // Validate a compute-capable queue has been selected.
  //
  if ((qfp[create_info->qfis[0]].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0)
    {
      fprintf(stderr,
              "Error -- .queueFamilyIndex %u does not not support VK_QUEUE_COMPUTE_BIT.\n",
              create_info->qfis[0]);

      free(rs);

      return NULL;
    }

  //
  // Validate a graphics-capable queue has been selected.
  //
  if ((qfp[create_info->qfis[1]].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)
    {
      fprintf(stderr,
              "Error -- .queueFamilyIndex %u does not not support VK_QUEUE_GRAPHICS_BIT.\n",
              create_info->qfis[0]);

      free(rs);

      return NULL;
    }

  //
  // TODO(allanmac): Validate a presentable queue has been selected.
  //

  //
  // save queue props and index
  //
  rs->vk.q.compute.index = create_info->qfis[0];
  rs->vk.q.compute.props = qfp[create_info->qfis[0]];
  rs->vk.q.present.index = create_info->qfis[1];
  rs->vk.q.present.props = qfp[create_info->qfis[1]];

  //
  // max queue sizes
  //
  uint32_t const vk_q_compute_count = MIN_MACRO(uint32_t,  //
                                                SPN_VK_Q_COMPUTE_MAX_QUEUES,
                                                rs->vk.q.compute.props.queueCount);

  uint32_t const vk_q_present_count = MIN_MACRO(uint32_t,  //
                                                SPN_VK_Q_PRESENT_MAX_QUEUES,
                                                rs->vk.q.present.props.queueCount);

  //
  // find max queue count
  //
  uint32_t const qps_size = MAX_MACRO(uint32_t, vk_q_compute_count, vk_q_present_count);

  //
  // default queue priorities
  //
  float qps[qps_size];

  for (uint32_t ii = 0; ii < qps_size; ii++)
    {
      qps[ii] = 1.0f;
    }

  //
  // These are the queues that will be used
  //
  VkDeviceQueueCreateInfo dqcis[2] = {
    { .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .pNext            = NULL,
      .flags            = 0,
      .queueFamilyIndex = rs->vk.q.compute.index,
      .queueCount       = vk_q_compute_count,
      .pQueuePriorities = qps },

    { .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .pNext            = NULL,
      .flags            = 0,
      .queueFamilyIndex = rs->vk.q.present.index,
      .queueCount       = vk_q_present_count,
      .pQueuePriorities = qps },
  };

  //
  // Are the queue families the same?  If so, then only list one.
  //
  bool const is_same_queue = (rs->vk.q.compute.index == rs->vk.q.present.index);

  //
  // probe Spinel device requirements for this target
  //
  spinel_vk_target_requirements_t spinel_tr = { 0 };

  spinel_vk_target_get_requirements(target, &spinel_tr);

  //
  // platform extensions
  //
  char const * platform_ext_names[] = {
    "VK_FUCHSIA_external_memory",
    "VK_FUCHSIA_buffer_collection",
    "VK_FUCHSIA_buffer_collection_x",
  };

  uint32_t const platform_ext_count = ARRAY_LENGTH_MACRO(platform_ext_names);
  uint32_t const ext_name_count     = spinel_tr.ext_name_count + platform_ext_count;

  char const * ext_names[ext_name_count];

  memcpy(ext_names, platform_ext_names, sizeof(*ext_names) * platform_ext_count);

  //
  // features
  //
  VkPhysicalDeviceVulkan12Features pdf12 = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
  };

  VkPhysicalDeviceVulkan11Features pdf11 = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
    .pNext = &pdf12
  };

  VkPhysicalDeviceFeatures2 pdf2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                                     .pNext = &pdf11 };

  //
  // populate Spinel device requirements
  //
  spinel_tr.ext_names = ext_names + platform_ext_count;
  spinel_tr.pdf       = &pdf2.features;
  spinel_tr.pdf11     = &pdf11;
  spinel_tr.pdf12     = &pdf12;

  if (!spinel_vk_target_get_requirements(target, &spinel_tr))
    {
      fprintf(stderr, "Error: spinel_vk_target_get_requirements() failure.\n");

      free(rs);

      return NULL;
    }

  //
  // create VkDevice
  //
  VkDeviceCreateInfo const vk_dci = {
    .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .pNext                   = &pdf2,
    .flags                   = 0,
    .queueCreateInfoCount    = is_same_queue ? 1 : 2,
    .pQueueCreateInfos       = dqcis,
    .enabledLayerCount       = 0,
    .ppEnabledLayerNames     = NULL,
    .enabledExtensionCount   = ext_name_count,
    .ppEnabledExtensionNames = ext_names,
    .pEnabledFeatures        = NULL,
  };

  vk(CreateDevice(rs->vk.pd, &vk_dci, NULL, &rs->vk.d));

  //
  // create pipeline cache
  //
  VkPipelineCache vk_pc;

  vk_ok(vk_pipeline_cache_create(rs->vk.d, NULL, SPN_PLATFORM_PIPELINE_CACHE_STRING, &vk_pc));

  //
  // save compute queue index and count
  //
  spinel_vk_context_create_info_t const cci = {
    .vk = {
      .pd = rs->vk.pd,
      .d  = rs->vk.d,
      .pc = vk_pc,
      .ac = rs->vk.ac,
      .q  = {
        .compute = {
          .flags        = dqcis[0].flags,
          .count        = dqcis[0].queueCount,
          .family_index = dqcis[0].queueFamilyIndex,
        },
        .shared  = {
          .family_count   = is_same_queue ? 1 : 2,
          .family_indices = {
            dqcis[0].queueFamilyIndex,
            dqcis[1].queueFamilyIndex,
          },
        },
      },
    },
    .target          = target,
    .block_pool_size = create_info->context_block_pool_size,
    .handle_count    = create_info->context_handle_count,
  };

  rs->spinel.context = spinel_vk_context_create(&cci);

  if (rs->spinel.context == NULL)
    {
      fprintf(stderr, "Error: failed to create context!\n");

      vkDestroyDevice(rs->vk.d, NULL);

      free(rs);

      return NULL;
    }

  //
  // the target is no longer needed
  //
  spinel_vk_target_dispose(target);

  //
  // destroy pipeline cache
  //
  vk_ok(vk_pipeline_cache_destroy(rs->vk.d, NULL, SPN_PLATFORM_PIPELINE_CACHE_STRING, vk_pc));

  //
  // Get context limits
  //
  spinel(context_get_limits(rs->spinel.context, &rs->spinel.limits));

  //
  // set up rendering extensions
  //
  // TODO(allanmac): Carnelian isn't plumbing a presentation surface wait down
  // to this level
  //
  rs->spinel.ext.graphics.signal = (spinel_vk_swapchain_submit_ext_graphics_signal_t){
    .ext    = NULL,
    .type   = SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_GRAPHICS_SIGNAL,
    .signal = {
      .count = 1,
      //
      // .semaphores[]
      // .values[]
      //
    },
  };

  rs->spinel.ext.graphics.store = (spinel_vk_swapchain_submit_ext_graphics_store_t){
    .ext                = &rs->spinel.ext.graphics.signal,
    .type               = SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_GRAPHICS_STORE,
    .queue_family_index = create_info->qfis[1],
    //
    // .extent_index
    // .cb
    // .queue
    // .layout_prev
    // .image
    // .image_info.imageView
    // .image_info.imageLayout
    //
  };

  //
  // TODO(allanmac): Carnelian isn't plumbing a presentation surface wait down
  // to this level
  //
  rs->spinel.ext.graphics.wait = (spinel_vk_swapchain_submit_ext_graphics_wait_t){
    .ext  = &rs->spinel.ext.graphics.store,
    .type = SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_GRAPHICS_WAIT,
    .wait = {
      .count  = 0,
      .stages = { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT },
      //
      // .semaphores[]
      // .values[]
      //
    },
  };

  rs->spinel.ext.compute.acquire = (spinel_vk_swapchain_submit_ext_compute_acquire_t){
    .ext                     = &rs->spinel.ext.graphics.wait,
    .type                    = SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_COMPUTE_ACQUIRE,
    .from_queue_family_index = create_info->qfis[1],
  };

  rs->spinel.ext.compute.fill = (spinel_vk_swapchain_submit_ext_compute_fill_t){
    .ext   = &rs->spinel.ext.compute.acquire,
    .type  = SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_COMPUTE_FILL,
    .dword = 0xFFFFFFFF,
  };

  rs->spinel.ext.compute.render = (spinel_vk_swapchain_submit_ext_compute_render_t){
    .ext  = NULL,  // &ext_graphics_wait or &ext_compute_acquire
    .type = SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_COMPUTE_RENDER,
    //
    // .clip = { 0, 0, ... },
    // .extent_index
    //
  };

  rs->spinel.ext.compute.release = (spinel_vk_swapchain_submit_ext_compute_release_t){
    .ext                   = NULL,  // &ext_compute_fill or &ext_compute_acquire,
    .type                  = SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_COMPUTE_RELEASE,
    .to_queue_family_index = create_info->qfis[1],
  };

  //
  // Create presentation queues
  //
  spinel_vk_rs_q_create(rs);

  //
  // Success but `spinel_vk_rs_regen()` must be called before rendering.
  //
  return rs;
}

//
// Regen will either succeed or terminally fail
//
void
spinel_vk_rs_regen(spinel_vk_rs_t * rs, uint32_t width, uint32_t height, uint32_t image_count)
{
  //
  // Regen spinel swapchain
  //
  if (rs->spinel.swapchain != NULL)
    {
      spinel(swapchain_release(rs->spinel.swapchain));
    }

  spinel_swapchain_create_info_t const create_info = {
    .extent = {
      .width  = width,
      .height = height,
    },
    .count = image_count,
  };

  spinel(swapchain_create(rs->spinel.context, &create_info, &rs->spinel.swapchain));

  spinel_vk_rs_cmd_regen(rs, image_count);
}

//
//
//
void
spinel_vk_rs_render(spinel_vk_rs_t *                         rs,
                    spinel_styling_t                         styling,
                    spinel_composition_t                     composition,
                    spinel_vk_rs_render_image_info_t const * image_info)
{
  //
  // Is this a new presentable with an implicit undefined layout?
  //
  bool const is_layout_undefined = (image_info->layout.prev == VK_IMAGE_LAYOUT_UNDEFINED);

  if (is_layout_undefined)
    {
      // compute_render -> compute_fill -> graphics_wait -> ...
      rs->spinel.ext.compute.render.ext = &rs->spinel.ext.compute.fill;
      rs->spinel.ext.compute.fill.ext   = &rs->spinel.ext.graphics.wait;
    }
  else
    {
      // compute_render -> compute_fill -> compute_acquire -> graphics_wait -> ...
      rs->spinel.ext.compute.render.ext = &rs->spinel.ext.compute.fill;
      rs->spinel.ext.compute.fill.ext   = &rs->spinel.ext.compute.acquire;
    }

  //
  // Update compute render extension for this presentable
  //
  rs->spinel.ext.compute.render.clip         = image_info->clip;
  rs->spinel.ext.compute.render.extent_index = image_info->image_index;

  //
  // Wait on presentable's "wait" semaphore
  //
  // TODO(allanmac): Carnelian isn't plumbing a presentation surface wait/signal
  // down to this level
  //
  // ext_graphics_wait.wait.semaphores[0] = presentable->wait.semaphore;
  //

  //
  // Update graphics store extension for this presentable
  //
  rs->spinel.ext.graphics.store.extent_index           = image_info->image_index;
  rs->spinel.ext.graphics.store.queue                  = spinel_vk_rs_q_next(rs);
  rs->spinel.ext.graphics.store.layout_prev            = image_info->layout.prev;
  rs->spinel.ext.graphics.store.image                  = image_info->image;
  rs->spinel.ext.graphics.store.image_info.imageView   = image_info->image_view;
  rs->spinel.ext.graphics.store.image_info.imageLayout = image_info->layout.curr;

  //
  // Signal presentable's "signal" semaphore
  //
  // TODO(allanmac): Carnelian isn't plumbing a presentation surface wait/signal
  // down to this level
  //
  // ext_graphics_signal.signal.semaphores[0] = presentable->signal;
  //

  //
  // Get a command buffer and its associated availability semaphore and store it
  // to index 0.  Store it to index 1 when the surface semaphores are plumbed.
  //
  spinel_vk_rs_cb_next(rs,
                       &rs->spinel.ext.graphics.store.cb,
                       rs->spinel.ext.graphics.signal.signal.semaphores + 0,
                       rs->spinel.ext.graphics.signal.signal.values + 0);

  //
  // Submit compute work
  //
  spinel_swapchain_submit_t const swapchain_submit = {
    .ext         = &rs->spinel.ext.compute.release,
    .styling     = styling,
    .composition = composition,
  };

  spinel(swapchain_submit(rs->spinel.swapchain, &swapchain_submit));
}

//
//
//
void
spinel_vk_rs_destroy(spinel_vk_rs_t * rs)
{
  // release the swapchain
  if (rs->spinel.swapchain != NULL)
    {
      spinel_swapchain_release(rs->spinel.swapchain);
    }

  // release the Spinel context
  spinel(context_release(rs->spinel.context));

  // VkQueue
  // -- nothing to destroy

  // VkCommand*
  spinel_vk_rs_cmd_destroy(rs);

  // VkDevice
  vkDestroyDevice(rs->vk.d, NULL);

  // Done...
  free(rs);
}

//
//
//
VkResult
spinel_vk_rs_get_physical_device_props(VkInstance                   instance,
                                       uint32_t *                   props_count,
                                       VkPhysicalDeviceProperties * props)
{
  if (props_count == NULL)
    {
      return VK_INCOMPLETE;
    }

  uint32_t pd_count;

  VkResult result = vkEnumeratePhysicalDevices(instance, &pd_count, NULL);

  if (props == NULL)
    {
      *props_count = pd_count;

      return result;
    }

  *props_count = MIN_MACRO(uint32_t, pd_count, *props_count);

  VkPhysicalDevice * pds = MALLOC_MACRO(sizeof(*pds) * *props_count);

  result = vkEnumeratePhysicalDevices(instance, props_count, pds);

  for (uint32_t ii = 0; ii < *props_count; ii++)
    {
      vkGetPhysicalDeviceProperties(pds[ii], props + ii);
    }

  free(pds);

  return result;
}

//
//
//

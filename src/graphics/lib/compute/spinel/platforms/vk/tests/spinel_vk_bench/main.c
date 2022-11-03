// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// Vulkan WSI platforms are included first
//

#if defined(__linux__)
#define VK_USE_PLATFORM_XCB_KHR
#elif defined(__Fuchsia__)
// VK_USE_PLATFORM_FUCHSIA is already defined
#else
#error "Unsupported WSI platform"
#endif

#include <vulkan/vulkan.h>

//
//
//

#include <getopt.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/macros.h"
#include "common/vk/assert.h"
#include "common/vk/debug_utils.h"
#include "common/vk/find_mem_type_idx.h"
#include "common/vk/pipeline_cache.h"
#include "spinel/platforms/vk/ext/find_target/find_target.h"
#include "spinel/platforms/vk/spinel_vk.h"
#include "spinel/spinel_assert.h"
#include "svg/svg.h"
#include "widget/coords.h"
#include "widget/fps.h"
#include "widget/mouse.h"
#include "widget/svg.h"

//
//
//

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

//////////////////////////////////////////////
//
// Define a platform-specific prefix for the pipeline cache
//

#ifdef __Fuchsia__
#define SPN_PLATFORM_PIPELINE_CACHE_PREFIX_STRING "/cache/."
#else
#define SPN_PLATFORM_PIPELINE_CACHE_PREFIX_STRING "."
#endif

//////////////////////////////////////////////
//
// LINUX
//
// clang-format off
//
#if defined(__linux__)

#include "surface/surface_xcb.h"

#define SPN_PLATFORM_EXTENSION_NAMES         // For example: VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME

#define SPN_PLATFORM_MIN_IMAGE_COUNT         2

#define SPN_PLATFORM_PRESENT_MODE            VK_PRESENT_MODE_FIFO_KHR
                                             // VK_PRESENT_MODE_IMMEDIATE_KHR
                                             // VK_PRESENT_MODE_MAILBOX_KHR
                                             // VK_PRESENT_MODE_FIFO_RELAXED_KHR

#define SPN_PLATFORM_IMAGE_VIEW_FORMAT       VK_FORMAT_B8G8R8A8_UNORM

#define SPN_PLATFORM_SURFACE_FORMAT          (VkSurfaceFormatKHR){ .format     = SPN_PLATFORM_IMAGE_VIEW_FORMAT, \
                                                                   .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };

//////////////////////////////////////////////
//
// FUCHSIA
//
#elif defined(__Fuchsia__)

#include "surface/surface_fuchsia_fb.h"

#define SPN_PLATFORM_EXTENSION_NAMES

#define SPN_PLATFORM_MIN_IMAGE_COUNT         2

#define SPN_PLATFORM_PRESENT_MODE            VK_PRESENT_MODE_FIFO_KHR
                                             // VK_PRESENT_MODE_MAILBOX_KHR
                                             // VK_PRESENT_MODE_IMMEDIATE_KHR
                                             // VK_PRESENT_MODE_FIFO_RELAXED_KHR

#define SPN_PLATFORM_IMAGE_VIEW_FORMAT       VK_FORMAT_R8G8B8A8_UNORM

#define SPN_PLATFORM_SURFACE_FORMAT          (VkSurfaceFormatKHR){ .format     = SPN_PLATFORM_IMAGE_VIEW_FORMAT, \
                                                                   .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };

//////////////////////////////////////////////
//
// UNSUPPORTED
//
#else
#error "Unsupported WSI platform"
#endif

//
// clang-format on
//

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

#define SPN_ACQUIRE_DEFAULT_TIMEOUT ((uint64_t)15e9)  // 15 seconds

//
// Support acquiring either a fenced or unfenced presentable
//
typedef VkResult  //
  (*spinel_acquire_presentable_pfn_t)(VkDevice                                  vk_d,
                                      struct surface * const                    surface,
                                      struct surface_presentable const ** const presentable,
                                      void *                                    payload);

//
// Acquire a fenced presentable
//
static VkResult
spinel_acquire_fenced_presentable(VkDevice                            vk_d,
                                  struct surface *                    surface,
                                  struct surface_presentable const ** presentable,
                                  void *                              payload)
{
  //
  // Wait for fence to signal
  //
  VkFence  fence;
  VkResult result = surface_next_fence(surface, &fence);

  switch (result)
    {
      case VK_SUCCESS:
        result = vkWaitForFences(vk_d, 1, &fence, VK_TRUE, SPN_ACQUIRE_DEFAULT_TIMEOUT);

        if (result != VK_SUCCESS)
          {
            return result;
          }
        break;

      case VK_ERROR_OUT_OF_DATE_KHR:
      case VK_ERROR_INITIALIZATION_FAILED:
      case VK_ERROR_DEVICE_LOST:
      default:
        return result;
    }

  //
  // Fence is signaled so attempt to acquire a presentable
  //
  result = surface_acquire(surface, SPN_ACQUIRE_DEFAULT_TIMEOUT, presentable, payload);

  return result;
}

//
// Acquire an unfenced presentable
//
static VkResult
spinel_acquire_unfenced_presentable(VkDevice                            vk_d,
                                    struct surface *                    surface,
                                    struct surface_presentable const ** presentable,
                                    void *                              payload)
{
  VkResult const result = surface_acquire(surface,  //
                                          SPN_ACQUIRE_DEFAULT_TIMEOUT,
                                          presentable,
                                          payload);

  return result;
}

//
//
//
static void
spinel_usage(char * const argv[])
{
  static char const * const pms[] = { "VK_PRESENT_MODE_IMMEDIATE_KHR",
                                      "VK_PRESENT_MODE_MAILBOX_KHR",
                                      "VK_PRESENT_MODE_FIFO_KHR",
                                      "VK_PRESENT_MODE_FIFO_RELAXED_KHR" };
  //
  // clang-format off
  //
  fprintf(stderr,
          "\n"
          "Usage: %s -f <filename> [...]\n"
          " -h                        Print usage.\n"
          " -d <vendorID>:<deviceID>  Execute on a specific Vulkan physical device.  Defaults to first device.\n"
          " -f <filename>             Filename of SVG file.\n"
          " -i <min image count>      Minimum number of images in swapchain. Defaults to %u.\n"
          " -j <pipeline stage>       Select which pipeline stages are enabled on the first loop.    Defaults to `11111`.\n"
          " -k <pipeline stage>       Select which pipeline stages are enabled after the first loop. Defaults to `11111`.\n"
          " -n <frames>               Maximum frames before exiting. Defaults to UINT32_MAX\n"
          " -p <present mode>         Select present mode [0-3]*. Defaults to %u/%s.\n"
          " -q <compute>:<present>    Select the compute and presentation queue family indices.  Defaults to `0:0`\n"
          " -r                        Rotate the SVG file around the origin.  Disabled by default.\n"
          " -t <seconds>              Maximum seconds before exiting. Defaults to UINT32_MAX\n"
          " -v                        Verbose SVG parsing output.  Quiet by default.\n"
          " -F                        Use VkFences to meter swapchain image acquires.\n"
          " -Q                        Disable Vulkan validation layers.  Enabled by default.\n"
          " -D                        Disable Vulkan debug info labels.  Enabled by default.\n"
          " -X                        Skip clearing the image entirely before every render.\n"
          " -c <x>,<y>:<scale>        (<x>,<y>) is the SVG center. Scale by <scale> and translate to center of surface.\n"
          "\n"
          " * Present Modes\n"
          "   -------------\n"
          "   0 : %s *\n"
          "   1 : %s\n"
          "   2 : %s\n"
          "   3 : %s *\n"
          "   * may result in tearing\n"
          "\n",
          argv[0],
          SPN_PLATFORM_MIN_IMAGE_COUNT,
          SPN_PLATFORM_PRESENT_MODE,
          pms[SPN_PLATFORM_PRESENT_MODE],
          pms[0],
          pms[1],
          pms[2],
          pms[3]);
  //
  // clang-format on
  //
}

//
//
//
struct spinel_state
{
  spinel_context_t      context;
  spinel_swapchain_t    swapchain;
  VkExtent2D            extent;
  uint32_t              image_count;
  struct widget_control initial;
  struct widget_control control;

  struct
  {
    float cx;
    float cy;
    float scale;
  } center;

  uint64_t t0;

  bool is_center;
  bool is_rotate;
  bool is_exit;
};

//
//
//
struct spinel_vk
{
  VkInstance                    i;
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

  struct
  {
    uint32_t          count;
    uint32_t          next;
    VkCommandPool *   pools;
    VkCommandBuffer * buffers;
    VkSemaphore *     timelines;
    uint64_t *        values;
  } cmd;
};

//
//
//
static void
spinel_secs_set(struct spinel_state * state)
{
  struct timespec ts;

  timespec_get(&ts, TIME_UTC);

  state->t0 = ts.tv_sec * 1000000000UL + ts.tv_nsec;
}

static bool
spinel_secs_lte(struct spinel_state * state, uint32_t seconds)
{
  struct timespec ts;

  timespec_get(&ts, TIME_UTC);

  uint64_t const t1 = ts.tv_sec * 1000000000UL + ts.tv_nsec;

  uint64_t const elapsed = t1 - state->t0;

  return (elapsed <= 1000000000UL * seconds);
}

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
spinel_vk_cmd_create(struct spinel_vk * vk, uint32_t image_count)
{
  uint32_t const count = image_count + 2;

  vk->cmd.count     = count;
  vk->cmd.next      = 0;
  vk->cmd.pools     = MALLOC_MACRO(count * sizeof(*vk->cmd.pools));
  vk->cmd.buffers   = MALLOC_MACRO(count * sizeof(*vk->cmd.buffers));
  vk->cmd.timelines = MALLOC_MACRO(count * sizeof(*vk->cmd.timelines));
  vk->cmd.values    = CALLOC_MACRO(count, sizeof(*vk->cmd.values));

  VkCommandPoolCreateInfo const cpci = {

    .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .pNext            = NULL,
    .flags            = 0,
    .queueFamilyIndex = vk->q.present.index,
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
      vk(CreateCommandPool(vk->d, &cpci, vk->ac, vk->cmd.pools + ii));

      cbai.commandPool = vk->cmd.pools[ii];

      vk(AllocateCommandBuffers(vk->d, &cbai, vk->cmd.buffers + ii));

      vk(CreateSemaphore(vk->d, &sci, vk->ac, vk->cmd.timelines + ii));
    }
}

//
//
//
static void
spinel_vk_cmd_destroy(struct spinel_vk * vk)
{
  // VkCommand*
  for (uint32_t ii = 0; ii < vk->cmd.count; ii++)
    {
      vkDestroySemaphore(vk->d, vk->cmd.timelines[ii], vk->ac);
      vkFreeCommandBuffers(vk->d, vk->cmd.pools[ii], 1, vk->cmd.buffers + ii);
      vkDestroyCommandPool(vk->d, vk->cmd.pools[ii], vk->ac);
    }

  free(vk->cmd.values);
  free(vk->cmd.timelines);
  free(vk->cmd.buffers);
  free(vk->cmd.pools);
}

//
//
//
static void
spinel_vk_cmd_regen(struct spinel_vk * vk, uint32_t image_count)
{
  spinel_vk_cmd_destroy(vk);

  spinel_vk_cmd_create(vk, image_count);
}

//
//
//
static void
spinel_vk_q_cmd_create(struct spinel_vk * vk, uint32_t image_count)
{
  vk->q.present.pool.count = MIN_MACRO(uint32_t,  //
                                       SPN_VK_Q_PRESENT_MAX_QUEUES,
                                       vk->q.present.props.queueCount);
  vk->q.present.pool.next  = 0;

  for (uint32_t ii = 0; ii < vk->q.present.pool.count; ii++)
    {
      vkGetDeviceQueue(vk->d, vk->q.present.index, ii, vk->q.present.pool.queues + ii);
    }

  spinel_vk_cmd_create(vk, image_count);
}

//
//
//
static VkQueue
spinel_vk_q_next(struct spinel_vk * vk)
{
  return vk->q.present.pool.queues[vk->q.present.pool.next++ % vk->q.present.pool.count];
}

//
// This is very simple and is only possible because Spinel and the surface
// module will meter access to images.
//
static void
spinel_vk_cb_next(struct spinel_vk * vk,
                  VkCommandBuffer *  cb,
                  VkSemaphore *      timeline,
                  uint64_t *         value)
{
  uint32_t const next = vk->cmd.next++ % vk->cmd.count;

  VkSemaphoreWaitInfo const swi = {
    .sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
    .pNext          = NULL,
    .flags          = 0,
    .semaphoreCount = 1,
    .pSemaphores    = vk->cmd.timelines + next,
    .pValues        = vk->cmd.values + next,
  };

  vk(WaitSemaphores(vk->d, &swi, UINT64_MAX));

  vk(ResetCommandPool(vk->d, vk->cmd.pools[next], 0));

  *cb       = vk->cmd.buffers[next];
  *timeline = vk->cmd.timelines[next];
  *value    = ++vk->cmd.values[next];
}

//
//
//
static void
spinel_vk_destroy(struct spinel_vk * vk)
{
  // VkQueue
  // -- nothing to destroy

  // VkCommand*
  spinel_vk_cmd_destroy(vk);

  // VkDevice
  vkDestroyDevice(vk->d, NULL);

  // VkInstance
  vkDestroyInstance(vk->i, NULL);
}

//
//
//
static void
spinel_state_input(void * data, struct surface_event const * event)
{
  struct spinel_state * const state = data;

  switch (event->type)
    {
        case SURFACE_EVENT_TYPE_EXIT: {
          state->is_exit = true;
          break;
        }

        case SURFACE_EVENT_TYPE_KEYBOARD_PRESS: {
          switch (event->keyboard.code)
            {
              case SURFACE_KEY_1:
                state->control.paths ^= true;
                break;

              case SURFACE_KEY_2:
                state->control.rasters ^= true;
                break;

              case SURFACE_KEY_3:
                state->control.styling ^= true;
                break;

              case SURFACE_KEY_4:
                state->control.composition ^= true;
                break;

              case SURFACE_KEY_5:
                state->control.render ^= true;
                break;

              case SURFACE_KEY_6:
                state->control.flags = 0;
                break;

              case SURFACE_KEY_R:
                state->is_rotate ^= true;
                break;

              case SURFACE_KEY_ESCAPE:
                state->is_exit = true;
                break;

              default:
                break;
            }
          break;
        }

        case SURFACE_EVENT_TYPE_TOUCH_INPUT_CONTACT_COUNT: {
          if (event->touch.contact_count.curr == 4)
            {
              state->is_rotate ^= true;
            }
          else if (event->touch.contact_count.curr == 5)
            {
              state->is_exit = true;
            }
          break;
        }

        default: {
          break;
        }
    }
}

//
// Regen will either succeed or terminally fail
//
static void
spinel_surface_regen(struct surface * surface, struct spinel_state * state)
{
  VkResult result = surface_regen(surface, &state->extent, &state->image_count);

  switch (result)
    {
      case VK_SUCCESS:
        break;

      case VK_ERROR_DEVICE_LOST:
        vk_ok(result);  // fatal -- vk_ok() aborts
        break;

      case VK_ERROR_OUT_OF_HOST_MEMORY:
      case VK_ERROR_OUT_OF_DEVICE_MEMORY:
      case VK_ERROR_SURFACE_LOST_KHR:
      case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
      case VK_ERROR_INITIALIZATION_FAILED:
      default:
        vk_ok(result);  // fatal -- vk_ok() aborts
        break;
    }

  //
  // Regen spinel swapchain
  //
  if (state->swapchain != NULL)
    {
      spinel(swapchain_release(state->swapchain));
    }

  spinel_swapchain_create_info_t const create_info = {
    .extent = {
      .width  = state->extent.width,
      .height = state->extent.height,
    },
    .count = state->image_count,
  };

  spinel(swapchain_create(state->context, &create_info, &state->swapchain));
}

//
//
//
int
main(int argc, char * const argv[])
{
  //
  // set defaults
  //
  uint32_t         vendor_id              = 0;
  uint32_t         device_id              = 0;
  uint32_t         min_image_count        = SPN_PLATFORM_MIN_IMAGE_COUNT;
  uint32_t         frame_count            = UINT32_MAX;
  uint32_t         seconds                = UINT32_MAX;
  uint32_t         qfis[2]                = { 0, 0 };
  VkPresentModeKHR present_mode           = SPN_PLATFORM_PRESENT_MODE;
  bool             is_verbose             = false;
  VkBool32         is_fence_acquired      = false;
  bool             is_validation          = true;
  bool             is_debug_info          = true;
  bool             is_clear_before_render = true;
  char const *     filename               = NULL;

  //
  // initial state of widgets
  //
  struct spinel_state state =  //
    {                          //
      .initial = WIDGET_CONTROL_PRSCR(),
      .control = WIDGET_CONTROL_PRSCR()
    };

  //
  // process options
  //
  int opt;

  while ((opt = getopt(argc, argv, "c:d:f:i:j:k:n:p:q:t:R:rvFQDXh")) != EOF)
    {
      switch (opt)
        {
          case 'c':
            // formatting
            {
              state.is_center    = true;
              state.center.scale = 1.0f;

              char * str_comma;

              state.center.cx = strtof(optarg, &str_comma);

              if (str_comma != optarg)
                {
                  if (*str_comma == ',')
                    {
                      char * str_colon;

                      state.center.cy = strtof(str_comma + 1, &str_colon);

                      if (*str_colon == ':')
                        {
                          state.center.scale = strtof(str_colon + 1, NULL);
                        }
                    }
                }
            }
            break;

          case 'd':
            // formatting
            {
              char * str_colon;

              vendor_id = (uint32_t)strtoul(optarg, &str_colon, 16);  // returns 0 on error

              if (str_colon != optarg)
                {
                  if (*str_colon == ':')
                    {
                      device_id = (uint32_t)strtoul(str_colon + 1, NULL, 16);  // returns 0 on error
                    }
                }
            }
            break;

          case 'f':
            filename = optarg;
            break;

          case 'i':
            min_image_count = (uint32_t)strtoul(optarg, NULL, 10);
            min_image_count = MAX_MACRO(uint32_t, 1, min_image_count);
            break;

          case 'j':
            state.initial.flags = (uint32_t)strtoul(optarg, NULL, 2);
            break;

          case 'k':
            state.control.flags = (uint32_t)strtoul(optarg, NULL, 2);
            break;

          case 'n':
            frame_count = (uint32_t)strtoul(optarg, NULL, 10);
            frame_count = MAX_MACRO(uint32_t, 1, frame_count);
            break;

          case 'p':
            present_mode = (uint32_t)strtoul(optarg, NULL, 10);
            present_mode = MIN_MACRO(uint32_t, present_mode, VK_PRESENT_MODE_FIFO_RELAXED_KHR);
            break;

          case 'q':
            qfis[0] = (uint32_t)strtoul(optarg, NULL, 10);                   // returns 0 on error
            qfis[1] = (uint32_t)strtoul(strchr(optarg, ':') + 1, NULL, 10);  // returns 0 on error
            break;

          case 'r':
            state.is_rotate ^= true;
            break;

          case 't':
            seconds = (uint32_t)strtoul(optarg, NULL, 10);
            break;

          case 'v':
            is_verbose = true;
            break;

          case 'F':
            is_fence_acquired = VK_TRUE;
            break;

          case 'Q':
            is_validation = false;
            break;

          case 'D':
            is_debug_info = false;
            break;

          case 'X':
            is_clear_before_render = false;
            break;

          case 'h':
          default:
            spinel_usage(argv);
            return EXIT_FAILURE;
        }
    }

  //
  // Vulkan handles that we'll need until shutdown
  //
  struct spinel_vk vk = { 0 };

  //
  // define Vulkan 1.2 app
  //
  VkApplicationInfo const app_info = {

    .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .pNext              = NULL,
    .pApplicationName   = "Fuchsia Spinel/VK Bench",
    .applicationVersion = 0,
    .pEngineName        = "Fuchsia Spinel/VK",
    .engineVersion      = 0,
    .apiVersion         = VK_API_VERSION_1_2
  };

  //
  // create a Vulkan instance
  //
  char const * const instance_layers[] = {
#if defined(__Fuchsia__)
    "VK_LAYER_FUCHSIA_imagepipe_swapchain_fb",
#endif
    //
    // additional layers here...
    //
    "VK_LAYER_KHRONOS_validation"  // keep this layer name last
  };

  char const * const instance_extensions[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
#if defined(__linux__)
    VK_KHR_XCB_SURFACE_EXTENSION_NAME,
#elif defined(__Fuchsia__)
    VK_FUCHSIA_IMAGEPIPE_SURFACE_EXTENSION_NAME,
#endif
    //
    // additional extensions here...
    //
    VK_EXT_DEBUG_UTILS_EXTENSION_NAME,  // keep this instance name last
  };

  uint32_t const instance_layer_count = ARRAY_LENGTH_MACRO(instance_layers) -  //
                                        (is_validation ? 0 : 1);

  uint32_t const instance_extension_count = ARRAY_LENGTH_MACRO(instance_extensions) -  //
                                            (is_debug_info ? 0 : 1);

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

  vk(CreateInstance(&ici, NULL, &vk.i));

  //
  // initialize debug util pfns
  //
  if (is_debug_info)
    {
      vk_debug_utils_init(vk.i);
    }

  //
  // acquire all physical devices
  //
  uint32_t pd_count;

  vk(EnumeratePhysicalDevices(vk.i, &pd_count, NULL));

  if (pd_count == 0)
    {
      fprintf(stderr, "No device found\n");

      return EXIT_FAILURE;
    }

  VkPhysicalDevice * pds = MALLOC_MACRO(pd_count * sizeof(*pds));

  vk(EnumeratePhysicalDevices(vk.i, &pd_count, pds));

  //
  // default to selecting the first id
  //
  VkPhysicalDeviceProperties pdp;

  vkGetPhysicalDeviceProperties(pds[0], &pdp);

  //
  // default vendor/device is the first physical device
  //
  if (vendor_id == 0)
    {
      vendor_id = pdp.vendorID;
    }

  if (device_id == 0)
    {
      device_id = pdp.deviceID;
    }

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
          pdp   = pdp_tmp;
          vk.pd = pds[ii];
        }

      fprintf(stdout,
              "%c %X : %X : %s\n",
              is_match ? '*' : ' ',
              pdp_tmp.vendorID,
              pdp_tmp.deviceID,
              pdp_tmp.deviceName);
    }

  if (vk.pd == VK_NULL_HANDLE)
    {
      fprintf(stderr, "Error -- device %X : %X not found.\n", vendor_id, device_id);

      return EXIT_FAILURE;
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

      return EXIT_FAILURE;
    }

  //
  // check that we have a valid filename before proceeding
  //
  if ((filename == NULL) || (optind != argc))
    {
      spinel_usage(argv);

      return EXIT_FAILURE;
    }

  //
  // try to load the svg doc
  //
  struct svg * svg_doc;

  svg_doc = svg_open(filename, is_verbose);

  if (svg_doc == NULL)
    {
      fprintf(stderr, "Not a valid SVG file: \"%s\"\n", filename);

      return EXIT_FAILURE;
    }

  //
  // create surface
  //
  struct surface * surface;

#if defined(__linux__)
  surface = surface_xcb_create(vk.i,
                               vk.ac,
                               (VkRect2D[]){ { .offset = { 100, 100 },  //
                                               .extent = { 1024, 1024 } } },
                               "Fuchsia Spinel/VK Bench");
#elif defined(__Fuchsia__)
  surface = surface_fuchsia_create(vk.i, vk.ac);
#else
#error "Unsupported WSI platform"
#endif

  if (surface == NULL)
    {
      fprintf(stderr, "Error -- surface creation failed!\n");
      exit(EXIT_FAILURE);
    }

  //
  // get queue properties
  //
  uint32_t qfp_count;

  vkGetPhysicalDeviceQueueFamilyProperties(vk.pd, &qfp_count, NULL);

  VkQueueFamilyProperties qfp[qfp_count];

  vkGetPhysicalDeviceQueueFamilyProperties(vk.pd, &qfp_count, qfp);

  //
  // make sure qfis[2] are in range
  //
  if ((qfis[0] >= qfp_count) || (qfis[1] >= qfp_count))
    {
      fprintf(stderr,
              "Error -- queue indices out of range: %u:%u >= [0-%u]:[0-%u].\n",
              qfis[0],
              qfis[1],
              qfp_count - 1,
              qfp_count - 1);
    }

  //
  // Validate a compute-capable queue has been selected.
  //
  if ((qfp[qfis[0]].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0)
    {
      fprintf(stderr,
              "Error -- .queueFamilyIndex %u does not not support VK_QUEUE_COMPUTE_BIT.\n",
              qfis[0]);

      exit(EXIT_FAILURE);
    }

  //
  // Validate a graphics-capable queue has been selected.
  //
  if ((qfp[qfis[1]].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)
    {
      fprintf(stderr,
              "Error -- .queueFamilyIndex %u does not not support VK_QUEUE_GRAPHICS_BIT.\n",
              qfis[0]);

      exit(EXIT_FAILURE);
    }

  //
  // Validate a presentable queue has been selected.
  //
  VkBool32 is_queue_presentable;

  vk(GetPhysicalDeviceSurfaceSupportKHR(vk.pd,
                                        qfis[1],
                                        surface_to_vk(surface),
                                        &is_queue_presentable));

  if (!is_queue_presentable)
    {
      fprintf(stderr,
              "Error -- .queueFamilyIndex %u does not not support surface presentation.\n",
              qfis[1]);

      exit(EXIT_FAILURE);
    }

  //
  // save queue props and index
  //
  vk.q.compute.index = qfis[0];
  vk.q.compute.props = qfp[qfis[0]];
  vk.q.present.index = qfis[1];
  vk.q.present.props = qfp[qfis[1]];

  //
  // max queue sizes
  //
  uint32_t const vk_q_compute_count = MIN_MACRO(uint32_t,  //
                                                SPN_VK_Q_COMPUTE_MAX_QUEUES,
                                                vk.q.compute.props.queueCount);

  uint32_t const vk_q_present_count = MIN_MACRO(uint32_t,  //
                                                SPN_VK_Q_PRESENT_MAX_QUEUES,
                                                vk.q.present.props.queueCount);

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
      .queueFamilyIndex = vk.q.compute.index,
      .queueCount       = vk_q_compute_count,
      .pQueuePriorities = qps },

    { .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .pNext            = NULL,
      .flags            = 0,
      .queueFamilyIndex = vk.q.present.index,
      .queueCount       = vk_q_present_count,
      .pQueuePriorities = qps },
  };

  //
  // Are the queue families the same?  If so, then only list one.
  //
  bool const is_same_queue = (vk.q.compute.index == vk.q.present.index);

  //
  // probe Spinel device requirements for this target
  //
  spinel_vk_target_requirements_t spinel_tr = { 0 };

  spinel_vk_target_get_requirements(target, &spinel_tr);

  //
  // platform extensions
  //
  char const * platform_ext_names[] = {

    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    SPN_PLATFORM_EXTENSION_NAMES
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

      exit(EXIT_FAILURE);
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
    .pEnabledFeatures        = NULL
  };

  vk(CreateDevice(vk.pd, &vk_dci, NULL, &vk.d));

  //
  // create pipeline cache
  //
  VkPipelineCache vk_pc;

  vk_ok(vk_pipeline_cache_create(vk.d,
                                 NULL,
                                 SPN_PLATFORM_PIPELINE_CACHE_PREFIX_STRING "spinel_vk_bench_cache",
                                 &vk_pc));

  //
  // save compute queue index and count
  //
  spinel_vk_context_create_info_t const cci = {
    .vk = {
      .pd = vk.pd,
      .d  = vk.d,
      .pc = vk_pc,
      .ac = vk.ac,
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
    .block_pool_size = 256UL << 20,  // 256 MB
    .handle_count    = 1 << 18,      // 256K handles
  };

  state.context = spinel_vk_context_create(&cci);

  if (state.context == NULL)
    {
      fprintf(stderr, "Error: failed to create context!\n");

      exit(EXIT_FAILURE);
    }

  //
  // the target is no longer needed
  //
  spinel_vk_target_dispose(target);

  //
  // destroy pipeline cache
  //
  vk_ok(vk_pipeline_cache_destroy(vk.d,
                                  NULL,
                                  SPN_PLATFORM_PIPELINE_CACHE_PREFIX_STRING "spinel_vk_bench_cache",
                                  vk_pc));

  //
  // Get context limits
  //
  spinel_context_limits_t limits;

  spinel(context_get_limits(state.context, &limits));

  //
  // create surface presentables
  //
  VkSurfaceFormatKHR const surface_format    = SPN_PLATFORM_SURFACE_FORMAT;
  VkFormat const           image_view_format = SPN_PLATFORM_IMAGE_VIEW_FORMAT;

  VkImageUsageFlags const image_usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |  //
                                        VK_IMAGE_USAGE_TRANSFER_DST_BIT |  //
                                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

  VkExtent2D const max_image_extent = { limits.extent.width,  //
                                        limits.extent.height };

  VkComponentMapping const image_view_components = {
    .r = VK_COMPONENT_SWIZZLE_IDENTITY,
    .g = VK_COMPONENT_SWIZZLE_IDENTITY,
    .b = VK_COMPONENT_SWIZZLE_IDENTITY,
    .a = VK_COMPONENT_SWIZZLE_IDENTITY,
  };

  vk_ok(surface_attach(surface,
                       vk.pd,
                       vk.d,
                       is_fence_acquired,
                       &surface_format,
                       min_image_count,
                       &max_image_extent,
                       image_usage,
                       image_view_format,
                       &image_view_components,
                       present_mode));

  //
  // create a transform stack
  //
  struct spinel_transform_stack * const ts = spinel_transform_stack_create(16);

  //
  // Apply world space transform (reflect over y=x at subpixel resolution)
  //
  spinel_transform_stack_push_transform(ts, &limits.global_transform);

  //
  // create builders
  //
  spinel_path_builder_t pb;

  spinel(path_builder_create(state.context, &pb));

  spinel_raster_builder_t rb;

  spinel(raster_builder_create(state.context, &rb));

  //
  // create widgets
  //
  widget_svg_t svg = widget_svg_create(svg_doc, false);  // don't linearize SVG colors

  struct widget * ws[] =
  {
#if !defined(__linux__)
    widget_mouse_create().widget,       // topmost layer
#endif                                  //
    widget_coords_create(8.0f).widget,  //
    widget_fps_create(16.0f).widget,    //
    svg.widget,                         // bottommost layer
  };

  //
  // initialize layout of widgets
  //
  struct widget_layout w_layout        = { 0 };
  uint32_t             group_depth_max = 0;

  widget_layout(ws, ARRAY_LENGTH_MACRO(ws), &w_layout, &group_depth_max);

  spinel_group_id parents[group_depth_max + 1];  // 1 or 2 for now

  //
  // Create composition
  //

  spinel_composition_t composition;

  spinel(composition_create(state.context, &composition));

  //
  // Create styling
  //
  // Sizing: 16 cmds per layer is conservative plus the number of groups and
  // their trail back to the parent
  //
  uint32_t const layer_count = w_layout.group.layer.base + w_layout.group.layer.count;

  spinel_styling_create_info_t const styling_create_info = {

    .layer_count = layer_count,
    .cmd_count   = layer_count * 8 + ARRAY_LENGTH_MACRO(ws) * 32,
  };

  spinel_styling_t styling;

  spinel(styling_create(state.context, &styling_create_info, &styling));

  //
  //
  //
  struct widget_context w_context = {
    .context          = state.context,
    .pb               = pb,
    .rb               = rb,
    .ts               = ts,
    .styling.curr     = styling,
    .composition.curr = composition,
    .parents          = parents,
  };

  //
  // initialize the first loop
  //
  struct widget_control w_control = state.initial;

  //
  // set up rendering extensions
  //
  spinel_vk_swapchain_submit_ext_graphics_signal_t ext_graphics_signal = {
    .ext    = NULL,
    .type   = SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_GRAPHICS_SIGNAL,
    .signal = {
      .count = 2,
      //
      // .semaphores[]
      // .values[]
      //
    },
  };

  spinel_vk_swapchain_submit_ext_graphics_store_t ext_graphics_store = {
    .ext                    = &ext_graphics_signal,
    .type                   = SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_GRAPHICS_STORE,
    .queue_family_index     = qfis[1],
    .image_info.imageLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    //
    // .extent_index
    // .cb
    // .queue
    // .old_layout
    // .image
    // .image_info
    //
  };

  spinel_vk_swapchain_submit_ext_graphics_wait_t ext_graphics_wait = {
    .ext  = &ext_graphics_store,
    .type = SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_GRAPHICS_WAIT,
    .wait = {
      .count  = 1,
      .stages = { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT },
      //
      // .semaphores[]
      // .values[]
      //
    },
  };

  spinel_vk_swapchain_submit_ext_compute_acquire_t ext_compute_acquire = {
    .ext                     = &ext_graphics_wait,
    .type                    = SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_COMPUTE_ACQUIRE,
    .from_queue_family_index = qfis[1],
  };

  spinel_vk_swapchain_submit_ext_compute_fill_t ext_compute_fill = {
    .ext   = NULL,  // &ext_graphics_wait or &ext_compute_acquire
    .type  = SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_COMPUTE_FILL,
    .dword = 0xFFFFFFFF,
  };

  spinel_vk_swapchain_submit_ext_compute_render_t ext_compute_render = {
    .ext  = NULL,  // &ext_compute_fill or &ext_compute_acquire,
    .type = SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_COMPUTE_RENDER,
    //
    // .clip = { 0, 0, ... },
    // .extent_index
    //
  };

  spinel_vk_swapchain_submit_ext_compute_release_t ext_compute_release = {
    .ext                   = &ext_compute_render,
    .type                  = SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_COMPUTE_RELEASE,
    .to_queue_family_index = qfis[1],
  };

  spinel_swapchain_submit_t swapchain_submit = {
    .ext         = &ext_compute_release,
    .styling     = styling,
    .composition = composition,
  };

  //
  // refresh the platform surface and spinel swapchain
  //
  spinel_surface_regen(surface, &state);

  //
  // create presentation queue pool and command buffers
  //
  spinel_vk_q_cmd_create(&vk, state.image_count);

  //
  // which "acquire_presentable" function?
  //
  spinel_acquire_presentable_pfn_t acquire_presentable_pfn =  //
    is_fence_acquired                                         //
      ? spinel_acquire_fenced_presentable
      : spinel_acquire_unfenced_presentable;

  //
  // RENDER/INPUT LOOP
  //
  // render and process input
  //
  spinel_secs_set(&state);

  for (uint32_t ii = 0; (ii < frame_count) && spinel_secs_lte(&state, seconds); ii++)
    {
      //
      // Explicit flushing is only for accurately benchmarking a path declaration.
      //
      if (w_control.paths)
        {
          spinel(path_builder_flush(pb));
        }

      //
      // Explicit flushing is only for accurately benchmarking rasterization.
      //
      if (w_control.rasters)
        {
          spinel(raster_builder_flush(rb));
        }

      //
      // RESET WIDGET COMPOSITION?
      //
      if (w_control.composition)
        {
          // unseal and reset composition
          spinel(composition_unseal(composition));
          spinel(composition_reset(composition));

          // update clip
          spinel_pixel_clip_t const clip = {
            .x0 = 0,
            .y0 = 0,
            .x1 = state.extent.width,
            .y1 = state.extent.height,
          };

          spinel(composition_set_clip(composition, &clip));
        }

      //
      // RESET WIDGET STYLING?
      //
      if (w_control.styling)
        {
          // unseal and reset styling
          spinel(styling_unseal(styling));
          spinel(styling_reset(styling));

          //
          // until there is a container widget to implicitly initialize the
          // root, explicitly initialize the styling root group
          //
          widget_regen_styling_root(&w_control, &w_context, &w_layout);
        }

      //
      // REGENERATE WIDGETS
      //
      widget_regen(ws, ARRAY_LENGTH_MACRO(ws), &w_control, &w_context);

      //
      // SEAL COMPOSITION & STYLING
      //
      // The composition and styling are implicitly sealed by render() but let's
      // explicitly seal them here in case we're skipping rendering in the
      // benchmark.
      //
      // NOTE(allanmac): the composition/styling/render API is in flux.
      //
      spinel_composition_seal(composition);
      spinel_styling_seal(styling);

      //
      // RENDER?
      //
      if (w_control.render)
        {
          //
          // ACQUIRE A PRESENTABLE
          //
          struct surface_presentable const * presentable;

          VkResult const acquire_result = acquire_presentable_pfn(vk.d,  //
                                                                  surface,
                                                                  &presentable,
                                                                  NULL);
          //
          // Possible results:
          //
          //   VK_SUCCESS                                   : render
          //   VK_TIMEOUT                                   : fatal
          //   VK_SUBOPTIMAL_KHR                            : render then regen
          //   VK_ERROR_OUT_OF_DATE_KHR                     : regen
          //   VK_ERROR_DEVICE_LOST                         : fatal for now
          //   VK_ERROR_OUT_OF_HOST_MEMORY                  : fatal
          //   VK_ERROR_OUT_OF_DEVICE_MEMORY                : fatal
          //   VK_ERROR_SURFACE_LOST_KHR                    : fatal for now
          //   VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT : fatal for now
          //
          bool is_fatal  = false;
          bool is_render = false;
          bool is_regen  = false;

          switch (acquire_result)
            {
              case VK_SUCCESS:
                is_render = true;
                break;

              case VK_TIMEOUT:
                is_fatal = true;
                break;

              case VK_SUBOPTIMAL_KHR:
                is_render = true;
                is_regen  = true;
                break;

              case VK_ERROR_OUT_OF_DATE_KHR:
              case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:
                is_regen = true;
                break;

              case VK_ERROR_DEVICE_LOST:
              case VK_ERROR_OUT_OF_HOST_MEMORY:
              case VK_ERROR_OUT_OF_DEVICE_MEMORY:
              case VK_ERROR_SURFACE_LOST_KHR:
              default:
                is_fatal = true;
                break;
            }

          //
          // UNHANDLED ERROR
          //
          if (is_fatal)
            {
              vk_ok(acquire_result);

              break;
            }

          //
          // RENDER
          //
          if (is_render)
            {
              //
              // Is this a new presentable with an implicit undefined layout?
              //
              bool const is_layout_undefined = (presentable->acquire_count == 1);

              if (is_layout_undefined)
                {
                  // compute_render -> compute_fill -> graphics_wait -> ...
                  ext_compute_render.ext = &ext_compute_fill;
                  ext_compute_fill.ext   = &ext_graphics_wait;
                }
              else if (is_clear_before_render)
                {
                  // compute_render -> compute_fill -> compute_acquire -> graphics_wait -> ...
                  ext_compute_render.ext = &ext_compute_fill;
                  ext_compute_fill.ext   = &ext_compute_acquire;
                }
              else
                {
                  // compute_render -> compute_acquire -> graphics_wait -> ...
                  ext_compute_render.ext = &ext_compute_acquire;
                }

              //
              // Update compute render extension for this presentable
              //
              ext_compute_render.clip.x1      = state.extent.width;
              ext_compute_render.clip.y1      = state.extent.height;
              ext_compute_render.extent_index = presentable->image_index;

              //
              // Wait on presentable's "wait" semaphore
              //
              ext_graphics_wait.wait.semaphores[0] = presentable->wait.semaphore;

              //
              // Update graphics store extension for this presentable
              //
              VkImageLayout const layout_prev = is_layout_undefined  //
                                                  ? VK_IMAGE_LAYOUT_UNDEFINED
                                                  : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

              ext_graphics_store.extent_index         = presentable->image_index;
              ext_graphics_store.queue                = spinel_vk_q_next(&vk);
              ext_graphics_store.layout_prev          = layout_prev;
              ext_graphics_store.image                = presentable->image;
              ext_graphics_store.image_info.imageView = presentable->image_view;

              //
              // Signal presentable's "signal" semaphore
              //
              ext_graphics_signal.signal.semaphores[0] = presentable->signal;

              //
              // Get a command buffer and its associated availability semaphore
              //
              spinel_vk_cb_next(&vk,
                                &ext_graphics_store.cb,
                                ext_graphics_signal.signal.semaphores + 1,
                                ext_graphics_signal.signal.values + 1);

              //
              // Submit compute work
              //
              spinel(swapchain_submit(state.swapchain, &swapchain_submit));

              //
              // Present graphics work
              //
              VkPresentInfoKHR const pi = {
                .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                .pNext              = NULL,
                .waitSemaphoreCount = 1,
                .pWaitSemaphores    = &presentable->signal,  // wait on "signal" semaphore
                .swapchainCount     = 1,
                .pSwapchains        = &presentable->swapchain,
                .pImageIndices      = &presentable->image_index,
                .pResults           = NULL,
              };

              (void)vkQueuePresentKHR(ext_graphics_store.queue, &pi);
            }

          //
          // REGEN SWAPCHAIN
          //
          if (is_regen)
            {
              spinel_surface_regen(surface, &state);

              //
              // Why regenerate the command buffers?  It seems unlikely that
              // swapchain image count will ever change -- even when resized --
              // but the spec says nothing about this.  The only way to
              // determine the actual image count is through
              // vkGetSwapchainImagesKHR() after creation of a new swapchain.
              //
              // Note that there is a vkDeviceWaitIdle() hiding in
              // spinel_surface_regen() so we know the queue command buffers
              // aren't executing.
              //
              spinel_vk_cmd_regen(&vk, state.image_count);
            }
        }

      //
      // WIDGET INPUT
      //
      w_control = state.control;  // reset control flags

      widget_surface_input(ws,
                           ARRAY_LENGTH_MACRO(ws),
                           &w_control,
                           surface,
                           spinel_state_input,
                           &state);

      if (state.is_center)
        {
          widget_svg_center(svg,
                            &w_control,
                            &state.extent,
                            state.center.cx,
                            state.center.cy,
                            state.center.scale);
        }

      if (state.is_rotate)
        {
          widget_svg_rotate(svg, &w_control, (float)((ii % 360) * (M_PI * 2.0 / 360.0)));
        }

      //
      // EXIT?
      //
      if (state.is_exit)
        {
          break;
        }
    }

  ////////////////////////////////////
  //
  // DISPOSAL
  //

  // done with swapchain
  spinel_swapchain_release(state.swapchain);

  // unseal Spinel composition and styling to ensure rendering is complete
  spinel(composition_unseal(composition));
  spinel(styling_unseal(styling));

  // widgets -- may release paths and rasters
  widget_destroy(ws, ARRAY_LENGTH_MACRO(ws), &w_context);

  // release the Spinel builders, composition and styling
  spinel(path_builder_release(pb));
  spinel(raster_builder_release(rb));
  spinel(composition_release(composition));
  spinel(styling_release(styling));

  // release the transform stack
  spinel_transform_stack_release(ts);

  // release the Spinel context
  spinel(context_release(state.context));

  // svg doc
  svg_dispose(svg_doc);

  // surface
  surface_destroy(surface);  // will implicitly `detach(surface)`

  // destroy vk handles
  spinel_vk_destroy(&vk);

  return EXIT_SUCCESS;
}

//
//
//

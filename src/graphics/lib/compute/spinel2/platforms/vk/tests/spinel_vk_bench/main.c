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
#include "common/vk/cache.h"
#include "common/vk/debug_utils.h"
#include "common/vk/find_mem_type_idx.h"
#include "spinel/platforms/vk/ext/find_target/find_target.h"
#include "spinel/platforms/vk/spinel_vk.h"
#include "spinel/spinel_assert.h"
#include "svg/svg.h"
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

#if defined(__linux__)

#include "surface/surface_xcb.h"

#define SPN_PLATFORM_EXTENSION_NAMES         VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME

#define SPN_PLATFORM_MIN_IMAGE_COUNT         2

#define SPN_PLATFORM_PRESENT_MODE            VK_PRESENT_MODE_IMMEDIATE_KHR
                                             // VK_PRESENT_MODE_MAILBOX_KHR
                                             // VK_PRESENT_MODE_FIFO_KHR
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

#if defined(__arm__)
#define SPN_PLATFORM_IMAGE_VIEW_FORMAT       VK_FORMAT_B8G8R8A8_SRGB
#else
#define SPN_PLATFORM_IMAGE_VIEW_FORMAT       VK_FORMAT_B8G8R8A8_UNORM
#endif

#define SPN_PLATFORM_SURFACE_FORMAT          (VkSurfaceFormatKHR){ .format     = SPN_PLATFORM_IMAGE_VIEW_FORMAT, \
                                                                   .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };

//
// FUCHSIA/INTEL is RGBA and UNORM for now but eventually BGRA write-only once Mesa updates land
//

#define SPN_PLATFORM_IMAGE_VIEW_FORMAT_INTEL VK_FORMAT_R8G8B8A8_UNORM

#define SPN_PLATFORM_SURFACE_FORMAT_INTEL    (VkSurfaceFormatKHR){ .format     = SPN_PLATFORM_IMAGE_VIEW_FORMAT_INTEL, \
                                                                   .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };

//////////////////////////////////////////////
//
// UNSUPPORTED
//

#else
#error "Unsupported WSI platform"
#endif
// clang-format on

//
//
//

#define SPN_ACQUIRE_DEFAULT_TIMEOUT ((uint64_t)15e9)  // 15 seconds

static VkResult
spinel_acquire_fenced_presentable(VkDevice                            vk_d,
                                  spinel_context_t                    context,
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
  // fence is signaled -- block to acquire a presentable
  //
  result = surface_acquire(surface, SPN_ACQUIRE_DEFAULT_TIMEOUT, presentable, payload);

  return result;
}

//
//
//

static VkResult
spinel_acquire_unfenced_presentable(VkDevice                            vk_d,
                                    spinel_context_t                    context,
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

typedef VkResult  //
  (*spinel_acquire_presentable_pfn_t)(VkDevice                                  vk_d,
                                      spinel_context_t                          context,
                                      struct surface * const                    surface,
                                      struct surface_presentable const ** const presentable,
                                      void *                                    payload);

//
//
//

#if 0
static void
spinel_render_submitter(VkQueue queue, VkFence fence, VkCommandBuffer const cb, void * data)
{
  struct surface_presentable const * presentable            = data;
  bool const                         is_layout_undefined    = (presentable->acquire_count == 1);
  bool const                         is_clear_before_render = *(bool *)presentable->payload;

  // are we clearing the image?
  VkPipelineStageFlags const dst_stage_mask = (is_layout_undefined || is_clear_before_render)
                                                ? VK_PIPELINE_STAGE_TRANSFER_BIT
                                                : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

  struct VkSubmitInfo const si = {

    .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .waitSemaphoreCount   = 1,
    .pWaitSemaphores      = &presentable->wait.semaphore,
    .pWaitDstStageMask    = &dst_stage_mask,
    .commandBufferCount   = 1,
    .pCommandBuffers      = &cb,
    .signalSemaphoreCount = 1,
    .pSignalSemaphores    = &presentable->signal
  };

  // submit
  vk(QueueSubmit(queue, 1, &si, fence));

  // present
  VkPresentInfoKHR const pi = {

    .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
    .pNext              = NULL,
    .waitSemaphoreCount = 1,
    .pWaitSemaphores    = &presentable->signal,
    .swapchainCount     = 1,
    .pSwapchains        = &presentable->swapchain,
    .pImageIndices      = &presentable->image_index,
    .pResults           = NULL
  };

  (void)vkQueuePresentKHR(queue, &pi);
}
#endif

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
  fprintf(
    stderr,
    "\n"
    "Usage: %s -f <filename> [-h] [-d:] [-i:] [-n:] [-p:] [-s:] [-q] [-F] [-Q] [-D] [-X]\n"
    " -f <filename>             Filename of SVG file.\n"
    " -h                        Print usage.\n"
    " -d <vendorID>:<deviceID>  Execute on a specific Vulkan physical device.  Defaults to first device.\n"
    " -i <min image count>      Minimum number of images in swapchain. Defaults to %u.\n"
    " -n <iterations>           Maximum iterations before exiting. Defaults to UINT_MAX\n"
    " -p <present mode>         Select present mode [0-3]*. Defaults to %u/%s.\n"
    " -q <compute>:<present>    Select the compute and presentation queue family indices.  Defaults to `0:0`\n"
    " -s <pipeline stage>       Select which pipeline stages are enabled on the first loop.    Defaults to `11111`.\n"
    " -t <pipeline stage>       Select which pipeline stages are enabled after the first loop. Defaults to `11111`.\n"
    " -v                        Verbose SVG parsing output.  Quiet by default.\n"
    " -r                        Rotate the SVG file around the origin.  Disabled by default.\n"
    " -F                        Use VkFences to meter swapchain image acquires.\n"
    " -Q                        Disable Vulkan validation layers.  Enabled by default.\n"
    " -D                        Disable Vulkan debug info labels.  Enabled by default.\n"
    " -X                        Skip clearing the image entirely before every render.\n"
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
}

//
// Don't report "VUID-VkImageViewCreateInfo-usage-02275" on Intel
// devices because this is not a validation error:
//
//   | vkCreateImageView(): pCreateInfo->format VK_FORMAT_B8G8R8A8_UNORM
//   | with tiling VK_IMAGE_TILING_OPTIMAL does not support usage that
//   | includes VK_IMAGE_USAGE_STORAGE_BIT.
//
//   if (strstr(pMessage, "VUID-VkImageViewCreateInfo-usage-02275") != NULL)
//     {
//       return VK_FALSE;
//     }
//

//
//
//

struct spinel_state
{
  struct widget_control initial;
  struct widget_control control;
  spinel_swapchain_t    swapchain;
  VkExtent2D            extent;
  bool                  is_rotate;
  bool                  is_exit;
};

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
spinel_surface_regen(spinel_context_t      context,
                     struct surface *      surface,
                     struct spinel_state * state)
{
  uint32_t image_count;

  VkResult result = surface_regen(surface, &state->extent, &image_count);

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
    .count = image_count,
  };

  spinel(swapchain_create(context, &create_info, &state->swapchain));
}

//
//
//

int
main(int argc, char * const argv[])
{
#if defined(__Fuchsia__)
  // putenv("VK_LAYER_SETTINGS_PATH=/cache/vulkan/settings.d");
  // setenv("VK_LAYER_SETTINGS_PATH", "/cache/vulkan/settings.d", true);
#endif

#ifndef NDEBUG
#define LOG_ENV(str_) fprintf(stderr, str_ "=%s\n", getenv(str_))
  LOG_ENV("VK_LOADER_DEBUG");
  LOG_ENV("VK_LAYER_LUNARG_override");
  LOG_ENV("VK_LAYER_PATH");
  LOG_ENV("VK_LAYER_SETTINGS_PATH");
#endif

  //
  // set up defaults
  //
  uint32_t         vendor_id              = 0;
  uint32_t         device_id              = 0;
  uint32_t         min_image_count        = SPN_PLATFORM_MIN_IMAGE_COUNT;
  uint32_t         loop_count             = UINT32_MAX;
  uint32_t         qfis[2]                = { 0, 0 };
  VkPresentModeKHR present_mode           = SPN_PLATFORM_PRESENT_MODE;
  bool             is_verbose             = false;
  VkBool32         is_fence_acquired      = false;
  bool             is_validation          = true;
  bool             is_debug_info          = true;
  bool             is_clear_before_render = true;
  char const *     filename               = NULL;

  struct spinel_state state =  //
    {                          //
      .initial = WIDGET_CONTROL_PRSCR(),
      .control = WIDGET_CONTROL_PRSCR()
    };

  //
  // process options
  //
  int opt;

  while ((opt = getopt(argc, argv, "d:f:i:n:p:q:s:t:R:rvFQDXh")) != EOF)
    {
      switch (opt)
        {
          case 'd':
            // formatting
            {
              char * str_end;

              vendor_id = (uint32_t)strtoul(argv[1], &str_end, 16);  // returns 0 on error

              if (str_end != argv[1])
                {
                  if (*str_end == ':')
                    {
                      device_id = (uint32_t)strtoul(str_end + 1, NULL, 16);  // returns 0 on error
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

          case 'n':
            loop_count = (uint32_t)strtoul(optarg, NULL, 10);
            loop_count = MAX_MACRO(uint32_t, 1, loop_count);
            break;

          case 'p':
            present_mode = (uint32_t)strtoul(optarg, NULL, 10);
            present_mode = MIN_MACRO(uint32_t, present_mode, VK_PRESENT_MODE_FIFO_RELAXED_KHR);
            break;

          case 'q':
            qfis[0] = (uint32_t)strtoul(optarg, NULL, 10);                   // returns 0 on error
            qfis[1] = (uint32_t)strtoul(strchr(optarg, ':') + 1, NULL, 10);  // returns 0 on error
            break;

          case 's':
            state.initial.flags = (uint32_t)strtoul(optarg, NULL, 2);
            break;

          case 't':
            state.control.flags = (uint32_t)strtoul(optarg, NULL, 2);
            break;

          case 'r':
            state.is_rotate ^= true;
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
    VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
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

  //
  // Vulkan handles that we'll need until shutdown
  //
  struct
  {
    VkInstance                    i;
    VkPhysicalDevice              pd;
    VkDevice                      d;
    VkPipelineCache               pc;
    VkAllocationCallbacks const * ac;
  } vk = { 0 };

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

  VkPhysicalDevice * pds = malloc(pd_count * sizeof(*pds));

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
  // find max queue count
  //
  uint32_t const queue_compute_count = qfp[qfis[0]].queueCount;
  uint32_t const queue_present_count = qfp[qfis[1]].queueCount;

  uint32_t const qps_size = MAX_MACRO(uint32_t, queue_compute_count, queue_present_count);

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
      .queueFamilyIndex = qfis[0],
      .queueCount       = queue_compute_count,
      .pQueuePriorities = qps },

    { .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .pNext            = NULL,
      .flags            = 0,
      .queueFamilyIndex = qfis[1],
      .queueCount       = queue_present_count,
      .pQueuePriorities = qps },
  };

  //
  // Are the queue families the same?  If so, then only list one.
  //
  bool const is_same_queue = (dqcis[0].queueFamilyIndex == dqcis[1].queueFamilyIndex);

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
  vk_ok(vk_pipeline_cache_create(vk.d,
                                 NULL,
                                 SPN_PLATFORM_PIPELINE_CACHE_PREFIX_STRING "spinel_vk_bench_cache",
                                 &vk.pc));

  //
  // save compute queue index and count
  //
  struct spinel_vk_context_create_info const cci = {
    .vk = {
      .pd = vk.pd,
      .d  = vk.d,
      .pc = vk.pc,
      .ac = vk.ac,
      .q  = {
        .compute = {
          .flags        = dqcis[0].flags,
          .family_index = dqcis[0].queueFamilyIndex,
          .count        = dqcis[0].queueCount,
        },
        .shared  = {
          .queue_family_count   = is_same_queue ? 1 : 2,
          .queue_family_indices = {
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

  spinel_context_t context = spinel_vk_context_create(&cci);

  if (context == NULL)
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
                                  vk.pc));

  //
  // Get context limits
  //
  spinel_context_limits_t limits;

  spinel(context_get_limits(context, &limits));

  //
  // create surface presentables
  //
  VkSurfaceFormatKHR surface_format;
  VkFormat           image_view_format;

#if defined(__Fuchsia__)
  //
  // NOTE(allanmac): Intel is special-cased while we wait for a Mesa patch
  //
  if (pdp.vendorID == 0x8086)
    {
      surface_format    = SPN_PLATFORM_SURFACE_FORMAT_INTEL;
      image_view_format = SPN_PLATFORM_IMAGE_VIEW_FORMAT_INTEL;
    }
  else
#endif
    {
      surface_format    = SPN_PLATFORM_SURFACE_FORMAT;
      image_view_format = SPN_PLATFORM_IMAGE_VIEW_FORMAT;
    }

  VkImageUsageFlags const image_usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |  //
                                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

  VkExtent2D const max_image_extent = { limits.extent.width,  //
                                        limits.extent.height };

  VkComponentMapping const image_view_components = {

    .r = VK_COMPONENT_SWIZZLE_IDENTITY,
    .g = VK_COMPONENT_SWIZZLE_IDENTITY,
    .b = VK_COMPONENT_SWIZZLE_IDENTITY,
    .a = VK_COMPONENT_SWIZZLE_IDENTITY
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

  spinel(path_builder_create(context, &pb));

  spinel_raster_builder_t rb;

  spinel(raster_builder_create(context, &rb));

  //
  // create widgets
  //
  widget_svg_t svg = widget_svg_create(svg_doc, true);

  struct widget * ws[] = {
    widget_fps_create(16.0f).widget,
    widget_mouse_create().widget,
    svg.widget,
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

  spinel(composition_create(context, &composition));

  //
  // Create styling
  //
  // Sizing: 16 cmds per layer is conservative plus the number of groups and
  // their trail back to the parent
  //
  uint32_t const layer_count = w_layout.group.layer.base + w_layout.group.layer.count;

  spinel_styling_create_info_t const styling_create_info = {

    .layer_count = layer_count,
    .cmd_count   = layer_count * 16 + ARRAY_LENGTH_MACRO(ws) * 7,
  };

  spinel_styling_t styling;

  spinel(styling_create(context, &styling_create_info, &styling));

  //
  //
  //
  struct widget_context w_context = {
    .context          = context,
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
  spinel_vk_swapchain_submit_ext_graphics_signal_t graphics_signal = {
    .ext    = NULL,
    .type   = SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_GRAPHICS_SIGNAL,
    .signal = { 0 },
  };

  spinel_vk_swapchain_submit_ext_graphics_store_t graphics_store = {
    .ext        = &graphics_signal,
    .type       = SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_GRAPHICS_STORE,
    .image      = VK_NULL_HANDLE,
    .image_info = { 0 },
    .old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
  };

  spinel_vk_swapchain_submit_ext_graphics_wait_t graphics_wait = {
    .ext  = &graphics_store,
    .type = SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_GRAPHICS_WAIT,
    .wait = { 0 },
  };

  spinel_vk_swapchain_submit_ext_compute_fill_t compute_fill = {
    .ext   = &graphics_wait,
    .type  = SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_COMPUTE_FILL,
    .dword = 0xFFFFFFFF,
  };

  spinel_vk_swapchain_submit_ext_compute_render_t compute_render = {
    .ext          = NULL,  // &compute_fill or &graphics_wait,
    .type         = SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_COMPUTE_RENDER,
    .clip         = { 0, 0, UINT32_MAX, UINT32_MAX },
    .extent_index = 0,
  };

  spinel_swapchain_submit_t swapchain_submit = {
    .ext         = &compute_render,
    .styling     = styling,
    .composition = composition,
  };

  //
  // refresh the platform surface and spinel swapchain
  //
  spinel_surface_regen(context, surface, &state);

  //
  // which "acquire_presentable" function?
  //
  spinel_acquire_presentable_pfn_t acquire_presentable_pfn =  //
    is_fence_acquired                                         //
      ? spinel_acquire_fenced_presentable
      : spinel_acquire_unfenced_presentable;

  //
  // render and process input
  //
  for (uint32_t ii = 0; ii < loop_count; ii++)
    {
      //
      // anything to do?
      //
      if (w_control.flags != 0)
        {
          //
          // A composition, styling or swapchain will implicitly meter
          // the frequency of this loop and unbounded path and raster
          // allocation.
          //
          // If none of them are activated and either paths or rasters
          // are being created then this loop will likely generate paths
          // or rasters faster than they can be reclaimed.
          //
          // In this case, the Vulkan queue is explicitly drained.
          //
          static struct widget_control const paths_or_rasters = { .paths = 1, .rasters = 1 };

          if (w_control.flags <= paths_or_rasters.flags)
            {
              spinel(path_builder_flush(pb));
              spinel(raster_builder_flush(rb));

              //
              // FIXME(allanmac): This isn't required anymore.
              //
              // spinel(vk_context_drain(context, UINT64_MAX));
              //
            }
          else
            {
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
            }
        }

      //
      // REGENERATE WIDGETS
      //
      widget_regen(ws, ARRAY_LENGTH_MACRO(ws), &w_control, &w_context);

      //
      // SEAL COMPOSITION & STYLING
      //
      // The composition and styling are implicitly sealed by render()
      // but let's explicitly seal them here.
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
                                                                  context,
                                                                  surface,
                                                                  &presentable,
                                                                  &is_clear_before_render);
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
              // update render clip
              //
              compute_render.clip.x1 = state.extent.width;
              compute_render.clip.y1 = state.extent.height;

              //
              // Is this the first time this image has even been acquired?
              //
              bool const is_layout_undefined = (presentable->acquire_count == 1);

              graphics_store.old_layout = is_layout_undefined  //
                                            ? VK_IMAGE_LAYOUT_UNDEFINED
                                            : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

              //
              // Clear before rendering?
              //
              bool const is_fill = (is_clear_before_render || is_layout_undefined);

              if (is_fill)
                {
                  compute_render.ext = &compute_fill;
                }
              else
                {
                  compute_render.ext = &graphics_wait;
                }

              //
              // Update image
              //
              graphics_store.image                  = presentable->image;
              graphics_store.image_info.imageView   = presentable->image_view;
              graphics_store.image_info.imageLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

              //
              // Submit
              //
              spinel(swapchain_submit(state.swapchain, &swapchain_submit));
            }

          //
          // REGEN SWAPCHAIN
          //
          if (is_regen)
            {
              spinel_surface_regen(context, surface, &state);
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

      if (state.is_rotate)
        {
          widget_svg_rotate(svg, &state.control, (float)((ii % 360) * M_PI * 2.0 / 360.0));
        }

      //
      // EXIT?
      //
      if (state.is_exit || ((ii + 1) == loop_count))
        {
#if 0
          // drain context to get accurate block pool stats
          // spinel(vk_context_wait(context, 0, NULL, true, UINT64_MAX, NULL));

          //
          // dump block pool stats
          //
          spinel_vk_status_ext_block_pool_t status_block_pool = {
            .type = SPN_VK_STATUS_EXT_TYPE_BLOCK_POOL
          };

          spinel_status_t const status = { .ext = &status_block_pool };

          spinel(context_status(context, &status));

          fprintf(stderr,
                  "avail / alloc: %9.3f / %9.3f MB\n",
                  (double)status_block_pool.avail / (1024.0 * 1024.0),
                  (double)status_block_pool.inuse / (1024.0 * 1024.0));
#endif

          break;
        }
    }

  // unseal Spinel composition and styling to ensure rendering is complete
  spinel(composition_unseal(composition));
  spinel(styling_unseal(styling));

  // widgets
  widget_destroy(ws, ARRAY_LENGTH_MACRO(ws), &w_context);

  // release the Spinel builders, composition and styling
  spinel(path_builder_release(pb));
  spinel(raster_builder_release(rb));
  spinel(composition_release(composition));
  spinel(styling_release(styling));

  // release the transform stack
  spinel_transform_stack_release(ts);

  // release the Spinel context
  spinel(context_release(context));

  ////////////////////////////////////
  //
  // DISPOSAL
  //

  // svg doc
  svg_dispose(svg_doc);

  // surface
  surface_destroy(surface);  // will implicitly `detach(surface)`

  // VkDevice
  vkDestroyDevice(vk.d, NULL);

  // VkInstance
  vkDestroyInstance(vk.i, NULL);

  return EXIT_SUCCESS;
}

//
//
//

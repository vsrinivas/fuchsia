// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#if defined( _MSC_VER )
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stddef.h>

//
//
//

#include "common/macros.h"
#include "common/vk/vk_assert.h"
#include "common/vk/vk_cache.h"
#include "common/vk/vk_debug.h"

//
//
//

#include "spinel.h"
#include "spinel_assert.h"

//
//
//

#include "device.h"
#include "target.h"

//
// Compile-time target
//

#include "spinel/platforms/vk/targets/amd/gcn3/name.h"
#include "spinel/platforms/vk/targets/arm/bifrost4/name.h"
#include "spinel/platforms/vk/targets/arm/bifrost8/name.h"
#include "spinel/platforms/vk/targets/arm/bifrost8_4x4/name.h"
#include "spinel/platforms/vk/targets/intel/gen8/name.h"
#include "spinel/platforms/vk/targets/nvidia/sm_50/name.h"

//
//
//

static
struct spn_target_image const *
find_target_image(VkPhysicalDeviceProperties         const * const pdp,
                  VkPhysicalDeviceSubgroupProperties const * const pdsp,
                  uint32_t                                   const vendor_id,
                  uint32_t                                   const device_id)
{
  if ((pdp->vendorID != vendor_id) || (pdp->deviceID != device_id))
    return NULL;

  switch (pdp->vendorID)
    {
    case 0x10DE: // NVIDIA
    {
      //
      // FIXME -- for now, the kernels in this app are targeting
      // sm_35+ devices.  You could add some rigorous rejection by
      // device id here...
      //
      return &spn_target_image_nvidia_sm_50;
    }
    case 0x8086: // INTEL
    {
      //
      // FIXME -- for now, the kernels in this app are targeting GEN8+
      // devices -- this does *not* include variants of GEN9LP+
      // "Apollo Lake" because that device has a different
      // architectural "shape" than GEN8 GTx.  You could add some
      // rigorous rejection by device id here...
      //
      return &spn_target_image_intel_gen8;
    }
    case 0x1002: // AMD
    {
      //
      // AMD GCN
      //
      return &spn_target_image_amd_gcn3;
    }
    case 0x13B5:
    {
      //
      // ARM BIFROST
      //
      if (pdsp->subgroupSize == 4)
        {
          //
          // BIFROST GEN1 - subgroupSize = 4
          //
          fprintf(stderr,"Detected Bifrost4...\n");
          return &spn_target_image_arm_bifrost4;
        }
      else if (pdsp->subgroupSize == 8)
        {
          //
          // BIFROST GEN2 - subgroupSize = 8
          //
          fprintf(stderr,"Detected Bifrost8...\n");
          return &spn_target_image_arm_bifrost8;
        }
      else
        {
          return NULL;
        }
    }
    default:
      return NULL;
    }
}

//
//
//

static
void
test_short_paths(spn_context_t context)
{
  spn_path_builder_t pb;

  spn(path_builder_create(context,&pb));

  //
  // generate lots of paths
  //
  for (uint32_t ii=0; ii<(1<<20); ii++)
    {
      spn(path_begin(pb));

      spn(path_move_to(pb,0.0f,0.0f));
      spn(path_line_to(pb,8.0f,8.0f));
      spn(path_line_to(pb,8.0f,0.0f));
      spn(path_line_to(pb,0.0f,0.0f));

      spn_path_t path;

      spn(path_end(pb,&path));

      spn_path_release(context,&path,1);

#if 1
      // every N paths
      if ((ii & BITS_TO_MASK_MACRO(18)) == BITS_TO_MASK_MACRO(18))
        fprintf(stderr,"%u\n",ii+1);
#endif
    }

  spn(path_builder_release(pb));
}



static
void
test_path_builder_lost(spn_context_t context)
{
  spn_path_builder_t pb;

  spn(path_builder_create(context,&pb));

  //
  // generate one extremely long path to force an error and
  // permanently "lose" the path builder
  //
  spn(path_begin(pb));

  spn_result result;

  for (uint32_t ii=0; ii<(1<<19); ii++)
    {
      result = spn_path_move_to(pb,0.0f,0.0f);

      if (spn_expect(result,SPN_SUCCESS,SPN_ERROR_PATH_BUILDER_LOST))
        break;

      result = spn_path_line_to(pb,8.0f,8.0f);

      if (spn_expect(result,SPN_SUCCESS,SPN_ERROR_PATH_BUILDER_LOST))
        break;

      result = spn_path_line_to(pb,8.0f,0.0f);

      if (spn_expect(result,SPN_SUCCESS,SPN_ERROR_PATH_BUILDER_LOST))
        break;

      result = spn_path_line_to(pb,0.0f,0.0f);

      if (spn_expect(result,SPN_SUCCESS,SPN_ERROR_PATH_BUILDER_LOST))
        break;

#if 0
      // every N paths
      if ((ii & BITS_TO_MASK_MACRO(11)) == BITS_TO_MASK_MACRO(11))
        fprintf(stderr,"%u\n",ii+1);
#endif
    }

  spn_path_t path;

  result = spn_path_end(pb,&path);

  spn_expect(result,SPN_ERROR_PATH_BUILDER_LOST);

  result = spn_path_release(context,&path,1);

  spn_expect(result,SPN_ERROR_HANDLE_INVALID);

  spn(path_builder_release(pb));
}

static
void
test_raster_builder_create(spn_context_t context)
{
  spn_raster_builder_t rb;

  spn(raster_builder_create(context,&rb));

  spn(raster_builder_release(rb));
}

//
//
//

static
void
test_short_fills(spn_context_t context)
{
  spn_path_builder_t   pb;

  spn(path_builder_create(context,&pb));

  spn_raster_builder_t rb;

  spn(raster_builder_create(context,&rb));

  //
  // generate lots of paths
  //
  for (uint32_t ii=0; ii<(1<<26); ii++)
    {
      spn(path_begin(pb));

      spn(path_move_to(pb,0.0f,0.0f));
      spn(path_line_to(pb,8.0f,8.0f));
      spn(path_line_to(pb,8.0f,0.0f));
      spn(path_line_to(pb,0.0f,0.0f));

      spn_path_t path;

      spn(path_end(pb,&path));

      //
      // FIXME -- flush()
      //

      spn_raster_begin(rb);

      spn_transform_weakref_t tw = SPN_TRANSFORM_WEAKREF_INVALID;
      spn_clip_weakref_t      cw = SPN_CLIP_WEAKREF_INVALID;

      spn_raster_fill(rb,
                      &path,
                      &tw,
                      &spn_transform_identity,
                      &cw,
                      &spn_clip_default,
                      1);

      spn_raster_t raster;

      spn_raster_end(rb,&raster);

      //
      //
      //

      spn_path_release(context,&path,1);

      spn_raster_release(context,&raster,1);

#if 0
      // every N paths
      if ((ii & BITS_TO_MASK_MACRO(19)) == BITS_TO_MASK_MACRO(19))
        fprintf(stderr,"%u\n",ii+1);
#endif
    }

  //
  // dispose
  //
  spn(raster_builder_release(rb));

  spn(path_builder_release(pb));
}

//
//
//

int
main(int argc, char const * argv[])
{
  //
  // select the target by vendor and device id
  //
  uint32_t const vendor_id = (argc <= 1) ? UINT32_MAX : strtoul(argv[1],NULL,16);
  uint32_t const device_id = (argc <= 2) ? UINT32_MAX : strtoul(argv[2],NULL,16);

  //
  // create a Vulkan instances
  //
  VkApplicationInfo const app_info = {
      .sType                 = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pNext                 = NULL,
      .pApplicationName      = "Spinel Benchmark",
      .applicationVersion    = 0,
      .pEngineName           = "Spinel",
      .engineVersion         = 0,
      .apiVersion            = VK_API_VERSION_1_1
  };

  char const * const instance_enabled_layers[] = {
    "VK_LAYER_LUNARG_standard_validation",
    NULL
  };

  char const * const instance_enabled_extensions[] = {
    VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
    NULL
  };

  VkInstanceCreateInfo const instance_info = {
    .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pNext                   = NULL,
    .flags                   = 0,
    .pApplicationInfo        = &app_info,
    .enabledLayerCount       = ARRAY_LENGTH_MACRO(instance_enabled_layers) - 1,
    .ppEnabledLayerNames     = instance_enabled_layers,
    .enabledExtensionCount   = ARRAY_LENGTH_MACRO(instance_enabled_extensions) - 1,
    .ppEnabledExtensionNames = instance_enabled_extensions
  };

  VkInstance instance;

  vk(CreateInstance(&instance_info,NULL,&instance));

  //
  //
  //
#ifndef NDEBUG
  PFN_vkCreateDebugReportCallbackEXT vkCreateDebugReportCallbackEXT =
    (PFN_vkCreateDebugReportCallbackEXT)
    vkGetInstanceProcAddr(instance,"vkCreateDebugReportCallbackEXT");

  PFN_vkDestroyDebugReportCallbackEXT vkDestroyDebugReportCallbackEXT =
    (PFN_vkDestroyDebugReportCallbackEXT)
    vkGetInstanceProcAddr(instance,"vkDestroyDebugReportCallbackEXT");

  struct VkDebugReportCallbackCreateInfoEXT const drcci = {
    .sType       = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT,
    .pNext       = NULL,
    .flags       = (VK_DEBUG_REPORT_INFORMATION_BIT_EXT          |
                    VK_DEBUG_REPORT_WARNING_BIT_EXT              |
                    VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT  |
                    VK_DEBUG_REPORT_ERROR_BIT_EXT                |
                    VK_DEBUG_REPORT_DEBUG_BIT_EXT),
    .pfnCallback = vk_debug_report_cb,
    .pUserData   = NULL
  };

  VkDebugReportCallbackEXT drc;

  vk(CreateDebugReportCallbackEXT(instance,
                                  &drcci,
                                  NULL,
                                  &drc));
#endif

  //
  // Vulkan objects for Spinel
  //
  struct spn_device_vk device_vk =
    {
      .ac  = NULL,
      .qfi = 0
    };

  //
  // acquire all physical devices and select a match
  //
  uint32_t pdc;

  vk(EnumeratePhysicalDevices(instance,
                              &pdc,
                              NULL));

  VkPhysicalDevice * pds = malloc(pdc * sizeof(*pds));

  vk(EnumeratePhysicalDevices(instance,
                              &pdc,
                              pds));

  device_vk.pd = VK_NULL_HANDLE;

  struct spn_target_image const * target_image = NULL;

  for (uint32_t ii=0; ii<pdc; ii++)
    {
      VkPhysicalDeviceSubgroupProperties pdsp =
        {
          .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES,
          .pNext = NULL
        };

      VkPhysicalDeviceProperties2 pdp2 =
        {
          .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
          .pNext = &pdsp
        };

      vkGetPhysicalDeviceProperties2(pds[ii],&pdp2);

      VkPhysicalDeviceProperties const * const pdp = &pdp2.properties;

      bool const is_match = (pdp->vendorID == vendor_id) && (pdp->deviceID == device_id);

      if (is_match)
        {
          device_vk.pd = pds[ii];

          target_image = find_target_image(pdp,&pdsp,vendor_id,device_id);
        }

      fprintf(stderr,"%c %X : %X : %s\n",
              is_match ? '*' : ' ',
              pdp->vendorID,
              pdp->deviceID,
              pdp->deviceName);

      vk_debug_compute_props(stderr,pdp);
      vk_debug_subgroup_props(stderr,&pdsp);
    }

  if (device_vk.pd == VK_NULL_HANDLE)
    {
      fprintf(stderr,"Device %4X:%4X not found.\n",
              vendor_id & 0xFFFF,
              device_id & 0xFFFF);

      return EXIT_FAILURE;
    }

  free(pds);

  //
  // get the physical device's memory props
  //
  vkGetPhysicalDeviceMemoryProperties(device_vk.pd,&device_vk.pdmp);

  //
  // get queue properties
  //
  VkQueueFamilyProperties qfp[2];
  uint32_t                qfc = ARRAY_LENGTH_MACRO(qfp);

  vkGetPhysicalDeviceQueueFamilyProperties(device_vk.pd,&qfc,qfp);

  //
  // get image properties
  //
  // vkGetPhysicalDeviceImageFormatProperties()
  //
  // vk(GetPhysicalDeviceImageFormatProperties(phy_device,
  //

  //
  // create device
  //
  float const qp[] = { 1.0f };

  VkDeviceQueueCreateInfo const qi = {
    .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
    .pNext            = NULL,
    .flags            = 0,
    .queueFamilyIndex = device_vk.qfi,
    .queueCount       = 1,
    .pQueuePriorities = qp
  };

  //
  // FIXME --temporariy enable AMD GCN shader info extension
  //
  char const * const device_enabled_extensions[] = {
    // VK_KHR_DESCRIPTOR_UPDATE_TEMPLATE_EXTENSION_NAME,
#if defined( SPN_VK_SHADER_INFO_AMD_STATISTICS ) || defined( SPN_VK_SHADER_INFO_AMD_DISASSEMBLY )
    VK_AMD_SHADER_INFO_EXTENSION_NAME,
#endif
    NULL
  };

  uint32_t const device_enabled_extension_count =
    ARRAY_LENGTH_MACRO(device_enabled_extensions)
    - 1
    - 1 /*(device_vk.pdp.vendorID == 0x1002 ? 0 : 1)*/;

  //
  //
  //
  VkPhysicalDeviceFeatures device_features = { false };

  //
  // FIXME -- HotSort *will* need 'shaderInt64' on most platforms
  //
  if (true /*key_val_words == 2*/)
    {
      //
      // FIXME
      //
      // SEGMENT_TTCK and SEGMENT_TTRK shaders benefit from
      // shaderInt64 but shaderFloat64 shouldn't be required.
      //
      device_features.shaderInt64   = true;
      device_features.shaderFloat64 = true;
    }

  VkDeviceCreateInfo const device_info = {
    .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .pNext                   = NULL,
    .flags                   = 0,
    .queueCreateInfoCount    = 1,
    .pQueueCreateInfos       = &qi,
    .enabledLayerCount       = 0,
    .ppEnabledLayerNames     = NULL,
    .enabledExtensionCount   = device_enabled_extension_count,
    .ppEnabledExtensionNames = device_enabled_extensions,
    .pEnabledFeatures        = &device_features
  };

  vk(CreateDevice(device_vk.pd,&device_info,NULL,&device_vk.d));

  //
  // create the pipeline cache
  //
  vk(_pipeline_cache_create(device_vk.d,NULL,".vk_cache",&device_vk.pc));

  //
  // create a Spinel context
  //
  spn_context_t context;

  spn(context_create_vk(&context,
                        &device_vk,
                        target_image,
                        1L<<27,   // 128 MByte pool
                        1 <<18)); // 256K handles


  ////////////////////////////////////
  //
  // exercise the Spinel context
  //

  // test_short_paths(context);
  // test_path_builder_lost(context);
  // test_raster_builder_create(context);
  test_short_fills(context);

  //
  // release the context
  //
  spn(context_release(context));

  //
  // dispose of Vulkan resources
  //
  vk(_pipeline_cache_destroy(device_vk.d,NULL,".vk_cache",device_vk.pc));

  vkDestroyDevice(device_vk.d,NULL);

#ifndef NDEBUG
  vkDestroyDebugReportCallbackEXT(instance,drc,NULL);
#endif

  vkDestroyInstance(instance,NULL);

  return EXIT_SUCCESS;
}

//
//
//

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vk_surface.h"

#include <stdio.h>
#include <stdlib.h>

#include "tests/common/vk_format_matcher.h"
#include "tests/common/vk_strings.h"
#include "tests/common/vk_utils.h"

//
// vk_device_surface_info_t
//

void
vk_device_surface_info_init(vk_device_surface_info_t * info,
                            VkPhysicalDevice           physical_device,
                            VkSurfaceKHR               surface,
                            VkInstance                 instance)
{
  info->physical_device = physical_device;

  // Get capabilities.
  vk(GetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &info->capabilities));

  // Get formats.
  vk(GetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &info->formats_count, NULL));
  info->formats = calloc(info->formats_count, sizeof(info->formats[0]));
  vk(GetPhysicalDeviceSurfaceFormatsKHR(physical_device,
                                        surface,
                                        &info->formats_count,
                                        info->formats));

  // Get present modes.
  vk(GetPhysicalDeviceSurfacePresentModesKHR(physical_device,
                                             surface,
                                             &info->present_modes_count,
                                             NULL));
  info->present_modes = calloc(info->present_modes_count, sizeof(info->present_modes[0]));
  vk(GetPhysicalDeviceSurfacePresentModesKHR(physical_device,
                                             surface,
                                             &info->present_modes_count,
                                             info->present_modes));
}

void
vk_device_surface_info_destroy(vk_device_surface_info_t * info)
{
  free(info->formats);
  info->formats       = NULL;
  info->formats_count = 0;

  free(info->present_modes);
  info->present_modes       = NULL;
  info->present_modes_count = 0;
}

VkFormat
vk_device_surface_info_find_presentation_format(vk_device_surface_info_t * info,
                                                VkImageUsageFlags          wanted_image_usage,
                                                VkFormat                   wanted_format)
{
  vk_format_matcher_t format_matcher;
  vk_format_matcher_init_for_image_usage(&format_matcher,
                                         wanted_image_usage,
                                         info->physical_device);

  if (info->formats_count == 1 && info->formats[0].format == VK_FORMAT_UNDEFINED)
    {
      // Special case, surface has no preferred format, and the application can use any
      // valid VkFormat value.
      if (wanted_format == VK_FORMAT_UNDEFINED)
        {
          // Check a few hard-coded formats for compatibility.
          vk_format_matcher_probe(&format_matcher, VK_FORMAT_R8G8B8A8_SRGB);
          vk_format_matcher_probe(&format_matcher, VK_FORMAT_R8G8B8A8_UNORM);
          vk_format_matcher_probe(&format_matcher, VK_FORMAT_B8G8R8A8_SRGB);
          vk_format_matcher_probe(&format_matcher, VK_FORMAT_B8G8R8A8_SRGB);
        }
      else
        {
          vk_format_matcher_probe(&format_matcher, wanted_format);
        }
    }
  else
    {
      // Find the first surface format that is compatible with |image_usage| with optimal
      // tiling, or linear tiling otherwise.
      for (uint32_t nn = 0; nn < info->formats_count; ++nn)
        {
          VkFormat format = info->formats[nn].format;

          ASSERT_MSG(format != VK_FORMAT_UNDEFINED,
                     "Unexpected VK_FORMAT_UNDEFINED entry in surface format list!\n");

          if (wanted_format != VK_FORMAT_UNDEFINED && wanted_format != format)
            continue;

          vk_format_matcher_probe(&format_matcher, format);
        }
    }

  VkFormat result;
  if (!vk_format_matcher_done(&format_matcher, &result, NULL))
    return VK_FORMAT_UNDEFINED;

  return result;
}

void
vk_device_surface_info_print(const vk_device_surface_info_t * info)
{
  printf("Surface info: num_present_modes=%u num_formats=%u\n",
         info->present_modes_count,
         info->formats_count);

  // Print capabilities.
  {
    const VkSurfaceCapabilitiesKHR * caps = &info->capabilities;
    printf("  minImageCount:             %u\n", caps->minImageCount);
    printf("  maxImageCount:             %u\n", caps->maxImageCount);
    printf("  currentExtent:             %ux%u\n",
           caps->currentExtent.width,
           caps->currentExtent.height);
    printf("  minImageExtent:            %ux%u\n",
           caps->minImageExtent.width,
           caps->minImageExtent.height);
    printf("  maxImageExtent:            %ux%u\n",
           caps->maxImageExtent.width,
           caps->maxImageExtent.height);
    printf("  maxImageArrayLayers:       %u\n", caps->maxImageArrayLayers);
  }

  for (uint32_t nn = 0; nn < info->present_modes_count; ++nn)
    {
      printf("     %s\n", vk_present_mode_khr_to_string(info->present_modes[nn]));
    }

  for (uint32_t nn = 0; nn < info->formats_count; ++nn)
    {
      const VkSurfaceFormatKHR sf = info->formats[nn];
      printf("     %s : %s\n",
             vk_format_to_string(sf.format),
             vk_colorspace_khr_to_string(sf.colorSpace));
      VkFormatProperties format_props;
      vkGetPhysicalDeviceFormatProperties(info->physical_device, sf.format, &format_props);
      printf("        linearTilingFeatures:   %s\n",
             vk_format_feature_flags_to_string(format_props.linearTilingFeatures));
      printf("        optimalTilingFeatures:  %s\n",
             vk_format_feature_flags_to_string(format_props.optimalTilingFeatures));
      printf("        bufferFeatures:         %s\n",
             vk_format_feature_flags_to_string(format_props.bufferFeatures));
    }
}

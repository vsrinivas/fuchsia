// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vk_format_matcher.h"

#include "tests/common/utils.h"
#include "tests/common/vk_utils.h"

//
// vk_format_matcher_t
//

static PFN_vkGetPhysicalDeviceFormatProperties s_callback = &vkGetPhysicalDeviceFormatProperties;

enum MatchMode
{
  MATCH_FORMAT_FEATURES,
  MATCH_IMAGE_USAGE,
};

void
vk_format_matcher_init_for_format_features(vk_format_matcher_t * matcher,
                                           VkFormatFeatureFlags  format_features,
                                           VkPhysicalDevice      physical_device)
{
  matcher->physical_device       = physical_device;
  matcher->mode                  = MATCH_FORMAT_FEATURES;
  matcher->format_features       = format_features;
  matcher->optimal_tiling_format = UINT32_MAX;
  matcher->linear_tiling_format  = UINT32_MAX;
}

void
vk_format_matcher_init_for_image_usage(vk_format_matcher_t * matcher,
                                       VkImageUsageFlags     image_usage,
                                       VkPhysicalDevice      physical_device)
{
  matcher->physical_device       = physical_device;
  matcher->mode                  = MATCH_IMAGE_USAGE;
  matcher->image_usage           = image_usage;
  matcher->optimal_tiling_format = UINT32_MAX;
  matcher->linear_tiling_format  = UINT32_MAX;
}

void
vk_format_matcher_probe(vk_format_matcher_t * matcher, VkFormat format)
{
  VkFormatProperties format_props;
  s_callback(matcher->physical_device, format, &format_props);

  if (matcher->mode == MATCH_IMAGE_USAGE)
    {
      if (vk_check_image_usage_vs_format_features(matcher->image_usage,
                                                  format_props.optimalTilingFeatures))
        {
          if (matcher->optimal_tiling_format == UINT32_MAX)
            matcher->optimal_tiling_format = format;
        }
      if (vk_check_image_usage_vs_format_features(matcher->image_usage,
                                                  format_props.linearTilingFeatures))
        {
          if (matcher->linear_tiling_format == UINT32_MAX)
            matcher->linear_tiling_format = format;
        }
    }
  else if (matcher->mode == MATCH_FORMAT_FEATURES)
    {
      if ((format_props.optimalTilingFeatures & matcher->format_features) ==
          matcher->format_features)
        {
          if (matcher->optimal_tiling_format == UINT32_MAX)
            matcher->optimal_tiling_format = format;
        }
      if ((format_props.linearTilingFeatures & matcher->format_features) ==
          matcher->format_features)
        {
          if (matcher->linear_tiling_format == UINT32_MAX)
            matcher->linear_tiling_format = format;
        }
    }
  else
    {
      ASSERT_MSG(false, "Invalid vk_format_matcher_t instance!");
    }
}

bool
vk_format_matcher_done(vk_format_matcher_t * matcher, VkFormat * p_format, VkImageTiling * p_tiling)
{
  if (matcher->optimal_tiling_format != UINT32_MAX)
    {
      *p_format = matcher->optimal_tiling_format;
      if (p_tiling)
        *p_tiling = VK_IMAGE_TILING_OPTIMAL;
      return true;
    }
  if (matcher->linear_tiling_format != UINT32_MAX)
    {
      *p_format = matcher->linear_tiling_format;
      if (p_tiling)
        *p_tiling = VK_IMAGE_TILING_LINEAR;
      return true;
    }
  *p_format = VK_FORMAT_UNDEFINED;
  return false;
}

void
vk_format_matcher_set_properties_callback_for_testing(
  PFN_vkGetPhysicalDeviceFormatProperties callback)
{
  if (!callback)
    callback = &vkGetPhysicalDeviceFormatProperties;

  s_callback = callback;
}

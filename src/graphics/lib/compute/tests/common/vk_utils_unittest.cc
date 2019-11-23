// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tests/common/vk_utils.h"

#include <gtest/gtest.h>

#include "tests/common/vk_strings.h"

TEST(ImageUsageTest, CheckVersusFeatureFlags)
{
  // clang-format off
  struct {
    VkImageUsageFlags     image_usage;
    VkFormatFeatureFlags  feature_flags;
    bool                  expected;
  } kData[] = {
    {VK_IMAGE_USAGE_TRANSFER_SRC_BIT,         VK_FORMAT_FEATURE_TRANSFER_SRC_BIT,     true},
    {VK_IMAGE_USAGE_TRANSFER_DST_BIT,         VK_FORMAT_FEATURE_TRANSFER_DST_BIT,     true},
    {VK_IMAGE_USAGE_STORAGE_BIT,              VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT,    true},
    {VK_IMAGE_USAGE_SAMPLED_BIT,              VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT,    true},
    {VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,     VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT, true},
    {VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,     VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT,    false},
    {VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT, (VkFormatFeatureFlags)0,                true},
    {VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,     (VkFormatFeatureFlags)0,                true},

    {VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,
     VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT,
     true},

    {VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,
     VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT,
     false},
  };
  // clang-format on
  for (const auto & data : kData)
    {
      ASSERT_EQ(data.expected,
                vk_check_image_usage_vs_format_features(data.image_usage, data.feature_flags))
        << "Image usage " << vk_image_usage_flags_to_string(data.image_usage) << " vs features "
        << vk_format_feature_flags_to_string(data.feature_flags);
    }
}

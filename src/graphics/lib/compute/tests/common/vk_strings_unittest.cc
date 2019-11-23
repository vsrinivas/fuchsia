// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tests/common/vk_strings.h"

#include <gtest/gtest.h>

TEST(vkStringsTest, VkImageUsageFlags)
{
  // clang-format off
  static const struct {
    VkImageUsageFlags flags;
    const char*       expected;
  } kData[] = {
    { (VkImageUsageFlags)0, "NONE" },
    { VK_IMAGE_USAGE_STORAGE_BIT, "VK_IMAGE_USAGE_STORAGE_BIT" },
    { VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      "VK_IMAGE_USAGE_[TRANSFER_SRC|TRANSFER_DST]_BIT" },
    { VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      "VK_IMAGE_USAGE_[SAMPLED|STORAGE|COLOR_ATTACHMENT]_BIT" },
  };
  // clang-format on
  for (const auto & data : kData)
    {
      EXPECT_STREQ(data.expected, vk_image_usage_flags_to_string(data.flags));
    }
}

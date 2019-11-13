// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tests/common/vk_format_matcher.h"

#include <gtest/gtest.h>

#include "tests/common/utils.h"

// Help Test class that injects a custom
class VkFormatMatcherTest : public ::testing::Test {
 public:
  void
  SetUp() override
  {
    vk_format_matcher_set_properties_callback_for_testing(&myFormatPropertiesCallback);
  }

  void
  TearDown() override
  {
    vk_format_matcher_set_properties_callback_for_testing(nullptr);
  }

  VkPhysicalDevice
  physicalDevice() const
  {
    return reinterpret_cast<VkPhysicalDevice>(const_cast<VkFormatMatcherTest *>(this));
  }

  void
  addFormat(VkFormat format, VkFormatProperties properties)
  {
    formats_.emplace(format, std::move(properties));
  }

  void
  getFormatProperties(VkFormat format, VkFormatProperties * pFormatProperties) const
  {
    auto it = formats_.find(format);
    if (it == formats_.end())
      {
        *pFormatProperties = {};
      }
    else
      {
        *pFormatProperties = it->second;
      }
  }

  struct MatchResult
  {
    VkFormat      format;
    VkImageTiling tiling;
  };

  MatchResult
  probeFormatsForImageUsage(VkImageUsageFlags image_usage) const
  {
    MatchResult result;

    vk_format_matcher_t matcher;
    vk_format_matcher_init_for_image_usage(&matcher, image_usage, physicalDevice());

    for (const auto & it : formats_)
      vk_format_matcher_probe(&matcher, it.first);

    vk_format_matcher_done(&matcher, &result.format, &result.tiling);
    return result;
  }

  MatchResult
  probeFormatsForFeatures(VkFormatFeatureFlags format_features) const
  {
    MatchResult result;

    vk_format_matcher_t matcher;
    vk_format_matcher_init_for_format_features(&matcher, format_features, physicalDevice());

    for (const auto & it : formats_)
      vk_format_matcher_probe(&matcher, it.first);

    vk_format_matcher_done(&matcher, &result.format, &result.tiling);
    return result;
  }

 protected:
  static void
  myFormatPropertiesCallback(VkPhysicalDevice     physical_device,
                             VkFormat             format,
                             VkFormatProperties * pFormatProperties)
  {
    reinterpret_cast<const VkFormatMatcherTest *>(physical_device)
      ->getFormatProperties(format, pFormatProperties);
  }

  std::map<VkFormat, VkFormatProperties> formats_;
};

TEST_F(VkFormatMatcherTest, MatchImageUsage)
{
  addFormat(VK_FORMAT_R8G8B8A8_UNORM,
            (const VkFormatProperties){
              .linearTilingFeatures =
                VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT,

              .optimalTilingFeatures =
                VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT,
            });

  addFormat(VK_FORMAT_B8G8R8A8_UNORM,
            (const VkFormatProperties){
              .linearTilingFeatures =
                VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT,

              .optimalTilingFeatures = VK_FORMAT_FEATURE_TRANSFER_SRC_BIT,
            });

  // Report VK_FORMAT_UNDEFINED if there is no match.
  MatchResult probe = probeFormatsForImageUsage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
  EXPECT_EQ(probe.format, VK_FORMAT_UNDEFINED);

  // When both optimal and linear feature masks support the same usage, take
  // the optimal tiling.
  probe = probeFormatsForImageUsage(VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
  EXPECT_EQ(probe.format, VK_FORMAT_B8G8R8A8_UNORM);
  EXPECT_EQ(probe.tiling, VK_IMAGE_TILING_OPTIMAL);

  // Fallback to the linear tiling if the optimal one does not support
  // the usage bit.
  probe = probeFormatsForImageUsage(VK_IMAGE_USAGE_TRANSFER_DST_BIT);
  EXPECT_EQ(probe.format, VK_FORMAT_B8G8R8A8_UNORM);
  EXPECT_EQ(probe.tiling, VK_IMAGE_TILING_LINEAR);

  probe = probeFormatsForImageUsage(VK_IMAGE_USAGE_STORAGE_BIT);
  EXPECT_EQ(probe.format, VK_FORMAT_R8G8B8A8_UNORM);
  EXPECT_EQ(probe.tiling, VK_IMAGE_TILING_LINEAR);

  probe = probeFormatsForImageUsage(VK_IMAGE_USAGE_SAMPLED_BIT);
  EXPECT_EQ(probe.format, VK_FORMAT_R8G8B8A8_UNORM);
  EXPECT_EQ(probe.tiling, VK_IMAGE_TILING_OPTIMAL);

  probe = probeFormatsForImageUsage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
  EXPECT_EQ(probe.format, VK_FORMAT_R8G8B8A8_UNORM);
  EXPECT_EQ(probe.tiling, VK_IMAGE_TILING_OPTIMAL);
}

TEST_F(VkFormatMatcherTest, MatchFormatFeatures)
{
  addFormat(VK_FORMAT_R8G8B8A8_UNORM,
            (const VkFormatProperties){
              .linearTilingFeatures =
                VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT,

              .optimalTilingFeatures =
                VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT,
            });

  addFormat(VK_FORMAT_B8G8R8A8_UNORM,
            (const VkFormatProperties){
              .linearTilingFeatures =
                VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT,

              .optimalTilingFeatures = VK_FORMAT_FEATURE_TRANSFER_SRC_BIT,
            });

  // Report VK_FORMAT_UNDEFINED if there is no match.
  MatchResult probe = probeFormatsForFeatures(VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
  EXPECT_EQ(probe.format, VK_FORMAT_UNDEFINED);

  // When both optimal and linear feature masks support the same usage, take
  // the optimal tiling.
  probe = probeFormatsForFeatures(VK_FORMAT_FEATURE_TRANSFER_SRC_BIT);
  EXPECT_EQ(probe.format, VK_FORMAT_B8G8R8A8_UNORM);
  EXPECT_EQ(probe.tiling, VK_IMAGE_TILING_OPTIMAL);

  // Fallback to the linear tiling if the optimal one does not support
  // the usage bit.
  probe = probeFormatsForFeatures(VK_FORMAT_FEATURE_TRANSFER_DST_BIT);
  EXPECT_EQ(probe.format, VK_FORMAT_B8G8R8A8_UNORM);
  EXPECT_EQ(probe.tiling, VK_IMAGE_TILING_LINEAR);

  probe = probeFormatsForFeatures(VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_EQ(probe.format, VK_FORMAT_R8G8B8A8_UNORM);
  EXPECT_EQ(probe.tiling, VK_IMAGE_TILING_LINEAR);

  probe = probeFormatsForFeatures(VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
  EXPECT_EQ(probe.format, VK_FORMAT_R8G8B8A8_UNORM);
  EXPECT_EQ(probe.tiling, VK_IMAGE_TILING_OPTIMAL);

  probe = probeFormatsForFeatures(VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
  EXPECT_EQ(probe.format, VK_FORMAT_R8G8B8A8_UNORM);
  EXPECT_EQ(probe.tiling, VK_IMAGE_TILING_OPTIMAL);
}

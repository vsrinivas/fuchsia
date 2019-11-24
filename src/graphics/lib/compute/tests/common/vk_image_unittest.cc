// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vk_image.h"

#include <gtest/gtest.h>

#include "vk_app_state.h"

class VkImageTest : public ::testing::Test {
 protected:
  void
  SetUp() override
  {
    const vk_app_state_config_t config = { .device_config = {
                                             .required_queues = VK_QUEUE_GRAPHICS_BIT,
                                           } };
    ASSERT_TRUE(vk_app_state_init(&app_, &config));
  }

  void
  TearDown() override
  {
    vk_app_state_destroy(&app_);
  }

  VkDevice
  device() const
  {
    return app_.d;
  }

  const VkAllocationCallbacks *
  allocator() const
  {
    return app_.ac;
  }

  VkPhysicalDevice
  physicalDevice() const
  {
    return app_.pd;
  }

  vk_queue_families_t
  getQueueFamilies() const
  {
    return vk_app_state_get_queue_families(&app_);
  }

 private:
  vk_app_state_t app_;
};

TEST_F(VkImageTest, AllocDeviceLocal)
{
  vk_image_t image = {};

  const VkFormat          kImageFormat = VK_FORMAT_R8G8B8A8_UNORM;
  const VkExtent2D        kImageExtent = { 100, 100 };
  const VkImageUsageFlags kUsageFlags =
    VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

  vk_image_alloc_device_local(&image,
                              kImageFormat,
                              kImageExtent,
                              kUsageFlags,
                              physicalDevice(),
                              device(),
                              allocator());

  const VkDeviceSize kMinimumImageSize = kImageExtent.width * kImageExtent.height * 4;

  ASSERT_GE(image.size, kMinimumImageSize);
  ASSERT_TRUE(image.image);
  ASSERT_TRUE(image.memory);
  ASSERT_TRUE(image.image_view);

  vk_image_free(&image);

  ASSERT_FALSE(image.image_view);
  ASSERT_FALSE(image.memory);
  ASSERT_FALSE(image.image);
}

TEST_F(VkImageTest, AllocGeneric)
{
  vk_image_t image = {};

  const VkFormat          kImageFormat = VK_FORMAT_B8G8R8A8_UNORM;
  const VkExtent2D        kImageExtent = { 100, 100 };
  const VkImageUsageFlags kUsageFlags =
    VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

  VkMemoryPropertyFlags kMemoryFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

  vk_queue_families_t queue_families = getQueueFamilies();

  vk_image_alloc_generic(&image,
                         kImageFormat,
                         kImageExtent,
                         VK_IMAGE_TILING_OPTIMAL,
                         kUsageFlags,
                         VK_IMAGE_LAYOUT_UNDEFINED,
                         kMemoryFlags,
                         queue_families.count,
                         queue_families.indices,
                         physicalDevice(),
                         device(),
                         allocator());

  const VkDeviceSize kMinimumImageSize = kImageExtent.width * kImageExtent.height * 4;

  ASSERT_GE(image.size, kMinimumImageSize);
  ASSERT_TRUE(image.image);
  ASSERT_TRUE(image.memory);
  ASSERT_TRUE(image.image_view);

  vk_image_free(&image);

  ASSERT_FALSE(image.image_view);
  ASSERT_FALSE(image.memory);
  ASSERT_FALSE(image.image);
}

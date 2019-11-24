// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vk_buffer.h"

#include <gtest/gtest.h>

#include "vk_app_state.h"

class vkBufferTest : public ::testing::Test {
 protected:
  void
  SetUp() override
  {
    const vk_app_state_config_t config = { .device_config = {
                                             .required_queues =
                                               VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
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
  getDeviceQueues() const
  {
    return vk_app_state_get_queue_families(&app_);
  }

 private:
  vk_app_state_t app_;
};

TEST_F(vkBufferTest, AllocHost)
{
  vk_buffer_t buffer = {};

  const VkDeviceSize       kBufferWantedSize = 8000;
  const VkBufferUsageFlags kUsageFlags =
    VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
    VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT |
    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
    VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
    VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

  vk_buffer_alloc_host(&buffer,
                       kBufferWantedSize,
                       kUsageFlags,
                       physicalDevice(),
                       device(),
                       allocator());

  ASSERT_GE(buffer.size, kBufferWantedSize);
  ASSERT_TRUE(buffer.buffer);
  ASSERT_TRUE(buffer.memory);
  ASSERT_TRUE(buffer.mapped);

  // Write random stuff to the buffer to verify that the mapping works.
  for (VkDeviceSize nn = 0; nn < buffer.size; ++nn)
    {
      *reinterpret_cast<uint8_t *>(buffer.mapped) = (uint8_t)nn;
    }

  // Flush it.
  vk_buffer_flush_all(&buffer);

  vk_buffer_free(&buffer);
  ASSERT_FALSE(buffer.mapped);
  ASSERT_FALSE(buffer.memory);
  ASSERT_FALSE(buffer.buffer);
}

TEST_F(vkBufferTest, AllocHostCachedCoherent)
{
  vk_buffer_t buffer = {};

  const VkDeviceSize       kBufferWantedSize = 8000;
  const VkBufferUsageFlags kUsageFlags =
    VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
    VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT |
    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
    VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
    VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

  vk_buffer_alloc_host_coherent(&buffer,
                                kBufferWantedSize,
                                kUsageFlags,
                                physicalDevice(),
                                device(),
                                allocator());

  ASSERT_GE(buffer.size, kBufferWantedSize);
  ASSERT_TRUE(buffer.buffer);
  ASSERT_TRUE(buffer.memory);
  ASSERT_TRUE(buffer.mapped);

  // Write random stuff to the buffer to verify that the mapping works.
  for (VkDeviceSize nn = 0; nn < buffer.size; ++nn)
    {
      *reinterpret_cast<uint8_t *>(buffer.mapped) = (uint8_t)nn;
    }

  vk_buffer_free(&buffer);
  ASSERT_FALSE(buffer.mapped);
  ASSERT_FALSE(buffer.memory);
  ASSERT_FALSE(buffer.buffer);
}

TEST_F(vkBufferTest, AllocDeviceLocal)
{
  vk_buffer_t buffer = {};

  const VkDeviceSize       kBufferWantedSize = 8000;
  const VkBufferUsageFlags kUsageFlags =
    VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
    VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT |
    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
    VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
    VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

  vk_buffer_alloc_device_local(&buffer,
                               kBufferWantedSize,
                               kUsageFlags,
                               physicalDevice(),
                               device(),
                               allocator());

  ASSERT_GE(buffer.size, kBufferWantedSize);
  ASSERT_TRUE(buffer.buffer);
  ASSERT_TRUE(buffer.memory);
  ASSERT_FALSE(buffer.mapped);

  vk_buffer_free(&buffer);
  ASSERT_FALSE(buffer.mapped);
  ASSERT_FALSE(buffer.memory);
  ASSERT_FALSE(buffer.buffer);
}

TEST_F(vkBufferTest, AllocGeneric)
{
  vk_buffer_t buffer = {};

  const VkDeviceSize       kBufferWantedSize = 8000;
  const VkBufferUsageFlags kUsageFlags =
    VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
    VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT |
    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
    VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
    VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

  VkMemoryPropertyFlags kMemoryFlags =
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

  vk_queue_families_t queue_families = getDeviceQueues();

  vk_buffer_alloc_generic(&buffer,
                          kBufferWantedSize,
                          kUsageFlags,
                          kMemoryFlags,
                          queue_families.count,
                          queue_families.indices,
                          physicalDevice(),
                          device(),
                          allocator());

  ASSERT_GE(buffer.size, kBufferWantedSize);
  ASSERT_TRUE(buffer.buffer);
  ASSERT_TRUE(buffer.memory);
  ASSERT_TRUE(buffer.mapped);

  // Write random stuff to the buffer to verify that the mapping works.
  for (VkDeviceSize nn = 0; nn < buffer.size; ++nn)
    {
      *reinterpret_cast<uint8_t *>(buffer.mapped) = (uint8_t)nn;
    }

  vk_buffer_free(&buffer);
  ASSERT_FALSE(buffer.mapped);
  ASSERT_FALSE(buffer.memory);
  ASSERT_FALSE(buffer.buffer);
}

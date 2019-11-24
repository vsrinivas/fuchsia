// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vk_sampler.h"

#include <gtest/gtest.h>

#include "vk_app_state.h"

class VkSamplerTest : public ::testing::Test {
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

 private:
  vk_app_state_t app_;
};

TEST_F(VkSamplerTest, CreateLinearClampToEdge)
{
  VkSampler sampler = vk_sampler_create_linear_clamp_to_edge(device(), allocator());
  ASSERT_TRUE(sampler);
  vkDestroySampler(device(), sampler, allocator());
}

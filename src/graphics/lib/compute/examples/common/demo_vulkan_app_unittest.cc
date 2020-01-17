// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "demo_vulkan_app.h"

#include <gtest/gtest.h>
#include <stdio.h>

#include "tests/common/vk_utils.h"

class TestDemoVulkanApp : public DemoVulkanApp {
 public:
  TestDemoVulkanApp() = default;

  bool setup_called_    = false;
  bool teardown_called_ = false;

  void
  setMaxCounter(uint32_t max_counter)
  {
    max_counter_ = max_counter;
  }

  uint32_t
  counter() const
  {
    return counter_;
  }

 protected:
  bool
  setup() override
  {
    setup_called_ = true;
    return true;
  }

  void
  teardown() override
  {
    teardown_called_ = true;
  }

  bool
  drawFrame(uint32_t frame_counter) override
  {
    if (++counter_ == max_counter_)
      return false;

    if (swapchain_queue_)
      {
        if (!acquireSwapchainQueueImage())
          return false;

        presentSwapchainQueueImage();
      }
    else
      {
        if (!acquireSwapchainImage())
          return false;

        // An empty submit is needed to signal the right semaphore.
        vk_submit_one(vk_swapchain_get_image_acquired_semaphore(swapchain_),
                      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                      vk_swapchain_get_image_rendered_semaphore(swapchain_),
                      graphics_queue_,
                      VK_NULL_HANDLE,
                      VK_NULL_HANDLE);

        presentImage();
      }
    return true;
  }

  uint32_t counter_     = 0;
  uint32_t max_counter_ = 1;
};

TEST(DemoVulkanAppTest, SimpleTest)
{
  DemoVulkanApp::Config config = {
    .app_name      = "DemoVulkanAppTest::SimpleTest",
    .window_width  = 16,
    .window_height = 16,
  };

  {
    TestDemoVulkanApp app;
    ASSERT_TRUE(app.init(config));

    // Should stop after 0 instances.
    app.run();
    ASSERT_EQ(app.counter(), 1u);
    ASSERT_TRUE(app.setup_called_);
    ASSERT_TRUE(app.teardown_called_);
  }

  {
    TestDemoVulkanApp app;
    ASSERT_TRUE(app.init(config));

    // Should stop after 10 instances.
    app.setMaxCounter(10u);
    app.run();
    ASSERT_EQ(app.counter(), 10u);
    ASSERT_TRUE(app.setup_called_);
    ASSERT_TRUE(app.teardown_called_);
  }
}

TEST(DemoVulkanAppTest, SimpleTestWithQueue)
{
  DemoVulkanApp::Config config = {
    .app_name               = "DemoVulkanAppTest::SimpleTestWithQueue",
    .window_width           = 16,
    .window_height          = 16,
    .enable_swapchain_queue = true,
    .sync_semaphores_count  = 1,
  };

  {
    TestDemoVulkanApp app;
    ASSERT_TRUE(app.init(config));

    // Should stop after 0 instances.
    app.run();
    ASSERT_EQ(app.counter(), 1u);
    ASSERT_TRUE(app.setup_called_);
    ASSERT_TRUE(app.teardown_called_);
  }

  {
    TestDemoVulkanApp app;
    ASSERT_TRUE(app.init(config));

    // Should stop after 10 instances.
    app.setMaxCounter(10u);
    app.run();
    ASSERT_EQ(app.counter(), 10u);
    ASSERT_TRUE(app.setup_called_);
    ASSERT_TRUE(app.teardown_called_);
  }
}

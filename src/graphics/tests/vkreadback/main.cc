// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

#include <gtest/gtest.h>
#include <vulkan/vulkan.h>

#include "src/graphics/tests/common/utils.h"
#include "src/lib/fxl/test/test_settings.h"
#include "vkreadback.h"

inline constexpr int64_t ms_to_ns(int64_t ms) { return ms * 1000000ull; }

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

TEST(Vulkan, Readback) {
  VkReadbackTest test;
  ASSERT_TRUE(test.Initialize(VK_API_VERSION_1_1));
  ASSERT_TRUE(test.Exec());
  ASSERT_TRUE(test.Readback());
}

TEST(Vulkan, ManyReadback) {
  std::vector<std::unique_ptr<VkReadbackTest>> tests;
  constexpr uint32_t kReps = 75;
  for (uint32_t i = 0; i < kReps; i++) {
    tests.emplace_back(std::make_unique<VkReadbackTest>());
    ASSERT_TRUE(tests.back()->Initialize(VK_API_VERSION_1_1));
    ASSERT_TRUE(tests.back()->Exec());
  }
  for (auto& test : tests) {
    ASSERT_TRUE(test->Readback());
  }
}

TEST(Vulkan, ReadbackLoopWithFenceWaitThread) {
  VkReadbackTest test;
  ASSERT_TRUE(test.Initialize(VK_API_VERSION_1_1));

  std::mutex mutex;
  std::condition_variable cond_var;
  std::queue<vk::Fence> fences;

  constexpr uint32_t kCounter = 500;
  const vk::Device device = test.vulkan_device();

  std::thread thread([&] {
    for (uint32_t i = 0; i < kCounter; i++) {
      vk::Fence fence = {};

      while (!fence) {
        std::unique_lock<std::mutex> lock(mutex);
        if (fences.empty()) {
          cond_var.wait(lock);
          continue;
        }
        fence = fences.front();
        fences.pop();
      }

      EXPECT_EQ(vk::Result::eSuccess,
                device.waitForFences(1, &fence, true /* waitAll */, ms_to_ns(1000)));
      device.destroyFence(fence, nullptr /* allocator */);
    }
  });

  const vk::FenceCreateInfo fence_info{};
  for (uint32_t i = 0; i < kCounter; i++) {
    vk::Fence fence{};
    auto rv_fence = device.createFence(&fence_info, nullptr /* allocator */, &fence);
    ASSERT_EQ(rv_fence, vk::Result::eSuccess);
    {
      std::unique_lock<std::mutex> lock(mutex);
      fences.push(fence);
      const bool transition_image = (i == 0);
      EXPECT_TRUE(test.Submit(fence, transition_image));
      cond_var.notify_one();
    }

    EXPECT_TRUE(test.Wait());
  }

  thread.join();

  EXPECT_TRUE(test.Readback());
}

TEST(Vulkan, ReadbackLoopWithFenceWait) {
  VkReadbackTest test;
  ASSERT_TRUE(test.Initialize(VK_API_VERSION_1_1));

  const vk::Device device = test.vulkan_device();

  vk::Fence fence{};

  {
    vk::FenceCreateInfo fence_info{};
    auto rv_fence = device.createFence(&fence_info, nullptr /* allocator */, &fence);
    ASSERT_EQ(rv_fence, vk::Result::eSuccess);
  }

  constexpr uint32_t kCounter = 500;
  for (uint32_t i = 0; i < kCounter; i++) {
    {
      const bool transition_image = (i == 0);
      EXPECT_TRUE(test.Submit(fence, transition_image));
    }

    EXPECT_EQ(vk::Result::eSuccess,
              device.waitForFences(1, &fence, true /* waitAll */, ms_to_ns(1000)));

    device.resetFences(1, &fence);

    EXPECT_TRUE(test.Readback());
  }

  device.destroyFence(fence, nullptr /* allocator */);
}

TEST(Vulkan, ReadbackLoopWithTimelineWait) {
  VkReadbackTest test;

  if (CheckVulkanTimelineSemaphoreSupport(VK_API_VERSION_1_2) ==
      VulkanExtensionSupportState::kNotSupported) {
    fprintf(stderr, "Timeline semaphore feature not supported. Test skipped.\n");
    GTEST_SKIP();
  }

  ASSERT_TRUE(test.Initialize(VK_API_VERSION_1_2));

  {
    vk::PhysicalDeviceTimelineSemaphoreProperties timeline_properties{};
    auto properties = vk::PhysicalDeviceProperties2{};
    properties.pNext = &timeline_properties;
    test.physical_device().getProperties2(&properties);
    // TODO(fxbug.dev/69054) - remove
    if (timeline_properties.maxTimelineSemaphoreValueDifference == 0)
      GTEST_SKIP();
  }

  const vk::Device device = test.vulkan_device();

  vk::Semaphore semaphore{};

  {
    auto type_create_info =
        vk::SemaphoreTypeCreateInfo().setSemaphoreType(vk::SemaphoreType::eTimeline);
    auto create_info = vk::SemaphoreCreateInfo().setPNext(&type_create_info);
    ASSERT_EQ(vk::Result::eSuccess,
              device.createSemaphore(&create_info, nullptr /* allocator */, &semaphore));
  }

  constexpr uint32_t kCounter = 500;
  for (uint32_t i = 0; i < kCounter; i++) {
    uint64_t timeline_value = 1 + i;

    {
      const bool transition_image = (i == 0);
      EXPECT_TRUE(test.Submit(semaphore, timeline_value, transition_image));
    }

    {
      auto wait_info = vk::SemaphoreWaitInfo()
                           .setSemaphoreCount(1)
                           .setPSemaphores(&semaphore)
                           .setPValues(&timeline_value);
      EXPECT_EQ(vk::Result::eSuccess, device.waitSemaphores(&wait_info, ms_to_ns(1000)));
    }

    {
      auto rv_result = device.getSemaphoreCounterValue(semaphore);
      EXPECT_EQ(rv_result.result, vk::Result::eSuccess);
      EXPECT_EQ(rv_result.value, timeline_value);
    }

    EXPECT_TRUE(test.Readback());
  }

  device.destroySemaphore(semaphore, nullptr /* allocator */);
}

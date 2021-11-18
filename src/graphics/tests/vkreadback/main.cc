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

  ASSERT_TRUE(test.Initialize(VK_API_VERSION_1_2));
  auto timeline_semaphore_support = test.timeline_semaphore_support();

  if (timeline_semaphore_support == VulkanExtensionSupportState::kNotSupported) {
    fprintf(stderr, "Timeline semaphore feature not supported. Test skipped.\n");
    GTEST_SKIP();
  }

  {
    // Check device timeline semaphore support.
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
    // Initialize a timeline semaphore with initial value of 0.
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
      // Every time we submit commands to VkQueue, the value of the timeline
      // semaphore will increment by 1.
      const bool transition_image = (i == 0);
      EXPECT_TRUE(test.Submit(semaphore, timeline_value, transition_image));
    }

    {
      // Wait until the timeline semaphore value is updated.
      auto wait_info = vk::SemaphoreWaitInfo()
                           .setSemaphoreCount(1)
                           .setPSemaphores(&semaphore)
                           .setPValues(&timeline_value);

      // We'll use Vulkan 1.2 core API only if it is supported; otherwise we
      // use Vulkan 1.1 with extension instead. Ditto for below.
      vk::Result wait_result = vk::Result::eErrorInitializationFailed;
      switch (timeline_semaphore_support) {
        case VulkanExtensionSupportState::kSupportedInCore:
          wait_result = device.waitSemaphores(&wait_info, ms_to_ns(1000));
          break;
        case VulkanExtensionSupportState::kSupportedAsExtensionOnly:
          wait_result = device.waitSemaphoresKHR(&wait_info, ms_to_ns(1000), test.vulkan_loader());
          break;
        case VulkanExtensionSupportState::kNotSupported:
          __builtin_unreachable();
          break;
      }
      EXPECT_EQ(vk::Result::eSuccess, wait_result);
    }

    {
      // Verify that the timeline semaphore counter has been updated.
      vk::ResultValue<unsigned long> counter_result(vk::Result::eErrorInitializationFailed, 0u);
      switch (timeline_semaphore_support) {
        case VulkanExtensionSupportState::kSupportedInCore:
          counter_result = device.getSemaphoreCounterValue(semaphore);
          break;
        case VulkanExtensionSupportState::kSupportedAsExtensionOnly:
          counter_result = device.getSemaphoreCounterValueKHR(semaphore, test.vulkan_loader());
          break;
        case VulkanExtensionSupportState::kNotSupported:
          __builtin_unreachable();
          break;
      }
      EXPECT_EQ(counter_result.result, vk::Result::eSuccess);
      EXPECT_EQ(counter_result.value, timeline_value);
    }

    EXPECT_TRUE(test.Readback());
  }

  device.destroySemaphore(semaphore, nullptr /* allocator */);
}

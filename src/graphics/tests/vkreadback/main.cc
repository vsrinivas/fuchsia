// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "src/graphics/tests/common/utils.h"
#include "src/graphics/tests/vkreadback/vkreadback.h"

#include <vulkan/vulkan.hpp>

namespace {

constexpr int64_t ms_to_ns(int64_t ms) { return ms * 1000000ll; }

}  // namespace

TEST(Vulkan, Readback) {
  VkReadbackTest test;
  ASSERT_TRUE(test.Initialize(VK_API_VERSION_1_1));
  ASSERT_TRUE(test.Exec());
  ASSERT_TRUE(test.Readback());
}

TEST(Vulkan, ReadbackMultiple) {
  std::vector<std::unique_ptr<VkReadbackTest>> readback_tests;
  constexpr int kReadbackCount = 75;
  for (int i = 0; i < kReadbackCount; i++) {
    auto readback_test = std::make_unique<VkReadbackTest>();
    ASSERT_TRUE(readback_test->Initialize(VK_API_VERSION_1_1));
    ASSERT_TRUE(readback_test->Exec());
    readback_tests.push_back(std::move(readback_test));
  }
  for (auto& readback_test : readback_tests) {
    ASSERT_TRUE(readback_test->Readback());
  }
}

TEST(Vulkan, ReadbackLoopWithFenceWaitOnSeparateThread) {
  VkReadbackTest readback_test;
  ASSERT_TRUE(readback_test.Initialize(VK_API_VERSION_1_1));

  std::mutex mutex;
  std::condition_variable cond_var;
  std::queue<vk::UniqueFence> fences;

  constexpr int kFenceCount = 500;
  const vk::Device device = readback_test.vulkan_device();

  std::thread fence_waiting_thread([&mutex, &cond_var, &fences, &device] {
    for (int i = 0; i < kFenceCount; i++) {
      vk::UniqueFence fence;

      while (!fence) {
        std::unique_lock<std::mutex> lock(mutex);
        if (fences.empty()) {
          cond_var.wait(lock);
          continue;
        }
        fence = std::move(fences.front());
        fences.pop();
      }

      EXPECT_EQ(vk::Result::eSuccess,
                device.waitForFences(*fence, /* waitAll= */ true, ms_to_ns(1000)));
    }
  });

  const vk::FenceCreateInfo fence_info{};
  for (int i = 0; i < kFenceCount; i++) {
    auto [fence_result, fence] = device.createFenceUnique(fence_info);
    ASSERT_EQ(fence_result, vk::Result::eSuccess);
    EXPECT_TRUE(readback_test.Submit(
        {.include_start_transition = (i == 0), .include_end_barrier = (i == kFenceCount - 1)},
        fence.get()));

    {
      std::unique_lock<std::mutex> lock(mutex);
      fences.push(std::move(fence));
    }
    cond_var.notify_one();
    EXPECT_TRUE(readback_test.Wait());
  }

  fence_waiting_thread.join();

  EXPECT_TRUE(readback_test.Readback());
}

TEST(Vulkan, ReadbackLoopWithFenceWait) {
  VkReadbackTest test;
  ASSERT_TRUE(test.Initialize(VK_API_VERSION_1_1));

  const vk::Device device = test.vulkan_device();

  auto [fence_result, fence] = device.createFenceUnique(vk::FenceCreateInfo{});
  ASSERT_EQ(fence_result, vk::Result::eSuccess);

  constexpr int kIterationCount = 500;
  for (int i = 0; i < kIterationCount; i++) {
    EXPECT_TRUE(
        test.Submit({.include_start_transition = (i == 0), .include_end_barrier = true}, *fence));

    EXPECT_EQ(vk::Result::eSuccess,
              device.waitForFences(*fence, /* waitAll= */ true, ms_to_ns(1000)));

    device.resetFences(*fence);

    EXPECT_TRUE(test.Readback());
  }
}

TEST(Vulkan, ReadbackLoopWithTimelineWait) {
  VkReadbackTest readback_test;

  ASSERT_TRUE(readback_test.Initialize(VK_API_VERSION_1_2));
  auto timeline_semaphore_support = readback_test.timeline_semaphore_support();

  if (timeline_semaphore_support == VulkanExtensionSupportState::kNotSupported) {
    fprintf(stderr, "Timeline semaphore feature not supported. Test skipped.\n");
    GTEST_SKIP();
  }

  const vk::Device device = readback_test.vulkan_device();

  vk::UniqueSemaphore semaphore{};
  {
    // Initialize a timeline semaphore with initial value of 0.
    vk::StructureChain<vk::SemaphoreCreateInfo, vk::SemaphoreTypeCreateInfo> create_info;
    create_info.get<vk::SemaphoreTypeCreateInfo>().semaphoreType = vk::SemaphoreType::eTimeline;
    auto create_semaphore_result = device.createSemaphoreUnique(create_info.get());
    ASSERT_EQ(vk::Result::eSuccess, create_semaphore_result.result);
    semaphore = std::move(create_semaphore_result.value);
  }

  constexpr int kSemaphoreUpdateCount = 500;
  for (int i = 0; i < kSemaphoreUpdateCount; i++) {
    const uint64_t timeline_value = 1 + i;

    {
      // Every time we submit commands to VkQueue, the value of the timeline
      // semaphore will increment by 1.
      EXPECT_TRUE(
          readback_test.Submit({.include_start_transition = (i == 0), .include_end_barrier = true},
                               *semaphore, timeline_value));
    }

    {
      // Wait until the timeline semaphore value is updated.
      vk::SemaphoreWaitInfo wait_info(vk::SemaphoreWaitFlags{}, *semaphore, timeline_value);

      // We'll use Vulkan 1.2 core API only if it is supported; otherwise we
      // use Vulkan 1.1 with extension instead. Ditto for below.
      vk::Result wait_result = vk::Result::eErrorInitializationFailed;
      switch (timeline_semaphore_support) {
        case VulkanExtensionSupportState::kSupportedInCore:
          wait_result = device.waitSemaphores(wait_info, ms_to_ns(1000));
          break;
        case VulkanExtensionSupportState::kSupportedAsExtensionOnly:
          wait_result =
              device.waitSemaphoresKHR(wait_info, ms_to_ns(1000), readback_test.vulkan_loader());
          break;
        case VulkanExtensionSupportState::kNotSupported:
          __builtin_unreachable();
          break;
      }
      EXPECT_EQ(vk::Result::eSuccess, wait_result);
    }

    {
      // Verify that the timeline semaphore counter has been updated.
      vk::ResultValue<uint64_t> counter_result(vk::Result::eErrorInitializationFailed, 0u);
      switch (timeline_semaphore_support) {
        case VulkanExtensionSupportState::kSupportedInCore:
          counter_result = device.getSemaphoreCounterValue(*semaphore);
          break;
        case VulkanExtensionSupportState::kSupportedAsExtensionOnly:
          counter_result =
              device.getSemaphoreCounterValueKHR(*semaphore, readback_test.vulkan_loader());
          break;
        case VulkanExtensionSupportState::kNotSupported:
          __builtin_unreachable();
          break;
      }
      EXPECT_EQ(counter_result.result, vk::Result::eSuccess);
      EXPECT_EQ(counter_result.value, timeline_value);
    }

    EXPECT_TRUE(readback_test.Readback());
  }
}

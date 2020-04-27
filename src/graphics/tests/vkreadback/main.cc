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
  ASSERT_TRUE(test.Initialize());
  ASSERT_TRUE(test.Exec());
  ASSERT_TRUE(test.Readback());
}

TEST(Vulkan, ManyReadback) {
  std::vector<std::unique_ptr<VkReadbackTest>> tests;
  constexpr uint32_t kReps = 75;
  for (uint32_t i = 0; i < kReps; i++) {
    tests.emplace_back(std::make_unique<VkReadbackTest>());
    ASSERT_TRUE(tests.back()->Initialize());
    ASSERT_TRUE(tests.back()->Exec());
  }
  for (auto& test : tests) {
    ASSERT_TRUE(test->Readback());
  }
}

TEST(Vulkan, ReadbackLoopWithFenceWait) {
  VkReadbackTest test;
  ASSERT_TRUE(test.Initialize());

  std::mutex mutex;
  std::condition_variable cond_var;
  std::queue<VkFence> fences;

  constexpr uint32_t kCounter = 500;

  std::thread thread([&] {
    for (uint32_t i = 0; i < kCounter; i++) {
      VkFence fence = VK_NULL_HANDLE;

      while (fence == VK_NULL_HANDLE) {
        std::unique_lock<std::mutex> lock(mutex);
        if (fences.empty()) {
          cond_var.wait(lock);
          continue;
        }
        fence = fences.front();
        fences.pop();
      }

      EXPECT_EQ(VK_SUCCESS, vkWaitForFences(test.vulkan_device(), 1, &fence, true, ms_to_ns(1000)));

      vkDestroyFence(test.vulkan_device(), fence, nullptr /*allocator*/);
    }
  });

  for (uint32_t i = 0; i < kCounter; i++) {
    VkFence fence;
    VkFenceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .pNext = nullptr, .flags = 0};
    ASSERT_EQ(VK_SUCCESS,
              vkCreateFence(test.vulkan_device(), &create_info, nullptr /*allocator*/, &fence));

    {
      std::unique_lock<std::mutex> lock(mutex);
      fences.push(fence);
      EXPECT_TRUE(test.Submit(fence));
      cond_var.notify_one();
    }

    test.Wait();
  }

  thread.join();

  EXPECT_TRUE(test.Readback());
}

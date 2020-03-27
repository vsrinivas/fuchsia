// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <stdint.h>
#include <stdlib.h>

#include <chrono>
#include <vector>

#include "gtest/gtest.h"
#include "src/graphics/tests/common/utils.h"
#include "src/graphics/tests/common/vulkan_context.h"
#include "src/lib/fxl/test/test_settings.h"

#include <vulkan/vulkan.hpp>

namespace {

void *vkallocate(void *user_data, unsigned long size, unsigned long alignment,
                 VkSystemAllocationScope scope) {
  const size_t aligned_size = (size + alignment - 1) / alignment * alignment;
  void *ptr = malloc(aligned_size);
  memset(ptr, 0, aligned_size);
  EXPECT_NE(ptr, nullptr);
  if (ptr) {
    auto *allocations = static_cast<int *>(user_data);
    (*allocations)++;
  }
  return ptr;
}

void *vkreallocate(void *user_data, void *original_ptr, size_t size, size_t alignment,
                   VkSystemAllocationScope scope) {
  const size_t aligned_size = (size + alignment - 1) / alignment * alignment;
  void *ptr = realloc(original_ptr, aligned_size);
  EXPECT_NE(ptr, nullptr);
  return ptr;
}

void vkfree(void *user_data, void *ptr) {
  free(ptr);
  auto *allocations = static_cast<int *>(user_data);
  (*allocations)--;
}

}  // namespace

TEST(VkContext, Unique) {
  const char *app_name = "Test VK Context";
  vk::ApplicationInfo app_info;
  app_info.pApplicationName = app_name;
  vk::InstanceCreateInfo instance_info;
  instance_info.pApplicationInfo = &app_info;

  std::unique_ptr<VulkanContext> ctx =
      VulkanContext::Builder{}.set_instance_info(instance_info).Unique();
  EXPECT_NE(ctx, nullptr);

  // Shallow copy.
  EXPECT_EQ(ctx->instance_info_.pApplicationInfo->pApplicationName, app_name);

  // String contents.
  EXPECT_EQ(strcmp(ctx->instance_info_.pApplicationInfo->pApplicationName, app_name), 0);
}

TEST(VkContext, Allocator) {
  vk::AllocationCallbacks allocator;
  int allocations = 0;
  allocator.pUserData = &allocations;
  allocator.pfnAllocation = &vkallocate;
  allocator.pfnReallocation = &vkreallocate;
  allocator.pfnFree = &vkfree;
  std::unique_ptr<VulkanContext> ctx = VulkanContext::Builder{}.set_allocator(allocator).Unique();
  EXPECT_NE(ctx, nullptr);
  EXPECT_GT(allocations, 0);
  ctx.reset();
  EXPECT_EQ(allocations, 0);
}

int main(int argc, char **argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

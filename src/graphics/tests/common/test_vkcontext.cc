// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <stdint.h>
#include <stdlib.h>

#include <chrono>
#include <vector>

#include <gtest/gtest.h>

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
  if (!original_ptr) {
    auto *allocations = static_cast<int *>(user_data);
    (*allocations)++;
  }
  return ptr;
}

void vkfree(void *user_data, void *ptr) {
  free(ptr);
  auto *allocations = static_cast<int *>(user_data);
  (*allocations)--;
}

struct CallbackUserData {
  explicit CallbackUserData(std::string msg_in = "Msg") : msg(std::move(msg_in)) {}
  std::string msg;
};

}  // namespace

TEST(VkContext, Unique) {
  const char *app_name = "Test VK Context";
  vk::ApplicationInfo app_info;
  app_info.pApplicationName = app_name;
  vk::InstanceCreateInfo instance_info;
  instance_info.pApplicationInfo = &app_info;

  std::unique_ptr<VulkanContext> ctx =
      VulkanContext::Builder{}.set_instance_info(instance_info).Unique();
  ASSERT_NE(ctx, nullptr);

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
  ASSERT_NE(ctx, nullptr);
  EXPECT_GT(allocations, 0);
  ctx.reset();
  EXPECT_EQ(allocations, 0);
}

TEST(VkContext, Queue) {
  vk::QueueFlagBits queue_flag_bits = vk::QueueFlagBits::eCompute;
  std::unique_ptr<VulkanContext> ctx =
      VulkanContext::Builder{}.set_queue_flag_bits(queue_flag_bits).Unique();

  // Failed to find a compute queue, use graphics instead.
  if (!ctx) {
    printf("VulkanContext: No compute queue found.\n");
    fflush(stdout);
    queue_flag_bits = vk::QueueFlagBits::eGraphics;
    ctx = VulkanContext::Builder{}.set_queue_flag_bits(queue_flag_bits).Unique();
  }
  ASSERT_NE(ctx, nullptr);
  EXPECT_EQ(queue_flag_bits, ctx->queue_flag_bits());

  int queue_family_index = ctx->queue_family_index();
  EXPECT_GT(queue_family_index, VulkanContext::kInvalidQueueFamily);
}

static VkBool32 DebugUtilsErrorCallback(VkDebugUtilsMessageSeverityFlagBitsEXT msg_severity,
                                        VkDebugUtilsMessageTypeFlagsEXT msg_types,
                                        const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
                                        void *user_data) {
  auto context_with_data = reinterpret_cast<VulkanContext::ContextWithUserData *>(user_data);
  EXPECT_TRUE(context_with_data->context()->validation_errors_ignored());
  EXPECT_TRUE(msg_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT);

  std::shared_ptr<void> string_void = context_with_data->user_data();
  std::string *string_ptr = static_cast<std::string *>(string_void.get());
  EXPECT_EQ(*string_ptr, "void user_data - error");
  fprintf(stderr, "%s: %s\n", __FUNCTION__, callback_data->pMessage);
  return VK_FALSE;
}

TEST(VkContext, Callback) {
  vk::DebugUtilsMessengerCreateInfoEXT debug_info(
      {} /* create flags */, vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
      vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
          vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation,
      DebugUtilsErrorCallback);
  std::shared_ptr<void> shared_void = std::make_shared<std::string>("void user_data - error");
  VulkanContext::ContextWithUserData user_data(shared_void);

  // Create the device with a bad extension name to force a VK_ERROR_EXTENSION_NOT_PRESENT error.
  vk::DeviceCreateInfo device_info;
  device_info.enabledExtensionCount = 1;
  std::vector<const char *> extensions = {"BOGUS_vk_extension_name"};
  device_info.ppEnabledExtensionNames = extensions.data();

  std::unique_ptr<VulkanContext> ctx = VulkanContext::Builder{}
                                           .set_device_info(device_info)
                                           .set_validation_errors_ignored(true)
                                           .set_debug_utils_messenger(debug_info, user_data)
                                           .Unique();
}

static VkBool32 DebugUtilsUserDataCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT msg_severity, VkDebugUtilsMessageTypeFlagsEXT msg_types,
    const VkDebugUtilsMessengerCallbackDataEXT *callback_data, void *user_data) {
  auto context_with_data = reinterpret_cast<VulkanContext::ContextWithUserData *>(user_data);
  std::shared_ptr<void> test_user_data_void = context_with_data->user_data();
  CallbackUserData *test_user_data_ptr = static_cast<CallbackUserData *>(test_user_data_void.get());
  EXPECT_EQ(test_user_data_ptr->msg, "User Data Message");
  fprintf(stderr, "%s: %s\n", __FUNCTION__, callback_data->pMessage);
  return VK_FALSE;
}

TEST(VkContext, UserData) {
  vk::DebugUtilsMessengerCreateInfoEXT debug_info(
      {} /* create flags */, vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
      vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
          vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation,
      DebugUtilsUserDataCallback);

  std::shared_ptr<void> shared_void =
      std::make_shared<CallbackUserData>(std::string("User Data Message"));
  VulkanContext::ContextWithUserData user_data(shared_void);

  // Create the device with a bad extension name to force a VK_ERROR_EXTENSION_NOT_PRESENT error.
  vk::DeviceCreateInfo device_info;
  device_info.enabledExtensionCount = 1;
  std::vector<const char *> extensions = {"BOGUS_vk_extension_name"};
  device_info.ppEnabledExtensionNames = extensions.data();

  std::unique_ptr<VulkanContext> ctx = VulkanContext::Builder{}
                                           .set_device_info(device_info)
                                           .set_debug_utils_messenger(debug_info, user_data)
                                           .Unique();
}

int main(int argc, char **argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

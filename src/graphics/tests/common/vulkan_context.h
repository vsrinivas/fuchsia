// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_GRAPHICS_TESTS_COMMON_VULKAN_CONTEXT_H_
#define SRC_GRAPHICS_TESTS_COMMON_VULKAN_CONTEXT_H_

#include <memory>

#include "gtest/gtest.h"

#include <vulkan/vulkan.hpp>

class VulkanContext {
 public:
  class Builder;
  static constexpr int kInvalidQueueFamily = -1;

  VulkanContext(const vk::InstanceCreateInfo &instance_info, size_t physical_device_index,
                const vk::DeviceCreateInfo &device_info,
                const vk::DeviceQueueCreateInfo &queue_info,
                vk::Optional<const vk::AllocationCallbacks> allocator = nullptr);

  bool Init();

  const vk::UniqueInstance &instance() const { return instance_; }
  const vk::PhysicalDevice &physical_device() const { return physical_device_; }
  const vk::UniqueDevice &device() const { return device_; }
  const vk::Queue &queue() const { return queue_; }
  int queue_family_index() const { return queue_family_index_; }

 private:
  FRIEND_TEST(VkContext, Unique);

  bool initialized_;

  vk::UniqueInstance instance_;
  vk::InstanceCreateInfo instance_info_;

  vk::PhysicalDevice physical_device_;
  size_t physical_device_index_;

  vk::UniqueDevice device_;
  vk::DeviceCreateInfo device_info_;

  vk::Queue queue_;
  vk::DeviceQueueCreateInfo queue_info_;

  int queue_family_index_;

  vk::Optional<const vk::AllocationCallbacks> allocator_;
};

class VulkanContext::Builder {
 public:
  Builder();

  std::unique_ptr<VulkanContext> Unique() const;

  Builder &set_allocator(vk::Optional<const vk::AllocationCallbacks> allocator);

  //
  // The mutators below shallow-copy the *CreateInfo structs because of the
  // chaining nature of these structs (i.e. the pNext member).
  //
  // The caller of these methods must preserve memory backing the *info
  // members through any calls to Unique() or Shared() which rely upon
  // this information for instantiation.
  //
  // Typical construction example:
  //   auto ctx = VulkanContext::Builder{}.(optional set* calls).Unique();
  //
  Builder &set_instance_info(const vk::InstanceCreateInfo &instance_info);
  Builder &set_physical_device_index(size_t physical_device_index);
  Builder &set_queue_info(const vk::DeviceQueueCreateInfo &queue_info);
  Builder &set_device_info(const vk::DeviceCreateInfo &device_info);

 private:
  vk::InstanceCreateInfo instance_info_;
  size_t physical_device_index_;
  float queue_priority_;
  vk::DeviceQueueCreateInfo queue_info_;
  vk::DeviceCreateInfo device_info_;
  vk::Optional<const vk::AllocationCallbacks> allocator_;
};

#endif  // SRC_GRAPHICS_TESTS_COMMON_VULKAN_CONTEXT_H_

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_GRAPHICS_TESTS_COMMON_VULKAN_CONTEXT_H_
#define SRC_GRAPHICS_TESTS_COMMON_VULKAN_CONTEXT_H_

#include <memory>

#include <gtest/gtest.h>

#include <vulkan/vulkan.hpp>

//
// VulkanContext is a convenience class for handling boilerplate vulkan setup code.
// It creates / encapsulates vulkan:
//   - instance
//   - physical device
//   - queue family
//   - device
//   - queue
//
// VulkanContext leverages hpp to have smart pointer semantics for simplified vulkan
// resource allocation and free.
//
// There are 2 canonical usage modalities expected for VulkanContext:
//
// (1) the simplest mode is to pair VulkanContext with its nested Builder class
// to selectively modify the required vulkan "CreateInfo" structs during
// construction.  E.g. to create an std::unique_ptr<VulkanContext> setting the instance
// CreateInfo and the queue flag bits:
//
//   auto ctx = VulkanContext::Builder{}.set_instance_info(info).set_queue_flag_bits(bits).Unique();
//
// (2) the second construction mode is for more sophisticated construction where more fine
// grained control is required during construction.  There are three primary piecewise construction
// phases that must be done in order:
//
//    - InitInstance()
//    - InitQueueFamily()
//    - InitDevice()
//
// For example, the device CreateInfo structure may need to be customized
// (e.g. to specify protected memory) before calling InitDevice(), and those modifications require
// access to the physical device chosen in the pair of calls to InitInstance() and
// InitQueueFamily().
//
class VulkanContext {
 public:
  class Builder;
  static constexpr int kInvalidQueueFamily = -1;

  VulkanContext(const vk::InstanceCreateInfo &instance_info, size_t physical_device_index,
                const vk::DeviceCreateInfo &device_info,
                const vk::DeviceQueueCreateInfo &queue_info,
                const vk::QueueFlagBits &queue_flag_bits = vk::QueueFlagBits::eGraphics,
                vk::Optional<const vk::AllocationCallbacks> allocator = nullptr,
                bool validation_layers_enabled = true);

  explicit VulkanContext(size_t physical_device_index,
                         const vk::QueueFlagBits &queue_flag_bits = vk::QueueFlagBits::eGraphics,
                         vk::Optional<const vk::AllocationCallbacks> allocator = nullptr);

  bool Init();
  bool InitInstance();
  bool InitQueueFamily();
  bool InitDevice();

  bool set_instance_info(const vk::InstanceCreateInfo &instance_info);
  bool set_device_info(const vk::DeviceCreateInfo &device_info);
  bool set_queue_info(const vk::DeviceQueueCreateInfo &queue_info);
  bool set_queue_flag_bits(const vk::QueueFlagBits &queue_flag_bits);
  void set_validation_layers_enabled(bool enabled) { validation_layers_enabled_ = enabled; }
  // Set to true to ignore validation errors and allow the test to pass even with errors.
  void set_validation_errors_ignored(bool allowed) { validation_errors_ignored_ = allowed; }

  const vk::InstanceCreateInfo &instance_info() const { return instance_info_; }
  const vk::DeviceCreateInfo &device_info() const { return device_info_; }
  const vk::DeviceQueueCreateInfo &queue_info() const { return queue_info_; }

  const vk::UniqueInstance &instance() const;
  const vk::PhysicalDevice &physical_device() const;
  const vk::UniqueDevice &device() const;
  const vk::Queue &queue() const;
  int queue_family_index() const;
  const vk::QueueFlagBits &queue_flag_bits() const { return queue_flag_bits_; }
  bool validation_errors_ignored() const { return validation_errors_ignored_; }

 private:
  FRIEND_TEST(VkContext, Unique);

  bool initialized_ = false;
  bool instance_initialized_ = false;
  bool queue_family_initialized_ = false;
  bool device_initialized_ = false;

  // By default validation errors should fail the test.
  bool validation_errors_ignored_ = false;

  // These ivars are listed in order of their use in initialization.
  vk::UniqueInstance instance_;
  vk::InstanceCreateInfo instance_info_;
  // These vectors may be referenced by |instance_info_|.
  std::vector<const char *> layers_;
  std::vector<const char *> extensions_;
  // By default validation layers should be enabled. A test may want to disable them if it's testing
  // completely invalid behavior that could cause the layers to crash, or if it's a benchmark.
  bool validation_layers_enabled_ = true;
  vk::DispatchLoaderDynamic loader_;
  vk::UniqueHandle<vk::DebugUtilsMessengerEXT, vk::DispatchLoaderDynamic> messenger_;

  vk::PhysicalDevice physical_device_;
  size_t physical_device_index_;

  float queue_priority_ = 0.0f;
  int queue_family_index_;
  vk::QueueFlagBits queue_flag_bits_;
  vk::DeviceQueueCreateInfo queue_info_;

  vk::DeviceCreateInfo device_info_;
  vk::UniqueDevice device_;

  vk::Queue queue_;

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
  Builder &set_queue_flag_bits(const vk::QueueFlagBits &queue_flag_bits);
  Builder &set_validation_layers_enabled(bool enabled);

 private:
  vk::InstanceCreateInfo instance_info_;
  size_t physical_device_index_;
  float queue_priority_;
  vk::DeviceQueueCreateInfo queue_info_;
  vk::DeviceCreateInfo device_info_;
  vk::QueueFlagBits queue_flag_bits_;
  bool validation_layers_enabled_ = true;
  vk::Optional<const vk::AllocationCallbacks> allocator_;
};

#endif  // SRC_GRAPHICS_TESTS_COMMON_VULKAN_CONTEXT_H_

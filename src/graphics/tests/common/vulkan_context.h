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
  class ContextWithUserData;
  static constexpr int kInvalidQueueFamily = -1;
  static vk::DebugUtilsMessengerCreateInfoEXT default_debug_info_s_;
  static ContextWithUserData default_debug_callback_user_data_s_;

  VulkanContext(const vk::InstanceCreateInfo &instance_info, size_t physical_device_index,
                const vk::DeviceCreateInfo &device_info,
                const vk::DeviceQueueCreateInfo &queue_info,
                const vk::QueueFlagBits &queue_flag_bits = vk::QueueFlagBits::eGraphics,
                const vk::DebugUtilsMessengerCreateInfoEXT &debug_info = default_debug_info_s_,
                ContextWithUserData debug_user_data = default_debug_callback_user_data_s_,
                vk::Optional<const vk::AllocationCallbacks> allocator = nullptr,
                bool validation_layers_enabled = true, bool validation_layers_ignored_ = false);

  explicit VulkanContext(size_t physical_device_index,
                         const vk::QueueFlagBits &queue_flag_bits = vk::QueueFlagBits::eGraphics,
                         vk::Optional<const vk::AllocationCallbacks> allocator = nullptr);

  bool Init();
  bool InitInstance();
  bool InitQueueFamily();
  bool InitDevice();

  bool set_instance_info(const vk::InstanceCreateInfo &v);
  bool set_device_info(const vk::DeviceCreateInfo &v);
  bool set_queue_info(const vk::DeviceQueueCreateInfo &v);
  bool set_queue_flag_bits(const vk::QueueFlagBits &v);
  void set_validation_layers_enabled(bool v) { validation_layers_enabled_ = v; }
  // Set to true to ignore validation errors and allow the test to pass even with errors.
  void set_validation_errors_ignored(bool v) { validation_errors_ignored_ = v; }
  void set_validation_allowed_errors(const std::vector<vk::Result> &v);
  void set_debug_utils_messenger(const vk::DebugUtilsMessengerCreateInfoEXT &debug_info,
                                 const ContextWithUserData &user_data);

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

  // Package up the vulkan context and the user data for the debug callback together.
  // |user_data_| declared such that VulkanContext will own the |user_data_| so we don't
  // accidentally end up with a dangling (void *).
  class ContextWithUserData {
   public:
    ContextWithUserData() : user_data_(nullptr) {}
    explicit ContextWithUserData(std::shared_ptr<void> &user_data)
        : user_data_(std::move(user_data)) {}
    const VulkanContext *context() const { return context_; }
    std::shared_ptr<void> user_data() { return user_data_; }

   private:
    // VulkanContext (only) should set the |context_| member.
    friend class VulkanContext;

    VulkanContext *context_ = nullptr;
    std::shared_ptr<void> user_data_;
  };

 private:
  FRIEND_TEST(VkContext, Unique);

  bool initialized_ = false;
  bool instance_initialized_ = false;
  bool queue_family_initialized_ = false;
  bool device_initialized_ = false;

  // These ivars are listed in order of their use in initialization.
  vk::UniqueInstance instance_;
  vk::InstanceCreateInfo instance_info_;

  vk::PhysicalDevice physical_device_;
  size_t physical_device_index_;

  float queue_priority_ = 0.0f;
  int queue_family_index_;
  vk::DeviceQueueCreateInfo queue_info_;

  vk::DeviceCreateInfo device_info_;
  vk::UniqueDevice device_;

  ContextWithUserData debug_callback_user_data_;
  vk::DebugUtilsMessengerCreateInfoEXT debug_info_;

  vk::Queue queue_;
  vk::QueueFlagBits queue_flag_bits_;

  vk::Optional<const vk::AllocationCallbacks> allocator_;

  // The data in |layers_| and |extensions_| may be referenced by |instance_info_|.
  std::vector<const char *> layers_;
  std::vector<const char *> extensions_;

  // By default validation layers should be enabled. A test may want to disable them if it's testing
  // completely invalid behavior that could cause the layers to crash, or if it's a benchmark.
  bool validation_layers_enabled_ = true;

  // By default validation errors should fail the test.
  bool validation_errors_ignored_ = false;

  vk::DispatchLoaderDynamic loader_;
  vk::UniqueHandle<vk::DebugUtilsMessengerEXT, vk::DispatchLoaderDynamic> messenger_;
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
  Builder &set_instance_info(const vk::InstanceCreateInfo &v);
  Builder &set_physical_device_index(size_t v);
  Builder &set_queue_info(const vk::DeviceQueueCreateInfo &v);
  Builder &set_device_info(const vk::DeviceCreateInfo &v);
  Builder &set_queue_flag_bits(const vk::QueueFlagBits &v);
  Builder &set_validation_layers_enabled(bool v);
  Builder &set_validation_errors_ignored(bool v);
  Builder &set_debug_utils_messenger(const vk::DebugUtilsMessengerCreateInfoEXT &v0,
                                     const ContextWithUserData &v1);

 private:
  vk::InstanceCreateInfo instance_info_;
  size_t physical_device_index_;
  float queue_priority_;
  vk::DeviceQueueCreateInfo queue_info_;
  vk::DeviceCreateInfo device_info_;
  vk::QueueFlagBits queue_flag_bits_;
  bool validation_layers_enabled_ = true;
  bool validation_errors_ignored_ = false;
  vk::Optional<const vk::AllocationCallbacks> allocator_;
  vk::DebugUtilsMessengerCreateInfoEXT debug_info_;
  ContextWithUserData debug_callback_user_data_;
};

#endif  // SRC_GRAPHICS_TESTS_COMMON_VULKAN_CONTEXT_H_

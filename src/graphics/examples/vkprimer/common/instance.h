// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_INSTANCE_H_
#define SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_INSTANCE_H_

#include <vector>

#include "src/lib/fxl/macros.h"

#include <vulkan/vulkan.hpp>

namespace vkp {

class Instance {
 public:
  class Builder;
  explicit Instance(bool validation_layers_enabled)
      : validation_layers_enabled_(validation_layers_enabled), allocator_(nullptr) {}
  Instance(const vk::InstanceCreateInfo &instance_info, bool validation_layers_enabled,
           vk::Optional<const vk::AllocationCallbacks> allocator);
  ~Instance();

  bool Init();

  const vk::Instance &get() const;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Instance);
  bool ConfigureDebugMessenger(const vk::Instance &instance);
  std::vector<const char *> GetExtensions();

  bool initialized_ = false;
  std::vector<const char *> extensions_;
  std::vector<const char *> layers_;
  vk::UniqueHandle<vk::DebugUtilsMessengerEXT, vk::DispatchLoaderDynamic> debug_messenger_;
  vk::DispatchLoaderDynamic dispatch_loader_{};
  vk::InstanceCreateInfo instance_info_{};
  bool validation_layers_enabled_ = true;
  vk::Optional<const vk::AllocationCallbacks> allocator_ = nullptr;

  vk::UniqueInstance instance_;
};

class Instance::Builder {
 public:
  Builder();
  Builder(const Builder &) = delete;

  std::shared_ptr<Instance> Shared() const;
  std::unique_ptr<Instance> Unique() const;

  Builder &set_instance_info(const vk::InstanceCreateInfo &v);
  Builder &set_validation_layers_enabled(bool v);
  Builder &set_allocator(const vk::Optional<const vk::AllocationCallbacks> &v);

  const vk::InstanceCreateInfo &instance_info() const { return instance_info_; }

 private:
  vk::InstanceCreateInfo instance_info_{};
  bool validation_layers_enabled_ = true;
  vk::Optional<const vk::AllocationCallbacks> allocator_ = nullptr;
};

}  // namespace vkp

#endif  // SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_INSTANCE_H_

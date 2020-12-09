// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_EXAMPLES_VKPROTO_COMMON_INSTANCE_H_
#define SRC_GRAPHICS_EXAMPLES_VKPROTO_COMMON_INSTANCE_H_

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
           std::vector<const char *> extensions, std::vector<const char *> layers,
           vk::Optional<const vk::AllocationCallbacks> allocator);

  Instance(Instance &&other) noexcept;

  ~Instance();

  bool Init();

  std::shared_ptr<vk::Instance> shared();
  const vk::Instance &get() const;
  bool initialized() const { return initialized_; }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Instance);

  bool initialized_ = false;
  vk::InstanceCreateInfo instance_info_{};
  bool validation_layers_enabled_ = true;
  std::vector<const char *> extensions_;
  std::vector<const char *> layers_;
  vk::Optional<const vk::AllocationCallbacks> allocator_ = nullptr;

  std::shared_ptr<vk::Instance> instance_;
};

class Instance::Builder {
 public:
  Builder();
  Builder(const Builder &) = delete;

  Instance Build() const;
  std::shared_ptr<Instance> Shared() const;
  std::unique_ptr<Instance> Unique() const;

  Builder &set_instance_info(const vk::InstanceCreateInfo &v);
  Builder &set_extensions(std::vector<const char *> v);
  Builder &set_layers(std::vector<const char *> v);
  Builder &set_validation_layers_enabled(bool v);
  Builder &set_allocator(const vk::Optional<const vk::AllocationCallbacks> &v);

  const vk::InstanceCreateInfo &instance_info() const { return instance_info_; }

 private:
  vk::InstanceCreateInfo instance_info_{};
  bool validation_layers_enabled_ = true;
  std::vector<const char *> extensions_;
  std::vector<const char *> layers_;
  vk::Optional<const vk::AllocationCallbacks> allocator_ = nullptr;
};

}  // namespace vkp

#endif  // SRC_GRAPHICS_EXAMPLES_VKPROTO_COMMON_INSTANCE_H_

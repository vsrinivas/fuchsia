// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_DEBUG_UTILS_MESSENGER_H_
#define SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_DEBUG_UTILS_MESSENGER_H_

#include <vector>

#include "src/graphics/examples/vkprimer/common/instance.h"
#include "src/lib/fxl/macros.h"

#include <vulkan/vulkan.hpp>

namespace vkp {

class DebugUtilsMessenger {
 public:
  DebugUtilsMessenger(std::shared_ptr<vk::Instance> instance);
  DebugUtilsMessenger(std::shared_ptr<vk::Instance> instance,
                      const vk::DebugUtilsMessengerCreateInfoEXT &info);
  ~DebugUtilsMessenger();

  bool Init();

  const vk::DebugUtilsMessengerEXT &get() const;
  vk::DebugUtilsMessengerCreateInfoEXT debug_utils_messenger_info();

  // Useful to tweak / customize a usable create info before constructing a
  // DebugUtilsMessenger() instance.
  static vk::DebugUtilsMessengerCreateInfoEXT DefaultDebugUtilsMessengerInfo();

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(DebugUtilsMessenger);

  bool initialized_ = false;
  std::shared_ptr<vk::Instance> instance_;
  const bool use_defaults_ = true;
  vk::DebugUtilsMessengerCreateInfoEXT debug_utils_messenger_info_{};
  vk::DispatchLoaderDynamic dispatch_loader_{};

  vk::UniqueHandle<vk::DebugUtilsMessengerEXT, vk::DispatchLoaderDynamic> debug_utils_messenger_;
};

}  // namespace vkp

#endif  // SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_DEBUG_UTILS_MESSENGER_H_

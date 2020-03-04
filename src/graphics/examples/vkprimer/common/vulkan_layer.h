// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_VULKAN_LAYER_H_
#define SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_VULKAN_LAYER_H_

#include <memory>
#include <vector>

#include "src/lib/fxl/macros.h"
#include "vulkan_instance.h"

#include <vulkan/vulkan.hpp>

class VulkanLayer {
 public:
  VulkanLayer(std::shared_ptr<VulkanInstance> instance);

  bool Init();

  static bool CheckValidationLayerSupport();
  static void AppendRequiredInstanceExtensions(std::vector<const char *> *extensions);
  static void AppendRequiredInstanceLayers(std::vector<const char *> *layers);
  static void AppendValidationInstanceLayers(std::vector<const char *> *layers);
  static void AppendRequiredDeviceLayers(std::vector<const char *> *layers);

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(VulkanLayer);

  bool SetupDebugCallback();

  bool initialized_;
  std::shared_ptr<VulkanInstance> instance_;
  vk::UniqueHandle<vk::DebugUtilsMessengerEXT, vk::DispatchLoaderDynamic> debug_messenger_;
  vk::DispatchLoaderDynamic dispatch_loader_;
};

#endif  // SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_VULKAN_LAYER_H_

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_LAYER_H_
#define GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_LAYER_H_

#include <src/lib/fxl/macros.h>

#include <memory>
#include <vector>

#include "vulkan/vulkan.h"
#include "vulkan_instance.h"

class VulkanLayer {
 public:
  VulkanLayer(std::shared_ptr<VulkanInstance> instance);
  ~VulkanLayer();

  bool Init();

  static bool CheckInstanceLayerSupport();
  static void AppendRequiredInstanceExtensions(
      std::vector<const char *> *extensions);
  static void AppendRequiredInstanceLayers(std::vector<const char *> *layers);
  static void AppendRequiredDeviceLayers(std::vector<const char *> *layers);

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(VulkanLayer);

  bool SetupDebugCallback();

  bool initialized_;
  std::shared_ptr<VulkanInstance> instance_;
  VkDebugUtilsMessengerEXT callback_;
};

#endif  // GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_LAYER_H_

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_SHADER_H_
#define GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_SHADER_H_

#include <src/lib/fxl/macros.h>

#include <string>
#include <vector>

#include "vulkan/vulkan.h"

class VulkanShader {
 public:
  static bool CreateShaderModule(VkDevice device, const std::vector<char>& code,
                                 VkShaderModule* shader_module);
  static bool ReadFile(const std::string& file_name, std::vector<char>* buffer);

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(VulkanShader);
};

#endif  // GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_SHADER_H_

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vulkan_shader.h"

#include <fstream>

#include "utils.h"

bool VulkanShader::ReadFile(const std::string& file_name,
                            std::vector<char>* buffer) {
  std::ifstream file(file_name, std::ios::ate | std::ios::binary);
  if (!file.is_open()) {
    RTN_MSG(false, "Failed to open file \"%s\"\n", file_name.c_str());
  }

  size_t file_size = (size_t)file.tellg();
  buffer->resize(file_size);
  file.seekg(0);
  file.read(buffer->data(), file_size);
  file.close();

  return true;
}

bool VulkanShader::CreateShaderModule(VkDevice device,
                                      const std::vector<char>& code,
                                      VkShaderModule* shader_module) {
  VkShaderModuleCreateInfo create_info = {};
  create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  create_info.codeSize = code.size();
  create_info.pCode = reinterpret_cast<const uint32_t*>(code.data());

  auto err = vkCreateShaderModule(device, &create_info, nullptr, shader_module);
  if (VK_SUCCESS != err) {
    RTN_MSG(false, "VK Error: 0x%x - Failed to create shader module.\n", err);
  }

  return true;
}

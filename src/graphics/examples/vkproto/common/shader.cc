// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/examples/vkproto/common/shader.h"

#include <fstream>

#include "src/graphics/examples/vkproto/common/utils.h"

#include <vulkan/vulkan.hpp>

namespace vkp {

bool Shader::ReadFile(const std::string& file_name, std::vector<char>* buffer) {
  std::ifstream file(file_name, std::ios::ate | std::ios::binary);
  if (!file.is_open()) {
    RTN_MSG(false, "Failed to open file \"%s\"\n", file_name.c_str());
  }

  size_t file_size = static_cast<size_t>(file.tellg());
  buffer->resize(file_size);
  file.seekg(0);
  file.read(buffer->data(), file_size);
  file.close();

  return true;
}

vk::ResultValue<vk::UniqueShaderModule> Shader::CreateShaderModule(vk::Device device,
                                                                   const std::vector<char>& code) {
  vk::ShaderModuleCreateInfo info;
  info.codeSize = code.size();
  info.pCode = reinterpret_cast<const uint32_t*>(code.data());

  return device.createShaderModuleUnique(info);
}

}  // namespace vkp

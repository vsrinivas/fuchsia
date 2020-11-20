// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_SHADER_H_
#define SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_SHADER_H_

#include <string>
#include <vector>

#include "src/lib/fxl/macros.h"

#include <vulkan/vulkan.hpp>

namespace vkp {

class Shader {
 public:
  static vk::ResultValue<vk::UniqueShaderModule> CreateShaderModule(vk::Device device,
                                                                    const std::vector<char>& code);
  static bool ReadFile(const std::string& file_name, std::vector<char>* buffer);

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Shader);
};

}  // namespace vkp

#endif  // SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_SHADER_H_

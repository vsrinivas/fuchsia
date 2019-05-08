// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/shader_module.h"

#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/third_party/granite/vk/shader_utils.h"

namespace escher {

ShaderModule::ShaderModule(vk::Device device, ShaderStage shader_stage)
    : device_(device), stage_(shader_stage) {
  FXL_DCHECK(device_);
}

ShaderModule::~ShaderModule() {
  FXL_DCHECK(listeners_.empty());
  if (module_) {
    device_.destroyShaderModule(module_);
  }
}

void ShaderModule::AddShaderModuleListener(ShaderModuleListener* listener) {
  FXL_DCHECK(listener);
  FXL_DCHECK(std::find(listeners_.begin(), listeners_.end(), listener) ==
             listeners_.end())
      << "ShaderModule::AddShaderModuleListener(): listener already added.";
  listeners_.push_back(listener);
  if (is_valid()) {
    listener->OnShaderModuleUpdated(this);
  }
}

void ShaderModule::RemoveShaderModuleListener(ShaderModuleListener* listener) {
  auto it = std::find(listeners_.begin(), listeners_.end(), listener);
  FXL_DCHECK(it != listeners_.end())
      << "ShaderModule::RemoveShaderModuleListener(): listener not found.";
  listeners_.erase(it);
}

void ShaderModule::RecreateModuleFromSpirvAndNotifyListeners(
    std::vector<uint32_t> spirv) {
  if (module_) {
    device_.destroyShaderModule(module_);
  }

  vk::ShaderModuleCreateInfo info;
  info.codeSize = spirv.size() * sizeof(uint32_t);
  info.pCode = spirv.data();
  module_ = ESCHER_CHECKED_VK_RESULT(device_.createShaderModule(info));

  GenerateShaderModuleResourceLayoutFromSpirv(std::move(spirv), stage_,
                                              &layout_);

  for (auto listener : listeners_) {
    listener->OnShaderModuleUpdated(this);
  }
}

}  // namespace escher

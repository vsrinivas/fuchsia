// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/shader_module.h"

#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/third_party/granite/vk/shader_utils.h"

namespace escher {

ShaderModule::ShaderModule(vk::Device device, ShaderStage shader_stage)
    : device_(device), stage_(shader_stage), is_valid_(false) {}

ShaderModule::~ShaderModule() { FXL_DCHECK(listeners_.empty()); }

vk::ShaderModule ShaderModule::CreateVkHandle() const {
  FXL_DCHECK(device_);
  FXL_DCHECK(is_valid_);

  vk::ShaderModuleCreateInfo info;
  info.codeSize = spirv_.size() * sizeof(uint32_t);
  info.pCode = spirv_.data();

  return ESCHER_CHECKED_VK_RESULT(device_.createShaderModule(info));
}

void ShaderModule::DestroyVkHandle(vk::ShaderModule shader_module) const {
  device_.destroyShaderModule(shader_module);
}

void ShaderModule::AddShaderModuleListener(ShaderModuleListener* listener) {
  FXL_DCHECK(listener);
  FXL_DCHECK(std::find(listeners_.begin(), listeners_.end(), listener) == listeners_.end())
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

void ShaderModule::UpdateSpirvAndNotifyListeners(std::vector<uint32_t> spirv) {
  spirv_ = std::move(spirv);
  is_valid_ = true;
  GenerateShaderModuleResourceLayoutFromSpirv(spirv_, stage_, &layout_);

  for (auto listener : listeners_) {
    listener->OnShaderModuleUpdated(this);
  }
}

}  // namespace escher

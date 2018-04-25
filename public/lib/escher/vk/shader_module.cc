// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/vk/shader_module.h"

#include "lib/escher/impl/vulkan_utils.h"
#include "lib/escher/util/enum_cast.h"
#include "third_party/spirv-cross/spirv_cross.hpp"

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

  GenerateShaderModuleResourceLayoutFromSpirv(std::move(spirv));

  for (auto listener : listeners_) {
    listener->OnShaderModuleUpdated(this);
  }
}

void ShaderModule::GenerateShaderModuleResourceLayoutFromSpirv(
    std::vector<uint32_t> spirv) {
  // Clear current layout; it will be populated below.
  layout_ = {};

  spirv_cross::Compiler compiler(std::move(spirv));
  vk::ShaderStageFlags stage_flags = ShaderStageToFlags(stage_);

  auto resources = compiler.get_shader_resources();
  for (auto& image : resources.sampled_images) {
    auto set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
    auto binding = compiler.get_decoration(image.id, spv::DecorationBinding);
    auto& type = compiler.get_type(image.base_type_id);
    if (type.image.dim == spv::DimBuffer)
      layout_.sets[set].sampled_buffer_mask |= 1u << binding;
    else
      layout_.sets[set].sampled_image_mask |= 1u << binding;
    layout_.sets[set].stages |= stage_flags;

    if (compiler.get_type(type.image.type).basetype ==
        spirv_cross::SPIRType::BaseType::Float)
      layout_.sets[set].fp_mask |= 1u << binding;
  }

  for (auto& image : resources.subpass_inputs) {
    auto set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
    auto binding = compiler.get_decoration(image.id, spv::DecorationBinding);
    layout_.sets[set].input_attachment_mask |= 1u << binding;
    layout_.sets[set].stages |= stage_flags;

    auto& type = compiler.get_type(image.base_type_id);
    if (compiler.get_type(type.image.type).basetype ==
        spirv_cross::SPIRType::BaseType::Float)
      layout_.sets[set].fp_mask |= 1u << binding;
  }

  for (auto& image : resources.storage_images) {
    auto set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
    auto binding = compiler.get_decoration(image.id, spv::DecorationBinding);
    layout_.sets[set].storage_image_mask |= 1u << binding;
    layout_.sets[set].stages |= stage_flags;

    auto& type = compiler.get_type(image.base_type_id);
    if (compiler.get_type(type.image.type).basetype ==
        spirv_cross::SPIRType::BaseType::Float)
      layout_.sets[set].fp_mask |= 1u << binding;
  }

  for (auto& buffer : resources.uniform_buffers) {
    auto set = compiler.get_decoration(buffer.id, spv::DecorationDescriptorSet);
    auto binding = compiler.get_decoration(buffer.id, spv::DecorationBinding);
    layout_.sets[set].uniform_buffer_mask |= 1u << binding;
    layout_.sets[set].stages |= stage_flags;
  }

  for (auto& buffer : resources.storage_buffers) {
    auto set = compiler.get_decoration(buffer.id, spv::DecorationDescriptorSet);
    auto binding = compiler.get_decoration(buffer.id, spv::DecorationBinding);
    layout_.sets[set].storage_buffer_mask |= 1u << binding;
    layout_.sets[set].stages |= stage_flags;
  }

  // TODO(SCN-681): determine what is required to support other pipeline stages,
  // such as tessellation and geometry shaders.
  if (stage_ == ShaderStage::kVertex) {
    for (auto& attrib : resources.stage_inputs) {
      auto location =
          compiler.get_decoration(attrib.id, spv::DecorationLocation);
      layout_.attribute_mask |= 1u << location;
    }
  } else if (stage_ == ShaderStage::kFragment) {
    for (auto& attrib : resources.stage_outputs) {
      auto location =
          compiler.get_decoration(attrib.id, spv::DecorationLocation);
      layout_.render_target_mask |= 1u << location;
    }
  }

  if (!resources.push_constant_buffers.empty()) {
    // Need to declare the entire block.
    size_t size = compiler.get_declared_struct_size(compiler.get_type(
        resources.push_constant_buffers.front().base_type_id));
    layout_.push_constant_offset = 0;
    layout_.push_constant_range = size;
  }
}

}  // namespace escher

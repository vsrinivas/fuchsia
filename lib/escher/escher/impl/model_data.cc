// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/model_data.h"

#include "escher/impl/command_buffer.h"
#include "escher/impl/gpu_allocator.h"
#include "escher/impl/model_uniform_writer.h"
#include "escher/impl/vulkan_utils.h"

namespace escher {
namespace impl {

// DescriptorSetPools allocate new sets as necessary, so these are no big deal.
constexpr uint32_t kInitialPerModelDescriptorSetCount = 50;
constexpr uint32_t kInitialPerObjectDescriptorSetCount = 200;

ModelData::ModelData(vk::Device device, GpuAllocator* allocator)
    : device_(device),
      uniform_buffer_pool_(device, allocator),
      per_model_descriptor_set_pool_(device,
                                     GetPerModelDescriptorSetLayoutCreateInfo(),
                                     kInitialPerModelDescriptorSetCount),
      per_object_descriptor_set_pool_(
          device,
          GetPerObjectDescriptorSetLayoutCreateInfo(),
          kInitialPerObjectDescriptorSetCount) {}

ModelData::~ModelData() {}

const vk::DescriptorSetLayoutCreateInfo&
ModelData::GetPerModelDescriptorSetLayoutCreateInfo() {
  constexpr uint32_t kNumBindings = 2;
  static vk::DescriptorSetLayoutBinding bindings[kNumBindings];
  static vk::DescriptorSetLayoutCreateInfo info;
  static vk::DescriptorSetLayoutCreateInfo* ptr = nullptr;
  if (!ptr) {
    auto& uniform_binding = bindings[0];
    auto& texture_binding = bindings[1];
    uniform_binding.binding = 0;
    uniform_binding.descriptorType = vk::DescriptorType::eUniformBuffer;
    uniform_binding.descriptorCount = 1;
    uniform_binding.stageFlags =
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    texture_binding.binding = 1;
    texture_binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    texture_binding.descriptorCount = 1;
    texture_binding.stageFlags = vk::ShaderStageFlagBits::eFragment;
    info.bindingCount = kNumBindings;
    info.pBindings = bindings;
    ptr = &info;
  }
  return *ptr;
}

const vk::DescriptorSetLayoutCreateInfo&
ModelData::GetPerObjectDescriptorSetLayoutCreateInfo() {
  constexpr uint32_t kNumBindings = 2;
  static vk::DescriptorSetLayoutBinding bindings[kNumBindings];
  static vk::DescriptorSetLayoutCreateInfo info;
  static vk::DescriptorSetLayoutCreateInfo* ptr = nullptr;
  if (!ptr) {
    auto& uniform_binding = bindings[0];
    auto& texture_binding = bindings[1];
    uniform_binding.binding = 0;
    uniform_binding.descriptorType = vk::DescriptorType::eUniformBuffer;
    uniform_binding.descriptorCount = 1;
    uniform_binding.stageFlags =
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    texture_binding.binding = 1;
    texture_binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    texture_binding.descriptorCount = 1;
    texture_binding.stageFlags = vk::ShaderStageFlagBits::eFragment;
    info.bindingCount = kNumBindings;
    info.pBindings = bindings;
    ptr = &info;
  }
  return *ptr;
}

}  // namespace impl
}  // namespace escher

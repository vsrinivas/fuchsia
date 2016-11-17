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
      allocator_(allocator),
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
  static vk::DescriptorSetLayoutBinding binding;
  static vk::DescriptorSetLayoutCreateInfo info;
  static vk::DescriptorSetLayoutCreateInfo* ptr = nullptr;
  if (!ptr) {
    binding.binding = 0;
    binding.descriptorType = vk::DescriptorType::eUniformBuffer;
    binding.descriptorCount = 1;
    binding.stageFlags |= vk::ShaderStageFlagBits::eFragment;
    info.bindingCount = 1;
    info.pBindings = &binding;
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
    uniform_binding.stageFlags |= vk::ShaderStageFlagBits::eFragment;
    uniform_binding.stageFlags |= vk::ShaderStageFlagBits::eVertex;
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

ModelUniformWriter* ModelData::GetWriterWithCapacity(
    CommandBuffer* command_buffer,
    size_t max_object_count,
    float overallocate_percent) {
  auto ptr = writers_[command_buffer].get();
  if (!ptr || ptr->capacity() < max_object_count) {
    // Create a new writer with at least the required capacity.
    uint32_t capacity =
        static_cast<uint32_t>(max_object_count * (1.f + overallocate_percent));
    FTL_CHECK(capacity >= max_object_count);
    auto writer = std::make_unique<ModelUniformWriter>(
        device_, allocator_, capacity, per_model_descriptor_set_pool(),
        per_object_descriptor_set_pool());
    ptr = writer.get();
    writers_[command_buffer] = std::move(writer);
  }
  ptr->BecomeWritable();
  return ptr;
}

}  // namespace impl
}  // namespace escher

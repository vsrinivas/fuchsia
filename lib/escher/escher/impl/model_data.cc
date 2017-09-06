// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/model_data.h"

#include "escher/escher.h"
#include "escher/impl/command_buffer.h"
#include "escher/impl/mesh_shader_binding.h"
#include "escher/impl/vulkan_utils.h"
#include "escher/vk/gpu_allocator.h"

namespace escher {
namespace impl {

// DescriptorSetPools allocate new sets as necessary, so these are no big
// deal.
constexpr uint32_t kInitialPerModelDescriptorSetCount = 50;
constexpr uint32_t kInitialPerObjectDescriptorSetCount = 200;

ModelData::ModelData(Escher* escher, GpuAllocator* allocator)
    : device_(escher->vulkan_context().device),
      uniform_buffer_pool_(escher, allocator),
      per_model_descriptor_set_pool_(escher,
                                     GetPerModelDescriptorSetLayoutCreateInfo(),
                                     kInitialPerModelDescriptorSetCount),
      per_object_descriptor_set_pool_(
          escher,
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

const MeshShaderBinding& ModelData::GetMeshShaderBinding(MeshSpec spec) {
  auto ptr = mesh_shader_binding_cache_[spec].get();
  if (ptr) {
    return *ptr;
  }
  FTL_DCHECK(spec.IsValid());

  std::vector<vk::VertexInputAttributeDescription> attributes;

  vk::DeviceSize stride = 0;
  if (spec.flags & MeshAttribute::kPosition2D) {
    vk::VertexInputAttributeDescription attribute;
    attribute.location = kPositionAttributeLocation;
    attribute.binding = 0;
    attribute.format = vk::Format::eR32G32Sfloat;
    attribute.offset = stride;

    stride += sizeof(vec2);
    attributes.push_back(attribute);
  }
  if (spec.flags & MeshAttribute::kPosition3D) {
    vk::VertexInputAttributeDescription attribute;
    attribute.location = kPositionAttributeLocation;
    attribute.binding = 0;
    attribute.format = vk::Format::eR32G32B32Sfloat;
    attribute.offset = stride;

    stride += sizeof(vec3);
    attributes.push_back(attribute);
  }
  if (spec.flags & MeshAttribute::kPositionOffset) {
    vk::VertexInputAttributeDescription attribute;
    attribute.location = kPositionOffsetAttributeLocation;
    attribute.binding = 0;
    attribute.format = vk::Format::eR32G32Sfloat;
    attribute.offset = stride;

    stride += sizeof(vec2);
    attributes.push_back(attribute);
  }
  if (spec.flags & MeshAttribute::kUV) {
    vk::VertexInputAttributeDescription attribute;
    attribute.location = kUVAttributeLocation;
    attribute.binding = 0;
    attribute.format = vk::Format::eR32G32Sfloat;
    attribute.offset = stride;

    stride += sizeof(vec2);
    attributes.push_back(attribute);
  }
  if (spec.flags & MeshAttribute::kPerimeterPos) {
    vk::VertexInputAttributeDescription attribute;
    attribute.location = kPerimeterPosAttributeLocation;
    attribute.binding = 0;
    attribute.format = vk::Format::eR32Sfloat;
    attribute.offset = stride;

    stride += sizeof(float);
    attributes.push_back(attribute);
  }

  vk::VertexInputBindingDescription binding;
  binding.binding = 0;
  binding.stride = stride;
  binding.inputRate = vk::VertexInputRate::eVertex;

  auto msb = std::make_unique<MeshShaderBinding>(std::move(binding),
                                                 std::move(attributes));
  ptr = msb.get();
  mesh_shader_binding_cache_[spec] = std::move(msb);
  return *ptr;
}

}  // namespace impl
}  // namespace escher

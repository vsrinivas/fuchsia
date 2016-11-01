// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/model_uniform_writer.h"

#include "escher/impl/vulkan_utils.h"

namespace escher {
namespace impl {

namespace {

vk::DescriptorPool CreateDescriptorPool(vk::Device device, uint32_t capacity) {
  vk::DescriptorPoolSize pool_size;
  pool_size = pool_size = vk::DescriptorPoolSize();
  pool_size.type = vk::DescriptorType::eUniformBuffer;
  pool_size.descriptorCount = capacity + 1;

  vk::DescriptorPoolCreateInfo pool_info;
  pool_info.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
  pool_info.poolSizeCount = 1;
  pool_info.pPoolSizes = &pool_size;
  pool_info.maxSets = capacity + 1;

  return ESCHER_CHECKED_VK_RESULT(device.createDescriptorPool(pool_info));
}

// TODO: This is a temporary hack (it is the value on my NVIDIA Quadro).  See
// discussion below.
constexpr uint32_t kMinUniformBufferOffsetAlignment = 256;

}  // namespace

ModelUniformWriter::ModelUniformWriter(
    vk::Device device,
    GpuAllocator* allocator,
    uint32_t capacity,
    vk::DescriptorSetLayout per_model_layout,
    vk::DescriptorSetLayout per_object_layout)
    : device_(device),
      capacity_(capacity),
      uniforms_(device_,
                allocator,
                // TODO: the use of kMinUniformBufferOffsetAlignment is a
                // temporary hack to make buffer-offsets work.  However, buffer
                // offsets are ultimately not what we want to use, because
                // our uniform data is much smaller than 256 bytes; as a result,
                // we waste > 100 bytes per object.  Instead, we should create
                // multiple buffers from a single memory allocation, and bind a
                // different uniform buffer to each descriptor.
                // sizeof(ModelData::PerModel) + capacity *
                // sizeof(ModelData::PerObject),
                (capacity + 1) * kMinUniformBufferOffsetAlignment,
                vk::BufferUsageFlagBits::eUniformBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible),
      descriptor_pool_(CreateDescriptorPool(device_, capacity)) {
  FTL_CHECK(kMinUniformBufferOffsetAlignment >= sizeof(ModelData::PerModel));
  FTL_CHECK(kMinUniformBufferOffsetAlignment >= sizeof(ModelData::PerObject));

  // Create the descriptor sets.
  {
    vk::DescriptorSetAllocateInfo info;
    info.descriptorPool = descriptor_pool_;
    info.descriptorSetCount = 1;
    info.pSetLayouts = &per_model_layout;
    per_model_descriptor_set_ =
        ESCHER_CHECKED_VK_RESULT(device_.allocateDescriptorSets(info))[0];

    std::vector<vk::DescriptorSetLayout> layouts(capacity, per_object_layout);
    info.descriptorSetCount = capacity;
    info.pSetLayouts = layouts.data();
    per_object_descriptor_sets_ =
        ESCHER_CHECKED_VK_RESULT(device_.allocateDescriptorSets(info));
  }

  // The descriptor sets have been created, but not initialized.
  {
    vk::DescriptorBufferInfo info;
    info.buffer = uniforms_.buffer();
    info.offset = 0;

    vk::WriteDescriptorSet write;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.pBufferInfo = &info;

    // Per-Model.
    info.range = sizeof(ModelData::PerModel);
    write.dstSet = per_model_descriptor_set_;
    write.descriptorType = vk::DescriptorType::eUniformBuffer;
    device_.updateDescriptorSets(1, &write, 0, nullptr);

    // Per-Object.
    info.range = sizeof(ModelData::PerObject);
    write.descriptorType = vk::DescriptorType::eUniformBuffer;
    for (size_t i = 0; i < per_object_descriptor_sets_.size(); ++i) {
      write.dstSet = per_object_descriptor_sets_[i];
      info.offset = (i + 1) * kMinUniformBufferOffsetAlignment;
      device_.updateDescriptorSets(1, &write, 0, nullptr);
    }
  }
}

ModelUniformWriter::~ModelUniformWriter() {
  device_.freeDescriptorSets(descriptor_pool_, 1, &per_model_descriptor_set_);
  device_.freeDescriptorSets(descriptor_pool_, capacity_,
                             per_object_descriptor_sets_.data());
  device_.destroyDescriptorPool(descriptor_pool_);
}

void ModelUniformWriter::WritePerModelData(
    const ModelData::PerModel& per_model) {
  FTL_DCHECK(is_writable_);
  auto ptr = reinterpret_cast<ModelData::PerModel*>(uniforms_.Map());
  ptr[0] = per_model;
}

ModelUniformWriter::PerObjectBinding ModelUniformWriter::WritePerObjectData(
    const ModelData::PerObject& per_object) {
  FTL_DCHECK(write_index_ < capacity_);
  auto ptr = reinterpret_cast<ModelData::PerObject*>(
      &uniforms_.Map()[(write_index_ + 1) * kMinUniformBufferOffsetAlignment]);
  ptr[0] = per_object;
  return write_index_++;
}

void ModelUniformWriter::Flush(vk::CommandBuffer command_buffer) {
  FTL_DCHECK(is_writable_);
  is_writable_ = false;

  uniforms_.Unmap();

// TODO: there should be a barrier similar to the following, but it cannot
// happen within a render-pass.  To address this, ModelRenderer::Draw() should
// be split into Prepare() and Draw() methods.
#if 0
  uint32_t flushed_size = (write_index_ + 1) * kMinUniformBufferOffsetAlignment;
  vk::BufferMemoryBarrier barrier;
  barrier.srcAccessMask = vk::AccessFlagBits::eHostWrite;
  // TODO: is this correct/sufficient?  Should there also be e.g. eShaderRead?
  barrier.dstAccessMask = vk::AccessFlagBits::eUniformRead;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.buffer = uniforms_.buffer();
  barrier.offset = 0;
  barrier.size = flushed_size;

  command_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eHost,
                                 vk::PipelineStageFlagBits::eVertexShader |
                                     vk::PipelineStageFlagBits::eFragmentShader,
                                 vk::DependencyFlags(), 0, nullptr, 1, &barrier,
                                 0, nullptr);
#endif

  write_index_ = 0;
}

void ModelUniformWriter::BecomeWritable() {
  FTL_DCHECK(!is_writable_);
  FTL_DCHECK(write_index_ == 0);
  is_writable_ = true;
}

void ModelUniformWriter::BindPerModelData(vk::PipelineLayout pipeline_layout,
                                          vk::CommandBuffer command_buffer) {
  FTL_DCHECK(!is_writable_);
  command_buffer.bindDescriptorSets(
      vk::PipelineBindPoint::eGraphics, pipeline_layout,
      ModelData::PerModel::kDescriptorSetBindIndex, 1,
      &per_model_descriptor_set_, 0, nullptr);
}

void ModelUniformWriter::BindPerObjectData(PerObjectBinding binding,
                                           vk::PipelineLayout pipeline_layout,
                                           vk::CommandBuffer command_buffer) {
  FTL_DCHECK(!is_writable_);
  command_buffer.bindDescriptorSets(
      vk::PipelineBindPoint::eGraphics, pipeline_layout,
      ModelData::PerObject::kDescriptorSetBindIndex, 1,
      &(per_object_descriptor_sets_[binding]), 0, nullptr);
}

}  // namespace impl
}  // namespace escher

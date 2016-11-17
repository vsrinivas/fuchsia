// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/model_uniform_writer.h"

#include "escher/impl/vulkan_utils.h"

namespace escher {
namespace impl {

namespace {

// See discussion in AllocateBuffersAndDescriptorSets().
constexpr vk::DeviceSize kMinUniformBufferOffsetAlignment = 256;

}  // namespace

ModelUniformWriter::ModelUniformWriter(
    uint32_t capacity,
    vk::Device device,
    UniformBufferPool* uniform_buffer_pool,
    DescriptorSetPool* per_model_descriptor_set_pool,
    DescriptorSetPool* per_object_descriptor_set_pool)
    : capacity_(capacity),
      device_(device),
      uniform_buffer_pool_(uniform_buffer_pool),
      per_model_descriptor_set_pool_(per_model_descriptor_set_pool),
      per_object_descriptor_set_pool_(per_object_descriptor_set_pool) {
  FTL_CHECK(kMinUniformBufferOffsetAlignment >= sizeof(ModelData::PerModel));
  FTL_CHECK(kMinUniformBufferOffsetAlignment >= sizeof(ModelData::PerObject));
  AllocateBuffersAndDescriptorSets();
}

ModelUniformWriter::~ModelUniformWriter() {}

void ModelUniformWriter::AllocateBuffersAndDescriptorSets() {
  FTL_DCHECK(!uniforms_);
  FTL_DCHECK(!per_model_descriptor_sets_);
  FTL_DCHECK(!per_object_descriptor_sets_);

  // Allocate a buffer and verify that it is large enough.
  uniforms_ = uniform_buffer_pool_->Allocate();
  // TODO: If the required capacity exceeds the buffer size, use multiple
  // buffers.  This could be done lazily, so that there is no need to know the
  // capacity in advance.
  // TODO: the use of kMinUniformBufferOffsetAlignment is a temporary
  // hack to make buffer-offsets work.  However, buffer offsets are
  // ultimately not what we want to use, because our uniform data is
  // much smaller than 256 bytes; as a result, we waste > 100 bytes per
  // object.  Instead, we should use descriptor sets with arrays, and
  // pass in the array index somehow (e.g. push constant).  See:
  // http://www.gamedev.net/topic/674379-solved-uniform-buffer-actually-viable/
  vk::DeviceSize required_size =
      (capacity_ + 1) * kMinUniformBufferOffsetAlignment;
  FTL_CHECK(uniforms_->size() >= required_size);

  per_model_descriptor_sets_ =
      per_model_descriptor_set_pool_->Allocate(1, nullptr);
  per_object_descriptor_sets_ =
      per_object_descriptor_set_pool_->Allocate(capacity_, nullptr);

  // The descriptor sets have been created, but not initialized.
  {
    vk::DescriptorBufferInfo info;
    info.buffer = uniforms_->get();

    vk::WriteDescriptorSet write;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = vk::DescriptorType::eUniformBuffer;
    write.pBufferInfo = &info;

    // Per-Model.
    info.range = sizeof(ModelData::PerModel);
    info.offset = 0;
    write.dstSet = per_model_descriptor_sets_->get(0);
    write.dstBinding = ModelData::PerModel::kDescriptorSetUniformBinding;
    device_.updateDescriptorSets(1, &write, 0, nullptr);

    // Per-Object.
    info.range = sizeof(ModelData::PerObject);
    for (uint32_t i = 0; i < per_object_descriptor_sets_->size(); ++i) {
      info.offset = (i + 1) * kMinUniformBufferOffsetAlignment;
      write.dstSet = per_object_descriptor_sets_->get(i);
      write.dstBinding = ModelData::PerObject::kDescriptorSetUniformBinding;
      device_.updateDescriptorSets(1, &write, 0, nullptr);
    }
  }
}

void ModelUniformWriter::WritePerModelData(
    const ModelData::PerModel& per_model) {
  FTL_DCHECK(is_writable_);
  auto ptr = reinterpret_cast<ModelData::PerModel*>(uniforms_->ptr());
  ptr[0] = per_model;
}

ModelUniformWriter::PerObjectBinding ModelUniformWriter::WritePerObjectData(
    const ModelData::PerObject& per_object,
    vk::ImageView texture,
    vk::Sampler sampler) {
  // Write the uniforms to the buffer.
  FTL_DCHECK(write_index_ < capacity_);
  auto ptr = reinterpret_cast<ModelData::PerObject*>(
      &uniforms_->ptr()[(write_index_ + 1) * kMinUniformBufferOffsetAlignment]);
  ptr[0] = per_object;

  // Update the texture.
  vk::DescriptorImageInfo image_info;
  image_info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  image_info.imageView = texture;
  image_info.sampler = sampler;
  vk::WriteDescriptorSet write;
  write.dstSet = per_object_descriptor_sets_->get(write_index_);
  write.dstBinding = ModelData::PerObject::kDescriptorSetSamplerBinding;
  write.dstArrayElement = 0;
  write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
  write.descriptorCount = 1;
  write.pImageInfo = &image_info;
  device_.updateDescriptorSets(1, &write, 0, nullptr);

  return write_index_++;
}

void ModelUniformWriter::Flush(CommandBuffer* command_buffer) {
  FTL_DCHECK(is_writable_);
  is_writable_ = false;

  command_buffer->AddUsedResource(uniforms_);
  command_buffer->AddUsedResource(per_model_descriptor_sets_);
  command_buffer->AddUsedResource(per_object_descriptor_sets_);

// TODO: there should be a barrier similar to the following, but it cannot
// happen within a render-pass.  To address this, ModelRenderer::Draw() should
// be split into Prepare() and Draw() methods.
// TODO: for a variety of reasons (see below) we may want to use an individual
// buffer for each PerObject data.  If we do this, then we might wish to use
// a global memory barrier rather than setting barriers for each buffer
// individually.  Reasons to use an individual buffer:
//   - we're now wasting a lot of space due to kMinUniformBufferOffsetAlignment.
//   - would make it easier to have per-pipeline descriptor sets, as follows.
//     Each pipeline could be associated with pools for PerModel/PerObject/etc.
//     data (these pools could be shared with other pipelines that use the
//     same descriptor-set layouts... note that two pipelines might share the
//     same same PerModel pool, but have different PerObject pools).  Each pool
//     entry would contain a descriptor set, but also a uniform buffer and any
//     other useful data (e.g. samplers).
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

void ModelUniformWriter::BindPerModelData(vk::PipelineLayout pipeline_layout,
                                          vk::CommandBuffer command_buffer) {
  FTL_DCHECK(!is_writable_);
  vk::DescriptorSet set_to_bind = per_model_descriptor_sets_->get(0);
  command_buffer.bindDescriptorSets(
      vk::PipelineBindPoint::eGraphics, pipeline_layout,
      ModelData::PerModel::kDescriptorSetIndex, 1, &set_to_bind, 0, nullptr);
}

void ModelUniformWriter::BindPerObjectData(PerObjectBinding binding,
                                           vk::PipelineLayout pipeline_layout,
                                           vk::CommandBuffer command_buffer) {
  FTL_DCHECK(!is_writable_);
  vk::DescriptorSet set_to_bind = per_object_descriptor_sets_->get(binding);
  command_buffer.bindDescriptorSets(
      vk::PipelineBindPoint::eGraphics, pipeline_layout,
      ModelData::PerObject::kDescriptorSetIndex, 1, &set_to_bind, 0, nullptr);
}

}  // namespace impl
}  // namespace escher

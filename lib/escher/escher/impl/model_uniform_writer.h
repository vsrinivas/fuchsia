// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vulkan/vulkan.hpp>

#include "escher/geometry/types.h"
#include "escher/impl/command_buffer.h"
#include "escher/impl/model_data.h"

namespace escher {
namespace impl {

class ModelUniformWriter {
 public:
  ModelUniformWriter(uint32_t capacity,
                     vk::Device device,
                     UniformBufferPool* uniform_buffer_pool,
                     DescriptorSetPool* per_model_descriptor_set_pool,
                     DescriptorSetPool* per_object_descriptor_set_pool);
  ~ModelUniformWriter();

  // PerModel data is first written to the uniform buffer, flushed to the GPU,
  // and then bound before rendering.
  void WritePerModelData(const ModelData::PerModel& per_model,
                         vk::ImageView texture,
                         vk::Sampler sampler);
  void BindPerModelData(vk::PipelineLayout pipeline_layout,
                        vk::CommandBuffer command_buffer);

  // PerObject data is first written to the uniform buffer, flushed to the GPU,
  // and then bound before rendering.  Compared to PerModel data, there is one
  // difference: binding the data requires passing the token obtained when the
  // data was written.
  typedef uint32_t PerObjectBinding;
  PerObjectBinding WritePerObjectData(const ModelData::PerObject& per_object,
                                      vk::ImageView texture,
                                      vk::Sampler sampler);
  void BindPerObjectData(PerObjectBinding binding,
                         vk::PipelineLayout pipeline_layout,
                         vk::CommandBuffer command_buffer);

  uint32_t capacity() const { return capacity_; }

  void Flush(CommandBuffer* command_buffer);

 private:
  // Initialize descriptor sets whenever the writer is reset.
  void AllocateBuffersAndDescriptorSets();

  // The number of ObjectData structs that can be held.
  uint32_t capacity_;

  vk::Device device_;

  UniformBufferPool* uniform_buffer_pool_;
  DescriptorSetPool* per_model_descriptor_set_pool_;
  DescriptorSetPool* per_object_descriptor_set_pool_;

  BufferPtr uniforms_;
  DescriptorSetAllocationPtr per_model_descriptor_sets_;
  DescriptorSetAllocationPtr per_object_descriptor_sets_;

  bool is_writable_ = true;
  uint32_t write_index_ = 0;

  FTL_DISALLOW_COPY_AND_ASSIGN(ModelUniformWriter);
};

}  // namespace impl
}  // namespace escher

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vulkan/vulkan.hpp>

#include "escher/geometry/types.h"
#include "escher/impl/buffer.h"
#include "escher/impl/command_buffer.h"
#include "escher/impl/model_data.h"

namespace escher {
namespace impl {

class ModelUniformWriter {
 public:
  ModelUniformWriter(vk::Device,
                     GpuAllocator* allocator,
                     uint32_t capacity,
                     DescriptorSetPool* per_model_descriptor_set_pool,
                     DescriptorSetPool* per_object_descriptor_set_pool);
  ~ModelUniformWriter();

  // PerModel data is first written to the uniform buffer, flushed to the GPU,
  // and then bound before rendering.
  void WritePerModelData(const ModelData::PerModel& per_model);
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

  // Allows ModelData to guarantee that Flush() is called each frame.
  void BecomeWritable();

 private:
  // Initialize descriptor sets whenever the writer is reset.
  void AllocateDescriptorSets();

  vk::Device device_;
  // The number of ObjectData structs that can be held.
  uint32_t capacity_;
  Buffer uniforms_;

  bool is_writable_ = false;
  uint32_t write_index_ = 0;

  DescriptorSetPool* per_model_descriptor_set_pool_;
  DescriptorSetPool* per_object_descriptor_set_pool_;

  DescriptorSetAllocationPtr per_model_descriptor_sets_;
  DescriptorSetAllocationPtr per_object_descriptor_sets_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ModelUniformWriter);
};

}  // namespace impl
}  // namespace escher

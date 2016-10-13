// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vulkan/vulkan.hpp>

#include "escher/geometry/types.h"
#include "escher/impl/buffer.h"
#include "escher/impl/model_data.h"

namespace escher {
namespace impl {

class ModelUniformWriter {
 public:
  ModelUniformWriter(vk::Device,
                     GpuAllocator* allocator,
                     uint32_t capacity,
                     vk::DescriptorSetLayout per_model_layout,
                     vk::DescriptorSetLayout per_object_layout);
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
  PerObjectBinding WritePerObjectData(const ModelData::PerObject& per_object);
  void BindPerObjectData(PerObjectBinding binding,
                         vk::PipelineLayout pipeline_layout,
                         vk::CommandBuffer command_buffer);

  uint32_t capacity() const { return capacity_; }

  void Flush(vk::CommandBuffer command_buffer);

  // Allows ModelData to guarantee that Flush() is called each frame.
  void BecomeWritable();

 private:
  vk::Device device_;
  // The number of ObjectData structs that can be held.
  uint32_t capacity_;
  Buffer uniforms_;
  vk::DescriptorPool descriptor_pool_;

  bool is_writable_ = false;
  uint32_t write_index_ = 0;

  vk::DescriptorSet per_model_descriptor_set_;
  std::vector<vk::DescriptorSet> per_object_descriptor_sets_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ModelUniformWriter);
};

}  // namespace impl
}  // namespace escher

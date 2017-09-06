// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/forward_declarations.h"
#include "escher/impl/descriptor_set_pool.h"

namespace escher {
namespace impl {

class GlslToSpirvCompiler;

// Simplifies the creation and use of Vulkan compute pipelines.  The current
// implementation is limited to using images, storage/uniform buffers, and
// push-constants for in/output.
class ComputeShader {
 public:
  ComputeShader(Escher* escher,
                std::vector<vk::ImageLayout> layouts,
                std::vector<vk::DescriptorType> buffer_types,
                size_t push_constants_size,
                const char* source_code);
  ~ComputeShader();

  // Update descriptors and push-constants, then dispatch x * y * z workgroups.
  // |push_constants| must point to data of the size passed to the ComputeShader
  // constructor, or nullptr if that size was 0.
  //
  // TODO: avoid reffing/unreffing textures and buffers.
  void Dispatch(std::vector<TexturePtr> textures,
                std::vector<BufferPtr> buffers,
                CommandBuffer* command_buffer,
                uint32_t x,
                uint32_t y,
                uint32_t z,
                const void* push_constants);

 private:
  const vk::Device device_;
  const std::vector<vk::DescriptorSetLayoutBinding>
      descriptor_set_layout_bindings_;
  const vk::DescriptorSetLayoutCreateInfo descriptor_set_layout_create_info_;
  const uint32_t push_constants_size_;
  DescriptorSetPool pool_;
  const PipelinePtr pipeline_;
  std::vector<vk::WriteDescriptorSet> descriptor_set_writes_;
  std::vector<vk::DescriptorImageInfo> descriptor_image_info_;
  std::vector<vk::DescriptorBufferInfo> descriptor_buffer_info_;
};

}  // namespace impl
}  // namespace escher

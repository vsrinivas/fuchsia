// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// DEPRECATED!! (ES-194) Avoid using in new code.

#ifndef SRC_UI_LIB_ESCHER_IMPL_COMPUTE_SHADER_H_
#define SRC_UI_LIB_ESCHER_IMPL_COMPUTE_SHADER_H_

#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/impl/descriptor_set_pool.h"

namespace escher {
namespace impl {

class GlslToSpirvCompiler;

// Range within the buffer.
struct BufferRange {
  vk::DeviceSize offset;
  vk::DeviceSize size;

  BufferRange(vk::DeviceSize _offset, vk::DeviceSize _size) : offset(_offset), size(_size) {}
};

// Simplifies the creation and use of Vulkan compute pipelines.  The current
// implementation is limited to using images, storage/uniform buffers, and
// push-constants for in/output.
class ComputeShader {
 public:
#if ESCHER_USE_RUNTIME_GLSL
  ComputeShader(EscherWeakPtr escher, const std::vector<vk::ImageLayout>& layouts,
                const std::vector<vk::DescriptorType>& buffer_types, size_t push_constants_size,
                const char* source_code);
#else
  ComputeShader(EscherWeakPtr escher, const std::vector<vk::ImageLayout>& layouts,
                const std::vector<vk::DescriptorType>& buffer_types, size_t push_constants_size,
                std::vector<uint32_t> spir_v);
#endif
  ~ComputeShader();

  // Update descriptors and push-constants, then dispatch x * y * z workgroups.
  // |push_constants| must point to data of the size passed to the ComputeShader
  // constructor, or nullptr if that size was 0.
  void Dispatch(const std::vector<TexturePtr>& textures, const std::vector<BufferPtr>& buffers,
                impl::CommandBuffer* command_buffer, uint32_t x, uint32_t y, uint32_t z,
                const void* push_constants);

  // TODO(ES-45): Implement a ComputeShaderDispatcher that follows builder
  // pattern and take the necessary arguments only.
  void DispatchWithRanges(const std::vector<TexturePtr>& textures,
                          const std::vector<BufferPtr>& buffers,
                          const std::vector<BufferRange>& buffer_ranges,
                          impl::CommandBuffer* command_buffer, uint32_t x, uint32_t y, uint32_t z,
                          const void* push_constants);

 private:
  // Construction helper function.
  void Initialize(const std::vector<vk::ImageLayout>& layouts,
                  const std::vector<vk::DescriptorType>& buffer_types, size_t push_constants_size);

  const vk::Device device_;
  const std::vector<vk::DescriptorSetLayoutBinding> descriptor_set_layout_bindings_;
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

#endif  // SRC_UI_LIB_ESCHER_IMPL_COMPUTE_SHADER_H_

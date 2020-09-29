// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/impl/model_data.h"

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/impl/command_buffer.h"
#include "src/ui/lib/escher/impl/mesh_shader_binding.h"
#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/vk/gpu_allocator.h"

namespace escher {
namespace impl {

// DescriptorSetPools allocate new sets as necessary, so these are no big
// deal.
constexpr uint32_t kInitialPerModelDescriptorSetCount = 50;
constexpr uint32_t kInitialPerObjectDescriptorSetCount = 200;

ModelData::ModelData(EscherWeakPtr escher, GpuAllocator* allocator)
    : device_(escher->vulkan_context().device),
      // This is a 1-deep pool because it was this way before UniformBufferPool
      // started to defer making buffers available for a number of frames.  The
      // reason why this works (i.e. why the data in the buffer doesn't get
      // stomped by the next frame while it is still being rendered) is because
      // ModelDisplayListBuilder adds all resources to the ModelDisplayList,
      // so they aren't returned to the pool until the frame is finished
      // rendering.
      //
      // Furthermore, if this is deeper that 1, the buffers would never be
      // recycled because nobody calls BeginFrame() on this pool.  In the future
      // we'll likely move to an Escher-wide UniformBufferPool.
      uniform_buffer_pool_(escher, 1, allocator) {}

ModelData::~ModelData() {}

const MeshShaderBinding& ModelData::GetMeshShaderBinding(MeshSpec spec) {
  auto ptr = mesh_shader_binding_cache_[spec].get();
  if (ptr) {
    return *ptr;
  }
  FX_DCHECK(spec.IsValidOneBufferMesh());

  std::vector<vk::VertexInputAttributeDescription> attributes;

  uint32_t stride = 0;
  if (spec.has_attribute(0, MeshAttribute::kPosition2D)) {
    vk::VertexInputAttributeDescription attribute;
    attribute.location = kPositionAttributeLocation;
    attribute.binding = 0;
    attribute.format = vk::Format::eR32G32Sfloat;
    attribute.offset = stride;

    stride += sizeof(vec2);
    attributes.push_back(attribute);
  }
  if (spec.has_attribute(0, MeshAttribute::kPosition3D)) {
    vk::VertexInputAttributeDescription attribute;
    attribute.location = kPositionAttributeLocation;
    attribute.binding = 0;
    attribute.format = vk::Format::eR32G32B32Sfloat;
    attribute.offset = stride;

    stride += sizeof(vec3);
    attributes.push_back(attribute);
  }
  if (spec.has_attribute(0, MeshAttribute::kPositionOffset)) {
    vk::VertexInputAttributeDescription attribute;
    attribute.location = kPositionOffsetAttributeLocation;
    attribute.binding = 0;
    attribute.format = vk::Format::eR32G32Sfloat;
    attribute.offset = stride;

    stride += sizeof(vec2);
    attributes.push_back(attribute);
  }
  if (spec.has_attribute(0, MeshAttribute::kUV)) {
    vk::VertexInputAttributeDescription attribute;
    attribute.location = kUVAttributeLocation;
    attribute.binding = 0;
    attribute.format = vk::Format::eR32G32Sfloat;
    attribute.offset = stride;

    stride += sizeof(vec2);
    attributes.push_back(attribute);
  }
  if (spec.has_attribute(0, MeshAttribute::kPerimeterPos)) {
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

  auto msb = std::make_unique<MeshShaderBinding>(std::move(binding), std::move(attributes));
  ptr = msb.get();
  mesh_shader_binding_cache_[spec] = std::move(msb);
  return *ptr;
}

}  // namespace impl
}  // namespace escher

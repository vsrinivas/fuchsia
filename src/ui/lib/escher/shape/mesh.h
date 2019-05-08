// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_SHAPE_MESH_H_
#define SRC_UI_LIB_ESCHER_SHAPE_MESH_H_

#include <map>

#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/geometry/bounding_box.h"
#include "src/ui/lib/escher/resources/waitable_resource.h"
#include "src/ui/lib/escher/shape/mesh_spec.h"

namespace escher {

// Immutable container for vertex indices and attribute data required to render
// a triangle mesh.
class Mesh : public WaitableResource {
 public:
  static const ResourceTypeInfo kTypeInfo;
  const ResourceTypeInfo& type_info() const override { return kTypeInfo; }

  Mesh(ResourceRecycler* resource_recycler, MeshSpec spec,
       BoundingBox bounding_box, uint32_t num_vertices, uint32_t num_indices,
       BufferPtr primary_attribute_buffer, BufferPtr index_buffer,
       vk::DeviceSize primary_attribute_buffer_offset = 0,
       vk::DeviceSize index_buffer_offset = 0);

  Mesh(ResourceRecycler* resource_recycler, MeshSpec spec,
       BoundingBox bounding_box, uint32_t num_indices, BufferPtr index_buffer,
       vk::DeviceSize index_buffer_offset, uint32_t num_vertices,
       BufferPtr attribute_buffer0, vk::DeviceSize attribute_buffer0_offset,
       BufferPtr attribute_buffer1 = nullptr,
       vk::DeviceSize attribute_buffer1_offset = 0,
       BufferPtr attribute_buffer2 = nullptr,
       vk::DeviceSize attribute_buffer2_offset = 0,
       BufferPtr attribute_buffer3 = nullptr,
       vk::DeviceSize attribute_buffer3_offset = 0);

  ~Mesh() override;

  const MeshSpec& spec() const { return spec_; }
  const BoundingBox& bounding_box() const { return bounding_box_; }

  // Number of indices in the mesh's index buffer, equal to the number of
  // triangles divided by 3.
  uint32_t num_indices() const { return num_indices_; }

  // Number of distinct vertices that are present in the mesh.
  uint32_t num_vertices() const { return num_vertices_; }

  const BufferPtr& index_buffer() const { return index_buffer_; }
  vk::Buffer vk_index_buffer() const { return vk_index_buffer_; }
  vk::DeviceSize index_buffer_offset() const { return index_buffer_offset_; }

  struct AttributeBuffer {
    vk::Buffer vk_buffer;
    BufferPtr buffer;
    vk::DeviceSize offset;
    uint32_t stride;

    explicit operator bool() const { return buffer.get() != nullptr; }
  };
  using AttributeBufferArray =
      std::array<AttributeBuffer, VulkanLimits::kNumVertexBuffers>;

  const AttributeBuffer& attribute_buffer(size_t buffer_index) const {
    return attribute_buffers_[buffer_index];
  }

  const AttributeBufferArray& attribute_buffers() const {
    return attribute_buffers_;
  }

 private:
  Mesh(ResourceRecycler* resource_recycler, MeshSpec spec,
       BoundingBox bounding_box, uint32_t num_vertices, uint32_t num_indices,
       std::array<AttributeBuffer, VulkanLimits::kNumVertexBuffers>
           attribute_buffers,
       BufferPtr index_buffer, vk::DeviceSize index_buffer_offset);

  const MeshSpec spec_;
  const BoundingBox bounding_box_;
  const uint32_t num_vertices_;
  const uint32_t num_indices_;
  AttributeBufferArray attribute_buffers_;
  const vk::Buffer vk_index_buffer_;
  const BufferPtr index_buffer_;
  const vk::DeviceSize index_buffer_offset_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Mesh);
};

typedef fxl::RefPtr<Mesh> MeshPtr;

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_SHAPE_MESH_H_

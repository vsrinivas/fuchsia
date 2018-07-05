// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_SHAPE_MESH_H_
#define LIB_ESCHER_SHAPE_MESH_H_

#include <map>

#include "lib/escher/forward_declarations.h"
#include "lib/escher/geometry/bounding_box.h"
#include "lib/escher/resources/waitable_resource.h"
#include "lib/escher/shape/mesh_spec.h"

namespace escher {

// Immutable container for vertex indices and attribute data required to render
// a triangle mesh.
class Mesh : public WaitableResource {
 public:
  static const ResourceTypeInfo kTypeInfo;
  const ResourceTypeInfo& type_info() const override { return kTypeInfo; }

  Mesh(ResourceRecycler* resource_recycler, MeshSpec spec,
       BoundingBox bounding_box, uint32_t num_vertices, uint32_t num_indices,
       BufferPtr vertex_buffer, BufferPtr index_buffer,
       vk::DeviceSize vertex_buffer_offset = 0,
       vk::DeviceSize index_buffer_offset = 0);

  ~Mesh() override;

  const MeshSpec& spec() const { return spec_; }
  const BoundingBox& bounding_box() const { return bounding_box_; }
  uint32_t num_vertices() const { return num_vertices_; }
  uint32_t num_indices() const { return num_indices_; }
  vk::Buffer vk_vertex_buffer() const { return vk_vertex_buffer_; }
  vk::Buffer vk_index_buffer() const { return vk_index_buffer_; }
  const BufferPtr& vertex_buffer() const { return vertex_buffer_; }
  const BufferPtr& index_buffer() const { return index_buffer_; }
  vk::DeviceSize vertex_buffer_offset() const { return vertex_buffer_offset_; }
  vk::DeviceSize index_buffer_offset() const { return index_buffer_offset_; }

 private:
  const MeshSpec spec_;
  const BoundingBox bounding_box_;
  const uint32_t num_vertices_;
  const uint32_t num_indices_;
  const vk::Buffer vk_vertex_buffer_;
  const vk::Buffer vk_index_buffer_;
  const BufferPtr vertex_buffer_;
  const BufferPtr index_buffer_;
  const vk::DeviceSize vertex_buffer_offset_;
  const vk::DeviceSize index_buffer_offset_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Mesh);
};

typedef fxl::RefPtr<Mesh> MeshPtr;

}  // namespace escher

#endif  // LIB_ESCHER_SHAPE_MESH_H_

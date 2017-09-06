// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/shape/mesh.h"
#include "escher/resources/resource_recycler.h"
#include "escher/vk/buffer.h"

namespace escher {

const ResourceTypeInfo Mesh::kTypeInfo("Mesh",
                                       ResourceType::kResource,
                                       ResourceType::kWaitableResource,
                                       ResourceType::kMesh);

Mesh::Mesh(ResourceRecycler* resource_recycler,
           MeshSpec spec,
           BoundingBox bounding_box,
           uint32_t num_vertices,
           uint32_t num_indices,
           BufferPtr vertex_buffer,
           BufferPtr index_buffer,
           vk::DeviceSize vertex_buffer_offset,
           vk::DeviceSize index_buffer_offset)
    : WaitableResource(resource_recycler),
      spec_(std::move(spec)),
      bounding_box_(bounding_box),
      num_vertices_(num_vertices),
      num_indices_(num_indices),
      vk_vertex_buffer_(vertex_buffer->get()),
      vk_index_buffer_(index_buffer->get()),
      vertex_buffer_(std::move(vertex_buffer)),
      index_buffer_(std::move(index_buffer)),
      vertex_buffer_offset_(vertex_buffer_offset),
      index_buffer_offset_(index_buffer_offset) {
  FTL_DCHECK(num_vertices_ * spec_.GetStride() + vertex_buffer_offset_ <=
             vertex_buffer_->size());
  FTL_DCHECK(num_indices_ * sizeof(uint32_t) + index_buffer_offset_ <=
             index_buffer_->size());
}

Mesh::~Mesh() {}

}  // namespace escher

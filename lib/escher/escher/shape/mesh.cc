// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/shape/mesh.h"

#include "escher/impl/escher_impl.h"
#include "escher/resources/resource_life_preserver.h"
#include "escher/vk/buffer.h"

namespace escher {

const ResourceTypeInfo Mesh::kTypeInfo("Mesh",
                                       ResourceType::kResource,
                                       ResourceType::kWaitableResource,
                                       ResourceType::kMesh);

Mesh::Mesh(ResourceLifePreserver* life_preserver,
           MeshSpec spec,
           uint32_t num_vertices,
           uint32_t num_indices,
           BufferPtr vertex_buffer,
           BufferPtr index_buffer,
           vk::DeviceSize vertex_buffer_offset,
           vk::DeviceSize index_buffer_offset)
    : WaitableResource(life_preserver),
      spec_(std::move(spec)),
      num_vertices_(num_vertices),
      num_indices_(num_indices),
      vertex_buffer_(vertex_buffer->get()),
      index_buffer_(index_buffer->get()),
      vertex_buffer_ptr_(std::move(vertex_buffer)),
      index_buffer_ptr_(std::move(index_buffer)),
      vertex_buffer_offset_(vertex_buffer_offset),
      index_buffer_offset_(index_buffer_offset) {
  FTL_DCHECK(num_vertices_ * spec_.GetStride() + vertex_buffer_offset_ <=
             vertex_buffer_ptr_->size());
  FTL_DCHECK(num_indices_ * sizeof(uint32_t) + index_buffer_offset_ <=
             index_buffer_ptr_->size());
}

Mesh::~Mesh() {}

}  // namespace escher

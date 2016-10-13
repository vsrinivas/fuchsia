// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/impl/buffer.h"
#include "escher/shape/mesh.h"

namespace escher {
namespace impl {
class MeshManager;

struct MeshSpecImpl {
  vk::VertexInputBindingDescription binding;
  std::vector<vk::VertexInputAttributeDescription> attributes;
};

class MeshImpl : public Mesh {
 public:
  MeshImpl(MeshSpec spec,
           uint32_t num_vertices,
           uint32_t num_indices,
           MeshManager* manager,
           Buffer vertex_buffer,
           Buffer index_buffer,
           const MeshSpecImpl& spec_impl);
  ~MeshImpl();

  vk::Buffer vertex_buffer() const { return vertex_buffer_.buffer(); }
  vk::Buffer index_buffer() const { return index_buffer_.buffer(); }

  // TODO: in the future, some or all of these may not be zero.
  vk::DeviceSize index_buffer_offset() const { return 0; }
  vk::DeviceSize vertex_buffer_offset() const { return 0; }

  uint32_t vertex_buffer_binding() const { return spec_impl_.binding.binding; }

 private:
  MeshManager* manager_;
  Buffer vertex_buffer_;
  Buffer index_buffer_;
  const MeshSpecImpl& spec_impl_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MeshImpl);
};

}  // namespace impl
}  // namespace escher

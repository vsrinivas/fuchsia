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

  // We need to explicitly define a default constructor, because of
  // FTL_DISALLOW_COPY_AND_ASSIGN below.
  MeshSpecImpl() {}

 private:
  // The binding contains raw pointers into the vector of attributes,
  // which would no longer be valid if a MeshSpecImpl is copied, and the
  // original destroyed.
  FTL_DISALLOW_COPY_AND_ASSIGN(MeshSpecImpl);
};

class MeshImpl : public Mesh {
 public:
  // Vertex attribute locations corresponding to the flags in MeshSpec.
  static constexpr uint32_t kPositionAttributeLocation = 0;
  static constexpr uint32_t kPositionOffsetAttributeLocation = 1;
  static constexpr uint32_t kUVAttributeLocation = 2;
  static constexpr uint32_t kPerimeterAttributeLocation = 3;

  // spec_impl continues to be referenced by the MeshImpl... it MUST outlive the
  // MeshImpl.
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

  const impl::MeshSpecImpl& spec_impl() const override { return spec_impl_; }

 private:
  MeshManager* manager_;
  Buffer vertex_buffer_;
  Buffer index_buffer_;
  const MeshSpecImpl& spec_impl_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MeshImpl);
};

}  // namespace impl
}  // namespace escher

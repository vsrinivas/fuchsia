// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/impl/buffer.h"
#include "escher/shape/mesh.h"

namespace escher {
namespace impl {
class MeshManager;

class MeshImpl : public Mesh {
 public:
  MeshImpl(MeshSpec spec,
           uint32_t num_vertices,
           uint32_t num_indices,
           MeshManager* manager,
           Buffer vertex_buffer,
           Buffer index_buffer,
           vk::Semaphore mesh_ready_semaphore);
  ~MeshImpl();

  // Bind the Mesh's vertex/index buffers and issue a drawIndexed() command.
  // If the mesh contents depend on some previously submitted command-buffer,
  // return a semaphore that should be waited upon (otherwise return nullptr).
  vk::Semaphore Draw(vk::CommandBuffer command_buffer, uint64_t frame_number);

 private:
  MeshManager* manager_;
  Buffer vertex_buffer_;
  Buffer index_buffer_;
  vk::Semaphore mesh_ready_semaphore_;
  uint64_t last_rendered_frame_ = 0;

  FTL_DISALLOW_COPY_AND_ASSIGN(MeshImpl);
};

}  // namespace impl
}  // namespace escher

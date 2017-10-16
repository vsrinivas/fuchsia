// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>
#include "garnet/bin/ui/sketchy/buffer.h"
#include "lib/escher/geometry/bounding_box.h"
#include "lib/escher/renderer/semaphore_wait.h"
#include "lib/escher/scene/shape_modifier.h"
#include "lib/escher/vk/buffer.h"
#include "zircon/system/ulib/zx/include/zx/event.h"

namespace sketchy_service {

// Manages the buffers and semaphores of the mesh for double buffering.
class MeshBuffer final {
 public:
  MeshBuffer(scenic_lib::Session* session,
             escher::BufferFactory* buffer_factory);
  // TODO: Return offsets instead when ComputeShader supports offset.
  std::pair<escher::BufferPtr, escher::BufferPtr> Preserve(
      escher::impl::CommandBuffer* command, escher::BufferFactory* factory,
      uint32_t vertex_count, uint32_t index_count,
      const escher::BoundingBox& bbox);
  void ProvideBuffersToScenicMesh(scenic_lib::Mesh* scenic_mesh);
  // Resets |vertex_count_|, |index_count_|, |bbox_|. Vertex and index
  // buffer won't be touched.
  void Reset();

  uint32_t vertex_count() const { return vertex_count_; }
  const Buffer* vertex_buffer() const { return vertex_buffer_.get(); }
  const Buffer* index_buffer() const { return index_buffer_.get(); }

 private:
  friend class Stroke;

  std::unique_ptr<Buffer> vertex_buffer_;
  std::unique_ptr<Buffer> index_buffer_;
  uint32_t vertex_count_ = 0;
  uint32_t index_count_ = 0;
  escher::BoundingBox bbox_;
};

}  // namespace sketchy_service

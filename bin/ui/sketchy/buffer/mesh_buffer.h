// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>
#include "garnet/bin/ui/sketchy/buffer/shared_buffer.h"
#include "garnet/bin/ui/sketchy/frame.h"
#include "lib/escher/geometry/bounding_box.h"
#include "lib/escher/renderer/semaphore_wait.h"
#include "lib/escher/scene/shape_modifier.h"
#include "lib/escher/vk/buffer.h"
#include "zircon/system/ulib/zx/include/zx/event.h"

namespace sketchy_service {

// Manages the buffers and semaphores of the mesh for multi-buffering.
class MeshBuffer final {
 public:
  // Prepare the current mesh buffer given the delta vertex/index counts. If
  // the current capacity is not enough, a new buffer will be grabbed from the
  // pool, and the original content will be copied to the new one. This is
  // MUST be called for multi-buffering purpose. Delta vertex/index count is
  // more of optimization; they won't affect correctness.
  void Prepare(Frame* frame,
               bool from_scratch,
               uint32_t delta_vertex_count = 0,
               uint32_t delta_index_count = 0);

  // Preserve a pair of vertex/index buffer for use. The underlying buffers will
  // grow dynamically if it's not enough.
  // TODO(ES-45): Return ranges to avoid sub-buffering.
  std::pair<escher::BufferPtr, escher::BufferPtr> Preserve(
      Frame* frame, uint32_t vertex_count, uint32_t index_count,
      const escher::BoundingBox& bbox);

  // Provide all the necessary parameters to scenic::Mesh::BindBuffers().
  void ProvideBuffersToScenicMesh(scenic_lib::Mesh* scenic_mesh);

  uint32_t vertex_count() const { return vertex_count_; }

 private:
  // Replace the buffer with one that is large enough for |capacity_req|. If
  // |keep_content| is true, the original content will be copied. A fence
  // listener will be implicitly setup to monitor the scenic release event.
  void ReplaceBuffer(Frame* frame,
                     SharedBufferPtr& shared_buffer,
                     vk::DeviceSize capacity_req,
                     bool keep_content);

  SharedBufferPtr vertex_buffer_;
  SharedBufferPtr index_buffer_;
  uint32_t vertex_count_ = 0;
  uint32_t index_count_ = 0;
  escher::BoundingBox bbox_;
};

}  // namespace sketchy_service

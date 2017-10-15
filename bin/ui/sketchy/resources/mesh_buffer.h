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
  void ProvideBuffersToScenicMesh(scenic_lib::Mesh* scenic_mesh);

 private:
  friend class Stroke;

  std::unique_ptr<Buffer> vertex_buffer_;
  std::unique_ptr<Buffer> index_buffer_;
  uint32_t num_vertices_ = 0;
  uint32_t num_indices_ = 0;
  escher::BoundingBox bounding_box_;
};

}  // namespace sketchy_service

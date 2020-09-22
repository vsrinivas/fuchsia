// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>
#include <memory>

#include "lib/ui/scenic/cpp/commands.h"
#include "lib/ui/scenic/cpp/resources.h"

using scenic::Buffer;
using scenic::Memory;
using scenic::Mesh;
using scenic::Session;

namespace scenic_util {

std::unique_ptr<Mesh> NewMeshWithVertices(Session* session, const std::vector<float>& vertices,
                                          const std::vector<uint32_t>& indices) {
  auto mesh = std::make_unique<Mesh>(session);

  ZX_DEBUG_ASSERT(vertices.size() % 3 == 0);
  const uint64_t num_vertices = vertices.size() / 3;

  ZX_DEBUG_ASSERT(*std::max_element(indices.begin(), indices.end()) < num_vertices);

  // TODO(fxbug.dev/23785) vertex_elements should be 3.
  uint64_t vertex_elements = 5;

  uint64_t vertex_size = vertex_elements * sizeof(float);
  uint64_t vertex_buffer_size = num_vertices * vertex_size;
  uint64_t index_buffer_size = indices.size() * sizeof(uint32_t);

  // TODO(fxbug.dev/23785) This whole block is a workaround and should be removed.
  std::vector<float> vertices_scn_558;
  vertices_scn_558.reserve(vertex_buffer_size / sizeof(float));
  for (uint64_t i = 0; i < num_vertices; i++) {
    const float* vertex_in = vertices.data() + i * 3;
    float* vertex_out = vertices_scn_558.data() + i * vertex_elements;
    vertex_out[0] = vertex_in[0];
    vertex_out[1] = vertex_in[1];
    vertex_out[2] = vertex_in[2];
    vertex_out[3] = 0;
    vertex_out[4] = 0;
  }
  // END TODO(fxbug.dev/23785)

  // Copy data to VMO and create buffers
  uint64_t vmo_size = vertex_buffer_size + index_buffer_size;
  zx::vmo vmo;
  zx_status_t status;
  status = zx::vmo::create(vmo_size, 0u, &vmo);

  // TODO(fxbug.dev/23785) uses vertices.data()
  status = vmo.write(vertices_scn_558.data(), 0, vertex_buffer_size);
  ZX_DEBUG_ASSERT(status == ZX_OK);
  status = vmo.write(indices.data(), vertex_buffer_size, index_buffer_size);
  ZX_DEBUG_ASSERT(status == ZX_OK);

  Memory mem(session, std::move(vmo), vmo_size, fuchsia::images::MemoryType::VK_DEVICE_MEMORY);
  Buffer vertex_buffer(mem, 0, vertex_buffer_size);
  Buffer index_buffer(mem, vertex_buffer_size, index_buffer_size);

  auto vertex_format = scenic::NewMeshVertexFormat(fuchsia::ui::gfx::ValueType::kVector3,
                                                   fuchsia::ui::gfx::ValueType::kNone,
                                                   fuchsia::ui::gfx::ValueType::kVector2);

  // Compute bounding box.
  std::array<float, 3> bounding_box_min;
  std::array<float, 3> bounding_box_max;
  for (int i = 0; i < 3; i++) {
    bounding_box_min[i] = std::numeric_limits<float>::max();
    bounding_box_max[i] = std::numeric_limits<float>::min();
  }

  for (uint64_t i = 0; i < num_vertices; i++) {
    const float* vertex = vertices.data() + i * 3;
    bounding_box_min[0] = std::min(bounding_box_min[0], vertex[0]);
    bounding_box_min[1] = std::min(bounding_box_min[1], vertex[1]);
    bounding_box_min[2] = std::min(bounding_box_min[2], vertex[2]);

    bounding_box_max[0] = std::max(bounding_box_max[0], vertex[0]);
    bounding_box_max[1] = std::max(bounding_box_max[1], vertex[1]);
    bounding_box_max[2] = std::max(bounding_box_max[2], vertex[2]);
  }

  // Bind buffers.
  mesh->BindBuffers(index_buffer, fuchsia::ui::gfx::MeshIndexFormat::kUint32, 0, indices.size(),
                    vertex_buffer, std::move(vertex_format), 0, num_vertices,
                    std::move(bounding_box_min), std::move(bounding_box_max));
  return mesh;
}

}  // namespace scenic_util

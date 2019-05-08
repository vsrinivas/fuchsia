// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_GEOMETRY_INDEXED_TRIANGLE_MESH_UPLOAD_H_
#define SRC_UI_LIB_ESCHER_GEOMETRY_INDEXED_TRIANGLE_MESH_UPLOAD_H_

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/geometry/bounding_box.h"
#include "src/ui/lib/escher/geometry/indexed_triangle_mesh.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/shape/mesh.h"
#include "src/ui/lib/escher/util/trace_macros.h"

namespace escher {

// Uploads the contents of an IndexedTriangleMesh<> to a Vulkan buffer, and
// returns a new Mesh that is bound to this buffer.
template <typename IndexedTriangleMeshT>
MeshPtr IndexedTriangleMeshUpload(Escher* escher, BatchGpuUploader* uploader,
                                  const MeshSpec& mesh_spec,
                                  const BoundingBox& bounding_box,
                                  const IndexedTriangleMeshT& mesh) {
  TRACE_DURATION("gfx", "escher::IndexedTriangleMeshUpload", "triangles",
                 mesh.triangle_count(), "vertices", mesh.vertex_count());
  if (mesh.index_count() == 0)
    return MeshPtr();

  const size_t ind_bytes = mesh.total_index_bytes();
  const size_t pos_bytes = mesh.total_position_bytes();
  const size_t attr1_bytes = mesh.total_attribute1_bytes();
  const size_t attr2_bytes = mesh.total_attribute2_bytes();
  const size_t attr3_bytes = mesh.total_attribute3_bytes();
  const size_t total_bytes =
      ind_bytes + pos_bytes + attr1_bytes + attr2_bytes + attr3_bytes;

  const size_t ind_offset = 0;
  const size_t pos_offset = ind_bytes;
  const size_t attr1_offset = pos_offset + pos_bytes;
  const size_t attr2_offset = attr1_offset + attr1_bytes;
  const size_t attr3_offset = attr2_offset + attr2_bytes;

  // Use a single buffer, but don't interleave the position and attribute
  // data.
  auto buffer = escher->NewBuffer(total_bytes,
                                  vk::BufferUsageFlagBits::eIndexBuffer |
                                      vk::BufferUsageFlagBits::eVertexBuffer |
                                      vk::BufferUsageFlagBits::eTransferDst,
                                  vk::MemoryPropertyFlagBits::eDeviceLocal);
  auto writer = uploader->AcquireWriter(total_bytes);
  {
    TRACE_DURATION("gfx", "escher::IndexedTriangleMeshUpload[memcpy]");
    uint8_t* base = writer->host_ptr();
    memcpy(base + ind_offset, mesh.indices.data(), ind_bytes);
    memcpy(base + pos_offset, mesh.positions.data(), pos_bytes);
    if (attr1_bytes > 0) {
      memcpy(base + attr1_offset, mesh.attributes1.data(), attr1_bytes);
    }
    if (attr2_bytes > 0) {
      memcpy(base + attr2_offset, mesh.attributes2.data(), attr2_bytes);
    }
    if (attr3_bytes > 0) {
      memcpy(base + attr3_offset, mesh.attributes3.data(), attr3_bytes);
    }
  }
  writer->WriteBuffer(buffer, {0, 0, total_bytes});
  uploader->PostWriter(std::move(writer));

  return fxl::MakeRefCounted<Mesh>(
      escher->resource_recycler(), mesh_spec, bounding_box, mesh.index_count(),
      buffer, ind_offset, mesh.vertex_count(), buffer, pos_offset,
      (attr1_bytes ? buffer : nullptr), attr1_offset,
      (attr2_bytes ? buffer : nullptr), attr2_offset,
      (attr3_bytes ? buffer : nullptr), attr3_offset);
}

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_GEOMETRY_INDEXED_TRIANGLE_MESH_UPLOAD_H_

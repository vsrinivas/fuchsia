// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_MESH_INDEXED_TRIANGLE_MESH_UPLOAD_H_
#define SRC_UI_LIB_ESCHER_MESH_INDEXED_TRIANGLE_MESH_UPLOAD_H_

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/geometry/bounding_box.h"
#include "src/ui/lib/escher/mesh/indexed_triangle_mesh.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/shape/mesh.h"
#include "src/ui/lib/escher/util/trace_macros.h"

namespace escher {

// Uploads the contents of an IndexedTriangleMesh<> to a Vulkan buffer, and
// returns a new Mesh that is bound to this buffer.
template <typename IndexedTriangleMeshT>
MeshPtr IndexedTriangleMeshUpload(Escher* escher, BatchGpuUploader* uploader,
                                  const MeshSpec& mesh_spec, const BoundingBox& bounding_box,
                                  IndexedTriangleMeshT mesh) {
  TRACE_DURATION("gfx", "escher::IndexedTriangleMeshUpload", "triangles", mesh.triangle_count(),
                 "vertices", mesh.vertex_count());
  if (mesh.index_count() == 0)
    return MeshPtr();

  const size_t ind_bytes = mesh.total_index_bytes();
  const size_t pos_bytes = mesh.total_position_bytes();
  const size_t attr1_bytes = mesh.total_attribute1_bytes();
  const size_t attr2_bytes = mesh.total_attribute2_bytes();
  const size_t attr3_bytes = mesh.total_attribute3_bytes();
  const size_t total_bytes = ind_bytes + pos_bytes + attr1_bytes + attr2_bytes + attr3_bytes;

  const size_t ind_offset = 0;
  const size_t pos_offset = ind_bytes;
  const size_t attr1_offset = pos_offset + pos_bytes;
  const size_t attr2_offset = attr1_offset + attr1_bytes;
  const size_t attr3_offset = attr2_offset + attr2_bytes;

  auto vertex_count = mesh.vertex_count();
  auto index_count = mesh.index_count();

  // Use a single buffer, but don't interleave the position and attribute
  // data.
  auto buffer = escher->NewBuffer(
      total_bytes,
      // |eTransferSrc| needed for glTF Exporter.
      vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eIndexBuffer |
          vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
      vk::MemoryPropertyFlagBits::eDeviceLocal);

  uploader->ScheduleWriteBuffer(
      buffer,
      [ind_bytes, pos_bytes, pos_offset, attr1_offset, attr2_offset, attr3_offset, attr1_bytes,
       attr2_bytes, attr3_bytes,
       mesh = std::move(mesh)](uint8_t* host_buffer_ptr, size_t copy_size) {
        TRACE_DURATION("gfx", "escher::IndexedTriangleMeshUpload[memcpy]");
        memcpy(host_buffer_ptr + ind_offset, mesh.indices.data(), ind_bytes);
        memcpy(host_buffer_ptr + pos_offset, mesh.positions.data(), pos_bytes);
        if (attr1_bytes > 0) {
          memcpy(host_buffer_ptr + attr1_offset, mesh.attributes1.data(), attr1_bytes);
        }
        if (attr2_bytes > 0) {
          memcpy(host_buffer_ptr + attr2_offset, mesh.attributes2.data(), attr2_bytes);
        }
        if (attr3_bytes > 0) {
          memcpy(host_buffer_ptr + attr3_offset, mesh.attributes3.data(), attr3_bytes);
        }
      },
      /* target_offset */ 0, /* copy_size */ total_bytes);

  return fxl::MakeRefCounted<Mesh>(escher->resource_recycler(), mesh_spec, bounding_box,
                                   index_count, buffer, ind_offset, vertex_count, buffer,
                                   pos_offset, (attr1_bytes ? buffer : nullptr), attr1_offset,
                                   (attr2_bytes ? buffer : nullptr), attr2_offset,
                                   (attr3_bytes ? buffer : nullptr), attr3_offset);
}

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_MESH_INDEXED_TRIANGLE_MESH_UPLOAD_H_

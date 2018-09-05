// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_GEOMETRY_INDEXED_TRIANGLE_MESH_UPLOAD_H_
#define LIB_ESCHER_GEOMETRY_INDEXED_TRIANGLE_MESH_UPLOAD_H_

#include "lib/escher/escher.h"
#include "lib/escher/geometry/bounding_box.h"
#include "lib/escher/geometry/indexed_triangle_mesh.h"
#include "lib/escher/renderer/batch_gpu_uploader.h"
#include "lib/escher/shape/mesh.h"
#include "lib/escher/util/trace_macros.h"

namespace escher {

template <typename PositionT, typename AttributeT>
MeshPtr IndexedTriangleMeshUpload(
    Escher* escher, BatchGpuUploader* uploader, const MeshSpec& mesh_spec,
    const BoundingBox& bounding_box,
    const IndexedTriangleMesh<PositionT, AttributeT>& mesh) {
  TRACE_DURATION("gfx", "escher::IndexedTriangleMeshUpload", "triangles",
                 mesh.triangle_count(), "vertices", mesh.vertex_count());
  if (mesh.index_count() == 0)
    return MeshPtr();

  const PositionT* pos_ptr = mesh.positions.data();
  const AttributeT* attr_ptr = mesh.attributes.data();
  const MeshSpec::IndexType* ind_ptr = mesh.indices.data();

  const size_t pos_bytes = mesh.total_position_bytes();
  const size_t attr_bytes = mesh.total_attribute_bytes();
  const size_t ind_num_bytes = mesh.total_index_bytes();
  const size_t total_num_bytes = pos_bytes + attr_bytes + ind_num_bytes;

  // Use a single buffer, but don't interleave the position and attribute
  // data.
  auto buffer = escher->NewBuffer(total_num_bytes,
                                  vk::BufferUsageFlagBits::eIndexBuffer |
                                      vk::BufferUsageFlagBits::eVertexBuffer |
                                      vk::BufferUsageFlagBits::eTransferDst,
                                  vk::MemoryPropertyFlagBits::eDeviceLocal);
  auto writer = uploader->AcquireWriter(total_num_bytes);
  {
    TRACE_DURATION("gfx", "escher::IndexedTriangleMeshUpload[memcpy]");
    memcpy(writer->host_ptr(), pos_ptr, pos_bytes);
    memcpy(writer->host_ptr() + pos_bytes, attr_ptr, attr_bytes);
    memcpy(writer->host_ptr() + pos_bytes + attr_bytes, ind_ptr, ind_num_bytes);
  }
  writer->WriteBuffer(buffer, {0, 0, total_num_bytes},
                      Semaphore::New(escher->vk_device()));
  uploader->PostWriter(std::move(writer));

  return fxl::MakeRefCounted<Mesh>(escher->resource_recycler(), mesh_spec,
                                   bounding_box, mesh.vertex_count(),
                                   mesh.index_count(), buffer, buffer, buffer,
                                   0, pos_bytes, pos_bytes + attr_bytes);
}

}  // namespace escher

#endif  // LIB_ESCHER_GEOMETRY_INDEXED_TRIANGLE_MESH_UPLOAD_H_

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/sketchy/resources/stroke_group.h"

#include "lib/ui/scenic/fidl_helpers.h"

#include "escher/escher.h"
#include "escher/geometry/tessellation.h"
#include "escher/shape/mesh.h"

namespace {

constexpr vk::DeviceSize kInitVertexBufferSize = 8192;
constexpr vk::DeviceSize kInitIndexBufferSize = 4096;

}  // namespace

namespace sketchy_service {

const ResourceTypeInfo StrokeGroup::kTypeInfo("StrokeGroup",
                                              ResourceType::kStrokeGroup,
                                              ResourceType::kResource);

StrokeGroup::StrokeGroup(scenic_lib::Session* session,
                         escher::BufferFactory* buffer_factory)
    : session_(session),
      shape_node_(session),
      mesh_(session),
      material_(session),
      vertex_buffer_(Buffer::NewVertexBuffer(
          session, buffer_factory, kInitVertexBufferSize)),
      index_buffer_(Buffer::NewIndexBuffer(
          session, buffer_factory, kInitIndexBufferSize)),
      vertex_buffer_offset_(0),
      index_buffer_offset_(0) {
  material_.SetColor(255, 0, 255, 255);
  shape_node_.SetMaterial(material_);
}

bool StrokeGroup::AddStroke(StrokePtr stroke) {
  // TODO(MZ-269): Support more strokes. When this is revisited, none of the
  // rest of the code in the method should be assumed to be valid.
  if (vertex_buffer_offset_ != 0 || index_buffer_offset_ != 0) {
    FXL_LOG(ERROR) << "Premature feature: only one stroke is supported.";
    return false;
  }

  auto escher_mesh = stroke->Tessellate();

  vertex_buffer_ =
      std::make_unique<Buffer>(session_, escher_mesh->vertex_buffer());
  index_buffer_ =
      std::make_unique<Buffer>(session_, escher_mesh->index_buffer());
  vertex_buffer_offset_ = vertex_buffer_->escher_buffer()->size();
  index_buffer_offset_ = index_buffer_->escher_buffer()->size();

  auto bb_min = escher_mesh->bounding_box().min();
  auto bb_max = escher_mesh->bounding_box().max();
  float bb_min_arr[] = {bb_min.x, bb_min.y, bb_min.z};
  float bb_max_arr[] = {bb_max.x, bb_max.y, bb_max.z};

  mesh_.BindBuffers(
      index_buffer_->scenic_buffer(), scenic::MeshIndexFormat::kUint32,
      escher_mesh->index_buffer_offset(), escher_mesh->num_indices(),
      vertex_buffer_->scenic_buffer(),
      scenic_lib::NewMeshVertexFormat(scenic::ValueType::kVector2,
                                      scenic::ValueType::kNone,
                                      scenic::ValueType::kVector2),
      escher_mesh->vertex_buffer_offset(), escher_mesh->num_vertices(),
      bb_min_arr, bb_max_arr);
  shape_node_.SetShape(mesh_);

  return true;
}

}  // namespace sketchy_service

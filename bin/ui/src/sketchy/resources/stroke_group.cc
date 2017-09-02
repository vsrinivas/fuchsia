// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/sketchy/resources/stroke_group.h"

#include "apps/mozart/lib/scenic/fidl_helpers.h"

#include "escher/escher.h"
#include "escher/geometry/tessellation.h"
#include "escher/shape/mesh.h"

namespace sketchy_service {

const ResourceTypeInfo StrokeGroup::kTypeInfo("StrokeGroup",
                                              ResourceType::kStrokeGroup,
                                              ResourceType::kResource);

StrokeGroup::StrokeGroup(scenic_lib::Session* session,
                         escher::BufferFactory* buffer_factory)
    : shape_node_(session), material_(session), mesh_(session) {
  auto escher_mesh = escher::NewRingMesh(
      buffer_factory->escher(),
      {escher::MeshAttribute::kPosition2D | escher::MeshAttribute::kUV}, 4,
      escher::vec2(200, 200), 200, 150);

  index_buffer_ =
      std::make_unique<Buffer>(session, escher_mesh->index_buffer());
  vertex_buffer_ =
      std::make_unique<Buffer>(session, escher_mesh->vertex_buffer());

  float bounding_box_min[] = {0.f, 0.f, 0.f};
  float bounding_box_max[] = {400.f, 400.f, 0.f};

  mesh_.BindBuffers(
      index_buffer_->scenic_buffer(), scenic::MeshIndexFormat::kUint32,
      escher_mesh->index_buffer_offset(), escher_mesh->num_indices(),
      vertex_buffer_->scenic_buffer(),
      scenic_lib::NewMeshVertexFormat(scenic::ValueType::kVector2,
                                      scenic::ValueType::kNone,
                                      scenic::ValueType::kVector2),
      escher_mesh->vertex_buffer_offset(), escher_mesh->num_vertices(),
      bounding_box_min, bounding_box_max);

  // TODO: Remove this. For now it will just add a hard-coded shape node
  // regardless of the request.
  material_.SetColor(255, 0, 255, 255);
  shape_node_.SetShape(mesh_);
  shape_node_.SetMaterial(material_);
}

}  // namespace sketchy_service

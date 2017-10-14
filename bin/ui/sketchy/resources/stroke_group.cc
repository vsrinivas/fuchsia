// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/sketchy/resources/stroke_group.h"

#include "lib/ui/scenic/fidl_helpers.h"

#include "lib/escher/escher.h"
#include "lib/escher/geometry/tessellation.h"
#include "lib/escher/shape/mesh.h"
#include "lib/escher/vk/gpu_mem.h"

namespace {

constexpr vk::DeviceSize kInitVertexBufferSize = 8192;
constexpr vk::DeviceSize kInitIndexBufferSize = 4096;

constexpr auto kMeshVertexPositionType = scenic::ValueType::kVector2;
constexpr auto kMeshVertexNormalType = scenic::ValueType::kNone;
constexpr auto kMeshVertexTexCoodType = scenic::ValueType::kVector2;
constexpr auto kMeshIndexFormat = scenic::MeshIndexFormat::kUint32;

}  // namespace

namespace sketchy_service {

const ResourceTypeInfo StrokeGroup::kTypeInfo("StrokeGroup",
                                              ResourceType::kStrokeGroup,
                                              ResourceType::kResource);

StrokeGroup::StrokeGroup(scenic_lib::Session* session,
                         escher::BufferFactory* buffer_factory)
    : shape_node_(session),
      mesh_(session),
      material_(session),
      vertex_buffer_(Buffer::New(session,
                                 buffer_factory,
                                 BufferType::kVertex,
                                 kInitVertexBufferSize)),
      index_buffer_(Buffer::New(session,
                                buffer_factory,
                                BufferType::kIndex,
                                kInitIndexBufferSize)),
      num_vertices_(0),
      num_indices_(0) {
  material_.SetColor(255, 0, 255, 255);
  shape_node_.SetMaterial(material_);
  shape_node_.SetShape(mesh_);
}

bool StrokeGroup::AddStroke(StrokePtr stroke) {
  if (strokes_to_add_.find(stroke) != strokes_to_add_.end()) {
    FXL_LOG(WARNING) << "Stroke " << stroke.get()
                     << " has already been added to group.";
    return false;
  }
  strokes_to_add_.insert(stroke);
  return true;
}

void StrokeGroup::ApplyChanges(escher::impl::CommandBuffer* command,
                               escher::BufferFactory* buffer_factory) {
  while (!strokes_to_add_.empty()) {
    const auto& stroke = *strokes_to_add_.begin();
    strokes_to_add_.erase(stroke);
    strokes_.insert(stroke);
    stroke->TessellateAndMerge(command, buffer_factory, this);
  }

  auto bb_min = bounding_box_.min();
  auto bb_max = bounding_box_.max();
  float bb_min_arr[] = {bb_min.x, bb_min.y, bb_min.z};
  float bb_max_arr[] = {bb_max.x, bb_max.y, bb_max.z};
  mesh_.BindBuffers(
      index_buffer_->scenic_buffer(), kMeshIndexFormat, 0 /* index_offset */,
      num_indices_, vertex_buffer_->scenic_buffer(),
      scenic_lib::NewMeshVertexFormat(kMeshVertexPositionType,
                                      kMeshVertexNormalType,
                                      kMeshVertexTexCoodType),
      0 /* vertex_offset */, num_vertices_, bb_min_arr, bb_max_arr);
}

}  // namespace sketchy_service

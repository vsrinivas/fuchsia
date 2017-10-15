// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/sketchy/resources/stroke_group.h"

#include "lib/ui/scenic/fidl_helpers.h"

#include "lib/escher/escher.h"
#include "lib/escher/geometry/tessellation.h"
#include "lib/escher/shape/mesh.h"
#include "lib/escher/vk/gpu_mem.h"

namespace sketchy_service {

const ResourceTypeInfo StrokeGroup::kTypeInfo("StrokeGroup",
                                              ResourceType::kStrokeGroup,
                                              ResourceType::kResource);

StrokeGroup::StrokeGroup(scenic_lib::Session* session,
                         escher::BufferFactory* buffer_factory)
    : shape_node_(session),
      mesh_(session),
      material_(session),
      mesh_buffer_(session, buffer_factory) {
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
    stroke->TessellateAndMerge(command, buffer_factory, &mesh_buffer_);
  }
  mesh_buffer_.ProvideBuffersToScenicMesh(&mesh_);
}

}  // namespace sketchy_service

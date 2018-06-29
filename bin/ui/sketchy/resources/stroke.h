// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_SKETCHY_RESOURCES_STROKE_H_
#define GARNET_BIN_UI_SKETCHY_RESOURCES_STROKE_H_

#include "garnet/bin/ui/sketchy/buffer/appendable_buffer.h"
#include "garnet/bin/ui/sketchy/buffer/mesh_buffer.h"
#include "garnet/bin/ui/sketchy/frame.h"
#include "garnet/bin/ui/sketchy/resources/resource.h"
#include "garnet/bin/ui/sketchy/stroke/divided_stroke_path.h"
#include "garnet/bin/ui/sketchy/stroke/stroke_fitter.h"
#include "garnet/bin/ui/sketchy/stroke/stroke_path.h"
#include "garnet/bin/ui/sketchy/stroke/stroke_tessellator.h"
#include "lib/ui/scenic/cpp/resources.h"
#include "lib/ui/scenic/cpp/session.h"

namespace sketchy_service {

class Stroke;
using StrokePtr = fxl::RefPtr<Stroke>;

class Stroke final : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;
  const ResourceTypeInfo& type_info() const override { return kTypeInfo; }

  Stroke(StrokeTessellator* tessellator, escher::BufferFactory* buffer_factory);
  bool SetPath(std::unique_ptr<StrokePath> path);

  bool Begin(glm::vec2 pt);
  bool Extend(const std::vector<glm::vec2>& sampled_pts);
  bool Finish();

  // Record the command to tessellate and merge the mesh into a larger
  // |mesh_buffer|. Base vertex index will be updated per frame in the uniform
  // buffer, so the order change in |mesh_buffer| won't matter.
  void TessellateAndMerge(Frame* frame, MeshBuffer* mesh_buffer);

  uint32_t vertex_count() const { return stable_path_.vertex_count(); }
  uint32_t index_count() const { return stable_path_.index_count(); }

 private:
  void AppendPathToBuffers(escher::impl::CommandBuffer* command,
                           escher::BufferFactory* buffer_factory,
                           const DividedStrokePath& path, bool is_stable);

  // TODO(SCN-269): Document how tessellator and fitter work together.
  StrokeTessellator* const tessellator_;
  std::unique_ptr<StrokeFitter> fitter_;

  // The stable part of the path that is taken from the fitter. Will be updated
  // on |TessellateAndMerge()|.
  DividedStrokePath stable_path_;

  // The delta but stable part of the path that is taken from the fitter. Will
  // be updated on |Finish()| and |TessellateAndMerge()|. It is required because
  // at |Finish()|, we have no access to command buffer in order to update the
  // buffers. We have to keep this delta path until tessellation to update them.
  DividedStrokePath delta_stable_path_;

  // True if either path is reset or extended.
  bool is_path_updated_ = false;

  escher::BufferPtr stroke_info_buffer_;
  AppendableBuffer control_points_buffer_;
  AppendableBuffer re_params_buffer_;
  AppendableBuffer division_counts_buffer_;
  AppendableBuffer cumulative_division_counts_buffer_;
  AppendableBuffer division_segment_index_buffer_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Stroke);
};

}  // namespace sketchy_service

#endif  // GARNET_BIN_UI_SKETCHY_RESOURCES_STROKE_H_

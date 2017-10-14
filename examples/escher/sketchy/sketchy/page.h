// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <unordered_set>

#include "lib/escher/escher.h"
#include "lib/escher/impl/wobble_modifier_absorber.h"
#include "lib/escher/util/stopwatch.h"
#include "sketchy/stroke.h"

namespace sketchy {

// A |Page| contains a number of drawn |Strokes|.
class Page {
 public:
  Page(escher::Escher* escher);
  ~Page();

  // Instantiate a new |Stroke| with the specified ID; this ID must not
  // correspond to an existing stroke in the page.
  Stroke* NewStroke(StrokeId id);
  // Get the |Stroke| with the specified ID, or nullptr if none exists.
  Stroke* GetStroke(StrokeId id);

  // Delete the |Stroke| with the specified ID.  No-op if no such stroke exists.
  void DeleteStroke(StrokeId id);

  // Compute the number of vertices required to tessellate each segment of the
  // stroke path.
  std::vector<size_t> ComputeVertexCounts(const StrokePath& path);

  // Allows the page to be rendered by an escher::Renderer.
  escher::Model* GetModel(const escher::Stopwatch& stopwatch,
                          const escher::Stage* stage);

  // Clear all strokes, except those that are still being drawn.
  void Clear();

 private:
  friend class Stroke;
  void FinalizeStroke(StrokeId id);

  std::map<StrokeId, std::unique_ptr<Stroke>> strokes_;
  std::unordered_set<Stroke*> dirty_strokes_;

  static constexpr size_t kStrokeColorCount = 1000;

  escher::Escher* const escher_;
  escher::MaterialPtr page_material_;
  escher::MaterialPtr stroke_materials_[kStrokeColorCount];

  std::unique_ptr<escher::Model> model_;
  std::unique_ptr<escher::impl::WobbleModifierAbsorber> wobble_absorber_;
};

}  // namespace sketchy

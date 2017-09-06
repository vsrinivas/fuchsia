// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "escher/shape/mesh.h"
#include "sketchy/stroke_segment.h"

namespace sketchy {

class Page;

typedef uint64_t StrokeId;
typedef std::vector<StrokeSegment> StrokePath;

// Represents a stroke drawn on a |Page|.  The path of the stroke is represented
// as a piecewise cubic Bezier curve.  The renderable representation of the
// stroke is an escher::Mesh, which is tessellated based on the stroke's path
// and width.
class Stroke {
 public:
  static constexpr float kStrokeWidth = 60.f;  // pixels

  void Finalize();
  void SetPath(StrokePath path);

  StrokeId id() const { return id_; }
  const StrokePath& path() const { return path_; }
  const escher::MeshPtr& mesh() const { return mesh_; }
  float length() const { return length_; }
  bool finalized() const { return finalized_; }

 private:
  friend class Page;
  Stroke(Page* page, StrokeId id);

  void Tessellate();

  Page* const page_;
  const StrokeId id_;
  StrokePath path_;
  escher::MeshPtr mesh_;
  int vertex_count_ = 0;
  int offset_ = 0;
  float length_ = 0.f;
  std::atomic_bool finalized_;
};

}  // namespace sketchy

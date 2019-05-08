// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_PAPER_PAPER_SCENE_H_
#define SRC_UI_LIB_ESCHER_PAPER_PAPER_SCENE_H_

#include "src/ui/lib/escher/paper/paper_readme.h"

#include "src/ui/lib/escher/base/reffable.h"
#include "src/ui/lib/escher/geometry/bounding_box.h"
#include "src/ui/lib/escher/paper/paper_light.h"

namespace escher {

// PaperScene describes high-level scene parameters, such the number of point
// lights and their parameters, and the scene's bounding-box.
//
// NOTE: this is a placeholder; we may want to be more elaborate here.  Should
// this provide virtual methods?  Or should it become dumber: a non-reffable
// struct that is copied into PaperRenderer::FrameData.
class PaperScene : public Reffable {
 public:
  BoundingBox bounding_box;

  PaperAmbientLight ambient_light;
  std::vector<PaperPointLight> point_lights;

  float width() const { return bounding_box.width(); }
  float height() const { return bounding_box.height(); }
  float depth() const { return bounding_box.depth(); }

  size_t num_point_lights() const { return point_lights.size(); }
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_PAPER_PAPER_SCENE_H_

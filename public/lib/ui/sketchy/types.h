// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_SKETCHY_TYPES_H_
#define LIB_UI_SKETCHY_TYPES_H_

#include "lib/ui/fun/sketchy/fidl/types.fidl.h"
#include "lib/ui/sketchy/glm_hack.h"

namespace sketchy_lib {

// C++ wrapper for sketchy::CubicBezier2.
class CubicBezier2 {
 public:
  CubicBezier2(glm::vec2 pt0, glm::vec2 pt1,
               glm::vec2 pt2, glm::vec2 pt3);
  sketchy::CubicBezier2Ptr NewSketchyCubicBezier2() const;

 private:
  glm::vec2 pt0_;
  glm::vec2 pt1_;
  glm::vec2 pt2_;
  glm::vec2 pt3_;
};

// C++ wrapper for sketchy::StrokePath.
class StrokePath {
 public:
  explicit StrokePath(std::vector<CubicBezier2> segments);
  sketchy::StrokePathPtr NewSketchyStrokePath() const;

 private:
  std::vector<CubicBezier2> segments_;
};

}  // sketchy_lib

#endif  // LIB_UI_SKETCHY_TYPES_H_

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_SKETCHY_CLIENT_TYPES_H_
#define LIB_UI_SKETCHY_CLIENT_TYPES_H_

#include <fuchsia/ui/sketchy/cpp/fidl.h>

#include "lib/ui/sketchy/client/glm_hack.h"

namespace sketchy_lib {

// C++ wrapper for ::fuchsia::ui::sketchy::CubicBezier2.
class CubicBezier2 {
 public:
  CubicBezier2(glm::vec2 pt0, glm::vec2 pt1, glm::vec2 pt2, glm::vec2 pt3);
  ::fuchsia::ui::sketchy::CubicBezier2 NewSketchyCubicBezier2() const;

 private:
  glm::vec2 pt0_;
  glm::vec2 pt1_;
  glm::vec2 pt2_;
  glm::vec2 pt3_;
};

// C++ wrapper for ::fuchsia::ui::sketchy::StrokePath.
class StrokePath {
 public:
  explicit StrokePath(std::vector<CubicBezier2> segments);
  ::fuchsia::ui::sketchy::StrokePath NewSketchyStrokePath() const;

 private:
  std::vector<CubicBezier2> segments_;
};

}  // namespace sketchy_lib

#endif  // LIB_UI_SKETCHY_CLIENT_TYPES_H_

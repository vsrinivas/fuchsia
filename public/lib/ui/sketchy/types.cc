// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/sketchy/types.h"

namespace sketchy_lib {

CubicBezier2::CubicBezier2(glm::vec2 pt0, glm::vec2 pt1,
                           glm::vec2 pt2, glm::vec2 pt3)
    : pt0_(pt0), pt1_(pt1), pt2_(pt2), pt3_(pt3) {}

sketchy::CubicBezier2Ptr CubicBezier2::NewSketchyCubicBezier2() {
  auto sketchy_cubic_bezier2 = sketchy::CubicBezier2::New();
  sketchy_cubic_bezier2->pt0 = scenic::vec2::New();
  sketchy_cubic_bezier2->pt0->x = pt0_.x;
  sketchy_cubic_bezier2->pt0->y = pt0_.y;
  sketchy_cubic_bezier2->pt1 = scenic::vec2::New();
  sketchy_cubic_bezier2->pt1->x = pt1_.x;
  sketchy_cubic_bezier2->pt1->y = pt1_.y;
  sketchy_cubic_bezier2->pt2 = scenic::vec2::New();
  sketchy_cubic_bezier2->pt2->x = pt2_.x;
  sketchy_cubic_bezier2->pt2->y = pt2_.y;
  sketchy_cubic_bezier2->pt3 = scenic::vec2::New();
  sketchy_cubic_bezier2->pt3->x = pt3_.x;
  sketchy_cubic_bezier2->pt3->y = pt3_.y;
  return sketchy_cubic_bezier2;
}

StrokePath::StrokePath(std::vector<CubicBezier2> segments)
    : segments_(segments) {}

sketchy::StrokePathPtr StrokePath::NewSketchyStrokePath() {
  auto sketchy_segments =
      fidl::Array<sketchy::CubicBezier2Ptr>::New(0);
  for (auto& segment : segments_) {
    auto sketchy_segment = segment.NewSketchyCubicBezier2();
    sketchy_segments.push_back(std::move(sketchy_segment));
  }
  auto sketchy_stroke_path = sketchy::StrokePath::New();
  sketchy_stroke_path->segments = std::move(sketchy_segments);
  return sketchy_stroke_path;
}

}  // sketchy_lib

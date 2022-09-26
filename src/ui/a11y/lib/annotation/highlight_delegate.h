// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_ANNOTATION_HIGHLIGHT_DELEGATE_H_
#define SRC_UI_A11Y_LIB_ANNOTATION_HIGHLIGHT_DELEGATE_H_

#include <fuchsia/math/cpp/fidl.h>

namespace a11y {

// Interface for the object that interacts with Flatland to show or hide the
// accessibility highlight.
class HighlightDelegate {
 public:
  // Draw a highlight around the rectangular region specified (in LTRB rect format).
  // `top_left` and `bottom_right` should be given in the coordinate space of the
  // 'highlight view' in which the highlight will be drawn.
  virtual void DrawHighlight(fuchsia::math::PointF top_left,
                             fuchsia::math::PointF bottom_right) = 0;

  // Clears the current highlight (if any).
  virtual void ClearHighlight() = 0;
};

}  // namespace a11y
#endif  // SRC_UI_A11Y_LIB_ANNOTATION_HIGHLIGHT_DELEGATE_H_

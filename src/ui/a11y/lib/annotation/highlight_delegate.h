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
  // Draw an accessibility highlight (a brightly colored border) around the
  // rectangular region specified.
  // The highlight rectangles will be centered on the edges of the rectangle provided, so the
  // highlight will cover some pixels 'inside' and some pixels 'outside' the rectangle.
  //
  // `top_left` and `bottom_right` should be given in the coordinate space of
  // the 'highlight view', where accessibility highlights are drawn.
  //
  // The callback is for synchronization in tests.
  virtual void DrawHighlight(fuchsia::math::Point top_left, fuchsia::math::Point bottom_right,
                             fit::function<void()> callback) = 0;

  inline void DrawHighlight(fuchsia::math::Point top_left, fuchsia::math::Point bottom_right) {
    DrawHighlight(top_left, bottom_right, [] {});
  }

  // Clears the current highlight (if any).
  //
  // The callback is for synchronization in tests.
  virtual void ClearHighlight(fit::function<void()> callback) = 0;

  inline void ClearHighlight() {
    ClearHighlight([] {});
  }
};

}  // namespace a11y
#endif  // SRC_UI_A11Y_LIB_ANNOTATION_HIGHLIGHT_DELEGATE_H_

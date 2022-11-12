// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/annotation/tests/mocks/mock_highlight_delegate.h"

#include "src/ui/a11y/lib/util/util.h"

namespace accessibility_test {
void MockHighlightDelegate::DrawHighlight(fuchsia::math::PointF top_left,
                                          fuchsia::math::PointF bottom_right, zx_koid_t view_koid,
                                          fit::function<void()> callback) {
  current_highlight_.emplace(Highlight{top_left, bottom_right, view_koid});
  callback();
}

void MockHighlightDelegate::ClearHighlight(fit::function<void()> callback) {
  current_highlight_.reset();
  callback();
}

}  // namespace accessibility_test

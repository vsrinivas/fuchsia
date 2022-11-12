// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_ANNOTATION_TESTS_MOCKS_MOCK_HIGHLIGHT_DELEGATE_H_
#define SRC_UI_A11Y_LIB_ANNOTATION_TESTS_MOCKS_MOCK_HIGHLIGHT_DELEGATE_H_

#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <map>
#include <memory>
#include <optional>

#include "src/ui/a11y/lib/annotation/highlight_delegate.h"

namespace accessibility_test {

class MockHighlightDelegate : public a11y::HighlightDelegate {
 public:
  struct Highlight {
    fuchsia::math::PointF top_left;
    fuchsia::math::PointF bottom_right;
    zx_koid_t view_koid;
  };

  // |HighlightDelegate|
  void DrawHighlight(fuchsia::math::PointF top_left, fuchsia::math::PointF bottom_right,
                     zx_koid_t view_koid, fit::function<void()> callback) override;

  // |HighlightDelegate|
  void ClearHighlight(fit::function<void()> callback) override;

  std::optional<Highlight> get_current_highlight() { return current_highlight_; }

 private:
  std::optional<Highlight> current_highlight_;
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_ANNOTATION_TESTS_MOCKS_MOCK_HIGHLIGHT_DELEGATE_H_
